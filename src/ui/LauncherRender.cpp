// Direct2D / DirectWrite part of the launcher: resources, list layout and drawing.
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <string>

#include "core/SearchEngine.h"
#include "ui/LauncherImpl.h"

namespace cs::ui {

namespace {

constexpr wchar_t kGlyphSearch[] = L"\uE721";
constexpr wchar_t kGlyphDocument[] = L"\uE8A5";
constexpr wchar_t kGlyphFolder[] = L"\uE8B7";
constexpr wchar_t kGlyphApp[] = L"\uE71D";

struct TileColors {
  uint8_t r1, g1, b1, r2, g2, b2;  // top -> bottom
  const wchar_t* glyph;            // default glyph when the provider sent none
};
constexpr TileColors kTiles[kTileColorCount] = {
    {150, 148, 140, 105, 103, 97, L"\uE8A5"},   // other: warm gray
    {226, 183, 148, 196, 141, 102, L"\uE8EF"},  // Калькулятор: kraft
    {148, 168, 117, 104, 125, 78, L"\uE8EF"},   // Конвертер: olive
    {232, 141, 110, 201, 100, 66, L"\uE756"},   // Команды: clay
    {140, 138, 130, 94, 93, 89, L"\uE713"},     // Настройки: slate
    {137, 177, 216, 90, 136, 186, L"\uE774"},   // Интернет: sky
    {212, 130, 160, 176, 88, 122, L"\uE71D"},   // Приложения: fig
    {222, 190, 150, 190, 150, 105, L"\uE8B7"},  // Папки: oat/kraft
    {176, 172, 162, 128, 125, 117, L"\uE8A5"},  // Файлы: cloud gray
};

uint8_t TileFor(const std::wstring& category) {
  static const wchar_t* const kNames[kTileColorCount] = {
      L"", L"Калькулятор", L"Конвертер", L"Команды", L"Настройки", L"Интернет", L"Приложения", L"Папки", L"Файлы"};
  for (int i = 1; i < kTileColorCount; ++i)
    if (category == kNames[i]) return uint8_t(i);
  return 0;
}

bool IsCalcCategory(const std::wstring& c) { return c == L"Калькулятор" || c == L"Конвертер"; }

const DWRITE_TRIMMING kTrimChar{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};

// "C:\Users\me\very\long\path\file.txt" -> "C:\Users\me\ve…path\file.txt" (keeps the informative tail).
std::wstring MiddleEllipsis(const std::wstring& s, float fullW, float maxW) {
  const size_t n = s.size();
  if (fullW <= 0.f) return s;
  size_t keep = size_t(float(n) * (maxW / fullW) * 0.92f);
  if (keep < 10 || keep >= n) return s;
  keep -= 1;
  size_t head = keep * 2 / 5;
  size_t tail = keep - head;
  if (head > 0 && s[head - 1] >= 0xD800 && s[head - 1] <= 0xDBFF) --head;
  if (tail > 0 && s[n - tail] >= 0xDC00 && s[n - tail] <= 0xDFFF) --tail;
  std::wstring out;
  out.reserve(head + tail + 1);
  out.append(s, 0, head);
  out.push_back(L'\u2026');
  out.append(s, n - tail, tail);
  return out;
}

struct RowGeom {
  float iconSize, iconX, textX, maxTextW, titleLineH;
};
RowGeom GeomFor(bool top) {
  RowGeom g{};
  g.iconSize = top ? kTopIcon : kIcon;
  g.iconX = kRowInset + (top ? 8.f : 10.f);
  g.textX = g.iconX + g.iconSize + 12.f;
  g.maxTextW = kWidth - kRowInset - kHintW - g.textX;
  g.titleLineH = top ? kTopTitleLineH : kTitleLineH;
  return g;
}

D2D1_RECT_F Rect(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------------------------------------------

bool LauncherImpl::CreateDeviceIndependentResources() {
  if (d2d_) return true;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                               reinterpret_cast<void**>(d2d_.ReleaseAndGetAddressOf()))))
    return false;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(dwrite_.ReleaseAndGetAddressOf()))))
    return false;

  ComPtr<IDWriteFontCollection> fonts;
  dwrite_->GetSystemFontCollection(fonts.GetAddressOf(), FALSE);
  auto has = [&](const wchar_t* name) {
    UINT32 idx = 0;
    BOOL exists = FALSE;
    return fonts && SUCCEEDED(fonts->FindFamilyName(name, &idx, &exists)) && exists;
  };
  const wchar_t* display = has(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : L"Segoe UI";
  const wchar_t* text = has(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
  iconFont_ = has(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
  // Claude's voice is a warm serif; Georgia ships with every Windows and covers Cyrillic.
  const wchar_t* serif = has(L"Georgia") ? L"Georgia" : display;

  bool ok = true;
  auto make = [&](const wchar_t* family, DWRITE_FONT_WEIGHT weight, float size, DWRITE_TEXT_ALIGNMENT ta,
                  DWRITE_PARAGRAPH_ALIGNMENT pa, ComPtr<IDWriteTextFormat>& out, ComPtr<IDWriteInlineObject>* ellipsis) {
    if (FAILED(dwrite_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         size, L"ru-ru", out.ReleaseAndGetAddressOf()))) {
      ok = false;
      return;
    }
    out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    out->SetTextAlignment(ta);
    out->SetParagraphAlignment(pa);
    if (ellipsis && SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(out.Get(), ellipsis->ReleaseAndGetAddressOf())))
      out->SetTrimming(&kTrimChar, ellipsis->Get());
  };
  const auto lead = DWRITE_TEXT_ALIGNMENT_LEADING, mid = DWRITE_TEXT_ALIGNMENT_CENTER,
             trail = DWRITE_TEXT_ALIGNMENT_TRAILING;
  const auto pc = DWRITE_PARAGRAPH_ALIGNMENT_CENTER, pf = DWRITE_PARAGRAPH_ALIGNMENT_FAR;
  const wchar_t* icons = iconFont_.c_str();
  make(serif, DWRITE_FONT_WEIGHT_NORMAL, 21.f, lead, pc, fmtQuery_, nullptr);
  make(icons, DWRITE_FONT_WEIGHT_NORMAL, 20.f, mid, pc, fmtSearchGlyph_, nullptr);
  make(text, DWRITE_FONT_WEIGHT_NORMAL, 15.f, lead, pc, fmtTitle_, &ellipsisTitle_);
  make(serif, DWRITE_FONT_WEIGHT_NORMAL, 17.f, lead, pc, fmtTopTitle_, &ellipsisTopTitle_);
  make(text, DWRITE_FONT_WEIGHT_NORMAL, 12.f, lead, pc, fmtSub_, nullptr);  // trimming is set per layout
  if (fmtSub_) dwrite_->CreateEllipsisTrimmingSign(fmtSub_.Get(), ellipsisSub_.ReleaseAndGetAddressOf());
  make(text, DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.5f, lead, pf, fmtHeader_, nullptr);
  make(text, DWRITE_FONT_WEIGHT_NORMAL, 11.5f, lead, pc, fmtFooter_, &ellipsisFooter_);
  make(text, DWRITE_FONT_WEIGHT_NORMAL, 14.f, trail, pc, fmtHint_, nullptr);
  make(icons, DWRITE_FONT_WEIGHT_NORMAL, 15.f, mid, pc, fmtTileGlyph_, nullptr);
  make(icons, DWRITE_FONT_WEIGHT_NORMAL, 19.f, mid, pc, fmtTopTileGlyph_, nullptr);
  make(icons, DWRITE_FONT_WEIGHT_NORMAL, 20.f, mid, pc, fmtLoadGlyph_, nullptr);
  make(icons, DWRITE_FONT_WEIGHT_NORMAL, 25.f, mid, pc, fmtTopLoadGlyph_, nullptr);
  if (!ok) return false;
  const D2D1_STROKE_STYLE_PROPERTIES round = D2D1::StrokeStyleProperties(
      D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
  d2d_->CreateStrokeStyle(&round, nullptr, 0, roundCap_.ReleaseAndGetAddressOf());

  static constexpr wchar_t kPlaceholder[] = L"Что найти?";
  dwrite_->CreateTextLayout(kPlaceholder, UINT32(std::size(kPlaceholder) - 1), fmtQuery_.Get(),
                            kWidth - kFieldLeft - kFieldRight, kSearchH, placeholder_.ReleaseAndGetAddressOf());
  dwrite_->CreateTextLayout(kGlyphSearch, 1, fmtSearchGlyph_.Get(), 36.f, kSearchH,
                            searchGlyph_.ReleaseAndGetAddressOf());
  dwrite_->CreateTextLayout(L"\u21B5", 1, fmtHint_.Get(), kHintW - 12.f, 20.f, hint_.ReleaseAndGetAddressOf());
  // Warm up font loading so the first Show() does not pay for it.
  DWRITE_TEXT_METRICS tm{};
  if (placeholder_) placeholder_->GetMetrics(&tm);
  if (searchGlyph_) searchGlyph_->GetMetrics(&tm);
  return true;
}

bool LauncherImpl::CreateDeviceResources() {
  if (rt_) return true;
  if (!hwnd_ || !d2d_) return false;
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const D2D1_SIZE_U size = D2D1::SizeU(UINT32(std::max<LONG>(1, rc.right - rc.left)),
                                       UINT32(std::max<LONG>(1, rc.bottom - rc.top)));
  // Premultiplied alpha + frame extended over the whole client area: transparent pixels show the DWM backdrop.
  const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
      96.f, 96.f);
  // IMMEDIATELY: Present never blocks the UI thread waiting for vsync (input stays responsive while animating).
  const D2D1_HWND_RENDER_TARGET_PROPERTIES hprops =
      D2D1::HwndRenderTargetProperties(hwnd_, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY);
  if (FAILED(d2d_->CreateHwndRenderTarget(&props, &hprops, rt_.ReleaseAndGetAddressOf()))) return false;
  rt_->SetDpi(float(dpi_), float(dpi_));
  rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);  // ClearType is wrong on transparent targets
  const D2D1_COLOR_F black = D2D1::ColorF(0, 0, 0, 1);
  if (FAILED(rt_->CreateSolidColorBrush(&black, nullptr, brush_.ReleaseAndGetAddressOf()))) {
    rt_.Reset();
    return false;
  }
  return true;
}

void LauncherImpl::DiscardDeviceResources() {
  brush_.Reset();
  for (auto& t : tiles_) t.Reset();
  rt_.Reset();
  icons_.DiscardDeviceResources();
  for (Row& r : rows_) {
    r.icon.Reset();
    r.iconState = IconCache::State::Loading;
  }
}

ID2D1SolidColorBrush* LauncherImpl::Brush(const D2D1_COLOR_F& c, float alphaMul) {
  const D2D1_COLOR_F col = D2D1::ColorF(c.r, c.g, c.b, c.a * alphaMul * opacity_);
  brush_->SetColor(&col);
  return brush_.Get();
}

// ---------------------------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------------------------

void LauncherImpl::BuildRows() {
  rows_.clear();
  headers_.clear();
  contentH_ = 0.f;
  const std::vector<Result>& res = engine_.Last();
  if (res.empty()) return;
  rows_.reserve(res.size());

  float y = kListPad;
  auto addHeader = [&](const std::wstring& text) {
    Header h;
    h.y = y;
    dwrite_->CreateTextLayout(text.c_str(), UINT32(text.size()), fmtHeader_.Get(), kWidth - 40.f, kHeaderH - 5.f,
                              h.layout.GetAddressOf());
    headers_.push_back(std::move(h));
    y += kHeaderH;
  };
  auto addRow = [&](size_t idx, bool top, bool headerAbove) {
    Row r;
    r.result = idx;
    r.top = top;
    r.headerAbove = headerAbove;
    r.tile = TileFor(res[idx].category);
    r.y = y;
    r.h = top ? kTopRowH : kRowH;
    y += r.h;
    rows_.push_back(std::move(r));
  };

  static const std::wstring kTopHit = L"Лучшее совпадение";
  addHeader(kTopHit);
  addRow(0, true, true);

  // Remaining results grouped by category, groups in order of first appearance, rank order inside a group.
  size_t groups[64];
  size_t groupCount = 0;
  for (size_t i = 1; i < res.size(); ++i) {
    bool found = false;
    for (size_t g = 0; g < groupCount && !found; ++g) found = res[groups[g]].category == res[i].category;
    if (!found && groupCount < std::size(groups)) groups[groupCount++] = i;
  }
  for (size_t g = 0; g < groupCount; ++g) {
    const std::wstring& cat = res[groups[g]].category;
    const bool header = !cat.empty();
    y += 4.f;
    if (header) addHeader(cat);
    bool first = true;
    for (size_t i = groups[g]; i < res.size(); ++i) {
      if (res[i].category != cat) continue;
      addRow(i, false, header && first);
      first = false;
    }
  }
  // Categories beyond the 64-group cap (never in practice) would be dropped; maxResults keeps us far below it.
  contentH_ = y + kListPad;
}

void LauncherImpl::LayoutRow(Row& r, const Result& res) {
  r.laidOut = true;
  const RowGeom g = GeomFor(r.top);
  dwrite_->CreateTextLayout(res.title.c_str(), UINT32(res.title.size()), (r.top ? fmtTopTitle_ : fmtTitle_).Get(),
                            g.maxTextW, g.titleLineH, r.title.ReleaseAndGetAddressOf());
  if (!res.subtitle.empty()) {
    ComPtr<IDWriteTextLayout> sub;
    if (SUCCEEDED(dwrite_->CreateTextLayout(res.subtitle.c_str(), UINT32(res.subtitle.size()), fmtSub_.Get(),
                                            g.maxTextW, kSubLineH, sub.GetAddressOf()))) {
      DWRITE_TEXT_METRICS tm{};
      sub->GetMetrics(&tm);
      const bool looksLikePath = res.subtitle.find_first_of(L"\\/") != std::wstring::npos;
      if (tm.widthIncludingTrailingWhitespace > g.maxTextW && looksLikePath) {
        const std::wstring shortened = MiddleEllipsis(res.subtitle, tm.widthIncludingTrailingWhitespace, g.maxTextW);
        ComPtr<IDWriteTextLayout> alt;
        if (SUCCEEDED(dwrite_->CreateTextLayout(shortened.c_str(), UINT32(shortened.size()), fmtSub_.Get(),
                                                g.maxTextW, kSubLineH, alt.GetAddressOf())))
          sub = alt;
      }
      sub->SetTrimming(&kTrimChar, ellipsisSub_.Get());  // safety net
      r.subtitle = sub;
    }
  }
  const wchar_t* glyph = nullptr;
  IDWriteTextFormat* fmt = nullptr;
  switch (res.iconKind) {
    case IconKind::Glyph:
      glyph = res.icon.empty() ? kTiles[r.tile].glyph : res.icon.c_str();
      fmt = (r.top ? fmtTopTileGlyph_ : fmtTileGlyph_).Get();
      break;
    case IconKind::FilePath:
    case IconKind::ShellItem: {
      const bool folder = r.tile == 7 || (!res.icon.empty() && (res.icon.back() == L'\\' || res.icon.back() == L'/'));
      glyph = res.iconKind == IconKind::ShellItem ? kGlyphApp : folder ? kGlyphFolder : kGlyphDocument;
      fmt = (r.top ? fmtTopLoadGlyph_ : fmtLoadGlyph_).Get();
      break;
    }
    case IconKind::None: break;
  }
  if (glyph && fmt) {
    const size_t len = std::wcslen(glyph);
    dwrite_->CreateTextLayout(glyph, UINT32(std::min<size_t>(len, 4)), fmt, g.iconSize, g.iconSize,
                              r.glyph.ReleaseAndGetAddressOf());
  }
}

void LauncherImpl::UpdateQueryLayout() {
  if (!dwrite_) return;
  const std::wstring& t = input_.Text();
  dwrite_->CreateTextLayout(t.c_str(), UINT32(t.size()), fmtQuery_.Get(), 100000.f, kSearchH,
                            queryLayout_.ReleaseAndGetAddressOf());
  UpdateTextScroll();
}

float LauncherImpl::CaretX() const {
  if (!queryLayout_) return 0.f;
  FLOAT x = 0, y = 0;
  DWRITE_HIT_TEST_METRICS m{};
  if (FAILED(queryLayout_->HitTestTextPosition(UINT32(input_.Caret()), FALSE, &x, &y, &m))) return 0.f;
  return x;
}

void LauncherImpl::UpdateTextScroll() {
  if (!queryLayout_) {
    textScrollX_ = 0.f;
    return;
  }
  DWRITE_TEXT_METRICS tm{};
  queryLayout_->GetMetrics(&tm);
  const float fieldW = kWidth - kFieldRight - kFieldLeft;
  const float cx = CaretX();
  float s = textScrollX_;
  if (cx - s > fieldW - 2.f) s = cx - fieldW + 2.f;
  if (cx - s < 0.f) s = cx;
  s = std::clamp(s, 0.f, std::max(0.f, tm.widthIncludingTrailingWhitespace - fieldW + 2.f));
  textScrollX_ = s;
}

void LauncherImpl::UpdateFooter() {
  int key = -1;
  const std::vector<Result>& res = engine_.Last();
  if (sel_ >= 0 && size_t(sel_) < rows_.size() && rows_[size_t(sel_)].result < res.size()) {
    const Result& r = res[rows_[size_t(sel_)].result];
    key = int(r.actions) | (IsCalcCategory(r.category) ? 0x100 : 0);
  }
  if (key == footerKey_) return;
  footerKey_ = key;
  footer_.Reset();
  if (key < 0 || !dwrite_) return;

  std::wstring s;
  DWRITE_TEXT_RANGE bold[4];
  size_t boldCount = 0;
  auto add = [&](const wchar_t* keys, const wchar_t* label) {
    if (!s.empty()) s += L"\u2003\u2003";
    bold[boldCount++] = DWRITE_TEXT_RANGE{UINT32(s.size()), UINT32(std::wcslen(keys))};
    s += keys;
    s += L"\u2002";
    s += label;
  };
  if (key & 0x100) {
    add(L"\u21B5", L"Копировать");
  } else {
    add(L"\u21B5", L"Открыть");
    if (key & kActReveal) add(L"Ctrl+\u21B5", L"Показать в папке");
    if (key & kActAdmin) add(L"Ctrl+Shift+\u21B5", L"От имени администратора");
    if (key & kActCopy) add(L"Ctrl+C", L"Копировать");
  }
  if (FAILED(dwrite_->CreateTextLayout(s.c_str(), UINT32(s.size()), fmtFooter_.Get(), kWidth - 40.f, kFooterH,
                                       footer_.ReleaseAndGetAddressOf())))
    return;
  for (size_t i = 0; i < boldCount; ++i) footer_->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, bold[i]);
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------------------------

void LauncherImpl::Render() {
  if (!hwnd_ || !CreateDeviceResources()) return;
  if (rt_->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED) return;
  rt_->BeginDraw();
  Draw();
  const HRESULT hr = rt_->EndDraw();
  if (hr == kRecreateTarget) {
    DiscardDeviceResources();
    if (visible_) InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void LauncherImpl::Draw() {
  const D2D1_MATRIX_3X2_F identity = D2D1::Matrix3x2F::Identity();
  rt_->SetTransform(&identity);
  rt_->Clear(D2D1::ColorF(0, 0, 0, 0));
  const D2D1_SIZE_F size = rt_->GetSize();

  // Tint over the backdrop (or the solid background). Never faded: with "none" a transparent frame would show
  // the desktop through the window.
  opacity_ = 1.f;
  const D2D1_RECT_F all = Rect(0, 0, size.width, size.height);
  rt_->FillRectangle(&all, Brush(pal_.background));

  const float e = 1.f - (1.f - showT_) * (1.f - showT_) * (1.f - showT_);
  opacity_ = e;
  if (e <= 0.f) {
    opacity_ = 1.f;
    return;
  }
  D2D1::Matrix3x2F base = D2D1::Matrix3x2F::Identity();
  if (e < 1.f) {
    const float s = 0.985f + 0.015f * e;
    base = D2D1::Matrix3x2F::Scale(D2D1::SizeF(s, s), D2D1::Point2F(kWidth * 0.5f, 0.f));
  }
  rt_->SetTransform(&base);

  DrawSearch();
  if (!rows_.empty()) {
    const D2D1_RECT_F sep = Rect(0, kSearchH, kWidth, kSearchH + OnePx());
    rt_->FillRectangle(&sep, Brush(pal_.separator));
    DrawList();
    DrawFooter(kSearchH + 1.f + ViewportH() + 1.f);
  }
  rt_->SetTransform(&identity);
  opacity_ = 1.f;
}

void LauncherImpl::DrawSearch() {
  {
    // Claude's spark: rays of alternating length radiating from the centre.
    const float cx = 32.f, cy = kSearchH * 0.5f;
    constexpr int kRays = 12;
    for (int i = 0; i < kRays; ++i) {
      const float a = float(i) * 6.2831853f / float(kRays) + 0.13f;
      const float r0 = 2.6f, r1 = (i % 2) ? 7.6f : 10.4f;
      const D2D1_POINT_2F p0 = D2D1::Point2F(cx + r0 * std::cos(a), cy + r0 * std::sin(a));
      const D2D1_POINT_2F p1 = D2D1::Point2F(cx + r1 * std::cos(a), cy + r1 * std::sin(a));
      rt_->DrawLine(p0, p1, Brush(pal_.accent), 2.3f, roundCap_.Get());
    }
  }

  const D2D1_RECT_F clip = Rect(kFieldLeft - 2.f, 0.f, kWidth - kFieldRight + 2.f, kSearchH);
  rt_->PushAxisAlignedClip(&clip, D2D1_ANTIALIAS_MODE_ALIASED);
  const D2D1_POINT_2F origin = D2D1::Point2F(kFieldLeft - textScrollX_, 0.f);
  const float mid = kSearchH * 0.5f;
  if (input_.Text().empty()) {
    if (placeholder_) rt_->DrawTextLayout(D2D1::Point2F(kFieldLeft, 0.f), placeholder_.Get(), Brush(pal_.text2));
  } else if (queryLayout_) {
    if (input_.HasSelection()) {
      DWRITE_HIT_TEST_METRICS hm[8];
      UINT32 count = 0;
      if (SUCCEEDED(queryLayout_->HitTestTextRange(UINT32(input_.SelStart()),
                                                   UINT32(input_.SelEnd() - input_.SelStart()), origin.x, origin.y,
                                                   hm, UINT32(std::size(hm)), &count))) {
        for (UINT32 i = 0; i < count && i < std::size(hm); ++i) {
          const D2D1_RECT_F r = Rect(hm[i].left, mid - 15.f, hm[i].left + hm[i].width, mid + 15.f);
          rt_->FillRectangle(&r, Brush(pal_.textSelection));
        }
      }
    }
    rt_->DrawTextLayout(origin, queryLayout_.Get(), Brush(pal_.text));
  }
  if (focused_ && caretOn_ && !input_.HasSelection()) {
    const float scale = float(dpi_) / 96.f;
    const float x = std::floor((origin.x + CaretX()) * scale + 0.5f) / scale;
    const float w = std::max(1.f, std::round(1.5f * scale)) / scale;
    const D2D1_RECT_F r = Rect(x, mid - 13.f, x + w, mid + 13.f);
    rt_->FillRectangle(&r, Brush(pal_.accent));
  }
  rt_->PopAxisAlignedClip();
}

void LauncherImpl::DrawList() {
  const float listTop = kSearchH + 1.f;
  const float view = ViewportH();
  D2D1::Matrix3x2F base;
  rt_->GetTransform(&base);
  const D2D1_RECT_F clip = Rect(0.f, listTop, kWidth, listTop + view);
  rt_->PushAxisAlignedClip(&clip, D2D1_ANTIALIAS_MODE_ALIASED);
  const D2D1::Matrix3x2F shifted = D2D1::Matrix3x2F::Translation(0.f, listTop - scrollY_) * base;
  rt_->SetTransform(&shifted);

  const float top = scrollY_, bottom = scrollY_ + view;
  for (const Header& h : headers_) {
    if (h.y + kHeaderH < top || h.y > bottom || !h.layout) continue;
    rt_->DrawTextLayout(D2D1::Point2F(20.f, h.y), h.layout.Get(), Brush(pal_.header));
  }
  if (hover_ >= 0 && hover_ != sel_ && size_t(hover_) < rows_.size()) {
    const Row& r = rows_[size_t(hover_)];
    const D2D1_ROUNDED_RECT rr{Rect(kRowInset, r.y, kWidth - kRowInset, r.y + r.h), kRowRadius, kRowRadius};
    rt_->FillRoundedRectangle(&rr, Brush(pal_.hover));
  }
  if (sel_ >= 0) {
    const D2D1_ROUNDED_RECT rr{Rect(kRowInset, selVisY_, kWidth - kRowInset, selVisY_ + selVisH_), kRowRadius,
                               kRowRadius};
    rt_->FillRoundedRectangle(&rr, Brush(pal_.accent));
  }
  for (size_t i = 0; i < rows_.size(); ++i) {
    Row& r = rows_[i];
    if (r.y + r.h < top || r.y > bottom) continue;
    DrawRow(r, int(i) == sel_);
  }
  rt_->SetTransform(&base);
  rt_->PopAxisAlignedClip();

  // Overlay scrollbar while scrolling.
  const float alpha = ScrollbarAlpha(NowMs());
  if (alpha > 0.f) {
    const float maxScroll = MaxScroll();
    const float thumbH = std::max(24.f, view * view / contentH_);
    const float ty = listTop + 2.f + (view - 4.f - thumbH) * (maxScroll > 0.f ? scrollY_ / maxScroll : 0.f);
    const D2D1_ROUNDED_RECT rr{Rect(kWidth - 7.f, ty, kWidth - 3.f, ty + thumbH), 2.f, 2.f};
    rt_->FillRoundedRectangle(&rr, Brush(pal_.scrollbar, alpha));
  }
}

void LauncherImpl::DrawRow(Row& r, bool selected) {
  const std::vector<Result>& results = engine_.Last();
  if (r.result >= results.size()) return;
  const Result& res = results[r.result];
  if (!r.laidOut) LayoutRow(r, res);
  const RowGeom g = GeomFor(r.top);

  DrawIcon(r, res, g.iconX, r.y + (r.h - g.iconSize) * 0.5f, g.iconSize, selected);

  const float block = r.subtitle ? g.titleLineH + kSubLineH : g.titleLineH;
  const float ty = r.y + (r.h - block) * 0.5f;
  if (r.title)
    rt_->DrawTextLayout(D2D1::Point2F(g.textX, ty), r.title.Get(), selected ? Brush(pal_.onAccent) : Brush(pal_.text));
  if (r.subtitle)
    rt_->DrawTextLayout(D2D1::Point2F(g.textX, ty + g.titleLineH), r.subtitle.Get(),
                        selected ? Brush(pal_.onAccent, 0.75f) : Brush(pal_.text2));
  if (selected && hint_)
    rt_->DrawTextLayout(D2D1::Point2F(kWidth - kRowInset - kHintW, r.y + (r.h - 20.f) * 0.5f), hint_.Get(),
                        Brush(pal_.onAccent, 0.7f));
}

void LauncherImpl::DrawIcon(Row& r, const Result& res, float x, float y, float size, bool selected) {
  if (res.iconKind == IconKind::None) return;
  if (res.iconKind == IconKind::Glyph) {
    ComPtr<ID2D1LinearGradientBrush>& grad = tiles_[r.tile];
    if (!grad) {
      const TileColors& c = kTiles[r.tile];
      const D2D1_GRADIENT_STOP stops[2] = {{0.f, Rgba(c.r1, c.g1, c.b1)}, {1.f, Rgba(c.r2, c.g2, c.b2)}};
      ComPtr<ID2D1GradientStopCollection> coll;
      if (SUCCEEDED(rt_->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                                                      coll.GetAddressOf()))) {
        const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp =
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 1));
        rt_->CreateLinearGradientBrush(&lp, nullptr, coll.Get(), grad.GetAddressOf());
      }
    }
    const float radius = size * 0.225f;
    const D2D1_ROUNDED_RECT rr{Rect(x, y, x + size, y + size), radius, radius};
    if (grad) {
      grad->SetStartPoint(D2D1::Point2F(x, y));
      grad->SetEndPoint(D2D1::Point2F(x, y + size));
      grad->SetOpacity(opacity_);
      rt_->FillRoundedRectangle(&rr, grad.Get());
    }
    const float half = OnePx() * 0.5f;
    const D2D1_ROUNDED_RECT stroke{Rect(x + half, y + half, x + size - half, y + size - half), radius, radius};
    rt_->DrawRoundedRectangle(&stroke, Brush(pal_.tileStroke), OnePx(), nullptr);
    if (r.glyph) rt_->DrawTextLayout(D2D1::Point2F(x, y), r.glyph.Get(), Brush(Rgba(255, 255, 255)));
    return;
  }
  // Shell icon: cached bitmap, or a neutral glyph while loading / if the shell has none.
  if (!r.icon && r.iconState == IconCache::State::Loading)
    r.iconState = icons_.Get(rt_.Get(), res.iconKind, res.icon, Px(size), &r.icon);
  if (r.icon) {
    const D2D1_RECT_F rc = Rect(x, y, x + size, y + size);
    rt_->DrawBitmap(r.icon.Get(), &rc, opacity_, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
  } else if (r.glyph) {
    rt_->DrawTextLayout(D2D1::Point2F(x, y), r.glyph.Get(),
                        selected ? Brush(pal_.onAccent, 0.8f) : Brush(pal_.text2));
  }
}

void LauncherImpl::DrawFooter(float y) {
  const D2D1_RECT_F sep = Rect(0, y - 1.f, kWidth, y - 1.f + OnePx());
  rt_->FillRectangle(&sep, Brush(pal_.separator));
  if (footer_) rt_->DrawTextLayout(D2D1::Point2F(20.f, y), footer_.Get(), Brush(pal_.text2));
}

}  // namespace cs::ui
