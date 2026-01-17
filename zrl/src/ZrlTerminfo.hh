//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface

#ifndef ZrlTerminfo_HH
#define ZrlTerminfo_HH

#ifndef ZrlLib_HH
#include <zlib/ZrlLib.hh>
#endif

#ifndef _WIN32

// including ncurses.h corrupts the preprocessor and compiler namespace,
// so here we just import what's needed and assume that none of it is
// a macro (that isn't the case as of 10/11/2020, and likely won't ever be)

extern "C" {
  struct term;
  typedef struct term TERMINAL;

  extern TERMINAL *cur_term;

  extern int setupterm(const char *, int, int *);
  extern int del_curterm(TERMINAL *);

  extern char *tigetstr(const char *);
  extern int tigetflag(const char *);
  extern int tigetnum(const char *);
  extern char *tiparm(const char *, ...);

  extern int tputs(const char *, int, int (*)(int));
};

#endif /* !_WIN32 */

#endif /* ZrlTerminfo_HH */
