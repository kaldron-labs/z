//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuUTF.hh>

#include <zlib/ZmLHash.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZfTOML.hh>

namespace ZfTOMLError {

ZuCSpan reason(uint8_t code)
{
  switch (code) {
    case ZfTOML::SyntaxErrorCode::Syntax: return {};
    case ZfTOML::SyntaxErrorCode::UTF8: return "invalid UTF-8";
    case ZfTOML::SyntaxErrorCode::Unicode: return "invalid Unicode scalar";
    case ZfTOML::SyntaxErrorCode::Control: return "invalid control character";
    case ZfTOML::SyntaxErrorCode::Newline: return "invalid newline";
    case ZfTOML::SyntaxErrorCode::Escape: return "invalid escape";
    case ZfTOML::SyntaxErrorCode::Key: return "invalid key";
    case ZfTOML::SyntaxErrorCode::Value: return "invalid value";
    case ZfTOML::SyntaxErrorCode::Integer: return "invalid integer";
    case ZfTOML::SyntaxErrorCode::IntegerRange: return "integer out of range";
    case ZfTOML::SyntaxErrorCode::Float: return "invalid float";
    case ZfTOML::SyntaxErrorCode::DateTime: return "invalid date/time";
    case ZfTOML::SyntaxErrorCode::DuplicateKey: return "duplicate key";
    case ZfTOML::SyntaxErrorCode::Redefine: return "redefined table";
    case ZfTOML::SyntaxErrorCode::TableConflict: return "table conflict";
    case ZfTOML::SyntaxErrorCode::InlineSealed: return "sealed inline table";
    case ZfTOML::SyntaxErrorCode::ArrayConflict: return "array conflict";
    case ZfTOML::SyntaxErrorCode::DepthMax: return "depth limit exceeded";
    case ZfTOML::SyntaxErrorCode::NodeMax: return "node limit exceeded";
    case ZfTOML::SyntaxErrorCode::TrailingInput: return "trailing input";
  }
  return {};
}

} // ZfTOMLError

namespace ZfTOML {

namespace ObjState { enum { Root, Implicit, Dotted, Header, Inline, Element }; }
namespace ArrayState { enum { Static, Tables }; }

struct TableState {
  AnyNode	*node = nullptr;
  uint8_t	kind = ObjState::Implicit;
  uint8_t	array = ArrayState::Static;
};

struct SourcePos {
  unsigned	offset = UINT_MAX;
  unsigned	line = 1;
  unsigned	lineOffset = 0;
};

struct HeaderPlan {
  AnyNode	*target = nullptr;
  unsigned	missing = 0;
};

using States = ZtArray<
  TableState, ZtArrayHeapID<"ZfTOML.TableState">>;
using KeyParts = ZtArray<
  AnyNode::String, ZtArrayHeapID<"ZfTOML.KeyPath">>;

static const char *stateIndexID() { return "ZfTOML.TableStateIndex"; }
using StateIndex = ZmLHashKV<
  AnyNode *, unsigned,
  ZmLHashID<stateIndexID, ZmLHashLocal<>>>;

enum {
  StateLinearMax = 32, // measured mainstream configuration-table count
  KeyScratchMax = 8,  // covers conventional dotted configuration-key depth
  UTF8Max = 4,        // Unicode scalar UTF-8 encoding maximum (RFC 3629)
  DecimalDigits = 18, // ZuDecimal's fixed fractional precision
  ExponentMax = 1000000 // bounds work while accepting extreme float exponents
};

struct NumberValue {
  long double	floating = 0;
  ZuDecimal	decimal;
};

static long double scale10(long double v, int64_t exponent)
{
  if (!v) return v;
  bool divide = exponent < 0;
  uint64_t n = divide ? uint64_t(-(exponent + 1)) + 1 : exponent;
  long double factor = 10;
  while (n) {
    if (n & 1) v = divide ? v / factor : v * factor;
    n >>= 1;
    if (n) factor *= factor;
  }
  return v;
}

static bool decimalNumber(
    ZuCSpan s, NumberValue &out, bool *decimalValid_ = nullptr)
{
  unsigned i = 0, n = s.length(), fraction = 0, keptFraction = 0;
  bool negative = false, digit = false, underscore = false;
  uint128_t integral = 0, fractional = 0;
  constexpr uint128_t IntegralMax =
    uint128_t(ZuDecimal::maximum()) / ZuDecimal::scale();
  bool decimalValid = true;
  if (!n) return false;
  char c = s[i];
  if (c == '+' || c == '-') {
    negative = c == '-';
    ++i;
    if (i >= n) return false;
  }
  for (; i < n; ++i) {
    c = s[i];
    if (c == '_') {
      if (!digit || underscore || i + 1 >= n) return false;
      char next = s[i + 1];
      if (next < '0' || next > '9') return false;
      underscore = true;
      continue;
    }
    if (c < '0' || c > '9') break;
    unsigned v = c - '0';
    out.floating = out.floating * 10 + v;
    if (integral > (IntegralMax - v) / 10)
      decimalValid = false;
    else
      integral = integral * 10 + v;
    digit = true;
    underscore = false;
  }
  if (!digit || underscore) return false;
  if (i < n && s[i] == '.') {
    ++i;
    digit = underscore = false;
    for (; i < n; ++i) {
      c = s[i];
      if (c == '_') {
        if (!digit || underscore || i + 1 >= n) return false;
        char next = s[i + 1];
        if (next < '0' || next > '9') return false;
        underscore = true;
        continue;
      }
      if (c < '0' || c > '9') break;
      unsigned v = c - '0';
      out.floating = out.floating * 10 + v;
      ++fraction;
      if (keptFraction < DecimalDigits) {
        fractional = fractional * 10 + v;
        ++keptFraction;
      }
      digit = true;
      underscore = false;
    }
    if (!digit || underscore) return false;
  }
  int64_t exponent = 0;
  if (i < n && ((c = s[i]) == 'e' || c == 'E')) {
    if (++i >= n) return false;
    bool exponentNegative = false;
    c = s[i];
    if (c == '+' || c == '-') {
      exponentNegative = c == '-';
      ++i;
      if (i >= n) return false;
    }
    digit = underscore = false;
    for (; i < n; ++i) {
      c = s[i];
      if (c == '_') {
        if (!digit || underscore || i + 1 >= n) return false;
        char next = s[i + 1];
        if (next < '0' || next > '9') return false;
        underscore = true;
        continue;
      }
      if (c < '0' || c > '9') return false;
      if (exponent < ExponentMax) exponent = exponent * 10 + c - '0';
      digit = true;
      underscore = false;
    }
    if (!digit || underscore) return false;
    if (exponentNegative) exponent = -exponent;
  }
  if (i != n) return false;
  int64_t floatingExponent = exponent - int64_t(fraction);
  out.floating = scale10(out.floating, floatingExponent);
  if (negative) out.floating = -out.floating;

  if (!decimalValid) {
    if (decimalValid_) *decimalValid_ = false;
    return true;
  }
  uint128_t magnitude = integral * ZuDecimal::scale();
  if (keptFraction)
    magnitude += fractional *
      ZuDecimalFn::pow10_64(DecimalDigits - keptFraction);
  if (magnitude && exponent > 0) {
    if (exponent > DecimalDigits ||
        magnitude > uint128_t(ZuDecimal::maximum()) /
          ZuDecimalFn::pow10_64(exponent))
      decimalValid = false;
    else
      magnitude *= ZuDecimalFn::pow10_64(exponent);
  } else if (magnitude && exponent < 0) {
    uint64_t shift = uint64_t(-(exponent + 1)) + 1;
    magnitude = shift > DecimalDigits ? 0 :
      magnitude / ZuDecimalFn::pow10_64(shift);
  }
  if (decimalValid) {
    int128_t value = magnitude;
    if (negative) value = -value;
    out.decimal = ZuDecimal{ZuDecimal::Unscaled{value}};
  }
  if (decimalValid_) *decimalValid_ = decimalValid;
  return true;
}

ZuTuple<int, ZuDecimal> TOMLPolicy::decimalEOV(ZuCSpan span)
{
  auto integer = tomlIntEOV<ZuBox<int64_t>>(span);
  if (integer.p<0>() >= 0 && unsigned(integer.p<0>()) == span.length())
    return {integer.p<0>(), ZuDecimal{integer.p<1>().val()}};
  NumberValue out;
  bool valid;
  if (!decimalNumber(span, out, &valid) || !valid)
    return {-1, ZuDecimal{}};
  return {int(span.length()), out.decimal};
}

ZuTuple<int, double> TOMLPolicy::floatEOV(ZuCSpan span)
{
  if (span == "inf" || span == "+inf")
    return {int(span.length()), ZuCmp<double>::inf()};
  if (span == "-inf")
    return {int(span.length()), -ZuCmp<double>::inf()};
  if (span == "nan" || span == "+nan" || span == "-nan")
    return {int(span.length()), ZuIntrin::nan<double>()};
  auto integer = tomlIntEOV<ZuBox<int64_t>>(span);
  if (integer.p<0>() >= 0 && unsigned(integer.p<0>()) == span.length())
    return {integer.p<0>(), double(integer.p<1>().val())};
  NumberValue out;
  if (!decimalNumber(span, out)) return {-1, 0};
  double value = out.floating;
  if (value == ZuCmp<double>::inf() || value == -ZuCmp<double>::inf())
    return {-1, 0};
  return {int(span.length()), value};
}

struct Scan::Impl {
  Scan		&scan;
  ZuCSpan	in;
  States	&states;
  StateIndex	stateIndex;
  Limits	limits;
  unsigned	pos = 0;
  unsigned	line = 1;
  unsigned	lineOffset = 0;
  unsigned	nodes = 0;
  AnyNode	*root = nullptr;
  AnyNode	*table = nullptr;

  Impl(Scan &scan_, States &states_) :
    scan{scan_}, in{scan_.m_span}, states{states_}, limits{scan_.m_limits} { }

  char at(unsigned i) const { return i < in.length() ? in[i] : 0; }

  void step()
  {
    if (at(pos) == '\r') {
      ++pos;
      if (at(pos) == '\n') ++pos;
      ++line;
      lineOffset = pos;
    } else if (at(pos) == '\n') {
      ++pos;
      ++line;
      lineOffset = pos;
    } else {
      ++pos;
    }
  }

  bool bad(uint8_t code = SyntaxErrorCode::Syntax)
  {
    if (!scan.m_error.failed) {
      scan.m_error.offset = pos;
      scan.m_error.line = line;
      scan.m_error.column = pos - lineOffset + 1;
      scan.m_error.ch = at(pos);
      scan.m_error.code = code;
      scan.m_error.failed = true;
    }
    return false;
  }

  SourcePos sourcePos() const { return {pos, line, lineOffset}; }

  bool badAt(uint8_t code, SourcePos source)
  {
    if (source.offset == UINT_MAX) return bad(code);
    if (!scan.m_error.failed) {
      scan.m_error.offset = source.offset;
      scan.m_error.line = source.line;
      scan.m_error.column = source.offset - source.lineOffset + 1;
      scan.m_error.ch = at(source.offset);
      scan.m_error.code = code;
      scan.m_error.failed = true;
    }
    return false;
  }

  template <typename Data, typename ...Args>
  ZuPtr<AnyNode> node(AnyNode *parent, Args &&...args)
  {
    if (nodes >= limits.nodes) {
      bad(SyntaxErrorCode::NodeMax);
      return {};
    }
    ++nodes;
    return newNode<Data>(parent, ZuFwd<Args>(args)...);
  }

  void hws()
  {
    for (;;) {
      char c = at(pos);
      if (c != ' ' && c != '\t') return;
      ++pos;
    }
  }

  bool comment()
  {
    if (at(pos) != '#') return true;
    unsigned begin = ++pos;
    for (;;) {
      uint8_t c = at(pos);
      if (!c || c == '\r' || c == '\n') break;
      if ((c < 0x20 && c != '\t') || c == 0x7f)
        return bad(SyntaxErrorCode::Control);
      ++pos;
    }
    return utf8(begin, pos);
  }

  bool trivia()
  {
    for (;;) {
      hws();
      if (!comment()) return false;
      if (at(pos) != '\r' && at(pos) != '\n') return true;
      step();
    }
  }

  bool lineEnd()
  {
    hws();
    if (!comment()) return false;
    if (!at(pos)) return true;
    if (at(pos) != '\r' && at(pos) != '\n') return bad();
    step();
    return true;
  }

  bool utf8(unsigned begin, unsigned end)
  {
    while (begin < end) {
      uint8_t c = in[begin];
      if (c < 0x80) { ++begin; continue; }
      auto span = ZuUTF<char, char>::gspan(
        ZuCSpan{in.data() + begin, end - begin});
      unsigned n = span.inLen();
      if (!n) { pos = begin; return bad(SyntaxErrorCode::UTF8); }
      uint32_t u;
      if (ZuUTF<uint32_t, char>::cvt(
          ZuSpan<uint32_t>{&u, 1},
          ZuCSpan{in.data() + begin, n}) != 1 ||
          u > 0x10ffff || (u >= 0xd800 && u <= 0xdfff)) {
        pos = begin;
        return bad(SyntaxErrorCode::UTF8);
      }
      for (unsigned i = 1; i < n; ++i)
        if ((uint8_t(in[begin + i]) & 0xc0) != 0x80) {
          pos = begin + i;
          return bad(SyntaxErrorCode::UTF8);
        }
      if ((n == 2 && u < 0x80) || (n == 3 && u < 0x800) ||
          (n == 4 && u < 0x10000)) {
        pos = begin;
        return bad(SyntaxErrorCode::UTF8);
      }
      begin += n;
    }
    return true;
  }

  bool putUTF8(AnyNode::String &out, uint32_t u)
  {
    if (u > 0x10ffff || (u >= 0xd800 && u <= 0xdfff))
      return bad(SyntaxErrorCode::Unicode);
    ZuCArray<UTF8Max> buf;
    unsigned n = ZuUTF<char, uint32_t>::cvt(
      ZuSpan<char>{buf.data(), UTF8Max}, ZuSpan<const uint32_t>{&u, 1});
    if (!n) return bad(SyntaxErrorCode::Unicode);
    out.append(buf.data(), n);
    return true;
  }

  bool hex(unsigned digits_, uint32_t &u)
  {
    u = 0;
    for (unsigned i = 0; i < digits_; ++i) {
      char c = at(pos);
      unsigned v = c >= '0' && c <= '9' ? c - '0' :
        c >= 'a' && c <= 'f' ? c - 'a' + 10 :
        c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
      if (v >= 16) return bad(SyntaxErrorCode::Escape);
      u = (u << 4) | v;
      ++pos;
    }
    return true;
  }

  bool string(AnyNode::String &out, bool key = false)
  {
    char quote = at(pos);
    if (quote != '"' && quote != '\'') return bad(SyntaxErrorCode::Value);
    bool basic = quote == '"';
    bool multi = at(pos + 1) == quote && at(pos + 2) == quote;
    pos += multi ? 3 : 1;
    if (multi && (at(pos) == '\r' || at(pos) == '\n')) step();
    unsigned raw = pos;
    for (;;) {
      char c = at(pos);
      if (!c) return bad(SyntaxErrorCode::Value);
      if (c == quote) {
        unsigned run = 1;
        while (at(pos + run) == quote) ++run;
        if ((!multi && run >= 1) || (multi && run >= 3)) {
          if (!utf8(raw, pos)) return false;
          out.append(in.data() + raw, pos - raw);
          pos += multi ? 3 : 1;
          unsigned extra = run - (multi ? 3 : 1);
          if (extra > 2) return bad(SyntaxErrorCode::Value);
          while (extra--) out << quote;
          return true;
        }
      }
      if (c == '\r' || c == '\n') {
        if (!multi) return bad(SyntaxErrorCode::Newline);
        if (!utf8(raw, pos)) return false;
        out.append(in.data() + raw, pos - raw);
        out << '\n';
        step();
        raw = pos;
        continue;
      }
      if ((uint8_t(c) < 0x20 && c != '\t') || uint8_t(c) == 0x7f)
        return bad(SyntaxErrorCode::Control);
      if (!basic || c != '\\') { ++pos; continue; }
      if (!utf8(raw, pos)) return false;
      out.append(in.data() + raw, pos - raw);
      ++pos;
      if (multi && (at(pos) == '\r' || at(pos) == '\n')) {
        step();
        while (at(pos) == ' ' || at(pos) == '\t' ||
            at(pos) == '\r' || at(pos) == '\n') {
          if (at(pos) == '\r' || at(pos) == '\n') step();
          else ++pos;
        }
        raw = pos;
        continue;
      }
      c = at(pos++);
      switch (c) {
        case 'b': out << '\b'; break;
        case 't': out << '\t'; break;
        case 'n': out << '\n'; break;
        case 'f': out << '\f'; break;
        case 'r': out << '\r'; break;
        case 'e': out << '\x1b'; break;
        case '"': out << '"'; break;
        case '\\': out << '\\'; break;
        case 'x': case 'u': case 'U': {
          uint32_t u;
          if (!hex(c == 'x' ? 2 : c == 'u' ? 4 : 8, u) ||
              !putUTF8(out, u)) return false;
        } break;
        default: return bad(SyntaxErrorCode::Escape);
      }
      raw = pos;
    }
  }

  bool keyPart(AnyNode::String &out)
  {
    hws();
    if (at(pos) == '"' || at(pos) == '\'') return string(out, true);
    unsigned begin = pos;
    while ((at(pos) >= 'a' && at(pos) <= 'z') ||
        (at(pos) >= 'A' && at(pos) <= 'Z') ||
        (at(pos) >= '0' && at(pos) <= '9') ||
        at(pos) == '_' || at(pos) == '-') ++pos;
    if (pos == begin) return bad(SyntaxErrorCode::Key);
    out.append(in.data() + begin, pos - begin);
    return true;
  }

  bool keyPath(KeyParts &parts)
  {
    for (;;) {
      new (parts.push()) AnyNode::String{};
      if (!keyPart(parts[parts.length() - 1])) return false;
      hws();
      if (at(pos) != '.') return true;
      ++pos;
      hws();
    }
  }

  TableState *state(AnyNode *node_)
  {
    if (states.length() > StateLinearMax) {
      auto found = stateIndex.find(node_);
      return found ? &states[found->p<1>()] : nullptr;
    }
    for (unsigned i = 0, n = states.length(); i < n; ++i)
      if (auto &state = states[i]; state.node == node_) return &state;
    return nullptr;
  }

  void addState(AnyNode *node_, uint8_t kind, uint8_t array = ArrayState::Static)
  {
    new (states.push()) TableState{node_, kind, array};
    unsigned n = states.length();
    if (n > StateLinearMax + 1) {
      stateIndex.add(node_, n - 1);
    } else if (n > StateLinearMax) {
      for (unsigned i = 0; i < n; ++i) {
	auto &state = states[i];
	stateIndex.add(state.node, i);
      }
    }
  }

  AnyNode::Field *field(AnyNode *object, ZuCSpan id)
  {
    auto &fields = object->data<AnyNode::Object>();
    for (unsigned i = 0, n = fields.length(); i < n; ++i) {
      auto &field = fields[i];
      if (field.p<0>() == id) return &field;
    }
    return nullptr;
  }

  AnyNode *latest(AnyNode *array)
  {
    auto &items = array->data<AnyNode::Array>();
    unsigned n = items.length();
    if (!n) return nullptr;
    return items[n - 1];
  }

  AnyNode *walk(AnyNode *object, ZuCSpan id, uint8_t kind)
  {
    if (auto s = state(object); s && s->kind == ObjState::Inline &&
        kind != ObjState::Inline) {
      bad(SyntaxErrorCode::InlineSealed);
      return nullptr;
    }
    auto f = field(object, id);
    if (f) {
      AnyNode *child = f->p<1>();
      if (child->has<AnyNode::Object>()) return child;
      if (child->has<AnyNode::Array>()) {
        auto s = state(child);
        if (s && s->array == ArrayState::Tables) {
          child = latest(child);
          if (child) return child;
        }
      }
      bad(SyntaxErrorCode::TableConflict);
      return nullptr;
    }
    auto child = node<AnyNode::Object>(object);
    if (!child) return nullptr;
    AnyNode *ptr = child;
    object->data<AnyNode::Object>().push(
      AnyNode::Field{AnyNode::String{id}, ZuMv(child)});
    addState(ptr, kind);
    return ptr;
  }

  bool put(
      AnyNode *object, KeyParts &parts, ZuPtr<AnyNode> value,
      uint8_t kind = ObjState::Dotted, SourcePos source = {})
  {
    unsigned n = parts.length();
    AnyNode *target = object;
    unsigned missing = n - 1;
    for (unsigned i = 0; i + 1 < n; ++i) {
      if (auto s = state(target); s && s->kind == ObjState::Inline &&
          kind != ObjState::Inline)
        return badAt(SyntaxErrorCode::InlineSealed, source);
      auto f = field(target, parts[i]);
      if (!f) { missing = i; break; }
      AnyNode *child = f->p<1>();
      if (child->has<AnyNode::Object>()) {
        target = child;
      } else if (child->has<AnyNode::Array>()) {
        auto s = state(child);
        if (!s || s->array != ArrayState::Tables || !(target = latest(child)))
          return badAt(SyntaxErrorCode::TableConflict, source);
      } else {
        return badAt(SyntaxErrorCode::TableConflict, source);
      }
    }
    if (missing == n - 1) {
      if (auto s = state(target); s && s->kind == ObjState::Inline &&
          kind != ObjState::Inline)
        return badAt(SyntaxErrorCode::InlineSealed, source);
      if (field(target, parts[n - 1]))
        return badAt(SyntaxErrorCode::DuplicateKey, source);
    } else if (nodes + (n - 1 - missing) > limits.nodes) {
      return badAt(SyntaxErrorCode::NodeMax, source);
    }
    object = target;
    for (unsigned i = missing; i + 1 < n; ++i) {
      object = walk(object, parts[i], kind);
      if (!object) return false;
    }
    value->parent = object;
    object->data<AnyNode::Object>().push(
      AnyNode::Field{ZuMv(parts[n - 1]), ZuMv(value)});
    return true;
  }

  bool integer(ZuCSpan token)
  {
    unsigned i = 0, n = token.length(), base = 10;
    bool negative = false;
    char c = token[i];
    if (c == '+' || c == '-') {
      negative = c == '-';
      ++i;
      if (i >= n) return false;
    }
    if (i + 2 <= n && token[i] == '0') {
      c = token[i + 1];
      if (i && (c == 'x' || c == 'o' || c == 'b')) return false;
      switch (c) {
        case 'x': base = 16; i += 2; break;
        case 'o': base = 8; i += 2; break;
        case 'b': base = 2; i += 2; break;
      }
    }
    if (i >= n) return false;
    uint64_t maximum = negative ? uint64_t(INT64_MAX) + 1 : uint64_t(INT64_MAX);
    uint64_t value = 0;
    bool digit = false, underscore = false;
    for (; i < n; ++i) {
      c = token[i];
      if (c == '_') {
        if (!digit || underscore || i + 1 == n) return false;
        underscore = true;
        continue;
      }
      unsigned v = c >= 'a' ? c - 'a' + 10 :
	c >= 'A' ? c - 'A' + 10 : c - '0';
      if (v >= base) return false;
      if (value > (maximum - v) / base) {
        bad(SyntaxErrorCode::IntegerRange);
        return false;
      }
      value = value * base + v;
      digit = true;
      underscore = false;
    }
    return digit && !underscore;
  }

  bool floating(ZuCSpan token)
  {
    if (token == "inf" || token == "+inf" || token == "-inf" ||
        token == "nan" || token == "+nan" || token == "-nan") return true;
    NumberValue out;
    return decimalNumber(token, out);
  }

  static bool dec2(ZuCSpan s, unsigned i, int &v)
  {
    if (i + 2 > s.length()) return false;
    char c0 = s[i], c1 = s[i + 1];
    if (c0 < '0' || c0 > '9' || c1 < '0' || c1 > '9') return false;
    v = (c0 - '0') * 10 + c1 - '0';
    return true;
  }

  bool dateTime(ZuCSpan s, ZuDateTime &out)
  {
    unsigned n = s.length(), i = 0;
    int y = 1970, m = 1, d = 1, H = 0, M = 0, S = 0, nsec = 0;
    bool date = n >= 10 && s[0] >= '0' && s[0] <= '9' && s[4] == '-' &&
      s[7] == '-';
    if (date) {
      y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 +
        (s[2] - '0') * 10 + s[3] - '0';
      if (!dec2(s, 5, m) || !dec2(s, 8, d)) return false;
      i = 10;
      if (i == n) {
        out = ZuDateTime{y, m, d};
        int y_, m_, d_;
        out.ymd(y_, m_, d_);
        return y == y_ && m == m_ && d == d_;
      }
      if (s[i] != 'T' && s[i] != 't' && s[i] != ' ') return false;
      ++i;
    }
    if (i + 5 > n || !dec2(s, i, H) || s[i + 2] != ':' ||
        !dec2(s, i + 3, M)) return false;
    i += 5;
    if (i < n && s[i] == ':') {
      if (!dec2(s, i + 1, S)) return false;
      i += 3;
    }
    if (i < n && s[i] == '.') {
      unsigned begin = ++i, scale = 100000000;
      while (i < n && s[i] >= '0' && s[i] <= '9') {
        if (scale) nsec += (s[i] - '0') * scale, scale /= 10;
        ++i;
      }
      if (i == begin) return false;
    }
    int offset = 0;
    if (i < n && (s[i] == 'Z' || s[i] == 'z')) ++i;
    else if (i < n && (s[i] == '+' || s[i] == '-')) {
      bool minus = s[i++] == '-';
      int oh, om;
      if (!dec2(s, i, oh) || i + 5 > n || s[i + 2] != ':' ||
          !dec2(s, i + 3, om) || oh > 23 || om > 59) return false;
      i += 5;
      offset = (oh * 60 + om) * 60;
      if (minus) offset = -offset;
    }
    if (i != n || H > 23 || M > 59 || S > 59) return false;
    out = ZuDateTime{y, m, d, H, M, S, nsec};
    int y_, m_, d_, H_, M_, S_;
    out.ymd(y_, m_, d_);
    out.hms(H_, M_, S_);
    if (y != y_ || m != m_ || d != d_ || H != H_ || M != M_ || S != S_)
      return false;
    if (offset) out = out - offset;
    return true;
  }

  ZuPtr<AnyNode> value(unsigned depth)
  {
    hws();
    if (depth > limits.depth) { bad(SyntaxErrorCode::DepthMax); return {}; }
    if (at(pos) == '"' || at(pos) == '\'') {
      auto out = node<AnyNode::String>(nullptr);
      if (!out || !string(out->data<AnyNode::String>())) return {};
      return out;
    }
    if (at(pos) == '[') return array(depth + 1);
    if (at(pos) == '{') return inlineTable(depth + 1);
    unsigned begin = pos;
    bool dateSpace = false;
    while (at(pos)) {
      char c = at(pos);
      if (c == ',' || c == ']' || c == '}' || c == '#' ||
          c == '\r' || c == '\n' || c == '\t') break;
      if (c == ' ') {
        if (!dateSpace && pos == begin + 10 && at(pos + 1) >= '0' &&
            at(pos + 1) <= '9') dateSpace = true;
        else break;
      }
      ++pos;
    }
    unsigned end = pos;
    while (end > begin && in[end - 1] == ' ') --end;
    if (begin == end) { bad(SyntaxErrorCode::Value); return {}; }
    ZuCSpan token{in.data() + begin, end - begin};
    if (token == "true" || token == "false") {
      auto out = node<AnyNode::String>(nullptr, token);
      if (out) out->scalarType = token == "true" ? ScalarTC::True : ScalarTC::False;
      return out;
    }
    ZuDateTime date;
    bool looksDate = token.length() >= 5 &&
      (token[2] == ':' || token[4] == '-');
    if (looksDate) {
      if (dateTime(token, date)) return node<AnyNode::DateTime>(nullptr, date);
      bad(SyntaxErrorCode::DateTime);
      return {};
    }
    bool isFloat = token == "inf" || token == "+inf" || token == "-inf" ||
      token == "nan" || token == "+nan" || token == "-nan";
    bool based = token.length() > 2 && token[0] == '0' &&
      (token[1] == 'x' || token[1] == 'o' || token[1] == 'b');
    for (unsigned i = 0, n = token.length(); i < n && !isFloat; ++i)
      if (char c = token[i];
          c == '.' || (!based && (c == 'e' || c == 'E')))
        isFloat = true;
    bool ok = isFloat ? floating(token) : integer(token);
    if (!ok) {
      if (!scan.m_error.failed)
        bad(isFloat ? SyntaxErrorCode::Float : SyntaxErrorCode::Integer);
      return {};
    }
    auto out = node<AnyNode::String>(nullptr, token);
    if (out) out->scalarType = ScalarTC::Number;
    return out;
  }

  ZuPtr<AnyNode> array(unsigned depth)
  {
    if (depth > limits.depth) { bad(SyntaxErrorCode::DepthMax); return {}; }
    auto out = node<AnyNode::Array>(nullptr);
    if (!out) return {};
    AnyNode *parent = out;
    addState(parent, ObjState::Implicit, ArrayState::Static);
    ++pos;
    if (!trivia()) return {};
    if (at(pos) == ']') { ++pos; return out; }
    for (;;) {
      auto item = value(depth);
      if (!item) return {};
      item->parent = parent;
      parent->data<AnyNode::Array>().push(ZuMv(item));
      hws();
      if (!comment()) return {};
      if (at(pos) == ']') { ++pos; return out; }
      if (at(pos) != ',') { bad(SyntaxErrorCode::Value); return {}; }
      ++pos;
      if (!trivia()) return {};
      if (at(pos) == ']') { ++pos; return out; }
    }
  }

  ZuPtr<AnyNode> inlineTable(unsigned depth)
  {
    if (depth > limits.depth) { bad(SyntaxErrorCode::DepthMax); return {}; }
    auto out = node<AnyNode::Object>(nullptr);
    if (!out) return {};
    AnyNode *parent = out;
    addState(parent, ObjState::Inline);
    ++pos;
    if (!trivia()) return {};
    if (at(pos) == '}') { ++pos; return out; }
    for (;;) {
      SourcePos source = sourcePos();
      auto parts = ZtScratch(KeyParts, KeyScratchMax);
      if (!keyPath(parts)) return {};
      hws();
      if (at(pos) != '=') { bad(SyntaxErrorCode::Key); return {}; }
      ++pos;
      auto item = value(depth);
      if (!item || !put(
          parent, parts, ZuMv(item), ObjState::Inline, source)) return {};
      hws();
      if (!comment()) return {};
      if (at(pos) == '}') { ++pos; return out; }
      if (at(pos) != ',') { bad(SyntaxErrorCode::Value); return {}; }
      ++pos;
      if (!trivia()) return {};
      if (at(pos) == '}') { ++pos; return out; }
    }
  }

  bool pair()
  {
    SourcePos source = sourcePos();
    auto parts = ZtScratch(KeyParts, KeyScratchMax);
    if (!keyPath(parts)) return false;
    hws();
    if (at(pos) != '=') return bad(SyntaxErrorCode::Key);
    ++pos;
    auto item = value(0);
    if (!item || !lineEnd()) return false;
    return put(table, parts, ZuMv(item), ObjState::Dotted, source);
  }

  bool checkHeader(
      KeyParts &parts, bool tables, SourcePos source, HeaderPlan &plan)
  {
    unsigned n = parts.length();
    AnyNode *target = root;
    unsigned missing = n - 1;
    for (unsigned i = 0; i + 1 < n; ++i) {
      if (auto s = state(target); s && s->kind == ObjState::Inline)
        return badAt(SyntaxErrorCode::InlineSealed, source);
      auto f = field(target, parts[i]);
      if (!f) { missing = i; break; }
      AnyNode *child = f->p<1>();
      if (child->has<AnyNode::Object>()) {
        target = child;
      } else if (child->has<AnyNode::Array>()) {
        auto s = state(child);
        if (!s || s->array != ArrayState::Tables || !(target = latest(child)))
          return badAt(SyntaxErrorCode::TableConflict, source);
      } else {
        return badAt(SyntaxErrorCode::TableConflict, source);
      }
    }
    if (auto s = state(target); s && s->kind == ObjState::Inline)
      return badAt(SyntaxErrorCode::InlineSealed, source);
    AnyNode::Field *f = nullptr;
    if (missing == n - 1) f = field(target, parts[n - 1]);
    if (f) {
      AnyNode *child = f->p<1>();
      auto s = state(child);
      if (tables) {
        if (child->has<AnyNode::Object>() && s &&
            s->kind == ObjState::Inline)
          return badAt(SyntaxErrorCode::InlineSealed, source);
        if (!child->has<AnyNode::Array>() || !s ||
            s->array != ArrayState::Tables)
          return badAt(SyntaxErrorCode::ArrayConflict, source);
      } else {
        if (!child->has<AnyNode::Object>())
          return badAt(SyntaxErrorCode::TableConflict, source);
        if (s && s->kind == ObjState::Inline)
          return badAt(SyntaxErrorCode::InlineSealed, source);
        if (!s || s->kind != ObjState::Implicit)
          return badAt(SyntaxErrorCode::Redefine, source);
      }
    }
    unsigned required = missing == n - 1 ? 0 : n - 1 - missing;
    required += tables ? 1 + !f : !f;
    if (nodes + required > limits.nodes)
      return badAt(SyntaxErrorCode::NodeMax, source);

    plan.target = target;
    plan.missing = missing;
    return true;
  }

  bool commitHeader(KeyParts &parts, bool tables, const HeaderPlan &plan)
  {
    unsigned n = parts.length();
    AnyNode *object = plan.target;
    for (unsigned i = plan.missing; i + 1 < n; ++i) {
      object = walk(object, parts[i], ObjState::Implicit);
      if (!object) return false;
    }
    auto f = field(object, parts[n - 1]);
    if (tables) {
      AnyNode *array_;
      if (!f) {
        auto value = node<AnyNode::Array>(object);
        if (!value) return false;
        array_ = value;
        object->data<AnyNode::Object>().push(
          AnyNode::Field{ZuMv(parts[n - 1]), ZuMv(value)});
        addState(array_, ObjState::Implicit, ArrayState::Tables);
      } else {
        array_ = f->template p<1>();
      }
      auto element = node<AnyNode::Object>(array_);
      if (!element) return false;
      table = element;
      array_->data<AnyNode::Array>().push(ZuMv(element));
      addState(table, ObjState::Element);
    } else {
      if (!f) {
        table = walk(object, parts[n - 1], ObjState::Header);
        if (!table) return false;
        state(table)->kind = ObjState::Header;
      } else {
        table = f->template p<1>();
        auto s = state(table);
        s->kind = ObjState::Header;
      }
    }
    return true;
  }

  bool header()
  {
    SourcePos source = sourcePos();
    ++pos;
    bool tables = at(pos) == '[';
    if (tables) ++pos;
    auto parts = ZtScratch(KeyParts, KeyScratchMax);
    if (!keyPath(parts)) return false;
    hws();
    if (at(pos) != ']' || (tables && at(pos + 1) != ']'))
      return bad(SyntaxErrorCode::Key);
    pos += tables ? 2 : 1;
    if (!lineEnd()) return false;
    HeaderPlan plan;
    return checkHeader(parts, tables, source, plan) &&
      commitHeader(parts, tables, plan);
  }

  ZuTuple<int, ZuPtr<const AnyNode>> run()
  {
    if (!in.data()) {
      bad();
      return {-1, ZuPtr<const AnyNode>{}};
    }
    if (in.length() >= 3 && uint8_t(in[0]) == 0xef &&
        uint8_t(in[1]) == 0xbb && uint8_t(in[2]) == 0xbf) pos = 3;
    auto root_ = node<AnyNode::Object>(nullptr);
    if (!root_) return {-1, ZuPtr<const AnyNode>{}};
    root = root_;
    table = root;
    addState(root, ObjState::Root);
    if (!trivia()) return {-1, ZuPtr<const AnyNode>{}};
    while (at(pos)) {
      if (!(at(pos) == '[' ? header() : pair()))
        return {-1, ZuPtr<const AnyNode>{}};
      if (!trivia()) return {-1, ZuPtr<const AnyNode>{}};
    }
    ZuPtr<const AnyNode> result = ZuMv(root_);
    return {int(in.length()), ZuMv(result)};
  }
};

ZuTuple<int, ZuPtr<const AnyNode>> Scan::scan()
{
  // 32 metadata records covers ordinary application configuration files;
  // ZtScratch transparently promotes larger table-heavy documents.
  auto states = ZtScratch(States, StateLinearMax);
  return Impl{*this, states}.run();
}

ZuTuple<int, ZuPtr<const AnyNode>> scan(ZuCSpan span, Limits limits)
{
  Scan scan{span, limits};
  auto out = scan.scan();
  if (out.p<0>() >= 0) return out;
  const auto &error = scan.error();
  throw ZfTOML_EXCEPT(ZfTOMLError::badSyntax(
    error.line, error.column, error.offset, error.ch, error.code));
}

} // ZfTOML
