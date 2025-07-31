#pragma once
#include <cstddef>

namespace dxmt::wsi {

void *aligned_malloc(size_t size, size_t alignment);

void aligned_free(void *ptr);

}; // namespace dxmt::wsi