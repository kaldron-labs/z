//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private XSK redirect program and guarded interface attachment.

#ifndef ZpktBPF_HH
#define ZpktBPF_HH

#ifndef ZpktXDPLib_HH
#include <zlib/ZpktXDPLib.hh>
#endif

#include <stdint.h>
#include <linux/bpf.h>

#include <zlib/ZuUnion.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZePlatform.hh>

namespace Zpkt::Xdp {

using BPFCode = ZtArray<bpf_insn, ZtArrayHeapID<"Zpkt.BPF.Code">>;
ZpktXDPAPI ZuUnion<BPFCode, ZeException> redirectCode(int xsk);

class ZpktXDPAPI BPFProgram {
public:
  BPFProgram() = default;
  ~BPFProgram();
  BPFProgram(const BPFProgram &) = delete;
  BPFProgram &operator =(const BPFProgram &) = delete;
  ZuUnion<void, ZeException> load(unsigned queues);
  ZuUnion<void, ZeException> publish(unsigned queue, int socket);
  ZuUnion<void, ZeException> quiesce(unsigned queue);
  ZuUnion<void, ZeException> synchronize();
  void reset();
  int fd() const { return m_fd; }
  uint32_t id() const { return m_id; }
private:
  int		m_fd = -1, m_xsk = -1, m_barrier = -1;
  uint32_t	m_id = 0;
};

struct XDPInfo {
  uint32_t drvID = 0, skbID = 0, hwID = 0, progID = 0;
  bool occupied() const { return drvID || skbID || hwID || progID; }
};
ZpktXDPAPI ZuUnion<XDPInfo, ZeException> xdpQuery(
  unsigned ifindex, int fd = -1, ZeError *native = nullptr);
ZpktXDPAPI ZuUnion<void, ZeException> setXDP(
  unsigned ifindex, int fd, unsigned flags, int expected, bool *unsupported = nullptr);

class ZpktXDPAPI BPFAttach {
public:
  BPFAttach() = default;
  ~BPFAttach();
  BPFAttach(const BPFAttach &) = delete;
  BPFAttach &operator =(const BPFAttach &) = delete;
  ZuUnion<void, ZeException> attach(unsigned ifindex, const BPFProgram &);
  ZuUnion<void, ZeException> detach();
  unsigned ifindex() const { return m_ifindex; }
  uint32_t mode() const { return m_mode; }
  uint32_t id() const { return m_id; }
private:
  void reset();
  unsigned	m_ifindex = 0;
  uint32_t	m_mode = 0, m_id = 0;
  int		m_fd = -1;
};

} // namespace Zpkt::Xdp
#endif /* ZpktBPF_HH */
