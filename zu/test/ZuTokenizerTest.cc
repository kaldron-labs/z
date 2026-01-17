//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuTokenizer.hh>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

int main()
{
  using namespace ZuTokenizer;

  ZuCSpan span;
  CHECK(Delimited<':'>::next(span = "") == "");
  CHECK(Delimited<':'>::next(span = ":") == "");
  CHECK(Delimited<':'>::next(span = "foo") == "foo");
  CHECK(Delimited<':'>::next(span = "foo:") == "foo");
  CHECK((WhiteSpace::skip(span = ""), span) == "");
  CHECK((WhiteSpace::skip(span = " "), span) == "");
  CHECK((WhiteSpace::skip(span = "  "), span) == "");
  CHECK((WhiteSpace::skip(span = "  foo"), span) == "foo");
  CHECK(WhiteSpace::next(span = "") == "");
  CHECK(WhiteSpace::next(span = " ") == "");
  CHECK(WhiteSpace::next(span = "foo  ") == "foo");
  CHECK(span == "");
  CHECK(WhiteSpace::next(span = "foo  bar ") == "foo");
  CHECK(span == "bar ");
}
