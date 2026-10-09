#include "app/Tray.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

namespace cs {
namespace {

constexpr UINT kIconId = 1;
constexpr int kAppIconRes = 1;  // IDI_APP in res/app.rc

template <size_t N>
void CopyTrunc(wchar_t (&dst)[N], std::wstring_view src) {
  size_t n = src.size();
  if (n >= N) {
    n = N - 2;
    if (n > 0 && src[n - 1] >= 0xD800 && src[n - 1] <= 0xDBFF) --n;  // don't split a surrogate pair
    src.copy(dst, n);
    dst[n++] = L'…';
  } else {
    src.copy(dst, n);
  }
  dst[n] = 0;
}

DWORD WindowsBuild() {
  using Fn = void(WINAPI*)(DWORD*, DWORD*, DWORD*);
  auto fn = reinterpret_cast<Fn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers")));
  DWORD major = 0, minor = 0, build = 0;
  if (fn) fn(&major, &minor, &build);
  return major >= 10 ? (build & 0x0FFFFFFF) : 0;
}

}  // namespace

Tray::~Tray() {
  Destroy();
  FreeIcons();
}

void Tray::LoadIcons() {
  FreeIcons();
  LoadIconMetric(inst_, MAKEINTRESOURCEW(kAppIconRes), LIM_SMALL, &small_);
  LoadIconMetric(inst_, MAKEINTRESOURCEW(kAppIconRes), LIM_LARGE, &large_);
  if (!small_) small_ = LoadIconW(nullptr, IDI_APPLICATION);
}

void Tray::FreeIcons() {
  if (small_) DestroyIcon(small_);
  if (large_) DestroyIcon(large_);
  small_ = large_ = nullptr;
}

bool Tray::Create(HWND owner, HINSTANCE inst, UINT callbackMsg, std::wstring_view tooltip) {
  owner_ = owner;
  inst_ = inst;
  msg_ = callbackMsg;
  tip_ = tooltip;
  LoadIcons();
  return Add();
}

bool Tray::Add() {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = owner_;
  nid.uID = kIconId;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid.uCallbackMessage = msg_;
  nid.hIcon = small_;
  CopyTrunc(nid.szTip, tip_);
  added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
  if (added_) {
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
  }
  return added_;
}

void Tray::Destroy() {
  if (!added_) return;
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = owner_;
  nid.uID = kIconId;
  Shell_NotifyIconW(NIM_DELETE, &nid);
  added_ = false;
}

bool Tray::Recreate() {
  if (!owner_) return false;
  Destroy();
  LoadIcons();  // the DPI may have changed too
  return Add();
}

void Tray::SetTooltip(std::wstring_view tooltip) {
  tip_ = tooltip;
  if (!added_) return;
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = owner_;
  nid.uID = kIconId;
  nid.uFlags = NIF_TIP | NIF_SHOWTIP;
  CopyTrunc(nid.szTip, tip_);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void Tray::ShowBalloon(std::wstring_view title, std::wstring_view text) {
  if (!added_ && !Add()) return;
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = owner_;
  nid.uID = kIconId;
  nid.uFlags = NIF_INFO | NIF_SHOWTIP;
  CopyTrunc(nid.szInfoTitle, title);
  CopyTrunc(nid.szInfo, text.empty() ? std::wstring_view(L" ") : text);
  nid.dwInfoFlags = NIIF_RESPECT_QUIET_TIME;
  if (large_) {
    nid.dwInfoFlags |= NIIF_USER | NIIF_LARGE_ICON;
    nid.hBalloonIcon = large_;
  } else {
    nid.dwInfoFlags |= NIIF_INFO;
  }
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

Tray::Event Tray::HandleCallback(WPARAM wp, LPARAM lp, POINT& pt) const {
  switch (LOWORD(lp)) {
    case NIN_SELECT: return Event::Activate;
    case NIN_KEYSELECT: return Event::Open;  // may arrive twice for Enter, so it must not toggle
    case WM_CONTEXTMENU:
      pt = {GET_X_LPARAM(wp), GET_Y_LPARAM(wp)};
      return Event::Menu;
    default: return Event::None;
  }
}

UINT Tray::ShowMenu(POINT pt, std::wstring_view hotkeyText, bool autostartChecked, bool restartHint) {
  HMENU m = CreatePopupMenu();
  if (!m) return 0;
  std::wstring open = L"Открыть";
  if (!hotkeyText.empty()) open += L" (" + std::wstring(hotkeyText) + L")";
  AppendMenuW(m, MF_STRING, kCmdOpen, open.c_str());
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, kCmdSettings, L"Настройки…");
  AppendMenuW(m, MF_STRING, kCmdDataDir, L"Открыть папку данных");
  AppendMenuW(m, MF_STRING | (autostartChecked ? MF_CHECKED : MF_UNCHECKED), kCmdAutostart,
              L"Запускать при входе в Windows");
  AppendMenuW(m, MF_STRING, kCmdRestart, restartHint ? L"Перезапустить (применить настройки)" : L"Перезапустить");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, kCmdExit, L"Выход");
  SetMenuDefaultItem(m, kCmdOpen, FALSE);

  // Required so the menu closes when the user clicks elsewhere (KB135788).
  SetForegroundWindow(owner_);
  UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
  UINT cmd = UINT(TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align,
                                   pt.x, pt.y, owner_, nullptr));
  PostMessageW(owner_, WM_NULL, 0, 0);
  DestroyMenu(m);
  return cmd;
}

void Tray::ApplyMenuTheme(HWND owner, std::wstring_view theme) {
  // Undocumented uxtheme ordinals (stable since 1809/1903). SetPreferredAppMode's predecessor on 1809 took a
  // BOOL at the same ordinal, so any non-zero mode still means "dark allowed" there.
  using SetPreferredAppModeFn = int(WINAPI*)(int);
  using AllowDarkModeForWindowFn = bool(WINAPI*)(HWND, bool);
  using VoidFn = void(WINAPI*)();
  static HMODULE ux = WindowsBuild() >= 17763 ? LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)
                                              : nullptr;
  if (!ux) return;
  auto proc = [](int ordinal) {
    return reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(ordinal)));
  };
  auto setMode = reinterpret_cast<SetPreferredAppModeFn>(proc(135));
  auto allowWnd = reinterpret_cast<AllowDarkModeForWindowFn>(proc(133));
  auto refreshPolicy = reinterpret_cast<VoidFn>(proc(104));
  auto flushMenus = reinterpret_cast<VoidFn>(proc(136));
  if (!setMode) return;
  int mode = theme == L"dark" ? 2 /*ForceDark*/ : theme == L"light" ? 3 /*ForceLight*/ : 1 /*AllowDark*/;
  setMode(mode);
  if (allowWnd && owner) allowWnd(owner, mode != 3);
  if (refreshPolicy) refreshPolicy();
  if (flushMenus) flushMenus();
}

}  // namespace cs
