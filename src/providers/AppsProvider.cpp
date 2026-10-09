#include "providers/AppsProvider.h"

#include <algorithm>
#include <unordered_set>

#include "core/Fuzzy.h"
#include "core/Str.h"
#include "platform/Win.h"

#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace cs {
namespace {

constexpr const wchar_t* kCategory = L"Приложения";
constexpr const wchar_t* kAppsFolder = L"shell:AppsFolder\\";
constexpr size_t kMaxResults = 12;
constexpr float kMinScore = 0.3f;
constexpr float kKeywordWeight = 0.95f;
constexpr ULONGLONG kRescanMs = 5 * 60 * 1000;
constexpr ULONGLONG kDebounceMs = 2000;

// Declared locally: older MinGW import libraries lack some of these symbols.
constexpr GUID kFolderIdAppsFolder = {0x1e87508d, 0x89c2, 0x42f0, {0x8a, 0x7e, 0x64, 0x5a, 0x0f, 0x50, 0xca, 0x58}};
constexpr GUID kBhidEnumItems = {0x94f60519, 0x2850, 0x4924, {0xaa, 0x5a, 0xd1, 0x5e, 0x84, 0x86, 0x80, 0x39}};
constexpr PROPERTYKEY kPkeyLinkTargetParsingPath = {
    {0xb9b4b3fc, 0x2b51, 0x4a42, {0xb5, 0xd8, 0x32, 0x41, 0x46, 0xaf, 0xcf, 0x25}}, 2};

template <class T>
class Com {
 public:
  Com() = default;
  ~Com() {
    if (p_) p_->Release();
  }
  Com(const Com&) = delete;
  Com& operator=(const Com&) = delete;
  T** Put() { return &p_; }
  T* operator->() const { return p_; }
  explicit operator bool() const { return p_ != nullptr; }

 private:
  T* p_ = nullptr;
};

std::wstring TakeCoString(PWSTR s) {
  std::wstring r = s ? s : L"";
  CoTaskMemFree(s);
  return r;
}

std::wstring DisplayName(IShellItem* item, SIGDN kind) {
  PWSTR s = nullptr;
  return SUCCEEDED(item->GetDisplayName(kind, &s)) ? TakeCoString(s) : std::wstring();
}

// "{F38BF404-1D43-42F2-9305-67DE0B28FC23}\explorer.exe" -> "C:\Windows\explorer.exe"; "C:\x.exe" stays.
std::wstring TargetFromParsingName(const std::wstring& p) {
  if (p.size() > 3 && str::IsLetter(p[0]) && p[1] == L':' && p[2] == L'\\') return p;
  if (p.size() > 40 && p[0] == L'{' && p[37] == L'}' && p[38] == L'\\') {
    GUID id;
    if (SUCCEEDED(CLSIDFromString(p.substr(0, 38).c_str(), &id))) {
      std::wstring base = win::KnownFolder(id);
      if (!base.empty()) return base + p.substr(38);
    }
  }
  return {};
}

std::wstring_view FileName(std::wstring_view path) {
  size_t s = path.find_last_of(L"\\/");
  return s == std::wstring_view::npos ? path : path.substr(s + 1);
}

std::wstring_view Extension(std::wstring_view path) {
  std::wstring_view n = FileName(path);
  size_t d = n.rfind(L'.');
  return d == std::wstring_view::npos ? std::wstring_view() : n.substr(d);
}

bool Contains(std::wstring_view s, std::wstring_view part) { return s.find(part) != std::wstring_view::npos; }

// Uninstallers, readmes, help files and website links clutter the list.
bool IsJunk(std::wstring_view titleLower, std::wstring_view target, std::wstring_view parsingLower) {
  for (const wchar_t* w : {L"uninstall", L"удалить", L"удаление", L"деинсталл", L"readme", L"read me", L"website",
                           L"веб-сайт", L"release notes", L"documentation"})
    if (Contains(titleLower, w)) return true;
  if (str::StartsWith(parsingLower, L"http:") || str::StartsWith(parsingLower, L"https:")) return true;
  std::wstring_view ext = Extension(target);
  for (const wchar_t* e : {L".url", L".chm", L".txt", L".html", L".htm", L".pdf", L".rtf", L".md"})
    if (str::IEquals(ext, e)) return true;
  return false;
}

// Russian/English aliases for built-in apps, so "блокнот" finds Notepad on an English Windows (and vice versa).
struct Alias {
  const wchar_t* keywords;  // '|' separated, added to the app's keywords
  const wchar_t* exact;     // '|' separated: lowercase title or exe stem equal to one of these
  const wchar_t* contains;  // '|' separated: lowercase parsing name contains one of these (UWP package names)
};

constexpr Alias kAliases[] = {
    {L"блокнот|notepad", L"notepad|блокнот", L"windowsnotepad"},
    {L"калькулятор|calculator|calc", L"calculator|калькулятор|calc", L"windowscalculator"},
    {L"проводник|explorer|file explorer|файлы", L"file explorer|explorer|проводник", L"microsoft.windows.explorer"},
    {L"терминал|terminal|консоль|wt", L"terminal|windows terminal|wt|терминал", L"windowsterminal"},
    {L"командная строка|cmd|консоль|command prompt", L"command prompt|cmd|командная строка", L""},
    {L"диспетчер задач|task manager|taskmgr", L"task manager|taskmgr|диспетчер задач", L""},
    {L"панель управления|control panel", L"control panel|control|панель управления",
     L"microsoft.windows.controlpanel"},
    {L"параметры|настройки|settings", L"settings|параметры", L"windows.immersivecontrolpanel"},
    {L"фото|фотографии|photos", L"photos|фотографии|фото", L"microsoft.windows.photos"},
    {L"ножницы|snipping tool|скриншот|screenshot", L"snipping tool|ножницы|snippingtool", L"screensketch"},
    {L"paint|паинт|пэинт|рисование", L"paint|mspaint", L"microsoft.paint"},
    {L"браузер|edge|эдж", L"microsoft edge|msedge", L""},
    {L"хром|chrome|браузер", L"google chrome|chrome", L""},
    {L"ворд|word", L"word|winword", L""},
    {L"эксель|excel", L"excel", L""},
    {L"пауэрпоинт|powerpoint|презентация", L"powerpoint|powerpnt", L""},
    {L"аутлук|outlook|почта", L"outlook", L""},
    {L"телеграм|telegram", L"telegram|telegram desktop", L""},
    {L"камера|camera", L"camera|камера", L"windowscamera"},
    {L"часы|будильник|таймер|alarms|clock", L"clock|часы|alarms & clock", L"windowsalarms"},
    {L"магазин|store|microsoft store", L"microsoft store", L"windowsstore"},
    {L"музыка|медиаплеер|media player|music", L"media player|медиаплеер", L"zunemusic"},
    {L"кино|видео|movies", L"movies & tv|кино и тв", L"zunevideo"},
    {L"погода|weather", L"weather|погода", L"bingweather"},
    {L"вскод|vscode|code|visual studio code", L"visual studio code|code", L""},
};

bool AnyEquals(std::wstring_view list, std::wstring_view a, std::wstring_view b) {
  for (auto w : str::Split(list, L'|'))
    if (w == a || (!b.empty() && w == b)) return true;
  return false;
}

bool AnyContained(std::wstring_view list, std::wstring_view s) {
  for (auto w : str::Split(list, L'|'))
    if (Contains(s, w)) return true;
  return false;
}

void AddKeyword(std::vector<std::wstring>& kws, std::wstring_view titleLower, std::wstring_view kw) {
  if (kw.empty() || kw == titleLower) return;
  for (const auto& k : kws)
    if (k == kw) return;
  kws.emplace_back(kw);
}

uint64_t Fingerprint(const AppsProvider::AppList& list) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](const std::wstring& s) {
    for (wchar_t c : s) h = (h ^ uint64_t(c)) * 1099511628211ull;
    h = (h ^ 0xFFu) * 1099511628211ull;
  };
  for (const auto& a : list) {
    mix(a.title);
    mix(a.parsingName);
    mix(a.target);
  }
  return h;
}

}  // namespace

AppsProvider::AppsProvider() = default;
AppsProvider::~AppsProvider() { Shutdown(); }

void AppsProvider::Init(const Config&, IHost& host) {
  host_ = &host;
  stop_ = false;
  stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (stopEvent_) thread_ = std::thread([this] { Run(); });
}

void AppsProvider::Shutdown() {
  stop_ = true;
  if (stopEvent_) SetEvent(stopEvent_);
  if (thread_.joinable()) thread_.join();
  if (stopEvent_) {
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
  }
}

void AppsProvider::Run() {
  win::ComInit com;  // STA: the wait below pumps messages
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

  HANDLE handles[3] = {stopEvent_, nullptr, nullptr};
  DWORD count = 1;
  for (REFKNOWNFOLDERID id : {FOLDERID_CommonPrograms, FOLDERID_Programs}) {
    std::wstring dir = win::KnownFolder(id);
    if (dir.empty()) continue;
    HANDLE h = FindFirstChangeNotificationW(
        dir.c_str(), TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (h != INVALID_HANDLE_VALUE) handles[count++] = h;
  }

  uint64_t lastPrint = 0;
  auto rescan = [&] {
    auto list = Scan();
    if (!list || stop_) return;
    const uint64_t fp = Fingerprint(*list);
    if (fp == lastPrint) return;
    lastPrint = fp;
    {
      std::lock_guard<std::mutex> lk(mu_);
      apps_ = std::move(list);
    }
    host_->RequestRefresh();
  };

  rescan();
  ULONGLONG nextScan = GetTickCount64() + kRescanMs;
  while (!stop_) {
    const ULONGLONG now = GetTickCount64();
    const DWORD timeout = nextScan > now ? DWORD(nextScan - now) : 0;
    const DWORD r = MsgWaitForMultipleObjects(count, handles, FALSE, timeout, QS_ALLINPUT);
    if (r == WAIT_OBJECT_0) break;
    if (r > WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + count) {
      FindNextChangeNotification(handles[r - WAIT_OBJECT_0]);
      nextScan = GetTickCount64() + kDebounceMs;  // installers touch many files: wait until it settles
    } else if (r == WAIT_OBJECT_0 + count) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    } else if (r == WAIT_TIMEOUT) {
      rescan();
      nextScan = GetTickCount64() + kRescanMs;
    } else {
      break;  // WAIT_FAILED
    }
  }
  for (DWORD i = 1; i < count; ++i) FindCloseChangeNotification(handles[i]);
}

std::shared_ptr<AppsProvider::AppList> AppsProvider::Scan() {
  Com<IShellItem> folder;
  if (FAILED(SHGetKnownFolderItem(kFolderIdAppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(folder.Put()))))
    return nullptr;
  Com<IEnumShellItems> items;
  if (FAILED(folder->BindToHandler(nullptr, kBhidEnumItems, IID_PPV_ARGS(items.Put())))) return nullptr;

  auto list = std::make_shared<AppList>();
  std::unordered_set<std::wstring> seen;
  for (;;) {
    if (stop_) return nullptr;
    Com<IShellItem> item;
    ULONG got = 0;
    if (items->Next(1, item.Put(), &got) != S_OK || !got) break;

    App a;
    a.title = DisplayName(item.operator->(), SIGDN_NORMALDISPLAY);
    a.parsingName = DisplayName(item.operator->(), SIGDN_PARENTRELATIVEPARSING);
    if (a.title.empty() || a.parsingName.empty()) continue;
    const std::wstring parsingLower = str::ToLower(a.parsingName);
    if (!seen.insert(parsingLower).second) continue;

    Com<IShellItem2> item2;
    if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(item2.Put())))) {
      PWSTR s = nullptr;
      if (SUCCEEDED(item2->GetString(kPkeyLinkTargetParsingPath, &s))) a.target = TakeCoString(s);
    }
    if (a.target.empty()) a.target = TargetFromParsingName(a.parsingName);
    a.uwp = a.target.empty() && Contains(a.parsingName, L"!") && !Contains(a.parsingName, L"\\");
    a.titleLower = str::ToLower(a.title);
    if (IsJunk(a.titleLower, a.target, parsingLower)) continue;

    // Keywords: exe stem ("chrome", "msedge", "winword", "wt"), then aliases.
    std::wstring stem;
    if (!a.target.empty()) {
      std::wstring_view fn = FileName(a.target);
      std::wstring_view ext = Extension(fn);
      stem = str::ToLower(fn.substr(0, fn.size() - ext.size()));
      AddKeyword(a.keywords, a.titleLower, stem);
    }
    for (const Alias& al : kAliases) {
      if (AnyEquals(al.exact, a.titleLower, stem) || (*al.contains && AnyContained(al.contains, parsingLower)))
        for (auto kw : str::Split(al.keywords, L'|')) AddKeyword(a.keywords, a.titleLower, kw);
    }
    list->push_back(std::move(a));
  }
  std::sort(list->begin(), list->end(), [](const App& x, const App& y) {
    return x.titleLower != y.titleLower ? x.titleLower < y.titleLower : x.parsingName < y.parsingName;
  });
  return list;
}

void AppsProvider::Search(std::wstring_view query, std::vector<Result>& out) {
  std::shared_ptr<const AppList> apps;
  {
    std::lock_guard<std::mutex> lk(mu_);
    apps = apps_;
  }
  if (!apps || apps->empty()) return;
  const fuzzy::Query q = fuzzy::Prepare(query);
  if (q.lower.empty()) return;

  cands_.clear();
  for (size_t i = 0; i < apps->size(); ++i) {
    const App& a = (*apps)[i];
    float s = fuzzy::ScoreLowered(q, a.title, a.titleLower);
    for (size_t k = 0; k < a.keywords.size() && s < kKeywordWeight; ++k) {
      float ks = fuzzy::ScoreLowered(q, a.keywords[k], a.keywords[k]);
      if (ks >= 0.5f) s = std::max(s, ks * kKeywordWeight);  // keyword subsequence matches are noise
    }
    if (s >= kMinScore) cands_.push_back({uint32_t(i), s});
  }
  const size_t n = std::min(cands_.size(), kMaxResults);
  std::partial_sort(cands_.begin(), cands_.begin() + std::ptrdiff_t(n), cands_.end(),
                    [&](const Cand& x, const Cand& y) {
                      if (x.score != y.score) return x.score > y.score;
                      return (*apps)[x.index].title.size() < (*apps)[y.index].title.size();
                    });
  for (size_t i = 0; i < n; ++i) {
    const App& a = (*apps)[cands_[i].index];
    Result r;
    r.title = a.title;
    r.subtitle = a.uwp ? L"Приложение Microsoft Store" : L"Приложение";
    r.category = kCategory;
    r.iconKind = IconKind::ShellItem;
    r.icon = kAppsFolder + a.parsingName;
    r.score = cands_[i].score;
    r.actions = kActOpen | kActCopy;
    if (!a.uwp) r.actions = uint8_t(r.actions | kActAdmin);
    if (!a.target.empty()) r.actions = uint8_t(r.actions | kActReveal);
    r.key = L"app:" + a.parsingName;
    r.payload = a.parsingName;
    r.copyText = a.target.empty() ? a.title : a.target;
    out.push_back(std::move(r));
  }
}

ExecResult AppsProvider::Execute(const Result& r, Action a) {
  const std::wstring shellPath = kAppsFolder + r.payload;
  const bool hasTarget = (r.actions & kActReveal) != 0;  // copyText holds the target then
  switch (a) {
    case Action::Open:
      win::ShellOpen(shellPath);
      break;
    case Action::RunAsAdmin:
      if (!(r.actions & kActAdmin)) {
        win::ShellOpen(shellPath);  // UWP apps cannot be elevated this way
      } else if (hasTarget && str::IEquals(Extension(r.copyText), L".exe")) {
        win::ShellOpen(r.copyText, {}, true);
      } else {
        win::ShellOpen(shellPath, {}, true);
      }
      break;
    case Action::Reveal:
      if (hasTarget) win::RevealInExplorer(r.copyText);
      break;
    case Action::Copy:
      host_->CopyToClipboard(r.copyText);
      break;
  }
  return {};
}

}  // namespace cs
