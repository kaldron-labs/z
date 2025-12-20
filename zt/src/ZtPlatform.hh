//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// platform-specific

#ifndef ZtPlatform_HH
#define ZtPlatform_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <stdlib.h>

namespace Zt {

// environment manipulation
#ifndef _WIN32
inline int putenv(const char *s) { return ::putenv(const_cast<char *>(s)); }
#else
inline int putenv(const char *s) { return ::_putenv(const_cast<char *>(s)); }
#endif

} // namespace Zt

#endif /* ZtPlatform_HH */
