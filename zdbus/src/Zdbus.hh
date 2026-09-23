//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Shared Linux D-Bus client and service types

#ifndef Zdbus_HH
#define Zdbus_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <string.h>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfDBUS.hh>
#include <zlib/ZdbusConnection.hh>
#include <zlib/ZdbusCatalog.hh>

namespace Zdbus_ {

namespace ClientError {
  enum { None, Build, Queue, Timeout, Cancelled, Disconnect, Stopped };
}

namespace ClientDefault {
  enum {
    PendingLimit = 1024,
    SubLimit = 1024,
    SignalBatch = 32
  };
}

struct ClientParams {
  CxnParams	cxn;
  unsigned	pendingLimit = ClientDefault::PendingLimit;
  unsigned	subLimit = ClientDefault::SubLimit;
  unsigned	signalBatch = ClientDefault::SignalBatch;
};

struct CallResult {
  ZmRef<ZiIOBuf>	frame;
  FrameInfo		info;
  int			error = ClientError::None;

  explicit operator bool() const { return error == ClientError::None; }
};

using BuildFn = ZmFn<BuildResult(uint32_t),
  ZmFnHeapID<"Zdbus.BuildFn">>;
using CallFn = ZmFn<void(CallResult), ZmFnHeapID<"Zdbus.CallFn">>;
using ReadyFn = ZmFn<void(ZuCSpan), ZmFnHeapID<"Zdbus.ReadyFn">>;
using SignalFn = ZmFn<void(ZmRef<ZiIOBuf>, FrameInfo),
  ZmFnHeapID<"Zdbus.SignalFn">>;
using MethodFn = ZmFn<void(ZmRef<ZiIOBuf>, FrameInfo),
  ZmFnHeapID<"Zdbus.MethodFn">>;
using SubDoneFn = ZmFn<void(uint64_t), ZmFnHeapID<"Zdbus.SubDoneFn">>;

template <ZuString HeapID>
struct PathKeyText {
  using Text = ZtString<ZtStringHeapID<HeapID>>;

  Text		text;
  unsigned	pathLen;
  unsigned	ifLen;

  static bool fits(ZuCSpan path, ZuCSpan interface, ZuCSpan member) {
    return path.length() <= Wire::MaxMessageSize &&
      interface.length() <= Wire::MaxMessageSize - path.length() &&
      member.length() <= Wire::MaxMessageSize - path.length() -
        interface.length();
  }
  PathKeyText(ZuCSpan path, ZuCSpan interface, ZuCSpan member)
  : pathLen{unsigned(path.length())}, ifLen{unsigned(interface.length())} {
    ZmAssert_(fits(path, interface, member));
    uint64_t n = pathLen + ifLen + member.length();
    text.length(n);
    auto p = text.data();
    if (pathLen) memcpy(p, path.data(), pathLen);
    if (ifLen) memcpy(p + pathLen, interface.data(), ifLen);
    if (member.length())
      memcpy(p + pathLen + ifLen, member.data(), member.length());
  }
  ZuCSpan path() const { return {text.data(), pathLen}; }
  ZuCSpan interface() const { return {text.data() + pathLen, ifLen}; }
  ZuCSpan member() const {
    return {text.data() + pathLen + ifLen,
      text.length() - pathLen - ifLen};
  }
};

template <typename Req, typename Fn>
CallFn typedCall(Fn &&fn) {
  return CallFn{[fn = ZuFwd<Fn>(fn)](CallResult result) mutable {
    dispatchReply<Req>(ZuMv(result.frame), result.info, result.error, fn);
  }};
}

template <typename Sig, typename Fn>
SignalFn typedSignal(Fn &&fn) {
  return SignalFn{[fn = ZuFwd<Fn>(fn)](ZmRef<ZiIOBuf> frame,
      FrameInfo info) mutable {
    parseTyped<Sig>(ZuMv(frame), info, fn);
  }};
}

template <typename Schema>
struct FixedRoute {
  using Headers = typename Schema::HeaderCatalog;
  using Path = FixedHeader<Header::Path, Headers>;
  using Interface = FixedHeader<Header::Interface, Headers>;
  using Member = FixedHeader<Header::Member, Headers>;

  ZuAssert((!ZuIsSame<Path, ZuVoid>{}));
  ZuAssert((!ZuIsSame<Interface, ZuVoid>{}));
  ZuAssert((!ZuIsSame<Member, ZuVoid>{}));
  ZuAssert((HeaderCount<Header::Path, Headers> == 1));
  ZuAssert((HeaderCount<Header::Interface, Headers> == 1));
  ZuAssert((HeaderCount<Header::Member, Headers> == 1));

  static ZuCSpan path() { return Path{}(); }
  static ZuCSpan interface() { return Interface{}(); }
  static ZuCSpan member() { return Member{}(); }
};

} // Zdbus_

#endif /* Zdbus_HH */
