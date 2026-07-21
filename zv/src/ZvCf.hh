//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// application configuration
// - ZvCf is intended for cold path loading/saving of
//   configuration data from files, command lines, environments and
//   other such origins, as typically performed at startup and
//   during an occasional live reconfiguration
// - it makes extensive use of pcre regular expressions to parse
//   source data, and general-purpose dynamic memory allocation to
//   store in-memory key/value trees
// - use ZtJSON, ZtASN1, etc. for latency-sensitive use cases

#ifndef ZvCf_HH
#define ZvCf_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#ifndef _MSC_VER
#include <unistd.h>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuICmp.hh>
#include <zlib/ZuBase64.hh>

#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmBackTrace.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtScanBool.hh>
#include <zlib/ZtStruct.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiFile.hh>

#define ZvCfMaxFileSize	(1<<20)	// 1Mb

namespace ZvCf_ {

class Cf;

// these are all on-heap temporaries returned by ZvCf
// - ZvCf as a whole is presumed to be latency-insensitive
ZuDerive(String, ZtString<ZtStringHeapID<"ZvCf">>);
ZuDerive(StringVec, (ZtArray<String, ZtArrayHeapID<"ZvCf">>));
ZuDerive(Bytes, (ZtArray<uint8_t, ZtArrayHeapID<"ZvCf">>));
ZuDerive(CfVec, (ZtArray<ZmRef<Cf>, ZtArrayHeapID<"ZvCf">>));

String fullKey(const Cf *cf, String key);

} // ZvCf_

// various errors thrown by ZvCf, all are instances of ZeException
namespace ZvCfError {

using Cf = ZvCf_::Cf;
using String = ZvCf_::String;

inline auto required(const Cf *cf, ZuCSpan key) {
  return ZeEXCEPT(Error, "ZvCf", ([
    key = ZeString{fullKey(cf, key)}, bt = ZmBackTrace{1}
  ](auto &s) {
    s << '"' << key << "\" missing at:\n" << bt;
  }));
}

inline auto badBool(const Cf *cf, ZuCSpan key, ZuCSpan value) {
  return ZeEXCEPT(Error, "ZvCf", ([
    key = ZeString{fullKey(cf, key)}, value = ZeString{value}
  ](auto &s) {
    s << '"' << key << "\": invalid boolean \"" << value << '"';
  }));
}

template <typename T>
inline auto badRange(const Cf *cf, ZuCSpan key, T minimum, T maximum, T value) {
  return ZeEXCEPT(Error, "ZvCf", ([
    key = ZeString{fullKey(cf, key)}, minimum, maximum, value
  ](auto &s) {
    s << '"' << key << "\" out of range " <<
      "min(" << minimum << ") <= " << value <<
      " <= max(" << maximum << ")";
  }));
}

template <typename Map>
inline auto badEnum(const Cf *cf, ZuCSpan key, ZuCSpan value) {
  return ZeEXCEPT(Error, "ZvCf", ([
    key = ZeString{fullKey(cf, key)}, value = ZeString{value}
  ](auto &s) {
    s << '"' << key << "\" did not match { ";
    bool first = true;
    Map::all([&s, &first](ZuCSpan key, auto v) {
      if (ZuLikely(!first)) s << ", ";
      first = false;
      s << key << " = " << v;
    });
    s << " }";
  }));
}

inline auto badSyntax(unsigned line, char ch, ZuCSpan fileName) {
  return ZeEXCEPT(Error, "ZvCf", ([
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
  }));
}

template <ZuString Op>
inline auto fileError(ZuCSpan fileName, ZeError e) {
  return ZeEXCEPT(Error, "ZvCf", ([
    fileName = ZeString{fileName}, e
  ](auto &s) {
    s << Op.cspan() << "(\"" << fileName << "\"): " << e;
  }));
}

inline auto file2Big(ZuCSpan fileName) {
  return ZeEXCEPT(Error, "ZvCf", ([
    fileName = ZeString{fileName}
  ](auto &s) {
    s << '"' << fileName << "\": file too big";
  }));
}

inline auto badDefine(ZuCSpan define, ZuCSpan fileName) {
  return ZeEXCEPT(Error, "ZvCf", ([
    define = ZeString{define}, fileName = ZeString{fileName}
  ](auto &s) {
    if (fileName) s << '"' << fileName << "\": ";
    s << "bad %define \"" << define << '"';
  }));
}

} // ZvCfError

namespace ZvCf_ {

using namespace ZvCfError;

template <typename T, bool = ZuTraits<T>::IsPrimitive> struct Scan_;
template <typename T_>
struct Scan_<T_, true> { using T = ZuBox<T_>; };
template <typename T_, typename NTP_>
struct Scan_<ZuBox<T_, NTP_>, false> { using T = ZuBox<T_, NTP_>; };
template <> struct Scan_<ZuFixed, false> { using T = ZuFixed; };
template <> struct Scan_<ZuDecimal, false> { using T = ZuDecimal; };
template <typename T> using Scan = typename Scan_<T>::T;

// scan bool
template <bool Required_ = false>
inline bool scanBool(
    const Cf *cf, ZuCSpan key, ZuCSpan value, bool deflt = false)
{
  if (!value) {
    if constexpr (Required_) throw required(cf, key);
    return deflt;
  }
  try {
    return ZtScanBool<true>(value);
  } catch (...) {
    throw badBool(cf, key, value);
  }
}

// scan generic scalar
template <typename T, bool Required_ = false>
inline T scanScalar(
    const Cf *cf, ZuCSpan key, ZuCSpan value,
    T minimum, T maximum, T deflt = ZuCmp<T>::null())
{
  if (!value) {
    if constexpr (Required_) throw required(cf, key);
    return deflt;
  }
  Scan<T> v{value};
  if (v < minimum || v > maximum)
    throw badRange(cf, key, minimum, maximum, v.val());
  return v;
}
// scanScalar() shorthand forwarding functions
template <bool Required_ = false, typename ...Args>
inline auto scanInt(Args &&...args) {
  return scanScalar<int, Required_>(ZuFwd<Args>(args)...);
}
template <bool Required_ = false, typename ...Args>
inline auto scanInt64(Args &&...args) {
  return scanScalar<int64_t, Required_>(ZuFwd<Args>(args)...);
}
template <bool Required_ = false, typename ...Args>
inline auto scanDbl(Args &&...args) {
  return scanScalar<double, Required_>(ZuFwd<Args>(args)...);
}

// scan enum
template <typename Map, typename T, bool Required_ = false>
inline T scanEnum(
  const Cf *cf, ZuCSpan key, ZuCSpan value, T deflt = -1)
{
  if (!value) {
    if constexpr (Required_) throw required(cf, key);
    return deflt;
  }
  T v = Map::s2v(value);
  if (ZuLikely(v >= 0)) return v;
  throw badEnum<Map>(cf, key, value);
}

// scan flags
template <typename Map, typename T, bool Required_ = false>
inline T scanFlags(
  const Cf *cf, ZuCSpan key, ZuCSpan value, T deflt = 0)
{
  if (!value) {
    if constexpr (Required_) throw required(cf, key);
    return deflt;
  }
  T v = typename Map::Scan{value};
  if (ZuLikely(v)) return v;
  throw badEnum<Map>(cf, key, value);
}

// data in a tree node
using Data = ZuUnion<void, String, StringVec, ZmRef<Cf>, CfVec>;

// main configuration class
class Cf;

// configuration tree node
struct CfNode {
  Cf * const		owner = nullptr;
  const String		key;
  Data			data;

friend Cf;

  CfNode() = delete;
  CfNode(const CfNode &) = delete;
  CfNode &operator =(const CfNode &) = delete;
  CfNode(CfNode &&) = delete;
  CfNode &operator =(CfNode &&) = delete;

protected:
  template <typename Key>
  CfNode(Cf *owner_, Key &&key_) : owner{owner_}, key{ZuFwd<Key>(key_)} { }

public:
  static const String &KeyAxor(const CfNode &node) { return node.key; }

  void null() { data = {}; }

  auto type() const { return data.type(); }

  // generic set()
  template <typename T, typename P>
  void set_(P &&v) { data.p<T>(ZuFwd<P>(v)); }
  // set() shorthand forwarding functions for String and ZmRef<Cf>
  template <typename P> void set(P &&v) { set_<String>(ZuFwd<P>(v)); }
  template <typename P> void setCf(P &&v) { set_<ZmRef<Cf>>(ZuFwd<P>(v)); }

  // generic get()
  template <typename T, bool Required_ = false>
  const T &get_() const { // optionally required, no specified default value
    if (!data.is<T>()) {
      if constexpr (Required_) throw required(owner, key);
      return ZuNullRef<T>();
    }
    return data.p<T>();
  }
  template <typename T>
  T get_(T deflt) const { // not required, specified default value
    if (!data.is<T>()) return deflt;
    return data.p<T>();
  }
  // generic assure() - sets to a specified default value if unset
  template <typename T, typename L>
  const T &assure_(L &&l) { // not required, set default if unset
    if (!data.is<T>()) data.p<T>(ZuFwd<L>(l)());
    return data.p<T>();
  }

  // get/assure String
  template <bool Required_ = false>
  const String &get() const { return get_<String, Required_>(); }
  String get(String deflt) const { return get_<String>(ZuMv(deflt)); }
  template <typename L>
  const String &assure(L &&l) { return assure_<String>(ZuFwd<L>(l)); }
  // get/assure StringVec
  template <bool Required_ = false>
  const StringVec &getStringVec() const { return get_<StringVec, Required_>(); }
  template <typename L>
  const StringVec &assureStringVec(L &&l) {
    return assure_<StringVec>(ZuFwd<L>(l));
  }
  // get/assure ZmRef<Cf>
  template <bool Required_ = false>
  const ZmRef<Cf> &getCf() const { return get_<ZmRef<Cf>, Required_>(); }
  template <typename L>
  const ZmRef<Cf> &assureCf(L &&l) { return assure_<ZmRef<Cf>>(ZuFwd<L>(l)); }
  // get/assure CfVec
  template <bool Required_ = false>
  const CfVec &getCfVec() const { return get_<CfVec, Required_>(); }
  template <typename L>
  const CfVec &assureCfVec(L &&l) { return assure_<CfVec>(ZuFwd<L>(l)); }

  // generic set/get/assure array element
  template <typename T, typename P>
  void setElem(unsigned i, P &&v) {
    using Elem = typename T::T;
    if (!data.is<T>()) new (data.new_<T>()) T();
    new (data.p<T>().set(i)) Elem{ZuFwd<P>(v)};
  }
  template <typename T, bool Required_ = false>
  const typename T::T &getElem(unsigned i) const {
    if (!data.is<T>()) {
      if constexpr (Required_) throw required(owner, key);
      return ZuNullRef<typename T::T>();
    }
    const auto &elems = data.p<T>();
    if (i >= elems.length()) return ZuNullRef<typename T::T>();
    return elems.get(i);
  }
  template <typename T>
  typename T::T getElem(unsigned i, typename T::T deflt) const {
    if (!data.is<T>()) return deflt;
    const auto &elems = data.p<T>();
    if (i >= elems.length()) return deflt;
    return elems.get(i);
  }
  template <typename T, typename L>
  const typename T::T &assureElem(unsigned i, L &&l) {
    if (!data.is<T>()) new (data.new_<T>()) T();
    if (i >= data.p<T>().length()) data.p<T>().set(i, ZuFwd<L>(l)());
    return data.p<T>().get(i);
  }

  // get/assure bool
  template <bool Required_ = false>
  bool getBool() const {
    return scanBool<Required_>(owner, key, get<Required_>());
  }
  bool getBool(bool deflt) const {
    return scanBool(owner, key, get(), deflt);
  }
  bool assureBool(bool deflt) {
    return scanBool(
	owner, key, assure([deflt]() { return deflt ? "1" : "0"; }), deflt);
  }

  // generic get/assure scalar
  template <typename T, bool Required_ = false>
  T getScalar(T minimum, T maximum) const {
    return scanScalar<T, Required_>(
	owner, key, get<Required_>(),
	minimum, maximum);
  }
  template <typename T>
  T getScalar(T minimum, T maximum, T deflt) const {
    return scanScalar<T>(owner, key, get(), minimum, maximum, deflt);
  }
  template <typename T>
  T assureScalar(T minimum, T maximum, T deflt) {
    return scanScalar<T>(
	owner, key,
	assure([deflt = ZuMv(deflt)]() { return String{} << deflt; }),
	minimum, maximum, deflt);
  }

  // getScalar/assureScalar shorthand forwarding functions for int
  template <bool Required_ = false>
  int getInt(int minimum, int maximum) const {
    return getScalar<int, Required_>(minimum, maximum);
  }
  int getInt(int minimum, int maximum, int deflt) const {
    return getScalar<int>(minimum, maximum, deflt);
  }
  int assureInt(int minimum, int maximum, int deflt) {
    return assureScalar<int>(minimum, maximum, deflt);
  }

  // getScalar/assureScalar shorthand forwarding functions for int64_t
  template <bool Required_ = false>
  int64_t getInt64(int64_t minimum, int64_t maximum) const {
    return getScalar<int64_t, Required_>(minimum, maximum);
  }
  int64_t getInt64(int64_t minimum, int64_t maximum, int64_t deflt) const {
    return getScalar<int64_t>(minimum, maximum, deflt);
  }
  int64_t assureInt64(int64_t minimum, int64_t maximum, int64_t deflt) {
    return assureScalar<int64_t>(minimum, maximum, deflt);
  }

  // getScalar/assureScalar shorthand forwarding functions for double
  template <bool Required_ = false>
  double getDbl(double minimum, double maximum) const {
    return getScalar<double, Required_>(minimum, maximum);
  }
  double getDbl(double minimum, double maximum, double deflt) const {
    return getScalar<double>(minimum, maximum, deflt);
  }
  double assureDbl(double minimum, double maximum, double deflt) {
    return assureScalar<double>(minimum, maximum, deflt);
  }

  // get/assure enum
  template <typename Map, typename T, bool Required_ = false>
  T getEnum() const {
    return scanEnum<Map, T, Required_>(owner, key, get<Required_>(), -1);
  }
  template <typename Map, typename T>
  T getEnum(T deflt) const {
    return scanEnum<Map, T>(owner, key, get(), deflt);
  }
  template <typename Map, typename T>
  T assureEnum(int deflt) {
    return scanEnum<Map, T>(
	owner, key, assure([deflt]() { return Map::v2s(deflt); }), deflt);
  }

  // get/assure flags
  template <typename Map, typename T, bool Required_ = false>
  T getFlags() const {
    return scanFlags<Map, T, Required_>(
	owner, key, get<Required_>(), 0);
  }
  template <typename Map, typename T>
  T getFlags(T deflt) const {
    return scanFlags<Map, T>(owner, key, get(), deflt);
  }
  template <typename Map, typename T>
  T assureFlags(T deflt) {
    using Print = typename Map::Print;
    return scanFlags<Map, T>(
	owner, key,
	assure([deflt]() { return String{} << Print{deflt}; }), deflt);
  }
};

// ZtStruct integration
template <typename O, typename Cf_>
struct Handler_ {
  using Fields = ZuFields<O>;

  template <typename Field>
  using AllFilter = ZuBool<!Field::ReadOnly>;
  using AllFields = ZuTypeGrep<AllFilter, Fields>;

  template <typename Field>
  using UpdateFilter = ZuTypeIn<ZuFieldProp::Mutable, typename Field::Props>;
  using UpdateFields = ZuTypeGrep<UpdateFilter, AllFields>;

  template <typename Field>
  using CtorFilter = ZuFieldProp::HasCtor<typename Field::Props>;
  template <typename Field>
  using CtorIndex = ZuFieldProp::GetCtor<typename Field::Props>;
  using CtorFields = ZuTypeSort<CtorIndex, ZuTypeGrep<CtorFilter, AllFields>>;

  template <typename Field>
  using InitFilter = ZuBool<!ZuFieldProp::HasCtor<typename Field::Props>{}>;
  using InitFields = ZuTypeGrep<InitFilter, AllFields>;

  template <typename ...Fields_>
  struct Ctor {
    template <typename ...Args>
    static O ctor(const Cf_ *cf, Args &&...args) {
      return O(ZuFwd<Args>(args)..., cf->template getField<Fields_>()...);
    }
    template <typename ...Args>
    static void new_(void *o, const Cf_ *cf, Args &&...args) {
      new (o) O(ZuFwd<Args>(args)..., cf->template getField<Fields_>()...);
    }
  };
  template <typename ...Args>
  static O ctor(const Cf_ *cf, Args &&...args) {
    O o = ZuTypeApply<Ctor, CtorFields>::ctor(cf, ZuFwd<Args>(args)...);
    ZuUnroll::all<InitFields>([&o, cf]<typename Field>() {
      Field::set(o, cf->template getField<Field>());
    });
    return o;
  }
  template <typename ...Args>
  static void new_(void *o_, const Cf_ *cf, Args &&...args) {
    ZuTypeApply<Ctor, CtorFields>::new_(o_, cf, ZuFwd<Args>(args)...);
    O &o = *reinterpret_cast<O *>(o_);
    ZuUnroll::all<InitFields>([&o, cf]<typename Field>() {
      Field::set(o, cf->template getField<Field>());
    });
  }

  static void load(O &o, const Cf_ *cf) {
    ZuUnroll::all<AllFields>([&o, cf]<typename Field>() {
      Field::set(o, cf->template getField<Field>());
    });
  }
  static void update(O &o, const Cf_ *cf) {
    ZuUnroll::all<UpdateFields>([&o, cf]<typename Field>() {
      Field::set(o, cf->template getField<Field>());
    });
  }
};
template <typename O, typename Cf_ = Cf>
using Handler = Handler_<ZuStructured<O>, Cf_>;

// main configuration class - contains tree of CfNodes (key + data pairs)
class ZvAPI Cf : public ZuObject {
public:
  Cf() = default;
private:
  Cf(CfNode *node) : m_node{node} { }

public:
  Cf(const Cf &) = delete;
  Cf &operator =(const Cf &) = delete;

  ZuDerive(Defines_,
    (ZmRBTreeKV<String, String, ZmRBTreeUnique<true>>));
  struct Defines : public ZuObject, public Defines_ { };

  void fromString(ZuCSpan in, ZmRef<Defines> defines = new Defines{}) {
    fromString(in, {}, defines);
  }

  template <typename Path>
  void fromFile(const Path &path, ZmRef<Defines> defines = new Defines{}) {
    String in;
    {
      ZiFile file;
      if (file.open(path, ZiFile::ReadOnly | ZiFile::GC, 0) < 0)
	throw fileError<"open">(path, file.error());
      int n = file.size();
      if (n >= ZvCfMaxFileSize) throw file2Big(path);
      in.length(n);
      if (file.read(in.data(), n) < 0)
	throw fileError<"read">(path, file.error());
    }
    String dir = ZiFile::dirname(path);
    if (!defines->find("TOPDIR")) defines->add("TOPDIR", dir);
    defines->del("CURDIR"); defines->add("CURDIR", ZuMv(dir));
    fromString(in, path, defines);
  }

  void print(ZuVStream &s, String &indent) const;

  template <typename S> void print(S &s_) const {
    ZuVStream s{s_};
    String indent;
    print(s, indent);
  }
  void print(ZuVStream &s) const {
    String indent;
    print(s, indent);
  }
  friend ZuPrintFn ZuPrintType(Cf *);

  // toFile() will throw ZeException on I/O error
  void toFile(ZuCSpan fileName) {
    ZiFile file;
    if (file.open(fileName, ZiFile::Create | ZiFile::Truncate, 0777) < 0)
      throw fileError<"open">(fileName, file.error());
    ZmRef<ZiIOBuf> out = new ZiIOBufAlloc<>();
    *out << *this;
    if (file.write(out->data(), out->length) != Zi::OK)
      throw fileError<"write">(fileName, file.error());
  }

private:
  ZuDerive(Tree,
    (ZmRBTree<CfNode,
      ZmRBTreeNode<CfNode,
	ZmRBTreeKey<CfNode::KeyAxor,
	  ZmRBTreeUnique<true,
	    ZmRBTreeHeapID<"ZvCf">>>>>));
  using Node = Tree::Node;

  ZuTuple<Cf *, String> getScope(ZuCSpan fullKey) const;

public:
  template <bool Required_ = false>
  const CfNode *getNode(ZuCSpan fullKey) const {
    auto [this_, key] = getScope(fullKey);
    if (!this_) {
      if constexpr (Required_) throw required(this, fullKey);
      return nullptr;
    }
    Node *node = this_->m_tree.find(key);
    if (!node)
      if constexpr (Required_) throw required(this, fullKey);
    return node;
  }
private:
  CfNode *mkNode(ZuCSpan fullKey);

public:
  // check if key exists
  bool exists(ZuCSpan fullKey) const { return getNode(fullKey); }

  // set/get/assure String
  void set(ZuCSpan key, String value);
  template <bool Required_ = false>
  const String &get(ZuCSpan key) const {
    if (auto node = getNode<Required_>(key))
      return node->template get<Required_>();
    if constexpr (Required_) throw required(this, key);
    return ZuNullRef<String>();
  }
  String get(ZuCSpan key, String deflt) const {
    if (auto node = getNode(key)) return node->get(deflt);
    return deflt;
  }
  template <typename L>
  const String &assure(ZuCSpan key, L &&l) {
    return mkNode(key)->assure(ZuFwd<L>(l));
  }

  // set/get/assure StringVec
  void setStringVec(ZuCSpan key, StringVec value);
  template <bool Required_ = false>
  const StringVec &getStringVec(ZuCSpan key) const {
    if (auto node = getNode<Required_>(key))
      return node->template getStringVec<Required_>();
    if constexpr (Required_) throw required(this, key);
    return ZuNullRef<StringVec>();
  }
  template <typename L>
  const StringVec &assureStringVec(ZuCSpan key, L &&l) {
    return mkNode(key)->assureStringVec(ZuFwd<L>(l));
  }

  // set/get/assure ZmRef<Cf>
  ZmRef<Cf> mkCf(ZuCSpan key);
  void setCf(ZuCSpan key, ZmRef<Cf> cf);
  template <bool Required_ = false>
  const ZmRef<Cf> &getCf(ZuCSpan key) const {
    if (auto node = getNode<Required_>(key))
      return node->template getCf<Required_>();
    if constexpr (Required_) throw required(this, key);
    return ZuNullRef<ZmRef<Cf>>();
  }
  template <typename L>
  const ZmRef<Cf> &assureCf(ZuCSpan key, L &&l) {
    return mkNode(key)->assureCf(ZuFwd<L>(l));
  }

  // set/get/assure CfVec
  void setCfVec(ZuCSpan key, CfVec value);
  template <bool Required_ = false>
  const CfVec &getCfVec(ZuCSpan key) const {
    if (auto node = getNode<Required_>(key))
      return node->template getCfVec<Required_>();
    if constexpr (Required_) throw required(this, key);
    return ZuNullRef<CfVec>();
  }
  template <typename L>
  const CfVec &assureCfVec(ZuCSpan key, L &&l) {
    return mkNode(key)->assureCfVec(ZuFwd<L>(l));
  }

  // unset node
  void unset(ZuCSpan key);

  // iterate over nodes
  template <typename L>
  void all(L &&l) {
    auto i = m_tree.iter();
    while (auto node = i()) ZuFwd<L>(l)(node);
  }

  // clean tree
  void clean();

  // merge tree
  void merge(const Cf *cf);

  // get/assure bool
  template <bool Required_ = false>
  bool getBool(ZuCSpan key) const {
    if (auto node = getNode<Required_>(key))
      return node->template getBool<Required_>();
    if constexpr (Required_) throw required(this, key);
    return false;
  }
  bool getBool(ZuCSpan key, bool deflt) const {
    if (auto node = getNode(key))
      return node->getBool(deflt);
    return deflt;
  }
  bool assureBool(ZuCSpan key, bool deflt) {
    return mkNode(key)->assureBool(deflt);
  }

  // generic get/assure scalar
  template <typename T, bool Required_ = false>
  T getScalar(ZuCSpan key, T minimum, T maximum) const {
    if (auto node = getNode<Required_>(key))
      return node->template getScalar<T, Required_>(minimum, maximum);
    if constexpr (Required_) throw required(this, key);
    return 0;
  }
  template <typename T>
  T getScalar(ZuCSpan key, T minimum, T maximum, T deflt) const {
    if (auto node = getNode(key))
      return node->template getScalar<T>(minimum, maximum, deflt);
    return deflt;
  }
  template <typename T>
  T assureScalar(ZuCSpan key, T minimum, T maximum, T deflt) {
    return mkNode(key)->template assureScalar<T>(minimum, maximum, deflt);
  }

  // get/assure shorthand forwarding functions for int
  template <bool Required_ = false>
  int getInt(ZuCSpan key, int minimum, int maximum) const {
    return getScalar<int, Required_>(key, minimum, maximum);
  }
  int getInt(ZuCSpan key, int minimum, int maximum, int deflt) const {
    return getScalar<int>(key, minimum, maximum, deflt);
  }
  int assureInt(ZuCSpan key, int minimum, int maximum, int deflt) {
    return assureScalar<int>(key, minimum, maximum, deflt);
  }

  // get/assure shorthand forwarding functions for int64_t
  template <bool Required_ = false>
  int64_t getInt64(ZuCSpan key, int64_t minimum, int64_t maximum) const {
    return getScalar<int64_t, Required_>(key, minimum, maximum);
  }
  int64_t getInt64(
      ZuCSpan key, int64_t minimum, int64_t maximum, int64_t deflt) const {
    return getScalar<int64_t>(key, minimum, maximum, deflt);
  }
  int64_t assureInt64(
      ZuCSpan key, int64_t minimum, int64_t maximum, int64_t deflt) {
    return assureScalar<int64_t>(key, minimum, maximum, deflt);
  }

  // get/assure shorthand forwarding functions for double
  template <bool Required_ = false>
  double getDbl(ZuCSpan key, double minimum, double maximum) const {
    return getScalar<double, Required_>(key, minimum, maximum);
  }
  double getDbl(
      ZuCSpan key, double minimum, double maximum, double deflt) const {
    return getScalar<double>(key, minimum, maximum, deflt);
  }
  double assureDbl(
      ZuCSpan key, double minimum, double maximum, double deflt) {
    return assureScalar<double>(key, minimum, maximum, deflt);
  }

  // get/assure for enum
  template <typename Map, typename T, bool Required_ = false>
  T getEnum(ZuCSpan key) const {
    if (auto node = getNode<Required_>(key))
      return node->template getEnum<Map, T, Required_>();
    if constexpr (Required_) throw required(this, key);
    return -1;
  }
  template <typename Map, typename T>
  T getEnum(ZuCSpan key, int deflt) const {
    if (auto node = getNode(key))
      return node->template getEnum<Map, T>(deflt);
    return deflt;
  }
  template <typename Map, typename T>
  T assureEnum(ZuCSpan key, int deflt) {
    return mkNode(key)->template assureEnum<Map, T>(deflt);
  }

  // generic get/assure for flags
  template <typename Map, typename T, bool Required_ = false>
  T getFlags(ZuCSpan key) const {
    if (auto node = getNode<Required_>(key))
      return node->template getFlags<Map, T, Required_>();
    if constexpr (Required_) throw required(this, key);
    return 0;
  }
  template <typename Map, typename T>
  T getFlags(ZuCSpan key, T deflt) const {
    if (auto node = getNode(key))
      return node->template getFlags<Map, T>(deflt);
    return deflt;
  }
  template <typename Map, typename T>
  T assureFlags(ZuCSpan key, T deflt) {
    return mkNode(key)->template assureFlags<Map, T>(deflt);
  }

  // ZtStruct integration - get individual field
  template <typename Field>
  ZuIfT<Field::Type::Code == ZtFieldTC::CString, typename Field::T>
  getField() {
    return get<ZuTypeIn<ZuFieldProp::Required, typename Field::Props>{}>(
	Field::id(), Field::deflt()).data();
  }
  template <typename Field>
  ZuIfT<Field::Type::Code == ZtFieldTC::String, typename Field::T>
  getField() {
    return get<ZuTypeIn<ZuFieldProp::Required, typename Field::Props>{}>(
	Field::id(), Field::deflt());
  }
  template <typename Field>
  ZuIfT<Field::Type::Code == ZtFieldTC::Bytes, typename Field::T>
  getField() {
    auto s = get<ZuTypeIn<ZuFieldProp::Required, typename Field::Props>{}>(
	Field::id(), Field::deflt());
    auto n = ZuBase64::declen(s.length());
    Bytes buf(n);
    buf.length(ZuBase64::decode(buf, ZuBSpan{s}));
    return buf;
  }
  template <typename Field>
  ZuIfT<Field::Type::Code == ZtFieldTC::UDT ||
	Field::Type::Code == ZtFieldTC::Time ||
	Field::Type::Code == ZtFieldTC::DateTime, typename Field::T>
  getField() {
    using T = typename Field::T;
    auto s = get<ZuTypeIn<ZuFieldProp::Required, typename Field::Props>{}>(
	Field::id(), "");
    if (ZuUnlikely(!s)) return Field::deflt();
    return T(s);
  }
  template <typename Field>
  ZuIfT<Field::Type::Code == ZtFieldTC::Bool, typename Field::T>
  getField() {
    return getBool<ZuTypeIn<ZuFieldProp::Required, typename Field::Props>{}>(
	Field::id(), Field::deflt());
  }
  template <typename Field>
  ZuIfT<Field::Type::Code == ZtFieldTC::Int8 ||
	Field::Type::Code == ZtFieldTC::UInt8 ||
	Field::Type::Code == ZtFieldTC::Int16 ||
	Field::Type::Code == ZtFieldTC::UInt16 ||
	Field::Type::Code == ZtFieldTC::Int32 ||
	Field::Type::Code == ZtFieldTC::UInt32 ||
	Field::Type::Code == ZtFieldTC::Int64 ||
	Field::Type::Code == ZtFieldTC::UInt64 ||
	Field::Type::Code == ZtFieldTC::Int128 ||
	Field::Type::Code == ZtFieldTC::UInt128, typename Field::T>
  getField() {
    using Props = typename Field::Props;
    if constexpr (ZuFieldProp::HasEnum<Props>{}) {
      using Map = ZuFieldProp::GetEnum<Props>;
      return getEnum<
	Map, ZuTypeIn<ZuFieldProp::Required, Props>{}>(
	  Field::id(), Field::deflt());
    } else if constexpr (ZuFieldProp::HasFlags<Props>{}) {
      using Map = ZuFieldProp::GetFlags<Props>;
      using T = typename Field::T;
      return getFlags<
	Map, T, ZuTypeIn<ZuFieldProp::Required, Props>{}>(
	  Field::id(), Field::deflt());
    } else {
      using T = typename Field::T;
      if constexpr (ZuTypeIn<ZuFieldProp::Required, Props>{})
	return getScalar<T, true>(
	    Field::id(), Field::minimum(), Field::maximum());
      else
	return getScalar<T>(
	    Field::id(), Field::minimum(), Field::maximum(), Field::deflt());
    }
  }
  template <typename Field>
  ZuIfT<Field::Type::Code == ZtFieldTC::Float ||
	Field::Type::Code == ZtFieldTC::Fixed ||
	Field::Type::Code == ZtFieldTC::Decimal, typename Field::T>
  getField() {
    using T = typename Field::T;
    using Props = typename Field::Props;
    if constexpr (ZuTypeIn<ZuFieldProp::Required, Props>{})
      return getScalar<T, true>(
	  Field::id(), Field::minimum(), Field::maximum());
    else
      return getScalar<T>(
	  Field::id(), Field::minimum(), Field::maximum(), Field::deflt());
  }
  template <typename Field>
  ZuIfT<Field::Type::Code == ZtFieldTC::CStringVec, typename Field::T>
  getField() {
    using T = typename Field::T;
    CfNode *node = getNode(Field::id());
    if (!node || !node->data.is<StringVec>()) return {};
    const auto &elems = node->data.p<StringVec>();
    using Elem = typename ZuTraits<T>::Elem;
    return T(ZuVArray<Elem, true>(
      elems, elems.length(),
      [](const void *ptr, unsigned i) {
	const auto &elems = *reinterpret_cast<const StringVec *>(ptr);
	return elems[i].data();
      }));
  }
  template <typename Field>
  ZuIfT<Field::Type::Code == ZtFieldTC::StringVec, typename Field::T>
  getField() {
    using T = typename Field::T;
    CfNode *node = getNode(Field::id());
    if (!node || !node->data.is<StringVec>()) return {};
    const auto &elems = node->data.p<StringVec>();
    using Elem = typename ZuTraits<T>::Elem;
    return T(ZuVArray<Elem, true>(
      elems, elems.length(),
      [](const void *ptr, unsigned i) {
	const auto &elems = *reinterpret_cast<const StringVec *>(ptr);
	return elems[i];
      }));
  }
  template <typename Field>
  ZuIfT<Field::Type::Code == ZtFieldTC::BytesVec, typename Field::T>
  getField() {
    using T = typename Field::T;
    CfNode *node = getNode(Field::id());
    if (!node || !node->data.is<StringVec>()) return {};
    const auto &elems = node->data.p<StringVec>();
    using Elem = typename ZuTraits<T>::Elem;
    return T(ZuVArray<Elem, true>(
      elems, elems.length(),
      [](const void *ptr, unsigned i) {
	const auto &elems = *reinterpret_cast<const StringVec *>(ptr);
	const auto &s = elems[i];
	auto n = ZuBase64::declen(s.length());
	Bytes buf(n);
	buf.length(ZuBase64::decode(buf, ZuBSpan{s}));
	return buf;
      }));
  }
  template <typename Field>
  ZuIfT<Field::Type::Code == ZtFieldTC::Int8Vec ||
	Field::Type::Code == ZtFieldTC::UInt8Vec ||
	Field::Type::Code == ZtFieldTC::Int16Vec ||
	Field::Type::Code == ZtFieldTC::UInt16Vec ||
	Field::Type::Code == ZtFieldTC::Int32Vec ||
	Field::Type::Code == ZtFieldTC::UInt32Vec ||
	Field::Type::Code == ZtFieldTC::Int64Vec ||
	Field::Type::Code == ZtFieldTC::UInt64Vec ||
	Field::Type::Code == ZtFieldTC::Int128Vec ||
	Field::Type::Code == ZtFieldTC::UInt128Vec ||
	Field::Type::Code == ZtFieldTC::FloatVec ||
	Field::Type::Code == ZtFieldTC::FixedVec ||
	Field::Type::Code == ZtFieldTC::DecimalVec, typename Field::T>
  getField() {
    using T = typename Field::T;
    CfNode *node = getNode(Field::id());
    if (!node || !node->data.is<StringVec>()) return {};
    const auto &elems = node->data.p<StringVec>();
    using Elem = typename ZuTraits<T>::Elem;
    return T(ZuVArray<Elem>(
      elems, elems.length(),
      [](const void *ptr, unsigned i) {
	const auto &elems = *reinterpret_cast<const StringVec *>(ptr);
	return Scan<Elem>{elems[i]};
      }));
  }
  template <typename Field>
  ZuIfT<Field::Type::Code == ZtFieldTC::TimeVec ||
	Field::Type::Code == ZtFieldTC::DateTimeVec, typename Field::T>
  getField() {
    using T = typename Field::T;
    CfNode *node = getNode(Field::id());
    if (!node || !node->data.is<StringVec>()) return {};
    const auto &elems = node->data.p<StringVec>();
    using Elem = typename ZuTraits<T>::Elem;
    return T(ZuVArray<Elem>(
      elems, elems.length(),
      [](const void *ptr, unsigned i) {
	const auto &elems = *reinterpret_cast<const StringVec *>(ptr);
	return Elem{elems[i]};
      }));
  }

  // ZtStruct integration - construct structured object
  template <typename O>
  inline O ctor() const { return Handler<O>::ctor(this); }
  template <typename O>
  inline void ctor(void *ptr) const { Handler<O>::ctor(ptr, this); }

  // ZtStruct integration - load structured object
  template <typename O>
  inline void load(O &o) const { Handler<O>::load(o, this); }
  template <typename O>
  inline void update(O &o) const { Handler<O>::update(o, this); }

  // ZtStruct integration - get key
  template <typename O, int KeyID = 0>
  inline auto key() const {
    return ctor<ZuStructKeyT<O, KeyID>>();
  }

  // node count
  unsigned count() const { return m_tree.count_(); }

  // parent node (nullptr if root)
  CfNode *node() const { return m_node; }

private:
  void fromString(ZuCSpan in, ZuCSpan path, ZmRef<Defines> defines);

  template <bool Raw>
  ZuTuple<Cf *, String, int, unsigned>
  getScope_(ZuCSpan in, Cf::Defines *defines = nullptr) const;
  template <bool Raw>
  ZuTuple<Cf *, String, int, unsigned>
  mkScope_(ZuCSpan in, Cf::Defines *defines = nullptr);
  template <bool Raw>
  ZuTuple<Cf *, CfNode *, int, unsigned>
  mkNode_(ZuCSpan in);

  Tree		m_tree;
  CfNode	*m_node;
};

// equivalent of pwd - returns the full key from a nested tree key
inline String fullKey(const Cf *cf, String key) {
  while (auto node = cf->node()) {
    key = String{} << node->CfNode::key << '.' << key;
    if (!(cf = node->owner)) break;
  }
  return key;
}

} // ZvCf_

using ZvCf = ZvCf_::Cf;
using ZvCfNode = ZvCf_::CfNode;
using ZvCfString = ZvCf_::String;
using ZvCfStringVec = ZvCf_::StringVec;

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* ZvCf_HH */
