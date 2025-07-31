#include "wsi_platform.hpp"
#include <cstdlib>

namespace dxmt::wsi {

void *aligned_malloc(size_t size, size_t alignment) {
  return aligned_alloc(size, alignment);
}

void aligned_free(void *ptr) { return free(ptr); }

} // namespace dxmt::wsi