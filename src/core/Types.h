#pragma once
// Core contracts shared by every module. Platform-independent: no <windows.h> here.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cs {

struct Config;

// How the UI should draw a result icon.
enum class IconKind : uint8_t {
  None,
  Glyph,         // `icon` holds 1-2 chars of the "Segoe Fluent Icons" font (Win11), e.g. L""
  FilePath,      // `icon` is a filesystem path; UI asks the shell for its icon (cached, async)
  ShellItem,     // `icon` is a shell parsing name, e.g. L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"
};

enum class Action : uint8_t {
  Open,        // Enter
  Reveal,      // Ctrl+Enter  — show in Explorer
  RunAsAdmin,  // Ctrl+Shift+Enter
  Copy,        // Ctrl+C with empty selection / Ctrl+Shift+C — copy copyText (or path)
};

enum ActionMask : uint8_t {
  kActOpen = 1,
  kActReveal = 2,
  kActAdmin = 4,
  kActCopy = 8,
};

struct Result {
  std::wstring title;
  std::wstring subtitle;
  std::wstring category;          // section header shown by the UI, e.g. L"Приложения"
  IconKind iconKind = IconKind::Glyph;
  std::wstring icon;
  float score = 0.f;              // 0..1 from provider; engine adds usage boost on top
  uint8_t actions = kActOpen;
  std::wstring key;               // stable id for usage stats, e.g. L"app:" + parsingName
  std::wstring payload;           // provider-private data (path, uri, value...)
  std::wstring copyText;          // what Action::Copy puts on the clipboard
  int provider = -1;              // filled by SearchEngine
};

struct ExecResult {
  bool hide = true;               // hide launcher after executing
  std::wstring toast;             // optional short message shown by the host (tray notification)
  std::wstring replaceQuery;      // if non-empty and hide==false: UI replaces the query text with this
};

// Services the application host offers to providers. All methods are thread-safe.
class IHost {
 public:
  virtual ~IHost() = default;
  virtual void RequestRefresh() = 0;  // re-run the current query (e.g. currency rates arrived)
  virtual void Notify(std::wstring_view title, std::wstring_view text) = 0;
  virtual void CopyToClipboard(std::wstring_view text) = 0;
};

class IProvider {
 public:
  virtual ~IProvider() = default;
  virtual const wchar_t* Id() const = 0;
  // Called once on the UI thread at startup. Must return quickly: heavy work -> own background thread.
  virtual void Init(const Config& cfg, IHost& host) { (void)cfg; (void)host; }
  // Called on the UI thread for every keystroke. Budget: ~2 ms. Append results to `out`.
  // `query` is trimmed and non-empty. Must be safe against the provider's own background threads.
  virtual void Search(std::wstring_view query, std::vector<Result>& out) = 0;
  // Called on the UI thread when the user activates a result produced by this provider.
  virtual ExecResult Execute(const Result& r, Action a) = 0;
  // Called once on exit. Stop/join background threads here.
  virtual void Shutdown() {}
};

}  // namespace cs
