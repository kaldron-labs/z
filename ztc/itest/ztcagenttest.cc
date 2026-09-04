//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiRing.hh>
#include <zlib/ZtcVer.hh>
#include <zlib/ZtcRing.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

class TestRing : public Ztc::Ring {
public:
  using Ztc::Ring::Ring;

  void dead()
  {
    rdrPID()[rdrID()] = uint32_t(INT_MAX);
  }

  void abandon()
  {
    ++ctrl()->rdrCount;
    rdrID(-1);
  }
};

static void portableBuild()
{
  ZuTestScope(portable_build);
  ZuCheck(Ztc::Ver::make(Z_VMAJOR, Z_VMINOR, Z_VPATCH) == Z_VERSION);
}

static void slowReader()
{
  ZuTestScope(slow_reader);
  enum { Size = 4096, Frame = 3000 };
  Zi::Name name = ZiTestResidue::uniqueName("slow-reader");
  ZiTestResidue::addShm(name);
  TestRing writer{ZiRingParams{name, Size}.timeout(1).killWait(0)};
  TestRing slow{ZiRingParams{name, 0}.timeout(1)};
  TestRing healthy{ZiRingParams{name, 0}.timeout(1)};
  bool ready = writer.open(TestRing::Write) == Zu::OK &&
    writer.reset() == Zu::OK;
  writer.close();
  ready = ready && writer.open(TestRing::Write) == Zu::OK &&
    slow.open(TestRing::Read) == Zu::OK &&
    slow.attach() == Zu::OK && healthy.open(TestRing::Read) == Zu::OK &&
    healthy.attach() == Zu::OK;
  ZuCheck(ready);
  if (!ready) return;

  void *ptr = writer.tryPush(Frame);
  ZuCheck(ptr);
  if (ptr) {
    memset(ptr, 0x5a, Frame);
    static_cast<Ztc::Hdr *>(ptr)->length = Frame - sizeof(Ztc::Hdr);
    writer.push2(ptr, Frame);
  }
  const void *read = healthy.shift();
  ZuCheck(read);
  if (read) healthy.shift2(Frame);
  ZuCheck(!writer.tryPush(Frame));

  slow.dead();
  ZuCheck(writer.kill() >= Frame);
  ptr = writer.tryPush(Frame);
  ZuCheck(ptr);
  if (ptr) {
    memset(ptr, 0xa5, Frame);
    static_cast<Ztc::Hdr *>(ptr)->length = Frame - sizeof(Ztc::Hdr);
    writer.push2(ptr, Frame);
  }
  read = healthy.shift();
  ZuCheck(read && static_cast<const uint8_t *>(read)[sizeof(Ztc::Hdr)] == 0xa5);
  if (read) healthy.shift2(Frame);

  slow.abandon();
  slow.close();
  healthy.detach();
  healthy.close();
  writer.close();
}

int main()
{
  ZiTestResidue::init("ztcagenttest");
  ZuTestMain();
  ZuTestCall(portableBuild);
  ZuTestCall(slowReader);
}
