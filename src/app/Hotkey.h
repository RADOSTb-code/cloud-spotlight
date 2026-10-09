#pragma once
// Global hotkey: parsing ("Alt+Space", "Win+Shift+K", "Ctrl+`"...) and registration.
// Both configured hotkeys (e.g. Alt+Space and Ctrl+Space) are active at the same time. Each is registered with
// RegisterHotKey; one that another app already owns is intercepted with a WH_KEYBOARD_LL hook instead.
// RegisterHotKey(MOD_ALT, VK_SPACE) does work: hotkeys are matched in the raw input thread before the
// foreground window ever sees WM_SYSKEYDOWN, so the window system menu never opens.
#include <windows.h>

#include <string>
#include <string_view>
#include <thread>
#include <vector>

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
  enum class How { Registered, Hook, Failed, Invalid };
  struct Binding {
    std::wstring source;  // as written in the config
    std::wstring text;    // canonical, e.g. "Ctrl+Space" (empty if Invalid)
    How how = How::Failed;
  };

  // WM_HOTKEY (id in [kHotkeyId, kHotkeyId + kMaxBindings)) or `hookMsg` (from the hook thread) is posted to
  // `target` when a hotkey fires.
  HotkeyManager(HWND target, UINT hookMsg);
  ~HotkeyManager();
  HotkeyManager(const HotkeyManager&) = delete;
  HotkeyManager& operator=(const HotkeyManager&) = delete;

  static constexpr int kHotkeyId = 1;
  static constexpr int kMaxBindings = 2;
  static bool IsOurHotkeyId(WPARAM id) { return id >= WPARAM(kHotkeyId) && id < WPARAM(kHotkeyId + kMaxBindings); }

  // (Re)binds both hotkeys (empty strings are skipped, duplicates collapsed).
  const std::vector<Binding>& Apply(std::wstring_view primary, std::wstring_view secondary);
  void Clear();
  // After resume from sleep: low-level hooks may have been silently removed by the system; reinstall.
  void Rearm();

  const std::vector<Binding>& bindings() const { return bindings_; }
  // Display text of the working bindings, e.g. "Alt+Space / Ctrl+Space". Empty if none works.
  const std::wstring& ActiveText() const { return active_; }

 private:
  bool StartHook(const std::vector<HotkeySpec>& specs);
  void StopHook();

  HWND target_;
  UINT hookMsg_;
  std::vector<Binding> bindings_;
  std::vector<HotkeySpec> hookSpecs_;
  std::wstring active_;
  std::thread hookThread_;
  DWORD hookThreadId_ = 0;
};

}  // namespace cs
