//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zfix_example_HH
#define zfix_example_HH

#include <zlib/ZuDateTime.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/Zfix.hh>

namespace Example {

namespace Handling {

  ZtEnum(, Handling, int8_t, AutomatedPrivate, AutomatedPublic, Manual);
  ZtEnumMap(, Handling, FIX, "1", "2", "3");
}

namespace Side {

  ZtEnum(, Side, int8_t, Buy, Sell);
  ZtEnumMap(, Side, FIX, "1", "2");
}

namespace OrdType {

  ZtEnum(, OrdType, int8_t, Market, Limit);
  ZtEnumMap(, OrdType, FIX, "1", "2");
}

template <typename T>
using Rows_ = ZtArray<T, ZtArrayHeapID<"Zfix.Example.Rows">>;
template <typename T>
struct Rows : public Rows_<T> {
  ZuDerive_(Rows, (Rows_<T>));
  friend Zfix::Fmt::AsArray<> Zfix_Fmt(Rows *);
};

struct Party {
  ZuCSpan	id, source{"D"};
  unsigned	role = 0;
};

struct Order {
  uint64_t	key = 0;
  ZuCSpan	symbol;
  Handling::T	handling = Handling::AutomatedPrivate;
  Side::T	side = Side::Buy;
  OrdType::T	type = OrdType::Limit;
  unsigned	qty = 0;
  ZuDecimal	price{0};
  ZuDateTime	time;
  Rows<Party>	parties;
};

struct Execution {
  uint64_t	key = 0, order = 0, exec = 0;
  ZuCSpan	kind, status, symbol;
  Side::T	side = Side::Buy;
  ZuDecimal	qty{0}, leavesQty{0}, cumQty{0}, lastQty{0};
  ZuDecimal	price{0}, avg{0}, lastPx{0};
  ZuDateTime	time;
  Rows<Party>	parties;
};

struct Event {
  ZuCSpan	symbol;
  ZuDecimal	price{0};
};

ZfStruct(, (Party, FIX),
  (id,		(FIX::ID<448>, Required),			String),
  (source,	(FIX::ID<447>, Required),			String),
  (role,	(FIX::ID<452>, Required),			UInt32));

ZfStruct(, (Order, FIX),
  (key,		(FIX::ID<11>, Required),			UInt64),
  (handling,	(FIX::ID<21>, Required, Enum<Handling::FIX>),	Int8),
  (symbol,	(FIX::ID<55>, Required),			String),
  (side,	(FIX::ID<54>, Required, Enum<Side::FIX>),	Int8),
  (time,	(FIX::ID<60>, Required),			DateTime),
  (qty,		(FIX::ID<38>, Required),			UInt32),
  (type,	(FIX::ID<40>, Required, Enum<OrdType::FIX>),	Int8),
  (price,	(FIX::ID<44>, Required),			Decimal),
  (parties,	(FIX::ID<453>),					UDT));

ZfStruct(, (Execution, FIX),
  (order,	(FIX::ID<37>, Required),			UInt64),
  (key,		(FIX::ID<11>, Required),			UInt64),
  (exec,	(FIX::ID<17>, Required),			UInt64),
  (kind,	(FIX::ID<150>, Required),			String),
  (status,	(FIX::ID<39>, Required),			String),
  (symbol,	(FIX::ID<55>, Required),			String),
  (side,	(FIX::ID<54>, Required, Enum<Side::FIX>),	Int8),
  (qty,		(FIX::ID<38>),					Decimal),
  (price,	(FIX::ID<44>),					Decimal),
  (leavesQty,	(FIX::ID<151>, Required),			Decimal),
  (cumQty,	(FIX::ID<14>, Required),			Decimal),
  (avg,		(FIX::ID<6>, Required),				Decimal),
  (lastQty,	(FIX::ID<32>),					Decimal),
  (lastPx,	(FIX::ID<31>),					Decimal),
  (time,	(FIX::ID<60>),					DateTime),
  (parties,	(FIX::ID<453>),					UDT));

ZfStruct(, (Event, FIX),
  (symbol,	(FIX::ID<55>, Required),			String),
  (price,	(FIX::ID<44>, Required),			Decimal));

using Ack = Zfix::MT<Execution, "8", ZuFacet::FIX, Zfix::Discrim<150, "0">>;
using Fill = Zfix::MT<Execution, "8", ZuFacet::FIX, Zfix::Discrim<150, "F">>;
using NewOrder = Zfix::MT<Order, "D", ZuFacet::FIX, Zfix::NoDiscrim,
  ZuTypeList<Ack, Fill>>;
using Tick = Zfix::MT<Event, "U1">; // application-defined unsolicited event
using MTList = Zfix::MTList<NewOrder, Ack, Fill, Tick>;
} // Example

#endif /* zfix_example_HH */
