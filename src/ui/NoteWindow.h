#pragma once

#include "model/Note.h"

#include <windows.h>
#include <functional>
#include <string>

namespace securememo {

class NoteWindow {
 public:
  using ChangeCallback = std::function<void(NoteWindow*)>;
  using CommandCallback = std::function<void(NoteWindow*, UINT cmd)>;

  NoteWindow();
  ~NoteWindow();

  bool Create(HINSTANCE inst, HWND appHwnd, const Note& note);
  void Destroy();

  HWND Hwnd() const { return hwnd_; }
  const std::wstring& Id() const { return note_.id; }
  Note& Data() { return note_; }
  const Note& Data() const { return note_; }

  void ApplyFromNote();
  void SyncToNote();
  void SetColor(const std::wstring& color);
  void SetAlwaysOnTop(bool on);
  void Show(bool show);
  void ClearSensitive();
  void FocusEdit();
  // Session lock: keep window open, hide body, show unlock overlay
  void SetSessionLocked(bool locked);
  bool IsSessionLocked() const { return sessionLocked_; }

  void SetOnChanged(ChangeCallback cb) { onChanged_ = std::move(cb); }
  void SetOnCommand(CommandCallback cb) { onCommand_ = std::move(cb); }
  void SetOnUnlockRequest(std::function<void()> cb) { onUnlock_ = std::move(cb); }

  static bool RegisterClass(HINSTANCE inst);
  static bool EnsureRichEdit();

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

  void RebuildBrushes();
  void Layout();
  void ApplyChrome();
  void Paint(HDC hdc);
  void DrawHeaderButton(HDC hdc, const RECT& rc, const wchar_t* glyph, bool hot);
  int HitTestChrome(int x, int y) const;
  int HitTestToolbar(int x, int y) const;
  void GetHeaderButtons(RECT& newBtn, RECT& menuBtn, RECT& closeBtn) const;
  void GetToolbarRects(RECT rects[6]) const;
  void ShowContextMenu();
  void EnsureFonts();
  void NotifyChanged();
  void ApplyCharFormat(DWORD mask, DWORD effects, bool toggle);
  void ApplyBullet();
  void InsertImage();
  bool LoadRtfOrText(const std::wstring& data);
  std::wstring SaveRtf();
  void SetDefaultCharFormat();

  HWND hwnd_ = nullptr;
  HWND edit_ = nullptr;
  HWND appHwnd_ = nullptr;
  HINSTANCE inst_ = nullptr;
  Note note_;

  HBRUSH bodyBrush_ = nullptr;
  HBRUSH headerBrush_ = nullptr;
  HFONT bodyFont_ = nullptr;
  HFONT iconFont_ = nullptr;
  HFONT toolFont_ = nullptr;

  ChangeCallback onChanged_;
  CommandCallback onCommand_;
  std::function<void()> onUnlock_;
  bool dirty_ = false;
  bool emptyDoc_ = true;
  bool sessionLocked_ = false;
  bool unlockBtnHot_ = false;

  int hotHit_ = 0;
  int hotTool_ = -1;
  bool trackingLeave_ = false;

  RECT UnlockButtonRect() const;
};

}  // namespace securememo
