//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// FIX typed message and session context declarations

#ifndef Zfix_HH
#define Zfix_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZtEnum.hh>

#include <zlib/ZfixFmt.hh>
#include <zlib/ZfixFields.hh>

namespace Zfix {

ZtEnumNS(ZfixAPI, Transport, int8_t, Raw, FIX4, FIX44A, FIXT11);

// BeginString identifies the session wire version, independently of transport
// behavior and the application's compile-time business dictionary.
ZtEnumNS(ZfixAPI, Version, int8_t, V4_0, V4_1, V4_2, V4_3, V4_4, VT1_1);

namespace Version {
  ZtEnumMap(ZfixAPI, Version, Wire,
    "FIX.4.0", "FIX.4.1", "FIX.4.2", "FIX.4.3", "FIX.4.4", "FIXT.1.1");
}

// Standard ApplVerID (1128) / DefaultApplVerID (1137) code set. Custom
// application versions are separate FIX fields, not extensions of these codes.
ZtEnumNS(ZfixAPI, AppVersion, int8_t, V2_7, V3_0, V4_0, V4_1, V4_2,
  V4_3, V4_4, V5_0, V5_0_SP1, V5_0_SP2, Latest);

namespace AppVersion {
  ZtEnumMap(ZfixAPI, AppVersion, Wire,
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10");
}

namespace State {
  enum {
    Disconnected,
    Accepting,
    Logon,
    Established,
    Logout,
    Closing,
    Resetting
  };
}

// Epoch disambiguates sequence reuse; submission identity is application-owned.
struct Context {
  uint128_t	session = 0; // runtime session owner, independent of physical opens
  uint64_t	connection = 0;
  uint64_t	epoch = 0;
  uint64_t	seqNo = 0;
  bool		replay = false;
  bool		possDup = false;
  ZuCSpan	version; // borrowed BeginString, including unknown Raw wire versions
};

// Secondary selectors compare one unique, non-repeating root wire field. It
// must not also occur inside any repeating group. Inspection uses SOH-delimited
// text. Entries sharing a
// MsgType must use the same tag and disjoint values, so selection is explicit
// and cannot depend on decoding alternatives or declaration order.
struct NoDiscrim {
  static constexpr int32_t Tag = 0;
  static constexpr auto	Value = ZuString{""};
};

template <int32_t Tag_, ZuString Value_>
struct Discrim {
  static constexpr int32_t Tag = Tag_;
  static constexpr auto	Value = Value_;
  ZuAssert(Tag && Tag != 8 && Tag != 9 && Tag != 10 && Tag != 35);
};

// Responses describe the application's allowed reply types, not correlation,
// cardinality or a promise to complete a business operation.
template <typename O, ZuString MsgType_, typename Facet = ZuFacet::FIX,
  typename Selector = NoDiscrim, typename ReplyTypes = ZuTypeList<>>
struct MT {
  using Object = O;
  using Mapping = Facet;
  using Discrim = Selector;
  using Responses = ReplyTypes;
  static constexpr auto	MsgType = MsgType_;
  ZuAssert((ZuTypeUnique<Responses>::N == Responses::N));
};

namespace Catalog_ {

// Schema metadata can reject a selector declared inside a group without any
// runtime group inspection. Flattened components retain their enclosing scope.
template <typename O, typename Facet, int32_t Tag, bool Nested = false>
consteval bool rootSelector() {
  bool ok = true;
  ZuUnroll::all<typename Fmt::Schema<O, Facet>::Leaves>([&ok]<typename L>() {
    using F = typename L::Field;
    if constexpr (Nested)
      if (Fmt::tag<F>() == Tag || Fmt::payloadTag<F>() == Tag) ok = false;
    if constexpr (Fmt::GroupField<F, Facet>{})
      if (!rootSelector<Fmt::GroupElem<typename F::T>, Facet, Tag, true>()) ok = false;
  });
  return ok;
}
} // Zfix::Catalog_

// Entries carry explicit application types and facets; BeginString never
// selects a business dictionary. Dispatch is supplied by the typed API stage.
template <typename ...Entries> struct MTList {
  using Messages = ZuTypeList<Entries...>;
  template <typename E> using Contains = ZuTypeIn<E, Messages>;

  static consteval bool unique() {
    bool ok = ZuTypeUnique<Messages>::N == Messages::N;
    ZuUnroll::all<Messages>([&ok]<typename L>() {
      using Replies = typename L::Responses;
      if constexpr (L::Discrim::Tag)
	if (!Catalog_::rootSelector<typename L::Object, typename L::Mapping,
	    L::Discrim::Tag>()) ok = false;
      if (!L::MsgType.length() || ZuTypeUnique<Replies>::N != Replies::N) ok = false;
      ZuUnroll::all<Messages>([&ok]<typename R>() {
	if constexpr (ZuTypeIndex<L, Messages>{} < ZuTypeIndex<R, Messages>{}) {
	  using LS = typename L::Discrim;
	  using RS = typename R::Discrim;
	  if (L::MsgType == R::MsgType &&
	      (!LS::Tag || LS::Tag != RS::Tag || LS::Value == RS::Value)) ok = false;
	}
      });
    });
    return ok;
  }

  ZuAssert(unique());
};

} // Zfix

#endif /* Zfix_HH */
