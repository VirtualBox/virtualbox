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

#include <atomic>

#include "../util_likely.hpp"

namespace dxmt {

/**
 * \brief Reference-counted object
 */
class RcObject {

public:
  /**
   * \brief Increments reference count
   * \returns New reference count
   */
  force_inline uint32_t incRef() { return ++m_refCount; }

  /**
   * \brief Decrements reference count
   * \returns New reference count
   */
  force_inline uint32_t decRef() { return --m_refCount; }

private:
  std::atomic<uint32_t> m_refCount = {0u};
};

} // namespace dxmt