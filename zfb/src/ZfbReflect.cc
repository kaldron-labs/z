//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuBox.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuSearch.hh>
#include <zlib/ZfbReflect.hh>

namespace Zfb::Reflect {

ZtEnumImplNS(BaseType);
ZfStructImpl(Type);
ZfStructImpl(Type, FB);
ZfStructImpl(KeyValue);
ZfStructImpl(KeyValue, FB);
ZfStructImpl(EnumVal);
ZfStructImpl(EnumVal, FB);
ZfStructImpl(Enum);
ZfStructImpl(Enum, FB);
ZfStructImpl(Field);
ZfStructImpl(Field, FB);
ZfStructImpl(Object);
ZfStructImpl(Object, FB);
ZfStructImpl(RPCCall);
ZfStructImpl(RPCCall, FB);
ZfStructImpl(Service);
ZfStructImpl(Service, FB);
ZfStructImpl(SchemaFile);
ZfStructImpl(SchemaFile, FB);
ZfStructImpl(Schema);
ZfStructImpl(Schema, FB);

} // Zfb::Reflect

namespace Zfb::Reflect {

static bool unsignedType(int base)
{
  using namespace BaseType;
  switch (base) {
    case UType: case UByte: case UShort: case UInt: case ULong: return true;
    default: return false;
  }
}

FieldView field(ObjectView o, ZuCSpan name)
{
  auto fields = o.get<ZfField(Object, fields)>();
  auto i = ZuSearch(fields.length(), [fields, name](unsigned i) {
    return name.cmp(fields[i].get<ZfField(Field, name)>());
  });
  return ZuSearchFound(i) ? fields[ZuSearchPos(i)] : FieldView{};
}

FieldView field(ObjectView o, unsigned id)
{
  // BFBS fields are sorted by name, not by ID. This is setup-time lookup;
  // the caller retains the resulting field handle for row operations.
  for (auto f : o.get<ZfField(Object, fields)>())
    if (f.get<ZfField(Field, id)>() == id) return f;
  return {};
}

ObjectView object(SchemaView s, ZuCSpan name)
{
  auto objects = s.get<ZfField(Schema, objects)>();
  auto i = ZuSearch(objects.length(), [objects, name](unsigned i) {
    return name.cmp(objects[i].get<ZfField(Object, name)>());
  });
  return ZuSearchFound(i) ? objects[ZuSearchPos(i)] : ObjectView{};
}

EnumValView enumValue(EnumView e, int64_t value)
{
  auto values = e.get<ZfField(Enum, values)>();
  bool u = unsignedType(e.get<ZfField(Enum, underlyingType)>().get<ZfField(Type, baseType)>());
  auto i = ZuSearch(values.length(), [values, value, u](unsigned i) {
    auto other = values[i].get<ZfField(EnumVal, value)>();
    return u ? ZuCmp<uint64_t>::cmp(value, other) : ZuCmp<int64_t>::cmp(value, other);
  });
  return ZuSearchFound(i) ? values[ZuSearchPos(i)] : EnumValView{};
}

unsigned alignment(FieldView f)
{
  auto attrs = f.get<ZfField(Field, attributes)>();
  auto i = ZuSearch(attrs.length(), [attrs](unsigned i) {
    return ZuCSpan{"force_align"}.cmp(attrs[i].get<ZfField(KeyValue, key)>());
  });
  if (!ZuSearchFound(i)) return 1;
  return ZuBox<unsigned>{attrs[ZuSearchPos(i)].get<ZfField(KeyValue, value)>()};
}

static unsigned typeAlign(SchemaView s, TypeView t, ZuSpan<uint8_t> seen);

static unsigned fieldAlign(SchemaView s, FieldView f, ZuSpan<uint8_t> seen)
{
  auto n = typeAlign(s, type(f), seen);
  auto forced = alignment(f);
  return n > forced ? n : forced;
}

static unsigned typeAlign(SchemaView s, TypeView t, ZuSpan<uint8_t> seen)
{
  auto base = t.get<ZfField(Type, baseType)>();
  if (base == BaseType::Vector || base == BaseType::Array)
    base = t.get<ZfField(Type, element)>();
  auto index = t.get<ZfField(Type, index)>();
  switch (base) {
    case BaseType::Long: case BaseType::ULong: case BaseType::Double: return 8;
    case BaseType::Obj: {
      if (seen[index]) return 1;
      seen[index] = 1;
      auto o = s.get<ZfField(Schema, objects)>()[index];
      unsigned n = o.get<ZfField(Object, minalign)>();
      for (auto f : o.get<ZfField(Object, fields)>()) {
	auto align = fieldAlign(s, f, seen);
	if (n < align) n = align;
      }
      return n;
    }
    case BaseType::Union: {
      unsigned n = 1;
      auto e = s.get<ZfField(Schema, enums)>()[index];
      for (auto v : e.get<ZfField(Enum, values)>()) {
	auto alt = v.get<ZfField(EnumVal, unionType)>();
	if (!alt) continue;
	auto align = typeAlign(s, alt, seen);
	if (n < align) n = align;
      }
      return n;
    }
    default: return Wire::Align;
  }
}

unsigned bufferAlign(SchemaView s, FieldView f)
{
  auto n = s.get<ZfField(Schema, objects)>().length();
  auto seen = ZtScratch(Buffer, n, n);
  memset(seen.data(), 0, n);
  auto align = fieldAlign(s, f, seen);
  return align > Wire::Align ? align : Wire::Align;
}

unsigned bufferAlign(SchemaView s)
{
  auto n = s.get<ZfField(Schema, objects)>().length();
  auto seen = ZtScratch(Buffer, n, n);
  memset(seen.data(), 0, n);
  unsigned align = Wire::Align;
  for (auto f : rootObject(s).get<ZfField(Object, fields)>()) {
    auto value = fieldAlign(s, f, seen);
    if (align < value) align = value;
  }
  return align;
}

struct Shape { unsigned size, align; };

ZuInline static Shape shape(SchemaView s, int base, int index)
{
  using namespace BaseType;
  switch (base) {
    case UType: case Bool: case Byte: case UByte: return {1, 1};
    case Short: case UShort: return {2, 2};
    case Int: case UInt: case Float: return {4, 4};
    case Long: case ULong: case Double: return {8, 8};
    case Obj: {
      auto o = s.get<ZfField(Schema, objects)>()[index];
      if (o.get<ZfField(Object, isStruct)>())
	return {unsigned(o.get<ZfField(Object, bytesize)>()),
	  unsigned(o.get<ZfField(Object, minalign)>())};
      return {sizeof(Offset), sizeof(Offset)};
    }
    case BaseType::String: case BaseType::Vector: case BaseType::Union:
      return {sizeof(Offset), sizeof(Offset)};
    default: return {0, 1};
  }
}

template <typename F, typename V>
static bool sorted(V values)
{
  for (unsigned i = 1, n = values.length(); i < n; ++i)
    if (values[i - 1].template get<F>().cmp(values[i].template get<F>()) >= 0)
      return false;
  return true;
}

template <typename V>
static bool attributes(V attrs)
{
  if (!sorted<ZfField(KeyValue, key)>(attrs)) return false;
  for (auto a : attrs) {
    if (a.template get<ZfField(KeyValue, key)>() != "force_align") continue;
    auto text = a.template get<ZfField(KeyValue, value)>();
    ZuBox<unsigned> n;
    if (!text || n.scan(text) != text.length() ||
	!n || n > Wire::MaxAlign || (n & (n - 1))) return false;
  }
  return true;
}

// Validate the references used by shape() before any indexed access. The
// wire's redundant size hints are not used to address payload memory.
static bool schemaType(SchemaView s, TypeView t)
{
  if (!t) return false;
  int base = t.get<ZfField(Type, baseType)>();
  int index = t.get<ZfField(Type, index)>();
  bool array = base == BaseType::Array;
  if (base == BaseType::Vector || array) {
    base = t.get<ZfField(Type, element)>();
    if (array && !t.get<ZfField(Type, fixedLength)>()) return false;
    if (base == BaseType::Vector || base == BaseType::Union || base == BaseType::UType)
      return false;
  }
  auto objects = s.get<ZfField(Schema, objects)>();
  auto enums = s.get<ZfField(Schema, enums)>();
  if (base == BaseType::Obj)
    return index >= 0 && unsigned(index) < objects.length() &&
      (!array || objects[index].get<ZfField(Object, isStruct)>());
  if (base == BaseType::String) return !array;
  if (base == BaseType::Union || base == BaseType::UType) {
    return index >= 0 && unsigned(index) < enums.length() &&
      enums[index].get<ZfField(Enum, isUnion)>() &&
      enums[index].get<ZfField(Enum, underlyingType)>().get<ZfField(Type, baseType)>() ==
	BaseType::UType;
  }
  if (base < BaseType::Bool || base > BaseType::Double) return false;
  if (index == -1) return true;
  return index >= 0 && unsigned(index) < enums.length() &&
    !enums[index].get<ZfField(Enum, isUnion)>() &&
    enums[index].get<ZfField(Enum, underlyingType)>().get<ZfField(Type, baseType)>() == base;
}

static bool schemaObject(SchemaView s, ObjectView o)
{
  auto fields = o.get<ZfField(Object, fields)>();
  if (!sorted<ZfField(Field, name)>(fields) ||
      !attributes(o.get<ZfField(Object, attributes)>())) return false;
  bool fixed = o.get<ZfField(Object, isStruct)>();
  for (auto f : fields) {
    auto t = type(f);
    if (!schemaType(s, t) || f.get<ZfField(Field, offset64)>() ||
	!attributes(f.get<ZfField(Field, attributes)>())) return false;
    int base = t.get<ZfField(Type, baseType)>();
    int index = t.get<ZfField(Type, index)>();
    unsigned id = f.get<ZfField(Field, id)>();
    if (fixed) {
      if (f.get<ZfField(Field, required)>() || f.get<ZfField(Field, deprecated)>()) return false;
      bool array = base == BaseType::Array;
      if (array) base = t.get<ZfField(Type, element)>();
      if (base == BaseType::Obj) {
	if (!object(s, t).get<ZfField(Object, isStruct)>()) return false;
      } else if (base < BaseType::Bool || base > BaseType::Double) return false;
      auto layout = shape(s, base, index);
      uint64_t size = layout.size;
      if (array) size *= t.get<ZfField(Type, fixedLength)>();
      unsigned at = f.get<ZfField(Field, offset)>();
      unsigned end = o.get<ZfField(Object, bytesize)>();
      if (at > end || size > end - at || (at & (layout.align - 1))) return false;
    } else {
      if (base == BaseType::Array ||
	  f.get<ZfField(Field, offset)>() != Wire::VHeader + id * sizeof(VOffset)) return false;
      if (base >= BaseType::UType && base <= BaseType::Double &&
	  (f.get<ZfField(Field, optional)>() || f.get<ZfField(Field, required)>())) return false;
      if (base == BaseType::Union) {
	if (!id) return false;
	auto tag = field(o, id - 1);
	if (!tag || type(tag).get<ZfField(Type, baseType)>() != BaseType::UType ||
	    type(tag).get<ZfField(Type, index)>() != index) return false;
      }
    }
  }
  return true;
}

bool schemaLayout(SchemaView s)
{
  auto objects = s.get<ZfField(Schema, objects)>();
  auto enums = s.get<ZfField(Schema, enums)>();
  if (!sorted<ZfField(Object, name)>(objects) ||
      !sorted<ZfField(Enum, name)>(enums)) return false;
  // Check inline sizes/alignments before fields refer to objects in any order.
  for (auto o : objects) {
    if (!o.get<ZfField(Object, isStruct)>()) continue;
    int size = o.get<ZfField(Object, bytesize)>();
    int align = o.get<ZfField(Object, minalign)>();
    if (size <= 0 || align <= 0 || align > Wire::MaxAlign ||
	(align & (align - 1)) || (size & (align - 1))) return false;
  }
  for (auto o : objects) if (!schemaObject(s, o)) return false;
  for (auto e : enums) {
    auto t = e.get<ZfField(Enum, underlyingType)>();
    int base = t.get<ZfField(Type, baseType)>();
    if (!schemaType(s, t) || base < BaseType::UType || base > BaseType::ULong ||
	base == BaseType::Bool || !attributes(e.get<ZfField(Enum, attributes)>())) return false;
    bool isUnion = e.get<ZfField(Enum, isUnion)>();
    auto values = e.get<ZfField(Enum, values)>();
    if (isUnion && (base != BaseType::UType || !values.length() ||
	values[0].get<ZfField(EnumVal, value)>() != 0)) return false;
    for (unsigned i = 0, n = values.length(); i < n; ++i) {
      auto val = values[i];
      int64_t v = val.get<ZfField(EnumVal, value)>();
      if (i) {
	int64_t prev = values[i - 1].get<ZfField(EnumVal, value)>();
	if (unsignedType(base) ? uint64_t(prev) >= uint64_t(v) : prev >= v) return false;
      }
      if (!isUnion || !v) continue;
      auto alt = val.get<ZfField(EnumVal, unionType)>();
      if (v < 0 || v > UINT8_MAX || !schemaType(s, alt) ||
	  (alt.get<ZfField(Type, baseType)>() != BaseType::Obj &&
	   alt.get<ZfField(Type, baseType)>() != BaseType::String)) return false;
    }
  }
  auto root = rootObject(s);
  return !root || (!root.get<ZfField(Object, isStruct)>() &&
    object(s, root.get<ZfField(Object, name)>()).data() == root.data());
}

static bool verifyValue(Verifier &v, SchemaView s, int base, int index, unsigned at)
{
  if (base == BaseType::String) return v.string(at);
  if (base == BaseType::Obj)
    return verifyObject(v, s, s.get<ZfField(Schema, objects)>()[index], at);
  auto layout = shape(s, base, index);
  return layout.size && v.range(at, layout.size, layout.align);
}

ZuInline static bool verifyVector(Verifier &v, SchemaView s, TypeView t,
  unsigned at, unsigned align)
{
  int base = t.get<ZfField(Type, element)>();
  int index = base == BaseType::Obj ? t.get<ZfField(Type, index)>() : -1;
  auto layout = shape(s, base, index);
  if (!layout.size) return false;
  if (align < layout.align) align = layout.align;
  unsigned count;
  if (!v.vector(at, layout.size, align, count)) return false;
  ObjectView child;
  switch (base) {
    case BaseType::Obj:
      child = s.get<ZfField(Schema, objects)>()[index];
      if (child.get<ZfField(Object, isStruct)>()) return true;
      break;
    case BaseType::String: break;
    case BaseType::Vector: case BaseType::Union: return false;
    default: return true; // scalar elements are covered by vector bounds
  }
  for (unsigned i = 0; i < count; ++i) {
    unsigned target;
    if (!v.offset_(at + sizeof(Offset) + i * sizeof(Offset), target)) return false;
    if (base == BaseType::String) {
      if (!v.string(target)) return false;
    } else if (!verifyObject(v, s, child, target)) return false;
  }
  return true;
}

static bool verifyUnion(Verifier &v, SchemaView s, FieldView f,
  const TableBounds &bounds, unsigned at)
{
  unsigned tagAt;
  if (!v.field(bounds, f.get<ZfField(Field, id)>() - 1, 1, 1, tagAt)) return false;
  unsigned tag = tagAt ? read<uint8_t>(v.data() + tagAt) : 0;
  unsigned target = 0;
  if (at && !v.offset_(at, target)) return false;
  if (!tag) return !f.get<ZfField(Field, required)>();
  if (!target) return false;
  auto e = s.get<ZfField(Schema, enums)>()[type(f).get<ZfField(Type, index)>()];
  auto alt = enumValue(e, tag);
  if (!alt) return false;
  auto t = alt.get<ZfField(EnumVal, unionType)>();
  return verifyValue(v, s, t.get<ZfField(Type, baseType)>(),
    t.get<ZfField(Type, index)>(), target);
}

static bool verifyTable(Verifier &v, SchemaView s, ObjectView o, unsigned at)
{
  TableBounds bounds;
  if (!v.table(at, bounds)) return false;
  for (auto f : o.get<ZfField(Object, fields)>()) {
    if (f.get<ZfField(Field, deprecated)>()) continue;
    auto t = type(f);
    // The trusted schema declares every field's type as required.
    if (!t) ZuUnreachable();
    int base = t.get<ZfField(Type, baseType)>();
    unsigned p;
    auto field = [&v, &bounds, f, &p]<typename T>() {
      return v.field(bounds, f.get<ZfField(Field, id)>(), sizeof(T), sizeof(T), p);
    };
    // Resolve object metadata only for object fields; all offsets and
    // scalar widths are fixed by the wire type.
    ObjectView child;
    using namespace BaseType;
    switch (base) {
      case UType: case Bool: case Byte: case UByte:
	if (!field.template operator()<uint8_t>()) return false;
	continue;
      case Short: case UShort:
	if (!field.template operator()<uint16_t>()) return false;
	continue;
      case Int: case UInt: case Float:
	if (!field.template operator()<uint32_t>()) return false;
	continue;
      case Long: case ULong: case Double:
	if (!field.template operator()<uint64_t>()) return false;
	continue;
      case Obj:
	child = object(s, t);
	if (child.get<ZfField(Object, isStruct)>()) {
	  if (!v.field(bounds, f.get<ZfField(Field, id)>(),
	      child.get<ZfField(Object, bytesize)>(),
	      child.get<ZfField(Object, minalign)>(), p)) return false;
	  if (!p && f.get<ZfField(Field, required)>()) return false;
	  continue;
	}
	[[fallthrough]];
      case BaseType::String: case BaseType::Vector: case BaseType::Union:
	if (!field.template operator()<Offset>()) return false;
	break;
      default: return false;
    }
    if (base == BaseType::Union) {
      if (!verifyUnion(v, s, f, bounds, p)) return false;
      continue;
    }
    if (!p) {
      if (f.get<ZfField(Field, required)>()) return false;
      continue;
    }
    unsigned target;
    if (!v.offset_(p, target)) return false;
    switch (base) {
      case BaseType::String:
	if (!v.string(target)) return false;
	break;
      case BaseType::Vector:
	if (!verifyVector(v, s, t, target, alignment(f))) return false;
	break;
      case Obj:
	if (!verifyObject(v, s, child, target)) return false;
	break;
    }
  }
  return true;
}

bool verifyObject(Verifier &v, SchemaView s, ObjectView o, unsigned at)
{
  // Schema verification or local generation established these metadata views.
  if (!s || !o) ZuUnreachable();
  if (o.get<ZfField(Object, isStruct)>())
    return v.range(at, o.get<ZfField(Object, bytesize)>(), o.get<ZfField(Object, minalign)>());
  if (!v.enter()) return false;
  bool ok = verifyTable(v, s, o, at);
  v.leave();
  return ok;
}

} // Zfb::Reflect
