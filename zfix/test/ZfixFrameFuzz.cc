//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtArray.hh>
#include <zlib/Zfix.hh>
#include <zlib/ZfixFrame.hh>

using namespace Zfix::Fmt;

struct Entry {
  int		id = 0;
  ZuCSpan	data;
};

ZfStruct(, (Entry, FIX),
  (id, (FIX::ID<448>),		Int32),
  (data, (FIX::ID<58>), String));
ZfStructImpl(Entry);
ZfStructImpl(Entry, FIX);
struct Entries : ZtArray<Entry, ZtArrayHeapID<"Zfix.FuzzEntries">> {
  ZuDerive_(Entries, (ZtArray<Entry, ZtArrayHeapID<"Zfix.FuzzEntries">>));
friend AsArray<> Zfix_Fmt(Entries *);
};

struct Group {
  Entries	entries;
};

ZfStruct(, (Group, FIX),
  (entries, (FIX::ID<453>),	UDT));
ZfStructImpl(Group);
ZfStructImpl(Group, FIX);
struct Groups : ZtArray<Group, ZtArrayHeapID<"Zfix.FuzzGroups">> {
  ZuDerive_(Groups, (ZtArray<Group, ZtArrayHeapID<"Zfix.FuzzGroups">>));
friend AsArray<> Zfix_Fmt(Groups *);
};

struct Body {
  int		id = 0;
  Groups	groups;
  ZuCSpan	data;
};

ZfStruct(, (Body, FIX),
  (id, (FIX::ID<11>),			Int32),
  (groups, (FIX::ID<7000>),		UDT),
  (data, (FIX::ID<58>), String));
ZfStructImpl(Body);
ZfStructImpl(Body, FIX);

struct OutBody : Body {
  ZuCSpan sender = "FUZZ.SENDER";
  ZuCSpan target = "FUZZ.TARGET";
  uint64_t seqNo = 1;
  ZuDateTime sendingTime;
};
ZfStruct(, (OutBody),
  (id, (FIX::ID<11>), Int32),
  (groups, (FIX::ID<7000>), UDT),
  (data, (FIX::ID<58>), String));
using OutFIXFields = ZuTypeConcat<Zfix::SessionFields<OutBody>, ZfFields_OutBody>;
OutFIXFields ZuFields_(OutBody *, ZuFacet::FIX *);
ZfStructImpl(OutBody);
ZfStructImpl(OutBody, FIX);
using OutEntry = Zfix::MT<OutBody, "D">;

using Output = ZtArray<char, ZtArrayHeapID<"Zfix.FuzzOutput">>;
static ZuSpan<char> growOutput(void *owner, unsigned used, unsigned needed)
{
  auto bytes = static_cast<Output *>(owner);
  // A fresh envelope prefix is reserved but not yet initialized. Preserve only
  // bytes backed by the current allocation before requesting its extension.
  bytes->length(used < bytes->length() ? used : bytes->length(), false);
  bytes->length(needed, false);
  return {bytes->data(), bytes->length()};
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  // Experiment limits, not storage capacities: large enough for nested/text
  // mutations, small enough to bound each coverage-guided iteration's work.
  Limits cfg;
  cfg.frameBytes = 4096;
  cfg.fieldBytes = cfg.frameBytes;
  cfg.groupEntries = 32;
  cfg.groupDepth = 8;
  cfg.parseBytes = 64 << 10;
  if (size > cfg.frameBytes) return 0;
  ZuCSpan input{data, unsigned(size)};
  auto r = frame(input, cfg);
  Framer parser{cfg};
  auto prefix = parser.scan({input.data(), unsigned(input.length()/2)});
  if (prefix.state == FrameState::Incomplete) prefix = parser.scan(input);
  if (prefix.state != r.state || prefix.consumed != r.consumed) abort();
  Body body;
  if (r.state == FrameState::Complete) {
    if (!r.consumed || r.consumed > size || r.view.wire.length() != r.consumed) abort();
    MessageHandler<Body> h{r.view, cfg};
    if (!h.valid() || !h.load(body)) return 0;
  } else {
    if (r.consumed) abort();
    Handler<Body> h{input, cfg};
    if (!h.valid() || !h.load(body)) return 0;
  }
  Output bytes;
  static const ZuDateTime stamp{ZuDateTimeScan::FIX{}, "20261026-00:00:00"};
  OutBody output;
  static_cast<Body &>(output) = ZuMv(body);
  output.sendingTime = stamp;
  FrameOutput encoded;
  if (size & 1) {
    FrameWriter writer{{}, "FIX.4.4", growOutput, &bytes, cfg};
    writer.body().field(35); writer.body() << "D"; writer.body().end();
    save(writer.body(), output);
    encoded = writer.finish();
  } else {
    bytes.length(cfg.frameBytes, false);
    encoded = writeFrame<OutEntry>(bytes, "FIX.4.4", output, cfg);
  }
  if (!encoded) {
    if (encoded.error.code != ErrorCode::Size) abort();
    return 0;
  }
  auto check = frame(encoded.data, cfg);
  if (check.state != FrameState::Complete || check.consumed != encoded.data.length()) abort();
  MessageHandler<Body> verify{check.view, cfg};
  if (!verify.valid()) abort();
  auto reconstructed = verify.ctor();
  if (!verify.valid()) abort();
  return 0;
}
