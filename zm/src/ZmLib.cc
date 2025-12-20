//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z Multithreading Library

#include <zlib/ZmLib.hh>

ZmExtern const char ZmLib[] = "@(#) Z Multithreading Library v" Z_VERNAME;

// force DLL inclusion of ZmBackoff and ZmTimeout

#include <zlib/ZmBackoff.hh>
#include <zlib/ZmTimeout.hh>
