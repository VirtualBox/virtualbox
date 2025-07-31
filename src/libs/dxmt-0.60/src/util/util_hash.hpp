/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */
#pragma once

#include <cstddef>

namespace dxmt {

class HashState {

public:
  void add(size_t hash) {
    m_value ^= hash + 0x9e3779b9 + (m_value << 6) + (m_value >> 2);
  }

  operator size_t() const { return m_value; }

private:
  size_t m_value = 0;
};

} // namespace dxmt