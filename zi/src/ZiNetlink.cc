//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZiNetlink.hh>
#include <zlib/ZiNetlinkMsg.hh>

int ZiNetlink::connect(Socket sock,
    ZuCSpan familyName, unsigned int &familyID, uint32_t &portID) 
{
  {
    // this is our 'payload' coming after the nlmsghdr and genlmsghdr
    ZiNetlinkFamilyName fname(familyName);
    ZiGenericNetlinkHdr hdr(fname.len(),
	GENL_ID_CTRL, NLM_F_REQUEST, 0, 0, CTRL_CMD_GETFAMILY);
    ZiVec vecs[2] = {
      { (void *)&hdr, hdr.hdrSize() },
      { (void *)fname.data_(), fname.len() }
    };
    int len = send(
	sock, vecs, 2, hdr.hdrSize() + fname.len(), familyName.length());
    if (len < 0) return ENETUNREACH;
  }

  {
    ZiGenericNetlinkHdr hdr2;
    ZuArray<char, 128> data;
    ZiVec vecs[2] = {
      { (void *)&hdr2, hdr2.hdrSize() },
      { (void *)data.data(), (size_t)data.size() },
    };
    struct msghdr msg = { 
      0,		/* socket name (addr) */
      0,		/* name length */
      vecs,		/* iovecs */
      2,		/* vec length */
      0,		/* protocol magic */
      0,		/* length of cmd list */
      0			/* msg flags */
    };

    // LATER: can we determine response size a priori?
    int len = recv(sock, &msg, (int)MSG_WAITALL);
    if (len < 0) return ENETUNREACH;

    // Walk through all attributes
    ZiNetlinkAttr *na = (ZiNetlinkAttr *)data.data();
    do {
      if (na->type() == CTRL_ATTR_FAMILY_ID) {
	familyID = *((uint32_t *)na->data());
	portID = ((ZiNetlinkHdr *)(&hdr2))->pid();
	return Ze::OK;
      }
    } while ((void *)(na = na->next()) < (data.data() + len));
  }
  return ENETUNREACH;
}

int ZiNetlink::recv(Socket sock,
    unsigned int familyID, uint32_t portID, char *buf, int len) 
{
  ZiGenericNetlinkHdr hdr;
  ZiNetlinkSockAddr addr(portID);
  struct nlattr ignore;
      
  ZiVec vecs[3] = {
    { (void *)&hdr, (size_t)hdr.hdrSize() },
    { &ignore, sizeof(struct nlattr) },
    { (void *)buf, (size_t)len },
  };
  struct msghdr msg = { 
    addr.sa(),			/* socket name (addr) */
    (socklen_t)addr.len(),	/* name length */
    vecs,			/* iovecs */
    3,				/* vec length */
    0,				/* protocol magic */
    0,				/* length of cmd list */
    0				/* msg flags */
  };
  return recv(sock, &msg, 0);
}

int ZiNetlink::send(Socket sock,
    unsigned int familyID, uint32_t portID, const void *buf, int len) 
{
  if (familyID == 0) { return errno = EINVAL, -1; }

  ZiNetlinkDataAttr attr(len);
  ZiGenericNetlinkHdr hdr(attr.len(),
      familyID, NLM_F_REQUEST, 0, portID, ZiGenericNetlinkCmd_Forward);
  ZiVec vecs[3] = {
    { (void *)&hdr, (size_t)hdr.hdrSize() },
    { (void *)attr.data_(), (size_t)attr.hdrLen() },
    { (void *)buf, (size_t)len }
  };
  return send(sock, vecs, 3, hdr.hdrSize() + attr.hdrLen() + len, len);
}

int ZiNetlink::send(Socket sock, ZiVec *vecs, int nvecs, 
		    int totalBytes, int dataBytes) 
{
  int n = ::writev(sock, vecs, nvecs);
  return (n == totalBytes ? dataBytes : n);
}

int ZiNetlink::recv(Socket sock, struct msghdr *msg, int flags) 
{
  int n = 0;
retry:
  if ((n = ::recvmsg(sock, msg, flags)) < 0) {
    if (errno == EINTR) goto retry;
    return -1;
  }
    
  ZiGenericNetlinkHdr *hdr = 
    (ZiGenericNetlinkHdr *)ZiVec_ptr(*(msg->msg_iov));
  struct nlmsghdr *nlhdr = (struct nlmsghdr *)hdr->hdr();

  /* Validate response message: make sure full message was read */
  if (!NLMSG_OK(nlhdr, (uint32_t)n)) {
    errno = EIO;
    ZeLOG(Error, ZtSprintf("incorrect number of bytes received. "
			   "got %d expected %d. ", n, nlhdr->nlmsg_len));
    return -1;
  }

  /* error/ack message */
  if (NLMSG_ERROR == nlhdr->nlmsg_type) {
    struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nlhdr);
    /* DO NOT ACCESS errMsg->msg. It is partially located in
       user provided buf, because packet was received in a vector! */

    // Error only when error code < 0, otherwise - acknowledge
    if (err->error < 0) {
      errno = -err->error;
      ZeLOG(Error, ZtSprintf("netlink error \"%d\"", err->error));
      return -1;
    }
    return 0; // received ack - not an error, but 0 bytes of data
  }

  // ack the received packet back to the kernel
  if (nlhdr->nlmsg_flags & NLM_F_ACK) {
    ZiGenericNetlinkHdr ack(0, nlhdr->nlmsg_type, NLM_F_REQUEST, 
			    nlhdr->nlmsg_seq, nlhdr->nlmsg_pid, 
			    ZiGenericNetlinkCmd_Ack);
    if (ack.hdrSize() != write(sock, &ack, ack.hdrSize())) {
      errno = EIO;
      return -1;
    }
  }
  return n - hdr->hdrSize(); // data length we care about
}

