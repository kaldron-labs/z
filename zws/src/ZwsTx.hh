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

template <typename Lower, typename = void>
struct RetainsBuffers : ZuFalse { };
template <typename Lower>
struct RetainsBuffers<Lower, decltype(Lower::RetainBuffers, void())> :
  ZuBool<Lower::RetainBuffers> { };

template <typename Lower, typename = void>
struct HasAllocatedBuf : ZuFalse { };
template <typename Lower>
struct HasAllocatedBuf<Lower, decltype(
  ZuDeclVal<const Lower &>().allocatedBuf_(ZuDeclVal<const ZiIOBuf *>()), void())> :
  ZuTrue { };

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
    return *this;
  }
  TxLayer &operator <<(Zi::Flush) {
    flush();
    return *this;
  }

  // Only mutable unpublished output from this destination's writer is owned.
  // Queued/prebuilt caller and replay storage must use ordinary ranges.
  bool appendOwned_(ZmRef<ZiIOBuf> buf) {
    if (m_finalized) { m_valid = false; this->fail(); return false; }
    if (!valid()) return false;
    if (!buf || buf->failed()) { this->fail(); return false; }
    if constexpr (RetainsBuffers<Lower>{} || HasAllocatedBuf<Lower>{}) {
      // Preserve a pending frame as non-final before transferring authority.
      // The TCP-specific setter also publishes its older pending first header.
      if (!this->flush_(false) || !m_lower.allocatedBuf_(buf)) {
	this->fail(); return false;
      }
    }
    *this << ZuMv(buf);
    return valid();
  }

  bool flush() {
    if (m_finalized) return valid();
    unsigned emitted = m_emitted;
    bool ok = Base::flush();
    if (ok && m_valid && m_emitted == emitted) ok = emitEmpty_();
    if (!ok || !m_valid) this->fail();
    m_finalized = true;
    return valid();
  }

  bool valid() const { return m_valid && !this->failed(); }
  using Base::fail;

  bool prepareAppend_(ZmRef<ZiIOBuf> &buf) {
    if constexpr (RetainsBuffers<Lower>{} || HasAllocatedBuf<Lower>{}) {
      // Exact writer storage is final frame output, including later parts.
      if (allocated_(m_lower, buf.ptr(), 0)) return false;
      if (control(m_opcode)) {
	// A control packet has one actual final wire buffer. Its bounded
	// application bytes are serialized there, without a staging buffer.
	static_cast<Base &>(*this) << buf->cspan();
	buf = nullptr;
      } else {
	auto n = buf->length;
	this->appendRange(Zi::RxRange{ZuMv(buf), 0, n});
      }
      return true;
    }
    return false;
  }

  bool prepareBuf_(ZiIOBuf *buf, bool final) { return prepare_(buf, final, {}); }

  bool prepareRange_(Zi::RxRange range) {
    // Control frames use their existing single, final owned-buffer path.
    if (m_finalized || control(m_opcode)) return false;
    unsigned max = this->maxSize() - this->headRoom() - this->tailRoom();
    while (range.length) {
      unsigned n = range.length < max ? range.length : max;
      if (!n) return false;
      auto buf = this->allocBuf_(this->headRoom());
      if (!buf) return false;
      auto part = range.cspan().trunc(n);
      if constexpr (Masked) {
	if (!buf->ensure(n + this->tailRoom())) return false;
	buf->length = n;
      }
      if (!prepare_(buf.ptr(), false, part)) return false;
      m_lower << ZuMv(buf);
      if (m_lower.failed()) return false;
      if constexpr (!Masked)
	if (!m_lower.appendRange(range.subspan(0, n))) return false;
      range.offset += n;
      range.length -= n;
    }
    return true;
  }

private:
  template <typename T, typename = decltype(
    ZuDeclVal<const T &>().allocatedBuf_(ZuDeclVal<const ZiIOBuf *>()))>
  static bool allocated_(const T &lower, const ZiIOBuf *buf, int) {
    return lower.allocatedBuf_(buf);
  }
  template <typename T>
  static bool allocated_(const T &, const ZiIOBuf *, ...) { return false; }

  bool prepare_(ZiIOBuf *buf, bool final, ZuBSpan source) {
    if (!m_valid) {
      buf->length = 0;
      return false;
    }
    uint64_t length = source.data() ? source.length() : buf->length;
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
      if (source.data()) {
	for (unsigned i = 0; i < length; ++i)
	  payload[i] = source[i] ^ key[i & 3];
      } else mask({payload, unsigned(length)}, key32);
    }
    ++m_emitted;
    return true;
  }

private:
  bool emitEmpty_() {
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
};

template <bool Masked, typename Random = void, typename Lower>
auto txLayer(
  Lower &lower, Opcode::T opcode, Random *random = nullptr)
{
  return TxLayer<Lower, Masked, Random>{lower, opcode, random};
}

} // namespace Zws

#endif /* ZwsTx_HH */
