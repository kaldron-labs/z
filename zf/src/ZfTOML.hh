//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TOML configuration parsing and formatting

#ifndef ZfTOML_HH
#define ZfTOML_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmBackTrace.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfTree.hh>
#include <zlib/ZfTreeLoad.hh>
#include <zlib/ZePlatform.hh>

ZuStructFacet(TOML);

namespace ZfTOML {

using namespace ZfJSON;

enum {
  NativeScalar,
  BasicScalar,
  LiteralScalar,
  MultilineBasicScalar,
  MultilineLiteralScalar,
  ScalarN
};
} // ZfTOML

namespace ZuFieldProp::TOML {

using namespace ZuFieldProp::JSON;

template <uint8_t I> struct ScalarFmt { };

using Native = ScalarFmt<ZfTOML::NativeScalar>;
using Basic = ScalarFmt<ZfTOML::BasicScalar>;
using Literal = ScalarFmt<ZfTOML::LiteralScalar>;
using MultilineBasic = ScalarFmt<ZfTOML::MultilineBasicScalar>;
using MultilineLiteral = ScalarFmt<ZfTOML::MultilineLiteralScalar>;

template <typename Props, bool = HasValue<Props, ScalarFmt>{}>
struct GetScalarFmt_ {
  using T = ZuConstant<uint8_t, ZfTOML::NativeScalar>;
};
template <typename Props>
struct GetScalarFmt_<Props, true> {
  using T = GetValue<Props, ScalarFmt>;
};
template <typename Props>
using GetScalarFmt = typename GetScalarFmt_<Props>::T;

struct Inline { };

} // ZuFieldProp::TOML

namespace ZfTOML {

using ZfTree::AnyNode;
using ZfTree::Node_;
using ZfTree::Node;
using ZfTree::NodeArray;
using ZfTree::CNodeArray;
using ZfTree::newNode;
namespace ValueTC = ZfTree::ValueTC;
namespace ScalarTC = ZfTree::ScalarTC;

namespace SyntaxErrorCode {
  enum {
    Syntax, UTF8, Unicode, Control, Newline, Escape, Key, Value,
    Integer, IntegerRange, Float, DateTime, DuplicateKey,
    Redefine, TableConflict, InlineSealed, ArrayConflict,
    DepthMax, NodeMax, TrailingInput
  };
}

struct Limits {
  enum {
    DepthMax = 128,	// bounds recursive array/inline-table parser frames
    NodeMax = 1<<18	// bounds worst-case tree storage below Zv's 1MiB cap
  };

  unsigned depth = DepthMax;
  unsigned nodes = NodeMax;
};

struct SyntaxError {
  unsigned	offset = 0;
  unsigned	line = 1;
  unsigned	column = 1;
  char		ch = 0;
  uint8_t	code = SyntaxErrorCode::Syntax;
  bool		failed = false;
};

class ZfAPI Scan {
public:
  Scan(ZuCSpan span, Limits limits = {}) :
    m_span{span}, m_limits{limits} { }

  ZuCSpan span() const { return m_span; }
  const SyntaxError &error() const { return m_error; }
  SyntaxError &error() { return m_error; }

  ZuTuple<int, ZuPtr<const AnyNode>> scan();

private:
  struct Impl;

  ZuCSpan	m_span;
  Limits	m_limits;
  SyntaxError	m_error;
};

ZfExtern ZuTuple<int, ZuPtr<const AnyNode>> scan(
  ZuCSpan span, Limits limits = {});

} // ZfTOML

namespace ZfTOMLError {

using AnyNode = ZfTOML::AnyNode;

constexpr auto Component = "ZfTOML"_Zu;

ZfExtern ZuCSpan reason(uint8_t code);

inline ZeString fullKey(const AnyNode *node, ZuCSpan key = {}) {
  ZeString s;
  if (node) node->path(s);
  if (key) {
    if (s) s << '.';
    s << key;
  }
  return s;
}

inline auto badValue(
    const AnyNode *node, ZuCSpan expected, ZuCSpan value) {
  return [key = ZeString{fullKey(node)}, expected, value = ZeString{value}]
      (auto &s) {
    s << '"' << key << "\": invalid " << expected << " \"" << value << '"';
  };
}

inline auto required(const AnyNode *node, ZuCSpan key) {
  return [key = ZeString{fullKey(node, key)}, bt = ZmBackTrace{1}](auto &s) {
    s << '"' << key << "\" missing at:\n" << bt;
  };
}

inline auto badBool(const AnyNode *node, ZuCSpan key, ZuCSpan value) {
  return [key = ZeString{fullKey(node, key)}, value = ZeString{value}](auto &s) {
    s << '"' << key << "\": invalid boolean \"" << value << '"';
  };
}

template <typename T, typename V>
inline auto badRange(
    const AnyNode *node, ZuCSpan key, T minimum, T maximum, V value) {
  return [key = ZeString{fullKey(node, key)}, minimum, maximum, value](auto &s) {
    s << '"' << key << "\" out of range min(" << minimum << ") <= " <<
      value << " <= max(" << maximum << ')';
  };
}

template <typename Map>
inline auto badEnum(const AnyNode *node, ZuCSpan key, ZuCSpan value) {
  return [key = ZeString{fullKey(node, key)}, value = ZeString{value}](auto &s) {
    s << '"' << key << "\" did not match { ";
    bool first = true;
    Map::all([&s, &first](ZuCSpan id, auto v) {
      if (!first) s << ", ";
      first = false;
      s << id << " = " << v;
    });
    s << " }";
  };
}

inline auto badType(const AnyNode *node, ZuCSpan expected) {
  return [key = ZeString{fullKey(node)}, expected = ZeString{expected}](auto &s) {
    s << '"' << key << "\": expected " << expected;
  };
}

inline auto badScalarFmt(ZuCSpan key, unsigned style) {
  return [key = ZeString{key}, style](auto &s) {
    s << '"' << key << "\": value cannot use TOML scalar style " << style;
  };
}

inline auto badSyntax(
    unsigned line, unsigned column, unsigned offset, char ch, uint8_t code,
    ZuCSpan fileName = {}) {
  return [line, column, offset, ch, code, fileName = ZeString{fileName}]
      (auto &s) {
    if (fileName)
      s << '"' << fileName << "\":" << line << ':' << column;
    else
      s << "line " << line << ", column " << column;
    s << " (offset " << offset << "): ";
    if (auto why = reason(code)) s << why;
    else s << "syntax error";
    s << " near '";
    if (ch >= 0x20 && ch < 0x7f) s << ch;
    else s << '\\' << ZuBoxed(unsigned(ch) & 0xff).
      fmt<ZuFmt::Hex<0, ZuFmt::Alt<ZuFmt::Right<2>>>>();
    s << '\'';
  };
}

} // ZfTOMLError

#define ZfTOML_EXCEPT(...) \
  ZeMkException(Ze::Error, __FILE__, __LINE__, ZuFnName, \
    ZfTOMLError::Component, __VA_ARGS__)

namespace ZfTOML {

using namespace ZfTOMLError;

struct AsObject;
struct AsUnion;
template <unsigned ElemCode, typename ElemProps = ZuTypeList<>>
struct AsArray;
template <unsigned ValCode, typename ValProps = ZuTypeList<>>
struct AsMap;
struct AsString;

template <typename ...Ts>
struct Union : public ZuUnion<void, const AnyNode *, Ts...> {
friend inline AsUnion ZfTOML_Fmt(Union *);
  ZuDerive_(Union, (ZuUnion<void, const AnyNode *, Ts...>));
};

template <typename T> struct IsUnion_ : public ZuFalse { };
template <typename ...Ts>
struct IsUnion_<Union<Ts...>> : public ZuTrue { };
template <typename T>
using IsUnion = IsUnion_<ZuDecay<T>>;

template <typename O, typename Facet, typename = ZuFields<O, Facet>>
struct AsDeflt_ { using T = AsObject; };
template <typename O, typename Facet>
struct AsDeflt_<O, Facet, ZuTypeList<>> { using T = AsString; };
struct AsDeflt {
  template <typename O, typename Facet>
  using Handler = typename AsDeflt_<O, Facet>::T::template Handler<O, Facet>;
};

} // ZfTOML

ZfTOML::AsDeflt ZfTOML_Fmt(...);

namespace ZfTOML {

template <typename O>
using As = decltype(ZfTOML_Fmt(ZuDeclVal<O *>()));

template <typename U, bool IsPtr = ZuTraits<U>::IsPointer>
struct IsObjPtr__ { using T = ZuFalse; };
template <typename U>
struct IsObjPtr__<U, true> {
  using T = ZuBool<ZuTraits<decltype(*(ZuDeclVal<const U &>()))>::IsComposite>;
};
template <typename U, typename = As<U>>
struct IsObjPtr_ { using T = ZuFalse; };
template <typename U>
struct IsObjPtr_<U, AsDeflt> { using T = typename IsObjPtr__<U>::T; };
template <typename U>
using IsObjPtr = typename IsObjPtr_<U>::T;

template <typename T, bool = IsObjPtr<ZuDecay<T>>{}>
struct ObjType_ { using T_ = ZuDecay<T>; };
template <typename T>
struct ObjType_<T, true> {
  using T_ = ZuDecay<decltype(*(ZuDeclVal<const ZuDecay<T> &>()))>;
};
template <typename T>
using ObjType = typename ObjType_<T>::T_;
template <typename T>
ZuInline decltype(auto) obj_(const T &v) {
  if constexpr (IsObjPtr<ZuDecay<T>>{}) return *v;
  else return v;
}
template <typename T, typename Facet>
using ObjHandler = typename As<ObjType<T>>::template Handler<ObjType<T>, Facet>;

template <typename O, typename Facet>
auto handler_(const AnyNode *);

struct ZfAPI TOMLPolicy {
  template <typename Fields>
  using GetIDs = ZuFieldProp::TOML::GetIDs<Fields>;
  template <typename Props>
  using GetBytesFmt = ZuFieldProp::TOML::GetBytesFmt<Props>;
  template <typename Props>
  using GetNumberFmt = ZuFieldProp::TOML::GetNumberFmt<Props>;
  template <typename Props>
  using GetTimeFmt = ZuFieldProp::TOML::GetTimeFmt<Props>;
  template <typename O, typename Facet>
  using Handler = typename As<O>::template Handler<O, Facet>;
  template <typename O>
  using IsObjPtr = ZfTOML::IsObjPtr<O>;

  static bool scalar(int type, ZfTreeLoad::ScalarMask::T mask) {
    switch (type) {
      case ScalarTC::String:
	return mask & ZfTreeLoad::ScalarMask::String();
      case ScalarTC::Number:
	return mask & ZfTreeLoad::ScalarMask::Number();
      case ScalarTC::False:
      case ScalarTC::True:
	return mask & ZfTreeLoad::ScalarMask::Bool();
      default:
	return false;
    }
  }

  template <typename B>
  static ZuTuple<int, B> tomlIntEOV(ZuCSpan span) {
    using T = typename B::T;
    unsigned i = 0, n = span.length(), base = 10;
    bool negative = false;
    if (!n) return {-1, B{}};
    char c = span[i];
    if (c == '+' || c == '-') {
      negative = c == '-';
      ++i;
      if (i >= n) return {-1, B{}};
    }
    if (i + 2 <= n && span[i] == '0') {
      c = span[i + 1];
      if (i && (c == 'x' || c == 'o' || c == 'b')) return {-1, B{}};
      switch (c) {
        case 'x': base = 16; i += 2; break;
        case 'o': base = 8; i += 2; break;
        case 'b': base = 2; i += 2; break;
      }
    }
    uint128_t maximum;
    if constexpr (ZuTraits<T>::IsSigned)
      maximum = negative ? uint128_t{B::Cmp::maximum()} + 1 :
	uint128_t{B::Cmp::maximum()};
    else {
      if (negative) return {-1, B{}};
      maximum = B::Cmp::maximum();
    }
    uint128_t value = 0;
    bool digit = false;
    for (; i < n; ++i) {
      c = span[i];
      if (c == '_') continue;
      unsigned v = c >= 'a' ? c - 'a' + 10 :
	c >= 'A' ? c - 'A' + 10 : c - '0';
      if (v >= base || value > (maximum - v) / base) return {-1, B{}};
      value = value * base + v;
      digit = true;
    }
    if (!digit) return {-1, B{}};
    T out = negative ? (value ? T(-T(value - 1) - 1) : T(0)) : T(value);
    return {int(n), B{out}};
  }

  template <typename B, typename Fmt, typename Props>
  static auto intEOV(int type, ZuCSpan span) {
    if (type == ScalarTC::Number) return tomlIntEOV<B>(span);
    return ZfTreeLoad::intEOV<B, Fmt, Props>(span);
  }

  static ZuTuple<int, ZuDecimal> decimalEOV(ZuCSpan);
  static ZuTuple<int, double> floatEOV(ZuCSpan);

  template <typename T, typename>
  static T boolean(const AnyNode *node) {
    if (node && node->has<AnyNode::String>()) {
      switch (node->scalarType) {
        case ScalarTC::False: return T{false};
        case ScalarTC::True: return T{true};
      }
    }
    badType(node, "boolean");
  }

  [[noreturn]] static void required(const AnyNode *node, ZuCSpan key) {
    throw ZfTOML_EXCEPT(ZfTOMLError::required(node, key));
  }
  [[noreturn]] static void badValue(
      const AnyNode *node, ZuCSpan expected, ZuCSpan value) {
    throw ZfTOML_EXCEPT(ZfTOMLError::badValue(node, expected, value));
  }
  [[noreturn]] static void badBool(
      const AnyNode *node, ZuCSpan key, ZuCSpan value) {
    throw ZfTOML_EXCEPT(ZfTOMLError::badBool(node, key, value));
  }
  [[noreturn]] static void badType(const AnyNode *node, ZuCSpan expected) {
    throw ZfTOML_EXCEPT(ZfTOMLError::badType(node, expected));
  }
  template <typename T, typename V>
  [[noreturn]] static void badRange(
      const AnyNode *node, ZuCSpan key, T minimum, T maximum, V value) {
    throw ZfTOML_EXCEPT(
      ZfTOMLError::badRange(node, key, minimum, maximum, value));
  }
  template <typename Map>
  [[noreturn]] static void badEnum(
      const AnyNode *node, ZuCSpan key, ZuCSpan value) {
    throw ZfTOML_EXCEPT(ZfTOMLError::badEnum<Map>(node, key, value));
  }
  template <typename O, typename Facet>
  static auto handler(const AnyNode *node) { return handler_<O, Facet>(node); }
};

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
auto loadValue(const AnyNode *);

template <typename S>
inline void basicText(S &, ZuCSpan);

template <typename S>
inline void saveKey(S &s, ZuCSpan key)
{
  bool bare = bool(key);
  for (unsigned i = 0, n = key.length(); i < n && bare; ++i) {
    char c = key[i];
    bare = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') || c == '_' || c == '-';
  }
  if (bare) s << key;
  else basicText(s, key);
}

template <typename S>
struct BasicOut {
  S &s;

  BasicOut &operator <<(char c) {
    uint8_t u = c;
    if (u < 0x20 || u == 0x7f)
      s << "\\u" << ZuBoxed(u).fmt<ZuFmt::Hex<0, ZuFmt::Right<4>>>();
    else
      s << c;
    return *this;
  }
  template <typename V>
  BasicOut &operator <<(V &&v) {
    s << ZuFwd<V>(v);
    return *this;
  }
};

template <typename S>
inline void basicText(S &s, ZuCSpan v)
{
  BasicOut<S> out{s};
  ZfJSON::quote(out, v);
}

template <typename S>
inline bool literalText(S &s, ZuCSpan v, bool multiline)
{
  unsigned n = v.length(), quotes = 0;
  for (unsigned i = 0; i < n; ++i) {
    uint8_t c = v[i];
    if ((c < 0x20 && c != '\t' && (c != '\n' || !multiline)) || c == 0x7f)
      return false;
    if (!multiline && (c == '\'' || c == '\r' || c == '\n')) return false;
    if (c == '\'') {
      if (++quotes >= 3) return false;
    } else {
      quotes = 0;
    }
  }
  s << (multiline ? "'''" : "'");
  if (multiline && n && v[0] == '\n') s << '\n';
  unsigned begin = 0;
  for (unsigned i = begin; i < n; ++i) {
    char c = v[i];
    if (c == '\r') {
      if (i + 1 < n && v[i + 1] == '\n') ++i;
      s << '\n';
    } else {
      s << c;
    }
  }
  s << (multiline ? "'''" : "'");
  return true;
}

template <typename S>
inline void multilineBasicText(S &s, ZuCSpan v)
{
  s << "\"\"\"";
  unsigned n = v.length(), begin = 0;
  if (n && v[0] == '\n') { s << "\\n"; begin = 1; }
  for (unsigned i = begin; i < n; ++i) {
    uint8_t c = v[i];
    switch (c) {
      case '\b': s << "\\b"; break;
      case '\t': s << '\t'; break;
      case '\n': s << '\n'; break;
      case '\f': s << "\\f"; break;
      case '\r':
	if (i + 1 < n && v[i + 1] == '\n') ++i;
	s << '\n';
	break;
      case '"': s << "\\\""; break;
      case '\\': s << "\\\\"; break;
      default:
	if (c < 0x20 || c == 0x7f)
	  s << "\\u" << ZuBoxed(c).fmt<ZuFmt::Hex<0, ZuFmt::Right<4>>>();
	else
	  s << char(c);
    }
  }
  s << "\"\"\"";
}

template <typename Props, typename S>
inline void saveText(S &s, ZuCSpan v, ZuCSpan key = {})
{
  constexpr unsigned Fmt = ZuFieldProp::TOML::GetScalarFmt<Props>{};
  if constexpr (Fmt == NativeScalar || Fmt == BasicScalar) {
    basicText(s, v);
  } else if constexpr (Fmt == LiteralScalar) {
    if (!literalText(s, v, false))
      throw ZfTOML_EXCEPT(ZfTOMLError::badScalarFmt(key, Fmt));
  } else if constexpr (Fmt == MultilineBasicScalar) {
    multilineBasicText(s, v);
  } else {
    if (!literalText(s, v, true))
      throw ZfTOML_EXCEPT(ZfTOMLError::badScalarFmt(key, Fmt));
  }
}

ZuDerive(ScalarScratch, (ZtString<ZtStringSharded<true,
  ZtStringHeapID<"ZfTOML.ScalarBuf">>>));
ZuDerive(PathScratch, (ZtString<ZtStringSharded<true,
  ZtStringHeapID<"ZfTOML.Path">>>));
enum {
  ScalarScratchSize = 128, // covers ordinary formatted scalar spellings
  PathScratchSize = 128    // covers ordinary configuration header paths
};

inline ZuCSpan unquote(ScalarScratch &buf)
{
  unsigned n = buf.length();
  if (n < 2 || buf[0] != '"' || buf[n - 1] != '"') return buf;
  ZuSpan<char> span{buf.data() + 1, n - 1};
  auto r = ZfJSON::eos(span);
  if (r.p<0>() < 0) return {};
  return {span.data(), unsigned(r.p<0>())};
}

template <typename S>
struct Unquoted {
  S &s;

  Unquoted &operator <<(char c) {
    if (c != '"') s << c;
    return *this;
  }
  template <typename V>
  Unquoted &operator <<(V &&v) {
    s << ZuFwd<V>(v);
    return *this;
  }
};

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename S, typename T>
void saveUnquoted(S &s, const T &v)
{
  Unquoted<S> out{s};
  ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(out, v);
}

template <unsigned TypeCode, typename Props, typename T>
bool scalarPresent(const T &v_)
{
  if constexpr (ZuFieldProp::TOML::GetOptional<Props>{}) {
    return true;
  } else if constexpr (
      TypeCode >= ZfFieldTC::Int8 && TypeCode <= ZfFieldTC::UInt128) {
    using V = ZuDecay<T>;
    if constexpr (ZuIsBoxed<V>{}) {
      if constexpr (ZuFieldProp::HasEnum<Props>{}) return v_ >= 0;
      else return bool(*v_);
    } else {
      using B = ZuBox<ZfFieldTC::Type<TypeCode>>;
      B v{v_};
      if constexpr (ZuFieldProp::HasEnum<Props>{}) return v >= 0;
      else return bool(*v);
    }
  } else if constexpr (TypeCode == ZfFieldTC::Float) {
    return !ZuNull(double(v_));
  } else if constexpr (
      TypeCode == ZfFieldTC::Fixed || TypeCode == ZfFieldTC::Decimal) {
    return bool(*v_);
  } else if constexpr (
      TypeCode == ZfFieldTC::Time || TypeCode == ZfFieldTC::DateTime) {
    using Fmt = ZuFieldProp::TOML::GetTimeFmt<Props>;
    if constexpr (Fmt::Fmt == ZfJSON::Unix) return bool(*ZuTime{v_});
    else return bool(*ZuDateTime{v_});
  } else {
    return true;
  }
}

template <unsigned TypeCode, typename Props>
consteval bool scalarFmtValid()
{
  constexpr unsigned Style = ZuFieldProp::TOML::GetScalarFmt<Props>{};
  if constexpr (Style == NativeScalar) {
    return true;
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    return false;
  } else if constexpr (
      (TypeCode >= ZfFieldTC::Int8 && TypeCode <= ZfFieldTC::UInt128) ||
      TypeCode == ZfFieldTC::Float || TypeCode == ZfFieldTC::Fixed ||
      TypeCode == ZfFieldTC::Decimal) {
    return ZuFieldProp::TOML::GetNumberFmt<Props>::String ||
      ZuFieldProp::HasEnum<Props>{} || ZuFieldProp::HasFlags<Props>{};
  } else if constexpr (
      TypeCode == ZfFieldTC::Time || TypeCode == ZfFieldTC::DateTime) {
    using Fmt = ZuFieldProp::TOML::GetTimeFmt<Props>;
    return Fmt::Fmt != ZfJSON::ISO && Fmt::Fmt != ZfJSON::Unix;
  } else {
    return true;
  }
}

template <unsigned TypeCode, typename Props>
struct ScalarFmtValid : public ZuBool<scalarFmtValid<TypeCode, Props>()> { };

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename S, typename T>
void saveScalar(S &s, const T &v, ZuCSpan key = {})
{
  ZuAssert((ScalarFmtValid<TypeCode, Props>{}),
    "TOML string ScalarFmt requires a string output representation");
  constexpr unsigned Style = ZuFieldProp::TOML::GetScalarFmt<Props>{};
  if constexpr (TypeCode == ZfFieldTC::CString ||
      TypeCode == ZfFieldTC::String) {
    saveText<Props>(s, ZuCSpan{v}, key);
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    s << (bool(v) ? "true" : "false");
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    if constexpr (Style == NativeScalar || Style == BasicScalar) {
      BasicOut<S> out{s};
      ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(out, v);
    } else {
      auto buf = ZtScratch(ScalarScratch, ScalarScratchSize);
      ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(buf, v);
      saveText<Props>(s, unquote(buf), key);
    }
  } else {
    if constexpr (TypeCode == ZfFieldTC::Float) {
      using Fmt = ZuFieldProp::TOML::GetNumberFmt<Props>;
      if constexpr (!Fmt::String) {
	double f = v;
	if (f != f) { s << "nan"; return; }
	if (f == ZuCmp<double>::inf()) { s << "inf"; return; }
	if (f == -ZuCmp<double>::inf()) { s << "-inf"; return; }
      }
    }
    if (!scalarPresent<TypeCode, Props>(v))
      throw ZfTOML_EXCEPT(
        ZfTOMLError::badValue(nullptr, "non-null", "null"));
    constexpr bool Number =
      (TypeCode >= ZfFieldTC::Int8 && TypeCode <= ZfFieldTC::UInt128) ||
      TypeCode == ZfFieldTC::Float || TypeCode == ZfFieldTC::Fixed ||
      TypeCode == ZfFieldTC::Decimal;
    constexpr bool Date =
      TypeCode == ZfFieldTC::Time || TypeCode == ZfFieldTC::DateTime;
    if constexpr (Number) {
      using Fmt = ZuFieldProp::TOML::GetNumberFmt<Props>;
      constexpr bool Text = Fmt::String ||
        ZuFieldProp::HasEnum<Props>{} || ZuFieldProp::HasFlags<Props>{};
      if constexpr (Text) {
	if constexpr (Style == NativeScalar || Style == BasicScalar) {
	  BasicOut<S> out{s};
	  ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(out, v);
	} else {
	  auto buf = ZtScratch(ScalarScratch, ScalarScratchSize);
	  ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(buf, v);
	  saveText<Props>(s, unquote(buf), key);
	}
      } else if constexpr (ZuTypeIn<ZuFieldProp::Hex, Props>{}) {
	s << "0x";
	saveUnquoted<Facet, Filter, TypeCode, Props>(s, v);
      } else {
	ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(s, v);
      }
    } else if constexpr (Date) {
      using Fmt = ZuFieldProp::TOML::GetTimeFmt<Props>;
      if constexpr (Fmt::Fmt == ZfJSON::ISO || Fmt::Fmt == ZfJSON::Unix) {
	saveUnquoted<Facet, Filter, TypeCode, Props>(s, v);
      } else if constexpr (Style == NativeScalar || Style == BasicScalar) {
	BasicOut<S> out{s};
	ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(out, v);
      } else {
	auto buf = ZtScratch(ScalarScratch, ScalarScratchSize);
	ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(buf, v);
	saveText<Props>(s, unquote(buf), key);
      }
    } else {
      ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(s, v);
    }
  }
}

enum Route { Assignment, Table, TableArray };
enum SavePass { ValuePass, AssignmentPass, TablePass };

struct SaveCtx {
  bool		&output;
  ZuCSpan	prefix;
  bool		inline_;
};

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename S, typename T>
void saveValue(S &s, const T &v, SaveCtx &ctx, ZuCSpan key = {});

template <typename T, typename Facet>
consteval bool tableShape()
{
  using Handler = ObjHandler<T, Facet>;
  return Handler::Object || Handler::Map;
}

template <typename T, typename Facet>
struct AllTableShapes_ : public ZuBool<tableShape<ObjType<T>, Facet>()> { };
template <typename ...Ts, typename Facet>
struct AllTableShapes_<Union<Ts...>, Facet> : public ZuBool<(
  tableShape<ObjType<Ts>, Facet>() && ...)> { };
template <typename T, typename Facet>
using AllTableShapes = AllTableShapes_<ObjType<T>, Facet>;

template <unsigned ElemCode, typename Elem, typename Facet>
consteval bool tableElems()
{
  if constexpr (ElemCode != ZfFieldTC::UDT) return false;
  else return AllTableShapes<Elem, Facet>{};
}

template <typename T, typename Facet>
consteval bool tableArray()
{
  using O = ObjType<T>;
  using Handler = ObjHandler<T, Facet>;
  if constexpr (!Handler::Array)
    return false;
  else if constexpr (Handler::ElemCode != ZfFieldTC::UDT)
    return false;
  else {
    using Elem = ZuDecay<decltype(ZuDeclVal<O &>()[0])>;
    return AllTableShapes<Elem, Facet>{};
  }
}

template <
  unsigned TypeCode, typename Props, typename T, typename Facet>
Route routeValue(const T &v, const SaveCtx &ctx)
{
  if (ctx.inline_) return Assignment;
  if constexpr (ZfFieldTC::IsVec<TypeCode>{}) {
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    using Elem = ZuDecay<decltype(v[0])>;
    if (!ZuTraits<T>::length(v)) return Assignment;
    if constexpr (tableElems<ElemCode, Elem, Facet>()) return TableArray;
  } else if constexpr (TypeCode == ZfFieldTC::UDT) {
    using O = ObjType<T>;
    using Handler = ObjHandler<T, Facet>;
    if constexpr (IsObjPtr<ZuDecay<T>>{})
      if (ZuUnlikely(!v))
	throw ZfTOML_EXCEPT(
	  ZfTOMLError::badValue(nullptr, "non-null", "null"));
    const auto &v_ = obj_(v);
    if constexpr (IsUnion<O>{}) {
      return Handler::route(v_, ctx);
    } else if constexpr (Handler::Object || Handler::Map) {
      return Table;
    } else if constexpr (Handler::Array) {
      if (!ZuTraits<O>::length(v_)) return Assignment;
      if constexpr (tableArray<O, Facet>()) return TableArray;
    }
  }
  return Assignment;
}

template <typename Field, typename O>
bool fieldPresent(const O &o)
{
  enum { TypeCode = Field::Type::Code };
  using Props = typename Field::Props;
  if constexpr (!ZuFieldProp::TOML::GetOptional<Props>{}) {
    return true;
  } else if constexpr (TypeCode == ZfFieldTC::CString ||
      TypeCode == ZfFieldTC::String) {
    return bool(ZuCSpan{Field::get(o)});
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    return bool(ZuBSpan{Field::get(o)});
  } else if constexpr (
      (TypeCode >= ZfFieldTC::Int8 && TypeCode <= ZfFieldTC::UInt128) ||
      TypeCode == ZfFieldTC::Float || TypeCode == ZfFieldTC::Fixed ||
      TypeCode == ZfFieldTC::Decimal || TypeCode == ZfFieldTC::Time ||
      TypeCode == ZfFieldTC::DateTime || TypeCode == ZfFieldTC::UDT) {
    return !ZuNull(Field::get(o));
  } else {
    return true;
  }
}

template <
  typename Facet, template <typename> class Filter,
  unsigned ElemCode, typename Props, typename S, typename O>
void saveArray(S &s, const O &o, SaveCtx &ctx, ZuCSpan key)
{
  unsigned n = ZuTraits<O>::length(o);
  using Elem = ZuDecay<decltype(o[0])>;
  if constexpr (tableElems<ElemCode, Elem, Facet>()) {
    if (!ctx.inline_ && n) {
      for (unsigned i = 0; i < n; ++i) {
	const auto &elem = o[i];
	if constexpr (IsObjPtr<Elem>{})
	  if (ZuUnlikely(!elem))
	    throw ZfTOML_EXCEPT(
	      ZfTOMLError::badValue(nullptr, "non-null", "null"));
	if (ctx.output) s << '\n';
	s << "[[" << ctx.prefix << "]]\n";
	ctx.output = true;
	SaveCtx elemCtx{ctx.output, ctx.prefix, ctx.inline_};
	saveValue<Facet, Filter, ElemCode, Props>(s, elem, elemCtx, key);
      }
      return;
    }
  }
  SaveCtx elemCtx{ctx.output, ctx.prefix, true};
  s << '[';
  for (unsigned i = 0; i < n; ++i) {
    if (i) s << ", ";
    saveValue<Facet, Filter, ElemCode, Props>(s, o[i], elemCtx, key);
  }
  s << ']';
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename S, typename T>
void saveValue(S &s, const T &v, SaveCtx &ctx, ZuCSpan key)
{
  if constexpr (ZfFieldTC::IsVec<TypeCode>{}) {
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    saveArray<Facet, Filter, ElemCode, Props>(s, v, ctx, key);
  } else if constexpr (TypeCode == ZfFieldTC::UDT) {
    using O = ObjType<T>;
    using Handler = ObjHandler<T, Facet>;
    if constexpr (IsObjPtr<ZuDecay<T>>{})
      if (ZuUnlikely(!v))
	throw ZfTOML_EXCEPT(
	  ZfTOMLError::badValue(nullptr, "non-null", "null"));
    const auto &v_ = obj_(v);
    if constexpr (IsUnion<O>{} || Handler::Object || Handler::Array ||
	Handler::Map) {
      Handler::template save_<Filter, Props>(s, v_, ctx, key);
    } else {
      constexpr unsigned Style =
	ZuFieldProp::TOML::GetScalarFmt<Props>{};
      if constexpr (Style == NativeScalar || Style == BasicScalar) {
	BasicOut<S> out{s};
	Handler::template save<Filter>(out, v_);
      } else {
	auto buf = ZtScratch(ScalarScratch, ScalarScratchSize);
	Handler::template save<Filter>(buf, v_);
	saveText<Props>(s, unquote(buf), key);
      }
    }
  } else {
    saveScalar<Facet, Filter, TypeCode, Props>(s, v, key);
  }
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename S, typename T>
bool saveEntry(
    S &s, ZuCSpan key, const T &v, SaveCtx &ctx, SavePass pass, bool first)
{
  auto route = routeValue<TypeCode, Props, T, Facet>(v, ctx);
  switch (pass) {
    case ValuePass:
    case AssignmentPass:
      if (route != Assignment) return false;
      if (!first) s << (pass == ValuePass ? ", " : "\n");
      saveKey(s, key);
      s << " = ";
      saveValue<Facet, Filter, TypeCode, Props>(s, v, ctx, key);
      ctx.output = true;
      return true;
    case TablePass:
      if (route == Assignment) return false;
      break;
  }
  auto path = ZtScratch(PathScratch, PathScratchSize);
  if (ctx.prefix) path << ctx.prefix << '.';
  saveKey(path, key);
  SaveCtx child{ctx.output, path, ctx.inline_};
  if (route == Table) {
    if (ctx.output) s << '\n';
    s << '[' << path << "]\n";
    ctx.output = true;
  }
  saveValue<Facet, Filter, TypeCode, Props>(s, v, child, key);
  return true;
}

template <
  typename Facet, template <typename> class Filter, typename Field,
  typename S, typename O>
bool saveField(
    S &s, const O &o, SaveCtx &ctx, SavePass pass, bool first)
{
  if (!fieldPresent<Field>(o)) return false;
  enum { TypeCode = Field::Type::Code };
  using Props = typename Field::Props;
  SaveCtx child{ctx.output, ctx.prefix,
    ctx.inline_ || ZuTypeIn<ZuFieldProp::TOML::Inline, Props>{}};
  return saveEntry<Facet, Filter, TypeCode, Props>(s,
    ZuFieldProp::TOML::GetID<Field>{}().cspan(), Field::get(o),
    child, pass, first);
}

template <
  typename Facet, template <typename> class Filter,
  typename S, typename O>
void saveObject(S &s, const O &o, SaveCtx &ctx)
{
  using Fields = ZuTypeGrep<Filter, ZuFields<O, Facet>>;
  if (ctx.inline_) {
    bool first = true;
    s << '{';
    ZuUnroll::all<Fields>([&s, &o, &ctx, &first]<typename Field>() {
      if (saveField<Facet, Filter, Field>(
	  s, o, ctx, ValuePass, first)) first = false;
    });
    s << '}';
    return;
  }
  bool first = true;
  ZuUnroll::all<Fields>([&s, &o, &ctx, &first]<typename Field>() {
    if (saveField<Facet, Filter, Field>(
	s, o, ctx, AssignmentPass, first)) first = false;
  });
  ZuUnroll::all<Fields>([&s, &o, &ctx]<typename Field>() {
    saveField<Facet, Filter, Field>(s, o, ctx, TablePass, false);
  });
}

template <
  typename Facet, template <typename> class Filter,
  typename S, typename O>
void saveMap(S &s, const O &o, SaveCtx &ctx)
{
  using Handler = typename As<O>::template Handler<O, Facet>;
  if (ctx.inline_) {
    bool first = true;
    s << '{';
    auto i = o.citer();
    while (auto node = i())
      if (saveEntry<Facet, Filter,
	  Handler::ValCode, typename Handler::ValProps>(s,
	    node->key(), node->val(), ctx, ValuePass, first)) first = false;
    s << '}';
    return;
  }
  bool first = true;
  {
    auto i = o.citer();
    while (auto node = i())
      if (saveEntry<Facet, Filter,
	  Handler::ValCode, typename Handler::ValProps>(s,
	    node->key(), node->val(), ctx, AssignmentPass, first)) first = false;
  }
  auto i = o.citer();
  while (auto node = i())
    saveEntry<Facet, Filter, Handler::ValCode, typename Handler::ValProps>(s,
      node->key(), node->val(), ctx, TablePass, false);
}

struct AsObject {
  template <typename O_, typename Facet>
  struct Handler : public ZfTreeLoad::Object<TOMLPolicy, O_, Facet> {
    using O = O_;
    using Base = ZfTreeLoad::Object<TOMLPolicy, O, Facet>;
    using Base::Base;

    enum { Object = 1, Array = 0, Map = 0, Root = 1 };

    template <template <typename> class Filter, typename, typename S>
    static void save_(S &s, const O &o, SaveCtx &ctx, ZuCSpan) {
      saveObject<Facet, Filter>(s, o, ctx);
    }

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      bool output = false;
      SaveCtx ctx{output, {}, false};
      save_<Filter, ZuTypeList<>>(s, o, ctx, {});
      if (output) s << '\n';
    }
  };
};

struct AsUnion {
  // Any actual parse-tree node is retained without inspecting its shape.  The
  // parse tree must remain alive until the caller resolves the union.  TOML
  // has no null value, so unresolved and null alternatives fail when saved.
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;

    enum { Object = 0, Array = 0, Map = 0, Root = 1 };

    const AnyNode *node;

    template <typename L>
    static void dispatch_(const O &o, L &&l) {
      auto type = o.type();
      if (ZuUnlikely(type < 2))
	throw ZfTOML_EXCEPT(
	  ZfTOMLError::badValue(nullptr, "resolved union", "unresolved"));
      ZuSwitch::dispatch<O::N - 2>(type - 2, [&o, &l](auto I_) {
	static constexpr unsigned I = I_ + 2;
	using V = typename O::template Type<I>;
	const auto &v = o.template p<I>();
	if constexpr (!IsObjPtr<V>{}) {
	  l(v);
	} else {
	  if (ZuUnlikely(!v))
	    throw ZfTOML_EXCEPT(
	      ZfTOMLError::badValue(nullptr, "non-null", "null"));
	  l(*v);
	}
      });
    }

    static Route route(const O &o, const SaveCtx &ctx) {
      if (ctx.inline_) return Assignment;
      Route route = Assignment;
      dispatch_(o, [&route, &ctx]<typename V>(const V &v) {
	route = routeValue<
	  ZfFieldTC::UDT, ZuTypeList<>, V, Facet>(v, ctx);
      });
      return route;
    }

    template <template <typename> class Filter, typename Props, typename S>
    static void save_(
	S &s, const O &o, SaveCtx &ctx, ZuCSpan key) {
      dispatch_(o, [&s, &ctx, key]<typename V>(const V &v) {
	saveValue<Facet, Filter, ZfFieldTC::UDT, Props>(s, v, ctx, key);
      });
    }

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      bool output = false;
      SaveCtx ctx{output, {}, false};
      if (route(o, ctx) != Table)
	throw ZfTOML_EXCEPT(
	  ZfTOMLError::badValue(nullptr, "table", "non-table union"));
      save_<Filter, ZuTypeList<>>(s, o, ctx, {});
      if (output) s << '\n';
    }

    static bool valid(const AnyNode *node) { return node != nullptr; }
    Handler(const AnyNode *node_) : node{node_} { }
    O ctor() const { return O(node); }
    O *alloc() const { return new O(node); }
    void new_(void *o) const { new (o) O(node); }
    void load(O &o) const { o = node; }
    void update(O &o) const { o = node; }
  };
};

template <unsigned ElemCode_, typename ElemProps_>
struct AsArray {
  template <typename O_, typename Facet>
  struct Handler : public
      ZfTreeLoad::Array<TOMLPolicy, ElemCode_, ElemProps_, O_, Facet> {
    using O = O_;
    using Base =
      ZfTreeLoad::Array<TOMLPolicy, ElemCode_, ElemProps_, O, Facet>;
    using ElemProps = ElemProps_;
    using Base::Base;

    enum {
      Object = 0, Array = 1, Map = 0, Root = 0,
      ElemCode = ElemCode_
    };

    template <template <typename> class Filter, typename, typename S>
    static void save_(
	S &s, const O &o, SaveCtx &ctx, ZuCSpan key) {
      saveArray<Facet, Filter, ElemCode_, ElemProps_>(s, o, ctx, key);
    }
  };
};

template <unsigned ValCode_, typename ValProps_>
struct AsMap {
  template <typename O_, typename Facet>
  struct Handler : public
      ZfTreeLoad::Map<TOMLPolicy, ValCode_, ValProps_, O_, Facet> {
    using O = O_;
    using Base =
      ZfTreeLoad::Map<TOMLPolicy, ValCode_, ValProps_, O, Facet>;
    using Val = typename Base::Val;
    using ValProps = ValProps_;
    using Base::Base;

    enum {
      Object = 0, Array = 0, Map = 1, Root = 1,
      ValCode = ValCode_
    };

    template <template <typename> class Filter, typename, typename S>
    static void save_(S &s, const O &o, SaveCtx &ctx, ZuCSpan) {
      saveMap<Facet, Filter>(s, o, ctx);
    }

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      bool output = false;
      SaveCtx ctx{output, {}, false};
      save_<Filter, ZuTypeList<>>(s, o, ctx, {});
      if (output) s << '\n';
    }
  };
};

struct AsStringDeflt {
  template <typename O>
  struct Handler {
    template <typename S>
    static void save(S &s, const O &o) {
      ZfJSON::AsStringDeflt::Handler<O>::save(s, o);
    }
    static O load(ZuCSpan span) { return O(span); }
  };
};

} // ZfTOML

ZfTOML::AsStringDeflt ZfTOML_StringFmt(...);

namespace ZfTOML {

struct AsString {
  template <typename O_, typename>
  struct Handler : public ZfTreeLoad::String<
      TOMLPolicy,
      typename decltype(ZfTOML_StringFmt(ZuDeclVal<O_ *>()))::
	template Handler<O_>, O_> {
    using O = O_;
    using Fmt = decltype(ZfTOML_StringFmt(ZuDeclVal<O *>()));
    using Handler_ = typename Fmt::template Handler<O>;
    using Base = ZfTreeLoad::String<TOMLPolicy, Handler_, O>;
    using Base::Base;

    enum { Object = 0, Array = 0, Map = 0, Root = 0 };

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) { Handler_::save(s, o); }
  };
};

template <typename O, typename Facet>
inline auto handler_(const AnyNode *node) {
  using Handler = typename As<O>::template Handler<O, Facet>;
  if (ZuUnlikely(!Handler::valid(node)))
    throw ZfTOML_EXCEPT(badType(node, "UDT"));
  return Handler{node};
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline auto loadValue(const AnyNode *node)
{
  return ZfTreeLoad::loadValue<
    TOMLPolicy, Facet, Filter, TypeCode, Props, T>(node);
}

template <typename O, typename Facet = ZuFacet::TOML>
auto handler(const AnyNode *node) { return handler_<O, Facet>(node); }

template <
  typename Facet = ZuFacet::TOML,
  template <typename> class Filter = ZfFieldFilter::Save,
  typename S, typename O>
inline S &save(S &s, const O &v) {
  using Handler = ObjHandler<O, Facet>;
  ZuAssert((Handler::Root));
  if constexpr (IsObjPtr<O>{})
    if (ZuUnlikely(!v))
      throw ZfTOML_EXCEPT(
	ZfTOMLError::badValue(nullptr, "non-null", "null"));
  Handler::template save<Filter>(s, obj_(v));
  return s;
}

template <typename Facet = ZuFacet::TOML, typename S, typename O>
inline S &saveUpd(S &s, const O &v) {
  return save<Facet, ZfFieldFilter::Upd>(s, v);
}

template <typename Facet = ZuFacet::TOML, typename S, typename O>
inline S &saveDel(S &s, const O &v) {
  return save<Facet, ZfFieldFilter::Del>(s, v);
}

} // ZfTOML

#endif /* ZfTOML_HH */
