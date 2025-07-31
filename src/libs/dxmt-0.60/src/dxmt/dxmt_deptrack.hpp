#pragma once
#include "util_bloom.hpp"

namespace dxmt {
using EncoderDepSet = PartitionedBloomFilter64<16>;
using EncoderDepKey = EncoderDepSet::Key;
} // namespace dxmt
