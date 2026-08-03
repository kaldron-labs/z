//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZhttpHubFixture.hh"

int main(int argc, char **argv)
{
  using namespace ZhttpH1HubTest_;

  parse(argc, argv);
  ZiLog::init("ZhttpH1HubTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  TempDir temp;
  TempDir otherCA;
  ZuTestMain();
  ZuCHECK(temp.init(), "temporary certificate creation failed");
  ZuCHECK(otherCA.init(), "second temporary certificate creation failed");
  if (temp.certPath && temp.keyPath &&
      otherCA.certPath && otherCA.keyPath) {
    ZuTestCall(run<Zhttp::H1TCP>, temp);
    ZuTestCall(run<Zhttp::H1TCP>, temp);
    ZuTestCall(run<Zhttp::H1TCP>, temp);
    ZuTestCall(runServerStop<Zhttp::H1TCP>, temp);
    ZuTestCall(runUpgrade<Zhttp::H1TCP>, temp);
    ZuTestCall(run<Zhttp::H1TLS>, temp);
    ZuTestCall(run<Zhttp::H1TLS>, temp);
    ZuTestCall(run<Zhttp::H1TLS>, temp);
    ZuTestCall(runServerStop<Zhttp::H1TLS>, temp);
    ZuTestCall(runUpgrade<Zhttp::H1TLS>, temp);
    ZuTestCall(testTLSFailure, temp, otherCA);
  }

  ZiLog::stop();
  return 0;
}
