#include "ui/LockDialog.h"

#include "ui/Theme.h"
#include "util/SecureBuffer.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>

namespace securememo {
namespace {

enum CtrlId : int {
  IDC_PASS1 = 101,
  IDC_PASS2 = 102,
  IDC_PASS_OLD = 103,
  IDC_OK = 1,
  IDC_CANCEL = 2,
  IDC_MSG = 104,
  IDC_TITLE = 105,
};

struct DlgState {
  LockDialogMode mode;
  const wchar_t* message;
  LockDialogResult* result;
  HWND anchor = nullptr;  // list or note window to position near
  HWND hPass1 = nullptr;
  HWND hPass2 = nullptr;
  HWND hPassOld = nullptr;
  HBRUSH bgBrush = nullptr;
  HBRUSH editBrush = nullptr;
  HFONT titleFont = nullptr;
  HFONT uiFont = nullptr;
};

// Place password dialog centered over list/note (or work area fallback)
void PositionNearAnchor(HWND hwnd, HWND anchor) {
  RECT rc{};
  GetWindowRect(hwnd, &rc);
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;

  int x = 0, y = 0;
  HWND ref = (anchor && IsWindow(anchor)) ? anchor : nullptr;
  if (ref && IsWindowVisible(ref)) {
    RECT ar{};
    GetWindowRect(ref, &ar);
    x = ar.left + ((ar.right - ar.left) - w) / 2;
    y = ar.top + ((ar.bottom - ar.top) - h) / 3;
    if (y < ar.top + 8) y = ar.top + 8;
  } else {
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    x = wa.left + ((wa.right - wa.left) - w) / 2;
    y = wa.top + ((wa.bottom - wa.top) - h) / 3;
  }

  // Keep fully visible on the monitor of the anchor (or dialog)
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  HMONITOR mon = MonitorFromWindow(ref ? ref : hwnd, MONITOR_DEFAULTTONEAREST);
  if (GetMonitorInfoW(mon, &mi)) {
    const RECT& wa = mi.rcWork;
    if (x < wa.left + 8) x = wa.left + 8;
    if (y < wa.top + 8) y = wa.top + 8;
    if (x + w > wa.right - 8) x = wa.right - w - 8;
    if (y + h > wa.bottom - 8) y = wa.bottom - h - 8;
  }
  SetWindowPos(hwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
}

const wchar_t* TitleFor(LockDialogMode mode) {
  switch (mode) {
    case LockDialogMode::Setup:
      return L"비밀번호 설정";
    case LockDialogMode::ConfirmExit:
      return L"종료 확인";
    case LockDialogMode::ChangePassword:
      return L"비밀번호 변경";
    default:
      return L"잠금 해제";
  }
}

LRESULT CALLBACK LockWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  DlgState* st = reinterpret_cast<DlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {
    case WM_CREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
      st = reinterpret_cast<DlgState*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

      st->bgBrush = CreateSolidBrush(DarkUI::kBg);
      st->editBrush = CreateSolidBrush(DarkUI::kSearchBg);
      st->titleFont = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
      st->uiFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

      ApplyWin11Chrome(hwnd, DarkUI::kBorder);

      // accent strip drawn in paint; title
      HWND title = CreateWindowExW(0, L"STATIC", TitleFor(st->mode),
                                   WS_CHILD | WS_VISIBLE, 28, 28, 360, 30, hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TITLE)),
                                   cs->hInstance, nullptr);
      SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(st->titleFont), TRUE);

      HWND msgCtl = CreateWindowExW(0, L"STATIC", st->message ? st->message : L"",
                                    WS_CHILD | WS_VISIBLE, 28, 64, 360, 40, hwnd,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MSG)),
                                    cs->hInstance, nullptr);
      SendMessageW(msgCtl, WM_SETFONT, reinterpret_cast<WPARAM>(st->uiFont), TRUE);

      int y = 120;

      auto makeLabel = [&](const wchar_t* text, int yy) {
        HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                 28, yy, 360, 20, hwnd, nullptr, cs->hInstance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(st->uiFont), TRUE);
      };
      auto makeEdit = [&](int id, int yy) -> HWND {
        HWND h = CreateWindowExW(0, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
                                 28, yy, 360, 36, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 cs->hInstance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(st->uiFont), TRUE);
        SendMessageW(h, EM_SETLIMITTEXT, 256, 0);
        return h;
      };

      if (st->mode == LockDialogMode::Setup) {
        makeLabel(L"새 비밀번호", y);
        st->hPass1 = makeEdit(IDC_PASS1, y + 24);
        y += 78;
        makeLabel(L"비밀번호 확인", y);
        st->hPass2 = makeEdit(IDC_PASS2, y + 24);
        y += 78;
      } else if (st->mode == LockDialogMode::ChangePassword) {
        makeLabel(L"현재 비밀번호", y);
        st->hPassOld = makeEdit(IDC_PASS_OLD, y + 24);
        y += 78;
        makeLabel(L"새 비밀번호", y);
        st->hPass1 = makeEdit(IDC_PASS1, y + 24);
        y += 78;
        makeLabel(L"새 비밀번호 확인", y);
        st->hPass2 = makeEdit(IDC_PASS2, y + 24);
        y += 78;
      } else {
        makeLabel(L"비밀번호", y);
        st->hPass1 = makeEdit(IDC_PASS1, y + 24);
        y += 78;
      }

      HWND ok = CreateWindowExW(0, L"BUTTON", L"확인",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                196, y + 16, 100, 36, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OK)),
                                cs->hInstance, nullptr);
      HWND cancel = CreateWindowExW(0, L"BUTTON", L"취소",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                    304, y + 16, 84, 36, hwnd,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CANCEL)),
                                    cs->hInstance, nullptr);
      SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(st->uiFont), TRUE);
      SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(st->uiFont), TRUE);

      // client size then place near list/note
      SetWindowPos(hwnd, nullptr, 0, 0, 420, y + 80, SWP_NOMOVE | SWP_NOZORDER);
      PositionNearAnchor(hwnd, st->anchor);

      if (st->hPassOld) SetFocus(st->hPassOld);
      else if (st->hPass1) SetFocus(st->hPass1);
      return 0;
    }

    case WM_NCCALCSIZE:
      if (wParam == TRUE) return 0;
      break;

    case WM_NCHITTEST: {
      LRESULT edge = HitTestBorderless(hwnd, lParam, 4);
      if (edge != HTCLIENT) return edge;
      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ScreenToClient(hwnd, &pt);
      if (pt.y < 24) return HTCAPTION;
      return HTCLIENT;
    }

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      if (st && st->bgBrush) FillRect(hdc, &rc, st->bgBrush);
      // gold top accent
      RECT accent{0, 0, rc.right, 4};
      HBRUSH ab = CreateSolidBrush(DarkUI::kLockGold);
      FillRect(hdc, &accent, ab);
      DeleteObject(ab);
      // border
      HPEN pen = CreatePen(PS_SOLID, 1, DarkUI::kBorder);
      HGDIOBJ op = SelectObject(hdc, pen);
      SelectObject(hdc, GetStockObject(NULL_BRUSH));
      Rectangle(hdc, 0, 0, rc.right, rc.bottom);
      SelectObject(hdc, op);
      DeleteObject(pen);
      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_CTLCOLORSTATIC: {
      if (!st) break;
      HDC hdc = reinterpret_cast<HDC>(wParam);
      SetBkColor(hdc, DarkUI::kBg);
      SetTextColor(hdc, DarkUI::kText);
      SetBkMode(hdc, OPAQUE);
      return reinterpret_cast<LRESULT>(st->bgBrush);
    }

    case WM_CTLCOLOREDIT: {
      if (!st) break;
      HDC hdc = reinterpret_cast<HDC>(wParam);
      SetBkColor(hdc, DarkUI::kSearchBg);
      SetTextColor(hdc, DarkUI::kText);
      SetBkMode(hdc, OPAQUE);
      return reinterpret_cast<LRESULT>(st->editBrush);
    }

    case WM_COMMAND: {
      if (!st) break;
      const int id = LOWORD(wParam);
      if (id == IDC_CANCEL || id == IDCANCEL) {
        st->result->ok = false;
        DestroyWindow(hwnd);
        return 0;
      }
      if (id == IDC_OK || id == IDOK) {
        auto readEdit = [](HWND h) -> std::wstring {
          if (!h) return {};
          int len = GetWindowTextLengthW(h);
          std::wstring s(static_cast<size_t>(len) + 1, L'\0');
          GetWindowTextW(h, s.data(), len + 1);
          s.resize(static_cast<size_t>(len));
          return s;
        };

        if (st->mode == LockDialogMode::Setup) {
          auto p1 = readEdit(st->hPass1);
          auto p2 = readEdit(st->hPass2);
          if (p1.empty()) {
            MessageBoxW(hwnd, L"비밀번호를 입력하세요.", kAppTitle, MB_ICONWARNING);
            SecureWipeString(p1);
            SecureWipeString(p2);
            return 0;
          }
          if (p1 != p2) {
            MessageBoxW(hwnd, L"비밀번호가 일치하지 않습니다.", kAppTitle, MB_ICONWARNING);
            SecureWipeString(p1);
            SecureWipeString(p2);
            return 0;
          }
          if (p1.size() < 4) {
            MessageBoxW(hwnd, L"비밀번호는 최소 4자 이상이어야 합니다.", kAppTitle, MB_ICONWARNING);
            SecureWipeString(p1);
            SecureWipeString(p2);
            return 0;
          }
          st->result->ok = true;
          st->result->password = p1;
          st->result->newPassword = p1;
          SecureWipeString(p2);
          DestroyWindow(hwnd);
          return 0;
        }

        if (st->mode == LockDialogMode::ChangePassword) {
          auto oldP = readEdit(st->hPassOld);
          auto p1 = readEdit(st->hPass1);
          auto p2 = readEdit(st->hPass2);
          if (oldP.empty() || p1.empty()) {
            MessageBoxW(hwnd, L"모든 필드를 입력하세요.", kAppTitle, MB_ICONWARNING);
            SecureWipeString(oldP);
            SecureWipeString(p1);
            SecureWipeString(p2);
            return 0;
          }
          if (p1 != p2) {
            MessageBoxW(hwnd, L"새 비밀번호가 일치하지 않습니다.", kAppTitle, MB_ICONWARNING);
            SecureWipeString(oldP);
            SecureWipeString(p1);
            SecureWipeString(p2);
            return 0;
          }
          if (p1.size() < 4) {
            MessageBoxW(hwnd, L"비밀번호는 최소 4자 이상이어야 합니다.", kAppTitle, MB_ICONWARNING);
            SecureWipeString(oldP);
            SecureWipeString(p1);
            SecureWipeString(p2);
            return 0;
          }
          st->result->ok = true;
          st->result->password = oldP;
          st->result->newPassword = p1;
          SecureWipeString(p2);
          DestroyWindow(hwnd);
          return 0;
        }

        auto p = readEdit(st->hPass1);
        if (p.empty()) {
          MessageBoxW(hwnd, L"비밀번호를 입력하세요.", kAppTitle, MB_ICONWARNING);
          return 0;
        }
        st->result->ok = true;
        st->result->password = p;
        DestroyWindow(hwnd);
        return 0;
      }
      break;
    }

    case WM_CLOSE:
      if (st) st->result->ok = false;
      DestroyWindow(hwnd);
      return 0;

    case WM_DESTROY:
      if (st) {
        if (st->bgBrush) DeleteObject(st->bgBrush);
        if (st->editBrush) DeleteObject(st->editBrush);
        if (st->titleFont) DeleteObject(st->titleFont);
        if (st->uiFont) DeleteObject(st->uiFont);
        st->bgBrush = nullptr;
        st->editBrush = nullptr;
        st->titleFont = nullptr;
        st->uiFont = nullptr;
      }
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool EnsureClass(HINSTANCE inst) {
  static bool registered = false;
  if (registered) return true;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = LockWndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.lpszClassName = L"SecureMemoLockDialog";
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }
  registered = true;
  return true;
}

}  // namespace

LockDialogResult ShowLockDialog(HWND owner, LockDialogMode mode, const wchar_t* message) {
  LockDialogResult result;
  HINSTANCE inst = GetModuleHandleW(nullptr);
  if (!EnsureClass(inst)) return result;

  const wchar_t* defaultMsg = L"";
  switch (mode) {
    case LockDialogMode::Setup:
      defaultMsg = L"최초 실행입니다. 마스터 비밀번호를 설정하세요.";
      break;
    case LockDialogMode::ConfirmExit:
      defaultMsg = L"보안 메모를 종료합니다.";
      break;
    case LockDialogMode::ChangePassword:
      defaultMsg = L"새 비밀번호로 vault를 다시 암호화합니다.";
      break;
    default:
      defaultMsg = L"메모를 보려면 비밀번호를 입력하세요.";
      break;
  }

  DlgState state;
  state.mode = mode;
  state.message = message ? message : defaultMsg;
  state.result = &result;
  // Prefer visible list/note as geometric anchor; owner may be hidden host window
  state.anchor = (owner && IsWindow(owner) && IsWindowVisible(owner)) ? owner : nullptr;

  HWND hwnd = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
      L"SecureMemoLockDialog",
      kAppTitle,
      WS_POPUP | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, 420, 280,
      owner, nullptr, inst, &state);

  if (!hwnd) return result;

  // Don't disable hidden host window only — disable visible anchor if different
  if (state.anchor) EnableWindow(state.anchor, FALSE);
  else if (owner) EnableWindow(owner, FALSE);

  PositionNearAnchor(hwnd, state.anchor);
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  SetForegroundWindow(hwnd);

  MSG msg;
  while (IsWindow(hwnd)) {
    const BOOL gm = GetMessageW(&msg, nullptr, 0, 0);
    if (gm <= 0) {
      if (gm == 0) PostQuitMessage(static_cast<int>(msg.wParam));
      break;
    }
    if (!IsDialogMessageW(hwnd, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  if (state.anchor && IsWindow(state.anchor)) {
    EnableWindow(state.anchor, TRUE);
    SetForegroundWindow(state.anchor);
  } else if (owner && IsWindow(owner)) {
    EnableWindow(owner, TRUE);
  }
  return result;
}

}  // namespace securememo
