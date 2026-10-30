//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixClient.hh>

struct Order {
  ZuCSpan	id;
};

ZfStruct(, (Order, FIX), (id, (FIX::ID<11>), String));
using Allowed = Zfix::MT<Order, "8">;
using Foreign = Zfix::MT<Order, "X">;
using Undeclared = Zfix::MT<Order, "9">;
using Request = Zfix::MT<Order, "D", ZuFacet::FIX, Zfix::NoDiscriminator,
  ZuTypeList<Allowed, Foreign>>;
using Dictionary = Zfix::MTList<Request, Allowed, Undeclared>;
struct Hub : Zfix::Client<Hub, Dictionary> { struct Link; };

struct Hub::Link : Zfix::CliLink<Hub, Link, Dictionary> { };

#if ZFIX_REPLY_CASE == 1
using Response = Undeclared;
#elif ZFIX_REPLY_CASE == 2
using Response = Foreign;
#else
using Response = Allowed;
#endif
void check(Hub::Link *link, const Zfix::RequestContext &context) {
  Zfix::Fmt::Error error;
  bool admitted = link->reply<Request, Response>(context, Order{"id"}, 42, error);
  (void)admitted;
}
