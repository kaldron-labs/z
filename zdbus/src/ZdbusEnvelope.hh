//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// D-Bus frame boundaries and generic header-field validation

#ifndef ZdbusEnvelope_HH
#define ZdbusEnvelope_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZfDBUS.hh>

namespace Zdbus_ {

namespace Wire {
  enum {
    PrefixSize = 12,       // yyyyuu before the header-fields array
    MinHeaderSize = 16,    // prefix plus the array's uint32 length
    BodyAlignment = 8,     // mandated message-relative body alignment
    MaxMessageSize = 1U << 27, // D-Bus protocol maximum: 128 MiB
    Version = 1
  };
}

namespace MessageType {
  enum { MethodCall = 1, MethodReturn = 2, Error = 3, Signal = 4 };
}

namespace MessageFlag {
  enum { NoReplyExpected = 1, NoAutoStart = 2,
    AllowInteractiveAuth = 4 };
}

namespace HeaderCode {
  enum {
    Path = 1, Interface = 2, Member = 3, ErrorName = 4,
    ReplySerial = 5, Destination = 6, Sender = 7,
    Signature = 8, UnixFDs = 9
  };
}

namespace FrameError {
  enum {
    OK, NeedMore, Order, Type, Version, Serial, Size, Header, Padding,
    Required, Body, Unsupported
  };
}

// Borrowed header values remain valid while the retained frame remains alive.
struct HeaderView {
  ZuCSpan	path;
  ZuCSpan	interface;
  ZuCSpan	member;
  ZuCSpan	errorName;
  ZuCSpan	destination;
  ZuCSpan	sender;
  ZuCSpan	signature;
  uint32_t	replySerial = 0;
  uint32_t	unixFDs = 0;
};

struct FrameInfo {
  HeaderView	headers;
  unsigned	total = 0;
  unsigned	bodyOffset = 0;
  unsigned	bodyLength = 0;
  unsigned	fieldsLength = 0;
  uint32_t	serial = 0;
  uint8_t	order = 0;
  uint8_t	type = 0;
  uint8_t	flags = 0;
};

struct FrameResult {
  FrameInfo	info;
  unsigned	offset = 0;
  int		error = FrameError::NeedMore;

  explicit operator bool() const { return error == FrameError::OK; }
};

ZdbusExtern FrameResult frame(ZuBSpan, unsigned limit = Wire::MaxMessageSize);

} // Zdbus_

#endif /* ZdbusEnvelope_HH */
