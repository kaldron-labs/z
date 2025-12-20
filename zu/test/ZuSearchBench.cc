//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>

#include <iostream>

#include <zlib/ZuSort.hh>
#include <zlib/ZuSearch.hh>
#include <zlib/ZuBox.hh>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

void search_noexcept(uint64_t *array, int n, uint64_t target, unsigned l)
{
  unsigned r = 0;
  for (unsigned i = 0; i < l; i++) {
    // std::cout << "target=" << target << '\n';
    // std::cout << "n=" << n << '\n';
    // std::cout << "first=" << array[0] << '\n';
    // std::cout << "last=" << array[n - 1] << '\n';
    r = ZuInterSearch<true>(n, [array, target](unsigned j) {
      return double(target) - double(array[j]);
      // std::cout << "j=" << j << " array[j]=" << array[j] << " target=" << target << " d=" << ZuBoxed(d) << '\n';
    });
  }
  r = ZuSearchPos(r);
  std::cout << "result=" << r << " array[r]=" << array[r] << " array[r - 1]=" << array[r - 1] << '\n';
}

struct OutOfBounds { };

void search_except(uint64_t *array, int n, uint64_t target, unsigned l)
{
  unsigned r;
  for (unsigned i = 0; i < l; i++) {
    // std::cout << "target=" << target << '\n';
    // std::cout << "n=" << n << '\n';
    // std::cout << "first=" << array[0] << '\n';
    // std::cout << "last=" << array[n - 1] << '\n';
    r = ZuInterSearch<true>(n, [array, target](unsigned j) {
      if (j >= 1000) throw OutOfBounds{};
      return double(target) - double(array[j]);
      // std::cout << "j=" << j << " array[j]=" << array[j] << " target=" << target << " d=" << ZuBoxed(d) << '\n';
    });
  }
  r = ZuSearchPos(r);
  std::cout << "result=" << r << " array[r]=" << array[r] << " array[r - 1]=" << array[r - 1] << '\n';
}

void usage()
{
  std::cerr << "Usage: ZuSearchBench [except|noexcept] N\n";
  ::exit(1);
}

int main(int argc, char **argv)
{
  auto array = static_cast<uint64_t *>(::malloc(1000 * sizeof(uint64_t)));
  for (unsigned i = 0; i < 1000; i++) {
    switch (i & 3) {
      case 0:
	array[i] = (i<<10);
	break;
      case 1:
	array[i] = (i<<10) + (i<<7) + (i<<2);
	break;
      case 2:
	array[i] = (i<<10) + (i<<7) + (i<<2) + i;
	break;
      case 3:
	array[i] = (i<<10) + (i<<5);
	break;
    }
  }
  if (argc != 3) usage();
  if (!strcmp(argv[1], "except"))
    try {
      search_except(array, 1000, (500<<10), atoi(argv[2]));
    } catch (...) {
      std::cerr << "exception!\n" << std::flush;
    }
  else if (!strcmp(argv[1], "noexcept"))
    search_noexcept(array, 1000, (500<<10), atoi(argv[2]));
  else
    usage();
}
