//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 HPACK test

#include <stdio.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZhttpHPack.hh>

using namespace ZuTestUtil;

namespace {

using Bytes = Zhttp::H2::HPackBytes;

struct OwnedField {
  Zhttp::H2::HPackString	name;
  Zhttp::H2::HPackString	value;
};

using Fields =
  ZtArray<OwnedField, ZtArrayHeapID<"Zhttp.HPackTest.Fields">>;

Bytes hex(ZuCSpan value)
{
  Bytes out;
  unsigned high = 0;
  bool haveHigh = false;
  for (unsigned i = 0; i < value.length(); ++i) {
    int c = value[i];
    if (c == ' ') continue;
    unsigned digit =
      c >= '0' && c <= '9' ? unsigned(c - '0') :
      c >= 'a' && c <= 'f' ? unsigned(c - 'a' + 10) :
      c >= 'A' && c <= 'F' ? unsigned(c - 'A' + 10) : 16;
    if (digit > 15) continue;
    if (!haveHigh) {
      high = digit;
      haveHigh = true;
    } else {
      out.push(uint8_t((high << 4) | digit));
      haveHigh = false;
    }
  }
  return out;
}

bool decode(
  Zhttp::H2::HPackDecoder &decoder, ZuBSpan encoded, Fields &fields)
{
  decoder.reset();
  fields.length(0);
  for (unsigned i = 0; i < encoded.length(); ++i)
    if (decoder.process(
	ZuBSpan{encoded.data() + i, 1},
	[&fields](Zhttp::H2::Field field) {
	  new (fields.push()) OwnedField{
	    Zhttp::H2::HPackString{field.name},
	    Zhttp::H2::HPackString{field.value}
	  };
	}) < 0)
      return false;
  return decoder.finish();
}

bool field(const Fields &fields, unsigned i, ZuCSpan name, ZuCSpan value)
{
  return i < fields.length() &&
    fields[i].name == name && fields[i].value == value;
}

void testStaticTable()
{
  ZuTestScope(testStaticTable);

  bool complete = true;
  for (unsigned i = 1; i <= 61; ++i) {
    Zhttp::H2::Field value;
    if (!Zhttp::H2::HPack::staticField(i, value) ||
	Zhttp::H2::HPack::staticNameIndex(value.name) <= 0)
      complete = false;
    if (value.value &&
	Zhttp::H2::HPack::staticIndex(value.name, value.value) != int(i))
      complete = false;
  }
  Zhttp::H2::Field value;
  ZuCHECK(complete, "all 61 RFC static entries round trip");
  ZuCHECK(!Zhttp::H2::HPack::staticField(0, value) &&
      !Zhttp::H2::HPack::staticField(62, value),
    "out-of-range static indexes fail");
}

void testRFCRequests()
{
  ZuTestScope(testRFCRequests);

  Zhttp::H2::HPackDecoder decoder;
  Fields fields;
  ZuCHECK(decoder.init(4096, 1U<<16), "initialize HPACK decoder");

  auto first = hex("82 86 84 41 0f 7777772e6578616d706c652e636f6d");
  ZuCHECK(decode(decoder, first, fields) && fields.length() == 4 &&
      field(fields, 0, ":method", "GET") &&
      field(fields, 1, ":scheme", "http") &&
      field(fields, 2, ":path", "/") &&
      field(fields, 3, ":authority", "www.example.com"),
    "RFC C.2.1 request at every byte split");
  ZuCHECK(decoder.table().count() == 1 &&
      decoder.table().used() == 57,
    "RFC C.2.1 dynamic table state");

  auto second = hex("82 86 84 be 58 08 6e6f2d6361636865");
  ZuCHECK(decode(decoder, second, fields) && fields.length() == 5 &&
      field(fields, 3, ":authority", "www.example.com") &&
      field(fields, 4, "cache-control", "no-cache"),
    "RFC C.2.2 request at every byte split");
  ZuCHECK(decoder.table().count() == 2 &&
      decoder.table().used() == 110,
    "RFC C.2.2 dynamic table state");

  auto third = hex(
    "82 87 85 bf 40 0a 637573746f6d2d6b6579 "
    "0c 637573746f6d2d76616c7565");
  ZuCHECK(decode(decoder, third, fields) && fields.length() == 5 &&
      field(fields, 4, "custom-key", "custom-value"),
    "RFC C.2.3 request at every byte split");
  ZuCHECK(decoder.table().count() == 3 &&
      decoder.table().used() == 164,
    "RFC C.2.3 dynamic table state");
  decoder.final();
}

void testHuffmanRequest()
{
  ZuTestScope(testHuffmanRequest);

  Zhttp::H2::HPackDecoder decoder;
  Fields fields;
  decoder.init(4096, 1U<<16);
  auto encoded = hex(
    "82 86 84 41 8c f1e3c2e5f23a6ba0ab90f4ff");
  ZuCHECK(decode(decoder, encoded, fields) && fields.length() == 4 &&
      field(fields, 3, ":authority", "www.example.com"),
    "RFC C.4.1 Huffman request at every byte split");
  encoded = hex("82 86 84 be 58 86 a8eb10649cbf");
  ZuCHECK(decode(decoder, encoded, fields) && fields.length() == 5 &&
      field(fields, 4, "cache-control", "no-cache"),
    "RFC C.4.2 Huffman request at every byte split");
  encoded = hex(
    "82 87 85 bf 40 88 25a849e95ba97d7f "
    "89 25a849e95bb8e8b4bf");
  ZuCHECK(decode(decoder, encoded, fields) && fields.length() == 5 &&
      field(fields, 4, "custom-key", "custom-value"),
    "RFC C.4.3 Huffman request at every byte split");
  decoder.final();
}

bool response(const Fields &fields, ZuCSpan status, ZuCSpan date)
{
  return fields.length() >= 4 &&
    field(fields, 0, ":status", status) &&
    field(fields, 1, "cache-control", "private") &&
    field(fields, 2, "date", date) &&
    field(fields, 3, "location", "https://www.example.com");
}

void testRFCResponses(bool huffman)
{
  ZuTestScope(testRFCResponses);

  Zhttp::H2::HPackDecoder decoder;
  Fields fields;
  decoder.init(256, 1U<<16);
  auto first = huffman ?
    hex("48 82 6402 58 85 aec3771a4b "
	"61 96 d07abe941054d444a8200595040b8166e082a62d1bff "
	"6e 91 9d29ad171863c78f0b97c8e9ae82ae43d3") :
    hex("48 03 333032 58 07 70726976617465 "
	"61 1d 4d6f6e2c203231204f637420323031332032303a31333a323120474d54 "
	"6e 17 68747470733a2f2f7777772e6578616d706c652e636f6d");
  ZuCHECK(decode(decoder, first, fields) &&
      response(fields, "302", "Mon, 21 Oct 2013 20:13:21 GMT"),
    huffman ? "RFC C.6.1 Huffman response" :
      "RFC C.5.1 raw response");
  ZuCHECK(decoder.table().count() == 4 && decoder.table().used() == 222,
    "first response dynamic table state");

  auto second = huffman ?
    hex("48 83 640eff c1 c0 bf") :
    hex("48 03 333037 c1 c0 bf");
  ZuCHECK(decode(decoder, second, fields) &&
      response(fields, "307", "Mon, 21 Oct 2013 20:13:21 GMT"),
    huffman ? "RFC C.6.2 Huffman response" :
      "RFC C.5.2 raw response");
  ZuCHECK(decoder.table().count() == 4 && decoder.table().used() == 222,
    "second response dynamic table state");

  auto third = huffman ?
    hex("88 c1 61 96 d07abe941054d444a8200595040b8166e084a62d1bff "
	"c0 5a 83 9bd9ab "
	"77 ad 94e7821dd7f2e6c7b335dfdfcd5b3960d5af27087f3672c1"
	"ab270fb5291f9587316065c003ed4ee5b1063d5007") :
    hex("88 c1 "
	"61 1d 4d6f6e2c203231204f637420323031332032303a31333a323220474d54 "
	"c0 5a 04 677a6970 "
	"77 38 666f6f3d4153444a4b48514b425a584f5157454f50495541585157454f"
	"49553b206d61782d6167653d333630303b2076657273696f6e3d31");
  ZuCHECK(decode(decoder, third, fields) && fields.length() == 6 &&
      field(fields, 0, ":status", "200") &&
      field(fields, 2, "date", "Mon, 21 Oct 2013 20:13:22 GMT") &&
      field(fields, 4, "content-encoding", "gzip") &&
      field(fields, 5, "set-cookie",
	"foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"),
    huffman ? "RFC C.6.3 Huffman response" :
      "RFC C.5.3 raw response");
  ZuCHECK(decoder.table().count() == 3 && decoder.table().used() == 215,
    "third response bounded eviction state");
  decoder.final();
}

void testRepresentationsAndFailures()
{
  ZuTestScope(testRepresentationsAndFailures);

  Zhttp::H2::HPackDecoder decoder;
  Fields fields;
  decoder.init(128, 256);

  auto legal = hex(
    "3f 21 82 0f 09 03 616263 10 03 782d78 01 79");
  ZuCHECK(decode(decoder, legal, fields) && fields.length() == 3 &&
      field(fields, 0, ":method", "GET") &&
      field(fields, 1, "cache-control", "abc") &&
      field(fields, 2, "x-x", "y"),
    "size update, indexed, non-indexed, and never-indexed literals");

  auto badIndex = hex("80");
  ZuCHECK(!decode(decoder, badIndex, fields) &&
      decoder.failure() == Zhttp::H2::HPackFailure::Index,
    "zero indexed representation fails");

  decoder.final();
  decoder.init(64, 16);
  auto tooLarge = hex("00 01 78 01 79");
  ZuCHECK(!decode(decoder, tooLarge, fields) &&
      decoder.failure() == Zhttp::H2::HPackFailure::HeaderList,
    "header list limit fails deterministically");

  decoder.final();
  decoder.init(64, 256);
  auto truncated = hex("40 03 6162");
  decoder.reset();
  ZuCHECK(decoder.process(
      truncated, [](Zhttp::H2::Field) { }) == 0 && !decoder.finish() &&
      decoder.failure() == Zhttp::H2::HPackFailure::Truncated,
    "truncated literal fails at block completion");
  decoder.final();
}

void testEncoder()
{
  ZuTestScope(testEncoder);

  Zhttp::H2::HPackEncoder encoder;
  Bytes encoded;
  uint8_t methodGET[] = {0x82};
  Zhttp::H2::Field method{":method", "GET"};
  Zhttp::H2::Field authorization{"authorization", "secret"};
  Zhttp::H2::Field cookie{"cookie", "a=b"};
  Zhttp::H2::Field setCookie{"set-cookie", "a=b"};
  encoder.init(4096);
  ZuCHECK(encoder.field(encoded, method) > 0 &&
      encoded == ZuBSpan{methodGET},
    "encoder uses exact static index");
  encoded.length(0);
  ZuCHECK(encoder.field(encoded, authorization) > 0 &&
      (encoded[0] & 0xf0) == 0x10,
    "encoder never-indexes authorization");
  encoded.length(0);
  ZuCHECK(encoder.field(encoded, cookie) > 0 &&
      (encoded[0] & 0xf0) == 0x10,
    "encoder never-indexes cookie");
  encoded.length(0);
  ZuCHECK(encoder.field(encoded, setCookie) > 0 &&
      (encoded[0] & 0xf0) == 0x10,
    "encoder never-indexes set-cookie");
  encoder.final();
}

void testDynamicEncoder()
{
  ZuTestScope(testDynamicEncoder);

  Zhttp::H2::HPackEncoder encoder;
  Zhttp::H2::HPackDecoder decoder;
  Fields fields;
  Bytes encoded;
  ZuCHECK(encoder.init(256) && decoder.init(256, 1U<<16),
    "initialize dynamic HPACK endpoints");
  auto initial = encoder.updates();
  ZuCHECK(initial.count == 1 && initial.first == 256 &&
      encoder.emit(encoded, initial) >= 0 &&
      decode(decoder, encoded, fields) && !fields,
    "local HPACK Tx ceiling did not schedule the initial size update");
  encoder.commit(initial);

  auto emit = [&encoder, &decoder, &fields, &encoded](
      Zhttp::Compression::FieldView field_) {
    encoded.length(0);
    auto plan = encoder.plan(field_);
    unsigned before = encoder.table().count();
    Bytes counted;
    bool planned = encoder.emit(counted, plan) >= 0 &&
      encoder.table().count() == before;
    bool emitted = encoder.emit(encoded, plan) >= 0 &&
      encoded == counted && encoder.table().count() == before;
    if (emitted) encoder.commit(plan);
    return planned && emitted &&
      decode(decoder, encoded, fields);
  };

  ZuCHECK(emit({"x-dyn", "one"}) &&
      fields.length() == 1 && field(fields, 0, "x-dyn", "one") &&
      (encoded[0] & 0xc0) == 0x40 && encoder.table().count() == 1,
    "first HPACK field used incremental indexing exactly once");
  ZuCHECK(emit({"x-dyn", "one"}) &&
      encoded.length() == 1 && encoded[0] == 0xbe &&
      encoder.table().count() == 1,
    "HPACK exact dynamic lookup selected index 62");
  ZuCHECK(emit({"x-dyn", "two"}) &&
      encoded[0] == 0x7e && encoder.table().count() == 2 &&
      field(fields, 0, "x-dyn", "two"),
    "HPACK name-only dynamic lookup selected the newest name");
  auto staticExact = encoder.plan({":method", "GET"});
  auto staticName = encoder.plan({"cache-control", "private"});
  ZuCHECK(staticExact.rep == Zhttp::H2::HPackRep::Indexed &&
      staticExact.index == 2 &&
      staticName.rep == Zhttp::H2::HPackRep::Incremental &&
      staticName.index == 24,
    "HPACK dynamic lookup displaced static exact/name precedence");
  ZuCHECK(emit({"x-split", "a?b"}) &&
      emit({"x-split", "a", '?', "b"}) &&
      (encoded[0] & 0x80) && field(fields, 0, "x-split", "a?b"),
    "segmented HPACK value matched a contiguous dynamic entry");

  unsigned before = encoder.table().count();
  ZuCHECK(emit({"authorization", "secret"}) &&
      emit({"authorization", "secret"}) &&
      (encoded[0] & 0xf0) == 0x10 &&
      encoder.table().count() == before,
    "never-index policy bypassed dynamic lookup and insertion");

  encoder.final();
  decoder.final();

  Zhttp::H2::HPackEncoder disabled;
  ZuCHECK(disabled.init(0), "initialize zero-capacity HPACK encoder");
  auto plan = disabled.plan({"x-zero", "value"});
  encoded.length(0);
  ZuCHECK(plan.rep == Zhttp::H2::HPackRep::NonIndexed &&
      disabled.emit(encoded, plan) >= 0 &&
      !(encoded[0] & 0xf0) && !disabled.table().count(),
    "zero-capacity HPACK did not use non-indexed fallback");
}

void testTxStorageAndUpdates()
{
  ZuTestScope(testTxStorageAndUpdates);

  Zhttp::H2::HPackTxTable table;
  enum { Capacity = 32768, Entries = 300 };
  ZuCHECK(table.init(Capacity) && table.capacity(Capacity),
    "initialize bounded HPACK Tx table");
  unsigned orderSlots = table.orderSlots();
  unsigned exactSlots = table.exactSlots();
  unsigned nameSlots = table.nameSlots();
  bool inserted = true;
  for (unsigned i = 0; i < Entries; ++i) {
    char name[24], value[24];
    snprintf(name, sizeof(name), "x-hpack-%u", i);
    snprintf(value, sizeof(value), "v%u", i);
    if (!table.insert({ZuCSpan{name}, ZuCSpan{value}})) {
      inserted = false;
      break;
    }
  }
  ZuCHECK(inserted && table.orderSlots() == orderSlots &&
      table.exactSlots() == exactSlots &&
      table.nameSlots() == nameSlots &&
      !table.exactResized() && !table.nameResized(),
    "HPACK Tx churn grew or resized bounded storage");

  Zhttp::H2::HPackTxTable names;
  ZuCHECK(names.init(128) && names.capacity(128) &&
      names.insert({"x", "one"}) && names.insert({"x", "two"}) &&
      names.insert({"y", "three"}) && names.insert({"z", "four"}) &&
      !names.find({"x", "one"}) && names.find({"x", "two"}) &&
      names.findName("x") && names.findName("x")->value == "two",
    "HPACK eviction hid the newest duplicate name");

  Zhttp::H2::HPackEncoder encoder;
  Bytes bytes;
  ZuCHECK(encoder.init(8192) && encoder.peerCapacity(0) &&
      encoder.peerCapacity(4096),
    "prepare coalesced HPACK capacity updates");
  auto updates = encoder.updates();
  ZuCHECK(updates.count == 2 && !updates.first &&
      updates.second == 4096 && encoder.emit(bytes, updates) >= 0 &&
      bytes[0] == 0x20,
    "HPACK capacity reduction/increase ordering mismatch");
  encoder.commit(updates);
  ZuCHECK(!encoder.updates().count,
    "HPACK capacity update remained pending after commit");
}

void testFieldView()
{
  ZuTestScope(testFieldView);

  Zhttp::Compression::FieldView contiguous{"x", "a?b"};
  Zhttp::Compression::FieldView segmented{"x", "a", '?', "b"};
  Zhttp::Compression::FieldView empty1{"x", ""};
  Zhttp::Compression::FieldView empty2{"x", "", '\0', ""};
  ZuCHECK(contiguous == segmented &&
      contiguous.hash() == segmented.hash() &&
      contiguous.valueLength() == 3,
    "contiguous and segmented field hashes diverged");
  ZuCHECK(!(empty1 == empty2) && empty1.valueLength() == 0 &&
      empty2.valueLength() == 1,
    "empty segmented field boundaries were conflated");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testStaticTable);
  ZuTestCall(testRFCRequests);
  ZuTestCall(testHuffmanRequest);
  ZuTestCall(testRFCResponses, false);
  ZuTestCall(testRFCResponses, true);
  ZuTestCall(testRepresentationsAndFailures);
  ZuTestCall(testEncoder);
  ZuTestCall(testDynamicEncoder);
  ZuTestCall(testTxStorageAndUpdates);
  ZuTestCall(testFieldView);
}
