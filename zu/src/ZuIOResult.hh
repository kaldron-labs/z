//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// normalized I/O result codes

#ifndef ZuIOResult_HH
#define ZuIOResult_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuPrint.hh>

namespace Zu {

namespace IO {
  enum { OK = 0, EndOfFile = -1, IOError = -2, NotReady = -3 };

  inline const char *ioResult(int i) {
    static const char *names[] = { "OK", "EndOfFile", "IOError", "NotReady" };
    if (i > 0) i = 0;
    i = -i;
    if (i > (sizeof(names) / sizeof(names[0]))) return "Unknown";
    return names[i];
  }

  struct IOResult {
    int code;
    IOResult(int code_) noexcept : code{code_} { }
    template <typename S> void print(S &s) const { s << ioResult(code); }
    friend ZuPrintFn ZuPrintType(IOResult *);
  };
}

using namespace IO;

} // Zu

#endif /* ZuIOResult_HH */
