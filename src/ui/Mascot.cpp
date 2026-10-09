#include "ui/Mascot.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace cs::ui {

namespace {

constexpr wchar_t kClassName[] = L"CloudSpotlight.Mascot";
constexpr UINT_PTR kTimerSwing = 1;
constexpr UINT_PTR kTimerBlinkStart = 2;
constexpr UINT_PTR kTimerBlinkEnd = 3;

// 13x9 sprite, hanging pose: hands (row 0) grip the launcher's bottom edge, arms up, body, legs dangling.
constexpr int kW = 13, kH = 9;
constexpr const char* kSprite[kH] = {
    "..a.......a..",  //
    "..a.......a..",  //
    ".ttttttttttt.",  // t: body, lit top row
    ".bbbbbbbbbbb.",  //
    ".bbbbbbbbbbb.",  //
    ".bbbbbbbbbbb.",  //
    ".ddddddddddd.",  // d: body, shaded bottom row
    "..a.a...a.a..",  //
    "..a.a...a.a..",  //
};
constexpr float kPivotX = 6.5f, kPivotY = 0.5f;  // sprite cells: between the hands
constexpr float kCellDip = 5.f;
constexpr float kAnchorFromRightDip = 74.f;     // pivot distance from the launcher's right edge
constexpr float kMarginX = 6.f, kAbove = 1.5f, kBelow = 2.f;  // window margins, in cells (room to swing)

struct Bgra {
  uint8_t b, g, r, a;
};
constexpr Bgra kClay{0x57, 0x77, 0xD9, 255};       // #D97757
constexpr Bgra kClayLight{0x6D, 0x8A, 0xE0, 255};  // #E08A6D
constexpr Bgra kClayDark{0x47, 0x66, 0xC4, 255};   // #C46647
constexpr Bgra kLimb{0x4B, 0x6A, 0xC9, 255};       // #C96A4B
constexpr Bgra kEye{0x1D, 0x1E, 0x1F, 255};        // #1F1E1D
constexpr Bgra kShadow{0, 0, 0, 56};               // premultiplied black, ~22%

// Pendulum: natural period ~1.2 s, amplitude halves in ~0.9 s.
constexpr float kOmega0Sq = 27.4f;
constexpr float kDamping = 1.5f;

double NowSec() {
  static LARGE_INTEGER freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f;
  }();
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return double(t.QuadPart) / double(freq.QuadPart);
}

}  // namespace

Mascot::~Mascot() { Destroy(); }

bool Mascot::Create(HINSTANCE inst, HWND owner, ID2D1Factory* factory) {
  if (hwnd_) return true;
  factory_ = factory;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &Mascot::WndProc;
  wc.hInstance = inst;
  wc.lpszClassName = kClassName;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
  // Owned by the launcher (stays above it), click-through, never activated, not in Alt+Tab.
  hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                          kClassName, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, inst, this);
  return hwnd_ != nullptr;
}

void Mascot::Destroy() {
  ReleaseSurface();
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

LRESULT CALLBACK Mascot::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* self = reinterpret_cast<Mascot*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_TIMER:
      if (self) self->OnTimer(wp);
      return 0;
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

void Mascot::Place(const RECT& ownerPx, UINT dpi) {
  if (!hwnd_) return;
  if (dpi && dpi != dpi_) {
    dpi_ = dpi;
    spritesDirty_ = true;
  }
  const int cell = std::max(2, int(std::lround(kCellDip * float(dpi_) / 96.f)));
  if (cell != cell_) {
    cell_ = cell;
    spritesDirty_ = true;
  }
  const SIZE size{LONG(float(kW + 2.f * kMarginX) * float(cell_)), LONG(float(kAbove + kH + kBelow) * float(cell_))};
  const int pivotX = ownerPx.right - int(std::lround(kAnchorFromRightDip * float(dpi_) / 96.f));
  const POINT pos{LONG(pivotX - size.cx / 2), LONG(ownerPx.bottom - int(kAbove * float(cell_)))};
  const bool resized = size.cx != size_.cx || size.cy != size_.cy;
  const bool moved = pos.x != pos_.x || pos.y != pos_.y;
  size_ = size;
  pos_ = pos;
  if (resized) ReleaseSurface();
  if ((resized || moved) && visible_) Render();
}

void Mascot::SetEnabled(bool on) {
  enabled_ = on;
  if (!on) Hide();
}

void Mascot::Show() {
  if (!hwnd_ || !enabled_ || visible_) return;
  visible_ = true;
  theta_ = 0.f;
  omega_ = (std::rand() & 1) ? 1.3f : -1.3f;  // ~14 degrees: "just grabbed the edge"
  blink_ = false;
  Render();
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  StartSwing();
  ScheduleBlink();
}

void Mascot::Hide() {
  if (!hwnd_ || !visible_) return;
  visible_ = false;
  swinging_ = false;
  KillTimer(hwnd_, kTimerSwing);
  KillTimer(hwnd_, kTimerBlinkStart);
  KillTimer(hwnd_, kTimerBlinkEnd);
  ShowWindow(hwnd_, SW_HIDE);
}

void Mascot::Nudge(float impulse) {
  if (!visible_) return;
  omega_ = std::clamp(omega_ + impulse, -2.5f, 2.5f);
  StartSwing();
}

void Mascot::SetMood(Mood m) {
  if (m == mood_) return;
  mood_ = m;
  spritesDirty_ = true;
  if (visible_) Render();
}

void Mascot::SetLook(float dir) {
  const int look = int(std::lround(std::clamp(dir, -1.f, 1.f) * float(cell_) * 0.4f));
  if (look == look_) return;
  look_ = look;
  spritesDirty_ = true;
  if (visible_ && !swinging_) Render();  // while swinging the next frame picks it up
}

void Mascot::StartSwing() {
  if (swinging_) return;
  swinging_ = true;
  lastTick_ = NowSec();
  SetTimer(hwnd_, kTimerSwing, 15, nullptr);
}

void Mascot::ScheduleBlink() { SetTimer(hwnd_, kTimerBlinkStart, UINT(2500 + std::rand() % 3500), nullptr); }

void Mascot::OnTimer(UINT_PTR id) {
  if (!visible_) {
    KillTimer(hwnd_, id);
    return;
  }
  if (id == kTimerSwing) {
    const double now = NowSec();
    float dt = float(std::min(0.05, now - lastTick_));
    lastTick_ = now;
    // Semi-implicit Euler in two half steps: stable and cheap.
    for (int i = 0; i < 2; ++i) {
      const float h = dt * 0.5f;
      omega_ += (-kOmega0Sq * std::sin(theta_) - kDamping * omega_) * h;
      theta_ += omega_ * h;
    }
    if (std::fabs(theta_) < 0.002f && std::fabs(omega_) < 0.02f) {
      theta_ = omega_ = 0.f;
      swinging_ = false;
      KillTimer(hwnd_, kTimerSwing);
    }
    Render();
  } else if (id == kTimerBlinkStart) {
    KillTimer(hwnd_, kTimerBlinkStart);
    blink_ = true;
    spritesDirty_ = true;
    Render();
    SetTimer(hwnd_, kTimerBlinkEnd, 130, nullptr);
  } else if (id == kTimerBlinkEnd) {
    KillTimer(hwnd_, kTimerBlinkEnd);
    blink_ = false;
    spritesDirty_ = true;
    Render();
    ScheduleBlink();
  }
}

bool Mascot::EnsureSurface() {
  if (rt_ && memDc_) return true;
  if (!factory_ || size_.cx <= 0 || size_.cy <= 0) return false;
  HDC screen = GetDC(nullptr);
  memDc_ = CreateCompatibleDC(screen);
  ReleaseDC(nullptr, screen);
  if (!memDc_) return false;
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = size_.cx;
  bi.bmiHeader.biHeight = -size_.cy;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  dib_ = CreateDIBSection(memDc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib_) {
    ReleaseSurface();
    return false;
  }
  oldBmp_ = SelectObject(memDc_, dib_);
  // Software target: the surface is tiny, and this avoids a GPU round trip per frame.
  const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
      96.f, 96.f);  // work in physical pixels
  if (FAILED(factory_->CreateDCRenderTarget(&props, rt_.ReleaseAndGetAddressOf()))) {
    ReleaseSurface();
    return false;
  }
  const RECT rc{0, 0, size_.cx, size_.cy};
  if (FAILED(rt_->BindDC(memDc_, &rc))) {
    ReleaseSurface();
    return false;
  }
  spritesDirty_ = true;
  return true;
}

void Mascot::ReleaseSurface() {
  body_.Reset();
  shadow_.Reset();
  rt_.Reset();
  if (memDc_) {
    if (oldBmp_) SelectObject(memDc_, oldBmp_);
    DeleteDC(memDc_);
    memDc_ = nullptr;
  }
  oldBmp_ = nullptr;
  if (dib_) {
    DeleteObject(dib_);
    dib_ = nullptr;
  }
}

bool Mascot::EnsureSprites() {
  if (!spritesDirty_ && body_ && shadow_) return true;
  const int c = cell_;
  const UINT32 w = UINT32(kW * c), h = UINT32(kH * c);
  std::vector<Bgra> body(size_t(w) * h, Bgra{0, 0, 0, 0}), shadow(size_t(w) * h, Bgra{0, 0, 0, 0});
  auto fill = [&](std::vector<Bgra>& px, int x0, int y0, int x1, int y1, Bgra col) {
    x0 = std::max(x0, 0), y0 = std::max(y0, 0);
    x1 = std::min(x1, int(w)), y1 = std::min(y1, int(h));
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) px[size_t(y) * w + size_t(x)] = col;
  };
  for (int r = 0; r < kH; ++r) {
    for (int k = 0; k < kW; ++k) {
      Bgra col;
      switch (kSprite[r][k]) {
        case 't': col = kClayLight; break;
        case 'b': col = kClay; break;
        case 'd': col = kClayDark; break;
        case 'a': col = kLimb; break;
        default: continue;
      }
      fill(body, k * c, r * c, (k + 1) * c, (r + 1) * c, col);
      fill(shadow, k * c, r * c, (k + 1) * c, (r + 1) * c, kShadow);
    }
  }
  // Eyes (columns 4 and 8). Sub-cell horizontal offset follows the caret.
  auto eyeCell = [&](int col, int row, float top = 0.f, float height = 1.f) {
    const int x = col * c + look_;
    const int y0 = row * c + int(std::lround(top * float(c)));
    fill(body, x, y0, x + c, y0 + std::max(1, int(std::lround(height * float(c)))), kEye);
  };
  if (blink_) {
    eyeCell(4, 4, 0.35f, 0.4f);
    eyeCell(8, 4, 0.35f, 0.4f);
  } else {
    switch (mood_) {
      case Mood::Idle:
        for (int col : {4, 8}) {
          eyeCell(col, 3);
          eyeCell(col, 4);
        }
        break;
      case Mood::Sad:  // looking down, droopy
        for (int col : {4, 8}) eyeCell(col, 4, 0.2f, 0.8f);
        eyeCell(3, 3, 0.55f, 0.35f);
        eyeCell(9, 3, 0.55f, 0.35f);
        break;
      case Mood::Happy:  // ^ ^
        for (int col : {4, 8}) {
          eyeCell(col - 1, 4, 0.f, 0.6f);
          eyeCell(col, 3, 0.4f, 0.6f);
          eyeCell(col + 1, 4, 0.f, 0.6f);
        }
        break;
    }
  }
  const D2D1_BITMAP_PROPERTIES bp =
      D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
  if (FAILED(rt_->CreateBitmap(D2D1::SizeU(w, h), body.data(), w * 4, &bp, body_.ReleaseAndGetAddressOf())) ||
      FAILED(rt_->CreateBitmap(D2D1::SizeU(w, h), shadow.data(), w * 4, &bp, shadow_.ReleaseAndGetAddressOf())))
    return false;
  spritesDirty_ = false;
  return true;
}

void Mascot::Render() {
  if (!hwnd_ || !EnsureSurface()) return;
  rt_->BeginDraw();
  if (!EnsureSprites()) {
    rt_->EndDraw();
    return;
  }
  rt_->Clear(D2D1::ColorF(0, 0, 0, 0));
  const float c = float(cell_);
  const float px = std::floor(float(size_.cx) * 0.5f), py = kAbove * c;  // pivot in window pixels
  const D2D1_RECT_F dst = D2D1::RectF(px - kPivotX * c, py - kPivotY * c, px + (kW - kPivotX) * c,
                                      py + (kH - kPivotY) * c);
  const bool still = std::fabs(theta_) < 0.003f;
  const auto interp = still ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
  const D2D1_MATRIX_3X2_F rot =
      still ? D2D1::Matrix3x2F::Identity()
            : D2D1::Matrix3x2F::Rotation(theta_ * 57.29578f, D2D1::Point2F(px, py));
  // Soft drop shadow straight down (applied after the rotation), then the body.
  rt_->SetTransform(rot * D2D1::Matrix3x2F::Translation(c * 0.35f, c * 0.75f));
  rt_->DrawBitmap(shadow_.Get(), dst, 1.f, interp);
  rt_->SetTransform(rot);
  rt_->DrawBitmap(body_.Get(), dst, 1.f, interp);
  rt_->SetTransform(D2D1::Matrix3x2F::Identity());
  if (rt_->EndDraw() == HRESULT(0x8899000CL)) {  // D2DERR_RECREATE_TARGET
    ReleaseSurface();
    return;
  }
  POINT src{0, 0};
  BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  POINT pos = pos_;
  SIZE size = size_;
  UpdateLayeredWindow(hwnd_, nullptr, &pos, &size, memDc_, &src, 0, &bf, ULW_ALPHA);
}

}  // namespace cs::ui
