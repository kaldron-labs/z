//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// C/C++ pre-processor macros

#ifndef ZuPP_HH
#define ZuPP_HH

#define ZuPP_Q(...) #__VA_ARGS__

#define ZuPP_Eval(...) ZuPP_Eval128(__VA_ARGS__)
// #define ZuPP_Eval(...) ZuPP_Eval1024(__VA_ARGS__)
// #define ZuPP_Eval1024(...) ZuPP_Eval512(ZuPP_Eval512(__VA_ARGS__))
// #define ZuPP_Eval512(...) ZuPP_Eval256(ZuPP_Eval256(__VA_ARGS__))
// #define ZuPP_Eval256(...) ZuPP_Eval128(ZuPP_Eval128(__VA_ARGS__))
#define ZuPP_Eval128(...) ZuPP_Eval64(ZuPP_Eval64(__VA_ARGS__))
#define ZuPP_Eval64(...) ZuPP_Eval32(ZuPP_Eval32(__VA_ARGS__))
#define ZuPP_Eval32(...) ZuPP_Eval16(ZuPP_Eval16(__VA_ARGS__))
#define ZuPP_Eval16(...) ZuPP_Eval8(ZuPP_Eval8(__VA_ARGS__))
#define ZuPP_Eval8(...) ZuPP_Eval4(ZuPP_Eval4(__VA_ARGS__))
#define ZuPP_Eval4(...) ZuPP_Eval2(ZuPP_Eval2(__VA_ARGS__))
#define ZuPP_Eval2(...) ZuPP_Eval1(ZuPP_Eval1(__VA_ARGS__))
#define ZuPP_Eval1(...) __VA_ARGS__

// nested eval
#define ZuPP_Eval_(...) ZuPP_Eval_128(__VA_ARGS__)
// #define ZuPP_Eval_(...) ZuPP_Eval_1024(__VA_ARGS__)
// #define ZuPP_Eval_1024(...) ZuPP_Eval_512(ZuPP_Eval_512(__VA_ARGS__))
// #define ZuPP_Eval_512(...) ZuPP_Eval_256(ZuPP_Eval_256(__VA_ARGS__))
// #define ZuPP_Eval_256(...) ZuPP_Eval_128(ZuPP_Eval_128(__VA_ARGS__))
#define ZuPP_Eval_128(...) ZuPP_Eval_64(ZuPP_Eval_64(__VA_ARGS__))
#define ZuPP_Eval_64(...) ZuPP_Eval_32(ZuPP_Eval_32(__VA_ARGS__))
#define ZuPP_Eval_32(...) ZuPP_Eval_16(ZuPP_Eval_16(__VA_ARGS__))
#define ZuPP_Eval_16(...) ZuPP_Eval_8(ZuPP_Eval_8(__VA_ARGS__))
#define ZuPP_Eval_8(...) ZuPP_Eval_4(ZuPP_Eval_4(__VA_ARGS__))
#define ZuPP_Eval_4(...) ZuPP_Eval_2(ZuPP_Eval_2(__VA_ARGS__))
#define ZuPP_Eval_2(...) ZuPP_Eval_1(ZuPP_Eval_1(__VA_ARGS__))
#define ZuPP_Eval_1(...) __VA_ARGS__

// sub-nested eval
#define ZuPP_Eval__(...) ZuPP_Eval__128(__VA_ARGS__)
// #define ZuPP_Eval__(...) ZuPP_Eval__1024(__VA_ARGS__)
// #define ZuPP_Eval__1024(...) ZuPP_Eval__512(ZuPP_Eval__512(__VA_ARGS__))
// #define ZuPP_Eval__512(...) ZuPP_Eval__256(ZuPP_Eval__256(__VA_ARGS__))
// #define ZuPP_Eval__256(...) ZuPP_Eval__128(ZuPP_Eval__128(__VA_ARGS__))
#define ZuPP_Eval__128(...) ZuPP_Eval__64(ZuPP_Eval__64(__VA_ARGS__))
#define ZuPP_Eval__64(...) ZuPP_Eval__32(ZuPP_Eval__32(__VA_ARGS__))
#define ZuPP_Eval__32(...) ZuPP_Eval__16(ZuPP_Eval__16(__VA_ARGS__))
#define ZuPP_Eval__16(...) ZuPP_Eval__8(ZuPP_Eval__8(__VA_ARGS__))
#define ZuPP_Eval__8(...) ZuPP_Eval__4(ZuPP_Eval__4(__VA_ARGS__))
#define ZuPP_Eval__4(...) ZuPP_Eval__2(ZuPP_Eval__2(__VA_ARGS__))
#define ZuPP_Eval__2(...) ZuPP_Eval__1(ZuPP_Eval__1(__VA_ARGS__))
#define ZuPP_Eval__1(...) __VA_ARGS__

// use ZuPP_Defer(x) to defer evaluation of x

#define ZuPP_Empty()
#define ZuPP_Defer(x) x ZuPP_Empty ZuPP_Empty()()

// use ZuPP_Strip(x) to strip x of any parentheses

#define ZuPP_Strip__(...) ZuPP_Strip__ __VA_ARGS__
#define ZuPP_Strip_Null_ZuPP_Strip__
#define ZuPP_Strip_Concat_(x, ...) x ## __VA_ARGS__
#define ZuPP_Strip_Concat(x, ...) ZuPP_Strip_Concat_(x, __VA_ARGS__)
#define ZuPP_Strip(x) ZuPP_Strip_Concat(ZuPP_Strip_Null_, ZuPP_Strip__ x)

// use ZuPP_StripAppend(x) to strip x of any parentheses and append to args

#define ZuPP_StripAppend__(...) ZuPP_StripAppend__ __VA_OPT__(, __VA_ARGS__)
#define ZuPP_StripAppend_Null_ZuPP_StripAppend__
#define ZuPP_StripAppend_Concat_(x, ...) x ## __VA_ARGS__
#define ZuPP_StripAppend_Concat(x, ...) \
  ZuPP_StripAppend_Concat_(x, __VA_ARGS__)
#define ZuPP_StripAppend(x) \
  ZuPP_StripAppend_Concat(ZuPP_StripAppend_Null_, ZuPP_StripAppend__ x)

// map expansions - the ...Comma versions suppress trailing commas

#define ZuPP_Map(map, first, ...) \
  map(first) __VA_OPT__(ZuPP_Defer(ZuPP_Map_)()(map, __VA_ARGS__))
#define ZuPP_Map_() ZuPP_Map

#define ZuPP_MapComma(map, first, ...) \
  map(first) __VA_OPT__(, ZuPP_Defer(ZuPP_MapComma_)()(map, __VA_ARGS__))
#define ZuPP_MapComma_() ZuPP_MapComma

#define ZuPP_MapArg(map, arg, first, ...) \
  map(arg, first) __VA_OPT__(ZuPP_Defer(ZuPP_MapArg_)()(map, arg, __VA_ARGS__))
#define ZuPP_MapArg_() ZuPP_MapArg

#define ZuPP_MapArgPair(map, l, r, first, ...) \
  map(l, r, first) __VA_OPT__(ZuPP_Defer(ZuPP_MapArgPair_)()(map, l, r, __VA_ARGS__))
#define ZuPP_MapArgPair_() ZuPP_MapArgPair

#define ZuPP_MapArgComma(map, arg, first, ...) \
  map(arg, first) \
  __VA_OPT__(, ZuPP_Defer(ZuPP_MapArgComma_)()(map, arg, __VA_ARGS__))
#define ZuPP_MapArgComma_() ZuPP_MapArgComma

#define ZuPP_MapArgPairComma(map, l, r, first, ...) \
  map(l, r, first) \
  __VA_OPT__(, ZuPP_Defer(ZuPP_MapArgPairComma_)()(map, l, r, __VA_ARGS__))
#define ZuPP_MapArgPairComma_() ZuPP_MapArgPairComma

#define ZuPP_MapIndex(map, i, first, ...) \
  map(i, first) \
  __VA_OPT__(ZuPP_Defer(ZuPP_MapIndex_)()(map, (i + 1), __VA_ARGS__))
#define ZuPP_MapIndex_() ZuPP_MapIndex

#define ZuPP_MapIndexComma(map, i, first, ...) \
  map(i, first) \
  __VA_OPT__(, ZuPP_Defer(ZuPP_MapIndexComma_)()(map, (i + 1), __VA_ARGS__))
#define ZuPP_MapIndexCommaComma_() ZuPP_MapIndexComma

#define ZuPP_PfxTypename_(Arg) typename Arg
#define ZuPP_PfxTypename(Args) \
  template <ZuPP_Eval(ZuPP_Defer(ZuPP_MapComma)(ZuPP_PfxTypename_, ZuPP_Strip(Args)))>

#endif /* ZuPP_HH */
