//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// structure reflection framework
// - code generation is eschewed - pure C++ and C pre-processor implementation
// - structures are any tuple-like composite object composed of fields
// - any C++ type can be used - no specific requirements are imposed
// - a field is a data/function member or lambda-defined derived data
// - fields can be combined into primary/secondary keys, which can form groups
// - compile-time reflection and access to individual fields
// - ZfStruct extends ZuStruct for type-erased run-time introspection,
//   type codes, printing/scanning, ...
// - ZfbStruct further extends ZfStruct for flatbuffers serialization
// - Zdb provides relational data store
//
// each structure has 1 or more "facets"
// - facets are equivalent to spokes in hub-and-spokes
// - facets are renderings of the structure for a particular
//   representation, for example JSON for a particular REST API
// - all structures have a Core facet (the hub) that is canonical
// - facets correspond to different data formats (JSON, URI, ASN1,
//   CLI argv, etc.); each format has a canonical facet, but applications
//   can define custom facets that are variants
//
// each field within any tuple-like composite object has a constexpr
// string ID, a get accessor and a set accessor (unless read-only),
// and an extensible type-list of properties (aka decorators)
//
// macro DSL for describing fields and keys:
//
// ZuStruct((Type[, Facet...]), Field, ...);
//
// Field Syntax
// ------------
// ((Accessor)[, (Props...)])
//
// Accessor			Description
// --------			-----------
// ID				data member
// ID, Rd			read-only data member
// ID, Alias, Member		data member - ID aliased to member
// ID, AliasRd, Member		read-only ''
// ID, Fn			function
// ID, RdFn			read-only function
// ID, AliasFn, Get, Set	function - ID aliased to getter, setter
// ID, AliasRdFn, Get		read-only ''
// ID, Lambda, Get, Set		lambda accessor
// ID, LambdaRd, Get		read-only lambda accessor
//
// if specified, Props is a parenthesized list of extensible properties.
//
// Properties defined by ZuStruct include:
//
// Ctor<N> - this field is the Nth parameter to the object constructor
// Keys<KeyID, ...> - key IDs 0..63, e.g. <0, 1>
//
// Note: by convention, 0 is the primary key and 1+ are secondary keys
//
// Note: if a property includes multiple arguments, as is the case for a
// field that forms part of multiple keys, then the property must be
// wrapped in parentheses for the C pre-processor:
//
// ((id), (Ctor<0>, Keys<0>))		// no need for additional parentheses
// ((id), (Ctor<0>, (Keys<0, 1>)))	// needs additional parentheses

// ZuField(O, ID) is the typename of the field metadata type for
// field "ID" of composite type "O":
// - O must be a leaf typename, without any namespace prefix
// - the typename will be of the form ZuField_O_ID
//
// ZuField API:
//   O			- type of containing object
//   T			- type of field
//   Props		- compile-time properties
//   ReadOnly		- true if read-only
//   id()		- identifier (unique name) of field
//   get(const O &)	- get function
//   set(O &, auto &&v)	- set function
//
//   template <typename U, typename ...Args>
//   call(U &&u, Args &&...args) - calls ZuFwd<U>(u).function(args)
//   where function is the same name as the field ID, unless function
//   is a reserved word
//
// ZuStructKey<KeyID>(O &&) extracts a key tuple from an object
// ZuStructKeyT<O, KeyID> is the key tuple type
// - ZuStructKeyID::All is a tuple containing all fields
// - ZuStructKeyID::Union is a tuple of the union of all the key fields
// ZuStructKeys<O> is a type list of all key types defined for O
//
// Props is a typelist of compile-time properties for the field
// - use ZuTypeIn<Props, Property> for boolean properties
// - in namespace ZuFieldProp:
//   - use HasValue<Props, Type, Property> and GetValue<Props, Type, Property>
//     for scalar NTTP properties like Ctor (see HasCtor and GetCtor)
//   - use HasType<Props, Property> and GetType<Props, Property> for types
//   - use HasSeq<Props, Property> and GetSeq<Props, Property> for sequences
//     like Keys (see GetKeys)

#ifndef ZuStruct_HH
#define ZuStruct_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuReserved.hh>

// sentinel pseudo key IDs

namespace ZuStructKeyID {
  enum {
    All = -1,		// all fields, including non-key fields
    Union = -2		// union of all key fields
  };
};

// compile-time field property list
// - a typelist of individual properties:
// - each type is declared in the ZuFieldProp namespace
// - additional properties can be injected into the ZuFieldProp namespace
//   by higher layers (ZfStruct, ZfbStruct, etc.)
namespace ZuFieldProp {
  // extract a value property
  // - property can be declared with a specific type T:
  //   template <T V> struct Prop { };
  // - ... or with a template:
  //   template <ZuString V> struct Prop { };
  // - ... or with auto:
  //   template <auto V> struct Prop { };
  template <template <auto> class Prop>
  struct GrepValue_ {
    template <typename> struct Is : public ZuFalse { };
    template <auto V> struct Is<Prop<V>> : public ZuTrue { };
    template <typename Props> using Apply = ZuTypeGrep<Is, Props>;
  };
  template <typename Props, template <auto> class Prop>
  using GrepValue = GrepValue_<Prop>::template Apply<Props>;
  template <template <auto> class Prop, typename>
  struct Value__;
  template <template <auto> class Prop, auto V>
  struct Value__<Prop, Prop<V>> { using T = ZuConstant<decltype(V), V>; };
  template <
    typename Props,
    template <auto> class Prop,
    typename Filtered = GrepValue<Props, Prop>,
    unsigned N = Filtered::N>
  struct Value_ {
    enum { Exists = 1 };
    using T = typename Value__<Prop, ZuType<N - 1, Filtered>>::T;
  };
  template <
    typename Props,
    template <auto> class Prop,
    typename Filtered>
  struct Value_<Props, Prop, Filtered, 0> {
    enum { Exists = 0 };
  };
  template <typename Props, template <auto> class Prop>
  using HasValue = ZuBool<Value_<Props, Prop>::Exists>;
  template <typename Props, template <auto> class Prop>
  using GetValue = typename Value_<Props, Prop>::T;

  // extract a type property
  template <template <typename> class Prop>
  struct GrepType_ {
    template <typename> struct Is : public ZuFalse { };
    template <typename T> struct Is<Prop<T>> : public ZuTrue { };
    template <typename Props> using Apply = ZuTypeGrep<Is, Props>;
  };
  template <typename Props, template <typename> class Prop>
  using GrepType = GrepType_<Prop>::template Apply<Props>;
  template <
    typename Props,
    template <typename> class Prop,
    typename Filtered = GrepType<Props, Prop>,
    unsigned N = Filtered::N>
  struct Type_ {
    enum { Exists = 1 };
    using T = typename ZuType<N - 1, Filtered>::T;
  };
  template <
    typename Props,
    template <typename> class Prop,
    typename Filtered>
  struct Type_<Props, Prop, Filtered, 0> {
    enum { Exists = 0 };
  };
  template <typename Props, template <typename> class Prop>
  using HasType = ZuBool<Type_<Props, Prop>::Exists>;
  template <typename Props, template <typename> class Prop>
  using GetType = typename Type_<Props, Prop>::T;

  // extract a sequence property
  template <template <unsigned ...> class Prop>
  struct GrepSeq_ {
    template <typename> struct Is : public ZuFalse { };
    template <unsigned ...Seq> struct Is<Prop<Seq...>> : public ZuTrue { };
    template <typename Props> using Apply = ZuTypeGrep<Is, Props>;
  };
  template <template <unsigned ...> class Prop, typename Props>
  using GrepSeq = GrepSeq_<Prop>::template Apply<Props>;
  template <template <unsigned ...> class, typename>
  struct Seq__;
  template <template <unsigned ...> class Prop, unsigned ...Seq>
  struct Seq__<Prop, Prop<Seq...>> { using T = ZuSeq<Seq...>; };
  template <
    typename Props,
    template <unsigned ...> class Prop,
    typename Filtered = GrepSeq<Prop, Props>,
    unsigned N = Filtered::N>
  struct Seq_ {
    enum { Exists = 1 };
    using T = typename Seq__<Prop, ZuType<N - 1, Filtered>>::T;
  };
  template <
    typename Props,
    template <unsigned ...> class Prop,
    typename Filtered>
  struct Seq_<Props, Prop, Filtered, 0> {
    enum { Exists = 0 };
    using T = ZuSeq<>;
  };
  template <typename Props, template <unsigned ...> class Prop>
  using HasSeq = ZuBool<Seq_<Props, Prop>::Exists>;
  template <typename Props, template <unsigned ...> class Prop>
  using GetSeq = typename Seq_<Props, Prop>::T;

  // constructor parameter index
  template <unsigned I> struct Ctor { };
  template <typename Props>
  using HasCtor = HasValue<Props, Ctor>;
  template <typename Props>
  using GetCtor = GetValue<Props, Ctor>;

  // key IDs
  template <unsigned ...KeyIDs> struct Keys { };
  template <typename Props>
  using GetKeys = GetSeq<Props, Keys>;

  // Key<Props, KeyID> - is this field a part of key KeyID?
  template <typename Props, int KeyID>
  struct Key :
    public ZuTypeIn<ZuUnsigned<unsigned(KeyID)>, ZuSeqTL<GetKeys<Props>>> { };
  template <typename Props>
  struct Key<Props, ZuStructKeyID::All> : public ZuTrue { };
  template <typename Props>
  struct Key<Props, ZuStructKeyID::Union> :
    public ZuBool<GetKeys<Props>::N> { };
}

#define ZuField(O, ID) ZuField_##O##_##ID

#define ZuField_Prop_(Prop) ZuFieldProp:: ZuPP_Strip(Prop)
#define ZuField_Props_(...) \
  ZuTypeList<ZuPP_Eval_(ZuPP_MapComma(ZuField_Prop_, __VA_ARGS__))>
#define ZuField_Props(Args) ZuPP_Defer(ZuField_Props_)Args

// if the field ID happens to be a C++ reserved word, suffix it with _
#define ZuField_Unreserved_(ID) ID
#define ZuField_Reserved_(ID) ID##_
#define ZuField_Reserved(ID) \
  ZuPP_Defer(ZuIfReserved)(ID, ZuField_Reserved_, ZuField_Unreserved_) \

// field definition preamble
#define ZuField_Pre__(O_, ID) \
  using O = O_; \
  static constexpr auto &id() { return #ID; } \
  template <typename U, typename ...Args> \
  static decltype(auto) call(U &&u, Args &&...args) { \
    return ZuFwd<U>(u).ZuField_Reserved(ID)(ZuFwd<Args>(args)...); \
  }
#define ZuField_Pre_1(O, ID) \
  ZuField_Pre__(O, ID) \
  using Props = ZuTypeList<>;
#define ZuField_Pre_2(O, ID, Props_) \
  ZuField_Pre__(O, ID) \
  using Props = ZuField_Props(Props_);
#define ZuField_Pre_N(_0, _1, Fn, ...) Fn
#define ZuField_Pre_(O, ...) \
  ZuField_Pre_N(__VA_ARGS__, \
    ZuField_Pre_2(O, __VA_ARGS__), \
    ZuField_Pre_1(O, __VA_ARGS__))
#define ZuField_Pre(O, ID, Args) \
  ZuPP_Defer(ZuField_Pre_)(O, ID ZuPP_StripAppend(Args))

#define ZuField_Adapt(O, ID) \
  using Orig = ZuField(O, ID); \
  template <template <typename> class Override> \
  using Adapt = Override<Orig>

#define ZuField_AliasRd_(O, Member) \
  using T = ZuDecay<decltype(ZuDeclVal<const O &>().Member)>; \
  static const T &get(const O &o) { return o.Member; }
#define ZuField_Alias_(O, Member) \
  static T &get(O &o) { return o.Member; } \
  static T &&get(O &&o) { return ZuMv(o.Member); } \
  template <typename P> \
  static void set(O &o, P &&v) { o.Member = ZuFwd<P>(v); }
#define ZuField_AliasRd(O, ID, Member, Args) \
  struct ZuField(O, ID) { \
    ZuField_Adapt(O, ID); \
    enum { ReadOnly = 1 }; \
    ZuField_Pre(O, ID, Args) \
    ZuField_AliasRd_(O, Member) \
    template <typename P> static void set(O &, P &&) { } \
  };
#define ZuField_Alias(O, ID, Member, Args) \
  struct ZuField(O, ID) { \
    ZuField_Adapt(O, ID); \
    enum { ReadOnly = 0 }; \
    ZuField_Pre(O, ID, Args) \
    ZuField_AliasRd_(O, Member) \
    ZuField_Alias_(O, Member) \
  };
#define ZuField_AliasRdFn_(O, Get) \
  using T = ZuDecay<decltype(ZuDeclVal<const O &>().Get())>; \
  static decltype(auto) get(const O &o) { return o.Get(); } \
  static decltype(auto) get(O &o) { return o.Get(); } \
  static decltype(auto) get(O &&o) { return ZuMv(o).Get(); }
#define ZuField_AliasFn_(O, Set) \
  template <typename V> \
  static void set(O &o, V &&v) { o.Set(ZuFwd<V>(v)); }
#define ZuField_AliasRdFn(O, ID, Get, Args) \
  struct ZuField(O, ID) { \
    ZuField_Adapt(O, ID); \
    enum { ReadOnly = 1 }; \
    ZuField_Pre(O, ID, Args) \
    ZuField_AliasRdFn_(O, Get) \
    template <typename P> static void set(O &, P &&) { } \
  };
#define ZuField_AliasFn(O, ID, Get, Set, Args) \
  struct ZuField(O, ID) { \
    ZuField_Adapt(O, ID); \
    enum { ReadOnly = 0 }; \
    ZuField_Pre(O, ID, Args) \
    ZuField_AliasRdFn_(O, Get) \
    ZuField_AliasFn_(O, Set) \
  };
#define ZuField_LambdaRd_(Get) \
  template <typename P> \
  static decltype(auto) get(P &&o) { \
    constexpr auto fn = Get(); \
    return fn(ZuFwd<P>(o)); \
  }
#define ZuField_Lambda_(O, Set) \
  template <typename P> \
  static void set(O &o, P &&v) { \
    constexpr auto fn = ZuPP_Strip(Set); \
    fn(o, ZuFwd<P>(v)); \
  }
#define ZuField_LambdaRd(O, ID, Get, Args) \
  constexpr auto ZuField_##O##_##ID##_get() { return ZuPP_Strip(Get); } \
  struct ZuField(O, ID) { \
    ZuField_Adapt(O, ID); \
    enum { ReadOnly = 1 }; \
    using T = \
      ZuDecay<decltype(ZuField_##O##_##ID##_get()(ZuDeclVal<const O &>()))>; \
    ZuField_Pre(O, ID, Args) \
    ZuField_LambdaRd_(ZuField_##O##_##ID##_get) \
    template <typename P> static void set(O &, P &&) { } \
  };
#define ZuField_Lambda(O, ID, Get, Set, Args) \
  constexpr auto ZuField_##O##_##ID##_get() { return ZuPP_Strip(Get); } \
  struct ZuField(O, ID) { \
    ZuField_Adapt(O, ID); \
    enum { ReadOnly = 0 }; \
    using T = \
      ZuDecay<decltype(ZuField_##O##_##ID##_get()(ZuDeclVal<const O &>()))>; \
    ZuField_Pre(O, ID, Args) \
    ZuField_LambdaRd_(ZuField_##O##_##ID##_get) \
    ZuField_Lambda_(O, Set) \
  };

#define ZuField_Val(U, Member, Args) \
  ZuField_Alias(U, Member, Member, Args)
#define ZuField_Rd(U, Member, Args) \
  ZuField_AliasRd(U, Member, Member, Args)

#define ZuField_Fn(U, Fn, Args) \
  ZuField_AliasFn(U, Fn, Fn, Fn, Args)
#define ZuField_RdFn(U, Fn, Args) \
  ZuField_AliasRdFn(U, Fn, Fn, Args)

#define ZuField_Decl_2(O, ID, Args) \
  ZuField_Val(O, ID, Args)
#define ZuField_Decl_3(O, ID, Method, Args) \
  ZuField_##Method(O, ID, Args)
#define ZuField_Decl_4(O, ID, Method, Get, Args) \
  ZuField_##Method(O, ID, Get, Args)
#define ZuField_Decl_5(O, ID, Method, Get, Set, Args) \
  ZuField_##Method(O, ID, Get, Set, Args)
#define ZuField_Decl_N(_0, _1, _2, _3, _4, Fn, ...) Fn
#define ZuField_Decl__(O, ...) \
  ZuField_Decl_N(__VA_ARGS__, \
      ZuField_Decl_5(O, __VA_ARGS__), \
      ZuField_Decl_4(O, __VA_ARGS__), \
      ZuField_Decl_3(O, __VA_ARGS__), \
      ZuField_Decl_2(O, __VA_ARGS__))
#define ZuField_Decl_(O, Axor, ...) \
  ZuPP_Defer(ZuField_Decl__)(O, ZuPP_Strip(Axor), (__VA_ARGS__))
#define ZuField_Decl(O, Args) ZuPP_Defer(ZuField_Decl_)(O, ZuPP_Strip(Args))

#define ZuField_Type__(O, ID, ...) ZuField(O, ID)
#define ZuField_Type_(O, Axor, ...) \
  ZuPP_Defer(ZuField_Type__)(O, ZuPP_Strip(Axor))
#define ZuField_Type(O, Args) ZuPP_Defer(ZuField_Type_)(O, ZuPP_Strip(Args))

ZuTypeList<> ZuFields_(...); // default
void ZuStructured_(...); // default
ZuFalse ZuStructRO_(...); // default

// determine if a struct is entirely read-only
template <typename O>
using ZuStructRO = decltype(ZuStructRO_(ZuDeclVal<ZuDecay<O> *>()));

namespace ZuFacet {

struct Core { }; // core facet

} // ZuFacet

// rendering onto a facet
#define ZuStruct_Render_(O, Facet) \
  ZuFields_##O ZuFields_(O *, ZuFacet::Facet *);
#define ZuStruct_Render(O, ...) \
  ZuPP_Eval_(ZuPP_MapArg(ZuStruct_Render_, O, __VA_ARGS__))

// main structure declaration macros ZuStruct()
#define ZuStruct_(O, Facets, ...) \
  O ZuStructured_(O *); \
  __VA_OPT__(ZuPP_MapArg(ZuField_Decl, O, __VA_ARGS__)) \
  using ZuFields_##O = ZuTypeList< \
    __VA_OPT__(ZuPP_MapArgComma(ZuField_Type, O, __VA_ARGS__))>; \
  ZuStruct_Render(O, Core ZuPP_StripAppend(Facets))

#define ZuStruct_Object_(O, ...) O
#define ZuStruct_Object(Args) ZuPP_Defer(ZuStruct_Object_)(ZuPP_Strip(Args))

#define ZuStruct_Facets_(O, ...) (__VA_ARGS__)
#define ZuStruct_Facets(Args) \
  ZuPP_Defer(ZuStruct_Facets_)(ZuPP_Strip(Args))

#define ZuStruct(O_Facets, ...) \
  ZuPP_Eval(ZuPP_Defer(ZuStruct_)( \
    ZuPP_Eval_(ZuStruct_Object(O_Facets)), \
    ZuPP_Eval_(ZuStruct_Facets(O_Facets)) \
    __VA_OPT__(, __VA_ARGS__)))

// obtain the fields of a ZuStruct
template <typename O, typename Facet = ZuFacet::Core>
using ZuFields = decltype(ZuFields_(ZuDeclVal<O *>(), ZuDeclVal<Facet *>()));

// obtain a consteval typelist of field IDs suitable for use with ZuMatcher_<>
template <typename> struct ZuFieldIDs_;
template <typename ...Field>
struct ZuFieldIDs_<ZuTypeList<Field...>> {
  using T = ZuStringTL<Field::id()...>;
};
template <typename O>
struct ZuFieldIDs_ : public ZuFieldIDs_<ZuFields<O>> { };
template <typename U>
using ZuFieldIDs = typename ZuFieldIDs_<U>::T;

// obtain the base structured type from a possibly-derived type
template <typename O>
using ZuStructured = decltype(ZuStructured_(ZuDeclVal<ZuDecay<O> *>()));

// obtain the original structured field from a possibly-transformed field
template <typename Field>
using ZuOrigField = typename Field::Orig;

// get field index within fields
#define ZuFieldIndex(O, ID) (ZuTypeIndex<ZuField(O, ID), ZuFields<O>>{})

// generic tuple from field list
template <typename O, typename Tuple, template <typename> class Filter>
struct ZuStructTuple_;
// recursive decay
struct ZuStructTuple_RDecayer {
  template <typename> struct Decay;
  template <typename O, typename ...Ts, template <typename> class Filter>
  struct Decay<ZuStructTuple_<O, ZuTuple<Ts...>, Filter>> {
    using T = ZuStructTuple_<
      O, ZuTypeApply<ZuTuple, ZuTypeMap<ZuRDecay, Ts...>>, Filter>;
  };
};
template <typename O_, typename Tuple, template <typename> class Filter>
struct ZuStructTuple_ : public Tuple {
  ZuDerive_(ZuStructTuple_, Tuple)

  using O = O_;
  using Fields_ = ZuTypeGrep<Filter, ZuFields<O>>;

  // adapt original fields, overriding get/set
  template <typename Base>
  struct Adapter : public Base {
    using Orig = Base;
    template <template <typename> class Override>
    using Adapt = Adapter<Override<Orig>>;
    using O = Tuple;
    enum { I = ZuTypeIndex<Orig, ZuTypeMap<ZuOrigField, Fields_>>{} };
    // substitute Ctor property for the tuple
  private:
    template <typename>
    struct CtorFilter : public ZuTrue { };
    template <unsigned J>
    struct CtorFilter<ZuFieldProp::Ctor<J>> : public ZuFalse { };
  public:
    using Props =
      ZuTypeGrep<CtorFilter, typename Orig::Props>::template Unshift<
	ZuFieldProp::Ctor<I>>;
    static decltype(auto) get(const O &o) { return o.template p<I>(); }
    static decltype(auto) get(O &o) { return o.template p<I>(); }
    static decltype(auto) get(O &&o) { return ZuMv(o).template p<I>(); }
    template <typename U>
    static void set(O &o, U &&v) { o.template p<I>(ZuFwd<U>(v)); }
  };
  template <typename Field>
  using Map = typename Field::template Adapt<Adapter>;
  // render fields
  template <typename Facet>
  using Fields = ZuTypeMap<Map, ZuTypeGrep<Filter, ZuFields<O, Facet>>>;
  template <typename Facet>
  friend Fields<Facet> ZuFields_(ZuStructTuple_ *, Facet *);

  friend ZuStructTuple_ ZuStructured_(ZuStructTuple_ *);

  // recursive decay
  friend ZuStructTuple_RDecayer ZuRDecayer(ZuStructTuple_ *);

  // underlying type
  friend ZuUnder<O> ZuUnderType(ZuStructTuple_ *);
};

template <
  typename O,
  typename Tuple,
  template <typename> class Filter>
struct ZuStructTupleT__ {
  using T = ZuStructTuple_<O, Tuple, Filter>;
};
template <
  typename O,
  typename Tuple_,
  template <typename> class Filter_,
  typename Tuple,
  template <typename> class Filter>
struct ZuStructTupleT__<ZuStructTuple_<O, Tuple_, Filter_>, Tuple, Filter>
{
  using T = ZuStructTuple_<O, Tuple, Filter>;
};
template <
  typename O,
  template <typename> class ObjectMap,
  template <typename> class ValueMap,
  template <typename> class Filter,
  typename ...Fields>
struct ZuStructTupleT_ {
  using T = typename ZuStructTupleT__<
    O,
    ZuTuple<ValueMap<decltype(Fields::get(ZuDeclVal<ObjectMap<O>>()))>...>,
    Filter>::T;
};
template <
  typename O,
  template <typename> class ObjectMap,
  template <typename> class ValueMap,
  template <typename> class Filter,
  typename ...Fields>
struct ZuStructTupleT_<O, ObjectMap, ValueMap, Filter, ZuTypeList<Fields...>> :
  public ZuStructTupleT_<O, ObjectMap, ValueMap, Filter, Fields...> { };
template <
  typename O,
  template <typename> class ObjectMap,
  template <typename> class ValueMap,
  template <typename> class Filter>
using ZuStructTupleT = typename ZuStructTupleT_<
  ZuDecay<O>, ObjectMap, ValueMap, Filter, 
  ZuTypeGrep<Filter, ZuFields<O>>>::T;

// value tuple - i.e. a tuple of decayed (copied) values

template <typename O>
using ZuStructTuple = ZuStructTupleT<O, ZuMkCRef, ZuDecay, ZuAlwaysTrue>;

// render the appropriate tuple from an object reference

template <typename O, template <typename> class Filter, typename ...Fields>
struct ZuStructTuple_Render {
  static decltype(auto) get(const O &o) {
    return ZuStructTupleT<O, ZuMkCRef, ZuAsIs, Filter>{
      Fields::get(o)...
    };
  }
  static decltype(auto) get(O &&o) {
    return ZuStructTupleT<O, ZuMkRRef, ZuAsIs, Filter>{
      Fields::get(ZuMv(o))...
    };
  }
};
template <typename O, template <typename> class Filter, typename ...Fields>
struct ZuStructTuple_Render<O, Filter, ZuTypeList<Fields...>> :
  public ZuStructTuple_Render<O, Filter, Fields...> { };

// generic tuple extraction

template <template <typename> class Filter, typename O_>
inline decltype(auto) ZuStructExtract(O_ &&o) {
  using O = ZuStructured<O_>;
  using Fields = ZuTypeGrep<Filter, ZuFields<O>>;
  ZuAssert(Fields::N > 0);
  return ZuStructTuple_Render<O, Filter, Fields>::get(ZuFwd<O_>(o));
}

// key field filter, given key ID (see ZuStructKeyID for sentinel values)

template <int KeyID>
struct ZuStructKeyFilter {
  template <typename Field>
  using Filter = ZuFieldProp::Key<typename Field::Props, KeyID>;
};

// generic key extraction

template <int KeyID = 0, typename O>
inline decltype(auto) ZuStructKey(O &&o) {
  using KeyFilter = ZuStructKeyFilter<KeyID>;
  return ZuStructExtract<KeyFilter::template Filter>(ZuFwd<O>(o));
}

// generic key accessor

template <typename O_, int KeyID = 0>
constexpr auto ZuFieldAxor() {
  using O = ZuStructured<O_>;
  using KeyFilter = ZuStructKeyFilter<KeyID>;
  // fields in the key
  using Fields = ZuTypeGrep<KeyFilter::template Filter, ZuFields<O>>;
  // check that the key comprises at least one field
  ZuAssert(Fields::N > 0);
  // return the accessor
  return []<typename P>(P &&o) -> decltype(auto) {
    return ZuStructTuple_Render<O, KeyFilter::template Filter, Fields>::get(
      ZuFwd<P>(o));
  };
}

// generic key tuple by value, and associated fields

template <typename O_, int KeyID>
struct ZuStructKeyT_ {
  using O = ZuStructured<O_>;
  template <typename Field>
  using Filter = ZuStructKeyFilter<KeyID>::template Filter<Field>;
  // core fields that comprise key
  using Fields = ZuTypeGrep<Filter, ZuFields<O>>;
  // check that the key comprises at least one field
  ZuAssert(Fields::N > 0);
  // the key value is a tuple
  using T = ZuStructTupleT<O, ZuMkCRef, ZuDecay, Filter>;
};
template <typename O, int KeyID = 0>
using ZuStructKeyT = typename ZuStructKeyT_<O, KeyID>::T;

// all potential key IDs for a type, as a ZuSeq<>

template <typename ...Fields>
struct ZuStructKeyIDs_ {
  // iterate over all fields; get the max key for each; max those results
  // to get an overall max; generate a sequence from 0 to that max inclusive
  using T = ZuMkSeq<ZuMax<ZuSeq<ZuMax<
    ZuFieldProp::GetKeys<typename Fields::Props>>{}...>>{} + 1>;
};
template <typename ...Fields>
struct ZuStructKeyIDs_<ZuTypeList<Fields...>> :
  public ZuStructKeyIDs_<Fields...> { };
template <typename O>
using ZuStructKeyIDs = typename ZuStructKeyIDs_<ZuFields<O>>::T;

// all keys for a type, as a typelist

template <typename O>
struct ZuStructKeys_ {
  using KeyIDs = ZuSeqTL<ZuStructKeyIDs<O>>;
  template <typename KeyID>
  using KeyT = ZuStructKeyT<O, KeyID{}>;
  using T = ZuTypeMap<KeyT, KeyIDs>;
};
template <typename O>
using ZuStructKeys = typename ZuStructKeys_<O>::T;

// all keys for a type, as a ZuUnion<void, ...>

template <typename O>
struct ZuStructKeyUnion_ {
  using KeyIDs = ZuSeqTL<ZuStructKeyIDs<O>>;
  template <typename KeyID>
  using KeyT = ZuStructKeyT<O, KeyID{}>;
  using UnionTypes = typename ZuTypeMap<KeyT, KeyIDs>::template Unshift<void>;
  using T = ZuTypeApply<ZuUnion, UnionTypes>;
};
template <typename O>
using ZuStructKeyUnion = typename ZuStructKeyUnion_<O>::T;

// CRTP mixin for a shim
// - a shim is an alternate type that mimics the original type
//   described by the fields, implementing members with the same names
// - the field get() functions are substituted with calls to the shim member
template <typename Impl, typename O_>
struct ZuStructShim {
  using O = O_;

  template <typename Base>
  struct Adapter : public Base {
    using Orig = Base;
    template <template <typename> class Override>
    using Adapt = Adapter<Override<Orig>>;
    using O = Impl;
    static decltype(auto) get(const Impl &impl) { return Base::call(impl); }
    template <typename U> static void set(Impl &, U &&) { }
  };
  template <typename Field>
  using Map = typename Field::template Adapt<Adapter>;
  // render fields
  template <typename Facet>
  using Fields = ZuTypeMap<Map, ZuFields<O, Facet>>;
  template <typename Facet>
  friend Fields<Facet> ZuFields_(ZuStructShim *, Facet *);

  friend Impl ZuStructured_(ZuStructShim *);
};

// structure facets

// define a structure facet
// - declared once at top-level namespace, e.g. ZuStructFacet(JSON);
// - structure facets perform compile-time selection of fields
//   and enrichment of field properties for JSON, FIX, ASN1, etc.
#define ZuStructFacet(Facet) \
  namespace ZuFacet { struct Facet { }; } \
  template <typename O> using ZuFields_##Facet = ZuFields<O, ZuFacet::Facet>

// render fields to a facet
// - optionally extends selected fields with additional properties
// - ZuStructRender(Object, Facet[, (FieldID[, Property...])...])
#define ZuField_RenderDecl__(O, ID, ...) \
  using Props_ = ZuField(O, ID)::Props; \
  using Props = typename Props_::template Push<ZuField_Props_(__VA_ARGS__)>;
#define ZuField_RenderDecl_(O, Facet, ID, ...) \
  struct ZuField(O##_##Facet, ID) : public ZuField(O, ID) { \
    __VA_OPT__(ZuField_RenderDecl__(O, ID, __VA_ARGS__)) \
  };
#define ZuField_RenderDecl(O_Facet, Args) \
  ZuPP_Defer(ZuField_RenderDecl_)(ZuPP_Strip(O_Facet), ZuPP_Strip(Args))

#define ZuField_RenderType_(O, Facet, ID, ...) ZuField(O##_##Facet, ID)
#define ZuField_RenderType(O_Facet, Args) \
  ZuPP_Defer(ZuField_RenderType_)(ZuPP_Strip(O_Facet), ZuPP_Strip(Args))

#define ZuStructRender(O, Facet, ...) \
  ZuPP_Eval(ZuPP_MapArg(ZuField_RenderDecl, (O, Facet), __VA_ARGS__)) \
  using ZuFields_##O##_##Facet = ZuTypeList< \
    ZuPP_Eval(ZuPP_MapArgComma(ZuField_RenderType, (O, Facet), __VA_ARGS__))>; \
  ZuFields_##O##_##Facet ZuFields_(O *, ZuFacet::Facet *)

// reflect all fields from one facet to another
// - example use case: re-use canonical JSON properties for different REST APIs
// - ZuStructRender(Object, JSON, ...) // canonical JSON
// - ZuStructReflect(Object, RESTAPI1, JSON) // re-use canonical for RESTAPI1
// - ZuStructReflect(Object, RESTAPI2, JSON) // re-use canonical for RESTAPI2
#define ZuStructReflect(O, Facet, From) \
  decltype(ZuFields_(ZuDeclVal<O *>(), ZuDeclVal<From *>())) \
  ZuFields_(O *, Facet *)

#endif /* ZuStruct_HH */
