#pragma once
#include "llvm/IR/Module.h"
#include <cstdint>
#include "sha256.hpp"

namespace dxmt::metallib {

#define MTLB_FOURCC(ch0, ch1, ch2, ch3)                                        \
  ((uint32_t)(char)(ch0) | ((uint32_t)(char)(ch1) << 8) |                      \
   ((uint32_t)(char)(ch2) << 16) | ((uint32_t)(char)(ch3) << 24))

enum class MTLBFourCC : uint32_t {
  Name = MTLB_FOURCC('N', 'A', 'M', 'E'),
  Type = MTLB_FOURCC('T', 'Y', 'P', 'E'),
  Offset = MTLB_FOURCC('O', 'F', 'F', 'T'),
  Hash = MTLB_FOURCC('H', 'A', 'S', 'H'),
  Size = MTLB_FOURCC('M', 'D', 'S', 'Z'),
  Version = MTLB_FOURCC('V', 'E', 'R', 'S'),
  Layer = MTLB_FOURCC('L', 'A', 'Y', 'R'),
  Tess = MTLB_FOURCC('T', 'E', 'S', 'S'),
  EndTag = MTLB_FOURCC('E', 'N', 'D', 'T'),
  VertexAttribute = MTLB_FOURCC('V', 'A', 'T', 'T'),
  VertexAttributeType = MTLB_FOURCC('V', 'A', 'T', 'Y'),
};

const uint32_t MTLB_Magic = MTLB_FOURCC('M', 'T', 'L', 'B');

enum class Platform : uint16_t { MTLBPlatform_macOS = 0x8001 };

enum class FileType : uint8_t {
  MTLBType_Executable = 0x00,
};

enum class OS : uint8_t {
  MTLBOS_macOS = 0x81,
};

typedef struct __attribute__((packed)) {
  uint32_t Magic; // MTLB
  Platform Platform;
  uint16_t VersionMajor;
  uint16_t VersionMinor;
  FileType Type;
  OS OS;
  uint16_t OSVersionMajor;
  uint16_t OSVersionMinor;
  uint64_t FileSize;
  uint64_t FunctionListOffset;
  uint64_t FunctionListSize;
  uint64_t PublicMetadataOffset;
  uint64_t PublicMetadataSize;
  uint64_t PrivateMetadataOffset;
  uint64_t PrivateMetadataSize;
  uint64_t BitcodeOffset;
  uint64_t BitcodeSize;
} MTLBHeader;

enum class FunctionType : uint8_t {
  Vertex = 0x00,
  Fragment = 0x01,
  Kernel = 0x02,
  Mesh = 0x07,
  Object = 0x08,
};

struct __attribute__((packed)) MTLB_TYPE_TAG {
  MTLBFourCC TAG = MTLBFourCC::Type;
  uint16_t TAG_SIZE = 1;
  FunctionType type;
};

struct __attribute__((packed)) MTLB_HASH_TAG {
  MTLBFourCC TAG = MTLBFourCC::Hash;
  uint16_t TAG_SIZE = 0x20;
  sha256_hash hash;
};

struct __attribute__((packed)) MTLB_OFFT_TAG {
  MTLBFourCC TAG = MTLBFourCC::Offset;
  uint16_t TAG_SIZE = 0x18;
  uint64_t PublicMetadataOffset;
  uint64_t PrivateMetadataOffset;
  uint64_t BitcodeOffset;
};

struct __attribute__((packed)) MTLB_VERS_TAG {
  MTLBFourCC TAG = MTLBFourCC::Version;
  uint16_t TAG_SIZE = 0x08;
  uint16_t airVersionMajor;
  uint16_t airVersionMinor;
  uint16_t languageVersionMajor;
  uint16_t languageVersionMinor;
};

struct __attribute__((packed)) MTLB_MDSZ_TAG {
  MTLBFourCC TAG = MTLBFourCC::Size;
  uint16_t TAG_SIZE = 0x08;
  uint64_t bitcodeSize;
};

struct __attribute__((packed)) MTLB_TESS_TAG {
  MTLBFourCC TAG = MTLBFourCC::Tess;
  uint16_t TAG_SIZE = 1;
  uint8_t patchType : 2; // triangle - 1, quad - 2
  uint8_t controlPointCount : 6;
};

struct __attribute__((packed)) MTLB_VATY {
  uint8_t attribute : 8;
  uint8_t __ : 5 ;
  uint8_t usage : 2;
  bool active : 1;
};

static_assert(sizeof(MTLB_VATY) == 2, "");

class MetallibWriter {

public:
  void Write(const llvm::Module &module, llvm::raw_ostream &OS);
};

} // namespace dxmt::metallib