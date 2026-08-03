//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZcmdDispatcher.hh>

using namespace ZuTestUtil;

static void dispatch()
{
  ZuTestScope(dispatch);
  Zcmd::Dispatcher dispatcher;
  dispatcher.init();
  ZuCHECK(dispatcher.dispatch("missing", nullptr, {}, {}) == -1,
      "missing command rejected");
  dispatcher.deflt([](void *, ZuCSpan id, ZmRef<ZiIOBuf>, ZuBSpan) {
    return id == "fallback" ? 7 : 0;
  });
  ZuCHECK(dispatcher.dispatch("fallback", nullptr, {}, {}) == 7,
      "default command dispatched");
  dispatcher.final();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(dispatch);
  return 0;
}
