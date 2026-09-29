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
#include <zlib/ZuDerive.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuRef.hh>

#include <zlib/ZmHeap.hh>

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
ZuDerive(FileTxBufHeap, (ZmHeap<"ZiFile.TxBuf", FileTxBuf_<BufSize>>));
template <unsigned BufSize>
ZuDerive(FileTxBuf, (FileTxBuf_<BufSize, FileTxBufHeap<BufSize>>));

template <typename NTP = ZiFileTxStream_Defaults>
class FileTxStream {
  FileTxStream(const FileTxStream &) = delete;
  FileTxStream &operator =(const FileTxStream &) = delete;

public:
  static constexpr unsigned maxSize = NTP::BufSize;
  using Buf = FileTxBuf<maxSize>;

  FileTxStream(ZiFile &file) : m_file{file} { }
  ~FileTxStream() { flush(); }

  bool failed() const { return m_failed; }
  bool operator !() const { return m_failed; }
  ZuOpBool

private:
  ZuRef<Buf> allocBuf_() { return new Buf; }
  void fail() { m_buf = {}; m_failed = true; }
  bool ensureBuf() {
    if (!m_buf) m_buf = allocBuf_();
    if (ZuUnlikely(!m_buf)) { fail(); return false; }
    return true;
  }
  bool sendBuf() {
    if (ZuUnlikely(!flush())) return false;
    return ensureBuf();
  }

public:
  void append(const uint8_t *data, unsigned length) {
    if (!length || ZuUnlikely(m_failed)) return;
    if (ZuUnlikely(length > maxSize)) {
      if (ZuUnlikely(!flush())) return;
      if (ZuUnlikely(m_file.write(data, length) != Zi::OK)) fail();
      return;
    }
    if (ZuUnlikely(!ensureBuf())) return;
    if (length > maxSize - m_buf->length() && ZuUnlikely(!sendBuf())) return;
    m_buf->append(ZuBSpan{data, length});
  }

private:
  template <typename U, typename R = void>
  using MatchPDelegate = ZuIfT<ZuPrint<U>::Delegate, R>;
  template <typename U, typename R = void>
  using MatchPBuffer = ZuIfT<ZuPrint<U>::Buffer, R>;

  template <typename P, typename = MatchPDelegate<P>>
  void append(P &&p) {
    if (ZuUnlikely(m_failed)) return;
    ZuPrint<P>::print(*this, ZuFwd<P>(p));
  }
  template <typename P, typename = MatchPBuffer<P>>
  void append(const P &p) {
    if (ZuUnlikely(m_failed)) return;
    unsigned length = ZuPrint<P>::length(p);
    if (ZuUnlikely(length > maxSize)) {
      m_failed = true; // preceding output may still flush
      return;
    }
    if (!length || ZuUnlikely(!ensureBuf())) return;
    unsigned bufLen = m_buf->length();
    if (length > maxSize - bufLen) {
      if (ZuUnlikely(!sendBuf())) return;
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
  template <typename C, typename = MatchChar<C>>
  FileTxStream & operator <<(C c) {
    return *this << ZuSpan{&c, 1};
  }

  template <typename R,
    typename = ZuIfT<
      (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuEquiv<R, char>{}) ||
      (ZuPrint<R>::OK && !ZuPrint<R>::String)>>
  FileTxStream & operator <<(const R &r) {
    if constexpr (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuEquiv<R, char>{}) {
      append(ZuBoxed(r));
      return *this;
    } else {
      append(r);
      return *this;
    }
  }

  bool flush() {
    if (m_buf && m_buf->length()) {
      if (ZuUnlikely(m_file.write(m_buf->data(), m_buf->length()) != Zi::OK))
	m_failed = true;
    }
    m_buf = {};
    return !m_failed;
  }
  FileTxStream &operator <<(Flush) {
    flush();
    return *this;
  }

private:
  ZiFile	&m_file;
  ZuRef<Buf>	m_buf;
  bool		m_failed = false;
};

} // Zi

template <typename NTP = ZiFileTxStream_Defaults>
using ZiFileTxStream = Zi::FileTxStream<NTP>;

#endif /* ZiFileTxStream_HH */
