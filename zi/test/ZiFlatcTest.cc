//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// FBS to native Z declarations and ZiFB metadata.
// Parsing, resolution and emission are separate passes over stable handles.

#include <zlib/ZuTestUtil.hh>
#include "ZiTestResidue.hh"

#define ZFLATC_TEST
#include "../src/zflatc.cc"

using namespace ZuTestUtil;

static bool bad(ZuCSpan in)
{
  try { Scan scan{3, in}; while (scan.next().kind != Tok::End); }
  catch (const Failure &e) { return e.loc.file == 3; }
  return false;
}
static bool badInt(ZuCSpan in)
{
  try { uintValue({Tok::Number, in, {}}); }
  catch (const Failure &) { return true; }
  return false;
}
static bool badStr(ZuCSpan in)
{
  try { unquote({Tok::String, in, {}}); }
  catch (const Failure &) { return true; }
  return false;
}
static void run()
{
  ZuTestScope(run);
  Scan scan{7, " \t// line\n/* block */foo_bar: [uint];"};
  auto key = scan.next();
  ZuCheck(key.kind == Tok::Name && key.text == "foo_bar");
  ZuCheck(key.loc.file == 7 && key.loc.offset == 21);
  ZuCheck(scan.next().kind == ':');
  ZuCheck(scan.next().kind == '[');
  ZuCheck(scan.next().text == "uint");
  ZuCheck(scan.next().kind == ']');
  ZuCheck(scan.next().kind == ';');
  ZuCheck(scan.next().kind == Tok::End);
  Scan nums{0, "-12 +3 .5 1.25e-3 0xFF 0x1.fp+2"};
  for (unsigned i = 0; i < 6; ++i) ZuCheck(nums.next().kind == Tok::Number);
  ZuCheck(nums.next().kind == Tok::End);
  ZuCheck(bad("/*"));
  ZuCheck(bad("\"unterminated"));
  ZuCheck(bad("\"line\nfeed\""));
  ZuCheck(bad("\"escaped\\\nfeed\""));
  ZuCheck(bad("1e+"));
  ZuCheck(bad("0x"));
  ZuCheck(bad("0x1.2"));
  ZuCheck(bad("12oops"));
  ZuCheck(bad("1.2.3"));
  ZuCheck(uintValue({Tok::Number, "18446744073709551615", {}}) == UINT64_MAX);
  ZuCheck(uintValue({Tok::Number, "0xFFFFFFFFFFFFFFFF", {}}) == UINT64_MAX);
  ZuCheck(uintValue({Tok::Number, "+00042", {}}) == 42);
  ZuCheck(uintValue({Tok::Number, "012", {}}) == 12);
  ZuCheck(badInt("18446744073709551616"));
  ZuCheck(badInt("0x10000000000000000"));
  ZuCheck(badInt("-1"));
  ZuCheck(badInt("1e2"));
  ZuCheck(badInt(""));
  Scan str{0, "\"plain\" \"a\\\"b\""};
  ZuCheck(unquote(str.next()) == "plain");
  ZuCheck(unquote(str.next()) == "a\"b");
  ZuCheck(unquote({Tok::String, "a\\n\\x62\\u20AC\\uD83D\\uDE00", {}}) == "a\nb\xe2\x82\xac\xf0\x9f\x98\x80");
  ZuCheck(badStr("\\x0G"));
  ZuCheck(badStr("\\x0x"));
  ZuCheck(badStr("\\u0x00"));
  ZuCheck(badStr("\\uZZZZ"));
  ZuCheck(badStr("\\uD800"));
  ZuCheck(badStr("\\uDC00"));
  ZuCheck(badStr("\\q"));
  ZuCheck(scalarCode("uint") == ZfFieldTC::UInt32);
  ZuCheck(scalarCode("uint_more") == -1);
  bool keyword = false;
  try { identifier("class", {}); } catch (const Failure &) { keyword = true; }
  ZuCheck(keyword);
  identifier("className", {});
}
static void parsed(Schema &schema, ZuCSpan text)
{
  unsigned file = schema.source(Zi::Path{"unit.fbs"}, Text{text});
  Parser{schema, file, schema.files[file].text}.run();
  schema.files[file].active = false;
  schema.resolve();
}

static bool invalid(ZuCSpan text)
{
  try { Schema schema; parsed(schema, text); } catch (const Failure &) { return true; }
  return false;
}
static bool invalidAt(ZuCSpan text, ZuCSpan marker, ZuCSpan reason)
{
  try { Schema schema; parsed(schema, text); }
  catch (const Failure &failure) {
    return failure.message == reason && failure.loc.offset == text.find(marker);
  }
  return false;
}
static bool invalidEmit(ZuCSpan text)
{
  try {
    Schema schema;
    parsed(schema, text);
    Options opts;
    opts.output = "unit";
    Emit{schema, 0, opts}.header();
  } catch (const Failure &) { return true; }
  return false;
}

static void syntax()
{
  ZuTestScope(syntax);
  Schema schema;
  parsed(schema, "namespace test; table Row { child: Child; } "
    "table Child { value: int = -2147483648; } root_type test.Row;");
  ZuCheck(schema.files[0].rootType == 0);
  ZuCheck(schema.objects[0].fields[0].type.target == 1);
  ZuCheck(invalid("table A { value: int = 2147483648; }"));
  ZuCheck(invalid("table A { value: uint = -1; }"));
  ZuCheck(invalid("table A { value: float = 1e1000; }"));
  ZuCheck(invalid("table A { value: int (required); }"));
  ZuCheck(invalid("table A { value: Missing; }"));
  ZuCheck(invalid("table A { value: int; value: int; }"));
  ZuCheck(invalid("table A { value: int (id:1); }"));
  ZuCheck(invalid("table A { value: int (unknown); }"));
  ZuCheck(invalid("table A { value:[int] (key); }"));
  ZuCheck(invalid("struct A { value:int (key); }"));
  ZuCheck(invalid("table A { a:int (key); b:int (key); }"));
  ZuCheck(invalid("table A { value:int (key, deprecated); }"));
  Schema keyed;
  parsed(keyed, "namespace test; table Row { key:string (key); value:int; extra:int; }");
  ZuCheck(keyed.objects[0].fields[0].key && keyed.objects[0].fields[0].required);
  Options opts;
  opts.output = "keyed";
  auto generated = Emit{keyed, 0, opts}.header();
  ZuCheck(strstr(generated.data(), "FB::Key") != nullptr);
  ZuCheck(strstr(generated.data(), "extra") != nullptr);
  ZuCheck(!mapEntry(keyed, keyed.objects[0]));
  ZuCheck(strstr(generated.data(), "::Map_<") == nullptr);
  Schema maps;
  parsed(maps, "namespace test; table PairsEntry { key:int (key); value:string; } "
    "table Pairs { entries:[PairsEntry]; } root_type Pairs;");
  ZuCheck(mapEntry(maps, maps.objects[0]));
  auto mapHeader = Emit{maps, 0, opts}.header();
  ZuCheck(strstr(mapHeader.data(), "ZiFB::AsMap<\"test.Pairs\"") != nullptr);
  ZuCheck(strstr(mapHeader.data(), "Zflatc_::PairsEntry::Map_<") != nullptr);
  ZuCheck(invalidEmit("table PairsEntry { key:int (key); value:int; } "
    "table Pairs { other:int; }"));
  ZuCheck(invalidEmit("table PairsEntry { key:int (key); value:int; } "
    "table Other { entry:PairsEntry; }"));
  ZuCheck(invalid("table A { badName: int; }"));
  ZuCheck(invalid("table A { foo_bar:int; fooBar:int; }"));
  ZuCheck(invalidAt("enum E:byte { A, A }",
    "A }", "duplicate enum/union name"));
  Schema enumDefaults;
  parsed(enumDefaults, "table Row { first:E = B; second:E = A; } "
    "enum E:byte { A, B }");
  ZuCheck(defaultExpr(enumDefaults, enumDefaults.objects[0].fields[0]) == "1" &&
    defaultExpr(enumDefaults, enumDefaults.objects[0].fields[1]) == "0");
  ZuCheck(invalidAt("table T {} union U { T } "
    "table A { value:U; value_type:ubyte; }",
    "value:U", "union tag-name collision"));
  ZuCheck(invalid("table A { class:int; }"));
  ZuCheck(invalid("table A { bad_:int; }"));
  ZuCheck(invalid("namespace Zflatc_; table A {}"));
  ZuCheck(invalid("table Zflatc_ {}"));
  ZuCheck(invalid("table ZuFwd {}"));
  ZuCheck(invalid("table ZmObject {}"));
  ZuCheck(invalid("table ZtArray {}"));
  ZuCheck(invalid("table ZdbThing {}"));
  ZuCheck(invalid("namespace Zu; table Row {}"));
  ZuCheck(invalid("namespace test.Zu; table Row {}"));
  ZuCheck(invalid("namespace Zf; table Custom {}"));
  ZuCheck(invalid("enum E:byte { ZuFwd }"));
  ZuCheck(invalid("struct A { b:B; } struct B { a:A; }"));
  ZuCheck(invalid("struct A { value:string; }"));
  ZuCheck(invalid("struct A (force_align:4) { value:ulong; }"));
  ZuCheck(invalid("struct A { value:ulong (force_align:4); }"));
  ZuCheck(invalid("struct A {}"));
  ZuCheck(invalid("struct A { b:B; } struct B (force_align:1) { value:uint; }"));
  ZuCheck(invalid("namespace Zf; struct Int128 { h:ulong; l:uint; }"));
  ZuCheck(invalid("table A {} root_type Missing;"));
  ZuCheck(invalid("struct A { value:int; } root_type A;"));
  ZuCheck(invalid("namespace Zf; table Bytes { data:[ubyte] (required); }"));
  ZuCheck(invalid("namespace Zf; table Bytes { data:[ubyte] (deprecated); }"));
  ZuCheck(invalid("namespace Zf; table Bytes { data:[ubyte] (force_align:16); }"));
  ZuCheck(invalid("namespace Zf; table Bitmap { data:[ulong] (required); }"));
  Schema values;
  parsed(values, "namespace Zf; struct Int128 { h:ulong; l:ulong; } "
    "namespace test; table Row { wide:Zf.Int128; }");
  ZuCheck(values.objects[0].builtin == ZfFieldTC::Int128);
  Schema ip;
  parsed(ip, "namespace Zi; struct IPv4 { addr:[ubyte:4]; } "
    "struct IPv6 { addr:[ubyte:16]; } union IP { IPv4, IPv6 } "
    "namespace test; table Row { ip:Zi.IP; }");
  ZuCheck(ip.objects[0].native == "ZiFB::IPv4");
  ZuCheck(ip.objects[1].native == "ZiFB::IPv6");
  ZuCheck(ip.objects[2].native == "ZiIP");
  ZuCheck(invalid("namespace Zi; struct IPv4 { addr:[ubyte:5]; }"));
  ZuCheck(invalid("namespace Zi; struct IPv4 { addr:[ubyte:4]; } union IP { IPv4 }"));
  Schema layout;
  parsed(layout, "struct A (force_align:16) { x:byte; y:uint; } "
    "struct B { x:ushort; a:[A:3]; }");
  ZuCheck(layout.objects[0].wireAlign == 16 && layout.objects[0].size == 16);
  ZuCheck(layout.objects[1].wireAlign == 16 && layout.objects[1].size == 64);
  Text enums{"enum E:byte {"};
  for (unsigned i = 0; i < 129; ++i) enums << 'V' << ZuBox<unsigned>{i} << ',';
  enums << '}';
  ZuCheck(invalid(enums));
  Text flags{"enum E:ubyte (bit_flags) {"};
  for (unsigned i = 0; i < 9; ++i) flags << 'V' << ZuBox<unsigned>{i} << ',';
  flags << '}';
  ZuCheck(invalid(flags));
  Text unions{"table T {} union U {"};
  for (unsigned i = 0; i < 256; ++i) unions << 'V' << ZuBox<unsigned>{i} << ":T,";
  unions << '}';
  ZuCheck(invalid(unions));
  ZuCheck(invalid("enum E:byte { N }"));
  ZuCheck(invalid("enum E:byte { T }"));
  ZuCheck(invalid("enum E:byte { Map }"));
  ZuCheck(invalid("enum E:byte { lookup }"));
  ZuCheck(invalid("enum E:byte { ZiFB_Fmt }"));
  ZuCheck(invalid("enum E:uint (bit_flags) { Flag_ }"));
  ZuCheck(invalid("enum E:uint (bit_flags) { AbcdefghijklmnopqrstuvwxyzAB }"));
  Schema files;
  files.source(Zi::Path{"short.fbs"}, Input{"table A {}"});
  auto begin = files.files[0].text.data();
  Parser{files, 0, files.files[0].text}.run();
  unsigned capacity = files.files.size();
  do files.source({}, Input{"// short"}); while (files.files.size() == capacity);
  ZuCheck(files.files[0].text.data() == begin);
  files.resolve();
  ZuCheck(files.objects[0].local == "A");
}

static void emission()
{
  ZuTestScope(emission);
  Schema schema;
  parsed(schema, "namespace test; table Row { display_name:string; "
    "names:[string]; count:ulong = 18446744073709551615; }");
  Options opts;
  opts.output = "unit";
  Emit emit{schema, 0, opts};
  auto hh = emit.header();
  auto cc = emit.source("unit.hh");
  opts.output = "dir-a/unit";
  auto guardA = Emit{schema, 0, opts}.header();
  opts.output = "dir_a/unit";
  auto guardB = Emit{schema, 0, opts}.header();
  ZuCheck(guardA.find("#ifndef Zflatc_dir_2Da_2Funit_HH") >= 0);
  ZuCheck(guardB.find("#ifndef Zflatc_dir_5Fa_2Funit_HH") >= 0);
  opts.output = "unit";
  ZuCheck(hh.find("ZtStringHeapID<\"test.Row.displayName\">") >= 0);
  ZuCheck(hh.find("ZtStringHeapID<\"test.Row.names\">") >= 0);
  ZuCheck(hh.find("ZtArrayHeapID<\"test.Row.names\">") >= 0);
  ZuCheck(hh.find("18446744073709551615ULL") >= 0);
  ZuCheck(hh.find("ZfStruct(, (Row, FB)") >= 0);
  ZuCheck(cc.find("ZfStructImpl(Row, FB)") >= 0);
  opts.ref = "ZmObject";
  opts.api = "ZiAPI";
  Emit refs{schema, 0, opts};
  auto rh = refs.header();
  ZuCheck(rh.find("public ::ZmObject") >= 0);
  ZuCheck(rh.find("ZfStruct(ZiAPI, (Row, FB)") >= 0);
  Schema names;
  parsed(names, "table O0 { e0:[string]; heap:string; base:[uint]; foo_1:int; ip_v4_address:int; }");
  auto nh = Emit{names, 0, opts}.header();
  ZuCheck(nh.find("namespace Zflatc_::O0") >= 0);
  ZuCheck(nh.find("ZuDerive(Base_, (ZtArray<::uint32_t") >= 0);
  ZuCheck(nh.find("E0_") >= 0 && nh.find("Heap_ = ZuVoid") >= 0);
  ZuCheck(names.objects[0].fields[3].cpp == "foo_1");
  ZuCheck(names.objects[0].fields[4].cpp == "ipV4Address");
  Schema counts;
  parsed(counts, "table Counts { ref:int; deref:int; mvref:int; "
    "ref_count:int; with_ref:int; }");
  Options plain;
  plain.output = "counts";
  auto countHeader = Emit{counts, 0, plain}.header();
  ZuCheck(countHeader.find("refCount") >= 0);
  using Reserved = ZuStringTL<"ref", "deref", "mvref", "ref_count", "with_ref">;
  ZuUnroll::all<Reserved>([&]<typename N>() {
    Text text{"table Counts { "}; text << N{}() << ":int; }";
    Schema count;
    parsed(count, text);
    bool rejected = false;
    try { Emit{count, 0, opts}.header(); }
    catch (const Failure &e) { rejected = e.message.find("reference-counting base") >= 0; }
    ZuCheck(rejected);
  });
  Schema type;
  parsed(type, "table ref { value:int; }");
  bool rejected = false;
  try { Emit{type, 0, opts}.header(); }
  catch (const Failure &) { rejected = true; }
  ZuCheck(rejected);
  using BadNames = ZuStringTL<"table Base {}", "union Base {}", "table row { row:int; }">;
  ZuUnroll::all<BadNames>([&]<typename N>() {
    Schema named;
    parsed(named, N{}());
    bool rejected = false;
    try { Emit{named, 0, plain}.header(); }
    catch (const Failure &) { rejected = true; }
    ZuCheck(rejected);
  });
}

struct ReplaceFile : public ZiFile {
  static inline Zi::Path reject;
  static inline bool failed = false;
  static int rename(const Zi::Path &from, const Zi::Path &to) {
    if (!failed && to == reject) {
      failed = true;
      return Zi::IOError;
    }
    return ZiFile::rename(from, to);
  }
};

static bool write(const Zi::Path &path, ZuCSpan data)
{
  ZiFile file;
  return file.open(path, ZiFile::Write | ZiFile::GC) == Zi::OK &&
    file.write(data.data(), data.length()) == Zi::OK && file.close() == Zi::OK;
}

static Text read(const Zi::Path &path)
{
  ZiFile file;
  if (file.open(path, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK) return {};
  auto n = file.fstat().size();
  Text data;
  data.length(n);
  if (n && file.read(data.data(), n) != n) return {};
  return data;
}

static void rollback()
{
  ZuTestScope(rollback);
  auto dir = ZiTestResidue::dir("rollback");
  auto hh = ZiFile::append(dir, "pair.hh");
  auto cc = ZiFile::append(dir, "pair.cc");
  ZuCheck(write(hh, "previous header"));
  ZuCheck(write(cc, "previous source"));
  ReplaceFile::reject = cc;
  ReplaceFile::failed = false;
  bool failed = false;
  {
    Publish<ReplaceFile> publish;
    publish.stage(hh, "next header");
    publish.stage(cc, "next source");
    try { publish.commit(); } catch (const Failure &) { failed = true; }
  }
  ZuCheck(failed && ReplaceFile::failed);
  ZuCheck(read(hh) == "previous header");
  ZuCheck(read(cc) == "previous source");
  ZiFile::remove(hh);
  ZiFile::remove(cc);
  ReplaceFile::failed = false;
  failed = false;
  {
    Publish<ReplaceFile> publish;
    publish.stage(hh, "new header");
    publish.stage(cc, "new source");
    try { publish.commit(); } catch (const Failure &) { failed = true; }
  }
  ZuCheck(failed && ReplaceFile::failed);
  ZuCheck(!ZiStat{hh}.exists() && !ZiStat{cc}.exists());
  ZuCheck(!ZiTestResidue::glob(ZiFile::dirname(hh), "pair.hh.zflatc.").length());
  ZuCheck(!ZiTestResidue::glob(ZiFile::dirname(cc), "pair.cc.zflatc.").length());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("ZiFlatcTest");
  ZuTestMain();
  ZuTestCall(run);
  ZuTestCall(syntax);
  ZuTestCall(emission);
  ZuTestCall(rollback);
}
