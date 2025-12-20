//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// generic string operations

#ifndef ZuStringFn_HH
#define ZuStringFn_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <string.h>
#include <wchar.h>
#include <stdarg.h>
#include <stdio.h>

namespace Zu {
// length

ZuInline uint64_t strlen_(const char *s) { return strlen(s); }
ZuInline uint64_t strlen_(const wchar_t *w) { return wcslen(w); }

// comparison

ZuInline int strcmp_(const char *s1, const char *s2) {
  return strcmp(s1, s2);
}
ZuInline int strcmp_(const wchar_t *w1, const wchar_t *w2) {
  return wcscmp(w1, w2);
}
ZuInline int strcmp_(const char *s1, const char *s2, uint64_t n) {
  return strncmp(s1, s2, n);
}
ZuInline int strcmp_(const wchar_t *w1, const wchar_t *w2, uint64_t n) {
  return wcsncmp(w1, w2, n);
}

#ifdef _WIN32
ZuInline int stricmp_(const char *s1, const char *s2) {
  return stricmp(s1, s2);
}
ZuInline int stricmp_(const wchar_t *w1, const wchar_t *w2) {
  return wcsicmp(w1, w2);
}
ZuInline int stricmp_(const char *s1, const char *s2, uint64_t n) {
  return strnicmp(s1, s2, n);
}
ZuInline int stricmp_(const wchar_t *w1, const wchar_t *w2, uint64_t n) {
  return wcsnicmp(w1, w2, n);
}
#else
ZuInline int stricmp_(const char *s1, const char *s2) {
  return strcasecmp(s1, s2);
}
ZuInline int stricmp_(const wchar_t *w1, const wchar_t *w2) {
  return wcscasecmp(w1, w2);
}
ZuInline int stricmp_(const char *s1, const char *s2, uint64_t n) {
  return strncasecmp(s1, s2, n);
}
ZuInline int stricmp_(const wchar_t *w1, const wchar_t *w2, uint64_t n) {
  return wcsncasecmp(w1, w2, n);
}
#endif

// padding

ZuInline void strpad(char *s, uint64_t n) { memset(s, ' ', n); }
ZuInline void strpad(wchar_t *w, uint64_t n) { wmemset(w, L' ', n); }

// vsnprintf

ZuExtern int vsnprintf(
    char *s, unsigned n, const char *format, va_list ap_);
ZuExtern int vsnprintf(
    wchar_t *w, unsigned n, const wchar_t *format, va_list ap_);

// null wchar_t string

ZuInline const wchar_t *nullWString() {
#ifdef _MSC_VER
  return L"";
#else
  static wchar_t s[1] = { 0 };

  return s;
#endif
}
} // Zu

#endif /* ZuStringFn_HH */
