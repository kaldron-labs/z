//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z User Management library main header

#ifndef ZumLib_HH
#define ZumLib_HH

#include <zlib/ZuLib.hh>

#include <zlib/ZmObject.hh>
#include <zlib/ZmPolymorph.hh>

#ifdef _WIN32

#ifdef ZUM_EXPORTS
#define ZumAPI ZuExport_API
#define ZumExplicit ZuExport_Explicit
#else
#define ZumAPI ZuImport_API
#define ZumExplicit ZuImport_Explicit
#endif
#define ZumExtern extern ZumAPI

#else

#define ZumAPI
#define ZumExplicit
#define ZumExtern extern

#endif

class ZumAPI ZumObjectAlloc {
public:
  static void *operator new(size_t);
  static void operator delete(void *);
  static void operator delete(void *, size_t);
};

class ZumObject : public ZumObjectAlloc, public ZmObject { };
class ZumPolymorph : public ZumObjectAlloc, public ZmPolymorph { };

#endif /* ZumLib_HH */
