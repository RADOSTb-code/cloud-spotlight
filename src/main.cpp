// Cloud Spotlight entry point: single instance, process-wide setup, message loop.
#include <windows.h>
#include <shellapi.h>

#include <string>

#include "app/App.h"
#include "platform/Win.h"

namespace {

constexpr wchar_t kMutexName[] = L"Local\\CloudSpotlight.SingleInstance";

struct Args {
  bool background = false;
  bool restart = false;
};

Args ParseArgs() {
  Args a;
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return a;
  for (int i = 1; i < argc; ++i) {
    if (lstrcmpiW(argv[i], L"--background") == 0 || lstrcmpiW(argv[i], L"/background") == 0) a.background = true;
    else if (lstrcmpiW(argv[i], L"--restart") == 0) a.restart = true;
  }
  LocalFree(argv);
  return a;
}

// Another instance owns the mutex: ask it to show the launcher. Its host window may not exist yet if it is
// still starting, so poll briefly.
void SignalRunningInstance() {
  for (int i = 0; i < 40; ++i) {
    if (HWND host = FindWindowW(cs::kHostWindowClass, nullptr)) {
      DWORD pid = 0;
      GetWindowThreadProcessId(host, &pid);
      AllowSetForegroundWindow(pid);  // we were just launched by the user, so we may pass foreground rights on
      PostMessageW(host, cs::kMsgShowLauncher, 0, 0);
      return;
    }
    Sleep(50);
  }
}

void SpawnRestart() {
  std::wstring exe = cs::win::ExePath();
  std::wstring cmd = L"\"" + exe + L"\" --background --restart";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
  // Also declared in the manifest (which wins); this covers builds without the embedded manifest.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  SetDllDirectoryW(L"");  // drop the current directory from the DLL search path

  const Args args = ParseArgs();

  HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
  if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
    // A restart waits for the old instance to finish shutting down (it owns the mutex until it exits).
    DWORD w = args.restart ? WaitForSingleObject(mutex, 15000) : WAIT_TIMEOUT;
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
      if (!args.background) SignalRunningInstance();
      CloseHandle(mutex);
      return 0;
    }
  }

  int exitCode = 0;
  bool restart = false;
  {
    cs::win::ComInit com(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    cs::App app;
    cs::App::StartOptions opt;
    opt.showLauncher = !args.background;
    opt.restarted = args.restart;
    if (!app.Init(inst, opt)) {
      exitCode = 1;
    } else {
      MSG msg;
      while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      exitCode = int(msg.wParam);
    }
    app.Shutdown();
    restart = app.RestartRequested();
  }

  if (restart) SpawnRestart();  // the new process blocks on the mutex until we release it below
  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  return exitCode;
}
