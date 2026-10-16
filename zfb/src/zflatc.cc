//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// FBS to native Z declarations and Zfb metadata.
// Parsing, resolution and emission are separate passes over stable handles.

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuFP.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUTF.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRBTree.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtCase.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfCLI.hh>
#include <zlib/ZfStruct.hh>

#include <zlib/ZiFile.hh>

ZuDerive(Text, (ZtString<ZtStringHeapID<"zflatc.Text">>));
// Source token spans must survive file-array growth, including short inputs.
ZuDerive(Input, (ZtString<ZtStringBuiltin<0, ZtStringHeapID<"zflatc.Input">>>));
template <typename T>
struct Items : public ZtArray<T, ZtArrayHeapID<"zflatc.Items">> {
  ZuDerive_(Items, (ZtArray<T, ZtArrayHeapID<"zflatc.Items">>));
};
using Index = ZmRBTreeKV<Text, unsigned,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"zflatc.Index">>>;
using AttrIndex = ZmRBTree<ZuCSpan,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"zflatc.AttrIndex">>>;
using NameIndex = ZmRBTreeKV<ZuCSpan, unsigned,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"zflatc.NameIndex">>>;
struct ObjectAttrIDs { using Keys = ZuStringTL<"force_align", "bit_flags">; };
static constexpr auto objectAttrs = ZuMatcher<ObjectAttrIDs>();
struct FieldAttrIDs { using Keys = ZuStringTL<
  "id", "force_align", "required", "deprecated", "key">; };
static constexpr auto fieldAttrs = ZuMatcher<FieldAttrIDs>();

struct Loc { unsigned file, offset; };
struct Failure { Loc loc; Text message; };

[[noreturn]] static void fail(Loc loc, ZuCSpan message)
{
  throw Failure{loc, Text{message}};
}

static bool alpha(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool digit(char c) { return c >= '0' && c <= '9'; }
static bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static bool refHeader(ZuCSpan ref)
{
  switch (ref.length()) {
    case 8:
      if (ref[1] == 'u') return ref == "ZuObject";
      if (ref[1] == 'm') return ref == "ZmObject";
      return false;
    case 11:
      if (ref[1] == 'u') return ref == "ZuPolymorph";
      if (ref[1] == 'm') return ref == "ZmPolymorph";
      return false;
    default: return false;
  }
}

namespace Tok { enum { End = 256, Name, Number, String }; }
struct Token { unsigned kind; ZuCSpan text; Loc loc; };

// End of key/number/string; offsets are relative to the supplied span.
static unsigned eok(ZuCSpan in)
{
  unsigned i = 0, n = in.length();
  while (i < n && (alpha(in[i]) || digit(in[i]))) ++i;
  return i;
}

static bool xdigit(char c)
{
  return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static unsigned eon(ZuCSpan in, Loc loc)
{
  unsigned i = 0, n = in.length();
  if (i < n && (in[i] == '+' || in[i] == '-')) ++i;
  if (i < n && alpha(in[i])) {
    unsigned len = eok({in.data() + i, n - i});
    ZuCSpan word{in.data() + i, len};
    struct IDs { using Keys = ZuStringTL<"inf", "nan">; };
    static constexpr auto ids = ZuMatcher<IDs>();
    if (ids.exact(word) < 0) fail(loc, "invalid signed number");
    return i + len;
  }
  bool hex = i + 1 < n && in[i] == '0' &&
    (in[i + 1] == 'x' || in[i + 1] == 'X');
  if (hex) i += 2;
  unsigned b = i;
  while (i < n && (hex ? xdigit(in[i]) : digit(in[i]))) ++i;
  unsigned digits = i - b;
  bool dot = i < n && in[i] == '.';
  if (dot) {
    b = ++i;
    while (i < n && (hex ? xdigit(in[i]) : digit(in[i]))) ++i;
    digits += i - b;
  }
  if (!digits) fail({loc.file, loc.offset + i}, "expected number digits");
  bool exp = i < n && (hex ? (in[i] == 'p' || in[i] == 'P') :
    (in[i] == 'e' || in[i] == 'E'));
  if (exp) {
    ++i;
    if (i < n && (in[i] == '+' || in[i] == '-')) ++i;
    b = i;
    while (i < n && digit(in[i])) ++i;
    if (i == b) fail({loc.file, loc.offset + i}, "expected exponent digits");
  }
  if (hex && dot && !exp)
    fail({loc.file, loc.offset + i}, "expected hexadecimal exponent");
  if (i < n && (alpha(in[i]) || in[i] == '.'))
    fail({loc.file, loc.offset + i}, "invalid number suffix");
  return i;
}

// The input starts after the opening quote; return the closing quote offset.
static unsigned eos(ZuCSpan in, Loc loc)
{
  unsigned i = 0, n = in.length();
  while (i < n) {
    char c = in[i];
    if (c == '"') return i;
    if (static_cast<unsigned char>(c) < ' ')
      fail({loc.file, loc.offset + i}, "control character in string");
    ++i;
    if (c == '\\') {
      if (i == n) break;
      if (static_cast<unsigned char>(in[i]) < ' ')
	fail({loc.file, loc.offset + i}, "control character in string escape");
      ++i;
    }
  }
  fail(loc, "unterminated string");
  ZuUnreachable();
}

class Scan {
public:
  Scan(unsigned file, ZuCSpan in) : m_file{file}, m_in{in} { }

  Token next() {
    bok();
    unsigned b = m_pos, n = m_in.length();
    Loc loc{m_file, b};
    if (b == n) return {Tok::End, {}, loc};
    ZuCSpan in{m_in.data() + b, n - b};
    char c = in[0];
    unsigned kind;
    if (alpha(c)) {
      kind = Tok::Name;
      m_pos += eok(in);
    } else if (digit(c) || c == '-' || c == '+' ||
	(c == '.' && in.length() > 1 && digit(in[1]))) {
      kind = Tok::Number;
      m_pos += eon(in, loc);
    } else if (c == '"') {
      ++b;
      auto len = eos({m_in.data() + b, n - b}, {m_file, b});
      m_pos = b + len + 1;
      return {Tok::String, {m_in.data() + b, len}, loc};
    } else {
      kind = static_cast<unsigned char>(c);
      ++m_pos;
    }
    return {kind, {m_in.data() + b, m_pos - b}, loc};
  }

private:
  // Beginning of key/token: skip whitespace and schema comments.
  void bok() {
    unsigned n = m_in.length();
    for (;;) {
      while (m_pos < n && space(m_in[m_pos])) ++m_pos;
      if (m_pos + 1 >= n || m_in[m_pos] != '/') return;
      switch (m_in[m_pos + 1]) {
	case '/':
	  m_pos += 2;
	  while (m_pos < n && m_in[m_pos] != '\n') ++m_pos;
	  continue;
	case '*': {
	  unsigned b = m_pos;
	  m_pos += 2;
	  while (m_pos + 1 < n &&
	      !(m_in[m_pos] == '*' && m_in[m_pos + 1] == '/')) ++m_pos;
	  if (m_pos + 1 >= n) fail({m_file, b}, "unterminated comment");
	  m_pos += 2;
	} continue;
	default: return;
      }
    }
  }

  unsigned m_file;
  ZuCSpan m_in;
  unsigned m_pos = 0;
};

template <unsigned N, typename T>
static bool scanHex(ZuCSpan in, T &v)
{
  if (in.length() < N) return false;
  for (unsigned i = 0; i < N; ++i) if (!xdigit(in[i])) return false;
  return ZuBoxed(v).template scan<ZuFmt::Hex<1, ZuFmt::Right<N>>>(
    ZuCSpan{in.data(), N}) == N;
}

static Text unquote(Token token)
{
  Text out{token.text};
  unsigned i = 0, n = out.length();
  // Exact-copy fast path; decode escapes in place only when needed.
  while (i < n && out[i] != '\\') ++i;
  unsigned o = i;
  while (i < n) {
    char c = out[i++];
    if (c == '\\') {
      if (i == n) fail(token.loc, "unterminated string escape");
      switch (c = out[i++]) {
	case '"': case '\\': case '/': break;
	case 'b': c = '\b'; break;
	case 'f': c = '\f'; break;
	case 'n': c = '\n'; break;
	case 'r': c = '\r'; break;
	case 't': c = '\t'; break;
	case 'x': {
	  unsigned v;
	  if (i + 2 > n ||
	      !scanHex<2>(ZuCSpan{out.data() + i, n - i}, v))
	    fail(token.loc, "invalid hexadecimal escape");
	  c = char(v);
	  i += 2;
	} break;
	case 'u': {
	  uint16_t utf[2]; // UTF-16 encodes a scalar in at most two units.
	  unsigned len = 1;
	  auto scan = [&](uint16_t &v) {
	    if (i + 4 > n ||
		!scanHex<4>(ZuCSpan{out.data() + i, n - i}, v))
	      fail(token.loc, "invalid Unicode escape");
	    i += 4;
	  };
	  scan(utf[0]);
	  if (ZuUTF16::in(utf[0]) == 2) {
	    if (i + 2 > n || out[i] != '\\' || out[i + 1] != 'u')
	      fail(token.loc, "missing Unicode surrogate");
	    i += 2;
	    scan(utf[1]);
	    len = 2;
	  }
	  ZuSpan<const uint16_t> in{utf, len};
	  auto bytes = ZuUTF<char, uint16_t>::len(in);
	  if (!bytes || ZuUTF<char, uint16_t>::cvt(
	      {out.data() + o, bytes}, in) != bytes)
	    fail(token.loc, "invalid Unicode scalar");
	  o += bytes;
	  continue;
	}
	default: fail(token.loc, "unsupported string escape");
      }
    }
    out[o++] = c;
  }
  out.length(o);
  return out;
}

static void identifier(ZuCSpan name, Loc loc, bool field = false)
{
  if (!name || name.length() > 28 || !alpha(name[0]) || name[0] == '_')
    fail(loc, "reserved or overlong identifier");
  if (name == "Zflatc_") fail(loc, "reserved generator namespace");
  for (unsigned i = 0, n = name.length(); i < n; ++i) {
    auto c = name[i];
    if (!(alpha(c) || digit(c)) || (c == '_' && i && name[i - 1] == '_'))
      fail(loc, "reserved or invalid identifier");
    if (field && c >= 'A' && c <= 'Z') fail(loc, "field name must be snake-case");
  }
  if (field && name[name.length() - 1] == '_')
    fail(loc, "field name must be snake-case");
  struct IDs { using Keys = ZuStringTL<
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
    "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
    "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
    "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
    "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
    "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
    "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
    "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private",
    "protected", "public", "register", "reinterpret_cast", "requires",
    "return", "short", "signed", "sizeof", "static", "static_assert",
    "static_cast", "struct", "switch", "template", "this", "thread_local",
    "throw", "true", "try", "typedef", "typeid", "typename", "union",
    "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while",
    "xor", "xor_eq">; };
  static constexpr auto words = ZuMatcher<IDs>();
  if (words.exact(name) >= 0) fail(loc, "C++ keyword used as an identifier");
}

static void schemaID(ZuCSpan name, Loc loc)
{
  identifier(name, loc);
  if (name.length() > 1 && name[0] == 'Z' && name[1] >= 'a' && name[1] <= 'z')
    fail(loc, "reserved framework identifier");
}

static int builtinID(ZuCSpan name)
{
  struct IDs { using Keys = ZuStringTL<"Zf.Int128", "Zf.UInt128", "Zf.Fixed",
    "Zf.Decimal", "Zf.Time", "Zf.DateTime", "Zf.Bytes", "Zf.Bitmap",
    "Zi.IPv4", "Zi.IPv6", "Zi.IP">; };
  static constexpr auto ids = ZuMatcher<IDs>();
  return ids.exact(name);
}

static uint64_t uintValue(Token token)
{
  ZuCSpan in = token.text;
  if (in && in[0] == '+') in = {in.data() + 1, in.length() - 1};
  bool hex = in.length() > 2 && in[0] == '0' &&
    (in[1] == 'x' || in[1] == 'X');
  if (hex) in = {in.data() + 2, in.length() - 2};
  while (in.length() > 1 && in[0] == '0')
    in = {in.data() + 1, in.length() - 1};
  constexpr ZuCSpan max = "18446744073709551615";
  if (hex ? in.length() > 2 * sizeof(uint64_t) :
      (in.length() > max.length() || (in.length() == max.length() &&
	memcmp(in.data(), max.data(), max.length()) > 0)))
    fail(token.loc, "unsigned integer out of range");
  uint64_t v;
  int n = hex ? ZuBoxed(v).scan<ZuFmt::Hex<>>(in) : ZuBoxed(v).scan(in);
  if (n <= 0 || unsigned(n) != in.length())
    fail(token.loc, "invalid unsigned integer");
  return v;
}

namespace Decl { enum { Table, Struct, Enum, Union }; }

static Text cppName(ZuCSpan name)
{
  Text out;
  for (unsigned i = 0, n = name.length(); i < n; ++i) {
    if (name[i] == '.') out << "::";
    else out << name[i];
  }
  return out;
}

struct TypeExpr {
  Text name;
  Loc loc;
  int target = -1;
  int code = -1;
  unsigned count = 0;
  bool vector = false;
};
struct Attr { ZuCSpan name, value; Loc loc; };
struct Field {
  ZuCSpan name;
  Text cpp;
  TypeExpr type;
  ZuCSpan deflt;
  Loc loc;
  Items<Attr> attrs;
  unsigned id = 0;
  unsigned align = 0;
  bool explicitID = false;
  bool required = false;
  bool deprecated = false;
  bool key = false;
};
struct EnumVal {
  ZuCSpan name;
  Text type;
  Loc loc;
  uint64_t value = 0;
  int target = -1;
};
struct Object {
  Text name, ns;
  ZuCSpan local;
  Loc loc;
  unsigned kind;
  Items<Field> fields;
  Items<EnumVal> values;
  Items<Attr> attrs;
  ZuCSpan underlying;
  unsigned align = 0;
  int code = -1;
  bool flags = false;
  int builtin = -1;
  ZuCSpan native;
  unsigned size = 0, wireAlign = 1;
};
using EnumValueKey = ZuTuple<const Object *, ZuCSpan>;
using EnumValueIndex = ZmRBTreeKV<EnumValueKey, unsigned,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"zflatc.EnumValueIndex">>>;
struct Include { Text name; Loc loc; unsigned file; };
struct Source {
  Zi::Path path;
  Input text;
  Items<Include> includes;
  Text root, rootNS;
  Loc rootLoc;
  int rootType = -1;
  bool active = false;
};

static int scalarCode(ZuCSpan name)
{
  struct IDs { using Keys = ZuStringTL<
    "bool", "byte", "ubyte", "short", "ushort", "int", "uint", "long",
    "ulong", "float", "double", "string">; };
  static constexpr auto ids = ZuMatcher<IDs>();
  switch (ids.exact(name)) {
    case 0: return ZfFieldTC::Bool;
    case 1: return ZfFieldTC::Int8;
    case 2: return ZfFieldTC::UInt8;
    case 3: return ZfFieldTC::Int16;
    case 4: return ZfFieldTC::UInt16;
    case 5: return ZfFieldTC::Int32;
    case 6: return ZfFieldTC::UInt32;
    case 7: return ZfFieldTC::Int64;
    case 8: return ZfFieldTC::UInt64;
    case 9: case 10: return ZfFieldTC::Float;
    case 11: return ZfFieldTC::String;
    default: return -1;
  }
}

class Schema;

class Parser {
public:
  Parser(Schema &schema, unsigned file, ZuCSpan text) :
    m_schema{schema}, m_scan{file, text}, m_file{file} { next(); }
  void run();

private:
  void next() { m_token = m_scan.next(); }
  bool take(unsigned kind) {
    if (m_token.kind != kind) return false;
    next();
    return true;
  }
  Token need(unsigned kind) {
    Token token = m_token;
    if (token.kind != kind) fail(token.loc, "unexpected token");
    next();
    return token;
  }
  Text qualified() {
    Text out{need(Tok::Name).text};
    while (take('.')) { out << '.' << need(Tok::Name).text; }
    return out;
  }
  Items<Attr> attrs() {
    Items<Attr> out;
    if (!take('(')) return out;
    AttrIndex index;
    do {
	auto name = need(Tok::Name);
	Attr attr{name.text, {}, name.loc};
	if (take(':')) {
	  attr.value = m_token.text;
	  if (m_token.kind != Tok::Name && m_token.kind != Tok::Number &&
	      m_token.kind != Tok::String) fail(m_token.loc, "expected attribute value");
	  next();
	}
	if (index.find(attr.name)) fail(attr.loc, "duplicate attribute");
	index.add(attr.name);
	out.push(ZuMv(attr));
    } while (take(','));
    need(')');
    return out;
  }
  TypeExpr type() {
    TypeExpr out;
    out.loc = m_token.loc;
    out.vector = take('[');
    out.name = qualified();
    if (out.vector) {
	if (take(':')) {
	  auto count = need(Tok::Number);
	  auto n = uintValue(count);
	  if (!n || n > UINT16_MAX) fail(count.loc, "fixed array extent out of range");
	  out.count = n;
	}
	need(']');
    }
    return out;
  }
  Field field() {
    auto name = need(Tok::Name);
    identifier(name.text, name.loc, true);
    Field out;
    out.name = name.text;
    out.loc = name.loc;
    ZtCase::snakeCamel(out.name, [&out](ZuCSpan cpp) { out.cpp = cpp; });
    schemaID(out.cpp, out.loc);
    need(':');
    out.type = type();
    if (take('=')) {
	if (m_token.kind != Tok::Name && m_token.kind != Tok::Number)
	  fail(m_token.loc, "expected scalar default");
	out.deflt = m_token.text;
	next();
    }
    out.attrs = attrs();
    need(';');
    return out;
  }
  void object(unsigned kind);

  Schema &m_schema;
  Scan m_scan;
  unsigned m_file;
  Token m_token;
  Text m_ns;
};

class Schema {
public:
  Items<Source> files;
  Items<Object> objects;
  Items<Zi::Path> includeDirs;

  unsigned source(Zi::Path path, Input text) {
    unsigned id = files.length();
    auto &file = *new (files.push()) Source{};
    file.path = ZuMv(path);
    file.text = ZuMv(text);
    file.active = true;
    return id;
  }
  unsigned read(const Zi::Path &path, Loc from = {}) {
    auto key = ZiFile::canonical(path);
    if (!key) fail(from, "cannot resolve schema path");
    Text keyText{key};
    if (auto node = m_files.find(keyText)) {
	unsigned id = node->val();
	if (files[id].active) fail(from, "cyclic schema include");
	return id;
    }
    ZiFile file;
    if (file.open(key, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK)
      fail(from, "cannot open schema file");
    auto stat = file.fstat();
    if (!stat) fail(from, "cannot stat schema file");
    auto size = stat.size();
    if (size < 0 || uint64_t(size) > INT_MAX)
      fail(from, "schema file too large");
    Input text;
    text.length(size);
    if (size && file.read(text.data(), size) != size)
      fail(from, "cannot read schema file");
    unsigned id = source(ZuMv(key), ZuMv(text));
    m_files.add(ZuMv(keyText), id);
    Parser{*this, id, files[id].text}.run();
    files[id].active = false;
    return id;
  }
  void include(unsigned file, Token token) {
    Text name = unquote(token);
    if (!name || memchr(name.data(), 0, name.length()))
      fail(token.loc, "invalid schema include path");
    Zi::Path path = ZiFile::append(ZiFile::dirname(files[file].path), Zi::Path{name});
    if (!ZiStat{path}.exists()) {
	bool found = false;
	for (unsigned i = 0, n = includeDirs.length(); i < n; ++i) {
	  path = ZiFile::append(includeDirs[i], Zi::Path{name});
	  if (ZiStat{path}.exists()) { found = true; break; }
	}
	if (!found) fail(token.loc, "schema include not found");
    }
    unsigned target = read(path, token.loc);
    files[file].includes.push(Include{ZuMv(name), token.loc, target});
  }
  void add(Object object) {
    if (m_symbols.find(object.name)) fail(object.loc, "duplicate schema type");
    unsigned id = objects.length();
    m_symbols.add(object.name, id);
    objects.push(ZuMv(object));
  }
  int find(ZuCSpan name, ZuCSpan ns) const {
    Text key;
    if (ns) key << ns << '.';
    key << name;
    for (;;) {
	if (auto node = m_symbols.find(key)) return node->val();
	if (!ns) break;
	unsigned n = ns.length();
	while (n && ns[n - 1] != '.') --n;
	ns = n ? ZuCSpan{ns.data(), n - 1} : ZuCSpan{};
	key.null();
	if (ns) key << ns << '.';
	key << name;
    }
    return -1;
  }
  void resolve();
  Items<unsigned> order() const;
  int enumValue(const Object &o, ZuCSpan name) const {
    unsigned n = o.values.length();
    if (!n) return -1;
    if (n == 1) return o.values[0].name == name ? 0 : -1;
    if (auto found = m_enumValues.find(
	EnumValueKey{&o, name})) return found->val();
    return -1;
  }

private:
  Index m_files;
  Index m_symbols;
  EnumValueIndex m_enumValues;
};

void Parser::object(unsigned kind)
{
  auto name = need(Tok::Name);
  schemaID(name.text, name.loc);
  Object out;
  out.ns = m_ns;
  if (m_ns) out.name << m_ns << '.';
  out.name << name.text;
  out.local = name.text;
  out.loc = name.loc;
  out.kind = kind;
  if (kind == Decl::Enum) { need(':'); out.underlying = need(Tok::Name).text; }
  out.attrs = attrs();
  need('{');
  while (!take('}')) {
    if (kind == Decl::Table || kind == Decl::Struct) {
      out.fields.push(field());
    } else {
      auto v = need(Tok::Name);
      schemaID(v.text, v.loc);
      EnumVal val{v.text, {}, v.loc, out.values.length(), -1};
      if (kind == Decl::Union) {
	val.value += 1;
	if (take(':')) {
	  val.type = qualified();
	} else val.type = val.name;
      }
      if (take('=')) val.value = uintValue(need(Tok::Number));
      out.values.push(ZuMv(val));
      if (!take(',')) { need('}'); break; }
    }
  }
  take(';');
  m_schema.add(ZuMv(out));
}

void Parser::run()
{
  struct IDs { using Keys = ZuStringTL<
    "namespace", "include", "root_type", "table", "struct", "enum", "union">; };
  static constexpr auto ids = ZuMatcher<IDs>();
  while (m_token.kind != Tok::End) {
    auto token = need(Tok::Name);
    switch (ids.exact(token.text)) {
      case 0:
	m_ns = qualified();
	need(';');
	break;
      case 1: {
	auto path = need(Tok::String);
	need(';');
	m_schema.include(m_file, path);
      } break;
      case 2: {
	if (m_schema.files[m_file].root) fail(token.loc, "duplicate root_type");
	Text name = qualified();
	auto &file = m_schema.files[m_file];
	file.root = ZuMv(name);
	file.rootNS = m_ns;
	file.rootLoc = token.loc;
	need(';');
      } break;
      case 3: object(Decl::Table); break;
      case 4: object(Decl::Struct); break;
      case 5: object(Decl::Enum); break;
      case 6: object(Decl::Union); break;
      default: fail(token.loc, "schema declaration is outside Z emission scope");
    }
  }
}

static unsigned intBits(int code)
{
  switch (code) {
    case ZfFieldTC::Int8: case ZfFieldTC::UInt8: return 8;
    case ZfFieldTC::Int16: case ZfFieldTC::UInt16: return 16;
    case ZfFieldTC::Int32: case ZfFieldTC::UInt32: return 32;
    case ZfFieldTC::Int64: case ZfFieldTC::UInt64: return 64;
    default: return 0;
  }
}

static bool isSigned(int code)
{
  return code == ZfFieldTC::Int8 || code == ZfFieldTC::Int16 ||
    code == ZfFieldTC::Int32 || code == ZfFieldTC::Int64;
}

static Text defaultExpr(const Schema &schema, const Field &field)
{
  const auto &type = field.type;
  int code = type.code;
  const Object *en = nullptr;
  if (type.target >= 0) {
    en = &schema.objects[type.target];
    if (en->kind == Decl::Enum) code = en->code;
    else en = nullptr;
  }
  if (type.vector || code < 0 || code == ZfFieldTC::String) {
    if (field.deflt) fail(field.loc, "default requires a scalar field");
    return {};
  }
  ZuCSpan in = field.deflt ? field.deflt : ZuCSpan{"0"};
  if (code == ZfFieldTC::Bool) {
    struct IDs { using Keys = ZuStringTL<"false", "true", "0", "1">; };
    static constexpr auto ids = ZuMatcher<IDs>();
    int i = ids.exact(in);
    if (i < 0) fail(field.loc, "invalid boolean default");
    return Text{i & 1 ? "true" : "false"};
  }
  if (code == ZfFieldTC::Float) {
    Text out;
    ZuCSpan cpp = type.name == "float" ? "float" : "double";
    struct IDs { using Keys = ZuStringTL<
      "inf", "+inf", "-inf", "nan", "+nan", "-nan">; };
    static constexpr auto ids = ZuMatcher<IDs>();
    switch (ids.exact(in)) {
      case 0: case 1: out << "ZuFP<" << cpp << ">::inf()"; return out;
      case 2: out << "-ZuFP<" << cpp << ">::inf()"; return out;
      case 3: case 4: case 5: out << "ZuFP<" << cpp << ">::nan()"; return out;
    }
    // Recheck raw defaults that arrived as name tokens (e.g. an enum name).
    if (eon(in, field.loc) != in.length()) fail(field.loc, "invalid float default");
    // ZuBox's decimal scanner does not consume hex floats or exponents.
    // Validate those literals with the C scanner against the original,
    // NUL-terminated source buffer, without a token copy or private FP codec.
    char *end;
    double value = type.name == "float" ? strtof(in.data(), &end) : strtod(in.data(), &end);
    if (end != in.data() + in.length() || ZuFP<double>::inf(value) ||
	ZuFP<double>::inf(-value)) fail(field.loc, "float default out of range");
    out << in;
    bool hex = false, frac = false;
    for (unsigned i = 0; i < in.length(); ++i) {
      hex |= in[i] == 'x' || in[i] == 'X';
      frac |= in[i] == '.' || in[i] == 'p' || in[i] == 'P';
      if (!hex) frac |= in[i] == 'e' || in[i] == 'E';
    }
    if (!frac) out << (hex ? "p0" : ".0");
    if (type.name == "float") out << 'f';
    return out;
  }
  if (en) {
    if (int i = schema.enumValue(*en, in); i >= 0) {
      Text out;
      out << ZuBox<uint64_t>{en->flags ? uint64_t(1) << i : i};
      return out;
    }
  }
  bool neg = in && in[0] == '-';
  if (neg) in = {in.data() + 1, in.length() - 1};
  uint64_t v = uintValue({Tok::Number, in, field.loc});
  unsigned bits = intBits(code);
  bool sign = isSigned(code);
  uint64_t limit = sign ? uint64_t(1) << (bits - 1) :
    (bits == 64 ? UINT64_MAX : (uint64_t(1) << bits) - 1);
  if (sign && !neg) --limit;
  if (v > limit || (neg && !sign && v)) fail(field.loc, "default out of range");
  if (en && ((en->flags && en->values.length() < 64 &&
      v >= (uint64_t(1) << en->values.length())) ||
      (!en->flags && (neg || v >= en->values.length()))))
    fail(field.loc, "default is outside the enum");
  Text out;
  if (neg && v) out << "(-" << ZuBox<uint64_t>{v - 1} << "LL - 1)";
  else out << ZuBox<uint64_t>{v} << "ULL";
  return out;
}

static void qualID(ZuCSpan name, Loc loc, bool schema = false)
{
  unsigned b = 0;
  for (unsigned i = 0, n = name.length(); i <= n; ++i) {
    if (i != n && name[i] != '.') continue;
    ZuCSpan part{name.data() + b, i - b};
    if (schema && (b || builtinID(name) < 0)) schemaID(part, loc);
    else identifier(part, loc);
    b = i + 1;
  }
}

static void builtin(const Schema &schema, Object &o)
{
  int id = builtinID(o.name);
  if (id < 0) return;
  if (id == 10) {
    if (o.kind != Decl::Union || o.values.length() != 2 ||
	o.values[0].name != "IPv4" || o.values[1].name != "IPv6" ||
	schema.objects[o.values[0].target].name != "Zi.IPv4" ||
	schema.objects[o.values[1].target].name != "Zi.IPv6")
      fail(o.loc, "incompatible built-in IP schema");
    o.builtin = ZfFieldTC::UDT;
    o.native = "ZiIP";
    return;
  }
  Text shape;
  for (unsigned i = 0; i < o.fields.length(); ++i) {
    const auto &f = o.fields[i];
    if (f.required || f.deprecated || f.align ||
	(f.type.count && id < 8) || f.deflt)
      fail(f.loc, "incompatible built-in value schema");
    shape << f.name << ':';
    if (f.type.vector) shape << '[';
    shape << f.type.name;
    if (f.type.count) shape << ':' << ZuBox<unsigned>{f.type.count};
    if (f.type.vector) shape << ']';
    shape << ';';
  }
  ZuCSpan expected;
  switch (id) {
    case 0: o.builtin = ZfFieldTC::Int128; expected = "h:ulong;l:ulong;"; break;
    case 1: o.builtin = ZfFieldTC::UInt128; expected = "h:ulong;l:ulong;"; break;
    case 2: o.builtin = ZfFieldTC::Fixed; expected = "mantissa:long;ndp:ubyte;"; break;
    case 3: o.builtin = ZfFieldTC::Decimal; expected = "h:ulong;l:ulong;"; break;
    case 4: o.builtin = ZfFieldTC::Time; expected = "sec:long;nsec:int;"; break;
    case 5: o.builtin = ZfFieldTC::DateTime; expected = "julian:int;sec:int;nsec:int;"; break;
    case 6: o.builtin = ZfFieldTC::Bytes; expected = "data:[ubyte];"; break;
    case 7: o.builtin = ZfFieldTC::UDT; expected = "data:[ulong];"; break;
    case 8:
      o.builtin = ZfFieldTC::UDT; o.native = "Zfb::IPv4";
      expected = "addr:[ubyte:4];";
      break;
    case 9:
      o.builtin = ZfFieldTC::UDT; o.native = "Zfb::IPv6";
      expected = "addr:[ubyte:16];";
      break;
  }
  if (shape != expected || o.align ||
      o.kind != (id < 6 || id >= 8 ? Decl::Struct : Decl::Table))
    fail(o.loc, "incompatible built-in value schema");
}

Items<unsigned> Schema::order() const
{
  Items<unsigned> out;
  Items<uint8_t> state;
  state.length(objects.length());
  memset(state.data(), 0, state.length());
  auto visit = [&](auto &&self, unsigned i) -> void {
    if (state[i] == 2) return;
    if (state[i] == 1) fail(objects[i].loc, "cycle of inline values");
    state[i] = 1;
    const auto &o = objects[i];
    for (unsigned f = 0; f < o.fields.length(); ++f) {
      int target = o.fields[f].type.target;
      if (target >= 0 && objects[target].kind == Decl::Struct) self(self, target);
    }
    state[i] = 2;
    out.push(i);
  };
  for (unsigned i = 0; i < objects.length(); ++i)
    if (objects[i].kind == Decl::Struct) visit(visit, i);
  for (unsigned i = 0; i < objects.length(); ++i)
    if (objects[i].kind != Decl::Struct) out.push(i);
  return out;
}

void Schema::resolve()
{
  m_enumValues.clean();
  for (const auto &o : objects)
    if (o.kind == Decl::Enum && o.values.length() > 1) {
      auto &index = m_enumValues;
      for (unsigned v = 0, n = o.values.length(); v < n; ++v) {
	EnumValueKey key{&o, o.values[v].name};
	if (!index.find(key)) index.add(ZuMv(key), v);
      }
    }
  for (unsigned i = 0; i < objects.length(); ++i)
    if (objects[i].kind == Decl::Enum)
      objects[i].code = scalarCode(objects[i].underlying);
  for (unsigned i = 0, n = objects.length(); i < n; ++i) {
    auto &o = objects[i];
    qualID(o.name, o.loc, true);
    for (unsigned a = 0, na = o.attrs.length(); a < na; ++a) {
      const auto &attr = o.attrs[a];
      switch (objectAttrs.exact(attr.name)) {
	case 0:
	  if (o.kind != Decl::Struct)
	    fail(attr.loc, "attribute is outside Z emission scope");
	  o.align = uintValue({Tok::Number, attr.value, attr.loc});
	  if (!o.align || (o.align & (o.align - 1)) || o.align > 32)
	    fail(attr.loc, "invalid struct alignment");
	  break;
	case 1:
	  if (o.kind != Decl::Enum || attr.value)
	    fail(attr.loc, "attribute is outside Z emission scope");
	  o.flags = true;
	  break;
	default:
	  fail(attr.loc, "attribute is outside Z emission scope");
      }
    }
    if (o.kind == Decl::Enum) {
      o.code = scalarCode(o.underlying);
      if (o.code < ZfFieldTC::Int8 || o.code > ZfFieldTC::UInt64)
	fail(o.loc, "invalid enum underlying type");
      unsigned bits = intBits(o.code) - unsigned(isSigned(o.code));
      if (o.flags ? o.values.length() > bits :
	  (bits < 32 && o.values.length() > (uint64_t(1) << bits)))
	fail(o.loc, "enum values exceed underlying type");
    }
    if (o.kind == Decl::Union && o.values.length() > UINT8_MAX)
      fail(o.loc, "union alternatives exceed discriminator range");
    NameIndex names;
    for (unsigned v = 0, nv = o.values.length(); v < nv; ++v) {
      auto &val = o.values[v];
      if (o.kind == Decl::Enum) {
	struct IDs { using Keys = ZuStringTL<"N", "T", "Map", "Map_", "name", "lookup", "Zfb_Fmt">; };
	static constexpr auto ids = ZuMatcher<IDs>();
	if (ids.exact(val.name) >= 0 || (o.flags &&
	    (val.name.length() >= 28 || val.name[val.name.length() - 1] == '_')))
	  fail(val.loc, "reserved or overlong enum identifier");
      }
      if (val.value != v + unsigned(o.kind == Decl::Union))
	fail(val.loc, "enum/union values must match Z ordinals");
      if (o.kind == Decl::Enum) {
	if (o.values.length() > 1 && enumValue(o, val.name) != int(v))
	  fail(val.loc, "duplicate enum/union name");
      } else {
	if (names.find(val.name)) fail(val.loc, "duplicate enum/union name");
	names.add(val.name, v);
      }
      if (o.kind == Decl::Union) {
	if (val.name == "NONE") fail(val.loc, "reserved union alternative");
	val.target = find(val.type, o.ns);
	if (val.target < 0) fail(val.loc, "unresolved union payload");
	auto kind = objects[val.target].kind;
	if (kind != Decl::Table && kind != Decl::Struct)
	  fail(val.loc, "unsupported union payload");
      }
    }
    unsigned next = 0, keys = 0;
    bool explicitIDs = false;
    bool implicitIDs = false;
    names.clean();
    for (unsigned f = 0, nf = o.fields.length(); f < nf; ++f)
      if (!names.find(o.fields[f].name)) names.add(o.fields[f].name, f);
    for (unsigned f = 0, nf = o.fields.length(); f < nf; ++f) {
      auto &field = o.fields[f];
      auto &type = field.type;
      type.code = scalarCode(type.name);
      if (type.code < 0) {
	type.target = find(type.name, o.ns);
	if (type.target < 0) fail(type.loc, "unresolved field type");
      }
      bool isUnion = type.target >= 0 && objects[type.target].kind == Decl::Union;
      if (isUnion && type.vector) fail(type.loc, "union vectors are outside Z emission scope");
      if (names.find(field.name)->val() != f)
	fail(field.loc, "converted field-name collision");
      if (isUnion) {
	Text tag{field.name};
	tag << "_type";
	if (names.find(tag))
	  fail(field.loc, "union tag-name collision");
      }
      for (unsigned a = 0, na = field.attrs.length(); a < na; ++a) {
	const auto &attr = field.attrs[a];
	switch (fieldAttrs.exact(attr.name)) {
	  case 0: {
	    if (o.kind != Decl::Table)
	      fail(attr.loc, "field attribute is outside Z emission scope");
	    auto id = uintValue({Tok::Number, attr.value, attr.loc});
	    if (id > UINT16_MAX) fail(attr.loc, "field ID out of range");
	    field.id = id;
	    field.explicitID = true;
	    break;
	  }
	  case 1: {
	    auto align = uintValue({Tok::Number, attr.value, attr.loc});
	    if (!align || (align & (align - 1)) || align > 32)
	      fail(attr.loc, "invalid field alignment");
	    field.align = align;
	    break;
	  }
	  case 2:
	    if (attr.value) fail(attr.loc, "field attribute is outside Z emission scope");
	    field.required = true;
	    break;
	  case 3:
	    if (attr.value) fail(attr.loc, "field attribute is outside Z emission scope");
	    field.deprecated = true;
	    break;
	  case 4:
	    if (attr.value) fail(attr.loc, "field attribute is outside Z emission scope");
	    field.key = true;
	    break;
	  default:
	    fail(attr.loc, "field attribute is outside Z emission scope");
	}
      }
      if (field.key) {
	int code = type.code;
	if (type.target >= 0 && objects[type.target].kind == Decl::Enum)
	  code = objects[type.target].code;
	if (++keys > 1 || o.kind != Decl::Table || field.deprecated || type.vector ||
	    (code != ZfFieldTC::String && code != ZfFieldTC::Bool &&
	      code != ZfFieldTC::Float && !intBits(code)))
	  fail(field.loc, "invalid table key");
	if (code == ZfFieldTC::String) field.required = true;
      }
      explicitIDs |= field.explicitID;
      implicitIDs |= !field.explicitID;
      if (!field.explicitID) field.id = next + unsigned(isUnion);
      next += 1 + unsigned(isUnion);
      if (o.kind == Decl::Struct) {
	if (field.required || field.deprecated || field.deflt ||
	    (type.vector && !type.count) || type.code == ZfFieldTC::String ||
	    (type.target >= 0 && objects[type.target].kind != Decl::Struct &&
	      objects[type.target].kind != Decl::Enum))
	  fail(field.loc, "invalid inline struct field");
      } else if (type.count) fail(type.loc, "fixed array requires an inline struct");
      if (field.required && !type.vector &&
	  ((type.code >= 0 && type.code != ZfFieldTC::String) ||
	    (type.target >= 0 && objects[type.target].kind == Decl::Enum))) fail(field.loc, "scalar field cannot be required");
      if (field.deprecated && field.required) fail(field.loc, "deprecated required field");
      if (field.align && o.kind == Decl::Table && !type.vector)
	fail(field.loc, "table field alignment requires a vector");
      defaultExpr(*this, field);
    }
    if (o.kind == Decl::Table) {
      if (explicitIDs && implicitIDs) fail(o.loc, "mixed explicit and implicit field IDs");
      Items<bool> slots;
      for (unsigned j = 0; j < next; ++j) slots.push(false);
      for (unsigned f = 0, nf = o.fields.length(); f < nf; ++f) {
	const auto &field = o.fields[f];
	bool isUnion = field.type.target >= 0 && objects[field.type.target].kind == Decl::Union;
	if (field.id >= next || slots[field.id] || (isUnion && (!field.id || slots[field.id - 1])))
	  fail(field.loc, "duplicate or noncontiguous field IDs");
	slots[field.id] = true;
	if (isUnion) slots[field.id - 1] = true;
      }
    }
  }
  for (auto &o : objects) builtin(*this, o);
  for (unsigned i = 0; i < files.length(); ++i) {
    auto &file = files[i];
    if (!file.root) continue;
    file.rootType = find(file.root, file.rootNS);
    if (file.rootType < 0 || objects[file.rootType].kind != Decl::Table)
      fail(file.rootLoc, "root_type must name a table");
  }
  // Resolve inline layout in dependency order, before publishing any output.
  // Member annotations force the enclosing alignment, not member offsets.
  auto ordered = order();
  for (unsigned i : ordered) {
    auto &o = objects[i];
    if (o.kind != Decl::Struct) continue;
    uint64_t size = 0;
    unsigned natural = 1, forced = o.align;
    for (const auto &f : o.fields) {
      unsigned width, align;
      int code = f.type.code;
      if (f.type.target >= 0) {
	const auto &target = objects[f.type.target];
	if (target.kind == Decl::Struct) {
	  width = target.size;
	  align = target.wireAlign;
	} else code = target.code;
      }
      if (code >= 0) {
	width = code == ZfFieldTC::Bool ? 1 : code == ZfFieldTC::Float ?
	  (f.type.name == "float" ? 4 : 8) : intBits(code) / 8;
	align = width;
      }
      if (natural < align) natural = align;
      if (forced < f.align) forced = f.align;
      size = ((size + align - 1) & ~uint64_t(align - 1)) +
	uint64_t(width) * (f.type.count ? f.type.count : 1);
      if (size > INT32_MAX) fail(f.loc, "inline struct exceeds wire size limit");
    }
    if (forced && forced < natural)
      fail(o.loc, "forced struct alignment is smaller than natural alignment");
    o.wireAlign = forced ? forced : natural;
    size = (size + o.wireAlign - 1) & ~uint64_t(o.wireAlign - 1);
    if (!size || size > INT32_MAX) fail(o.loc, "invalid inline struct size");
    o.size = size;
  }
}
struct Options {
  Zi::Path input, output;
  Text ref, api;
  Items<Text> includes;
  bool alt = false, help = false;
};
ZfStruct(, (Options, CLI),
  (input, (CLI::Arg<1>), String),
  (output, (CLI::Opt<'o'>), String),
  (ref, (CLI::Long<"ref">), String),
  (api, (CLI::Long<"export">), String),
  (includes, (CLI::Opt<'I'>), StringVec),
  (alt, (CLI::Long<"alt">), Bool),
  (help, (CLI::Flag<'h'>), Bool));

static ZuCSpan nativeCode(int code)
{
  switch (code) {
    case ZfFieldTC::Bool: return "bool";
    case ZfFieldTC::Int8: return "::int8_t";
    case ZfFieldTC::UInt8: return "::uint8_t";
    case ZfFieldTC::Int16: return "::int16_t";
    case ZfFieldTC::UInt16: return "::uint16_t";
    case ZfFieldTC::Int32: return "::int32_t";
    case ZfFieldTC::UInt32: return "::uint32_t";
    case ZfFieldTC::Int64: return "::int64_t";
    case ZfFieldTC::UInt64: return "::uint64_t";
    case ZfFieldTC::Int128: return "::int128_t";
    case ZfFieldTC::UInt128: return "::uint128_t";
    case ZfFieldTC::Float: return "double";
    case ZfFieldTC::Fixed: return "ZuFixed";
    case ZfFieldTC::Decimal: return "ZuDecimal";
    case ZfFieldTC::Time: return "ZuTime";
    case ZfFieldTC::DateTime: return "ZuDateTime";
    case ZfFieldTC::UDT: return "ZtBitmap"; // canonical Zf.Bitmap recovery
    default: ZuUnreachable();
  }
}

static ZuCSpan nativeType(const Object &o)
{
  return o.native ? o.native : nativeCode(o.builtin);
}

static bool ownsBytes(const Schema &schema, const Object &o)
{
  if (o.kind != Decl::Union || o.builtin >= 0) return false;
  for (const auto &v : o.values)
    if (schema.objects[v.target].builtin == ZfFieldTC::Bytes) return true;
  return false;
}

// A keyed table is an associative entry only when its whole wire shape is
// key/value. Other keyed tables retain every ordinary field.
static bool mapEntry(const Schema &schema, const Object &o)
{
  ZuCSpan name{o.local};
  if (o.kind != Decl::Table || o.builtin >= 0 || name.length() <= 5 ||
      ZuCSpan{name.data() + name.length() - 5, 5} != "Entry" ||
      o.fields.length() != 2) return false;
  const auto &key = o.fields[0], &value = o.fields[1];
  if (key.name != "key" || key.id != 0 || !key.key || key.deprecated ||
      value.name != "value" || value.deprecated) return false;
  bool isUnion = value.type.target >= 0 &&
    schema.objects[value.type.target].kind == Decl::Union;
  return value.id == 1 + unsigned(isUnion);
}

static bool mapVector(const Schema &schema, const Field &f)
{
  return f.type.vector && f.type.target >= 0 &&
    mapEntry(schema, schema.objects[f.type.target]);
}

static int memberCode(const Schema &schema, const Field &field)
{
  const auto &type = field.type;
  int code = type.code;
  if (type.target >= 0) {
    const auto &o = schema.objects[type.target];
    code = o.kind == Decl::Enum ? o.code : o.builtin;
  }
  if (code < 0) return ZfFieldTC::UDT;
  // Zf.Bytes is a wrapper table, distinct from a direct [ubyte] field.
  if (!type.vector) return code == ZfFieldTC::Bytes ? ZfFieldTC::UDT : code;
  switch (code) {
    case ZfFieldTC::String: return ZfFieldTC::StringVec;
    case ZfFieldTC::Bytes: return ZfFieldTC::BytesVec;
    case ZfFieldTC::Int8: return ZfFieldTC::Int8Vec;
    case ZfFieldTC::UInt8: return type.count ? ZfFieldTC::UInt8Vec : ZfFieldTC::Bytes;
    case ZfFieldTC::Int16: return ZfFieldTC::Int16Vec;
    case ZfFieldTC::UInt16: return ZfFieldTC::UInt16Vec;
    case ZfFieldTC::Int32: return ZfFieldTC::Int32Vec;
    case ZfFieldTC::UInt32: return ZfFieldTC::UInt32Vec;
    case ZfFieldTC::Int64: return ZfFieldTC::Int64Vec;
    case ZfFieldTC::UInt64: return ZfFieldTC::UInt64Vec;
    case ZfFieldTC::Int128: return ZfFieldTC::Int128Vec;
    case ZfFieldTC::UInt128: return ZfFieldTC::UInt128Vec;
    case ZfFieldTC::Float: return ZfFieldTC::FloatVec;
    case ZfFieldTC::Fixed: return ZfFieldTC::FixedVec;
    case ZfFieldTC::Decimal: return ZfFieldTC::DecimalVec;
    case ZfFieldTC::Time: return ZfFieldTC::TimeVec;
    case ZfFieldTC::DateTime: return ZfFieldTC::DateTimeVec;
    case ZfFieldTC::UDT: return ZfFieldTC::UDT;
    default: fail(field.loc, "unsupported vector element type");
  }
}

class Emit {
public:
  Emit(const Schema &schema, unsigned file, const Options &opts) :
    m_schema{schema}, m_file{file}, m_opts{opts} {
    struct IDs { using Keys = ZuStringTL<
      "ref", "deref", "mvref", "refCount", "withRef">; };
    static constexpr auto ids = ZuMatcher<IDs>();
    auto check = [&](ZuCSpan name, Loc loc) {
      if (ids.exact(name) < 0) return;
      Text message;
      message << "identifier hides reference-counting base: " << name;
      fail(loc, message);
    };
    for (const auto &o : schema.objects) {
      if (!own(o) || o.kind == Decl::Enum) continue;
      if (o.local == "Base") fail(o.loc, "type name is reserved by ZuDerive");
      if (o.kind == Decl::Union) continue;
      if (opts.ref) check(o.local, o.loc);
      for (const auto &f : o.fields) {
	if (f.deprecated) continue;
	if (f.cpp == o.local) fail(f.loc, "member name hides its containing type");
	if (opts.ref) check(f.cpp, f.loc);
      }
    }
    for (const auto &entry : schema.objects) {
      if (!own(entry) || !mapEntry(schema, entry)) continue;
      Text name{entry.name};
      name.length(name.length() - 5);
      if (int otherID = schema.find(name, {}); otherID >= 0) {
	const auto &other = schema.objects[otherID];
	if (other.kind != Decl::Table || other.fields.length() != 1 ||
	    other.fields[0].name != "entries" || other.fields[0].id != 0 ||
	    !other.fields[0].type.vector || other.fields[0].type.target < 0 ||
	    &schema.objects[other.fields[0].type.target] != &entry)
	  fail(other.loc, "map adapter name collides with a different table");
      }
      for (const auto &other : schema.objects)
	for (const auto &field : other.fields)
	  if (field.type.target >= 0 && &schema.objects[field.type.target] == &entry &&
	      !field.type.vector)
	    fail(field.loc, "map entry also used as a singular table");
    }
  }
  Text header();
  Text source(ZuCSpan header);

private:
  void open(Text &out, const Object &o) const {
    if (o.ns) out << "namespace " << cppName(o.ns) << " {\n\n";
  }
  void close(Text &out, const Object &o) const {
    if (o.ns) out << "\n} // " << cppName(o.ns) << "\n";
    out << '\n';
  }
  Text refType(const Object &o) const {
    Text out;
    out << (m_opts.ref ? "ZmRef<" : "ZuPtr<") << "::" << cppName(o.name) << '>';
    return out;
  }
  Text elemType(const Field &f) const {
    if (f.type.target >= 0) {
      const auto &o = m_schema.objects[f.type.target];
      if (o.kind == Decl::Enum) return Text{nativeCode(o.code)};
      if (o.builtin >= 0 && o.builtin != ZfFieldTC::Bytes)
	return Text{nativeType(o)};
      if (o.kind == Decl::Table) return refType(o);
      Text out;
      out << "::" << cppName(o.name);
      return out;
    }
    return Text{f.type.name == "float" ? ZuCSpan{"float"} : nativeCode(f.type.code)};
  }
  Text typeName(const Field &f) const {
    Text out{f.cpp};
    out[0] = ZtCase::toupper__(out[0]);
    // ZuDerive_ supplies Base in the public struct.
    if (out == "Base") out << '_';
    return out;
  }
  Text memberType(const Field &f) const {
    if (f.type.vector || f.type.code == ZfFieldTC::String ||
	(f.type.target >= 0 && (m_schema.objects[f.type.target].builtin == ZfFieldTC::Bytes ||
	  ownsBytes(m_schema, m_schema.objects[f.type.target]))))
      return typeName(f);
    return elemType(f);
  }
  Text mapType(const Object &o, const Field &f) const {
    const auto &entry = m_schema.objects[f.type.target];
    Text out;
    if (m_opts.ref) out << "ZmRef<";
    out << "::";
    if (entry.ns) out << cppName(entry.ns) << "::";
    out << "Zflatc_::" << entry.local << "::Map_<\"" << o.name << '.' << f.cpp << "\">";
    if (m_opts.ref) out << '>';
    return out;
  }
  Text mapFormat(const Object &o) const;
  Text mapValue(const Field &f, ZuCSpan member) const;
  void map(Text &out, const Object &o) const;
  Text containerType(const Object &o, const Field &f) const {
    Text out{"::"};
    Text name = typeName(f);
    if (name != o.local) out << cppName(o.name);
    else {
      if (o.ns) out << cppName(o.ns) << "::";
      out << "Zflatc_::" << o.local << "::O_";
    }
    out << "::" << name;
    return out;
  }
  void containers(Text &out, const Object &o) const;
  Text altType(const Object &o, const EnumVal &v) const {
    const auto &target = m_schema.objects[v.target];
    Text out;
    if (target.builtin == ZfFieldTC::Bytes) {
      out << "::";
      if (o.ns) out << cppName(o.ns) << "::";
      out << "Zflatc_::" << o.local << "::B_<HeapID_>";
    } else if (target.builtin >= 0) out = nativeType(target);
    else if (target.kind == Decl::Table) out = refType(target);
    else out << "::" << cppName(target.name);
    return out;
  }
  void object(Text &out, const Object &o) const;
  void metadata(Text &out, const Object &o) const;
  void root(Text &out, const Object &o) const;
  bool mappedRoot(const Object &o) const {
    int i = m_schema.files[m_file].rootType;
    return i >= 0 && &o == &m_schema.objects[i] && o.builtin >= 0;
  }
  bool own(const Object &o) const { return o.loc.file == m_file && o.builtin < 0; }

  const Schema &m_schema;
  unsigned m_file;
  const Options &m_opts;
};

static ZuCSpan modeline =
  "//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-\n"
  "//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2\n\n";

void Emit::containers(Text &out, const Object &o) const
{
  for (unsigned i = 0; i < o.fields.length(); ++i) {
    const auto &f = o.fields[i];
    Text heap;
    heap << o.name << '.' << f.cpp;
    Text name = typeName(f);
    if (!f.type.vector) {
      if (f.type.code == ZfFieldTC::String)
	out << "  ZuDerive(" << name << ", (ZtString<ZtStringHeapID<\""
	  << heap << "\">>));\n";
      else if (f.type.target >= 0 &&
	  m_schema.objects[f.type.target].builtin == ZfFieldTC::Bytes)
	out << "  struct " << name << " : public ZtArray<::uint8_t, ZtArrayHeapID<\""
	  << heap << "\">> {\n    ZuDerive_(" << name
	  << ", (ZtArray<::uint8_t, ZtArrayHeapID<\"" << heap << "\">>));\n"
	  << "    friend Zfb::Values::BytesMap ZfbTransform(" << name << " *);\n  };\n";
      else if (f.type.target >= 0 && ownsBytes(m_schema, m_schema.objects[f.type.target]))
	out << "  using " << name << " = ::" << cppName(m_schema.objects[f.type.target].name)
	  << "<\"" << heap << "\">;\n";
      continue;
    }
    if (!mapEntry(m_schema, o) && mapVector(m_schema, f)) {
      out << "  using " << name << " = " << mapType(o, f) << ";\n";
      continue;
    }
    Text elem;
    if (f.type.code == ZfFieldTC::String) {
      elem << 'E' << ZuBox<unsigned>{i} << '_';
      out << "  ZuDerive(" << elem << ", (ZtString<ZtStringHeapID<\""
	<< heap << "\">>));\n";
    } else if (f.type.target >= 0 &&
	m_schema.objects[f.type.target].builtin == ZfFieldTC::Bytes) {
      elem << 'E' << ZuBox<unsigned>{i} << '_';
      out << "  ZuDerive(" << elem << ", (ZtArray<::uint8_t, ZtArrayHeapID<\""
	<< heap << "\">>));\n";
    } else elem = elemType(f);
    out << "  ZuDerive(" << name << ", (ZtArray<" << elem
      << ", ZtArrayHeapID<\"" << heap << "\">>));\n";
  }
}

Text Emit::mapFormat(const Object &o) const
{
  Text name{o.name};
  name.length(name.length() - 5);
  Text out;
  out << "Zfb::AsMap<\"" << name << "\", ZfFieldTC::"
    << ZfFieldTC::Map::v2s(memberCode(m_schema, o.fields[0]))
    << ", ZfFieldTC::"
    << ZfFieldTC::Map::v2s(memberCode(m_schema, o.fields[1]));
  for (const auto &f : o.fields) {
    out << ", ZuTypeList<";
    bool comma = false;
    auto prop = [&out, &comma](ZuCSpan text) {
      if (comma) out << ", ";
      out << "ZuFieldProp::" << text;
      comma = true;
    };
    Text p;
    if (auto deflt = defaultExpr(m_schema, f)) {
      p << "Deflt<" << deflt << '>';
      prop(p);
    }
    if (f.required) prop("Required");
    if (f.type.target >= 0 && m_schema.objects[f.type.target].kind == Decl::Enum) {
      const auto &en = m_schema.objects[f.type.target];
      p.null(); p << (en.flags ? "Flags<" : "Enum<") << "::" << cppName(en.name)
        << "::Map>";
      prop(p);
    }
    if (f.align) {
      p.null(); p << "FB::Align<" << ZuBox<unsigned>{f.align} << '>';
      prop(p);
    }
    out << '>';
  }
  out << '>';
  return out;
}

Text Emit::mapValue(const Field &f, ZuCSpan member) const
{
  Text out;
  if (mapVector(m_schema, f)) {
    const auto &target = m_schema.objects[f.type.target];
    if (m_opts.ref) out << "ZmRef<";
    out << "::";
    if (target.ns) out << cppName(target.ns) << "::";
    out << "Zflatc_::" << target.local << "::Map_<HeapID_ + \"." << member << "\"_z>";
    if (m_opts.ref) out << '>';
  } else if (f.type.vector) {
    Text elem;
    if (f.type.code == ZfFieldTC::String)
      elem << "ZtString<ZtStringHeapID<HeapID_ + \"." << member << ".elem\"_z>>";
    else elem = elemType(f);
    out << "ZtArray<" << elem << ", ZtArrayHeapID<HeapID_ + \"." << member
      << "\"_z>>";
  } else if (f.type.code == ZfFieldTC::String)
    out << "ZtString<ZtStringHeapID<HeapID_ + \"." << member << "\"_z>>";
  else out = elemType(f);
  return out;
}

void Emit::map(Text &out, const Object &o) const
{
  open(out, o);
  out << "namespace Zflatc_::" << o.local << " {\n\n"
    << "template <ZuString HeapID_>\nstruct Types_ {\n"
    << "  using Key = " << mapValue(o.fields[0], "key") << ";\n"
    << "  using Val = " << mapValue(o.fields[1], "value") << ";\n};\n\n"
    << "template <ZuString HeapID_>\n"
    << "struct Map_ : public ZmNHeap<Map_<HeapID_>>";
  if (m_opts.ref) out << ", public ::" << m_opts.ref;
  out << ", public ZmRBTreeKV<typename Types_<HeapID_>::Key, "
    << "typename Types_<HeapID_>::Val, ZmRBTreeHeapID<HeapID_>> {\n"
    << "  using Key = Types_<HeapID_>::Key;\n"
    << "  using Val = Types_<HeapID_>::Val;\n"
    << "friend inline ZuStringT<HeapID_ + \".Obj\"_z> ZmNHeapID(Map_ *);\n"
    << "  ZuDerive_(Map_, (ZmRBTreeKV<Key, Val, ZmRBTreeHeapID<HeapID_>>));\n"
    << "};\n\n} // Zflatc_::" << o.local << '\n';
  close(out, o);
}

void Emit::metadata(Text &out, const Object &o) const
{
  out << "ZfStruct(" << m_opts.api << ", (" << o.local << ", FB)";
  for (unsigned i = 0; i < o.fields.length(); ++i) {
    const auto &f = o.fields[i];
    out << ",\n  (";
    if (f.deprecated) {
      Text type = memberType(f);
      if (type == typeName(f)) type = containerType(o, f);
      out << '(' << f.cpp << ", Skip, " << type << ')';
    }
    else out << f.cpp;
    out << ", ";
    Text props;
    bool comma = false;
    auto prop = [&props, &comma](ZuCSpan p) {
      if (comma) props << ", ";
      props << p;
      comma = true;
    };
    Text p;
    if (o.kind == Decl::Table) {
      p << "FB::FieldID<" << ZuBox<unsigned>{f.id} << '>';
      prop(p);
    }
    Text deflt = defaultExpr(m_schema, f);
    if (deflt) {
      p.null(); p << "Deflt<" << deflt << '>';
      prop(p);
    }
    if (f.required) prop("Required");
    if (f.key) prop("FB::Key");
    if (f.type.target >= 0 && m_schema.objects[f.type.target].kind == Decl::Enum) {
      const auto &en = m_schema.objects[f.type.target];
      p.null(); p << (en.flags ? "Flags<" : "Enum<") << "::" << cppName(en.name) << "::Map>";
      prop(p);
    }
    unsigned align = f.align;
    if (o.kind == Decl::Struct && !i && o.align > align) align = o.align;
    if (align) {
      p.null(); p << "FB::Align<" << ZuBox<unsigned>{align} << '>';
      prop(p);
    }
    ZtCase::camelSnake(f.cpp, [&](ZuCSpan wire) {
      if (wire == f.name) return;
      p.null(); p << "FB::ID<\"" << f.name << "\">";
      prop(p);
    });
    if (comma) out << '(' << props << ')';
    out << ", " << ZfFieldTC::Map::v2s(memberCode(m_schema, f)) << ')';
  }
  out << ");\n";
}

void Emit::object(Text &out, const Object &o) const
{
  open(out, o);
  // Helpers are scoped by the public type, so separate schema files cannot
  // reuse an index-based helper name in their shared C++ namespace.
  ZuCSpan base = "O_";
  out << "namespace Zflatc_::" << o.local << " {\n\n"
    << "struct ";
  if (m_opts.api) out << m_opts.api << ' ';
  out << base << " : public ZmNHeap<" << base << ">";
  if (m_opts.ref) out << ", public ::" << m_opts.ref;
  out << " {\nfriend inline ZuStringT<\"" << o.name
    << "\"> ZmNHeapID(" << base << " *);\n\n";
  containers(out, o);
  for (unsigned i = 0; i < o.fields.length(); ++i) {
    const auto &f = o.fields[i];
    if (f.deprecated) continue;
    out << "  " << memberType(f) << ' ' << f.cpp;
    auto deflt = defaultExpr(m_schema, f);
    if (deflt) out << " = " << deflt;
    out << ";\n";
  }
  // Reference bases have no copy/move operation. Inline values copy their
  // payload into a fresh base and preserve the destination's reference count.
  if (m_opts.ref && o.kind == Decl::Struct) {
    out << '\n' << "  " << base << "() = default;\n";
    for (unsigned move = 0; move < 2; ++move) {
      ZuCSpan arg = move ? " &&" : " const &";
      out << "  " << base << '(' << base << arg << "v)";
      for (unsigned i = 0; i < o.fields.length(); ++i) {
	const auto &f = o.fields[i];
	out << (i ? ", " : " : ") << f.cpp << '{';
	if (move) out << "ZuMv(";
	out << "v." << f.cpp;
	if (move) out << ')';
	out << '}';
      }
      out << " { }\n  " << base << " &operator =(" << base << arg << "v) {\n";
      for (unsigned i = 0; i < o.fields.length(); ++i) {
	const auto &f = o.fields[i];
	out << "    this->" << f.cpp << " = ";
	if (move) out << "ZuMv(";
	out << "v." << f.cpp;
	if (move) out << ')';
	out << ";\n";
      }
      out << "    return *this;\n  }\n";
    }
  }
  out << "};\n"
    << "\n} // Zflatc_::" << o.local << "\n\nstruct ";
  if (m_opts.api) out << m_opts.api << ' ';
  Text impl;
  impl << "Zflatc_::" << o.local << "::";
  out << o.local << " : public " << impl << base << " {\n"
    << "  ZuDerive_(" << o.local << ", (" << impl << base << "));\n};\n";
  close(out, o);
  for (unsigned i = 0; i < o.fields.length(); ++i) {
    const auto &f = o.fields[i];
    if (!f.type.count) continue;
    out << "namespace Zfb {\ntemplate <> struct ArrayCount<" << containerType(o, f)
      << "> : public ZuUnsigned<"
      << ZuBox<unsigned>{f.type.count} << "> { };\n}\n\n";
  }
  open(out, o);
  metadata(out, o);
  close(out, o);
}

void Emit::root(Text &out, const Object &o) const
{
  Text guard{"Zflatc_Root_"};
  guard << o.name;
  for (unsigned i = 0; i < guard.length(); ++i)
    if (guard[i] == '.') guard[i] = '_';
  out << "#ifndef " << guard << "\n#define " << guard << "\n\n";
  open(out, o);
  Text base;
  if (o.builtin == ZfFieldTC::Bytes)
    base << "ZtArray<::uint8_t, ZtArrayHeapID<\"" << o.name << ".data\">>";
  else base = "ZtBitmap";
  out << "namespace Zflatc_::" << o.local << " {\n\n"
    << "struct O_ : public ZmNHeap<O_>, public " << base;
  if (m_opts.ref) out << ", public ::" << m_opts.ref;
  out << " {\nfriend inline ZuStringT<\"" << o.name
    << "\"> ZmNHeapID(O_ *);\n\n"
    << "  ZuDerive_(O_, (" << base << "));\n};\n\n"
    << "} // Zflatc_::" << o.local << "\n\nstruct ";
  if (m_opts.api) out << m_opts.api << ' ';
  Text impl; impl << "Zflatc_::" << o.local << "::O_";
  out << o.local << " : public " << impl << " {\n  ZuDerive_(" << o.local
    << ", (" << impl << "));\n};\n"
    << "Zfb::AsMapped<Zfb::Values::";
  if (o.builtin == ZfFieldTC::Bytes) out << "BytesMap";
  else out << "BitmapMap<ZtBitmap>";
  out << "> Zfb_Fmt(" << o.local << " *);\n";
  close(out, o);
  out << "#endif // " << guard << "\n\n";
}

Text Emit::header()
{
  Text out;
  out << modeline << "// Generated by zflatc from " << ZiFile::leafname(m_schema.files[m_file].path)
    << ".\n\n";
  // Encode the complete output path injectively: separate directories and
  // punctuation must not collapse to the same header guard.
  Text guard{"Zflatc_"};
  auto hex = [](unsigned n) { return char(n < 10 ? '0' + n : 'A' + n - 10); };
  for (auto c : m_opts.output) {
    unsigned v = uint8_t(c);
    if (c != '_' && (alpha(c) || digit(c))) guard << c;
    else guard << '_' << hex(v >> 4) << hex(v & 15);
  }
  guard << (m_opts.alt ? "_HPP" : "_HH");
  out << "#ifndef " << guard << "\n#define " << guard << "\n\n"
    << "#include <stdint.h>\n\n"
    << "#include <zlib/ZuDateTime.hh>\n#include <zlib/ZuDecimal.hh>\n#include <zlib/ZuDerive.hh>\n"
    << "#include <zlib/ZuFixed.hh>\n#include <zlib/ZuFP.hh>\n#include <zlib/ZuPtr.hh>\n"
    << "#include <zlib/ZuTime.hh>\n#include <zlib/ZuUnion.hh>\n\n"
    << "#include <zlib/ZmHeap.hh>\n";
  if (m_opts.ref) {
    if (refHeader(m_opts.ref))
      out << "#include <zlib/" << m_opts.ref << ".hh>\n";
    out << "#include <zlib/ZmRef.hh>\n";
  }
  for (const auto &o : m_schema.objects)
    if (own(o) && mapEntry(m_schema, o)) {
      out << "#include <zlib/ZmRBTree.hh>\n";
      break;
    }
  out << "\n#include <zlib/ZtArray.hh>\n#include <zlib/ZtBitmap.hh>\n#include <zlib/ZtEnum.hh>\n"
    << "#include <zlib/ZtString.hh>\n\n#include <zlib/Zfb.hh>\n#include <zlib/ZiIP.hh>\n";
  const auto &file = m_schema.files[m_file];
  for (unsigned i = 0; i < file.includes.length(); ++i) {
    auto name = file.includes[i].name;
    if (name.length() < 4 || ZuCSpan{name.data() + name.length() - 4, 4} != ".fbs")
      fail(file.includes[i].loc, "schema include requires .fbs extension");
    name.length(name.length() - 4);
    name << (m_opts.alt ? ".hpp" : ".hh");
    out << "#include \"" << name << "\"\n";
  }
  out << '\n';
  for (const auto &o : m_schema.objects)
    if (mappedRoot(o)) root(out, o);
  // All object ADL declarations precede metadata instantiation. Table and
  // union references stay shallow, including mutually recursive tables.
  for (unsigned i = 0; i < m_schema.objects.length(); ++i) {
    const auto &o = m_schema.objects[i];
    if (!own(o) || o.kind == Decl::Union) continue;
    open(out, o);
    if (o.kind == Decl::Enum) {
      out << "namespace " << o.local << " {\n  " << (o.flags ? "ZtFlags(" : "ZtEnum(")
	<< m_opts.api << ", " << o.local << ", " << nativeCode(o.code);
      for (unsigned v = 0; v < o.values.length(); ++v) out << ", " << o.values[v].name;
      out << ");\n  Zfb::AsEnum<\"" << o.name << "\"> Zfb_Fmt(Map *);\n}\n";
    } else if (o.kind == Decl::Table || o.kind == Decl::Struct) {
      out << "struct ";
      if (m_opts.api) out << m_opts.api << ' ';
      out << o.local << ";\nZfb::" << (o.kind == Decl::Table ? "AsTable" : "AsStruct")
	<< "<\"" << o.name << "\"> Zfb_Fmt(" << o.local << " *);\n";
    }
    close(out, o);
  }
  for (const auto &o : m_schema.objects) {
    if (!own(o) || !mapEntry(m_schema, o)) continue;
    open(out, o);
    out << "namespace Zflatc_::" << o.local << " {\n"
      << "template <ZuString HeapID_> struct Map_;\n"
      << "template <ZuString HeapID_> " << mapFormat(o)
      << " Zfb_Fmt(Map_<HeapID_> *);\n"
      << "} // Zflatc_::" << o.local << '\n';
    close(out, o);
  }
  auto order = m_schema.order();
  for (unsigned i = 0; i < order.length(); ++i) {
    const auto &o = m_schema.objects[order[i]];
    if (own(o) && o.kind == Decl::Struct) object(out, o);
  }
  for (unsigned i = 0; i < m_schema.objects.length(); ++i) {
    const auto &o = m_schema.objects[i];
    if (!own(o) || o.kind != Decl::Union) continue;
    open(out, o);
    bool bytes = ownsBytes(m_schema, o);
    if (bytes) {
      out << "namespace Zflatc_::" << o.local << " {\n\n"
	<< "template <ZuString HeapID_>\nstruct B_ : public ZtArray<::uint8_t, ZtArrayHeapID<HeapID_>> {\n"
	<< "  ZuDerive_(B_, (ZtArray<::uint8_t, ZtArrayHeapID<HeapID_>>));\n"
	<< "  friend Zfb::Values::BytesMap ZfbTransform(B_ *);\n};\n\n"
	<< "} // Zflatc_::" << o.local << "\n\n"
	<< "template <ZuString HeapID_ = \"" << o.name << "\">\n";
    }
    out << "struct ";
    if (m_opts.api) out << m_opts.api << ' ';
    out << o.local << " : public ZuUnion<void";
    Text args;
    for (unsigned v = 0; v < o.values.length(); ++v) {
      args << ", " << altType(o, o.values[v]);
    }
    out << args << "> {\n  ZuDerive_(" << o.local << ", (ZuUnion<void" << args << ">));\n};\n";
    if (bytes) out << "template <ZuString HeapID_>\n";
    out << "Zfb::AsUnion<\"" << o.name << '"';
    for (unsigned v = 0; v < o.values.length(); ++v) {
      const auto &a = o.values[v];
      out << ",\n  Zfb::Alt<\"" << a.name << "\", " << altType(o, a) << '>';
    }
    out << "> Zfb_Fmt(" << o.local;
    if (bytes) out << "<HeapID_>";
    out << " *);\n";
    close(out, o);
  }
  for (unsigned i = 0; i < order.length(); ++i) {
    const auto &o = m_schema.objects[order[i]];
    if (own(o) && mapEntry(m_schema, o)) object(out, o);
  }
  for (const auto &o : m_schema.objects) {
    if (own(o) && mapEntry(m_schema, o)) map(out, o);
  }
  for (unsigned i = 0; i < order.length(); ++i) {
    const auto &o = m_schema.objects[order[i]];
    if (own(o) && o.kind == Decl::Table && !mapEntry(m_schema, o)) object(out, o);
  }
  out << "#endif // " << guard << '\n';
  return out;
}

Text Emit::source(ZuCSpan header)
{
  Text out;
  out << modeline << "// Generated by zflatc.\n\n#include \"" << header << "\"\n\n";
  for (unsigned i = 0; i < m_schema.objects.length(); ++i) {
    const auto &o = m_schema.objects[i];
    if (!own(o) || o.kind == Decl::Union) continue;
    open(out, o);
    if (o.kind == Decl::Enum)
      out << "namespace " << o.local << " { ZtEnumImpl(" << o.local << "); }\n";
    else out << "ZfStructImpl(" << o.local << ");\nZfStructImpl(" << o.local << ", FB);\n";
    close(out, o);
  }
  return out;
}

struct Output {
  Zi::Path dst, tmp, bak;
  bool held = false, installed = false;
};

// Stage the complete pair before moving any old destination. The backup files
// remain available if rollback itself fails; two paths cannot be crash-atomic.
template <typename File = ZiFile>
class Publish {
public:
  ~Publish() {
    for (unsigned i = 0; i < m_files.length(); ++i) {
      auto &f = m_files[i];
      if (f.tmp) File::remove(f.tmp);
      if (f.bak && !f.held) File::remove(f.bak);
    }
  }
  void stage(Zi::Path dst, ZuCSpan data) {
    if (ZiStat{dst}.isdir()) fail({}, "output destination is a directory");
    auto &f = *new (m_files.push()) Output{ZuMv(dst), {}, {}};
    File file;
    f.tmp = reserve(f.dst, file);
    if (file.write(data.data(), data.length()) != Zi::OK ||
	file.sync() != Zi::OK) fail({}, "cannot write staged output");
    if (file.close() != Zi::OK) fail({}, "cannot close staged output");
    if (ZiStat{f.dst}.exists()) {
      f.bak = reserve(f.dst, file);
      if (file.close() != Zi::OK) fail({}, "cannot close output backup");
    }
  }
  void commit() {
    try {
      for (unsigned i = 0; i < m_files.length(); ++i) {
	auto &f = m_files[i];
	if (!f.bak) continue;
	if (File::rename(f.dst, f.bak) != Zi::OK)
	  fail({}, "cannot preserve previous output");
	f.held = true;
      }
      for (unsigned i = 0; i < m_files.length(); ++i) {
	auto &f = m_files[i];
	if (File::rename(f.tmp, f.dst) != Zi::OK)
	  fail({}, "cannot replace output");
	f.installed = true;
	f.tmp.null();
      }
    } catch (Failure &e) {
      for (unsigned i = m_files.length(); i; --i) {
	auto &f = m_files[i - 1];
	if (f.installed && File::remove(f.dst) != Zi::OK)
	  e.message << "; cannot remove partial output " << f.dst;
	if (f.held) {
	  if (File::rename(f.bak, f.dst) == Zi::OK) f.held = false;
	  else e.message << "; previous output retained at " << f.bak;
	}
      }
      throw;
    }
    for (unsigned i = 0; i < m_files.length(); ++i) {
      auto &f = m_files[i];
      if (f.held) {
	if (File::remove(f.bak) != Zi::OK)
	  fail({}, "cannot remove previous-output backup");
	f.held = false;
	f.bak.null();
      }
    }
  }

private:
  Zi::Path reserve(const Zi::Path &dst, File &file) {
    for (;;) {
      Text name;
      name << dst << ".zflatc." << ZuBox<Zm::ProcessID>{Zm::getPID()} << '.'
	<< ZuBox<unsigned>{m_seq++};
      Zi::Path path{name};
      if (file.open(path, File::WriteOnly | File::Create |
	  File::Exclusive | File::GC) == Zi::OK) return path;
      if (!ZiStat{path}.exists() || !m_seq) fail({}, "cannot stage output");
    }
  }

  Items<Output> m_files;
  unsigned m_seq = 0;
};

static bool cli(int argc, char **argv, Options &opts, Schema &schema)
{
  unsigned n;
  try { n = ZfCLI::load(opts, argc, argv); }
  catch (const ZeException &e) {
    Text message;
    message << e;
    fail({}, message);
  }
  if (opts.help) return false;
  if (n != 2) fail({}, "expected one input schema");
  if (opts.api) identifier(opts.api, {});
  if (opts.ref) {
    Text name;
    ZuCSpan in{opts.ref};
    unsigned i = 0;
    if (in.length() > 2 && in[0] == ':' && in[1] == ':') i = 2;
    for (; i < in.length(); ++i) {
      if (in[i] != ':') { name << in[i]; continue; }
      if (++i == in.length() || in[i] != ':') fail({}, "invalid reference base");
      name << '.';
    }
    qualID(name, {});
    opts.ref = cppName(name);
  }
  for (unsigned i = 0; i < opts.includes.length(); ++i)
    schema.includeDirs.push(Zi::Path{opts.includes[i]});
  if (!opts.input) fail({}, "missing input schema");
  Text input{opts.input};
  if (input.length() < 4 || ZuCSpan{input.data() + input.length() - 4, 4} != ".fbs")
    fail({}, "input requires .fbs extension");
  if (!opts.output) {
    input.length(input.length() - 4);
    opts.output = input;
  }
  return true;
}

#ifndef ZFLATC_TEST
int main(int argc, char **argv)
{
  Schema schema;
  Options opts;
  ZiFile err;
  err.openStdErr();
  try {
    if (!cli(argc, argv, opts, schema)) {
      ZuCSpan usage = "usage: zflatc [--alt] [--ref <type>] [--export <macro>] "
	"[-I <dir>] [-o <stem>] <name>.fbs\n";
      ZiFile out;
      out.openStdOut();
      return out.write(usage.data(), usage.length()) == Zi::OK ? 0 : 1;
    }
    unsigned file = schema.read(opts.input);
    schema.resolve();
    Emit emit{schema, file, opts};
    Text hh{opts.output}, cc{opts.output};
    hh << (opts.alt ? ".hpp" : ".hh");
    cc << (opts.alt ? ".cpp" : ".cc");
    Text header = emit.header();
    Text source = emit.source(Text{ZiFile::leafname(Zi::Path{hh})});
    Publish<> publish;
    publish.stage(Zi::Path{hh}, header);
    publish.stage(Zi::Path{cc}, source);
    publish.commit();
    return 0;
  } catch (const Failure &e) {
    Text message;
    if (e.loc.file < schema.files.length()) {
      const auto &file = schema.files[e.loc.file];
      unsigned line = 1, column = 1;
      for (unsigned i = 0; i < e.loc.offset && i < file.text.length(); ++i) {
	if (file.text[i] == '\n') { ++line; column = 1; }
	else ++column;
      }
      message << file.path << ':' << ZuBox<unsigned>{line} << ':'
	<< ZuBox<unsigned>{column} << ": ";
    } else message << "zflatc: ";
    message << e.message << '\n';
    err.write(message.data(), message.length());
    return 1;
  }
}
#endif
