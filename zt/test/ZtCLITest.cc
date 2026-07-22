//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuID.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZtStruct.hh>
#include <zlib/ZtCLI.hh>

using namespace ZuTestUtil;

ZtEnumNS(Values, int8_t, High, Low, Normal);

namespace Flags {
  ZtFlags(Flags, uint8_t, Bit0, Bit1, Bit2);
}

struct Nested {
  int i1 = 0, i2 = 1;

  friend ZtStructPrint ZuPrintType(Nested *);
};

struct NestedJSON {
  int i1 = 2, i2 = 3;

  friend ZtCLI::AsJSON ZtCLI_Fmt(NestedJSON *);	// use JSON in CLI

  friend ZtStructPrint ZuPrintType(NestedJSON *);
};

ZuStructFacet(Bah);

struct Scalar {
  Scalar() = default;
  Scalar(ZuCSpan value_) : value{value_} { }

  ZtString<>	value;

  template <typename S>
  friend S &operator <<(S &s, const Scalar &v) {
    s << v.value;
    return s;
  }
};

struct ScalarArgs {
  Scalar scalar;
};

ZtStruct((ScalarArgs, Bah),
  (((scalar), (Ctor<0>)), (UDT)));

ZtStruct((Nested, Bah),
  (((i1), (Ctor<0>)), (Int32)),
  (((i2), (Ctor<1>)), (Int32)));

ZtStruct((NestedJSON, Bah),
  (((i1), (Ctor<0>)), (Int32)),
  (((i2), (Ctor<1>)), (Int32)));

struct UBool : public ZuUnion<void, bool> {
  ZuDerive_(UBool, (ZuUnion<void, bool>))
  operator bool() const { return this->template p<bool>(); }
};

struct Foo {
  const char *string = nullptr;
  ZtArray<uint8_t> bytes;
  ZuID id = "goodbye";
  int int_ = 0;
  int int_ranged = 42;
  unsigned hex = 0xdeadbeef;
  int enum_ = Values::Normal;
  uint128_t flags = Flags::Bit1();
  double float_ = ZuCmp<double>::null();
  double float_ranged = 0.42;
  ZuFixed fixed;
  ZuDecimal decimal;
  ZuTime time_;
  Nested nested;
  NestedJSON nestedJSON;
  ZtArray<ZtArray<uint8_t>> bytesVec;
  bool bool_ = false;
  /* UBool ubool; */

  friend ZtStructPrint ZuPrintType(Foo *);
};

ZtStruct((Foo, Bah),
  (((string, Rd), (Ctor<0>)), (CString, "hello \"world\"")),
  (((bytes), (Ctor<1>, CLI::Escaped, CLI::Arg<1>)), (Bytes, ZuBSpan{"bytes"})),
  (((id), (Ctor<2>, Mutable)), (String, "goodbye")),
  (((int_), (Ctor<3>, CLI::ID<"int">, CLI::Number<ZuFmt::Right<9>>)), (Int32)),
  (((int_ranged), (Ctor<4>, CLI::ID<"int-ranged">, (Range<0, 100>))),
    (Int32, 42)),
  (((hex), (Ctor<5>, Hex)), (UInt32, 0xdeadbeef)),
  (((enum_), (Ctor<6>, Enum<Values::Map>, CLI::ID<"enum">, CLI::Opt<'e'>)),
    (Int32, Values::Normal)),
  (((flags), (Ctor<7>, Flags<Flags::Map>)), (UInt128, Flags::Bit1())),
  (((float_), (Ctor<8>, CLI::ID<"float">, CLI::Number<ZuFmt::FP<4>>)), (Float)),
  (((float_ranged), (Ctor<9>, CLI::ID<"float-ranged">, (Range<0.0, 1>))),
    (Float, 0.42)),
  (((fixed), (Ctor<10>,
      (Range<ZuDecimal{0}, ZuDecimal{1}>))), (Fixed)),
  (((decimal), (Ctor<11>,
      (Range<ZuDecimal{0}, ZuDecimal{1}>))), (Decimal)),
  (((time_), (Ctor<12>, CLI::ID<"time">)), (Time)),
  (((nested), (Ctor<13>)), (UDT)),
  (((nestedJSON), (Ctor<14>)), (UDT)),
  (((bytesVec), (Ctor<15>, CLI::Args<3>)), (BytesVec)),
  (((bool_), (Ctor<16>, CLI::ID<"bool">, CLI::Flag<'b'>)), (Bool)) /*,
  (((ubool), (Ctor<17>)), (Bool)) */);

ZtCLIConfig(Bah, (ZtCLI_ArrayFmt<ZtCLI::Delimited>));

struct LongOnly {
  unsigned	port = 0;
  bool		verbose = false;
};

ZtStruct((LongOnly, CLI),
  (((port),    (CLI::Long<"port">)),    (UInt32)),
  (((verbose), (CLI::Long<"verbose">)), (Bool)));

struct DelimitedArgs {
  ZtArray<ZuCSpan> values;

  template <typename V>
  DelimitedArgs(V &&v) : values{ZuFwd<V>(v)} { }
};

ZtStruct((DelimitedArgs, Bah),
  (((values), (Ctor<0>, CLI::Long<"values">)), (StringVec)));

void roundTrip()
{
  ZuTestScope(roundTrip);

  ZtCLI::Parser<Foo, ZuFacet::Bah> parser;
  ZuCheck(parser.scanArgv(ZtCLI::SpanArgv{}));
  Foo foo = ZtCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
  foo.int_ = 42;
  foo.float_ = 42.01;
  foo.bytes = "-bytes";
  foo.bytesVec = { "xxx", "yyyy", "zzzzz" };
  foo.time_ = Zm::now();
  foo.bool_ = true;

  ZtString<> cli;
  cli << "'' "; // argv[0]
  ZtCLI::save<ZuFacet::Bah>(cli, foo);
  log("cli=", cli);
  if (verbose) {
    ZuUnroll::all<ZuFields<Foo>>([&foo]<typename T>() mutable {
      std::cerr
	<< T::id() << '='
	<< typename T::Type::template Print<ZtFmt::Default>{T::get(foo)}
	<< '\n';
    });
  }

  ZtString<> cli_ = cli;
  parser.scanArgv(ZtCLI::InCLI{cli}.argv);
  Foo foo2 = ZtCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
  if (verbose) {
    ZuUnroll::all<ZuFields<Foo>>([&foo2]<typename T>() mutable {
      std::cerr
	<< T::id() << '='
	<< typename T::Type::template Print<ZtFmt::Default>{T::get(foo2)}
	<< '\n';
    });
  }

  ZtString<> cli2;
  cli2 << "'' "; // argv[0]
  ZtCLI::save<ZuFacet::Bah>(cli2, foo2);
  log("cli2=", cli2);
  ZuCheck(cli_ == cli2);

  ZtCLI::OutArgv out;
  out.argv.push(""); // argv[0]
  ZtCLI::saveArgv<ZuFacet::Bah>(out, foo2);
  ZuCheck(!out.argv_c()[out.argc()]);
  if (verbose) {
    for (unsigned i = 0, n = out.argc(); i < n; i++)
      std::cerr << i << ": " << out.argv_c()[i] << '\n';
  }

  parser.reset();
  ZtCLI::InArgv<> in(out.argc(), out.argv_c());
  parser.scanArgv(in.argv);
  Foo foo3 = ZtCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
  ZtString<> cli3;
  cli3 << "'' "; // argv[0]
  ZtCLI::save<ZuFacet::Bah>(cli3, foo3);
  log("cli3=", cli3);
  ZuCheck(cli_ == cli3);
}

void fieldlessUDT()
{
  ZuTestScope(fieldlessUDT);

  ScalarArgs value{Scalar{"hello world"}};
  ZtString<> cli{"x "};
  ZtCLI::save<ZuFacet::Bah>(cli, value);
  ZtCLI::InCLI in{cli};
  ZtCLI::Parser<ScalarArgs, ZuFacet::Bah> parser;
  ZuCheck(parser.scanArgv(in.argv));
  auto loaded =
    ZtCLI::handler<ScalarArgs, ZuFacet::Bah>(parser.root).ctor();
  ZuCheck(loaded.scalar.value == "hello world");
}

void cmdQuote()
{
  ZuTestScope(cmdQuote);
  ZtString s;
  ZtCLI::CmdQuote::quote(s, "foo"); ZuCheck(s == "foo"); s.length_(0);
  ZtCLI::CmdQuote::quote(s, "foo\\"); ZuCheck(s == "foo\\"); s.length_(0);
  ZtCLI::CmdQuote::quote(s, "foo\\\"");
    ZuCheck(s == "\"foo\\\\\\\"\""); s.length_(0);
  ZtCLI::CmdQuote::quote(s, "foo\\\\\"");
    ZuCheck(s == "\"foo\\\\\\\\\\\"\""); s.length_(0);
  ZtCLI::CmdQuote::quote(s, "foo\"\\\\");
    ZuCheck(s == "\"foo\\\"\\\\\\\\\"");
}

void parseCLI()
{
  ZuTestScope(parseCLI);
  static char cli[] = "'x \\'\"y'\\ \" z\" blurch";
  ZtCLI::InCLI in(cli);
  ZuCheck(in.argv[0] == "x '\"y  z");
  ZuCheck(in.argv[1] == "blurch");
}

void parseCLIEscapedAndEmpty()
{
  ZuTestScope(parseCLIEscapedAndEmpty);

  static char cli[] = "\"a\\\\\\\"b\" '' \"\" tail";
  ZtCLI::InCLI in(cli);
  ZuCheck(in.argv[0] == "a\\\"b");
  ZuCheck(in.argv[1] == "");
  ZuCheck(in.argv[2] == "");
  ZuCheck(in.argv[3] == "tail");
}

void longOnlyOptions()
{
  ZuTestScope(longOnlyOptions);

  ZtCLI::OutArgv out;
  out.argv.push("");
  out.argv.push("--port");
  out.argv.push("8080");
  out.argv.push("--verbose");
  out.finish();

  ZtCLI::Parser<LongOnly> parser;
  ZuCheck(parser.scanArgv(out.argv));
  ZuCheck(parser.hasKey("verbose"));

  LongOnly options;
  int argc = ZtCLI::load(options, out.argc(), out.argv_c());
  ZuCheck(argc == 1);
  ZuCheck(options.port == 8080);
  ZuCheck(options.verbose);

  parser.reset();
  char unknown[] = "--unknown";
  ZuCheck(!parser.scanArg({unknown, sizeof(unknown) - 1}));
  parser.reset();
  char unknownValue[] = "--unknown=1";
  ZuCheck(!parser.scanArg({unknownValue, sizeof(unknownValue) - 1}));
}

void integerRange()
{
  ZuTestScope(integerRange);

  char cli[] = "x --int_ranged=101";
  ZtCLI::InCLI in(cli);
  ZtCLI::Parser<Foo, ZuFacet::Bah> parser;
  ZuCheck(parser.scanArgv(in.argv));
  auto value = ZtCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
  ZuCheck(value.int_ranged == ZuCmp<int>::null());
}

void realRange()
{
  ZuTestScope(realRange);

  char cli[] =
    "x --float_ranged=1.1 --fixed=-0.1 --decimal=1.1";
  ZtCLI::InCLI in(cli);
  ZtCLI::Parser<Foo, ZuFacet::Bah> parser;
  ZuCheck(parser.scanArgv(in.argv));
  auto value = ZtCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
  ZuCheck(ZuCmp<double>::null(value.float_ranged));
  ZuCheck(ZuCmp<ZuFixed>::null(value.fixed));
  ZuCheck(ZuCmp<ZuDecimal>::null(value.decimal));
}

void delimitedLoad()
{
  ZuTestScope(delimitedLoad);

  char cli[] = "x --values=a,b";
  ZtCLI::InCLI in(cli);
  ZtCLI::Parser<DelimitedArgs, ZuFacet::Bah> parser;
  ZuCheck(parser.scanArgv(in.argv));
  auto field = parser.root->data<ZtURI::AnyNode::Object>().find("values");
  ZuCheck(field && field->val()->has<ZtURI::AnyNode::String>());

  auto args = ZtCLI::handler<DelimitedArgs, ZuFacet::Bah>(parser.root).ctor();
  ZuCheck(field->val()->has<ZtURI::AnyNode::Array>());
  ZuCheck(args.values.length() == 2);
  ZuCheck(args.values[0] == "a");
  ZuCheck(args.values[1] == "b");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(roundTrip);
  ZuTestCall(fieldlessUDT);
  ZuTestCall(cmdQuote);
  ZuTestCall(parseCLI);
  ZuTestCall(parseCLIEscapedAndEmpty);
  ZuTestCall(longOnlyOptions);
  ZuTestCall(integerRange);
  ZuTestCall(realRange);
  ZuTestCall(delimitedLoad);
  return 0;
}
