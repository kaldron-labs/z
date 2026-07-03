//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <iostream>

#include <zlib/ZtEnum.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZvCf.hh>

void fail() { Zm::exit(1); }

void out_(bool ok, ZuCSpan check, ZuCSpan diag) {
  std::cout
    << (ok ? "OK  " : "NOK ") << check << ' ' << diag
    << '\n' << std::flush;
}

#define CHECK_(x) out_((x), #x, "")
#define CHECK(x, y) out_((x), #x, y)

static const char testdata[] =
"#\n"
"  #\n"
"     key4 # kick kick\n"
"\n"
"\n"
"     \\#\\ value4\n"
"key2 ok\\ \n"
"key3 ok2\\\\\n"
"\n"
"# \\grok this word\n"
"\n"
"	key1		\n"
"			\"ok \\\"this is val1\\\\\"		# comment !!\n"
"  0 \"\" 1 Arg1\n"
"key6 { a b c d\\} }\n"
"\n"
"key5 [\\#\\ k51, \"k5\\\\2\", k\\ 53\\,,\n"
"k54\\ , k55 ]\n"
"\n"
"%define FAT artma\n"
"key7 { foo { bah 1 } } key8 C${FAT}n\n";

ZtEnumNS(Values, int8_t, High, Low, Normal);

int main()
{
  ZiLog::init("CfTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  try {
    {
      ZiFile file;
      if (file.open("in.cf", ZiFile::Write, 0777) != Zi::OK)
	throw file.error();
      if (file.write(&testdata[0], sizeof(testdata) - 1) != Zi::OK)
	throw file.error();
    }
    {
      ZmRef<ZvCf> cf = new ZvCf();

      cf->fromFile("in.cf");
      cf->toFile("out.cf");
    }
    ZtString<> out;
    {
      ZmRef<ZvCf> cf = new ZvCf();

      cf->fromFile("out.cf");
      out << *cf;
      bool caught = false;
      try {
	cf->fromFile("out_.cf");
      } catch (const ZeException &) {
	caught = true;
      } catch (const ZeError &) {
	caught = true;
      }
      CHECK(caught, "nonexistent file detected");
    }
    {
      ZmRef<ZvCf> cf = new ZvCf();

      cf->fromString(out);
      cf->fromFile("in.cf");
      cf->toFile("out2.cf");
    }
    {
      ZmRef<ZvCf> cf = new ZvCf();

      cf->fromFile("out2.cf");
      ZtString<> out2;
      out2 << *cf;
      CHECK(out == out2, "out.cf identical to out2.cf");
    }

    try {
      ZmRef<ZvCf> cf = new ZvCf();

      cf->fromString("i 101");
      if (cf->getInt("j", 1, 100, 42) != 42) {
	std::cout << "NOK getInt() default failed\n";
	fail();
      }
      try {
	cf->getInt<true>("j", 1, 100);
	std::cout << "NOK getInt() required failed\n";
	fail();
      } catch (const ZeException &e) {
	std::cout << "OK: " << e << '\n';
      }
      cf->getInt("i", 1, 100, 42);
      std::cout << "NOK getInt() range failed\n";
      fail();
    } catch (const ZeException &e) {
      std::cout << "OK: " << e << '\n';
    }

    try {
      ZmRef<ZvCf> cf = new ZvCf();

      cf->fromString("i 100.01");
      if (cf->getDbl("j", .1, 100, .42) != .42) {
	std::cout << "NOK getDbl() default failed\n";
	fail();
      }
      try {
	cf->getDbl<true>("j", .1, 100);
	std::cout << "NOK getDbl() required failed\n";
	fail();
      } catch (const ZeException &e) {
	std::cout << "OK: " << e << '\n';
      }
      cf->getDbl("i", .1, 100, .42);
      std::cout << "NOK getDbl() range failed\n";
      fail();
    } catch (const ZeException &e) {
      std::cout << "OK  " << e << '\n';
    }

    try {
      ZmRef<ZvCf> cf = new ZvCf();
      cf->fromString("i FooHigh");
      if (cf->getEnum<Values::Map, int>("j") >= 0) {
	std::cout << "NOK getEnum() default failed\n";
	fail();
      }
      cf->getEnum<Values::Map, int, true>("i");
      std::cout << "NOK getEnum() invalid failed\n";
      fail();
    } catch (const ZeException &e) {
      std::cout << "OK  " << e << '\n';
    }

    {
      ZmRef<ZvCf> cf1 = new ZvCf{}, cf2 = new ZvCf{},
		  cf3 = new ZvCf{}, cf4 = new ZvCf{};

      cf1->fromString("i foo l { m baz }");
      cf2->fromString("j { k bar } n bah");
      cf3->merge(cf1);
      cf3->merge(cf2);
      cf4->merge(cf2);
      cf4->merge(cf1);
      cf3->toFile("out6.cf");
      cf4->toFile("out7.cf");
      ZtString<> out3, out4;
      out3 << *cf3;
      out4 << *cf4;
      CHECK(out3 == out4, "out6.cf is identical to out7.cf");
    }

    {
      ZmRef<ZvCf> cf = new ZvCf();
      cf->fromString("\\=A value");
      CHECK_(cf->get("=A") == "value");
    }

    {
      ZmRef<ZvCf> cf = new ZvCf();
      cf->fromString("x { y z }");
      CHECK_(cf->get("x.y") == "z");
    }

  } catch (const ZeException &e) {
    std::cerr << e << '\n' << std::flush;
    Zm::exit(1);
  } catch (const ZeError &e) {
    std::cerr << e << '\n' << std::flush;
    Zm::exit(1);
  } catch (...) {
    std::cerr << "unknown exception\n" << std::flush;
    Zm::exit(1);
  }

  ZiLog::stop();
  return 0;
}
