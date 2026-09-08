//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZfStruct URI query load/save
// - compile-time formatting
// - in-place overwrite decoding of percent-escaping, base64, base32, hex etc. 

// the many and varied ways of flattening structures into endpoint paths
// and URI queries, sigh...
//
// this implementation intentionally excludes built-in support for legacy
// frameworks like Zope with its :list annotation for arrays;
// supported frameworks, stack-ranked by mid-2026 adoption:
// - Java/Spring
// - Python/Django
// - Node/Express
// - Golang (Render/Chi/Echo/Gin)
// - Ruby/Rails
// - PHP/Laravel
//
// Whether in the request header or in the x-www-form-urlencoded body,
// URI query strings produced/consumed by these frameworks fall
// into three groups. JSONs and corresponding URI queries for each group
// (URI queries are shown without percent encoding):
//
// Java/Python/Golang - "Member" format
// ------------------
// {"a":{"b":1},"c":2}			a.b=1&c=2
// {"a":[1,2,3]}			a[0]=1&a[1]=2&a[2]=3
// {"a":{"b":[1,2,3]},"c":4}		a.b[0]=1&a.b[1]=2&a.b[2]=3&c=4
// {"a":[{"b":[1,2]},{"c":[3]}]}	a[0].b[0]=1&a[0].b[1]=2&a[1].c[0]=3
//
// Node/PHP - "Array" format
// --------
// {"a":{"b":1},"c":2}			a[b]=1&c=2
// {"a":[1,2,3]}			a[0]=1&a[1]=2&a[2]=3
// {"a":{"b":[1,2,3]},"c":4}		a[b][0]=1&a[b][1]=2&a[b][2]=3&c=4
// {"a":[{"b":[1,2]},{"c":[3]}]}	a[0][b][0]=1&a[0][b][1]=2&a[1][c][0]=3
//
// Ruby - "List" format
// ----
// {"a":{"b":1},"c":2}			a[b]=1&c=2
// {"a":[1,2,3]}			a[]=1&a[]=2&a[]=3
// {"a":{"b":[1,2,3]},"c":4}		a[b][]=1&a[b][]=2&a[b][]=3&c=4
// {"a":[{"b":[1,2]},{"c":[3]}]}	a[][b][]=1&a[][b][]=2&a[][c][]=3
//
// two types of object nesting are observed:
//   Member	Java/Python/Golang		a.b
//   Array	Node/PHP/Ruby			a[b]
//
// ... and two types of array subscripting:
//   Array	Node/PHP			a[0]
//   List	Ruby				a[]
//
// common practices for primitive value arrays at the leaf of the key
// that are observed in the wild:
//   Bare	repeated bare			a=1&a=2&a=3
//   Delimited	delimited with , 		a=1,2,3
//   Delimited	delimited with |		a=1|2|3
//   Delimited	annotated and delimited		a[]=1,2,3
//   Delimited	wrapped and delimited		a=[1,2,3]
//
// Bare repetition is typically not used together with
// array-formatted object nesting, so Bare implies Member formatting
// for nested objects
//
// Delimited is a parameterized format, with the following parameters:
//
//		Annotated Wrapped Delimiter
//		--------- ------- ---------
// a=1,2,3	false,    false,  ','
// a=1|2|3	false,    false,  '|'
// a[]=1|2|3	true,     false,  '|'
// a=[1,2,3]	false,    true,   ','
//
// both arrays and nested objects can also be embedded as JSON
//
// IncrementalURI permits loading from a path query then enriching
// the same object from a x-www-form-urlencoded body without overwriting
// already-loaded data
//
// finally, Zrest permits appending a signature to a query or body
//
// recap: the top-level is always an object whose fields are ZfStruct-defined;
// individual fields are one of:
// - a primitive type with non-UDT typecode (String, Int*, etc.)
//   - per-field StringFmt/BytesFmt/NumberFmt/TimeFmt control the format
// - a vector type with vector typecode (vector of primitive types)
//   - the ArrayFmt defined for the mapping controls the array formatting
//     - canonical mapping is URI, default ArrayFmt is Member
//   - no recursion is possible because the element type is primitive
//   - per-field StringFmt/BytesFmt/NumberFmt/TimeFmt control the element format
// - a nested UDT type dispatched via the ZfURI_Fmt() mechanism:
//   - AsString (default for any type without ZfStruct-defined fields)
//     - by default operator << is used for saving and the type is expected
//       to construct itself from a string (ZuCSpan passed to the constructor)
//     - a custom load/save handler can be defined and bound by
//       declaring ZfURI_StringFmt(T *) in T's namespace (see AsString)
//   - AsObject (default for any type with ZfStruct-defined fields)
//     - the ObjectFmt configured for the mapping controls the formatting
//     - default mapping is URI, default ObjectFmt is Member
//   - AsArray (ZfURI::AsArray ZfURI_Fmt(T *) must be declared in T's namespace)
//     - the ArrayFmt configured for the mapping controls the formatting
//       - default mapping is URI, default ArrayFmt is Member
//     - if the elements are UDT AsObject, Member or Array format must be used
//     - otherwise each element is recursively formatted as if a top-level,
//       with its keys prefixed accordingly
//   - AsJSON (ZfURI::AsJSON ZfURI_Fmt(T *) must be declared in T's namespace)
//     - the format is delegated to ZfJSON and any JSON formatting
//       defined for the field is used
//     - the same mapping as for ZfURI is used (URI if canonical), so
//       JSON-in-URI must be configured via the URI mapping not the
//       canonical JSON mapping

#ifndef ZfURI_HH
#define ZfURI_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <math.h>

#include <zlib/ZuDecimal.hh>
#include <zlib/ZuMArray.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZuPercent.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>

#include <zlib/ZmRBTree.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZePlatform.hh>

ZuStructFacet(URI); // canonical URI facet, others can be defined
ZuStructFacet(IncrementalURI); // variant with Incremental = true

// --- configuration (per-mapping)

// NTP (named template parameters):
//
// ZfURIConfig(URI,				// configure canonical URI
//   ZfURI::ObjectFmt<ZfURI::Array,		// object format is array
//     ZfURI::ArrayFmt<ZfURI::Delimited,	// array format is delimited
//       ZfURI::Annotated<true,			// arrays are annotated []=...
// 	   ZfURI::Wrapped<true>>>>);		// arrays are wrapped [...]

namespace ZfURI { enum { Member, Array, List, Bare, Delimited }; }

// NTP defaults
struct ZfURI_DefltConfig {
  enum {
    ObjectFmt = ZfURI::Member,
    ArrayFmt = ZfURI::Array,
    Annotated = 0,
    Wrapped = 0,
    Delimiter = ',',
    Incremental = false
  };
};

// ZfURI_ObjectFmt<...> - configure object format
template <unsigned Fmt_, typename NTP = ZfURI_DefltConfig>
struct ZfURI_ObjectFmt : public NTP { enum { ObjectFmt = Fmt_ }; };

// ZfURI_ArrayFmt<...> - configure array format
template <unsigned Fmt_, typename NTP = ZfURI_DefltConfig>
struct ZfURI_ArrayFmt : public NTP { enum { ArrayFmt = Fmt_ }; };

// ZfURI_Annotated<...> - configure array annotation (a=... vs a[]=...)
template <bool _, typename NTP = ZfURI_DefltConfig>
struct ZfURI_Annotated : public NTP { enum { Annotated = _ }; };

// ZfURI_Wrapped<...> - configure array wrapping (a=... vs a=[...])
template <bool _, typename NTP = ZfURI_DefltConfig>
struct ZfURI_Wrapped : public NTP { enum { Wrapped = _ }; };

// ZfURI_Delimiter<...> - configure array element delimiter character
template <char _, typename NTP = ZfURI_DefltConfig>
struct ZfURI_Delimiter : public NTP { enum { Delimiter = _ }; };

// ZfURI_Incremental<...> - incrementally load fields
template <bool _, typename NTP = ZfURI_DefltConfig>
struct ZfURI_Incremental_ : public NTP { enum { Incremental = _ }; };
template <typename NTP = ZfURI_DefltConfig>
using ZfURI_Incremental = ZfURI_Incremental_<true, NTP>;

ZfURI_DefltConfig ZfURI_Config(...); // default

namespace ZfURI {

// resolve configuration (per facet)
// - E.g. ZfURI::Config<Facet>::ObjectFmt
template <typename Facet>
using Config = decltype(ZfURI_Config(ZuDeclVal<Facet *>()));

}

namespace ZfURIError {

  constexpr auto Component = "ZfURI"_Zu;

  inline auto nullPath() {
    return [](auto &s) { s << "null URI path component"; };
  }

}

// ZfURIConfig(Facet, Config)
// - configure URI for facet
// - must be used in top-level namespace
// - to further modify a Base facet pass tail NTP as:
//   ZfURI::Config<ZuFacet::Base>
#define ZfURIConfig(Facet, Config) \
  ZuPP_Strip(Config) ZfURI_Config(ZuFacet::Facet *);

ZfURIConfig(IncrementalURI, ZfURI_Incremental<>);

namespace ZfURI {

// temporary on-stack string buffer for quoting
// - falls back to sharded ZmVHeap if built-in size is exceeded (unlikely)
ZuDerive(QuoteBuf,
  (ZtString<
    ZtStringBuiltin<128,
      ZtStringHeapID<"ZfURI.Quote",
	ZtStringSharded<true>>>>));

// AsString save/load handler for UDTs
// - custom handler skeleton:
// struct Fmt {
//   template <typename Quote, typename O>
//   struct Handler {
//     template <typename S>
//     void save(S &s, const O &o) { ... }
//     O load(ZuCSpan span) { return O{...}; }
//   }
// };
// class A {
//   ...
//   // bind A to Fmt::Handler<Quote, A>
//   friend inline Fmt ZfURI_StringFmt(A *);
// };

struct AsStringDeflt {	// default string formatter
  template <typename Quote, typename O>
  struct Handler {
    template <typename S>
    ZuInline static void save(S &s, const O &o) {
      QuoteBuf buf;
      buf << o;
      Quote::quote(s, buf);
    }
    ZuInline static O load(ZuCSpan span) { // string is already unquoted
      return O(span);
    }
  };
};

} // ZfURI

ZfURI::AsStringDeflt ZfURI_StringFmt(...);

namespace ZfURI {

// enums are intentionally not namespaced (for brevity, they don't collide)

// bytes format - different than ZtBytesFmt
enum { Base64, Base64URL, Base32, Hex, Escaped };

// number format
template <typename Fmt_ = ZtFmt::Default>
struct NumberFmt {
  using Fmt = Fmt_;	// ZuFmt
};

// boolean format
enum {
  Bool_TRUE_FALSE, Bool_True_False, Bool_true_false, Bool_T_F, Bool_t_f,
  Bool_YES_NO, Bool_Yes_No, Bool_yes_no, Bool_Y_N, Bool_y_n,
  Bool_0_1
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

} // ZfURI

namespace ZuFieldProp::URI {

template <int8_t I> struct PathIndex { }; // endpoint path index
template <ZuString ID_> struct ID { }; // defaults to field ID
template <uint8_t I> struct BytesFmt { };
template <typename Fmt> struct NumberFmt { using T = Fmt; };
template <uint8_t I> struct BoolFmt { };
template <typename Fmt> struct TimeFmt { using T = Fmt; };

// shorthand
using Base64 = BytesFmt<ZfURI::Base64>;
using Base64URL = BytesFmt<ZfURI::Base64URL>;
using Base32 = BytesFmt<ZfURI::Base32>;
using Hex = BytesFmt<ZfURI::Hex>;
using Escaped = BytesFmt<ZfURI::Escaped>;

template <typename Fmt = ZtFmt::Default>
using Number = NumberFmt<ZfURI::NumberFmt<Fmt>>;

using Bool_TRUE_FALSE = BoolFmt<ZfURI::Bool_TRUE_FALSE>;
using Bool_True_False = BoolFmt<ZfURI::Bool_True_False>;
using Bool_true_false = BoolFmt<ZfURI::Bool_true_false>;
using Bool_T_F = BoolFmt<ZfURI::Bool_T_F>;
using Bool_t_f = BoolFmt<ZfURI::Bool_t_f>;
using Bool_YES_NO = BoolFmt<ZfURI::Bool_YES_NO>;
using Bool_Yes_No = BoolFmt<ZfURI::Bool_Yes_No>;
using Bool_yes_no = BoolFmt<ZfURI::Bool_yes_no>;
using Bool_Y_N = BoolFmt<ZfURI::Bool_Y_N>;
using Bool_y_n = BoolFmt<ZfURI::Bool_y_n>;
using Bool_0_1 = BoolFmt<ZfURI::Bool_0_1>;

template <uint8_t Scale, int8_t NDP>
using ISO = TimeFmt<ZfURI::TimeFmt<ZfURI::ISO, Scale, NDP>>;
template <uint8_t Scale, int8_t NDP>
using FIX = TimeFmt<ZfURI::TimeFmt<ZfURI::FIX, Scale, NDP>>;
template <uint8_t Scale, int8_t NDP>
using CSV = TimeFmt<ZfURI::TimeFmt<ZfURI::CSV, Scale, NDP>>;
template <uint8_t Scale, int8_t NDP>
using Unix = TimeFmt<ZfURI::TimeFmt<ZfURI::Unix, Scale, NDP>>;

// GetPathIndex<Field> - int8_t
// - gets the endpoint path index for the field
// - returns -1 if the field is part of the query string (usual case)
template <typename Props, bool = HasValue<Props, PathIndex>{}>
struct GetPathIndex_ {
  using T = ZuConstant<int8_t, -1>;
};
template <typename Props>
struct GetPathIndex_<Props, true> {
  using T = GetValue<Props, PathIndex>;
};
template <typename Props>
using GetPathIndex = typename GetPathIndex_<Props>::T;

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

// GetBytesFmt - ZuConstant<uint8_t>
template <typename Props, bool = HasValue<Props, BytesFmt>{}>
struct GetBytesFmt_ {
  using T = ZuConstant<uint8_t, ZfURI::Base64URL>; // default
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
  using T = ZfURI::NumberFmt<ZtFmt::Default>; // default
};
template <typename Props>
struct GetNumberFmt_<Props, true> {
  using T = GetType<Props, NumberFmt>;
};
template <typename Props>
using GetNumberFmt = typename GetNumberFmt_<Props>::T;

// GetBoolFmt - ZuConstant<uint8_t>
template <typename Props, bool = HasValue<Props, BoolFmt>{}>
struct GetBoolFmt_ {
  using T = ZuConstant<uint8_t, ZfURI::Bool_true_false>; // default
};
template <typename Props>
struct GetBoolFmt_<Props, true> {
  using T = GetValue<Props, BoolFmt>;
};
template <typename Props>
using GetBoolFmt = typename GetBoolFmt_<Props>::T;

// GetTimeFmt - {Fmt, Scale, NDP}
template <typename Props, bool = HasType<Props, TimeFmt>{}>
struct GetTimeFmt_ { using T = ZfURI::TimeFmt<ZfURI::ISO, 0, 3>; };
template <typename Props>
struct GetTimeFmt_<Props, true> { using T = GetType<Props, TimeFmt>; };
template <typename Props>
using GetTimeFmt = typename GetTimeFmt_<Props>::T;

} // ZuFieldProp::URI

namespace ZfURI {

// --- input functions

ZuInline constexpr bool isspace__(char c) {
  return ((c >= '\t' && c <= '\r') || c == ' ');
}

// a URI query string is ?...
// - this implementation intentionally assumes UTF8

// boq() finds the beginning of a query string
// - -1 is returned if none is found
ZfExtern int boq(ZuCSpan span);

// eos() finds end of a string (EOS) in 1-pass, mutating the contents as
// necessary if escaped characters are embedded
// - leaves strings without escaped characters as-is (fast path)
// - ... but null-terminates them by overwriting the trailing delimiter
//   (normally '=', '&' or ' ') unless the string ends with the span,
//   in which case '\0' is returned and no null-terminator is written
// - mutates escaped strings in-place (slow path)
// - any mutation is reduction, shifting down the remainder of the string
// - if mutated, zero-byte padding is performed at end
// - a pair of offsets are returned with the delimiter
//   - {output, input, delimiter}
//   - int output is the end of the (possibly mutated) string
//   - int input is past the end of the input string,
//     i.e. past the terminating delimiter
//   - char delimiter is the character that is now overwritten with '\0'
// - returns {-1, -1, 0} if no terminating delimiter is found
// - Example: "foo%20bar&" -> "foo bar\0\0\0" returning { 7, 10, '&' }
ZfExtern ZuTuple<int, int, char> eos(ZuSpan<char> data);

// eoc() is the path-component equivalent of eos(); '/' and '?' terminate
// the component and '+' is not decoded as space
ZfExtern ZuTuple<int, int, char> eoc(ZuSpan<char> data);

struct Node_HeapID : public ZuStringT<"ZfURI.Node"> { };

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

  // built-in array size (can be exceeded by heap allocation)
  static constexpr unsigned LNodeSize = 128;
  static constexpr int ArraySize =
    (LNodeSize - sizeof(ZtArray<ZuPtr<AnyNode>>)) / sizeof(ZuPtr<AnyNode>);
  ZuAssert(ArraySize > 0);

  ZuDerive(String, ZuSpan<char>);
  ZuDerive(Array, (ZtBuiltin<
      ZtArray<ZuPtr<AnyNode>, ZtArrayHeapID_<Node_HeapID>>, ArraySize>));
  ZmRBTreeKVDerive(Object, ZuCSpan, ZuPtr<AnyNode>,
    ZmRBTreeUnique<true,
      ZmRBTreeHeapID_<Node_HeapID>>);

  using TL = ZuTypeList<String, Array, Object>;

  template <typename T>
  using Index = ZuTypeIndex<T, TL>;
};

template <typename Data, typename Heap>
class Node_ : public Heap, public AnyNode {
  Node_(const Node_ &) = delete;
  Node_ &operator =(const Node_ &) = delete;
  Node_(Node_ &&) = delete;
  Node_ &operator =(Node_ &&) = delete;

public:
  using AnyNode::TL;

  Node_() : AnyNode{ZuTypeIndex<Data, TL>{}()} { }
  template <typename ...Args,
    decltype(Data(ZuDeclVal<Args &&>()...), int()) = 0>
  Node_(Args &&...args) :
    AnyNode{ZuTypeIndex<Data, TL>{}()},
    data(ZuFwd<Args>(args)...) { }
  ~Node_() = default;

  Data	data;
};

template <typename Data>
struct Node : public Node_<Data, ZmHeap_<Node_HeapID, Node_<Data, ZuVoid>>> {
  using Base = Node_<Data, ZmHeap_<Node_HeapID, Node_<Data, ZuVoid>>>;
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

template <typename Data, typename ...Args>
inline auto newNode(Args &&...args) {
  using T = Node<Data>;
  return ZuPtr<T>{new T(ZuFwd<Args>(args)...)};
}

// add a string
inline bool string(ZuPtr<AnyNode> &node, ZuSpan<char> val) {
  if (ZuLikely(!node)) {
    node = newNode<AnyNode::String>(val);
    return true;
  }
  // nodes cannot be both an array of strings and a nested object
  if (ZuUnlikely(node->has<AnyNode::Object>())) return false;
  // if currently a string, this is a repeat - promote it to array
  if (node->has<AnyNode::String>()) {
    auto string = ZuMv(node);
    node = newNode<AnyNode::Array>(2);
    node->data<AnyNode::Array>().push(ZuMv(string));
  }
  node->data<AnyNode::Array>().push(newNode<AnyNode::String>(val));
  return true;
}

// push an array element
inline ZuPtr<AnyNode> *push(ZuPtr<AnyNode> &node) {
  if (ZuUnlikely(node && (
      node->has<AnyNode::Object>() ||
      node->has<AnyNode::String>())))
    return nullptr;
  if (ZuLikely(!node)) node = newNode<AnyNode::Array>();
  auto &array = node->data<AnyNode::Array>();
  array.push(ZuPtr<AnyNode>{});
  return &array[array.length() - 1];
}

// add an array element at a specified index
inline ZuPtr<AnyNode> *elem(ZuPtr<AnyNode> &node, unsigned index) {
  if (ZuUnlikely(node && (
      node->has<AnyNode::Object>() ||
      node->has<AnyNode::String>())))
    return nullptr;
  if (ZuLikely(!node)) node = newNode<AnyNode::Array>();
  auto &array = node->data<AnyNode::Array>();
  array.ensure(index + 1);
  while (index >= array.length()) array.push(ZuPtr<AnyNode>{});
  return &array[index];
}

// add an object field
inline ZuPtr<AnyNode> *field(ZuPtr<AnyNode> &node, ZuCSpan key) {
  if (ZuUnlikely(node && (
      node->has<AnyNode::Array>() ||
      node->has<AnyNode::String>())))
    return nullptr;
  if (ZuLikely(!node)) node = newNode<AnyNode::Object>();
  auto &object = node->data<AnyNode::Object>();
  if (auto node_ = object.find(key)) return &node_->val();
  auto node_ = object.add(key, ZuPtr<AnyNode>{});
  return &node_->val();
}

// add an endpoint path component
// - path components are stored as an array under an empty object key
// - unlike query keys, generated path indices have no backing URI storage
inline ZuPtr<AnyNode> *path(ZuPtr<AnyNode> &node, unsigned index) {
  auto slot = field(node, {});
  if (ZuUnlikely(!slot)) return nullptr;
  return elem(*slot, index);
}

// coerce node to array, splitting strings using Config::Delimiter
template <typename Config>
bool asArray(ZuPtr<AnyNode> &node) {
  if (ZuLikely(node && node->has<AnyNode::Array>())) return true;
  if (ZuUnlikely(node && node->has<AnyNode::Object>())) return false;
  if (ZuUnlikely(!node)) {
    node = newNode<AnyNode::Array>();
    return true;
  }
  auto string = ZuMv(node);
  ZuSpan<char> span = string->data<AnyNode::String>();
  auto n = span.length();
  node = newNode<AnyNode::Array>();
  auto &array = node->data<AnyNode::Array>();
  if (n >= 2 && span[0] == '[' && span[n - 1] == ']') {
    span = {&span[1], n -= 2};
    span[n] = 0;
  }
  bool first = true;
  while (span) {
    unsigned i;
    for (i = 0; i < n; ++i)
      if (span[i] == Config::Delimiter) { span[i] = 0; break; }
    if (first) {
      first = false;
      string->data<AnyNode::String>() = ZuSpan<char>{span.data(), i};
      array.push(ZuMv(string));
    } else
      array.push(newNode<AnyNode::String>(span.data(), i));
    span.offset(i + 1);
    n = span.length();
  }
  return true;
}

using NodeArray = typename AnyNode::Array;
using CNodeArray = const NodeArray;
using NodeObject = typename AnyNode::Object;
using CNodeObject = const NodeObject;

// scan URI (path + query), build parse tree
ZfExtern ZuTuple<int, ZuCSpan> scanKey(ZuCSpan key);
ZfExtern ZuTuple<int, ZuPtr<AnyNode>> scan(ZuSpan<char> span);
ZfExtern ZuTuple<int, ZuPtr<AnyNode>> scan(ZuPtr<AnyNode>, ZuSpan<char> span);

// --- output functions

// this code handles the query portion of the URI, not the path
// - it intentionally does not escape [ ] { } etc.
// - see https://bugzilla.mozilla.org/show_bug.cgi?id=1152455#c6
// - escapes the following characters: space " # % & ' < = >
constexpr bool escaped(uint8_t c) {
  static constexpr uint8_t map[] = {
    0xed, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80
  };
  if (c < 32 || c >= 128) return true;
  c -= 32;
  return map[c>>3] & (1U<<(c & 7));
}

struct PercentEsc {
  static constexpr bool esc(uint8_t c) { return escaped(c); }
};

// plus() - should '+' be quoted?
// spacePlus() - should ' ' be quoted as '+'?

struct PercentPath : public PercentEsc {
  static constexpr bool term(uint8_t c) {
    return c == '/' || c == '?' || escaped(c);
  }
  static constexpr bool plus() { return false; }
  static constexpr bool spacePlus() { return false; }
};

struct PercentQuery : public PercentEsc {
  static constexpr bool term(uint8_t c) { return escaped(c); }
  static constexpr bool plus() { return true; }
  static constexpr bool spacePlus() { return false; }
};

template <bool Body>
struct PercentQuote : public PercentEsc {
  static constexpr bool term(uint8_t) { return false; }
  static constexpr bool plus() { return Body; }
  static constexpr bool spacePlus() { return Body; }
};

template <bool Body = false>
struct URIQuote {
  using Policy = PercentQuote<Body>;

  // in the body, + should be used for space
  template <typename S>
  static void quote(S &s, ZuCSpan v) {
    ZuPercent::Codec<Policy>::print(s, ZuBSpan{v});
  }
};

struct PathQuote {
  struct Policy : public ZuPercent::NoTerm, public ZuPercent::NoPlus {
    static constexpr bool esc(uint8_t c) {
      return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
	c == '~');
    }
  };

  template <typename S>
  static void quote(S &s, ZuCSpan v) {
    ZuPercent::Codec<Policy>::print(s, v);
  }
};

// output stream wrappers with delimiter

template <typename S, char Delimiter>
struct OutStream {
  S	&stream;
  bool	first = true;
  char	initial = 0;

  template <typename U, decltype(ZuDeclVal<S &>().S::operator <<(
      ZuDeclVal<const U &>()), int()) = 0>
  inline OutStream &operator <<(const U &v) {
    stream << v;
    return *this;
  }

  void delimit() {
    if (first) {
      first = false;
      if (initial) stream << initial;
      return;
    }
    stream << Delimiter;
  }
};
template <typename S> using OutQuery = OutStream<S, '&'>;

// --- field save/load

struct AsObject;	// as object
template <unsigned ElemCode, typename ElemProps = ZuTypeList<>>
struct AsArray;		// as array
struct AsString;	// as string
struct AsJSON;		// as embedded JSON

template <typename ...Ts>
struct Union : public ZuUnion<void, const AnyNode *, Ts...> {
friend inline AsObject ZfURI_Fmt(Union *);
  ZuDerive_(Union, (ZuUnion<void, const AnyNode *, Ts...>));
};

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

} // ZfURI

ZfURI::AsDeflt ZfURI_Fmt(...);	// default

namespace ZfURI {

// filter fields that form part of the endpoint path
template <typename Field>
struct PathFilter : public ZuBool<(
  ZuFieldProp::URI::GetPathIndex<typename Field::Props>{}() >= 0)> { };
template <typename Field>
using PathIndex = ZuFieldProp::URI::GetPathIndex<typename Field::Props>;
template <typename Fields>
using PathFields = ZuTypeSort<PathIndex, ZuTypeGrep<PathFilter, Fields>>;

// filter fields that form part of the URI query string
template <template <typename> class Filter_>
struct QueryFilter {
  template <typename Field>
  using Filter = ZuBool<
    bool(Filter_<Field>{}) && 
    (ZuFieldProp::URI::GetPathIndex<typename Field::Props>{}() < 0)>;
};

// dispatch saved objects to AsObject, AsArray or AsString
template <typename O>
using As = decltype(ZfURI_Fmt(ZuDeclVal<O *>()));

template <typename U, bool IsPtr = ZuTraits<U>::IsPointer>
struct IsObjPtr__ { using T = ZuFalse; };
template <typename U>
struct IsObjPtr__<U, true> {
  using T = ZuBool<ZuTraits<decltype(*(ZuDeclVal<const U &>()))>::IsComposite>;
};
template <typename U, typename = As<U>>
struct IsObjPtr_ { using T = ZuFalse; };
template <typename U>
struct IsObjPtr_<U, AsDeflt> { using T = typename IsObjPtr__<U>::T; };
template <typename U>
using IsObjPtr = typename IsObjPtr_<U>::T;

template <typename T, bool = IsObjPtr<ZuDecay<T>>{}>
struct ObjType_ { using T_ = ZuDecay<T>; };
template <typename T>
struct ObjType_<T, true> {
  using T_ = ZuDecay<decltype(*(ZuDeclVal<const ZuDecay<T> &>()))>;
};
template <typename T>
using ObjType = typename ObjType_<T>::T_;
template <typename T>
ZuInline decltype(auto) obj_(const T &v) {
  if constexpr (IsObjPtr<ZuDecay<T>>{}) return *v;
  else return v;
}

template <typename T> struct IsUnion_ : public ZuFalse { };
template <typename ...Ts>
struct IsUnion_<Union<Ts...>> : public ZuTrue { };
template <typename T>
using IsUnion = IsUnion_<ZuDecay<T>>;

// save an individual field
template <
  typename Facet, template <typename> class Filter, typename Quote,
  typename Field,
  typename S, typename O>
void saveField(S &, const O &, ZuCSpan prefix);

// save an individual value
template <
  typename Facet, template <typename> class Filter, typename Quote,
  unsigned TypeCode, typename Props,
  typename S, typename T>
void saveValue(S &s, const T &v, ZuCSpan prefix);

// load an individual value
template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
auto loadValue(const AnyNode *);
template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
auto loadValue(ZuPtr<AnyNode> &);

// save/load handler for object-formatted types
// - object-formatted in URI query strings means multiple individual fields
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

    enum { Incremental = Config<Facet>::Incremental };

    template <template <typename> class Filter, typename Quote, typename S>
    static void save(S &s, const O &o, ZuCSpan prefix) {
      using Fields = ZuTypeGrep<Filter, AllFields>;
      ZuUnroll::all<Fields>([&s, &o, &prefix]<typename Field>() {
	saveField<Facet, Filter, Quote, Field>(s, o, prefix);
      });
    }

    const AnyNode	*node;

    static bool valid(const AnyNode *node) {
      return node && node->has<AnyNode::Object>();
    }

    Handler(const AnyNode *node_) : node{node_} { }

    template <template <typename> class Filter, typename Field, typename L>
    decltype(auto) loadField(L &&l) const;

    template <typename ...Field>
    struct Ctor {
      template <typename ...Args>
      static O ctor(const Handler &handler, Args &&...args) {
	return O(
	  ZuFwd<Args>(args)...,
	  handler.loadField<ZfFieldFilter::Load, Field>(
	    []<typename V>(V &&v, bool) { return ZuFwd<V>(v); })...);
      }
      template <typename ...Args>
      static O *alloc(const Handler &handler, Args &&...args) {
	return new O(
	  ZuFwd<Args>(args)...,
	  handler.loadField<ZfFieldFilter::Load, Field>(
	    []<typename V>(V &&v, bool) { return ZuFwd<V>(v); })...);
      }
      template <typename ...Args>
      static void new_(void *o, const Handler &handler, Args &&...args) {
	new (o) O(
	  ZuFwd<Args>(args)...,
	  handler.loadField<ZfFieldFilter::Load, Field>(
	    []<typename V>(V &&v, bool) { return ZuFwd<V>(v); })...);
      }
    };
    template <typename ...Args>
    O ctor(Args &&...args) const {
      if constexpr (!InitFields::N) // exploit guaranteed copy elision
	return ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
      else {
	O o = ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
	ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	  this->template loadField<ZfFieldFilter::Load, Field>(
	      [&o]<typename V>(V &&v, bool deflt) {
	    if (!Incremental || !deflt)
	      Field::set(o, ZuFwd<V>(v));
	  });
	});
	return o;
      }
    }
    template <typename ...Args>
    O *alloc(Args &&...args) const {
      O *o = ZuTypeApply<Ctor, CtorFields>::alloc(
	*this, ZuFwd<Args>(args)...);
      ZuUnroll::all<InitFields>([this, o]<typename Field>() {
	this->template loadField<ZfFieldFilter::Load, Field>(
	    [o]<typename V>(V &&v, bool deflt) {
	  if (!Incremental || !deflt)
	    Field::set(*o, ZuFwd<V>(v));
	});
      });
      return o;
    }
    template <typename ...Args>
    void new_(void *o_, Args &&...args) const {
      ZuTypeApply<Ctor, CtorFields>::new_(o_, *this, ZuFwd<Args>(args)...);
      O &o = *static_cast<O *>(o_);
      ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	loadField<ZfFieldFilter::Load, Field>([&o]<typename V>(V &&v, bool deflt) {
	  if (!Incremental || !deflt)
	    Field::set(o, ZuFwd<V>(v));
	});
      });
    }

    void load(O &o) const {
      ZuUnroll::all<LoadFields>([this, &o]<typename Field>() {
	this->loadField<ZfFieldFilter::Load, Field>([&o]<typename V>(V &&v, bool deflt) {
	  if (!Incremental || !deflt)
	    Field::set(o, ZuFwd<V>(v));
	});
      });
    }
    void update(O &o) const {
      ZuUnroll::all<UpdFields>([this, &o]<typename Field>() {
	using Props = typename Field::Props;
	this->loadField<ZfFieldFilter::Upd, Field>([&o]<typename V>(V &&v, bool deflt) {
	  if (ZuTypeIn<ZuFieldProp::Reset, Props>{}() || !deflt)
	    Field::set(o, ZuFwd<V>(v));
	});
      });
    }
  };

  // Any actual parse-tree node is retained without inspecting its shape.
  // The raw-node state is non-owning; retain the scan tree until resolving it.
  // Empty, unresolved, and null alternatives are omitted when saving.
  template <typename ...Ts, typename Facet>
  struct Handler<Union<Ts...>, Facet> {
    using O = Union<Ts...>;

    const AnyNode *node;

    template <typename L>
    static bool dispatch_(const O &o, L &&l) {
      auto type = o.type();
      if (ZuUnlikely(type < 2)) return false;
      bool output = false;
      ZuSwitch::dispatch<O::N - 2>(type - 2, [&o, &l, &output](auto I_) {
	static constexpr unsigned I = I_ + 2;
	using V = typename O::template Type<I>;
	const auto &v = o.template p<I>();
	if constexpr (!IsObjPtr<V>{}) {
	  l(v);
	  output = true;
	} else {
	  if (ZuLikely(v)) {
	    l(*v);
	    output = true;
	  }
	}
      });
      return output;
    }

    template <template <typename> class Filter, typename Quote, typename S>
    static void save(S &s, const O &o, ZuCSpan prefix) {
      dispatch_(o, [&s, prefix]<typename V>(const V &v) {
	using MemberHandler = typename As<V>::template Handler<V, Facet>;
	MemberHandler::template save<Filter, Quote>(s, v, prefix);
      });
    }

    static bool valid(const AnyNode *node) {
      return node != nullptr;
    }
    Handler(const AnyNode *node_) : node{node_} { }
    O ctor() const { return O(node); }
    O *alloc() const { return new O(node); }
    void new_(void *o) const { new (o) O(node); }
    void load(O &o) const { o = node; }
    void update(O &o) const { o = node; }
  };
};

// LoadVec wraps a NodeArray, parsing each span on demand
template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
struct LoadVec :
  public ZuMArray<LoadVec<Facet, Filter, TypeCode, Props, T>, CNodeArray, T>
{
  ZuDerive_(LoadVec, (ZuMArray<LoadVec, CNodeArray, T>))
  using Base::underlying;
  T get(unsigned i) const & {
    return loadValue<Facet, Filter, TypeCode, Props, T>(underlying[i]);
  }
  template <typename V> void set(unsigned, const V &) { } // unused
};

// save/load handler for array-formatted types
template <unsigned ElemCode, typename ElemProps>
struct AsArray {
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;

    using Config = ZfURI::Config<Facet>;

    // the top-level cannot be AsArray, so prefix must be non-null
    template <template <typename> class Filter, typename Quote, typename S>
    static void save(S &s, const O &o, ZuCSpan prefix) {
      unsigned n = ZuTraits<O>::length(o);
      if constexpr (
	  Config::ArrayFmt == Member || Config::ArrayFmt == Array ||
	  Config::ArrayFmt == List || Config::ArrayFmt == Bare) {
	// append array indices to prefix
	unsigned nestedSize = prefix.length();
	if constexpr (Config::ArrayFmt == Member) // .I + formatter spare
	  nestedSize += 2 + Zu_ntoa::Log10_MaxLog<sizeof(nestedSize)>::N;
	else if constexpr (Config::ArrayFmt == Array) // [I]
	  nestedSize += 2 + Zu_ntoa::Log10_MaxLog<sizeof(nestedSize)>::N;
	else if constexpr (Config::ArrayFmt == List) // []
	  nestedSize += 2;
	auto nested = ZmScratch(char, nestedSize);
	nested << prefix;
	if constexpr (Config::ArrayFmt == Member)
	  nested << '.';
	else if constexpr (Config::ArrayFmt == Array)
	  nested << '[';
	else if constexpr (Config::ArrayFmt == List)
	  nested << "[]";
	if constexpr (Config::ArrayFmt == Member || Config::ArrayFmt == Array) {
	  unsigned nestedLen = nested.length();
	  for (unsigned i = 0; i < n; i++) {
	    nested.length(nestedLen);
	    nested << ZuBoxed(i);
	    if constexpr (Config::ArrayFmt == Array) nested << ']';
	    saveValue<Facet, Filter, Quote, ElemCode, ElemProps>(
	      s, o[i], nested);
	  }
	} else {
	  for (unsigned i = 0; i < n; i++)
	    saveValue<Facet, Filter, Quote, ElemCode, ElemProps>(
	      s, o[i], nested);
	}
      } else { // Delimited - no nesting is possible, use AsString
	using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
	using U = ObjType<Elem>;
	using AsString_ = decltype(ZfURI_StringFmt(ZuDeclVal<U *>()));
	using Handler_ = typename AsString_::template Handler<Quote, U>;
	unsigned first = 0;
	if constexpr (IsObjPtr<Elem>{}) {
	  while (first < n && !o[first]) ++first;
	  if (first >= n) return;
	}
	s.delimit();
	if constexpr (Config::Annotated)
	  s << prefix << "[]=";
	else 
	  s << prefix << '=';
	if constexpr (Config::Wrapped) s << '[';
	bool delimiter = false;
	for (unsigned i = first; i < n; i++) {
	  const auto &v = o[i];
	  if constexpr (IsObjPtr<Elem>{})
	    if (!v) continue;
	  if (delimiter) s << Config::Delimiter;
	  delimiter = true;
	  Handler_::save(s, obj_(v));
	}
	if constexpr (Config::Wrapped) s << ']';
      }
    }

    const AnyNode	*node;

    static bool valid(const AnyNode *node) {
      return node && node->has<AnyNode::Array>();
    }

    Handler(const AnyNode *node_) : node{node_} { }

    using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
    using LoadVec_ =
      LoadVec<Facet, ZfFieldFilter::Load, ElemCode, ElemProps, Elem>;
    template <typename ...Args>
    O ctor(Args &&...args) const {
      if (ZuUnlikely(!valid(node)))
	return O(ZuFwd<Args>(args)...);
      return O(
	ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
    }
    template <typename ...Args>
    O *alloc(Args &&...args) const {
      if (ZuUnlikely(!valid(node)))
	return new O(ZuFwd<Args>(args)...);
      return new O(
	ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
    }
    template <typename ...Args>
    void new_(void *o, Args &&...args) const {
      if (ZuUnlikely(!valid(node)))
	new (o) O(ZuFwd<Args>(args)...);
      else
	new (o) O(
	  ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
    }

    void load(O &o) const {
      if (ZuLikely(valid(node)))
	o = LoadVec_(node->data<AnyNode::Array>());
    }
    void update(O &o) const {
      if (ZuUnlikely(!valid(node))) return;
      const auto &nodes = node->data<AnyNode::Array>();
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
	    auto &child = nodes[i];
	    if (!ElemHandler::valid(child)) continue;
	    auto handler = ElemHandler{child};
	    if (ZuLikely(p))
	      handler.update(*p);
	    else
	      p = handler.alloc();
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

// save/load handler for string-formatted types
struct AsString {
  template <typename O_, typename>
  struct Handler {
    using O = O_;

    using AsString_ = decltype(ZfURI_StringFmt(ZuDeclVal<O *>()));
    using Handler_ = typename AsString_::template Handler<URIQuote<>, O>;

    template <template <typename> class Filter, typename Quote, typename S>
    static void save(S &s, const O &o, ZuCSpan prefix) {
      using Handler_ = typename AsString_::template Handler<Quote, O>;
      s.delimit();
      s << prefix << '=';
      Handler_::save(s, o);
    }
 
    const AnyNode	*node;

    static bool valid(const AnyNode *node) {
      return node && node->has<AnyNode::String>();
    }

    Handler(const AnyNode *node_) : node{node_} { }

    ZuCSpan span() const {
      if (!valid(node)) return {};
      return node->data<AnyNode::String>();
    }

    ZuInline O ctor() const { return Handler_::load(span()); }
    ZuInline O *alloc() const { return new O(Handler_::load(span())); }
    ZuInline void new_(void *o) const { new (o) O(Handler_::load(span())); }
    ZuInline void load(O &o) const { o = Handler_::load(span()); }
    ZuInline void update(O &o) const { o = Handler_::load(span()); }
  };
};

// save/load handler for JSON-formatted types
struct AsJSON {
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;

    using Handler_ = typename ZfJSON::As<O>::template Handler<O, Facet>;

    template <template <typename> class Filter, typename Quote, typename S>
    static void save(S &s, const O &o, ZuCSpan prefix) {
      QuoteBuf buf;
      Handler_::template save<Filter>(buf, o);
      s.delimit();
      s << prefix << '=';
      Quote::quote(s, buf);
    }

    const AnyNode	*node;

    static bool valid(const AnyNode *node) {
      return node && node->has<AnyNode::String>();
    }

    Handler(const AnyNode *node_) : node{node_} { }

    // resolve JSON handler
    ZuTuple<ZuPtr<ZfJSON::AnyNode>, Handler_> handler_() const {
      ZuSpan<char> span;
      if (node->has<AnyNode::String>())
	span = node->data<AnyNode::String>();
      auto scan = ZfJSON::scan(span);
      const bool valid = scan.template p<0>() >= 0;
      if (!valid)
	scan = ZfJSON::scan({});
      ZuPtr<ZfJSON::AnyNode> node_ = ZuMv(scan.template p<1>());
      const ZfJSON::AnyNode *ptr = valid ? (*node_)[0].ptr() : node_.ptr();
      return {ZuMv(node_), Handler_(ptr)};
    }

    template <typename ...Args>
    O ctor(Args &&...args) const {
      return handler_().template p<1>().ctor(ZuFwd<Args>(args)...);
    }
    template <typename ...Args>
    O *alloc(Args &&...args) const {
      return handler_().template p<1>().alloc(ZuFwd<Args>(args)...);
    }
    template <typename ...Args>
    void new_(void *o, Args &&...args) const {
      handler_().template p<1>().new_(o, ZuFwd<Args>(args)...);
    }
    void load(O &o) const { handler_().template p<1>().load(o); }
    void update(O &o) const { handler_().template p<1>().update(o); }
  };
};

template <
  typename Facet, template <typename> class Filter, typename Quote,
  unsigned TypeCode, typename Props,
  typename S, typename T, typename L>
inline void saveValue_(S &s, const T &v_, L &&l)
{
  if constexpr (
      TypeCode == ZfFieldTC::CString ||
      TypeCode == ZfFieldTC::String) {
    l(s);
    Quote::quote(s, v_);
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    l(s);
    constexpr unsigned Fmt = ZuFieldProp::URI::GetBytesFmt<Props>{};
    if constexpr (Fmt == ZfURI::Base64) {
      ZuBSpan v{v_};
      unsigned n = ZuBase64::enclen(v.length());
      auto buf = ZmScratch(uint8_t, n);
      buf.length(n);
      buf.length(ZuBase64::encode(buf, v));
      Quote::quote(s, ZuCSpan(buf)); // base64 needs quoting
    } else if constexpr (Fmt == ZfURI::Base64URL) {
      ZuBSpan v{v_};
      unsigned n = ZuBase64URL::enclen(v.length());
      auto buf = ZmScratch(uint8_t, n);
      buf.length(n);
      buf.length(ZuBase64URL::encode(buf, v));
      s << ZuCSpan(buf);
    } else if constexpr (Fmt == ZfURI::Base32) {
      ZuBSpan v{v_};
      unsigned n = ZuBase32::enclen(v.length());
      auto buf = ZmScratch(uint8_t, n);
      buf.length(n);
      buf.length(ZuBase32::encode(buf, v));
      s << ZuCSpan(buf);
    } else if constexpr (Fmt == ZfURI::Hex) {
      ZuBSpan v{v_};
      unsigned n = ZuHex::enclen(v.length());
      auto buf = ZmScratch(uint8_t, n);
      buf.length(n);
      buf.length(ZuHex::encode(buf, v));
      s << ZuCSpan(buf);
    } else // if constexpr (Fmt == ZfURI::Escaped)
      Quote::quote(s, v_);
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    l(s);
    constexpr unsigned Fmt = ZuFieldProp::URI::GetBoolFmt<Props>{};
    bool v = v_;
    if constexpr (Fmt == ZfURI::Bool_TRUE_FALSE)
      s << (v ? "TRUE" : "FALSE");
    else if constexpr (Fmt == ZfURI::Bool_True_False)
      s << (v ? "True" : "False");
    else if constexpr (Fmt == ZfURI::Bool_true_false)
      s << (v ? "true" : "false");
    else if constexpr (Fmt == ZfURI::Bool_T_F)
      s << (v ? "T" : "F");
    else if constexpr (Fmt == ZfURI::Bool_t_f)
      s << (v ? "t" : "f");
    else if constexpr (Fmt == ZfURI::Bool_YES_NO)
      s << (v ? "YES" : "NO");
    else if constexpr (Fmt == ZfURI::Bool_Yes_No)
      s << (v ? "Yes" : "No");
    else if constexpr (Fmt == ZfURI::Bool_yes_no)
      s << (v ? "yes" : "no");
    else if constexpr (Fmt == ZfURI::Bool_Y_N)
      s << (v ? "Y" : "N");
    else if constexpr (Fmt == ZfURI::Bool_y_n)
      s << (v ? "y" : "n");
    else // if constexpr (Fmt == ZfURI::Bool_0_1)
      s << (v ? "0" : "1");
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
    using Fmt = ZuFieldProp::URI::GetNumberFmt<Props>;
    if constexpr (ZuIsBoxed<T>{}) {
      l(s);
      s << ZfFieldPrintInt<Props, typename Fmt::Fmt, T>(v_);
    } else {
      using B = ZuBox<ZfFieldTC::Type<TypeCode>>;
      auto v = B{v_};
      if (!*v) return;
      l(s);
      s << ZfFieldPrintInt<Props, typename Fmt::Fmt, B>(v);
    }
  } else if constexpr (TypeCode == ZfFieldTC::Float) {
    using Fmt = ZuFieldProp::URI::GetNumberFmt<Props>;
    double v = v_;
    if (ZuUnlikely(ZuNull(v))) return;
    l(s);
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
    if (e) {
      s << 'e';
      if (e > 0) {
	if constexpr (Quote::Policy::plus())
	  s << "%2B";
	else
	  s << '+';
      }
      s << e;
    }
  } else if constexpr (TypeCode == ZfFieldTC::Fixed) {
    using Fmt = ZuFieldProp::URI::GetNumberFmt<Props>;
    ZuFixed v = v_;
    if (ZuUnlikely(!*v)) return;
    l(s);
    s << v.fmt<typename Fmt::Fmt>();
  } else if constexpr (TypeCode == ZfFieldTC::Decimal) {
    using Fmt = ZuFieldProp::URI::GetNumberFmt<Props>;
    ZuDecimal v = v_;
    if (ZuUnlikely(!*v)) return;
    l(s);
    s << v.fmt<typename Fmt::Fmt>();
  } else if constexpr (
      TypeCode == ZfFieldTC::Time ||
      TypeCode == ZfFieldTC::DateTime) {
    using Fmt = ZuFieldProp::URI::GetTimeFmt<Props>;
    if constexpr (Fmt::Fmt == ZfURI::Unix) {
      ZuTime v{v_};
      if (!*v) return;
      l(s);
      if constexpr (Fmt::Unit == ZfURI::Sec) {
	s << ZuBoxed(v.sec());
	if constexpr (Fmt::NDP)
	  s << '.' << ZuBoxed(v.nsec()).fmt<ZuFmt::Frac<9, Fmt::NDP>>();
      } else if constexpr (Fmt::Unit == ZfURI::MSec) {
	int128_t t = int128_t(v.sec());
	int64_t f = v.nsec();
	t = t * 1000U + (f / 1000000U);
	f %= 1000000U;
	s << ZuBoxed(t);
	if constexpr (Fmt::NDP)
	  s << '.' << ZuBoxed(f).fmt<ZuFmt::Frac<6, Fmt::NDP>>();
      } else if constexpr (Fmt::Unit == ZfURI::USec) {
	int128_t t = int128_t(v.sec());
	int64_t f = v.nsec();
	t = t * 1000000U + (v.nsec() / 1000U);
	f %= 1000U;
	s << ZuBoxed(t);
	if constexpr (Fmt::NDP)
	  s << '.' << ZuBoxed(f).fmt<ZuFmt::Frac<3, Fmt::NDP>>();
      } else if constexpr (Fmt::Unit == ZfURI::NSec) {
	int128_t t = int128_t(v.sec());
	t = t * 1000000000U + v.nsec();
	s << ZuBoxed(t);
      }
    } else if constexpr (Fmt::Fmt == ZfURI::CSV) {
      ZuDateTime v{v_};
      if (!*v) return;
      l(s);
      auto &fmt = ZmTLS<ZuDateTimeFmt::CSV, (int Props::*){}>();
      s << v.fmt(fmt);
    } else if constexpr (Fmt::Fmt == ZfURI::FIX) {
      auto &fmt = ZmTLS<ZuDateTimeFmt::FIX<Fmt::NDP>, (int Props::*){}>();
      ZuDateTime v{v_};
      if (!*v) return;
      l(s);
      s << v.fmt(fmt);
    } else if constexpr (Fmt::Fmt == ZfURI::ISO) {
      auto &fmt = ZmTLS<ZuDateTimeFmt::ISO, (int Props::*){}>();
      ZuDateTime v{v_};
      if (!*v) return;
      l(s);
      s << v.fmt(fmt);
    }
  }
}

template <
  typename Facet, template <typename> class Filter, typename Quote,
  unsigned TypeCode, typename Props,
  typename S, typename T_>
inline void saveValue(S &s, const T_ &v, ZuCSpan prefix)
{
  using T = ZuDecay<T_>;
  using Config = ZfURI::Config<Facet>;
  if constexpr (!ZfFieldTC::IsVec<TypeCode>{}) {
    if constexpr (TypeCode == ZfFieldTC::UDT) {
      // UDT type - recurse
      if constexpr (!IsObjPtr<T>{}) {
	using Handler = typename As<T>::template Handler<T, Facet>;
	Handler::template save<Filter, Quote>(s, v, prefix);
      } else {
	if (ZuUnlikely(!v)) return;
	using U = ObjType<T>;
	using Handler = typename As<U>::template Handler<U, Facet>;
	Handler::template save<Filter, Quote>(s, *v, prefix);
      }
    } else {
      // leaf type - output it
      saveValue_<Facet, Filter, Quote, TypeCode, Props>(s, v,
	[&prefix](auto &s) { s.delimit(); s << prefix << '='; });
    }
  } else {
    unsigned n = ZuTraits<ZuDecay<decltype(v)>>::length(v);
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    if constexpr (
	Config::ArrayFmt == Member || Config::ArrayFmt == Array ||
	Config::ArrayFmt == List || Config::ArrayFmt == Bare) {
      // array of non-UDT elements using repetition
      for (unsigned i = 0; i < n; i++) {
	if constexpr (Config::ArrayFmt == Member)
	  saveValue_<Facet, Filter, Quote, ElemCode, Props>(s, v[i],
	    [&prefix, i](auto &s) {
	      s.delimit();
	      s << prefix << '.' << ZuBoxed(i) << '=';
	    });
	else if constexpr (Config::ArrayFmt == Array)
	  saveValue_<Facet, Filter, Quote, ElemCode, Props>(s, v[i],
	    [&prefix, i](auto &s) {
	      s.delimit();
	      s << prefix << '[' << ZuBoxed(i) << "]=";
	    });
	else if constexpr (Config::ArrayFmt == List)
	  saveValue_<Facet, Filter, Quote, ElemCode, Props>(s, v[i],
	    [&prefix](auto &s) {
	      s.delimit();
	      s << prefix << "[]=";
	    });
	else // if constexpr (Config::ArrayFmt == Bare)
	  saveValue_<Facet, Filter, Quote, ElemCode, Props>(s, v[i],
	    [&prefix](auto &s) {
	      s.delimit();
	      s << prefix << "=";
	    });
      }
    } else {
      // array of non-UDT elements using single composite value
      s.delimit();
      if constexpr (Config::Annotated)
	s << prefix << "[]=";
      else 
	s << prefix << '=';
      if constexpr (Config::Wrapped) s << '[';
      for (unsigned i = 0; i < n; i++) {
	if (i) s << char(Config::Delimiter);
	saveValue_<Facet, Filter, Quote, ElemCode, Props>(
	  s, v[i], [](auto &) { });
      }
      if constexpr (Config::Wrapped) s << ']';
    }
  }
}

template <
  typename Facet, template <typename> class Filter, typename Quote,
  typename Field,
  typename S, typename O>
inline void saveField(S &s, const O &o, ZuCSpan prefix)
{
  using Type = typename Field::Type;
  using Props = typename Field::Props;
  using Config = ZfURI::Config<Facet>;

  // append field ID to prefix (copy on stack)
  ZuCSpan fieldID = ZuFieldProp::URI::GetID<Field>{}().cspan();
  unsigned nestedSize = 0;
  if (ZuLikely(!prefix))
    nestedSize = fieldID.length();
  else
    nestedSize =
      prefix.length() + fieldID.length() + 1 + (Config::ObjectFmt == Array);
  auto nested = ZmScratch(char, nestedSize);
  if constexpr (Config::ObjectFmt == Member) {
    if (prefix) nested << prefix << '.';
    nested << fieldID;
  } else if constexpr (Config::ObjectFmt == Array) {
    if (prefix)
      nested << prefix << '[' << fieldID << ']';
    else
      nested << fieldID;
  }
  saveValue<Facet, Filter, Quote, Type::Code, Props>(
    s, Field::get(o), nested);
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline T loadValue_(ZuSpan<char> span)
{
  if constexpr (TypeCode == ZfFieldTC::CString) {
    // eos() in-place null-terminates the string
    return T(&span[0]);
  } else if constexpr (TypeCode == ZfFieldTC::String) {
    return T(span);
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    unsigned n = span.length();
    if (ZuUnlikely(!n)) return ZuCmp<T>::null();
    ZuSpan<uint8_t> bytes(span);
    constexpr unsigned Fmt = ZuFieldProp::URI::GetBytesFmt<Props>{};
    // encodings are idempotently in-place-overwrite decoded
    // - trailing bytes are zero-filled to ensure idempotence
    // - for base32/64, the final trailing byte is used to stash
    //   the number of padding bytes from the original encoding
    if constexpr (Fmt == ZfURI::Base64) {
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
    } else if constexpr (Fmt == ZfURI::Base64URL) {
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
    } else if constexpr (Fmt == ZfURI::Base32) {
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
    } else if constexpr (Fmt == ZfURI::Hex) {
      unsigned m = ZuHex::declen(n);
      if (bytes[n - 1]) {
	m = ZuHex::decode({&bytes[0], m}, bytes);
	memset(&bytes[m], 0, n - m);
      }
      bytes.trunc(m);
      return T(bytes);
    } else if constexpr (Fmt == ZfURI::Escaped) {
      return T(bytes);
    }
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    constexpr unsigned BoolFmt = ZuFieldProp::URI::GetBoolFmt<Props>{};
    if constexpr (BoolFmt == ZfURI::Bool_TRUE_FALSE)
      return span == "TRUE" ? T(true) : span == "FALSE" ? T(false) :
	ZuCmp<T>::null();
    else if constexpr (BoolFmt == ZfURI::Bool_True_False)
      return span == "True" ? T(true) : span == "False" ? T(false) :
	ZuCmp<T>::null();
    else if constexpr (BoolFmt == ZfURI::Bool_true_false)
      return span == "true" ? T(true) : span == "false" ? T(false) :
	ZuCmp<T>::null();
    else if constexpr (BoolFmt == ZfURI::Bool_T_F)
      return span == "T" ? T(true) : span == "F" ? T(false) : ZuCmp<T>::null();
    else if constexpr (BoolFmt == ZfURI::Bool_t_f)
      return span == "t" ? T(true) : span == "f" ? T(false) : ZuCmp<T>::null();
    else if constexpr (BoolFmt == ZfURI::Bool_YES_NO)
      return span == "YES" ? T(true) : span == "NO" ? T(false) :
	ZuCmp<T>::null();
    else if constexpr (BoolFmt == ZfURI::Bool_Yes_No)
      return span == "Yes" ? T(true) : span == "No" ? T(false) :
	ZuCmp<T>::null();
    else if constexpr (BoolFmt == ZfURI::Bool_yes_no)
      return span == "yes" ? T(true) : span == "no" ? T(false) :
	ZuCmp<T>::null();
    else if constexpr (BoolFmt == ZfURI::Bool_Y_N)
      return span == "Y" ? T(true) : span == "N" ? T(false) : ZuCmp<T>::null();
    else if constexpr (BoolFmt == ZfURI::Bool_y_n)
      return span == "y" ? T(true) : span == "n" ? T(false) : ZuCmp<T>::null();
    else // if constexpr (BoolFmt == ZfURI::Bool_0_1)
      return span == "1" ? T(true) : span == "0" ? T(false) : ZuCmp<T>::null();
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
    using Fmt = ZuFieldProp::URI::GetNumberFmt<Props>;
    if constexpr (ZuIsBoxed<T>{}) {
      using Scan = ZfFieldScanInt<Props, typename Fmt::Fmt, T>;
      return Scan{span}.value;
    } else {
      using B = ZuBox<ZfFieldTC::Type<TypeCode>>;
      using Scan = ZfFieldScanInt<Props, typename Fmt::Fmt, B>;
      return T(Scan{span}.value.val());
    }
    ZuUnreachable();
  } else if constexpr (
      TypeCode == ZfFieldTC::Float ||
      TypeCode == ZfFieldTC::Fixed ||
      TypeCode == ZfFieldTC::Decimal) {
    if constexpr (
	TypeCode == ZfFieldTC::Decimal ||
	TypeCode == ZfFieldTC::Fixed) {
      auto d = ZfJSON::eov_Decimal(span);
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
      auto d = ZfJSON::eov_Float(span);
      if (d.p<0>() < 0) return ZuCmp<T>::null();
      auto v = d.p<1>();
      ZfFieldLimit<Props>(v);
      return v;
    }
  } else if constexpr (
      TypeCode == ZfFieldTC::Time ||
      TypeCode == ZfFieldTC::DateTime) {
    using TimeFmt = ZuFieldProp::URI::GetTimeFmt<Props>;
    if constexpr (TimeFmt::Fmt == ZfURI::Unix) {
      auto d = ZfJSON::eov_Decimal(span);
      if (d.p<0>() < 0) return ZuCmp<T>::null();
      auto &v = d.p<1>();
      if constexpr (TimeFmt::Unit == ZfURI::MSec) {
	v.value /= 1000;
      } else if constexpr (TimeFmt::Unit == ZfURI::USec) {
	v.value /= 1000000;
      } else if constexpr (TimeFmt::Unit == ZfURI::NSec) {
	v.value /= 1000000000;
      }
      if constexpr (ZuIs_<T, ZuTime>{})
	return ZuTime{v};
      else
	return ZuDateTime{ZuTime{v}};
    } else if constexpr (TimeFmt::Fmt == ZfURI::CSV) {
      auto &fmt = ZmTLS<ZuDateTimeScan::CSV, (int Props::*){}>();
      ZuDateTime v;
      if (v.scan(fmt, span) < 0) return ZuCmp<T>::null();
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    } else if constexpr (TimeFmt::Fmt == ZfURI::FIX) {
      auto &fmt = ZmTLS<ZuDateTimeScan::FIX, (int Props::*){}>();
      ZuDateTime v;
      if (v.scan(fmt, span) < 0) return ZuCmp<T>::null();
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    } else if constexpr (TimeFmt::Fmt == ZfURI::ISO) {
      auto &fmt = ZmTLS<ZuDateTimeScan::ISO, (int Props::*){}>();
      ZuDateTime v;
      if (v.scan(fmt, span) < 0) return ZuCmp<T>::null();
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    }
  }
  ZuUnreachable();
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline auto loadValue(const AnyNode *node)
{
  if constexpr (!ZfFieldTC::IsVec<TypeCode>{}) {
    if constexpr (TypeCode == ZfFieldTC::UDT) {
      // UDT type - recurse
      if constexpr (!IsObjPtr<T>{}) {
	using Handler = typename As<T>::template Handler<T, Facet>;
	if (ZuUnlikely(!Handler::valid(node))) return ZuCmp<T>::null();
	return Handler{node}.ctor();
      } else {
	using U = ObjType<T>;
	using Handler = typename As<U>::template Handler<U, Facet>;
	if (ZuUnlikely(!Handler::valid(node))) return T{};
	return T{Handler{node}.alloc()};
      }
    } else {
      ZuSpan<char> span;
      if (ZuLikely(node && node->has<AnyNode::String>()))
	span = node->data<AnyNode::String>();
      return loadValue_<Facet, Filter, TypeCode, Props, T>(span);
    }
  } else {
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    using Actual = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
    using Elem = ZuIf<
      ElemCode >= ZfFieldTC::Int8 && ElemCode <= ZfFieldTC::UInt128 &&
      bool(ZuIsBoxed<Actual>{}), Actual, ZfFieldTC::Type<ElemCode>>;
    using LoadVec_ = LoadVec<Facet, Filter, ElemCode, Props, Elem>;
    if (!node || !node->has<AnyNode::Array>()) {
      static const NodeArray _;
      return LoadVec_(_);
    }
    return LoadVec_(node->data<AnyNode::Array>());
  }
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline auto loadValue(ZuPtr<AnyNode> &node)
{
  if constexpr (!ZfFieldTC::IsVec<TypeCode>{}) {
    return loadValue<Facet, Filter, TypeCode, Props, T>(node.ptr());
  } else {
    using Config = ZfURI::Config<Facet>;
    asArray<Config>(node);
    return loadValue<Facet, Filter, TypeCode, Props, T>(node.ptr());
  }
}

template <typename O, typename Facet>
template <template <typename> class Filter, typename Field, typename L>
inline decltype(auto) AsObject::Handler<O, Facet>::loadField(L &&l) const
{
  enum { TypeCode = Field::Type::Code };
  using Props = typename Field::Props;
  using T = typename Field::T;
  using R = decltype(
    loadValue<Facet, Filter, TypeCode, Props, T>(
	ZuDeclVal<const AnyNode *>()));
  if (ZuLikely(node && node->has<AnyNode::Object>())) {
    const auto &object = node->data<AnyNode::Object>();
    using PathIndex = ZuFieldProp::URI::GetPathIndex<Props>;
    if constexpr (PathIndex{}() >= 0) {
      if (auto paths_ = object.find(ZuCSpan{})) {
	auto &paths = paths_->val();
	if (paths && paths->template has<AnyNode::Array>()) {
	  auto &array = paths->template data<AnyNode::Array>();
	  constexpr unsigned index = uint8_t(PathIndex{}());
	  if (index < array.length()) {
	    auto &child = array[index];
	    if (child)
	      return ZuFwd<L>(l)(loadValue<Facet, Filter, TypeCode, Props, T>(child), false);
	  }
	}
      }
    } else {
      ZuCSpan fieldID = ZuFieldProp::URI::GetID<Field>{}().cspan();
      if (auto node_ = object.find(fieldID)) {
	auto &child = node_->val();
	return ZuFwd<L>(l)(loadValue<Facet, Filter, TypeCode, Props, T>(child), false);
      }
    }
  }
  if constexpr (ZfFieldTC::IsVec<TypeCode>{}) {
    static const NodeArray _;
    return ZuFwd<L>(l)(R(_), true);
  } else
    return ZuFwd<L>(l)(R(Field::deflt()), true);
}

template <
  typename Facet, template <typename> class Filter,
  typename S, typename O>
inline S &savePath_(S &s, const O &o) {
  using T = ZuDecay<O>;
  if constexpr (IsObjPtr<T>{}) {
    if (ZuLikely(o)) savePath_<Facet, Filter>(s, *o);
  } else if constexpr (IsUnion<T>{}) {
    using Handler = typename As<T>::template Handler<T, Facet>;
    Handler::dispatch_(o, [&s]<typename V>(const V &v) {
      savePath_<Facet, Filter>(s, v);
    });
  } else {
    using PathFields_ = PathFields<ZuTypeGrep<Filter, ZuFields<T, Facet>>>;
    if constexpr (PathFields_::N > 0) {
      ZuUnroll::all<PathFields_>([&s, &o]<typename Field>() {
	using Type = typename Field::Type;
	using Props = typename Field::Props;
	if constexpr (Type::Code == ZfFieldTC::UDT) {
	  // UDT type in the path - process as string
	  using FieldT = typename Field::T;
	  using U = ObjType<FieldT>;
	  const auto &v = Field::get(o);
	  s << '/';
	  bool null = false;
	  if constexpr (IsObjPtr<FieldT>{})
	    if (!v) null = true;
	  if (!null) {
	    using AsString = decltype(ZfURI_StringFmt(ZuDeclVal<U *>()));
	    using StringHandler =
	      typename AsString::template Handler<URIQuote<false>, U>;
	    StringHandler::save(s, obj_(v));
	  }
	} else {
	  // leaf type
	  // - Note: vectors in the path are always in bare list format
	  s << '/';
	  saveValue_<Facet, Filter, URIQuote<false>, Type::Code, Props>(
	    s, Field::get(o), [](auto &s) { });
	}
      });
    }
  }
  return s;
}

template <
  typename Facet, template <typename> class Filter_,
  typename S, typename O>
inline S &save_(S &s_, const O &o) {
  using T = ZuDecay<O>;
  using U = ObjType<T>;
  if constexpr (IsObjPtr<T>{})
    if (ZuUnlikely(!o)) return s_;
  const auto &v = obj_(o);
  using Handler = typename As<U>::template Handler<U, Facet>;
  if constexpr (IsUnion<U>{}) {
    Handler::dispatch_(v, [&s_]<typename V>(const V &member) {
      savePath_<Facet, Filter_>(s_, member);
      using Filter = QueryFilter<Filter_>;
      OutQuery<S> s{s_, true, '?'};
      using MemberHandler = typename As<V>::template Handler<V, Facet>;
      MemberHandler::template save<
	Filter::template Filter, URIQuote<false>>(s, member, {});
    });
  } else {
    savePath_<Facet, Filter_>(s_, v);
    using Filter = QueryFilter<Filter_>;
    OutQuery<S> s{s_, true, '?'};
    Handler::template save<Filter::template Filter, URIQuote<false>>(s, v, {});
  }
  return s_;
}

template <
  typename Facet, template <typename> class Filter_,
  typename S, typename O>
inline S &saveBody_(S &s_, const O &o) {
  using T = ZuDecay<O>;
  using U = ObjType<T>;
  if constexpr (IsObjPtr<T>{})
    if (ZuUnlikely(!o)) return s_;
  const auto &v = obj_(o);
  using Handler = typename As<U>::template Handler<U, Facet>;
  if constexpr (IsUnion<U>{}) {
    Handler::dispatch_(v, [&s_]<typename V>(const V &member) {
      using Filter = QueryFilter<Filter_>;
      OutQuery<S> s{s_};
      using MemberHandler = typename As<V>::template Handler<V, Facet>;
      MemberHandler::template save<
	Filter::template Filter, URIQuote<true>>(s, member, {});
    });
  } else {
    using Filter = QueryFilter<Filter_>;
    OutQuery<S> s{s_};
    Handler::template save<Filter::template Filter, URIQuote<true>>(s, v, {});
  }
  return s_;
}

template <
  typename Facet = ZuFacet::URI,
  typename S, typename O>
inline S &save(S &s, const O &o) {
  return save_<Facet, ZfFieldFilter::Save>(s, o);
}
template <
  typename Facet = ZuFacet::URI,
  typename S, typename O>
ZuInline S &saveUpd(S &s, const O &o) {
  return save_<Facet, ZfFieldFilter::Upd>(s, o);
}
template <
  typename Facet = ZuFacet::URI,
  typename S, typename O>
ZuInline S &saveDel(S &s, const O &o) {
  return save_<Facet, ZfFieldFilter::Del>(s, o);
}

template <
  typename Facet = ZuFacet::URI,
  typename S, typename O>
inline S &savePath(S &s, const O &o) {
  return savePath_<Facet, ZfFieldFilter::Save>(s, o);
}
template <
  typename Facet = ZuFacet::URI,
  typename S, typename O>
ZuInline S &savePathUpd(S &s, const O &o) {
  return savePath_<Facet, ZfFieldFilter::Upd>(s, o);
}
template <
  typename Facet = ZuFacet::URI,
  typename S, typename O>
ZuInline S &savePathDel(S &s, const O &o) {
  return savePath_<Facet, ZfFieldFilter::Del>(s, o);
}

template <
  typename Facet = ZuFacet::URI,
  typename S, typename O>
inline S &saveBody(S &s, const O &o) {
  return saveBody_<Facet, ZfFieldFilter::Save>(s, o);
}
template <
  typename Facet = ZuFacet::URI,
  typename S, typename O>
ZuInline S &saveBodyUpd(S &s, const O &o) {
  return saveBody_<Facet, ZfFieldFilter::Upd>(s, o);
}
template <
  typename Facet = ZuFacet::URI,
  typename S, typename O>
ZuInline S &saveBodyDel(S &s, const O &o) {
  return saveBody_<Facet, ZfFieldFilter::Del>(s, o);
}

template <typename O, typename Facet = ZuFacet::URI>
auto handler(const AnyNode *node) {
  return typename As<O>::template Handler<O, Facet>{node};
}

} // ZfURI

#endif /* ZfURI_HH */
