//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZtStruct CLI load/save
// - compile-time formatting
// - in-place overwrite decoding of quoting, base64, base32, hex etc. 

// the many and varied ways of flattening structures into CLI args, sigh...
// - built on ZtURI, which does the same job for HTTP URI queries
// - see ZtURI.hh for an explanation of Member/Array/List formats

// both arrays and nested objects can also be embedded as JSON

// recap: the top-level is always an object whose fields are ZtStruct-defined;
// individual fields are one of:
// - a primitive type with non-UDT typecode (String, Int*, etc.)
//   - per-field StringFmt/BytesFmt/NumberFmt/TimeFmt control the format
// - a vector type with vector typecode (vector of primitive types)
//   - no recursion is possible because the element type is primitive
//   - per-field StringFmt/BytesFmt/NumberFmt/TimeFmt control the element format
// - a nested UDT type dispatched via the ZtCLI_Fmt() mechanism:
//   - AsString (default for any type without ZtStruct-defined fields)
//     - by default operator << is used for saving and the type is expected
//       to construct itself from a string (ZuCSpan passed to the constructor)
//     - a custom load/save handler can be defined and bound by
//       declaring ZtCLI_StringFmt(T *) in T's namespace (see AsString)
//   - AsObject (default for any type with ZtStruct-defined fields)
//   - AsArray (ZtCLI::AsArray ZtCLI_Fmt(T *) must be declared in T's namespace)
//   - AsJSON (ZtCLI::AsJSON ZtCLI_Fmt(T *) must be declared in T's namespace)
//     - the format is delegated to ZtJSON and any JSON formatting
//       defined for the field is used
//     - the same mapping as for ZtCLI is used (CLI if canonical), so
//       JSON-in-CLI must be configured via the CLI mapping not the
//       canonical JSON mapping

#ifndef ZtCLI_HH
#define ZtCLI_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <math.h>

#include <zlib/ZuDecimal.hh>
#include <zlib/ZuMArray.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>

#include <zlib/ZmRBTree.hh>
#include <zlib/ZmLHash.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtStruct.hh>
#include <zlib/ZtJSON.hh>
#include <zlib/ZtURI.hh>
#include <zlib/ZtLocalString.hh>

#ifndef ZtCLI_MutableArgv 
// the argv parameter to main() is mutable on modern operating systems and
// C libraries including Linux, Windows and MacOS, despite the modern
// preference for declaring it as const char *const * instead of the
// original K&R char **

// even on hardened platforms where argv[0] is read-only, argv[1..] is
// read-write - and this library will never mutate argv[0] unless a
// string or bytes field is bound to it with base64/32 or hex encoding,
// which in practice is never done

// use -DZtCLI_MutableArgv=0 if argv[N] where N > 0 is actually in
// read-only memory on a particularly stubborn platform
#define ZtCLI_MutableArgv 1
#endif

ZuStructFacet(CLI); // canonical CLI facet, others can be defined

// --- configuration (per-mapping)

// NTP (named template parameters):
//
// ZtCLIConfig(CLI,			// configure canonical CLI
//   ZtCLI::ArrayFmt<ZtCLI::Delimited,	// array format is delimited
//     ZtCLI::Delimiter<','>>>);	// arrays are delimited by ,

namespace ZtCLI { using namespace ZtURI; }

// NTP defaults
struct ZtCLI_DefltConfig {
  enum {
    ObjectFmt = ZtCLI::Member,	// CLI only supports Member
    ArrayFmt = ZtCLI::Bare,	// CLI only supports Bare and Delimited
    Annotated = 0,		// CLI does not support Annotated=1
    Wrapped = 0,		// CLI does not support Wrapped=1
    Delimiter = ','
  };
};

// ZtCLI_ArrayFmt<...> - configure array format
template <unsigned Fmt, typename NTP = ZtCLI_DefltConfig>
struct ZtCLI_ArrayFmt : public ZtURI_ArrayFmt<Fmt, NTP> {
  ZuAssert(Fmt == ZtCLI::Bare || Fmt == ZtCLI::Delimited);
};

// ZtCLI_Delimiter<...> - configure array element delimiter character
template <char _, typename NTP = ZtCLI_DefltConfig>
using ZtCLI_Delimiter = ZtURI_Delimiter<_, NTP>;

ZtCLI_DefltConfig ZtCLI_Config(...); // default

namespace ZtCLI {

// built-in size for on-heap argument arrays
constexpr unsigned BuiltinSize = 16;

// resolve configuration (per field mapping)
// - E.g. ZtCLI::Config<Mapping>::ArrayFmt
template <typename Facet>
using Config = decltype(ZtCLI_Config(ZuDeclVal<Facet *>()));

}

// ZtCLIConfig(Facet, Config)
// - configure CLI for specific facet
// - must be used in top-level namespace
#define ZtCLIConfig(Facet, Config) \
  namespace ZtCLI { \
    ZuPP_Strip(Config) ZtCLI_Config(ZuFacet::Facet *); \
  }

namespace ZtCLI {

ZuDerive(QuoteBuf, // temporary on-stack string buffer for quoting
  (ZtString<
    ZtStringBuiltin<128,
      ZtStringHeapID<"ZtCLI.Quote",
	ZtStringSharded<true>>>>));

using AsStringDeflt = ZtURI::AsStringDeflt;

} // ZtCLI

ZtCLI::AsStringDeflt ZtCLI_StringFmt(...);

namespace ZuFieldProp::CLI {

using namespace ZuFieldProp::URI;

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

namespace ZtCLI {

// --- input functions

// this implementation intentionally assumes UTF8

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
ZtExtern ZuTuple<int, int, char> eos(ZuSpan<char>);

// find end of key '='
ZtExtern int eok(ZuCSpan);

// on-heap argument types used to build and parse argv[] for interoperating
// with main, execv*, etc.
ZuDerive(Arg,
  (ZtString<ZtStringBuiltin<32, ZtStringHeapID<"ZtCLI.Arg">>>));
ZuDerive(Argv,
  (ZtBuiltin<ZtArray<Arg, ZtArrayHeapID<"ZtCLI.Argv">>, BuiltinSize>));
ZuDerive(SpanArgv,
  (ZtBuiltin<ZtArray<ZuSpan<char>, ZtArrayHeapID<"ZtCLI.Argv">>, BuiltinSize>));
ZuDerive(Argv_C,
  (ZtBuiltin<ZtArray<const char *, ZtArrayHeapID<"ZtCLI.Argv">>, BuiltinSize>));

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

using AsDeflt = AsObject;

} // ZtCLI

ZtCLI::AsDeflt ZtCLI_Fmt(...);	// default

namespace ZtCLI {

template <typename O>
using As = decltype(ZtCLI_Fmt(ZuDeclVal<O *>()));

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

// save/load handler for object-formatted types
// - object-formatted in CLI query strings means multiple individual fields
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

    Handler(const AnyNode *node_) : node{node_} { }

    template <template <typename> class Filter, typename Field>
    auto loadField() const;

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
	  Field::set(o, loadField<ZtFieldFilter::Load, Field>());
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

// save/load handler for array-formatted types
template <unsigned ElemCode, typename ElemProps>
struct AsArray {
  template <typename O_, typename Facet>
  struct Handler {
    using O = O_;

    using Config = ZtCLI::Config<Facet>;

    // the top-level cannot be AsArray, so prefix must be non-null
    template <
      template <typename> class Filter, typename Quote,
      typename Props = ZuTypeList<>,
      typename S>
    static void save(S &s, const O &o, ZuCSpan prefix) {
      unsigned n = ZuTraits<O>::length(o);
      if constexpr (Config::ArrayFmt == Bare) {
	// append array indices to prefix
	unsigned nestedSize = prefix.length();
	auto nested_ = ZmAlloc(char, nestedSize);
	ZuStream nested(ZuSpan<char>(&nested_[0], nestedSize));
	nested << prefix;
	auto nestedLen =
	  nested ? unsigned(&nested[0] - &nested_[0]) : nestedSize;
	prefix = ZuCSpan(&nested_[0], nestedLen);
	for (unsigned i = 0; i < n; i++)
	  saveValue<Facet, Filter, Quote, ElemCode, ElemProps>(s, o[i], prefix);
      } else { // Delimited - no nesting is possible, use AsString
	using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
	using AsString_ = decltype(ZtCLI_StringFmt(ZuDeclVal<Elem *>()));
	using Handler_ = typename AsString_::template Handler<Quote, Elem>;
	s.template out<Props>(prefix);
	for (unsigned i = 0; i < n; i++) {
	  if (i) s << char(Config::Delimiter);
	  Handler_::save(s, o[i]);
	}
      }
    }

    AnyNode	*node;

    Handler(AnyNode *node_) : node{node_} { }

    using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
    using LoadVec_ =
      LoadVec<Facet, ZtFieldFilter::Load, ElemCode, ElemProps, Elem>;
    template <typename ...Args>
    O ctor(Args &&...args) const {
      if (ZuUnlikely(!node->has<AnyNode::Array>()))
	return O(ZuFwd<Args>(args)...);
      return O(
	ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
    }
    template <typename ...Args>
    void new_(void *o, Args &&...args) const {
      if (ZuUnlikely(!node->has<AnyNode::Array>()))
	new (o) O(ZuFwd<Args>(args)...);
      else
	new (o) O(
	  ZuFwd<Args>(args)..., LoadVec_(node->data<AnyNode::Array>()));
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

// save/load handler for string-formatted types
struct AsString {
  template <typename O_, typename>
  struct Handler {
    using O = O_;

    using AsString_ = decltype(ZtCLI_StringFmt(ZuDeclVal<O *>()));
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
      if (!node->has<AnyNode::String>()) return {};
      return node->data<AnyNode::String>();
    }

    O ctor() const { return Handler_::load(span()); }
    void new_(void *o) const { new (o) O(Handler_::load(span())); }
    void load(O &o) const { o = Handler_::load(span()); }
    void update(O &o) const { o = Handler_::load(span()); }
  };
};

// save/load handler for JSON-formatted types
struct AsJSON {
  template <typename O_, typename Facet>
  struct Handler : public ZtURI::AsJSON::Handler<O_, Facet> {
    using O = O_;
    using Base = ZtURI::AsJSON::Handler<O, Facet>;
    using Base::Base;
    using typename Base::Handler_;

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
  };
};

// uses ZtURI::saveValue_()

template <
  typename Facet, template <typename> class Filter,
  typename Quote, unsigned TypeCode, typename Props,
  typename S, typename T_>
inline void saveValue(S &s, const T_ &v, ZuCSpan prefix)
{
  using T = ZuDecay<T_>;
  using Config = ZtCLI::Config<Facet>;
  if constexpr (!ZtFieldTC::IsVec<TypeCode>{}) {
    if constexpr (TypeCode == ZtFieldTC::UDT) {
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
    enum { ElemCode = ZtFieldTC::Elem<TypeCode>{} };
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
  using Config = ZtCLI::Config<Facet>;

  // append long option name to prefix (copy on stack)
  ZuCSpan longOpt = ZuFieldProp::CLI::GetLong<Field>{}().cspan();
  unsigned nestedSize = 0;
  if (ZuLikely(!prefix))
    nestedSize = longOpt.length();
  else
    nestedSize =
      prefix.length() + longOpt.length() + 1 + (Config::ObjectFmt == Array);
  auto nested_ = ZmAlloc(char, nestedSize);
  ZuStream nested(ZuSpan<char>(&nested_[0], nestedSize));
  if (prefix) nested << prefix << '.';
  nested << longOpt;
  auto nestedLen = nested ? unsigned(&nested[0] - &nested_[0]) : nestedSize;
  prefix = ZuCSpan(&nested_[0], nestedLen);

  saveValue<Facet, Filter, Quote, Type::Code, Props>(
    s, Field::get(o), prefix);
}

// uses ZtURI::loadValue_()

// uses ZtURI::loadValue()

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
      if constexpr (Flag >= 0)
	return R(true);
      else if constexpr (TypeCode == ZtFieldTC::Bool) {
	if (child->has<AnyNode::String>())
	  return R(ZtScanBool(child->data<AnyNode::String>()));
	return R(true);
      }
      else if constexpr (TypeCode == ZtFieldTC::UDT)
	return typename As<T>::template Handler<T, Facet>{child.ptr()}.ctor();
      else
	return loadValue<Facet, Filter, TypeCode, Props, T>(child);
    }
  }
  if constexpr (ZtFieldTC::IsVec<TypeCode>{}) {
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
  return save_<Facet, ZtFieldFilter::Save, ShellQuote>(s, v);
}
template <typename Facet = ZuFacet::CLI, typename S, typename O>
ZuInline S &saveUpd(S &s, const O &v) {
  return save_<Facet, ZtFieldFilter::Upd, ShellQuote>(s, v);
}
template <typename Facet = ZuFacet::CLI, typename S, typename O>
ZuInline S &saveDel(S &s, const O &v) {
  return save_<Facet, ZtFieldFilter::Del, ShellQuote>(s, v);
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
ZuInline OutArgv &saveArgv(OutArgv &out, const O &v) {
  save_<Facet, ZtFieldFilter::Save, RawQuote>(out.argv, v);
  out.finish();
  return out;
}
template <typename Facet = ZuFacet::CLI, typename O>
ZuInline OutArgv &saveArgvUpd(OutArgv &out, const O &v) {
  save_<Facet, ZtFieldFilter::Upd, RawQuote>(out.argv, v);
  out.finish();
  return out;
}
template <typename Facet = ZuFacet::CLI, typename O>
ZuInline OutArgv &saveArgvDel(OutArgv &out, const O &v) {
  save_<Facet, ZtFieldFilter::Del, RawQuote>(out.argv, v);
  out.finish();
  return out;
}

template <typename O, typename Facet = ZuFacet::CLI>
auto handler(const AnyNode *root) {
  return typename As<O>::template Handler<O, Facet>{root};
}

ZtEnumNS(OptType, int8_t,
  Arg = 0,	// value
  Args,	// value...
  Option,	// -x value
  Flag);	// -x
// expansion for an individual option
// - i.e. fully-qualified field ID
ZuDerive(Expansion,
  (ZtString<ZtStringHeapID<"ZtCLI.Expansion", ZtStringSharded<true>>>));
struct Option {
  OptType::T	type;
  Expansion	expansion;
};
template <typename O, typename Facet>
constexpr unsigned nOptions_() {
  using AllFields = ZuFields<O, Facet>;
  using LoadFields = ZuTypeGrep<ZtFieldFilter::Load, AllFields>;
  unsigned n = 0;
  ZuUnroll::all<LoadFields>([&n]<typename Field>() {
    using Props = typename Field::Props;
    enum { TypeCode = Field::Type::Code };
    constexpr int8_t Arg = ZuFieldProp::CLI::GetArg<Props>{};
    constexpr int8_t Args = ZuFieldProp::CLI::GetArgs<Props>{};
    constexpr int8_t Opt = ZuFieldProp::CLI::GetOpt<Props>{};
    constexpr int8_t Flag = ZuFieldProp::CLI::GetFlag<Props>{};
    if constexpr (Arg >= 0 || Args >= 0 || Opt >= 0 || Flag >= 0) ++n;
    if constexpr (Field::Type::Code == ZtFieldTC::UDT)
      n += nOptions_<typename Field::T, Facet>();
  });
  return n;
}
template <typename O, typename Facet>
constexpr unsigned nOptions() {
  unsigned n = nOptions_<O, Facet>();
  return n <= 1 ? 1 : ((sizeof(n)<<3) - ZuIntrin::clz(n - 1));
}
template <typename O, typename Facet, typename Hash>
void initOptions(Hash &hash, ZuCSpan prefix = {}) {
  using AllFields = ZuFields<O, Facet>;
  using LoadFields = ZuTypeGrep<ZtFieldFilter::Load, AllFields>;
  ZuUnroll::all<LoadFields>([&hash, &prefix]<typename Field>() {
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
    auto expansion = ZtLocalString(Expansion, n);
    if (prefix) expansion << prefix << '.';
    expansion << longOpt;
    if constexpr (Arg >= 0) {
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
    if constexpr (Field::Type::Code == ZtFieldTC::UDT)
      initOptions<typename Field::T, Facet>(hash, expansion.cspan());
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
  bool opt = false;		// reading option key with single arg
  bool args = false;		// reading vector value of multiple args
  bool eoo = false;		// no more options following --

  template <typename U>
  static OptType::T longOptType_(ZuCSpan key_, ZuCSpan prefix = {}) {
    OptType::T type = -1;
    using AllFields = ZuFields<U, Facet>;
    using SaveFields = ZuTypeGrep<ZtFieldFilter::Save, AllFields>;
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
	auto expansion = ZtLocalString(Expansion, n);
	if (prefix) expansion << prefix << '.';
	expansion << longOpt;
	if (key_ == expansion) {
	  if constexpr (TypeCode == ZtFieldTC::Bool ||
	      ZuIsSame<ZuDecay<typename Field::T>, bool>{})
	    type = OptType::Flag;
	  else
	    type = OptType::Option;
	}
	if constexpr (TypeCode == ZtFieldTC::UDT) {
	  if (type < 0)
	    type = longOptType_<typename Field::T>(key_, expansion.cspan());
	}
      }
    });
    return type;
  }

public:
  Parser() : root{newNode<AnyNode::Object>()} {
    initOptions<O, Facet>(options);
  }

private:
  bool addNode(ZuCSpan key_, ZuSpan<char> val) {
    // scan within the key, descend to the leaf node, set the value
    ZuPtr<AnyNode> *slot = &root;
    while (key_) {
      auto sk = scanKey(key_); // {offset, span}
      auto offset = sk.p<0>();
      if (offset < 0) return false;
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
      if (!slot) return false;
    }
    return string(*slot, val);
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
	node = array[ZuBox<unsigned>(keyPart)];
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
    root = newNode<AnyNode::Object>();
    key = {};
    argc = 0;
    opt = false;
    args = false;
    eoo = false;
  }

  // scan a single argument
  bool scanArg(ZuSpan<char> arg) {
    unsigned n = arg.length();
    ZuSpan<char> val;

    if (!opt && !eoo && n >= 2 && arg[0] == '-') {
      if (n >= 3 && arg[1] == '-') {
	// --x...
	arg.offset(2);
	n -= 2;
	auto p = eok(arg);
	if (!p) return false;
	if (p > 0) {
	  // --x=val
	  key = {&arg[0], uint64_t(p)};
	  if (ZuUnlikely(longOptType_<O>(key) < 0)) return false;
	  if (++p >= n) return false;
	  val = {&arg[p], uint64_t(n - p)};
	} else {
	  // --x
	  switch (longOptType_<O>(arg)) {
	    default:
	      return false;
	    case OptType::Option:
	      key = arg;
	      opt = true;
	      return true;
	    case OptType::Flag:
	      key = arg;
	      static char true_[] = "1";
	      val = {true_, 1};
	      break;
	  }
	}
	return addNode(key, val);
      }

      // -x
      if (n == 2 && arg[1] == '-') {
	// -- end of options marker
	eoo = true;
	return true;
      }
      for (unsigned i = 1; i < n; i++) {
	auto option = options.find(arg[i]);
	if (!option) return false;
	switch (option->template p<1>().type) {
	  default:
	    return false;
	  case OptType::Option:
	    if (opt) return false;
	    key = option->template p<1>().expansion;
	    opt = true;
	    break;
	  case OptType::Flag:
	    key = option->template p<1>().expansion;
	    val = {};
	    if (!addNode(key, val)) return false;
	    break;
	}
      }
      return true;
    }
    // plain arg
    if (opt || args) {
      // option value
      if (opt) opt = false;
      val = arg;
      return addNode(key, val);
    }
    // plain positional arg
    auto option = options.find(argc++);
    if (!option) return true;
    switch (option->template p<1>().type) {
      default:
	return false;
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
    return addNode(key, val);
  }

  // scan arguments
  template <typename Argv>
  bool scanArgv(const Argv &argv) {
    for (unsigned i = 0, n = argv.length(); i < n; i++)
      if (!scanArg(argv[i])) return false;
    return !opt && !args;
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
    while (span) {
      auto [o, i, c] = eos(span);
      Span arg(&span[0], o);
      span.offset(i);
      bool eol = false;
      switch (arg[0]) {
	case '<':
	  if (in_ || out_) break;
	  in_ = true;
	  arg.offset(1);
	  break;
	case '>':
	  if (in_ || out_) break;
	  out_ = true;
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
	in_ = false;
	in = arg;
      } else if (out_) {
	out_ = false;
	out = arg;
      } else
	argv.push(arg);
    }
  }
};

// scan argc, argv from platform
// - MutableArgv is used to configure argv[] mutability
template <bool MutableArgv = ZtCLI_MutableArgv>
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
// - returns -1 on parse error
template <typename Facet = ZuFacet::CLI, typename O, typename Argv>
int load(O &o, const Argv &argv) {
  Parser<O, Facet> parser;
  if (!parser.scanArgv(argv)) return -1;
  ZtCLI::handler<O, Facet>(parser.root).load(o);
  return parser.argc;
}

// load object from platform argc, argv
template <typename Facet = ZuFacet::CLI, typename O>
int load(O &o, int argc, const char *const *argv) {
  InArgv<> in(argc, argv);
  return load(o, in.argv);
}

} // ZtCLI

#endif /* ZtCLI_HH */
