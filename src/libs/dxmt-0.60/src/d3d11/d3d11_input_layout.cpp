#include "d3d11_input_layout.hpp"
#include "d3d11_device_child.hpp"
#include "DXBCParser/DXBCUtils.h"
#include "d3d11_shader.hpp"
#include "d3d11_state_object.hpp"
#include "dxmt_format.hpp"
#include "util_math.hpp"
#include "log/log.hpp"

namespace dxmt {

HRESULT ExtractMTLInputLayoutElements(
    MTLD3D11Device *device, const void *pShaderBytecodeWithInputSignature,
    const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, uint32_t NumElements,
    MTL_SHADER_INPUT_LAYOUT_ELEMENT_DESC *pInputLayout,
    uint32_t *pNumElementsOut) {

  using namespace microsoft;
  uint16_t append_offset[32] = {0};
  uint32_t register_mask = 0;

  CSignatureParser parser;
  HRESULT hr =
      DXBCGetInputSignature(pShaderBytecodeWithInputSignature, &parser);
  if (FAILED(hr)) {
    return hr;
  }
  const D3D11_SIGNATURE_PARAMETER *pParamters;
  auto num_parameters = parser.GetParameters(&pParamters);

  UINT attribute_count = 0;
  for (UINT j = 0; j < NumElements; j++) {
    auto &desc = pInputElementDescs[j];
    auto pSig = std::find_if(
        pParamters, pParamters + num_parameters,
        [&](const D3D11_SIGNATURE_PARAMETER &inputSig) {
          return desc.SemanticIndex == inputSig.SemanticIndex &&
                 strcasecmp(desc.SemanticName, inputSig.SemanticName) == 0;
        });
    if (pSig == pParamters + num_parameters)
      continue; // shader has no such input register, so skip it
    auto &inputSig = *pSig;
    auto &attribute = pInputLayout[attribute_count++];

    MTL_DXGI_FORMAT_DESC metal_format;
    if (FAILED(MTLQueryDXGIFormat(device->GetMTLDevice(), desc.Format, metal_format))) {
      ERR("CreateInputLayout: Unsupported vertex format ", desc.Format);
      return E_FAIL;
    }

    if (!metal_format.AttributeFormat) {
      ERR("CreateInputLayout: Unsupported vertex format ", desc.Format);
      return E_INVALIDARG;
    }
    attribute.Format = metal_format.AttributeFormat;

    attribute.Slot = desc.InputSlot;
    attribute.Index = inputSig.Register;

    if (desc.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT) {
      attribute.Offset = align(append_offset[attribute.Slot],
                               std::min(4u, metal_format.BytesPerTexel));
    } else {
      attribute.Offset = desc.AlignedByteOffset;
    }
    append_offset[attribute.Slot] =
        attribute.Offset + metal_format.BytesPerTexel;
    // the layout stride is provided in IASetVertexBuffer
    attribute.StepFunction = desc.InputSlotClass;
    attribute.InstanceStepRate =
        desc.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA
            ? desc.InstanceDataStepRate
            : 1;
    register_mask |= (1 << inputSig.Register);
  }
  for (UINT i = 0; i < num_parameters; i++) {
    auto &inputSig = pParamters[i];
    if (inputSig.SystemValue != D3D10_SB_NAME_UNDEFINED) 
      continue; // ignore SIV & SGV
    if (!(register_mask & (1 << inputSig.Register))) {
      WARN("CreateInputLayout: Vertex shader expects ", inputSig.SemanticName,
           "_", inputSig.SemanticIndex, " but it's not in input layout element descriptors");
      return E_INVALIDARG;
    }
  }
  *pNumElementsOut = attribute_count;

  return S_OK;
}

class MTLD3D11StreamOutputLayout final
    : public ManagedDeviceChild<IMTLD3D11StreamOutputLayout> {
public:
  MTLD3D11StreamOutputLayout(MTLD3D11Device *device,
                             const MTL_STREAM_OUTPUT_DESC &desc)
      : ManagedDeviceChild<IMTLD3D11StreamOutputLayout>(device), desc_(desc),
        null_gs(this) {}

  ~MTLD3D11StreamOutputLayout() {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11GeometryShader) ||
        riid == __uuidof(IMTLD3D11StreamOutputLayout)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(IMTLD3D11Shader)) {
      *ppvObject = ref(&null_gs);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(IMTLD3D11StreamOutputLayout), riid)) {
      WARN("IMTLD3D11StreamOutputLayout: Unknown interface query ",
           str::format(riid));
    }

    return E_NOINTERFACE;
  };

  uint32_t STDMETHODCALLTYPE
  GetStreamOutputElements(MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC **ppElements,
                          uint32_t Strides[4]) override {
    *ppElements = desc_.Elements.data();
    memcpy(Strides, desc_.Strides, sizeof(desc_.Strides));
    return desc_.Elements.size();
  }

private:
  MTL_STREAM_OUTPUT_DESC desc_;

  class NullGeometryShader : public IMTLD3D11Shader {
    IUnknown *container;

  public:
    NullGeometryShader(IUnknown *container) : container(container) {};

    ULONG STDMETHODCALLTYPE AddRef() override { return container->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return container->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override {
      return container->QueryInterface(riid, ppvObject);
    }

    virtual ManagedShader GetManagedShader() override { return nullptr; };
  };

  NullGeometryShader null_gs;
};

HRESULT ExtractMTLStreamOutputElements(
    MTLD3D11Device *pDevice, const void *pShaderBytecode, UINT NumEntries,
    const D3D11_SO_DECLARATION_ENTRY *pEntries,
    MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC *pStreamOut,
    uint32_t *pNumElementsOut) {
  using namespace microsoft;
  std::array<uint32_t, 4> offsets = {{}};
  uint32_t element_count = 0;
  CSignatureParser parser;
  HRESULT hr = DXBCGetOutputSignature(pShaderBytecode, &parser);
  if (FAILED(hr)) {
    return hr;
  }
  const D3D11_SIGNATURE_PARAMETER *pParamters;
  auto numParameteres = parser.GetParameters(&pParamters);
  for (unsigned i = 0; i < NumEntries; i++) {
    auto entry = pEntries[i];
    if (entry.Stream != 0) {
      ERR("CreateEmulatedVertexStreamOutputShader: stream must be 0");
      return E_INVALIDARG;
    }
    if (entry.OutputSlot > 3) {
      ERR("CreateEmulatedVertexStreamOutputShader: invalid output slot ",
          entry.OutputSlot);
      return E_INVALIDARG;
    }
    // FIXME: support more than 1 output slot
    if (entry.OutputSlot != 0) {
      ERR("CreateEmulatedVertexStreamOutputShader: only slot 0 supported");
      return E_INVALIDARG;
    }
    if ((entry.ComponentCount - entry.StartComponent) < 0 ||
        (entry.ComponentCount + entry.StartComponent) > 4) {
      ERR("CreateEmulatedVertexStreamOutputShader: invalid components");
      return E_INVALIDARG;
    }
    if ((entry.ComponentCount - entry.StartComponent) == 0) {
      continue;
    }
    if (entry.SemanticName == nullptr) {
      // skip hole
      for (unsigned i = 0; i < entry.ComponentCount; i++) {
        auto offset = offsets[entry.OutputSlot];
        offsets[entry.OutputSlot] += sizeof(float);
        auto component = entry.StartComponent + i;
        pStreamOut[element_count++] = {.Register = 0xffffffff,
                                       .Component = component,
                                       .OutputSlot = entry.OutputSlot,
                                       .Offset = offset};
      }
      continue;
    }
    auto pDesc = std::find_if(
        pParamters, pParamters + numParameteres,
        [&](const D3D11_SIGNATURE_PARAMETER &Ele) {
          return Ele.SemanticIndex == entry.SemanticIndex &&
                 strcasecmp(Ele.SemanticName, entry.SemanticName) == 0;
        });
    if (pDesc == pParamters + numParameteres) {
      ERR("CreateEmulatedVertexStreamOutputShader: output parameter not found");
      return E_INVALIDARG;
    }
    auto reg_id = pDesc->Register;
    for (unsigned i = 0; i < entry.ComponentCount; i++) {
      auto offset = offsets[entry.OutputSlot];
      offsets[entry.OutputSlot] += sizeof(float);
      auto component = entry.StartComponent + i;
      pStreamOut[element_count++] = {.Register = reg_id,
                                     .Component = component,
                                     .OutputSlot = entry.OutputSlot,
                                     .Offset = offset};
    }
  }
  *pNumElementsOut = element_count;
  return S_OK;
}

template <>
HRESULT StateObjectCache<MTL_STREAM_OUTPUT_DESC, IMTLD3D11StreamOutputLayout>::
    CreateStateObject(const MTL_STREAM_OUTPUT_DESC *pSOLayoutDesc,
                      IMTLD3D11StreamOutputLayout **ppSOLayout) {
  std::lock_guard<dxmt::mutex> lock(mutex_cache);
  InitReturnPtr(ppSOLayout);

  if (!pSOLayoutDesc)
    return E_INVALIDARG;

  if (!ppSOLayout)
    return S_FALSE;

  if (cache.contains(*pSOLayoutDesc)) {
    cache.at(*pSOLayoutDesc)->QueryInterface(IID_PPV_ARGS(ppSOLayout));
    return S_OK;
  }

  cache.emplace(*pSOLayoutDesc, std::make_unique<MTLD3D11StreamOutputLayout>(
                                    device, *pSOLayoutDesc));
  cache.at(*pSOLayoutDesc)->QueryInterface(IID_PPV_ARGS(ppSOLayout));

  return S_OK;
}

} // namespace dxmt