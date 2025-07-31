
#pragma once

#include "d3d11_device.hpp"
#include "d3d11_device_child.hpp"

namespace dxmt {

using MTLD3D11DeviceContextBase =
    MTLD3D11DeviceChild<IMTLD3D11DeviceContext>;

class CommandQueue;

std::unique_ptr<MTLD3D11DeviceContextBase>
InitializeImmediateContext(MTLD3D11Device *device, CommandQueue& cmd_queue);

Com<MTLD3D11DeviceContextBase>
CreateDeferredContext(MTLD3D11Device *pDevice, UINT ContextFlags);

class MTLD3D11CommandListPoolBase {
public:
  virtual ~MTLD3D11CommandListPoolBase() {}
  virtual void RecycleCommandList(ID3D11CommandList *cmdlist) = 0;
  virtual void CreateCommandList(ID3D11CommandList **ppCommandList) = 0;
};

std::unique_ptr<MTLD3D11CommandListPoolBase> InitializeCommandListPool(MTLD3D11Device *device);

} // namespace dxmt