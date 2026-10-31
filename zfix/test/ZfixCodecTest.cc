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

struct Binary {
  ZuBSpan	bytes;
};

ZfStruct(, (Binary, FIX),
  (bytes, (FIX::ID<96>, FIX::Len<95>),		Bytes));
ZfStructImpl(Binary);
ZfStructImpl(Binary, FIX);

struct SkippedBinary {
  ZuBSpan	bytes;
  int		number = 0;
};

ZfStruct(, (SkippedBinary, FIX),
  ((bytes, Rd), (FIX::ID<96>, FIX::Len<95>),	Bytes),
  (number, (FIX::ID<21>),				Int32));
ZfStructImpl(SkippedBinary);
ZfStructImpl(SkippedBinary, FIX);

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
  ZuCheck(load(n, "54=invalid\1"_z).code == ErrorCode::Value);
  ZuCheck(load(n, "9003=One invalid\1"_z).code == ErrorCode::Value);
  ZuCheck(load(n, "54=SellX\1"_z).code == ErrorCode::Value);
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
  ZuCheck(load(n, "8003=1e2\1"_z).code == ErrorCode::Value);
  ZuCheck(load(n, "8004=nan\1"_z).code == ErrorCode::Value);
  Record r;
  auto nullTime = handler<Record, ZuFacet::Millis>("60=\1");
  ZuCheck(!nullTime.load(r) && nullTime.error().code == ErrorCode::Value);
  constexpr auto NullWire = "60=\1"_z;
  char nullBytes[NullWire.length()];
  Sink nullOut{{nullBytes, sizeof(nullBytes)}};
  ZuCheck(save<ZuFacet::Millis>(nullOut, r) && nullOut.data() == NullWire);
  Sink invalid{{bytes, sizeof(bytes)}};
  ZuCheck(!save(invalid, r) && invalid.error().code == ErrorCode::Value);
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
  ZuCheck(load(l, "8005=-3\1"_z).code == ErrorCode::Value);
  ZuCheck(load(l, "8005=3\1"_z).code == ErrorCode::Value);
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
  ZuCheck(load(b, "11=A\0B\1"_z).code == ErrorCode::Value);
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
  ZuCheck(!h.valid() && !failed.owned);
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
  ZuCheck(load(lists, "9010=first \1"_z).code == ErrorCode::Value);
  ZuCheck(load(lists, "9010=first bad\0value\1"_z).code == ErrorCode::Value);
  ZuCheck(!update(lists, {}) && lists.strings.length() == 2);
  ZuCheck(ZuCSpan{lists.strings[0]} == "one" && lists.strings[0] != DefaultOne);
  ZuCheck(!load(lists, "9010=\1"_z) && !lists.strings.length());
  constexpr auto BadWire = "9010=alpha beta\1" "9011=bad\0value\1"_z;
  auto bad = handler<CLists>(BadWire);
  ZuCheck(!bad.alloc() && bad.error().code == ErrorCode::Value);
  CRead skipped;
  ZuCheck(!load(skipped, "9010=alpha beta\1" "1=7\1"_z));
  ZuCheck(!skipped.strings.length() && skipped.number == 7);
  ZuCheck(load(skipped, "9010=first bad\0value\1"_z).code == ErrorCode::Value);
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
    ZuCheck(load(lists, Wire).code == ErrorCode::Value);
  };
  bad("9001= 1\1"_z); bad("9001=1 \1"_z); bad("9001=1  2\1"_z);
  bad("9001=2147483648\1"_z); bad("9001=1 X\1"_z);
  ZuCheck(!update(lists, "9001=\1"_z));
  ZuCheck(!lists.codes.length() && !lists.words.length());
  lists.words.push("bad word");
  Sink invalid{{bytes, sizeof(bytes)}};
  ZuCheck(!save(invalid, lists) && invalid.error().code == ErrorCode::Value);
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
  ZuCheck(!bad.raw<ZfField(Record, number)>().data());
  ZuCheck(bad.error().code == ErrorCode::Value && bad.error().tag == 1000000);
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
  ZuCheck(e.code == ErrorCode::Value && e.tag == 12);
  e = load(r, "1000000=2147483648\1"_z);
  ZuCheck(e.code == ErrorCode::Value && e.tag == 1000000);
  e = load(r, "1000000=1x\1"_z);
  ZuCheck(e.code == ErrorCode::Value);
  e = load(r, "13=1\1"_z);
  ZuCheck(e.code == ErrorCode::Value);
  e = load(r, "44=1e3\1"_z);
  ZuCheck(e.code == ErrorCode::Value);
  e = load(r, "44=23.\1"_z);
  ZuCheck(!e && r.price == ZuDecimal{23});
  e = load(r, "60=202:1026-12:34:56\1"_z);
  ZuCheck(e.code == ErrorCode::Value);
  e = load(r, "60=20260231-12:34:56\1"_z);
  ZuCheck(e.code == ErrorCode::Value);
  e = load(r, "1000000=1\1" "1000000=2\1"_z);
  ZuCheck(e.code == ErrorCode::Duplicate);
  e = load(r, "11=missing-delimiter"_z);
  ZuCheck(e.code == ErrorCode::Truncated);
  e = load(r, "011=x\1"_z);
  ZuCheck(e.code == ErrorCode::Tag);
  e = load(r, "11=bad\0value\1"_z);
  ZuCheck(e.code == ErrorCode::Value && e.tag == 11);
  e = load(r, "11=bad\nvalue\1"_z);
  ZuCheck(e.code == ErrorCode::Value);
  e = load(r, "11=bad\x85value\1"_z);
  ZuCheck(e.code == ErrorCode::Value);
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
  uint128_t magnitude;
  bool negative;
  ZuCheck((integer<1, true>("0", magnitude, negative) && magnitude == 0));
  ZuCheck((!integer<1, true>("1", magnitude, negative)));
  ZuCheck((integer<1, true>("-1", magnitude, negative) && magnitude == 1 && negative));
  ZuCheck((!integer<1, false>("2", magnitude, negative)));
  ZuCheck((integer<128, false>("340282366920938463463374607431768211455",
    magnitude, negative)));
  ZuCheck(magnitude == ~uint128_t(0));
  ZuCheck((!integer<128, false>("340282366920938463463374607431768211456",
    magnitude, negative)));
  ZuCheck((!integer<32, false>("-1", magnitude, negative)));
  Token token;
  ZuCheck(!scan("4294967295=x\1"_z, 0, token) && token.tag == UINT_MAX);
  ZuCheck(scan("4294967296=x\1"_z, 0, token).code == ErrorCode::Tag);
  ZuCheck(scan("999999999999999999999=x\1"_z, 0, token).code == ErrorCode::Tag);
  ZuCheck(scan("4294967295"_z, 0, token).code == ErrorCode::Truncated);
}

template <unsigned Bits, bool Sign> static bool integerWidth()
{
  constexpr uint128_t Max = (~uint128_t(0) >> (128 - Bits)) >> unsigned(Sign);
  using U = ZuIf<(Bits <= 8), uint8_t, ZuIf<(Bits <= 16), uint16_t,
    ZuIf<(Bits <= 32), uint32_t, ZuIf<(Bits <= 64), uint64_t, uint128_t>>>>;
  U magnitude;
  bool negative;
  Text bound; bound << ZuBox<uint128_t>{Max};
  if (!integer<Bits, Sign>(bound, magnitude, negative) || magnitude != Max || negative)
    return false;
  Text padded; padded << "00000000000000000000" << bound;
  if (!integer<Bits, Sign>(padded, magnitude, negative) || magnitude != Max || negative)
    return false;
  Text over;
  if constexpr (Max != ~uint128_t(0)) over << ZuBox<uint128_t>{Max + 1};
  else over << bound << '0';
  if (integer<Bits, Sign>(over, magnitude, negative)) return false;
  padded.length(0); padded << "00000000000000000000" << over;
  if (integer<Bits, Sign>(padded, magnitude, negative)) return false;
  if constexpr (Sign) {
    bound.length(0); bound << '-' << ZuBox<uint128_t>{Max + 1};
    if (!integer<Bits, Sign>(bound, magnitude, negative) ||
	magnitude != Max + 1 || !negative) return false;
    padded.length(0); padded << "-00000000000000000000" << ZuBox<uint128_t>{Max + 1};
    if (!integer<Bits, Sign>(padded, magnitude, negative) ||
	magnitude != Max + 1 || !negative) return false;
    over.length(0); over << '-' << ZuBox<uint128_t>{Max + 2};
    if (integer<Bits, Sign>(over, magnitude, negative)) return false;
    if (!integer<Bits, Sign>("-0", magnitude, negative) || magnitude || !negative) return false;
  } else if (integer<Bits, Sign>("-1", magnitude, negative)) return false;
  return !integer<Bits, Sign>({}, magnitude, negative) &&
    !integer<Bits, Sign>("+1", magnitude, negative) &&
    !integer<Bits, Sign>("1x", magnitude, negative) &&
    !integer<Bits, Sign>(" 1", magnitude, negative) &&
    !integer<Bits, Sign>("-", magnitude, negative);
}
static void integerWidths()
{
  ZuTestScope(integerWidths);
  bool valid = true;
  ZuUnroll::all<ZuSeq<1, 8, 16, 32, 64, 128>>([&valid](auto I) -> void {
    valid = valid && integerWidth<I, false>() && integerWidth<I, true>();
  });
  ZuCheck(valid);
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
  Sink invalid{{bytes, sizeof(bytes)}};
  ZuCheck(!save(invalid, a));
  ZuCheck(invalid.failed());
}

static void binary()
{
  ZuTestScope(binary);
  constexpr auto Wire = "95=4\1" "96=A\1\0=\1"_z;
  Binary b;
  auto e = load(b, Wire);
  ZuCheck(!e);
  ZuCheck(b.bytes == ZuBSpan{"A\1\0="_z});
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, b));
  ZuCheck(out.data() == Wire && out.flush());
  SkippedBinary skipped;
  e = load(skipped, "95=4\1" "96=A\1\0=\1" "21=8\1"_z);
  ZuCheck(!e);
  ZuCheck(skipped.number == 8 && !skipped.bytes);
  Limits cfg;
  cfg.fieldBytes = 1;
  auto tooLong = load(b, "95=0000\1" "96=\1"_z, cfg);
  ZuCheck(tooLong.code == ErrorCode::Size && tooLong.tag == 95);
  e = load(b, "96=data\1"_z);
  ZuCheck(e.code == ErrorCode::Pair);
  e = load(b, "95=4\1" "97=data\1"_z);
  ZuCheck(e.code == ErrorCode::Pair);
  e = load(b, "95=9\1" "96=data\1"_z);
  ZuCheck(e.code == ErrorCode::Truncated);
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
  ZuCheck(load(c, "54=22\1"_z).code == ErrorCode::Value);
  Accessors a;
  ZuCheck(!update(a, "2=8\1"_z));
  ZuCheck(a.key == 1 && a.value == 8 && a.immutable == 55);
  ZuCheck(!update(a, {}));
  ZuCheck(a.key == 1 && a.value == 2 && a.immutable == 55);
  ZuCheck(load(a, {}).code == ErrorCode::Required);
  ZuCheck(!load(a, "1=7\1" "2=9\1" "3=4\1" "4=999\1"_z));
  ZuCheck(a.key == 7 && a.value == 9 && a.immutable == 4);
  ZuCheck(update(a, "3=bad\1"_z).code == ErrorCode::Value);
  ZuCheck(update(a, "4=bad\1"_z).code == ErrorCode::Value);
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
  ZuCheck(!bad.alloc());
  ZuCheck(bad.error().code == ErrorCode::Value);
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
  FormatBytes	b64, url, b32, hex, raw;
};

ZfStruct(, (ByteFormats, FIX),
  (b64, (FIX::ID<6101>, FIX::Len<6100>, FIX::Base64),		Bytes),
  (url, (FIX::ID<6111>, FIX::Len<6110>, FIX::Base64URL),	Bytes),
  (b32, (FIX::ID<6121>, FIX::Base32),				Bytes),
  (hex, (FIX::ID<6131>, FIX::Hex),				Bytes),
  (raw, (FIX::ID<6141>, FIX::Len<6140>, FIX::Raw),		Bytes));
ZfStructImpl(ByteFormats, FIX);

static void byteFormats()
{
  ZuTestScope(byteFormats);
  constexpr auto Data = "\xfb\xff"_z;
  constexpr auto Wire = "6100=4\1" "6101=+/8=\1" "6110=3\1" "6111=-_8\1"
    "6121=7P7Q====\1" "6131=FBFF\1" "6140=2\1" "6141=\xfb\xff\1"_z;
  ByteFormats value;
  value.b64 = Data; value.url = Data; value.b32 = Data; value.hex = Data; value.raw = Data;
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, value) && out.data() == Wire);
  ByteFormats copy;
  auto input = handler<ByteFormats>(Wire);
  ZuCheck(input.check() && input.load(copy));
  ZuCheck(ZuCSpan{copy.b64} == Data && ZuCSpan{copy.url} == Data &&
    ZuCSpan{copy.b32} == Data && ZuCSpan{copy.hex} == Data && ZuCSpan{copy.raw} == Data);
  ZuCheck(load(copy, "6100=4\1" "6101=TQ?=\1"_z).code == ErrorCode::Value);
  ZuCheck(load(copy, "6100=3\1" "6101=TQ=\1"_z).code == ErrorCode::Value);
  ZuCheck(load(copy, "6121=ABC\1"_z).code == ErrorCode::Value);
  ZuCheck(load(copy, "6131=F\1"_z).code == ErrorCode::Value);
  ZuCheck(load(copy, "6131=FG\1"_z).code == ErrorCode::Value);
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
  ZuCheck(load(copy, "6201=0x100000000\1"_z).code == ErrorCode::Value);
  ZuCheck(load(copy, "6202=18,446,744,073,709,551,616\1"_z).code == ErrorCode::Value);
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
  ZuCheck(load(copy, "6301=1970-01-01T00:20:34.567Z\1"_z).code == ErrorCode::Value);
  ZuCheck(load(copy, "6303=not-a-time\1"_z).code == ErrorCode::Value);
  ZuCheck(load(copy, "6305=1e3\1"_z).code == ErrorCode::Value);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
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
  ZuTestCall(integerWidths);
  ZuTestCall(output);
  ZuTestCall(binary);
  ZuTestCall(policies);
  ZuTestCall(allocation);
  ZuTestCall(customAndBorrowed);
  ZuTestCall(byteFormats);
  ZuTestCall(numberFormats);
  ZuTestCall(timeFormats);
  return 0;
}
