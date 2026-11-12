//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/if_link.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <zlib/Zu_aton.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZxdpBPF.hh>

namespace Zxdp {

static int bpfCall(unsigned command, bpf_attr &attr)
{
  int result;
  do result = syscall(__NR_bpf, command, &attr, sizeof(attr));
  while (result < 0 && errno == EINTR);
  return result;
}

static bool failed(ZtString<> &error, const char *operation)
{
  auto errorNo = errno;
  error = operation;
  error << ": " << strerror(errorNo);
  return false;
}

bool parseFilter(ZuCSpan text, Filter &out, ZtString<> &error)
{
  error = {};
  if (!text.length()) { out = {}; return true; }
  auto slash = static_cast<const char *>(memchr(text.data(), '/', text.length()));
  auto n = slash ? unsigned(slash - text.data()) : unsigned(text.length());
  // inet_pton needs a terminated address; its protocol bounds this scratch.
  char address[INET6_ADDRSTRLEN];
  if (!n || n >= sizeof(address)) { error = "invalid network mask address"; return false; }
  memcpy(address, text.data(), n);
  address[n] = 0;
  Filter result;
  if (inet_pton(AF_INET, address, result.address) == 1) result.family = 4;
  else if (inet_pton(AF_INET6, address, result.address) == 1) result.family = 6;
  else { error = "invalid network mask address"; return false; }
  unsigned bits = result.family == 4 ? 32 : 128;
  if (slash) {
    auto length = unsigned(text.length()) - n - 1;
    unsigned prefix;
    if (!length || length > 3 || Zu_nscan<ZuFmt::Default>::atou(prefix, slash + 1, length) != length ||
	prefix > bits) { error = "invalid network mask prefix"; return false; }
    bits = prefix;
  }
  auto words = result.family == 4 ? 1U : 4U;
  for (unsigned i = 0; i < words; ++i) {
    auto width = bits > 32 ? 32 : bits;
    result.mask[i] = htonl(!width ? 0 : width == 32 ? UINT32_MAX : UINT32_MAX<<(32 - width));
    result.address[i] &= result.mask[i];
    bits -= width;
  }
  out = result;
  return true;
}

class Builder {
public:
  unsigned label() {
    auto id = m_labels.length();
    m_labels.push(-1);
    return id;
  }
  void mark(unsigned label) {
    if (!m_valid) return;
    if (label >= m_labels.length() || m_labels[label] == Marked) {
      m_valid = false;
      return;
    }
    auto index = m_labels[label];
    auto end = m_code.length();
    while (index >= 0) {
      auto &insn = m_code[index];
      auto next = insn.off;
      insn.off = end - unsigned(index) - 1;
      index = next;
    }
    m_labels[label] = Marked;
  }
  void emit(uint8_t code, unsigned dst = 0, unsigned src = 0, int off = 0, int imm = 0) {
    if (m_code.length() == BPF_MAXINSNS) { m_valid = false; return; }
    bpf_insn insn;
    insn.code = code;
    insn.dst_reg = dst;
    insn.src_reg = src;
    insn.off = off;
    insn.imm = imm;
    m_code.push(insn);
  }
  void jump(uint8_t op, unsigned dst, unsigned src, int imm, unsigned label) {
    if (label >= m_labels.length() || m_labels[label] == Marked) {
      m_valid = false;
      return;
    }
    auto prev = m_labels[label];
    m_labels[label] = m_code.length();
    emit(BPF_JMP | op, dst, src, prev, imm);
  }
  void mov(unsigned dst, unsigned src) { emit(BPF_ALU64 | BPF_MOV | BPF_X, dst, src); }
  void imm(unsigned dst, int value) { emit(BPF_ALU64 | BPF_MOV | BPF_K, dst, 0, 0, value); }
  void add(unsigned dst, int value) { emit(BPF_ALU64 | BPF_ADD | BPF_K, dst, 0, 0, value); }
  void load(unsigned size, unsigned dst, unsigned src, int off) {
    emit(BPF_LDX | BPF_MEM | size, dst, src, off);
  }
  void be16(unsigned reg) { emit(BPF_ALU | BPF_END | BPF_FROM_BE, reg, 0, 0, 16); }
  void map(unsigned dst, int fd) {
    emit(BPF_LD | BPF_DW | BPF_IMM, dst, BPF_PSEUDO_MAP_FD, 0, fd);
    emit(0);
  }
  void call(int fn) { emit(BPF_JMP | BPF_CALL, 0, 0, 0, fn); }
  void exit() { emit(BPF_JMP | BPF_EXIT); }
  void bound(unsigned cursor, unsigned bytes, unsigned pass) {
    mov(1, cursor);
    add(1, bytes);
    jump(BPF_JGT | BPF_X, 1, 7, 0, pass);
  }
  bool finish(BPFCode &out, ZtString<> &error) {
    for (auto label : m_labels) if (label != Marked) { m_valid = false; break; }
    if (!m_valid) { error = "invalid or overflowing XDP instruction program"; return false; }
    out = ZuMv(m_code);
    return true;
  }

private:
  // Linux's instruction limit also bounds chain indices and patched offsets.
  ZuAssert(BPF_MAXINSNS <= SHRT_MAX);
  enum { Marked = -2 };
  using Labels = ZtArray<int, ZtArrayHeapID<"Zxdp.BPF.Labels">>;
  BPFCode	m_code;
  Labels	m_labels;
  bool		m_valid = true;
};

bool classifierCode(int xsk, const Filter &filter, BPFCode &out, ZtString<> &error)
{
  error = {};
  if (xsk < 0) { error = "invalid XSK map descriptor"; return false; }
  Builder b;
  auto pass = b.label();
  b.mov(9, 1);
  if (filter.family) {
    b.load(BPF_W, 6, 9, offsetof(xdp_md, data));
    b.load(BPF_W, 7, 9, offsetof(xdp_md, data_end));
    b.bound(6, 14, pass);
    b.load(BPF_H, 2, 6, 12);
    b.be16(2);
    b.mov(8, 6);
    b.add(8, 14);
    // Ethernet permits stacked service/customer VLAN headers.
    for (unsigned i = 0; i < 2; ++i) {
      auto tag = b.label(), next = b.label();
      b.jump(BPF_JEQ | BPF_K, 2, 0, 0x8100, tag);
      b.jump(BPF_JNE | BPF_K, 2, 0, 0x88a8, next);
      b.mark(tag);
      b.bound(8, 4, pass);
      b.load(BPF_H, 2, 8, 2);
      b.be16(2);
      b.add(8, 4);
      b.mark(next);
    }
    b.jump(BPF_JNE | BPF_K, 2, 0, filter.family == 4 ? 0x0800 : 0x86dd, pass);
    b.bound(8, filter.family == 4 ? 20 : 40, pass);
    auto words = filter.family == 4 ? 1U : 4U;
    for (unsigned i = 0; i < words; ++i) {
      if (!filter.mask[i]) continue;
      b.load(BPF_W, 2, 8, (filter.family == 4 ? 16 : 24) + (i<<2));
      b.emit(BPF_ALU | BPF_AND | BPF_K, 2, 0, 0, filter.mask[i]);
      b.emit(BPF_ALU | BPF_MOV | BPF_K, 3, 0, 0, filter.address[i]);
      b.jump(BPF_JNE | BPF_X, 2, 3, 0, pass);
    }
  }
  b.load(BPF_W, 2, 9, offsetof(xdp_md, rx_queue_index));
  b.map(1, xsk);
  b.imm(3, XDP_DROP);
  b.call(BPF_FUNC_redirect_map);
  b.exit();
  b.mark(pass);
  if (filter.family) { b.imm(0, XDP_PASS); b.exit(); }
  return b.finish(out, error);
}

BPFProgram::~BPFProgram() { reset(); }
void BPFProgram::reset()
{
  if (m_fd >= 0) { close(m_fd); m_fd = -1; }
  if (m_xsk >= 0) { close(m_xsk); m_xsk = -1; }
  m_id = 0;
}

bool BPFProgram::load(unsigned queues, const Filter &filter, ZtString<> &error)
{
  error = {};
  if (m_fd >= 0 || !queues) { error = "invalid XDP program configuration"; return false; }
  auto fail = [this, &error](const char *operation) {
    failed(error, operation);
    reset();
    return false;
  };
  bpf_attr attr{};
  attr.map_type = BPF_MAP_TYPE_XSKMAP;
  attr.key_size = attr.value_size = sizeof(uint32_t);
  attr.max_entries = queues;
  m_xsk = bpfCall(BPF_MAP_CREATE, attr);
  if (m_xsk < 0) return fail("XSK map create");
  BPFCode code;
  if (!classifierCode(m_xsk, filter, code, error)) { reset(); return false; }
  // Bounded cold-load diagnostics, independent of packet service storage.
  enum { VerifierLogSize = 128<<10 };
  ZtArray<char, ZtArrayHeapID<"Zxdp.Verifier">> log;
  log.length(VerifierLogSize);
  log[0] = 0; // errors before verifier execution produce no log
  attr = {};
  attr.prog_type = BPF_PROG_TYPE_XDP;
  attr.expected_attach_type = BPF_XDP;
  attr.insn_cnt = code.length();
  attr.insns = uintptr_t(code.data());
  attr.license = uintptr_t("MIT");
  attr.log_level = 1;
  attr.log_size = log.length();
  attr.log_buf = uintptr_t(log.data());
  memcpy(attr.prog_name, "zxdp_mask", sizeof("zxdp_mask"));
  m_fd = bpfCall(BPF_PROG_LOAD, attr);
  if (m_fd < 0) {
    failed(error, "XDP program load");
    error << '\n' << ZuCSpan{log.data(), strnlen(log.data(), log.length())};
    reset();
    return false;
  }
  bpf_prog_info info{};
  attr = {};
  attr.info.bpf_fd = m_fd;
  attr.info.info_len = sizeof(info);
  attr.info.info = uintptr_t(&info);
  if (bpfCall(BPF_OBJ_GET_INFO_BY_FD, attr) < 0) return fail("XDP program identity");
  m_id = info.id;
  return true;
}

bool BPFProgram::publish(unsigned queue, int socket, ZtString<> &error)
{
  bpf_attr attr{};
  attr.map_fd = m_xsk;
  attr.key = uintptr_t(&queue);
  attr.value = uintptr_t(&socket);
  attr.flags = BPF_NOEXIST;
  if (bpfCall(BPF_MAP_UPDATE_ELEM, attr) < 0) return failed(error, "publish XSK destination");
  error = {};
  return true;
}

bool BPFProgram::quiesce(unsigned queue, ZtString<> &error)
{
  bpf_attr attr{};
  attr.map_fd = m_xsk;
  attr.key = uintptr_t(&queue);
  if (bpfCall(BPF_MAP_DELETE_ELEM, attr) < 0 && errno != ENOENT)
    return failed(error, "remove XSK destination");
  error = {};
  return true;
}

// Each transaction has its own unicast socket/sequence. No global event socket
// or kernel-interface shadow table is maintained by packet I/O.
struct LinkRequest {
  // Outer XDP nest plus FD, flags and expected-FD attributes.
  enum { AttrSize = RTA_SPACE(0) + 3 * RTA_SPACE(sizeof(uint32_t)) };
  nlmsghdr	header{};
  ifinfomsg	info{};
  uint8_t	attrs[AttrSize];

  void add(unsigned type, const void *data, unsigned length) {
    unsigned offset = NLMSG_ALIGN(header.nlmsg_len);
    unsigned extent = RTA_LENGTH(length);
    ZmAssert_(offset + RTA_ALIGN(extent) <= sizeof(LinkRequest));
    auto attr = reinterpret_cast<rtattr *>(reinterpret_cast<uint8_t *>(this) + offset);
    attr->rta_type = type;
    attr->rta_len = extent;
    if (length) memcpy(RTA_DATA(attr), data, length);
    header.nlmsg_len = offset + RTA_ALIGN(extent);
  }
};

static bool malformed(ZtString<> &error)
{
  error = "malformed or incomplete rtnetlink link response";
  errno = EPROTO;
  return false;
}

static bool parseLink(const nlmsghdr *header, LinkState &out, ZtString<> &error)
{
  if (header->nlmsg_len < NLMSG_LENGTH(sizeof(ifinfomsg))) return malformed(error);
  auto info = static_cast<const ifinfomsg *>(NLMSG_DATA(header));
  if (info->ifi_type != ARPHRD_ETHER) {
    error = "XDP packet interface must provide Ethernet framing";
    errno = EAFNOSUPPORT;
    return false;
  }
  LinkState state;
  state.ifindex = info->ifi_index;
  bool mac = false;
  int length = IFLA_PAYLOAD(header);
  auto attr = IFLA_RTA(info);
  for (; RTA_OK(attr, length); attr = RTA_NEXT(attr, length)) {
    auto type = attr->rta_type & NLA_TYPE_MASK;
    auto value = static_cast<const uint8_t *>(RTA_DATA(attr));
    auto size = RTA_PAYLOAD(attr);
    switch (type) {
      case IFLA_MTU:
      case IFLA_NUM_RX_QUEUES:
	if (size != 4) return malformed(error);
	memcpy(type == IFLA_MTU ? &state.mtu : &state.rxQueues, value, 4);
	break;
      case IFLA_ADDRESS:
	if (size != 6) return malformed(error);
	memcpy(state.mac, value, 6);
	mac = true;
	break;
      case IFLA_XDP: {
	int remaining = size;
	auto xdp = reinterpret_cast<const rtattr *>(value);
	for (; RTA_OK(xdp, remaining); xdp = RTA_NEXT(xdp, remaining)) {
	  uint32_t *id = nullptr;
	  switch (xdp->rta_type & NLA_TYPE_MASK) {
	    case IFLA_XDP_DRV_PROG_ID: id = &state.drvID; break;
	    case IFLA_XDP_SKB_PROG_ID: id = &state.skbID; break;
	    case IFLA_XDP_HW_PROG_ID: id = &state.hwID; break;
	    case IFLA_XDP_PROG_ID: id = &state.progID; break;
	  }
	  if (id) {
	    if (RTA_PAYLOAD(xdp) != 4) return malformed(error);
	    memcpy(id, RTA_DATA(xdp), 4);
	  }
	}
	if (remaining) return malformed(error);
      } break;
    }
  }
  if (length || !mac || !state.mtu || !state.rxQueues) return malformed(error);
  out = state;
  return true;
}

static bool transact(
  LinkRequest &request, LinkState *state, ZtString<> &error, bool *rejected = nullptr)
{
  if (rejected) *rejected = false;
  int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (fd < 0) return failed(error, "rtnetlink socket");
  auto fail = [fd, &error](const char *operation) {
    auto errorNo = errno;
    failed(error, operation);
    close(fd);
    errno = errorNo;
    return false;
  };
  // Setup is a control-path transaction, never a packet-service wait. Bound a
  // missing kernel response so failed startup remains diagnosable.
  timeval timeout{2, 0};
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    return fail("rtnetlink receive timeout");
  sockaddr_nl peer{};
  peer.nl_family = AF_NETLINK;
  if (sendto(fd, &request, request.header.nlmsg_len, 0,
      reinterpret_cast<sockaddr *>(&peer), sizeof(peer)) != request.header.nlmsg_len)
    return fail("rtnetlink request");
  ZtArray<uint8_t, ZtArrayHeapID<"Zxdp.Netlink">> buffer;
  for (;;) {
    // Query the next datagram's exact extent. Link attributes have no fixed
    // userspace maximum; retain no arbitrary response-capacity limit.
    auto extent = recv(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC);
    if (extent < 0) {
      if (errno == EINTR) continue;
      return fail("rtnetlink response length");
    }
    if (!extent) { close(fd); return malformed(error); }
    buffer.length(extent);
    iovec iov{buffer.data(), size_t(buffer.length())};
    msghdr message{};
    message.msg_name = &peer;
    message.msg_namelen = sizeof(peer);
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    auto n = recvmsg(fd, &message, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return fail("rtnetlink response");
    }
    if (!n || (message.msg_flags & MSG_TRUNC) || peer.nl_pid) {
      close(fd);
      return malformed(error);
    }
    int remaining = n;
    auto header = reinterpret_cast<const nlmsghdr *>(buffer.data());
    for (; NLMSG_OK(header, remaining); header = NLMSG_NEXT(header, remaining)) {
      if (header->nlmsg_seq != request.header.nlmsg_seq) continue;
      if (header->nlmsg_type == NLMSG_ERROR) {
	if (header->nlmsg_len < NLMSG_LENGTH(sizeof(nlmsgerr))) {
	  close(fd);
	  return malformed(error);
	}
	auto result = static_cast<const nlmsgerr *>(NLMSG_DATA(header))->error;
	if (result) {
	  if (rejected) *rejected = true;
	  errno = -result;
	  return fail("rtnetlink link operation");
	}
	if (!state) { close(fd); error = {}; return true; }
      } else if (state && header->nlmsg_type == RTM_NEWLINK) {
	bool result = parseLink(header, *state, error);
	close(fd);
	return result;
      }
    }
    if (remaining) { close(fd); return malformed(error); }
  }
}

bool queryLink(unsigned ifindex, LinkState &out, ZtString<> &error)
{
  if (!ifindex || ifindex > INT_MAX) {
    error = "invalid packet interface index";
    errno = EINVAL;
    return false;
  }
  LinkRequest request;
  request.header.nlmsg_len = NLMSG_LENGTH(sizeof(ifinfomsg));
  request.header.nlmsg_type = RTM_GETLINK;
  request.header.nlmsg_flags = NLM_F_REQUEST;
  request.header.nlmsg_seq = 1;
  request.info.ifi_family = AF_UNSPEC;
  request.info.ifi_index = ifindex;
  return transact(request, &out, error);
}

bool setXDP(
  unsigned ifindex, int fd, unsigned flags, int expected, ZtString<> &error,
  bool *unsupported)
{
  if (unsupported) *unsupported = false;
  bool initial = flags & XDP_FLAGS_UPDATE_IF_NOEXIST;
  bool replace = flags & XDP_FLAGS_REPLACE;
  if (!ifindex || ifindex > INT_MAX ||
      (expected < 0 ? (!initial || replace || fd < 0) : (!replace || initial))) {
    error = "XDP attachment requires no-replace or expected program ownership";
    errno = EINVAL;
    return false;
  }
  LinkRequest request;
  request.header.nlmsg_len = NLMSG_LENGTH(sizeof(ifinfomsg));
  request.header.nlmsg_type = RTM_SETLINK;
  request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  request.header.nlmsg_seq = 1;
  request.info.ifi_family = AF_UNSPEC;
  request.info.ifi_index = ifindex;
  auto offset = request.header.nlmsg_len;
  request.add(IFLA_XDP | NLA_F_NESTED, nullptr, 0);
  request.add(IFLA_XDP_FD, &fd, sizeof(fd));
  request.add(IFLA_XDP_FLAGS, &flags, sizeof(flags));
  if (expected >= 0) request.add(IFLA_XDP_EXPECTED_FD, &expected, sizeof(expected));
  auto outer = reinterpret_cast<rtattr *>(reinterpret_cast<uint8_t *>(&request) + offset);
  outer->rta_len = request.header.nlmsg_len - offset;
  bool rejected = false;
  bool result = transact(request, nullptr, error, &rejected);
  if (unsupported) *unsupported = !result && rejected && errno == EOPNOTSUPP;
  return result;
}

BPFAttach::~BPFAttach() { ZmAssert_(!m_ifindex && m_fd < 0); }

void BPFAttach::reset()
{
  if (m_fd >= 0) close(m_fd);
  m_fd = -1;
  m_ifindex = m_mode = m_id = 0;
}

bool BPFAttach::attach(unsigned ifindex, const BPFProgram &program, ZtString<> &error)
{
  if (m_ifindex || program.fd() < 0 || !program.id()) {
    error = "invalid or already active XDP attachment";
    return false;
  }
  LinkState state;
  if (!queryLink(ifindex, state, error)) return false;
  if (state.occupied()) {
    error = "packet interface already has an unrelated XDP program";
    return false;
  }
  m_fd = fcntl(program.fd(), F_DUPFD_CLOEXEC, 0);
  if (m_fd < 0) return failed(error, "retain XDP program FD");
  // Record identity before sending: a lost acknowledgment does not establish
  // that the kernel failed to attach. Resolve every uncertain outcome using
  // identity checks and expected-FD retirement before releasing the program FD.
  m_ifindex = ifindex;
  m_mode = XDP_FLAGS_DRV_MODE;
  m_id = program.id();
  auto rollback = [this, &error] {
    auto cause = ZuMv(error);
    ZtString<> cleanup;
    if (!detach(cleanup) && m_ifindex) cause << "; XDP rollback pending: " << cleanup;
    error = ZuMv(cause);
    return false;
  };
  bool unsupported = false;
  if (!setXDP(ifindex, m_fd, m_mode | XDP_FLAGS_UPDATE_IF_NOEXIST, -1, error, &unsupported)) {
    if (!unsupported) return rollback();
    m_mode = XDP_FLAGS_SKB_MODE;
    if (!setXDP(ifindex, m_fd, m_mode | XDP_FLAGS_UPDATE_IF_NOEXIST, -1, error))
      return rollback();
  }
  if (!queryLink(ifindex, state, error)) return rollback();
  auto id = m_mode == XDP_FLAGS_DRV_MODE ? state.drvID : state.skbID;
  if (id != m_id) {
    error = "XDP attachment identity changed during setup";
    reset();
    return false;
  }
  return true;
}

bool BPFAttach::detach(ZtString<> &error)
{
  if (!m_ifindex) { error = {}; return true; }
  LinkState state;
  if (!queryLink(m_ifindex, state, error)) {
    if (errno == ENODEV) { reset(); error = {}; return true; }
    return false;
  }
  auto id = m_mode == XDP_FLAGS_DRV_MODE ? state.drvID : state.skbID;
  if (id != m_id) {
    error = "XDP attachment ownership lost; foreign program preserved";
    reset();
    return false;
  }
  if (!setXDP(m_ifindex, -1, m_mode | XDP_FLAGS_REPLACE, m_fd, error)) {
    auto saved = ZuMv(error);
    if (queryLink(m_ifindex, state, error)) {
      id = m_mode == XDP_FLAGS_DRV_MODE ? state.drvID : state.skbID;
      if (id != m_id) reset();
    } else if (errno == ENODEV) reset();
    error = ZuMv(saved);
    return false;
  }
  reset();
  return true;
}

} // namespace Zxdp
