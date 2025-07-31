#include "util_bloom.hpp"
#include <cstdint>
#include <cstdlib>

namespace dxmt {

uint64_t seeds[] = {0x1A2B3C4D5E6F7081, 0x2A3B4C5D6E7F8092, 0x3A4B5C6D7E8F90A3,
                    0x4A5B6C7D8E9F91B4, 0x5A6B7C8D9EAF92C5, 0x6A7B8C9D0E1F93D6,
                    0x7A8B9C0D1E2F94E7, 0x8A9B0C1D2E3F95F8, 0x9A0B1C2D3E4F96A9,
                    0x1A2B3C4D5E6F970A, 0x2A3B4C5D6E7F981B, 0x3A4B5C6D7E8F992C,
                    0x4A5B6C7D8E9F903D, 0x5A6B7C8D9EAF914E, 0x6A7B8C9D0E1F925F,
                    0x7A8B9C0D1E2F936F};

uint8_t hash_64_to_6(uint64_t value, uint64_t seed) {
  value ^= seed;
  value ^= value >> 33;
  value *= 304687631748357029;
  value ^= value >> 33;
  value *= 640853312006043431;
  value ^= value >> 33;

  return value & 0x3F;
}

template <unsigned k>
#ifdef VBOX
typename
#endif
PartitionedBloomFilter64<k>::Key
PartitionedBloomFilter64<k>::generateNewKey(uint64_t seq) {
  Key ret;
  for (unsigned i = 0; i < k; i++) {
    ret.indices[i] = hash_64_to_6(seq, seeds[i]);
  }
  return ret;
};

template PartitionedBloomFilter64<16>::Key PartitionedBloomFilter64<16>::generateNewKey(uint64_t seq);
} // namespace dxmt