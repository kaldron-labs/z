//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z User Management library main header

#include <zlib/ZumLib.hh>
#include <zlib/zum_cli.hh>

ZumExtern const char ZumLib[] = "@(#) Z User Management Library v" Z_VERNAME;

int ZfCLI::LongMatcher<ZumCLI::Options, ZuFacet::CLI>::exact(ZuCSpan key)
{
  using Entries = typename
    ZfCLI::LongEntries<ZumCLI::Options, ZuFacet::CLI, "">::T;
  using KeyList = ZuTypeUnique<ZuTypeMap<ZfCLI::LongKey, Entries>>;
  struct IDs { using Keys = KeyList; };
  static constexpr auto matcher = ZuMatcher<IDs>();
  return matcher.exact(key);
}
