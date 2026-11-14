//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Instance-owned TCP initial sequence primitive.

#ifndef ZutcpISN_HH
#define ZutcpISN_HH

#ifndef ZutcpLib_HH
#include <zlib/ZutcpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZiIP.hh>

namespace Zutcp {

class ZutcpAPI ISN {
friend struct ISNTest;
public:
  enum { SecretSize = 32, InputSize = 53 };
  ISN() = default;
  ISN(const ISN &) = delete;
  ISN &operator =(const ISN &) = delete;
  ~ISN();

  // Initialize once for the instance lifetime, including same-instance restart.
  // Failure leaves the primitive unavailable; there is no predictable fallback.
  bool init(unsigned interface);
  bool generate(const ZiSockAddr &local, const ZiSockAddr &remote, uint32_t &) const;

private:
  bool init(unsigned interface, ZuTime origin, int (*random)(uint8_t *, int));
  bool init(ZuBSpan secret, ZuTime origin, unsigned interface);
  bool encode(const ZiSockAddr &local, const ZiSockAddr &remote, uint8_t *) const;
  bool generate(
    const ZiSockAddr &local, const ZiSockAddr &remote, ZuTime now, uint32_t &) const;

  uint8_t	m_secret[SecretSize] = {};
  ZuTime	m_origin;
  unsigned	m_interface = 0;
};

} // namespace Zutcp

#endif /* ZutcpISN_HH */
