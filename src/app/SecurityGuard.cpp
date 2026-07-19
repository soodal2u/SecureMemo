#include "app/SecurityGuard.h"

#include "app/Application.h"
#include "ui/Theme.h"

namespace securememo {

SecurityGuard::SecurityGuard(Application* app) : app_(app) {
  lastActivityTick_ = GetTickCount();
}

void SecurityGuard::Start(HWND hwnd) {
  lastActivityTick_ = GetTickCount();
  SetTimer(hwnd, IDT_AUTO_LOCK, 2000, nullptr);  // check every 2s
}

void SecurityGuard::Stop(HWND hwnd) {
  KillTimer(hwnd, IDT_AUTO_LOCK);
}

void SecurityGuard::SetAutoLockMinutes(int minutes) {
  if (minutes < 0) minutes = 0;
  if (minutes > 360) minutes = 360;  // up to 6 hours
  autoLockMinutes_ = minutes;
  lastActivityTick_ = GetTickCount();
}

void SecurityGuard::NotifyActivity() {
  // Only app-side activity (typing notes, using list, etc.)
  // NOT global mouse/keyboard elsewhere on the PC.
  lastActivityTick_ = GetTickCount();
}

void SecurityGuard::OnTimer(HWND hwnd) {
  (void)hwnd;
  if (!app_ || !app_->IsUnlocked()) return;
  if (autoLockMinutes_ <= 0) return;

  // Idle = no note editing / app interaction for N minutes
  const DWORD now = GetTickCount();
  const DWORD idleMs = now - lastActivityTick_;
  const DWORD limitMs = static_cast<DWORD>(autoLockMinutes_) * 60u * 1000u;
  if (idleMs >= limitMs) {
    app_->LockNow();
  }
}

}  // namespace securememo
