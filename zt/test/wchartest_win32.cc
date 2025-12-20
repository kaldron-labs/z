//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef WINVER
#define WINVER 0x0501
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#ifndef _WIN32_DCOM
#define _WIN32_DCOM
#endif

#ifndef _WIN32_WINDOWS
#define _WIN32_WINDOWS 0x0410
#endif

#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <wchar.h>
#include <string.h>
#include <locale.h>
#include <stdio.h>

const char *nihongo = "異本後";

int main()
{
  int l = strlen(nihongo);

  printf("strlen(nihongo) = %d\n", l);

  int n;

  n = MultiByteToWideChar(CP_UTF8, 0, nihongo, l, 0, 0);
  printf("MultiByteToWideChar(..., l, 0, 0) = %d\n", n);
  n = MultiByteToWideChar(CP_UTF8, 0, nihongo, l + 1, 0, 0);
  printf("MultiByteToWideChar(..., l + 1, 0, 0) = %d\n", n);
  n = MultiByteToWideChar(CP_UTF8, 0, nihongo, -1, 0, 0);
  printf("MultiByteToWideChar(..., -1, 0, 0) = %d\n", n);

  wchar_t wbuf[256];

  n = MultiByteToWideChar(CP_UTF8, 0, nihongo, l, wbuf, 256);
  printf("MultiByteToWideChar(..., l, wbuf, 256) = %d\n", n);
  n = MultiByteToWideChar(CP_UTF8, 0, nihongo, l + 1, wbuf, 256);
  printf("MultiByteToWideChar(..., l + 1, wbuf, 256) = %d\n", n);
  n = MultiByteToWideChar(CP_UTF8, 0, nihongo, -1, wbuf, 256);
  printf("MultiByteToWideChar(..., -1, wbuf, 256) = %d\n", n);

  n = wcslen(wbuf);
  printf("wcslen(wbuf) = %d\n", n);

  l = WideCharToMultiByte(CP_UTF8, 0, wbuf, n, 0, 0, 0, 0);
  printf("WideCharToMultiByte(..., n, 0, 0, 0, 0) = %d\n", l);
  l = WideCharToMultiByte(CP_UTF8, 0, wbuf, n + 1, 0, 0, 0, 0);
  printf("WideCharToMultiByte(..., n + 1, 0, 0, 0, 0) = %d\n", l);
  l = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, 0, 0, 0, 0);
  printf("WideCharToMultiByte(..., -1, 0, 0, 0, 0) = %d\n", l);

  char buf[256];

  l = WideCharToMultiByte(CP_UTF8, 0, wbuf, n, buf, 256, 0, 0);
  printf("WideCharToMultiByte(..., n, buf, 256, 0, 0) = %d\n", l);
  l = WideCharToMultiByte(CP_UTF8, 0, wbuf, n + 1, buf, 256, 0, 0);
  printf("WideCharToMultiByte(..., n + 1, buf, 256, 0, 0) = %d\n", l);
  l = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, 256, 0, 0);
  printf("WideCharToMultiByte(..., -1, buf, 256, 0, 0) = %d\n", l);

  l = strlen(buf);
  printf("strlen(buf) = %d\n", l);

  _locale_t loc = _create_locale(LC_ALL, ".932");

  WideCharToMultiByte(932, 0, wbuf, -1, buf, 256, 0, 0);

  l = strlen(buf);
  printf("strlen(buf) SJIS = %d\n", l);

  n = _mbstowcs_l(0, buf, 0, loc);
  printf("_mbstowcs_l(0, buf, 0, loc) = %d\n", n);
  n = _mbstowcs_l(wbuf, buf, 256, loc);
  printf("_mbstowcs_l(wbuf, buf, 256, loc) = %d\n", n);

  l = wcslen(wbuf);
  printf("wcslen(wbuf) = %d\n", l);

  l = _wcstombs_l(0, wbuf, 0, loc);
  printf("_wcstombs_l(0, wbuf, 0, loc) = %d\n", l);
  l = _wcstombs_l(buf, wbuf, 256, loc);
  printf("_wcstombs_l(buf, wbuf, 256, loc) = %d\n", l);

  l = strlen(buf);
  printf("strlen(buf) SJIS = %d\n", l);
}
