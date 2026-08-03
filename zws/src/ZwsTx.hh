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

#include <zlib/ZiAssert.hh>
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
      ZiAssert(m_random, "Zws", (), "masked Tx requires RNG", m_valid = false);
  }

  ~TxLayer() = default;

  template <typename P>
  TxLayer &operator <<(P &&p) {
    if (m_finalized) {
      m_valid = false;
      return *this;
    }
    static_cast<Base &>(*this) << ZuFwd<P>(p);
    return *this;
  }
  TxLayer &operator <<(Zi::Flush) {
    flush();
    return *this;
  }

  void flush() {
    if (m_finalized) return;
    unsigned emitted = m_emitted;
    Base::flush();
    if (m_emitted == emitted) emitEmpty_();
    m_lower.flush();
    m_finalized = true;
  }

  bool valid() const { return m_valid; }

  void prepareBuf_(ZiIOBuf *buf, bool final) {
    if (!m_valid) {
      buf->length = 0;
      return;
    }
    uint64_t length = buf->length;
    Opcode::T opcode = m_emitted ? Opcode::Continuation : m_opcode;
    if (control(opcode) && (!final || length > MaxControl)) {
      m_valid = false;
      buf->length = 0;
      return;
    }

    unsigned ext = length < 126 ? 0 : length <= 0xffff ? 2 : 8;
    unsigned hdrLen = 2 + ext + (Masked ? 4 : 0);
    ZiAssert(
      buf->skip >= hdrLen,
      "Zws", (), "WebSocket Tx headroom error", m_valid = false; return);
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
	return;
      }
      unsigned keyOffset = 2 + ext;
      for (unsigned i = 0; i < 4; ++i) hdr[keyOffset + i] = key[i];
      uint32_t key32 =
	(uint32_t(key[0])<<24) | (uint32_t(key[1])<<16) |
	(uint32_t(key[2])<<8) | key[3];
      mask({payload, unsigned(length)}, key32);
    }
    ++m_emitted;
  }

private:
  void emitEmpty_() {
    auto buf = m_lower.allocBuf_(this->headRoom());
    if (ZuUnlikely(!buf)) { m_valid = false; return; }
    prepareBuf_(buf, true);
    if (!m_lower.sendBuf_(ZuMv(buf), true)) m_valid = false;
  }

  Lower		&m_lower;
  Random	*m_random = nullptr;
  unsigned	m_emitted = 0;
  Opcode::T	m_opcode;
  bool		m_valid = true;
  bool		m_finalized = false;
};

template <bool Masked, typename Random = void, typename Lower>
auto txLayer(
  Lower &lower, Opcode::T opcode, Random *random = nullptr)
{
  return TxLayer<Lower, Masked, Random>{lower, opcode, random};
}

} // namespace Zws

#endif /* ZwsTx_HH */
