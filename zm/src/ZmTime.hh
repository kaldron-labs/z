//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// high precision timer

#ifndef ZmTime_HH
#define ZmTime_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuTime.hh>

namespace Zm {

#ifndef _WIN32
inline ZuTime now() {
  timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return ZuTime{t};
}
#else
ZmExtern ZuTime now();
#endif

template <typename T>
inline typename ZuTime::MatchInt<T, ZuTime>::T
now(T d) { ZuTime t = now(); return t += d; }
inline ZuTime now(const ZuTime &d) { ZuTime t = now(); return t += d; }

ZmExtern void sleep(ZuTime);

#ifdef _WIN32
ZmExtern uint64_t cpuFreq();
#endif

} // Zm

#endif /* ZmTime_HH */
