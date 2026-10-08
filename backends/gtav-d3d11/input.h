// StreamEmber Overlay — GTA V backend: window message hook (hotkeys, cursor tracking, UI input forwarding).
#pragma once

#include <windows.h>

namespace seo_gtav {

// Subclasses the game window (idempotent). Call from the render thread once the window is known.
void InstallInputHook(HWND window);
// Restores the original window procedure if ours is still installed.
void UninstallInputHook();

}  // namespace seo_gtav
