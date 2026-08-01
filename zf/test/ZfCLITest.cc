//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuID.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfCLI.hh>

using namespace ZuTestUtil;

ZtEnumNS(, Values, int8_t, High, Low, Normal);

namespace Flags {
  ZtFlags(, Flags, uint8_t, Bit0, Bit1, Bit2);
}

ZtEnumImplNS(Values);
ZtEnumImplNS(Flags);

struct Nested {
  int i1 = 0, i2 = 1;

  friend ZfStructPrint ZuPrintType(Nested *);
};

struct NestedJSON {
  int i1 = 2, i2 = 3;

  friend ZfCLI::AsJSON ZfCLI_Fmt(NestedJSON *);	// use JSON in CLI

  friend ZfStructPrint ZuPrintType(NestedJSON *);
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

ZfStruct((ScalarArgs, Bah),
  (((scalar), (Ctor<0>)), (UDT)));

ZfStruct((Nested, Bah),
  (((i1), (Ctor<0>)), (Int32)),
  (((i2), (Ctor<1>)), (Int32)));

ZfStruct((NestedJSON, Bah),
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

  friend ZfStructPrint ZuPrintType(Foo *);
};

ZfStruct((Foo, Bah),
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

ZfCLIConfig(Bah, (ZfCLI_ArrayFmt<ZfCLI::Delimited>));

struct LongOnly {
  unsigned	port = 0;
  bool		verbose = false;
};

ZfStruct((LongOnly, CLI),
  (((port),    (CLI::Long<"port">)),    (UInt32)),
  (((verbose), (CLI::Long<"verbose">)), (Bool)));

struct DelimitedArgs {
  ZtArray<ZuCSpan> values;

  template <typename V>
  DelimitedArgs(V &&v) : values{ZuFwd<V>(v)} { }
};

ZfStruct((DelimitedArgs, Bah),
  (((values), (Ctor<0>, CLI::Long<"values">)), (StringVec)));

struct IntArray : public ZtArray<int> {
  ZuDerive_(IntArray, ZtArray<int>);
  friend ZfCLI::AsArray<ZfFieldTC::Int32> ZfCLI_Fmt(IntArray *);
};

struct ArrayOpt {
  IntArray values;
};

ZfStruct((ArrayOpt, Bah),
  (((values), (Ctor<0>)), (UDT)));

struct BareArrayOpt {
  IntArray values;
};

ZfStruct((BareArrayOpt, CLI),
  (((values), (Ctor<0>)), (UDT)));

struct Positional {
  int value = 0;
};

ZfStruct((Positional, CLI),
  (((value), (Ctor<0>, CLI::Arg<1>, (Range<0, 10>))), (Int32)));

struct RequiredOpt {
  int value = ZuCmp<int>::null();
};

ZfStruct((RequiredOpt, CLI),
  (((value), (Ctor<0>, Required)), (Int32)));

template <typename L>
static ZeString cliError(L l)
{
  try {
    l();
  } catch (const ZeException &e) {
    ZeString message;
    message << e;
    return message;
  }
  return {};
}

void roundTrip()
{
  ZuTestScope(roundTrip);

  ZfCLI::Parser<Foo, ZuFacet::Bah> parser;
  parser.scanArgv(ZfCLI::SpanArgv{});
  Foo foo = ZfCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
  foo.int_ = 42;
  foo.float_ = 42.01;
  foo.bytes = "-bytes";
  foo.bytesVec = { "xxx", "yyyy", "zzzzz" };
  foo.time_ = Zm::now();
  foo.bool_ = true;

  ZtString<> cli;
  cli << "'' "; // argv[0]
  ZfCLI::save<ZuFacet::Bah>(cli, foo);
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
  parser.scanArgv(ZfCLI::InCLI{cli}.argv);
  Foo foo2 = ZfCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
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
  ZfCLI::save<ZuFacet::Bah>(cli2, foo2);
  log("cli2=", cli2);
  ZuCheck(cli_ == cli2);

  ZfCLI::OutArgv out;
  out.argv.push(""); // argv[0]
  ZfCLI::saveArgv<ZuFacet::Bah>(out, foo2);
  ZuCheck(!out.argv_c()[out.argc()]);
  if (verbose) {
    for (unsigned i = 0, n = out.argc(); i < n; i++)
      std::cerr << i << ": " << out.argv_c()[i] << '\n';
  }

  parser.reset();
  ZfCLI::InArgv<> in(out.argc(), out.argv_c());
  parser.scanArgv(in.argv);
  Foo foo3 = ZfCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
  ZtString<> cli3;
  cli3 << "'' "; // argv[0]
  ZfCLI::save<ZuFacet::Bah>(cli3, foo3);
  log("cli3=", cli3);
  ZuCheck(cli_ == cli3);
}

void fieldlessUDT()
{
  ZuTestScope(fieldlessUDT);

  ScalarArgs value{Scalar{"hello world"}};
  ZtString<> cli{"x "};
  ZfCLI::save<ZuFacet::Bah>(cli, value);
  ZfCLI::InCLI in{cli};
  ZfCLI::Parser<ScalarArgs, ZuFacet::Bah> parser;
  parser.scanArgv(in.argv);
  auto loaded =
    ZfCLI::handler<ScalarArgs, ZuFacet::Bah>(parser.root).ctor();
  ZuCheck(loaded.scalar.value == "hello world");
}

void bareArraySave()
{
  ZuTestScope(bareArraySave);

  BareArrayOpt value;
  value.values.push(1);
  value.values.push(2);
  ZtString<> cli;
  ZfCLI::save(cli, value);
  ZuCheck(cli == "--values=1 --values=2");
}

void cmdQuote()
{
  ZuTestScope(cmdQuote);
  ZtString s;
  ZfCLI::CmdQuote::quote(s, "foo"); ZuCheck(s == "foo"); s.length_(0);
  ZfCLI::CmdQuote::quote(s, "foo\\"); ZuCheck(s == "foo\\"); s.length_(0);
  ZfCLI::CmdQuote::quote(s, "foo\\\"");
    ZuCheck(s == "\"foo\\\\\\\"\""); s.length_(0);
  ZfCLI::CmdQuote::quote(s, "foo\\\\\"");
    ZuCheck(s == "\"foo\\\\\\\\\\\"\""); s.length_(0);
  ZfCLI::CmdQuote::quote(s, "foo\"\\\\");
    ZuCheck(s == "\"foo\\\"\\\\\\\\\"");
}

void parseCLI()
{
  ZuTestScope(parseCLI);
  static char cli[] = "'x \\'\"y'\\ \" z\" blurch";
  ZfCLI::InCLI in(cli);
  ZuCheck(in.argv[0] == "x '\"y  z");
  ZuCheck(in.argv[1] == "blurch");
}

void parseCLIEscapedAndEmpty()
{
  ZuTestScope(parseCLIEscapedAndEmpty);

  static char cli[] = "\"a\\\\\\\"b\" '' \"\" tail";
  ZfCLI::InCLI in(cli);
  ZuCheck(in.argv[0] == "a\\\"b");
  ZuCheck(in.argv[1] == "");
  ZuCheck(in.argv[2] == "");
  ZuCheck(in.argv[3] == "tail");
}

void parseCLIRedirect()
{
  ZuTestScope(parseCLIRedirect);

  static char cli[] = "x <in >> out";
  ZfCLI::InCLI in(cli);
  ZuCheck(in.argv.length() == 1);
  ZuCheck(in.argv[0] == "x");
  ZuCheck(in.in == "in");
  ZuCheck(in.out == "out");
  ZuCheck(in.append);
}

void longOnlyOptions()
{
  ZuTestScope(longOnlyOptions);

  ZfCLI::OutArgv out;
  out.argv.push("");
  out.argv.push("--port");
  out.argv.push("8080");
  out.argv.push("--verbose");
  out.finish();

  ZfCLI::Parser<LongOnly> parser;
  parser.scanArgv(out.argv);
  ZuCheck(parser.hasKey("verbose"));

  LongOnly options;
  int argc = ZfCLI::load(options, out.argc(), out.argv_c());
  ZuCheck(argc == 1);
  ZuCheck(options.port == 8080);
  ZuCheck(options.verbose);

  parser.reset();
  char unknown[] = "--unknown";
  auto message = cliError([&parser, &unknown] {
    parser.scanArg({unknown, sizeof(unknown) - 1});
  });
  ZuCheck(message.find("unrecognized option '--unknown'") >= 0);
  parser.reset();
  char unknownValue[] = "--unknown=1";
  message = cliError([&parser, &unknownValue] {
    parser.scanArg({unknownValue, sizeof(unknownValue) - 1});
  });
  ZuCheck(message.find("unrecognized option '--unknown'") >= 0);
}

void integerRange()
{
  ZuTestScope(integerRange);

  char cli[] = "x --int_ranged=101";
  ZfCLI::InCLI in(cli);
  ZfCLI::Parser<Foo, ZuFacet::Bah> parser;
  parser.scanArgv(in.argv);
  auto message = cliError([&parser] {
    ZfCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
  });
  ZuCheck(message.find("option '--int_ranged'") >= 0);
  ZuCheck(message.find("out of range") >= 0);
}

void realRange()
{
  ZuTestScope(realRange);

  char cli[] =
    "x --float_ranged=1.1 --fixed=-0.1 --decimal=1.1";
  ZfCLI::InCLI in(cli);
  ZfCLI::Parser<Foo, ZuFacet::Bah> parser;
  parser.scanArgv(in.argv);
  auto message = cliError([&parser] {
    ZfCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
  });
  ZuCheck(message.find("option '--float_ranged'") >= 0);
  ZuCheck(message.find("out of range") >= 0);
}

void diagnostics()
{
  ZuTestScope(diagnostics);

  {
    char cli[] = "x --foo";
    ZfCLI::InCLI in(cli);
    ZfCLI::Parser<LongOnly> parser;
    auto message = cliError([&parser, &in] { parser.scanArgv(in.argv); });
    ZuCheck(message.find("unrecognized option '--foo'") >= 0);
    ZuCheck(message.find("line") < 0);
    ZuCheck(message.find("offset") < 0);
    ZuCheck(message.find("ZfCLI.hh") < 0);
    ZuCheck(message.find("ZfCLI.cc") < 0);
  }
  {
    char cli[] = "x --port";
    ZfCLI::InCLI in(cli);
    ZfCLI::Parser<LongOnly> parser;
    auto message = cliError([&parser, &in] { parser.scanArgv(in.argv); });
    ZuCheck(message.find("option '--port' requires a value") >= 0);
  }
  {
    char cli[] = "x --port=12x";
    ZfCLI::InCLI in(cli);
    LongOnly value;
    auto message = cliError([&value, &in] { ZfCLI::load(value, in.argv); });
    ZuCheck(message.find("invalid integer '12x'") >= 0);
    ZuCheck(message.find("option '--port'") >= 0);
  }
  {
    char cli[] = "x --port=1 --port=2";
    ZfCLI::InCLI in(cli);
    LongOnly value;
    auto message = cliError([&value, &in] { ZfCLI::load(value, in.argv); });
    ZuCheck(message.find(
      "option '--port' was specified more than once") >= 0);
  }
  {
    char cli[] = "x --verbose=maybe";
    ZfCLI::InCLI in(cli);
    LongOnly value;
    auto message = cliError([&value, &in] { ZfCLI::load(value, in.argv); });
    ZuCheck(message.find("invalid boolean 'maybe'") >= 0);
    ZuCheck(message.find("option '--verbose'") >= 0);
  }
  {
    char cli[] = "x -e Bad";
    ZfCLI::InCLI in(cli);
    ZfCLI::Parser<Foo, ZuFacet::Bah> parser;
    parser.scanArgv(in.argv);
    auto message = cliError([&parser] {
      ZfCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
    });
    ZuCheck(message.find("invalid value 'Bad' for option '-e'") >= 0);
    ZuCheck(message.find("High") >= 0);
    ZuCheck(message.find("Low") >= 0);
    ZuCheck(message.find("Normal") >= 0);
  }
  {
    char cli[] = "x --time_=bad";
    ZfCLI::InCLI in(cli);
    ZfCLI::Parser<Foo, ZuFacet::Bah> parser;
    parser.scanArgv(in.argv);
    auto message = cliError([&parser] {
      ZfCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
    });
    ZuCheck(message.find("invalid date/time 'bad'") >= 0);
    ZuCheck(message.find("option '--time_'") >= 0);
  }
  {
    char cli[] = "x '' '' !!!";
    ZfCLI::InCLI in(cli);
    ZfCLI::Parser<Foo, ZuFacet::Bah> parser;
    parser.scanArgv(in.argv);
    auto message = cliError([&parser] {
      ZfCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
    });
    ZuCheck(message.find("invalid encoded bytes '!!!'") >= 0);
    ZuCheck(message.find("argument 3") >= 0);
  }
  {
    char cli[] = "x --values=1,bad";
    ZfCLI::InCLI in(cli);
    ZfCLI::Parser<ArrayOpt, ZuFacet::Bah> parser;
    parser.scanArgv(in.argv);
    auto message = cliError([&parser] {
      ZfCLI::handler<ArrayOpt, ZuFacet::Bah>(parser.root).ctor();
    });
    ZuCheck(message.find("invalid integer 'bad'") >= 0);
    ZuCheck(message.find("option '--values'") >= 0);
  }
  {
    char cli[] = "x bad";
    ZfCLI::InCLI in(cli);
    ZfCLI::Parser<Positional> parser;
    parser.scanArgv(in.argv);
    auto message = cliError([&parser] {
      ZfCLI::handler<Positional>(parser.root).ctor();
    });
    ZuCheck(message.find("invalid integer 'bad'") >= 0);
    ZuCheck(message.find("argument 1") >= 0);
  }
  {
    char cli[] = "x";
    ZfCLI::InCLI in(cli);
    ZfCLI::Parser<RequiredOpt> parser;
    parser.scanArgv(in.argv);
    auto message = cliError([&parser] {
      ZfCLI::handler<RequiredOpt>(parser.root).ctor();
    });
    ZuCheck(message.find("option '--value' is required") >= 0);
  }
  {
    char cli[] = "x --nestedJSON=broken";
    ZfCLI::InCLI in(cli);
    ZfCLI::Parser<Foo, ZuFacet::Bah> parser;
    parser.scanArgv(in.argv);
    auto message = cliError([&parser] {
      ZfCLI::handler<Foo, ZuFacet::Bah>(parser.root).ctor();
    });
    ZuCheck(message.find("invalid JSON 'broken'") >= 0);
    ZuCheck(message.find("option '--nestedJSON'") >= 0);
  }
  {
    char cli[] = "x 'broken";
    auto message = cliError([&cli] { ZfCLI::InCLI in(cli); });
    ZuCheck(message.find("unterminated quote ' in argument 1") >= 0);
  }
  {
    char cli[] = "x >";
    auto message = cliError([&cli] { ZfCLI::InCLI in(cli); });
    ZuCheck(message.find(
      "output redirection in argument 1 requires a path") >= 0);
  }
}

void delimitedLoad()
{
  ZuTestScope(delimitedLoad);

  char cli[] = "x --values=a,b";
  ZfCLI::InCLI in(cli);
  ZfCLI::Parser<DelimitedArgs, ZuFacet::Bah> parser;
  parser.scanArgv(in.argv);
  auto field = parser.root->data<ZfCLI::AnyNode::Object>().find("values");
  ZuCheck(field && field->val()->has<ZfCLI::AnyNode::String>());

  auto args = ZfCLI::handler<DelimitedArgs, ZuFacet::Bah>(parser.root).ctor();
  ZuCheck(field->val()->has<ZfCLI::AnyNode::Array>());
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
  ZuTestCall(bareArraySave);
  ZuTestCall(cmdQuote);
  ZuTestCall(parseCLI);
  ZuTestCall(parseCLIEscapedAndEmpty);
  ZuTestCall(parseCLIRedirect);
  ZuTestCall(longOnlyOptions);
  ZuTestCall(integerRange);
  ZuTestCall(realRange);
  ZuTestCall(diagnostics);
  ZuTestCall(delimitedLoad);
  return 0;
}
