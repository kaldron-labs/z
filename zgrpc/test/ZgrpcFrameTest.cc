//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZgrpcFrame.hh>

using namespace ZuTestUtil;
using namespace Zgrpc;
using Pool = Zi::IOBufAlloc<ZiIOBuf, 32, 1U << 20,
  ZuStringT<"ZgrpcFrameTest">>;

struct Payload { ZuBSpan bytes; };
ZfStruct(, (Payload, PB), (bytes, (Ctor<0>, PB::FieldID<1>), Bytes));
Zpb::AsMessage<"test.Payload"> Zpb_Fmt(Payload *);

struct CtorOnly {
  Payload nested;

  explicit CtorOnly(Payload value) : nested{value} { ++constructed; }
  static unsigned constructed;
};
unsigned CtorOnly::constructed = 0;
ZfStruct(, (CtorOnly, PB), (nested, (Ctor<0>, PB::FieldID<1>), UDT));
Zpb::AsMessage<"test.CtorOnly"> Zpb_Fmt(CtorOnly *);

struct ChoiceNum { uint64_t value; };
ZfStruct(, (ChoiceNum, PB), (value, (Ctor<0>, PB::FieldID<1>), UInt64));
Zpb::AsMessage<"test.ChoiceNum"> Zpb_Fmt(ChoiceNum *);
ZuDerive(RichChoice, (ZuUnion<void, ChoiceNum, Payload>));
Zpb::AsUnion<"test.Rich.choice", Zpb::Alt<"number", 17, ChoiceNum>,
  Zpb::Alt<"bytes", 19, Payload>> Zpb_Fmt(RichChoice *);
ZuDerive(RichNums, (ZtArray<uint32_t, ZtArrayHeapID<"ZgrpcFrameTest.Nums">>));
ZuDerive(RichMap, (ZmRBTreeKV<uint32_t, ZuBSpan, ZmRBTreeHeapID<"ZgrpcFrameTest.Map">>));
Zpb::AsMap<"test.Rich.map", ZfFieldTC::UInt32, ZfFieldTC::Bytes> Zpb_Fmt(RichMap *);
struct Rich {
  uint64_t id;
  Payload nested;
  RichNums nums;
  RichMap map;
  RichChoice choice;
};
// Deliberately distinct declaration order, constructor positions and wire IDs.
ZfStruct(, (Rich, PB),
  (id, (Ctor<0>, PB::FieldID<20>), UInt64),
  (nested, (Ctor<1>, PB::FieldID<1>), UDT),
  (nums, (Ctor<2>, PB::FieldID<7>), UInt32Vec),
  (map, (Ctor<3>, PB::FieldID<11>), UDT),
  (choice, (Ctor<4>), UDT));
Zpb::AsMessage<"test.Rich"> Zpb_Fmt(Rich *);

struct RichRPC : Method {
  using Service = ZuStringT<"test.Rich">;
  using Name = ZuStringT<"Echo">;
  using Request = Rich;
  using Response = Rich;
  enum { ReqBorrowed = 1, ResBorrowed = 1 };
};
ZuAssert((MethodValid<RichRPC>{}));

static void splits()
{
  ZuTestScopeRT(splits);
  const uint8_t wire[] = {0, 0, 0, 0, 3, 'a', 'b', 'c'};
  for (unsigned split = 0; split <= sizeof(wire); split++) {
    FrameRx rx;
    Frame frame;
    unsigned used = 0;
    auto result = rx.feed({wire, split}, nullptr, used, frame);
    ZuCheckRT(used == split);
    if (split < sizeof(wire)) {
      ZuCheckRT(result == FrameResult::More);
      result = rx.feed({wire + split, sizeof(wire) - split}, nullptr, used, frame);
      ZuCheckRT(used == sizeof(wire) - split);
    }
    ZuCheckRT(result == FrameResult::Message && frame.bytes == ZuBSpan{"abc"});
    ZuCheckRT(frame.backing && rx.end());
  }
  // Feed every byte independently, then reuse the input before inspecting output.
  FrameRx rx;
  Frame frame;
  for (unsigned i = 0; i < sizeof(wire); i++) {
    uint8_t c = wire[i];
    unsigned used;
    auto result = rx.feed({&c, 1}, nullptr, used, frame);
    c = 0xff;
    ZuCheckRT(used == 1 && result ==
      (i + 1 == sizeof(wire) ? FrameResult::Message : FrameResult::More));
  }
  ZuCheckRT(frame.bytes == ZuBSpan{"abc"} && rx.end());
}

static void coalesced()
{
  ZuTestScopeRT(coalesced);
  const uint8_t wire[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 'x', 0, 0, 0, 0, 0};
  FrameRx rx;
  unsigned offset = 0;
  for (unsigned i = 0; i < 3; i++) {
    Frame frame;
    unsigned used;
    ZuCheckRT(rx.feed({wire + offset, sizeof(wire) - offset}, nullptr, used, frame) ==
      FrameResult::Message);
    offset += used;
    ZuCheckRT(frame.bytes.length() == (i == 1 ? 1 : 0));
  }
  ZuCheckRT(offset == sizeof(wire) && rx.end());
}

static void errors()
{
  ZuTestScopeRT(errors);
  const uint8_t wire[] = {0, 0, 0, 0, 3, 'a', 'b', 'c'};
  for (unsigned n = 1; n < sizeof(wire); n++) {
    FrameRx rx;
    Frame frame;
    unsigned used;
    rx.feed({wire, n}, nullptr, used, frame);
    ZuCheckRT(!rx.end() && rx.error() == FrameError::Truncated);
    ZuCheckRT(rx.feed(wire, nullptr, used, frame) == FrameResult::Failed && !used);
    rx.reset(3);
    ZuCheckRT(rx.feed(wire, nullptr, used, frame) == FrameResult::Message && rx.end());
  }
  const uint8_t flags[] = {1, 2, 255};
  for (auto flag : flags) {
    uint8_t prefix[] = {flag, 0, 0, 0, 0};
    FrameRx rx;
    Frame frame;
    unsigned used;
    ZuCheckRT(rx.feed(prefix, nullptr, used, frame) == FrameResult::Failed);
    ZuCheckRT(rx.error() == (flag == 1 ? FrameError::Compression : FrameError::Flag));
    ZuCheckRT(!frame.backing);
  }
  FrameRx rx{2};
  Frame frame;
  unsigned used;
  ZuCheckRT(rx.feed(wire, nullptr, used, frame) == FrameResult::Failed && used == 5);
  ZuCheckRT(rx.error() == FrameError::Limit && !frame.backing);
  rx.reset(UINT_MAX);
  const uint8_t huge[] = {0, 255, 255, 255, 255};
  ZuCheckRT(rx.feed(huge, nullptr, used, frame) == FrameResult::Failed && used == 5);
  ZuCheckRT(rx.error() == FrameError::Limit);
}

static void budgets()
{
  ZuTestScopeRT(budgets);
  const uint8_t wire[] = {0, 0, 0, 0, 3, 'a', 'b', 'c'};
  FrameRx rx;
  Frame frame;
  unsigned used, offset = 0;
  ZuCheckRT(rx.feed(wire, nullptr, used, frame, 0) == FrameResult::More && !used);
  while (offset < sizeof(wire)) {
    auto result = rx.feed({wire + offset, sizeof(wire) - offset}, nullptr, used, frame, 2);
    ZuCheckRT(used <= 2 && used > 0);
    offset += used;
    ZuCheckRT(result == (offset == sizeof(wire) ? FrameResult::Message : FrameResult::More));
  }
  ZuCheckRT(frame.bytes == ZuBSpan{"abc"} && rx.end());
}

static void buffers()
{
  ZuTestScope(buffers);
  ZmRef<ZiIOBuf> buf = new Pool{nullptr, 0};
  const uint8_t wire[] = {0, 0, 0, 0, 3, 'a', 'b', 'c'};
  memcpy(buf->data(), wire, sizeof(wire));
  buf->length = sizeof(wire);
  FrameRx rx;
  Frame frame;
  unsigned used;
  ZuCheck(rx.feed(buf->cspan(), buf, used, frame) == FrameResult::Message);
  ZuCheck(frame.backing.ptr() == buf.ptr() && frame.bytes.data() == buf->data() + 5);
  buf = nullptr;
  ZuCheck(frame.bytes == ZuBSpan{"abc"});
}

static void protobuf()
{
  ZuTestScopeRT(protobuf);
  for (auto size : {0U, 1U, 300U, 70000U}) {
    Bytes data;
    data.length(size);
    if (size) memset(data.data(), 0x71, size);
    ZmRef<ZiIOBuf> initial = new Pool{nullptr, 0};
    initial->skip = 8;
    FrameError::T error;
    auto identity = initial.ptr();
    auto buf = encodeFrame(initial, 4, 100000, Payload{data}, error);
    ZuCheckRT(buf.ptr() == identity);
    ZuCheckRT(buf && error == FrameError::OK && buf->skip >= 8 && buf->avail() >= 4);
    FrameRx rx;
    Frame frame;
    unsigned used;
    ZuCheckRT(rx.feed(buf->cspan(), buf, used, frame) == FrameResult::Message);
    ZuCheckRT(Zpb::verify<Payload>(frame.bytes));
    auto object = Zpb::handler<Payload>(frame.bytes).ctor();
    ZuCheckRT(object.bytes == ZuBSpan{data});
  }
  FrameError::T error;
  auto empty = encodeFrame(ZmRef<ZiIOBuf>{new Pool{nullptr, 0}}, 0, 0, Payload{}, error);
  ZuCheckRT(empty && empty->length == PrefixSize && error == FrameError::OK);
  ZmRef<ZiIOBuf> tiny = new Pool{nullptr, 0};
  tiny->skip = tiny->size - 2;
  auto identity = tiny.ptr();
  auto grown = encodeFrame(ZuMv(tiny), 0, 0, Payload{}, error);
  ZuCheckRT(grown && grown.ptr() == identity && grown->length == PrefixSize);
  ZuCheckRT(grown->skip + grown->length <= grown->size && grown->jumbo());
  ZuCheckRT(error == FrameError::OK);
  auto fail = encodeFrame(ZmRef<ZiIOBuf>{new Pool{nullptr, 0}}, 0, 1, Payload{{"abc"}}, error);
  ZuCheckRT(!fail && error == FrameError::Limit);
  auto overflow = encodeFrame(ZmRef<ZiIOBuf>{new Pool{nullptr, 0}}, UINT_MAX, UINT_MAX,
    Payload{}, error);
  ZuCheckRT(!overflow && error == FrameError::Limit);
}

static void decoding()
{
  ZuTestScope(decoding);
  FrameError::T error;
  auto encoded = encodeFrame(ZmRef<ZiIOBuf>{new Pool{nullptr, 0}}, 0, 100,
    CtorOnly{Payload{{"borrowed"}}}, error);
  FrameRx rx;
  Frame frame;
  unsigned used;
  ZuCheck(rx.feed(encoded->cspan(), encoded, used, frame) == FrameResult::Message);
  auto decoded = decodeFrame<CtorOnly, ZuFacet::PB, true>(frame);
  ZuCheck(decoded.is<Message<CtorOnly>>());
  auto message = ZuMv(decoded.p<Message<CtorOnly>>());
  ZuCheck(message.backing.ptr() == encoded.ptr());
  encoded = nullptr;
  frame = {};
  ZuCheck(message.object().nested.bytes == ZuBSpan{"borrowed"});
  auto before = CtorOnly::constructed;
  const uint8_t malformed[] = {0x0a, 0x80};
  auto rejected = decodeFrame<CtorOnly>(Frame{malformed, nullptr});
  ZuCheck(rejected.is<void>() && before == CtorOnly::constructed);
}

static void rich()
{
  ZuTestScopeRT(rich);
  // Independent protobuf bytes: unknown varint and delimited fields, packed
  // integers, a borrowed map value, a nested message, and a oneof alternative.
  const uint8_t wire[]{
    0xa0, 0x06, 0x7f,
    0xa0, 0x01, 0x2a,
    0x3a, 0x03, 0x01, 0x02, 0x03,
    0x5a, 0x07, 0x08, 0x05, 0x12, 0x03, 'm', 'a', 'p',
    0x0a, 0x08, 0x0a, 0x06, 'n', 'e', 's', 't', 'e', 'd',
    0x8a, 0x01, 0x02, 0x08, 0x09,
    0xaa, 0x06, 0x02, 0xff, 0x00};
  const uint8_t last[]{0x9a, 0x01, 0x05, 0x0a, 0x03, 'a', 'l', 't'};
  for (bool changed : {false, true}) {
    auto check = [changed](const Rich &value) {
      ZuCheckRT(value.id == 42 && value.nested.bytes == ZuBSpan{"nested"});
      ZuCheckRT(value.nums.length() == 3 && value.nums[0] == 1 &&
	value.nums[1] == 2 && value.nums[2] == 3);
      auto entry = value.map.find(5);
      ZuCheckRT(value.map.count_() == 1 && entry, (return));
      ZuCheckRT(entry->val() == ZuBSpan{"map"});
      if (changed) {
	ZuCheckRT(value.choice.is<Payload>(), (return));
	ZuCheckRT(value.choice.p<Payload>().bytes == ZuBSpan{"alt"});
      } else {
	ZuCheckRT(value.choice.is<ChoiceNum>(), (return));
	ZuCheckRT(value.choice.p<ChoiceNum>().value == 9);
      }
    };
    auto size = sizeof(wire) + (changed ? sizeof(last) : 0);
    ZmRef<ZiIOBuf> input = new Pool{nullptr, 0};
    ZuCheckRT(input->ensure(PrefixSize + size), (return));
    framePrefix(input->data(), size);
    memcpy(input->data() + PrefixSize, wire, sizeof(wire));
    if (changed) memcpy(input->data() + PrefixSize + sizeof(wire), last, sizeof(last));
    input->length = PrefixSize + size;
    FrameRx rx;
    Frame frame;
    unsigned used;
    ZuCheckRT(rx.feed(input->cspan(), input, used, frame) == FrameResult::Message && rx.end());
    auto decoded = decodeFrame<RichRPC::Request,
      RichRPC::ReqFacet, RichRPC::ReqBorrowed>(frame);
    ZuCheckRT(decoded.is<Message<Rich>>(), (return));
    auto message = ZuMv(decoded.p<Message<Rich>>());
    ZuCheckRT(message.backing.ptr() == input.ptr());
    input = nullptr;
    frame = {};
    check(message.object()); // Nested/container views survive receive retirement.
    FrameError::T error;
    auto output = encodeFrame(ZmRef<ZiIOBuf>{new Pool{nullptr, 0}}, 0, 1024, message.object(), error);
    ZuCheckRT(output && error == FrameError::OK, (return));
    ZuCheckRT(rx.feed(output->cspan(), output, used, frame) == FrameResult::Message && rx.end());
    auto round = decodeFrame<RichRPC::Response,
      RichRPC::ResFacet, RichRPC::ResBorrowed>(frame);
    ZuCheckRT(round.is<Message<Rich>>(), (return));
    auto response = ZuMv(round.p<Message<Rich>>());
    ZuCheckRT(response.backing.ptr() == output.ptr());
    output = nullptr;
    frame = {};
    check(response.object());
  }
}

static void fragmentedOwner()
{
  ZuTestScope(fragmentedOwner);
  ZmRef<ZiIOBuf> first = new Pool{nullptr, 0};
  uint8_t prefix[PrefixSize];
  framePrefix(prefix, 6);
  first->append(ZuBSpan{prefix});
  first->append(ZuBSpan{"ab"});
  auto identity = first.ptr();
  auto input = first->cspan();
  FrameRx parser;
  Frame out;
  unsigned used;
  ZuCheck(parser.feed(input, ZuMv(first), used, out, UINT_MAX, true) == FrameResult::More);
  ZuCheck(used == PrefixSize + 2 && !out.backing);
  ZuCheck(parser.feed(ZuBSpan{"cdef"}, {}, used, out) == FrameResult::Message);
  ZuCheck(out.backing.ptr() == identity && out.bytes == ZuBSpan{"abcdef"});
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(splits);
  ZuTestCall(coalesced);
  ZuTestCall(errors);
  ZuTestCall(budgets);
  ZuTestCall(buffers);
  ZuTestCall(fragmentedOwner);
  ZuTestCall(protobuf);
  ZuTestCall(decoding);
  ZuTestCall(rich);
}
