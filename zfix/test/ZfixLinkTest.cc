//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zfix.hh>
#include <zlib/ZfixClient.hh>
#include <zlib/ZfixHooks.hh>
#include <zlib/ZfixSession.hh>
#include <zlib/ZfixServer.hh>
#include <zlib/ZfixLinkState.hh>

using namespace ZuTestUtil;

struct Order { };

using Entry = Zfix::MT<Order, "D">;
using MTList = Zfix::MTList<Entry>;
ZuAssert(MTList::Messages::N == 1);
ZuAssert((ZuIsSame<ZuType<0, MTList::Messages>, Entry>{}));
using Fill = Zfix::MT<Order, "8", ZuFacet::FIX, Zfix::Discriminator<150, "F">>;
using Cancel = Zfix::MT<Order, "8", ZuFacet::FIX, Zfix::Discriminator<150, "4">>;
using Request = Zfix::MT<Order, "D", ZuFacet::FIX, Zfix::NoDiscriminator,
  ZuTypeList<Fill, Cancel>>;
using Selected = Zfix::MTList<Request, Fill, Cancel>;
ZuAssert(Selected::Messages::N == 3);
ZuAssert((Selected::Contains<Fill>{}));
ZuAssert((!Selected::Contains<Entry>{}));
ZuAssert((ZuIsSame<Request::Responses, ZuTypeList<Fill, Cancel>>{}));
ZuAssert(Fill::Discriminator::tag == 150);
ZuAssert(Cancel::Discriminator::value == "4");


// Adapter bases must stay stateless: native Rx state belongs inside the engine,
// before its Tx group, including when TCP/TLS and the engine use MI.
struct LayoutLink;
template <typename Net> static void layout() {
  ZuTestScope(layout);
  using Hooks = Zfix::LinkHooks_<LayoutLink, Net>;
  using Input = Zfix::NetInput<LayoutLink, typename Net::RxStream>;
  ZuCheck(ZuTraits<Hooks>::IsEmpty);
  ZuCheck(ZuTraits<Input>::IsEmpty);
}


int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuCheck(ZfixLib[0] == '@');
  ZuCheck(ZfixFmtLib[0] == '@');
  ZuCheck(Entry::msgType == "D");
  ZuTestCall(layout<Zfix::TCP>);
  ZuTestCall(layout<Zfix::TLS>);
  return 0;
}
