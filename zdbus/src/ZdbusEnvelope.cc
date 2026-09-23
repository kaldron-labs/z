//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZdbusEnvelope.hh>

#include "ZdbusName.hh"

namespace Zdbus_ {

static uint32_t u32(const uint8_t *p, uint8_t order)
{
  uint32_t value;
  memcpy(&value, p, sizeof(value));
  if constexpr (Zu_BIGENDIAN) {
    if (order == ZfDBUS::Order::Little) value = ZuIntrin::bswap(value);
  } else if (order == ZfDBUS::Order::Big)
    value = ZuIntrin::bswap(value);
  return value;
}

static bool header(FrameResult &result, ZuBSpan bytes)
{
  auto &info = result.info;
  auto p = bytes.begin();
  auto fields = ZuBSpan{p + Wire::PrefixSize,
    unsigned(sizeof(uint32_t) + info.fieldsLength)};
  ZfDBUS::Reader reader{{fields, "a(yv)", Wire::PrefixSize, info.order}};
  uint32_t length;
  if (!reader.integer(length) || length != info.fieldsLength ||
      !reader.align(8)) {
    result.offset = reader.result.offset;
    result.error = FrameError::Header;
    return false;
  }
  unsigned seen = 0;
  while (reader.p != reader.end) {
    if (!reader.align(8) || reader.p == reader.end) break;
    unsigned fieldOffset = reader.view.offset +
      unsigned(reader.p - reader.view.bytes.begin());
    unsigned code = *reader.p++;
    ZuBSpan sig;
    if (!code || !ZfDBUS::text(reader, ZfDBUS::Type::Signature, sig)) {
      result.offset = fieldOffset;
      result.error = FrameError::Header;
      return false;
    }
    uint8_t expected = 0;
    switch (code) {
      case HeaderCode::Path: expected = ZfDBUS::Type::ObjectPath; break;
      case HeaderCode::Signature: expected = ZfDBUS::Type::Signature; break;
      case HeaderCode::ReplySerial: case HeaderCode::UnixFDs:
        expected = ZfDBUS::Type::UInt32; break;
      case HeaderCode::Interface: case HeaderCode::Member:
      case HeaderCode::ErrorName: case HeaderCode::Destination:
      case HeaderCode::Sender: expected = ZfDBUS::Type::String; break;
      default: break;
    }
    if (expected && (sig.length() != 1 || sig[0] != expected)) {
      result.offset = fieldOffset;
      result.error = FrameError::Header;
      return false;
    }
    unsigned valueOffset = reader.view.offset +
      unsigned(reader.p - reader.view.bytes.begin());
    bool relevant = true;
    switch (code) {
      case HeaderCode::Path:
      case HeaderCode::Interface:
      case HeaderCode::Member:
        relevant = info.type == MessageType::MethodCall ||
          info.type == MessageType::Signal;
        break;
      case HeaderCode::ErrorName:
        relevant = info.type == MessageType::Error;
        break;
      case HeaderCode::ReplySerial:
        relevant = info.type == MessageType::MethodReturn ||
          info.type == MessageType::Error;
        break;
      default:
        relevant = code <= HeaderCode::UnixFDs;
        break;
    }
    if (!relevant) {
      auto q = sig.begin();
      if (!ZfDBUS::skipValue(reader, q, sig.end(), 0) || q != sig.end()) {
	result.offset = reader.result ? fieldOffset : reader.result.offset;
	result.error = FrameError::Header;
	return false;
      }
      continue;
    }
    if (code <= HeaderCode::UnixFDs) {
      unsigned mask = 1U << code;
      if (seen & mask) {
        result.offset = fieldOffset;
        result.error = FrameError::Header;
        return false;
      }
      seen |= mask;
    }
    auto readText = [&reader](uint8_t type, ZuCSpan &target) {
      ZuBSpan value;
      if (!ZfDBUS::text(reader, type, value)) return false;
      target = ZuCSpan{value};
      return true;
    };
    auto readUInt = [&reader](uint32_t &target) {
      return reader.align(4) && reader.integer(target);
    };
    bool loaded = true;
    switch (code) {
      case HeaderCode::Path:
        loaded = readText(ZfDBUS::Type::ObjectPath, info.headers.path); break;
      case HeaderCode::Interface:
        loaded = readText(ZfDBUS::Type::String, info.headers.interface) &&
          Name::interface(info.headers.interface); break;
      case HeaderCode::Member:
        loaded = readText(ZfDBUS::Type::String, info.headers.member) &&
          Name::member(info.headers.member); break;
      case HeaderCode::ErrorName:
        loaded = readText(ZfDBUS::Type::String, info.headers.errorName) &&
          Name::error(info.headers.errorName); break;
      case HeaderCode::Destination:
        loaded = readText(ZfDBUS::Type::String, info.headers.destination) &&
          Name::bus(info.headers.destination); break;
      case HeaderCode::Sender:
        loaded = readText(ZfDBUS::Type::String, info.headers.sender) &&
          Name::bus(info.headers.sender); break;
      case HeaderCode::Signature:
        loaded = readText(ZfDBUS::Type::Signature, info.headers.signature);
        break;
      case HeaderCode::ReplySerial:
        loaded = readUInt(info.headers.replySerial); break;
      case HeaderCode::UnixFDs:
        loaded = readUInt(info.headers.unixFDs); break;
      default: break;
    }
    if (!loaded) {
      result.offset = reader.result ? valueOffset : reader.result.offset;
      result.error = FrameError::Header;
      return false;
    }
  }
  if (!reader.result || reader.p != reader.end) {
    result.offset = reader.result.offset;
    result.error = FrameError::Header;
    return false;
  }
  bool required = true;
  switch (info.type) {
    case MessageType::MethodCall:
      required = info.headers.path.length() && info.headers.member.length();
      break;
    case MessageType::MethodReturn:
      required = info.headers.replySerial;
      break;
    case MessageType::Error:
      required = info.headers.replySerial && info.headers.errorName.length();
      break;
    case MessageType::Signal:
      required = info.headers.path.length() &&
        info.headers.interface.length() && info.headers.member.length();
      break;
  }
  if (!required) {
    result.offset = Wire::PrefixSize;
    result.error = FrameError::Required;
    return false;
  }
  if (info.headers.unixFDs) {
    result.offset = Wire::PrefixSize;
    result.error = FrameError::Unsupported;
    return false;
  }
  auto body = ZuBSpan{p + info.bodyOffset, info.bodyLength};
  auto checked = ZfDBUS::validate(
    {body, info.headers.signature, info.bodyOffset, info.order},
    info.headers.signature);
  if (!checked) {
    result.offset = checked.offset;
    result.error = FrameError::Body;
    return false;
  }
  return true;
}

FrameResult frame(ZuBSpan bytes, unsigned limit)
{
  FrameResult result;
  unsigned n = bytes.length();
  if (n < Wire::MinHeaderSize) return result;
  auto p = bytes.begin();
  auto &info = result.info;
  info.order = p[0];
  if (info.order != ZfDBUS::Order::Little &&
      info.order != ZfDBUS::Order::Big) {
    result.error = FrameError::Order;
    return result;
  }
  info.type = p[1];
  if (!info.type) {
    result.offset = 1;
    result.error = FrameError::Type;
    return result;
  }
  info.flags = p[2];
  if (p[3] != Wire::Version) {
    result.offset = 3;
    result.error = FrameError::Version;
    return result;
  }
  info.bodyLength = u32(p + 4, info.order);
  info.serial = u32(p + 8, info.order);
  if (!info.serial) {
    result.offset = 8;
    result.error = FrameError::Serial;
    return result;
  }
  info.fieldsLength = u32(p + Wire::PrefixSize, info.order);
  uint64_t fieldsEnd = uint64_t(Wire::MinHeaderSize) + info.fieldsLength;
  uint64_t bodyOffset = (fieldsEnd + Wire::BodyAlignment - 1) &
    ~(uint64_t(Wire::BodyAlignment) - 1);
  uint64_t total = bodyOffset + info.bodyLength;
  if (info.fieldsLength > ZfDBUS::Limit::Array ||
      total > limit || total > Wire::MaxMessageSize) {
    result.offset = Wire::PrefixSize;
    result.error = FrameError::Size;
    return result;
  }
  info.bodyOffset = unsigned(bodyOffset);
  info.total = unsigned(total);
  if (n < info.total) return result;

  if (!header(result, bytes)) return result;
  for (unsigned i = unsigned(fieldsEnd); i < info.bodyOffset; ++i)
    if (p[i]) {
      result.offset = i;
      result.error = FrameError::Padding;
      return result;
    }
  result.offset = info.total;
  result.error = FrameError::OK;
  return result;
}

} // Zdbus_
