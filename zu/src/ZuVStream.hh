//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// monomorphic (type-erased) meta-stream
// - uses function pointers to wrap any stream type into a single
//   realized type for compiled interfaces

#ifndef ZuVStream_HH
#define ZuVStream_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuTraits.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuUTF.hh>

class ZuVStreamBuf {
public:
  template <typename T>
  ZuVStreamBuf(const T &v) noexcept :
    m_ptr{&v},
    m_lengthFn{[](const void *v) -> unsigned {
	return ZuPrint<T>::length(*static_cast<const T *>(v));
    }},
    m_printFn{[](const void *v, char *buf, unsigned n) -> unsigned {
	return ZuPrint<T>::print(buf, n, *static_cast<const T *>(v));
    }} { }

  ZuVStreamBuf(const ZuVStreamBuf &) = default;
  ZuVStreamBuf &operator =(const ZuVStreamBuf &) = default;
  ZuVStreamBuf(ZuVStreamBuf &&) = default;
  ZuVStreamBuf &operator =(ZuVStreamBuf &&) = default;
  ~ZuVStreamBuf() = default;

  unsigned length() const {
    return m_lengthFn(m_ptr);
  }
  unsigned print(char *buf, unsigned n) const {
    return m_printFn(m_ptr, buf, n);
  }

  struct PrintType : public ZuPrintBuffer {
    static unsigned length(const ZuVStreamBuf &b) {
      return b.length();
    }
    static unsigned print(char *buf, unsigned n, const ZuVStreamBuf &b) {
      return b.print(buf, n);
    }
  };
  friend PrintType ZuPrintType(ZuVStreamBuf *);

private:
  typedef unsigned (*LengthFn)(const void *);
  typedef unsigned (*PrintFn)(const void *, char *, unsigned);

  const void	*m_ptr;
  LengthFn	m_lengthFn;
  PrintFn	m_printFn;
};

class ZuVStream {
public:
  template <typename S>
  ZuVStream(S &s) noexcept :
    m_ptr{&s},
    m_strFn{[](void *s, ZuCSpan v) {
      *static_cast<S *>(s) << v;
    }},
    m_bufFn{[](void *s, const ZuVStreamBuf &v) {
      *static_cast<S *>(s) << v;
    }} { }

  ZuVStream() = delete;

  ZuVStream(const ZuVStream &) = default;
  ZuVStream &operator =(const ZuVStream &) = default;
  ZuVStream(ZuVStream &&) = default;
  ZuVStream &operator =(ZuVStream &&) = default;
  ~ZuVStream() = default;

private:
  template <typename U, typename R = void>
  using MatchChar = ZuIfT<ZuEquiv<U, char>{}, R>;

  template <typename U, typename R = void>
  using MatchWChar = ZuIfT<ZuIsSame<ZuDecay<U>, wchar_t>{}, R>;

  template <typename U, typename R = void>
  using MatchReal = ZuIfT<
    ZuTraits<U>::IsPrimitive &&
    ZuTraits<U>::IsReal &&
    !ZuEquiv<U, char>{}, R>;

  template <typename U, typename R = void>
  using MatchString = ZuIfT<
    ZuTraits<U>::IsString &&
    !ZuTraits<U>::IsWString &&
    !ZuIs_<U, ZuCSpan>{}, R>;

  template <typename U, typename R = void>
  using MatchWString = ZuIfT<
    ZuTraits<U>::IsString &&
    ZuTraits<U>::IsWString, R>;

  template <typename U, typename R = void>
  using MatchPDelegate = ZuIfT<ZuPrint<U>::Delegate, R>;

  template <typename U, typename R = void>
  using MatchPBuffer = ZuIfT<ZuPrint<U>::Buffer, R>;

public:

  template <typename C,
    typename = ZuIfT<(ZuEquiv<C, char>{}) || (ZuIsSame<ZuDecay<C>, wchar_t>{})>>
  ZuVStream & operator <<(C c) {
    if constexpr (ZuEquiv<C, char>{}) {
      (*m_strFn)(m_ptr, {&c, 1});
      return *this;
    } else {
      char buf[8];
      auto r = ZuUTF<char, wchar_t>::cvt_overflow(
	{&buf[0], sizeof(buf)}, {&c, 1});
      if (!r.template p<1>())
	(*m_strFn)(m_ptr, {&buf[0], r.template p<0>()});
      return *this;
    }
  }

  ZuVStream &operator <<(ZuCSpan s) {
    (*m_strFn)(m_ptr, s);
    return *this;
  }

  template <typename S,
    typename = ZuIfT<
      (ZuTraits<S &&>::IsString && !ZuTraits<S &&>::IsWString && !ZuIs_<S &&, ZuCSpan>{}) ||
      (ZuTraits<S>::IsString && ZuTraits<S>::IsWString)>>
  ZuVStream & operator <<(S &&s_) {
    if constexpr (ZuTraits<S && >::IsString && !ZuTraits<S && >::IsWString && !ZuIs_<S && ,
      ZuCSpan>{}) {
      (*m_strFn)(m_ptr, ZuCSpan(s_));
      return *this;
    } else {
      ZuWSpan s(s_);
      if (!s.length()) return *this;
      auto n = ZuUTF<char, wchar_t>::len(s);
      if (!n) return *this;
      auto buf = static_cast<char *>(ZuAlloca(n, 1));
      if (!buf) return *this;
      auto r = ZuUTF<char, wchar_t>::cvt_overflow({buf, n}, s);
      if (!r.template p<1>())
	(*m_strFn)(m_ptr, {buf, r.template p<0>()});
      return *this;
    }
  }

  template <typename R,
    typename = ZuIfT<
      (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuEquiv<R, char>{}) ||
      (ZuPrint<R>::Delegate) ||
      (ZuPrint<R>::Buffer)>>
  ZuVStream & operator <<(const R &r) {
    if constexpr (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuEquiv<R, char>{}) {
      (*m_bufFn)(m_ptr, ZuVStreamBuf{ZuBoxed(r)});
      return *this;
    } else if constexpr (ZuPrint<R>::Delegate) {
      ZuPrint<R>::print(*this, r);
      return *this;
    } else {
      (*m_bufFn)(m_ptr, ZuVStreamBuf{r});
      return *this;
    }
  }

private:
  typedef void (*StrFn)(void *, ZuCSpan);
  typedef void (*BufFn)(void *, const ZuVStreamBuf &);

  void		*m_ptr;
  StrFn		m_strFn;
  BufFn		m_bufFn;
};

#endif /* ZuVStream_HH */
