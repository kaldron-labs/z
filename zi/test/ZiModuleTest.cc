//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiModule.hh>

using namespace ZuTestUtil;

void testLoadInvalidPathFails()
{
  ZuTestScope(testLoadInvalidPathFails);

  ZiModule mod;
  ZeString e;
  ZuCheck(mod.load("/definitely/not/a/real/library/path.so", ZiModule::GC, &e) == Zi::IOError);
  ZuCheck(!!e);
}

void testLoadResolveUnload()
{
  ZuTestScope(testLoadResolveUnload);

  ZiModule mod;
  ZeString e;

  ZuCheck(mod.load("libc.so.6", ZiModule::GC, &e) == Zi::OK);
  ZuCheck(!!mod.handle());

  void *printfSym = mod.resolve("printf", &e);
  ZuCheck(printfSym != nullptr);

  void *missingSym = mod.resolve("definitely_missing_symbol_zi_module_test", &e);
  ZuCheck(missingSym == nullptr);
  ZuCheck(!!e);

  ZuCheck(mod.unload(&e) == Zi::OK);
  ZuCheck(!mod.handle());

  // idempotent unload
  ZuCheck(mod.unload(&e) == Zi::OK);
}

void testFinalizeWithGCFlag()
{
  ZuTestScope(testFinalizeWithGCFlag);

  ZeString e;
  {
    ZiModule mod;
    ZuCheck(mod.load("libc.so.6", ZiModule::GC, &e) == Zi::OK);
    ZuCheck(!!mod.handle());
  }
  ZuCheck(true);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testLoadInvalidPathFails);
  ZuTestCall(testLoadResolveUnload);
  ZuTestCall(testFinalizeWithGCFlag);
  return 0;
}
