//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZhttpQPack.hh>

using namespace ZuTestUtil;

static ZuCSpan span(const Zhttp::H3::HeaderBytes &bytes)
{
  return ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()};
}

void testRxTable()
{
  ZuTestScope(testRxTable);

  Zhttp::H3::QPackRxTable table;
  table.maxCapacityBytes_ = 128;
  ZuCHECK(table.setCapacity(128), "rx capacity setup failed");
  ZuCHECK(table.insert({":authority", "www.example.com"}) &&
      table.insert({":path", "/sample/path"}) &&
      table.insertCount() == 2 && table.count() == 2,
    "rx insert failed");

  Zhttp::H3::Header h;
  ZuCHECK(table.lookupAbs(0, h) &&
      h.name == ":authority" && h.value == "www.example.com",
    "rx absolute lookup failed");
  ZuCHECK(table.lookupRelative(table.insertCount(), 0, h) &&
      h.name == ":path" && h.value == "/sample/path",
    "rx relative lookup failed");
  ZuCHECK(table.lookupPostBase(0, 1, h) &&
      h.name == ":path" && h.value == "/sample/path",
    "rx post-base lookup failed");

  ZuCHECK(table.duplicate(0) && table.insertCount() == 3,
    "rx duplicate failed");
  table.setCapacity(48);
  ZuCHECK(table.used() <= table.capacity(), "rx eviction failed");
  ZuCHECK(!table.lookupAbs(0, h), "rx lookup ignored eviction");
  auto count = table.insertCount();
  ZuCHECK(!table.insert({"x-large", "012345678901234567890123456789"}) &&
      table.insertCount() == count,
    "rx oversized insert changed state");
}

void testDynamicFieldSectionDecode()
{
  ZuTestScope(testDynamicFieldSectionDecode);

  Zhttp::H3::QPackRxTable table;
  table.maxCapacityBytes_ = 256;
  table.setCapacity(256);
  table.insert({":authority", "www.example.com"});
  table.insert({":path", "/sample/path"});

  Zhttp::H3::FieldSectionPrefix prefix;
  prefix.requiredInsertCount = table.insertCount();
  prefix.base = table.insertCount();

  Zhttp::H3::HeaderBytes bytes;
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(
      bytes, prefix, table.maxCapacity()) > 0,
    "dynamic relative prefix encode failed");
  bytes.push(0x80);
  bytes.push(0x81);
  unsigned seen = 0;
  bool sawPath = false, sawAuthority = false;
  ZuCHECK(Zhttp::H3::decodeLiteralDynamic(
      span(bytes), table,
      [&](Zhttp::H3::Header h) {
	if (seen == 0 && h.name == ":path" && h.value == "/sample/path")
	  sawPath = true;
	if (seen == 1 && h.name == ":authority" &&
	    h.value == "www.example.com")
	  sawAuthority = true;
	++seen;
      }) == int(bytes.length()) &&
      seen == 2 && sawPath && sawAuthority,
    "dynamic relative indexed field decode failed");

  bytes.length(0);
  prefix.base = 0;
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(
      bytes, prefix, table.maxCapacity()) > 0,
    "dynamic post-base prefix encode failed");
  bytes.push(0x10);
  bytes.push(0x11);
  seen = 0;
  sawPath = sawAuthority = false;
  ZuCHECK(Zhttp::H3::decodeLiteralDynamic(
      span(bytes), table,
      [&](Zhttp::H3::Header h) {
	if (seen == 0 && h.name == ":authority" &&
	    h.value == "www.example.com")
	  sawAuthority = true;
	if (seen == 1 && h.name == ":path" && h.value == "/sample/path")
	  sawPath = true;
	++seen;
      }) == int(bytes.length()) &&
      seen == 2 && sawPath && sawAuthority,
    "dynamic post-base indexed field decode failed");
}

void testTxTable()
{
  ZuTestScope(testTxTable);

  Zhttp::H3::QPackTxTable table;
  ZuCHECK(table.setMaxCapacity(128) && table.setCapacity(128),
    "tx capacity setup failed");
  uint64_t abs = uint64_t(-1);
  ZuCHECK(table.insert({"accept", "application/json"}, &abs) && !abs,
    "tx insert failed");
  ZuCHECK(table.find("accept", "application/json") &&
      table.find(
	ZuCSpan{"accept"}, ZuCSpan{"application/json"})->abs == abs,
    "tx exact lookup by temporary spans failed");
  table.insertCountIncrement(1);
  Zhttp::H3::Header h;
  ZuCHECK(table.lookupAbs(0, h) &&
      h.name == "accept" && h.value == "application/json",
    "tx absolute lookup failed");
  ZuCHECK(table.insert({"server", "z"}, &abs) && abs == 1,
    "tx second insert failed");
  table.setCapacity(48);
  ZuCHECK(table.used() <= table.capacity(), "tx eviction failed");
}

void testInstructionEncoding()
{
  ZuTestScope(testInstructionEncoding);

  Zhttp::H3::HeaderBytes bytes;
  Zhttp::H3::QPackDecodedInstruction decoded;

  ZuCHECK(Zhttp::H3::QPack::encodeSetCapacity(bytes, 10) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x2a &&
      Zhttp::H3::QPack::decodeEncoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::SetCapacity &&
      decoded.value == 10,
    "set capacity instruction mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeInsertWithNameRef(
      bytes, 46, false, "txt") > 0 &&
      bytes.length() == 5 && bytes[0] == 0xee && bytes[1] == 3 &&
      Zhttp::H3::QPack::decodeEncoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::InsertWithNameRef &&
      !decoded.nameRefDynamic && decoded.value == 46 &&
      decoded.header.value == "txt",
    "insert with static name reference mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeInsertLiteral(
      bytes, {"accept", "json"}) > 0 &&
      bytes[0] == 0x46 &&
      Zhttp::H3::QPack::decodeEncoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::InsertWithoutNameRef &&
      decoded.header.name == "accept" &&
      decoded.header.value == "json",
    "insert literal mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeDuplicate(bytes, 0) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x00 &&
      Zhttp::H3::QPack::decodeEncoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::Duplicate &&
      !decoded.value,
    "duplicate instruction mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeSectionAck(bytes, 4) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x84 &&
      Zhttp::H3::QPack::decodeDecoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::SectionAck &&
      decoded.value == 4,
    "section ack instruction mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeStreamCancellation(bytes, 4) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x44 &&
      Zhttp::H3::QPack::decodeDecoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::StreamCancellation &&
      decoded.value == 4,
    "stream cancellation instruction mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeInsertCountIncrement(bytes, 1) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x01 &&
      Zhttp::H3::QPack::decodeDecoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::InsertCountIncrement &&
      decoded.value == 1,
    "insert count increment instruction mismatch");
  ZuCHECK(Zhttp::H3::QPack::encodeInsertCountIncrement(bytes, 0) < 0,
    "zero insert count increment was encoded");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRxTable);
  ZuTestCall(testDynamicFieldSectionDecode);
  ZuTestCall(testTxTable);
  ZuTestCall(testInstructionEncoding);
}
