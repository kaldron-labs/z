// -*- mode: c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
// vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i4

#include <zlib/ZiNetlinkMsg.hh>
#include <zlib/ZiMultiplex.hh>

ZiGenericNetlinkHdr::ZiGenericNetlinkHdr(
    ZiConnection *connection, uint32_t seqNo, uint32_t len) :
  ZiNetlinkHdr(GENL_HDRLEN + len, connection->info().familyID(), 
	       NLM_F_REQUEST, seqNo, connection->info().portID()) {
  m_g.cmd = ZiGenericNetlinkCmd_Forward;
  m_g.version = ZiGenericNetlinkVersion;
  m_g.reserved = 0;
}
