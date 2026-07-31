//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtRegex.hh>

using namespace ZuTestUtil;

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  // match, search/replace
  {
    ZtString<> x = "/foo/bar/bah/leaf";
    const auto &leafName = ZtREGEX("[^/]+$");
    const auto &separator = ZtREGEX("/");
    ZtRegexSplitCaptures(c, 1);

    int i = leafName.m(x, c);
    ZuCheck(i > 0);
    ZuCheck(c.length() > 0);

    i = separator.sg(x, ":");
    ZuCheck(i == 4);
    ZuCheck(x == ":foo:bar:bah:leaf");

    c.length(0);
    i = separator.split("/foo/bar/bah/leaf", c);
    ZuCheck(i == 5);
    ZuCheck(c.length() == 5);
    ZuCheck(c[1] == "foo");
    ZuCheck(c[4] == "leaf");
  }
  // character classes
  {
    const auto &space = ZtREGEX("[[:space:]]+");
    ZtString<> x = "  foo \t bar \n";
    int i = space.sg(x, "");
    ZuCheck(i > 0);
    ZuCheck(x == "foobar");
  }
  // named captures
  {
    const auto &r = ZtREGEX("([^ ]+) (?<who>[^ ]+) (?<age>[0-9]+)");
    ZtRegexCaptures(d, 3);
    int who = r.index("who");
    int age = r.index("age");
    int i = r.m("foo Joe 42", d);
    ZuCheck(i >= 3);
    ZuCheck(d[who] == "Joe");
    ZuCheck(d[age] == "42");
  }
  return 0;
}
