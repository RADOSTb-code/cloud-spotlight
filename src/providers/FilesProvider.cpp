#include "providers/FilesProvider.h"

#include <algorithm>

#include "core/Config.h"
#include "core/Fuzzy.h"
#include "core/Str.h"
#include "platform/Win.h"

#include <knownfolders.h>
#include <shlobj.h>

namespace cs {
namespace {

constexpr const wchar_t* kCatFolders = L"Папки";
constexpr const wchar_t* kCatFiles = L"Файлы";
constexpr float kFolderWeight = 0.84f;
constexpr float kFileWeight = 0.8f;
constexpr float kPathModeScore = 0.97f;
constexpr size_t kPathModeMax = 15;
constexpr size_t kPathModeMaxListed = 20000;  // huge directories are truncated in path mode
constexpr DWORD kStartupDelayMs = 1500;       // let the app finish starting before hammering the disk

uint16_t TodayDays() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  return FileIndex::DaysFromFileTime((uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime);
}

bool IsDotName(const wchar_t* n) { return n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0)); }

// Junctions/symlinks to directories can create cycles or duplicate whole trees: skip them. Cloud placeholders
// (OneDrive Files On-Demand) are reparse points too, but they are regular folders and must be indexed.
bool SkipEntry(const WIN32_FIND_DATAW& fd) {
  const DWORD a = fd.dwFileAttributes;
  if (a & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) return true;
  if ((a & FILE_ATTRIBUTE_REPARSE_POINT) && (a & FILE_ATTRIBUTE_DIRECTORY))
    return fd.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT || fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
  return false;
}

bool HasAdminExt(std::wstring_view path) {
  for (const wchar_t* ext : {L".exe", L".bat", L".cmd", L".ps1", L".msi"})
    if (path.size() > 4 && str::IEquals(path.substr(path.size() - std::wstring_view(ext).size()), ext)) return true;
  return false;
}

std::wstring StripTrailingSep(std::wstring p) {
  while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
  return p;
}

}  // namespace

FilesProvider::FilesProvider() = default;
FilesProvider::~FilesProvider() { Shutdown(); }

void FilesProvider::Init(const Config& cfg, IHost& host) {
  host_ = &host;
  profile_ = win::ExpandEnv(L"%USERPROFILE%");
  maxDepth_ = std::clamp(cfg.fileMaxDepth, 1, FileIndex::kMaxDepth);
  reindexMinutes_ = std::max(1, cfg.fileReindexMinutes);
  maxEntries_ = size_t(std::max(1000, cfg.fileMaxEntries));
  for (const auto& e : cfg.fileExclude)
    if (!e.empty()) exclude_.push_back(str::ToLower(e));

  std::vector<std::wstring> roots = cfg.fileRoots;
  if (roots.empty())
    for (REFKNOWNFOLDERID id : {FOLDERID_Desktop, FOLDERID_Documents, FOLDERID_Downloads})
      roots.push_back(win::KnownFolder(id));
  // Normalise, drop missing and nested duplicates (a root inside another root is walked once).
  for (auto& r : roots) {
    r = StripTrailingSep(win::ExpandEnv(r));
    std::replace(r.begin(), r.end(), L'/', L'\\');
  }
  for (const auto& r : roots) {
    if (r.empty() || !win::DirExists(r)) continue;
    bool nested = false;
    for (const auto& o : roots)
      if (&o != &r && !o.empty() && o.size() <= r.size() && str::IStartsWith(r, o) &&
          (o.size() == r.size() ? &o < &r : (r[o.size()] == L'\\' || o.back() == L'\\')))
        nested = true;
    if (!nested) roots_.push_back(r);
  }

  stop_ = false;
  stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!roots_.empty() && stopEvent_) thread_ = std::thread([this] { Run(); });
}

void FilesProvider::Shutdown() {
  stop_ = true;
  if (stopEvent_) SetEvent(stopEvent_);
  if (thread_.joinable()) thread_.join();
  if (stopEvent_) {
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
  }
}

void FilesProvider::Run() {
  // Low CPU/IO/memory priority for the whole walker thread.
  SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
  uint64_t lastPrint = 0;
  DWORD wait = kStartupDelayMs;
  while (WaitForSingleObject(stopEvent_, wait) == WAIT_TIMEOUT) {
    auto idx = BuildIndex();
    if (stop_) break;
    const uint64_t fp = idx->Fingerprint();
    if (fp != lastPrint) {
      lastPrint = fp;
      {
        std::lock_guard<std::mutex> lk(mu_);
        index_ = std::move(idx);
      }
      host_->RequestRefresh();
    }
    wait = DWORD(reindexMinutes_) * 60u * 1000u;
  }
  SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
}

bool FilesProvider::IsExcluded(std::wstring_view name) const {
  for (const auto& e : exclude_)
    if (str::IEquals(name, e)) return true;
  return false;
}

std::shared_ptr<const FileIndex> FilesProvider::BuildIndex() {
  auto idx = std::make_shared<FileIndex>(L'\\');
  {
    std::lock_guard<std::mutex> lk(mu_);  // size the new pools like the previous index
    if (index_) idx->Reserve(index_->Size() + index_->Size() / 8, (index_->Size() + index_->Size() / 8) * 24);
  }
  struct Pending {
    uint32_t ref;
    int depth;  // depth of the directory itself (root = 0)
    std::wstring path;
  };
  std::vector<Pending> queue;  // BFS: shallow entries win when maxEntries_ truncates
  for (const auto& r : roots_) queue.push_back({idx->AddRoot(r), 0, r});
  WIN32_FIND_DATAW fd;
  std::wstring pattern;
  for (size_t qi = 0; qi < queue.size() && idx->Size() < maxEntries_; ++qi) {
    if (stop_) break;
    // copy out: push_back below may reallocate `queue`
    const uint32_t dirRef = queue[qi].ref;
    const int depth = queue[qi].depth;
    std::wstring dirPath = std::move(queue[qi].path);
    pattern.assign(dirPath).append(L"\\*");
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) continue;
    do {
      if (IsDotName(fd.cFileName) || SkipEntry(fd)) continue;
      const bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      std::wstring_view name(fd.cFileName);
      if (IsExcluded(name)) continue;
      const uint64_t ft = (uint64_t(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
      const uint32_t ref = idx->Add(dirRef, name, dir, FileIndex::DaysFromFileTime(ft));
      if (dir && ref != FileIndex::kInvalid && depth + 1 < maxDepth_) {
        std::wstring child;
        child.reserve(dirPath.size() + 1 + name.size());
        child.append(dirPath).append(L"\\").append(name);
        queue.push_back({ref, depth + 1, std::move(child)});
      }
    } while (!stop_ && idx->Size() < maxEntries_ && FindNextFileW(h, &fd));
    FindClose(h);
  }
  idx->Finish();
  return idx;
}

std::wstring FilesProvider::Pretty(std::wstring_view path) const {
  if (!profile_.empty() && str::IStartsWith(path, profile_) &&
      (path.size() == profile_.size() || path[profile_.size()] == L'\\'))
    return L"~" + std::wstring(path.substr(profile_.size()));
  return std::wstring(path);
}

Result FilesProvider::MakeResult(std::wstring fullPath, std::wstring_view name, std::wstring subtitle, bool dir,
                                 float score) const {
  Result r;
  r.title.assign(name);
  r.subtitle = std::move(subtitle);
  r.category = dir ? kCatFolders : kCatFiles;
  r.iconKind = IconKind::FilePath;
  r.icon = fullPath;
  r.score = score;
  r.actions = kActOpen | kActReveal | kActCopy;
  if (!dir && HasAdminExt(fullPath)) r.actions = uint8_t(r.actions | kActAdmin);
  r.key = L"file:" + fullPath;
  r.copyText = fullPath;
  r.payload = std::move(fullPath);
  return r;
}

void FilesProvider::Search(std::wstring_view query, std::vector<Result>& out) {
  if (pathq::LooksLikePath(query)) {
    SearchInDirectory(query, out);
    return;
  }
  if (query.size() < 2) return;
  std::shared_ptr<const FileIndex> idx;
  {
    std::lock_guard<std::mutex> lk(mu_);
    idx = index_;
  }
  if (!idx) return;
  const fuzzy::Query q = fuzzy::Prepare(query);
  FileIndex::SearchOptions opt;
  opt.today = TodayDays();
  hitFiles_.clear();
  hitDirs_.clear();
  idx->Search(q, opt, hitFiles_, hitDirs_);
  for (const auto& h : hitDirs_)
    out.push_back(MakeResult(idx->FullPath(h.index), idx->Name(h.index), Pretty(idx->ParentPath(h.index)), true,
                             h.score * kFolderWeight));
  for (const auto& h : hitFiles_)
    out.push_back(MakeResult(idx->FullPath(h.index), idx->Name(h.index), Pretty(idx->ParentPath(h.index)), false,
                             h.score * kFileWeight));
}

// "C:\Users\me\Doc" -> children of C:\Users\me\ matching "Doc". Synchronous: a single directory listing, cached
// while the user keeps typing inside the same directory.
void FilesProvider::SearchInDirectory(std::wstring_view query, std::vector<Result>& out) {
  std::wstring expanded = win::ExpandEnv(query);
  std::replace(expanded.begin(), expanded.end(), L'/', L'\\');
  auto [dirView, prefix] = pathq::SplitDirAndPrefix(expanded);
  if (dirView.empty()) return;
  const std::wstring dir(dirView);

  const unsigned long long now = GetTickCount64();
  if (!str::IEquals(dir, listedDir_) || now - listedAt_ > 3000) {
    listedDir_ = dir;
    listedAt_ = now;
    listed_.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
      do {
        if (IsDotName(fd.cFileName) || (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)) continue;
        DirChild c{fd.cFileName, str::ToLower(fd.cFileName), (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
                   (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0};
        listed_.push_back(std::move(c));
      } while (listed_.size() < kPathModeMaxListed && FindNextFileW(h, &fd));
      FindClose(h);
    }
  }
  if (listed_.empty()) return;

  struct Cand {
    const DirChild* c;
    float s;
  };
  std::vector<Cand> cands;
  cands.reserve(listed_.size());
  const fuzzy::Query q = fuzzy::Prepare(prefix);
  for (const auto& c : listed_) {
    if (c.hidden && q.lower.empty()) continue;  // hidden items (AppData, ProgramData) only when typed
    float s = q.lower.empty() ? 1.f : fuzzy::ScoreLowered(q, c.name, c.lower);
    if (s >= 0.3f) cands.push_back({&c, s});
  }
  const size_t n = std::min(cands.size(), kPathModeMax);
  std::partial_sort(cands.begin(), cands.begin() + std::ptrdiff_t(n), cands.end(), [](const Cand& a, const Cand& b) {
    if (a.s != b.s) return a.s > b.s;
    if (a.c->dir != b.c->dir) return a.c->dir;  // folders first
    return a.c->lower < b.c->lower;
  });
  for (size_t i = 0; i < n; ++i) {
    const DirChild& c = *cands[i].c;
    std::wstring full = dir + c.name;
    // Folders get a trailing '\' in payload so Tab-completion (if it uses payload) continues into the folder.
    Result r = MakeResult(full, c.name, Pretty(full), c.dir, kPathModeScore - 0.001f * float(i));
    if (c.dir) r.payload += L'\\';
    out.push_back(std::move(r));
  }
}

ExecResult FilesProvider::Execute(const Result& r, Action a) {
  const std::wstring path = StripTrailingSep(r.payload);
  switch (a) {
    case Action::Open:
      win::ShellOpen(path);
      break;
    case Action::Reveal:
      win::RevealInExplorer(path);
      break;
    case Action::RunAsAdmin: {
      if (path.size() > 4 && str::IEquals(std::wstring_view(path).substr(path.size() - 4), L".ps1"))
        win::ShellOpen(L"powershell.exe", L"-NoExit -File \"" + path + L"\"", true);
      else
        win::ShellOpen(path, {}, true);
      break;
    }
    case Action::Copy:
      host_->CopyToClipboard(path);
      break;
  }
  return {};
}

}  // namespace cs
