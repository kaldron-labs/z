//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtcMsg.hh>

namespace Ztc {

ZfStructImpl(Request);
ZfStructImpl(Request, FB);
ZfStructImpl(Ack);
ZfStructImpl(Ack, FB);
ZfStructImpl(EOS);
ZfStructImpl(EOS, FB);
ZfStructImpl(Error);
ZfStructImpl(Error, FB);
ZfStructImpl(HeapTelemetry);
ZfStructImpl(HeapTelemetry, FB);
ZfStructImpl(HashTelemetry);
ZfStructImpl(HashTelemetry, FB);
ZfStructImpl(ThreadTelemetry);
ZfStructImpl(ThreadTelemetry, FB);
ZfStructImpl(CxnTelemetry);
ZfStructImpl(CxnTelemetry, FB);
ZfStructImpl(MxTelemetry);
ZfStructImpl(MxTelemetry, FB);
ZfStructImpl(QueueTelemetry);
ZfStructImpl(QueueTelemetry, FB);
ZfStructImpl(HubTelemetry);
ZfStructImpl(HubTelemetry, FB);
ZfStructImpl(LinkTelemetry);
ZfStructImpl(LinkTelemetry, FB);
ZfStructImpl(PoolTelemetry);
ZfStructImpl(PoolTelemetry, FB);
ZfStructImpl(AppTelemetry);
ZfStructImpl(AppTelemetry, FB);
ZfStructImpl(AlertTelemetry);
ZfStructImpl(AlertTelemetry, FB);


ZfStructImpl(Shutdown);
ZfStructImpl(Shutdown, FB);
ZfStructImpl(Telemetry);
ZfStructImpl(Telemetry, FB);
ZfStructImpl(Payload);
ZfStructImpl(Payload, FB);
ZfStructImpl(Msg);
ZfStructImpl(Msg, FB);
ZtEnumImplNS(Group);
ZtEnumImplNS(AckStatus);
ZtEnumImplNS(EngineState);
ZtEnumImplNS(CxnType);
ZtEnumImplNS(QueueType);

} // namespace Ztc
