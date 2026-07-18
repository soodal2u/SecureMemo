#include "app/NoteManager.h"

#include "app/Application.h"
#include "ui/Theme.h"

namespace securememo {

NoteManager::NoteManager(Application* app) : app_(app) {}

void NoteManager::Wire(NoteWindow* win) {
  win->SetOnChanged([this](NoteWindow*) {
    if (app_) app_->OnNotesChanged();
  });
  win->SetOnCommand([this](NoteWindow* w, UINT cmd) {
    if (app_) app_->OnNoteCommand(w, cmd);
  });
}

void NoteManager::LoadFromVault(const VaultData& data, HINSTANCE inst, HWND appHwnd) {
  DestroyAll();
  int idx = 0;
  for (auto n : data.notes) {
    if (n.width < kNoteMinW) n.width = kNoteDefaultW;
    if (n.height < kNoteMinH) n.height = kNoteDefaultH;
    // Restore saved screen geometry (multi-monitor aware clamp only)
    if (n.x < 0 || n.y < 0 || !IsRectOnAnyMonitor(n.x, n.y, n.width, n.height)) {
      DefaultNotePlacement(idx, n.x, n.y, n.width, n.height);
    } else {
      ClampRectToWorkArea(n.x, n.y, n.width, n.height);
    }
    const bool wasOpen = n.visible;
    auto win = std::make_unique<NoteWindow>();
    // Create hidden first, then Show(wasOpen) to avoid flash then hide
    n.visible = false;
    if (!win->Create(inst, appHwnd, n)) continue;
    win->SetSessionLocked(false);
    win->Data().visible = wasOpen;
    win->Show(wasOpen);
    if (wasOpen) {
      // Ensure restored geometry applied after create
      SetWindowPos(win->Hwnd(),
                   win->Data().alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                   win->Data().x, win->Data().y, win->Data().width, win->Data().height,
                   SWP_SHOWWINDOW | SWP_NOACTIVATE);
    }
    Wire(win.get());
    windows_.push_back(std::move(win));
    ++idx;
  }
  if (windows_.empty()) {
    CreateNote(inst, appHwnd, nullptr);
    if (!windows_.empty()) windows_.back()->Show(false);
  }
}

void NoteManager::CollectToVault(VaultData& data) {
  // Preserve settings
  AppSettings settings = data.settings;
  data.notes.clear();
  data.settings = settings;
  for (auto& w : windows_) {
    if (!w) continue;
    w->SyncToNote();
    // Never drop open notes: visible follows real window state
    data.notes.push_back(w->Data());
  }
}

void NoteManager::HideAll() {
  for (auto& w : windows_) {
    if (w) w->Show(false);
  }
}

void NoteManager::ShowVisible() {
  for (auto& w : windows_) {
    if (w && w->Data().visible) w->Show(true);
  }
}

void NoteManager::ShowAll() {
  for (auto& w : windows_) {
    if (!w) continue;
    w->Data().visible = true;
    w->Show(true);
  }
}

void NoteManager::ClearAllSensitive() {
  for (auto& w : windows_) {
    if (w) w->ClearSensitive();
  }
}

void NoteManager::SetSessionLocked(bool locked, const std::function<void()>& onUnlock) {
  for (auto& w : windows_) {
    if (!w) continue;
    if (locked) {
      // Capture geometry + open state BEFORE wiping content
      w->SyncToNote();
      const bool keepOpen = w->Data().visible;
      w->ClearSensitive();
      w->SetOnUnlockRequest(onUnlock);
      w->SetSessionLocked(true);
      w->Data().visible = keepOpen;
      if (keepOpen) w->Show(true);
    } else {
      w->SetSessionLocked(false);
      w->SetOnUnlockRequest(nullptr);
    }
  }
}

void NoteManager::DestroyAll() {
  for (auto& w : windows_) {
    if (w) w->Destroy();
  }
  windows_.clear();
}

NoteWindow* NoteManager::CreateNote(HINSTANCE inst, HWND appHwnd, const Note* seed,
                                    const int* nearX, const int* nearY,
                                    const int* nearW, const int* nearH) {
  Note n;
  if (seed) {
    n = *seed;
  } else {
    n.id = NewNoteId();
    n.content = L"";
    n.rtf.clear();
    n.color = L"yellow";
    // Prefer next to list; else cascade from last open note; else default
    bool placed = false;
    if (nearX && nearY && nearW && nearH && *nearW > 0) {
      PlaceNoteNear(*nearX, *nearY, *nearW, *nearH,
                    static_cast<int>(windows_.size()), n.x, n.y, n.width, n.height);
      placed = true;
    }
    if (!placed) {
      for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
        if (*it && (*it)->Hwnd() && IsWindowVisible((*it)->Hwnd())) {
          const auto& d = (*it)->Data();
          PlaceNoteNear(d.x, d.y, d.width, d.height, 1, n.x, n.y, n.width, n.height);
          placed = true;
          break;
        }
      }
    }
    if (!placed) {
      DefaultNotePlacement(static_cast<int>(windows_.size()), n.x, n.y, n.width, n.height);
    }
    n.updatedAt = NowTimestamp();
    n.visible = true;
    n.alwaysOnTop = true;
  }
  auto win = std::make_unique<NoteWindow>();
  if (!win->Create(inst, appHwnd, n)) return nullptr;
  auto* ptr = win.get();
  Wire(ptr);
  windows_.push_back(std::move(win));
  return ptr;
}

void NoteManager::DeleteNote(NoteWindow* win) {
  if (!win) return;
  for (auto it = windows_.begin(); it != windows_.end(); ++it) {
    if (it->get() == win) {
      (*it)->Destroy();
      windows_.erase(it);
      break;
    }
  }
  if (windows_.empty() && app_) {
    CreateNote(app_->Instance(), app_->Hwnd(), nullptr);
    if (!windows_.empty()) windows_.back()->Show(false);
  }
  if (app_) app_->OnNotesChanged();
}

NoteWindow* NoteManager::FindById(const std::wstring& id) {
  for (auto& w : windows_) {
    if (w && w->Id() == id) return w.get();
  }
  return nullptr;
}

NoteWindow* NoteManager::OpenNote(const std::wstring& id) {
  auto* w = FindById(id);
  if (!w) return nullptr;
  w->Data().visible = true;
  w->FocusEdit();
  return w;
}

std::vector<Note> NoteManager::SnapshotNotes() const {
  std::vector<Note> out;
  out.reserve(windows_.size());
  for (const auto& w : windows_) {
    if (!w) continue;
    // const_cast for Sync without mutating visibility — copy data as stored
    Note n = w->Data();
    // live content from window if possible
    // NoteWindow SyncToNote is non-const; call via const_cast carefully
    const_cast<NoteWindow*>(w.get())->SyncToNote();
    out.push_back(w->Data());
  }
  return out;
}

void NoteManager::ForEach(const std::function<void(NoteWindow*)>& fn) {
  for (auto& w : windows_) {
    if (w) fn(w.get());
  }
}

bool NoteManager::HasVisibleWindow() const {
  for (const auto& w : windows_) {
    if (w && w->Hwnd() && IsWindowVisible(w->Hwnd())) return true;
  }
  return false;
}

}  // namespace securememo
