#pragma once

#include <windows.h>
#include <shellapi.h>

namespace securememo {

class TrayIcon {
 public:
  TrayIcon();
  ~TrayIcon();

  bool Create(HWND owner);
  void Destroy();
  void ShowBalloon(const wchar_t* title, const wchar_t* text);
  bool HandleMessage(WPARAM wParam, LPARAM lParam);

 private:
  HWND owner_ = nullptr;
  NOTIFYICONDATAW nid_{};
  bool added_ = false;
  HMENU menu_ = nullptr;
};

}  // namespace securememo
