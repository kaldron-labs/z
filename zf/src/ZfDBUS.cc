//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuUTF.hh>

#include <zlib/ZfDBUS.hh>

namespace ZfDBUS {

bool utf8(ZuBSpan span)
{
  auto p = span.begin();
  auto end = span.end();
  while (p != end) {
    uint32_t u;
    unsigned n = ZuUTF8::in(p, unsigned(end - p), u);
    if (!n || u == 0 || (n > 1 &&
	((n == 2 && u < 0x80) || (n == 3 && u < 0x800) ||
	 (n == 4 && (u < 0x10000 || u > 0x10ffff)) ||
	 (u >= 0xd800 && u <= 0xdfff)))) return false;
    for (unsigned i = 1; i < n; ++i)
      if ((p[i] & 0xc0) != 0x80) return false;
    p += n;
  }
  return true;
}

bool objectPath(ZuCSpan path)
{
  if (path.length() > UINT_MAX) return false;
  unsigned n = path.length();
  if (!n || path[0] != '/') return false;
  if (n == 1) return true;
  if (path[n - 1] == '/') return false;
  for (unsigned i = 1; i < n; ++i) {
    uint8_t c = path[i];
    if (c == '/') {
      if (path[i - 1] == '/') return false;
    } else if (!((c >= 'a' && c <= 'z') ||
	  (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
      return false;
  }
  return true;
}

bool signatureType(
  const uint8_t *&p, const uint8_t *end, SignatureDepth depth, bool dict)
{
  if (p == end) return false;
  uint8_t c = *p++;
  if (basicType(c) || c == Type::Variant) return true;
  if (c == Type::Array) {
    if (depth.arrays == Limit::TypeDepth) return false;
    ++depth.arrays;
    return signatureType(p, end, depth, true);
  }
  if (c == '(') {
    if (depth.structs == Limit::TypeDepth) return false;
    ++depth.structs;
    auto begin = p;
    while (p != end && *p != ')')
      if (!signatureType(p, end, depth, false)) return false;
    if (p == begin || p == end) return false;
    ++p;
    return true;
  }
  if (c == '{') {
    if (depth.structs == Limit::TypeDepth) return false;
    ++depth.structs;
    if (!dict || p == end || !basicType(*p) || *p == Type::UnixFD)
      return false;
    ++p;
    if (!signatureType(p, end, depth, false) || p == end || *p++ != '}')
      return false;
    return true;
  }
  return false;
}

bool validSignature(ZuCSpan signature, bool single)
{
  unsigned n = signature.length();
  if (n > Limit::Signature) return false;
  ZuBSpan bytes{signature};
  auto p = bytes.begin();
  auto end = bytes.end();
  unsigned count = 0;
  while (p != end) {
    if (!signatureType(p, end, {}, false)) return false;
    ++count;
  }
  return !single || count == 1;
}

bool textData(Reader &reader, uint8_t code, ZuBSpan &value)
{
  uint32_t n;
  if (code == Type::Signature) {
    if (reader.p == reader.end) return reader.fail(Error::Bounds);
    n = *reader.p++;
  } else if (!reader.align(4) || !reader.integer(n)) {
    return false;
  }
  if (!reader.bytes(n, value) || reader.p == reader.end || *reader.p++)
    return reader.fail(Error::Syntax);
  return true;
}

bool text(Reader &reader, uint8_t code, ZuBSpan &value)
{
  if (!textData(reader, code, value)) return false;
  ZuCSpan span{value};
  switch (code) {
    case Type::ObjectPath:
      return objectPath(span) || reader.fail(Error::Syntax);
    case Type::Signature:
      return validSignature(span) || reader.fail(Error::Syntax);
    default:
      return utf8(value) || reader.fail(Error::Syntax);
  }
}

bool skipValue(
  Reader &reader, const uint8_t *&p, const uint8_t *end, unsigned depth)
{
  if (p == end) return reader.fail(Error::Syntax);
  if (depth > Limit::ValueDepth) return reader.fail(Error::Overflow);
  uint8_t code = *p++;
  switch (code) {
    case Type::Byte: {
      if (reader.p == reader.end) return reader.fail(Error::Bounds);
      ++reader.p;
      return true;
    }
    case Type::Bool: {
      uint32_t value;
      if (!reader.align(4) || !reader.integer(value)) return false;
      return value <= 1 || reader.fail(Error::Syntax);
    }
    case Type::Int16: case Type::UInt16: {
      uint16_t value;
      return reader.align(2) && reader.integer(value);
    }
    case Type::Int32: case Type::UInt32: {
      uint32_t value;
      return reader.align(4) && reader.integer(value);
    }
    case Type::Int64: case Type::UInt64: case Type::Double: {
      uint64_t value;
      return reader.align(8) && reader.integer(value);
    }
    case Type::String: case Type::ObjectPath: case Type::Signature: {
      ZuBSpan value;
      return text(reader, code, value);
    }
    case Type::UnixFD:
      return reader.fail(Error::Unsupported);
    case Type::Array: {
      auto element = p;
      if (!signatureType(p, end, {}, true))
	return reader.fail(Error::Syntax);
      auto elementEnd = p;
      uint32_t n;
      if (!reader.align(4) || !reader.integer(n) ||
	  !reader.align(signatureAlignment(element))) return false;
      if (n > Limit::Array)
	return reader.fail(Error::Overflow);
      if (uint64_t(reader.end - reader.p) < n)
	return reader.fail(Error::Bounds);
      auto outerEnd = reader.end;
      reader.end = reader.p + n;
      while (reader.result && reader.p != reader.end) {
	auto before = reader.p;
	auto q = element;
	if (!skipValue(reader, q, elementEnd, depth + 1) || q != elementEnd)
	  break;
	if (reader.p == before) {
	  reader.fail(Error::Syntax);
	  break;
	}
      }
      bool ok = bool(reader.result) && reader.p == reader.end;
      reader.end = outerEnd;
      return ok;
    }
    case '(':
      if (!reader.align(8)) return false;
      if (p == end || *p == ')') return reader.fail(Error::Syntax);
      while (p != end && *p != ')')
	if (!skipValue(reader, p, end, depth + 1)) return false;
      if (p == end) return reader.fail(Error::Syntax);
      ++p;
      return true;
    case '{':
      if (!reader.align(8) || p == end || !basicType(*p) ||
	  *p == Type::UnixFD) return reader.fail(Error::Syntax);
      if (!skipValue(reader, p, end, depth + 1) ||
	  !skipValue(reader, p, end, depth + 1) ||
	  p == end || *p++ != '}') return reader.fail(Error::Syntax);
      return true;
    case Type::Variant: {
      ZuBSpan signature;
      if (!text(reader, Type::Signature, signature)) return false;
      ZuCSpan chars{signature};
      if (!validSignature(chars, true)) return reader.fail(Error::Syntax);
      auto q = signature.begin();
      auto qEnd = signature.end();
      return skipValue(reader, q, qEnd, depth + 1) && q == qEnd;
    }
    default:
      return reader.fail(Error::Syntax);
  }
}

bool skipValues(Reader &reader, ZuCSpan signature)
{
  ZuBSpan bytes{signature};
  auto p = bytes.begin();
  auto end = bytes.end();
  while (reader.result && p != end)
    if (!skipValue(reader, p, end, 0)) return false;
  return bool(reader.result) && p == end;
}

Result validate(const View &view, ZuCSpan expected)
{
  if (view.signature != expected) return {view.offset, Error::Type};
  if (!validSignature(expected)) return {view.offset, Error::Type};
  if (view.bytes.length() > UINT_MAX - view.offset)
    return {view.offset, Error::Overflow};
  Reader reader{view};
  if (reader.result) skipValues(reader, view.signature);
  if (reader.result && reader.p != reader.end) reader.fail(Error::Trailing);
  if (!reader.result) return reader.result;
  return {view.offset + unsigned(view.bytes.length()), Error::OK};
}

} // ZfDBUS
