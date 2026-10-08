// StreamEmber Overlay — GTA V (D3D11) renderer. Runs only inside ScriptHookV's IDXGISwapChain::Present callback.
#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>

#include "backend_common.h"

namespace seo_gtav {

class Renderer {
 public:
  // Draws the overlay onto the swap chain's back buffer. Never throws; on any failure it logs and skips the frame.
  void OnPresent(IDXGISwapChain* swapChain);

 private:
  bool EnsureResources(IDXGISwapChain* swapChain);
  void ReleaseAll();
  bool CreatePipeline();
  bool CreateStaticTextures();
  bool EnsureUiTexture(int width, int height);
  void UploadUiFrame();
  void DrawSprites(const CoreApi* core);
  // Screen rect in back buffer pixels, texture rect in UV (0..1)
  void DrawQuad(ID3D11ShaderResourceView* srv, bool swizzle, float left, float top, float right, float bottom,
                float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f, float alpha = 1.0f);

  ID3D11Device* device_ = nullptr;           // not owned beyond the frame check (we AddRef via GetDevice)
  ID3D11DeviceContext* context_ = nullptr;
  ID3D11VertexShader* vs_ = nullptr;
  ID3D11PixelShader* ps_ = nullptr;
  ID3D11Buffer* constants_ = nullptr;
  ID3D11SamplerState* sampler_ = nullptr;
  ID3D11BlendState* blend_ = nullptr;
  ID3D11RasterizerState* raster_ = nullptr;
  ID3D11DepthStencilState* depth_ = nullptr;

  ID3D11Texture2D* uiTexture_ = nullptr;
  ID3D11ShaderResourceView* uiSrv_ = nullptr;
  int uiWidth_ = 0;
  int uiHeight_ = 0;
  bool uiSwizzle_ = false;       // true when the texture is RGBA and the shader must swap R/B
  bool uiHasContent_ = false;
  bool uiNeedsFullUpload_ = true;  // texture (re)created: dirty rects are not enough
  uint64_t uiSerial_ = 0;
  SEO_Sprite sprites_[SEO_MAX_SPRITES];

  ID3D11ShaderResourceView* testSrv_ = nullptr;
  ID3D11ShaderResourceView* cursorSrv_ = nullptr;

  int width_ = 0;
  int height_ = 0;
  bool failed_ = false;          // pipeline creation failed permanently for this device
};

}  // namespace seo_gtav
