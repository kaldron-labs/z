//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// FIX facet, properties and typed conversion declarations

#ifndef ZfixFmt_HH
#define ZfixFmt_HH

#ifndef ZfixFmtLib_HH
#include <zlib/ZfixFmtLib.hh>
#endif

#include <limits.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZuFixed.hh>
#include <zlib/ZuIntrin.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/Zu_aton.hh>
#include <zlib/Zu_ntoa.hh>

#include <zlib/ZmLHash.hh>
#include <zlib/ZmStackAvail.hh>
#include <zlib/ZmSpecific.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtBytesFmt.hh>
#include <zlib/ZtFmt.hh>

#include <zlib/ZfStruct.hh>

ZuStructFacet(FIX);

namespace Zfix::Time {

  enum { ISO, FIX, CSV, Unix };
  enum { Sec, MSec, USec, NSec };
}

namespace Zfix {

template <unsigned Fmt_ = Time::FIX, unsigned Unit_ = Time::Sec, int NDP_ = -9>
struct TimeFmt {
  enum { Fmt = Fmt_, Unit = Unit_, NDP = NDP_ };
  ZuAssert(Fmt <= Time::Unix && Unit <= Time::NSec && NDP >= -9 && NDP <= 9);
};

}

namespace ZuFieldProp::FIX {

  template <unsigned N> struct ID { ZuAssert(N > 0); };

  template <unsigned N> struct Len { ZuAssert(N > 0); };

  struct Char { };

  struct Multiple { };

  template <uint8_t I> struct BytesFmt { };

  template <typename Fmt> struct NumberFmt { using T = Fmt; };

  template <typename Fmt> struct TimeFmt { using T = Fmt; };

  template <bool> struct Optional { };

  using Base64 = BytesFmt<ZtBytesFmt::Base64>;
  using Base64URL = BytesFmt<ZtBytesFmt::Base64URL>;
  using Base32 = BytesFmt<ZtBytesFmt::Base32>;
  using Hex = BytesFmt<ZtBytesFmt::Hex>;
  using Raw = BytesFmt<ZtBytesFmt::Raw>;

  template <typename Fmt = ZtFmt::Default> using Number = NumberFmt<Fmt>;
  template <uint8_t Scale, int8_t NDP>
  using ISO = TimeFmt<Zfix::TimeFmt<Zfix::Time::ISO, Scale, NDP>>;
  template <uint8_t Scale, int8_t NDP>
  using FIX = TimeFmt<Zfix::TimeFmt<Zfix::Time::FIX, Scale, NDP>>;
  template <uint8_t Scale, int8_t NDP>
  using CSV = TimeFmt<Zfix::TimeFmt<Zfix::Time::CSV, Scale, NDP>>;
  template <uint8_t Scale, int8_t NDP>
  using Unix = TimeFmt<Zfix::TimeFmt<Zfix::Time::Unix, Scale, NDP>>;

  using Opt = Optional<true>;

  template <typename Props, bool = HasValue<Props, BytesFmt>{}>
  struct GetBytesFmt_ { using T = ZuConstant<uint8_t, ZtBytesFmt::Raw>; };

  template <typename Props>
  struct GetBytesFmt_<Props, true> { using T = GetValue<Props, BytesFmt>; };

  template <typename Props> using GetBytesFmt = GetBytesFmt_<Props>::T;

  template <typename Props, bool = HasValue<Props, Optional>{}>
  struct GetOptional_ { using T = ZuFalse; };

  template <typename Props>
  struct GetOptional_<Props, true> { using T = GetValue<Props, Optional>; };

  template <typename Props> using GetOptional = GetOptional_<Props>::T;

}

namespace Zfix::Fmt {

// Workload bounds, shared by standalone callers and managed hubs.
// Defaults accommodate ordinary orders; large market-data dictionaries must
// override them explicitly. Enforcement is supplied by the codec/session.
struct Limits {
  enum {
    FrameBytes = 1 << 20, FieldBytes = 1 << 18,
    GroupEntries = 1 << 16, GroupDepth = 16, ParseBytes = 4 << 20
  };
  unsigned	frameBytes = FrameBytes;
  unsigned	fieldBytes = FieldBytes;
  unsigned	groupEntries = GroupEntries;
  unsigned	groupDepth = GroupDepth;
  unsigned	parseBytes = ParseBytes;
};

// A deferred value borrows the complete bounded body. The application/catalog
// selects one alternative before schema-aware parsing; no alternatives are
// tried to infer a discriminator or binary/group boundaries.
struct ParseView {
  ZuCSpan	input;
  Limits	limits;
};

struct DefltConfig {
  using NumberFmt = ZtFmt::Default;
  using TimeFmt = Zfix::TimeFmt<>;
  template <typename U> using Union = void;
};

namespace Kind {

  enum { Deflt, Object, String, Array, Union };
}

struct AsObject { enum { Kind = Kind::Object }; };

struct AsString { enum { Kind = Kind::String }; };

struct AsUnion { enum { Kind = Kind::Union }; };

template <unsigned Code = ZfFieldTC::UDT,
  typename Props = ZuTypeList<>> struct AsArray {
  enum { Kind = Kind::Array, ElemCode = Code };
  using ElemProps = Props;
};

struct AsDeflt { enum { Kind = Kind::Deflt }; };

// Explicit primitive-list policy: one space-delimited value, never repeated tags.
template <unsigned Code, typename Props = ZuTypeList<>>
struct AsMultiple : AsString {
  enum { ElemCode = Code };
  using ElemProps = Props;
};

template <typename T> struct Multiple : ZuFalse { };

template <unsigned Code, typename Props>
struct Multiple<AsMultiple<Code, Props>> : ZuTrue { };

namespace ErrorCode {

  enum { OK, Tag, Truncated, Size, Duplicate, Value, Required, Pair,
    Reserved, Group, Depth, Unresolved, Output, Envelope, Length, Checksum };
}

struct Error {
  unsigned	code = ErrorCode::OK;
  unsigned	tag = 0;
  unsigned	offset = 0;

  explicit operator bool() const { return code != ErrorCode::OK; }
};

// Standalone, non-publishing output into caller-selected storage. Printers
// write directly to the destination; only a tight-tail upper-bound fallback
// uses scratch. Failure is sticky, and flush() must be checked by the caller.
using PrintHeap = ZmVHeap_<ZuStringT<"Zfix.Print">>;
using PrintBuf = ZtArray<char, ZtArrayHeapID<"Zfix.Print",
  ZtArrayHeapMin<PrintHeap::Min, ZtArrayHeapMax<PrintHeap::Max>>>>;
class ZfixFmtAPI Sink {
public:
  // Preserve the first used bytes and return the new writable span. An empty
  // return declines growth; it must leave the original span valid. The callback
  // runs only before a write, and never re-enters this sink.
  using Grow = ZuSpan<char> (*)(void *, unsigned used, unsigned needed);
  explicit Sink(ZuSpan<char> storage, Limits limits = {},
    Grow grow = nullptr, void *owner = nullptr);

  bool failed() const { return bool(m_error); }
  const Error &error() const { return m_error; }
  unsigned length() const { return m_used; }

  unsigned available() const {
    unsigned n = m_storage.length() < m_limits.frameBytes ?
      m_storage.length() : m_limits.frameBytes;
    return n - m_used;
  }

  ZuCSpan data() const { return {m_storage.data(), m_used}; }
  bool flush() const { return !failed(); }

  void fail(unsigned code = ErrorCode::Output);

  void fail(Error error) { if (!failed()) m_error = error; }

  void field(unsigned tag, bool binary = false);
  void end();
  bool group(uint64_t count);
  void endGroup();
  void entry(unsigned offset, unsigned &delimiter);

  Sink &operator <<(char c) { put({&c, 1}); return *this; }

  template <typename T, typename = ZuIfT<ZuPrint<T>::OK ||
    ZuTraits<T>::IsReal || ZuTraits<T>::IsArray || ZuTraits<T>::IsString>>
  Sink &operator <<(const T &value) {
    if (failed()) return *this;
    using P = ZuPrint<T>;
    if constexpr (P::Delegate) P::print(*this, value);
    else if constexpr (P::Buffer) {
      unsigned n = P::length(value);
      if (n <= available()) {
	auto ptr = m_storage.data() + m_used;
	unsigned written = P::print(ptr, n, value);
	if (check({ptr, written})) m_used += written;
      } else {
	// Framework print lengths can be upper bounds, e.g. integer digit
	// widths. A value fitting the exact tail must still succeed.
	// The upper bound comes from the selected formatter, not a wire
	// length. Enforce output limits on the actual bytes below.
	bool heap = (ZmStackAvail()>>1) < n;
	auto data = heap ? static_cast<char *>(PrintHeap::valloc(n)) :
	  static_cast<char *>(ZuAlloca(n, alignof(char)));
	// Track allocation ownership explicitly: sanitizer fake stacks need
	// not lie inside the thread's native stack address range.
	PrintBuf scratch{data, 0, n, heap};
	put({scratch.data(), P::print(scratch.data(), n, value)});
      }
    } else if constexpr (ZuTraits<T>::IsReal) *this << ZuBox<T>{value};
    else put(ZuCSpan{value});
    return *this;
  }

private:
  bool check(ZuCSpan value);
  void put(ZuCSpan value);

  ZuSpan<char>	m_storage;
  Limits	m_limits;
  Error		m_error;
  Grow		m_grow = nullptr;
  void		*m_owner = nullptr;
  unsigned	m_used = 0;
  unsigned	m_start = 0;
  unsigned	m_tag = 0;
  unsigned	m_depth = 0;
  unsigned	m_entries = 0;
  bool		m_value = false;
  bool		m_binary = false;
};

struct Position {
  unsigned	offset;
  unsigned	length;
  unsigned	child;
  unsigned	count;
};

// One borrowed text field, independent of the codec's group position tree.
struct FieldPos {
  unsigned	offset;
  unsigned	length;
};

using Positions = ZtArray<Position, ZtArrayHeapID<"Zfix.Parse">>;

// All entries use offsets into one caller-owned input and one bounded slot
// array. Growing the slot array never invalidates a stored pointer: child
// scopes and entries are addressed by indexes until parsing is complete.
struct ZfixFmtAPI Parse {
  ZuCSpan	input;
  Limits	limits;
  Positions	&positions;
  Error		&error;
  unsigned	entries = 0;
  bool fail(unsigned code, unsigned tag, unsigned offset) {
    if (!error) error = {code, tag, offset};
    return false;
  }

  unsigned slots(unsigned count);
};

struct Scope {
  const Scope	*parent;
  bool (*contains)(unsigned);
  unsigned	delimiter;
  unsigned	depth;

  bool outside(unsigned tag) const {
    for (auto p = parent; p; p = p->parent)
      if (p->contains(tag)) return true;
    return false;
  }
};

struct Token {
  unsigned	tag;
  unsigned	offset;
  unsigned	length;
  unsigned	next;
};

// Scanning is shared with framing/discriminator inspection. Offsets address
// original caller-owned bytes; no token allocates or null-terminates input.
ZfixFmtExtern Error scan(ZuCSpan, unsigned, Token &);
ZfixFmtExtern Error scanData(ZuCSpan, unsigned, unsigned, unsigned, Token &);
// Inspect one unique field in SOH-delimited text. Binary Length/Data inspection
// is outside this contract. An absent field leaves offset == UINT_MAX.
ZfixFmtExtern Error inspect(ZuCSpan, unsigned tag, FieldPos &, Limits = {}, unsigned offset = 0);
// Bound significant digits before scanning: Zu_nscan does not detect overflow.
// Only boundary-width values need a lexical comparison with the wire limit.
template <unsigned Bits, bool Sign, typename T, typename Fmt = ZuFmt::Default>
bool integer(ZuCSpan value, T &magnitude, bool &negative) {
  ZuAssert(Bits && Bits <= 128);
  ZuAssert(ZuTraits<T>::IsIntegral && !ZuTraits<T>::IsSigned && (sizeof(T)<<3) >= Bits);
  using U = ZuIf<(Bits <= (sizeof(unsigned)<<3)), unsigned,
    ZuIf<(Bits <= 64), uint64_t, uint128_t>>;
  constexpr U Max = (~U(0) >> ((sizeof(U)<<3) - Bits)) >> unsigned(Sign);
  constexpr U Min = Max + unsigned(Sign);
  unsigned n = value.length();
  if (!n) return false;
  const char *data = value.data();
  if constexpr (Fmt::Justification_ != ZuFmt::Just::None) {
    if (n > Fmt::Width_) return false;
    if constexpr (Fmt::Pad_ && Fmt::Pad_ != '0') {
      if constexpr (Fmt::Justification_ == ZuFmt::Just::Right)
	while (n && *data == Fmt::Pad_) { ++data; --n; }
      else while (n && data[n - 1] == Fmt::Pad_) --n;
    }
    if (!n) return false;
  }
  negative = data[0] == '-';
  if (negative) {
    if constexpr (!Sign) return false;
    if (n == 1) return false;
    ++data; --n;
  }
  if constexpr (Fmt::Hex_)
    if (n > 2 && data[0] == '0' && data[1] == 'x') { data += 2; n -= 2; }
  while (n > 1 && (*data == '0' || (Fmt::Comma_ && *data == Fmt::Comma_))) {
    ++data; --n;
  }
  unsigned digits = n;
  if constexpr (Fmt::Comma_) {
    digits = 0;
    for (unsigned i = 0; i < n; ++i) if (data[i] != Fmt::Comma_) ++digits;
    if (!digits) return false;
  }
  unsigned width = Zu_ntoa::LogN<Fmt::Hex_, sizeof(U)>::log(Max);
  if (digits > width) return false;
  if (digits == width) {
    using BaseFmt = ZuIf<Fmt::Hex_, ZuFmt::Hex<true>, ZuFmt::Default>;
    char bound[Zu_nprint<BaseFmt>::template ulen<U>()];
    Zu_nprint<BaseFmt>::utoa(negative ? Min : Max, bound);
    if constexpr (!Fmt::Hex_ && !Fmt::Comma_) {
      if (memcmp(data, bound, n) > 0) return false;
    } else {
      int cmp = 0;
      for (unsigned i = 0, j = 0; i < n; ++i) {
	if (Fmt::Comma_ && data[i] == Fmt::Comma_) continue;
	int digit, limit;
	if constexpr (Fmt::Hex_) {
	  digit = Zu_aton::Base16<Fmt>::hex(data[i]); limit = Zu_aton::Base16<Fmt>::hex(bound[j++]);
	  if (digit < 0) return false;
	} else { digit = uint8_t(data[i]); limit = bound[j++]; }
	if (!cmp && digit != limit) cmp = digit < limit ? -1 : 1;
      }
      if (cmp > 0) return false;
    }
  }
  U v;
  if (Zu_nscan<Fmt>::atou(v, data, n) != n) return false;
  magnitude = v;
  return true;
}
ZfixFmtExtern bool decimal(ZuCSpan);
ZfixFmtExtern bool timestamp(ZuCSpan);
ZfixFmtExtern bool text(ZuCSpan);
// CString metadata uses the framework C ABI ownership convention: malloc/free,
// independently of the input lifetime. This allocation is destination storage.
ZfixFmtExtern char *cstring(ZuCSpan, Error &);
constexpr bool reserved(unsigned tag) {
  switch (tag) {
    case 8: case 9: case 10: case 35: case 34: case 49: case 56:
    case 52: case 43: case 122: case 90: case 91: case 93: case 89: case 347:
    case 50: case 57: case 142: case 143: case 1128: return true;
    default: return false;
  }
}

struct AsStringDeflt {
  template <typename O> struct Handler {
    static O load(ZuCSpan value, Error &) { return O(value); }

    template <typename S>
    static void save(S &s, const O &o) { s << o; }
  };

};

} // Zfix::Fmt

Zfix::Fmt::DefltConfig Zfix_Config(...);
Zfix::Fmt::AsDeflt Zfix_Fmt(...);
Zfix::Fmt::AsStringDeflt Zfix_StringFmt(...);

#define ZfixConfig(Facet, Config) \
  namespace ZuFacet { ZuPP_Strip(Config) Zfix_Config(Facet *); }

namespace Zfix::Fmt {

template <typename Facet>
using Config = decltype(Zfix_Config(ZuDeclVal<Facet *>()));
template <typename O>
using As = decltype(Zfix_Fmt(ZuDeclVal<O *>()));
template <typename O, typename Facet>
using Format = ZuIf<As<O>::Kind == Kind::Deflt,
  ZuIf<bool(ZuActiveFields<O, Facet>::N), AsObject, AsString>, As<O>>;

template <typename T, bool = ZuTraits<T>::IsPointer>
struct ObjPtr : ZuFalse { };

template <typename T> struct ObjPtr<T, true> : ZuBool<
  ZuIsSame<As<T>, AsDeflt>{}

  &&
  ZuTraits<ZuDecay<decltype(*ZuDeclVal<T>())>>::IsComposite> { };
template <typename T, bool = ObjPtr<T>{}>
struct ObjType_ { using T_ = T; };

template <typename T> struct ObjType_<T, true> {
  using T_ = ZuDecay<decltype(*ZuDeclVal<T>())>;
};

template <typename T> using ObjType = ObjType_<T>::T_;

// A flattened union needs an explicitly selected object alternative so its
// tags and binary/group boundaries are known before scanning the parent scope.
template <typename F, typename Facet, bool = F::Type::Code == ZfFieldTC::UDT>
struct UnionField : ZuFalse { };

template <typename F, typename Facet>
struct UnionField<F, Facet, true> :
  ZuBool<Format<typename F::T, Facet>::Kind == Kind::Union> { };

template <typename F, typename Facet, bool = UnionField<F, Facet>{}>
struct ComponentType_ { using T = ObjType<typename F::T>; };

template <typename F, typename Facet>
struct ComponentType_<F, Facet, true> {
  using T = Config<Facet>::template Union<typename F::T>;
  ZuAssert((!ZuIsSame<T, void>{}));
  ZuAssert((Format<T, Facet>::Kind == Kind::Object));
  ZuAssert((typename F::T::template In<T>{}));
};

template <typename F, typename Facet>
using ComponentType = ComponentType_<F, Facet>::T;
template <typename F, typename Facet, bool = F::Type::Code == ZfFieldTC::UDT>
struct Component : ZuFalse { };

template <typename F, typename Facet>
struct Component<F, Facet, true> : ZuBool<
  Format<ObjType<typename F::T>, Facet>::Kind == Kind::Object || UnionField<F, Facet>{}> { };

template <typename F, typename Facet, bool = F::Type::Code == ZfFieldTC::UDT>
struct GroupField : ZuFalse { };

template <typename F, typename Facet>
struct GroupField<F, Facet, true> :
  ZuBool<Format<typename F::T, Facet>::Kind == Kind::Array> { };

template <typename T>
using GroupElem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;

template <typename F, typename Path> struct Leaf { using Field = F; };

template <typename Fields, typename Facet, typename Path> struct Flatten;
template <typename F, typename Facet, typename Path,
  bool = Component<F, Facet>{}> struct FlattenField {
  using T = ZuTypeList<Leaf<F, Path>>;
};

template <typename F, typename Facet, typename Path>
struct FlattenField<F, Facet, Path, true> {
  using T = Flatten<ZuActiveFields<ComponentType<F, Facet>, Facet>, Facet,
    typename Path::template Push<F>>::T;
};

template <typename ...F, typename Facet, typename Path>
struct Flatten<ZuTypeList<F...>, Facet, Path> {
  using T = ZuTypeConcat<typename FlattenField<F, Facet, Path>::T...>;
};

template <typename F> consteval unsigned tag() {
  using P = F::Props;
  ZuAssert((ZuFieldProp::HasValue<P, ZuFieldProp::FIX::ID>{}));
  return ZuFieldProp::GetValue<P, ZuFieldProp::FIX::ID>{};
}
template <typename F> consteval unsigned lenTag() {
  using P = F::Props;
  if constexpr (ZuFieldProp::HasValue<P, ZuFieldProp::FIX::Len>{}) {
    ZuAssert(F::Type::Code == ZfFieldTC::Bytes);
    return ZuFieldProp::GetValue<P, ZuFieldProp::FIX::Len>{};
  } else {
    if constexpr (F::Type::Code == ZfFieldTC::Bytes) {
      ZuAssert((ZuFieldProp::HasValue<P, ZuFieldProp::FIX::BytesFmt>{}));
      ZuAssert((ZuFieldProp::GetValue<P, ZuFieldProp::FIX::BytesFmt>{} != ZtBytesFmt::Raw));
    }
    return 0;
  }
}

template <typename Leaves, typename Facet>
struct TagIndex : ZmLHashKV<unsigned, unsigned,
  ZmLHashStatic<(Leaves::N > 1 ? ZuIntrin::log2(Leaves::N) + 2 : 2),
    ZmLHashLocal<>>> {
  using Base = ZmLHashKV<unsigned, unsigned,
    ZmLHashStatic<(Leaves::N > 1 ? ZuIntrin::log2(Leaves::N) + 2 : 2),
      ZmLHashLocal<>>>;

  TagIndex() {
    ZuUnroll::all<Leaves>([this]<typename L>() {
      using F = L::Field;
      enum { I = ZuTypeIndex<L, Leaves>{} };
      // Plain values need only a position write; dispatch only groups/pairs.
      this->add(tag<F>(), (I << 2) | (GroupField<F, Facet>{} ? 2 : 0) |
	(lenTag<F>() ? 2 : 0));
      if constexpr (lenTag<F>()) this->add(lenTag<F>(), (I << 2) | 1);
    });
  }

  int find(unsigned key) const {
    auto i = Base::findVal(key);
    return ZuCmp<unsigned>::null(i) ? -1 : int(i);
  }
};

template <typename O, typename Facet, typename Fields_, bool Envelope_ = false>
struct Schema_ {
  using Fields = Fields_;
  enum { Envelope = Envelope_, Prefix = false };

  static unsigned region(unsigned) { return 1; }

  using Leaves = Flatten<Fields, Facet, ZuTypeList<>>::T;

  static consteval bool unique() {
    bool ok = true;
    ZuUnroll::all<Leaves>([&ok]<typename L>() {
      using F = L::Field;
      enum { N = tag<F>(), Len = lenTag<F>() };
      if constexpr (ZfFieldTC::IsVec<F::Type::Code>{})
	if (!ZuTypeIn<ZuFieldProp::FIX::Multiple, typename F::Props>{}) ok = false;
      if (!N || (!Envelope_ && reserved(N)) ||
	  (Len && (Len == N || (!Envelope_ && reserved(Len))))) ok = false;
      ZuUnroll::all<Leaves>([&ok]<typename R>() {
	if constexpr (ZuTypeIndex<L, Leaves>{} < ZuTypeIndex<R, Leaves>{}) {
	  using G = R::Field;
	  if (N == tag<G>() || (Len && Len == tag<G>()) ||
	      (lenTag<G>() && (N == lenTag<G>() || Len == lenTag<G>())))
	    ok = false;
	}
      });
    });
    return ok;
  }

  ZuAssert(unique());

  template <typename F> static consteval unsigned offset() {
    unsigned n = 0;
    ZuUnroll::all<Fields>([&n]<typename G>() {
      if constexpr (ZuTypeIndex<G, Fields>{} < ZuTypeIndex<F, Fields>{})
	n += FlattenField<G, Facet, ZuTypeList<>>::T::N;
    });
    return n;
  }

  // Schema-sized immutable metadata; the local/static hash stores its slots
  // inline and performs no heap allocation or per-object reconstruction.
  static const auto &index() { static const TagIndex<Leaves, Facet> map; return map; }
};

template <typename O, typename Facet>
struct Schema : Schema_<O, Facet, ZuActiveFields<O, Facet>> { };

template <typename O, typename Facet>
bool parseScope(Parse &, unsigned, unsigned &, Scope &, bool, bool = true, unsigned = 0);

template <typename O, typename Facet> bool scopeContains(unsigned tag) {
  return Schema<O, Facet>::index().find(tag) >= 0;
}
template <typename O, typename Facet>
bool parseGroup(Parse &p, unsigned slot, Token &token, Scope &parent) {
  using E = GroupElem<O>;
  using S = Schema<E, Facet>;
  ZuAssert((Format<O, Facet>::ElemCode == ZfFieldTC::UDT));
  ZuAssert((Format<E, Facet>::Kind == Kind::Object));
  ZuAssert(S::Leaves::N > 0);
  unsigned n;
  bool negative;
  if (!integer<32, false>({p.input.data() + token.offset, token.length}, n, negative))
    return p.fail(ErrorCode::Value, token.tag, token.offset);
  if (parent.depth >= p.limits.groupDepth)
    return p.fail(ErrorCode::Depth, token.tag, token.offset);
  if (n > p.limits.groupEntries - p.entries)
    return p.fail(ErrorCode::Size, token.tag, token.offset);
  // Every entry must emit at least one tag, '=', SOH (even an empty value).
  // Session::Reject impossible counts before retaining metadata for them.
  if (n > (p.input.length() - token.next)/3)
    return p.fail(ErrorCode::Group, token.tag, token.offset);
  p.entries += n;
  if (n > p.limits.parseBytes/(sizeof(Position) * S::Leaves::N))
    return p.fail(ErrorCode::Size, token.tag, token.offset);
  unsigned child = p.slots(n * S::Leaves::N);
  if (p.error) return false;
  p.positions[slot] = {token.offset, token.length, child, n};
  unsigned offset = token.next;
  Scope scope{&parent, scopeContains<E, Facet>, 0, parent.depth + 1};
  for (unsigned i = 0; i < n; ++i) {
    if (!parseScope<E, Facet>(p, child + i * S::Leaves::N, offset, scope, true, i + 1 == n))
      return false;
  }
  if (offset < p.input.length()) {
    Token next;
    if (p.error = scan(p.input, offset, next)) return false;
    // Count zero must not admit an entry either. A tag belonging to the
    // containing scope is valid after the exact advertised entry count.
    if (scope.contains(next.tag) && !parent.contains(next.tag) &&
	!parent.outside(next.tag))
      return p.fail(ErrorCode::Group, next.tag, offset);
  }
  token.next = offset;
  return true;
}

template <typename O, typename Facet>
bool parseScope(
    Parse &p, unsigned base, unsigned &offset, Scope &scope, bool entry, bool lastEntry,
    unsigned region) {
  using S = Schema<O, Facet>;
  bool started = false;
  int last = -1;
  unsigned length = p.input.length();
  while (offset < length) {
    Token token;
    if (p.error = scan(p.input, offset, token)) return false;
    int index = S::index().find(token.tag);
    if (index < 0) {
      if constexpr (S::Prefix) {
	if (!scope.parent || scope.outside(token.tag)) break;
	offset = token.next;
	continue;
      }
      if (entry && scope.outside(token.tag)) break;
      if (reserved(token.tag)) return p.fail(ErrorCode::Reserved, token.tag, offset);
      offset = token.next;
      continue;
    }
    if (entry) {
      if (!started) {
	if (scope.delimiter && scope.delimiter != token.tag)
	  return p.fail(ErrorCode::Group, token.tag, offset);
	scope.delimiter = token.tag;
	started = true;
      } else if (scope.delimiter == token.tag) return true;
    }
    unsigned field = unsigned(index) >> 2;
    if constexpr (S::Envelope) {
      unsigned next = S::region(field);
      if (next < region) return p.fail(ErrorCode::Envelope, token.tag, offset);
      region = next;
    }
    unsigned slot = base + field;
    if (p.positions[slot].offset != UINT_MAX) {
      if (entry && lastEntry && scope.outside(token.tag)) return true;
      return p.fail(ErrorCode::Duplicate, token.tag, offset);
    }
    if (entry && int(field) < last) {
      if (lastEntry && scope.outside(token.tag)) return true;
      return p.fail(ErrorCode::Group, token.tag, offset);
    }
    last = field;
    if (token.length > p.limits.fieldBytes)
      return p.fail(ErrorCode::Size, token.tag, token.offset);
    if (!(index & 3)) {
      p.positions[slot] = {token.offset, token.length, 0, 0};
      offset = token.next;
      continue;
    }
    if constexpr (S::Leaves::N) {
      ZuSwitch::dispatch<S::Leaves::N>(field,
	[&p, &token, &scope, slot, index]<typename I>(I) {
	  using F = typename ZuType<I{}, typename S::Leaves>::Field;
	  if (index & 1) {
	    unsigned n;
	    bool negative;
	    if (!integer<32, false>(
		{p.input.data() + token.offset, token.length}, n, negative)) {
	      p.fail(ErrorCode::Value, token.tag, token.offset); return;
	    }
	    p.error = scanData(p.input, token.next, tag<F>(), n, token);
	  } else if constexpr (lenTag<F>())
	    p.fail(ErrorCode::Pair, token.tag, token.offset);
	  if (p.error) return;
	  if (token.length > p.limits.fieldBytes) {
	    p.fail(ErrorCode::Size, token.tag, token.offset); return;
	  }
	  if constexpr (GroupField<F, Facet>{})
	    parseGroup<typename F::T, Facet>(p, slot, token, scope);
	  else p.positions[slot] = {token.offset, token.length, 0, 0};
	});
    }
    if (p.error) return false;
    offset = token.next;
  }
  if (entry && !started) return p.fail(ErrorCode::Group, 0, offset);
  return true;
}

template <typename Props, template <typename> class P, typename Default,
  bool = ZuFieldProp::HasType<Props, P>{}> struct FormatProp { using T = Default; };

template <typename Props, template <typename> class P, typename Default>
struct FormatProp<Props, P, Default, true> { using T = ZuFieldProp::GetType<Props, P>; };

template <typename Props, typename Facet = ZuFacet::FIX>
using NumberFmt = FormatProp<Props, ZuFieldProp::FIX::NumberFmt,
  typename Config<Facet>::NumberFmt>::T;
template <typename Props, typename Facet = ZuFacet::FIX>
using TimeFmt = FormatProp<Props, ZuFieldProp::FIX::TimeFmt,
  typename Config<Facet>::TimeFmt>::T;

template <unsigned Fmt> struct BytesCodec;
template <> struct BytesCodec<ZtBytesFmt::Base64> : ZuBase64 { };

template <> struct BytesCodec<ZtBytesFmt::Base64URL> : ZuBase64URL { };

template <> struct BytesCodec<ZtBytesFmt::Base32> : ZuBase32 { };

template <> struct BytesCodec<ZtBytesFmt::Hex> : ZuHex { };

// The scanner reports bytes written, not input consumed. Validate the alphabet
// and padding once before decoding directly into destination-owned storage.
template <typename Props> bool bytesValid(ZuCSpan value) {
  enum { Fmt = ZuFieldProp::FIX::GetBytesFmt<Props>{} };
  ZuAssert(Fmt <= ZtBytesFmt::Raw);
  if constexpr (Fmt == ZtBytesFmt::Raw) return true;
  else {
    unsigned n = value.length(), end = n;
    if constexpr (Fmt == ZtBytesFmt::Hex) {
      if (n & 1) return false;
    } else {
      while (end && value[end - 1] == '=') --end;
      unsigned padding = n - end;
      if constexpr (Fmt == ZtBytesFmt::Base32) {
	switch (end & 7) { case 1: case 3: case 6: return false; }
	if (padding && (padding > 6 || (n & 7))) return false;
      } else {
	if ((end & 3) == 1 || (padding && (padding > 2 || (n & 3)))) return false;
      }
      if (n && !end) return false;
    }
    for (unsigned i = 0; i < end; ++i)
      if (!BytesCodec<Fmt>::is(value[i])) return false;
    return true;
  }
}

template <unsigned Fmt> struct BytesPrint;
template <unsigned Fmt> struct BytesPrinter : ZuPrintBuffer {
  static unsigned length(const BytesPrint<Fmt> &v) { return v.size; }
  static unsigned print(char *out, unsigned n, const BytesPrint<Fmt> &v) {
    return BytesCodec<Fmt>::encode({reinterpret_cast<uint8_t *>(out), n}, v.bytes);
  }
};

template <unsigned Fmt> struct BytesPrint {
  ZuBSpan	bytes;
  unsigned	size;
  friend BytesPrinter<Fmt> ZuPrintType(BytesPrint *);
};

template <typename Props> uint64_t bytesLength(ZuBSpan value) {
  enum { Fmt = ZuFieldProp::FIX::GetBytesFmt<Props>{} };
  if (value.length() > UINT_MAX) return UINT64_MAX;
  if constexpr (Fmt == ZtBytesFmt::Raw) return value.length();
  else {
    if constexpr (Fmt == ZtBytesFmt::Hex) return value.length()<<1;
    else return BytesCodec<Fmt>::enclen(value.length());
  }
}

template <typename Props, typename Out>
void saveBytes(Out &out, ZuBSpan value, unsigned size) {
  enum { Fmt = ZuFieldProp::FIX::GetBytesFmt<Props>{} };
  if constexpr (Fmt == ZtBytesFmt::Raw) out << ZuCSpan{value};
  else out << BytesPrint<Fmt>{value, size};
}

template <typename T, typename = void> struct OwnBytes : ZuFalse { };
template <typename T>
struct OwnBytes<T, decltype(ZuDeclVal<T &>().ensure(unsigned{}), void())> : ZuTrue { };

template <typename Props, typename T> T loadBytes(ZuCSpan value, Error &error) {
  enum { Fmt = ZuFieldProp::FIX::GetBytesFmt<Props>{} };
  if (!bytesValid<Props>(value)) { error.code = ErrorCode::Value; return T{}; }
  if constexpr (Fmt == ZtBytesFmt::Raw) return T(value);
  else {
    // Immutable input cannot back a decoded borrowed span. Encoded fields use
    // owning destinations; raw FIX Data fields keep their borrowing semantics.
    ZuAssert((OwnBytes<T>{}));
    T out;
    unsigned n = BytesCodec<Fmt>::declen(value.length());
    out.length(n);
    if (n) out.length(BytesCodec<Fmt>::decode(ZuSpan<uint8_t>{out}, ZuBSpan{value}), false);
    return out;
  }
}

template <typename Fmt> struct TimePolicy {
  enum { Kind = Time::FIX, Unit = Time::Sec, NDP = -9 };
  using Print = Fmt;
  using Scan = ZuDateTimeScan::FIX;
};

template <> struct TimePolicy<ZuDateTimeFmt::ISO> {
  enum { Kind = Time::ISO };
  using Print = ZuDateTimeFmt::ISO;
  using Scan = ZuDateTimeScan::ISO;
};

template <> struct TimePolicy<ZuDateTimeFmt::CSV> {
  enum { Kind = Time::CSV };
  using Print = ZuDateTimeFmt::CSV;
  using Scan = ZuDateTimeScan::CSV;
};

template <unsigned Fmt, unsigned Unit_, int NDP_>
struct TimePolicy<Zfix::TimeFmt<Fmt, Unit_, NDP_>> {
  enum { Kind = Fmt, Unit = Unit_, NDP = NDP_ };
  using Print = ZuIf<Fmt == Time::FIX, ZuDateTimeFmt::FIX<NDP>,
    ZuIf<Fmt == Time::ISO, ZuDateTimeFmt::ISO, ZuDateTimeFmt::CSV>>;
  using Scan = ZuIf<Fmt == Time::FIX, ZuDateTimeScan::FIX,
    ZuIf<Fmt == Time::ISO, ZuDateTimeScan::ISO, ZuDateTimeScan::CSV>>;
};

template <typename Props, typename Facet, typename T>
T loadTime(ZuCSpan value, Error &error) {
  using P = TimePolicy<TimeFmt<Props, Facet>>;
  auto fail = [&error]() { error.code = ErrorCode::Value; return T{}; };
  if constexpr (P::Kind == Time::Unix) {
    if (!decimal(value)) return fail();
    ZuDecimal d;
    if (d.scan(value) != value.length() || !*d) return fail();
    auto time = ZuTime{d / ZuDecimal{int64_t(ZuDecimalFn::pow10_64(P::Unit * 3))}};
    if (!*time) return fail();
    if constexpr (ZuIsSame<T, ZuTime>{}) return time;
    else return ZuDateTime{time};
  } else {
    if constexpr (P::Kind == Time::FIX) if (!timestamp(value)) return fail();
    auto &fmt = ZmTLS<typename P::Scan, (int Props::*){}>();
    ZuDateTime v;
    if (v.scan(fmt, value) != value.length()) return fail();
    if constexpr (P::Kind == Time::FIX) {
      auto date = ZuBox<unsigned>::eov(ZuCSpan{value.data(), 8}).template p<1>().val();
      if (v.yyyymmdd() != date) return fail();
    }
    if constexpr (ZuIsSame<T, ZuTime>{}) return v.as_time();
    else return v;
  }
}

template <typename Props, typename Facet, typename Out, typename T>
void saveTime(Out &out, const T &value) {
  using P = TimePolicy<TimeFmt<Props, Facet>>;
  ZuDateTime date{value};
  if constexpr (P::Kind == Time::Unix) {
    if (!*date) return;
    auto time = date.as_time();
    if (time.sec() < 0) { out << '-'; time = -time; }
    constexpr uint64_t Scale = ZuDecimalFn::pow10_64(P::Unit * 3);
    constexpr uint64_t Divisor = 1000000000/Scale;
    // Scaled Unix timestamps can exceed 64 bits within the supported date range.
    using I = ZuIf<P::Unit == Time::Sec, int64_t, int128_t>;
    out << ZuBox<I>{I(time.sec()) * Scale + time.nsec()/Divisor};
    if constexpr (P::Unit < Time::NSec && P::NDP) {
      auto fraction = time.nsec()%Divisor;
      if (P::NDP > 0 || fraction)
	out << '.' << ZuBoxed(fraction).template fmt<ZuFmt::Frac<9 - P::Unit * 3,
	  (P::NDP < 0 ? -P::NDP : P::NDP), (P::NDP < 0 ? '\0' : '0')>>();
    }
  } else {
    auto &fmt = ZmTLS<typename P::Print, (int Props::*){}>();
    out << date.fmt(fmt);
  }
}

template <typename Fmt> ZuCSpan numberText(ZuCSpan value) {
  if constexpr (Fmt::Pad_ && Fmt::Pad_ != '0') {
    if constexpr (Fmt::Justification_ == ZuFmt::Just::Right)
      while (value && value[0] == Fmt::Pad_) value.offset(1);
    else if constexpr (Fmt::Justification_ == ZuFmt::Just::Left)
      while (value && value[value.length() - 1] == Fmt::Pad_) value.trunc(value.length() - 1);
  }
  return value;
}

template <typename Fmt> bool numberValid(ZuCSpan value) {
  if constexpr (!Fmt::Comma_) return decimal(value);
  else {
    bool point = false, digits = false;
    for (unsigned i = value && value[0] == '-' ? 1 : 0; i < value.length(); ++i) {
      auto c = value[i];
      if (c >= '0' && c <= '9') { digits = true; continue; }
      if (!point && c == Fmt::Comma_) continue;
      if (!point && c == '.') { point = true; continue; }
      return false;
    }
    return digits;
  }
}

template <typename Fmt> bool loadDecimal(ZuCSpan value, ZuDecimal &out) {
  value = numberText<Fmt>(value);
  if constexpr (!Fmt::Hex_ && !Fmt::Comma_ && Fmt::Justification_ == ZuFmt::Just::None) {
    if (!decimal(value)) return false;
    if (value[value.length() - 1] == '.') value.trunc(value.length() - 1);
    return out.scan(value) == value.length() && *out;
  } else {
    if (!value) return false;
    bool negative = value[0] == '-';
    if (negative) value.offset(1);
    if (!value) return false;
    auto point = static_cast<const char *>(memchr(value.data(), '.', value.length()));
    unsigned n = point ? unsigned(point - value.data()) : unsigned(value.length());
    uint64_t whole = 0;
    bool sign;
    if (n && !integer<64, false, uint64_t, Fmt>({value.data(), n}, whole, sign)) return false;
    if (!n && (!point || value.length() == 1)) return false;
    out = ZuDecimal{whole};
    if (point && value.length() > n + 1) {
      ZuCSpan fraction{point, value.length() - n};
      ZuDecimal part;
      if (!decimal(fraction) || part.scan(fraction) != fraction.length() || !*part) return false;
      out += part;
    }
    if (negative) out = -out;
    return bool(*out);
  }
}

template <typename Fn>
void multipleTokens(ZuCSpan value, Error &error, Fn fn) {
  unsigned begin = 0, n = value.length();
  while (begin < n && !error) {
    auto ptr = static_cast<const char *>(memchr(value.data() + begin, ' ', n - begin));
    unsigned end = ptr ? unsigned(ptr - value.data()) : n;
    if (begin == end) { error.code = ErrorCode::Value; return; }
    fn(ZuCSpan{value.data() + begin, end - begin});
    if (end == n) return;
    begin = end + 1;
    if (begin == n) error.code = ErrorCode::Value;
  }
}
template <unsigned Code, typename Props, typename T, typename Facet = ZuFacet::FIX>
T scalar(ZuCSpan value, Error &error);
template <typename T> void freeCStrings(const T &values) {
  using E = GroupElem<T>;
  if constexpr (ZuTraits<E>::IsPointer && ZuTraits<E>::IsPrimitive)
    for (unsigned i = 0, n = ZuTraits<T>::length(values); i < n; ++i)
      ::free(const_cast<char *>(values[i]));
}
template <unsigned Code, typename Props, typename Facet = ZuFacet::FIX>
void validateMultiple(ZuCSpan value, Error &error) {
  ZuAssert(Code != ZfFieldTC::UDT && !ZfFieldTC::IsVec<Code>{});
  multipleTokens(value, error, [&error](ZuCSpan token) {
    if constexpr (Code == ZfFieldTC::Bytes) {
      if (!text(token) || !bytesValid<Props>(token)) error.code = ErrorCode::Value;
    } else if constexpr (Code == ZfFieldTC::CString) {
      scalar<ZfFieldTC::String, Props, ZuCSpan, Facet>(token, error);
    } else scalar<Code, Props, ZfFieldTC::Type<Code>, Facet>(token, error);
  });
}
template <unsigned Code, typename Props, typename T, typename Facet = ZuFacet::FIX>
T loadMultiple(ZuCSpan value, Error &error) {
  ZuAssert(Code != ZfFieldTC::UDT && !ZfFieldTC::IsVec<Code>{});
  using E = GroupElem<T>;
  T out;
  multipleTokens(value, error, [&out, &error](ZuCSpan token) {
    if constexpr (Code == ZfFieldTC::Bytes)
      if (!text(token)) { error.code = ErrorCode::Value; return; }
    auto elem = scalar<Code, Props, E, Facet>(token, error);
    if (!error) out.push(ZuMv(elem));
  });
  if constexpr (Code == ZfFieldTC::CString) {
    if (error) { freeCStrings(out); out.length(0, false); }
  }
  return out;
}

template <unsigned Code, typename Props, typename T, typename Facet>
T scalar(ZuCSpan value, Error &error) {
  auto fail = [&error]() { error.code = ErrorCode::Value; return T{}; };
  if constexpr (ZfFieldTC::IsVec<Code>{}) {
    ZuAssert((ZuTypeIn<ZuFieldProp::FIX::Multiple, Props>{}));
    return loadMultiple<ZfFieldTC::Elem<Code>{}, Props, T, Facet>(value, error);
  } else if constexpr (Code == ZfFieldTC::CString) {
    if constexpr (ZuTypeIn<ZuFieldProp::FIX::Char, Props>{})
      if (value.length() != 1) return fail();
    if (!text(value)) return fail();
    if constexpr (ZuTraits<T>::IsPointer && ZuTraits<T>::IsPrimitive)
      return T(cstring(value, error));
    else return T(value);
  } else if constexpr (Code == ZfFieldTC::Bytes) {
    return loadBytes<Props, T>(value, error);
  } else if constexpr (Code == ZfFieldTC::String) {
    if constexpr (ZuTypeIn<ZuFieldProp::FIX::Char, Props>{})
      if (value.length() != 1 || !text(value)) return fail();
    if constexpr (Code == ZfFieldTC::String) if (!text(value)) return fail();
    return T(value);
  } else if constexpr (Code == ZfFieldTC::Bool) {
    if (value == "Y") return T(true);
    if (value == "N") return T(false);
    return fail();
  } else if constexpr (Code >= ZfFieldTC::Int8 && Code <= ZfFieldTC::UInt128) {
    using V = ZfFieldTC::Type<Code>;
    V v;
    if constexpr (ZuTypeIn<ZuFieldProp::FIX::Char, Props>{}) {
      if (value.length() != 1 || !text(value)) return fail();
      v = V(value[0]);
    } else if constexpr (ZuFieldProp::HasEnum<Props>{}) {
      if (!text(value)) return fail();
      auto n = ZuFieldProp::GetEnum<Props>::s2v(value);
      if (n < 0) return fail();
      v = V(n);
    } else if constexpr (ZuFieldProp::HasFlags<Props>{}) {
      if (!text(value)) return fail();
      using Map = ZuFieldProp::GetFlags<Props>;
      v = 0;
      multipleTokens(value, error, [&v, &error](ZuCSpan token) {
	auto bit = Map::s2v(token);
	if (unsigned(bit) >= Map::Keys::N || unsigned(bit) >= (sizeof(V)<<3)) {
	  error.code = ErrorCode::Value; return;
	}
	v |= V(V(1) << bit);
      });
      if (error) return T{};
    } else {
      Zu_ntoa::Unsigned<V> magnitude;
      bool negative;
      if (!integer<(sizeof(V)<<3), ZuTraits<V>::IsSigned, decltype(magnitude),
	NumberFmt<Props, Facet>>(value, magnitude, negative)) return fail();
      v = V(negative ? ~magnitude + 1 : magnitude);
    }
    if constexpr (ZuFieldProp::HasRange<Props>{})
      if (!ZfFieldLimit<Props>(v)) return fail();
    return T(v);
  } else if constexpr (Code == ZfFieldTC::Decimal || Code == ZfFieldTC::Fixed) {
    ZuDecimal d;
    if (!loadDecimal<NumberFmt<Props, Facet>>(value, d)) return fail();
    if constexpr (ZuFieldProp::HasRange<Props>{})
      if (!ZfFieldLimit<Props>(d)) return fail();
    if constexpr (Code == ZfFieldTC::Decimal) return d;
    else if constexpr (ZuFieldProp::HasNDP<Props>{})
      return ZuFixed{d, ZuFieldProp::GetNDP<Props>{}};
    else return ZuFixed{d};
  } else if constexpr (Code == ZfFieldTC::Float) {
    using Fmt = NumberFmt<Props, Facet>;
    value = numberText<Fmt>(value);
    if (!numberValid<Fmt>(value)) return fail();
    auto r = ZuBox<double>::template eov<Fmt>(value);
    if (r.template p<0>() < 0 || unsigned(r.template p<0>()) != value.length() ||
	!isfinite(r.template p<1>().val())) return fail();
    auto v = r.template p<1>().val();
    if constexpr (ZuFieldProp::HasRange<Props>{})
      if (!ZfFieldLimit<Props>(v)) return fail();
    return T(v);
  } else if constexpr (Code == ZfFieldTC::DateTime || Code == ZfFieldTC::Time) {
    auto v = loadTime<Props, Facet, T>(value, error);
    if constexpr (ZuFieldProp::HasRange<Props>{})
      if (!error && !ZfFieldLimit<Props>(v)) return fail();
    return v;
  } else if constexpr (Code == ZfFieldTC::UDT) {
    if constexpr (Multiple<As<T>>{}) {
      using A = As<T>;
      return loadMultiple<A::ElemCode, typename A::ElemProps, T, Facet>(value, error);
    } else {
      if (!text(value)) return fail();
      using Fmt = decltype(Zfix_StringFmt(ZuDeclVal<T *>()));
      return Fmt::template Handler<T>::load(value, error);
    }
  } else {
    ZuAssert(Code == ZfFieldTC::UDT); // no arbitrary repeated primitive tags
  }
}

template <typename O, typename Facet, template <typename> class Filter,
  typename Out> void saveObject(Out &out, const O &o);

template <typename Facet, unsigned Code, typename Props, typename Out, typename T>
void saveScalar(Out &out, const T &value);
template <typename Facet, unsigned Code, typename Props, typename Out, typename T>
void saveMultiple(Out &out, const T &value) {
  ZuAssert(Code != ZfFieldTC::UDT && !ZfFieldTC::IsVec<Code>{});
  unsigned n = ZuTraits<T>::length(value);
  for (unsigned i = 0; i < n && !out.failed(); ++i) {
    const auto &element = value[i];
    if (i) out << ' ';
    if constexpr (Code == ZfFieldTC::CString || Code == ZfFieldTC::String ||
	(Code == ZfFieldTC::Bytes &&
	 ZuFieldProp::FIX::GetBytesFmt<Props>{} == ZtBytesFmt::Raw)) {
      ZuCSpan span{element};
      if (!span.length() || memchr(span.data(), ' ', span.length())) {
	out.fail(ErrorCode::Value); return;
      }
    }
    unsigned before = out.length();
    saveScalar<Facet, Code, Props>(out, element);
    if (!out.failed() && out.length() == before) out.fail(ErrorCode::Value);
  }
}

template <typename Facet, unsigned Code, typename Props, typename Out, typename T>
void saveScalar(Out &out, const T &value) {
  if constexpr (ZfFieldTC::IsVec<Code>{}) {
    ZuAssert((ZuTypeIn<ZuFieldProp::FIX::Multiple, Props>{}));
    saveMultiple<Facet, ZfFieldTC::Elem<Code>{}, Props>(out, value);
  } else if constexpr (Code == ZfFieldTC::Bytes) {
    auto size = bytesLength<Props>(ZuBSpan{value});
    if (size > UINT_MAX) { out.fail(ErrorCode::Size); return; }
    saveBytes<Props>(out, ZuBSpan{value}, unsigned(size));
  } else if constexpr (Code == ZfFieldTC::CString || Code == ZfFieldTC::String) {
    ZuCSpan span{value};
    if constexpr (ZuTypeIn<ZuFieldProp::FIX::Char, Props>{})
      if (span.length() != 1 || !text(span)) { out.fail(ErrorCode::Value); return; }
    out << span;
  } else if constexpr (Code == ZfFieldTC::Bool) out << (value ? 'Y' : 'N');
  else if constexpr (Code >= ZfFieldTC::Int8 && Code <= ZfFieldTC::UInt128) {
    if constexpr (ZuTypeIn<ZuFieldProp::FIX::Char, Props>{}) out << char(value);
    else if constexpr (ZuFieldProp::HasFlags<Props>{})
      out << typename ZuFieldProp::GetFlags<Props>::Print(value, " ");
    else if constexpr (ZuFieldProp::HasEnum<Props>{})
      out << ZuFieldProp::GetEnum<Props>::v2s(value);
    else out << ZuBox<T>{value}.template fmt<NumberFmt<Props, Facet>>();
  } else if constexpr (Code == ZfFieldTC::DateTime || Code == ZfFieldTC::Time)
    saveTime<Props, Facet>(out, value);
  else if constexpr (Code == ZfFieldTC::Decimal || Code == ZfFieldTC::Fixed) {
    if (!*value) { out.fail(ErrorCode::Value); return; }
    if constexpr (ZuFieldProp::HasNDP<Props>{})
      out << value.template fp<ZuFieldProp::GetNDP<Props>{}, '\0', NumberFmt<Props, Facet>>();
    else out << value.template fmt<NumberFmt<Props, Facet>>();
  } else if constexpr (Code == ZfFieldTC::Float) {
    if (!isfinite(value)) { out.fail(ErrorCode::Value); return; }
    out << ZuBox<T>{value}.template fmt<NumberFmt<Props, Facet>>();
  }
  else if constexpr (Code == ZfFieldTC::UDT) {
    if constexpr (Multiple<As<T>>{}) {
      using A = As<T>;
      saveMultiple<Facet, A::ElemCode, typename A::ElemProps>(out, value);
    } else {
      using Fmt = decltype(Zfix_StringFmt(ZuDeclVal<T *>()));
      Fmt::template Handler<T>::save(out, value);
    }
  } else ZuAssert(Code == ZfFieldTC::UDT);
}

template <typename O, typename Facet, template <typename> class Filter,
  typename Out> void saveObject(Out &out, const O &o) {
  using S = Schema<O, Facet>;
  ZuUnroll::all<typename S::Fields>([&out, &o]<typename F>() {
    if constexpr (Filter<F>{}) {
      if constexpr (ZuFieldProp::FIX::GetOptional<typename F::Props>{}) {
	if constexpr (F::Type::Code != ZfFieldTC::Bool) {
	  const auto &value = F::get(o);
	  if constexpr (ZuFieldProp::HasEnum<typename F::Props>{}) {
	    if (value < 0) return;
	  } else if (ZuNull(value)) return;
	}
      }
      if (out.failed()) return;
      if constexpr (Component<F, Facet>{}) {
	using T = ComponentType<F, Facet>;
	if constexpr (UnionField<F, Facet>{}) {
	  const auto &u = F::get(o);
	  if (!u.template is<T>()) out.fail(ErrorCode::Unresolved);
	  else saveObject<T, Facet, Filter>(out, u.template p<T>());
	} else if constexpr (ObjPtr<typename F::T>{}) {
	  const auto &ptr = F::get(o);
	  if (ptr) saveObject<T, Facet, Filter>(out, *ptr);
	  else if constexpr (ZuTypeIn<ZuFieldProp::Required, typename F::Props>{})
	    out.fail(ErrorCode::Required);
	} else saveObject<T, Facet, Filter>(out, F::get(o));
      } else if constexpr (GroupField<F, Facet>{}) {
	using T = F::T;
	using E = GroupElem<T>;
	ZuAssert((Format<T, Facet>::ElemCode == ZfFieldTC::UDT));
	ZuAssert((Format<E, Facet>::Kind == Kind::Object));
	const auto &values = F::get(o);
	uint64_t n = ZuTraits<T>::length(values);
	if (!out.group(n)) return;
	out.field(tag<F>());
	out << ZuBox<uint64_t>{n};
	out.end();
	unsigned delimiter = 0;
	for (uint64_t i = 0; i < n && !out.failed(); ++i) {
	  auto before = out.length();
	  saveObject<E, Facet, Filter>(out, values[i]);
	  out.entry(before, delimiter);
	}
	out.endGroup();
      } else {
	if constexpr (F::Type::Code == ZfFieldTC::UDT)
	  ZuAssert((Format<typename F::T, Facet>::Kind == Kind::String));
	const auto &value = F::get(o);
	if constexpr (F::Type::Code == ZfFieldTC::Bytes) {
	  auto bytes = ZuBSpan{value};
	  auto size = bytesLength<typename F::Props>(bytes);
	  if (size > UINT_MAX) { out.fail(ErrorCode::Size); return; }
	  if constexpr (lenTag<F>()) {
	    out.field(lenTag<F>()); out << ZuBox<unsigned>{unsigned(size)}; out.end();
	  }
	  out.field(tag<F>(), lenTag<F>() != 0);
	  saveBytes<typename F::Props>(out, bytes, unsigned(size));
	} else {
	  out.field(tag<F>());
	  saveScalar<Facet, F::Type::Code, typename F::Props>(out, value);
	}
	out.end();
      }
    }
  });
}

template <typename O, typename Facet> class ObjectView {
  using S = Schema<O, Facet>;
  using Fields = S::Fields;
  template <typename F> using CtorIndex = ZuFieldProp::GetCtor<typename F::Props>;
  using CtorFields = ZuTypeSort<CtorIndex, ZuTypeGrep<ZfFieldFilter::Ctor, Fields>>;

  template <typename F> bool present() const {
    constexpr unsigned I = S::template offset<F>();
    if constexpr (Component<F, Facet>{}) {
      for (unsigned j = I, n = I + FlattenField<F, Facet, ZuTypeList<>>::T::N;
	  j < n; ++j) if (m_positions[m_base + j].offset != UINT_MAX) return true;
      return false;
    } else return m_positions[m_base + I].offset != UINT_MAX;
  }

  template <typename F> auto field() const {
    using T = F::T;
    constexpr unsigned I = S::template offset<F>();
    if constexpr (Component<F, Facet>{}) {
      ObjectView<ComponentType<F, Facet>, Facet> child{
	m_input, m_positions, m_base + I, m_error};
      if constexpr (ObjPtr<T>{}) {
	if (!present<F>()) return T{};
	return T{child.alloc()};
      } else return T{child.ctor()};
    } else if constexpr (GroupField<F, Facet>{}) {
      using E = GroupElem<T>;
      const auto &p = m_positions[m_base + I];
      T out;
      if (p.offset == UINT_MAX) return out;
      out.ensure(p.count);
      for (unsigned j = 0; j < p.count && !m_error; ++j) {
	ObjectView<E, Facet> child{m_input, m_positions,
	  p.child + j * Schema<E, Facet>::Leaves::N, m_error};
	new (out.data() + j) E(child.ctor());
	out.length(j + 1, false);
      }
      return out;
    } else {
      const auto &p = m_positions[m_base + I];
      if constexpr (F::Type::Code == ZfFieldTC::UDT)
	ZuAssert((Format<T, Facet>::Kind == Kind::String));
      if (p.offset == UINT_MAX) {
	if constexpr (F::Type::Code == ZfFieldTC::CString) {
	  auto ptr = F::deflt();
	  if (!ptr) return T{};
	  return T(cstring(ZuCSpan{ptr}, m_error));
	} else if constexpr (F::Type::Code == ZfFieldTC::CStringVec) {
	  using E = GroupElem<T>;
	  T out;
	  auto defaults = F::deflt();
	  for (unsigned j = 0, n = ZuTraits<decltype(defaults)>::length(defaults);
	      j < n && !m_error; ++j) {
	    const char *ptr = defaults[j];
	    if constexpr (ZuTraits<E>::IsPointer && ZuTraits<E>::IsPrimitive)
	      out.push(ptr ? E(cstring(ZuCSpan{ptr}, m_error)) : E{});
	    else out.push(ptr ? E(ptr) : E{});
	  }
	  if (m_error) { freeCStrings(out); out.length(0, false); }
	  return out;
	} else if constexpr (ZuIsSame<decltype(F::deflt()), void>{}) return T{};
	else return T(F::deflt());
      }
      Error error;
      auto value = scalar<F::Type::Code, typename F::Props, T, Facet>(
	{m_input.data() + p.offset, p.length}, error);
      if (error && !m_error) m_error = {error.code, tag<F>(), p.offset};
      return value;
    }
  }

  template <typename List> void required() const {
    ZuUnroll::all<List>([this]<typename F>() {
      if constexpr (ZuTypeIn<ZuFieldProp::Required, typename F::Props>{})
	if (!m_error && !present<F>())
	  m_error = {ErrorCode::Required, missingTag<F>(), 0};
    });
  }

  template <typename F> static consteval unsigned missingTag() {
    if constexpr (Component<F, Facet>{}) return 0;
    else return tag<F>();
  }

  template <typename ...F> struct Construct {
    template <typename ...Args>
    static O ctor(const ObjectView &v, Args &&...args) {
      return O{ZuFwd<Args>(args)..., v.template field<F>()...};
    }

    template <typename ...Args>
    static O *alloc(const ObjectView &v, Args &&...args) {
      return new O{ZuFwd<Args>(args)..., v.template field<F>()...};
    }

    template <typename ...Args>
    static O *new_(void *ptr, const ObjectView &v, Args &&...args) {
      return new (ptr) O{ZuFwd<Args>(args)..., v.template field<F>()...};
    }
  };

public:
  ObjectView(ZuCSpan input, const Positions &positions, unsigned base, Error &error) :
    m_input{input}, m_positions{positions}, m_base{base}, m_error{error} { }

  template <typename F> auto get() const {
    ZuAssert((ZuTypeIn<F, Fields>{}));
    required<ZuTypeList<F>>();
    return field<F>();
  }

  template <typename F> ZuCSpan raw() const {
    ZuAssert((ZuTypeIn<F, Fields>{}));
    ZuAssert((!Component<F, Facet>{} && !GroupField<F, Facet>{}));
    if (m_error) return {};
    required<ZuTypeList<F>>();
    validateField<F>();
    if (m_error) return {};
    constexpr unsigned I = S::template offset<F>();
    const auto &p = m_positions[m_base + I];
    if (p.offset == UINT_MAX) return {};
    return {m_input.data() + p.offset, p.length};
  }

  template <typename F> void validateField() const {
    constexpr unsigned I = S::template offset<F>();
    if constexpr (Component<F, Facet>{}) {
      ObjectView<ComponentType<F, Facet>, Facet> child{
	m_input, m_positions, m_base + I, m_error};
      child.validate();
    } else {
      const auto &p = m_positions[m_base + I];
      if (p.offset == UINT_MAX || m_error) return;
      if constexpr (GroupField<F, Facet>{}) {
	using E = GroupElem<typename F::T>;
	for (unsigned j = 0; j < p.count && !m_error; ++j) {
	  ObjectView<E, Facet> child{m_input, m_positions,
	    p.child + j * Schema<E, Facet>::Leaves::N, m_error};
	  child.validate();
	}
      } else {
	Error error;
	if constexpr (ZfFieldTC::IsVec<F::Type::Code>{}) {
	  using P = F::Props;
	  ZuAssert((ZuTypeIn<ZuFieldProp::FIX::Multiple, P>{}));
	  validateMultiple<ZfFieldTC::Elem<F::Type::Code>{}, P, Facet>(
	    {m_input.data() + p.offset, p.length}, error);
	} else if constexpr (F::Type::Code == ZfFieldTC::Bytes) {
	  if (!bytesValid<typename F::Props>({m_input.data() + p.offset, p.length}))
	    error.code = ErrorCode::Value;
	} else if constexpr (F::Type::Code == ZfFieldTC::CString) {
	  ZuCSpan value{m_input.data() + p.offset, p.length};
	  scalar<ZfFieldTC::String, typename F::Props, ZuCSpan, Facet>(value, error);
	} else if constexpr (F::Type::Code == ZfFieldTC::UDT) {
	  using T = F::T;
	  ZuAssert((Format<T, Facet>::Kind == Kind::String));
	  if constexpr (Multiple<As<T>>{}) {
	    using A = As<T>;
	    validateMultiple<A::ElemCode, typename A::ElemProps, Facet>(
	      {m_input.data() + p.offset, p.length}, error);
	  } else scalar<F::Type::Code, typename F::Props, T, Facet>(
	    {m_input.data() + p.offset, p.length}, error);
	} else {
	  using T = ZfFieldTC::Type<F::Type::Code>;
	  scalar<F::Type::Code, typename F::Props, T, Facet>(
	    {m_input.data() + p.offset, p.length}, error);
	}
	if (error) m_error = {error.code, tag<F>(), p.offset};
      }
    }
  }

  void validate() const {
    ZuUnroll::all<Fields>([this]<typename F>() {
      if (!m_error) validateField<F>();
    });
  }

  // Validate a complete selected representation without constructing O or
  // calling its getters/setters. Optional absent pointer components stay absent.
  template <template <typename> class Filter = ZfFieldFilter::Save>
  void check() const {
    required<ZuTypeGrep<Filter, Fields>>();
    ZuUnroll::all<Fields>([this]<typename F>() {
      if (m_error) return;
      constexpr unsigned I = S::template offset<F>();
      if constexpr (Component<F, Facet>{} && Filter<F>{}) {
	if constexpr (ObjPtr<typename F::T>{}) if (!present<F>()) return;
	ObjectView<ComponentType<F, Facet>, Facet> child{
	  m_input, m_positions, m_base + I, m_error};
	child.template check<Filter>();
      } else if constexpr (GroupField<F, Facet>{} && Filter<F>{}) {
	const auto &p = m_positions[m_base + I];
	if (p.offset == UINT_MAX) return;
	using E = GroupElem<typename F::T>;
	for (unsigned j = 0; j < p.count && !m_error; ++j) {
	  ObjectView<E, Facet> child{m_input, m_positions,
	    p.child + j * Schema<E, Facet>::Leaves::N, m_error};
	  child.template check<Filter>();
	}
      } else validateField<F>();
    });
  }

  template <typename F, bool Update> void component(O &o) const {
    using T = F::T;
    using E = ComponentType<F, Facet>;
    constexpr unsigned I = S::template offset<F>();
    ObjectView<E, Facet> child{m_input, m_positions, m_base + I, m_error};
    auto apply = [&child]<typename V>(V &value, bool has) {
      if constexpr (ObjPtr<T>{}) {
	if (!has) {
	  if constexpr (ZuTraits<T>::IsPrimitive) delete value;
	  value = T{}; return;
	}
	if (!value) { value = T{child.alloc()}; return; }
	if constexpr (Update) child.update(*value);
	else child.load(*value);
      } else if constexpr (UnionField<F, Facet>{}) {
	if (!value.template is<E>()) {
	  value.template p<E>(child.ctor());
	  return;
	}
	if constexpr (Update) child.update(value.template p<E>());
	else child.load(value.template p<E>());
      } else {
	if constexpr (Update) child.update(value);
	else child.load(value);
      }
    };
    using R = decltype(F::get(o));
    if constexpr (ZuIsLRef<R>{} && !ZuIsCRef<R>{})
      apply(F::get(o), ObjPtr<T>{} ? present<F>() : true);
    else {
      // A value/const getter requires the declared setter for writeback.
      T value{F::get(o)};
      apply(value, ObjPtr<T>{} ? present<F>() : true);
      F::set(o, ZuMv(value));
    }
  }

  template <typename List, bool Update = false, bool Init = false>
  bool apply(O &o) const {
    required<List>();
    ZuUnroll::all<Fields>([this, &o]<typename F>() {
      if constexpr (!F::ReadOnly && (ZuTypeIn<F, List>{} ||
	(Update && Component<F, Facet>{}))) {
	if (m_error) return;
	if constexpr (Component<F, Facet>{}) component<F, Update>(o);
	else {
	  auto value = field<F>();
	  if (!m_error) {
	    if constexpr (F::Type::Code == ZfFieldTC::CString)
	      ::free(const_cast<char *>(F::get(o)));
	    else if constexpr (F::Type::Code == ZfFieldTC::CStringVec)
	      freeCStrings(F::get(o));
	    F::set(o, ZuMv(value));
	  }
	}
      } else if constexpr (!(Init && ZuTypeIn<F, CtorFields>{})) {
	if (!m_error) validateField<F>();
      }
    });
    return !m_error;
  }

  template <typename ...Args> O ctor(Args &&...args) const {
    required<CtorFields>();
    using InitFields = ZuTypeGrep<ZfFieldFilter::Init, Fields>;
    if constexpr (!InitFields::N) {
      ZuUnroll::all<Fields>([this]<typename F>() {
	if constexpr (!ZuTypeIn<F, CtorFields>{})
	  if (!m_error) validateField<F>();
      });
      return ZuTypeApply<Construct, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
    } else {
      auto o = ZuTypeApply<Construct, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
      apply<InitFields, false, true>(o);
      return o;
    }
  }

  template <typename ...Args> O *alloc(Args &&...args) const {
    required<CtorFields>();
    if (m_error) return nullptr;
    auto o = ZuTypeApply<Construct, CtorFields>::alloc(*this, ZuFwd<Args>(args)...);
    apply<ZuTypeGrep<ZfFieldFilter::Init, Fields>, false, true>(*o);
    if (m_error) { delete o; return nullptr; }
    return o;
  }

  template <typename ...Args> bool new_(void *ptr, Args &&...args) const {
    required<CtorFields>();
    if (m_error) return false;
    auto o = ZuTypeApply<Construct, CtorFields>::new_(ptr, *this, ZuFwd<Args>(args)...);
    apply<ZuTypeGrep<ZfFieldFilter::Init, Fields>, false, true>(*o);
    if (m_error) { o->~O(); return false; }
    return true;
  }

  bool load(O &o) const { return apply<ZuTypeGrep<ZfFieldFilter::Load, Fields>>(o); }
  bool update(O &o) const { return apply<ZuTypeGrep<ZfFieldFilter::Upd, Fields>, true>(o); }

private:
  ZuCSpan		m_input;
  const Positions	&m_positions;
  unsigned		m_base;
  Error			&m_error;
};

template <typename O, typename Facet = ZuFacet::FIX,
  unsigned Kind = Format<O, Facet>::Kind> class Handler;

template <typename O, typename Facet> class Handler<O, Facet, Kind::Object> {
  using S = Schema<O, Facet>;

public:
  explicit Handler(ZuCSpan input, Limits limits = {}) : m_input{input} {
    // Builtin root slots exist even on an early limit failure: ctor() must
    // still be able to supply absent/default constructor arguments safely.
    m_positions.length(S::Leaves::N, false);
    for (unsigned i = 0; i < S::Leaves::N; ++i) m_positions[i].offset = UINT_MAX;
    if (input.length() > limits.frameBytes ||
	S::Leaves::N > limits.parseBytes/sizeof(Position)) {
      m_error = {ErrorCode::Size}; return;
    }
    Parse p{input, limits, m_positions, m_error};
    unsigned offset = 0;
    Scope scope{nullptr, scopeContains<O, Facet>, 0, 0};
    parseScope<O, Facet>(p, 0, offset, scope, false);
  }

  Handler(const Handler &) = delete;
  Handler &operator =(const Handler &) = delete;

  bool valid() const { return !m_error; }
  const Error &error() const { return m_error; }

  template <template <typename> class Filter = ZfFieldFilter::Save>
  bool check() { if (!m_error) view().template check<Filter>(); return !m_error; }

  template <typename F> ZuCSpan raw() { return view().template raw<F>(); }

  template <typename ...Args> O ctor(Args &&...args) {
    return view().ctor(ZuFwd<Args>(args)...);
  }

  template <typename ...Args> O *alloc(Args &&...args) {
    if (m_error) return nullptr;
    return view().alloc(ZuFwd<Args>(args)...);
  }

  template <typename ...Args> bool new_(void *ptr, Args &&...args) {
    if (m_error) return false;
    return view().new_(ptr, ZuFwd<Args>(args)...);
  }

  bool load(O &o) { return !m_error && view().load(o); }
  bool update(O &o) { return !m_error && view().update(o); }

  template <template <typename> class Filter = ZfFieldFilter::Save, typename Out>
  static bool save(Out &out, const O &o) {
    saveObject<O, Facet, Filter>(out, o);
    return !out.failed();
  }

private:
  auto view() { return ObjectView<O, Facet>{m_input, m_positions, 0, m_error}; }

  using Slots = ZtBuiltin<Positions, S::Leaves::N>;
  ZuCSpan	m_input;
  Slots		m_positions;
  Error		m_error;
};

template <typename O, typename Facet> class Handler<O, Facet, Kind::Union> {
public:
  explicit Handler(ZuCSpan input, Limits limits = {}) : m_view{input, limits} {
    if (input.length() > limits.frameBytes) m_error = {ErrorCode::Size};
  }

  bool valid() const { return !m_error; }
  const Error &error() const { return m_error; }
  ParseView view() const { return m_view; }

  O ctor() {
    if (m_error) return O{};
    if constexpr (typename O::template In<ParseView>{}) return O(m_view);
    else { m_error = {ErrorCode::Unresolved}; return O{}; }
  }

  O *alloc() {
    if (m_error) return nullptr;
    if constexpr (typename O::template In<ParseView>{}) return new O(m_view);
    else { m_error = {ErrorCode::Unresolved}; return nullptr; }
  }

  bool new_(void *ptr) {
    if (m_error) return false;
    if constexpr (typename O::template In<ParseView>{}) { new (ptr) O(m_view); return true; }
    else { m_error = {ErrorCode::Unresolved}; return false; }
  }

  bool load(O &o) {
    if (m_error) return false;
    if constexpr (typename O::template In<ParseView>{}) { o = m_view; return true; }
    else { m_error = {ErrorCode::Unresolved}; return false; }
  }

  bool update(O &o) { return load(o); }

  template <typename A> bool resolve(O &o) {
    ZuAssert((typename O::template In<A>{}));
    if (m_error) return false;
    Handler<A, Facet> h{m_view.input, m_view.limits};
    if (!h.valid()) { m_error = h.error(); return false; }
    o.template p<A>(h.ctor());
    m_error = h.error();
    return !m_error;
  }

  template <template <typename> class Filter = ZfFieldFilter::Save, typename Out>
  static bool save(Out &out, const O &o) {
    ZuSwitch::dispatch<O::N>(o.type(), [&out, &o](auto I) {
      using A = typename O::template Type<I>;
      if constexpr (ZuIsSame<A, void>{} || ZuIsSame<A, ParseView>{}) {
	using Fmt = decltype(Zfix_StringFmt(ZuDeclVal<O *>()));
	if constexpr (!ZuIsSame<Fmt, AsStringDeflt>{})
	  Fmt::template Handler<O>::save(out, o);
	else out.fail(ErrorCode::Unresolved);
      } else if constexpr (Format<A, Facet>::Kind == Kind::Object ||
	Format<A, Facet>::Kind == Kind::Union)
	Handler<A, Facet>::template save<Filter>(out, o.template p<I>());
      else out.fail(ErrorCode::Unresolved);
    });
    return !out.failed();
  }

private:
  ParseView	m_view;
  Error		m_error;
};

template <typename A, typename Facet = ZuFacet::FIX, typename O>
Error resolve(O &o) {
  ZuAssert((typename O::template In<ParseView>{}));
  if (!o.template is<ParseView>()) return {ErrorCode::Unresolved};
  auto view = o.template p<ParseView>();
  Handler<O, Facet> h{view.input, view.limits};
  h.template resolve<A>(o);
  return h.error();
}

template <typename O, typename Facet = ZuFacet::FIX>
auto handler(ZuCSpan input, Limits limits = {}) { return Handler<O, Facet>{input, limits}; }
template <typename Facet = ZuFacet::FIX, typename O>
Error load(O &o, ZuCSpan input, Limits limits = {}) {
  Handler<O, Facet> h{input, limits}; h.load(o); return h.error();
}
template <typename Facet = ZuFacet::FIX, typename O>
Error update(O &o, ZuCSpan input, Limits limits = {}) {
  Handler<O, Facet> h{input, limits}; h.update(o); return h.error();
}

template <typename Facet = ZuFacet::FIX,
  template <typename> class Filter = ZfFieldFilter::Save, typename Out, typename O>
bool save(Out &out, const O &o) {
  return Handler<O, Facet>::template save<Filter>(out, o);
}
} // Zfix::Fmt

#endif /* ZfixFmt_HH */
