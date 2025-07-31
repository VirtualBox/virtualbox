#pragma once
#include "com/com_guid.hpp"
#include "d3d11_1.h"
#include "dxmt_buffer.hpp"
#include "dxmt_resource_binding.hpp"
#include "dxmt_texture.hpp"

struct MTL_RENDER_PASS_ATTACHMENT_DESC {
  uint32_t RenderTargetArrayLength;
  uint32_t SampleCount;
  uint32_t DepthPlane;
  uint32_t Width;
  uint32_t Height;
};

DEFINE_COM_INTERFACE("f1d21087-fbde-44b3-bc2c-b69be540a0ad",
                     IMTLD3D11RenderTargetView)
    : public ID3D11RenderTargetView1 {
  virtual WMTPixelFormat GetPixelFormat() = 0;
  virtual ULONG64 GetUnderlyingResourceId() = 0;
  virtual dxmt::ResourceSubset GetViewRange() = 0;
  virtual MTL_RENDER_PASS_ATTACHMENT_DESC &GetAttachmentDesc() = 0;

  virtual dxmt::Rc<dxmt::Texture> __texture() = 0;
  virtual unsigned __viewId() = 0;
};

DEFINE_COM_INTERFACE("42e48164-8733-422b-8421-4c57229641f9",
                     IMTLD3D11DepthStencilView)
    : public ID3D11DepthStencilView {
  virtual ULONG64 GetUnderlyingResourceId() = 0;
  virtual dxmt::ResourceSubset GetViewRange() = 0;
  virtual WMTPixelFormat GetPixelFormat() = 0;
  virtual MTL_RENDER_PASS_ATTACHMENT_DESC &GetAttachmentDesc() = 0;
  virtual UINT GetReadOnlyFlags() = 0;

  virtual dxmt::Rc<dxmt::Texture> __texture() = 0;
  virtual dxmt::Rc<dxmt::RenamableTexturePool> __renamable() = 0;
  virtual unsigned __viewId() = 0;
};

namespace dxmt {

struct D3D11ShaderResourceView : ID3D11ShaderResourceView1 {
  virtual Rc<Buffer> buffer() = 0;
  virtual BufferSlice bufferSlice() = 0;
  virtual Rc<Texture> texture() = 0;
  virtual unsigned viewId() = 0;
};

struct D3D11UnorderedAccessView : ID3D11UnorderedAccessView1 {
  virtual Rc<Buffer> buffer() = 0;
  virtual BufferSlice bufferSlice() = 0;
  virtual Rc<Texture> texture() = 0;
  virtual unsigned viewId() = 0;
  virtual Rc<Buffer> counter() = 0;
};

}
