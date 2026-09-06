//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZfStruct JSON load/save
// - compile-time formatting
// - compile-time field matching automaton (ZuMatcher)
// - ingests unquoted keys
// - in-place overwrite decoding of string quoting, base64, base32, hex etc. 

#ifndef ZfJSON_HH
#define ZfJSON_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <math.h>

#include <zlib/ZuDecimal.hh>
#include <zlib/ZuMArray.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>

#include <zlib/ZmScratch.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtBytesFmt.hh>

#include <zlib/ZfStruct.hh>

ZuStructFacet(JSON); // canonical JSON facet, others can be defined

namespace ZfJSON {

// bytes format
using namespace ZtBytesFmt;

// number format
template <bool String_, typename Fmt_ = ZtFmt::Default>
struct NumberFmt {
  static constexpr bool String = String_;	// "1.234" instead of 1.234
  using Fmt = Fmt_;				// ZuFmt
};

// date/time format
enum { ISO = 0, FIX, CSV, Unix };
enum { Sec = 0, MSec, USec, NSec };
template <unsigned Fmt_, unsigned Unit_, int NDP_>
struct TimeFmt {
  static constexpr unsigned Fmt = Fmt_;	  // ISO | FIX | CSV | Unix
  static constexpr unsigned Unit = Unit_; // Sec | MSec | USec | NSec (Unix)
  static constexpr int NDP = NDP_;        // decimal places (FIX, Unix)
};

} // ZfJSON

namespace ZuFieldProp::JSON {

template <ZuString ID_> struct ID { }; // defaults to field ID
template <uint8_t I> struct BytesFmt { };
template <typename Fmt> struct NumberFmt { using T = Fmt; };
template <typename Fmt> struct TimeFmt { using T = Fmt; };
template <bool> struct Optional { };

// shorthand
using Base64 = BytesFmt<ZfJSON::Base64>;
using Base64URL = BytesFmt<ZfJSON::Base64URL>;
using Base32 = BytesFmt<ZfJSON::Base32>;
using Hex = BytesFmt<ZfJSON::Hex>;
using Raw = BytesFmt<ZfJSON::Raw>;

template <typename Fmt = ZtFmt::Default>
using Number = NumberFmt<ZfJSON::NumberFmt<false, Fmt>>;
template <typename Fmt = ZtFmt::Default>
using String = NumberFmt<ZfJSON::NumberFmt<true, Fmt>>;

template <uint8_t Scale, int8_t NDP>
using ISO = TimeFmt<ZfJSON::TimeFmt<ZfJSON::ISO, Scale, NDP>>;
template <uint8_t Scale, int8_t NDP>
using FIX = TimeFmt<ZfJSON::TimeFmt<ZfJSON::FIX, Scale, NDP>>;
template <uint8_t Scale, int8_t NDP>
using CSV = TimeFmt<ZfJSON::TimeFmt<ZfJSON::CSV, Scale, NDP>>;
template <uint8_t Scale, int8_t NDP>
using Unix = TimeFmt<ZfJSON::TimeFmt<ZfJSON::Unix, Scale, NDP>>;

// shorthand for Optional<true>
using Opt = Optional<true>;

// GetID<Field> - ZuStringT
// - gets the JSON-specific ID for the field
// - the Field is passed because the value defaults to Field::id()
template <
  typename Field,
  bool = HasValue<typename Field::Props, ID>{}>
struct GetID_ {
  using T = ZuStringT<Field::id()>;
};
template <typename Field>
struct GetID_<Field, true> {
  using T = GetValue<typename Field::Props, ID>;
};
template <typename Field>
using GetID = typename GetID_<Field>::T;

// obtain a consteval typelist of field IDs suitable for use with ZuMatcher_<>
template <typename> struct GetIDs_;
template <typename ...Field>
struct GetIDs_<ZuTypeList<Field...>> {
  using T = ZuTypeList<GetID<Field>...>;
};
template <typename O>
struct GetIDs_ : public GetIDs_<ZuFields<O>> { };
template <typename U>
using GetIDs = typename GetIDs_<U>::T;

// GetBytesFmt - ZuConstant<uint8_t>
template <typename Props, bool = HasValue<Props, BytesFmt>{}>
struct GetBytesFmt_ {
  using T = ZuConstant<uint8_t, ZfJSON::Base64>; // default
};
template <typename Props>
struct GetBytesFmt_<Props, true> {
  using T = GetValue<Props, BytesFmt>;
};
template <typename Props>
using GetBytesFmt = typename GetBytesFmt_<Props>::T;

// GetNumberFmt - ZuFmt
template <typename Props, bool = HasType<Props, NumberFmt>{}>
struct GetNumberFmt_ {
  using T = ZfJSON::NumberFmt<false, ZtFmt::Default>; // default
};
template <typename Props>
struct GetNumberFmt_<Props, true> {
  using T = GetType<Props, NumberFmt>;
};
template <typename Props>
using GetNumberFmt = typename GetNumberFmt_<Props>::T;

// GetTimeFmt - ZfJSON::TimeFmt::{Fmt,Scale,NDP}
template <typename Props, bool = HasType<Props, TimeFmt>{}>
struct GetTimeFmt_ { using T = ZfJSON::TimeFmt<ZfJSON::ISO, 0, 3>; };
template <typename Props>
struct GetTimeFmt_<Props, true> { using T = GetType<Props, TimeFmt>; };
template <typename Props>
using GetTimeFmt = typename GetTimeFmt_<Props>::T;

// GetOptional
template <
  typename Props,
  bool = HasValue<Props, Optional>{}>
struct GetOptional_ {
  using T = ZuFalse; // default
};
template <typename Props>
struct GetOptional_<Props, true> {
  using T = GetValue<Props, Optional>;
};
template <typename Props>
using GetOptional = typename GetOptional_<Props>::T;

} // ZuFieldProp::JSON

namespace ZfJSON {

// --- input functions

ZuInline constexpr bool isspace__(char c) {
  return ((c >= '\t' && c <= '\r') || c == ' ');
}

// a JSON string is "..."
// - this implementation intentionally assumes UTF8
// - backquotes are \" \/
// - white space is one of \b \f \n \r \t
// - unicode is \uXXXX (hex)

// bos() finds the beginning of a string, skipping white space
// - -1 is returned if a non-white-space character other than " is encountered
ZfExtern int bos(ZuCSpan span);

// eos() finds end of string (EOS) in 1-pass, mutating the contents as
// necessary if back-quoted characters are embedded
// - leaves strings without embedded backquotes as-is (fast path)
// - ... but null-terminates them by overwriting the trailing "
// - mutates back-quoted strings in-place (slow path)
// - any mutation is reduction, shifting down the remainder of the string
// - if mutated, zero-byte padding is performed at end
// - a pair of offsets are returned - {output, input}
//   - output is the end of the (possibly mutated) string
//   - input is past the end of the input string, i.e. past the terminating "
// - returns {-1, -1} if no terminating " is found
// - Example: foo\\nbar\" -> foo\nbar\0\0 returning { 7, 9 }
ZfExtern ZuTuple<int, int> eos(ZuSpan<char> data);

// bok() finds the beginning of a key, skipping white space
// - -1 is returned if [^a-zA-Z0-9_$] is encountered
//   (bizarrely, JavaScript has $ as a permitted character in keys)
ZfExtern int bok(ZuCSpan span);

// eok() finds end of key (EOK) in 1-pass
// - no mutation is necessary
ZfExtern unsigned eok(ZuCSpan span);

struct Node_HeapID : public ZuStringT<"ZfJSON.Node"> { };

// node in a scan tree
class AnyNode {
  AnyNode() = delete;
  AnyNode(const AnyNode &) = delete;
  AnyNode &operator =(const AnyNode &) = delete;
  AnyNode(AnyNode &&) = delete;
  AnyNode &operator =(AnyNode &&) = delete;

public:
  int		type;

  AnyNode(int type_) : type(type_) { }
  virtual ~AnyNode() = default;

  template <typename Data>
  bool has() const;

  template <typename Data>
  decltype(auto) data(this auto &&);

  ZuDerive(Field, (ZuTuple<ZuCSpan, ZuPtr<AnyNode>>));

  // built-in array sizes (can be exceeded by heap allocation)
  static constexpr unsigned LNodeSize = 512;
  static constexpr int ArraySize =
    (LNodeSize - sizeof(ZtArray<ZuPtr<AnyNode>>)) / sizeof(ZuPtr<AnyNode>);
  static constexpr int ObjectSize =
    (LNodeSize - sizeof(ZtArray<Field>)) / sizeof(Field);
  ZuAssert(ArraySize > 0);
  ZuAssert(ObjectSize > 0);

  struct Null { };
  ZuDerive(Array, (ZtBuiltin<
      ZtArray<ZuPtr<AnyNode>, ZtArrayHeapID_<Node_HeapID>>, ArraySize>));
  ZuDerive(Object, (ZtBuiltin<
      ZtArray<Field, ZtArrayHeapID_<Node_HeapID>>, ObjectSize>));
  ZuDerive(String, ZuSpan<char>);
  ZuDerive(Number, ZuSpan<char>);
  struct True { };
  struct False { };

  using TL = ZuTypeList<Null, Array, Object, String, Number, True, False>;

  template <typename T>
  using Index = ZuTypeIndex<T, TL>;

  decltype(auto) operator [](this auto &&self, unsigned i) {
    return ZuFwdLike<decltype(self)>((self.template data<Array>())[i]);
  }
};

// value type enum (with same values as the AnyNode typelist indices)
namespace ValueTC {
  enum {
    _ = AnyNode::Index<AnyNode::Null>{},	Null = _,
    A = AnyNode::Index<AnyNode::Array>{},	Array = A,
    O = AnyNode::Index<AnyNode::Object>{},	Object = O,
    S = AnyNode::Index<AnyNode::String>{},	String = S,
    N = AnyNode::Index<AnyNode::Number>{},	Number = N,
    T = AnyNode::Index<AnyNode::True>{},	True = T,
    F = AnyNode::Index<AnyNode::False>{},	False = F
  };
}

template <typename Data, typename Heap = ZuVoid>
class Node_ : public Heap, public AnyNode {
  Node_(const Node_ &) = delete;
  Node_ &operator =(const Node_ &) = delete;
  Node_(Node_ &&) = delete;
  Node_ &operator =(Node_ &&) = delete;

public:
  using AnyNode::TL;

  Node_() : AnyNode{ZuTypeIndex<Data, TL>{}()} { }
  template <typename ...Args>
  Node_(Args &&...args) :
    AnyNode{ZuTypeIndex<Data, TL>{}()},
    data(ZuFwd<Args>(args)...) { }
  ~Node_() = default;

  Data	data;
};

template <typename Data>
struct Node : public Node_<Data, ZmHeap_<Node_HeapID, Node_<Data>>> {
  using Base = Node_<Data, ZmHeap_<Node_HeapID, Node_<Data>>>;
  using Base::Base;
  template <typename ...Args,
    decltype(Base(ZuDeclVal<Args &&>()...), int()) = 0>
  Node(Args &&...args) : Base(ZuFwd<Args>(args)...) { }
};

template <typename Data>
inline bool AnyNode::has() const {
  return type == ZuTypeIndex<Data, TL>{};
}

template <typename Data>
inline decltype(auto) AnyNode::data(this auto &&self) {
  using Self = decltype(self);
  using NodeT = Node<Data>;
  return ZuFwdLike<Self>(ZuFwdLike<Self, NodeT>(self).data);
}

using NodeArray = typename AnyNode::Array;
using CNodeArray = const NodeArray;

// scan JSON, build parse tree
ZfExtern ZuTuple<int, ZuPtr<AnyNode>> scan(ZuSpan<char> span);
ZfExtern ZuTuple<int, ZuPtr<AnyNode>> scan(ZuPtr<AnyNode>, ZuSpan<char> span);

struct ScanLimits {
  unsigned size;
  unsigned depth;
  unsigned nodes;
  unsigned string;
};

namespace ScanError {
  enum {
    OK = 0,
    Syntax,
    Size,
    Depth,
    Nodes,
    String,
    Duplicate
  };
}

struct ScanResult {
  int offset = -1;
  int error = ScanError::Syntax;
  ZuPtr<AnyNode> root;

  explicit operator bool() const { return error == ScanError::OK; }
};

// strict RFC 8259 scan for bounded untrusted input; builds the normal node tree
ZfExtern ScanResult scanStrict(ZuSpan<char>, const ScanLimits &);

template <typename Data, typename ...Args>
inline auto newNode(Args && ...args) {
  using T = Node<Data>;
  return ZuPtr<T>{new T(ZuFwd<Args>(args)...)};
}

// a JSON top-level is either an object { ... } or an array [ ... ]
ZfExtern ZuTuple<int, int> botl(ZuCSpan span);

// boc() returns the beginning of a colon key/value separator
// - returns -1 if the input is invalid or no colon is found
int boc(ZuCSpan span);

// bov() returns the beginning of a value together with the type of the value
// - returns {-1, -1} if the input is corrupt or no value is found
ZfExtern ZuTuple<int, int> bov(ZuCSpan span);

// Note: no eov_String(), eos() is used for both keys and values

// eov_Null() moves past "null", validating the remainder of the input
// - returns -1 if the input is too short or doesn't match
ZfExtern int eov_Null(ZuCSpan span);

// eov_Array() scans an array, allocating and returning a new Node
// - returns {offset, node}
// - returns {-1, nullptr} on invalid input
ZfExtern ZuTuple<int, ZuPtr<AnyNode>> eov_Array(ZuSpan<char> span);

// eov_Object() scans an object, allocating and returning a new Node
// - returns {offset, node}
// - returns {-1, nullptr} on invalid input
ZfExtern ZuTuple<int, ZuPtr<AnyNode>> eov_Object(ZuSpan<char> span);

// eov_Number() scans for the end of a number without calculating the value
// - JSON specifies an optional exponent [eE]N where N is potentially negative
// - returns offset
// - returns -1 on invalid input
ZfExtern int eov_Number(ZuCSpan span);

// eov_Decimal() scans a number as ZuDecimal
// - returns {offset, value}
// - returns {-1, {}} on invalid input
ZfExtern ZuTuple<int, ZuDecimal> eov_Decimal(ZuCSpan span);

// eov_Float() scans a number as double
// - returns {offset, value}
// - returns {-1, NaN} on invalid input
ZfExtern ZuTuple<int, double> eov_Float(ZuCSpan span);

// eov_True() moves past "true", validating the remainder of the input
// - returns -1 if the input is too short or doesn't match
ZfExtern int eov_True(ZuCSpan span);

// eov_False() moves past "false", validating the remainder of the input
// - returns -1 if the input is too short or doesn't match
ZfExtern int eov_False(ZuCSpan span);

// bod() finds the beginning of a delimiter within an object or array
template <char Close>
inline ZuTuple<int, char> bod(ZuCSpan span) {
  unsigned n = span.length();

  if (ZuUnlikely(n < 1)) goto bad;

  {
    char c;

    for (unsigned o = 0; o < n; o++) {
      c = span[o];
      if (c == ',') return {o + 1, ','};
      if (ZuLikely(c == Close)) return {o + 1, Close};
      if (!isspace__(c)) break;
    }
  }
bad:
  return {-1, -1};
}

// --- output functions

template <typename S>
inline void quote(S &s, ZuCSpan v) {
  s << '"';
  for (unsigned i = 0, n = v.length(); i < n; i++) {
    uint32_t u32;
    unsigned l8 =
      ZuUTF8::in(reinterpret_cast<const uint8_t *>(&v[i]), n - i, u32);
    if (ZuUnlikely(!l8)) break;
    if (ZuLikely(l8 == 1)) {
      switch (u32) {
	case '\\':
	case '"': s << '\\' << char(u32); break;
	case '\b': s << "\\b"; break;
	case '\f': s << "\\f"; break;
	case '\n': s << "\\n"; break;
	case '\r': s << "\\r"; break;
	case '\t': s << "\\t"; break;
	default: s << char(u32); break;
      }
    } else  {
      uint16_t u16[2];
      unsigned l16 = ZuUTF16::out(u16, 2, u32);
      char buf[10] = { '\\', 'u' };
      {
	ZuStream s_(&buf[2], 8);
	s_ << ZuBoxed(u16[0]).fmt<ZuFmt::Hex<false, ZuFmt::Right<4>>>();
      }
      s << ZuCSpan(&buf[0], 6);
      if (l16 > 1) {
	{
	  ZuStream s_(&buf[2], 8);
	  s_ << ZuBoxed(u16[1]).fmt<ZuFmt::Hex<false, ZuFmt::Right<4>>>();
	}
	s << ZuCSpan(&buf[0], 6);
      }
    }
  }
  s << '"';
}

// --- field save/load

struct AsObject;	// as JSON object {...}
template <unsigned ElemCode, typename ElemProps = ZuTypeList<>>
struct AsArray;		// as JSON array [...]
template <unsigned ValCode, typename ValProps = ZuTypeList<>>
struct AsMap;		// as JSON object {...} with homogeneous values
struct AsString;	// as JSON string "..."

// if fields are defined, default to AsObject
template <typename O, typename Facet, typename = ZuFields<O, Facet>>
struct AsDeflt_ { using T = AsObject; };
// ... otherwise fall back to AsString
template <typename O, typename Facet>
struct AsDeflt_<O, Facet, ZuTypeList<>> { using T = AsString; };
struct AsDeflt {
  template <typename O, typename Facet>
  using Handler = typename AsDeflt_<O, Facet>::T::template Handler<O, Facet>;
};

} // ZfJSON

ZfJSON::AsDeflt ZfJSON_Fmt(...); // default

namespace ZfJSON {

template <typename O>
using As = decltype(ZfJSON_Fmt(ZuDeclVal<O *>()));

// save an individual field
template <
  typename Facet, template <typename> class Filter, typename Field,
  typename S, typename O>
bool saveField(S &s, const O &o, bool first);

// save an individual value
template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props,
  typename S, typename T>
void saveValue(S &s, const T &v);

// load an individual value
template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
auto loadValue(AnyNode *);

// discriminated union handling
template <typename ...Ts>
struct Union : public ZuUnion<void, const AnyNode *, Ts...> {
  ZuDerive_(Union, (ZuUnion<void, const AnyNode *, Ts...>));
  friend inline AsObject ZfJSON_Fmt(Union *);
};

template <typename T> struct IsUnion_ : public ZuFalse { };
template <typename ...Ts>
struct IsUnion_<Union<Ts...>> : public ZuTrue { };
template <typename T>
using IsUnion = IsUnion_<ZuDecay<T>>;

// pointer-to-object handling
template <typename U, bool IsPtr = ZuTraits<U>::IsPointer>
struct IsObjPtr__ { using T = ZuFalse; };
template <typename U>
struct IsObjPtr__<U, true> {
  using T = ZuBool<ZuTraits<decltype(*(ZuDeclVal<const U &>()))>::IsComposite>;
};
template <typename U, typename = As<U>>
struct IsObjPtr_ { using T = ZuFalse; };
template <typename U>
struct IsObjPtr_<U, AsDeflt> {
  using T = typename IsObjPtr__<U>::T;
};
template <typename U>
using IsObjPtr = typename IsObjPtr_<U>::T;

// save/load handler for object-formatted types {...}
struct AsObject {
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;

    template <typename Field>
    using CtorIndex = ZuFieldProp::GetCtor<typename Field::Props>;

    using AllFields = ZuFields<O, Facet>;
    using LoadFields = ZuTypeGrep<ZfFieldFilter::Load, AllFields>;
    using SaveFields = ZuTypeGrep<ZfFieldFilter::Save, AllFields>;
    using CtorFields_ = ZuTypeGrep<ZfFieldFilter::Ctor, AllFields>;
    using CtorFields = ZuTypeSort<CtorIndex, CtorFields_>;
    using InitFields = ZuTypeGrep<ZfFieldFilter::Init, AllFields>;
    using UpdFields = ZuTypeGrep<ZfFieldFilter::Upd, AllFields>;
    using DelFields = ZuTypeGrep<ZfFieldFilter::Del, AllFields>;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      using Fields = ZuTypeGrep<Filter, AllFields>;
      s << '{';
      bool first = true;
      ZuUnroll::all<Fields>([&s, &o, &first]<typename Field>() {
	if (saveField<Facet, Filter, Field>(s, o, first)) first = false;
      });
      s << '}';
    }

    const AnyNode	*node;
    int			lookup[SaveFields::N];

    static bool valid(const AnyNode *node) {
      return node->has<AnyNode::Object>();
    }

    Handler(const AnyNode *node_) : node{node_} {
      for (unsigned i = 0; i < SaveFields::N; i++) lookup[i] = -1;
      if (node->has<AnyNode::Object>()) {
	constexpr auto matcher =
	  ZuMatcher<ZuFieldProp::JSON::GetIDs<SaveFields>>();
	unsigned matched = 0;
	const auto &fields = node->data<AnyNode::Object>();
	for (unsigned i = 0, n = fields.length(); i < n; i++) {
	  auto j = matcher.exact(fields[i].p<0>());
	  if (j >= 0) {
	    if (lookup[j] < 0) ++matched;
	    lookup[j] = i;
	    if (matched >= SaveFields::N) break;
	  }
	}
      }
    }

    template <typename Field>
    bool hasField() const {
      enum { I = ZuTypeIndex<Field, SaveFields>{} };
      return lookup[I] >= 0;
    }

    template <template <typename> class Filter, typename Field>
    auto loadField() const {
      enum { TypeCode = Field::Type::Code };
      using Props = typename Field::Props;
      using T = typename Field::T;
      using R = decltype(
	loadValue<Facet, Filter, TypeCode, Props, T>(ZuDeclVal<AnyNode *>()));
      {
	enum { I = ZuTypeIndex<Field, SaveFields>{} };
	auto j = lookup[I];
	if (j >= 0) {
	  auto &fields = node->data<AnyNode::Object>();
	  AnyNode *node = fields[j].template p<1>();
	  return loadValue<Facet, Filter, TypeCode, Props, T>(node);
	}
      }
      if constexpr (ZfFieldTC::IsVec<TypeCode>{}) {
	// ZuMArray needs an underlying reference
	static const NodeArray _;
	return R(_);
      } else
	return R{Field::deflt()};
    }

    template <typename ...Field>
    struct Ctor {
      template <typename ...Args>
      static O ctor(const Handler &handler, Args &&...args) {
	return O(
	  ZuFwd<Args>(args)...,
	  handler.loadField<ZfFieldFilter::Load, Field>()...);
      }
      template <typename ...Args>
      static O *alloc(const Handler &handler, Args &&...args) {
	return new O(
	  ZuFwd<Args>(args)...,
	  handler.loadField<ZfFieldFilter::Load, Field>()...);
      }
      template <typename ...Args>
      static void new_(void *o, const Handler &handler, Args &&...args) {
	new (o) O(
	  ZuFwd<Args>(args)...,
	  handler.loadField<ZfFieldFilter::Load, Field>()...);
      }
    };
    template <typename ...Args>
    O ctor(Args &&...args) const {
      if constexpr (!InitFields::N) // exploit guaranteed copy elision
	return ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
      else {
	O o = ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
	ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	  Field::set(o, this->loadField<ZfFieldFilter::Load, Field>());
	});
	return o;
      }
    }
    template <typename ...Args>
    O *alloc(Args &&...args) const {
      O *o = ZuTypeApply<Ctor, CtorFields>::alloc(
	*this, ZuFwd<Args>(args)...);
      ZuUnroll::all<InitFields>([this, o]<typename Field>() {
	Field::set(*o, this->loadField<ZfFieldFilter::Load, Field>());
      });
      return o;
    }
    template <typename ...Args>
    void new_(void *o_, Args &&...args) const {
      ZuTypeApply<Ctor, CtorFields>::new_(o_, *this, ZuFwd<Args>(args)...);
      O &o = *static_cast<O *>(o_);
      ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	Field::set(o, this->loadField<ZfFieldFilter::Load, Field>());
      });
    }

    void load(O &o) const {
      ZuUnroll::all<LoadFields>([this, &o]<typename Field>() {
	Field::set(o, this->loadField<ZfFieldFilter::Load, Field>());
      });
    }
    void update(O &o) const {
      ZuUnroll::all<UpdFields>([this, &o]<typename Field>() {
	using Props = typename Field::Props;
	if (ZuTypeIn<ZuFieldProp::Reset, Props>{}() || hasField<Field>())
	  Field::set(o, this->loadField<ZfFieldFilter::Upd, Field>());
      });
    }
  };

  // discriminated unions
  // - on load, any actual parse-tree node is retained as const AnyNode *
  // - the parse tree must remain alive until the union is resolved
  // - on save, the member is dispatched
  //   (callers are always required to resolve unions before saving)
  template <typename ...Ts, typename Facet>
  struct Handler<Union<Ts...>, Facet> {
    using O = Union<Ts...>;
    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      auto type = o.type();
      if (ZuUnlikely(type < 2)) { s << "null"; return; }
      ZuSwitch::dispatch<O::N - 2>(type - 2, [&s, &o](auto I_) {
	static constexpr unsigned I = I_ + 2;
	using V = typename O::template Type<I>;
	const auto &v = o.template p<I>();
	if constexpr (!IsObjPtr<V>{})
	  As<V>::template Handler<V, Facet>::template save<Filter>(s, v);
	else {
	  if (ZuLikely(v)) {
	    using U = ZuDecay<decltype(*v)>;
	    As<U>::template Handler<U, Facet>::template save<Filter>(s, *v);
	  } else
	    s << "null";
	}
      });
    }

    const AnyNode	*node;

    static bool valid(const AnyNode *node) {
      return node != nullptr;
    }
    Handler(const AnyNode *node_) : node{node_} {
      if (ZuUnlikely(!valid(node))) node = nullptr;
    }
    O ctor() const { return O(node); }
    O *alloc() const { return new O(node); }
    void new_(void *o) const { new (o) O(node); }
    void load(O &o) const { o = node; }
    void update(O &o) const { o = node; }
  };
};

// LoadVec wraps NodeArray, parsing each span on demand
template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
struct LoadVec :
  public ZuMArray<LoadVec<Facet, Filter, TypeCode, Props, T>, CNodeArray, T>
{
  ZuDerive_(LoadVec, (ZuMArray<LoadVec, CNodeArray, T>));
  using Base::underlying;
  T get(unsigned i) const & {
    return loadValue<Facet, Filter, TypeCode, Props, T>(underlying[i]);
  }
  template <typename V> void set(unsigned, const V &) { } // unused
};

// save/load handler for array-formatted types [...]
template <unsigned ElemCode, typename ElemProps>
struct AsArray {
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      s << '[';
      for (unsigned i = 0, n = ZuTraits<O>::length(o); i < n; i++) {
	if (ZuLikely(i)) s << ',';
	saveValue<Facet, Filter, ElemCode, ElemProps>(s, o[i]);
      }
      s << ']';
    }

    const AnyNode	*node;

    static bool valid(const AnyNode *node) {
      return node->has<NodeArray>();
    }

    Handler(const AnyNode *node_) : node{node_} { }

    using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
    using LoadVec_ =
      LoadVec<Facet, ZfFieldFilter::Load, ElemCode, ElemProps, Elem>;
    template <typename ...Args>
    O ctor(Args &&...args) const {
      if (ZuUnlikely(!valid(node)))
	return O(ZuFwd<Args>(args)...);
      return O(ZuFwd<Args>(args)..., LoadVec_(node->data<NodeArray>()));
    }
    template <typename ...Args>
    O *alloc(Args &&...args) const {
      if (ZuUnlikely(!valid(node)))
	return new O(ZuFwd<Args>(args)...);
      else
	return new O(ZuFwd<Args>(args)..., LoadVec_(node->data<NodeArray>()));
    }
    template <typename ...Args>
    void new_(void *o, Args &&...args) const {
      if (ZuUnlikely(!valid(node)))
	new (o) O(ZuFwd<Args>(args)...);
      else
	new (o) O(ZuFwd<Args>(args)..., LoadVec_(node->data<NodeArray>()));
    }

    void load(O &o) const {
      if (ZuLikely(valid(node)))
	o = LoadVec_(node->data<NodeArray>());
    }
    void update(O &o) const {
      if (ZuUnlikely(!valid(node))) return;
      const auto &nodes = node->data<NodeArray>();
      unsigned n = ZuTraits<O>::length(o);
      unsigned m = nodes.length();
      if (n > m) n = m;
      if constexpr (ElemCode == ZfFieldTC::UDT) {
	if constexpr (!IsObjPtr<Elem>{}) {
	  using ElemHandler = typename As<Elem>::template Handler<Elem, Facet>;
	  for (unsigned i = 0; i < n; i++)
	    ElemHandler{nodes[i]}.update(o[i]);
	} else {
	  using U = ZuDecay<decltype(*(ZuDeclVal<const Elem &>()))>;
	  using ElemHandler = typename As<U>::template Handler<U, Facet>;
	  for (unsigned i = 0; i < n; i++) {
	    auto &p = o[i];
	    auto &node = nodes[i];
	    if (ElemHandler::valid(node)) {
	      auto handler = ElemHandler{node};
	      if (ZuLikely(p))
		handler.update(*p);
	      else
		p = handler.alloc();
	    }
	  }
	}
      } else {
	for (unsigned i = 0; i < n; i++)
	  o[i] = loadValue<
	    Facet, ZfFieldFilter::Upd, ElemCode, ElemProps, Elem>(nodes[i]);
      }
    }
  };
};

// save/load handler for map-formatted types {...}
template <unsigned ValCode, typename ValProps>
struct AsMap {
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;
    using Key = typename O::Key;
    using Val = typename O::Val;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      s << '{';
      bool first = true;
      {
	auto i = o.citer();
	while (auto node = i()) {
	  if (!first) s << ',';
	  first = false;
	  quote(s, node->key());
	  s << ':';
	  saveValue<Facet, Filter, ValCode, ValProps>(s, node->val());
	}
      }
      s << '}';
    }

    const AnyNode	*node;

    static bool valid(const AnyNode *node) {
      return node->has<AnyNode::Object>();
    }

    Handler(const AnyNode *node_) : node{node_} { }

    template <template <typename> class Filter, bool Update = false>
    void load_(O &o) const {
      if (ZuUnlikely(!valid(node))) return;
      const auto &fields = node->data<AnyNode::Object>();
      for (unsigned i = 0, n = fields.length(); i < n; i++) {
	const auto &field = fields[i];
	Key key{field.template p<0>()};
	if constexpr (Update) o.del(key);
	o.add(
	  ZuMv(key),
	  loadValue<Facet, Filter, ValCode, ValProps, Val>(
	    field.template p<1>()));
      }
    }

    O ctor() const {
      O o;
      load_<ZfFieldFilter::Load>(o);
      return o;
    }
    O *alloc() const {
      auto o = new O();
      load_<ZfFieldFilter::Load>(*o);
      return o;
    }
    void new_(void *p_) const {
      auto p = new (p_) O();
      load_<ZfFieldFilter::Load>(*p);
    }

    void load(O &o) const {
      o.clean();
      load_<ZfFieldFilter::Load>(o);
    }
    void update(O &o) const {
      load_<ZfFieldFilter::Upd, true>(o);
    }
  };
};

// save/load handler for string-formatted types "..."
// - custom handler skeleton:
// struct Fmt {
//   template <typename O>
//   struct Handler {
//     template <typename S>
//     void save(S &s, const O &o) { ...; }
//     O load(ZuCSpan span) { return O{...}; }
//   }
// };
// class A {
//   ...
//   friend inline Fmt ZfJSON_StringFmt(A *); // bind Fmt to A
// };

// temporary on-stack string buffer for quoting
ZuDerive(QuoteBuf,
  (ZtString<
    ZtStringBuiltin<128,
      ZtStringHeapID<"ZfJSON.Quote",
	ZtStringSharded<true>>>>));

struct AsStringDeflt {	// default string formatter
  template <typename O>
  struct Handler {
    template <typename S>
    ZuInline static void save(S &s, const O &o) {
      QuoteBuf buf;
      buf << o;
      quote(s, buf);
    }
    ZuInline static O load(ZuCSpan span) { // string is already unquoted
      return O(span);
    }
  };
};

} // ZfJSON

ZfJSON::AsStringDeflt ZfJSON_StringFmt(...);

namespace ZfJSON {

struct AsString {
  template <typename O_, typename>
  struct Handler {
    using O = O_;
    using Fmt = decltype(ZfJSON_StringFmt(ZuDeclVal<O *>()));
    using Handler_ = typename Fmt::template Handler<O>;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) { Handler_::save(s, o); }
 
    const AnyNode	*node;

    static bool valid(const AnyNode *node) {
      return node->has<AnyNode::String>();
    }

    Handler(const AnyNode *node_) : node{node_} { }

    ZuCSpan span() const {
      if (!valid(node)) return {};
      return node->data<AnyNode::String>();
    }

    O ctor() const { return Handler_::load(span()); }
    O *alloc() const { return new O(Handler_::load(span())); }
    void new_(void *o) const { new (o) O(Handler_::load(span())); }
    void load(O &o) const { o = Handler_::load(span()); }
    void update(O &o) const { o = Handler_::load(span()); }
  };
};

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props,
  typename S, typename T_>
inline void saveValue_(S &s, const T_ &v_)
{
  using T = ZuDecay<T_>;
  if constexpr (
      TypeCode == ZfFieldTC::CString ||
      TypeCode == ZfFieldTC::String)
    quote(s, v_);
  else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    constexpr unsigned Fmt = ZuFieldProp::JSON::GetBytesFmt<Props>{};
    if constexpr (Fmt == ZfJSON::Base64) {
      ZuBSpan v{v_};
      unsigned n = ZuBase64::enclen(v.length());
      auto buf = ZmScratch(uint8_t, n);
      buf.length(n);
      buf.length(ZuBase64::encode(buf, v));
      s << '"' << ZuCSpan(buf) << '"';
    } else if constexpr (Fmt == ZfJSON::Base64URL) {
      ZuBSpan v{v_};
      unsigned n = ZuBase64URL::enclen(v.length());
      auto buf = ZmScratch(uint8_t, n);
      buf.length(n);
      buf.length(ZuBase64URL::encode(buf, v));
      s << '"' << ZuCSpan(buf) << '"';
    } else if constexpr (Fmt == ZfJSON::Base32) {
      ZuBSpan v{v_};
      unsigned n = ZuBase32::enclen(v.length());
      auto buf = ZmScratch(uint8_t, n);
      buf.length(n);
      buf.length(ZuBase32::encode(buf, v));
      s << '"' << ZuCSpan(buf) << '"';
    } else if constexpr (Fmt == ZfJSON::Hex) {
      ZuBSpan v{v_};
      unsigned n = ZuHex::enclen(v.length());
      auto buf = ZmScratch(uint8_t, n);
      buf.length(n);
      buf.length(ZuHex::encode(buf, v));
      s << '"' << ZuCSpan(buf) << '"';
    } else if constexpr (Fmt == ZfJSON::Raw) {
      quote(s, v_);
    }
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    bool v = v_;
    s << (v ? "true" : "false");
  } else if constexpr (
      TypeCode == ZfFieldTC::Int8 ||
      TypeCode == ZfFieldTC::Int16 ||
      TypeCode == ZfFieldTC::Int32 ||
      TypeCode == ZfFieldTC::Int64 ||
      TypeCode == ZfFieldTC::Int128 ||
      TypeCode == ZfFieldTC::UInt8 ||
      TypeCode == ZfFieldTC::UInt16 ||
      TypeCode == ZfFieldTC::UInt32 ||
      TypeCode == ZfFieldTC::UInt64 ||
      TypeCode == ZfFieldTC::UInt128) {
    using Fmt = ZuFieldProp::JSON::GetNumberFmt<Props>;
    if constexpr (ZuIsBoxed<T>{}) {
      if constexpr (
	  Fmt::String ||
	  bool(ZuFieldProp::HasEnum<Props>{}) ||
	  bool(ZuFieldProp::HasFlags<Props>{}) ||
	  bool(ZuTypeIn<ZuFieldProp::Hex, Props>{})) {
	s << '"' << ZfFieldPrintInt<Props, typename Fmt::Fmt, T>(v_) << '"';
      } else {
	s << ZfFieldPrintInt<Props, typename Fmt::Fmt, T>(v_);
      }
    } else {
      using B = ZuBox<ZfFieldTC::Type<TypeCode>>;
      auto v = B{v_};
      if constexpr (!ZuFieldProp::JSON::GetOptional<Props>{}) {
	if constexpr (ZuFieldProp::HasEnum<Props>{}) {
	  if (v < 0) { s << "null"; return; }
	} else {
	  if (!*v) { s << "null"; return; }
	}
      }
      if constexpr (
	  Fmt::String ||
	  bool(ZuFieldProp::HasEnum<Props>{}) ||
	  bool(ZuFieldProp::HasFlags<Props>{}) ||
	  bool(ZuTypeIn<ZuFieldProp::Hex, Props>{})) {
	s << '"' << ZfFieldPrintInt<Props, typename Fmt::Fmt, B>(v) << '"';
      } else {
	s << ZfFieldPrintInt<Props, typename Fmt::Fmt, B>(v);
      }
    }
  } else if constexpr (TypeCode == ZfFieldTC::Float) {
    using Fmt = ZuFieldProp::JSON::GetNumberFmt<Props>;
    double v = v_;
    if constexpr (!ZuFieldProp::JSON::GetOptional<Props>{})
      if (ZuNull(v)) { s << "null"; return; }
    if constexpr (Fmt::String) s << '"';
    bool negative = v < 0;
    if (negative) { s << '-'; v = -v; }
    if (ZuUnlikely(v == ZuCmp<double>::inf())) { s << "Infinity"; return; }
    int e = 0;
    if (ZuUnlikely(v < 1.0e-9 || v >= 1.0e18)) {
      e = log10(v);
      if (e < 0) --e;
      v = v * pow(10, -e);
      if (v >= 10.0) { v /= 10.0; ++e; }
    }
    s << ZuBoxed(v).fmt<typename Fmt::Fmt>();
    if (e) { s << 'e'; if (e > 0) s << '+'; s << e; }
    if constexpr (Fmt::String) s << '"';
  } else if constexpr (TypeCode == ZfFieldTC::Fixed) {
    using Fmt = ZuFieldProp::JSON::GetNumberFmt<Props>;
    ZuFixed v = v_;
    if constexpr (!ZuFieldProp::JSON::GetOptional<Props>{})
      if (!*v) { s << "null"; return; }
    if constexpr (Fmt::String) s << '"';
    s << v.fmt<typename Fmt::Fmt>();
    if constexpr (Fmt::String) s << '"';
  } else if constexpr (TypeCode == ZfFieldTC::Decimal) {
    using Fmt = ZuFieldProp::JSON::GetNumberFmt<Props>;
    ZuDecimal v = v_;
    if constexpr (!ZuFieldProp::JSON::GetOptional<Props>{})
      if (!*v) { s << "null"; return; }
    if constexpr (Fmt::String) s << '"';
    s << v.fmt<typename Fmt::Fmt>();
    if constexpr (Fmt::String) s << '"';
  } else if constexpr (
      TypeCode == ZfFieldTC::Time ||
      TypeCode == ZfFieldTC::DateTime) {
    using Fmt = ZuFieldProp::JSON::GetTimeFmt<Props>;
    if constexpr (Fmt::Fmt == ZfJSON::Unix) {
      ZuTime v{v_};
      if constexpr (!ZuFieldProp::JSON::GetOptional<Props>{})
	if (!*v) { s << "null"; return; }
      if constexpr (Fmt::Unit == ZfJSON::Sec) {
	s << '"' << ZuBoxed(v.sec());
	if constexpr (Fmt::NDP)
	  s << '.' << ZuBoxed(v.nsec()).fmt<ZuFmt::Frac<9, Fmt::NDP>>();
	s << '"';
      } else if constexpr (Fmt::Unit == ZfJSON::MSec) {
	int128_t t = int128_t(v.sec());
	int64_t f = v.nsec();
	t = t * 1000U + (f / 1000000U);
	f %= 1000000U;
	s << '"' << ZuBoxed(t);
	if constexpr (Fmt::NDP)
	  s << '.' << ZuBoxed(f).fmt<ZuFmt::Frac<6, Fmt::NDP>>();
	s << '"';
      } else if constexpr (Fmt::Unit == ZfJSON::USec) {
	int128_t t = int128_t(v.sec());
	int64_t f = v.nsec();
	t = t * 1000000U + (v.nsec() / 1000U);
	f %= 1000U;
	s << '"' << ZuBoxed(t);
	if constexpr (Fmt::NDP)
	  s << '.' << ZuBoxed(f).fmt<ZuFmt::Frac<3, Fmt::NDP>>();
	s << '"';
      } else if constexpr (Fmt::Unit == ZfJSON::NSec) {
	int128_t t = int128_t(v.sec());
	t = t * 1000000000U + v.nsec();
	s << '"' << ZuBoxed(t) << '"';
      }
    } else if constexpr (Fmt::Fmt == ZfJSON::CSV) {
      ZuDateTime v{v_};
      if constexpr (!ZuFieldProp::JSON::GetOptional<Props>{})
	if (!*v) { s << "null"; return; }
      auto &fmt = ZmTLS<ZuDateTimeFmt::CSV, (int Props::*){}>();
      s << '"' << v.fmt(fmt) << '"';
    } else if constexpr (Fmt::Fmt == ZfJSON::FIX) {
      auto &fmt = ZmTLS<ZuDateTimeFmt::FIX<Fmt::NDP>, (int Props::*){}>();
      ZuDateTime v{v_};
      if constexpr (!ZuFieldProp::JSON::GetOptional<Props>{})
	if (!*v) { s << "null"; return; }
      s << '"' << v.fmt(fmt) << '"';
    } else if constexpr (Fmt::Fmt == ZfJSON::ISO) {
      auto &fmt = ZmTLS<ZuDateTimeFmt::ISO, (int Props::*){}>();
      ZuDateTime v{v_};
      if constexpr (!ZuFieldProp::JSON::GetOptional<Props>{})
	if (!*v) { s << "null"; return; }
      s << '"' << v.fmt(fmt) << '"';
    }
  } else if constexpr (TypeCode == ZfFieldTC::UDT) {
    if constexpr (!IsObjPtr<T>{})
      As<T>::template Handler<T, Facet>::template save<Filter>(s, v_);
    else {
      if (ZuUnlikely(!v_)) { s << "null"; return; }
      using U = ZuDecay<decltype(*v_)>;
      As<U>::template Handler<U, Facet>::template save<Filter>(s, *v_);
    }
  }
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props,
  typename S, typename T_>
inline void saveValue(S &s, const T_ &v)
{
  using T = ZuDecay<T_>;
  if constexpr (!ZfFieldTC::IsVec<TypeCode>{}) {
    saveValue_<Facet, Filter, TypeCode, Props>(s, v);
  } else {
    unsigned n = ZuTraits<T>::length(v);
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    s << '[';
    for (unsigned i = 0; i < n; i++) {
      if (i) s << ',';
      saveValue_<Facet, Filter, ElemCode, Props>(s, v[i]);
    }
    s << ']';
  }
}

template <
  typename Facet, template <typename> class Filter,
  typename Field,
  typename S, typename O>
inline bool saveField(S &s, const O &o, bool first)
{
  enum { TypeCode = Field::Type::Code };
  using Props = typename Field::Props;
  auto save = [&s, first](const auto &v) mutable -> bool {
    auto fieldID = ZuFieldProp::JSON::GetID<Field>{}().cspan();
    if (!first) s << ',';
    s << '"' << fieldID << "\":";
    saveValue<Facet, Filter, TypeCode, Props>(s, v);
    return true;
  };
  if constexpr (ZuFieldProp::JSON::GetOptional<Props>{}) {
    if constexpr (
	TypeCode == ZfFieldTC::CString ||
	TypeCode == ZfFieldTC::String) {
      ZuCSpan v = Field::get(o);
      if (!v) return false;
      return save(v);
    } else if constexpr (
	TypeCode == ZfFieldTC::Bytes) {
      ZuBSpan v = Field::get(o);
      if (!v) return false;
      return save(v);
    } else if constexpr (
	TypeCode == ZfFieldTC::Int8 ||
	TypeCode == ZfFieldTC::Int16 ||
	TypeCode == ZfFieldTC::Int32 ||
	TypeCode == ZfFieldTC::Int64 ||
	TypeCode == ZfFieldTC::Int128 ||
	TypeCode == ZfFieldTC::UInt8 ||
	TypeCode == ZfFieldTC::UInt16 ||
	TypeCode == ZfFieldTC::UInt32 ||
	TypeCode == ZfFieldTC::UInt64 ||
	TypeCode == ZfFieldTC::UInt128) {
      auto &&v = Field::get(o);
      if constexpr (ZuFieldProp::HasEnum<Props>{}) {
	if (v < 0) return false;
      } else {
	if (ZuNull(v)) return false;
      }
      return save(v);
    } else if constexpr (
	TypeCode == ZfFieldTC::Float ||
	TypeCode == ZfFieldTC::Fixed ||
	TypeCode == ZfFieldTC::Decimal ||
	TypeCode == ZfFieldTC::Time ||
	TypeCode == ZfFieldTC::DateTime ||
	TypeCode == ZfFieldTC::UDT) {
      auto &&v = Field::get(o);
      if (ZuNull(v)) return false;
      return save(v);
    } else
      return save(Field::get(o));
  } else
    return save(Field::get(o));
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline T loadValue_(AnyNode *node)
{
  auto type = node->type;

  if (type == ValueTC::Null && !IsUnion<T>{}) return ZuCmp<T>::null();

  if constexpr (TypeCode == ZfFieldTC::CString) {
    if (ZuUnlikely(type != ValueTC::String)) return nullptr;
    // eos() in-place null-terminates the string
    return node->data<AnyNode::String>().data();
  } else if constexpr (TypeCode == ZfFieldTC::String) {
    if (ZuUnlikely(type != ValueTC::String))
      return ZuCmp<T>::null();
    return T(node->data<AnyNode::String>());
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    if (ZuUnlikely(type != ValueTC::String))
      return ZuCmp<T>::null();
    auto &span = node->data<AnyNode::String>();
    unsigned n = span.length();
    if (ZuUnlikely(!n)) return ZuCmp<T>::null();
    ZuSpan<uint8_t> bytes(span);
    constexpr unsigned Fmt = ZuFieldProp::JSON::GetBytesFmt<Props>{};
    // encodings are idempotently decoded in-place
    // - zero-fill trailing bytes are used for idempotence
    // - the final trailing byte is used to stash the number of
    //   padding bytes from the original base32/64 encoding
    if constexpr (Fmt == ZfJSON::Base64) {
      unsigned m = ZuBase64::declen(n), l;
      if (bytes[n - 1] >= 4) {
	l = ZuBase64::decode({&bytes[0], m}, bytes);
	memset(&bytes[m], 0, (n - 1) - l);
	bytes[n - 1] = m - l; // guaranteed to be < 4
      } else {
	l = m - bytes[n - 1];
      }
      bytes.trunc(l);
      return T(bytes);
    } else if constexpr (Fmt == ZfJSON::Base64URL) {
      // permit padding
      unsigned m = ZuBase64URL::declen(n), l;
      if (bytes[n - 1] >= 4) {
	l = ZuBase64URL::decode({&bytes[0], m}, bytes);
	memset(&bytes[m], 0, (n - 1) - l);
	bytes[n - 1] = m - l; // guaranteed to be < 4
      } else {
	l = m - bytes[n - 1];
      }
      bytes.trunc(l);
      return T(bytes);
    } else if constexpr (Fmt == ZfJSON::Base32) {
      unsigned m = ZuBase32::declen(n), l;
      if (bytes[n - 1] >= 8) {
	l = ZuBase32::decode({&bytes[0], m}, bytes);
	memset(&bytes[m], 0, (n - 1) - l);
	bytes[n - 1] = m - l; // guaranteed to be < 8
      } else {
	l = m - bytes[n - 1];
      }
      bytes.trunc(l);
      return T(bytes);
    } else if constexpr (Fmt == ZfJSON::Hex) {
      unsigned m = ZuHex::declen(n);
      if (bytes[n - 1]) {
	m = ZuHex::decode({&bytes[0], m}, bytes);
	memset(&bytes[m], 0, n - m);
      }
      bytes.trunc(m);
      return T(bytes);
    } else if constexpr (Fmt == ZfJSON::Raw) {
      return T(bytes);
    }
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    switch(type) {
      case ValueTC::True:
	return true;
      case ValueTC::False:
	return false;
      default:
	return ZuCmp<T>::null();
    }
    ZuUnreachable();
  } else if constexpr (
      TypeCode == ZfFieldTC::Int8 ||
      TypeCode == ZfFieldTC::Int16 ||
      TypeCode == ZfFieldTC::Int32 ||
      TypeCode == ZfFieldTC::Int64 ||
      TypeCode == ZfFieldTC::Int128 ||
      TypeCode == ZfFieldTC::UInt8 ||
      TypeCode == ZfFieldTC::UInt16 ||
      TypeCode == ZfFieldTC::UInt32 ||
      TypeCode == ZfFieldTC::UInt64 ||
      TypeCode == ZfFieldTC::UInt128) {
    using Fmt = ZuFieldProp::JSON::GetNumberFmt<Props>;
    switch (type) {
      case ValueTC::String: {
	if constexpr (ZuIsBoxed<T>{}) {
	  using Scan = ZfFieldScanInt<Props, typename Fmt::Fmt, T>;
	  return Scan{node->data<AnyNode::String>()}.value;
	} else {
	  using B = ZuBox<ZfFieldTC::Type<TypeCode>>;
	  using Scan = ZfFieldScanInt<Props, typename Fmt::Fmt, B>;
	  return T(Scan{node->data<AnyNode::String>()}.value.val());
	}
      }
      case ValueTC::Number: {
	auto d = eov_Decimal(node->data<AnyNode::Number>());
	if (d.p<0>() < 0) return ZuCmp<T>::null();
	auto v = T(d.p<1>().floor());
	ZfFieldLimit<Props>(v);
	return v;
      }
      default:
	return ZuCmp<T>::null();
    }
    ZuUnreachable();
  } else if constexpr (
      TypeCode == ZfFieldTC::Float ||
      TypeCode == ZfFieldTC::Fixed ||
      TypeCode == ZfFieldTC::Decimal) {
    switch (type) {
      case ValueTC::String: {
	ZuCSpan span = node->data<AnyNode::String>();
	if constexpr (
	    TypeCode == ZfFieldTC::Decimal ||
	    TypeCode == ZfFieldTC::Fixed) {
	  ZuDecimal d{span};
	  ZfFieldLimit<Props>(d);
	  if constexpr (TypeCode == ZfFieldTC::Decimal)
	    return d;
	  else {
	    if (!*d) return ZuFixed{};
	    if constexpr (ZuFieldProp::HasNDP<Props>{})
	      return ZuFixed{d, ZuFieldProp::GetNDP<Props>{}};
	    else
	      return ZuFixed{d};
	  }
	} else {
	  auto v = ZuBox<double>{span}.val();
	  ZfFieldLimit<Props>(v);
	  return v;
	}
	ZuUnreachable();
      } break;
      case ValueTC::Number: {
	ZuCSpan span = node->data<AnyNode::Number>();
	if constexpr (
	    TypeCode == ZfFieldTC::Decimal ||
	    TypeCode == ZfFieldTC::Fixed) {
	  auto d = eov_Decimal(span);
	  if (d.p<0>() < 0) return T{};
	  auto v = d.p<1>();
	  ZfFieldLimit<Props>(v);
	  if constexpr (TypeCode == ZfFieldTC::Decimal)
	    return v;
	  else {
	    if (!*v) return ZuFixed{};
	    if constexpr (ZuFieldProp::HasNDP<Props>{})
	      return ZuFixed{v, ZuFieldProp::GetNDP<Props>{}};
	    else
	      return ZuFixed{v};
	  }
	} else {
	  auto d = eov_Float(span);
	  if (d.p<0>() < 0) return ZuCmp<T>::null();
	  auto v = d.p<1>();
	  ZfFieldLimit<Props>(v);
	  return v;
	}
      } break;
      default:
	return ZuCmp<T>::null();
    }
  } else if constexpr (
      TypeCode == ZfFieldTC::Time ||
      TypeCode == ZfFieldTC::DateTime) {
    using Fmt = ZuFieldProp::JSON::GetTimeFmt<Props>;
    ZuCSpan span;
    switch (type) {
      case ValueTC::String:
	span = node->data<AnyNode::String>();
	break;
      case ValueTC::Number:
	span = node->data<AnyNode::Number>();
	break;
      default:
	return ZuCmp<T>::null();
    }
    if constexpr (Fmt::Fmt == ZfJSON::Unix) {
      auto d = eov_Decimal(span);
      if (d.p<0>() < 0) return ZuCmp<T>::null();
      auto &v = d.p<1>();
      if constexpr (Fmt::Unit == ZfJSON::MSec) {
	v.value /= 1000;
      } else if constexpr (Fmt::Unit == ZfJSON::USec) {
	v.value /= 1000000;
      } else if constexpr (Fmt::Unit == ZfJSON::NSec) {
	v.value /= 1000000000;
      }
      if constexpr (ZuIs_<T, ZuTime>{})
	return ZuTime{v};
      else
	return ZuDateTime{ZuTime{v}};
    } else if constexpr (Fmt::Fmt == ZfJSON::CSV) {
      auto &fmt = ZmTLS<ZuDateTimeScan::CSV, (int Props::*){}>();
      ZuDateTime v;
      if (v.scan(fmt, span) < 0) return ZuCmp<T>::null();
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    } else if constexpr (Fmt::Fmt == ZfJSON::FIX) {
      auto &fmt = ZmTLS<ZuDateTimeScan::FIX, (int Props::*){}>();
      ZuDateTime v;
      if (v.scan(fmt, span) < 0) return ZuCmp<T>::null();
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    } else if constexpr (Fmt::Fmt == ZfJSON::ISO) {
      auto &fmt = ZmTLS<ZuDateTimeScan::ISO, (int Props::*){}>();
      ZuDateTime v;
      if (v.scan(fmt, span) < 0) return ZuCmp<T>::null();
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    }
  } else if constexpr (TypeCode == ZfFieldTC::UDT) {
    if constexpr (!IsObjPtr<T>{}) {
      using Handler = typename As<T>::template Handler<T, Facet>;
      if (ZuUnlikely(!Handler::valid(node))) return ZuCmp<T>::null();
      return Handler{node}.ctor();
    } else {
      using U = ZuDecay<decltype(*(ZuDeclVal<const T &>()))>;
      using Handler = typename As<U>::template Handler<U, Facet>;
      if (ZuUnlikely(!Handler::valid(node))) return nullptr;
      return Handler{node}.alloc();
    }
  }
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline auto loadValue(AnyNode *node)
{
  if constexpr (!ZfFieldTC::IsVec<TypeCode>{}) {
    return loadValue_<Facet, Filter, TypeCode, Props, T>(node);
  } else {
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    using Actual = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
    using Elem = ZuIf<
      ElemCode >= ZfFieldTC::Int8 && ElemCode <= ZfFieldTC::UInt128 &&
      bool(ZuIsBoxed<Actual>{}), Actual, ZfFieldTC::Type<ElemCode>>;
    using LoadVec_ = LoadVec<Facet, Filter, ElemCode, Props, Elem>;
    if (ZuUnlikely(!node->has<NodeArray>())) {
      static const NodeArray _;
      return LoadVec_(_);
    }
    return LoadVec_(node->data<NodeArray>());
  }
}

template <
  typename Facet = ZuFacet::JSON,
  template <typename> class Filter = ZfFieldFilter::Save,
  typename S, typename O>
inline S &save(S &s, const O &v) {
  if constexpr (!IsObjPtr<O>{})
    As<O>::template Handler<O, Facet>::template save<Filter>(s, v);
  else {
    if (ZuUnlikely(!v))
      s << "null";
    else {
      using U = ZuDecay<decltype(*v)>;
      As<U>::template Handler<U, Facet>::template save<Filter>(s, *v);
    }
  }
  return s;
}
template <
  typename Facet = ZuFacet::JSON,
  typename S, typename O>
ZuInline S &saveUpd(S &s, const O &v) {
  return save<Facet, ZfFieldFilter::Upd>(s, v);
}
template <
  typename Facet = ZuFacet::JSON,
  typename S, typename O>
ZuInline S &saveDel(S &s, const O &v) {
  return save<Facet, ZfFieldFilter::Del>(s, v);
}

template <typename O, typename Facet = ZuFacet::JSON>
auto handler(const AnyNode *node) {
  return typename As<O>::template Handler<O, Facet>{node};
}

} // ZfJSON

#endif /* ZfJSON_HH */
