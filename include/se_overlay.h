// StreamEmber Overlay — public C ABI of StreamEmber.Overlay.dll (game independent core).
//
// Used by the per-game backends (e.g. StreamEmber.Overlay.GTAV.asi) and by the C# bridge (P/Invoke).
// Rules:
//   - All functions are thread-safe unless noted.
//   - Strings are UTF-8 (char) or UTF-16 (wchar_t) as named.
//   - Frames are BGRA, premultiplied alpha, top-down rows.
// Bump SEO_API_VERSION on any breaking change; callers must check SEO_GetApiVersion().
#pragma once

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(SE_OVERLAY_CORE_BUILD)
#define SEO_API __declspec(dllexport)
#else
#define SEO_API __declspec(dllimport)
#endif
#define SEO_CALL __cdecl

#define SEO_API_VERSION 1

// SEO_GetState()
#define SEO_STATE_FAILED   (-1)
#define SEO_STATE_STOPPED  0
#define SEO_STATE_STARTING 1
#define SEO_STATE_READY    2

// SEO_SetInputMode()
#define SEO_INPUT_GAME 0  // UI is click-through, the game receives input (HUD mode)
#define SEO_INPUT_UI   1  // mouse/keyboard go to the UI (menu mode)

// SEO_SendMouseButton() button
#define SEO_MOUSE_LEFT   0
#define SEO_MOUSE_MIDDLE 1
#define SEO_MOUSE_RIGHT  2

// SEO_SendKey() type
#define SEO_KEY_RAWKEYDOWN 0
#define SEO_KEY_KEYUP      2
#define SEO_KEY_CHAR       3

// Modifier flags (same values as cef_event_flags_t)
#define SEO_MOD_CAPS_LOCK    (1u << 0)
#define SEO_MOD_SHIFT        (1u << 1)
#define SEO_MOD_CONTROL      (1u << 2)
#define SEO_MOD_ALT          (1u << 3)
#define SEO_MOD_LEFT_MOUSE   (1u << 4)
#define SEO_MOD_MIDDLE_MOUSE (1u << 5)
#define SEO_MOD_RIGHT_MOUSE  (1u << 6)
#define SEO_MOD_NUM_LOCK     (1u << 8)
#define SEO_MOD_IS_KEY_PAD   (1u << 9)
#define SEO_MOD_IS_LEFT      (1u << 10)
#define SEO_MOD_IS_RIGHT     (1u << 11)
#define SEO_MOD_IS_REPEAT    (1u << 13)

typedef struct SEO_InitParams {
  uint32_t structSize;      // sizeof(SEO_InitParams)
  const wchar_t* baseDir;   // folder with StreamEmber.Overlay.dll, libcef.dll and the host exe (no trailing slash)
  const char* startUrl;     // UTF-8; NULL/"" = <baseDir>/ui/index.html, no scheme = path under ui/, else full URL
  int32_t width;            // initial view size in pixels
  int32_t height;
  int32_t frameRate;        // 1..60, 0 = 60
} SEO_InitParams;

typedef struct SEO_Frame {
  const uint8_t* pixels;    // BGRA premultiplied, valid until SEO_ReleaseFrame()
  int32_t width;
  int32_t height;
  int32_t stride;           // bytes per row
  uint64_t serial;          // increases with every painted frame
} SEO_Frame;

SEO_API int32_t SEO_CALL SEO_GetApiVersion(void);

// Starts CEF on its own thread and returns immediately (1 = started, 0 = invalid call or already started).
// Poll SEO_GetState() for SEO_STATE_READY / SEO_STATE_FAILED.
SEO_API int32_t SEO_CALL SEO_Initialize(const SEO_InitParams* params);
SEO_API int32_t SEO_CALL SEO_GetState(void);
// Closes the browser and shuts CEF down (blocks up to a few seconds). Do not call from DllMain.
SEO_API void SEO_CALL SEO_Shutdown(void);

SEO_API void SEO_CALL SEO_Resize(int32_t width, int32_t height);

// Returns 1 and locks the frame if a frame newer than lastSerial exists; then the caller MUST call
// SEO_ReleaseFrame() on the same thread. Returns 0 otherwise (nothing locked).
SEO_API int32_t SEO_CALL SEO_AcquireFrame(SEO_Frame* outFrame, uint64_t lastSerial);
SEO_API void SEO_CALL SEO_ReleaseFrame(void);

SEO_API void SEO_CALL SEO_SetVisible(int32_t visible);
SEO_API int32_t SEO_CALL SEO_IsVisible(void);
SEO_API void SEO_CALL SEO_SetInputMode(int32_t mode);
SEO_API int32_t SEO_CALL SEO_GetInputMode(void);

// Input, coordinates in view pixels
SEO_API void SEO_CALL SEO_SendMouseMove(int32_t x, int32_t y, uint32_t modifiers, int32_t mouseLeave);
SEO_API void SEO_CALL SEO_SendMouseButton(int32_t x, int32_t y, uint32_t modifiers, int32_t button, int32_t mouseUp,
                                          int32_t clickCount);
SEO_API void SEO_CALL SEO_SendMouseWheel(int32_t x, int32_t y, uint32_t modifiers, int32_t deltaX, int32_t deltaY);
SEO_API void SEO_CALL SEO_SendKey(int32_t type, int32_t windowsKeyCode, int32_t nativeKeyCode, uint32_t modifiers,
                                  uint16_t character, int32_t isSystemKey);
SEO_API void SEO_CALL SEO_SetFocus(int32_t focused);

// Messages. The page uses window.streamember.post(obj) / window.streamember.on(fn).
SEO_API void SEO_CALL SEO_PostToUi(const char* utf8Json);
// Copies the oldest message from the UI into buffer (NUL-terminated) and returns its byte length.
// Returns 0 if the queue is empty, or -(required buffer size) if buffer is too small (message stays queued).
SEO_API int32_t SEO_CALL SEO_PollFromUi(char* buffer, int32_t bufferSize);

// Writes a line to <baseDir>/logs/overlay.log
SEO_API void SEO_CALL SEO_Log(const char* utf8Message);

// Function pointer types for backends that load the core with LoadLibrary
typedef int32_t(SEO_CALL* SEO_GetApiVersion_t)(void);
typedef int32_t(SEO_CALL* SEO_Initialize_t)(const SEO_InitParams*);
typedef int32_t(SEO_CALL* SEO_GetState_t)(void);
typedef void(SEO_CALL* SEO_Shutdown_t)(void);
typedef void(SEO_CALL* SEO_Resize_t)(int32_t, int32_t);
typedef int32_t(SEO_CALL* SEO_AcquireFrame_t)(SEO_Frame*, uint64_t);
typedef void(SEO_CALL* SEO_ReleaseFrame_t)(void);
typedef void(SEO_CALL* SEO_SetVisible_t)(int32_t);
typedef int32_t(SEO_CALL* SEO_IsVisible_t)(void);
typedef void(SEO_CALL* SEO_SetInputMode_t)(int32_t);
typedef int32_t(SEO_CALL* SEO_GetInputMode_t)(void);
typedef void(SEO_CALL* SEO_SendMouseMove_t)(int32_t, int32_t, uint32_t, int32_t);
typedef void(SEO_CALL* SEO_SendMouseButton_t)(int32_t, int32_t, uint32_t, int32_t, int32_t, int32_t);
typedef void(SEO_CALL* SEO_SendMouseWheel_t)(int32_t, int32_t, uint32_t, int32_t, int32_t);
typedef void(SEO_CALL* SEO_SendKey_t)(int32_t, int32_t, int32_t, uint32_t, uint16_t, int32_t);
typedef void(SEO_CALL* SEO_SetFocus_t)(int32_t);
typedef void(SEO_CALL* SEO_PostToUi_t)(const char*);
typedef int32_t(SEO_CALL* SEO_PollFromUi_t)(char*, int32_t);
typedef void(SEO_CALL* SEO_Log_t)(const char*);

#ifdef __cplusplus
}  // extern "C"
#endif
