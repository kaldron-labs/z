//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// D-Bus unix: address decoding and AF_UNIX sockaddr conversion

#ifndef ZdbusAddress_HH
#define ZdbusAddress_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <sys/socket.h>
#include <sys/un.h>

#include <zlib/ZuSpan.hh>
#include <zlib/ZtString.hh>

namespace Zdbus_ {

namespace AddrKind { enum { Invalid, Path, Abstract }; }

struct Address {
  using Text = ZtString<ZtStringHeapID<"Zdbus.Address">>;

  Text		value;
  uint8_t	kind = AddrKind::Invalid;

  explicit operator bool() const { return kind != AddrKind::Invalid; }

  ZdbusAPI static bool parse(Address &, ZuCSpan);
  ZdbusAPI static bool session(Address &);
  ZdbusAPI static bool system(Address &);

  ZdbusAPI bool socketAddr(sockaddr_un &, socklen_t &) const;
};

} // Zdbus_

using ZdbusAddress = Zdbus_::Address;

#endif /* ZdbusAddress_HH */
