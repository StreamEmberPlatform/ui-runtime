// StreamEmber Overlay — D3D11 renderer shared by the backends. Runs on the game's render thread, right before
// the swap chain presents: GTA V (native D3D11) and RDR2 (D3D12 through D3D11On12).
#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>

#include "backend_common.h"

namespace seo_backend {

class Renderer {
 public:
  // Binds the renderer to a device (takes its own reference). A different device than last time releases and
  // recreates every resource. Returns false if the renderer cannot draw on this device.
  bool SetDevice(ID3D11Device* device);
  // Once per frame, before any render target is created: keeps the core's view at the back buffer size and
  // uploads the newest UI frame. Returns true if something has to be drawn this frame.
  bool BeginFrame(int width, int height);
  // Draws the overlay onto rtv (a back buffer of the size given to BeginFrame). preserveState saves and restores
  // the context's pipeline state (required when the context is the game's own immediate context).
  // Never throws; on any failure it logs and skips the frame.
  void Draw(ID3D11RenderTargetView* rtv, bool preserveState);
  // Releases every resource and the device reference.
  void Reset() { ReleaseAll(); }

  // Format to use for an RTV on a back buffer (typeless formats resolved to their UNORM/FLOAT variant).
  static DXGI_FORMAT RenderTargetFormat(DXGI_FORMAT format);

 private:
  void ReleaseAll();
  bool CreatePipeline();
  bool CreateStaticTextures();
  bool EnsureUiTexture(int width, int height);
  void UploadUiFrame();
  void DrawSprites(const CoreApi* core);
  // Screen rect in back buffer pixels, texture rect in UV (0..1)
  void DrawQuad(ID3D11ShaderResourceView* srv, bool swizzle, float left, float top, float right, float bottom,
                float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f, float alpha = 1.0f);

  ID3D11Device* device_ = nullptr;           // owned reference
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
  bool drawUi_ = false;          // decided in BeginFrame, used by Draw
  bool drawTest_ = false;
  bool drawCursor_ = false;
  bool failed_ = false;          // pipeline creation failed permanently for this device
};

}  // namespace seo_backend
