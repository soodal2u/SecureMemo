#include "app/Application.h"

#include "app/UpdateChecker.h"
#include "app/Version.h"
#include "crypto/Auth.h"
#include "ui/LockDialog.h"
#include "ui/Theme.h"
#include "util/SecureBuffer.h"

#include <commctrl.h>

namespace securememo {

Application::Application()
    : notes_(this), guard_(this) {}

Application::~Application() {
  key_.Wipe();
  list_.Destroy();
  notes_.DestroyAll();
  tray_.Destroy();
}

int Application::Run(HINSTANCE inst) {
  inst_ = inst;
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
  InitCommonControlsEx(&icc);
  NoteWindow::EnsureRichEdit();

  if (!NoteWindow::RegisterClass(inst_)) return 1;
  if (!NotesListWindow::RegisterClass(inst_)) return 1;
  if (!InitWindow()) return 1;

  // Start with list only (locked). No password until user opens content.
  EnterLockedShell();
  tray_.Create(hwnd_);
  guard_.Start(hwnd_);
  ScheduleUpdateCheck();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  guard_.Stop(hwnd_);
  tray_.Destroy();
  list_.Destroy();
  notes_.DestroyAll();
  key_.Wipe();
  return static_cast<int>(msg.wParam);
}

bool Application::InitWindow() {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = Application::AppWndProc;
  wc.hInstance = inst_;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(1));
  if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.lpszClassName = kAppClass;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  hwnd_ = CreateWindowExW(
      0, kAppClass, kAppTitle,
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
      nullptr, nullptr, inst_, this);
  if (!hwnd_) return false;
  ShowWindow(hwnd_, SW_HIDE);
  return true;
}

LRESULT CALLBACK Application::AppWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  Application* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = reinterpret_cast<Application*>(cs->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self) return self->HandleMessage(msg, wParam, lParam);
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Application::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_TRAYICON:
      tray_.HandleMessage(wParam, lParam);
      return 0;

    case WM_APP_ACTIVATE_EXISTING:
      // Second launch of the exe — only raise UI; vault stays with this process
      if (state_ == AppState::Locked) {
        if (!list_.Hwnd() || !list_.IsVisible()) {
          EnterLockedShell();
        } else {
          list_.Show(true);
        }
      } else if (state_ == AppState::Unlocked) {
        ShowList();
      }
      return 0;

    case WM_COMMAND: {
      OnNoteCommand(nullptr, LOWORD(wParam));
      return 0;
    }

    case WM_TIMER:
      if (wParam == IDT_AUTO_LOCK) {
        guard_.OnTimer(hwnd_);
        return 0;
      }
      if (wParam == IDT_SAVE_DEBOUNCE) {
        KillTimer(hwnd_, IDT_SAVE_DEBOUNCE);
        if (savePending_ && state_ == AppState::Unlocked) {
          SaveVault();
          savePending_ = false;
          RefreshList();
        }
        return 0;
      }
      if (wParam == IDT_UPDATE_CHECK) {
        KillTimer(hwnd_, IDT_UPDATE_CHECK);
        if (!updateCheckStarted_) {
          updateCheckStarted_ = true;
          HANDLE th = CreateThread(
              nullptr, 0,
              [](LPVOID p) -> DWORD {
                HWND hw = static_cast<HWND>(p);
                auto* info = new UpdateInfo(CheckForUpdate());
                PostMessageW(hw, WM_APP_UPDATE_RESULT, 0, reinterpret_cast<LPARAM>(info));
                return 0;
              },
              hwnd_, 0, nullptr);
          if (th) CloseHandle(th);
        }
        return 0;
      }
      break;

    case WM_APP_NOTE_ACTIVITY:
      guard_.NotifyActivity();
      return 0;

    case WM_APP_UPDATE_RESULT:
      OnUpdateCheckResult(lParam);
      return 0;

    case WM_APP_LOCK_NOW:
      PerformLock();
      return 0;

    case WM_QUERYENDSESSION:
      // PC shutdown / logoff: always persist note positions & open state
      if (state_ == AppState::Unlocked) {
        SaveVault();
      } else if (state_ == AppState::Locked) {
        // Geometry already saved at lock time; refresh open-window rects if possible
        // (content cannot be re-encrypted without key — positions were saved on lock)
      }
      return TRUE;

    case WM_ENDSESSION:
      if (wParam) {
        // Session is ending — last chance save when unlocked
        if (state_ == AppState::Unlocked) SaveVault();
      }
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void Application::EnterLockedShell() {
  state_ = AppState::Locked;
  PublicIndex idx;
  int count = 0;
  if (publicIndex_.Load(idx)) count = static_cast<int>(idx.noteCount);
  if (count <= 0 && storage_.Exists()) count = 1;

  if (!list_.Hwnd()) {
    list_.Create(inst_, hwnd_);
    list_.SetOnCommand([this](UINT cmd) { OnNoteCommand(nullptr, cmd); });
    list_.SetOnOpenNote([this](const std::wstring& id) { OpenNoteId(id); });
    list_.SetOnUnlockRequest([this]() { RequestUnlock(); });
    list_.SetOnPlacementChanged([this]() { UpdatePublicIndex(); });
    if (idx.hasListPlacement && idx.listW > 0 && idx.listH > 0) {
      list_.SetPlacement(idx.listX, idx.listY, idx.listW, idx.listH);
    }
  }
  list_.SetLocked(true, count);
  list_.Show(true);
}

HWND Application::UnlockAnchorHwnd() const {
  if (list_.Hwnd() && IsWindow(list_.Hwnd()) && IsWindowVisible(list_.Hwnd())) {
    return list_.Hwnd();
  }
  HWND noteHwnd = nullptr;
  // NoteManager::ForEach is non-const; cast carefully for read-only walk
  const_cast<NoteManager&>(notes_).ForEach([&](NoteWindow* w) {
    if (noteHwnd || !w || !w->Hwnd()) return;
    if (IsWindowVisible(w->Hwnd())) noteHwnd = w->Hwnd();
  });
  if (noteHwnd) return noteHwnd;
  if (list_.Hwnd() && IsWindow(list_.Hwnd())) return list_.Hwnd();
  return hwnd_;
}

void Application::RequestUnlock() {
  if (state_ == AppState::Unlocked) {
    ShowList();
    return;
  }
  // Tray may deliver NIN_SELECT + LBUTTONUP together → one unlock UI only
  if (unlockUiBusy_) return;
  unlockUiBusy_ = true;

  // Prefer showing list; if unlock came from a note, that note stays as anchor
  if (!list_.Hwnd()) {
    EnterLockedShell();
  } else if (!list_.IsVisible()) {
    // Keep list hidden if user unlocked from a note-only view
  }

  if (!storage_.Exists()) {
    SetupFlow();
    unlockUiBusy_ = false;
    return;
  }
  UnlockFlow();
  unlockUiBusy_ = false;
}

bool Application::SetupFlow() {
  auto res = ShowLockDialog(UnlockAnchorHwnd(), LockDialogMode::Setup);
  if (!res.ok) return false;

  if (!storage_.CreateNew(res.password, key_, vault_)) {
    SecureWipeString(res.password);
    SecureWipeString(res.newPassword);
    MessageBoxW(hwnd_, L"저장소 생성에 실패했습니다.", kAppTitle, MB_ICONERROR);
    return false;
  }
  SecureWipeString(res.password);
  SecureWipeString(res.newPassword);
  EnterUnlocked();
  return true;
}

bool Application::UnlockFlow() {
  failCount_ = 0;
  while (true) {
    auto res = ShowLockDialog(UnlockAnchorHwnd(), LockDialogMode::Unlock);
    if (!res.ok) {
      return false;
    }

    VaultData data;
    SecureBytes key;
    if (storage_.Unlock(res.password, key, data)) {
      SecureWipeString(res.password);
      key_ = std::move(key);
      vault_ = std::move(data);
      EnterUnlocked();
      return true;
    }

    SecureWipeString(res.password);
    failCount_++;
    MessageBoxW(hwnd_, L"비밀번호가 올바르지 않습니다.", kAppTitle, MB_ICONWARNING);
    if (failCount_ >= 5) {
      Sleep(2000);
      failCount_ = 0;
    }
  }
}

void Application::EnterUnlocked() {
  state_ = AppState::Unlocked;
  guard_.SetAutoLockMinutes(vault_.settings.autoLockMinutes);

  // Rebuild notes from vault (content restored; visibility remembered)
  notes_.LoadFromVault(vault_, inst_, hwnd_);
  notes_.SetSessionLocked(false, nullptr);

  if (!list_.Hwnd()) {
    list_.Create(inst_, hwnd_);
    list_.SetOnCommand([this](UINT cmd) { OnNoteCommand(nullptr, cmd); });
    list_.SetOnOpenNote([this](const std::wstring& id) { OpenNoteId(id); });
    list_.SetOnUnlockRequest([this]() { RequestUnlock(); });
    list_.SetOnPlacementChanged([this]() { UpdatePublicIndex(); });
    PublicIndex idx;
    if (publicIndex_.Load(idx) && idx.hasListPlacement && idx.listW > 0) {
      list_.SetPlacement(idx.listX, idx.listY, idx.listW, idx.listH);
    }
  }
  list_.SetLocked(false, 0);

  notes_.ForEach([](NoteWindow* w) {
    if (!w) return;
    auto& n = w->Data();
    if (IsJunkPreviewText(n.content)) {
      n.content.clear();
      if (!n.rtf.empty()) n.content = PlainFromRtf(n.rtf);
      if (IsJunkPreviewText(n.content)) {
        n.content.clear();
        n.rtf.clear();
      }
    }
  });
  RefreshList();
  UpdatePublicIndex();
  ShowList();
}

void Application::UpdatePublicIndex() {
  PublicIndex idx;
  publicIndex_.Load(idx);
  idx.version = 2;
  idx.noteCount = static_cast<uint32_t>(notes_.Count());
  if (list_.Hwnd()) {
    int x, y, w, h;
    list_.GetPlacement(x, y, w, h);
    idx.listX = x;
    idx.listY = y;
    idx.listW = w;
    idx.listH = h;
    idx.hasListPlacement = true;
  }
  publicIndex_.Save(idx);
}

void Application::RefreshList() {
  if (!list_.Hwnd()) return;
  if (state_ != AppState::Unlocked) return;
  list_.SetNotes(notes_.SnapshotNotes());
}

void Application::ShowList() {
  if (!list_.Hwnd()) {
    list_.Create(inst_, hwnd_);
    list_.SetOnCommand([this](UINT cmd) { OnNoteCommand(nullptr, cmd); });
    list_.SetOnOpenNote([this](const std::wstring& id) { OpenNoteId(id); });
    list_.SetOnUnlockRequest([this]() { RequestUnlock(); });
  }
  if (state_ == AppState::Unlocked) RefreshList();
  list_.Show(true);
}

void Application::OpenNoteId(const std::wstring& id) {
  if (state_ != AppState::Unlocked) {
    RequestUnlock();
    return;
  }
  notes_.OpenNote(id);
  guard_.NotifyActivity();
}

void Application::ShowSettingsMenu() {
  POINT pt;
  GetCursorPos(&pt);
  HMENU menu = CreatePopupMenu();
  HMENU autoLock = CreatePopupMenu();

  const int cur = vault_.settings.autoLockMinutes;
  auto check = [cur](int m) -> UINT {
    return MF_STRING | (cur == m ? MF_CHECKED : 0);
  };

  AppendMenuW(autoLock, check(5), IDM_AUTOLOCK_5, L"5분");
  AppendMenuW(autoLock, check(10), IDM_AUTOLOCK_10, L"10분");
  AppendMenuW(autoLock, check(30), IDM_AUTOLOCK_30, L"30분");
  AppendMenuW(autoLock, check(60), IDM_AUTOLOCK_60, L"1시간");
  AppendMenuW(autoLock, check(180), IDM_AUTOLOCK_180, L"3시간");
  AppendMenuW(autoLock, check(360), IDM_AUTOLOCK_360, L"6시간");
  AppendMenuW(autoLock, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(autoLock, check(0), IDM_AUTOLOCK_OFF, L"자동 잠금 끄기");

  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(autoLock),
              L"자동 잠금 (메모 미사용 시)");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_CHANGE_PASSWORD, L"비밀번호 변경");
  AppendMenuW(menu, MF_STRING, IDM_LOCK, L"지금 잠금");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_EXIT, L"종료");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"저장: %APPDATA%\\SecureMemo\\");

  SetForegroundWindow(list_.Hwnd() ? list_.Hwnd() : hwnd_);
  const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0,
                                  list_.Hwnd() ? list_.Hwnd() : hwnd_, nullptr);
  DestroyMenu(menu);
  if (cmd) OnNoteCommand(nullptr, cmd);
}

void Application::OnNotesChanged() {
  if (state_ != AppState::Unlocked) return;
  ScheduleSave();
  RefreshList();
  UpdatePublicIndex();
  guard_.NotifyActivity();
}

void Application::ScheduleSave() {
  savePending_ = true;
  KillTimer(hwnd_, IDT_SAVE_DEBOUNCE);
  SetTimer(hwnd_, IDT_SAVE_DEBOUNCE, 500, nullptr);
}

bool Application::SaveVault() {
  if (key_.empty()) return false;
  // Allow save while unlocked; during lock path we call this before wiping the key
  notes_.CollectToVault(vault_);
  if (!storage_.Save(key_, vault_)) {
    MessageBoxW(hwnd_, L"메모 저장에 실패했습니다.", kAppTitle, MB_ICONERROR);
    return false;
  }
  UpdatePublicIndex();
  return true;
}

void Application::OnNoteCommand(NoteWindow* win, UINT cmd) {
  guard_.NotifyActivity();
  switch (cmd) {
    case IDM_SHOW_LIST:
    case IDM_SHOW_NOTES:
      // Tray click: only restore list (password via 잠금 해제 button — no auto dialog)
      if (state_ == AppState::Locked) {
        if (!list_.Hwnd()) EnterLockedShell();
        else list_.Show(true);
      } else {
        ShowList();
      }
      break;
    case IDM_UNLOCK:
      RequestUnlock();
      break;
    case IDM_NEW_NOTE:
      if (state_ == AppState::Locked) {
        RequestUnlock();
      }
      if (state_ == AppState::Unlocked) {
        int lx = 0, ly = 0, lw = 0, lh = 0;
        if (list_.Hwnd()) list_.GetPlacement(lx, ly, lw, lh);
        auto* n = notes_.CreateNote(inst_, hwnd_, nullptr, &lx, &ly, &lw, &lh);
        if (n) {
          n->Show(true);
          n->FocusEdit();
        }
        RefreshList();
        UpdatePublicIndex();
        ScheduleSave();
      }
      break;
    case IDM_DELETE_NOTE:
      if (state_ == AppState::Unlocked && win) {
        if (MessageBoxW(win->Hwnd(), L"이 메모를 삭제할까요?", kAppTitle,
                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
          notes_.DeleteNote(win);
          RefreshList();
          UpdatePublicIndex();
          ShowList();
        }
      }
      break;
    case IDM_SETTINGS:
      if (state_ == AppState::Locked) RequestUnlock();
      else ShowSettingsMenu();
      break;
    case IDM_LOCK:
      if (state_ == AppState::Unlocked) PerformLock();
      else {
        // already locked: closing list hides UI, stay in tray
        list_.Show(false);
      }
      break;
    case IDM_EXIT:
      RequestExit();
      break;
    case IDM_CHANGE_PASSWORD:
      if (state_ == AppState::Unlocked) ChangePasswordFlow();
      else RequestUnlock();
      break;
    case IDM_AUTOLOCK_5:
    case IDM_AUTOLOCK_10:
    case IDM_AUTOLOCK_30:
    case IDM_AUTOLOCK_60:
    case IDM_AUTOLOCK_180:
    case IDM_AUTOLOCK_360:
    case IDM_AUTOLOCK_OFF: {
      if (state_ != AppState::Unlocked) {
        RequestUnlock();
        break;
      }
      int minutes = 5;
      if (cmd == IDM_AUTOLOCK_10) minutes = 10;
      else if (cmd == IDM_AUTOLOCK_30) minutes = 30;
      else if (cmd == IDM_AUTOLOCK_60) minutes = 60;
      else if (cmd == IDM_AUTOLOCK_180) minutes = 180;
      else if (cmd == IDM_AUTOLOCK_360) minutes = 360;
      else if (cmd == IDM_AUTOLOCK_OFF) minutes = 0;
      else minutes = 5;
      vault_.settings.autoLockMinutes = minutes;
      guard_.SetAutoLockMinutes(minutes);
      ScheduleSave();
      if (minutes <= 0) {
        MessageBoxW(list_.Hwnd() ? list_.Hwnd() : hwnd_,
                    L"자동 잠금이 꺼졌습니다.", kAppTitle, MB_OK | MB_ICONINFORMATION);
      } else {
        wchar_t msg[128];
        if (minutes < 60)
          swprintf(msg, 128, L"메모를 %d분 동안 편집하지 않으면 자동 잠금됩니다.", minutes);
        else
          swprintf(msg, 128, L"메모를 %d시간 동안 편집하지 않으면 자동 잠금됩니다.",
                   minutes / 60);
        MessageBoxW(list_.Hwnd() ? list_.Hwnd() : hwnd_, msg, kAppTitle,
                    MB_OK | MB_ICONINFORMATION);
      }
      break;
    }
    default:
      break;
  }
}

void Application::LockNow() {
  PostMessageW(hwnd_, WM_APP_LOCK_NOW, 0, 0);
}

void Application::ScheduleUpdateCheck() {
  // Delay a few seconds after start so UI appears first
  SetTimer(hwnd_, IDT_UPDATE_CHECK, 4000, nullptr);
}

void Application::OnUpdateCheckResult(LPARAM lParam) {
  auto* info = reinterpret_cast<UpdateInfo*>(lParam);
  if (!info) return;
  if (!info->error.empty() || !info->available) {
    delete info;
    return;
  }

  std::wstring msg = L"새 버전이 있습니다.\n\n";
  msg += L"현재: ";
  msg += SECUREMEMO_VERSION_WIDE;
  msg += L"\n최신: ";
  msg += info->latestVersion;
  msg += L"\n\n지금 업데이트를 설치할까요?\n(설치 프로그램이 실행됩니다)";

  HWND owner = UnlockAnchorHwnd();
  const int r = MessageBoxW(owner, msg.c_str(), L"SecureMemo Update",
                            MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST);
  if (r == IDYES && !info->downloadUrl.empty()) {
    DownloadAndRunSetup(info->downloadUrl, owner);
  }
  delete info;
}

void Application::PerformLock() {
  if (state_ != AppState::Unlocked) {
    // Already locked: X on list hides it
    list_.Show(false);
    return;
  }

  // 1) Sync every note (x/y/w/h + which are open) then encrypt to disk
  //    This is what allows reboot → unlock → same notes at same places.
  SaveVault();
  UpdatePublicIndex();

  // 2) Keep open note windows on screen with lock overlay (no content in RAM)
  notes_.SetSessionLocked(true, [this]() { RequestUnlock(); });

  // 3) Wipe key material from memory (vault file already has full state)
  key_.Wipe();
  for (auto& n : vault_.notes) {
    SecureWipeString(n.content);
    SecureWipeString(n.rtf);
  }
  // Keep note metadata (id/pos/visible) in vault_ without body for crash recovery? — clear all
  vault_ = VaultData{};
  state_ = AppState::Locked;

  // 4) List → lock overlay only if it was already open (do not force-show)
  PublicIndex idx;
  publicIndex_.Load(idx);
  const bool listWasVisible = list_.Hwnd() && list_.IsVisible();
  if (list_.Hwnd()) {
    list_.SetLocked(true, static_cast<int>(idx.noteCount));
    if (listWasVisible) {
      list_.Show(true);
    } else {
      list_.Show(false);
    }
  }
  // Title uses encoding-safe kAppTitle; body in UTF-16 escapes
  static const wchar_t kLockedMsg[] = {
      0xBCF4, 0xC548, 0x0020, 0xBA54, 0xBAA8, 0xAC00, 0x0020,  // 보안 메모가
      0xC7A0, 0xACBC, 0xC2B5, 0xB2C8, 0xB2E4, 0x002E, 0x0000};  // 잠겼습니다.
  tray_.ShowBalloon(kAppTitle, kLockedMsg);
}

bool Application::RequestExit() {
  if (state_ == AppState::Exiting) return true;

  // Exit does not need a password. Data is already encrypted on disk;
  // we just save (if unlocked), wipe keys from memory, and quit.
  if (state_ == AppState::Unlocked) {
    SaveVault();
  }

  list_.Destroy();
  notes_.DestroyAll();
  key_.Wipe();
  for (auto& n : vault_.notes) {
    SecureWipeString(n.content);
  }
  vault_ = VaultData{};
  state_ = AppState::Exiting;
  DestroyWindow(hwnd_);
  return true;
}

bool Application::ChangePasswordFlow() {
  auto res = ShowLockDialog(UnlockAnchorHwnd(), LockDialogMode::ChangePassword);
  if (!res.ok) {
    SecureWipeString(res.password);
    SecureWipeString(res.newPassword);
    return false;
  }

  SecureBytes checkKey;
  VaultData tmp;
  if (!storage_.Unlock(res.password, checkKey, tmp)) {
    MessageBoxW(hwnd_, L"현재 비밀번호가 올바르지 않습니다.", kAppTitle, MB_ICONWARNING);
    SecureWipeString(res.password);
    SecureWipeString(res.newPassword);
    checkKey.Wipe();
    return false;
  }
  for (auto& n : tmp.notes) SecureWipeString(n.content);
  checkKey.Wipe();

  AuthMeta meta;
  SecureBytes newKey;
  if (!Auth::Create(res.newPassword, meta, newKey)) {
    MessageBoxW(hwnd_, L"비밀번호 변경에 실패했습니다.", kAppTitle, MB_ICONERROR);
    SecureWipeString(res.password);
    SecureWipeString(res.newPassword);
    return false;
  }

  notes_.CollectToVault(vault_);
  if (!storage_.SaveWithNewKey(newKey, vault_, meta)) {
    MessageBoxW(hwnd_, L"새 비밀번호로 저장하지 못했습니다.", kAppTitle, MB_ICONERROR);
    newKey.Wipe();
    SecureWipeString(res.password);
    SecureWipeString(res.newPassword);
    return false;
  }

  key_.Wipe();
  key_ = std::move(newKey);
  SecureWipeString(res.password);
  SecureWipeString(res.newPassword);
  MessageBoxW(hwnd_, L"비밀번호가 변경되었습니다.", kAppTitle, MB_ICONINFORMATION);
  return true;
}

}  // namespace securememo
