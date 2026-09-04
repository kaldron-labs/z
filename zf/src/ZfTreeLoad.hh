//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// compile-time loading from ZfTree

#ifndef ZfTreeLoad_HH
#define ZfTreeLoad_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <zlib/ZuDecimal.hh>
#include <zlib/ZuMArray.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZtBytesFmt.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfTree.hh>
#include <zlib/ZePlatform.hh>

namespace ZfTreeLoad {

// Policy supplies field-property selectors, scalar admission and boolean
// extraction, [[noreturn]] load errors, and nested UDT handler selection.
// All policy dispatch is compile-time; this layer owns no parser or emitter.

namespace ScalarMask {
  ZtFlags(, ScalarMask, uint8_t, String, Number, Bool, Null);
}

template <typename Policy>
inline bool validScalar(
    const ZfTree::AnyNode *node, ScalarMask::T mask)
{
  return node && node->has<ZfTree::AnyNode::String>() &&
    Policy::scalar(node->scalarType, mask);
}

template <typename Policy>
inline ZuCSpan scalar(
    const ZfTree::AnyNode *node, ScalarMask::T mask, ZuCSpan expected)
{
  if (ZuUnlikely(!validScalar<Policy>(node, mask)))
    Policy::badType(node, expected);
  return node->data<ZfTree::AnyNode::String>();
}

template <typename B, typename Fmt, typename Props>
inline auto intEOV(ZuCSpan span)
{
  if constexpr (ZuTypeIn<ZuFieldProp::Hex, Props>{})
    return B::template eov<ZuFmt::Hex<false, typename Fmt::Fmt>>(span);
  else
    return B::template eov<typename Fmt::Fmt>(span);
}

template <typename Policy, typename = void>
struct NumberScan {
  static auto decimal(ZuCSpan span) { return ZfJSON::eov_Decimal(span); }
  static auto floating(ZuCSpan span) { return ZfJSON::eov_Float(span); }
};

template <typename Policy>
struct NumberScan<Policy, decltype(
  Policy::decimalEOV(ZuDeclVal<ZuCSpan>()),
  Policy::floatEOV(ZuDeclVal<ZuCSpan>()), void())> {
  static auto decimal(ZuCSpan span) { return Policy::decimalEOV(span); }
  static auto floating(ZuCSpan span) { return Policy::floatEOV(span); }
};

template <
  typename Policy, typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
auto loadValue(const ZfTree::AnyNode *);

template <typename Policy, typename O_, typename Facet>
struct Object {
  using O = O_;
  using AnyNode = ZfTree::AnyNode;

  template <typename Field>
  using CtorIndex = ZuFieldProp::GetCtor<typename Field::Props>;
  template <typename Field>
  using Required =
    ZuTypeIn<ZuFieldProp::Required, typename Field::Props>;
  template <typename Field>
  using UpdReq = ZuBool<
    ZfFieldFilter::Upd<Field>{}() &&
    ZuTypeIn<ZuFieldProp::Required, typename Field::Props>{}()>;

  using AllFields = ZuFields<O, Facet>;
  using LoadFields = ZuTypeGrep<ZfFieldFilter::Load, AllFields>;
  using SaveFields = ZuTypeGrep<ZfFieldFilter::Save, AllFields>;
  using CtorFields_ = ZuTypeGrep<ZfFieldFilter::Ctor, AllFields>;
  using CtorFields = ZuTypeSort<CtorIndex, CtorFields_>;
  using InitFields = ZuTypeGrep<ZfFieldFilter::Init, AllFields>;
  using UpdFields = ZuTypeGrep<ZfFieldFilter::Upd, AllFields>;
  using ReqFields = ZuTypeGrep<Required, AllFields>;
  using UpdReqFields = ZuTypeGrep<UpdReq, AllFields>;

  const AnyNode	*node;
  int		lookup[SaveFields::N];

  static bool valid(const AnyNode *node)
  {
    return node && node->has<AnyNode::Object>();
  }

  Object(const AnyNode *node_) : node{node_}
  {
    for (unsigned i = 0; i < SaveFields::N; i++) lookup[i] = -1;
    if (valid(node)) {
      constexpr auto matcher =
	ZuMatcher<typename Policy::template GetIDs<SaveFields>>();
      const auto &fields = node->data<AnyNode::Object>();
      for (unsigned i = 0, n = fields.length(); i < n; i++) {
	auto j = matcher.exact(fields[i].p<0>());
	if (j >= 0) lookup[j] = i;
      }
    }
  }

  template <typename Field>
  bool hasField() const
  {
    enum { I = ZuTypeIndex<Field, SaveFields>{} };
    return lookup[I] >= 0;
  }

  template <typename Fields>
  void checkRequired() const
  {
    if constexpr (Fields::N)
      ZuUnroll::all<Fields>([this]<typename Field>() {
	if (!this->hasField<Field>()) Policy::required(node, Field::id());
      });
  }

  template <template <typename> class Filter, typename Field>
  auto loadField() const
  {
    enum { TypeCode = Field::Type::Code };
    using Props = typename Field::Props;
    using T = typename Field::T;
    using R = decltype(loadValue<
      Policy, Facet, Filter, TypeCode, Props, T>(ZuDeclVal<AnyNode *>()));
    {
      enum { I = ZuTypeIndex<Field, SaveFields>{} };
      auto j = lookup[I];
      if (j >= 0) {
	auto &fields = node->data<AnyNode::Object>();
	AnyNode *node = fields[j].template p<1>();
	return loadValue<Policy, Facet, Filter, TypeCode, Props, T>(node);
      }
    }
    if constexpr (TypeCode == ZfFieldTC::BytesVec) {
      return R{};
    } else if constexpr (ZfFieldTC::IsVec<TypeCode>{}) {
      static const ZfTree::NodeArray _;
      return R(_);
    } else
      return R{Field::deflt()};
  }

  template <typename ...Field>
  struct Ctor {
    template <typename ...Args>
    static O ctor(const Object &handler, Args &&...args)
    {
      return O(
	ZuFwd<Args>(args)...,
	handler.template loadField<ZfFieldFilter::Load, Field>()...);
    }
    template <typename ...Args>
    static O *alloc(const Object &handler, Args &&...args)
    {
      return new O(
	ZuFwd<Args>(args)...,
	handler.template loadField<ZfFieldFilter::Load, Field>()...);
    }
    template <typename ...Args>
    static void new_(void *o, const Object &handler, Args &&...args)
    {
      new (o) O(
	ZuFwd<Args>(args)...,
	handler.template loadField<ZfFieldFilter::Load, Field>()...);
    }
  };

  template <typename ...Args>
  O ctor(Args &&...args) const
  {
    checkRequired<ReqFields>();
    if constexpr (!InitFields::N)
      return ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
    else {
      O o = ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
      ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	Field::set(o,
	  this->template loadField<ZfFieldFilter::Load, Field>());
      });
      return o;
    }
  }

  template <typename ...Args>
  O *alloc(Args &&...args) const
  {
    checkRequired<ReqFields>();
    O *o = ZuTypeApply<Ctor, CtorFields>::alloc(
      *this, ZuFwd<Args>(args)...);
    ZuUnroll::all<InitFields>([this, o]<typename Field>() {
      Field::set(*o, this->template loadField<ZfFieldFilter::Load, Field>());
    });
    return o;
  }

  template <typename ...Args>
  void new_(void *o_, Args &&...args) const
  {
    checkRequired<ReqFields>();
    ZuTypeApply<Ctor, CtorFields>::new_(o_, *this, ZuFwd<Args>(args)...);
    O &o = *static_cast<O *>(o_);
    ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
      Field::set(o, this->template loadField<ZfFieldFilter::Load, Field>());
    });
  }

  void load(O &o) const
  {
    checkRequired<ReqFields>();
    ZuUnroll::all<LoadFields>([this, &o]<typename Field>() {
      Field::set(o, this->template loadField<ZfFieldFilter::Load, Field>());
    });
  }

  void update(O &o) const
  {
    checkRequired<UpdReqFields>();
    ZuUnroll::all<UpdFields>([this, &o]<typename Field>() {
      using Props = typename Field::Props;
      if (ZuTypeIn<ZuFieldProp::Reset, Props>{}() || hasField<Field>())
	Field::set(o,
	  this->template loadField<ZfFieldFilter::Upd, Field>());
    });
  }
};

template <
  typename Policy, typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
struct LoadVec : public ZuMArray<
  LoadVec<Policy, Facet, Filter, TypeCode, Props, T>,
  ZfTree::CNodeArray, T>
{
  ZuDerive_(LoadVec, (ZuMArray<
    LoadVec, ZfTree::CNodeArray, T>));
  using Base::underlying;
  T get(unsigned i) const &
  {
    return loadValue<
      Policy, Facet, Filter, TypeCode, Props, T>(underlying[i]);
  }
  template <typename V> void set(unsigned, const V &) { }
};

template <
  typename Policy, unsigned ElemCode, typename ElemProps,
  typename O_, typename Facet>
struct Array {
  using O = O_;
  using AnyNode = ZfTree::AnyNode;

  const AnyNode	*node;

  static bool valid(const AnyNode *node)
  {
    return node && node->has<AnyNode::Array>();
  }

  Array(const AnyNode *node_) : node{node_} { }

  using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
  using LoadVec_ = LoadVec<
    Policy, Facet, ZfFieldFilter::Load, ElemCode, ElemProps, Elem>;

  template <typename ...Args>
  O ctor(Args &&...args) const
  {
    if (ZuUnlikely(!valid(node)))
      return O(ZuFwd<Args>(args)...);
    return O(ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
  }

  template <typename ...Args>
  O *alloc(Args &&...args) const
  {
    if (ZuUnlikely(!valid(node)))
      return new O(ZuFwd<Args>(args)...);
    return new O(
      ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
  }

  template <typename ...Args>
  void new_(void *o, Args &&...args) const
  {
    if (ZuUnlikely(!valid(node)))
      new (o) O(ZuFwd<Args>(args)...);
    else
      new (o) O(
	ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
  }

  void load(O &o) const
  {
    if (ZuLikely(valid(node)))
      o = LoadVec_(node->data<AnyNode::Array>());
  }

  void update(O &o) const
  {
    if (ZuUnlikely(!valid(node))) return;
    const auto &nodes = node->data<AnyNode::Array>();
    unsigned n = ZuTraits<O>::length(o);
    unsigned m = nodes.length();
    if (n > m) n = m;
    if constexpr (ElemCode == ZfFieldTC::UDT) {
      using IsPtr = typename Policy::template IsObjPtr<Elem>;
      if constexpr (!IsPtr{}) {
	for (unsigned i = 0; i < n; i++)
	  Policy::template handler<Elem, Facet>(nodes[i]).update(o[i]);
      } else {
	using U = ZuDecay<decltype(*(ZuDeclVal<const Elem &>()))>;
	for (unsigned i = 0; i < n; i++) {
	  auto handler = Policy::template handler<U, Facet>(nodes[i]);
	  auto &p = o[i];
	  if (ZuLikely(p))
	    handler.update(*p);
	  else
	    p = handler.alloc();
	}
      }
    } else {
      for (unsigned i = 0; i < n; i++)
	o[i] = loadValue<
	  Policy, Facet, ZfFieldFilter::Upd,
	  ElemCode, ElemProps, Elem>(nodes[i]);
    }
  }
};

template <
  typename Policy, unsigned ValCode, typename ValProps,
  typename O_, typename Facet>
struct Map {
  using O = O_;
  using AnyNode = ZfTree::AnyNode;
  using Key = typename O::Key;
  using Val = typename O::Val;

  const AnyNode	*node;

  static bool valid(const AnyNode *node)
  {
    return node && node->has<AnyNode::Object>();
  }

  Map(const AnyNode *node_) : node{node_} { }

  template <template <typename> class Filter, bool Update = false>
  void load_(O &o) const
  {
    if (ZuUnlikely(!valid(node))) return;
    const auto &fields = node->data<AnyNode::Object>();
    for (unsigned i = 0, n = fields.length(); i < n; i++) {
      const auto &field = fields[i];
      Key key{field.template p<0>()};
      if constexpr (Update) o.del(key);
      o.add(
	ZuMv(key),
	loadValue<Policy, Facet, Filter, ValCode, ValProps, Val>(
	  field.template p<1>()));
    }
  }

  O ctor() const
  {
    O o;
    load_<ZfFieldFilter::Load>(o);
    return o;
  }

  O *alloc() const
  {
    auto o = new O();
    load_<ZfFieldFilter::Load>(*o);
    return o;
  }

  void new_(void *p_) const
  {
    auto p = new (p_) O();
    load_<ZfFieldFilter::Load>(*p);
  }

  void load(O &o) const
  {
    o.clean();
    load_<ZfFieldFilter::Load>(o);
  }

  void update(O &o) const
  {
    load_<ZfFieldFilter::Upd, true>(o);
  }
};

template <typename Policy, typename StringHandler, typename O>
struct String {
  using AnyNode = ZfTree::AnyNode;

  const AnyNode	*node;

  static bool valid(const AnyNode *node)
  {
    return validScalar<Policy>(node, ScalarMask::String());
  }

  String(const AnyNode *node_) : node{node_} { }

  ZuCSpan span() const
  {
    if (!valid(node)) return {};
    return node->data<AnyNode::String>();
  }

  O ctor() const { return StringHandler::load(span()); }
  O *alloc() const { return new O(StringHandler::load(span())); }
  void new_(void *o) const { new (o) O(StringHandler::load(span())); }
  void load(O &o) const { o = StringHandler::load(span()); }
  void update(O &o) const { o = StringHandler::load(span()); }
};

template <
  typename Policy, typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline T loadValue_(const ZfTree::AnyNode *node)
{
  if constexpr (TypeCode == ZfFieldTC::CString) {
    return scalar<Policy>(node, ScalarMask::String(), "string").data();
  } else if constexpr (TypeCode == ZfFieldTC::String) {
    return T(scalar<Policy>(node, ScalarMask::String(), "string"));
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    auto span = scalar<Policy>(node, ScalarMask::String(), "string");
    unsigned n = span.length();
    if (ZuUnlikely(!n)) return ZuCmp<T>::null();
    constexpr unsigned Fmt =
      typename Policy::template GetBytesFmt<Props>{};
    if constexpr (Fmt == ZfJSON::Raw) {
      return T(span);
    } else {
      using Codec = ZuIf<
	Fmt == ZfJSON::Base64, ZuBase64,
	ZuIf<Fmt == ZfJSON::Base64URL, ZuBase64URL,
	  ZuIf<Fmt == ZfJSON::Base32, ZuBase32, ZuHex>>>;
      T out;
      out.length(Codec::declen(n));
      unsigned bytes = Codec::decode(out.span(), ZuBSpan{span});
      if (ZuUnlikely(n > Codec::enclen(bytes)))
	Policy::badValue(node, "encoded bytes", span);
      out.length(bytes);
      return out;
    }
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    return Policy::template boolean<T, Props>(node);
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
    auto span = scalar<Policy>(
      node, ScalarMask::String() | ScalarMask::Number(), "integer");
    using Fmt = typename Policy::template GetNumberFmt<Props>;
    using B = ZuIf<ZuIsBoxed<T>{}, T, ZuBox<ZfFieldTC::Type<TypeCode>>>;
    B v;
    if constexpr (ZuFieldProp::HasEnum<Props>{}) {
      using Map = ZuFieldProp::GetEnum<Props>;
      auto i = Map::s2v(span);
      if (ZuUnlikely(i < 0)) Policy::template badEnum<Map>(node, {}, span);
      v = B{i};
    } else if constexpr (ZuFieldProp::HasFlags<Props>{}) {
      using Map = ZuFieldProp::GetFlags<Props>;
      using Scan = typename Map::Scan;
      auto r = Scan::eov(span, Fmt::Fmt::FlagsDelim());
      if (ZuUnlikely(r.template p<0>() < 0 ||
          unsigned(r.template p<0>()) != span.length()))
	Policy::template badEnum<Map>(node, {}, span);
      v = B{r.template p<1>().val()};
    } else {
      auto r = Policy::template intEOV<B, Fmt, Props>(
	node->scalarType, span);
      if (ZuUnlikely(r.template p<0>() < 0 ||
          unsigned(r.template p<0>()) != span.length()))
	Policy::badValue(node, "integer", span);
      v = r.template p<1>();
    }
    if constexpr (ZuFieldProp::HasRange<Props>{}) {
      auto v_ = v;
      if (ZuUnlikely(!ZfFieldLimit<Props>(v))) {
	using Range = ZuFieldProp::GetRange<Props>;
	Policy::badRange(
	  node, {}, Range::minimum(), Range::maximum(), v_);
      }
    }
    if constexpr (ZuIsBoxed<T>{})
      return v;
    else
      return T(v.val());
  } else if constexpr (
      TypeCode == ZfFieldTC::Float ||
      TypeCode == ZfFieldTC::Fixed ||
      TypeCode == ZfFieldTC::Decimal) {
    ZuCSpan span = scalar<Policy>(
      node, ScalarMask::String() | ScalarMask::Number(), "number");
    if constexpr (
	TypeCode == ZfFieldTC::Decimal ||
	TypeCode == ZfFieldTC::Fixed) {
      auto r = NumberScan<Policy>::decimal(span);
      if (ZuUnlikely(r.template p<0>() < 0 ||
          unsigned(r.template p<0>()) != span.length()))
	Policy::badValue(node, "decimal", span);
      auto d = r.template p<1>();
      if (ZuUnlikely(!*d)) Policy::badValue(node, "decimal", span);
      if constexpr (ZuFieldProp::HasRange<Props>{}) {
	auto d_ = d;
	if (ZuUnlikely(!ZfFieldLimit<Props>(d))) {
	  using Range = ZuFieldProp::GetRange<Props>;
	  Policy::badRange(
	    node, {}, Range::minimum(), Range::maximum(), d_);
	}
      }
      if constexpr (TypeCode == ZfFieldTC::Decimal)
	return d;
      else {
	if constexpr (ZuFieldProp::HasNDP<Props>{})
	  return ZuFixed{d, ZuFieldProp::GetNDP<Props>{}};
	else
	  return ZuFixed{d};
      }
    } else {
      auto d = NumberScan<Policy>::floating(span);
      if (ZuUnlikely(d.template p<0>() < 0 ||
          unsigned(d.template p<0>()) != span.length()))
	Policy::badValue(node, "floating point number", span);
      auto v = d.template p<1>();
      if constexpr (ZuFieldProp::HasRange<Props>{}) {
	auto v_ = v;
	if (ZuUnlikely(!ZfFieldLimit<Props>(v))) {
	  using Range = ZuFieldProp::GetRange<Props>;
	  Policy::badRange(
	    node, {}, Range::minimum(), Range::maximum(), v_);
	}
      }
      return v;
    }
  } else if constexpr (
      TypeCode == ZfFieldTC::Time ||
      TypeCode == ZfFieldTC::DateTime) {
    if (node && node->has<ZfTree::AnyNode::DateTime>()) {
      const auto &v = node->data<ZfTree::AnyNode::DateTime>();
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    }
    using Fmt = typename Policy::template GetTimeFmt<Props>;
    ZuCSpan span = scalar<Policy>(
      node, ScalarMask::String() | ScalarMask::Number(), "date/time");
    if constexpr (Fmt::Fmt == ZfJSON::Unix) {
      auto d = ZfJSON::eov_Decimal(span);
      if (ZuUnlikely(d.p<0>() < 0 || unsigned(d.p<0>()) != span.length()))
	Policy::badValue(node, "date/time", span);
      auto &v = d.p<1>();
      if (ZuUnlikely(!*v)) Policy::badValue(node, "date/time", span);
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
      auto n = v.scan(fmt, span);
      if (ZuUnlikely(n < 0 || unsigned(n) != span.length()))
	Policy::badValue(node, "date/time", span);
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    } else if constexpr (Fmt::Fmt == ZfJSON::FIX) {
      auto &fmt = ZmTLS<ZuDateTimeScan::FIX, (int Props::*){}>();
      ZuDateTime v;
      auto n = v.scan(fmt, span);
      if (ZuUnlikely(n < 0 || unsigned(n) != span.length()))
	Policy::badValue(node, "date/time", span);
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    } else if constexpr (Fmt::Fmt == ZfJSON::ISO) {
      auto &fmt = ZmTLS<ZuDateTimeScan::ISO, (int Props::*){}>();
      ZuDateTime v;
      auto n = v.scan(fmt, span);
      if (ZuUnlikely(n < 0 || unsigned(n) != span.length()))
	Policy::badValue(node, "date/time", span);
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    }
  } else if constexpr (TypeCode == ZfFieldTC::UDT) {
    using IsPtr = typename Policy::template IsObjPtr<T>;
    if constexpr (!IsPtr{}) {
      return Policy::template handler<T, Facet>(node).ctor();
    } else {
      using U = ZuDecay<decltype(*(ZuDeclVal<const T &>()))>;
      return Policy::template handler<U, Facet>(node).alloc();
    }
  }
}

template <
  typename Policy, typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline auto loadValue(const ZfTree::AnyNode *node)
{
  using AnyNode = ZfTree::AnyNode;

  if constexpr (!ZfFieldTC::IsVec<TypeCode>{}) {
    return loadValue_<Policy, Facet, Filter, TypeCode, Props, T>(node);
  } else {
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    if constexpr (ElemCode == ZfFieldTC::Bytes) {
      T out;
      if (ZuUnlikely(!node || !node->has<AnyNode::Array>()))
	Policy::badType(node, "array");
      using Elem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
      const auto &nodes = node->data<AnyNode::Array>();
      for (unsigned i = 0, n = nodes.length(); i < n; i++)
	new (out.push()) Elem(loadValue_<
	  Policy, Facet, Filter, ElemCode, Props, Elem>(nodes[i]));
      return out;
    } else {
      using Actual = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
      using Elem = ZuIf<
	ElemCode >= ZfFieldTC::Int8 && ElemCode <= ZfFieldTC::UInt128 &&
	bool(ZuIsBoxed<Actual>{}), Actual, ZfFieldTC::Type<ElemCode>>;
      using LoadVec_ = LoadVec<
	Policy, Facet, Filter, ElemCode, Props, Elem>;
      if (ZuUnlikely(!node || !node->has<AnyNode::Array>()))
	Policy::badType(node, "array");
      return LoadVec_(node->data<AnyNode::Array>());
    }
  }
}

} // ZfTreeLoad

#endif /* ZfTreeLoad_HH */
