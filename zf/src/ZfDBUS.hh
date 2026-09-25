//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// D-Bus structural load/save

#ifndef ZfDBUS_HH
#define ZfDBUS_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <string.h>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuAssert.hh>
#include <zlib/ZuIntrin.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZuFP.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZfStruct.hh>

ZuStructFacet(DBUS);

namespace ZuFieldProp::DBUS {

template <uint8_t Code_> struct Type { };
template <uint8_t Code_> struct ElemType { };

template <typename Props, uint8_t Deflt,
  bool = HasValue<Props, Type>{}>
struct GetType_ { using T = ZuConstant<uint8_t, Deflt>; };
template <typename Props, uint8_t Deflt>
struct GetType_<Props, Deflt, true> { using T = GetValue<Props, Type>; };
template <typename Props, uint8_t Deflt>
using GetType = typename GetType_<Props, Deflt>::T;

template <typename Props, bool = HasValue<Props, ElemType>{}>
struct GetElemProps_ { using T = ZuTypeList<>; };
template <typename Props>
struct GetElemProps_<Props, true> {
  using T = ZuTypeList<Type<GetValue<Props, ElemType>{}>>;
};
template <typename Props>
using GetElemProps = typename GetElemProps_<Props>::T;

} // ZuFieldProp::DBUS

namespace ZfDBUS {

namespace Type {
  enum {
    Byte = 'y', Bool = 'b', Int16 = 'n', UInt16 = 'q', Int32 = 'i',
    UInt32 = 'u', Int64 = 'x', UInt64 = 't', Double = 'd', String = 's',
    ObjectPath = 'o', Signature = 'g', Array = 'a', Struct = 'r',
    Variant = 'v', DictEntry = 'e', UnixFD = 'h'
  };
}

namespace Order { enum { Little = 'l', Big = 'B' }; }
namespace Error {
  enum { OK, Syntax, Type, Bounds, Unsupported, Overflow, Trailing };
}

// D-Bus specification limits: signatures are at most 255 bytes, arrays are
// at most 64MiB, and nesting is at most 32 arrays plus 32 structs.
namespace Limit {
  enum { Signature = 255, Array = 1U << 26, TypeDepth = 32, ValueDepth = 64 };
}

struct SignatureDepth {
  uint8_t arrays = 0;
  uint8_t structs = 0;
};

struct Result {
  unsigned	offset = 0;
  int		error = Error::Syntax;

  explicit operator bool() const { return error == Error::OK; }
};

// Borrowed input; both spans must remain immutable for a handler's lifetime.
struct View {
  ZuBSpan	bytes;
  ZuCSpan	signature;
  unsigned	offset = 0;
  uint8_t	order = Order::Little;
};

struct Context {
  uint64_t	offset = 0;
  uint8_t	order = Order::Little;
};

struct State_ : public Context {
  int		error = Error::OK;
  uint8_t	depth = 0;

  explicit State_(const Context &context) : Context{context} { }
  bool fail(int error_) {
    if (error == Error::OK) error = error_;
    return false;
  }
};

// Borrowed dynamic value; both spans retain the input message storage.
struct Any {
  ZuBSpan	bytes;
  ZuCSpan	signature;
  unsigned	offset = 0;
  uint8_t	order = Order::Little;
};

struct SignatureHeap : public ZuStringT<"ZfDBUS.Signature"> { };
ZuDerive(Signature, (ZtString<ZtStringHeapID_<SignatureHeap>>));

// Compilation-only sink: D-Bus mandates at most 255 signature bytes.
struct SigConst {
  char bytes[Limit::Signature + 1] = {};
  unsigned length = 0;

  constexpr SigConst &operator <<(char c) {
    if (length >= Limit::Signature) __builtin_trap();
    bytes[length++] = c;
    return *this;
  }
  constexpr SigConst &operator <<(const char *s) {
    while (*s) *this << *s++;
    return *this;
  }
};

struct FieldViewHeap : public ZuStringT<"ZfDBUS.FieldView"> { };
using FieldViews = ZtArray<View, ZtArrayHeapID_<FieldViewHeap>>;

struct Measure {
  Measure &operator <<(char) { return *this; }
};

template <typename S, typename = void>
struct Prepare_ {
  static void run(S &, unsigned) { }
};
template <typename S>
struct Prepare_<S, decltype(
  ZuDeclVal<S &>().length(),
  ZuDeclVal<S &>().ensure(uint64_t{}), void())> {
  static void run(S &s, unsigned n) {
    s.ensure(uint64_t(s.length()) + n + unsigned(ZuTraits<S>::IsString));
  }
};

template <typename S, typename = void>
struct PatchSink_ : ZuFalse { };
template <typename S>
struct PatchSink_<S, decltype(
  ZuDeclVal<S &>().patch32(unsigned{}, uint32_t{}, uint8_t{}),
  void())> : ZuTrue { };

template <typename T, typename = void>
struct Reserve_ {
  static void run(T &, unsigned) { }
};
template <typename T>
struct Reserve_<T, decltype(ZuDeclVal<T &>().ensure(uint64_t{}), void())> {
  static void run(T &v, unsigned n) {
    v.ensure(uint64_t(n) + unsigned(ZuTraits<T>::IsString));
  }
};

template <typename T, typename V, typename = void>
struct Assign_ {
  static void run(T &dst, const V &src) { dst = T(src); }
};
template <typename T, typename V>
struct Assign_<T, V, decltype(
  ZuDeclVal<T &>() = ZuDeclVal<const V &>(), void())> {
  static void run(T &dst, const V &src) { dst = src; }
};

template <typename T, typename = void>
struct Clear_ {
  static void run(T &v) { v = T{}; }
};
template <typename T>
struct Clear_<T, decltype(ZuDeclVal<T &>().clear(), void())> {
  static void run(T &v) { v.clear(); }
};

template <typename S>
inline void put(S &s, State_ &context, uint8_t c)
{
  s << char(c);
  ++context.offset;
}

template <typename S>
inline void align(S &s, State_ &context, unsigned n)
{
  unsigned pad = unsigned((-context.offset) & (n - 1));
  if constexpr (ZuIsSame<S, Measure>{}) {
    (void)s;
    context.offset += pad;
  } else {
    while (pad--) put(s, context, 0);
  }
}

inline unsigned alignment(uint8_t code)
{
  switch (code) {
    case Type::Byte: case Type::Signature: case Type::Variant: return 1;
    case Type::Int16: case Type::UInt16: return 2;
    case Type::Bool: case Type::Int32: case Type::UInt32:
    case Type::String: case Type::ObjectPath:
    case Type::Array: case Type::UnixFD: return 4;
    default: return 8;
  }
}

template <typename S>
inline void emit(S &s, State_ &context, ZuBSpan value);

template <typename S, typename T>
inline void integer(S &s, State_ &context, T value)
{
  if constexpr (ZuIsSame<S, Measure>{}) {
    (void)s;
    (void)value;
    context.offset += sizeof(T);
  } else {
    bool swap = context.order == Order::Big;
    if constexpr (Zu_BIGENDIAN) swap = !swap;
    T encoded = swap ? ZuIntrin::bswap(value) : value;
    emit(s, context, {reinterpret_cast<const uint8_t *>(&encoded), sizeof(T)});
  }
}

template <typename S, typename = void>
struct Emit_ {
  static void run(S &s, State_ &context, ZuBSpan value) {
    unsigned n = value.length();
    for (unsigned i = 0; i < n; ++i) put(s, context, value[i]);
  }
};
template <typename S>
struct Emit_<S, decltype(ZuDeclVal<S &>() << ZuBSpan{}, void())> {
  static void run(S &s, State_ &context, ZuBSpan value) {
    s << value;
    context.offset += value.length();
  }
};

template <typename S>
inline void emit(S &s, State_ &context, ZuBSpan value)
{
  if constexpr (ZuIsSame<S, Measure>{}) {
    (void)s;
    context.offset += value.length();
  } else {
    Emit_<S>::run(s, context, value);
  }
}

struct Reader {
  View		view;
  const uint8_t	*p;
  const uint8_t	*end;
  Result		result;
  uint8_t	depth = 0;

  Reader(const View &view_)
  :
    view{view_}, p{view_.bytes.begin()}, end{view_.bytes.end()},
    result{view_.offset, Error::OK}
  {
    if (view_.order != Order::Little && view_.order != Order::Big)
      result.error = Error::Syntax;
  }

  bool fail(int error) {
    if (result)
      result = {view.offset + unsigned(p - view.bytes.begin()), error};
    return false;
  }
  bool align(unsigned n) {
    unsigned offset = view.offset + unsigned(p - view.bytes.begin());
    unsigned pad = (-offset) & (n - 1);
    if (unsigned(end - p) < pad) return fail(Error::Bounds);
    while (pad--) if (*p++) return fail(Error::Syntax);
    return true;
  }
  bool bytes(unsigned n, ZuBSpan &v) {
    if (unsigned(end - p) < n) return fail(Error::Bounds);
    v = {p, n}; p += n; return true;
  }
  template <typename T> bool integer(T &value) {
    if (unsigned(end - p) < sizeof(T)) return fail(Error::Bounds);
    T v;
    memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    bool swap = view.order == Order::Big;
    if constexpr (Zu_BIGENDIAN) swap = !swap;
    value = swap ? ZuIntrin::bswap(v) : v;
    return true;
  }
};

struct SaveDepth_ {
  State_	&context;
  uint8_t	depth;
  bool		valid;

  SaveDepth_(State_ &context_)
  : context{context_}, depth{context_.depth},
    valid{depth < Limit::ValueDepth}
  {
    if (valid)
      ++context.depth;
    else
      context.fail(Error::Overflow);
  }
  ~SaveDepth_() { context.depth = depth; }
  explicit operator bool() const { return valid; }
};

struct ReadDepth_ {
  Reader	&reader;
  uint8_t	depth;
  bool		valid;

  ReadDepth_(Reader &reader_)
  : reader{reader_}, depth{reader_.depth},
    valid{depth < Limit::ValueDepth}
  {
    if (valid)
      ++reader.depth;
    else
      reader.fail(Error::Syntax);
  }
  ~ReadDepth_() { reader.depth = depth; }
  explicit operator bool() const { return valid; }
};

ZfExtern bool utf8(ZuBSpan span);
ZfExtern bool objectPath(ZuCSpan path);

constexpr bool basicType(uint8_t c)
{
  switch (c) {
    case Type::Byte: case Type::Bool:
    case Type::Int16: case Type::UInt16:
    case Type::Int32: case Type::UInt32:
    case Type::Int64: case Type::UInt64:
    case Type::Double: case Type::String:
    case Type::ObjectPath: case Type::Signature:
    case Type::UnixFD: return true;
    default: return false;
  }
}

ZfExtern bool signatureType(
  const uint8_t *&p, const uint8_t *end, SignatureDepth depth, bool dict);
ZfExtern bool validSignature(ZuCSpan signature, bool single = false);
ZfExtern bool textData(Reader &reader, uint8_t code, ZuBSpan &value);
ZfExtern bool text(Reader &reader, uint8_t code, ZuBSpan &value);

inline unsigned signatureAlignment(const uint8_t *p)
{
  return *p == '(' || *p == '{' ? 8 : alignment(*p);
}

ZfExtern bool skipValue(
  Reader &reader, const uint8_t *&p, const uint8_t *end, unsigned depth);
ZfExtern bool skipValues(Reader &reader, ZuCSpan signature);
ZfExtern Result validate(const View &view, ZuCSpan expected);

template <typename S>
inline bool copyValue(S &, State_ &, Reader &,
  const uint8_t *&, const uint8_t *, unsigned);

template <typename S>
inline void writeText(S &s, State_ &context, uint8_t code, ZuBSpan value)
{
  unsigned n = value.length();
  if (code == Type::Signature)
    put(s, context, uint8_t(n));
  else {
    align(s, context, 4);
    integer(s, context, uint32_t(n));
  }
  emit(s, context, value);
  put(s, context, 0);
}

template <typename S>
inline bool copyValue(S &s, State_ &context, Reader &reader,
  const uint8_t *&p, const uint8_t *end, unsigned depth)
{
  if (p == end) return reader.fail(Error::Syntax);
  if (depth > Limit::ValueDepth) return reader.fail(Error::Overflow);
  uint8_t code = *p++;
  switch (code) {
    case Type::Byte: {
      if (reader.p == reader.end) return reader.fail(Error::Bounds);
      put(s, context, *reader.p++);
      return true;
    }
    case Type::Bool: case Type::Int32: case Type::UInt32: {
      uint32_t value;
      if (!reader.align(4) || !reader.integer(value)) return false;
      if (code == Type::Bool && value > 1)
	return reader.fail(Error::Syntax);
      align(s, context, 4);
      integer(s, context, value);
      return true;
    }
    case Type::Int16: case Type::UInt16: {
      uint16_t value;
      if (!reader.align(2) || !reader.integer(value)) return false;
      align(s, context, 2);
      integer(s, context, value);
      return true;
    }
    case Type::Int64: case Type::UInt64: case Type::Double: {
      uint64_t value;
      if (!reader.align(8) || !reader.integer(value)) return false;
      align(s, context, 8);
      integer(s, context, value);
      return true;
    }
    case Type::String: case Type::ObjectPath: case Type::Signature: {
      ZuBSpan value;
      if (!text(reader, code, value)) return false;
      writeText(s, context, code, value);
      return true;
    }
    case Type::UnixFD:
      return reader.fail(Error::Unsupported);
    case Type::Array: {
      auto element = p;
      if (!signatureType(p, end, {}, true))
	return reader.fail(Error::Syntax);
      auto elementEnd = p;
      uint32_t n;
      if (!reader.align(4) || !reader.integer(n) ||
	  !reader.align(signatureAlignment(element))) return false;
      if (n > Limit::Array)
	return reader.fail(Error::Overflow);
      if (uint64_t(reader.end - reader.p) < n)
	return reader.fail(Error::Bounds);
      auto outerEnd = reader.end;
      reader.end = reader.p + n;
      align(s, context, 4);
      integer(s, context, n);
      align(s, context, signatureAlignment(element));
      uint64_t begin = context.offset;
      while (reader.result && reader.p != reader.end) {
	auto q = element;
	if (!copyValue(s, context, reader, q, elementEnd, depth + 1) ||
	    q != elementEnd) break;
      }
      bool ok = bool(reader.result) && reader.p == reader.end;
      reader.end = outerEnd;
      if (!ok) return false;
      if (context.offset - begin != n) return reader.fail(Error::Syntax);
      return true;
    }
    case '(':
      if (!reader.align(8)) return false;
      align(s, context, 8);
      if (p == end || *p == ')') return reader.fail(Error::Syntax);
      while (p != end && *p != ')')
	if (!copyValue(s, context, reader, p, end, depth + 1)) return false;
      if (p == end) return reader.fail(Error::Syntax);
      ++p;
      return true;
    case '{':
      if (!reader.align(8) || p == end || !basicType(*p) ||
	  *p == Type::UnixFD) return reader.fail(Error::Syntax);
      align(s, context, 8);
      if (!copyValue(s, context, reader, p, end, depth + 1) ||
	  !copyValue(s, context, reader, p, end, depth + 1) ||
	  p == end || *p++ != '}') return reader.fail(Error::Syntax);
      return true;
    case Type::Variant: {
      ZuBSpan signature;
      if (!text(reader, Type::Signature, signature)) return false;
      ZuCSpan chars{signature};
      if (!validSignature(chars, true)) return reader.fail(Error::Syntax);
      writeText(s, context, Type::Signature, signature);
      auto q = signature.begin();
      auto qEnd = signature.end();
      return copyValue(s, context, reader, q, qEnd, depth + 1) && q == qEnd;
    }
    default:
      return reader.fail(Error::Syntax);
  }
}

template <typename S>
inline bool saveAny(S &s, State_ &context, const Any &any)
{
  if (any.bytes.length() > UINT_MAX - any.offset)
    return context.fail(Error::Overflow);
  if constexpr (ZuIsSame<S, Measure>{})
    if (!validSignature(any.signature, true))
      return context.fail(Error::Type);
  Reader reader{{any.bytes, any.signature, any.offset, any.order}};
  ZuBSpan signature{any.signature};
  auto p = signature.begin();
  auto end = signature.end();
  if (!reader.result ||
      !copyValue(s, context, reader, p, end, context.depth))
    return context.fail(reader.result.error);
  if (p != end || reader.p != reader.end)
    return context.fail(Error::Syntax);
  return true;
}

constexpr uint8_t type(unsigned code)
{
  switch (code) {
    case ZfFieldTC::Bool: return Type::Bool;
    case ZfFieldTC::Int8: return Type::Byte;
    case ZfFieldTC::UInt8: return Type::Byte;
    case ZfFieldTC::Int16: return Type::Int16;
    case ZfFieldTC::UInt16: return Type::UInt16;
    case ZfFieldTC::Int32: return Type::Int32;
    case ZfFieldTC::UInt32: return Type::UInt32;
    case ZfFieldTC::Int64: return Type::Int64;
    case ZfFieldTC::UInt64: return Type::UInt64;
    case ZfFieldTC::Float: return Type::Double;
    case ZfFieldTC::CString: case ZfFieldTC::String: return Type::String;
    default: return 0;
  }
}

constexpr bool validStringType(uint8_t wire)
{
  return wire == Type::String || wire == Type::ObjectPath ||
    wire == Type::Signature;
}

template <typename Field, typename S> constexpr void signatureField(S &);
template <typename S, typename Field>
bool saveField(S &, State_ &, const typename Field::T &);
template <typename Field>
bool loadField(Reader &, typename Field::T &);
template <typename Field>
bool skipField(Reader &);
template <typename Field>
typename Field::T loadFieldValue(Reader &);

template <typename T_, unsigned Code_, typename Props_ = ZuTypeList<>>
struct ValueField {
  using T = T_;
  using Props = Props_;
  struct Type { enum { Code = Code_ }; };
};

template <typename Field>
constexpr unsigned fieldAlignment();
template <typename Field>
constexpr unsigned fieldSize();

struct AsObject;
template <unsigned ElemCode> struct AsArray;
template <typename T> constexpr unsigned rawCode();
// Outside ZfFieldTC's int8_t range, including CString == 0.
namespace MapCode { enum { Infer = 255 }; }
template <unsigned ValCode = MapCode::Infer, typename ValProps = ZuTypeList<>,
  unsigned KeyCode = MapCode::Infer, typename KeyProps = ZuTypeList<>>
struct AsMap;
struct AsVariant;

} // ZfDBUS

ZfDBUS::AsObject ZfDBUS_Fmt(...);

namespace ZfDBUS {

template <typename O>
using As = decltype(ZfDBUS_Fmt(ZuDeclVal<O *>()));

template <typename Field>
constexpr unsigned fieldAlignment()
{
  using T = typename Field::T;
  using Props = typename Field::Props;
  constexpr unsigned Code = Field::Type::Code;
  if constexpr (ZfFieldTC::IsVec<Code>{} || Code == ZfFieldTC::Bytes)
    return 4;
  else if constexpr (Code == ZfFieldTC::UDT)
    return As<T>::template Handler<T, ZuFacet::DBUS, Props>::Alignment;
  else if constexpr (Code == ZfFieldTC::CString || Code == ZfFieldTC::String) {
    constexpr uint8_t TypeCode =
      ZuFieldProp::DBUS::GetType<Props, Type::String>{};
    return TypeCode == Type::Signature ? 1 : 4;
  } else
    return alignment(type(Code));
}

template <typename O, typename Facet = ZuFacet::DBUS>
Signature signature();

template <typename Field, typename S>
constexpr void signatureField(S &s)
{
  using T = typename Field::T;
  using Props = typename Field::Props;
  constexpr unsigned Code = Field::Type::Code;
  if constexpr (ZfFieldTC::IsVec<Code>{}) {
    s << 'a';
    enum { ElemCode = ZfFieldTC::Elem<Code>{} };
    using Elem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
    using ElemProps = ZuFieldProp::DBUS::GetElemProps<Props>;
    signatureField<ValueField<Elem, ElemCode, ElemProps>>(s);
  } else if constexpr (Code == ZfFieldTC::Bytes) {
    s << "ay";
  } else if constexpr (Code == ZfFieldTC::UDT) {
    As<T>::template Handler<T, ZuFacet::DBUS, Props>::template
      signature<T, ZuFacet::DBUS, Props>(s);
  } else if constexpr (
      Code == ZfFieldTC::CString || Code == ZfFieldTC::String) {
    constexpr uint8_t TypeCode =
      ZuFieldProp::DBUS::GetType<Props, Type::String>{};
    ZuAssert(validStringType(TypeCode));
    s << char(TypeCode);
  } else {
    constexpr uint8_t TypeCode = type(Code);
    ZuAssert(TypeCode);
    s << char(TypeCode);
  }
}

template <typename Field>
constexpr unsigned fieldSize()
{
  constexpr unsigned Code = Field::Type::Code;
  switch (Code) {
    case ZfFieldTC::Int8: case ZfFieldTC::UInt8: return 1;
    case ZfFieldTC::Int16: case ZfFieldTC::UInt16: return 2;
    case ZfFieldTC::Bool:
    case ZfFieldTC::Int32: case ZfFieldTC::UInt32: return 4;
    case ZfFieldTC::Int64: case ZfFieldTC::UInt64:
    case ZfFieldTC::Float: return 8;
    default: return 0;
  }
}

template <typename Field>
constexpr bool rawField()
{
  using T = typename Field::T;
  constexpr unsigned Code = Field::Type::Code;
  // The wire type is fixed by Code; these checks only select the native-order
  // bulk-copy optimization when the C++ representation has the same width.
  return ZuTraits<T>::IsPrimitive && sizeof(T) == fieldSize<Field>() &&
    Code != ZfFieldTC::Bool &&
    (Code != ZfFieldTC::Float || sizeof(T) == sizeof(double));
}

inline bool nativeOrder(uint8_t order)
{
  if constexpr (Zu_BIGENDIAN) return order == Order::Big;
  else return order == Order::Little;
}

template <typename O, typename = void>
struct LoadRaw_ {
  static bool run(O &, Reader &, unsigned, unsigned) { return false; }
};
template <typename O>
struct LoadRaw_<O, decltype(
  ZuDeclVal<O &>().length(uint64_t{}, false),
  ZuTraits<O>::data(ZuDeclVal<O &>()), void())> {
  static bool run(O &o, Reader &reader, unsigned count, unsigned n) {
    o.length(count, false);
    if (n) memcpy(ZuTraits<O>::data(o), reader.p, n);
    reader.p += n;
    return true;
  }
};

template <typename S, typename O, typename ElemField>
bool saveArrayElems(S &s, State_ &context, const O &o, unsigned n)
{
  if constexpr (rawField<ElemField>()) {
    if (nativeOrder(context.order)) {
      if (n) emit(s, context, {
	reinterpret_cast<const uint8_t *>(ZuTraits<O>::data(o)),
	unsigned(uint64_t(n) * sizeof(typename ElemField::T))});
      return true;
    }
  }
  for (unsigned i = 0; i < n; ++i)
    if (!saveField<S, ElemField>(s, context, o[i])) return false;
  return true;
}

template <typename S, typename O, typename ElemField>
bool saveArray(S &s, State_ &context, const O &o)
{
  SaveDepth_ nesting{context};
  if (!nesting) return false;
  if (ZuTraits<O>::length(o) > Limit::Array)
    return context.fail(Error::Overflow);
  unsigned n = ZuTraits<O>::length(o);
  if constexpr (PatchSink_<S>{}) {
    align(s, context, 4);
    unsigned patchAt = unsigned(context.offset);
    integer(s, context, uint32_t(0));
    align(s, context, fieldAlignment<ElemField>());
    uint64_t begin = context.offset;
    if (!saveArrayElems<S, O, ElemField>(s, context, o, n)) return false;
    uint64_t length = context.offset - begin;
    if (length > Limit::Array) return context.fail(Error::Overflow);
    s.patch32(patchAt, uint32_t(length), context.order);
    return true;
  }
  State_ measured = context;
  Measure measure;
  align(measure, measured, 4);
  integer(measure, measured, uint32_t(0));
  align(measure, measured, fieldAlignment<ElemField>());
  uint64_t begin = measured.offset;
  if constexpr (fieldSize<ElemField>()) {
    measured.offset += uint64_t(n) * fieldSize<ElemField>();
  } else {
    for (unsigned i = 0; i < n; ++i) {
      if (!saveField<Measure, ElemField>(measure, measured, o[i])) {
	context.fail(measured.error == Error::OK ?
	  Error::Unsupported : measured.error);
	return false;
      }
    }
  }
  uint64_t length = measured.offset - begin;
  if (length > Limit::Array) return context.fail(Error::Overflow);
  if constexpr (ZuIsSame<S, Measure>{}) {
    context.offset = measured.offset;
    return true;
  }
  align(s, context, 4);
  integer(s, context, uint32_t(length));
  align(s, context, fieldAlignment<ElemField>());
  return saveArrayElems<S, O, ElemField>(s, context, o, n);
}

template <typename O, typename ElemField>
bool loadArray(Reader &reader, O &o)
{
  uint32_t n;
  if (!reader.align(4) || !reader.integer(n) ||
      !reader.align(fieldAlignment<ElemField>())) return false;
  if (n > Limit::Array) return reader.fail(Error::Overflow);
  if (uint64_t(reader.end - reader.p) < n)
    return reader.fail(Error::Bounds);
  auto outerEnd = reader.end;
  reader.end = reader.p + n;
  unsigned count = 0;
  if constexpr (rawField<ElemField>()) {
    constexpr unsigned Size = fieldSize<ElemField>();
    if (n % Size) {
      reader.end = outerEnd;
      return reader.fail(Error::Syntax);
    }
    count = n / Size;
  } else {
    Reader scanned = reader;
    while (scanned.result && scanned.p != scanned.end) {
      auto before = scanned.p;
      if (!skipField<ElemField>(scanned)) break;
      if (scanned.p == before) {
	scanned.fail(Error::Syntax);
	break;
      }
      ++count;
    }
    if (!scanned.result || scanned.p != scanned.end) {
      reader.result = scanned.result;
      reader.end = outerEnd;
      return false;
    }
  }
  Clear_<O>::run(o);
  if constexpr (rawField<ElemField>()) {
    if (nativeOrder(reader.view.order) &&
	LoadRaw_<O>::run(o, reader, count, n)) {
      reader.end = outerEnd;
      return true;
    }
  }
  Reserve_<O>::run(o, count);
  while (reader.result && reader.p != reader.end) {
    auto before = reader.p;
    auto value = loadFieldValue<ElemField>(reader);
    if (!reader.result) break;
    if (reader.p == before) {
      reader.fail(Error::Syntax);
      break;
    }
    o << ZuMv(value);
  }
  bool ok = bool(reader.result) && reader.p == reader.end;
  reader.end = outerEnd;
  return ok;
}

template <typename ElemField>
bool skipArray(Reader &reader)
{
  ReadDepth_ nesting{reader};
  if (!nesting) return false;
  uint32_t n;
  if (!reader.align(4) || !reader.integer(n) ||
      !reader.align(fieldAlignment<ElemField>())) return false;
  if (n > Limit::Array) return reader.fail(Error::Overflow);
  if (uint64_t(reader.end - reader.p) < n)
    return reader.fail(Error::Bounds);
  auto outerEnd = reader.end;
  reader.end = reader.p + n;
  if constexpr (rawField<ElemField>()) {
    constexpr unsigned Size = fieldSize<ElemField>();
    if (n % Size) {
      reader.end = outerEnd;
      return reader.fail(Error::Syntax);
    }
    reader.p += n;
    reader.end = outerEnd;
    return true;
  }
  while (reader.result && reader.p != reader.end) {
    auto before = reader.p;
    if (!skipField<ElemField>(reader)) break;
    if (reader.p == before) {
      reader.fail(Error::Syntax);
      break;
    }
  }
  bool ok = bool(reader.result) && reader.p == reader.end;
  reader.end = outerEnd;
  return ok;
}

template <typename S, typename Field>
bool saveField(S &s, State_ &context, const typename Field::T &value)
{
  using T = typename Field::T;
  using Props = typename Field::Props;
  constexpr unsigned Code = Field::Type::Code;
  if constexpr (ZfFieldTC::IsVec<Code>{}) {
    enum { ElemCode = ZfFieldTC::Elem<Code>{} };
    using Elem = ZuDecay<decltype(value[0])>;
    using ElemProps = ZuFieldProp::DBUS::GetElemProps<Props>;
    using ElemField = ValueField<Elem, ElemCode, ElemProps>;
    return saveArray<S, T, ElemField>(s, context, value);
  } else if constexpr (Code == ZfFieldTC::CString || Code == ZfFieldTC::String) {
    constexpr uint8_t TypeCode =
      ZuFieldProp::DBUS::GetType<Props, Type::String>{};
    ZuCSpan v{value};
    if (v.length() > UINT_MAX) return context.fail(Error::Overflow);
    unsigned n = v.length();
    if constexpr (ZuIsSame<S, Measure>{}) {
      if constexpr (TypeCode == Type::ObjectPath) {
	if (!objectPath(v)) return context.fail(Error::Syntax);
      } else if constexpr (TypeCode == Type::Signature) {
	if (!validSignature(v)) return context.fail(Error::Syntax);
      } else if (!utf8(ZuBSpan(v)))
	return context.fail(Error::Syntax);
    }
    if constexpr (TypeCode == Type::Signature) {
      if (n > UINT8_MAX) return context.fail(Error::Overflow);
      put(s, context, uint8_t(n));
    } else {
      align(s, context, 4);
      integer(s, context, uint32_t(n));
    }
    emit(s, context, ZuBSpan(v));
    put(s, context, 0); return true;
  } else if constexpr (Code == ZfFieldTC::Bytes) {
    ZuBSpan v{value};
    if (v.length() > Limit::Array) return context.fail(Error::Overflow);
    unsigned n = v.length();
    align(s, context, 4); integer(s, context, uint32_t(n));
    emit(s, context, v);
    return true;
  } else if constexpr (Code == ZfFieldTC::Bool) {
    align(s, context, 4); integer(s, context, uint32_t(!!value)); return true;
  } else if constexpr (Code == ZfFieldTC::Int8) {
    put(s, context, uint8_t(value)); return true;
  } else if constexpr (Code == ZfFieldTC::UInt8) {
    put(s, context, uint8_t(value)); return true;
  } else if constexpr (Code == ZfFieldTC::Int16 || Code == ZfFieldTC::UInt16 ||
      Code == ZfFieldTC::Int32 || Code == ZfFieldTC::UInt32 ||
      Code == ZfFieldTC::Int64 || Code == ZfFieldTC::UInt64) {
    align(s, context, alignment(type(Code)));
    integer(s, context, value);
    return true;
  } else if constexpr (Code == ZfFieldTC::Float) {
    align(s, context, 8); integer(s, context, ZuPun<double, uint64_t>(double(value)).out);
    return true;
  } else if constexpr (Code == ZfFieldTC::UDT) {
    return As<T>::template Handler<T, ZuFacet::DBUS, Props>::save(
      s, context, value);
  } else {
    ZuAssert(Code == ZfFieldTC::UDT);
  }
}

template <typename Field>
bool loadField(Reader &reader, typename Field::T &value)
{
  using T = typename Field::T;
  using Props = typename Field::Props;
  constexpr unsigned Code = Field::Type::Code;
  if constexpr (ZfFieldTC::IsVec<Code>{}) {
    enum { ElemCode = ZfFieldTC::Elem<Code>{} };
    using Elem = ZuDecay<decltype(value[0])>;
    using ElemProps = ZuFieldProp::DBUS::GetElemProps<Props>;
    using ElemField = ValueField<Elem, ElemCode, ElemProps>;
    return loadArray<T, ElemField>(reader, value);
  } else if constexpr (Code == ZfFieldTC::CString || Code == ZfFieldTC::String) {
    constexpr uint8_t TypeCode =
      ZuFieldProp::DBUS::GetType<Props, Type::String>{};
    ZuBSpan v;
    if (!textData(reader, TypeCode, v)) return false;
    ZuCSpan text{v};
    if constexpr (Code == ZfFieldTC::CString)
      value = text.begin();
    else
      Assign_<T, ZuCSpan>::run(value, text);
    return true;
  } else if constexpr (Code == ZfFieldTC::Bytes) {
    uint32_t n; ZuBSpan v;
    if (!reader.align(4) || !reader.integer(n)) return false;
    if (n > Limit::Array) return reader.fail(Error::Overflow);
    if (!reader.bytes(n, v)) return false;
    Assign_<T, ZuBSpan>::run(value, v); return true;
  } else if constexpr (Code == ZfFieldTC::Bool) {
    uint32_t v;
    if (!reader.align(4) || !reader.integer(v) || v > 1) return reader.fail(Error::Syntax);
    value = T(v); return true;
  } else if constexpr (Code == ZfFieldTC::Int8) {
    if (reader.p == reader.end) return reader.fail(Error::Bounds);
    value = T(*reader.p++); return true;
  } else if constexpr (Code == ZfFieldTC::UInt8) {
    if (reader.p == reader.end) return reader.fail(Error::Bounds);
    value = T(*reader.p++); return true;
  } else if constexpr (Code == ZfFieldTC::Int16 || Code == ZfFieldTC::UInt16 ||
      Code == ZfFieldTC::Int32 || Code == ZfFieldTC::UInt32 ||
      Code == ZfFieldTC::Int64 || Code == ZfFieldTC::UInt64) {
    return reader.align(alignment(type(Code))) && reader.integer(value);
  } else if constexpr (Code == ZfFieldTC::Float) {
    uint64_t v;
    if (!reader.align(8) || !reader.integer(v)) return false;
    value = T(ZuPun<uint64_t, double>(v).out);
    return true;
  } else if constexpr (Code == ZfFieldTC::UDT) {
    return As<T>::template Handler<T, ZuFacet::DBUS, Props>::load(reader, value);
  } else {
    return reader.fail(Error::Unsupported);
  }
}

template <typename Field>
bool skipField(Reader &reader)
{
  using T = typename Field::T;
  using Props = typename Field::Props;
  constexpr unsigned Code = Field::Type::Code;
  if constexpr (ZfFieldTC::IsVec<Code>{}) {
    enum { ElemCode = ZfFieldTC::Elem<Code>{} };
    using Elem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
    using ElemProps = ZuFieldProp::DBUS::GetElemProps<Props>;
    using ElemField = ValueField<Elem, ElemCode, ElemProps>;
    return skipArray<ElemField>(reader);
  } else if constexpr (Code == ZfFieldTC::CString || Code == ZfFieldTC::String) {
    constexpr uint8_t TypeCode =
      ZuFieldProp::DBUS::GetType<Props, Type::String>{};
    ZuBSpan value;
    return text(reader, TypeCode, value);
  } else if constexpr (Code == ZfFieldTC::Bytes) {
    uint32_t n; ZuBSpan v;
    if (!reader.align(4) || !reader.integer(n)) return false;
    return n <= Limit::Array ? reader.bytes(n, v) :
      reader.fail(Error::Overflow);
  } else if constexpr (Code == ZfFieldTC::Bool) {
    uint32_t v;
    if (!reader.align(4) || !reader.integer(v)) return false;
    return v <= 1 || reader.fail(Error::Syntax);
  } else if constexpr (Code == ZfFieldTC::Int8 || Code == ZfFieldTC::UInt8) {
    if (reader.p == reader.end) return reader.fail(Error::Bounds);
    ++reader.p;
    return true;
  } else if constexpr (Code == ZfFieldTC::Int16 || Code == ZfFieldTC::UInt16 ||
      Code == ZfFieldTC::Int32 || Code == ZfFieldTC::UInt32 ||
      Code == ZfFieldTC::Int64 || Code == ZfFieldTC::UInt64) {
    if (!reader.align(fieldAlignment<Field>())) return false;
    ZuBSpan ignored;
    return reader.bytes(Code == ZfFieldTC::Int16 || Code == ZfFieldTC::UInt16 ?
      2 : Code == ZfFieldTC::Int32 || Code == ZfFieldTC::UInt32 ? 4 : 8,
      ignored);
  } else if constexpr (Code == ZfFieldTC::Float) {
    if (!reader.align(8)) return false;
    ZuBSpan ignored;
    return reader.bytes(8, ignored);
  } else if constexpr (Code == ZfFieldTC::UDT) {
    return As<T>::template Handler<T, ZuFacet::DBUS, Props>::skip(reader);
  } else {
    return reader.fail(Error::Unsupported);
  }
}

template <typename Field>
typename Field::T loadFieldValue(Reader &reader)
{
  using T = typename Field::T;
  if constexpr (Field::Type::Code == ZfFieldTC::UDT)
    return As<T>::template Handler<T, ZuFacet::DBUS,
      typename Field::Props>::loadCtor(reader);
  else {
    T value;
    loadField<Field>(reader, value);
    return value;
  }
}

template <typename Impl>
Result checkView(const View &view)
{
  auto signature = ZtScratch(Signature, Limit::Signature + 1);
  Impl::signatureTop(signature);
  if (view.signature != signature || !validSignature(signature))
    return {view.offset, Error::Type};
  if (view.bytes.length() > UINT_MAX - view.offset)
    return {view.offset, Error::Overflow};
  Reader reader{view};
  if (reader.result) Impl::skipTop(reader);
  if (reader.result && reader.p != reader.end) reader.fail(Error::Trailing);
  if (!reader.result) return reader.result;
  return {view.offset + unsigned(view.bytes.length()), Error::OK};
}

struct AsObject {
  template <typename O_, typename Facet, typename Props = ZuTypeList<>>
  struct Handler {
    enum { Alignment = 8 };
    using O = O_;
    using WireFields = ZuFields<O, Facet>;
    using CtorFields_ = ZuTypeGrep<ZfFieldFilter::Ctor, WireFields>;
    template <typename Field>
    using CtorIndex = ZuFieldProp::GetCtor<typename Field::Props>;
    using CtorFields = ZuTypeSort<CtorIndex, CtorFields_>;
    using InitFields = ZuTypeGrep<ZfFieldFilter::Init, WireFields>;

    View		view;
    Result		status;

    template <typename T, typename F, typename P>
    static constexpr void signature(auto &s) {
      ZuAssert(WireFields::N);
      s << '(';
      ZuUnroll::all<WireFields>([&s]<typename Field>() {
	signatureField<Field>(s);
      });
      s << ')';
    }
    static constexpr void signatureTop(auto &s) {
      ZuUnroll::all<WireFields>([&s]<typename Field>() {
	signatureField<Field>(s);
      });
    }
    template <typename S>
    static bool saveFields(S &s, State_ &context, const O &o) {
      bool ok = true;
      ZuUnroll::all<WireFields>([&s, &context, &o, &ok]<typename Field>() {
	if (ok) ok = saveField<S, Field>(
	  s, context, Field::get(o));
      });
      return ok;
    }
    template <typename S>
    static bool save(S &s, State_ &context, const O &o) {
      SaveDepth_ nesting{context};
      if (!nesting) return false;
      align(s, context, 8);
      return saveFields(s, context, o);
    }
    template <typename S>
    static bool saveTop(S &s, State_ &context, const O &o) {
      return saveFields(s, context, o);
    }
    Handler(const View &view_) : view(view_), status(check(view_)) { }
    explicit operator bool() const { return bool(status); }
    Result result() const { return status; }
    static Result check(const View &view) {
      return checkView<Handler>(view);
    }

    static bool indexFields(Reader &reader, FieldViews &fields) {
      ZuUnroll::all<WireFields>([&reader, &fields]<typename Field>() {
	if (!reader.result) return;
	auto begin = reader.p;
	unsigned offset = reader.view.offset +
	  unsigned(begin - reader.view.bytes.begin());
	if (!skipField<Field>(reader)) return;
	fields << View{{begin, unsigned(reader.p - begin)}, {}, offset,
	  reader.view.order};
      });
      return bool(reader.result);
    }
    template <typename Field>
    static typename Field::T value(const FieldViews &fields) {
      constexpr unsigned I = ZuTypeIndex<Field, WireFields>{};
      Reader reader{fields[I]};
      auto value = loadFieldValue<Field>(reader);
      ZmAssert_(reader.result && reader.p == reader.end);
      return value;
    }

    template <typename ...Field>
    struct Ctor {
      template <typename ...Args>
      static O ctor(const FieldViews &fields, Args &&...args) {
	return O(ZuFwd<Args>(args)..., value<Field>(fields)...);
      }
      template <typename ...Args>
      static O *alloc(const FieldViews &fields, Args &&...args) {
	return new O(ZuFwd<Args>(args)...,
	  value<Field>(fields)...);
      }
      template <typename ...Args>
      static void new_(void *ptr, const FieldViews &fields, Args &&...args) {
	new (ptr) O(ZuFwd<Args>(args)...,
	  value<Field>(fields)...);
      }
    };

    template <typename ...Args>
    O ctor(Args &&...args) const {
      ZmAssert_(status);
      Reader reader{view};
      auto fields = ZtScratch(FieldViews, WireFields::N);
      ZmAssert_(indexFields(reader, fields) && reader.p == reader.end);
      O o = ZuTypeApply<Ctor, CtorFields>::ctor(
	fields, ZuFwd<Args>(args)...);
      ZuUnroll::all<InitFields>([&fields, &o]<typename Field>() {
	Field::set(o, value<Field>(fields));
      });
      return o;
    }
    template <typename ...Args>
    O *alloc(Args &&...args) const {
      ZmAssert_(status);
      Reader reader{view};
      auto fields = ZtScratch(FieldViews, WireFields::N);
      ZmAssert_(indexFields(reader, fields) && reader.p == reader.end);
      O *o = ZuTypeApply<Ctor, CtorFields>::alloc(
	fields, ZuFwd<Args>(args)...);
      ZuUnroll::all<InitFields>([&fields, o]<typename Field>() {
	Field::set(*o, value<Field>(fields));
      });
      return o;
    }
    template <typename ...Args>
    void new_(void *p, Args &&...args) const {
      ZmAssert_(status);
      Reader reader{view};
      auto fields = ZtScratch(FieldViews, WireFields::N);
      ZmAssert_(indexFields(reader, fields) && reader.p == reader.end);
      ZuTypeApply<Ctor, CtorFields>::new_(
	p, fields, ZuFwd<Args>(args)...);
      O &o = *static_cast<O *>(p);
      ZuUnroll::all<InitFields>([&fields, &o]<typename Field>() {
	Field::set(o, value<Field>(fields));
      });
    }
    void load(O &o) const {
      if (!status) return;
      Reader reader{view};
      ZmAssert_(loadFields(reader, o) && reader.p == reader.end);
    }
    static bool skipFields(Reader &reader) {
      ZuUnroll::all<WireFields>([&reader]<typename Field>() {
	if (reader.result) skipField<Field>(reader);
      });
      return bool(reader.result);
    }
    static bool skip(Reader &reader) {
      ReadDepth_ nesting{reader};
      if (!nesting) return false;
      if (!reader.align(8)) return false;
      return skipFields(reader);
    }
    static bool skipTop(Reader &reader) { return skipFields(reader); }
    static bool loadFields(Reader &reader, O &o) {
      ZuUnroll::all<WireFields>([&reader, &o]<typename Field>() {
	if (!reader.result) return;
	if constexpr (ZfFieldFilter::Load<Field>{}) {
	  if constexpr (ZuIsSame<decltype(Field::get(o)),
	      typename Field::T &>{}) {
	    loadField<Field>(reader, Field::get(o));
	  } else {
	    auto value = loadFieldValue<Field>(reader);
	    if (reader.result) Field::set(o, ZuMv(value));
	  }
	} else {
	  skipField<Field>(reader);
	}
      });
      return bool(reader.result);
    }
    static bool load(Reader &reader, O &o) {
      if (!reader.align(8)) return false;
      return loadFields(reader, o);
    }
    static O loadCtor(Reader &reader) {
      ZmAssert_(reader.align(8));
      auto fields = ZtScratch(FieldViews, WireFields::N);
      ZmAssert_(indexFields(reader, fields));
      O o = ZuTypeApply<Ctor, CtorFields>::ctor(fields);
      ZuUnroll::all<InitFields>([&fields, &o]<typename Field>() {
	Field::set(o, value<Field>(fields));
      });
      return o;
    }
  };
};

template <typename Impl, typename O, typename Facet>
struct Checked {
  View		view;
  Result	status;

  Checked(const View &view_) : view(view_), status(Impl::check(view_)) { }
  explicit operator bool() const { return bool(status); }
  Result result() const { return status; }

  template <typename ...Args>
  O ctor(Args &&...args) const {
    ZmAssert_(status);
    Reader reader{view};
    if constexpr (!sizeof...(Args)) {
      return Impl::loadCtor(reader);
    } else {
      O o{ZuFwd<Args>(args)...};
      Impl::loadValue(reader, o);
      return o;
    }
  }
  template <typename ...Args>
  O *alloc(Args &&...args) const {
    ZmAssert_(status);
    O *o = new O{ZuFwd<Args>(args)...};
    Reader reader{view};
    Impl::loadValue(reader, *o);
    return o;
  }
  template <typename ...Args>
  void new_(void *ptr, Args &&...args) const {
    ZmAssert_(status);
    O *o = new (ptr) O{ZuFwd<Args>(args)...};
    Reader reader{view};
    Impl::loadValue(reader, *o);
  }
  void load(O &o) const {
    if (!status) return;
    Reader reader{view};
    Impl::loadValue(reader, o);
  }
};

template <unsigned ElemCode>
struct AsArray {
  template <typename O_, typename Facet, typename Props = ZuTypeList<>>
  struct Handler : public Checked<Handler<O_, Facet, Props>, O_, Facet> {
    enum { Alignment = 4 };
    using O = O_;
    using Base = Checked<Handler, O, Facet>;
    using Base::Base;
    using Base::load;
    using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
    using ElemProps = ZuFieldProp::DBUS::GetElemProps<Props>;
    using ElemField = ValueField<Elem, ElemCode, ElemProps>;

    template <typename T, typename F, typename P>
    static constexpr void signature(auto &s) {
      s << 'a';
      signatureField<ElemField>(s);
    }
    static constexpr void signatureTop(auto &s) {
      signature<O, Facet, Props>(s);
    }
    template <typename S>
    static bool saveValue(S &s, State_ &context, const O &o) {
      return saveArray<S, O, ElemField>(s, context, o);
    }
    template <typename S>
    static bool save(S &s, State_ &context, const O &o) {
      return saveValue(s, context, o);
    }
    template <typename S>
    static bool saveTop(S &s, State_ &context, const O &o) {
      return saveValue(s, context, o);
    }
    static bool load_(Reader &reader, O &o) {
      return loadArray<O, ElemField>(reader, o);
    }
    static bool loadValue(Reader &reader, O &o) {
      return load_(reader, o);
    }
    static O loadCtor(Reader &reader) {
      O o;
      ZmAssert_(load_(reader, o));
      return o;
    }
    static bool load(Reader &reader, O &o) {
      return loadValue(reader, o);
    }
    static bool skip(Reader &reader) {
      return skipArray<ElemField>(reader);
    }
    static bool skipTop(Reader &reader) { return skip(reader); }
    static Result check(const View &view) {
      return checkView<Handler>(view);
    }
  };
};

template <unsigned ValCode, typename ValProps,
  unsigned KeyCode, typename KeyProps>
struct AsMap {
  template <typename O_, typename Facet, typename Props = ZuTypeList<>>
  struct Handler : public Checked<Handler<O_, Facet, Props>, O_, Facet> {
    enum { Alignment = 4 };
    using O = O_;
    using Base = Checked<Handler, O, Facet>;
    using Base::Base;
    using Base::load;
    using Key = typename O::Key;
    using Val = typename O::Val;
    using KeyField = ValueField<Key,
      KeyCode == MapCode::Infer ? rawCode<Key>() : KeyCode, KeyProps>;
    using ValField = ValueField<Val,
      ValCode == MapCode::Infer ? rawCode<Val>() : ValCode, ValProps>;

    template <typename T, typename F, typename P>
    static constexpr void signature(auto &s) {
      constexpr unsigned KeyTC = KeyField::Type::Code;
      constexpr uint8_t KeyType = [] {
	if constexpr (KeyTC == ZfFieldTC::CString ||
	    KeyTC == ZfFieldTC::String)
	  return ZuFieldProp::DBUS::GetType<
	    typename KeyField::Props, Type::String>{};
	else
	  return type(KeyTC);
      }();
      ZuAssert(basicType(KeyType) && KeyType != Type::UnixFD);
      s << "a{";
      signatureField<KeyField>(s);
      signatureField<ValField>(s);
      s << '}';
    }
    static constexpr void signatureTop(auto &s) {
      signature<O, Facet, Props>(s);
    }
    template <typename S>
    static bool saveEntries(S &s, State_ &context, const O &o) {
      auto i = o.citer();
      while (auto node = i()) {
	SaveDepth_ nesting{context};
	if (!nesting) return false;
	align(s, context, 8);
	if (!saveField<S, KeyField>(s, context, node->key()) ||
	    !saveField<S, ValField>(s, context, node->val())) return false;
      }
      return true;
    }
    template <typename S>
    static bool saveValue(S &s, State_ &context, const O &o) {
      SaveDepth_ nesting{context};
      if (!nesting) return false;
      if constexpr (PatchSink_<S>{}) {
	align(s, context, 4);
	unsigned patchAt = unsigned(context.offset);
	integer(s, context, uint32_t(0));
	align(s, context, 8);
	uint64_t begin = context.offset;
	if (!saveEntries(s, context, o)) return false;
	uint64_t length = context.offset - begin;
	if (length > Limit::Array) return context.fail(Error::Overflow);
	s.patch32(patchAt, uint32_t(length), context.order);
	return true;
      }
      State_ measured = context;
      Measure measure;
      align(measure, measured, 4);
      integer(measure, measured, uint32_t(0));
      align(measure, measured, 8);
      uint64_t begin = measured.offset;
      if (!saveEntries(measure, measured, o)) {
	context.fail(measured.error == Error::OK ?
	  Error::Unsupported : measured.error);
	return false;
      }
      uint64_t length = uint64_t(measured.offset) - begin;
      if (length > Limit::Array) return context.fail(Error::Overflow);
      if constexpr (ZuIsSame<S, Measure>{}) {
	context.offset = measured.offset;
	return true;
      }
      align(s, context, 4);
      integer(s, context, uint32_t(length));
      align(s, context, 8);
      return saveEntries(s, context, o);
    }
    template <typename S>
    static bool save(S &s, State_ &context, const O &o) {
      return saveValue(s, context, o);
    }
    template <typename S>
    static bool saveTop(S &s, State_ &context, const O &o) {
      return saveValue(s, context, o);
    }
    static bool load_(Reader &reader, O &o) {
      uint32_t n;
      if (!reader.align(4) || !reader.integer(n) || !reader.align(8))
	return false;
      if (n > Limit::Array) return reader.fail(Error::Overflow);
      if (uint64_t(reader.end - reader.p) < n)
	return reader.fail(Error::Bounds);
      auto outerEnd = reader.end;
      reader.end = reader.p + n;
      while (reader.result && reader.p != reader.end) {
	if (!reader.align(8)) break;
	auto key = loadFieldValue<KeyField>(reader);
	if (!reader.result) break;
	auto value = loadFieldValue<ValField>(reader);
	if (!reader.result) break;
	o.add(ZuMv(key), ZuMv(value));
      }
      bool ok = bool(reader.result) && reader.p == reader.end;
      reader.end = outerEnd;
      return ok;
    }
    static bool loadValue(Reader &reader, O &o) {
      o.clean();
      return load_(reader, o);
    }
    static O loadCtor(Reader &reader) {
      O o;
      ZmAssert_(load_(reader, o));
      return o;
    }
    static bool load(Reader &reader, O &o) {
      return loadValue(reader, o);
    }
    static bool skip(Reader &reader) {
      ReadDepth_ arrayDepth{reader};
      if (!arrayDepth) return false;
      uint32_t n;
      if (!reader.align(4) || !reader.integer(n) || !reader.align(8))
	return false;
      if (n > Limit::Array) return reader.fail(Error::Overflow);
      if (uint64_t(reader.end - reader.p) < n)
	return reader.fail(Error::Bounds);
      auto outerEnd = reader.end;
      reader.end = reader.p + n;
      while (reader.result && reader.p != reader.end) {
	ReadDepth_ entryDepth{reader};
	if (!entryDepth || !reader.align(8) ||
	    !skipField<KeyField>(reader) ||
	    !skipField<ValField>(reader)) break;
      }
      bool ok = bool(reader.result) && reader.p == reader.end;
      reader.end = outerEnd;
      return ok;
    }
    static bool skipTop(Reader &reader) { return skip(reader); }
    static Result check(const View &view) {
      return checkView<Handler>(view);
    }
  };
};

template <typename T>
constexpr unsigned rawCode()
{
  using U = ZuDecay<T>;
  if constexpr (ZuIsSame<U, bool>{}) return ZfFieldTC::Bool;
  else if constexpr (ZuTraits<U>::IsString && !ZuTraits<U>::IsWString)
    return ZfFieldTC::String;
  else if constexpr (ZuTraits<U>::IsIntegral && ZuTraits<U>::IsSigned) {
    if constexpr (sizeof(U) == 1) return ZfFieldTC::Int8;
    else if constexpr (sizeof(U) == 2) return ZfFieldTC::Int16;
    else if constexpr (sizeof(U) == 4) return ZfFieldTC::Int32;
    else if constexpr (sizeof(U) == 8) return ZfFieldTC::Int64;
    else return ZfFieldTC::UDT;
  } else if constexpr (ZuTraits<U>::IsIntegral) {
    if constexpr (sizeof(U) == 1) return ZfFieldTC::UInt8;
    else if constexpr (sizeof(U) == 2) return ZfFieldTC::UInt16;
    else if constexpr (sizeof(U) == 4) return ZfFieldTC::UInt32;
    else if constexpr (sizeof(U) == 8) return ZfFieldTC::UInt64;
    else return ZfFieldTC::UDT;
  } else if constexpr (ZuTraits<U>::IsFloatingPoint) return ZfFieldTC::Float;
  else return ZfFieldTC::UDT;
}

template <typename T>
using RawField = ValueField<ZuDecay<T>, rawCode<T>()>;

template <typename T> struct IsAny : public ZuFalse { };
template <> struct IsAny<Any> : public ZuTrue { };

struct AsVariant {
  template <typename O_, typename Facet, typename Props = ZuTypeList<>>
  struct Handler : public Checked<Handler<O_, Facet, Props>, O_, Facet> {
    enum { Alignment = 1 };
    using O = O_;
    using Base = Checked<Handler, O, Facet>;
    using Base::Base;
    using Base::load;
    using Types = typename O::Types;

    template <typename T, typename F, typename P>
    static constexpr void signature(auto &s) { s << 'v'; }
    static constexpr void signatureTop(auto &s) { s << 'v'; }

    template <typename V>
    static constexpr SigConst alternativeSignature() {
      SigConst s;
      signatureField<RawField<V>>(s);
      return s;
    }
    template <typename S>
    static bool saveValue(S &s, State_ &context, const O &o) {
      SaveDepth_ nesting{context};
      if (!nesting) return false;
      bool ok = false;
      o.cdispatch([&s, &context, &ok](auto, const auto &value) {
	using V = ZuDecay<decltype(value)>;
	if constexpr (IsAny<V>{}) {
	  writeText(s, context, Type::Signature, ZuBSpan(value.signature));
	  ok = saveAny(s, context, value);
	} else {
	  static constexpr auto signature = alternativeSignature<V>();
	  ZuCSpan bytes{signature.bytes, signature.length};
	  if constexpr (ZuIsSame<S, Measure>{})
	    if (!validSignature(bytes, true)) return;
	  writeText(s, context, Type::Signature, ZuBSpan(bytes));
	  ok = saveField<S, RawField<V>>(s, context, value);
	}
      });
      if (!ok && context.error == Error::OK) context.fail(Error::Type);
      return ok;
    }
    template <typename S>
    static bool save(S &s, State_ &context, const O &o) {
      return saveValue(s, context, o);
    }
    template <typename S>
    static bool saveTop(S &s, State_ &context, const O &o) {
      return saveValue(s, context, o);
    }
    static bool loadValue(Reader &reader, O &o) {
      ZuBSpan signatureBytes;
      if (!textData(reader, Type::Signature, signatureBytes)) return false;
      ZuCSpan signature{signatureBytes};
      bool found = false;
      ZuUnroll::all<Types>([
	&reader, &o, signature, &found
      ]<typename V>() {
	if (found) return;
	if constexpr (!ZuIsSame<V, void>{} && !IsAny<V>{}) {
	  static constexpr auto expected = alternativeSignature<V>();
	  if (ZuCSpan{expected.bytes, expected.length} != signature) return;
	  found = true;
	  auto value = loadFieldValue<RawField<V>>(reader);
	  if (!reader.result) return;
	  constexpr unsigned I = ZuTypeIndex<V, Types>{};
	  o.template p<I>(ZuMv(value));
	}
      });
      if (found) return bool(reader.result);
      bool captured = false;
      ZuUnroll::all<Types>([&reader, &o, signature, &captured]<typename V>() {
	if (captured) return;
	if constexpr (IsAny<V>{}) {
	  auto begin = reader.p;
	  ZuBSpan signatureBytes{signature};
	  auto p = signatureBytes.begin();
	  auto end = signatureBytes.end();
	  if (!skipValue(reader, p, end, 0) || p != end) return;
	  constexpr unsigned I = ZuTypeIndex<V, Types>{};
	  o.template p<I>(Any{{begin, unsigned(reader.p - begin)}, signature,
	    reader.view.offset + unsigned(begin - reader.view.bytes.begin()),
	    reader.view.order});
	  captured = true;
	}
      });
      return captured || reader.fail(Error::Type);
    }
    static O loadCtor(Reader &reader) {
      O o;
      ZmAssert_(loadValue(reader, o));
      return o;
    }
    static bool load(Reader &reader, O &o) {
      return loadValue(reader, o);
    }
    static bool skip(Reader &reader) {
      ReadDepth_ nesting{reader};
      if (!nesting) return false;
      ZuBSpan signatureBytes;
      if (!text(reader, Type::Signature, signatureBytes)) return false;
      ZuCSpan signature{signatureBytes};
      if (!validSignature(signature, true)) return reader.fail(Error::Syntax);
      bool found = false;
      ZuUnroll::all<Types>([
	&reader, signature, &found
      ]<typename V>() {
	if (found) return;
	if constexpr (!ZuIsSame<V, void>{} && !IsAny<V>{}) {
	  static constexpr auto expected = alternativeSignature<V>();
	  if (ZuCSpan{expected.bytes, expected.length} != signature) return;
	  found = true;
	  if (!skipField<RawField<V>>(reader) && reader.result)
	    reader.fail(Error::Type);
	}
      });
      if (found) return bool(reader.result);
      bool captured = false;
      ZuUnroll::all<Types>([&reader, signature, &captured]<typename V>() {
	if (captured) return;
	if constexpr (IsAny<V>{}) {
	  ZuBSpan signatureBytes{signature};
	  auto p = signatureBytes.begin();
	  auto end = signatureBytes.end();
	  captured = skipValue(reader, p, end, reader.depth) && p == end;
	}
      });
      return captured || reader.fail(Error::Type);
    }
    static bool skipTop(Reader &reader) { return skip(reader); }
    static Result check(const View &view) {
      return checkView<Handler>(view);
    }
  };
};

template <typename O, typename Facet>
inline Signature signature()
{
  Signature s;
  using Handler = typename As<O>::template Handler<O, Facet>;
  Handler::signatureTop(s);
  return s;
}

template <typename O, typename Facet = ZuFacet::DBUS>
consteval SigConst signatureConst()
{
  SigConst s;
  using Handler = typename As<O>::template Handler<O, Facet>;
  Handler::signatureTop(s);
  return s;
}

template <typename Facet = ZuFacet::DBUS, typename O>
inline Result measure(const O &o, Context context = {})
{
  if (context.offset > UINT_MAX)
    return {UINT_MAX, Error::Overflow};
  if (context.order != Order::Little && context.order != Order::Big)
    return {unsigned(context.offset), Error::Syntax};
  State_ state{context};
  using Handler = typename As<O>::template Handler<O, Facet>;
  auto signature = ZtScratch(Signature, Limit::Signature + 1);
  Handler::signatureTop(signature);
  if (!validSignature(signature))
    return {unsigned(context.offset), Error::Type};
  State_ measured = state;
  Measure measure;
  if (!Handler::saveTop(measure, measured, o))
    return {unsigned(context.offset), measured.error == Error::OK ?
      Error::Unsupported : measured.error};
  if (measured.offset > UINT_MAX)
    return {unsigned(context.offset), Error::Overflow};
  return {unsigned(measured.offset), Error::OK};
}

// Caller has measured the stable object and reserved the final sink capacity.
template <typename Facet = ZuFacet::DBUS, typename S, typename O>
inline S &saveMeasured(S &s, const O &o, Context context, Result measured)
{
  ZmAssert_(measured && measured.offset >= context.offset);
  State_ state{context};
  using Handler = typename As<O>::template Handler<O, Facet>;
  ZmAssert_(Handler::saveTop(s, state, o) &&
    state.offset == measured.offset);
  return s;
}

template <typename Facet = ZuFacet::DBUS, typename S, typename O>
inline S &save(S &s, const O &o, Context context = {})
{
  auto measured = measure<Facet>(o, context);
  if (!measured) return s;
  Prepare_<S>::run(s, unsigned(measured.offset - context.offset));
  return saveMeasured<Facet>(s, o, context, measured);
}

template <typename O, typename Facet = ZuFacet::DBUS>
auto handler(const View &view)
{
  using Handler = typename As<O>::template Handler<O, Facet>;
  return Handler{view};
}

} // ZfDBUS

#endif /* ZfDBUS_HH */
