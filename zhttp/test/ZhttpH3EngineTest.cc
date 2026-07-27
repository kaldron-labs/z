//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZhttpEngineFixture.hh"

int main(int argc, char **argv)
{
  using namespace ZhttpH1EngineTest_;

  parse(argc, argv);
  ZiLog::init("ZhttpH3EngineTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  TempDir temp;
  ZuTestMain();
  ZuCHECK(temp.init(), "temporary certificate creation failed");
  if (temp.certPath && temp.keyPath) {
    ZuTestCall(run<Zhttp::QUIC>, temp, 1U, 2U);
    ZuTestCall(run<Zhttp::QUIC>, temp, 1U, 2U);
    ZuTestCall(run<Zhttp::QUIC>, temp, 1U, 2U, true);
    ZuTestCall(runServerStop<Zhttp::QUIC>, temp);
  }

  ZiLog::stop();
  return 0;
}
