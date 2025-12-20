//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// stream with overflow detection to buffers
// - supports both ZuSpan<char>, ZuSpan<wchar_t>

#ifndef ZuStream_HH
#define ZuStream_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuTraits.hh>
#include <zlib/ZuStringFn.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuUTF.hh>
#include <zlib/ZuEquiv.hh>
#include <zlib/ZuDerive.hh>

template <typename T_> struct ZuStream_Char2;
template <> struct ZuStream_Char2<char> { using T = wchar_t; };
template <> struct ZuStream_Char2<wchar_t> { using T = char; };

template <typename Char_>
class ZuStream_ : public ZuSpan<Char_> {
  ZuAssert((ZuIsSame<Char_, char>{} || ZuIsSame<Char_, wchar_t>{}));

public:
  ZuDerive_(ZuStream_, ZuSpan<Char_>)
  using Base::data;
  using Base::length;
  using Base::offset;
  using Base::trunc;

  // overflow indication
  void reset() { m_overflow = false; }
  bool overflow() const { return m_overflow; }

  using Char = Char_;
  using Char2 = typename ZuStream_Char2<Char>::T;

  // from any string with same char (including string literals)
  template <typename U, typename V = Char> struct IsString :
    public ZuBool<(ZuTraits<U>::IsArray || ZuTraits<U>::IsString) &&
      bool(ZuEquiv<typename ZuTraits<U>::Elem, V>{})> { };
  template <typename U, typename R = void>
  using MatchString = ZuIfT<IsString<U>{}, R>;

  // from individual char
  template <typename U, typename V = Char> struct IsChar :
    public ZuEquiv<U, V> { };
  template <typename U, typename R = void>
  using MatchChar = ZuIfT<IsChar<U>{}, R>;

  // from char2 string (requires conversion)
  template <typename U, typename V = Char2> struct IsChar2String :
    public ZuBool<(ZuTraits<U>::IsArray || ZuTraits<U>::IsString) &&
      bool(ZuEquiv<typename ZuTraits<U>::Elem, V>{})> { };
  template <typename U, typename R = void>
  using MatchChar2String = ZuIfT<IsChar2String<U>{}, R>;

  // from individual char2 (requires conversion, char->wchar_t only)
  template <typename U, typename V = Char2> struct IsChar2 :
    public ZuBool<bool(ZuEquiv<U, V>{}) && !ZuEquiv<U, wchar_t>{}> { };
  template <typename U, typename R = void>
  using MatchChar2 = ZuIfT<IsChar2<U>{}, R>;

  // from printable type (if this is a char array)
  template <typename U, typename V = Char> struct IsPDelegate : public ZuBool<
    !IsString<U>{} &&
    !IsChar2String<U>{} &&
    bool(ZuEquiv<char, V>{}) &&
    ZuPrint<U>::Delegate> { };
  template <typename U, typename R = void>
  using MatchPDelegate = ZuIfT<IsPDelegate<U>{}, R>;
  template <typename U, typename V = Char> struct IsPBuffer :
    public ZuBool<bool(ZuEquiv<char, V>{}) && ZuPrint<U>::Buffer> { };
  template <typename U, typename R = void>
  using MatchPBuffer = ZuIfT<IsPBuffer<U>{}, R>;

  // from real primitive types other than chars (if this is a char string)
  template <typename U, typename V = Char> struct IsReal :
    public ZuBool<bool(ZuEquiv<char, V>{}) && !bool(ZuEquiv<U, V>{}) &&
      ZuTraits<U>::IsReal && ZuTraits<U>::IsPrimitive &&
      !ZuTraits<U>::IsArray> { };
  template <typename U, typename R = void>
  using MatchReal = ZuIfT<IsReal<U>{}, R>;

  // from primitive pointer (not an array, string, or otherwise printable)
  template <typename U, typename V = Char> struct IsPtr :
    public ZuBool<bool(ZuEquiv<char, V>{}) &&
      ZuTraits<U>::IsPointer && ZuTraits<U>::IsPrimitive &&
      !ZuTraits<U>::IsArray && !ZuTraits<U>::IsString> { };
  template <typename U, typename R = void>
  using MatchPtr = ZuIfT<IsPtr<U>{}, R>;

  // limit member operator <<() overload resolution to supported types
  template <typename U>
  struct IsStreamable : public ZuBool<
      bool(IsString<U>{}) ||
      bool(IsChar<U>{}) ||
      bool(IsChar2String<U>{}) ||
      bool(IsChar2<U>{}) ||
      bool(IsPDelegate<U>{}) ||
      bool(IsPBuffer<U>{}) ||
      bool(IsReal<U>{}) ||
      bool(IsPtr<U>{})> { };
  template <typename U, typename R = void>
  using MatchStreamable = ZuIfT<IsStreamable<U>{}, R>;

  void append(const Char *s, uint64_t length_) {
    if (length() < length_) {
      m_overflow = true;
      length_ = length();
      if (!length_) return;
    }
    if (s && length_) memcpy(data(), s, length_ * sizeof(Char));
    offset(length_);
  }

protected:
  template <typename S> MatchString<S> append_(S &&s_) {
    ZuSpan<const Char> s(s_);
    append(s.data(), s.length());
  }

  template <typename C> MatchChar<C> append_(C c) {
    if (!length()) {
      m_overflow = true;
      return;
    }
    *(data()) = c;
    offset(1);
  }

  template <typename S>
  MatchChar2String<S> append_(S &&s) {
    if (!length()) {
      m_overflow = true;
      return;
    }
    auto r = ZuUTF<Char, Char2>::cvt_overflow(*this, s);
    m_overflow = r.template p<1>();
    offset(r.template p<0>());
  }

  template <typename C> MatchChar2<C> append_(C c) {
    if (!length()) return;
    auto r = ZuUTF<Char, Char2>::cvt_overflow(*this, {&c, 1});
    m_overflow = r.template p<1>();
    offset(r.template p<0>());
  }

  template <typename P>
  MatchPDelegate<P> append_(const P &p) {
    ZuPrint<P>::print(*this, p);
  }
  template <typename P>
  MatchPBuffer<P> append_(const P &p) {
    auto length_ = ZuPrint<P>::length(p);
    if (length() < length_) { m_overflow = true; return; }
    auto n = ZuPrint<P>::print(data(), length(), p);
    offset(n);
  }

  template <typename V> MatchReal<V> append_(V v) {
    append_(ZuBoxed(v));
  }
  template <typename V> MatchPtr<V> append_(V v) {
    append_(ZuBoxPtr(v).hex<false, ZuFmt::Alt<>>());
  }

public:
  template <typename U>
  MatchStreamable<U, ZuStream_ &>
  operator <<(U &&v) {
    append_(ZuFwd<U>(v));
    return *this;
  }

private:
  bool	m_overflow = false;
};

using ZuStream = ZuStream_<char>;
using ZuWStream = ZuStream_<wchar_t>;

#endif /* ZuStream_HH */
