//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Native-only canonical FIX 4.4 workload mappings

#ifndef ZfixLoadFIX_HH
#define ZfixLoadFIX_HH

#include <zlib/Zfix.hh>
#include <zlib/ZfixFields.hh>
#include "load.hh"

ZuStructFacet(LoadScalar);
ZuStructFacet(LoadText);

namespace ZfixLoad {

template <typename T> Zfix::Fmt::AsArray<> Zfix_Fmt(Rows<T> *);
ZfStruct(, (SubParty, FIX), (id, (FIX::ID<523>, Required), String),
  (type, (FIX::ID<803>, Required), UInt32));
ZfStruct(, (Party, FIX), (id, (FIX::ID<448>, Required), String),
  (source, (FIX::ID<447>, Required), String), (role, (FIX::ID<452>, Required), UInt32),
  (subs, (FIX::ID<802>), UDT));
ZfStruct(, (Order), (key, (FIX::ID<11>, Required), UInt64),
  (handling, (FIX::ID<21>), String), (symbol, (FIX::ID<55>, Required), String),
  (side, (FIX::ID<54>, Required), String), (time, (FIX::ID<60>, Required), DateTime),
  (qty, (FIX::ID<38>, Required), UInt32), (type, (FIX::ID<40>, Required), String),
  (price, (FIX::ID<44>, Required), Decimal), (parties, (FIX::ID<453>), UDT),
  (text, (FIX::ID<58>), String));
ZfStruct(, (Cancel), (original, (FIX::ID<41>, Required), UInt64),
  (key, (FIX::ID<11>, Required), UInt64), (symbol, (FIX::ID<55>, Required), String),
  (side, (FIX::ID<54>, Required), String), (time, (FIX::ID<60>, Required), DateTime),
  (qty, (FIX::ID<38>, Required), UInt32));
ZfStruct(, (Replace, FIX), (order, , UDT), (original, (FIX::ID<41>, Required), UInt64));
ZfStruct(, (Report), (avg, (FIX::ID<6>, Required), Decimal),
  (key, (FIX::ID<11>, Required), UInt64), (cum, (FIX::ID<14>, Required), UInt32),
  (exec, (FIX::ID<17>, Required), UInt64), (lastPx, (FIX::ID<31>), Decimal),
  (lastQty, (FIX::ID<32>), UInt32), (order, (FIX::ID<37>, Required), UInt64),
  (qty, (FIX::ID<38>), UInt32), (status, (FIX::ID<39>, Required), String),
  (original, (FIX::ID<41>, Deflt<0>), UInt64), (price, (FIX::ID<44>), Decimal),
  (side, (FIX::ID<54>, Required), String), (symbol, (FIX::ID<55>, Required), String),
  (text, (FIX::ID<58>), String), (time, (FIX::ID<60>), DateTime),
  (kind, (FIX::ID<150>, Required), String), (leaves, (FIX::ID<151>, Required), UInt32));
// Session declarations are part of each actual message's ordinary field list.
// Replace reuses Order as its first component, including those declarations.
using OrderFIXFields = ZuTypeConcat<Zfix::SessionFields<Order>, ZfFields_Order>;
OrderFIXFields ZuFields_(Order *, ZuFacet::FIX *);
ZfVFieldMatchFn ZfFieldMatcher_(Order *, ZuFacet::FIX *);
using CancelFIXFields = ZuTypeConcat<Zfix::SessionFields<Cancel>, ZfFields_Cancel>;
CancelFIXFields ZuFields_(Cancel *, ZuFacet::FIX *);
ZfVFieldMatchFn ZfFieldMatcher_(Cancel *, ZuFacet::FIX *);
using ReportFIXFields = ZuTypeConcat<Zfix::SessionFields<Report>, ZfFields_Report>;
ReportFIXFields ZuFields_(Report *, ZuFacet::FIX *);
ZfVFieldMatchFn ZfFieldMatcher_(Report *, ZuFacet::FIX *);

// Render the same ordinary declarations into each workload-selected facet.
#define ZfixLoadRender(O, Facet, ...) \
  ZuPP_Eval(ZuPP_MapArg(ZfField_RenderDecl, (O, Facet), __VA_ARGS__)) \
  using ZfFields_##O##_##Facet = ZuTypeConcat<Zfix::SessionFields<O>, ZuTypeList< \
    ZuPP_Eval(ZuPP_MapArgComma(ZfField_RenderType, (O, Facet), __VA_ARGS__))>>; \
  ZfFields_##O##_##Facet ZuFields_(O *, ZuFacet::Facet *); \
  ZfVFieldMatchFn ZfFieldMatcher_(O *, ZuFacet::Facet *)

// Presence is an explicit application render choice. A zero group count remains
// a value; optional text/data fields are absent when that shape is not selected.
ZfStructRender(, SubParty, LoadScalar, id, type);
ZfStructRender(, Party, LoadScalar, id, source, role, subs);
ZfStructRender(, SubParty, LoadText, id, type);
ZfStructRender(, Party, LoadText, id, source, role, subs);
ZfixLoadRender(Order, LoadScalar, key, handling, symbol, side, time, qty, type, price, parties);
ZfixLoadRender(Order, LoadText, key, handling, symbol, side, time, qty, type, price, parties,
  text);
ZfStructRender(, Replace, LoadScalar, order, original);
ZfStructRender(, Replace, LoadText, order, original);
ZfixLoadRender(Report, LoadScalar, avg, key, cum, exec, lastPx, lastQty, order, qty,
  status, original, price, side, symbol, time, kind, leaves);
ZfixLoadRender(Report, LoadText, avg, key, cum, exec, lastPx, lastQty, order, qty,
  status, original, price, side, symbol, text, time, kind, leaves);

#undef ZfixLoadRender

// Ordinary reports have no OrigClOrdID. Retain the selected render/configuration
// while removing only that root field; cancel/replace reports keep it.
template <typename Facet> struct NoOrig : Facet { };
template <typename F> using ReportField = ZuBool<(Zfix::Fmt::tag<F>() != 41)>;
template <typename Facet>
ZuTypeGrep<ReportField, ZuActiveFields<Report, Facet>> ZuFields_(Report *, NoOrig<Facet> *);

template <typename O, typename Fn> decltype(auto) mapping(const O &o, Fn &&fn) {
  if constexpr (ZuIsSame<O, Cancel>{}) return fn(ZuFacet::FIX{});
  else if constexpr (ZuIsSame<O, Replace>{}) return mapping(o.order, ZuFwd<Fn>(fn));
  else {
    auto select = [&]<typename Facet>(Facet) -> decltype(auto) {
      if constexpr (ZuIsSame<O, Report>{})
	if (!o.original) return fn(NoOrig<Facet>{});
      return fn(Facet{});
    };
    if (o.text.length()) return select(ZuFacet::LoadText{});
    return select(ZuFacet::LoadScalar{});
  }
}
using Ack = Zfix::MT<Report, "8", ZuFacet::FIX, Zfix::Discrim<150, "0">>;
using Fill = Zfix::MT<Report, "8", ZuFacet::FIX, Zfix::Discrim<150, "F">>;
using Canceled = Zfix::MT<Report, "8", ZuFacet::FIX, Zfix::Discrim<150, "4">>;
using Replaced = Zfix::MT<Report, "8", ZuFacet::FIX, Zfix::Discrim<150, "5">>;
using NewOrder = Zfix::MT<Order, "D", ZuFacet::FIX, Zfix::NoDiscrim, ZuTypeList<Ack,
  Fill>>;
using CancelOrder = Zfix::MT<Cancel, "F", ZuFacet::FIX, Zfix::NoDiscrim,
  ZuTypeList<Canceled>>;
using ReplaceOrder = Zfix::MT<Replace, "G", ZuFacet::FIX, Zfix::NoDiscrim,
  ZuTypeList<Replaced, Fill>>;
using Dictionary = Zfix::MTList<NewOrder, Ack, Fill, CancelOrder, ReplaceOrder, Canceled,
  Replaced>;
} // ZfixLoad

#endif /* ZfixLoadFIX_HH */
