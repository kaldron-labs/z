//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// C++ demangling (binutils BFD version)

#ifndef ZmDemangle_HH
#define ZmDemangle_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuPrint.hh>

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

class ZmDemangle : public ZuPrintable {
  ZmDemangle(const ZmDemangle &) = delete;
  ZmDemangle &operator =(const ZmDemangle &) = delete;
  ZmDemangle(ZmDemangle &&) = delete;
  ZmDemangle &operator =(ZmDemangle &&) = delete;

public:
  ZmDemangle() = default;

  ZmDemangle(const char *mangled) {
    m_output = cplus_demangle(mangled,
	DMGL_TYPES | DMGL_PARAMS | DMGL_ANSI | DMGL_VERBOSE);
    if (m_output)
      m_free = true;
    else
      m_output = mangled;
  }

  ~ZmDemangle() noexcept {
    if (m_free && m_output) ::free(const_cast<char *>(m_output));
  }

  ZmDemangle &operator =(const char *symbol) {
    this->~ZmDemangle();
    new (this) ZmDemangle{symbol};
    return *this;
  }

  operator ZuCSpan() const { return m_output; }

  template <typename S> void print(S &s) const {
    if (m_output) s << m_output;
  }

private:
  const char	*m_output = nullptr;
  bool		m_free = false;
};

#endif /* ZmDemangle_HH */
