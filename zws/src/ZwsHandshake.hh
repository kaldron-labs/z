//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// RFC 6455 opening-handshake utilities

#ifndef ZwsHandshake_HH
#define ZwsHandshake_HH

#ifndef ZwsLib_HH
#include <zlib/ZwsLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZtString.hh>

namespace Ztls { class Random; }

namespace Zws {

ZuDerive(
  HandshakeString, ZtString<ZtStringHeapID<"Zws.Handshake">>);

ZwsAPI bool nonce(Ztls::Random &, HandshakeString &);
ZwsAPI bool accept(HandshakeString &, ZuCSpan key);
ZwsAPI bool validKey(ZuCSpan);
ZwsAPI bool validAccept(ZuCSpan value, ZuCSpan key);
ZwsAPI bool token(ZuCSpan value, ZuCSpan expected);
ZwsAPI bool subprotocol(ZuCSpan offered, ZuCSpan selected);

} // namespace Zws

#endif /* ZwsHandshake_HH */
