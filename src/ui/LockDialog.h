#pragma once

#include <windows.h>
#include <string>

namespace securememo {

enum class LockDialogMode {
  Unlock,
  Setup,
  ConfirmExit,
  ConfirmLock,  // optional re-auth not needed for lock, but used if desired
  ChangePassword,
};

struct LockDialogResult {
  bool ok = false;
  std::wstring password;
  std::wstring newPassword;  // for setup / change
};

// Modal dialog. Owner may be nullptr.
LockDialogResult ShowLockDialog(HWND owner, LockDialogMode mode, const wchar_t* message = nullptr);

}  // namespace securememo
