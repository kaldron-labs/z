//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// time-based one time password (Google Authenticator compatible)

#include <zlib/ZtlsTOTP.hh>

#include <zlib/ZmTime.hh>

namespace Ztls::TOTP {

ZtlsExtern unsigned calc(ZuBSpan data, int offset)
{
  ZuBigEndian<uint64_t> t = (Zm::now().sec() / 30) + offset;
  HMAC<SHA1> hmac;
  uint8_t sha1[20];
  hmac.start(data);
  hmac.update({reinterpret_cast<const uint8_t *>(&t), 8});
  hmac.finish(sha1);
  typedef uint8_t Bytes[4];
  ZuPun<Bytes, uint32_t> pun;
  memcpy(&pun.in[0], sha1 + (sha1[19] & 0xf), 4);
  auto code = pun.out;
  code &= ~(uint32_t(1)<<31);
  return code % uint32_t(1000000);
}

ZtlsExtern bool verify(ZuBSpan data, unsigned code, unsigned range)
{
  for (int i = -static_cast<int>(range); i <= static_cast<int>(range); i++)
    if (code == calc(data, i)) return true;
  return false;
}

} // Ztls::TOTP
