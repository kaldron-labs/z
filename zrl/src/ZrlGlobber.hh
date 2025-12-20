//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface

#ifndef ZrlGlobber_HH
#define ZrlGlobber_HH

#ifndef ZrlLib_HH
#include <zlib/ZrlLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuUTF.hh>

#include <zlib/ZiGlob.hh>

#include <zlib/ZrlApp.hh>

namespace Zrl {

// filename glob completion
class ZrlAPI Globber {
  struct QState {	// quoting state
    enum {
      WhiteSpace	= 0x000,
      Unquoted		= 0x001,
      SglQuoted		= 0x002,
      DblQuoted		= 0x003,
      Mask		= 0x003,
      BackQuote		= 0x004		// flag - implies !WhiteSpace
    };
  };
  struct QMode {	// quoting mode
    enum {
      Unset		= 0x000,
      BackQuote		= 0x001,
      SglQuote		= 0x002,
      DblQuote		= 0x003,
      Mask		= 0x003,
      Extant		= 0x004		// flag - implies single/double
    };
  };

private:
  ZuInline static constexpr bool isspace__(char c) {
    return ((c >= '\t' && c <= '\r') || c == ' ');
  }
  typedef bool (*QuoteFn)(uint32_t c);
  QuoteFn quoteFn();

public:
  void init(
      ZuBSpan data,	// line data
      unsigned cursor,			// cursor offset (in bytes)
      CompSpliceFn splice);		// line splice callback function
  void final();

  void start();

  bool subst(CompSpliceFn splice, bool next);

  bool next(CompIterFn iter);

  auto initFn() { return CompInitFn{this, ZmFnPtr<&Globber::init>{}}; }
  auto finalFn() { return CompFinalFn{this, ZmFnPtr<&Globber::final>{}}; }
  auto startFn() { return CompStartFn{this, ZmFnPtr<&Globber::start>{}}; }
  auto substFn() { return CompSubstFn{this, ZmFnPtr<&Globber::subst>{}}; }
  auto nextFn() { return CompNextFn{this, ZmFnPtr<&Globber::next>{}}; }

private:
  bool		m_appendSpace = false;	// append space to leafname?
  int		m_qmode = QMode::Unset;	// quoting mode for leafname
  unsigned	m_loff = 0;		// current leafname offset in line
  ZuUTFSpan	m_lspan;		// current leafname span in line
  ZiGlob	m_glob;			// filesystem globber
};

} // Zrl

#endif /* ZrlGlobber_HH */
