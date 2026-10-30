//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Application-owned queue and recovery hook boundary

#ifndef ZfixHooks_HH
#define ZfixHooks_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZfixNotice.hh>
#include <zlib/ZfixTxMsg.hh>

namespace Zfix {

// Fixed parsed values borrow one immutable pooled backing across owner posts.
// context identifies the received Session::Reject; message.refSeqNo names the outbound
// association within that epoch, possibly from an earlier physical connection.
struct PeerReject {
  ZmRef<const ZiIOBuf>	buffer;
  TxContext		context;
  Session::Reject	message;
  ZuCSpan		wire;
  bool			possDup = false;
};

// Optional Tx-owned application queue. No queue or retention policy is supplied
// by the library. Off-owner queues hand prepared candidates to this owner.
template <typename App> class Hooks {
public:
  bool enqueueTx_(TxBody::Ref) { return false; }
  TxBody::Ref dequeueTx_() { return {}; }
  void selectedTx_(TxBody::Ref, TxContext, unsigned) { }
  void rejectedTx_(PeerReject) { }
  void sessionTx_(TxBody::Ref, TxContext) { }
};

}

#endif /* ZfixHooks_HH */
