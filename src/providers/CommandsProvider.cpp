#include "providers/CommandsProvider.h"

#include "platform/Win.h"  // <windows.h> first

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <knownfolders.h>
#include <powrprof.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cwchar>

#include "core/Fuzzy.h"
#include "core/Str.h"

namespace cs {
namespace {

using cmd::Kind;

constexpr const wchar_t* kCategory = L"Команды";
constexpr int64_t kMaxShutdownDelay = 315360000;  // shutdown.exe /t limit (10 years)

const wchar_t* Glyph(Kind k) {
  switch (k) {
    case Kind::Shutdown: return L"";
    case Kind::Restart: return L"";
    case Kind::CancelShutdown: case Kind::CancelTimer: return L"";
    case Kind::Sleep: case Kind::Hibernate: return L"";
    case Kind::Lock: return L"";
    case Kind::SignOut: return L"";
    case Kind::EmptyRecycleBin: return L"";
    case Kind::MonitorOff: case Kind::Screensaver: return L"";
    case Kind::Timer: return L"";
    case Kind::Uuid: return L"";
    case Kind::Password: return L"";
    case Kind::Ip: return L"";
    case Kind::None: break;
  }
  return L"";
}

const wchar_t* Description(Kind k) {
  switch (k) {
    case Kind::Shutdown: return L"Сейчас · Enter — выключить";
    case Kind::Restart: return L"Сейчас · Enter — перезагрузить";
    case Kind::Sleep: return L"Перевести компьютер в спящий режим";
    case Kind::Hibernate: return L"Сохранить сеанс на диск и выключить питание";
    case Kind::Lock: return L"Экран блокировки (Win+L)";
    case Kind::SignOut: return L"Завершить сеанс пользователя";
    case Kind::EmptyRecycleBin: return L"Удалить файлы из корзины навсегда";
    case Kind::MonitorOff: return L"Погасить экран — движение мыши включит его снова";
    case Kind::Screensaver: return L"Запустить экранную заставку";
    default: return L"";
  }
}

int NowSecOfDay() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  return st.wHour * 3600 + st.wMinute * 60 + st.wSecond;
}

// Local clock time `secs` from now: "15:42", "завтра 07:00".
std::wstring ClockIn(int64_t secs) {
  SYSTEMTIME now, then;
  GetLocalTime(&now);
  FILETIME ft;
  SystemTimeToFileTime(&now, &ft);
  ULARGE_INTEGER u{};
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  u.QuadPart += uint64_t(secs) * 10000000ULL;
  ft.dwLowDateTime = u.LowPart;
  ft.dwHighDateTime = u.HighPart;
  FileTimeToSystemTime(&ft, &then);
  std::wstring t = cmd::FormatClock(then.wHour, then.wMinute);
  if (then.wDay == now.wDay && then.wMonth == now.wMonth && then.wYear == now.wYear) return t;
  if (secs < 2 * 86400) return L"завтра в " + t;
  wchar_t buf[16];
  swprintf(buf, 16, L"%02u.%02u ", unsigned(then.wDay), unsigned(then.wMonth));
  return buf + t;
}

std::wstring At(const std::wstring& clock) { return str::StartsWith(clock, L"завтра") ? clock : L"в " + clock; }

// Runs a system tool hidden and waits for it (shutdown.exe returns quickly). Returns the exit code or -1.
DWORD RunAndWait(std::wstring_view exe, std::wstring_view args, DWORD timeoutMs = 5000) {
  wchar_t sys[MAX_PATH];
  UINT n = GetSystemDirectoryW(sys, MAX_PATH);
  if (!n || n >= MAX_PATH) return DWORD(-1);
  std::wstring cmdline = L"\"" + std::wstring(sys, n) + L"\\" + std::wstring(exe) + L"\" " + std::wstring(args);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                      &pi))
    return DWORD(-1);
  CloseHandle(pi.hThread);
  DWORD code = DWORD(-1);
  if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  return code;
}

std::vector<std::wstring> SplitNames(const wchar_t* names) {
  std::vector<std::wstring> r;
  for (auto v : str::Split(names, L'|')) r.emplace_back(v);
  return r;
}

std::wstring BuiltinPath(cmd::KnownFolder id, bool& shell) {
  shell = false;
  switch (id) {
    case cmd::KnownFolder::Downloads: return win::KnownFolder(FOLDERID_Downloads);
    case cmd::KnownFolder::Documents: return win::KnownFolder(FOLDERID_Documents);
    case cmd::KnownFolder::Desktop: return win::KnownFolder(FOLDERID_Desktop);
    case cmd::KnownFolder::Pictures: return win::KnownFolder(FOLDERID_Pictures);
    case cmd::KnownFolder::Music: return win::KnownFolder(FOLDERID_Music);
    case cmd::KnownFolder::Videos: return win::KnownFolder(FOLDERID_Videos);
    case cmd::KnownFolder::AppData: return win::KnownFolder(FOLDERID_RoamingAppData);
    case cmd::KnownFolder::LocalAppData: return win::KnownFolder(FOLDERID_LocalAppData);
    case cmd::KnownFolder::Temp: {
      std::wstring t = win::ExpandEnv(L"%TEMP%");
      while (t.size() > 3 && (t.back() == L'\\' || t.back() == L'/')) t.pop_back();
      return t;
    }
    case cmd::KnownFolder::ProgramFiles: return win::KnownFolder(FOLDERID_ProgramFiles);
    case cmd::KnownFolder::Startup: return win::KnownFolder(FOLDERID_Startup);
    case cmd::KnownFolder::UserProfile: return win::KnownFolder(FOLDERID_Profile);
    case cmd::KnownFolder::RecycleBin: shell = true; return L"shell:RecycleBinFolder";
    case cmd::KnownFolder::ThisPC: shell = true; return L"shell:MyComputerFolder";
  }
  return {};
}

bool RandomBytes(void* buf, size_t n) {
  return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, static_cast<PUCHAR>(buf), ULONG(n),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

std::wstring NewGuid() {
  GUID g;
  if (FAILED(CoCreateGuid(&g))) return {};
  wchar_t buf[64];
  int n = StringFromGUID2(g, buf, 64);
  if (n < 3) return {};
  std::wstring s(buf + 1, size_t(n) - 3);  // strip braces and the terminator
  return str::ToLower(s);
}

}  // namespace

CommandsProvider::~CommandsProvider() { Shutdown(); }

void CommandsProvider::Init(const Config& cfg, IHost& host) {
  host_ = &host;
  folders_.clear();
  for (const auto& f : cfg.folders) {
    if (f.name.empty() || f.path.empty()) continue;
    folders_.push_back({f.name, f.path, {f.name}, false});
  }
  for (const auto& b : cmd::BuiltinFolders()) {
    Folder f;
    f.title = b.title;
    f.path = BuiltinPath(b.id, f.shell);
    if (f.path.empty()) continue;
    f.names = SplitNames(b.names);
    folders_.push_back(std::move(f));
  }
  custom_ = cfg.commands;
  customIcons_.clear();
  for (const auto& c : custom_) {
    bool file = c.target.size() > 2 && c.target[1] == L':' && win::FileExists(c.target);  // absolute paths only
    customIcons_.push_back(file ? c.target : std::wstring());
  }
}

void CommandsProvider::Shutdown() {
  {
    std::lock_guard lk(tmu_);
    tstop_ = true;
  }
  tcv_.notify_all();
  if (tthread_.joinable()) tthread_.join();
}

// ---- search -------------------------------------------------------------------------------------------------

void CommandsProvider::Search(std::wstring_view query, std::vector<Result>& out) {
  if (query.size() > 200) return;
  cmd::Command c;
  if (cmd::Parse(query, NowSecOfDay(), c)) {
    if (c.kind == Kind::Uuid || c.kind == Kind::Password) {
      if (genQuery_ != query) genQuery_.assign(query), genValue_.clear();
    }
    AddCommand(c, 0.96f, out);
  } else {
    std::vector<std::pair<Kind, float>> sugg;
    cmd::Suggest(query, sugg);
    for (auto& [k, s] : sugg) {
      if (k == Kind::Uuid || k == Kind::Password) continue;  // generated values only for explicit phrases
      cmd::Command sc;
      sc.kind = k;
      AddCommand(sc, s * 0.95f, out);
    }
  }
  AddFolders(query, out);
  AddCustom(query, out);
}

void CommandsProvider::AddCommand(const cmd::Command& c, float score, std::vector<Result>& out) {
  Result r;
  r.category = kCategory;
  r.iconKind = IconKind::Glyph;
  r.icon = Glyph(c.kind);
  r.score = score;
  r.actions = kActOpen;
  const bool delayed = c.delaySec >= 0;
  std::wstring when, at;
  if (delayed) {
    when = c.atClock ? L"в " + cmd::FormatClock(c.clockH, c.clockM) : L"через " + cmd::FormatDuration(c.delaySec);
    at = ClockIn(c.delaySec);
  }
  std::wstring payload = L"c|" + std::to_wstring(int(c.kind)) + L"|" + std::to_wstring(c.delaySec) + L"|";

  switch (c.kind) {
    case Kind::Shutdown:
    case Kind::Restart:
      r.title = cmd::Title(c.kind);
      if (delayed) {
        r.title += L" " + when;
        r.subtitle = (c.atClock ? L"через " + cmd::FormatDuration(c.delaySec) : At(at)) +
                     L" · Enter — запланировать, отменить: «отменить выключение»";
      } else {
        r.subtitle = Description(c.kind);
      }
      break;
    case Kind::CancelShutdown:
      r.title = cmd::Title(c.kind);
      r.subtitle = shutdownAt_.empty() ? L"shutdown /a" : L"Запланировано " + At(shutdownAt_);
      break;
    case Kind::Sleep: case Kind::Hibernate: case Kind::Lock: case Kind::SignOut: case Kind::MonitorOff:
      r.title = cmd::Title(c.kind);
      if (delayed) {
        r.title += L" " + when;
        r.subtitle = At(at) + L" · Enter — запланировать, отменить: «отменить таймер»";
      } else {
        r.subtitle = Description(c.kind);
      }
      break;
    case Kind::EmptyRecycleBin:
    case Kind::Screensaver:
      r.title = cmd::Title(c.kind);
      r.subtitle = Description(c.kind);
      break;
    case Kind::Timer:
      if (!delayed) {
        r.title = L"Таймер";
        r.subtitle = L"Укажите время: «таймер 5 минут», «напомни через 10 минут позвонить»";
        r.payload = L"t";
        out.push_back(std::move(r));
        return;
      }
      r.title = c.label.empty() ? L"Таймер на " + cmd::FormatDuration(c.delaySec)
                                : L"Напомнить «" + c.label + L"» " + when;
      r.subtitle = L"Сработает " + At(at) + L" · Enter — запустить";
      payload += c.label;
      break;
    case Kind::CancelTimer: {
      std::wstring next;
      size_t n = ActiveTimers(&next);
      r.title = cmd::Title(c.kind);
      r.subtitle = n ? L"Активных: " + std::to_wstring(n) + L", ближайший " + At(next) : L"Нет активных таймеров";
      break;
    }
    case Kind::Uuid:
    case Kind::Password: {
      if (genValue_.empty() || score < 0.96f)
        genValue_ = c.kind == Kind::Uuid ? NewGuid() : Password(c.length ? c.length : 16);
      if (genValue_.empty()) return;
      r.title = genValue_;
      r.subtitle = c.kind == Kind::Uuid
                       ? L"Новый GUID · Enter — скопировать"
                       : L"Случайный пароль, " + std::to_wstring(genValue_.size()) + L" " +
                             cmd::PluralRu(int64_t(genValue_.size()), L"символ", L"символа", L"символов") +
                             L" · Enter — скопировать";
      r.actions = kActOpen | kActCopy;
      r.payload = L"v|" + genValue_;
      r.copyText = genValue_;
      out.push_back(std::move(r));
      return;
    }
    case Kind::Ip: {
      std::wstring ips = LocalIps();
      if (ips.empty()) {
        r.title = L"Нет активного сетевого подключения";
        r.actions = 0;
        out.push_back(std::move(r));
        return;
      }
      size_t comma = ips.find(L", ");
      r.title = ips.substr(0, comma);
      r.subtitle = L"Локальный IP-адрес";
      if (comma != std::wstring::npos) r.subtitle += L" · также " + ips.substr(comma + 2);
      r.actions = kActOpen | kActCopy;
      r.payload = L"v|" + r.title;
      r.copyText = r.title;
      r.key = L"cmd:ip";
      out.push_back(std::move(r));
      return;
    }
    case Kind::None:
      return;
  }
  if (!delayed) r.key = L"cmd:" + std::to_wstring(int(c.kind));
  r.payload = std::move(payload);
  out.push_back(std::move(r));
}

void CommandsProvider::AddFolders(std::wstring_view query, std::vector<Result>& out) {
  cmd::FolderPhrase fp;
  if (!cmd::ParseFolderPhrase(query, fp)) return;
  if (!fp.explicitPhrase && fp.target.size() < 3) return;
  struct Hit { float score; size_t idx; };
  Hit hits[3];
  size_t nh = 0;
  for (size_t i = 0; i < folders_.size(); ++i) {
    float best = 0;
    for (const auto& name : folders_[i].names) best = std::max(best, cmd::FolderNameScore(fp.target, name));
    float s;
    if (best >= 0.999f) s = fp.explicitPhrase ? 0.96f : 0.9f;
    else if (best >= 0.6f) s = best * 0.9f;
    else continue;
    // keep the top 3 (insertion into a tiny sorted array)
    size_t pos = nh;
    while (pos > 0 && hits[pos - 1].score < s) --pos;
    if (pos >= 3) continue;
    for (size_t k = std::min<size_t>(nh, 2); k > pos; --k) hits[k] = hits[k - 1];
    hits[pos] = {s, i};
    nh = std::min<size_t>(nh + 1, 3);
  }
  for (size_t k = 0; k < nh; ++k) {
    const Folder& f = folders_[hits[k].idx];
    Result r;
    r.title = f.title;
    r.subtitle = f.shell ? L"Системная папка" : f.path;
    r.category = L"Папки";
    r.iconKind = f.shell ? IconKind::ShellItem : IconKind::FilePath;
    r.icon = f.path;
    r.score = hits[k].score;
    r.actions = f.shell ? uint8_t(kActOpen) : uint8_t(kActOpen | kActCopy | kActReveal);
    // Same key scheme as FilesProvider so the folder isn't listed twice.
    std::wstring keyPath = f.path;
    while (keyPath.size() > 3 && (keyPath.back() == L'\\' || keyPath.back() == L'/')) keyPath.pop_back();
    r.key = L"file:" + keyPath;
    r.payload = L"f|" + f.path;
    r.copyText = f.path;
    out.push_back(std::move(r));
  }
}

void CommandsProvider::AddCustom(std::wstring_view query, std::vector<Result>& out) {
  if (custom_.empty()) return;
  auto q = fuzzy::Prepare(query);
  for (size_t i = 0; i < custom_.size(); ++i) {
    const CustomCommand& c = custom_[i];
    float best = 0;
    for (const auto& kw : c.keywords) {
      if (str::IEquals(str::Trim(query), kw)) { best = 1.f; break; }
      best = std::max(best, fuzzy::Score(q, kw));
    }
    if (!c.title.empty()) best = std::max(best, fuzzy::Score(q, c.title) * 0.9f);
    if (best < 0.6f) continue;
    Result r;
    r.title = c.title.empty() ? (c.keywords.empty() ? c.target : c.keywords[0]) : c.title;
    r.subtitle = c.args.empty() ? c.target : c.target + L" " + c.args;
    r.category = kCategory;
    if (!customIcons_[i].empty()) {
      r.iconKind = IconKind::FilePath;
      r.icon = customIcons_[i];
    } else {
      r.iconKind = IconKind::Glyph;
      r.icon = c.target.find(L"://") != std::wstring::npos ? L"" : L"";
    }
    r.score = best >= 0.999f ? 0.96f : best * 0.95f;
    r.actions = kActOpen | kActCopy | kActAdmin;
    r.key = L"custom:" + r.title;
    r.payload = L"x|" + std::to_wstring(i);
    r.copyText = c.target;
    out.push_back(std::move(r));
  }
}

// ---- execute ------------------------------------------------------------------------------------------------

ExecResult CommandsProvider::Execute(const Result& r, Action a) {
  const std::wstring& p = r.payload;
  if (p.size() < 1) return {false, {}, {}};
  if (p == L"t") return {false, {}, L"таймер 5 минут"};
  std::wstring_view rest = std::wstring_view(p).substr(std::min<size_t>(2, p.size()));
  switch (p[0]) {
    case L'v':
      if (host_) host_->CopyToClipboard(rest);
      genValue_.clear();  // next time: a fresh value
      return {};
    case L'f': {
      std::wstring path(rest);
      if (a == Action::Copy) {
        if (host_) host_->CopyToClipboard(path);
      } else if (a == Action::Reveal && !str::StartsWith(path, L"shell:")) {
        win::RevealInExplorer(path);
      } else {
        win::ShellOpen(path);
      }
      return {};
    }
    case L'x': {
      size_t idx = size_t(std::wcstoul(std::wstring(rest).c_str(), nullptr, 10));
      if (idx >= custom_.size()) return {};
      const CustomCommand& c = custom_[idx];
      if (a == Action::Copy) {
        if (host_) host_->CopyToClipboard(c.target);
      } else {
        win::ShellOpen(c.target, c.args, c.admin || a == Action::RunAsAdmin);
      }
      return {};
    }
    case L'c': {
      // "c|<kind>|<delay>|<label>"
      auto parts = str::Split(rest, L'|', false);
      if (parts.size() < 2) return {};
      Kind kind = Kind(std::wcstol(std::wstring(parts[0]).c_str(), nullptr, 10));
      int64_t delay = std::wcstoll(std::wstring(parts[1]).c_str(), nullptr, 10);
      size_t labelPos = parts[0].size() + parts[1].size() + 2;
      std::wstring label = labelPos <= rest.size() ? std::wstring(rest.substr(labelPos)) : std::wstring();
      return RunCommand(kind, delay, label);
    }
    default:
      return {};
  }
}

ExecResult CommandsProvider::RunCommand(Kind kind, int64_t delay, const std::wstring& label) {
  ExecResult res;
  switch (kind) {
    case Kind::Shutdown:
    case Kind::Restart: {
      const wchar_t* flag = kind == Kind::Shutdown ? L"/s" : L"/r";
      if (delay < 0) {
        RunAndWait(L"shutdown.exe", std::wstring(flag) + L" /t 0", 0);
        return res;
      }
      delay = std::min(delay, kMaxShutdownDelay);
      RunAndWait(L"shutdown.exe", L"/a");  // replace a previously scheduled shutdown, if any
      DWORD code = RunAndWait(L"shutdown.exe", std::wstring(flag) + L" /t " + std::to_wstring(delay) +
                                                   L" /c \"Cloud Spotlight\"");
      if (code == 0) {
        shutdownAt_ = ClockIn(delay);
        res.toast = (kind == Kind::Shutdown ? L"Компьютер выключится " : L"Компьютер перезагрузится ") +
                    At(shutdownAt_);
      } else {
        res.toast = L"Не удалось запланировать выключение (shutdown.exe: " + std::to_wstring(long(code)) + L")";
      }
      return res;
    }
    case Kind::CancelShutdown: {
      DWORD code = RunAndWait(L"shutdown.exe", L"/a");
      shutdownAt_.clear();
      res.toast = code == 0 ? L"Запланированное выключение отменено" : L"Нет запланированного выключения";
      return res;
    }
    case Kind::Sleep: case Kind::Hibernate: case Kind::Lock: case Kind::SignOut: case Kind::MonitorOff:
    case Kind::Screensaver:
      if (delay >= 0) {
        Schedule(kind, delay * 1000, label, false);
        res.toast = std::wstring(cmd::Title(kind)) + L" " + At(ClockIn(delay)) + L". Отменить: «отменить таймер»";
      } else {
        // Run a moment later from the timer thread so the launcher hides (and keys are released) first.
        Schedule(kind, kind == Kind::MonitorOff || kind == Kind::Screensaver ? 700 : 300, label, true);
      }
      return res;
    case Kind::EmptyRecycleBin:
      SHEmptyRecycleBinW(nullptr, nullptr, 0);  // keeps the system confirmation dialog
      return res;
    case Kind::Timer:
      if (delay < 0) return {false, {}, L"таймер 5 минут"};
      Schedule(kind, delay * 1000, label, false);
      res.toast = (label.empty() ? std::wstring(L"Таймер сработает ") : L"Напомню «" + label + L"» ") +
                  At(ClockIn(delay));
      return res;
    case Kind::CancelTimer: {
      size_t n = CancelTimers();
      res.toast = n ? L"Отменено таймеров: " + std::to_wstring(n) : L"Нет активных таймеров";
      return res;
    }
    default:
      return res;
  }
}

// ---- timers -------------------------------------------------------------------------------------------------

void CommandsProvider::Schedule(Kind kind, int64_t delayMs, std::wstring label, bool silent) {
  std::lock_guard lk(tmu_);
  if (tstop_) return;
  Pending p;
  p.id = nextId_++;
  p.due = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
  p.kind = kind;
  p.label = std::move(label);
  p.seconds = delayMs / 1000;
  p.at = silent ? std::wstring() : ClockIn(p.seconds);
  p.silent = silent;
  timers_.push_back(std::move(p));
  if (!tthread_.joinable()) tthread_ = std::thread([this] { TimerThread(); });
  tcv_.notify_all();
}

size_t CommandsProvider::CancelTimers() {
  std::lock_guard lk(tmu_);
  size_t n = 0;
  for (const auto& t : timers_) n += t.silent ? 0 : 1;
  timers_.clear();
  tcv_.notify_all();
  return n;
}

size_t CommandsProvider::ActiveTimers(std::wstring* nextAt) {
  std::lock_guard lk(tmu_);
  size_t n = 0;
  const Pending* next = nullptr;
  for (const auto& t : timers_) {
    if (t.silent) continue;
    ++n;
    if (!next || t.due < next->due) next = &t;
  }
  if (nextAt && next) *nextAt = next->at;
  return n;
}

void CommandsProvider::TimerThread() {
  std::unique_lock lk(tmu_);
  while (!tstop_) {
    if (timers_.empty()) {
      tcv_.wait(lk);
      continue;
    }
    auto it = std::min_element(timers_.begin(), timers_.end(),
                               [](const Pending& a, const Pending& b) { return a.due < b.due; });
    auto now = std::chrono::steady_clock::now();
    if (now < it->due) {
      // Re-check at least every minute (waits may not advance while the PC sleeps).
      tcv_.wait_until(lk, std::min(it->due, now + std::chrono::seconds(60)));
      continue;
    }
    Pending p = std::move(*it);
    timers_.erase(it);
    lk.unlock();
    if (p.kind == Kind::Timer) {
      MessageBeep(MB_ICONASTERISK);
      if (host_)
        host_->Notify(L"Таймер", p.label.empty() ? L"Время вышло: таймер на " + cmd::FormatDuration(p.seconds)
                                                 : p.label);
    } else {
      Perform(p.kind);
    }
    lk.lock();
  }
}

void CommandsProvider::Perform(Kind kind) {
  switch (kind) {
    case Kind::Sleep:
      // Note: SetSuspendState(FALSE, ...) hibernates instead if hibernation is enabled and hybrid sleep is
      // off — standard Windows behaviour for this API.
      SetSuspendState(FALSE, FALSE, FALSE);
      break;
    case Kind::Hibernate:
      SetSuspendState(TRUE, FALSE, FALSE);
      break;
    case Kind::Lock:
      LockWorkStation();
      break;
    case Kind::SignOut:
      ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
      break;
    case Kind::MonitorOff:
      PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
      break;
    case Kind::Screensaver:
      DefWindowProcW(GetDesktopWindow(), WM_SYSCOMMAND, SC_SCREENSAVE, 0);
      break;
    default:
      break;
  }
}

// ---- generators ---------------------------------------------------------------------------------------------

std::wstring CommandsProvider::Password(int length) {
  static constexpr wchar_t kUpper[] = L"ABCDEFGHJKLMNPQRSTUVWXYZ";
  static constexpr wchar_t kLower[] = L"abcdefghijkmnopqrstuvwxyz";
  static constexpr wchar_t kDigits[] = L"23456789";
  static constexpr wchar_t kSymbols[] = L"!@#$%^&*-_=+?";
  std::wstring alphabet = std::wstring(kUpper) + kLower + kDigits + kSymbols;
  const unsigned n = unsigned(alphabet.size());
  const unsigned limit = 256 - 256 % n;  // rejection sampling: no modulo bias
  for (int attempt = 0; attempt < 16; ++attempt) {
    std::wstring pw;
    unsigned char buf[256];
    while (int(pw.size()) < length) {
      if (!RandomBytes(buf, sizeof(buf))) return {};
      for (unsigned char b : buf) {
        if (b >= limit) continue;
        pw.push_back(alphabet[b % n]);
        if (int(pw.size()) == length) break;
      }
    }
    SecureZeroMemory(buf, sizeof(buf));
    auto has = [&](const wchar_t* set) { return pw.find_first_of(set) != std::wstring::npos; };
    if (length < 8 || (has(kUpper) && has(kLower) && has(kDigits) && has(kSymbols))) return pw;
  }
  return {};
}

std::wstring CommandsProvider::LocalIps() {
  uint64_t now = GetTickCount64();
  if (!ipCache_.empty() && now - ipCacheTick_ < 10000) return ipCache_;
  ULONG size = 16 * 1024;
  std::vector<unsigned char> buf;
  ULONG rc = ERROR_BUFFER_OVERFLOW;
  for (int i = 0; i < 3 && rc == ERROR_BUFFER_OVERFLOW; ++i) {
    buf.resize(size);
    rc = GetAdaptersAddresses(AF_INET,
                              GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER |
                                  GAA_FLAG_INCLUDE_GATEWAYS,
                              nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
  }
  std::vector<std::wstring> withGw, other;
  if (rc == NO_ERROR) {
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
      if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
      for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
        if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
        const auto* sin = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
        const unsigned char* b = reinterpret_cast<const unsigned char*>(&sin->sin_addr);
        if (b[0] == 169 && b[1] == 254) continue;  // link-local
        std::wstring ip = std::to_wstring(b[0]) + L"." + std::to_wstring(b[1]) + L"." + std::to_wstring(b[2]) +
                          L"." + std::to_wstring(b[3]);
        (a->FirstGatewayAddress ? withGw : other).push_back(std::move(ip));
      }
    }
  }
  std::wstring r;
  for (auto* list : {&withGw, &other})
    for (const auto& ip : *list) r += (r.empty() ? L"" : L", ") + ip;
  ipCache_ = r;
  ipCacheTick_ = now;
  return r;
}

}  // namespace cs
