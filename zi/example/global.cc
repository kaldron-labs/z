//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuBox.hh>
#include <zlib/ZmSingleton.hh>

#include "global.hh"

Global *Global::instance()
{
  static constexpr auto ctor = [] { return new Global(); };
  return ZmSingleton<Global, ZmSingletonCtor<ctor>>::instance();
}

void Global::dumpStats_()
{
  std::cout <<
    "Recv " << ZuBoxed(m_recvRequests) << ':' << ZuBoxed(m_recvBytes) <<
    " Send " << ZuBoxed(m_sendRequests) << ':' << ZuBoxed(m_sendBytes) <<
    '\n';
}
