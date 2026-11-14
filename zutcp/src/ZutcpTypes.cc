//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zutcp.hh>

namespace Zutcp {

ZtEnumImplStruct(DataError);

#define ZutcpEnumMatchers(ID) \
  ZtEnumImplStruct(ID); \
  ZtEnumImplStruct(ID, JSON)

ZutcpEnumMatchers(PktNumSpace);
ZutcpEnumMatchers(PktType);
ZutcpEnumMatchers(LinkState);
ZutcpEnumMatchers(FrameType);
ZutcpEnumMatchers(EcnMark);

#undef ZutcpEnumMatchers

ZtEnumImplStruct(TransportError);
ZtEnumImplStruct(CxnState);
ZtEnumImplStruct(PathMode);
ZtEnumImplStruct(IPFamily);
ZtEnumImplStruct(PMTUDState);
ZtEnumImplStruct(PathECNState);
ZtEnumImplStruct(PathHintKind);
ZtEnumImplStruct(ServerPktAction);
ZtEnumImplStruct(RecEvt);
ZtEnumImplStruct(SentFrameKind);

} // namespace Zutcp
