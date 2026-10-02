//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuSemVer.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuTestUtil.hh>

using namespace ZuTestUtil;

void version()
{
  ZuTestScope(version);
  ZuCheck(sizeof(ZuSemVer) == sizeof(uint32_t));
  ZuSemVer v{1000000};
  ZuCheck(v.major() == 10 && v.minor() == 0 && v.patch() == 0);
  ZuCArray<3 * Zu_ulen<unsigned>() + 3> s;
  s << v;
  ZuCheck(s == "10.0.0");
  ZuCheck(v.scan("12.34.567") == 9);
  ZuCheck(v == ZuSemVer(12, 34, 567));
  ZuCheck(v.value() == 1234567);
  ZuCheck(v > ZuSemVer(12, 34, 566));
  ZuCheck(v < ZuSemVer(12, 35, 0));
  ZuCheck(v.hash() == ZuSemVer("12.34.567").hash());
  v = "0.0.0";
  ZuCheck(v.value() == 0);
  ZuCheck(v.scan("42949.67.295") == 12);
  ZuCheck(v.value() == UINT32_MAX);
  s.length(0);
  s << v;
  ZuCheck(s == "42949.67.295");
  for (auto bad : {"", "1", "1.2", "1..3", "-1.2.3", "1.100.0",
      "1.0.1000", "42949.67.296", "42950.0.0", "999999999999.0.0"}) {
    ZuCHECK(!v.scan(bad), bad);
    ZuCHECK(v.value() == UINT32_MAX, bad);
  }
  ZuCheck(v.scan(ZuCSpan{"1.2.3/end", 9}) == 5);
  ZuCheck(v == ZuSemVer(1, 2, 3));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(version);
}
