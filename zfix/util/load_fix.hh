//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Native-only canonical FIX 4.4 workload mappings

#ifndef ZfixLoadFIX_HH
#define ZfixLoadFIX_HH

#include <zlib/Zfix.hh>
#include "load.hh"

ZuStructFacet(LoadScalar);
ZuStructFacet(LoadText);
ZuStructFacet(LoadBinary);

namespace ZfixLoad {

template <typename T> Zfix::Fmt::AsArray<> Zfix_Fmt(Rows<T> *);
ZfStruct(, (SubParty, FIX), (id, (FIX::ID<523>, Required), String),
  (type, (FIX::ID<803>, Required), UInt32));
ZfStruct(, (Party, FIX), (id, (FIX::ID<448>, Required), String),
  (source, (FIX::ID<447>, Required), String), (role, (FIX::ID<452>, Required), UInt32),
  (subs, (FIX::ID<802>), UDT));
ZfStruct(, (Order, FIX), (key, (FIX::ID<11>, Required), UInt64),
  (handling, (FIX::ID<21>), String), (symbol, (FIX::ID<55>, Required), String),
  (side, (FIX::ID<54>, Required), String), (time, (FIX::ID<60>, Required), DateTime),
  (qty, (FIX::ID<38>, Required), UInt32), (type, (FIX::ID<40>, Required), String),
  (price, (FIX::ID<44>, Required), Decimal), (parties, (FIX::ID<453>), UDT),
  (text, (FIX::ID<58>), String), (encoded, (FIX::ID<355>, FIX::Len<354>), Bytes));
ZfStruct(, (Cancel, FIX), (original, (FIX::ID<41>, Required), UInt64),
  (key, (FIX::ID<11>, Required), UInt64), (symbol, (FIX::ID<55>, Required), String),
  (side, (FIX::ID<54>, Required), String), (time, (FIX::ID<60>, Required), DateTime),
  (qty, (FIX::ID<38>, Required), UInt32));
ZfStruct(, (Replace, FIX), (original, (FIX::ID<41>, Required), UInt64), (order, , UDT));
ZfStruct(, (Report, FIX), (order, (FIX::ID<37>, Required), UInt64),
  (key, (FIX::ID<11>, Required), UInt64), (exec, (FIX::ID<17>, Required), UInt64),
  (original, (FIX::ID<41>, Deflt<0>), UInt64),
  (kind, (FIX::ID<150>, Required), String), (status, (FIX::ID<39>, Required), String),
  (symbol, (FIX::ID<55>, Required), String), (side, (FIX::ID<54>, Required), String),
  (qty, (FIX::ID<38>), UInt32), (price, (FIX::ID<44>), Decimal),
  (leaves, (FIX::ID<151>, Required), UInt32), (cum, (FIX::ID<14>, Required), UInt32),
  (avg, (FIX::ID<6>, Required), Decimal), (lastQty, (FIX::ID<32>), UInt32),
  (lastPx, (FIX::ID<31>), Decimal), (time, (FIX::ID<60>), DateTime),
  (text, (FIX::ID<58>), String), (encoded, (FIX::ID<355>, FIX::Len<354>), Bytes));
// Presence is an explicit application render choice. A zero group count remains
// a value; optional text/data fields are absent when that shape is not selected.
ZfStructRender(, SubParty, LoadScalar, id, type);
ZfStructRender(, Party, LoadScalar, id, source, role, subs);
ZfStructRender(, SubParty, LoadText, id, type);
ZfStructRender(, Party, LoadText, id, source, role, subs);
ZfStructRender(, SubParty, LoadBinary, id, type);
ZfStructRender(, Party, LoadBinary, id, source, role, subs);
ZfStructRender(, Order, LoadScalar, key, handling, symbol, side, time, qty, type, price, parties);
ZfStructRender(, Order, LoadText, key, handling, symbol, side, time, qty, type, price, parties,
  text);
ZfStructRender(, Order, LoadBinary, key, handling, symbol, side, time, qty, type, price,
  parties, encoded);
ZfStructRender(, Replace, LoadScalar, original, order);
ZfStructRender(, Replace, LoadText, original, order);
ZfStructRender(, Replace, LoadBinary, original, order);
ZfStructRender(, Report, LoadScalar, order, key, exec, original, kind, status, symbol, side,
  qty, price,
  leaves, cum, avg, lastQty, lastPx, time);
ZfStructRender(, Report, LoadText, order, key, exec, original, kind, status, symbol, side, qty,
  price,
  leaves, cum, avg, lastQty, lastPx, time, text);
ZfStructRender(, Report, LoadBinary, order, key, exec, original, kind, status, symbol, side,
  qty, price,
  leaves, cum, avg, lastQty, lastPx, time, encoded);
template <typename O, typename Fn> decltype(auto) mapping(const O &o, Fn &&fn) {
  if constexpr (ZuIsSame<O, Cancel>{}) return fn(ZuFacet::FIX{});
  else if constexpr (ZuIsSame<O, Replace>{}) return mapping(o.order, ZuFwd<Fn>(fn));
  else {
    if (o.text.length()) {
      if (o.encoded.length()) return fn(ZuFacet::FIX{});
      return fn(ZuFacet::LoadText{});
    }
    if (o.encoded.length()) return fn(ZuFacet::LoadBinary{});
    return fn(ZuFacet::LoadScalar{});
  }
}
using Ack = Zfix::MT<Report, "8", ZuFacet::FIX, Zfix::Discriminator<150, "0">>;
using Fill = Zfix::MT<Report, "8", ZuFacet::FIX, Zfix::Discriminator<150, "F">>;
using Canceled = Zfix::MT<Report, "8", ZuFacet::FIX, Zfix::Discriminator<150, "4">>;
using Replaced = Zfix::MT<Report, "8", ZuFacet::FIX, Zfix::Discriminator<150, "5">>;
using NewOrder = Zfix::MT<Order, "D", ZuFacet::FIX, Zfix::NoDiscriminator, ZuTypeList<Ack,
  Fill>>;
using CancelOrder = Zfix::MT<Cancel, "F", ZuFacet::FIX, Zfix::NoDiscriminator,
  ZuTypeList<Canceled>>;
using ReplaceOrder = Zfix::MT<Replace, "G", ZuFacet::FIX, Zfix::NoDiscriminator,
  ZuTypeList<Replaced, Fill>>;
using Dictionary = Zfix::MTList<NewOrder, Ack, Fill, CancelOrder, ReplaceOrder, Canceled,
  Replaced>;
} // ZfixLoad

#endif /* ZfixLoadFIX_HH */
