#pragma once
// A small clay-coloured pixel creature that hangs from the bottom edge of the launcher and swings.
// Lives in its own click-through layered window owned by the launcher (it has to be drawn outside the
// launcher's rounded rectangle). Costs nothing while idle: timers run only while it is visible, and the
// physics timer only while it is actually swinging.
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <cstdint>

namespace cs::ui {

class Mascot {
 public:
  enum class Mood : uint8_t { Idle, Happy, Sad };

  Mascot() = default;
  ~Mascot();
  Mascot(const Mascot&) = delete;
  Mascot& operator=(const Mascot&) = delete;

  bool Create(HINSTANCE inst, HWND owner, ID2D1Factory* factory);
  void Destroy();

  // Hang from the bottom edge of `ownerPx` (owner window rect, screen pixels).
  void Place(const RECT& ownerPx, UINT dpi);
  void Show();  // appears with a little swing
  void Hide();
  void SetEnabled(bool on);
  bool Enabled() const { return enabled_; }

  void Nudge(float impulse);  // adds angular velocity (rad/s); sign = direction
  void SetMood(Mood m);
  void SetLook(float dir);    // where the eyes look: -1 = left .. 1 = right

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  void OnTimer(UINT_PTR id);
  void StartSwing();
  void ScheduleBlink();
  bool EnsureSurface();
  void ReleaseSurface();
  bool EnsureSprites();
  void Render();

  HWND hwnd_ = nullptr;
  ID2D1Factory* factory_ = nullptr;  // owned by the launcher
  bool enabled_ = true;
  bool visible_ = false;
  UINT dpi_ = 96;
  POINT pos_{};   // window top-left, screen px
  SIZE size_{};   // window size, px
  int cell_ = 5;  // sprite pixel size in physical pixels

  // swing physics (pendulum around the hands)
  float theta_ = 0.f, omega_ = 0.f;
  double lastTick_ = 0;
  bool swinging_ = false;

  Mood mood_ = Mood::Idle;
  bool blink_ = false;
  int look_ = 0;  // eye offset in physical pixels
  bool spritesDirty_ = true;

  // Software DC render target over a 32-bit DIB, pushed with UpdateLayeredWindow.
  HDC memDc_ = nullptr;
  HBITMAP dib_ = nullptr;
  HGDIOBJ oldBmp_ = nullptr;
  Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> rt_;
  Microsoft::WRL::ComPtr<ID2D1Bitmap> body_, shadow_;
};

}  // namespace cs::ui
