//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZiNetlink_HH
#define ZiNetlink_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZmAssert.hh>

#include <zlib/ZeLog.hh>

#include <zlib/ZiIP.hh>
#include <zlib/ZiPlatform.hh>

class ZiNetlinkSockAddr {
  struct sockaddr_nl  m_snl;

public:
  ZiNetlinkSockAddr() {
    m_snl.nl_family = AF_NETLINK;
    memset(&m_snl.nl_pad, 0, sizeof(m_snl.nl_pad));
    // LATER: we always let the kernel specify these...
    m_snl.nl_groups = 0;
    m_snl.nl_pid = 0;
  }

  ZiNetlinkSockAddr(uint32_t portID) {
    m_snl.nl_family = AF_NETLINK;
    memset(&m_snl.nl_pad, 0, sizeof(m_snl.nl_pad));
    m_snl.nl_groups = 0;
    m_snl.nl_pid = portID;
  }

  ZuInline struct sockaddr *sa() { return (struct sockaddr *)&m_snl; }
  ZuInline int len() const { return sizeof(struct sockaddr_nl); }

  template <typename S> void print(S &s) const {
    s << "pid=" << ZuBoxed(m_snl.nl_pid) <<
      " groups=" << ZuBoxed(m_snl.nl_groups);
  }
  friend ZuPrintFn ZuPrintType(ZiNetlinkSockAddr *);
};

class ZiNetlink {
  friend ZiMultiplex;
  friend ZiConnection;

  using Socket = Zi::Socket;

  // takes the familyName and gets the familyID and a portID from
  // the kernel. The familyID and portID are needed for
  // further communication. Returns 0 on success non-zero on error
  static int connect(Socket sock, ZuCSpan familyName,
      unsigned int &familyID, uint32_t &portID);

  // read 'len' bytes into buffer 'buf' for the given familyID and portID
  // the nlmsghdr and genlmsghdr are read into scratch space and ignored.
  static int recv(Socket sock, unsigned int familyID,
      uint32_t portID, char *buf, int len);

  // send data in 'buf' of length 'len'. This method will prepend
  // an nlmsghdr and genlmsghdr as well as a ZiGNLAttr_Data attribute
  // to the data. The attribute describes the data in 'buf'
  static int send(Socket sock, unsigned int familyID,
      uint32_t portID, const void *buf, int len);

private:
  static int send(Socket sock,
      ZiVec *vecs, int nvecs, int totalBytes, int dataBytes);
  static int recv(Socket sock, struct msghdr *msg, int flags);
};

#endif /* ZiNetlink_HH */
