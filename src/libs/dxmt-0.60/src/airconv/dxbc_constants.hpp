#pragma once

#include <cstdint>

namespace dxmt::dxbc {

enum class RegisterComponentType : uint32_t {
  Unknown,
  Uint = 1,
  Int = 2,
  Float = 3
};

#define _UNREACHABLE assert(0 && "unreachable");

/* anything in CAPITAL is not handled properly yet (rename in progress) */

} // namespace dxmt::dxbc
