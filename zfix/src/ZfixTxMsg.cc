//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixTxMsg.hh>
#include <zlib/ZfixIOBuf.hh>
#include <zlib/ZfixTxCapture.hh>

namespace Zfix {

using TxBodyAlloc = Zi::IOBufAlloc<TxBody, ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.TxBody">>;
using TxFrameAlloc = Zi::IOBufAlloc<ZiIOBuf, ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.TxFrame">>;

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
  ZmRef<TxBody> buf = new TxBodyAlloc{nullptr, 0};
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

TxBody::Ref TxBody::captureControl(const Session::Msg &control, Fmt::Error &error,
    unsigned mode, Fmt::Limits limits, uint64_t id)
{
  Ref result;
  error = Session::visit(control, mode,
    [&result, id, limits]<typename O, typename Facet>(const O &message) {
      Fmt::Error error;
      result = captureRaw<MT<O, O::MsgType, Facet>>(message, id, true, error, limits);
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

struct TxArchiveOutput {
  using Buffer = Zi::IOBufAlloc<ZiIOBuf, ZiIOBuf_DefltSize,
    ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.TxArchive">>;
  ZmRef<ZiIOBuf> buffer{new Buffer{nullptr, 0}};
  static ZuSpan<char> grow(void *owner, unsigned used, unsigned needed) {
    auto &buf = static_cast<TxArchiveOutput *>(owner)->buffer;
    buf->length = used;
    if (!buf->ensure(needed)) return {};
    return {buf->data(), buf->size - buf->skip};
  }
};

Zi::RxRange TxBody::archive(Fmt::Error &error, Fmt::Limits limits) const
{
  error = {};
  TxArchiveOutput output;
  Fmt::Sink sink{{output.buffer->data(), output.buffer->size}, limits,
    TxArchiveOutput::grow, &output};
  if (m_archive) m_archive(sink, *this);
  else sink << body();
  if ((error = sink.error())) return {};
  output.buffer->length = sink.length();
  return {ZuMv(output.buffer), 0, sink.length()};
}

TxFrame::TxFrame(ZuCSpan beginString, Fmt::Limits limits) :
  TxFrame{ZmRef<ZiIOBuf>{}, beginString, limits} { }

TxFrame::TxFrame(ZmRef<ZiIOBuf> buffer, ZuCSpan beginString,
    Fmt::Limits limits, unsigned tailRoom) :
  m_buffer{buffer ? ZuMv(buffer) : ZmRef<ZiIOBuf>{new TxFrameAlloc{nullptr, 0}}},
  m_tailRoom{tailRoom}, m_limits{limits},
  m_writer{{m_buffer->data(),
    m_buffer->size - m_buffer->skip > tailRoom ?
      m_buffer->size - m_buffer->skip - tailRoom : 0},
    beginString, grow, this, limits}
{
}
ZuSpan<char> TxFrame::grow(void *owner, unsigned used, unsigned needed)
{
  auto self = static_cast<TxFrame *>(owner);
  auto &buf = self->m_buffer;
  if (needed > self->m_limits.frameBytes ||
      needed > UINT32_MAX - self->m_tailRoom) return {};
  // FrameWriter may reserve an envelope beyond the initial slot. Only retain
  // initialized bytes present in that slot; outer transport room stays outside
  // the writer's arena throughout growth and final output selection.
  unsigned capacity = buf->size - buf->skip;
  buf->length = used < capacity ? used : capacity;
  if (!buf->ensure(needed + self->m_tailRoom)) return {};
  return {buf->data(), buf->size - buf->skip - self->m_tailRoom};
}
TxImage TxFrame::finish(Fmt::Error &error)
{
  return output_(m_writer.finish(), error);
}
TxImage TxFrame::output_(Fmt::FrameOutput output, Fmt::Error &error)
{
  error = output.error;
  if (error) return {};
  ZuCSpan storage{m_buffer->data_(), m_buffer->size};
  m_buffer->skip = unsigned(output.data.data() - storage.data());
  m_buffer->length = output.data.length();
  return TxImage{ZuMv(m_buffer)};
}
} // Zfix

