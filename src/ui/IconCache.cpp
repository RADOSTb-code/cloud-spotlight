#include "ui/IconCache.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cwchar>

#include "core/Str.h"
#include "platform/Win.h"

namespace cs::ui {

using Microsoft::WRL::ComPtr;

namespace {

// Extensions whose icon is per file, not per type.
bool PerFileExtension(std::wstring_view ext) {
  static constexpr const wchar_t* kList[] = {L".exe", L".lnk", L".ico", L".url", L".appref-ms", L".cpl", L".msc",
                                             L".scr", L".cur",  L".ani", L".website", L".library-ms"};
  for (const wchar_t* e : kList)
    if (str::IEquals(ext, e)) return true;
  return false;
}

// Makes the pixels valid premultiplied BGRA: no alpha at all -> opaque; straight alpha -> premultiply.
void FixAlpha(std::vector<uint32_t>& px) {
  bool anyAlpha = false, straight = false;
  for (uint32_t p : px) {
    const uint32_t a = p >> 24;
    if (a) anyAlpha = true;
    if (((p >> 16) & 0xFF) > a || ((p >> 8) & 0xFF) > a || (p & 0xFF) > a) straight = true;
    if (anyAlpha && straight) break;
  }
  if (!anyAlpha) {
    for (uint32_t& p : px) p |= 0xFF000000u;
    return;
  }
  if (!straight) return;
  for (uint32_t& p : px) {
    const uint32_t a = p >> 24;
    const uint32_t r = ((p >> 16) & 0xFF) * a / 255, g = ((p >> 8) & 0xFF) * a / 255, b = (p & 0xFF) * a / 255;
    p = (a << 24) | (r << 16) | (g << 8) | b;
  }
}

// Area-averaging downscale of premultiplied pixels (shell may hand back 256 px for a 40 px request).
void Downscale(std::vector<uint32_t>& px, int& w, int& h, int target) {
  if (w <= 0 || h <= 0 || (w <= target * 5 / 4 && h <= target * 5 / 4)) return;
  const int dw = std::max(1, w >= h ? target : target * w / h);
  const int dh = std::max(1, h >= w ? target : target * h / w);
  std::vector<uint32_t> out(size_t(dw) * size_t(dh));
  for (int y = 0; y < dh; ++y) {
    const int y0 = y * h / dh, y1 = std::max(y0 + 1, (y + 1) * h / dh);
    for (int x = 0; x < dw; ++x) {
      const int x0 = x * w / dw, x1 = std::max(x0 + 1, (x + 1) * w / dw);
      uint32_t s[4] = {0, 0, 0, 0};
      for (int sy = y0; sy < y1; ++sy) {
        const uint32_t* row = px.data() + size_t(sy) * size_t(w);
        for (int sx = x0; sx < x1; ++sx) {
          const uint32_t p = row[sx];
          s[0] += p & 0xFF;
          s[1] += (p >> 8) & 0xFF;
          s[2] += (p >> 16) & 0xFF;
          s[3] += p >> 24;
        }
      }
      const uint32_t n = uint32_t((y1 - y0) * (x1 - x0));
      out[size_t(y) * size_t(dw) + size_t(x)] =
          (s[0] / n) | ((s[1] / n) << 8) | ((s[2] / n) << 16) | ((s[3] / n) << 24);
    }
  }
  px.swap(out);
  w = dw;
  h = dh;
}

bool BitmapPixels(HBITMAP hbm, std::vector<uint32_t>& px, int& w, int& h, bool* hadAlphaChannel) {
  BITMAP bm{};
  if (!GetObjectW(hbm, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight == 0) return false;
  w = bm.bmWidth;
  h = bm.bmHeight < 0 ? -bm.bmHeight : bm.bmHeight;
  if (w > 1024 || h > 1024) return false;
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  px.assign(size_t(w) * size_t(h), 0);
  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc) return false;
  const int lines = GetDIBits(dc, hbm, 0, UINT(h), px.data(), &bi, DIB_RGB_COLORS);
  DeleteDC(dc);
  if (lines != h) return false;
  if (hadAlphaChannel) *hadAlphaChannel = bm.bmBitsPixel == 32;
  if (bm.bmBitsPixel != 32)
    for (uint32_t& p : px) p |= 0xFF000000u;
  return true;
}

bool IconPixels(HICON icon, std::vector<uint32_t>& px, int& w, int& h) {
  ICONINFO ii{};
  if (!GetIconInfo(icon, &ii)) return false;
  bool ok = false;
  if (ii.hbmColor) {
    bool alphaChannel = false;
    ok = BitmapPixels(ii.hbmColor, px, w, h, &alphaChannel);
    bool anyAlpha = false;
    if (ok && alphaChannel)
      for (uint32_t p : px)
        if (p >> 24) {
          anyAlpha = true;
          break;
        }
    if (ok && !anyAlpha && ii.hbmMask) {
      // Old-style icon: transparency comes from the AND mask (white = transparent).
      std::vector<uint32_t> mask;
      int mw = 0, mh = 0;
      if (BitmapPixels(ii.hbmMask, mask, mw, mh, nullptr) && mw == w && mh >= h) {
        for (size_t i = 0; i < px.size(); ++i)
          px[i] = (mask[i] & 0x00FFFFFFu) ? 0u : (px[i] | 0xFF000000u);
      }
    }
  }
  if (ii.hbmColor) DeleteObject(ii.hbmColor);
  if (ii.hbmMask) DeleteObject(ii.hbmMask);
  return ok;
}

}  // namespace

IconCache::IconCache() = default;

IconCache::~IconCache() { Stop(); }

void IconCache::Start(HWND notify, UINT notifyMsg) {
  if (thread_.joinable()) return;
  notify_ = notify;
  notifyMsg_ = notifyMsg;
  stop_ = false;
  thread_ = std::thread([this] { Worker(); });
}

void IconCache::Stop() {
  {
    std::lock_guard lock(mu_);
    stop_ = true;
    queue_.clear();
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

std::wstring IconCache::MakeKey(IconKind kind, const std::wstring& source, int px, bool* byExt) const {
  std::wstring key = std::to_wstring(px);
  *byExt = false;
  if (kind == IconKind::FilePath) {
    const size_t slash = source.find_last_of(L"\\/");
    const size_t dot = source.rfind(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash) && dot + 1 < source.size()) {
      const std::wstring_view ext(source.data() + dot, source.size() - dot);
      if (!PerFileExtension(ext) && ext.size() <= 16) {
        std::wstring lower = str::ToLower(source);
        if (!dirPaths_.count(lower)) {
          *byExt = true;
          key += L"|e";
          key.append(str::ToLower(ext));
          return key;
        }
      }
    }
    key += L"|f";
  } else {
    key += L"|s";
  }
  key += str::ToLower(source);
  return key;
}

IconCache::State IconCache::Get(ID2D1RenderTarget* rt, IconKind kind, const std::wstring& source, int px,
                                ComPtr<ID2D1Bitmap>* out) {
  if (source.empty() || px <= 0 || (kind != IconKind::FilePath && kind != IconKind::ShellItem)) return State::Failed;
  bool byExt = false;
  std::wstring key = MakeKey(kind, source, px, &byExt);
  auto it = entries_.find(key);
  if (it == entries_.end()) {
    if (pending_.insert(key).second) {
      {
        std::lock_guard lock(mu_);
        queue_.push_back(Request{key, source, px, byExt});
      }
      cv_.notify_one();
    }
    return State::Loading;
  }
  Entry& e = it->second;
  lru_.splice(lru_.begin(), lru_, e.lru);
  if (e.failed) return State::Failed;
  if (!e.bitmap) {
    if (!rt || e.pixels.empty()) return State::Failed;
    const D2D1_BITMAP_PROPERTIES props =
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    HRESULT hr = rt->CreateBitmap(D2D1::SizeU(UINT32(e.w), UINT32(e.h)), e.pixels.data(), UINT32(e.w) * 4, props,
                                  e.bitmap.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
      e.failed = true;
      std::vector<uint32_t>().swap(e.pixels);
      return State::Failed;
    }
    std::vector<uint32_t>().swap(e.pixels);
  }
  *out = e.bitmap;
  return State::Ready;
}

void IconCache::Insert(const std::wstring& key, Done&& d) {
  auto it = entries_.find(key);
  if (it != entries_.end()) return;  // e.g. two paths of the same type racing
  while (entries_.size() >= kCapacity && !lru_.empty()) {
    entries_.erase(lru_.back());
    lru_.pop_back();
  }
  lru_.push_front(key);
  Entry& e = entries_[key];
  e.lru = lru_.begin();
  e.failed = d.pixels.empty();
  e.w = d.w;
  e.h = d.h;
  e.pixels = std::move(d.pixels);
}

bool IconCache::OnLoaded() {
  std::vector<Done> batch;
  {
    std::lock_guard lock(mu_);
    batch.swap(done_);
  }
  for (Done& d : batch) {
    pending_.erase(d.req.key);
    if (d.req.byExt && d.isDir) {
      // "node.js" style folder: never share the per-type entry, cache under the path itself.
      dirPaths_.insert(str::ToLower(d.req.source));
      bool byExt = false;
      Insert(MakeKey(IconKind::FilePath, d.req.source, d.req.px, &byExt), std::move(d));
    } else {
      Insert(d.req.key, std::move(d));
    }
  }
  return !batch.empty();
}

void IconCache::CancelPending() {
  std::lock_guard lock(mu_);
  for (const Request& r : queue_) pending_.erase(r.key);
  queue_.clear();
}

void IconCache::DiscardDeviceResources() {
  entries_.clear();
  lru_.clear();
}

void IconCache::Worker() {
  win::ComInit com;  // STA, as the shell expects
  for (;;) {
    Request req;
    {
      std::unique_lock lock(mu_);
      cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
      if (stop_) return;
      req = std::move(queue_.front());
      queue_.pop_front();
    }
    Done d;
    d.req = std::move(req);
    Load(d.req, d);
    bool notify = false;
    {
      std::lock_guard lock(mu_);
      if (stop_) return;
      notify = done_.empty();
      done_.push_back(std::move(d));
    }
    if (notify) PostMessageW(notify_, notifyMsg_, 0, 0);
  }
}

void IconCache::Load(const Request& r, Done& d) {
  if (r.byExt) {
    const DWORD attr = GetFileAttributesW(r.source.c_str());
    d.isDir = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
  }
  std::vector<uint32_t> px;
  int w = 0, h = 0;
  bool ok = false;

  ComPtr<IShellItemImageFactory> factory;
  if (SUCCEEDED(SHCreateItemFromParsingName(r.source.c_str(), nullptr, IID_PPV_ARGS(&factory)))) {
    HBITMAP hbm = nullptr;
    if (SUCCEEDED(factory->GetImage(SIZE{r.px, r.px}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbm)) && hbm) {
      ok = BitmapPixels(hbm, px, w, h, nullptr);
      DeleteObject(hbm);
    }
  }
  if (!ok) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (SUCCEEDED(SHParseDisplayName(r.source.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
      SHFILEINFOW sfi{};
      if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, &sfi, sizeof(sfi),
                         SHGFI_PIDL | SHGFI_ICON | SHGFI_LARGEICON) &&
          sfi.hIcon) {
        ok = IconPixels(sfi.hIcon, px, w, h);
        DestroyIcon(sfi.hIcon);
      }
      CoTaskMemFree(pidl);
    }
  }
  if (!ok || px.empty()) return;
  FixAlpha(px);
  Downscale(px, w, h, r.px);
  d.pixels = std::move(px);
  d.w = w;
  d.h = h;
}

}  // namespace cs::ui
