#pragma once

#include <string>
#include <windows.h>

namespace securememo {

struct UpdateInfo {
  bool available = false;
  std::wstring latestTag;       // e.g. v1.0.2
  std::wstring latestVersion;   // e.g. 1.0.2
  std::wstring downloadUrl;     // setup exe URL
  std::wstring releaseNotes;
  std::wstring error;
};

// Fetch latest release from GitHub (blocking; call from worker thread or delayed timer)
UpdateInfo CheckForUpdate();

// Download setup to %TEMP% and launch it. Returns true if ShellExecute succeeded.
bool DownloadAndRunSetup(const std::wstring& url, HWND owner);

// Compare "1.0.1" style versions: >0 if a>b, 0 equal, <0 if a<b
int CompareVersions(const std::wstring& a, const std::wstring& b);

}  // namespace securememo
