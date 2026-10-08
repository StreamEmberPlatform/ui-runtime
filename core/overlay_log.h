// StreamEmber Overlay core — tiny thread-safe file logger and string helpers.
#pragma once

#include <string>

namespace seo {

// Opens <logDir>/Overlay.log (appends). Safe to call more than once; the last call wins.
void LogOpen(const std::wstring& logDir);
void Log(const char* level, const std::string& message);

inline void LogInfo(const std::string& m) { Log("INFO", m); }
inline void LogWarn(const std::string& m) { Log("WARN", m); }
inline void LogError(const std::string& m) { Log("ERROR", m); }

std::string ToUtf8(const std::wstring& text);
std::wstring FromUtf8(const std::string& text);

}  // namespace seo
