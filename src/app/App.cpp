#include "app/App.h"

#include <shellapi.h>
#include <shlwapi.h>

#include <cstring>

#include "app/Autostart.h"
#include "core/SearchEngine.h"
#include "core/Str.h"
#include "platform/Win.h"
#include "providers/AppsProvider.h"
#include "providers/CalculatorProvider.h"
#include "providers/CommandsProvider.h"
#include "providers/CurrencyProvider.h"
#include "providers/FilesProvider.h"
#include "providers/SettingsProvider.h"
#include "providers/WebSearchProvider.h"
#include "ui/LauncherWindow.h"

namespace cs {
namespace {

constexpr UINT kMsgRefresh = WM_APP + 2;
constexpr UINT kMsgNotify = WM_APP + 3;     // lParam: NotifyPayload*
constexpr UINT kMsgClipboard = WM_APP + 4;  // lParam: std::wstring*
constexpr UINT kMsgTray = WM_APP + 5;
constexpr UINT kMsgHookHotkey = WM_APP + 6;
constexpr UINT kMsgConfigDirChanged = WM_APP + 7;

constexpr UINT_PTR kTimerIdle = 1;
constexpr UINT_PTR kTimerConfig = 2;
constexpr UINT kIdleDelayMs = 20000;     // hidden this long -> trim working set + EcoQoS
constexpr UINT kConfigDebounceMs = 300;  // editors write files in several steps
constexpr ULONGLONG kTrayClickGraceMs = 400;

// Not in older SDK/MinGW headers.
constexpr DWORD kEventObjectCloaked = 0x8017;
constexpr DWORD kEventObjectUncloaked = 0x8018;

constexpr wchar_t kAppName[] = L"Cloud Spotlight";

struct NotifyPayload {
  std::wstring title, text;
};

App* g_app = nullptr;  // for the WinEvent callback, which has no context pointer

bool SameFolders(const std::vector<FolderAlias>& a, const std::vector<FolderAlias>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].name != b[i].name || a[i].path != b[i].path) return false;
  return true;
}

bool SameCommands(const std::vector<CustomCommand>& a, const std::vector<CustomCommand>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].keywords != b[i].keywords || a[i].title != b[i].title || a[i].target != b[i].target ||
        a[i].args != b[i].args || a[i].admin != b[i].admin)
      return false;
  return true;
}

// Settings read by providers in Init(); changing them needs a restart.
bool ProviderSettingsDiffer(const Config& a, const Config& b) {
  return a.fileRoots != b.fileRoots || a.fileExclude != b.fileExclude || a.fileMaxDepth != b.fileMaxDepth ||
         a.fileReindexMinutes != b.fileReindexMinutes || a.fileMaxEntries != b.fileMaxEntries ||
         !SameFolders(a.folders, b.folders) || !SameCommands(a.commands, b.commands) ||
         a.currencyBase != b.currencyBase || a.currencyApi != b.currencyApi ||
         a.currencyCacheHours != b.currencyCacheHours || a.webSearch != b.webSearch;
}

}  // namespace

App::App() { g_app = this; }

App::~App() {
  Shutdown();
  if (hwnd_) DestroyWindow(hwnd_);
  if (g_app == this) g_app = nullptr;
}

bool App::Init(HINSTANCE inst, const StartOptions& opt) {
  inst_ = inst;
  uiThread_ = GetCurrentThreadId();

  ConfigStore::Status status = store_.Load(cfg_);
  bootCfg_ = cfg_;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.lpszClassName = kHostWindowClass;
  if (!RegisterClassExW(&wc)) return false;
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kHostWindowClass, kAppName, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                          inst, this);
  if (!hwnd_) return false;
  taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
  // Let a non-elevated Explorer deliver TaskbarCreated even if we run elevated.
  ChangeWindowMessageFilterEx(hwnd_, taskbarCreatedMsg_, MSGFLT_ALLOW, nullptr);
  Tray::ApplyMenuTheme(hwnd_, cfg_.theme);

  engine_ = std::make_unique<SearchEngine>(cfg_.dataDir + L"\\usage.tsv");
  engine_->Add(std::make_unique<CalculatorProvider>());
  engine_->Add(std::make_unique<CurrencyProvider>());
  engine_->Add(std::make_unique<CommandsProvider>());
  engine_->Add(std::make_unique<AppsProvider>());
  engine_->Add(std::make_unique<SettingsProvider>());
  engine_->Add(std::make_unique<FilesProvider>());
  engine_->Add(std::make_unique<WebSearchProvider>());
  engine_->SetMaxResults(cfg_.maxResults);
  engine_->InitAll(bootCfg_, *this);

  launcher_ = std::make_unique<LauncherWindow>(*engine_, *this);
  if (!launcher_->Create(inst, cfg_)) {
    MessageBoxW(nullptr, L"Не удалось создать окно (Direct2D/DirectWrite недоступны?).", kAppName,
                MB_ICONERROR | MB_OK);
    return false;
  }

  hotkey_ = std::make_unique<HotkeyManager>(hwnd_, kMsgHookHotkey);
  tray_.Create(hwnd_, inst, kMsgTray, kAppName);  // may fail at logon before Explorer is up; TaskbarCreated retries
  ApplyHotkey(false);
  autostart::Sync(cfg_.autostart);

  if (status == ConfigStore::Status::ParseError || status == ConfigStore::Status::IoError) {
    lastConfigError_ = store_.LastError();
    Notify(L"Настройки не загружены", lastConfigError_);
  } else if (status == ConfigStore::Status::Created) {
    std::wstring hk = hotkey_->ActiveText().empty() ? cfg_.hotkey : hotkey_->ActiveText();
    Notify(L"Cloud Spotlight запущен", L"Нажмите " + hk + L", чтобы открыть поиск. Значок в трее — меню и настройки.");
  } else if (opt.restarted) {
    Notify(L"Cloud Spotlight перезапущен", L"Настройки применены.");
  }

  // Observe launcher show/hide (whoever triggers it: Esc, focus loss, our hotkey) to drive idle-mode timers.
  DWORD pid = GetCurrentProcessId();
  winEvents_[0] = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, WinEventProc, pid, uiThread_,
                                  WINEVENT_OUTOFCONTEXT);
  winEvents_[1] = SetWinEventHook(kEventObjectCloaked, kEventObjectUncloaked, nullptr, WinEventProc, pid, uiThread_,
                                  WINEVENT_OUTOFCONTEXT);
  StartConfigWatcher();

  if (opt.showLauncher) {
    ShowLauncher();
  } else {
    SetTimer(hwnd_, kTimerIdle, kIdleDelayMs, nullptr);
  }
  return true;
}

void App::Shutdown() {
  if (shutDown_) return;
  shutDown_ = true;
  StopConfigWatcher();
  for (auto& h : winEvents_) {
    if (h) UnhookWinEvent(h);
    h = nullptr;
  }
  if (hwnd_) {
    KillTimer(hwnd_, kTimerIdle);
    KillTimer(hwnd_, kTimerConfig);
  }
  hotkey_.reset();
  tray_.Destroy();
  if (launcher_) launcher_->Hide();
  // Providers' background threads may still post to hwnd_ while stopping; it stays alive until ~App.
  if (engine_) engine_->ShutdownAll();
  launcher_.reset();
  engine_.reset();
}

// ---- IHost ----

void App::RequestRefresh() {
  if (!hwnd_) return;
  if (!refreshPending_.exchange(true) && !PostMessageW(hwnd_, kMsgRefresh, 0, 0)) refreshPending_ = false;
}

void App::Notify(std::wstring_view title, std::wstring_view text) {
  auto* p = new NotifyPayload{std::wstring(title), std::wstring(text)};
  if (!hwnd_ || !PostMessageW(hwnd_, kMsgNotify, 0, reinterpret_cast<LPARAM>(p))) delete p;
}

void App::CopyToClipboard(std::wstring_view text) {
  if (GetCurrentThreadId() == uiThread_) {
    SetClipboardText(std::wstring(text));
    return;
  }
  auto* p = new std::wstring(text);
  if (!hwnd_ || !PostMessageW(hwnd_, kMsgClipboard, 0, reinterpret_cast<LPARAM>(p))) delete p;
}

void App::SetClipboardText(const std::wstring& text) {
  // Another app (clipboard managers, RDP) may briefly hold the clipboard open.
  bool open = false;
  for (int i = 0; i < 10 && !open; ++i) {
    open = OpenClipboard(hwnd_) != FALSE;
    if (!open) Sleep(5);
  }
  if (!open) return;
  EmptyClipboard();
  size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
    if (void* dst = GlobalLock(h)) {
      memcpy(dst, text.c_str(), bytes);
      GlobalUnlock(h);
      if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
    } else {
      GlobalFree(h);
    }
  }
  CloseClipboard();
}

// ---- Window ----

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  }
  auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCDESTROY) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    if (self) self->hwnd_ = nullptr;
    // Drop payloads still queued so they don't leak.
    MSG m;
    while (PeekMessageW(&m, hwnd, kMsgNotify, kMsgClipboard, PM_REMOVE)) {
      if (m.message == kMsgNotify) delete reinterpret_cast<NotifyPayload*>(m.lParam);
      else delete reinterpret_cast<std::wstring*>(m.lParam);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_HOTKEY:
      if (wp == HotkeyManager::kHotkeyId) ToggleLauncher(false);
      return 0;
    case kMsgHookHotkey:
      ToggleLauncher(false);
      return 0;
    case kMsgShowLauncher:
      ShowLauncher();
      return 0;
    case kMsgRefresh:
      refreshPending_ = false;
      if (launcher_ && launcher_->IsVisible()) launcher_->Refresh();
      return 0;
    case kMsgNotify: {
      std::unique_ptr<NotifyPayload> p(reinterpret_cast<NotifyPayload*>(lp));
      if (!shutDown_) tray_.ShowBalloon(p->title, p->text);
      return 0;
    }
    case kMsgClipboard: {
      std::unique_ptr<std::wstring> p(reinterpret_cast<std::wstring*>(lp));
      SetClipboardText(*p);
      return 0;
    }
    case kMsgTray: {
      if (shutDown_) return 0;
      POINT pt{};
      switch (tray_.HandleCallback(wp, lp, pt)) {
        case Tray::Event::Activate: ToggleLauncher(true); break;
        case Tray::Event::Open: ShowLauncher(); break;
        case Tray::Event::Menu:
          OnTrayCommand(tray_.ShowMenu(pt, hotkey_ ? hotkey_->ActiveText() : std::wstring(), autostart::IsEnabled(),
                                       restartHint_));
          break;
        case Tray::Event::None: break;
      }
      return 0;
    }
    case kMsgConfigDirChanged:
      if (!shutDown_) SetTimer(hwnd_, kTimerConfig, kConfigDebounceMs, nullptr);
      return 0;
    case WM_TIMER:
      if (wp == kTimerIdle) OnIdleTimer();
      if (wp == kTimerConfig) {
        KillTimer(hwnd_, kTimerConfig);
        if (store_.ChangedOnDisk()) ReloadConfig();
      }
      return 0;
    case WM_SETTINGCHANGE:
      if (lp && lstrcmpiW(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0 && !shutDown_) {
        Tray::ApplyMenuTheme(hwnd_, cfg_.theme);
        if (launcher_) launcher_->OnSystemThemeChanged();
      }
      return 0;
    case WM_POWERBROADCAST:
      if (wp == PBT_APMRESUMEAUTOMATIC && hotkey_) hotkey_->Rearm();
      return TRUE;
    case WM_QUERYENDSESSION:
      return TRUE;
    case WM_ENDSESSION:
      if (wp) Shutdown();  // the process may be terminated any moment after we return
      return 0;
    case WM_CLOSE:  // e.g. `taskkill /im CloudSpotlight.exe` without /f: exit gracefully
      PostQuitMessage(0);
      return 0;
    default:
      if (msg == taskbarCreatedMsg_ && taskbarCreatedMsg_ && !shutDown_) {
        tray_.Recreate();
        UpdateTooltip();
        return 0;
      }
      return DefWindowProcW(hwnd_, msg, wp, lp);
  }
}

void CALLBACK App::WinEventProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG obj, LONG child, DWORD, DWORD) {
  if (obj != OBJID_WINDOW || child != CHILDID_SELF || !g_app || !g_app->launcher_) return;
  if (hwnd == g_app->launcher_->Hwnd()) g_app->SyncLauncherVisibility();
}

// ---- Launcher visibility & idle mode ----

void App::ToggleLauncher(bool fromTrayClick) {
  if (!launcher_) return;
  if (launcher_->IsVisible()) {
    launcher_->Hide();
  } else {
    // Clicking the tray icon deactivates the launcher first (it hides itself), then the click arrives:
    // treat that as "hide", not "show again".
    if (fromTrayClick && GetTickCount64() - lastHiddenTick_ < kTrayClickGraceMs) return;
    SetIdleMode(false);
    launcher_->Show();
  }
  SyncLauncherVisibility();
}

void App::ShowLauncher() {
  if (!launcher_) return;
  SetIdleMode(false);
  launcher_->Show();
  SyncLauncherVisibility();
}

void App::SyncLauncherVisibility() {
  bool visible = launcher_ && launcher_->IsVisible();
  if (visible == launcherVisible_) return;
  launcherVisible_ = visible;
  if (visible) {
    KillTimer(hwnd_, kTimerIdle);
    SetIdleMode(false);
  } else {
    lastHiddenTick_ = GetTickCount64();
    SetTimer(hwnd_, kTimerIdle, kIdleDelayMs, nullptr);
  }
}

void App::OnIdleTimer() {
  KillTimer(hwnd_, kTimerIdle);
  if (shutDown_ || (launcher_ && launcher_->IsVisible())) return;
  SetIdleMode(true);
  // Hand back pages touched while the launcher was visible (D2D, fonts, result lists); they fault back in
  // cheaply from the standby list on the next show.
  SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void App::SetIdleMode(bool idle) {
  if (idleState_ == int(idle)) return;
  idleState_ = int(idle);
  // Idle: EcoQoS (efficiency cores, low clocks) for the whole process incl. provider indexing threads.
  // Visible: explicitly opt out, so the UI is never throttled even though our process isn't "foreground" yet.
  PROCESS_POWER_THROTTLING_STATE s{};
  s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
  s.StateMask = idle ? s.ControlMask : 0;
  if (!SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof(s))) {
    // IGNORE_TIMER_RESOLUTION is Win11+; retry with execution speed only.
    s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    s.StateMask = idle ? s.ControlMask : 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof(s));
  }
}

// ---- Hotkey / tray ----

void App::ApplyHotkey(bool announce) {
  HotkeyManager::Mode mode = hotkey_->Apply(cfg_.hotkey, cfg_.fallbackHotkey);
  const std::wstring& active = hotkey_->ActiveText();
  std::wstring msg;
  if (!hotkey_->ParseError().empty())
    msg = L"Не удалось разобрать сочетание «" + hotkey_->ParseError() + L"» в config.json. ";
  switch (mode) {
    case HotkeyManager::Mode::Primary:
      if (announce || !msg.empty()) msg += L"Горячая клавиша: " + active + L".";
      break;
    case HotkeyManager::Mode::Fallback:
      if (hotkey_->ParseError() == cfg_.hotkey) msg += L"Используется " + active + L".";
      else msg += L"«" + cfg_.hotkey + L"» занято другой программой — используется " + active + L".";
      break;
    case HotkeyManager::Mode::PrimaryHook:
      msg += L"«" + cfg_.hotkey + L"» занято другой программой";
      if (!str::Trim(cfg_.fallbackHotkey).empty()) msg += L" (и «" + cfg_.fallbackHotkey + L"» тоже)";
      msg += L" — " + active + L" перехватывается напрямую.";
      break;
    case HotkeyManager::Mode::None:
      msg += L"Горячая клавиша не назначена. Откройте поиск значком в трее или измените «hotkey» в настройках.";
      break;
  }
  if (!msg.empty()) Notify(kAppName, msg);
  UpdateTooltip();
}

void App::UpdateTooltip() {
  std::wstring tip = kAppName;
  if (hotkey_ && !hotkey_->ActiveText().empty()) tip += L" — " + hotkey_->ActiveText();
  tray_.SetTooltip(tip);
}

void App::OnTrayCommand(UINT cmd) {
  switch (cmd) {
    case Tray::kCmdOpen: ShowLauncher(); break;
    case Tray::kCmdSettings: OpenSettings(); break;
    case Tray::kCmdDataDir: win::ShellOpen(cfg_.dataDir); break;
    case Tray::kCmdAutostart: ToggleAutostart(); break;
    case Tray::kCmdRestart:
      restart_ = true;
      PostQuitMessage(0);
      break;
    case Tray::kCmdExit: PostQuitMessage(0); break;
    default: break;
  }
}

void App::OpenSettings() {
  if (!store_.EnsureFile()) {
    Notify(kAppName, L"Не удалось создать " + store_.Path());
    return;
  }
  // Use the registered .json handler if there is one; otherwise ShellExecute would pop the "Open with" dialog.
  wchar_t exe[MAX_PATH];
  DWORD n = MAX_PATH;
  bool hasHandler =
      SUCCEEDED(AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN, ASSOCSTR_EXECUTABLE, L".json", L"open", exe, &n));
  if (!hasHandler || !win::ShellOpen(store_.Path())) win::ShellOpen(L"notepad.exe", L"\"" + store_.Path() + L"\"");
}

void App::ToggleAutostart() {
  bool enable = !autostart::IsEnabled();
  bool ok = enable ? autostart::Enable() : autostart::Disable();
  if (!ok) {
    Notify(kAppName, L"Не удалось изменить автозапуск (реестр недоступен).");
    return;
  }
  cfg_.autostart = enable;
  if (!store_.SetValue("autostart", json::Value::Bool(enable)))
    Notify(kAppName, L"Автозапуск изменён, но config.json не обновлён (файл содержит ошибку).");
}

// ---- Config hot reload ----

void App::StartConfigWatcher() {
  watcherStop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!watcherStop_) return;
  HANDLE change = FindFirstChangeNotificationW(
      store_.Dir().c_str(), FALSE,
      FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE);
  if (change == INVALID_HANDLE_VALUE) return;  // no watching; config still loads at startup
  HWND target = hwnd_;
  HANDLE stop = watcherStop_;
  watcher_ = std::thread([change, stop, target] {
    HANDLE handles[2] = {stop, change};
    while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
      PostMessageW(target, kMsgConfigDirChanged, 0, 0);
      if (!FindNextChangeNotification(change)) break;
    }
    FindCloseChangeNotification(change);
  });
}

void App::StopConfigWatcher() {
  if (watcherStop_) SetEvent(watcherStop_);
  if (watcher_.joinable()) watcher_.join();
  if (watcherStop_) CloseHandle(watcherStop_);
  watcherStop_ = nullptr;
}

void App::ReloadConfig() {
  Config next;
  ConfigStore::Status status = store_.Load(next);
  if (status == ConfigStore::Status::ParseError || status == ConfigStore::Status::IoError) {
    // Keep running with the previous settings; tell the user once per distinct error.
    if (store_.LastError() != lastConfigError_) {
      lastConfigError_ = store_.LastError();
      Notify(L"Ошибка в настройках", lastConfigError_ + L" Изменения не применены.");
    }
    return;
  }
  lastConfigError_.clear();

  Config prev = std::move(cfg_);
  cfg_ = std::move(next);
  if (cfg_.hotkey != prev.hotkey || cfg_.fallbackHotkey != prev.fallbackHotkey) ApplyHotkey(true);
  if (cfg_.autostart != prev.autostart) {
    if (cfg_.autostart) autostart::Enable();
    else autostart::Disable();
  }
  if (cfg_.theme != prev.theme) Tray::ApplyMenuTheme(hwnd_, cfg_.theme);
  engine_->SetMaxResults(cfg_.maxResults);
  launcher_->ApplyConfig(cfg_);

  bool needRestart = ProviderSettingsDiffer(cfg_, bootCfg_);
  if (needRestart && (!restartHint_ || ProviderSettingsDiffer(cfg_, prev)))
    Notify(L"Настройки сохранены",
           L"Изменения поиска (папки, файлы, команды, валюты) применятся после перезапуска: меню в трее → "
           L"«Перезапустить».");
  restartHint_ = needRestart;
}

}  // namespace cs
