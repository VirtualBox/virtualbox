#include "d3d10_device.hpp"
#include "d3d10_input_layout.hpp"
#include "d3d10_buffer.hpp"
#include "d3d10_shader.hpp"
#include "d3d10_state_object.hpp"
#include "d3d10_view.hpp"
#include "d3d10_texture.hpp"
#include "../d3d11/d3d11_context.hpp"
#include "../d3d11/d3d11_device.hpp"

namespace dxmt {

template <typename Impl, typename Interface>
auto
GetD3D11Interface(Interface *ptr) {
  return ptr ? static_cast<Impl *>(ptr)->d3d11 : nullptr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::QueryInterface(REFIID riid, void **ppvObject) {
  return d3d11_device_->QueryInterface(riid, ppvObject);
}

ULONG STDMETHODCALLTYPE
MTLD3D10Device::AddRef() {
  return d3d11_device_->AddRef();
}

ULONG STDMETHODCALLTYPE
MTLD3D10Device::Release() {
  return d3d11_device_->Release();
}

void STDMETHODCALLTYPE
MTLD3D10Device::VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer *const *ppConstantBuffers) {
  ID3D11Buffer *d3d11_buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];

  if (NumBuffers > D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
    return;

  for (unsigned i = 0; i < NumBuffers; i++)
    d3d11_buffers[i] = ppConstantBuffers ? GetD3D11Interface<MTLD3D10Buffer>(ppConstantBuffers[i]) : nullptr;

  d3d11_context_->VSSetConstantBuffers(StartSlot, NumBuffers, d3d11_buffers);
}

void STDMETHODCALLTYPE
MTLD3D10Device::PSSetShaderResources(
    UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView *const *ppShaderResourceViews
) {
  ID3D11ShaderResourceView *d3d11_views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];

  if (NumViews > D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
    return;

  for (unsigned i = 0; i < NumViews; i++)
    d3d11_views[i] =
        ppShaderResourceViews ? GetD3D11Interface<MTLD3D10ShaderResourceView>(ppShaderResourceViews[i]) : nullptr;

  d3d11_context_->PSSetShaderResources(StartSlot, NumViews, d3d11_views);
}

void STDMETHODCALLTYPE
MTLD3D10Device::PSSetShader(ID3D10PixelShader *pPixelShader) {
  d3d11_context_->PSSetShader(GetD3D11Interface<MTLD3D10PixelShader>(pPixelShader), nullptr, 0);
}

void STDMETHODCALLTYPE
MTLD3D10Device::PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState *const *ppSamplers) {
  ID3D11SamplerState *d3d11_samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];
  if (NumSamplers > D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT)
    return;

  for (unsigned i = 0; i < NumSamplers; i++)
    d3d11_samplers[i] = ppSamplers ? GetD3D11Interface<MTLD3D10SamplerState>(ppSamplers[i]) : nullptr;

  d3d11_context_->PSSetSamplers(StartSlot, NumSamplers, d3d11_samplers);
}

void STDMETHODCALLTYPE
MTLD3D10Device::VSSetShader(ID3D10VertexShader *pVertexShader) {
  d3d11_context_->VSSetShader(GetD3D11Interface<MTLD3D10VertexShader>(pVertexShader), nullptr, 0);
}

void STDMETHODCALLTYPE
MTLD3D10Device::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) {
  d3d11_context_->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
}

void STDMETHODCALLTYPE
MTLD3D10Device::Draw(UINT VertexCount, UINT StartVertexLocation) {
  d3d11_context_->Draw(VertexCount, StartVertexLocation);
}

void STDMETHODCALLTYPE
MTLD3D10Device::PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer *const *ppConstantBuffers) {
  ID3D11Buffer *d3d11_buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];

  if (NumBuffers > D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
    return;

  for (unsigned i = 0; i < NumBuffers; i++)
    d3d11_buffers[i] = ppConstantBuffers ? GetD3D11Interface<MTLD3D10Buffer>(ppConstantBuffers[i]) : nullptr;

  d3d11_context_->PSSetConstantBuffers(StartSlot, NumBuffers, d3d11_buffers);
}

void STDMETHODCALLTYPE
MTLD3D10Device::IASetInputLayout(ID3D10InputLayout *pInputLayout) {
  d3d11_context_->IASetInputLayout(GetD3D11Interface<MTLD3D10InputLayout>(pInputLayout));
}

void STDMETHODCALLTYPE
MTLD3D10Device::IASetVertexBuffers(
    UINT StartSlot, UINT NumBuffers, ID3D10Buffer *const *ppVertexBuffers, const UINT *pStrides, const UINT *pOffsets
) {
  ID3D11Buffer *d3d11_buffers[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];

  if (NumBuffers > D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
    return;

  for (unsigned i = 0; i < NumBuffers; i++)
    d3d11_buffers[i] = GetD3D11Interface<MTLD3D10Buffer>(ppVertexBuffers[i]);

  d3d11_context_->IASetVertexBuffers(StartSlot, NumBuffers, d3d11_buffers, pStrides, pOffsets);
}

void STDMETHODCALLTYPE
MTLD3D10Device::IASetIndexBuffer(ID3D10Buffer *pIndexBuffer, DXGI_FORMAT Format, UINT Offset) {
  d3d11_context_->IASetIndexBuffer(GetD3D11Interface<MTLD3D10Buffer>(pIndexBuffer), Format, Offset);
}

void STDMETHODCALLTYPE
MTLD3D10Device::DrawIndexedInstanced(
    UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation,
    UINT StartInstanceLocation
) {
  d3d11_context_->DrawIndexedInstanced(
      IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartIndexLocation
  );
}

void STDMETHODCALLTYPE
MTLD3D10Device::DrawInstanced(
    UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation
) {
  d3d11_context_->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}

void STDMETHODCALLTYPE
MTLD3D10Device::GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer *const *ppConstantBuffers) {
  ID3D11Buffer *d3d11_buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];

  if (NumBuffers > D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
    return;

  for (unsigned i = 0; i < NumBuffers; i++)
    d3d11_buffers[i] = ppConstantBuffers ? GetD3D11Interface<MTLD3D10Buffer>(ppConstantBuffers[i]) : nullptr;

  d3d11_context_->GSSetConstantBuffers(StartSlot, NumBuffers, d3d11_buffers);
}

void STDMETHODCALLTYPE
MTLD3D10Device::GSSetShader(ID3D10GeometryShader *pShader) {
  d3d11_context_->GSSetShader(GetD3D11Interface<MTLD3D10GeometryShader>(pShader), nullptr, 0);
}

void STDMETHODCALLTYPE
MTLD3D10Device::IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY Topology) {
  d3d11_context_->IASetPrimitiveTopology(Topology);
}

void STDMETHODCALLTYPE
MTLD3D10Device::VSSetShaderResources(
    UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView *const *ppShaderResourceViews
) {
  ID3D11ShaderResourceView *d3d11_views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];

  if (NumViews > D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
    return;

  for (unsigned i = 0; i < NumViews; i++)
    d3d11_views[i] =
        ppShaderResourceViews ? GetD3D11Interface<MTLD3D10ShaderResourceView>(ppShaderResourceViews[i]) : nullptr;

  d3d11_context_->VSSetShaderResources(StartSlot, NumViews, d3d11_views);
}

void STDMETHODCALLTYPE
MTLD3D10Device::VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState *const *ppSamplers) {
  ID3D11SamplerState *d3d11_samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];

  if (NumSamplers > D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT)
    return;

  for (unsigned i = 0; i < NumSamplers; i++)
    d3d11_samplers[i] = ppSamplers ? GetD3D11Interface<MTLD3D10SamplerState>(ppSamplers[i]) : nullptr;

  d3d11_context_->VSSetSamplers(StartSlot, NumSamplers, d3d11_samplers);
}

void STDMETHODCALLTYPE
MTLD3D10Device::SetPredication(ID3D10Predicate *pPredicate, WINBOOL PredicateValue) {
  Com<ID3D11Query> d3d11_predicate;
  if (pPredicate) {
    pPredicate->QueryInterface(__uuidof(ID3D11Query), reinterpret_cast<void **>(&d3d11_predicate));
    d3d11_context_->SetPredication(reinterpret_cast<ID3D11Predicate *>(d3d11_predicate.ptr()), PredicateValue);
  } else {
    d3d11_context_->SetPredication(nullptr, PredicateValue);
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::GSSetShaderResources(
    UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView *const *ppShaderResourceViews
) {
  ID3D11ShaderResourceView *d3d11_views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];

  if (NumViews > D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
    return;

  for (unsigned i = 0; i < NumViews; i++)
    d3d11_views[i] =
        ppShaderResourceViews ? GetD3D11Interface<MTLD3D10ShaderResourceView>(ppShaderResourceViews[i]) : nullptr;

  d3d11_context_->GSSetShaderResources(StartSlot, NumViews, d3d11_views);
}

void STDMETHODCALLTYPE
MTLD3D10Device::GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState *const *ppSamplers) {
  ID3D11SamplerState *d3d11_samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];

  if (NumSamplers > D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT)
    return;

  for (unsigned i = 0; i < NumSamplers; i++)
    d3d11_samplers[i] = ppSamplers ? GetD3D11Interface<MTLD3D10SamplerState>(ppSamplers[i]) : nullptr;

  d3d11_context_->GSSetSamplers(StartSlot, NumSamplers, d3d11_samplers);
}

void STDMETHODCALLTYPE
MTLD3D10Device::OMSetRenderTargets(
    UINT NumViews, ID3D10RenderTargetView *const *ppRenderTargetViews, ID3D10DepthStencilView *pDepthStencilView
) {
  ID3D11RenderTargetView *d3d11_views[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT];

  if (NumViews > D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT)
    return;

  for (unsigned i = 0; i < NumViews; i++)
    d3d11_views[i] =
        ppRenderTargetViews ? GetD3D11Interface<MTLD3D10RenderTargetView>(ppRenderTargetViews[i]) : nullptr;

  d3d11_context_->OMSetRenderTargets(
      NumViews, d3d11_views, GetD3D11Interface<MTLD3D10DepthStencilView>(pDepthStencilView)
  );
}

void STDMETHODCALLTYPE
MTLD3D10Device::OMSetBlendState(ID3D10BlendState *pBlendState, const FLOAT BlendFactor[4], UINT SampleMask) {
  d3d11_context_->OMSetBlendState(GetD3D11Interface<MTLD3D10BlendState>(pBlendState), BlendFactor, SampleMask);
}

void STDMETHODCALLTYPE
MTLD3D10Device::OMSetDepthStencilState(ID3D10DepthStencilState *pDepthStencilState, UINT StencilRef) {
  d3d11_context_->OMSetDepthStencilState(GetD3D11Interface<MTLD3D10DepthStencilState>(pDepthStencilState), StencilRef);
}

void STDMETHODCALLTYPE
MTLD3D10Device::SOSetTargets(UINT NumBuffers, ID3D10Buffer *const *ppSOTargets, const UINT *pOffsets) {

  ID3D11Buffer *d3d11_buffers[D3D10_SO_BUFFER_SLOT_COUNT];

  if (NumBuffers > D3D10_SO_BUFFER_SLOT_COUNT)
    return;

  for (unsigned i = 0; i < NumBuffers; i++)
    d3d11_buffers[i] = ppSOTargets ? GetD3D11Interface<MTLD3D10Buffer>(ppSOTargets[i]) : nullptr;

  d3d11_context_->SOSetTargets(NumBuffers, d3d11_buffers, pOffsets);
}

void STDMETHODCALLTYPE
MTLD3D10Device::DrawAuto() {
  d3d11_context_->DrawAuto();
}

void STDMETHODCALLTYPE
MTLD3D10Device::RSSetState(ID3D10RasterizerState *pRasterizerState) {
  d3d11_context_->RSSetState(GetD3D11Interface<MTLD3D10RasterizerState>(pRasterizerState));
}

void STDMETHODCALLTYPE
MTLD3D10Device::RSSetViewports(UINT NumViewports, const D3D10_VIEWPORT *pViewports) {
  D3D11_VIEWPORT vp[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];

  if (NumViewports > D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)
    return;

  for (unsigned i = 0; i < NumViewports; i++) {
    vp[i].TopLeftX = float(pViewports[i].TopLeftX);
    vp[i].TopLeftY = float(pViewports[i].TopLeftY);
    vp[i].Width = float(pViewports[i].Width);
    vp[i].Height = float(pViewports[i].Height);
    vp[i].MinDepth = pViewports[i].MinDepth;
    vp[i].MaxDepth = pViewports[i].MaxDepth;
  }

  d3d11_context_->RSSetViewports(NumViewports, vp);
}

void STDMETHODCALLTYPE
MTLD3D10Device::RSSetScissorRects(UINT NumRects, const D3D10_RECT *pRects) {
  d3d11_context_->RSSetScissorRects(NumRects, pRects);
}

void STDMETHODCALLTYPE
MTLD3D10Device::CopySubresourceRegion(
    ID3D10Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D10Resource *pSrcResource,
    UINT SrcSubresource, const D3D10_BOX *pSrcBox
) {
  d3d11_context_->CopySubresourceRegion(
      GetD3D11Interface<MTLD3D10Buffer>(pDstResource), DstSubresource, DstX, DstY, DstZ,
      GetD3D11Interface<MTLD3D10Buffer>(pSrcResource), SrcSubresource, reinterpret_cast<const D3D11_BOX *>(pSrcBox)
  );
}

void STDMETHODCALLTYPE
MTLD3D10Device::CopyResource(ID3D10Resource *pDstResource, ID3D10Resource *pSrcResource) {
  d3d11_context_->CopyResource(
      GetD3D11Interface<MTLD3D10Buffer>(pDstResource), GetD3D11Interface<MTLD3D10Buffer>(pSrcResource)
  );
}

void STDMETHODCALLTYPE
MTLD3D10Device::UpdateSubresource(
    ID3D10Resource *pDstResource, UINT DstSubresource, const D3D10_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch,
    UINT SrcDepthPitch
) {
  d3d11_context_->UpdateSubresource(
      GetD3D11Interface<MTLD3D10Buffer>(pDstResource), DstSubresource, reinterpret_cast<const D3D11_BOX *>(pDstBox),
      pSrcData, SrcRowPitch, SrcDepthPitch
  );
}

void STDMETHODCALLTYPE
MTLD3D10Device::ClearRenderTargetView(ID3D10RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4]) {
  d3d11_context_->ClearRenderTargetView(GetD3D11Interface<MTLD3D10RenderTargetView>(pRenderTargetView), ColorRGBA);
}

void STDMETHODCALLTYPE
MTLD3D10Device::ClearDepthStencilView(
    ID3D10DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil
) {
  d3d11_context_->ClearDepthStencilView(
      GetD3D11Interface<MTLD3D10DepthStencilView>(pDepthStencilView), ClearFlags, Depth, Stencil
  );
}

void STDMETHODCALLTYPE
MTLD3D10Device::GenerateMips(ID3D10ShaderResourceView *pShaderResourceView) {
  d3d11_context_->GenerateMips(GetD3D11Interface<MTLD3D10ShaderResourceView>(pShaderResourceView));
}

void STDMETHODCALLTYPE
MTLD3D10Device::ResolveSubresource(
    ID3D10Resource *pDstResource, UINT DstSubresource, ID3D10Resource *pSrcResource, UINT SrcSubresource,
    DXGI_FORMAT Format
) {
  d3d11_context_->ResolveSubresource(
      GetD3D11Interface<MTLD3D10Texture2D>(pDstResource), DstSubresource,
      GetD3D11Interface<MTLD3D10Texture2D>(pSrcResource), SrcSubresource, Format
  );
}

void STDMETHODCALLTYPE
MTLD3D10Device::VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer **ppConstantBuffers) {
  ID3D11Buffer *d3d11_buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
  d3d11_context_->VSGetConstantBuffers(StartSlot, NumBuffers, d3d11_buffers);

  for (unsigned i = 0; i < NumBuffers; i++) {
    if (d3d11_buffers[i]) {
      d3d11_buffers[i]->QueryInterface(IID_PPV_ARGS(&ppConstantBuffers[i]));
      d3d11_buffers[i]->Release();
    } else {
      ppConstantBuffers[i] = nullptr;
    }
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView **ppShaderResourceViews) {
  ID3D11ShaderResourceView *d3d11_views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
  d3d11_context_->PSGetShaderResources(StartSlot, NumViews, d3d11_views);

  for (unsigned i = 0; i < NumViews; i++) {
    if (d3d11_views[i]) {
      d3d11_views[i]->QueryInterface(IID_PPV_ARGS(&ppShaderResourceViews[i]));
      d3d11_views[i]->Release();
    } else {
      ppShaderResourceViews[i] = nullptr;
    }
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::PSGetShader(ID3D10PixelShader **ppPixelShader) {
  Com<ID3D11PixelShader> d3d11_shader;
  d3d11_context_->PSGetShader(&d3d11_shader, nullptr, nullptr);

  if (d3d11_shader)
    d3d11_shader->QueryInterface(IID_PPV_ARGS(ppPixelShader));
  else
    *ppPixelShader = nullptr;
}

void STDMETHODCALLTYPE
MTLD3D10Device::PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState **ppSamplers) {
  ID3D11SamplerState *d3d11_samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];
  d3d11_context_->PSGetSamplers(StartSlot, NumSamplers, d3d11_samplers);

  for (unsigned i = 0; i < NumSamplers; i++) {
    if (d3d11_samplers[i]) {
      d3d11_samplers[i]->QueryInterface(IID_PPV_ARGS(&ppSamplers[i]));
      d3d11_samplers[i]->Release();
    } else {
      ppSamplers[i] = nullptr;
    }
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::VSGetShader(ID3D10VertexShader **ppVertexShader) {
  Com<ID3D11VertexShader> d3d11_shader;
  d3d11_context_->VSGetShader(&d3d11_shader, nullptr, nullptr);

  if (d3d11_shader)
    d3d11_shader->QueryInterface(IID_PPV_ARGS(ppVertexShader));
  else
    *ppVertexShader = nullptr;
}

void STDMETHODCALLTYPE
MTLD3D10Device::PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer **ppConstantBuffers) {
  ID3D11Buffer *d3d11_buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
  d3d11_context_->PSGetConstantBuffers(StartSlot, NumBuffers, d3d11_buffers);

  for (unsigned i = 0; i < NumBuffers; i++) {
    if (d3d11_buffers[i]) {
      d3d11_buffers[i]->QueryInterface(IID_PPV_ARGS(&ppConstantBuffers[i]));
      d3d11_buffers[i]->Release();
    } else {
      ppConstantBuffers[i] = nullptr;
    }
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::IAGetInputLayout(ID3D10InputLayout **ppInputLayout) {
  Com<ID3D11InputLayout> d3d11_ia;
  d3d11_context_->IAGetInputLayout(&d3d11_ia);

  if (d3d11_ia)
    d3d11_ia->QueryInterface(IID_PPV_ARGS(ppInputLayout));
  else
    *ppInputLayout = nullptr;
}

void STDMETHODCALLTYPE
MTLD3D10Device::IAGetVertexBuffers(
    UINT StartSlot, UINT NumBuffers, ID3D10Buffer **ppVertexBuffers, UINT *pStrides, UINT *pOffsets
) {
  ID3D11Buffer *d3d11_buffers[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];

  d3d11_context_->IAGetVertexBuffers(
      StartSlot, NumBuffers, ppVertexBuffers ? d3d11_buffers : nullptr, pStrides, pOffsets
  );

  if (ppVertexBuffers) {
    for (unsigned i = 0; i < NumBuffers; i++) {
      if (d3d11_buffers[i]) {
        d3d11_buffers[i]->QueryInterface(IID_PPV_ARGS(&ppVertexBuffers[i]));
        d3d11_buffers[i]->Release();
      } else {
        ppVertexBuffers[i] = nullptr;
      }
    }
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::IAGetIndexBuffer(ID3D10Buffer **pIndexBuffer, DXGI_FORMAT *Format, UINT *Offset) {
  Com<ID3D11Buffer> d3d11_buffer;
  d3d11_context_->IAGetIndexBuffer(pIndexBuffer ? &d3d11_buffer : nullptr, Format, Offset);

  if (pIndexBuffer) {
    if (d3d11_buffer)
      d3d11_buffer->QueryInterface(IID_PPV_ARGS(pIndexBuffer));
    else
      pIndexBuffer = nullptr;
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer **ppConstantBuffers) {
  ID3D11Buffer *d3d11_buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
  d3d11_context_->GSGetConstantBuffers(StartSlot, NumBuffers, d3d11_buffers);

  for (unsigned i = 0; i < NumBuffers; i++) {
    if (d3d11_buffers[i]) {
      d3d11_buffers[i]->QueryInterface(IID_PPV_ARGS(&ppConstantBuffers[i]));
      d3d11_buffers[i]->Release();
    } else {
      ppConstantBuffers[i] = nullptr;
    }
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::GSGetShader(ID3D10GeometryShader **ppGeometryShader) {
  Com<ID3D11GeometryShader> d3d11_shader;
  d3d11_context_->GSGetShader(&d3d11_shader, nullptr, nullptr);

  if (d3d11_shader)
    d3d11_shader->QueryInterface(IID_PPV_ARGS(ppGeometryShader));
  else
    *ppGeometryShader = nullptr;
}

void STDMETHODCALLTYPE
MTLD3D10Device::IAGetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY *pTopology) {
  D3D11_PRIMITIVE_TOPOLOGY d3d11_topo;
  d3d11_context_->IAGetPrimitiveTopology(&d3d11_topo);

  *pTopology = d3d11_topo < D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST ? D3D10_PRIMITIVE_TOPOLOGY(d3d11_topo)
                                                                             : D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

void STDMETHODCALLTYPE
MTLD3D10Device::VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView **ppShaderResourceViews) {
  ID3D11ShaderResourceView *d3d11_views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
  d3d11_context_->VSGetShaderResources(StartSlot, NumViews, d3d11_views);

  for (unsigned i = 0; i < NumViews; i++) {
    if (d3d11_views[i]) {
      d3d11_views[i]->QueryInterface(IID_PPV_ARGS(&ppShaderResourceViews[i]));
      d3d11_views[i]->Release();
    } else {
      ppShaderResourceViews[i] = nullptr;
    }
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState **ppSamplers) {
  ID3D11SamplerState *d3d11_samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];
  d3d11_context_->VSGetSamplers(StartSlot, NumSamplers, d3d11_samplers);

  for (unsigned i = 0; i < NumSamplers; i++) {
    if (d3d11_samplers[i]) {
      d3d11_samplers[i]->QueryInterface(IID_PPV_ARGS(&ppSamplers[i]));
      d3d11_samplers[i]->Release();
    } else {
      ppSamplers[i] = nullptr;
    }
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::GetPredication(ID3D10Predicate **ppPredicate, WINBOOL *pPredicateValue) {
  Com<ID3D11Query> d3d11_predicate;
  d3d11_context_->GetPredication(reinterpret_cast<ID3D11Predicate **>(&d3d11_predicate), pPredicateValue);
  if (d3d11_predicate) {
    d3d11_predicate->QueryInterface(__uuidof(ID3D10Query), reinterpret_cast<void **>(ppPredicate));
  } else {
    *ppPredicate = nullptr;
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView **ppShaderResourceViews) {
  ID3D11ShaderResourceView *d3d11_views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
  d3d11_context_->GSGetShaderResources(StartSlot, NumViews, d3d11_views);

  for (unsigned i = 0; i < NumViews; i++) {
    if (d3d11_views[i]) {
      d3d11_views[i]->QueryInterface(IID_PPV_ARGS(&ppShaderResourceViews[i]));
      d3d11_views[i]->Release();
    } else {
      ppShaderResourceViews[i] = nullptr;
    }
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState **ppSamplers) {
  ID3D11SamplerState *d3d11_samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];
  d3d11_context_->GSGetSamplers(StartSlot, NumSamplers, d3d11_samplers);

  for (unsigned i = 0; i < NumSamplers; i++) {
    if (d3d11_samplers[i]) {
      d3d11_samplers[i]->QueryInterface(IID_PPV_ARGS(&ppSamplers[i]));
      d3d11_samplers[i]->Release();
    } else {
      ppSamplers[i] = nullptr;
    }
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::OMGetRenderTargets(
    UINT NumViews, ID3D10RenderTargetView **ppRenderTargetViews, ID3D10DepthStencilView **ppDepthStencilView
) {
  ID3D11RenderTargetView *d3d11_rtv[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT];
  Com<ID3D11DepthStencilView> d3d11_dsv;

  d3d11_context_->OMGetRenderTargets(
      NumViews, ppRenderTargetViews ? d3d11_rtv : nullptr, ppDepthStencilView ? &d3d11_dsv : nullptr
  );

  if (ppRenderTargetViews != nullptr) {
    for (unsigned i = 0; i < NumViews; i++) {
      if (d3d11_rtv[i]) {
        d3d11_rtv[i]->QueryInterface(IID_PPV_ARGS(&ppRenderTargetViews[i]));
        d3d11_rtv[i]->Release();
      } else {
        ppRenderTargetViews[i] = nullptr;
      }
    }
  }

  if (ppDepthStencilView) {
    if (d3d11_dsv)
      d3d11_dsv->QueryInterface(IID_PPV_ARGS(ppDepthStencilView));
    else
      *ppDepthStencilView = nullptr;
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::OMGetBlendState(ID3D10BlendState **ppBlendState, FLOAT BlendFactor[4], UINT *pSampleMask) {
  Com<ID3D11BlendState> d3d11_state;
  d3d11_context_->OMGetBlendState(ppBlendState ? &d3d11_state : nullptr, BlendFactor, pSampleMask);

  if (ppBlendState != nullptr) {
    if (d3d11_state)
      d3d11_state->QueryInterface(IID_PPV_ARGS(ppBlendState));
    else
      *ppBlendState = nullptr;
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::OMGetDepthStencilState(ID3D10DepthStencilState **ppDepthStencilState, UINT *pStencilRef) {
  Com<ID3D11DepthStencilState> d3d11_state;
  d3d11_context_->OMGetDepthStencilState(ppDepthStencilState ? &d3d11_state : nullptr, pStencilRef);

  if (ppDepthStencilState != nullptr) {
    if (d3d11_state)
      d3d11_state->QueryInterface(IID_PPV_ARGS(ppDepthStencilState));
    else
      *ppDepthStencilState = nullptr;
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::SOGetTargets(UINT NumBuffers, ID3D10Buffer **ppSOTargets, UINT *pOffsets) {
  d3d11_context_->SOGetTargetsWithOffsets(
      NumBuffers, __uuidof(ID3D10Buffer), reinterpret_cast<void **>(ppSOTargets), pOffsets
  );
}

void STDMETHODCALLTYPE
MTLD3D10Device::RSGetState(ID3D10RasterizerState **ppRasterizerState) {
  Com<ID3D11RasterizerState> d3d11_state;
  d3d11_context_->RSGetState(&d3d11_state);

  if (d3d11_state)
    d3d11_state->QueryInterface(IID_PPV_ARGS(ppRasterizerState));
  else
    *ppRasterizerState = nullptr;
}

void STDMETHODCALLTYPE
MTLD3D10Device::RSGetViewports(UINT *NumViewports, D3D10_VIEWPORT *pViewports) {
  D3D11_VIEWPORT vp[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
  d3d11_context_->RSGetViewports(NumViewports, pViewports != nullptr ? vp : nullptr);

  if (pViewports != nullptr) {
    for (unsigned i = 0; i < *NumViewports; i++) {
      pViewports[i].TopLeftX = int32_t(vp[i].TopLeftX);
      pViewports[i].TopLeftY = int32_t(vp[i].TopLeftY);
      pViewports[i].Width = uint32_t(vp[i].Width);
      pViewports[i].Height = uint32_t(vp[i].Height);
      pViewports[i].MinDepth = vp[i].MinDepth;
      pViewports[i].MaxDepth = vp[i].MaxDepth;
    }
  }
}

void STDMETHODCALLTYPE
MTLD3D10Device::RSGetScissorRects(UINT *NumRects, D3D10_RECT *pRects) {
  d3d11_context_->RSGetScissorRects(NumRects, pRects);
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::GetDeviceRemovedReason() {
  return d3d11_device_->GetDeviceRemovedReason();
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::SetExceptionMode(UINT RaiseFlags) {
  return d3d11_device_->SetExceptionMode(RaiseFlags);
}

UINT STDMETHODCALLTYPE
MTLD3D10Device::GetExceptionMode() {
  return d3d11_device_->GetExceptionMode();
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) {
  return d3d11_device_->GetPrivateData(guid, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) {
  return d3d11_device_->SetPrivateData(guid, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) {
  return d3d11_device_->SetPrivateDataInterface(guid, pData);
}

void STDMETHODCALLTYPE
MTLD3D10Device::ClearState() {
  d3d11_context_->ClearState();
}

void STDMETHODCALLTYPE
MTLD3D10Device::Flush() {
  d3d11_context_->Flush();
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateBuffer(
    const D3D10_BUFFER_DESC *pDesc, const D3D10_SUBRESOURCE_DATA *pInitialData, ID3D10Buffer **ppBuffer
) {
  InitReturnPtr(ppBuffer);

  if (pDesc == nullptr)
    return E_INVALIDARG;

  D3D11_BUFFER_DESC d3d11_desc;
  d3d11_desc.ByteWidth = pDesc->ByteWidth;
  d3d11_desc.Usage = D3D11_USAGE(pDesc->Usage);
  d3d11_desc.BindFlags = pDesc->BindFlags;
  d3d11_desc.CPUAccessFlags = pDesc->CPUAccessFlags;
  d3d11_desc.MiscFlags = ConvertD3D10ResourceFlags(pDesc->MiscFlags);
  d3d11_desc.StructureByteStride = 0;

  Com<ID3D11Buffer> d3d11_buffer = nullptr;
  HRESULT hr = d3d11_device_->CreateBuffer(
      &d3d11_desc, reinterpret_cast<const D3D11_SUBRESOURCE_DATA *>(pInitialData), ppBuffer ? &d3d11_buffer : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppBuffer ? d3d11_buffer->QueryInterface(IID_PPV_ARGS(ppBuffer)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateTexture1D(
    const D3D10_TEXTURE1D_DESC *pDesc, const D3D10_SUBRESOURCE_DATA *pInitialData, ID3D10Texture1D **ppTexture1D
) {
  InitReturnPtr(ppTexture1D);

  if (pDesc == nullptr)
    return E_INVALIDARG;

  D3D11_TEXTURE1D_DESC d3d11_desc;
  d3d11_desc.Width = pDesc->Width;
  d3d11_desc.MipLevels = pDesc->MipLevels;
  d3d11_desc.ArraySize = pDesc->ArraySize;
  d3d11_desc.Format = pDesc->Format;
  d3d11_desc.Usage = D3D11_USAGE(pDesc->Usage);
  d3d11_desc.BindFlags = pDesc->BindFlags;
  d3d11_desc.CPUAccessFlags = pDesc->CPUAccessFlags;
  d3d11_desc.MiscFlags = ConvertD3D10ResourceFlags(pDesc->MiscFlags);

  Com<ID3D11Texture1D> d3d11_tex1d;
  HRESULT hr = d3d11_device_->CreateTexture1D(
      &d3d11_desc, reinterpret_cast<const D3D11_SUBRESOURCE_DATA *>(pInitialData),
      ppTexture1D != nullptr ? &d3d11_tex1d : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppTexture1D ? d3d11_tex1d->QueryInterface(IID_PPV_ARGS(ppTexture1D)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateTexture2D(
    const D3D10_TEXTURE2D_DESC *pDesc, const D3D10_SUBRESOURCE_DATA *pInitialData, ID3D10Texture2D **ppTexture2D
) {
  InitReturnPtr(ppTexture2D);

  if (pDesc == nullptr)
    return E_INVALIDARG;

  D3D11_TEXTURE2D_DESC d3d11_desc;
  d3d11_desc.Width = pDesc->Width;
  d3d11_desc.Height = pDesc->Height;
  d3d11_desc.MipLevels = pDesc->MipLevels;
  d3d11_desc.ArraySize = pDesc->ArraySize;
  d3d11_desc.Format = pDesc->Format;
  d3d11_desc.SampleDesc = pDesc->SampleDesc;
  d3d11_desc.Usage = D3D11_USAGE(pDesc->Usage);
  d3d11_desc.BindFlags = pDesc->BindFlags;
  d3d11_desc.CPUAccessFlags = pDesc->CPUAccessFlags;
  d3d11_desc.MiscFlags = ConvertD3D10ResourceFlags(pDesc->MiscFlags);

  Com<ID3D11Texture2D> d3d11_tex2d;
  HRESULT hr = d3d11_device_->CreateTexture2D(
      &d3d11_desc, reinterpret_cast<const D3D11_SUBRESOURCE_DATA *>(pInitialData), ppTexture2D ? &d3d11_tex2d : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppTexture2D ? d3d11_tex2d->QueryInterface(IID_PPV_ARGS(ppTexture2D)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateTexture3D(
    const D3D10_TEXTURE3D_DESC *pDesc, const D3D10_SUBRESOURCE_DATA *pInitialData, ID3D10Texture3D **ppTexture3D
) {
  InitReturnPtr(ppTexture3D);

  if (pDesc == nullptr)
    return E_INVALIDARG;

  D3D11_TEXTURE3D_DESC d3d11_desc;
  d3d11_desc.Width = pDesc->Width;
  d3d11_desc.Height = pDesc->Height;
  d3d11_desc.Depth = pDesc->Depth;
  d3d11_desc.MipLevels = pDesc->MipLevels;
  d3d11_desc.Format = pDesc->Format;
  d3d11_desc.Usage = D3D11_USAGE(pDesc->Usage);
  d3d11_desc.BindFlags = pDesc->BindFlags;
  d3d11_desc.CPUAccessFlags = pDesc->CPUAccessFlags;
  d3d11_desc.MiscFlags = ConvertD3D10ResourceFlags(pDesc->MiscFlags);

  Com<ID3D11Texture3D> d3d11_tex3d;
  HRESULT hr = d3d11_device_->CreateTexture3D(
      &d3d11_desc, reinterpret_cast<const D3D11_SUBRESOURCE_DATA *>(pInitialData), ppTexture3D ? &d3d11_tex3d : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppTexture3D ? d3d11_tex3d->QueryInterface(IID_PPV_ARGS(ppTexture3D)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateShaderResourceView(
    ID3D10Resource *pResource, const D3D10_SHADER_RESOURCE_VIEW_DESC *pDesc, ID3D10ShaderResourceView **ppSRView
) {
  InitReturnPtr(ppSRView);

  if (!pResource)
    return E_INVALIDARG;

  Com<ID3D11ShaderResourceView> d3d11_view;
  HRESULT hr = d3d11_device_->CreateShaderResourceView(
      /* compatible memory layout to MTLD3D10Texture1/2/3D */
      GetD3D11Interface<MTLD3D10Buffer>(pResource), reinterpret_cast<const D3D11_SHADER_RESOURCE_VIEW_DESC *>(pDesc),
      ppSRView ? &d3d11_view : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppSRView ? d3d11_view->QueryInterface(IID_PPV_ARGS(ppSRView)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateRenderTargetView(
    ID3D10Resource *pResource, const D3D10_RENDER_TARGET_VIEW_DESC *pDesc, ID3D10RenderTargetView **ppRTView
) {
  InitReturnPtr(ppRTView);

  if (!pResource)
    return E_INVALIDARG;

  ID3D11RenderTargetView *d3d11_view = nullptr;
  HRESULT hr = d3d11_device_->CreateRenderTargetView(
      /* compatible memory layout to MTLD3D10Texture1/2/3D */
      GetD3D11Interface<MTLD3D10Buffer>(pResource), reinterpret_cast<const D3D11_RENDER_TARGET_VIEW_DESC *>(pDesc),
      ppRTView ? &d3d11_view : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppRTView ? d3d11_view->QueryInterface(IID_PPV_ARGS(ppRTView)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateDepthStencilView(
    ID3D10Resource *pResource, const D3D10_DEPTH_STENCIL_VIEW_DESC *pDesc, ID3D10DepthStencilView **ppDepthStencilView
) {
  InitReturnPtr(ppDepthStencilView);

  if (!pResource)
    return E_INVALIDARG;

  D3D11_DEPTH_STENCIL_VIEW_DESC d3d11_desc;

  if (pDesc != nullptr) {
    d3d11_desc.ViewDimension = D3D11_DSV_DIMENSION(pDesc->ViewDimension);
    d3d11_desc.Format = pDesc->Format;
    d3d11_desc.Flags = 0;

    switch (pDesc->ViewDimension) {
    case D3D10_DSV_DIMENSION_UNKNOWN:
      break;

    case D3D10_DSV_DIMENSION_TEXTURE1D:
      d3d11_desc.Texture1D.MipSlice = pDesc->Texture1D.MipSlice;
      break;

    case D3D10_DSV_DIMENSION_TEXTURE1DARRAY:
      d3d11_desc.Texture1DArray.MipSlice = pDesc->Texture1DArray.MipSlice;
      d3d11_desc.Texture1DArray.FirstArraySlice = pDesc->Texture1DArray.FirstArraySlice;
      d3d11_desc.Texture1DArray.ArraySize = pDesc->Texture1DArray.ArraySize;
      break;

    case D3D10_DSV_DIMENSION_TEXTURE2D:
      d3d11_desc.Texture2D.MipSlice = pDesc->Texture2D.MipSlice;
      break;

    case D3D10_DSV_DIMENSION_TEXTURE2DARRAY:
      d3d11_desc.Texture2DArray.MipSlice = pDesc->Texture2DArray.MipSlice;
      d3d11_desc.Texture2DArray.FirstArraySlice = pDesc->Texture2DArray.FirstArraySlice;
      d3d11_desc.Texture2DArray.ArraySize = pDesc->Texture2DArray.ArraySize;
      break;

    case D3D10_DSV_DIMENSION_TEXTURE2DMS:
      break;

    case D3D10_DSV_DIMENSION_TEXTURE2DMSARRAY:
      d3d11_desc.Texture2DMSArray.FirstArraySlice = pDesc->Texture2DMSArray.FirstArraySlice;
      d3d11_desc.Texture2DMSArray.ArraySize = pDesc->Texture2DMSArray.ArraySize;
      break;
    }
  }

  Com<ID3D11DepthStencilView> d3d11_view;
  HRESULT hr = d3d11_device_->CreateDepthStencilView(
      /* compatible memory layout to MTLD3D10Texture1/2/3D */
      GetD3D11Interface<MTLD3D10Buffer>(pResource), pDesc ? &d3d11_desc : nullptr,
      ppDepthStencilView ? &d3d11_view : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppDepthStencilView ? d3d11_view->QueryInterface(IID_PPV_ARGS(ppDepthStencilView)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateInputLayout(
    const D3D10_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements, const void *pShaderBytecodeWithInputSignature,
    SIZE_T BytecodeLength, ID3D10InputLayout **ppInputLayout
) {
  InitReturnPtr(ppInputLayout);

  static_assert(sizeof(D3D10_INPUT_ELEMENT_DESC) == sizeof(D3D11_INPUT_ELEMENT_DESC));

  Com<ID3D11InputLayout> d3d11_ia = nullptr;
  HRESULT hr = d3d11_device_->CreateInputLayout(
      reinterpret_cast<const D3D11_INPUT_ELEMENT_DESC *>(pInputElementDescs), NumElements,
      pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout ? &d3d11_ia : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppInputLayout ? d3d11_ia->QueryInterface(IID_PPV_ARGS(ppInputLayout)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateVertexShader(
    const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D10VertexShader **ppVertexShader
) {
  InitReturnPtr(ppVertexShader);

  Com<ID3D11VertexShader> d3d11_shader;

  HRESULT hr = d3d11_device_->CreateVertexShader(
      pShaderBytecode, BytecodeLength, nullptr, ppVertexShader ? &d3d11_shader : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppVertexShader ? d3d11_shader->QueryInterface(IID_PPV_ARGS(ppVertexShader)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateGeometryShader(
    const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D10GeometryShader **ppGeometryShader
) {
  InitReturnPtr(ppGeometryShader);

  Com<ID3D11GeometryShader> d3d11_shader;

  HRESULT hr = d3d11_device_->CreateGeometryShader(
      pShaderBytecode, BytecodeLength, nullptr, ppGeometryShader ? &d3d11_shader : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppGeometryShader ? d3d11_shader->QueryInterface(IID_PPV_ARGS(ppGeometryShader)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateGeometryShaderWithStreamOutput(
    const void *pShaderBytecode, SIZE_T BytecodeLength, const D3D10_SO_DECLARATION_ENTRY *pSODeclaration,
    UINT NumEntries, UINT OutputStreamStride, ID3D10GeometryShader **ppGeometryShader
) {
  InitReturnPtr(ppGeometryShader);

  std::vector<D3D11_SO_DECLARATION_ENTRY> entries(NumEntries);

  for (unsigned i = 0; i < NumEntries; i++) {
    entries[i].Stream = 0;
    entries[i].SemanticName = pSODeclaration[i].SemanticName;
    entries[i].SemanticIndex = pSODeclaration[i].SemanticIndex;
    entries[i].StartComponent = pSODeclaration[i].StartComponent;
    entries[i].ComponentCount = pSODeclaration[i].ComponentCount;
    entries[i].OutputSlot = pSODeclaration[i].OutputSlot;
  }

  Com<ID3D11GeometryShader> d3d11_shader = nullptr;

  HRESULT hr = d3d11_device_->CreateGeometryShaderWithStreamOutput(
      pShaderBytecode, BytecodeLength, entries.data(), entries.size(), &OutputStreamStride, 1,
      D3D11_SO_NO_RASTERIZED_STREAM, nullptr, ppGeometryShader ? &d3d11_shader : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppGeometryShader ? d3d11_shader->QueryInterface(IID_PPV_ARGS(ppGeometryShader)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreatePixelShader(
    const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D10PixelShader **ppPixelShader
) {
  InitReturnPtr(ppPixelShader);

  Com<ID3D11PixelShader> d3d11_shader;

  HRESULT hr = d3d11_device_->CreatePixelShader(
      pShaderBytecode, BytecodeLength, nullptr, ppPixelShader ? &d3d11_shader : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppPixelShader ? d3d11_shader->QueryInterface(IID_PPV_ARGS(ppPixelShader)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateBlendState(const D3D10_BLEND_DESC *pBlendStateDesc, ID3D10BlendState **ppBlendState) {
  InitReturnPtr(ppBlendState);

  D3D11_BLEND_DESC d3d11_desc;

  if (pBlendStateDesc != nullptr) {
    d3d11_desc.AlphaToCoverageEnable = pBlendStateDesc->AlphaToCoverageEnable;
    d3d11_desc.IndependentBlendEnable = TRUE;

    for (unsigned i = 0; i < 8; i++) {
      d3d11_desc.RenderTarget[i].BlendEnable = pBlendStateDesc->BlendEnable[i];
      d3d11_desc.RenderTarget[i].SrcBlend = D3D11_BLEND(pBlendStateDesc->SrcBlend);
      d3d11_desc.RenderTarget[i].DestBlend = D3D11_BLEND(pBlendStateDesc->DestBlend);
      d3d11_desc.RenderTarget[i].BlendOp = D3D11_BLEND_OP(pBlendStateDesc->BlendOp);
      d3d11_desc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND(pBlendStateDesc->SrcBlendAlpha);
      d3d11_desc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND(pBlendStateDesc->DestBlendAlpha);
      d3d11_desc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP(pBlendStateDesc->BlendOpAlpha);
      d3d11_desc.RenderTarget[i].RenderTargetWriteMask = pBlendStateDesc->RenderTargetWriteMask[i];
    }
  }

  Com<ID3D11BlendState> d3d11_state;
  HRESULT hr = d3d11_device_->CreateBlendState(&d3d11_desc, ppBlendState ? &d3d11_state : nullptr);

  if (hr != S_OK)
    return hr;

  return ppBlendState ? d3d11_state->QueryInterface(IID_PPV_ARGS(ppBlendState)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateDepthStencilState(
    const D3D10_DEPTH_STENCIL_DESC *pDepthStencilDesc, ID3D10DepthStencilState **ppDepthStencilState
) {
  InitReturnPtr(ppDepthStencilState);

  Com<ID3D11DepthStencilState> d3d11_state = nullptr;
  HRESULT hr = d3d11_device_->CreateDepthStencilState(
      reinterpret_cast<const D3D11_DEPTH_STENCIL_DESC *>(pDepthStencilDesc),
      ppDepthStencilState ? &d3d11_state : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppDepthStencilState ? d3d11_state->QueryInterface(IID_PPV_ARGS(ppDepthStencilState)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateRasterizerState(
    const D3D10_RASTERIZER_DESC *pRasterizerDesc, ID3D10RasterizerState **ppRasterizerState
) {
  InitReturnPtr(ppRasterizerState);

  Com<ID3D11RasterizerState> d3d11_state = nullptr;
  HRESULT hr = d3d11_device_->CreateRasterizerState(
      reinterpret_cast<const D3D11_RASTERIZER_DESC *>(pRasterizerDesc), ppRasterizerState ? &d3d11_state : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppRasterizerState ? d3d11_state->QueryInterface(IID_PPV_ARGS(ppRasterizerState)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateSamplerState(const D3D10_SAMPLER_DESC *pSamplerDesc, ID3D10SamplerState **ppSamplerState) {
  InitReturnPtr(ppSamplerState);

  if (pSamplerDesc == nullptr)
    return E_INVALIDARG;

  D3D11_SAMPLER_DESC d3d11_desc;
  d3d11_desc.Filter = D3D11_FILTER(pSamplerDesc->Filter);
  d3d11_desc.AddressU = D3D11_TEXTURE_ADDRESS_MODE(pSamplerDesc->AddressU);
  d3d11_desc.AddressV = D3D11_TEXTURE_ADDRESS_MODE(pSamplerDesc->AddressV);
  d3d11_desc.AddressW = D3D11_TEXTURE_ADDRESS_MODE(pSamplerDesc->AddressW);
  d3d11_desc.MipLODBias = pSamplerDesc->MipLODBias;
  d3d11_desc.MaxAnisotropy = pSamplerDesc->MaxAnisotropy;
  d3d11_desc.ComparisonFunc = D3D11_COMPARISON_FUNC(pSamplerDesc->ComparisonFunc);
  d3d11_desc.MinLOD = pSamplerDesc->MinLOD;
  d3d11_desc.MaxLOD = pSamplerDesc->MaxLOD;

  for (unsigned i = 0; i < 4; i++)
    d3d11_desc.BorderColor[i] = pSamplerDesc->BorderColor[i];

  Com<ID3D11SamplerState> d3d11_state;
  HRESULT hr = d3d11_device_->CreateSamplerState(&d3d11_desc, ppSamplerState ? &d3d11_state : nullptr);

  if (hr != S_OK)
    return hr;

  return ppSamplerState ? d3d11_state->QueryInterface(IID_PPV_ARGS(ppSamplerState)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateQuery(const D3D10_QUERY_DESC *pQueryDesc, ID3D10Query **ppQuery) {
  InitReturnPtr(ppQuery);

  if (pQueryDesc == nullptr)
    return E_INVALIDARG;

  D3D11_QUERY_DESC d3d11_desc;
  d3d11_desc.Query = D3D11_QUERY(pQueryDesc->Query);
  d3d11_desc.MiscFlags = pQueryDesc->MiscFlags;

  Com<ID3D11Query> d3d11_query;
  HRESULT hr = d3d11_device_->CreateQuery(&d3d11_desc, ppQuery ? &d3d11_query : nullptr);

  if (hr != S_OK)
    return hr;

  return ppQuery ? d3d11_query->QueryInterface(IID_PPV_ARGS(ppQuery)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreatePredicate(const D3D10_QUERY_DESC *pPredicateDesc, ID3D10Predicate **ppPredicate) {
  InitReturnPtr(ppPredicate);

  D3D11_QUERY_DESC d3d11_desc;
  d3d11_desc.Query = D3D11_QUERY(pPredicateDesc->Query);
  d3d11_desc.MiscFlags = pPredicateDesc->MiscFlags;

  Com<ID3D11Predicate> d3d11_predicate;
  HRESULT hr = d3d11_device_->CreatePredicate(&d3d11_desc, ppPredicate ? &d3d11_predicate : nullptr);

  if (hr != S_OK)
    return hr;

  return ppPredicate ? d3d11_predicate->QueryInterface(IID_PPV_ARGS(ppPredicate)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateCounter(const D3D10_COUNTER_DESC *pCounterDesc, ID3D10Counter **ppCounter) {
  UNIMPLEMENTED("CreateCounter");
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CheckFormatSupport(DXGI_FORMAT Format, UINT *pFormatSupport) {
  // FIXME: valid mask pFormatSupport & 0x1FFFFFF
  return d3d11_device_->CheckFormatSupport(Format, pFormatSupport);
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CheckMultisampleQualityLevels(DXGI_FORMAT Format, UINT SampleCount, UINT *pNumQualityLevels) {
  return d3d11_device_->CheckMultisampleQualityLevels(Format, SampleCount, pNumQualityLevels);
}

void STDMETHODCALLTYPE
MTLD3D10Device::CheckCounterInfo(D3D10_COUNTER_INFO *pCounterInfo) {
  UNIMPLEMENTED("CheckCounterInfo");
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CheckCounter(
    const D3D10_COUNTER_DESC *pDesc, D3D10_COUNTER_TYPE *pType, UINT *pActiveCounters, char *name, UINT *pNameLength,
    char *units, UINT *pUnitsLength, char *description, UINT *pDescriptionLength
) {
  UNIMPLEMENTED("CheckCounter");
}

UINT STDMETHODCALLTYPE
MTLD3D10Device::GetCreationFlags() {
  return d3d11_device_->GetCreationFlags();
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::OpenSharedResource(HANDLE hResource, REFIID ReturnedInterface, void **ppResource) {
  return d3d11_device_->OpenSharedResource(hResource, ReturnedInterface, ppResource);
}

void STDMETHODCALLTYPE
MTLD3D10Device::SetTextFilterSize(UINT Width, UINT Height) {}

void STDMETHODCALLTYPE
MTLD3D10Device::GetTextFilterSize(UINT *pWidth, UINT *pHeight) {
  if (pWidth)
    *pWidth = 0;

  if (pHeight)
    *pHeight = 0;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateShaderResourceView1(
    ID3D10Resource *pResource, const D3D10_SHADER_RESOURCE_VIEW_DESC1 *pDesc, ID3D10ShaderResourceView1 **ppSRView
) {
  InitReturnPtr(ppSRView);

  if (!pResource)
    return E_INVALIDARG;

  Com<ID3D11ShaderResourceView> d3d11_view;
  HRESULT hr = d3d11_device_->CreateShaderResourceView(
      /* compatible memory layout to MTLD3D10Texture1/2/3D */
      GetD3D11Interface<MTLD3D10Buffer>(pResource), reinterpret_cast<const D3D11_SHADER_RESOURCE_VIEW_DESC *>(pDesc),
      ppSRView ? &d3d11_view : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppSRView ? d3d11_view->QueryInterface(IID_PPV_ARGS(ppSRView)) : hr;
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Device::CreateBlendState1(const D3D10_BLEND_DESC1 *pBlendStateDesc, ID3D10BlendState1 **ppBlendState) {
  InitReturnPtr(ppBlendState);

  Com<ID3D11BlendState> d3d11_state;
  HRESULT hr = d3d11_device_->CreateBlendState(
      reinterpret_cast<const D3D11_BLEND_DESC *>(pBlendStateDesc), ppBlendState ? &d3d11_state : nullptr
  );

  if (hr != S_OK)
    return hr;

  return ppBlendState ? d3d11_state->QueryInterface(IID_PPV_ARGS(ppBlendState)) : hr;
}

D3D10_FEATURE_LEVEL1 STDMETHODCALLTYPE
MTLD3D10Device::GetFeatureLevel() {
  return D3D10_FEATURE_LEVEL1(d3d11_device_->GetFeatureLevel());
}

} // namespace dxmt