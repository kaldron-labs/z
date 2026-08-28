//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// test filesystem and shared-memory residue lifecycle

#ifndef ZiTestResidue_HH
#define ZiTestResidue_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiPlatform.hh>

namespace ZiTestResidue {

enum { Age = 8 }; // current failure plus eight diagnostic archives

ZuDerive(Paths, (
  ZtArray<Zi::Path, ZtArrayHeapID<"ZiTestResidue.Paths">>));

void init(const char *testName);
Zi::Path path(ZuCSpan name);
Zi::Path file(ZuCSpan name);
Zi::Path dir(ZuCSpan name);
void add(const Zi::Path &path);
Paths glob(const Zi::Path &dir, ZuCSpan prefix);
void del(const Zi::Path &dir, ZuCSpan prefix);
Zi::Name uniqueName(const char *tag = nullptr);
void addShm(Zi::Name name);
void final(bool passed);
void cleanup();

} // ZiTestResidue

#endif /* ZiTestResidue_HH */
