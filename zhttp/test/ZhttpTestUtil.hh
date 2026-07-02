//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZhttpTestUtil_HH
#define ZhttpTestUtil_HH

#include "ZquicInteropTest.hh"

namespace Zhttp::Test {

using Zquic::Test::TempDir;
using Zquic::Test::haveCurlH3;
using Zquic::Test::loopbackPort;
using Zquic::Test::printFile;
using Zquic::Test::runCurlH3;
using Zquic::Test::sleepMS;
using Zquic::Test::systemOK;
using Zquic::Test::waitUntil;
using Zquic::Test::writeSelfSignedLocalhostCert;

template <typename L>
bool retry(unsigned attempts, unsigned delayMS, L l)
{
  for (unsigned i = 0; i < attempts; ++i) {
    if (l()) return true;
    sleepMS(delayMS);
  }
  return false;
}

inline bool runCurlH3Retry(
  const TempDir &temp, unsigned port, ZuCSpan path, ZuCSpan expectedBody,
  unsigned attempts = 5, unsigned delayMS = 100)
{
  return retry(attempts, delayMS, [&temp, port, path, expectedBody]() {
    return runCurlH3(temp, port, path, expectedBody);
  });
}

} // namespace Zhttp::Test

#endif /* ZhttpTestUtil_HH */
