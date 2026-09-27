//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// binary and interpolation search of sorted data

#ifndef ZuSearch_HH
#define ZuSearch_HH

inline bool ZuSearchFound(uint64_t i) { return i & 1; }
inline uint64_t ZuSearchPos(uint64_t i) { return i>>1; }

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

// binary search in sorted data (e.g. following ZuSort)
// - returns insertion position if not found
template <bool Match = true, typename Cmp>
inline uint64_t ZuSearch(uint64_t n, Cmp cmp) {
  if (!n) return 0;
  uint64_t o = 0;
  uint64_t p = n>>1;
  int l = cmp(p);
  while (n > 2) {
    n -= p;
    if (l > 0) o += p;
    l = cmp(o + (p = n>>1));
  }
  int r;
  if (n == 2) {
    if (l > 0) return (o + 2)<<1;
    r = l;
    l = cmp(o);
  }
  if constexpr (Match) if (!l) return (o<<1) | 1;
  if (l <= 0) return o<<1;
  if (n == 1) return (o + 1)<<1;
  if constexpr (Match) if (!r) return ((o + 1)<<1) | 1;
  return (o + 1)<<1;
}
template <
  bool Match = true,
  typename Array,
  typename T,
  typename = ZuSame<ZuDecay<T>,
    ZuDecay<decltype(ZuDeclVal<const Array &>()[0])>>>
inline uint64_t ZuSearch(const Array &data, uint64_t n, const T &v)
{
  return ZuSearch<Match>(n,
      [&data, &v](uint64_t i) { return ZuCmp<T>::cmp(v, data[i]); });
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// interpolation search in sorted data (e.g. following ZuSort)
// - returns insertion position if not found
// - uses floating point for interpolation
template <bool Match = true, typename Cmp>
inline uint64_t ZuInterSearch(uint64_t n, Cmp cmp) {
  if (!n) return 0;
  if (n <= 2) { // special case for small arrays
    // std::cout << "cmp(0)\n";
    double l = cmp(0);
    if constexpr (Match) if (l == 0) return 1;
    if (l <= 0) return 0;
    if (n == 1) return 2;
    // std::cout << "cmp(1)\n";
    double r = cmp(1);
    if constexpr (Match) if (r == 0) return 3;
    if (r <= 0) return 2;
    return 4;
  }
  // std::cout << "cmp(0)\n";
  double l = cmp(0);
  // std::cout << "cmp(" << (n - 1) << ")\n";
  double r = cmp(n - 1);
  if constexpr (Match) if (l == 0) return 1;
  if (l <= 0) return 0;
  if (r > 0) return n<<1;
  uint64_t o = 0;
  do {
    uint64_t p;
    if (n <= 8)
      p = n>>1; // use binary search for small partitions
    else {
      double d = l - r; // "distance" of left-to-right value span
      p = (l * (n - 3) + (d / 2)) / d; // left/right interpolated pivot
    }
    double m = cmp(o + p);
    // std::cout << "l=" << l << " r=" << r << " p=" << p << " o=" << o << " n=" << n << " cmp(" << (o + p) << ")=" << m << "\n";
    if (m <= 0) {
      n = p + 1;
      r = m;
    } else {
      o += p;
      n -= p;
      l = m;
    }
  } while (n > 2);
  if constexpr (Match) {
    if (l == 0) return (o<<1) | 1;
    if (n > 1 && r == 0) return ((o + n - 1)<<1) | 1;
  }
  if (l <= 0) return o<<1;
  if (r > 0) return (o + n)<<1;
  return (o + 1)<<1;
}
template <
  bool Match = true,
  typename Array,
  typename T,
  typename = ZuSame<ZuDecay<T>,
    ZuDecay<decltype(ZuDeclVal<const Array &>()[0])>>>
inline uint64_t ZuInterSearch(const Array &data, uint64_t n, const T &v)
{
  return ZuInterSearch<Match>(n,
    [&data, v](uint64_t i) {
      return double(v) - double(data[i]);
    });
}

#endif /* ZuSearch_HH */
