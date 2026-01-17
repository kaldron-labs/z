//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZtString.hh>

#include <zlib/ZiIOBuf.hh>

inline void out(const char *s) {
  std::cout << s << '\n' << std::flush;
}

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

int main()
{
  ZmRef<ZiIOBuf> buf = new ZiIOBufAlloc<>{};
  *buf << "fbah";
  ZtString<> s;
  s << ZuCSpan(buf->cspan());
  CHECK(s == "fbah");
  ++buf->skip, --buf->length;
  s.clear();
  s << ZuCSpan(buf->cspan());
  CHECK(s == "bah");

  buf->clear();
  *buf << "0123456789";
  buf->skip = 2;
  buf->length = 5;
  ZtString<> before;
  before << ZuCSpan(buf->cspan());
  auto old_skip = buf->skip;
  auto old_length = buf->length;
  CHECK(buf->ensure(buf->size + 128));
  CHECK(buf->skip == old_skip);
  CHECK(buf->length == old_length);
  ZtString<> after;
  after << ZuCSpan(buf->cspan());
  CHECK(after == before);
}
