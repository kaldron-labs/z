//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZiNetlinkMsg.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

namespace {

void testGenericHeaderFields()
{
  ZuTestScope(testGenericHeaderFields);

  ZiGenericNetlinkHdr h(4, 10, NLM_F_REQUEST, 7, 9, 3);

  ZuCheck(h.hdrSize() == (NLMSG_HDRLEN + GENL_HDRLEN));
  ZuCheck(h.len() == (h.hdrSize() + 4));
  ZuCheck(h.dataLen() == (GENL_HDRLEN + 4));
  ZuCheck(h.type() == 10);
  ZuCheck(h.flags() == NLM_F_REQUEST);
  ZuCheck(h.seq() == 7);
  ZuCheck(h.pid() == 9);
  ZuCheck(h.cmd() == 3);
  ZuCheck(h.version() == ZiGenericNetlinkVersion);
}

void testAttrSizingAndTraversal()
{
  ZuTestScope(testAttrSizingAndTraversal);

  ZiNetlinkDataAttr a(3);
  ZuCheck(a.hdrLen() == NLA_HDRLEN);
  ZuCheck(a.dataLen() == 3);
  ZuCheck(a.len() == (NLA_HDRLEN + 3));
  ZuCheck(a.size() >= a.len());
  ZuCheck(a.type() == ZiGNLAttr_Data);

  alignas(ZiNetlinkAttr) char storage[256];
  auto *first = new (&storage[0]) ZiNetlinkDataAttr(5);
  auto *second = new (&storage[first->size()]) ZiNetlinkDataAttr(2);

  ZuCheck(first->next() == static_cast<ZiNetlinkAttr *>(second));
  ZuCheck(second->type() == ZiGNLAttr_Data);
}

void testFamilyNameTruncation()
{
  ZuTestScope(testFamilyNameTruncation);

  char longName[GENL_NAMSIZ * 2];
  for (unsigned i = 0; i < sizeof(longName) - 1; ++i)
    longName[i] = 'A' + (i % 26);
  longName[sizeof(longName) - 1] = 0;

  ZiNetlinkFamilyName attr(longName);

  ZuCheck(attr.type() == CTRL_ATTR_FAMILY_NAME);
  ZuCheck(attr.dataLen() == GENL_NAMSIZ);

  const char *p = attr.data();
  ZuCheck(p[GENL_NAMSIZ - 1] == 0);
}

} // namespace

int main(int argc, char **argv)
{
  ZiTestResidue::init("ZiNetlinkMsgTest");
  ZmTrap::sigintFn(&ZiTestResidue::cleanupNow);
  ZmTrap::trap();
  ::atexit(&ZiTestResidue::cleanupNow);

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testGenericHeaderFields);
  ZuTestCall(testAttrSizingAndTraversal);
  ZuTestCall(testFamilyNameTruncation);
  return 0;
}
