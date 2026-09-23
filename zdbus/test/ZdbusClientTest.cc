//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZdbusClient.hh>

#include "ZdbusSerial.hh"

using namespace ZuTestUtil;

namespace Zdbus_ {

static void packedKey()
{
  ZuTestScope(packedKey);
  PathKeyText<"Zdbus.TestKey"> key{"/a/b", "org.example.Test", "Run"};
  ZuCheck(key.path() == "/a/b");
  ZuCheck(key.interface() == "org.example.Test");
  ZuCheck(key.member() == "Run");
  ZuCheck(key.text.length() == key.path().length() +
    key.interface().length() + key.member().length());
}

static void serialWrap()
{
  ZuTestScope(serialWrap);
  uint32_t next = UINT32_MAX;
  auto busy = [](uint32_t serial) { return serial == UINT32_MAX; };
  ZuCheck(serialProbe(next, 2, busy) == 1);
  ZuCheck(next == 2);

  next = UINT32_MAX - 1;
  auto threeBusy = [](uint32_t serial) {
    return serial >= UINT32_MAX - 1 || serial == 1;
  };
  ZuCheck(serialProbe(next, 4, threeBusy) == 2);
  ZuCheck(next == 3);

  next = 7;
  ZuCheck(!serialProbe(next, 3, [](uint32_t) { return true; }));
  ZuCheck(next == 10);
}

} // Zdbus_

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(Zdbus_::packedKey);
  ZuTestCall(Zdbus_::serialWrap);
  return 0;
}
