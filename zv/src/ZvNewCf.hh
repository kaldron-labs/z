//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZvNewCf load/save
// - compile-time formatting
// - compile-time field matching automaton (ZuMatcher)
// - ingests bare, single-quoted, double-quoted and mixed keys/values
// - owns decoded keys, strings and number lexemes

#ifndef ZvNewCf_HH
#define ZvNewCf_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <limits.h>
#include <math.h>

#include <zlib/ZuDecimal.hh>
#include <zlib/ZuMArray.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuUTF.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmBackTrace.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtLocalString.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtStruct.hh>
#include <zlib/ZtBytesFmt.hh>
#include <zlib/ZtJSON.hh>
#include <zlib/ZePlatform.hh>

ZuStructFacet(Cf); // canonical Cf facet, others can be defined

namespace ZvNewCf { using namespace ZtJSON; }

namespace ZuFieldProp::Cf {

using namespace ZuFieldProp::JSON;

} // ZuFieldProp::Cf

namespace ZvNewCf {

// --- input functions

ZuDerive(DefKey, (ZtString<ZtStringBuiltin<16, ZtStringHeapID<"ZvNewCf.DefineKey">>>));
ZuDerive(DefVal, (ZtString<ZtStringBuiltin<48, ZtStringHeapID<"ZvNewCf.DefineVal">>>));
ZuDerive(Defines_, (
  ZmRBTreeKV<DefKey, DefVal,
    ZmRBTreeUnique<true,
      ZmRBTreeHeapID<"ZvNewCf.Defines">>>));
struct Defines : public ZuObject, public Defines_ { };

struct Node_HeapID : public ZuStringT<"ZvNewCf.Node"> { };

// node in a scan tree
class AnyNode {
  AnyNode() = delete;
  AnyNode(const AnyNode &) = delete;
  AnyNode &operator =(const AnyNode &) = delete;
  AnyNode(AnyNode &&) = delete;
  AnyNode &operator =(AnyNode &&) = delete;

public:
  AnyNode	*parent = nullptr;
  int		type;

  AnyNode(int type_) : type(type_) { }
  virtual ~AnyNode() = default;

  template <typename Data>
  bool has() const;

  template <typename Data>
  decltype(auto) data(this auto &&);

  // path is in "Member" format, see ZtURI.hh
  const AnyNode *resolve(ZuCSpan path) const;
  template <typename S> void path(S &s) const;

  static constexpr unsigned LNodeSize = 512;
  static constexpr unsigned SNodeSize = 48;

  static constexpr int StringSize =
    (SNodeSize - (sizeof(ZtString<>) - ZtString<>::BuiltinSize));
  ZuAssert(StringSize > 0);
  ZuDerive(String, (ZtString<
      ZtStringBuiltin<StringSize, ZtStringHeapID_<Node_HeapID>>>));

  ZuDerive(Field, (ZuTuple<String, ZuPtr<AnyNode>>));

  static constexpr int ArraySize =
    (LNodeSize - sizeof(ZtArray<ZuPtr<AnyNode>>)) / sizeof(ZuPtr<AnyNode>);
  static constexpr int ObjectSize =
    (LNodeSize - sizeof(ZtArray<Field>)) / sizeof(Field);
  ZuAssert(ArraySize > 0);
  ZuAssert(ObjectSize > 0);

  ZuDerive(Array, (ZtBuiltin<
      ZtArray<ZuPtr<AnyNode>, ZtArrayHeapID_<Node_HeapID>>, ArraySize>));
  ZuDerive(Object, (ZtBuiltin<
      ZtArray<Field, ZtArrayHeapID_<Node_HeapID>>, ObjectSize>));

  using TL = ZuTypeList<Array, Object, String>;

  template <typename T>
  using Index = ZuTypeIndex<T, TL>;
};

// value type enum (with same values as the AnyNode typelist indices)
namespace ValueTC {
  enum {
    Array = AnyNode::Index<AnyNode::Array>{},
    Object = AnyNode::Index<AnyNode::Object>{},
    String = AnyNode::Index<AnyNode::String>{}
  };
}

template <typename Data, typename Heap>
class Node_ : public Heap, public AnyNode {
  Node_(const Node_ &) = delete;
  Node_ &operator =(const Node_ &) = delete;
  Node_(Node_ &&) = delete;
  Node_ &operator =(Node_ &&) = delete;

public:
  using AnyNode::TL;

  Node_() : AnyNode{ZuTypeIndex<Data, TL>{}()} { }
  template <typename ...Args>
  Node_(Args &&...args) :
    AnyNode{ZuTypeIndex<Data, TL>{}()},
    data(ZuFwd<Args>(args)...) { }
  ~Node_() = default;

  Data	data;
};

template <typename Data>
struct Node : public Node_<Data, ZmHeap_<Node_HeapID, Node_<Data, ZuVoid>>> {
  using Base = Node_<Data, ZmHeap_<Node_HeapID, Node_<Data, ZuVoid>>>;
  using Base::Base;
  template <typename ...Args,
    decltype(Base(ZuDeclVal<Args &&>()...), int()) = 0>
  Node(Args &&...args) : Base(ZuFwd<Args>(args)...) { }
};

template <typename Data>
inline bool AnyNode::has() const {
  return type == ZuTypeIndex<Data, TL>{};
}

template <typename Data>
inline decltype(auto) AnyNode::data(this auto &&self) {
  using Self = decltype(self);
  using NodeT = Node<Data>;
  return ZuFwdLike<Self>(ZuFwdLike<Self, NodeT>(self).data);
}

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

inline const AnyNode *AnyNode::resolve(ZuCSpan path) const {
  const AnyNode *node = this;
  while (path) {
    unsigned i = 0, n = path.length();
    if (path[0] == '[') {
      if (!node->has<Array>() || n < 3) return nullptr;
      unsigned index = 0;
      while (++i < n) {
	auto c = path[i];
	if (!isdigit__(c)) break;
	unsigned digit = c - '0';
	if (index > (UINT_MAX - digit) / 10) return nullptr;
	index = (index * 10) + digit;
      }
      if (i == 1 || i >= n || path[i] != ']') return nullptr;
      const auto &array = node->data<Array>();
      if (index >= array.length() || !array[index]) return nullptr;
      node = array[index].ptr();
      ++i;
      if (i < n) {
	if (path[i] == '[') { path.offset(i); continue; }
	if (path[i] != '.' || ++i >= n || path[i] == '[') return nullptr;
      }
    } else {
      if (!node->has<Object>() || path[0] == '.') return nullptr;
      while (++i < n && path[i] != '[' && path[i] != '.');
      ZuCSpan id{path.data(), i};
      const auto &fields = node->data<Object>();
      node = nullptr;
      for (auto &&field: fields) {
	if (field.p<0>() == id) {
	  node = field.p<1>().ptr();
	  break;
	}
      }
      if (!node) return nullptr;
      if (i < n && path[i] == '.') {
	if (++i >= n || path[i] == '[') return nullptr;
      }
    }
    path.offset(i);
  }
  return node;
}

// path() intentionally uses linear search
// - it is intended for diagnosing misconfiguration, nothing more
// - it is only used when throwing exceptions
//   - typically during a failing program start
// - it should never be called in a hot path
template <typename S> void AnyNode::path(S &s) const {
  if (!parent) return;
  parent->path(s);
  if (parent->has<Object>()) {
    if (parent->parent) s << '.';
    const auto &fields = parent->data<Object>();
    for (auto &&field: fields)
      if (field.p<1>().ptr() == this) {
	s << field.p<0>();
	break;
      }
  } else {
    const auto &array = parent->data<Array>();
    for (unsigned i = 0, n = array.length(); i < n; i++)
      if (array[i].ptr() == this) {
	s << '[' << i << ']';
	break;
      }
  }
}

using NodeArray = typename AnyNode::Array;
using CNodeArray = const NodeArray;

// scan a bare, quoted or mixed token into owned storage
template <bool Key>
ZvExtern int eos(ZuCSpan span, AnyNode::String &out);
extern template int eos<false>(ZuCSpan, AnyNode::String &);
extern template int eos<true>(ZuCSpan, AnyNode::String &);

// PctFn must:
// - propagate expansion failure
// - call the expansion callback synchronously
//   - if not called, no expansion is performed
using PctFnHeapID = ZmFnHeapID<"ZvNewCf.PctFn">;
using PctExpandFn = ZmFn<bool(ZuCSpan), PctFnHeapID>;
using PctFn = ZmFn<bool(
  ZuCSpan, ZuSpan<const ZuCSpan>, PctExpandFn), PctFnHeapID>;
// scan configuration data, build parse tree
ZvExtern ZuTuple<int, ZuPtr<const AnyNode>> scan(
  ZuCSpan span, PctFn pctFn = {}, ZmRef<Defines> defines = new Defines());

template <typename Data, typename ...Args>
inline auto newNode(Args && ...args) {
  using T = Node<Data>;
  return ZuPtr<T>{new T(ZuFwd<Args>(args)...)};
}

// bov() returns the beginning of a value together with the type of the value
// - returns {-1, -1} if the input is corrupt or no value is found
ZvExtern ZuTuple<int, int> bov(ZuCSpan span);

// eov_Array() scans an array, allocating and returning a new Node
// - returns {offset, node}
// - returns {-1, nullptr} on invalid input
ZvExtern ZuTuple<int, ZuPtr<AnyNode>> eov_Array(ZuCSpan span);

// eov_Object() scans an object, allocating and returning a new Node
// - returns {offset, node}
// - returns {-1, nullptr} on invalid input
ZvExtern ZuTuple<int, ZuPtr<AnyNode>> eov_Object(
  ZuCSpan span, bool root = false);

} // ZvNewCf

namespace ZvNewCfError {

// --- exceptions thrown by ZvNewCf

using AnyNode = ZvNewCf::AnyNode;

constexpr auto Component = "ZvNewCf"_Zu;

inline ZeString fullKey(const AnyNode *node, ZuCSpan key = {}) {
  ZeString s;
  node->path(s);
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

inline auto badSyntax(unsigned line, char ch, ZuCSpan fileName) {
  return [
    line, ch, fileName = ZeString{fileName}
  ](auto &s) {
    if (fileName)
      s << '"' << fileName << "\":" << line << " syntax error";
    else
      s << "syntax error at line " << line;
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

template <ZuString Op>
inline auto fileError(ZuCSpan fileName, ZeError e) {
  return [
    fileName = ZeString{fileName}, e
  ](auto &s) {
    s << Op.cspan() << "(\"" << fileName << "\"): " << e;
  };
}

inline auto file2Big(ZuCSpan fileName) {
  return [
    fileName = ZeString{fileName}
  ](auto &s) {
    s << '"' << fileName << "\": file too big";
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

} // ZvNewCfError

#define ZvNewCf_EXCEPT(...) \
  ZeMkException(Ze::Error, __FILE__, __LINE__, ZuFnName, \
    ZvNewCfError::Component, __VA_ARGS__)

namespace ZvNewCf {

using namespace ZvNewCfError;

// --- field save/load

struct AsObject;	// as Cf object {...}
template <unsigned ElemCode, typename ElemProps = ZuTypeList<>>
struct AsArray;		// as Cf array [...]
struct AsString;	// as Cf string

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

} // ZvNewCf

ZvNewCf::AsDeflt ZvNewCf_Fmt(...); // default

namespace ZvNewCf {

template <typename O>
using As = decltype(ZvNewCf_Fmt(ZuDeclVal<O *>()));

// load an individual value
template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
auto loadValue(const AnyNode *);

// save/load handler for object-formatted types {...}
struct AsObject {
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;

    template <typename Field>
    using CtorIndex = ZuFieldProp::GetCtor<typename Field::Props>;

    using AllFields = ZuFields<O, Facet>;
    using LoadFields = ZuTypeGrep<ZtFieldFilter::Load, AllFields>;
    using SaveFields = ZuTypeGrep<ZtFieldFilter::Save, AllFields>;
    using CtorFields_ = ZuTypeGrep<ZtFieldFilter::Ctor, AllFields>;
    using CtorFields = ZuTypeSort<CtorIndex, CtorFields_>;
    using InitFields = ZuTypeGrep<ZtFieldFilter::Init, AllFields>;
    using UpdFields = ZuTypeGrep<ZtFieldFilter::Upd, AllFields>;
    using DelFields = ZuTypeGrep<ZtFieldFilter::Del, AllFields>;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      ZtJSON::AsObject::Handler<O, Facet>::template save<Filter>(s, o);
    }

    const AnyNode	*node;
    int			lookup[SaveFields::N];

    static bool valid(const AnyNode *node) {
      return node->has<AnyNode::Object>();
    }

    Handler(const AnyNode *node_) : node{node_} {
      for (unsigned i = 0; i < SaveFields::N; i++) lookup[i] = -1;
      if (node->has<AnyNode::Object>()) {
	constexpr auto matcher =
	  ZuMatcher<ZuFieldProp::Cf::GetIDs<SaveFields>>();
	unsigned matched = 0;
	const auto &fields = node->data<AnyNode::Object>();
	for (unsigned i = 0, n = fields.length(); i < n; i++) {
	  auto j = matcher.match(fields[i].p<0>());
	  if (j >= 0) {
	    lookup[j] = i;
	    if (++matched >= SaveFields::N) break;
	  }
	}
      }
    }

    template <template <typename> class Filter, typename Field>
    auto loadField() const {
      enum { TypeCode = Field::Type::Code };
      using Props = typename Field::Props;
      using T = typename Field::T;
      using R = decltype(
	loadValue<Facet, Filter, TypeCode, Props, T>(ZuDeclVal<AnyNode *>()));
      {
	enum { I = ZuTypeIndex<Field, SaveFields>{} };
	auto j = lookup[I];
	if (j >= 0) {
	  auto &fields = node->data<AnyNode::Object>();
	  AnyNode *node = fields[j].template p<1>();
	  return loadValue<Facet, Filter, TypeCode, Props, T>(node);
	}
      }
      if constexpr (TypeCode == ZtFieldTC::BytesVec) {
	return R{};
      } else if constexpr (ZtFieldTC::IsVec<TypeCode>{}) {
	// ZuMArray needs an underlying reference
	static const NodeArray _;
	return R(_);
      } else
	return R{Field::deflt()};
    }

    template <typename ...Field>
    struct Ctor {
      template <typename ...Args>
      static O ctor(const Handler &handler, Args &&...args) {
	return O(
	  ZuFwd<Args>(args)...,
	  handler.loadField<ZtFieldFilter::Load, Field>()...);
      }
      template <typename ...Args>
      static void new_(void *o, const Handler &handler, Args &&...args) {
	new (o) O(
	  ZuFwd<Args>(args)...,
	  handler.loadField<ZtFieldFilter::Load, Field>()...);
      }
    };
    template <typename ...Args>
    O ctor(Args &&...args) const {
      if constexpr (!InitFields::N) // exploit guaranteed copy elision
	return ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
      else {
	O o = ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
	ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	  Field::set(o, this->loadField<ZtFieldFilter::Load, Field>());
	});
	return o;
      }
    }
    template <typename ...Args>
    void new_(void *o_, Args &&...args) const {
      ZuTypeApply<Ctor, CtorFields>::new_(o_, *this, ZuFwd<Args>(args)...);
      O &o = *static_cast<O *>(o_);
      ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	Field::set(o, this->loadField<ZtFieldFilter::Load, Field>());
      });
    }

    void load(O &o) const {
      ZuUnroll::all<LoadFields>([this, &o]<typename Field>() {
	Field::set(o, this->loadField<ZtFieldFilter::Load, Field>());
      });
    }
    void update(O &o) const {
      ZuUnroll::all<UpdFields>([this, &o]<typename Field>() {
	Field::set(o, this->loadField<ZtFieldFilter::Upd, Field>());
      });
    }
  };
};

// LoadVec wraps NodeArray, parsing each span on demand
template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
struct LoadVec :
  public ZuMArray<LoadVec<Facet, Filter, TypeCode, Props, T>, CNodeArray, T>
{
  ZuDerive_(LoadVec, (ZuMArray<LoadVec, CNodeArray, T>));
  using Base::underlying;
  T get(unsigned i) const & {
    return loadValue<Facet, Filter, TypeCode, Props, T>(underlying[i]);
  }
  template <typename V> void set(unsigned, const V &) { } // unused
};

// save/load handler for array-formatted types [...]
template <unsigned ElemCode, typename ElemProps>
struct AsArray {
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) {
      ZtJSON::AsArray<ElemCode, ElemProps>::
	template Handler<O, Facet>::template save<Filter>(s, o);
    }

    const AnyNode	*node;

    static bool valid(const AnyNode *node) {
      return node->has<AnyNode::Array>();
    }

    Handler(const AnyNode *node_) : node{node_} { }

    using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
    using LoadVec_ =
      LoadVec<Facet, ZtFieldFilter::Load, ElemCode, ElemProps, Elem>;
    template <typename ...Args>
    O ctor(Args &&...args) const {
      if (ZuUnlikely(!node->has<AnyNode::Array>()))
	return O(ZuFwd<Args>(args)...);
      return O(ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
    }
    template <typename ...Args>
    void new_(void *o, Args &&...args) const {
      if (ZuUnlikely(!node->has<AnyNode::Array>()))
	new (o) O(ZuFwd<Args>(args)...);
      else
	new (o) O(ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
    }

    void load(O &o) const {
      if (ZuLikely(node->has<AnyNode::Array>()))
	o = LoadVec_(node->data<AnyNode::Array>());
    }
    void update(O &o) const {
      if (ZuUnlikely(!node->has<AnyNode::Array>())) return;
      const auto &nodes = node->data<AnyNode::Array>();
      unsigned n = ZuTraits<O>::length(o);
      unsigned m = nodes.length();
      if (n > m) n = m;
      if constexpr (ElemCode == ZtFieldTC::UDT) {
	using ElemHandler = typename As<Elem>::template Handler<Elem, Facet>;
	for (unsigned i = 0; i < n; i++)
	  ElemHandler{nodes[i]}.update(o[i]);
      } else {
	for (unsigned i = 0; i < n; i++)
	  o[i] = loadValue<
	    Facet, ZtFieldFilter::Upd, ElemCode, ElemProps, Elem>(nodes[i]);
      }
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
//   friend inline Fmt ZvNewCf_StringFmt(A *); // bind Fmt to A
// };

struct AsStringDeflt {	// default string formatter
  template <typename O>
  struct Handler {
    template <typename S>
    ZuInline static void save(S &s, const O &o) {
      ZtJSON::AsStringDeflt::Handler<O>::save(s, o);
    }
    ZuInline static O load(ZuCSpan span) { // string is already unquoted
      return O(span);
    }
  };
};

} // ZvNewCf

ZvNewCf::AsStringDeflt ZvNewCf_StringFmt(...);

namespace ZvNewCf {

struct AsString {
  template <typename O_, typename>
  struct Handler {
    using O = O_;
    using Fmt = decltype(ZvNewCf_StringFmt(ZuDeclVal<O *>()));
    using Handler_ = typename Fmt::template Handler<O>;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &o) { Handler_::save(s, o); }

    const AnyNode	*node;

    static bool valid(const AnyNode *node) {
      return node->has<AnyNode::String>();
    }

    Handler(const AnyNode *node_) : node{node_} { }

    ZuCSpan span() const {
      if (!node->has<AnyNode::String>()) return {};
      return node->data<AnyNode::String>();
    }

    O ctor() const { return Handler_::load(span()); }
    void new_(void *o) const { new (o) O(Handler_::load(span())); }
    void load(O &o) const { o = Handler_::load(span()); }
    void update(O &o) const { o = Handler_::load(span()); }
  };
};

template <typename O, typename Facet>
inline auto handler_(const AnyNode *node) {
  using Handler = typename As<O>::template Handler<O, Facet>;
  if (ZuUnlikely(!Handler::valid(node)))
    throw ZvNewCf_EXCEPT(badType(node, "UDT"));
  return Handler{node};
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline T loadValue_(const AnyNode *node)
{
  auto type = node->type;

  if constexpr (TypeCode == ZtFieldTC::CString) {
    if (ZuUnlikely(type != ValueTC::String))
      throw ZvNewCf_EXCEPT(badType(node, "string"));
    return node->data<AnyNode::String>().data();
  } else if constexpr (TypeCode == ZtFieldTC::String) {
    if (ZuUnlikely(type != ValueTC::String))
      throw ZvNewCf_EXCEPT(badType(node, "string"));
    return T(node->data<AnyNode::String>());
  } else if constexpr (TypeCode == ZtFieldTC::Bytes) {
    if (ZuUnlikely(type != ValueTC::String))
      throw ZvNewCf_EXCEPT(badType(node, "string"));
    const auto &span = node->data<AnyNode::String>();
    unsigned n = span.length();
    if (ZuUnlikely(!n)) return ZuCmp<T>::null();
    constexpr unsigned Fmt = ZuFieldProp::Cf::GetBytesFmt<Props>{};
    if constexpr (Fmt == ZvNewCf::Raw) {
      return T(span);
    } else {
      using Codec = ZuIf<
	Fmt == ZvNewCf::Base64, ZuBase64,
	ZuIf<Fmt == ZvNewCf::Base64URL, ZuBase64URL,
	  ZuIf<Fmt == ZvNewCf::Base32, ZuBase32, ZuHex>>>;
      T out;
      out.length(Codec::declen(n));
      unsigned bytes = Codec::decode(out.span(), ZuBSpan{span});
      if (ZuUnlikely(n > Codec::enclen(bytes)))
	throw ZvNewCf_EXCEPT(badValue(node, "encoded bytes", span));
      out.length(bytes);
      return out;
    }
  } else if constexpr (TypeCode == ZtFieldTC::Bool) {
    if (ZuUnlikely(type != ValueTC::String))
      throw ZvNewCf_EXCEPT(badType(node, "boolean"));
    const auto &span = node->data<AnyNode::String>();
    try {
      return ZtScanBool<true>(span);
    } catch (const ZtBadBool &) {
      throw ZvNewCf_EXCEPT(badBool(node, {}, span));
    }
  } else if constexpr (
      TypeCode == ZtFieldTC::Int8 ||
      TypeCode == ZtFieldTC::Int16 ||
      TypeCode == ZtFieldTC::Int32 ||
      TypeCode == ZtFieldTC::Int64 ||
      TypeCode == ZtFieldTC::Int128 ||
      TypeCode == ZtFieldTC::UInt8 ||
      TypeCode == ZtFieldTC::UInt16 ||
      TypeCode == ZtFieldTC::UInt32 ||
      TypeCode == ZtFieldTC::UInt64 ||
      TypeCode == ZtFieldTC::UInt128) {
    if (ZuUnlikely(type != ValueTC::String))
      throw ZvNewCf_EXCEPT(badType(node, "integer"));
    const auto &span = node->data<AnyNode::String>();
    using Fmt = ZuFieldProp::Cf::GetNumberFmt<Props>;
    using B = ZuIf<ZuIsBoxed<T>{}, T, ZuBox<ZtFieldTC::Type<TypeCode>>>;
    B v;
    if constexpr (ZuFieldProp::HasEnum<Props>{}) {
      using Map = ZuFieldProp::GetEnum<Props>;
      auto i = Map::s2v(span);
      if (ZuUnlikely(i < 0))
	throw ZvNewCf_EXCEPT(badEnum<Map>(node, {}, span));
      v = B{i};
    } else if constexpr (ZuFieldProp::HasFlags<Props>{}) {
      using Map = ZuFieldProp::GetFlags<Props>;
      using Scan = typename Map::Scan;
      auto r = Scan::eov(span, Fmt::Fmt::FlagsDelim());
      if (ZuUnlikely(r.template p<0>() < 0 ||
          unsigned(r.template p<0>()) != span.length()))
	throw ZvNewCf_EXCEPT(badEnum<Map>(node, {}, span));
      v = B{r.template p<1>().val()};
    } else {
      auto r = [&]() {
	if constexpr (ZuTypeIn<ZuFieldProp::Hex, Props>{})
	  return B::template eov<ZuFmt::Hex<false, typename Fmt::Fmt>>(span);
	else
	  return B::eov(span);
      }();
      if (ZuUnlikely(r.template p<0>() < 0 ||
          unsigned(r.template p<0>()) != span.length()))
	throw ZvNewCf_EXCEPT(badValue(node, "integer", span));
      v = r.template p<1>();
    }
    if constexpr (ZuFieldProp::HasRange<Props>{}) {
      auto v_ = v;
      if (ZuUnlikely(!ZtFieldLimit<Props>(v))) {
	using Range = ZuFieldProp::GetRange<Props>;
	throw ZvNewCf_EXCEPT(badRange(node, {}, Range::minimum(), Range::maximum(), v_));
      }
    }
    if constexpr (ZuIsBoxed<T>{})
      return v;
    else
      return T(v.val());
  } else if constexpr (
      TypeCode == ZtFieldTC::Float ||
      TypeCode == ZtFieldTC::Fixed ||
      TypeCode == ZtFieldTC::Decimal) {
    if (ZuUnlikely(type != ValueTC::String))
      throw ZvNewCf_EXCEPT(badType(node, "number"));
    ZuCSpan span = node->data<AnyNode::String>();
    if constexpr (
	TypeCode == ZtFieldTC::Decimal ||
	TypeCode == ZtFieldTC::Fixed) {
      auto r = eov_Decimal(span);
      if (ZuUnlikely(r.template p<0>() < 0 ||
          unsigned(r.template p<0>()) != span.length()))
	throw ZvNewCf_EXCEPT(badValue(node, "decimal", span));
      auto d = r.template p<1>();
      if (ZuUnlikely(!*d))
	throw ZvNewCf_EXCEPT(badValue(node, "decimal", span));
      if constexpr (ZuFieldProp::HasRange<Props>{}) {
	auto d_ = d;
	if (ZuUnlikely(!ZtFieldLimit<Props>(d))) {
	  using Range = ZuFieldProp::GetRange<Props>;
	  throw ZvNewCf_EXCEPT(badRange(node, {}, Range::minimum(), Range::maximum(), d_));
	}
      }
      if constexpr (TypeCode == ZtFieldTC::Decimal)
	return d;
      else {
	if constexpr (ZuFieldProp::HasNDP<Props>{})
	  return ZuFixed{d, ZuFieldProp::GetNDP<Props>{}};
	else
	  return ZuFixed{d};
      }
    } else {
      auto d = eov_Float(span);
      if (ZuUnlikely(d.p<0>() < 0 || unsigned(d.p<0>()) != span.length()))
	throw ZvNewCf_EXCEPT(badValue(node, "floating point number", span));
      auto v = d.p<1>();
      if constexpr (ZuFieldProp::HasRange<Props>{}) {
	auto v_ = v;
	if (ZuUnlikely(!ZtFieldLimit<Props>(v))) {
	  using Range = ZuFieldProp::GetRange<Props>;
	  throw ZvNewCf_EXCEPT(badRange(node, {}, Range::minimum(), Range::maximum(), v_));
	}
      }
      return v;
    }
  } else if constexpr (
      TypeCode == ZtFieldTC::Time ||
      TypeCode == ZtFieldTC::DateTime) {
    using Fmt = ZuFieldProp::Cf::GetTimeFmt<Props>;
    if (ZuUnlikely(type != ValueTC::String))
      throw ZvNewCf_EXCEPT(badType(node, "date/time"));
    ZuCSpan span = node->data<AnyNode::String>();
    if constexpr (Fmt::Fmt == ZvNewCf::Unix) {
      auto d = eov_Decimal(span);
      if (ZuUnlikely(d.p<0>() < 0 || unsigned(d.p<0>()) != span.length()))
	throw ZvNewCf_EXCEPT(badValue(node, "date/time", span));
      auto &v = d.p<1>();
      if (ZuUnlikely(!*v)) throw ZvNewCf_EXCEPT(badValue(node, "date/time", span));
      if constexpr (Fmt::Unit == ZvNewCf::MSec) {
	v.value /= 1000;
      } else if constexpr (Fmt::Unit == ZvNewCf::USec) {
	v.value /= 1000000;
      } else if constexpr (Fmt::Unit == ZvNewCf::NSec) {
	v.value /= 1000000000;
      }
      if constexpr (ZuIs_<T, ZuTime>{})
	return ZuTime{v};
      else
	return ZuDateTime{ZuTime{v}};
    } else if constexpr (Fmt::Fmt == ZvNewCf::CSV) {
      auto &fmt = ZmTLS<ZuDateTimeScan::CSV, (int Props::*){}>();
      ZuDateTime v;
      auto n = v.scan(fmt, span);
      if (ZuUnlikely(n < 0 || unsigned(n) != span.length()))
	throw ZvNewCf_EXCEPT(badValue(node, "date/time", span));
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    } else if constexpr (Fmt::Fmt == ZvNewCf::FIX) {
      auto &fmt = ZmTLS<ZuDateTimeScan::FIX, (int Props::*){}>();
      ZuDateTime v;
      auto n = v.scan(fmt, span);
      if (ZuUnlikely(n < 0 || unsigned(n) != span.length()))
	throw ZvNewCf_EXCEPT(badValue(node, "date/time", span));
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    } else if constexpr (Fmt::Fmt == ZvNewCf::ISO) {
      auto &fmt = ZmTLS<ZuDateTimeScan::ISO, (int Props::*){}>();
      ZuDateTime v;
      auto n = v.scan(fmt, span);
      if (ZuUnlikely(n < 0 || unsigned(n) != span.length()))
	throw ZvNewCf_EXCEPT(badValue(node, "date/time", span));
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    }
  } else if constexpr (TypeCode == ZtFieldTC::UDT) {
    return handler_<T, Facet>(node).ctor();
  }
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline auto loadValue(const AnyNode *node)
{
  if constexpr (!ZtFieldTC::IsVec<TypeCode>{}) {
    return loadValue_<Facet, Filter, TypeCode, Props, T>(node);
  } else {
    enum { ElemCode = ZtFieldTC::Elem<TypeCode>{} };
    if constexpr (ElemCode == ZtFieldTC::Bytes) {
      T out;
      if (ZuUnlikely(!node->has<AnyNode::Array>()))
	throw ZvNewCf_EXCEPT(badType(node, "array"));
      using Elem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
      const auto &nodes = node->data<AnyNode::Array>();
      for (unsigned i = 0, n = nodes.length(); i < n; i++)
	new (out.push()) Elem(
	  loadValue_<Facet, Filter, ElemCode, Props, Elem>(nodes[i]));
      return out;
    } else {
      using Actual = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
      using Elem = ZuIf<
	ElemCode >= ZtFieldTC::Int8 && ElemCode <= ZtFieldTC::UInt128 &&
	bool(ZuIsBoxed<Actual>{}), Actual, ZtFieldTC::Type<ElemCode>>;
      using LoadVec_ = LoadVec<Facet, Filter, ElemCode, Props, Elem>;
      if (ZuUnlikely(!node->has<AnyNode::Array>()))
	throw ZvNewCf_EXCEPT(badType(node, "array"));
      return LoadVec_(node->data<AnyNode::Array>());
    }
  }
}

template <
  typename Facet = ZuFacet::Cf,
  template <typename> class Filter = ZtFieldFilter::Save,
  typename S, typename O>
inline S &save(S &s, const O &v) {
  As<O>::template Handler<O, Facet>::template save<Filter>(s, v);
  return s;
}
template <
  typename Facet = ZuFacet::Cf,
  typename S, typename O>
ZuInline S &saveUpd(S &s, const O &v) {
  return save<Facet, ZtFieldFilter::Upd>(s, v);
}
template <
  typename Facet = ZuFacet::Cf,
  typename S, typename O>
ZuInline S &saveDel(S &s, const O &v) {
  return save<Facet, ZtFieldFilter::Del>(s, v);
}

template <typename O, typename Facet = ZuFacet::Cf>
auto handler(const AnyNode *node) {
  return handler_<O, Facet>(node);
}

} // ZvNewCf

#endif /* ZvNewCf_HH */
