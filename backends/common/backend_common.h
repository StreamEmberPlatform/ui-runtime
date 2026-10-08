// StreamEmber Overlay — game backends: shared declarations (config, core loading, overlay state, logging).
// Used by every backend (gtav-d3d11, rdr2-d3d12); nothing here is game specific.
#pragma once

#include <windows.h>

#include <atomic>
#include <string>

#include "se_overlay.h"

namespace seo_backend {

struct Config {
  bool enabled = true;
  bool testPattern = false;        // phase 1: draw a generated pattern without loading CEF
  bool drawCursor = true;          // draw our own cursor while the UI has input
  bool showBadge = true;           // small "StreamEmber" label at the top left: the overlay is running
  bool blockRawInputInUiMode = true;
  int frameRate = 60;
  int keyToggleVisible = VK_F7;    // 0 = disabled
  int keyToggleInput = VK_F8;      // 0 = disabled
  std::string startUrl;            // empty = about:blank until a script calls SEO_LoadUrl
};

// Function table of StreamEmber.Overlay.dll (loaded at runtime)
struct CoreApi {
  SEO_GetApiVersion_t GetApiVersion = nullptr;
  SEO_Initialize_t Initialize = nullptr;
  SEO_GetState_t GetState = nullptr;
  SEO_Shutdown_t Shutdown = nullptr;
  SEO_Resize_t Resize = nullptr;
  SEO_AcquireFrame_t AcquireFrame = nullptr;
  SEO_ReleaseFrame_t ReleaseFrame = nullptr;
  SEO_SetVisible_t SetVisible = nullptr;
  SEO_IsVisible_t IsVisible = nullptr;
  SEO_SetInputMode_t SetInputMode = nullptr;
  SEO_GetInputMode_t GetInputMode = nullptr;
  SEO_SendMouseMove_t SendMouseMove = nullptr;
  SEO_SendMouseButton_t SendMouseButton = nullptr;
  SEO_SendMouseWheel_t SendMouseWheel = nullptr;
  SEO_SendKey_t SendKey = nullptr;
  SEO_SetFocus_t SetFocus = nullptr;
  SEO_Log_t Log = nullptr;
  SEO_GetAtlasLayout_t GetAtlasLayout = nullptr;
  SEO_GetSprites_t GetSprites = nullptr;
};

// --- Startup (backend_base.cpp) -------------------------------------------------------------------------------
// Reads <game folder>\StreamEmber\Config\Overlay.ini and opens StreamEmber\Logs\<logName>. Call once, from the render thread
// on the first presented frame (not from DllMain). Returns false when the overlay is disabled (Enabled=0).
bool BackendStartup(HMODULE module, const wchar_t* logName, const char* displayName);
// Hooks the game window and (unless TestPattern=1) loads the core on a worker thread. Call once after
// BackendStartup with the swap chain's window and back buffer size.
void BackendAttach(HWND window, int width, int height);

// Backend globals (defined in backend_base.cpp)
const Config& GetConfig();
const std::wstring& GetBaseDir();
// Non-null once the core DLL is loaded and SEO_Initialize was called
const CoreApi* GetCore();

// Overlay visibility / input mode. Use the core's state when it is loaded, a local flag otherwise (test pattern).
bool IsOverlayVisible();
void ToggleOverlayVisible();
bool IsUiInputMode();
void ToggleUiInputMode();

void BLog(const char* level, const std::string& message);
inline void BLogInfo(const std::string& m) { BLog("INFO", m); }
inline void BLogError(const std::string& m) { BLog("ERROR", m); }

// Back buffer size in pixels (written by the renderer, read by input mapping)
extern std::atomic<int> g_backBufferWidth;
extern std::atomic<int> g_backBufferHeight;
// Last known cursor position in back buffer pixels (-1 = unknown)
extern std::atomic<int> g_cursorX;
extern std::atomic<int> g_cursorY;

}  // namespace seo_backend
