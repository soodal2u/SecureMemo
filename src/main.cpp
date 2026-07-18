#include "app/Application.h"
#include "ui/Theme.h"

#include <windows.h>
#include <shobjidl.h>

namespace {

// Prevent two processes from opening/writing the same vault (data-loss risk).
// If already running, activate the existing instance and exit.
bool EnsureSingleInstance(HANDLE& outMutex) {
  outMutex = CreateMutexW(nullptr, TRUE, securememo::kSingleInstanceMutex);
  if (!outMutex) return false;

  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    // Find the hidden app host window of the first instance
    HWND existing = FindWindowW(securememo::kAppClass, securememo::kAppTitle);
    if (existing) {
      // Ask first instance to show list / tray UI
      PostMessageW(existing, securememo::WM_APP_ACTIVATE_EXISTING, 0, 0);
      // Also try to bring any list window forward
      HWND list = FindWindowW(securememo::kListClass, securememo::kAppTitle);
      if (list) {
        ShowWindow(list, SW_SHOW);
        ShowWindow(list, SW_RESTORE);
        SetForegroundWindow(list);
      }
    }
    ReleaseMutex(outMutex);
    CloseHandle(outMutex);
    outMutex = nullptr;
    return false;
  }
  return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
  SetProcessDPIAware();

  // Helps Windows notification center use our identity (with installed shortcut)
  SetCurrentProcessExplicitAppUserModelID(L"SecureMemo.App");

  HANDLE mutex = nullptr;
  if (!EnsureSingleInstance(mutex)) {
    // Another instance is already running — do not open vault again
    return 0;
  }

  int code = 0;
  {
    securememo::Application app;
    code = app.Run(hInstance);
  }

  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  return code;
}
