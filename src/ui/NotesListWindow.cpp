#include "ui/NotesListWindow.h"

#include "ui/Theme.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
// for DrawPadlockIcon scale

namespace securememo {
namespace {
constexpr int kListTopUnlocked = 108;
constexpr int kListTopLocked = 96;
constexpr int kLockedItemH = 72;
constexpr int kUnlockedItemH = 88;
}

NotesListWindow::NotesListWindow() = default;

NotesListWindow::~NotesListWindow() {
  Destroy();
}

bool NotesListWindow::RegisterClass(HINSTANCE inst) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_DROPSHADOW;
  wc.lpfnWndProc = NotesListWindow::WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kListClass;
  return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void NotesListWindow::EnsureFonts() {
  if (!titleFont_)
    titleFont_ = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  if (!uiFont_)
    uiFont_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  if (!smallFont_)
    smallFont_ = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  if (!bgBrush_) bgBrush_ = CreateSolidBrush(DarkUI::kBg);
  if (!searchBrush_) searchBrush_ = CreateSolidBrush(DarkUI::kSearchBg);
}

bool NotesListWindow::Create(HINSTANCE inst, HWND appHwnd) {
  inst_ = inst;
  appHwnd_ = appHwnd;
  EnsureFonts();

  int x, y, w, h;
  DefaultListPlacement(x, y, w, h);
  // Caller may SetPlacement after Create; use defaults here

  hwnd_ = CreateWindowExW(
      WS_EX_APPWINDOW | WS_EX_TOOLWINDOW, kListClass, kAppTitle,
      WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE,
      x, y, w, h, nullptr, nullptr, inst, this);
  if (!hwnd_) return false;

  ApplyWin11Chrome(hwnd_, DarkUI::kBorder);

  // Dark search field — no white system fill
  search_ = CreateWindowExW(
      0, L"EDIT", L"",
      WS_CHILD | ES_AUTOHSCROLL,
      0, 0, 100, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(3001)),
      inst, nullptr);
  SendMessageW(search_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
  SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"검색..."));
  SetWindowTheme(search_, L"DarkMode_Explorer", nullptr);

  Layout();
  // default locked: hide search
  if (search_) ShowWindow(search_, SW_HIDE);
  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);
  return true;
}

void NotesListWindow::Destroy() {
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    search_ = nullptr;
  }
  if (titleFont_) { DeleteObject(titleFont_); titleFont_ = nullptr; }
  if (uiFont_) { DeleteObject(uiFont_); uiFont_ = nullptr; }
  if (smallFont_) { DeleteObject(smallFont_); smallFont_ = nullptr; }
  if (bgBrush_) { DeleteObject(bgBrush_); bgBrush_ = nullptr; }
  if (searchBrush_) { DeleteObject(searchBrush_); searchBrush_ = nullptr; }
}

void NotesListWindow::Show(bool show) {
  if (!hwnd_) return;
  ShowWindow(hwnd_, show ? SW_SHOW : SW_HIDE);
  if (show) {
    ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
    BringWindowToTop(hwnd_);
  }
}

bool NotesListWindow::IsVisible() const {
  return hwnd_ && IsWindowVisible(hwnd_);
}

void NotesListWindow::GetPlacement(int& x, int& y, int& w, int& h) const {
  if (!hwnd_) {
    DefaultListPlacement(x, y, w, h);
    return;
  }
  RECT wr{};
  GetWindowRect(hwnd_, &wr);
  x = wr.left;
  y = wr.top;
  w = wr.right - wr.left;
  h = wr.bottom - wr.top;
}

void NotesListWindow::SetPlacement(int x, int y, int w, int h) {
  if (w < 300) w = kListDefaultW;
  if (h < 360) h = kListDefaultH;
  ClampRectToWorkArea(x, y, w, h);
  if (hwnd_) {
    SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

void NotesListWindow::SetLocked(bool locked, int lockedCount) {
  locked_ = locked;
  lockedCount_ = lockedCount;
  scrollY_ = 0;
  unlockBtnHot_ = false;
  if (search_) {
    if (locked) {
      ShowWindow(search_, SW_HIDE);
      SetWindowTextW(search_, L"");
    } else {
      ShowWindow(search_, SW_SHOW);
      EnableWindow(search_, TRUE);
      LONG_PTR style = GetWindowLongPtrW(search_, GWL_STYLE);
      style &= ~ES_READONLY;
      SetWindowLongPtrW(search_, GWL_STYLE, style);
      SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"검색..."));
    }
  }
  // Keep note metadata empty while locked (content not in memory)
  if (locked) {
    all_.clear();
    filtered_.clear();
  }
  Layout();
  if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void NotesListWindow::SetNotes(const std::vector<Note>& notes) {
  all_ = notes;
  std::sort(all_.begin(), all_.end(), [](const Note& a, const Note& b) {
    return a.updatedAt > b.updatedAt;
  });
  locked_ = false;
  lockedCount_ = static_cast<int>(all_.size());
  if (search_) {
    ShowWindow(search_, SW_SHOW);
    EnableWindow(search_, TRUE);
    LONG_PTR style = GetWindowLongPtrW(search_, GWL_STYLE);
    style &= ~ES_READONLY;
    SetWindowLongPtrW(search_, GWL_STYLE, style);
    SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"검색..."));
  }
  FilterNotes();
  Layout();
  if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void NotesListWindow::Refresh() {
  FilterNotes();
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NotesListWindow::FilterNotes() {
  filtered_.clear();
  if (locked_) return;
  std::wstring q = filter_;
  for (auto& c : q) c = static_cast<wchar_t>(towlower(c));
  for (int i = 0; i < static_cast<int>(all_.size()); ++i) {
    if (q.empty()) {
      filtered_.push_back(i);
      continue;
    }
    std::wstring content = PreviewLine(all_[i].content, 500);
    for (auto& c : content) c = static_cast<wchar_t>(towlower(c));
    if (content.find(q) != std::wstring::npos) filtered_.push_back(i);
  }
  scrollY_ = 0;
}

void NotesListWindow::OnSearchChanged() {
  if (!search_ || locked_) return;
  int len = GetWindowTextLengthW(search_);
  std::wstring t(static_cast<size_t>(len) + 1, L'\0');
  GetWindowTextW(search_, t.data(), len + 1);
  t.resize(static_cast<size_t>(len));
  filter_ = std::move(t);
  FilterNotes();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void NotesListWindow::GetHeaderButtons(RECT& newBtn, RECT& settingsBtn, RECT& closeBtn) const {
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const int y = 10;
  closeBtn = {rc.right - 12 - kBtnSize, y, rc.right - 12, y + kBtnSize};
  settingsBtn = {closeBtn.left - 4 - kBtnSize, y, closeBtn.left - 4, y + kBtnSize};
  newBtn = {12, y, 12 + kBtnSize, y + kBtnSize};
}

RECT NotesListWindow::ListArea() const {
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  rc.top = locked_ ? kListTopLocked : kListTopUnlocked;
  rc.left = 12;
  rc.right -= 12;
  rc.bottom -= 12;
  return rc;
}

void NotesListWindow::Layout() {
  if (!hwnd_ || !search_) return;
  RECT rc;
  GetClientRect(hwnd_, &rc);
  // Search only when unlocked — sits under title
  MoveWindow(search_, 16, 68, rc.right - 32, 30, TRUE);
  if (locked_) ShowWindow(search_, SW_HIDE);
  else ShowWindow(search_, SW_SHOW);
}

int NotesListWindow::HitTestHeader(int x, int y) const {
  RECT n{}, s{}, c{};
  GetHeaderButtons(n, s, c);
  POINT pt{x, y};
  if (PtInRect(&n, pt)) return HIT_NEW;
  if (PtInRect(&s, pt)) return HIT_SETTINGS;
  if (PtInRect(&c, pt)) return HIT_CLOSE;
  if (y < 48) return HIT_DRAG;
  return HIT_NONE;
}

int NotesListWindow::HitTestItem(int x, int y) const {
  if (locked_) return -1;  // overlay only — no item rows
  RECT area = ListArea();
  POINT pt{x, y};
  if (!PtInRect(&area, pt)) return -1;
  const int itemH = kUnlockedItemH;
  const int relY = y - area.top + scrollY_;
  if (relY < 0) return -1;
  const int idx = relY / itemH;
  if (idx < 0 || idx >= static_cast<int>(filtered_.size())) return -1;
  return idx;
}

void NotesListWindow::ScrollBy(int dy) {
  int count = locked_ ? (lockedCount_ > 0 ? lockedCount_ : 1)
                      : static_cast<int>(filtered_.size());
  const int itemH = locked_ ? kLockedItemH : kUnlockedItemH;
  const int contentH = count * itemH + (locked_ ? 36 : 0);
  RECT area = ListArea();
  const int viewH = area.bottom - area.top;
  int maxScroll = (std::max)(0, contentH - viewH);
  scrollY_ += dy;
  if (scrollY_ < 0) scrollY_ = 0;
  if (scrollY_ > maxScroll) scrollY_ = maxScroll;
  // Only list region needs redraw on scroll
  InvalidateRect(hwnd_, &area, FALSE);
}

RECT NotesListWindow::ItemScreenRect(int index) const {
  RECT area = ListArea();
  const int itemH = locked_ ? kLockedItemH : kUnlockedItemH;
  int topBase = area.top;
  if (locked_) topBase += 40;
  RECT r{area.left, topBase + index * itemH - scrollY_, area.right,
         topBase + index * itemH - scrollY_ + itemH};
  return r;
}

RECT NotesListWindow::HeaderButtonsUnion() const {
  RECT n{}, s{}, c{};
  GetHeaderButtons(n, s, c);
  RECT u = n;
  UnionRect(&u, &u, &s);
  UnionRect(&u, &u, &c);
  InflateRect(&u, 2, 2);
  return u;
}

void NotesListWindow::InvalidateHoverChange(int oldHeader, int newHeader,
                                           int oldItem, int newItem) {
  if (oldHeader != newHeader) {
    RECT hr = HeaderButtonsUnion();
    InvalidateRect(hwnd_, &hr, FALSE);
  }
  if (oldItem != newItem) {
    if (oldItem >= 0) {
      RECT r = ItemScreenRect(oldItem);
      InvalidateRect(hwnd_, &r, FALSE);
    }
    if (newItem >= 0) {
      RECT r = ItemScreenRect(newItem);
      InvalidateRect(hwnd_, &r, FALSE);
    }
  }
}

RECT NotesListWindow::ListUnlockButtonRect() const {
  RECT area = ListArea();
  const int bw = 168;
  const int bh = 40;
  const int cx = (area.left + area.right) / 2;
  // Fixed offset from vertical center — leave room above for icon + texts
  const int midY = (area.top + area.bottom) / 2;
  const int top = midY + 70;
  return {cx - bw / 2, top, cx + bw / 2, top + bh};
}

// Classic padlock icon (shackle + body + keyhole) — not a purse shape
static void DrawPadlockIcon(HDC hdc, int cx, int cy, int scale) {
  const int s = scale;
  HPEN shacklePen = CreatePen(PS_SOLID, (std::max)(2, s / 5), DarkUI::kLockGold);
  HBRUSH bodyBr = CreateSolidBrush(DarkUI::kLockGold);
  HGDIOBJ oldP = SelectObject(hdc, shacklePen);
  HGDIOBJ oldB = SelectObject(hdc, GetStockObject(NULL_BRUSH));

  // Shackle: open-bottom U using arc (top half circle + sides)
  // Outer shackle arc
  const int shL = cx - s;
  const int shR = cx + s;
  const int shT = cy - s * 2;
  const int shB = cy + s / 3;
  Arc(hdc, shL, shT, shR, shB, shR, cy - s / 4, shL, cy - s / 4);
  // Inner cutout by redrawing dark arc slightly smaller (optional thickness via pen width)

  // Body
  SelectObject(hdc, bodyBr);
  SelectObject(hdc, GetStockObject(NULL_PEN));
  const int bodyTop = cy - s / 5;
  RoundRect(hdc, cx - s - 2, bodyTop, cx + s + 2, cy + s + 6, s / 2, s / 2);

  // Keyhole
  HBRUSH hole = CreateSolidBrush(RGB(36, 36, 36));
  SelectObject(hdc, hole);
  const int kx = cx;
  const int ky = bodyTop + (cy + s + 6 - bodyTop) / 2 - 2;
  Ellipse(hdc, kx - s / 5, ky - s / 4, kx + s / 5, ky + s / 8);
  POINT tri[3] = {
      {kx - s / 7, ky},
      {kx + s / 7, ky},
      {kx, ky + s / 2},
  };
  Polygon(hdc, tri, 3);

  SelectObject(hdc, oldB);
  SelectObject(hdc, oldP);
  DeleteObject(shacklePen);
  DeleteObject(bodyBr);
  DeleteObject(hole);
}

void NotesListWindow::PaintLockOverlay(HDC hdc, const RECT& area) {
  HBRUSH panel = CreateSolidBrush(RGB(32, 32, 32));
  FillRect(hdc, &area, panel);
  DeleteObject(panel);

  const int cx = (area.left + area.right) / 2;
  const int midY = (area.top + area.bottom) / 2;

  // Vertical stack with generous gaps (no overlap)
  const int iconCy = midY - 56;
  DrawPadlockIcon(hdc, cx, iconCy, 22);

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, DarkUI::kText);
  SelectObject(hdc, titleFont_ ? titleFont_ : uiFont_);
  RECT t1{area.left + 24, midY - 8, area.right - 24, midY + 24};
  DrawTextW(hdc, L"보안 메모가 잠겨 있습니다", -1, &t1,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

  SetTextColor(hdc, DarkUI::kMuted);
  SelectObject(hdc, smallFont_ ? smallFont_ : uiFont_);
  RECT t2{area.left + 28, midY + 28, area.right - 28, midY + 56};
  DrawTextW(hdc, L"비밀번호를 입력하면 목록과 메모를 사용할 수 있습니다", -1, &t2,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

  RECT btn = ListUnlockButtonRect();
  HBRUSH bb = CreateSolidBrush(unlockBtnHot_ ? RGB(255, 220, 90) : DarkUI::kLockGold);
  HGDIOBJ ob = SelectObject(hdc, bb);
  HGDIOBJ op = SelectObject(hdc, GetStockObject(NULL_PEN));
  RoundRect(hdc, btn.left, btn.top, btn.right, btn.bottom, 10, 10);
  SelectObject(hdc, ob);
  SelectObject(hdc, op);
  DeleteObject(bb);

  SetTextColor(hdc, RGB(28, 28, 28));
  SelectObject(hdc, uiFont_);
  DrawTextW(hdc, L"잠금 해제", -1, &btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void NotesListWindow::PaintItem(HDC hdc, const RECT& rc, const Note& n, bool hot) {
  auto th = ThemeFor(n.color);
  COLORREF card = hot ? DarkUI::kCardHover : DarkUI::kCard;
  HBRUSH br = CreateSolidBrush(card);
  FillRect(hdc, &rc, br);
  DeleteObject(br);

  RECT accent{rc.left, rc.top, rc.left + 4, rc.bottom};
  HBRUSH abr = CreateSolidBrush(th.accent);
  FillRect(hdc, &accent, abr);
  DeleteObject(abr);

  HPEN pen = CreatePen(PS_SOLID, 1, DarkUI::kBorder);
  HGDIOBJ oldP = SelectObject(hdc, pen);
  MoveToEx(hdc, rc.left + 4, rc.bottom - 1, nullptr);
  LineTo(hdc, rc.right, rc.bottom - 1);
  SelectObject(hdc, oldP);
  DeleteObject(pen);

  SetBkMode(hdc, TRANSPARENT);
  RECT textRc = rc;
  textRc.left += 14;
  textRc.right -= 72;
  textRc.top += 12;
  textRc.bottom -= 12;

  // Always prefer plain content; never show RTF header junk in the list
  std::wstring plain = n.content;
  if (plain.empty() && !n.rtf.empty()) plain = PlainFromRtf(n.rtf);
  else if (plain.size() >= 5 && plain.rfind(L"{\\rtf", 0) == 0) plain = PlainFromRtf(plain);
  std::wstring preview = PreviewLine(plain, 90);
  const bool empty = preview == kPlaceholderText;
  SetTextColor(hdc, empty ? DarkUI::kPlaceholder : DarkUI::kText);
  SelectObject(hdc, uiFont_);
  DrawTextW(hdc, preview.c_str(), -1, &textRc,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);

  RECT dateRc = rc;
  dateRc.left = rc.right - 70;
  dateRc.right -= 10;
  dateRc.top += 12;
  dateRc.bottom = dateRc.top + 20;
  SetTextColor(hdc, DarkUI::kMuted);
  SelectObject(hdc, smallFont_);
  std::wstring date = FormatListDate(n.updatedAt);
  DrawTextW(hdc, date.c_str(), -1, &dateRc, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

void NotesListWindow::Paint(HDC hdc) {
  RECT rc;
  GetClientRect(hwnd_, &rc);
  FillRect(hdc, &rc, bgBrush_);

  HPEN border = CreatePen(PS_SOLID, 1, DarkUI::kBorder);
  HGDIOBJ oldP = SelectObject(hdc, border);
  SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, 0, 0, rc.right, rc.bottom);
  SelectObject(hdc, oldP);
  DeleteObject(border);

  RECT newBtn{}, setBtn{}, closeBtn{};
  GetHeaderButtons(newBtn, setBtn, closeBtn);

  auto drawBtn = [&](const RECT& r, const wchar_t* g, bool hot) {
    if (hot) {
      HBRUSH hb = CreateSolidBrush(DarkUI::kSurface);
      FillRect(hdc, &r, hb);
      DeleteObject(hb);
    }
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, DarkUI::kText);
    SelectObject(hdc, uiFont_);
    DrawTextW(hdc, g, -1, const_cast<RECT*>(&r), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  };
  drawBtn(newBtn, L"+", hotHeader_ == HIT_NEW);
  drawBtn(setBtn, L"⚙", hotHeader_ == HIT_SETTINGS);
  drawBtn(closeBtn, L"✕", hotHeader_ == HIT_CLOSE);

  RECT title{16, 40, rc.right - 80, 66};
  SetTextColor(hdc, DarkUI::kText);
  SelectObject(hdc, titleFont_);
  DrawTextW(hdc, kAppTitle, -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  if (locked_) {
    PaintLockOverlay(hdc, ListArea());
    return;
  }

  // unlocked: draw dark search chrome around edit
  {
    RECT sbox{14, 66, rc.right - 14, 100};
    HBRUSH sb = CreateSolidBrush(DarkUI::kSearchBg);
    FillRect(hdc, &sbox, sb);
    DeleteObject(sb);
    HPEN sp = CreatePen(PS_SOLID, 1, DarkUI::kBorder);
    HGDIOBJ op = SelectObject(hdc, sp);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, sbox.left, sbox.top, sbox.right, sbox.bottom);
    SelectObject(hdc, op);
    DeleteObject(sp);
  }

  RECT area = ListArea();
  IntersectClipRect(hdc, area.left, area.top, area.right, area.bottom);
  int y = area.top - scrollY_;
  for (int i = 0; i < static_cast<int>(filtered_.size()); ++i) {
    RECT item{area.left, y, area.right, y + kUnlockedItemH - 4};
    if (item.bottom > area.top && item.top < area.bottom) {
      PaintItem(hdc, item, all_[filtered_[i]], hotItem_ == i);
    }
    y += kUnlockedItemH;
  }
  if (filtered_.empty()) {
    SelectClipRgn(hdc, nullptr);
    SetTextColor(hdc, DarkUI::kMuted);
    SelectObject(hdc, uiFont_);
    DrawTextW(hdc, L"메모가 없습니다. + 로 새 메모를 만드세요.", -1, &area,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

LRESULT CALLBACK NotesListWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  NotesListWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = reinterpret_cast<NotesListWindow*>(cs->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<NotesListWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self) return self->HandleMessage(msg, wParam, lParam);
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT NotesListWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
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
      int h = HitTestHeader(cpt.x, cpt.y);
      if (h == HIT_DRAG) return HTCAPTION;
      return HTCLIENT;
    }

    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
      mmi->ptMinTrackSize.x = 300;
      mmi->ptMinTrackSize.y = 360;
      return 0;
    }

    case WM_SIZE:
      Layout();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;

    case WM_EXITSIZEMOVE:
      if (onPlacement_) onPlacement_();
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd_, &ps);
      RECT rc;
      GetClientRect(hwnd_, &rc);
      // Double-buffer to eliminate hover flicker
      const int w = rc.right - rc.left;
      const int h = rc.bottom - rc.top;
      HDC mem = CreateCompatibleDC(hdc);
      HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
      HGDIOBJ old = SelectObject(mem, bmp);
      Paint(mem);
      BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
      DeleteObject(bmp);
      DeleteDC(mem);
      EndPaint(hwnd_, &ps);
      return 0;
    }

    case WM_ERASEBKGND:
      // No background erase — prevents white flash; paint does full fill
      return 1;

    case WM_CTLCOLOREDIT: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      SetBkColor(hdc, DarkUI::kSearchBg);
      SetTextColor(hdc, DarkUI::kText);
      SetBkMode(hdc, OPAQUE);
      return reinterpret_cast<LRESULT>(searchBrush_);
    }

    case WM_MOUSEWHEEL:
      ScrollBy(-GET_WHEEL_DELTA_WPARAM(wParam) / 2);
      return 0;

    case WM_MOUSEMOVE: {
      int x = GET_X_LPARAM(lParam);
      int y = GET_Y_LPARAM(lParam);
      if (locked_) {
        RECT btn = ListUnlockButtonRect();
        POINT pt{x, y};
        bool hot = PtInRect(&btn, pt) != FALSE;
        int hh = HitTestHeader(x, y);
        if (hot != unlockBtnHot_ || hh != hotHeader_) {
          unlockBtnHot_ = hot;
          hotHeader_ = hh;
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        if (!trackingLeave_) {
          TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
          TrackMouseEvent(&tme);
          trackingLeave_ = true;
        }
        return 0;
      }
      int hh = HitTestHeader(x, y);
      int item = HitTestItem(x, y);
      if (hh != hotHeader_ || item != hotItem_) {
        const int oh = hotHeader_;
        const int oi = hotItem_;
        hotHeader_ = hh;
        hotItem_ = item;
        InvalidateHoverChange(oh, hh, oi, item);
      }
      if (!trackingLeave_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        trackingLeave_ = true;
      }
      return 0;
    }

    case WM_MOUSELEAVE: {
      trackingLeave_ = false;
      unlockBtnHot_ = false;
      const int oh = hotHeader_;
      const int oi = hotItem_;
      hotHeader_ = HIT_NONE;
      hotItem_ = -1;
      if (locked_) InvalidateRect(hwnd_, nullptr, FALSE);
      else InvalidateHoverChange(oh, HIT_NONE, oi, -1);
      return 0;
    }

    case WM_LBUTTONUP: {
      int x = GET_X_LPARAM(lParam);
      int y = GET_Y_LPARAM(lParam);
      int hh = HitTestHeader(x, y);
      // ✕ = hide list only (do NOT lock the whole app)
      if (hh == HIT_CLOSE) {
        Show(false);
        if (onPlacement_) onPlacement_();
        return 0;
      }
      if (locked_) {
        // + / gear / unlock button / list body → unlock
        RECT btn = ListUnlockButtonRect();
        POINT pt{x, y};
        RECT area = ListArea();
        if (hh == HIT_NEW || hh == HIT_SETTINGS || PtInRect(&btn, pt) || PtInRect(&area, pt)) {
          if (onUnlock_) onUnlock_();
        }
        return 0;
      }
      if (hh == HIT_NEW) {
        if (onCommand_) onCommand_(IDM_NEW_NOTE);
        return 0;
      }
      if (hh == HIT_SETTINGS) {
        if (onCommand_) onCommand_(IDM_SETTINGS);
        return 0;
      }
      int item = HitTestItem(x, y);
      if (item >= 0 && item < static_cast<int>(filtered_.size())) {
        const auto& n = all_[filtered_[item]];
        if (onOpenNote_) onOpenNote_(n.id);
      }
      return 0;
    }

    case WM_COMMAND:
      if (HIWORD(wParam) == EN_CHANGE && reinterpret_cast<HWND>(lParam) == search_) {
        OnSearchChanged();
        return 0;
      }
      break;

    case WM_CLOSE:
      // Hide list window only — locking is separate (settings / auto-lock / menu)
      Show(false);
      if (onPlacement_) onPlacement_();
      return 0;

    case WM_DESTROY:
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
      hwnd_ = nullptr;
      search_ = nullptr;
      return 0;
  }
  return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

}  // namespace securememo
