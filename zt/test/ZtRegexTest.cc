//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdio.h>

#include <zlib/ZtString.hh>
#include <zlib/ZtRegex.hh>

int main()
{
  ZtString<> x = "/foo/bar/bah/leaf";
  const auto &leafName = ZtREGEX("[^/]+$");
  const auto &separator = ZtREGEX("/");
  const auto &nullSpace = ZtREGEX("\s*");
  ZtRegexCaptures(c, 0);
  printf("x is \"%s\"\n", x.data());
  int i = leafName.m(x, c);
  printf("m/[^\\/]+$/ returned %d\n", i);
  printf("c.length() is %d\n", int(c.length()));
  int n = c.length();
  for (i = 0; i < n; i++)
    printf("c[%d] = \"%.*s\"\n", i, int(c[i].length()), c[i].data());
  i = separator.sg(x, "/");
  printf("s/\\//\\//g returned %d\n", i);
  printf("x is \"%s\"\n", x.data());
  c.length(0);
  i = separator.split(x, c);
  printf("split /\\// returned %d\n", i);
  printf("c.length() is %d\n", int(c.length()));
  n = c.length();
  for (i = 0; i < n; i++)
    printf("c[%d] = \"%.*s\"\n", i, int(c[i].length()), c[i].data());
  i = nullSpace.sg(x, "");
  printf("s/\\s*//g returned %d\n", i);
  printf("x is \"%s\"\n", x.data());
  c.length(0);
  i = nullSpace.split(x, c);
  printf("split /\\s*/ returned %d\n", i);
  printf("c.length() is %d\n", int(c.length()));
  n = c.length();
  for (i = 0; i < n; i++)
    printf("c[%d] = \"%.*s\"\n", i, int(c[i].length()), c[i].data());
  
  {
    const auto &r = ZtREGEX("\w+\s+(?<name>\w+)\s+(?<age>\d+)");
    ZtRegexCaptures(d, 0);
    int name = r.index("name");
    int age = r.index("age");

    int i = r.m("foo Joe 42", d);
    if (i >= 3) {
      ZtString<> out;
      out << "name=" << d[name] << ", age=" << d[age];
      puts(out);
    }
  }
}
