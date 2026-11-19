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

#include <rte_dev.h>
#include <rte_devargs.h>
#include <rte_errno.h>
#include <rte_kvargs.h>
#include <rte_mbuf.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZxdpBPF.hh>
#include <zlib/ZxdpDpdk.hh>
#include <zlib/ZxdpDpdkInstance.hh>

namespace Zxdp::Dpdk {

static ZeException deviceError(const char *op, int code)
{
  return ZeEXCEPT(Error, "Zxdp.Dpdk", ([op, code](auto &out) {
    out << op << ": " << rte_strerror(code);
  }));
}

static int linkInfo(uint16_t port, rte_eth_dev_info &info,
  uint16_t &mtu, rte_ether_addr &mac)
{
  if (int rc = rte_eth_dev_info_get(port, &info)) return rc;
  if (int rc = rte_eth_dev_get_mtu(port, &mtu)) return rc;
  return rte_eth_macaddr_get(port, &mac);
}

static uint16_t descriptors(unsigned count, const rte_eth_desc_lim &limits)
{
  count = RTE_MIN(count, limits.nb_max ? unsigned(limits.nb_max) : unsigned(UINT16_MAX));
  // Ethdev rounds up. Feed it the largest aligned count the pool can serve.
  if (limits.nb_align && count >= limits.nb_align) count -= count % limits.nb_align;
  return uint16_t(count);
}

static bool infinitePcap(const rte_device *device)
{
  auto args = device ? rte_dev_devargs(device) : nullptr;
  if (!args || !args->args) return false;
  auto values = rte_kvargs_parse(args->args, nullptr);
  if (!values) return false;
  auto value = rte_kvargs_get(values, "infinite_rx");
  bool enabled = rte_kvargs_count(values, "infinite_rx") == 1 && value && atoi(value) > 0;
  rte_kvargs_free(values);
  return enabled;
}

static int afPacketMinRxSize(const rte_device *device)
{
  // The PMD exposes framesz through its devargs. Its default is private.
  constexpr unsigned slotHeader = TPACKET2_HDRLEN - sizeof(sockaddr_ll);
  constexpr unsigned defaultFrameSize = 2048;
  auto args = device ? rte_dev_devargs(device) : nullptr;
  if (!args || !args->args) return defaultFrameSize - slotHeader;
  auto values = rte_kvargs_parse(args->args, nullptr);
  if (!values) return -EINVAL;
  const char *value = nullptr;
  for (unsigned i = 0; i < values->count; ++i)
    if (!strcmp(values->pairs[i].key, "framesz")) value = values->pairs[i].value;
  char *end = nullptr;
  errno = 0;
  auto frameSize = value ? strtoul(value, &end, 10) : defaultFrameSize;
  bool invalid = errno == ERANGE || (value && (end == value || *end)) ||
    frameSize <= slotHeader;
  rte_kvargs_free(values);
  if (invalid) return -EINVAL;
  auto dataSize = frameSize - slotHeader;
  return dataSize <= UINT16_MAX - RTE_PKTMBUF_HEADROOM ? int(dataSize) : -EOVERFLOW;
}

Instance::~Instance()
{
  if (m_mx) final();
}

ZmRef<Zi::IOBufPool> Instance::bufPool(
  Zi::IOBufType::T type, unsigned count, unsigned size,
  unsigned headRoom, unsigned tailRoom, unsigned align)
{
  return new BufPool{type, count, size, headRoom, tailRoom, align};
}

Instance::Result Instance::init(ZiMultiplex *mx, const Zxdp::Params &params)
{
  if (!mx || Engine::state() != ZmEngineState::Stopped ||
      mx->state() != ZmEngineState::Stopped)
    return ZeEXCEPT(Error, "Zxdp.Dpdk", "initialize with a stopped multiplexer");
  if (!params.is<Dpdk::Params>())
    return ZeEXCEPT(Error, "Zxdp.Dpdk", "DPDK instance requires DPDK parameters");
  const auto &config = params.p<Dpdk::Params>();
  if (m_mx) {
    if (mx == m_mx && config.name == m_config.name && config.ifindex == m_config.ifindex &&
	config.spin == m_config.spin && config.rxInterval == m_config.rxInterval) return {};
    return ZeEXCEPT(Error, "Zxdp.Dpdk", "conflicting DPDK initialization");
  }
  if (!config.name || config.name.length() >= RTE_ETH_NAME_MAX_LEN ||
      memchr(config.name.data(), 0, config.name.length()))
    return deviceError("invalid Ethernet device name", EINVAL);
  uint16_t port;
  if (int rc = rte_eth_dev_get_port_by_name(config.name, &port))
    return deviceError("Ethernet device lookup failed", -rc);
  if (!m_eal.claim(port)) return deviceError("Ethernet device is already claimed", EBUSY);
  auto release = [this, port](ZeException error) {
    m_eal.release(port);
    return error;
  };
  rte_eth_dev_info info{};
  rte_ether_addr mac{};
  uint16_t mtu = 0;
  if (int rc = linkInfo(port, info, mtu, mac))
    return release(deviceError("query Ethernet port", -rc));
  if (!info.max_rx_queues || !info.max_tx_queues || !mtu ||
      unsigned(mtu) + RTE_ETHER_HDR_LEN + RTE_PKTMBUF_HEADROOM > UINT16_MAX)
    return release(deviceError("unsupported Ethernet port geometry", EINVAL));
  bool pcap = info.driver_name && !strcmp(info.driver_name, "net_pcap");
  if (pcap && infinitePcap(info.device))
    return release(deviceError("pcap infinite Rx retains backing across queue stop", ENOTSUP));
  int minRxSize = 0;
  if (info.driver_name && !strcmp(info.driver_name, "net_af_packet")) {
    minRxSize = afPacketMinRxSize(info.device);
    if (minRxSize < 0) return release(deviceError("invalid AF_PACKET framesz", -minRxSize));
  }
  if (config.ifindex) {
    auto result = Xdp::queryLink(config.ifindex);
    if (result.is<ZeException>()) return release(ZuMv(result).p<ZeException>());
    const auto &link = result.p<Xdp::LinkState>();
    if (link.mtu != mtu || memcmp(link.mac, mac.addr_bytes, sizeof(link.mac)))
      return release(deviceError("Linux link MTU or MAC differs from the DPDK port", EINVAL));
  }
  int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (fd < 0) return release(deviceError("create Rx wake eventfd", errno));

  m_config = config;
  m_port = port;
  m_wakeFD = fd;
  m_mtu = mtu;
  m_mac = mac;
  m_ring = info.driver_name && !strcmp(info.driver_name, "net_ring");
  m_pcap = pcap;
  m_afPacketMinRxSize = minRxSize;
  m_mx = mx;
  return {};
}

void Instance::reserveTxPools(ZuSpan<Zi::IOBufPool *const> pools)
{
  ZmAssert_(m_mx && Engine::state() == ZmEngineState::Stopped && !allocated());
  m_reserves.null();
  for (auto pool : pools) {
    auto native = dynamic_cast<BufPool *>(pool);
    ZmAssert_(native && (native->type == Zi::IOBufType::TxTCP ||
      native->type == Zi::IOBufType::TxUDP));
    m_reserves.push(ZmRef<BufPool>{native});
  }
}

Instance::Result Instance::alloc(Zi::IOBufPool *rxPool, const Zi::IOBufPoolList &txPools)
{
  if (!m_mx || Engine::state() != ZmEngineState::Stopped)
    return ZeEXCEPT(Error, "Zxdp.Dpdk", "initialize and stop the device before allocation");
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
    return ZeEXCEPT(Error, "Zxdp.Dpdk", "conflicting DPDK pool set");
  }

  auto ingress = dynamic_cast<BufPool *>(rxPool);
  if (!ingress || ingress->type != Zi::IOBufType::Rx ||
      ingress->size < m_mtu + RTE_ETHER_HDR_LEN || !ingress->count)
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
  ZtArray<BufPool::Prepared, ZtArrayHeapID<"Zxdp.Dpdk.Prepared">> backing;
  ZtArray<rte_mempool *, ZtArrayHeapID<"Zxdp.Dpdk.NativePools">> native;
  backing.length(tx.length());
  for (unsigned i = 0; i < tx.length(); ++i) {
    if (int rc = tx[i]->prepare(backing[i], socket))
      return deviceError("allocate DPDK Tx backing", -rc);
    native.push(backing[i].native());
  }
  AddressIndex index;
  if (int rc = index.init(native.cspan())) return deviceError("index DPDK Tx backing", -rc);
  m_pending.length(unsigned(capacity));
  m_txIndex = ZuMv(index);
  m_appTx = appTx;
  m_rxPool = ingress;
  m_txPools = ZuMv(tx);
  m_rxPool->returned([this] { returned(); });
  m_rxPool->publish(ZuMv(rxBacking));
  for (unsigned i = 0; i < m_txPools.length(); ++i) m_txPools[i]->publish(ZuMv(backing[i]));
  return {};
}

void Instance::final()
{
  ZmAssert_(Engine::state() == ZmEngineState::Stopped);
  ZmGuard guard(m_queueLock);
  resetRun();
  if (!m_mx) return;
  // A failed stop or an outstanding native reference does not permit backing
  // release. Retry the stop with the scheduler already quiescent.
  if (m_started) {
    int rc = rte_eth_dev_stop(m_port);
    ZmAssert_(!rc);
    m_started = false;
  }
  m_reapCursor = 0;
  reapTx(m_pendingCount);
  ZmAssert_(!m_pendingCount);
  m_txIndex.reset();
  if (m_rxPool) ZmAssert_(m_rxPool->final());
  for (auto &pool : m_txPools) ZmAssert_(pool->final());
  m_rxPool = nullptr;
  m_txPools.null();
  m_reserves.null();
  m_pending.null();
  m_eal.release(m_port);
  ::close(m_wakeFD);
  m_wakeFD = m_rxFD = -1;
  m_port = NoPort;
  m_mx = nullptr;
  m_config = {};
  m_mac = {};
  m_mtu = m_appTx = m_afPacketMinRxSize = 0;
  m_ring = m_pcap = false;
  m_error = {};
}

void Instance::start_()
{
  auto run = takeRunFns();
  m_receive = ZuMv(run.receive);
  m_txStopped = ZuMv(run.txStopped);
  m_error = {};
  if (!allocated() || !m_mx->running() || !m_receive) {
    m_error = ZeEXCEPT(
      Error, "Zxdp.Dpdk", "start requires allocated pools, a running mux and Rx callback");
  } else {
    auto result = activate();
    if (result.is<void>()) {
      m_active = 1;
      Engine::started(true);
      return;
    }
    m_error = ZuMv(result).p<ZeException>();
  }
  m_receive = {};
  m_txStopped = {};
  Engine::started(false);
}

Instance::Result Instance::checkPort() const
{
  rte_eth_dev_info info{};
  rte_ether_addr mac{};
  uint16_t mtu = 0;
  if (int rc = linkInfo(m_port, info, mtu, mac))
    return deviceError("query Ethernet port before activation", -rc);
  if (mtu != m_mtu || !rte_is_same_ether_addr(&mac, &m_mac))
    return deviceError("Ethernet port changed since initialization", EINVAL);
  if (m_config.ifindex) {
    auto result = Xdp::queryLink(m_config.ifindex);
    if (result.is<ZeException>()) return ZuMv(result).p<ZeException>();
    const auto &link = result.p<Xdp::LinkState>();
    if (link.mtu != m_mtu || memcmp(link.mac, m_mac.addr_bytes, sizeof(link.mac)))
      return deviceError("Linux control-plane link changed since initialization", EINVAL);
  }
  return {};
}

Instance::Result Instance::activate()
{
  ZmGuard guard(m_queueLock);
  if (m_started) {
    if (int rc = rte_eth_dev_stop(m_port))
      return deviceError("retire the prior Ethernet run", -rc);
    m_started = false;
  }
  m_reapCursor = 0;
  reapTx(m_pendingCount);
  if (m_pendingCount) return deviceError("prior Tx submissions remain outstanding", EBUSY);
  auto checked = checkPort();
  if (checked.is<ZeException>()) return checked;
  rte_eth_dev_info info{};
  if (int rc = rte_eth_dev_info_get(m_port, &info))
    return deviceError("query queue capabilities", -rc);

  rte_eth_conf config{};
  config.rxmode.mtu = m_mtu;
  // Try arrival interrupts where available. Software PMDs may also accept
  // this setting and supply no interrupt fd.
  config.intr_conf.rxq = m_config.rxInterval && !m_ring && !m_pcap && !m_afPacketMinRxSize;
  int rc = rte_eth_dev_configure(m_port, 1, 1, &config);
  if ((rc == -ENOTSUP || rc == -EINVAL) && config.intr_conf.rxq) {
    config.intr_conf.rxq = 0;
    rc = rte_eth_dev_configure(m_port, 1, 1, &config);
  }
  if (rc) return deviceError("configure Ethernet port", -rc);
  auto rx = info.default_rxconf;
  unsigned rxCount = m_rxPool->count;
  if (!m_ring && !m_pcap) {
    // Descriptor-based PMDs can allocate replacements before returning the
    // consumed descriptors. Leave a rearm batch in the same ingress mempool.
    unsigned rearm = RTE_MAX(unsigned(rx.rx_free_thresh), unsigned(Burst));
    if (rxCount <= rearm)
      return deviceError("ingress pool cannot supply an Rx rearm batch", EINVAL);
    rxCount -= rearm;
  }
  auto rxDesc = descriptors(rxCount, info.rx_desc_lim);
  auto txDesc = descriptors(m_pending.length(), info.tx_desc_lim);
  if ((rc = rte_eth_dev_adjust_nb_rx_tx_desc(m_port, &rxDesc, &txDesc)))
    return deviceError("adjust Ethernet descriptors", -rc);
  if (!rxDesc || !txDesc || rxDesc > rxCount)
    return deviceError("ingress pool is smaller than the required descriptors", EINVAL);
  const auto socket = rte_eth_dev_socket_id(m_port);
  auto tx = info.default_txconf;
  tx.offloads = 0;
  rx.offloads = 0;
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
  m_rxDesc = rxDesc;
  m_txDesc = txDesc;
  m_rxFD = config.intr_conf.rxq ? rte_eth_dev_rx_intr_ctl_q_get_fd(m_port, 0) : -1;
  m_txCleanup = 0;
  m_stopDeadline = {};
  m_wakes = m_parking = m_rxReturned = 0;
  m_rxDepthRequested = m_rxDepth = 0;
  m_stopping = 0;
  drainWake(m_wakeFD);
  return {};
}

void Instance::stop_()
{
  m_active = 0;
  signalWake(m_wakeFD, m_parking);
  // Even if stop originates in an Rx callback, clear callbacks only after
  // that invocation and its containing receive burst have returned.
  m_mx->rxRun([this] { quiesceRx(m_ring && m_started ? m_rxPool->count : 0); });
}

void Instance::quiesceRx(unsigned left)
{
  if (left) {
    // The ring PMD does not drain its backing rings on stop. After the peer
    // stops publishing, one ingress-pool capacity covers all posted buffers.
    auto count = receive(false);
    if (count && count < left) {
      m_mx->push([this, left = left - count] { quiesceRx(left); }, m_mx->rxThread());
      return;
    }
  }
  m_mx->txRun([this] {
    m_stopDeadline = monotonicNow() + ZuTime{2};
    retire();
  });
}

void Instance::retire()
{
  if (m_pendingCount) {
    cleanTx();
    reapTx(Burst);
    if (m_pendingCount && monotonicNow() < m_stopDeadline) {
      m_mx->push([this] { retire(); }, m_mx->txThread());
      return;
    }
  }
  {
    ZmGuard guard(m_queueLock);
    if (m_started) {
      if (int rc = rte_eth_dev_stop(m_port)) m_error = deviceError("stop Ethernet port", -rc);
      else m_started = false;
    }
    m_rxFD = -1;
  }
  m_reapCursor = 0;
  finishStop(m_pendingCount);
}

void Instance::finishStop(unsigned left)
{
  auto budget = RTE_MIN(left, unsigned(Burst));
  reapTx(budget);
  if (left > budget) {
    m_mx->push([this, left = left - budget] { finishStop(left); }, m_mx->txThread());
    return;
  }
  if (!m_error && m_pendingCount)
    m_error = deviceError("Ethernet Tx still owns submitted buffers after stop", ETIMEDOUT);
  if (!m_error && m_txCleanup < 0 && m_txCleanup != -ENOTSUP && m_txCleanup != -EAGAIN)
    m_error = deviceError("service Ethernet Tx completions", -m_txCleanup);
  auto txStopped = ZuMv(m_txStopped);
  m_receive = {};
  if (txStopped) txStopped();
  stoppedRun(!m_error);
}

void Instance::wake()
{
  if (m_wakeFD < 0) return;
  ++m_wakes;
  signalWake(m_wakeFD, m_parking);
  if (Engine::stopping() && !m_stopping.cmpXch(1, 0)) Engine::stopped();
}

bool Instance::send(uint8_t *packet, unsigned length)
{
  if (!ready() || !packet || !length || length > m_mtu + RTE_ETHER_HDR_LEN) return false;
  auto slot = m_txIndex.find(packet);
  if (!slot || length > slot->pool->size - unsigned(packet - slot->packet) ||
      m_pendingCount == m_pending.length() || !slot->submit()) return false;
  auto mbuf = slot->mbuf;
  mbuf->data_off = packet - static_cast<uint8_t *>(mbuf->buf_addr);
  mbuf->data_len = mbuf->pkt_len = length;
  if (!rte_eth_tx_burst(m_port, 0, &mbuf, 1)) {
    rte_pktmbuf_free_seg(mbuf);
    slot->releaseSubmission();
    return false;
  }
  m_pending[m_pendingCount++] = slot;
  return true;
}

void Instance::cleanTx()
{
  if (!m_started || m_txCleanup == -ENOTSUP) return;
  int rc = rte_eth_tx_done_cleanup(m_port, 0, Burst);
  if (rc < 0 && rc != m_txCleanup && rc != -ENOTSUP && rc != -EAGAIN)
    ZiLogEvent(deviceError("service Ethernet Tx completions", -rc));
  m_txCleanup = rc < 0 ? rc : 0;
}

unsigned Instance::reapTx(unsigned budget)
{
  unsigned retired = 0;
  if (budget > m_pendingCount) budget = m_pendingCount;
  // PMDs need not return mbuf references in submission order.
  while (budget--) {
    auto slot = m_pending[m_reapCursor];
    if (slot->retired()) {
      m_pending[m_reapCursor] = m_pending[--m_pendingCount];
      slot->releaseSubmission();
      ++retired;
    } else {
      ++m_reapCursor;
    }
    if (m_reapCursor == m_pendingCount) m_reapCursor = 0;
  }
  return retired;
}

unsigned Instance::pollTx()
{
  if (!ready() || !m_pendingCount) return 0;
  cleanTx();
  auto retired = reapTx(Burst);
  if (!m_pendingCount || retired || !m_mx->stopping()) {
    m_stopDeadline = {};
  } else {
    // Some PMDs only release their last descriptors when deactivated.
    // Let the mux reach device stop if Tx cannot progress during shutdown.
    // This instance still owns each outstanding lease until native retirement.
    auto now = monotonicNow();
    if (!m_stopDeadline) m_stopDeadline = now + ZuTime{2};
    else if (now >= m_stopDeadline) {
      m_active = 0;
      signalWake(m_wakeFD, m_parking);
    }
  }
  return retired;
}

unsigned Instance::receive(bool deliver)
{
  rte_mbuf *packets[Burst];
  unsigned count = rte_eth_rx_burst(m_port, 0, packets, Burst);
  for (unsigned i = 0; i < count; ++i) {
    auto mbuf = packets[i];
    if (deliver && m_active && mbuf->nb_segs == 1 &&
	mbuf->data_len == mbuf->pkt_len && mbuf->pkt_len &&
	mbuf->pkt_len <= m_mtu + RTE_ETHER_HDR_LEN && m_rxPool->adopt(mbuf))
      m_receive(rte_pktmbuf_mtod(mbuf, uint8_t *), mbuf->pkt_len);
    else
      rte_pktmbuf_free(mbuf);
  }
  return count;
}

void Instance::returned()
{
  if (m_active && !m_mx->invoked(m_mx->rxThread()) && !m_rxReturned.xch(1))
    signalWake(m_wakeFD, m_parking);
}

unsigned Instance::park(unsigned delay)
{
  unsigned count = 0;
  m_parking.xch(1);
  if (m_active && !m_wakes && !m_rxReturned) {
    bool interrupt = m_rxFD >= 0 && !rte_eth_dev_rx_intr_enable(m_port, 0);
    // The last probe after arming closes the packet/interrupt race.
    if (interrupt && !m_wakes && m_active) count = receive(true);
    if (!count && m_active && !m_wakes && !m_rxReturned) {
      pollfd fds[] = {{m_wakeFD, POLLIN, 0}, {m_rxFD, POLLIN, 0}};
      timespec timeout{time_t(delay / 1000000), long(delay % 1000000) * 1000};
      if (ppoll(fds, interrupt ? 2 : 1, &timeout, nullptr) > 0 &&
	  (fds[1].revents & POLLIN)) {
	uint64_t value;
	// Linux ethdev exposes the receive interrupt vector's eventfd.
	if (::read(m_rxFD, &value, sizeof(value)) < 0 && errno != EINTR && errno != EAGAIN)
	  m_rxFD = -1;
      }
    }
    if (interrupt) rte_eth_dev_rx_intr_disable(m_port, 0);
  }
  m_parking.xch(0);
  drainWake(m_wakeFD);
  return count;
}

void Instance::rx()
{
  unsigned spin = m_config.spin;
  unsigned initial = RTE_MIN(m_config.rxInterval, 4U), delay = initial;
  while (ready()) {
    if (takeWake(m_wakes)) return;
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
    if (park(delay)) {
      spin = m_config.spin;
      delay = initial;
    } else {
      delay += RTE_MIN(delay, m_config.rxInterval - delay);
    }
  }
}

void Instance::status()
{
  ZmGuard guard(m_queueLock);
  char mac[RTE_ETHER_ADDR_FMT_SIZE];
  rte_ether_format_addr(mac, sizeof(mac), &m_mac);
  ZiLOG(Info, "Zxdp.Dpdk", ([
    name = m_config.name, state = state(), index = ifindex(), mtu = m_mtu,
    address = ZuCArray<RTE_ETHER_ADDR_FMT_SIZE>{mac},
    mode = m_ring ? "zero-copy" : (m_pcap || m_afPacketMinRxSize) ? "copy" : "unverified",
    started = m_started, rxDesc = m_rxDesc, txDesc = m_txDesc
  ](auto &out) {
    out << "DPDK " << name << " state " << ZuBoxed(state)
      << " ifindex " << ZuBoxed(index) << " MTU " << ZuBoxed(mtu)
      << " MAC " << address << " mode " << mode;
    if (started)
      out << " Rx queues 1 (" << ZuBoxed(rxDesc) << " requested descriptors)"
        " Tx queues 1 (" << ZuBoxed(txDesc) << " requested descriptors)";
  }));
}

struct QueueView : public Ztc::Queue {
  ZuID		owner, id;
  rte_eth_stats	stats{};
  unsigned	depth;
  uint16_t	port;
  Ztc::QueueType::T type;
  bool		haveStats;

  QueueView(ZuID owner_, ZuID id_, uint16_t port_, Ztc::QueueType::T type_,
    const rte_eth_stats &stats_, bool haveStats_, unsigned depth_ = 0)
  : owner{owner_}, id{id_}, stats{stats_}, depth{depth_}, port{port_}, type{type_},
    haveStats{haveStats_} { }

  TelKey telKey() const override { return {owner, id, type}; }
  void telemetry(Ztc::QueueTelemetry &data) const override {
    data = {};
    data.ownerID = owner; data.id = id; data.type = type;
    if (type == Ztc::QueueType::Rx) {
      rte_eth_rxq_info info{};
      if (!rte_eth_rx_queue_info_get(port, 0, &info)) data.size = info.nb_desc;
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

unsigned Instance::allQueues(Ztc::QueueMgr::AllFn fn) const
{
  ZmGuard guard(m_queueLock);
  if (!m_started || !m_active) return 0;
  rte_eth_stats stats{};
  bool haveStats = !rte_eth_stats_get(m_port, &stats);
  auto owner = ZuID{m_mx->id()};
  auto name = ZuID{} << "dpdk." << m_config.name << '.';
  // The application Rx owner samples dataplane depth on its next turn.
  auto depth = m_rxDepth.load_();
  m_rxDepthRequested.store_(1);
  QueueView rx{owner, ZuID{name} << "rx", m_port, Ztc::QueueType::Rx, stats, haveStats, depth};
  QueueView tx{owner, ZuID{name} << "tx", m_port, Ztc::QueueType::Tx, stats, haveStats};
  fn(&rx);
  fn(&tx);
  return 2;
}

} // namespace Zxdp::Dpdk
