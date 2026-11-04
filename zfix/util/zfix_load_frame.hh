//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixLoadFrame_HH
#define ZfixLoadFrame_HH

#include <zlib/ZmRef.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZfixTxMsg.hh>
#include "load_fix.hh"

namespace ZfixLoad {

using FrameAlloc = Zi::IOBufAlloc<ZiIOBuf,
  ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.Load.Frame">>;

inline ZmRef<ZiIOBuf> frameOutput(
  ZmRef<ZiIOBuf> storage, Zfix::Fmt::FrameOutput output)
{
  if (!output) return {};
  storage->skip = unsigned(output.data.data() -
    reinterpret_cast<char *>(storage->data_()));
  storage->length = output.data.length();
  return storage;
}

inline ZuSpan<char> grow(void *owner, unsigned used, unsigned needed) {
  auto buf = static_cast<ZiIOBuf *>(owner);
  buf->length = used;
  if (!buf->ensure(needed)) return {};
  return {reinterpret_cast<char *>(buf->data_()), buf->size};
}
template <typename Entry> ZmRef<ZiIOBuf> frame(const typename Entry::Object &o) {
  ZmRef<ZiIOBuf> storage = new FrameAlloc{nullptr, 0};
  Zfix::Fmt::FrameWriter writer{{}, "FIX.4.4", grow, storage.ptr()};
  Zfix::TxConfig config;
  config.identity.local = "ZFIX-C"; config.identity.peer = "ZFIX-S";
  Zfix::TxContext context{.seqNo = 2, .sendingTime = ZuDateTime{Zm::now()}};
  writer.body().field(35); writer.body() << Entry::MsgType; writer.body().end();
  mapping(o, [&writer, &o, &config, &context]<typename Facet>(Facet) {
    Zfix::ConfiguredSink<Facet> out{writer.body(), config, context};
    Zfix::Fmt::save<Facet>(out, o);
  });
  auto result = writer.finish();
  return frameOutput(ZuMv(storage), result);
}
} // ZfixLoad

#endif
