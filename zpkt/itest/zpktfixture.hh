//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zpktfixture_HH
#define zpktfixture_HH

#include <zlib/ZiMultiplex.hh>
#include <net/if.h>
#include <zlib/ZpktEnv.hh>
#include <zlib/ZpktPool.hh>
#include "ZpktTestCf.hh"

namespace ZpktTest {

struct Interface {
  unsigned ifindex = 0, txSid = 0;
  int64_t spin = -1;
};

class Environment {
public:
  ZuUnion<void, ZeException> open(
    ZmRef<Zpkt::Nif> &nif, ZiMultiplex *mx, const Interface &params)
  {
    char name[IF_NAMESIZE]{};
    if (!if_indextoname(params.ifindex, name))
      return ZeEXCEPT(Error, "Zpkt.Test", "resolve fixture Linux interface");
    if (!m_loaded) {
      auto cf = config(ZPKT_XDP_MODULE, static_cast<const char *>(name),
	params.txSid, params.spin);
      m_env->init(cf.ptr());
      m_loaded = true;
    }
    auto opened = m_env->nif(mx, static_cast<const char *>(name));
    if (opened.is<ZeException>()) return ZuMv(opened).p<ZeException>();
    nif = opened.p<Zpkt::Nif *>();
    return {};
  }

private:
  ZmRef<Zpkt::Env> m_env = new Zpkt::Env;
  bool m_loaded = false;
};

inline ZmRef<Zi::IOBufPool> pool(Zi::IOBufType::T type, unsigned count, unsigned size)
{
  return new Zpkt::Xdp::BufPool{type, count, size};
}


inline ZeException start(
  Zpkt::Nif *instance, ZiMultiplex &mx, ZuUnion<void, ZeException> initialized,
  Zi::IOBufPool *rx, const Zi::IOBufPoolList &tx, Zpkt::RecvFn receive,
  Zpkt::StopFn stopped = {})
{
  if (initialized.is<ZeException>()) return ZuMv(initialized).p<ZeException>();
  auto allocated = instance->alloc(rx, tx);
  if (allocated.is<ZeException>()) return ZuMv(allocated).p<ZeException>();
  if (!mx.start()) return ZeEXCEPT(Error, "Zpkt.Test", "multiplexer start failed");
  if (!instance->start(ZuMv(receive), ZuMv(stopped))) return instance->error();
  return {};
}

} // namespace ZpktTest

#endif /* zpktfixture_HH */
