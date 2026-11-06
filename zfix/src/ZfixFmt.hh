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
#include <zlib/ZuBitmap.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZuFixed.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZuIntrin.hh>
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

  template <int32_t N> struct ID { ZuAssert(N != 0); };

  template <int32_t N> struct Binary { ZuAssert(N != 0); };
  struct Signature { };
  template <int32_t Length, int32_t Value> struct Sig { };

  // Normalize the two-tag shorthand before exact-template property lookup.
  template <typename P> struct Expand { using T = ZuTypeList<P>; };
  template <int32_t L, int32_t V> struct Expand<Sig<L, V>> {
    using T = ZuTypeList<ID<L>, Binary<V>, Signature>;
  };
  template <typename> struct Props_;
  template <typename ...P> struct Props_<ZuTypeList<P...>> {
    using T = ZuTypeConcat<typename Expand<P>::T...>;
  };
  template <typename P> using Props = Props_<P>::T;
  template <typename P> using IsBinary = HasValue<Props<P>, Binary>;
  template <typename P> using IsSignature = ZuTypeIn<Signature, Props<P>>;

  struct Char { };
  struct Multiple { };
  template <uint8_t I> struct BytesFmt { ZuAssert(I < ZtBytesFmt::Raw); };
  template <typename Fmt> struct NumberFmt { using T = Fmt; };
  template <typename Fmt> struct TimeFmt { using T = Fmt; };
  template <bool> struct Optional { };

  using Base64 = BytesFmt<ZtBytesFmt::Base64>;
  using Base64URL = BytesFmt<ZtBytesFmt::Base64URL>;
  using Base32 = BytesFmt<ZtBytesFmt::Base32>;
  using Hex = BytesFmt<ZtBytesFmt::Hex>;

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
  struct GetBytesFmt_ { ZuAssert((HasValue<Props, BytesFmt>{})); };

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
// tried to infer a discriminator or group boundaries.
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

  enum { OK, Tag, Truncated, Size, Duplicate, Value, Required,
    Reserved, Group, Depth, Unresolved, Output, Envelope, Length, Checksum };
}

struct Error {
  unsigned	code = ErrorCode::OK;
  int32_t	tag = 0;
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

  void field(int32_t tag);
  void end();
  bool group(uint64_t count);
  void endGroup();
  void entry(unsigned offset, int32_t &delimiter);

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
  int32_t	m_tag = 0;
  unsigned	m_depth = 0;
  unsigned	m_entries = 0;
  bool		m_value = false;
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
  FieldPos	*ranges = nullptr; // optional root logical spans captured by archive restore
  bool fail(unsigned code, int32_t tag, unsigned offset) {
    if (!error) error = {code, tag, offset};
    return false;
  }

  unsigned slots(unsigned count);
};

struct Scope {
  const Scope	*parent;
  bool (*contains)(int32_t);
  int32_t	delimiter;
  unsigned	depth;

  bool outside(int32_t tag) const {
    for (auto p = parent; p; p = p->parent)
      if (p->contains(tag)) return true;
    return false;
  }
};

struct Token {
  int32_t	tag;
  unsigned	offset;
  unsigned	length;
  unsigned	next;
  unsigned	countLength; // binary handler's original decimal text extent
  int32_t	valueTag;
};

// Scanning is shared with framing/discriminator inspection. Offsets address
// original caller-owned bytes; no token allocates or null-terminates input.
ZfixFmtExtern Error prefix(ZuCSpan, unsigned, Token &);
ZfixFmtExtern unsigned consume(ZuCSpan, Token &, Limits, Error &, int32_t payload = 0);
ZfixFmtExtern Error scan(ZuCSpan, unsigned, Token &);
// Inspect one unique field in SOH-delimited text. An absent field leaves offset == UINT_MAX.
ZfixFmtExtern Error inspect(ZuCSpan, int32_t tag, FieldPos &, Limits = {}, unsigned offset = 0);
// CString metadata uses the framework C ABI ownership convention: malloc/free,
// independently of the input lifetime. This allocation is destination storage.
ZfixFmtExtern char *cstring(ZuCSpan, Error &);
constexpr bool reserved(int32_t tag) {
  switch (tag) {
    case 8: case 9: case 10: case 35: return true;
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
// tags and group boundaries are known before scanning the parent scope.
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

// A component with no selected leaves has no wire presence obligation. This
// matters for an unsigned archive whose only component member is a signature.
template <typename F, typename Facet, template <typename> class Filter>
consteval bool activeField() {
  if constexpr (!Filter<F>{}) return false;
  else if constexpr (Component<F, Facet>{}) {
    bool active = false;
    ZuUnroll::all<ZuActiveFields<ComponentType<F, Facet>, Facet>>([&active]<typename G>() {
      if (activeField<G, Facet, Filter>()) active = true;
    });
    return active;
  } else return true;
}

template <typename T>
using GroupElem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;

template <typename F, typename Path_> struct Leaf {
  using Field = F;
  using Path = Path_;
};

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

template <typename F> consteval int32_t tag() {
  using P = ZuFieldProp::FIX::Props<typename F::Props>;
  ZuAssert((ZuFieldProp::HasValue<P, ZuFieldProp::FIX::ID>{}));
  return ZuFieldProp::GetValue<P, ZuFieldProp::FIX::ID>{};
}
template <typename F>
using Binary = ZuFieldProp::FIX::IsBinary<typename F::Props>;
template <typename F>
using Signature = ZuFieldProp::FIX::IsSignature<typename F::Props>;
template <typename F>
using SaveField = ZuBool<ZfFieldFilter::Save<F>{} && !Signature<F>{}>;
template <template <typename> class Filter> struct NestedFilter {
  template <typename F> using T = Filter<F>;
};
template <typename F> consteval int32_t payloadTag() {
  if constexpr (Binary<F>{})
    return ZuFieldProp::GetValue<ZuFieldProp::FIX::Props<typename F::Props>,
      ZuFieldProp::FIX::Binary>{};
  else return 0;
}

// Validate properties independently of flattening, including component fields.
template <typename F> consteval bool fieldProps() {
  using namespace ZuFieldProp;
  using P = FIX::Props<typename F::Props>;
  if (GrepValue<P, FIX::ID>::N > 1 || GrepValue<P, FIX::Binary>::N > 1)
    return false;
  if constexpr (Binary<F>{}) {
    if (F::Type::Code != ZfFieldTC::String && F::Type::Code != ZfFieldTC::Bytes)
      return false;
    if (HasValue<P, FIX::BytesFmt>{} || ZuTypeIn<FIX::Char, P>{} ||
        ZuTypeIn<FIX::Multiple, P>{}) return false;
    if (!payloadTag<F>() || payloadTag<F>() == tag<F>()) return false;
  } else {
    if (Signature<F>{}) return false;
    if (F::Type::Code == ZfFieldTC::Bytes && !HasValue<P, FIX::BytesFmt>{})
      return false;
  }
  return true;
}

template <typename O, typename Facet> consteval bool hasSignature() {
  bool found = false;
  using Leaves = Flatten<ZuActiveFields<O, Facet>, Facet, ZuTypeList<>>::T;
  ZuUnroll::all<Leaves>([&found]<typename L>() {
    using F = L::Field;
    if constexpr (Signature<F>{}) found = true;
    if constexpr (GroupField<F, Facet>{})
      if (hasSignature<GroupElem<typename F::T>, Facet>()) found = true;
  });
  return found;
}

template <typename O, typename Facet, typename Fields_>
struct Schema_ {
  using Fields = Fields_;
  using Leaves = Flatten<Fields, Facet, ZuTypeList<>>::T;

  static consteval bool unique() {
    bool ok = true;
    unsigned signatures = 0;
    ZuUnroll::all<Fields>([&ok]<typename F>() {
      if (!fieldProps<F>()) ok = false;
    });
    ZuUnroll::all<Leaves>([&ok, &signatures]<typename L>() {
      using F = L::Field;
      enum { N = tag<F>() };
      if (!fieldProps<F>()) ok = false;
      if constexpr (Signature<F>{}) ++signatures;
      if constexpr (Binary<F>{})
	if (reserved(payloadTag<F>())) ok = false;
      if constexpr (GroupField<F, Facet>{}) {
	if (hasSignature<GroupElem<typename F::T>, Facet>()) ok = false;
	if constexpr (Format<typename F::T, Facet>::ElemCode != ZfFieldTC::UDT) ok = false;
	else if constexpr (Format<GroupElem<typename F::T>, Facet>::Kind != Kind::Object) ok = false;
      }
      if constexpr (ZfFieldTC::IsVec<F::Type::Code>{})
	if (!ZuTypeIn<ZuFieldProp::FIX::Multiple, typename F::Props>{}) ok = false;
      if (!N || reserved(N)) ok = false;
      ZuUnroll::all<Leaves>([&ok]<typename R>() {
	if constexpr (ZuTypeIndex<L, Leaves>{} < ZuTypeIndex<R, Leaves>{}) {
	  using G = R::Field;
	  if (N == tag<G>() || (payloadTag<G>() && N == payloadTag<G>()) ||
	      (payloadTag<F>() && (payloadTag<F>() == tag<G>() ||
		payloadTag<F>() == payloadTag<G>()))) ok = false;
	}
      });
    });
    return ok && signatures <= 1;
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

  // Schema-sized storage, rounded to a power of two at <=25% occupancy.
  enum { TagBits = Leaves::N > 1 ? ZuIntrin::log2(Leaves::N) + 2 : 2 };

  // Scope metadata and indexed inspection; the streaming loader table is below.
  using Index = ZmLHashKV<int32_t, unsigned,
    ZmLHashStatic<TagBits, ZmLHashLocal<>>>;

  struct TagIndex : Index {
    TagIndex() {
      ZuUnroll::all<Leaves>([this]<typename L>() {
	using F = L::Field;
	enum { I = ZuTypeIndex<L, Leaves>{} };
	this->add(tag<F>(), (I << 1) | (GroupField<F, Facet>{} ? 1 : 0));
      });
    }
  };

  // Populated during program initialization; lookup has no lazy-init guard.
  inline static const TagIndex index{};

  ZuInline static int find(int32_t key) {
    auto value = index.findVal(key);
    return ZuCmp<unsigned>::null(value) ? -1 : int(value);
  }

};

template <typename O, typename Facet>
struct Schema : Schema_<O, Facet, ZuActiveFields<O, Facet>> { };

// Framing metadata deliberately carries no scalar conversion or requiredness.
// It can be combined across candidates before selecting an object schema.
template <int32_t Tag_, int32_t Payload_> struct WireField {
  enum { Tag = Tag_, Payload = Payload_ };
  static unsigned read(ZuCSpan input, Token &token, Limits limits, Error &error) {
    return consume(input, token, limits, error, Payload);
  }
};

template <typename O, typename Facet> struct WireFields;
template <typename F, typename Facet, bool = GroupField<F, Facet>{}>
struct WireLeaf {
  using T = ZuTypeList<WireField<tag<F>(), payloadTag<F>()>>;
};
template <typename F, typename Facet> struct WireLeaf<F, Facet, true> {
  using T = ZuTypeConcat<ZuTypeList<WireField<tag<F>(), payloadTag<F>()>>,
    typename WireFields<GroupElem<typename F::T>, Facet>::T>;
};
template <typename O, typename Facet> struct WireFields {
  template <typename L> using Fields = typename WireLeaf<typename L::Field, Facet>::T;
  template <typename> struct Join;
  template <typename ...L> struct Join<ZuTypeList<L...>> {
    using T = ZuTypeConcat<Fields<L>...>;
  };
  using T = typename Join<typename Schema<O, Facet>::Leaves>::T;
};

template <typename Fields_> struct InspectFields {
  using Fields = ZuTypeUnique<Fields_>;
  static consteval bool unique() {
    bool ok = true;
    ZuUnroll::all<Fields>([&ok]<typename F>() {
      ZuUnroll::all<Fields>([&ok]<typename G>() {
	if constexpr (F::Tag == G::Tag && F::Payload != G::Payload) ok = false;
      });
    });
    return ok;
  }
  ZuAssert(unique());
  enum { Bits = Fields::N > 1 ? ZuIntrin::log2(Fields::N) + 2 : 2 };
  using Reader = unsigned (*)(ZuCSpan, Token &, Limits, Error &);
  struct Index : ZmLHashKV<int32_t, Reader,
    ZmLHashStatic<Bits, ZmLHashLocal<>>> {
    Index() {
      ZuUnroll::all<Fields>([this]<typename F>() {
	if constexpr (F::Payload) this->add(int32_t(F::Tag), &F::read);
	else this->add(int32_t(F::Tag), &WireField<0, 0>::read);
      });
    }
  };
  inline static const Index index{};

  static Error inspect(ZuCSpan input, int32_t tag, FieldPos &out,
      Limits limits = {}, unsigned offset = 0) {
    out = {UINT_MAX, 0};
    if (input.length() > limits.frameBytes) return {ErrorCode::Size};
    while (offset < input.length()) {
      Token token;
      if (auto error = prefix(input, offset, token)) return error;
      unsigned value = token.offset;
      auto reader = index.findVal(token.tag);
      Error error;
      unsigned consumed = reader ? reader(input, token, limits, error) :
	consume(input, token, limits, error);
      if (!consumed) return error;
      FieldPos pos{UINT_MAX, 0};
      if (token.tag == tag) {
	pos = {value, token.offset == value ? token.length : token.countLength};
      } else if (token.offset != value) {
	// Only the primary tag has a hash entry. Its handler supplies the
	// payload range when the selector names the adjacent value tag.
	if (token.valueTag == tag) pos = {token.offset, token.length};
      }
      if (pos.offset != UINT_MAX) {
	if (out.offset != UINT_MAX) return {ErrorCode::Duplicate, tag, pos.offset};
	out = pos;
      }
      offset = value + consumed;
    }
    return {};
  }
};

template <typename O, typename Facet = ZuFacet::FIX>
Error inspect(ZuCSpan input, int32_t tag, FieldPos &out,
    Limits limits = {}, unsigned offset = 0) {
  return InspectFields<typename WireFields<O, Facet>::T>::inspect(input, tag, out, limits, offset);
}

template <typename O, typename Facet>
bool parseScope(Parse &, unsigned, unsigned &, Scope &, bool, bool = true);

template <typename O, typename Facet, int32_t SelectTag = 0>
bool scopeContains(int32_t tag) {
  return (SelectTag && tag == SelectTag) || Schema<O, Facet>::find(tag) >= 0;
}
template <typename O, typename Facet>
bool parseGroup(Parse &p, unsigned slot, Token &token, Scope &parent) {
  using E = GroupElem<O>;
  using S = Schema<E, Facet>;
  ZuAssert((Format<O, Facet>::ElemCode == ZfFieldTC::UDT));
  ZuAssert((Format<E, Facet>::Kind == Kind::Object));
  ZuAssert(S::Leaves::N > 0);
  unsigned n = ZuBox<unsigned>{ZuCSpan{p.input.data() + token.offset, token.length}}.val();
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
    if (p.error = prefix(p.input, offset, next)) return false;
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
    Parse &p, unsigned base, unsigned &offset, Scope &scope, bool entry, bool lastEntry) {
  using S = Schema<O, Facet>;
  bool started = false;
  int last = -1;
  unsigned length = p.input.length();
  while (offset < length) {
    Token token;
    if (p.error = prefix(p.input, offset, token)) return false;
    int index = S::find(token.tag);
    if (index < 0) {
      if (entry && scope.outside(token.tag)) break;
      if (reserved(token.tag)) return p.fail(ErrorCode::Reserved, token.tag, offset);
      unsigned value = token.offset;
      unsigned consumed = consume(p.input, token, p.limits, p.error);
      if (!consumed) return false;
      offset = value + consumed;
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
    unsigned value = token.offset, consumed = 0;
    if constexpr (S::Leaves::N) {
      consumed = ZuSwitch::dispatch<S::Leaves::N>(field,
	[&p, &token, &scope, slot, value]<typename I>(I) -> unsigned {
	  using F = typename ZuType<I{}, typename S::Leaves>::Field;
	  if (!consume(p.input, token, p.limits, p.error, payloadTag<F>())) return 0;
	  if constexpr (GroupField<F, Facet>{}) {
	    if (!parseGroup<typename F::T, Facet>(p, slot, token, scope)) return 0;
	  } else p.positions[slot] = {token.offset, token.length, 0, 0};
	  return token.next - value;
	});
    }
    if (!consumed) return false;
    if (p.ranges && !scope.parent) p.ranges[field] = {offset, value + consumed - offset};
    offset = value + consumed;
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
  {
    if constexpr (Fmt == ZtBytesFmt::Hex) return value.length()<<1;
    else return BytesCodec<Fmt>::enclen(value.length());
  }
}

template <typename Props, typename Out>
void saveBytes(Out &out, ZuBSpan value, unsigned size) {
  enum { Fmt = ZuFieldProp::FIX::GetBytesFmt<Props>{} };
  out << BytesPrint<Fmt>{value, size};
}

template <typename T, typename = void> struct OwnBytes : ZuFalse { };
template <typename T>
struct OwnBytes<T, decltype(ZuDeclVal<T &>().ensure(unsigned{}), void())> : ZuTrue { };

template <typename T> struct RawSpan : ZuFalse { };
template <typename E> struct RawSpan<ZuSpan<E>> : ZuTrue { using Elem = E; };

// Wire extents are octets; ZuSpan extents are elements. Mutable spans are
// explicit views of the caller's buffer, just like their const counterparts.
template <typename T> T loadRawSpan(ZuCSpan value, Error &error) {
  using E = typename RawSpan<T>::Elem;
  if (value.length() % sizeof(E)) { error.code = ErrorCode::Value; return {}; }
  auto ptr = reinterpret_cast<E *>(const_cast<char *>(value.data()));
  return {ptr, value.length()/sizeof(E)};
}
template <typename T> ZuBSpan rawBytes(const T &value) {
  if constexpr (RawSpan<T>{}) {
    using E = typename RawSpan<T>::Elem;
    // The sentinel is larger than any configured field/frame limit; the
    // bounded sink rejects it before reading the backing storage.
    uint64_t n = value.length();
    n = n > UINT64_MAX/sizeof(E) ? UINT64_MAX : n * sizeof(E);
    return {reinterpret_cast<const uint8_t *>(value.data()), n};
  } else return ZuBSpan{value};
}

template <typename Props, typename T> T loadBytes(ZuCSpan value, Error &error) {
  if constexpr (ZuFieldProp::FIX::IsBinary<Props>{}) {
    if constexpr (RawSpan<T>{}) return loadRawSpan<T>(value, error);
    else return T(ZuBSpan{value});
  } else {
    enum { Fmt = ZuFieldProp::FIX::GetBytesFmt<Props>{} };
    // Text encodings decode directly into destination-owned storage.
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
T loadTime(ZuCSpan value) {
  using P = TimePolicy<TimeFmt<Props, Facet>>;
  if constexpr (P::Kind == Time::Unix) {
    ZuDecimal d{value};
    if constexpr (P::Unit) d.value /= ZuDecimalFn::pow10_64(P::Unit * 3);
    auto time = ZuTime{d};
    if constexpr (ZuIsSame<T, ZuTime>{}) return time;
    else return ZuDateTime{time};
  } else {
    ZuDateTime v{typename P::Scan{}, value};
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

template <typename Fmt> ZuDecimal loadDecimal(ZuCSpan value) {
  if constexpr (!Fmt::Hex_ && !Fmt::Comma_ && Fmt::Justification_ == ZuFmt::Just::None)
    return ZuDecimal{value};
  else {
    value = numberText<Fmt>(value);
    if (!value) return {};
    bool negative = value[0] == '-';
    if (negative) value.offset(1);
    uint64_t whole = 0, fraction = 0;
    unsigned n = Zu_nscan<Fmt>::atou(whole, value.data(), value.length());
    if (n < value.length() && value[n] == '.')
      Zu_nscan<ZuFmt::Frac<18, 18>>::atou(
	fraction, value.data() + n + 1, value.length() - n - 1);
    else if (!n) return {};
    int128_t scaled = uint128_t(whole) * ZuDecimal::scale() + fraction;
    return ZuDecimal::Unscaled{negative ? -scaled : scaled};
  }
}

template <typename Fn>
void multipleTokens(ZuCSpan value, Error &error, Fn fn) {
  unsigned begin = 0, n = value.length();
  while (begin < n && !error) {
    auto ptr = static_cast<const char *>(memchr(value.data() + begin, ' ', n - begin));
    unsigned end = ptr ? unsigned(ptr - value.data()) : n;
    if (begin != end) fn(ZuCSpan{value.data() + begin, end - begin});
    if (end == n) return;
    begin = end + 1;
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
template <unsigned Code, typename Props, typename T, typename Facet = ZuFacet::FIX>
T loadMultiple(ZuCSpan value, Error &error) {
  ZuAssert(Code != ZfFieldTC::UDT && !ZfFieldTC::IsVec<Code>{});
  using E = GroupElem<T>;
  T out;
  multipleTokens(value, error, [&out, &error](ZuCSpan token) {
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
  if constexpr (ZfFieldTC::IsVec<Code>{}) {
    ZuAssert((ZuTypeIn<ZuFieldProp::FIX::Multiple, Props>{}));
    return loadMultiple<ZfFieldTC::Elem<Code>{}, Props, T, Facet>(value, error);
  } else if constexpr (Code == ZfFieldTC::CString) {
    if constexpr (ZuTraits<T>::IsPointer && ZuTraits<T>::IsPrimitive)
      return T(cstring(value, error));
    else return T(value);
  } else if constexpr (Code == ZfFieldTC::Bytes) {
    return loadBytes<Props, T>(value, error);
  } else if constexpr (Code == ZfFieldTC::String) {
    if constexpr (ZuFieldProp::FIX::IsBinary<Props>{} && RawSpan<T>{})
      return loadRawSpan<T>(value, error);
    else return T(value);
  } else if constexpr (Code == ZfFieldTC::Bool) {
    return T(value && value[0] == 'Y');
  } else if constexpr (Code >= ZfFieldTC::Int8 && Code <= ZfFieldTC::UInt128) {
    using V = ZfFieldTC::Type<Code>;
    if constexpr (ZuTypeIn<ZuFieldProp::FIX::Char, Props>{})
      return T(V(value ? value[0] : 0));
    else if constexpr (ZuFieldProp::HasEnum<Props>{})
      return T(V(ZuFieldProp::GetEnum<Props>::s2v(value)));
    else if constexpr (ZuFieldProp::HasFlags<Props>{}) {
      using Map = ZuFieldProp::GetFlags<Props>;
      V v = 0;
      multipleTokens(value, error, [&v](ZuCSpan token) {
	v |= V(typename Map::Scan{token});
      });
      return T(v);
    } else {
      V v = ZuCmp<V>::null();
      using Fmt = NumberFmt<Props, Facet>;
      if constexpr (ZuTraits<V>::IsSigned)
	Zu_nscan<Fmt>::atoi(v, value.data(), value.length());
      else Zu_nscan<Fmt>::atou(v, value.data(), value.length());
      return T(v);
    }
  } else if constexpr (Code == ZfFieldTC::Decimal) {
    return loadDecimal<NumberFmt<Props, Facet>>(value);
  } else if constexpr (Code == ZfFieldTC::Fixed) {
    using Fmt = NumberFmt<Props, Facet>;
    if constexpr (!Fmt::Hex_ && !Fmt::Comma_ && Fmt::Justification_ == ZuFmt::Just::None) {
      if constexpr (ZuFieldProp::HasNDP<Props>{})
	return ZuFixed{value, ZuFieldProp::GetNDP<Props>{}};
      else return ZuFixed{value};
    } else {
      auto d = loadDecimal<Fmt>(value);
      if constexpr (ZuFieldProp::HasNDP<Props>{})
	return ZuFixed{d, ZuFieldProp::GetNDP<Props>{}};
      else return ZuFixed{d};
    }
  } else if constexpr (Code == ZfFieldTC::Float) {
    using Fmt = NumberFmt<Props, Facet>;
    double v = ZuFP<double>::nan();
    Zu_nscan<Fmt>::atof(v, value.data(), value.length());
    return T(v);
  } else if constexpr (Code == ZfFieldTC::DateTime || Code == ZfFieldTC::Time) {
    return loadTime<Props, Facet, T>(value);
  } else if constexpr (Code == ZfFieldTC::UDT) {
    if constexpr (Multiple<As<T>>{}) {
      using A = As<T>;
      return loadMultiple<A::ElemCode, typename A::ElemProps, T, Facet>(value, error);
    } else {
      using Fmt = decltype(Zfix_StringFmt(ZuDeclVal<T *>()));
      return Fmt::template Handler<T>::load(value, error);
    }
  } else ZuAssert(Code == ZfFieldTC::UDT);
}

template <typename O, typename Facet, template <typename> class Filter,
  typename Path = ZuTypeList<>, bool Track = true, typename Out>
void saveObject(Out &out, const O &o);

template <typename Facet, unsigned Code, typename Props, typename Out, typename T>
void saveScalar(Out &out, const T &value);
template <typename Facet, unsigned Code, typename Props, typename Out, typename T>
void saveMultiple(Out &out, const T &value) {
  ZuAssert(Code != ZfFieldTC::UDT && !ZfFieldTC::IsVec<Code>{});
  unsigned n = ZuTraits<T>::length(value);
  for (unsigned i = 0; i < n && !out.failed(); ++i) {
    const auto &element = value[i];
    if (i) out << ' ';
    saveScalar<Facet, Code, Props>(out, element);
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
    if constexpr (ZuFieldProp::HasNDP<Props>{})
      out << value.template fp<ZuFieldProp::GetNDP<Props>{}, '\0', NumberFmt<Props, Facet>>();
    else out << value.template fmt<NumberFmt<Props, Facet>>();
  } else if constexpr (Code == ZfFieldTC::Float) {
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

// Also used by signMsg: emit the declared pair directly, outside the
// ordinary Tx save filter. The octet count is independent of NumberFmt.
template <typename F, typename Out, typename T>
void saveBinary(Out &out, const T &value) {
  ZuAssert((Binary<F>{}));
  auto bytes = rawBytes(value);
  out.field(tag<F>());
  out << ZuBox<uint64_t>{bytes.length()};
  out.end();
  out.field(payloadTag<F>());
  out << ZuCSpan{bytes};
  out.end();
}

template <typename Out, typename F, typename O, typename = void>
struct SaveValue {
  static decltype(auto) get(Out &, const O &o) { return F::get(o); }
  static bool enabled(Out &, const O &) { return true; }
};
template <typename Out, typename F, typename O>
struct SaveValue<Out, F, O, decltype(
    ZuDeclVal<Out &>().template value<F>(ZuDeclVal<const O &>()), void())> {
  static decltype(auto) get(Out &out, const O &o) { return out.template value<F>(o); }
  static bool enabled(Out &out, const O &o) { return out.template enabled<F>(o); }
};

template <bool Track, typename F, typename Out, typename O>
decltype(auto) saveValue(Out &out, const O &o) {
  if constexpr (Track) return SaveValue<Out, F, O>::get(out, o);
  else return F::get(o);
}

template <typename Out, typename L, bool Track, typename = void> class SaveRange {
public:
  explicit SaveRange(Out &) { }
  void begin() { }
};
template <typename Out, typename L>
class SaveRange<Out, L, true, decltype(
    ZuDeclVal<Out &>().template rangeField<L>(FieldPos{}), void())> {
public:
  explicit SaveRange(Out &out) : m_out{out} { }
  ~SaveRange() {
    m_out.template rangeField<L>({m_start,
      m_start == UINT_MAX ? 0 : m_out.length() - m_start});
  }
  void begin() { m_start = m_out.length(); }
private:
  Out &m_out;
  unsigned m_start = UINT_MAX;
};

template <typename List, bool Track, typename Out>
void saveAbsent(Out &out) {
  ZuUnroll::all<List>([&out]<typename L>() { SaveRange<Out, L, Track> range{out}; });
}

template <typename O, typename Facet, template <typename> class Filter,
  typename Path, bool Track, typename Out>
void saveObject(Out &out, const O &o) {
  using S = Schema<O, Facet>;
  ZuUnroll::all<typename S::Fields>([&out, &o]<typename F>() {
    if constexpr (Component<F, Facet>{}) {
      using Leaves = typename FlattenField<F, Facet, Path>::T;
      using Next = typename Path::template Push<F>;
      using T = ComponentType<F, Facet>;
      if constexpr (!activeField<F, Facet, Filter>()) {
	saveAbsent<Leaves, Track>(out);
      } else {
	if (out.failed()) return;
	const auto &value = F::get(o);
	if constexpr (UnionField<F, Facet>{}) {
	  if (!value.template is<T>()) out.fail(ErrorCode::Unresolved);
	  else saveObject<T, Facet, Filter, Next, Track>(out, value.template p<T>());
	} else if constexpr (ObjPtr<typename F::T>{}) {
	  if (value) saveObject<T, Facet, Filter, Next, Track>(out, *value);
	  else {
	    saveAbsent<Leaves, Track>(out);
	    if constexpr (ZuTypeIn<ZuFieldProp::Required, typename F::Props>{})
	      out.fail(ErrorCode::Required);
	  }
	} else saveObject<T, Facet, Filter, Next, Track>(out, value);
      }
    } else {
      SaveRange<Out, Leaf<F, Path>, Track> range{out};
      if constexpr (activeField<F, Facet, Filter>()) {
	if (out.failed()) return;
	if constexpr (Track) if (!SaveValue<Out, F, O>::enabled(out, o)) return;
	const auto &value = saveValue<Track, F>(out, o);
	if constexpr (ZuFieldProp::FIX::GetOptional<typename F::Props>{}) {
	  if constexpr (F::Type::Code != ZfFieldTC::Bool) {
	    if constexpr (ZuFieldProp::HasEnum<typename F::Props>{}) {
	      if (value < 0) return;
	    } else if constexpr (F::Type::Code == ZfFieldTC::DateTime) {
	      if (!*value) return;
	    } else if (ZuNull(value)) return;
	  }
	}
	range.begin();
	if constexpr (GroupField<F, Facet>{}) {
	  using T = F::T;
	  using E = GroupElem<T>;
	  ZuAssert((Format<T, Facet>::ElemCode == ZfFieldTC::UDT));
	  ZuAssert((Format<E, Facet>::Kind == Kind::Object));
	  uint64_t n = ZuTraits<T>::length(value);
	  if (!out.group(n)) return;
	  out.field(tag<F>()); out << ZuBox<uint64_t>{n}; out.end();
	  int32_t delimiter = 0;
	  for (uint64_t i = 0; i < n && !out.failed(); ++i) {
	    auto before = out.length();
	    saveObject<E, Facet, NestedFilter<Filter>::template T,
	      ZuTypeList<>, false>(out, value[i]);
	    out.entry(before, delimiter);
	  }
	  out.endGroup();
	} else {
	  if constexpr (F::Type::Code == ZfFieldTC::UDT)
	    ZuAssert((Format<typename F::T, Facet>::Kind == Kind::String));
	  if constexpr (Binary<F>{}) saveBinary<F>(out, value);
	  else if constexpr (F::Type::Code == ZfFieldTC::Bytes) {
	    auto bytes = ZuBSpan{value};
	    auto size = bytesLength<typename F::Props>(bytes);
	    if (size > UINT_MAX) { out.fail(ErrorCode::Size); return; }
	    out.field(tag<F>());
	    saveBytes<typename F::Props>(out, bytes, unsigned(size));
	  } else {
	    out.field(tag<F>());
	    saveScalar<Facet, F::Type::Code, typename F::Props>(out, value);
	  }
	  if constexpr (!Binary<F>{}) out.end();
	}
      }
    }
  });
}

template <typename F> auto defaultValue(Error &error) {
  using T = F::T;
  if constexpr (F::Type::Code == ZfFieldTC::CString) {
    auto ptr = F::deflt();
    if (!ptr) return T{};
    return T(cstring(ZuCSpan{ptr}, error));
  } else if constexpr (F::Type::Code == ZfFieldTC::CStringVec) {
    using E = GroupElem<T>;
    T out;
    auto defaults = F::deflt();
    for (unsigned j = 0, n = ZuTraits<decltype(defaults)>::length(defaults);
	j < n && !error; ++j) {
      const char *ptr = defaults[j];
      if constexpr (ZuTraits<E>::IsPointer && ZuTraits<E>::IsPrimitive)
	out.push(ptr ? E(cstring(ZuCSpan{ptr}, error)) : E{});
      else out.push(ptr ? E(ptr) : E{});
    }
    if (error) { freeCStrings(out); out.length(0, false); }
    return out;
  } else if constexpr (ZuIsSame<decltype(F::deflt()), void>{}) return T{};
  else if constexpr (Binary<F>{} && RawSpan<T>{})
    return loadRawSpan<T>(ZuCSpan{rawBytes(F::deflt())}, error);
  else return T(F::deflt());

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
      if (p.offset == UINT_MAX) return defaultValue<F>(m_error);
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

  template <typename F> static consteval int32_t missingTag() {
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
    if (m_error) return {};
    constexpr unsigned I = S::template offset<F>();
    const auto &p = m_positions[m_base + I];
    if (p.offset == UINT_MAX) return {};
    return {m_input.data() + p.offset, p.length};
  }

  // Check selected presence and structure without converting field values.
  template <template <typename> class Filter = ZfFieldFilter::Save>
  void check() const {
    ZuUnroll::all<Fields>([this]<typename F>() {
      if constexpr (activeField<F, Facet, Filter>() &&
	  ZuTypeIn<ZuFieldProp::Required, typename F::Props>{})
	if (!m_error && !present<F>()) m_error = {ErrorCode::Required, missingTag<F>(), 0};
    });
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
	  child.template check<NestedFilter<Filter>::template T>();
	}
      }
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
      }
    });
    return !m_error;
  }

  template <typename ...Args> O ctor(Args &&...args) const {
    required<CtorFields>();
    using InitFields = ZuTypeGrep<ZfFieldFilter::Init, Fields>;
    if constexpr (!InitFields::N) {
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

// A live decoder needs only presence bits per scope. Values are converted and
// assigned as each tag arrives; no message-wide tag/value index is constructed.
template <typename O, typename = void> struct LiveCtor : ZuFalse { };
template <typename O> struct LiveCtor<O, decltype(O{}, void())> :
  ZuIsConstructible<O &&, O> { };

template <typename O, typename Facet, unsigned Depth = 0>
consteval bool liveRead() {
  if constexpr (Depth >= Limits::GroupDepth || !LiveCtor<O>{}) return false;
  else {
    bool ok = true;
    ZuUnroll::all<typename Schema<O, Facet>::Fields>([&ok]<typename F>() {
      if constexpr (ZfFieldFilter::Ctor<F>{}) ok = false;
      else if constexpr (Component<F, Facet>{} || GroupField<F, Facet>{}) {
	using R = decltype(F::get(ZuDeclVal<O &>()));
	if constexpr (F::ReadOnly || !ZuIsLRef<R>{} || ZuIsCRef<R>{}) ok = false;
	else if constexpr (Component<F, Facet>{}) {
	  if constexpr (ObjPtr<typename F::T>{} || UnionField<F, Facet>{}) ok = false;
	  else if (!liveRead<ComponentType<F, Facet>, Facet, Depth + 1>()) ok = false;
	} else if (!liveRead<GroupElem<typename F::T>, Facet, Depth + 1>()) ok = false;
      }
    });
    return ok;
  }
}

template <typename Path> struct ReadPath;
template <> struct ReadPath<ZuTypeList<>> {
  template <typename O> static decltype(auto) get(O &o) { return (o); }
};
template <typename P, typename ...Tail> struct ReadPath<ZuTypeList<P, Tail...>> {
  template <typename O> static decltype(auto) get(O &o) {
    return ReadPath<ZuTypeList<Tail...>>::get(P::get(o));
  }
};

template <typename F, typename O, typename T> void setField(O &o, T &&value) {
  if constexpr (F::Type::Code == ZfFieldTC::CString)
    ::free(const_cast<char *>(F::get(o)));
  else if constexpr (F::Type::Code == ZfFieldTC::CStringVec)
    freeCStrings(F::get(o));
  F::set(o, ZuFwd<T>(value));
}

struct Read {
  ZuCSpan input;
  Limits limits;
  Error &error;
  unsigned entries = 0, bytes = 0;
  FieldPos selector{UINT_MAX, 0};
  int32_t selectTag = 0;

  bool observe(int32_t tag, unsigned offset, unsigned length) {
    if (!selectTag || tag != selectTag) return true;
    if (selector.offset != UINT_MAX) return fail(ErrorCode::Duplicate, tag, offset);
    selector = {offset, length};
    return true;
  }

  bool fail(unsigned code, int32_t tag, unsigned offset) {
    if (!error) error = {code, tag, offset};
    return false;
  }
};

// Match positional presence checks without constructing objects or positions.
// An absent nullable component has no child presence obligations.
template <typename O, typename Facet, typename F, unsigned N>
bool readPresent(const ZuBitmap<N> &seen, unsigned base) {
  constexpr unsigned I = Schema<O, Facet>::template offset<F>();
  if constexpr (Component<F, Facet>{}) {
    for (unsigned j = I, n = I + FlattenField<F, Facet, ZuTypeList<>>::T::N;
	j < n; ++j) if (seen.get(base + j)) return true;
    return false;
  } else return seen.get(base + I);
}

template <typename O, typename Facet, template <typename> class Filter, unsigned N>
void readRequired(Read &p, const ZuBitmap<N> &seen, unsigned base = 0) {
  using S = Schema<O, Facet>;
  ZuUnroll::all<typename S::Fields>([&p, &seen, base]<typename F>() {
    (void)p; (void)seen; (void)base;
    if constexpr (activeField<F, Facet, Filter>() &&
	ZuTypeIn<ZuFieldProp::Required, typename F::Props>{}) {
      if (!p.error && !readPresent<O, Facet, F>(seen, base)) {
	if constexpr (Component<F, Facet>{}) p.fail(ErrorCode::Required, 0, 0);
	else p.fail(ErrorCode::Required, tag<F>(), 0);
      }
    }
  });
  ZuUnroll::all<typename S::Fields>([&p, &seen, base]<typename F>() {
    (void)p; (void)seen; (void)base;
    if constexpr (Component<F, Facet>{} && Filter<F>{}) {
      if (p.error) return;
      if constexpr (ObjPtr<typename F::T>{})
	if (!readPresent<O, Facet, F>(seen, base)) return;
      readRequired<ComponentType<F, Facet>, Facet, Filter>(
	p, seen, base + S::template offset<F>());
    }
  });
}

template <typename> using ReadNone = ZuFalse;

template <typename O, typename Facet, template <typename> class Filter,
  bool Recover = false, int32_t SelectTag = 0, bool Assign = true>
bool readScope(Read &, ZuIf<Assign, O &, O *>, unsigned &, Scope &, bool, bool = true);

template <typename T, typename Facet, bool Apply,
  template <typename> class Check = ReadNone>
bool readGroup(Read &p, ZuIf<Apply, T &, T *> out, Token &token, Scope &parent) {
  using E = GroupElem<T>;
  unsigned n = ZuBox<unsigned>{ZuCSpan{p.input.data() + token.offset, token.length}}.val();
  if (parent.depth >= p.limits.groupDepth)
    return p.fail(ErrorCode::Depth, token.tag, token.offset);
  if (n > p.limits.groupEntries - p.entries)
    return p.fail(ErrorCode::Size, token.tag, token.offset);
  if (n > (p.input.length() - token.next)/3)
    return p.fail(ErrorCode::Group, token.tag, token.offset);
  p.entries += n;
  if constexpr (Apply) { out.length(0); out.ensure(n); }
  unsigned offset = token.next;
  Scope scope{&parent, scopeContains<E, Facet>, 0, parent.depth + 1};
  for (unsigned i = 0; i < n; ++i) {
    if constexpr (Apply) {
      auto e = new (out.data() + i) E{};
      out.length(i + 1, false);
      if (!readScope<E, Facet, ZfFieldFilter::Init>(p, *e, offset, scope, true, i + 1 == n))
	return false;
    } else {
      if (!readScope<E, Facet, NestedFilter<Check>::template T, false, 0, false>(
	  p, nullptr, offset, scope, true, i + 1 == n))
	return false;
    }
  }
  if (offset < p.input.length()) {
    Token next;
    if (auto error = prefix(p.input, offset, next)) {
      p.fail(error.code, error.tag, error.offset); return false;
    }
    if (scope.contains(next.tag) && !parent.contains(next.tag) && !parent.outside(next.tag))
      return p.fail(ErrorCode::Group, next.tag, offset);
  }
  token.next = offset;
  return true;
}

// The wire loop looks up and calls a typed loader directly. The policy is
// part of the table type; selectors and recovery do not duplicate the table.
template <typename O, typename Facet, template <typename> class Filter, bool Assign>
struct ReadFields {
  using S = Schema<O, Facet>;
  using Loader = unsigned (*)(ReadFields &, Token &);
  struct Item { unsigned index; Loader load; };
  struct Index : ZmLHashKV<int32_t, const Item *,
    ZmLHashStatic<S::TagBits, ZmLHashLocal<>>> {
    Index() {
      ZuUnroll::all<typename S::Leaves>([this]<typename L>() {
	static const Item item{ZuTypeIndex<L, typename S::Leaves>{},
	  +[](ReadFields &fields, Token &token) {
	    return fields.template field<L>(token);
	  }};
	this->add(tag<typename L::Field>(), &item);
      });
    }
  };

  inline static const Index index{};

  Read &p;
  ZuIf<Assign, O &, O *> o;
  unsigned &offset;
  Scope &scope;
  bool entry, lastEntry;
  ZuBitmap<S::Leaves::N> seen;
  bool started = false;
  int last = -1;

  template <typename L> ZuInline unsigned field(Token &token) {
    using F = L::Field;
    enum { I = ZuTypeIndex<L, typename S::Leaves>{} };
    unsigned value = token.offset;
    if (!consume(p.input, token, p.limits, p.error, payloadTag<F>())) return 0;
    if (!entry) {
    if constexpr (Binary<F>{}) {
      if (!p.observe(token.tag, value, token.countLength) ||
	  !p.observe(payloadTag<F>(), token.offset, token.length)) return 0;
    } else if (!p.observe(token.tag, token.offset, token.length)) return 0;
    }
    last = I; seen.set(I);
    if constexpr (GroupField<F, Facet>{}) {
      if constexpr (Assign && !F::ReadOnly && Filter<F>{})
	readGroup<typename F::T, Facet, true>(
	  p, F::get(ReadPath<typename L::Path>::get(o)), token, scope);
      else {
	if constexpr (!Assign && Filter<F>{})
	  readGroup<typename F::T, Facet, false, Filter>(p, nullptr, token, scope);
	else readGroup<typename F::T, Facet, false>(p, nullptr, token, scope);
      }
    } else if constexpr (Assign && !F::ReadOnly && Filter<F>{}) {
      Error error;
      ZuCSpan input{p.input.data() + token.offset, token.length};
      auto value = scalar<F::Type::Code, typename F::Props, typename F::T, Facet>(input, error);
      if (!error) setField<F>(ReadPath<typename L::Path>::get(o), ZuMv(value));
      if (error) p.fail(error.code, token.tag, token.offset);
    }
    return p.error ? 0 : token.next - value;
  }
};

template <typename O, typename Facet, template <typename> class Filter,
  bool Recover, int32_t SelectTag, bool Assign>
bool readScope(Read &p, ZuIf<Assign, O &, O *> o,
    unsigned &offset, Scope &scope, bool entry, bool lastEntry) {
  using S = Schema<O, Facet>;
  enum { Charge = ((S::Leaves::N + 63)>>6) * sizeof(uint64_t) };
  if (Charge > p.limits.parseBytes - p.bytes)
    return p.fail(ErrorCode::Size, 0, offset);
  p.bytes += Charge;
  struct Leave { Read &p; ~Leave() { p.bytes -= Charge; } } leave{p};
  using Fields = ReadFields<O, Facet, Filter, Assign>;
  Fields fields{p, o, offset, scope, entry, lastEntry};
  auto &seen = fields.seen;
  while (offset < p.input.length()) {
    Token token;
    if (auto error = prefix(p.input, offset, token))
      return p.fail(error.code, error.tag, error.offset);
    auto item = Fields::index.findVal(token.tag);
    if (item) {
      unsigned i = item->index;
      if (entry) {
	if (!fields.started) {
	  if (scope.delimiter && scope.delimiter != token.tag)
	    return p.fail(ErrorCode::Group, token.tag, offset);
	  scope.delimiter = token.tag; fields.started = true;
	} else if (scope.delimiter == token.tag) break;
      }
      if (seen.get(i)) {
	if (entry && lastEntry && scope.outside(token.tag)) break;
	return p.fail(ErrorCode::Duplicate, token.tag, offset);
      }
      if (entry && int(i) < fields.last) {
	if (lastEntry && scope.outside(token.tag)) break;
	return p.fail(ErrorCode::Group, token.tag, offset);
      }
    } else {
      if (entry && scope.outside(token.tag)) break;
      if (reserved(token.tag)) return p.fail(ErrorCode::Reserved, token.tag, offset);
    }
    unsigned value = token.offset;
    unsigned consumed;
    if (item) consumed = item->load(fields, token);
    else {
      consumed = consume(p.input, token, p.limits, p.error);
      if (consumed && !entry && !p.observe(token.tag, token.offset, token.length))
	consumed = 0;
    }
    if (!consumed) return false;
    if constexpr (!Recover) if (p.error) return false;
    offset = value + consumed;
  }
  if (entry && !fields.started) return p.fail(ErrorCode::Group, 0, offset);
  if (p.error) return false;
  if constexpr (!Assign) readRequired<O, Facet, Filter>(p, seen);
  else {
    ZuUnroll::all<typename S::Fields>([&p, &seen]<typename F>() {
      if constexpr (Component<F, Facet>{} && activeField<F, Facet, Filter>() &&
	  ZuTypeIn<ZuFieldProp::Required, typename F::Props>{}) {
	constexpr unsigned I = S::template offset<F>();
	bool present = false;
	for (unsigned j = I, n = I + FlattenField<F, Facet, ZuTypeList<>>::T::N;
	    j < n; ++j) present |= seen.get(j);
	if (!present) p.fail(ErrorCode::Required, 0, 0);
      }
    });
    ZuUnroll::all<typename S::Leaves>([&p, &o, &seen]<typename L>() {
      (void)p; (void)o; (void)seen;
      using F = L::Field;
      enum { I = ZuTypeIndex<L, typename S::Leaves>{} };
      if constexpr (Filter<F>{}) {
	if (p.error || seen.get(I)) return;
	if constexpr (ZuTypeIn<ZuFieldProp::Required, typename F::Props>{})
	  p.fail(ErrorCode::Required, tag<F>(), 0);
	else if constexpr (Assign && !F::ReadOnly) {
	  auto value = defaultValue<F>(p.error);
	  if (!p.error) setField<F>(ReadPath<typename L::Path>::get(o), ZuMv(value));
	}
      }
    });
  }
  return !p.error;
}

template <typename O, typename Facet, template <typename> class Filter, int32_t SelectTag = 0>
bool read(O &o, ZuCSpan input, Error &error, Limits limits = {}, unsigned offset = 0,
    FieldPos *selector = nullptr) {
  if (error) return false;
  if (input.length() > limits.frameBytes) { error = {ErrorCode::Size}; return false; }
  Read p{input, limits, error};
  p.selectTag = SelectTag;
  Scope scope{nullptr, scopeContains<O, Facet, SelectTag>, 0, 0};
  bool result = readScope<O, Facet, Filter, false, SelectTag>(p, o, offset, scope, false);
  if constexpr (SelectTag) *selector = p.selector;
  return result;
}

template <typename O, typename Facet = ZuFacet::FIX,
  unsigned Kind = Format<O, Facet>::Kind> class Handler;

template <typename O, typename Facet> class Indexed {
  using S = Schema<O, Facet>;

public:
  explicit Indexed(ZuCSpan input, Limits limits = {}) : m_input{input} {
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

  Indexed(const Indexed &) = delete;
  Indexed &operator =(const Indexed &) = delete;

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

  template <template <typename> class Filter = SaveField, typename Out>
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

// Indexing is explicit: raw/check and constructor-bound representations retain
// the positional decoder. Ordinary writable objects take the live path first.
template <typename O, typename Facet> class Live {
public:
  explicit Live(ZuCSpan input, Limits limits = {}) : m_input{input}, m_limits{limits} {
    if (input.length() > limits.frameBytes) m_error = {ErrorCode::Size};
  }
  bool valid() const { return !m_error; }
  const Error &error() const { return m_error; }

  template <template <typename> class Filter = ZfFieldFilter::Save>
  bool check() {
    if (m_error) return false;
    Read p{m_input, m_limits, m_error};
    unsigned offset = 0;
    Scope scope{nullptr, scopeContains<O, Facet>, 0, 0};
    return readScope<O, Facet, Filter, false, 0, false>(p, nullptr, offset, scope, false);
  }
  template <typename F> ZuCSpan raw() {
    Indexed<O, Facet> h{m_input, m_limits};
    auto result = h.template raw<F>(); m_error = h.error(); return result;
  }
  template <typename ...Args> O ctor(Args &&...args) {
    if constexpr (sizeof...(Args)) {
      Indexed<O, Facet> h{m_input, m_limits};
      auto o = h.ctor(ZuFwd<Args>(args)...); m_error = h.error(); return o;
    } else {
      O o{}; read<O, Facet, ZfFieldFilter::Init>(o, m_input, m_error, m_limits); return o;
    }
  }
  template <typename ...Args> O *alloc(Args &&...args) {
    if constexpr (sizeof...(Args)) {
      Indexed<O, Facet> h{m_input, m_limits};
      auto o = h.alloc(ZuFwd<Args>(args)...); m_error = h.error(); return o;
    } else {
      if (m_error) return nullptr;
      O *o = new O{};
      if (!read<O, Facet, ZfFieldFilter::Init>(*o, m_input, m_error, m_limits)) {
	delete o; return nullptr;
      }
      return o;
    }
  }
  template <typename ...Args> bool new_(void *ptr, Args &&...args) {
    if constexpr (sizeof...(Args)) {
      Indexed<O, Facet> h{m_input, m_limits};
      bool result = h.new_(ptr, ZuFwd<Args>(args)...); m_error = h.error(); return result;
    } else {
      if (m_error) return false;
      auto o = new (ptr) O{};
      if (read<O, Facet, ZfFieldFilter::Init>(*o, m_input, m_error, m_limits)) return true;
      o->~O(); return false;
    }
  }
  bool load(O &o) { return read<O, Facet, ZfFieldFilter::Load>(o, m_input, m_error, m_limits); }
  bool update(O &o) { return read<O, Facet, ZfFieldFilter::Upd>(o, m_input, m_error, m_limits); }
  template <template <typename> class Filter = SaveField, typename Out>
  static bool save(Out &out, const O &o) {
    saveObject<O, Facet, Filter>(out, o); return !out.failed();
  }
private:
  ZuCSpan m_input;
  Limits m_limits;
  Error m_error;
};

template <typename O, typename Facet> class Handler<O, Facet, Kind::Object> :
  public ZuIf<liveRead<O, Facet>(), Live<O, Facet>, Indexed<O, Facet>> {
  using Base = ZuIf<liveRead<O, Facet>(), Live<O, Facet>, Indexed<O, Facet>>;
public:
  using Base::Base;
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

  template <template <typename> class Filter = SaveField, typename Out>
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
  template <typename> class Filter = SaveField, typename Out, typename O>
bool save(Out &out, const O &o) {
  return Handler<O, Facet>::template save<Filter>(out, o);
}
} // Zfix::Fmt

#endif /* ZfixFmt_HH */
