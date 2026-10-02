//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZfTOML.hh>
#include <zlib/ZfYAML.hh>

using namespace ZuTestUtil;

struct Config {
  enum { Max = 8 };
  unsigned value = 4;
  unsigned spin = 0;
  ZtString<> role{"server"};
};

ZfStruct(, Config,
  (((value), (Mutable, CLI::Long<"value">, (Range<0U, Config::Max>))), UInt32),
  (((spin), (Mutable, CLI::Long<"spin">)), UInt32),
  (((role), (Mutable, CLI::Long<"role">)), String));

template <typename Scan, typename Handler>
static void config(Scan scan, Handler handler, ZuCSpan valid, ZuCSpan invalid)
{
  ZuTestScope(config);
  auto parsed = scan(valid);
  Config value;
  value.spin = 17;
  handler(parsed.template p<1>().ptr()).update(value);
  ZuCheck(value.value == Config::Max);
  ZuCheck(value.spin == 17);
  ZuCheck(value.role == "server");

  auto bad = scan(invalid);
  bool rejected = false;
  try {
    handler(bad.template p<1>().ptr()).update(value);
  } catch (const ZeException &e) {
    rejected = true;
  }
  ZuCheck(rejected);
  ZuCheck(value.value == Config::Max);
}

static void cli()
{
  ZuTestScope(cli);
  ZfCLI::OutArgv args;
  args.argv.push("config");
  args.argv.push("--value");
  args.argv.push("8");
  args.finish();
  Config value;
  ZuCheck(ZfCLI::load<ZuFacet::Core>(value, args.argv) == 1);
  ZuCheck(value.value == Config::Max);
  ZuCheck(value.spin == 0 && value.role == "server");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(config,
    [](ZuCSpan s) { return ZfCf::scan(s); },
    [](auto node) { return ZfCf::handler<Config, ZuFacet::Core>(node); },
    "value: 8", "value: 9");
  ZuTestCall(config,
    [](ZuCSpan s) { return ZfTOML::scan(s); },
    [](auto node) { return ZfTOML::handler<Config, ZuFacet::Core>(node); },
    "value = 8\n", "value = 9\n");
  ZuTestCall(config,
    [](ZuCSpan s) { return ZfYAML::scan(s); },
    [](auto node) { return ZfYAML::handler<Config, ZuFacet::Core>(node); },
    "value: 8\n", "value: 9\n");
  ZuTestCall(cli);
}
