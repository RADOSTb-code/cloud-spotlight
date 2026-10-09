#pragma once
// Application host: owns config, search engine, launcher window, tray icon and the global hotkey.
// Implements IHost for providers. All public methods except the IHost ones are UI-thread only.
#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "app/ConfigStore.h"
#include "app/Hotkey.h"
#include "app/Tray.h"
#include "core/Config.h"
#include "core/Types.h"

namespace cs {

class SearchEngine;
class LauncherWindow;

// Hidden top-level host window (not message-only: it must receive the "TaskbarCreated" broadcast).
inline constexpr wchar_t kHostWindowClass[] = L"CloudSpotlight.Host";
// Posted to the host window by a second instance: "show the launcher".
inline constexpr UINT kMsgShowLauncher = WM_APP + 1;

class App final : public IHost {
 public:
  struct StartOptions {
    bool showLauncher = true;  // false with --background (autostart)
    bool restarted = false;    // started by "Перезапустить"
  };

  App();
  ~App() override;
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  bool Init(HINSTANCE inst, const StartOptions& opt);
  void Shutdown();  // idempotent; stops providers and watchers, removes the tray icon
  bool RestartRequested() const { return restart_; }

  // IHost (thread-safe)
  void RequestRefresh() override;
  void Notify(std::wstring_view title, std::wstring_view text) override;
  void CopyToClipboard(std::wstring_view text) override;

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG obj, LONG child, DWORD, DWORD);
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

  void ToggleLauncher(bool fromTrayClick);
  void ShowLauncher();
  void SyncLauncherVisibility();
  void SetIdleMode(bool idle);
  void OnIdleTimer();

  void ApplyHotkey(bool announce);
  void UpdateTooltip();
  void OnTrayCommand(UINT cmd);
  void OpenSettings();
  void ToggleAutostart();

  void StartConfigWatcher();
  void StopConfigWatcher();
  void ReloadConfig();

  void SetClipboardText(const std::wstring& text);

  HINSTANCE inst_ = nullptr;
  HWND hwnd_ = nullptr;
  DWORD uiThread_ = 0;
  UINT taskbarCreatedMsg_ = 0;

  ConfigStore store_;
  Config bootCfg_;  // what providers were initialised with; never modified (providers may keep a reference)
  Config cfg_;      // live config, hot-reloaded
  std::unique_ptr<SearchEngine> engine_;
  std::unique_ptr<LauncherWindow> launcher_;
  std::unique_ptr<HotkeyManager> hotkey_;
  Tray tray_;

  std::atomic<bool> refreshPending_{false};
  HWINEVENTHOOK winEvents_[2] = {};
  bool launcherVisible_ = false;
  ULONGLONG lastHiddenTick_ = 0;
  int idleState_ = -1;  // -1 unknown, 0 full speed, 1 EcoQoS

  std::thread watcher_;
  HANDLE watcherStop_ = nullptr;
  std::wstring lastConfigError_;
  bool restartHint_ = false;  // provider settings changed on disk; they apply after a restart

  bool restart_ = false;
  bool shutDown_ = false;
};

}  // namespace cs
