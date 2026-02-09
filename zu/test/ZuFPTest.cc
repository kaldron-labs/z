//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTest.hh>
#include <zlib/ZuFP.hh>
#include <zlib/ZuBox.hh>

template <typename F, typename I = typename ZuFP<F>::I>
void decode(F d, ZuTuple<I, I> em)
{
  ZuTestScope(decode);
  using FP = ZuFP<decltype(d)>;
  ZuTuple<I, I> em_ = FP::decode(d);
  ZuCheck(em == em_,
    std::cerr << ZuBoxed(d) << " -> " << em_ << " != " << em << '\n');
  auto e = FP::encode(em_.template p<0>(), em_.template p<1>());
  ZuCheck(e == d || (FP::nan(e) && FP::nan(d)),
    std::cerr << em << " -> " << ZuBoxed(e) << " expected " << ZuBoxed(d) << '\n');
}

int main()
{
  ZuTestMain();
  ZuTestCall(decode, 1.0F, (ZuTuple<int, int>{0, 1}));
  ZuTestCall(decode, 1.0, (ZuTuple<int, int>{0, 1}));
  ZuTestCall(decode, 1.0L, (ZuTuple<int, int>{0, 1}));
  ZuTestCall(decode, 2.0, (ZuTuple<int, int>{1, 1}));
  ZuTestCall(decode, 8.0F, (ZuTuple<int, int>{3, 1}));
  ZuTestCall(decode, 8.0, (ZuTuple<int, int>{3, 1}));
  ZuTestCall(decode, 8.0L, (ZuTuple<int, int>{3, 1}));
  ZuTestCall(decode, 0.5, (ZuTuple<int, int>{-1, 1}));
  ZuTestCall(decode, 0.25, (ZuTuple<int, int>{-2, 1}));
  ZuTestCall(decode, 0.0625F, (ZuTuple<int, int>{-4, 1}));
  ZuTestCall(decode, 0.0625, (ZuTuple<int, int>{-4, 1}));
  ZuTestCall(decode, 0.0625L, (ZuTuple<int, int>{-4, 1}));
  ZuTestCall(decode, ZuFP<double>::nan(), ZuFP<double>::decodeNaN());
  ZuTestCall(decode, ZuFP<double>::inf(), ZuFP<double>::decodePosInf());
  ZuTestCall(decode, -ZuFP<double>::inf(), ZuFP<double>::decodeNegInf());
  ZuTestCall(decode, ZuFP<float>::epsilon(1), (ZuTuple<int, int>{-23, 5}));
  ZuTestCall(decode, ZuFP<double>::epsilon(1), (ZuTuple<int, int>{-52, 5}));
  ZuTestCall(decode, ZuFP<long double>::epsilon(1), (ZuTuple<int, int>{-63, 5}));
}
