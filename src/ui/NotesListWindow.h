#pragma once

#include "model/Note.h"

#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace securememo {

class NotesListWindow {
 public:
  using CommandCallback = std::function<void(UINT cmd)>;
  using OpenNoteCallback = std::function<void(const std::wstring& noteId)>;
  using UnlockCallback = std::function<void()>;

  NotesListWindow();
  ~NotesListWindow();

  static bool RegisterClass(HINSTANCE inst);

  bool Create(HINSTANCE inst, HWND appHwnd);
  void Destroy();
  void Show(bool show = true);
  bool IsVisible() const;
  HWND Hwnd() const { return hwnd_; }

  void SetLocked(bool locked, int lockedCount = 0);
  bool IsLocked() const { return locked_; }

  void SetNotes(const std::vector<Note>& notes);
  void Refresh();

  // Geometry for restore / new-note placement
  void GetPlacement(int& x, int& y, int& w, int& h) const;
  void SetPlacement(int x, int y, int w, int h);

  void SetOnCommand(CommandCallback cb) { onCommand_ = std::move(cb); }
  void SetOnOpenNote(OpenNoteCallback cb) { onOpenNote_ = std::move(cb); }
  void SetOnUnlockRequest(UnlockCallback cb) { onUnlock_ = std::move(cb); }
  void SetOnPlacementChanged(std::function<void()> cb) { onPlacement_ = std::move(cb); }

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

  void EnsureFonts();
  void Layout();
  void Paint(HDC hdc);
  void PaintItem(HDC hdc, const RECT& rc, const Note& n, bool hot);
  void PaintLockOverlay(HDC hdc, const RECT& area);
  RECT ListUnlockButtonRect() const;
  int HitTestHeader(int x, int y) const;
  int HitTestItem(int x, int y) const;
  void GetHeaderButtons(RECT& newBtn, RECT& settingsBtn, RECT& closeBtn) const;
  RECT ListArea() const;
  void FilterNotes();
  void OnSearchChanged();
  void ScrollBy(int dy);
  RECT ItemScreenRect(int index) const;
  RECT HeaderButtonsUnion() const;
  void InvalidateHoverChange(int oldHeader, int newHeader, int oldItem, int newItem);

  HWND hwnd_ = nullptr;
  HWND search_ = nullptr;
  HWND appHwnd_ = nullptr;
  HINSTANCE inst_ = nullptr;

  HFONT titleFont_ = nullptr;
  HFONT uiFont_ = nullptr;
  HFONT smallFont_ = nullptr;
  HBRUSH bgBrush_ = nullptr;
  HBRUSH searchBrush_ = nullptr;

  std::vector<Note> all_;
  std::vector<int> filtered_;
  std::wstring filter_;
  int scrollY_ = 0;
  int hotItem_ = -1;
  int hotHeader_ = 0;
  bool trackingLeave_ = false;
  bool locked_ = true;
  int lockedCount_ = 0;
  bool unlockBtnHot_ = false;

  CommandCallback onCommand_;
  OpenNoteCallback onOpenNote_;
  UnlockCallback onUnlock_;
  std::function<void()> onPlacement_;
};

}  // namespace securememo
