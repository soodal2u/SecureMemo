#pragma once

#include "model/Note.h"
#include "ui/NoteWindow.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace securememo {

class Application;

class NoteManager {
 public:
  explicit NoteManager(Application* app);

  void LoadFromVault(const VaultData& data, HINSTANCE inst, HWND appHwnd);
  void CollectToVault(VaultData& data);
  void HideAll();
  void ShowVisible();
  void ShowAll();
  void ClearAllSensitive();
  void DestroyAll();
  // Keep note windows open; wipe content and show lock overlay
  void SetSessionLocked(bool locked, const std::function<void()>& onUnlock);

  // seedPlacement: optional x,y,w,h of list (or last note) to spawn nearby
  NoteWindow* CreateNote(HINSTANCE inst, HWND appHwnd, const Note* seed = nullptr,
                         const int* nearX = nullptr, const int* nearY = nullptr,
                         const int* nearW = nullptr, const int* nearH = nullptr);
  void DeleteNote(NoteWindow* win);
  NoteWindow* FindById(const std::wstring& id);
  NoteWindow* OpenNote(const std::wstring& id);  // show + focus
  std::vector<Note> SnapshotNotes() const;
  void ForEach(const std::function<void(NoteWindow*)>& fn);

  size_t Count() const { return windows_.size(); }
  bool HasVisibleWindow() const;

 private:
  void Wire(NoteWindow* win);

  Application* app_;
  std::vector<std::unique_ptr<NoteWindow>> windows_;
};

}  // namespace securememo
