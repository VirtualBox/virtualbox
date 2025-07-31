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

#include "util_hash.hpp"
#include <array>
#include <cstring>
#include <string>

namespace dxmt {

using Sha1Digest = std::array<uint8_t, 20>;

struct Sha1Data {
  const void *data;
  size_t size;
};

class Sha1Hash {

public:
  Sha1Hash() {}
  Sha1Hash(const Sha1Digest &digest) : m_digest(digest) {}

  std::string toString() const;

  uint32_t dword(uint32_t id) const {
    return uint32_t(m_digest[4 * id + 0]) << 0 |
           uint32_t(m_digest[4 * id + 1]) << 8 |
           uint32_t(m_digest[4 * id + 2]) << 16 |
           uint32_t(m_digest[4 * id + 3]) << 24;
  }

  bool operator==(const Sha1Hash &other) const {
    return !std::memcmp(this->m_digest.data(), other.m_digest.data(),
                        other.m_digest.size());
  }

  bool operator!=(const Sha1Hash &other) const {
    return !this->operator==(other);
  }

  static Sha1Hash compute(const void *data, size_t size);

  static Sha1Hash compute(size_t numChunks, const Sha1Data *chunks);

  template <typename T> static Sha1Hash compute(const T &data) {
    return compute(&data, sizeof(T));
  }

private:
  Sha1Digest m_digest;
};

} // namespace dxmt

namespace std {
template <> struct hash<dxmt::Sha1Hash> {
  size_t operator()(const dxmt::Sha1Hash &v) const noexcept {
    dxmt::HashState result;

    for (uint32_t i = 0; i < 5; i++)
      result.add(v.dword(i));

    return result;
  };
};

template <> struct equal_to<dxmt::Sha1Hash> {
  bool operator()(const dxmt::Sha1Hash &x, const dxmt::Sha1Hash &y) const {
    constexpr size_t binsize = sizeof(x);
    return std::string_view({reinterpret_cast<const char *>(&x), binsize}) ==
           std::string_view({reinterpret_cast<const char *>(&y), binsize});
  }
};
} // namespace std
