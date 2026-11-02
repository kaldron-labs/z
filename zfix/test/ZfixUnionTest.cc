//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfixFmt.hh>

using namespace ZuTestUtil;
using namespace Zfix::Fmt;
using Text = ZtString<ZtStringHeapID<"Zfix.UnionTest">>;

// These alternatives intentionally share a tag. Wire syntax alone must not
// choose a type: application/catalog discrimination selects exactly one.
struct TextMsg {
  Text	value;
};

ZfStruct(, (TextMsg, FIX),
  (value, (FIX::ID<55>, Required),		String));
ZfStructImpl(TextMsg);
ZfStructImpl(TextMsg, FIX);
struct NumMsg {
  int	value = 0;
};

ZfStruct(, (NumMsg, FIX),
  (value, (FIX::ID<55>, Required),		Int32));
ZfStructImpl(NumMsg);
ZfStructImpl(NumMsg, FIX);
struct DataMsg {
  ZuCSpan	data;
};

ZfStruct(, (DataMsg, FIX),
  (data, (FIX::ID<58>), String));
ZfStructImpl(DataMsg);
ZfStructImpl(DataMsg, FIX);

struct Choice : ZmNHeap<Choice>, ZuUnion<void, ParseView, TextMsg, NumMsg, DataMsg> {
  ZuDerive_(Choice, (ZuUnion<void, ParseView, TextMsg, NumMsg, DataMsg>));
friend inline ZuStringT<"Zfix.TestChoice"> ZmNHeapID(Choice *);
friend AsUnion Zfix_Fmt(Choice *);
};

// A plain application union does not have to carry a deferred-view alternative
// when the application selects its type while the handler is alive.
struct Immediate : ZuUnion<void, TextMsg, NumMsg> {
  ZuDerive_(Immediate, (ZuUnion<void, TextMsg, NumMsg>));
friend AsUnion Zfix_Fmt(Immediate *);
};

struct Explicit : ZuUnion<void, ParseView, NumMsg> {
  ZuDerive_(Explicit, (ZuUnion<void, ParseView, NumMsg>));
friend AsUnion Zfix_Fmt(Explicit *);
};

struct ExplicitFmt {
  template <typename U> struct Handler {
    template <typename Out> static void save(Out &out, const U &) {
      out.field(55);
      out << "explicit";
      out.end();
    }
  };

};

ExplicitFmt Zfix_StringFmt(Explicit *);

ZuStructFacet(TextFIX);
ZuStructFacet(NumFIX);
ZuStructFacet(DataFIX);
struct TextConfig : DefltConfig {
  template <typename U> using Union = TextMsg;
};

struct NumConfig : DefltConfig {
  template <typename U> using Union = NumMsg;
};

struct DataConfig : DefltConfig {
  template <typename U> using Union = DataMsg;
};

ZfixConfig(TextFIX, TextConfig);
ZfixConfig(NumFIX, NumConfig);
ZfixConfig(DataFIX, DataConfig);
ZfStructRender(, TextMsg, TextFIX, value);
ZfStructRender(, NumMsg, NumFIX, value);
ZfStructRender(, DataMsg, DataFIX, data);
ZfStructImpl(TextMsg, TextFIX);
ZfStructImpl(NumMsg, NumFIX);
ZfStructImpl(DataMsg, DataFIX);
struct Envelope {
  int		marker = 0;
  Choice	choice;
};

ZfStruct(, (Envelope, FIX),
  (marker, (FIX::ID<1000>),		Int32),
  (choice, ,				UDT));
ZfStructRender(, Envelope, TextFIX, marker, choice);
ZfStructRender(, Envelope, NumFIX, marker, choice);
ZfStructRender(, Envelope, DataFIX, marker, choice);
ZfStructImpl(Envelope);
ZfStructImpl(Envelope, FIX);
ZfStructImpl(Envelope, TextFIX);
ZfStructImpl(Envelope, NumFIX);
ZfStructImpl(Envelope, DataFIX);

static void nested()
{
  ZuTestScope(nested);
  constexpr auto Wire = "1000=7\1" "55=123\1"_z;
  auto text = handler<Envelope, ZuFacet::TextFIX>(Wire);
  auto a = text.ctor();
  ZuCheck(text.valid() && a.marker == 7 && a.choice.is<TextMsg>());
  ZuCheck(a.choice.p<TextMsg>().value == "123");
  auto number = handler<Envelope, ZuFacet::NumFIX>(Wire);
  ZuCheck(number.load(a));
  ZuCheck(a.choice.is<NumMsg>() && a.choice.p<NumMsg>().value == 123);
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save<ZuFacet::NumFIX>(out, a) && out.flush());
  ZuCheck(out.data() == Wire);
  Sink wrong{{bytes, sizeof(bytes)}};
  ZuCheck(!save<ZuFacet::TextFIX>(wrong, a));
  ZuCheck(wrong.error().code == ErrorCode::Unresolved);
  constexpr auto Text = "1000=1\1" "58=ABCD\1"_z;
  auto data = handler<Envelope, ZuFacet::DataFIX>(Text);
  auto b = data.ctor();
  ZuCheck(data.valid() && b.choice.is<DataMsg>());
  ZuCheck(b.choice.p<DataMsg>().data == ZuCSpan{"ABCD"_z});
}

static void selection()
{
  ZuTestScope(selection);
  constexpr auto Wire = "55=123\1"_z;
  auto h = handler<Choice>(Wire);
  auto a = h.ctor();
  ZuCheck(h.valid() && a.is<ParseView>());
  ZuCheck(a.p<ParseView>().input.data() == Wire.data());
  ZuCheck(!resolve<TextMsg>(a));
  ZuCheck(a.is<TextMsg>() && a.p<TextMsg>().value == "123");
  auto b = handler<Choice>(Wire).ctor();
  ZuCheck(!resolve<NumMsg>(b));
  ZuCheck(b.is<NumMsg>() && b.p<NumMsg>().value == 123);
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, b) && out.flush());
  ZuCheck(out.data() == Wire);
  Immediate immediate;
  auto selected = handler<Immediate>(Wire);
  ZuCheck(selected.resolve<NumMsg>(immediate));
  ZuCheck(immediate.is<NumMsg>() && immediate.p<NumMsg>().value == 123);
}

static void lifetime()
{
  ZuTestScope(lifetime);
  char backing[] = "55=old\1";
  Choice c;
  {
    auto h = handler<Choice>(ZuCSpan{backing, sizeof(backing) - 1});
    ZuCheck(h.load(c));
  }
  ZuCheck(c.is<ParseView>() && c.p<ParseView>().input.data() == backing);
  backing[3] = 'n'; backing[4] = 'e'; backing[5] = 'w';
  ZuCheck(!resolve<TextMsg>(c));
  ZuCheck(c.p<TextMsg>().value == "new");
  backing[3] = 'x';
  ZuCheck(c.p<TextMsg>().value == "new");
  constexpr auto TextWire = "58=ABCD\1"_z;
  auto selected = handler<Choice>(TextWire).ctor();
  ZuCheck(!resolve<DataMsg>(selected));
  ZuCheck(selected.p<DataMsg>().data == ZuCSpan{"ABCD"_z});
}

static void errors()
{
  ZuTestScope(errors);
  auto c = handler<Choice>("55=text\1").ctor();
  auto e = resolve<NumMsg>(c);
  ZuCheck(!e && c.is<NumMsg>());
  ZuCheck(resolve<TextMsg>(c).code == ErrorCode::Unresolved);
  auto missing = handler<Choice>("999=unknown\1").ctor();
  ZuCheck(resolve<NumMsg>(missing).code == ErrorCode::Required);
  Limits cfg;
  cfg.frameBytes = 2;
  auto oversized = handler<Choice>("55=123\1", cfg);
  ZuCheck(!oversized.valid() && oversized.error().code == ErrorCode::Size);
  ZuCheck(oversized.ctor().is<void>());
  char bytes[sizeof("55=123\1") - 1];
  auto unresolved = handler<Choice>("55=123\1").ctor();
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(!save(out, unresolved));
  ZuCheck(out.error().code == ErrorCode::Unresolved && !out.length() && !out.flush());
  Choice empty;
  Sink nullOut{{bytes, sizeof(bytes)}};
  ZuCheck(!save(nullOut, empty) && nullOut.error().code == ErrorCode::Unresolved);
}

static void construction()
{
  ZuTestScope(construction);
  auto h = handler<Choice>("55=8\1");
  auto p = h.alloc();
  ZuCheck(p && p->is<ParseView>());
  ZuCheck(!resolve<NumMsg>(*p) && p->p<NumMsg>().value == 8);
  delete p;
  alignas(Choice) unsigned char bytes[sizeof(Choice)];
  ZuCheck(h.new_(bytes));
  auto q = ZuLaunder(reinterpret_cast<Choice *>(bytes));
  ZuCheck(q->is<ParseView>());
  q->~Choice();
  Immediate immediate;
  auto noView = handler<Immediate>("55=8\1");
  ZuCheck(!noView.load(immediate) && noView.error().code == ErrorCode::Unresolved);
  auto explicit_ = handler<Explicit>("raw-but-not-pass-through").ctor();
  constexpr auto Wire = "55=explicit\1"_z;
  char output[Wire.length()];
  Sink out{{output, sizeof(output)}};
  ZuCheck(save(out, explicit_) && out.flush());
  ZuCheck(out.data() == Wire);
}

struct BinaryTextAlt { ZuCSpan data; };
ZfStruct(, (BinaryTextAlt, FIX),
  (data, (FIX::ID<212>, FIX::Binary<213>, Required), String));
struct BinaryBytesAlt { ZuBSpan data; };
ZfStruct(, (BinaryBytesAlt, FIX),
  (data, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes));
struct BinaryOwnedAlt {
  ZtArray<uint8_t, ZtArrayHeapID<"Zfix.UnionBytes">> data;
};
ZfStruct(, (BinaryOwnedAlt, FIX),
  (data, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes));
struct BinaryChoice : ZuUnion<void, ParseView, BinaryTextAlt, BinaryBytesAlt, BinaryOwnedAlt> {
  ZuDerive_(BinaryChoice, (ZuUnion<void, ParseView,
    BinaryTextAlt, BinaryBytesAlt, BinaryOwnedAlt>));
friend AsUnion Zfix_Fmt(BinaryChoice *);
};
ZuStructFacet(BinaryTextFIX);
ZuStructFacet(BinaryBytesFIX);
struct BinaryTextConfig : DefltConfig {
  template <typename U> using Union = BinaryTextAlt;
};
struct BinaryBytesConfig : DefltConfig {
  template <typename U> using Union = BinaryBytesAlt;
};
ZfixConfig(BinaryTextFIX, BinaryTextConfig);
ZfixConfig(BinaryBytesFIX, BinaryBytesConfig);
ZfStructRender(, BinaryTextAlt, BinaryTextFIX, data);
ZfStructRender(, BinaryBytesAlt, BinaryBytesFIX, data);
struct BinaryEnvelope {
  int marker = 0;
  BinaryChoice choice;
  int after = 0;
};
ZfStruct(, (BinaryEnvelope, FIX),
  (marker, (FIX::ID<1000>), Int32),
  (choice, , UDT),
  (after, (FIX::ID<5000>), Int32));
ZfStructRender(, BinaryEnvelope, BinaryTextFIX, marker, choice, after);
ZfStructRender(, BinaryEnvelope, BinaryBytesFIX, marker, choice, after);
static void binarySelection()
{
  ZuTestScope(binarySelection);
  constexpr auto Wire = "1000=7\1" "212=5\1" "213=A\1\0=B\1" "5000=9\1"_z;
  auto text = handler<BinaryEnvelope, ZuFacet::BinaryTextFIX>(Wire);
  auto value = text.ctor();
  ZuCheck(text.valid() && value.marker == 7 && value.after == 9 &&
    value.choice.is<BinaryTextAlt>());
  ZuCheck(value.choice.p<BinaryTextAlt>().data.data() == Wire.data() + 17 &&
    value.choice.p<BinaryTextAlt>().data.length() == 5);
  Indexed<BinaryEnvelope, ZuFacet::BinaryTextFIX> indexed{Wire};
  BinaryEnvelope copy;
  ZuCheck(indexed.load(copy) && copy.choice.is<BinaryTextAlt>());
  ZuCheck(copy.choice.p<BinaryTextAlt>().data.data() ==
    value.choice.p<BinaryTextAlt>().data.data());
  auto bytes = handler<BinaryEnvelope, ZuFacet::BinaryBytesFIX>(Wire);
  ZuCheck(bytes.load(value) && value.choice.is<BinaryBytesAlt>() && value.after == 9);
  ZuCheck(value.choice.p<BinaryBytesAlt>().data.data() ==
    reinterpret_cast<const uint8_t *>(Wire.data() + 17));
  auto storage = ZtScratch(PrintBuf, Wire.length());
  Sink out{{storage.data(), unsigned(Wire.length())}};
  ZuCheck(save<ZuFacet::BinaryBytesFIX>(out, value) && out.data() == Wire);
  Sink wrong{{storage.data(), unsigned(Wire.length())}};
  ZuCheck(!save<ZuFacet::BinaryTextFIX>(wrong, value) &&
    wrong.error().code == ErrorCode::Unresolved);
  constexpr auto Bad = "1000=7\1" "212=1\1" "213=XX\1"_z;
  auto bad = handler<BinaryEnvelope, ZuFacet::BinaryBytesFIX>(Bad);
  bad.load(value);
  Indexed<BinaryEnvelope, ZuFacet::BinaryBytesFIX> badIndex{Bad}; badIndex.load(copy);
  ZuCheck(bad.error().code == ErrorCode::Length && bad.error().tag == 213 &&
    bad.error().offset == 18);
  ZuCheck(badIndex.error().code == bad.error().code && badIndex.error().tag == bad.error().tag &&
    badIndex.error().offset == bad.error().offset);
}
static void binaryDeferred()
{
  ZuTestScope(binaryDeferred);
  constexpr auto Wire = "212=5\1" "213=A\1\0=B\1"_z;
  auto bytes = handler<BinaryChoice>(Wire).ctor();
  ZuCheck(bytes.is<ParseView>() && bytes.p<ParseView>().input.data() == Wire.data());
  ZuCheck(!resolve<BinaryBytesAlt>(bytes) && bytes.is<BinaryBytesAlt>());
  ZuCheck(bytes.p<BinaryBytesAlt>().data.data() ==
    reinterpret_cast<const uint8_t *>(Wire.data() + 10));
  auto text = handler<BinaryChoice>(Wire).ctor();
  ZuCheck(!resolve<BinaryTextAlt>(text) && text.is<BinaryTextAlt>());
  ZuCheck(text.p<BinaryTextAlt>().data.data() == Wire.data() + 10 &&
    text.p<BinaryTextAlt>().data.length() == 5);
  auto owned = handler<BinaryChoice>(Wire).ctor();
  ZuCheck(!resolve<BinaryOwnedAlt>(owned) && owned.is<BinaryOwnedAlt>());
  ZuCheck(ZuBSpan{owned.p<BinaryOwnedAlt>().data} == bytes.p<BinaryBytesAlt>().data);
  ZuCheck(owned.p<BinaryOwnedAlt>().data.data() != bytes.p<BinaryBytesAlt>().data.data());
  auto storage = ZtScratch(PrintBuf, Wire.length());
  Sink out{{storage.data(), unsigned(Wire.length())}};
  ZuCheck(save(out, owned) && out.data() == Wire);
  constexpr auto Bad = "212=1\1" "213=XX\1"_z;
  auto bad = handler<BinaryChoice>(Bad).ctor();
  auto error = resolve<BinaryBytesAlt>(bad);
  ZuCheck(error.code == ErrorCode::Length && error.tag == 213 && error.offset == 11);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(nested);
  ZuTestCall(binarySelection);
  ZuTestCall(binaryDeferred);
  ZuTestCall(selection);
  ZuTestCall(lifetime);
  ZuTestCall(errors);
  ZuTestCall(construction);
  return 0;
}
