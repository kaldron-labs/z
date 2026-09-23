//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZdbusMessage.hh>

#include "ZdbusName.hh"
#include "ZdbusValue.hh"

namespace Zdbus_ {

static unsigned align8(unsigned offset)
{
  return (offset + Wire::BodyAlignment - 1) &
    ~(Wire::BodyAlignment - 1);
}

template <typename Value>
static bool measureField(unsigned &offset, uint8_t order, Value value)
{
  unsigned start = align8(offset);
  if (start > Wire::MaxMessageSize - 4) return false;
  auto measured = ZfDBUS::measure(value,
    {.offset = start + 4, .order = order});
  if (!measured || measured.offset > Wire::MaxMessageSize) return false;
  offset = measured.offset;
  return true;
}

bool planHead(HeadPlan &plan, const HeadSpec &head, ZuCSpan signature)
{
  if (!head.serial ||
      (head.order != ZfDBUS::Order::Little &&
       head.order != ZfDBUS::Order::Big) ||
      !ZfDBUS::validSignature(signature)) return false;
  if ((head.interface.length() && !Name::interface(head.interface)) ||
      (head.member.length() && !Name::member(head.member)) ||
      (head.errorName.length() && !Name::error(head.errorName)) ||
      (head.destination.length() && !Name::bus(head.destination)))
    return false;
  switch (head.type) {
    case MessageType::MethodCall:
      if (!head.path.length() || !head.member.length()) return false;
      break;
    case MessageType::MethodReturn:
      if (!head.replySerial) return false;
      break;
    case MessageType::Error:
      if (!head.replySerial || !head.errorName.length()) return false;
      break;
    case MessageType::Signal:
      if (!head.path.length() || !head.interface.length() ||
          !head.member.length()) return false;
      break;
    default: return false;
  }
  unsigned offset = Wire::MinHeaderSize;
  if (head.path.length() &&
      !measureField(offset, head.order, PathValue{head.path})) return false;
  if (head.interface.length() &&
      !measureField(offset, head.order, TextValue{head.interface}))
    return false;
  if (head.member.length() &&
      !measureField(offset, head.order, TextValue{head.member})) return false;
  if (head.errorName.length() &&
      !measureField(offset, head.order, TextValue{head.errorName}))
    return false;
  if (head.replySerial &&
      !measureField(offset, head.order, UIntValue{head.replySerial}))
    return false;
  if (head.destination.length() &&
      !measureField(offset, head.order, TextValue{head.destination}))
    return false;
  if (signature.length() &&
      !measureField(offset, head.order, SigValue{signature})) return false;
  plan.fieldsLength = offset - Wire::MinHeaderSize;
  if (plan.fieldsLength > ZfDBUS::Limit::Array) return false;
  plan.bodyOffset = align8(offset);
  return plan.bodyOffset <= Wire::MaxMessageSize;
}

static void write32(Sink &sink, uint32_t value, uint8_t order)
{
  bool swap = order == ZfDBUS::Order::Big;
  if constexpr (Zu_BIGENDIAN) swap = !swap;
  uint32_t encoded = swap ? ZuIntrin::bswap(value) : value;
  sink << ZuBSpan{reinterpret_cast<const uint8_t *>(&encoded),
    sizeof(encoded)};
}

template <typename Value>
static void writeField(Sink &sink, unsigned code, uint8_t type,
  uint8_t order, Value value)
{
  while (sink.buf->length & (Wire::BodyAlignment - 1)) sink << char(0);
  sink << char(code) << char(1) << char(type) << char(0);
  ZfDBUS::save(sink, value,
    {.offset = sink.buf->length, .order = order});
}

void writeHead(Sink &sink, const HeadPlan &plan, const HeadSpec &head,
  ZuCSpan signature, unsigned bodyLength)
{
  ZmAssert_(!sink.buf->length);
  sink << char(head.order) << char(head.type) << char(head.flags)
    << char(Wire::Version);
  write32(sink, bodyLength, head.order);
  write32(sink, head.serial, head.order);
  write32(sink, plan.fieldsLength, head.order);
  if (head.path.length())
    writeField(sink, HeaderCode::Path, ZfDBUS::Type::ObjectPath,
      head.order, PathValue{head.path});
  if (head.interface.length())
    writeField(sink, HeaderCode::Interface, ZfDBUS::Type::String,
      head.order, TextValue{head.interface});
  if (head.member.length())
    writeField(sink, HeaderCode::Member, ZfDBUS::Type::String,
      head.order, TextValue{head.member});
  if (head.errorName.length())
    writeField(sink, HeaderCode::ErrorName, ZfDBUS::Type::String,
      head.order, TextValue{head.errorName});
  if (head.replySerial)
    writeField(sink, HeaderCode::ReplySerial, ZfDBUS::Type::UInt32,
      head.order, UIntValue{head.replySerial});
  if (head.destination.length())
    writeField(sink, HeaderCode::Destination, ZfDBUS::Type::String,
      head.order, TextValue{head.destination});
  if (signature.length())
    writeField(sink, HeaderCode::Signature, ZfDBUS::Type::Signature,
      head.order, SigValue{signature});
  ZmAssert_(sink.buf->length ==
    Wire::MinHeaderSize + plan.fieldsLength);
  while (sink.buf->length < plan.bodyOffset) sink << char(0);
}

} // Zdbus_
