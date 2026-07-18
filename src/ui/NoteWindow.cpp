#include "ui/NoteWindow.h"

#include "crypto/Crypto.h"
#include "ui/Theme.h"
#include "util/SecureBuffer.h"

#include <commdlg.h>
#include <dwmapi.h>
#include <richedit.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <vector>

#ifndef MSFTEDIT_CLASS
#define MSFTEDIT_CLASS L"RICHEDIT50W"
#endif

namespace securememo {
namespace {

struct StreamCookie {
  const char* data = nullptr;
  size_t size = 0;
  size_t pos = 0;
  std::string* out = nullptr;
};

DWORD CALLBACK EditStreamInCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) {
  auto* c = reinterpret_cast<StreamCookie*>(dwCookie);
  if (!c || !c->data) {
    *pcb = 0;
    return 0;
  }
  size_t remain = (c->pos < c->size) ? (c->size - c->pos) : 0;
  LONG n = static_cast<LONG>((remain < static_cast<size_t>(cb)) ? remain : static_cast<size_t>(cb));
  if (n > 0) {
    memcpy(pbBuff, c->data + c->pos, static_cast<size_t>(n));
    c->pos += static_cast<size_t>(n);
  }
  *pcb = n;
  return 0;
}

DWORD CALLBACK EditStreamOutCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) {
  auto* c = reinterpret_cast<StreamCookie*>(dwCookie);
  if (!c || !c->out) {
    *pcb = 0;
    return 0;
  }
  c->out->append(reinterpret_cast<char*>(pbBuff), static_cast<size_t>(cb));
  *pcb = cb;
  return 0;
}

}  // namespace

bool NoteWindow::EnsureRichEdit() {
  static HMODULE mod = nullptr;
  if (!mod) mod = LoadLibraryW(L"Msftedit.dll");
  return mod != nullptr;
}

NoteWindow::NoteWindow() = default;

NoteWindow::~NoteWindow() {
  Destroy();
}

bool NoteWindow::RegisterClass(HINSTANCE inst) {
  EnsureRichEdit();
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_DROPSHADOW;
  wc.lpfnWndProc = NoteWindow::WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kNoteClass;
  return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void NoteWindow::EnsureFonts() {
  if (!bodyFont_)
    bodyFont_ = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  if (!iconFont_)
    iconFont_ = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, L"Segoe UI Symbol");
  if (!toolFont_)
    toolFont_ = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void NoteWindow::ApplyChrome() {
  if (!hwnd_) return;
  ApplyWin11Chrome(hwnd_, DarkUI::kBorder);
}

bool NoteWindow::Create(HINSTANCE inst, HWND appHwnd, const Note& note) {
  inst_ = inst;
  appHwnd_ = appHwnd;
  note_ = note;
  EnsureRichEdit();
  EnsureFonts();
  RebuildBrushes();

  if (note_.width <= 0) note_.width = kNoteDefaultW;
  if (note_.height <= 0) note_.height = kNoteDefaultH;
  if (note_.x < 0 || note_.y < 0 ||
      !IsRectOnAnyMonitor(note_.x, note_.y, note_.width, note_.height)) {
    DefaultNotePlacement(0, note_.x, note_.y, note_.width, note_.height);
  } else {
    ClampRectToWorkArea(note_.x, note_.y, note_.width, note_.height);
  }

  DWORD ex = WS_EX_APPWINDOW | WS_EX_TOOLWINDOW;
  if (note_.alwaysOnTop) ex |= WS_EX_TOPMOST;

  // No WS_THICKFRAME — custom dark edge resize (avoids white border)
  hwnd_ = CreateWindowExW(
      ex, kNoteClass, kAppTitle,
      WS_POPUP | WS_CLIPCHILDREN,
      note_.x, note_.y, note_.width, note_.height,
      nullptr, nullptr, inst, this);
  if (!hwnd_) return false;
  ApplyChrome();

  const wchar_t* richeditClass = MSFTEDIT_CLASS;
  edit_ = CreateWindowExW(
      0, richeditClass, L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
          ES_WANTRETURN | ES_NOHIDESEL,
      0, 0, 100, 100, hwnd_, nullptr, inst, nullptr);
  if (!edit_) {
    // fallback plain edit
    edit_ = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_WANTRETURN | ES_NOHIDESEL,
        0, 0, 100, 100, hwnd_, nullptr, inst, nullptr);
  }

  SendMessageW(edit_, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(DarkUI::kNoteBody));
  SetDefaultCharFormat();
  SendMessageW(edit_, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
  // Dark scrollbar / chrome (Win10+)
  SetWindowTheme(edit_, L"DarkMode_Explorer", nullptr);
  BOOL dark = TRUE;
  DwmSetWindowAttribute(edit_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

  if (!note_.rtf.empty()) {
    LoadRtfOrText(note_.rtf);
    emptyDoc_ = note_.content.empty();
  } else if (!note_.content.empty()) {
    // plain text (or legacy RTF stored in content)
    LoadRtfOrText(note_.content);
    emptyDoc_ = false;
  } else {
    emptyDoc_ = true;
    SetWindowTextW(edit_, L"");
  }

  Layout();
  if (note_.visible) {
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
  }
  return true;
}

void NoteWindow::SetDefaultCharFormat() {
  if (!edit_) return;
  CHARFORMAT2W cf{};
  cf.cbSize = sizeof(cf);
  cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
  cf.crTextColor = DarkUI::kText;
  cf.yHeight = 220;  // twips ~ 11pt
  wcscpy(cf.szFaceName, L"Segoe UI");
  SendMessageW(edit_, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
}

bool NoteWindow::LoadRtfOrText(const std::wstring& data) {
  if (!edit_) return false;
  std::string utf8 = WideToUtf8(data);
  const bool isRtf = utf8.rfind("{\\rtf", 0) == 0;
  if (isRtf) {
    StreamCookie cookie{utf8.data(), utf8.size(), 0, nullptr};
    EDITSTREAM es{};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&cookie);
    es.pfnCallback = EditStreamInCallback;
    SendMessageW(edit_, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
    return true;
  }
  SetWindowTextW(edit_, data.c_str());
  SetDefaultCharFormat();
  return true;
}

std::wstring NoteWindow::SaveRtf() {
  if (!edit_) return {};
  std::string out;
  StreamCookie cookie{nullptr, 0, 0, &out};
  EDITSTREAM es{};
  es.dwCookie = reinterpret_cast<DWORD_PTR>(&cookie);
  es.pfnCallback = EditStreamOutCallback;
  // Prefer RTF; if fails, plain text
  LRESULT r = SendMessageW(edit_, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&es));
  if (r == 0 || out.empty()) {
    int len = GetWindowTextLengthW(edit_);
    std::wstring t(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(edit_, t.data(), len + 1);
    t.resize(static_cast<size_t>(len));
    return t;
  }
  return Utf8ToWide(out);
}

void NoteWindow::Destroy() {
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    edit_ = nullptr;
  }
  if (bodyBrush_) { DeleteObject(bodyBrush_); bodyBrush_ = nullptr; }
  if (headerBrush_) { DeleteObject(headerBrush_); headerBrush_ = nullptr; }
  if (bodyFont_) { DeleteObject(bodyFont_); bodyFont_ = nullptr; }
  if (iconFont_) { DeleteObject(iconFont_); iconFont_ = nullptr; }
  if (toolFont_) { DeleteObject(toolFont_); toolFont_ = nullptr; }
}

void NoteWindow::RebuildBrushes() {
  if (bodyBrush_) DeleteObject(bodyBrush_);
  if (headerBrush_) DeleteObject(headerBrush_);
  auto th = ThemeFor(note_.color);
  bodyBrush_ = CreateSolidBrush(th.body);
  headerBrush_ = CreateSolidBrush(th.header);
}

void NoteWindow::GetHeaderButtons(RECT& newBtn, RECT& menuBtn, RECT& closeBtn) const {
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const int y = (kHeaderH - kBtnSize) / 2;
  newBtn = {kBtnPad, y, kBtnPad + kBtnSize, y + kBtnSize};
  closeBtn = {rc.right - kBtnPad - kBtnSize, y, rc.right - kBtnPad, y + kBtnSize};
  menuBtn = {closeBtn.left - kBtnPad - kBtnSize, y, closeBtn.left - kBtnPad, y + kBtnSize};
}

void NoteWindow::GetToolbarRects(RECT rects[6]) const {
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  int x = 12;
  const int y = rc.bottom - kToolbarH;
  for (int i = 0; i < 6; ++i) {
    rects[i] = {x, y + 4, x + 30, y + kToolbarH - 4};
    x += 34;
  }
}

void NoteWindow::Layout() {
  if (!hwnd_ || !edit_) return;
  RECT rc;
  GetClientRect(hwnd_, &rc);
  if (sessionLocked_) {
    // Hide editor completely while locked
    ShowWindow(edit_, SW_HIDE);
    return;
  }
  ShowWindow(edit_, SW_SHOW);
  MoveWindow(edit_, kBodyPad, kHeaderH + 6, rc.right - kBodyPad * 2,
             rc.bottom - kHeaderH - kToolbarH - 10, TRUE);
}

RECT NoteWindow::UnlockButtonRect() const {
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const int bw = 140;
  const int bh = 34;
  const int cx = (rc.left + rc.right) / 2;
  const int midY = (rc.top + kHeaderH + rc.bottom) / 2;
  const int top = midY + 28;
  return {cx - bw / 2, top, cx + bw / 2, top + bh};
}

void NoteWindow::SetSessionLocked(bool locked) {
  sessionLocked_ = locked;
  unlockBtnHot_ = false;
  if (edit_) {
    EnableWindow(edit_, locked ? FALSE : TRUE);
    ShowWindow(edit_, locked ? SW_HIDE : SW_SHOW);
  }
  Layout();
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NoteWindow::DrawHeaderButton(HDC hdc, const RECT& rc, const wchar_t* glyph, bool hot) {
  auto th = ThemeFor(note_.color);
  if (hot) {
    HBRUSH hotBr = CreateSolidBrush(th.headerHot);
    HGDIOBJ oldB = SelectObject(hdc, hotBr);
    HGDIOBJ oldP = SelectObject(hdc, GetStockObject(NULL_PEN));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, oldB);
    SelectObject(hdc, oldP);
    DeleteObject(hotBr);
  }
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(30, 30, 30));
  SelectObject(hdc, iconFont_ ? iconFont_ : GetStockObject(DEFAULT_GUI_FONT));
  DrawTextW(hdc, glyph, -1, const_cast<RECT*>(&rc), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void NoteWindow::Paint(HDC hdc) {
  RECT rc;
  GetClientRect(hwnd_, &rc);
  FillRect(hdc, &rc, bodyBrush_);

  RECT header{rc.left, rc.top, rc.right, rc.top + kHeaderH};
  FillRect(hdc, &header, headerBrush_);

  if (sessionLocked_) {
    RECT body{rc.left, rc.top + kHeaderH, rc.right, rc.bottom};
    HBRUSH dim = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(hdc, &body, dim);
    DeleteObject(dim);

    const int cx = (rc.left + rc.right) / 2;
    const int midY = (body.top + body.bottom) / 2;

    // Padlock
    {
      const int s = 16;
      HPEN shacklePen = CreatePen(PS_SOLID, 3, DarkUI::kLockGold);
      HBRUSH bodyBr = CreateSolidBrush(DarkUI::kLockGold);
      HGDIOBJ oldP = SelectObject(hdc, shacklePen);
      HGDIOBJ oldB = SelectObject(hdc, GetStockObject(NULL_BRUSH));
      const int iconCy = midY - 40;
      Arc(hdc, cx - s, iconCy - s * 2, cx + s, iconCy + s / 3,
          cx + s, iconCy - s / 4, cx - s, iconCy - s / 4);
      SelectObject(hdc, bodyBr);
      SelectObject(hdc, GetStockObject(NULL_PEN));
      RoundRect(hdc, cx - s - 2, iconCy - s / 5, cx + s + 2, iconCy + s + 4, 6, 6);
      HBRUSH hole = CreateSolidBrush(RGB(30, 30, 30));
      SelectObject(hdc, hole);
      Ellipse(hdc, cx - 3, iconCy + 2, cx + 3, iconCy + 10);
      POINT tri[3] = {{cx - 2, iconCy + 8}, {cx + 2, iconCy + 8}, {cx, iconCy + 16}};
      Polygon(hdc, tri, 3);
      SelectObject(hdc, oldB);
      SelectObject(hdc, oldP);
      DeleteObject(shacklePen);
      DeleteObject(bodyBr);
      DeleteObject(hole);
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, DarkUI::kText);
    SelectObject(hdc, bodyFont_ ? bodyFont_ : GetStockObject(DEFAULT_GUI_FONT));
    RECT msg{rc.left + 12, midY - 4, rc.right - 12, midY + 20};
    DrawTextW(hdc, L"메모가 잠겨 있습니다", -1, &msg,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    RECT btn = UnlockButtonRect();
    HBRUSH bb = CreateSolidBrush(unlockBtnHot_ ? RGB(255, 220, 90) : DarkUI::kLockGold);
    HGDIOBJ ob = SelectObject(hdc, bb);
    HGDIOBJ op2 = SelectObject(hdc, GetStockObject(NULL_PEN));
    RoundRect(hdc, btn.left, btn.top, btn.right, btn.bottom, 8, 8);
    SelectObject(hdc, ob);
    SelectObject(hdc, op2);
    DeleteObject(bb);
    SetTextColor(hdc, RGB(28, 28, 28));
    DrawTextW(hdc, L"잠금 해제", -1, &btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT newBtn{}, menuBtn{}, closeBtn{};
    GetHeaderButtons(newBtn, menuBtn, closeBtn);
    DrawHeaderButton(hdc, closeBtn, L"✕", hotHit_ == HIT_CLOSE);

    HPEN pen = CreatePen(PS_SOLID, 1, DarkUI::kBorder);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, 0, 0, rc.right, rc.bottom);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    return;
  }

  RECT toolbar{rc.left, rc.bottom - kToolbarH, rc.right, rc.bottom};
  HBRUSH tb = CreateSolidBrush(DarkUI::kToolbar);
  FillRect(hdc, &toolbar, tb);
  DeleteObject(tb);

  HPEN pen = CreatePen(PS_SOLID, 1, DarkUI::kBorder);
  HGDIOBJ oldP = SelectObject(hdc, pen);
  MoveToEx(hdc, 0, toolbar.top, nullptr);
  LineTo(hdc, rc.right, toolbar.top);
  SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, 0, 0, rc.right, rc.bottom);
  SelectObject(hdc, oldP);
  DeleteObject(pen);

  RECT newBtn{}, menuBtn{}, closeBtn{};
  GetHeaderButtons(newBtn, menuBtn, closeBtn);
  DrawHeaderButton(hdc, newBtn, L"+", hotHit_ == HIT_NEW);
  DrawHeaderButton(hdc, menuBtn, L"···", hotHit_ == HIT_MENU);
  DrawHeaderButton(hdc, closeBtn, L"✕", hotHit_ == HIT_CLOSE);

  const wchar_t* tools[] = {L"B", L"I", L"U", L"ab", L"≡", L"🖼"};
  RECT tr[6];
  GetToolbarRects(tr);
  SelectObject(hdc, toolFont_ ? toolFont_ : GetStockObject(DEFAULT_GUI_FONT));
  for (int i = 0; i < 6; ++i) {
    if (hotTool_ == i) {
      HBRUSH hb = CreateSolidBrush(DarkUI::kSurface);
      FillRect(hdc, &tr[i], hb);
      DeleteObject(hb);
    }
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, DarkUI::kMuted);
    DrawTextW(hdc, tools[i], -1, &tr[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

int NoteWindow::HitTestChrome(int x, int y) const {
  RECT newBtn{}, menuBtn{}, closeBtn{};
  GetHeaderButtons(newBtn, menuBtn, closeBtn);
  POINT pt{x, y};
  if (PtInRect(&newBtn, pt)) return HIT_NEW;
  if (PtInRect(&menuBtn, pt)) return HIT_MENU;
  if (PtInRect(&closeBtn, pt)) return HIT_CLOSE;
  if (y < kHeaderH) return HIT_DRAG;
  return HIT_NONE;
}

int NoteWindow::HitTestToolbar(int x, int y) const {
  RECT tr[6];
  GetToolbarRects(tr);
  POINT pt{x, y};
  for (int i = 0; i < 6; ++i) {
    if (PtInRect(&tr[i], pt)) return i;
  }
  return -1;
}

void NoteWindow::ApplyCharFormat(DWORD mask, DWORD effects, bool toggle) {
  if (!edit_) return;
  CHARFORMAT2W cf{};
  cf.cbSize = sizeof(cf);
  SendMessageW(edit_, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
  cf.dwMask = mask;
  if (toggle) {
    if (cf.dwEffects & effects) cf.dwEffects &= ~effects;
    else cf.dwEffects |= effects;
  } else {
    cf.dwEffects |= effects;
  }
  // Ensure color stays readable
  cf.dwMask |= CFM_COLOR;
  cf.crTextColor = DarkUI::kText;
  SendMessageW(edit_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
  SetFocus(edit_);
}

void NoteWindow::ApplyBullet() {
  if (!edit_) return;
  PARAFORMAT2 pf{};
  pf.cbSize = sizeof(pf);
  SendMessageW(edit_, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
  pf.dwMask = PFM_NUMBERING | PFM_OFFSET;
  if (pf.wNumbering == PFN_BULLET) {
    pf.wNumbering = 0;
    pf.dxOffset = 0;
  } else {
    pf.wNumbering = PFN_BULLET;
    pf.dxOffset = 200;
  }
  SendMessageW(edit_, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
  SetFocus(edit_);
}

void NoteWindow::InsertImage() {
  if (!edit_) return;
  wchar_t file[MAX_PATH] = L"";
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd_;
  ofn.lpstrFilter = L"이미지\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0모든 파일\0*.*\0";
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameW(&ofn)) return;

  // Insert file path as a visible placeholder line (OLE image needs more deps);
  // Prefer paste bitmap via clipboard if LoadImage works
  HBITMAP bmp = static_cast<HBITMAP>(LoadImageW(
      nullptr, file, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION));
  if (bmp) {
    if (OpenClipboard(hwnd_)) {
      EmptyClipboard();
      SetClipboardData(CF_BITMAP, bmp);
      CloseClipboard();
      SendMessageW(edit_, WM_PASTE, 0, 0);
      // clipboard owns bitmap if SetClipboardData succeeds — don't DeleteObject
      bmp = nullptr;
    }
    if (bmp) DeleteObject(bmp);
  } else {
    // non-bmp: insert path reference text
    std::wstring tag = L"\n[이미지: ";
    tag += file;
    tag += L"]\n";
    SendMessageW(edit_, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(tag.c_str()));
  }
  NotifyChanged();
  SetFocus(edit_);
}

void NoteWindow::ShowContextMenu() {
  POINT pt;
  GetCursorPos(&pt);
  HMENU menu = CreatePopupMenu();
  HMENU color = CreatePopupMenu();
  AppendMenuW(color, MF_STRING, IDM_COLOR_YELLOW, L"노랑");
  AppendMenuW(color, MF_STRING, IDM_COLOR_PINK, L"분홍");
  AppendMenuW(color, MF_STRING, IDM_COLOR_BLUE, L"파랑");
  AppendMenuW(color, MF_STRING, IDM_COLOR_GREEN, L"초록");
  AppendMenuW(color, MF_STRING, IDM_COLOR_PURPLE, L"보라");
  AppendMenuW(color, MF_STRING, IDM_COLOR_GRAY, L"회색");

  AppendMenuW(menu, MF_STRING, IDM_SHOW_LIST, L"노트 목록");
  AppendMenuW(menu, MF_STRING, IDM_NEW_NOTE, L"새 메모");
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(color), L"색상");
  AppendMenuW(menu, MF_STRING | (note_.alwaysOnTop ? MF_CHECKED : 0),
              IDM_ALWAYS_ON_TOP, L"항상 위");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_LOCK, L"잠금");
  AppendMenuW(menu, MF_STRING, IDM_CHANGE_PASSWORD, L"비밀번호 변경");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_DELETE_NOTE, L"메모 삭제");
  AppendMenuW(menu, MF_STRING, IDM_EXIT, L"종료");

  SetForegroundWindow(hwnd_);
  const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
  DestroyMenu(menu);
  if (!cmd) return;
  if (cmd == IDM_COLOR_YELLOW) SetColor(L"yellow");
  else if (cmd == IDM_COLOR_PINK) SetColor(L"pink");
  else if (cmd == IDM_COLOR_BLUE) SetColor(L"blue");
  else if (cmd == IDM_COLOR_GREEN) SetColor(L"green");
  else if (cmd == IDM_COLOR_PURPLE) SetColor(L"purple");
  else if (cmd == IDM_COLOR_GRAY) SetColor(L"gray");
  else if (cmd == IDM_ALWAYS_ON_TOP) SetAlwaysOnTop(!note_.alwaysOnTop);
  else if (onCommand_) onCommand_(this, cmd);
}

void NoteWindow::NotifyChanged() {
  dirty_ = true;
  if (onChanged_) onChanged_(this);
}

void NoteWindow::ApplyFromNote() {
  if (!hwnd_) return;
  ClampRectToWorkArea(note_.x, note_.y, note_.width, note_.height);
  SetWindowPos(hwnd_, note_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
               note_.x, note_.y, note_.width, note_.height, SWP_NOACTIVATE);
  if (!note_.rtf.empty()) {
    LoadRtfOrText(note_.rtf);
    emptyDoc_ = note_.content.empty();
  } else if (!note_.content.empty()) {
    LoadRtfOrText(note_.content);
    emptyDoc_ = false;
  } else {
    SetWindowTextW(edit_, L"");
    emptyDoc_ = true;
  }
  RebuildBrushes();
  ApplyChrome();
  InvalidateRect(hwnd_, nullptr, TRUE);
  Show(note_.visible);
}

void NoteWindow::SyncToNote() {
  if (!hwnd_) return;
  RECT wr;
  GetWindowRect(hwnd_, &wr);
  note_.x = wr.left;
  note_.y = wr.top;
  note_.width = wr.right - wr.left;
  note_.height = wr.bottom - wr.top;

  // Persist open/closed so reboot + unlock restores the same windows
  // Session-locked notes that are still on screen count as open.
  if (sessionLocked_) {
    note_.visible = (IsWindowVisible(hwnd_) != FALSE);
  } else {
    note_.visible = (IsWindowVisible(hwnd_) != FALSE);
  }

  // When locked, content was wiped — do not overwrite vault body from empty UI
  if (sessionLocked_) {
    note_.updatedAt = NowTimestamp();
    return;
  }

  // Plain text for list preview / search (RichEdit GETTEXT is plain)
  std::wstring plain;
  if (edit_) {
    int len = GetWindowTextLengthW(edit_);
    plain.assign(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(edit_, plain.data(), len + 1);
    plain.resize(static_cast<size_t>(len));
  }

  // Trim whitespace-only → treat as empty
  while (!plain.empty() && (plain.back() == L' ' || plain.back() == L'\r' ||
                            plain.back() == L'\n' || plain.back() == L'\t'))
    plain.pop_back();
  while (!plain.empty() && (plain.front() == L' ' || plain.front() == L'\r' ||
                            plain.front() == L'\n' || plain.front() == L'\t'))
    plain.erase(plain.begin());

  if (plain.empty() || IsJunkPreviewText(plain)) {
    note_.content.clear();
    note_.rtf.clear();
    emptyDoc_ = true;
  } else {
    note_.content = plain;
    note_.rtf = SaveRtf();
    // If RTF path failed and stored junk, keep plain only
    if (IsJunkPreviewText(PlainFromRtf(note_.rtf)) && plain.size() < 4) {
      note_.rtf.clear();
    }
    emptyDoc_ = false;
  }
  note_.updatedAt = NowTimestamp();
}

void NoteWindow::SetColor(const std::wstring& color) {
  note_.color = color;
  RebuildBrushes();
  ApplyChrome();
  if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
  NotifyChanged();
}

void NoteWindow::SetAlwaysOnTop(bool on) {
  note_.alwaysOnTop = on;
  if (hwnd_) {
    SetWindowPos(hwnd_, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  NotifyChanged();
}

void NoteWindow::Show(bool show) {
  note_.visible = show;
  if (!hwnd_) return;
  if (show) {
    ShowWindow(hwnd_, SW_SHOW);
    ShowWindow(hwnd_, SW_RESTORE);
    UpdateWindow(hwnd_);
  } else {
    ShowWindow(hwnd_, SW_HIDE);
  }
}

void NoteWindow::ClearSensitive() {
  if (edit_) SetWindowTextW(edit_, L"");
  SecureWipeString(note_.content);
  SecureWipeString(note_.rtf);
  emptyDoc_ = true;
}

void NoteWindow::FocusEdit() {
  if (hwnd_) {
    Show(true);
    SetForegroundWindow(hwnd_);
    BringWindowToTop(hwnd_);
  }
  if (sessionLocked_) return;
  if (edit_) SetFocus(edit_);
}

LRESULT CALLBACK NoteWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  NoteWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = reinterpret_cast<NoteWindow*>(cs->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<NoteWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self) return self->HandleMessage(msg, wParam, lParam);
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT NoteWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_NCCALCSIZE:
      if (wParam == TRUE) return 0;
      break;

    case WM_NCHITTEST: {
      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      POINT cpt = pt;
      ScreenToClient(hwnd_, &cpt);
      LRESULT edge = HitTestBorderless(hwnd_, lParam);
      if (edge != HTCLIENT) return edge;
      int ch = HitTestChrome(cpt.x, cpt.y);
      if (ch == HIT_DRAG) return HTCAPTION;
      if (ch == HIT_NEW || ch == HIT_MENU || ch == HIT_CLOSE) return HTCLIENT;
      if (HitTestToolbar(cpt.x, cpt.y) >= 0) return HTCLIENT;
      return HTCLIENT;
    }

    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
      mmi->ptMinTrackSize.x = kNoteMinW;
      mmi->ptMinTrackSize.y = kNoteMinH;
      return 0;
    }

    case WM_SIZE:
      Layout();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd_, &ps);
      Paint(hdc);
      EndPaint(hwnd_, &ps);
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_MOUSEMOVE: {
      int x = GET_X_LPARAM(lParam);
      int y = GET_Y_LPARAM(lParam);
      if (sessionLocked_) {
        RECT btn = UnlockButtonRect();
        POINT pt{x, y};
        bool hot = PtInRect(&btn, pt) != FALSE;
        int ch = HitTestChrome(x, y);
        if (hot != unlockBtnHot_ || ch != hotHit_) {
          unlockBtnHot_ = hot;
          hotHit_ = ch;
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        if (!trackingLeave_) {
          TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
          TrackMouseEvent(&tme);
          trackingLeave_ = true;
        }
        return 0;
      }
      int ch = HitTestChrome(x, y);
      int tool = HitTestToolbar(x, y);
      if (ch != hotHit_ || tool != hotTool_) {
        hotHit_ = ch;
        hotTool_ = tool;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      if (!trackingLeave_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        trackingLeave_ = true;
      }
      return 0;
    }

    case WM_MOUSELEAVE:
      trackingLeave_ = false;
      unlockBtnHot_ = false;
      hotHit_ = HIT_NONE;
      hotTool_ = -1;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;

    case WM_LBUTTONUP: {
      int x = GET_X_LPARAM(lParam);
      int y = GET_Y_LPARAM(lParam);
      if (sessionLocked_) {
        int ch = HitTestChrome(x, y);
        if (ch == HIT_CLOSE) {
          Show(false);
          note_.visible = false;
          return 0;
        }
        RECT btn = UnlockButtonRect();
        POINT pt{x, y};
        if (PtInRect(&btn, pt) || y > kHeaderH) {
          if (onUnlock_) onUnlock_();
          else if (onCommand_) onCommand_(this, IDM_UNLOCK);
        }
        return 0;
      }
      int ch = HitTestChrome(x, y);
      if (ch == HIT_NEW) {
        if (onCommand_) onCommand_(this, IDM_NEW_NOTE);
        return 0;
      }
      if (ch == HIT_MENU) {
        ShowContextMenu();
        return 0;
      }
      if (ch == HIT_CLOSE) {
        SyncToNote();
        Show(false);
        NotifyChanged();
        if (onCommand_) onCommand_(this, IDM_SHOW_LIST);
        return 0;
      }
      int tool = HitTestToolbar(x, y);
      if (tool == 0) ApplyCharFormat(CFM_BOLD, CFE_BOLD, true);
      else if (tool == 1) ApplyCharFormat(CFM_ITALIC, CFE_ITALIC, true);
      else if (tool == 2) ApplyCharFormat(CFM_UNDERLINE, CFE_UNDERLINE, true);
      else if (tool == 3) ApplyCharFormat(CFM_STRIKEOUT, CFE_STRIKEOUT, true);
      else if (tool == 4) ApplyBullet();
      else if (tool == 5) InsertImage();
      if (tool >= 0) {
        NotifyChanged();
        return 0;
      }
      break;
    }

    case WM_COMMAND: {
      if (HIWORD(wParam) == EN_CHANGE) {
        dirty_ = true;
        emptyDoc_ = false;
        KillTimer(hwnd_, IDT_SAVE_DEBOUNCE);
        SetTimer(hwnd_, IDT_SAVE_DEBOUNCE, 800, nullptr);
        return 0;
      }
      break;
    }

    case WM_NOTIFY: {
      auto* nm = reinterpret_cast<NMHDR*>(lParam);
      if (nm && nm->hwndFrom == edit_ && nm->code == EN_CHANGE) {
        dirty_ = true;
        KillTimer(hwnd_, IDT_SAVE_DEBOUNCE);
        SetTimer(hwnd_, IDT_SAVE_DEBOUNCE, 800, nullptr);
        return 0;
      }
      break;
    }

    case WM_TIMER:
      if (wParam == IDT_SAVE_DEBOUNCE) {
        KillTimer(hwnd_, IDT_SAVE_DEBOUNCE);
        if (dirty_ && onChanged_) {
          SyncToNote();
          onChanged_(this);
          dirty_ = false;
        }
        return 0;
      }
      break;

    case WM_EXITSIZEMOVE:
      SyncToNote();
      ClampRectToWorkArea(note_.x, note_.y, note_.width, note_.height);
      SetWindowPos(hwnd_, nullptr, note_.x, note_.y, note_.width, note_.height,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      // Always notify so vault gets latest position (even before lock)
      if (onChanged_ && !sessionLocked_) onChanged_(this);
      return 0;

    case WM_CLOSE:
      if (!sessionLocked_) SyncToNote();
      Show(false);
      note_.visible = false;
      if (onChanged_ && !sessionLocked_) onChanged_(this);
      if (!sessionLocked_ && onCommand_) onCommand_(this, IDM_SHOW_LIST);
      return 0;

    case WM_DESTROY:
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
      hwnd_ = nullptr;
      edit_ = nullptr;
      return 0;
  }
  return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

}  // namespace securememo
