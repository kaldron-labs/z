//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixLoadFrame_HH
#define ZfixLoadFrame_HH

#include <string.h>
#include <zlib/ZfixFrame.hh>
#include "load_fix.hh"

namespace ZfixLoad {

inline ZuSpan<char> grow(void *owner, unsigned used, unsigned needed) {
  auto &s = *static_cast<Text *>(owner);
  s.length(used < s.length() ? used : s.length());
  s.length(needed);
  return {s.data(), s.length()};
}
template <typename Entry> Text frame(const typename Entry::Object &o) {
  Text storage;
  Zfix::Fmt::FrameWriter writer{{}, "FIX.4.4", grow, &storage};
  Zfix::Fmt::Header h;
  h.beginString = "FIX.4.4"; h.msgType = Entry::MsgType;
  h.sender = "ZFIX-C"; h.target = "ZFIX-S"; h.seqNo = 2;
  h.sendingTime = ZuDateTime{Zm::now()};
  if (Zfix::Fmt::writeHeader(writer.body(), h)) return {};
  mapping(o, [&writer, &o]<typename Facet>(Facet) { Zfix::Fmt::save<Facet>(writer.body(), o); });
  auto result = writer.finish();
  if (!result) return {};
  // Return one owning fixture buffer; Text cannot own an interior data pointer.
  memmove(storage.data(), result.data.data(), result.data.length());
  storage.length(result.data.length());
  return storage;
}
} // ZfixLoad

#endif
