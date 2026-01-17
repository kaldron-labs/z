//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZiLog.hh>

#include <zlib/ZiGlob.hh>

int main(int argc, char **argv)
{
  ZiGlob g;
  for (int i = 1; i < argc; i++) {
    g.init(argv[i]);
    const auto &dirName = g.dirName();
    while (auto entry = g.iterate(true, false))
      std::cout << (Zi::Name{}
	<< dirName << '/' << entry->name << (entry->isdir ? "/\n" : "\n"));
  }
}
