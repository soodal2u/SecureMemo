#include "app/UpdateChecker.h"

#include "app/Version.h"

#include <winhttp.h>

#include <sstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace securememo {
namespace {

std::wstring Utf8ToWideLocal(const std::string& u) {
  if (u.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), static_cast<int>(u.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, u.c_str(), static_cast<int>(u.size()), out.data(), n);
  return out;
}

std::string WideToUtf8Local(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                              nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                      out.data(), n, nullptr, nullptr);
  return out;
}

// Minimal JSON string value extractor: "key": "value"
bool FindJsonString(const std::string& json, const std::string& key, std::string& out) {
  const std::string pat = "\"" + key + "\"";
  size_t p = 0;
  while (true) {
    p = json.find(pat, p);
    if (p == std::string::npos) return false;
    size_t c = json.find(':', p + pat.size());
    if (c == std::string::npos) return false;
    size_t q1 = json.find('"', c + 1);
    if (q1 == std::string::npos) return false;
    size_t q2 = q1 + 1;
    while (q2 < json.size()) {
      if (json[q2] == '\\' && q2 + 1 < json.size()) {
        q2 += 2;
        continue;
      }
      if (json[q2] == '"') break;
      ++q2;
    }
    if (q2 >= json.size()) return false;
    out = json.substr(q1 + 1, q2 - q1 - 1);
    // unescape simple \/
    std::string un;
    for (size_t i = 0; i < out.size(); ++i) {
      if (out[i] == '\\' && i + 1 < out.size()) {
        char n = out[++i];
        if (n == '/') un.push_back('/');
        else if (n == '\\') un.push_back('\\');
        else if (n == '"') un.push_back('"');
        else if (n == 'n') un.push_back('\n');
        else un.push_back(n);
      } else {
        un.push_back(out[i]);
      }
    }
    out = un;
    return true;
  }
}

// Find browser_download_url whose sibling name is SecureMemo_Setup.exe
bool FindSetupDownloadUrl(const std::string& json, std::string& urlOut) {
  const std::string assetName = WideToUtf8Local(SECUREMEMO_SETUP_ASSET);
  size_t pos = 0;
  while (true) {
    pos = json.find("\"browser_download_url\"", pos);
    if (pos == std::string::npos) return false;
    // Look backward/forward in a window for the asset name
    size_t start = (pos > 400) ? pos - 400 : 0;
    size_t end = (std::min)(json.size(), pos + 500);
    std::string window = json.substr(start, end - start);
    if (window.find(assetName) != std::string::npos) {
      std::string url;
      // parse from this occurrence
      size_t c = json.find(':', pos);
      if (c == std::string::npos) return false;
      size_t q1 = json.find('"', c + 1);
      if (q1 == std::string::npos) return false;
      size_t q2 = q1 + 1;
      while (q2 < json.size()) {
        if (json[q2] == '\\' && q2 + 1 < json.size()) {
          q2 += 2;
          continue;
        }
        if (json[q2] == '"') break;
        ++q2;
      }
      url = json.substr(q1 + 1, q2 - q1 - 1);
      std::string un;
      for (size_t i = 0; i < url.size(); ++i) {
        if (url[i] == '\\' && i + 1 < url.size() && url[i + 1] == '/') {
          un.push_back('/');
          ++i;
        } else {
          un.push_back(url[i]);
        }
      }
      urlOut = un;
      return true;
    }
    pos += 20;
  }
}

bool HttpGet(const std::wstring& host, const std::wstring& path, bool https,
             std::string& bodyOut, std::wstring& err) {
  bodyOut.clear();
  HINTERNET ses = WinHttpOpen(L"SecureMemo-UpdateChecker/1.0",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) {
    err = L"WinHttpOpen failed";
    return false;
  }
  // Timeouts (ms)
  WinHttpSetTimeouts(ses, 5000, 5000, 10000, 30000);

  HINTERNET con = WinHttpConnect(ses, host.c_str(),
                                 https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT,
                                 0);
  if (!con) {
    err = L"WinHttpConnect failed";
    WinHttpCloseHandle(ses);
    return false;
  }

  DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET req = WinHttpOpenRequest(con, L"GET", path.c_str(), nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!req) {
    err = L"WinHttpOpenRequest failed";
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }

  // GitHub API prefers a User-Agent
  std::wstring headers = L"User-Agent: SecureMemo\r\nAccept: application/vnd.github+json\r\n";
  BOOL ok = WinHttpSendRequest(req, headers.c_str(), static_cast<DWORD>(-1L),
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (!ok || !WinHttpReceiveResponse(req, nullptr)) {
    err = L"HTTP request failed";
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }

  std::string body;
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req, &avail)) break;
    if (avail == 0) break;
    std::vector<char> buf(avail);
    DWORD read = 0;
    if (!WinHttpReadData(req, buf.data(), avail, &read) || read == 0) break;
    body.append(buf.data(), read);
  }

  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  bodyOut = std::move(body);
  return !bodyOut.empty();
}

bool HttpDownloadFile(const std::wstring& url, const std::wstring& destPath, std::wstring& err) {
  // Parse https://host/path
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  wchar_t host[256]{};
  wchar_t path[2048]{};
  wchar_t extra[1024]{};
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 1024;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
    err = L"Invalid download URL";
    return false;
  }
  std::wstring fullPath = path;
  if (extra[0]) fullPath += extra;

  std::string body;
  // Follow redirect manually for github releases -> objects.githubusercontent.com
  std::wstring hostW = host;
  std::wstring pathW = fullPath;
  bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

  HINTERNET ses = WinHttpOpen(L"SecureMemo-UpdateChecker/1.0",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) {
    err = L"WinHttpOpen failed";
    return false;
  }
  WinHttpSetTimeouts(ses, 10000, 10000, 30000, 120000);
  // Enable redirects
  DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(ses, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

  HINTERNET con = WinHttpConnect(ses, hostW.c_str(),
                                 https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
  if (!con) {
    err = L"Connect failed";
    WinHttpCloseHandle(ses);
    return false;
  }
  DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET req = WinHttpOpenRequest(con, L"GET", pathW.c_str(), nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!req) {
    err = L"OpenRequest failed";
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }
  std::wstring headers = L"User-Agent: SecureMemo\r\n";
  if (!WinHttpSendRequest(req, headers.c_str(), static_cast<DWORD>(-1L),
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(req, nullptr)) {
    err = L"Download request failed";
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }

  HANDLE file = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    err = L"Cannot create temp setup file";
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }

  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req, &avail)) break;
    if (avail == 0) break;
    std::vector<char> buf(avail);
    DWORD read = 0;
    if (!WinHttpReadData(req, buf.data(), avail, &read) || read == 0) break;
    DWORD written = 0;
    WriteFile(file, buf.data(), read, &written, nullptr);
  }
  CloseHandle(file);
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);

  // Basic size check
  WIN32_FILE_ATTRIBUTE_DATA fad{};
  if (!GetFileAttributesExW(destPath.c_str(), GetFileExInfoStandard, &fad) ||
      fad.nFileSizeLow < 100000) {
    err = L"Downloaded file looks invalid";
    DeleteFileW(destPath.c_str());
    return false;
  }
  return true;
}

}  // namespace

int CompareVersions(const std::wstring& a, const std::wstring& b) {
  auto parse = [](const std::wstring& s) {
    int maj = 0, min = 0, pat = 0;
    std::wstring t = s;
    if (!t.empty() && (t[0] == L'v' || t[0] == L'V')) t = t.substr(1);
    swscanf(t.c_str(), L"%d.%d.%d", &maj, &min, &pat);
    return std::tuple<int, int, int>{maj, min, pat};
  };
  auto [am, an, ap] = parse(a);
  auto [bm, bn, bp] = parse(b);
  if (am != bm) return am - bm;
  if (an != bn) return an - bn;
  return ap - bp;
}

UpdateInfo CheckForUpdate() {
  UpdateInfo info;
  std::wstring path = L"/repos/";
  path += SECUREMEMO_GH_OWNER;
  path += L"/";
  path += SECUREMEMO_GH_REPO;
  path += L"/releases/latest";

  std::string body;
  std::wstring err;
  if (!HttpGet(L"api.github.com", path, true, body, err)) {
    info.error = err.empty() ? L"update check failed" : err;
    return info;
  }

  std::string tag;
  if (!FindJsonString(body, "tag_name", tag)) {
    info.error = L"tag_name not found";
    return info;
  }
  info.latestTag = Utf8ToWideLocal(tag);
  info.latestVersion = info.latestTag;
  if (!info.latestVersion.empty() &&
      (info.latestVersion[0] == L'v' || info.latestVersion[0] == L'V')) {
    info.latestVersion = info.latestVersion.substr(1);
  }

  std::string url;
  if (!FindSetupDownloadUrl(body, url)) {
    // fallback: any browser_download_url ending with Setup.exe
    if (!FindJsonString(body, "browser_download_url", url)) {
      info.error = L"setup download URL not found";
      return info;
    }
  }
  info.downloadUrl = Utf8ToWideLocal(url);

  std::string notes;
  if (FindJsonString(body, "body", notes)) {
    info.releaseNotes = Utf8ToWideLocal(notes);
  }

  const std::wstring current = SECUREMEMO_VERSION_WIDE;
  info.available = CompareVersions(info.latestVersion, current) > 0;
  return info;
}

bool DownloadAndRunSetup(const std::wstring& url, HWND owner) {
  wchar_t tempDir[MAX_PATH]{};
  GetTempPathW(MAX_PATH, tempDir);
  std::wstring dest = tempDir;
  dest += SECUREMEMO_SETUP_ASSET;

  std::wstring err;
  if (!HttpDownloadFile(url, dest, err)) {
    MessageBoxW(owner, err.c_str(), L"SecureMemo Update", MB_ICONERROR);
    return false;
  }

  // Quiet-ish install to same per-user location; user can click through
  INT_PTR r = reinterpret_cast<INT_PTR>(
      ShellExecuteW(owner, L"open", dest.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (r <= 32) {
    MessageBoxW(owner, L"설치 프로그램을 실행하지 못했습니다.", L"SecureMemo Update",
                MB_ICONERROR);
    return false;
  }
  return true;
}

}  // namespace securememo
