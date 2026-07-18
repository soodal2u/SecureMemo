#pragma once

#include <windows.h>

namespace securememo {

class Application;

// Tracks idle time and requests auto-lock
class SecurityGuard {
 public:
  explicit SecurityGuard(Application* app);

  void Start(HWND hwnd);
  void Stop(HWND hwnd);
  void OnTimer(HWND hwnd);
  void SetAutoLockMinutes(int minutes);
  int AutoLockMinutes() const { return autoLockMinutes_; }
  void NotifyActivity();

 private:
  Application* app_;
  int autoLockMinutes_ = 5;
  DWORD lastActivityTick_ = 0;
};

}  // namespace securememo
