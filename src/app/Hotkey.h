#pragma once
// Global hotkey: parsing ("Alt+Space", "Win+Shift+K", "Ctrl+`"...) and registration.
// Strategy: RegisterHotKey(primary) -> RegisterHotKey(fallback) -> WH_KEYBOARD_LL hook for the primary.
// RegisterHotKey(MOD_ALT, VK_SPACE) does work: hotkeys are matched in the raw input thread before the
// foreground window ever sees WM_SYSKEYDOWN, so the window system menu never opens.
#include <windows.h>

#include <string>
#include <string_view>
#include <thread>

namespace cs {

struct HotkeySpec {
  UINT mods = 0;  // MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN (without MOD_NOREPEAT)
  UINT vk = 0;
  bool Valid() const { return vk != 0; }
  bool operator==(const HotkeySpec&) const = default;
};

// Case-insensitive; tokens separated by '+', e.g. "ctrl + alt + f1", "Ctrl++" (plus key). false on error.
bool ParseHotkey(std::wstring_view text, HotkeySpec& out);
// Canonical display form: "Ctrl+Alt+F1".
std::wstring FormatHotkey(const HotkeySpec& hk);

class HotkeyManager {
 public:
  enum class Mode { None, Primary, Fallback, PrimaryHook };

  // WM_HOTKEY (id = kHotkeyId) or `hookMsg` (from the hook thread) is posted to `target` when the hotkey fires.
  HotkeyManager(HWND target, UINT hookMsg);
  ~HotkeyManager();
  HotkeyManager(const HotkeyManager&) = delete;
  HotkeyManager& operator=(const HotkeyManager&) = delete;

  static constexpr int kHotkeyId = 1;

  // (Re)binds. Returns the mode that ended up active; ActiveText() then holds the binding's display text.
  Mode Apply(std::wstring_view primary, std::wstring_view fallback);
  void Clear();
  // After resume from sleep: low-level hooks may have been silently removed by the system; reinstall.
  void Rearm();

  Mode mode() const { return mode_; }
  const std::wstring& ActiveText() const { return active_; }
  // Non-empty if a hotkey string in the config could not be parsed.
  const std::wstring& ParseError() const { return parseError_; }

 private:
  bool StartHook(const HotkeySpec& hk);
  void StopHook();

  HWND target_;
  UINT hookMsg_;
  Mode mode_ = Mode::None;
  HotkeySpec hookSpec_;
  std::wstring active_;
  std::wstring parseError_;
  std::thread hookThread_;
  DWORD hookThreadId_ = 0;
};

}  // namespace cs
