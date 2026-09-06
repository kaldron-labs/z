//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// bounded CBOR reader for WebAuthn and COSE

#ifndef ZfCBOR_HH
#define ZfCBOR_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <zlib/ZuSpan.hh>

namespace ZfCBOR {

namespace Type {
  enum { UInt, NInt, Bytes, Text, Array, Map, Bool, Null, Simple };
}

namespace Error {
  enum { OK, Syntax, Size, Depth, Items, Data, Unsupported, Stopped };
}

struct Limits {
  unsigned size;
  unsigned depth;
  unsigned items;
  unsigned data;
};

struct Item {
  int		type;
  uint64_t	value;
  ZuBSpan	data;
  ZuBSpan	encoded;
  unsigned	depth;
};

using Visit = bool (*)(void *, const Item &);

struct Result {
  unsigned	offset = 0;
  int		error = Error::Syntax;

  explicit operator bool() const { return error == Error::OK; }
};

ZfExtern Result scan(ZuBSpan, const Limits &, void *, Visit);

} // namespace ZfCBOR

#endif /* ZfCBOR_HH */
