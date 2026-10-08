#include "d3d11_renderer.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace seo_backend {
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
    float4 options;  // x: 1 = swap red/blue, y: opacity
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
    c = options.x > 0.5 ? c.bgra : c;
    return c * options.y;                  // premultiplied alpha: scale every channel
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
    // In/out capacities: reset every time (the instance is reused across frames)
    psInstCount = vsInstCount = gsInstCount = hsInstCount = dsInstCount = kMaxInstances;
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

}  // namespace

DXGI_FORMAT Renderer::RenderTargetFormat(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return format;
  }
}

bool Renderer::SetDevice(ID3D11Device* device) {
  if (device == nullptr) {
    return false;
  }
  if (device == device_) {
    return !failed_;
  }

  // First frame, or the game recreated its device
  ReleaseAll();
  device_ = device;
  device_->AddRef();
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
  desc.Usage = D3D11_USAGE_DEFAULT;  // updated with UpdateSubresource, only the dirty part
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
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
  uiNeedsFullUpload_ = true;
  return true;
}

void Renderer::UploadUiFrame() {
  const CoreApi* core = GetCore();
  SEO_Frame frame = {};
  if (core == nullptr || !core->AcquireFrame(&frame, uiSerial_)) {
    return;
  }
  if (EnsureUiTexture(frame.width, frame.height)) {
    int x = frame.dirtyX, y = frame.dirtyY, w = frame.dirtyWidth, h = frame.dirtyHeight;
    if (uiNeedsFullUpload_) {
      x = 0;
      y = 0;
      w = frame.width;
      h = frame.height;
    }
    x = std::max(0, x);
    y = std::max(0, y);
    w = std::min(w, frame.width - x);
    h = std::min(h, frame.height - y);
    if (w > 0 && h > 0) {
      D3D11_BOX box = {static_cast<UINT>(x), static_cast<UINT>(y), 0, static_cast<UINT>(x + w),
                       static_cast<UINT>(y + h), 1};
      const uint8_t* src = frame.pixels + static_cast<size_t>(y) * frame.stride + static_cast<size_t>(x) * 4;
      context_->UpdateSubresource(uiTexture_, 0, &box, src, static_cast<UINT>(frame.stride), 0);
      uiNeedsFullUpload_ = false;
      uiHasContent_ = true;
    }
  }
  uiSerial_ = frame.serial;
  core->ReleaseFrame();
}

void Renderer::DrawQuad(ID3D11ShaderResourceView* srv, bool swizzle, float left, float top, float right,
                        float bottom, float u0, float v0, float u1, float v1, float alpha) {
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
  c.uv[0] = u0;
  c.uv[1] = v0;
  c.uv[2] = u1;
  c.uv[3] = v1;
  c.options[0] = swizzle ? 1.0f : 0.0f;
  c.options[1] = alpha;
  std::memcpy(mapped.pData, &c, sizeof(c));
  context_->Unmap(constants_, 0);

  context_->PSSetShaderResources(0, 1, &srv);
  context_->Draw(4, 0);
}

bool Renderer::BeginFrame(int width, int height) {
  drawUi_ = drawTest_ = drawCursor_ = false;
  if (device_ == nullptr || failed_ || width <= 0 || height <= 0) {
    return false;
  }
  width_ = width;
  height_ = height;
  g_backBufferWidth.store(width_);
  g_backBufferHeight.store(height_);

  const Config& cfg = GetConfig();
  const bool visible = IsOverlayVisible();
  const CoreApi* core = GetCore();
  if (core != nullptr && core->GetState() == SEO_STATE_READY) {
    core->Resize(width_, height_);
    if (visible) {
      UploadUiFrame();
    }
  }

  drawUi_ = visible && core != nullptr && uiHasContent_;
  drawTest_ = visible && cfg.testPattern;
  drawCursor_ = visible && cfg.drawCursor && IsUiInputMode() && g_cursorX.load() >= 0 && g_cursorY.load() >= 0;
  return drawUi_ || drawTest_ || drawCursor_;
}

void Renderer::Draw(ID3D11RenderTargetView* rtv, bool preserveState) {
  if (rtv == nullptr || context_ == nullptr || failed_ || !(drawUi_ || drawTest_ || drawCursor_)) {
    return;
  }
  const CoreApi* core = GetCore();
  if (drawUi_ && core == nullptr) {
    return;
  }

  // ~10 KB: static instead of on the game's render thread stack, and no allocation that could throw.
  // Draw only runs on the one render thread.
  static StateBackup s_backup;
  StateBackup* backup = nullptr;
  if (preserveState) {
    backup = &s_backup;
    backup->Save(context_);
  }

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
  if (drawUi_) {
    // World-anchored sprites first (beneath the HUD/menus), then the screen part of the page.
    DrawSprites(core);
    // The view may be taller than the screen (atlas below it): sample only the screen part.
    const float v1 = std::min(1.0f, static_cast<float>(height_) / static_cast<float>(std::max(1, uiHeight_)));
    DrawQuad(uiSrv_, uiSwizzle_, 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 0.0f, 1.0f,
             v1);
  }
  if (drawTest_) {
    const float x = 48.0f * scale;
    const float y = 48.0f * scale;
    DrawQuad(testSrv_, false, x, y, x + 512.0f * scale, y + 256.0f * scale);
  }
  if (drawCursor_) {
    const float x = static_cast<float>(g_cursorX.load());
    const float y = static_cast<float>(g_cursorY.load());
    DrawQuad(cursorSrv_, false, x, y, x + 32.0f * scale, y + 32.0f * scale);
  }

  if (backup != nullptr) {
    backup->Restore(context_);  // also releases the saved references
  } else {
    // Our own context (D3D11On12): just drop the references to the back buffer view and the UI texture
    ID3D11RenderTargetView* nullRtv = nullptr;
    ID3D11ShaderResourceView* nullSrv = nullptr;
    context_->OMSetRenderTargets(1, &nullRtv, nullptr);
    context_->PSSetShaderResources(0, 1, &nullSrv);
  }
}

void Renderer::DrawSprites(const CoreApi* core) {
  SEO_AtlasLayout layout = {};
  if (!core->GetAtlasLayout(&layout) || layout.rows <= 0 || layout.columns <= 0) {
    return;
  }
  const int atlasTop = uiHeight_ - layout.rows * layout.slotHeight;  // atlas = bottom rows of the view
  // The frame must have exactly screen + atlas height; otherwise it was painted before the layout took effect
  // (or for another screen size) and its "atlas" rows would be something else.
  if (atlasTop != height_ || layout.columns * layout.slotWidth > uiWidth_) {
    return;
  }
  const int count = core->GetSprites(sprites_, SEO_MAX_SPRITES);
  const int slots = layout.columns * layout.rows;
  const float texW = static_cast<float>(uiWidth_);
  const float texH = static_cast<float>(uiHeight_);
  const float sw = static_cast<float>(layout.slotWidth);
  const float sh = static_cast<float>(layout.slotHeight);
  for (int i = 0; i < count; ++i) {
    const SEO_Sprite& s = sprites_[i];
    if (s.slot < 0 || s.slot >= slots || s.alpha <= 0.0f || s.scale <= 0.0f) {
      continue;
    }
    const float px = static_cast<float>((s.slot % layout.columns) * layout.slotWidth);
    const float py = static_cast<float>(atlasTop + (s.slot / layout.columns) * layout.slotHeight);
    // Half-texel inset keeps linear filtering from bleeding the neighbouring slot in
    const float u0 = (px + 0.5f) / texW, v0 = (py + 0.5f) / texH;
    const float u1 = (px + sw - 0.5f) / texW, v1 = (py + sh - 0.5f) / texH;
    const float w = sw * s.scale, h = sh * s.scale;
    const float ax = s.x * static_cast<float>(width_), ay = s.y * static_cast<float>(height_);
    DrawQuad(uiSrv_, uiSwizzle_, ax - w * 0.5f, ay - h, ax + w * 0.5f, ay, u0, v0, u1, v1, std::min(1.0f, s.alpha));
  }
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
  uiNeedsFullUpload_ = true;
  uiSerial_ = 0;
  failed_ = false;
}

}  // namespace seo_backend
