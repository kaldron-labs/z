//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixTxMsg.hh>
#include "../util/load_fix.hh"
#include "codec.hh"

struct NativeCodec {
  static constexpr auto	Name = "zfix"_z;
  ZfixLoad::Text	storage;

  static ZuSpan<char> grow(void *owner, unsigned used, unsigned needed) {
    auto &s = static_cast<NativeCodec *>(owner)->storage;
    s.length(used < s.length() ? used : s.length());
    s.length(needed); return s;
  }

  ZuCSpan encode(const ZfixLoad::Order &o) {
    Zfix::Fmt::FrameWriter writer{storage, "FIX.4.4", grow, this};
    Zfix::TxConfig config;
    config.identity.local = "ZFIX-C"; config.identity.peer = "ZFIX-S";
    Zfix::TxContext context{.seqNo = 2, .sendingTime = o.time};
    writer.body().field(35); writer.body() << "D"; writer.body().end();
    ZfixLoad::mapping(o, [&writer, &o, &config, &context]<typename Facet>(Facet) {
      Zfix::ConfiguredSink<Facet> out{writer.body(), config, context};
      Zfix::Fmt::save<Facet>(out, o);
    });
    auto result = writer.finish(); return result ? result.data : ZuCSpan{};
  }

  uint64_t decode(ZuCSpan bytes, unsigned qty) {
    auto parsed = Zfix::Fmt::frame(bytes);
    if (parsed.state != Zfix::Fmt::FrameState::Complete) return 0;
    Zfix::Fmt::MessageHandler<ZfixLoad::Order> handler{parsed.view};
    auto value = handler.ctor();
    return handler.valid() && value.qty == qty ? value.key : 0;
  }
};

int main(int argc, char **argv) {
  NativeCodec engine; return ZfixBench::codec(engine, argc, argv);
}
