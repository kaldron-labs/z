//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// generic delegated STL iterator
// - reduced boilerplate
// - random access, but not necessarily contiguous

#ifndef ZuIter_HH
#define ZuIter_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <iterator>

template <typename Container_, typename Elem_>
class ZuIter {
public:
  using Container = Container_;
  using Elem = Elem_;

  using iterator_category = std::random_access_iterator_tag;
  using value_type = Elem;
  using difference_type = ptrdiff_t;
  using pointer = Elem *;
  using reference = Elem &;

  ZuIter() = delete;
  ZuIter(Container &container_, uint64_t i) :
      container{container_}, i{i} { }
  ZuIter(const ZuIter &) = default;
  ZuIter &operator =(const ZuIter &) = default;
  ZuIter(ZuIter &&) = default;
  ZuIter &operator =(ZuIter &&) = default;

  ZuIter &operator++() { ++i; return *this; }
  ZuIter operator++(int) { auto _ = *this; ++(*this); return _; }
  ZuIter &operator--() { --i; return *this; }
  ZuIter operator--(int) { auto _ = *this; --(*this); return _; }

  bool operator ==(const ZuIter &r) const {
    return &container == &r.container && i == r.i;
  }

  inline decltype(auto) operator *(this auto &&self) {
    return ZuFwdLike<decltype(self)>(self.container)[self.i];
  }

  friend inline ptrdiff_t operator -(const ZuIter &l, const ZuIter &r) {
    return ptrdiff_t(l.i) - ptrdiff_t(r.i);
  }

protected:
  Container	&container;
  uint64_t	i;
};

#endif /* ZuIter_HH */
