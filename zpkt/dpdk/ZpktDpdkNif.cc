//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <limits.h>
#include <linux/if_packet.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

#include <rte_bus.h>
#include <rte_dev.h>
#include <rte_devargs.h>
#include <rte_errno.h>
#include <rte_kvargs.h>
#include <rte_flow.h>
#include <rte_mbuf.h>
#include <rte_prefetch.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZpktNif.hh>
#include "ZpktDpdk.hh"
#include "ZpktDpdkNif.hh"
#include <rte_net.h>

namespace Zpkt::Dpdk {

static ZeException deviceError(const char *op, int code)
{
  return ZeEXCEPT(Error, "Zpkt.Dpdk", ([op, code](auto &out) {
    out << op << ": " << rte_strerror(code);
  }));
}

static uint16_t descriptors(unsigned count, const rte_eth_desc_lim &limits)
{
  count = RTE_MIN(count, limits.nb_max ? unsigned(limits.nb_max) : unsigned(UINT16_MAX));
  // Ethdev rounds up. Feed it the largest aligned count the pool can serve.
  if (limits.nb_align && count >= limits.nb_align) count -= count % limits.nb_align;
  return uint16_t(count);
}

static int pcapInput(const rte_device *device)
{
  auto args = device ? rte_dev_devargs(device) : nullptr;
  if (!args || !args->args) return EINVAL;
  auto values = rte_kvargs_parse(args->args, nullptr);
  if (!values) return EINVAL;
  auto value = rte_kvargs_get(values, "infinite_rx");
  bool live = rte_kvargs_count(values, "iface") || rte_kvargs_count(values, "rx_iface") ||
    rte_kvargs_count(values, "rx_iface_in");
  int err = live || (value && atoi(value) > 0) ? ENOTSUP : 0;
  rte_kvargs_free(values);
  return err;
}

static int afPacketGeometry(const rte_device *device, unsigned mtu, unsigned &frames)
{
  // net_af_packet creates PACKET_RX_RING at probe. rx_queue_setup ignores nb_desc.
  constexpr unsigned header = TPACKET2_HDRLEN - sizeof(sockaddr_ll);
  unsigned frameSize = 2048;
  frames = 512;
  auto args = device ? rte_dev_devargs(device) : nullptr;
  if (!args || !args->args) return -EINVAL;
  auto values = rte_kvargs_parse(args->args, nullptr);
  if (!values) return -EINVAL;
  bool invalid = false;
  for (unsigned i = 0; i < values->count; ++i) {
    const auto &pair = values->pairs[i];
    bool size = !strcmp(pair.key, "framesz");
    if (!size && strcmp(pair.key, "framecnt")) continue;
    char *end = nullptr;
    errno = 0;
    auto n = strtoul(pair.value, &end, 10);
    if (errno || *end || !n || n > INT_MAX) { invalid = true; break; }
    (size ? frameSize : frames) = n;
  }
  rte_kvargs_free(values);
  // TPACKET_V2 aligns the network header after sockaddr_ll and link headroom.
  if (invalid || frameSize < TPACKET_ALIGN(TPACKET2_HDRLEN + 16) + mtu) return -EINVAL;
  auto dataSize = frameSize - header;
  return dataSize <= UINT16_MAX - RTE_PKTMBUF_HEADROOM ? int(dataSize) : -EOVERFLOW;
}

static bool dedicatedPCI(const rte_device *device)
{
  // A missing PMD ifindex alone does not establish that Linux has no Rx path.
  auto bus = device ? rte_dev_bus(device) : nullptr;
  if (!bus || strcmp(rte_bus_name(bus), "pci")) return false;
  char path[PATH_MAX], target[PATH_MAX];
  snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/driver", rte_dev_name(device));
  auto n = readlink(path, target, sizeof(target) - 1);
  if (n <= 0) return false;
  target[n] = 0;
  const char *name = strrchr(target, '/');
  return name && (!strcmp(name, "/vfio-pci") || !strcmp(name, "/igb_uio") ||
    !strcmp(name, "/uio_pci_generic"));
}

Nif::~Nif()
{
  if (mx()) {
    final();
    ZmAssert_(!mx());
  }
}

ZmRef<Zi::IOBufPool> Nif::bufPool(
  Zi::IOBufType::T type, unsigned count, unsigned size,
  unsigned headRoom, unsigned tailRoom, unsigned align)
{
  return new BufPool{type, count, size, headRoom, tailRoom, align};
}

Nif::Nif(EAL &eal, ZiMultiplex *mx, const NifCf &config, bool loopback)
: Zpkt::Nif{mx, {}}, m_eal{eal}
{
  if (!mx || mx->state() != ZmEngineState::Stopped)
    throw ZeEXCEPT(Error, "Zpkt.Dpdk", "construct with a stopped multiplexer");
  uint16_t port;
  if (int rc = rte_eth_dev_get_port_by_name(config.port, &port))
    throw deviceError("Ethernet device lookup failed", -rc);
  if (!m_eal.claim(port)) throw deviceError("Ethernet device is already claimed", EBUSY);
  try {
    rte_eth_dev_info info{};
    if (int rc = rte_eth_dev_info_get(port, &info))
      throw deviceError("query Ethernet port", -rc);
    bool ring = info.driver_name && !strcmp(info.driver_name, "net_ring");
    if (loopback && !ring)
      throw deviceError("backend loopback requires a net_ring port", ENOTSUP);
    uint16_t portMtu;
    rte_ether_addr portMac{};
    if (int rc = rte_eth_dev_get_mtu(port, &portMtu))
      throw deviceError("query Ethernet port MTU", -rc);
    if (int rc = rte_eth_macaddr_get(port, &portMac))
      throw deviceError("query Ethernet port MAC", -rc);
    if (!info.max_rx_queues || !info.max_tx_queues || !portMtu ||
	unsigned(portMtu) + RTE_ETHER_HDR_LEN + RTE_PKTMBUF_HEADROOM > UINT16_MAX)
      throw deviceError("unsupported Ethernet port geometry", EINVAL);
    bool pcap = info.driver_name && !strcmp(info.driver_name, "net_pcap");
    if (pcap)
      if (int err = pcapInput(info.device))
        throw deviceError("net_pcap requires finite file Rx; use net_af_packet for sized live Rx", err);
    int minRxSize = 0;
    if (info.driver_name && !strcmp(info.driver_name, "net_af_packet")) {
      if (info.max_rx_queues != 1)
        throw deviceError("net_af_packet requires qpairs=1 for whole-interface ingress", EINVAL);
      minRxSize = afPacketGeometry(info.device, portMtu, m_captureFrames);
      if (minRxSize < 0) throw deviceError("invalid AF_PACKET capture ring geometry", -minRxSize);
    }
    auto bus = info.device ? rte_dev_bus(info.device) : nullptr;
    bool virtualPort = bus && !strcmp(rte_bus_name(bus), "vdev");
    // Software-only ethdevs (e.g. memif) have no hardware Rx queues to steer.
    if (!ring && !pcap && (minRxSize || info.if_index || !virtualPort) &&
        !dedicatedPCI(info.device)) {
      if (!info.if_index)
        throw deviceError("cannot identify host interface for exclusive DPDK ingress", ENODEV);
      m_ingressFD = nifClaimIngress(info.if_index);
      if (m_ingressFD < 0) throw deviceError("claim DPDK netdev ingress", -m_ingressFD);
    }
    m_config = config;
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (fd < 0) throw deviceError("create Rx wake eventfd", errno);

    NifInfo geometry{info.if_index, portMtu, 1, {}};
    memcpy(geometry.mac, portMac.addr_bytes, sizeof(geometry.mac));
    nifInfo(geometry);
    m_port = port;
    tcpChksumOffload(bool(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_TCP_CKSUM));
    wakeFD(fd);
    m_ring = ring;
    m_pcap = pcap;
    m_afPacketMinRxSize = minRxSize;
    zeroCopy(m_ring);
  } catch (...) {
    if (m_ingressFD >= 0) close(m_ingressFD);
    m_eal.release(port);
    throw;
  }
}

void Nif::reserveTxPools(ZuSpan<Zi::IOBufPool *const> pools)
{
  ZmAssert_(mx() && Engine::state() == ZmEngineState::Stopped && !allocated());
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
  if (!mx() || Engine::state() != ZmEngineState::Stopped)
    return ZeEXCEPT(Error, "Zpkt.Dpdk", "initialize and stop the device before allocation");
  if (allocated()) {
    auto iter = txPools.citer();
    bool same = m_rxPool.ptr() == rxPool && m_appTx == txPools.count_();
    while (same) {
      auto pool = iter();
      if (!pool) return {};
      bool found = false;
      for (unsigned i = 0; i < m_appTx; ++i) found |= m_txPools[i].ptr() == pool;
      same = found;
    }
    return ZeEXCEPT(Error, "Zpkt.Dpdk", "conflicting DPDK pool set");
  }

  auto ingress = dynamic_cast<BufPool *>(rxPool);
  if (!ingress || ingress->type != Zi::IOBufType::Rx ||
      ingress->size < mtu() + RTE_ETHER_HDR_LEN || !ingress->count)
    return deviceError("invalid ingress pool or frame geometry", EINVAL);

  Pools tx;
  uint64_t capacity = 0;
  auto add = [&tx, &capacity](BufPool *pool) {
    if (!pool || !pool->count || (pool->type != Zi::IOBufType::TxTCP &&
	pool->type != Zi::IOBufType::TxUDP)) return false;
    for (const auto &seen : tx) if (seen.ptr() == pool) return false;
    capacity += pool->count;
    tx.push(ZmRef<BufPool>{pool});
    return true;
  };
  auto iter = txPools.citer();
  while (auto pool = iter())
    if (!add(dynamic_cast<BufPool *>(pool))) return deviceError("invalid DPDK Tx pool", EINVAL);
  unsigned appTx = tx.length();
  for (const auto &pool : m_reserves)
    if (!add(pool.ptr())) return deviceError("invalid or duplicate DPDK Tx reserve", EINVAL);
  if (!capacity || capacity > UINT_MAX)
    return deviceError("invalid aggregate Tx capacity", EINVAL);

  const int socket = rte_eth_dev_socket_id(m_port);
  BufPool::Prepared rxBacking;
  // AF_PACKET validates against the socket ring's frame capacity, which can
  // exceed the Ethernet MTU. Preserve the caller's frame size in BufPool.
  if (int rc = ingress->prepare(rxBacking, socket, m_afPacketMinRxSize))
    return deviceError("allocate DPDK ingress backing", -rc);
  ZtArray<BufPool::Prepared, ZtArrayHeapID<"Zpkt.Dpdk.Prepared">> backing;
  ZtArray<rte_mempool *, ZtArrayHeapID<"Zpkt.Dpdk.NativePools">> native;
  backing.length(tx.length());
  for (unsigned i = 0; i < tx.length(); ++i) {
    if (int rc = tx[i]->prepare(backing[i], socket, 0, &m_txReturns))
      return deviceError("allocate DPDK Tx backing", -rc);
    native.push(backing[i].native());
  }
  AddressIndex index;
  if (int rc = index.init(native.cspan())) return deviceError("index DPDK Tx backing", -rc);
  if (int rc = m_txReturns.init(tx.length(), socket))
    return deviceError("allocate DPDK Tx return queue", -rc);
  m_txCapacity = unsigned(capacity);
  m_txIndex = ZuMv(index);
  m_appTx = appTx;
  m_rxPool = ingress;
  m_txPools = ZuMv(tx);
  m_rxPool->returned([this] { returned(); });
  m_rxPool->publish(ZuMv(rxBacking));
  for (unsigned i = 0; i < m_txPools.length(); ++i) m_txPools[i]->publish(ZuMv(backing[i]));
  return {};
}

void Nif::final()
{
  ZmAssert_(Engine::state() == ZmEngineState::Stopped);
  ZmGuard guard(m_queueLock);
  if (!mx()) return;
  // A failed stop or an outstanding native reference does not permit backing
  // release. Retry the stop with the scheduler already quiescent.
  if (auto result = stopPort(); result.is<ZeException>()) {
    error(ZuMv(result).p<ZeException>());
    return;
  }
  drainTx(UINT_MAX);
  if (m_txOutstanding || m_txReturns.active()) {
    error(deviceError("Ethernet Tx returns remain active", EBUSY));
    return;
  }
  drainTx(UINT_MAX); // consume announcements from the last exiting publisher
  if (m_rxPool && m_rxPool->available() != m_rxPool->count) {
    error(deviceError("Ethernet Rx buffers remain in use", EBUSY));
    return;
  }
  for (const auto &pool : m_txPools)
    if (pool->available() != pool->count) {
      error(deviceError("Ethernet Tx buffers remain in use", EBUSY));
      return;
    }
  m_txIndex.reset();
  if (m_rxPool) ZmAssert_(m_rxPool->final());
  for (auto &pool : m_txPools) ZmAssert_(pool->final());
  m_rxPool = nullptr;
  m_txPools.null();
  m_reserves.null();
  m_txReturns.reset();
  m_txCapacity = 0;
  if (m_ingressFD >= 0) { close(m_ingressFD); m_ingressFD = -1; }
  m_eal.release(m_port);
  closeWake();
  m_rxFD = -1;
  m_port = NoPort;
  resetInfo();
  m_config = {};
  m_appTx = m_afPacketMinRxSize = 0;
  m_ring = m_pcap = false;
  error({});
  final_();
}

void Nif::start_()
{

  error({});
  if (!allocated() || !mx()->running() || !receiving()) {
    error(ZeEXCEPT(
      Error, "Zpkt.Dpdk", "start requires allocated pools, a running mux and Rx callback"));
  } else {
    auto result = activate();
    if (result.is<void>()) {
      m_active = 1;
      Engine::started(true);
      return;
    }
    error(ZuMv(result).p<ZeException>());
  }
  clearCallbacks();
  Engine::started(false);
}

Nif::Result Nif::activate()
{
  ZmGuard guard(m_queueLock);
  if (auto result = stopPort(); result.is<ZeException>()) return result;
  drainTx(UINT_MAX);
  if (m_txOutstanding || m_txReturns.active())
    return deviceError("prior Tx submissions remain outstanding", EBUSY);
  drainTx(UINT_MAX);
  rte_eth_dev_info info{};
  if (int rc = rte_eth_dev_info_get(m_port, &info))
    return deviceError("query queue capabilities", -rc);

  bool steer = m_ingressFD >= 0 && !m_afPacketMinRxSize;
  if (steer && !m_isolated) {
    rte_flow_error detail{};
    if (int rc = rte_flow_isolate(m_port, 1, &detail))
      return deviceError("isolate Ethernet ingress before queue configuration", -rc);
    m_isolated = true;
  }
  rte_eth_conf config{};
  config.rxmode.mtu = mtu();
  config.txmode.offloads = info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_TCP_CKSUM;
  config.rxmode.offloads = info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_TCP_CKSUM;
  // Try arrival interrupts where available. Software PMDs may also accept
  // this setting and supply no interrupt fd.
  config.intr_conf.rxq = m_config.rxInterval && !m_ring && !m_pcap && !m_afPacketMinRxSize;
  if (m_captureFrames && !m_rxPool->available())
    return deviceError("need a free buffer to drain the prior PACKET_RX_RING", EBUSY);
  int rc = rte_eth_dev_configure(m_port, 1, 1, &config);
  if ((rc == -ENOTSUP || rc == -EINVAL) && config.intr_conf.rxq) {
    config.intr_conf.rxq = 0;
    rc = rte_eth_dev_configure(m_port, 1, 1, &config);
  }
  if ((rc == -ENOTSUP || rc == -EINVAL) && config.rxmode.offloads) {
    config.rxmode.offloads = 0;
    rc = rte_eth_dev_configure(m_port, 1, 1, &config);
  }
  if ((rc == -ENOTSUP || rc == -EINVAL) && config.txmode.offloads) {
    config.txmode.offloads = 0;
    rc = rte_eth_dev_configure(m_port, 1, 1, &config);
  }
  if (rc) return deviceError("configure Ethernet port", -rc);
  tcpChksumOffload(bool(config.txmode.offloads & RTE_ETH_TX_OFFLOAD_TCP_CKSUM));
  auto rx = info.default_rxconf;
  unsigned rxCount = m_rxPool->count;
  if (!m_ring && !m_pcap && !m_afPacketMinRxSize) {
    // Descriptor-based PMDs can allocate replacements before returning the
    // consumed descriptors. Leave a rearm batch in the same ingress mempool.
    unsigned rearm = RTE_MAX(unsigned(rx.rx_free_thresh), unsigned(Burst));
    if (rxCount <= rearm)
      return deviceError("ingress pool cannot supply an Rx rearm batch", EINVAL);
    rxCount -= rearm;
  }
  auto rxDesc = descriptors(rxCount, info.rx_desc_lim);
  auto txDesc = descriptors(m_txCapacity, info.tx_desc_lim);
  if ((rc = rte_eth_dev_adjust_nb_rx_tx_desc(m_port, &rxDesc, &txDesc)))
    return deviceError("adjust Ethernet descriptors", -rc);
  if (!rxDesc || !txDesc || rxDesc > rxCount)
    return deviceError("ingress pool is smaller than the required descriptors", EINVAL);
  const auto socket = rte_eth_dev_socket_id(m_port);
  auto tx = info.default_txconf;
  tx.offloads = config.txmode.offloads;
  rx.offloads = config.rxmode.offloads;
  if ((rc = rte_eth_tx_queue_setup(m_port, 0, txDesc, socket, &tx)))
    return deviceError("set up Ethernet Tx queue", -rc);
  if ((rc = rte_eth_rx_queue_setup(m_port, 0, rxDesc, socket, &rx, m_rxPool->native())))
    return deviceError("set up Ethernet Rx queue", -rc);
  if ((rc = rte_eth_dev_start(m_port))) {
    // Ethdev may have started the port before restoring its configuration.
    // Stop is idempotent if activation never brought the port up.
    if (int rollback = rte_eth_dev_stop(m_port)) {
      m_started = true; // retry-stop before the next run or finalization
      ZiLogEvent(deviceError("roll back Ethernet activation", -rollback));
    }
    return deviceError("start Ethernet port", -rc);
  }

  m_started = true;
  if (steer) {
    rte_flow_attr attr{};
    attr.ingress = 1;
    rte_flow_item pattern[] = {{.type = RTE_FLOW_ITEM_TYPE_ETH}, {.type = RTE_FLOW_ITEM_TYPE_END}};
    rte_flow_action_queue queue{0};
    rte_flow_action actions[] = {{RTE_FLOW_ACTION_TYPE_QUEUE, &queue}, {RTE_FLOW_ACTION_TYPE_END}};
    rte_flow_error detail{};
    rc = rte_flow_validate(m_port, &attr, pattern, actions, &detail);
    if (!rc) {
      m_flow = rte_flow_create(m_port, &attr, pattern, actions, &detail);
      if (!m_flow) rc = rte_errno ? -rte_errno : -EIO;
    }
    if (rc) {
      auto failure = deviceError("claim all Ethernet ingress on Rx queue 0", -rc);
      if (auto rollback = stopPort(); rollback.is<ZeException>())
        ZiLogEvent(rollback.p<ZeException>());
      return failure;
    }
  }
  // net_af_packet leaves the socket tap attached from EAL probe through port
  // stop. Discard queued copies from before this run (including before open).
  for (unsigned left = m_captureFrames; left;) {
    rte_mbuf *stale[Burst];
    unsigned n = rte_eth_rx_burst(m_port, 0, stale, RTE_MIN(left, unsigned(Burst)));
    if (!n) break;
    rte_pktmbuf_free_bulk(stale, n);
    left -= n;
  }
  m_rxDesc = rxDesc;
  m_txDesc = txDesc;
  m_rxFD = config.intr_conf.rxq ? rte_eth_dev_rx_intr_ctl_q_get_fd(m_port, 0) : -1;
  m_txCleanup = 0;
  m_stopDeadline = {};
  m_rxReturned = 0;
  resetWake();
  m_rxDepthRequested = m_rxDepth = 0;
  ackWake();
  return {};
}

Nif::Result Nif::stopPort()
{
  if (m_flow) {
    rte_flow_error detail{};
    if (int rc = rte_flow_destroy(m_port, m_flow, &detail))
      return deviceError("withdraw Ethernet ingress rule", -rc);
    m_flow = nullptr;
  }
  if (m_started) {
    if (int rc = rte_eth_dev_stop(m_port)) return deviceError("stop Ethernet port", -rc);
    m_started = false;
  }
  return {};
}

void Nif::stop_()
{
  m_active = 0;
  wake();
  // Even if stop originates in an Rx callback, clear callbacks only after
  // that invocation and its containing receive burst have returned.
  mx()->rxRun([this] { quiesceRx(m_ring && m_started ? m_rxPool->count : 0); });
}

void Nif::quiesceRx(unsigned left)
{
  if (left) {
    // The ring PMD does not drain its backing rings on stop. After the peer
    // stops publishing, one ingress-pool capacity covers all posted buffers.
    auto count = receive(false);
    if (count && count < left) {
      mx()->push([this, left = left - count] { quiesceRx(left); }, mx()->rxThread());
      return;
    }
  }
  mx()->txRun([this] {
    m_stopDeadline = monotonicNow() + ZuTime{2};
    retire();
  });
}

void Nif::retire()
{
  if (m_txOutstanding) {
    auto retired = drainTx(Burst);
    if (m_txOutstanding && retired < Burst) {
      cleanTx();
      drainTx(Burst - retired);
    }
    if (m_txOutstanding && monotonicNow() < m_stopDeadline) {
      mx()->push([this] { retire(); }, mx()->txThread());
      return;
    }
  }
  {
    ZmGuard guard(m_queueLock);
    if (auto result = stopPort(); result.is<ZeException>())
      error(ZuMv(result).p<ZeException>());
    m_rxFD = -1;
  }
  finishStop();
}

void Nif::finishStop()
{
  const bool quiesced = !m_txOutstanding && !m_txReturns.active();
  auto retired = drainTx(Burst);
  if (retired < Burst && m_txOutstanding && !m_txReturns.active())
    retired += drainTx(Burst - retired);
  // A last publisher may announce the pool after the drain observes it empty.
  // A clean run needs a drain begun with no outstanding work and no publishers.
  if (retired == Burst || m_txReturns.active() || (!m_txOutstanding && !quiesced)) {
#ifdef ZPKT_DPDK_TEST
    if (m_txReturns.active()) ZpktTestDpdkReturns(ReturnPoint::Wait, 0);
#endif
    mx()->push([this] { finishStop(); }, mx()->txThread());
    return;
  }
  if (!failed() && m_txOutstanding)
    error(deviceError("Ethernet Tx still owns submitted buffers after stop", ETIMEDOUT));
  if (!failed() && m_txCleanup < 0 && m_txCleanup != -ENOTSUP && m_txCleanup != -EAGAIN)
    error(deviceError("service Ethernet Tx completions", -m_txCleanup));
  finishCallbacks();
  Engine::stopped(!failed());
}

Zu::IO::Result Nif::txReady(uint8_t *packet) const
{
  if (!ready()) return Zi::IOError;
  auto slot = m_txIndex.find(packet);
  ZmAssert_(slot);
  unsigned leases = slot->leases.load_();
  ZmAssert_(leases & Slot::Caller);
  return leases & Slot::Submitted ? Zi::NotReady : Zi::OK;
}

Zu::IO::Result Nif::send(uint8_t *packet, unsigned length)
{
  if (!ready()) return Zi::IOError;
  Slot *slot;
  auto result = prepareTx(packet, length, slot);
  if (result != Zi::OK) return result;
  auto mbuf = slot->mbuf;
  if (!rte_eth_tx_burst(m_port, 0, &mbuf, 1)) {
    refuseTx(slot);
    return Zi::NotReady;
  }
  ++m_txOutstanding;
  return Zi::OK;
}

unsigned Nif::send(ZuSpan<const TxPacket> packets)
{
  if (!ready()) return 0;
  unsigned count = RTE_MIN(packets.length(), uint64_t(MaxTxBatch));
  count = RTE_MIN(count, m_txCapacity - m_txOutstanding);
  if (!count) return 0;
  rte_mbuf *mbufs[MaxTxBatch];
  unsigned prepared = 0;
  for (; prepared < count; ++prepared) {
    const auto &packet = packets[prepared];
    Slot *slot;
    if (prepareTx(packet.data, packet.length, slot) != Zi::OK) break;
    mbufs[prepared] = slot->mbuf;
  }
  if (!prepared) return 0;
  unsigned accepted = rte_eth_tx_burst(m_port, 0, mbufs, prepared);
  m_txOutstanding += accepted;
  while (prepared > accepted) refuseTx(BufPool::slot(mbufs[--prepared]));
  return accepted;
}

ZuInline Zu::IO::Result Nif::prepareTx(uint8_t *packet, unsigned length, Slot *&slot)
{
  slot = m_txIndex.find(packet);
  ZmAssert_(slot);
  if (!length || length > slot->pool->size - unsigned(packet - slot->packet) ||
      (length > mtu() + RTE_ETHER_HDR_LEN && !frameFitsMTU(packet, length, mtu())))
    return Zi::IOError;
  if (m_txOutstanding == m_txCapacity) return Zi::NotReady;
  if (!slot->submit()) {
    ZmAssert_(slot->leases.load_() & Slot::Caller);
    return Zi::NotReady;
  }
  auto mbuf = slot->mbuf;
  BufPool::resetTx(mbuf);
  mbuf->data_off = packet - static_cast<uint8_t *>(mbuf->buf_addr);
  mbuf->data_len = mbuf->pkt_len = length;
  if (configuredTCPChksumOffload()) {
    // The caller supplies the pseudo-header seed. Only native mbuf metadata
    // belongs here; no checksum traversal or modification of caller bytes.
    rte_net_hdr_lens head{};
    auto type = rte_net_get_ptype(mbuf, &head,
      RTE_PTYPE_L2_MASK | RTE_PTYPE_L3_MASK | RTE_PTYPE_L4_MASK);
    if ((type & RTE_PTYPE_L4_MASK) == RTE_PTYPE_L4_TCP) {
      auto ip = packet + head.l2_len;
      bool v6 = ip[0]>>4 == 6;
      unsigned size = v6 ? 40 + (unsigned(ip[4])<<8) + ip[5] :
	(unsigned(ip[2])<<8) + ip[3];
      mbuf->l2_len = head.l2_len;
      mbuf->l3_len = head.l3_len;
      mbuf->l4_len = head.l4_len;
      mbuf->ol_flags |= RTE_MBUF_F_TX_TCP_CKSUM |
	(v6 ? RTE_MBUF_F_TX_IPV6 : RTE_MBUF_F_TX_IPV4);
      // The NIC supplies Ethernet minimum-frame padding after checksum work.
      // Do not include software padding in the offloaded transport extent.
      mbuf->data_len = mbuf->pkt_len = head.l2_len + size;
    }
  }
  return Zi::OK;
}

ZuInline void Nif::refuseTx(Slot *slot)
{
  auto mbuf = slot->mbuf;
  BufPool::resetTx(mbuf);
  slot->release(Slot::Submitted);
}

void Nif::cleanTx()
{
  if (!m_started || m_txCleanup == -ENOTSUP) return;
  int rc = rte_eth_tx_done_cleanup(m_port, 0, Burst);
  if (rc < 0 && rc != m_txCleanup && rc != -ENOTSUP && rc != -EAGAIN)
    ZiLogEvent(deviceError("service Ethernet Tx completions", -rc));
  m_txCleanup = rc < 0 ? rc : 0;
}

unsigned Nif::drainTx(unsigned budget)
{
  unsigned retired = 0;
  while (retired < budget) {
    auto pool = m_txReturns.shift();
    if (!pool) break;
    void *objects[Burst];
    unsigned count = RTE_MIN(rte_mempool_avail_count(pool),
      RTE_MIN(unsigned(Burst), budget - retired));
    if (count) {
      ZmAssert_(!rte_mempool_get_bulk(pool, objects, count));
      ZmAssert_(count <= m_txOutstanding);
      m_txOutstanding -= count;
      retired += count;
#ifdef ZPKT_DPDK_TEST
      ZpktTestDpdkReturns(ReturnPoint::Visit, count);
#endif
      for (unsigned i = 0; i < count; ++i)
	BufPool::slot(static_cast<rte_mbuf *>(objects[i]))->release(Slot::Submitted);
    }
    m_txReturns.settle(pool);
  }
  return retired;
}

unsigned Nif::pollTx()
{
  if (!ready() || !m_txOutstanding) return 0;
  auto retired = drainTx(Burst);
  if (m_txOutstanding && retired < Burst) {
    cleanTx();
    retired += drainTx(Burst - retired);
  }
  if (!m_txOutstanding || retired || !mx()->stopping()) {
    m_stopDeadline = {};
  } else {
    // Some PMDs only release their last descriptors when deactivated.
    // Let the mux reach device stop if Tx cannot progress during shutdown.
    // This instance still owns each outstanding lease until native retirement.
    auto now = monotonicNow();
    if (!m_stopDeadline) m_stopDeadline = now + ZuTime{2};
    else if (now >= m_stopDeadline) {
      m_active = 0;
      wake();
    }
  }
  return retired;
}

unsigned Nif::receive(bool deliver)
{
  rte_mbuf *packets[Burst];
  unsigned count = rte_eth_rx_burst(m_port, 0, packets, Burst);
  if (!count) return 0;
  ZiRxBurst burst;
  for (unsigned i = 0; i < count; ++i) {
    auto mbuf = packets[i];
    if (deliver && i + 1 < count) {
      auto next = packets[i + 1];
      rte_prefetch0(next);
      rte_prefetch0(rte_pktmbuf_mtod(next, const void *));
    }
    auto data = rte_pktmbuf_mtod(mbuf, uint8_t *);
    // net_af_packet can reinsert a stripped VLAN tag in mbuf headroom.
    // Deliver the complete frame at the canonical caller-owned slot address.
    if (m_captureFrames && mbuf->data_off == RTE_PKTMBUF_HEADROOM - RTE_VLAN_HLEN &&
        mbuf->pool == m_rxPool->native() && mbuf->nb_segs == 1 &&
        mbuf->pkt_len <= m_rxPool->size) {
      memmove(data + RTE_VLAN_HLEN, data, mbuf->pkt_len);
      mbuf->data_off += RTE_VLAN_HLEN;
      data += RTE_VLAN_HLEN;
    }
    if (deliver && m_active && mbuf->nb_segs == 1 &&
        (mbuf->ol_flags & RTE_MBUF_F_RX_L4_CKSUM_MASK) != RTE_MBUF_F_RX_L4_CKSUM_BAD &&
	mbuf->data_len == mbuf->pkt_len && mbuf->pkt_len &&
	mbuf->pkt_len <= m_rxPool->size &&
        (mbuf->pkt_len <= mtu() + RTE_ETHER_HDR_LEN || frameFitsMTU(data, mbuf->pkt_len, mtu())) &&
        m_rxPool->adopt(mbuf))
      recv(data, mbuf->pkt_len, count > 1 ? &burst : nullptr);
    else {
      burst.flush();
      rte_pktmbuf_free(mbuf);
    }
  }
  return count;
}

void Nif::returned()
{
  if (m_active && !mx()->invoked(mx()->rxThread()) && !m_rxReturned.xch(1))
    wake_();
}

unsigned Nif::block(unsigned delay)
{
  unsigned count = 0;
  startBlocking();
  if (m_active && !wakePending() && !m_rxReturned) {
    bool interrupt = m_rxFD >= 0 && !rte_eth_dev_rx_intr_enable(m_port, 0);
    // The last probe after arming closes the packet/interrupt race.
    if (interrupt && !wakePending() && m_active) count = receive(true);
    if (!count && m_active && !wakePending() && !m_rxReturned) {
      pollfd fds[] = {{wakeFD(), POLLIN, 0}, {m_rxFD, POLLIN, 0}};
      timespec timeout{time_t(delay / 1000000), long(delay % 1000000) * 1000};
      ppoll(fds, interrupt ? 2 : 1, &timeout, nullptr);
      if (fds[0].revents & POLLIN) ackWake();
      if (fds[1].revents & POLLIN) {
	uint64_t value;
	// Linux ethdev exposes the receive interrupt vector's eventfd.
	if (::read(m_rxFD, &value, sizeof(value)) < 0 && errno != EINTR && errno != EAGAIN)
	  m_rxFD = -1;
      }
    }
    if (interrupt) rte_eth_dev_rx_intr_disable(m_port, 0);
  }
  stopBlocking();
  return count;
}

void Nif::rx()
{
  unsigned spin = m_config.spin;
  unsigned initial = RTE_MIN(m_config.rxInterval, 4U), delay = initial;
  while (ready()) {
    if (woken()) return;
    if (m_rxDepthRequested.load_() && m_rxDepthRequested.xch(0)) {
      int count = rte_eth_rx_queue_count(m_port, 0);
      m_rxDepth.store_(count > 0 ? unsigned(count) : 0);
    }
    if ((m_rxReturned && m_rxReturned.xch(0)) || receive(true)) {
      spin = m_config.spin;
      delay = initial;
      continue;
    }
    if (spin) { --spin; continue; }
    if (!delay) continue;
    if (block(delay)) {
      spin = m_config.spin;
      delay = initial;
    } else {
      delay += RTE_MIN(delay, m_config.rxInterval - delay);
    }
  }
}

void Nif::status()
{
  ZmGuard guard(m_queueLock);
  char mac[RTE_ETHER_ADDR_FMT_SIZE];
  rte_ether_addr address;
  memcpy(address.addr_bytes, Nif::mac().data(), sizeof(address.addr_bytes));
  rte_ether_format_addr(mac, sizeof(mac), &address);
  ZiLOG(Info, "Zpkt.Dpdk", ([
    name = m_config.port, state = state(), index = ifindex(), mtu = mtu(),
    address = ZuCArray<RTE_ETHER_ADDR_FMT_SIZE>{mac},
    mode = m_ring ? "zero-copy" : (m_pcap || m_afPacketMinRxSize) ? "copy" : "unverified",
    guarded = m_ingressFD >= 0, catchAll = !!m_flow, captureFrames = m_captureFrames,
    started = m_started, rxDesc = m_rxDesc, txDesc = m_txDesc, tcpOffload = configuredTCPChksumOffload()
  ](auto &out) {
    out << "DPDK " << name << " state " << ZuBoxed(state)
      << " ifindex " << ZuBoxed(index) << " MTU " << ZuBoxed(mtu)
      << " MAC " << address << " mode " << mode
      << " TCP checksum " << (tcpOffload ? "offload" : "offload unavailable");
    if (guarded) out << " netdev ingress guarded";
    if (catchAll) out << " Ethernet catch-all to queue 0";
    if (captureFrames) out << " PACKET_RX_RING frames " << ZuBoxed(captureFrames);
    if (started)
      out << " Rx queues 1 (" << ZuBoxed(rxDesc) << " requested descriptors)"
        " Tx queues 1 (" << ZuBoxed(txDesc) << " requested descriptors)";
  }));
}

struct QueueView : public Ztc::Queue {
  ZuID		owner, id;
  rte_eth_stats	stats{};
  unsigned	depth, captureFrames;
  uint16_t	port;
  Ztc::QueueType::T type;
  bool		haveStats;

  QueueView(ZuID owner_, ZuID id_, uint16_t port_, Ztc::QueueType::T type_,
    const rte_eth_stats &stats_, bool haveStats_, unsigned depth_ = 0, unsigned captureFrames_ = 0)
  : owner{owner_}, id{id_}, stats{stats_}, depth{depth_}, captureFrames{captureFrames_}, port{port_}, type{type_},
    haveStats{haveStats_} { }

  TelKey telKey() const override { return {owner, id, type}; }
  void telemetry(Ztc::QueueTelemetry &data) const override {
    data = {};
    data.ownerID = owner; data.id = id; data.type = type;
    if (type == Ztc::QueueType::Rx) {
      rte_eth_rxq_info info{};
      if (captureFrames) data.size = captureFrames;
      else if (!rte_eth_rx_queue_info_get(port, 0, &info)) data.size = info.nb_desc;
      data.count = depth;
      if (haveStats) {
	data.outCount = stats.ipackets;
	data.outBytes = stats.ibytes;
	data.full = RTE_MIN(stats.imissed, uint64_t(UINT32_MAX));
      }
    } else {
      rte_eth_txq_info info{};
      if (!rte_eth_tx_queue_info_get(port, 0, &info)) data.size = info.nb_desc;
      if (haveStats) { data.inCount = stats.opackets; data.inBytes = stats.obytes; }
    }
  }
};

unsigned Nif::allQueues(Ztc::QueueMgr::AllFn fn) const
{
  ZmGuard guard(m_queueLock);
  if (!m_started || !m_active) return 0;
  rte_eth_stats stats{};
  bool haveStats = !rte_eth_stats_get(m_port, &stats);
  auto owner = ZuID{mx()->id()};
  auto name = ZuID{} << "dpdk." << m_config.port << '.';
  // The application Rx owner samples dataplane depth on its next turn.
  auto depth = m_rxDepth.load_();
  m_rxDepthRequested.store_(1);
  QueueView rx{owner, ZuID{name} << "rx", m_port, Ztc::QueueType::Rx, stats, haveStats, depth, m_captureFrames};
  QueueView tx{owner, ZuID{name} << "tx", m_port, Ztc::QueueType::Tx, stats, haveStats};
  fn(&rx);
  fn(&tx);
  return 2;
}

} // namespace Zpkt::Dpdk
