//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuBox.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZiPBReflect.hh>

namespace ZiPB::Reflect {

ZtEnumImplNS(Type);

ZfStructImpl(FieldDesc);
ZfStructImpl(FieldDesc, PB);
ZfStructImpl(EnumValueDesc);
ZfStructImpl(EnumValueDesc, PB);
ZfStructImpl(EnumDesc);
ZfStructImpl(EnumDesc, PB);
ZfStructImpl(OneofDesc);
ZfStructImpl(OneofDesc, PB);
ZfStructImpl(MsgOpts);
ZfStructImpl(MsgOpts, PB);
ZfStructImpl(MsgDesc);
ZfStructImpl(MsgDesc, PB);
ZfStructImpl(FileDesc);
ZfStructImpl(FileDesc, PB);
ZfStructImpl(DescSet);
ZfStructImpl(DescSet, PB);

struct WireKey {
  const MsgDesc	*message;
  uint32_t	number;

  uint32_t hash() const {
    return ZuHash<const MsgDesc *>::hash(message) ^
      ZuHash<uint32_t>::hash(number);
  }
};
static bool operator ==(const WireKey &l, const WireKey &r)
  { return l.message == r.message && l.number == r.number; }
static const char *wireIndexID() { return "ZiPB.Reflect.WireIndex"; }
using WireIndex = ZmLHashKV<WireKey, unsigned,
  ZmLHashID<wireIndexID, ZmLHashLocal<>>>;
enum { WireIndexThreshold = 48 };

template <typename Heap = ZuVoid>
struct SchemaIndex_ : Heap {
  WireIndex fields;
};
struct Schema::Index :
  SchemaIndex_<ZmHeap<"ZiPB.Reflect.Index", SchemaIndex_<>>> { };

Schema::Schema(ZuBSpan bytes, bool prepareFields) :
  m_bytes{bytes.data(), bytes.length()},
  m_desc{ZiPB::load<DescSet>({m_bytes.data(), m_bytes.length()})}
{
  if (!prepareFields) return;
  auto prepare = [&](auto &&self, const MsgDesc &message) -> void {
    if (message.fields.length() >= WireIndexThreshold) {
      if (!m_index) m_index = ZuPtr<Index>{new Index};
      for (unsigned i = 0, n = message.fields.length(); i < n; ++i) {
	WireKey key{&message, uint32_t(message.fields[i].number)};
	if (!m_index->fields.find(key)) m_index->fields.add(key, i);
      }
      m_index->fields.add(WireKey{&message, 0}, UINT_MAX);
    }
    for (const auto &nested : message.nested) self(self, *nested.ptr());
  };
  for (const auto &file : m_desc.files)
    for (const auto &message : file.messages) prepare(prepare, message);
}

Schema::~Schema() = default;

const FileDesc *Schema::file(ZuCSpan name) const
{
  for (const auto &file : m_desc.files)
    if (file.name == name) return &file;
  return nullptr;
}

const FileDesc *Schema::file(ZuCSpan name, Syntax::T version) const
{
  if (!syntaxValid(version)) return nullptr;
  auto selected = file(name);
  return selected && fileSyntax(*selected) == version ? selected : nullptr;
}

const MsgDesc *Schema::message(const FileDesc &file, ZuCSpan name) const
{
  for (const auto &message : file.messages)
    if (message.name == name) return &message;
  return nullptr;
}

static const MsgDesc *nestedMessage(const MsgDesc &message, ZuCSpan path)
{
  unsigned n = message.name.length();
  if (path.length() < n || memcmp(path.data(), message.name.data(), n))
    return nullptr;
  if (path.length() == n) return &message;
  if (path[n] != '.') return nullptr;
  ZuCSpan child{path.data() + n + 1, path.length() - n - 1};
  for (const auto &nested : message.nested)
    if (auto found = nestedMessage(*nested.ptr(), child)) return found;
  return nullptr;
}

const MsgDesc *Schema::message(ZuCSpan qualified) const
{
  if (!qualified.length() || qualified[0] != '.') return nullptr;
  for (const auto &file : m_desc.files) {
    unsigned pos = 1;
    if (file.package.length()) {
      if (qualified.length() <= pos + file.package.length() ||
	memcmp(qualified.data() + pos, file.package.data(),
	  file.package.length()) || qualified[pos + file.package.length()] != '.')
	continue;
      pos += file.package.length() + 1;
    }
    ZuCSpan path{qualified.data() + pos, qualified.length() - pos};
    for (const auto &message : file.messages)
      if (auto found = nestedMessage(message, path)) return found;
  }
  return nullptr;
}

static const EnumDesc *nestedEnumeration(const MsgDesc &message, ZuCSpan path)
{
  unsigned n = message.name.length();
  if (path.length() <= n || memcmp(path.data(), message.name.data(), n) ||
    path[n] != '.') return nullptr;
  ZuCSpan child{path.data() + n + 1, path.length() - n - 1};
  for (const auto &enumeration : message.enums)
    if (enumeration.name == child) return &enumeration;
  for (const auto &nested : message.nested)
    if (auto found = nestedEnumeration(*nested.ptr(), child)) return found;
  return nullptr;
}

const EnumDesc *Schema::enumeration(ZuCSpan qualified) const
{
  if (!qualified.length() || qualified[0] != '.') return nullptr;
  for (const auto &file : m_desc.files) {
    unsigned pos = 1;
    if (file.package.length()) {
      if (qualified.length() <= pos + file.package.length() ||
	memcmp(qualified.data() + pos, file.package.data(),
	  file.package.length()) || qualified[pos + file.package.length()] != '.')
	continue;
      pos += file.package.length() + 1;
    }
    ZuCSpan path{qualified.data() + pos, qualified.length() - pos};
    for (const auto &enumeration : file.enums)
      if (enumeration.name == path) return &enumeration;
    for (const auto &message : file.messages)
      if (auto found = nestedEnumeration(message, path)) return found;
  }
  return nullptr;
}

const FieldDesc *Schema::field(const MsgDesc &message, unsigned number) const
{
  if (number && m_index &&
      message.fields.length() >= WireIndexThreshold) {
    if (auto found = m_index->fields.find(
	WireKey{&message, uint32_t(number)}))
      return &message.fields[found->p<1>()];
    if (m_index->fields.find(WireKey{&message, 0})) return nullptr;
  }
  for (const auto &field : message.fields)
    if (field.number == number) return &field;
  return nullptr;
}

const FieldDesc *Schema::field(const MsgDesc &message, ZuCSpan name) const
{
  for (const auto &field : message.fields)
    if (field.name == name) return &field;
  return nullptr;
}

ZtEnumNS(, DescKind, int8_t,
  SetKind, FileKind, MsgKind, FieldKind, EnumKind, EnumValueKind,
  OneofKind, MsgOptsKind, FileOptsKind, FieldOptsKind, ReservedKind);
ZtEnumImplNS(DescKind);

static bool unsupported(DescKind::T kind, uint32_t field, ZuCSpan *reason)
{
  if (!reason) return false;
  switch (kind) {
    case DescKind::FileKind:
      switch (field) {
	case 6: *reason = "unsupported service descriptor"; break;
	case 7: *reason = "unsupported extension descriptor"; break;
	case 8: *reason = "unsupported file options"; break;
	case 10: case 11:
	  *reason = "unsupported import qualifier"; break;
	case 14: *reason = "unsupported edition descriptor"; break;
      }
      break;
    case DescKind::MsgKind:
      if (field == 5 || field == 6)
	*reason = "unsupported extension descriptor";
      break;
    case DescKind::FieldKind:
      if (field == 8) *reason = "unsupported field options";
      break;
    case DescKind::FieldOptsKind:
      *reason = "unsupported field options";
      break;
    case DescKind::EnumKind:
      if (field == 3) *reason = "unsupported enum options";
      break;
    case DescKind::EnumValueKind:
      if (field == 3) *reason = "unsupported enum value options";
      break;
    case DescKind::OneofKind:
      if (field == 2) *reason = "unsupported oneof options";
      break;
    case DescKind::MsgOptsKind:
      if (field != 7) *reason = "unsupported message options";
      break;
    case DescKind::FileOptsKind:
      *reason = "unsupported file options";
      break;
    default: break;
  }
  return false;
}

// The typed descriptor codec skips unknown fields. The schema verifier also
// rejects semantic constructs this reflection slice cannot represent yet.
static bool supported(ZuBSpan bytes, DescKind::T kind, ZuCSpan *reason)
{
  unsigned pos = 0;
  while (pos < bytes.length()) {
    uint32_t field;
    Wire::T wire;
    if (!Wire::readTag(bytes, pos, field, wire)) return false;
    DescKind::T child = kind;
    bool nested = false;
    switch (kind) {
      case DescKind::SetKind:
	if (field != 1 || wire != Wire::Length)
	  return unsupported(kind, field, reason);
	child = DescKind::FileKind;
	nested = true;
	break;
      case DescKind::FileKind:
	if (field == 4 && wire == Wire::Length) {
	  child = DescKind::MsgKind;
	  nested = true;
	} else if (field == 5 && wire == Wire::Length) {
	  child = DescKind::EnumKind;
	  nested = true;
	} else if (field == 8 && wire == Wire::Length) {
	  child = DescKind::FileOptsKind;
	  nested = true;
	} else if (wire != Wire::Length ||
	  (field != 1 && field != 2 && field != 3 &&
	    field != 9 && field != 12))
	  return unsupported(kind, field, reason);
	break;
      case DescKind::MsgKind:
	if (field == 2 && wire == Wire::Length) {
	  child = DescKind::FieldKind;
	  nested = true;
	} else if (field == 3 && wire == Wire::Length) {
	  child = DescKind::MsgKind;
	  nested = true;
	} else if (field == 4 && wire == Wire::Length) {
	  child = DescKind::EnumKind;
	  nested = true;
	} else if (field == 7 && wire == Wire::Length) {
	  child = DescKind::MsgOptsKind;
	  nested = true;
	} else if (field == 8 && wire == Wire::Length) {
	  child = DescKind::OneofKind;
	  nested = true;
	} else if (field == 9 && wire == Wire::Length) {
	  child = DescKind::ReservedKind;
	  nested = true;
	} else if (field == 10 && wire == Wire::Length) {
	  break;
	} else if (field != 1 || wire != Wire::Length)
	  return unsupported(kind, field, reason);
	break;
      case DescKind::ReservedKind:
	if ((field != 1 && field != 2) || wire != Wire::Varint)
	  return unsupported(kind, field, reason);
	break;
      case DescKind::OneofKind:
	if (field != 1 || wire != Wire::Length)
	  return unsupported(kind, field, reason);
	break;
      case DescKind::MsgOptsKind:
	if (field != 7 || wire != Wire::Varint)
	  return unsupported(kind, field, reason);
	break;
      case DescKind::FileOptsKind:
	switch (field) {
	  case 1: case 8: case 11: case 36: case 37:
	  case 39: case 40: case 41: case 44: case 45:
	    if (wire != Wire::Length)
	      return unsupported(kind, field, reason);
	    break;
	  case 9: case 10: case 16: case 17: case 18:
	  case 20: case 23: case 27: case 31:
	    if (wire != Wire::Varint)
	      return unsupported(kind, field, reason);
	    break;
	  default:
	    return unsupported(kind, field, reason);
	}
	break;
      case DescKind::EnumKind:
	if (field == 2 && wire == Wire::Length) {
	  child = DescKind::EnumValueKind;
	  nested = true;
	} else if (field != 1 || wire != Wire::Length)
	  return unsupported(kind, field, reason);
	break;
      case DescKind::EnumValueKind:
	if ((field == 1 && wire != Wire::Length) ||
	  (field == 2 && wire != Wire::Varint) ||
	  (field != 1 && field != 2))
	  return unsupported(kind, field, reason);
	break;
      case DescKind::FieldKind:
	if (field == 8 && wire == Wire::Length) {
	  child = DescKind::FieldOptsKind;
	  nested = true;
	} else if (field == 1 || field == 6 || field == 7 || field == 10) {
	  if (wire != Wire::Length) return false;
	} else if (field == 3 || field == 4 || field == 5 || field == 9 ||
	  field == 17) {
	  if (wire != Wire::Varint) return false;
	} else return unsupported(kind, field, reason);
	break;
      case DescKind::FieldOptsKind:
	if (field != 2 || wire != Wire::Varint)
	  return unsupported(kind, field, reason);
	break;
    }
    if (wire == Wire::Varint) {
      uint64_t value;
      if (!Wire::readVarint(bytes, pos, value)) return false;
    } else {
      ZuBSpan value;
      if (!Wire::readLength(bytes, pos, value)) return false;
      if (nested && !supported(value, child, reason)) return false;
    }
  }
  return true;
}

static bool contains(const MsgDesc &message, const MsgDesc *target)
{
  if (&message == target) return true;
  for (const auto &nested : message.nested)
    if (contains(*nested.ptr(), target)) return true;
  return false;
}

static bool contains(const MsgDesc &message, const EnumDesc *target)
{
  for (const auto &enumeration : message.enums)
    if (&enumeration == target) return true;
  for (const auto &nested : message.nested)
    if (contains(*nested.ptr(), target)) return true;
  return false;
}

static bool visible(const FileDesc &file, const FileDesc &source)
{
  if (&file == &source) return true;
  for (const auto &dependency : file.dependencies)
    if (dependency == source.name) return true;
  return false;
}

using NameIndex = ZmRBTree<ZuCSpan,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"ZiPB.Reflect.NameIndex">>>;
using NumberIndex = ZmRBTree<int32_t,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"ZiPB.Reflect.NumberIndex">>>;
using TypeKey = ZuTuple<ZuCSpan, ZuCSpan>;
using TypeIndex = ZmRBTree<TypeKey,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"ZiPB.Reflect.TypeIndex">>>;

static bool validEnum(const EnumDesc &enumeration, ZuCSpan syntax)
{
  if (!enumeration.name.length() || !enumeration.values.length()) return false;
  if (syntax == "proto3" && enumeration.values[0].number) return false;
  NameIndex names;
  NumberIndex numbers;
  for (unsigned i = 0, n = enumeration.values.length(); i < n; ++i) {
    const auto &value = enumeration.values[i];
    if (!value.name.length() || names.find(value.name) ||
	numbers.find(value.number)) return false;
    names.add(value.name);
    numbers.add(value.number);
  }
  return true;
}

template <typename Values, typename Name>
static bool uniqueNames(const Values &values, Name nameOf)
{
  NameIndex names;
  for (unsigned i = 0, n = values.length(); i < n; ++i) {
    ZuCSpan name = nameOf(values[i]);
    if (!name.length() || names.find(name)) return false;
    names.add(name);
  }
  return true;
}

static bool mapKey(int type)
{
  switch (type) {
    case Type::Int32: case Type::Int64: case Type::UInt32: case Type::UInt64:
    case Type::SInt32: case Type::SInt64:
    case Type::Fixed32: case Type::Fixed64:
    case Type::SFixed32: case Type::SFixed64:
    case Type::Bool: case Type::String: return true;
    default: return false;
  }
}

static bool packable(int type)
{
  switch (type) {
    case Type::Double: case Type::Float:
    case Type::Int64: case Type::UInt64: case Type::Int32:
    case Type::Fixed64: case Type::Fixed32: case Type::Bool:
    case Type::UInt32: case Type::Enum:
    case Type::SFixed32: case Type::SFixed64:
    case Type::SInt32: case Type::SInt64: return true;
    default: return false;
  }
}

static bool validMessage(const Schema &schema, const FileDesc &file,
  const MsgDesc &message, ZuCSpan *reason)
{
  if (!message.name.length()) return false;
  const auto &files = schema.descriptor().files;
  const auto &fields = message.fields;
  for (unsigned i = 0, n = message.reservedRanges.length(); i < n; ++i) {
    const auto &range = message.reservedRanges[i];
    if (!Wire::fieldNumber(range.start) || range.end <= range.start ||
      !Wire::fieldNumber(range.end - 1)) return false;
    for (unsigned j = 0; j < i; ++j)
      if (range.start < message.reservedRanges[j].end &&
	message.reservedRanges[j].start < range.end) return false;
    for (const auto &field : fields)
      if (field.number >= range.start && field.number < range.end)
	return false;
  }
  if (!uniqueNames(message.reservedNames,
      [](ZuCSpan name) { return name; })) return false;
  if (!uniqueNames(message.oneofs,
      [](const OneofDesc &oneof) { return oneof.name; })) return false;
  bool synthetic = false;
  auto checkOneof = [&synthetic](unsigned members, unsigned optional) {
    if (optional) {
      if (members != 1 || optional != 1) return false;
      synthetic = true;
    } else if (synthetic) return false;
    return true;
  };
  unsigned oneofN = message.oneofs.length();
  if (oneofN >= 4) {
    if (oneofN > UINT_MAX / 2) return false;
    using Counts = ZtArray<unsigned, ZtArrayHeapID<"ZiPB.Reflect.OneofCounts">>;
    auto counts = ZtScratch(Counts, oneofN * 2, oneofN * 2);
    memset(counts.data(), 0, oneofN * 2 * sizeof(unsigned));
    auto members = counts.data(), optional = members + oneofN;
    for (const auto &field : fields) {
      int32_t i = field.oneofIndex;
      if (i >= 0 && unsigned(i) < oneofN) {
	++members[i];
	optional[i] += field.proto3Optional;
      }
    }
    for (unsigned i = 0; i < oneofN; ++i)
      if (!checkOneof(members[i], optional[i])) return false;
  } else for (unsigned i = 0; i < oneofN; ++i) {
    unsigned members = 0, optional = 0;
    for (const auto &field : fields)
      if (field.oneofIndex == int32_t(i)) {
	++members;
	optional += field.proto3Optional;
      }
    if (!checkOneof(members, optional)) return false;
  }
  NameIndex names;
  NumberIndex numbers;
  for (unsigned i = 0, n = fields.length(); i < n; ++i) {
    const auto &field = fields[i];
    if (field.type == Type::Group) {
      if (reason) *reason = "unsupported group field";
      return false;
    }
    if (!field.name.length() || field.number <= 0 ||
      !Wire::fieldNumber(field.number) || field.label < 1 ||
      field.label > 3 || field.type < 1 || field.type > 18 ||
      (file.syntax == "proto3" && field.label == 2)) return false;
    if (field.oneofIndex != ZuCmp<int32_t>::null() &&
      (field.oneofIndex < 0 ||
       unsigned(field.oneofIndex) >= message.oneofs.length() ||
       field.label != 1)) return false;
    if (field.proto3Optional &&
      (file.syntax != "proto3" || field.label != 1 ||
       field.oneofIndex == ZuCmp<int32_t>::null())) return false;
    if (field.defaultValue.data() &&
      ((file.syntax.length() && file.syntax != "proto2") ||
       field.label == 3 ||
       field.oneofIndex != ZuCmp<int32_t>::null() ||
       field.type == Type::Message || field.type == Type::Group))
      return false;
    if (field.options.data()) {
      int packed = packedOption(field);
      if (packed < 0 && field.options.length()) return false;
      if (packed >= 0 &&
	(field.label != 3 || !packable(field.type))) return false;
    }
    if (field.type == Type::Message) {
      auto target = schema.message(field.typeName);
      if (!target) return false;
      bool found = false;
      for (const auto &source : files) {
	for (const auto &candidate : source.messages)
	  if (contains(candidate, target)) {
	    found = visible(file, source);
	    break;
	  }
	if (found) break;
      }
      if (!found) return false;
    } else if (field.type == Type::Enum) {
      auto target = schema.enumeration(field.typeName);
      if (!target) return false;
      bool found = false;
      for (const auto &source : files) {
	bool matched = false;
	for (const auto &candidate : source.enums)
	  if (&candidate == target) matched = true;
	for (const auto &candidate : source.messages)
	  if (contains(candidate, target)) matched = true;
	if (!matched) continue;
	if (file.syntax == "proto3" && source.syntax != "proto3") {
	  if (reason) *reason = "proto2 enum in proto3 field";
	  return false;
	}
	found = visible(file, source);
	if (found) break;
      }
      if (!found) return false;
    } else if (field.typeName.length()) return false;
    if (names.find(field.name) || numbers.find(field.number)) return false;
    names.add(field.name);
    numbers.add(field.number);
  }
  for (ZuCSpan name : message.reservedNames)
    if (names.find(name)) return false;
  for (const auto &oneof : message.oneofs)
    if (names.find(oneof.name)) return false;
  if (message.options && message.options->mapEntry) {
    const FieldDesc *key = nullptr, *value = nullptr;
    for (const auto &field : fields) {
      if (field.number == 1) key = &field;
      else if (field.number == 2) value = &field;
    }
    if (!key || !value || !mapKey(key->type) ||
      key->label != 1 || value->label != 1) return false;
  }
  NameIndex typeNames;
  auto duplicateType = [&typeNames](ZuCSpan name) {
    if (typeNames.find(name)) return true;
    typeNames.add(name);
    return false;
  };
  for (unsigned i = 0, n = message.nested.length(); i < n; ++i) {
    const auto &nested = *message.nested[i].ptr();
    if (duplicateType(nested.name)) return false;
    if (!validMessage(schema, file, nested, reason)) return false;
  }
  for (unsigned i = 0, n = message.enums.length(); i < n; ++i) {
    const auto &enumeration = message.enums[i];
    if (!validEnum(enumeration, file.syntax)) return false;
    if (duplicateType(enumeration.name)) return false;
  }
  return true;
}

bool verifySchema(ZuBSpan bytes, VerifyLimits limits, ZuCSpan *reason)
{
  if (reason) *reason = {};
  if (!ZiPB::verify<DescSet>(bytes, limits)) return false;
  if (!supported(bytes, DescKind::SetKind, reason)) return false;
  Schema schema{bytes, false};
  const auto &files = schema.descriptor().files;
  NameIndex fileNames;
  for (unsigned i = 0, n = files.length(); i < n; ++i) {
    const auto &name = files[i].name;
    if (!name.length() || fileNames.find(name)) return false;
    fileNames.add(name);
  }
  TypeIndex types;
  for (unsigned fi = 0, fn = files.length(); fi < fn; ++fi) {
    const auto &file = files[fi];
    if (!file.name.length() ||
      (file.syntax.length() && file.syntax != "proto2" &&
	file.syntax != "proto3")) return false;
    for (const auto &dependency : file.dependencies)
      if (!fileNames.find(dependency)) return false;
    for (const auto &enumeration : file.enums) {
      if (!validEnum(enumeration, file.syntax)) return false;
      TypeKey key{file.package, enumeration.name};
      if (types.find(key)) return false;
      types.add(key);
    }
    for (const auto &message : file.messages) {
      if (!validMessage(schema, file, message, reason)) return false;
      TypeKey key{file.package, message.name};
      if (types.find(key)) return false;
      types.add(key);
    }
  }
  return true;
}

static bool wireType(int type, Wire::T &wire)
{
  switch (type) {
    case Type::Double: case Type::Fixed64: case Type::SFixed64:
      wire = Wire::Fixed64;
      return true;
    case Type::Float: case Type::Fixed32: case Type::SFixed32:
      wire = Wire::Fixed32;
      return true;
    case Type::Int64: case Type::UInt64: case Type::Int32: case Type::Enum:
    case Type::Bool: case Type::UInt32: case Type::SInt32: case Type::SInt64:
      wire = Wire::Varint;
      return true;
    case Type::String: case Type::Bytes: case Type::Message:
      wire = Wire::Length;
      return true;
    default: return false;
  }
}

static bool readValue(ZuBSpan bytes, unsigned &pos, const FieldDesc &field,
  Wire::T wire, bool strict, Value &value)
{
  value.field = &field;
  switch (wire) {
    case Wire::Varint: {
      uint64_t raw;
      if (!Wire::readVarint(bytes, pos, raw)) return false;
      switch (field.type) {
	case Type::Int32: case Type::Enum: {
	  int32_t n = int32_t(raw);
	  if (strict && uint64_t(int64_t(n)) != raw) return false;
	  value.integer = uint64_t(int64_t(n));
	  break;
	}
	case Type::UInt32:
	  if (strict && raw > UINT32_MAX) return false;
	  value.integer = uint32_t(raw);
	  break;
	case Type::SInt32: {
	  int64_t n = int64_t((raw >> 1) ^ uint64_t(-int64_t(raw & 1)));
	  if (strict && int64_t(int32_t(n)) != n) return false;
	  value.integer = uint64_t(int64_t(int32_t(n)));
	  break;
	}
	case Type::SInt64:
	  value.integer = (raw >> 1) ^ uint64_t(-int64_t(raw & 1));
	  break;
	case Type::Bool:
	  value.integer = bool(raw);
	  break;
	default:
	  value.integer = raw;
	  break;
      }
      return true;
    }
    case Wire::Fixed32: {
      if (bytes.length() - pos < 4) return false;
      ZuLittleEndian<uint32_t> word;
      memcpy(&word, bytes.data() + pos, 4);
      pos += 4;
      uint32_t bits = word;
      if (field.type == Type::Float) {
	float n;
	memcpy(&n, &bits, sizeof(n));
	value.real = n;
      } else if (field.type == Type::SFixed32)
	value.integer = uint64_t(int64_t(int32_t(bits)));
      else value.integer = bits;
      return true;
    }
    case Wire::Fixed64: {
      if (bytes.length() - pos < 8) return false;
      ZuLittleEndian<uint64_t> word;
      memcpy(&word, bytes.data() + pos, 8);
      pos += 8;
      uint64_t bits = word;
      if (field.type == Type::Double)
	memcpy(&value.real, &bits, sizeof(bits));
      else value.integer = bits;
      return true;
    }
    case Wire::Length:
      return Wire::readLength(bytes, pos, value.bytes);
    default: return false;
  }
}

using Seen = ZtArray<uint8_t, ZtArrayHeapID<"ZiPB.Reflect.Seen">>;

static bool scan(const Schema *schema, const MsgDesc &message,
  ZuBSpan bytes, bool strict,
  unsigned maxDepth, uint8_t *seen, void *ctx, Visitor visitor)
{
  unsigned pos = 0;
  bool indexed = schema &&
    message.fields.length() >= WireIndexThreshold;
  while (pos < bytes.length()) {
    uint32_t number;
    Wire::T wire;
    if (!Wire::readTag(bytes, pos, number, wire)) return false;
    const FieldDesc *field = nullptr;
    unsigned index = 0;
    if (indexed) {
      field = schema->field(message, number);
      if (field) index = field - message.fields.data();
    } else
      for (unsigned n = message.fields.length(); index < n; ++index)
	if (message.fields[index].number == number) {
	  field = &message.fields[index];
	  break;
	}
    if (!field) {
      if (!Wire::skip(bytes, pos, number, wire, maxDepth)) return false;
      continue;
    }
    Wire::T expected;
    if (!wireType(field->type, expected)) return false;
    if (seen) seen[index] = 1;
    if (field->label == 3 && wire == Wire::Length && expected != Wire::Length) {
      ZuBSpan packed;
      if (!Wire::readLength(bytes, pos, packed)) return false;
      unsigned offset = 0;
      while (offset < packed.length()) {
	Value value{};
	if (!readValue(packed, offset, *field, expected, strict, value) ||
	  (visitor && !visitor(ctx, value))) return false;
      }
    } else {
      if (wire != expected) return false;
      Value value{};
      if (!readValue(bytes, pos, *field, wire, strict, value) ||
	(visitor && !visitor(ctx, value))) return false;
    }
  }
  return true;
}

static bool required(const MsgDesc &message, const uint8_t *seen)
{
  for (unsigned i = 0, n = message.fields.length(); i < n; ++i)
    if (message.fields[i].label == 2 && !seen[i]) return false;
  return true;
}

bool verify(const MsgDesc &message, ZuBSpan bytes, VerifyLimits limits)
{
  unsigned n = message.fields.length();
  auto seen = ZtScratch(Seen, n ? n : 1, n ? n : 1);
  if (n) memset(seen.data(), 0, n);
  return scan(nullptr, message, bytes, true, limits.maxDepth, seen.data(), nullptr,
    [](void *, const Value &value) {
      return value.field->type != Type::Message;
    }) && required(message, seen.data());
}

struct Part { ZuBSpan bytes; unsigned next; };
using Parts = ZtArray<Part, ZtArrayHeapID<"ZiPB.Reflect.Parts">>;
using Indices = ZtArray<unsigned, ZtArrayHeapID<"ZiPB.Reflect.Indices">>;

static bool validWireParts(const Schema &schema, const MsgDesc &message,
  const Part *parts, unsigned first, unsigned depth)
{
  struct State { const Schema &schema; unsigned depth; } state{schema, depth};
  auto validate = [](void *context, const Value &value) {
    if (value.field->type != Type::Message) return true;
    auto &state = *static_cast<State *>(context);
    if (!state.depth) return false;
    auto child = state.schema.message(value.field->typeName);
    if (!child) return false;
    Part part{value.bytes, UINT_MAX};
    return validWireParts(state.schema, *child, &part, 0, state.depth - 1);
  };
  for (unsigned part = first; part != UINT_MAX; part = parts[part].next)
    if (!scan(&schema, message, parts[part].bytes, true, depth, nullptr,
      &state, validate)) return false;
  return true;
}

static bool verifyParts(const Schema &schema, const MsgDesc &message,
  const Part *parts, unsigned first, unsigned depth)
{
  unsigned n = message.fields.length();
  auto seen = ZtScratch(Seen, n ? n : 1, n ? n : 1);
  auto head = ZtScratch(Indices, n ? n : 1, n ? n : 1);
  auto tail = ZtScratch(Indices, n ? n : 1, n ? n : 1);
  unsigned m = message.oneofs.length();
  auto active = ZtScratch(Indices, m ? m : 1, m ? m : 1);
  if (n) memset(seen.data(), 0, n);
  for (unsigned i = 0; i < n; ++i) head[i] = tail[i] = UINT_MAX;
  for (unsigned i = 0; i < m; ++i) active[i] = UINT_MAX;
  Parts children;
  struct State {
    const Schema &schema;
    const MsgDesc &message;
    Parts &children;
    unsigned *head, *tail, *active;
    unsigned depth;
  } state{schema, message, children, head.data(), tail.data(),
    active.data(), depth};
  auto collect = [](void *context, const Value &value) {
    auto &state = *static_cast<State *>(context);
    unsigned index = value.field - state.message.fields.data();
    if (value.field->oneofIndex != ZuCmp<int32_t>::null()) {
      unsigned &active = state.active[value.field->oneofIndex];
      if (active != index) {
	if (active != UINT_MAX) {
	  if (state.head[active] != UINT_MAX) {
	    const auto &field = state.message.fields[active];
	    auto child = state.schema.message(field.typeName);
	    if (!child || !state.depth ||
	      !validWireParts(state.schema, *child, state.children.data(),
		state.head[active], state.depth - 1)) return false;
	  }
	  state.head[active] = state.tail[active] = UINT_MAX;
	}
	active = index;
      }
    }
    if (value.field->type != Type::Message) return true;
    unsigned next = state.children.length();
    state.children.push(Part{value.bytes, UINT_MAX});
    if (state.head[index] == UINT_MAX) state.head[index] = next;
    else state.children[state.tail[index]].next = next;
    state.tail[index] = next;
    return true;
  };
  for (unsigned part = first; part != UINT_MAX; part = parts[part].next)
    if (!scan(&schema, message, parts[part].bytes, true, depth, seen.data(),
      &state, collect)) return false;
  if (!required(message, seen.data())) return false;
  for (unsigned i = 0; i < n; ++i) {
    if (head[i] == UINT_MAX) continue;
    if (!depth) return false;
    const auto &field = message.fields[i];
    auto child = schema.message(field.typeName);
    if (!child) return false;
    if (field.label == 3) {
      for (unsigned part = head[i]; part != UINT_MAX;
	part = children[part].next) {
	Part one{children[part].bytes, UINT_MAX};
	if (!verifyParts(schema, *child, &one, 0, depth - 1)) return false;
      }
    } else if (!verifyParts(schema, *child, children.data(), head[i], depth - 1))
      return false;
  }
  return true;
}

bool verify(const Schema &schema, const MsgDesc &message, ZuBSpan bytes,
  VerifyLimits limits)
{
  Part root{bytes, UINT_MAX};
  return verifyParts(schema, message, &root, 0, limits.maxDepth);
}

bool visit(const MsgDesc &message, ZuBSpan bytes, void *ctx, Visitor visitor)
{
  return scan(nullptr, message, bytes, false, UINT_MAX, nullptr, ctx, visitor);
}

bool visit(const Schema &schema, const MsgDesc &message, ZuBSpan bytes,
  void *ctx, Visitor visitor)
{
  return scan(&schema, message, bytes, false, UINT_MAX, nullptr, ctx, visitor);
}

static void indent(ProtoText &out, unsigned depth)
{
  for (unsigned i = 0; i < depth; ++i) out << "  ";
}

static ZuCSpan scalarName(int type)
{
  switch (type) {
    case Type::Double: return "double";
    case Type::Float: return "float";
    case Type::Int64: return "int64";
    case Type::UInt64: return "uint64";
    case Type::Int32: return "int32";
    case Type::Fixed64: return "fixed64";
    case Type::Fixed32: return "fixed32";
    case Type::Bool: return "bool";
    case Type::String: return "string";
    case Type::Bytes: return "bytes";
    case Type::UInt32: return "uint32";
    case Type::SFixed32: return "sfixed32";
    case Type::SFixed64: return "sfixed64";
    case Type::SInt32: return "sint32";
    case Type::SInt64: return "sint64";
    default: return {};
  }
}

static bool emitType(ProtoText &out, const FieldDesc &field)
{
  if (field.type == Type::Message || field.type == Type::Enum) {
    if (!field.typeName.length()) return false;
    out << field.typeName;
    return true;
  }
  auto name = scalarName(field.type);
  if (!name.length()) return false;
  out << name;
  return true;
}

static bool emitField(ProtoText &out, const Schema &schema,
  const FieldDesc &field, ZuCSpan syntax, unsigned depth, bool oneof)
{
  indent(out, depth);
  const MsgDesc *entry = nullptr;
  if (field.label == 3 && field.type == Type::Message)
    entry = schema.message(field.typeName);
  if (entry && entry->options && entry->options->mapEntry) {
    auto key = schema.field(*entry, 1);
    auto value = schema.field(*entry, 2);
    if (!key || !value) return false;
    out << "map<";
    if (!emitType(out, *key)) return false;
    out << ", ";
    if (!emitType(out, *value)) return false;
    out << ">";
  } else {
    if (!oneof) {
      if (field.label == 3) out << "repeated ";
      else if (field.proto3Optional) out << "optional ";
      else if (syntax != "proto3")
	out << (field.label == 2 ? "required " : "optional ");
    }
    if (!emitType(out, field)) return false;
  }
  out << ' ' << field.name << " = " << ZuBox<int32_t>{field.number};
  if (field.defaultValue.data()) {
    out << " [default = ";
    if (field.type == Type::String) emitQuoted(out, field.defaultValue);
    else if (field.type == Type::Bytes)
      out << '"' << field.defaultValue << '"';
    else out << field.defaultValue;
    out << ']';
  }
  if (int packed = packedOption(field); packed >= 0)
    out << " [packed = " <<
      (packed ? "true" : "false") << ']';
  out << ";\n";
  return true;
}

static void emitEnum(ProtoText &out, const EnumDesc &enumeration,
  unsigned depth)
{
  indent(out, depth);
  out << "enum " << enumeration.name << " {\n";
  for (const auto &value : enumeration.values) {
    indent(out, depth + 1);
    out << value.name << " = " << ZuBox<int32_t>{value.number} << ";\n";
  }
  indent(out, depth);
  out << "}\n";
}

static bool emitMessage(ProtoText &out, const Schema &schema,
  const MsgDesc &message, ZuCSpan syntax, unsigned depth)
{
  indent(out, depth);
  out << "message " << message.name << " {\n";
  for (const auto &enumeration : message.enums)
    emitEnum(out, enumeration, depth + 1);
  for (const auto &nested : message.nested)
    if (!nested->options || !nested->options->mapEntry)
      if (!emitMessage(out, schema, *nested.ptr(), syntax, depth + 1))
	return false;
  for (const auto &range : message.reservedRanges) {
    indent(out, depth + 1);
    out << "reserved " << ZuBox<int32_t>{range.start};
    if (range.end != range.start + 1)
      out << " to " << ZuBox<int32_t>{range.end - 1};
    out << ";\n";
  }
  for (const auto &name : message.reservedNames) {
    indent(out, depth + 1);
    out << "reserved ";
    emitQuoted(out, name);
    out << ";\n";
  }
  for (const auto &field : message.fields)
    if (field.oneofIndex == ZuCmp<int32_t>::null() ||
      field.proto3Optional)
      if (!emitField(out, schema, field, syntax, depth + 1, false))
	return false;
  for (unsigned i = 0, n = message.oneofs.length(); i < n; ++i) {
    bool synthetic = false;
    for (const auto &field : message.fields)
      if (field.oneofIndex == int32_t(i) && field.proto3Optional) {
	synthetic = true;
	break;
      }
    if (synthetic) continue;
    indent(out, depth + 1);
    out << "oneof " << message.oneofs[i].name << " {\n";
    for (const auto &field : message.fields)
      if (field.oneofIndex == int32_t(i))
	if (!emitField(out, schema, field, syntax, depth + 2, true))
	  return false;
    indent(out, depth + 1);
    out << "}\n";
  }
  indent(out, depth);
  out << "}\n";
  return true;
}

bool emitProto(ProtoText &out, const Schema &schema, const FileDesc &file,
  Syntax::T version)
{
  out.clear();
  if (!syntaxValid(version)) return false;
  ZuCSpan syntax = syntaxName(version);
  if (syntax != (file.syntax.length() ? file.syntax : ZuCSpan{"proto2"}))
    return false;
  out << "syntax = \"" << syntax << "\";\n";
  if (file.package.length()) out << "package " << file.package << ";\n";
  for (const auto &dependency : file.dependencies)
    out << "import \"" << dependency << "\";\n";
  out << '\n';
  for (const auto &enumeration : file.enums) {
    emitEnum(out, enumeration, 0);
    out << '\n';
  }
  for (const auto &message : file.messages) {
    if (!emitMessage(out, schema, message, syntax, 0)) return false;
    out << '\n';
  }
  return true;
}

} // ZiPB::Reflect
