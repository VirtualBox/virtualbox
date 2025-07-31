#pragma once

#include "airconv_public.h"
#include "com/com_guid.hpp"
#include "d3d11_device.hpp"
#include "d3d11_device_child.hpp"
#include "d3d11_input_layout.hpp"
#include "sha1/sha1_util.hpp"
#include "log/log.hpp"
#include <variant>

struct MTL_COMPILED_SHADER {
  /**
  NOTE: it's not retained by design
  */
  WMT::Function Function;
};

namespace dxmt {
class Shader;
};
typedef dxmt::Shader *ManagedShader;

DEFINE_COM_INTERFACE("e95ba1c7-e43f-49c3-a907-4ac669c9fb42", IMTLD3D11Shader)
    : public IUnknown {
  virtual ManagedShader GetManagedShader() = 0;
};

namespace dxmt {

class NullD3D10Shader {
public:
  NullD3D10Shader(IUnknown *discard) {}
};

template <typename Interface, typename D3D10Interface = NullD3D10Shader>
class TShaderBase : public MTLD3D11DeviceChild<Interface, IMTLD3D11Shader> {
private:
  ManagedShader shader;
  D3D10Interface d3d10;

public:
  TShaderBase(MTLD3D11Device *device, ManagedShader shader)
      : MTLD3D11DeviceChild<Interface, IMTLD3D11Shader>(device),
        shader(shader),
        d3d10(static_cast<Interface *>(this)) {}

  ~TShaderBase() {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(Interface)) {
      *ppvObject = ref_and_cast<Interface>(this);
      return S_OK;
    }

    if constexpr (!std::is_same<D3D10Interface, NullD3D10Shader>::value) {
      if (riid == __uuidof(ID3D10DeviceChild)
          || riid == __uuidof(typename D3D10Interface::ImplementedInterface)) {
        *ppvObject = ref_and_cast<D3D10Interface>(&d3d10);
        return S_OK;
      }
    }

    if (riid == __uuidof(IMTLD3D11Shader)) {
      *ppvObject = ref_and_cast<IMTLD3D11Shader>(this);
      return S_OK;
    }

    // FIXME: should it really be here?
    if (riid == __uuidof(IMTLD3D11StreamOutputLayout)) {
      return E_NOINTERFACE;
    }

    if (logQueryInterfaceError(__uuidof(Interface), riid)) {
      WARN("D3D11Shader: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  ManagedShader GetManagedShader() { return shader; }
};

struct ShaderVariantVertex {
  using this_type = ShaderVariantVertex;
  uint64_t input_layout_handle;
  uint32_t gs_passthrough;
  bool rasterization_disabled;
  bool operator==(const this_type &rhs) const {
    return input_layout_handle == rhs.input_layout_handle &&
           gs_passthrough == rhs.gs_passthrough &&
           rasterization_disabled == rhs.rasterization_disabled;
  }
};

struct ShaderVariantPixel {
  using this_type = ShaderVariantPixel;
  uint32_t sample_mask;
  bool dual_source_blending;
  bool disable_depth_output;
  uint32_t unorm_output_reg_mask;
  bool operator==(const this_type &rhs) const {
    return sample_mask == rhs.sample_mask &&
           dual_source_blending == rhs.dual_source_blending &&
           disable_depth_output == rhs.disable_depth_output &&
           unorm_output_reg_mask == rhs.unorm_output_reg_mask;
  }
};

struct ShaderVariantTessellationVertex {
  using this_type = ShaderVariantTessellationVertex;
  uint64_t input_layout_handle;
  uint64_t hull_shader_handle;
  SM50_INDEX_BUFFER_FORAMT index_buffer_format;
  bool operator==(const this_type &rhs) const {
    return input_layout_handle == rhs.input_layout_handle &&
           hull_shader_handle == rhs.hull_shader_handle &&
           index_buffer_format == rhs.index_buffer_format;
  }
};

struct ShaderVariantTessellationHull {
  using this_type = ShaderVariantTessellationHull;
  uint64_t vertex_shader_handle;
  bool operator==(const this_type &rhs) const {
    return vertex_shader_handle == rhs.vertex_shader_handle;
  }
};

struct ShaderVariantTessellationDomain {
  using this_type = ShaderVariantTessellationDomain;
  uint64_t hull_shader_handle;
  uint32_t gs_passthrough;
  bool rasterization_disabled;
  bool operator==(const this_type &rhs) const {
    return hull_shader_handle == rhs.hull_shader_handle &&
           gs_passthrough == rhs.gs_passthrough &&
           rasterization_disabled == rhs.rasterization_disabled;
  }
};

struct ShaderVariantGeometryVertex {
  using this_type = ShaderVariantGeometryVertex;
  uint64_t input_layout_handle;
  uint64_t geometry_shader_handle;
  SM50_INDEX_BUFFER_FORAMT index_buffer_format;
  bool strip_topology;
  bool operator==(const this_type &rhs) const {
    return input_layout_handle == rhs.input_layout_handle &&
           geometry_shader_handle == rhs.geometry_shader_handle &&
           index_buffer_format == rhs.index_buffer_format &&
           strip_topology == rhs.strip_topology;
  }
};

struct ShaderVariantGeometry {
  using this_type = ShaderVariantGeometry;
  uint64_t vertex_shader_handle;
  bool strip_topology;
  bool operator==(const this_type &rhs) const {
    return vertex_shader_handle == rhs.vertex_shader_handle &&
           strip_topology == rhs.strip_topology;
  }
};

struct ShaderVariantDefault {
  using this_type = ShaderVariantDefault;
  bool operator==(const this_type &rhs) const { return true; }
};

struct ShaderVariantVertexStreamOutput {
  using this_type = ShaderVariantVertexStreamOutput;
  uint64_t input_layout_handle;
  uint64_t stream_output_layout_handle;
  bool operator==(const this_type &rhs) const {
    return input_layout_handle == rhs.input_layout_handle &&
           stream_output_layout_handle == rhs.stream_output_layout_handle;
  }
};

using ShaderVariant =
    std::variant<ShaderVariantDefault, ShaderVariantVertex, ShaderVariantPixel,
                 ShaderVariantTessellationVertex, ShaderVariantTessellationHull,
                 ShaderVariantTessellationDomain, ShaderVariantVertexStreamOutput,
                 ShaderVariantGeometryVertex, ShaderVariantGeometry>;

class CompiledShader : public IMTLThreadpoolWork {
public:
  virtual ~CompiledShader() {};
  /**
  return false if it's not ready
   */
  virtual bool GetShader(MTL_COMPILED_SHADER *pShaderData) = 0;
};

class Shader {
public:
  virtual ~Shader() {};
  /* FIXME: exposed implementation detail */
  virtual sm50_shader_t handle() = 0;
  /* FIXME: exposed implementation detail */
  virtual MTL_SHADER_REFLECTION &reflection() = 0;
  virtual MTL_SM50_SHADER_ARGUMENT *constant_buffers_info() = 0;
  virtual MTL_SM50_SHADER_ARGUMENT *arguments_info() = 0;
  virtual Com<CompiledShader> get_shader(ShaderVariant variant) = 0;
  virtual const Sha1Hash& hash() = 0;
  virtual void dump() = 0;
};

template <typename Variant>
std::unique_ptr<CompiledShader>
CreateVariantShader(MTLD3D11Device *, ManagedShader, Variant);

} // namespace dxmt

namespace std {
template <> struct hash<dxmt::ShaderVariant> {
  size_t operator()(const dxmt::ShaderVariant &v) const noexcept {
    return v.index(); // FIXME:
  };
};
} // namespace std
