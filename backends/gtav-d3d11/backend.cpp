// StreamEmber.Overlay.GTAV.asi — GTA V (Legacy, D3D11) backend of the StreamEmber overlay.
//
// Loaded by ScriptHookV's ASI loader from the game folder. It
//   1. registers ScriptHookV's IDXGISwapChain::Present callback (must happen in DllMain),
//   2. on the first Present: reads StreamEmber\Config\Overlay.ini, hooks the game window, and (unless
//      TestPattern=1) loads StreamEmber\Overlay\StreamEmber.Overlay.dll on a worker thread and starts CEF,
//   3. every Present: uploads the newest CEF frame and draws it over the game with the game's own D3D11 device.
// Natives can't be called from the Present callback; game data must come from scripts through the bridge.
// Everything game independent is in backends/common.
#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <string>

#include <main.h>  // ScriptHookV SDK

#include "backend_common.h"
#include "d3d11_renderer.h"
#include "input.h"

namespace seo_gtav {
namespace {

using namespace seo_backend;

HMODULE g_module = nullptr;
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_disabled{false};
Renderer* g_renderer = nullptr;  // created on the render thread, never destroyed (process exit)

void InitializeOnFirstPresent(IDXGISwapChain* swapChain) {
  if (!BackendStartup(g_module, L"Overlay.Backend.log", "GTA V")) {
    g_disabled.store(true);
    return;
  }
  DXGI_SWAP_CHAIN_DESC desc = {};
  swapChain->GetDesc(&desc);
  BackendAttach(desc.OutputWindow, static_cast<int>(desc.BufferDesc.Width), static_cast<int>(desc.BufferDesc.Height));
}

void DrawFrame(IDXGISwapChain* swapChain) {
  DXGI_SWAP_CHAIN_DESC scDesc = {};
  if (FAILED(swapChain->GetDesc(&scDesc)) || scDesc.BufferDesc.Width == 0 || scDesc.BufferDesc.Height == 0) {
    return;
  }
  ID3D11Device* device = nullptr;
  if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || device == nullptr) {
    return;
  }
  const bool usable = g_renderer->SetDevice(device);
  device->Release();  // the renderer keeps its own reference
  if (!usable ||
      !g_renderer->BeginFrame(static_cast<int>(scDesc.BufferDesc.Width), static_cast<int>(scDesc.BufferDesc.Height))) {
    return;
  }

  ID3D11Texture2D* backBuffer = nullptr;
  if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) {
    return;
  }
  D3D11_TEXTURE2D_DESC bbDesc = {};
  backBuffer->GetDesc(&bbDesc);
  D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
  rtvDesc.Format = Renderer::RenderTargetFormat(bbDesc.Format);
  rtvDesc.ViewDimension = bbDesc.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
  ID3D11Device* rtvDevice = nullptr;
  backBuffer->GetDevice(&rtvDevice);
  ID3D11RenderTargetView* rtv = nullptr;
  // Created and released every frame: holding a back buffer reference would make the game's ResizeBuffers fail.
  const HRESULT hr = rtvDevice->CreateRenderTargetView(backBuffer, &rtvDesc, &rtv);
  rtvDevice->Release();
  backBuffer->Release();
  if (FAILED(hr)) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      char buf[16];
      wsprintfA(buf, "0x%08lX", static_cast<unsigned long>(hr));
      BLogError(std::string("CreateRenderTargetView failed: ") + buf + " (back buffer format " +
                std::to_string(static_cast<int>(bbDesc.Format)) + ")");
    }
    return;
  }
  g_renderer->Draw(rtv, /*preserveState=*/true);  // the game's own immediate context
  rtv->Release();
}

void OnPresent(void* swapChainPtr) {
  auto* swapChain = static_cast<IDXGISwapChain*>(swapChainPtr);
  if (swapChain == nullptr || g_disabled.load()) {
    return;
  }
  if (!g_initialized.exchange(true)) {
    InitializeOnFirstPresent(swapChain);
    if (g_disabled.load()) {
      return;
    }
    g_renderer = new Renderer();
  }
  DrawFrame(swapChain);
}

}  // namespace
}  // namespace seo_gtav

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
  switch (reason) {
    case DLL_PROCESS_ATTACH:
      seo_gtav::g_module = module;
      DisableThreadLibraryCalls(module);
      presentCallbackRegister(&seo_gtav::OnPresent);  // SHV: must be called on dll attach
      break;
    case DLL_PROCESS_DETACH:
      presentCallbackUnregister(&seo_gtav::OnPresent);  // SHV: must be called on dll detach
      if (reserved == nullptr) {
        // FreeLibrary (not process exit): put the window procedure back. CEF is left running; it cannot be shut
        // down safely under the loader lock.
        seo_backend::UninstallInputHook();
      }
      break;
    default:
      break;
  }
  return TRUE;
}
