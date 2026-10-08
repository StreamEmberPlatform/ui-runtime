// StreamEmber.Overlay.RDR2.asi — Red Dead Redemption 2 (DirectX 12) backend of the StreamEmber overlay.
//
// ScriptHookRDR2 has no present callback (unlike ScriptHookV), so this backend hooks DXGI/D3D12 itself (MinHook):
//   IDXGIFactory::CreateSwapChain / IDXGIFactory2::CreateSwapChainForHwnd
//       the game's swap chain queue (= the queue D3D11On12 must submit on); drop our back buffer references
//       before the game re-creates its swap chain on the same window
//   IDXGISwapChain::Present / IDXGISwapChain1::Present1   draw the overlay right before the game presents
//   IDXGISwapChain::ResizeBuffers / IDXGISwapChain3::ResizeBuffers1   drop our back buffer references first
//   ID3D12CommandQueue::ExecuteCommandLists   fallback queue discovery when the swap chain already existed
// Drawing reuses the D3D11 renderer of backends/common through D3D11On12 (wrapped back buffers).
//
// Loaded by the ASI loader (dinput8.dll shipped with ScriptHookRDR2) from the game folder. The hooks are
// installed from a worker thread (never from DllMain). Vulkan mode is not supported: then no DXGI Present is
// normally seen and the log says so.
//
// Robustness rules:
//   - All swap chain state lives in g_state and is only touched under g_stateLock (a CRITICAL_SECTION: plain
//     Enter/Leave calls, so SEH-guarded functions can use it without C++ unwinding).
//   - Every call into our drawing code from a game hook is SEH guarded; a fault disables the overlay for the session
//     and releases our references so the game's swap chain is never pinned.
//   - We attach to one window only; other swap chains in the process are ignored.
#include <windows.h>

#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <MinHook.h>

#include "backend_common.h"
#include "d3d11_renderer.h"
#include "input.h"

namespace seo_rdr2 {
namespace {

using namespace seo_backend;

template <typename T>
void SafeRelease(T*& p) {
  if (p != nullptr) {
    p->Release();
    p = nullptr;
  }
}

std::string Hex(unsigned long value) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%08lX", value);
  return buf;
}

// --- Hook plumbing --------------------------------------------------------------------------------------------

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ResizeBuffers1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT,
                                                     const UINT*, IUnknown* const*);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*,
                                                      IDXGISwapChain**);
using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND,
                                                             const DXGI_SWAP_CHAIN_DESC1*,
                                                             const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
                                                             IDXGISwapChain1**);

// vtable indices (declaration order in dxgi.h / dxgi1_2.h / dxgi1_4.h / d3d12.h; IUnknown = 0..2)
constexpr int kPresentIndex = 8;                  // IDXGISwapChain::Present
constexpr int kResizeBuffersIndex = 13;           // IDXGISwapChain::ResizeBuffers
constexpr int kPresent1Index = 22;                // IDXGISwapChain1::Present1
constexpr int kResizeBuffers1Index = 39;          // IDXGISwapChain3::ResizeBuffers1
constexpr int kExecuteCommandListsIndex = 10;     // ID3D12CommandQueue::ExecuteCommandLists
constexpr int kCreateSwapChainIndex = 10;         // IDXGIFactory::CreateSwapChain
constexpr int kCreateSwapChainForHwndIndex = 15;  // IDXGIFactory2::CreateSwapChainForHwnd

PresentFn g_origPresent = nullptr;
Present1Fn g_origPresent1 = nullptr;
ResizeBuffersFn g_origResizeBuffers = nullptr;
ResizeBuffers1Fn g_origResizeBuffers1 = nullptr;
ExecuteCommandListsFn g_origExecuteCommandLists = nullptr;
CreateSwapChainFn g_origCreateSwapChain = nullptr;
CreateSwapChainForHwndFn g_origCreateSwapChainForHwnd = nullptr;

HMODULE g_module = nullptr;

// --- Diagnostics: StreamEmber\Logs\Overlay.Diag.log -----------------------------------------------------------
// DXGI events (swap chain creation, resizes, failed presents, device removal), a heartbeat, and every serious
// exception with the module it happened in. Written raw and allocation free, so the vectored exception handler can
// use it too. Capped, so a stream of first-chance exceptions cannot fill the disk.
HANDLE g_diag = INVALID_HANDLE_VALUE;
volatile LONG g_diagLines = 0;
constexpr LONG kMaxDiagLines = 2000;
std::atomic<unsigned long long> g_presentCount{0};
std::atomic<unsigned long long> g_drawCount{0};
std::atomic<DWORD> g_lastPresentThread{0};
std::atomic<unsigned long long> g_lastHeartbeat{0};
volatile LONG g_failedPresents = 0;
std::atomic<long> g_lastPresentStatus{0};  // last non-failure Present result (S_OK, DXGI_STATUS_OCCLUDED, ...)

void Diag(const char* format, ...) {
  if (g_diag == INVALID_HANDLE_VALUE || InterlockedIncrement(&g_diagLines) > kMaxDiagLines) {
    return;
  }
  char line[1100];
  SYSTEMTIME t;
  GetLocalTime(&t);
  int n = wsprintfA(line, "[%02u:%02u:%02u.%03u] [T%lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                    GetCurrentThreadId());
  va_list args;
  va_start(args, format);
  n += wvsprintfA(line + n, format, args);  // at most 1024 chars
  va_end(args);
  line[n++] = '\r';
  line[n++] = '\n';
  DWORD written = 0;
  WriteFile(g_diag, line, static_cast<DWORD>(n), &written, nullptr);
}

void DiagOpen() {
  wchar_t dir[MAX_PATH] = {};
  if (GetModuleFileNameW(g_module, dir, MAX_PATH) == 0) return;
  wchar_t* slash = wcsrchr(dir, L'\\');
  if (slash == nullptr) return;
  *slash = 0;
  const wchar_t* parts[] = {L"\\StreamEmber", L"\\Logs"};
  for (const wchar_t* part : parts) {
    if (lstrlenW(dir) + lstrlenW(part) + 40 >= MAX_PATH) return;
    lstrcatW(dir, part);
    CreateDirectoryW(dir, nullptr);
  }
  wchar_t previous[MAX_PATH] = {};
  lstrcpyW(previous, dir);
  lstrcatW(previous, L"\\Overlay.Diag.previous.log");
  lstrcatW(dir, L"\\Overlay.Diag.log");
  // Keep the last session's diagnostics: the interesting run is usually the one before the restart
  MoveFileExW(dir, previous, MOVEFILE_REPLACE_EXISTING);
  g_diag = CreateFileW(dir, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

const char* ModuleOf(const void* address, char (&buffer)[MAX_PATH], unsigned long long& offset) {
  HMODULE module = nullptr;
  buffer[0] = '?';
  buffer[1] = 0;
  offset = reinterpret_cast<unsigned long long>(address);
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         static_cast<LPCSTR>(address), &module) &&
      module != nullptr && GetModuleFileNameA(module, buffer, MAX_PATH) != 0) {
    offset -= reinterpret_cast<unsigned long long>(module);
  }
  const char* base = strrchr(buffer, '\\');
  return base != nullptr ? base + 1 : buffer;
}

LONG CALLBACK DiagExceptionHandler(EXCEPTION_POINTERS* info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  // Only errors (0xC...); skip C++ throws (0xE06D7363), debugger messages, guard pages used by stacks
  if (code < 0xC0000000u || code == 0xE06D7363u) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  static volatile LONG lastCode = 0;
  static void* volatile lastAddress = nullptr;
  void* address = info->ExceptionRecord->ExceptionAddress;
  if (static_cast<LONG>(code) == lastCode && address == lastAddress) {
    return EXCEPTION_CONTINUE_SEARCH;  // same fault repeating
  }
  lastCode = static_cast<LONG>(code);
  lastAddress = address;
  char module[MAX_PATH];
  unsigned long long offset = 0;
  const char* name = ModuleOf(address, module, offset);
  if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
    const unsigned long long target = info->ExceptionRecord->ExceptionInformation[1];
    Diag("exception 0x%08lX (access violation, %s 0x%08lX%08lX) at %s+0x%08lX%08lX (first chance)", code,
         info->ExceptionRecord->ExceptionInformation[0] == 0 ? "read" :
         info->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "execute",
         static_cast<unsigned long>(target >> 32), static_cast<unsigned long>(target), name,
         static_cast<unsigned long>(offset >> 32), static_cast<unsigned long>(offset));
  } else {
    Diag("exception 0x%08lX at %s+0x%08lX%08lX (first chance)", code, name, static_cast<unsigned long>(offset >> 32),
         static_cast<unsigned long>(offset));
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
std::atomic<bool> g_presentSeen{false};
thread_local int t_inPresent = 0;  // >0 while inside the game's Present (other overlays submit work there too)
thread_local bool t_presentThread = false;  // this thread has called Present (only its submissions are recorded)

// Queue candidates. Each holds a reference of ours, so a pointer is never used after the game released the queue.
// t_lastDirectQueue: last DIRECT queue that submitted work on this thread (the render thread submits its frame on
// the swap chain queue, then presents). g_swapChainQueue: the queue passed to CreateSwapChain* (exact answer).
thread_local ID3D12CommandQueue* t_lastDirectQueue = nullptr;
ID3D12CommandQueue* g_swapChainQueue = nullptr;  // under g_stateLock

CRITICAL_SECTION g_stateLock;

// --- Swap chain state (under g_stateLock) ---------------------------------------------------------------------

struct BackBuffer {
  ID3D11Resource* wrapped = nullptr;
  ID3D11RenderTargetView* rtv = nullptr;
};

struct D3D12State {
  IDXGISwapChain3* swapChain = nullptr;  // weak: identity only
  ID3D12Device* device12 = nullptr;
  ID3D12CommandQueue* queue = nullptr;
  ID3D11Device* device11 = nullptr;
  ID3D11DeviceContext* context11 = nullptr;
  ID3D11On12Device* on12 = nullptr;
  std::vector<BackBuffer> buffers;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  int width = 0;
  int height = 0;
  bool failed = false;        // gave up on this swap chain
  int bufferErrors = 0;       // GetBuffer failures (logged once)
};

D3D12State g_state;
HWND g_window = nullptr;      // the one window we draw on (set on the first D3D12 Present)

// Window state for "the game runs but nothing is on screen" reports (hung / hidden / minimized / layered window).
void DiagWindow(const char* when) {
  HWND window = g_window;
  if (window == nullptr || !IsWindow(window)) {
    Diag("window (%s): none", when);
    return;
  }
  RECT rect = {};
  GetWindowRect(window, &rect);
  const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
  const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
  BYTE alpha = 255;
  DWORD layeredFlags = 0;
  COLORREF key = 0;
  if ((exStyle & WS_EX_LAYERED) != 0) GetLayeredWindowAttributes(window, &key, &alpha, &layeredFlags);
  Diag("window (%s): visible %d, minimized %d, hung %d, foreground %d, rect %ld,%ld %ldx%ld, style 0x%08lX, "
       "exstyle 0x%08lX, layered alpha %u",
       when, IsWindowVisible(window) ? 1 : 0, IsIconic(window) ? 1 : 0, IsHungAppWindow(window) ? 1 : 0,
       GetForegroundWindow() == window ? 1 : 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
       static_cast<unsigned long>(style), static_cast<unsigned long>(exStyle), static_cast<unsigned>(alpha));
}
Renderer* g_renderer = nullptr;
bool g_backendStarted = false;
std::atomic<bool> g_backendDisabled{false};

// Drops our references to the back buffers. useContext=false after a fault: do not touch the D3D11 context again.
void ReleaseBackBuffers(D3D12State& s, bool useContext) {
  for (BackBuffer& b : s.buffers) {
    SafeRelease(b.rtv);
    SafeRelease(b.wrapped);
  }
  s.buffers.clear();
  s.width = s.height = 0;
  s.format = DXGI_FORMAT_UNKNOWN;
  if (useContext && s.context11 != nullptr) {
    // Wrapped resources are destroyed lazily; flush so their last references to the back buffers go away now.
    s.context11->ClearState();
    s.context11->Flush();
  }
}

void ReleaseDevice(D3D12State& s, bool useContext) {
  ReleaseBackBuffers(s, useContext);
  if (g_renderer != nullptr) {
    g_renderer->Reset();
  }
  SafeRelease(s.on12);
  SafeRelease(s.context11);
  SafeRelease(s.device11);
  SafeRelease(s.queue);
  SafeRelease(s.device12);
  s.swapChain = nullptr;
  s.failed = false;
  s.bufferErrors = 0;
}

// Our references to candidate queues keep their device alive; drop them once attached (or on device removal).
void DropQueueCandidates() {
  SafeRelease(t_lastDirectQueue);
  SafeRelease(g_swapChainQueue);
}

bool QueueBelongsTo(ID3D12CommandQueue* q, ID3D12Device* device) {
  if (q == nullptr) return false;
  ID3D12Device* qDevice = nullptr;
  if (FAILED(q->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&qDevice))) || qDevice == nullptr) {
    return false;
  }
  const bool same = qDevice == device;
  qDevice->Release();
  return same && q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT;
}

// The swap chain's queue: exact when we saw CreateSwapChain*, otherwise this thread's last DIRECT queue.
ID3D12CommandQueue* FindGameQueue(ID3D12Device* device, bool& exact) {
  exact = QueueBelongsTo(g_swapChainQueue, device);
  if (exact) return g_swapChainQueue;
  return QueueBelongsTo(t_lastDirectQueue, device) ? t_lastDirectQueue : nullptr;
}

void LogColorSpace(IDXGISwapChain3* swapChain, DXGI_FORMAT format) {
  bool hdr10 = false;
  IDXGIOutput* output = nullptr;
  if (SUCCEEDED(swapChain->GetContainingOutput(&output)) && output != nullptr) {
    IDXGIOutput6* output6 = nullptr;
    if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&output6)))) {
      DXGI_OUTPUT_DESC1 desc = {};
      if (SUCCEEDED(output6->GetDesc1(&desc))) {
        hdr10 = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
      }
      output6->Release();
    }
    output->Release();
  }
  if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
    BLogInfo("HDR (scRGB FP16) back buffer: the overlay will look dim. Turn HDR off in the game for correct colors.");
  } else if (hdr10) {
    BLogInfo("Display is in HDR10 mode: overlay colors will be off. Turn HDR off in the game for correct colors.");
  }
}

bool CreateDevice(D3D12State& s, IDXGISwapChain3* swapChain, ID3D12Device* device12) {
  bool exact = false;
  ID3D12CommandQueue* queue = FindGameQueue(device12, exact);
  if (queue == nullptr) {
    return false;  // no submission seen yet on this thread; try again next frame
  }

  IUnknown* queues[] = {queue};
  ID3D11Device* device11 = nullptr;
  ID3D11DeviceContext* context11 = nullptr;
  const HRESULT hr = D3D11On12CreateDevice(device12, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, queues, 1, 0,
                                           &device11, &context11, nullptr);
  s.swapChain = swapChain;
  if (FAILED(hr)) {
    BLogError("D3D11On12CreateDevice failed: " + Hex(static_cast<unsigned long>(hr)));
    s.failed = true;
    return false;
  }
  ID3D11On12Device* on12 = nullptr;
  if (FAILED(device11->QueryInterface(__uuidof(ID3D11On12Device), reinterpret_cast<void**>(&on12)))) {
    BLogError("ID3D11On12Device not available.");
    context11->Release();
    device11->Release();
    s.failed = true;
    return false;
  }

  s.device12 = device12;
  s.device12->AddRef();
  s.queue = queue;
  s.queue->AddRef();
  s.device11 = device11;
  s.context11 = context11;
  s.on12 = on12;
  BLogInfo(std::string("D3D12 swap chain attached (D3D11On12 on the game's DIRECT queue, ") +
           (exact ? "from CreateSwapChain)." : "from the present thread's submissions)."));
  DropQueueCandidates();  // s.queue holds the one we use
  if (!g_renderer->SetDevice(device11)) {
    s.failed = true;
    return false;
  }
  return true;
}

bool EnsureBackBuffers(D3D12State& s, IDXGISwapChain3* swapChain) {
  DXGI_SWAP_CHAIN_DESC desc = {};
  if (FAILED(swapChain->GetDesc(&desc)) || desc.BufferCount == 0 || desc.BufferDesc.Width == 0 ||
      desc.BufferDesc.Height == 0) {
    return false;
  }
  const int width = static_cast<int>(desc.BufferDesc.Width);
  const int height = static_cast<int>(desc.BufferDesc.Height);
  if (!s.buffers.empty() && s.buffers.size() == desc.BufferCount && s.width == width && s.height == height &&
      s.format == desc.BufferDesc.Format) {
    return true;
  }
  ReleaseBackBuffers(s, true);

  for (UINT i = 0; i < desc.BufferCount; ++i) {
    ID3D12Resource* resource = nullptr;
    HRESULT hr = swapChain->GetBuffer(i, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&resource));
    if (FAILED(hr)) {
      if (s.bufferErrors++ == 0) {
        BLogError("GetBuffer(" + std::to_string(i) + ") failed: " + Hex(static_cast<unsigned long>(hr)) +
                  " (retrying silently)");
      }
      ReleaseBackBuffers(s, true);
      return false;
    }
    BackBuffer b;
    D3D11_RESOURCE_FLAGS flags = {};
    flags.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = s.on12->CreateWrappedResource(resource, &flags, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT,
                                       __uuidof(ID3D11Resource), reinterpret_cast<void**>(&b.wrapped));
    resource->Release();
    if (FAILED(hr)) {
      BLogError("CreateWrappedResource failed: " + Hex(static_cast<unsigned long>(hr)) + " (format " +
                std::to_string(static_cast<int>(desc.BufferDesc.Format)) + ")");
      ReleaseBackBuffers(s, true);
      s.failed = true;
      return false;
    }
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = Renderer::RenderTargetFormat(desc.BufferDesc.Format);
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    hr = s.device11->CreateRenderTargetView(b.wrapped, &rtvDesc, &b.rtv);
    if (FAILED(hr)) {
      BLogError("CreateRenderTargetView failed: " + Hex(static_cast<unsigned long>(hr)) + " (format " +
                std::to_string(static_cast<int>(desc.BufferDesc.Format)) + ")");
      SafeRelease(b.wrapped);
      ReleaseBackBuffers(s, true);
      s.failed = true;
      return false;
    }
    s.buffers.push_back(b);
  }
  s.width = width;
  s.height = height;
  s.format = desc.BufferDesc.Format;
  s.bufferErrors = 0;
  BLogInfo("Back buffers: " + std::to_string(desc.BufferCount) + " x " + std::to_string(width) + "x" +
           std::to_string(height) + ", format " + std::to_string(static_cast<int>(desc.BufferDesc.Format)));
  LogColorSpace(swapChain, desc.BufferDesc.Format);
  return true;
}

// Called with g_stateLock held. swapChain is a D3D12 swap chain (checked by the caller).
void DrawOverlay(IDXGISwapChain3* swapChain, ID3D12Device* device12) {
  D3D12State& s = g_state;
  DXGI_SWAP_CHAIN_DESC desc = {};
  if (FAILED(swapChain->GetDesc(&desc))) {
    return;
  }

  if (!g_backendStarted) {
    g_backendStarted = true;
    g_backendDisabled = !BackendStartup(g_module, L"Overlay.Backend.log", "RDR2 (D3D12)");
    if (g_backendDisabled) {
      return;
    }
    g_window = desc.OutputWindow;
    BackendAttach(desc.OutputWindow, static_cast<int>(desc.BufferDesc.Width), static_cast<int>(desc.BufferDesc.Height));
    g_renderer = new Renderer();
  }
  if (desc.OutputWindow != g_window) {
    if (IsWindow(g_window)) {
      return;  // another window's swap chain (launcher, other overlay, ...): not ours
    }
    // The game re-created its window: follow it
    BLogInfo("Game window re-created; re-attaching.");
    ReleaseDevice(s, true);
    g_window = desc.OutputWindow;
    InstallInputHook(g_window);
  }
  if (device12->GetDeviceRemovedReason() != S_OK) {
    if (s.device11 != nullptr) {
      Diag("D3D12 device removed, reason 0x%08lX", static_cast<unsigned long>(device12->GetDeviceRemovedReason()));
      BLogError("D3D12 device removed: " + Hex(static_cast<unsigned long>(device12->GetDeviceRemovedReason())));
      ReleaseDevice(s, false);  // let the game get a fresh device
    }
    DropQueueCandidates();
    return;
  }

  if (s.swapChain != swapChain) {
    if (s.swapChain != nullptr) {
      BLogInfo("Swap chain changed; re-attaching.");
    }
    ReleaseDevice(s, true);
  }
  if (s.failed) {
    return;
  }
  if (s.device11 == nullptr && !CreateDevice(s, swapChain, device12)) {
    return;
  }
  if (!EnsureBackBuffers(s, swapChain)) {
    return;
  }

  if (g_renderer->BeginFrame(s.width, s.height)) {
    const UINT index = swapChain->GetCurrentBackBufferIndex();
    if (index < s.buffers.size()) {
      BackBuffer& b = s.buffers[index];
      s.on12->AcquireWrappedResources(&b.wrapped, 1);
      g_renderer->Draw(b.rtv, /*preserveState=*/false);  // our own context: nothing of the game's to restore
      s.on12->ReleaseWrappedResources(&b.wrapped, 1);
      s.context11->Flush();  // submits on the game's queue, ahead of the Present below
      g_drawCount.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

void ReportException(const char* where, DWORD code) {
  Diag("overlay disabled after exception 0x%08lX in %s", code, where);
  BLogError(std::string("Exception in ") + where + " (code " + Hex(code) +
            "); overlay drawing disabled for this session.");
}

// After a fault: drop every reference (without touching the possibly broken D3D11 context) so the game's swap
// chain is never pinned by us. Guarded itself; leaks rather than crashes if even that faults.
void DropAfterFault() {
  __try {
    ReleaseDevice(g_state, /*useContext=*/false);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

// The functions below contain no C++ objects with destructors (required for __try, C2712).
void GuardedDraw(IDXGISwapChain3* swapChain, ID3D12Device* device12) {
  DWORD code = 0;
  EnterCriticalSection(&g_stateLock);
  if (!g_backendDisabled) {
    __try {
      DrawOverlay(swapChain, device12);
    } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
      g_backendDisabled = true;
    }
    if (code != 0) {
      DropAfterFault();
    }
  }
  LeaveCriticalSection(&g_stateLock);
  if (code != 0) {
    ReportException("Present", code);
  }
}

void GuardedReleaseFor(IDXGISwapChain3* swapChain, HWND window) {
  DWORD code = 0;
  EnterCriticalSection(&g_stateLock);
  __try {
    const bool ours = (swapChain != nullptr && g_state.swapChain == swapChain) ||
                      (window != nullptr && g_state.swapChain != nullptr &&
                       (window == g_window || !IsWindow(g_window)));
    if (ours) {
      if (window != nullptr) {
        ReleaseDevice(g_state, !g_backendDisabled);  // swap chain re-created: start over with the new one
      } else {
        ReleaseBackBuffers(g_state, !g_backendDisabled);  // ResizeBuffers fails while anyone holds a back buffer
      }
    }
  } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    g_backendDisabled = true;
  }
  if (code != 0) {
    DropAfterFault();
  }
  LeaveCriticalSection(&g_stateLock);
  if (code != 0) {
    ReportException("ResizeBuffers/CreateSwapChain", code);
  }
}

void PresentBookkeeping() {
  const unsigned long long presents = g_presentCount.fetch_add(1, std::memory_order_relaxed) + 1;
  const DWORD thread = GetCurrentThreadId();
  const DWORD previous = g_lastPresentThread.exchange(thread);
  if (previous != thread) {
    Diag("present thread %lu -> %lu (present #%lu)", previous, thread, static_cast<unsigned long>(presents));
  }
  const unsigned long long now = GetTickCount64();
  unsigned long long last = g_lastHeartbeat.load();
  if (now - last >= 30000 && g_lastHeartbeat.compare_exchange_strong(last, now)) {
    Diag("heartbeat: presents %lu, overlay draws %lu, disabled %d", static_cast<unsigned long>(presents),
         static_cast<unsigned long>(g_drawCount.load()), g_backendDisabled ? 1 : 0);
    DiagWindow("heartbeat");
  }
}

void AfterPresent(IDXGISwapChain* swapChain, HRESULT hr) {
  // Success codes matter too: DXGI_STATUS_OCCLUDED etc. mean the frame was not shown
  if (SUCCEEDED(hr)) {
    const long previous = g_lastPresentStatus.exchange(static_cast<long>(hr));
    if (previous != static_cast<long>(hr)) {
      Diag("Present status 0x%08lX -> 0x%08lX", static_cast<unsigned long>(previous), static_cast<unsigned long>(hr));
      DiagWindow("present status changed");
    }
  }
  if (FAILED(hr) && InterlockedIncrement(&g_failedPresents) <= 20) {
    unsigned long reason = 0;
    ID3D12Device* device12 = nullptr;
    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device12)))) {
      reason = static_cast<unsigned long>(device12->GetDeviceRemovedReason());
      device12->Release();
    }
    Diag("Present failed 0x%08lX (device removed reason 0x%08lX)", static_cast<unsigned long>(hr), reason);
  }
}

void OnPresentCommon(IDXGISwapChain* rawSwapChain, UINT flags) {
  g_presentSeen.store(true);
  PresentBookkeeping();
  if ((flags & DXGI_PRESENT_TEST) != 0 || t_inPresent > 1 || g_backendDisabled || rawSwapChain == nullptr) {
    return;
  }
  IDXGISwapChain3* swapChain = nullptr;
  if (FAILED(rawSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&swapChain)))) {
    return;
  }
  // Only D3D12 swap chains are ours to draw on
  ID3D12Device* device12 = nullptr;
  if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device12)))) {
    GuardedDraw(swapChain, device12);
    device12->Release();
  }
  swapChain->Release();  // outside the guarded region: always released
}

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
  t_presentThread = true;
  ++t_inPresent;
  OnPresentCommon(swapChain, flags);
  const HRESULT hr = g_origPresent(swapChain, syncInterval, flags);
  AfterPresent(swapChain, hr);
  --t_inPresent;
  return hr;
}

HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags,
                                       const DXGI_PRESENT_PARAMETERS* params) {
  t_presentThread = true;
  ++t_inPresent;
  OnPresentCommon(swapChain, flags);
  const HRESULT hr = g_origPresent1(swapChain, syncInterval, flags, params);
  AfterPresent(swapChain, hr);
  --t_inPresent;
  return hr;
}

void BeforeResize(IDXGISwapChain* swapChain) {
  IDXGISwapChain3* sc3 = nullptr;
  if (swapChain != nullptr &&
      SUCCEEDED(swapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&sc3)))) {
    GuardedReleaseFor(sc3, nullptr);
    sc3->Release();
  }
}

HRESULT STDMETHODCALLTYPE HookResizeBuffers(IDXGISwapChain* swapChain, UINT count, UINT width, UINT height,
                                            DXGI_FORMAT format, UINT flags) {
  Diag("ResizeBuffers(count %u, %ux%u, format %d, flags 0x%X)", count, width, height, static_cast<int>(format), flags);
  BeforeResize(swapChain);
  const HRESULT hr = g_origResizeBuffers(swapChain, count, width, height, format, flags);
  if (FAILED(hr)) Diag("ResizeBuffers failed 0x%08lX", static_cast<unsigned long>(hr));
  return hr;
}

HRESULT STDMETHODCALLTYPE HookResizeBuffers1(IDXGISwapChain3* swapChain, UINT count, UINT width, UINT height,
                                             DXGI_FORMAT format, UINT flags, const UINT* nodeMasks,
                                             IUnknown* const* queues) {
  Diag("ResizeBuffers1(count %u, %ux%u, format %d, flags 0x%X)", count, width, height, static_cast<int>(format), flags);
  BeforeResize(swapChain);
  const HRESULT hr = g_origResizeBuffers1(swapChain, count, width, height, format, flags, nodeMasks, queues);
  if (FAILED(hr)) Diag("ResizeBuffers1 failed 0x%08lX", static_cast<unsigned long>(hr));
  return hr;
}

// A new swap chain on our window: release everything first (our wrapped back buffers keep the old swap chain
// alive and creating the new one would fail), and remember its queue: for D3D12 'device' IS the command queue.
void BeforeCreateSwapChain(IUnknown* device, HWND window) {
  if (window != nullptr) {
    GuardedReleaseFor(nullptr, window);
  }
  ID3D12CommandQueue* queue = nullptr;
  if (device != nullptr &&
      SUCCEEDED(device->QueryInterface(__uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue)))) {
    EnterCriticalSection(&g_stateLock);
    ID3D12CommandQueue* old = g_swapChainQueue;
    g_swapChainQueue = queue;  // keeps the QueryInterface reference
    LeaveCriticalSection(&g_stateLock);
    if (old != nullptr) old->Release();
  }
}

HRESULT STDMETHODCALLTYPE HookCreateSwapChain(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
                                              IDXGISwapChain** swapChain) {
  if (desc != nullptr) {
    Diag("CreateSwapChain(window %p, %ux%u, format %d, buffers %u, windowed %d)", desc->OutputWindow,
         desc->BufferDesc.Width, desc->BufferDesc.Height, static_cast<int>(desc->BufferDesc.Format), desc->BufferCount,
         desc->Windowed ? 1 : 0);
  }
  BeforeCreateSwapChain(device, desc != nullptr ? desc->OutputWindow : nullptr);
  const HRESULT hr = g_origCreateSwapChain(factory, device, desc, swapChain);
  Diag("CreateSwapChain -> 0x%08lX", static_cast<unsigned long>(hr));
  return hr;
}

HRESULT STDMETHODCALLTYPE HookCreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND window,
                                                     const DXGI_SWAP_CHAIN_DESC1* desc,
                                                     const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
                                                     IDXGIOutput* output, IDXGISwapChain1** swapChain) {
  if (desc != nullptr) {
    Diag("CreateSwapChainForHwnd(window %p, %ux%u, format %d, buffers %u, flags 0x%X, fullscreen desc %d)", window,
         desc->Width, desc->Height, static_cast<int>(desc->Format), desc->BufferCount, desc->Flags,
         fullscreen != nullptr ? (fullscreen->Windowed ? 0 : 1) : -1);
  }
  BeforeCreateSwapChain(device, window);
  const HRESULT hr = g_origCreateSwapChainForHwnd(factory, device, window, desc, fullscreen, output, swapChain);
  Diag("CreateSwapChainForHwnd -> 0x%08lX", static_cast<unsigned long>(hr));
  return hr;
}

void STDMETHODCALLTYPE HookExecuteCommandLists(ID3D12CommandQueue* queue, UINT count,
                                               ID3D12CommandList* const* lists) {
  // Fallback queue discovery: the last DIRECT queue this thread submitted on before presenting. Submissions made
  // inside Present (other overlays, our own D3D11On12 flush) are not the game's. We hold a reference to the
  // recorded queue so it can be inspected later without a use-after-free; it changes rarely.
  // Only threads that present record (no references left behind on worker threads), and only until attached.
  if (t_presentThread && t_inPresent == 0 && g_state.device11 == nullptr && queue != nullptr &&
      queue != t_lastDirectQueue &&
      queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
    queue->AddRef();
    ID3D12CommandQueue* old = t_lastDirectQueue;
    t_lastDirectQueue = queue;
    if (old != nullptr) old->Release();
  }
  g_origExecuteCommandLists(queue, count, lists);
}

// --- Hook installation ----------------------------------------------------------------------------------------

void* VtableEntry(void* object, int index) {
  return (*reinterpret_cast<void***>(object))[index];
}

struct Targets {
  void* present = nullptr;
  void* present1 = nullptr;
  void* resize = nullptr;
  void* resize1 = nullptr;
  void* execute = nullptr;
  void* createSwapChain = nullptr;
  void* createSwapChainForHwnd = nullptr;
};

// Creates a throw-away D3D12 device, queue and swap chain on a hidden window, only to read their vtables.
bool FindTargets(Targets& t) {
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = g_module;
  wc.lpszClassName = L"StreamEmberOverlayDx12Probe";
  RegisterClassExW(&wc);
  HWND window = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr,
                                g_module, nullptr);
  if (window == nullptr) {
    return false;
  }

  bool ok = false;
  IDXGIFactory4* factory = nullptr;
  ID3D12Device* device = nullptr;
  ID3D12CommandQueue* queue = nullptr;
  IDXGISwapChain1* swapChain = nullptr;
  HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory));
  if (SUCCEEDED(hr)) {
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
  }
  if (SUCCEEDED(hr)) {
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue));
  }
  if (SUCCEEDED(hr)) {
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = 64;
    sd.Height = 64;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = factory->CreateSwapChainForHwnd(queue, window, &sd, nullptr, nullptr, &swapChain);
  }
  if (SUCCEEDED(hr)) {
    t.present = VtableEntry(swapChain, kPresentIndex);
    t.resize = VtableEntry(swapChain, kResizeBuffersIndex);
    t.present1 = VtableEntry(swapChain, kPresent1Index);
    IDXGISwapChain3* sc3 = nullptr;
    if (SUCCEEDED(swapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&sc3)))) {
      t.resize1 = VtableEntry(sc3, kResizeBuffers1Index);
      sc3->Release();
    }
    t.execute = VtableEntry(queue, kExecuteCommandListsIndex);
    t.createSwapChain = VtableEntry(factory, kCreateSwapChainIndex);
    t.createSwapChainForHwnd = VtableEntry(factory, kCreateSwapChainForHwndIndex);
    ok = true;
  } else {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "[StreamEmber.Overlay.RDR2] D3D12 probe failed: 0x%08lX\n",
                  static_cast<unsigned long>(hr));
    OutputDebugStringA(buf);
  }

  SafeRelease(swapChain);
  SafeRelease(queue);
  SafeRelease(device);
  SafeRelease(factory);
  DestroyWindow(window);
  UnregisterClassW(wc.lpszClassName, g_module);
  return ok;
}

template <typename T>
bool Hook(void* target, void* detour, T& original, const char* name) {
  if (target == nullptr) {
    BLogError(std::string("Hook target missing: ") + name);
    return false;
  }
  MH_STATUS st = MH_CreateHook(target, detour, reinterpret_cast<void**>(&original));
  if (st == MH_OK) st = MH_EnableHook(target);
  if (st != MH_OK) {
    BLogError(std::string("Hook failed: ") + name + " (" + MH_StatusToString(st) + ")");
    return false;
  }
  return true;
}

void WriteEarlyLog(const char* message) {
  // BackendStartup (which opens the real log) never ran: write a minimal note where the logs normally are
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(g_module, path, MAX_PATH);
  std::wstring dir(path);
  dir = dir.substr(0, dir.find_last_of(L"\\/")) + L"\\StreamEmber\\Logs";
  CreateDirectoryW((dir.substr(0, dir.find_last_of(L"\\/"))).c_str(), nullptr);
  CreateDirectoryW(dir.c_str(), nullptr);
  HANDLE f = CreateFileW((dir + L"\\Overlay.Backend.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(f, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
    CloseHandle(f);
  }
}

void InstallHooksThread() {
  DiagOpen();
  Diag("StreamEmber.Overlay.RDR2 loaded; diagnostics on");
  AddVectoredExceptionHandler(1, &DiagExceptionHandler);
  Targets t;
  if (!FindTargets(t)) {
    WriteEarlyLog("[ERROR] D3D12 is not available on this system; overlay disabled.\r\n");
    return;
  }
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    WriteEarlyLog("[ERROR] MinHook initialization failed; overlay disabled.\r\n");
    return;
  }
  // Order: everything that releases references or finds the queue goes in before Present, so drawing can never
  // start without them. ResizeBuffers1 is mandatory when it exists: drawing holds back buffer references.
  bool ok = Hook(t.execute, reinterpret_cast<void*>(&HookExecuteCommandLists), g_origExecuteCommandLists,
                 "ID3D12CommandQueue::ExecuteCommandLists");
  ok = ok && Hook(t.createSwapChain, reinterpret_cast<void*>(&HookCreateSwapChain), g_origCreateSwapChain,
                  "IDXGIFactory::CreateSwapChain");
  ok = ok && Hook(t.createSwapChainForHwnd, reinterpret_cast<void*>(&HookCreateSwapChainForHwnd),
                  g_origCreateSwapChainForHwnd, "IDXGIFactory2::CreateSwapChainForHwnd");
  ok = ok && Hook(t.resize, reinterpret_cast<void*>(&HookResizeBuffers), g_origResizeBuffers,
                  "IDXGISwapChain::ResizeBuffers");
  if (t.resize1 != nullptr) {
    ok = ok && Hook(t.resize1, reinterpret_cast<void*>(&HookResizeBuffers1), g_origResizeBuffers1,
                    "IDXGISwapChain3::ResizeBuffers1");
  }
  ok = ok && Hook(t.present, reinterpret_cast<void*>(&HookPresent), g_origPresent, "IDXGISwapChain::Present");
  if (ok && t.present1 != nullptr && t.present1 != t.present) {
    Hook(t.present1, reinterpret_cast<void*>(&HookPresent1), g_origPresent1, "IDXGISwapChain1::Present1");
  }
  if (!ok) {
    // Never run half hooked: without the release hooks, drawing would break the game's resize
    MH_DisableHook(MH_ALL_HOOKS);
    WriteEarlyLog("[ERROR] Installing the DXGI/D3D12 hooks failed (another tool may have patched DXGI); overlay "
                  "disabled.\r\n");
    return;
  }
  OutputDebugStringA("[StreamEmber.Overlay.RDR2] DXGI/D3D12 hooks installed.\n");
  Diag("DXGI/D3D12 hooks installed");

  // Watchdog: tell the user why nothing shows up when the game runs on Vulkan
  for (int i = 0; i < 180 && !g_presentSeen.load(); ++i) {
    Sleep(1000);
  }
  if (!g_presentSeen.load()) {
    WriteEarlyLog(
        "[ERROR] No DXGI Present in 3 minutes. RDR2 is probably running on Vulkan: set Graphics API to DirectX 12 "
        "(Settings > Graphics > Advanced) and restart the game.\r\n");
  }
}

}  // namespace
}  // namespace seo_rdr2

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
  switch (reason) {
    case DLL_PROCESS_ATTACH:
      seo_rdr2::g_module = module;
      DisableThreadLibraryCalls(module);
      InitializeCriticalSection(&seo_rdr2::g_stateLock);
      // D3D12 device creation and hooking must not run under the loader lock
      if (HANDLE thread = CreateThread(
              nullptr, 0,
              [](LPVOID) -> DWORD {
                seo_rdr2::InstallHooksThread();
                return 0;
              },
              nullptr, 0, nullptr)) {
        CloseHandle(thread);
      }
      break;
    case DLL_PROCESS_DETACH:
      // Reached on a normal exit (ExitProcess); a crash or TerminateProcess never gets here
      seo_rdr2::Diag("process detach (%s)", reserved != nullptr ? "process exit" : "FreeLibrary");
      if (reserved == nullptr) {
        // FreeLibrary (not process exit; ASIs are normally never unloaded): our code is about to go away, so the
        // hooks must too.
        MH_DisableHook(MH_ALL_HOOKS);
        seo_backend::UninstallInputHook();
      }
      break;
    default:
      break;
  }
  return TRUE;
}
