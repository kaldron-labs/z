//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Fixed owner-to-owner publication metadata

#ifndef ZfixNotice_HH
#define ZfixNotice_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZtEnum.hh>

namespace Zfix {

struct TxContext {
  uint64_t	epoch = 0;
  uint64_t	connection = 0;
  uint64_t	seqNo = 0;
  bool		replay = false;
};

ZtEnumNS(ZfixAPI, TxNoticeKind, int8_t,
  Logon, Probe, Recovery, Drained, DrainBoundary, ResetReady, ResetAborted);
struct TxNotice {
  TxContext	context;
  uint64_t	marker = 0;
  uint64_t	loaded = 1;
  uint64_t	published = 1;
  uint64_t	acknowledged = 1;
  unsigned	kind = TxNoticeKind::Recovery;
  bool		done = false;
  uint64_t	nextEpoch = 0;
  uint64_t	request = 0;
};

} // Zfix

#endif /* ZfixNotice_HH */
