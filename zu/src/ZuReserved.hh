//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// C pre-processor macro to condition on a word being reserved
// - ZuIfReserved(Word, Teue, False) evaluates to:
//   True(Word) if Word is a C++23 reserved word
//   False(Word) otherwise

#ifndef ZuReserved_HH
#define ZuReserved_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuPP.hh>

// all C++ reserved words as of C++23
#define ZuReserved__alignas ,
#define ZuReserved__alignof ,
#define ZuReserved__and ,
#define ZuReserved__and_eq ,
#define ZuReserved__asm ,
#define ZuReserved__auto ,
#define ZuReserved__bitand ,
#define ZuReserved__bitor ,
#define ZuReserved__bool ,
#define ZuReserved__break ,
#define ZuReserved__case ,
#define ZuReserved__catch ,
#define ZuReserved__char ,
#define ZuReserved__char16_t ,
#define ZuReserved__char32_t ,
#define ZuReserved__char8_t ,
#define ZuReserved__class ,
#define ZuReserved__co_await ,
#define ZuReserved__compl ,
#define ZuReserved__concept ,
#define ZuReserved__const ,
#define ZuReserved__const_cast ,
#define ZuReserved__consteval ,
#define ZuReserved__constexpr ,
#define ZuReserved__constinit ,
#define ZuReserved__continue ,
#define ZuReserved__co_return ,
#define ZuReserved__co_yield ,
#define ZuReserved__decltype ,
#define ZuReserved__default ,
#define ZuReserved__delete ,
#define ZuReserved__do ,
#define ZuReserved__double ,
#define ZuReserved__dynamic_cast ,
#define ZuReserved__else ,
#define ZuReserved__enum ,
#define ZuReserved__explicit ,
#define ZuReserved__export ,
#define ZuReserved__extern ,
#define ZuReserved__false ,
#define ZuReserved__float ,
#define ZuReserved__for ,
#define ZuReserved__friend ,
#define ZuReserved__goto ,
#define ZuReserved__if ,
#define ZuReserved__inline ,
#define ZuReserved__int ,
#define ZuReserved__long ,
#define ZuReserved__mutable ,
#define ZuReserved__namespace ,
#define ZuReserved__new ,
#define ZuReserved__noexcept ,
#define ZuReserved__not ,
#define ZuReserved__not_eq ,
#define ZuReserved__nullptr ,
#define ZuReserved__operator ,
#define ZuReserved__or ,
#define ZuReserved__private ,
#define ZuReserved__protected ,
#define ZuReserved__public ,
#define ZuReserved__register ,
#define ZuReserved__reinterpret_cast ,
#define ZuReserved__requires ,
#define ZuReserved__return ,
#define ZuReserved__short ,
#define ZuReserved__signed ,
#define ZuReserved__sizeof ,
#define ZuReserved__static ,
#define ZuReserved__static_assert ,
#define ZuReserved__static_cast ,
#define ZuReserved__struct ,
#define ZuReserved__switch ,
#define ZuReserved__template ,
#define ZuReserved__this ,
#define ZuReserved__thread_local ,
#define ZuReserved__throw ,
#define ZuReserved__true ,
#define ZuReserved__try ,
#define ZuReserved__typedef ,
#define ZuReserved__typeid ,
#define ZuReserved__typename ,
#define ZuReserved__union ,
#define ZuReserved__unsigned ,
#define ZuReserved__using ,
#define ZuReserved__virtual ,
#define ZuReserved__void ,
#define ZuReserved__volatile ,
#define ZuReserved__wchar_t ,
#define ZuReserved__while ,
#define ZuReserved__xor ,
#define ZuReserved__xor_eq ,

#define ZuReservedWord(word) ZuReserved__##word

#define ZuIfReserved_(_0, _1, _2, ...) _2
#define ZuIfReserved(Word, True, False) \
  ZuPP_Eval_(ZuPP_Defer(ZuIfReserved_)(ZuReservedWord(Word), \
      ZuPP_Defer(True)(Word), ZuPP_Defer(False)(Word)))

#endif /* ZuReserved_HH */
