#pragma once
// Notification-area icon (NOTIFYICON_VERSION_4) with the context menu and balloon notifications.
#include <windows.h>

#include <string>
#include <string_view>

namespace cs {

class Tray {
 public:
  enum Command : UINT {
    kCmdOpen = 100,
    kCmdSettings,
    kCmdDataDir,
    kCmdAutostart,
    kCmdRestart,
    kCmdExit,
  };

  Tray() = default;
  ~Tray();
  Tray(const Tray&) = delete;
  Tray& operator=(const Tray&) = delete;

  // `callbackMsg` is sent to `owner` for mouse/keyboard events on the icon (pass them to HandleCallback).
  bool Create(HWND owner, HINSTANCE inst, UINT callbackMsg, std::wstring_view tooltip);
  void Destroy();
  bool Recreate();  // after "TaskbarCreated" (Explorer restart) or a DPI change
  void SetTooltip(std::wstring_view tooltip);
  void ShowBalloon(std::wstring_view title, std::wstring_view text);

  enum class Event { None, Activate, Open, Menu };  // Activate = toggle (click), Open = show (keyboard)
  // Decodes the callback message. For Menu, `pt` receives the anchor point in screen coordinates.
  Event HandleCallback(WPARAM wp, LPARAM lp, POINT& pt) const;
  // Shows the popup menu modally; returns the chosen Command or 0. `restartHint` marks "Перезапустить" as needed
  // to apply settings that cannot be hot-reloaded.
  UINT ShowMenu(POINT pt, std::wstring_view hotkeyText, bool autostartChecked, bool restartHint);

  // Dark context menus that follow the app theme (uxtheme private API, guarded). theme: system | light | dark.
  static void ApplyMenuTheme(HWND owner, std::wstring_view theme);

 private:
  bool Add();
  void LoadIcons();
  void FreeIcons();

  HWND owner_ = nullptr;
  HINSTANCE inst_ = nullptr;
  UINT msg_ = 0;
  HICON small_ = nullptr;
  HICON large_ = nullptr;
  std::wstring tip_;
  bool added_ = false;
};

}  // namespace cs
