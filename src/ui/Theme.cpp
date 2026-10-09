#include "ui/Theme.h"

#include <dwmapi.h>

#include "platform/Win.h"

namespace cs::ui {

namespace {

// Not (all) present in MinGW headers.
constexpr DWORD kDwmUseImmersiveDarkModeOld = 19;
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmSystemBackdropType = 38;
constexpr DWORD kDwmTransitionsForceDisabled = 3;
constexpr int kDwmCornerRound = 2;
constexpr int kDwmSbtNone = 1;
constexpr int kDwmSbtMainWindow = 2;       // Mica
constexpr int kDwmSbtTransientWindow = 3;  // Acrylic

// Undocumented SetWindowCompositionAttribute (user32) — Win10 acrylic.
struct AccentPolicy {
  int accentState;
  int accentFlags;
  DWORD gradientColor;  // AABBGGRR
  int animationId;
};
struct WindowCompositionAttribData {
  int attrib;  // WCA_ACCENT_POLICY = 19
  void* data;
  SIZE_T size;
};
constexpr int kWcaAccentPolicy = 19;
constexpr int kAccentDisabled = 0;
constexpr int kAccentAcrylicBlurBehind = 4;
using SetWcaFn = BOOL(WINAPI*)(HWND, WindowCompositionAttribData*);

bool SetAccent(HWND hwnd, int state, DWORD abgr) {
  static const SetWcaFn fn = ProcAddress<SetWcaFn>(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
  if (!fn) return false;
  AccentPolicy policy{state, state == kAccentDisabled ? 0 : 2, abgr, 0};
  WindowCompositionAttribData d{kWcaAccentPolicy, &policy, sizeof(policy)};
  return fn(hwnd, &d) != FALSE;
}

}  // namespace

DWORD OsBuild() {
  static const DWORD build = [] {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto fn = ProcAddress<RtlGetVersionFn>(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (fn && fn(&vi) == 0) return vi.dwBuildNumber;
    return DWORD(0);
  }();
  return build;
}

bool ResolveDark(const std::wstring& theme) {
  if (theme == L"dark") return true;
  if (theme == L"light") return false;
  return win::SystemUsesDarkTheme();
}

D2D1_COLOR_F SystemAccent() {
  DWORD v = 0, sz = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD,
                   nullptr, &v, &sz) == ERROR_SUCCESS) {
    return Rgba(int(v & 0xFF), int((v >> 8) & 0xFF), int((v >> 16) & 0xFF));  // stored as AABBGGRR
  }
  DWORD argb = 0;
  BOOL opaque = FALSE;
  if (SUCCEEDED(DwmGetColorizationColor(&argb, &opaque)))
    return Rgba(int((argb >> 16) & 0xFF), int((argb >> 8) & 0xFF), int(argb & 0xFF));
  return Rgba(0, 103, 192);
}

BackdropKind ApplyBackdrop(HWND hwnd, const std::wstring& backdrop, bool dark) {
  const DWORD build = OsBuild();
  BOOL darkFlag = dark ? TRUE : FALSE;
  if (FAILED(DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkMode, &darkFlag, sizeof(darkFlag))))
    DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkModeOld, &darkFlag, sizeof(darkFlag));
  BOOL noTransitions = TRUE;  // we animate ourselves; DWM's popup fade only adds latency
  DwmSetWindowAttribute(hwnd, kDwmTransitionsForceDisabled, &noTransitions, sizeof(noTransitions));
  if (build >= 22000) {
    int corner = kDwmCornerRound;
    DwmSetWindowAttribute(hwnd, kDwmWindowCornerPreference, &corner, sizeof(corner));
  }

  // The whole client area is "frame": DWM composes our premultiplied pixels over the backdrop.
  MARGINS m{-1, -1, -1, -1};
  DwmExtendFrameIntoClientArea(hwnd, &m);

  const bool wantMica = backdrop == L"mica";
  const bool wantBlur = backdrop != L"none";
  if (build >= 22523) {  // DWMWA_SYSTEMBACKDROP_TYPE is available (Win11 22H2 and Insider builds before it)
    SetAccent(hwnd, kAccentDisabled, 0);
    int type = !wantBlur ? kDwmSbtNone : wantMica ? kDwmSbtMainWindow : kDwmSbtTransientWindow;
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, kDwmSystemBackdropType, &type, sizeof(type))) && wantBlur)
      return wantMica ? BackdropKind::SystemMica : BackdropKind::SystemAcrylic;
    return BackdropKind::Solid;
  }
  if (wantBlur) {
    // Tint is part of the accent (AABBGGRR). Fairly opaque: Win10 blur has no luminosity layer.
    const DWORD tint = dark ? 0xB01E1E1Eu : 0xB0F3F3F3u;
    if (SetAccent(hwnd, kAccentAcrylicBlurBehind, tint)) return BackdropKind::AccentBlur;
  } else {
    SetAccent(hwnd, kAccentDisabled, 0);
  }
  return BackdropKind::Solid;
}

Palette MakePalette(bool dark, BackdropKind kind) {
  Palette p;
  p.dark = dark;
  p.accent = SystemAccent();
  // Perceived luminance of the accent decides the text color on the selected row.
  const float lum = 0.2126f * p.accent.r + 0.7152f * p.accent.g + 0.0722f * p.accent.b;
  p.onAccent = lum > 0.62f ? Rgba(0, 0, 0, 0.9f) : Rgba(255, 255, 255);
  if (dark) {
    p.text = Rgba(255, 255, 255);
    p.text2 = Rgba(255, 255, 255, 0.55f);
    p.header = Rgba(255, 255, 255, 0.5f);
    p.separator = Rgba(255, 255, 255, 0.08f);
    p.hover = Rgba(255, 255, 255, 0.06f);
    p.scrollbar = Rgba(255, 255, 255, 0.35f);
    p.tileStroke = Rgba(255, 255, 255, 0.12f);
    switch (kind) {
      case BackdropKind::SystemAcrylic: p.background = Rgba(30, 30, 32, 0.32f); break;
      case BackdropKind::SystemMica: p.background = Rgba(30, 30, 32, 0.18f); break;
      case BackdropKind::AccentBlur: p.background = Rgba(0, 0, 0, 0.f); break;
      case BackdropKind::Solid: p.background = Rgba(36, 36, 38, 1.f); break;
    }
  } else {
    p.text = Rgba(0x1C, 0x1C, 0x1E);
    p.text2 = Rgba(0, 0, 0, 0.5f);
    p.header = Rgba(0, 0, 0, 0.48f);
    p.separator = Rgba(0, 0, 0, 0.08f);
    p.hover = Rgba(0, 0, 0, 0.05f);
    p.scrollbar = Rgba(0, 0, 0, 0.3f);
    p.tileStroke = Rgba(0, 0, 0, 0.10f);
    switch (kind) {
      case BackdropKind::SystemAcrylic: p.background = Rgba(246, 246, 246, 0.40f); break;
      case BackdropKind::SystemMica: p.background = Rgba(246, 246, 246, 0.22f); break;
      case BackdropKind::AccentBlur: p.background = Rgba(0, 0, 0, 0.f); break;
      case BackdropKind::Solid: p.background = Rgba(246, 246, 246, 1.f); break;
    }
  }
  p.textSelection = p.accent;
  p.textSelection.a = dark ? 0.55f : 0.35f;
  return p;
}

}  // namespace cs::ui
