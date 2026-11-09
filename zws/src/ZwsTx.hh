//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// RFC 6455 zero-copy transmit layer

#ifndef ZwsTx_HH
#define ZwsTx_HH

#ifndef ZwsLib_HH
#include <zlib/ZwsLib.hh>
#endif

#include <zlib/ZmRef.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiTxStream.hh>

#include <zlib/ZwsProtocol.hh>

namespace Zws {

template <typename Lower, bool Masked, typename Random>
class TxLayer :
  public ZiTxLayer<TxLayer<Lower, Masked, Random>, Lower> {
  using Base = ZiTxLayer<TxLayer<Lower, Masked, Random>, Lower>;

public:
  TxLayer(
    Lower &lower, Opcode::T opcode,
    Random *random = nullptr) :
      Base{lower, Masked ? 14U : 10U, 0},
      m_lower{lower}, m_random{random}, m_opcode{opcode}
  {
    if constexpr (Masked)
      ZiAssert(m_random, "Zws", (), "masked Tx requires RNG", m_valid = false; this->fail());
  }

  ~TxLayer() { flush(); }

  template <typename P>
  TxLayer &operator <<(P &&p) {
    if (m_finalized) {
      m_valid = false;
      this->fail();
      return *this;
    }
    static_cast<Base &>(*this) << ZuFwd<P>(p);
    m_appended = true;
    return *this;
  }
  TxLayer &operator <<(Zi::Flush) {
    flush();
    return *this;
  }
  TxLayer &operator <<(ZmRef<ZiIOBuf> buf) {
    if (!buf || buf->failed()) { this->fail(); return *this; }
    if (control(m_opcode) && m_appended)
      return *this << buf->cspan();
    if (!allocated_(m_lower, buf.ptr(), 0))
      return *this << buf->cspan();
    if (m_finalized) { m_valid = false; this->fail(); return *this; }
    static_cast<Base &>(*this) << ZuMv(buf);
    m_appended = true;
    return *this;
  }

  ZmRef<ZiIOBuf> allocBuf(unsigned headRoom) {
    if (!control(m_opcode)) {
	if (!flushPartial_()) return nullptr;
    }
    return Base::allocBuf(headRoom);
  }

  // Transfer mutable output from the destination writer. Foreign or replay
  // storage uses the span overload and is serialized into final output.
  bool appendOwned(ZmRef<ZiIOBuf> buf) {
    uint64_t reserve = uint64_t(this->headRoom()) + this->tailRoom();
    if (m_finalized || !valid() || !buf || buf->failed() ||
	reserve >= this->maxSize() ||
	buf->skip < this->headRoom() || buf->avail() < this->tailRoom() ||
	buf->length > this->maxSize() - reserve) {
      this->fail();
      return false;
    }
    // Control payload remains in one final frame. An existing prefix makes
    // serialization into that final output buffer necessary and bounded.
    if (control(m_opcode) && m_appended) {
      *this << buf->cspan();
      return valid();
    }
    // Publish an earlier fragment before changing the HTTP writer's actual
    // first-header allocation authority. This message continues in buf.
    if (!flushPartial_() || !takeOwned_(m_lower, buf, 0)) {
      this->fail();
      return false;
    }
    *this << ZuMv(buf);
    return valid();
  }

  bool flush() {
    if (m_finalized) return valid();
    unsigned emitted = m_emitted;
    bool ok = Base::flush();
    if (ok && m_valid && m_emitted == emitted) ok = emitEmpty();
    if (!ok || !m_valid) this->fail();
    m_finalized = true;
    return valid();
  }

  bool valid() const { return m_valid && !this->failed(); }
  using Base::fail;

  bool prepareBuf_(ZiIOBuf *buf, bool final) {
    return prepare_(buf, final && !m_partial);
  }

private:
  bool flushPartial_() {
    m_partial = !control(m_opcode);
    bool ok = Base::flush();
    m_partial = false;
    return ok;
  }

  template <typename T>
  static auto allocated_(const T &lower, const ZiIOBuf *buf, int)
    -> decltype(lower.allocatedBuf(buf)) { return lower.allocatedBuf(buf); }
  template <typename T>
  static bool allocated_(const T &, const ZiIOBuf *, ...) { return true; }
  template <typename T>
  static auto takeOwned_(T &lower, ZmRef<ZiIOBuf> buf, int)
    -> decltype(lower.allocatedBuf(ZuMv(buf))) {
    return lower.allocatedBuf(ZuMv(buf));
  }
  template <typename T>
  static bool takeOwned_(T &, ZmRef<ZiIOBuf>, ...) { return true; }

  bool prepare_(ZiIOBuf *buf, bool final) {
    if (!m_valid) {
      buf->length = 0;
      return false;
    }
    uint64_t length = buf->length;
    Opcode::T opcode = m_emitted ? Opcode::Continuation : m_opcode;
    if (control(opcode) && (!final || length > MaxControl)) {
      m_valid = false;
      buf->length = 0;
      return false;
    }

    unsigned ext = length < 126 ? 0 : length <= 0xffff ? 2 : 8;
    unsigned hdrLen = 2 + ext + (Masked ? 4 : 0);
    ZiAssert(
      buf->skip >= hdrLen,
      "Zws", (), "WebSocket Tx headroom error", m_valid = false; return false);
    auto payload = buf->data();
    buf->rewind(hdrLen);
    auto hdr = buf->data();
    hdr[0] = (final ? 0x80 : 0) | uint8_t(opcode);
    if (!ext)
      hdr[1] = (Masked ? 0x80 : 0) | uint8_t(length);
    else if (ext == 2) {
      hdr[1] = (Masked ? 0x80 : 0) | 126;
      hdr[2] = uint8_t(length>>8);
      hdr[3] = uint8_t(length);
    } else {
      hdr[1] = (Masked ? 0x80 : 0) | 127;
      for (unsigned i = 0; i < 8; ++i)
	hdr[2 + i] = uint8_t(length>>(56 - (i<<3)));
    }

    if constexpr (Masked) {
      uint8_t key[4];
      if (ZuUnlikely(!m_random || !m_random->random(key))) {
	m_valid = false;
	buf->length = 0;
	return false;
      }
      unsigned keyOffset = 2 + ext;
      for (unsigned i = 0; i < 4; ++i) hdr[keyOffset + i] = key[i];
      uint32_t key32 =
	(uint32_t(key[0])<<24) | (uint32_t(key[1])<<16) |
	(uint32_t(key[2])<<8) | key[3];
      mask({payload, unsigned(length)}, key32);
    }
    ++m_emitted;
    return true;
  }

private:
  bool emitEmpty() {
    auto buf = this->allocBuf_(this->headRoom());
    if (ZuUnlikely(!buf || buf->failed() || !prepareBuf_(buf, true)))
      return false;
    m_lower << ZuMv(buf) << Zi::Flush{};
    return !m_lower.failed();
  }

  Lower		&m_lower;
  Random	*m_random = nullptr;
  unsigned	m_emitted = 0;
  Opcode::T	m_opcode;
  bool		m_valid = true;
  bool		m_finalized = false;
  bool		m_partial = false;
  bool		m_appended = false;
};

template <bool Masked, typename Random = void, typename Lower>
auto txLayer(
  Lower &lower, Opcode::T opcode, Random *random = nullptr)
{
  return TxLayer<Lower, Masked, Random>{lower, opcode, random};
}

} // namespace Zws

#endif /* ZwsTx_HH */
