//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// iconv wrapper

#ifndef ZtIconv_HH
#define ZtIconv_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <stdlib.h>	// for size_t

#include <iconv.h>

#include <zlib/ZmLock.hh>

// specialize ZtIconvFn for any output buffer type
template <class Out> struct ZtIconvFn {
  enum { N = sizeof(typename ZuTraits<Out>::Elem) };
  // set length of buffer in bytes, retaining/copying previous data if resized,
  // returning actual length obtained (which can be less than requested)
  static size_t length(Out &buf, size_t n) {
    buf.length((n + N - 1) / N);
    return buf.length() * N;
  }
  // return modifiable pointer to buffer data
  static char *data(Out &buf) {
    return reinterpret_cast<char *>(buf.data());
  }
};

class ZtIconv {
  // the required output buffer size is over-estimated using a multiplier
  constexpr double factor() { return 1.1; }	// add 10%

  // different iconv libraries have inconsistent types for iconv()'s inbuf
  // parameter - insulate ourselves accordingly
  class IconvTraits {
  private:
    typedef char	Small;
    struct		Big { char _[2]; };
    static Small	test(size_t (*)(
	  iconv_t, const char **, size_t *, char **, size_t *));
    static Big	test(...);
  public:
    enum { Const = sizeof(test(&iconv)) == sizeof(Small) };
  };
  using InBuf = ZuIf<IconvTraits::Const, const char **, char **>;

public:
  ZtIconv(const char *tocode, const char *fromcode) :
    m_cd(iconv_open(tocode, fromcode)) { }
  ~ZtIconv() {
    if (m_cd != (iconv_t)-1) iconv_close(m_cd);
  }

  ZtIconv() = delete;
  ZtIconv(const ZtIconv &) = delete;
  ZtIconv &operator =(const ZtIconv &) = delete;

  template <class Out, typename In>
  ZuIfT<ZuTraits<Out>::IsString && ZuTraits<In>::IsString, int>
  convert(Out &out, const In &in) {
    if (ZuUnlikely(m_cd == (iconv_t)-1)) return -1;
    auto inBuf = reinterpret_cast<const char *>(ZuTraits<In>::data(in));
    size_t inSize =
      ZuTraits<In>::length(in) * sizeof(typename ZuTraits<In>::Elem);
    if (ZuUnlikely(!inSize)) {
      ZtIconvFn<Out>::length(out, 0);
      return 0;
    }
    size_t outSize = inSize;
    outSize = ZtIconvFn<Out>::length(out, outSize);
    size_t inLen = inSize;
    size_t outLen = outSize;
    char *outBuf = ZtIconvFn<Out>::data(out);
  resized:
    size_t n = iconv(m_cd, (InBuf)&inBuf, &inLen, &outBuf, &outLen);
    if (n == (size_t)-1 && errno == E2BIG) {
      if (ZuUnlikely(inLen >= inSize)) inLen = 0;
      double ratio = (double)(outSize - outLen) / (double)(inSize - inLen);
      if (ZuUnlikely(ratio < 1.0)) ratio = 1.0;
      size_t newOutSize = (size_t)(ratio * factor() * (double)inSize);
      size_t minOutSize = (size_t)((double)outSize * factor());
      if (ZuUnlikely(newOutSize < minOutSize)) newOutSize = minOutSize;
      newOutSize = ZtIconvFn<Out>::length(out, newOutSize);
      if (ZuUnlikely(newOutSize <= minOutSize)) {
	outLen += (newOutSize - outSize);
	outSize = newOutSize;
	goto ret;
      }
      outBuf = ZtIconvFn<Out>::data(out) + (outSize - outLen);
      outLen += (newOutSize - outSize);
      outSize = newOutSize;
      goto resized;
    }
  ret:
    iconv(m_cd, 0, 0, 0, 0);
    ZtIconvFn<Out>::length(out, outSize - outLen);
    return outSize - outLen;
  }

private:
  iconv_t	m_cd;
};

#endif /* ZtIconv_HH */
