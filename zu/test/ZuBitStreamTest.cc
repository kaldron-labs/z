//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <sstream>

#include <zlib/ZuBitStream.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuHex.hh>

using namespace ZuTestUtil;
using namespace ZuBitStream;

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  ZuArray<uint8_t, 100> buf;
  ZuCArray<200> hex;

  {
    LE::Writer o{buf.data(), buf.data() + buf.size()};

    o.out<3>(0x5);
    o.out<2>(0x1);
    o.out<3>(0x5);
    o.out(0x55, 8);
    o.out(0x555, 11);
    o.out(0x155, 9);
    o.out(0x555, 12);
    o.out(0x5555, 16);
    o.out(0x15555, 19);
    o.out(0x5555, 17);
    o.out(0x55555, 20);
    o.out<3>(0x5);
    o.out(0x1555555, 26);
    o.out(1, 1);

    o.finish();
    buf.length(o.pos() - buf.data());

    hex.length(ZuHex::encode({hex.data(), hex.size()}, buf));
    log(hex);

    LE::Reader i{buf.data(), buf.data() + buf.length()};

    ZuCHECK(i.in<3>() == 0x5);
    ZuCHECK(i.in<2>() == 0x1);
    ZuCHECK(i.in<3>() == 0x5);
    ZuCHECK(i.in(8) == 0x55);
    ZuCHECK(i.in(11) == 0x555);
    ZuCHECK(i.in(9) == 0x155);
    ZuCHECK(i.in(12) == 0x555);
    ZuCHECK(i.in(16) == 0x5555);
    ZuCHECK(i.in(19) == 0x15555);
    ZuCHECK(i.in(17) == 0x5555);
    ZuCHECK(i.in(20) == 0x55555);
    ZuCHECK(i.in<3>() == 0x5);
    ZuCHECK(i.in(26) == 0x1555555);
    ZuCHECK(i.in(1) == 1);
    ZuCHECK(!i.avail<8>());

    LE::Writer o2{i, buf.data() + buf.size()};

    o2.out<1>(1);

    o2.finish();
    buf.length(o2.pos() - buf.data());

    hex.length(ZuHex::encode({hex.data(), hex.size()}, buf));
    log(hex);

    LE::Reader i2{buf.data(), buf.data() + buf.length()};

    ZuCHECK(i2.in<3>() == 0x5);
    ZuCHECK(i2.in<2>() == 0x1);
    ZuCHECK(i2.in<3>() == 0x5);
    ZuCHECK(i2.in(8) == 0x55);
    ZuCHECK(i2.in(11) == 0x555);
    ZuCHECK(i2.in(9) == 0x155);
    ZuCHECK(i2.in(12) == 0x555);
    ZuCHECK(i2.in(16) == 0x5555);
    ZuCHECK(i2.in(19) == 0x15555);
    ZuCHECK(i2.in(17) == 0x5555);
    ZuCHECK(i2.in(20) == 0x55555);
    ZuCHECK(i2.in<3>() == 0x5);
    ZuCHECK(i2.in(26) == 0x1555555);
    ZuCHECK(i2.in(1) == 1);
    ZuCHECK(i2.in<1>() == 1);
    ZuCHECK(!i2.avail<8>());
  }
  {
    BE::Writer o{buf.data(), buf.data() + buf.size()};

    o.out<3>(0x5);
    o.out<2>(0x1);
    o.out<3>(0x5);
    o.out(0x55, 8);
    o.out(0x555, 11);
    o.out(0x155, 9);
    o.out(0x555, 12);
    o.out(0x5555, 16);
    o.out(0x15555, 19);
    o.out(0x5555, 17);
    o.out(0x55555, 20);
    o.out<3>(0x5);
    o.out(0x1555555, 26);
    o.out(1, 1);

    o.finish();
    buf.length(o.pos() - buf.data());

    hex.length(ZuHex::encode({hex.data(), hex.size()}, buf));
    log(hex);
    ZuCHECK(hex == "AD55AAB5555555552AAAA555555555AAAAAAAC");

    BE::Reader i{buf.data(), buf.data() + buf.length()};

    ZuCHECK(i.in<3>() == 0x5);
    ZuCHECK(i.in<2>() == 0x1);
    ZuCHECK(i.in<3>() == 0x5);
    ZuCHECK(i.in(8) == 0x55);
    ZuCHECK(i.in(11) == 0x555);
    ZuCHECK(i.in(9) == 0x155);
    ZuCHECK(i.in(12) == 0x555);
    ZuCHECK(i.in(16) == 0x5555);
    ZuCHECK(i.in(19) == 0x15555);
    ZuCHECK(i.in(17) == 0x5555);
    ZuCHECK(i.in(20) == 0x55555);
    ZuCHECK(i.in<3>() == 0x5);
    ZuCHECK(i.in(26) == 0x1555555);
    ZuCHECK(i.in(1) == 1);
    ZuCHECK(!i.avail<8>());

    BE::Writer o2{i, buf.data() + buf.size()};

    o2.out<1>(1);

    o2.finish();
    buf.length(o2.pos() - buf.data());

    hex.length(ZuHex::encode({hex.data(), hex.size()}, buf));
    log(hex);
    ZuCHECK(hex == "AD55AAB5555555552AAAA555555555AAAAAAAE");

    BE::Reader i2{buf.data(), buf.data() + buf.length()};

    ZuCHECK(i2.in<3>() == 0x5);
    ZuCHECK(i2.in<2>() == 0x1);
    ZuCHECK(i2.in<3>() == 0x5);
    ZuCHECK(i2.in(8) == 0x55);
    ZuCHECK(i2.in(11) == 0x555);
    ZuCHECK(i2.in(9) == 0x155);
    ZuCHECK(i2.in(12) == 0x555);
    ZuCHECK(i2.in(16) == 0x5555);
    ZuCHECK(i2.in(19) == 0x15555);
    ZuCHECK(i2.in(17) == 0x5555);
    ZuCHECK(i2.in(20) == 0x55555);
    ZuCHECK(i2.in<3>() == 0x5);
    ZuCHECK(i2.in(26) == 0x1555555);
    ZuCHECK(i2.in(1) == 1);
    ZuCHECK(i2.in<1>() == 1);
    ZuCHECK(!i2.avail<8>());
  }
  {
    LE::Writer o{buf.data(), buf.data() + buf.size()};

    o.out<3>(0x5);
    o.out(0x1234567, 28);

    o.finish();
    buf.length(o.pos() - buf.data());

    hex.length(ZuHex::encode({hex.data(), hex.size()}, buf));
    log(hex);

    LE::Reader i{buf.data(), buf.data() + buf.length()};

    ZuCHECK(i.in<3>() == 0x5);
    ZuCHECK(i.in<4>() == 0x7);
    ZuCHECK(i.in(20) == 0x23456);
    ZuCHECK(i.in<4>() == 0x1);
  }
  {
    BE::Writer o{buf.data(), buf.data() + buf.size()};

    o.out<3>(0x5);
    o.out(0x1234567, 28);

    o.finish();
    buf.length(o.pos() - buf.data());

    hex.length(ZuHex::encode({hex.data(), hex.size()}, buf));
    log(hex);
    ZuCHECK(hex == "A2468ACE");

    BE::Reader i{buf.data(), buf.data() + buf.length()};

    ZuCHECK(i.in<3>() == 0x5);
    ZuCHECK(i.in<4>() == 0x1);
    ZuCHECK(i.in(20) == 0x23456);
    ZuCHECK(i.in<4>() == 0x7);
  }
  {
    LE::Writer o{buf.data(), buf.data() + buf.size()};

    o.out<2>(0);
    o.out<2>(2);
    o.out(0x3e668c6fa0b2f9a3, 64);

    o.finish();
    buf.length(o.pos() - buf.data());

    hex.length(ZuHex::encode({hex.data(), hex.size()}, buf));
    log(hex);

    LE::Reader i{buf.data(), buf.data() + buf.length()};

    ZuCHECK(i.in<2>() == 0);
    ZuCHECK(i.in<2>() == 2);
    ZuCHECK(i.in(64) == 0x3e668c6fa0b2f9a3);
  }
  {
    BE::Writer o{buf.data(), buf.data() + buf.size()};

    o.out<2>(0);
    o.out<2>(2);
    o.out(0x3e668c6fa0b2f9a3, 64);

    o.finish();
    buf.length(o.pos() - buf.data());

    hex.length(ZuHex::encode({hex.data(), hex.size()}, buf));
    log(hex);
    ZuCHECK(hex == "23E668C6FA0B2F9A30");

    BE::Reader i{buf.data(), buf.data() + buf.length()};

    ZuCHECK(i.in<2>() == 0);
    ZuCHECK(i.in<2>() == 2);
    ZuCHECK(i.in(64) == 0x3e668c6fa0b2f9a3);
  }
}
