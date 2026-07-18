#pragma once

#include "app/NoteManager.h"
#include "app/SecurityGuard.h"
#include "crypto/PublicIndex.h"
#include "crypto/VaultStorage.h"
#include "ui/NotesListWindow.h"
#include "ui/TrayIcon.h"

#include <windows.h>

namespace securememo {

enum class AppState {
  Locked,
  Unlocked,
  Exiting,
};

class Application {
 public:
  Application();
  ~Application();

  int Run(HINSTANCE inst);

  HINSTANCE Instance() const { return inst_; }
  HWND Hwnd() const { return hwnd_; }
  bool IsUnlocked() const { return state_ == AppState::Unlocked; }

  void OnNotesChanged();
  void OnNoteCommand(NoteWindow* win, UINT cmd);
  void LockNow();

 private:
  static LRESULT CALLBACK AppWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

  bool InitWindow();
  void EnterLockedShell();
  bool UnlockFlow();
  bool SetupFlow();
  void EnterUnlocked();
  void PerformLock();
  bool RequestExit();
  bool SaveVault();
  bool ChangePasswordFlow();
  void ScheduleSave();
  void RefreshList();
  void ShowList();
  void OpenNoteId(const std::wstring& id);
  void ShowSettingsMenu();
  void UpdatePublicIndex();
  void RequestUnlock();
  // Prefer visible list, else a visible note, for password-dialog placement
  HWND UnlockAnchorHwnd() const;

  HINSTANCE inst_ = nullptr;
  HWND hwnd_ = nullptr;
  AppState state_ = AppState::Locked;

  VaultStorage storage_;
  PublicIndexStore publicIndex_;
  VaultData vault_;
  SecureBytes key_;
  int failCount_ = 0;

  NoteManager notes_;
  NotesListWindow list_;
  SecurityGuard guard_;
  TrayIcon tray_;
  bool savePending_ = false;
  bool unlockUiBusy_ = false;  // prevent double password dialogs
};

}  // namespace securememo
