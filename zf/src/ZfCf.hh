//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZfCf load/save
// - compile-time formatting
// - compile-time field matching automaton (ZuMatcher)
// - ingests bare, single-quoted, double-quoted and mixed keys/values
// - owns decoded keys, strings and number lexemes

#ifndef ZfCf_HH
#define ZfCf_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <zlib/ZuUTF.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmBackTrace.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfStruct.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfTree.hh>
#include <zlib/ZfTreeLoad.hh>
#include <zlib/ZePlatform.hh>

ZuStructFacet(Cf); // canonical Cf facet, others can be defined

namespace ZfCf { using namespace ZfJSON; }

namespace ZuFieldProp::Cf {

using namespace ZuFieldProp::JSON;

} // ZuFieldProp::Cf

namespace ZfCf {

// --- input functions

ZuDerive(DefKey, (ZtString<ZtStringBuiltin<16, ZtStringHeapID<"ZfCf.DefineKey">>>));
ZuDerive(DefVal, (ZtString<ZtStringBuiltin<48, ZtStringHeapID<"ZfCf.DefineVal">>>));
ZuDerive(Defines_, (
  ZmRBTreeKV<DefKey, DefVal,
    ZmRBTreeUnique<true,
      ZmRBTreeHeapID<"ZfCf.Defines">>>));
struct Defines : public ZuObject, public Defines_ { };

using ZfTree::AnyNode;
using ZfTree::Node_;
using ZfTree::Node;
using ZfTree::NodeArray;
using ZfTree::CNodeArray;
using ZfTree::newNode;
namespace ValueTC = ZfTree::ValueTC;
namespace ScalarTC = ZfTree::ScalarTC;

ZuInline constexpr bool isalpha__(char c) {
  return (c >= 'a' && c <= 'z') ||
    (c >= 'A' && c <= 'Z') || c == '_';
}

ZuInline constexpr bool isdigit__(char c) {
  return c >= '0' && c <= '9';
}

ZuInline constexpr bool isword__(char c) {
  return isalpha__(c) || isdigit__(c);
}

class Scan;

// PctFn must:
// - receive the mutable scan context
// - propagate expansion failure
// - call the expansion callback synchronously
//   - if not called, no expansion is performed
using PctFnHeapID = ZmFnHeapID<"ZfCf.PctFn">;
using PctExpandFn = ZmFn<bool(ZuCSpan), PctFnHeapID>;
using PctFn = ZmFn<bool(
  Scan &, ZuCSpan, ZuSpan<const ZuCSpan>, PctExpandFn), PctFnHeapID>;

struct SyntaxError {
  unsigned	offset = 0;
  unsigned	line = 1;
  unsigned	column = 1;
  char		ch = 0;
  bool		failed = false;
};

class ZfAPI Scan {
public:
  Scan(ZuCSpan span, PctFn pctFn, ZmRef<Defines> defines) :
    m_state{span}, m_defines{ZuMv(defines)}, m_pctFn{ZuMv(pctFn)} { }

  ZuCSpan span() const { return m_state.span; }
  void span(ZuCSpan span) { m_state = {span}; }

  const SyntaxError &error() const { return m_state.error; }
  SyntaxError &error() { return m_state.error; }

  const ZmRef<Defines> &defines() const { return m_defines; }
  ZmRef<Defines> &defines() { return m_defines; }

  ZuTuple<int, ZuPtr<const AnyNode>> scan();

private:
  struct State {
    ZuCSpan	span;
    SyntaxError	error;
    unsigned	offset = 0;
    unsigned	lineOffset = 0;
    unsigned	line = 1;
  };

  unsigned position(ZuCSpan at, unsigned offset = 0);
  void fail(ZuCSpan at, unsigned offset = 0);

  bool appendDefine(ZuCSpan key, AnyNode::String &out);
  void setDefine(ZuCSpan key, ZuCSpan value);

  // eos() scans a bare, quoted, or mixed key/value token into owned storage
  // - Key selects key delimiters and disables ${...} expansion
  // - returns the offset past the token
  // - returns -1 on invalid input
  template <bool Key>
  int eos(ZuCSpan span, AnyNode::String &out);

  // bok() finds the beginning of a key, processing comments and line-level
  // directives
  // - returns {offset, true} when a key is found
  // - returns {offset, false} at the end of the object
  // - returns {-1, false} on directive failure
  ZuTuple<int, bool> bok(ZuCSpan span, AnyNode *node);

  // bov() returns the beginning of a value together with its node type
  // - returns {offset, type}
  // - returns {-1, -1} if the input is invalid or no value is found
  ZuTuple<int, int> bov(ZuCSpan span);

  // eod() scans and executes a line-level % directive
  // - returns the offset past the directive line
  // - returns -1 on invalid input or directive failure
  int eod(ZuCSpan span, AnyNode *node);

  // eov() scans a value, allocating and returning the appropriate Node
  // - returns {offset, node}
  // - returns {-1, nullptr} on invalid input
  ZuTuple<int, ZuPtr<AnyNode>> eov(ZuCSpan span, AnyNode *parent);

  // eov_Array() scans an array, allocating and returning a new Node
  // - returns {offset, node}
  // - returns {-1, nullptr} on invalid input
  ZuTuple<int, ZuPtr<AnyNode>> eov_Array(ZuCSpan span, AnyNode *parent);

  // eov_Object() scans an object into an existing Node
  // - using an existing node permits % directive expansion in-place
  // - returns the offset past the object
  // - returns -1 on invalid input
  int eov_Object(ZuCSpan span, AnyNode *node, bool root);

  State			m_state;
  ZmRef<Defines>	m_defines;
  PctFn			m_pctFn;
};

// scan configuration data, build parse tree
// - throws ZfCfError::badSyntax on syntax, directive or trailing-input failure
ZfExtern ZuTuple<int, ZuPtr<const AnyNode>> scan(
  ZuCSpan span, PctFn pctFn = {}, ZmRef<Defines> defines = new Defines());

} // ZfCf

namespace ZfCfError {

// --- exceptions thrown by ZfCf

using AnyNode = ZfCf::AnyNode;

constexpr auto Component = "ZfCf"_Zu;

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
      " <= max(" << maximum << ")";
  };
}

template <typename Map>
inline auto badEnum(const AnyNode *node, ZuCSpan key, ZuCSpan value) {
  return [
    key = ZeString{fullKey(node, key)}, value = ZeString{value}
  ](auto &s) {
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
    unsigned line, unsigned column, unsigned offset, char ch,
    ZuCSpan fileName = {}) {
  return [
    line, column, offset, ch, fileName = ZeString{fileName}
  ](auto &s) {
    if (fileName)
      s << '"' << fileName << "\":" << line << ':' << column <<
	" syntax error at offset " << offset;
    else
      s << "syntax error at line " << line << ", column " << column <<
	" (offset " << offset << ')';
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

inline auto badDefine(ZuCSpan define, ZuCSpan fileName) {
  return [
    define = ZeString{define}, fileName = ZeString{fileName}
  ](auto &s) {
    if (fileName) s << '"' << fileName << "\": ";
    s << "bad %define \"" << define << '"';
  };
}

} // ZfCfError

#define ZfCf_EXCEPT(...) \
  ZeMkException(Ze::Error, __FILE__, __LINE__, ZuFnName, \
    ZfCfError::Component, __VA_ARGS__)

namespace ZfCf {

using namespace ZfCfError;

// --- field save/load

struct AsObject;	// as Cf object {...}
template <unsigned ElemCode, typename ElemProps = ZuTypeList<>>
struct AsArray;		// as Cf array [...]
template <unsigned ValCode, typename ValProps = ZuTypeList<>>
struct AsMap;		// as Cf object {...} with homogeneous values
struct AsString;	// as Cf string

template <typename ...Ts>
struct Union : public ZuUnion<void, const AnyNode *, Ts...> {
friend inline AsObject ZfCf_Fmt(Union *);
  ZuDerive_(Union, (ZuUnion<void, const AnyNode *, Ts...>));
};

// if fields are defined, default to AsObject
template <typename O, typename Facet, typename = ZuFields<O, Facet>>
struct AsDeflt_ { using T = AsObject; };
// ... otherwise fall back to AsString
template <typename O, typename Facet>
struct AsDeflt_<O, Facet, ZuTypeList<>> { using T = AsString; };
struct AsDeflt {
  template <typename O, typename Facet>
  using Handler = typename AsDeflt_<O, Facet>::T::template Handler<O, Facet>;
};

} // ZfCf

ZfCf::AsDeflt ZfCf_Fmt(...); // default

namespace ZfCf {

template <typename O>
using As = decltype(ZfCf_Fmt(ZuDeclVal<O *>()));

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

template <typename O, typename Facet>
auto handler_(const AnyNode *);

struct CfPolicy {
  template <typename Fields>
  using GetIDs = ZuFieldProp::Cf::GetIDs<Fields>;
  template <typename Props>
  using GetBytesFmt = ZuFieldProp::Cf::GetBytesFmt<Props>;
  template <typename Props>
  using GetNumberFmt = ZuFieldProp::Cf::GetNumberFmt<Props>;
  template <typename Props>
  using GetTimeFmt = ZuFieldProp::Cf::GetTimeFmt<Props>;
  template <typename O, typename Facet>
  using Handler = typename As<O>::template Handler<O, Facet>;
  template <typename O>
  using IsObjPtr = ZfCf::IsObjPtr<O>;

  static bool scalar(int type, ZfTreeLoad::ScalarMask::T) {
    return type == ScalarTC::String;
  }

  template <typename B, typename Fmt, typename Props>
  static auto intEOV(int, ZuCSpan span) {
    return ZfTreeLoad::intEOV<B, Fmt, Props>(span);
  }

  template <typename T, typename>
  static T boolean(const AnyNode *node) {
    auto span = ZfTreeLoad::scalar<CfPolicy>(
      node, ZfTreeLoad::ScalarMask::String(), "boolean");
    try {
      return ZtScanBool<true>(span);
    } catch (const ZtBadBool &) {
      badBool(node, {}, span);
    }
  }

  [[noreturn]] static void required(const AnyNode *node, ZuCSpan key) {
    throw ZfCf_EXCEPT(ZfCfError::required(node, key));
  }
  [[noreturn]] static void badValue(
      const AnyNode *node, ZuCSpan expected, ZuCSpan value) {
    throw ZfCf_EXCEPT(ZfCfError::badValue(node, expected, value));
  }
  [[noreturn]] static void badBool(
      const AnyNode *node, ZuCSpan key, ZuCSpan value) {
    throw ZfCf_EXCEPT(ZfCfError::badBool(node, key, value));
  }
  [[noreturn]] static void badType(
      const AnyNode *node, ZuCSpan expected) {
    throw ZfCf_EXCEPT(ZfCfError::badType(node, expected));
  }
  template <typename T, typename V>
  [[noreturn]] static void badRange(
      const AnyNode *node, ZuCSpan key, T minimum, T maximum, V value) {
    throw ZfCf_EXCEPT(
      ZfCfError::badRange(node, key, minimum, maximum, value));
  }
  template <typename Map>
  [[noreturn]] static void badEnum(
      const AnyNode *node, ZuCSpan key, ZuCSpan value) {
    throw ZfCf_EXCEPT(ZfCfError::badEnum<Map>(node, key, value));
  }
  template <typename O, typename Facet>
  static auto handler(const AnyNode *node) {
    return handler_<O, Facet>(node);
  }
};

// load an individual value
template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
auto loadValue(const AnyNode *);

template <
  typename Facet, template <typename> class Filter, typename Field,
  typename S, typename O>
bool saveField(S &, const O &, bool);

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename S, typename T>
void saveValue(S &, const T &);

// save/load handler for object-formatted types {...}
struct AsObject {
  template <typename O_, typename Facet>
  struct Handler : public ZfTreeLoad::Object<CfPolicy, O_, Facet> {
    using O = O_;
    using Base = ZfTreeLoad::Object<CfPolicy, O, Facet>;
    using Base::Base;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      using Fields = ZuTypeGrep<Filter, ZuFields<O, Facet>>;
      s << '{';
      bool first = true;
      ZuUnroll::all<Fields>([&s, &o, &first]<typename Field>() {
	if (saveField<Facet, Filter, Field>(s, o, first)) first = false;
      });
      s << '}';
    }
  };

  // Any actual parse-tree node is retained without inspecting its shape.
  // The parse tree must remain alive and callers must resolve the non-owning
  // raw-node state before semantic decoding or non-null saving.
  template <typename ...Ts, typename Facet>
  struct Handler<Union<Ts...>, Facet> {
    using O = Union<Ts...>;

    const AnyNode *node;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      auto type = o.type();
      if (ZuUnlikely(type < 2)) { s << "null"; return; }
      ZuSwitch::dispatch<O::N - 2>(type - 2, [&s, &o](auto I_) {
	enum { I = I_ + 2 };
	using V = typename O::template Type<I>;
	const auto &v = o.template p<I>();
	if constexpr (!IsObjPtr<V>{}) {
	  As<V>::template Handler<V, Facet>::template save<Filter>(s, v);
	} else {
	  using U = ZuDecay<decltype(*v)>;
	  if (ZuLikely(v))
	    As<U>::template Handler<U, Facet>::template save<Filter>(s, *v);
	  else
	    s << "null";
	}
      });
    }

    static bool valid(const AnyNode *node) {
      return node != nullptr;
    }
    Handler(const AnyNode *node_) : node{node_} { }
    O ctor() const { return O(node); }
    O *alloc() const { return new O(node); }
    void new_(void *o) const { new (o) O(node); }
    void load(O &o) const { o = node; }
    void update(O &o) const { o = node; }
  };
};

// save/load handler for array-formatted types [...]
template <unsigned ElemCode, typename ElemProps>
struct AsArray {
  template <typename O_, typename Facet>
  struct Handler : public
      ZfTreeLoad::Array<CfPolicy, ElemCode, ElemProps, O_, Facet> {
    using O = O_;
    using Base =
      ZfTreeLoad::Array<CfPolicy, ElemCode, ElemProps, O, Facet>;
    using Base::Base;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      s << '[';
      for (unsigned i = 0, n = ZuTraits<O>::length(o); i < n; ++i) {
	if (i) s << ',';
	saveValue<Facet, Filter, ElemCode, ElemProps>(s, o[i]);
      }
      s << ']';
    }
  };
};

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename S, typename T>
void saveValue(S &s, const T &v)
{
  if constexpr (ZfFieldTC::IsVec<TypeCode>{}) {
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    unsigned n = ZuTraits<T>::length(v);
    s << '[';
    for (unsigned i = 0; i < n; i++) {
      if (i) s << ',';
      saveValue<Facet, Filter, ElemCode, Props>(s, v[i]);
    }
    s << ']';
  } else if constexpr (TypeCode == ZfFieldTC::UDT) {
    using O = ZuDecay<T>;
    if constexpr (!IsObjPtr<O>{})
      As<O>::template Handler<O, Facet>::template save<Filter>(s, v);
    else if (ZuLikely(v)) {
      using U = ZuDecay<decltype(*v)>;
      As<U>::template Handler<U, Facet>::template save<Filter>(s, *v);
    } else
      s << "null";
  } else {
    ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(s, v);
  }
}

template <
  typename Facet, template <typename> class Filter, typename Field,
  typename S, typename O>
bool saveField(S &s, const O &o, bool first)
{
  enum { TypeCode = Field::Type::Code };
  using Props = typename Field::Props;
  auto save = [&s, first](const auto &v) {
    if (!first) s << ',';
    ZfJSON::quote(s, ZuFieldProp::Cf::GetID<Field>{}().cspan());
    s << ':';
    saveValue<Facet, Filter, TypeCode, Props>(s, v);
    return true;
  };
  if constexpr (ZuFieldProp::Cf::GetOptional<Props>{}) {
    if constexpr (
	TypeCode == ZfFieldTC::CString || TypeCode == ZfFieldTC::String) {
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

template <unsigned ValCode, typename ValProps>
struct AsMap {
  template <typename O_, typename Facet>
  struct Handler : public
      ZfTreeLoad::Map<CfPolicy, ValCode, ValProps, O_, Facet> {
    using O = O_;
    using Base = ZfTreeLoad::Map<CfPolicy, ValCode, ValProps, O, Facet>;
    using Base::Base;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o)
    {
      s << '{';
      bool first = true;
      {
	auto i = o.citer();
	while (auto node = i()) {
	  if (!first) s << ',';
	  first = false;
	  ZfJSON::quote(s, node->key());
	  s << ':';
	  saveValue<Facet, Filter, ValCode, ValProps>(s, node->val());
	}
      }
      s << '}';
    }
  };
};

// save/load handler for strings
// - custom handler skeleton:
// struct Fmt {
//   template <typename O>
//   struct Handler {
//     template <typename S>
//     void save(S &s, const O &o) { ...; }
//     O load(ZuCSpan span) { return O{...}; }
//   }
// };
// class A {
//   ...
//   friend inline Fmt ZfCf_StringFmt(A *); // bind Fmt to A
// };

struct AsStringDeflt {	// default string formatter
  template <typename O>
  struct Handler {
    template <typename S>
    ZuInline static void save(S &s, const O &o) {
      ZfJSON::AsStringDeflt::Handler<O>::save(s, o);
    }
    ZuInline static O load(ZuCSpan span) { // string is already unquoted
      return O(span);
    }
  };
};

} // ZfCf

ZfCf::AsStringDeflt ZfCf_StringFmt(...);

namespace ZfCf {

struct AsString {
  template <typename O_, typename>
  struct Handler : public ZfTreeLoad::String<
      CfPolicy,
      typename decltype(ZfCf_StringFmt(ZuDeclVal<O_ *>()))::
	template Handler<O_>, O_> {
    using O = O_;
    using Fmt = decltype(ZfCf_StringFmt(ZuDeclVal<O *>()));
    using Handler_ = typename Fmt::template Handler<O>;
    using Base = ZfTreeLoad::String<CfPolicy, Handler_, O>;
    using Base::Base;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) { Handler_::save(s, o); }
  };
};

template <typename O, typename Facet>
inline auto handler_(const AnyNode *node) {
  using Handler = typename As<O>::template Handler<O, Facet>;
  if (ZuUnlikely(!Handler::valid(node)))
    throw ZfCf_EXCEPT(badType(node, "UDT"));
  return Handler{node};
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline auto loadValue(const AnyNode *node)
{
  return ZfTreeLoad::loadValue<
    CfPolicy, Facet, Filter, TypeCode, Props, T>(node);
}

template <
  typename Facet = ZuFacet::Cf,
  template <typename> class Filter = ZfFieldFilter::Save,
  typename S, typename O>
inline S &save(S &s, const O &v) {
  if constexpr (!IsObjPtr<O>{})
    As<O>::template Handler<O, Facet>::template save<Filter>(s, v);
  else if (ZuLikely(v)) {
    using U = ZuDecay<decltype(*v)>;
    As<U>::template Handler<U, Facet>::template save<Filter>(s, *v);
  } else
    s << "null";
  return s;
}
template <
  typename Facet = ZuFacet::Cf,
  typename S, typename O>
ZuInline S &saveUpd(S &s, const O &v) {
  return save<Facet, ZfFieldFilter::Upd>(s, v);
}
template <
  typename Facet = ZuFacet::Cf,
  typename S, typename O>
ZuInline S &saveDel(S &s, const O &v) {
  return save<Facet, ZfFieldFilter::Del>(s, v);
}

template <typename O, typename Facet = ZuFacet::Cf>
auto handler(const AnyNode *node) {
  return handler_<O, Facet>(node);
}

} // ZfCf

#endif /* ZfCf_HH */
