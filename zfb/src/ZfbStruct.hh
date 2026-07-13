//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// structured object introspection - flatbuffers extensions
// - ZtStruct extensions for flatbuffers, with extensible type support

// Syntax
// ------
// (((Accessor)[, (Props...)]), (Type[, Args...]))
// 
// Example: (((id, Rd), (Ctor<0>, Keys<0>)), (String))

// macro DSL syntax is identical to that for ZtStruct, with the Type
// extended to specify an extensible flatbuffers <-> C++ mapping

// ZfbStruct	ZtStruct	C++ Type
// --------	--------	--------
// CString	CString		<CString>
// String	String		<String>
// Bytes	Bytes		<uint8_t[]>
// Bool		Bool		<Integral>
// Int<N>	Int<N>		<Integral>
// UInt<N>	UInt<N>		<Integral>
// Float	Float		<FloatingPoint>
// Fixed	Fixed		ZuFixed
// Decimal	Decimal		ZuDecimal
// Time		Time		ZuTime
// DateTime	DateTime	ZuDateTime
//
// Bitmap	UDT		ZuBitmap<Bits> | ZmBitmap | ZtBitmap
// IP		UDT		ZiIP
// ID		UDT		ZuID
// Object	UDT		<Any>

// UDT transformer example - ZiIP support is added as follows:
//   (network/host byte-order swapping is intentionally elided since
//   IP addresses are in network byte order both on the wire and in memory)
//
// fbs:
//   namespace Zfb;
//   struct IPv4 {
//     addr:[uint8:4];
//   }
//   struct IPv6 {
//     addr:[uint8:16];
//   }
//   union IP {
//     IPv4,
//     IPv6
//   }
//
// C++:
// namespace ZfbTransform {
// struct IP {
//   enum { IsInline = 0 };
//   static Save save(Zfb::Builder &fbb, ZiIP addr);
//   static ZiIP load(Zfb::IP type, const void *v);
// };
// } // ZfbTransform
// ZfbTransform::IP ZfbTransformer_(ZiIP *);
// inline ZuID ZtVFieldTypeID(ZiIP *) { return "IP"; }

#ifndef ZfbStruct_HH
#define ZfbStruct_HH

#ifndef ZfbLib_HH
#include <zlib/ZfbLib.hh>
#endif

#include <assert.h>

#include <zlib/ZuID.hh>

#include <zlib/ZtStruct.hh>

#include <zlib/Zfb.hh>

void ZfbBuilder_(...);	// default
void ZfbType_(...);	// ''
void ZfbSchema_(...);	// ''

// internal use - pass underlying object type
template <typename O>
using Zfb_Builder = decltype(ZfbBuilder_(ZuDeclVal<O *>()));
template <typename O>
using Zfb_Type = decltype(ZfbType_(ZuDeclVal<O *>()));
template <typename O>
using Zfb_Schema = decltype(ZfbSchema_(ZuDeclVal<O *>()));

// resolve FB type from object type
template <typename O>
using ZfbBuilder = decltype(ZfbBuilder_(ZuDeclVal<ZuUnder<O> *>()));
template <typename O>
using ZfbType = decltype(ZfbType_(ZuDeclVal<ZuUnder<O> *>()));
template <typename O>
using ZfbSchema = decltype(ZfbSchema_(ZuDeclVal<ZuUnder<O> *>()));

// --- load/save handling

namespace ZfbStruct {

template <typename T> using Offset = Zfb::Offset<T>;

template <typename Field> using IsNested = ZuBool<!Field::IsInline>;
template <
  typename O, typename Facet, typename NestedFields, typename Field,
  bool = IsNested<Field>{}>
struct SaveFieldFn {
  template <template <typename> class Filter, typename Builder>
  static void save(Builder &fbb, const O &o, const Offset<void> *) {
    Field::template save<Facet, Filter>(fbb, o);
  }
};
template <typename O, typename Facet, typename NestedFields, typename Field>
struct SaveFieldFn<O, Facet, NestedFields, Field, true> {
  template <template <typename> class Filter, typename Builder>
  static void save(Builder &fbb, const O &o, const Offset<void> *offsets) {
    using OffsetIndex = ZuTypeIndex<Field, NestedFields>;
    Field::template save<Facet, Filter>(fbb, o, offsets[OffsetIndex{}]);
  }
};
template <
  typename O, typename Facet, typename Fields, template <typename> class Filter,
  typename NestedFields = ZuTypeGrep<IsNested, Fields>,
  unsigned = Fields::N,
  unsigned = NestedFields::N>
struct SaveFieldsFn {
  using Builder = ZfbBuilder<O>;
  using FBType = ZfbType<O>;
  static Offset<FBType> save(Zfb::Builder &fbb_, const O &o) {
    Offset<void> offsets[NestedFields::N];
    ZuUnroll::all<NestedFields>(
	[&fbb_, &o, offsets = &offsets[0]]<typename Field>() {
	  using OffsetIndex = ZuTypeIndex<Field, NestedFields>;
	  offsets[OffsetIndex{}] = Field::template save<Facet, Filter>(fbb_, o);
	});
    Builder fbb{fbb_};
    ZuUnroll::all<Fields>(
	[&fbb, &o, offsets = &offsets[0]]<typename Field>() {
	  using Fn = SaveFieldFn<O, Facet, NestedFields, Field>;
	  Fn::template save<Filter>(fbb, o, offsets);
	});
    return fbb.Finish();
  }
};
template <
  typename O, typename Facet, typename Fields, template <typename> class Filter,
  typename NestedFields, unsigned N>
struct SaveFieldsFn<O, Facet, Fields, Filter, NestedFields, N, 0> {
  using Builder = ZfbBuilder<O>;
  using FBType = ZfbType<O>;
  static Offset<FBType> save(Zfb::Builder &fbb_, const O &o) {
    Builder fbb{fbb_};
    ZuUnroll::all<Fields>([&fbb, &o]<typename Field>() {
      Field::template save<Facet, Filter>(fbb, o);
    });
    return fbb.Finish();
  }
};
template <
  typename O, typename Facet, typename Fields, template <typename> class Filter,
  typename NestedFields>
struct SaveFieldsFn<O, Facet, Fields, Filter, NestedFields, 0, 0> {
  using Builder = ZfbBuilder<O>;
  using FBType = ZfbType<O>;
  static Offset<FBType> save(Zfb::Builder &fbb_, const O &) {
    Builder fbb{fbb_};
    return fbb.Finish();
  }
};

template <typename O_, typename Facet>
struct Handler_ {
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

  static constexpr bool ReadOnly = ZuStructRO<O>{};

  using FBType = ZfbType<O>;

  const FBType	*fbo;

  Handler_(const FBType *fbo_) : fbo{fbo_} { }

  template <template <typename> class Filter>
  static Offset<FBType> save(Zfb::Builder &fbb, const O &o) {
    using Fields = ZuTypeGrep<Filter, AllFields>;
    return SaveFieldsFn<O, Facet, Fields, Filter>::save(fbb, o);
  }

  template <typename ...Field>
  struct Ctor {
    template <typename ...Args>
    static O ctor(const Handler_ &handler, Args &&...args) {
      return O{ZuFwd<Args>(args)..., Field::load_(handler.fbo)...};
    }
    template <typename ...Args>
    static void new_(void *o, const Handler_ &handler, Args &&...args) {
      new (o) O{ZuFwd<Args>(args)..., Field::load_(handler.fbo)...};
    }
  };
  template <
    bool RO = ReadOnly,
    typename = ZuIfT<!RO, void>,
    typename ...Args>
  O ctor(Args &&...args) const {
    if constexpr (!InitFields::N) // exploit guaranteed copy elision
      return ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
    else {
      O o = ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
      ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	Field::load(o, fbo);
      });
      return o;
    }
  }
  template <
    bool RO = ReadOnly,
    typename = ZuIfT<!RO, void>,
    typename ...Args>
  void new_(void *o_, Args &&...args) const {
    ZuTypeApply<Ctor, CtorFields>::new_(o_, *this, ZuFwd<Args>(args)...);
    O &o = *static_cast<O *>(o_);
    ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
      Field::load(o, fbo);
    });
  }

  template <bool RO = ReadOnly, typename = ZuIfT<!RO, void>>
  void load(O &o) const {
    ZuUnroll::all<LoadFields>([this, &o]<typename Field>() {
      Field::load(o, fbo);
    });
  }
  template <bool RO = ReadOnly, typename = ZuIfT<!RO, void>>
  void update(O &o) {
    ZuUnroll::all<UpdFields>([this, &o]<typename Field>() {
      Field::load(o, fbo);
    });
  }
};
template <typename O, typename Facet = ZuFacet::Core>
using Handler = Handler_<ZuStructured<O>, Facet>;

template <typename P>
struct FBFields_ {
  using O = ZuStructured<P>;
  using Fields_ = ZuFields<O>;
  using FBType = ZfbType<O>;

  // adapts fields from original object to flatbuffer
  template <typename Base>
  struct Adapter : public Base {
    using Orig = Base;
    template <template <typename> class Override>
    using Adapt = Adapter<Override<Orig>>;
    using O = FBType;
    enum { I = ZuTypeIndex<Orig, ZuTypeMap<ZuOrigField, Fields_>>{} };
    using Field = ZuType<I, Fields_>;
    static decltype(auto) get(const O &o) { return Field::load_(&o); }
    template <typename U> static void set(O &, U &&v);
  };
  template <typename Field>
  using Map = typename Field::template Adapt<Adapter>;
};

template <typename O, typename Facet = ZuFacet::Core>
using FBFields = ZuTypeMap<FBFields_<O>::template Map, ZuFields<O, Facet>>;

template <
  typename Facet = ZuFacet::Core,
  template <typename> class Filter = ZtFieldFilter::Save,
  typename O>
inline auto save(Zfb::Builder &fbb, const O &o) {
  return Handler<O, Facet>::template save<Filter>(fbb, o);
}
template <typename Facet = ZuFacet::Core, typename O>
ZuInline auto saveUpd(Zfb::Builder &fbb, const O &o) {
  return Handler<O, Facet>::template save<ZtFieldFilter::Upd>(fbb, o);
}
template <typename Facet = ZuFacet::Core, typename O>
ZuInline auto saveDel(Zfb::Builder &fbb, const O &o) {
  return Handler<O, Facet>::template save<ZtFieldFilter::Del>(fbb, o);
}

template <typename O>
inline const ZfbType<O> *root(const uint8_t *data) {
  return Zfb::GetRoot<ZfbType<O>>(data);
}

template <typename O>
inline const ZfbType<O> *verify(ZuBSpan data) {
  if (!Zfb::Verifier{&data[0], data.length()}.VerifyBuffer<ZfbType<O>>())
    return nullptr;
  return root<O>(&data[0]);
}

template <
  typename O,
  typename Facet = ZuFacet::Core,
  typename = ZuIfT<!ZuStructRO<O>{}, void>,
  typename ...Args>
inline O ctor(const ZfbType<O> *fbo, Args &&...args) {
  return Handler<O, Facet>{fbo}.ctor(ZuFwd<Args>(args)...);
}
template <
  typename O,
  typename Facet = ZuFacet::Core,
  typename = ZuIfT<!ZuStructRO<O>{}, void>,
  typename ...Args>
inline void new_(void *o_, const ZfbType<O> *fbo, Args &&...args) {
  Handler<O, Facet>{fbo}.new_(o_, ZuFwd<Args>(args)...);
}

// template <typename O> using Load = typename Handler<O>::Load;

template <
  typename Facet = ZuFacet::Core,
  typename O,
  typename = ZuIfT<!ZuStructRO<O>{}, void>>
inline void load(O &o, const ZfbType<O> *fbo) {
  Handler<O, Facet>{fbo}.load(o);
}
template <
  typename Facet = ZuFacet::Core,
  typename O,
  typename = ZuIfT<!ZuStructRO<O>{}, void>>
inline void update(O &o, const ZfbType<O> *fbo) {
  Handler<O, Facet>{fbo}.update(o);
}

} // ZfbStruct

// a transformer defines save and load functions for a C++ type that
// is persisted in a flatbuffer; there are three types of transformer:
// - primitive - used for integers, floating point values, etc.
// - inline - used for fixed-size flatbuffer structs
// - nested - used for variable-size flatbuffer strings, vectors, tables, etc.
// all user-defined transformers are either inline or nested

namespace ZfbTransform {

struct String {
  enum { IsInline = 0 };
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, ZuCSpan s) {
    return fbb.CreateString(s.data(), s.length());
  }
  template <typename = ZuCSpan>
  static ZuCSpan load(const Zfb::String *s) {
    if (!s) return {};
    return {reinterpret_cast<const char *>(s->Data()), s->size()};
  }
};

struct Bytes {
  enum { IsInline = 0 };
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, ZuBSpan b) {
    return fbb.CreateVector(b.data(), b.length());
  }
  template <typename = ZuBSpan>
  static ZuBSpan load(const Zfb::Vector<uint8_t> *v) {
    if (!v) return {};
    return {v->data(), v->size()};
  }
};

struct Int128 {
  enum { IsInline = 1 };
  static auto save(const int128_t &v) {
    return Zfb::Int128{uint64_t(v>>64), uint64_t(v)};
  }
  static int128_t load(const Zfb::Int128 *v) {
    return (int128_t(v->h())<<64) | v->l();
  }
};

struct UInt128 {
  enum { IsInline = 1 };
  static auto save(const uint128_t &v) {
    return Zfb::UInt128{uint64_t(v>>64), uint64_t(v)};
  }
  static uint128_t load(const Zfb::UInt128 *v) {
    return (uint128_t(v->h())<<64) | v->l();
  }
};

struct Fixed {
  enum { IsInline = 1 };
  static auto save(const ZuFixed &v) {
    return Zfb::Fixed{
      static_cast<int64_t>(v.mantissa),
      static_cast<uint8_t>(v.ndp)};
  }
  static ZuFixed load(const Zfb::Fixed *v) {
    return ZuFixed{v->mantissa(), v->ndp()};
  }
};

struct Decimal {
  enum { IsInline = 1 };
  static auto save(const ZuDecimal &v) {
    return Zfb::Decimal{uint64_t(v.value>>64), uint64_t(v.value)};
  }
  static ZuDecimal load(const Zfb::Decimal *v) {
    return ZuDecimal{ZuDecimal::Unscaled{
      (static_cast<int128_t>(v->h())<<64) | v->l()}};
  }
};

struct Time {
  enum { IsInline = 1 };
  static auto save(const ZuTime &v) {
    return Zfb::Time{v.sec(), int32_t(v.nsec())};
  }
  static auto load(const Zfb::Time *v) {
    return ZuTime{int64_t(v->sec()), int32_t(v->nsec())};
  }
};

struct DateTime {
  enum { IsInline = 1 };
  static auto save(const ZuDateTime &v) {
    return Zfb::DateTime{v.julian(), v.sec(), v.nsec()};
  }
  static auto load(const Zfb::DateTime *v) {
    return ZuDateTime{ZuDateTime::Julian{v->julian()}, v->sec(), v->nsec()};
  }
};

struct Object {
  enum { IsInline = 0 };
  template <
    typename Facet, template <typename> class Filter,
    typename Builder, typename O>
  static auto save(Builder &fbb, const O &o) {
    return ZfbStruct::save<Facet, Filter>(fbb, o);
  }
  template <typename O>
  static O load(const ZfbType<O> *fbo) {
    return ZfbStruct::ctor<O>(fbo);
  }
};

struct CStringVec {
  enum { IsInline = 0 };
  using Vec = Zfb::Vector<Zfb::Offset<Zfb::String>>;
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, ZtStruct_::CStringVec a) {
    using CString = const char *;
    return Zfb::Save::vectorIter<Zfb::String>(fbb, a.length(),
      [&a](Builder &fbb, uint64_t i) mutable {
	return str(fbb, CString(a[i]));
      });
  }
  template <typename = ZtStruct_::CStringVec>
  static const ZtStruct_::CStringVec load(const Vec *v) {
    if (!v) return {};
    return ZtStruct_::CStringVec(*const_cast<Vec *>(v), v->size(),
      [](const void *v_, uint64_t i) {
	return reinterpret_cast<const char *>(
	  static_cast<const Vec *>(v_)->Get(i)->Data());
      });
  }
};

struct StringVec {
  enum { IsInline = 0 };
  using Vec = Zfb::Vector<Zfb::Offset<Zfb::String>>;
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, ZtStruct_::StringVec a) {
    return Zfb::Save::vectorIter<Zfb::String>(fbb, a.length(),
      [&a](Builder &fbb, uint64_t i) mutable {
	return String::save(fbb, ZuCSpan(a[i]));
      });
  }
  template <typename = ZtStruct_::StringVec>
  static const ZtStruct_::StringVec load(const Vec *v) {
    if (!v) return {};
    return ZtStruct_::StringVec(*const_cast<Vec *>(v), v->size(),
      [](const void *v_, uint64_t i) {
	return String::load(static_cast<const Vec *>(v_)->Get(i));
      });
  }
};

struct BytesVec {
  enum { IsInline = 0 };
  using Vec = Zfb::Vector<Zfb::Offset<Zfb::Bytes>>;
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, ZtStruct_::BytesVec a) {
    return Zfb::Save::vectorIter<Zfb::Bytes>(fbb, a.length(),
      [&a](Builder &fbb, uint64_t i) mutable {
	return Zfb::CreateBytes(fbb, Bytes::save(fbb, ZuBSpan(a[i])));
      });
  }
  template <typename = ZtStruct_::BytesVec>
  static const ZtStruct_::BytesVec load(const Vec *v) {
    if (!v) return {};
    return ZtStruct_::BytesVec(*const_cast<Vec *>(v), v->size(),
      [](const void *v_, uint64_t i) {
	return Bytes::load(static_cast<const Vec *>(v_)->Get(i)->data());
      });
  }
};

#define ZfbField_IntVecTransformer(Width) \
struct Int##Width##Vec { \
  enum { IsInline = 0 }; \
  template < \
    typename = void, template <typename> class = ZuAlwaysTrue, \
    typename Builder> \
  static auto save(Builder &fbb, ZtStruct_::Int##Width##Vec a) { \
    return Zfb::Save::pvectorIter<int##Width##_t>(fbb, a.length(), \
      [&a](uint64_t i) mutable { return int##Width##_t(a[i]); }); \
  } \
  using Vec = Zfb::Vector<int##Width##_t>; \
  template <typename = ZtStruct_::Int##Width##Vec> \
  static const ZtStruct_::Int##Width##Vec load(const Vec *v) { \
    if (!v) return {}; \
    return ZtStruct_::Int##Width##Vec(*const_cast<Vec *>(v), v->size(), \
      [](const void *v_, uint64_t i) -> int##Width##_t { \
	return static_cast<const Vec *>(v_)->Get(i); \
      }); \
  } \
}; \
struct UInt##Width##Vec { \
  enum { IsInline = 0 }; \
  template < \
    typename = void, template <typename> class = ZuAlwaysTrue, \
    typename Builder> \
  static auto save(Builder &fbb, ZtStruct_::UInt##Width##Vec a) { \
    return Zfb::Save::pvectorIter<uint##Width##_t>(fbb, a.length(), \
      [&a](uint64_t i) mutable { return uint##Width##_t(a[i]); }); \
  } \
  using Vec = Zfb::Vector<uint##Width##_t>; \
  template <typename = ZtStruct_::UInt##Width##Vec> \
  static const ZtStruct_::UInt##Width##Vec load(const Vec *v) { \
    if (!v) return {}; \
    return ZtStruct_::UInt##Width##Vec(*const_cast<Vec *>(v), v->size(), \
      [](const void *v_, uint64_t i) -> uint##Width##_t { \
	return static_cast<const Vec *>(v_)->Get(i); \
      }); \
  } \
};
  ZfbField_IntVecTransformer(8)
  ZfbField_IntVecTransformer(16)
  ZfbField_IntVecTransformer(32)
  ZfbField_IntVecTransformer(64)

struct Int128Vec {
  enum { IsInline = 0 };
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, ZtStruct_::Int128Vec a) {
    return Zfb::Save::structVecIter<Int128>(fbb, a.length(),
      [&a](Int128 *ptr, uint64_t i) mutable {
	new (ptr) Zfb::Int128{Int128::save(int128_t(a[i]))};
      });
  }
  using Vec = Zfb::Vector<const Zfb::Int128 *>;
  template <typename = ZtStruct_::Int128Vec>
  static const ZtStruct_::Int128Vec load(const Vec *v) {
    if (!v) return {};
    return ZtStruct_::Int128Vec(*const_cast<Vec *>(v), v->size(),
      [](const void *v_, uint64_t i) {
	return int128_t(Int128::load(static_cast<const Vec *>(v_)->Get(i)));
      });
  }
};

struct UInt128Vec {
  enum { IsInline = 0 };
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, ZtStruct_::UInt128Vec a) {
    return Zfb::Save::structVecIter<UInt128>(fbb, a.length(),
      [&a](UInt128 *ptr, uint64_t i) mutable {
	new (ptr) Zfb::UInt128{UInt128::save(uint128_t(a[i]))};
      });
  }
  using Vec = Zfb::Vector<const Zfb::UInt128 *>;
  template <typename = ZtStruct_::UInt128Vec>
  static const ZtStruct_::UInt128Vec load(const Vec *v) {
    if (!v) return {};
    return ZtStruct_::UInt128Vec(*const_cast<Vec *>(v), v->size(),
      [](const void *v_, uint64_t i) {
	return uint128_t(UInt128::load(static_cast<const Vec *>(v_)->Get(i)));
      });
  }
};

struct FloatVec {
  enum { IsInline = 0 };
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, ZtStruct_::FloatVec a) {
    using Float = double;
    return Zfb::Save::pvectorIter<Float>(fbb, a.length(),
      [&a](uint64_t i) mutable { return Float(a[i]); });
  }
  using Vec = Zfb::Vector<double>;
  template <typename = ZtStruct_::FloatVec>
  static const ZtStruct_::FloatVec load(const Vec *v) {
    if (!v) return {};
    return ZtStruct_::FloatVec(*const_cast<Vec *>(v), v->size(),
      [](const void *v_, uint64_t i) {
	return static_cast<const Vec *>(v_)->Get(i);
      });
  }
};

struct FixedVec {
  enum { IsInline = 0 };
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, ZtStruct_::FixedVec a) {
    return Zfb::Save::structVecIter<Fixed>(fbb, a.length(),
      [&a](Fixed *ptr, uint64_t i) mutable {
	new (ptr) Zfb::Fixed{Fixed::save(ZuFixed(a[i]))};
      });
  }
  using Vec = Zfb::Vector<const Zfb::Fixed *>;
  template <typename = ZtStruct_::FixedVec>
  static const ZtStruct_::FixedVec load(const Vec *v) {
    if (!v) return {};
    return ZtStruct_::FixedVec(*const_cast<Vec *>(v), v->size(),
      [](const void *v_, uint64_t i) {
	return Fixed::load(static_cast<const Vec *>(v_)->Get(i));
      });
  }
};

struct DecimalVec {
  enum { IsInline = 0 };
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, ZtStruct_::DecimalVec a) {
    return Zfb::Save::structVecIter<Decimal>(fbb, a.length(),
      [&a](Decimal *ptr, uint64_t i) mutable {
	new (ptr) Zfb::Decimal{Decimal::save(ZuDecimal(a[i]))};
      });
  }
  using Vec = Zfb::Vector<const Zfb::Decimal *>;
  template <typename = ZtStruct_::DecimalVec>
  static const ZtStruct_::DecimalVec load(const Vec *v) {
    if (!v) return {};
    return ZtStruct_::DecimalVec(*const_cast<Vec *>(v), v->size(),
      [](const void *v_, uint64_t i) {
	return Decimal::load(static_cast<const Vec *>(v_)->Get(i));
      });
  }
};

struct TimeVec {
  enum { IsInline = 0 };
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, ZtStruct_::TimeVec a) {
    return Zfb::Save::structVecIter<Time>(fbb, a.length(),
      [&a](Time *ptr, uint64_t i) mutable {
	new (ptr) Zfb::Time{Time::save(ZuTime(a[i]))};
      });
  }
  using Vec = Zfb::Vector<const Zfb::Time *>;
  template <typename = ZtStruct_::TimeVec>
  static const ZtStruct_::TimeVec load(const Vec *v) {
    if (!v) return {};
    return ZtStruct_::TimeVec(*const_cast<Vec *>(v), v->size(),
      [](const void *v_, uint64_t i) {
	return Time::load(static_cast<const Vec *>(v_)->Get(i));
      });
  }
};

struct DateTimeVec {
  enum { IsInline = 0 };
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, ZtStruct_::DateTimeVec a) {
    return Zfb::Save::structVecIter<DateTime>(fbb, a.length(),
      [&a](DateTime *ptr, uint64_t i) mutable {
	new (ptr) Zfb::DateTime{DateTime::save(ZuDateTime(a[i]))};
      });
  }
  using Vec = Zfb::Vector<const Zfb::DateTime *>;
  template <typename = ZtStruct_::DateTimeVec>
  static const ZtStruct_::DateTimeVec load(const Vec *v) {
    if (!v) return {};
    return ZtStruct_::DateTimeVec(*const_cast<Vec *>(v), v->size(),
      [](const void *v_, uint64_t i) {
	return DateTime::load(static_cast<const Vec *>(v_)->Get(i));
      });
  }
};

} // ZfbTransform

ZfbTransform::Object ZfbTransformer_(...);	// default

template <typename T>
using ZfbTransformer = decltype(ZfbTransformer_(ZuDeclVal<T *>()));

namespace ZfbTransform {

template <typename O, typename Base, typename Under = O>
struct Primitive : public Base {
  template <template <typename> class Override>
  using Adapt = Primitive<
    typename Override<ZuOrigField<Base>>::O,
    typename Base::template Adapt<Override>, Under>;
  using FBType = Zfb_Type<Under>;
  enum { IsInline = 1 };
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static void save(Builder &fbb, const O &o) {
    using SaveFn = typename Base::template SaveFn<Builder>;
    using P = ZuType<0, typename ZuDeduce<SaveFn>::Args>;
    Base::save_(fbb, static_cast<P>(Base::get(o)));
  }
  template <typename FBType_>
  static auto load_(const FBType_ *fbo) {
    return static_cast<typename Base::T>(Base::load_(fbo));
  }
  template <bool _ = Base::ReadOnly, ZuIfT<_, int> = 0>
  static void load(O &, const FBType *) { }
  template <bool _ = Base::ReadOnly, ZuIfT<!_, int> = 0>
  static void load(O &o, const FBType *fbo) {
    Base::set(o, load_(fbo));
  }
};

template <typename O, typename Base, typename Transformer, typename Under = O>
struct Inline : public Base {
  template <template <typename> class Override>
  using Adapt = Inline<
    typename Override<ZuOrigField<Base>>::O,
    typename Base::template Adapt<Override>, Transformer, Under>;
  using Builder = Zfb_Builder<Under>;
  using FBType = Zfb_Type<Under>;
  enum { IsInline = 1 };
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static void save(Builder &fbb, const O &o) {
    auto v = Transformer::save(Base::get(o));
    Base::save_(fbb, &v);
  }
  template <typename FBType_>
  static decltype(auto) load_(const FBType_ *fbo) {
    return Transformer::load(Base::load_(fbo));
  }
  template <bool _ = Base::ReadOnly, ZuIfT<_, int> = 0>
  static void load(O &, const FBType *) { }
  template <bool _ = Base::ReadOnly, ZuIfT<!_, int> = 0>
  static void load(O &o, const FBType *fbo) {
    Base::set(o, load_(fbo));
  }
};

template <typename O, typename Base, typename Transformer, typename Under = O>
struct Nested : public Base {
  template <template <typename> class Override>
  using Adapt = Nested<
    typename Override<ZuOrigField<Base>>::O,
    typename Base::template Adapt<Override>, Transformer, Under>;
  using Builder = Zfb_Builder<Under>;
  using FBType = Zfb_Type<Under>;
  enum { IsInline = 0 };
  template <typename Facet, template <typename> class Filter>
  static Zfb::Offset<void> save(Zfb::Builder &fbb, const O &o) {
    return Transformer::template save<Facet, Filter>(fbb, Base::get(o)).Union();
  }
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static void save(Builder &fbb, Zfb::Offset<void> offset) {
    Base::save_(fbb, offset.o);
  }
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static void save(Builder &fbb, const O &, Zfb::Offset<void> offset) {
    save(fbb, offset);
  }
  template <typename FBType_>
  static decltype(auto) load_(const FBType_ *fbo) {
    return Transformer::template load<typename Base::T>(Base::load_(fbo));
  }
  template <bool _ = Base::ReadOnly, ZuIfT<_, int> = 0>
  static void load(O &, const FBType *) { }
  template <bool _ = Base::ReadOnly, ZuIfT<!_, int> = 0>
  static void load(O &o, const FBType *fbo) {
    Base::set(o, load_(fbo));
  }
};

struct IP;

template <typename O, typename Base, typename Under = O, typename Transformer = IP>
struct IP_ : public Base {
  template <template <typename> class Override>
  using Adapt = IP_<
    typename Override<ZuOrigField<Base>>::O,
    typename Base::template Adapt<Override>, Under>;
  using Builder = Zfb_Builder<Under>;
  using FBType = Zfb_Type<Under>;
  enum { IsInline = 0 };
  template <typename Facet, template <typename> class Filter>
  static Zfb::Offset<void> save(Zfb::Builder &fbb, const O &o) {
    return Transformer::save(fbb, Base::get(o)).offset;
  }
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static void save(Builder &fbb, const O &o, Zfb::Offset<void> offset) {
    Base::saveType_(fbb, Transformer::type(Base::get(o)));
    Base::save_(fbb, offset.o);
  }
  template <typename FBType_>
  static decltype(auto) load_(const FBType_ *fbo) {
    return Transformer::load(Base::loadType_(fbo), Base::load_(fbo));
  }
  template <bool _ = Base::ReadOnly, ZuIfT<_, int> = 0>
  static void load(O &, const FBType *) { }
  template <bool _ = Base::ReadOnly, ZuIfT<!_, int> = 0>
  static void load(O &o, const FBType *fbo) {
    Base::set(o, load_(fbo));
  }
};

template <unsigned, typename> struct Resolve;

#define ZfbTransform_Primitive(Code) \
  template <typename U> struct Resolve<ZtFieldTC::Code, U> { \
    template <typename O, typename Base> \
    using Field = Primitive<O, Base>; \
  }
#define ZfbTransform_Inline(Code, Transformer) \
  template <typename U> struct Resolve<ZtFieldTC::Code, U> { \
    template <typename O, typename Base> \
    using Field = Inline<O, Base, Transformer>; \
  }
#define ZfbTransform_Nested(Code, Transformer) \
  template <typename U> struct Resolve<ZtFieldTC::Code, U> { \
    template <typename O, typename Base> \
    using Field = Nested<O, Base, Transformer>; \
  }

ZfbTransform_Nested(CString, String);
ZfbTransform_Nested(String, String);
ZfbTransform_Nested(Bytes, Bytes);
ZfbTransform_Primitive(Bool);
ZfbTransform_Primitive(Int8);
ZfbTransform_Primitive(UInt8);
ZfbTransform_Primitive(Int16);
ZfbTransform_Primitive(UInt16);
ZfbTransform_Primitive(Int32);
ZfbTransform_Primitive(UInt32);
ZfbTransform_Primitive(Int64);
ZfbTransform_Primitive(UInt64);
ZfbTransform_Inline(Int128, Int128);
ZfbTransform_Inline(UInt128, UInt128);
ZfbTransform_Primitive(Float);
ZfbTransform_Inline(Fixed, Fixed);
ZfbTransform_Inline(Decimal, Decimal);
ZfbTransform_Inline(Time, Time);
ZfbTransform_Inline(DateTime, DateTime);

ZfbTransform_Nested(CStringVec, CStringVec);
ZfbTransform_Nested(StringVec, StringVec);
ZfbTransform_Nested(BytesVec, BytesVec);
ZfbTransform_Nested(Int8Vec, Int8Vec);
ZfbTransform_Nested(UInt8Vec, UInt8Vec);
ZfbTransform_Nested(Int16Vec, Int16Vec);
ZfbTransform_Nested(UInt16Vec, UInt16Vec);
ZfbTransform_Nested(Int32Vec, Int32Vec);
ZfbTransform_Nested(UInt32Vec, UInt32Vec);
ZfbTransform_Nested(Int64Vec, Int64Vec);
ZfbTransform_Nested(UInt64Vec, UInt64Vec);
ZfbTransform_Nested(Int128Vec, Int128Vec);
ZfbTransform_Nested(UInt128Vec, UInt128Vec);
ZfbTransform_Nested(FloatVec, FloatVec);
ZfbTransform_Nested(FixedVec, FixedVec);
ZfbTransform_Nested(DecimalVec, DecimalVec);
ZfbTransform_Nested(TimeVec, TimeVec);
ZfbTransform_Nested(DateTimeVec, DateTimeVec);

template <typename U> struct Resolve<ZtFieldTC::UDT, U> {
  using Transformer = ZfbTransformer<U>;
  template <typename O, typename Base>
  using Field = ZuIf<Transformer::IsInline,
    Inline<O, Base, Transformer>,
    Nested<O, Base, Transformer>>;
};
template <> struct Resolve<ZtFieldTC::UDT, ZiIP> {
  template <typename O, typename Base>
  using Field = IP_<O, Base>;
};

} // ZfbTransform

template <typename O, typename Base>
using ZfbFieldT =
  typename ZfbTransform::Resolve<Base::Code, typename Base::T>::
    template Field<O, Base>;

// user-defined type transformers

namespace ZfbTransform {

struct Bitmap {
  enum { IsInline = 0 };
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder, unsigned Bits>
  static auto save(Builder &fbb, const ZuBitmap<Bits> &v) {
    unsigned n = Bits>>6;
    return Zfb::CreateBitmap(
      fbb, Zfb::Save::pvectorIter<uint64_t>(fbb, n, [&v](unsigned i) {
	return v.data[i];
      }));
  }
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static Zfb::Offset<Zfb::Bitmap> save(Builder &fbb, const ZmBitmap &v) {
    int n = v.last();
    if (ZuUnlikely(n <= 0)) return {};
    n = (n + 0x40)>>6;
    return Zfb::CreateBitmap(
      fbb, Zfb::Save::pvectorIter<uint64_t>(fbb, n, [&v](unsigned i) {
	return hwloc_bitmap_to_ith_ulong(v, i);
      }));
  }
  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, const ZtBitmap &v) {
    unsigned n = v.data.length();
    return Zfb::CreateBitmap(
      fbb, Zfb::Save::pvectorIter<uint64_t>(fbb, n, [&v](unsigned i) {
	return v.data[i];
      }));
  }

  template <typename T> struct IsZuBitmap : public ZuFalse { };
  template <unsigned Bits>
  struct IsZuBitmap<ZuBitmap<Bits>> : public ZuTrue { };
  template <typename T, typename R = void>
  using MatchZuBitmap = ZuIfT<IsZuBitmap<T>{}, R>;
  template <typename T, decltype(MatchZuBitmap<T>(), int()) = 0>
  static T load(const Zfb::Bitmap *bitmap) {
    if (!bitmap || !bitmap->data()) return T{};
    auto vec = bitmap->data();
    unsigned n = vec->size();
    T b;
    if (n > T::Words) n = T::Words;
    for (unsigned i = 0; i < n; i++) b.data[i] = vec->Get(i);
    return b;
  }
  // ZmBitmap - hwloc bitmap (variable-size)
  template <typename T, decltype(ZuSame<ZmBitmap, T>(), int()) = 0>
  static T load(const Zfb::Bitmap *bitmap) {
    if (!bitmap || !bitmap->data()) return T{};
    auto vec = bitmap->data();
    unsigned n = vec->size();
    T b;
    if (n) {
      --n;
      hwloc_bitmap_from_ith_ulong(b, n, vec->Get(n));
      while (n--) hwloc_bitmap_set_ith_ulong(b, n, vec->Get(n));
    }
    return b;
  }
  // ZtBitmap - variable-size bitmap
  template <typename T, decltype(ZuSame<ZtBitmap, T>(), int()) = 0>
  static T load(const Zfb::Bitmap *bitmap) {
    if (!bitmap || !bitmap->data()) return T{};
    auto vec = bitmap->data();
    unsigned n = vec->size();
    T b;
    b.data.length(n); // avoid unnecessary zero-fill
    for (unsigned i = 0; i < n; i++) b.data[i] = vec->Get(i);
    return b;
  }
};

struct IP {
  enum { IsInline = 0 };
  struct Save {
    Zfb::IP		type = Zfb::IP::NONE;
    Zfb::Offset<void>	offset;
  };
  static Zfb::IP type(ZiIP addr) {
    switch (addr.type()) {
      case ZiIPType::V4:
	return Zfb::IP::IPv4;
      case ZiIPType::V6:
	return Zfb::IP::IPv6;
      default:
	return Zfb::IP::NONE;
    }
  }
  static Save save(Zfb::Builder &fbb, ZiIP addr) {
    switch (addr.type()) {
      case ZiIPType::V4:
	{
	  auto a = addr.inAddr();
	  Zfb::IPv4 v{Zfb::span<const uint8_t, 4>{
	    reinterpret_cast<const uint8_t *>(&a), sizeof(a)}};
	  return {Zfb::IP::IPv4, fbb.CreateStruct(v).Union()};
	}
      case ZiIPType::V6:
	{
	  auto a = addr.in6Addr();
	  Zfb::IPv6 v{Zfb::span<const uint8_t, 16>{
	    reinterpret_cast<const uint8_t *>(&a), sizeof(a)}};
	  return {Zfb::IP::IPv6, fbb.CreateStruct(v).Union()};
	}
      default:
	return {};
    }
  }
  static ZiIP load(Zfb::IP type, const void *v) {
    if (!v) return {};
    switch (type) {
      case Zfb::IP::IPv4:
	{
	  in_addr addr;
	  memcpy(&addr,
	    static_cast<const Zfb::IPv4 *>(v)->addr()->data(), sizeof(addr));
	  return ZiIP{addr};
	}
      case Zfb::IP::IPv6:
	{
	  in6_addr addr;
	  memcpy(&addr,
	    static_cast<const Zfb::IPv6 *>(v)->addr()->data(), sizeof(addr));
	  return ZiIP{addr};
	}
      default:
	return {};
    }
  }
};

struct ID {
  enum { IsInline = 1 };
  template <typename _ = void>
  static Zfb::ID save(ZuID id) {
    return {Zfb::span<const uint8_t, 8>{
      reinterpret_cast<const uint8_t *>(id.data()), 8}};
  }
  static ZuID load(const Zfb::ID *v) {
    if (!v) return {};
    return {*reinterpret_cast<const uint64_t *>(v->data()->data())};
  }
};

} // ZfbTransform

namespace ZuBitmap_ {
  template <unsigned Bits>
  ZfbTransform::Bitmap ZfbTransformer_(Bitmap<Bits> *);
  template <unsigned Bits>
  inline ZuID ZtVFieldTypeID(Bitmap<Bits> *) { return "Bitmap"; }
}
namespace ZmBitmap_ {
  ZfbTransform::Bitmap ZfbTransformer_(Bitmap *);
  inline ZuID ZtVFieldTypeID(Bitmap *) { return "Bitmap"; }
}
namespace ZtBitmap_ {
  ZfbTransform::Bitmap ZfbTransformer_(Bitmap *);
  inline ZuID ZtVFieldTypeID(Bitmap *) { return "Bitmap"; }
}

ZfbTransform::IP ZfbTransformer_(ZiIP *);
inline ZuID ZtVFieldTypeID(ZiIP *) { return "IP"; }

ZfbTransform::ID ZfbTransformer_(ZuID *);
inline ZuID ZtVFieldTypeID(ZuID *) { return "ID"; }

#define ZfbField_Decl__(O_, ID, Base_, TypeName, Type) \
  ZuField_Decl(O_, Base_) \
  using ZtField(O_, ID##__) = \
    ZtField_##TypeName<ZuField(O_, ID) ZtField_TypeArgs(Type)>; \
  template < \
    typename O = O_, typename Base = ZtField(O_, ID##__), typename Under = O, \
    typename _ = void> \
  struct ZtField(O_, ID##_) { \
    static_assert( \
      ZuAlwaysFalse<_>{}(), \
      #O_ "/" #ID " - flatbuffer / C++ mismatch"); \
  }; \
  template <typename O, typename Base, typename Under> \
  struct ZtField(O_, ID##_)< \
    O, Base, Under, decltype(&Zfb_Type<Under>::ID, void())> : \
      public Base { \
    template <template <typename> class Override> \
    using Adapt = ZtField(O_, ID##_)< \
      typename Override<ZuOrigField<Base>>::O, \
      typename Base::template Adapt<Override>, Under>; \
    template <typename Builder> using SaveFn = decltype(&Builder::add_##ID); \
    template <typename Builder> \
    using SaveTypeFn = decltype(&Builder::add_##ID##_type); \
    template <typename Builder, typename Arg> \
    static void save_(Builder &fbb, Arg &&arg) { \
      fbb.add_##ID(ZuFwd<Arg>(arg)); \
    } \
    template <typename Builder, typename Arg> \
    static void saveType_(Builder &fbb, Arg &&arg) { \
      fbb.add_##ID##_type(ZuFwd<Arg>(arg)); \
    } \
    template <typename FBType> \
    static decltype(auto) load_(const FBType *fbo) { \
      return fbo->ID(); \
    } \
    template <typename FBType> \
    static decltype(auto) loadType_(const FBType *fbo) { \
      return fbo->ID##_type(); \
    } \
  }; \
  using ZtField(O_, ID) = ZfbFieldT<O_, ZtField(O_, ID##_)<>>;
#define ZfbField_Decl_(O, Base, Type) \
  ZuPP_Defer(ZfbField_Decl__)(O, \
      ZuPP_Eval__(ZtField_BaseID(Base)), Base, \
      ZuPP_Eval__(ZtField_TypeName(Type)), Type)
#define ZfbField_Decl(O, Args) ZuPP_Defer(ZfbField_Decl_)(O, ZuPP_Strip(Args))

// ZfbStruct preamble
#define ZfbStruct_Pre(O) \
  fbs::O##Builder ZfbBuilder_(O *); \
  fbs::O ZfbType_(O *); \
  \
  namespace Zfb_Under_ { using O = O; } \
  namespace fbs { Zfb_Under_::O ZuUnderType(O *); }

// ZfbStruct postscript
#define ZfbStruct_Post(O) \
  template <typename Facet> \
  using ZuFields_##O##_FB = ZfbStruct::FBFields<O, Facet>; \
  namespace fbs { \
    template <typename Facet> \
    ZuFields_##O##_FB<Facet> ZuFields_(O *, Facet *); \
    O ZuStructured_(O *); \
    ZtStructPrint ZuPrintType(O *); \
    ZuTrue ZuStructRO_(O *); \
  }

// rendering
#define ZfbStruct_Render_(O, Facet) \
  ZuFields_##O ZuFields_(O *, ZuFacet::Facet *);
#define ZfbStruct_Render(O, ...) \
  ZuPP_Eval_(ZuPP_MapArg(ZfbStruct_Render_, O, __VA_ARGS__))

// main structure declaration macros ZfbStruct()
#define ZfbStruct_(O, Facets, ...) \
  O ZuStructured_(O *); \
  ZfbStruct_Pre(O) \
  __VA_OPT__(ZuPP_MapArg(ZfbField_Decl, O, __VA_ARGS__)) \
  using ZuFields_##O = ZuTypeList< \
    __VA_OPT__(ZuPP_MapArgComma(ZtField_Type, O, __VA_ARGS__))>; \
  ZfbStruct_Render(O, Core ZuPP_StripAppend(Facets)) \
  ZfbStruct_Post(O)

#define ZfbStruct(O_Facets, ...) \
  ZuPP_Eval(ZuPP_Defer(ZfbStruct_)( \
    ZuPP_Eval_(ZuStruct_Object(O_Facets)), \
    ZuPP_Eval_(ZuStruct_Facets(O_Facets)) \
    __VA_OPT__(, __VA_ARGS__)))

// enable load/save for a ZuStructShim<..., ZuFields<Orig>>
#define ZfbEnableShim(O, Orig) \
  fbs::Orig##Builder ZfbBuilder_(O *); \
  fbs::Orig ZfbType_(O *)

// enable load/save for a ZuStructShim<..., ZuFields<OrigNS::Orig>>
#define ZfbEnableShimNS(O, OrigNS, Orig) \
  OrigNS::fbs::Orig##Builder ZfbBuilder_(O *); \
  OrigNS::fbs::Orig ZfbType_(O *)

// bind root schema
#define ZfbRoot(O) \
  fbs::O##Schema ZfbSchema_(O *)

#endif /* ZfbStruct_HH */
