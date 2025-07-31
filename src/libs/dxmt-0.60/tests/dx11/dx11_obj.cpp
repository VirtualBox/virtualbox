#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>

#include <assert.h>
#include <stdint.h>

#include "3DMaths.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ObjLoading.h"

#pragma warning(push)
#pragma warning(disable : 4996) // disable warning that fopen() is unsafe

#include <assert.h>
#include <math.h> //pow(), fabs(), sqrtf()
#include <stdio.h>
#include <stdlib.h>

// Based on the .obj loading code by Arseny Kapoulkine
// in the meshoptimizer project

static int parseInt(const char *s, const char **end) {
  // skip whitespace
  while (*s == ' ' || *s == '\t')
    ++s;

  // read sign bit
  int sign = (*s == '-');
  if (*s == '-' || *s == '+')
    ++s;

  unsigned int result = 0;
  while ((unsigned(*s - '0') < 10)) {
    result = result * 10 + (*s - '0');
    ++s;
  }

  // return end-of-string
  *end = s;

  return sign ? -int(result) : int(result);
}

static float parseFloat(const char *s, const char **end) {
  static const double powers[] = {1e0,   1e+1,  1e+2,  1e+3,  1e+4,  1e+5,
                                  1e+6,  1e+7,  1e+8,  1e+9,  1e+10, 1e+11,
                                  1e+12, 1e+13, 1e+14, 1e+15, 1e+16, 1e+17,
                                  1e+18, 1e+19, 1e+20, 1e+21, 1e+22};

  // skip whitespace
  while (*s == ' ' || *s == '\t')
    ++s;

  // read sign
  double sign = (*s == '-') ? -1 : 1;
  if (*s == '-' || *s == '+')
    ++s;

  // read integer part
  double result = 0;
  int power = 0;

  while (unsigned(*s - '0') < 10) {
    result = result * 10 + (double)(*s - '0');
    ++s;
  }

  // read fractional part
  if (*s == '.') {
    ++s;

    while (unsigned(*s - '0') < 10) {
      result = result * 10 + (double)(*s - '0');
      ++s;
      --power;
    }
  }

  // read exponent part
  // NOTE: bitwise OR with ' ' will transform an uppercase char
  // to lowercase while leaving lowercase chars unchanged
  if ((*s | ' ') == 'e') {
    ++s;

    // read exponent sign
    int expSign = (*s == '-') ? -1 : 1;
    if (*s == '-' || *s == '+')
      ++s;

    // read exponent
    int expPower = 0;
    while (unsigned(*s - '0') < 10) {
      expPower = expPower * 10 + (*s - '0');
      ++s;
    }

    power += expSign * expPower;
  }

  // return end-of-string
  *end = s;

  // note: this is precise if result < 9e15
  // for longer inputs we lose a bit of precision here
  if (unsigned(-power) < sizeof(powers) / sizeof(powers[0]))
    return float(sign * result / powers[-power]);
  else if (unsigned(power) < sizeof(powers) / sizeof(powers[0]))
    return float(sign * result * powers[power]);
  else
    return float(sign * result * pow(10.0, power));
}

static const char *parseFaceElement(const char *s, int &vi, int &vti,
                                    int &vni) {
  while (*s == ' ' || *s == '\t')
    ++s;

  vi = parseInt(s, &s);

  if (*s != '/')
    return s;
  ++s;

  // handle vi//vni indices
  if (*s != '/')
    vti = parseInt(s, &s);

  if (*s != '/')
    return s;
  ++s;

  vni = parseInt(s, &s);

  return s;
}

static int fixupIndex(int index, size_t size) {
  return (index >= 0) ? index - 1 : int(size) + index;
}

static bool areAlmostEqual(float a, float b) {
  return (fabs(a - b) < 0.00001f);
}

static void growArray(void **array, size_t *capacity, size_t itemSize) {
  *capacity = (*capacity == 0) ? 32 : (*capacity + *capacity / 2);
  *array = realloc(*array, *capacity * itemSize);
  assert(*array);
}

LoadedObj loadObj(const char *filename) {
  LoadedObj result = {};

  // Read entire file into string
  char *fileBytes;
  {
    FILE *file = fopen(filename, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    size_t fileNumBytes = ftell(file);
    fseek(file, 0, SEEK_SET);

    fileBytes = (char *)malloc(fileNumBytes + 1);
    assert(fileBytes);
    fread(fileBytes, 1, fileNumBytes, file);
    fileBytes[fileNumBytes] = '\0';
    fclose(file);
  }

  // Count number of elements in obj file
  uint32_t numVertexPositions = 0;
  uint32_t numVertexTexCoords = 0;
  uint32_t numVertexNormals = 0;
  uint32_t numFaces = 0;
  {
    const char *s = fileBytes;
    while (*s) {
      if (*s == 'v') {
        ++s;
        if (*s == ' ')
          ++numVertexPositions;
        else if (*s == 't')
          ++numVertexTexCoords;
        else if (*s == 'n')
          ++numVertexNormals;
      } else if (*s == 'f')
        ++numFaces;

      while (*s != 0 && *s++ != '\n')
        ;
    }
  }

  float *vpBuffer = (float *)malloc(numVertexPositions * 3 * sizeof(float));
  float *vtBuffer = (float *)malloc(numVertexTexCoords * 2 * sizeof(float));
  float *vnBuffer = (float *)malloc(numVertexNormals * 3 * sizeof(float));
  float *vpIt = vpBuffer;
  float *vtIt = vtBuffer;
  float *vnIt = vnBuffer;

  size_t vertexBufferCapacity = 0;
  size_t indexBufferCapacity = 0;
  size_t vertexBufferSize = 0;
  size_t indexBufferSize = 0;
  VertexData *outVertexBuffer = NULL;
  uint16_t *outIndexBuffer = NULL;

  bool smoothNormals = false;

  const char *s = fileBytes;
  while (*s) {
    char currChar = *s;
    if (currChar == 'v') {
      ++s;
      currChar = *s++;
      if (currChar == ' ') {
        *vpIt++ = parseFloat(s, &s);
        *vpIt++ = parseFloat(s, &s);
        *vpIt++ = parseFloat(s, &s);
      } else if (currChar == 't') {
        *vtIt++ = parseFloat(s, &s);
        *vtIt++ = parseFloat(s, &s);
      } else if (currChar == 'n') {
        *vnIt++ = parseFloat(s, &s);
        *vnIt++ = parseFloat(s, &s);
        *vnIt++ = parseFloat(s, &s);
      }
    } else if (currChar == 'f') {
      ++s;
      while (*s != '\r' && *s != '\n' && *s != '\0') {
        int vpIdx = 0, vtIdx = 0, vnIdx = 0;
        s = parseFaceElement(s, vpIdx, vtIdx, vnIdx);
        if (!vpIdx)
          assert(vpIdx != 0);

        vpIdx = fixupIndex(vpIdx, numVertexPositions);
        vtIdx = fixupIndex(vtIdx, numVertexTexCoords);
        vnIdx = fixupIndex(vnIdx, numVertexNormals);

        VertexData newVert = {
            vpBuffer[3 * vpIdx],     vpBuffer[3 * vpIdx + 1],
            vpBuffer[3 * vpIdx + 2], vtBuffer[2 * vtIdx],
            vtBuffer[2 * vtIdx + 1], vnBuffer[3 * vnIdx],
            vnBuffer[3 * vnIdx + 1], vnBuffer[3 * vnIdx + 2],
        };

        // Search vertexBuffer for matching vertex
        uint32_t index;
        for (index = 0; index < vertexBufferSize; ++index) {
          VertexData *v = outVertexBuffer + index;
          bool posMatch = areAlmostEqual(v->pos[0], newVert.pos[0]) &&
                          areAlmostEqual(v->pos[1], newVert.pos[1]) &&
                          areAlmostEqual(v->pos[2], newVert.pos[2]);
          bool uvMatch = areAlmostEqual(v->uv[0], newVert.uv[0]) &&
                         areAlmostEqual(v->uv[1], newVert.uv[1]);
          bool normMatch = areAlmostEqual(v->norm[0], newVert.norm[0]) &&
                           areAlmostEqual(v->norm[1], newVert.norm[1]) &&
                           areAlmostEqual(v->norm[2], newVert.norm[2]);
          if (posMatch && uvMatch) {
            if (normMatch || smoothNormals) {
              v->norm[0] += newVert.norm[0];
              v->norm[1] += newVert.norm[1];
              v->norm[2] += newVert.norm[2];
              break;
            }
          }
        }
        if (index == vertexBufferSize) {
          if (vertexBufferSize + 1 > vertexBufferCapacity) {
            growArray((void **)(&outVertexBuffer), &vertexBufferCapacity,
                      sizeof(VertexData));
          }
          outVertexBuffer[vertexBufferSize++] = newVert;
        }
        if (indexBufferSize + 1 > indexBufferCapacity) {
          growArray((void **)(&outIndexBuffer), &indexBufferCapacity,
                    sizeof(uint16_t));
        }
        outIndexBuffer[indexBufferSize++] = (uint16_t)index;
      }
    } else if (currChar == 's' && *(++s) == ' ') {
      ++s;
      if ((*s == 'o' && *(s + 1) == 'f' && *(s + 2) == 'f') || *s == '0')
        smoothNormals = false;
      else {
        assert((*s == 'o' && *(s + 1) == 'n') || (*s >= '1' && *s <= '9'));
        smoothNormals = true;
      }
    }

    while (*s != 0 && *s++ != '\n')
      ;
  }

  // Normalise the normals
  for (uint32_t i = 0; i < vertexBufferSize; ++i) {
    VertexData *v = outVertexBuffer + i;
    float normLength = sqrtf(v->norm[0] * v->norm[0] + v->norm[1] * v->norm[1] +
                             v->norm[2] * v->norm[2]);
    float invNormLength = 1.f / normLength;
    v->norm[0] *= invNormLength;
    v->norm[1] *= invNormLength;
    v->norm[2] *= invNormLength;
  }

  free(vpBuffer);
  free(vtBuffer);
  free(vnBuffer);
  free(fileBytes);

  result.numVertices = vertexBufferSize;
  result.numIndices = indexBufferSize;
  result.vertexBuffer = outVertexBuffer;
  result.indexBuffer = outIndexBuffer;

  return result;
}

void freeLoadedObj(LoadedObj loadedObj) {
  free(loadedObj.vertexBuffer);
  free(loadedObj.indexBuffer);
}

#pragma warning(pop)

static bool global_windowDidResize = false;

// Input
enum GameAction {
  GameActionMoveCamFwd,
  GameActionMoveCamBack,
  GameActionMoveCamLeft,
  GameActionMoveCamRight,
  GameActionTurnCamLeft,
  GameActionTurnCamRight,
  GameActionLookUp,
  GameActionLookDown,
  GameActionRaiseCam,
  GameActionLowerCam,
  GameActionCount
};
static bool global_keyIsDown[GameActionCount] = {};

bool win32CreateD3D11RenderTargets(
    ID3D11Device1 *d3d11Device, IDXGISwapChain1 *swapChain,
    ID3D11RenderTargetView **d3d11FrameBufferView,
    ID3D11DepthStencilView **depthBufferView) {
  ID3D11Texture2D *d3d11FrameBuffer;
  HRESULT hResult = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                         (void **)&d3d11FrameBuffer);
  assert(SUCCEEDED(hResult));

  hResult = d3d11Device->CreateRenderTargetView(d3d11FrameBuffer, 0,
                                                d3d11FrameBufferView);
  assert(SUCCEEDED(hResult));

  D3D11_TEXTURE2D_DESC depthBufferDesc;
  d3d11FrameBuffer->GetDesc(&depthBufferDesc);

  d3d11FrameBuffer->Release();

  depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

  ID3D11Texture2D *depthBuffer;
  d3d11Device->CreateTexture2D(&depthBufferDesc, nullptr, &depthBuffer);

  d3d11Device->CreateDepthStencilView(depthBuffer, nullptr, depthBufferView);

  depthBuffer->Release();

  return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  LRESULT result = 0;
  switch (msg) {
  case WM_KEYDOWN:
  case WM_KEYUP: {
    bool isDown = (msg == WM_KEYDOWN);
    if (wparam == VK_ESCAPE)
      DestroyWindow(hwnd);
    else if (wparam == 'W')
      global_keyIsDown[GameActionMoveCamFwd] = isDown;
    else if (wparam == 'A')
      global_keyIsDown[GameActionMoveCamLeft] = isDown;
    else if (wparam == 'S')
      global_keyIsDown[GameActionMoveCamBack] = isDown;
    else if (wparam == 'D')
      global_keyIsDown[GameActionMoveCamRight] = isDown;
    else if (wparam == 'E')
      global_keyIsDown[GameActionRaiseCam] = isDown;
    else if (wparam == 'Q')
      global_keyIsDown[GameActionLowerCam] = isDown;
    else if (wparam == VK_UP)
      global_keyIsDown[GameActionLookUp] = isDown;
    else if (wparam == VK_LEFT)
      global_keyIsDown[GameActionTurnCamLeft] = isDown;
    else if (wparam == VK_DOWN)
      global_keyIsDown[GameActionLookDown] = isDown;
    else if (wparam == VK_RIGHT)
      global_keyIsDown[GameActionTurnCamRight] = isDown;
    break;
  }
  case WM_DESTROY: {
    PostQuitMessage(0);
    break;
  }
  case WM_SIZE: {
    global_windowDidResize = true;
    break;
  }
  default:
    result = DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  return result;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int /*nShowCmd*/) {
  // Open a window
  HWND hwnd;
  {
    WNDCLASSEXW winClass = {};
    winClass.cbSize = sizeof(WNDCLASSEXW);
    winClass.style = CS_HREDRAW | CS_VREDRAW;
    winClass.lpfnWndProc = &WndProc;
    winClass.hInstance = hInstance;
    winClass.hIcon = LoadIconW(0, IDI_APPLICATION);
    winClass.hCursor = LoadCursorW(0, IDC_ARROW);
    winClass.lpszClassName = L"MyWindowClass";
    winClass.hIconSm = LoadIconW(0, IDI_APPLICATION);

    if (!RegisterClassExW(&winClass)) {
      MessageBoxA(0, "RegisterClassEx failed", "Fatal Error", MB_OK);
      return GetLastError();
    }

    RECT initialRect = {0, 0, 1024, 768};
    AdjustWindowRectEx(&initialRect, WS_OVERLAPPEDWINDOW, FALSE,
                       WS_EX_OVERLAPPEDWINDOW);
    LONG initialWidth = initialRect.right - initialRect.left;
    LONG initialHeight = initialRect.bottom - initialRect.top;

    hwnd = CreateWindowExW(WS_EX_OVERLAPPEDWINDOW, winClass.lpszClassName,
                           L"09. Loading a Wavefront .obj Mesh",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                           CW_USEDEFAULT, initialWidth, initialHeight, 0, 0,
                           hInstance, 0);

    if (!hwnd) {
      MessageBoxA(0, "CreateWindowEx failed", "Fatal Error", MB_OK);
      return GetLastError();
    }
  }

  // Create D3D11 Device and Context
  ID3D11Device1 *d3d11Device;
  ID3D11DeviceContext1 *d3d11DeviceContext;
  {
    ID3D11Device *baseDevice;
    ID3D11DeviceContext *baseDeviceContext;
    D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_0};
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(DEBUG_BUILD)
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hResult = D3D11CreateDevice(
        0, D3D_DRIVER_TYPE_HARDWARE, 0, creationFlags, featureLevels,
        ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &baseDevice, 0,
        &baseDeviceContext);
    if (FAILED(hResult)) {
      MessageBoxA(0, "D3D11CreateDevice() failed", "Fatal Error", MB_OK);
      return GetLastError();
    }

    // Get 1.1 interface of D3D11 Device and Context
    hResult = baseDevice->QueryInterface(__uuidof(ID3D11Device1),
                                         (void **)&d3d11Device);
    assert(SUCCEEDED(hResult));
    baseDevice->Release();

    hResult = baseDeviceContext->QueryInterface(__uuidof(ID3D11DeviceContext1),
                                                (void **)&d3d11DeviceContext);
    assert(SUCCEEDED(hResult));
    baseDeviceContext->Release();
  }

#ifdef DEBUG_BUILD
  // Set up debug layer to break on D3D11 errors
  ID3D11Debug *d3dDebug = nullptr;
  d3d11Device->QueryInterface(__uuidof(ID3D11Debug), (void **)&d3dDebug);
  if (d3dDebug) {
    ID3D11InfoQueue *d3dInfoQueue = nullptr;
    if (SUCCEEDED(d3dDebug->QueryInterface(__uuidof(ID3D11InfoQueue),
                                           (void **)&d3dInfoQueue))) {
      d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
      d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
      d3dInfoQueue->Release();
    }
    d3dDebug->Release();
  }
#endif

  // Create Swap Chain
  IDXGISwapChain1 *d3d11SwapChain;
  {
    // Get DXGI Factory (needed to create Swap Chain)
    IDXGIFactory2 *dxgiFactory;
    {
      IDXGIDevice1 *dxgiDevice;
      HRESULT hResult = d3d11Device->QueryInterface(__uuidof(IDXGIDevice1),
                                                    (void **)&dxgiDevice);
      assert(SUCCEEDED(hResult));

      IDXGIAdapter *dxgiAdapter;
      hResult = dxgiDevice->GetAdapter(&dxgiAdapter);
      assert(SUCCEEDED(hResult));
      dxgiDevice->Release();

      DXGI_ADAPTER_DESC adapterDesc;
      dxgiAdapter->GetDesc(&adapterDesc);

      OutputDebugStringA("Graphics Device: ");
      OutputDebugStringW(adapterDesc.Description);

      hResult = dxgiAdapter->GetParent(__uuidof(IDXGIFactory2),
                                       (void **)&dxgiFactory);
      assert(SUCCEEDED(hResult));
      dxgiAdapter->Release();
    }

    DXGI_SWAP_CHAIN_DESC1 d3d11SwapChainDesc = {};
    d3d11SwapChainDesc.Width = 0;  // use window width
    d3d11SwapChainDesc.Height = 0; // use window height
    d3d11SwapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    d3d11SwapChainDesc.SampleDesc.Count = 1;
    d3d11SwapChainDesc.SampleDesc.Quality = 0;
    d3d11SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d3d11SwapChainDesc.BufferCount = 2;
    d3d11SwapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    d3d11SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    d3d11SwapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    d3d11SwapChainDesc.Flags = 0;

    HRESULT hResult = dxgiFactory->CreateSwapChainForHwnd(
        d3d11Device, hwnd, &d3d11SwapChainDesc, 0, 0, &d3d11SwapChain);
    assert(SUCCEEDED(hResult));

    dxgiFactory->Release();
  }

  // Create Render Target and Depth Buffer
  ID3D11RenderTargetView *d3d11FrameBufferView;
  ID3D11DepthStencilView *depthBufferView;
  win32CreateD3D11RenderTargets(d3d11Device, d3d11SwapChain,
                                &d3d11FrameBufferView, &depthBufferView);

  UINT shaderCompileFlags = 0;
// Compiling with this flag allows debugging shaders with Visual Studio
#if defined(DEBUG_BUILD)
  shaderCompileFlags |= D3DCOMPILE_DEBUG;
#endif

  // Create Vertex Shader
  ID3DBlob *vsBlob;
  ID3D11VertexShader *vertexShader;
  {
    ID3DBlob *shaderCompileErrorsBlob;
    HRESULT hResult = D3DCompileFromFile(
        L"shader_obj.hlsl", nullptr, nullptr, "vs_main", "vs_5_0",
        shaderCompileFlags, 0, &vsBlob, &shaderCompileErrorsBlob);
    if (FAILED(hResult)) {
      const char *errorString = NULL;
      if (hResult == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        errorString = "Could not compile shader; file not found";
      else if (shaderCompileErrorsBlob) {
        errorString = (const char *)shaderCompileErrorsBlob->GetBufferPointer();
        shaderCompileErrorsBlob->Release();
      }
      MessageBoxA(0, errorString, "Shader Compiler Error",
                  MB_ICONERROR | MB_OK);
      return 1;
    }

    hResult = d3d11Device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                              vsBlob->GetBufferSize(), nullptr,
                                              &vertexShader);
    assert(SUCCEEDED(hResult));
  }

  // Create Pixel Shader
  ID3D11PixelShader *pixelShader;
  {
    ID3DBlob *psBlob;
    ID3DBlob *shaderCompileErrorsBlob;
    HRESULT hResult = D3DCompileFromFile(
        L"shader_obj.hlsl", nullptr, nullptr, "ps_main", "ps_5_0",
        shaderCompileFlags, 0, &psBlob, &shaderCompileErrorsBlob);
    if (FAILED(hResult)) {
      const char *errorString = NULL;
      if (hResult == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        errorString = "Could not compile shader; file not found";
      else if (shaderCompileErrorsBlob) {
        errorString = (const char *)shaderCompileErrorsBlob->GetBufferPointer();
        shaderCompileErrorsBlob->Release();
      }
      MessageBoxA(0, errorString, "Shader Compiler Error",
                  MB_ICONERROR | MB_OK);
      return 1;
    }

    hResult = d3d11Device->CreatePixelShader(psBlob->GetBufferPointer(),
                                             psBlob->GetBufferSize(), nullptr,
                                             &pixelShader);
    assert(SUCCEEDED(hResult));
    psBlob->Release();
  }

  // Create Input Layout
  ID3D11InputLayout *inputLayout;
  {
    D3D11_INPUT_ELEMENT_DESC inputElementDesc[] = {
        {"POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEX", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT,
         D3D11_INPUT_PER_VERTEX_DATA, 0}};

    HRESULT hResult = d3d11Device->CreateInputLayout(
        inputElementDesc, ARRAYSIZE(inputElementDesc),
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
    assert(SUCCEEDED(hResult));
    vsBlob->Release();
  }

  // Create Vertex and Index Buffer
  ID3D11Buffer *vertexBuffer;
  ID3D11Buffer *indexBuffer;
  // UINT numVerts;
  UINT numIndices;
  UINT stride;
  UINT offset;
  {
    LoadedObj obj = loadObj("cube.obj");
    stride = sizeof(VertexData);
    // numVerts = obj.numVertices;
    offset = 0;
    numIndices = obj.numIndices;

    D3D11_BUFFER_DESC vertexBufferDesc = {};
    vertexBufferDesc.ByteWidth = obj.numVertices * sizeof(VertexData);
    vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vertexSubresourceData = {obj.vertexBuffer};

    HRESULT hResult = d3d11Device->CreateBuffer(
        &vertexBufferDesc, &vertexSubresourceData, &vertexBuffer);
    assert(SUCCEEDED(hResult));

    D3D11_BUFFER_DESC indexBufferDesc = {};
    indexBufferDesc.ByteWidth = obj.numIndices * sizeof(uint16_t);
    indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA indexSubresourceData = {obj.indexBuffer};

    hResult = d3d11Device->CreateBuffer(&indexBufferDesc, &indexSubresourceData,
                                        &indexBuffer);
    assert(SUCCEEDED(hResult));
    freeLoadedObj(obj);
  }

  // Create Sampler State
  ID3D11SamplerState *samplerState;
  {
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDesc.BorderColor[0] = 1.0f;
    samplerDesc.BorderColor[1] = 1.0f;
    samplerDesc.BorderColor[2] = 1.0f;
    samplerDesc.BorderColor[3] = 1.0f;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    d3d11Device->CreateSamplerState(&samplerDesc, &samplerState);
  }

  // Load Image
  int texWidth, texHeight, texNumChannels;
  int texForceNumChannels = 4;
  unsigned char *testTextureBytes = stbi_load(
      "test.png", &texWidth, &texHeight, &texNumChannels, texForceNumChannels);
  assert(testTextureBytes);
  int texBytesPerRow = 4 * texWidth;

  // Create Texture
  ID3D11ShaderResourceView *textureView;
  {
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = texWidth;
    textureDesc.Height = texHeight;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA textureSubresourceData = {};
    textureSubresourceData.pSysMem = testTextureBytes;
    textureSubresourceData.SysMemPitch = texBytesPerRow;

    ID3D11Texture2D *texture;
    d3d11Device->CreateTexture2D(&textureDesc, &textureSubresourceData,
                                 &texture);

    d3d11Device->CreateShaderResourceView(texture, nullptr, &textureView);
    texture->Release();
  }

  free(testTextureBytes);

  // Create Constant Buffer
  struct Constants {
    float4x4 modelViewProj;
  };

  ID3D11Buffer *constantBuffer;
  {
    D3D11_BUFFER_DESC constantBufferDesc = {};
    // ByteWidth must be a multiple of 16, per the docs
    constantBufferDesc.ByteWidth = sizeof(Constants) + 0xf & 0xfffffff0;
    constantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hResult = d3d11Device->CreateBuffer(&constantBufferDesc, nullptr,
                                                &constantBuffer);
    assert(SUCCEEDED(hResult));
  }

  ID3D11RasterizerState *rasterizerState;
  {
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_BACK;
    rasterizerDesc.FrontCounterClockwise = TRUE;

    d3d11Device->CreateRasterizerState(&rasterizerDesc, &rasterizerState);
  }

  ID3D11DepthStencilState *depthStencilState;
  {
    D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;

    d3d11Device->CreateDepthStencilState(&depthStencilDesc, &depthStencilState);
  }

  // Camera
  float3 cameraPos = {0, 0, 2};
  float3 cameraFwd = {0, 0, -1};
  float cameraPitch = 0.f;
  float cameraYaw = 0.f;

  float4x4 perspectiveMat = {};
  global_windowDidResize = true; // To force initial perspectiveMat calculation

  // Timing
  LONGLONG startPerfCount = 0;
  LONGLONG perfCounterFrequency = 0;
  {
    LARGE_INTEGER perfCount;
    QueryPerformanceCounter(&perfCount);
    startPerfCount = perfCount.QuadPart;
    LARGE_INTEGER perfFreq;
    QueryPerformanceFrequency(&perfFreq);
    perfCounterFrequency = perfFreq.QuadPart;
  }
  double currentTimeInSeconds = 0.0;

  // Main Loop
  bool isRunning = true;
  while (isRunning) {
    float dt;
    {
      double previousTimeInSeconds = currentTimeInSeconds;
      LARGE_INTEGER perfCount;
      QueryPerformanceCounter(&perfCount);

      currentTimeInSeconds = (double)(perfCount.QuadPart - startPerfCount) /
                             (double)perfCounterFrequency;
      dt = (float)(currentTimeInSeconds - previousTimeInSeconds);
      if (dt > (1.f / 60.f))
        dt = (1.f / 60.f);
    }

    MSG msg = {};
    while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT)
        isRunning = false;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    // Get window dimensions
    int windowWidth, windowHeight;
    float windowAspectRatio;
    {
      RECT clientRect;
      GetClientRect(hwnd, &clientRect);
      windowWidth = clientRect.right - clientRect.left;
      windowHeight = clientRect.bottom - clientRect.top;
      windowAspectRatio = (float)windowWidth / (float)windowHeight;
    }

    if (global_windowDidResize) {
      d3d11DeviceContext->OMSetRenderTargets(0, 0, 0);
      d3d11FrameBufferView->Release();
      depthBufferView->Release();

      HRESULT res =
          d3d11SwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
      assert(SUCCEEDED(res));

      win32CreateD3D11RenderTargets(d3d11Device, d3d11SwapChain,
                                    &d3d11FrameBufferView, &depthBufferView);
      perspectiveMat = makePerspectiveMat(windowAspectRatio,
                                          degreesToRadians(84), 0.1f, 1000.f);

      global_windowDidResize = false;
    }

    // Update camera
    {
      float3 camFwdXZ = normalise((float3){cameraFwd.x, 0, cameraFwd.z});
      float3 cameraRightXZ = cross(camFwdXZ, {0, 1, 0});

      const float CAM_MOVE_SPEED = 5.f; // in metres per second
      const float CAM_MOVE_AMOUNT = CAM_MOVE_SPEED * dt;
      if (global_keyIsDown[GameActionMoveCamFwd])
        cameraPos += camFwdXZ * CAM_MOVE_AMOUNT;
      if (global_keyIsDown[GameActionMoveCamBack])
        cameraPos -= camFwdXZ * CAM_MOVE_AMOUNT;
      if (global_keyIsDown[GameActionMoveCamLeft])
        cameraPos -= cameraRightXZ * CAM_MOVE_AMOUNT;
      if (global_keyIsDown[GameActionMoveCamRight])
        cameraPos += cameraRightXZ * CAM_MOVE_AMOUNT;
      if (global_keyIsDown[GameActionRaiseCam])
        cameraPos.y += CAM_MOVE_AMOUNT;
      if (global_keyIsDown[GameActionLowerCam])
        cameraPos.y -= CAM_MOVE_AMOUNT;

      const float CAM_TURN_SPEED = M_PI; // in radians per second
      const float CAM_TURN_AMOUNT = CAM_TURN_SPEED * dt;
      if (global_keyIsDown[GameActionTurnCamLeft])
        cameraYaw += CAM_TURN_AMOUNT;
      if (global_keyIsDown[GameActionTurnCamRight])
        cameraYaw -= CAM_TURN_AMOUNT;
      if (global_keyIsDown[GameActionLookUp])
        cameraPitch += CAM_TURN_AMOUNT;
      if (global_keyIsDown[GameActionLookDown])
        cameraPitch -= CAM_TURN_AMOUNT;

      // Wrap yaw to avoid floating-point errors if we turn too far
      while (cameraYaw >= 2 * M_PI)
        cameraYaw -= 2 * M_PI;
      while (cameraYaw <= -2 * M_PI)
        cameraYaw += 2 * M_PI;

      // Clamp pitch to stop camera flipping upside down
      if (cameraPitch > degreesToRadians(85))
        cameraPitch = degreesToRadians(85);
      if (cameraPitch < -degreesToRadians(85))
        cameraPitch = -degreesToRadians(85);
    }

    // Calculate view matrix from camera data
    //
    // float4x4 viewMat = inverse(rotateXMat(cameraPitch) *
    // rotateYMat(cameraYaw) * translationMat(cameraPos)); NOTE: We can simplify
    // this calculation to avoid inverse()! Applying the rule inverse(A*B) =
    // inverse(B) * inverse(A) gives: float4x4 viewMat =
    // inverse(translationMat(cameraPos)) * inverse(rotateYMat(cameraYaw)) *
    // inverse(rotateXMat(cameraPitch)); The inverse of a rotation/translation
    // is a negated rotation/translation:
    float4x4 viewMat = translationMat(-cameraPos) * rotateYMat(-cameraYaw) *
                       rotateXMat(-cameraPitch);
    // Update the forward vector we use for camera movement:
    cameraFwd = {-viewMat.m[2][0], -viewMat.m[2][1], -viewMat.m[2][2]};

    // Spin the cube
    float4x4 modelMat =
        rotateXMat(-0.2f * (float)(M_PI * currentTimeInSeconds)) *
        rotateYMat(0.1f * (float)(M_PI * currentTimeInSeconds));

    // Calculate model-view-projection matrix to send to shader
    float4x4 modelViewProj = modelMat * viewMat * perspectiveMat;

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mappedSubresource;
    d3d11DeviceContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
                            &mappedSubresource);
    Constants *constants = (Constants *)(mappedSubresource.pData);
    constants->modelViewProj = modelViewProj;
    d3d11DeviceContext->Unmap(constantBuffer, 0);

    FLOAT backgroundColor[4] = {0.1f, 0.2f, 0.6f, 1.0f};
    d3d11DeviceContext->ClearRenderTargetView(d3d11FrameBufferView,
                                              backgroundColor);

    d3d11DeviceContext->ClearDepthStencilView(depthBufferView,
                                              D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT viewport = {
        0.0f, 0.0f, (FLOAT)windowWidth, (FLOAT)windowHeight, 0.0f, 1.0f};
    d3d11DeviceContext->RSSetViewports(1, &viewport);

    d3d11DeviceContext->RSSetState(rasterizerState);
    d3d11DeviceContext->OMSetDepthStencilState(depthStencilState, 0);

    d3d11DeviceContext->OMSetRenderTargets(1, &d3d11FrameBufferView,
                                           depthBufferView);

    d3d11DeviceContext->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d3d11DeviceContext->IASetInputLayout(inputLayout);

    d3d11DeviceContext->VSSetShader(vertexShader, nullptr, 0);
    d3d11DeviceContext->PSSetShader(pixelShader, nullptr, 0);

    d3d11DeviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);

    d3d11DeviceContext->PSSetShaderResources(0, 1, &textureView);
    d3d11DeviceContext->PSSetSamplers(0, 1, &samplerState);

    d3d11DeviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride,
                                           &offset);
    d3d11DeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R16_UINT, 0);

    d3d11DeviceContext->DrawIndexed(numIndices, 0, 0);

    d3d11SwapChain->Present(1, 0);
  }

  return 0;
}