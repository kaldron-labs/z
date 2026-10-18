//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef PingHTTP_HH
#define PingHTTP_HH

#include <zlib/ZmEngine.hh>
#include <zlib/ZumService.hh>

class ZiMultiplex;

namespace Zum {
class PingHTTPState;

// Start before its consumers; stop after those consumers, then finalize.
class PingHTTP {
  PingHTTP(const PingHTTP &) = delete;
  PingHTTP &operator =(const PingHTTP &) = delete;
public:
  PingHTTP();
  ~PingHTTP();
  bool init(ZiMultiplex *, ZuCSpan issuer, ZuCSpan managementIssuer,
    ZuCSpan management, ZuCSpan caPath = {});
  using CtrlFn = ZmFn<void(bool), ZmFnHeapID<"ZmEngine.CtrlFn">>;
  bool start();
  void start(CtrlFn);
  bool stop();
  void stop(CtrlFn);
  int state() const;
  ServiceHTTPFn fn() const;
  void final();
private:
  ZmRef<PingHTTPState> m_state;
};
}

#endif
