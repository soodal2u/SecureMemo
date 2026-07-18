#include "ui/TrayIcon.h"

#include "ui/Theme.h"

#include <strsafe.h>

namespace securememo {

TrayIcon::TrayIcon() {
  ZeroMemory(&nid_, sizeof(nid_));
}

TrayIcon::~TrayIcon() {
  Destroy();
}

bool TrayIcon::Create(HWND owner) {
  owner_ = owner;
  ZeroMemory(&nid_, sizeof(nid_));
  nid_.cbSize = sizeof(NOTIFYICONDATAW);
  nid_.hWnd = owner;
  nid_.uID = 1;
  nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid_.uCallbackMessage = WM_TRAYICON;
  nid_.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
  if (!nid_.hIcon) nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  StringCchCopyW(nid_.szTip, ARRAYSIZE(nid_.szTip), kAppTitle);

  menu_ = CreatePopupMenu();
  AppendMenuW(menu_, MF_STRING, IDM_SHOW_LIST, L"노트 목록");
  AppendMenuW(menu_, MF_STRING, IDM_NEW_NOTE, L"새 메모");
  AppendMenuW(menu_, MF_STRING, IDM_LOCK, L"잠금");
  AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu_, MF_STRING, IDM_CHANGE_PASSWORD, L"비밀번호 변경");
  AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu_, MF_STRING, IDM_EXIT, L"종료");

  added_ = Shell_NotifyIconW(NIM_ADD, &nid_) == TRUE;
  if (added_) {
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
  }
  return added_;
}

void TrayIcon::Destroy() {
  if (added_) {
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    added_ = false;
  }
  if (menu_) {
    DestroyMenu(menu_);
    menu_ = nullptr;
  }
}

void TrayIcon::ShowBalloon(const wchar_t* title, const wchar_t* text) {
  if (!added_) return;
  // Toast header uses exe ProductName (ASCII "SecureMemo" in version resource).
  // szInfoTitle / szInfo use UTF-16 code-unit strings from the app.
  nid_.uFlags = NIF_INFO | NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  ZeroMemory(nid_.szInfoTitle, sizeof(nid_.szInfoTitle));
  ZeroMemory(nid_.szInfo, sizeof(nid_.szInfo));
  StringCchCopyW(nid_.szInfoTitle, ARRAYSIZE(nid_.szInfoTitle),
                 (title && title[0]) ? title : kAppTitle);
  StringCchCopyW(nid_.szInfo, ARRAYSIZE(nid_.szInfo),
                 (text && text[0]) ? text : L"");
  nid_.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
  Shell_NotifyIconW(NIM_MODIFY, &nid_);
  nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
}

bool TrayIcon::HandleMessage(WPARAM wParam, LPARAM lParam) {
  (void)wParam;
  // NOTIFYICON_VERSION_4: use NIN_SELECT only for left-click activate.
  // Handling both NIN_SELECT and WM_LBUTTONUP opens two dialogs.
  const UINT ev = LOWORD(lParam);
  if (ev == WM_CONTEXTMENU || ev == WM_RBUTTONUP) {
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(owner_);
    TrackPopupMenu(menu_, TPM_RIGHTBUTTON, pt.x, pt.y, 0, owner_, nullptr);
    PostMessageW(owner_, WM_NULL, 0, 0);
    return true;
  }
  if (ev == NIN_SELECT || ev == NIN_KEYSELECT) {
    PostMessageW(owner_, WM_COMMAND, IDM_SHOW_LIST, 0);
    return true;
  }
  // Ignore raw WM_LBUTTONUP / DBLCLK when VERSION_4 is active (avoids double fire)
  return false;
}

}  // namespace securememo
