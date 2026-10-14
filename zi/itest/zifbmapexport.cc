//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "zflatcfixture.hh"
#include "zifbmapfixture.hh"

using namespace ZuTestUtil;
using FlatcFixture::Text;

static bool export_(ZuCSpan stem)
{
  MapFixture::Row row{new MapFixture::Ints(), {}, {}};
  row.ints->add(7, 70);
  row.texts.add("a", "first");
  MapFixture::Children children;
  children.add(9, new MapFixture::Child(71));
  row.nested.add(12, ZuMv(children));
  Text fbs, bfbs;
  ZiFB::saveFBS<MapFixture::Row>(fbs);
  ZiFB::saveSchema<MapFixture::Row>(bfbs);
  ZiFB::Buffer bytes;
  if (ZiFB::save(bytes, row) != ZiFB::Error::OK ||
      !ZiFB::verify<MapFixture::Row>(bytes)) return false;
  Text name{stem}; name << ".fbs";
  if (!FlatcFixture::write(name, fbs)) return false;
  name = stem; name << ".bfbs";
  if (!FlatcFixture::write(name, bfbs)) return false;
  name = stem; name << ".bin";
  return FlatcFixture::write(name, ZuCSpan{
    reinterpret_cast<const char *>(bytes.data()), bytes.length()});
}

static bool verify_(ZuCSpan path)
{
  Text bytes = FlatcFixture::read(path);
  if (!ZiFB::verify<MapFixture::Row>(ZuBSpan{bytes})) return false;
  auto row = ZiFB::load<MapFixture::Row>(ZuBSpan{bytes});
  return MapFixture::valid(row, 170);
}

int main(int argc, char **argv)
{
  bool ok = argc == 2 ? export_(argv[1]) :
    argc == 3 && ZuCSpan{argv[1]} == "--verify" ? verify_(argv[2]) : false;
  parse(1, argv);
  ZuTestMain();
  ZuCheck(ok);
  return ok ? 0 : 1;
}
