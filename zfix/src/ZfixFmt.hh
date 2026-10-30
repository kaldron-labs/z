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
#include <zlib/ZuCmp.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZuFixed.hh>
#include <zlib/ZuIntrin.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmLHash.hh>
#include <zlib/ZmStackAvail.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtFmt.hh>

#include <zlib/ZfStruct.hh>

ZuStructFacet(FIX);

namespace ZuFieldProp::FIX {

  template <unsigned N> struct ID { ZuAssert(N > 0); };

  template <unsigned N> struct Len { ZuAssert(N > 0); };

  struct Char { };

  struct Multiple { };

  template <typename Fmt> struct NumberFmt { using T = Fmt; };

  template <typename Fmt> struct TimeFmt { using T = Fmt; };

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
  using TimeFmt = ZuDateTimeFmt::FIXDeflt;
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
  unsigned	selectTag = 0;
  Position	*selected = nullptr;

  bool select(unsigned tag, unsigned offset, unsigned length) {
    if (tag != selectTag || !selected) return true;
    if (selected->offset != UINT_MAX) return fail(ErrorCode::Duplicate, tag, offset);
    if (length > limits.fieldBytes) return fail(ErrorCode::Size, tag, offset);
    *selected = {offset, length, 0, 0}; return true;
  }

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
// Validate before narrowing. The accumulator and bounds follow the compile-time
// wire width; ordinary 32/64-bit fields do not pay for 128-bit arithmetic.
template <unsigned Bits, bool Sign>
bool integer(ZuCSpan value, uint128_t &magnitude, bool &negative) {
  ZuAssert(Bits && Bits <= 128);
  using U = ZuIf<(Bits <= (sizeof(unsigned)<<3)), unsigned,
    ZuIf<(Bits <= 64), uint64_t, uint128_t>>;
  constexpr U max = (~U(0) >> ((sizeof(U)<<3) - Bits)) >> unsigned(Sign);
  constexpr U min = max + unsigned(Sign);
  unsigned i = 0, n = value.length();
  if (!n) return false;
  negative = value[0] == '-';
  if (negative) {
    if constexpr (!Sign) return false;
    if (n == 1) return false;
    ++i;
  }
  U quotient = negative ? min/10 : max/10;
  unsigned remainder = negative ? unsigned(min%10) : unsigned(max%10);
  U v = 0;
  for (; i < n; ++i) {
    unsigned c = unsigned(value[i]);
    if (c < '0' || c > '9') return false;
    unsigned digit = c - '0';
    if (v > quotient || (v == quotient && digit > remainder)) return false;
    v = v * 10 + digit;
  }
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
    ZuAssert(F::Type::Code != ZfFieldTC::Bytes);
    return 0;
  }
}

template <typename Leaves>
struct TagIndex : ZmLHashKV<unsigned, unsigned,
  ZmLHashStatic<(Leaves::N > 1 ? ZuIntrin::log2(Leaves::N) + 2 : 2),
    ZmLHashLocal<>>> {
  using Base = ZmLHashKV<unsigned, unsigned,
    ZmLHashStatic<(Leaves::N > 1 ? ZuIntrin::log2(Leaves::N) + 2 : 2),
      ZmLHashLocal<>>>;

  TagIndex() {
    ZuUnroll::all<Leaves>([this]<typename L>() {
      using F = L::Field;
      constexpr unsigned i = ZuTypeIndex<L, Leaves>{};
      this->add(tag<F>(), i << 1);
      if constexpr (lenTag<F>()) this->add(lenTag<F>(), (i << 1) | 1);
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
  enum { Envelope = Envelope_, Prefix = false, Ordered = true, SelectTag = 0 };

  static unsigned region(unsigned) { return 1; }

  using Leaves = Flatten<Fields, Facet, ZuTypeList<>>::T;

  static consteval bool unique() {
    bool ok = true;
    ZuUnroll::all<Leaves>([&ok]<typename L>() {
      using F = L::Field;
      constexpr unsigned n = tag<F>(), len = lenTag<F>();
      if constexpr (ZfFieldTC::IsVec<F::Type::Code>{})
	if (!ZuTypeIn<ZuFieldProp::FIX::Multiple, typename F::Props>{}) ok = false;
      if (!n || (!Envelope_ && reserved(n)) ||
	  (len && (len == n || (!Envelope_ && reserved(len))))) ok = false;
      ZuUnroll::all<Leaves>([&ok]<typename R>() {
	if constexpr (ZuTypeIndex<L, Leaves>{} < ZuTypeIndex<R, Leaves>{}) {
	  using G = R::Field;
	  if (n == tag<G>() || (len && len == tag<G>()) ||
	      (lenTag<G>() && (n == lenTag<G>() || len == lenTag<G>())))
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
  static const auto &index() { static const TagIndex<Leaves> map; return map; }
};

template <typename O, typename Facet>
struct Schema : Schema_<O, Facet, ZuActiveFields<O, Facet>> { };

template <typename O, typename Facet, unsigned Select = Schema<O, Facet>::SelectTag>
bool parseScope(Parse &, unsigned, unsigned &, Scope &, bool, bool = true);

template <typename O, typename Facet> bool scopeContains(unsigned tag) {
  return Schema<O, Facet>::index().find(tag) >= 0;
}
template <typename O, typename Facet, unsigned Select>
bool rootContains(unsigned tag) {
  if constexpr (Select) if (tag == Select) return true;
  return scopeContains<O, Facet>(tag);
}

template <typename O, typename Facet>
bool parseGroup(Parse &p, unsigned slot, Token &token, Scope &parent) {
  using E = GroupElem<O>;
  using S = Schema<E, Facet>;
  ZuAssert((Format<O, Facet>::ElemCode == ZfFieldTC::UDT));
  ZuAssert((Format<E, Facet>::Kind == Kind::Object));
  ZuAssert(S::Leaves::N > 0);
  uint128_t count;
  bool negative;
  if (!integer<32, false>({p.input.data() + token.offset, token.length}, count, negative))
    return p.fail(ErrorCode::Value, token.tag, token.offset);
  unsigned n = unsigned(count);
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

template <typename O, typename Facet, unsigned Select>
bool parseScope(
    Parse &p, unsigned base, unsigned &offset, Scope &scope, bool entry, bool lastEntry) {
  using S = Schema<O, Facet>;
  bool started = false;
  int last = -1;
  unsigned region = 0;
  unsigned length = p.input.length();
  while (offset < length) {
    Token token;
    if (p.error = scan(p.input, offset, token)) return false;
    if constexpr (Select)
      if (!scope.parent && !p.select(token.tag, token.offset, token.length)) return false;
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
    unsigned field = unsigned(index) >> 1;
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
    if constexpr (S::Ordered) {
      if (entry && int(field) < last) {
	if (lastEntry && scope.outside(token.tag)) return true;
	return p.fail(ErrorCode::Group, token.tag, offset);
      }
    }
    last = field;
    if constexpr (S::Leaves::N) {
      ZuSwitch::dispatch<S::Leaves::N>(field,
	[&p, &token, &scope, slot, index]<typename I>(I) {
	  using F = typename ZuType<I{}, typename S::Leaves>::Field;
	  if (token.length > p.limits.fieldBytes) {
	    p.fail(ErrorCode::Size, token.tag, token.offset); return;
	  }
	  if (index & 1) {
	    uint128_t n;
	    bool negative;
	    if (!integer<32, false>(
		{p.input.data() + token.offset, token.length}, n, negative)) {
	      p.fail(ErrorCode::Value, token.tag, token.offset); return;
	    }
	    p.error = scanData(p.input, token.next, tag<F>(), unsigned(n), token);
	    if constexpr (Select)
	      if (!p.error && !scope.parent) p.select(token.tag, token.offset, token.length);
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
template <unsigned Code, typename Props, typename T>
T scalar(ZuCSpan value, Error &error);
template <typename T> void freeCStrings(const T &values) {
  using E = GroupElem<T>;
  if constexpr (ZuTraits<E>::IsPointer && ZuTraits<E>::IsPrimitive)
    for (unsigned i = 0, n = ZuTraits<T>::length(values); i < n; ++i)
      ::free(const_cast<char *>(values[i]));
}
template <unsigned Code, typename Props>
void validateMultiple(ZuCSpan value, Error &error) {
  ZuAssert(Code != ZfFieldTC::UDT && !ZfFieldTC::IsVec<Code>{});
  multipleTokens(value, error, [&error](ZuCSpan token) {
    if constexpr (Code == ZfFieldTC::CString || Code == ZfFieldTC::Bytes) {
      scalar<ZfFieldTC::String, Props, ZuCSpan>(token, error);
    } else scalar<Code, Props, ZfFieldTC::Type<Code>>(token, error);
  });
}
template <unsigned Code, typename Props, typename T>
T loadMultiple(ZuCSpan value, Error &error) {
  ZuAssert(Code != ZfFieldTC::UDT && !ZfFieldTC::IsVec<Code>{});
  using E = GroupElem<T>;
  T out;
  multipleTokens(value, error, [&out, &error](ZuCSpan token) {
    if constexpr (Code == ZfFieldTC::Bytes)
      if (!text(token)) { error.code = ErrorCode::Value; return; }
    auto elem = scalar<Code, Props, E>(token, error);
    if (!error) out.push(ZuMv(elem));
  });
  if constexpr (Code == ZfFieldTC::CString) {
    if (error) { freeCStrings(out); out.length(0, false); }
  }
  return out;
}

template <unsigned Code, typename Props, typename T>
T scalar(ZuCSpan value, Error &error) {
  auto fail = [&error]() { error.code = ErrorCode::Value; return T{}; };
  if constexpr (ZfFieldTC::IsVec<Code>{}) {
    ZuAssert((ZuTypeIn<ZuFieldProp::FIX::Multiple, Props>{}));
    return loadMultiple<ZfFieldTC::Elem<Code>{}, Props, T>(value, error);
  } else if constexpr (Code == ZfFieldTC::CString) {
    if constexpr (ZuTypeIn<ZuFieldProp::FIX::Char, Props>{})
      if (value.length() != 1) return fail();
    if (!text(value)) return fail();
    if constexpr (ZuTraits<T>::IsPointer && ZuTraits<T>::IsPrimitive)
      return T(cstring(value, error));
    else return T(value);
  } else if constexpr (Code == ZfFieldTC::String || Code == ZfFieldTC::Bytes) {
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
      uint128_t magnitude;
      bool negative;
      if (!integer<(sizeof(V)<<3), ZuTraits<V>::IsSigned>(value,
	magnitude, negative)) return fail();
      v = V(negative ? ~magnitude + 1 : magnitude);
    }
    if constexpr (ZuFieldProp::HasRange<Props>{})
      if (!ZfFieldLimit<Props>(v)) return fail();
    return T(v);
  } else if constexpr (Code == ZfFieldTC::Decimal || Code == ZfFieldTC::Fixed) {
    if (!decimal(value)) return fail();
    if (value[value.length() - 1] == '.') value.trunc(value.length() - 1);
    ZuDecimal d;
    if (d.scan(value) != value.length() || !*d) return fail();
    if constexpr (ZuFieldProp::HasRange<Props>{})
      if (!ZfFieldLimit<Props>(d)) return fail();
    if constexpr (Code == ZfFieldTC::Decimal) return d;
    else if constexpr (ZuFieldProp::HasNDP<Props>{})
      return ZuFixed{d, ZuFieldProp::GetNDP<Props>{}};
    else return ZuFixed{d};
  } else if constexpr (Code == ZfFieldTC::Float) {
    if (!decimal(value)) return fail();
    auto r = ZuBox<double>::eov(value);
    if (r.template p<0>() < 0 || unsigned(r.template p<0>()) != value.length() ||
	!isfinite(r.template p<1>().val())) return fail();
    auto v = r.template p<1>().val();
    if constexpr (ZuFieldProp::HasRange<Props>{})
      if (!ZfFieldLimit<Props>(v)) return fail();
    return T(v);
  } else if constexpr (Code == ZfFieldTC::DateTime || Code == ZfFieldTC::Time) {
    if (!timestamp(value)) return fail();
    ZuDateTime v;
    if (v.scan(ZuDateTimeScan::FIX{}, value) != value.length()) return fail();
    // Session::Reject dates normalized by the scanner (e.g. February 31) without
    // duplicating the framework's calendar implementation.
    auto date = ZuBox<unsigned>::eov(ZuCSpan{value.data(), 8}).template p<1>().val();
    if (v.yyyymmdd() != date) return fail();
    if constexpr (Code == ZfFieldTC::DateTime) return v;
    else return v.as_time();
  } else if constexpr (Code == ZfFieldTC::UDT) {
    if constexpr (Multiple<As<T>>{}) {
      using A = As<T>;
      return loadMultiple<A::ElemCode, typename A::ElemProps, T>(value, error);
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
	Code == ZfFieldTC::Bytes) {
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
  } else if constexpr (Code == ZfFieldTC::CString || Code == ZfFieldTC::String ||
	Code == ZfFieldTC::Bytes) {
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
  } else if constexpr (Code == ZfFieldTC::DateTime) out << value.fmt(TimeFmt<Props, Facet>{});
  else if constexpr (Code == ZfFieldTC::Time)
    out << ZuDateTime{value}.fmt(TimeFmt<Props, Facet>{});
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
	if constexpr (lenTag<F>()) {
	  out.field(lenTag<F>());
	  out << ZuBox<unsigned>{unsigned(ZuTraits<typename F::T>::length(value))};
	  out.end();
	}
	out.field(tag<F>(), lenTag<F>() != 0);
	saveScalar<Facet, F::Type::Code, typename F::Props>(out, value);
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
    constexpr unsigned i = S::template offset<F>();
    if constexpr (Component<F, Facet>{}) {
      for (unsigned j = i, n = i + FlattenField<F, Facet, ZuTypeList<>>::T::N;
	  j < n; ++j) if (m_positions[m_base + j].offset != UINT_MAX) return true;
      return false;
    } else return m_positions[m_base + i].offset != UINT_MAX;
  }

  template <typename F> auto field() const {
    using T = F::T;
    constexpr unsigned i = S::template offset<F>();
    if constexpr (Component<F, Facet>{}) {
      ObjectView<ComponentType<F, Facet>, Facet> child{
	m_input, m_positions, m_base + i, m_error};
      if constexpr (ObjPtr<T>{}) {
	if (!present<F>()) return T{};
	return T{child.alloc()};
      } else return T{child.ctor()};
    } else if constexpr (GroupField<F, Facet>{}) {
      using E = GroupElem<T>;
      const auto &p = m_positions[m_base + i];
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
      const auto &p = m_positions[m_base + i];
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
      auto value = scalar<F::Type::Code, typename F::Props, T>(
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
    constexpr unsigned i = S::template offset<F>();
    const auto &p = m_positions[m_base + i];
    if (p.offset == UINT_MAX) return {};
    return {m_input.data() + p.offset, p.length};
  }

  template <typename F> void validateField() const {
    constexpr unsigned i = S::template offset<F>();
    if constexpr (Component<F, Facet>{}) {
      ObjectView<ComponentType<F, Facet>, Facet> child{
	m_input, m_positions, m_base + i, m_error};
      child.validate();
    } else {
      const auto &p = m_positions[m_base + i];
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
	  validateMultiple<ZfFieldTC::Elem<F::Type::Code>{}, P>(
	    {m_input.data() + p.offset, p.length}, error);
	} else if constexpr (F::Type::Code == ZfFieldTC::CString) {
	  ZuCSpan value{m_input.data() + p.offset, p.length};
	  scalar<ZfFieldTC::String, typename F::Props, ZuCSpan>(value, error);
	} else if constexpr (F::Type::Code == ZfFieldTC::UDT) {
	  using T = F::T;
	  ZuAssert((Format<T, Facet>::Kind == Kind::String));
	  if constexpr (Multiple<As<T>>{}) {
	    using A = As<T>;
	    validateMultiple<A::ElemCode, typename A::ElemProps>(
	      {m_input.data() + p.offset, p.length}, error);
	  } else scalar<F::Type::Code, typename F::Props, T>(
	    {m_input.data() + p.offset, p.length}, error);
	} else {
	  using T = ZfFieldTC::Type<F::Type::Code>;
	  scalar<F::Type::Code, typename F::Props, T>(
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
      constexpr unsigned i = S::template offset<F>();
      if constexpr (Component<F, Facet>{} && Filter<F>{}) {
	if constexpr (ObjPtr<typename F::T>{}) if (!present<F>()) return;
	ObjectView<ComponentType<F, Facet>, Facet> child{
	  m_input, m_positions, m_base + i, m_error};
	child.template check<Filter>();
      } else if constexpr (GroupField<F, Facet>{} && Filter<F>{}) {
	const auto &p = m_positions[m_base + i];
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
    constexpr unsigned i = S::template offset<F>();
    ObjectView<E, Facet> child{m_input, m_positions, m_base + i, m_error};
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

  template <unsigned Select>
  Handler(ZuCSpan input, Limits limits, ZuUnsigned<Select>, Position *selector) :
    m_input{input} {
    if constexpr (Select) *selector = {UINT_MAX, 0, 0, 0};
    // Builtin root slots exist even on an early limit failure: ctor() must
    // still be able to supply absent/default constructor arguments safely.
    m_positions.length(S::Leaves::N, false);
    for (unsigned i = 0; i < S::Leaves::N; ++i) m_positions[i].offset = UINT_MAX;
    if (input.length() > limits.frameBytes ||
	S::Leaves::N > limits.parseBytes/sizeof(Position)) {
      m_error = {ErrorCode::Size}; return;
    }
    Parse p{input, limits, m_positions, m_error, 0, Select, selector};
    unsigned offset = 0;
    Scope scope{nullptr, rootContains<O, Facet, Select>, 0, 0};
    parseScope<O, Facet, Select>(p, 0, offset, scope, false);
  }

public:
  explicit Handler(ZuCSpan input, Limits limits = {}) :
    Handler{input, limits, ZuUnsigned<0>{}, nullptr} { }

  // Capture a root discriminator while parsing the selected ordinary schema.
  // Nested groups never become candidates for this root-scoped value.
  template <unsigned Select>
  Handler(ZuCSpan input, Limits limits, ZuUnsigned<Select> tag, Position &selector) :
    Handler{input, limits, tag, &selector} { }

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
