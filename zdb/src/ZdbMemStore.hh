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

#include <zlib/ZmHash.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZtCase.hh>

#include <zlib/ZiLog.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/ZdbStore.hh>

namespace ZdbMem {

using namespace Zdb_;

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

// all supported types
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
  const reflection::Field	*field;
  unsigned			type;	// Value union discriminator
};
ZuDerive(XFields, (ZtArray<XField, ZtArrayHeapID<"ZdbMem.XField">>));
ZuDerive(XKeyFields, (ZtArray<XFields, ZtArrayHeapID<"ZdbMem.XKeyField">>));

// resolve Value union discriminator from flatbuffers reflection data
XField xField(
  const Zfb::Vector<Zfb::Offset<reflection::Field>> *fbFields_,
  const ZfVField *field,
  ZuCSpan id)
{
  // resolve flatbuffers reflection data for field
  const reflection::Field *fbField = fbFields_->LookupByKey(id);
  if (!fbField) return {nullptr, 0};
  unsigned type = 0;
  auto ftype = field->type;
  switch (fbField->type()->base_type()) {
    case reflection::String:
      if (ftype->code == ZfFieldTC::CString ||
	  ftype->code == ZfFieldTC::String)
	type = Value::Index<String>{};
      break;
    case reflection::Bool:
      if (ftype->code == ZfFieldTC::Bool)
	type = Value::Index<bool>{};
      break;
    case reflection::Byte:
      if (ftype->code == ZfFieldTC::Int8)
	type = Value::Index<int8_t>{};
      break;
    case reflection::UByte:
      if (ftype->code == ZfFieldTC::UInt8)
	type = Value::Index<uint8_t>{};
      break;
    case reflection::Short:
      if (ftype->code == ZfFieldTC::Int16)
	type = Value::Index<int16_t>{};
      break;
    case reflection::UShort:
      if (ftype->code == ZfFieldTC::UInt16)
	type = Value::Index<uint16_t>{};
      break;
    case reflection::Int:
      if (ftype->code == ZfFieldTC::Int32)
	type = Value::Index<int32_t>{};
      break;
    case reflection::UInt:
      if (ftype->code == ZfFieldTC::UInt32)
	type = Value::Index<uint32_t>{};
      break;
    case reflection::Long:
      if (ftype->code == ZfFieldTC::Int64)
	type = Value::Index<int64_t>{};
      break;
    case reflection::ULong:
      if (ftype->code == ZfFieldTC::UInt64)
	type = Value::Index<uint64_t>{};
      break;
    case reflection::Double:
      if (ftype->code == ZfFieldTC::Float)
	type = Value::Index<double>{};
      break;
    case reflection::Obj: {
      switch (ftype->code) {
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
	case ZfFieldTC::UDT: {
	  auto typeID = ftype->info.udt()->id;
	  if (typeID == "Bitmap") {
	    type = Value::Index<ZtBitmap>{};
	    break;
	  }
	  if (typeID == "IP") {
	    type = Value::Index<ZiIP>{};
	    break;
	  }
	}
      }
    } break;
    case reflection::Union:
      if (ftype->code == ZfFieldTC::UDT &&
	  ftype->info.udt()->id == "IP")
	type = Value::Index<ZiIP>{};
      break;
    case reflection::Vector:
      switch (fbField->type()->element()) {
	default: break;
	case reflection::String:
	  if (ftype->code == ZfFieldTC::StringVec)
	    type = Value::Index<StringVec>{};
	  break;
	case reflection::Byte:
	  if (ftype->code == ZfFieldTC::Int8Vec)
	    type = Value::Index<Int8Vec>{};
	  break;
	case reflection::UByte:
	  if (ftype->code == ZfFieldTC::Bytes)
	    type = Value::Index<Bytes>{};
	  else if (ftype->code == ZfFieldTC::UInt8Vec)
	    type = Value::Index<UInt8Vec>{};
	  break;
	case reflection::Short:
	  if (ftype->code == ZfFieldTC::Int16Vec)
	    type = Value::Index<Int16Vec>{};
	  break;
	case reflection::UShort:
	  if (ftype->code == ZfFieldTC::UInt16Vec)
	    type = Value::Index<UInt16Vec>{};
	  break;
	case reflection::Int:
	  if (ftype->code == ZfFieldTC::Int32Vec)
	    type = Value::Index<Int32Vec>{};
	  break;
	case reflection::UInt:
	  if (ftype->code == ZfFieldTC::UInt32Vec)
	    type = Value::Index<UInt32Vec>{};
	  break;
	case reflection::Long:
	  if (ftype->code == ZfFieldTC::Int64Vec)
	    type = Value::Index<Int64Vec>{};
	  break;
	case reflection::ULong:
	  if (ftype->code == ZfFieldTC::UInt64Vec)
	    type = Value::Index<UInt64Vec>{};
	  break;
	case reflection::Double:
	  if (ftype->code == ZfFieldTC::FloatVec)
	    type = Value::Index<FloatVec>{};
	  break;
	case reflection::Obj:
	  switch (ftype->code) {
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
  return {fbField, type};
}

// --- load value from flatbuffer

template <unsigned Type, typename = ZuIfT<(Type < Value::N)>>
inline void loadValue(
  void *ptr, const reflection::Field *field, const Zfb::Table *fbo)
{
  if constexpr (Type == Value::Index<void>{}) { } else if constexpr (Type == Value::Index<String>{}) {
    new (ptr) String{Zfb::Load::str(Zfb::GetFieldS(*fbo, *field))};
  } else if constexpr (Type == Value::Index<Bytes>{}) {
    new (ptr) Bytes{Zfb::Load::bytes(Zfb::GetFieldV<uint8_t>(*fbo, *field))};
  } else if constexpr (Type == Value::Index<bool>{}) {
    *static_cast<bool *>(ptr) = Zfb::GetFieldI<bool>(*fbo, *field);
  }

#define zdbtest_LoadInt(width) \
  else if constexpr (Type == Value::Index<int##width##_t>{}) {  \
    *static_cast<int##width##_t *>(ptr) =  \
      Zfb::GetFieldI<int##width##_t>(*fbo, *field);  \
  } \
  else if constexpr (Type == Value::Index<uint##width##_t>{}) {  \
    *static_cast<uint##width##_t *>(ptr) =  \
    Zfb::GetFieldI<uint##width##_t>(*fbo, *field);  \
  }

  zdbtest_LoadInt(8)
  zdbtest_LoadInt(16)
  zdbtest_LoadInt(32)
  zdbtest_LoadInt(64)

  else if constexpr (Type == Value::Index<double>{}) {
    *static_cast<double *>(ptr) = Zfb::GetFieldF<double>(*fbo, *field);
  } else if constexpr (Type == Value::Index<ZuFixed>{}) {
    new (ptr) ZuFixed{ZfbTransform::Fixed::load(
      fbo->GetStruct<const Zfb::Fixed *>(field->offset()))
    };
  } else if constexpr (Type == Value::Index<ZuDecimal>{}) {
    new (ptr) ZuDecimal{ZfbTransform::Decimal::load(
      fbo->GetStruct<const Zfb::Decimal *>(field->offset()))
    };
  } else if constexpr (Type == Value::Index<ZuTime>{}) {
    new (ptr) ZuTime{ZfbTransform::Time::load(
      fbo->GetStruct<const Zfb::Time *>(field->offset()))
    };
  } else if constexpr (Type == Value::Index<ZuDateTime>{}) {
    new (ptr) ZuDateTime{ZfbTransform::DateTime::load(
      fbo->GetStruct<const Zfb::DateTime *>(field->offset()))
    };
  } else if constexpr (Type == Value::Index<int128_t>{}) {
    *static_cast<int128_t *>(ptr) = ZfbTransform::Int128::load(
      fbo->GetStruct<const Zfb::Int128 *>(field->offset()));
  } else if constexpr (Type == Value::Index<uint128_t>{}) {
    *static_cast<uint128_t *>(ptr) = ZfbTransform::UInt128::load(
      fbo->GetStruct<const Zfb::UInt128 *>(field->offset()));
  } else if constexpr (Type == Value::Index<ZtBitmap>{}) {
    new (ptr) ZtBitmap{ZfbTransform::Bitmap::load<ZtBitmap>(
      fbo->GetPointer<const Zfb::Bitmap *>(field->offset()))};
  } else if constexpr (Type == Value::Index<ZiIP>{}) {
    new (ptr) ZiIP{ZfbTransform::IP::load(
      static_cast<Zfb::IP>(fbo->GetField<uint8_t>(field->offset() - 2, 0)),
      fbo->GetPointer<const void *>(field->offset()))};
  } else if constexpr (Type == Value::Index<StringVec>{}) {
    auto v = Zfb::GetFieldV<Zfb::Offset<Zfb::String>>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    auto array = new (ptr) StringVec(n);
    for (unsigned i = 0; i < n; i++) array->push(Zfb::Load::str(v->Get(i)));
  } else if constexpr (Type == Value::Index<BytesVec>{}) {
    auto v = Zfb::GetFieldV<Zfb::Offset<Zfb::Bytes>>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    auto array = new (ptr) BytesVec(n);
    for (unsigned i = 0; i < n; i++)
      array->push(Zfb::Load::bytes(v->Get(i)->data()));
  }

#define zdbtest_LoadIntVec(width) \
  else if constexpr (Type == Value::Index<Int##width##Vec>{}) {  \
    auto v = Zfb::GetFieldV<int##width##_t>(*fbo, *field);  \
    unsigned n = v ? v->size() : 0;  \
    auto array = new (ptr) Int##width##Vec(n);  \
    for (unsigned i = 0; i < n; i++) array->push(v->Get(i));  \
  } \
  else if constexpr (Type == Value::Index<UInt##width##Vec>{}) {  \
    auto v = Zfb::GetFieldV<uint##width##_t>(*fbo, *field);  \
    unsigned n = v ? v->size() : 0;  \
    auto array = new (ptr) UInt##width##Vec(n);  \
    for (unsigned i = 0; i < n; i++) array->push(v->Get(i));  \
  }

  zdbtest_LoadIntVec(8)
  zdbtest_LoadIntVec(16)
  zdbtest_LoadIntVec(32)
  zdbtest_LoadIntVec(64)

  else if constexpr (Type == Value::Index<Int128Vec>{}) {
    auto v = Zfb::GetFieldV<Zfb::Int128 *>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    auto array = new (ptr) Int128Vec(n);
    for (unsigned i = 0; i < n; i++)
      array->push(ZfbTransform::Int128::load(v->Get(i)));
  } else if constexpr (Type == Value::Index<UInt128Vec>{}) {
    auto v = Zfb::GetFieldV<Zfb::UInt128 *>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    auto array = new (ptr) UInt128Vec(n);
    for (unsigned i = 0; i < n; i++)
      array->push(ZfbTransform::UInt128::load(v->Get(i)));
  } else if constexpr (Type == Value::Index<FloatVec>{}) {
    auto v = Zfb::GetFieldV<double>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    auto array = new (ptr) FloatVec(n);
    for (unsigned i = 0; i < n; i++) array->push(v->Get(i));
  } else if constexpr (Type == Value::Index<FixedVec>{}) {
    auto v = Zfb::GetFieldV<Zfb::Fixed *>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    auto array = new (ptr) FixedVec(n);
    for (unsigned i = 0; i < n; i++)
      array->push(ZfbTransform::Fixed::load(v->Get(i)));
  } else if constexpr (Type == Value::Index<DecimalVec>{}) {
    auto v = Zfb::GetFieldV<Zfb::Decimal *>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    auto array = new (ptr) DecimalVec(n);
    for (unsigned i = 0; i < n; i++)
      array->push(ZfbTransform::Decimal::load(v->Get(i)));
  } else if constexpr (Type == Value::Index<TimeVec>{}) {
    auto v = Zfb::GetFieldV<Zfb::Time *>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    auto array = new (ptr) TimeVec(n);
    for (unsigned i = 0; i < n; i++)
      array->push(ZfbTransform::Time::load(v->Get(i)));
  } else if constexpr (Type == Value::Index<DateTimeVec>{}) {
    auto v = Zfb::GetFieldV<Zfb::DateTime *>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    auto array = new (ptr) DateTimeVec(n);
    for (unsigned i = 0; i < n; i++)
      array->push(ZfbTransform::DateTime::load(v->Get(i)));
  }
}

// --- save value to flatbuffer (for passing to Zdb/app)

using Offset = Zfb::Offset<void>;

struct Offsets {
  Offset		*data;
  unsigned		in = 0;
  mutable unsigned	out = 0;

  Offsets(Offset *data_) : data{data_} { }

  bool operator !() const { return !data; }

  void push(Offset o) { data[in++] = o; }
  Offset shift() const { return data[out++]; }
};

template <unsigned Type, typename = ZuIfT<(Type < Value::N)>>
inline void saveOffset(
  Zfb::Builder &fbb, Offsets &offsets, const Value &value)
{
  if constexpr (Type == Value::Index<String>{}) {
    offsets.push(Zfb::Save::str(fbb, value.p<Type>()).Union());
  } else if constexpr (Type == Value::Index<Bytes>{}) {
    offsets.push(Zfb::Save::bytes(fbb, value.p<Type>()).Union());
  } else if constexpr (Type == Value::Index<StringVec>{}) {
    const auto &array = value.p<StringVec>();
    unsigned n = array.length();
    offsets.push(
      Zfb::Save::strVecIter(fbb, n, [&array](unsigned i) {
	return array[i];
      }).Union());
  } else if constexpr (Type == Value::Index<BytesVec>{}) {
    const auto &array = value.p<BytesVec>();
    unsigned n = array.length();
    offsets.push(Zfb::Save::vectorIter<Zfb::Bytes>(fbb, n,
      [&array](Zfb::Builder &fbb, unsigned i) {
	return Zfb::CreateBytes(fbb, Zfb::Save::bytes(fbb, array[i]));
      }).Union());
  }

#define ZdbPQ_SaveIntVec(width) \
  else if constexpr (Type == Value::Index<Int##width##Vec>{}) {  \
    const auto &array = value.p<Int##width##Vec>();  \
    unsigned n = array.length();  \
    offsets.push(Zfb::Save::pvectorIter<int##width##_t>(  \
	fbb, n, [&array](unsigned i) { return array[i]; }).Union());  \
  } \
  else if constexpr (Type == Value::Index<UInt##width##Vec>{}) {  \
    const auto &array = value.p<UInt##width##Vec>();  \
    unsigned n = array.length();  \
    offsets.push(Zfb::Save::pvectorIter<uint##width##_t>(  \
	fbb, n, [&array](unsigned i) { return array[i]; }).Union());  \
  }

  ZdbPQ_SaveIntVec(8)
  ZdbPQ_SaveIntVec(16)
  ZdbPQ_SaveIntVec(32)
  ZdbPQ_SaveIntVec(64)

  else if constexpr (Type == Value::Index<Int128Vec>{}) {
    const auto &array = value.p<Int128Vec>();
    unsigned n = array.length();
    offsets.push(Zfb::Save::structVecIter<Zfb::Int128>(fbb, n,
      [&array](Zfb::Int128 *ptr, unsigned i) {
	*ptr = ZfbTransform::Int128::save(array[i]);
      }).Union());
  } else if constexpr (Type == Value::Index<UInt128Vec>{}) {
    const auto &array = value.p<UInt128Vec>();
    unsigned n = array.length();
    offsets.push(Zfb::Save::structVecIter<Zfb::UInt128>(fbb, n,
      [&array](Zfb::UInt128 *ptr, unsigned i) {
	*ptr = ZfbTransform::UInt128::save(array[i]);
      }).Union());
  } else if constexpr (Type == Value::Index<FloatVec>{}) {
    const auto &array = value.p<FloatVec>();
    unsigned n = array.length();
    offsets.push(Zfb::Save::pvectorIter<double>(fbb, n, [&array](unsigned i) {
      return array[i];
    }).Union());
  } else if constexpr (Type == Value::Index<FixedVec>{}) {
    const auto &array = value.p<FixedVec>();
    unsigned n = array.length();
    offsets.push(Zfb::Save::structVecIter<Zfb::Fixed>(fbb, n,
      [&array](Zfb::Fixed *ptr, unsigned i) {
	*ptr = ZfbTransform::Fixed::save(array[i]);
      }).Union());
  } else if constexpr (Type == Value::Index<DecimalVec>{}) {
    const auto &array = value.p<DecimalVec>();
    unsigned n = array.length();
    offsets.push(Zfb::Save::structVecIter<Zfb::Decimal>(fbb, n,
      [&array](Zfb::Decimal *ptr, unsigned i) {
	*ptr = ZfbTransform::Decimal::save(array[i]);
      }).Union());
  } else if constexpr (Type == Value::Index<TimeVec>{}) {
    const auto &array = value.p<TimeVec>();
    unsigned n = array.length();
    offsets.push(Zfb::Save::structVecIter<Zfb::Time>(fbb, n,
      [&array](Zfb::Time *ptr, unsigned i) {
	*ptr = ZfbTransform::Time::save(array[i]);
      }).Union());
  } else if constexpr (Type == Value::Index<DateTimeVec>{}) {
    const auto &array = value.p<DateTimeVec>();
    unsigned n = array.length();
    offsets.push(Zfb::Save::structVecIter<Zfb::DateTime>(fbb, n,
      [&array](Zfb::DateTime *ptr, unsigned i) {
	*ptr = ZfbTransform::DateTime::save(array[i]);
      }).Union());
  } else if constexpr (Type == Value::Index<ZtBitmap>{}) {
    offsets.push(ZfbTransform::Bitmap::save(fbb, value.p<Type>()).Union());
  } else if constexpr (Type == Value::Index<ZiIP>{}) {
    offsets.push(ZfbTransform::IP::save(fbb, value.p<Type>()).offset);
  } else if constexpr (Type != Value::Index<String>{} &&
    Type != Value::Index<Bytes>{} &&
    Type != Value::Index<ZtBitmap>{} &&
    Type != Value::Index<ZiIP>{} &&
    !isVec(Type)) { }
}

template <unsigned Type>
inline void saveValue(
  Zfb::Builder &fbb, const Offsets &offsets, const reflection::Field *field, const Value &value)
{
  if constexpr (Type == Value::Index<void>{}) { } else if constexpr (Type == Value::Index<String>{} ||
    Type == Value::Index<Bytes>{} ||
    Type == Value::Index<ZtBitmap>{} ||
    isVec(Type)) {
    fbb.AddOffset(field->offset(), offsets.shift());
  } else if constexpr (Type == Value::Index<bool>{}) {
    fbb.AddElement<bool>(
      field->offset(), value.p<Type>(), field->default_integer());
  }

#define zdbtest_SaveInt(width) \
  else if constexpr (Type == Value::Index<int##width##_t>{}) {  \
    fbb.AddElement<int##width##_t>(  \
      field->offset(), value.p<Type>(), field->default_integer());  \
  } \
  else if constexpr (Type == Value::Index<uint##width##_t>{}) {  \
    fbb.AddElement<uint##width##_t>(  \
      field->offset(), value.p<Type>(), field->default_integer());  \
  }

  zdbtest_SaveInt(8)
  zdbtest_SaveInt(16)
  zdbtest_SaveInt(32)
  zdbtest_SaveInt(64)

  else if constexpr (Type == Value::Index<double>{}) {
    fbb.AddElement<double>(
      field->offset(), value.p<Type>(), field->default_real());
  } else if constexpr (Type == Value::Index<ZuFixed>{}) {
    auto v = ZfbTransform::Fixed::save(value.p<Type>());
    fbb.AddStruct(field->offset(), &v);
  } else if constexpr (Type == Value::Index<ZuDecimal>{}) {
    auto v = ZfbTransform::Decimal::save(value.p<Type>());
    fbb.AddStruct(field->offset(), &v);
  } else if constexpr (Type == Value::Index<ZuTime>{}) {
    auto v = ZfbTransform::Time::save(value.p<Type>());
    fbb.AddStruct(field->offset(), &v);
  } else if constexpr (Type == Value::Index<ZuDateTime>{}) {
    auto v = ZfbTransform::DateTime::save(value.p<Type>());
    fbb.AddStruct(field->offset(), &v);
  } else if constexpr (Type == Value::Index<int128_t>{}) {
    auto v = ZfbTransform::Int128::save(value.p<Type>());
    fbb.AddStruct(field->offset(), &v);
  } else if constexpr (Type == Value::Index<uint128_t>{}) {
    auto v = ZfbTransform::UInt128::save(value.p<Type>());
    fbb.AddStruct(field->offset(), &v);
  } else if constexpr (Type == Value::Index<ZiIP>{}) {
    fbb.AddElement<uint8_t>(
      field->offset() - 2,
      static_cast<uint8_t>(ZfbTransform::IP::type(value.p<Type>())), 0);
    fbb.AddOffset(field->offset(), offsets.shift());
  }
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
  const Zfb::Table *fbo,
  Filter filter)
{
  Tuple tuple(nParams); // not {}
  for (unsigned i = 0; i < nParams; i++)
    if (filter(fields[i])) {
      auto value = static_cast<Value *>(tuple.push());
      auto type = xFields[i].type;
      ZuSwitch::dispatch<Value::N>(type,
	[value, field = xFields[i].field, fbo](auto I) {
	  loadValue<I>(value->new_<I, true>(), field, fbo);
	});
    } else
      new (tuple.push()) Value{};
  return tuple;
}
Tuple loadTuple_(
  unsigned nParams,
  const ZfVFieldArray &fields,
  const XFields &xFields,
  const Zfb::Table *fbo)
{
  return loadTuple_(nParams, fields, xFields, fbo,
    [](const ZfVField *) { return true; });
}
Tuple loadTuple(
  const ZfVFieldArray &fields, const XFields &xFields, const Zfb::Table *fbo)
{
  return loadTuple_(fields.length(), fields, xFields, fbo);
}
Tuple loadUpdTuple(
  const ZfVFieldArray &fields, const XFields &xFields, const Zfb::Table *fbo)
{
  return loadTuple_(fields.length(), fields, xFields, fbo,
    [](const ZfVField *field) -> bool {
      return bool(field->props & ZfVFieldProp::Mutable()) || (field->keys & 1);
    });
}
Tuple loadDelTuple(
  const ZfVFieldArray &fields, const XFields &xFields, const Zfb::Table *fbo)
{
  return loadTuple_(fields.length(), fields, xFields, fbo,
    [](const ZfVField *field) -> bool { return (field->keys & 1); });
}

// save tuple to flatbuffer
Offset saveTuple(
  Zfb::Builder &fbb,
  const XFields &xFields,
  ZuSpan<const Value> tuple)
{
  unsigned n = xFields.length();
  ZmAssert(tuple.length() == n);
  auto offsets_ = ZmScratch(Offset, n);
  Offsets offsets(offsets_.data());
  if (!offsets) return {};
  for (unsigned i = 0; i < n; i++) {
    auto type = xFields[i].type;
    const auto &value = tuple[i];
    ZuSwitch::dispatch<Value::N>(type,
      [&fbb, &offsets, &value](auto I) {
	saveOffset<I>(fbb, offsets, value);
      });
  }
  auto start = fbb.StartTable();
  for (unsigned i = 0; i < n; i++) {
    auto type = xFields[i].type;
    const auto &value = tuple[i];
    ZuSwitch::dispatch<Value::N>(type,
      [&fbb, &offsets, field = xFields[i].field, &value](auto I) {
	saveValue<I>(fbb, offsets, field, value);
      });
  }
  auto end = fbb.EndTable(start);
  return Offset{end};
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
Tuple extractKey(
  const ZfVFieldArray &fields,
  const ZfVKeyFieldArray &keyFields,
  KeyID keyID, const Tuple &data)
{
  ZmAssert(keyID >= 0 && keyID < 64);
  ZmAssert(fields.length() == data.length());
  Tuple key(keyFields[keyID].length()); // not {}
  unsigned m = fields.length();
  for (unsigned j = 0; j < m; j++)
    if (fields[j]->keys & (uint64_t(1)<<keyID)) key.push(data[j]);
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
    const reflection::Schema *schema, IOBufAllocFn bufAllocFn)
  :
    m_store{store}, m_id{ZuMv(id)}, m_internal{internal},
    m_fields{ZuMv(fields)}, m_keyFields{ZuMv(keyFields)},
    m_bufAllocFn{ZuMv(bufAllocFn)}
  {
    // introspect fields and flatbuffers reflection data, building
    // m_xFields[], m_keyGroup[] and m_xKeyFields[]
    for (unsigned i = 0, n = keyFields.length(); i < n; i++) {
      uint64_t descending = 0;
      unsigned m = keyFields[i].length();
      ZmAssert(m < 64);
      for (unsigned j = 0; j < m; j++)
	if (keyFields[i][j]->descend & (uint64_t(1)<<i))
	  descending |= (uint64_t(1)<<j);
      new (m_indices.push()) Index{TupleCmp<>{descending}};
    }
    const reflection::Object *rootTbl = schema->root_table();
    const Zfb::Vector<Zfb::Offset<reflection::Field>> *fbFields_ =
      rootTbl->fields();
    unsigned n = m_fields.length();
    m_xFields.size(n);
    for (unsigned i = 0; i < n; i++)
      ZtCase::camelSnake(m_fields[i]->id,
	[this, fbFields_, i](ZuCSpan id) {
	  m_xFields.push(xField(fbFields_, m_fields[i], id));
	});
    n = m_keyFields.length();
    m_xKeyFields.size(n);
    m_keyGroup.length(n);
    for (unsigned i = 0; i < n; i++) {
      unsigned m = m_keyFields[i].length();
      new (m_xKeyFields.push()) XFields{m};
      m_keyGroup[i] = 0;
      for (unsigned j = 0; j < m; j++) {
	if (m_keyFields[i][j]->group & (uint64_t(1)<<i))
	  m_keyGroup[i] = j + 1;
	ZtCase::camelSnake(m_keyFields[i][j]->id,
	  [this, fbFields_, i, j](ZuCSpan id) {
	    m_xKeyFields[i].push(xField(fbFields_, m_keyFields[i][j], id));
	  });
      }
    }
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
  // load a row from a buffer containing a replication/recovery message
  ZmRef<const MemRow> loadRow(const ZmRef<IOBuf> &buf) {
    auto record = record_(msg_(buf->hdr()));
    auto sn = ZfbTransform::UInt128::load(record->sn());
    auto data = Zfb::Load::bytes(record->data());
    auto fbo = Zfb::GetAnyRoot(data.data());
    Tuple tuple;
    if (!record->vn())
      tuple = loadTuple(m_fields, m_xFields, fbo);
    else if (record->vn() > 0)
      tuple = loadUpdTuple(m_fields, m_xFields, fbo);
    else
      tuple = loadDelTuple(m_fields, m_xFields, fbo);
    return new MemRow{
      record->shard(), record->un(), sn, record->vn(), ZuMv(tuple)};
  }

  // save a row to a buffer as a replication/recovery message
  template <bool Recovery>
  ZmRef<IOBuf> saveRow(const ZmRef<const MemRow> &row) {
    Zfb::IOBuilder fbb{m_bufAllocFn()};
    auto data = Zfb::Save::nest(fbb, [this, &row](Zfb::Builder &fbb) {
      return saveTuple(fbb, m_xFields, row->data);
    });
    if (ZuUnlikely(data.IsNull())) return {};
    {
      auto sn = ZfbTransform::UInt128::save(row->sn);
      auto msg = fbs::CreateMsg(
	fbb, Recovery ? fbs::Body::Recovery : fbs::Body::Replication,
	fbs::CreateRecord(
	  fbb, Zfb::Save::str(fbb, this->id()),
	  row->un, &sn, row->vn, row->shard, data).Union());
      fbb.Finish(msg);
    }
    return saveHdr(fbb);
  }

public:
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
    const reflection::Schema *schema,
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
    }
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
