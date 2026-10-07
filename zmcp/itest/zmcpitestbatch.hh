//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZmcpITestBatch_HH
#define ZmcpITestBatch_HH

#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZjrpcPending.hh>

namespace ZmcpITest {

template <typename Heap = ZuVoid>
struct BatchCall_ : public Heap, public ZmObject {
  ZmSemaphore done;
  Zjrpc::BatchReply reply;
  unsigned completions = 0;
  unsigned failures = 0;

  void process(Zjrpc::BatchReply value) {
    reply = ZuMv(value);
    ++completions;
    done.post();
  }
  void failed() { ++failures; done.post(); }
  bool ids(int64_t first, int64_t second) const {
    if (!reply.parsed.batch() || reply.entries().length() != 2) return false;
    auto a = Zjrpc::decode(reply.entries()[0].ptr());
    auto b = Zjrpc::decode(reply.entries()[1].ptr());
    return a.kind == Zjrpc::MessageKind::Result && b.kind == Zjrpc::MessageKind::Result &&
      ((a.id() == Zjrpc::ID{first} && b.id() == Zjrpc::ID{second}) ||
	(a.id() == Zjrpc::ID{second} && b.id() == Zjrpc::ID{first}));
  }
};
using BatchCallHeap = ZmHeap<"ZmcpITest.Batch", BatchCall_<>>;
using BatchCall = BatchCall_<BatchCallHeap>;

} // ZmcpITest

#endif /* ZmcpITestBatch_HH */
