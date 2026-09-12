//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumAdmin.hh>

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZiLog.hh>

namespace Zum {

void logEvent(Audit audit)
{
  ZiLOG(Info, "Zum", ([event = ZuMv(audit)](auto &s) {
    s << "time=" << event.time << " issuer=" << event.issuer
      << " app=" << event.appID
      << " actor=" << event.actor << " operation=" << event.operationID
      << " subject=" << event.subject << " target=" << event.target
      << " event=" << int(event.event)
      << " outcome=" << int(event.outcome)
      << " correlation=" << event.correlationID << " detail=" << event.detail;
  }));
}

String auditID(ZuBSpan id)
{
  String target;
  target.length(ZuBase64URL::enclen(id.length()));
  target.length(ZuBase64URL::encode(target.span(), id));
  return target;
}

Audit managementAuditRecord(
    String issuer, int operation, String actor, AppID appID, String target,
    String correlationID, unsigned status, int64_t now)
{
  return Audit{
    .time = now,
    .issuer = ZuMv(issuer),
    .appID = appID,
    .operationID = ActionID(operation),
    .actor = ZuMv(actor),
    .target = ZuMv(target),
    .event = AuditEvent::Administration,
    .outcome = AuditOutcome::T(status >= 200 && status < 300 ?
      AuditOutcome::Success : AuditOutcome::Failure),
    .correlationID = ZuMv(correlationID),
    .detail = MgmtOp::name(operation)
  };
}

} // namespace Zum
