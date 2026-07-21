//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuTokenizer.hh>

using namespace ZuTestUtil;

int main(int argc, char **argv)
{
  parse(argc, argv);

  using namespace ZuTokenizer;

  ZuTestMain();

  ZuCSpan span;
  ZuCheck(Delimited<':'>::next(span = "") == "");
  ZuCheck(Delimited<':'>::next(span = ":") == "");
  ZuCheck(Delimited<':'>::next(span = "foo") == "foo");
  ZuCheck(Delimited<':'>::next(span = "foo:") == "foo");
  ZuCheck(WhiteSpace::next(span = "") == "");
  ZuCheck(WhiteSpace::next(span = " ") == "");
  ZuCheck(WhiteSpace::next(span = "foo  ") == "foo");
  ZuCheck(span == "");
  ZuCheck(WhiteSpace::next(span = "foo  bar ") == "foo");
  ZuCheck(span == "bar ");

  // empty/consecutive/leading/trailing delimiter cases
  ZuCheck(Delimited<','>::next(span = "") == "");
  ZuCheck(Delimited<','>::next(span = ",a,,b,") == "");
  ZuCheck(Delimited<','>::next(span) == "a");
  ZuCheck(Delimited<','>::next(span) == "");
  ZuCheck(Delimited<','>::next(span) == "b");
  ZuCheck(Delimited<','>::next(span) == "");
  ZuCheck(Delimited<','>::next(span) == "");

  ZuCheck(WhiteSpace::next(span = "   ") == "");
  (span = "  a   b   c ").trim();
  ZuCheck(WhiteSpace::next(span) == "a");
  ZuCheck(WhiteSpace::next(span) == "b");
  ZuCheck(WhiteSpace::next(span) == "c");
  ZuCheck(WhiteSpace::next(span) == "");
  return 0;
}
