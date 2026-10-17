//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtArray.hh>
#include <zlib/Zpb.hh>

using namespace ZuTestUtil;

ZuDerive(Output, (ZtArray<uint8_t, ZtArrayHeapID<"ZpbIOTest.Output">>));
using Pool = ZiIOBufAlloc<32, 1024, "ZpbIOTest.Pool">;

struct Payload { ZuBSpan bytes; };
ZfStruct(, (Payload, PB), (bytes, (PB::FieldID<1>), Bytes));
Zpb::AsMessage<"test.Payload"> Zpb_Fmt(Payload *);

struct Envelope { Payload payload; };
ZfStruct(, (Envelope, PB), (payload, (PB::FieldID<1>), UDT));
Zpb::AsMessage<"test.Envelope"> Zpb_Fmt(Envelope *);

struct UpdateRow { uint32_t id; ZuCSpan text; };
ZfStruct(, (UpdateRow, PB),
  (id, (PB::FieldID<3>, Mutable),	UInt32),
  (text, (PB::FieldID<9>),		String));
Zpb::AsMessage<"test.UpdateRow"> Zpb_Fmt(UpdateRow *);

ZuStructFacet(PrefixedPB);
ZpbConfig(PrefixedPB, Zpb_SizePrefixed<>);
ZuStructReflect(UpdateRow, ZuFacet::PrefixedPB, ZuFacet::PB);
ZuStructReflect(Payload, ZuFacet::PrefixedPB, ZuFacet::PB);
ZuStructReflect(Envelope, ZuFacet::PrefixedPB, ZuFacet::PB);

static void pooled()
{
  ZuTestScope(pooled);
  Output data;
  data.length(300);
  for (unsigned i = 0; i < data.length(); i++) data[i] = i;
  Envelope object{{data}};

  Output expected;
  Zpb::ArrayStorage storage{expected};
  Zpb::Builder reference{storage};
  auto wire = Zpb::save(reference, object);
  ZuCheck(!reference.failed() && Zpb::verify<Envelope>(wire));

  ZmRef<ZiIOBuf> owner = new Pool{};
  owner->skip = 8;
  owner->owner = owner.ptr();
  memset(owner->data_(), 0xa5, owner->skip);
  memset(owner->data_() + owner->size - 4, 0x5a, 4);
  unsigned initialSize = owner->size;
  Zpb::IOBuilder builder{owner, 1024, 4};
  auto pooledWire = Zpb::save(builder, object);
  ZuCheck(!builder.failed() && builder.capacity() > initialSize);
  ZuCheck(pooledWire.length() == wire.length() &&
    !memcmp(pooledWire.data(), wire.data(), wire.length()));
  auto buf = builder.buf();
  ZuCheck(buf.ptr() == owner.ptr() && buf->owner == owner.ptr());
  ZuCheck(buf->data() == pooledWire.data() && buf->length == wire.length());
  ZuCheck(Zpb::verify<Envelope>(buf->cspan()));
  ZuCheck(buf->skip >= 8 && buf->avail() >= 4);
  bool head = true, tail = true;
  for (unsigned i = 0; i < 8; i++) head &= buf->data_()[i] == 0xa5;
  for (unsigned i = 0; i < 4; i++)
    tail &= buf->data_()[buf->size - 4 + i] == 0x5a;
  ZuCheck(head && tail);
  ZuCheck(!builder.buf());
}

static void limits()
{
  ZuTestScope(limits);
  Output data;
  data.length(300);
  memset(data.data(), 0x7f, data.length());
  Envelope object{{data}};
  Zpb::IOBuilder builder{new Pool{}, 128};
  auto wire = Zpb::save(builder, object);
  ZuCheck(!wire.length() && builder.failed());
  ZuCheck(builder.error() == Zpb::Error::Limit);
  ZuCheck(!builder.buf());
  ZuCheck(!Zpb::save(builder, object).length() &&
    builder.error() == Zpb::Error::Limit);

  Zpb::IOBuilder prefixed{new Pool{}, 1024};
  auto framed = Zpb::save<ZuFacet::PrefixedPB>(prefixed, object);
  ZuCheck((!prefixed.failed() &&
    Zpb::verify<Envelope, ZuFacet::PrefixedPB>(framed)));
  auto buf = prefixed.buf();
  ZuCheck(buf && buf->length == framed.length() && buf->data() == framed.data());
}

static void delimited()
{
  ZuTestScope(delimited);
  // Payload lengths straddle the one/two/three-byte varint boundaries.
  const struct {
    unsigned length;
    unsigned size;
    ZuBSpan prefix;
  } cases[] = {
    {0, 0, {"\x00", 1}},
    {125, 127, {"\x7f", 1}},
    {126, 128, {"\x80\x01", 2}},
    {16380, 16383, {"\xff\x7f", 2}},
    {16381, 16384, {"\x80\x80\x01", 3}}
  };
  for (const auto &test : cases) {
    Output data;
    data.length(test.length);
    if (test.length) memset(data.data(), 0x5a, test.length);
    Payload object{data};
    unsigned size = test.size + test.prefix.length();
    Output output;
    Zpb::ArrayStorage storage{output};
    Zpb::Builder builder{storage, size};
    auto frame = Zpb::save<ZuFacet::PrefixedPB>(builder, object);
    ZuCheck(!builder.failed() && frame.length() == size);
    ZuCheck(!memcmp(frame.data(), test.prefix.data(), test.prefix.length()));
    ZuBSpan payload;
    ZuCheck(Zpb::Wire::unframe(frame, payload) && payload.length() == test.size);
    ZuCheck((Zpb::verify<Payload, ZuFacet::PrefixedPB>(frame)));
    ZuCheck((Zpb::load<Payload, ZuFacet::PrefixedPB>(frame).bytes == data));
    ZuCheck((!Zpb::verify<Payload, ZuFacet::PrefixedPB>(
      {frame.data(), frame.length() - 1})));

    Output shortOutput;
    Zpb::ArrayStorage shortStorage{shortOutput};
    Zpb::Builder shortBuilder{shortStorage, size - 1};
    auto shortFrame = Zpb::save<ZuFacet::PrefixedPB>(shortBuilder, object);
    ZuCheck(!shortFrame.length() && shortBuilder.error() == Zpb::Error::Limit);
  }
  const ZuBSpan invalid[] = {
    {"", 0},                         // missing prefix
    {"\x80", 1},                     // truncated prefix
    {"\x01", 1},                     // missing payload
    {"\x00\x00", 2},                 // trailing bytes
    {"\x80\x80\x80\x80\x10", 5},     // length exceeds uint32_t
    {"\xff\xff\xff\xff\xff\xff\xff\xff\xff\x02", 10}
  };
  for (auto frame : invalid) {
    ZuBSpan payload;
    ZuCheck(!Zpb::Wire::unframe(frame, payload));
    ZuCheck((!Zpb::verify<Payload, ZuFacet::PrefixedPB>(frame)));
  }
}

struct SpanStream {
  Output output;
  SpanStream &operator <<(ZuBSpan bytes) {
    for (unsigned i = 0; i < bytes.length(); i++) output.push(bytes[i]);
    return *this;
  }
};

struct TxTest : public ZiTxStream<TxTest> {
  using Base = ZiTxStream<TxTest>;
  using Base::operator <<;

  Output output;
  ZmRef<ZiIOBuf> sent;
  unsigned allocs = 0;
  unsigned sends = 0;
  bool reject = false;
  bool sendOK = true;
  bool final = false;

  TxTest() : Base{128, 8, 4} { }
  ~TxTest() { flush(); }
  ZmRef<ZiIOBuf> allocBuf_(unsigned head) {
    ++allocs;
    if (reject) return nullptr;
    ZmRef<ZiIOBuf> buf = new Pool{};
    buf->skip = head;
    return buf;
  }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool flush) {
    ++sends;
    final = flush;
    for (auto byte : buf->cspan()) output.push(byte);
    sent = ZuMv(buf);
    return sendOK;
  }
};
ZuAssert(ZiIsTxStream<TxTest>{});

static void streams()
{
  ZuTestScope(streams);
  Output data;
  data.length(300);
  for (unsigned i = 0; i < data.length(); i++) data[i] = i;
  Envelope object{{data}};
  Output expected;
  Zpb::ArrayStorage storage{expected};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, object);

  SpanStream generic;
  ZuCheck(Zpb::save(generic, object) == Zpb::Error::OK);
  ZuCheck(generic.output == wire);
  UpdateRow update{42, "ignored"};
  Output updateOutput;
  Zpb::ArrayStorage updateStorage{updateOutput};
  Zpb::Builder updateBuilder{updateStorage};
  auto updateWire = Zpb::save<ZuFacet::PrefixedPB, ZfFieldFilter::Upd>(
    updateBuilder, update);
  const uint8_t updateBytes[] = {0x02, 0x18, 42};
  ZuCheck(!updateBuilder.failed() && updateWire.length() == sizeof(updateBytes) &&
    !memcmp(updateWire.data(), updateBytes, sizeof(updateBytes)));
  SpanStream updateStream;
  ZuCheck((Zpb::save<ZuFacet::PrefixedPB, ZfFieldFilter::Upd>(
    updateStream, update)) == Zpb::Error::OK &&
    updateStream.output == updateWire);
  TxTest updateTx;
  ZuCheck((Zpb::save<ZuFacet::PrefixedPB, ZfFieldFilter::Upd>(
    updateTx, update)) == Zpb::Error::OK && !updateTx.failed() &&
    updateTx.output == updateWire);
  Output large;
  large.length(ZiIOBuf_DefltSize + 100);
  for (unsigned i = 0; i < large.length(); i++) large[i] = i ^ (i >> 8);
  Envelope largeObject{{large}};
  SpanStream grown;
  ZuCheck(Zpb::save(grown, largeObject) == Zpb::Error::OK);
  ZuCheck(Zpb::verify<Envelope>(grown.output) &&
    Zpb::load<Envelope>(grown.output).payload.bytes == large);

  TxTest split;
  ZuCheck(Zpb::save(split, object) == Zpb::Error::OK && !split.failed());
  ZuCheck(split.output == wire && split.sends > 1 && split.final);

  Envelope small{{ZuBSpan{"small"}}};
  TxTest direct;
  ZuCheck(Zpb::save(direct, small) == Zpb::Error::OK && !direct.failed());
  ZuCheck(direct.sends == 1 && direct.final && direct.allocs == 1);
  ZuCheck(direct.sent && Zpb::verify<Envelope>(direct.sent->cspan()));

  TxTest reject;
  reject.reject = true;
  ZuCheck(Zpb::save(reject, small) == Zpb::Error::OK && reject.failed());
  ZuCheck(!reject.flush() && reject.sends == 0);

  TxTest badSend;
  badSend.sendOK = false;
  ZuCheck(Zpb::save(badSend, small) == Zpb::Error::OK && badSend.failed());
  ZuCheck(!badSend.flush() && badSend.sends == 1);
}

int main(int argc, char **argv)
{
  ZuTestMain();
  ZuTestCall(pooled);
  ZuTestCall(limits);
  ZuTestCall(delimited);
  ZuTestCall(streams);
}
