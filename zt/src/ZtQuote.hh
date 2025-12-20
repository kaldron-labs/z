//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// string quoting and binary data base64 printing

#ifndef ZtQuote_HH
#define ZtQuote_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuHex.hh>

#include <zlib/ZmAlloc.hh>

namespace ZtQuote {

// C string quoting
struct CString {
  const char *v;
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const CString &print) {
    const char *v = print.v;
    s << '"';
    if (v) {
      char c;
      for (unsigned i = 0; c = v[i]; i++) {
	if (ZuUnlikely(c == '"')) s << '\\';
	s << c;
      }
    }
    return s << '"';
  }
};

// string quoting
struct String {
  ZuCSpan v;
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const String &print) {
    const auto &v = print.v;
    s << '"';
    for (unsigned i = 0, n = v.length(); i < n; i++) {
      char c = v[i];
      if (ZuUnlikely(c == '"')) s << '\\';
      s << c;
    }
    return s << '"';
  }
};

// printing ZuBSpan in base32
struct Base32 {
  ZuBSpan v;
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const Base32 &print) {
    const auto &v = print.v;
    auto n = ZuBase32::enclen(v.length());
    auto buf_ = ZmAlloc(uint8_t, n);
    ZuSpan<uint8_t> buf(&buf_[0], n);
    buf.trunc(ZuBase32::encode(buf, v));
    return s << ZuCSpan(buf);
  }
};

// printing ZuBSpan in base64
struct Base64 {
  ZuBSpan v;
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const Base64 &print) {
    const auto &v = print.v;
    auto n = ZuBase64::enclen(v.length());
    auto buf_ = ZmAlloc(uint8_t, n);
    ZuSpan<uint8_t> buf(&buf_[0], n);
    buf.trunc(ZuBase64::encode(buf, v));
    return s << ZuCSpan(buf);
  }
};

// printing ZuBSpan in hex
struct Hex {
  ZuBSpan v;
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const Hex &print) {
    const auto &v = print.v;
    auto n = ZuHex::enclen(v.length());
    auto buf_ = ZmAlloc(uint8_t, n);
    ZuSpan<uint8_t> buf(&buf_[0], n);
    buf.trunc(ZuHex::encode(buf, v));
    return s << ZuCSpan(buf);
  }
};

} // ZtQuote

#endif /* ZtQuote_HH */
