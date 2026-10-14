//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZiPB.hh>

using namespace ZuTestUtil;

ZuDerive(Output, (ZtArray<uint8_t, ZtArrayHeapID<"ZiPBIOTest.Output">>));
using Pool = ZiIOBufAlloc<32, 1024, "ZiPBIOTest.Pool">;

struct Payload { ZuBSpan bytes; };
ZfStruct(, (Payload, PB), (bytes, (PB::FieldID<1>), Bytes));
ZiPB::AsMessage<"test.Payload"> ZiPB_Fmt(Payload *);

struct Envelope { Payload payload; };
ZfStruct(, (Envelope, PB), (payload, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.Envelope"> ZiPB_Fmt(Envelope *);

struct UpdateRow { uint32_t id; ZuCSpan text; };
ZfStruct(, (UpdateRow, PB),
  (id, (PB::FieldID<3>, Mutable),	UInt32),
  (text, (PB::FieldID<9>),		String));
ZiPB::AsMessage<"test.UpdateRow"> ZiPB_Fmt(UpdateRow *);

ZuStructFacet(PrefixedPB);
ZiPBConfig(PrefixedPB, ZiPB_SizePrefixed<>);
ZuStructReflect(UpdateRow, ZuFacet::PrefixedPB, ZuFacet::PB);

static void pooled()
{
  ZuTestScope(pooled);
  Output data;
  data.length(300);
  for (unsigned i = 0; i < data.length(); i++) data[i] = i;
  Envelope object{{data}};

  Output expected;
  ZiPB::ArrayStorage storage{expected};
  ZiPB::Builder reference{storage};
  auto wire = ZiPB::save(reference, object);
  ZuCheck(!reference.failed() && ZiPB::verify<Envelope>(wire));

  ZmRef<ZiIOBuf> owner = new Pool{};
  owner->skip = 8;
  owner->owner = owner.ptr();
  memset(owner->data_(), 0xa5, owner->skip);
  memset(owner->data_() + owner->size - 4, 0x5a, 4);
  unsigned initialSize = owner->size;
  ZiPB::IOBuilder builder{owner, 1024, 4};
  auto pooledWire = ZiPB::save(builder, object);
  ZuCheck(!builder.failed() && builder.capacity() > initialSize);
  ZuCheck(pooledWire.length() == wire.length() &&
    !memcmp(pooledWire.data(), wire.data(), wire.length()));
  auto buf = builder.buf();
  ZuCheck(buf.ptr() == owner.ptr() && buf->owner == owner.ptr());
  ZuCheck(buf->data() == pooledWire.data() && buf->length == wire.length());
  ZuCheck(ZiPB::verify<Envelope>(buf->cspan()));
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
  ZiPB::IOBuilder builder{new Pool{}, 128};
  auto wire = ZiPB::save(builder, object);
  ZuCheck(!wire.length() && builder.failed());
  ZuCheck(builder.error() == ZiPB::Error::Limit);
  ZuCheck(!builder.buf());
  ZuCheck(!ZiPB::save(builder, object).length() &&
    builder.error() == ZiPB::Error::Limit);

  ZiPB::IOBuilder prefixed{new Pool{}, 1024};
  auto framed = ZiPB::save<ZuFacet::PrefixedPB>(prefixed, object);
  ZuCheck((!prefixed.failed() &&
    ZiPB::verify<Envelope, ZuFacet::PrefixedPB>(framed)));
  auto buf = prefixed.buf();
  ZuCheck(buf && buf->length == framed.length() && buf->data() == framed.data());
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
  ZiPB::ArrayStorage storage{expected};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, object);

  SpanStream generic;
  ZuCheck(ZiPB::save(generic, object) == ZiPB::Error::OK);
  ZuCheck(generic.output == wire);
  UpdateRow update{42, "ignored"};
  Output updateOutput;
  ZiPB::ArrayStorage updateStorage{updateOutput};
  ZiPB::Builder updateBuilder{updateStorage};
  auto updateWire = ZiPB::save<ZuFacet::PrefixedPB, ZfFieldFilter::Upd>(
    updateBuilder, update);
  const uint8_t updateBytes[] = {0x02, 0, 0, 0, 0x18, 42};
  ZuCheck(!updateBuilder.failed() && updateWire.length() == sizeof(updateBytes) &&
    !memcmp(updateWire.data(), updateBytes, sizeof(updateBytes)));
  SpanStream updateStream;
  ZuCheck((ZiPB::save<ZuFacet::PrefixedPB, ZfFieldFilter::Upd>(
    updateStream, update)) == ZiPB::Error::OK &&
    updateStream.output == updateWire);
  TxTest updateTx;
  ZuCheck((ZiPB::save<ZuFacet::PrefixedPB, ZfFieldFilter::Upd>(
    updateTx, update)) == ZiPB::Error::OK && !updateTx.failed() &&
    updateTx.output == updateWire);
  Output large;
  large.length(ZiIOBuf_DefltSize + 100);
  for (unsigned i = 0; i < large.length(); i++) large[i] = i ^ (i >> 8);
  Envelope largeObject{{large}};
  SpanStream grown;
  ZuCheck(ZiPB::save(grown, largeObject) == ZiPB::Error::OK);
  ZuCheck(ZiPB::verify<Envelope>(grown.output) &&
    ZiPB::load<Envelope>(grown.output).payload.bytes == large);

  TxTest split;
  ZuCheck(ZiPB::save(split, object) == ZiPB::Error::OK && !split.failed());
  ZuCheck(split.output == wire && split.sends > 1 && split.final);

  Envelope small{{ZuBSpan{"small"}}};
  TxTest direct;
  ZuCheck(ZiPB::save(direct, small) == ZiPB::Error::OK && !direct.failed());
  ZuCheck(direct.sends == 1 && direct.final && direct.allocs == 1);
  ZuCheck(direct.sent && ZiPB::verify<Envelope>(direct.sent->cspan()));

  TxTest reject;
  reject.reject = true;
  ZuCheck(ZiPB::save(reject, small) == ZiPB::Error::OK && reject.failed());
  ZuCheck(!reject.flush() && reject.sends == 0);

  TxTest badSend;
  badSend.sendOK = false;
  ZuCheck(ZiPB::save(badSend, small) == ZiPB::Error::OK && badSend.failed());
  ZuCheck(!badSend.flush() && badSend.sends == 1);
}

int main(int argc, char **argv)
{
  ZuTestMain();
  ZuTestCall(pooled);
  ZuTestCall(limits);
  ZuTestCall(streams);
}
