//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <limits.h>
#include <linux/if_ether.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmGuard.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include "ZpktAfpNif.hh"

namespace Zpkt::Afp {

static ZeException deviceError(const char *operation, int err)
{
  return ZeEXCEPT(Error, "Zpkt.Afp", ([operation, err](auto &s) {
    s << operation << ": " << ZeError{err};
  }));
}

Nif::Nif(ZiMultiplex *mx, const NifCf &config, const NifInfo &info) :
  Zpkt::Nif{mx, info}, m_config{config}
{
  ZmAssert_(mx && mx->state() == ZmEngineState::Stopped);
  if (!ifindex() || ifindex() > INT_MAX || !mtu())
    throw deviceError("invalid AF_PACKET interface geometry", EINVAL);
  if (int err = openSocket()) throw deviceError("open dormant AF_PACKET socket", err);
  m_ingressFD = openIngress(ifindex());
  if (m_ingressFD < 0) {
    ::close(m_fd);
    throw deviceError("claim AF_PACKET nftables ingress", -m_ingressFD);
  }
  rxQueues(1);
}

Nif::~Nif()
{
  ZmAssert_(state() == ZmEngineState::Stopped);
  if (mx()) final();
  ZmAssert_(!mx());
}

int Nif::openSocket()
{
  ZmAssert_(m_fd < 0);
  m_fd = ::socket(AF_PACKET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (m_fd < 0) return errno;
  if (int err = bindSocket(0)) {
    ::close(m_fd);
    m_fd = -1;
    return err;
  }
  return 0;
}

int Nif::bindSocket(unsigned protocol)
{
  sockaddr_ll addr{};
  addr.sll_family = AF_PACKET;
  addr.sll_ifindex = int(ifindex());
  addr.sll_protocol = htons(uint16_t(protocol));
  return ::bind(m_fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) < 0 ?
    errno : 0;
}

ZmRef<Zi::IOBufPool> Nif::bufPool(
  Zi::IOBufType::T type, unsigned count, unsigned size,
  unsigned headRoom, unsigned tailRoom, unsigned align)
{
  return new BufPool{type, count, size, headRoom, tailRoom, align};
}

void Nif::reserveTxPools(ZuSpan<Zi::IOBufPool *const> pools)
{
  ZmAssert_(mx() && state() == ZmEngineState::Stopped && !allocated());
  m_reserves.null();
  for (auto pool : pools) {
    auto native = dynamic_cast<BufPool *>(pool);
    ZmAssert_(native && (native->type == Zi::IOBufType::TxTCP ||
      native->type == Zi::IOBufType::TxUDP));
    m_reserves.push(ZmRef<BufPool>{native});
  }
}

Nif::Result Nif::alloc(Zi::IOBufPool *rxPool, const Zi::IOBufPoolList &txPools)
{
  if (!mx() || state() != ZmEngineState::Stopped)
    return deviceError("initialize and stop AFP before allocation", EBUSY);
  bool hadPools = allocated();
  auto result = m_pools.alloc(rxPool, txPools, m_reserves, mtu());
  if (!hadPools && result.is<void>()) m_pools.rx()->returned([this] { returned(); });
  return result;
}

void Nif::final()
{
  ZmAssert_(state() == ZmEngineState::Stopped);
  ZmGuard guard(m_observe);
  if (!mx()) return;
  if (!m_pools.final()) {
    error(deviceError("AFP packet allocations still held by caller", EBUSY));
    return;
  }
  m_reserves.null();
  if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
  ZmAssert_(!(uint64_t(m_gate) & (Ready | Armed | FinishWaiting | WriterMask)) && wakeFD() < 0);
  ::close(m_ingressFD); // releases the socket-owned nftables table and ingress hook
  m_ingressFD = -1;
  resetInfo();
  error({});
  final_();
}

void Nif::start_()
{
  error({});
  if (mx() && mx()->running() && allocated() && receiving()) {
    auto result = activate();
    if (result.is<void>()) { Engine::started(true); return; }
    error(ZuMv(result).p<ZeException>());
  } else
    error(deviceError("start requires allocated AFP pools, a running mux and Rx callback", EINVAL));
  closeRun();
  clearCallbacks();
  Engine::started(false);
}

Nif::Result Nif::activate()
{
  ZmGuard guard(m_observe);
  if (m_fd < 0)
    if (int err = openSocket()) return deviceError("reopen dormant AF_PACKET socket", err);
  int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (fd < 0) return deviceError("create AFP run eventfd", errno);
  wakeFD(fd);

  int one = 1;
  if (::setsockopt(m_fd, SOL_PACKET, PACKET_AUXDATA, &one, sizeof(one)) < 0)
    return deviceError("enable PACKET_AUXDATA", errno);
#ifdef PACKET_IGNORE_OUTGOING
  if (::setsockopt(m_fd, SOL_PACKET, PACKET_IGNORE_OUTGOING, &one, sizeof(one)) < 0 &&
      errno != ENOPROTOOPT && errno != EOPNOTSUPP)
    return deviceError("enable PACKET_IGNORE_OUTGOING", errno);
#endif
  unsigned bypass = BypassOff;
  if (m_config.qdiscBypass) {
    if (::setsockopt(m_fd, SOL_PACKET, PACKET_QDISC_BYPASS, &one, sizeof(one)) == 0)
      bypass = BypassOn;
    else if (errno == ENOPROTOOPT)
      bypass = BypassUnsupported;
    else
      return deviceError("enable PACKET_QDISC_BYPASS", errno);
  }

  for (unsigned i = 0; i < RxBurst; ++i) {
    auto &msg = m_rx.msg[i].msg_hdr;
    msg.msg_name = &m_rx.addr[i];
    msg.msg_iov = &m_rx.iov[i];
    msg.msg_iovlen = 1;
    msg.msg_control = m_rx.control[i].data;
  }
  for (unsigned i = 0; i < MaxTxBatch; ++i) {
    m_tx.msg[i].msg_hdr.msg_iov = &m_tx.iov[i];
    m_tx.msg[i].msg_hdr.msg_iovlen = 1;
  }
  if (int err = bindSocket(ETH_P_ALL)) return deviceError("activate AF_PACKET bind", err);

  ZmAssert_(!m_pending);
  resetWake();
  m_failure = 0;
  m_errorRecorded = false;
  m_rxPackets = m_rxBytes = m_rxDiscard = 0;
  m_txAccepted = m_txBytes = m_txRetired = m_txRetiredBytes = 0;
  m_drops = 0;
  m_bypass = bypass;
  m_gate = ((m_gate.load_() & RunMask) + RunStep) | Ready;
  return {};
}

void Nif::closeRun()
{
  ZmGuard guard(m_observe);
  if (wakeFD() >= 0) { ackWake(); closeWake(); }
  if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
}

void Nif::stop_()
{
  closeAdmission();
  // Engine-owned until finishStop, including when admission is already closed
  // by a dataplane error or the Rx callback requested this stop.
  signal();
  mx()->rxRun([this] { mx()->txRun([this] { finishTx(); }); });
}

void Nif::finishTx()
{
  pollTx();
  finishCallbacks();
  mx()->rxRun([this] { finishStop(); });
}

void Nif::finishStop()
{
  uint64_t gate = m_gate;
  while (gate & WriterMask) {
    // Closing admission prevents new writers. Give the last existing writer
    // responsibility for scheduling this continuation without polling.
    uint64_t observed = m_gate.cmpXch(gate | FinishWaiting, gate);
    if (observed == gate) return;
    gate = observed;
  }
  m_gate.xchAnd(~uint64_t(FinishWaiting));
  recordFailure();
  closeRun();
  Engine::stopped(!failed());
}

void Nif::recordFailure()
{
  uint64_t value = m_failure;
  if (!value || m_errorRecorded) return;
  m_errorRecorded = true;
  unsigned op = unsigned(value >> 32);
  error(deviceError(op == Receive ? "recvmmsg" : op == Transmit ? "sendmmsg" : "poll",
    int(uint32_t(value))));
}

void Nif::serviceFailure()
{
  if (!uint64_t(m_failure)) return;
  recordFailure();
  if (!stopping() && state() != ZmEngineState::Stopped) stop(StartFn{});
}

void Nif::failure(unsigned operation, int err)
{
  if (m_failure.cmpXch((uint64_t(operation) << 32) | unsigned(err), 0)) return;
  bool writer = admitWriter(false);
  closeAdmission();
  if (writer) { signal(); releaseWriter(); }
  mx()->rxRun([this] { serviceFailure(); });
}

bool Nif::admitWriter(bool replenish)
{
  // Synchronize pool publication with armReturn even when no arm is
  // present. If this exchange runs first, the armer sees the free slot.
  uint64_t gate = replenish ? m_gate.xchAdd(0) : uint64_t(m_gate);
  uint64_t run = gate & RunMask;
  uint64_t required = replenish ? uint64_t(Ready | Armed) : uint64_t(Ready);
  while ((gate & required) == required && (gate & RunMask) == run) {
    ZmAssert_((gate & WriterMask) != WriterMask);
    uint64_t next = gate + Writer;
    if (replenish) next &= ~uint64_t(Armed);
    uint64_t observed = m_gate.cmpXch(next, gate);
    if (observed == gate) return true;
    gate = observed;
  }
  return false;
}

void Nif::releaseWriter()
{
  auto previous = m_gate.xchSub(Writer);
  if ((previous & (WriterMask | FinishWaiting)) == (Writer | FinishWaiting))
    mx()->rxRun([this] { finishStop(); });
}

void Nif::closeAdmission() { m_gate.xchAnd(~uint64_t(Ready | Armed)); }

void Nif::wake()
{
  if (!admitWriter(false)) return;
  Zpkt::Nif::wake();
  releaseWriter();
}

void Nif::signal()
{
  uint64_t one = 1;
  while (::write(wakeFD(), &one, sizeof(one)) < 0) {
    if (errno == EINTR) continue;
    ZmAssert_(errno == EAGAIN);
    break;
  }
}

void Nif::returned()
{
  if (!admitWriter(true)) return;
  signal();
  releaseWriter();
}

void Nif::armReturn()
{
  uint64_t gate = m_gate;
  while (gate & Ready) {
    uint64_t observed = m_gate.cmpXch(gate | Armed, gate);
    if (observed == gate) return;
    gate = observed;
  }
}

void Nif::waitRx(bool depleted)
{
  startBlocking();
  if (depleted) armReturn();
  auto rxPool = m_pools.rx();
  constexpr short bad = POLLERR | POLLHUP | POLLNVAL;
  while (ready() && !wakePending() && (!depleted || !rxPool->hasFree())) {
    pollfd fds[] = {{wakeFD(), POLLIN, 0}, {m_fd, POLLIN, 0}};
    int count = ::poll(fds, depleted ? 1 : 2, -1);
    if (count < 0) {
      if (errno == EINTR) continue;
      failure(Wait, errno);
    } else {
      auto events = fds[0].revents | fds[1].revents;
      if (events & bad) failure(Wait, events & POLLNVAL ? EBADF : EIO);
      if (fds[0].revents & POLLIN) ackWake();
    }
    break;
  }
  if (depleted) m_gate.xchAnd(~uint64_t(Armed));
  stopBlocking();
}

static unsigned etherType(const uint8_t *data)
{
  return (unsigned(data[0])<<8) | data[1];
}

bool Nif::wireLength(const uint8_t *data, unsigned length) const
{
  if (length < ETH_HLEN) return false;
  unsigned header = ETH_HLEN;
  auto type = etherType(data + header - 2);
  while (type == ETH_P_8021Q || type == ETH_P_8021AD) {
    if (length - header < 4) return false;
    header += 4;
    type = etherType(data + header - 2);
  }
  return length - header <= mtu();
}

unsigned Nif::rxLength(unsigned i) const
{
  const auto &msg = m_rx.msg[i];
  const auto &header = msg.msg_hdr;
  const auto &addr = m_rx.addr[i];
  auto data = static_cast<uint8_t *>(m_rx.iov[i].iov_base);
  unsigned length = msg.msg_len;
  unsigned capacity = m_pools.rx()->size;
  if (header.msg_flags & (MSG_TRUNC | MSG_CTRUNC) ||
      header.msg_namelen < offsetof(sockaddr_ll, sll_addr) ||
      addr.sll_pkttype == PACKET_OUTGOING || length < ETH_HLEN || length > capacity ||
      header.msg_controllen < CMSG_LEN(sizeof(tpacket_auxdata)) ||
      header.msg_controllen > sizeof(m_rx.control[i].data))
    return 0;

  // With PACKET_AUXDATA the packet socket supplies a single SOL_PACKET record.
  auto cmsg = CMSG_FIRSTHDR(&header);
  if (!cmsg || cmsg->cmsg_level != SOL_PACKET || cmsg->cmsg_type != PACKET_AUXDATA ||
      cmsg->cmsg_len < CMSG_LEN(sizeof(tpacket_auxdata)) ||
      cmsg->cmsg_len > header.msg_controllen)
    return 0;
  tpacket_auxdata aux;
  memcpy(&aux, CMSG_DATA(cmsg), sizeof(aux));
  if (aux.tp_snaplen < aux.tp_len) return 0;
  if (aux.tp_status & TP_STATUS_VLAN_VALID) {
    if (capacity - length < 4) return 0;
    auto tpid = (aux.tp_status & TP_STATUS_VLAN_TPID_VALID) ? aux.tp_vlan_tpid : ETH_P_8021Q;
    memmove(data + 16, data + 12, length - 12);
    data[12] = uint8_t(tpid>>8);
    data[13] = uint8_t(tpid);
    data[14] = uint8_t(aux.tp_vlan_tci>>8);
    data[15] = uint8_t(aux.tp_vlan_tci);
    length += 4;
  }
  return wireLength(data, length) ? length : 0;
}

unsigned Nif::receive()
{
  auto pool = m_pools.rx();
  unsigned limit = mx()->rxQuantum();
  if (limit > RxBurst) limit = RxBurst;
  unsigned prepared = 0;
  for (; prepared < limit; ++prepared) {
    auto p = pool->alloc();
    if (!p) break;
    m_rx.iov[prepared] = {p, pool->size};
    auto &msg = m_rx.msg[prepared].msg_hdr;
    msg.msg_namelen = sizeof(sockaddr_ll);
    msg.msg_controllen = sizeof(m_rx.control[prepared].data);
    msg.msg_flags = 0;
  }
  if (!prepared) return 0;
  int received;
  do {
    received = ::recvmmsg(m_fd, m_rx.msg, prepared, MSG_DONTWAIT | MSG_TRUNC, nullptr);
  } while (received < 0 && errno == EINTR && ready() && !wakePending());

  if (received < 0) {
    int err = errno;
    if (err != EINTR && err != EAGAIN && err != EWOULDBLOCK) failure(Receive, err);
    received = 0;
  }
  ZmAssert_(unsigned(received) <= prepared);
  auto data = [this](unsigned i) { return static_cast<uint8_t *>(m_rx.iov[i].iov_base); };
  for (unsigned i = received; i < prepared; ++i) pool->discard(data(i));

  unsigned lengths[RxBurst];
  unsigned deliverable = 0;
  for (int i = 0; i < received; ++i) deliverable += bool(lengths[i] = rxLength(i));
  uint64_t delivered = 0, bytes = 0;
  ZiRxBurst burst;
  for (int i = 0; i < received; ++i) {
    if (!ready() || !lengths[i]) { pool->discard(data(i)); continue; }
    recv(data(i), lengths[i], deliverable > 1 ? &burst : nullptr);
    ++delivered;
    bytes += lengths[i];
  }
  burst.flush();
  m_rxPackets += delivered;
  m_rxBytes += bytes;
  m_rxDiscard += received - delivered;
  return received;
}

void Nif::rx()
{
  unsigned spin = m_config.rxSpin;
  while (ready()) {
    if (woken()) return;
    if (!m_pools.rx()->hasFree()) {
      waitRx(true);
      spin = m_config.rxSpin;
    } else if (receive()) {
      spin = m_config.rxSpin;
    } else if (spin) {
      --spin;
    } else {
      waitRx(false);
      spin = m_config.rxSpin;
    }
  }
}

Slot *Nif::txSlot(uint8_t *packet) const
{
  auto slot = m_pools.findTx(packet);
  ZmAssert_(slot && (slot->leases.load_() & Slot::Caller));
  return slot;
}

Zu::IO::Result Nif::txReady(uint8_t *packet) const
{
  if (!ready()) return Zi::IOError;
  return txSlot(packet)->leases.load_() & Slot::Submitted ? Zi::NotReady : Zi::OK;
}

Zu::IO::Result Nif::send(uint8_t *packet, unsigned length)
{
  TxPacket frame{packet, length};
  Zu::IO::Result result = Zi::IOError;
  send({&frame, 1}, result);
  return result;
}

unsigned Nif::send(ZuSpan<const TxPacket> packets)
{
  Zu::IO::Result result = Zi::IOError;
  return send(packets, result);
}

unsigned Nif::send(ZuSpan<const TxPacket> packets, Zu::IO::Result &result)
{
  unsigned limit = packets.length();
  if (limit > MaxTxBatch) limit = MaxTxBatch;
  unsigned prepared = 0;
  for (; prepared < limit && ready(); ++prepared) {
    const auto &frame = packets[prepared];
    auto slot = txSlot(frame.data);
    if (slot->leases.load_() & Slot::Submitted) { result = Zi::NotReady; break; }
    unsigned offset = uintptr_t(frame.data) - uintptr_t(slot->packet());
    if (offset >= slot->pool->size || frame.length > slot->pool->size - offset ||
	!wireLength(frame.data, frame.length))
      break;
    bool submitted = slot->submit();
    ZmAssert_(submitted);
    m_tx.iov[prepared] = {frame.data, frame.length};
    m_tx.slots[prepared] = slot;
  }
  if (!prepared) return 0;

  int sent = 0;
  int err = 0;
  while (ready()) {
    sent = ::sendmmsg(m_fd, m_tx.msg, prepared, MSG_DONTWAIT);
    if (sent >= 0) break;
    if ((err = errno) == EINTR) { sent = 0; continue; }
    break;
  }
  unsigned accepted = sent > 0 ? unsigned(sent) : 0;
  ZmAssert_(accepted <= prepared);
  for (unsigned i = accepted; i < prepared; ++i) m_tx.slots[i]->release(Slot::Submitted);

  if (accepted) {
    unsigned pending = m_pending.load_();
    ZmAssert_(accepted <= m_pools.txCapacity() - pending);
    auto completion = m_pools.completions() + pending;
    uint64_t bytes = 0;
    for (unsigned i = 0; i < accepted; ++i) {
      auto length = unsigned(m_tx.iov[i].iov_len);
      completion[i] = {m_tx.slots[i], length};
      bytes += length;
    }
    m_pending.store_(pending + accepted);
    m_txAccepted += accepted;
    m_txBytes += bytes;
    result = Zi::OK;
  } else if (err == EAGAIN || err == EWOULDBLOCK || err == ENOBUFS || (!err && ready())) {
    result = Zi::NotReady;
  } else {
    result = Zi::IOError;
    if (err && err != EINTR && err != EMSGSIZE) failure(Transmit, err);
  }
  return accepted;
}

unsigned Nif::pollTx()
{
  unsigned pending = m_pending.load_();
  if (!pending) return 0;
  auto completion = m_pools.completions();
  uint64_t bytes = 0;
  for (unsigned i = 0; i < pending; ++i) {
    bytes += completion[i].length;
    completion[i].slot->release(Slot::Submitted);
  }
  m_pending.store_(0);
  m_txRetired += pending;
  m_txRetiredBytes += bytes;
  return pending;
}

uint64_t Nif::sampleDrops() const
{
  if (ready()) {
    tpacket_stats stats;
    socklen_t length = sizeof(stats);
    if (::getsockopt(m_fd, SOL_PACKET, PACKET_STATISTICS, &stats, &length) == 0 &&
	length >= sizeof(stats)) {
      uint64_t total;
      m_drops = __builtin_add_overflow(m_drops, stats.tp_drops, &total) ? UINT64_MAX : total;
    }
  }
  return m_drops;
}

void Nif::status()
{
  unsigned index, mtu, bypass;
  bool active;
  uint64_t drops;
  {
    ZmGuard guard(m_observe);
    index = ifindex();
    mtu = this->mtu();
    bypass = m_bypass;
    active = ready();
    drops = sampleDrops();
  }
  ZiLOG(Info, "Zpkt.Afp", ([
    name = CfString{name()}, index, mtu, active, state = this->state(),
    requested = m_config.qdiscBypass, bypass, drops, discards = m_rxDiscard
  ](auto &out) {
    out << "AF_PACKET " << name << " ifindex " << ZuBoxed(index) << " MTU " << ZuBoxed(mtu)
      << " state " << ZuBoxed(state) << " mode copy";
    if (active)
      out << " Rx queues 1 Tx queues 1 qdiscBypass requested " << (requested ? "on" : "off")
	<< " effective " << (bypass == BypassOn ? "on" : "off")
	<< (bypass == BypassUnsupported ? " (PACKET_QDISC_BYPASS unsupported)" : "")
	<< " kernel drops " << ZuBoxed(drops) << " software discards " << ZuBoxed(discards);
  }));
}

struct QueueView : public Ztc::Queue {
  Ztc::QueueTelemetry	data;
  TelKey telKey() const override { return {data.ownerID, data.id, data.type}; }
  void telemetry(Ztc::QueueTelemetry &dst) const override { dst = data; }
};

unsigned Nif::allQueues(Ztc::QueueMgr::AllFn fn) const
{
  QueueView rx, tx;
  {
    ZmGuard guard(m_observe);
    if (!ready()) return 0;
    auto owner = ZuID{mx()->id()};
    auto name = ZuID{} << "afp." << ZuBoxed(ifindex()) << '.';
    auto drops = sampleDrops();
    rx.data.ownerID = tx.data.ownerID = owner;
    rx.data.id = ZuID{name} << "rx";
    tx.data.id = ZuID{name} << "tx";
    rx.data.type = Ztc::QueueType::Rx;
    tx.data.type = Ztc::QueueType::Tx;
    rx.data.size = m_pools.rx()->count;
    rx.data.outCount = m_rxPackets;
    rx.data.outBytes = m_rxBytes;
    rx.data.full = drops > UINT32_MAX ? UINT32_MAX : unsigned(drops);
    tx.data.size = m_pools.txCapacity();
    tx.data.count = m_pending.load_();
    tx.data.inCount = m_txAccepted;
    tx.data.inBytes = m_txBytes;
    tx.data.outCount = m_txRetired;
    tx.data.outBytes = m_txRetiredBytes;
  }
  fn(&rx);
  fn(&tx);
  return 2;
}

} // namespace Zpkt::Afp
