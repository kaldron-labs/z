//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Incremental D-Bus client EXTERNAL authentication

#ifndef ZdbusAuth_HH
#define ZdbusAuth_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

namespace Zdbus_ {

namespace AuthState { enum { Initial, Waiting, Ready, Failed }; }

class ZdbusAPI Auth {
public:
  ZuDerive(Text, (ZtString<ZtStringHeapID<"Zdbus.Auth">>));
  ZuDerive(UIDText, (ZtString<ZtStringHeapID<"Zdbus.AuthUID">>));

  // The auth reply is one short ASCII line; its GUID is 16 hex-encoded bytes.
  enum { MaxLine = 4096, GuidChars = 32 };

  template <typename Sink> void start(Sink &sink, unsigned uid) {
    m_line.length(0);
    m_state = AuthState::Waiting;
    auto boxed = ZuBoxed(uid);
    auto decimal = ZtScratch(UIDText, boxed.length());
    decimal << boxed;
    sink << char(0) << "AUTH EXTERNAL ";
    constexpr auto digits = "0123456789abcdef"_Zu;
    for (unsigned i = 0, n = decimal.length(); i < n; ++i) {
      uint8_t c = decimal[i];
      sink << digits[c >> 4] << digits[c & 15];
    }
    sink << "\r\n";
  }

  ZdbusAPI unsigned feed(ZuBSpan);
  int state() const { return m_state; }
  static ZuCSpan begin() { return "BEGIN\r\n"; }

private:
  bool reply_();

private:
  Text	m_line;
  int	m_state = AuthState::Initial;
};

} // Zdbus_

#endif /* ZdbusAuth_HH */
