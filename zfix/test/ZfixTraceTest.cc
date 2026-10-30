//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfixTrace.hh>

using namespace ZuTestUtil;
using namespace Zfix;
struct MessageBody {
  ZuCSpan	id, password, extra;
  ZuBSpan	blob;
};

ZfStruct(, (MessageBody, FIX),
  (id, (FIX::ID<11>), String), (password, (FIX::ID<554>), String),
  (extra, (FIX::ID<9000>), String), (blob, (FIX::ID<9002>, FIX::Len<9001>), Bytes));
ZfStructImpl(MessageBody); ZfStructImpl(MessageBody, FIX);
struct Party {
  ZuCSpan	id;
  ZuBSpan	blob;
};

ZfStruct(, (Party, FIX), (id, (FIX::ID<448>), String),
  (blob, (FIX::ID<9002>, FIX::Len<9001>), Bytes));
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
struct Ambiguous {
  ZuCSpan	id;
  Parties	parties;
};

ZfStruct(, (Ambiguous, FIX), (id, (FIX::ID<9002>), String), (parties, (FIX::ID<453>), UDT));
ZfStructImpl(Ambiguous); ZfStructImpl(Ambiguous, FIX);
static TraceField resolve(void *, ZuCSpan, unsigned tag) {
  auto f = traceField<MessageBody>(tag); if (tag == 9000) f.secret = true; return f;
}
static bool contains(ZuCSpan text, ZuCSpan term) {
  return text.find(term) >= 0;
}
static ZmRef<ZiIOBuf> wire(ZuCSpan type, ZuCSpan content) {
  ZmRef<ZiIOBuf> b = new ZiIOBufAlloc<>;
  constexpr auto envelope = "8=FIX.4.4\1" "9=4294967295\1" "35=\1" "10=255\1"_z;
  b->ensure(type.length() + content.length() + envelope.length());
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
static void binary() {
  ZuTestScope(binary);
  WireTrace t{{.bytes = 64 << 10}};
  auto payload = "\1" "11=STOLEN\1\0="_z;
  ZtString content;
  content << "9001=" << payload.length() << '\1'
    << "9002=" << payload << '\1' << "11=visible\1";
  auto b = wire("D", content);
  auto record = t.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  ZuCheck(record && !contains(record->data().text(), "STOLEN"));
  ZuCheck(contains(record->data().text(), "11=visible") && contains(record->data().text(),
    "9002=<redacted>"));
  // Unknown binary metadata makes the entire remaining suffix opaque.
  b = wire("D", "9998=12\1" "9999=prefix\1" "11=HIDDEN\1");
  record = t.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  ZuCheck(contains(record->data().text(), "<opaque>") && !contains(record->data().text(),
    "HIDDEN"));
  b = wire("D", "9001=999\1" "9002=hidden\1" "11=HIDDEN\1");
  record = t.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  ZuCheck(contains(record->data().text(), "<malformed>") && !contains(record->data().text(),
    "HIDDEN"));
}
static void escaping() {
  ZuTestScope(escaping);
  WireTrace t{{.bytes = 64 << 10}};
  auto b = wire("D", "11=a\n\r\"\\b\1");
  auto record = t.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  ZuCheck(contains(record->data().text(), "a\\x0A\\x0D\\x22\\x5Cb"));
  for (auto c : record->data().text()) ZuCheck(uint8_t(c) >= 32 && uint8_t(c) < 127);
  b->data()[b->length - 2] ^= 1;
  record = t.capture_(ZuCSpan{b->cspan()}, {}, resolve);
  ZuCheck(record->data().text() == "<invalid frame>");
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
  auto f = traceField<GroupBody>(9001);
  ZuCheck(f.known && f.dataTag == 9002 && !f.ambiguous);
  f = traceField<Ambiguous>(9002); ZuCheck(f.ambiguous && !f.known);
  WireTrace t{{.bytes = 64 << 10}};
  auto b = wire("D", "453=1\1" "448=party\1" "9001=3\1" "9002=x\1=\1" "11=visible\1");
  auto record = t.capture_(ZuCSpan{b->cspan()}, {},
    [](void *, ZuCSpan, unsigned tag) { return traceField<GroupBody>(tag); });
  ZuCheck(contains(record->data().text(), "448=party") &&
    contains(record->data().text(), "11=visible") && contains(record->data().text(),
      "9002=<redacted>"));
  record = t.capture_(ZuCSpan{b->cspan()}, {},
    [](void *, ZuCSpan, unsigned tag) { return traceField<Ambiguous>(tag); });
  ZuCheck(contains(record->data().text(), "<opaque>") && !contains(record->data().text(),
    "9002="));
  record = t.capture_(ZuCSpan{b->cspan()}, {}, [](void *, ZuCSpan, unsigned tag) {
    auto f = traceField<GroupBody>(tag); if (tag == 9002) f.secret = true; return f;
  });
  ZuCheck(!contains(record->data().text(), "visible") && contains(record->data().text(),
    "<opaque>"));
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(redaction);
  ZuTestCall(binary);
  ZuTestCall(escaping);
  ZuTestCall(bounds);
  ZuTestCall(groups);
  return 0;
}
