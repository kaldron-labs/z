//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZtString.hh>
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
  ZuBSpan	data;
};

ZfStruct(, (DataMsg, FIX),
  (data, (FIX::ID<96>, FIX::Len<95>),	Bytes));
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
  constexpr auto wire = "1000=7\1" "55=123\1"_z;
  auto text = handler<Envelope, ZuFacet::TextFIX>(wire);
  auto a = text.ctor();
  ZuCheck(text.valid() && a.marker == 7 && a.choice.is<TextMsg>());
  ZuCheck(a.choice.p<TextMsg>().value == "123");
  auto number = handler<Envelope, ZuFacet::NumFIX>(wire);
  ZuCheck(number.load(a));
  ZuCheck(a.choice.is<NumMsg>() && a.choice.p<NumMsg>().value == 123);
  char bytes[wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save<ZuFacet::NumFIX>(out, a) && out.flush());
  ZuCheck(out.data() == wire);
  Sink wrong{{bytes, sizeof(bytes)}};
  ZuCheck(!save<ZuFacet::TextFIX>(wrong, a));
  ZuCheck(wrong.error().code == ErrorCode::Unresolved);
  constexpr auto binary = "1000=1\1" "95=4\1" "96=A\1\0=\1"_z;
  auto data = handler<Envelope, ZuFacet::DataFIX>(binary);
  auto b = data.ctor();
  ZuCheck(data.valid() && b.choice.is<DataMsg>());
  ZuCheck(b.choice.p<DataMsg>().data == ZuBSpan{"A\1\0="_z});
}

static void selection()
{
  ZuTestScope(selection);
  constexpr auto wire = "55=123\1"_z;
  auto h = handler<Choice>(wire);
  auto a = h.ctor();
  ZuCheck(h.valid() && a.is<ParseView>());
  ZuCheck(a.p<ParseView>().input.data() == wire.data());
  ZuCheck(!resolve<TextMsg>(a));
  ZuCheck(a.is<TextMsg>() && a.p<TextMsg>().value == "123");
  auto b = handler<Choice>(wire).ctor();
  ZuCheck(!resolve<NumMsg>(b));
  ZuCheck(b.is<NumMsg>() && b.p<NumMsg>().value == 123);
  char bytes[wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, b) && out.flush());
  ZuCheck(out.data() == wire);
  Immediate immediate;
  auto selected = handler<Immediate>(wire);
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
  constexpr auto binaryWire = "95=4\1" "96=A\1\0=\1"_z;
  auto binary = handler<Choice>(binaryWire).ctor();
  ZuCheck(!resolve<DataMsg>(binary));
  ZuCheck(binary.p<DataMsg>().data == ZuBSpan{"A\1\0="_z});
}

static void errors()
{
  ZuTestScope(errors);
  auto c = handler<Choice>("55=text\1").ctor();
  auto e = resolve<NumMsg>(c);
  ZuCheck(e.code == ErrorCode::Value && e.tag == 55);
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
  constexpr auto wire = "55=explicit\1"_z;
  char output[wire.length()];
  Sink out{{output, sizeof(output)}};
  ZuCheck(save(out, explicit_) && out.flush());
  ZuCheck(out.data() == wire);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(nested);
  ZuTestCall(selection);
  ZuTestCall(lifetime);
  ZuTestCall(errors);
  ZuTestCall(construction);
  return 0;
}
