//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "zflatcfixture.hh"
#include "zflatcmapnative.hh"

using namespace ZuTestUtil;
using FlatcFixture::Text;

int main(int argc, char **argv)
{
  if (argc != 3) return 1;
  parse(1, argv);
  ZuTestMain();
  Text stem{argv[1]};
  Text name{stem}; name << ".bin";
  Text image = FlatcFixture::read(name);
  ZuBSpan bytes{image};
  ZuCheck(ZiFB::verify<exchange::Row>(bytes));
  auto row = ZiFB::load<exchange::Row>(bytes);
  ZuCheck(row.ints.findVal(7) == 70);
  ZuCheck(row.texts.findVal("a") == "first");
  ZuCheck(row.nested.find(12)->val().find(9)->val()->value == 71);
  row.ints.del(7);
  row.ints.add(7, 170);
  row.nested.find(12)->val().del(9);
  auto child = new exchange::Child();
  child->value = 171;
  row.nested.find(12)->val().add(9, child);
  ZiFB::Buffer out;
  ZuCheck(ZiFB::save(out, row) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<exchange::Row>(out));
  ZuCheck(FlatcFixture::write(argv[2], ZuCSpan{
    reinterpret_cast<const char *>(out.data()), out.length()}));
}
