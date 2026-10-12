//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zflatcfixture_HH
#define zflatcfixture_HH

#include <limits.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtCase.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiFBReflect.hh>

namespace FlatcFixture {

ZuDerive(Text, (ZtString<ZtStringHeapID<"zflatctest.Text">>));

inline bool write(ZuCSpan name, ZuCSpan data)
{
  ZiFile file;
  if (file.open(Zi::Path{name}, ZiFile::Write | ZiFile::GC) != Zi::OK) return false;
  if (file.write(data.data(), data.length()) != Zi::OK) return false;
  return file.close() == Zi::OK;
}

inline Text read(ZuCSpan name)
{
  ZiFile file;
  if (file.open(Zi::Path{name}, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK) return {};
  auto stat = file.fstat();
  if (!stat) return {};
  auto size = stat.size();
  if (size < 0 || size > INT_MAX) return {};
  Text out;
  out.length(size);
  if (size && file.read(out.data(), size) != size) return {};
  return out;
}

// Normalize references to names, preserve all recovered wire semantics, and
// compare scalar default bits (including negative zero and NaN).
inline Text contract(ZuBSpan bytes)
{
  namespace R = ZiFB::Reflect;
  if (!R::verifySchema(bytes)) return {};
  auto schema = ZiFB::view<R::Schema>(bytes);
  auto objects = schema.get<R::ZfField(Schema, objects)>();
  auto enums = schema.get<R::ZfField(Schema, enums)>();
  Text out;
  auto attrs = [&out](auto list) {
    for (unsigned i = 0; i < list.length(); ++i)
      out << ' ' << list[i].template get<R::ZfField(KeyValue, key)>() << '='
	<< list[i].template get<R::ZfField(KeyValue, value)>();
    out << '\n';
  };
  auto type = [&](auto t) {
    auto base = t.template get<R::ZfField(Type, baseType)>();
    auto elem = t.template get<R::ZfField(Type, element)>();
    auto index = t.template get<R::ZfField(Type, index)>();
    out << ZuBox<int>{base} << ':' << ZuBox<int>{elem} << ':'
      << ZuBox<unsigned>{t.template get<R::ZfField(Type, fixedLength)>()} << ':'
      << ZuBox<unsigned>{t.template get<R::ZfField(Type, baseSize)>()} << ':'
      << ZuBox<unsigned>{t.template get<R::ZfField(Type, elementSize)>()};
    if (index >= 0) {
      if (base == R::BaseType::Obj || elem == R::BaseType::Obj)
	out << ':' << objects[index].template get<R::ZfField(Object, name)>();
      else out << ':' << enums[index].template get<R::ZfField(Enum, name)>();
    }
    out << '\n';
  };
  out << "root " << R::rootObject(schema).get<R::ZfField(Object, name)>() << '\n';
  for (unsigned i = 0; i < objects.length(); ++i) {
    auto o = objects[i];
    out << "object " << o.get<R::ZfField(Object, name)>() << ' '
      << ZuBox<bool>{o.get<R::ZfField(Object, isStruct)>()} << ' '
      << ZuBox<int>{o.get<R::ZfField(Object, minalign)>()} << ' '
      << ZuBox<int>{o.get<R::ZfField(Object, bytesize)>()};
    attrs(o.get<R::ZfField(Object, attributes)>());
    auto fields = o.get<R::ZfField(Object, fields)>();
    for (unsigned j = 0; j < fields.length(); ++j) {
      auto f = fields[j];
      uint64_t real;
      double value = f.get<R::ZfField(Field, defaultReal)>();
      memcpy(&real, &value, sizeof(real));
      out << "field " << f.get<R::ZfField(Field, name)>() << ' '
	<< ZuBox<unsigned>{f.get<R::ZfField(Field, id)>()} << ' '
	<< ZuBox<unsigned>{f.get<R::ZfField(Field, offset)>()} << ' '
	<< ZuBox<int64_t>{f.get<R::ZfField(Field, defaultInteger)>()} << ' '
	<< ZuBox<uint64_t>{real} << ' '
	<< ZuBox<bool>{f.get<R::ZfField(Field, deprecated)>()} << ' '
	<< ZuBox<bool>{f.get<R::ZfField(Field, required)>()} << ' '
	<< ZuBox<bool>{f.get<R::ZfField(Field, optional)>()} << ' '
	<< ZuBox<unsigned>{f.get<R::ZfField(Field, padding)>()};
      attrs(f.get<R::ZfField(Field, attributes)>());
      type(f.get<R::ZfField(Field, type)>());
    }
  }
  for (unsigned i = 0; i < enums.length(); ++i) {
    auto e = enums[i];
    out << "enum " << e.get<R::ZfField(Enum, name)>() << ' '
      << ZuBox<bool>{e.get<R::ZfField(Enum, isUnion)>()};
    attrs(e.get<R::ZfField(Enum, attributes)>());
    type(e.get<R::ZfField(Enum, underlyingType)>());
    auto values = e.get<R::ZfField(Enum, values)>();
    for (unsigned j = 0; j < values.length(); ++j) {
      auto v = values[j];
      out << "value " << v.get<R::ZfField(EnumVal, name)>() << ' '
	<< ZuBox<int64_t>{v.get<R::ZfField(EnumVal, value)>()} << '\n';
      type(v.get<R::ZfField(EnumVal, unionType)>());
    }
  }
  return out;
}

template <typename O, typename Facet = ZuFacet::FB>
Text metadata()
{
  using H = typename ZiFB::As<O>::template Handler<O, Facet>;
  Text out;
  out << "fields " << ZuBox<unsigned>{H::SchemaFields::N} << ' '
    << ZuBox<unsigned>{H::AllFields::N} << '\n';
  ZuUnroll::all<typename H::SchemaFields>([&]<typename F>() {
    auto wire = ZuFieldProp::FB::name<F>();
    out << wire << ' ';
    ZtCase::snakeCamel(wire, [&](ZuCSpan cpp) { out << cpp; });
    out << ' ' << ZuBox<unsigned>{F::Code} << ' '
      << ZuBox<unsigned>{H::template id<F>()} << ' '
      << ZuBox<bool>{ZuFieldActive<F>{}} << '\n';
  });
  return out;
}

} // FlatcFixture

#endif // zflatcfixture_HH
