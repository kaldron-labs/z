//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// public Zum service-client wire types

#ifndef ZumTypes_HH
#define ZumTypes_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

namespace Zum {

ZuDerive(String, ZtString<ZtStringHeapID<"Zum.String">>);
ZuDerive(Bytes, (ZtArray<uint8_t, ZtArrayHeapID<"Zum.Bytes">>));
struct VecHeapID : public ZuStringT<"Zum.Vec"> { };
using VecHeap = ZtArrayHeapID_<VecHeapID>;
ZuDerive(StringVec, (ZtArray<String, VecHeap>));
ZuDerive(BytesVec, (ZtArray<Bytes,
  ZtArrayHeapID<"Zum.Saga.Images">>));
ZuDerive(IDVec, (ZtArray<uint64_t, VecHeap>));
ZuDerive(ActionIDVec, (ZtArray<uint32_t, VecHeap>));

using AppID = uint64_t;
using UserID = uint64_t;
using RoleID = uint64_t;
using ProviderID = uint64_t;
using ActionID = uint32_t;

} // namespace Zum

#endif /* ZumTypes_HH */
