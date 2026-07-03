//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// "good ole' quick sort", modern C++ version
// - optimized three-way comparison
// - optimized array memory operations
// - median-of-three pivot
// - fallback to insertion sort for small partitions of N items or less
// - recurse smaller partition, iterate larger partition
// - minimized stack usage during recursion

// template <unsigned N = 8, typename T>
// ZuSort(T *data, unsigned n)
//
// template <unsigned N = 8, typename T, typename Cmp>
// ZuSort(T *data, unsigned n, Cmp cmp)
//
// data - pointer to T * data to be sorted
// n - number of contiguous elements of type T
//
// Cmp - three-way comparison - defaults to ZuCmp<T>::cmp
// N - insertion sort is used below this partition size threshold

#ifndef ZuSort_HH
#define ZuSort_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <utility>

#include <zlib/ZuCmp.hh>
#include <zlib/ZuArrayFn.hh>

template <typename T, typename Cmp, unsigned N>
struct ZuSort_Fn {
  using Fn = ZuArrayFn<T>;

  static void isort_(T *base, unsigned n, Cmp &cmp) {
    using std::swap;
    T *end = base + n; // past end
    T *minimum = base, *ptr = base;
    // find minimum
    while (++ptr < end)
      if (cmp(*minimum, *ptr) > 0)
	minimum = ptr;
    // swap minimum into base
    if (minimum != base) {
      swap(*minimum, *base);
      minimum = base;
    }
    // standard insertion sort
    while ((ptr = ++minimum) < end) {
      while (cmp(*--ptr, *minimum) > 0);
      if (++ptr != minimum) {
	T tmp = ZuMv(*minimum);
	Fn::moveElems(ptr + 1, ptr, minimum - ptr);
	*ptr = ZuMv(tmp);
      }
    }
  }

  static void qsort_(T *base, unsigned n, Cmp &cmp) {
    using std::swap;
  loop:
    T *middle = base + (n>>1);
    {
      T *end = base + (n - 1); // at end (not past)
      // find pivot - median of base, middle, end
      T *ptr;
      if (cmp(*base, *middle) >= 0)
	ptr = base;
      else
	ptr = middle, middle = base;
      if (cmp(*ptr, *end) > 0)
	ptr = cmp(*middle, *end) >= 0 ? middle : end;
      // swap pivot into base (gets swapped back later)
      if (ptr != base) {
	swap(*ptr, *base);
	ptr = base;
      }
      // standard quicksort & check for flat partition
      middle = base;
      unsigned j = 1;
      int k;
      while (++ptr <= end) {
	if (!(k = cmp(*ptr, *base)))
	  j++;
	if (k < 0) {
	  if (++middle != ptr) {
	    swap(*middle, *ptr);
	  }
	}
      }
      if (j == n) return; // exit if flat
    }
    if (base != middle) swap(*base, *middle); // swap back pivot
    unsigned i = middle - base;
    if (i < (n>>1)) {
      // recurse smaller partition
      if (i > N)
	qsort_(base, i, cmp);
      else if (i > 1)
	isort_(base, i, cmp);
      ++middle;
      i = n - i - 1;
      // iterate larger partition (unless insertion sort)
      if (i > N) {
	base = middle;
	n = i;
	goto loop;
      }
    } else {
      // recurse smaller partition
      ++middle;
      i = n - i - 1;
      if (i > N)
	qsort_(middle, i, cmp);
      else if (i > 1)
	isort_(middle, i, cmp);
      middle = base;
      i = n - i - 1;
      // iterate larger partition (unless insertion sort)
      if (i > N) {
	n = i;
	goto loop;
      }
    }
    if (i > 1)
      isort_(middle, i, cmp);
  }
};

template <unsigned N = 8, typename T>
inline void ZuSort(T *data, unsigned n) {
  auto cmp = [](const T &l, const T &r) {
    return ZuCompare(l, r);
  };
  using Cmp = decltype(cmp);
  using Fn = ZuSort_Fn<T, Cmp, N>;
  if (n > N)
    Fn::qsort_(data, n, cmp);
  else if (n > 1)
    Fn::isort_(data, n, cmp);
}

template <unsigned N = 8, typename T, typename Cmp>
inline void ZuSort(T *data, unsigned n, Cmp cmp) {
  using Fn = ZuSort_Fn<T, Cmp, N>;
  if (n > N)
    Fn::qsort_(data, n, cmp);
  else if (n > 1)
    Fn::isort_(data, n, cmp);
}

#endif /* ZuSort_HH */
