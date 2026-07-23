//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// buffered file transmit stream

#ifndef ZiFileTxStream_HH
#define ZiFileTxStream_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuArray.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuRef.hh>

#include <zlib/ZmHeap.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiTxStream.hh>

struct ZiFileTxStream_Defaults {
  enum { BufSize = (8<<10) }; // 8k
};

template <unsigned BufSize_, typename NTP = ZiFileTxStream_Defaults>
struct ZiFileTxBufSize : public NTP {
  enum { BufSize = BufSize_ };
};

namespace Zi {

template <unsigned BufSize_, typename Heap = ZuVoid>
struct FileTxBuf_ :
    public Heap, public ZuObject, public ZuBArray<BufSize_> {
  enum { BufSize = BufSize_ };
};

template <unsigned BufSize>
using FileTxBuf = FileTxBuf_<
  BufSize, ZmHeap<"ZiFile.TxBuf", FileTxBuf_<BufSize>>>;

template <typename NTP = ZiFileTxStream_Defaults>
class FileTxStream {
  FileTxStream(const FileTxStream &) = delete;
  FileTxStream &operator =(const FileTxStream &) = delete;

public:
  static constexpr unsigned maxSize = NTP::BufSize;
  using Buf = FileTxBuf<maxSize>;

  FileTxStream(ZiFile &file) : m_file{file} { }
  ~FileTxStream() { flush(); }

private:
  ZuRef<Buf> allocBuf_() { return new Buf; }
  void sendBuf_() { m_file.write(m_buf->data(), m_buf->length()); }

  void ensureBuf() { if (!m_buf) m_buf = allocBuf_(); }
  void sendBuf() {
    sendBuf_();
    m_buf = allocBuf_();
  }
  void flushBuf() {
    sendBuf_();
    m_buf = {};
  }

public:
  void append(const uint8_t *data, unsigned length) {
    if (!length) return;
    if (ZuUnlikely(length > maxSize)) {
      flush();
      m_file.write(data, length);
      return;
    }
    ensureBuf();
    if (length > maxSize - m_buf->length()) sendBuf();
    m_buf->append(ZuBSpan{data, length});
  }

private:
  template <typename U, typename R = void>
  using MatchPDelegate = ZuIfT<ZuPrint<U>::Delegate, R>;
  template <typename U, typename R = void>
  using MatchPBuffer = ZuIfT<ZuPrint<U>::Buffer, R>;

  template <typename P>
  MatchPDelegate<P> append(P &&p) {
    ZuPrint<P>::print(*this, ZuFwd<P>(p));
  }
  template <typename P>
  MatchPBuffer<P> append(const P &p) {
    unsigned length = ZuPrint<P>::length(p);
    if (ZuUnlikely(length > maxSize))
      throw ZeEXCEPT(Fatal, "ZiFileTxStream", ([length](auto &s) {
	s << "output length " << length << " exceeds maximum size " << maxSize;
      }));
    ensureBuf();
    unsigned bufLen = m_buf->length();
    if (length > maxSize - bufLen) {
      sendBuf();
      bufLen = 0;
    }
    m_buf->length(bufLen + ZuPrint<P>::print(
	reinterpret_cast<char *>(m_buf->data() + bufLen), length, p));
  }

  template <typename U, typename R = void>
  using MatchChar = ZuIfT<ZuEquiv<U, char>{}, R>;

  template <typename U, typename R = void>
  using MatchReal = ZuIfT<
    ZuTraits<U>::IsPrimitive &&
    ZuTraits<U>::IsReal &&
    !ZuEquiv<U, char>{}, R>;

  template <typename U, typename R = void>
  using MatchPrint = ZuIfT<
    ZuPrint<U>::OK && !ZuPrint<U>::String, R>;

public:
  FileTxStream &operator <<(ZuBSpan buf) {
    append(buf.data(), buf.length());
    return *this;
  }
  template <typename C>
  MatchChar<C, FileTxStream &> operator <<(C c) {
    return *this << ZuSpan{&c, 1};
  }
  template <typename R>
  MatchReal<R, FileTxStream &> operator <<(const R &r) {
    append(ZuBoxed(r));
    return *this;
  }
  template <typename P>
  MatchPrint<P, FileTxStream &> operator <<(const P &p) {
    append(p);
    return *this;
  }

  void flush() { if (m_buf && m_buf->length()) flushBuf(); }
  FileTxStream &operator <<(Flush) {
    flush();
    return *this;
  }

private:
  ZiFile	&m_file;
  ZuRef<Buf>	m_buf;
};

} // Zi

template <typename NTP = ZiFileTxStream_Defaults>
using ZiFileTxStream = Zi::FileTxStream<NTP>;

#endif /* ZiFileTxStream_HH */
