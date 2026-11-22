//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TCP software checksum and offload seed preparation

#ifndef ZutcpChecksum_HH
#define ZutcpChecksum_HH

#ifndef ZutcpLib_HH
#include <zlib/ZutcpLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZiIP.hh>

namespace Zutcp {

// Encoded IP length is authoritative; it excludes Ethernet padding.
struct TCPLayout {
  uint16_t ip = 0, tcp = 0;
};

struct ZutcpAPI TCPChecksum {
friend struct TCPChecksumTest;
  uint8_t		*tcp = nullptr;
  unsigned	length = 0, pseudo = 0;
  unsigned	l2 = 0, l3 = 0, l4 = 0;
  bool		v6 = false;

  bool init(uint8_t *frame, unsigned wire, TCPLayout);
  unsigned value() const { return fold(pseudo + sum(tcp, length)) ^ 0xffff; }
  void finish();
  void seed();

  static unsigned sum(const void *, unsigned);
  static unsigned fold(uint64_t value) {
    value = (value & 0xffffffff) + (value>>32);
    value = (value & 0xffff) + (value>>16);
    value = (value & 0xffff) + (value>>16);
    return unsigned((value & 0xffff) + (value>>16));
  }

private:
  using SumFn = unsigned (*)(const void *, unsigned);
  struct Dispatch {
    SumFn	fn;
    unsigned	min;
  };
  static Dispatch select();
  static const Dispatch m_dispatch;
  static unsigned scalar(const void *, unsigned);
#if defined(__x86_64__)
  static unsigned sse2(const void *, unsigned);
  static unsigned avx2(const void *, unsigned);
#elif defined(__ARM_NEON) || defined(__aarch64__)
  static unsigned neon(const void *, unsigned);
#endif
};

} // namespace Zutcp

#endif /* ZutcpChecksum_HH */
