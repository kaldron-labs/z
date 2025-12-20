//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// tzOffset() calls tzset() if the tz argument is non-null
// - it sets and reverts the TZ environment variable as necessary
// - it acquires and releases a global lock to ensure serialization
//   (tzset() is not thread-safe in any case)
// - it should not be called with high frequency by high-performance
//   applications with a non-null tz since
// - it is potentially time-consuming
// - it is single-threaded
// - it accesses and temporarily modifies global environment variables
// - tzset() probably accesses system configuration files and/or external
//   timezone databases

#ifndef ZtTimeZone_HH
#define ZtTimeZone_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <zlib/ZuDateTime.hh>

namespace Zt {

// timezone manipulation
#ifndef _WIN32
inline void tzset(void) { ::tzset(); }
#else
inline void tzset(void) { ::_tzset(); }
#endif

ZtExtern int tzOffset(ZuDateTime value, const char *tz = nullptr);

} // Zt

#endif /* ZtTimeZone_HH */
