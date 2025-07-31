#include "d3d10_state_object.hpp"

namespace dxmt {

void STDMETHODCALLTYPE
MTLD3D10BlendState::GetDesc(D3D10_BLEND_DESC *pDesc) {
  D3D11_BLEND_DESC d3d11_desc;
  d3d11->GetDesc(&d3d11_desc);

  pDesc->AlphaToCoverageEnable = d3d11_desc.AlphaToCoverageEnable;
  pDesc->SrcBlend = D3D10_BLEND(d3d11_desc.RenderTarget[0].SrcBlend);
  pDesc->DestBlend = D3D10_BLEND(d3d11_desc.RenderTarget[0].DestBlend);
  pDesc->BlendOp = D3D10_BLEND_OP(d3d11_desc.RenderTarget[0].BlendOp);
  pDesc->SrcBlendAlpha = D3D10_BLEND(d3d11_desc.RenderTarget[0].SrcBlendAlpha);
  pDesc->DestBlendAlpha = D3D10_BLEND(d3d11_desc.RenderTarget[0].DestBlendAlpha);
  pDesc->BlendOpAlpha = D3D10_BLEND_OP(d3d11_desc.RenderTarget[0].BlendOpAlpha);

  for (unsigned i = 0; i < 8; i++) {
    unsigned id = d3d11_desc.IndependentBlendEnable ? i : 0;
    pDesc->BlendEnable[i] = d3d11_desc.RenderTarget[id].BlendEnable;
    pDesc->RenderTargetWriteMask[i] = d3d11_desc.RenderTarget[id].RenderTargetWriteMask;
  }
}

void STDMETHODCALLTYPE
MTLD3D10BlendState::GetDesc1(D3D10_BLEND_DESC1 *pDesc) {
  static_assert(sizeof(D3D10_BLEND_DESC1) == sizeof(D3D11_BLEND_DESC));
  d3d11->GetDesc(reinterpret_cast<D3D11_BLEND_DESC *>(pDesc));
}

void STDMETHODCALLTYPE
MTLD3D10RasterizerState::GetDesc(D3D10_RASTERIZER_DESC *pDesc) {
  static_assert(sizeof(D3D10_RASTERIZER_DESC) == sizeof(D3D11_RASTERIZER_DESC));
  d3d11->GetDesc(reinterpret_cast<D3D11_RASTERIZER_DESC *>(pDesc));
}

void STDMETHODCALLTYPE
MTLD3D10SamplerState::GetDesc(D3D10_SAMPLER_DESC *pDesc) {
  D3D11_SAMPLER_DESC d3d11_desc;
  d3d11->GetDesc(&d3d11_desc);

  pDesc->Filter = D3D10_FILTER(d3d11_desc.Filter);
  pDesc->AddressU = D3D10_TEXTURE_ADDRESS_MODE(d3d11_desc.AddressU);
  pDesc->AddressV = D3D10_TEXTURE_ADDRESS_MODE(d3d11_desc.AddressV);
  pDesc->AddressW = D3D10_TEXTURE_ADDRESS_MODE(d3d11_desc.AddressW);
  pDesc->MipLODBias = d3d11_desc.MipLODBias;
  pDesc->MaxAnisotropy = d3d11_desc.MaxAnisotropy;
  pDesc->ComparisonFunc = D3D10_COMPARISON_FUNC(d3d11_desc.ComparisonFunc);
  pDesc->MinLOD = d3d11_desc.MinLOD;
  pDesc->MaxLOD = d3d11_desc.MaxLOD;

  for (unsigned i = 0; i < 4; i++)
    pDesc->BorderColor[i] = d3d11_desc.BorderColor[i];
}

void STDMETHODCALLTYPE
MTLD3D10DepthStencilState::GetDesc(D3D10_DEPTH_STENCIL_DESC *pDesc) {
  static_assert(sizeof(D3D10_DEPTH_STENCIL_DESC) == sizeof(D3D11_DEPTH_STENCIL_DESC));
  d3d11->GetDesc(reinterpret_cast<D3D11_DEPTH_STENCIL_DESC *>(pDesc));
}

} // namespace dxmt