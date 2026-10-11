//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtcMsg.hh>

namespace Ztc {

ZfStructImpl(Request);
ZfStructImpl(Ack);
ZfStructImpl(EOS);
ZfStructImpl(Error);
ZfStructImpl(HeapTelemetry);
ZfStructImpl(HashTelemetry);
ZfStructImpl(ThreadTelemetry);
ZfStructImpl(CxnTelemetry);
ZfStructImpl(MxTelemetry);
ZfStructImpl(QueueTelemetry);
ZfStructImpl(HubTelemetry);
ZfStructImpl(LinkTelemetry);
ZfStructImpl(PoolTelemetry);
ZfStructImpl(AppTelemetry);
ZfStructImpl(AlertTelemetry);


ZfStructImpl(Shutdown);
ZfStructImpl(Telemetry);
ZfStructImpl(Payload);
ZfStructImpl(Msg);
ZtEnumImplNS(Group);
ZtEnumImplNS(AckStatus);
ZtEnumImplNS(EngineState);
ZtEnumImplNS(CxnType);
ZtEnumImplNS(QueueType);

} // namespace Ztc
