#include "renderer.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace seo_gtav {
namespace {

template <typename T>
void SafeRelease(T*& p) {
  if (p != nullptr) {
    p->Release();
    p = nullptr;
  }
}

std::string Hex(HRESULT hr) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
  return buf;
}

// Feature level 10.0 compatible (the game may run DX11 with a DX10/10.1 feature level).
const char kShaderSource[] = R"HLSL(
cbuffer Params : register(b0)
{
    float4 rect;     // left, top, right, bottom in NDC
    float4 uvRect;   // u0, v0, u1, v1
    float4 options;  // x: 1 = swap red/blue
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    float2 t = float2(id & 1, id >> 1);
    VSOut o;
    o.pos = float4(lerp(rect.x, rect.z, t.x), lerp(rect.y, rect.w, t.y), 0.0, 1.0);
    o.uv = lerp(uvRect.xy, uvRect.zw, t);
    return o;
}

Texture2D tex : register(t0);
SamplerState smp : register(s0);

float4 PSMain(VSOut i) : SV_Target
{
    float4 c = tex.Sample(smp, i.uv);
    return options.x > 0.5 ? c.bgra : c;   // premultiplied alpha
}
)HLSL";

struct Constants {
  float rect[4];
  float uv[4];
  float options[4];
};

// Saves and restores every piece of pipeline state the overlay touches, so the game never notices us.
struct StateBackup {
  static constexpr UINT kMaxInstances = 256;

  UINT scissorCount = 0;
  UINT viewportCount = 0;
  D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  ID3D11RasterizerState* raster = nullptr;
  ID3D11BlendState* blend = nullptr;
  FLOAT blendFactor[4] = {};
  UINT sampleMask = 0;
  ID3D11DepthStencilState* depth = nullptr;
  UINT stencilRef = 0;
  ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* dsv = nullptr;
  ID3D11ShaderResourceView* psSrv = nullptr;
  ID3D11SamplerState* psSampler = nullptr;
  ID3D11Buffer* psCb = nullptr;
  ID3D11PixelShader* ps = nullptr;
  ID3D11ClassInstance* psInst[kMaxInstances] = {};
  UINT psInstCount = kMaxInstances;
  ID3D11VertexShader* vs = nullptr;
  ID3D11ClassInstance* vsInst[kMaxInstances] = {};
  UINT vsInstCount = kMaxInstances;
  ID3D11Buffer* vsCb = nullptr;
  ID3D11GeometryShader* gs = nullptr;
  ID3D11ClassInstance* gsInst[kMaxInstances] = {};
  UINT gsInstCount = kMaxInstances;
  ID3D11HullShader* hs = nullptr;
  ID3D11ClassInstance* hsInst[kMaxInstances] = {};
  UINT hsInstCount = kMaxInstances;
  ID3D11DomainShader* ds = nullptr;
  ID3D11ClassInstance* dsInst[kMaxInstances] = {};
  UINT dsInstCount = kMaxInstances;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  ID3D11Buffer* ib = nullptr;
  DXGI_FORMAT ibFormat = DXGI_FORMAT_UNKNOWN;
  UINT ibOffset = 0;
  ID3D11Buffer* vb = nullptr;
  UINT vbStride = 0;
  UINT vbOffset = 0;
  ID3D11InputLayout* layout = nullptr;

  void Save(ID3D11DeviceContext* c) {
    scissorCount = viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    c->RSGetScissorRects(&scissorCount, scissors);
    c->RSGetViewports(&viewportCount, viewports);
    c->RSGetState(&raster);
    c->OMGetBlendState(&blend, blendFactor, &sampleMask);
    c->OMGetDepthStencilState(&depth, &stencilRef);
    c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
    c->PSGetShaderResources(0, 1, &psSrv);
    c->PSGetSamplers(0, 1, &psSampler);
    c->PSGetConstantBuffers(0, 1, &psCb);
    c->PSGetShader(&ps, psInst, &psInstCount);
    c->VSGetShader(&vs, vsInst, &vsInstCount);
    c->VSGetConstantBuffers(0, 1, &vsCb);
    c->GSGetShader(&gs, gsInst, &gsInstCount);
    c->HSGetShader(&hs, hsInst, &hsInstCount);
    c->DSGetShader(&ds, dsInst, &dsInstCount);
    c->IAGetPrimitiveTopology(&topology);
    c->IAGetIndexBuffer(&ib, &ibFormat, &ibOffset);
    c->IAGetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);
    c->IAGetInputLayout(&layout);
  }

  void Restore(ID3D11DeviceContext* c) {
    c->RSSetScissorRects(scissorCount, scissors);
    c->RSSetViewports(viewportCount, viewports);
    c->RSSetState(raster);
    c->OMSetBlendState(blend, blendFactor, sampleMask);
    c->OMSetDepthStencilState(depth, stencilRef);
    c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
    c->PSSetShaderResources(0, 1, &psSrv);
    c->PSSetSamplers(0, 1, &psSampler);
    c->PSSetConstantBuffers(0, 1, &psCb);
    c->PSSetShader(ps, psInst, psInstCount);
    c->VSSetShader(vs, vsInst, vsInstCount);
    c->VSSetConstantBuffers(0, 1, &vsCb);
    c->GSSetShader(gs, gsInst, gsInstCount);
    c->HSSetShader(hs, hsInst, hsInstCount);
    c->DSSetShader(ds, dsInst, dsInstCount);
    c->IASetPrimitiveTopology(topology);
    c->IASetIndexBuffer(ib, ibFormat, ibOffset);
    c->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);
    c->IASetInputLayout(layout);
    ReleaseRefs();
  }

  void ReleaseRefs() {
    SafeRelease(raster);
    SafeRelease(blend);
    SafeRelease(depth);
    for (auto& rtv : rtvs) {
      SafeRelease(rtv);
    }
    SafeRelease(dsv);
    SafeRelease(psSrv);
    SafeRelease(psSampler);
    SafeRelease(psCb);
    SafeRelease(ps);
    for (UINT i = 0; i < psInstCount && i < kMaxInstances; ++i) SafeRelease(psInst[i]);
    SafeRelease(vs);
    for (UINT i = 0; i < vsInstCount && i < kMaxInstances; ++i) SafeRelease(vsInst[i]);
    SafeRelease(vsCb);
    SafeRelease(gs);
    for (UINT i = 0; i < gsInstCount && i < kMaxInstances; ++i) SafeRelease(gsInst[i]);
    SafeRelease(hs);
    for (UINT i = 0; i < hsInstCount && i < kMaxInstances; ++i) SafeRelease(hsInst[i]);
    SafeRelease(ds);
    for (UINT i = 0; i < dsInstCount && i < kMaxInstances; ++i) SafeRelease(dsInst[i]);
    SafeRelease(ib);
    SafeRelease(vb);
    SafeRelease(layout);
  }
};

uint32_t PremultipliedBgra(int r, int g, int b, int a) {
  r = r * a / 255;
  g = g * a / 255;
  b = b * a / 255;
  return static_cast<uint32_t>(b) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(a) << 24);
}

// Phase 1 test pattern: a dark glass panel with an accent frame and diagonal stripes.
std::vector<uint32_t> MakeTestPattern(int w, int h) {
  std::vector<uint32_t> px(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const bool border = x < 6 || y < 6 || x >= w - 6 || y >= h - 6;
      const bool stripe = ((x + y) / 24) % 2 == 0;
      const int shade = 18 + (40 * x) / w;
      px[static_cast<size_t>(y) * w + x] = border   ? PremultipliedBgra(255, 112, 32, 255)
                                           : stripe ? PremultipliedBgra(shade + 10, shade + 14, shade + 24, 210)
                                                    : PremultipliedBgra(shade, shade + 4, shade + 12, 190);
    }
  }
  return px;
}

bool PointInPolygon(float x, float y, const float (*pts)[2], int count) {
  bool inside = false;
  for (int i = 0, j = count - 1; i < count; j = i++) {
    if (((pts[i][1] > y) != (pts[j][1] > y)) &&
        (x < (pts[j][0] - pts[i][0]) * (y - pts[i][1]) / (pts[j][1] - pts[i][1]) + pts[i][0])) {
      inside = !inside;
    }
  }
  return inside;
}

// 32x32 arrow cursor: white fill, black outline, hotspot at (0,0)
std::vector<uint32_t> MakeCursor(int size) {
  static const float arrow[][2] = {{1, 1}, {1, 23}, {7, 17}, {11, 26}, {15, 24}, {11, 16}, {19, 16}};
  const int count = static_cast<int>(sizeof(arrow) / sizeof(arrow[0]));
  std::vector<uint32_t> px(static_cast<size_t>(size) * size, 0);
  auto inside = [&](int x, int y) {
    return x >= 0 && y >= 0 && x < size && y < size && PointInPolygon(x + 0.5f, y + 0.5f, arrow, count);
  };
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      if (inside(x, y)) {
        const bool edge = !inside(x - 1, y) || !inside(x + 1, y) || !inside(x, y - 1) || !inside(x, y + 1);
        px[static_cast<size_t>(y) * size + x] = edge ? PremultipliedBgra(0, 0, 0, 255)
                                                     : PremultipliedBgra(255, 255, 255, 255);
      }
    }
  }
  return px;
}

ID3D11ShaderResourceView* CreateStaticTexture(ID3D11Device* device, std::vector<uint32_t> pixels, int w, int h) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = static_cast<UINT>(w);
  desc.Height = static_cast<UINT>(h);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  D3D11_SUBRESOURCE_DATA data = {};
  data.pSysMem = pixels.data();
  data.SysMemPitch = static_cast<UINT>(w) * 4;

  ID3D11Texture2D* texture = nullptr;
  HRESULT hr = device->CreateTexture2D(&desc, &data, &texture);
  if (FAILED(hr)) {
    // BGRA not supported on this device: swap to RGBA on the CPU
    for (uint32_t& p : pixels) {
      p = (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
    }
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    hr = device->CreateTexture2D(&desc, &data, &texture);
  }
  if (FAILED(hr)) {
    BLogError("CreateTexture2D (static) failed: " + Hex(hr));
    return nullptr;
  }
  ID3D11ShaderResourceView* srv = nullptr;
  hr = device->CreateShaderResourceView(texture, nullptr, &srv);
  texture->Release();
  if (FAILED(hr)) {
    BLogError("CreateShaderResourceView (static) failed: " + Hex(hr));
    return nullptr;
  }
  return srv;
}

DXGI_FORMAT RenderTargetFormat(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return format;
  }
}

}  // namespace

bool Renderer::EnsureResources(IDXGISwapChain* swapChain) {
  ID3D11Device* device = nullptr;
  if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || device == nullptr) {
    return false;
  }
  if (device == device_) {
    device->Release();  // we already hold a reference
    return !failed_;
  }

  // First frame, or the game recreated its device
  ReleaseAll();
  device_ = device;  // keep the reference from GetDevice
  device_->GetImmediateContext(&context_);
  BLogInfo("D3D11 device acquired (feature level " + Hex(device_->GetFeatureLevel()) + ").");
  failed_ = !CreatePipeline() || !CreateStaticTextures();
  if (failed_) {
    BLogError("Overlay renderer disabled for this device.");
  }
  return !failed_;
}

bool Renderer::CreatePipeline() {
  ID3DBlob* vsBlob = nullptr;
  ID3DBlob* psBlob = nullptr;
  ID3DBlob* errors = nullptr;
  HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "seo_overlay", nullptr, nullptr, "VSMain", "vs_4_0",
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errors);
  if (FAILED(hr)) {
    BLogError("Vertex shader compile failed: " +
              std::string(errors ? static_cast<const char*>(errors->GetBufferPointer()) : Hex(hr).c_str()));
    SafeRelease(errors);
    return false;
  }
  SafeRelease(errors);  // warnings only
  hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "seo_overlay", nullptr, nullptr, "PSMain", "ps_4_0",
                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errors);
  if (FAILED(hr)) {
    BLogError("Pixel shader compile failed: " +
              std::string(errors ? static_cast<const char*>(errors->GetBufferPointer()) : Hex(hr).c_str()));
    SafeRelease(errors);
    SafeRelease(vsBlob);
    return false;
  }
  SafeRelease(errors);

  hr = device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);
  if (SUCCEEDED(hr)) {
    hr = device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_);
  }
  SafeRelease(vsBlob);
  SafeRelease(psBlob);
  if (FAILED(hr)) {
    BLogError("Create shader failed: " + Hex(hr));
    return false;
  }

  D3D11_BUFFER_DESC cb = {};
  cb.ByteWidth = sizeof(Constants);
  cb.Usage = D3D11_USAGE_DYNAMIC;
  cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(hr = device_->CreateBuffer(&cb, nullptr, &constants_))) {
    BLogError("CreateBuffer (constants) failed: " + Hex(hr));
    return false;
  }

  D3D11_SAMPLER_DESC sd = {};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  if (FAILED(hr = device_->CreateSamplerState(&sd, &sampler_))) {
    BLogError("CreateSamplerState failed: " + Hex(hr));
    return false;
  }

  // Premultiplied alpha
  D3D11_BLEND_DESC bd = {};
  bd.RenderTarget[0].BlendEnable = TRUE;
  bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
  bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  if (FAILED(hr = device_->CreateBlendState(&bd, &blend_))) {
    BLogError("CreateBlendState failed: " + Hex(hr));
    return false;
  }

  D3D11_RASTERIZER_DESC rd = {};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_NONE;
  rd.DepthClipEnable = TRUE;
  if (FAILED(hr = device_->CreateRasterizerState(&rd, &raster_))) {
    BLogError("CreateRasterizerState failed: " + Hex(hr));
    return false;
  }

  D3D11_DEPTH_STENCIL_DESC dd = {};
  dd.DepthEnable = FALSE;
  dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
  dd.StencilEnable = FALSE;
  if (FAILED(hr = device_->CreateDepthStencilState(&dd, &depth_))) {
    BLogError("CreateDepthStencilState failed: " + Hex(hr));
    return false;
  }
  return true;
}

bool Renderer::CreateStaticTextures() {
  testSrv_ = CreateStaticTexture(device_, MakeTestPattern(512, 256), 512, 256);
  cursorSrv_ = CreateStaticTexture(device_, MakeCursor(32), 32, 32);
  return testSrv_ != nullptr && cursorSrv_ != nullptr;
}

bool Renderer::EnsureUiTexture(int width, int height) {
  if (uiTexture_ != nullptr && uiWidth_ == width && uiHeight_ == height) {
    return true;
  }
  SafeRelease(uiSrv_);
  SafeRelease(uiTexture_);
  uiHasContent_ = false;

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  uiSwizzle_ = false;
  HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &uiTexture_);
  if (FAILED(hr)) {
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // upload BGRA bytes, swap in the shader
    uiSwizzle_ = true;
    hr = device_->CreateTexture2D(&desc, nullptr, &uiTexture_);
  }
  if (FAILED(hr)) {
    BLogError("CreateTexture2D (UI " + std::to_string(width) + "x" + std::to_string(height) + ") failed: " + Hex(hr));
    return false;
  }
  hr = device_->CreateShaderResourceView(uiTexture_, nullptr, &uiSrv_);
  if (FAILED(hr)) {
    BLogError("CreateShaderResourceView (UI) failed: " + Hex(hr));
    SafeRelease(uiTexture_);
    return false;
  }
  uiWidth_ = width;
  uiHeight_ = height;
  return true;
}

void Renderer::UploadUiFrame() {
  const CoreApi* core = GetCore();
  SEO_Frame frame = {};
  if (core == nullptr || !core->AcquireFrame(&frame, uiSerial_)) {
    return;
  }
  if (EnsureUiTexture(frame.width, frame.height)) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context_->Map(uiTexture_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      const size_t rowBytes = static_cast<size_t>(frame.width) * 4;
      auto* dst = static_cast<uint8_t*>(mapped.pData);
      for (int y = 0; y < frame.height; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * mapped.RowPitch,
                    frame.pixels + static_cast<size_t>(y) * frame.stride, rowBytes);
      }
      context_->Unmap(uiTexture_, 0);
      uiHasContent_ = true;
    }
  }
  uiSerial_ = frame.serial;
  core->ReleaseFrame();
}

void Renderer::DrawQuad(ID3D11ShaderResourceView* srv, bool swizzle, float left, float top, float right,
                        float bottom) {
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(context_->Map(constants_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    return;
  }
  Constants c = {};
  const float w = static_cast<float>(width_);
  const float h = static_cast<float>(height_);
  c.rect[0] = left / w * 2.0f - 1.0f;
  c.rect[1] = 1.0f - top / h * 2.0f;
  c.rect[2] = right / w * 2.0f - 1.0f;
  c.rect[3] = 1.0f - bottom / h * 2.0f;
  c.uv[0] = 0.0f;
  c.uv[1] = 0.0f;
  c.uv[2] = 1.0f;
  c.uv[3] = 1.0f;
  c.options[0] = swizzle ? 1.0f : 0.0f;
  std::memcpy(mapped.pData, &c, sizeof(c));
  context_->Unmap(constants_, 0);

  context_->PSSetShaderResources(0, 1, &srv);
  context_->Draw(4, 0);
}

void Renderer::OnPresent(IDXGISwapChain* swapChain) {
  if (swapChain == nullptr) {
    return;
  }
  DXGI_SWAP_CHAIN_DESC scDesc = {};
  if (FAILED(swapChain->GetDesc(&scDesc)) || scDesc.BufferDesc.Width == 0 || scDesc.BufferDesc.Height == 0) {
    return;
  }
  width_ = static_cast<int>(scDesc.BufferDesc.Width);
  height_ = static_cast<int>(scDesc.BufferDesc.Height);
  g_backBufferWidth.store(width_);
  g_backBufferHeight.store(height_);

  if (!EnsureResources(swapChain)) {
    return;
  }

  const Config& cfg = GetConfig();
  const bool visible = IsOverlayVisible();
  const CoreApi* core = GetCore();
  if (core != nullptr && core->GetState() == SEO_STATE_READY) {
    core->Resize(width_, height_);
    if (visible) {
      UploadUiFrame();
    }
  }

  const int cursorX = g_cursorX.load();
  const int cursorY = g_cursorY.load();
  const bool drawUi = visible && core != nullptr && uiHasContent_;
  const bool drawTest = visible && cfg.testPattern;
  const bool drawCursor = visible && cfg.drawCursor && IsUiInputMode() && cursorX >= 0 && cursorY >= 0;
  if (!drawUi && !drawTest && !drawCursor) {
    return;
  }

  ID3D11Texture2D* backBuffer = nullptr;
  if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) {
    return;
  }
  D3D11_TEXTURE2D_DESC bbDesc = {};
  backBuffer->GetDesc(&bbDesc);
  D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
  rtvDesc.Format = RenderTargetFormat(bbDesc.Format);
  rtvDesc.ViewDimension = bbDesc.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
  ID3D11RenderTargetView* rtv = nullptr;
  // Created and released every frame: holding a back buffer reference would make the game's ResizeBuffers fail.
  const HRESULT hr = device_->CreateRenderTargetView(backBuffer, &rtvDesc, &rtv);
  backBuffer->Release();
  if (FAILED(hr)) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      BLogError("CreateRenderTargetView failed: " + Hex(hr) + " (back buffer format " +
                std::to_string(static_cast<int>(bbDesc.Format)) + ")");
    }
    return;
  }

  StateBackup backup;
  backup.Save(context_);

  const FLOAT blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  D3D11_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
  ID3D11Buffer* nullBuffer = nullptr;
  const UINT zero = 0;
  context_->OMSetRenderTargets(1, &rtv, nullptr);
  context_->RSSetViewports(1, &viewport);
  context_->RSSetState(raster_);
  context_->OMSetBlendState(blend_, blendFactor, 0xFFFFFFFFu);
  context_->OMSetDepthStencilState(depth_, 0);
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  context_->IASetVertexBuffers(0, 1, &nullBuffer, &zero, &zero);
  context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
  context_->VSSetShader(vs_, nullptr, 0);
  context_->VSSetConstantBuffers(0, 1, &constants_);
  context_->PSSetShader(ps_, nullptr, 0);
  context_->PSSetConstantBuffers(0, 1, &constants_);
  context_->PSSetSamplers(0, 1, &sampler_);
  context_->GSSetShader(nullptr, nullptr, 0);
  context_->HSSetShader(nullptr, nullptr, 0);
  context_->DSSetShader(nullptr, nullptr, 0);

  const float scale = std::max(1.0f, static_cast<float>(height_) / 1080.0f);
  if (drawUi) {
    DrawQuad(uiSrv_, uiSwizzle_, 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
  }
  if (drawTest) {
    const float x = 48.0f * scale;
    const float y = 48.0f * scale;
    DrawQuad(testSrv_, false, x, y, x + 512.0f * scale, y + 256.0f * scale);
  }
  if (drawCursor) {
    const float x = static_cast<float>(cursorX);
    const float y = static_cast<float>(cursorY);
    DrawQuad(cursorSrv_, false, x, y, x + 32.0f * scale, y + 32.0f * scale);
  }

  backup.Restore(context_);
  rtv->Release();
}

void Renderer::ReleaseAll() {
  SafeRelease(uiSrv_);
  SafeRelease(uiTexture_);
  SafeRelease(testSrv_);
  SafeRelease(cursorSrv_);
  SafeRelease(depth_);
  SafeRelease(raster_);
  SafeRelease(blend_);
  SafeRelease(sampler_);
  SafeRelease(constants_);
  SafeRelease(ps_);
  SafeRelease(vs_);
  SafeRelease(context_);
  SafeRelease(device_);
  uiWidth_ = uiHeight_ = 0;
  uiHasContent_ = false;
  uiSerial_ = 0;
  failed_ = false;
}

}  // namespace seo_gtav
