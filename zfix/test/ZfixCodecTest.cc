//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuArray.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfixFmt.hh>

using namespace ZuTestUtil;
using namespace Zfix::Fmt;
using Text = ZtString<ZtStringHeapID<"Zfix.Test">>;

struct Record {
  int		number = 99;
  Text		text = "old";
  int8_t	small = 20;
  bool		flag = false;
  ZuDecimal	price;
  ZuDateTime	time;
};

ZfStruct(, (Record, FIX),
  (number, (FIX::ID<1000000>, Mutable, Deflt<7>),	Int32),
  (text, (FIX::ID<11>, Mutable),			String),
  (small, (FIX::ID<12>),				Int8),
  (flag, (FIX::ID<13>),				Bool),
  (price, (FIX::ID<44>),				Decimal),
  (time, (FIX::ID<60>),				DateTime));
ZfStructImpl(Record);
ZfStructImpl(Record, FIX);

struct Immediate {
  int aValue = 0, bValue = 0;
  unsigned calls = 0;
  int a() const { return aValue; }
  int b() const { return bValue; }
  void a(int v) { aValue = v; calls = calls * 10 + 1; }
  void b(int v) { bValue = v; calls = calls * 10 + 2; }
};
ZfStruct(, (Immediate, FIX), ((a, Fn), (FIX::ID<11>), Int32),
  ((b, Fn), (FIX::ID<21>), Int32));

static void singlePass()
{
  ZuTestScope(singlePass);
  ZuAssert((liveRead<Immediate, ZuFacet::FIX>()));
  Immediate o;
  auto e = load(o, "21=2\1" "999=skip\1" "11=1\1"_z);
  ZuCheck(!e && o.aValue == 1 && o.bValue == 2 && o.calls == 21);
  Immediate partial;
  e = load(partial, "11=7\1" "malformed"_z);
  ZuCheck(e && partial.aValue == 7 && partial.calls == 1);
}

struct Args {
  Text	name;
  int	seq;
  int	value = 45;

  Args(ZuCSpan name_, int seq_) : name{name_}, seq{seq_} { }
};

ZfStruct(, (Args, FIX),
  (seq, (FIX::ID<21>, Ctor<1>, Required),		Int32),
  (name, (FIX::ID<22>, Ctor<0>, Required),		String),
  (value, (FIX::ID<23>, Deflt<9>),			Int32));
ZfStructImpl(Args);
ZfStructImpl(Args, FIX);

struct ComponentData {
  Record	record;
  int		extra = 1;
};

ZfStruct(, (ComponentData, FIX),
  (record, ,					UDT),
  (extra, (FIX::ID<24>),				Int32));
ZfStructImpl(ComponentData);
ZfStructImpl(ComponentData, FIX);

ZuStructFacet(Millis);
ZfStructRender(, Record, Millis,
  (time, FIX::TimeFmt<ZuDateTimeFmt::FIX<3>>));
ZfStructImpl(Record, Millis);

struct Accessors {
  int	key = 99;
  int	value = 77;
  int	immutable = 55;

  int fn() const { return value; }
  void fn(int n) { value = n; }

  int read() const { return immutable; }
};

ZfStruct(, (Accessors, FIX),
  (key, (FIX::ID<1>, Keys<0>, Deflt<1>),		Int32),
  ((fn, Fn), (FIX::ID<2>, Mutable, Deflt<2>),	Int32),
  (immutable, (FIX::ID<3>, Required),		Int32),
  ((read, RdFn), (FIX::ID<4>),			Int32));
ZfStructImpl(Accessors);
ZfStructImpl(Accessors, FIX);

class Owned : public ZmNHeap<Owned> {
friend inline ZuStringT<"Zfix.TestOwned"> ZmNHeapID(Owned *);
public:
  explicit Owned(int value) : m_value{value} { }

  Owned(const Owned &) = delete;
  Owned(Owned &&) = delete;

  int value() const { return m_value; }
  void value(int v) { m_value = v; }
private:
  int	m_value;
};

ZfStruct(, (Owned, FIX),
  ((value, Fn), (FIX::ID<21>, Ctor<0>, Required),	Int32));
ZfStructImpl(Owned);
ZfStructImpl(Owned, FIX);

struct Character {
  int8_t	value = 'X';
};

ZfStruct(, (Character, FIX),
  (value, (FIX::ID<54>, FIX::Char),		Int8));
ZfStructImpl(Character);
ZfStructImpl(Character, FIX);

ZuStructFacet(Seconds);
struct SecondsConfig : DefltConfig {
  using TimeFmt = ZuDateTimeFmt::FIX<0>;
};

ZfixConfig(Seconds, SecondsConfig);
ZfStructRender(, Record, Seconds, time);
ZfStructImpl(Record, Seconds);

struct Label {
  Text	value;
  Label() = default;

  explicit Label(ZuCSpan s) : value{s} { }
};

struct LabelFormat {
  template <typename O> struct Handler {
    static O load(ZuCSpan value, Error &error) {
      if (value == "valid") return O(value);
      error.code = ErrorCode::Value;
      return O{};
    }

    template <typename Out> static void save(Out &out, const O &o) {
      out << o.value;
    }
  };

};

LabelFormat Zfix_StringFmt(Label *);
struct ScalarUDT {
  Label	label;
};

ZfStruct(, (ScalarUDT, FIX),
  (label, (FIX::ID<55>),		UDT));
ZfStructImpl(ScalarUDT);
ZfStructImpl(ScalarUDT, FIX);

struct Borrowed {
  ZuCSpan	value;
};

ZfStruct(, (Borrowed, FIX),
  (value, (FIX::ID<11>),		String));
ZfStructImpl(Borrowed);
ZfStructImpl(Borrowed, FIX);

struct Empty { friend AsObject Zfix_Fmt(Empty *); };

ZfStruct(, (Empty, FIX));
ZfStructImpl(Empty);
ZfStructImpl(Empty, FIX);

ZtEnumNS(, Side, int8_t, Buy, Sell);
namespace Features {

  ZtFlags(, Features, uint8_t, One, Two, Three);
}

ZtEnumImplNS(Side);
ZtEnumImplNS(Features);
struct Named {
  int		side = 0;
  unsigned	features = 0;
};

ZfStruct(, (Named, FIX),
  (side, (FIX::ID<54>, Enum<Side::Map>),		Int32),
  (features, (FIX::ID<9003>, Flags<Features::Map>),	UInt32));
ZfStructImpl(Named);
ZfStructImpl(Named, FIX);

static void names()
{
  ZuTestScope(names);
  constexpr auto Wire = "54=Sell\1" "9003=One Three\1"_z;
  Named n;
  ZuCheck(!load(n, Wire));
  ZuCheck(n.side == Side::Sell && n.features == (Features::One() | Features::Three()));
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, n) && out.data() == Wire);
  ZuCheck(!load(n, "54=invalid\1"_z));
  ZuCheck(!load(n, "9003=One invalid\1"_z));
  ZuCheck(!load(n, "54=SellX\1"_z));
}

struct Numbers {
  int128_t	signed_ = 0;
  uint128_t	unsigned_ = 0;
  double	float_ = 0;
  ZuFixed	fixed;
};

ZfStruct(, (Numbers, FIX),
  (signed_, (FIX::ID<8001>),	Int128),
  (unsigned_, (FIX::ID<8002>),	UInt128),
  (float_, (FIX::ID<8003>),	Float),
  (fixed, (FIX::ID<8004>, NDP<3>),	Fixed));
ZfStructImpl(Numbers);
ZfStructImpl(Numbers, FIX);

static void numbers()
{
  ZuTestScope(numbers);
  constexpr auto Wire =
    "8001=-170141183460469231731687303715884105728\1"
    "8002=340282366920938463463374607431768211455\1"
    "8003=-1.25\1" "8004=-12.125\1"_z;
  Numbers n;
  ZuCheck(!load(n, Wire));
  ZuCheck(n.signed_ == ZuCmp<int128_t>::null() && n.unsigned_ == ~uint128_t(0));
  ZuCheck(n.float_ == -1.25 && n.fixed == ZuFixed{"-12.125"});
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, n) && out.data() == Wire);
  ZuCheck(!load(n, "8003=1e2\1"_z));
  ZuCheck(!load(n, "8004=nan\1"_z));
  Record r;
  auto nullTime = handler<Record, ZuFacet::Millis>("60=\1");
  ZuCheck(nullTime.load(r) && !nullTime.error() && !*r.time);
  constexpr auto NullWire = "60=\1"_z;
  char nullBytes[NullWire.length()];
  Sink nullOut{{nullBytes, sizeof(nullBytes)}};
  ZuCheck(save<ZuFacet::Millis>(nullOut, r) && nullOut.data() == NullWire);
  Sink invalid{{bytes, sizeof(bytes)}};
  ZuCheck(save(invalid, r));
}

struct Limited {
  int	value = 0;
};

ZfStruct(, (Limited, FIX),
  (value, (FIX::ID<8005>, (Range<-2, 2>)),	Int32));
ZfStructImpl(Limited);
ZfStructImpl(Limited, FIX);

static void filters()
{
  ZuTestScope(filters);
  Accessors a;
  a.key = 7; a.value = 8;
  constexpr auto Upd = "1=7\1" "2=8\1"_z;
  char bytes[Upd.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck((save<ZuFacet::FIX, ZfFieldFilter::Upd>(out, a) && out.data() == Upd));
  Sink del{{bytes, sizeof(bytes)}};
  ZuCheck((save<ZuFacet::FIX, ZfFieldFilter::Del>(del, a) && del.data() == "1=7\1"_z));
  Limited l;
  ZuCheck(!load(l, "8005=-2\1"_z) && l.value == -2);
  ZuCheck(!load(l, "8005=-3\1"_z) && l.value == -3);
  ZuCheck(!load(l, "8005=3\1"_z) && l.value == 3);
}

struct CBody : ZmNHeap<CBody> {
  const char	*text = nullptr;

  ~CBody() { ::free(const_cast<char *>(text)); }

friend inline ZuStringT<"Zfix.TestCBody"> ZmNHeapID(CBody *);
};

ZfStruct(, (CBody, FIX),
  (text, (FIX::ID<11>, Mutable, Deflt<"preset"_z>),	CString));
ZfStructImpl(CBody);
ZfStructImpl(CBody, FIX);

static void cstrings()
{
  ZuTestScope(cstrings);
  CBody b;
  char backing[] = "11=owned\1";
  ZuCheck(!load(b, ZuCSpan{backing, sizeof(backing) - 1}));
  ZuCheck(ZuCSpan{b.text} == "owned" && b.text != backing + 3);
  backing[3] = 'X';
  ZuCheck(ZuCSpan{b.text} == "owned");
  constexpr auto Wire = "11=owned\1"_z;
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, b) && out.data() == Wire);
  ZuCheck(!load(b, "11=\1"_z) && b.text && !*b.text);
  ZuCheck(!load(b, "11=A\0B\1"_z));
  ZuCheck(!update(b, {}) && ZuCSpan{b.text} == "preset");
  auto h = handler<CBody>("11=allocated\1");
  auto p = h.alloc();
  ZuCheck(h.valid() && p && ZuCSpan{p->text} == "allocated");
  delete p;
}

struct PtrBody {
  ZuPtr<Owned>	owned;
};

ZfStruct(, (PtrBody, FIX),
  (owned, ,			UDT));
ZfStructImpl(PtrBody);
ZfStructImpl(PtrBody, FIX);

static void pointers()
{
  ZuTestScope(pointers);
  PtrBody b;
  ZuCheck(!load(b, "21=42\1"_z));
  ZuCheck(b.owned && b.owned->value() == 42);
  auto ptr = b.owned.ptr();
  ZuCheck(!load(b, "21=43\1"_z));
  ZuCheck(b.owned.ptr() == ptr && b.owned->value() == 43);
  constexpr auto Wire = "21=43\1"_z;
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, b) && out.data() == Wire);
  ZuCheck(!load(b, {}) && !b.owned);
  auto h = handler<PtrBody>("21=bad\1");
  auto failed = h.ctor();
  ZuCheck(h.valid() && failed.owned);
}

// Named constant storage makes pointer defaults structural; decoding clones
// these values into independently owned destination strings.
constexpr char DefaultOne[] = "one";
constexpr char DefaultTwo[] = "two";
constexpr ZuArray<const char *, 2> CDefaults{DefaultOne, DefaultTwo};
using CStrings = ZtArray<const char *, ZtArrayHeapID<"Zfix.TestCStrings">>;
using Labels = ZtArray<Text, ZtArrayHeapID<"Zfix.TestLabels">>;
struct CLists : ZmNHeap<CLists> {
  CStrings	strings;
  Labels	labels;
  Labels	cLabels;
  CLists() = default;
  CLists(CLists &&) = default;
  CLists(const CLists &) = delete;
  CLists	&operator =(const CLists &) = delete;

  ~CLists() { freeCStrings(strings); }

friend inline ZuStringT<"Zfix.TestCLists"> ZmNHeapID(CLists *);
};

ZfStruct(, (CLists, FIX),
  (strings, (FIX::ID<9010>, FIX::Multiple, Mutable, Deflt<CDefaults>), CStringVec),
  (labels, (FIX::ID<9011>, FIX::Multiple, Mutable),	StringVec),
  (cLabels, (FIX::ID<9012>, FIX::Multiple, Mutable),	CStringVec));
ZfStructImpl(CLists);
ZfStructImpl(CLists, FIX);
struct CRead {
  CStrings	strings;
  int		number = 0;
};

ZfStruct(, (CRead, FIX),
  ((strings, Rd), (FIX::ID<9010>, FIX::Multiple),	CStringVec),
  (number, (FIX::ID<1>),				Int32));
ZfStructImpl(CRead);
ZfStructImpl(CRead, FIX);

static void stringLists()
{
  ZuTestScope(stringLists);
  constexpr auto Wire = "9010=alpha beta\1" "9011=gamma delta\1" "9012=epsilon zeta\1"_z;
  auto h = handler<CLists>(Wire);
  auto lists = h.ctor();
  ZuCheck(h.valid() && lists.strings.length() == 2);
  ZuCheck(ZuCSpan{lists.strings[0]} == "alpha" && ZuCSpan{lists.strings[1]} == "beta");
  ZuCheck(lists.labels.length() == 2 && lists.labels[1] == "delta");
  ZuCheck(lists.cLabels.length() == 2 && lists.cLabels[1] == "zeta");
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, lists) && out.data() == Wire);
  ZuCheck(!load(lists, "9010=next\1"_z));
  ZuCheck(lists.strings.length() == 1 && !lists.labels.length() && !lists.cLabels.length());
  ZuCheck(!load(lists, "9010=first \1"_z));
  ZuCheck(!load(lists, "9010=first bad\0value\1"_z));
  ZuCheck(!update(lists, {}) && lists.strings.length() == 2);
  ZuCheck(ZuCSpan{lists.strings[0]} == "one" && lists.strings[0] != DefaultOne);
  ZuCheck(!load(lists, "9010=\1"_z) && !lists.strings.length());
  constexpr auto BadWire = "9010=alpha beta\1" "9011=bad\0value\1"_z;
  auto bad = handler<CLists>(BadWire);
  auto accepted = bad.alloc();
  ZuCheck(accepted && !bad.error());
  delete accepted;
  CRead skipped;
  ZuCheck(!load(skipped, "9010=alpha beta\1" "1=7\1"_z));
  ZuCheck(!skipped.strings.length() && skipped.number == 7);
  ZuCheck(!load(skipped, "9010=first bad\0value\1"_z));
}

struct Codes : ZtArray<int, ZtArrayHeapID<"Zfix.TestCodes">> {
  ZuDerive_(Codes, (ZtArray<int, ZtArrayHeapID<"Zfix.TestCodes">>));
friend AsMultiple<ZfFieldTC::Int32> Zfix_Fmt(Codes *);
};

struct Words : ZtArray<Text, ZtArrayHeapID<"Zfix.TestWords">> {
  ZuDerive_(Words, (ZtArray<Text, ZtArrayHeapID<"Zfix.TestWords">>));
friend AsMultiple<ZfFieldTC::String> Zfix_Fmt(Words *);
};

struct Lists {
  Codes	codes;
  Words	words;
};

ZfStruct(, (Lists, FIX),
  (codes, (FIX::ID<9001>, Mutable, FIX::Multiple),	Int32Vec),
  (words, (FIX::ID<9002>, Mutable),	UDT));
ZfStructImpl(Lists);
ZfStructImpl(Lists, FIX);

static void multiple()
{
  ZuTestScope(multiple);
  constexpr auto Wire = "9001=-1 0 2147483647\1" "9002=alpha beta\1"_z;
  Lists lists;
  ZuCheck(!load(lists, Wire));
  ZuCheck(lists.codes.length() == 3 && lists.codes[0] == -1 &&
    lists.codes[2] == INT_MAX);
  ZuCheck(lists.words.length() == 2 && lists.words[1] == "beta");
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, lists) && out.flush() && out.data() == Wire);
  auto bad = [&lists](ZuCSpan Wire) {
    ZuCheck(!load(lists, Wire));
  };
  bad("9001= 1\1"_z); bad("9001=1 \1"_z); bad("9001=1  2\1"_z);
  bad("9001=2147483648\1"_z); bad("9001=1 X\1"_z);
  ZuCheck(!update(lists, "9001=\1"_z));
  ZuCheck(!lists.codes.length() && !lists.words.length());
  lists.words.push("bad word");
  Sink invalid{{bytes, sizeof(bytes)}};
  ZuCheck(save(invalid, lists));
}

static void inspect()
{
  ZuTestScope(inspect);
  constexpr auto Wire = "1000000=42\1" "11=\1"_z;
  auto h = handler<Record>(Wire);
  auto number = h.raw<ZfField(Record, number)>();
  ZuCheck(h.valid() && number == "42" && number.data() == Wire.data() + 8);
  auto empty = h.raw<ZfField(Record, text)>();
  ZuCheck(h.valid() && !empty.length() && empty.data());
  auto absent = h.raw<ZfField(Record, small)>();
  ZuCheck(h.valid() && !absent.data());
  auto bad = handler<Record>("1000000=not-an-int\1");
  ZuCheck(bad.raw<ZfField(Record, number)>() == "not-an-int");
  ZuCheck(!bad.error());
}

static void defaults()
{
  ZuTestScope(defaults);
  Record r;
  auto e = load(r, "1000000=42\1"_z);
  ZuCheck(!e);
  ZuCheck(r.number == 42);
  ZuCheck(!r.text);
  ZuCheck(r.small == ZfField(Record, small)::deflt());
  r.text = "stale";
  r.small = 30;
  e = update(r, "11=\1"_z);
  ZuCheck(!e);
  ZuCheck(r.number == 7);
  ZuCheck(!r.text);
  ZuCheck(r.small == 30);
}

static void conversions()
{
  ZuTestScope(conversions);
  Record r;
  auto e = load(r,
    "44=-23.50\1" "13=Y\1" "12=-128\1"
    "60=20261026-12:34:56.123\1" "777=not-a-number\1"_z);
  ZuCheck(!e);
  ZuCheck(r.flag);
  ZuCheck(r.small == -128);
  ZuCheck(r.price == ZuDecimal{"-23.50"});
  ZuCheck((r.time == ZuDateTime{ZuDateTimeScan::FIX{}, "20261026-12:34:56.123"}));
  e = load(r, "12=128\1"_z);
  ZuCheck(!e && r.small == int8_t(128));
  e = load(r, "1000000=2147483648\1"_z);
  ZuCheck(!e && r.number == INT32_MIN);
  e = load(r, "1000000=1x\1"_z);
  ZuCheck(!e);
  e = load(r, "13=1\1"_z);
  ZuCheck(!e);
  e = load(r, "44=1e3\1"_z);
  ZuCheck(!e);
  e = load(r, "44=23.\1"_z);
  ZuCheck(!e && r.price == ZuDecimal{"23."});
  e = load(r, "60=202:1026-12:34:56\1"_z);
  ZuCheck(!e);
  e = load(r, "60=20260231-12:34:56\1"_z);
  ZuCheck(!e);
  e = load(r, "1000000=1\1" "1000000=2\1"_z);
  ZuCheck(e.code == ErrorCode::Duplicate);
  e = load(r, "11=missing-delimiter"_z);
  ZuCheck(e.code == ErrorCode::Truncated);
  e = load(r, "011=x\1"_z);
  ZuCheck(!e);
  e = load(r, "11=bad\0value\1"_z);
  ZuCheck(!e && r.text.length() == 9);
  e = load(r, "11=bad\nvalue\1"_z);
  ZuCheck(!e);
  e = load(r, "11=bad\x85value\1"_z);
  ZuCheck(!e);
  e = load(r, "35=D\1"_z);
  ZuCheck(e.code == ErrorCode::Reserved);
}

static void construction()
{
  ZuTestScope(construction);
  Limits cfg;
  cfg.frameBytes = 0;
  Handler<Args> oversized{"22=order\1", cfg};
  auto failedSize = oversized.ctor();
  ZuCheck(!oversized.valid() && oversized.error().code == ErrorCode::Size);
  cfg = {};
  cfg.parseBytes = 0;
  Handler<Args> noRecords{"22=order\1", cfg};
  auto failedRecords = noRecords.ctor();
  ZuCheck(!noRecords.valid() && noRecords.error().code == ErrorCode::Size);
  Handler<Args> h{"23=31\1" "22=order\1" "21=4\1"};
  auto a = h.ctor();
  ZuCheck(h.valid());
  ZuCheck(a.name == "order" && a.seq == 4 && a.value == 31);
  Handler<Args> missing{"22=order\1"};
  auto b = missing.ctor();
  ZuCheck(!missing.valid());
  ZuCheck(missing.error().code == ErrorCode::Required);
  Handler<Args> absent{"22=other\1" "21=8\1"};
  auto c = absent.ctor();
  ZuCheck(absent.valid());
  ZuCheck(c.value == 9);
  alignas(Args) unsigned char storage[sizeof(Args)];
  ZuCheck(absent.new_(storage));
  auto p = ZuLaunder(reinterpret_cast<Args *>(storage));
  ZuCheck(p->name == "other" && p->seq == 8 && p->value == 9);
  p->~Args();
}

static void components()
{
  ZuTestScope(components);
  ComponentData c;
  auto e = load(c, "24=5\1" "11=nested\1" "1000000=6\1"_z);
  ZuCheck(!e);
  ZuCheck(c.record.number == 6 && c.record.text == "nested" && c.extra == 5);
  c.record.small = 17;
  c.extra = 9;
  e = update(c, "11=updated\1"_z);
  ZuCheck(!e);
  ZuCheck(c.record.number == 7 && c.record.text == "updated");
  ZuCheck(c.record.small == 17 && c.extra == 9);
}

static void bounds()
{
  ZuTestScope(bounds);
  Record r;
  Limits limits;
  limits.frameBytes = 2;
  ZuCheck(load(r, "11=x\1"_z, limits).code == ErrorCode::Size);
  limits.frameBytes = Limits::FrameBytes;
  limits.fieldBytes = 2;
  ZuCheck(load(r, "11=abc\1"_z, limits).code == ErrorCode::Size);
  Token token;
  ZuCheck(!scan("2147483647=x\1"_z, 0, token) && token.tag == INT32_MAX);
  ZuCheck(!scan("2147483648=x\1"_z, 0, token) && token.tag == INT32_MIN);
  ZuCheck(scan("2147483647"_z, 0, token).code == ErrorCode::Truncated);
}

static void output()
{
  ZuTestScope(output);
  Args a{"order", 4};
  a.value = 9;
  constexpr auto Wire = "21=4\1" "22=order\1" "23=9\1"_z;
  char bytes[Wire.length()]; // exact golden-fixture size, exercises tail fallback
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, a));
  ZuCheck(out.flush());
  ZuCheck(out.data() == Wire);
  char small[sizeof("21=4") - 1];
  Sink bad{{small, sizeof(small)}};
  ZuCheck(!save(bad, a));
  ZuCheck(bad.failed());
  auto n = bad.length();
  bad << "more";
  ZuCheck(bad.length() == n && !bad.flush());
  a.name = "bad\1value";
  constexpr auto RawWire = "21=4\1" "22=bad\1value\1" "23=9\1"_z;
  char raw[RawWire.length()];
  Sink invalid{{raw, sizeof(raw)}};
  ZuCheck(save(invalid, a) && invalid.data() == RawWire);
}

static void policies()
{
  ZuTestScope(policies);
  Record r;
  r.time = ZuDateTime{ZuDateTimeScan::FIX{}, "20261026-12:34:56.123456789"};
  constexpr auto Wire = "60=20261026-12:34:56.123\1"_z;
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save<ZuFacet::Millis>(out, r));
  if (out.data() != Wire) log("timestamp wire: ", out.data());
  ZuCheck(out.data() == Wire);
  constexpr auto SecondWire = "60=20261026-12:34:56\1"_z;
  char seconds[SecondWire.length()];
  Sink secondOut{{seconds, sizeof(seconds)}};
  ZuCheck(save<ZuFacet::Seconds>(secondOut, r));
  ZuCheck(secondOut.data() == SecondWire);
  Character c;
  ZuCheck(!load(c, "54=2\1"_z) && c.value == '2');
  constexpr auto CharWire = "54=2\1"_z;
  char chars[CharWire.length()];
  Sink charOut{{chars, sizeof(chars)}};
  ZuCheck(save(charOut, c) && charOut.data() == CharWire);
  ZuCheck(!load(c, "54=22\1"_z));
  Accessors a;
  ZuCheck(!update(a, "2=8\1"_z));
  ZuCheck(a.key == 1 && a.value == 8 && a.immutable == 55);
  ZuCheck(!update(a, {}));
  ZuCheck(a.key == 1 && a.value == 2 && a.immutable == 55);
  ZuCheck(load(a, {}).code == ErrorCode::Required);
  ZuCheck(!load(a, "1=7\1" "2=9\1" "3=4\1" "4=999\1"_z));
  ZuCheck(a.key == 7 && a.value == 9 && a.immutable == 4);
  ZuCheck(!update(a, "3=bad\1"_z));
  ZuCheck(!update(a, "4=bad\1"_z));
  ZuCheck(a.immutable == 4);
}

static void allocation()
{
  ZuTestScope(allocation);
  Handler<Owned> h{"21=7\1"};
  auto p = h.alloc();
  ZuCheck(p && p->value() == 7);
  delete p;
  alignas(Owned) unsigned char bytes[sizeof(Owned)];
  ZuCheck(h.new_(bytes));
  auto q = ZuLaunder(reinterpret_cast<Owned *>(bytes));
  ZuCheck(q->value() == 7);
  q->~Owned();
  Handler<Owned> bad{"21=bad\1"};
  auto accepted = bad.alloc();
  ZuCheck(accepted && !bad.error());
  delete accepted;
}

struct SignedTags {
  int min = 0, neg = 0, max = 0;
};
ZfStruct(, (SignedTags, FIX),
  (min, (FIX::ID<INT32_MIN>), Int32),
  (neg, (FIX::ID<-11>), Int32),
  (max, (FIX::ID<INT32_MAX>), Int32));

static void signedTags()
{
  ZuTestScope(signedTags);
  constexpr auto Wire = "-2147483648=1\1" "-11=2\1" "2147483647=3\1"_z;
  SignedTags value;
  auto e = load(value, Wire);
  ZuCheck(!e && value.min == 1 && value.neg == 2 && value.max == 3);
  Indexed<SignedTags, ZuFacet::FIX> indexed{Wire};
  auto copy = indexed.ctor();
  ZuCheck(indexed.valid() && copy.min == 1 && copy.neg == 2 && copy.max == 3);
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, value) && out.data() == Wire);
  e = load(value, "-12=skip\1" "-11=7\1"_z);
  ZuCheck(!e && value.neg == 7);
  e = load(value, "-11=1\1" "-11=2\1"_z);
  ZuCheck(e.code == ErrorCode::Duplicate && e.tag == -11);
  e = load(value, "-11=bad\1"_z);
  ZuCheck(!e);
  Token token;
  ZuCheck(!scan("-2147483648=x\1"_z, 0, token) && token.tag == INT32_MIN);
  ZuCheck(!scan("-2147483649=x\1"_z, 0, token));
  ZuCheck(!scan("-999999999999999999999=x\1"_z, 0, token));
  ZuCheck(scan("-=x\1"_z, 0, token).code == ErrorCode::Tag);
  ZuCheck(!scan("-0=x\1"_z, 0, token));
  ZuCheck(scan("-"_z, 0, token).code == ErrorCode::Truncated);
  ZuCheck(scan("-2147483648"_z, 0, token).code == ErrorCode::Truncated);
}

static void tagDispatch()
{
  ZuTestScope(tagDispatch);
  using S = Schema<Record, ZuFacet::FIX>;
  ZuCheck(S::find(1000000) == 0);
  ZuCheck(S::find(11) == 2);
  ZuCheck(S::find(1000001) == -1);
  ZuCheck(S::find(INT32_MAX) == -1);
  ZuCheck((Schema<Empty, ZuFacet::FIX>::find(777) == -1));
  Record value;
  auto e = load(value, "999999=skip\1" "1000000=23\1" "999999=again\1"
    "11=kept\1" "1000001=skip\1" "2147483647=skip\1"_z);
  ZuCheck(!e && value.number == 23 && value.text == "kept");
}

static void customAndBorrowed()
{
  ZuTestScope(customAndBorrowed);
  ScalarUDT o;
  ZuCheck(!load(o, "55=valid\1"_z));
  ZuCheck(o.label.value == "valid");
  constexpr auto Wire = "55=valid\1"_z;
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, o) && out.data() == Wire);
  ZuCheck(load(o, "55=bad\1"_z).code == ErrorCode::Value);
  ZuCheck(!load(o, {}) && !o.label.value);
  char backing[] = "11=borrowed\1";
  Borrowed b;
  ZuCheck(!load(b, ZuCSpan{backing, sizeof(backing) - 1}));
  ZuCheck(b.value.data() == backing + 3);
  ZuCheck(b.value == "borrowed");
  Empty empty;
  ZuCheck(!load(empty, "777=untyped\1"_z));
  Sink emptyOut{{}};
  ZuCheck(save(emptyOut, empty) && !emptyOut.length());
}

using FormatBytes = ZtArray<uint8_t, ZtArrayHeapID<"Zfix.TestFormat">>;
struct ByteFormats {
  FormatBytes	b64, url, b32, hex;
};

ZfStruct(, (ByteFormats, FIX),
  (b64, (FIX::ID<6101>, FIX::Base64),		Bytes),
  (url, (FIX::ID<6111>, FIX::Base64URL),	Bytes),
  (b32, (FIX::ID<6121>, FIX::Base32),				Bytes),
  (hex, (FIX::ID<6131>, FIX::Hex),				Bytes));
ZfStructImpl(ByteFormats, FIX);

static void byteFormats()
{
  ZuTestScope(byteFormats);
  constexpr auto Data = "\xfb\xff"_z;
  constexpr auto Wire = "6101=+/8=\1" "6111=-_8\1"
    "6121=7P7Q====\1" "6131=FBFF\1"_z;
  ByteFormats value;
  value.b64 = Data; value.url = Data; value.b32 = Data; value.hex = Data;
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, value) && out.data() == Wire);
  ByteFormats copy;
  auto input = handler<ByteFormats>(Wire);
  ZuCheck(input.check() && input.load(copy));
  ZuCheck(ZuCSpan{copy.b64} == Data && ZuCSpan{copy.url} == Data &&
    ZuCSpan{copy.b32} == Data && ZuCSpan{copy.hex} == Data);
  ZuCheck(!load(copy, "6101=TQ?=\1"_z));
  ZuCheck(!load(copy, "6101=TQ=\1"_z));
  ZuCheck(!load(copy, "6121=ABC\1"_z));
  ZuCheck(!load(copy, "6131=F\1"_z));
  ZuCheck(!load(copy, "6131=FG\1"_z));
}

struct NumberFormats {
  unsigned	hex = 0xabc;
  uint64_t	comma = 1234567;
  int		padded = -12;
  ZuDecimal	decimal{123456789, 3};
  double	real = 1234.5;
  int		absent = ZuCmp<int>::null(), retained = ZuCmp<int>::null();
  Text		empty;
  bool		flag = false;
};

using HexNumber = ZtFmt::Alt<ZtFmt::Hex<true>>;
using PaddedNumber = ZtFmt::Right<8, ' '>;

ZfStruct(, (NumberFormats, FIX),
  (hex, (FIX::ID<6201>, FIX::Number<HexNumber>),		UInt32),
  (comma, (FIX::ID<6202>, FIX::Number<ZtFmt::Comma<>>),		UInt64),
  (padded, (FIX::ID<6203>, FIX::Number<PaddedNumber>),		Int32),
  (decimal, (FIX::ID<6204>, FIX::Number<ZtFmt::Comma<>>),	Decimal),
  (real, (FIX::ID<6205>, FIX::Number<ZtFmt::Comma<>>),		Float),
  (absent, (FIX::ID<6206>, FIX::Opt),				Int32),
  (retained, (FIX::ID<6207>, FIX::Optional<false>),		Int32),
  (empty, (FIX::ID<6208>, FIX::Opt),				String),
  (flag, (FIX::ID<6209>, FIX::Opt),				Bool));
ZfStructImpl(NumberFormats, FIX);

static void numberFormats()
{
  ZuTestScope(numberFormats);
  constexpr auto Wire = "6201=0xABC\1" "6202=1,234,567\1" "6203=     -12\1"
    "6204=123,456.789\1" "6205=1,234.5\1" "6207=-2147483648\1" "6209=N\1"_z;
  NumberFormats value;
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, value) && out.data() == Wire);
  NumberFormats copy;
  copy.hex = 0; copy.comma = 0; copy.padded = 0; copy.decimal = ZuDecimal{0}; copy.real = 0;
  ZuCheck(!load(copy, Wire));
  ZuCheck(copy.hex == value.hex && copy.comma == value.comma && copy.padded == value.padded &&
    copy.decimal == value.decimal && copy.real == value.real && !copy.flag);
  ZuCheck(!load(copy, "6201=0x100000000\1"_z));
  ZuCheck(!load(copy, "6202=18,446,744,073,709,551,616\1"_z));
}

struct TimeFormats {
  ZuDateTime	deflt, fix, iso, csv;
  ZuTime	sec, msec, usec, nsec;
};

ZfStruct(, (TimeFormats, FIX),
  (deflt, (FIX::ID<6301>),					DateTime),
  (fix, (FIX::ID<6302>, (FIX::FIX<0, 3>)),				DateTime),
  (iso, (FIX::ID<6303>, (FIX::ISO<0, 3>)),				DateTime),
  (csv, (FIX::ID<6304>, (FIX::CSV<0, 3>)),				DateTime),
  (sec, (FIX::ID<6305>, (FIX::Unix<0, 3>)),				Time),
  (msec, (FIX::ID<6306>, (FIX::Unix<1, 3>)),				Time),
  (usec, (FIX::ID<6307>, (FIX::Unix<2, 3>)),				Time),
  (nsec, (FIX::ID<6308>, (FIX::Unix<3, 0>)),				Time));
ZfStructImpl(TimeFormats, FIX);

static void timeFormats()
{
  ZuTestScope(timeFormats);
  ZuTime stamp{1234, 567000000};
  ZuDateTime date{stamp};
  TimeFormats value{date, date, date, date, stamp, stamp, stamp, stamp};
  Text expected;
  auto iso = ZuDateTimeFmt::ISO{}; auto csv = ZuDateTimeFmt::CSV{};
  expected << "6301=19700101-00:20:34.567\1" "6302=19700101-00:20:34.567\1"
    "6303=" << date.fmt(iso) << '\1' << "6304=" << date.fmt(csv) << '\1' <<
    "6305=1234.567\1" "6306=1234567.000\1" "6307=1234567000.000\1" "6308=1234567000000\1";
  auto bytes = ZtScratch(PrintBuf, expected.length());
  Sink out{{bytes.data(), unsigned(expected.length())}};
  ZuCheck(save(out, value) && out.data() == expected);
  TimeFormats copy;
  ZuCheck(!load(copy, expected));
  ZuCheck(copy.deflt == date && copy.fix == date && copy.iso == date && copy.csv == date &&
    copy.sec == stamp && copy.msec == stamp && copy.usec == stamp && copy.nsec == stamp);
  ZuCheck(!load(copy, "6301=1970-01-01T00:20:34.567Z\1"_z));
  ZuCheck(!load(copy, "6303=not-a-time\1"_z));
  ZuCheck(!load(copy, "6305=1e3\1"_z));
}

// Each pair occupies one position; bytes after its count are opaque.
struct BinaryRecord {
  ZuCSpan xml;
  ZuBSpan raw;
  ZuBSpan signature;
  int after = 0;
};
ZfStruct(, (BinaryRecord, FIX),
  (xml, (FIX::ID<212>, FIX::Binary<213>, Required), String),
  (raw, (FIX::ID<95>, FIX::Binary<96>, Required), Bytes),
  (signature, ((FIX::Sig<93, 89>)), Bytes),
  (after, (FIX::ID<5000>), Int32));

struct BinaryOwned {
  Text xml;
  ZtArray<uint8_t, ZtArrayHeapID<"Zfix.Binary">> raw;
};
ZfStruct(, (BinaryOwned, FIX),
  (xml, (FIX::ID<212>, FIX::Binary<213>), String),
  (raw, (FIX::ID<95>, FIX::Binary<96>), Bytes));

struct BinaryArgs {
  ZuBSpan raw;
  explicit BinaryArgs(ZuBSpan v) : raw{v} { }
};
ZfStruct(, (BinaryArgs, FIX),
  (raw, (FIX::ID<95>, FIX::Binary<96>, Ctor<0>, Required), Bytes));

struct BinaryRow {
  ZuBSpan raw;
  int after = 0;
};
ZfStruct(, (BinaryRow, FIX),
  (raw, (FIX::ID<95>, FIX::Binary<96>, Required), Bytes),
  (after, (FIX::ID<5000>), Int32));
struct BinaryRows : ZtArray<BinaryRow, ZtArrayHeapID<"Zfix.BinaryRows">> {
  ZuDerive_(BinaryRows, (ZtArray<BinaryRow, ZtArrayHeapID<"Zfix.BinaryRows">>));
friend AsArray<> Zfix_Fmt(BinaryRows *);
};
struct BinaryGroup {
  BinaryRows rows;
};
ZfStruct(, (BinaryGroup, FIX), (rows, (FIX::ID<6000>), UDT));

static void binaryScopes()
{
  ZuTestScope(binaryScopes);
  constexpr auto wire = "6000=2\1" "95=5\1" "96=A\1\0=B\1" "5000=1\1"
    "95=4\1" "96=Z\1=Y\1" "5000=2\1"_z;
  BinaryGroup live;
  ZuCheck(!load(live, wire) && live.rows.length() == 2);
  ZuCheck(live.rows[0].raw.data() == reinterpret_cast<const uint8_t *>(wire.data() + 15));
  ZuCheck(live.rows[0].raw.length() == 5 && live.rows[0].after == 1);
  ZuCheck(live.rows[1].raw.data() == reinterpret_cast<const uint8_t *>(wire.data() + 36));
  ZuCheck(live.rows[1].raw.length() == 4 && live.rows[1].after == 2);
  BinaryGroup copy;
  Indexed<BinaryGroup, ZuFacet::FIX> indexed{wire};
  ZuCheck(indexed.load(copy) && copy.rows.length() == 2);
  ZuCheck(copy.rows[0].raw.data() == live.rows[0].raw.data());
  ZuCheck(copy.rows[1].raw.data() == live.rows[1].raw.data());
  constexpr auto ctorWire = "95=5\1" "96=A\1\0=B\1"_z;
  Handler<BinaryArgs> args{ctorWire};
  auto value = args.ctor();
  ZuCheck(args.valid() && value.raw.length() == 5);
  ZuCheck(value.raw.data() == reinterpret_cast<const uint8_t *>(ctorWire.data() + 8));
  FieldPos pos;
  auto error = inspect<BinaryGroup>(wire, 5000, pos);
  ZuCheck(error.code == ErrorCode::Duplicate); // both actual entry fields
  error = inspect<BinaryRecord>("212=11\1" "213=x\1" "5000=99\1" "y\1" "5000=7\1"_z,
    5000, pos);
  ZuCheck(!error && pos.length == 1);
  constexpr auto selectWire = "95=3\1" "96=A\1B\1"_z;
  error = inspect<BinaryArgs>(selectWire, 96, pos);
  ZuCheck(!error && pos.offset == 8 && pos.length == 3);
  error = inspect<BinaryArgs>(selectWire, 95, pos);
  ZuCheck(!error && pos.offset == 3 && pos.length == 1);
}

static void binaryFields()
{
  ZuTestScope(binaryFields);
  using S = Schema<BinaryRecord, ZuFacet::FIX>;
  ZuAssert(S::Leaves::N == 4);
  ZuAssert(tag<ZfField(BinaryRecord, signature)>() == 93);
  ZuAssert(payloadTag<ZfField(BinaryRecord, signature)>() == 89);
  ZuAssert((Signature<ZfField(BinaryRecord, signature)>{}));
  ZuCheck(S::find(93) >= 0 && S::find(89) < 0);
  constexpr auto wire = "212=5\1" "213=A\1\0=B\1" "95=4\1" "96=\xff\1=X\1"
    "93=3\1" "89=\0\1\xff\1" "5000=7\1"_z;
  BinaryRecord live;
  ZuCheck(!load(live, wire));
  ZuCheck((live.xml == ZuCSpan{wire.data() + 10, 5}));
  ZuCheck(live.xml.data() == wire.data() + 10 && live.xml.length() == 5);
  ZuCheck(live.raw.data() == reinterpret_cast<const uint8_t *>(wire.data() + 24));
  ZuCheck(live.raw.length() == 4 && live.after == 7);
  ZuCheck(live.signature.data() == reinterpret_cast<const uint8_t *>(wire.data() + 37));
  ZuCheck(live.signature.length() == 3);
  Indexed<BinaryRecord, ZuFacet::FIX> indexed{wire};
  BinaryRecord copy;
  ZuCheck(indexed.valid() && indexed.load(copy));
  ZuCheck(copy.xml.data() == live.xml.data() && copy.raw.data() == live.raw.data());
  ZuCheck(copy.signature.data() == live.signature.data() && copy.after == 7);
  auto raw = indexed.raw<ZfField(BinaryRecord, xml)>();
  ZuCheck(raw.data() == live.xml.data() && raw.length() == 5);
  BinaryOwned owned;
  ZuCheck((!load(owned, ZuCSpan{wire.data(), 29})));
  ZuCheck(owned.xml == live.xml && ZuBSpan{owned.raw} == live.raw);
  ZuCheck(owned.xml.data() != live.xml.data() && owned.raw.data() != live.raw.data());
  auto storage = ZtScratch(PrintBuf, wire.length());
  Sink out{{storage.data(), unsigned(wire.length())}};
  constexpr auto unsignedWire = "212=5\1" "213=A\1\0=B\1" "95=4\1" "96=\xff\1=X\1"
    "5000=7\1"_z;
  ZuCheck(save(out, live) && out.data() == unsignedWire);
  ZuCheck(live.signature.data() == reinterpret_cast<const uint8_t *>(wire.data() + 37));
  auto fullStorage = ZtScratch(PrintBuf, wire.length());
  Sink full{{fullStorage.data(), unsigned(wire.length())}};
  ZuCheck((save<ZuFacet::FIX, ZfFieldFilter::Save>(full, live) && full.data() == wire));
  constexpr auto empty = "212=0\1" "213=\1" "95=0\1" "96=\1"_z;
  ZuCheck(!load(live, empty) && live.xml.length() == 0 && live.raw.length() == 0);
  Indexed<BinaryRecord, ZuFacet::FIX> emptyIndex{empty};
  ZuCheck(emptyIndex.load(copy) && copy.xml.length() == 0 && copy.raw.length() == 0);

  auto bad = [](ZuCSpan wire, unsigned code, int32_t tag, unsigned offset) {
    BinaryRecord record;
    auto error = load(record, wire);
    Indexed<BinaryRecord, ZuFacet::FIX> indexed{wire};
    return error.code == code && error.tag == tag && error.offset == offset &&
      indexed.error().code == error.code && indexed.error().tag == error.tag &&
      indexed.error().offset == error.offset;
  };
  ZuCheck(bad("212=\1" "213=\1"_z, ErrorCode::Value, 212, 4));
  ZuCheck(bad("212=-1\1" "213=x\1"_z, ErrorCode::Value, 212, 4));
  ZuCheck(bad("212=+1\1" "213=x\1"_z, ErrorCode::Value, 212, 4));
  ZuCheck(bad("212=1x\1" "213=x\1"_z, ErrorCode::Value, 212, 4));
  ZuCheck(bad("212=4294967296\1" "213=x\1"_z, ErrorCode::Size, 212, 4));
  ZuCheck(bad("212=1\1" "214=x\1"_z, ErrorCode::Length, 213, 6));
  ZuCheck(bad("212=1\1" "213=xx\1"_z, ErrorCode::Length, 213, 11));
  ZuCheck(bad("212=9\1" "213=x\1"_z, ErrorCode::Length, 213, 10));
  ZuCheck(bad("212=1\1" "213=x"_z, ErrorCode::Length, 213, 10));
}

struct BinaryValue {
  ZuBSpan data;
  int after = 0;
};
ZfStruct(, (BinaryValue, FIX),
  (data, (FIX::ID<95>, FIX::Binary<96>, Required), Bytes),
  (after, (FIX::ID<5000>), Int32));

// A rejected pair has the same primary/payload diagnostic in every consumer.
static bool binaryError(ZuCSpan wire, unsigned code, int32_t tag, unsigned offset,
    Limits limits = {})
{
  BinaryValue value;
  auto error = load(value, wire, limits);
  Indexed<BinaryValue, ZuFacet::FIX> indexed{wire, limits};
  BinaryValue copy;
  indexed.load(copy);
  FieldPos pos;
  auto inspected = inspect<BinaryValue>(wire, 0, pos, limits);
  return error.code == code && error.tag == tag && error.offset == offset &&
    indexed.error().code == code && indexed.error().tag == tag &&
    indexed.error().offset == offset && inspected.code == code &&
    inspected.tag == tag && inspected.offset == offset;
}
static void binaryMalformed()
{
  ZuTestScope(binaryMalformed);
  ZuCheck(binaryError("95= 1\1" "96=A\1"_z, ErrorCode::Value, 95, 3));
  ZuCheck(binaryError("95=1 \1" "96=A\1"_z, ErrorCode::Value, 95, 3));
  ZuCheck(binaryError("95=\t1\1" "96=A\1"_z, ErrorCode::Value, 95, 3));
  ZuCheck(binaryError("95=1"_z, ErrorCode::Truncated, 95, 4));
  ZuCheck(binaryError("95=1\1"_z, ErrorCode::Truncated, 96, 5));
  ZuCheck(binaryError("95=1\1" "96"_z, ErrorCode::Truncated, 96, 5));
  ZuCheck(binaryError("95=1\1" "X=A\1"_z, ErrorCode::Truncated, 96, 5));
  ZuCheck(binaryError("95=1\1" "96?A\1"_z, ErrorCode::Truncated, 96, 5));
  ZuCheck(binaryError("X=A\1"_z, ErrorCode::Tag, 0, 0));
  ZuCheck(binaryError("95=1\1" "96="_z, ErrorCode::Length, 96, 8));
  ZuCheck(binaryError("95=0\1" "96=x\1"_z, ErrorCode::Length, 96, 8));
  ZuCheck(binaryError("95=1\1" "96=AB\1"_z, ErrorCode::Length, 96, 9));
  ZuCheck(binaryError("95=2\1" "96=A\1" "5000=8\1"_z, ErrorCode::Length, 96, 10));
  ZuCheck(binaryError("95=2\1" "5000=8\1" "96=AB\1"_z, ErrorCode::Length, 96, 5));
  ZuCheck(binaryError("96=AB\1" "95=2\1"_z, ErrorCode::Truncated, 96, 11));
  BinaryValue value, copy;
  constexpr auto Orphan = "96=ordinary-orphan\1" "5000=7\1"_z;
  auto error = load(value, Orphan);
  Indexed<BinaryValue, ZuFacet::FIX> orphan{Orphan}; orphan.load(copy);
  ZuCheck(error.code == ErrorCode::Required && error.tag == 95 && !error.offset);
  ZuCheck(orphan.error().code == error.code && orphan.error().tag == 95 &&
    !orphan.error().offset);
  constexpr auto Duplicate = "95=1\1" "96=A\1" "95=1\1" "96=B\1"_z;
  error = load(value, Duplicate);
  Indexed<BinaryValue, ZuFacet::FIX> duplicate{Duplicate}; duplicate.load(copy);
  ZuCheck(error.code == ErrorCode::Duplicate && error.tag == 95 && error.offset == 10);
  ZuCheck(duplicate.error().code == error.code && duplicate.error().tag == 95 &&
    duplicate.error().offset == 10);
  constexpr auto Zeros = "95=0002\1" "96=A\1\1" "5000=9\1"_z;
  ZuCheck(!load(value, Zeros) && value.after == 9 && value.data.length() == 2);
  ZuCheck(value.data.data() == reinterpret_cast<const uint8_t *>(Zeros.data() + 11));
  Indexed<BinaryValue, ZuFacet::FIX> zeros{Zeros};
  ZuCheck(zeros.load(copy) && copy.data.data() == value.data.data());
}

struct BinaryAccess {
  ZuBSpan data, ignored;
  int after = 0;
};
ZfStruct(, (BinaryAccess, FIX),
  (data, (FIX::ID<95>, FIX::Binary<96>, Mutable), Bytes),
  ((ignored, Rd), (FIX::ID<212>, FIX::Binary<213>), Bytes),
  (after, (FIX::ID<5000>, Mutable), Int32));
struct BinaryOptional { ZuBSpan data; };
ZfStruct(, (BinaryOptional, FIX),
  (data, (FIX::ID<95>, FIX::Binary<96>, FIX::Opt), Bytes));
struct BinaryMutable { ZuSpan<uint8_t> data; };
ZfStruct(, (BinaryMutable, FIX),
  (data, (FIX::ID<95>, FIX::Binary<96>), Bytes));
struct BinaryTextArgs {
  ZuCSpan data;
  explicit BinaryTextArgs(ZuCSpan input) : data{input} { }
};
ZfStruct(, (BinaryTextArgs, FIX),
  (data, (FIX::ID<95>, FIX::Binary<96>, Ctor<0>, Required), String));
static void binarySelection()
{
  ZuTestScope(binarySelection);
  constexpr auto Wire = "95=4\1" "96=A\1\0=\1" "212=3\1" "213=X\1Y\1" "5000=7\1"_z;
  BinaryAccess access{ZuBSpan{"old"}, ZuBSpan{"keep"}, 1};
  ZuCheck(!load(access, Wire) && access.after == 7 && access.data.length() == 4);
  ZuCheck(access.ignored == ZuBSpan{"keep"});
  ZuCheck(access.data.data() == reinterpret_cast<const uint8_t *>(Wire.data() + 8));
  BinaryAccess indexedValue{ZuBSpan{"old"}, ZuBSpan{"keep"}, 1};
  Indexed<BinaryAccess, ZuFacet::FIX> indexed{Wire};
  ZuCheck(indexed.load(indexedValue) && indexedValue.ignored == ZuBSpan{"keep"});
  ZuCheck(indexedValue.data.data() == access.data.data());
  constexpr auto Update = "95=3\1" "96=B\0C\1" "5000=8\1"_z;
  ZuCheck(!update(access, Update) && access.after == 8 && access.ignored == ZuBSpan{"keep"});
  ZuCheck(access.data.data() == reinterpret_cast<const uint8_t *>(Update.data() + 8));
  constexpr auto Bad = "212=1\1" "213=XX\1"_z;
  auto error = load(access, Bad);
  Indexed<BinaryAccess, ZuFacet::FIX> bad{Bad}; bad.load(indexedValue);
  ZuCheck(error.code == ErrorCode::Length && error.tag == 213 && error.offset == 11);
  ZuCheck(bad.error().code == error.code && bad.error().tag == error.tag &&
    bad.error().offset == error.offset);
  constexpr auto Filtered = "95=1\1" "96=XX\1"_z;
  Live<BinaryValue, ZuFacet::FIX> filtered{Filtered};
  ZuCheck(!filtered.template check<ReadNone>() && filtered.error().code == ErrorCode::Length);
  Indexed<BinaryValue, ZuFacet::FIX> filteredIndex{Filtered};
  ZuCheck(!filteredIndex.template check<ReadNone>() &&
    filteredIndex.error().code == filtered.error().code &&
    filteredIndex.error().tag == filtered.error().tag &&
    filteredIndex.error().offset == filtered.error().offset);
  auto storage = ZtScratch(PrintBuf, Wire.length());
  Sink omitted{{storage.data(), unsigned(Wire.length())}};
  ZuCheck(save(omitted, BinaryOptional{}) && !omitted.length());
  Sink selected{{storage.data(), unsigned(Wire.length())}};
  constexpr auto OptionalData = "A\1B"_z;
  BinaryOptional optional{ZuBSpan{OptionalData}};
  ZuCheck(save(selected, optional) && selected.data() == "95=3\1" "96=A\1B\1"_z);
  Sink none{{storage.data(), unsigned(Wire.length())}};
  ZuCheck((save<ZuFacet::FIX, ReadNone>(none, optional) && !none.length()));
  constexpr auto SpanWire = "95=3\1" "96=A\0B\1"_z;
  BinaryMutable mutableSpan;
  ZuCheck(!load(mutableSpan, SpanWire));
  ZuCheck(mutableSpan.data.data() == reinterpret_cast<const uint8_t *>(SpanWire.data() + 8));
  Indexed<BinaryMutable, ZuFacet::FIX> spanIndex{SpanWire};
  BinaryMutable spanCopy;
  ZuCheck(spanIndex.load(spanCopy) && spanCopy.data.data() == mutableSpan.data.data());
  Handler<BinaryTextArgs> ctor{SpanWire}; auto args = ctor.ctor();
  ZuCheck(ctor.valid() && args.data.data() == SpanWire.data() + 8 && args.data.length() == 3);
}
static void binaryLimits()
{
  ZuTestScope(binaryLimits);
  constexpr auto Wire = "95=4\1" "96=A\1\0=\1" "5000=7\1"_z;
  Limits limits; limits.fieldBytes = 4; limits.frameBytes = Wire.length();
  BinaryValue value, copy;
  ZuCheck(!load(value, Wire, limits) && value.data.length() == 4 && value.after == 7);
  Indexed<BinaryValue, ZuFacet::FIX> indexed{Wire, limits};
  ZuCheck(indexed.load(copy) && copy.data.data() == value.data.data());
  auto bytes = ZtScratch(PrintBuf, Wire.length());
  Sink exact{{bytes.data(), unsigned(Wire.length())}, limits};
  ZuCheck(save(exact, value) && exact.data() == Wire);
  limits.fieldBytes = 3;
  ZuCheck(binaryError(Wire, ErrorCode::Size, 95, 3, limits));
  Sink tooSmall{{bytes.data(), unsigned(Wire.length())}, limits};
  ZuCheck(!save(tooSmall, value) && tooSmall.error().code == ErrorCode::Size &&
    tooSmall.error().tag == 96);
  auto used = tooSmall.length(); tooSmall << "must-not-resume";
  ZuCheck(!tooSmall.flush() && tooSmall.length() == used);
  limits.fieldBytes = 1;
  ZuCheck(binaryError("95=0000\1" "96=\1"_z, ErrorCode::Size, 95, 3, limits));
  limits.fieldBytes = 4; --limits.frameBytes;
  ZuCheck(binaryError(Wire, ErrorCode::Size, 0, 0, limits));
  Sink frameLimit{{bytes.data(), unsigned(Wire.length())}, limits};
  ZuCheck(!save(frameLimit, value) && bool(frameLimit.error()));
}

struct BinaryChars { ZuSpan<char> data; };
ZfStruct(, (BinaryChars, FIX),
  (data, (FIX::ID<95>, FIX::Binary<96>), String));
struct BinaryWide { ZuSpan<const uint32_t> data; };
ZfStruct(, (BinaryWide, FIX),
  (data, (FIX::ID<95>, FIX::Binary<96>, Required), Bytes));
struct BinaryWideArgs {
  ZuSpan<uint32_t> data;
  explicit BinaryWideArgs(ZuSpan<uint32_t> input) : data{input} { }
};
ZfStruct(, (BinaryWideArgs, FIX),
  (data, (FIX::ID<95>, FIX::Binary<96>, Ctor<0>, Required), Bytes));
static void binarySpanExtents()
{
  ZuTestScope(binarySpanExtents);
  // This is caller-owned mutable input. Octet counts and element extents differ.
  char backing[] = "95=8\1" "96=\x01\0\xff=\x80\0\x01X\1";
  ZuCSpan wire{backing, sizeof(backing) - 1};
  BinaryChars chars;
  ZuCheck(!load(chars, wire) && chars.data.length() == 8);
  ZuCheck(chars.data.data() == backing + 8);
  chars.data[7] = 'Y';
  ZuCheck(backing[15] == 'Y');
  BinaryWide wide, copy;
  ZuCheck(!load(wide, wire) && wide.data.length() == 2);
  ZuCheck(wide.data.data() == reinterpret_cast<const uint32_t *>(backing + 8));
  Indexed<BinaryWide, ZuFacet::FIX> indexed{wire};
  ZuCheck(indexed.load(copy) && copy.data.data() == wide.data.data() && copy.data.length() == 2);
  Handler<BinaryWideArgs> ctor{wire}; auto args = ctor.ctor();
  ZuCheck(ctor.valid() && args.data.length() == 2);
  ZuCheck(args.data.data() == reinterpret_cast<uint32_t *>(backing + 8));
  auto bytes = ZtScratch(PrintBuf, wire.length());
  Sink out{{bytes.data(), unsigned(wire.length())}};
  ZuCheck(save(out, wide) && out.data() == wire);
  Sink fromCtor{{bytes.data(), unsigned(wire.length())}};
  ZuCheck(save(fromCtor, args) && fromCtor.data() == wire);
  constexpr auto Partial = "95=3\1" "96=ABC\1"_z;
  auto error = load(wide, Partial);
  Indexed<BinaryWide, ZuFacet::FIX> partial{Partial}; partial.load(copy);
  ZuCheck(error.code == ErrorCode::Value);
  ZuCheck(partial.error().code == error.code && partial.error().tag == error.tag &&
    partial.error().offset == error.offset);
}

struct VenueViews {
  ZuBSpan xml;
  ZuCSpan replay;
  int after = 0;
  ZuCSpan signature;
};
ZfStruct(, (VenueViews, FIX),
  (xml, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes),
  (replay, (FIX::ID<95>, FIX::Binary<96>, Required), String),
  (after, (FIX::ID<5000>), Int32),
  (signature, ((FIX::Sig<93, 89>)), String));
struct VenueOwned {
  ZtArray<uint8_t, ZtArrayHeapID<"Zfix.VenueBytes">> xml;
  Text replay;
  int after = 0;
  Text signature;
};
ZfStruct(, (VenueOwned, FIX),
  (xml, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes),
  (replay, (FIX::ID<95>, FIX::Binary<96>, Required), String),
  (after, (FIX::ID<5000>), Int32),
  (signature, ((FIX::Sig<93, 89>)), String));
static void binaryVenues()
{
  ZuTestScope(binaryVenues);
  constexpr auto XML = "<RTRF>8=FIX.4.4\1" "9=12\1" "35=D\1" "10=000\1" "</RTRF>"_z;
  constexpr auto Replay = "8=FIX.4.4\1" "35=D\1" "10=001\1"
    "8=FIX.4.4\1" "35=8\1" "10=002\1"_z;
  constexpr auto Wire = "212=40\1" "213=<RTRF>8=FIX.4.4\1" "9=12\1" "35=D\1"
    "10=000\1" "</RTRF>\1" "95=44\1" "96=8=FIX.4.4\1" "35=D\1" "10=001\1"
    "8=FIX.4.4\1" "35=8\1" "10=002\1" "\1" "5000=7\1" "93=4\1" "89=A\1\0B\1"_z;
  ZuAssert(XML.length() == 40 && Replay.length() == 44);
  VenueViews borrowed, copy;
  ZuCheck(!load(borrowed, Wire) && borrowed.xml == ZuBSpan{XML} && borrowed.replay == Replay);
  ZuCheck(borrowed.xml.data() == reinterpret_cast<const uint8_t *>(Wire.data() + 11));
  ZuCheck(borrowed.replay.data() == Wire.data() + 61 && borrowed.after == 7);
  ZuCheck(borrowed.signature.data() == Wire.data() + 121 && borrowed.signature.length() == 4);
  Indexed<VenueViews, ZuFacet::FIX> indexed{Wire};
  ZuCheck(indexed.load(copy) && copy.xml.data() == borrowed.xml.data() &&
    copy.replay.data() == borrowed.replay.data() &&
    copy.signature.data() == borrowed.signature.data());
  VenueOwned owned, indexedOwned;
  ZuCheck(!load(owned, Wire) && ZuBSpan{owned.xml} == borrowed.xml && owned.replay == Replay);
  ZuCheck(owned.xml.data() != borrowed.xml.data() &&
    owned.replay.data() != borrowed.replay.data());
  ZuCheck(owned.signature == borrowed.signature &&
    owned.signature.data() != borrowed.signature.data());
  Indexed<VenueOwned, ZuFacet::FIX> ownIndex{Wire};
  ZuCheck(ownIndex.load(indexedOwned) && ZuBSpan{indexedOwned.xml} == borrowed.xml &&
    indexedOwned.signature == borrowed.signature && indexedOwned.replay == Replay);
  // The opposite String/Bytes mapping uses the same wire and pair handler.
  BinaryRecord opposite;
  ZuCheck(!load(opposite, Wire) && opposite.xml == XML && opposite.raw == ZuBSpan{Replay});
  ZuCheck(opposite.xml.data() == Wire.data() + 11 && opposite.raw.data() ==
    reinterpret_cast<const uint8_t *>(Wire.data() + 61));
  ZuCheck(opposite.signature.data() == reinterpret_cast<const uint8_t *>(Wire.data() + 121));
  auto bytes = ZtScratch(PrintBuf, Wire.length());
  Sink out{{bytes.data(), unsigned(Wire.length())}};
  ZuCheck((save<ZuFacet::FIX, ZfFieldFilter::Save>(out, borrowed) && out.data() == Wire));
  Sink ownerOut{{bytes.data(), unsigned(Wire.length())}};
  ZuCheck((save<ZuFacet::FIX, ZfFieldFilter::Save>(ownerOut, owned) && ownerOut.data() == Wire));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(binaryFields);
  ZuTestCall(binaryVenues);
  ZuTestCall(binarySpanExtents);
  ZuTestCall(binaryMalformed);
  ZuTestCall(binarySelection);
  ZuTestCall(binaryLimits);
  ZuTestCall(binaryScopes);
  ZuTestCall(singlePass);
  ZuTestCall(inspect);
  ZuTestCall(stringLists);
  ZuTestCall(filters);
  ZuTestCall(cstrings);
  ZuTestCall(numbers);
  ZuTestCall(names);
  ZuTestCall(pointers);
  ZuTestCall(multiple);
  ZuTestCall(defaults);
  ZuTestCall(conversions);
  ZuTestCall(construction);
  ZuTestCall(components);
  ZuTestCall(bounds);
  ZuTestCall(output);
  ZuTestCall(policies);
  ZuTestCall(allocation);
  ZuTestCall(tagDispatch);
  ZuTestCall(signedTags);
  ZuTestCall(customAndBorrowed);
  ZuTestCall(byteFormats);
  ZuTestCall(numberFormats);
  ZuTestCall(timeFormats);
  return 0;
}
