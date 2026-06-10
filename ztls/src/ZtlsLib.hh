//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// zpicotls/OpenSSL C++ wrapper

#ifndef ZtlsLib_HH
#define ZtlsLib_HH

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZTLS_EXPORTS
#define ZtlsAPI ZuExport_API
#define ZtlsExplicit ZuExport_Explicit
#else
#define ZtlsAPI ZuImport_API
#define ZtlsExplicit ZuImport_Explicit
#endif
#define ZtlsExtern extern ZtlsAPI

#else

#define ZtlsAPI
#define ZtlsExplicit
#define ZtlsExtern extern

#endif

#include <zlib/ZtlsBackend.hh>

#include <zlib/ZmSpecific.hh>

#include <zlib/ZtString.hh>

namespace Ztls {

ZtlsExtern void init(); // can be called repeatedly

struct StrError { char buf[200]; };

ZuInline static constexpr bool isspace__(char c) {
  return ((c >= '\t' && c <= '\r') || c == ' ');
}

inline ZuCSpan strerror_(int e) {
  char *buf = ZmTLS<StrError>().buf;
  constexpr unsigned N = sizeof(StrError{}.buf);
  unsigned n = Backend::format_error(e, buf, N);
  for (unsigned i = 0; i < N; i++) if (!buf[i]) { n = i; break; }
  while (n) if (!isspace__(buf[--n])) { ++n; break; }
  return {&buf[0], n};
}

}

#endif /* ZtlsLib_HH */
