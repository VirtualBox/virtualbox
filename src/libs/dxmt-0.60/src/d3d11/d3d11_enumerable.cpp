#include "d3d11_enumerable.hpp"

namespace dxmt {

generator<SUBRESOURCE_ENTRY>
EnumerateSubresources(const D3D11_TEXTURE1D_DESC &Desc) {
  for (uint32_t slice = 0; slice < Desc.ArraySize; slice++) {
    for (uint32_t level = 0; level < Desc.MipLevels; level++) {
      co_yield SUBRESOURCE_ENTRY{
          D3D11CalcSubresource(level, slice, Desc.MipLevels), level, slice};
    }
  }
}
generator<SUBRESOURCE_ENTRY>
EnumerateSubresources(const D3D11_TEXTURE2D_DESC &Desc) {
  for (uint32_t slice = 0; slice < Desc.ArraySize; slice++) {
    for (uint32_t level = 0; level < Desc.MipLevels; level++) {
      co_yield SUBRESOURCE_ENTRY{
          D3D11CalcSubresource(level, slice, Desc.MipLevels), level, slice};
    }
  }
}
generator<SUBRESOURCE_ENTRY>
EnumerateSubresources(const D3D11_TEXTURE2D_DESC1 &Desc) {
  for (uint32_t slice = 0; slice < Desc.ArraySize; slice++) {
    for (uint32_t level = 0; level < Desc.MipLevels; level++) {
      co_yield SUBRESOURCE_ENTRY{
          D3D11CalcSubresource(level, slice, Desc.MipLevels), level, slice};
    }
  }
}
generator<SUBRESOURCE_ENTRY>
EnumerateSubresources(const D3D11_TEXTURE3D_DESC &Desc) {
  for (uint32_t level = 0; level < Desc.MipLevels; level++) {
    co_yield SUBRESOURCE_ENTRY{D3D11CalcSubresource(level, 0, Desc.MipLevels),
                               level, 0};
  }
}
generator<SUBRESOURCE_ENTRY>
EnumerateSubresources(const D3D11_TEXTURE3D_DESC1 &Desc) {
  for (uint32_t level = 0; level < Desc.MipLevels; level++) {
    co_yield SUBRESOURCE_ENTRY{D3D11CalcSubresource(level, 0, Desc.MipLevels),
                               level, 0};
  }
}

}; // namespace dxmt