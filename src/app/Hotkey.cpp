#include "app/Hotkey.h"

#include <atomic>
#include <vector>

#include "core/Str.h"

namespace cs {
namespace {

struct KeyName {
  const wchar_t* name;
  UINT vk;
};

// Lowercase names. Punctuation uses the US-layout OEM keys (VK_OEM_3 is '`' on EN and 'ё' on RU layouts).
constexpr KeyName kKeys[] = {
    {L"space", VK_SPACE},     {L"enter", VK_RETURN},     {L"return", VK_RETURN},  {L"tab", VK_TAB},
    {L"esc", VK_ESCAPE},      {L"escape", VK_ESCAPE},    {L"backspace", VK_BACK}, {L"delete", VK_DELETE},
    {L"del", VK_DELETE},      {L"insert", VK_INSERT},    {L"ins", VK_INSERT},     {L"home", VK_HOME},
    {L"end", VK_END},         {L"pgup", VK_PRIOR},       {L"pageup", VK_PRIOR},   {L"pgdn", VK_NEXT},
    {L"pagedown", VK_NEXT},   {L"up", VK_UP},            {L"down", VK_DOWN},      {L"left", VK_LEFT},
    {L"right", VK_RIGHT},     {L"pause", VK_PAUSE},      {L"printscreen", VK_SNAPSHOT},
    {L"prtsc", VK_SNAPSHOT},  {L"apps", VK_APPS},        {L"menu", VK_APPS},
    {L"`", VK_OEM_3},         {L"~", VK_OEM_3},          {L"backtick", VK_OEM_3}, {L"grave", VK_OEM_3},
    {L"tilde", VK_OEM_3},     {L"ё", VK_OEM_3},          {L"-", VK_OEM_MINUS},    {L"minus", VK_OEM_MINUS},
    {L"=", VK_OEM_PLUS},      {L"+", VK_OEM_PLUS},       {L"plus", VK_OEM_PLUS},  {L"[", VK_OEM_4},
    {L"]", VK_OEM_6},         {L"\\", VK_OEM_5},         {L";", VK_OEM_1},        {L"'", VK_OEM_7},
    {L",", VK_OEM_COMMA},     {L"comma", VK_OEM_COMMA},  {L".", VK_OEM_PERIOD},   {L"period", VK_OEM_PERIOD},
    {L"/", VK_OEM_2},         {L"slash", VK_OEM_2},
};

UINT ModifierFromName(std::wstring_view t) {
  if (t == L"alt" || t == L"option") return MOD_ALT;
  if (t == L"ctrl" || t == L"control" || t == L"ctl") return MOD_CONTROL;
  if (t == L"shift") return MOD_SHIFT;
  if (t == L"win" || t == L"windows" || t == L"super" || t == L"meta" || t == L"cmd") return MOD_WIN;
  return 0;
}

UINT KeyFromName(std::wstring_view t) {
  if (t.size() == 1) {
    wchar_t c = t[0];
    if (c >= L'a' && c <= L'z') return UINT(c - L'a' + 'A');
    if (c >= L'0' && c <= L'9') return UINT(c);
  }
  if (t.size() >= 2 && t.size() <= 3 && t[0] == L'f') {
    int n = 0;
    for (size_t i = 1; i < t.size(); ++i) {
      if (!str::IsDigit(t[i])) return 0;
      n = n * 10 + (t[i] - L'0');
    }
    return n >= 1 && n <= 24 ? UINT(VK_F1 + n - 1) : 0;
  }
  for (std::wstring_view p : {std::wstring_view(L"numpad"), std::wstring_view(L"num")}) {
    if (t.size() == p.size() + 1 && t.substr(0, p.size()) == p && str::IsDigit(t.back()))
      return UINT(VK_NUMPAD0 + (t.back() - L'0'));
  }
  for (const auto& k : kKeys)
    if (t == k.name) return k.vk;
  return 0;
}

// ---- Low-level keyboard hook (fallback path). One hook per process; state is global because the hook proc
// has no context pointer. Everything here runs on the dedicated hook thread and must stay very cheap: every
// keystroke in the session passes through it.
constexpr UINT kMaskVk = 0xE8;  // unassigned VK; injected to stop Alt/Win release from opening menus / Start

struct HookState {
  static constexpr int kMax = 2;
  std::atomic<UINT> mods[kMax]{};
  std::atomic<UINT> vk[kMax]{};
  std::atomic<HWND> target{nullptr};
  std::atomic<UINT> msg{0};
  UINT swallowingVk = 0;  // hook thread only: key whose repeats/release we are eating
};
HookState g_hook;

UINT CurrentMods() {
  auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
  UINT m = 0;
  if (down(VK_MENU)) m |= MOD_ALT;
  if (down(VK_CONTROL)) m |= MOD_CONTROL;
  if (down(VK_SHIFT)) m |= MOD_SHIFT;
  if (down(VK_LWIN) || down(VK_RWIN)) m |= MOD_WIN;
  return m;
}

void SendMaskKey() {
  INPUT in[2] = {};
  in[0].type = in[1].type = INPUT_KEYBOARD;
  in[0].ki.wVk = in[1].ki.wVk = kMaskVk;
  in[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(2, in, sizeof(INPUT));
}

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION) {
    const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
    const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
    if (g_hook.swallowingVk && k->vkCode == g_hook.swallowingVk) {
      if (!down) g_hook.swallowingVk = 0;
      return 1;  // auto-repeat or release of the hotkey we already handled
    }
    for (int i = 0; i < HookState::kMax; ++i) {
      if (k->vkCode != g_hook.vk[i].load(std::memory_order_relaxed) || !down) continue;
      // GetAsyncKeyState is only consulted for a hotkey's own key, so normal typing costs two compares.
      UINT mods = CurrentMods();
      if (mods != g_hook.mods[i].load(std::memory_order_relaxed)) continue;
      g_hook.swallowingVk = k->vkCode;
      if (mods & (MOD_ALT | MOD_WIN)) SendMaskKey();
      PostMessageW(g_hook.target.load(), g_hook.msg.load(), 0, 0);
      return 1;
    }
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

// The process may be EcoQoS-throttled while idle; the hook thread must never be, or system-wide typing would lag.
void OptOutOfPowerThrottling() {
  struct ThreadPowerThrottlingState {
    ULONG Version, ControlMask, StateMask;
  };
  constexpr int kThreadPowerThrottling = 3;  // THREAD_INFORMATION_CLASS::ThreadPowerThrottling
  using Fn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
  static auto fn = reinterpret_cast<Fn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadInformation")));
  if (!fn) return;
  ThreadPowerThrottlingState s{1, 1 /*EXECUTION_SPEED*/, 0};
  fn(GetCurrentThread(), kThreadPowerThrottling, &s, sizeof(s));
}

}  // namespace

bool ParseHotkey(std::wstring_view text, HotkeySpec& out) {
  out = {};
  std::wstring s = str::ToLower(str::Trim(text));
  if (s.empty()) return false;
  // "Ctrl++" -> the last key is '+'
  std::wstring keyTail;
  if (s.size() >= 2 && s.back() == L'+' && s[s.size() - 2] == L'+') {
    keyTail = L"+";
    s.resize(s.size() - 2);
  } else if (s == L"+") {
    keyTail = L"+";
    s.clear();
  }
  std::vector<std::wstring> tokens;
  for (auto t : str::Split(s, L'+', false)) {
    auto tt = str::Trim(t);
    if (tt.empty()) return false;
    tokens.emplace_back(tt);
  }
  if (!keyTail.empty()) tokens.push_back(keyTail);
  if (tokens.empty()) return false;

  HotkeySpec hk;
  for (size_t i = 0; i < tokens.size(); ++i) {
    bool last = i + 1 == tokens.size();
    if (UINT m = ModifierFromName(tokens[i]); m && !last) {
      hk.mods |= m;
      continue;
    }
    if (!last) return false;  // a non-modifier before the end: "A+B"
    hk.vk = KeyFromName(tokens[i]);
    if (!hk.vk) return false;
  }
  out = hk;
  return true;
}

std::wstring FormatHotkey(const HotkeySpec& hk) {
  std::wstring s;
  if (hk.mods & MOD_CONTROL) s += L"Ctrl+";
  if (hk.mods & MOD_ALT) s += L"Alt+";
  if (hk.mods & MOD_SHIFT) s += L"Shift+";
  if (hk.mods & MOD_WIN) s += L"Win+";
  UINT vk = hk.vk;
  if (vk >= 'A' && vk <= 'Z') s += wchar_t(vk);
  else if (vk >= '0' && vk <= '9') s += wchar_t(vk);
  else if (vk >= VK_F1 && vk <= VK_F24) s += L"F" + std::to_wstring(vk - VK_F1 + 1);
  else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) s += L"Num" + std::to_wstring(vk - VK_NUMPAD0);
  else {
    switch (vk) {
      case VK_SPACE: s += L"Space"; break;
      case VK_RETURN: s += L"Enter"; break;
      case VK_TAB: s += L"Tab"; break;
      case VK_ESCAPE: s += L"Esc"; break;
      case VK_BACK: s += L"Backspace"; break;
      case VK_DELETE: s += L"Delete"; break;
      case VK_INSERT: s += L"Insert"; break;
      case VK_HOME: s += L"Home"; break;
      case VK_END: s += L"End"; break;
      case VK_PRIOR: s += L"PgUp"; break;
      case VK_NEXT: s += L"PgDn"; break;
      case VK_UP: s += L"Up"; break;
      case VK_DOWN: s += L"Down"; break;
      case VK_LEFT: s += L"Left"; break;
      case VK_RIGHT: s += L"Right"; break;
      case VK_PAUSE: s += L"Pause"; break;
      case VK_SNAPSHOT: s += L"PrtSc"; break;
      case VK_APPS: s += L"Menu"; break;
      case VK_OEM_3: s += L"`"; break;
      case VK_OEM_MINUS: s += L"-"; break;
      case VK_OEM_PLUS: s += L"="; break;
      case VK_OEM_4: s += L"["; break;
      case VK_OEM_6: s += L"]"; break;
      case VK_OEM_5: s += L"\\"; break;
      case VK_OEM_1: s += L";"; break;
      case VK_OEM_7: s += L"'"; break;
      case VK_OEM_COMMA: s += L","; break;
      case VK_OEM_PERIOD: s += L"."; break;
      case VK_OEM_2: s += L"/"; break;
      default: s += L"0x" + std::to_wstring(vk); break;
    }
  }
  return s;
}

HotkeyManager::HotkeyManager(HWND target, UINT hookMsg) : target_(target), hookMsg_(hookMsg) {}

HotkeyManager::~HotkeyManager() { Clear(); }

void HotkeyManager::Clear() {
  for (int i = 0; i < kMaxBindings; ++i) UnregisterHotKey(target_, kHotkeyId + i);
  StopHook();
  bindings_.clear();
  active_.clear();
}

const std::vector<HotkeyManager::Binding>& HotkeyManager::Apply(std::wstring_view primary,
                                                               std::wstring_view secondary) {
  Clear();
  std::vector<HotkeySpec> hooked;
  std::vector<HotkeySpec> seen;
  const std::wstring_view texts[kMaxBindings] = {primary, secondary};
  for (int i = 0; i < kMaxBindings; ++i) {
    if (str::Trim(texts[i]).empty()) continue;
    Binding b;
    b.source.assign(texts[i]);
    HotkeySpec hk;
    if (!ParseHotkey(texts[i], hk)) {
      b.how = How::Invalid;
      bindings_.push_back(std::move(b));
      continue;
    }
    b.text = FormatHotkey(hk);
    bool dup = false;
    for (auto& s2 : seen) dup |= s2 == hk;
    if (dup) continue;
    seen.push_back(hk);
    if (RegisterHotKey(target_, kHotkeyId + i, hk.mods | MOD_NOREPEAT, hk.vk)) {
      b.how = How::Registered;
    } else {
      // Owned by another app via RegisterHotKey: intercept at the input level (low-level hooks run before
      // hotkey matching).
      b.how = How::Hook;
      hooked.push_back(hk);
    }
    bindings_.push_back(std::move(b));
  }
  if (!hooked.empty() && !StartHook(hooked)) {
    for (auto& b : bindings_)
      if (b.how == How::Hook) b.how = How::Failed;
  }
  for (auto& b : bindings_) {
    if (b.how != How::Registered && b.how != How::Hook) continue;
    if (!active_.empty()) active_ += L" / ";
    active_ += b.text;
  }
  return bindings_;
}

void HotkeyManager::Rearm() {
  if (hookSpecs_.empty()) return;
  std::vector<HotkeySpec> specs = hookSpecs_;
  StopHook();
  if (!StartHook(specs)) {
    for (auto& b : bindings_)
      if (b.how == How::Hook) b.how = How::Failed;
  }
}

bool HotkeyManager::StartHook(const std::vector<HotkeySpec>& specs) {
  StopHook();
  for (int i = 0; i < HookState::kMax; ++i) {
    g_hook.mods[i] = size_t(i) < specs.size() ? specs[size_t(i)].mods : 0;
    g_hook.vk[i] = size_t(i) < specs.size() ? specs[size_t(i)].vk : 0;
  }
  g_hook.target = target_;
  g_hook.msg = hookMsg_;
  g_hook.swallowingVk = 0;

  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ready) return false;
  std::atomic<bool> ok{false};
  std::atomic<DWORD> tid{0};
  hookThread_ = std::thread([&ok, &tid, ready] {
    tid = GetCurrentThreadId();
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);  // create the queue before signalling
    OptOutOfPowerThrottling();
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    ok = hook != nullptr;
    SetEvent(ready);  // `ok`/`tid`/`ready` must not be touched after this point
    if (!hook) return;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    }
    UnhookWindowsHookEx(hook);
  });
  WaitForSingleObject(ready, INFINITE);
  CloseHandle(ready);
  hookThreadId_ = tid;
  if (!ok) {
    hookThread_.join();
    hookThreadId_ = 0;
    return false;
  }
  hookSpecs_ = specs;
  return true;
}

void HotkeyManager::StopHook() {
  if (!hookThread_.joinable()) return;
  PostThreadMessageW(hookThreadId_, WM_QUIT, 0, 0);
  hookThread_.join();
  hookThreadId_ = 0;
  hookSpecs_.clear();
  for (auto& v : g_hook.vk) v = 0;
  g_hook.swallowingVk = 0;
}

}  // namespace cs
