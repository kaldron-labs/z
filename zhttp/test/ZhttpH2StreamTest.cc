//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 native Stream and queue-admission test

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmObject.hh>

#include <zlib/ZhttpH2Hub.hh>

using namespace ZuTestUtil;

namespace {

struct Logical : public ZmObject { };

void testQueueAdmission()
{
  ZuTestScope(testQueueAdmission);

  Zhttp::H2::QueueAdmission admission;
  admission.init(2);
  ZuCHECK(admission.push() && admission.push() && !admission.push() &&
      admission.count() == 2,
    "queued buffer references are explicitly bounded");
  admission.pop(2);
  ZuCHECK(!admission.count(), "queued buffer admission drains exactly");
}

void testStreamRegistry()
{
  ZuTestScope(testStreamRegistry);

  using Stream = Zhttp::H2_::Stream<Logical>;
  using Streams = Zhttp::H2_::StreamHash<Logical>;

  ZmRef<Streams> streams = new Streams;
  ZmRef<Logical> logical = new Logical;
  auto stream = streams->add(Stream{1, logical, true});
  ZuCHECK(stream && stream->id == 1 && stream->local &&
      stream->logical.ptr() == logical.ptr() &&
      stream->rxWindow == Zhttp::H2::DefltWindow &&
      stream->txWindowHint == Zhttp::H2::DefltWindow,
    "native Stream record owns the logical Link and flow-control shadows");
  ZuCHECK(streams->findPtr(1) == stream,
    "native Stream is indexed by its Link-local ID");
  streams->delNode(static_cast<Streams::Node *>(stream));
  ZuCHECK(!streams->count_(), "native Stream removal is deterministic");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testQueueAdmission);
  ZuTestCall(testStreamRegistry);
}
