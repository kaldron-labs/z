//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZfStruct CLI load/save
// - compile-time formatting
// - in-place overwrite decoding of quoting, base64, base32, hex etc. 

// the many and varied ways of flattening structures into CLI args, sigh...

// both arrays and nested objects can also be embedded as JSON

// recap: the top-level is always an object whose fields are ZfStruct-defined;
// individual fields are one of:
// - a primitive type with non-UDT typecode (String, Int*, etc.)
//   - per-field StringFmt/BytesFmt/NumberFmt/TimeFmt control the format
// - a vector type with vector typecode (vector of primitive types)
//   - no recursion is possible because the element type is primitive
//   - per-field StringFmt/BytesFmt/NumberFmt/TimeFmt control the element format
// - a nested UDT type dispatched via the ZfCLI_Fmt() mechanism:
//   - AsString (default for any type without ZfStruct-defined fields)
//     - by default operator << is used for saving and the type is expected
//       to construct itself from a string (ZuCSpan passed to the constructor)
//     - a custom load/save handler can be defined and bound by
//       declaring ZfCLI_StringFmt(T *) in T's namespace (see AsString)
//   - AsObject (default for any type with ZfStruct-defined fields)
//   - AsArray (ZfCLI::AsArray ZfCLI_Fmt(T *) must be declared in T's namespace)
//   - AsJSON (ZfCLI::AsJSON ZfCLI_Fmt(T *) must be declared in T's namespace)
//     - the format is delegated to ZfJSON and any JSON formatting
//       defined for the field is used
//     - the same mapping as for ZfCLI is used (CLI if canonical), so
//       JSON-in-CLI must be configured via the CLI mapping not the
//       canonical JSON mapping

#ifndef ZfCLI_HH
#define ZfCLI_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <math.h>

#include <zlib/ZuDecimal.hh>
#include <zlib/ZuMArray.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>

#include <zlib/ZmRBTree.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZePlatform.hh>
#include <zlib/ZfStruct.hh>
#include <zlib/ZfJSON.hh>

#ifndef ZfCLI_MutableArgv 
// the argv parameter to main() is mutable on modern operating systems and
// C libraries including Linux, Windows and MacOS, despite the modern
// preference for declaring it as const char *const * instead of the
// original K&R char **

// even on hardened platforms where argv[0] is read-only, argv[1..] is
// read-write - and this library will never mutate argv[0] unless a
// string or bytes field is bound to it with base64/32 or hex encoding,
// which in practice is never done

// use -DZfCLI_MutableArgv=0 if argv[N] where N > 0 is actually in
// read-only memory on a particularly stubborn platform
#define ZfCLI_MutableArgv 1
#endif

ZuStructFacet(CLI); // canonical CLI facet, others can be defined

// --- configuration (per-mapping)

// NTP (named template parameters):
//
// ZfCLIConfig(CLI,			// configure canonical CLI
//   ZfCLI::ArrayFmt<ZfCLI::Delimited,	// array format is delimited
//     ZfCLI::Delimiter<','>>>);	// arrays are delimited by ,

namespace ZfCLI { enum { Bare, Delimited }; }

// NTP defaults
struct ZfCLI_DefltConfig {
  enum {
    ArrayFmt = ZfCLI::Bare,
    Delimiter = ','
  };
};

// ZfCLI_ArrayFmt<...> - configure array format
template <unsigned Fmt, typename NTP = ZfCLI_DefltConfig>
struct ZfCLI_ArrayFmt : public NTP {
  enum { ArrayFmt = Fmt };
  ZuAssert(Fmt == ZfCLI::Bare || Fmt == ZfCLI::Delimited);
};

// ZfCLI_Delimiter<...> - configure array element delimiter character
template <char _, typename NTP = ZfCLI_DefltConfig>
struct ZfCLI_Delimiter : public NTP { enum { Delimiter = _ }; };

ZfCLI_DefltConfig ZfCLI_Config(...); // default

namespace ZfCLI {

// built-in size for on-heap argument arrays
constexpr unsigned BuiltinSize = 16;

enum { Base64, Base64URL, Base32, Hex, Escaped };

template <typename Fmt_ = ZtFmt::Default>
struct NumberFmt {
  using Fmt = Fmt_;
};

enum {
  Bool_TRUE_FALSE, Bool_True_False, Bool_true_false, Bool_T_F, Bool_t_f,
  Bool_YES_NO, Bool_Yes_No, Bool_yes_no, Bool_Y_N, Bool_y_n,
  Bool_0_1
};

enum { ISO = 0, FIX, CSV, Unix };
enum { Sec = 0, MSec, USec, NSec };
template <unsigned Fmt_, unsigned Unit_, int NDP_>
struct TimeFmt {
  static constexpr unsigned Fmt = Fmt_;
  static constexpr unsigned Unit = Unit_;
  static constexpr int NDP = NDP_;
};

// resolve configuration (per field mapping)
// - E.g. ZfCLI::Config<Mapping>::ArrayFmt
template <typename Facet>
using Config = decltype(ZfCLI_Config(ZuDeclVal<Facet *>()));

}

// ZfCLIConfig(Facet, Config)
// - configure CLI for specific facet
// - must be used in top-level namespace
#define ZfCLIConfig(Facet, Config) \
  namespace ZfCLI { \
    ZuPP_Strip(Config) ZfCLI_Config(ZuFacet::Facet *); \
  }

namespace ZfCLI {

ZuDerive(QuoteBuf, // temporary on-stack string buffer for quoting
  (ZtString<
    ZtStringBuiltin<128,
      ZtStringHeapID<"ZfCLI.Quote",
	ZtStringSharded<true>>>>));

struct AsStringDeflt {
  template <typename Quote, typename O>
  struct Handler {
    template <typename S>
    ZuInline static void save(S &s, const O &o) {
      QuoteBuf buf;
      buf << o;
      Quote::quote(s, buf);
    }
    ZuInline static O load(ZuCSpan span) { return O(span); }
  };
};

} // ZfCLI

ZfCLI::AsStringDeflt ZfCLI_StringFmt(...);

namespace ZfCLIError {

constexpr auto Component = "ZfCLI"_z;

inline auto unrecognizedOption(ZuCSpan option) {
  return [option = ZeString{option}](auto &s) {
    s << "unrecognized option '" << option << '\'';
  };
}

inline auto missingValue(ZuCSpan option) {
  return [option = ZeString{option}](auto &s) {
    s << "option '" << option << "' requires a value";
  };
}

inline auto badOption(ZuCSpan option) {
  return [option = ZeString{option}](auto &s) {
    s << "invalid option '" << option << '\'';
  };
}

inline auto badKey(ZuCSpan source) {
  return [source = ZeString{source}](auto &s) {
    s << "invalid nested field for " << source;
  };
}

inline auto unterminated(unsigned position, char quote) {
  return [position, quote](auto &s) {
    s << "unterminated ";
    if (quote)
      s << "quote " << quote;
    else
      s << "escape";
    if (position != unsigned(-1)) s << " in argument " << position;
  };
}

inline auto missingRedirect(unsigned position, bool output) {
  return [position, output](auto &s) {
    s << (output ? "output" : "input") <<
      " redirection in argument " << position << " requires a path";
  };
}

inline auto badValue(
    ZuCSpan source, ZuCSpan expected, ZuCSpan value) {
  return [
    source = ZeString{source},
    expected = ZeString{expected},
    value = ZeString{value}
  ](auto &s) {
    s << "invalid " << expected << " '" << value << "' for " << source;
  };
}

inline auto badType(ZuCSpan source, ZuCSpan expected) {
  return [
    source = ZeString{source}, expected = ZeString{expected}
  ](auto &s) {
    s << "expected " << expected << " for " << source;
  };
}

inline auto multipleValues(ZuCSpan source) {
  return [source = ZeString{source}](auto &s) {
    s << source << " was specified more than once";
  };
}

inline auto required(ZuCSpan source) {
  return [source = ZeString{source}](auto &s) {
    s << source << " is required";
  };
}

template <typename Minimum, typename Maximum, typename V>
inline auto badRange(
    ZuCSpan source, Minimum minimum, Maximum maximum, V value) {
  return [
    source = ZeString{source}, minimum, maximum, value
  ](auto &s) {
    s << "value " << value << " for " << source << " is out of range; " <<
      "expected " << minimum << " <= value <= " << maximum;
  };
}

template <typename Map>
inline auto badEnum(ZuCSpan source, ZuCSpan value) {
  return [
    source = ZeString{source}, value = ZeString{value}
  ](auto &s) {
    s << "invalid value '" << value << "' for " << source <<
      "; expected one of { ";
    bool first = true;
    Map::all([&s, &first](ZuCSpan key, auto) {
      if (!first) s << ", ";
      first = false;
      s << key;
    });
    s << " }";
  };
}

} // ZfCLIError

#define ZfCLI_EXCEPT(...) \
  ZeMkException(Ze::Error, __FILE__, __LINE__, ZuFnName, \
    ZfCLIError::Component, __VA_ARGS__)

namespace ZuFieldProp::CLI {

template <ZuString ID_> struct ID { };
template <uint8_t I> struct BytesFmt { };
template <typename Fmt> struct NumberFmt { using T = Fmt; };
template <uint8_t I> struct BoolFmt { };
template <typename Fmt> struct TimeFmt { using T = Fmt; };

using Base64 = BytesFmt<ZfCLI::Base64>;
using Base64URL = BytesFmt<ZfCLI::Base64URL>;
using Base32 = BytesFmt<ZfCLI::Base32>;
using Hex = BytesFmt<ZfCLI::Hex>;
using Escaped = BytesFmt<ZfCLI::Escaped>;

template <typename Fmt = ZtFmt::Default>
using Number = NumberFmt<ZfCLI::NumberFmt<Fmt>>;

using Bool_TRUE_FALSE = BoolFmt<ZfCLI::Bool_TRUE_FALSE>;
using Bool_True_False = BoolFmt<ZfCLI::Bool_True_False>;
using Bool_true_false = BoolFmt<ZfCLI::Bool_true_false>;
using Bool_T_F = BoolFmt<ZfCLI::Bool_T_F>;
using Bool_t_f = BoolFmt<ZfCLI::Bool_t_f>;
using Bool_YES_NO = BoolFmt<ZfCLI::Bool_YES_NO>;
using Bool_Yes_No = BoolFmt<ZfCLI::Bool_Yes_No>;
using Bool_yes_no = BoolFmt<ZfCLI::Bool_yes_no>;
using Bool_Y_N = BoolFmt<ZfCLI::Bool_Y_N>;
using Bool_y_n = BoolFmt<ZfCLI::Bool_y_n>;
using Bool_0_1 = BoolFmt<ZfCLI::Bool_0_1>;

template <uint8_t Scale, int8_t NDP>
using ISO = TimeFmt<ZfCLI::TimeFmt<ZfCLI::ISO, Scale, NDP>>;
template <uint8_t Scale, int8_t NDP>
using FIX = TimeFmt<ZfCLI::TimeFmt<ZfCLI::FIX, Scale, NDP>>;
template <uint8_t Scale, int8_t NDP>
using CSV = TimeFmt<ZfCLI::TimeFmt<ZfCLI::CSV, Scale, NDP>>;
template <uint8_t Scale, int8_t NDP>
using Unix = TimeFmt<ZfCLI::TimeFmt<ZfCLI::Unix, Scale, NDP>>;

template <
  typename Field,
  bool = HasValue<typename Field::Props, ID>{}>
struct GetID_ {
  using T = ZuStringT<Field::id()>;
};
template <typename Field>
struct GetID_<Field, true> {
  using T = GetValue<typename Field::Props, ID>;
};
template <typename Field>
using GetID = typename GetID_<Field>::T;

template <typename Props, bool = HasValue<Props, BytesFmt>{}>
struct GetBytesFmt_ {
  using T = ZuConstant<uint8_t, ZfCLI::Base64URL>;
};
template <typename Props>
struct GetBytesFmt_<Props, true> {
  using T = GetValue<Props, BytesFmt>;
};
template <typename Props>
using GetBytesFmt = typename GetBytesFmt_<Props>::T;

template <typename Props, bool = HasType<Props, NumberFmt>{}>
struct GetNumberFmt_ {
  using T = ZfCLI::NumberFmt<ZtFmt::Default>;
};
template <typename Props>
struct GetNumberFmt_<Props, true> {
  using T = GetType<Props, NumberFmt>;
};
template <typename Props>
using GetNumberFmt = typename GetNumberFmt_<Props>::T;

template <typename Props, bool = HasValue<Props, BoolFmt>{}>
struct GetBoolFmt_ {
  using T = ZuConstant<uint8_t, ZfCLI::Bool_true_false>;
};
template <typename Props>
struct GetBoolFmt_<Props, true> {
  using T = GetValue<Props, BoolFmt>;
};
template <typename Props>
using GetBoolFmt = typename GetBoolFmt_<Props>::T;

template <typename Props, bool = HasType<Props, TimeFmt>{}>
struct GetTimeFmt_ {
  using T = ZfCLI::TimeFmt<ZfCLI::ISO, ZfCLI::Sec, 3>;
};
template <typename Props>
struct GetTimeFmt_<Props, true> {
  using T = GetType<Props, TimeFmt>;
};
template <typename Props>
using GetTimeFmt = typename GetTimeFmt_<Props>::T;

// field long option name (defaults to field ID)
template <ZuString ID> struct Long { };

// CLI parameter specification
template <int8_t I> struct Arg {	// fixed argument position
  ZuAssert(I >= 0 && I < 32);
};
template <int8_t I> struct Args {	// fixed argument pack position
  ZuAssert(I >= 0 && I < 32);
};
template <int8_t C> struct Opt { };	// option "-x value"
template <int8_t C> struct Flag { };	// flag "-x"

// GetLong<Field> - ZuStringT
// - gets the long option name for the field
// - the Field is passed because the value defaults to Field::id()
template <
  typename Field,
  bool = HasValue<typename Field::Props, Long>{}>
struct GetLong_ {
  using T = ZuStringT<Field::id()>;
};
template <typename Field>
struct GetLong_<Field, true> {
  using T = GetValue<typename Field::Props, Long>;
};
template <typename Field>
using GetLong = typename GetLong_<Field>::T;

// GetParam - ZuConstant<int8_t>
template <
  template <int8_t> class Prop,
  typename Props,
  bool = HasValue<Props, Prop>{}>
struct GetParam {
  using T = ZuConstant<int8_t, -1>; // default - -1 is sentinel for unspecified
};
template <
  template <int8_t> class Prop,
  typename Props>
struct GetParam<Prop, Props, true> {
  using T = GetValue<Props, Prop>;
};
template <typename Props> using GetOpt = typename GetParam<Opt, Props>::T;
template <typename Props> using GetFlag = typename GetParam<Flag, Props>::T;
template <typename Props> using GetArg = typename GetParam<Arg, Props>::T;
template <typename Props> using GetArgs = typename GetParam<Args, Props>::T;

} // ZuFieldProp::CLI

namespace ZfCLI {

// --- input functions

// this implementation intentionally assumes UTF8

ZuInline constexpr bool isspace__(char c) {
  return ((c >= '\t' && c <= '\r') || c == ' ');
}

// eos() finds end of a string (EOS) in 1-pass, mutating the contents as
// necessary if quoted characters are embedded
// - returns {output, input, delimiter}
// - returns {-1, -1, 0} if span is empty
// - output is the length of the (possibly mutated) output
// - input is the offset of the remaining input
// - delimiter is the character that ended the input, 0 if end of span
// - leaves strings without any quoted characters as-is (fast path)
// - ... but null-terminates them by overwriting the trailing delimiter
//   (normally ' ') unless the string ends with the span,
//   in which case '\0' is returned and no null-terminator is written
// - mutates quoted strings in-place (slow path)
// - any mutation is reduction, shifting down the remainder of the string
// - truncated bytes and the terminating delimiter are overwritten with 0
// - Example: "foo\\ bar " -> "foo bar\0\0" returning { 7, 9, ' ' }
ZfExtern ZuTuple<int, int, char> eos(
  ZuSpan<char>, unsigned position = unsigned(-1));

// find end of key '='
ZfExtern int eok(ZuCSpan);

struct Node_HeapID : public ZuStringT<"ZfCLI.Node"> { };

class AnyNode {
  AnyNode() = delete;
  AnyNode(const AnyNode &) = delete;
  AnyNode &operator =(const AnyNode &) = delete;
  AnyNode(AnyNode &&) = delete;
  AnyNode &operator =(AnyNode &&) = delete;

public:
  int		type;
  ZeString	source;

  AnyNode(int type_, ZuCSpan source_ = {}) :
    type{type_}, source{source_} { }
  virtual ~AnyNode() = default;

  template <typename Data>
  bool has() const;

  template <typename Data>
  decltype(auto) data(this auto &&);

  static constexpr unsigned LNodeSize = 128;
  static constexpr int ArraySize =
    (LNodeSize - sizeof(ZtArray<ZuPtr<AnyNode>>)) / sizeof(ZuPtr<AnyNode>);
  ZuAssert(ArraySize > 0);

  ZuDerive(String, ZuSpan<char>);
  ZuDerive(Array, (ZtBuiltin<
      ZtArray<ZuPtr<AnyNode>, ZtArrayHeapID_<Node_HeapID>>, ArraySize>));
  ZmRBTreeKVDerive(Object, ZuCSpan, ZuPtr<AnyNode>,
    ZmRBTreeUnique<true,
      ZmRBTreeHeapID_<Node_HeapID>>);

  using TL = ZuTypeList<String, Array, Object>;

  template <typename T>
  using Index = ZuTypeIndex<T, TL>;
};

template <typename Data, typename Heap = ZuVoid>
class Node_ : public Heap, public AnyNode {

public:
  Node_ &operator =(const Node_ &) = delete;
  Node_ &operator =(Node_ &&) = delete;

private:
  Node_(const Node_ &) = delete;
  Node_(Node_ &&) = delete;

public:
  using AnyNode::TL;

  template <typename ...Args,
    decltype(Data(ZuDeclVal<Args &&>()...), int()) = 0>
  Node_(ZuCSpan source_, Args &&...args) :
    AnyNode{ZuTypeIndex<Data, TL>{}(), source_},
    data(ZuFwd<Args>(args)...) { }
  ~Node_() = default;

  Data	data;
};

template <typename Data>
using Node_Heap = ZmHeap_<Node_HeapID, Node_<Data>>;
template <typename Data>
ZuDerive(Node, (Node_<Data, Node_Heap<Data>>));

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

template <typename Data, typename ...Args>
inline auto newNode(ZuCSpan source, Args &&...args) {
  using T = Node<Data>;
  return ZuPtr<T>{new T(source, ZuFwd<Args>(args)...)};
}

inline bool string(
    ZuPtr<AnyNode> &node, ZuSpan<char> val, ZuCSpan source) {
  if (ZuLikely(!node)) {
    node = newNode<AnyNode::String>(source, val);
    return true;
  }
  if (ZuUnlikely(node->has<AnyNode::Object>())) return false;
  if (node->has<AnyNode::String>()) {
    auto string = ZuMv(node);
    node = newNode<AnyNode::Array>(source);
    node->data<AnyNode::Array>().push(ZuMv(string));
  }
  node->data<AnyNode::Array>().push(
    newNode<AnyNode::String>(source, val));
  return true;
}

inline ZuPtr<AnyNode> *push(ZuPtr<AnyNode> &node) {
  if (ZuUnlikely(node && (
      node->has<AnyNode::Object>() ||
      node->has<AnyNode::String>())))
    return nullptr;
  if (ZuLikely(!node)) node = newNode<AnyNode::Array>({});
  auto &array = node->data<AnyNode::Array>();
  array.push(ZuPtr<AnyNode>{});
  return &array[array.length() - 1];
}

inline ZuPtr<AnyNode> *elem(ZuPtr<AnyNode> &node, unsigned index) {
  if (ZuUnlikely(node && (
      node->has<AnyNode::Object>() ||
      node->has<AnyNode::String>())))
    return nullptr;
  if (ZuLikely(!node)) node = newNode<AnyNode::Array>({});
  auto &array = node->data<AnyNode::Array>();
  array.ensure(index + 1);
  if (index >= array.length()) array.length(index + 1);
  return &array[index];
}

inline ZuPtr<AnyNode> *field(ZuPtr<AnyNode> &node, ZuCSpan key) {
  if (ZuUnlikely(node && (
      node->has<AnyNode::Array>() ||
      node->has<AnyNode::String>())))
    return nullptr;
  if (ZuLikely(!node)) node = newNode<AnyNode::Object>({});
  auto &object = node->data<AnyNode::Object>();
  if (auto node_ = object.find(key)) return &node_->val();
  auto node_ = object.add(key, ZuPtr<AnyNode>{});
  return &node_->val();
}

template <typename Config>
bool asArray(ZuPtr<AnyNode> &node) {
  if (ZuLikely(node && node->has<AnyNode::Array>())) return true;
  if (ZuUnlikely(node && node->has<AnyNode::Object>())) return false;
  if (ZuUnlikely(!node)) {
    node = newNode<AnyNode::Array>({});
    return true;
  }
  auto string = ZuMv(node);
  ZeString source = string->source;
  ZuSpan<char> span = string->data<AnyNode::String>();
  auto n = span.length();
  node = newNode<AnyNode::Array>({});
  auto &array = node->data<AnyNode::Array>();
  bool first = true;
  while (span) {
    unsigned i;
    for (i = 0; i < n; ++i)
      if (span[i] == Config::Delimiter) { span[i] = 0; break; }
    if (first) {
      first = false;
      string->data<AnyNode::String>() = ZuSpan<char>{span.data(), i};
      array.push(ZuMv(string));
    } else
      array.push(newNode<AnyNode::String>(
	source, span.data(), i));
    span.offset(i + 1);
    n = span.length();
  }
  return true;
}

using NodeArray = typename AnyNode::Array;
using CNodeArray = const NodeArray;

ZfExtern ZuTuple<int, ZuCSpan> scanKey(ZuCSpan);

// on-heap argument types used to build and parse argv[] for interoperating
// with main, execv*, etc.
ZuDerive(Arg,
  (ZtString<ZtStringBuiltin<32, ZtStringHeapID<"ZfCLI.Arg">>>));
ZuDerive(Argv,
  (ZtBuiltin<ZtArray<Arg, ZtArrayHeapID<"ZfCLI.Argv">>, BuiltinSize>));
ZuDerive(SpanArgv,
  (ZtBuiltin<ZtArray<ZuSpan<char>, ZtArrayHeapID<"ZfCLI.Argv">>, BuiltinSize>));
ZuDerive(Argv_C,
  (ZtBuiltin<ZtArray<const char *, ZtArrayHeapID<"ZfCLI.Argv">>, BuiltinSize>));

// --- output functions

// output is a 3-pass
// - first arg0 is output
// - then all options
// - finally all positional args

// quotes the following characters: space \ # ; " ' < > ? *
constexpr bool quoted(uint8_t c) {
  static constexpr uint8_t map[] = {
    0x8d, 0x04, 0x00, 0xd8, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00
  };
  if (c < 32 || c >= 128) return true;
  c -= 32;
  return map[c>>3] & (1U<<(c & 7));
}

// raw (pass-through) quoting
struct RawQuote {
  template <typename S>
  static void quote(S &s, ZuCSpan v) { s << v; }
};

// shell-style quoting
// - chooses least overhead among individual back-quoting,
//   single-quoting and double-quoting
struct ShellQuote {
  template <typename S>
  static void quote(S &s, ZuCSpan v) {
    if (!v) { s << "''"; return; }
    unsigned quote = 0, sgl = 0, dbl = 0;
    unsigned n = v.length();
    for (unsigned i = 0; i < n; i++) {
      char c = v[i];
      if (ZuLikely(!quoted(c))) continue;
      if (ZuUnlikely(c == '\''))
	++sgl;
      else if (ZuUnlikely(c == '"'))
	++dbl;
      else
	++quote;
    }
    unsigned total = quote + sgl + dbl;
    if (!total) {
      s << v;
    } else if (total <= 2) {
      // individual back-quote
      for (unsigned i = 0; i < n; i++) {
	char c = v[i];
	if (ZuUnlikely(quoted(c))) s << '\\';
	s << c;
      }
    } else if (sgl <= dbl) {
      // single quote
      s << '\'';
      for (unsigned i = 0; i < n; i++) {
	char c = v[i];
	if (ZuUnlikely(c == '\'')) s << '\\';
	s << c;
      }
      s << '\'';
    } else {
      // double quote
      s << '"';
      for (unsigned i = 0; i < n; i++) {
	char c = v[i];
	if (ZuUnlikely(c == '"')) s << '\\';
	s << c;
      }
      s << '"';
    }
  }
};

// Windows cmd-style quoting
// - for use with CreateProcess()
// - msvcrt/ucrt/msvcr*.dll parse GetCommandLine() using cmd-style quoting
struct CmdQuote {
  template <typename S>
  static void quote(S &s, ZuCSpan v) {
    if (!v) { s << "\"\""; return; }
    bool dbl = false;
    unsigned n = v.length();
    for (unsigned i = 0; i < n; i++) {
      char c = v[i];
      if (ZuUnlikely(isspace__(c) || c == '"')) { dbl = true; break; }
    }
    if (!dbl) {
      s << v;
    } else {
      // double quote
      unsigned bq = 0;
      s << '"';
      for (unsigned i = 0; i < n; i++) {
	char c = v[i];
	if (ZuUnlikely(c == '\\')) {
	  ++bq;
	} else {
	  if (ZuUnlikely(c == '"')) {
	    for (unsigned i = 0; i < bq; i++) s << '\\';
	    s << '\\';
	  }
	  bq = 0;
	}
	s << c;
      }
      for (unsigned i = 0; i < bq; i++) s << '\\';
      s << '"';
    }
  }
};

// output stream wrapper with delimiter
template <typename S>
struct OutStream {
  S		&stream;
  unsigned	argc = 0;
  bool		first = true;
  bool		eoo = false;	// "--" emitted

  template <typename U, decltype(ZuDeclVal<S &>().S::operator <<(
      ZuDeclVal<const U &>()), int()) = 0>
  inline OutStream &operator <<(const U &v) {
    if (argc > 0) {
      if (eoo)
	stream << v;
      else {
	QuoteBuf buf;
	buf << v;
	if (buf[0] == '-') {
	  eoo = true;
	  stream << "-- ";
	}
	stream << buf;
      }
    } else
      stream << v;
    return *this;
  }

  void delimit() {
    if (first) { first = false; return; }
    stream << ' ';
  }

  template <typename Props>
  void out(ZuCSpan prefix) {
    constexpr int8_t Arg = ZuFieldProp::CLI::GetArg<Props>{};
    constexpr int8_t Args = ZuFieldProp::CLI::GetArgs<Props>{};
    constexpr int8_t Opt = ZuFieldProp::CLI::GetOpt<Props>{};
    constexpr int8_t Flag = ZuFieldProp::CLI::GetFlag<Props>{};
    ZuAssert(Flag < 0);
    delimit();
    if constexpr (Arg > 0 || Args > 0) {
      ++argc;
      constexpr int8_t N = Arg > 0 ? Arg : Args;
      while (argc < N) { stream << "'' "; ++argc; }
    } else if constexpr (Opt >= 0) {
      stream << '-' << char(Opt) << ' ';
    } else {
      stream << "--" << prefix << '=';
    }
  }
};

template <>
struct OutStream<Argv> {
  Argv		&argv;
  bool		delimited = true;
  unsigned	argc = 0;
  bool		first = true;
  bool		eoo = false;	// "--" emitted

  template <typename U, decltype(ZuDeclVal<Arg &>().Arg::operator <<(
      ZuDeclVal<const U &>()), int()) = 0>
  inline OutStream &operator <<(const U &v) {
    Arg *arg_;
    if (delimited) {
      delimited = false;
      arg_ = new (argv.push()) Arg();
    } else {
      arg_ = &argv[argv.length() - 1];
    }
    auto &arg = *arg_;
    arg << v;
    if (argc > 0) {
      if (!eoo && arg[0] == '-') {
	eoo = true;
	argv.push(ZuMv(arg));
	arg = "--";
      }
    }
    return *this;
  }

  void delimit() {
    delimited = true;
  }

  template <typename Props>
  void out(ZuCSpan prefix) {
    constexpr int8_t Arg_ = ZuFieldProp::CLI::GetArg<Props>{};
    constexpr int8_t Args_ = ZuFieldProp::CLI::GetArgs<Props>{};
    constexpr int8_t Opt = ZuFieldProp::CLI::GetOpt<Props>{};
    constexpr int8_t Flag = ZuFieldProp::CLI::GetFlag<Props>{};
    ZuAssert(Flag < 0);
    if constexpr (Arg_ > 0 || Args_ > 0) {
      ++argc;
      constexpr int8_t N = Arg_ > 0 ? Arg_ : Args_;
      if (argc < N) {
	argv.length(argv.length() + (N - argc));
	argc = N;
      }
      delimited = true;
    } else if constexpr (Opt >= 0) {
      *(new (argv.push()) Arg()) << '-' << char(Opt);
      delimited = true;
    } else {
      *(new (argv.push()) Arg()) << "--" << prefix << '=';
      delimited = false;
    }
  }
};

// --- field save/load

struct AsObject;	// as object
template <unsigned ElemCode, typename ElemProps = ZuTypeList<>>
struct AsArray;		// as array
struct AsString;	// as string
struct AsJSON;		// as embedded JSON

template <typename>
struct IsAsArray : public ZuFalse { };
template <unsigned ElemCode, typename ElemProps>
struct IsAsArray<AsArray<ElemCode, ElemProps>> : public ZuTrue { };

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

} // ZfCLI

ZfCLI::AsDeflt ZfCLI_Fmt(...);	// default

namespace ZfCLI {

template <typename O>
using As = decltype(ZfCLI_Fmt(ZuDeclVal<O *>()));

template <typename O, typename Facet>
using Format = ZuIf<
  ZuIsSame<As<O>, AsDeflt>{},
  typename AsDeflt_<O, Facet>::T,
  As<O>>;

// save an individual field
template <
  typename Facet, template <typename> class Filter,
  typename Quote, typename Field,
  typename S, typename O>
void saveField(S &, const O &, ZuCSpan prefix);

// save an individual value
template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props,
  typename S, typename T>
void saveValue(S &s, const T &v, ZuCSpan prefix);

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
auto loadValue(AnyNode *);
template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
auto loadValue(ZuPtr<AnyNode> &);

// save/load handler for object-formatted types
// - object-formatted in CLI query strings means multiple individual fields
struct AsObject {
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;

    template <typename Field>
    using CtorIndex = ZuFieldProp::GetCtor<typename Field::Props>;
    template <typename Field>
    using Required =
      ZuTypeIn<ZuFieldProp::Required, typename Field::Props>;
    template <typename Field>
    using UpdReq = ZuBool<
      ZfFieldFilter::Upd<Field>{}() &&
      ZuTypeIn<ZuFieldProp::Required, typename Field::Props>{}()>;

    using AllFields = ZuFields<O, Facet>;
    using LoadFields = ZuTypeGrep<ZfFieldFilter::Load, AllFields>;
    using SaveFields = ZuTypeGrep<ZfFieldFilter::Save, AllFields>;
    using CtorFields_ = ZuTypeGrep<ZfFieldFilter::Ctor, AllFields>;
    using CtorFields = ZuTypeSort<CtorIndex, CtorFields_>;
    using InitFields = ZuTypeGrep<ZfFieldFilter::Init, AllFields>;
    using UpdFields = ZuTypeGrep<ZfFieldFilter::Upd, AllFields>;
    using DelFields = ZuTypeGrep<ZfFieldFilter::Del, AllFields>;
    using ReqFields = ZuTypeGrep<Required, AllFields>;
    using UpdReqFields = ZuTypeGrep<UpdReq, AllFields>;

    template <
      template <typename> class Filter, typename Quote,
      typename Props = ZuTypeList<>,
      typename S>
    static void save(S &s, const O &o, ZuCSpan prefix) {
      using Fields = ZuTypeGrep<Filter, AllFields>;
      ZuUnroll::all<Fields>([&s, &o, &prefix]<typename Field>() {
	saveField<Facet, Filter, Quote, Field>(s, o, prefix);
      });
    }

    const AnyNode	*node;
    ZeString		prefix;

    Handler(const AnyNode *node_, ZuCSpan prefix_ = {}) :
      node{node_}, prefix{prefix_} { }

    template <template <typename> class Filter, typename Field>
    auto loadField() const;

    template <typename Field>
    bool hasField() const {
      if (ZuUnlikely(!node->has<AnyNode::Object>())) return false;
      const auto &object = node->data<AnyNode::Object>();
      ZuCSpan longOpt = ZuFieldProp::CLI::GetLong<Field>{}().cspan();
      return object.find(longOpt);
    }

    template <typename Fields>
    void checkRequired() const {
      if constexpr (Fields::N)
	ZuUnroll::all<Fields>([this]<typename Field>() {
	  if (!this->hasField<Field>())
	    throw ZfCLI_EXCEPT(
	      ZfCLIError::required(source<Field>(prefix.cspan())));
	});
    }

    template <typename ...Field>
    struct Ctor {
      template <typename ...Args>
      static O ctor(const Handler &handler, Args &&...args) {
	return O(
	  ZuFwd<Args>(args)...,
	  handler.loadField<ZfFieldFilter::Load, Field>()...);
      }
      template <typename ...Args>
      static O *alloc(const Handler &handler, Args &&...args) {
	return new O(
	  ZuFwd<Args>(args)...,
	  handler.loadField<ZfFieldFilter::Load, Field>()...);
      }
      template <typename ...Args>
      static void new_(void *o, const Handler &handler, Args &&...args) {
	new (o) O(
	  ZuFwd<Args>(args)...,
	  handler.loadField<ZfFieldFilter::Load, Field>()...);
      }
    };
    template <typename ...Args>
    O ctor(Args &&...args) const {
      checkRequired<ReqFields>();
      if constexpr (!InitFields::N)
	// exploit guaranteed copy elision
	return ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
      else {
	O o = ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
	ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	  Field::set(o, loadField<ZfFieldFilter::Load, Field>());
	});
	return o;
      }
    }
    template <typename ...Args>
    O *alloc(Args &&...args) const {
      checkRequired<ReqFields>();
      O *o = ZuTypeApply<Ctor, CtorFields>::alloc(
	*this, ZuFwd<Args>(args)...);
      if constexpr (InitFields::N)
	ZuUnroll::all<InitFields>([this, o]<typename Field>() {
	  Field::set(*o, loadField<ZfFieldFilter::Load, Field>());
	});
      return o;
    }
    template <typename ...Args>
    void new_(void *o_, Args &&...args) const {
      checkRequired<ReqFields>();
      ZuTypeApply<Ctor, CtorFields>::new_(o_, *this, ZuFwd<Args>(args)...);
      O &o = *static_cast<O *>(o_);
      ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	Field::set(o, this->loadField<ZfFieldFilter::Load, Field>());
      });
    }

    void load(O &o) const {
      checkRequired<ReqFields>();
      ZuUnroll::all<LoadFields>([this, &o]<typename Field>() {
	using Props = typename Field::Props;
	if (ZuTypeIn<ZuFieldProp::Reset, Props>{}() || hasField<Field>())
	  Field::set(o, this->loadField<ZfFieldFilter::Load, Field>());
      });
    }
    void update(O &o) const {
      checkRequired<UpdReqFields>();
      ZuUnroll::all<UpdFields>([this, &o]<typename Field>() {
	using Props = typename Field::Props;
	if (ZuTypeIn<ZuFieldProp::Reset, Props>{}() || hasField<Field>())
	  Field::set(o, this->loadField<ZfFieldFilter::Upd, Field>());
      });
    }

  private:
    template <typename Field>
    static ZeString source(ZuCSpan prefix) {
      using Props = typename Field::Props;
      constexpr int8_t Arg = ZuFieldProp::CLI::GetArg<Props>{};
      constexpr int8_t Args = ZuFieldProp::CLI::GetArgs<Props>{};
      constexpr int8_t Opt = ZuFieldProp::CLI::GetOpt<Props>{};
      constexpr int8_t Flag = ZuFieldProp::CLI::GetFlag<Props>{};
      ZeString s;
      if constexpr (Arg >= 0)
	s << "argument " << ZuBoxed(Arg);
      else if constexpr (Args >= 0)
	s << "argument " << ZuBoxed(Args);
      else if constexpr (Opt >= 0)
	s << "option '-" << char(Opt) << '\'';
      else if constexpr (Flag >= 0)
	s << "option '-" << char(Flag) << '\'';
      else {
	s << "option '--";
	if (prefix) s << prefix << '.';
	s << ZuFieldProp::CLI::GetLong<Field>{}().cspan() << '\'';
      }
      return s;
    }

  };
};

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
struct LoadVec :
  public ZuMArray<LoadVec<Facet, Filter, TypeCode, Props, T>, CNodeArray, T>
{
  ZuDerive_(LoadVec, (ZuMArray<LoadVec, CNodeArray, T>))
  using Base::underlying;
  T get(unsigned i) const & {
    return loadValue<Facet, Filter, TypeCode, Props, T>(underlying[i]);
  }
  template <typename V> void set(unsigned, const V &) { }
};

// save/load handler for array-formatted types
template <unsigned ElemCode, typename ElemProps>
struct AsArray {
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;

    using Config = ZfCLI::Config<Facet>;

    // the top-level cannot be AsArray, so prefix must be non-null
    template <
      template <typename> class Filter, typename Quote,
      typename Props = ZuTypeList<>,
      typename S>
    static void save(S &s, const O &o, ZuCSpan prefix) {
      unsigned n = ZuTraits<O>::length(o);
      if constexpr (Config::ArrayFmt == Bare) {
	for (unsigned i = 0; i < n; i++)
	  saveValue<Facet, Filter, Quote, ElemCode, ElemProps>(s, o[i], prefix);
      } else { // Delimited - no nesting is possible, use AsString
	using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
	using AsString_ = decltype(ZfCLI_StringFmt(ZuDeclVal<Elem *>()));
	using Handler_ = typename AsString_::template Handler<Quote, Elem>;
	s.template out<Props>(prefix);
	for (unsigned i = 0; i < n; i++) {
	  if (i) s << char(Config::Delimiter);
	  Handler_::save(s, o[i]);
	}
      }
    }

    const AnyNode	*node;

    Handler(const AnyNode *node_) : node{node_} { }

    using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
    using LoadVec_ =
      LoadVec<Facet, ZfFieldFilter::Load, ElemCode, ElemProps, Elem>;
    template <typename ...Args>
    O ctor(Args &&...args) const {
      if (ZuUnlikely(!node->has<AnyNode::Array>()))
	return O(ZuFwd<Args>(args)...);
      validate<ZfFieldFilter::Load>();
      return O(
	ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
    }
    template <typename ...Args>
    O *alloc(Args &&...args) const {
      if (ZuUnlikely(!node->has<AnyNode::Array>()))
	return new O(ZuFwd<Args>(args)...);
      validate<ZfFieldFilter::Load>();
      return new O(
	ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
    }
    template <typename ...Args>
    void new_(void *o, Args &&...args) const {
      if (ZuUnlikely(!node->has<AnyNode::Array>()))
	new (o) O(ZuFwd<Args>(args)...);
      else {
	validate<ZfFieldFilter::Load>();
	new (o) O(
	  ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
      }
    }

    void load(O &o) const {
      if (ZuLikely(node->has<AnyNode::Array>())) {
	validate<ZfFieldFilter::Load>();
	o = LoadVec_(node->data<AnyNode::Array>());
      }
    }
    void update(O &o) const {
      if (ZuUnlikely(!node->has<AnyNode::Array>())) return;
      const auto &nodes = node->data<AnyNode::Array>();
      unsigned n = ZuTraits<O>::length(o);
      unsigned m = nodes.length();
      if (n > m) n = m;
      if constexpr (ElemCode == ZfFieldTC::UDT) {
	using ElemHandler = typename As<Elem>::template Handler<Elem, Facet>;
	for (unsigned i = 0; i < n; i++)
	  ElemHandler{nodes[i]}.update(o[i]);
      } else {
	for (unsigned i = 0; i < n; i++)
	o[i] = loadValue<
	    Facet, ZfFieldFilter::Upd, ElemCode, ElemProps, Elem>(nodes[i]);
      }
    }

  private:
    template <template <typename> class Filter>
    void validate() const {
      const auto &nodes = node->data<AnyNode::Array>();
      for (unsigned i = 0, n = nodes.length(); i < n; i++)
	loadValue<Facet, Filter, ElemCode, ElemProps, Elem>(nodes[i]);
    }
  };
};

// save/load handler for string-formatted types
struct AsString {
  template <typename O_, typename>
  struct Handler {
    using O = O_;

    using AsString_ = decltype(ZfCLI_StringFmt(ZuDeclVal<O *>()));
    using Handler_ = typename AsString_::template Handler<ShellQuote, O>;

    template <
      template <typename> class Filter, typename Quote,
      typename Props = ZuTypeList<>,
      typename S>
    static void save(S &s, const O &o, ZuCSpan prefix) {
      using Handler_ = typename AsString_::template Handler<Quote, O>;
      s.template out<Props>(prefix);
      Handler_::save(s, o);
    }
 
    const AnyNode	*node;

    Handler(const AnyNode *node_) : node{node_} { }

    ZuCSpan span() const {
      if (ZuUnlikely(node->has<AnyNode::Array>()))
	throw ZfCLI_EXCEPT(
	  ZfCLIError::multipleValues(node->source));
      if (!node->has<AnyNode::String>()) return {};
      return node->data<AnyNode::String>();
    }

    O ctor() const { return Handler_::load(span()); }
    O *alloc() const { return new O(Handler_::load(span())); }
    void new_(void *o) const { new (o) O(Handler_::load(span())); }
    void load(O &o) const { o = Handler_::load(span()); }
    void update(O &o) const { o = Handler_::load(span()); }
  };
};

// save/load handler for JSON-formatted types
struct AsJSON {
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;
    using Handler_ = typename ZfJSON::As<O>::template Handler<O, Facet>;

    template <
      template <typename> class Filter, typename Quote,
      typename Props = ZuTypeList<>,
      typename S>
    static void save(S &s, const O &o, ZuCSpan prefix) {
      QuoteBuf buf;
      Handler_::template save<Filter>(buf, o);
      s.template out<Props>(prefix);
      Quote::quote(s, buf);
    }

    const AnyNode	*node;

    Handler(const AnyNode *node_) : node{node_} { }

    ZuTuple<ZuPtr<ZfJSON::AnyNode>, Handler_> handler_() const {
      if (ZuUnlikely(node->has<AnyNode::Array>()))
	throw ZfCLI_EXCEPT(
	  ZfCLIError::multipleValues(node->source));
      ZuSpan<char> span;
      if (node->has<AnyNode::String>())
	span = node->data<AnyNode::String>();
      auto scan = ZfJSON::scan(span);
      if (scan.template p<0>() < 0)
	throw ZfCLI_EXCEPT(
	  ZfCLIError::badValue(node->source, "JSON", span));
      ZuPtr<ZfJSON::AnyNode> node_ = ZuMv(scan.template p<1>());
      const ZfJSON::AnyNode *ptr = (*node_)[0].ptr();
      return {ZuMv(node_), Handler_(ptr)};
    }

    template <typename ...Args>
    O ctor(Args &&...args) const {
      return handler_().template p<1>().ctor(ZuFwd<Args>(args)...);
    }
    template <typename ...Args>
    O *alloc(Args &&...args) const {
      return handler_().template p<1>().alloc(ZuFwd<Args>(args)...);
    }
    template <typename ...Args>
    void new_(void *o, Args &&...args) const {
      handler_().template p<1>().new_(o, ZuFwd<Args>(args)...);
    }
    void load(O &o) const { handler_().template p<1>().load(o); }
    void update(O &o) const { handler_().template p<1>().update(o); }
  };
};

template <
  typename Facet, template <typename> class Filter, typename Quote,
  unsigned TypeCode, typename Props,
  typename S, typename T, typename L>
inline void saveValue_(S &s, const T &v_, L &&l)
{
  if constexpr (
      TypeCode == ZfFieldTC::CString ||
      TypeCode == ZfFieldTC::String) {
    l(s);
    Quote::quote(s, v_);
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    l(s);
    constexpr unsigned Fmt = ZuFieldProp::CLI::GetBytesFmt<Props>{};
    if constexpr (Fmt == ZfCLI::Base64) {
      ZuBSpan v{v_};
      unsigned n = ZuBase64::enclen(v.length());
      auto buf = ZmScratch(uint8_t, n);
      buf.length(n);
      buf.length(ZuBase64::encode(buf, v));
      Quote::quote(s, ZuCSpan(buf)); // base64 needs quoting
    } else if constexpr (Fmt == ZfCLI::Base64URL) {
      ZuBSpan v{v_};
      unsigned n = ZuBase64URL::enclen(v.length());
      auto buf = ZmScratch(uint8_t, n);
      buf.length(n);
      buf.length(ZuBase64URL::encode(buf, v));
      s << ZuCSpan(buf);
    } else if constexpr (Fmt == ZfCLI::Base32) {
      ZuBSpan v{v_};
      unsigned n = ZuBase32::enclen(v.length());
      auto buf = ZmScratch(uint8_t, n);
      buf.length(n);
      buf.length(ZuBase32::encode(buf, v));
      s << ZuCSpan(buf);
    } else if constexpr (Fmt == ZfCLI::Hex) {
      ZuBSpan v{v_};
      unsigned n = ZuHex::enclen(v.length());
      auto buf = ZmScratch(uint8_t, n);
      buf.length(n);
      buf.length(ZuHex::encode(buf, v));
      s << ZuCSpan(buf);
    } else // if constexpr (Fmt == ZfCLI::Escaped)
      Quote::quote(s, v_);
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    l(s);
    constexpr unsigned Fmt = ZuFieldProp::CLI::GetBoolFmt<Props>{};
    bool v = v_;
    if constexpr (Fmt == ZfCLI::Bool_TRUE_FALSE)
      s << (v ? "TRUE" : "FALSE");
    else if constexpr (Fmt == ZfCLI::Bool_True_False)
      s << (v ? "True" : "False");
    else if constexpr (Fmt == ZfCLI::Bool_true_false)
      s << (v ? "true" : "false");
    else if constexpr (Fmt == ZfCLI::Bool_T_F)
      s << (v ? "T" : "F");
    else if constexpr (Fmt == ZfCLI::Bool_t_f)
      s << (v ? "t" : "f");
    else if constexpr (Fmt == ZfCLI::Bool_YES_NO)
      s << (v ? "YES" : "NO");
    else if constexpr (Fmt == ZfCLI::Bool_Yes_No)
      s << (v ? "Yes" : "No");
    else if constexpr (Fmt == ZfCLI::Bool_yes_no)
      s << (v ? "yes" : "no");
    else if constexpr (Fmt == ZfCLI::Bool_Y_N)
      s << (v ? "Y" : "N");
    else if constexpr (Fmt == ZfCLI::Bool_y_n)
      s << (v ? "y" : "n");
    else // if constexpr (Fmt == ZfCLI::Bool_0_1)
      s << (v ? "0" : "1");
  } else if constexpr (
      TypeCode == ZfFieldTC::Int8 ||
      TypeCode == ZfFieldTC::Int16 ||
      TypeCode == ZfFieldTC::Int32 ||
      TypeCode == ZfFieldTC::Int64 ||
      TypeCode == ZfFieldTC::Int128 ||
      TypeCode == ZfFieldTC::UInt8 ||
      TypeCode == ZfFieldTC::UInt16 ||
      TypeCode == ZfFieldTC::UInt32 ||
      TypeCode == ZfFieldTC::UInt64 ||
      TypeCode == ZfFieldTC::UInt128) {
    using Fmt = ZuFieldProp::CLI::GetNumberFmt<Props>;
    if constexpr (ZuIsBoxed<T>{}) {
      l(s);
      s << ZfFieldPrintInt<Props, typename Fmt::Fmt, T>(v_);
    } else {
      using B = ZuBox<ZfFieldTC::Type<TypeCode>>;
      auto v = B{v_};
      if (!*v) return;
      l(s);
      s << ZfFieldPrintInt<Props, typename Fmt::Fmt, B>(v);
    }
  } else if constexpr (TypeCode == ZfFieldTC::Float) {
    using Fmt = ZuFieldProp::CLI::GetNumberFmt<Props>;
    double v = v_;
    if (ZuUnlikely(ZuNull(v))) return;
    l(s);
    bool negative = v < 0;
    if (negative) { s << '-'; v = -v; }
    if (ZuUnlikely(v == ZuCmp<double>::inf())) { s << "Infinity"; return; }
    int e = 0;
    if (ZuUnlikely(v < 1.0e-9 || v >= 1.0e18)) {
      e = log10(v);
      if (e < 0) --e;
      v = v * pow(10, -e);
      if (v >= 10.0) { v /= 10.0; ++e; }
    }
    s << ZuBoxed(v).fmt<typename Fmt::Fmt>();
    if (e) { s << 'e'; if (e > 0) s << '+'; s << e; }
  } else if constexpr (TypeCode == ZfFieldTC::Fixed) {
    using Fmt = ZuFieldProp::CLI::GetNumberFmt<Props>;
    ZuFixed v = v_;
    if (ZuUnlikely(!*v)) return;
    l(s);
    s << v.fmt<typename Fmt::Fmt>();
  } else if constexpr (TypeCode == ZfFieldTC::Decimal) {
    using Fmt = ZuFieldProp::CLI::GetNumberFmt<Props>;
    ZuDecimal v = v_;
    if (ZuUnlikely(!*v)) return;
    l(s);
    s << v.fmt<typename Fmt::Fmt>();
  } else if constexpr (
      TypeCode == ZfFieldTC::Time ||
      TypeCode == ZfFieldTC::DateTime) {
    using Fmt = ZuFieldProp::CLI::GetTimeFmt<Props>;
    if constexpr (Fmt::Fmt == ZfCLI::Unix) {
      ZuTime v{v_};
      if (!*v) return;
      l(s);
      if constexpr (Fmt::Unit == ZfCLI::Sec) {
	s << ZuBoxed(v.sec());
	if constexpr (Fmt::NDP)
	  s << '.' << ZuBoxed(v.nsec()).fmt<ZuFmt::Frac<9, Fmt::NDP>>();
      } else if constexpr (Fmt::Unit == ZfCLI::MSec) {
	int128_t t = int128_t(v.sec());
	int64_t f = v.nsec();
	t = t * 1000U + (f / 1000000U);
	f %= 1000000U;
	s << ZuBoxed(t);
	if constexpr (Fmt::NDP)
	  s << '.' << ZuBoxed(f).fmt<ZuFmt::Frac<6, Fmt::NDP>>();
      } else if constexpr (Fmt::Unit == ZfCLI::USec) {
	int128_t t = int128_t(v.sec());
	int64_t f = v.nsec();
	t = t * 1000000U + (v.nsec() / 1000U);
	f %= 1000U;
	s << ZuBoxed(t);
	if constexpr (Fmt::NDP)
	  s << '.' << ZuBoxed(f).fmt<ZuFmt::Frac<3, Fmt::NDP>>();
      } else if constexpr (Fmt::Unit == ZfCLI::NSec) {
	int128_t t = int128_t(v.sec());
	t = t * 1000000000U + v.nsec();
	s << ZuBoxed(t);
      }
    } else if constexpr (Fmt::Fmt == ZfCLI::CSV) {
      ZuDateTime v{v_};
      if (!*v) return;
      l(s);
      auto &fmt = ZmTLS<ZuDateTimeFmt::CSV, (int Props::*){}>();
      s << v.fmt(fmt);
    } else if constexpr (Fmt::Fmt == ZfCLI::FIX) {
      auto &fmt = ZmTLS<ZuDateTimeFmt::FIX<Fmt::NDP>, (int Props::*){}>();
      ZuDateTime v{v_};
      if (!*v) return;
      l(s);
      s << v.fmt(fmt);
    } else if constexpr (Fmt::Fmt == ZfCLI::ISO) {
      auto &fmt = ZmTLS<ZuDateTimeFmt::ISO, (int Props::*){}>();
      ZuDateTime v{v_};
      if (!*v) return;
      l(s);
      s << v.fmt(fmt);
    }
  }
}

template <
  typename Facet, template <typename> class Filter,
  typename Quote, unsigned TypeCode, typename Props,
  typename S, typename T_>
inline void saveValue(S &s, const T_ &v, ZuCSpan prefix)
{
  using T = ZuDecay<T_>;
  using Config = ZfCLI::Config<Facet>;
  if constexpr (!ZfFieldTC::IsVec<TypeCode>{}) {
    if constexpr (TypeCode == ZfFieldTC::UDT) {
      // UDT type - recurse
      using Handler = typename As<T>::template Handler<T, Facet>;
      Handler::template save<Filter, Quote, Props>(s, v, prefix);
    } else {
      // leaf type - output it
      constexpr int8_t Flag = ZuFieldProp::CLI::GetFlag<Props>{};
      if constexpr (Flag >= 0) {
	if (bool(v)) {
	  s.delimit();
	  s << '-' << char(Flag);
	}
      } else {
	saveValue_<Facet, Filter, Quote, TypeCode, Props>(s, v,
	  [&prefix](auto &s) { s.template out<Props>(prefix); });
      }
    }
  } else {
    unsigned n = ZuTraits<ZuDecay<decltype(v)>>::length(v);
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    if constexpr (Config::ArrayFmt == Bare) {
      for (unsigned i = 0; i < n; i++) {
	saveValue_<Facet, Filter, Quote, ElemCode, Props>(s, v[i],
	  [&prefix](auto &s) { s.template out<Props>(prefix); });
      }
    } else {
      s.template out<Props>(prefix);
      for (unsigned i = 0; i < n; i++) {
	if (i) s << char(Config::Delimiter);
	saveValue_<Facet, Filter, Quote, ElemCode, Props>(
	  s, v[i], [](auto &) { });
      }
    }
  }
}

template <
  typename Facet, template <typename> class Filter,
  typename Quote, typename Field,
  typename S, typename O>
inline void saveField(S &s, const O &o, ZuCSpan prefix)
{
  using Type = typename Field::Type;
  using Props = typename Field::Props;

  // append long option name to prefix (copy on stack)
  ZuCSpan longOpt = ZuFieldProp::CLI::GetLong<Field>{}().cspan();
  unsigned nestedSize = 0;
  if (ZuLikely(!prefix))
    nestedSize = longOpt.length();
  else
    nestedSize = prefix.length() + longOpt.length() + 1;
  auto nested = ZmScratch(char, nestedSize);
  if (prefix) nested << prefix << '.';
  nested << longOpt;

  saveValue<Facet, Filter, Quote, Type::Code, Props>(
    s, Field::get(o), nested);
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline T loadValue_(const AnyNode *node)
{
  if (ZuUnlikely(node->has<AnyNode::Array>()))
    throw ZfCLI_EXCEPT(
      ZfCLIError::multipleValues(node->source));
  auto type = node->type;

  if constexpr (TypeCode == ZfFieldTC::CString) {
    if (ZuUnlikely(type != AnyNode::Index<AnyNode::String>{}))
      throw ZfCLI_EXCEPT(ZfCLIError::badType(node->source, "string"));
    return node->data<AnyNode::String>().data();
  } else if constexpr (TypeCode == ZfFieldTC::String) {
    if (ZuUnlikely(type != AnyNode::Index<AnyNode::String>{}))
      throw ZfCLI_EXCEPT(ZfCLIError::badType(node->source, "string"));
    return T(node->data<AnyNode::String>());
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    if (ZuUnlikely(type != AnyNode::Index<AnyNode::String>{}))
      throw ZfCLI_EXCEPT(ZfCLIError::badType(node->source, "string"));
    auto span = node->data<AnyNode::String>();
    unsigned n = span.length();
    if (ZuUnlikely(!n)) return ZuCmp<T>::null();
    constexpr unsigned Fmt = ZuFieldProp::CLI::GetBytesFmt<Props>{};
    if constexpr (Fmt == ZfCLI::Escaped) {
      return T(span);
    } else if constexpr (
	Fmt == ZfCLI::Base64 || Fmt == ZfCLI::Base64URL ||
	Fmt == ZfCLI::Base32) {
      using Codec = ZuIf<Fmt == ZfCLI::Base64, ZuBase64,
	ZuIf<Fmt == ZfCLI::Base64URL, ZuBase64URL, ZuBase32>>;
      constexpr unsigned Mod = Fmt == ZfCLI::Base32 ? 8 : 4;
      ZuSpan<uint8_t> bytes{span};
      unsigned m = Codec::declen(n), l;
      if (bytes[n - 1] >= Mod) {
	ZeString encoded{span};
	l = Codec::decode({&bytes[0], m}, bytes);
	if (ZuUnlikely(n > Codec::enclen(l)))
	  throw ZfCLI_EXCEPT(
	    ZfCLIError::badValue(node->source, "encoded bytes", encoded));
	memset(&bytes[m], 0, (n - 1) - l);
	bytes[n - 1] = m - l;
      } else {
	l = m - bytes[n - 1];
      }
      bytes.trunc(l);
      return T(bytes);
    } else {
      ZuSpan<uint8_t> bytes{span};
      unsigned m = ZuHex::declen(n);
      if (bytes[n - 1]) {
	ZeString encoded{span};
	m = ZuHex::decode({&bytes[0], m}, bytes);
	if (ZuUnlikely(n > ZuHex::enclen(m)))
	  throw ZfCLI_EXCEPT(
	    ZfCLIError::badValue(node->source, "encoded bytes", encoded));
	memset(&bytes[m], 0, n - m);
      }
      bytes.trunc(m);
      return T(bytes);
    }
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    if (ZuUnlikely(type != AnyNode::Index<AnyNode::String>{}))
      throw ZfCLI_EXCEPT(ZfCLIError::badType(node->source, "boolean"));
    const auto &span = node->data<AnyNode::String>();
    constexpr unsigned Fmt = ZuFieldProp::CLI::GetBoolFmt<Props>{};
    int v = -1;
    if constexpr (Fmt == ZfCLI::Bool_TRUE_FALSE)
      v = span == "TRUE" ? 1 : span == "FALSE" ? 0 : -1;
    else if constexpr (Fmt == ZfCLI::Bool_True_False)
      v = span == "True" ? 1 : span == "False" ? 0 : -1;
    else if constexpr (Fmt == ZfCLI::Bool_true_false)
      v = span == "true" ? 1 : span == "false" ? 0 : -1;
    else if constexpr (Fmt == ZfCLI::Bool_T_F)
      v = span == "T" ? 1 : span == "F" ? 0 : -1;
    else if constexpr (Fmt == ZfCLI::Bool_t_f)
      v = span == "t" ? 1 : span == "f" ? 0 : -1;
    else if constexpr (Fmt == ZfCLI::Bool_YES_NO)
      v = span == "YES" ? 1 : span == "NO" ? 0 : -1;
    else if constexpr (Fmt == ZfCLI::Bool_Yes_No)
      v = span == "Yes" ? 1 : span == "No" ? 0 : -1;
    else if constexpr (Fmt == ZfCLI::Bool_yes_no)
      v = span == "yes" ? 1 : span == "no" ? 0 : -1;
    else if constexpr (Fmt == ZfCLI::Bool_Y_N)
      v = span == "Y" ? 1 : span == "N" ? 0 : -1;
    else if constexpr (Fmt == ZfCLI::Bool_y_n)
      v = span == "y" ? 1 : span == "n" ? 0 : -1;
    else
      v = span == "1" ? 1 : span == "0" ? 0 : -1;
    if (ZuUnlikely(v < 0))
      throw ZfCLI_EXCEPT(ZfCLIError::badValue(node->source, "boolean", span));
    return T(bool(v));
  } else if constexpr (
      TypeCode == ZfFieldTC::Int8 ||
      TypeCode == ZfFieldTC::Int16 ||
      TypeCode == ZfFieldTC::Int32 ||
      TypeCode == ZfFieldTC::Int64 ||
      TypeCode == ZfFieldTC::Int128 ||
      TypeCode == ZfFieldTC::UInt8 ||
      TypeCode == ZfFieldTC::UInt16 ||
      TypeCode == ZfFieldTC::UInt32 ||
      TypeCode == ZfFieldTC::UInt64 ||
      TypeCode == ZfFieldTC::UInt128) {
    if (ZuUnlikely(type != AnyNode::Index<AnyNode::String>{}))
      throw ZfCLI_EXCEPT(ZfCLIError::badType(node->source, "integer"));
    const auto &span = node->data<AnyNode::String>();
    using Fmt = ZuFieldProp::CLI::GetNumberFmt<Props>;
    using B = ZuIf<ZuIsBoxed<T>{}, T, ZuBox<ZfFieldTC::Type<TypeCode>>>;
    B v;
    if constexpr (ZuFieldProp::HasEnum<Props>{}) {
      using Map = ZuFieldProp::GetEnum<Props>;
      auto i = Map::s2v(span);
      if (ZuUnlikely(i < 0))
	throw ZfCLI_EXCEPT(ZfCLIError::badEnum<Map>(node->source, span));
      v = B{i};
    } else if constexpr (ZuFieldProp::HasFlags<Props>{}) {
      using Map = ZuFieldProp::GetFlags<Props>;
      using Scan = typename Map::Scan;
      auto r = Scan::eov(span, Fmt::Fmt::FlagsDelim());
      if (ZuUnlikely(r.template p<0>() < 0 ||
          unsigned(r.template p<0>()) != span.length()))
	throw ZfCLI_EXCEPT(ZfCLIError::badEnum<Map>(node->source, span));
      v = B{r.template p<1>().val()};
    } else {
      auto r = [&]() {
	if constexpr (ZuTypeIn<ZuFieldProp::Hex, Props>{})
	  return B::template eov<ZuFmt::Hex<false, typename Fmt::Fmt>>(span);
	else
	  return B::template eov<typename Fmt::Fmt>(span);
      }();
      if (ZuUnlikely(r.template p<0>() < 0 ||
          unsigned(r.template p<0>()) != span.length()))
	throw ZfCLI_EXCEPT(ZfCLIError::badValue(node->source, "integer", span));
      v = r.template p<1>();
    }
    if constexpr (ZuFieldProp::HasRange<Props>{}) {
      auto v_ = v;
      if (ZuUnlikely(!ZfFieldLimit<Props>(v))) {
	using Range = ZuFieldProp::GetRange<Props>;
	throw ZfCLI_EXCEPT(ZfCLIError::badRange(
	    node->source, Range::minimum(), Range::maximum(), v_));
      }
    }
    if constexpr (ZuIsBoxed<T>{})
      return v;
    else
      return T(v.val());
  } else if constexpr (
      TypeCode == ZfFieldTC::Float ||
      TypeCode == ZfFieldTC::Fixed ||
      TypeCode == ZfFieldTC::Decimal) {
    if (ZuUnlikely(type != AnyNode::Index<AnyNode::String>{}))
      throw ZfCLI_EXCEPT(ZfCLIError::badType(node->source, "number"));
    ZuCSpan span = node->data<AnyNode::String>();
    if constexpr (
	TypeCode == ZfFieldTC::Decimal ||
	TypeCode == ZfFieldTC::Fixed) {
      auto r = ZfJSON::eov_Decimal(span);
      if (ZuUnlikely(r.template p<0>() < 0 ||
          unsigned(r.template p<0>()) != span.length()))
	throw ZfCLI_EXCEPT(ZfCLIError::badValue(
	    node->source, "decimal", span));
      auto d = r.template p<1>();
      if (ZuUnlikely(!*d))
	throw ZfCLI_EXCEPT(ZfCLIError::badValue(
	    node->source, "decimal", span));
      if constexpr (ZuFieldProp::HasRange<Props>{}) {
	auto d_ = d;
	if (ZuUnlikely(!ZfFieldLimit<Props>(d))) {
	  using Range = ZuFieldProp::GetRange<Props>;
	  throw ZfCLI_EXCEPT(ZfCLIError::badRange(
	      node->source, Range::minimum(), Range::maximum(), d_));
	}
      }
      if constexpr (TypeCode == ZfFieldTC::Decimal)
	return d;
      else {
	if constexpr (ZuFieldProp::HasNDP<Props>{})
	  return ZuFixed{d, ZuFieldProp::GetNDP<Props>{}};
	else
	  return ZuFixed{d};
      }
    } else {
      auto d = ZfJSON::eov_Float(span);
      if (ZuUnlikely(d.p<0>() < 0 || unsigned(d.p<0>()) != span.length()))
	throw ZfCLI_EXCEPT(ZfCLIError::badValue(
	    node->source, "floating point number", span));
      auto v = d.p<1>();
      if constexpr (ZuFieldProp::HasRange<Props>{}) {
	auto v_ = v;
	if (ZuUnlikely(!ZfFieldLimit<Props>(v))) {
	  using Range = ZuFieldProp::GetRange<Props>;
	  throw ZfCLI_EXCEPT(ZfCLIError::badRange(
	      node->source, Range::minimum(), Range::maximum(), v_));
	}
      }
      return v;
    }
  } else if constexpr (
      TypeCode == ZfFieldTC::Time ||
      TypeCode == ZfFieldTC::DateTime) {
    using Fmt = ZuFieldProp::CLI::GetTimeFmt<Props>;
    if (ZuUnlikely(type != AnyNode::Index<AnyNode::String>{}))
      throw ZfCLI_EXCEPT(ZfCLIError::badType(node->source, "date/time"));
    ZuCSpan span = node->data<AnyNode::String>();
    if constexpr (Fmt::Fmt == ZfCLI::Unix) {
      auto d = ZfJSON::eov_Decimal(span);
      if (ZuUnlikely(d.p<0>() < 0 || unsigned(d.p<0>()) != span.length()))
	throw ZfCLI_EXCEPT(ZfCLIError::badValue(node->source, "date/time", span));
      auto &v = d.p<1>();
      if (ZuUnlikely(!*v))
	throw ZfCLI_EXCEPT(ZfCLIError::badValue(
	    node->source, "date/time", span));
      if constexpr (Fmt::Unit == ZfCLI::MSec) {
	v.value /= 1000;
      } else if constexpr (Fmt::Unit == ZfCLI::USec) {
	v.value /= 1000000;
      } else if constexpr (Fmt::Unit == ZfCLI::NSec) {
	v.value /= 1000000000;
      }
      if constexpr (ZuIs_<T, ZuTime>{})
	return ZuTime{v};
      else
	return ZuDateTime{ZuTime{v}};
    } else if constexpr (Fmt::Fmt == ZfCLI::CSV) {
      auto &fmt = ZmTLS<ZuDateTimeScan::CSV, (int Props::*){}>();
      ZuDateTime v;
      auto n = v.scan(fmt, span);
      if (ZuUnlikely(n < 0 || unsigned(n) != span.length()))
	throw ZfCLI_EXCEPT(ZfCLIError::badValue(node->source, "date/time", span));
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    } else if constexpr (Fmt::Fmt == ZfCLI::FIX) {
      auto &fmt = ZmTLS<ZuDateTimeScan::FIX, (int Props::*){}>();
      ZuDateTime v;
      auto n = v.scan(fmt, span);
      if (ZuUnlikely(n < 0 || unsigned(n) != span.length()))
	throw ZfCLI_EXCEPT(ZfCLIError::badValue(node->source, "date/time", span));
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    } else if constexpr (Fmt::Fmt == ZfCLI::ISO) {
      auto &fmt = ZmTLS<ZuDateTimeScan::ISO, (int Props::*){}>();
      ZuDateTime v;
      auto n = v.scan(fmt, span);
      if (ZuUnlikely(n < 0 || unsigned(n) != span.length()))
	throw ZfCLI_EXCEPT(ZfCLIError::badValue(node->source, "date/time", span));
      if constexpr (ZuIs_<T, ZuTime>{})
	return v.as_time();
      else
	return v;
    }
  } else if constexpr (TypeCode == ZfFieldTC::UDT) {
    return typename As<T>::template Handler<T, Facet>{const_cast<AnyNode *>(node)}.ctor();
  }
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline auto loadValue(AnyNode *node)
{
  if constexpr (!ZfFieldTC::IsVec<TypeCode>{}) {
    if constexpr (TypeCode == ZfFieldTC::UDT) {
      // UDT type - recurse
      return typename As<T>::template Handler<T, Facet>{node}.ctor();
    } else {
      return loadValue_<Facet, Filter, TypeCode, Props, T>(node);
    }
  } else {
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    using Actual = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
    using Elem = ZuIf<
      ElemCode >= ZfFieldTC::Int8 && ElemCode <= ZfFieldTC::UInt128 &&
      bool(ZuIsBoxed<Actual>{}), Actual, ZfFieldTC::Type<ElemCode>>;
    using LoadVec_ = LoadVec<Facet, Filter, ElemCode, Props, Elem>;
    if (!node->has<AnyNode::Array>()) {
      static const NodeArray _;
      return LoadVec_(_);
    }
    const auto &nodes = node->data<AnyNode::Array>();
    for (unsigned i = 0, n = nodes.length(); i < n; i++)
      loadValue_<Facet, Filter, ElemCode, Props, Elem>(nodes[i]);
    return LoadVec_(nodes);
  }
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
inline auto loadValue(ZuPtr<AnyNode> &node)
{
  if constexpr (!ZfFieldTC::IsVec<TypeCode>{}) {
    return loadValue<Facet, Filter, TypeCode, Props, T>(node.ptr());
  } else {
    using Config = ZfCLI::Config<Facet>;
    asArray<Config>(node);
    return loadValue<Facet, Filter, TypeCode, Props, T>(node.ptr());
  }
}

template <typename O, typename Facet>
template <template <typename> class Filter, typename Field>
inline auto AsObject::Handler<O, Facet>::loadField() const
{
  enum { TypeCode = Field::Type::Code };
  using Props = typename Field::Props;
  using T = typename Field::T;
  using R = decltype(
    loadValue<Facet, Filter, TypeCode, Props, T>(ZuDeclVal<AnyNode *>()));
  if (ZuLikely(node->has<AnyNode::Object>())) {
    const auto &object = node->data<AnyNode::Object>();
    ZuCSpan longOpt = ZuFieldProp::CLI::GetLong<Field>{}().cspan();
    if (auto node_ = object.find(longOpt)) {
      auto &child = node_->val();
      constexpr int8_t Flag = ZuFieldProp::CLI::GetFlag<Props>{};
      if constexpr (Flag >= 0) {
	if (ZuUnlikely(child->has<AnyNode::Array>()))
	  throw ZfCLI_EXCEPT(
	    ZfCLIError::multipleValues(child->source));
	return R(true);
      } else if constexpr (TypeCode == ZfFieldTC::UDT) {
	using Format_ = Format<T, Facet>;
	if constexpr (ZuIsSame<Format_, AsObject>{}) {
	  ZeString expansion;
	  if (prefix) expansion << prefix << '.';
	  expansion << longOpt;
	  return typename Format_::template Handler<T, Facet>{
	    child.ptr(), expansion}.ctor();
	} else if constexpr (IsAsArray<Format_>{}) {
	  using Config = ZfCLI::Config<Facet>;
	  if (ZuUnlikely(!asArray<Config>(child)))
	    throw ZfCLI_EXCEPT(
	      ZfCLIError::badType(child->source, "array"));
	  return typename Format_::template Handler<T, Facet>{child.ptr()}.ctor();
	} else
	  return typename Format_::template Handler<T, Facet>{child.ptr()}.ctor();
      }
      else
	return loadValue<Facet, Filter, TypeCode, Props, T>(child);
    }
  }
  if constexpr (ZfFieldTC::IsVec<TypeCode>{}) {
    static const NodeArray _;
    return R(_);
  } else
    return R{Field::deflt()};
}

template <template <typename> class Filter_>
struct Arg0Filter {
  template <typename Field>
  using Filter = ZuBool<
    Filter_<Field>{}() &&
    typename ZuFieldProp::CLI::GetArg<typename Field::Props>{}() == 0>;
};
template <template <typename> class Filter_>
struct OptFilter {
  template <typename Field>
  using Filter = ZuBool<
    Filter_<Field>{}() &&
    (typename ZuFieldProp::CLI::GetArg<typename Field::Props>{}() < 0) &&
    (typename ZuFieldProp::CLI::GetArgs<typename Field::Props>{}() < 0)>;
};

template <template <typename> class Filter_>
struct ArgFilter {
  template <typename Field>
  using Filter = ZuBool<
    Filter_<Field>{}() && (
      typename ZuFieldProp::CLI::GetArg<typename Field::Props>{}() > 0 ||
      typename ZuFieldProp::CLI::GetArgs<typename Field::Props>{}() > 0)>;
};
template <
  typename Facet, template <typename> class Filter,
  typename Quote,
  typename S, typename O>
inline S &save_(S &s_, const O &v) {
  OutStream<S> s{s_};
  using Handler = As<O>::template Handler<O, Facet>;
  Handler::template save<Arg0Filter<Filter>::template Filter, Quote>(s, v, {});
  Handler::template save<OptFilter<Filter>::template Filter, Quote>(s, v, {});
  Handler::template save<ArgFilter<Filter>::template Filter, Quote>(s, v, {});
  return s_;
}

template <typename Facet = ZuFacet::CLI, typename S, typename O>
ZuInline S &save(S &s, const O &v) {
  return save_<Facet, ZfFieldFilter::Save, ShellQuote>(s, v);
}
template <typename Facet = ZuFacet::CLI, typename S, typename O>
ZuInline S &saveUpd(S &s, const O &v) {
  return save_<Facet, ZfFieldFilter::Upd, ShellQuote>(s, v);
}
template <typename Facet = ZuFacet::CLI, typename S, typename O>
ZuInline S &saveDel(S &s, const O &v) {
  return save_<Facet, ZfFieldFilter::Del, ShellQuote>(s, v);
}

// constructs argv[] array for execv*(), etc.
struct OutArgv {

  Argv				argv;
  Argv_C			argv_c_;

  void finish() {
    unsigned argc = argv.length();
    argv_c_.length(argc + 1);
    for (unsigned i = 0; i < argc; i++)
      if (!(argv_c_[i] = argv[i].data())) argv_c_[i] = "";
    argv_c_[argc] = nullptr;
  }

  int argc() const { return argv.length(); }
  const char *const *argv_c() const { return &argv_c_[0]; }
};

template <typename Facet = ZuFacet::CLI, typename O>
inline OutArgv &saveArgv(OutArgv &out, const O &v) {
  save_<Facet, ZfFieldFilter::Save, RawQuote>(out.argv, v);
  out.finish();
  return out;
}
template <typename Facet = ZuFacet::CLI, typename O>
inline OutArgv &saveArgvUpd(OutArgv &out, const O &v) {
  save_<Facet, ZfFieldFilter::Upd, RawQuote>(out.argv, v);
  out.finish();
  return out;
}
template <typename Facet = ZuFacet::CLI, typename O>
inline OutArgv &saveArgvDel(OutArgv &out, const O &v) {
  save_<Facet, ZfFieldFilter::Del, RawQuote>(out.argv, v);
  out.finish();
  return out;
}

template <typename O, typename Facet = ZuFacet::CLI>
auto handler(const AnyNode *root) {
  return typename As<O>::template Handler<O, Facet>{root};
}

ZtEnumNS(ZfAPI, OptType, int8_t,
  Arg = 0,	// value
  Args,		// value...
  Option,	// -x value
  Flag);	// -x
// expansion for an individual option
// - i.e. fully-qualified field ID
ZuDerive(Expansion,
  (ZtString<ZtStringHeapID<"ZfCLI.Expansion", ZtStringSharded<true>>>));
struct Option {
  OptType::T	type;
  Expansion	expansion;
};
template <typename O, typename Facet>
constexpr unsigned nOptions_() {
  using AllFields = ZuFields<O, Facet>;
  using SaveFields = ZuTypeGrep<ZfFieldFilter::Save, AllFields>;
  unsigned n = 0;
  ZuUnroll::all<SaveFields>([&n]<typename Field>() {
    using Props = typename Field::Props;
    enum { TypeCode = Field::Type::Code };
    constexpr int8_t Arg = ZuFieldProp::CLI::GetArg<Props>{};
    constexpr int8_t Args = ZuFieldProp::CLI::GetArgs<Props>{};
    constexpr int8_t Opt = ZuFieldProp::CLI::GetOpt<Props>{};
    constexpr int8_t Flag = ZuFieldProp::CLI::GetFlag<Props>{};
    if constexpr (
	Field::Type::Code == ZfFieldTC::UDT &&
	ZuIsSame<Format<typename Field::T, Facet>, AsObject>{})
      n += nOptions_<typename Field::T, Facet>();
    else if constexpr (Arg >= 0 || Args >= 0 || Opt >= 0 || Flag >= 0)
      ++n;
  });
  return n;
}
template <typename O, typename Facet>
constexpr unsigned nOptions() {
  return ZuIntrin::log2(nOptions_<O, Facet>());
}
template <typename O, typename Facet, typename Hash>
void initOptions(Hash &hash, ZuCSpan prefix = {}) {
  using AllFields = ZuFields<O, Facet>;
  using SaveFields = ZuTypeGrep<ZfFieldFilter::Save, AllFields>;
  ZuUnroll::all<SaveFields>([&hash, &prefix]<typename Field>() {
    using Props = typename Field::Props;
    enum { TypeCode = Field::Type::Code };
    constexpr int8_t Arg = ZuFieldProp::CLI::GetArg<Props>{};
    constexpr int8_t Args = ZuFieldProp::CLI::GetArgs<Props>{};
    constexpr int8_t Opt = ZuFieldProp::CLI::GetOpt<Props>{};
    constexpr int8_t Flag = ZuFieldProp::CLI::GetFlag<Props>{};
    unsigned n = prefix.length();
    if (n) ++n;
    ZuCSpan longOpt = ZuFieldProp::CLI::GetLong<Field>{}().cspan();
    n += longOpt.length() + 1; // +1 for null terminator
    auto expansion = ZmScratch(char, n, Expansion::VHeap);
    if (prefix) expansion << prefix << '.';
    expansion << longOpt;
    if constexpr (
	Field::Type::Code == ZfFieldTC::UDT &&
	ZuIsSame<Format<typename Field::T, Facet>, AsObject>{})
      initOptions<typename Field::T, Facet>(hash, expansion);
    else if constexpr (Arg >= 0) {
      ZuAssert(Args < 0 && Opt < 0 && Flag < 0);
      hash.add(Arg, Option{OptType::Arg, expansion});
    } else if constexpr (Args >= 0) {
      ZuAssert(Arg < 0 && Opt < 0 && Flag < 0);
      hash.add(Args, Option{OptType::Args, expansion});
    } else if constexpr (Opt >= 0) {
      ZuAssert(Arg < 0 && Args < 0 && Flag < 0);
      hash.add(Opt, Option{OptType::Option, expansion});
    } else if constexpr (Flag >= 0) {
      ZuAssert(Arg < 0 && Args < 0 && Opt < 0);
      hash.add(Flag, Option{OptType::Flag, expansion});
    }
  });
}
template <typename O, typename Facet>
using Options = ZmLHashKV<char, Option,
  ZmLHashStatic<nOptions<O, Facet>(),
    ZmLHashLocal<>>>;
template <typename O, typename Facet = ZuFacet::CLI>
struct Parser {
  ZuPtr<AnyNode> root;		// root node
  unsigned argc = 0;		// number of positional arguments

private:
  Options<O, Facet> options;	// options (built from fields by initOptions())
  ZuCSpan key;			// current key (while reading)
  ZeString source;		// current option/argument diagnostic context
  bool opt = false;		// reading option key with single arg
  bool args = false;		// reading vector value of multiple args
  bool eoo = false;		// no more options following --

  template <typename U>
  static OptType::T longOptType_(ZuCSpan key_, ZuCSpan prefix = {}) {
    OptType::T type = -1;
    using AllFields = ZuFields<U, Facet>;
    using SaveFields = ZuTypeGrep<ZfFieldFilter::Save, AllFields>;
    ZuUnroll::all<SaveFields>([&type, &key_, &prefix]<typename Field>() {
      if (type >= 0) return;
      using Props = typename Field::Props;
      enum { TypeCode = Field::Type::Code };
      constexpr int8_t Arg = ZuFieldProp::CLI::GetArg<Props>{};
      constexpr int8_t Args = ZuFieldProp::CLI::GetArgs<Props>{};
      if constexpr (Arg < 0 && Args < 0) {
	ZuCSpan longOpt = ZuFieldProp::CLI::GetLong<Field>{}().cspan();
	unsigned n = prefix.length();
	if (n) ++n;
	n += longOpt.length();
	auto expansion = ZmScratch(char, n, Expansion::VHeap);
	if (prefix) expansion << prefix << '.';
	expansion << longOpt;
	if constexpr (
	    TypeCode == ZfFieldTC::UDT &&
	    ZuIsSame<Format<typename Field::T, Facet>, AsObject>{}) {
	  type = longOptType_<typename Field::T>(key_, expansion);
	} else if (key_ == expansion) {
	  if constexpr (TypeCode == ZfFieldTC::Bool ||
	      ZuIsSame<ZuDecay<typename Field::T>, bool>{})
	    type = OptType::Flag;
	  else
	    type = OptType::Option;
	}
      }
    });
    return type;
  }

public:
  Parser() : root{newNode<AnyNode::Object>({})} {
    initOptions<O, Facet>(options);
  }

private:
  void addNode(ZuCSpan key_, ZuSpan<char> val, ZuCSpan source_) {
    // scan within the key, descend to the leaf node, set the value
    ZuPtr<AnyNode> *slot = &root;
    while (key_) {
      auto sk = scanKey(key_); // {offset, span}
      auto offset = sk.p<0>();
      if (offset < 0) throw ZfCLI_EXCEPT(ZfCLIError::badKey(source_));
      key_.offset(offset);
      const auto &keyPart = sk.p<1>();
      if (!keyPart)
	slot = push(*slot);
      else {
	auto c = keyPart[0];
	if (c >= '0' && c <= '9')
	  slot = elem(*slot, ZuBox<unsigned>(keyPart));
	else
	  slot = field(*slot, keyPart);
      }
      if (!slot) throw ZfCLI_EXCEPT(ZfCLIError::badKey(source_));
    }
    if (!string(*slot, val, source_))
      throw ZfCLI_EXCEPT(ZfCLIError::badKey(source_));
  }

public:
  bool hasKey(ZuCSpan key_) {
    // scan within the key, descend to the leaf node
    AnyNode *node = root;
    while (key_) {
      auto sk = scanKey(key_); // {offset, span}
      auto offset = sk.p<0>();
      if (offset < 0) return false;
      key_.offset(offset);
      const auto &keyPart = sk.p<1>();
      if (node->has<AnyNode::Array>()) {
	if (!keyPart) return false;
	auto c = keyPart[0];
	if (c < '0' || c > '9') return false;
	const auto &array = node->data<AnyNode::Array>();
	auto index = ZuBox<unsigned>(keyPart);
	if (index >= array.length()) return false;
	node = array[index];
      } else if (node->has<AnyNode::Object>()) {
	const auto &object = node->data<AnyNode::Object>();
	// can't use findVal() here due to ZuPtr being move-only
	auto node_ = object.find(keyPart);
	node = node_ ? node_->val().ptr() : nullptr;
      } else
	return false;
      if (!node) return false;
    }
    return node->has<AnyNode::String>();
  }

  // reset parser state
  void reset() {
    root = newNode<AnyNode::Object>({});
    key = {};
    source.length_(0);
    argc = 0;
    opt = false;
    args = false;
    eoo = false;
  }

  // scan a single argument; throws ZeException on invalid input
  void scanArg(ZuSpan<char> arg) {
    unsigned n = arg.length();
    ZuSpan<char> val;

    if (!opt && !eoo && n >= 2 && arg[0] == '-') {
      if (n >= 3 && arg[1] == '-') {
	// --x...
	arg.offset(2);
	n -= 2;
	auto p = eok(arg);
	if (!p) {
	  ZeString option{"--"};
	  option << arg;
	  throw ZfCLI_EXCEPT(ZfCLIError::badOption(option));
	}
	if (p > 0) {
	  // --x=val
	  key = {&arg[0], uint64_t(p)};
	  if (ZuUnlikely(longOptType_<O>(key) < 0)) {
	    ZeString option{"--"};
	    option << key;
	    throw ZfCLI_EXCEPT(ZfCLIError::unrecognizedOption(option));
	  }
	  source << "option '--" << key << '\'';
	  ++p;
	  val = {&arg[p], uint64_t(n - p)};
	} else {
	  // --x
	  switch (longOptType_<O>(arg)) {
	    default: {
	      ZeString option{"--"};
	      option << arg;
	      throw ZfCLI_EXCEPT(ZfCLIError::unrecognizedOption(option));
	    }
	    case OptType::Option:
	      key = arg;
	      source << "option '--" << key << '\'';
	      opt = true;
	      return;
	    case OptType::Flag:
	      key = arg;
	      source << "option '--" << key << '\'';
	      static char true_[] = "true";
	      val = {true_, 4};
	      break;
	  }
	}
	addNode(key, val, source);
	source.length_(0);
	return;
      }

      // -x
      if (n == 2 && arg[1] == '-') {
	// -- end of options marker
	eoo = true;
	return;
      }
      for (unsigned i = 1; i < n; i++) {
	auto option = options.find(arg[i]);
	if (!option) {
	  char option_[] = {'-', arg[i]};
	  throw ZfCLI_EXCEPT(
	    ZfCLIError::unrecognizedOption({option_, unsigned(2)}));
	}
	switch (option->template p<1>().type) {
	  default: {
	    char option_[] = {'-', arg[i]};
	    throw ZfCLI_EXCEPT(
	      ZfCLIError::badOption({option_, unsigned(2)}));
	  }
	  case OptType::Option:
	    if (i + 1 < n) {
	      ZeString option_;
	      option_ << '-' << arg[i];
	      throw ZfCLI_EXCEPT(ZfCLIError::badOption(option_));
	    }
	    key = option->template p<1>().expansion;
	    source << "option '-" << arg[i] << '\'';
	    opt = true;
	    break;
	  case OptType::Flag:
	    key = option->template p<1>().expansion;
	    source << "option '-" << arg[i] << '\'';
	    val = {};
	    addNode(key, val, source);
	    source.length_(0);
	    break;
	}
      }
      return;
    }
    // plain arg
    if (opt || args) {
      // option value
      if (opt) opt = false;
      val = arg;
      addNode(key, val, source);
      source.length_(0);
      return;
    }
    // plain positional arg
    auto option = options.find(argc++);
    if (!option) return;
    source << "argument " << ZuBoxed(argc - 1);
    switch (option->template p<1>().type) {
      default:
	throw ZfCLI_EXCEPT(ZfCLIError::badKey(source));
      case OptType::Arg:
	key = option->template p<1>().expansion;
	val = arg;
	break;
      case OptType::Args:
	key = option->template p<1>().expansion;
	val = arg;
	args = true;
	break;
    }
    addNode(key, val, source);
    source.length_(0);
  }

  // scan arguments; throws ZeException on invalid input
  template <typename Argv>
  void scanArgv(const Argv &argv) {
    for (unsigned i = 0, n = argv.length(); i < n; i++)
      scanArg(argv[i]);
    if (opt) {
      ZeString option;
      option << source;
      auto span = option.cspan();
      span.offset(sizeof("option '") - 1);
      span = {span.data(), span.length() - 1};
      throw ZfCLI_EXCEPT(ZfCLIError::missingValue(span));
    }
  }
};

// scan CLI from Zrl or script file; splits a CLI into mutable argv
struct InCLI {
  using Span = ZuSpan<char>;

  SpanArgv		argv;
  ZuCSpan		in, out;		// < > >> redirects
  bool			append = false;		// >> if true

  InCLI(Span span) {
    bool in_ = false, out_ = false;
    unsigned redirectPos = 0;
    while (span) {
      auto [o, i, c] = eos(span, argv.length());
      Span arg(&span[0], o);
      span.offset(i);
      bool eol = false;
      bool redirect = false;
      switch (arg[0]) {
	case '<':
	  if (in_ || out_) break;
	  in_ = true;
	  redirect = true;
	  redirectPos = argv.length();
	  arg.offset(1);
	  break;
	case '>':
	  if (in_ || out_) break;
	  out_ = true;
	  redirect = true;
	  redirectPos = argv.length();
	  arg.offset(1);
	  if (arg && arg[0] == '>') { append = true; arg.offset(1); }
	  break;
	case '#':
	  eol = true;
	  break;
	case ';':
	  eol = true;
	  break;
	default:
	  break;
      }
      if (eol) break;
      if (in_) {
	if (redirect && !arg) continue;
	in_ = false;
	in = arg;
      } else if (out_) {
	if (redirect && !arg) continue;
	out_ = false;
	out = arg;
      } else
	argv.push(arg);
    }
    if (in_ || out_)
      throw ZfCLI_EXCEPT(
	ZfCLIError::missingRedirect(redirectPos, out_));
  }
};

// scan argc, argv from platform
// - MutableArgv is used to configure argv[] mutability
template <bool MutableArgv = ZfCLI_MutableArgv>
struct InArgv {
  using Arg_ = ZuIf<MutableArgv, ZuSpan<char>, Arg>;
  using Argv_ = ZuIf<MutableArgv, SpanArgv, Argv>;

  Argv_			argv;

  InArgv(unsigned argc, const char *const *argv_) {
    argv.size(argc);
    for (unsigned i = 0; i < argc; i++) argv.push(const_cast<char *>(argv_[i]));
  }
};

// load object from argv
// - returns argc of positional arguments (other than the options)
// - throws ZeException on invalid options, arguments or values
template <typename Facet = ZuFacet::CLI, typename O, typename Argv>
int load(O &o, const Argv &argv) {
  Parser<O, Facet> parser;
  parser.scanArgv(argv);
  ZfCLI::handler<O, Facet>(parser.root).load(o);
  return parser.argc;
}

// load object from platform argc, argv
template <typename Facet = ZuFacet::CLI, typename O>
int load(O &o, int argc, const char *const *argv) {
  InArgv<> in(argc, argv);
  return load(o, in.argv);
}

} // ZfCLI

#endif /* ZfCLI_HH */
