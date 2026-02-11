//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuTokenizer.hh>

int main()
{
  using namespace ZuTokenizer;

  ZuTestMain();

  ZuCSpan span;
  ZuCheck(Delimited<':'>::next(span = "") == "");
  ZuCheck(Delimited<':'>::next(span = ":") == "");
  ZuCheck(Delimited<':'>::next(span = "foo") == "foo");
  ZuCheck(Delimited<':'>::next(span = "foo:") == "foo");
  ZuCheck((WhiteSpace::skip(span = ""), span) == "");
  ZuCheck((WhiteSpace::skip(span = " "), span) == "");
  ZuCheck((WhiteSpace::skip(span = "  "), span) == "");
  ZuCheck((WhiteSpace::skip(span = "  foo"), span) == "foo");
  ZuCheck(WhiteSpace::next(span = "") == "");
  ZuCheck(WhiteSpace::next(span = " ") == "");
  ZuCheck(WhiteSpace::next(span = "foo  ") == "foo");
  ZuCheck(span == "");
  ZuCheck(WhiteSpace::next(span = "foo  bar ") == "foo");
  ZuCheck(span == "bar ");
}
