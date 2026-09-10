//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZumUpstream_HH
#define ZumUpstream_HH

#include <zlib/ZmRef.hh>

#include <zlib/ZumOIDC.hh>

class ZiMultiplex;

namespace Zum {

class UpstreamHTTPState;

class UpstreamHTTP {
public:
  UpstreamHTTP();
  ~UpstreamHTTP();

  UpstreamHTTP(const UpstreamHTTP &) = delete;
  UpstreamHTTP &operator =(const UpstreamHTTP &) = delete;

  bool init(ZiMultiplex *);
  OIDCHTTPFn fn() const;
  void final();

private:
  ZmRef<UpstreamHTTPState> m_state;
};

} // namespace Zum

#endif /* ZumUpstream_HH */
