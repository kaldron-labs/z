//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZdbusAdapter.hh>

namespace Zdbus_ {

bool setHeader(HeadSpec &head, unsigned code, ZuCSpan value,
  unsigned &seen)
{
  if (!value.length() || code > HeaderCode::Signature ||
      code == HeaderCode::ReplySerial || code == HeaderCode::Sender ||
      code == HeaderCode::Signature) return false;
  unsigned bit = 1U << code;
  if (seen & bit) return false;
  if (code == HeaderCode::Destination && head.destination.length())
    return false;
  seen |= bit;
  switch (code) {
    case HeaderCode::Path: head.path = value; return true;
    case HeaderCode::Interface: head.interface = value; return true;
    case HeaderCode::Member: head.member = value; return true;
    case HeaderCode::ErrorName: head.errorName = value; return true;
    case HeaderCode::Destination: head.destination = value; return true;
  }
  return false;
}

ZuCSpan headerValue(const HeaderView &headers, unsigned code)
{
  switch (code) {
    case HeaderCode::Path: return headers.path;
    case HeaderCode::Interface: return headers.interface;
    case HeaderCode::Member: return headers.member;
    case HeaderCode::ErrorName: return headers.errorName;
    case HeaderCode::Destination: return headers.destination;
    case HeaderCode::Sender: return headers.sender;
    case HeaderCode::Signature: return headers.signature;
  }
  return {};
}

} // Zdbus_
