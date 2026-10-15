//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Protocol Buffers descriptor import and borrowed schema handles.

#ifndef ZpbReflect_HH
#define ZpbReflect_HH

#ifndef ZpbLib_HH
#include <zlib/ZpbLib.hh>
#endif

#include <stdint.h>
#include <string.h>

#include <zlib/ZuCmp.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>

#include <zlib/ZmHeap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/Zpb.hh>

namespace Zpb::Reflect {

ZtEnumNS(ZpbAPI, Syntax, int8_t, Proto2, Proto3);
inline bool syntaxValid(Syntax::T syntax) {
  return syntax == Syntax::Proto2 || syntax == Syntax::Proto3;
}
inline ZuCSpan syntaxName(Syntax::T syntax) {
  return syntax == Syntax::Proto2 ? "proto2" : "proto3";
}

// The field numbers below are from google/protobuf/descriptor.proto. Only
// metadata used by this import slice is decoded; other wire fields are skipped.
struct FieldDesc {
  ZuCSpan	name, typeName, jsonName;
  int32_t	number = 0, label = 0, type = 0;
  int32_t	oneofIndex = ZuCmp<int32_t>::null();
  ZuCSpan	defaultValue;
  ZuBSpan	options;
  bool		proto3Optional = false;
};
ZfStruct(ZpbAPI, (FieldDesc, PB),
  (name,		(PB::FieldID<1>),	String),
  (number,		(PB::FieldID<3>),	Int32),
  (label,		(PB::FieldID<4>),	Int32),
  (type,		(PB::FieldID<5>),	Int32),
  (typeName,		(PB::FieldID<6>),	String),
  (defaultValue,	(PB::FieldID<7>),	String),
  (options,		(PB::FieldID<8>),	Bytes),
  (oneofIndex,		(PB::FieldID<9>),	Int32),
  (jsonName,		(PB::FieldID<10>),	String),
  (proto3Optional,	(PB::FieldID<17>),	Bool));
AsMessage<"google.protobuf.FieldDescriptorProto"> Zpb_Fmt(FieldDesc *);

// FieldOptions is retained as a borrowed nested-message payload so descriptors
// stay copyable. Schema verification admits only its packed field.
inline int packedOption(const FieldDesc &field) {
  if (!field.options.data()) return -1;
  unsigned pos = 0;
  int packed = -1;
  while (pos < field.options.length()) {
    uint32_t number;
    Wire::T wire;
    uint64_t value;
    if (!Wire::readTag(field.options, pos, number, wire) ||
      number != 2 || wire != Wire::Varint ||
      !Wire::readVarint(field.options, pos, value) || value > 1)
      return -1;
    packed = int(value);
  }
  return packed;
}

inline ZuBSpan packedOptionBytes(bool packed) {
  static constexpr uint8_t yes[] = {0x10, 0x01};
  static constexpr uint8_t no[] = {0x10, 0x00};
  return packed ? ZuBSpan{yes, sizeof(yes)} : ZuBSpan{no, sizeof(no)};
}

ZuDerive(Fields, (ZtArray<FieldDesc, ZtArrayHeapID<"Zpb.Reflect.Fields">>));
AsArray<"google.protobuf.FieldDescriptors"> Zpb_Fmt(Fields *);

struct EnumValueDesc {
  ZuCSpan	name;
  int32_t	number = 0;
};
ZfStruct(ZpbAPI, (EnumValueDesc, PB),
  (name,		(PB::FieldID<1>),	String),
  (number,	(PB::FieldID<2>),		Int32));
AsMessage<"google.protobuf.EnumValueDescriptorProto">
  Zpb_Fmt(EnumValueDesc *);
ZuDerive(EnumValues,
  (ZtArray<EnumValueDesc, ZtArrayHeapID<"Zpb.Reflect.EnumValues">>));
AsArray<"google.protobuf.EnumValueDescriptors"> Zpb_Fmt(EnumValues *);

struct EnumDesc {
  ZuCSpan	name;
  EnumValues	values;
};
ZfStruct(ZpbAPI, (EnumDesc, PB),
  (name,		(PB::FieldID<1>),	String),
  (values,	(PB::FieldID<2>),		UDT));
AsMessage<"google.protobuf.EnumDescriptorProto"> Zpb_Fmt(EnumDesc *);
ZuDerive(Enums, (ZtArray<EnumDesc, ZtArrayHeapID<"Zpb.Reflect.Enums">>));
AsArray<"google.protobuf.EnumDescriptors"> Zpb_Fmt(Enums *);

struct OneofDesc { ZuCSpan name; };
ZfStruct(ZpbAPI, (OneofDesc, PB),
  (name,		(PB::FieldID<1>),	String));
AsMessage<"google.protobuf.OneofDescriptorProto"> Zpb_Fmt(OneofDesc *);
ZuDerive(Oneofs,
  (ZtArray<OneofDesc, ZtArrayHeapID<"Zpb.Reflect.Oneofs">>));
AsArray<"google.protobuf.OneofDescriptors"> Zpb_Fmt(Oneofs *);

struct MsgDesc;
ZuDerive(NestedMessages,
  (ZtArray<ZuPtr<MsgDesc>, ZtArrayHeapID<"Zpb.Reflect.NestedMessages">>));
AsArray<"google.protobuf.NestedDescriptors"> Zpb_Fmt(NestedMessages *);

struct ReservedRange { int32_t start = 0, end = 0; };
ZfStruct(ZpbAPI, (ReservedRange, PB),
  (start,	(PB::FieldID<1>),	Int32),
  (end,		(PB::FieldID<2>),	Int32));
AsMessage<"google.protobuf.DescriptorProto.ReservedRange">
  Zpb_Fmt(ReservedRange *);
ZuDerive(ReservedRanges,
  (ZtArray<ReservedRange, ZtArrayHeapID<"Zpb.Reflect.ReservedRanges">>));
AsArray<"google.protobuf.ReservedRanges"> Zpb_Fmt(ReservedRanges *);
ZuDerive(ReservedNames,
  (ZtArray<ZuCSpan, ZtArrayHeapID<"Zpb.Reflect.ReservedNames">>));
AsArray<"google.protobuf.ReservedNames"> Zpb_Fmt(ReservedNames *);

template <typename Heap = ZuVoid>
struct MsgOpts_ : Heap {
  bool mapEntry = false;

  explicit MsgOpts_(bool entry = false) : mapEntry{entry} { }
};
using MsgOpts = MsgOpts_<ZmHeap<"Zpb.Reflect.MsgOpts", MsgOpts_<>>>;
ZfStruct(ZpbAPI, (MsgOpts, PB),
  (mapEntry,	(PB::FieldID<7>),	Bool));
AsMessage<"google.protobuf.MessageOptions"> Zpb_Fmt(MsgOpts *);

template <typename Heap = ZuVoid>
struct MsgDesc_ : Heap {
  ZuCSpan		name;
  Fields		fields;
  NestedMessages	nested;
  Enums			enums;
  ZuPtr<MsgOpts>		options;
  Oneofs		oneofs;
  ReservedRanges	reservedRanges;
  ReservedNames		reservedNames;
  bool			complete = false;
};
ZuDerive(MsgDesc, (MsgDesc_<ZmHeap<"Zpb.Reflect.MsgDesc", MsgDesc_<>>>));
ZfStruct(ZpbAPI, (MsgDesc, PB),
  (name,		(PB::FieldID<1>),	String),
  (fields,	(PB::FieldID<2>),		UDT),
  (nested,	(PB::FieldID<3>),		UDT),
  (enums,	(PB::FieldID<4>),		UDT),
  (options,	(PB::FieldID<7>),		UDT),
  (oneofs,	(PB::FieldID<8>),		UDT),
  (reservedRanges,	(PB::FieldID<9>),	UDT),
  (reservedNames,	(PB::FieldID<10>),	StringVec));
AsMessage<"google.protobuf.DescriptorProto"> Zpb_Fmt(MsgDesc *);

ZuDerive(Messages, (ZtArray<MsgDesc, ZtArrayHeapID<"Zpb.Reflect.Messages">>));
AsArray<"google.protobuf.Descriptors"> Zpb_Fmt(Messages *);

ZuDerive(Dependencies,
  (ZtArray<ZuCSpan, ZtArrayHeapID<"Zpb.Reflect.Dependencies">>));
template <typename Heap = ZuVoid>
struct RetainedText_ : Heap,
  ZtString<ZtStringHeapID<"Zpb.Reflect.RetainedText">> {
};
using RetainedText = RetainedText_<ZmHeap<"Zpb.Reflect.RetainedText", RetainedText_<>>>;
ZuDerive(RetainedTexts,
  (ZtArray<ZuPtr<RetainedText>, ZtArrayHeapID<"Zpb.Reflect.RetainedTexts">>));

struct FileDesc {
  ZuCSpan		name, package, syntax;
  Dependencies	dependencies;
  Messages	messages;
  Enums			enums;
  RetainedTexts	retained;
};
inline Syntax::T fileSyntax(const FileDesc &file) {
  return file.syntax == "proto3" ? Syntax::Proto3 : Syntax::Proto2;
}
ZfStruct(ZpbAPI, (FileDesc, PB),
  (name,			(PB::FieldID<1>),	String),
  (package,		(PB::FieldID<2>),		String),
  (dependencies,	(PB::FieldID<3>),		StringVec),
  (messages,		(PB::FieldID<4>),		UDT),
  (enums,		(PB::FieldID<5>),		UDT),
  (syntax,		(PB::FieldID<12>),		String));
AsMessage<"google.protobuf.FileDescriptorProto"> Zpb_Fmt(FileDesc *);

ZuDerive(Files, (ZtArray<FileDesc, ZtArrayHeapID<"Zpb.Reflect.Files">>));
AsArray<"google.protobuf.FileDescriptors"> Zpb_Fmt(Files *);

struct DescSet { Files files; };
ZfStruct(ZpbAPI, (DescSet, PB),
  (files,	(PB::FieldID<1>),	UDT));
AsMessage<"google.protobuf.FileDescriptorSet"> Zpb_Fmt(DescSet *);

ZtEnumNS(ZpbAPI, Type, int8_t,
  Invalid, Double, Float, Int64, UInt64, Int32, Fixed64, Fixed32,
  Bool, String, Group, Message, Bytes, UInt32, Enum,
  SFixed32, SFixed64, SInt32, SInt64);

// A visitor receives decoded values while the input bytes remain live.
// `integer`, `real`, or `bytes` is selected by FieldDesc::type. The bytes
// span is valid during the callback; retain a copy if it must outlive input.
struct Value {
  const FieldDesc	*field;
  uint64_t	integer;
  double		real;
  ZuBSpan	bytes;
};

// The descriptor fields borrow strings from m_bytes. Handles borrow the
// Schema object; neither lookup nor trusted import revalidates the wire.
class ZpbAPI Schema {
public:
  explicit Schema(ZuBSpan bytes, bool prepareFields = true);
  ~Schema();
  Schema(const Schema &) = delete;
  Schema &operator =(const Schema &) = delete;

  const DescSet &descriptor() const { return m_desc; }
  const FileDesc *file(ZuCSpan name) const;
  const FileDesc *file(ZuCSpan name, Syntax::T version) const;
  const MsgDesc *message(const FileDesc &, ZuCSpan name) const;
  const MsgDesc *message(ZuCSpan qualified) const;
  const EnumDesc *enumeration(ZuCSpan qualified) const;
  const FieldDesc *field(const MsgDesc &, unsigned number) const;
  const FieldDesc *field(const MsgDesc &, ZuCSpan name) const;

private:
  struct Index;
  using Bytes = ZtArray<uint8_t, ZtArrayHeapID<"Zpb.Reflect.Bytes">>;
  Bytes m_bytes;
  DescSet m_desc;
  ZuPtr<Index> m_index;
};

// `reason` identifies recognized unsupported descriptor features; malformed
// wire and other invalid schema semantics leave it empty.
ZpbAPI bool verifySchema(ZuBSpan bytes, VerifyLimits limits = {},
  ZuCSpan *reason = nullptr);
ZpbAPI bool verify(const MsgDesc &, ZuBSpan bytes, VerifyLimits limits = {});
ZpbAPI bool verify(const Schema &, const MsgDesc &, ZuBSpan bytes,
  VerifyLimits limits = {});

using Visitor = bool (*)(void *, const Value &);
ZpbAPI bool visit(const MsgDesc &, ZuBSpan bytes, void *, Visitor);
ZpbAPI bool visit(const Schema &, const MsgDesc &, ZuBSpan bytes, void *, Visitor);
template <typename L>
bool visit(const MsgDesc &message, ZuBSpan bytes, L &&fn) {
  auto call = [&fn](const Value &value) { return fn(value); };
  return visit(message, bytes, &call,
    [](void *p, const Value &value) {
      return (*static_cast<decltype(call) *>(p))(value);
    });
}
template <typename L>
bool visit(const Schema &schema, const MsgDesc &message, ZuBSpan bytes, L &&fn) {
  auto call = [&fn](const Value &value) { return fn(value); };
  return visit(schema, message, bytes, &call,
    [](void *p, const Value &value) {
      return (*static_cast<decltype(call) *>(p))(value);
    });
}

ZuDerive(ProtoText,
  (ZtString<ZtStringHeapID<"Zpb.Reflect.ProtoText">>));
template <typename Out, typename Span>
void emitQuoted(Out &out, const Span &value)
{
  out << '"';
  for (unsigned i = 0, n = value.length(); i < n; ++i) {
    uint8_t c = uint8_t(value[i]);
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
	if (c >= 32 && c < 127) out << char(c);
	else
	  out << '\\' << char('0' + ((c >> 6) & 7))
	    << char('0' + ((c >> 3) & 7)) << char('0' + (c & 7));
    }
  }
  out << '"';
}
// The requested version must match the selected descriptor file. Native
// export can derive either version from the same C++ type before this call.
ZpbAPI bool emitProto(ProtoText &, const Schema &, const FileDesc &, Syntax::T);

namespace Native {

template <typename T> struct Name {
  static constexpr auto value = As<T>::name();
  static constexpr auto qualified = ZuString{"."} + value;
};

template <typename F> struct FieldName {
  static constexpr auto value = [] {
    if constexpr (ZuFieldProp::HasValue<typename F::Props,
      ZuFieldProp::PB::ID>{})
      return ZuFieldProp::GetValue<typename F::Props,
	ZuFieldProp::PB::ID>{}();
    else return ZuString{F::id()};
  }();
};

template <typename A> struct AltName {
  static constexpr auto value = A::name();
};

template <typename T, typename F> struct EntryName {
  static constexpr auto value = [] {
    auto name = FieldName<F>::value + ZuString{"Entry"};
    if (name[0] >= 'a' && name[0] <= 'z') name[0] -= 'a' - 'A';
    return name;
  }();
  static constexpr auto qualified = ZuString{"."} + Name<T>::value +
    ZuString{"."} + value;
};

template <typename Props> struct PropsHandler {
  template <typename> static consteval bool zigzag() {
    return ZuTypeIn<ZuFieldProp::PB::ZigZag, Props>{};
  }
  template <typename> static consteval bool fixed() {
    return ZuTypeIn<ZuFieldProp::PB::Fixed, Props>{};
  }
  template <typename> static consteval bool boolVec() {
    return ZuTypeIn<ZuFieldProp::PB::BoolVec, Props>{};
  }
};

template <typename Fmt, typename = void> struct IsMap : ZuFalse { };
template <typename Fmt>
struct IsMap<Fmt, decltype(Fmt::KC, Fmt::VC, void())> : ZuTrue { };

template <typename T, typename = void> struct WireObject { using Type = T; };
template <typename T>
struct WireObject<T, decltype(sizeof(typename As<T>::Wire), void())> {
  using Type = typename As<T>::Wire;
};
template <typename T> using WireType = typename WireObject<T>::Type;

template <typename A, typename = void> struct Reserves {
  using Types = ZuTypeList<>;
};
template <typename A>
struct Reserves<A, decltype(sizeof(typename A::Reserved), void())> {
  using Types = typename A::Reserved;
};

template <typename F, unsigned = F::Code>
struct UnionField : ZuFalse { };
template <typename F>
struct UnionField<F, ZfFieldTC::UDT> :
  ZuBool<As<ObjectType<typename F::T>>::Union> { };

template <typename Selected> struct Effective {
  template <typename F> struct Find {
    template <typename G> using Match = ZuBool<
      ZuString{F::id()} == ZuString{G::id()}>;
    using Matches = ZuTypeGrep<Match, Selected>;
    using T = ZuType<0, typename Matches::template Push<F>>;
  };
  template <typename F> using Apply = typename Find<F>::T;
};

inline ZuCSpan shortName(ZuCSpan name, ZuCSpan package) {
  if (package.length()) {
    if (name.length() <= package.length() ||
      memcmp(name.data(), package.data(), package.length()) ||
      name[package.length()] != '.') return {};
    name = {name.data() + package.length() + 1,
      name.length() - package.length() - 1};
  }
  for (unsigned i = 0, n = name.length(); i < n; ++i)
    if (name[i] == '.') return {};
  return name;
}

inline bool inPackage(ZuCSpan qualified, ZuCSpan package) {
  return !package.length() ||
    (qualified.length() > package.length() &&
      !memcmp(qualified.data(), package.data(), package.length()) &&
      qualified[package.length()] == '.');
}

inline const MsgDesc *findMessage(const FileDesc &file, ZuCSpan path) {
  const MsgDesc *message = nullptr;
  while (path.length()) {
    unsigned n = 0;
    while (n < path.length() && path[n] != '.') ++n;
    ZuCSpan name{path.data(), n};
    const MsgDesc *found = nullptr;
    if (message) {
      for (const auto &nested : message->nested)
	if (nested->name == name) { found = nested.ptr(); break; }
    } else {
      for (const auto &candidate : file.messages)
	if (candidate.name == name) { found = &candidate; break; }
    }
    if (!found) return nullptr;
    message = found;
    if (n == path.length()) break;
    path = {path.data() + n + 1, path.length() - n - 1};
  }
  return message;
}

inline bool addExternal(const DescSet &set, FileDesc &file,
  ZuCSpan qualified, bool isEnum, const EnumValues *values = nullptr) {
  if (qualified.length() && qualified[0] == '.')
    qualified = {qualified.data() + 1, qualified.length() - 1};
  for (const auto &source : set.files) {
    if (source.package == file.package ||
      !inPackage(qualified, source.package)) continue;
    ZuCSpan path = source.package.length() ?
      ZuCSpan{qualified.data() + source.package.length() + 1,
	qualified.length() - source.package.length() - 1} : qualified;
    bool found = false;
    if (!isEnum) {
      auto message = findMessage(source, path);
      found = message != nullptr;
    } else {
      ZuCSpan name = path;
      ZuCSpan parent;
      for (unsigned i = path.length(); i; --i)
	if (path[i - 1] == '.') {
	  parent = {path.data(), i - 1};
	  name = {path.data() + i, path.length() - i};
	  break;
	}
      const EnumDesc *enumeration = nullptr;
      if (parent.length()) {
	if (auto message = findMessage(source, parent))
	  for (const auto &candidate : message->enums)
	    if (candidate.name == name) { enumeration = &candidate; break; }
      } else for (const auto &candidate : source.enums)
	if (candidate.name == name) { enumeration = &candidate; break; }
      found = enumeration != nullptr;
      if (found && values) {
	found = enumeration->values.length() == values->length();
	for (const auto &value : *values) {
	  bool match = false;
	  for (const auto &candidate : enumeration->values)
	    if (candidate.name == value.name &&
		candidate.number == value.number) { match = true; break; }
	  if (!match) { found = false; break; }
	}
      }
    }
    if (!found) continue;
    for (auto dependency : file.dependencies)
      if (dependency == source.name) return true;
    file.dependencies.push(source.name);
    return true;
  }
  return false;
}

inline MsgDesc *ensureMessage(FileDesc &file, ZuCSpan path)
{
  MsgDesc *parent = nullptr;
  while (path.length()) {
    unsigned n = 0;
    while (n < path.length() && path[n] != '.') ++n;
    ZuCSpan name{path.data(), n};
    if (!name.length()) return nullptr;
    if (!parent) {
      MsgDesc *found = nullptr;
      for (auto &message : file.messages)
	if (message.name == name) { found = &message; break; }
      if (!found) {
	file.messages.push(MsgDesc{});
	found = &file.messages[file.messages.length() - 1];
	found->name = name;
      }
      parent = found;
    } else {
      MsgDesc *found = nullptr;
      for (auto &nested : parent->nested)
	if (nested->name == name) { found = nested.ptr(); break; }
      if (!found) {
	ZuPtr<MsgDesc> nested{new MsgDesc};
	nested->name = name;
	parent->nested.push(ZuMv(nested));
	found = parent->nested[parent->nested.length() - 1].ptr();
      }
      parent = found;
    }
    if (n == path.length()) break;
    path = {path.data() + n + 1, path.length() - n - 1};
  }
  return parent;
}

inline void mergeMessage(MsgDesc &dst, MsgDesc &&src)
{
  for (auto &enumeration : src.enums) {
    bool found = false;
    for (const auto &existing : dst.enums)
      if (existing.name == enumeration.name) { found = true; break; }
    if (!found) dst.enums.push(ZuMv(enumeration));
  }
  for (auto &nested : src.nested) {
    MsgDesc *found = nullptr;
    for (auto &existing : dst.nested)
      if (existing->name == nested->name) { found = existing.ptr(); break; }
    if (found) mergeMessage(*found, ZuMv(*nested.ptr()));
    else dst.nested.push(ZuMv(nested));
  }
  if (src.complete) {
    dst.fields = ZuMv(src.fields);
    dst.options = ZuMv(src.options);
    dst.oneofs = ZuMv(src.oneofs);
    dst.reservedRanges = ZuMv(src.reservedRanges);
    dst.reservedNames = ZuMv(src.reservedNames);
    dst.complete = true;
  }
}

template <unsigned C, typename F, typename H>
bool scalar(FieldDesc &field) {
  if constexpr (C == ZfFieldTC::CString || C == ZfFieldTC::String)
    field.type = Type::String;
  else if constexpr (C == ZfFieldTC::Bytes)
    field.type = Type::Bytes;
  else if constexpr (C == ZfFieldTC::Bool)
    field.type = Type::Bool;
  else if constexpr (C == ZfFieldTC::Int8 || C == ZfFieldTC::Int16 ||
    C == ZfFieldTC::Int32) {
    if constexpr (C == ZfFieldTC::Int32 && H::template zigzag<F>())
      field.type = Type::SInt32;
    else if constexpr (C == ZfFieldTC::Int32 && H::template fixed<F>())
      field.type = Type::SFixed32;
    else field.type = Type::Int32;
  } else if constexpr (C == ZfFieldTC::UInt8 || C == ZfFieldTC::UInt16 ||
    C == ZfFieldTC::UInt32) {
    if constexpr (C == ZfFieldTC::UInt8 && H::template boolVec<F>())
      field.type = Type::Bool;
    else if constexpr (C == ZfFieldTC::UInt32 && H::template fixed<F>())
      field.type = Type::Fixed32;
    else field.type = Type::UInt32;
  } else if constexpr (C == ZfFieldTC::Int64) {
    if constexpr (H::template zigzag<F>()) field.type = Type::SInt64;
    else if constexpr (H::template fixed<F>()) field.type = Type::SFixed64;
    else field.type = Type::Int64;
  } else if constexpr (C == ZfFieldTC::UInt64) {
    if constexpr (H::template fixed<F>()) field.type = Type::Fixed64;
    else field.type = Type::UInt64;
  } else if constexpr (C == ZfFieldTC::Float) {
    if constexpr (ZfFieldTC::IsVec<F::Code>{})
      field.type = sizeof(typename ZuTraits<typename F::T>::Elem) == 4 ?
	Type::Float : Type::Double;
    else field.type = sizeof(typename F::T) == 4 ? Type::Float : Type::Double;
  }
  else return false;
  return true;
}

template <unsigned C, typename Props>
bool enumeration(const DescSet &set, FileDesc &file, FieldDesc &field)
{
  if constexpr (!ZuFieldProp::HasEnum<Props>{}) return true;
  else if constexpr (C != ZfFieldTC::Int8 && C != ZfFieldTC::Int16 &&
    C != ZfFieldTC::Int32) return false;
  else {
    using Map = ZuFieldProp::GetEnum<Props>;
    ZuCSpan id{Map::id()};
    if (!id.length()) return false;
    EnumDesc value;
    bool valid = true;
    Map::all([&](ZuCSpan label, auto number) {
      int64_t n = number;
      if (!label.length() || n < INT32_MIN || n > INT32_MAX) valid = false;
      else value.values.push(EnumValueDesc{label, int32_t(n)});
    });
    if (!valid || !value.values.length()) return false;
    if (addExternal(set, file, id, true, &value.values)) {
      field.type = Type::Enum;
      ZuPtr<RetainedText> qualified{new RetainedText};
      *qualified << '.' << id;
      file.retained.push(ZuMv(qualified));
      field.typeName = file.retained[file.retained.length() - 1]->cspan();
      return true;
    }
    if (addExternal(set, file, id, true)) return false;
    ZuCSpan name = id;
    ZuCSpan parentPath;
    if (file.package.length() && id.length() > file.package.length() &&
      !memcmp(id.data(), file.package.data(), file.package.length()) &&
      id[file.package.length()] == '.')
      name = {id.data() + file.package.length() + 1,
	id.length() - file.package.length() - 1};
    else for (char c : id) if (c == '.') return false;
    for (char c : name) if (c == '.') {
      for (unsigned i = name.length(); i; --i)
	if (name[i - 1] == '.') {
	  parentPath = {name.data(), i - 1};
	  name = {name.data() + i, name.length() - i};
	  break;
	}
      break;
    }
    if (!name.length()) return false;
    value.name = name;
    Enums *enums = &file.enums;
    if (parentPath.length()) {
      MsgDesc *parent = ensureMessage(file, parentPath);
      if (!parent) return false;
      enums = &parent->enums;
    }
    bool found = false;
    for (const auto &existing : *enums) if (existing.name == name) {
      if (existing.values.length() != value.values.length()) return false;
      for (unsigned i = 0, n = value.values.length(); i < n; ++i)
	if (existing.values[i].name != value.values[i].name ||
	  existing.values[i].number != value.values[i].number) return false;
      found = true;
      break;
    }
    if (!found) enums->push(ZuMv(value));
    ZuPtr<RetainedText> qualified{new RetainedText};
    if (id == name && file.package.length())
      *qualified << '.' << file.package << '.' << id;
    else *qualified << '.' << id;
    file.retained.push(ZuMv(qualified));
    field.type = Type::Enum;
    field.typeName = file.retained[file.retained.length() - 1]->cspan();
    return true;
  }
}

template <typename F>
bool defaultValue(FileDesc &file, FieldDesc &field)
{
  if constexpr (!ZuFieldProp::HasDeflt<typename F::Props>{}) return true;
  else {
    using Default = ZuFieldProp::GetDeflt<typename F::Props>;
    if (file.syntax == "proto3") return true;
    if (field.label == 3 ||
      field.oneofIndex != ZuCmp<int32_t>::null()) return false;
    ZuPtr<RetainedText> text{new RetainedText};
    auto &dst = *text;
    constexpr unsigned code = F::Code;
    if constexpr (code == ZfFieldTC::Bool)
      dst << (Default::deflt() ? "true" : "false");
    else if constexpr (code == ZfFieldTC::CString ||
      code == ZfFieldTC::String) {
      ZuCSpan value{Default::deflt()};
      if (!value.data()) return false;
      dst << value;
    } else if constexpr (code == ZfFieldTC::Bytes) {
      ZuBSpan value{Default::deflt()};
      if (!value.data()) return false;
      ProtoText quoted;
      emitQuoted(quoted, value);
      dst << ZuCSpan{quoted.data() + 1, quoted.length() - 2};
    } else if constexpr (code >= ZfFieldTC::Int8 &&
      code <= ZfFieldTC::UInt64) {
      auto value = Default::deflt();
      if (ZuNull(value)) return false;
      if constexpr (ZuFieldProp::HasEnum<typename F::Props>{}) {
	auto name = ZuFieldProp::GetEnum<typename F::Props>::v2s(value);
	if (!name.data()) return false;
	dst << name;
      } else dst << ZuBox<decltype(value)>{value};
    } else if constexpr (code == ZfFieldTC::Float) {
      auto value = Default::deflt();
      if (ZuNull(value)) return false;
      if (ZuFP<decltype(value)>::inf(value))
	dst << (value < 0 ? "-inf" : "inf");
      else dst << ZuBox<decltype(value)>{value};
    } else return false;
    file.retained.push(ZuMv(text));
    auto &saved = *file.retained[file.retained.length() - 1];
    field.defaultValue = saved.length() ? saved.cspan() : ZuCSpan{"", 0};
    return true;
  }
}

template <typename T, typename Facet, typename Seen>
bool message(const DescSet &set, FileDesc &file);

template <typename T, typename Facet, typename Seen>
bool message(const DescSet &set, FileDesc &file) {
  if (addExternal(set, file, Name<T>::qualified.cspan(), false)) return true;
  if constexpr (ZuTypeIn<T, Seen>{})
    return inPackage(Name<T>::value.cspan(), file.package);
  else {
    ZuCSpan qualified = Name<T>::value.cspan();
    if (!inPackage(qualified, file.package)) return false;
    ZuCSpan path = qualified;
    if (file.package.length()) {
      if (qualified.length() > file.package.length() &&
	!memcmp(qualified.data(), file.package.data(), file.package.length()) &&
	qualified[file.package.length()] == '.')
	path = {qualified.data() + file.package.length() + 1,
	  qualified.length() - file.package.length() - 1};
      else path = {};
    }
    if (!path.length()) return false;
    MsgDesc *slot = ensureMessage(file, path);
    if (!slot) return false;
    if (slot->complete) return true;
    ZuCSpan name = path;
    for (unsigned i = path.length(); i; --i)
      if (path[i - 1] == '.') {
	name = {path.data() + i, path.length() - i};
	break;
      }
    using H = typename As<T>::template Handler<T, Facet>;
    using Next = typename Seen::template Push<T>;
    using Complete = ZuFields<ZuStructured<T>>;
    using Selected = ZuFields<T, Facet>;
    using SchemaFields = ZuTypeMap<Effective<Selected>::template Apply,
      Complete>;
    MsgDesc desc;
    desc.name = name;
    ZuUnroll::all<typename Reserves<As<T>>::Types>([&]<typename R>() {
      if constexpr (R::IsRange)
	desc.reservedRanges.push(ReservedRange{R::Start_, R::End_});
      else desc.reservedNames.push(R::value.cspan());
    });
    ZuUnroll::all<SchemaFields>([&]<typename F>() {
      if constexpr (!ZuFieldActive<F>{}) {
	int32_t number = H::template number<F>();
	desc.reservedRanges.push(ReservedRange{number, number + 1});
      }
    });
    bool ok = true;
    ZuUnroll::all<ZuTypeGrep<ZuFieldActive, SchemaFields>>([&]<typename F>() {
      if (!ok) return;
      constexpr unsigned code = F::Code;
      if constexpr (UnionField<F>{}) {
	using U = ObjectType<typename F::T>;
	if constexpr (ZuTypeIn<ZuFieldProp::Required,
	  typename F::Props>{}) ok = false;
	else {
	  int32_t index = desc.oneofs.length();
	  desc.oneofs.push(OneofDesc{FieldName<F>::value.cspan()});
	  ZuUnroll::all<typename As<U>::Types>([&]<typename A>() {
	    if (!ok) return;
	    using V = typename A::T;
	    FieldDesc alt;
	    alt.name = AltName<A>::value.cspan();
	    alt.number = A::Field;
	    alt.label = 1;
	    alt.type = Type::Message;
	    alt.typeName = Name<V>::qualified.cspan();
	    alt.oneofIndex = index;
	    ok = message<V, Facet, Next>(set, file);
	    if (ok) desc.fields.push(ZuMv(alt));
	  });
	}
      } else {
	FieldDesc field;
	field.name = FieldName<F>::value.cspan();
	field.number = H::template number<F>();
	field.label = file.syntax == "proto2" &&
	  ZuTypeIn<ZuFieldProp::Required, typename F::Props>{} ? 2 : 1;
	if constexpr (code == ZfFieldTC::UDT) {
	  using U = ObjectType<typename F::T>;
	  if constexpr (As<U>::Repeated) {
	    if constexpr (IsMap<As<U>>{}) {
	      using Fmt = As<U>;
	      field.type = Type::Message;
	      field.label = 3;
	      field.typeName = EntryName<T, F>::qualified.cspan();
	      ZuPtr<MsgDesc> entry{new MsgDesc{}};
	      entry->name = EntryName<T, F>::value.cspan();
	      entry->options = new MsgOpts{true};
	      FieldDesc key;
	      key.name = "key";
	      key.number = 1;
	      key.label = 1;
	      if constexpr (ZuFieldProp::HasEnum<typename Fmt::KP>{})
		ok = false;
	      else ok = scalar<Fmt::KC, F,
		PropsHandler<typename Fmt::KP>>(key);
	      if (ok) entry->fields.push(ZuMv(key));
	      FieldDesc value;
	      value.name = "value";
	      value.number = 2;
	      value.label = 1;
	      if constexpr (Fmt::VC == ZfFieldTC::UDT) {
		using V = WireType<ObjectType<typename U::Val>>;
		value.type = Type::Message;
		value.typeName = Name<V>::qualified.cspan();
		if (ok) ok = message<V, Facet, Next>(set, file);
	      } else if constexpr (MappedCode<Fmt::VC>{}) {
		using V = typename Mapping<Fmt::VC>::T;
		value.type = Type::Message;
		value.typeName = Name<V>::qualified.cspan();
		if (ok) ok = message<V, Facet, Next>(set, file);
	      } else if (ok) {
		ok = scalar<Fmt::VC, F,
		  PropsHandler<typename Fmt::VP>>(value);
		if (ok) ok = enumeration<Fmt::VC,
		  typename Fmt::VP>(set, file, value);
	      }
	      if (ok) {
		entry->fields.push(ZuMv(value));
		desc.nested.push(ZuMv(entry));
	      }
	    } else {
	      using E = WireType<ObjectType<typename ZuTraits<U>::Elem>>;
	      field.type = Type::Message;
	      field.label = 3;
	      field.typeName = Name<E>::qualified.cspan();
	      ok = message<E, Facet, Next>(set, file);
	    }
	  }
	  else {
	    using V = WireType<U>;
	    field.type = Type::Message;
	    field.typeName = Name<V>::qualified.cspan();
	    ok = message<V, Facet, Next>(set, file);
	  }
	} else if constexpr (code >= ZfFieldTC::CStringVec &&
	  code <= ZfFieldTC::DateTimeVec) {
	  field.label = 3;
	  constexpr unsigned elem = ZfFieldTC::Elem<code>{};
	  if constexpr (MappedCode<elem>{}) {
	    using V = typename Mapping<elem>::T;
	    field.type = Type::Message;
	    field.typeName = Name<V>::qualified.cspan();
	    ok = message<V, Facet, Next>(set, file);
	  } else {
	    ok = scalar<elem, F, H>(field);
	    if (ok) ok = enumeration<elem, typename F::Props>(
	      set, file, field);
	  }
	} else if constexpr (MappedCode<code>{}) {
	  using V = typename Mapping<code>::T;
	  field.type = Type::Message;
	  field.typeName = Name<V>::qualified.cspan();
	  ok = message<V, Facet, Next>(set, file);
	} else {
	  ok = scalar<code, F, H>(field);
	  if (ok) ok = enumeration<code, typename F::Props>(
	    set, file, field);
	}
	if constexpr ((code >= ZfFieldTC::Int8Vec &&
	  code <= ZfFieldTC::UInt64Vec) || code == ZfFieldTC::FloatVec) {
	  if (ok && (file.syntax == "proto3" || H::template packed<F>()))
	    field.options = packedOptionBytes(H::template packed<F>());
	}
	if (ok) ok = defaultValue<F>(file, field);
	if (ok) desc.fields.push(ZuMv(field));
      }
    });
    if (!ok) return false;
    for (unsigned i = 0, n = desc.fields.length(); i < n; ++i) {
      const auto &field = desc.fields[i];
      for (unsigned j = 0; j < i; ++j)
	if (field.name == desc.fields[j].name) return false;
      for (const auto &oneof : desc.oneofs)
	if (field.name == oneof.name) return false;
    }
    for (unsigned i = 0, n = desc.oneofs.length(); i < n; ++i)
      for (unsigned j = 0; j < i; ++j)
	if (desc.oneofs[i].name == desc.oneofs[j].name) return false;
    desc.complete = true;
    slot = ensureMessage(file, path);
    if (!slot) return false;
    mergeMessage(*slot, ZuMv(desc));
    return true;
  }
}

} // Native

// Export the supported native fields from the effective facet. Add dependency
// files to out first; external message/enum references resolve against them.
// Names borrow compile-time format/field strings; fileName and package borrow
// the caller. Unsupported fields or unresolved references leave out unchanged.
template <typename T, typename Facet = ZuFacet::PB>
bool exportSchema(DescSet &out, ZuCSpan fileName, ZuCSpan package,
  Syntax::T syntax) {
  if (!fileName.length() || !syntaxValid(syntax)) return false;
  using Wire = Native::WireType<T>;
  if (!Native::shortName(Native::Name<Wire>::value.cspan(), package).length())
    return false;
  FileDesc file;
  file.name = fileName;
  file.package = package;
  file.syntax = syntaxName(syntax);
  if (!Native::message<Wire, Facet, ZuTypeList<>>(out, file)) return false;
  out.files.push(ZuMv(file));
  return true;
}

// Builder output is backward: individual calls prepend a field. `write` on a
// span reverses the input sequence so repeated values retain caller order.
template <typename B>
bool write(B &builder, const Value &value) {
  auto invalid = [&builder]() {
    builder.fail(Error::Invalid);
    return false;
  };
  if (!value.field || !Wire::fieldNumber(value.field->number)) return invalid();
  const auto &field = *value.field;
  Wire::T wire;
  switch (field.type) {
    case Type::Double: {
      uint64_t bits;
      memcpy(&bits, &value.real, sizeof(bits));
      builder.fixed64(bits);
      wire = Wire::Fixed64;
      break;
    }
    case Type::Float: {
      float real = value.real;
      uint32_t bits;
      memcpy(&bits, &real, sizeof(bits));
      builder.fixed32(bits);
      wire = Wire::Fixed32;
      break;
    }
    case Type::Int64: case Type::UInt64:
      builder.varint(value.integer);
      wire = Wire::Varint;
      break;
    case Type::Int32: case Type::Enum:
      if (uint64_t(int64_t(int32_t(value.integer))) != value.integer)
	return invalid();
      builder.varint(uint64_t(int64_t(int32_t(value.integer))));
      wire = Wire::Varint;
      break;
    case Type::UInt32:
      if (value.integer > UINT32_MAX) return invalid();
      builder.varint(value.integer);
      wire = Wire::Varint;
      break;
    case Type::SInt32: {
      int64_t n = int64_t(value.integer);
      if (int64_t(int32_t(n)) != n) return invalid();
      builder.varint((uint32_t(n) << 1) ^ uint32_t(-(n < 0)));
      wire = Wire::Varint;
      break;
    }
    case Type::SInt64: {
      int64_t n = int64_t(value.integer);
      builder.varint((uint64_t(n) << 1) ^ uint64_t(-(n < 0)));
      wire = Wire::Varint;
      break;
    }
    case Type::Bool:
      builder.varint(bool(value.integer));
      wire = Wire::Varint;
      break;
    case Type::Fixed32:
      if (value.integer > UINT32_MAX) return invalid();
      builder.fixed32(uint32_t(value.integer));
      wire = Wire::Fixed32;
      break;
    case Type::SFixed32: {
      int64_t n = int64_t(value.integer);
      if (int64_t(int32_t(n)) != n) return invalid();
      builder.fixed32(uint32_t(n));
      wire = Wire::Fixed32;
      break;
    }
    case Type::Fixed64: case Type::SFixed64:
      builder.fixed64(value.integer);
      wire = Wire::Fixed64;
      break;
    case Type::String: case Type::Bytes: case Type::Message:
      builder.bytes(value.bytes);
      builder.varint(value.bytes.length());
      wire = Wire::Length;
      break;
    default: return invalid();
  }
  builder.tag(field.number, wire);
  return !builder.failed();
}

template <typename B>
bool write(B &builder, ZuSpan<const Value> values) {
  for (unsigned i = values.length(); i; )
    if (!write(builder, values[--i])) return false;
  return !builder.failed();
}

using WriteSeen = ZtArray<uint8_t, ZtArrayHeapID<"Zpb.Reflect.WriteSeen">>;

template <typename B>
bool write(B &builder, const MsgDesc &message, ZuSpan<const Value> values) {
  unsigned n = message.fields.length();
  auto seen = ZtScratch(WriteSeen, n ? n : 1, n ? n : 1);
  if (n) memset(seen.data(), 0, n);
  for (const auto &value : values) {
    unsigned i = 0;
    for (; i < n; ++i)
      if (value.field == &message.fields[i]) break;
    if (i == n) { builder.fail(Error::Invalid); return false; }
    seen[i] = 1;
  }
  for (unsigned i = 0; i < n; ++i)
    if (message.fields[i].label == 2 && !seen[i]) {
      builder.fail(Error::Required);
      return false;
    }
  return write(builder, values);
}

// Prepend one message field from child values in the same backward builder.
// The caller writes later parent fields first, as with individual Value writes.
template <typename B>
bool writeNested(B &builder, const Schema &schema, const MsgDesc &parent,
  const FieldDesc &field, ZuSpan<const Value> values) {
  bool member = false;
  for (const auto &candidate : parent.fields)
    if (&candidate == &field) { member = true; break; }
  if (!member || field.type != Type::Message ||
      !Wire::fieldNumber(field.number)) {
    builder.fail(Error::Invalid);
    return false;
  }
  auto child = schema.message(field.typeName);
  if (!child) {
    builder.fail(Error::Invalid);
    return false;
  }
  unsigned before = builder.size();
  if (!write(builder, *child, values)) return false;
  builder.varint(builder.size() - before);
  builder.tag(field.number, Wire::Length);
  return !builder.failed();
}

} // Zpb::Reflect

#endif // ZpbReflect_HH
