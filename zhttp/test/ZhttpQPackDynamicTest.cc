//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZhttpQPack.hh>

using namespace ZuTestUtil;

static void putValue_(Zhttp::H3::HeaderBytes &out, ZuCSpan value)
{
  out.push(uint8_t(value.length()));
  for (unsigned i = 0; i < value.length(); ++i)
    out.push(uint8_t(value[i]));
}

void testDynamicTablePolicy()
{
  ZuTestScope(testDynamicTablePolicy);

  Zhttp::H3::Params params;
  params.qpackTableCapacity(128).qpackIndex("accept").qpackNeverIndex("cookie");
  ZuCHECK(params.indexAllowed("accept"), "QPACK index allowlist failed");
  ZuCHECK(!params.indexAllowed("cookie") && params.neverIndex("cookie"),
    "QPACK never-index policy failed");

  Zhttp::H3::DynamicTable table{128};
  ZuCHECK(table.insert({"accept", "application/json"}),
    "dynamic insert failed");
  ZuCHECK(table.insertCount() == 1, "dynamic insert count mismatch");
  ZuCHECK(table.find("accept", "application/json") == 1,
    "dynamic find failed");
  ZuCHECK(table.duplicate(1), "dynamic duplicate failed");
  ZuCHECK(table.count() == 2 && table.insertCount() == 2,
    "dynamic duplicate count mismatch");
  Zhttp::H3::Header indexed;
  ZuCHECK(table.entryAbsolute(0, indexed) &&
      indexed.name == "accept" &&
      indexed.value == "application/json" &&
      table.entryAbsolute(1, indexed) &&
      indexed.name == "accept" &&
      indexed.value == "application/json",
    "dynamic absolute index lookup failed");
  ZuCHECK(table.entryRelative(table.insertCount(), 0, indexed) &&
      indexed.name == "accept" &&
      table.entryRelative(table.insertCount(), 1, indexed) &&
      indexed.name == "accept" &&
      table.entryPostBase(0, 0, indexed) &&
      indexed.name == "accept" &&
      table.entryPostBase(0, 1, indexed) &&
      indexed.name == "accept",
    "dynamic relative/post-base lookup failed");
  table.capacity(48);
  ZuCHECK(table.used() <= table.capacity(), "dynamic eviction failed");
  ZuCHECK(!table.entryAbsolute(0, indexed),
    "dynamic absolute lookup ignored eviction");
  unsigned insertCount = table.insertCount();
  ZuCHECK(!table.insert({"x-large", "012345678901234567890123456789"}) &&
      table.used() <= table.capacity() &&
      table.insertCount() == insertCount,
    "dynamic memory cap failure mismatch");

  Zhttp::H3::Header cookie[] = { { "cookie", "sensitive" } };
  Zhttp::H3::HeaderBytes bytes;
  Zhttp::H3::Params zeroCapacity;
  zeroCapacity.qpackTableCapacity(0).qpackIndex("cookie");
  ZuCHECK(Zhttp::H3::QPack::encodeLiteral(
      bytes, ZuSpan<Zhttp::H3::Header>{cookie, 1}, zeroCapacity) > 0,
    "zero-capacity literal cookie was rejected");

  Zhttp::H3::Params badPolicy;
  badPolicy.qpackTableCapacity(128).qpackIndex("cookie");
  ZuCHECK(Zhttp::H3::QPack::encodeLiteral(
      bytes, ZuSpan<Zhttp::H3::Header>{cookie, 1}, badPolicy) < 0,
    "sensitive indexed QPACK field was accepted");
}

void testDynamicFieldSectionDecode()
{
  ZuTestScope(testDynamicFieldSectionDecode);

  Zhttp::H3::DynamicTable table{256};
  ZuCHECK(table.insert({":authority", "www.example.com"}) &&
      table.insert({":path", "/sample/path"}) &&
      table.insertCount() == 2,
    "dynamic field section table setup failed");

  Zhttp::H3::FieldSectionPrefix prefix;
  prefix.requiredInsertCount = table.insertCount();
  prefix.base = 0;
  prefix.baseNegative = false;

  Zhttp::H3::HeaderBytes bytes;
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(bytes, prefix) > 0,
    "dynamic relative prefix encode failed");
  bytes.push(0x80);
  bytes.push(0x81);
  unsigned seen = 0;
  bool sawPath = false, sawAuthority = false;
  ZuCHECK(Zhttp::H3::decodeLiteralDynamic(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      table,
      [&](Zhttp::H3::Header h) {
	if (seen == 0 && h.name == ":path" && h.value == "/sample/path")
	  sawPath = true;
	if (seen == 1 && h.name == ":authority" &&
	    h.value == "www.example.com")
	  sawAuthority = true;
	++seen;
      },
      {}, table.insertCount()) == int(bytes.length()) &&
      seen == 2 && sawPath && sawAuthority,
    "dynamic relative indexed field decode failed");

  bytes.length(0);
  prefix.base = table.insertCount();
  prefix.baseNegative = true;
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(bytes, prefix) > 0,
    "dynamic post-base prefix encode failed");
  bytes.push(0x10);
  bytes.push(0x11);
  seen = 0;
  sawPath = sawAuthority = false;
  ZuCHECK(Zhttp::H3::decodeLiteralDynamic(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      table,
      [&](Zhttp::H3::Header h) {
	if (seen == 0 && h.name == ":authority" &&
	    h.value == "www.example.com")
	  sawAuthority = true;
	if (seen == 1 && h.name == ":path" && h.value == "/sample/path")
	  sawPath = true;
	++seen;
      },
      {}, table.insertCount()) == int(bytes.length()) &&
      seen == 2 && sawPath && sawAuthority,
    "dynamic post-base indexed field decode failed");

  bytes.length(0);
  prefix.base = 0;
  prefix.baseNegative = false;
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(bytes, prefix) > 0,
    "dynamic name-reference prefix encode failed");
  bytes.push(0x40);
  putValue_(bytes, "/other");
  seen = 0;
  sawPath = false;
  ZuCHECK(Zhttp::H3::decodeLiteralDynamic(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      table,
      [&](Zhttp::H3::Header h) {
	if (h.name == ":path" && h.value == "/other") sawPath = true;
	++seen;
      },
      {}, table.insertCount()) == int(bytes.length()) &&
      seen == 1 && sawPath,
    "dynamic literal name-reference decode failed");
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      [](Zhttp::H3::Header) { },
      {}, table.insertCount()) < 0,
    "stateless QPACK decoder accepted dynamic reference");

  bytes.length(0);
  prefix.base = table.insertCount();
  prefix.baseNegative = true;
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(bytes, prefix) > 0,
    "dynamic post-base name-reference prefix encode failed");
  bytes.push(0x00);
  putValue_(bytes, "api.example");
  seen = 0;
  sawAuthority = false;
  ZuCHECK(Zhttp::H3::decodeLiteralDynamic(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      table,
      [&](Zhttp::H3::Header h) {
	if (h.name == ":authority" && h.value == "api.example")
	  sawAuthority = true;
	++seen;
      },
      {}, table.insertCount()) == int(bytes.length()) &&
      seen == 1 && sawAuthority,
    "dynamic post-base name-reference decode failed");
}

void testDynamicState()
{
  ZuTestScope(testDynamicState);

  Zhttp::H3::DynamicState state{128, 1};
  ZuCHECK(state.maxCapacity() == 128 &&
      state.capacity(64) &&
      state.table().capacity() == 64 &&
      !state.capacity(129) &&
      state.table().capacity() == 64 &&
      state.capacity(128),
    "dynamic capacity bounds mismatch");
  ZuCHECK(state.block(1, 1) && state.blockedCount() == 1,
    "blocked stream was not tracked");
  ZuCHECK(!state.block(2, 1), "blocked stream limit ignored");
  ZuCHECK(state.insert({"accept", "application/json"}) &&
    state.unblockReady() == 1 && !state.blockedCount(),
    "blocked stream was not released by insert");

  ZuCHECK(state.insert({"server", "z"}) && state.insertCount() == 2,
    "second insert failed");
  ZuCHECK(state.insertCountIncrement(1) &&
    state.knownReceivedCount() == 1 &&
    !state.insertCountIncrement(2),
    "insert count increment bounds mismatch");
  ZuCHECK(state.block(3, 3) && state.blockedCount() == 1,
    "second blocked stream was not tracked");
  ZuCHECK(state.streamCancellation(3) && state.cancellations() == 1 &&
    !state.blockedCount(), "stream cancellation mismatch");
  ZuCHECK(state.sectionAck(4) && state.acknowledgements() == 1,
    "section acknowledgement mismatch");

  Zhttp::H3::DynamicState multi{256, 3};
  ZuCHECK(multi.block(1, 1) &&
      multi.block(5, 2) &&
      multi.block(9, 3) &&
      !multi.block(13, 4) &&
      multi.blockedCount() == 3,
    "multiple blocked stream tracking mismatch");
  ZuCHECK(multi.insert({"accept", "a"}) &&
      multi.unblockReady() == 1 &&
      multi.blockedCount() == 2,
    "first blocked stream unblock mismatch");
  ZuCHECK(multi.insert({"accept", "b"}) &&
      multi.unblockReady() == 1 &&
      multi.blockedCount() == 1,
    "second blocked stream unblock mismatch");
  ZuCHECK(multi.insert({"accept", "c"}) &&
      multi.unblockReady() == 1 &&
      !multi.blockedCount(),
    "last blocked stream unblock mismatch");

  Zhttp::H3::DynamicState update{256, 2};
  ZuCHECK(update.block(1, 2) &&
      update.block(1, 3) &&
      update.blockedCount() == 1 &&
      update.insert({"accept", "a"}) &&
      !update.unblockReady() &&
      update.insert({"accept", "b"}) &&
      !update.unblockReady() &&
      update.insert({"accept", "c"}) &&
      update.unblockReady() == 1,
    "blocked stream required insert count update mismatch");
}

void testInstructionEncoding()
{
  ZuTestScope(testInstructionEncoding);

  Zhttp::H3::HeaderBytes bytes;
  Zhttp::H3::QPackDecodedInstruction decoded;
  Zhttp::H3::DynamicState state{256, 1};

  ZuCHECK(Zhttp::H3::QPack::encodeSetCapacity(bytes, 256) > 0,
    "set capacity instruction failed");
  ZuCHECK(bytes.length() >= 2 &&
      Zhttp::H3::QPack::decodeInstruction(
	ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
	decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::SetCapacity &&
      decoded.value == 256 &&
      state.applyInstruction(decoded) &&
      state.table().capacity() == 256,
    "set capacity instruction decode/apply failed");
  ZuCHECK(Zhttp::H3::QPack::encodeSetCapacity(bytes, 257) > 0 &&
      Zhttp::H3::QPack::decodeInstruction(
	ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
	decoded) == int(bytes.length()) &&
      !state.applyInstruction(decoded) &&
      state.table().capacity() == 256,
    "oversized set capacity instruction was accepted");
  ZuCHECK(Zhttp::H3::QPack::encodeInsertWithNameRef(
      bytes, 46, false, "text/plain") > 0,
    "insert with static name reference instruction failed");
  ZuCHECK(Zhttp::H3::QPack::decodeInstruction(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::InsertWithNameRef &&
      !decoded.nameRefDynamic &&
      decoded.value == 46 &&
      decoded.header.value == "text/plain" &&
      state.applyInstruction(decoded) &&
      state.table().find("content-type", "text/plain") == 1,
    "insert with static name reference decode/apply failed");
  ZuCHECK(Zhttp::H3::QPack::encodeInsertWithNameRef(
      bytes, 1, true, "application/xml") > 0,
    "insert with dynamic name reference instruction failed");
  ZuCHECK(Zhttp::H3::QPack::decodeInstruction(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::InsertWithNameRef &&
      decoded.nameRefDynamic &&
      decoded.value == 1 &&
      decoded.header.value == "application/xml" &&
      state.applyInstruction(decoded) &&
      state.table().find("content-type", "application/xml") == 1,
    "insert with dynamic name reference decode/apply failed");
  ZuCHECK(Zhttp::H3::QPack::encodeInsertLiteral(
    bytes, {"accept", "application/json"}) > 0,
    "insert literal instruction failed");
  ZuCHECK(Zhttp::H3::QPack::decodeInstruction(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::InsertWithoutNameRef &&
      decoded.header.name == "accept" &&
      decoded.header.value == "application/json" &&
      state.applyInstruction(decoded) &&
      state.insertCount() == 3,
    "insert literal instruction decode/apply failed");
  ZuCHECK(Zhttp::H3::QPack::encodeDuplicate(bytes, 1) > 0 &&
      Zhttp::H3::QPack::decodeInstruction(
	ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
	decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::Duplicate &&
      decoded.value == 1 &&
      state.applyInstruction(decoded) &&
      state.insertCount() == 4,
    "duplicate instruction decode/apply failed");
  ZuCHECK(Zhttp::H3::QPack::encodeSectionAck(bytes, 4) > 0,
    "section acknowledgement instruction failed");
  ZuCHECK(Zhttp::H3::QPack::decodeInstruction(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::SectionAck &&
      decoded.value == 4 &&
      state.applyInstruction(decoded) &&
      state.acknowledgements() == 1,
    "section acknowledgement instruction decode/apply failed");
  ZuCHECK(Zhttp::H3::QPack::encodeStreamCancellation(bytes, 4) > 0,
    "stream cancellation instruction failed");
  ZuCHECK(Zhttp::H3::QPack::decodeInstruction(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::StreamCancellation &&
      decoded.value == 4 &&
      state.applyInstruction(decoded),
    "stream cancellation instruction decode/apply failed");
  ZuCHECK(Zhttp::H3::QPack::encodeInsertCountIncrement(bytes, 1) > 0,
    "insert count increment instruction failed");
  ZuCHECK(Zhttp::H3::QPack::decodeInstruction(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::InsertCountIncrement &&
      decoded.value == 1 &&
      state.applyInstruction(decoded) &&
      state.knownReceivedCount() == 1,
    "insert count increment instruction decode/apply failed");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testDynamicTablePolicy);
  ZuTestCall(testDynamicFieldSectionDecode);
  ZuTestCall(testDynamicState);
  ZuTestCall(testInstructionEncoding);
}
