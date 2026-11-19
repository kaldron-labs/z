//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rte_dev.h>
#include <rte_errno.h>
#include <rte_eth_ring.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ring.h>

#include <zlib/ZmAssert.hh>

#include <zlib/ZtScratch.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZxdpDpdk.hh>
#include <zlib/ZxdpDpdkPool.hh>

#include "zxdpdkfixture.hh"

namespace ZxdpDpdkTest {

static ZeException error(const char *op, int code)
{
  return ZeEXCEPT(Error, "Zxdp.DpdkTest", ([op, code](auto &s) {
    s << op << ": " << rte_strerror(code);
  }));
}

ZtString<> errorText(const ZeException &error)
{
  ZeLogBuf msg;
  error.fn()(msg, error);
  return ZtString<>{msg.data(), msg.length()};
}

static void removeDir(const char *dir)
{
  if (rmdir(dir) < 0 && errno != ENOENT) {
    auto cause = ZeLastError;
    std::cerr << "cannot remove test directory " << dir << ": " << cause << '\n';
    abort();
  }
}

Runtime::~Runtime()
{
  if (!*m_dir) return;
  ZtString<> dpdk{ZuCSpan{m_dir}};
  dpdk << "/dpdk";
  ZtString<> prefix = dpdk;
  prefix << "/zxdp";
  removeDir(prefix);
  removeDir(dpdk);
  removeDir(m_dir);
}

Result Runtime::init()
{
  memcpy(m_dir, "/tmp/zxdpdk.XXXXXX", sizeof(m_dir));
  if (!mkdtemp(m_dir)) {
    *m_dir = 0;
    return error("create test directory", errno);
  }
  auto runtime = getenv("RUNTIME_DIRECTORY");
  ZtString<> previous{ZuCSpan{runtime}};
  if (setenv("RUNTIME_DIRECTORY", m_dir, 1) < 0)
    return error("set fixture runtime directory", errno);
  using namespace Zxdp::Dpdk;
  EALParams params{};
  params.noPCI = params.noHuge = params.noShconf = params.noTelemetry = true;
  params.iova = IOVA::VA;
  params.filePrefix = "zxdp";
  auto eal = EAL::instance();
  auto result = eal->init(params);
  if (previous)
    setenv("RUNTIME_DIRECTORY", previous, 1);
  else
    unsetenv("RUNTIME_DIRECTORY");
  return result;
}

static void detach(const char *name)
{
  if (int rc = rte_eal_hotplug_remove("vdev", name)) {
    std::cerr << "cannot remove test port " << name << ": " << rte_strerror(-rc) << '\n';
    abort();
  }
}

RingPort::~RingPort()
{
  if (m_name) detach(m_name);
  drain();
  rte_ring_free(m_tx);
  rte_ring_free(m_rx);
}

Result RingPort::init(const char *id, unsigned ringSize, rte_ether_addr mac)
{
  ZtString<> rxName, txName;
  rxName << id << "_rx";
  txName << id << "_tx";
  constexpr unsigned flags = RING_F_SP_ENQ | RING_F_SC_DEQ;
  m_rx = rte_ring_create(rxName, ringSize, SOCKET_ID_ANY, flags);
  if (!m_rx) return error("create fixture Rx ring", rte_errno);
  m_tx = rte_ring_create(txName, ringSize, SOCKET_ID_ANY, flags);
  if (!m_tx) return error("create fixture Tx ring", rte_errno);
  int port = rte_eth_from_rings(id, &m_rx, 1, &m_tx, 1, SOCKET_ID_ANY);
  if (port < 0) return error("attach fixture rings to net_ring", rte_errno);
  m_name << "net_ring_" << id;
  auto portID = uint16_t(port);
  if (int rc = rte_eth_dev_mac_addr_add(portID, &mac, 0))
    return error("set fixture port MAC", -rc);
  rte_ether_addr actual{};
  if (int rc = rte_eth_macaddr_get(portID, &actual)) return error("get fixture port MAC", -rc);
  if (!rte_is_same_ether_addr(&mac, &actual)) return error("validate fixture port MAC", EINVAL);
  return {};
}

bool RingPort::inject(rte_mempool *pool, ZuBSpan bytes, uint8_t **packet)
{
  if (!pool || !bytes || bytes.length() > UINT16_MAX) return false;
  auto mbuf = rte_pktmbuf_alloc(pool);
  if (!mbuf) return false;
  auto dest = reinterpret_cast<uint8_t *>(rte_pktmbuf_append(mbuf, uint16_t(bytes.length())));
  if (!dest) {
    rte_pktmbuf_free(mbuf);
    return false;
  }
  memcpy(dest, bytes.data(), bytes.length());
  if (rte_ring_enqueue(m_rx, mbuf)) {
    rte_pktmbuf_free(mbuf);
    return false;
  }
  if (packet) *packet = dest;
  return true;
}

static rte_mbuf *take(rte_ring *ring)
{
  void *value;
  return ring && !rte_ring_dequeue(ring, &value) ? static_cast<rte_mbuf *>(value) : nullptr;
}

rte_mbuf *RingPort::take() { return ZxdpDpdkTest::take(m_tx); }

void RingPort::drain()
{
  while (auto packet = ZxdpDpdkTest::take(m_rx)) rte_pktmbuf_free(packet);
  while (auto packet = take()) rte_pktmbuf_free(packet);
}

Result Wire::start(End a, End b)
{
  if (!a.port || !a.ingress || !b.port || !b.ingress || m_mode != Idle)
    return error("bad wire fixture endpoints", EINVAL);
  m_mode = Forward;
  int rc = m_peer.run([this, a, b] {
    bool dropping = false;
    for (;;) {
      auto mode = m_mode.load_();
      if (mode == Exit) return;
      if (mode == Drop && !dropping) {
	dropping = true;
	m_quiesced.post();
      }
      auto transfer = [dropping](End from, End to) {
	bool pending = false;
	for (unsigned n = 0; n < 32; ++n) {
	  auto packet = from.port->take();
	  if (!packet) break;
	  pending = true;
	  if (!dropping && rte_pktmbuf_is_contiguous(packet))
	    to.port->inject(to.ingress,
	      {rte_pktmbuf_mtod(packet, const uint8_t *), rte_pktmbuf_pkt_len(packet)});
	  rte_pktmbuf_free(packet);
	  if (from.retired) from.retired->post();
	}
	return pending;
      };
      bool progressed = transfer(a, b);
      progressed |= transfer(b, a);
      if (!progressed) Zm::yield();
    }
  });
  if (rc) {
    m_mode = Idle;
    return error("start software wire peer", EAGAIN);
  }
  return {};
}

void Wire::quiesce()
{
  if (m_mode == Forward) {
    m_mode = Drop;
    m_quiesced.wait();
  }
}

void Wire::stop()
{
  if (!m_peer) return;
  m_mode = Exit;
  ZmAssert_(!m_peer.join());
  m_mode = Idle;
}

struct PcapHeader {
  uint32_t magic;
  uint16_t major, minor;
  uint32_t zone, accuracy, snaplen, link;
};

struct PcapRecord {
  uint32_t seconds, fractional, captured, original;
};

static_assert(sizeof(PcapHeader) == 24 && sizeof(PcapRecord) == 16);

PcapPort::~PcapPort()
{
  if (m_attached) detach(name());
  if (m_input) unlink(m_input);
  if (m_output) unlink(m_output);
}

Result PcapPort::init(const Runtime &runtime, ZuBSpan input)
{
  m_input << runtime.dir() << "/input.pcap";
  m_output << runtime.dir() << "/output.pcap";
  ZiFile file;
  if (file.open(m_input, ZiFile::Write | ZiFile::GC) != Zi::OK)
    return error("open pcap input fixture", file.error().errNo());
  const PcapHeader header{0xa1b2c3d4U, 2, 4, 0, 0, 65535, 1};
  const PcapRecord record{0, 0, unsigned(input.length()), unsigned(input.length())};
  if (file.write(&header, sizeof(header)) != Zi::OK ||
      file.write(&record, sizeof(record)) != Zi::OK ||
      file.write(input.data(), input.length()) != Zi::OK)
    return error("write pcap input fixture", file.error().errNo());
  if (file.close() != Zi::OK) return error("close pcap input fixture", file.error().errNo());

  ZtString<> args;
  args << "rx_pcap=" << m_input << ",tx_pcap=" << m_output;
  int rc = rte_eal_hotplug_add("vdev", name(), args);
  if (rc) return error("attach fixture pcap port", -rc);
  m_attached = true;
  return {};
}

bool PcapPort::captured(ZuBSpan output) const
{
  ZiFile file;
  if (file.open(m_output, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK) return false;
  PcapHeader header;
  PcapRecord record;
  // DPDK uses libpcap's nanosecond precision when available.
  if (file.read(&header, sizeof(header)) != int(sizeof(header)) ||
      (header.magic != 0xa1b2c3d4U && header.magic != 0xa1b23c4dU) ||
      header.major != 2 || header.minor != 4 || header.link != 1 ||
      file.read(&record, sizeof(record)) != int(sizeof(record)) ||
      record.captured != output.length() || record.original != output.length())
    return false;
  using Bytes = ZtArray<uint8_t, ZtArrayHeapID<"Zxdp.PcapTest">>;
  auto bytes = ZtScratch(Bytes, output.length(), output.length());
  uint8_t extra;
  bool matches = file.read(bytes.data(), bytes.length()) == int(bytes.length()) &&
    bytes.cspan() == output && file.read(&extra, 1) == Zi::EndOfFile;
  return file.close() == Zi::OK && matches;
}

Device::~Device()
{
  ZmAssert_(stop());
  if (device) device->final();
}

Result Device::init(const char *name, unsigned rxInterval, unsigned spin, unsigned count)
{
  device = Zxdp::instance(Zxdp::Type::Dpdk);
  Zxdp::Params params(Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Dpdk::Params>{}>{});
  auto &config = params.p<Zxdp::Dpdk::Params>();
  config.name = name;
  config.rxInterval = rxInterval;
  config.spin = spin;
  auto result = device->init(&mx, params);
  if (result.is<ZeException>()) return result;
  auto size = device->mtu() + RTE_ETHER_HDR_LEN;
  ingress = device->bufPool(Zi::IOBufType::Rx, count, size);
  tx = device->bufPool(Zi::IOBufType::TxUDP, count, size);
  pools.pushNode(tx.ptr());
  return device->alloc(ingress, pools);
}

Result Device::start(Zxdp::RecvFn receive)
{
  if (!mx.running() && !mx.start())
    return ZeEXCEPT(Error, "Zxdp.DpdkTest", "start direct-device scheduler");
  if (!device->start(ZuMv(receive))) return device->error();
  return {};
}

bool Device::stop()
{
  if (device && !device->stop()) return false;
  return !mx.running() || mx.stop();
}

rte_mempool *native(Zi::IOBufPool *pool)
{
  return static_cast<Zxdp::Dpdk::BufPool *>(pool)->native();
}

} // namespace ZxdpDpdkTest
