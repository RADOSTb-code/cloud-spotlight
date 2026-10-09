#include "ui/LauncherWindow.h"

#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "core/Config.h"
#include "core/SearchEngine.h"
#include "ui/LauncherImpl.h"

namespace cs {

// ---------------------------------------------------------------------------------------------------------------
// Public facade
// ---------------------------------------------------------------------------------------------------------------

LauncherWindow::LauncherWindow(SearchEngine& engine, IHost& host)
    : impl_(std::make_unique<ui::LauncherImpl>(engine, host)) {}
LauncherWindow::~LauncherWindow() = default;
bool LauncherWindow::Create(HINSTANCE inst, const Config& cfg) { return impl_->Create(inst, cfg); }
void LauncherWindow::Toggle() { impl_->Toggle(); }
void LauncherWindow::Show() { impl_->Show(); }
void LauncherWindow::Hide() { impl_->Hide(); }
bool LauncherWindow::IsVisible() const { return impl_->IsVisible(); }
void LauncherWindow::Refresh() { impl_->Refresh(); }
void LauncherWindow::ApplyConfig(const Config& cfg) { impl_->ApplyConfig(cfg); }
void LauncherWindow::OnSystemThemeChanged() { impl_->OnSystemThemeChanged(); }
HWND LauncherWindow::Hwnd() const { return impl_->Hwnd(); }

namespace ui {

namespace {

constexpr wchar_t kClassName[] = L"CloudSpotlightLauncher";
constexpr UINT_PTR kTimerAnim = 1;
constexpr UINT_PTR kTimerCaret = 2;
constexpr UINT kMsgIcons = WM_APP + 0x31;
constexpr double kShowMs = 140.0;
constexpr double kSelMs = 90.0;
constexpr double kCaretIdleStopMs = 15000.0;  // stop blinking (and waking up) after this much inactivity
constexpr double kScrollbarHoldMs = 800.0;
constexpr double kScrollbarFadeMs = 250.0;

bool KeyDown(int vk) { return GetKeyState(vk) < 0; }

UINT MonitorDpi(HMONITOR mon, UINT fallback) {
  using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
  static const Fn fn = [] {
    HMODULE m = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return ProcAddress<Fn>(m, "GetDpiForMonitor");
  }();
  UINT x = 0, y = 0;
  if (fn && SUCCEEDED(fn(mon, 0 /*MDT_EFFECTIVE_DPI*/, &x, &y)) && x) return x;
  return fallback;
}

float EaseOutCubic(float t) {
  t = std::clamp(t, 0.f, 1.f);
  const float u = 1.f - t;
  return 1.f - u * u * u;
}

struct PositioningScope {
  explicit PositioningScope(int& c) : c_(c) { ++c_; }
  ~PositioningScope() { --c_; }
  int& c_;
};

}  // namespace

double NowMs() {
  static const double freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return double(f.QuadPart) / 1000.0;
  }();
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return double(c.QuadPart) / freq;
}

LauncherImpl::LauncherImpl(SearchEngine& engine, IHost& host) : engine_(engine), host_(host) {}

LauncherImpl::~LauncherImpl() {
  destroying_ = true;
  icons_.Stop();
  if (hwnd_) DestroyWindow(hwnd_);
}

// ---------------------------------------------------------------------------------------------------------------
// Creation / config
// ---------------------------------------------------------------------------------------------------------------

bool LauncherImpl::Create(HINSTANCE inst, const Config& cfg) {
  if (hwnd_) return true;
  theme_ = cfg.theme;
  backdrop_ = cfg.backdrop;
  visibleRows_ = std::clamp(cfg.visibleRows, 3, 20);
  if (!CreateDeviceIndependentResources()) return false;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = &LauncherImpl::WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClassName;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

  const POINT origin{0, 0};
  HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (GetMonitorInfoW(mon, &mi)) work_ = mi.rcWork;
  dpi_ = MonitorDpi(mon, 96);

  // WS_THICKFRAME gives us the DWM shadow, border and rounded corners; WM_NCCALCSIZE removes the visible frame.
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassName, L"Cloud Spotlight", WS_POPUP | WS_THICKFRAME,
                          work_.left, work_.top, Px(kWidth), Px(kSearchH), nullptr, nullptr, inst, this);
  if (!hwnd_) return false;
  if (UINT d = GetDpiForWindow(hwnd_)) dpi_ = d;

  ApplyTheme();
  {
    PositioningScope scope(positioning_);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  icons_.Start(hwnd_, kMsgIcons);
  UpdateQueryLayout();
  CreateDeviceResources();  // the expensive part (D3D device) happens now, not on first Show
  PlaceWindow();
  return true;
}

void LauncherImpl::ApplyTheme() {
  if (!hwnd_) return;
  dark_ = ResolveDark(theme_);
  backdropKind_ = ApplyBackdrop(hwnd_, backdrop_, dark_);
  pal_ = MakePalette(dark_, backdropKind_);
  Invalidate();
}

void LauncherImpl::ApplyConfig(const Config& cfg) {
  theme_ = cfg.theme;
  backdrop_ = cfg.backdrop;
  visibleRows_ = std::clamp(cfg.visibleRows, 3, 20);
  engine_.SetMaxResults(cfg.maxResults);
  ApplyTheme();
  scrollTarget_ = std::clamp(scrollTarget_, 0.f, MaxScroll());
  scrollY_ = scrollTarget_;
  if (sel_ >= 0) EnsureVisible(sel_, false);
  PlaceWindow();
  Invalidate();
}

void LauncherImpl::OnSystemThemeChanged() { ApplyTheme(); }

void LauncherImpl::SetDpi(UINT dpi) {
  if (!dpi || dpi == dpi_) return;
  dpi_ = dpi;
  if (rt_) rt_->SetDpi(float(dpi), float(dpi));
  for (Row& r : rows_) {  // icon bitmaps are requested in physical pixels
    r.icon.Reset();
    r.iconState = IconCache::State::Loading;
  }
}

float LauncherImpl::MaxViewportH() const {
  return float(visibleRows_) * kRowH + (kTopRowH - kRowH) + 2.f * kHeaderH + 2.f * kListPad;
}
float LauncherImpl::ViewportH() const { return std::min(contentH_, MaxViewportH()); }
float LauncherImpl::MaxScroll() const { return std::max(0.f, contentH_ - ViewportH()); }
float LauncherImpl::WindowHeightDip() const {
  return rows_.empty() ? kSearchH : kSearchH + 1.f + ViewportH() + 1.f + kFooterH;
}
float LauncherImpl::MaxWindowHeightDip() const { return kSearchH + 1.f + MaxViewportH() + 1.f + kFooterH; }

void LauncherImpl::PlaceWindow() {
  if (!hwnd_) return;
  const int w = Px(kWidth), h = Px(WindowHeightDip()), hMax = Px(MaxWindowHeightDip());
  const int workW = work_.right - work_.left, workH = work_.bottom - work_.top;
  const int x = work_.left + (workW - w) / 2;
  int y = work_.top + int(float(workH) * 0.22f);
  if (y + hMax > work_.bottom) y = std::max<int>(work_.top, work_.bottom - hMax);
  RECT cur{};
  GetWindowRect(hwnd_, &cur);
  if (cur.left == x && cur.top == y && cur.right - cur.left == w && cur.bottom - cur.top == h) return;
  PositioningScope scope(positioning_);
  SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

// ---------------------------------------------------------------------------------------------------------------
// Show / hide
// ---------------------------------------------------------------------------------------------------------------

void LauncherImpl::Toggle() {
  if (visible_) Hide();
  else Show();
}

void LauncherImpl::ForceForeground() {
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
  if (SetForegroundWindow(hwnd_) && GetForegroundWindow() == hwnd_) {
    SetFocus(hwnd_);
    return;
  }
  // Foreground lock: borrow the foreground thread's input state.
  HWND fg = GetForegroundWindow();
  const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
  const DWORD me = GetCurrentThreadId();
  const bool attached = fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE);
  BringWindowToTop(hwnd_);
  SetForegroundWindow(hwnd_);
  SetActiveWindow(hwnd_);
  SetFocus(hwnd_);
  if (attached) AttachThreadInput(me, fgThread, FALSE);
  if (GetForegroundWindow() == hwnd_) return;
  // Last resort: injecting input makes us "the process that received the last input event", which lifts the
  // foreground lock. Use an unassigned virtual key so no application reacts to it.
  INPUT in[2]{};
  in[0].type = in[1].type = INPUT_KEYBOARD;
  in[0].ki.wVk = in[1].ki.wVk = 0xE8;  // VK unassigned
  in[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(2, in, sizeof(INPUT));
  SetForegroundWindow(hwnd_);
  SetFocus(hwnd_);
}

void LauncherImpl::Show() {
  if (!hwnd_ || destroying_) return;
  if (visible_) {
    ForceForeground();
    return;
  }
  POINT pt{};
  GetCursorPos(&pt);
  HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (GetMonitorInfoW(mon, &mi)) work_ = mi.rcWork;
  SetDpi(MonitorDpi(mon, dpi_));

  // Like Spotlight: keep the previous query, selected, so typing replaces it.
  input_.SelectAll();
  pendingHigh_ = 0;
  hover_ = pressRow_ = -1;
  lastMouse_ = POINT{-100000, -100000};
  UpdateQueryLayout();
  if (!executing_) RunQuery(nullptr);

  showStart_ = NowMs();
  showT_ = 0.f;
  visible_ = true;
  PlaceWindow();
  ShowWindow(hwnd_, SW_SHOW);
  ForceForeground();
  if (UINT d = GetDpiForWindow(hwnd_); d && d != dpi_) {  // moved to a monitor with another DPI
    SetDpi(d);
    PlaceWindow();
  }
  ResetCaretBlink();
  StartAnim();
  Render();
}

void LauncherImpl::Hide() {
  if (!visible_ || !hwnd_) return;
  visible_ = false;
  KillTimer(hwnd_, kTimerAnim);
  KillTimer(hwnd_, kTimerCaret);
  animTimer_ = caretTimer_ = false;
  selAnimating_ = false;
  scrollY_ = scrollTarget_;
  dragging_ = false;
  if (GetCapture() == hwnd_) ReleaseCapture();
  hover_ = pressRow_ = -1;
  // Leave an empty frame behind so the next Show never flashes stale content.
  showT_ = 0.f;
  Render();
  ShowWindow(hwnd_, SW_HIDE);
}

// ---------------------------------------------------------------------------------------------------------------
// Query / selection
// ---------------------------------------------------------------------------------------------------------------

void LauncherImpl::RunQuery(const std::wstring* keepKey) {
  const std::vector<Result>& res = engine_.Query(input_.Text());
  icons_.CancelPending();
  BuildRows();
  int sel = rows_.empty() ? -1 : 0;
  if (keepKey && !keepKey->empty()) {
    for (size_t i = 0; i < rows_.size(); ++i)
      if (rows_[i].result < res.size() && res[rows_[i].result].key == *keepKey) {
        sel = int(i);
        break;
      }
  }
  sel_ = sel;
  hover_ = pressRow_ = -1;
  selAnimating_ = false;
  if (sel_ >= 0) {
    selVisY_ = rows_[size_t(sel_)].y;
    selVisH_ = rows_[size_t(sel_)].h;
  }
  if (keepKey) {
    scrollTarget_ = scrollY_ = std::clamp(scrollTarget_, 0.f, MaxScroll());
    if (sel_ >= 0) EnsureVisible(sel_, false);
  } else {
    scrollTarget_ = scrollY_ = 0.f;
  }
  UpdateFooter();
  PlaceWindow();
  Invalidate();
}

void LauncherImpl::Refresh() {
  if (!visible_) return;  // Show() re-runs the query anyway
  if (executing_) {
    refreshPending_ = true;
    return;
  }
  std::wstring key;
  const auto& res = engine_.Last();
  if (sel_ >= 0 && size_t(sel_) < rows_.size() && rows_[size_t(sel_)].result < res.size())
    key = res[rows_[size_t(sel_)].result].key;
  RunQuery(&key);
}

void LauncherImpl::TextChanged() {
  if (executing_) return;
  UpdateQueryLayout();  // before RunQuery: its resize paints synchronously
  RunQuery(nullptr);
  ResetCaretBlink();
  Invalidate();
}

void LauncherImpl::CaretMoved() {
  UpdateTextScroll();
  UpdateIme();
  ResetCaretBlink();
  Invalidate();
}

void LauncherImpl::Select(int row, bool animate) {
  if (row < 0 || size_t(row) >= rows_.size()) return;
  if (row != sel_) {
    if (animate && sel_ >= 0 && visible_) {
      selFromY_ = selVisY_;
      selFromH_ = selVisH_;
      selAnimStart_ = NowMs();
      selAnimating_ = true;
      StartAnim();
    } else {
      selAnimating_ = false;
      selVisY_ = rows_[size_t(row)].y;
      selVisH_ = rows_[size_t(row)].h;
    }
    sel_ = row;
    UpdateFooter();
  }
  EnsureVisible(row, visible_);
  Invalidate();
}

void LauncherImpl::MoveSelection(int delta, bool wrap) {
  const int n = int(rows_.size());
  if (n == 0) return;
  int next = (sel_ < 0 ? 0 : sel_) + delta;
  bool wrapped = false;
  if (wrap) {
    if (next < 0 || next >= n) wrapped = true;
    next = ((next % n) + n) % n;
  } else {
    next = std::clamp(next, 0, n - 1);
  }
  Select(next, !wrapped);
}

void LauncherImpl::EnsureVisible(int row, bool smooth) {
  if (row < 0 || size_t(row) >= rows_.size()) return;
  const Row& r = rows_[size_t(row)];
  const float view = ViewportH();
  float top = r.y - (r.headerAbove ? kHeaderH : 0.f);
  float bottom = r.y + r.h;
  if (row == 0) top = 0.f;
  if (size_t(row) + 1 == rows_.size()) bottom = contentH_;
  float t = scrollTarget_;
  if (top < t) t = top;
  else if (bottom > t + view) t = bottom - view;
  t = std::clamp(t, 0.f, MaxScroll());
  if (t == scrollTarget_) return;
  scrollTarget_ = t;
  if (!smooth) {
    scrollY_ = t;
  } else {
    scrollActivity_ = NowMs();
    StartAnim();
  }
}

int LauncherImpl::VisibleRowByOrdinal(int n) const {
  const float top = scrollY_, bottom = scrollY_ + ViewportH();
  int k = 0;
  for (size_t i = 0; i < rows_.size(); ++i) {
    const Row& r = rows_[i];
    const float mid = r.y + r.h * 0.5f;
    if (mid < top || mid > bottom) continue;
    if (k++ == n) return int(i);
  }
  return -1;
}

void LauncherImpl::Activate(int row, Action a) {
  if (executing_ || row < 0 || size_t(row) >= rows_.size()) return;
  const std::vector<Result>& res = engine_.Last();
  const size_t idx = rows_[size_t(row)].result;
  if (idx >= res.size()) return;
  const uint8_t need = a == Action::Open ? kActOpen : a == Action::Reveal ? kActReveal
                                                   : a == Action::RunAsAdmin ? kActAdmin : kActCopy;
  if (a != Action::Open && !(res[idx].actions & need)) return;

  executing_ = true;
  ExecResult er = engine_.Execute(idx, a);  // may pump messages (ShellExecute); guarded by executing_
  executing_ = false;

  if (!er.toast.empty()) host_.Notify(L"Cloud Spotlight", er.toast);
  if (er.hide) {
    refreshPending_ = false;
    Hide();
  } else if (!er.replaceQuery.empty()) {
    refreshPending_ = false;
    input_.SetText(er.replaceQuery, false);
    TextChanged();
    CaretMoved();
  } else if (refreshPending_) {
    refreshPending_ = false;
    Refresh();
  }
}

void LauncherImpl::CopyOrResultCopy(bool forceResult) {
  if (!forceResult && input_.HasSelection()) {
    host_.CopyToClipboard(input_.SelectedText());
    return;
  }
  Activate(sel_, Action::Copy);
}

void LauncherImpl::Paste() {
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(hwnd_)) return;
  std::wstring text;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
      const size_t maxChars = GlobalSize(h) / sizeof(wchar_t);
      text.assign(p, wcsnlen(p, maxChars));
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  // Trim surrounding whitespace/newlines of pasted text; inner newlines become spaces in Insert().
  const size_t b = text.find_first_not_of(L" \t\r\n");
  const size_t e = text.find_last_not_of(L" \t\r\n");
  text = b == std::wstring::npos ? std::wstring() : text.substr(b, e - b + 1);
  if (input_.Insert(text)) TextChanged();
  CaretMoved();
}

// ---------------------------------------------------------------------------------------------------------------
// Timers / animation
// ---------------------------------------------------------------------------------------------------------------

void LauncherImpl::Invalidate() {
  if (hwnd_ && visible_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void LauncherImpl::ResetCaretBlink() {
  caretOn_ = true;
  lastInput_ = NowMs();
  const UINT blink = GetCaretBlinkTime();
  if (visible_ && focused_ && blink != INFINITE && blink > 0) {
    SetTimer(hwnd_, kTimerCaret, blink, nullptr);  // re-arming resets the phase
    caretTimer_ = true;
  } else if (caretTimer_) {
    KillTimer(hwnd_, kTimerCaret);
    caretTimer_ = false;
  }
}

void LauncherImpl::StartAnim() {
  if (!visible_ || animTimer_) return;
  lastTick_ = NowMs();
  SetTimer(hwnd_, kTimerAnim, USER_TIMER_MINIMUM, nullptr);
  animTimer_ = true;
}

float LauncherImpl::ScrollbarAlpha(double now) const {
  if (MaxScroll() <= 0.f) return 0.f;
  const double since = now - scrollActivity_;
  if (since < kScrollbarHoldMs) return 1.f;
  if (since < kScrollbarHoldMs + kScrollbarFadeMs) return float(1.0 - (since - kScrollbarHoldMs) / kScrollbarFadeMs);
  return 0.f;
}

void LauncherImpl::Tick() {
  const double now = NowMs();
  const double dt = std::clamp(now - lastTick_, 0.0, 100.0);
  lastTick_ = now;
  bool active = false;

  if (showT_ < 1.f) {
    showT_ = float(std::min(1.0, (now - showStart_) / kShowMs));
    active |= showT_ < 1.f;
  }
  if (selAnimating_ && sel_ >= 0) {
    const Row& r = rows_[size_t(sel_)];
    const float t = float((now - selAnimStart_) / kSelMs);
    if (t >= 1.f) {
      selAnimating_ = false;
      selVisY_ = r.y;
      selVisH_ = r.h;
    } else {
      const float e = EaseOutCubic(t);
      selVisY_ = selFromY_ + (r.y - selFromY_) * e;
      selVisH_ = selFromH_ + (r.h - selFromH_) * e;
      active = true;
    }
  } else {
    selAnimating_ = false;
  }
  if (scrollY_ != scrollTarget_) {
    const float k = float(1.0 - std::exp(-dt / 45.0));
    scrollY_ += (scrollTarget_ - scrollY_) * k;
    if (std::fabs(scrollTarget_ - scrollY_) < 0.3f) scrollY_ = scrollTarget_;
    else active = true;
    scrollActivity_ = now;
  }
  if (ScrollbarAlpha(now) > 0.f) active = true;

  if (!active && animTimer_) {
    KillTimer(hwnd_, kTimerAnim);
    animTimer_ = false;
  }
  Invalidate();
}

void LauncherImpl::UpdateIme() {
  if (!hwnd_ || !focused_) return;
  const float x = kFieldLeft - textScrollX_ + CaretX();
  const int px = Px(std::clamp(x, kFieldLeft, kWidth - kFieldRight));
  const int top = Px(kSearchH * 0.5f - 13.f), bottom = Px(kSearchH * 0.5f + 13.f);
  SetCaretPos(px, top);
  if (HIMC imc = ImmGetContext(hwnd_)) {
    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos = POINT{px, top};
    ImmSetCompositionWindow(imc, &cf);
    CANDIDATEFORM cand{};
    cand.dwIndex = 0;
    cand.dwStyle = CFS_EXCLUDE;
    cand.ptCurrentPos = POINT{px, bottom};
    cand.rcArea = RECT{px, top, px + 2, bottom};
    ImmSetCandidateWindow(imc, &cand);
    ImmReleaseContext(hwnd_, imc);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------------------------

bool LauncherImpl::OnKeyDown(WPARAM vk) {
  if (executing_) return true;
  const bool alt = KeyDown(VK_MENU);
  const bool ctrl = KeyDown(VK_CONTROL) && !alt;  // Ctrl+Alt is AltGr: leave it to WM_CHAR
  const bool shift = KeyDown(VK_SHIFT);
  switch (vk) {
    case VK_ESCAPE:
      if (!input_.Text().empty()) {
        input_.SetText(L"", false);
        TextChanged();
        CaretMoved();
      } else {
        Hide();
      }
      return true;
    case VK_RETURN:
      Activate(sel_, ctrl && shift ? Action::RunAsAdmin : ctrl ? Action::Reveal : Action::Open);
      return true;
    case VK_UP: MoveSelection(-1, true); return true;
    case VK_DOWN: MoveSelection(1, true); return true;
    case VK_PRIOR: MoveSelection(-std::max(1, visibleRows_ - 1), false); return true;
    case VK_NEXT: MoveSelection(std::max(1, visibleRows_ - 1), false); return true;
    case VK_TAB:
      if (!shift && sel_ >= 0 && rows_[size_t(sel_)].result < engine_.Last().size()) {
        const std::wstring title = engine_.Last()[rows_[size_t(sel_)].result].title;
        if (title != input_.Text()) {
          input_.SetText(title, false);
          TextChanged();
        }
        CaretMoved();
      }
      return true;
    case VK_LEFT: input_.Left(ctrl, shift); CaretMoved(); return true;
    case VK_RIGHT: input_.Right(ctrl, shift); CaretMoved(); return true;
    case VK_HOME:
      if (ctrl) {
        Select(0, true);
      } else {
        input_.Home(shift);
        CaretMoved();
      }
      return true;
    case VK_END:
      if (ctrl) {
        Select(int(rows_.size()) - 1, true);
      } else {
        input_.End(shift);
        CaretMoved();
      }
      return true;
    case VK_BACK:
      if (input_.Backspace(ctrl)) TextChanged();
      CaretMoved();
      return true;
    case VK_DELETE:
      if (shift && !ctrl) {  // Shift+Del = cut
        if (input_.HasSelection()) {
          host_.CopyToClipboard(input_.SelectedText());
          input_.DeleteSelection();
          TextChanged();
        }
      } else if (input_.Delete(ctrl)) {
        TextChanged();
      }
      CaretMoved();
      return true;
    case VK_INSERT:
      if (ctrl && input_.HasSelection()) host_.CopyToClipboard(input_.SelectedText());
      else if (shift) Paste();
      return true;
    default: break;
  }
  if (ctrl) {
    switch (vk) {
      case 'A': input_.SelectAll(); CaretMoved(); return true;
      case 'C': CopyOrResultCopy(shift); return true;
      case 'X':
        if (input_.HasSelection()) {
          host_.CopyToClipboard(input_.SelectedText());
          input_.DeleteSelection();
          TextChanged();
          CaretMoved();
        }
        return true;
      case 'V': Paste(); return true;
      case 'Z':
        if (input_.Undo()) TextChanged();
        CaretMoved();
        return true;
      default:
        if (vk >= '1' && vk <= '9') {
          Activate(VisibleRowByOrdinal(int(vk - '1')), shift ? Action::Reveal : Action::Open);
          return true;
        }
        break;
    }
  }
  return false;
}

void LauncherImpl::OnChar(wchar_t c) {
  if (executing_) return;
  if (c < 0x20 || c == 0x7F) return;  // control chars (Ctrl+letters, Esc, Tab, Enter, Ctrl+Backspace)
  if (c >= 0xD800 && c <= 0xDBFF) {
    pendingHigh_ = c;
    return;
  }
  wchar_t buf[2] = {c, 0};
  size_t n = 1;
  if (c >= 0xDC00 && c <= 0xDFFF) {
    if (!pendingHigh_) return;
    buf[0] = pendingHigh_;
    buf[1] = c;
    n = 2;
  }
  pendingHigh_ = 0;
  if (input_.Insert(std::wstring_view(buf, n))) TextChanged();
  CaretMoved();
}

bool LauncherImpl::InField(float x, float y) const {
  return y >= 0 && y < kSearchH && x >= kFieldLeft - 8.f && x < kWidth - kFieldRight + 8.f;
}

int LauncherImpl::RowAt(float y) const {
  const float listTop = kSearchH + 1.f;
  if (rows_.empty() || y < listTop || y >= listTop + ViewportH()) return -1;
  const float cy = y - listTop + scrollY_;
  for (size_t i = 0; i < rows_.size(); ++i)
    if (cy >= rows_[i].y && cy < rows_[i].y + rows_[i].h) return int(i);
  return -1;
}

size_t LauncherImpl::TextPosAt(float x) const {
  if (!queryLayout_) return 0;
  BOOL trailing = FALSE, inside = FALSE;
  DWRITE_HIT_TEST_METRICS m{};
  if (FAILED(queryLayout_->HitTestPoint(x - kFieldLeft + textScrollX_, kSearchH * 0.5f, &trailing, &inside, &m)))
    return input_.Caret();
  return input_.Snap(size_t(m.textPosition) + (trailing ? size_t(m.length) : 0));
}

void LauncherImpl::OnMouseDown(float x, float y, bool dbl) {
  if (executing_) return;
  if (y < kSearchH) {
    const size_t pos = TextPosAt(x);
    if (dbl) {
      input_.SelectWordAt(pos);
    } else {
      input_.MoveTo(pos, KeyDown(VK_SHIFT));
      dragging_ = true;
      SetCapture(hwnd_);
    }
    CaretMoved();
    return;
  }
  if (dbl) return;  // the first click already activated the row
  pressRow_ = RowAt(y);
  if (pressRow_ >= 0) {
    Select(pressRow_, true);
    SetCapture(hwnd_);
  }
}

void LauncherImpl::OnMouseMove(float x, float y, POINT screen) {
  if (dragging_) {
    input_.MoveTo(TextPosAt(x), true);
    CaretMoved();
    return;
  }
  // Ignore the synthetic move Windows sends when the window appears under a still cursor.
  if (screen.x == lastMouse_.x && screen.y == lastMouse_.y) return;
  lastMouse_ = screen;
  if (!trackingMouse_) {
    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
    trackingMouse_ = TrackMouseEvent(&tme) != FALSE;
  }
  const int h = RowAt(y);
  if (h != hover_) {
    hover_ = h;
    Invalidate();
  }
}

void LauncherImpl::OnMouseUp(float /*x*/, float y) {
  const bool wasDragging = dragging_;
  dragging_ = false;
  const int pressed = pressRow_;
  pressRow_ = -1;
  if (GetCapture() == hwnd_) ReleaseCapture();
  if (wasDragging || pressed < 0) return;
  if (RowAt(y) == pressed) {
    const bool ctrl = KeyDown(VK_CONTROL), shift = KeyDown(VK_SHIFT);
    Activate(pressed, ctrl && shift ? Action::RunAsAdmin : ctrl ? Action::Reveal : Action::Open);
  }
}

void LauncherImpl::OnWheel(int delta) {
  if (rows_.empty() || MaxScroll() <= 0.f) return;
  scrollTarget_ = std::clamp(scrollTarget_ - float(delta) / float(WHEEL_DELTA) * 2.f * kRowH, 0.f, MaxScroll());
  scrollActivity_ = NowMs();
  StartAnim();
  Invalidate();
}

// ---------------------------------------------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------------------------------------------

LRESULT CALLBACK LauncherImpl::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  LauncherImpl* self = nullptr;
  if (msg == WM_NCCREATE) {
    self = static_cast<LauncherImpl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<LauncherImpl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
  if (msg == WM_NCDESTROY) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    self->hwnd_ = nullptr;
    self->visible_ = false;
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  return self->Handle(msg, wp, lp);
}

LRESULT LauncherImpl::Handle(UINT msg, WPARAM wp, LPARAM lp) {
  HWND hwnd = hwnd_;
  switch (msg) {
    case WM_NCCALCSIZE:
      return 0;  // client area = whole window: no visible frame, DWM still draws shadow/border/corners
    case WM_NCHITTEST:
      return HTCLIENT;  // not movable, not resizable
    case WM_NCACTIVATE:
      return DefWindowProcW(hwnd, msg, wp, -1);  // -1: don't repaint the (non-existent) non-client area
    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
      mmi->ptMinTrackSize = POINT{1, 1};
      return 0;
    }
    case WM_WINDOWPOSCHANGING:
      // Only we size/move this window (blocks Aero Snap / Win+Arrow on the WS_THICKFRAME popup).
      if (!positioning_) reinterpret_cast<WINDOWPOS*>(lp)->flags |= SWP_NOMOVE | SWP_NOSIZE;
      return 0;
    case WM_SYSCOMMAND:
      switch (wp & 0xFFF0) {
        case SC_KEYMENU: case SC_MOVE: case SC_SIZE: case SC_MAXIMIZE: case SC_MINIMIZE: case SC_RESTORE:
          return 0;
        default: break;
      }
      break;
    case WM_CLOSE:
      Hide();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      Render();
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_SIZE:
      if (rt_) {
        if (rt_->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp))) == kRecreateTarget) DiscardDeviceResources();
      }
      if (visible_) {
        Render();  // paint the new size right away (no stretched frame)
        ValidateRect(hwnd, nullptr);
      }
      return 0;
    case WM_DPICHANGED: {
      SetDpi(HIWORD(wp));
      if (sel_ >= 0) EnsureVisible(sel_, false);
      PlaceWindow();  // our own geometry, not the suggested rect (we are centered, not dragged)
      Invalidate();
      return 0;
    }
    case WM_ACTIVATE:
      if (LOWORD(wp) == WA_INACTIVE) {
        if (visible_ && !destroying_) Hide();
        return 0;
      }
      break;  // DefWindowProc gives us keyboard focus on activation (also when activated by a click)
    case WM_SETFOCUS:
      focused_ = true;
      CreateCaret(hwnd, nullptr, 1, Px(26.f));  // invisible system caret for IME / accessibility tools
      UpdateIme();
      ResetCaretBlink();
      Invalidate();
      return 0;
    case WM_KILLFOCUS:
      focused_ = false;
      DestroyCaret();
      if (caretTimer_) KillTimer(hwnd, kTimerCaret);
      caretTimer_ = false;
      Invalidate();
      return 0;
    case WM_TIMER:
      if (wp == kTimerAnim) {
        Tick();
      } else if (wp == kTimerCaret) {
        if (NowMs() - lastInput_ > kCaretIdleStopMs) {
          caretOn_ = true;
          KillTimer(hwnd, kTimerCaret);
          caretTimer_ = false;
        } else {
          caretOn_ = !caretOn_;
        }
        Invalidate();
      }
      return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      if (OnKeyDown(wp)) return 0;
      break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
      // A lone Alt (or F10) release would enter menu mode via DefWindowProc — e.g. the Alt of the Alt+Space
      // hotkey released right after we took focus — and eat the next keystroke. We have no menu: swallow it.
      if (wp == VK_MENU || wp == VK_F10) return 0;
      break;
    case WM_CHAR:
      OnChar(wchar_t(wp));
      return 0;
    case WM_SYSCHAR:
      return 0;  // no menu: swallow Alt+letter (and its beep)
    case WM_IME_STARTCOMPOSITION:
      UpdateIme();
      break;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        POINT pt{};
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        static HCURSOR ibeam = LoadCursorW(nullptr, IDC_IBEAM);
        static HCURSOR arrow = LoadCursorW(nullptr, IDC_ARROW);
        SetCursor(InField(Dip(pt.x), Dip(pt.y)) ? ibeam : arrow);
        return TRUE;
      }
      break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
      OnMouseDown(Dip(GET_X_LPARAM(lp)), Dip(GET_Y_LPARAM(lp)), msg == WM_LBUTTONDBLCLK);
      return 0;
    case WM_LBUTTONUP:
      OnMouseUp(Dip(GET_X_LPARAM(lp)), Dip(GET_Y_LPARAM(lp)));
      return 0;
    case WM_MOUSEMOVE: {
      POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ClientToScreen(hwnd, &screen);
      OnMouseMove(Dip(GET_X_LPARAM(lp)), Dip(GET_Y_LPARAM(lp)), screen);
      return 0;
    }
    case WM_MOUSELEAVE:
      trackingMouse_ = false;
      if (hover_ != -1) {
        hover_ = -1;
        Invalidate();
      }
      return 0;
    case WM_CAPTURECHANGED:
      dragging_ = false;
      return 0;
    case WM_MOUSEWHEEL:
      OnWheel(GET_WHEEL_DELTA_WPARAM(wp));
      return 0;
    case WM_SETTINGCHANGE:
      if (lp && std::wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) ApplyTheme();
      break;
    case WM_DWMCOLORIZATIONCOLORCHANGED:
      pal_ = MakePalette(dark_, backdropKind_);
      Invalidate();
      break;
    case kMsgIcons:
      if (icons_.OnLoaded()) Invalidate();
      return 0;
    default: break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace ui
}  // namespace cs
