#pragma once
// Asynchronous shell icon loader + LRU of Direct2D bitmaps.
// Worker thread (COM STA) asks the shell for icons and converts them to premultiplied BGRA pixels; the UI thread
// is notified with PostMessage(notifyMsg), drains the results in OnLoaded() and creates ID2D1Bitmaps lazily.
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/Types.h"

namespace cs::ui {

class IconCache {
 public:
  enum class State : uint8_t { Loading, Ready, Failed };

  IconCache();
  ~IconCache();
  IconCache(const IconCache&) = delete;
  IconCache& operator=(const IconCache&) = delete;

  void Start(HWND notify, UINT notifyMsg);
  void Stop();

  // UI thread. kind must be FilePath or ShellItem. On Ready, *out receives the bitmap (created on `rt` if needed).
  State Get(ID2D1RenderTarget* rt, IconKind kind, const std::wstring& source, int px,
            Microsoft::WRL::ComPtr<ID2D1Bitmap>* out);
  // UI thread: move finished loads into the cache. Returns true if anything arrived.
  bool OnLoaded();
  // UI thread: forget queued (not yet started) requests — call when the result list changes.
  void CancelPending();
  // UI thread: device lost — drop every bitmap.
  void DiscardDeviceResources();

 private:
  struct Request {
    std::wstring key;
    std::wstring source;
    int px = 0;
    bool byExt = false;
  };
  struct Done {
    Request req;
    std::vector<uint32_t> pixels;  // premultiplied BGRA, top-down
    int w = 0, h = 0;
    bool isDir = false;
  };
  struct Entry {
    std::vector<uint32_t> pixels;  // released once the bitmap exists
    int w = 0, h = 0;
    bool failed = false;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    std::list<std::wstring>::iterator lru;
  };

  void Worker();
  static void Load(const Request& r, Done& d);
  void Insert(const std::wstring& key, Done&& d);
  std::wstring MakeKey(IconKind kind, const std::wstring& source, int px, bool* byExt) const;

  static constexpr size_t kCapacity = 300;

  // UI-thread state
  std::unordered_map<std::wstring, Entry> entries_;
  std::list<std::wstring> lru_;  // front = most recent
  std::unordered_set<std::wstring> pending_;
  std::unordered_set<std::wstring> dirPaths_;  // ext-keyed paths that turned out to be directories
  std::wstring keyBuf_;

  // Shared with the worker
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Request> queue_;
  std::vector<Done> done_;
  bool stop_ = false;
  HWND notify_ = nullptr;
  UINT notifyMsg_ = 0;
  std::thread thread_;
};

}  // namespace cs::ui
