#include "wsi_platform.hpp"
#include <cstdlib>

namespace dxmt::wsi {

void *aligned_malloc(size_t size, size_t alignment) {
  return _aligned_malloc(size, alignment);
}

void aligned_free(void *ptr) { return _aligned_free(ptr); }

} // namespace dxmt::wsi