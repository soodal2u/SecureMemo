#pragma once

#include <cstdint>
#include <filesystem>

namespace securememo {

// Lightweight unencrypted UI state (no note body text)
struct PublicIndex {
  uint32_t version = 2;
  uint32_t noteCount = 0;
  // Last notes-list window placement (0 = unset)
  int listX = 0;
  int listY = 0;
  int listW = 0;
  int listH = 0;
  bool hasListPlacement = false;
};

class PublicIndexStore {
 public:
  PublicIndexStore();
  std::filesystem::path Path() const;
  bool Load(PublicIndex& out) const;
  bool Save(const PublicIndex& idx) const;
  std::filesystem::path DataDir() const { return dataDir_; }

 private:
  std::filesystem::path dataDir_;
};

}  // namespace securememo
