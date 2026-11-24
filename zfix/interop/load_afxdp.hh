//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixLoadAFXDP_HH
#define ZfixLoadAFXDP_HH

#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>

#include <zlib/ZiLog.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/Zutcp.hh>
#include <zlib/Zpkt.hh>
#include <zlib/ZpktEnv.hh>
#include "ZpktTestCf.hh"

namespace ZfixLoad {

inline unsigned afxdpPoolCount()
{
  static const unsigned count = [] {
    auto value = getenv("ZFIX_INTEROP_POOL_COUNT");
    if (!value) return unsigned(ZiIOBuf_DefltCount);
    char *end = nullptr;
    auto n = strtoul(value, &end, 10);
    return end != value && !*end && n >= 1024 && n <= 65536 ? unsigned(n) : 0U;
  }();
  return count;
}

template <typename Alloc> Zi::IOBufAllocator *afxdpAllocator(ZiMultiplex *mx)
{
  return [mx]<Zi::IOBufType::T Type, typename Buf, unsigned Size, unsigned Align,
    typename HeapID, unsigned Max, auto Grow>(
    Zi::IOBufBuiltinAllocator<Type, Buf, Size, Align, HeapID, Max, Grow> *)
      -> Zi::IOBufAllocator * {
    using Builtin = Zi::IOBufBuiltinAllocator<Type, Buf, Size, Align, HeapID, Max, Grow>;
    if (auto a = mx->template findAllocator<Builtin>()) return a;
    if constexpr (Type == Zi::IOBufType::Rx || Type == Zi::IOBufType::TxUDP ||
	Type == Zi::IOBufType::TxTCP) {
      using Pool = Zi::IOBufPoolAllocator<Type, Buf, Size, Align, HeapID, Max, Grow>;
      if (auto a = mx->template findAllocator<Pool>()) return a;
    }
    return mx->template allocator<Type, Buf, Size, Align, HeapID, Max, Grow>(afxdpPoolCount());
  }(static_cast<Alloc *>(nullptr));
}

inline bool attachAFXDP(Zpkt::Env *env, ZiMultiplex &mx, bool encrypted)
{
  auto count = afxdpPoolCount();
  if (!count) {
    ZiLOG(Error, "zfix", "ZFIX_INTEROP_POOL_COUNT must be between 1024 and 65536");
    return false;
  }
  auto enabled = getenv("ZFIX_INTEROP_AFXDP");
  auto index = enabled && !strcmp(enabled, "1") ? if_nametoindex("zfixio") : 0;
  if (!index) {
    ZiLOG(Error, "zfix", "AF_XDP peers require afxdp-fixture.sh and the zfixio veth");
    return false;
  }
  auto cf = ZpktTest::config(ZFIX_XDP_MODULE, "zfixio", mx.params().nThreads());
  try { env->init(cf.ptr()); }
  catch (ZeException &e) { ZiLogEvent(e); return false; }
  auto opened = env->nif(&mx, "zfixio");
  if (opened.is<ZeException>()) { ZiLogEvent(ZuMv(opened).p<ZeException>()); return false; }
  ZmRef<Zpkt::Nif> xdp = opened.p<Zpkt::Nif *>();
  ZmRef<Zutcp::Instance> tcp = new Zutcp::Instance;
  auto initialized = tcp->init(&mx, env, ZuMv(xdp), Zutcp::InstanceParams{.packetCount = count});
  if (initialized.is<ZeException>()) {
    xdp->final();
    if (auto error = xdp->error()) ZiLogEvent(ZuMv(error));
    ZiLogEvent(ZuMv(initialized).p<ZeException>()); return false;
  }
  mx.plugin(ZuMv(tcp));
  // TLS requests this packet type internally. Register it once at the selected
  // capacity before its first hub; plain TCP needs only its formatting pool.
  return !encrypted ||
    afxdpAllocator<Zi::IOBufBuiltinAllocator<Zi::IOBufType::TxTCP, ZiIOBuf>>(&mx);
}

inline bool readyAFXDP(ZiMultiplex &mx)
{
  auto tcp = static_cast<Zutcp::Instance *>(mx.plugin());
  if (!tcp || !tcp->ready()) return false;
  std::cout << "{\"event\":\"tcp_backend\",\"backend\":\"Zutcp/AF_XDP\","
    "\"ready\":true,\"poolCount\":" << afxdpPoolCount() << "}\n" << std::flush;
  return true;
}

} // namespace ZfixLoad

#endif /* ZfixLoadAFXDP_HH */
