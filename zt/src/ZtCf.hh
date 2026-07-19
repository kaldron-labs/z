//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZtStruct Cf load/save
// - compile-time formatting
// - compile-time field matching automaton (ZuMatcher)
// - ingests bare, single-quoted, double-quoted and mixed keys/values
// - owns decoded keys, strings and number lexemes

#ifndef ZtCf_HH
#define ZtCf_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <math.h>

#include <zlib/ZuDecimal.hh>
#include <zlib/ZuMArray.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuUTF.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtLocalString.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtStruct.hh>
#include <zlib/ZtBytesFmt.hh>
#include <zlib/ZtJSON.hh>

ZuStructFacet(Cf); // canonical Cf facet, others can be defined

namespace ZtCf { using namespace ZtJSON; }

namespace ZuFieldProp::Cf {

using namespace ZuFieldProp::JSON;

} // ZuFieldProp::Cf

namespace ZtCf {

// --- input functions

ZuDerive(DefKey, (ZtString<ZtStringBuiltin<16, ZtStringHeapID<"ZtCf.DefineKey">>>));
ZuDerive(DefVal, (ZtString<ZtStringBuiltin<48, ZtStringHeapID<"ZtCf.DefineVal">>>));
ZuDerive(Defines_, (
  ZmRBTreeKV<DefKey, DefVal,
    ZmRBTreeUnique<true,
      ZmRBTreeHeapID<"ZtCf.Defines">>>));
struct Defines : public ZuObject, public Defines_ { };

struct Node_HeapID : public ZuStringT<"ZtCf.Node"> { };

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

  static constexpr unsigned LNodeSize = 512;
  static constexpr unsigned SNodeSize = 48;

  static constexpr int StringSize =
    (SNodeSize - (sizeof(ZtString<>) - ZtString<>::BuiltinSize));
  ZuAssert(StringSize > 0);
  ZuDerive(String, (ZtString<
      ZtStringBuiltin<StringSize, ZtStringHeapID_<Node_HeapID>>>)); 

  ZuDerive(Field, (ZuTuple<String, ZuPtr<AnyNode>>));

  static constexpr int ArraySize =
    (LNodeSize - sizeof(ZtArray<ZuPtr<AnyNode>>)) / sizeof(ZuPtr<AnyNode>);
  static constexpr int ObjectSize =
    (LNodeSize - sizeof(ZtArray<Field>)) / sizeof(Field);
  ZuAssert(ArraySize > 0);
  ZuAssert(ObjectSize > 0);

  ZuDerive(Array, (ZtBuiltin<
      ZtArray<ZuPtr<AnyNode>, ZtArrayHeapID_<Node_HeapID>>, ArraySize>));
  ZuDerive(Object, (ZtBuiltin<
      ZtArray<Field, ZtArrayHeapID_<Node_HeapID>>, ObjectSize>));

  using TL = ZuTypeList<Array, Object, String>;

  template <typename T>
  using Index = ZuTypeIndex<T, TL>;
};

// value type enum (with same values as the AnyNode typelist indices)
namespace ValueTC {
  enum {
    Array = AnyNode::Index<AnyNode::Array>{},
    Object = AnyNode::Index<AnyNode::Object>{},
    String = AnyNode::Index<AnyNode::String>{}
  };
}

template <typename Data, typename Heap>
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

using NodeArray = typename AnyNode::Array;
using CNodeArray = const NodeArray;

// scan a bare, quoted or mixed token into owned storage
template <bool Key>
ZtExtern int eos(ZuCSpan span, AnyNode::String &out);
extern template int eos<false>(ZuCSpan, AnyNode::String &);
extern template int eos<true>(ZuCSpan, AnyNode::String &);

// scan Cf, build parse tree
using PctFn = ZmFn<void(
  ZuCSpan, ZuSpan<const ZuCSpan>, ZmFn<void(ZuCSpan)>)>;
// PctFn must invoke its expansion callback synchronously, if at all
ZtExtern ZuTuple<int, ZuPtr<const AnyNode>> scan(
  ZuCSpan span, PctFn pctFn = {}, ZmRef<Defines> defines = new Defines());

template <typename Data, typename ...Args>
inline auto newNode(Args && ...args) {
  using T = Node<Data>;
  return ZuPtr<T>{new T(ZuFwd<Args>(args)...)};
}

// bov() returns the beginning of a value together with the type of the value
// - returns {-1, -1} if the input is corrupt or no value is found
ZtExtern ZuTuple<int, int> bov(ZuCSpan span);

// eov_Array() scans an array, allocating and returning a new Node
// - returns {offset, node}
// - returns {-1, nullptr} on invalid input
ZtExtern ZuTuple<int, ZuPtr<AnyNode>> eov_Array(ZuCSpan span);

// eov_Object() scans an object, allocating and returning a new Node
// - returns {offset, node}
// - returns {-1, nullptr} on invalid input
ZtExtern ZuTuple<int, ZuPtr<AnyNode>> eov_Object(
  ZuCSpan span, bool root = false);

// --- field save/load

struct AsObject;	// as Cf object {...}
template <unsigned ElemCode, typename ElemProps = ZuTypeList<>>
struct AsArray;		// as Cf array [...]
struct AsString;	// as Cf string

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

} // ZtCf

ZtCf::AsDeflt ZtCf_Fmt(...); // default

namespace ZtCf {

template <typename O>
using As = decltype(ZtCf_Fmt(ZuDeclVal<O *>()));

// load an individual value
template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
auto loadValue(const AnyNode *);

// save/load handler for object-formatted types {...}
struct AsObject {
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;

    template <typename Field>
    using CtorIndex = ZuFieldProp::GetCtor<typename Field::Props>;

    using AllFields = ZuFields<O, Facet>;
    using LoadFields = ZuTypeGrep<ZtFieldFilter::Load, AllFields>;
    using SaveFields = ZuTypeGrep<ZtFieldFilter::Save, AllFields>;
    using CtorFields_ = ZuTypeGrep<ZtFieldFilter::Ctor, AllFields>;
    using CtorFields = ZuTypeSort<CtorIndex, CtorFields_>;
    using InitFields = ZuTypeGrep<ZtFieldFilter::Init, AllFields>;
    using UpdFields = ZuTypeGrep<ZtFieldFilter::Upd, AllFields>;
    using DelFields = ZuTypeGrep<ZtFieldFilter::Del, AllFields>;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      ZtJSON::AsObject::Handler<O, Facet>::template save<Filter>(s, o);
    }

    const AnyNode	*node;
    int			lookup[SaveFields::N];

    Handler(const AnyNode *node_) : node{node_} {
      for (unsigned i = 0; i < SaveFields::N; i++) lookup[i] = -1;
      if (node->has<AnyNode::Object>()) {
	constexpr auto matcher =
	  ZuMatcher<ZuFieldProp::Cf::GetIDs<SaveFields>>();
	unsigned matched = 0;
	const auto &fields = node->data<AnyNode::Object>();
	for (unsigned i = 0, n = fields.length(); i < n; i++) {
	  auto j = matcher.match(fields[i].p<0>());
	  if (j >= 0) {
	    lookup[j] = i;
	    if (++matched >= SaveFields::N) break;
	  }
	}
      }
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
      if constexpr (TypeCode == ZtFieldTC::BytesVec) {
	return R{};
      } else if constexpr (ZtFieldTC::IsVec<TypeCode>{}) {
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
	  handler.loadField<ZtFieldFilter::Load, Field>()...);
      }
      template <typename ...Args>
      static void new_(void *o, const Handler &handler, Args &&...args) {
	new (o) O(
	  ZuFwd<Args>(args)...,
	  handler.loadField<ZtFieldFilter::Load, Field>()...);
      }
    };
    template <typename ...Args>
    O ctor(Args &&...args) const {
      if constexpr (!InitFields::N) // exploit guaranteed copy elision
	return ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
      else {
	O o = ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
	ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	  Field::set(o, this->loadField<ZtFieldFilter::Load, Field>());
	});
	return o;
      }
    }
    template <typename ...Args>
    void new_(void *o_, Args &&...args) const {
      ZuTypeApply<Ctor, CtorFields>::new_(o_, *this, ZuFwd<Args>(args)...);
      O &o = *static_cast<O *>(o_);
      ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	Field::set(o, this->loadField<ZtFieldFilter::Load, Field>());
      });
    }

    void load(O &o) const {
      ZuUnroll::all<LoadFields>([this, &o]<typename Field>() {
	Field::set(o, this->loadField<ZtFieldFilter::Load, Field>());
      });
    }
    void update(O &o) const {
      ZuUnroll::all<UpdFields>([this, &o]<typename Field>() {
	Field::set(o, this->loadField<ZtFieldFilter::Upd, Field>());
      });
    }
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
      ZtJSON::AsArray<ElemCode, ElemProps>::
	template Handler<O, Facet>::template save<Filter>(s, o);
    }

    const AnyNode	*node;

    Handler(const AnyNode *node_) : node{node_} { }

    using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
    using LoadVec_ =
      LoadVec<Facet, ZtFieldFilter::Load, ElemCode, ElemProps, Elem>;
    template <typename ...Args>
    O ctor(Args &&...args) const {
      if (ZuUnlikely(!node->has<AnyNode::Array>()))
	return O(ZuFwd<Args>(args)...);
      return O(ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
    }
    template <typename ...Args>
    void new_(void *o, Args &&...args) const {
      if (ZuUnlikely(!node->has<AnyNode::Array>()))
	new (o) O(ZuFwd<Args>(args)...);
      else
	new (o) O(ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
    }

    void load(O &o) const {
      if (ZuLikely(node->has<AnyNode::Array>()))
	o = LoadVec_(node->data<AnyNode::Array>());
    }
    void update(O &o) const {
      if (ZuUnlikely(!node->has<AnyNode::Array>())) return;
      const auto &nodes = node->data<AnyNode::Array>();
      unsigned n = ZuTraits<O>::length(o);
      unsigned m = nodes.length();
      if (n > m) n = m;
      if constexpr (ElemCode == ZtFieldTC::UDT) {
	using ElemHandler = typename As<Elem>::template Handler<Elem, Facet>;
	for (unsigned i = 0; i < n; i++)
	  ElemHandler{nodes[i]}.update(o[i]);
      } else {
	for (unsigned i = 0; i < n; i++)
	  o[i] = loadValue<
	    Facet, ZtFieldFilter::Upd, ElemCode, ElemProps, Elem>(nodes[i]);
      }
    }
  };
};

// save/load handler for strings
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
//   friend inline Fmt ZtCf_StringFmt(A *); // bind Fmt to A
// };

struct AsStringDeflt {	// default string formatter
  template <typename O>
  struct Handler {
    template <typename S>
    ZuInline static void save(S &s, const O &o) {
      ZtJSON::AsStringDeflt::Handler<O>::save(s, o);
    }
    ZuInline static O load(ZuCSpan span) { // string is already unquoted
      return O(span);
    }
  };
};

} // ZtCf

ZtCf::AsStringDeflt ZtCf_StringFmt(...);

namespace ZtCf {

struct AsString {
  template <typename O_, typename>
  struct Handler {
    using O = O_;
    using Fmt = decltype(ZtCf_StringFmt(ZuDeclVal<O *>()));
    using Handler_ = typename Fmt::template Handler<O>;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) { Handler_::save(s, o); }

    const AnyNode	*node;

    Handler(const AnyNode *node_) : node{node_} { }

    ZuCSpan span() const {
      if (!node->has<AnyNode::String>()) return {};
      return node->data<AnyNode::String>();
    }

    O ctor() const { return Handler_::load(span()); }
    void new_(void *o) const { new (o) O(Handler_::load(span())); }
    void load(O &o) const { o = Handler_::load(span()); }
    void update(O &o) const { o = Handler_::load(span()); }
  };
};

template <unsigned> struct BytesCodec;
template <> struct BytesCodec<ZtCf::Base64> {
  static unsigned declen(unsigned n) { return ZuBase64::declen(n); }
  static unsigned decode(ZuSpan<uint8_t> out, ZuBSpan in) {
    return ZuBase64::decode(out, in);
  }
};
template <> struct BytesCodec<ZtCf::Base64URL> {
  static unsigned declen(unsigned n) { return ZuBase64URL::declen(n); }
  static unsigned decode(ZuSpan<uint8_t> out, ZuBSpan in) {
    return ZuBase64URL::decode(out, in);
  }
};
template <> struct BytesCodec<ZtCf::Base32> {
  static unsigned declen(unsigned n) { return ZuBase32::declen(n); }
  static unsigned decode(ZuSpan<uint8_t> out, ZuBSpan in) {
    return ZuBase32::decode(out, in);
  }
};
template <> struct BytesCodec<ZtCf::Hex> {
  static unsigned declen(unsigned n) { return ZuHex::declen(n); }
  static unsigned decode(ZuSpan<uint8_t> out, ZuBSpan in) {
    return ZuHex::decode(out, in);
  }
};

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline T loadValue_(const AnyNode *node)
{
  auto type = node->type;

  if constexpr (TypeCode == ZtFieldTC::CString) {
    if (ZuUnlikely(type != ValueTC::String)) return nullptr;
    return node->data<AnyNode::String>().data();
  } else if constexpr (TypeCode == ZtFieldTC::String) {
    if (ZuUnlikely(type != ValueTC::String))
      return ZuCmp<T>::null();
    return T(node->data<AnyNode::String>());
  } else if constexpr (TypeCode == ZtFieldTC::Bytes) {
    if (ZuUnlikely(type != ValueTC::String))
      return ZuCmp<T>::null();
    const auto &span = node->data<AnyNode::String>();
    unsigned n = span.length();
    if (ZuUnlikely(!n)) return ZuCmp<T>::null();
    constexpr unsigned Fmt = ZuFieldProp::Cf::GetBytesFmt<Props>{};
    if constexpr (Fmt == ZtCf::Raw) {
      return T(ZuBSpan{span});
    } else {
      using Codec = BytesCodec<Fmt>;
      T out;
      out.length(Codec::declen(n));
      ZuSpan<uint8_t> bytes{out};
      out.length(Codec::decode(bytes, ZuBSpan{span}));
      return out;
    }
  } else if constexpr (TypeCode == ZtFieldTC::Bool) {
    if (ZuUnlikely(type != ValueTC::String)) return ZuCmp<T>::null();
    return ZtScanBool(node->data<AnyNode::String>());
  } else if constexpr (
      TypeCode == ZtFieldTC::Int8 ||
      TypeCode == ZtFieldTC::Int16 ||
      TypeCode == ZtFieldTC::Int32 ||
      TypeCode == ZtFieldTC::Int64 ||
      TypeCode == ZtFieldTC::Int128 ||
      TypeCode == ZtFieldTC::UInt8 ||
      TypeCode == ZtFieldTC::UInt16 ||
      TypeCode == ZtFieldTC::UInt32 ||
      TypeCode == ZtFieldTC::UInt64 ||
      TypeCode == ZtFieldTC::UInt128) {
    using Fmt = ZuFieldProp::Cf::GetNumberFmt<Props>;
    if (ZuUnlikely(type != ValueTC::String)) return ZuCmp<T>::null();
    using Scan =
      ZtFieldScanInt<Props, typename Fmt::Fmt, ZtFieldTC::Type<TypeCode>>;
    return T(Scan{node->data<AnyNode::String>()}.value.val());
  } else if constexpr (
      TypeCode == ZtFieldTC::Float ||
      TypeCode == ZtFieldTC::Fixed ||
      TypeCode == ZtFieldTC::Decimal) {
    if (ZuUnlikely(type != ValueTC::String)) return ZuCmp<T>::null();
    ZuCSpan span = node->data<AnyNode::String>();
    if constexpr (
	TypeCode == ZtFieldTC::Decimal ||
	TypeCode == ZtFieldTC::Fixed) {
      ZuDecimal d{span};
      if constexpr (TypeCode == ZtFieldTC::Decimal)
	return d;
      else {
	if (!*d) return ZuFixed{};
	if constexpr (ZuFieldProp::HasNDP<Props>{})
	  return ZuFixed{d, ZuFieldProp::GetNDP<Props>{}};
	else
	  return ZuFixed{d};
      }
    } else {
      auto d = eov_Float(span);
      if (d.p<0>() < 0) return ZuCmp<T>::null();
      return d.p<1>();
    }
  } else if constexpr (
      TypeCode == ZtFieldTC::Time ||
      TypeCode == ZtFieldTC::DateTime) {
    using Fmt = ZuFieldProp::Cf::GetTimeFmt<Props>;
    if (ZuUnlikely(type != ValueTC::String)) return ZuCmp<T>::null();
    ZuCSpan span = node->data<AnyNode::String>();
    if constexpr (Fmt::Fmt == ZtCf::Unix) {
      auto d = eov_Decimal(span);
      if (d.p<0>() < 0) return ZuCmp<T>::null();
      auto &v = d.p<1>();
      if constexpr (Fmt::Unit == ZtCf::MSec) {
	v.value /= 1000;
      } else if constexpr (Fmt::Unit == ZtCf::USec) {
	v.value /= 1000000;
      } else if constexpr (Fmt::Unit == ZtCf::NSec) {
	v.value /= 1000000000;
      }
      if constexpr (ZuIs_<T, ZuTime>{})
	return ZuTime{v};
      else
	return ZuDateTime{ZuTime{v}};
    } else if constexpr (Fmt::Fmt == ZtCf::CSV) {
      auto &fmt = ZmTLS<ZuDateTimeScan::CSV, (int Props::*){}>();
      ZuDateTime v;
      if (!v.scan(fmt, span)) return ZuCmp<T>::null();
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    } else if constexpr (Fmt::Fmt == ZtCf::FIX) {
      auto &fmt = ZmTLS<ZuDateTimeScan::FIX, (int Props::*){}>();
      ZuDateTime v;
      if (!v.scan(fmt, span)) return ZuCmp<T>::null();
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    } else if constexpr (Fmt::Fmt == ZtCf::ISO) {
      auto &fmt = ZmTLS<ZuDateTimeScan::ISO, (int Props::*){}>();
      ZuDateTime v;
      if (!v.scan(fmt, span)) return ZuCmp<T>::null();
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    }
  } else if constexpr (TypeCode == ZtFieldTC::UDT) {
    return typename As<T>::template Handler<T, Facet>{node}.ctor();
  }
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline auto loadValue(const AnyNode *node)
{
  if constexpr (!ZtFieldTC::IsVec<TypeCode>{}) {
    return loadValue_<Facet, Filter, TypeCode, Props, T>(node);
  } else {
    enum { ElemCode = ZtFieldTC::Elem<TypeCode>{} };
    if constexpr (ElemCode == ZtFieldTC::Bytes) {
      T out;
      if (!node->has<AnyNode::Array>()) return out;
      using Elem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
      const auto &nodes = node->data<AnyNode::Array>();
      for (unsigned i = 0, n = nodes.length(); i < n; i++)
	new (out.push()) Elem(
	  loadValue_<Facet, Filter, ElemCode, Props, Elem>(nodes[i]));
      return out;
    } else {
      using Elem = ZtFieldTC::Type<ElemCode>;
      using LoadVec_ = LoadVec<Facet, Filter, ElemCode, Props, Elem>;
      if (!node->has<AnyNode::Array>()) {
	static const NodeArray _;
	return LoadVec_(_);
      }
      return LoadVec_(node->data<AnyNode::Array>());
    }
  }
}

template <
  typename Facet = ZuFacet::Cf,
  template <typename> class Filter = ZtFieldFilter::Save,
  typename S, typename O>
inline S &save(S &s, const O &v) {
  As<O>::template Handler<O, Facet>::template save<Filter>(s, v);
  return s;
}
template <
  typename Facet = ZuFacet::Cf,
  typename S, typename O>
ZuInline S &saveUpd(S &s, const O &v) {
  return save<Facet, ZtFieldFilter::Upd>(s, v);
}
template <
  typename Facet = ZuFacet::Cf,
  typename S, typename O>
ZuInline S &saveDel(S &s, const O &v) {
  return save<Facet, ZtFieldFilter::Del>(s, v);
}

template <typename O, typename Facet = ZuFacet::Cf>
auto handler(const ZuPtr<const AnyNode> &node) {
  return typename As<O>::template Handler<O, Facet>{node};
}

} // ZtCf

#endif /* ZtCf_HH */
