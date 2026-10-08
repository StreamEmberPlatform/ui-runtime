#include "overlay_log.h"

#include <windows.h>

#include <cstdio>
#include <mutex>

namespace seo {
namespace {

std::mutex& LogMutex() {
  static std::mutex* m = new std::mutex();  // leaked on purpose: no static destruction at process exit
  return *m;
}

HANDLE g_logFile = INVALID_HANDLE_VALUE;

}  // namespace

void LogOpen(const std::wstring& logDir) {
  std::lock_guard<std::mutex> lock(LogMutex());
  CreateDirectoryW(logDir.c_str(), nullptr);
  const std::wstring path = logDir + L"\\overlay.log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  if (g_logFile != INVALID_HANDLE_VALUE) {
    CloseHandle(g_logFile);
  }
  g_logFile = file;
}

void Log(const char* level, const std::string& message) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  char prefix[64];
  std::snprintf(prefix, sizeof(prefix), "[%02u:%02u:%02u.%03u] [%s] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                level);
  std::string line = prefix + message + "\r\n";

  std::lock_guard<std::mutex> lock(LogMutex());
  if (g_logFile == INVALID_HANDLE_VALUE) {
    OutputDebugStringA(line.c_str());
    return;
  }
  DWORD written = 0;
  WriteFile(g_logFile, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

std::string ToUtf8(const std::wstring& text) {
  if (text.empty()) {
    return std::string();
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr,
                                       nullptr);
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &result[0], size, nullptr, nullptr);
  return result;
}

std::wstring FromUtf8(const std::string& text) {
  if (text.empty()) {
    return std::wstring();
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &result[0], size);
  return result;
}

}  // namespace seo
