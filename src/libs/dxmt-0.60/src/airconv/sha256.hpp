/*********************************************************************
* Authors:    Brad Conte (original code, brad AT bradconte.com)
*             Florian Ziesche (C++, LLVM/MetalLib integration)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Implementation of the SHA-256 hashing algorithm.
              SHA-256 is one of the three algorithms in the SHA2
              specification. The others, SHA-384 and SHA-512, are not
              offered in this implementation.
              Algorithm specification can be found here:
               *
http://csrc.nist.gov/publications/fips/fips180-2/fips180-2withchangenotice.pdf
              This implementation uses little endian byte order.
 * Original:  https://github.com/B-Con/crypto-algorithms
*********************************************************************/

#ifndef __METALLIB_SHA256_HPP__
#define __METALLIB_SHA256_HPP__

/*************************** HEADER FILES ***************************/
#include <cstdint>
#include <cstring>

/****************************** MACROS ******************************/
#define SHA256_BLOCK_SIZE 32 // SHA256 outputs a 32 byte digest

/**************************** DATA TYPES ****************************/

struct sha256_hash {
  uint8_t hash[SHA256_BLOCK_SIZE]{
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
};
static_assert(sizeof(sha256_hash) == SHA256_BLOCK_SIZE, "invalid hash size");

/****************************** MACROS ******************************/
#define SHA256_ROTLEFT(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define SHA256_ROTRIGHT(a, b) (((a) >> (b)) | ((a) << (32 - (b))))

#define SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x)                                                          \
  (SHA256_ROTRIGHT(x, 2) ^ SHA256_ROTRIGHT(x, 13) ^ SHA256_ROTRIGHT(x, 22))
#define SHA256_EP1(x)                                                          \
  (SHA256_ROTRIGHT(x, 6) ^ SHA256_ROTRIGHT(x, 11) ^ SHA256_ROTRIGHT(x, 25))
#define SHA256_SIG0(x)                                                         \
  (SHA256_ROTRIGHT(x, 7) ^ SHA256_ROTRIGHT(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x)                                                         \
  (SHA256_ROTRIGHT(x, 17) ^ SHA256_ROTRIGHT(x, 19) ^ ((x) >> 10))

/**************************** VARIABLES *****************************/
static constexpr const uint32_t k[64]{
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1,
    0x923F82A4, 0xAB1C5ED5, 0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174, 0xE49B69C1, 0xEFBE4786,
    0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147,
    0x06CA6351, 0x14292967, 0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85, 0xA2BFE8A1, 0xA81A664B,
    0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A,
    0x5B9CCA4F, 0x682E6FF3, 0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2};

/*********************** FUNCTION DEFINITIONS ***********************/
static inline sha256_hash compute_sha256_hash(const uint8_t *data,
                                              const size_t size) {
  // init
  struct {
    uint8_t data[64];
    uint32_t datalen{0};
    uint64_t bitlen{0};
    uint32_t state[8]{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  } ctx;

  // transform
  const auto sha256_transform = [&ctx]() {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4) {
      m[i] = (uint32_t(ctx.data[j]) << 24u) |
             (uint32_t(ctx.data[j + 1]) << 16u) |
             (uint32_t(ctx.data[j + 2]) << 8u) | uint32_t(ctx.data[j + 3]);
    }
    for (; i < 64; ++i) {
      m[i] =
          SHA256_SIG1(m[i - 2]) + m[i - 7] + SHA256_SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx.state[0];
    b = ctx.state[1];
    c = ctx.state[2];
    d = ctx.state[3];
    e = ctx.state[4];
    f = ctx.state[5];
    g = ctx.state[6];
    h = ctx.state[7];

    for (i = 0; i < 64; ++i) {
      t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + k[i] + m[i];
      t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }

    ctx.state[0] += a;
    ctx.state[1] += b;
    ctx.state[2] += c;
    ctx.state[3] += d;
    ctx.state[4] += e;
    ctx.state[5] += f;
    ctx.state[6] += g;
    ctx.state[7] += h;
  };

  // update
  for (size_t i = 0; i < size; ++i) {
    ctx.data[ctx.datalen] = data[i];
    ++ctx.datalen;
    if (ctx.datalen == 64) {
      sha256_transform();
      ctx.bitlen += 512;
      ctx.datalen = 0;
    }
  }

  // final
  auto pad_idx = ctx.datalen;

  // Pad whatever data is left in the buffer.
  if (ctx.datalen < 56) {
    ctx.data[pad_idx++] = 0x80;
    while (pad_idx < 56) {
      ctx.data[pad_idx++] = 0x00;
    }
  } else {
    ctx.data[pad_idx++] = 0x80;
    while (pad_idx < 64) {
      ctx.data[pad_idx++] = 0x00;
    }
    sha256_transform();
    memset(ctx.data, 0, 56);
  }

  // Append to the padding the total message's length in bits and transform.
  ctx.bitlen += ctx.datalen * 8;
  ctx.data[63] = uint8_t(ctx.bitlen & 0xFFull);
  ctx.data[62] = uint8_t((ctx.bitlen >> 8ull) & 0xFFull);
  ctx.data[61] = uint8_t((ctx.bitlen >> 16ull) & 0xFFull);
  ctx.data[60] = uint8_t((ctx.bitlen >> 24ull) & 0xFFull);
  ctx.data[59] = uint8_t((ctx.bitlen >> 32ull) & 0xFFull);
  ctx.data[58] = uint8_t((ctx.bitlen >> 40ull) & 0xFFull);
  ctx.data[57] = uint8_t((ctx.bitlen >> 48ull) & 0xFFull);
  ctx.data[56] = uint8_t((ctx.bitlen >> 56ull) & 0xFFull);
  sha256_transform();

  // Since this implementation uses little endian byte ordering and SHA uses
  // big endian, reverse all the bytes when copying the final state to the
  // output hash.
  sha256_hash ret;
  for (uint32_t i = 0; i < 4; ++i) {
    ret.hash[i] = (ctx.state[0] >> (24 - i * 8)) & 0x000000FF;
    ret.hash[i + 4] = (ctx.state[1] >> (24 - i * 8)) & 0x000000FF;
    ret.hash[i + 8] = (ctx.state[2] >> (24 - i * 8)) & 0x000000FF;
    ret.hash[i + 12] = (ctx.state[3] >> (24 - i * 8)) & 0x000000FF;
    ret.hash[i + 16] = (ctx.state[4] >> (24 - i * 8)) & 0x000000FF;
    ret.hash[i + 20] = (ctx.state[5] >> (24 - i * 8)) & 0x000000FF;
    ret.hash[i + 24] = (ctx.state[6] >> (24 - i * 8)) & 0x000000FF;
    ret.hash[i + 28] = (ctx.state[7] >> (24 - i * 8)) & 0x000000FF;
  }

  return ret;
}

// cleanup
#undef SHA256_ROTLEFT
#undef SHA256_ROTRIGHT
#undef SHA256_CH
#undef SHA256_MAJ
#undef SHA256_EP0
#undef SHA256_EP1
#undef SHA256_SIG0
#undef SHA256_SIG1

#endif