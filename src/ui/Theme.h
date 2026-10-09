#pragma once
// Colors, accent color, OS version and DWM backdrop (Acrylic/Mica/rounded corners) for the launcher window.
#include <windows.h>
#include <d2d1.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace cs::ui {

// What the DWM actually gives us behind the client area (decides how much tint we paint ourselves).
enum class BackdropKind : uint8_t {
  Solid,          // no blur: we paint an opaque background
  SystemAcrylic,  // Win11 22H2+: DWMSBT_TRANSIENTWINDOW
  SystemMica,     // Win11 22H2+: DWMSBT_MAINWINDOW
  AccentBlur,     // Win10 / Win11 21H2: SetWindowCompositionAttribute(ACCENT_ENABLE_ACRYLICBLURBEHIND), tint included
};

struct Palette {
  bool dark = true;
  D2D1_COLOR_F text{};        // primary text
  D2D1_COLOR_F text2{};       // secondary text (subtitles, placeholder, footer)
  D2D1_COLOR_F header{};      // section headers
  D2D1_COLOR_F separator{};
  D2D1_COLOR_F hover{};
  D2D1_COLOR_F background{};  // painted over the whole client area (tint over the backdrop / solid fill)
  D2D1_COLOR_F accent{};      // selected row fill, caret
  D2D1_COLOR_F onAccent{};    // text on the selected row
  D2D1_COLOR_F textSelection{};
  D2D1_COLOR_F scrollbar{};
  D2D1_COLOR_F tileStroke{};  // thin outline around glyph tiles
};

// Real OS build number (RtlGetVersion — not subject to manifest lies).
DWORD OsBuild();

// cfg.theme: "dark" | "light" | anything else = follow the system.
bool ResolveDark(const std::wstring& theme);

// System accent color (HKCU\Software\Microsoft\Windows\DWM\AccentColor, then DwmGetColorizationColor).
D2D1_COLOR_F SystemAccent();

// Applies dark-mode flag, rounded corners, frame extension and the backdrop. Returns what was achieved.
BackdropKind ApplyBackdrop(HWND hwnd, const std::wstring& backdrop, bool dark);

Palette MakePalette(bool dark, BackdropKind kind);

inline D2D1_COLOR_F Rgba(int r, int g, int b, float a = 1.f) {
  return D2D1::ColorF(float(r) / 255.f, float(g) / 255.f, float(b) / 255.f, a);
}

// GetProcAddress without -Wcast-function-type noise.
template <class F>
F ProcAddress(HMODULE m, const char* name) {
  FARPROC p = m ? GetProcAddress(m, name) : nullptr;
  F f;
  static_assert(sizeof(f) == sizeof(p));
  std::memcpy(&f, &p, sizeof(f));
  return f;
}

}  // namespace cs::ui
