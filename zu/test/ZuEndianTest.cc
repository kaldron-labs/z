//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuArray.hh>

ZuAssert((ZuIsSame<ZuByteSwap<ZuByteSwap<uint32_t>>, uint32_t>{}));
ZuAssert((ZuIsSame<ZuByteSwap<ZuByteSwap<ZuBox<uint32_t>>>,
    ZuBox<uint32_t>>{}));

template <typename T>
void test(T v)
{
  ZuTestScope(test);
  char _[sizeof(T)] = { 0 };
  T &d = *(new (_) T{v});
  ZuByteSwap<T> e = d;
  ZuCheck(
    reinterpret_cast<const uint8_t *>(&d)[0] == 
    reinterpret_cast<const uint8_t *>(&e)[sizeof(T) - 1]);
  ZuCheck(
    reinterpret_cast<const uint8_t *>(&e)[0] == 
    reinterpret_cast<const uint8_t *>(&d)[sizeof(T) - 1]);
  ZuCheck(d == e);
  d = e;
  ++d, ++e;
  ZuCheck(
    reinterpret_cast<const uint8_t *>(&d)[0] == 
    reinterpret_cast<const uint8_t *>(&e)[sizeof(T) - 1]);
  ZuCheck(
    reinterpret_cast<const uint8_t *>(&e)[0] == 
    reinterpret_cast<const uint8_t *>(&d)[sizeof(T) - 1]);
  ZuCheck(d == e);
  if constexpr (ZuTraits<T>::IsComposite)
    reinterpret_cast<T *>(_)->~T();
}

int main()
{
  ZuTestMain();
  ZuTestCall((test<uint16_t>), 42000);
  ZuTestCall((test<int16_t>), -4200);
  ZuTestCall((test<uint32_t>), 4200042);
  ZuTestCall((test<int32_t>), -420042);
  ZuTestCall((test<uint64_t>), 420000000000042ULL);
  ZuTestCall((test<int64_t>), -42000000000042ULL);
  ZuTestCall((test<uint128_t>), uint128_t(420000000000042ULL)<<69);
  ZuTestCall((test<int128_t>), int128_t(-42000000000042)<<69);
  ZuTestCall((test<float>), 42.42);
  ZuTestCall((test<ZuBox<float>>), 42.42);
  ZuTestCall((test<double>), 42.420001);
  ZuTestCall((test<ZuBox<double>>), -42.420001);
  ZuTestCall((test<long double>), 42.420000001L);
  ZuTestCall((test<ZuBox<long double>>), -42.420000001L);
}
