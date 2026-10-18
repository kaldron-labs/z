//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Shared HTTP request and response streaming producer state

#ifndef ZhttpStreamState_HH
#define ZhttpStreamState_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZmThread.hh>
#include <zlib/ZiAssert.hh>

namespace Zhttp {

struct StreamState {
  enum {
    Initial, InitialWriting, InitialOpen, InitialFinal, InitialTerminal,
    Writing, Open, Final, Terminal
  };
};

namespace Stream_ {

inline bool streamTxThread(int txThread)
{
  bool valid = ZmSelf() &&ZmSelf()->sid() == txThread;
  ZiAssert(valid, "Zhttp", (), "body stream called outside its Tx shard",
    return false);
  return valid;
}

inline bool streamEnter(int8_t &state, bool &beginHeaders)
{
  switch (state) {
    case StreamState::Initial:
      state = StreamState::InitialWriting;
      beginHeaders = true;
      return true;
    case StreamState::InitialOpen:
      state = StreamState::InitialWriting;
      beginHeaders = false;
      return true;
    case StreamState::Open:
      state = StreamState::Writing;
      beginHeaders = false;
      return true;
    case StreamState::InitialWriting:
    case StreamState::Writing:
      ZiAssert(false, "Zhttp", (), "reentrant stream emission", return false);
      return false;
    case StreamState::InitialFinal:
    case StreamState::Final:
    case StreamState::InitialTerminal:
    case StreamState::Terminal:
      return false;
    default:
      ZiAssert(false, "Zhttp", (), "invalid stream state", return false);
      return false;
  }
}

inline bool streamContentLength()
{
  ZiAssert(false, "Zhttp", (),
    "streaming body supplied Content-Length", return false);
  return false;
}

inline bool streamBodyAbsent(bool optional)
{
  ZiAssert(optional, "Zhttp", (),
    "required streaming body emitted no writer", return false);
  return optional;
}

} // Stream_
} // Zhttp

#endif /* ZhttpStreamState_HH */
