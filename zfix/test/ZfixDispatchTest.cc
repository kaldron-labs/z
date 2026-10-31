//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfixDispatch.hh>

using namespace ZuTestUtil;
using namespace Zfix;
using namespace Zfix::Fmt;
using Text = ZtString<ZtStringHeapID<"Zfix.DispatchTest">>;
template <typename T> struct Rows : ZtArray<T, ZtArrayHeapID<"Zfix.DispatchTest.Rows">> {
  ZuDerive_(Rows, (ZtArray<T, ZtArrayHeapID<"Zfix.DispatchTest.Rows">>));
friend AsArray<> Zfix_Fmt(Rows *);
};

struct Sub {
  ZuCSpan	id, kind;
};

ZfStruct(, (Sub, FIX), (id, (FIX::ID<523>, Required), String),
  (kind, (FIX::ID<9004>), String));
ZfStructImpl(Sub); ZfStructImpl(Sub, FIX);
struct PartyA {
  ZuCSpan	id;
  int		rank;
  ZuCSpan	kind;
  Rows<Sub>	subs;
};

ZfStruct(, (PartyA, FIX), (id, (FIX::ID<448>, Required), String),
  (rank, (FIX::ID<452>), Int32), (kind, (FIX::ID<9003>), String),
  (subs, (FIX::ID<802>), UDT));
ZfStructImpl(PartyA); ZfStructImpl(PartyA, FIX);
struct PartyB {
  ZuCSpan	id, extra;
  int		rank;
  ZuCSpan	kind;
  Rows<Sub>	subs;
};

ZfStruct(, (PartyB, FIX), (id, (FIX::ID<448>, Required), String),
  (extra, (FIX::ID<447>), String), (rank, (FIX::ID<452>), Int32),
  (kind, (FIX::ID<9003>), String), (subs, (FIX::ID<802>), UDT));
ZfStructImpl(PartyB); ZfStructImpl(PartyB, FIX);
struct A {
  ZuCSpan	kind, id;
  int		amount;
  Rows<PartyA>	parties;
};

ZfStruct(, (A, FIX), (kind, (FIX::ID<150>, Required), String),
  (id, (FIX::ID<11>, Required), String), (amount, (FIX::ID<44>), Int32),
  (parties, (FIX::ID<453>), UDT));
ZfStructImpl(A); ZfStructImpl(A, FIX);
struct B {
  ZuCSpan	kind, id, text;
  Rows<PartyB>	parties;
};

ZfStruct(, (B, FIX), (parties, (FIX::ID<453>), UDT),
  (id, (FIX::ID<11>, Required), String), (text, (FIX::ID<58>, Required), String),
  (kind, (FIX::ID<150>, Required), String));
ZfStructImpl(B); ZfStructImpl(B, FIX);
using AMsg = MT<A, "X", ZuFacet::FIX, Discrim<150, "A">>;
using BMsg = MT<B, "X", ZuFacet::FIX, Discrim<150, "B">>;
using Plain = MT<A, "D">;
using Dictionary = MTList<AMsg, Plain, BMsg>;
static ZuSpan<char> grow(void *owner, unsigned, unsigned needed) {
  auto &text = *static_cast<Text *>(owner); text.length(needed);
  return {text.data(), text.length()};
}
static Text packet(ZuCSpan body, ZuCSpan type = "X", ZuCSpan version = "FIX.4.4") {
  Text storage;
  FrameWriter out{{}, version, grow, &storage};
  out.body() << "35=" << type << "\1" "49=CLIENT\1" "56=SERVER\1"
    "34=7\1" "52=20261027-12:34:56\1" << body;
  auto result = out.finish();
  return result ? Text{result.data} : Text{};
}
static void primary() {
  ZuTestScope(primary);
  auto wire = packet("150=A\1" "11=plain\1" "44=12\1", "D");
  auto parsed = frame(wire); ZuCheck(parsed.state == FrameState::Complete);
  unsigned calls = 0;
  auto error = dispatch<Dictionary>(parsed.view, [&calls](auto *type, const auto &o,
      const Header &h, const Trailer &) -> Error {
    using E = ZuDecay<decltype(*type)>;
    if constexpr (ZuIsSame<E, Plain>{}) {
      ++calls; ZuCheck(o.id == "plain" && o.amount == 12 && h.seqNo == 7);
      ZuCheck(h.sender == "CLIENT" && h.target == "SERVER");
    } else ZuCheck(false);
    return {};
  });
  ZuCheck(!error && calls == 1);
  wire = packet("11=x\1", "UNKNOWN"); parsed = frame(wire);
  error = dispatch<Dictionary>(parsed.view, [](auto *, const auto &, const auto &,
    const auto &) -> Error {
    ZuCheck(false); return {};
  });
  ZuCheck(error.code == ErrorCode::Unresolved && error.tag == 35);
}
static void selected() {
  ZuTestScope(selected);
  auto wire = packet("11=selected\1" "44=not-an-integer\1" "58=valid\1"
    "453=2\1"
    "448=p1\1" "447=extra\1" "452=4\1" "9003=A\1"
    "448=p2\1" "452=5\1" "9003=A\1" "150=B\1");
  auto parsed = frame(wire); ZuCheck(parsed.state == FrameState::Complete);
  unsigned calls = 0;
  auto error = dispatch<Dictionary>(parsed.view, [&calls](auto *type, const auto &o,
      const Header &, const Trailer &) -> Error {
    using E = ZuDecay<decltype(*type)>;
    if constexpr (ZuIsSame<E, BMsg>{}) {
      ++calls; ZuCheck(o.kind == "B" && o.id == "selected" && o.text == "valid");
      ZuCheck(o.parties.length() == 2);
      if (o.parties.length() == 2) {
	ZuCheck(o.parties[0].id == "p1" && o.parties[0].extra == "extra"
	  && o.parties[0].rank == 4);
	ZuCheck(o.parties[1].id == "p2" && o.parties[1].rank == 5 && o.parties[1].kind == "A");
      }
    } else ZuCheck(false);
    return {};
  });
  if (error) log("# selected error ", error.code, " tag ", error.tag, " at ", error.offset);
  ZuCheck(!error && calls == 1);
}
static void prepared() {
  ZuTestScope(prepared);
  for (ZuCSpan type : {ZuCSpan{"D"}, ZuCSpan{"X"}}) {
    auto wire = packet("150=A\1" "11=x\1" "453=2\1"
      "448=p1\1" "452=4\1" "448=p2\1" "452=5\1", type);
    auto parsed = frame(wire); Header header;
    ZuCheck(!catalogHeader<Dictionary>(parsed.view, header));
    ZuCheck(header.bytes && parsed.view.content[header.bytes] == '1');
    unsigned calls = 0;
    auto error = dispatch<Dictionary>(parsed.view,
      [&calls]<typename E>(E *, const auto &o, const Header &h, const Trailer &) -> Error {
	if constexpr (!ZuIsSame<E, BMsg>{}) {
	  ++calls;
	  ZuCheck(h.seqNo == 7 && h.sender == "CLIENT" && o.id == "x");
	  ZuCheck(o.parties.length() == 2);
	  if (o.parties.length() == 2)
	    ZuCheck(o.parties[0].id == "p1" && o.parties[1].id == "p2");
	}
	return {};
      }, {}, &header);
    ZuCheck(!error && calls == 1);
  }
  auto wire = packet("150=A\1" "11=x\1" "49=LATE\1", "D");
  auto parsed = frame(wire); Header header;
  ZuCheck(!catalogHeader<Dictionary>(parsed.view, header));
  auto error = dispatch<Dictionary>(parsed.view,
    [](auto *, const auto &, const Header &, const Trailer &) -> Error {
      ZuCheck(false); return {};
    }, {}, &header);
  ZuCheck(error.code == ErrorCode::Envelope && error.tag == 49);
}
static void failures() {
  ZuTestScopeRT(failures);
  auto check = [](ZuCSpan body, unsigned code, unsigned tag) {
    auto wire = packet(body); auto parsed = frame(wire);
      ZuCheckRT(parsed.state == FrameState::Complete);
    unsigned calls = 0;
    auto error = dispatch<Dictionary>(parsed.view,
      [&calls](auto *, const auto &, const auto &, const auto &) -> Error { ++calls; return {};
	});
    if (calls || error.code != code || error.tag != tag)
      log("# expected ", code, "/", tag, " got ", error.code, "/", error.tag, " at ",
	error.offset, " calls=", calls);
    ZuCheckRT(!calls && error.code == code && error.tag == tag);
  };
  check("11=x\1" "453=1\1" "448=p\1" "9003=B\1", ErrorCode::Required, 150);
  check("11=x\1" "150=Z\1", ErrorCode::Unresolved, 150);
  check("11=x\1" "150=A\1" "150=B\1", ErrorCode::Duplicate, 150);
  check("11=x\1" "150=A\1" "44=bad\1", ErrorCode::Value, 44);
  check("11=x\1" "150=B\1", ErrorCode::Required, 58);
  check("150=B\1" "11=x\1" "58=valid\1" "453=2\1" "448=p\1", ErrorCode::Group, 0);
  check("150=B\1" "11=x\1" "58=valid\1" "49=late\1", ErrorCode::Envelope, 49);
  check("150=B\1" "11=x\1" "58=valid\1" "453=1\1"
    "448=p\1" "452=5\1" "447=late\1", ErrorCode::Group, 447);
}
static void limits() {
  ZuTestScope(limits);
  auto wire = packet("150=A\1" "11=x\1" "453=1\1" "448=p\1");
  auto parsed = frame(wire);
  auto callback = [](auto *, const auto &, const auto &, const auto &) -> Error { return {}; };
  Limits cfg; cfg.parseBytes = 1;
  ZuCheck(dispatch<Dictionary>(parsed.view, callback, cfg).code == ErrorCode::Size);
  cfg = {}; cfg.groupEntries = 0;
  ZuCheck(dispatch<Dictionary>(parsed.view, callback, cfg).code == ErrorCode::Size);
  cfg = {}; cfg.groupDepth = 0;
  ZuCheck(dispatch<Dictionary>(parsed.view, callback, cfg).code == ErrorCode::Depth);
  cfg = {}; cfg.frameBytes = wire.length() - 1;
  ZuCheck(dispatch<Dictionary>(parsed.view, callback, cfg).code == ErrorCode::Size);
  // Selection stores no tree; only the selected ordinary layout is charged.
  wire = packet("150=A\1" "11=x\1"); parsed = frame(wire);
  cfg = {}; cfg.parseBytes = sizeof(Position) * Schema<Wire_<A>, ZuFacet::FIX>::Leaves::N;
  ZuCheck(!dispatch<Dictionary>(parsed.view, callback, cfg));
  --cfg.parseBytes;
  ZuCheck(dispatch<Dictionary>(parsed.view, callback, cfg).code == ErrorCode::Size);
  using LongType = MT<A, "VERY-LONG-MESSAGE-TYPE">;
  wire = packet("150=A\1" "11=x\1", LongType::MsgType); parsed = frame(wire);
  cfg = {}; cfg.fieldBytes = 17; // standard SendingTime fits; MsgType does not
  auto error = dispatch<MTList<LongType>>(parsed.view, callback, cfg);
  ZuCheck(error.code == ErrorCode::Size && error.tag == 35);
  wire = packet("150=A\1" "11=x\1", "D", "VERY-LONG-BEGINSTRING-VALUE"); parsed = frame(wire);
  ZuCheck(!dispatch<Dictionary>(parsed.view, callback));
  error = dispatch<Dictionary>(parsed.view, callback, cfg);
  ZuCheck(error.code == ErrorCode::Size && error.tag == 8);
}
struct One {
  ZuCSpan	id;
};

ZfStruct(, (One, FIX), (id, (FIX::ID<11>, Required), String));
ZfStructImpl(One); ZfStructImpl(One, FIX);
using Empty = MT<One, "K", ZuFacet::FIX, Discrim<9000, "">>;
using Named = MT<One, "K", ZuFacet::FIX, Discrim<9000, "named">>;
using KeyDictionary = MTList<Empty, Named>;
static void opaqueKeys() {
  ZuTestScope(opaqueKeys);
  auto wire = packet("11=key\1" "9000=\1", "K");
  auto parsed = frame(wire); unsigned calls = 0;
  auto error = dispatch<KeyDictionary>(parsed.view, [&calls](auto *type, const One &o,
      const auto &, const auto &) -> Error {
    if constexpr (ZuIsSame<ZuDecay<decltype(*type)>, Empty>{}) { ++calls;
      ZuCheck(o.id == "key"); }
    else ZuCheck(false);
    return {};
  });
  ZuCheck(!error && calls == 1);

}
static void nested() {
  ZuTestScope(nested);
  auto wire = packet("11=nested\1" "58=valid\1" "453=1\1" "448=p\1" "9003=A\1"
    "802=2\1" "523=s1\1" "9004=A\1" "523=s2\1" "9004=B\1" "150=B\1");
  auto parsed = frame(wire); unsigned calls = 0;
  auto error = dispatch<Dictionary>(parsed.view, [&calls](auto *type, const auto &o,
      const auto &, const auto &) -> Error {
    if constexpr (ZuIsSame<ZuDecay<decltype(*type)>, BMsg>{}) {
      ++calls; ZuCheck(o.parties.length() == 1 && o.parties[0].subs.length() == 2);
      if (o.parties.length() == 1 && o.parties[0].subs.length() == 2) {
	ZuCheck(o.parties[0].subs[0].kind == "A");
	ZuCheck(o.parties[0].subs[1].id == "s2");
      }
    } else ZuCheck(false);
    return {};
  });
  ZuCheck(!error && calls == 1);
}
ZuStructFacet(Alternate);
ZfixConfig(Alternate, DefltConfig);
struct Other {
  ZuCSpan	id, kind;
  int		value;
};

ZfStruct(, (Other, Alternate), (id, (FIX::ID<11>, Required), String),
  (kind, (FIX::ID<150>, Required), String), (value, (FIX::ID<9002>, Required), Int32));
ZfStructImpl(Other); ZfStructImpl(Other, Alternate);
using OtherMsg = MT<Other, "X", ZuFacet::Alternate, Discrim<150, "Q">>;
static void facets() {
  ZuTestScope(facets);
  auto wire = packet("11=alternate\1" "44=bad\1" "58=text 150=A\1"
    "9002=42\1" "150=Q\1");
  auto parsed = frame(wire); unsigned calls = 0;
  auto error = dispatch<MTList<AMsg, OtherMsg>>(parsed.view,
    [&calls](auto *type, const auto &o, const auto &, const auto &) -> Error {
      if constexpr (ZuIsSame<ZuDecay<decltype(*type)>, OtherMsg>{}) {
	++calls; ZuCheck(o.value == 42 && o.id == "alternate");
      } else ZuCheck(false);
      return {};
    });
  ZuCheck(!error && calls == 1);
}
static void lifetime() {
  ZuTestScopeRT(lifetime);
  for (unsigned i = 0; i < 2; ++i) {
    ZuCSpan type = i ? "X" : "D";
    auto wire = packet("150=A\1" "11=borrowed\1", type); auto parsed = frame(wire);
    ZuCSpan borrowed; Text owned; unsigned calls = 0;
    auto error = dispatch<Dictionary>(parsed.view, [&borrowed, &owned, &calls](auto *type,
	const auto &o, const auto &, const auto &) -> Error {
      using E = ZuDecay<decltype(*type)>;
      if constexpr (ZuIsSame<E, Plain>{} || ZuIsSame<E, AMsg>{}) {
	borrowed = o.id; owned = o.id; ++calls;
      } else ZuCheckRT(false);
      return {ErrorCode::Output, 5000, 37};
    });
    ZuCheckRT(calls == 1 && error.code == ErrorCode::Output && error.tag == 5000
      && error.offset == 37);
    ZuCheckRT(borrowed.data() >= wire.data()
      && borrowed.data() + borrowed.length() <= wire.data() + wire.length());
    wire.data()[borrowed.data() - wire.data()] = 'Z';
    ZuCheckRT(borrowed == "Zorrowed" && owned == "borrowed");
  }
  unsigned calls = 0;
  auto wire = packet("11=x\1"); auto parsed = frame(wire);
  auto error = dispatch<MTList<>>(parsed.view,
    [&calls](auto *, const auto &, const auto &, const auto &) -> Error { ++calls; return {}; });
  ZuCheckRT(!calls && error.code == ErrorCode::Unresolved && error.tag == 35);
}
static void headerPrefix() {
  ZuTestScopeRT(headerPrefix);
  for (ZuCSpan type : {ZuCSpan{"X"}, ZuCSpan{"D"}, ZuCSpan{"UNKNOWN"}}) {
    Text wire;
    FrameWriter out{{}, "FIX.4.4", grow, &wire};
    out.body() << "35=" << type << "\1" "9000=extension\1"
      "49=CLIENT\1" "56=SERVER\1" "34=7\1" "52=20261027-12:34:56\1"
      "150=A\1" "11=body\1";
    auto result = out.finish(); ZuCheckRT(bool(result));
    if (!result) continue;
    auto parsed = frame(result.data); Header header;
    ZuCheckRT(parsed.state == FrameState::Complete);
    auto error = catalogHeader<Dictionary>(parsed.view, header);
    if (type == "UNKNOWN") {
      ZuCheckRT(error.code == ErrorCode::Required && error.tag == 49);
    } else {
      ZuCheckRT(!error && header.sender == "CLIENT" && header.target == "SERVER"
	&& header.seqNo == 7);
    }
    Limits small; small.frameBytes = parsed.view.wire.length() - 1;
    error = catalogHeader<Dictionary>(parsed.view, header, small);
    ZuCheckRT(error.code == ErrorCode::Size);
  }
}
static void sameSource()
{
  ZuTestScope(sameSource);
  using OtherA = MT<A, "X", ZuFacet::FIX, Discrim<150, "B">>;
  using Same = MTList<AMsg, OtherA>;
  auto wire = packet("150=B\1" "11=x\1" "453=1\1" "448=p\1" "452=7\1"
    "9003=nested\1" "802=1\1" "523=sub\1" "9004=inner\1");
  auto parsed = frame(wire);
  Limits cfg;
  cfg.parseBytes = sizeof(Position) * (Schema<Wire_<A>, ZuFacet::FIX>::Leaves::N +
    Schema<PartyA, ZuFacet::FIX>::Leaves::N + Schema<Sub, ZuFacet::FIX>::Leaves::N);
  bool called = false;
  auto callback = [&called]<typename E>(E *, const A &o, const Header &h,
      const Trailer &) -> Error {
    if constexpr (!ZuIsSame<E, OtherA>{}) return {ErrorCode::Unresolved, 150};
    if (o.parties.length() != 1) return {};
    const auto &party = o.parties[0];
    if (party.subs.length() != 1) return {};
    const auto &sub = party.subs[0];
    called = o.kind == "B" && o.id == "x" &&
      party.id == "p" && party.rank == 7 && sub.id == "sub" &&
      h.seqNo == 7 && party.kind == "nested" && sub.kind == "inner";
    return {};
  };
  ZuCheck(!dispatch<Same>(parsed.view, callback, cfg) && called);
  --cfg.parseBytes; called = false;
  ZuCheck(dispatch<Same>(parsed.view, callback, cfg).code == ErrorCode::Size && !called);
  wire = packet("150=B\1" "11=x\1" "453=1\1" "452=7\1" "448=p\1");
  parsed = frame(wire);
  ZuCheck(dispatch<Same>(parsed.view, callback).code == ErrorCode::Group && !called);
  wire = packet("150=B\1" "11=x\1" "150=A\1"); parsed = frame(wire);
  auto error = dispatch<Same>(parsed.view, callback);
  ZuCheck(error.code == ErrorCode::Duplicate && error.tag == 150 && !called);
}

static void inspection()
{
  ZuTestScope(inspection);
  auto callback = [](auto *, const auto &, const auto &, const auto &) -> Error {
    ZuCheck(false); return {};
  };
  auto wire = packet("11=text 150=A\1" "150=Z\1");
  auto parsed = frame(wire);
  auto error = dispatch<Dictionary>(parsed.view, callback);
  auto value = static_cast<const char *>(memchr(wire.data(), 'Z', wire.length()));
  ZuCheck(error.code == ErrorCode::Unresolved && error.tag == 150 &&
    error.offset == unsigned(value - wire.data()));
  wire = packet("11=x\1" "150=A\1" "150=A\1"); parsed = frame(wire);
  error = dispatch<Dictionary>(parsed.view, callback);
  ZuCheck(error.code == ErrorCode::Duplicate && error.tag == 150 &&
    error.offset == unsigned(parsed.view.content.data() - wire.data()) +
      parsed.view.content.length() - 2);
  wire = packet("11=x\1"); parsed = frame(wire);
  error = dispatch<Dictionary>(parsed.view, callback);
  ZuCheck(error.code == ErrorCode::Required && error.tag == 150 && !error.offset);

  FieldPos pos;
  Limits cfg; cfg.parseBytes = 0;
  error = inspect("58=text 150=B\1" "150=A\1", 150, pos, cfg);
  ZuCheck(!error && pos.offset == 18 && pos.length == 1);
  error = inspect("150=A\1" "bad\1", 150, pos);
  ZuCheck(error.code == ErrorCode::Tag && error.offset == 6);
  error = inspect("150=A\1" "11=unterminated", 150, pos);
  ZuCheck(error.code == ErrorCode::Truncated);
  cfg.fieldBytes = 1;
  error = inspect("150=AB\1", 150, pos, cfg);
  ZuCheck(error.code == ErrorCode::Size && error.tag == 150 && error.offset == 4);
}

int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(primary);
  ZuTestCall(selected);
  ZuTestCall(prepared);
  ZuTestCall(failures);
  ZuTestCall(limits);
  ZuTestCall(opaqueKeys);
  ZuTestCall(nested);
  ZuTestCall(facets);
  ZuTestCall(lifetime);
  ZuTestCall(headerPrefix);
  ZuTestCall(sameSource);
  ZuTestCall(inspection);
  return 0;
}
