//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAlloc.hh>
#include <zlib/ZmHash.hh>

#include <zlib/ZfCf.hh>

#include "ZfHeapTest.hh"
#include "ZfMapTest.hh"

using namespace ZuTestUtil;

ZtEnumNS(, CfValues, int8_t, High, Low, Normal);

namespace CfFlags {
  ZtFlags(, Flags, uint8_t, Bit0, Bit1, Bit2);
}

ZtEnumImplNS(CfValues);
ZtEnumImplNS(CfFlags);

namespace ZuFieldProp::Cf {
  using Unix9 = Unix<ZfCf::Sec, 9>;
}

struct CfNested {
  int value = 0;
};

ZfStruct(, (CfNested, Cf),
  (((value), (Ctor<0>)), (Int32)));

using CfMapKey = ZtString<>;

using CfIntMap =
  ZfMapTest<"ZfTest.Cf.IntMap", ZmHashKV<CfMapKey, int>>;
using CfIntMapRef = ZmRef<CfIntMap>;
inline ZfCf::AsMap<ZfFieldTC::Int32> ZfCf_Fmt(CfIntMap *);

using CfObjMap =
  ZfMapTest<"ZfTest.Cf.ObjMap", ZmHashKV<CfMapKey, CfNested>>;
using CfObjMapRef = ZmRef<CfObjMap>;
inline ZfCf::AsMap<ZfFieldTC::UDT> ZfCf_Fmt(CfObjMap *);

struct CfMapHolder {
  CfIntMapRef map;
};
ZfStruct(, (CfMapHolder, Cf),
  (((map), (Ctor<0>, Mutable)), (UDT)));

struct CfUnionA { int foo = 0; };
struct CfUnionB { int bar = 0; };
ZfStruct(, (CfUnionA, Cf), (((foo), (Ctor<0>, Mutable)), (Int32)));
ZfStruct(, (CfUnionB, Cf), (((bar), (Ctor<0>, Mutable)), (Int32)));
struct CfUnionArray : public ZtArray<int> {
  using ZtArray<int>::ZtArray;
  friend ZfCf::AsArray<ZfFieldTC::Int32> ZfCf_Fmt(CfUnionArray *);
};
struct CfUnionText {
  ZtString<> value;
  CfUnionText() = default;
  CfUnionText(ZuCSpan value_) : value{value_} { }
  template <typename S> friend S &operator <<(S &s, const CfUnionText &v) {
    s << v.value;
    return s;
  }
  friend ZfCf::AsString ZfCf_Fmt(CfUnionText *);
};
struct CfUnionHolder {
  ZfCf::Union<CfUnionA, CfUnionB, CfIntMapRef, CfUnionArray, CfUnionText> value;
};
ZfStruct(, (CfUnionHolder, Cf),
  (((value), (Ctor<0>, Mutable)), (UDT)));

struct CfPtrObj_ : public ZmObject {
  int value = 0;
  CfPtrObj_(int value_ = 0) : value{value_} { }
};
using CfPtrObj = ZfHeapTest<"ZfTest.Cf.PtrObj", CfPtrObj_>;
ZfStruct(, (CfPtrObj, Cf),
  (((value), (Ctor<0>, Mutable)), (Int32)));

struct CfFmtOpt : public ZmObject { };
inline ZfCf::AsString ZfCf_Fmt(ZmRef<CfFmtOpt> *);
ZuAssert((!ZfCf::IsObjPtr<ZmRef<CfFmtOpt>>{}));

struct CfPtrText_ : public ZmObject {
  ZtString<> value;
  CfPtrText_() = default;
  CfPtrText_(ZuCSpan value_) : value{value_} { }
  template <typename S>
  friend S &operator <<(S &s, const CfPtrText_ &v) {
    s << v.value;
    return s;
  }
};
using CfPtrText = ZfHeapTest<"ZfTest.Cf.PtrText", CfPtrText_>;

struct CfPtrArray_ : public ZmObject, public ZtArray<ZmRef<CfPtrObj>> {
  using Base = ZtArray<ZmRef<CfPtrObj>>;
  using Base::Base;
  using Base::operator =;
};
using CfPtrArray = ZfHeapTest<"ZfTest.Cf.PtrArray", CfPtrArray_>;
inline ZfCf::AsArray<ZfFieldTC::UDT> ZfCf_Fmt(CfPtrArray *);

using CfPtrMap = ZfMapTest<
  "ZfTest.Cf.PtrMap", ZmHashKV<CfMapKey, ZmRef<CfPtrObj>>>;
inline ZfCf::AsMap<ZfFieldTC::UDT> ZfCf_Fmt(CfPtrMap *);

struct CfPtrHolder {
  ZmRef<CfPtrObj> object;
  ZmRef<CfPtrArray> objects;
  ZmRef<CfPtrText> text;
};
ZfStruct(, (CfPtrHolder, Cf),
  (((object), (Mutable)), (UDT)),
  (((objects), (Mutable)), (UDT)),
  (((text), (Mutable)), (UDT)));

ZuAssert((ZuIsSame<CfIntMap::Key, CfMapKey>{}));
ZuAssert((ZuIsSame<CfIntMap::Val, int>{}));
ZuAssert((ZuIsSame<
  ZmHeapID<CfIntMap>, ZuStringT<"ZfTest.Cf.IntMap">>{}));
ZuAssert((ZuIsSame<
  ZmHeapID<CfPtrObj>, ZuStringT<"ZfTest.Cf.PtrObj">>{}));
ZuAssert((ZuIsSame<
  ZmHeapID<CfPtrText>, ZuStringT<"ZfTest.Cf.PtrText">>{}));
ZuAssert((ZuIsSame<
  ZmHeapID<CfPtrArray>, ZuStringT<"ZfTest.Cf.PtrArray">>{}));

struct CfText {
  CfText() = default;
  CfText(ZuCSpan value_) : value{value_} { }

  ZtString<> value;

  friend ZfCf::AsString ZfCf_Fmt(CfText *);
};

struct CfTextData {
  CfText text;
};

ZfStruct(, (CfTextData, Cf),
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

ZfStruct(, (CfData, Cf),
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

ZfStruct(, (CfBytes, Cf),
  (((base64),		(Ctor<0>, Cf::Base64)),		(Bytes)),
  (((base64URL),	(Ctor<1>, Cf::Base64URL)),	(Bytes)),
  (((base32),		(Ctor<2>, Cf::Base32)),		(Bytes)),
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

ZfStruct(, (CfNumbers, Cf),
  (((i),		(Ctor<0>, Mutable)),			(Int32)),
  (((hex),		(Ctor<1>, Hex)),			(UInt32)),
  (((enum_),		(Ctor<2>, Enum<CfValues::Map>)),	(Int32)),
  (((flags),		(Ctor<3>, Flags<CfFlags::Map>)),	(UInt128)),
  (((float_),		(Ctor<4>, (Range<-1000.0, 1000.0>))),	(Float)),
  (((fixed),		(Ctor<5>,
    (Range<ZuDecimal{-1000}, ZuDecimal{1000}>))),		(Fixed)),
  (((decimal),		(Ctor<6>,
    (Range<ZuDecimal{-1000}, ZuDecimal{1000}>))),		(Decimal)),
  (((time),		(Ctor<7>, Cf::Unix9)),			(Time)),
  (((ints),		(Ctor<8>)),				(Int32Vec)));

struct CfOptional {
  const char *head = nullptr;
  const char *req = "";
  const char *tail = "tail";
};

ZfStruct(, (CfOptional, Cf),
  (((head), (Ctor<0>, Cf::Opt)), (CString)),
  (((req),  (Ctor<1>)), (CString)),
  (((tail), (Ctor<2>)), (CString)));

struct CfRange {
  int value = 42;
};

ZfStruct(, (CfRange, Cf),
  (((value), (Ctor<0>, (Range<0, 100>))), (Int32, 42)));

struct CfRequired {
  int required;
  int optional;
};

ZfStruct(, (CfRequired, Cf),
  (((required), (Ctor<0>, Required)), (Int32)),
  (((optional), (Ctor<1>)), (Int32)));

struct CfFormatInt {
  unsigned value = 0;
};
using CfIntFormat = ZuFmt::Hex<false, ZuFmt::Right<8>>;
ZfStruct(, (CfFormatInt, Cf),
  (((value), (Ctor<0>, Cf::Number<CfIntFormat>)), (UInt32)));

static const ZfCf::AnyNode *field(
    const ZfCf::AnyNode *node, ZuCSpan id) {
  if (!node || !node->has<ZfCf::AnyNode::Object>()) return nullptr;
  const auto &fields = node->data<ZfCf::AnyNode::Object>();
  for (unsigned i = 0, n = fields.length(); i < n; i++)
    if (fields[i].p<0>() == id) return fields[i].p<1>();
  return nullptr;
}

static ZuCSpan string(const ZfCf::AnyNode *node) {
  if (!node || !node->has<ZfCf::AnyNode::String>()) return {};
  return node->data<ZfCf::AnyNode::String>();
}

template <typename L>
static bool loadError(L l) {
  try {
    l();
  } catch (const ZeException &) {
    return true;
  }
  return false;
}

template <typename L>
static ZeString loadException(L l) {
  try {
    l();
  } catch (const ZeException &e) {
    ZeString message;
    message << e;
    return message;
  }
  return {};
}

static ZeString syntaxError(ZuCSpan input, ZfCf::PctFn pctFn = {}) {
  try {
    ZfCf::scan(input, ZuMv(pctFn));
  } catch (const ZeException &e) {
    ZeString message;
    message << e;
    return message;
  }
  return {};
}

static void checkToken(ZuCSpan in, ZuCSpan expected) {
  ZuTestScope(checkToken);
  ZtString<> source;
  source << "value: " << in;
  auto result = ZfCf::scan(source);
  ZuCheck(result.p<0>() == int(source.length()));
  auto node = field(result.p<1>(), "value");
  const auto &out = node->data<ZfCf::AnyNode::String>();
  ZuCheck(out == expected);
  ZuCheck(!out.data()[out.length()]);
}

static void ownership() {
  ZuTestScope(ownership);
  static const char input[] =
    "key: value, number: -42.5e+2, nested: { child: text }";
  auto result = ZfCf::scan(ZuCSpan{input});
  ZuCheck(result.p<0>() == int(sizeof(input) - 1));
  ZuCheck(string(field(result.p<1>(), "key")) == "value");
  auto number = field(result.p<1>(), "number");
  ZuCheck(number && number->has<ZfCf::AnyNode::String>());
  ZuCheck(string(number) == "-42.5e+2");

  ZuPtr<const ZfCf::AnyNode> tree;
  {
    ZtString<> source =
      "cstr: retained, string: owned, number: 7, bool_: true, "
      "bytes: eHh4, strings: [one, two], nested: {value: 9}";
    auto scan = ZfCf::scan(source);
    ZuCheck(scan.p<0>() == int(source.length()));
    tree = ZuMv(scan.p<1>());
    source = "overwritten";
  }
  ZuCheck(string(field(tree, "cstr")) == "retained");
  ZuCheck(string(field(tree, "string")) == "owned");
  auto value = ZfCf::handler<CfData>(tree).ctor();
  ZuCheck(ZuCSpan{value.cstr} == "retained");
  ZuCheck(value.string == "owned");
  ZuCheck(value.number == 7);
  ZuCheck(value.nested.value == 9);
}

static void resolve() {
  ZuTestScope(resolve);
  auto result = ZfCf::scan(
    "a: {b: [{value: zero}, {value: one}], empty: []}, text: leaf");
  ZuCheck(result.p<0>() >= 0);
  auto &root = result.p<1>();
  auto a = root->resolve("a");
  auto b = root->resolve("a.b");
  auto element = root->resolve("a.b[0]");
  auto value = root->resolve("a.b[0].value");
  ZuCheck(!root->parent);
  ZuCheck(a->parent == root.ptr());
  ZuCheck(b->parent == a);
  ZuCheck(element->parent == b);
  ZuCheck(value->parent == element);
  ZuCheck(root->resolve("") == root.ptr());
  ZuCheck(string(root->resolve("a.b[0].value")) == "zero");
  ZuCheck(string(root->resolve("a.b[1].value")) == "one");
  ZuCheck(root->resolve("a.b")->has<ZfCf::AnyNode::Array>());
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
      ZtString<> source;
      source << "value: " << bad;
      ZuCheck(syntaxError(source));
    }
  }
  {
    auto result = ZfCf::scan("foo\\:bar: value");
    ZuCheck(string(field(result.p<1>(), "foo:bar")) == "value");
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
  ZuTestCall(checkToken, "${ZFCF_TEST_UNSET}", "");

  {
    auto result = ZfCf::scan("${x}: value");
    ZuCheck(string(field(result.p<1>(), "${x}")) == "value");
  }

  {
    ZuTestRepeat(invalid, 3);
    for (auto bad : {
        ZuCSpan{"${}"}, ZuCSpan{"${1x}"}, ZuCSpan{"${x"}}) {
      ZtString<> source;
      source << "value: " << bad;
      ZuCheck(syntaxError(source));
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
  auto scan = ZfCf::scan(
    "n: null, t: true, f: false, i: -42, d: .5, e: 1e+3, "
    "tv: trueValue, np: nullPath, ns: nanosecond, ni: 123abc, "
    "qn: \"42\", en: ${x}");
  ZuCheck(scan.p<0>() >= 0);
  {
    ZuTestRepeat(strings, 12);
    for (auto id : {
	"n", "t", "f", "i", "d", "e", "tv", "np", "ns", "ni", "qn", "en"})
      ZuCheck(field(scan.p<1>(), id)->has<ZfCf::AnyNode::String>());
  }
}

static void grammar() {
  ZuTestScope(grammar);
  ZuCheck(syntaxError(ZuCSpan{}));
  {
    ZuTestRepeat(valid, 10);
    for (auto good : {
        ZuCSpan{""}, ZuCSpan{"   "}, ZuCSpan{"x: 42, y: 43"},
        ZuCSpan{"x:42"}, ZuCSpan{"'x'\"y\": value"},
        ZuCSpan{"{x: 42, y: 43}"},
        ZuCSpan{"x: {y: [one, 'two', \"three\",],},"},
        ZuCSpan{"x: 1,, y: 2,"}, ZuCSpan{",x: 1"},
        ZuCSpan{"{,x: 1,}"}})
      ZuCheck(ZfCf::scan(good).p<0>() == int(good.length()));
  }
  {
    ZuTestRepeat(invalid, 8);
    for (auto bad : {
        ZuCSpan{"[one, two]"}, ZuCSpan{"x = 1"},
        ZuCSpan{"x: 1 garbage"}, ZuCSpan{"x: ]"},
      ZuCSpan{"x: 1}"}, ZuCSpan{"x:: 1"},
      ZuCSpan{"x: [one two]"}, ZuCSpan{"{x: 1"}})
      ZuCheck(syntaxError(bad));
  }

  ZuCheck(syntaxError("a: one,\nb: two,\nc: ]") ==
    "syntax error at line 3, column 4 (offset 19) near ']'");
  ZuCheck(syntaxError("{a: one}\n junk") ==
    "syntax error at line 2, column 2 (offset 10) near 'j'");
}

static void comments() {
  ZuTestScope(comments);
  ZuCSpan source =
    "# heading\n"
    "a: one # first field\n"
    "# between fields\n"
    "b: { # nested heading\n"
    "  c: two # nested field\n"
    "}, escaped: \\#value, '#key': value # final comment";
  auto scan = ZfCf::scan(source);
  ZuCheck(scan.p<0>() == int(source.length()));
  ZuCheck(string(field(scan.p<1>(), "a")) == "one");
  ZuCheck(string(field(field(scan.p<1>(), "b"), "c")) == "two");
  ZuCheck(string(field(scan.p<1>(), "escaped")) == "#value");
  ZuCheck(string(field(scan.p<1>(), "#key")) == "value");

  ZuCheck(syntaxError("key # comment\n: value"));
  ZuCheck(syntaxError("key: # comment\nvalue"));
}

static void percent() {
  ZuTestScope(percent);
  const char *id = "ZFCF_PCT_DEFINE_1";
  const char *old = ::getenv(id);
  ZtString<> saved;
  if (old) saved = old;
#ifdef _WIN32
  _putenv_s(id, "environment");
#else
  setenv(id, "environment", 1);
#endif

  {
    ZmRef<ZfCf::Defines> defines = new ZfCf::Defines();
    auto scan = ZfCf::scan(
      "%define(ZFCF_PCT_DEFINE_1, first)\n"
      "one: ${ZFCF_PCT_DEFINE_1},\n"
      "%define(ZFCF_PCT_DEFINE_1, 'second,value') # replace\n"
      "two: ${ZFCF_PCT_DEFINE_1}", {}, defines);
    ZuCheck(scan.p<0>() >= 0);
    ZuCheck(string(field(scan.p<1>(), "one")) == "first");
    ZuCheck(string(field(scan.p<1>(), "two")) == "second,value");

    auto next = ZfCf::scan(
      "value: ${ZFCF_PCT_DEFINE_1}", {}, defines);
    ZuCheck(next.p<0>() >= 0);
    ZuCheck(string(field(next.p<1>(), "value")) == "second,value");

    auto isolated = ZfCf::scan("value: ${ZFCF_PCT_DEFINE_1}");
    ZuCheck(isolated.p<0>() >= 0);
    ZuCheck(string(field(isolated.p<1>(), "value")) == "environment");
  }

  unsigned calls = 0;
  bool callbackOK = true;
  ZfCf::PctFn pctFn{[&calls, &callbackOK](
      ZfCf::Scan &context, ZuCSpan directive, ZuSpan<const ZuCSpan> args,
      ZfCf::PctExpandFn expand) {
    ++calls;
    if (directive == "outer") {
      bool valid = args.length() == 2;
      if (valid)
        valid = args[0] == "first,arg" && args[1] == "second value";
      if (valid) {
	valid = context.defines();
	context.defines()->add(
	  ZfCf::DefKey{"ZFCF_PCT_CONTEXT"}, ZfCf::DefVal{"context"});
      }
      callbackOK &= valid;
      return expand(
        "%define(ZFCF_PCT_NESTED, yes)\n"
        "included: ${ZFCF_PCT_NESTED}, fromContext: ${ZFCF_PCT_CONTEXT},\n"
	"%inner()\n"
	"deep: {value: 9}");
    } else if (directive == "inner") {
      callbackOK &= !args;
      return expand("{inner: expanded}");
    } else if (directive == "array") {
      callbackOK &= !args;
      return expand("element: included");
    } else if (directive == "none") {
      callbackOK &= args.length() == 1 && args[0] == "ignored";
      return true;
    } else if (directive == "many") {
      callbackOK &= args.length() == 5 && args[0] == "one" &&
	args[1] == "two value" && args[2] == "three" &&
	args[3] == "four" && args[4] == "context";
      return true;
    } else if (directive == "bad") {
      return expand("not an object body");
    }
    return false;
  }};

  {
    auto scan = ZfCf::scan(
      "before: 1,\n"
      "%outer('first,arg', \"second value\")\n"
      "%many(one, 'two value', three, four, ${ZFCF_PCT_CONTEXT}) # five\n"
      "nested: {\n"
      "%none(ignored)\n"
      "retained: true},\n"
      "array: [{\n"
      "%array()\n"
      "}], after: 2",
      pctFn);
    ZuCheck(scan.p<0>() >= 0);
    ZuCheck(calls == 5);
    ZuCheck(callbackOK);
    ZuCheck(string(field(scan.p<1>(), "included")) == "yes");
    ZuCheck(string(field(scan.p<1>(), "fromContext")) == "context");
    ZuCheck(string(field(scan.p<1>(), "inner")) == "expanded");
    auto deep = field(scan.p<1>(), "deep");
    ZuCheck(deep->parent == scan.p<1>().ptr());
    ZuCheck(string(field(deep, "value")) == "9");
    auto nested = field(scan.p<1>(), "nested");
    ZuCheck(string(field(nested, "retained")) == "true");
    auto array = field(scan.p<1>(), "array");
    ZuCheck(array && array->has<ZfCf::AnyNode::Array>());
    ZuCheck(string(field(array->data<ZfCf::AnyNode::Array>()[0], "element")) ==
      "included");
    ZuCheck(string(field(scan.p<1>(), "after")) == "2");
  }

  {
    auto scan = ZfCf::scan("'%quoted': one, \\%escaped: two");
    ZuCheck(scan.p<0>() >= 0);
    ZuCheck(string(field(scan.p<1>(), "%quoted")) == "one");
    ZuCheck(string(field(scan.p<1>(), "%escaped")) == "two");
  }

  {
    ZuCheck(syntaxError("ok: 1,\n%unknown()") ==
      "syntax error at line 2, column 1 (offset 7) near '%'");
    ZuTestRepeat(invalid, 12);
    for (auto input : {
        ZuCSpan{"%1bad()"}, ZuCSpan{"%missing"}, ZuCSpan{"%unknown()"},
        ZuCSpan{"%define(one)"},
        ZuCSpan{"%define(one, two"}, ZuCSpan{"%none(a,,b)"},
        ZuCSpan{"%none(a,)"},
        ZuCSpan{"%bad()"}, ZuCSpan{" %none()"},
        ZuCSpan{"ok: 1, %none()"}, ZuCSpan{"%none() trailing"},
        ZuCSpan{"%none(\na)"}})
      ZuCheck(syntaxError(input, pctFn));
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

static void duplicates() {
  ZuTestScope(duplicates);

  auto direct = ZfCf::scan("value: 1, value: 2");
  ZuCheck(string(direct.p<1>()->resolve("value")) == "2");
  auto value = ZfCf::handler<CfNested>(direct.p<1>()).ctor();
  ZuCheck(value.value == 2);
  const auto &directFields =
    direct.p<1>()->data<ZfCf::AnyNode::Object>();
  ZuCheck(directFields.length() == 2);
  ZuCheck(string(directFields[0].p<1>()) == "1");
  ZuCheck(string(directFields[1].p<1>()) == "2");

  ZfCf::PctFn pctFn{[](ZfCf::Scan &, ZuCSpan directive,
      ZuSpan<const ZuCSpan>, ZfCf::PctExpandFn expand) {
    return directive == "inject" && expand("value: 2");
  }};
  auto expanded = ZfCf::scan(
    "value: 1,\n"
    "%inject()\n"
    "value: 3", pctFn);
  ZuCheck(string(expanded.p<1>()->resolve("value")) == "3");
  value = ZfCf::handler<CfNested>(expanded.p<1>()).ctor();
  ZuCheck(value.value == 3);
  const auto &expandedFields =
    expanded.p<1>()->data<ZfCf::AnyNode::Object>();
  ZuCheck(expandedFields.length() == 3);
  ZuCheck(string(expandedFields[0].p<1>()) == "1");
  ZuCheck(string(expandedFields[1].p<1>()) == "2");
  ZuCheck(string(expandedFields[2].p<1>()) == "3");
}

static bool checkScalarTypes(const ZfCf::AnyNode *node)
{
  if (node->has<ZfCf::AnyNode::String>()) {
    return node->scalarType == ZfCf::ScalarTC::String;
  } else if (node->has<ZfCf::AnyNode::Array>()) {
    if (node->scalarType != ZfCf::ScalarTC::None) return false;
    for (auto &child: node->data<ZfCf::AnyNode::Array>())
      if (!checkScalarTypes(child)) return false;
  } else {
    if (node->scalarType != ZfCf::ScalarTC::None) return false;
    for (auto &field: node->data<ZfCf::AnyNode::Object>())
      if (!checkScalarTypes(field.p<1>())) return false;
  }
  return true;
}

static void loadTypes() {
  ZuTestScope(loadTypes);
  {
    auto scan = ZfCf::scan(
      "bool_: true, i: 1, float_: 1.5, enum_: Normal, "
      "flags: Bit0|Bit1, time: 1.25");
    ZuCheck(checkScalarTypes(scan.p<1>()));
  }
  {
    auto missingScan = ZfCf::scan("optional: 1");
    ZuCheck(loadError([&]() {
      ZfCf::handler<CfRequired>(missingScan.p<1>()).ctor();
    }));

    auto nullScan = ZfCf::scan("required: -2147483648");
    auto null = ZfCf::handler<CfRequired>(nullScan.p<1>()).ctor();
    ZuCheck(ZuNull(null.required));

    auto validScan = ZfCf::scan("required: 0");
    auto valid = ZfCf::handler<CfRequired>(validScan.p<1>()).ctor();
    ZuCheck(valid.required == 0);
    ZuCheck(ZuNull(valid.optional));

    CfRequired loaded{1, 2};
    ZuCheck(loadError([&]() {
      ZfCf::handler<CfRequired>(missingScan.p<1>()).load(loaded);
    }));

    CfRequired updated{ZuCmp<int>::null(), 2};
    ZfCf::handler<CfRequired>(validScan.p<1>()).update(updated);
    ZuCheck(ZuNull(updated.required));
    ZuCheck(updated.optional == 2);
  }
  {
    auto minimumScan = ZfCf::scan("value: 0");
    auto minimum = ZfCf::handler<CfRange>(minimumScan.p<1>()).ctor();
    ZuCheck(minimum.value == 0);

    auto maximumScan = ZfCf::scan("value: 100");
    auto maximum = ZfCf::handler<CfRange>(maximumScan.p<1>()).ctor();
    ZuCheck(maximum.value == 100);

    auto trailingScan = ZfCf::scan("value: 100tail");
    ZuCheck(loadError([&]() {
      ZfCf::handler<CfRange>(trailingScan.p<1>()).ctor();
    }));

    auto unknownScan = ZfCf::scan("value-junk: 99");
    auto unknown = ZfCf::handler<CfRange>(unknownScan.p<1>()).ctor();
    ZuCheck(unknown.value == 42);

    auto belowScan = ZfCf::scan("value: -1");
    ZuCheck(loadError([&]() {
      ZfCf::handler<CfRange>(belowScan.p<1>()).ctor();
    }));

    auto aboveScan = ZfCf::scan("value: 101");
    ZuCheck(loadError([&]() {
      ZfCf::handler<CfRange>(aboveScan.p<1>()).ctor();
    }));
  }
  {
    auto scan = ZfCf::scan("float_: 1001, fixed: -1001, decimal: 1001");
    ZuCheck(loadError([&]() {
      ZfCf::handler<CfNumbers>(scan.p<1>()).ctor();
    }));
  }
  {
    auto scan = ZfCf::scan("bool_: maybe");
    ZuCheck(loadError([&]() {
      ZfCf::handler<CfData>(scan.p<1>()).ctor();
    }));
  }
  {
    for (auto source: {
      ZuCSpan{"i: 1x"}, ZuCSpan{"enum_: unknown"},
      ZuCSpan{"enum_: NormalJunk"},
      ZuCSpan{"flags: Bit0|unknown"}, ZuCSpan{"float_: 1.0x"},
      ZuCSpan{"fixed: 1.0x"}, ZuCSpan{"decimal: 1.0x"},
      ZuCSpan{"time: 1.0x"}, ZuCSpan{"fixed: 1e19"},
      ZuCSpan{"decimal: 1e19"}, ZuCSpan{"time: 1e19"}}) {
      auto scan = ZfCf::scan(source);
      ZuCheck(loadError([&]() {
	ZfCf::handler<CfNumbers>(scan.p<1>()).ctor();
      }));
    }
  }
  {
    auto numberScan = ZfCf::scan("nested: 42");
    ZuCheck(numberScan.p<0>() >= 0);
    ZuCheck(field(numberScan.p<1>(), "nested")->
	has<ZfCf::AnyNode::String>());
    ZuCheck(loadError([&]() {
      ZfCf::handler<CfData>(numberScan.p<1>()).ctor();
    }));
    ZuCheck(loadError([&]() {
      ZfCf::handler<CfNested>(field(numberScan.p<1>(), "nested")).ctor();
    }));

    auto nullScan = ZfCf::scan("nested: null");
    ZuCheck(nullScan.p<0>() >= 0);
    ZuCheck(string(field(nullScan.p<1>(), "nested")) == "null");
    ZuCheck(loadError([&]() {
      ZfCf::handler<CfData>(nullScan.p<1>()).ctor();
    }));

    auto objectScan = ZfCf::scan("nested: {}");
    ZuCheck(objectScan.p<0>() >= 0);
    auto object = ZfCf::handler<CfData>(objectScan.p<1>()).ctor();
    ZuCheck(object.nested.value == ZuCmp<int>::null());

    auto textScan = ZfCf::scan("text: null");
    ZuCheck(textScan.p<0>() >= 0);
    auto text = ZfCf::handler<CfTextData>(textScan.p<1>()).ctor();
    ZuCheck(text.text.value == "null");
    ZuCheck(loadError([&]() {
      ZfCf::handler<CfText>(objectScan.p<1>()).ctor();
    }));
  }
  {
    for (auto source: {
      ZuCSpan{"strings: nope"}, ZuCSpan{"ints: nope"}}) {
      auto scan = ZfCf::scan(source);
      ZuCheck(loadError([&]() {
	if (source[0] == 's')
	  ZfCf::handler<CfData>(scan.p<1>()).ctor();
	else
	  ZfCf::handler<CfNumbers>(scan.p<1>()).ctor();
      }));
    }
    auto scan = ZfCf::scan("vec: nope");
    ZuCheck(loadError([&]() {
      ZfCf::handler<CfBytes>(scan.p<1>()).ctor();
    }));
  }
  {
    for (auto source: {
      ZuCSpan{"base64: 'eA==junk'"}, ZuCSpan{"base64URL: '_w!junk'"},
      ZuCSpan{"base32: 'PB4HQ===junk'"}, ZuCSpan{"hex: '78787'"}}) {
      auto scan = ZfCf::scan(source);
      ZuCheck(loadError([&]() {
	ZfCf::handler<CfBytes>(scan.p<1>()).ctor();
      }));
    }
  }
  {
    ZtString<> source =
      "base64: eHh4, base64URL: _w, base32: PB4HQ===, "
      "hex: '787878', raw: 'raw bytes', vec: [eA==, eXk=]";
    auto scan = ZfCf::scan(source);
    ZuCheck(scan.p<0>() == int(source.length()));
    auto value = ZfCf::handler<CfBytes>(scan.p<1>()).ctor();
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

    auto again = ZfCf::handler<CfBytes>(scan.p<1>()).ctor();
    ZuCheck(ZuBSpan{again.base64} == ZuBSpan{"xxx"});
    ZuCheck(string(field(scan.p<1>(), "base64")) == "eHh4");
  }
  {
    ZtString<> source =
      "i: -42, hex: deadbeef, enum_: Low, flags: Bit0|Bit2, "
      "float_: 1.25e2, fixed: 12.5, decimal: -0.125, "
      "time: 1700000000.25, ints: [1, -2, 3]";
    auto scan = ZfCf::scan(source);
    ZuCheck(scan.p<0>() == int(source.length()));
    auto value = ZfCf::handler<CfNumbers>(scan.p<1>()).ctor();
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
    ZfCf::handler<CfNumbers>(scan.p<1>()).load(loaded);
    ZuCheck(loaded.i == -42 && loaded.hex == 0xdeadbeef);
    auto update = ZfCf::scan("i: 7");
    ZfCf::handler<CfNumbers>(update.p<1>()).update(loaded);
    ZuCheck(loaded.i == 7);
    ZuCheck(loaded.hex == 0xdeadbeef);

    ZtString<> saved;
    ZfCf::save(saved, value);
    auto rescanned = ZfCf::scan(saved);
    ZuCheck(rescanned.p<0>() == int(saved.length()));
    auto roundTrip = ZfCf::handler<CfNumbers>(rescanned.p<1>()).ctor();
    ZuCheck(roundTrip.i == value.i);
    ZuCheck(roundTrip.hex == value.hex);
    ZuCheck(roundTrip.enum_ == value.enum_);
    ZuCheck(roundTrip.flags == value.flags);
    ZuCheck(roundTrip.time == value.time);
  }
  {
    CfOptional value;
    ZtString<> saved;
    ZfCf::save(saved, value);
    auto scan = ZfCf::scan(saved);
    ZuCheck(scan.p<0>() == int(saved.length()));
    auto roundTrip = ZfCf::handler<CfOptional>(scan.p<1>()).ctor();
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
  auto scan = ZfCf::scan(source);
  ZuCheck(scan.p<0>() == int(source.length()));
  ZuCheck(source == original);

  auto value = ZfCf::handler<CfData>(scan.p<1>()).ctor();
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

  auto again = ZfCf::handler<CfData>(scan.p<1>()).ctor();
  ZuCheck(ZuBSpan{again.bytes} == ZuBSpan{"xxx"});
  ZuCheck(source == original);

  ZtString<> saved;
  ZfCf::save(saved, value);
  auto rescanned = ZfCf::scan(saved);
  ZuCheck(rescanned.p<0>() == int(saved.length()));
  auto roundTrip = ZfCf::handler<CfData>(rescanned.p<1>()).ctor();
  ZuCheck(roundTrip.string == value.string);
  ZuCheck(roundTrip.number == value.number);
  ZuCheck(roundTrip.nested.value == value.nested.value);
}

static void compatibility()
{
  ZuTestScope(compatibility);

  {
    auto scan = ZfCf::scan("optional: 1");
    auto message = loadException([&]() {
      ZfCf::handler<CfRequired>(scan.p<1>()).ctor();
    });
    constexpr auto prefix = "\"required\" missing at:\n"_Zu;
    ZuCheck(ZuCSpan{message}.prefix(prefix) == prefix.length());
    ZuCheck(message.length() > prefix.length());
  }
  {
    auto scan = ZfCf::scan("nested: 42");
    ZuCheck(loadException([&]() {
      ZfCf::handler<CfData>(scan.p<1>()).ctor();
    }) == "\"nested\": expected UDT");
  }
  {
    auto scan = ZfCf::scan("bool_: maybe");
    ZuCheck(loadException([&]() {
      ZfCf::handler<CfData>(scan.p<1>()).ctor();
    }) == "\"bool_\": invalid boolean \"maybe\"");
  }
  {
    auto scan = ZfCf::scan("enum_: unknown");
    ZuCheck(loadException([&]() {
      ZfCf::handler<CfNumbers>(scan.p<1>()).ctor();
    }) == "\"enum_\" did not match { High = 0, Low = 1, Normal = 2 }");
  }
  {
    auto scan = ZfCf::scan("value: 101");
    ZuCheck(loadException([&]() {
      ZfCf::handler<CfRange>(scan.p<1>()).ctor();
    }) == "\"value\" out of range min(0) <= 101 <= max(100)");
  }
  {
    auto scan = ZfCf::scan("base64: 'eA==junk'");
    ZuCheck(loadException([&]() {
      ZfCf::handler<CfBytes>(scan.p<1>()).ctor();
    }) == "\"base64\": invalid encoded bytes \"eA==junk\"");
  }
  {
    auto scan = ZfCf::scan("i: 1x");
    ZuCheck(loadException([&]() {
      ZfCf::handler<CfNumbers>(scan.p<1>()).ctor();
    }) == "\"i\": invalid integer \"1x\"");
  }
  {
    auto scan = ZfCf::scan("time: 1.0x");
    ZuCheck(loadException([&]() {
      ZfCf::handler<CfNumbers>(scan.p<1>()).ctor();
    }) == "\"time\": invalid date/time \"1.0x\"");
  }

  CfNumbers value;
  value.i = -42;
  value.hex = 0xdeadbeef;
  value.enum_ = CfValues::Low;
  value.flags = CfFlags::Bit0() | CfFlags::Bit2();
  value.float_ = 125;
  value.fixed = ZuFixed{ZuDecimal{"12.5"}};
  value.decimal = ZuDecimal{"-0.125"};
  value.time = ZuTime{1700000000, 250000000};
  value.ints = ZtArray<int>{1, -2, 3};

  ZtString<> saved;
  ZfCf::save(saved, value);
  ZuCheck(saved ==
    "{\"i\":-42,\"hex\":\"deadbeef\",\"enum_\":\"Low\","
    "\"flags\":\"Bit0|Bit2\",\"float_\":125,\"fixed\":12.5,"
    "\"decimal\":-0.125,\"time\":\"1700000000.25\","
    "\"ints\":[1,-2,3]}");

  saved.null();
  ZfCf::saveUpd(saved, value);
  ZuCheck(saved == "{\"i\":-42}");

  saved.null();
  ZfCf::saveDel(saved, value);
  ZuCheck(saved == "{}");
}

static void formattedInteger()
{
  ZuTestScope(formattedInteger);
  auto scan = ZfCf::scan("value: 00abcdef");
  auto loaded = ZfCf::handler<CfFormatInt>(scan.p<1>()).ctor();
  ZuCheck(loaded.value == 0xabcdef);
}

static void maps()
{
  ZuTestScope(maps);

  auto scan = ZfCf::scan("{a: 1, b: 2, 'a.b': 3}");
  auto map = ZmRef(ZfCf::handler<CfIntMap>(scan.p<1>()).alloc());
  ZuCheck(map);
  ZuCheck(map->count_() == 3);
  ZuCheck(map->findVal("a") == 1);
  ZuCheck(map->findVal("b") == 2);
  ZuCheck(map->findVal("a.b") == 3);

  ZtString<> saved;
  ZfCf::save(saved, map);
  ZtString<> direct;
  ZfCf::save(direct, *map);
  ZuCheck(direct == saved);
  direct.null();
  ZfCf::save(direct, map.ptr());
  ZuCheck(direct == saved);
  auto roundScan = ZfCf::scan(saved);
  auto round = ZmRef(ZfCf::handler<CfIntMap>(roundScan.p<1>()).alloc());
  ZuCheck(round->count_() == 3);
  ZuCheck(round->findVal("a.b") == 3);

  auto ptr = map.ptr();
  auto loadScan = ZfCf::scan("{loaded: 4}");
  ZfCf::handler<CfIntMap>(loadScan.p<1>()).load(*map);
  ZuCheck(map.ptr() == ptr);
  ZuCheck(map->count_() == 1);
  ZuCheck(map->findVal("loaded") == 4);

  map->add("kept", 5);
  auto updateScan = ZfCf::scan("{loaded: 6, added: 7}");
  ZfCf::handler<CfIntMap>(updateScan.p<1>()).update(*map);
  ZuCheck(map.ptr() == ptr);
  ZuCheck(map->count_() == 3);
  ZuCheck(map->findVal("loaded") == 6);
  ZuCheck(map->findVal("kept") == 5);
  ZuCheck(map->findVal("added") == 7);

  auto storage = ZmAlloc(CfIntMap, 1);
  ZfCf::handler<CfIntMap>(loadScan.p<1>()).new_(storage.data);
  auto &placed = storage[0];
  ZuCheck(placed.findVal("loaded") == 4);
  placed.~CfIntMap();

  auto objectScan = ZfCf::scan("{one: {value: 8}}");
  auto objects = ZmRef(ZfCf::handler<CfObjMap>(objectScan.p<1>()).alloc());
  ZuCheck(objects->findVal("one").value == 8);
  saved.null();
  ZfCf::save(saved, objects);
  ZuCheck(saved.find("\"one\":{\"value\":8}") >= 0);

  auto holderScan = ZfCf::scan("map: {nested: 9}");
  auto holder = ZfCf::handler<CfMapHolder>(holderScan.p<1>()).ctor();
  ZuCheck(holder.map->findVal("nested") == 9);

  CfIntMapRef empty = new CfIntMap{};
  saved.null();
  ZfCf::save(saved, empty);
  ZuCheck(saved == "{}");
  CfIntMapRef null;
  saved.null();
  ZfCf::save(saved, null);
  ZuCheck(saved == "null");
  saved.null();
  ZfCf::saveUpd(saved, null);
  ZuCheck(saved == "null");
  saved.null();
  ZfCf::saveDel(saved, null);
  ZuCheck(saved == "null");
}

static void unions()
{
  ZuTestScope(unions);

  CfUnionHolder value;
  value.value = CfUnionA{42};
  ZtString<> saved;
  ZfCf::save(saved, value);
  auto tree = ZfCf::scan(saved);
  auto loaded = ZfCf::handler<CfUnionHolder>(tree.p<1>()).ctor();
  auto node = loaded.value.p<const ZfCf::AnyNode *>();
  loaded.value = ZfCf::handler<CfUnionA>(node).ctor();
  ZtString<> round;
  ZfCf::save(round, loaded);
  ZuCheck(round == saved);

  auto arrayTree = ZfCf::scan("value: [1, 2, 3]");
  auto array = ZfCf::handler<CfUnionHolder>(arrayTree.p<1>()).ctor();
  ZuCheck((array.value.is<const ZfCf::AnyNode *>()));
  auto arrayNode = array.value.p<const ZfCf::AnyNode *>();
  ZuCheck(arrayNode && arrayNode->has<ZfCf::AnyNode::Array>());
  array.value = ZfCf::handler<CfUnionArray>(arrayNode).ctor();
  ZuCheck(array.value.p<CfUnionArray>().length() == 3);
  saved.null();
  ZfCf::save(saved, array);
  ZuCheck(saved == "{\"value\":[1,2,3]}");

  auto scalarTree = ZfCf::scan("value: text");
  auto scalar = ZfCf::handler<CfUnionHolder>(scalarTree.p<1>()).ctor();
  auto scalarNode = scalar.value.p<const ZfCf::AnyNode *>();
  ZuCheck(scalarNode && scalarNode->has<ZfCf::AnyNode::String>());
  scalar.value = ZfCf::handler<CfUnionText>(scalarNode).ctor();
  saved.null();
  ZfCf::save(saved, scalar);
  ZuCheck(saved == "{\"value\":\"text\"}");

  auto nullTree = ZfCf::scan("value: null");
  auto nullValue = ZfCf::handler<CfUnionHolder>(nullTree.p<1>()).ctor();
  ZuCheck((nullValue.value.is<const ZfCf::AnyNode *>()));
  ZuCheck(nullValue.value.p<const ZfCf::AnyNode *>() != nullptr);
  using UnionHandler = typename ZfCf::As<decltype(nullValue.value)>::
    template Handler<decltype(nullValue.value), ZuFacet::Cf>;
  ZuCheck(!UnionHandler::valid(nullptr));

  CfIntMapRef map = new CfIntMap{};
  map->add("answer", 42);
  CfUnionHolder mapped;
  mapped.value = map;
  saved.null();
  ZfCf::save(saved, mapped);
  ZuCheck(saved == "{\"value\":{\"answer\":42}}");

  CfUnionHolder empty;
  saved.null();
  ZfCf::save(saved, empty);
  ZuCheck(saved == "{\"value\":null}");

  ZfCf::Union<CfUnionA, ZmRef<CfPtrObj>> pointer;
  pointer = ZmRef<CfPtrObj>{new CfPtrObj{9}};
  saved.null();
  ZfCf::save(saved, pointer);
  ZuCheck(saved == "{\"value\":9}");
  pointer = ZmRef<CfPtrObj>{};
  saved.null();
  ZfCf::save(saved, pointer);
  ZuCheck(saved == "null");
}

static void pointers()
{
  ZuTestScope(pointers);

  auto tree = ZfCf::scan(
    "object: {value: 1}, objects: [{value: 2}, {value: 3}], text: hello");
  auto value = ZfCf::handler<CfPtrHolder>(tree.p<1>()).ctor();
  ZuCheck(value.object && value.object->value == 1);
  ZuCheck(value.objects && value.objects->length() == 2);
  ZuCheck((*value.objects)[0] && (*value.objects)[0]->value == 2);
  ZuCheck(value.text && value.text->value == "hello");

  ZtString<> saved;
  ZfCf::save(saved, value);
  ZuCheck(saved.find("\"text\":\"hello\"") >= 0);
  ZuCheck(saved.find("\"objects\":[{\"value\":2},{\"value\":3}]") >= 0);

  auto first = (*value.objects)[0].ptr();
  (*value.objects)[1] = nullptr;
  auto update = ZfCf::scan("v: [{value: 4}, {value: 5}]");
  ZfCf::handler<CfPtrArray>(field(update.p<1>(), "v")).update(*value.objects);
  ZuCheck((*value.objects)[0].ptr() == first && first->value == 4);
  ZuCheck((*value.objects)[1] && (*value.objects)[1]->value == 5);

  auto bad = ZfCf::scan("v: [wrong, {value: 6}]");
  ZuCheck(loadError([&bad, &value] {
    ZfCf::handler<CfPtrArray>(field(bad.p<1>(), "v")).update(*value.objects);
  }));
  ZuCheck(first->value == 4);

  saved.null();
  ZfCf::save(saved, value.object);
  ZuCheck(saved == "{\"value\":1}");
  ZmRef<CfPtrObj> null;
  saved.null();
  ZfCf::save(saved, null);
  ZuCheck(saved == "null");

  auto raw = ZfCf::handler<CfPtrObj>(
    ZfCf::scan("{value: 7}").p<1>()).alloc();
  ZuCheck(raw && raw->value == 7);
  delete raw;

  auto mapTree = ZfCf::scan("one: {value: 8}");
  auto map = ZmRef(ZfCf::handler<CfPtrMap>(mapTree.p<1>()).alloc());
  ZuCheck(map->findVal("one") && map->findVal("one")->value == 8);
  saved.null();
  ZfCf::save(saved, map);
  ZuCheck(saved.find("\"one\":{\"value\":8}") >= 0);
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
  ZuTestCall(comments);
  ZuTestCall(percent);
  ZuTestCall(duplicates);
  ZuTestCall(loadTypes);
  ZuTestCall(loadSave);
  ZuTestCall(compatibility);
  ZuTestCall(formattedInteger);
  ZuTestCall(maps);
  ZuTestCall(unions);
  ZuTestCall(pointers);
}
