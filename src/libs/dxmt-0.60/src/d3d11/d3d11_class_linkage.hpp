#pragma once

#include "d3d11_device_child.hpp"

namespace dxmt {

class MTLD3D11Device;

// TODO implement properly
class MTLD3D11ClassLinkage : public MTLD3D11DeviceChild<ID3D11ClassLinkage> {

public:
  MTLD3D11ClassLinkage(MTLD3D11Device *pDevice);

  ~MTLD3D11ClassLinkage();

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) final;

  HRESULT STDMETHODCALLTYPE
  CreateClassInstance(LPCSTR pClassTypeName, UINT ConstantBufferOffset,
                      UINT ConstantVectorOffset, UINT TextureOffset,
                      UINT SamplerOffset, ID3D11ClassInstance **ppInstance);

  HRESULT STDMETHODCALLTYPE GetClassInstance(LPCSTR pClassInstanceName,
                                             UINT InstanceIndex,
                                             ID3D11ClassInstance **ppInstance);
};

} // namespace dxmt