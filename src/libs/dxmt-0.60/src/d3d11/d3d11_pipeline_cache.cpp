#include "d3d11_pipeline_cache.hpp"
#include "airconv_public.h"
#include "d3d11_device.hpp"
#include "d3d11_shader.hpp"
#include "d3d11_pipeline.hpp"
#include "log/log.hpp"
#include "sha1/sha1_util.hpp"
#include "../d3d10/d3d10_shader.hpp"
#include "../d3d10/d3d10_input_layout.hpp"
#include <cstring>
#include <shared_mutex>

namespace dxmt {

class CachedSM50Shader final : public Shader {
  MTLD3D11Device *device;
  sm50_shader_t shader = nullptr;
  Sha1Hash hash_;
  MTL_SHADER_REFLECTION reflection_;
  MTL_SM50_SHADER_ARGUMENT* arguments_info_buffer;
  std::unordered_map<ShaderVariant, std::unique_ptr<CompiledShader>> variants;

public:
  CachedSM50Shader(MTLD3D11Device *device, sm50_shader_t shader_transfered,
                   const Sha1Hash &hash, MTL_SHADER_REFLECTION &reflection)
      : device(device), shader(shader_transfered), hash_(hash),
        reflection_(reflection) {
    if (reflection_.NumConstantBuffers + reflection_.NumArguments) {
      arguments_info_buffer = (MTL_SM50_SHADER_ARGUMENT *)malloc(
          sizeof(MTL_SM50_SHADER_ARGUMENT) *
          (reflection_.NumConstantBuffers + reflection_.NumArguments));
      SM50GetArgumentsInfo(shader, arguments_info_buffer, arguments_info_buffer + reflection_.NumConstantBuffers);
    } else {
      arguments_info_buffer = nullptr;
    }
  }

  ~CachedSM50Shader() {
    if (shader) {
      SM50Destroy(shader);
      if (arguments_info_buffer)
        free(arguments_info_buffer);
      shader = nullptr;
    }
  };

  CachedSM50Shader(CachedSM50Shader &&moved) = delete;
  CachedSM50Shader(const CachedSM50Shader &copy) = delete;

  virtual sm50_shader_t handle() { return shader; };
  virtual MTL_SHADER_REFLECTION &reflection() { return reflection_; }
  virtual MTL_SM50_SHADER_ARGUMENT *constant_buffers_info() {
    return arguments_info_buffer;
  };
  virtual MTL_SM50_SHADER_ARGUMENT *arguments_info() {
    return arguments_info_buffer + reflection_.NumConstantBuffers;
  };
  virtual Com<CompiledShader> get_shader(ShaderVariant variant) {
    auto c = variants.insert({variant, nullptr});
    if (c.second) {
      c.first->second = std::visit(
          [=, this](auto var) {
            return CreateVariantShader(device, this, var);
          },
          variant);
      device->SubmitThreadgroupWork(c.first->second.get());
    }
    return c.first->second.get();
  }
  virtual const Sha1Hash &hash() { return hash_; };

#ifdef DXMT_DEBUG
  void *bytecode;
  size_t bytecode_length;

  virtual void dump() {
    std::fstream dump_out;
    dump_out.open("shader_dump_" + hash_.toString() + ".cso",
                  std::ios::out | std::ios::binary);
    if (dump_out) {
      dump_out.write((char *)bytecode, bytecode_length);
    }
    dump_out.close();
    WARN("shader dumped to ./shader_dump_" + hash_.toString() + ".cso");
  }
#else
  virtual void dump() {}
#endif
};

class CachedInputLayout final : public InputLayout {
private:
public:
  CachedInputLayout(
      std::vector<MTL_SHADER_INPUT_LAYOUT_ELEMENT_DESC> &&attributes,
      uint32_t input_slot_mask)
      : attributes_(attributes), input_slot_mask_(input_slot_mask) {}

  virtual uint32_t input_slot_mask() final { return input_slot_mask_; }

  virtual uint32_t input_layout_element(
      MTL_SHADER_INPUT_LAYOUT_ELEMENT_DESC **ppElements) final {
    *ppElements = attributes_.data();
    return attributes_.size();
  }

  std::vector<MTL_SHADER_INPUT_LAYOUT_ELEMENT_DESC> attributes_;
  uint32_t input_slot_mask_;
};

class MTLD3D11InputLayout final
    : public MTLD3D11DeviceChild<IMTLD3D11InputLayout> {
public:
  MTLD3D11InputLayout(MTLD3D11Device *device, ManagedInputLayout input_layout)
      : MTLD3D11DeviceChild<IMTLD3D11InputLayout>(device),
        input_layout(input_layout), d3d10(this) {}

  ~MTLD3D11InputLayout() {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11InputLayout) ||
        riid == __uuidof(IMTLD3D11InputLayout)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(ID3D10DeviceChild) ||
        riid == __uuidof(ID3D10InputLayout)) {
      *ppvObject = ref(&d3d10);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D11InputLayout), riid)) {
      WARN("D3D311InputLayout: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  };

  ManagedInputLayout GetManagedInputLayout() override { return input_layout; }

private:
  ManagedInputLayout input_layout;
  MTLD3D10InputLayout d3d10;
};

class PipelineCache : public MTLD3D11PipelineCacheBase {

  MTLD3D11Device *device;
  StateObjectCache<D3D11_BLEND_DESC1, IMTLD3D11BlendState> blend_states;

  std::unordered_map<MTL_INPUT_LAYOUT_DESC, std::unique_ptr<CachedInputLayout>> input_layouts;
  dxmt::mutex mutex_ia_;

  StateObjectCache<MTL_STREAM_OUTPUT_DESC, IMTLD3D11StreamOutputLayout>
      so_layouts;
  dxmt::mutex mutex_so_;

  std::unordered_map<MTL_GRAPHICS_PIPELINE_DESC,
                     Com<IMTLCompiledGraphicsPipeline>>
      pipelines_;
  dxmt::mutex mutex_;

  std::unordered_map<MTL_GRAPHICS_PIPELINE_DESC,
                     Com<IMTLCompiledTessellationPipeline>>
      pipelines_ts_;
  dxmt::mutex mutex_ts_;

  std::unordered_map<MTL_GRAPHICS_PIPELINE_DESC,
                     Com<IMTLCompiledGeometryPipeline>>
      pipelines_gs_;
  dxmt::mutex mutex_gs_;

  std::unordered_map<Sha1Hash, std::unique_ptr<CachedSM50Shader>> shaders_;
  std::shared_mutex mutex_shares;

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(IMTLD3D11PipeineCache)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  CachedSM50Shader *CreateShader(const void *pBytecode,
                                 uint32_t BytecodeLength) {
    auto sha1 = Sha1Hash::compute(pBytecode, BytecodeLength);
    {
      std::shared_lock<std::shared_mutex> lock(mutex_shares);
      auto result = shaders_.find(sha1);
      if (result != shaders_.end()) {
        return shaders_.at(sha1).get();
      }
    }
    sm50_error_t err;
    sm50_shader_t sm50;
    MTL_SHADER_REFLECTION reflection;
    if (SM50Initialize(pBytecode, BytecodeLength, &sm50, &reflection, &err)) {
      ERR("Failed to initialize shader: ", SM50GetErrorMessageString(err));
      SM50FreeError(err);
      return nullptr;
    }
    auto shader = std::make_unique<CachedSM50Shader>(device, sm50, sha1, reflection);
    {
      std::unique_lock<std::shared_mutex> lock(mutex_shares);
      auto result = shaders_.find(sha1);
      if (result != shaders_.end()) {
        return shaders_.at(sha1).get();
      }
#ifdef DXMT_DEBUG
      shader->bytecode = malloc(BytecodeLength);
      shader->bytecode_length = BytecodeLength;
      memcpy(shader->bytecode, pBytecode, BytecodeLength);
#endif
      return shaders_.emplace(sha1, std::move(shader)).first->second.get();
    }
  }

  virtual HRESULT AddVertexShader(const void *pBytecode,
                                  uint32_t BytecodeLength,
                                  ID3D11VertexShader **ppShader) override {
    auto managed_shader = CreateShader(pBytecode, BytecodeLength);
    if (!managed_shader) {
      return E_FAIL;
    }
    *ppShader =
        ref(new TShaderBase<ID3D11VertexShader, MTLD3D10VertexShader>(device, managed_shader));
    return S_OK;
  }

  virtual HRESULT AddPixelShader(const void *pBytecode, uint32_t BytecodeLength,
                                 ID3D11PixelShader **ppShader) override {
    auto managed_shader = CreateShader(pBytecode, BytecodeLength);
    if (!managed_shader) {
      return E_FAIL;
    }
    *ppShader = ref(new TShaderBase<ID3D11PixelShader, MTLD3D10PixelShader>(device, managed_shader));
    return S_OK;
  }

  virtual HRESULT AddHullShader(const void *pBytecode, uint32_t BytecodeLength,
                                ID3D11HullShader **ppShader) override {
    auto managed_shader = CreateShader(pBytecode, BytecodeLength);
    if (!managed_shader) {
      return E_FAIL;
    }
    *ppShader = ref(new TShaderBase<ID3D11HullShader>(device, managed_shader));
    return S_OK;
  }

  virtual HRESULT AddDomainShader(const void *pBytecode,
                                  uint32_t BytecodeLength,
                                  ID3D11DomainShader **ppShader) override {
    auto managed_shader = CreateShader(pBytecode, BytecodeLength);
    if (!managed_shader) {
      return E_FAIL;
    }
    *ppShader =
        ref(new TShaderBase<ID3D11DomainShader>(device, managed_shader));
    return S_OK;
  }

  virtual HRESULT AddGeometryShader(const void *pBytecode,
                                    uint32_t BytecodeLength,
                                    ID3D11GeometryShader **ppShader) override {
    auto managed_shader = CreateShader(pBytecode, BytecodeLength);
    if (!managed_shader) {
      return E_FAIL;
    }
    *ppShader =
        ref(new TShaderBase<ID3D11GeometryShader, MTLD3D10GeometryShader>(device, managed_shader));
    return S_OK;
  }

  virtual HRESULT AddComputeShader(const void *pBytecode,
                                   uint32_t BytecodeLength,
                                   ID3D11ComputeShader **ppShader) override {
    auto managed_shader = CreateShader(pBytecode, BytecodeLength);
    if (!managed_shader) {
      return E_FAIL;
    }
    *ppShader =
        ref(new TShaderBase<ID3D11ComputeShader>(device, managed_shader));
    return S_OK;
  }

  HRESULT AddInputLayout(const void *pShaderBytecodeWithInputSignature,
                         const D3D11_INPUT_ELEMENT_DESC *pInputElementDesc,
                         UINT NumElements,
                         IMTLD3D11InputLayout **ppInputLayout) override {
    std::lock_guard<dxmt::mutex> lock(mutex_ia_);
    std::vector<MTL_SHADER_INPUT_LAYOUT_ELEMENT_DESC> buffer(NumElements);
    uint32_t num_metal_ia_elements;
    HRESULT hr;
    if (FAILED(hr = ExtractMTLInputLayoutElements(
            device, pShaderBytecodeWithInputSignature, pInputElementDesc,
            NumElements, buffer.data(), &num_metal_ia_elements))) {
      return hr;
    }
    buffer.resize(num_metal_ia_elements);
    if (!input_layouts.contains(buffer)) {
      uint32_t input_slot_mask = 0;
      for (auto &element : buffer) {
        input_slot_mask |= (1 << element.Slot);
      }
      input_layouts.emplace(buffer, std::make_unique<CachedInputLayout>(
                                        std::move(buffer), input_slot_mask));
    }
    *ppInputLayout =
        ref(new MTLD3D11InputLayout(device, input_layouts.at(buffer).get()));
    return hr;
  }

  HRESULT
  AddStreamOutputLayout(const void *pShaderBytecode, UINT NumEntries,
                        const D3D11_SO_DECLARATION_ENTRY *pEntries,
                        UINT NumStrides, const UINT *pStrides,
                        IMTLD3D11StreamOutputLayout **ppSOLayout) override {
    std::lock_guard<dxmt::mutex> lock(mutex_so_);
    std::vector<MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC> buffer(NumEntries * 4);
    std::array<uint32_t, 4> strides = {{}};
    uint32_t num_metal_so_elements;
    if (FAILED(ExtractMTLStreamOutputElements(
            device, pShaderBytecode, NumEntries, pEntries, buffer.data(),
            &num_metal_so_elements))) {
      return E_FAIL;
    }
    buffer.resize(num_metal_so_elements);
    for (unsigned i = 0; i < NumStrides; i++) {
      strides[i] = pStrides[i];
    }
    MTL_STREAM_OUTPUT_DESC desc;
    memcpy(desc.Strides, strides.data(), sizeof(strides));
    desc.Elements = std::move(buffer);
    return so_layouts.CreateStateObject(&desc, ppSOLayout);
  };

  HRESULT AddBlendState(const D3D11_BLEND_DESC1 *pBlendDesc,
                        IMTLD3D11BlendState **ppBlendState) override {
    return blend_states.CreateStateObject(pBlendDesc, ppBlendState);
  }

  void GetGraphicsPipeline(MTL_GRAPHICS_PIPELINE_DESC *pDesc,
                           IMTLCompiledGraphicsPipeline **ppPipeline) override {
    std::lock_guard<dxmt::mutex> lock(mutex_);

    auto iter = pipelines_.find(*pDesc);
    if (iter != pipelines_.end()) {
      *ppPipeline = iter->second.ref();
      return;
    }
    auto temp = dxmt::CreateGraphicsPipeline(device, pDesc);
    if (!pipelines_.insert({*pDesc, temp}).second) // copy
    {
      D3D11_ASSERT(0 && "duplicated graphics pipeline");
    }
    *ppPipeline = std::move(temp);                          // move
  }

  void GetTessellationPipeline(
      MTL_GRAPHICS_PIPELINE_DESC *pDesc,
      IMTLCompiledTessellationPipeline **ppPipeline) override {
    std::lock_guard<dxmt::mutex> lock(mutex_ts_);

    auto iter = pipelines_ts_.find(*pDesc);
    if (iter != pipelines_ts_.end()) {
      *ppPipeline = iter->second.ref();
      return;
    }
    auto temp = dxmt::CreateTessellationPipeline(device, pDesc);
    if (!pipelines_ts_.insert({*pDesc, temp}).second) // copy
    {
      D3D11_ASSERT(0 && "duplicated tessellation pipeline");
    }
    *ppPipeline = std::move(temp);
  }

  void GetGeometryPipeline(
      MTL_GRAPHICS_PIPELINE_DESC *pDesc,
      IMTLCompiledGeometryPipeline **ppPipeline) override {
    std::lock_guard<dxmt::mutex> lock(mutex_ts_);

    auto iter = pipelines_gs_.find(*pDesc);
    if (iter != pipelines_gs_.end()) {
      *ppPipeline = iter->second.ref();
      return;
    }
    auto temp = dxmt::CreateGeometryPipeline(device, pDesc);
    if (!pipelines_gs_.insert({*pDesc, temp}).second) // copy
    {
      D3D11_ASSERT(0 && "duplicated geometry pipeline");
    }
    *ppPipeline = std::move(temp);
  }

public:
  PipelineCache(MTLD3D11Device *pDevice)
      : MTLD3D11PipelineCacheBase(pDevice), device(pDevice),
        blend_states(pDevice), so_layouts(pDevice) {

        };
};

std::unique_ptr<MTLD3D11PipelineCacheBase>
InitializePipelineCache(MTLD3D11Device *device) {
  return std::make_unique<PipelineCache>(device);
}

}; // namespace dxmt