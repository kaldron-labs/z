//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZdbusAuth.hh>

using namespace ZuTestUtil;

static void external()
{
  ZuTestScope(external);
  Zdbus_::Auth auth;
  Zdbus_::Auth::String output;
  auth.start(output, 1000);
  ZuCheck(ZuBSpan(output).length() == 25);
  ZuCheck(ZuBSpan(output)[0] == 0);
  ZuCheck((ZuCSpan{output.data() + 1, output.length() - 1} ==
    "AUTH EXTERNAL 31303030\r\n"));
  ZuCheck(auth.state() == Zdbus_::AuthState::Waiting);

  ZuCheck(auth.feed("OK 0123456789abcdef") == 19);
  ZuCheck(auth.state() == Zdbus_::AuthState::Waiting);
  auto tail = ZuBSpan{"0123456789abcdef\r\nDATA", 22};
  ZuCheck(auth.feed(tail) == 18);
  ZuCheck(auth.state() == Zdbus_::AuthState::Ready);
  ZuCheck((ZuBSpan{tail.begin() + 18, 4} == "DATA"));
  ZuCheck(Zdbus_::Auth::begin() == "BEGIN\r\n");
}

static void invalid()
{
  ZuTestScope(invalid);
  Zdbus_::Auth auth;
  Zdbus_::Auth::String output;
  auth.start(output, 0);
  ZuCheck(auth.feed("REJECTED EXTERNAL\r\n") == 19);
  ZuCheck(auth.state() == Zdbus_::AuthState::Failed);

  auth.start(output, 0);
  ZuCheck(auth.feed("OK 00\n") == 6);
  ZuCheck(auth.state() == Zdbus_::AuthState::Failed);

  auth.start(output, 0);
  uint8_t nul[] = {'O', 'K', 0};
  ZuCheck(auth.feed(nul) == 3);
  ZuCheck(auth.state() == Zdbus_::AuthState::Failed);

  auth.start(output, 0);
  ZuCheck(auth.feed("OK xyz\r\n") == 8);
  ZuCheck(auth.state() == Zdbus_::AuthState::Failed);

  auth.start(output, 0);
  ZuCheck(auth.feed("OK 0123\r\n") == 9);
  ZuCheck(auth.state() == Zdbus_::AuthState::Failed);

  auth.start(output, 0);
  Zdbus_::Auth::String longLine;
  for (unsigned i = 0; i <= Zdbus_::Auth::MaxLine; ++i)
    longLine << 'x';
  ZuCheck(auth.feed(ZuBSpan{longLine}) == longLine.length());
  ZuCheck(auth.state() == Zdbus_::AuthState::Failed);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(external);
  ZuTestCall(invalid);
  return 0;
}
