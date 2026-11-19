//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixLoadAFXDP_HH
#define ZfixLoadAFXDP_HH

#include <net/if.h>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/Zutcp.hh>
#include <zlib/Zxdp.hh>

namespace ZfixLoad {

inline bool attachAFXDP(ZiMultiplex &mx)
{
  auto enabled = getenv("ZFIX_INTEROP_AFXDP");
  auto index = enabled && !strcmp(enabled, "1") ? if_nametoindex("zfixio") : 0;
  if (!index) {
    ZiLOG(Error, "zfix", "AF_XDP peers require afxdp-fixture.sh and the zfixio veth");
    return false;
  }
  auto xdp = Zxdp::instance(Zxdp::Type::Xdp);
  auto opened = xdp->init(&mx, Zxdp::Params{Zxdp::Xdp::Params{
    .ifindex = index, .txSid = mx.params().nThreads()}});
  if (opened.is<ZeException>()) { ZiLogEvent(ZuMv(opened).p<ZeException>()); return false; }
  ZmRef<Zutcp::Instance> tcp = new Zutcp::Instance;
  auto initialized = tcp->init(&mx, ZuMv(xdp), Zutcp::InstanceParams{});
  if (initialized.is<ZeException>()) {
    ZiLogEvent(ZuMv(initialized).p<ZeException>()); return false;
  }
  mx.plugin(ZuMv(tcp));
  return true;
}

} // namespace ZfixLoad

#endif /* ZfixLoadAFXDP_HH */
