//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtCf.hh>

using namespace ZuTestUtil;

ZtEnumNS(CfValues, int8_t, High, Low, Normal);

namespace CfFlags {
  ZtFlags(Flags, uint8_t, Bit0, Bit1, Bit2);
}

namespace ZuFieldProp::Cf {
  using Unix9 = Unix<ZtCf::Sec, 9>;
}

struct CfNested {
  int value = 0;
};

ZtStruct((CfNested, Cf),
  (((value), (Ctor<0>)), (Int32)));

struct CfText {
  CfText() = default;
  CfText(ZuCSpan value_) : value{value_} { }

  ZtString<> value;

  friend ZtCf::AsString ZtCf_Fmt(CfText *);
};

struct CfTextData {
  CfText text;
};

ZtStruct((CfTextData, Cf),
  (((text), (Ctor<0>)), (UDT)));

struct CfData {
  const char *cstr = nullptr;
  ZtString<> string;
  int number = 0;
  bool bool_ = false;
  ZtArray<uint8_t> bytes;
  ZtArray<ZtString<>> strings;
  CfNested nested;
};

ZtStruct((CfData, Cf),
  (((cstr), (Ctor<0>)), (CString)),
  (((string), (Ctor<1>)), (String)),
  (((number), (Ctor<2>)), (Int32)),
  (((bool_), (Ctor<3>)), (Bool)),
  (((bytes), (Ctor<4>)), (Bytes)),
  (((strings), (Ctor<5>)), (StringVec)),
  (((nested), (Ctor<6>)), (UDT)));

struct CfBytes {
  ZtArray<uint8_t> base64;
  ZtArray<uint8_t> base64URL;
  ZtArray<uint8_t> base32;
  ZtArray<uint8_t> hex;
  ZtArray<uint8_t> raw;
  ZtArray<ZtArray<uint8_t>> vec;
};

ZtStruct((CfBytes, Cf),
  (((base64),		(Ctor<0>, Cf::Base64)),	(Bytes)),
  (((base64URL),	(Ctor<1>, Cf::Base64URL)),	(Bytes)),
  (((base32),		(Ctor<2>, Cf::Base32)),	(Bytes)),
  (((hex),		(Ctor<3>, Cf::Hex)),		(Bytes)),
  (((raw),		(Ctor<4>, Cf::Raw)),		(Bytes)),
  (((vec),		(Ctor<5>)),			(BytesVec)));

struct CfNumbers {
  int i = 0;
  unsigned hex = 0;
  int enum_ = CfValues::Normal;
  uint128_t flags = 0;
  double float_ = 0;
  ZuFixed fixed;
  ZuDecimal decimal;
  ZuTime time;
  ZtArray<int> ints;
};

ZtStruct((CfNumbers, Cf),
  (((i),		(Ctor<0>, Mutable)),		(Int32)),
  (((hex),	(Ctor<1>, Hex)),		(UInt32)),
  (((enum_),	(Ctor<2>, Enum<CfValues::Map>)),	(Int32)),
  (((flags),	(Ctor<3>, Flags<CfFlags::Map>)),	(UInt128)),
  (((float_),	(Ctor<4>, (Range<-1000.0, 1000.0>))),	(Float)),
  (((fixed),	(Ctor<5>,
      (Range<ZuDecimal{-1000}, ZuDecimal{1000}>))),	(Fixed)),
  (((decimal),	(Ctor<6>,
      (Range<ZuDecimal{-1000}, ZuDecimal{1000}>))),	(Decimal)),
  (((time),	(Ctor<7>, Cf::Unix9)),		(Time)),
  (((ints),	(Ctor<8>)),			(Int32Vec)));

struct CfOptional {
  const char *head = nullptr;
  const char *req = "";
  const char *tail = "tail";
};

ZtStruct((CfOptional, Cf),
  (((head), (Ctor<0>, Cf::Opt)), (CString)),
  (((req),  (Ctor<1>)), (CString)),
  (((tail), (Ctor<2>)), (CString)));

struct CfRange {
  int value = 42;
};

ZtStruct((CfRange, Cf),
  (((value), (Ctor<0>, (Range<0, 100>))), (Int32, 42)));

static const ZtCf::AnyNode *field(
    const ZtCf::AnyNode *node, ZuCSpan id) {
  if (!node || !node->has<ZtCf::AnyNode::Object>()) return nullptr;
  const auto &fields = node->data<ZtCf::AnyNode::Object>();
  for (unsigned i = 0, n = fields.length(); i < n; i++)
    if (fields[i].p<0>() == id) return fields[i].p<1>();
  return nullptr;
}

static ZuCSpan string(const ZtCf::AnyNode *node) {
  if (!node || !node->has<ZtCf::AnyNode::String>()) return {};
  return node->data<ZtCf::AnyNode::String>();
}

static void checkToken(ZuCSpan in, ZuCSpan expected) {
  ZuTestScope(checkToken);
  ZtCf::AnyNode::String out;
  auto result = ZtCf::eos<false>(in, out);
  ZuCheck(result == int(in.length()));
  ZuCheck(out == expected);
  ZuCheck(!out.data()[out.length()]);
}

static void ownership() {
  ZuTestScope(ownership);
  static const char input[] =
    "key: value, number: -42.5e+2, nested: { child: text }";
  auto result = ZtCf::scan(ZuCSpan{input});
  ZuCheck(result.p<0>() == int(sizeof(input) - 1));
  ZuCheck(string(field(result.p<1>(), "key")) == "value");
  auto number = field(result.p<1>(), "number");
  ZuCheck(number && number->has<ZtCf::AnyNode::String>());
  ZuCheck(string(number) == "-42.5e+2");

  ZuPtr<const ZtCf::AnyNode> tree;
  {
    ZtString<> source =
      "cstr: retained, string: owned, number: 7, bool_: true, "
      "bytes: eHh4, strings: [one, two], nested: {value: 9}";
    auto scan = ZtCf::scan(source);
    ZuCheck(scan.p<0>() == int(source.length()));
    tree = ZuMv(scan.p<1>());
    source = "overwritten";
  }
  ZuCheck(string(field(tree, "cstr")) == "retained");
  ZuCheck(string(field(tree, "string")) == "owned");
  auto value = ZtCf::handler<CfData>(tree).ctor();
  ZuCheck(ZuCSpan{value.cstr} == "retained");
  ZuCheck(value.string == "owned");
  ZuCheck(value.number == 7);
  ZuCheck(value.nested.value == 9);
}

static void resolve() {
  ZuTestScope(resolve);
  auto result = ZtCf::eov_Object(
    "a: {b: [{value: zero}, {value: one}], empty: []}, text: leaf", true);
  ZuCheck(result.p<0>() >= 0);
  auto &root = result.p<1>();
  ZuCheck(root->resolve("") == root.ptr());
  ZuCheck(string(root->resolve("a.b[0].value")) == "zero");
  ZuCheck(string(root->resolve("a.b[1].value")) == "one");
  ZuCheck(root->resolve("a.b")->has<ZtCf::AnyNode::Array>());
  ZuCheck(!root->resolve("a.missing"));
  ZuCheck(!root->resolve("a.b[2]"));
  ZuCheck(!root->resolve("a.b[-1]"));
  ZuCheck(!root->resolve("a.b[0x0]"));
  ZuCheck(!root->resolve("a.b[4294967296]"));
  ZuCheck(!root->resolve("a.b[0]value"));
  ZuCheck(!root->resolve("a.b[0].[0]"));
  ZuCheck(!root->resolve("a.b."));
  ZuCheck(!root->resolve("text.value"));
  ZuCheck(!root->resolve("[0]"));
  ZuCheck(!root->resolve("a[0]"));
  ZuCheck(!root->resolve("a.empty[0]"));
}

static void quoting() {
  ZuTestScope(quoting);
  ZuTestCall(checkToken, "bare", "bare");
  ZuTestCall(checkToken, "'single'", "single");
  ZuTestCall(checkToken, "\"double\"", "double");
  ZuTestCall(checkToken, "''", "");
  ZuTestCall(checkToken, "foo'bar'\"baz\"'bam'", "foobarbazbam");
  ZuTestCall(checkToken, "'it\\'s'", "it's");
  ZuTestCall(checkToken, "bare\\ value", "bare value");
  ZuTestCall(checkToken, "'single \"double\"'", "single \"double\"");
  ZuTestCall(checkToken, "\"double 'single'\"", "double 'single'");
  ZuTestCall(checkToken, "\"a\\b\\f\\n\\r\\t\\/\\\\\\\"z\"",
    "a\b\f\n\r\t/\\\"z");
  ZuTestCall(checkToken, "\"\\u03bb\"", "\xce\xbb");
  ZuTestCall(checkToken,
    "\"\\ud83d\\udc04\"", "\xf0\x9f\x90\x84");

  {
    ZuTestRepeat(invalid, 8);
    for (auto bad : {
        ZuCSpan{"'unterminated"}, ZuCSpan{"\"unterminated"},
        ZuCSpan{"bare\\"}, ZuCSpan{"\"\\u12xz\""},
        ZuCSpan{"\"\\ud83d\""}, ZuCSpan{"\"\\udc04\""},
        ZuCSpan{"\"\\ud83d\\u03bb\""}, ZuCSpan{"\"\\u123\""}}) {
      ZtCf::AnyNode::String out;
      ZuCheck(ZtCf::eos<false>(bad, out) < 0);
    }
  }
  {
    ZtCf::AnyNode::String out;
    auto result = ZtCf::eos<true>("foo\\:bar", out);
    ZuCheck(result == 8);
    ZuCheck(out == "foo:bar");
  }
}

static void expansion() {
  ZuTestScope(expansion);
  const char *old = ::getenv("x");
  ZtString<> saved;
  if (old) saved = old;
#ifdef _WIN32
  _putenv_s("x", "X");
#else
  setenv("x", "X", 1);
#endif

  ZuTestCall(checkToken, "${x}", "X");
  ZuTestCall(checkToken, "a${x}b", "aXb");
  ZuTestCall(checkToken, "\"${x}${x}\"", "XX");
  ZuTestCall(checkToken, "'${x}'", "${x}");
  ZuTestCall(checkToken,
    "foo'bar'\"baz${x}bah\"'bam'", "foobarbazXbahbam");
  ZuTestCall(checkToken, "${ZTCF_TEST_UNSET}", "");

  {
    ZtCf::AnyNode::String out;
    auto result = ZtCf::eos<true>("${x}", out);
    ZuCheck(result == 4);
    ZuCheck(out == "${x}");
  }

  {
    ZuTestRepeat(invalid, 3);
    for (auto bad : {
        ZuCSpan{"${}"}, ZuCSpan{"${1x}"}, ZuCSpan{"${x"}}) {
      ZtCf::AnyNode::String out;
      ZuCheck(ZtCf::eos<false>(bad, out) < 0);
    }
  }

#ifdef _WIN32
  _putenv_s("x", old ? saved.data() : "");
#else
  if (old)
    setenv("x", saved.data(), 1);
  else
    unsetenv("x");
#endif
}

static void classification() {
  ZuTestScope(classification);
  auto scan = ZtCf::scan(
    "n: null, t: true, f: false, i: -42, d: .5, e: 1e+3, "
    "tv: trueValue, np: nullPath, ns: nanosecond, ni: 123abc, "
    "qn: \"42\", en: ${x}");
  ZuCheck(scan.p<0>() >= 0);
  {
    ZuTestRepeat(strings, 12);
    for (auto id : {
	"n", "t", "f", "i", "d", "e", "tv", "np", "ns", "ni", "qn", "en"})
      ZuCheck(field(scan.p<1>(), id)->has<ZtCf::AnyNode::String>());
  }
}

static void grammar() {
  ZuTestScope(grammar);
  ZuCheck(ZtCf::scan(ZuCSpan{}).p<0>() < 0);
  {
    ZuTestRepeat(valid, 10);
    for (auto good : {
        ZuCSpan{""}, ZuCSpan{"   "}, ZuCSpan{"x: 42, y: 43"},
        ZuCSpan{"x:42"}, ZuCSpan{"'x'\"y\": value"},
        ZuCSpan{"{x: 42, y: 43}"},
        ZuCSpan{"x: {y: [one, 'two', \"three\",],},"},
        ZuCSpan{"x: 1,, y: 2,"}, ZuCSpan{",x: 1"},
        ZuCSpan{"{,x: 1,}"}})
      ZuCheck(ZtCf::scan(good).p<0>() == int(good.length()));
  }
  {
    ZuTestRepeat(invalid, 8);
    for (auto bad : {
        ZuCSpan{"[one, two]"}, ZuCSpan{"x = 1"},
        ZuCSpan{"x: 1 garbage"}, ZuCSpan{"x: ]"},
        ZuCSpan{"x: 1}"}, ZuCSpan{"x:: 1"},
        ZuCSpan{"x: [one two]"}, ZuCSpan{"{x: 1"}})
      ZuCheck(ZtCf::scan(bad).p<0>() < 0);
  }
}

static void percent() {
  ZuTestScope(percent);
  const char *id = "ZTCF_PCT_DEFINE_1";
  const char *old = ::getenv(id);
  ZtString<> saved;
  if (old) saved = old;
#ifdef _WIN32
  _putenv_s(id, "environment");
#else
  setenv(id, "environment", 1);
#endif

  {
    ZmRef<ZtCf::Defines> defines = new ZtCf::Defines();
    auto scan = ZtCf::scan(
      "%define(ZTCF_PCT_DEFINE_1, first), "
      "one: ${ZTCF_PCT_DEFINE_1}, "
      "%define(ZTCF_PCT_DEFINE_1, 'second,value'), "
      "two: ${ZTCF_PCT_DEFINE_1}", {}, defines);
    ZuCheck(scan.p<0>() >= 0);
    ZuCheck(string(field(scan.p<1>(), "one")) == "first");
    ZuCheck(string(field(scan.p<1>(), "two")) == "second,value");

    auto next = ZtCf::scan(
      "value: ${ZTCF_PCT_DEFINE_1}", {}, defines);
    ZuCheck(next.p<0>() >= 0);
    ZuCheck(string(field(next.p<1>(), "value")) == "second,value");

    auto isolated = ZtCf::scan("value: ${ZTCF_PCT_DEFINE_1}");
    ZuCheck(isolated.p<0>() >= 0);
    ZuCheck(string(field(isolated.p<1>(), "value")) == "environment");
  }

  unsigned calls = 0;
  bool callbackOK = true;
  ZtCf::PctFn pctFn{[&calls, &callbackOK](
      ZuCSpan directive, ZuSpan<const ZuCSpan> args,
      ZtCf::PctExpandFn expand) {
    ++calls;
    if (directive == "outer") {
      bool valid = args.length() == 2;
      if (valid)
        valid = args[0] == "first,arg" && args[1] == "second value";
      callbackOK &= valid;
      return expand(
        "%define(ZTCF_PCT_NESTED, yes), "
        "included: ${ZTCF_PCT_NESTED}, %inner(), deep: {value: 9}");
    } else if (directive == "inner") {
      callbackOK &= !args;
      return expand("{inner: expanded}");
    } else if (directive == "array") {
      callbackOK &= !args;
      return expand("element: included");
    } else if (directive == "none") {
      callbackOK &= args.length() == 1 && args[0] == "ignored";
      return true;
    } else if (directive == "bad") {
      return expand("not an object body");
    }
    return false;
  }};

  {
    auto scan = ZtCf::scan(
      "before: 1, %outer('first,arg', \"second value\"), "
      "nested: {%none(ignored), retained: true}, "
      "array: [{%array()}], after: 2",
      pctFn);
    ZuCheck(scan.p<0>() >= 0);
    ZuCheck(calls == 4);
    ZuCheck(callbackOK);
    ZuCheck(string(field(scan.p<1>(), "included")) == "yes");
    ZuCheck(string(field(scan.p<1>(), "inner")) == "expanded");
    auto deep = field(scan.p<1>(), "deep");
    ZuCheck(string(field(deep, "value")) == "9");
    auto nested = field(scan.p<1>(), "nested");
    ZuCheck(string(field(nested, "retained")) == "true");
    auto array = field(scan.p<1>(), "array");
    ZuCheck(array && array->has<ZtCf::AnyNode::Array>());
    ZuCheck(string(field(array->data<ZtCf::AnyNode::Array>()[0], "element")) ==
      "included");
    ZuCheck(string(field(scan.p<1>(), "after")) == "2");
  }

  {
    auto scan = ZtCf::scan("'%quoted': one, \\%escaped: two");
    ZuCheck(scan.p<0>() >= 0);
    ZuCheck(string(field(scan.p<1>(), "%quoted")) == "one");
    ZuCheck(string(field(scan.p<1>(), "%escaped")) == "two");
  }

  {
    ZuCheck(ZtCf::scan("%unknown()").p<0>() < 0);
    ZuTestRepeat(invalid, 8);
    for (auto input : {
        ZuCSpan{"%1bad()"}, ZuCSpan{"%missing"}, ZuCSpan{"%unknown()"},
        ZuCSpan{"%define(one)"},
        ZuCSpan{"%define(one, two"}, ZuCSpan{"%none(a,,b)"},
        ZuCSpan{"%none(a,)"},
        ZuCSpan{"%bad()"}})
      ZuCheck(ZtCf::scan(input, pctFn).p<0>() < 0);
  }

#ifdef _WIN32
  _putenv_s(id, old ? saved.data() : "");
#else
  if (old)
    setenv(id, saved.data(), 1);
  else
    unsetenv(id);
#endif
}

static void loadTypes() {
  ZuTestScope(loadTypes);
  {
    auto minimumScan = ZtCf::scan("value: 0");
    auto minimum = ZtCf::handler<CfRange>(minimumScan.p<1>()).ctor();
    ZuCheck(minimum.value == 0);

    auto maximumScan = ZtCf::scan("value: 100tail");
    auto maximum = ZtCf::handler<CfRange>(maximumScan.p<1>()).ctor();
    ZuCheck(maximum.value == 100);

    auto belowScan = ZtCf::scan("value: -1");
    auto below = ZtCf::handler<CfRange>(belowScan.p<1>()).ctor();
    ZuCheck(below.value == ZuCmp<int>::null());

    auto aboveScan = ZtCf::scan("value: 101");
    auto above = ZtCf::handler<CfRange>(aboveScan.p<1>()).ctor();
    ZuCheck(above.value == ZuCmp<int>::null());
  }
  {
    auto scan = ZtCf::scan("float_: 1001, fixed: -1001, decimal: 1001");
    auto outside = ZtCf::handler<CfNumbers>(scan.p<1>()).ctor();
    ZuCheck(ZuCmp<double>::null(outside.float_));
    ZuCheck(ZuCmp<ZuFixed>::null(outside.fixed));
    ZuCheck(ZuCmp<ZuDecimal>::null(outside.decimal));
  }
  {
    auto numberScan = ZtCf::scan("nested: 42");
    ZuCheck(numberScan.p<0>() >= 0);
    ZuCheck(field(numberScan.p<1>(), "nested")->
	has<ZtCf::AnyNode::String>());
    auto number = ZtCf::handler<CfData>(numberScan.p<1>()).ctor();

    auto nullScan = ZtCf::scan("nested: null");
    ZuCheck(nullScan.p<0>() >= 0);
    ZuCheck(string(field(nullScan.p<1>(), "nested")) == "null");
    auto null = ZtCf::handler<CfData>(nullScan.p<1>()).ctor();

    ZuCheck(number.nested.value == ZuCmp<CfNested>::null().value);
    ZuCheck(null.nested.value == ZuCmp<CfNested>::null().value);

    auto objectScan = ZtCf::scan("nested: {}");
    ZuCheck(objectScan.p<0>() >= 0);
    auto object = ZtCf::handler<CfData>(objectScan.p<1>()).ctor();
    ZuCheck(object.nested.value == ZuCmp<int>::null());

    auto textScan = ZtCf::scan("text: null");
    ZuCheck(textScan.p<0>() >= 0);
    auto text = ZtCf::handler<CfTextData>(textScan.p<1>()).ctor();
    ZuCheck(text.text.value == "null");
  }
  {
    ZtString<> source =
      "base64: eHh4, base64URL: _w==, base32: PB4HQ===, "
      "hex: '787878', raw: 'raw bytes', vec: [eA==, eXk=]";
    auto scan = ZtCf::scan(source);
    ZuCheck(scan.p<0>() == int(source.length()));
    auto value = ZtCf::handler<CfBytes>(scan.p<1>()).ctor();
    ZuCheck(ZuBSpan{value.base64} == ZuBSpan{"xxx"});
    ZuCheck(value.base64URL.length() == 1 && value.base64URL[0] == 0xff);
    ZuCheck(ZuBSpan{value.base32} == ZuBSpan{"xxx"});
    ZuCheck(ZuBSpan{value.hex} == ZuBSpan{"xxx"});
    ZuCheck(ZuBSpan{value.raw} == ZuBSpan{"raw bytes"});
    ZuCheck(value.vec.length() == 2);
    ZuCheck(value.vec[0].length() == 1);
    ZuCheck(value.vec[0][0] == 'x');
    ZuCheck(value.vec[1].length() == 2);
    ZuCheck(value.vec[1][0] == 'y' && value.vec[1][1] == 'y');

    auto again = ZtCf::handler<CfBytes>(scan.p<1>()).ctor();
    ZuCheck(ZuBSpan{again.base64} == ZuBSpan{"xxx"});
    ZuCheck(string(field(scan.p<1>(), "base64")) == "eHh4");
  }
  {
    ZtString<> source =
      "i: -42, hex: deadbeef, enum_: Low, flags: Bit0|Bit2, "
      "float_: 1.25e2, fixed: 12.5, decimal: -0.125, "
      "time: 1700000000.25, ints: [1, -2, 3]";
    auto scan = ZtCf::scan(source);
    ZuCheck(scan.p<0>() == int(source.length()));
    auto value = ZtCf::handler<CfNumbers>(scan.p<1>()).ctor();
    ZuCheck(value.i == -42);
    ZuCheck(value.hex == 0xdeadbeef);
    ZuCheck(value.enum_ == CfValues::Low);
    ZuCheck(value.flags == (CfFlags::Bit0() | CfFlags::Bit2()));
    ZuCheck(value.float_ == 125.0);
    ZuCheck(value.fixed == ZuFixed{ZuDecimal{"12.5"}});
    ZuCheck(value.decimal == ZuDecimal{"-0.125"});
    ZuCheck(value.time.sec() == 1700000000);
    ZuCheck(value.time.nsec() == 250000000);
    ZuCheck(value.ints.length() == 3);
    ZuCheck(value.ints[0] == 1 && value.ints[1] == -2 && value.ints[2] == 3);

    CfNumbers loaded;
    ZtCf::handler<CfNumbers>(scan.p<1>()).load(loaded);
    ZuCheck(loaded.i == -42 && loaded.hex == 0xdeadbeef);
    auto update = ZtCf::scan("i: 7");
    ZtCf::handler<CfNumbers>(update.p<1>()).update(loaded);
    ZuCheck(loaded.i == 7);
    ZuCheck(loaded.hex == 0xdeadbeef);

    ZtString<> saved;
    ZtCf::save(saved, value);
    auto rescanned = ZtCf::scan(saved);
    ZuCheck(rescanned.p<0>() == int(saved.length()));
    auto roundTrip = ZtCf::handler<CfNumbers>(rescanned.p<1>()).ctor();
    ZuCheck(roundTrip.i == value.i);
    ZuCheck(roundTrip.hex == value.hex);
    ZuCheck(roundTrip.enum_ == value.enum_);
    ZuCheck(roundTrip.flags == value.flags);
    ZuCheck(roundTrip.time == value.time);
  }
  {
    CfOptional value;
    ZtString<> saved;
    ZtCf::save(saved, value);
    auto scan = ZtCf::scan(saved);
    ZuCheck(scan.p<0>() == int(saved.length()));
    auto roundTrip = ZtCf::handler<CfOptional>(scan.p<1>()).ctor();
    ZuCheck(!roundTrip.head);
    ZuCheck(!roundTrip.req[0]);
    ZuCheck(ZuCSpan{roundTrip.tail} == "tail");
  }
}

static void loadSave() {
  ZuTestScope(loadSave);
  ZtString<> source =
    "cstr: hello, string: 'world', number: 42, bool_: true, "
    "bytes: eHh4, strings: [one, 'two', \"three\"], "
    "nested: {value: 7}";
  ZtString<> original = source;
  auto scan = ZtCf::scan(source);
  ZuCheck(scan.p<0>() == int(source.length()));
  ZuCheck(source == original);

  auto value = ZtCf::handler<CfData>(scan.p<1>()).ctor();
  ZuCheck(ZuCSpan{value.cstr} == "hello");
  ZuCheck(value.string == "world");
  ZuCheck(value.number == 42);
  ZuCheck(value.bool_);
  ZuCheck(ZuBSpan{value.bytes} == ZuBSpan{"xxx"});
  ZuCheck(value.strings.length() == 3);
  ZuCheck(value.strings[0] == "one");
  ZuCheck(value.strings[1] == "two");
  ZuCheck(value.strings[2] == "three");
  ZuCheck(value.nested.value == 7);
  ZuCheck(source == original);

  auto again = ZtCf::handler<CfData>(scan.p<1>()).ctor();
  ZuCheck(ZuBSpan{again.bytes} == ZuBSpan{"xxx"});
  ZuCheck(source == original);

  ZtString<> saved;
  ZtCf::save(saved, value);
  auto rescanned = ZtCf::scan(saved);
  ZuCheck(rescanned.p<0>() == int(saved.length()));
  auto roundTrip = ZtCf::handler<CfData>(rescanned.p<1>()).ctor();
  ZuCheck(roundTrip.string == value.string);
  ZuCheck(roundTrip.number == value.number);
  ZuCheck(roundTrip.nested.value == value.nested.value);
}

int main(int argc, char **argv) {
  parse(argc, argv);
  ZuTestMain();

  ZuTestCall(ownership);
  ZuTestCall(resolve);
  ZuTestCall(quoting);
  ZuTestCall(expansion);
  ZuTestCall(classification);
  ZuTestCall(grammar);
  ZuTestCall(percent);
  ZuTestCall(loadTypes);
  ZuTestCall(loadSave);
}
