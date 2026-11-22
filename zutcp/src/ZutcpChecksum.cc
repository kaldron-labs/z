//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#if defined(__x86_64__)
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

#include <zlib/Zutcp.hh>
#include <zlib/ZutcpChecksum.hh>

namespace Zutcp {

static unsigned networkSum(uint64_t value)
{
  auto sum = TCPChecksum::fold(value);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  return sum;
#else
  return (sum>>8) | ((sum & 0xff)<<8);
#endif
}

unsigned TCPChecksum::scalar(const void *data, unsigned length)
{
  auto p = static_cast<const uint8_t *>(data);
  uint64_t value = 0;
  while (length >= 8) {
    uint64_t word;
    memcpy(&word, p, 8);
    auto previous = value;
    value += word;
    value += value < previous;
    p += 8; length -= 8;
  }
  unsigned sum = networkSum(value);
  while (length >= 2) {
    sum += (unsigned(p[0])<<8) | p[1];
    p += 2; length -= 2;
  }
  if (length) sum += unsigned(*p)<<8;
  return fold(sum);
}

// Reset 32-bit lanes every 64 KiB to support the full unsigned input extent.
// This bound keeps even an all-ones lane below 2^30, independently of the ISA.
enum { ChecksumBlock = 65536 };

#if defined(__x86_64__)
unsigned TCPChecksum::sse2(const void *data, unsigned length)
{
  auto p = static_cast<const uint8_t *>(data);
  uint64_t value = 0;
  unsigned bytes = length & ~15U;
  const auto zero = _mm_setzero_si128();
  while (bytes) {
    unsigned n = bytes > ChecksumBlock ? ChecksumBlock : bytes;
    auto acc = zero;
    for (unsigned i = 0; i < n; i += 16) {
      auto words = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p + i));
      acc = _mm_add_epi32(acc, _mm_unpacklo_epi16(words, zero));
      acc = _mm_add_epi32(acc, _mm_unpackhi_epi16(words, zero));
    }
    uint32_t lanes[4];
    _mm_storeu_si128(reinterpret_cast<__m128i *>(lanes), acc);
    for (auto lane : lanes) value += lane;
    p += n; length -= n; bytes -= n;
  }
  return fold(networkSum(value) + scalar(p, length));
}

__attribute__((target("avx2")))
unsigned TCPChecksum::avx2(const void *data, unsigned length)
{
  auto p = static_cast<const uint8_t *>(data);
  uint64_t value = 0;
  unsigned bytes = length & ~31U;
  const auto zero = _mm256_setzero_si256();
  while (bytes) {
    unsigned n = bytes > ChecksumBlock ? ChecksumBlock : bytes;
    auto acc = zero;
    for (unsigned i = 0; i < n; i += 32) {
      auto words = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p + i));
      acc = _mm256_add_epi32(acc, _mm256_unpacklo_epi16(words, zero));
      acc = _mm256_add_epi32(acc, _mm256_unpackhi_epi16(words, zero));
    }
    uint32_t lanes[8];
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(lanes), acc);
    for (auto lane : lanes) value += lane;
    p += n; length -= n; bytes -= n;
  }
  return fold(networkSum(value) + scalar(p, length));
}
#elif defined(__ARM_NEON) || defined(__aarch64__)
unsigned TCPChecksum::neon(const void *data, unsigned length)
{
  auto p = static_cast<const uint8_t *>(data);
  uint64_t value = 0;
  unsigned bytes = length & ~15U;
  while (bytes) {
    unsigned n = bytes > ChecksumBlock ? ChecksumBlock : bytes;
    auto acc = vdupq_n_u32(0);
    for (unsigned i = 0; i < n; i += 16)
      acc = vpadalq_u16(acc, vreinterpretq_u16_u8(vld1q_u8(p + i)));
    auto lanes = vpaddlq_u32(acc);
    value += vgetq_lane_u64(lanes, 0) + vgetq_lane_u64(lanes, 1);
    p += n; length -= n; bytes -= n;
  }
  return fold(networkSum(value) + scalar(p, length));
}
#endif

TCPChecksum::Dispatch TCPChecksum::select()
{
#if defined(__x86_64__)
  // Preserve the existing dispatch thresholds when relocating the checksum.
  // Recalibrate only with separate optimized measurements.
  __builtin_cpu_init();
  return __builtin_cpu_supports("avx2") ? Dispatch{avx2, 256} : Dispatch{sse2, 768};
#elif defined(__ARM_NEON) || defined(__aarch64__)
  return {neon, 64}; // provisional crossover; ARM is untested in this environment
#else
  return {scalar, 0};
#endif
}

// Resolve CPU dispatch during module initialization, outside packet service.
const TCPChecksum::Dispatch TCPChecksum::m_dispatch = TCPChecksum::select();

unsigned TCPChecksum::sum(const void *data, unsigned length)
{
  if (length < m_dispatch.min) return scalar(data, length);
  return m_dispatch.fn(data, length);
}

static unsigned get16(const uint8_t *p) { return (unsigned(p[0])<<8) | p[1]; }
static void put16(uint8_t *p, unsigned value) { p[0] = value>>8; p[1] = value; }

static unsigned pseudoSum(
  const uint8_t *local, const uint8_t *remote, unsigned bytes, unsigned length)
{
  return TCPChecksum::fold(TCPChecksum::sum(local, bytes) +
    TCPChecksum::sum(remote, bytes) + length + 6);
}

unsigned Instance::tcpChecksum(const ZiIP &local, const ZiIP &remote, ZuBSpan segment)
{
  if (segment.length() < 20 || segment.length() > 65535 ||
      local.type() != remote.type()) return 0xffff;
  unsigned pseudo;
  if (local.type() == ZiIPType::V4)
    pseudo = pseudoSum(reinterpret_cast<const uint8_t *>(&local.inAddr()),
      reinterpret_cast<const uint8_t *>(&remote.inAddr()), 4, segment.length());
  else if (local.type() == ZiIPType::V6)
    pseudo = pseudoSum(reinterpret_cast<const uint8_t *>(&local.in6Addr()),
      reinterpret_cast<const uint8_t *>(&remote.in6Addr()), 16, segment.length());
  else return 0xffff;
  return TCPChecksum::fold(pseudo + TCPChecksum::sum(segment.data(), segment.length())) ^ 0xffff;
}

bool TCPChecksum::init(uint8_t *frame, unsigned wire, TCPLayout layout)
{
  if (!frame || layout.ip > wire || wire - layout.ip < 20 ||
      layout.tcp < layout.ip) return false;
  auto ip = frame + layout.ip;
  unsigned version = ip[0]>>4, size;
  l2 = layout.ip;
  l3 = unsigned(layout.tcp) - layout.ip;
  if (version == 4) {
    unsigned head = unsigned(ip[0] & 15)<<2;
    if (head < 20 || l3 != head || ip[9] != 6 || (get16(ip + 6) & 0x3fff))
      return false;
    size = get16(ip + 2);
  } else if (version == 6) {
    if (wire - layout.ip < 40 || l3 < 40 || ((l3 == 40) != (ip[6] == 6)))
      return false;
    size = 40 + get16(ip + 4);
  } else return false;
  if (size > wire - layout.ip || size < l3 || size - l3 < 20 || size - l3 > 65535)
    return false;
  length = size - l3;
  tcp = frame + layout.tcp;
  l4 = unsigned(tcp[12]>>4)<<2;
  if (l4 < 20 || l4 > length) return false;
  v6 = version == 6;
  pseudo = v6 ? pseudoSum(ip + 8, ip + 24, 16, length) :
    pseudoSum(ip + 12, ip + 16, 4, length);
  return true;
}

void TCPChecksum::finish()
{
  put16(tcp + 16, 0);
  put16(tcp + 16, value());
}

void TCPChecksum::seed() { put16(tcp + 16, pseudo); }

bool Instance::tcpChecksum(uint8_t *frame, unsigned wire, TCPLayout layout, bool offload)
{
  TCPChecksum sum;
  if (!sum.init(frame, wire, layout)) return false;
  if (offload) sum.seed(); else sum.finish();
  return true;
}

} // namespace Zutcp
