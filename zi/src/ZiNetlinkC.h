// -*- mode: c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
// vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i4

// User-space <-> Kernel Generic Netlink Interface
// Common C source for user-mode and kernel-mode

#ifndef zi_netlink_H
#define zi_netlink_H

#ifdef __cplusplus
extern "C" {
#endif

#define ZiGNLVersion 1

// nlattr.nla_type values
enum ZiGNLAttr {
  ZiGNLAttr_Unspec = 0,
  ZiGNLAttr_Data,		// Data of any length
  ZiGNLAttr_PCI,		// Major/Minor Opcode
  ZiGNLAttr_N
};
  
// genlmsghdr.cmd values
enum ZiGNLCmd {
  ZiGNLCmd_Unspec = 0,
  ZiGNLCmd_Forward,	// forward msg from user-space to wanic board
  ZiGNLCmd_Ack,	// user-space acks going to kernel
  ZiGNLCmd_N
};

#ifdef __cplusplus
}
#endif

#endif
