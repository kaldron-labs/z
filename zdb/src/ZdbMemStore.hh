//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zdb in-memory data store

#ifndef ZdbMemStore_HH
#define ZdbMemStore_HH

#ifndef ZdbLib_HH
#include <zlib/ZdbLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuJoin.hh>

#include <zlib/ZmHash.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZtCase.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiFB.hh>
#include <zlib/ZiFBReflect.hh>

#include <zlib/ZdbStore.hh>

namespace ZdbMem {

using namespace Zdb_;
namespace R = ZiFB::Reflect;

// --- value union

ZuDerive(String, ZtString<ZtStringHeapID<"ZdbMem.String">>);
ZuDerive(Bytes, (ZtArray<uint8_t, ZtArrayHeapID<"ZdbMem.Bytes">>));

struct VecHeapID : public ZuStringT<"ZdbMem.Vec"> { };
using ArrayVecHeapID = ZtArrayHeapID_<VecHeapID>;

ZuDerive(StringVec, (ZtArray<String, ArrayVecHeapID>));
ZuDerive(BytesVec, (ZtArray<Bytes, ArrayVecHeapID>));
ZuDerive(Int8Vec, (ZtArray<int8_t, ArrayVecHeapID>));
// UInt8Vec must be distinct from Bytes
ZuDerive(UInt8Vec, (ZtArray<uint8_t, ArrayVecHeapID>));
ZuDerive(Int16Vec, (ZtArray<int16_t, ArrayVecHeapID>));
ZuDerive(UInt16Vec, (ZtArray<uint16_t, ArrayVecHeapID>));
ZuDerive(Int32Vec, (ZtArray<int32_t, ArrayVecHeapID>));
ZuDerive(UInt32Vec, (ZtArray<uint32_t, ArrayVecHeapID>));
ZuDerive(Int64Vec, (ZtArray<int64_t, ArrayVecHeapID>));
ZuDerive(UInt64Vec, (ZtArray<uint64_t, ArrayVecHeapID>));
ZuDerive(Int128Vec, (ZtArray<int128_t, ArrayVecHeapID>));
ZuDerive(UInt128Vec, (ZtArray<uint128_t, ArrayVecHeapID>));
ZuDerive(FloatVec, (ZtArray<double, ArrayVecHeapID>));
ZuDerive(FixedVec, (ZtArray<ZuFixed, ArrayVecHeapID>));
ZuDerive(DecimalVec, (ZtArray<ZuDecimal, ArrayVecHeapID>));
ZuDerive(TimeVec, (ZtArray<ZuTime, ArrayVecHeapID>));
ZuDerive(DateTimeVec, (ZtArray<ZuDateTime, ArrayVecHeapID>));

using Value_ = ZuUnion<
  void,
  String,	// String
  Bytes,	// Vector<uint8_t>
  bool,
  int8_t,
  uint8_t,
  int16_t,
  uint16_t,
  int32_t,
  uint32_t,
  int64_t,
  uint64_t,
  double,
  ZuFixed,	// Zfb.Fixed
  ZuDecimal,	// Zfb.Decimal
  ZuTime,	// Zfb.Time
  ZuDateTime,	// Zfb.DateTime
  int128_t,	// Zfb.UInt128
  uint128_t,	// Zfb.Int128
  ZtBitmap,	// Zfb.Bitmap
  ZiIP,		// Zfb.IP

  // all types after this are vectors, see isVec() below
  StringVec,
  BytesVec,
  Int8Vec,
  UInt8Vec,
  Int16Vec,
  UInt16Vec,
  Int32Vec,
  UInt32Vec,
  Int64Vec,
  UInt64Vec,
  Int128Vec,
  UInt128Vec,
  FloatVec,
  FixedVec,
  DecimalVec,
  TimeVec,
  DateTimeVec>;

enum { VecBase = Value_::Index<StringVec>{} };

constexpr bool isVec(unsigned i) { return i >= VecBase; }

struct Value : public Value_ {
  ZuDerive_(Value, Value_)

  template <unsigned I, typename S,
    typename = ZuIfT<
      (I == Value_::Index<void>{}) ||
      (I == Value_::Index<String>{}) ||
      (I == Value_::Index<Bytes>{}) ||
      (I == Value_::Index<bool>{} || I == Value_::Index<int8_t>{} ||
	I == Value_::Index<uint8_t>{} || I == Value_::Index<int16_t>{} ||
	I == Value_::Index<uint16_t>{} || I == Value_::Index<int32_t>{} ||
	I == Value_::Index<uint32_t>{} || I == Value_::Index<int64_t>{} ||
	I == Value_::Index<uint64_t>{} || I == Value_::Index<int128_t>{} ||
	I == Value_::Index<uint128_t>{} || I == Value_::Index<double>{}) ||
      (I == Value_::Index<ZuFixed>{} || I == Value_::Index<ZuDecimal>{} ||
	I == Value_::Index<ZuTime>{} || I == Value_::Index<ZtBitmap>{} ||
	I == Value_::Index<ZiIP>{}) ||
      (I == Value_::Index<ZuDateTime>{}) ||
      (I == Value_::Index<StringVec>{}) ||
      (I == Value_::Index<BytesVec>{}) ||
      (I == Value_::Index<Int8Vec>{} || I == Value_::Index<UInt8Vec>{} ||
	I == Value_::Index<Int16Vec>{} || I == Value_::Index<UInt16Vec>{} ||
	I == Value_::Index<Int32Vec>{} || I == Value_::Index<UInt32Vec>{} ||
	I == Value_::Index<Int64Vec>{} || I == Value_::Index<UInt64Vec>{} ||
	I == Value_::Index<Int128Vec>{} || I == Value_::Index<UInt128Vec>{} ||
	I == Value_::Index<FloatVec>{}) ||
      (I == Value_::Index<FixedVec>{} || I == Value_::Index<DecimalVec>{} ||
	I == Value_::Index<TimeVec>{}) ||
      (I == Value_::Index<DateTimeVec>{})>>
  void
  print_(S &s) const {
    if constexpr (I == Value_::Index<void>{}) {
    } else if constexpr (I == Value_::Index<String>{}) {
      s << ZfStruct_::Print::String{p<I>()};
    } else if constexpr (I == Value_::Index<Bytes>{}) {
      s << ZfStruct_::Print::Bytes{p<I>()};
    } else if constexpr (I == Value_::Index<bool>{} || I == Value_::Index<int8_t>{} ||
      I == Value_::Index<uint8_t>{} || I == Value_::Index<int16_t>{} ||
      I == Value_::Index<uint16_t>{} || I == Value_::Index<int32_t>{} ||
      I == Value_::Index<uint32_t>{} || I == Value_::Index<int64_t>{} ||
      I == Value_::Index<uint64_t>{} || I == Value_::Index<int128_t>{} ||
      I == Value_::Index<uint128_t>{} || I == Value_::Index<double>{}) {
      s << ZuBoxed(p<I>());
    } else if constexpr (I == Value_::Index<ZuFixed>{} ||
      I == Value_::Index<ZuDecimal>{} || I == Value_::Index<ZuTime>{} ||
      I == Value_::Index<ZtBitmap>{} || I == Value_::Index<ZiIP>{}) {
      s << p<I>();
    } else if constexpr (I == Value_::Index<ZuDateTime>{}) {
      auto &fmt = ZmTLS<ZuDateTimeFmt::CSV, (int Value_::*){}>();
      s << p<I>().fmt(fmt);
    } else if constexpr (I == Value_::Index<StringVec>{}) {
      s << '[';
      bool first = true;
      p<I>().all([&s, &first](const String &v) {
	if (!first) s << ','; else first = false;
	s << ZfStruct_::Print::String{v};
      });
      s << ']';
    } else if constexpr (I == Value_::Index<BytesVec>{}) {
      s << '[';
      bool first = true;
      p<I>().all([&s, &first](const Bytes &v) {
	if (!first) s << ','; else first = false;
	s << ZfStruct_::Print::Bytes{v};
      });
      s << ']';
    } else if constexpr (I == Value_::Index<Int8Vec>{} ||
      I == Value_::Index<UInt8Vec>{} || I == Value_::Index<Int16Vec>{} ||
      I == Value_::Index<UInt16Vec>{} || I == Value_::Index<Int32Vec>{} ||
      I == Value_::Index<UInt32Vec>{} || I == Value_::Index<Int64Vec>{} ||
      I == Value_::Index<UInt64Vec>{} || I == Value_::Index<Int128Vec>{} ||
      I == Value_::Index<UInt128Vec>{} || I == Value_::Index<FloatVec>{}) {
      using Elem = typename ZuTraits<Type<I>>::Elem;
      s << '[';
      bool first = true;
      p<I>().all([&s, &first](const Elem &v) {
	if (!first) s << ','; else first = false;
	s << ZuBoxed(v);
      });
      s << ']';
    } else if constexpr (I == Value_::Index<FixedVec>{} ||
      I == Value_::Index<DecimalVec>{} || I == Value_::Index<TimeVec>{}) {
      using Elem = typename ZuTraits<Type<I>>::Elem;
      s << '[';
      bool first = true;
      p<I>().all([&s, &first](const Elem &v) {
	if (!first) s << ','; else first = false;
	s << v;
      });
      s << ']';
    } else {
      s << '[';
      bool first = true;
      p<I>().all([&s, &first](const ZuDateTime &v) {
	auto &fmt = ZmTLS<ZuDateTimeFmt::CSV, (int Value_::*){}>();
	if (!first) s << ','; else first = false;
	s << v.fmt(fmt);
      });
      s << ']';
    }
  }

  template <typename S>
  void print(S &s) const {
    ZuSwitch::dispatch<Value_::N>(this->type(), [this, &s](auto I) {
      this->print_<I>(s);
    });
  }
  friend ZuPrintFn ZuPrintType(Value *);
};

// --- extended field information

struct XField {
  R::FieldView		field;
  unsigned			type;	// Value union discriminator
  unsigned			align = 1;	// resolved BFBS force_align
  unsigned			bufAlign = ZiFB::Wire::Align;
  unsigned			index = UINT_MAX;	// key component's row Tuple index
  const ZfVField		*native = nullptr;
};
struct XFields : public ZtArray<XField, ZtArrayHeapID<"ZdbMem.XField">> {
  unsigned bufAlign = ZiFB::Wire::Align;

  ZuDerive_(XFields, (ZtArray<XField, ZtArrayHeapID<"ZdbMem.XField">>))
};
ZuDerive(XKeyFields, (ZtArray<XFields, ZtArrayHeapID<"ZdbMem.XKeyField">>));

// resolve Value union discriminator from flatbuffers reflection data
XField xField(
  R::FieldView fbField, const ZfVField *field,
  R::SchemaView schema)
{
  // resolve flatbuffers reflection data for field
  if (!fbField) return {{}, 0, 1, ZiFB::Wire::Align, UINT_MAX, field};
  unsigned type = 0;
  auto ftype = field->type;
  unsigned code = ftype->code;
  switch (R::type(fbField).get<R::ZfField(Type, baseType)>()) {
    case R::BaseType::String:
      if (code == ZfFieldTC::CString ||
	  code == ZfFieldTC::String)
	type = Value::Index<String>{};
      break;
    case R::BaseType::Bool:
      if (code == ZfFieldTC::Bool)
	type = Value::Index<bool>{};
      break;
    case R::BaseType::Byte:
      if (code == ZfFieldTC::Int8)
	type = Value::Index<int8_t>{};
      break;
    case R::BaseType::UByte:
      if (code == ZfFieldTC::UInt8)
	type = Value::Index<uint8_t>{};
      break;
    case R::BaseType::Short:
      if (code == ZfFieldTC::Int16)
	type = Value::Index<int16_t>{};
      break;
    case R::BaseType::UShort:
      if (code == ZfFieldTC::UInt16)
	type = Value::Index<uint16_t>{};
      break;
    case R::BaseType::Int:
      if (code == ZfFieldTC::Int32)
	type = Value::Index<int32_t>{};
      break;
    case R::BaseType::UInt:
      if (code == ZfFieldTC::UInt32)
	type = Value::Index<uint32_t>{};
      break;
    case R::BaseType::Long:
      if (code == ZfFieldTC::Int64)
	type = Value::Index<int64_t>{};
      break;
    case R::BaseType::ULong:
      if (code == ZfFieldTC::UInt64)
	type = Value::Index<uint64_t>{};
      break;
    case R::BaseType::Float:
    case R::BaseType::Double:
      if (code == ZfFieldTC::Float)
	type = Value::Index<double>{};
      break;
    case R::BaseType::Obj: {
      switch (code) {
	case ZfFieldTC::Int128:
	  type = Value::Index<int128_t>{};
	  break;
	case ZfFieldTC::UInt128:
	  type = Value::Index<uint128_t>{};
	  break;
	case ZfFieldTC::Fixed:
	  type = Value::Index<ZuFixed>{};
	  break;
	case ZfFieldTC::Decimal:
	  type = Value::Index<ZuDecimal>{};
	  break;
	case ZfFieldTC::Time:
	  type = Value::Index<ZuTime>{};
	  break;
	case ZfFieldTC::DateTime:
	  type = Value::Index<ZuDateTime>{};
	  break;
	case ZfFieldTC::UDT:
	  if (ftype->info.udt()->id == "Bitmap") type = Value::Index<ZtBitmap>{};
	  break;
      }
    } break;
    case R::BaseType::Union:
      if (code == ZfFieldTC::UDT &&
	  ftype->info.udt()->id == "IP")
	type = Value::Index<ZiIP>{};
      break;
    case R::BaseType::Vector:
      switch (R::type(fbField).get<R::ZfField(Type, element)>()) {
	default: break;
	case R::BaseType::String:
	  if (code == ZfFieldTC::CStringVec ||
	      code == ZfFieldTC::StringVec)
	    type = Value::Index<StringVec>{};
	  break;
	case R::BaseType::Byte:
	  if (code == ZfFieldTC::Int8Vec)
	    type = Value::Index<Int8Vec>{};
	  break;
	case R::BaseType::UByte:
	  if (code == ZfFieldTC::Bytes)
	    type = Value::Index<Bytes>{};
	  else if (code == ZfFieldTC::UInt8Vec)
	    type = Value::Index<UInt8Vec>{};
	  break;
	case R::BaseType::Short:
	  if (code == ZfFieldTC::Int16Vec)
	    type = Value::Index<Int16Vec>{};
	  break;
	case R::BaseType::UShort:
	  if (code == ZfFieldTC::UInt16Vec)
	    type = Value::Index<UInt16Vec>{};
	  break;
	case R::BaseType::Int:
	  if (code == ZfFieldTC::Int32Vec)
	    type = Value::Index<Int32Vec>{};
	  break;
	case R::BaseType::UInt:
	  if (code == ZfFieldTC::UInt32Vec)
	    type = Value::Index<UInt32Vec>{};
	  break;
	case R::BaseType::Long:
	  if (code == ZfFieldTC::Int64Vec)
	    type = Value::Index<Int64Vec>{};
	  break;
	case R::BaseType::ULong:
	  if (code == ZfFieldTC::UInt64Vec)
	    type = Value::Index<UInt64Vec>{};
	  break;
	case R::BaseType::Float:
	case R::BaseType::Double:
	  if (code == ZfFieldTC::FloatVec)
	    type = Value::Index<FloatVec>{};
	  break;
	case R::BaseType::Obj:
	  switch (code) {
	    case ZfFieldTC::BytesVec:
	      type = Value::Index<BytesVec>{};
	      break;
	    case ZfFieldTC::Int128Vec:
	      type = Value::Index<Int128Vec>{};
	      break;
	    case ZfFieldTC::UInt128Vec:
	      type = Value::Index<UInt128Vec>{};
	      break;
	    case ZfFieldTC::FixedVec:
	      type = Value::Index<FixedVec>{};
	      break;
	    case ZfFieldTC::DecimalVec:
	      type = Value::Index<DecimalVec>{};
	      break;
	    case ZfFieldTC::TimeVec:
	      type = Value::Index<TimeVec>{};
	      break;
	    case ZfFieldTC::DateTimeVec:
	      type = Value::Index<DateTimeVec>{};
	      break;
	  }
	  break;
      }
      break;
    default:
      break;
  }
  return {fbField, type, R::alignment(fbField), R::bufferAlign(schema, fbField),
    UINT_MAX, field};
}

// Supported column values share the codec's native value representations.
template <unsigned I>
consteval unsigned valueCode() {
  if constexpr (I == Value::Index<String>{}) return ZfFieldTC::String;
  if constexpr (I == Value::Index<Bytes>{}) return ZfFieldTC::Bytes;
  if constexpr (I == Value::Index<bool>{}) return ZfFieldTC::Bool;
  if constexpr (I == Value::Index<int8_t>{}) return ZfFieldTC::Int8;
  if constexpr (I == Value::Index<uint8_t>{}) return ZfFieldTC::UInt8;
  if constexpr (I == Value::Index<int16_t>{}) return ZfFieldTC::Int16;
  if constexpr (I == Value::Index<uint16_t>{}) return ZfFieldTC::UInt16;
  if constexpr (I == Value::Index<int32_t>{}) return ZfFieldTC::Int32;
  if constexpr (I == Value::Index<uint32_t>{}) return ZfFieldTC::UInt32;
  if constexpr (I == Value::Index<int64_t>{}) return ZfFieldTC::Int64;
  if constexpr (I == Value::Index<uint64_t>{}) return ZfFieldTC::UInt64;
  if constexpr (I == Value::Index<int128_t>{}) return ZfFieldTC::Int128;
  if constexpr (I == Value::Index<uint128_t>{}) return ZfFieldTC::UInt128;
  if constexpr (I == Value::Index<double>{}) return ZfFieldTC::Float;
  if constexpr (I == Value::Index<ZuFixed>{}) return ZfFieldTC::Fixed;
  if constexpr (I == Value::Index<ZuDecimal>{}) return ZfFieldTC::Decimal;
  if constexpr (I == Value::Index<ZuTime>{}) return ZfFieldTC::Time;
  if constexpr (I == Value::Index<ZuDateTime>{}) return ZfFieldTC::DateTime;
  if constexpr (I == Value::Index<ZtBitmap>{}) return ZfFieldTC::UDT;
  if constexpr (I == Value::Index<ZiIP>{}) return ZfFieldTC::UDT;
  if constexpr (I == Value::Index<StringVec>{}) return ZfFieldTC::StringVec;
  if constexpr (I == Value::Index<BytesVec>{}) return ZfFieldTC::BytesVec;
  if constexpr (I == Value::Index<Int8Vec>{}) return ZfFieldTC::Int8Vec;
  if constexpr (I == Value::Index<UInt8Vec>{}) return ZfFieldTC::UInt8Vec;
  if constexpr (I == Value::Index<Int16Vec>{}) return ZfFieldTC::Int16Vec;
  if constexpr (I == Value::Index<UInt16Vec>{}) return ZfFieldTC::UInt16Vec;
  if constexpr (I == Value::Index<Int32Vec>{}) return ZfFieldTC::Int32Vec;
  if constexpr (I == Value::Index<UInt32Vec>{}) return ZfFieldTC::UInt32Vec;
  if constexpr (I == Value::Index<Int64Vec>{}) return ZfFieldTC::Int64Vec;
  if constexpr (I == Value::Index<UInt64Vec>{}) return ZfFieldTC::UInt64Vec;
  if constexpr (I == Value::Index<Int128Vec>{}) return ZfFieldTC::Int128Vec;
  if constexpr (I == Value::Index<UInt128Vec>{}) return ZfFieldTC::UInt128Vec;
  if constexpr (I == Value::Index<FloatVec>{}) return ZfFieldTC::FloatVec;
  if constexpr (I == Value::Index<FixedVec>{}) return ZfFieldTC::FixedVec;
  if constexpr (I == Value::Index<DecimalVec>{}) return ZfFieldTC::DecimalVec;
  if constexpr (I == Value::Index<TimeVec>{}) return ZfFieldTC::TimeVec;
  if constexpr (I == Value::Index<DateTimeVec>{}) return ZfFieldTC::DateTimeVec;
  return ZfFieldTC::UDT; // void is excluded by the callers
}
template <unsigned I>
using WireValue = ZiFB::Value<typename Value::template Type<I>, valueCode<I>()>;

// --- load value from a trusted flatbuffer

template <unsigned I>
void loadAt(
  void *ptr, const ZfVField *native, R::FieldView field,
  const uint8_t *p, unsigned tag = 0)
{
  if constexpr (I) {
    using T = typename Value::template Type<I>;
    using W = WireValue<I>;
    if constexpr (I == Value::Index<double>{}) {
      if (R::type(field).get<R::ZfField(Type, baseType)>() == R::BaseType::Float) {
	new (ptr) T((p ? ZiFB::read<float>(p) : R::deflt<float>(field)));
	return;
      }
    } else if constexpr (I == Value::Index<FloatVec>{}) {
      if (R::type(field).get<R::ZfField(Type, element)>() == R::BaseType::Float) {
	auto vector = ZiFB::VectorValue<ZiFB::Scalar<float>>{p ? ZiFB::follow(p) : nullptr};
	auto array = new (ptr) T(vector.length());
	for (auto v: vector) array->push(v);
	return;
      }
    }
    if constexpr (W::Kind == ZiFB::ValueKind::Scalar) {
      new (ptr) T((p ? ZiFB::read<typename W::WireType>(p) : R::deflt<typename W::WireType>(field)));
    } else if constexpr (W::Kind == ZiFB::ValueKind::Union) {
      new (ptr) T(W::fromWire(ZiFB::UnionView<typename W::Native>{
	tag, p ? ZiFB::follow(p) : nullptr}));
    } else {
      if constexpr (W::Kind == ZiFB::ValueKind::Inline) {
	if (!p) {
	  new (ptr) T(native->constant.template get<valueCode<I>()>(
	    ZfVField::cget(ZfVFieldConstant::Deflt)));
	  return;
	}
      } else if (p) p = ZiFB::follow(p);
      if constexpr (isVec(I)) {
	auto value = ZiFB::valueAt<W>(p);
	auto array = new (ptr) T(value.length());
	for (auto v: value) array->push(v);
      } else
	new (ptr) T(ZiFB::valueAt<W>(p));
    }
  }
}

// --- save value using BFBS defaults, IDs and the shared native mappings

using Offset = ZiFB::Offset;

template <unsigned I, typename B, typename A>
Offset saveOffset(B &fbb, const XField &field, const A &value) {
  if constexpr (I) {
    using W = WireValue<I>;
    auto align = field.align;
    if constexpr (I == Value::Index<FloatVec>{}) {
      if (R::type(field.field).get<R::ZfField(Type, element)>() == R::BaseType::Float)
	return ZiFB::saveValue<ZiFB::Vector<ZiFB::Scalar<float>>,
	  ZuFacet::FB, ZfFieldFilter::All>(fbb, value.template p<I>(),
	    align > sizeof(float) ? align : sizeof(float));
    }
    if constexpr (W::Kind == ZiFB::ValueKind::Union)
      return ZiFB::save<ZuFacet::FB, ZfFieldFilter::All>(
	fbb, value.template p<I>()).offset;
    else if constexpr (ZiFB::IsOffset<W>) {
      return ZiFB::saveValue<W, ZuFacet::FB, ZfFieldFilter::All>(
	fbb, value.template p<I>(), (align > ZiFB::valueAlign<W>() ? align : ZiFB::valueAlign<W>()));
    }
  }
  return 0;
}

template <unsigned I, typename B, typename A>
void saveValue(B &fbb, ZiFB::TableState &table, Offset offset,
    R::FieldView field, unsigned align, const A &value) {
  if constexpr (I) {
    using W = WireValue<I>;
    if constexpr (I == Value::Index<double>{}) {
      if (R::type(field).get<R::ZfField(Type, baseType)>() == R::BaseType::Float) {
	R::saveScalar<float>(fbb, table, field, float(value.template p<I>()));
	return;
      }
    }
    if constexpr (W::Kind == ZiFB::ValueKind::Scalar)
      R::saveScalar<typename W::WireType>(fbb, table, field, W::toWire(value.template p<I>()));
    else if constexpr (W::Kind == ZiFB::ValueKind::Inline) {
      fbb.fieldInline(table, field.get<R::ZfField(Field, id)>(),
	W::Size, (align > W::Align ? align : W::Align), [&value](uint8_t *p) {
	  ZiFB::save(p, W::toWire(value.template p<I>()));
	});
    } else if constexpr (W::Kind == ZiFB::ValueKind::Union)
      R::saveUnion(fbb, table, field, ZiFB::As<typename W::Native>::tag(value.template p<I>()), offset);
    else
      R::saveOffset(fbb, table, field, offset);
  }
}

void loadValue(Value &value, const XField &field, ZiFB::TableView table) {
  auto p = R::address(table, field.field);
  ZuSwitch::dispatch<Value::N>(field.type, [&value, &field, table, p](auto I) {
    unsigned tag = 0;
    if constexpr (unsigned(I) == unsigned(Value::Index<ZiIP>{}))
      tag = R::tag(table, field.field);
    loadAt<I>(value.new_<I, true>(), field.native, field.field, p, tag);
  });
}

// --- data tuple

ZuDerive(Tuple, (ZtArray<Value, ZtArrayHeapID<"ZdbMem.Tuple">>));

// loadTuple() and saveTuple() rely on tuples being a full row
// of values, i.e. tuples.length() == fields.length() == xFields.length()
// (individual elements of the tuple can be null values)

// load tuple from flatbuffer
// - when called from select(), nParams is < fields.length()
template <typename Filter>
Tuple loadTuple_(
  unsigned nParams,
  const ZfVFieldArray &fields,
  const XFields &xFields,
  ZiFB::TableView fbo,
  Filter filter)
{
  Tuple tuple(nParams); // not {}
  for (unsigned i = 0; i < nParams; i++)
    if (filter(fields[i])) {
      auto value = static_cast<Value *>(tuple.push());
      loadValue(*value, xFields[i], fbo);
    } else
      new (tuple.push()) Value{};
  return tuple;
}
Tuple loadTuple_(
  unsigned nParams,
  const ZfVFieldArray &fields,
  const XFields &xFields,
  ZiFB::TableView fbo)
{
  return loadTuple_(nParams, fields, xFields, fbo,
    [](const ZfVField *) { return true; });
}
Tuple loadTuple(
  const ZfVFieldArray &fields, const XFields &xFields, ZiFB::TableView fbo)
{
  return loadTuple_(fields.length(), fields, xFields, fbo);
}
Tuple loadUpdTuple(
  const ZfVFieldArray &fields, const XFields &xFields, ZiFB::TableView fbo)
{
  return loadTuple_(fields.length(), fields, xFields, fbo,
    [](const ZfVField *field) -> bool {
      return bool(field->props & ZfVFieldProp::Mutable()) || (field->keys & 1);
    });
}
Tuple loadDelTuple(
  const ZfVFieldArray &fields, const XFields &xFields, ZiFB::TableView fbo)
{
  return loadTuple_(fields.length(), fields, xFields, fbo,
    [](const ZfVField *field) -> bool { return (field->keys & 1); });
}

// save tuple to flatbuffer
template <bool Key = false, typename B>
Offset saveTuple(B &fbb, const XFields &xFields, ZuSpan<const Value> tuple) {
  unsigned n = xFields.length();
  if constexpr (!Key) ZmAssert(tuple.length() == n);
  auto offsets = ZtScratch(ZiFB::Offsets, n, n);
  for (unsigned i = 0; i < n; ++i) {
    const auto &value = tuple[Key ? xFields[i].index : i];
    offsets[i] = ZuSwitch::dispatch<Value::N>(value.type(),
      [&fbb, &field = xFields[i], &value](auto I) {
	return saveOffset<I>(fbb, field, value);
      });
  }
  auto table = fbb.startTable();
  for (unsigned i = 0; i < n; ++i) {
    const auto &value = tuple[Key ? xFields[i].index : i];
    ZuSwitch::dispatch<Value::N>(value.type(),
      [&fbb, &table, offset = offsets[i], &field = xFields[i], &value](auto I) {
	saveValue<I>(fbb, table, offset, field.field, field.align, value);
      });
  }
  return fbb.endTable(table);
}

// update tuple
void updTuple(const ZfVFieldArray &fields, Tuple &data, Tuple &&update) {
  ZmAssert(fields.length() == data.length());
  ZmAssert(data.length() == update.length());
  unsigned n = data.length();
  for (unsigned i = 0; i < n; i++)
    if (fields[i]->props & ZfVFieldProp::Mutable()) {
      ZmAssert(update[i].type());
      data[i] = ZuMv(update[i]);
    }
}

// extract key from tuple
Tuple extractKey(const XFields &fields, const Tuple &data) {
  Tuple key(fields.length()); // not {}
  for (const auto &field : fields) {
    ZmAssert(field.index < data.length());
    key.push(data[field.index]);
  }
  ZmAssert(key.length() == key.size());
  return key;
}

// --- in-memory row

struct MemRow__ {
  Shard		shard;
  UN		un;
  SN		sn;
  VN		vn;
  Tuple		data;

  static ZuTuple<unsigned, ZdbUN> UNAxor(const MemRow__ &row) {
    return {row.shard, row.un};
  }
};

// UN index
struct MemRow_ : public ZuObject, public MemRow__ {
  using MemRow__::MemRow__;
  template <typename ...Args>
  MemRow_(Args &&...args) : MemRow__{ZuFwd<Args>(args)...} { }
};
ZmRBTreeDerive(IndexUN, MemRow_,
  ZmRBTreeNode<MemRow_,
    ZmRBTreeKey<MemRow_::UNAxor,
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"MemRow">>>>);
struct MemRow : public IndexUN::Node {
  using Base = IndexUN::Node;
  using Base::Base;
  using MemRow__::data;
};

// key indices
// - override the default comparator to provide in-memory indices
//   that mimic a RDBMS B-Tree ascending/descending indices
inline bool equals_(const Tuple &l, const Tuple &r, unsigned n) {
  for (unsigned i = 0; i < n; i++)
    if (!l[i].equals(r[i])) return false;
  return true;
}
template <typename T = Tuple> struct TupleCmp {
  uint64_t	descending = 0;

  int cmp(const T &l, const T &r) const {
    unsigned ln = l.length();
    unsigned rn = r.length();
    unsigned i, n = ln < rn ? ln : rn;
    for (i = 0; i < n; i++) {
      if (int j = l[i].cmp(r[i])) {
	if (descending & (uint64_t(1)<<i)) j = -j;
	return j;
      }
    }
    return ZuCompare(ln, rn);
  }
  static bool equals(const T &l, const T &r) {
    unsigned ln = l.length();
    unsigned rn = r.length();
    return equals_(l, r, ln < rn ? ln : rn);
  }
};
// A row belongs to every secondary index; each index retains a shared row reference.
ZmRBTreeKVDerive(Index, Tuple, ZmRef<const MemRow>,
  ZmRBTreeCmp<TupleCmp,
    ZmRBTreeUnique<false,
      ZmRBTreeHeapID<"MemRowIndex">>>);
// Key 0 uniqueness is enforced by insert(); secondary keys may repeat.

// --- in-memory data store base class

class Store__ {
public:
  void init(ZiMultiplex *mx, unsigned sid) {
    m_mx = mx;
    m_sid = sid;
  }

  template <typename ...Args> void run(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_sid);
  }
  template <typename ...Args> void invoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_sid);
  }

private:
  ZiMultiplex		*m_mx = nullptr;
  unsigned		m_sid = ZuCmp<unsigned>::null();
};

// --- in-memory data store table

class ZdbAPI StoreTbl : public Zdb_::StoreTbl {
public:
  using Store = Store__;

  StoreTbl(
    Store *store, bool internal, IDString id, unsigned nShards,
    ZfVFieldArray fields, ZfVKeyFieldArray keyFields,
    Schema schema, IOBufAllocFn bufAllocFn)
  :
    m_store{store}, m_id{ZuMv(id)}, m_internal{internal},
    m_fields{ZuMv(fields)}, m_keyFields{ZuMv(keyFields)},
    m_bufAllocFn{ZuMv(bufAllocFn)}
  {
    // introspect fields and flatbuffers reflection data, building
    // m_xFields[], m_keyGroup[] and m_xKeyFields[]
    for (unsigned i = 0, n = m_keyFields.length(); i < n; i++) {
      uint64_t descending = 0;
      unsigned m = m_keyFields[i].length();
      ZmAssert(m < 64);
      for (unsigned j = 0; j < m; j++)
	if (m_keyFields[i][j]->descend & (uint64_t(1)<<i))
	  descending |= (uint64_t(1)<<j);
      new (m_indices.push()) Index{TupleCmp<>{descending}};
    }
    unsigned n = m_keyFields.length();
    m_keyGroup.length(n);
    for (unsigned i = 0; i < n; i++) {
      unsigned m = m_keyFields[i].length();
      m_keyGroup[i] = 0;
      for (unsigned j = 0; j < m; j++) {
	if (m_keyFields[i][j]->group & (uint64_t(1)<<i))
	  m_keyGroup[i] = j + 1;
      }
    }
    this->schema(schema);
    m_maxUN.length(nShards);
    for (unsigned i = 0; i < nShards; i++) m_maxUN[i] = ZdbNullUN();
  }

  Store *store() const { return m_store; }
  const auto &id() const { return m_id; }
  bool internal() const { return m_internal; }

  bool opened() const { return m_opened; }

  auto count() const { return m_indices[0].count_(); }
  unsigned nShards() const { return m_maxUN.length(); }
  const auto &maxUN() const { return m_maxUN; }
  auto maxUN(Shard shard) const { return m_maxUN[shard]; }
  auto maxSN() const { return m_maxSN; }

protected:
  ~StoreTbl() = default;

private:
  // load a row from the caller's trusted replication/recovery record
  ZmRef<const MemRow> loadRow(Msg::RecordView record, Shard shard, UN un) {
    auto sn = record.get<Msg::ZfField(Record, sn)>();
    auto vn = record.get<Msg::ZfField(Record, vn)>();
    auto data = record.get<Msg::ZfField(Record, data)>();
    ZiFB::TableView fbo{ZiFB::root(data)};
    Tuple tuple;
    if (!vn)
      tuple = loadTuple(m_fields, m_xFields, fbo);
    else if (vn > 0)
      tuple = loadUpdTuple(m_fields, m_xFields, fbo);
    else
      tuple = loadDelTuple(m_fields, m_xFields, fbo);
    return new MemRow{shard, un, sn, vn, ZuMv(tuple)};
  }

  // save a row to a buffer as a replication/recovery message
  template <bool Recovery>
  ZmRef<IOBuf> saveRow(const ZmRef<const MemRow> &row) {
    constexpr unsigned msgAlign = ZiFB::BufferAlign<Msg::Root>::N;
    ZiFB::IOBuilder<ZiFB::Wire::MaxAlign> fbb{
      m_bufAllocFn(), m_rowAlign > msgAlign ? m_rowAlign : msgAlign};
    auto nested = fbb.startNested();
    auto root = saveTuple(fbb, m_xFields, row->data);
    auto data = fbb.endNested(nested, root);
    auto record = saveRecord(fbb, this->id(), row->un, row->sn, row->vn, row->shard, data);
    fbb.finish(saveMsg(fbb, record, Recovery ? Msg::Tag::Recovery : Msg::Tag::Replication));
    return saveHdr(fbb);
  }

public:
  // Reopen may supply a new Table owner (notably the saga catalog).
  // Rebind only dormant views, before any operation can consume them.
  void schema(Schema schema) {
    auto bind = [schema](const ZfVField *native) {
      return xField(schema.field(native->id), native, schema.view);
    };
    XFields fields{m_fields.length()};
    XKeyFields keys(m_keyFields.length()); // capacity, not an initializer list
    unsigned align = ZiFB::Wire::Align;
    for (unsigned i = 0, n = m_fields.length(); i < n; ++i)
      fields.push(bind(m_fields[i]));
    for (const auto &field : fields)
      if (align < field.bufAlign) align = field.bufAlign;
    for (unsigned i = 0, n = m_keyFields.length(); i < n; ++i) {
      auto key = new (keys.push()) XFields{m_keyFields[i].length()};
      for (unsigned j = 0, m = m_keyFields[i].length(); j < m; ++j) {
	auto field = bind(m_keyFields[i][j]);
	// Resolve row components once; key order may differ from declaration
	// order when group fields follow member fields.
	for (unsigned k = 0, n = fields.length(); k < n; ++k)
	  if (fields[k].field.data() == field.field.data()) { field.index = k; break; }
	ZmAssert(field.index != UINT_MAX);
	if (key->bufAlign < field.bufAlign) key->bufAlign = field.bufAlign;
	key->push(ZuMv(field));
      }
    }
    m_xFields = ZuMv(fields);
    m_xKeyFields = ZuMv(keys);
    m_rowAlign = align;
  }
  void open() { m_opened = true; }
  void close(CloseFn fn) {
    m_store->run([this, fn = ZuMv(fn)]() mutable {
      m_opened = false;
      fn();
    });
  }

  void warmup() { }

  void count(KeyID keyID, ZmRef<IOBuf>, CountFn);

  void select(
    bool selectRow, bool selectNext, bool inclusive,
    KeyID keyID, ZmRef<IOBuf>,
    unsigned limit, TupleFn);

  void find(KeyID keyID, ZmRef<IOBuf>, RowFn);

  void recover(Shard shard, UN, RowFn);

  void write(ZmRef<IOBuf>, CommitFn);

private:
  void insert(ZmRef<MemRow>, ZmRef<IOBuf>, CommitFn);
  void update(ZmRef<MemRow>, ZmRef<IOBuf>, CommitFn);
  void del(ZmRef<MemRow>, ZmRef<IOBuf>, CommitFn);

private:
  ZuDerive(KeyGroup, (ZtArray<unsigned, ZtArrayHeapID<"ZdbMem.KeyGroup">>));
  ZuDerive(Indices, (ZtArray<Index, ZtArrayHeapID<"ZdbMem.Indices">>));
  ZuDerive(MaxUN, (ZtArray<UN, ZtArrayHeapID<"ZdbMem.MaxUN">>));

  Store			*m_store;
  IDString		m_id;
  bool			m_internal;
  ZfVFieldArray		m_fields;
  ZfVKeyFieldArray	m_keyFields;
  XFields		m_xFields;
  XKeyFields		m_xKeyFields;
  KeyGroup		m_keyGroup;	// length of group key, 0 if none
  IndexUN		m_indexUN;
  Indices		m_indices;
  IOBufAllocFn		m_bufAllocFn;
  unsigned		m_rowAlign = ZiFB::Wire::Align;

  bool			m_opened = false;

  MaxUN			m_maxUN;
  SN			m_maxSN = ZdbNullSN();
};

// --- in-memory data store

template <typename StoreTbl_>
inline auto StoreTbl_IDAxor(const StoreTbl_ &tbl) {
  return ZuTuple<bool, ZuCSpan>{tbl.internal(), tbl.id()};
}
ZmHashDeriveT((StoreTbl_), StoreTbls_, StoreTbl_,
  (ZmHashNode<StoreTbl_,
    ZmHashKey<StoreTbl_IDAxor<StoreTbl_>,
      ZmHashLock<ZmPLock,
	ZmHashHeapID<"ZdbMem.StoreTbl">>>>));

struct MemStoreCf {
  String	thread;
};

ZfStruct(ZdbAPI, (MemStoreCf, Cf),
  (thread, (Mutable, Required),		String));

template <typename StoreTbl_>
class Store_ : public Zdb_::Store, public Store__ {
public:
  using StoreTbl = StoreTbl_;
private:
  using StoreTbls = StoreTbls_<StoreTbl>;
  using StoreTblNode = typename StoreTbls::Node;

public:
  InitResult init(
      const ZfCf::AnyNode *cf, ZiMultiplex *mx, unsigned nShards,
      FailFn failFn) {
    if (!m_storeTbls) m_storeTbls = new StoreTbls{};
    if (m_nShards && m_nShards != nShards)
      return ZeEXCEPT(Fatal, "ZdbMem", ([
	m_nShards = m_nShards, nShards
      ](auto &s, const auto &) {
	s << "Store::init() failed: configured shard count " << nShards
	  << " differs from stored shard count " << m_nShards;
      }));
    m_failFn = ZuMv(failFn);
    try {
      MemStoreCf config;
      ZfCf::handler<MemStoreCf>(cf).update(config);
      const auto &tid = config.thread;
      auto sid = mx->sid(tid);
      if (!sid ||
	  sid > mx->params().nThreads() ||
	  sid == mx->rxThread() ||
	  sid == mx->txThread())
	return ZeEXCEPT(Fatal, "ZdbMem",
	  ([tid = ZeString{tid}](auto &s, const auto &) {
	    s << "Store::init() failed: invalid thread configuration \""
	      << tid << '"';
	  }));
      Store__::init(mx, sid);
    } catch (const ZeException &e) {
      return ZeEXCEPT(Fatal, "ZdbMem", ([e](auto &s, const auto &) {
	s << "Store::init() failed: invalid configuration: " << e;
      }));
    }
    m_nShards = nShards;
    return InitData{.replicated = false};
  }
  void final() {
    m_failFn = FailFn{};
    if (!m_preserve) {
      m_storeTbls->clean();
      m_storeTbls = nullptr;
      m_nShards = 0;
    }
  }

  void fail(ZeException e) { m_failFn(ZuMv(e)); } // simulate async store failure

  void preserve() { m_preserve = true; }

  void open(
    bool internal,
    IDString id,
    ZfVFieldArray fields, ZfVKeyFieldArray keyFields,
    Schema schema,
    IOBufAllocFn bufAllocFn, OpenFn openFn)
  {
    StoreTblNode *storeTbl =
      m_storeTbls->find(ZuTuple<bool, ZuCSpan>{internal, id});
    if (storeTbl && storeTbl->opened()) {
      openFn(OpenResult{ZeEXCEPT(Error, "ZdbMem",
	  ([id = ZuMv(id)](auto &s, const auto &) {
	    s << "open(" << id << ") failed - already open";
	  }))});
      return;
    }
    if (!storeTbl) {
      storeTbl = new StoreTblNode{
	this, internal, ZuMv(id), m_nShards,
	ZuMv(fields), ZuMv(keyFields), schema, ZuMv(bufAllocFn)};
      m_storeTbls->addNode(storeTbl);
    } else
      storeTbl->schema(schema);
    storeTbl->open();
    openFn(OpenResult{OpenData{
      .storeTbl = storeTbl,
      .count = storeTbl->count(),
      .un = storeTbl->maxUN(),
      .sn = storeTbl->maxSN()
    }});
  }

private:
  ZmRef<StoreTbls>	m_storeTbls;
  FailFn		m_failFn;
  unsigned		m_nShards = 0;
  bool			m_preserve = false;
};

ZuDerive(Store, (Store_<StoreTbl>));

} // ZdbMem

// main data store driver entry point
extern "C" {
  ZdbExtern Zdb_::Store *ZdbStore();
}

#endif /* ZdbMemStore_HH */
