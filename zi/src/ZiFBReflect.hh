//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// FlatBuffers reflection and compile-time BFBS/FBS schema emission

#ifndef ZiFBReflect_HH
#define ZiFBReflect_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuFmt.hh>
#include <zlib/ZuFP.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuStruct.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZiFB.hh>

// --- Canonical BFBS wire declarations; no owning runtime schema graph

namespace ZiFB::Reflect {

// Values and declarations match reflection/reflection.fbs in the canonical
// 25.12.19 implementation. Unsupported payload kinds remain inspectable.
namespace BaseType {
  ZtEnum(ZiAPI, BaseType, int8_t,
    None, UType, Bool, Byte, UByte, Short, UShort, Int, UInt, Long, ULong,
    Float, Double, String, Vector, Obj, Union, Array, Vector64, MaxBaseType);
  ZiFB::Enum<"reflection.BaseType"> ZiFBType(Map *);
}

struct Type {
  int8_t			baseType, element;
  int				index;
  uint16_t			fixedLength;
  unsigned			baseSize, elementSize;
};
ZfStruct(ZiAPI, Type,
  (baseType,	(Deflt<0>, Enum<BaseType::Map>),	Int8),
  (element,	(Deflt<0>, Enum<BaseType::Map>),	Int8),
  (index,	(Deflt<-1>),				Int32),
  (fixedLength,	(Deflt<0>),				UInt16),
  (baseSize,	(Deflt<4>),				UInt32),
  (elementSize,	(Deflt<0>),				UInt32));
Table<"reflection.Type"> ZiFBType(Type *);

struct KeyValue { ZuCSpan key, value; };
ZfStruct(ZiAPI, KeyValue,
  (key,		(Required),	String),
  (value,,			String));
Table<"reflection.KeyValue"> ZiFBType(KeyValue *);

struct Object;
struct EnumVal {
  ZuCSpan			name;
  int64_t			value;
  const Type			*unionType;
  ZuSpan<const ZuCSpan>		documentation;
  ZuSpan<const KeyValue *>	attributes;
};
ZfStruct(ZiAPI, EnumVal,
  (name,				(Required),	String),
  (value,				(Deflt<0>),	Int64),
  ((object, Skip, const Object *),,			UDT),
  (unionType,,						UDT),
  (documentation,,					StringVec),
  (attributes,,						UDT));
Table<"reflection.EnumVal"> ZiFBType(EnumVal *);

struct Enum {
  ZuCSpan			name;
  ZuSpan<const EnumVal *>	values;
  bool				isUnion;
  const Type			*underlyingType;
  ZuSpan<const KeyValue *>	attributes;
  ZuSpan<const ZuCSpan>		documentation;
  ZuCSpan			declarationFile;
};
ZfStruct(ZiAPI, Enum,
  (name,		(Required),	String),
  (values,		(Required),	UDT),
  (isUnion,		(Deflt<false>),	Bool),
  (underlyingType,	(Required),	UDT),
  (attributes,,				UDT),
  (documentation,,			StringVec),
  (declarationFile,,			String));
Table<"reflection.Enum"> ZiFBType(Enum *);

struct Field {
  ZuCSpan			name;
  const Type			*type;
  uint16_t			id, offset;
  int64_t			defaultInteger;
  double			defaultReal;
  bool				deprecated, required, key;
  ZuSpan<const KeyValue *>	attributes;
  ZuSpan<const ZuCSpan>		documentation;
  bool				optional;
  uint16_t			padding;
  bool				offset64;
};
ZfStruct(ZiAPI, Field,
  (name,		(Required),	String),
  (type,		(Required),	UDT),
  (id,			(Deflt<0>),	UInt16),
  (offset,		(Deflt<0>),	UInt16),
  (defaultInteger,	(Deflt<0>),	Int64),
  (defaultReal,		(Deflt<0.0>),	Float),
  (deprecated,		(Deflt<false>),	Bool),
  (required,		(Deflt<false>),	Bool),
  (key,			(Deflt<false>),	Bool),
  (attributes,,				UDT),
  (documentation,,			StringVec),
  (optional,		(Deflt<false>),	Bool),
  (padding,		(Deflt<0>),	UInt16),
  (offset64,		(Deflt<false>),	Bool));
Table<"reflection.Field"> ZiFBType(Field *);

struct Object {
  ZuCSpan			name;
  ZuSpan<const Field *>		fields;
  bool				isStruct;
  int				minalign, bytesize;
  ZuSpan<const KeyValue *>	attributes;
  ZuSpan<const ZuCSpan>		documentation;
  ZuCSpan			declarationFile;
};
ZfStruct(ZiAPI, Object,
  (name,		(Required),	String),
  (fields,		(Required),	UDT),
  (isStruct,		(Deflt<false>),	Bool),
  (minalign,		(Deflt<0>),	Int32),
  (bytesize,		(Deflt<0>),	Int32),
  (attributes,,				UDT),
  (documentation,,			StringVec),
  (declarationFile,,			String));
Table<"reflection.Object"> ZiFBType(Object *);

struct RPCCall {
  ZuCSpan			name;
  const Object			*request, *response;
  ZuSpan<const KeyValue *>	attributes;
  ZuSpan<const ZuCSpan>		documentation;
};
ZfStruct(ZiAPI, RPCCall,
  (name,		(Required),	String),
  (request,		(Required),	UDT),
  (response,		(Required),	UDT),
  (attributes,,				UDT),
  (documentation,,			StringVec));
Table<"reflection.RPCCall"> ZiFBType(RPCCall *);

struct Service {
  ZuCSpan			name;
  ZuSpan<const RPCCall *>	calls;
  ZuSpan<const KeyValue *>	attributes;
  ZuSpan<const ZuCSpan>		documentation;
  ZuCSpan			declarationFile;
};
ZfStruct(ZiAPI, Service,
  (name,		(Required),	String),
  (calls,,				UDT),
  (attributes,,				UDT),
  (documentation,,			StringVec),
  (declarationFile,,			String));
Table<"reflection.Service"> ZiFBType(Service *);

struct SchemaFile {
  ZuCSpan			filename;
  ZuSpan<const ZuCSpan>		includedFilenames;
};
ZfStruct(ZiAPI, SchemaFile,
  (filename,		(Required),	String),
  (includedFilenames,,			StringVec));
Table<"reflection.SchemaFile"> ZiFBType(SchemaFile *);

struct Schema {
  ZuSpan<const Object *>	objects;
  ZuSpan<const Enum *>		enums;
  ZuCSpan			fileIdent, fileExt;
  const Object			*rootTable;
  ZuSpan<const Service *>	services;
  uint64_t			advancedFeatures;
  ZuSpan<const SchemaFile *>	fbsFiles;
};
ZfStruct(ZiAPI, Schema,
  (objects,		(Required),	UDT),
  (enums,		(Required),	UDT),
  (fileIdent,,				String),
  (fileExt,,				String),
  (rootTable,,				UDT),
  (services,,				UDT),
  (advancedFeatures,	(Deflt<0>),	UInt64),
  (fbsFiles,,				UDT));
Table<"reflection.Schema"> ZiFBType(Schema *);

using SchemaView = ZiFB::View<Schema>;
using ObjectView = ZiFB::View<Object>;
using FieldView = ZiFB::View<Field>;
using TypeView = ZiFB::View<Type>;
using EnumView = ZiFB::View<Enum>;
using EnumValView = ZiFB::View<EnumVal>;

// Only the external-schema entry point runs both structural and semantic
// checks. Generated BFBS goes straight to view<Schema>() instead.
ZiAPI bool schemaLayout(SchemaView);
template <bool SizePrefix = false>
bool verifySchema(const ZuBSpan &bytes, VerifyLimits limits = {}) {
  return ZiFB::verify<Schema, SizePrefix>(bytes, "BFBS", limits) &&
    schemaLayout(ZiFB::view<Schema, SizePrefix>(bytes));
}

// These handles borrow BFBS bytes; stores retain them in existing field
// associations. Generated or previously verified schemas need no recheck.
ZiAPI FieldView field(ObjectView, ZuCSpan name);
ZiAPI FieldView field(ObjectView, unsigned id);
ZiAPI ObjectView object(SchemaView, ZuCSpan name);
ZiAPI EnumValView enumValue(EnumView, int64_t value);
ZiAPI unsigned alignment(FieldView);
ZiAPI unsigned bufferAlign(SchemaView);
ZiAPI unsigned bufferAlign(SchemaView, FieldView);

inline TypeView type(FieldView f) { return f.get<ZfField(Field, type)>(); }
inline ObjectView rootObject(SchemaView s) { return s.get<ZfField(Schema, rootTable)>(); }
inline ObjectView object(SchemaView s, TypeView t) {
  return s.get<ZfField(Schema, objects)>()[t.get<ZfField(Type, index)>()];
}
inline const uint8_t *address(TableView table, FieldView f) {
  return table.field(f.get<ZfField(Field, id)>());
}
template <typename T>
T deflt(FieldView f) {
  if constexpr (ZuTraits<T>::IsFloatingPoint) return f.get<ZfField(Field, defaultReal)>();
  else return f.get<ZfField(Field, defaultInteger)>();
}
template <typename T>
T scalar(TableView table, FieldView f) {
  auto p = address(table, f);
  return p ? read<T>(p) : deflt<T>(f);
}
inline VectorView vector(TableView table, FieldView f) {
  return table.vector(f.get<ZfField(Field, id)>());
}
inline ZuCSpan string(TableView table, FieldView f) { return vector(table, f).string(); }
inline const uint8_t *object(SchemaView s, TableView table, FieldView f) {
  auto p = address(table, f);
  if (!p) return nullptr;
  return object(s, type(f)).get<ZfField(Object, isStruct)>() ? p : follow(p);
}
inline uint8_t tag(TableView table, FieldView f) {
  return table.scalar<uint8_t>(f.get<ZfField(Field, id)>() - 1, 0);
}

// Dynamic stores choose native conversions using their existing XField
// association. These writers supply schema defaults, IDs and wire layout;
// they do not construct native rows or an auxiliary descriptor graph.
template <typename T, typename B>
void saveScalar(B &b, TableState &table, FieldView f, T value, bool force = false) {
  b.template field<T>(table, f.get<ZfField(Field, id)>(), value, deflt<T>(f), force);
}
template <typename B>
void saveOffset(B &b, TableState &table, FieldView f, Offset value) {
  if (!value && f.get<ZfField(Field, required)>()) b.fail(Error::Required);
  else b.fieldOffset(table, f.get<ZfField(Field, id)>(), value);
}
template <typename B, typename Fill>
void saveInline(B &b, TableState &table, SchemaView s, FieldView f, Fill &&fill) {
  auto o = object(s, type(f));
  b.fieldInline(table, f.get<ZfField(Field, id)>(),
    o.get<ZfField(Object, bytesize)>(), o.get<ZfField(Object, minalign)>(), ZuFwd<Fill>(fill));
}
template <typename B>
void saveUnion(B &b, TableState &table, FieldView f, uint8_t tag, Offset value) {
  if (bool(tag) != bool(value)) { b.fail(Error::Union); return; }
  if (!tag && f.get<ZfField(Field, required)>()) { b.fail(Error::Required); return; }
  auto id = f.get<ZfField(Field, id)>();
  b.template field<uint8_t>(table, id - 1, tag, 0);
  b.fieldOffset(table, id, value);
}

// External payloads use these visitors over an already trusted schema.
// They share the typed reader's bounds primitives and traversal budgets.
ZiAPI bool verifyObject(Verifier &, SchemaView, ObjectView, unsigned at);
template <bool SizePrefix = false>
bool verify(SchemaView s, ObjectView o, const ZuBSpan &bytes,
  const char *id = nullptr, VerifyLimits limits = {}) {
  Verifier v{bytes, limits};
  unsigned at;
  return o && v.template root<SizePrefix>(at, id) && verifyObject(v, s, o, at);
}
template <bool SizePrefix = false>
bool verify(SchemaView s, const ZuBSpan &bytes,
  const char *id = nullptr, VerifyLimits limits = {}) {
  return verify<SizePrefix>(s, rootObject(s), bytes, id, limits);
}

} // ZiFB::Reflect

// --- Forward-only BFBS emission primitives with compile-time table layout

namespace ZiFB::SchemaOut {

template <typename T, typename S>
void scalar(S &stream, T value) {
  ZuLittleEndian<T> bytes{value};
  stream << ZuCSpan{reinterpret_cast<const char *>(&bytes), sizeof(bytes)};
}

template <typename S>
void zero(S &stream, unsigned n) {
  const uint64_t bytes = 0;
  // Runtime branches here implement byte streaming, not schema decisions.
  while (n) {
    unsigned count = n < sizeof(bytes) ? n : sizeof(bytes);
    stream << ZuCSpan{reinterpret_cast<const char *>(&bytes), count};
    n -= count;
  }
}

constexpr unsigned round(unsigned n, unsigned align) {
  return (n + align - 1) & ~(align - 1);
}

// Every emitted BFBS table starts at its natural alignment. Its vtable is
// immediately before its data, with alignment padding between them. Only
// the listed fields are emitted. IDs and scalar widths come from ZfStruct.
template <typename O, typename ...F>
struct Table {
  using Fields = ZuTypeList<F...>;
  using M = Meta<O>;
  static consteval unsigned alignment() {
    unsigned align = sizeof(SOffset);
    ZuUnroll::all<Fields>([&align]<typename G>() {
      using V = FieldWire<G>;
      ZuAssert(V::Kind == ValueKind::Scalar || V::Kind == ValueKind::String ||
	V::Kind == ValueKind::Vector || V::Kind == ValueKind::Table);
      if (align < V::Align) align = V::Align;
    });
    return align;
  }
  enum { Align = alignment(), VSize = Wire::VHeader + M::N * sizeof(VOffset) };
  enum { Data = round(VSize, Align) };

  template <typename G>
  static consteval unsigned offset() {
    unsigned n = sizeof(SOffset);
    ZuUnroll::all<Fields>([&n]<typename H>() {
      if constexpr (ZuTypeIndex<H, Fields>{} <= ZuTypeIndex<G, Fields>{}) {
	n = round(n, FieldWire<H>::Align);
	if constexpr (!ZuIsSame<G, H>{}) n += FieldWire<H>::Size;
      }
    });
    return n;
  }
  template <typename G>
  static consteval unsigned previousEnd() {
    unsigned n = sizeof(SOffset);
    ZuUnroll::all<Fields>([&n]<typename H>() {
      if constexpr (ZuTypeIndex<H, Fields>{} < ZuTypeIndex<G, Fields>{})
	n = offset<H>() + FieldWire<H>::Size;
    });
    return n;
  }
  static consteval unsigned end() {
    unsigned n = sizeof(SOffset);
    ZuUnroll::all<Fields>([&n]<typename G>() { n = offset<G>() + FieldWire<G>::Size; });
    return n;
  }
  enum { End = end(), ObjectSize = round(End, Align), Size = Data + ObjectSize };
  ZuAssert(VSize <= UINT16_MAX && ObjectSize <= UINT16_MAX);

  template <unsigned ID>
  static consteval unsigned slot() {
    unsigned n = 0;
    ZuUnroll::all<Fields>([&n]<typename G>() {
      if constexpr (M::template id<G>() == ID) n = offset<G>();
    });
    return n;
  }
  template <typename S, typename Values>
  static void emit(S &stream, Values &&values) {
    scalar<VOffset>(stream, VSize);
    scalar<VOffset>(stream, ObjectSize);
    ZuUnroll::all<M::N>([&stream](auto id) {
      scalar<VOffset>(stream, slot<id>());
    });
    zero(stream, Data - VSize);
    scalar<SOffset>(stream, Data);
    ZuUnroll::all<Fields>([&stream, &values]<typename G>() {
      using V = FieldWire<G>;
      zero(stream, offset<G>() - previousEnd<G>());
      if constexpr (V::Kind == ValueKind::Scalar)
	scalar<typename V::WireType>(stream, values.template operator()<G>());
      else
	scalar<Offset>(stream, values.template operator()<G>());
    });
    zero(stream, ObjectSize - End);
  }
};

template <ZuString Text>
struct String {
  enum { Size = round(sizeof(Offset) + Text.length() + 1, sizeof(Offset)) };
  template <typename S>
  static void emit(S &stream) {
    scalar<Offset>(stream, Text.length());
    stream << Text.cspan();
    zero(stream, Size - sizeof(Offset) - Text.length());
  }
};

ZuDerive(AttrText, (ZtString<ZtStringHeapID<"ZiFB.AttrText">>));

template <unsigned N>
struct AlignString {
  ZuAssert(N && N <= Wire::MaxAlign);
  // Wire::MaxAlign is 32, so its decimal spelling has at most two digits.
  enum { Length = N < 10 ? 1 : 2 };
  enum { Size = round(sizeof(Offset) + Length + 1, sizeof(Offset)) };
  template <typename S>
  static void emit(S &stream) {
    auto text = ZtScratch(AttrText, Length);
    text << ZuBox<unsigned>{N};
    scalar<Offset>(stream, Length);
    stream << text.cspan();
    zero(stream, Size - sizeof(Offset) - Length);
  }
};

} // ZiFB::SchemaOut

// --- Compile-time-specialized in-memory canonical BFBS emission

namespace ZiFB::SchemaOut {

namespace R = Reflect;

template <typename Map_, typename W, bool Flags_ = false>
struct EnumRef {
  using Map = Map_;
  using WireType = W;
  enum { Flags = Flags_ };
  friend Type<Map> ZiFBType(EnumRef *);
};

template <typename F, bool = ZuFieldProp::HasFlags<typename F::Props>{}>
struct EnumMap { using T = ZuFieldProp::GetEnum<typename F::Props>; };
template <typename F>
struct EnumMap<F, true> { using T = ZuFieldProp::GetFlags<typename F::Props>; };

template <typename V, unsigned Kind_ = V::Kind>
struct EnumScalar { using T = typename V::WireType; };
template <typename V>
struct EnumScalar<V, ValueKind::Vector> : public EnumScalar<typename V::Elem> { };
template <typename V>
struct EnumScalar<V, ValueKind::Array> : public EnumScalar<typename V::Elem> { };

template <typename F, bool = ZuFieldProp::HasEnum<typename F::Props>{} ||
  ZuFieldProp::HasFlags<typename F::Props>{}>
struct FieldEnum { using Types = ZuTypeList<>; enum { Present = false }; };
template <typename F>
struct FieldEnum<F, true> {
  using V = FieldWire<F>;
  using Map = typename EnumMap<F>::T;
  using W = typename EnumScalar<V>::T;
  using Names = typename Map::Names;
  enum { Flags = ZuFieldProp::HasFlags<typename F::Props>{} };
  ZuAssert(!ZuTraits<W>::IsFloatingPoint);
  ZuAssert(!Flags || Names::N <= (sizeof(W)<<3) - unsigned(ZuTraits<W>::IsSigned));
  static consteval bool representable() {
    if constexpr (Flags || V::Kind != ValueKind::Scalar || IsInline<ZuUnder<typename F::O>>{})
      return true;
    else return defaultValue<F>() >= 0 && uint64_t(defaultValue<F>()) < Names::N;
  }
  // Canonical enum defaults must name an enumerator. A Z null/default outside
  // the map has no such representation: retain the scalar value and omit the
  // enum annotation (Q8), without inventing another enumerator or default.
  enum { Present = representable() };
  using E = EnumRef<Map, W, Flags>;
  using Types = ZuIf<Present, ZuTypeList<E>, ZuTypeList<>>;
};

template <typename F> using FieldDeps = ZuTypeConcat<
  typename WireDeps<FieldWire<F>>::T, typename FieldEnum<F>::Types>;

template <typename O, typename Facet = ZuFacet::Core, unsigned Kind_ = Type<O>::Kind>
struct Children {
  using T = ZuTypeApply<ZuTypeConcat, ZuTypeMap<FieldDeps, typename Meta<O, Facet>::Fields>>;
};
template <typename O, typename Facet>
struct Children<O, Facet, Kind::Union> {
  using T = ZuTypeApply<ZuTypeConcat, ZuTypeMap<WireAltDeps, typename Type<O>::Types>>;
};
template <typename O, typename Facet>
struct Children<O, Facet, Kind::Enum> { using T = ZuTypeList<>; };

template <ZuString Name>
struct NameIndex {
  constexpr const NameIndex &operator ()() const { return *this; }
  template <ZuString Other>
  constexpr bool operator >(NameIndex<Other>) const { return Name > Other; }
};
template <typename O> using ObjectName = NameIndex<Type<O>::name()>;
template <typename O> using IsObject = ZuBool<
  Type<O>::Kind == Kind::Table || Type<O>::Kind == Kind::Inline>;
template <typename O> using IsEnum = ZuBool<!IsObject<O>{}>;

template <typename F, bool Tag_ = false>
struct Slot {
  using Field = F;
  enum { Tag = Tag_ };
  static consteval auto name() {
    if constexpr (Tag) return ZuFieldProp::FB::name<F>() + ZuString{"_type"};
    else return ZuFieldProp::FB::name<F>();
  }
};
template <typename F>
using Slots = ZuIf<FieldWire<F>::Kind == ValueKind::Union,
  ZuTypeList<Slot<F>, Slot<F, true>>, ZuTypeList<Slot<F>>>;
template <typename S> using SlotName = NameIndex<S::name()>;

template <typename Root, typename Facet = ZuFacet::Core>
struct Graph {
  template <typename O> using Children_ = Children<O, Facet>;
  template <typename O> using Fields = typename Meta<O, Facet>::Fields;
  using Unsorted = typename Reach<ZuTypeList<ZuUnder<Root>>, ZuTypeList<>, Children_>::T;
  using Objects = ZuTypeSort<ObjectName, ZuTypeGrep<IsObject, Unsorted>>;
  using Enums = ZuTypeSort<ObjectName, ZuTypeGrep<IsEnum, Unsorted>>;
  static consteval bool namesUnique() {
    bool ok = true;
    ZuUnroll::all<Unsorted>([&ok]<typename O>() {
      ZuUnroll::all<Unsorted>([&ok]<typename P>() {
	if constexpr (!ZuIsSame<O, P>{})
	  if (Type<O>::name() == Type<P>::name()) ok = false;
      });
    });
    return ok;
  }
  ZuAssert(namesUnique());
  static consteval bool arrays() {
    bool found = false;
    ZuUnroll::all<Objects>([&found]<typename O>() {
      ZuUnroll::all<Fields<O>>([&found]<typename F>() {
	if constexpr (FieldWire<F>::Kind == ValueKind::Array) found = true;
      });
    });
    return found;
  }
};

using TypeTable = Table<R::Type,
  R::ZfField(Type, baseType), R::ZfField(Type, element), R::ZfField(Type, index),
  R::ZfField(Type, fixedLength), R::ZfField(Type, baseSize), R::ZfField(Type, elementSize)>;
using FieldTable = Table<R::Field,
  R::ZfField(Field, name), R::ZfField(Field, type), R::ZfField(Field, id),
  R::ZfField(Field, offset), R::ZfField(Field, defaultInteger),
  R::ZfField(Field, defaultReal), R::ZfField(Field, deprecated),
  R::ZfField(Field, required), R::ZfField(Field, optional), R::ZfField(Field, padding)>;
using AlignedFieldTable = Table<R::Field,
  R::ZfField(Field, name), R::ZfField(Field, type), R::ZfField(Field, id),
  R::ZfField(Field, offset), R::ZfField(Field, defaultInteger),
  R::ZfField(Field, defaultReal), R::ZfField(Field, deprecated),
  R::ZfField(Field, required), R::ZfField(Field, optional), R::ZfField(Field, padding),
  R::ZfField(Field, attributes)>;
using ObjectTable = Table<R::Object,
  R::ZfField(Object, name), R::ZfField(Object, fields), R::ZfField(Object, isStruct),
  R::ZfField(Object, minalign), R::ZfField(Object, bytesize)>;
using AlignedObjectTable = Table<R::Object,
  R::ZfField(Object, name), R::ZfField(Object, fields), R::ZfField(Object, isStruct),
  R::ZfField(Object, minalign), R::ZfField(Object, bytesize), R::ZfField(Object, attributes)>;
using EnumTable = Table<R::Enum,
  R::ZfField(Enum, name), R::ZfField(Enum, values), R::ZfField(Enum, isUnion),
  R::ZfField(Enum, underlyingType)>;
using FlagsTable = Table<R::Enum,
  R::ZfField(Enum, name), R::ZfField(Enum, values), R::ZfField(Enum, isUnion),
  R::ZfField(Enum, underlyingType), R::ZfField(Enum, attributes)>;
using EnumValTable = Table<R::EnumVal,
  R::ZfField(EnumVal, name), R::ZfField(EnumVal, value), R::ZfField(EnumVal, unionType)>;
using RootTable = Table<R::Schema,
  R::ZfField(Schema, objects), R::ZfField(Schema, enums), R::ZfField(Schema, rootTable),
  R::ZfField(Schema, advancedFeatures)>;

template <typename V>
consteval int baseType() {
  using namespace R::BaseType;
  if constexpr (V::Kind == ValueKind::Scalar) {
    using T = typename V::WireType;
    if constexpr (ZuTraits<T>::IsFloatingPoint) {
      if constexpr (sizeof(T) == 4) return Float;
      else return Double;
    } else if constexpr (ZuTraits<T>::IsSigned) {
      if constexpr (sizeof(T) == 1) return Byte;
      else if constexpr (sizeof(T) == 2) return Short;
      else if constexpr (sizeof(T) == 4) return Int;
      else return Long;
    } else {
      if constexpr (sizeof(T) == 1) return UByte;
      else if constexpr (sizeof(T) == 2) return UShort;
      else if constexpr (sizeof(T) == 4) return UInt;
      else return ULong;
    }
  } else if constexpr (V::Kind == ValueKind::String) return R::BaseType::String;
  else if constexpr (V::Kind == ValueKind::Vector) return R::BaseType::Vector;
  else if constexpr (V::Kind == ValueKind::Array) return R::BaseType::Array;
  else if constexpr (V::Kind == ValueKind::Table || V::Kind == ValueKind::Inline) return Obj;
  else return R::BaseType::Union;
}

template <typename V, typename G>
consteval int typeIndex() {
  if constexpr (V::Kind == ValueKind::Table || V::Kind == ValueKind::Inline)
    return ZuTypeIndex<typename V::Native, typename G::Objects>{};
  else if constexpr (V::Kind == ValueKind::Union)
    return ZuTypeIndex<typename V::Native, typename G::Enums>{};
  else if constexpr (V::Kind == ValueKind::Vector || V::Kind == ValueKind::Array)
    return typeIndex<typename V::Elem, G>();
  else return -1;
}

template <ZuString Key_, typename Value>
struct Attribute {
  using KV = Table<R::KeyValue, R::ZfField(KeyValue, key), R::ZfField(KeyValue, value)>;
  using Key = String<Key_>;
  enum { NodeAt = 2 * sizeof(Offset), KeyAt = NodeAt + KV::Size };
  enum { ValueAt = KeyAt + Key::Size, Size = round(ValueAt + Value::Size, EnumValTable::Align) };
  template <typename S>
  static void emit(S &stream) {
    scalar<Offset>(stream, 1);
    scalar<Offset>(stream, NodeAt + KV::Data - sizeof(Offset));
    KV::emit(stream, []<typename F>() {
      constexpr unsigned from = NodeAt + KV::Data + KV::template offset<F>();
      if constexpr (Meta<R::KeyValue>::template id<F>() == 0) return KeyAt - from;
      else return ValueAt - from;
    });
    Key::emit(stream);
    Value::emit(stream);
    zero(stream, Size - ValueAt - Value::Size);
  }
};

template <unsigned N> using AlignAttr = Attribute<"force_align", AlignString<N>>;
using FlagsAttr = Attribute<"bit_flags", String<"0">>;

template <typename O, typename Entry, typename G>
struct FieldNode {
  using F = typename Entry::Field;
  enum { Tag = Entry::Tag, ID = Meta<O>::template id<F>() - Tag };
  using V = FieldWire<F>;
  enum { IsInline = Type<O>::Kind == Kind::Inline };
  enum { Align = !Tag && !IsInline ? V::ForcedAlign : 0 };
  using Header = ZuIf<Align != 0, AlignedFieldTable, FieldTable>;
  using Name = String<Entry::name()>;
  enum { Data = Header::Data, NameAt = Header::Size, TypeAt = NameAt + Name::Size };
  enum { AttrAt = round(TypeAt + TypeTable::Size, Header::Align) };
  static consteval unsigned size() {
    if constexpr (Align) return AttrAt + AlignAttr<Align>::Size;
    else return AttrAt;
  }
  enum { Size = size() };

  static consteval unsigned padding() {
    if constexpr (!IsInline) return 0;
    else {
      using Fields = ZuFields<O>;
      constexpr unsigned i = ID;
      unsigned end;
      if constexpr (i + 1 < Fields::N)
	end = Layout<O>::template offset<ZuType<i + 1, Fields>>();
      else end = Layout<O>::Size;
      return end - Layout<O>::template offset<ZuType<i, Fields>>() - V::Size;
    }
  }
  template <typename S>
  static void emit(S &stream) {
    Header::emit(stream, []<typename H>() {
      constexpr unsigned id = Meta<R::Field>::template id<H>();
      if constexpr (id == 0) return NameAt - Header::Data - Header::template offset<H>();
      else if constexpr (id == 1)
	return TypeAt + TypeTable::Data - Header::Data - Header::template offset<H>();
      else if constexpr (id == 2) return ID;
      else if constexpr (id == 3) {
	if constexpr (IsInline) return Layout<O>::template offset<ZuType<ID, ZuFields<O>>>();
	else return Wire::VHeader + ID * sizeof(VOffset);
      } else if constexpr (id == 4) {
	if constexpr (!IsInline && V::Kind == ValueKind::Scalar) {
	  if constexpr (!ZuTraits<typename V::WireType>::IsFloatingPoint)
	    return int64_t(defaultValue<F>());
	  else return int64_t(0);
	} else return int64_t(0);
      } else if constexpr (id == 5) {
	if constexpr (!IsInline && V::Kind == ValueKind::Scalar) {
	  if constexpr (ZuTraits<typename V::WireType>::IsFloatingPoint)
	    return double(defaultValue<F>());
	  else return 0.0;
	} else return 0.0;
      } else if constexpr (id == 6) return !ZuFieldActive<F>{};
      else if constexpr (id == 7) return !Tag && V::Required;
      else if constexpr (id == 9) return AttrAt - Header::Data - Header::template offset<H>();
      else if constexpr (id == 11)
	return !Tag && !IsInline && !V::Required && V::Kind != ValueKind::Scalar;
      else return padding();
    });
    Name::emit(stream);
    TypeTable::emit(stream, []<typename H>() {
      constexpr unsigned id = Meta<R::Type>::template id<H>();
      if constexpr (id == 0) {
	if constexpr (Tag) return R::BaseType::UType;
	else if constexpr (V::Code_ == ZfFieldTC::Bool) return R::BaseType::Bool;
	else return baseType<V>();
      } else if constexpr (id == 1) {
	if constexpr (V::Kind == ValueKind::Vector || V::Kind == ValueKind::Array)
	  return baseType<typename V::Elem>();
	else return R::BaseType::None;
      } else if constexpr (id == 2) {
	if constexpr (FieldEnum<F>::Present)
	  return ZuTypeIndex<typename FieldEnum<F>::E, typename G::Enums>{}();
	else return typeIndex<V, G>();
      }
      else if constexpr (id == 3) {
	if constexpr (V::Kind == ValueKind::Array) return unsigned(V::Count);
	else return 0U;
      }
      else if constexpr (id == 4) {
	if constexpr (Tag) return 1U;
	else if constexpr (V::Kind == ValueKind::Scalar) return unsigned(V::Size);
	else return unsigned(sizeof(Offset));
      } else {
	if constexpr (V::Kind == ValueKind::Vector) return unsigned(V::Elem::Size);
	else if constexpr (V::Kind == ValueKind::Array) {
	  if constexpr (V::Elem::Kind == ValueKind::Inline) return unsigned(sizeof(Offset));
	  else return unsigned(V::Elem::Size);
	}
	else return 1U; // canonical SizeOf(None), distinct from the BFBS field default
      }
    });
    zero(stream, AttrAt - TypeAt - TypeTable::Size);
    if constexpr (Align) AlignAttr<Align>::emit(stream);
  }
};

template <typename O, typename G>
struct ObjectNode {
  static consteval unsigned forcedAlign() {
    if constexpr (Type<O>::Kind == Kind::Inline) return Layout<O>::ForcedAlign;
    else return 0;
  }
  enum { Align = forcedAlign() };
  using Header = ZuIf<Align != 0, AlignedObjectTable, ObjectTable>;
  using Fields = ZuTypeSort<SlotName,
    ZuTypeApply<ZuTypeConcat, ZuTypeMap<Slots, typename G::template Fields<O>>>>;
  using Name = String<Type<O>::name()>;
  enum { Data = Header::Data, NameAt = Header::Size, FieldsAt = NameAt + Name::Size };
  enum { VecEnd = FieldsAt + sizeof(Offset) * (Fields::N + 1) };
  enum { NodesAt = round(VecEnd, FieldTable::Align) };
  template <typename F>
  static consteval unsigned fieldAt() {
    unsigned n = NodesAt;
    ZuUnroll::all<Fields>([&n]<typename H>() {
      if constexpr (ZuTypeIndex<H, Fields>{} < ZuTypeIndex<F, Fields>{})
	n += FieldNode<O, H, G>::Size;
    });
    return n;
  }
  static consteval unsigned fieldsEnd() {
    unsigned n = NodesAt;
    ZuUnroll::all<Fields>([&n]<typename F>() { n += FieldNode<O, F, G>::Size; });
    return n;
  }
  enum { AttrAt = fieldsEnd() };
  static consteval unsigned size() {
    if constexpr (Align) return AttrAt + AlignAttr<Align>::Size;
    else return AttrAt;
  }
  enum { Size = size() };
  template <typename S>
  static void emit(S &stream) {
    Header::emit(stream, []<typename F>() {
      constexpr unsigned id = Meta<R::Object>::template id<F>();
      if constexpr (id == 0) return NameAt - Header::Data - Header::template offset<F>();
      else if constexpr (id == 1) return FieldsAt - Header::Data - Header::template offset<F>();
      else if constexpr (id == 2) return Type<O>::Kind == Kind::Inline;
      else if constexpr (id == 3) {
	if constexpr (Type<O>::Kind == Kind::Inline) return unsigned(Layout<O>::Align);
	else return 1U;
      } else if constexpr (id == 4) {
	if constexpr (Type<O>::Kind == Kind::Inline) return unsigned(Layout<O>::Size);
	else return 0U;
      } else return AttrAt - Header::Data - Header::template offset<F>();
    });
    Name::emit(stream);
    scalar<Offset>(stream, Fields::N);
    ZuUnroll::all<Fields>([&stream]<typename F>() {
      constexpr unsigned from = FieldsAt + sizeof(Offset) * (1 + ZuTypeIndex<F, Fields>{});
      scalar<Offset>(stream, fieldAt<F>() + FieldNode<O, F, G>::Data - from);
    });
    zero(stream, NodesAt - VecEnd);
    ZuUnroll::all<Fields>([&stream]<typename F>() { FieldNode<O, F, G>::emit(stream); });
    if constexpr (Align) AlignAttr<Align>::emit(stream);
  }
};

template <typename U, bool = IsUnion<U>{}>
struct EnumInfo {
  using Names = typename U::Map::Names;
  using W = typename U::WireType;
  enum { N = Names::N, Base = baseType<Scalar<W>>(), Width = sizeof(W), Flags = U::Flags };
  template <unsigned I> static consteval auto name() { return ZuType<I, Names>{}(); }
  template <unsigned I> static consteval int64_t value() {
    if constexpr (Flags) return int64_t(uint64_t(1) << I);
    else return I;
  }
};
template <typename U>
struct EnumInfo<U, true> {
  using Alts = typename Type<U>::Types;
  enum { N = Alts::N + 1, Base = R::BaseType::UType, Width = 1, Flags = false };
  template <unsigned I> static consteval int64_t value() { return I; }
  template <unsigned I>
  static consteval auto name() {
    if constexpr (!I) return ZuString{"NONE"};
    else return ZuType<I - 1, Alts>::name();
  }
};

// Enum values are keyed by number. Union NONE has no payload type;
// multiple union names may refer to the same object index.
template <typename U, unsigned I, typename G>
struct EnumValNode {
  using Name = String<EnumInfo<U>::template name<I>()>;
  enum { Payload = IsUnion<U>{} && I != 0 };
  enum { NameAt = EnumValTable::Size, TypeAt = NameAt + Name::Size };
  enum { End = TypeAt + TypeTable::Size, Size = round(End, EnumValTable::Align) };
  template <typename S>
  static void emit(S &stream) {
    EnumValTable::emit(stream, []<typename F>() {
      constexpr unsigned id = Meta<R::EnumVal>::template id<F>();
      constexpr unsigned from = EnumValTable::Data + EnumValTable::template offset<F>();
      if constexpr (id == 0) return NameAt - from;
      else if constexpr (id == 1) return EnumInfo<U>::template value<I>();
      else return TypeAt + TypeTable::Data - from;
    });
    Name::emit(stream);
    TypeTable::emit(stream, []<typename F>() {
      constexpr unsigned id = Meta<R::Type>::template id<F>();
      if constexpr (id == 0) {
	if constexpr (Payload) return R::BaseType::Obj;
	else return R::BaseType::None;
      } else if constexpr (id == 2) {
	if constexpr (!Payload) return -1;
	else {
	  using A = ZuType<I - 1, typename Type<U>::Types>;
	  return typeIndex<Value<typename A::T, ZfFieldTC::UDT>, G>();
	}
      } else if constexpr (id == 4) return Payload ? 4U : 1U;
      else if constexpr (id == 5) return 1U;
      else return 0U;
    });
    zero(stream, Size - End);
  }
};

template <typename U, typename G>
struct EnumNode {
  using Name = String<Type<U>::name()>;
  using Info = EnumInfo<U>;
  using Header = ZuIf<Info::Flags, FlagsTable, EnumTable>;
  enum { Data = Header::Data, N = Info::N, NameAt = Header::Size };
  enum { TypeAt = NameAt + Name::Size, ValuesAt = TypeAt + TypeTable::Size };
  enum { VecEnd = ValuesAt + sizeof(Offset) * (N + 1) };
  enum { NodesAt = round(VecEnd, EnumValTable::Align) };
  template <unsigned I>
  static consteval unsigned valueAt() {
    unsigned n = NodesAt;
    ZuUnroll::all<I>([&n](auto i) { n += EnumValNode<U, i, G>::Size; });
    return n;
  }
  enum { AttrAt = valueAt<N>(), Size = AttrAt + (Info::Flags ? FlagsAttr::Size : 0) };
  template <typename S>
  static void emit(S &stream) {
    Header::emit(stream, []<typename F>() {
      constexpr unsigned id = Meta<R::Enum>::template id<F>();
      constexpr unsigned from = Header::Data + Header::template offset<F>();
      if constexpr (id == 0) return NameAt - from;
      else if constexpr (id == 1) return ValuesAt - from;
      else if constexpr (id == 2) return bool(IsUnion<U>{});
      else if constexpr (id == 3) return TypeAt + TypeTable::Data - from;
      else return AttrAt - from;
    });
    Name::emit(stream);
    TypeTable::emit(stream, []<typename F>() {
      constexpr unsigned id = Meta<R::Type>::template id<F>();
      if constexpr (id == 0) return Info::Base;
      else if constexpr (id == 2) return ZuTypeIndex<U, typename G::Enums>{}();
      else if constexpr (id == 4) return unsigned(Info::Width);
      else if constexpr (id == 5) return 1U;
      else return 0U;
    });
    scalar<Offset>(stream, N);
    ZuUnroll::all<N>([&stream](auto i) {
      constexpr unsigned from = ValuesAt + sizeof(Offset) * (i + 1);
      scalar<Offset>(stream, valueAt<i>() + EnumValTable::Data - from);
    });
    zero(stream, NodesAt - VecEnd);
    ZuUnroll::all<N>([&stream](auto i) { EnumValNode<U, i, G>::emit(stream); });
    if constexpr (Info::Flags) FlagsAttr::emit(stream);
  }
};

template <typename T, typename Facet = ZuFacet::Core>
struct Schema {
  using Root = ZuUnder<T>;
  using G = Graph<T, Facet>;
  using Objects = typename G::Objects;
  using Enums = typename G::Enums;
  enum { RootAt = 2 * sizeof(Offset), ObjectsAt = RootAt + RootTable::Size };
  enum { EnumsAt = ObjectsAt + sizeof(Offset) * (Objects::N + 1) };
  enum { VecEnd = EnumsAt + sizeof(Offset) * (Enums::N + 1) };
  enum { NodesAt = round(VecEnd, FieldTable::Align) };
  template <typename O>
  static consteval unsigned objectAt() {
    unsigned n = NodesAt;
    ZuUnroll::all<Objects>([&n]<typename P>() {
      if constexpr (ZuTypeIndex<P, Objects>{} < ZuTypeIndex<O, Objects>{})
	n += ObjectNode<P, G>::Size;
    });
    return n;
  }
  static consteval unsigned objectsEnd() {
    unsigned n = NodesAt;
    ZuUnroll::all<Objects>([&n]<typename O>() { n += ObjectNode<O, G>::Size; });
    return n;
  }
  template <typename U>
  static consteval unsigned enumAt() {
    unsigned n = objectsEnd();
    ZuUnroll::all<Enums>([&n]<typename V>() {
      if constexpr (ZuTypeIndex<V, Enums>{} < ZuTypeIndex<U, Enums>{})
	n += EnumNode<V, G>::Size;
    });
    return n;
  }
  static consteval unsigned size() {
    unsigned n = objectsEnd();
    ZuUnroll::all<Enums>([&n]<typename U>() { n += EnumNode<U, G>::Size; });
    return n;
  }
  enum { Size = size() };
  ZuAssert(Type<Root>::Kind == Kind::Table);
  ZuAssert(Size < Wire::MaxSize);
  template <typename S>
  static void emit(S &stream) {
    scalar<Offset>(stream, RootAt + RootTable::Data);
    stream << ZuCSpan{"BFBS", Wire::Identifier};
    RootTable::emit(stream, []<typename F>() {
      constexpr unsigned from = RootAt + RootTable::Data + RootTable::template offset<F>();
      constexpr unsigned id = Meta<R::Schema>::template id<F>();
      if constexpr (id == 0) return ObjectsAt - from;
      else if constexpr (id == 1) return EnumsAt - from;
      else if constexpr (id == 4) return objectAt<Root>() + ObjectNode<Root, G>::Data - from;
      else return uint64_t(G::arrays()); // AdvancedArrayFeatures
    });
    scalar<Offset>(stream, Objects::N);
    ZuUnroll::all<Objects>([&stream]<typename O>() {
      constexpr unsigned from = ObjectsAt + sizeof(Offset) * (1 + ZuTypeIndex<O, Objects>{});
      scalar<Offset>(stream, objectAt<O>() + ObjectNode<O, G>::Data - from);
    });
    scalar<Offset>(stream, Enums::N);
    ZuUnroll::all<Enums>([&stream]<typename U>() {
      constexpr unsigned from = EnumsAt + sizeof(Offset) * (1 + ZuTypeIndex<U, Enums>{});
      scalar<Offset>(stream, enumAt<U>() + EnumNode<U, G>::Data - from);
    });
    zero(stream, NodesAt - VecEnd);
    ZuUnroll::all<Objects>([&stream]<typename O>() { ObjectNode<O, G>::emit(stream); });
    ZuUnroll::all<Enums>([&stream]<typename U>() { EnumNode<U, G>::emit(stream); });
  }
};

} // ZiFB::SchemaOut

namespace ZiFB {

template <typename O, typename Facet = ZuFacet::Core, typename S>
void saveSchema(S &stream) { SchemaOut::Schema<O, Facet>::emit(stream); }

} // ZiFB

// --- Application-instantiated textual schema export to generic streams

namespace ZiFB::FBS {

ZuDerive(Number, (ZtString<ZtStringHeapID<"ZiFB.FBS.Number">>));

// Canonical scalar spellings; indexing is compile-time only.
using Scalars = ZuStringTL<"", "ubyte", "bool", "byte", "ubyte",
  "short", "ushort", "int", "uint", "long", "ulong", "float", "double">;

template <typename V, typename S>
void type(S &stream) {
  if constexpr (V::Kind == ValueKind::Scalar)
    stream << ZuType<SchemaOut::baseType<V>(), Scalars>{}();
  else if constexpr (V::Kind == ValueKind::String)
    stream << "string";
  else if constexpr (V::Kind == ValueKind::Vector || V::Kind == ValueKind::Array) {
    stream << '[';
    type<typename V::Elem>(stream);
    if constexpr (V::Kind == ValueKind::Array) stream << ':' << ZuBox<unsigned>{V::Count};
    stream << ']';
  } else if constexpr (V::Kind == ValueKind::Table || V::Kind == ValueKind::Inline ||
      V::Kind == ValueKind::Union)
    stream << Type<typename V::Native>::name();
  else
    ZuAssert(V::Kind != ValueKind::Union);
}

template <typename F, typename S>
void deflt(S &stream) {
  using W = typename FieldWire<F>::WireType;
  constexpr W value = defaultValue<F>();
  if constexpr (FieldWire<F>::Code_ == ZfFieldTC::Bool) {
    if constexpr (value) stream << "true";
    else stream << "false";
  } else if constexpr (ZuTraits<W>::IsFloatingPoint) {
    if constexpr (ZuFP<W>::nan(value)) stream << "nan";
    else if constexpr (ZuFP<W>::inf(value)) stream << "inf";
    else if constexpr (ZuFP<W>::inf(-value)) stream << "-inf";
    else {
      // ZuFmt::FP limits decimal places to 19, which cannot preserve all
      // subnormals. Use ZtString's existing hexadecimal float formatter;
      // no private number formatter or parser is needed.
      auto text = ZtScratch(Number, ZuFmt::MaxWidth);
      text.sprintf("%a", value);
      stream << text;
    }
  } else stream << ZuBox<W>{value};
}

template <ZuString Name>
consteval unsigned qualifier() {
  unsigned last = 0;
  for (unsigned i = 0; i < Name.length(); ++i)
    if (Name[i] == '.') last = i;
  return last;
}

// BFBS objects are name-sorted; textual structs instead need their inline
// dependencies defined first. Tables may refer forward to other tables.
template <typename Pending, typename Seen = ZuTypeList<>> struct InlineOrder;
template <typename O, typename Tail, typename Seen, bool = ZuTypeIn<O, Seen>{}>
struct InlineOne : public InlineOrder<Tail, Seen> { };
template <typename O, typename Tail, typename Seen>
struct InlineOne<O, Tail, Seen, false> {
  using Children = ZuTypeGrep<IsInline, typename SchemaOut::Children<O>::T>;
  using Before = typename InlineOrder<Children, Seen>::T;
  using T = typename InlineOrder<Tail, typename Before::template Push<O>>::T;
};
template <typename O, typename ...Tail, typename Seen>
struct InlineOrder<ZuTypeList<O, Tail...>, Seen> :
  public InlineOne<O, ZuTypeList<Tail...>, Seen> { };
template <typename Seen>
struct InlineOrder<ZuTypeList<>, Seen> { using T = Seen; };

template <typename O> using IsTable = ZuBool<Type<O>::Kind == Kind::Table>;

template <typename O, typename S>
void union_(S &stream) {
  static constexpr auto Name = Type<O>::name();
  constexpr unsigned split = qualifier<Name>();
  ZuAssert(split && split + 1 < Name.length());
  stream << "namespace " << ZuCSpan{Name.data(), split} << ";\nunion "
    << ZuCSpan{Name.data() + split + 1, Name.length() - split - 1} << " {\n";
  using Alts = typename Type<O>::Types;
  ZuUnroll::all<Alts>([&stream]<typename A>() {
    using V = Value<typename A::T, ZfFieldTC::UDT>;
    stream << "  " << A::name() << ':' << Type<typename V::Native>::name()
      << " = " << ZuBox<unsigned>{ZuTypeIndex<A, Alts>{} + 1};
    if constexpr (ZuTypeIndex<A, Alts>{} + 1 < Alts::N) stream << ',';
    stream << '\n';
  });
  stream << "}\n\n";
}

template <typename O, typename S>
void enum_(S &stream) {
  static constexpr auto Name = Type<O>::name();
  constexpr unsigned split = qualifier<Name>();
  ZuAssert(split && split + 1 < Name.length());
  using Info = SchemaOut::EnumInfo<O>;
  stream << "namespace " << ZuCSpan{Name.data(), split} << ";\nenum "
    << ZuCSpan{Name.data() + split + 1, Name.length() - split - 1} << ':'
    << ZuType<Info::Base, Scalars>{}();
  if constexpr (Info::Flags) stream << " (bit_flags)";
  stream << " {\n";
  ZuUnroll::all<Info::N>([&stream](auto i) {
    stream << "  " << Info::template name<i>() << " = " << ZuBox<unsigned>{i};
    if constexpr (i + 1 < Info::N) stream << ',';
    stream << '\n';
  });
  stream << "}\n\n";
}

template <typename O, typename Facet, typename S>
void object(S &stream) {
  using M = Meta<O, Facet>;
  static constexpr auto Name = Type<O>::name();
  constexpr unsigned split = qualifier<Name>();
  ZuAssert(split && split + 1 < Name.length()); // explicit qualified names
  stream << "namespace " << ZuCSpan{Name.data(), split} << ";\n";
  if constexpr (Type<O>::Kind == Kind::Inline) stream << "struct ";
  else stream << "table ";
  stream << ZuCSpan{Name.data() + split + 1, Name.length() - split - 1};
  if constexpr (Type<O>::Kind == Kind::Inline) {
    if constexpr (Layout<O>::ForcedAlign)
      stream << " (force_align: " << ZuBox<unsigned>{Layout<O>::ForcedAlign} << ')';
  }
  stream << " {\n";
  ZuUnroll::all<typename M::Fields>([&stream]<typename F>() {
    using V = FieldWire<F>;
    stream << "  " << ZuFieldProp::FB::name<F>() << ':';
    if constexpr (SchemaOut::FieldEnum<F>::Present) {
      using E = typename SchemaOut::FieldEnum<F>::E;
      if constexpr (V::Kind == ValueKind::Vector || V::Kind == ValueKind::Array)
	stream << '[';
      stream << Type<E>::name();
      if constexpr (V::Kind == ValueKind::Array) stream << ':' << ZuBox<unsigned>{V::Count};
      if constexpr (V::Kind == ValueKind::Vector || V::Kind == ValueKind::Array)
	stream << ']';
    } else if constexpr (V::Code_ == ZfFieldTC::Bool) stream << "bool";
    else type<V>(stream);
    if constexpr (Type<O>::Kind == Kind::Table) {
      if constexpr (V::Kind == ValueKind::Scalar) { stream << " = "; deflt<F>(stream); }
      stream << " (id: " << ZuBox<unsigned>{M::template id<F>()};
      if constexpr (!ZuFieldActive<F>{}) stream << ", deprecated";
      if constexpr (V::Required) stream << ", required";
      if constexpr (V::ForcedAlign)
	stream << ", force_align: " << ZuBox<unsigned>{V::ForcedAlign};
      stream << ')';
    }
    stream << ";\n";
  });
  stream << "}\n\n";
}

} // ZiFB::FBS

namespace ZiFB {

template <typename O, typename Facet = ZuFacet::Core, typename S>
void saveFBS(S &stream) {
  ZuAssert(Type<ZuUnder<O>>::Kind == Kind::Table);
  using G = SchemaOut::Graph<O, Facet>;
  using Inlines = typename FBS::InlineOrder<
    ZuTypeGrep<IsInline, typename G::Objects>>::T;
  using Tables = ZuTypeGrep<FBS::IsTable, typename G::Objects>;
  ZuUnroll::all<typename G::Enums>([&stream]<typename T>() {
    if constexpr (!IsUnion<T>{}) FBS::enum_<T>(stream);
  });
  ZuUnroll::all<Inlines>([&stream]<typename T>() { FBS::object<T, Facet>(stream); });
  ZuUnroll::all<typename G::Enums>([&stream]<typename T>() {
    if constexpr (IsUnion<T>{}) FBS::union_<T>(stream);
  });
  ZuUnroll::all<Tables>([&stream]<typename T>() { FBS::object<T, Facet>(stream); });
  stream << "root_type " << Type<ZuUnder<O>>::name() << ";\n";
}

} // ZiFB

#endif /* ZiFBReflect_HH */
