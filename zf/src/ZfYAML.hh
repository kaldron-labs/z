//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// JSON-interoperable, single-document YAML configuration parsing/formatting
// - supports block/flow collections, standard scalar styles and aliases
// - composes aliases into a uniquely owned tree
// - rejects directives, tags, merge/complex keys and shared/cyclic graphs

#ifndef ZfYAML_HH
#define ZfYAML_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuPtr.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmBackTrace.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfTree.hh>
#include <zlib/ZfTreeLoad.hh>
#include <zlib/ZePlatform.hh>

ZuStructFacet(YAML);

namespace ZfYAML {

using namespace ZfJSON;

enum { Plain, SingleQuoted, DoubleQuoted, BlockLiteral, BlockFolded };

} // ZfYAML

namespace ZuFieldProp::YAML {

using namespace ZuFieldProp::JSON;

template <uint8_t I> struct ScalarFmt { };

using Plain = ScalarFmt<ZfYAML::Plain>;
using SingleQuoted = ScalarFmt<ZfYAML::SingleQuoted>;
using DoubleQuoted = ScalarFmt<ZfYAML::DoubleQuoted>;
using BlockLiteral = ScalarFmt<ZfYAML::BlockLiteral>;
using BlockFolded = ScalarFmt<ZfYAML::BlockFolded>;

template <typename Props, bool = HasValue<Props, ScalarFmt>{}>
struct GetScalarFmt_ {
  using T = ZuConstant<uint8_t, ZfYAML::Plain>;
};
template <typename Props>
struct GetScalarFmt_<Props, true> {
  using T = GetValue<Props, ScalarFmt>;
};
template <typename Props>
using GetScalarFmt = typename GetScalarFmt_<Props>::T;

} // ZuFieldProp::YAML

namespace ZfYAML {

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
    Syntax,
    Indent,
    DuplicateKey,
    Directive,
    Tag,
    ComplexKey,
    MergeKey,
    Anchor,
    AliasMissing,
    AliasActive,
    AliasMax,
    AliasDepthMax,
    NodeMax,
    Unicode,
    Escape,
    SecondDocument,
    TrailingInput
  };
}

struct Limits {
  enum {
    AliasMax = 100,	// conservative mainstream parser expansion limit
    AliasDepthMax = 32, // bounds chained anchor amplification
    NodeMax = 1<<18	// ~128MiB worst-case tree below ZvYAML's 1MiB input cap
  };

  unsigned aliases = AliasMax;
  unsigned aliasDepth = AliasDepthMax;
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

  ZuTuple<int, ZuPtr<AnyNode>> scan();

private:
  struct Impl;

  ZuCSpan	m_span;
  Limits	m_limits;
  SyntaxError	m_error;
};

// scan one JSON-interoperable YAML document into an owned mutable tree
ZfExtern ZuTuple<int, ZuPtr<AnyNode>> scan(
  ZuCSpan span, Limits limits = {});

// resolve local OpenAPI $ref objects and flatten schema composition
ZfExtern ZuPtr<AnyNode> resolve(ZuPtr<AnyNode> &&root);
ZfExtern ZuPtr<AnyNode> flatten(
  ZuPtr<AnyNode> &&root, bool oneOf = false, bool anyOf = false);

} // ZfYAML

namespace ZfYAMLError {

using AnyNode = ZfYAML::AnyNode;

constexpr auto Component = "ZfYAML"_Zu;

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
  return [
    key = ZeString{fullKey(node)}, expected, value = ZeString{value}
  ](auto &s) {
    s << '"' << key << "\": invalid " << expected << " \"" << value << '"';
  };
}

inline auto required(const AnyNode *node, ZuCSpan key) {
  return [
    key = ZeString{fullKey(node, key)}, bt = ZmBackTrace{1}
  ](auto &s) {
    s << '"' << key << "\" missing at:\n" << bt;
  };
}

inline auto badBool(const AnyNode *node, ZuCSpan key, ZuCSpan value) {
  return [
    key = ZeString{fullKey(node, key)}, value = ZeString{value}
  ](auto &s) {
    s << '"' << key << "\": invalid boolean \"" << value << '"';
  };
}

template <typename T, typename V>
inline auto badRange(
    const AnyNode *node, ZuCSpan key, T minimum, T maximum, V value) {
  return [
    key = ZeString{fullKey(node, key)}, minimum, maximum, value
  ](auto &s) {
    s << '"' << key << "\" out of range " <<
      "min(" << minimum << ") <= " << value <<
      " <= max(" << maximum << ')';
  };
}

template <typename Map>
inline auto badEnum(const AnyNode *node, ZuCSpan key, ZuCSpan) {
  return [key = ZeString{fullKey(node, key)}](auto &s) {
    s << '"' << key << "\" did not match { ";
    bool first = true;
    Map::all([&s, &first](ZuCSpan key, auto v) {
      if (ZuLikely(!first)) s << ", ";
      first = false;
      s << key << " = " << v;
    });
    s << " }";
  };
}

inline auto badSyntax(
    unsigned line, unsigned column, unsigned offset, char ch, uint8_t code,
    ZuCSpan fileName = {}) {
  return [
    line, column, offset, ch, code, fileName = ZeString{fileName}
  ](auto &s) {
    if (fileName)
      s << '"' << fileName << "\":" << line << ':' << column <<
	" syntax error at offset " << offset;
    else
      s << "syntax error at line " << line << ", column " << column <<
	" (offset " << offset << ')';
    if (auto r = reason(code)) s << ": " << r;
    s << " near '";
    if (ch >= 0x20 && ch < 0x7f)
      s << ch;
    else
      s << '\\' << ZuBoxed(unsigned(ch) & 0xff).
	fmt<ZuFmt::Hex<0, ZuFmt::Alt<ZuFmt::Right<2>>>>();
    s << '\'';
  };
}

inline auto badType(const AnyNode *node, ZuCSpan expected) {
  return [
    key = ZeString{fullKey(node)}, expected = ZeString{expected}
  ](auto &s) {
    s << '"' << key << "\": expected " << expected;
  };
}

inline auto badPointer(ZuCSpan path, ZuCSpan ref) {
  return [path = ZeString{path}, ref = ZeString{ref}](auto &s) {
    s << '"' << path << "\": invalid $ref \"" << ref << '"';
  };
}

inline auto badMerge(ZuCSpan path) {
  return [path = ZeString{path}](auto &s) {
    s << '"' << path << "\": incompatible allOf merge";
  };
}

inline auto badAlternative(ZuCSpan path, ZuCSpan key) {
  return [path = ZeString{path}, key = ZeString{key}](auto &s) {
    s << '"' << path << "\": cannot flatten " << key << " alternative";
  };
}

} // ZfYAMLError

#define ZfYAML_EXCEPT(...) \
  ZeMkException(Ze::Error, __FILE__, __LINE__, ZuFnName, \
    ZfYAMLError::Component, __VA_ARGS__)

namespace ZfYAML {

using namespace ZfYAMLError;

struct AsObject;
template <unsigned ElemCode, typename ElemProps = ZuTypeList<>>
struct AsArray;
template <unsigned ValCode, typename ValProps = ZuTypeList<>>
struct AsMap;
struct AsString;
struct AsJSON;

template <typename O, typename Facet, typename = ZuFields<O, Facet>>
struct AsDeflt_ { using T = AsObject; };
template <typename O, typename Facet>
struct AsDeflt_<O, Facet, ZuTypeList<>> { using T = AsString; };
struct AsDeflt {
  template <typename O, typename Facet>
  using Handler = typename AsDeflt_<O, Facet>::T::template Handler<O, Facet>;
};

} // ZfYAML

ZfYAML::AsDeflt ZfYAML_Fmt(...);

namespace ZfYAML {

template <typename O>
using As = decltype(ZfYAML_Fmt(ZuDeclVal<O *>()));

template <typename O, typename Facet>
auto handler_(const AnyNode *);

struct ZfAPI YAMLPolicy {
  template <typename Fields>
  using GetIDs = ZuFieldProp::YAML::GetIDs<Fields>;
  template <typename Props>
  using GetBytesFmt = ZuFieldProp::YAML::GetBytesFmt<Props>;
  template <typename Props>
  using GetNumberFmt = ZuFieldProp::YAML::GetNumberFmt<Props>;
  template <typename Props>
  using GetTimeFmt = ZuFieldProp::YAML::GetTimeFmt<Props>;
  template <typename O, typename Facet>
  using Handler = typename As<O>::template Handler<O, Facet>;

  static int intBase(ZuCSpan span);
  static bool implicit(ZuCSpan span);

  static bool scalar(int type, ZfTreeLoad::ScalarMask::T mask) {
    switch (type) {
      case ScalarTC::String:
	return mask & ZfTreeLoad::ScalarMask::String();
      case ScalarTC::Number:
	return mask & ZfTreeLoad::ScalarMask::Number();
      case ScalarTC::False:
      case ScalarTC::True:
	return mask & ZfTreeLoad::ScalarMask::Bool();
      case ScalarTC::Null:
	return mask & ZfTreeLoad::ScalarMask::Null();
      default:
	return false;
    }
  }

  template <typename B>
  static ZuTuple<int, B> yamlIntEOV(ZuCSpan span, unsigned base) {
    using T = typename B::T;
    unsigned i = 0, n = span.length();
    bool negative = false;
    if (span[i] == '+' || span[i] == '-') {
      negative = span[i++] == '-';
    }
    uint128_t maximum;
    if constexpr (ZuTraits<T>::IsSigned) {
      maximum = negative ? uint128_t{B::Cmp::maximum()} + 1 :
	uint128_t{B::Cmp::maximum()};
    } else {
      if (negative) return {-1, B{}};
      maximum = B::Cmp::maximum();
    }
    uint128_t value = 0;
    if (base == 60) {
      uint128_t group = 0;
      for (; i < n; ++i) {
	if (span[i] == '_') continue;
	if (span[i] == ':') {
	  if (value > (maximum - group) / 60) return {-1, B{}};
	  value = value * 60 + group;
	  group = 0;
	  continue;
	}
	unsigned digit = span[i] - '0';
	if (group > (maximum - digit) / 10) return {-1, B{}};
	group = group * 10 + digit;
      }
      if (value > (maximum - group) / 60) return {-1, B{}};
      value = value * 60 + group;
    } else {
      if (base == 2 || base == 16 ||
	  (base == 8 && i + 1 < n && span[i + 1] == 'o')) i += 2;
      for (; i < n; ++i) {
	if (span[i] == '_') continue;
	unsigned digit = span[i] >= 'a' ? span[i] - 'a' + 10 :
	  span[i] >= 'A' ? span[i] - 'A' + 10 : span[i] - '0';
	if (value > (maximum - digit) / base) return {-1, B{}};
	value = value * base + digit;
      }
    }
    T out;
    if (negative)
      out = value ? T(-T(value - 1) - 1) : T(0);
    else
      out = T(value);
    return {int(n), B{out}};
  }

  template <typename B, typename Fmt, typename Props>
  static auto intEOV(int type, ZuCSpan span) {
    if (type == ScalarTC::Number)
      if (unsigned base = intBase(span)) return yamlIntEOV<B>(span, base);
    return ZfTreeLoad::intEOV<B, Fmt, Props>(span);
  }

  template <typename T, typename Props>
  static T boolean(const AnyNode *node) {
    if (ZuLikely(node && node->has<AnyNode::String>())) {
      switch (node->scalarType) {
	case ScalarTC::False: return T{false};
	case ScalarTC::True: return T{true};
      }
      if constexpr (
	  ZuFieldProp::YAML::GetScalarFmt<Props>{} != Plain) {
	auto span = node->data<AnyNode::String>();
	if (span == "false") return T{false};
	if (span == "true") return T{true};
      }
    }
    badType(node, "boolean");
  }

  [[noreturn]] static void required(const AnyNode *node, ZuCSpan key) {
    throw ZfYAML_EXCEPT(ZfYAMLError::required(node, key));
  }
  [[noreturn]] static void badValue(
      const AnyNode *node, ZuCSpan expected, ZuCSpan value) {
    throw ZfYAML_EXCEPT(ZfYAMLError::badValue(node, expected, value));
  }
  [[noreturn]] static void badBool(
      const AnyNode *node, ZuCSpan key, ZuCSpan value) {
    throw ZfYAML_EXCEPT(ZfYAMLError::badBool(node, key, value));
  }
  [[noreturn]] static void badType(
      const AnyNode *node, ZuCSpan expected) {
    throw ZfYAML_EXCEPT(ZfYAMLError::badType(node, expected));
  }
  template <typename T, typename V>
  [[noreturn]] static void badRange(
      const AnyNode *node, ZuCSpan key, T minimum, T maximum, V value) {
    throw ZfYAML_EXCEPT(
      ZfYAMLError::badRange(node, key, minimum, maximum, value));
  }
  template <typename Map>
  [[noreturn]] static void badEnum(
      const AnyNode *node, ZuCSpan key, ZuCSpan value) {
    throw ZfYAML_EXCEPT(ZfYAMLError::badEnum<Map>(node, key, value));
  }
  template <typename O, typename Facet>
  static auto handler(const AnyNode *node) {
    return handler_<O, Facet>(node);
  }
};

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
auto loadValue(const AnyNode *);

template <typename S>
inline void indent(S &s, unsigned n) {
  while (n--) s << ' ';
}

inline bool plainKey(ZuCSpan key)
{
  unsigned n = key.length();
  if (!n || !((key[0] >= 'a' && key[0] <= 'z') ||
      (key[0] >= 'A' && key[0] <= 'Z') || key[0] == '_')) return false;
  for (unsigned i = 1; i < n; ++i)
    if (!((key[i] >= 'a' && key[i] <= 'z') ||
	(key[i] >= 'A' && key[i] <= 'Z') ||
	(key[i] >= '0' && key[i] <= '9') ||
	key[i] == '_' || key[i] == '-' || key[i] == '.')) return false;
  return !YAMLPolicy::implicit(key);
}

template <typename S>
inline void saveKey(S &s, ZuCSpan key)
{
  if (plainKey(key)) s << key;
  else ZfJSON::quote(s, key);
}

inline bool plainScalar(ZuCSpan v)
{
  unsigned n = v.length();
  if (!n || YAMLPolicy::implicit(v) ||
      !((v[0] >= 'a' && v[0] <= 'z') ||
	(v[0] >= 'A' && v[0] <= 'Z') || v[0] == '_')) return false;
  if (v[n - 1] == ' ' || v[n - 1] == '\t') return false;
  for (unsigned i = 0; i < n; ++i) {
    unsigned c = uint8_t(v[i]);
    if (c < 0x20 && c != '\t') return false;
    switch (c) {
      case ':': case '#': case '[': case ']': case '{': case '}':
      case ',': case '&': case '*': case '!': case '|': case '>':
      case '\'': case '"': case '%': case '@': case '`': return false;
    }
  }
  return true;
}

template <typename S>
void singleQuote(S &s, ZuCSpan v)
{
  for (unsigned i = 0, n = v.length(); i < n; ++i)
    if (uint8_t(v[i]) < 0x20 && v[i] != '\t' && v[i] != '\n' &&
	v[i] != '\r') {
      ZfJSON::quote(s, v);
      return;
    }
  s << '\'';
  for (unsigned i = 0, n = v.length(); i < n; ++i) {
    if (v[i] == '\'') s << '\'';
    s << v[i];
  }
  s << '\'';
}

template <typename S>
void blockScalar(S &s, ZuCSpan v, unsigned indent_, char style)
{
  unsigned n = v.length(), trailing = 0;
  while (trailing < n && v[n - trailing - 1] == '\n') ++trailing;
  s << ' ' << style;
  if (!trailing) s << '-';
  else if (trailing > 1) s << '+';
  s << '\n';
  indent(s, indent_);
  for (unsigned i = 0; i < n; ++i) {
    s << v[i];
    if (v[i] == '\n' && i + 1 < n) indent(s, indent_);
  }
}

template <typename Props, typename S>
void saveText(S &s, ZuCSpan v, unsigned indent_)
{
  constexpr unsigned Fmt = ZuFieldProp::YAML::GetScalarFmt<Props>{};
  if constexpr (Fmt == Plain) {
    s << ' ';
    if (plainScalar(v)) s << v;
    else ZfJSON::quote(s, v);
  } else if constexpr (Fmt == SingleQuoted) {
    s << ' ';
    singleQuote(s, v);
  } else if constexpr (Fmt == DoubleQuoted) {
    s << ' ';
    ZfJSON::quote(s, v);
  } else if constexpr (Fmt == BlockLiteral) {
    blockScalar(s, v, indent_, '|');
  } else {
    blockScalar(s, v, indent_, '>');
  }
}

using ScalarBuf = ZtString<ZtStringHeapID<"ZfYAML.ScalarBuf">>;

inline ZuSpan<char> scalarSpan(ScalarBuf &buf)
{
  if (buf.length() < 2 || buf[0] != '"' || buf[buf.length() - 1] != '"')
    return buf.span();
  auto span = buf.span().offset(1);
  auto r = ZfJSON::eos(span);
  return {span.data(), unsigned(r.p<0>())};
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename S, typename T>
void saveScalar(S &s, const T &v, unsigned indent_)
{
  constexpr unsigned Fmt = ZuFieldProp::YAML::GetScalarFmt<Props>{};
  if constexpr (
      TypeCode == ZfFieldTC::CString || TypeCode == ZfFieldTC::String) {
    saveText<Props>(s, ZuCSpan{v}, indent_);
  } else if constexpr (Fmt == Plain) {
    s << ' ';
    ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(s, v);
  } else {
    ScalarBuf buf;
    ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(buf, v);
    if (buf == "null") {
      s << " null";
      return;
    }
    saveText<Props>(s, scalarSpan(buf), indent_);
  }
}

template <
  typename Facet, template <typename> class Filter,
  unsigned ElemCode, typename Props, typename S, typename O>
void saveArray(S &s, const O &o, unsigned indent_)
{
  using Elem = ZuDecay<decltype(o[0])>;
  unsigned n = ZuTraits<O>::length(o);
  if (!n) {
    indent(s, indent_);
    s << "[]";
    return;
  }
  for (unsigned i = 0; i < n; ++i) {
    if (i) s << '\n';
    indent(s, indent_);
    s << '-';
    if constexpr (ElemCode == ZfFieldTC::UDT) {
      using Handler = typename As<Elem>::template Handler<Elem, Facet>;
      if constexpr (Handler::Block) {
	s << '\n';
	Handler::template save_<Filter>(s, o[i], indent_ + 2);
      } else if constexpr (Handler::Scalar) {
	ScalarBuf buf;
	Handler::template save<Filter>(buf, o[i]);
	saveText<Props>(s, scalarSpan(buf), indent_ + 2);
      } else {
	s << ' ';
	Handler::template save<Filter>(s, o[i]);
      }
    } else {
      saveScalar<Facet, Filter, ElemCode, Props>(s, o[i], indent_ + 2);
    }
  }
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename S, typename T>
void saveFieldValue(S &s, const T &v, unsigned indent_)
{
  if constexpr (ZfFieldTC::IsVec<TypeCode>{}) {
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    s << '\n';
    saveArray<Facet, Filter, ElemCode, Props>(s, v, indent_);
  } else if constexpr (TypeCode == ZfFieldTC::UDT) {
    using O = ZuDecay<T>;
    using Handler = typename As<O>::template Handler<O, Facet>;
    if constexpr (Handler::Block) {
      s << '\n';
      Handler::template save_<Filter>(s, v, indent_);
    } else if constexpr (Handler::Scalar) {
      ScalarBuf buf;
      Handler::template save<Filter>(buf, v);
      saveText<Props>(s, scalarSpan(buf), indent_);
    } else {
      s << ' ';
      Handler::template save<Filter>(s, v);
    }
  } else {
    saveScalar<Facet, Filter, TypeCode, Props>(s, v, indent_);
  }
}

template <
  typename Facet, template <typename> class Filter, typename Field,
  typename S, typename O>
bool saveField(S &s, const O &o, unsigned indent_, bool first)
{
  enum { TypeCode = Field::Type::Code };
  using Props = typename Field::Props;
  auto save = [&s, indent_, first](const auto &v) {
    if (!first) s << '\n';
    indent(s, indent_);
    saveKey(s, ZuFieldProp::YAML::GetID<Field>{}().cspan());
    s << ':';
    saveFieldValue<Facet, Filter, TypeCode, Props>(s, v, indent_ + 2);
    return true;
  };
  if constexpr (ZuFieldProp::YAML::GetOptional<Props>{}) {
    if constexpr (
	TypeCode == ZfFieldTC::CString ||
	TypeCode == ZfFieldTC::String) {
      ZuCSpan v = Field::get(o);
      if (!v) return false;
      return save(v);
    } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
      ZuBSpan v = Field::get(o);
      if (!v) return false;
      return save(v);
    } else if constexpr (
	TypeCode == ZfFieldTC::Int8 || TypeCode == ZfFieldTC::Int16 ||
	TypeCode == ZfFieldTC::Int32 || TypeCode == ZfFieldTC::Int64 ||
	TypeCode == ZfFieldTC::Int128 || TypeCode == ZfFieldTC::UInt8 ||
	TypeCode == ZfFieldTC::UInt16 || TypeCode == ZfFieldTC::UInt32 ||
	TypeCode == ZfFieldTC::UInt64 || TypeCode == ZfFieldTC::UInt128) {
      auto &&v = Field::get(o);
      if constexpr (ZuFieldProp::HasEnum<Props>{}) {
	if (v < 0) return false;
      } else if (ZuNull(v)) return false;
      return save(v);
    } else if constexpr (
	TypeCode == ZfFieldTC::Float || TypeCode == ZfFieldTC::Fixed ||
	TypeCode == ZfFieldTC::Decimal || TypeCode == ZfFieldTC::Time ||
	TypeCode == ZfFieldTC::DateTime || TypeCode == ZfFieldTC::UDT) {
      auto &&v = Field::get(o);
      if (ZuNull(v)) return false;
      return save(v);
    } else {
      return save(Field::get(o));
    }
  } else {
    return save(Field::get(o));
  }
}

struct AsObject {
  template <typename O_, typename Facet>
  struct Handler : public ZfTreeLoad::Object<YAMLPolicy, O_, Facet> {
    using O = O_;
    using Base = ZfTreeLoad::Object<YAMLPolicy, O, Facet>;
    using Base::Base;

    enum { Block = 1, Scalar = 0 };

    using AllFields = ZuFields<O, Facet>;

    template <template <typename> class Filter, typename S>
    static void save_(S &s, const O &o, unsigned indent_) {
      using Fields = ZuTypeGrep<Filter, AllFields>;
      bool first = true;
      ZuUnroll::all<Fields>([&s, &o, &first, indent_]<typename Field>() {
	if (saveField<Facet, Filter, Field>(s, o, indent_, first))
	  first = false;
      });
      if (first) {
	indent(s, indent_);
	s << "{}";
      }
    }

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      save_<Filter>(s, o, 0);
    }
  };
};

template <unsigned ElemCode, typename ElemProps>
struct AsArray {
  template <typename O_, typename Facet>
  struct Handler : public
      ZfTreeLoad::Array<YAMLPolicy, ElemCode, ElemProps, O_, Facet> {
    using O = O_;
    using Base =
      ZfTreeLoad::Array<YAMLPolicy, ElemCode, ElemProps, O, Facet>;
    using Base::Base;

    enum { Block = 1, Scalar = 0 };

    template <template <typename> class Filter, typename S>
    static void save_(S &s, const O &o, unsigned indent_) {
      saveArray<Facet, Filter, ElemCode, ElemProps>(s, o, indent_);
    }

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      save_<Filter>(s, o, 0);
    }
  };
};

template <unsigned ValCode, typename ValProps>
struct AsMap {
  template <typename O_, typename Facet>
  struct Handler : public
      ZfTreeLoad::Map<YAMLPolicy, ValCode, ValProps, O_, Facet> {
    using O = O_;
    using Base = ZfTreeLoad::Map<YAMLPolicy, ValCode, ValProps, O, Facet>;
    using Base::Base;

    enum { Block = 1, Scalar = 0 };

    template <template <typename> class Filter, typename S>
    static void save_(S &s, const O &o, unsigned indent_)
    {
      if (ZuUnlikely(!o)) {
	indent(s, indent_);
	s << "null";
	return;
      }
      bool first = true;
      {
	auto i = o->citer();
	while (auto node = i()) {
	  if (!first) s << '\n';
	  first = false;
	  indent(s, indent_);
	  saveKey(s, Base::key(node));
	  s << ':';
	  saveFieldValue<Facet, Filter, ValCode, ValProps>(
	    s, Base::val(node), indent_ + 2);
	}
      }
      if (first) {
	indent(s, indent_);
	s << "{}";
      }
    }

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) { save_<Filter>(s, o, 0); }
  };
};

struct AsStringDeflt {
  template <typename O>
  struct Handler {
    template <typename S>
    ZuInline static void save(S &s, const O &o) {
      ZfJSON::AsStringDeflt::Handler<O>::save(s, o);
    }
    ZuInline static O load(ZuCSpan span) { return O(span); }
  };
};

} // ZfYAML

ZfYAML::AsStringDeflt ZfYAML_StringFmt(...);

namespace ZfYAML {

struct AsString {
  template <typename O_, typename>
  struct Handler : public ZfTreeLoad::String<
      YAMLPolicy,
      typename decltype(ZfYAML_StringFmt(ZuDeclVal<O_ *>()))::
	template Handler<O_>, O_> {
    using O = O_;
    using Fmt = decltype(ZfYAML_StringFmt(ZuDeclVal<O *>()));
    using Handler_ = typename Fmt::template Handler<O>;
    using Base = ZfTreeLoad::String<YAMLPolicy, Handler_, O>;
    using Base::Base;

    enum { Block = 0, Scalar = 1 };

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) { Handler_::save(s, o); }
  };
};

struct AsJSON {
  template <typename O_, typename Facet>
  struct Handler : public ZfJSON::As<O_>::template Handler<O_, Facet> {
    using O = O_;
    using Base = typename ZfJSON::As<O>::template Handler<O, Facet>;
    using Base::Base;

    enum { Block = 0, Scalar = 0 };

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      Base::template save<Filter>(s, o);
    }
  };
};

template <typename O, typename Facet>
inline auto handler_(const AnyNode *node) {
  using Handler = typename As<O>::template Handler<O, Facet>;
  if (ZuUnlikely(!Handler::valid(node)))
    throw ZfYAML_EXCEPT(badType(node, "UDT"));
  return Handler{node};
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline auto loadValue(const AnyNode *node)
{
  return ZfTreeLoad::loadValue<
    YAMLPolicy, Facet, Filter, TypeCode, Props, T>(node);
}

template <
  typename Facet = ZuFacet::YAML,
  template <typename> class Filter = ZfFieldFilter::Save,
  typename S, typename O>
inline S &save(S &s, const O &v) {
  As<O>::template Handler<O, Facet>::template save<Filter>(s, v);
  return s;
}
template <typename Facet = ZuFacet::YAML, typename S, typename O>
ZuInline S &saveUpd(S &s, const O &v) {
  return save<Facet, ZfFieldFilter::Upd>(s, v);
}
template <typename Facet = ZuFacet::YAML, typename S, typename O>
ZuInline S &saveDel(S &s, const O &v) {
  return save<Facet, ZfFieldFilter::Del>(s, v);
}

template <typename O, typename Facet = ZuFacet::YAML>
auto handler(const AnyNode *node) {
  return handler_<O, Facet>(node);
}

} // ZfYAML

#endif /* ZfYAML_HH */
