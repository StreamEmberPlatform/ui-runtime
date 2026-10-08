// StreamEmber Overlay — game backends: startup, Overlay.ini, core (StreamEmber.Overlay.dll) loading, overlay
// state and logging. Shared by every backend; the game-specific part (how frames are presented) lives in
// backends/<game>-*/backend.cpp.
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "backend_common.h"
#include "input.h"

namespace seo_backend {

std::atomic<int> g_backBufferWidth{0};
std::atomic<int> g_backBufferHeight{0};
std::atomic<int> g_cursorX{-1};
std::atomic<int> g_cursorY{-1};

namespace {

HMODULE g_module = nullptr;
Config g_config;
std::wstring g_baseDir;  // <game>\StreamEmber\Overlay (program files)
std::wstring g_rootDir;  // <game>\StreamEmber (Config, Logs)
CoreApi g_coreApi;
std::atomic<const CoreApi*> g_core{nullptr};
std::atomic<bool> g_localVisible{true};
std::atomic<bool> g_localUiInput{false};

std::mutex g_logMutex;
HANDLE g_logFile = INVALID_HANDLE_VALUE;

std::wstring ModuleDirectory(HMODULE module) {
  wchar_t path[MAX_PATH] = {};
  const DWORD len = GetModuleFileNameW(module, path, MAX_PATH);
  std::wstring result(path, len);
  const size_t slash = result.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : result.substr(0, slash);
}

std::string Narrow(const std::wstring& text) {
  if (text.empty()) return std::string();
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr,
                                       nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size, nullptr, nullptr);
  return out;
}

void OpenLog(const wchar_t* logName) {
  const std::wstring dir = g_rootDir + L"\\Logs";
  CreateDirectoryW(g_rootDir.c_str(), nullptr);
  CreateDirectoryW(dir.c_str(), nullptr);
  // New file on every game start; the previous session's log is kept as <name>.previous.log
  const std::wstring path = dir + L"\\" + logName;
  std::wstring previous = path;
  const size_t dot = previous.find_last_of(L'.');
  previous = (dot == std::wstring::npos ? previous : previous.substr(0, dot)) + L".previous.log";
  MoveFileExW(path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);
  g_logFile = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                          nullptr);
}

void ReadConfig() {
  const std::wstring ini = g_rootDir + L"\\Config\\Overlay.ini";
  auto readInt = [&](const wchar_t* section, const wchar_t* key, int def) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, def, ini.c_str()));
  };
  g_config.enabled = readInt(L"Overlay", L"Enabled", 1) != 0;
  g_config.testPattern = readInt(L"Overlay", L"TestPattern", 0) != 0;
  g_config.frameRate = readInt(L"Overlay", L"FrameRate", 60);
  g_config.drawCursor = readInt(L"Overlay", L"DrawCursor", 1) != 0;
  g_config.showBadge = readInt(L"Overlay", L"ShowBadge", 1) != 0;
  g_config.blockRawInputInUiMode = readInt(L"Overlay", L"BlockRawInputInUiMode", 1) != 0;
  g_config.keyToggleVisible = readInt(L"Hotkeys", L"ToggleVisible", VK_F7);
  g_config.keyToggleInput = readInt(L"Hotkeys", L"ToggleInput", VK_F8);
  wchar_t url[2048] = {};
  GetPrivateProfileStringW(L"Overlay", L"StartUrl", L"", url, 2048, ini.c_str());
  g_config.startUrl = Narrow(url);
}

template <typename T>
bool Resolve(HMODULE module, const char* name, T& out) {
  out = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module, name)));
  if (out == nullptr) {
    BLogError(std::string("Core export missing: ") + name);
    return false;
  }
  return true;
}

// Worker thread: loading libcef.dll takes a moment, so it must not block the render thread.
void LoadCore(int width, int height) {
  const std::wstring dllPath = g_baseDir + L"\\StreamEmber.Overlay.dll";
  // LOAD_WITH_ALTERED_SEARCH_PATH: the core's own imports (libcef.dll, chrome_elf.dll) resolve from its folder
  HMODULE module = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (module == nullptr) {
    BLogError("LoadLibrary failed for " + Narrow(dllPath) + " (error " + std::to_string(GetLastError()) +
              "). Is the overlay installed in StreamEmber\\Overlay?");
    return;
  }

  CoreApi& api = g_coreApi;
  bool ok = Resolve(module, "SEO_GetApiVersion", api.GetApiVersion) &&
            Resolve(module, "SEO_Initialize", api.Initialize) && Resolve(module, "SEO_GetState", api.GetState) &&
            Resolve(module, "SEO_Shutdown", api.Shutdown) && Resolve(module, "SEO_Resize", api.Resize) &&
            Resolve(module, "SEO_AcquireFrame", api.AcquireFrame) &&
            Resolve(module, "SEO_ReleaseFrame", api.ReleaseFrame) &&
            Resolve(module, "SEO_SetVisible", api.SetVisible) && Resolve(module, "SEO_IsVisible", api.IsVisible) &&
            Resolve(module, "SEO_SetInputMode", api.SetInputMode) &&
            Resolve(module, "SEO_GetInputMode", api.GetInputMode) &&
            Resolve(module, "SEO_SendMouseMove", api.SendMouseMove) &&
            Resolve(module, "SEO_SendMouseButton", api.SendMouseButton) &&
            Resolve(module, "SEO_SendMouseWheel", api.SendMouseWheel) && Resolve(module, "SEO_SendKey", api.SendKey) &&
            Resolve(module, "SEO_SetFocus", api.SetFocus) && Resolve(module, "SEO_Log", api.Log) &&
            Resolve(module, "SEO_GetAtlasLayout", api.GetAtlasLayout) &&
            Resolve(module, "SEO_GetSprites", api.GetSprites);
  if (!ok) {
    return;
  }
  if (api.GetApiVersion() != SEO_API_VERSION) {
    BLogError("Core API version " + std::to_string(api.GetApiVersion()) + " does not match the backend (" +
              std::to_string(SEO_API_VERSION) + "). Install matching files.");
    return;
  }

  SEO_InitParams params = {};
  params.structSize = sizeof(params);
  params.baseDir = g_baseDir.c_str();
  params.startUrl = g_config.startUrl.c_str();
  params.width = width;
  params.height = height;
  params.frameRate = g_config.frameRate;
  // Carry over hotkey changes made before the core was ready
  api.SetVisible(g_localVisible.load() ? 1 : 0);
  api.SetInputMode(g_localUiInput.load() ? SEO_INPUT_UI : SEO_INPUT_GAME);
  if (!api.Initialize(&params)) {
    BLogError("SEO_Initialize refused to start.");
    return;
  }
  g_core.store(&api);
  BLogInfo("Core loaded; CEF is starting (see StreamEmber\\Logs\\Overlay.log).");
}

}  // namespace

bool BackendStartup(HMODULE module, const wchar_t* logName, const char* displayName) {
  g_module = module;
  g_rootDir = ModuleDirectory(g_module) + L"\\StreamEmber";
  g_baseDir = g_rootDir + L"\\Overlay";
  OpenLog(logName);
  ReadConfig();
  BLogInfo(std::string("StreamEmber Overlay ") + displayName + " backend. Base: " + Narrow(g_baseDir) +
           (g_config.testPattern ? " (TestPattern=1)" : ""));
  if (!g_config.enabled) {
    BLogInfo("Disabled in StreamEmber\\Config\\Overlay.ini (Enabled=0).");
    return false;
  }
  return true;
}

void BackendAttach(HWND window, int width, int height) {
  InstallInputHook(window);
  if (!g_config.testPattern) {
    std::thread(LoadCore, width, height).detach();
  }
}

const Config& GetConfig() { return g_config; }
const std::wstring& GetBaseDir() { return g_baseDir; }
const CoreApi* GetCore() { return g_core.load(); }

bool IsOverlayVisible() {
  const CoreApi* core = GetCore();
  return core != nullptr ? core->IsVisible() != 0 : g_localVisible.load();
}

void ToggleOverlayVisible() {
  const bool visible = !IsOverlayVisible();
  g_localVisible.store(visible);
  if (const CoreApi* core = GetCore()) {
    core->SetVisible(visible ? 1 : 0);
  }
  BLogInfo(visible ? "Overlay shown." : "Overlay hidden.");
}

bool IsUiInputMode() {
  const CoreApi* core = GetCore();
  return core != nullptr ? core->GetInputMode() == SEO_INPUT_UI : g_localUiInput.load();
}

void ToggleUiInputMode() {
  const bool ui = !IsUiInputMode();
  g_localUiInput.store(ui);
  if (const CoreApi* core = GetCore()) {
    core->SetInputMode(ui ? SEO_INPUT_UI : SEO_INPUT_GAME);
  }
  BLogInfo(ui ? "Input: UI (menu mode)." : "Input: game (HUD mode).");
}

void BLog(const char* level, const std::string& message) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  char prefix[48];
  std::snprintf(prefix, sizeof(prefix), "[%02u:%02u:%02u.%03u] [%s] ", t.wHour, t.wMinute, t.wSecond,
                t.wMilliseconds, level);
  const std::string line = prefix + message + "\r\n";
  std::lock_guard<std::mutex> lock(g_logMutex);
  if (g_logFile == INVALID_HANDLE_VALUE) {
    OutputDebugStringA(line.c_str());
    return;
  }
  DWORD written = 0;
  WriteFile(g_logFile, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

}  // namespace seo_backend
