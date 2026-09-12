//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// pooled HTTP client for upstream OIDC providers

#ifndef ZumUpstream_HH
#define ZumUpstream_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZmRef.hh>

#include <zlib/ZumOIDC.hh>

class ZiMultiplex;

namespace Zum {

class UpstreamHTTPState;

class UpstreamHTTP {
public:
  // Small deployments normally use only a handful of provider origins; callers
  // can raise this bound without changing the per-origin HTTP concurrency.
  enum { DefaultOrigins = 32 };
  UpstreamHTTP();
  ~UpstreamHTTP();

  UpstreamHTTP(const UpstreamHTTP &) = delete;
  UpstreamHTTP &operator =(const UpstreamHTTP &) = delete;

  // Own mutable transport state on a non-I/O scheduler shard. The caller drains
  // users of fn() before final(), which runs on the main thread with mx alive.
  // Empty caPath uses native system trust; otherwise use a CA file/directory.
  bool init(ZiMultiplex *, unsigned sid, unsigned origins = DefaultOrigins,
      ZuCSpan caPath = {});
  OIDCHTTPFn fn() const;
  void final();

private:
  ZmRef<UpstreamHTTPState> m_state;
};

} // namespace Zum

#endif /* ZumUpstream_HH */
