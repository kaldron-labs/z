//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Received FIX message backing, parsed values and operational queue node

#ifndef ZfixRxMsg_HH
#define ZfixRxMsg_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZmObject.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZfixFrame.hh>
#include <zlib/ZfixSession.hh>
#include <zlib/ZfixLinkState.hh>

namespace Zfix {

struct RxMsg {
  ZmRef<ZiIOBuf>	buffer;
  Fmt::FrameView	frame;
  Fmt::Header		header;
  Fmt::Trailer		trailer;
  Session::Msg		control;
  Fmt::Error		error;
  SessionFault		fault;
  ZuTime		received;
  ZuDateTime		utc;
  uint64_t		connection = 0;
  uint64_t		epoch = 0;
  uint64_t		charge = 0;
  bool			nativeMemory = false; // charge includes original allocator/node geometry
  bool			observed = false;
  bool			early = false;
  bool			logoutReply = false;

  uint64_t key() const { return header.seqNo; }
  uint64_t length() const { return 1; }
  uint64_t retainedBytes() const { return charge; }
};

using RxQueue = ZmPQueue<RxMsg, ZmPQueueHeapID<"Zfix.Rx",
  ZmPQueueNode<ZmObject, ZmPQueueOverlap<false, ZmPQueueOverwrite<false>>>>>;
} // Zfix

#endif /* ZfixRxMsg_HH */
