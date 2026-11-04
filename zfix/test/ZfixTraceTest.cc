//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfixSession.hh>
#include <zlib/ZfixTrace.hh>

using LocalBuf = Zi::IOBufAlloc<ZiIOBuf, ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>;

using namespace ZuTestUtil;
using namespace Zfix;
struct MessageBody {
  ZuCSpan	id, password, extra;
};

ZfStruct(, (MessageBody, FIX),
  (id, (FIX::ID<11>), String), (password, (FIX::ID<554>), String),
  (extra, (FIX::ID<9000>), String));
ZfStructImpl(MessageBody); ZfStructImpl(MessageBody, FIX);
struct Party {
  ZuCSpan	id;
};

ZfStruct(, (Party, FIX), (id, (FIX::ID<448>), String));
ZfStructImpl(Party); ZfStructImpl(Party, FIX);
struct Parties : ZtArray<Party, ZtArrayHeapID<"Zfix.TraceTest">> {
friend Fmt::AsArray<> Zfix_Fmt(Parties *);
};

struct GroupBody {
  ZuCSpan	id;
  Parties	parties;
};

ZfStruct(, (GroupBody, FIX), (id, (FIX::ID<11>), String), (parties, (FIX::ID<453>), UDT));
ZfStructImpl(GroupBody); ZfStructImpl(GroupBody, FIX);
static TraceField resolve(void *, ZuCSpan, int32_t tag) {
  auto f = traceField<MessageBody>(tag); if (tag == 9000) f.secret = true; return f;
}
static bool contains(ZuCSpan text, ZuCSpan term) {
  return text.find(term) >= 0;
}
static ZmRef<ZiIOBuf> wire(ZuCSpan type, ZuCSpan content) {
  ZmRef<ZiIOBuf> b = new LocalBuf{nullptr, 0};
  constexpr auto Envelope = "8=FIX.4.4\1" "9=4294967295\1" "35=\1" "10=255\1"_z;
  b->ensure(type.length() + content.length() + Envelope.length());
  ZuSpan<char> storage{b->data(), b->size};
  Fmt::FrameWriter out{storage, "FIX.4.4"};
  out.body() << "35=" << type << '\1' << content;
  auto result = out.finish(); ZmAssert(result);
  b->skip = result.data.data() - storage.data();
  b->length = result.data.length();
  return b;
}
static void redaction() {
  ZuTestScope(redaction);
  WireTrace t{{.bytes = 64 << 10}};
  auto b = wire("A", "49=SECRET_ID\1" "554=password\1" "9000=private-auth\1");
  auto record = t.capture_(ZuCSpan{b->cspan()}, {7, 9, 1}, resolve);
  ZuCheck(record && contains(record->data().text(), "<redacted>"));
  ZuCheck(!contains(record->data().text(), "SECRET_ID") &&
    !contains(record->data().text(), "password") && !contains(record->data().text(),
      "private-auth"));
  ZuCheck(record->data().context.epoch == 7 && record->data().context.connection == 9);
  memset(b->data(), 'X', b->length); b = nullptr; t.clear_();
  ZuCheck(contains(record->data().text(), "FIX.4.4") && !t.stats_().bytes);
  b = wire("D", "11=public\1" "554=secret-password\1" "9000=custom-secret\1");
  record = t.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  ZuCheck(contains(record->data().text(), "11=public") &&
    !contains(record->data().text(), "secret-password") && !contains(record->data().text(),
      "custom-secret"));
  b = wire("D", "9000=custom-secret\1" "11=HIDDEN\1");
  record = t.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  ZuCheck(contains(record->data().text(), "9000=<redacted>") &&
    !contains(record->data().text(), "custom-secret") && !contains(record->data().text(),
      "HIDDEN"));
}
static void escaping() {
  ZuTestScopeRT(escaping);
  WireTrace t{{.bytes = 64 << 10}};
  auto b = wire("D", "11=a\n\r\"\\b\1");
  auto record = t.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  ZuCheckRT(contains(record->data().text(), "a\\x0A\\x0D\\x22\\x5Cb"));
  for (auto c : record->data().text()) ZuCheckRT(uint8_t(c) >= 32 && uint8_t(c) < 127);
  b->data()[b->length - 2] ^= 1;
  record = t.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  ZuCheckRT(record->data().text() == "<invalid frame>");
}
static void bounds() {
  ZuTestScope(bounds);
  auto b = wire("D", "11=public\1");
  WireTrace disabled;
  ZuCheck(!disabled.capture_(ZuCSpan{b->cspan()}, {}) && !disabled.stats_().records);
  WireTrace small{{.bytes = 1}};
  ZuCheck(!small.capture_(ZuCSpan{b->cspan()}, {}) && small.stats_().dropped == 1);
  WireTrace probe{{.bytes = UINT64_MAX, .recordBytes = 32}};
  auto old = probe.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  auto charge = probe.stats_().bytes;
  WireTrace t{{.bytes = charge, .recordBytes = 32}};
  auto first = t.capture_(ZuCSpan{b->cspan()}, {1, 1, 1}, resolve);
  auto second = t.capture_(ZuCSpan{b->cspan()}, {1, 1, 2}, resolve);
  auto s = t.stats_();
  ZuCheck(s.records == 2 && s.rotated == 1 && s.count == 1 && s.bytes == charge
    && s.truncated == 2);
  ZuCheck(first->data().context.seqNo == 1 && second->data().context.seqNo == 2);
  ZuCheck(first->data().text().length() <= 32 && first->data().truncated);
  t.clear_(); ZuCheck(!t.stats_().count && first->data().text());
}
static void groups() {
  ZuTestScope(groups);
  ZuCheck(traceField<GroupBody>(448).known && !traceField<GroupBody>(9001).known);
  WireTrace t{{.bytes = 64 << 10}};
  auto b = wire("D", "453=2\1" "448=party1\1" "448=party2\1" "11=visible\1");
  auto record = t.capture_(ZuCSpan{b->cspan()}, {},
    [](void *, ZuCSpan, int32_t tag) { return traceField<GroupBody>(tag); });
  ZuCheck(contains(record->data().text(), "448=party1") &&
    contains(record->data().text(), "448=party2") && contains(record->data().text(), "11=visible"));
  b = wire("D", "9999=unknown\1" "11=HIDDEN\1");
  record = t.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  ZuCheck(contains(record->data().text(), "<opaque>") && !contains(record->data().text(), "HIDDEN"));
}
struct BinaryBody {
  ZuBSpan data;
  ZuCSpan id;
};
ZfStruct(, (BinaryBody, FIX),
  (data, (FIX::ID<212>, FIX::Binary<213>), Bytes),
  (id, (FIX::ID<11>), String));

static void binary() {
  ZuTestScope(binary);
  WireTrace trace{{.bytes = 64 << 10}};
  constexpr auto content = "212=14\1" "213=A\1\0=11=FAKE\1" "\xffZ\1" "11=real\1"_z;
  auto b = wire("n", content);
  auto resolve = [](void *, ZuCSpan, int32_t tag) { return traceField<BinaryBody>(tag); };
  auto record = trace.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  ZuCheck(record && contains(record->data().text(), "213=A\\x01\\x00=11=FAKE\\x01\\xFFZ"));
  ZuCheck(contains(record->data().text(), "11=real") &&
    !contains(record->data().text(), "<malformed>"));
  record = trace.capture_(ZuCSpan{b->cspan()}, {},
    [](void *, ZuCSpan, int32_t tag) {
      auto f = traceField<BinaryBody>(tag);
      if (tag == 212) f.secret = true;
      return f;
    });
  ZuCheck(contains(record->data().text(), "212=<redacted>") &&
    !contains(record->data().text(), "FAKE") && !contains(record->data().text(), "11=real"));
  b = wire("n", "212=200\1" "213=A\1" "11=HIDDEN\1");
  record = trace.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  ZuCheck(contains(record->data().text(), "<malformed>") &&
    !contains(record->data().text(), "HIDDEN"));
  b = wire("n", content);
  record = trace.capture_(ZuCSpan{b->cspan()}, {},
    [](void *, ZuCSpan, int32_t tag) {
      auto f = traceField<BinaryBody>(tag);
      if (tag == 212) f.merge({.known = true});
      return f;
    });
  ZuCheck(contains(record->data().text(), "<opaque>") &&
    !contains(record->data().text(), "FAKE"));
}

struct RawReplay {
  ZuBSpan data;
};
ZfStruct(, (RawReplay, FIX), (data, (FIX::ID<95>, FIX::Binary<96>), Bytes));

static void authentication() {
  ZuTestScope(authentication);
  WireTrace trace{{.bytes = 64 << 10}};
  constexpr auto Credentials = "95=10\1" "96=secret\1\0=X\1"_z;
  auto b = wire("A", Credentials);
  auto record = trace.capture_(ZuCSpan{b->cspan()}, {});
  ZuCheck(record && contains(record->data().text(), "95=<redacted>") &&
    !contains(record->data().text(), "secret"));
  b = wire("U", Credentials);
  record = trace.capture_(ZuCSpan{b->cspan()}, {},
    [](void *, ZuCSpan, int32_t tag) { return traceField<RawReplay>(tag); });
  ZuCheck(record && contains(record->data().text(), "96=secret\\x01\\x00=X") &&
    !contains(record->data().text(), "<redacted>"));
}

struct AmbigEntry {
  ZuBSpan data;
};
ZfStruct(, (AmbigEntry, FIX), (data, (FIX::ID<212>, FIX::Binary<214>), Bytes));
struct AmbigEntries : ZtArray<AmbigEntry, ZtArrayHeapID<"Zfix.TraceAmbig">> {
friend Fmt::AsArray<> Zfix_Fmt(AmbigEntries *);
};
struct AmbigMessage {
  ZuBSpan data;
  AmbigEntries rows;
};
ZfStruct(, (AmbigMessage, FIX),
  (data, (FIX::ID<212>, FIX::Binary<213>), Bytes),
  (rows, (FIX::ID<6000>), UDT));

static void interpretations() {
  ZuTestScope(interpretations);
  auto builtin = traceBuiltin("0", 93);
  auto application = traceField<Session::HeartBt>(93);
  ZuCheck(builtin.known && application.known && builtin.valueTag == 89 &&
    application.valueTag == 89);
  builtin.merge(application);
  ZuCheck(!builtin.ambiguous);
  WireTrace trace{{.bytes = 64 << 10}};
  auto b = wire("0", "93=5\1" "89=A\1\0=B\1"_z);
  auto record = trace.capture_(ZuCSpan{b->cspan()}, {},
    [](void *, ZuCSpan, int32_t tag) { return traceField<Session::HeartBt>(tag); });
  ZuCheck(contains(record->data().text(), "93=5\\x0189=A\\x01\\x00=B\\x01") &&
    !contains(record->data().text(), "<opaque>") &&
    !contains(record->data().text(), "<malformed>"));
  auto ambiguous = traceField<AmbigMessage>(212);
  ZuCheck(ambiguous.known && ambiguous.ambiguous);
  b = wire("n", "6000=1\1" "212=3\1" "214=A\1B\1"_z);
  record = trace.capture_(ZuCSpan{b->cspan()}, {},
    [](void *, ZuCSpan, int32_t tag) { return traceField<AmbigMessage>(tag); });
  ZuCheck(contains(record->data().text(), "<opaque>") &&
    !contains(record->data().text(), "214="));
}

int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(authentication);
  ZuTestCall(interpretations);
  ZuTestCall(binary);
  ZuTestCall(redaction);
  ZuTestCall(escaping);
  ZuTestCall(bounds);
  ZuTestCall(groups);
  return 0;
}
