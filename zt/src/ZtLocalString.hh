//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// extends ZtString with an initial stack-allocated buffer (see ZmAlloc)
// - falls back to heap allocation if stack space is insufficient
// - auto var = ZtLocalString(ZtString<>, size); // length initialized to 0
// - auto var = ZtLocalString(ZtString<>, length, size);
// 
// ZtWString is the same, but for ZtWString

#ifndef ZtLocalString_HH
#define ZtLocalString_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZmAlloc.hh>

#include <zlib/ZtString.hh>

template <typename String>
struct ZtLocalString_ : private ZmAlloc_<typename String::Char>, public String {
  using Char = typename String::Char;
  using String::data;
  using String::operator [];
  using String::operator !;
  ZuOpBool

  ZtLocalString_(ZmAlloc_<Char> buf, unsigned size) :
    ZmAlloc_<Char>(ZuMv(buf)),
    String(ZmAlloc_<Char>::ptr, 0, size, false) { }
  ZtLocalString_(ZmAlloc_<Char> buf, unsigned length, unsigned size) :
    ZmAlloc_<Char>(ZuMv(buf)),
    String(ZmAlloc_<Char>::ptr, length, size, false)
  {
    auto ptr = ZmAlloc_<Char>::ptr;
    if (ZuUnlikely(!ptr)) String::null_();
  }
};

#define ZtLocalString_1(T, size) \
  ZtLocalString_<T>(ZmAlloc(typename T::Char, size), size)
#define ZtLocalString_2(T, length, size) \
  ZtLocalString_<T>(ZmAlloc(typename T::Char, size), length, size)
#define ZtLocalString_N(_0, _1, Fn, ...) Fn
#define ZtLocalString__(T, ...) \
  ZtLocalString_N(__VA_ARGS__, \
    ZtLocalString_2(T, __VA_ARGS__), \
    ZtLocalString_1(T, __VA_ARGS__))
#define ZtLocalString(...) \
  ZuPP_Eval(ZuPP_Defer(ZtLocalString__)(__VA_ARGS__))

#endif /* ZtLocalString_HH */
