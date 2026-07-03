//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Transport metadata shared by application-facing network CRTP interfaces

#ifndef ZiTransport_HH
#define ZiTransport_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZtEnum.hh>

namespace Zi {

ZtEnumStruct(Transport, int8_t, TCP, TLS, QUIC);

ZtEnumStruct(StreamType, int8_t, Duplex, Simplex);

struct Connected {
  Transport::T	transport = Transport::TCP;
  ZuCSpan	alpn;
  int		version = 0;
  bool		resumed = false;
  bool		earlyData = false;
};

} // namespace Zi

#endif /* ZiTransport_HH */
