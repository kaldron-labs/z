//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixTxMsg.hh>
#include <zlib/ZfixIOBuf.hh>

namespace Zfix {

using TxBodyAlloc = Zi::IOBufAlloc<TxBody, ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.TxBody">>;
using TxFrameAlloc = ZiIOBufAlloc<ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, "Zfix.TxFrame">;

unsigned TxBody::TxBodyAllocMax_() { return TxBodyAlloc::MaxSize; }

uint64_t TxBody::memory() const
{
  return ZmHeapAllocSize<sizeof(TxBodyAlloc)>{} +
    ((data__ & ZiIOBuf::Jumbo) ? Zfix::jumboBytes(size) : 0);
}

ZmRef<TxBody> TxBody::start(ZuCSpan type, unsigned slots, uint64_t id, bool admin,
    Fmt::Error &error, Fmt::Limits limits)
{
  error = {};
  if (!type.length()) { error = {Fmt::ErrorCode::Required, 35}; return {}; }
  if (type.length() > limits.fieldBytes || type.length() >= limits.frameBytes ||
      type.length() > TxBodyAlloc::MaxSize) {
    error = {Fmt::ErrorCode::Size, 35}; return {};
  }
  uint64_t metadata = uint64_t(slots) * sizeof(Fmt::FieldPos);
  if (metadata > TxBodyAlloc::MaxSize || type.length() > TxBodyAlloc::MaxSize - metadata) {
    error = {Fmt::ErrorCode::Size}; return {};
  }
  ZmRef<TxBody> buf = new TxBodyAlloc{};
  if (!buf->ensure(metadata + type.length())) { error = {Fmt::ErrorCode::Size}; return {}; }
  memcpy(buf->data() + metadata, type.data(), type.length());
  buf->m_slots = slots;
  buf->m_typeLen = type.length();
  buf->length = buf->m_bodyOffset = metadata + type.length();
  buf->m_id = id;
  buf->m_admin = admin;
  return buf;
}
ZuSpan<char> TxBody::grow(void *owner, unsigned used, unsigned needed)
{
  auto buf = static_cast<TxBody *>(owner);
  if (needed > TxBodyAlloc::MaxSize - buf->m_bodyOffset) return {};
  buf->length = buf->m_bodyOffset + used;
  if (!buf->ensure(buf->m_bodyOffset + needed)) return {};
  return buf->storage();
}
TxBody::Ref TxBody::control(const Session::Msg &control, Fmt::Error &error,
    unsigned mode, Fmt::Limits limits, uint64_t id)
{
  Ref result;
  error = Session::visit(control, mode,
    [&result, id, limits]<typename O, typename Facet>(const O &message) {
      Fmt::Error error;
      result = encodeRaw<MT<O, O::MsgType, Facet>>(message, id, true, error, limits);
      return error;
    });
  return error ? Ref{} : ZuMv(result);
}

TxBody::Ref TxBody::restoreControl(ZuCSpan type, ZuCSpan bytes, uint64_t id,
    Fmt::Error &error, Fmt::Limits limits)
{
  Ref result;
  error = {Fmt::ErrorCode::Unresolved, 35};
  ZuSwitch::dispatch<Session::Msg::N>(Session::type(type),
    [bytes, id, &error, limits, &result]<typename I>(I) {
      using O = Session::Msg::Type<I{}>;
      if constexpr (!ZuIsSame<O, void>{})
	result = restoreControl<MT<O, O::MsgType>>(bytes, id, error, limits);
    });
  return result;
}

TxFrame::TxFrame(ZuCSpan beginString, Fmt::Limits limits) :
  m_buffer{new TxFrameAlloc{}},
  m_writer{{m_buffer->data(), m_buffer->size},
    beginString, grow, m_buffer.ptr(), limits}
{
}
ZuSpan<char> TxFrame::grow(void *owner, unsigned used, unsigned needed)
{
  auto buf = static_cast<ZiIOBuf *>(owner);
  if (needed > TxFrameAlloc::MaxSize) return {};
  // Initial envelope headroom may exceed an empty built-in buffer when an
  // application supplies a long BeginString. Those bytes are not initialized
  // until finish, so preserve only the part present in the current allocation.
  buf->length = used < buf->size ? used : buf->size;
  if (!buf->ensure(needed)) return {};
  return {buf->data(), buf->size};
}
ZmRef<ZiIOBuf> TxFrame::finish(Fmt::Error &error)
{
  return output_(m_writer.finish(), error);
}
ZmRef<ZiIOBuf> TxFrame::output_(Fmt::FrameOutput output, Fmt::Error &error)
{
  error = output.error;
  if (error) return {};
  ZuCSpan storage{m_buffer->data_(), m_buffer->size};
  m_buffer->skip = unsigned(output.data.data() - storage.data());
  m_buffer->length = output.data.length();
  return ZuMv(m_buffer);
}
} // Zfix

