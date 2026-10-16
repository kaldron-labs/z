//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Protocol Buffers schema compiler. Parsing and descriptor validation precede
// emission; generated output is staged before either destination is replaced.

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUTF.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRBTree.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZpbReflect.hh>

using namespace Zpb::Reflect;
namespace Zpb::Reflect::Syntax {
ZtEnumMap(, Syntax, CLIMap, "proto2", "proto3");
ZtEnumImpl(Syntax, CLIMap);
}

ZuDerive(Text, (ZtString<ZtStringHeapID<"zprotoc.Text">>));
ZuDerive(Input,
  (ZtString<ZtStringBuiltin<0, ZtStringHeapID<"zprotoc.Input">>>));
ZuDerive(Inputs, (ZtArray<Input, ZtArrayHeapID<"zprotoc.Inputs">>));
struct SyntheticText : ZmNHeap<SyntheticText>, Input {
friend inline ZuStringT<"zprotoc.SyntheticText"> ZmNHeapID(SyntheticText *);
  explicit SyntheticText(ZuCSpan text) : Input{text} { }
};

using Synthetic = ZtArray<ZuPtr<SyntheticText>,
  ZtArrayHeapID<"zprotoc.Synthetic">>;
ZuDerive(Output, (ZtArray<uint8_t, ZtArrayHeapID<"zprotoc.Output">>));
ZuDerive(Includes, (ZtArray<Text, ZtArrayHeapID<"zprotoc.Includes">>));

struct Failure { unsigned offset; Text message; };
[[noreturn]] static void fail(unsigned offset, ZuCSpan message)
{
  throw Failure{offset, Text{message}};
}

static bool alpha(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool digit(char c) { return c >= '0' && c <= '9'; }

namespace Tok { enum { End = 256, Name, Number, String }; }
struct Token {
  unsigned	kind;
  ZuCSpan	text;
  unsigned	offset;
};

struct ImportQualifiers {
  using Keys = ZuStringTL<"public", "weak">;
};
struct FileDecls {
  using Keys = ZuStringTL<"service", "extend", "option">;
};
struct FileStringOpts {
  using Keys = ZuStringTL<"java_package", "java_outer_classname",
    "go_package", "objc_class_prefix", "csharp_namespace", "swift_prefix",
    "php_class_prefix", "php_namespace", "php_metadata_namespace",
    "ruby_package">;
};
struct FileBoolOpts {
  using Keys = ZuStringTL<"java_multiple_files",
    "java_generate_equals_and_hash", "java_string_check_utf8",
    "cc_generic_services", "java_generic_services", "py_generic_services",
    "deprecated", "cc_enable_arenas">;
};
struct OptimizeModes {
  using Keys = ZuStringTL<"SPEED", "CODE_SIZE", "LITE_RUNTIME">;
};
struct MessageDecls {
  using Keys = ZuStringTL<"extensions", "extend", "option">;
};
struct BoolValues {
  using Keys = ZuStringTL<"true", "false">;
};
struct FloatInfinities {
  using Keys = ZuStringTL<"inf", "+inf", "-inf">;
};
struct EnumReserved {
  using Keys = ZuStringTL<"T", "Map">;
};

template <typename Set>
static int fixedName(ZuCSpan value)
{
  static constexpr auto matcher = ZuMatcher<Set>();
  return matcher.exact(value);
}

struct ParseOneofKey {
  const MsgDesc	*message;
  unsigned	offset;

  static ZuCSpan key(const ParseOneofKey &v) {
    return v.message->oneofs[v.offset].name;
  }
};
using ParseOneofIndex = ZmRBTree<ParseOneofKey,
  ZmRBTreeKey<ParseOneofKey::key, ZmRBTreeUnique<true,
    ZmRBTreeHeapID<"zprotoc.ParseOneof">>>>;

struct ParseFieldKey {
  const MsgDesc	*message;
  unsigned	offset;

  static ZuCSpan key(const ParseFieldKey &v) {
    return v.message->fields[v.offset].name;
  }
};
using ParseFieldIndex = ZmRBTree<ParseFieldKey,
  ZmRBTreeKey<ParseFieldKey::key, ZmRBTreeUnique<true,
    ZmRBTreeHeapID<"zprotoc.ParseField">>>>;

struct ParseNames {
  MsgDesc		&message;
  ParseOneofIndex oneofs;
  ParseFieldIndex fields;

  void checkField(Token name) {
    if (oneofs.find(name.text))
      fail(name.offset, "field collides with oneof name");
  }

  void checkOneof(Token name) {
    if (fields.find(name.text))
      fail(name.offset, "field collides with oneof name");
    if (oneofs.find(name.text)) fail(name.offset, "duplicate oneof name");
  }

  void addOneof() {
    unsigned i = message.oneofs.length() - 1;
    oneofs.add(ParseOneofKey{&message, i});
  }

  void addField() {
    unsigned i = message.fields.length() - 1;
    if (!fields.find(message.fields[i].name))
      fields.add(ParseFieldKey{&message, i});
  }
};

class Scan {
public:
  explicit Scan(ZuCSpan input) : m_input{input} { }

  Token next() {
    unsigned n = m_input.length();
    for (;;) {
      while (m_pos < n && (m_input[m_pos] == ' ' ||
	m_input[m_pos] == '\t' || m_input[m_pos] == '\r' ||
	m_input[m_pos] == '\n')) ++m_pos;
      if (m_pos + 1 < n && m_input[m_pos] == '/' &&
	m_input[m_pos + 1] == '/') {
	m_pos += 2;
	while (m_pos < n && m_input[m_pos] != '\n') ++m_pos;
	continue;
      }
      if (m_pos + 1 < n && m_input[m_pos] == '/' &&
	m_input[m_pos + 1] == '*') {
	unsigned start = m_pos;
	m_pos += 2;
	while (m_pos + 1 < n &&
	  (m_input[m_pos] != '*' || m_input[m_pos + 1] != '/')) ++m_pos;
	if (m_pos + 1 >= n) fail(start, "unterminated comment");
	m_pos += 2;
	continue;
      }
      break;
    }
    unsigned start = m_pos;
    if (start == n) return {Tok::End, {}, start};
    char c = m_input[m_pos++];
    if (alpha(c)) {
      while (m_pos < n && (alpha(m_input[m_pos]) || digit(m_input[m_pos])))
	++m_pos;
      return {Tok::Name, {m_input.data() + start, m_pos - start}, start};
    }
    if (digit(c)) {
      while (m_pos < n && digit(m_input[m_pos])) ++m_pos;
      return {Tok::Number, {m_input.data() + start, m_pos - start}, start};
    }
    if (c == '"') {
      unsigned value = m_pos;
      while (m_pos < n && m_input[m_pos] != '"') {
	if (uint8_t(m_input[m_pos]) < ' ')
	  fail(m_pos, "unsupported string escape");
	if (m_input[m_pos] == '\\') {
	  if (m_pos + 1 >= n) fail(m_pos, "unterminated string escape");
	  m_pos += 2;
	} else ++m_pos;
      }
      if (m_pos == n) fail(start, "unterminated string");
      ZuCSpan text{m_input.data() + value, m_pos - value};
      ++m_pos;
      return {Tok::String, text, start};
    }
    return {uint8_t(c), {m_input.data() + start, 1}, start};
  }

private:
  ZuCSpan m_input;
  unsigned m_pos = 0;
};

static int hex(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool unescape(ZuCSpan raw, Text &out, bool unicode = false)
{
  for (unsigned i = 0, n = raw.length(); i < n; ++i) {
    char c = raw[i];
    if (c != '\\') { out << c; continue; }
    if (++i == n) return false;
    c = raw[i];
    switch (c) {
      case 'a': out << '\a'; break;
      case 'b': out << '\b'; break;
      case 'f': out << '\f'; break;
      case 'n': out << '\n'; break;
      case 'r': out << '\r'; break;
      case 't': out << '\t'; break;
      case 'v': out << '\v'; break;
      case '\\': case '\'': case '"': case '?': out << c; break;
      case 'x': {
	if (i + 1 == n || hex(raw[i + 1]) < 0) return false;
	unsigned value = 0, count = 0;
	while (i + 1 < n && count < 2 && hex(raw[i + 1]) >= 0) {
	  value = (value << 4) | unsigned(hex(raw[++i]));
	  ++count;
	}
	out << char(value);
	break;
      }
      case 'u': case 'U': {
	if (!unicode) return false;
	unsigned digits = c == 'u' ? 4 : 8;
	if (n - i - 1 < digits) return false;
	uint32_t value = 0;
	for (unsigned j = 0; j < digits; ++j) {
	  int part = hex(raw[++i]);
	  if (part < 0) return false;
	  value = (value << 4) | unsigned(part);
	}
	if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
	  return false;
	uint8_t bytes[4]; // one UTF-8 scalar occupies at most four bytes
	unsigned length = ZuUTF8::out(bytes, sizeof(bytes), value);
	if (!length) return false;
	for (unsigned j = 0; j < length; ++j) out << char(bytes[j]);
	break;
      }
      default:
	if (c < '0' || c > '7') return false;
	unsigned value = c - '0', count = 1;
	while (i + 1 < n && count < 3 && raw[i + 1] >= '0' &&
	  raw[i + 1] <= '7') {
	  value = (value << 3) | unsigned(raw[++i] - '0');
	  ++count;
	}
	if (value > 255) return false;
	out << char(value);
	break;
    }
  }
  return true;
}

static int type(ZuCSpan name)
{
  struct IDs {
    using Keys = ZuStringTL<"bool", "int32", "uint32", "int64", "uint64",
      "sint32", "sint64", "fixed32", "fixed64", "sfixed32", "sfixed64",
      "double", "float", "string", "bytes">;
  };
  static constexpr auto ids = ZuMatcher<IDs>();
  switch (ids.exact(name)) {
    case 0: return Type::Bool;
    case 1: return Type::Int32;
    case 2: return Type::UInt32;
    case 3: return Type::Int64;
    case 4: return Type::UInt64;
    case 5: return Type::SInt32;
    case 6: return Type::SInt64;
    case 7: return Type::Fixed32;
    case 8: return Type::Fixed64;
    case 9: return Type::SFixed32;
    case 10: return Type::SFixed64;
    case 11: return Type::Double;
    case 12: return Type::Float;
    case 13: return Type::String;
    case 14: return Type::Bytes;
    default: return 0;
  }
}

class Parser {
public:
  Parser(ZuCSpan input, Synthetic &synthetic) :
    m_scan{input}, m_synthetic{synthetic} { next(); }

  void run(FileDesc &file) {
    if (m_token.text == "edition")
      fail(m_token.offset, "unsupported edition schema");
    if (take("syntax")) {
      need('=');
      file.syntax = need(Tok::String).text;
      if (file.syntax != "proto2" && file.syntax != "proto3")
	fail(m_token.offset, "unsupported syntax");
      m_syntax = fileSyntax(file);
      need(';');
    } else file.syntax = "proto2";
    bool packaged = false, declared = false;
    while (m_token.kind != Tok::End) {
      unsigned offset = m_token.offset;
      if (take("package")) {
	if (packaged) fail(offset, "duplicate package");
	packaged = true;
	Token first = need(Tok::Name);
	const char *end = first.text.data() + first.text.length();
	while (take('.')) {
	  auto part = need(Tok::Name);
	  end = part.text.data() + part.text.length();
	}
	file.package = {first.text.data(), unsigned(end - first.text.data())};
	need(';');
	continue;
      }
      if (take("import")) {
	if (fixedName<ImportQualifiers>(m_token.text) >= 0)
	  fail(m_token.offset, "unsupported import qualifier");
	file.dependencies.push(need(Tok::String).text);
	need(';');
	continue;
      }
      if (take("option")) {
	Token name = m_token;
	if (name.kind != Tok::Name)
	  fail(name.offset, "unsupported file option");
	next();
	need('=');
	if (fixedName<FileStringOpts>(name.text) >= 0)
	  need(Tok::String);
	else if (fixedName<FileBoolOpts>(name.text) >= 0) {
	  if (m_token.kind != Tok::Name ||
	    fixedName<BoolValues>(m_token.text) < 0)
	    fail(m_token.offset, "invalid file option value");
	  next();
	} else if (name.text == "optimize_for") {
	  if (m_token.kind != Tok::Name ||
	    fixedName<OptimizeModes>(m_token.text) < 0)
	    fail(m_token.offset, "invalid file option value");
	  next();
	} else fail(name.offset, "unsupported file option");
	need(';');
	continue;
      }
      if (fixedName<FileDecls>(m_token.text) >= 0)
	fail(m_token.offset, "unsupported file declaration");
      if (take("enum")) {
	EnumDesc enumeration;
	parseEnum(enumeration);
	file.enums.push(ZuMv(enumeration));
	declared = true;
	continue;
      }
      word("message");
      MsgDesc message;
      parseMessage(message);
      file.messages.push(ZuMv(message));
      declared = true;
    }
    if (!declared) fail(m_token.offset, "expected declaration");
  }

private:
  void parseEnum(EnumDesc &enumeration) {
    enumeration.name = need(Tok::Name).text;
    need('{');
    while (m_token.kind != '}') {
      if (m_token.kind == Tok::End) fail(m_token.offset, "unterminated enum");
      if (m_token.text == "option")
	fail(m_token.offset, "unsupported enum option");
      EnumValueDesc value;
      value.name = need(Tok::Name).text;
      need('=');
      Text number;
      if (take('-')) number << '-';
      auto token = need(Tok::Number);
      number << token.text;
      if (ZuBoxed(value.number).scan(number) != int(number.length()))
	fail(token.offset, "invalid enum number");
      need(';');
      enumeration.values.push(ZuMv(value));
    }
    need('}');
    take(';');
  }

  void parseMessage(MsgDesc &message) {
    message.name = need(Tok::Name).text;
    need('{');
    ParseNames names{message, {}, {}};
    while (m_token.kind != '}') {
      if (m_token.kind == Tok::End) fail(m_token.offset, "unterminated message");
      if (fixedName<MessageDecls>(m_token.text) >= 0)
	fail(m_token.offset, "unsupported message declaration");
      if (take("message")) {
	ZuPtr<MsgDesc> nested{new MsgDesc};
	parseMessage(*nested);
	message.nested.push(ZuMv(nested));
	continue;
      }
      if (take("enum")) {
	EnumDesc enumeration;
	parseEnum(enumeration);
	message.enums.push(ZuMv(enumeration));
	continue;
      }
      if (take("reserved")) {
	parseReserved(message);
	continue;
      }
      if (take("oneof")) {
	OneofDesc oneof;
	auto name = need(Tok::Name);
	oneof.name = name.text;
	names.checkOneof(name);
	int32_t index = message.oneofs.length();
	message.oneofs.push(ZuMv(oneof));
	names.addOneof();
	need('{');
	while (m_token.kind != '}') {
	  if (m_token.kind == Tok::End) fail(m_token.offset, "unterminated oneof");
	  parseField(message, index, names);
	}
	need('}');
	take(';');
	continue;
      }
      parseField(message, ZuCmp<int32_t>::null(), names);
    }
    need('}');
    take(';');
    if (m_syntax == Syntax::Proto3)
      for (auto &field : message.fields) if (field.proto3Optional) {
	Text name;
	name << '_' << field.name;
	m_synthetic.push(ZuPtr<SyntheticText>{new SyntheticText{name}});
	field.oneofIndex = message.oneofs.length();
	message.oneofs.push(OneofDesc{
	  *m_synthetic[m_synthetic.length() - 1]});
      }
  }

  void parseReserved(MsgDesc &message) {
    if (m_token.kind == Tok::String) {
      do {
	message.reservedNames.push(need(Tok::String).text);
      } while (take(','));
    } else {
      do {
	auto token = need(Tok::Number);
	uint32_t start;
	if (ZuBoxed(start).scan(token.text) != int(token.text.length()) ||
	  !Zpb::Wire::fieldNumber(start))
	  fail(token.offset, "invalid reserved number");
	uint32_t end = start;
	if (take("to")) {
	  if (take("max")) end = 536870911;
	  else {
	    token = need(Tok::Number);
	    if (ZuBoxed(end).scan(token.text) != int(token.text.length()) ||
	      !Zpb::Wire::fieldNumber(end))
	      fail(token.offset, "invalid reserved number");
	  }
	}
	if (end < start) fail(token.offset, "invalid reserved range");
	message.reservedRanges.push(ReservedRange{int32_t(start),
	  int32_t(end + 1)});
      } while (take(','));
    }
    need(';');
  }

  void parseField(MsgDesc &message, int32_t oneof, ParseNames &names) {
    if (take("map")) {
      if (oneof != ZuCmp<int32_t>::null())
	fail(m_token.offset, "map field in oneof");
      need('<');
      auto keyName = need(Tok::Name).text;
      need(',');
      Token valueKind = m_token;
      if (m_token.kind == '.') next();
      auto last = need(Tok::Name);
      const char *end = last.text.data() + last.text.length();
      while (take('.')) {
	auto part = need(Tok::Name);
	end = part.text.data() + part.text.length();
      }
      need('>');
      FieldDesc field;
      auto fieldName = need(Tok::Name);
      names.checkField(fieldName);
      field.name = fieldName.text;
      field.label = 3;
      field.type = Type::Message;
      need('=');
      auto number = need(Tok::Number);
      uint32_t id = 0;
      if (ZuBoxed(id).scan(number.text) != int(number.text.length()) ||
	!Zpb::Wire::fieldNumber(id))
	fail(number.offset, "invalid field number");
      field.number = id;
      need(';');
      Text name;
      bool upper = true;
      for (char c : field.name) {
	if (c == '_') { upper = true; continue; }
	name << char(upper && c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
	upper = false;
      }
      name << "Entry";
      m_synthetic.push(ZuPtr<SyntheticText>{new SyntheticText{name}});
      auto entry = ZuPtr<MsgDesc>{new MsgDesc};
      entry->name = *m_synthetic[m_synthetic.length() - 1];
      entry->options = new MsgOpts{true};
      FieldDesc key;
      key.name = "key";
      key.number = 1;
      key.label = 1;
      key.type = type(keyName);
      FieldDesc value;
      value.name = "value";
      value.number = 2;
      value.label = 1;
      ZuCSpan valueName{valueKind.text.data(),
	unsigned(end - valueKind.text.data())};
      value.type = type(valueName);
      if (!value.type) {
	value.type = Type::Message;
	value.typeName = valueName;
      }
      entry->fields.push(ZuMv(key));
      entry->fields.push(ZuMv(value));
      field.typeName = entry->name;
      message.fields.push(ZuMv(field));
      names.addField();
      message.nested.push(ZuMv(entry));
      return;
    }
    unsigned label = 1;
    bool explicitLabel = false;
    bool optional = take("optional");
    if (optional) explicitLabel = true;
    else if (take("required")) { label = 2; explicitLabel = true; }
    else if (take("repeated")) { label = 3; explicitLabel = true; }
    if (oneof != ZuCmp<int32_t>::null() && explicitLabel)
      fail(m_token.offset, "unsupported oneof field label");
    if (oneof == ZuCmp<int32_t>::null() &&
      (m_syntax == Syntax::Proto2 ? !explicitLabel :
	 explicitLabel && label != 3 && !optional))
      fail(m_token.offset, "unsupported field label");
    if (m_token.text == "group")
      fail(m_token.offset, "unsupported group field");
    Token kind = m_token;
    if (m_token.kind == '.') next();
    auto last = need(Tok::Name);
    const char *end = last.text.data() + last.text.length();
    while (take('.')) {
      auto part = need(Tok::Name);
      end = part.text.data() + part.text.length();
    }
    FieldDesc field;
    ZuCSpan typeName{kind.text.data(), unsigned(end - kind.text.data())};
    field.type = type(typeName);
    if (!field.type) {
      field.type = Type::Message;
      field.typeName = typeName;
    }
    auto fieldName = need(Tok::Name);
    names.checkField(fieldName);
    field.name = fieldName.text;
    field.label = label;
    field.proto3Optional = optional && m_syntax == Syntax::Proto3;
    need('=');
    auto number = need(Tok::Number);
    uint32_t id = 0;
    if (ZuBoxed(id).scan(number.text) != int(number.text.length()) ||
      !Zpb::Wire::fieldNumber(id)) fail(number.offset, "invalid field number");
    field.number = id;
    field.oneofIndex = oneof;
    if (take('[')) {
      if (take("packed")) {
	need('=');
	if (take("true")) field.options = packedOptionBytes(true);
	else if (take("false")) field.options = packedOptionBytes(false);
	else fail(m_token.offset, "invalid packed option");
	need(']');
      } else if (m_token.text != "default")
	fail(m_token.offset, "unsupported field option");
      else {
	word("default");
	need('=');
	if (m_token.kind == ']' || m_token.kind == Tok::End)
	  fail(m_token.offset, "unsupported field default");
	unsigned kind = m_token.kind;
	const char *begin = m_token.text.data();
	const char *end = begin;
	while (m_token.kind != ']' && m_token.kind != Tok::End) {
	  end = m_token.text.data() + m_token.text.length();
	  next();
	}
	field.defaultValue = {begin, unsigned(end - begin)};
	if (kind == Tok::String) {
	  if (m_token.kind != ']' || (field.type != Type::String &&
	    field.type != Type::Bytes))
	    fail(m_token.offset, "unsupported field default");
	  if (field.type == Type::String) {
	    Text decoded;
	    if (!unescape(field.defaultValue, decoded, true))
	      fail(m_token.offset, "invalid string default");
	    if (decoded.length()) {
	      m_synthetic.push(ZuPtr<SyntheticText>{new SyntheticText{decoded}});
	      field.defaultValue = *m_synthetic[m_synthetic.length() - 1];
	    } else field.defaultValue = {begin, 0};
	  }
	}
	need(']');
      }
    }
    need(';');
    message.fields.push(ZuMv(field));
    names.addField();
  }

  void next() { m_token = m_scan.next(); }
  bool take(unsigned kind) {
    if (m_token.kind != kind) return false;
    next();
    return true;
  }
  bool take(ZuCSpan name) {
    if (m_token.kind != Tok::Name || m_token.text != name) return false;
    next();
    return true;
  }
  Token need(unsigned kind) {
    Token token = m_token;
    if (token.kind != kind) fail(token.offset, "unexpected token");
    next();
    return token;
  }
  void word(ZuCSpan name) {
    if (!take(name)) fail(m_token.offset, "unexpected keyword");
  }

  Scan m_scan;
  Synthetic &m_synthetic;
  Token m_token;
  Syntax::T m_syntax = Syntax::Proto2;
};

struct Decl {
  const FileDesc	*file;
  const MsgDesc	*message;
  const EnumDesc	*enumeration;
  Text		proto, cpp, qualified;
  unsigned	ordinal;
};
struct Decls;
struct DeclKey {
  const Decls *decls;
  unsigned offset;
  ZuCSpan query;

  ZuCSpan id() const;
  int cmp(const DeclKey &r) const { return id().cmp(r.id()); }
};
using DeclIndex = ZmRBTree<DeclKey,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"zprotoc.DeclIndex">>>;
using EnumValueKey = ZuTuple<const EnumDesc *, ZuCSpan>;
using EnumValueIndex = ZmRBTreeKV<EnumValueKey, unsigned,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"zprotoc.EnumValueIndex">>>;

struct Decls : ZtArray<Decl, ZtArrayHeapID<"zprotoc.Decls">> {
  using Base = ZtArray<Decl, ZtArrayHeapID<"zprotoc.Decls">>;
  DeclIndex index;
  mutable ZuUnion<void, EnumValueIndex> enumValues;

  void push(Decl decl) {
    if (index.find(DeclKey{nullptr, UINT_MAX, decl.qualified}))
      fail(UINT_MAX, "duplicate protobuf declaration");
    unsigned offset = length();
    Base::push(ZuMv(decl));
    index.add(DeclKey{this, offset, {}});
  }

  const Decl *find(ZuCSpan name) const {
    auto found = index.find(DeclKey{nullptr, UINT_MAX, name});
    return found ? &(*this)[found->data().offset] : nullptr;
  }

  const EnumValueDesc *findValue(const EnumDesc &enumeration,
    ZuCSpan name) const {
    const auto &values = enumeration.values;
    if (!enumValues.is<EnumValueIndex>()) {
      new (enumValues.new_<EnumValueIndex>()) EnumValueIndex;
      auto &lookup = enumValues.p<EnumValueIndex>();
      for (const auto &decl : *this) if (decl.enumeration)
	for (unsigned i = 0, n = decl.enumeration->values.length(); i < n; ++i) {
	  const auto &value = decl.enumeration->values[i];
	  if (ZuNull(value.number)) continue;
	  EnumValueKey key{decl.enumeration, value.name};
	  if (!lookup.find(key)) lookup.add(key, i);
	}
    }
    auto found = enumValues.p<EnumValueIndex>().find(
      EnumValueKey{&enumeration, name});
    return found ? &values[found->val()] : nullptr;
  }
};

ZuCSpan DeclKey::id() const
{
  return offset == UINT_MAX ? query : ZuCSpan{(*decls)[offset].qualified};
}

struct FileKey {
  const DescSet *set;
  unsigned offset;
  ZuCSpan query;

  ZuCSpan id() const {
    return offset == UINT_MAX ? query : ZuCSpan{set->files[offset].name};
  }
  int cmp(const FileKey &r) const { return id().cmp(r.id()); }
};
using FileIndex = ZmRBTree<FileKey,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"zprotoc.FileIndex">>>;

struct FileLookup {
  const DescSet			&set;
  mutable ZuUnion<void, FileIndex>	index;

  explicit FileLookup(const DescSet &set_) : set{set_} { }

  unsigned find(ZuCSpan name) const {
    if (set.files.length() < 2)
      return set.files.length() && set.files[0].name == name ? 0 : UINT_MAX;
    if (!index.is<FileIndex>()) build();
    auto found = index.p<FileIndex>().find(FileKey{nullptr, UINT_MAX, name});
    return found ? found->data().offset : UINT_MAX;
  }

  void add(unsigned offset) {
    if (!index.is<FileIndex>()) return;
    auto key = FileKey{&set, offset, {}};
    if (!index.p<FileIndex>().find(key)) index.p<FileIndex>().add(key);
  }

private:
  void build() const {
    new (index.new_<FileIndex>()) FileIndex;
    auto &files = index.p<FileIndex>();
    for (unsigned i = 0, n = set.files.length(); i < n; ++i) {
      auto key = FileKey{&set, i, {}};
      if (!files.find(key)) files.add(key);
    }
  }
};

static void collectEnum(Decls &decls, const FileDesc &file,
  const EnumDesc &enumeration, ZuCSpan proto, ZuCSpan cpp)
{
  Text name, native, qualified{"."};
  if (proto.length()) name << proto << '.';
  name << enumeration.name;
  if (cpp.length()) native << cpp << '_';
  native << enumeration.name;
  if (file.package.length()) qualified << file.package << '.';
  qualified << name;
  decls.push(Decl{&file, nullptr, &enumeration, ZuMv(name), ZuMv(native),
    ZuMv(qualified), 0});
}

static void collectMessage(Decls &decls, const FileDesc &file,
  const MsgDesc &message, ZuCSpan proto, ZuCSpan cpp, unsigned &ordinal)
{
  Text name, native, qualified{"."};
  if (proto.length()) name << proto << '.';
  name << message.name;
  if (cpp.length()) native << cpp << '_';
  native << message.name;
  if (file.package.length()) qualified << file.package << '.';
  qualified << name;
  unsigned id = ordinal++;
  Text childProto{name}, childCpp{native};
  decls.push(Decl{&file, &message, nullptr, ZuMv(name), ZuMv(native),
    ZuMv(qualified), id});
  for (const auto &enumeration : message.enums)
    collectEnum(decls, file, enumeration, childProto, childCpp);
  for (const auto &nested : message.nested)
    collectMessage(decls, file, *nested.ptr(), childProto, childCpp, ordinal);
}

static void collect(const DescSet &set, Decls &decls)
{
  for (const auto &file : set.files) {
    unsigned ordinal = 0;
    for (const auto &enumeration : file.enums)
      collectEnum(decls, file, enumeration, {}, {});
    for (const auto &message : file.messages)
      collectMessage(decls, file, message, {}, {}, ordinal);
  }
}

static const Decl *findDecl(const Decls &decls, ZuCSpan name)
{
  return decls.find(name);
}

ZtEnumNS(, Builtin, int8_t,
  NoBuiltin, BInt128, BUInt128, BFixed, BDecimal, BTime,
  BDateTime, BIP, BBitmap, BHwBitmap);
ZtEnumImplNS(Builtin);
struct BuiltinNames {
  using Keys = ZuStringTL<".Zf.Int128", ".Zf.UInt128", ".Zf.Fixed",
    ".Zf.Decimal", ".Zf.Time", ".Zf.DateTime", ".Zi.IP",
    ".Zf.Bitmap", ".Zf.HwBitmap">;
};

static Builtin::T builtin(const Decl &decl)
{
  if (!decl.message) return Builtin::NoBuiltin;
  static constexpr auto matcher = ZuMatcher<BuiltinNames>();
  int i = matcher.exact(decl.qualified);
  return i < 0 ? Builtin::NoBuiltin : Builtin::T(i + 1);
}

static Builtin::T builtin(const Decls &decls, const FieldDesc &field)
{
  if (field.type != Type::Message) return Builtin::NoBuiltin;
  auto decl = findDecl(decls, field.typeName);
  return decl ? builtin(*decl) : Builtin::NoBuiltin;
}

static bool valueBuiltin(const Decl &decl)
{
  return builtin(decl) != Builtin::NoBuiltin;
}

static bool valueBuiltin(const Decls &decls, const FieldDesc &field)
{
  return builtin(decls, field) != Builtin::NoBuiltin;
}

static bool vectorBuiltin(const Decls &decls, const FieldDesc &field)
{
  auto kind = builtin(decls, field);
  return kind && kind <= Builtin::BDateTime;
}

static ZuCSpan builtinNative(Builtin::T kind)
{
  switch (kind) {
    case Builtin::BInt128: return "int128_t";
    case Builtin::BUInt128: return "uint128_t";
    case Builtin::BFixed: return "ZuFixed";
    case Builtin::BDecimal: return "ZuDecimal";
    case Builtin::BTime: return "ZuTime";
    case Builtin::BDateTime: return "ZuDateTime";
    case Builtin::BIP: return "ZiIP";
    case Builtin::BBitmap: return "ZtBitmap";
    case Builtin::BHwBitmap: return "ZmBitmap";
    default: return {};
  }
}

static bool mapEntry(const Decl &decl)
{
  return decl.message && decl.message->options &&
    decl.message->options->mapEntry;
}

static const MsgDesc *mapEntry(const Decls &decls, const FieldDesc &field)
{
  if (field.type != Type::Message || field.label != 3) return nullptr;
  auto decl = findDecl(decls, field.typeName);
  return decl && mapEntry(*decl) ? decl->message : nullptr;
}

static const FieldDesc *entryField(const MsgDesc &entry, unsigned number)
{
  for (const auto &field : entry.fields)
    if (field.number == int32_t(number)) return &field;
  return nullptr;
}

static void resolveNames(Decls &decls, Text &names)
{
  struct Ref { FieldDesc *field; uint64_t offset, length; };
  using Refs = ZtArray<Ref, ZtArrayHeapID<"zprotoc.Refs">>;
  Refs refs;
  for (const auto &decl : decls)
    if (decl.message)
      for (auto &field : const_cast<MsgDesc *>(decl.message)->fields)
	if (field.type == Type::Message) {
	  Text raw;
	  for (char c : field.typeName)
	    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') raw << c;
	  Text qualified;
	  if (raw.length() && raw[0] == '.') qualified = raw;
	  else {
	    Text context;
	    if (decl.file->package.length())
	      context << decl.file->package << '.';
	    context << decl.proto;
	    ZuCSpan scope{context};
	    for (;;) {
	      qualified = ".";
	      if (scope.length()) qualified << scope << '.';
	      qualified << raw;
	      if (findDecl(decls, qualified) || !scope.length()) break;
	      unsigned n = scope.length();
	      while (n && scope[n - 1] != '.') --n;
	      scope = n ? ZuCSpan{scope.data(), n - 1} : ZuCSpan{};
	    }
	  }
	  if (auto target = findDecl(decls, qualified))
	    if (target->enumeration) field.type = Type::Enum;
	  uint64_t offset = names.length();
	  names << qualified;
	  refs.push(Ref{&field, offset, names.length() - offset});
	}
  for (const auto &ref : refs)
    ref.field->typeName = {names.data() + ref.offset, ref.length};
}

static void checkCycles(const DescSet &set, const FileLookup &files)
{
  using States = ZtArray<uint8_t, ZtArrayHeapID<"zprotoc.States">>;
  States states;
  unsigned n = set.files.length();
  states.length(n);
  for (unsigned i = 0; i < n; ++i) states[i] = 0;
  auto visit = [&](auto &&self, unsigned i) -> void {
    if (states[i] == 1) fail(UINT_MAX, "cyclic schema import");
    if (states[i] == 2) return;
    states[i] = 1;
    for (auto dependency : set.files[i].dependencies) {
      unsigned j = files.find(dependency);
      if (j != UINT_MAX) self(self, j);
    }
    states[i] = 2;
  };
  for (unsigned i = 0; i < n; ++i) visit(visit, i);
}

struct Options {
  Zi::Path	input, output, file;
  Text		api;
  Includes	includes;
  Syntax::T	syntax = -1;
  bool		descriptor = false, help = false;
};
ZfStruct(, (Options, CLI),
  (input,	(CLI::Arg<1>),					String),
  (output,	(CLI::Opt<'o'>),				String),
  (file,		(CLI::Long<"file">),			String),
  (api,		(CLI::Long<"export">),				String),
  (includes,	(CLI::Opt<'I'>),				StringVec),
  (syntax,	(Enum<Syntax::CLIMap>, CLI::Long<"syntax">),	Int8),
  (descriptor,	(CLI::Long<"descriptor">),			Bool),
  (help,		(CLI::Flag<'h'>),			Bool));

static ZuCSpan native(int code)
{
  switch (code) {
    case Type::Bool: return "bool";
    case Type::Int32: case Type::SInt32: case Type::SFixed32:
    case Type::Enum: return "int32_t";
    case Type::UInt32: case Type::Fixed32: return "uint32_t";
    case Type::Int64: case Type::SInt64: case Type::SFixed64: return "int64_t";
    case Type::UInt64: case Type::Fixed64: return "uint64_t";
    case Type::Double: return "double";
    case Type::Float: return "float";
    case Type::String: return "ZuCSpan";
    case Type::Bytes: return "ZuBSpan";
    default: return {};
  }
}

static ZuCSpan code(int type)
{
  switch (type) {
    case Type::Bool: return "Bool";
    case Type::Int32: case Type::SInt32: case Type::SFixed32:
    case Type::Enum: return "Int32";
    case Type::UInt32: case Type::Fixed32: return "UInt32";
    case Type::Int64: case Type::SInt64: case Type::SFixed64: return "Int64";
    case Type::UInt64: case Type::Fixed64: return "UInt64";
    case Type::Double: return "Float";
    case Type::Float: return "Float";
    case Type::String: return "String";
    case Type::Bytes: return "Bytes";
    case Type::Message: return "UDT";
    default: return {};
  }
}

static ZuCSpan fieldCode(const FieldDesc &field)
{
  if (field.label != 3) return code(field.type);
  switch (field.type) {
    case Type::Bool: return "UInt8Vec";
    case Type::Message: return "UDT";
    case Type::Int32: case Type::SInt32: case Type::SFixed32:
    case Type::Enum:
      return "Int32Vec";
    case Type::UInt32: case Type::Fixed32: return "UInt32Vec";
    case Type::Int64: case Type::SInt64: case Type::SFixed64:
      return "Int64Vec";
    case Type::UInt64: case Type::Fixed64: return "UInt64Vec";
    case Type::Double: case Type::Float: return "FloatVec";
    case Type::String: return "StringVec";
    case Type::Bytes: return "BytesVec";
    default: return {};
  }
}

static ZuCSpan mappedCode(const Decls &decls, const FieldDesc &field)
{
  bool repeated = field.label == 3;
  switch (builtin(decls, field)) {
    case Builtin::BInt128: return repeated ? "Int128Vec" : "Int128";
    case Builtin::BUInt128: return repeated ? "UInt128Vec" : "UInt128";
    case Builtin::BFixed: return repeated ? "FixedVec" : "Fixed";
    case Builtin::BDecimal: return repeated ? "DecimalVec" : "Decimal";
    case Builtin::BTime: return repeated ? "TimeVec" : "Time";
    case Builtin::BDateTime: return repeated ? "DateTimeVec" : "DateTime";
    case Builtin::BIP: case Builtin::BBitmap: case Builtin::BHwBitmap: return "UDT";
    default: break;
  }
  return fieldCode(field);
}

template <typename T>
static bool defaultInt(ZuCSpan value)
{
  T parsed;
  return ZuBoxed(parsed).scan(value) == int(value.length()) &&
    !ZuNull(parsed);
}

template <typename T>
static bool defaultFloat(ZuCSpan value)
{
  if (fixedName<FloatInfinities>(value) >= 0) return true;
  Text text{value};
  char *end;
  T parsed;
  if constexpr (ZuIsSame<T, float>{}) parsed = strtof(text.data(), &end);
  else parsed = strtod(text.data(), &end);
  return end == text.data() + text.length() && isfinite(parsed) &&
    !ZuNull(parsed);
}

static const EnumValueDesc *defaultEnum(const Decls &decls,
  const FieldDesc &field)
{
  auto target = findDecl(decls, field.typeName);
  if (!target || !target->enumeration) return nullptr;
  return decls.findValue(*target->enumeration, field.defaultValue);
}

static bool supportedDefault(const Decls &decls, const FieldDesc &field)
{
  if (!field.defaultValue.data()) return true;
  if (field.label == 3 || field.oneofIndex != ZuCmp<int32_t>::null())
    return false;
  auto value = field.defaultValue;
  switch (field.type) {
    case Type::Bool: return fixedName<BoolValues>(value) >= 0;
    case Type::Int32: case Type::SInt32: case Type::SFixed32:
      return defaultInt<int32_t>(value);
    case Type::UInt32: case Type::Fixed32:
      return defaultInt<uint32_t>(value);
    case Type::Int64: case Type::SInt64: case Type::SFixed64:
      return defaultInt<int64_t>(value);
    case Type::UInt64: case Type::Fixed64:
      return defaultInt<uint64_t>(value);
    case Type::Float: return defaultFloat<float>(value);
    case Type::Double: return defaultFloat<double>(value);
    case Type::Enum: return defaultEnum(decls, field);
    case Type::String: return true;
    case Type::Bytes: {
      Text decoded;
      return unescape(value, decoded);
    }
    default: return false;
  }
}

static void emitDefault(Text &out, const Decls &decls,
  const FieldDesc &field)
{
  int infinity = field.type == Type::Float || field.type == Type::Double ?
    fixedName<FloatInfinities>(field.defaultValue) : -1;
  if (field.type == Type::Enum) {
    auto value = defaultEnum(decls, field);
    if (!value) fail(UINT_MAX, "unsupported field default");
    out << ZuBox<int32_t>{value->number};
  } else if (infinity >= 0) {
    if (infinity == 2) out << '-';
    out << "ZuFP<" << native(field.type) << ">::inf()";
  } else {
    out << field.defaultValue;
    if (field.type == Type::Float) out << 'f';
  }
}

static Text arrayName(unsigned message, unsigned field)
{
  Text name{"ZpA"};
  name << ZuBox<unsigned>{message} << '_' << ZuBox<unsigned>{field};
  return name;
}

static Text unionName(unsigned message, unsigned oneof)
{
  Text name{"ZpU"};
  name << ZuBox<unsigned>{message} << '_' << ZuBox<unsigned>{oneof};
  return name;
}

static Text mapName(unsigned message, unsigned field)
{
  Text name{"ZpM"};
  name << ZuBox<unsigned>{message} << '_' << ZuBox<unsigned>{field};
  return name;
}

static bool cppName(ZuCSpan name)
{
  if (!name.length() || name.length() > 28 || !alpha(name[0])) return false;
  for (char c : name) if (!alpha(c) && !digit(c)) return false;
  if (name.length() > 1 && name[0] == '_' &&
    (name[1] == '_' || (name[1] >= 'A' && name[1] <= 'Z')))
    return false;
  struct Keywords {
    using Keys = ZuStringTL<
      "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand",
      "bitor", "bool", "break", "case", "catch", "char", "char8_t",
      "char16_t", "char32_t", "class", "compl", "concept", "const",
      "consteval", "constexpr", "constinit", "const_cast", "continue",
      "co_await", "co_return", "co_yield", "decltype", "default",
      "delete", "do", "double", "dynamic_cast", "else", "enum",
      "explicit", "export", "extern", "false", "float", "for",
      "friend", "goto", "if", "import", "inline", "int", "long",
      "module", "mutable", "namespace", "new", "noexcept", "not",
      "not_eq", "nullptr", "operator", "or", "or_eq", "private",
      "protected", "public", "register", "reinterpret_cast", "requires",
      "return", "short", "signed", "sizeof", "static", "static_assert",
      "static_cast", "struct", "switch", "template", "this",
      "thread_local", "throw", "true", "try", "typedef", "typeid",
      "typename", "union", "unsigned", "using", "virtual", "void",
      "volatile", "wchar_t", "while", "xor", "xor_eq">;
  };
  static constexpr auto keywords = ZuMatcher<Keywords>();
  return keywords.exact(name) < 0;
}

static bool cppPackage(ZuCSpan name)
{
  if (!name.length()) return true;
  unsigned begin = 0;
  for (unsigned i = 0, n = name.length(); i <= n; ++i)
    if (i == n || name[i] == '.') {
      if (!cppName({name.data() + begin, i - begin})) return false;
      begin = i + 1;
    }
  return true;
}

static void namespaceOpen(Text &out, ZuCSpan package)
{
  if (!package.length()) return;
  unsigned begin = 0;
  for (unsigned i = 0, n = package.length(); i <= n; ++i)
    if (i == n || package[i] == '.') {
      out << "namespace " << ZuCSpan{package.data() + begin, i - begin}
	<< " {\n";
      begin = i + 1;
    }
  out << '\n';
}
static void namespaceClose(Text &out, ZuCSpan package)
{
  if (!package.length()) return;
  out << '}';
  for (char c : package) if (c == '.') out << '}';
  out << " // namespace " << package << '\n';
}

static Text guard(ZuCSpan path)
{
  Text out{"Zprotoc_"};
  for (char c : path) {
    uint8_t v = uint8_t(c);
    if (c != '_' && (alpha(c) || digit(c))) out << c;
    else out << '_' << char((v >> 4) < 10 ? '0' + (v >> 4) :
      'A' + (v >> 4) - 10) << char((v & 15) < 10 ? '0' + (v & 15) :
      'A' + (v & 15) - 10);
  }
  out << "_HH";
  return out;
}

static void cppType(Text &out, const Decls &decls, ZuCSpan qualified)
{
  auto decl = findDecl(decls, qualified);
  if (!decl) fail(UINT_MAX, "unresolved generated type");
  if (auto native = builtinNative(builtin(*decl))) {
    out << native;
    return;
  }
  out << "::";
  for (char c : decl->file->package) {
    if (c == '.') out << "::";
    else out << c;
  }
  if (decl->file->package.length()) out << "::";
  out << decl->cpp;
}

static ZuCSpan protoName(const Decl &decl)
{
  return {decl.qualified.data() + 1, decl.qualified.length() - 1};
}

static void mapNative(Text &out, const Decls &decls, const Decl &parent,
  const FieldDesc &field, ZuCSpan member, ZuCSpan part)
{
  Text heap;
  heap << protoName(parent) << '.' << member << '.' << part;
  switch (field.type) {
    case Type::String:
      out << "ZtString<ZtStringHeapID<\"" << heap << "\">>";
      return;
    case Type::Bytes:
      out << "ZtArray<uint8_t, ZtArrayHeapID<\"" << heap << "\">>";
      return;
    case Type::Message:
      cppType(out, decls, field.typeName);
      return;
    default:
      out << native(field.type);
  }
}

static void mapProps(Text &out, const Decls &decls, const FieldDesc &field)
{
  out << "ZuTypeList<";
  switch (field.type) {
    case Type::SInt32: case Type::SInt64:
      out << "ZuFieldProp::PB::ZigZag";
      break;
    case Type::Fixed32: case Type::Fixed64:
    case Type::SFixed32: case Type::SFixed64:
      out << "ZuFieldProp::PB::Fixed";
      break;
    case Type::Enum:
      out << "ZuFieldProp::Enum<";
      cppType(out, decls, field.typeName);
      out << "::Map>";
      break;
  }
  out << '>';
}

static bool packedField(const FileDesc &file, const FieldDesc &field)
{
  if (field.label != 3) return false;
  switch (field.type) {
    case Type::Double: case Type::Float:
    case Type::Int64: case Type::UInt64: case Type::Int32:
    case Type::Fixed64: case Type::Fixed32: case Type::Bool:
    case Type::UInt32: case Type::Enum:
    case Type::SFixed32: case Type::SFixed64:
    case Type::SInt32: case Type::SInt64:
      if (int packed = packedOption(field); packed >= 0)
	return packed;
      return file.syntax == "proto3";
    default: return false;
  }
}

using Ordered = ZtArray<const Decl *, ZtArrayHeapID<"zprotoc.Ordered">>;

static Ordered definitionOrder(const FileDesc &file, const Decls &decls)
{
  Ordered ordered;
  using Done = ZtArray<uint8_t, ZtArrayHeapID<"zprotoc.Done">>;
  unsigned n = decls.length();
  auto done = ZtScratch(Done, n, n);
  if (n) memset(done.data(), 0, n);
  unsigned total = 0;
  for (const auto &decl : decls)
    if (decl.file == &file && decl.message && !mapEntry(decl) &&
      !valueBuiltin(decl)) ++total;
  while (ordered.length() < total) {
    unsigned before = ordered.length();
    for (const auto &decl : decls) {
      if (decl.file != &file || !decl.message || mapEntry(decl) ||
	valueBuiltin(decl)) continue;
      unsigned index = &decl - decls.data();
      if (done[index]) continue;
      bool ready = true;
      for (const auto &field : decl.message->fields)
	if (field.oneofIndex != ZuCmp<int32_t>::null() ||
	  mapEntry(decls, field)) {
	  ZuCSpan targetName = field.typeName;
	  if (auto entry = mapEntry(decls, field)) {
	    auto value = entryField(*entry, 2);
	    if (!value || value->type != Type::Message) continue;
	    targetName = value->typeName;
	  }
	  auto target = findDecl(decls, targetName);
	  if (!target) fail(UINT_MAX, "unresolved oneof alternative");
	  if (target->file != &file || valueBuiltin(*target)) continue;
	  if (!done[target - decls.data()]) { ready = false; break; }
	}
      if (ready) {
	ordered.push(&decl);
	done[index] = 1;
      }
    }
    if (ordered.length() == before)
      fail(UINT_MAX, "cyclic oneof value dependency");
  }
  return ordered;
}

static void typeCode(Text &out, ZuCSpan code)
{
  unsigned begin = out.length();
  while (begin && out[begin - 1] != '\n') --begin;
  unsigned col = 0;
  for (unsigned i = begin, n = out.length(); i < n; ++i)
    col = out[i] == '\t' ? (col + 8) & ~7U : col + 1;
  if (col > 56) out << "\n    ";
  else do {
    out << '\t';
    col = (col + 8) & ~7U;
  } while (col < 56);
  out << code;
}

static Text header(const FileDesc &file, const Decls &decls,
  const FileLookup &files, ZuCSpan output, ZuCSpan api)
{
  Text out;
  auto g = guard(output);
  out << "//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-\n"
      "//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2\n\n"
      "// Generated by zprotoc.\n\n#ifndef " << g << "\n#define " << g
    << "\n\n#ifndef ZpbLib_HH\n#include <zlib/ZpbLib.hh>\n#endif\n\n"
      "#include <stddef.h>\n#include <stdint.h>\n\n"
      "#include <zlib/ZuCmp.hh>\n#include <zlib/ZuDerive.hh>\n"
      "#include <zlib/ZuFP.hh>\n#include <zlib/ZuPtr.hh>\n"
      "#include <zlib/ZuSpan.hh>\n#include <zlib/ZuTL.hh>\n\n"
      "#include <zlib/ZmHeap.hh>\n#include <zlib/ZmRBTree.hh>\n\n"
      "#include <zlib/ZtArray.hh>\n#include <zlib/ZtString.hh>\n\n"
      "#include <zlib/ZfStruct.hh>\n\n"
      "#include <zlib/Zpb.hh>\n";
  if (file.dependencies.length()) {
    using Generated = ZtArray<uint8_t, ZtArrayHeapID<"zprotoc.Generated">>;
    unsigned n = files.set.files.length();
    auto generated = ZtScratch(Generated, n, n);
    if (n) memset(generated.data(), 0, n);
    for (const auto &decl : decls)
      if (decl.enumeration || (decl.message && !mapEntry(decl) &&
	!valueBuiltin(decl))) {
	unsigned i = files.find(decl.file->name);
	if (i != UINT_MAX) generated[i] = 1;
      }
    for (auto dependency : file.dependencies) {
      unsigned i = files.find(dependency);
      if (i == UINT_MAX || !generated[i]) continue;
      ZuCSpan stem{dependency.data(), dependency.length() - 6};
      out << "#include \"" << stem << ".hh\"\n";
    }
  }
  out << '\n';
  namespaceOpen(out, file.package);
  for (const auto &decl : decls) if (decl.file == &file && decl.enumeration) {
    const auto &enumeration = *decl.enumeration;
    out << "namespace " << decl.cpp << " {\n"
	"using T = int32_t;\nenum {\n";
    for (unsigned i = 0, n = enumeration.values.length(); i < n; ++i) {
      const auto &value = enumeration.values[i];
      out << "  " << value.name << " = " << ZuBox<int32_t>{value.number};
      out << (i + 1 == enumeration.values.length() ? "\n" : ",\n");
    }
    out << "};\nstruct ";
    if (api.length()) out << api << ' ';
    out << "Map {\n  static constexpr const char *id() { return \"";
    out << protoName(decl) << "\"; }\n"
	"  static T s2v(ZuCSpan);\n  static T match(ZuCSpan);\n"
	"  static ZuCSpan v2s(int);\n"
	"  template <typename L> static void all(L &&fn) {\n";
    for (const auto &value : enumeration.values)
      out << "    fn(\"" << value.name << "\", " << value.name << ");\n";
    out << "  }\n};\n} // namespace " << decl.cpp << "\n\n";
  }
  for (const auto &decl : decls)
    if (decl.file == &file && decl.message && !mapEntry(decl) &&
      !valueBuiltin(decl)) {
    const auto &message = *decl.message;
    out << "struct ";
    if (api.length()) out << api << ' ';
    out << decl.cpp << ";\nZpb::AsMessage<\"" << protoName(decl)
      << '"';
    for (const auto &range : message.reservedRanges)
      out << ",\n  Zpb::ReserveRange<" << ZuBox<int32_t>{range.start}
	<< ", " << ZuBox<int32_t>{range.end} << '>';
    for (auto name : message.reservedNames) {
      out << ",\n  Zpb::ReserveName<";
      emitQuoted(out, name);
      out << '>';
    }
    out << ">\n  Zpb_Fmt(" << decl.cpp << " *);\n\n";
  }
  for (const auto &decl : decls)
    if (decl.file == &file && decl.message && !mapEntry(decl) &&
      !valueBuiltin(decl)) {
    const auto &message = *decl.message;
    unsigned m = decl.ordinal;
    for (unsigned f = 0, n = message.fields.length(); f < n; ++f) {
      const auto &field = message.fields[f];
      if (field.type != Type::Message || field.label != 3 ||
	mapEntry(decls, field) || vectorBuiltin(decls, field)) continue;
      auto name = arrayName(m, f);
      bool mapped = valueBuiltin(decls, field);
      out << "ZuDerive(" << name << ",\n  (ZtArray<";
      if (!mapped) out << "ZuPtr<";
      cppType(out, decls, field.typeName);
      if (!mapped) out << '>';
      out << ",\n    ZtArrayHeapID<\"";
      out << protoName(decl) << '.' << field.name << "\">>));\n"
	<< "Zpb::AsArray<\"" << protoName(decl) << '.' << field.name
	<< "\">\n  Zpb_Fmt("
	<< name << " *);\n\n";
    }
  }
  auto ordered = definitionOrder(file, decls);
  for (auto item : ordered) {
    const auto &decl = *item;
    const auto &message = *decl.message;
    unsigned m = decl.ordinal;
    using FirstAlts = ZtArray<unsigned, ZtArrayHeapID<"zprotoc.FirstAlts">>;
    using AltLinks = ZtArray<unsigned, ZtArrayHeapID<"zprotoc.AltLinks">>;
    unsigned oneofN = message.oneofs.length();
    auto first = ZtScratch(FirstAlts, oneofN, oneofN);
    auto last = ZtScratch(FirstAlts, oneofN, oneofN);
    unsigned fieldN = message.fields.length();
    unsigned linkN = oneofN ? fieldN : 0;
    auto next = ZtScratch(AltLinks, linkN, linkN);
    for (unsigned u = 0; u < oneofN; ++u)
      first[u] = last[u] = UINT_MAX;
    if (oneofN)
      for (unsigned f = 0; f < fieldN; ++f) {
	int32_t u = message.fields[f].oneofIndex;
	if (u == ZuCmp<int32_t>::null()) continue;
	next[f] = UINT_MAX;
	if (first[u] == UINT_MAX) first[u] = f;
	else next[last[u]] = f;
	last[u] = f;
      }
    for (unsigned f = 0, n = message.fields.length(); f < n; ++f) {
      const auto &field = message.fields[f];
      auto entry = mapEntry(decls, field);
      if (!entry) continue;
      auto key = entryField(*entry, 1), value = entryField(*entry, 2);
      if (!key || !value) fail(UINT_MAX, "invalid map entry");
      auto name = mapName(m, f);
      Text heap;
      heap << protoName(decl) << '.' << field.name;
      out << "ZuDerive(" << name << ",\n  (ZmRBTreeKV<";
      mapNative(out, decls, decl, *key, field.name, "key");
      out << ",\n    ";
      mapNative(out, decls, decl, *value, field.name, "value");
      out << ",\n    ZmRBTreeHeapID<\"" << heap << "\">>));\n"
	"Zpb::AsMap<\"" << heap << "\", ZfFieldTC::" << code(key->type)
	<< ",\n  ZfFieldTC::" << mappedCode(decls, *value) << ", ";
      mapProps(out, decls, *key);
      out << ", ";
      mapProps(out, decls, *value);
      out << ">\n  Zpb_Fmt(" << name << " *);\n\n";
    }
    for (unsigned u = 0, n = message.oneofs.length(); u < n; ++u) {
      auto name = unionName(m, u);
      out << "ZuDerive(" << name << ",\n  (ZuUnion<void";
      for (unsigned f = first[u]; f != UINT_MAX; f = next[f]) {
	const auto &field = message.fields[f];
	out << ",\n    ";
	cppType(out, decls, field.typeName);
      }
      out << ">));\nZpb::AsUnion<\"" << protoName(decl) << '.'
	<< message.oneofs[u].name << "\"";
      for (unsigned f = first[u]; f != UINT_MAX; f = next[f]) {
	const auto &field = message.fields[f];
	out << ",\n  Zpb::Alt<\"" << field.name << "\", "
	  << ZuBox<int32_t>{field.number} << ", ";
	cppType(out, decls, field.typeName);
	out << '>';
      }
      out << ">\n  Zpb_Fmt(" << name << " *);\n\n";
    }
    for (unsigned f = 0, n = message.fields.length(); f < n; ++f) {
      const auto &field = message.fields[f];
      if (field.label != 3 ||
	(field.type != Type::String && field.type != Type::Bytes)) continue;
      Text heap;
      heap << protoName(decl) << '.' << field.name;
      out << "ZuDerive(" << arrayName(m, f) << ",\n  (ZtArray<";
      if (field.type == Type::String)
	out << "ZtString<ZtStringHeapID<\"" << heap << ".elem\">>";
      else out << "ZtArray<uint8_t, ZtArrayHeapID<\"" << heap << ".elem\">>";
      out << ",\n    ZtArrayHeapID<\"" << heap << "\">>));\n\n";
    }
    out << "namespace Zprotoc_::" << decl.cpp << " {\n\n"
      "struct O_ : ZmNHeap<O_> {\n"
      "friend inline ZuStringT<\"" << protoName(decl) << "\"> ZmNHeapID(O_ *);\n\n";
    bool ownsChild = false;
    for (unsigned f = 0, n = message.fields.length(); f < n; ++f) {
      const auto &field = message.fields[f];
      if (mapEntry(decls, field)) {
	out << "  " << mapName(m, f) << ' ' << field.name << "{};\n";
	continue;
      }
      if (field.oneofIndex != ZuCmp<int32_t>::null()) {
	if (first[field.oneofIndex] == f)
	  out << "  " << unionName(m, field.oneofIndex) << ' '
	    << message.oneofs[field.oneofIndex].name << "{};\n";
	continue;
      }
      if (field.label == 3 &&
	(field.type == Type::String || field.type == Type::Bytes)) {
	out << "  " << arrayName(m, f) << ' ' << field.name << "{};\n";
	continue;
      }
      out << "  ";
      Text heap;
      heap << protoName(decl) << '.' << field.name;
      if (field.type == Type::Message && field.label == 3 &&
	!vectorBuiltin(decls, field))
	out << arrayName(m, f);
      else if (field.label == 3) out << "ZtArray<";
      if (field.type == Type::Message && field.label == 1) {
	bool mapped = valueBuiltin(decls, field);
	if (!mapped) out << "ZuPtr<";
	cppType(out, decls, field.typeName);
	if (!mapped) { out << ">"; ownsChild = true; }
      } else if (field.type == Type::String)
	out << "ZtString<ZtStringHeapID<\"" << heap;
      else if (field.type == Type::Bytes)
	out << "ZtArray<uint8_t, ZtArrayHeapID<\"" << heap;
      if (field.type == Type::String || field.type == Type::Bytes) {
	if (field.label == 3) out << ".elem";
	out << "\">>";
      } else if (field.label == 3 && field.type == Type::Bool)
	out << "uint8_t";
      else if (field.type == Type::Message && field.label == 3 &&
	vectorBuiltin(decls, field))
	cppType(out, decls, field.typeName);
      else out << native(field.type);
      if (field.label == 3 &&
	(field.type != Type::Message || vectorBuiltin(decls, field)))
	out << ", ZtArrayHeapID<\"" << heap << "\">>";
      out << ' ' << field.name;
      if (field.defaultValue.data()) {
	if (field.type == Type::String) {
	  out << "{ZuCSpan{";
	  emitQuoted(out, field.defaultValue);
	  out << ", " << ZuBox<unsigned>{field.defaultValue.length()} << "}}";
	} else if (field.type == Type::Bytes) {
	  Text decoded;
	  if (!unescape(field.defaultValue, decoded))
	    fail(UINT_MAX, "invalid bytes default");
	  out << "{\n    reinterpret_cast<const uint8_t *>(";
	  emitQuoted(out, decoded);
	  out << "), " << ZuBox<unsigned>{decoded.length()} << '}';
	} else {
	  out << '{';
	  emitDefault(out, decls, field);
	  out << '}';
	}
      }
      else if (field.label != 3 && field.type != Type::Bool &&
	field.type != Type::Message && field.type != Type::String &&
	field.type != Type::Bytes)
	out << "{ZuCmp<" << native(field.type) << ">::null()}";
      else out << "{}";
      out << ";\n";
    }
    out << "};\n\n"
      "} // Zprotoc_::" << decl.cpp << "\n\nstruct ";
    if (api.length()) out << api << ' ';
    out << decl.cpp << " :\n  Zprotoc_::" << decl.cpp << "::O_ {\n";
    if (ownsChild)
      out << "  " << decl.cpp << "();\n"
	<< "  " << decl.cpp << '(' << decl.cpp << " &&) noexcept;\n"
	<< "  " << decl.cpp << " &operator =(" << decl.cpp
	<< " &&) noexcept;\n  ~" << decl.cpp << "();\n"
	<< "  " << decl.cpp << "(const " << decl.cpp << " &) = delete;\n"
	<< "  " << decl.cpp << " &operator =(const " << decl.cpp
	<< " &) = delete;\n";
    out << "};\nZfStruct(" << api << ", (" << decl.cpp << ", PB)";
    for (unsigned f = 0, n = message.fields.length(); f < n; ++f) {
      const auto &field = message.fields[f];
      if (mapEntry(decls, field)) {
	out << ",\n  (" << field.name << ", (PB::FieldID<"
	  << ZuBox<int32_t>{field.number} << ">, Mutable),";
	typeCode(out, "UDT");
	out << ')';
	continue;
      }
      if (field.oneofIndex != ZuCmp<int32_t>::null()) {
	if (first[field.oneofIndex] == f) {
	  out << ",\n  (" << message.oneofs[field.oneofIndex].name
	    << ", (Mutable),";
	  typeCode(out, "UDT");
	  out << ')';
	}
	continue;
      }
      out << ",\n  (" << field.name << ", (";
      if (field.label == 2) out << "Required, ";
      out << "PB::FieldID<" << ZuBox<int32_t>{field.number};
      switch (field.type) {
	case Type::Bool:
	  if (field.label == 3) out << ">, PB::BoolVec";
	  else out << '>';
	  break;
	case Type::SInt32: case Type::SInt64: out << ">, PB::ZigZag"; break;
	case Type::Fixed32: case Type::Fixed64:
	case Type::SFixed32: case Type::SFixed64:
	  out << ">, PB::Fixed"; break;
	case Type::Enum:
	  out << ">, Enum<";
	  cppType(out, decls, field.typeName);
	  out << "::Map>";
	  break;
	default: out << '>'; break;
      }
      if (packedField(file, field)) out << ", PB::Packed";
      if (field.defaultValue.data()) {
	out << ", Deflt<";
	if (field.type == Type::String) {
	  emitQuoted(out, field.defaultValue);
	  out << "_z";
	} else if (field.type == Type::Bytes) {
	  Text decoded;
	  if (!unescape(field.defaultValue, decoded))
	    fail(UINT_MAX, "invalid bytes default");
	  emitQuoted(out, decoded);
	  out << "_z";
	} else emitDefault(out, decls, field);
	out << '>';
      }
      out << "),";
      typeCode(out, mappedCode(decls, field));
      out << ')';
    }
    out << ");\n\n";
  }
  namespaceClose(out, file.package);
  out << "\n#endif // " << g << '\n';
  return out;
}

static Text source(const FileDesc &file, const Decls &decls, ZuCSpan header)
{
  Text out;
  out << "//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-\n"
      "//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2\n\n"
      "// Generated by zprotoc.\n\n#include \"" << header << "\"\n"
    "#include <zlib/ZuMatcher.hh>\n\n";
  namespaceOpen(out, file.package);
  for (const auto &decl : decls) if (decl.file == &file && decl.enumeration) {
    const auto &enumeration = *decl.enumeration;
    out << decl.cpp << "::T " << decl.cpp
	<< "::Map::s2v(ZuCSpan s) {\n"
	"  struct IDs { using Keys = ZuStringTL<";
    for (unsigned i = 0, n = enumeration.values.length(); i < n; ++i) {
      if (i) out << ", ";
      out << '"' << enumeration.values[i].name << '"';
    }
    out << ">; };\n  static constexpr auto ids = ZuMatcher<IDs>();\n"
	"  switch (ids.exact(s)) {\n";
    for (unsigned i = 0, n = enumeration.values.length(); i < n; ++i)
      out << "    case " << ZuBox<unsigned>{i} << ": return "
	<< decl.cpp << "::" << enumeration.values[i].name << ";\n";
    out << "    default: return -1;\n  }\n}\n" << decl.cpp << "::T "
	<< decl.cpp << "::Map::match(ZuCSpan s) {\n"
	"  return s2v(s);\n}\nZuCSpan " << decl.cpp
	<< "::Map::v2s(int value) {\n  switch (value) {\n";
    for (const auto &value : enumeration.values)
      out << "    case " << decl.cpp << "::" << value.name
	<< ": return \"" << value.name << "\";\n";
    out << "    default: return {};\n  }\n}\n";
  }
  for (const auto &decl : decls)
    if (decl.file == &file && decl.message && !mapEntry(decl) &&
      !valueBuiltin(decl)) {
    const auto &message = *decl.message;
    bool ownsChild = false;
    for (const auto &field : message.fields)
      if (field.type == Type::Message &&
	field.oneofIndex == ZuCmp<int32_t>::null() &&
	!mapEntry(decls, field) && !valueBuiltin(decls, field))
	ownsChild = true;
    if (ownsChild)
      out << decl.cpp << "::" << decl.cpp << "() = default;\n"
	<< decl.cpp << "::" << decl.cpp << '(' << decl.cpp
	<< " &&) noexcept = default;\n" << decl.cpp << " &"
	<< decl.cpp << "::operator =(" << decl.cpp
	<< " &&) noexcept = default;\n" << decl.cpp << "::~"
	<< decl.cpp << "() = default;\n";
    out << "ZfStructImpl(" << decl.cpp << ");\n"
      << "ZfStructImpl(" << decl.cpp << ", PB);\n";
  }
  namespaceClose(out, file.package);
  return out;
}

struct Staged {
  Zi::Path	dst, tmp, bak;
  bool		held = false, installed = false;
};
using StagedFiles = ZtArray<Staged, ZtArrayHeapID<"zprotoc.StagedFiles">>;

class Publish {
public:
  ~Publish() {
    for (auto &f : m_files) {
      if (f.tmp) ZiFile::remove(f.tmp);
      if (f.bak && !f.held) ZiFile::remove(f.bak);
    }
  }
  void stage(Zi::Path dst, ZuCSpan data) {
    if (ZiStat{dst}.isdir()) fail(UINT_MAX, "output destination is a directory");
    auto &f = *new (m_files.push()) Staged{ZuMv(dst)};
    ZiFile file;
    f.tmp = reserve(f.dst, file);
    if (file.write(data.data(), data.length()) != Zi::OK ||
      file.sync() != Zi::OK || file.close() != Zi::OK)
      fail(UINT_MAX, "cannot write staged output");
    if (ZiStat{f.dst}.exists()) {
      f.bak = reserve(f.dst, file);
      if (file.close() != Zi::OK) fail(UINT_MAX, "cannot close backup");
    }
  }
  void commit() {
    try {
      for (auto &f : m_files)
	if (f.bak) {
	  if (ZiFile::rename(f.dst, f.bak) != Zi::OK)
	    fail(UINT_MAX, "cannot preserve previous output");
	  f.held = true;
	}
      for (auto &f : m_files) {
	if (ZiFile::rename(f.tmp, f.dst) != Zi::OK)
	  fail(UINT_MAX, "cannot replace output");
	f.installed = true;
	f.tmp.null();
      }
    } catch (Failure &e) {
      for (unsigned i = m_files.length(); i; --i) {
	auto &f = m_files[i - 1];
	if (f.installed && ZiFile::remove(f.dst) != Zi::OK)
	  e.message << "; cannot remove partial output " << f.dst;
	if (f.held) {
	  if (ZiFile::rename(f.bak, f.dst) == Zi::OK) f.held = false;
	  else e.message << "; previous output retained at " << f.bak;
	}
      }
      throw;
    }
    for (auto &f : m_files)
      if (f.held) {
	if (ZiFile::remove(f.bak) != Zi::OK)
	  fail(UINT_MAX, "cannot remove backup");
	f.held = false;
	f.bak.null();
      }
  }

private:
  Zi::Path reserve(const Zi::Path &dst, ZiFile &file) {
    for (;;) {
      Text name;
      name << dst << ".zprotoc." << ZuBox<Zm::ProcessID>{Zm::getPID()}
	<< '.' << ZuBox<unsigned>{m_seq++};
      Zi::Path path{name};
      if (file.open(path, ZiFile::WriteOnly | ZiFile::Create |
	  ZiFile::Exclusive | ZiFile::GC) == Zi::OK) return path;
      if (!ZiStat{path}.exists() || !m_seq)
	fail(UINT_MAX, "cannot stage output");
    }
  }
  StagedFiles m_files;
  unsigned m_seq = 0;
};

using OneofIndex = ZmRBTree<ZuCSpan,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"zprotoc.OneofIndex">>>;

static void supportedFile(const FileDesc &file, const Decls &decls)
{
  auto enumSyntax = [&](const FieldDesc &field) {
    if (file.syntax != "proto3" || field.type != Type::Enum) return true;
    auto target = findDecl(decls, field.typeName);
    return !target || target->file->syntax == "proto3";
  };
  if ((file.syntax.length() && file.syntax != "proto2" &&
    file.syntax != "proto3") ||
    (!file.enums.length() && !file.messages.length()))
    fail(UINT_MAX, "unsupported file schema");
  for (auto dependency : file.dependencies) {
    unsigned n = dependency.length();
    if (n < 7 || ZuCSpan{dependency.data() + n - 6, 6} != ".proto")
      fail(UINT_MAX, "invalid import path");
    for (char c : dependency)
      if (c == '"' || c == '\\' || c == '\n' || c == '\r' || !c)
	fail(UINT_MAX, "invalid import path");
  }
  if (!cppPackage(file.package)) fail(UINT_MAX, "invalid C++ package name");
  for (const auto &decl : decls) if (decl.file == &file && decl.enumeration) {
    const auto &enumeration = *decl.enumeration;
    if (!cppName(enumeration.name) || !cppName(decl.cpp))
      fail(UINT_MAX, "invalid C++ enum name");
    for (const auto &value : enumeration.values)
      if (!cppName(value.name) || fixedName<EnumReserved>(value.name) >= 0 ||
	value.name == enumeration.name)
	fail(UINT_MAX, "invalid C++ enum value name");
  }
  for (const auto &decl : decls) if (decl.file == &file && decl.message) {
    if (mapEntry(decl)) {
      bool nested = false;
      for (char c : decl.proto) if (c == '.') { nested = true; break; }
      if (!nested) fail(UINT_MAX, "map entry must be nested");
      continue;
    }
    if (valueBuiltin(decl)) continue;
    const auto &message = *decl.message;
    if (!cppName(message.name) || !cppName(decl.cpp))
      fail(UINT_MAX, "invalid C++ message name");
    if (message.options)
      fail(UINT_MAX, "unsupported message schema");
    for (const auto &field : message.fields)
      if (field.proto3Optional)
	fail(UINT_MAX, "unsupported proto3 optional field");
    unsigned oneofN = message.oneofs.length();
    using Used = ZtArray<uint8_t, ZtArrayHeapID<"zprotoc.OneofUsed">>;
    unsigned usedN = oneofN > 1 ? oneofN : 0;
    auto used = ZtScratch(Used, usedN, usedN);
    if (usedN) {
      memset(used.data(), 0, usedN);
      for (const auto &field : message.fields) {
	int32_t u = field.oneofIndex;
	if (u >= 0 && unsigned(u) < oneofN) used[u] = 1;
      }
    }
    OneofIndex oneofs;
    for (unsigned u = 0, n = message.oneofs.length(); u < n; ++u) {
      const auto &oneof = message.oneofs[u];
      if (!cppName(oneof.name) || oneof.name == decl.cpp)
	fail(UINT_MAX, "invalid C++ oneof name");
      bool occupied = usedN ? used[u] : false;
      if (!usedN)
	for (const auto &field : message.fields)
	  if (field.oneofIndex == int32_t(u)) { occupied = true; break; }
      if (!occupied) fail(UINT_MAX, "empty oneof");
      if (oneofs.find(oneof.name)) fail(UINT_MAX, "duplicate oneof name");
      oneofs.add(oneof.name);
      if (!cppName(unionName(decl.ordinal, u)))
	fail(UINT_MAX, "invalid generated union name");
    }
    for (unsigned f = 0, n = message.fields.length(); f < n; ++f) {
      const auto &field = message.fields[f];
      if (field.type == Type::Message) {
	auto target = findDecl(decls, field.typeName);
	if (target && mapEntry(*target) && !mapEntry(decls, field))
	  fail(UINT_MAX, "unsupported map entry field");
      }
      if (auto entry = mapEntry(decls, field)) {
	auto key = entryField(*entry, 1), value = entryField(*entry, 2);
	if (value && !enumSyntax(*value))
	  fail(UINT_MAX, "proto2 enum in proto3 field");
	if (entry->fields.length() != 2 || !key || !value ||
	  !cppName(mapName(decl.ordinal, f)) ||
	  !fieldCode(*key) || !fieldCode(*value) ||
	  key->label != 1 || value->label != 1 ||
	  (key->type != Type::Bool && key->type != Type::Int32 &&
	    key->type != Type::UInt32 && key->type != Type::Int64 &&
	    key->type != Type::UInt64 && key->type != Type::SInt32 &&
	    key->type != Type::SInt64 && key->type != Type::Fixed32 &&
	    key->type != Type::Fixed64 && key->type != Type::SFixed32 &&
	    key->type != Type::SFixed64 && key->type != Type::String))
	  fail(UINT_MAX, "unsupported map entry");
      }
      if (!cppName(field.name) || field.name == decl.cpp)
	fail(UINT_MAX, "invalid C++ field name");
      if (!enumSyntax(field))
	fail(UINT_MAX, "proto2 enum in proto3 field");
      if (field.oneofIndex != ZuCmp<int32_t>::null() &&
	(field.oneofIndex < 0 ||
	 unsigned(field.oneofIndex) >= message.oneofs.length() ||
	 field.type != Type::Message || field.label != 1 ||
	 valueBuiltin(decls, field)))
	fail(UINT_MAX, "unsupported oneof alternative");
      if (field.oneofIndex == ZuCmp<int32_t>::null() && oneofs.find(field.name))
	fail(UINT_MAX, "field collides with oneof name");
      if ((field.label != 1 && field.label != 2 && field.label != 3) ||
	(file.syntax == "proto3" && field.label == 2) ||
	!fieldCode(field) ||
	(field.type != Type::Message && field.type != Type::Enum &&
	  field.typeName.length()) ||
	((field.type == Type::Message || field.type == Type::Enum) &&
	  !field.typeName.length()) ||
	(file.syntax == "proto3" && field.defaultValue.data()) ||
	!supportedDefault(decls, field))
	fail(UINT_MAX, "unsupported field schema");
      if (field.type == Type::Message && field.label == 3 &&
	!vectorBuiltin(decls, field) &&
	  !cppName(arrayName(decl.ordinal, f)))
	fail(UINT_MAX, "invalid generated array name");
    }
  }
}

static void supportedBuiltins(const Decls &decls)
{
  for (const auto &decl : decls) if (auto kind = builtin(decl)) {
    const auto &message = *decl.message;
    auto field = [&](unsigned number, ZuCSpan name, int type) {
      auto found = entryField(message, number);
      return found && found->name == name && found->type == type &&
	found->label == 1 && !found->typeName.length() &&
	!found->defaultValue.data() &&
	found->oneofIndex == ZuCmp<int32_t>::null();
    };
    bool valid = !message.options && !message.enums.length() &&
      !message.nested.length() && !message.oneofs.length() &&
      !message.reservedRanges.length() && !message.reservedNames.length();
    switch (kind) {
      case Builtin::BInt128: case Builtin::BUInt128: case Builtin::BDecimal:
	valid = valid && message.fields.length() == 2 &&
	  field(1, "h", Type::UInt64) && field(2, "l", Type::UInt64);
	break;
      case Builtin::BFixed:
	valid = valid && message.fields.length() == 2 &&
	  field(1, "mantissa", Type::Int64) &&
	  field(2, "ndp", Type::UInt32);
	break;
      case Builtin::BTime:
	valid = valid && message.fields.length() == 2 &&
	  field(1, "sec", Type::Int64) && field(2, "nsec", Type::Int32);
	break;
      case Builtin::BDateTime:
	valid = valid && message.fields.length() == 3 &&
	  field(1, "julian", Type::Int32) && field(2, "sec", Type::Int32) &&
	  field(3, "nsec", Type::Int32);
	break;
      case Builtin::BIP:
	valid = valid && message.fields.length() == 1 &&
	  field(1, "addr", Type::Bytes);
	break;
      case Builtin::BBitmap:
	valid = valid && message.fields.length() == 1 &&
	  field(1, "words", Type::Bytes);
	break;
      case Builtin::BHwBitmap:
	valid = valid && message.fields.length() == 1 &&
	  field(1, "ranges", Type::String);
	break;
      default: break;
    }
    if (!valid) {
      Text diagnostic{"invalid "};
      diagnostic << protoName(decl) << " mapping";
      fail(UINT_MAX, diagnostic);
    }
  }
}

struct Symbol {
  Text		name;
  unsigned	helper;
  bool		namespace_;
};
using Symbols = ZtArray<Symbol, ZtArrayHeapID<"zprotoc.Symbols">>;
struct SymbolKey {
  const Symbols	*symbols;
  unsigned	offset;
  ZuCSpan	query;

  ZuCSpan id() const {
    return offset == UINT_MAX ? query : ZuCSpan{(*symbols)[offset].name};
  }
  int cmp(const SymbolKey &r) const { return id().cmp(r.id()); }
};
using SymbolIndex = ZmRBTree<SymbolKey,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"zprotoc.SymbolIndex">>>;

static void generatedNames(const DescSet &set, const FileDesc &selected,
  const Decls &decls, const FileLookup &files)
{
  using Indices = ZtArray<unsigned, ZtArrayHeapID<"zprotoc.Indices">>;
  unsigned n = set.files.length(), root = n;
  for (unsigned i = 0; i < n; ++i)
    if (&set.files[i] == &selected) { root = i; break; }
  if (root == n) fail(UINT_MAX, "selected file not in descriptor set");
  Indices queue;
  using Seen = ZtArray<uint8_t, ZtArrayHeapID<"zprotoc.FileSeen">>;
  auto seen = ZtScratch(Seen, n, n);
  memset(seen.data(), 0, n);
  queue.push(root);
  seen[root] = 1;
  for (unsigned i = 0; i < queue.length(); ++i) {
    const auto &file = set.files[queue[i]];
    for (auto dependency : file.dependencies) {
      unsigned target = files.find(dependency);
      if (target == UINT_MAX) fail(UINT_MAX, "import schema not found");
      if (!seen[target]) {
	seen[target] = 1;
	queue.push(target);
      }
    }
  }
  Symbols symbols;
  SymbolIndex index;
  auto add = [&](ZuCSpan name, unsigned helper, bool namespace_) {
    if (auto found = index.find(SymbolKey{nullptr, UINT_MAX, name})) {
	const auto &symbol = symbols[found->data().offset];
	if (namespace_ && symbol.namespace_) return;
	if (namespace_ || symbol.namespace_)
	  fail(UINT_MAX, "generated namespace name collision");
	if (helper || symbol.helper) {
	  if (helper == 3 || symbol.helper == 3)
	    fail(UINT_MAX, "generated map name collision");
	  if (helper == 2 || symbol.helper == 2)
	    fail(UINT_MAX, "generated union name collision");
	  if (helper != symbol.helper)
	    fail(UINT_MAX, "generated array name collides with message");
	  fail(UINT_MAX, "generated array name collision");
	}
	fail(UINT_MAX, "generated message name collision");
    }
    symbols.push(Symbol{Text{name}, helper, namespace_});
    index.add(SymbolKey{&symbols, unsigned(symbols.length() - 1), {}});
  };
  for (auto i : queue) {
    const auto &file = set.files[i];
    if (!cppPackage(file.package))
      fail(UINT_MAX, "invalid imported C++ package name");
    for (unsigned c = 0, n = file.package.length(); c < n; ++c)
      if (file.package[c] == '.')
	add({file.package.data(), c}, false, true);
    if (file.package.length()) add(file.package, false, true);
    for (const auto &decl : decls)
      if (decl.file == &file && !mapEntry(decl) &&
	!valueBuiltin(decl)) {
      if (!cppName(decl.cpp))
	fail(UINT_MAX, "invalid imported C++ type name");
      Text name;
      if (file.package.length()) name << file.package << '.';
      name << decl.cpp;
      add(name, false, !!decl.enumeration);
      if (!decl.message) continue;
      Text impl;
      if (file.package.length()) impl << file.package << '.';
      impl << "Zprotoc_";
      add(impl, false, true);
      impl << '.' << decl.cpp;
      add(impl, false, true);
      Text base{impl}, heap{impl};
      base << ".O_";
      heap << ".H_";
      add(base, false, false);
      add(heap, false, false);
      const auto &message = *decl.message;
      for (unsigned f = 0, n = message.fields.length(); f < n; ++f) {
	const auto &field = message.fields[f];
	if (mapEntry(decls, field)) {
	  Text helper;
	  if (file.package.length()) helper << file.package << '.';
	  helper << mapName(decl.ordinal, f);
	  add(helper, 3, false);
	  continue;
	}
	if (field.type == Type::Message && field.label == 3) {
	  Text helper;
	  if (file.package.length()) helper << file.package << '.';
	  helper << arrayName(decl.ordinal, f);
	  add(helper, 1, false);
	}
      }
      for (unsigned u = 0, n = message.oneofs.length(); u < n; ++u) {
	Text helper;
	if (file.package.length()) helper << file.package << '.';
	helper << unionName(decl.ordinal, u);
	add(helper, 2, false);
      }
    }
  }
}

static void generate(const DescSet &set, const FileDesc &file,
  const Decls &decls, const FileLookup &files, const Zi::Path &output, ZuCSpan api)
{
  supportedBuiltins(decls);
  supportedFile(file, decls);
  generatedNames(set, file, decls, files);
  Text hh{output}, cc{output};
  hh << ".hh";
  cc << ".cc";
  Text generatedHeader = header(file, decls, files, output, api);
  Text generatedSource = source(file, decls, ZiFile::leafname(Zi::Path{hh}));
  Publish publish;
  publish.stage(Zi::Path{hh}, generatedHeader);
  publish.stage(Zi::Path{cc}, generatedSource);
  publish.commit();
}

int main(int argc, char **argv)
{
  ZiFile err;
  err.openStdErr();
  Options opts;
  Input input;
  try {
    unsigned n;
    try { n = ZfCLI::load(opts, argc, argv); }
    catch (const ZeException &e) {
      Text message;
      message << e;
      fail(UINT_MAX, message);
    }
    if (opts.help) {
      ZuCSpan usage = "usage: zprotoc [-I <dir>]... [-o <stem>] "
	"[--syntax proto2|proto3] "
	"[--export <macro>] <name>.proto\n"
	"       zprotoc --descriptor [--file <name>] [-o <stem>] "
	"[--syntax proto2|proto3] "
	"[--export <macro>] <set.pb>\n";
      ZiFile out;
      out.openStdOut();
      return out.write(usage.data(), usage.length()) == Zi::OK ? 0 : 1;
    }
    if (n != 2 || !opts.input) fail(UINT_MAX, "expected one input schema");
    ZuCSpan inputName{opts.input};
    if (!opts.descriptor && (inputName.length() < 6 ||
      ZuCSpan{inputName.data() + inputName.length() - 6, 6} != ".proto"))
      fail(UINT_MAX, "input requires .proto extension");
    if (opts.file && !opts.descriptor)
      fail(UINT_MAX, "--file requires --descriptor");
    if (opts.api && !cppName(opts.api))
      fail(UINT_MAX, "invalid export macro name");
    ZiFile file;
    if (file.open(opts.input, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK)
      fail(UINT_MAX, "cannot open schema file");
    auto stat = file.fstat();
    if (!stat || stat.size() < 0 || uint64_t(stat.size()) > INT_MAX)
      fail(UINT_MAX, "invalid schema size");
    input.length(stat.size());
    if (stat.size() && file.read(input.data(), stat.size()) != stat.size())
      fail(UINT_MAX, "cannot read schema file");
    if (!opts.output) {
      Text stem{opts.input};
      if (!opts.descriptor) stem.length(stem.length() - 6);
      opts.output = stem;
    }
    if (opts.descriptor) {
      ZuBSpan bytes{reinterpret_cast<const uint8_t *>(input.data()),
	input.length()};
      ZuCSpan reason;
      if (!verifySchema(bytes, {}, &reason))
	fail(UINT_MAX, reason.length() ? reason : "invalid descriptor set");
      Schema schema{bytes, false};
      const FileDesc *selected = nullptr;
      if (opts.file) selected = schema.file(opts.file);
      else if (schema.descriptor().files.length()) {
	const auto &files = schema.descriptor().files;
	selected = &files[files.length() - 1];
      }
      if (!selected) fail(UINT_MAX, "descriptor file not found");
      if (opts.syntax >= 0 &&
	!schema.file(selected->name, Syntax::T(opts.syntax)))
	fail(UINT_MAX, "schema syntax mismatch");
      FileLookup files{schema.descriptor()};
      checkCycles(schema.descriptor(), files);
      Decls decls;
      collect(schema.descriptor(), decls);
      generate(schema.descriptor(), *selected, decls, files, opts.output, opts.api);
    } else {
      FileDesc desc;
      Text rootName{ZiFile::leafname(opts.input)};
      desc.name = rootName;
      Text names;
      Synthetic synthetic;
      Parser{input, synthetic}.run(desc);
      if (opts.syntax >= 0 && opts.syntax != fileSyntax(desc))
	fail(UINT_MAX, "schema syntax mismatch");
      DescSet set;
      set.files.push(ZuMv(desc));
      FileLookup files{set};
      using Paths = ZtArray<Zi::Path, ZtArrayHeapID<"zprotoc.Paths">>;
      Paths paths;
      Inputs imports;
      paths.push(opts.input);
      for (unsigned i = 0; i < set.files.length(); ++i)
	for (unsigned j = 0; j < set.files[i].dependencies.length(); ++j) {
	  Text name{set.files[i].dependencies[j]};
	  if (files.find(name) != UINT_MAX) continue;
	  Zi::Path path = ZiFile::append(ZiFile::dirname(paths[i]),
	    Zi::Path{name});
	  if (!ZiStat{path}.exists()) {
	    bool loaded = false;
	    for (const auto &dir : opts.includes) {
	      path = ZiFile::append(Zi::Path{dir}, Zi::Path{name});
	      if (ZiStat{path}.exists()) { loaded = true; break; }
	    }
	    if (!loaded) fail(UINT_MAX, "import schema not found");
	  }
	  ZiFile source;
	  if (source.open(path, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK)
	    fail(UINT_MAX, "cannot open import schema");
	  auto size = source.fstat();
	  if (!size || size.size() < 0 || uint64_t(size.size()) > INT_MAX)
	    fail(UINT_MAX, "invalid import schema size");
	  Input imported;
	  imported.length(size.size());
	  if (size.size() && source.read(imported.data(), size.size()) !=
	      size.size()) fail(UINT_MAX, "cannot read import schema");
	  imports.push(ZuMv(imported));
	  FileDesc dependency;
	  dependency.name = set.files[i].dependencies[j];
	  Parser{imports[imports.length() - 1], synthetic}.run(dependency);
	  paths.push(ZuMv(path));
	  set.files.push(ZuMv(dependency));
	  files.add(set.files.length() - 1);
	}
      checkCycles(set, files);
      Decls decls;
      collect(set, decls);
      resolveNames(decls, names);
      Output output;
      Zpb::ArrayStorage storage{output};
      Zpb::Builder builder{storage};
      auto wire = Zpb::save(builder, set);
      if (builder.failed()) fail(UINT_MAX, "cannot encode schema");
      if (!Zpb::verify<DescSet>(wire))
	fail(UINT_MAX, "invalid descriptor encoding");
      ZuCSpan reason;
      if (!verifySchema(wire, {}, &reason))
	fail(UINT_MAX, reason.length() ? reason : "invalid schema");
      generate(set, set.files[0], decls, files, opts.output, opts.api);
    }
    return 0;
  } catch (const Failure &e) {
    Text message;
    if (e.offset < input.length()) {
      unsigned line = 1, column = 1;
      for (unsigned i = 0; i < e.offset; ++i) {
	if (input[i] == '\n') { ++line; column = 1; }
	else ++column;
      }
      message << opts.input << ':' << ZuBox<unsigned>{line} << ':'
	<< ZuBox<unsigned>{column} << ": ";
    } else message << "zprotoc: ";
    message << e.message << '\n';
    err.write(message.data(), message.length());
    return 1;
  }
}
