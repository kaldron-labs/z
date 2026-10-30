//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixClient.hh>

struct Order {
  ZuCSpan	id;
};

ZfStruct(, (Order, FIX), (id, (FIX::ID<11>), String));
using Allowed = Zfix::MT<Order, "D">;
using Foreign = Zfix::MT<Order, "8">;
using Dictionary = Zfix::MTList<Allowed>;
struct Hub : Zfix::Client<Hub, Dictionary> { struct Link; };

struct Hub::Link : Zfix::CliLink<Hub, Link, Dictionary> { };

void reject(Hub::Link *link) {
  Zfix::Fmt::Error error;
  bool admitted = link->send<Foreign>(Order{"id"}, 42, error);
  (void)admitted;
}
