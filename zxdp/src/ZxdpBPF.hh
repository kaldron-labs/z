//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private mask classifier and guarded interface attachment.

#ifndef ZxdpBPF_HH
#define ZxdpBPF_HH

#ifndef ZxdpLib_HH
#include <zlib/ZxdpLib.hh>
#endif
#include <stdint.h>
#include <linux/bpf.h>
#include <zlib/ZuSpan.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

namespace Zxdp {

struct Filter {
  uint32_t address[4]{};
  uint32_t mask[4]{};
  unsigned family = 0; // zero selects all interface traffic
};
using BPFCode = ZtArray<bpf_insn, ZtArrayHeapID<"Zxdp.BPF.Code">>;
ZxdpAPI bool parseFilter(ZuCSpan, Filter &, ZtString<> &error);
ZxdpAPI bool classifierCode(int xsk, const Filter &, BPFCode &, ZtString<> &error);

class ZxdpAPI BPFProgram {
public:
  BPFProgram() = default;
  ~BPFProgram();
  BPFProgram(const BPFProgram &) = delete;
  BPFProgram &operator =(const BPFProgram &) = delete;
  bool load(unsigned queues, const Filter &, ZtString<> &error);
  bool publish(unsigned queue, int socket, ZtString<> &error);
  bool quiesce(unsigned queue, ZtString<> &error);
  void reset();
  int fd() const { return m_fd; }
  uint32_t id() const { return m_id; }
private:
  int m_fd = -1, m_xsk = -1;
  uint32_t m_id = 0;
};

struct LinkState {
  unsigned ifindex = 0, mtu = 0, rxQueues = 0;
  uint8_t mac[6]{};
  uint32_t drvID = 0, skbID = 0, hwID = 0, progID = 0;
  bool occupied() const { return drvID || skbID || hwID || progID; }
};
ZxdpAPI bool queryLink(unsigned ifindex, LinkState &, ZtString<> &error);
ZxdpAPI bool setXDP(unsigned ifindex, int fd, unsigned flags, int expected,
  ZtString<> &error, bool *unsupported = nullptr);

class ZxdpAPI BPFAttach {
public:
  BPFAttach() = default;
  ~BPFAttach();
  BPFAttach(const BPFAttach &) = delete;
  BPFAttach &operator =(const BPFAttach &) = delete;
  bool attach(unsigned ifindex, const BPFProgram &, ZtString<> &error);
  bool detach(ZtString<> &error);
  unsigned ifindex() const { return m_ifindex; }
  uint32_t mode() const { return m_mode; }
  uint32_t id() const { return m_id; }
private:
  void reset();
  unsigned m_ifindex = 0;
  uint32_t m_mode = 0, m_id = 0;
  int m_fd = -1;
};

} // namespace Zxdp
#endif /* ZxdpBPF_HH */
