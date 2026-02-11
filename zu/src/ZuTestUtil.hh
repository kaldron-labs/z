//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TAP-emitting test framework utility
// - see ZuTest.hh for the core library

#ifndef ZuTestUtil_HH
#define ZuTestUtil_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdlib.h>
#include <streambuf>
#include <string>

#include <zlib/ZuTest.hh>

namespace ZuTestUtil {

inline bool verbose = true;

template <typename ...Args>
inline void log_(Args &&...args) {
  if constexpr (sizeof...(args))
    (std::cerr << ...<< ZuFwd<Args>(args)) << '\n';
}
template <typename ...Args>
inline void log(Args &&...args) {
  if (verbose) log_(ZuFwd<Args>(args)...);
}

inline void usage(ZuCSpan name)
{
  std::cerr <<
    "Usage: " << name << " [-q]\n\n"
    "Options:\n"
    "  -q\tquiet output (default when test-harnessed)\n";
  ::exit(1);
}

bool parse(int argc, char **argv)
{
  verbose = !::getenv("HARNESS_ACTIVE");
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-q")) {
      verbose = false;
      return true;
    }
    ZuCSpan argv0 = argv[0];
    for (int i = argv0.length(); --i >= 0; ) {
      auto c = argv0[i]; 
      if (c == '/'
#ifdef _WIN32
	|| c == '\\'
#endif
	) {
	argv0.offset(i + 1);
	break;
      }
    }
    usage(argv0);
  }
  return false;
}

}

#define ZuCHECK(x, ...) ZuCheck(x, log_(__VA_ARGS__))

#endif /* ZuTestUtil_HH */
