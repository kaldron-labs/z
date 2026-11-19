//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zxdpfixture_HH
#define zxdpfixture_HH

#include <zlib/ZiMultiplex.hh>
#include <zlib/Zxdp.hh>

namespace ZxdpTest {

inline ZeException start(
  Zxdp::Instance *instance, ZiMultiplex &mx, ZuUnion<void, ZeException> initialized,
  Zi::IOBufPool *rx, const Zi::IOBufPoolList &tx, Zxdp::RecvFn receive,
  Zxdp::StopFn stopped = {})
{
  if (initialized.is<ZeException>()) return ZuMv(initialized).p<ZeException>();
  auto allocated = instance->alloc(rx, tx);
  if (allocated.is<ZeException>()) return ZuMv(allocated).p<ZeException>();
  if (!mx.start()) return ZeEXCEPT(Error, "Zxdp.Test", "multiplexer start failed");
  if (!instance->start(ZuMv(receive), ZuMv(stopped))) return instance->error();
  return {};
}

} // namespace ZxdpTest

#endif /* zxdpfixture_HH */
