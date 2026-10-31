//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixTx.hh>

namespace Zfix {

ZtEnumImplNS(TxLoad);
ZtEnumImplNS(TxSelect);

TxConfig txConfig(SessionConfig config)
{
  ZmAssert(config.valid() && config.mode != Transport::Raw);
  return {ZuMv(config.header),
    config.appVerID, config.sequenceLimit, config.mode};
}

Fmt::Header txHeader(const TxConfig &config, uint64_t seqNo,
    ZuDateTime now, ZuDateTime original)
{
  Fmt::Header h;
  const auto &header = config.header;
  h.beginString = Version::Wire::v2s(header.version);
  h.sender = header.local;
  h.target = header.peer;
  if (header.localSub.length()) h.senderSub = header.localSub;
  if (header.peerSub.length()) h.targetSub = header.peerSub;
  if (header.localLocation.length()) h.senderLocation = header.localLocation;
  if (header.peerLocation.length()) h.targetLocation = header.peerLocation;
  if (config.mode == Transport::FIXT11) h.appVerID = AppVersion::Wire::v2s(config.appVerID);
  h.seqNo = seqNo;
  h.sendingTime = now;
  h.origSendingTime = original;
  h.possDup = *original;
  return h;
}
} // Zfix

