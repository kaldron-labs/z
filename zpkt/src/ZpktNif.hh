//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private kernel-interface query and rtnetlink transport.

#ifndef ZpktNif_HH
#define ZpktNif_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
#endif

#include <linux/rtnetlink.h>

#include <zlib/ZtArray.hh>
#include <zlib/Zpkt.hh>

namespace Zpkt {

using NifReply = ZtArray<uint8_t, ZtArrayHeapID<"Zpkt.Nif.Reply">>;
ZpktAPI ZuUnion<int, ZeException> nifOpen();
ZpktAPI ZuUnion<NifInfo, ZeException> nifQuery(ZuCSpan name);
ZpktAPI ZuUnion<void, ZeException> nifTransact(
  nlmsghdr &request, NifReply *reply = nullptr,
  bool *rejected = nullptr, ZeError *native = nullptr, int fd = -1);

} // namespace Zpkt
#endif /* ZpktNif_HH */
