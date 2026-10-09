#pragma once
// Internal implementation of cs::LauncherWindow. Split across LauncherWindow.cpp (window, input, behaviour)
// and LauncherRender.cpp (Direct2D/DirectWrite resources, layout and drawing).
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Types.h"
#include "ui/IconCache.h"
#include "ui/TextInput.h"
#include "ui/Theme.h"

namespace cs {
class SearchEngine;
struct Config;
}  // namespace cs

namespace cs::ui {

using Microsoft::WRL::ComPtr;

// Layout metrics, DIPs.
inline constexpr float kWidth = 680.f;
inline constexpr float kSearchH = 56.f;
inline constexpr float kRowH = 44.f;
inline constexpr float kTopRowH = 52.f;
inline constexpr float kHeaderH = 26.f;
inline constexpr float kListPad = 6.f;
inline constexpr float kFooterH = 28.f;
inline constexpr float kRowInset = 8.f;   // selection rect inset from the window edges
inline constexpr float kRowRadius = 8.f;
inline constexpr float kIcon = 28.f;
inline constexpr float kTopIcon = 36.f;
inline constexpr float kFieldLeft = 54.f;
inline constexpr float kFieldRight = 20.f;
inline constexpr float kHintW = 40.f;
inline constexpr float kTitleLineH = 20.f;
inline constexpr float kTopTitleLineH = 23.f;
inline constexpr float kSubLineH = 16.f;

inline constexpr int kTileColorCount = 9;

double NowMs();

// D2DERR_RECREATE_TARGET is an unsigned literal in MinGW headers (-Wsign-compare); compare against this instead.
inline constexpr HRESULT kRecreateTarget = HRESULT(0x8899000CL);  // monotonic milliseconds (QueryPerformanceCounter)

struct Row {
  size_t result = 0;          // index into SearchEngine::Last()
  bool top = false;           // the "top hit" row
  bool headerAbove = false;   // a section header sits directly above (keep it visible when scrolling up)
  uint8_t tile = 0;           // glyph tile color index
  float y = 0, h = 0;         // list-content coordinates
  bool laidOut = false;
  ComPtr<IDWriteTextLayout> title, subtitle, glyph;
  ComPtr<ID2D1Bitmap> icon;
  IconCache::State iconState = IconCache::State::Loading;
};

struct Header {
  float y = 0;
  ComPtr<IDWriteTextLayout> layout;
};

class LauncherImpl {
 public:
  LauncherImpl(SearchEngine& engine, IHost& host);
  ~LauncherImpl();

  bool Create(HINSTANCE inst, const Config& cfg);
  void Toggle();
  void Show();
  void Hide();
  bool IsVisible() const { return visible_; }
  void Refresh();
  void ApplyConfig(const Config& cfg);
  void OnSystemThemeChanged();
  HWND Hwnd() const { return hwnd_; }

 private:
  // ---- window / behaviour (LauncherWindow.cpp)
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
  bool OnKeyDown(WPARAM vk);
  void OnChar(wchar_t c);
  void OnMouseDown(float x, float y, bool dbl);
  void OnMouseMove(float x, float y, POINT screen);
  void OnMouseUp(float x, float y);
  void OnWheel(int delta);
  bool InField(float x, float y) const;
  int RowAt(float y) const;
  size_t TextPosAt(float x) const;

  void ApplyTheme();
  void SetDpi(UINT dpi);
  void PlaceWindow();  // size/position for the current monitor, row count and DPI
  void ForceForeground();

  void TextChanged();
  void CaretMoved();
  void RunQuery(const std::wstring* keepKey);
  void Select(int row, bool animate);
  void MoveSelection(int delta, bool wrap);
  void EnsureVisible(int row, bool smooth);
  void Activate(int row, Action a);
  void CopyOrResultCopy(bool forceResult);
  void Paste();
  int VisibleRowByOrdinal(int n) const;

  void ResetCaretBlink();
  void StartAnim();
  void Tick();
  void Invalidate();
  void UpdateIme();
  float ScrollbarAlpha(double now) const;

  // ---- rendering (LauncherRender.cpp)
  bool CreateDeviceIndependentResources();
  bool CreateDeviceResources();
  void DiscardDeviceResources();
  void Render();
  void Draw();
  void DrawSearch();
  void DrawList();
  void DrawRow(Row& r, bool selected);
  void DrawIcon(Row& r, const Result& res, float x, float y, float size, bool selected);
  void DrawFooter(float y);
  void BuildRows();
  void LayoutRow(Row& r, const Result& res);
  void UpdateQueryLayout();
  void UpdateTextScroll();
  void UpdateFooter();
  float CaretX() const;
  float ContentH() const { return contentH_; }
  float MaxViewportH() const;
  float ViewportH() const;
  float MaxScroll() const;
  float WindowHeightDip() const;
  float MaxWindowHeightDip() const;
  ID2D1SolidColorBrush* Brush(const D2D1_COLOR_F& c, float alphaMul = 1.f);
  int Px(float dip) const { return int(dip * float(dpi_) / 96.f + 0.5f); }
  float Dip(int px) const { return float(px) * 96.f / float(dpi_); }
  float OnePx() const { return 96.f / float(dpi_); }

  SearchEngine& engine_;
  IHost& host_;

  HWND hwnd_ = nullptr;
  UINT dpi_ = 96;
  RECT work_{0, 0, 1920, 1080};  // work area of the monitor we show on
  bool visible_ = false;
  bool focused_ = false;
  bool destroying_ = false;
  bool executing_ = false;
  bool refreshPending_ = false;
  int positioning_ = 0;  // >0 while we move/resize the window ourselves

  // config
  std::wstring theme_ = L"system";
  std::wstring backdrop_ = L"acrylic";
  int visibleRows_ = 8;
  bool dark_ = true;
  BackdropKind backdropKind_ = BackdropKind::Solid;
  Palette pal_;

  // text field
  TextInput input_;
  wchar_t pendingHigh_ = 0;
  float textScrollX_ = 0;
  bool caretOn_ = true;
  bool caretTimer_ = false;
  double lastInput_ = 0;
  bool dragging_ = false;

  // results
  std::vector<Row> rows_;
  std::vector<Header> headers_;
  float contentH_ = 0;
  int sel_ = -1;
  int hover_ = -1;
  int pressRow_ = -1;
  bool trackingMouse_ = false;
  POINT lastMouse_{-100000, -100000};

  // animation
  bool animTimer_ = false;
  double lastTick_ = 0;
  double showStart_ = 0;
  float showT_ = 1.f;
  bool selAnimating_ = false;
  double selAnimStart_ = 0;
  float selFromY_ = 0, selFromH_ = 0;
  float selVisY_ = 0, selVisH_ = 0;
  float scrollY_ = 0, scrollTarget_ = 0;
  double scrollActivity_ = -1e9;
  float opacity_ = 1.f;  // content opacity for the current frame

  // footer
  int footerKey_ = -1;
  ComPtr<IDWriteTextLayout> footer_;

  // device-independent
  ComPtr<ID2D1Factory> d2d_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<IDWriteTextFormat> fmtQuery_, fmtSearchGlyph_, fmtTitle_, fmtTopTitle_, fmtSub_, fmtHeader_,
      fmtFooter_, fmtHint_, fmtTileGlyph_, fmtTopTileGlyph_, fmtLoadGlyph_, fmtTopLoadGlyph_;
  ComPtr<IDWriteInlineObject> ellipsisTitle_, ellipsisTopTitle_, ellipsisSub_, ellipsisFooter_;
  ComPtr<IDWriteTextLayout> queryLayout_, placeholder_, searchGlyph_, hint_;
  std::wstring iconFont_;

  // device-dependent
  ComPtr<ID2D1HwndRenderTarget> rt_;
  ComPtr<ID2D1SolidColorBrush> brush_;
  std::array<ComPtr<ID2D1LinearGradientBrush>, kTileColorCount> tiles_;

  IconCache icons_;
};

}  // namespace cs::ui
