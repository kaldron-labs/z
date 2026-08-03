//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZGtkValue.hh>

using namespace ZuTestUtil;

static void value()
{
  ZuTestScope(value);
  ZGtk::Value v{G_TYPE_INT};
  v.set_int(42);
  ZuCHECK(v.get_int() == 42, "integer value round trip");
  v.unset();
  ZuCHECK(!G_VALUE_TYPE(&v), "unset clears the type");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(value);
  return 0;
}
