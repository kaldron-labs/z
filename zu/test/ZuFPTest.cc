//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuLib.hh>
#include <zlib/ZuFP.hh>
#include <zlib/ZuBox.hh>
// #include <zlib/ZuHex.hh>

inline void out(const char *s) {
  std::cout << s << '\n' << std::flush;
}

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

template <typename F, typename I = typename ZuFP<F>::I>
void decode(F d, ZuTuple<I, I> em) {
  using FP = ZuFP<decltype(d)>;
  ZuTuple<I, I> em_ = FP::decode(d);
  if (em == em_) {
    std::cout << "OK  " << ZuBoxed(d) << " -> " << em << '\n';
  } else {
    std::cout << "NOK " << ZuBoxed(d) << " -> " << em_ << " != " << em << '\n';
  }
  auto e = FP::encode(em_.template p<0>(), em_.template p<1>());
  if (e == d || (FP::nan(e) && FP::nan(d))) {
    std::cout << "OK  " << em << " -> " << ZuBoxed(e) << '\n';
  } else {
    std::cout << "NOK " << em << " -> " << ZuBoxed(e) << '\n';
  }
}

int main()
{
  decode(1.0F, {0, 1});
  decode(1.0, {0, 1});
  decode(1.0L, {0, 1});
  decode(2.0, {1, 1});
  decode(8.0F, {3, 1});
  decode(8.0, {3, 1});
  decode(8.0L, {3, 1});
  decode(0.5, {-1, 1});
  decode(0.25, {-2, 1});
  decode(0.0625F, {-4, 1});
  decode(0.0625, {-4, 1});
  decode(0.0625L, {-4, 1});
  decode(ZuFP<double>::nan(), ZuFP<double>::decodeNaN());
  decode(ZuFP<double>::inf(), ZuFP<double>::decodePosInf());
  decode(-ZuFP<double>::inf(), ZuFP<double>::decodeNegInf());
  decode(ZuFP<float>::epsilon(1), {-23, 5});
  decode(ZuFP<double>::epsilon(1), {-52, 5});
  decode(ZuFP<long double>::epsilon(1), {-63, 5});
}
