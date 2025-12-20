//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <zlib/ZuInt.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuDerive.hh>

#include <string>
#include <sstream>
#include <iostream>

template <typename V1, typename V2>
void fail2(unsigned line, const char *s, const V1 &v1, const V2 &v2)
{
  std::cerr << "FAIL: "
    << line << ':' << s << " v1=" << v1 << " v2=" << v2 << '\n' << std::flush;
  abort();
}

#define CHECK2(x, v1, v2) ((x) ? (void()) : fail2(__LINE__, #x, v1, v2))

unsigned itoa(char *buf, int i) {
  bool negative = 0;
  unsigned j;
  if (i < 0) { *buf++ = '-'; j = -i; negative = 1; }
  else j = i;
  unsigned n = 11;
  do { buf[--n] = (j % 10) + '0'; j /= 10; } while (j);
  unsigned o = 11 - n;
  if (n) memmove(buf, buf + n, o);
  buf[o] = 0;
  return o + negative;
}

int main()
{
  struct timespec start, end;

  {
    char buf[30];
    for (int i = INT_MIN; i < INT_MAX - 420; i += 420) {
      itoa(buf, i);
      CHECK2(i == atoi(buf), i, buf);
      buf[0] = 0;
    }
    clock_gettime(CLOCK_REALTIME, &start);
    for (int i = INT_MIN; i < INT_MAX - 42; i += 42) {
      itoa(buf, i);
      buf[0] = 0;
    }
    clock_gettime(CLOCK_REALTIME, &end);
    if (end.tv_nsec < start.tv_nsec) {
      end.tv_nsec = (end.tv_nsec + 1000000000L) - start.tv_nsec;
      end.tv_sec = (end.tv_sec - 1) - start.tv_sec;
    } else {
      end.tv_nsec -= start.tv_nsec;
      end.tv_sec -= start.tv_sec;
    }
    std::cout << "itoa: " <<
      ZuBoxed(end.tv_sec) << '.'
	<< ZuBoxed(end.tv_nsec).fmt<ZuFmt::Frac<9, 9>>() << '\n';
  }

  {
    ZuCArray<30> buf;
    for (int i = INT_MIN; i < INT_MAX - 420; i += 420) {
      buf << ZuBoxed(i);
      CHECK2(i == ZuBox<int>(buf), i, buf.data());
      buf.null();
    }
    clock_gettime(CLOCK_REALTIME, &start);
    for (int i = INT_MIN; i < INT_MAX - 42; i += 42) {
      buf << ZuBoxed(i);
      buf.null();
    }
    clock_gettime(CLOCK_REALTIME, &end);
    if (end.tv_nsec < start.tv_nsec) {
      end.tv_nsec = (end.tv_nsec + 1000000000L) - start.tv_nsec;
      end.tv_sec = (end.tv_sec - 1) - start.tv_sec;
    } else {
      end.tv_nsec -= start.tv_nsec;
      end.tv_sec -= start.tv_sec;
    }
    std::cout << "ZuBox<int>::fmt(): " <<
      ZuBoxed(end.tv_sec) << '.'
	<< ZuBoxed(end.tv_nsec).fmt<ZuFmt::Frac<9, 9>>() << '\n';
  }

  {
    ZuVFmt fmt;
    ZuCArray<30> buf;
    for (int i = INT_MIN; i < INT_MAX - 420; i += 420) {
      buf << ZuBoxed(i).vfmt(fmt);
      CHECK2(i == ZuBox<int>(buf), i, buf.data());
      buf.null();
    }
    clock_gettime(CLOCK_REALTIME, &start);
    for (int i = INT_MIN; i < INT_MAX - 42; i += 42) {
      buf << ZuBoxed(i).vfmt(fmt);
      buf.null();
    }
    clock_gettime(CLOCK_REALTIME, &end);
    if (end.tv_nsec < start.tv_nsec) {
      end.tv_nsec = (end.tv_nsec + 1000000000L) - start.tv_nsec;
      end.tv_sec = (end.tv_sec - 1) - start.tv_sec;
    } else {
      end.tv_nsec -= start.tv_nsec;
      end.tv_sec -= start.tv_sec;
    }
    std::cout << "ZuBox<int>::vfmt(): " <<
      ZuBoxed(end.tv_sec) << '.'
	<< ZuBoxed(end.tv_nsec).fmt<ZuFmt::Frac<9, 9>>() << '\n';
  }

  {
    char buf[30];
    for (double d = INT_MIN; d < INT_MAX - 4201; d += 4200.000420) {
      sprintf(buf, "%f", d);
      double e = d - strtod(buf, 0);
      CHECK2(e < .00001 && e > -.00001, e, buf);
      buf[0] = 0;
    }
    clock_gettime(CLOCK_REALTIME, &start);
    for (double d = INT_MIN; d < INT_MAX - 421; d += 420.000420) {
      sprintf(buf, "%f", d);
      buf[0] = 0;
    }
    clock_gettime(CLOCK_REALTIME, &end);
    if (end.tv_nsec < start.tv_nsec) {
      end.tv_nsec = (end.tv_nsec + 1000000000L) - start.tv_nsec;
      end.tv_sec = (end.tv_sec - 1) - start.tv_sec;
    } else {
      end.tv_nsec -= start.tv_nsec;
      end.tv_sec -= start.tv_sec;
    }
    std::cout << "sprintf(\"%f\"): " <<
      ZuBoxed(end.tv_sec) << '.'
	<< ZuBoxed(end.tv_nsec).fmt<ZuFmt::Frac<9, 9>>() << '\n';
  }

  {
    ZuCArray<30> buf;
    for (double d = INT_MIN; d < INT_MAX - 4201; d += 4200.000420) {
      buf << ZuBoxed(d);
      ZuBox<double> e(buf);
      // printf("d: %f buf: %.*s e: %f diff: %g, epsilon: %g\n", d, buf.length(), buf.data(), (double)e, (double)e - d, (double)ZuFP<double>::epsilon_(d));
      CHECK2(ZuBoxed(d).feq(e), e, buf.data());
      buf.null();
    }
    clock_gettime(CLOCK_REALTIME, &start);
    for (double d = INT_MIN; d < INT_MAX - 421; d += 420.000420) {
      buf << ZuBoxed(d);
      buf.null();
    }
    clock_gettime(CLOCK_REALTIME, &end);
    if (end.tv_nsec < start.tv_nsec) {
      end.tv_nsec = (end.tv_nsec + 1000000000L) - start.tv_nsec;
      end.tv_sec = (end.tv_sec - 1) - start.tv_sec;
    } else {
      end.tv_nsec -= start.tv_nsec;
      end.tv_sec -= start.tv_sec;
    }
    std::cout << "ZuBox<double>::fmt(): " <<
      ZuBoxed(end.tv_sec) << '.'
	<< ZuBoxed(end.tv_nsec).fmt<ZuFmt::Frac<9, 9>>() << '\n';
  }

  {
    ZuVFmt fmt;
    ZuCArray<30> buf;
    for (double d = INT_MIN; d < INT_MAX - 4201; d += 4200.000420) {
      buf << ZuBoxed(d).vfmt(fmt);
      CHECK2(ZuBoxed(d).feq(ZuBox<double>{buf}), d, buf.data());
      buf.null();
    }
    clock_gettime(CLOCK_REALTIME, &start);
    for (double d = INT_MIN; d < INT_MAX - 421; d += 420.000420) {
      buf << ZuBoxed(d).vfmt(fmt);
      buf.null();
    }
    clock_gettime(CLOCK_REALTIME, &end);
    if (end.tv_nsec < start.tv_nsec) {
      end.tv_nsec = (end.tv_nsec + 1000000000L) - start.tv_nsec;
      end.tv_sec = (end.tv_sec - 1) - start.tv_sec;
    } else {
      end.tv_nsec -= start.tv_nsec;
      end.tv_sec -= start.tv_sec;
    }
    std::cout << "ZuBox<double>::vfmt(): " <<
      ZuBoxed(end.tv_sec) << '.'
	<< ZuBoxed(end.tv_nsec).fmt<ZuFmt::Frac<9, 9>>() << '\n';
  }
}
