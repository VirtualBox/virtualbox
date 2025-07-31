#pragma once
#include <array>
#include <cstdint>

namespace dxmt {

template <unsigned k> class PartitionedBloomFilter64 {
public:
  struct Key {
    std::array<uint8_t, k> indices;
  };

  constexpr bool isDisjointWith(PartitionedBloomFilter64 const &other) {
    for (unsigned i = 0; i < k; i++) {
      if ((bits[i] & other.bits[i]) == 0) {
        return true;
      }
    }
    return false;
  }

  constexpr void add(Key const &key) {
    for (unsigned i = 0; i < k; i++) {
      bits[i] |= (1uLL << key.indices[i]);
    }
  }

  void merge(PartitionedBloomFilter64 const &other) {
    for (unsigned i = 0; i < k; i++) {
      bits[i] |= other.bits[i];
    }
  }

  PartitionedBloomFilter64() : bits({}) {}

  static Key generateNewKey(uint64_t seq);

private:
  std::array<uint64_t, k> bits;
};

} // namespace dxmt
