//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "load_fix.hh"

namespace ZfixLoad {

  ZfStructImpl(SubParty); ZfStructImpl(SubParty, FIX);
  ZfStructImpl(Party); ZfStructImpl(Party, FIX);
  ZfStructImpl(Order); ZfStructImpl(Order, FIX);
  ZfStructImpl(Cancel); ZfStructImpl(Cancel, FIX);
  ZfStructImpl(Replace); ZfStructImpl(Replace, FIX);
  ZfStructImpl(Report); ZfStructImpl(Report, FIX);
  ZfStructImpl(SubParty, LoadScalar); ZfStructImpl(Party, LoadScalar);
  ZfStructImpl(SubParty, LoadText); ZfStructImpl(Party, LoadText);
  ZfStructImpl(SubParty, LoadBinary); ZfStructImpl(Party, LoadBinary);
  ZfStructImpl(Order, LoadScalar); ZfStructImpl(Order, LoadText); ZfStructImpl(Order, LoadBinary);
  ZfStructImpl(Replace, LoadScalar); ZfStructImpl(Replace, LoadText); ZfStructImpl(Replace,
  LoadBinary);
  ZfStructImpl(Report, LoadScalar); ZfStructImpl(Report, LoadText); ZfStructImpl(Report,
  LoadBinary);
} // ZfixLoad
