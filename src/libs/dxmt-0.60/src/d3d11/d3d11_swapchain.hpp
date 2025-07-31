#pragma once

#include "d3d11_device.hpp"

namespace dxmt {

HRESULT
CreateSwapChain(IDXGIFactory1 *pFactory, MTLD3D11Device *pDevice, HWND hWnd,
                const DXGI_SWAP_CHAIN_DESC1 *pDesc,
                const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
                IDXGISwapChain1 **ppSwapChain);

} // namespace dxmt