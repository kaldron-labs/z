//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// C++ demangling (binutils BFD version)

// s << ZmDemangle<T>{}
// s << ZmDemangle_{typeid(T).name()}

#ifndef ZmDemangle_HH
#define ZmDemangle_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuDemangle.hh>

// Note: __cxxabiv1::__cxa_demangle() doesn't correctly demangle
// "Z1XvEUlTyOT_E_" to
// "X()::{lambda<typename $T0>($T0&&)#1}"
//
// in contrast, binutils/bfd cplus_demangle() does this correctly

// from binutils demangle.h
#define DMGL_PARAMS	 (1<<0)	/* Include function args */
#define DMGL_ANSI	 (1<<1)	/* Include const, volatile, etc. */
#define DMGL_VERBOSE	 (1<<3)	/* Include implementation details */
#define DMGL_TYPES	 (1<<4)	/* Also try to demangle type encodings */
extern "C" { extern char *cplus_demangle(const char *mangled, int options); }

class ZmDemangle_ : public ZuPrintable {
  ZmDemangle_(const ZmDemangle_ &) = delete;
  ZmDemangle_ &operator =(const ZmDemangle_ &) = delete;
  ZmDemangle_(ZmDemangle_ &&) = delete;
  ZmDemangle_ &operator =(ZmDemangle_ &&) = delete;

public:
  ZmDemangle_() = default;

  ZmDemangle_(const char *mangled) {
    m_output = cplus_demangle(mangled,
	DMGL_TYPES | DMGL_PARAMS | DMGL_ANSI | DMGL_VERBOSE);
    if (!m_output)
      m_output = mangled;
    else {
      ZuSpan<char> output{const_cast<char *>(m_output.data()), m_output.length()};
      ZuDemangle_::transform(output);
      m_output = output;
      m_free = true;
    }
  }

  ~ZmDemangle_() noexcept {
    if (m_free && m_output) ::free(const_cast<char *>(m_output.data()));
  }

  ZmDemangle_ &operator =(const char *symbol) {
    this->~ZmDemangle_();
    new (this) ZmDemangle_{symbol};
    return *this;
  }

  operator ZuCSpan() const { return m_output; }

  template <typename S> void print(S &s) const {
    if (m_output) s << m_output;
  }

private:
  ZuCSpan	m_output;
  bool		m_free = false;
};

template <typename T>
struct ZmDemangle {
  template <typename S>
  static void print(S &s) { s << ZmDemangle_{typeid(T).name()}; }
};
template <typename T>
struct ZmDemangle<const T> {
  template <typename S>
  static void print(S &s) { s << "const "; ZmDemangle<T>::print(s); }
};
template <typename T>
struct ZmDemangle<volatile T> {
  template <typename S>
  static void print(S &s) { s << "volatile "; ZmDemangle<T>::print(s); }
};
template <typename T>
struct ZmDemangle<const volatile T> {
  template <typename S>
  static void print(S &s) { s << "const volatile "; ZmDemangle<T>::print(s); }
};
template <typename T>
struct ZmDemangle<T &> {
  template <typename S>
  static void print(S &s) { ZmDemangle<T>::print(s); s << " &"; }
};
template <typename T>
struct ZmDemangle<const T &> {
  template <typename S>
  static void print(S &s) {
    s << "const ";
    ZmDemangle<T>::print(s);
    s << " &";
  }
};
template <typename T>
struct ZmDemangle<T &&> {
  template <typename S>
  static void print(S &s) { ZmDemangle<T>::print(s); s << " &&"; }
};

template <typename S, typename T>
inline S &operator <<(S &s, const ZmDemangle<T> &d) { d.print(s); return s; }

#endif /* ZmDemangle_HH */
