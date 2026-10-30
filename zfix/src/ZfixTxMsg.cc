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

uint64_t TxBody::memory() const
{
  return ZmHeapAllocSize<sizeof(TxBodyAlloc)>{} +
    ((data__ & ZiIOBuf::Jumbo) ? Zfix::jumboBytes(size) : 0);
}

ZmRef<TxBody> TxBody::start(ZuCSpan type, uint64_t id, bool admin,
    Fmt::Error &error, Fmt::Limits limits)
{
  error = {};
  if (!type.length()) { error = {Fmt::ErrorCode::Required, 35}; return {}; }
  if (type.length() > limits.fieldBytes || type.length() >= limits.frameBytes ||
      type.length() > TxBodyAlloc::MaxSize) {
    error = {Fmt::ErrorCode::Size, 35}; return {};
  }
  if (!Fmt::text(type)) { error = {Fmt::ErrorCode::Value, 35}; return {}; }
  ZmRef<TxBody> buf = new TxBodyAlloc{};
  if (!buf->ensure(type.length())) { error = {Fmt::ErrorCode::Size}; return {}; }
  memcpy(buf->data(), type.data(), type.length());

  buf->length = buf->m_typeLen = type.length();
  buf->m_id = id;
  buf->m_admin = admin;
  return buf;
}
ZuSpan<char> TxBody::grow(void *owner, unsigned used, unsigned needed)
{
  auto buf = static_cast<TxBody *>(owner);
  if (needed > TxBodyAlloc::MaxSize - buf->m_typeLen) return {};
  buf->length = buf->m_typeLen + used;
  if (!buf->ensure(buf->m_typeLen + needed)) return {};
  return buf->storage();
}
TxBody::Ref TxBody::control(const Session::Msg &control, Fmt::Error &error,
    unsigned mode, Fmt::Limits limits, uint64_t id)
{
  auto buf = start(Session::msgType(control), id, true, error, limits);
  if (!buf) return {};
  limits.frameBytes -= buf->m_typeLen;
  Fmt::Sink out{buf->storage(), limits, grow, buf.ptr()};
  error = Session::save(out, control, mode);
  if (error) return {};
  buf->length = buf->m_typeLen + out.length();
  return buf;
}

ZmRef<TxBody> TxBody::archive_(ZuCSpan type, ZuCSpan bytes, uint64_t id, bool admin,
    Fmt::Error &error, Fmt::Limits limits)
{
  if (type.length() > limits.frameBytes || bytes.length() > limits.frameBytes - type.length() ||
      type.length() > TxBodyAlloc::MaxSize ||
      bytes.length() > TxBodyAlloc::MaxSize - type.length()) {
    error = {Fmt::ErrorCode::Size}; return {};
  }
  auto buf = start(type, id, admin, error, limits);
  if (!buf) return {};
  if (!buf->ensure(buf->m_typeLen + bytes.length())) {
    error = {Fmt::ErrorCode::Size}; return {};
  }
  if (bytes.length()) memcpy(buf->data() + buf->m_typeLen, bytes.data(), bytes.length());

  buf->length = buf->m_typeLen + bytes.length();
  return buf;
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
	result = restoreControl<MT<O, O::msgType>>(bytes, id, error, limits);
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
  auto output = m_writer.finish();
  error = output.error;
  if (error) return {};
  ZuCSpan storage{m_buffer->data_(), m_buffer->size};
  m_buffer->skip = unsigned(output.data.data() - storage.data());
  m_buffer->length = output.data.length();
  return ZuMv(m_buffer);
}
} // Zfix

