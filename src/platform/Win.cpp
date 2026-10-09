#include "platform/Win.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <vector>

namespace cs::win {

ComInit::ComInit(DWORD flags) : hr_(CoInitializeEx(nullptr, flags)) {}
ComInit::~ComInit() {
  if (SUCCEEDED(hr_)) CoUninitialize();
}

std::wstring ExpandEnv(std::wstring_view s) {
  std::wstring in(s);
  if (!in.empty() && in[0] == L'~' && (in.size() == 1 || in[1] == L'\\' || in[1] == L'/'))
    in.replace(0, 1, L"%USERPROFILE%");
  DWORD n = ExpandEnvironmentStringsW(in.c_str(), nullptr, 0);
  if (!n) return in;
  std::wstring out(n, L'\0');
  n = ExpandEnvironmentStringsW(in.c_str(), out.data(), n);
  out.resize(n ? n - 1 : 0);
  return out;
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR p = nullptr;
  std::wstring r;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) && p) r = p;
  CoTaskMemFree(p);
  return r;
}

std::wstring ExePath() {
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    if (n < buf.size()) { buf.resize(n); return buf; }
    buf.resize(buf.size() * 2);
  }
}

bool FileExists(std::wstring_view path) {
  DWORD a = GetFileAttributesW(std::wstring(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(std::wstring_view path) {
  DWORD a = GetFileAttributesW(std::wstring(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ShellOpen(std::wstring_view target, std::wstring_view args, bool admin, std::wstring_view workDir) {
  std::wstring t(target), a(args), d(workDir);
  SHELLEXECUTEINFOW sei{sizeof(sei)};
  sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI | SEE_MASK_INVOKEIDLIST;
  sei.lpVerb = admin ? L"runas" : nullptr;
  sei.lpFile = t.c_str();
  sei.lpParameters = a.empty() ? nullptr : a.c_str();
  sei.lpDirectory = d.empty() ? nullptr : d.c_str();
  sei.nShow = SW_SHOWNORMAL;
  return ShellExecuteExW(&sei) != FALSE;
}

bool RevealInExplorer(std::wstring_view path) {
  std::wstring p(path);
  PIDLIST_ABSOLUTE pidl = nullptr;
  if (SUCCEEDED(SHParseDisplayName(p.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
    HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    CoTaskMemFree(pidl);
    if (SUCCEEDED(hr)) return true;
  }
  return ShellOpen(L"explorer.exe", L"/select,\"" + p + L"\"");
}

bool RunHidden(std::wstring_view commandLine) {
  std::wstring cmd(commandLine);  // CreateProcessW may modify the buffer
  STARTUPINFOW si{sizeof(si)};
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    return false;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

bool SystemUsesDarkTheme() {
  DWORD v = 1, sz = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
    return false;
  return v == 0;
}

}  // namespace cs::win
