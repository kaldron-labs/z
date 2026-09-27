//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// platform-specific

#ifndef ZtPlatform_HH
#define ZtPlatform_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <stdlib.h>

#ifdef _WIN32
#include <zlib/ZtString.hh>
#include <zlib/ZtScratch.hh>
#endif

namespace Zt {

// environment manipulation
#ifndef _WIN32
inline void setenv(const char *key, const char *value) {
  if (!value || !*value)
    ::unsetenv(key);
  else
    ::setenv(key, value, 1);
}
inline void unsetenv(const char *key) {
  ::unsetenv(key);
}
inline auto getpath(const char *key) {
  return ::getenv(key);
}
#else
inline void setenv(const char *key, const char *value) {
  _putenv_s(key, value ? value : "");
}
ZuDerive(EnvWScratch, (ZtWString<ZtStringSharded<true,
  ZtStringHeapID<"Zt.EnvWString">>>));
inline void setenv(ZuCSpan key_, const wchar_t *value) {
  auto key = ZtScratch(EnvWScratch, key_.length());
  key = key_;
  _wputenv_s(key, value ? value : L"");
}
inline void unsetenv(const char *key) {
  _putenv_s(key, "");
}
inline auto getpath(const char *key) {
  auto key = ZtScratch(EnvWScratch, key_.length());
  key = key_;
  return _wgetenv(key);
}
#endif

} // namespace Zt

#endif /* ZtPlatform_HH */
