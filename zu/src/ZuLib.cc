//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z U-Layer Library

#include <zlib/ZuLib.hh>

ZuExtern const char ZuLib[] = "@(#) Z U-Layer Library v" Z_VERNAME;

#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

#include <stdarg.h>
#include <stdio.h>

#include <zlib/ZuStringFn.hh>

#ifndef va_copy
#ifdef __va_copy
#define va_copy(DST, SRC) __va_copy((DST), (SRC))
#else
#ifdef HAVE_VA_LIST_IS_ARRAY
#define va_copy(DST, SRC) (*(DST) = *(SRC))
#else
#define va_copy(DST, SRC) ((DST) = (SRC))
#endif
#endif
#endif

int Zu::vsnprintf(char *s, unsigned n, const char *format, va_list ap_)
{
  va_list ap;
  va_copy(ap, ap_);
  n = ::vsnprintf(s, n, format, ap);
  va_end(ap);
  return n;
}

int Zu::vsnprintf(wchar_t *w, unsigned n, const wchar_t *format, va_list ap_)
{
  va_list ap;
  va_copy(ap, ap_);
#ifndef _WIN32
  n = vswprintf(w, n, format, ap);
#else
  n = _vsnwprintf(w, n, format, ap);
#endif
  va_end(ap);
  return n;
}
