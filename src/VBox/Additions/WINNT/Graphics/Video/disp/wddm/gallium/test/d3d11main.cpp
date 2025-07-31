/* $Id: d3d11main.cpp 110306 2025-07-18 13:59:09Z vitali.pelenjow@oracle.com $ */
/** @file
 * D3D testcase. Win32 application to run D3D11 tests.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "d3d11render.h"

#include <iprt/file.h>
#include <iprt/formats/bmp.h>
#include <iprt/alloc.h>
#include <iprt/string.h>

/*
 * Utilities.
 */
uint8_t *readBmpFile(char const *pszFilename, uint32_t cbPixel, uint32_t *pWidth, uint32_t *pHeight, uint32_t *pcbData)
{
    uint8_t *pu8Data = NULL;
    uint32_t cbFileData = 0;

    RTFILE f = NIL_RTFILE;
    int rc = RTFileOpen(&f, pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        BMPFILEHDR fileHdr;
        rc = RTFileRead(f, &fileHdr, sizeof(fileHdr), NULL);
        if (RT_SUCCESS(rc))
        {
            uint32_t const cbHdr = fileHdr.offBits - sizeof(fileHdr);
            if (cbHdr == sizeof(BITMAPV4HEADER))
            {
                BITMAPV4HEADER hdrV4;
                rc = RTFileRead(f, &hdrV4, sizeof(hdrV4), NULL);
                if (RT_SUCCESS(rc))
                {
                    *pWidth = hdrV4.bV4Width;
                    *pHeight = (uint32_t)-hdrV4.bV4Height;
                }
            }
            else if (cbHdr == sizeof(BMPWIN3XINFOHDR))
            {
                BMPWIN3XINFOHDR coreHdr;
                rc = RTFileRead(f, &coreHdr, sizeof(coreHdr), NULL);
                if (RT_SUCCESS(rc))
                {
                    *pWidth = coreHdr.uWidth;
                    *pHeight = (uint32_t)(-(int32_t)coreHdr.uHeight);
                }
            }
            else
                rc = VERR_NOT_SUPPORTED;

            if (RT_SUCCESS(rc))
            {
                cbFileData = fileHdr.cbFileSize - fileHdr.offBits;
                pu8Data = (uint8_t *)RTMemAlloc(cbFileData);
                if (pu8Data)
                    rc = RTFileRead(f, pu8Data, cbFileData, NULL);
                else
                    rc = VERR_NO_MEMORY;
            }
        }

        RTFileClose(f);
    }

    if (RT_SUCCESS(rc))
    {
        /* If the actual data has less bits than 32, then convert the pixels. */
        uint32_t const *s = (uint32_t *)pu8Data;
        if (cbPixel == 2)
        {
            uint16_t *d = (uint16_t *)pu8Data;
            for (unsigned i = 0 ; i < cbFileData / 4; ++i)
                *d++ = (uint16_t)*s++;
        }
        else if (cbPixel == 1)
        {
            uint8_t *d = (uint8_t *)pu8Data;
            for (unsigned i = 0 ; i < cbFileData / 4; ++i)
                *d++ = (uint8_t)*s++;
        }

        *pcbData = (cbFileData / 4) * cbPixel;
    }
    else
    {
        RTMemFree(pu8Data);
        pu8Data = NULL;
    }

    return pu8Data;
}


class D3D11Test : public D3D11DeviceProvider
{
public:
    D3D11Test();
    ~D3D11Test();

    HRESULT Init(HINSTANCE hInstance, int argc, char **argv, int nCmdShow);
    int Run();

    virtual ID3D11Device *Device();
    virtual ID3D11DeviceContext *ImmediateContext();
    virtual ID3D11RenderTargetView *RenderTargetView();
    virtual ID3D11DepthStencilView *DepthStencilView();

private:
    HRESULT initWindow(HINSTANCE hInstance, int nCmdShow);
    HRESULT initDirect3D11();
    void parseCmdLine(int argc, char **argv);
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    int miRenderId;
    enum
    {
        RenderModeStep = 0,
        RenderModeContinuous = 1,
        RenderModeFPS = 2
    } miRenderMode;

    HWND mHwnd;
    UINT mRTWidth;
    UINT mRTHeight;

    DXGI_FORMAT mFormat;

    struct
    {
        ID3D11Device           *pDevice;               /* Device for rendering. */
        ID3D11DeviceContext    *pImmediateContext;     /* Associated context. */
        IDXGIFactory           *pDxgiFactory;          /* DXGI Factory associated with the rendering device. */
        ID3D11Texture2D        *pRenderTarget;         /* The render target. */
        ID3D11RenderTargetView *pRenderTargetView;     /* The render target view. */
        IDXGIResource          *pDxgiResource;         /* Interface of the render target. */
        IDXGIKeyedMutex        *pDXGIKeyedMutex;       /* Synchronization interface for the render device. */
        ID3D11Texture2D        *pDepthStencilBuffer;
        ID3D11DepthStencilView *pDepthStencilView;
    } mRender;

    HANDLE mSharedHandle;

    struct
    {
        ID3D11Device           *pDevice;               /* Device for the output drawing. */
        ID3D11DeviceContext    *pImmediateContext;     /* Corresponding context. */
        IDXGIFactory           *pDxgiFactory;          /* DXGI Factory associated with the output device. */
        IDXGISwapChain         *pSwapChain;            /* Swap chain for displaying in the mHwnd window. */
        ID3D11Texture2D        *pSharedTexture;        /* The texture to draw. Shared resource of mRender.pRenderTarget */
        IDXGIKeyedMutex        *pDXGIKeyedMutex;       /* Synchronization interface for the render device. */
    } mOutput;

    D3D11Render *mpRender;
};

D3D11Test::D3D11Test()
    :
    miRenderId(1),
    miRenderMode(RenderModeStep),
    mHwnd(0),
    mRTWidth(0),
    mRTHeight(0),
    mFormat(DXGI_FORMAT_B8G8R8A8_UNORM),
    mSharedHandle(0),
    mpRender(0)
{
    RT_ZERO(mRender);
    RT_ZERO(mOutput);
}

D3D11Test::~D3D11Test()
{
    if (mpRender)
    {
        delete mpRender;
        mpRender = 0;
    }

    if (mOutput.pImmediateContext)
        mOutput.pImmediateContext->ClearState();
    if (mRender.pImmediateContext)
        mRender.pImmediateContext->ClearState();

    D3D_RELEASE(mOutput.pDevice);
    D3D_RELEASE(mOutput.pImmediateContext);
    D3D_RELEASE(mOutput.pDxgiFactory);
    D3D_RELEASE(mOutput.pSwapChain);
    D3D_RELEASE(mOutput.pSharedTexture);
    D3D_RELEASE(mOutput.pDXGIKeyedMutex);

    D3D_RELEASE(mRender.pDevice);
    D3D_RELEASE(mRender.pImmediateContext);
    D3D_RELEASE(mRender.pDxgiFactory);
    D3D_RELEASE(mRender.pRenderTarget);
    D3D_RELEASE(mRender.pRenderTargetView);
    D3D_RELEASE(mRender.pDxgiResource);
    D3D_RELEASE(mRender.pDXGIKeyedMutex);
}

LRESULT CALLBACK D3D11Test::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

HRESULT D3D11Test::initWindow(HINSTANCE hInstance,
                             int nCmdShow)
{
    HRESULT hr = S_OK;

    WNDCLASSA wc;
    RT_ZERO(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName  = 0;
    wc.lpszClassName = "D3D11TestWndClassName";

    if (RegisterClassA(&wc))
    {
       RECT r = {0, 0, 800, 600};
       AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, false);

       mHwnd = CreateWindowA("D3D11TestWndClassName",
                             "D3D11 Test",
                             WS_OVERLAPPEDWINDOW,
                             100, 100, r.right, r.bottom,
                             0, 0, hInstance, 0);
       if (mHwnd)
       {
           ShowWindow(mHwnd, nCmdShow);
           UpdateWindow(mHwnd);
       }
       else
       {
           D3DTestShowError(hr, "CreateWindow");
           hr = E_FAIL;
       }
    }
    else
    {
        D3DTestShowError(hr, "RegisterClass");
        hr = E_FAIL;
    }

    return hr;
}

static HRESULT d3d11TestCreateDevice(ID3D11Device **ppDevice,
                                     ID3D11DeviceContext **ppImmediateContext,
                                     IDXGIFactory **ppDxgiFactory)
{
    HRESULT hr = S_OK;

    IDXGIAdapter *pAdapter = NULL; /* Default adapter. */
    static const D3D_FEATURE_LEVEL aFeatureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1
    };
    UINT Flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef DEBUG
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_9_1;

    hr = D3D11CreateDevice(pAdapter,
                           D3D_DRIVER_TYPE_HARDWARE,
                           NULL,
                           Flags,
                           aFeatureLevels,
                           RT_ELEMENTS(aFeatureLevels),
                           D3D11_SDK_VERSION,
                           ppDevice,
                           &FeatureLevel,
                           ppImmediateContext);
    if (FAILED(hr) && RT_BOOL(Flags & D3D11_CREATE_DEVICE_DEBUG))
    {
        /* Device creation may fail because _DEBUG flag requires "D3D11 SDK Layers for Windows 10" ("Graphics Tools"):
         *   Settings/System/Apps/Optional features/Add a feature/Graphics Tools
         * Retry without the flag.
         */
        Flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(pAdapter,
                               D3D_DRIVER_TYPE_HARDWARE,
                               NULL,
                               Flags,
                               aFeatureLevels,
                               RT_ELEMENTS(aFeatureLevels),
                               D3D11_SDK_VERSION,
                               ppDevice,
                               &FeatureLevel,
                               ppImmediateContext);
    }
    D3DAssertHR(hr);

    if (FeatureLevel != D3D_FEATURE_LEVEL_11_1)
    {
        char sz[128];
        RTStrPrintf(sz, sizeof(sz), "Feature level %x", FeatureLevel);
        D3DTestShowError(hr, sz);
    }

    IDXGIDevice *pDxgiDevice = 0;
    hr = (*ppDevice)->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDxgiDevice);
    if (SUCCEEDED(hr))
    {
        IDXGIAdapter *pDxgiAdapter = 0;
        hr = pDxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&pDxgiAdapter);
        if (SUCCEEDED(hr))
        {
            HTEST(pDxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void**)ppDxgiFactory));
            D3D_RELEASE(pDxgiAdapter);
        }
        else
            D3DTestShowError(hr, "IDXGIAdapter");

        D3D_RELEASE(pDxgiDevice);
    }
    else
        D3DTestShowError(hr, "IDXGIDevice");

    return hr;
}

HRESULT D3D11Test::initDirect3D11()
{
    HRESULT hr = S_OK;

    RECT clientRect;
    GetClientRect(mHwnd, &clientRect);
    mRTWidth = clientRect.right;
    mRTHeight = clientRect.bottom;

    bool const fDirectOutput = mpRender->IsDirectOutputRequired(this);

    /*
     * Render.
     */
    d3d11TestCreateDevice(&mRender.pDevice,
                          &mRender.pImmediateContext,
                          &mRender.pDxgiFactory);
    if (mRender.pDevice)
    {
        if (!fDirectOutput)
        {
            D3D11_TEXTURE2D_DESC texDesc;
            RT_ZERO(texDesc);
            texDesc.Width     = mRTWidth;
            texDesc.Height    = mRTHeight;
            texDesc.MipLevels = 1;
            texDesc.ArraySize = 1;
            texDesc.Format    = mFormat;
            texDesc.SampleDesc.Count   = 1;
            texDesc.SampleDesc.Quality = 0;
            texDesc.Usage          = D3D11_USAGE_DEFAULT;
            texDesc.BindFlags      = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            texDesc.CPUAccessFlags = 0;
            texDesc.MiscFlags      = fDirectOutput ? 0 : D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

            HTEST(mRender.pDevice->CreateTexture2D(&texDesc, 0, &mRender.pRenderTarget));
            HTEST(mRender.pDevice->CreateRenderTargetView(mRender.pRenderTarget, 0, &mRender.pRenderTargetView));

            /* Get the shared handle. */
            HTEST(mRender.pRenderTarget->QueryInterface(__uuidof(IDXGIResource), (void**)&mRender.pDxgiResource));
            HTEST(mRender.pDxgiResource->GetSharedHandle(&mSharedHandle));

            HTEST(mRender.pRenderTarget->QueryInterface(__uuidof(IDXGIKeyedMutex), (LPVOID*)&mRender.pDXGIKeyedMutex));
        }

        if (mpRender->IsDepthStencilBufferRequired(this))
        {
            D3D11_TEXTURE2D_DESC depthStencilDesc;
            depthStencilDesc.Width     = mRTWidth;
            depthStencilDesc.Height    = mRTHeight;
            depthStencilDesc.MipLevels = 1;
            depthStencilDesc.ArraySize = 1;
            depthStencilDesc.Format    = DXGI_FORMAT_D24_UNORM_S8_UINT;
            depthStencilDesc.SampleDesc.Count   = 1;
            depthStencilDesc.SampleDesc.Quality = 0;
            depthStencilDesc.Usage          = D3D11_USAGE_DEFAULT;
            depthStencilDesc.BindFlags      = D3D11_BIND_DEPTH_STENCIL;
            depthStencilDesc.CPUAccessFlags = 0;
            depthStencilDesc.MiscFlags      = 0;

            HTEST(mRender.pDevice->CreateTexture2D(&depthStencilDesc, 0, &mRender.pDepthStencilBuffer));
            HTEST(mRender.pDevice->CreateDepthStencilView(mRender.pDepthStencilBuffer, 0, &mRender.pDepthStencilView));
        }
    }

    if (mRender.pImmediateContext)
    {
        // Set the viewport transform.
        D3D11_VIEWPORT mScreenViewport;
        mScreenViewport.TopLeftX = 0;
        mScreenViewport.TopLeftY = 0;
        mScreenViewport.Width    = static_cast<float>(mRTWidth);
        mScreenViewport.Height   = static_cast<float>(mRTHeight);
        mScreenViewport.MinDepth = 0.0f;
        mScreenViewport.MaxDepth = 1.0f;

        mRender.pImmediateContext->RSSetViewports(1, &mScreenViewport);
    }

    /*
     * Output.
     */
    if (fDirectOutput)
    {
        if (mRender.pDxgiFactory)
        {
            DXGI_SWAP_CHAIN_DESC sd;
            RT_ZERO(sd);
            sd.BufferDesc.Width  = mRTWidth;
            sd.BufferDesc.Height = mRTHeight;
            sd.BufferDesc.RefreshRate.Numerator = 60;
            sd.BufferDesc.RefreshRate.Denominator = 1;
            sd.BufferDesc.Format = mFormat;
            sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
            sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
            sd.SampleDesc.Count   = 1;
            sd.SampleDesc.Quality = 0;
            sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount  = 1;
            sd.OutputWindow = mHwnd;
            sd.Windowed     = true;
            sd.SwapEffect   = DXGI_SWAP_EFFECT_DISCARD;
            sd.Flags        = 0;

            HTEST(mRender.pDxgiFactory->CreateSwapChain(mRender.pDevice, &sd, &mOutput.pSwapChain));

            HTEST(mOutput.pSwapChain->ResizeBuffers(1, mRTWidth, mRTHeight, mFormat, 0));
        }
    }
    else
    {
        d3d11TestCreateDevice(&mOutput.pDevice,
                              &mOutput.pImmediateContext,
                              &mOutput.pDxgiFactory);
        if (mOutput.pDxgiFactory)
        {
            DXGI_SWAP_CHAIN_DESC sd;
            RT_ZERO(sd);
            sd.BufferDesc.Width  = mRTWidth;
            sd.BufferDesc.Height = mRTHeight;
            sd.BufferDesc.RefreshRate.Numerator = 60;
            sd.BufferDesc.RefreshRate.Denominator = 1;
            sd.BufferDesc.Format = mFormat;
            sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
            sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
            sd.SampleDesc.Count   = 1;
            sd.SampleDesc.Quality = 0;
            sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount  = 1;
            sd.OutputWindow = mHwnd;
            sd.Windowed     = true;
            sd.SwapEffect   = DXGI_SWAP_EFFECT_DISCARD;
            sd.Flags        = 0;

            HTEST(mOutput.pDxgiFactory->CreateSwapChain(mOutput.pDevice, &sd, &mOutput.pSwapChain));

            HTEST(mOutput.pSwapChain->ResizeBuffers(1, mRTWidth, mRTHeight, mFormat, 0));

            HTEST(mOutput.pDevice->OpenSharedResource(mSharedHandle, __uuidof(ID3D11Texture2D), (void**)&mOutput.pSharedTexture));
            HTEST(mOutput.pSharedTexture->QueryInterface(__uuidof(IDXGIKeyedMutex), (LPVOID*)&mOutput.pDXGIKeyedMutex));
        }
    }

    return hr;
}

void D3D11Test::parseCmdLine(int argc, char **argv)
{
    /* Very simple: a test identifier followed by the render mode. */

    /* First number is the render id. */
    if (argc >= 2)
        miRenderId = RTStrToInt32(argv[1]);

    /* Second number is the render/step mode. */
    if (argc >= 3)
    {
        int i = RTStrToInt32(argv[2]);
        switch (i)
        {
            default:
            case 0: miRenderMode = RenderModeStep;       break;
            case 1: miRenderMode = RenderModeContinuous; break;
            case 2: miRenderMode = RenderModeFPS;        break;
        }
    }
}

HRESULT D3D11Test::Init(HINSTANCE hInstance, int argc, char **argv, int nCmdShow)
{
    parseCmdLine(argc, argv);

    HRESULT hr = initWindow(hInstance, nCmdShow);
    if (SUCCEEDED(hr))
    {
        mpRender = CreateRender(miRenderId);
        if (mpRender)
        {
            hr = initDirect3D11();
            if (SUCCEEDED(hr))
            {
                hr = mpRender->InitRender(this);
                if (FAILED(hr))
                {
                    D3DTestShowError(hr, "InitRender");
                }
            }
        }
        else
        {
            D3DTestShowError(0, "No render");
            hr = E_FAIL;
        }
    }

    return hr;
}

#include "d3d11blitter.hlsl.vs.h"
#include "d3d11blitter.hlsl.ps.h"

typedef struct D3D11BLITTER
{
    ID3D11Device           *pDevice;
    ID3D11DeviceContext    *pImmediateContext;

    ID3D11VertexShader     *pVertexShader;
    ID3D11PixelShader      *pPixelShader;
    ID3D11SamplerState     *pSamplerState;
    ID3D11RasterizerState  *pRasterizerState;
    ID3D11BlendState       *pBlendState;
} D3D11BLITTER;


static void BlitRelease(D3D11BLITTER *pBlitter)
{
    D3D_RELEASE(pBlitter->pVertexShader);
    D3D_RELEASE(pBlitter->pPixelShader);
    D3D_RELEASE(pBlitter->pSamplerState);
    D3D_RELEASE(pBlitter->pRasterizerState);
    D3D_RELEASE(pBlitter->pBlendState);
    RT_ZERO(*pBlitter);
}


static HRESULT BlitInit(D3D11BLITTER *pBlitter, ID3D11Device *pDevice, ID3D11DeviceContext *pImmediateContext)
{
    HRESULT hr;

    RT_ZERO(*pBlitter);

    pBlitter->pDevice = pDevice;
    pBlitter->pImmediateContext = pImmediateContext;

    HTEST(pBlitter->pDevice->CreateVertexShader(g_vs_blitter, sizeof(g_vs_blitter), NULL, &pBlitter->pVertexShader));
    HTEST(pBlitter->pDevice->CreatePixelShader(g_ps_blitter, sizeof(g_ps_blitter), NULL, &pBlitter->pPixelShader));

    D3D11_SAMPLER_DESC SamplerDesc;
    SamplerDesc.Filter         = D3D11_FILTER_ANISOTROPIC;
    SamplerDesc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
    SamplerDesc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
    SamplerDesc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
    SamplerDesc.MipLODBias     = 0.0f;
    SamplerDesc.MaxAnisotropy  = 4;
    SamplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    SamplerDesc.BorderColor[0] = 0.0f;
    SamplerDesc.BorderColor[1] = 0.0f;
    SamplerDesc.BorderColor[2] = 0.0f;
    SamplerDesc.BorderColor[3] = 0.0f;
    SamplerDesc.MinLOD         = 0.0f;
    SamplerDesc.MaxLOD         = 0.0f;
    HTEST(pBlitter->pDevice->CreateSamplerState(&SamplerDesc, &pBlitter->pSamplerState));

    D3D11_RASTERIZER_DESC RasterizerDesc;
    RasterizerDesc.FillMode              = D3D11_FILL_SOLID;
    RasterizerDesc.CullMode              = D3D11_CULL_NONE;
    RasterizerDesc.FrontCounterClockwise = FALSE;
    RasterizerDesc.DepthBias             = 0;
    RasterizerDesc.DepthBiasClamp        = 0.0f;
    RasterizerDesc.SlopeScaledDepthBias  = 0.0f;
    RasterizerDesc.DepthClipEnable       = FALSE;
    RasterizerDesc.ScissorEnable         = FALSE;
    RasterizerDesc.MultisampleEnable     = FALSE;
    RasterizerDesc.AntialiasedLineEnable = FALSE;
    HTEST(pBlitter->pDevice->CreateRasterizerState(&RasterizerDesc, &pBlitter->pRasterizerState));

    D3D11_BLEND_DESC BlendDesc;
    BlendDesc.AlphaToCoverageEnable = FALSE;
    BlendDesc.IndependentBlendEnable = FALSE;
    for (unsigned i = 0; i < RT_ELEMENTS(BlendDesc.RenderTarget); ++i)
    {
        BlendDesc.RenderTarget[i].BlendEnable           = FALSE;
        BlendDesc.RenderTarget[i].SrcBlend              = D3D11_BLEND_SRC_COLOR;
        BlendDesc.RenderTarget[i].DestBlend             = D3D11_BLEND_ZERO;
        BlendDesc.RenderTarget[i].BlendOp               = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[i].SrcBlendAlpha         = D3D11_BLEND_SRC_ALPHA;
        BlendDesc.RenderTarget[i].DestBlendAlpha        = D3D11_BLEND_ZERO;
        BlendDesc.RenderTarget[i].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[i].RenderTargetWriteMask = 0xF;
    }
    HTEST(pBlitter->pDevice->CreateBlendState(&BlendDesc, &pBlitter->pBlendState));

    return S_OK;
}


static HRESULT BlitFromTexture(D3D11BLITTER *pBlitter, ID3D11RenderTargetView *pDstRenderTargetView,
                               float cDstWidth, float cDstHeight, D3D11_RECT const &rectDst,
                               ID3D11ShaderResourceView *pSrcShaderResourceView)
{
    HRESULT hr;

    /*
     * Save pipeline state.
     */
    struct
    {
        D3D11_PRIMITIVE_TOPOLOGY    Topology;
        ID3D11InputLayout          *pInputLayout;
        ID3D11Buffer               *pConstantBuffer;
        ID3D11VertexShader         *pVertexShader;
        ID3D11ShaderResourceView   *pShaderResourceView;
        ID3D11PixelShader          *pPixelShader;
        ID3D11SamplerState         *pSamplerState;
        ID3D11RasterizerState      *pRasterizerState;
        ID3D11BlendState           *pBlendState;
        FLOAT                       BlendFactor[4];
        UINT                        SampleMask;
        ID3D11RenderTargetView     *apRenderTargetView[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
        ID3D11DepthStencilView     *pDepthStencilView;
        UINT                        NumViewports;
        D3D11_VIEWPORT              aViewport[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    } SavedState;

    pBlitter->pImmediateContext->IAGetPrimitiveTopology(&SavedState.Topology);
    pBlitter->pImmediateContext->IAGetInputLayout(&SavedState.pInputLayout);
    pBlitter->pImmediateContext->VSGetConstantBuffers(0, 1, &SavedState.pConstantBuffer);
    pBlitter->pImmediateContext->VSGetShader(&SavedState.pVertexShader, NULL, NULL);
    pBlitter->pImmediateContext->PSGetShaderResources(0, 1, &SavedState.pShaderResourceView);
    pBlitter->pImmediateContext->PSGetShader(&SavedState.pPixelShader, NULL, NULL);
    pBlitter->pImmediateContext->PSGetSamplers(0, 1, &SavedState.pSamplerState);
    pBlitter->pImmediateContext->RSGetState(&SavedState.pRasterizerState);
    pBlitter->pImmediateContext->OMGetBlendState(&SavedState.pBlendState, SavedState.BlendFactor, &SavedState.SampleMask);
    pBlitter->pImmediateContext->OMGetRenderTargets(RT_ELEMENTS(SavedState.apRenderTargetView), SavedState.apRenderTargetView, &SavedState.pDepthStencilView);
    SavedState.NumViewports = RT_ELEMENTS(SavedState.aViewport);
    pBlitter->pImmediateContext->RSGetViewports(&SavedState.NumViewports, &SavedState.aViewport[0]);

    /*
     * Setup pipeline for the blitter.
     */

    /* Render target is first.
     * If the source texture is bound as a render target, then this call will unbind it
     * and allow to use it as the shader resource.
     */
    pBlitter->pImmediateContext->OMSetRenderTargets(1, &pDstRenderTargetView, NULL);

    /* Input assembler. */
    pBlitter->pImmediateContext->IASetInputLayout(NULL);
    pBlitter->pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    /* Constant buffer. */
    struct
    {
        float scaleX;
        float scaleY;
        float offsetX;
        float offsetY;
    } VSConstantBuffer;
    VSConstantBuffer.scaleX = (float)(rectDst.right - rectDst.left) / cDstWidth;
    VSConstantBuffer.scaleY = (float)(rectDst.bottom - rectDst.top) / cDstHeight;
    VSConstantBuffer.offsetX = (float)(rectDst.right + rectDst.left) / cDstWidth - 1.0f;
    VSConstantBuffer.offsetY = -((float)(rectDst.bottom + rectDst.top) / cDstHeight - 1.0f);

    D3D11_SUBRESOURCE_DATA initialData;
    initialData.pSysMem          = &VSConstantBuffer;
    initialData.SysMemPitch      = sizeof(VSConstantBuffer);
    initialData.SysMemSlicePitch = sizeof(VSConstantBuffer);

    D3D11_BUFFER_DESC bd;
    RT_ZERO(bd);
    bd.ByteWidth           = sizeof(VSConstantBuffer);
    bd.Usage               = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;

    ID3D11Buffer *pConstantBuffer;
    HTEST(pBlitter->pDevice->CreateBuffer(&bd, &initialData, &pConstantBuffer));
    pBlitter->pImmediateContext->VSSetConstantBuffers(0, 1, &pConstantBuffer);
    D3D_RELEASE(pConstantBuffer); /* xSSetConstantBuffers "will hold a reference to the interfaces passed in." */

    /* Vertex shader. */
    pBlitter->pImmediateContext->VSSetShader(pBlitter->pVertexShader, NULL, 0);

    /* Shader resource view. */
    pBlitter->pImmediateContext->PSSetShaderResources(0, 1, &pSrcShaderResourceView);

    /* Pixel shader. */
    pBlitter->pImmediateContext->PSSetShader(pBlitter->pPixelShader, NULL, 0);

    /* Sampler. */
    pBlitter->pImmediateContext->PSSetSamplers(0, 1, &pBlitter->pSamplerState);

    /* Rasterizer. */
    pBlitter->pImmediateContext->RSSetState(pBlitter->pRasterizerState);

    /* Blend state. */
    static FLOAT const BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    pBlitter->pImmediateContext->OMSetBlendState(pBlitter->pBlendState, BlendFactor, 0xffffffff);

    /* Viewport. */
    D3D11_VIEWPORT Viewport;
    Viewport.TopLeftX = 0;
    Viewport.TopLeftY = 0;
    Viewport.Width    = cDstWidth;
    Viewport.Height   = cDstHeight;
    Viewport.MinDepth = 0.0f;
    Viewport.MaxDepth = 1.0f;
    pBlitter->pImmediateContext->RSSetViewports(1, &Viewport);

    /* Draw. */
    pBlitter->pImmediateContext->Draw(4, 0);

    /*
     * Restore pipeline state.
     */
    pBlitter->pImmediateContext->IASetPrimitiveTopology(SavedState.Topology);
    pBlitter->pImmediateContext->IASetInputLayout(SavedState.pInputLayout);
    D3D_RELEASE(SavedState.pInputLayout);
    pBlitter->pImmediateContext->VSSetConstantBuffers(0, 1, &SavedState.pConstantBuffer);
    D3D_RELEASE(SavedState.pConstantBuffer);
    pBlitter->pImmediateContext->VSSetShader(SavedState.pVertexShader, NULL, 0);
    D3D_RELEASE(SavedState.pVertexShader);
    pBlitter->pImmediateContext->PSSetShaderResources(0, 1, &SavedState.pShaderResourceView);
    D3D_RELEASE(SavedState.pShaderResourceView);
    pBlitter->pImmediateContext->PSSetShader(SavedState.pPixelShader, NULL, 0);
    D3D_RELEASE(SavedState.pPixelShader);
    pBlitter->pImmediateContext->PSSetSamplers(0, 1, &SavedState.pSamplerState);
    D3D_RELEASE(SavedState.pSamplerState);
    pBlitter->pImmediateContext->RSSetState(SavedState.pRasterizerState);
    D3D_RELEASE(SavedState.pRasterizerState);
    pBlitter->pImmediateContext->OMSetBlendState(SavedState.pBlendState, SavedState.BlendFactor, SavedState.SampleMask);
    D3D_RELEASE(SavedState.pBlendState);
    pBlitter->pImmediateContext->OMSetRenderTargets(RT_ELEMENTS(SavedState.apRenderTargetView), SavedState.apRenderTargetView, SavedState.pDepthStencilView);
    D3D_RELEASE_ARRAY(RT_ELEMENTS(SavedState.apRenderTargetView), SavedState.apRenderTargetView);
    D3D_RELEASE(SavedState.pDepthStencilView);
    pBlitter->pImmediateContext->RSSetViewports(SavedState.NumViewports, &SavedState.aViewport[0]);

    return S_OK;
}


int D3D11Test::Run()
{
    HRESULT hr = S_OK;

    bool fFirst = true;
    MSG msg;

    LARGE_INTEGER PerfFreq;
    QueryPerformanceFrequency(&PerfFreq);
    float const PerfPeriod = 1.0f / (float)PerfFreq.QuadPart; /* Period in seconds. */

    LARGE_INTEGER PrevTS;
    QueryPerformanceCounter(&PrevTS);

    int cFrames = 0;
    float elapsed = 0;

    bool const fDirectOutput = mpRender->IsDirectOutputRequired(this);

    D3D11BLITTER Blitter;
    if (fDirectOutput)
        RT_ZERO(Blitter);
    else
        BlitInit(&Blitter, mOutput.pDevice, mOutput.pImmediateContext);

    do
    {
        BOOL fGotMessage;
        if (miRenderMode == RenderModeStep)
        {
            fGotMessage = GetMessageA(&msg, 0, 0, 0);
        }
        else
        {
            fGotMessage = PeekMessageA(&msg, 0, 0, 0, PM_REMOVE);
        }

        if (fGotMessage)
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        BOOL fDoRender = FALSE;
        if (miRenderMode == RenderModeStep)
        {
            if (msg.message == WM_CHAR)
            {
                if (msg.wParam == ' ')
                {
                    fDoRender = TRUE;
                }
            }
        }
        else
        {
             fDoRender = TRUE;
        }

        if (fDoRender)
        {
            LARGE_INTEGER CurrTS;
            QueryPerformanceCounter(&CurrTS);

            /* Time in seconds since the previous render step. */
            float dt = fFirst ? 0.0f : (float)(CurrTS.QuadPart - PrevTS.QuadPart) * PerfPeriod;
            if (mpRender)
            {
                /*
                 * Render the scene.
                 */
                mpRender->TimeAdvance(dt);

                if (fDirectOutput)
                {
                    ID3D11Texture2D *pBackBuffer = NULL;
                    HTEST(mOutput.pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer)));
                    if (pBackBuffer)
                    {
                        ID3D11RenderTargetView *pRenderTargetView = 0;
                        HTEST(mRender.pDevice->CreateRenderTargetView(pBackBuffer, NULL, &pRenderTargetView));

                        mRender.pImmediateContext->OMSetRenderTargets(1, &pRenderTargetView, mRender.pDepthStencilView);

                        mRender.pRenderTargetView = pRenderTargetView;
                        mpRender->DoRender(this);
                        mRender.pRenderTargetView = NULL;

                        D3D_RELEASE(pRenderTargetView);
                        D3D_RELEASE(pBackBuffer);
                    }
                    else
                        D3DTestShowError(hr, "pSwapChain->GetBuffer (DirectOutput)");
                }
                else
                {
                    DWORD result = mRender.pDXGIKeyedMutex->AcquireSync(0, 1000);
                    if (result == WAIT_OBJECT_0)
                    {
                        /*
                         * Use the shared texture from the render device.
                         */
                        mRender.pImmediateContext->OMSetRenderTargets(1, &mRender.pRenderTargetView, mRender.pDepthStencilView);
                        mpRender->DoRender(this);
                    }
                    else
                        D3DTestShowError(hr, "Render.AcquireSync(0)");
                    result = mRender.pDXGIKeyedMutex->ReleaseSync(1);
                    if (result == WAIT_OBJECT_0)
                    { }
                    else
                        D3DTestShowError(hr, "Render.ReleaseSync(1)");

                    /*
                     * Copy the rendered scene to the backbuffer and present.
                     */
                    ID3D11Texture2D *pBackBuffer = NULL;
                    HTEST(mOutput.pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer)));
                    if (pBackBuffer)
                    {
                        result = mOutput.pDXGIKeyedMutex->AcquireSync(1, 1000);
                        if (result == WAIT_OBJECT_0)
                        {
                            /*
                             * Use the shared texture from the output device.
                             */
                            float cDstWidth = static_cast<float>(mRTWidth);
                            float cDstHeight = static_cast<float>(mRTHeight);

                            D3D11_RECT rectDst;
                            rectDst.left   = 0;
                            rectDst.top    = 0;
                            rectDst.right  = mRTWidth;
                            rectDst.bottom = mRTHeight;

                            ID3D11ShaderResourceView *pShaderResourceView = 0;
                            HTEST(Blitter.pDevice->CreateShaderResourceView(mOutput.pSharedTexture, NULL, &pShaderResourceView));

                            ID3D11RenderTargetView *pRenderTargetView = 0;
                            HTEST(Blitter.pDevice->CreateRenderTargetView(pBackBuffer, NULL, &pRenderTargetView));

                            BlitFromTexture(&Blitter, pRenderTargetView, cDstWidth, cDstHeight, rectDst, pShaderResourceView);

                            D3D_RELEASE(pRenderTargetView);
                            D3D_RELEASE(pShaderResourceView);
                        }
                        else
                            D3DTestShowError(hr, "Output.AcquireSync(1)");
                        result = mOutput.pDXGIKeyedMutex->ReleaseSync(0);
                        if (result == WAIT_OBJECT_0)
                        { }
                        else
                            D3DTestShowError(hr, "Output.ReleaseSync(0)");

                        D3D_RELEASE(pBackBuffer);
                    }
                }

                HTEST(mOutput.pSwapChain->Present(0, 0));

                fFirst = false;
            }

            if (miRenderMode == RenderModeFPS)
            {
                ++cFrames;
                elapsed += dt;
                if (elapsed > 1.0f)
                {
                    float msPerFrame = elapsed * 1000.0f / (float)cFrames;
                    char sz[256];
                    RTStrPrintf(sz, sizeof(sz), "D3D11 Test FPS %d Frame Time %u.%03ums",
                                cFrames, (unsigned)msPerFrame, (unsigned)(msPerFrame * 1000) % 1000);
                    SetWindowTextA(mHwnd, sz);

                    cFrames = 0;
                    elapsed = 0.0f;
                }
            }

            PrevTS = CurrTS;
        }
    } while (msg.message != WM_QUIT);

    BlitRelease(&Blitter);
    return msg.wParam;
}

ID3D11Device *D3D11Test::Device()
{
    return mRender.pDevice;
}

ID3D11DeviceContext *D3D11Test::ImmediateContext()
{
    return mRender.pImmediateContext;
}

ID3D11RenderTargetView *D3D11Test::RenderTargetView()
{
    return mRender.pRenderTargetView;
}

ID3D11DepthStencilView *D3D11Test::DepthStencilView()
{
    return mRender.pDepthStencilView;
}

int main(int argc, char **argv)
{
    int      rcExit = RTEXITCODE_FAILURE;

    D3D11Test test;
    HRESULT hr = test.Init(GetModuleHandleW(NULL), argc, argv, SW_SHOWDEFAULT);
    if (SUCCEEDED(hr))
        rcExit = test.Run();

    return rcExit;
}
