#pragma once
// Thin Win32 helpers shared by app, ui and providers. Windows-only.
#include <windows.h>
#include <objbase.h>
#include <shtypes.h>

#include <string>
#include <string_view>

namespace cs::win {

// RAII COM initialisation for the current thread.
class ComInit {
 public:
  explicit ComInit(DWORD flags = COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  ~ComInit();
  ComInit(const ComInit&) = delete;
  ComInit& operator=(const ComInit&) = delete;
  bool ok() const { return SUCCEEDED(hr_); }

 private:
  HRESULT hr_;
};

std::wstring ExpandEnv(std::wstring_view s);       // "%USERPROFILE%\\x" -> "C:\\Users\\me\\x"; also leading "~"
std::wstring KnownFolder(REFKNOWNFOLDERID id);     // FOLDERID_Downloads etc. Empty on failure.
std::wstring ExePath();                            // full path of the running exe
bool FileExists(std::wstring_view path);
bool DirExists(std::wstring_view path);

// ShellExecuteEx wrapper. target may be a path, URL, "ms-settings:..." or "shell:AppsFolder\\<AUMID>".
bool ShellOpen(std::wstring_view target, std::wstring_view args = {}, bool admin = false,
               std::wstring_view workDir = {});
// Opens Explorer with the item selected (falls back to opening the parent folder).
bool RevealInExplorer(std::wstring_view path);
// Starts a console program without showing a window (e.g. "shutdown /s /t 1800"). Does not wait.
bool RunHidden(std::wstring_view commandLine);

// Reads HKCU\...\Themes\Personalize\AppsUseLightTheme. true = dark.
bool SystemUsesDarkTheme();

}  // namespace cs::win
