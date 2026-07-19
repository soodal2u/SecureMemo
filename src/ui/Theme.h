#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <cstdio>
#include <string>

namespace securememo {

namespace DarkUI {
inline constexpr COLORREF kBg = RGB(32, 32, 32);
inline constexpr COLORREF kSurface = RGB(45, 45, 45);
inline constexpr COLORREF kCard = RGB(50, 50, 50);
inline constexpr COLORREF kCardHover = RGB(58, 58, 58);
inline constexpr COLORREF kBorder = RGB(55, 55, 55);
inline constexpr COLORREF kText = RGB(240, 240, 240);
inline constexpr COLORREF kMuted = RGB(160, 160, 160);
inline constexpr COLORREF kPlaceholder = RGB(140, 140, 140);
inline constexpr COLORREF kSearchBg = RGB(55, 55, 55);
inline constexpr COLORREF kNoteBody = RGB(48, 48, 48);
inline constexpr COLORREF kToolbar = RGB(40, 40, 40);
inline constexpr COLORREF kLockGold = RGB(255, 200, 40);
}

struct ColorTheme {
  COLORREF header;
  COLORREF headerHot;
  COLORREF body;
  COLORREF border;
  COLORREF text;
  COLORREF subtle;
  COLORREF accent;
  const wchar_t* name;
};

inline ColorTheme ThemeFor(const std::wstring& color) {
  using namespace DarkUI;
  if (color == L"pink")
    return {RGB(255, 140, 170), RGB(255, 160, 185), kNoteBody, kBorder, kText, kMuted,
            RGB(255, 140, 170), L"분홍"};
  if (color == L"blue")
    return {RGB(80, 160, 230), RGB(100, 180, 240), kNoteBody, kBorder, kText, kMuted,
            RGB(80, 160, 230), L"파랑"};
  if (color == L"green")
    return {RGB(90, 190, 110), RGB(110, 205, 130), kNoteBody, kBorder, kText, kMuted,
            RGB(90, 190, 110), L"초록"};
  if (color == L"purple")
    return {RGB(170, 130, 230), RGB(185, 150, 240), kNoteBody, kBorder, kText, kMuted,
            RGB(170, 130, 230), L"보라"};
  if (color == L"gray")
    return {RGB(140, 140, 140), RGB(160, 160, 160), kNoteBody, kBorder, kText, kMuted,
            RGB(140, 140, 140), L"회색"};
  return {RGB(255, 200, 40), RGB(255, 215, 80), kNoteBody, kBorder, kText, kMuted,
          RGB(255, 200, 40), L"노랑"};
}

inline constexpr int kHeaderH = 36;
inline constexpr int kToolbarH = 40;
inline constexpr int kBtnSize = 28;
inline constexpr int kBtnPad = 6;
inline constexpr int kResizeBorder = 6;
inline constexpr int kNoteDefaultW = 340;
inline constexpr int kNoteDefaultH = 380;
inline constexpr int kNoteMinW = 240;
inline constexpr int kNoteMinH = 200;
inline constexpr int kBodyPad = 12;
inline constexpr int kListDefaultW = 360;
inline constexpr int kListDefaultH = 520;
inline constexpr int kListItemH = 88;

// UTF-16 code units (encoding-safe for MinGW): "보안 메모"
inline constexpr wchar_t kAppTitle[] = {
    0xBCF4, 0xC548, 0x0020, 0xBA54, 0xBAA8, 0x0000};
inline const wchar_t* kNoteClass = L"SecureMemoNoteWindow";
inline const wchar_t* kListClass = L"SecureMemoNotesList";
inline const wchar_t* kAppClass = L"SecureMemoAppWindow";
inline const wchar_t* kPlaceholderText = L"메모를 작성하세요...";

enum CommandId : UINT {
  IDM_NEW_NOTE = 1001,
  IDM_DELETE_NOTE = 1002,
  IDM_LOCK = 1003,
  IDM_EXIT = 1004,
  IDM_ALWAYS_ON_TOP = 1005,
  IDM_SHOW_NOTES = 1006,
  IDM_SHOW_LIST = 1007,
  IDM_UNLOCK = 1008,
  IDM_COLOR_YELLOW = 1100,
  IDM_COLOR_PINK = 1101,
  IDM_COLOR_BLUE = 1102,
  IDM_COLOR_GREEN = 1103,
  IDM_COLOR_PURPLE = 1104,
  IDM_COLOR_GRAY = 1105,
  IDM_CHANGE_PASSWORD = 1200,
  IDM_SETTINGS = 1201,
  IDM_HIDE_LIST = 1202,
  // Auto-lock intervals (minutes; 0 = off)
  IDM_AUTOLOCK_5 = 1210,
  IDM_AUTOLOCK_10 = 1211,
  IDM_AUTOLOCK_30 = 1212,
  IDM_AUTOLOCK_60 = 1213,
  IDM_AUTOLOCK_180 = 1214,
  IDM_AUTOLOCK_360 = 1215,
  IDM_AUTOLOCK_OFF = 1216,
  IDM_FMT_BOLD = 1301,
  IDM_FMT_ITALIC = 1302,
  IDM_FMT_UNDERLINE = 1303,
  IDM_FMT_STRIKE = 1304,
  IDM_FMT_BULLET = 1305,
  IDM_FMT_IMAGE = 1306,
  IDT_SAVE_DEBOUNCE = 2001,
  IDT_AUTO_LOCK = 2002,
  IDT_TRAY = 2003,
  IDT_UPDATE_CHECK = 2004,
  HIT_NONE = 0,
  HIT_DRAG = 1,
  HIT_NEW = 2,
  HIT_MENU = 3,
  HIT_CLOSE = 4,
  HIT_RESIZE = 5,
  HIT_SETTINGS = 6,
  HIT_LIST = 7,
  HIT_UNLOCK = 8,
  WM_TRAYICON = WM_APP + 1,
  WM_APP_LOCK_NOW = WM_APP + 2,
  WM_APP_REQUEST_EXIT = WM_APP + 3,
  WM_APP_LIST_REFRESH = WM_APP + 4,
  WM_APP_OPEN_NOTE = WM_APP + 5,
  // Second process → first process: show UI, do not start another vault session
  WM_APP_ACTIVATE_EXISTING = WM_APP + 6,
  WM_APP_UPDATE_RESULT = WM_APP + 7,
  WM_APP_NOTE_ACTIVITY = WM_APP + 8,
};

// Named mutex — only one SecureMemo process may own the vault
inline const wchar_t* kSingleInstanceMutex = L"Local\\SecureMemo_SingleInstance_Mutex_v1";

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Remove white system border; dark chrome + rounded corners
inline void ApplyWin11Chrome(HWND hwnd, COLORREF /*border*/) {
  DWORD pref = DWMWCP_ROUND;
  DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  // Hide default light border (Win11) — use COLOR_NONE
  COLORREF none = static_cast<COLORREF>(DWMWA_COLOR_NONE);
  DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &none, sizeof(none));
}

// Borderless window edge resize / drag hit-test
inline LRESULT HitTestBorderless(HWND hwnd, LPARAM lParam, int edgePx = kResizeBorder) {
  POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
  RECT wr{};
  GetWindowRect(hwnd, &wr);
  const int x = pt.x - wr.left;
  const int y = pt.y - wr.top;
  const int w = wr.right - wr.left;
  const int h = wr.bottom - wr.top;
  const bool left = x < edgePx;
  const bool right = x >= w - edgePx;
  const bool top = y < edgePx;
  const bool bottom = y >= h - edgePx;
  if (top && left) return HTTOPLEFT;
  if (top && right) return HTTOPRIGHT;
  if (bottom && left) return HTBOTTOMLEFT;
  if (bottom && right) return HTBOTTOMRIGHT;
  if (left) return HTLEFT;
  if (right) return HTRIGHT;
  if (top) return HTTOP;
  if (bottom) return HTBOTTOM;
  return HTCLIENT;
}

inline void GetPrimaryWorkArea(RECT& wa) {
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
}

inline bool GetNearestMonitorWorkArea(int x, int y, int w, int h, RECT& wa) {
  RECT r{x, y, x + (w > 0 ? w : kNoteDefaultW), y + (h > 0 ? h : kNoteDefaultH)};
  HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
  if (!mon) return false;
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(mon, &mi)) return false;
  wa = mi.rcWork;
  return true;
}

inline void ClampRectToWorkArea(int& x, int& y, int& w, int& h) {
  RECT wa{};
  if (!GetNearestMonitorWorkArea(x, y, w, h, wa)) GetPrimaryWorkArea(wa);
  const int waW = wa.right - wa.left;
  const int waH = wa.bottom - wa.top;
  if (w < kNoteMinW) w = kNoteMinW;
  if (h < kNoteMinH) h = kNoteMinH;
  if (w > waW - 16) w = (waW > 32) ? (waW - 16) : kNoteMinW;
  if (h > waH - 16) h = (waH > 32) ? (waH - 16) : kNoteMinH;
  if (x < wa.left) x = wa.left + 12;
  if (y < wa.top) y = wa.top + 12;
  if (x + w > wa.right) x = wa.right - w - 12;
  if (y + h > wa.bottom) y = wa.bottom - h - 12;
  if (x < wa.left) x = wa.left + 12;
  if (y < wa.top) y = wa.top + 12;
}

inline void DefaultNotePlacement(int index, int& x, int& y, int& w, int& h) {
  RECT wa{};
  GetPrimaryWorkArea(wa);
  w = kNoteDefaultW;
  h = kNoteDefaultH;
  const int offset = (index % 8) * 28;
  x = wa.right - w - 48 - offset;
  y = wa.top + 48 + offset;
  ClampRectToWorkArea(x, y, w, h);
}

// Place new note to the right of the list window (or cascade from a seed rect)
inline void PlaceNoteNear(int seedX, int seedY, int seedW, int seedH,
                          int index, int& x, int& y, int& w, int& h) {
  w = kNoteDefaultW;
  h = kNoteDefaultH;
  const int offset = (index % 8) * 28;
  if (seedW > 0 && seedH > 0) {
    x = seedX + seedW + 16 + offset;
    y = seedY + offset;
  } else {
    DefaultNotePlacement(index, x, y, w, h);
    return;
  }
  ClampRectToWorkArea(x, y, w, h);
}

inline void DefaultListPlacement(int& x, int& y, int& w, int& h) {
  RECT wa{};
  GetPrimaryWorkArea(wa);
  w = kListDefaultW;
  h = kListDefaultH;
  x = wa.left + 48;
  y = wa.top + 48;
  if (x + w > wa.right) x = wa.right - w - 24;
  if (y + h > wa.bottom) y = wa.bottom - h - 24;
}

// Reject RTF/font junk that leaked into plain preview
inline bool IsJunkPreviewText(const std::wstring& s) {
  if (s.empty()) return true;
  if (s.find(L"{\\rtf") != std::wstring::npos) return true;
  if (s.find(L"\\rtf") != std::wstring::npos) return true;
  if (s.find(L"Riched") != std::wstring::npos || s.find(L"riched") != std::wstring::npos)
    return true;
  if (s.find(L"Segoe UI") != std::wstring::npos && s.find(L";;") != std::wstring::npos)
    return true;
  // font-table style fragments
  if (s.rfind(L"Segoe", 0) == 0 && s.size() < 40) return true;
  int letter = 0, weird = 0;
  for (wchar_t c : s) {
    if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
        (c >= 0xAC00 && c <= 0xD7A3) || (c >= L'0' && c <= L'9'))
      ++letter;
    if (c == L';' || c == L'{' || c == L'}' || c == L'\\') ++weird;
  }
  if (weird > 2 && letter < 8) return true;
  return false;
}

inline bool IsRectOnAnyMonitor(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return false;
  RECT r{x, y, x + w, y + h};
  return MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != nullptr;
}

inline std::wstring PreviewLine(const std::wstring& content, size_t maxChars = 80) {
  // content should already be plain; still guard against leftover RTF
  std::wstring s = content;
  if (s.size() >= 5 && s.rfind(L"{\\rtf", 0) == 0) {
    // defer to model helper via light inline strip of font/header junk
    // Prefer note.content (plain) — this is a last resort only
    auto pos = s.find(L"\\pard");
    if (pos == std::wstring::npos) pos = s.find(L"\\f0");
    if (pos != std::wstring::npos) s = s.substr(pos);
    std::wstring plain;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == L'\\') {
        if (i + 1 < s.size() && (s[i + 1] == L'\\' || s[i + 1] == L'{' || s[i + 1] == L'}')) {
          plain.push_back(s[++i]);
          continue;
        }
        size_t j = i + 1;
        while (j < s.size() && ((s[j] >= L'a' && s[j] <= L'z') || (s[j] >= L'A' && s[j] <= L'Z')))
          ++j;
        while (j < s.size() && ((s[j] >= L'0' && s[j] <= L'9') || s[j] == L'-')) ++j;
        if (j < s.size() && s[j] == L' ') ++j;
        i = j - 1;
        continue;
      }
      if (s[i] == L'{' || s[i] == L'}' || s[i] == L'\r' || s[i] == L'\n') continue;
      plain.push_back(s[i]);
    }
    s = plain;
  }
  for (auto& c : s) {
    if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
  }
  while (!s.empty() && s.front() == L' ') s.erase(s.begin());
  while (!s.empty() && s.back() == L' ') s.pop_back();
  // drop leftover RTF noise tokens
  if (s.find(L"Riched") != std::wstring::npos || s.find(L"\\rtf") != std::wstring::npos) {
    // still garbage — empty preview
    s.clear();
  }
  if (s.empty() || IsJunkPreviewText(s)) return kPlaceholderText;
  if (s.size() > maxChars) {
    s.resize(maxChars);
    s += L"…";
  }
  return s;
}

inline std::wstring FormatListDate(const std::wstring& iso) {
  if (iso.size() < 10) return iso;
  int month = 0, day = 0;
  try {
    month = std::stoi(iso.substr(5, 2));
    day = std::stoi(iso.substr(8, 2));
  } catch (...) {
    return iso;
  }
  wchar_t buf[32];
  swprintf(buf, 32, L"%d월 %d일", month, day);
  return buf;
}

}  // namespace securememo
