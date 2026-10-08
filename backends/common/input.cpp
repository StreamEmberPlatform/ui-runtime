#include "input.h"

#include <windowsx.h>

#include <atomic>
#include <cstdlib>

#include "backend_common.h"

namespace seo_backend {
namespace {

std::atomic<HWND> g_window{nullptr};
std::atomic<WNDPROC> g_originalProc{nullptr};

// Double/triple click detection (the game window class may not have CS_DBLCLKS)
struct ClickTracker {
  int button = -1;
  int x = 0;
  int y = 0;
  DWORD time = 0;
  int count = 0;

  int OnDown(int b, int px, int py) {
    const DWORD now = GetTickCount();
    if (b == button && now - time <= GetDoubleClickTime() && std::abs(px - x) <= 4 && std::abs(py - y) <= 4) {
      count = count >= 3 ? 1 : count + 1;
    } else {
      count = 1;
    }
    button = b;
    x = px;
    y = py;
    time = now;
    return count;
  }
};
ClickTracker g_clicks;

uint32_t KeyboardModifiers() {
  uint32_t m = 0;
  if (GetKeyState(VK_SHIFT) & 0x8000) m |= SEO_MOD_SHIFT;
  if (GetKeyState(VK_CONTROL) & 0x8000) m |= SEO_MOD_CONTROL;
  if (GetKeyState(VK_MENU) & 0x8000) m |= SEO_MOD_ALT;
  if (GetKeyState(VK_CAPITAL) & 0x0001) m |= SEO_MOD_CAPS_LOCK;
  if (GetKeyState(VK_NUMLOCK) & 0x0001) m |= SEO_MOD_NUM_LOCK;
  return m;
}

bool IsKeyDown(int vk) {
  return (GetKeyState(vk) & 0x8000) != 0;
}

// Same rules as CEF's cefclient (GetCefKeyboardModifiers): key location (left/right/keypad) per key.
uint32_t KeyEventModifiers(WPARAM wParam, LPARAM lParam) {
  uint32_t m = KeyboardModifiers();
  const bool extended = (lParam & (1 << 24)) != 0;
  switch (wParam) {
    case VK_RETURN:
      if (extended) m |= SEO_MOD_IS_KEY_PAD;
      break;
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
      if (!extended) m |= SEO_MOD_IS_KEY_PAD;
      break;
    case VK_NUMLOCK:
    case VK_NUMPAD0:
    case VK_NUMPAD1:
    case VK_NUMPAD2:
    case VK_NUMPAD3:
    case VK_NUMPAD4:
    case VK_NUMPAD5:
    case VK_NUMPAD6:
    case VK_NUMPAD7:
    case VK_NUMPAD8:
    case VK_NUMPAD9:
    case VK_DIVIDE:
    case VK_MULTIPLY:
    case VK_SUBTRACT:
    case VK_ADD:
    case VK_DECIMAL:
    case VK_CLEAR:
      m |= SEO_MOD_IS_KEY_PAD;
      break;
    case VK_SHIFT:
      if (IsKeyDown(VK_LSHIFT)) m |= SEO_MOD_IS_LEFT;
      else if (IsKeyDown(VK_RSHIFT)) m |= SEO_MOD_IS_RIGHT;
      break;
    case VK_CONTROL:
      if (IsKeyDown(VK_LCONTROL)) m |= SEO_MOD_IS_LEFT;
      else if (IsKeyDown(VK_RCONTROL)) m |= SEO_MOD_IS_RIGHT;
      break;
    case VK_MENU:
      if (IsKeyDown(VK_LMENU)) m |= SEO_MOD_IS_LEFT;
      else if (IsKeyDown(VK_RMENU)) m |= SEO_MOD_IS_RIGHT;
      break;
    case VK_LWIN:
      m |= SEO_MOD_IS_LEFT;
      break;
    case VK_RWIN:
      m |= SEO_MOD_IS_RIGHT;
      break;
    default:
      break;
  }
  return m;
}

uint32_t MouseModifiers(WPARAM wParam) {
  uint32_t m = KeyboardModifiers();
  if (wParam & MK_LBUTTON) m |= SEO_MOD_LEFT_MOUSE;
  if (wParam & MK_MBUTTON) m |= SEO_MOD_MIDDLE_MOUSE;
  if (wParam & MK_RBUTTON) m |= SEO_MOD_RIGHT_MOUSE;
  return m;
}

// Client coordinates -> back buffer pixels (they differ when the window is scaled)
void ToView(HWND window, int clientX, int clientY, int& x, int& y) {
  RECT rc = {};
  GetClientRect(window, &rc);
  const int cw = rc.right - rc.left;
  const int ch = rc.bottom - rc.top;
  const int bw = g_backBufferWidth.load();
  const int bh = g_backBufferHeight.load();
  x = (cw > 0 && bw > 0) ? MulDiv(clientX, bw, cw) : clientX;
  y = (ch > 0 && bh > 0) ? MulDiv(clientY, bh, ch) : clientY;
}

// Returns true if the message was consumed by the UI.
bool ForwardToUi(const CoreApi* core, HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_MOUSEMOVE: {
      int x, y;
      ToView(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), x, y);
      core->SendMouseMove(x, y, MouseModifiers(wParam), 0);
      return true;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
      int x, y;
      ToView(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), x, y);
      const bool up = msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP;
      const int button = (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK || msg == WM_RBUTTONUP) ? SEO_MOUSE_RIGHT
                         : (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONDBLCLK || msg == WM_MBUTTONUP) ? SEO_MOUSE_MIDDLE
                                                                                                    : SEO_MOUSE_LEFT;
      const int clicks = up ? (g_clicks.button == button ? g_clicks.count : 1) : g_clicks.OnDown(button, x, y);
      if (!up) {
        SetCapture(window);
      } else {
        ReleaseCapture();
      }
      core->SendMouseButton(x, y, MouseModifiers(wParam), button, up ? 1 : 0, clicks);
      return true;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
      POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};  // screen coordinates for wheel messages
      ScreenToClient(window, &pt);
      int x, y;
      ToView(window, pt.x, pt.y, x, y);
      const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
      core->SendMouseWheel(x, y, MouseModifiers(GET_KEYSTATE_WPARAM(wParam)), msg == WM_MOUSEHWHEEL ? delta : 0,
                           msg == WM_MOUSEWHEEL ? delta : 0);
      return true;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR: {
      const bool system = msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_SYSCHAR;
      int type = SEO_KEY_RAWKEYDOWN;
      if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        type = SEO_KEY_KEYUP;
      } else if (msg == WM_CHAR || msg == WM_SYSCHAR) {
        type = SEO_KEY_CHAR;
      }
      uint32_t mods = KeyEventModifiers(wParam, lParam);
      if (type == SEO_KEY_RAWKEYDOWN && (lParam & (1 << 30))) {
        mods |= SEO_MOD_IS_REPEAT;
      }
      // AltGr (Ctrl+Alt) characters such as '@' on the Turkish layout: drop Ctrl/Alt from the CHAR event,
      // otherwise the page treats them as shortcuts and nothing is typed (same as cefclient).
      if (type == SEO_KEY_CHAR && IsKeyDown(VK_RMENU)) {
        const SHORT scan = VkKeyScanExW(static_cast<WCHAR>(wParam), GetKeyboardLayout(0));
        if (scan != -1 && ((HIBYTE(scan) & (2 | 4)) == (2 | 4))) {
          mods &= ~(SEO_MOD_CONTROL | SEO_MOD_ALT);
        }
      }
      core->SendKey(type, static_cast<int32_t>(wParam), static_cast<int32_t>(lParam), mods,
                    static_cast<uint16_t>(type == SEO_KEY_CHAR ? wParam : 0), system ? 1 : 0);
      // Let Alt+F4 / Alt+Tab reach the system
      return !(system && (wParam == VK_F4 || wParam == VK_TAB));
    }
    default:
      return false;
  }
}

LRESULT CALLBACK OverlayWndProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
  const WNDPROC original = g_originalProc.load();
  const Config& cfg = GetConfig();

  // Hotkeys (first press only, not auto-repeat)
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(lParam & (1 << 30))) {
    const int key = static_cast<int>(wParam);
    if (cfg.keyToggleVisible != 0 && key == cfg.keyToggleVisible) {
      ToggleOverlayVisible();
      return 0;
    }
    if (cfg.keyToggleInput != 0 && key == cfg.keyToggleInput) {
      ToggleUiInputMode();
      return 0;
    }
  }
  if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && wParam != 0 &&
      (static_cast<int>(wParam) == cfg.keyToggleVisible || static_cast<int>(wParam) == cfg.keyToggleInput)) {
    return 0;
  }

  // Cursor tracking for our own cursor
  if (msg == WM_MOUSEMOVE) {
    int x, y;
    ToView(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), x, y);
    g_cursorX.store(x);
    g_cursorY.store(y);
  }

  if (IsOverlayVisible() && IsUiInputMode()) {
    if (const CoreApi* core = GetCore()) {
      if (ForwardToUi(core, window, msg, wParam, lParam)) {
        return 0;
      }
    } else if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) {
      return 0;  // test pattern mode: just keep the mouse away from the game
    }
    // The game reads mouse look / movement from raw input: hide it while the UI owns the input.
    if (msg == WM_INPUT && cfg.blockRawInputInUiMode) {
      return DefWindowProcW(window, msg, wParam, lParam);
    }
  }

  return original != nullptr ? CallWindowProcW(original, window, msg, wParam, lParam)
                             : DefWindowProcW(window, msg, wParam, lParam);
}

}  // namespace

void InstallInputHook(HWND window) {
  if (window == nullptr || g_window.load() == window) {
    return;
  }
  if (g_window.load() != nullptr) {
    UninstallInputHook();  // the game recreated its window
  }
  // Store the current procedure BEFORE swapping: the window thread may dispatch a message to our procedure
  // right after SetWindowLongPtrW, and it must already know where to forward it.
  g_originalProc.store(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC)));
  SetLastError(0);
  const auto previous = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&OverlayWndProc)));
  if (previous == nullptr && GetLastError() != 0) {
    BLogError("SetWindowLongPtrW(GWLP_WNDPROC) failed: " + std::to_string(GetLastError()));
    return;
  }
  g_originalProc.store(previous);  // the value actually replaced (normally identical)
  g_window.store(window);
  BLogInfo("Input hook installed.");
}

void UninstallInputHook() {
  const HWND window = g_window.exchange(nullptr);
  const WNDPROC original = g_originalProc.load();
  if (window == nullptr || original == nullptr) {
    return;
  }
  // Only restore if nobody hooked the window after us; otherwise we would cut their hook off.
  if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC)) == &OverlayWndProc) {
    SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
  }
}

}  // namespace seo_backend
