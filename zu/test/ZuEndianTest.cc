//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuLib.hh>

#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuArray.hh>

inline void out(bool ok, const char *s) {
  std::cout << (ok ? "OK  " : "NOK ") << s << '\n' << std::flush;
  assert(ok);
}

#define CHECK(x) (out((x), #x))

template <typename T>
void test(T v)
{
  char _[sizeof(T)] = { 0 };
  T &d = *(new (_) T{v});
  ZuByteSwap<T> e = d;
  CHECK(
    reinterpret_cast<const uint8_t *>(&d)[0] == 
    reinterpret_cast<const uint8_t *>(&e)[sizeof(T) - 1]);
  CHECK(
    reinterpret_cast<const uint8_t *>(&e)[0] == 
    reinterpret_cast<const uint8_t *>(&d)[sizeof(T) - 1]);
  CHECK(d == e);
  d = e;
  ++d, ++e;
  CHECK(
    reinterpret_cast<const uint8_t *>(&d)[0] == 
    reinterpret_cast<const uint8_t *>(&e)[sizeof(T) - 1]);
  CHECK(
    reinterpret_cast<const uint8_t *>(&e)[0] == 
    reinterpret_cast<const uint8_t *>(&d)[sizeof(T) - 1]);
  CHECK(d == e);
  if constexpr (ZuTraits<T>::IsComposite)
    reinterpret_cast<T *>(_)->~T();
}

int main()
{
  test<uint16_t>(42000);
  test<int16_t>(-4200);
  test<uint32_t>(4200042);
  test<int32_t>(-420042);
  test<uint64_t>(420000000000042ULL);
  test<int64_t>(-42000000000042ULL);
  test<uint128_t>(uint128_t(420000000000042ULL)<<69);
  test<int128_t>(int128_t(-42000000000042)<<69);
  test<float>(42.42);
  test<ZuBox<float>>(42.42);
  test<double>(42.420001);
  test<ZuBox<double>>(-42.420001);
  test<long double>(42.420000001L);
  test<ZuBox<long double>>(-42.420000001L);
}
