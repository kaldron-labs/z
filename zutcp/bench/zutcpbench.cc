//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// The application, framing and dispatch are shared by all transport choices.
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <iostream>

#include <zlib/ZuArray.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuTime.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBitmap.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZfCf.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRx.hh>
#include <zlib/ZiTx.hh>

#include <zlib/Zpkt.hh>
#include <zlib/ZpktEnv.hh>

#ifdef HAVE_DPDK
#include <zlib/ZvCf.hh>
#endif

#include <zlib/Zutcp.hh>

namespace FixBench {
// Existing fixture bounds: short FIX records and capped cold sample/config storage.
enum { MaxFrame = 256, MaxSamples = 2000000, Port = 18187,
  MaxCxns = 128, MaxPipeline = 1024, MaxBurstUS = 1000000,
  MinReserve = 1024, MaxReserve = 65536 };
using Samples = ZtArray<uint64_t, ZtArrayHeapID<"Zutcp.Bench.Samples">>;
using Times = ZtArray<ZuTime, ZtArrayHeapID<"Zutcp.Bench.Times">>;

namespace Workload { enum { Order, MarketData, Updates, N }; }
namespace Frame { enum { Order, Execution, Request, Snapshot, Update, Feedback, N }; }
namespace Backend { enum { Kernel, Xdp, Afp, Dpdk }; }

// Fixed representative FIX application records, not a FIX session engine.
// MaxFrame bounds the six short records, including the largest execution report.
static const char *workloadName(unsigned kind)
{
  switch (kind) {
    case Workload::Order: return "order";
    case Workload::MarketData: return "marketdata";
    default: return "updates";
  }
}
static unsigned frameKind(unsigned workload, bool response)
{
  return (workload << 1) + unsigned(response);
}
static unsigned message(char *output, unsigned sequence, unsigned kind = Frame::Order)
{
  if (kind >= Frame::N) return 0;
  char bytes[MaxFrame];
  ZuStream body{bytes, sizeof(bytes)};
  auto seqValue = ZuBoxed(sequence);
  auto seq = seqValue.fmt<ZuFmt::Right<10>>();
  constexpr char types[] = {'D', '8', 'V', 'W', 'X', 'X'};
  body << "35=" << types[kind] << '\001'
    << (kind & 1 ? "49=PEER\00156=BENCH\001" : "49=BENCH\00156=PEER\001")
    << "34=" << seq << "\00152=20260101-00:00:00.000\001";
  switch (kind) {
    case Frame::Order:
      body << "11=" << seq << "\00155=TEST\00154=1\00160=20260101-00:00:00.000\001"
	<< "38=100\00140=2\00144=123.45\001";
      break;
    case Frame::Execution:
      body << "11=" << seq << "\00117=" << seq << "\00137=" << seq
	<< "\001150=0\00139=0\00155=TEST\00154=1\00138=100\00144=123.45\001"
	<< "151=100\00114=0\0016=0\001";
      break;
    case Frame::Request:
      body << "262=" << seq << "\001263=0\001264=1\001267=1\001269=0\001146=1\00155=TEST\001";
      break;
    case Frame::Snapshot:
      body << "262=" << seq << "\00155=TEST\001268=1\001269=0\001270=123.45\001271=100\001";
      break;
    default:
      body << "262=" << seq << "\001268=1\001279=0\001269=0\001278=" << seq
	<< "\00155=TEST\001270=123.45\001271=100\001";
      break;
  }
  if (body.overflow()) return 0;
  unsigned n = unsigned(body.data() - bytes);
  ZuStream out{output, MaxFrame};
  out << "8=FIX.4.4\0019=" << ZuBoxed(n) << '\001' << ZuCSpan{bytes, n};
  if (out.overflow()) return 0;
  unsigned sum = 0, length = unsigned(out.data() - output);
  for (unsigned i = 0; i < length; ++i) sum += uint8_t(output[i]);
  out << "10=" << ZuBoxed(sum & 255).fmt<ZuFmt::Right<3>>() << '\001';
  return out.overflow() ? 0 : unsigned(out.data() - output);
}

// Return the complete frame size or INT_MAX for a partial header/frame.
static int frameLength(const uint8_t *data, unsigned length)
{
  constexpr auto prefix = "8=FIX.4.4\0019="_z;
  unsigned n = prefix.length();
  if (memcmp(data, prefix.data(), length < n ? length : n)) return -1;
  if (length <= n) return INT_MAX;
  unsigned body = 0, digits = 0;
  while (n < length && data[n] != 1) {
    if (++digits > 3 || data[n] < '0' || data[n] > '9') return -1;
    body = body * 10 + data[n++] - '0';
  }
  if (n == length) return INT_MAX;
  if (!digits || !body || body + n + 8 > MaxFrame) return -1;
  unsigned total = n + 1 + body + 7;
  return length < total ? INT_MAX : int(total);
}

static bool valid(const uint8_t *data, unsigned length, unsigned sequence, unsigned kind)
{
  char expected[MaxFrame];
  unsigned n = message(expected, sequence, kind);
  return n && length == n && !memcmp(data, expected, n);
}

static bool selftest()
{
  constexpr unsigned sequences[] = {1, 1000, 2000000, 4000000};
  for (unsigned kind = 0; kind < Frame::N; ++kind) {
    for (unsigned sequence : sequences) {
      char data[MaxFrame];
      unsigned n = message(data, sequence, kind);
      if (!n || n > MaxFrame) return false;
      for (unsigned i = 0; i < n; ++i)
	if (frameLength(reinterpret_cast<uint8_t *>(data), i) != INT_MAX) return false;
      if (frameLength(reinterpret_cast<uint8_t *>(data), n) != int(n) ||
	  !valid(reinterpret_cast<uint8_t *>(data), n, sequence, kind) ||
	  valid(reinterpret_cast<uint8_t *>(data), n, sequence + 1, kind) ||
	  valid(reinterpret_cast<uint8_t *>(data), n, sequence, (kind + 1) % Frame::N))
	return false;
      data[n - 2] ^= 1;
      if (valid(reinterpret_cast<uint8_t *>(data), n, sequence, kind)) return false;
      data[0] = '7';
      if (frameLength(reinterpret_cast<uint8_t *>(data), n) != -1) return false;
    }
  }
  return true;
}

struct Config {
  using Spin = ZuBox<unsigned>;
  bool		server = false, ll = false, txBatch = false;
  int		xdpTxCPU = -1, afpBypass = -1;
  unsigned	backend = Backend::Kernel, messages = 10000, warmup = 1000, pipeline = 1;
  unsigned	connections = 1, reserve = 4096, timeout = 1;
  Spin		spin, rxSpin, txSpin;
  unsigned	workload = Workload::Order, burstSize = 0, burstUS = 1000, quietUS = 0;
  unsigned	socketBytes = 1<<20, rxCPU = 0, txCPU = 0;
  ZiIP		address;
  const char	*backendName = "kernel", *interface = nullptr, *routeInterface = nullptr;
  const char	*dpdkConfig = nullptr;
  const char	*samples = nullptr;

  bool native() const { return backend != Backend::Kernel; }
};

struct Result {
  ZuTime		begin, end; // Rx, read by main after owner stop
  uint64_t	bytes = 0;
  unsigned	bursts = 0, lateBursts = 0, pipelinePeak = 0;
  unsigned	received = 0, rxCPU = ~0U, rxSocketBytes = 0, txSocketBytes = 0;
  bool		native = false;
  alignas(Zm::CacheLineSize)
  uint64_t	accepted = 0; // Tx
  unsigned	txCPU = ~0U; // Tx
};
using Results = ZtArray<Result, ZtArrayHeapID<"Zutcp.Bench.Results">>;
class App;
class Cxn : public ZmNHeap<Cxn>, public ZiConnection,
  public ZiRx<Cxn, MaxFrame>, public ZiTx<Cxn> {
friend inline ZuStringT<"Zutcp.Bench.Cxn"> ZmNHeapID(Cxn *);
  using Rx = ZiRx<Cxn, MaxFrame>;
  using Tx = ZiTx<Cxn>;
public:
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  Cxn(App *, const ZiCxnInfo &, unsigned);
  void connected(ZiRxContext &) override;
  void disconnected(bool) override;
  void stopBurst() { m_stopping = true; cancelBurst(); } // Rx, before owner barriers
  Zi::IOBufAllocator *rxAllocator() const;
  int frame(const ZiRxContext &, ZiIOBuf *);
  int received(const ZiRxContext &, ZmRef<ZiIOBuf>);
  void sent(ZmRef<ZiIOBuf>, bool);
  void sending(ZiTxContext &);
  unsigned maxQueueLen() const;
private:
  void offer(unsigned);
  void fill();
  void beginPhase();
  void scheduleBurst();
  void cancelBurst();
private:
  using Timer = ZmScheduler::Timer;
  App		*m_app;
  const unsigned	m_id;
  const unsigned	m_pipeline, m_burstSize;
  const ZuTime	m_burstInterval;
  alignas(Zm::CacheLineSize)
  Times		m_times; // Rx: one timestamp per admitted application request
  unsigned	m_offered = 0, m_received = 0, m_limit = 0; // Rx
  unsigned	m_burstRemaining = 0; // Rx: application offer tokens only
  uint64_t	m_burstSerial = 0; // Rx: invalidates queued prior-phase callbacks
  bool		m_burstArmed = false, m_stopping = false; // Rx
  ZuTime		m_nextBurst;
  Timer		m_burstTimer; // existing scheduler, same Rx owner
  alignas(Zm::CacheLineSize)
  unsigned	m_accepted = 0; // Tx
};

using Owners = ZtArray<ZmRef<Cxn>, ZtArrayHeapID<"Zutcp.Bench.Owners">>;

using Node = ZfCf::AnyNode;

static void value(Node *parent, ZuCSpan key, auto value)
{
  Node::String text;
  text << value;
  parent->data<Node::Object>().push(Node::Field{
    key, ZfCf::newNode<Node::String>(parent, ZuMv(text))});
}

static void initEnv(Zpkt::Env *env, const Config &cf, Zpkt::CfString &module)
{
#ifdef HAVE_DPDK
  if (cf.backend == Backend::Dpdk) {
    auto loaded = ZvCf::load(cf.dpdkConfig);
    auto root = loaded.p<1>().ptr();
    if (!root || !root->has<Node::Object>())
      throw ZeEXCEPT(Error, "zutcpbench", "BENCH_DPDK_CONFIG must contain an Env configuration");
    bool haveModule = false;
    for (const auto &field : root->data<Node::Object>())
      if (field.p<0>() == "module"_z) { haveModule = true; break; }
    // ZvCf allocated this tree for this invocation. Supply the build-tree
    // module when the file only describes EAL and its interface mappings.
    if (!haveModule) value(const_cast<Node *>(root), "module", ZUTCP_DPDK_MODULE);
    Zpkt::PluginLoadCf config;
    ZfCf::handler<Zpkt::PluginLoadCf>(root).update(config);
    module = ZuMv(config.module);
    env->init(root);
    return;
  }
#endif

  auto root = ZfCf::newNode<Node::Object>(nullptr);
  auto interfaces = ZfCf::newNode<Node::Object>(root.ptr());
  auto nif = ZfCf::newNode<Node::Object>(interfaces.ptr());
  bool xdp = cf.backend == Backend::Xdp;
  module = xdp ? ZUTCP_XDP_MODULE : ZUTCP_AFP_MODULE;
  value(root.ptr(), "module", module);
  if (*cf.rxSpin) value(nif.ptr(), "rxSpin", cf.rxSpin.val());
  if (xdp) {
    value(nif.ptr(), "txSid", 3);
    if (*cf.txSpin) value(nif.ptr(), "txSpin", cf.txSpin.val());
  } else if (cf.afpBypass >= 0) {
    value(nif.ptr(), "qdiscBypass", cf.afpBypass ? "true" : "false");
  }
  interfaces->data.push(Node::Field{cf.interface, ZuMv(nif)});
  root->data.push(Node::Field{"interfaces", ZuMv(interfaces)});
  env->init(root.ptr());
}

struct PacketEnvOwner {
  ZmRef<Zpkt::Env>	env = new Zpkt::Env;
};

class App : private PacketEnvOwner, public ZiMultiplex {
public:
  explicit App(const Config &config, ZiMxParams params) :
    ZiMultiplex{ZuMv(params), config.native() ?
      ZmRef<Zutcp::MxPlugin>{new Zutcp::MxPlugin} : ZmRef<Zutcp::MxPlugin>{}},
    m_config{config} {
    if (config.native()) {
      ZmRef<Zpkt::Nif> nif;
      Zutcp::MxPlugin::Params settings{.packetCount = config.reserve, .txBatch = config.txBatch};
      auto instance = static_cast<Zutcp::MxPlugin *>(plugin());
      try {
	initEnv(env, config, m_module);
	ZuCSpan name = config.interface;
	if (name == "_loop"_z && !env->loopback())
	  throw ZeEXCEPT(Error, "zutcpbench", "the loaded packet backend lacks loopback");
	auto route = config.routeInterface;
	if (!route && config.backend == Backend::Dpdk && name != "_loop"_z)
	  route = config.interface;
	if (route && !(settings.routingIfindex = if_nametoindex(route)))
	  throw ZeEXCEPT(Error, "zutcpbench", ([route = ZeString{route}](auto &out) {
	    out << "failed to resolve routing interface " << route;
	  }));
	auto opened = env->nif(this, name);
	if (opened.is<ZeException>()) throw ZuMv(opened).p<ZeException>();
	nif = opened.p<Zpkt::Nif *>();
	if (!settings.routingIfindex) settings.routingIfindex = nif->ifindex();
	auto openedNif = nif.ptr();
	instance->init(this, env, ZuMv(nif), settings);
	m_nif = openedNif;
	m_routeIfindex = settings.routingIfindex;
      } catch (ZeException &e) {
	if (nif) {
	  nif->final();
	  if (auto error = nif->error()) ZiLogEvent(ZuMv(error));
	}
	ZiLogEvent(ZuMv(e));
	m_failed = 1;
	return;
      }
      std::cout << "native_configuration control_count=" << settings.controlBufCount
	<< " control_credits=" << settings.txControlCredits << " rx_retain="
	<< settings.rxRetainFramesPerCxn << " max_data="
	<< settings.maxData << "\n";
    }
    m_network = allocator<Zi::IOBufType::Rx, ZiRxBuf, 2048>(config.reserve);
    m_frames = allocator<Zi::IOBufType::App, ZiRxBuf, MaxFrame>(config.reserve);
    m_output = allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 1024,
      ZiIOBuf_DefltAlign, ZuStringT<"Zutcp.Bench.Output">>(config.reserve);
    if (!m_network || !m_frames || !m_output) {
      m_failed = 1;
      return;
    }
    std::cout << "allocator_geometry network_size=" << m_network->size << " network_head="
      << m_network->headRoom << " network_tail=" << m_network->tailRoom << " tx_size="
      << m_output->size << " tx_head=" << m_output->headRoom << " tx_tail="
      << m_output->tailRoom << " app_capacity=" << m_frames->capacity() << "\n";
    m_results.length(config.connections);
    m_owners.length(config.connections);
    if (!config.server) {
      m_samples.length(uint64_t(config.messages) * config.connections);
      memset(m_samples.data(), 0, m_samples.length() * sizeof(uint64_t));
      if (config.quietUS) m_quietSamples.length(config.messages);
    }
  }
  ~App() { shutdown(); }
  const Config &config() const { return m_config; }
  Zi::IOBufAllocator *frames() const { return m_frames; }
  Zi::IOBufAllocator *output() const { return m_output; }
  Result &result(unsigned id) { return m_results[id]; }
  void measurementStarted(unsigned id, ZuTime now) {
    m_results[id].begin = now;
    if (id && m_config.quietUS) ++m_busy;
  }
  void measurementEnded(unsigned id, ZuTime now) {
    m_results[id].end = now;
    if (id && m_config.quietUS) --m_busy;
  }
  void sample(unsigned id, unsigned sequence, uint64_t nanos) {
    m_samples[uint64_t(id) * m_config.messages + sequence] = nanos;
    if (!id && m_busy) m_quietSamples[m_quietCount++] = nanos;
  }
  void failure(const char *reason = "unspecified", unsigned id = ~0U,
      unsigned sequence = 0, unsigned detail = 0) {
    if (m_failed.xch(1)) return;
    ZiLOG(Error, "zutcpbench", ([reason = ZeString{reason}, id, sequence, detail](auto &out) {
      out << "first_failure reason=" << reason << " id=" << id
	<< " sequence=" << sequence << " detail=" << detail;
    }));
    m_done.post();
  }
  bool failed() const { return m_failed.load_(); }
  void closed(bool ok) {
    if (!ok) m_failed = 1;
    if (++m_closed == m_config.connections) m_done.post();
  }
  ZiConnection *connected(const ZiCxnInfo &info) {
    if (m_created == m_config.connections) { failure(); return nullptr; }
    unsigned id = m_created++;
    m_owners[id] = new Cxn{this, info, id};
    return m_owners[id].ptr();
  }
  void failedConnect(bool) { failure(); m_ready.post(); }
  void listening(const ZiListenInfo &) {
    std::cout << "ready=1\n" << std::flush; m_ready.post();
  }
  bool run() {
    if (failed() || !start()) return false;
    if (m_nif) {
      ZuCArray<18> mac;
      for (auto octet : m_nif->mac()) {
	if (mac.length()) mac << ':';
	mac << ZuBoxed(unsigned(octet)).hex<false, ZuFmt::Right<2>>();
      }
      std::cout << "packet_backend module=" << m_module << " selected_interface="
	<< m_config.interface << " packet_ifindex=" << m_nif->ifindex()
	<< " routing_ifindex=" << m_routeIfindex << " mac=" << mac << " mtu=" << m_nif->mtu()
	<< " rx_queues=" << m_nif->rxQueues() << " zero_copy=" << unsigned(m_nif->zeroCopy())
	<< " tcp_checksum_offload=" << unsigned(m_nif->tcpChksumOffload()) << '\n';
      m_nif->status();
      packetDrops("activation");
    }
    if (m_config.server) {
      listen(ZiListenFn{this, ZmFnPtr<&App::listening>{}},
	ZiFailFn{this, ZmFnPtr<&App::failedConnect>{}},
	ZiConnectFn{this, ZmFnPtr<&App::connected>{}},
	m_config.address, Port, m_config.connections, m_network, ZiCxnOptions{});
      if (m_ready.timedwait(Zm::now(30))) return false;
    } else {
      for (unsigned i = 0; i < m_config.connections; ++i)
	connect(ZiConnectFn{this, ZmFnPtr<&App::connected>{}},
	  ZiFailFn{this, ZmFnPtr<&App::failedConnect>{}}, ZiIP{}, 0,
	  m_config.address, Port, m_network, ZiCxnOptions{});
    }
    bool completed = !m_done.timedwait(Zm::now(120)) && !failed();
    if (m_nif) {
      packetDrops("through_exchange");
      if (m_config.backend == Backend::Afp) m_nif->status();
    }
    return completed;
  }
  void shutdown() {
    if (m_shutdown) return;
    m_shutdown = true;
    if (running()) {
      if (m_config.server) stopListening(m_config.address, Port);
      ZmBlock<>{}([this](auto wake) {
	rxRun([this, wake = ZuMv(wake)]() mutable {
	  for (auto &cxn : m_owners) if (cxn) cxn->stopBurst();
	  wake();
	});
      });
    }
    if (!stop()) m_failed = 1;
    // App owns every Cxn through cancellation and the existing mx owner barriers.
    // Already-dispatched raw callbacks cannot outlive this release boundary.
    m_owners.length(0);
  }
  bool report(); // Called only after shutdown and its owner barriers.
private:
  void packetDrops(const char *interval) {
    uint64_t full = 0;
    unsigned rxQueues = 0;
    m_nif->allQueues([&full, &rxQueues](Ztc::Queue *queue) {
      Ztc::QueueTelemetry data;
      queue->telemetry(data);
      if (data.type != Ztc::QueueType::Rx) return;
      full += data.full;
      ++rxQueues;
    });
    if (!rxQueues) return;
    const char *source = m_config.backend == Backend::Xdp ? "XDP_STATISTICS.rx_ring_full" :
      m_config.backend == Backend::Afp ? "PACKET_STATISTICS.tp_drops" : "rte_eth_stats.imissed";
    std::cout << "packet_drops source=" << source << " interval=" << interval
      << " cumulative_rx_full=" << full << "\n";
  }

  using Alloc = Zi::IOBufAllocator;
  using Flag = ZmAtomic<unsigned>;
  Config		m_config;
  Zpkt::Nif		*m_nif = nullptr; // owned by the transport plugin after init
  Zpkt::CfString	m_module;
  unsigned		m_routeIfindex = 0;
  Alloc		*m_network = nullptr, *m_frames = nullptr, *m_output = nullptr;
  Results		m_results;
  Owners		m_owners; // bounded App ownership through mx stop
  Samples		m_samples, m_quietSamples;
  ZmSemaphore	m_ready, m_done;
  Flag		m_failed = 0;
  unsigned	m_created = 0, m_closed = 0; // Rx
  unsigned	m_busy = 0, m_quietCount = 0; // Rx, read after owner stop
  bool		m_shutdown = false; // main: explicit shutdown and destructor share one stop
};

Cxn::Cxn(App *app, const ZiCxnInfo &info, unsigned id) :
  ZiConnection{app, info}, m_app{app}, m_id{id},
  m_pipeline{!id && !app->config().server && app->config().quietUS ? 1 : app->config().pipeline},
  m_burstSize{!id && !app->config().server && app->config().quietUS ?
    1 : app->config().burstSize},
  m_burstInterval{ZuTime::Nano{uint64_t(
    !id && !app->config().server && app->config().quietUS ?
      app->config().quietUS : app->config().burstUS) * 1000}} {
  m_times.length(m_pipeline);
}

Zi::IOBufAllocator *Cxn::rxAllocator() const { return m_app->frames(); }
unsigned Cxn::maxQueueLen() const { return m_pipeline + 1; }

void Cxn::connected(ZiRxContext &io)
{
  auto &result = m_app->result(m_id);
  result.rxCPU = unsigned(sched_getcpu());
  result.native = bool(plugin());
  if (result.native != m_app->config().native() ||
      result.native != Zi::nullSocket(info().socket)) {
    m_app->failure(); io.disconnect(); return;
  }
  if (!plugin()) {
    int enabled = 1;
    if (setsockopt(info().socket, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled))) {
      m_app->failure(); io.disconnect(); return;
    }
    socklen_t bytes = sizeof(unsigned);
    if (getsockopt(info().socket, SOL_SOCKET, SO_RCVBUF, &result.rxSocketBytes, &bytes) ||
	getsockopt(info().socket, SOL_SOCKET, SO_SNDBUF, &result.txSocketBytes, &bytes)) {
      m_app->failure(); io.disconnect(); return;
    }
    int value = 0;
    socklen_t size = sizeof(value);
    if (getsockopt(info().socket, IPPROTO_TCP, TCP_NODELAY, &value, &size) || !value) {
      m_app->failure(); io.disconnect(); return;
    }
  }
  Rx::recv<&Cxn::frame, &Cxn::received>(io);
  if (!m_app->config().server) {
    m_limit = m_app->config().warmup;
    if (!m_limit) {
      m_limit = m_app->config().messages;
      m_app->measurementStarted(m_id, Zm::now());
    }
    beginPhase();
    fill();
  }
}

void Cxn::offer(unsigned sequence)
{
  char bytes[MaxFrame];
  unsigned length = message(bytes, sequence,
    frameKind(m_app->config().workload, m_app->config().server));
  auto buf = m_app->output()->allocBuf(this, length, info().mps);
  if (!length || !buf || buf->skip > buf->size || length > buf->size - buf->skip) {
    m_app->failure(!buf ? "tx-allocation" : "tx-geometry", m_id, sequence, length);
    disconnect(); return;
  }
  memcpy(buf->data(), bytes, length);
  buf->length = length;
  Tx::send(ZmRef<ZiIOBuf>::acquire(static_cast<ZiIOBuf *>(ZuMv(buf).release())));
}

void Cxn::cancelBurst()
{
  ++m_burstSerial;
  mx()->del(&m_burstTimer); // invalidate queued work and delete the future timer
  m_burstArmed = false;
}

void Cxn::beginPhase()
{
  cancelBurst();
  m_burstRemaining = m_burstSize;
  if (m_burstSize) {
    m_nextBurst = Zm::now() + m_burstInterval;
    ++m_app->result(m_id).bursts;
  }
}

void Cxn::scheduleBurst()
{
  if (m_burstArmed || m_stopping || !up() || m_offered >= m_limit) return;
  auto now = Zm::now();
  if (m_nextBurst <= now) {
    ++m_app->result(m_id).lateBursts;
    // Do not replay a backlog of missed application bursts on an I/O owner.
    m_nextBurst = now + m_burstInterval;
  }
  m_burstArmed = true;
  mx()->add(&m_burstTimer, m_nextBurst, ZmScheduler::Update,
    [this, serial = m_burstSerial](auto &&arm) {
      return arm([this, serial] {
	if (m_burstSerial != serial) return;
	m_burstArmed = false;
	if (m_stopping || !up()) return;
	m_burstRemaining = m_burstSize;
	m_nextBurst = Zm::now() + m_burstInterval;
	++m_app->result(m_id).bursts;
	fill();
      });
    }, mx()->rxThread());
}

void Cxn::fill()
{
  while (!m_stopping && m_offered < m_limit && m_offered - m_received < m_pipeline &&
      (!m_burstSize || m_burstRemaining)) {
    if (m_burstSize) --m_burstRemaining;
    m_times[m_offered % m_pipeline] = Zm::now();
    offer(++m_offered);
    auto &peak = m_app->result(m_id).pipelinePeak;
    if (m_offered - m_received > peak) peak = m_offered - m_received;
    if (m_app->failed()) return;
  }
  if (m_burstSize && !m_burstRemaining) scheduleBurst();
}

int Cxn::frame(const ZiRxContext &, ZiIOBuf *buf)
{
  int length = frameLength(buf->data(), buf->length);
  if (length < 0) m_app->failure("rx-framing", m_id, m_received + 1, buf->length);
  return length;
}

int Cxn::received(const ZiRxContext &, ZmRef<ZiIOBuf> buf)
{
  auto &config = m_app->config();
  if (m_received >= config.warmup + config.messages ||
      !valid(buf->data(), buf->length, m_received + 1,
	frameKind(config.workload, !config.server))) {
    m_app->failure("rx-corpus", m_id, m_received + 1, buf->length); return -1;
  }
  unsigned sequence = ++m_received;
  auto &result = m_app->result(m_id);
  result.received = sequence;
  result.bytes += buf->length;
  if (config.server) offer(sequence);
  else {
    ZuTime now = Zm::now();
    if (sequence > config.warmup &&
	now <= m_times[(sequence - 1) % m_pipeline]) {
      m_app->failure("rx-time", m_id, sequence); return -1;
    }
    if (sequence > config.warmup)
      m_app->sample(m_id, sequence - config.warmup - 1,
	uint64_t((now - m_times[(sequence - 1) % m_pipeline]).nanosecs()));
    if (sequence == config.warmup) {
      m_app->measurementStarted(m_id, now);
      m_limit = config.warmup + config.messages;
      beginPhase();
    }
    if (sequence == config.warmup + config.messages) {
      m_app->measurementEnded(m_id, now);
      stopBurst();
      disconnect(); // All exact echoed requests have arrived; normal FIN/drain.
    } else fill();
  }
  return int(buf->length);
}

void Cxn::sending(ZiTxContext &)
{
  auto &result = m_app->result(m_id);
  if (result.txCPU == ~0U) result.txCPU = unsigned(sched_getcpu());
}

void Cxn::sent(ZmRef<ZiIOBuf>, bool ok)
{
  if (!ok) { m_app->failure("tx-completion", m_id, m_accepted + 1, m_accepted); return; }
  ++m_accepted;
  m_app->result(m_id).accepted = m_accepted;
  // The client closes after its final exact response. The server waits for EOF,
  // preserving the ordinary peer-close drain instead of racing two local closes.
}

void Cxn::disconnected(bool)
{
  stopBurst();
  if (m_received != m_app->config().warmup + m_app->config().messages)
    m_app->failure("early-disconnect", m_id, m_received, m_offered);
  m_app->closed(m_received == m_app->config().warmup + m_app->config().messages);
}

static int compare(const void *a, const void *b)
{
  auto x = *static_cast<const uint64_t *>(a), y = *static_cast<const uint64_t *>(b);
  return (x > y) - (x < y);
}

bool App::report()
{
  bool ok = !failed() && m_closed == m_config.connections;
  if (!ok)
    ZiLOG(Error, "zutcpbench", ([failed = unsigned(failed()), created = m_created,
      closed = m_closed, expected = m_config.connections](auto &out) {
      out << "completion failed=" << failed << " created=" << created
	<< " closed=" << closed << " expected=" << expected;
    }));
  if (!m_results.length()) return false;
  uint64_t bytes = 0;
  ZuTime begin, end;
  for (unsigned i = 0; i < m_config.connections; ++i) {
    const auto &r = m_results[i];
    ok &= r.received == m_config.warmup + m_config.messages && r.accepted == r.received;
    bytes += r.bytes;
    std::cout << "connection id=" << i << " plugin_actual=" << unsigned(r.native)
      << " rx_cpu_actual=" << r.rxCPU << " tx_cpu_actual=" << r.txCPU
      << " kernel_rcvbuf_actual=" << r.rxSocketBytes << " kernel_sndbuf_actual="
      << r.txSocketBytes << " pipeline_peak=" << r.pipelinePeak << " application_bursts="
      << r.bursts << " late_bursts=" << r.lateBursts << " received=" << r.received
      << " accepted=" << r.accepted << "\n";
    ok &= r.native == m_config.native() &&
      r.rxCPU == m_config.rxCPU && r.txCPU == m_config.txCPU;
    if (*r.begin && (!*begin || r.begin < begin)) begin = r.begin;
    if (*r.end && (!*end || r.end > end)) end = r.end;
  }
  std::cout << "result role=" << (m_config.server ? "server" : "client") << " backend="
    << m_config.backendName << " exact=" << unsigned(ok) << " connections="
    << m_config.connections << " pipeline=" << m_config.pipeline << " warmup="
    << m_config.warmup << " measured_per_connection=" << m_config.messages << " workload="
    << workloadName(m_config.workload) << " burst_size=" << m_config.burstSize
    << " burst_us=" << m_config.burstUS << " quiet_us=" << m_config.quietUS
    << " received_fix_bytes="
    << bytes << "\n";
  if (!m_config.server && ok) {
    if (m_config.samples) {
      ZiFile file{m_config.samples, ZiFile::Write | ZiFile::GC};
      if (!file) return false;
      constexpr auto header = "connection,sequence,rtt_ns\n"_z;
      if (file.write(header.data(), header.length()) != Zi::OK) return false;
      // Two unsigned fields, uint64 nanoseconds and CSV delimiters fit in64 bytes.
      enum { CSVLineSize = 64 };
      for (unsigned i = 0; i < m_config.connections; ++i)
	for (unsigned j = 0; j < m_config.messages; ++j) {
	  ZuArray<char, CSVLineSize> line;
	  line << i << ',' << j + 1 << ','
	    << m_samples[uint64_t(i) * m_config.messages + j] << '\n';
	  if (file.write(line.data(), line.length()) != Zi::OK) return false;
	}
      if (file.close() != Zi::OK) return false;
    }
    qsort(m_samples.data(), m_samples.length(), sizeof(uint64_t), compare);
    uint64_t n = m_samples.length();
    double seconds = double((end - begin).nanosecs())/1e9;
    if (!*begin || !*end || seconds <= 0) return false;
    char bytes[MaxFrame];
    unsigned request = message(bytes, 1, frameKind(m_config.workload, false));
    unsigned response = message(bytes, 1, frameKind(m_config.workload, true));
    std::cout << "measurement samples=" << n << " envelope_seconds="
      << ZuBoxed(seconds).fp<9>() << " requests_per_second="
      << ZuBoxed(double(n)/seconds).fp<3>() << " p50_ns="
      << m_samples[(n - 1)/2] << " p99_ns="
      << m_samples[((n - 1) * 99)/100] << " max_ns="
      << m_samples[n - 1] << "\n";
    if (m_config.quietUS) {
      if (!m_quietCount) return false;
      qsort(m_quietSamples.data(), m_quietCount, sizeof(uint64_t), compare);
      unsigned last = m_quietCount - 1;
      std::cout << "quiet_measurement samples=" << m_quietCount
	<< " p50_ns=" << m_quietSamples[last/2]
	<< " p99_ns=" << m_quietSamples[(last * uint64_t(99))/100]
	<< " p999_ns=" << m_quietSamples[(last * uint64_t(999))/1000] << "\n";
    }
    std::cout << "application_bytes request_frame=" << request << " response_frame=" << response
      << " measured_request=" << (n * request) << " measured_response="
      << (n * response) << " request_bytes_per_second="
      << ZuBoxed(double(n * request)/seconds).fp<3>() << " response_bytes_per_second="
      << ZuBoxed(double(n * response)/seconds).fp<3>() << "\n";
  }
  if (m_config.native()) {
    auto stack = static_cast<Zutcp::MxPlugin *>(plugin());
    auto cxns = stack->cxnAdmission().total;
    auto outstanding = stack->txAdmission().outstanding;
    std::cout << "native_stopped running=" << unsigned(running()) << " cxns=" << cxns
      << " tx_outstanding=" << outstanding << "\n";
    ok &= !running() && !cxns && !outstanding;
  }
  return ok;
}
} // FixBench

static void usage()
{
  std::cerr << "zutcpbench server|client kernel|native|xdp|afp|dpdk INTERFACE ADDRESS "
    "MESSAGES WARMUP "
    "PIPELINE CONNECTIONS RESERVE RX_CPU TX_CPU SPIN|default [SAMPLES.csv]\n"
    "Environment: BENCH_WORKLOAD=order|marketdata|updates BENCH_BURST_SIZE=0 "
    "BENCH_BURST_US=1000 BENCH_LL=0|1 BENCH_RX_SPIN=N BENCH_TX_SPIN=N BENCH_TX_BATCH=0|1\n"
    "Packet backends: BENCH_AFP_QDISC_BYPASS=0|1 BENCH_DPDK_CONFIG=/path "
    "BENCH_ROUTE_INTERFACE=NAME\n"
    "zutcpbench --selftest\n";
  exit(2);
}

int main(int argc, char **argv)
{
  using namespace FixBench;
  if (argc == 2 && !strcmp(argv[1], "--selftest")) return selftest() ? 0 : 1;
  if (argc != 13 && argc != 14) usage();
  Config config;
  config.server = !strcmp(argv[1], "server");
  if (!config.server && strcmp(argv[1], "client")) usage();
  config.backendName = argv[2];
  config.interface = argv[3];
  if (!strcmp(argv[2], "native") || !strcmp(argv[2], "xdp")) config.backend = Backend::Xdp;
  else if (!strcmp(argv[2], "afp")) config.backend = Backend::Afp;
  else if (!strcmp(argv[2], "dpdk")) {
#ifdef HAVE_DPDK
    config.backend = Backend::Dpdk;
    config.dpdkConfig = getenv("BENCH_DPDK_CONFIG");
    if (!config.dpdkConfig || !*config.dpdkConfig) {
      std::cerr << "dpdk requires BENCH_DPDK_CONFIG\n";
      return 2;
    }
#else
    std::cerr << "zutcpbench was built without DPDK support\n";
    return 2;
#endif
  } else if (strcmp(argv[2], "kernel")) usage();
  if (config.native()) {
    if (!*config.interface) usage();
    config.routeInterface = getenv("BENCH_ROUTE_INTERFACE");
    if (auto value = getenv("BENCH_TX_BATCH")) {
      if (strcmp(value, "0") && strcmp(value, "1")) usage();
      config.txBatch = *value == '1';
    }
  }
  if (config.backend == Backend::Afp) {
    if (auto value = getenv("BENCH_AFP_QDISC_BYPASS")) {
      if (strcmp(value, "0") && strcmp(value, "1")) usage();
      config.afpBypass = *value == '1';
    }
  }
  config.address = ZiIP{argv[4]};
  config.messages = unsigned(ZuBox<unsigned>{argv[5]});
  config.warmup = unsigned(ZuBox<unsigned>{argv[6]});
  config.pipeline = unsigned(ZuBox<unsigned>{argv[7]});
  config.connections = unsigned(ZuBox<unsigned>{argv[8]});
  config.reserve = unsigned(ZuBox<unsigned>{argv[9]});
  config.rxCPU = unsigned(ZuBox<unsigned>{argv[10]});
  config.txCPU = unsigned(ZuBox<unsigned>{argv[11]});
  if (strcmp(argv[12], "default")) config.spin = unsigned(ZuBox<unsigned>{argv[12]});
  if (auto value = getenv("BENCH_RX_SPIN")) {
    config.rxSpin = ZuBox<unsigned>{value};
    if (!*config.rxSpin) usage();
  }
  if (auto value = getenv("BENCH_TX_SPIN")) {
    config.txSpin = ZuBox<unsigned>{value};
    if (!*config.txSpin) usage();
  }
  if (auto value = getenv("BENCH_LL")) {
    if (strcmp(value, "0") && strcmp(value, "1")) usage();
    config.ll = *value == '1';
  }
  if (auto value = getenv(
      config.server ? "BENCH_SERVER_XDP_TX_CPU" : "BENCH_CLIENT_XDP_TX_CPU")) {
    config.xdpTxCPU = int(ZuBox<int>{value});
    cpu_set_t allowed;
    if (config.xdpTxCPU < 0 || config.xdpTxCPU >= CPU_SETSIZE ||
	sched_getaffinity(0, sizeof(allowed), &allowed) ||
	!CPU_ISSET(config.xdpTxCPU, &allowed)) usage();
  }
  if (argc == 14) config.samples = argv[13];
  if (auto value = getenv("BENCH_WORKLOAD")) {
    if (!strcmp(value, "order")) config.workload = Workload::Order;
    else if (!strcmp(value, "marketdata")) config.workload = Workload::MarketData;
    else if (!strcmp(value, "updates")) config.workload = Workload::Updates;
    else usage();
  }
  if (auto value = getenv("BENCH_BURST_SIZE"))
    config.burstSize = unsigned(ZuBox<unsigned>{value});
  if (auto value = getenv("BENCH_BURST_US")) config.burstUS = unsigned(ZuBox<unsigned>{value});
  if (auto value = getenv("BENCH_QUIET_US")) config.quietUS = unsigned(ZuBox<unsigned>{value});
  if (!config.messages || !config.pipeline || !config.connections ||
      config.connections > MaxCxns || config.warmup > MaxSamples ||
      uint64_t(config.messages) * config.connections > MaxSamples ||
      config.pipeline > MaxPipeline || config.burstSize > MaxPipeline ||
      (config.burstSize && (!config.burstUS || config.burstUS > MaxBurstUS)) ||
      (config.quietUS && (config.quietUS > MaxBurstUS || config.connections < 2)) ||
      config.reserve < MinReserve || config.reserve > MaxReserve) usage();
  ZiLog::init("zutcpbench");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZiMxParams params;
  params.rxBufSize(config.native() ? config.reserve : config.socketBytes)
    .txBufSize(config.socketBytes).rxQuantum(8)
    .rxThread(1).txThread(2).scheduler([&config](auto &s) {
      ZmBitmap rx, tx, sender;
      rx.set(config.rxCPU); tx.set(config.txCPU);
      sender.set(config.xdpTxCPU < 0 ? config.rxCPU : unsigned(config.xdpTxCPU));
      if (*config.spin) s.spin(config.spin.val());
      s.nThreads(3).queueSize(1<<16).ll(config.ll).quantum(ZuTime{ZuTime::Nano{1000}})
	.timeout(config.timeout)
	.thread(1, [&rx](auto &t) { t.isolated(true).cpuset(rx); })
	.thread(2, [&tx](auto &t) { t.isolated(true).cpuset(tx); })
	.thread(3, [&sender](auto &t) { t.isolated(true).cpuset(sender); });
    });
  bool xdp = config.backend == Backend::Xdp;
  std::cout << "config backend=" << config.backendName << " selected_interface="
    << config.interface << " rx_cpu="
    << config.rxCPU << " tx_cpu=" << config.txCPU
    << " xdp_tx_cpu=" << (config.xdpTxCPU < 0 ? int(config.rxCPU) : config.xdpTxCPU)
    << " scheduler_threads=3 rx_thread=1 tx_thread=2 xdp_tx_sid=3 native_xdp_sender_threads="
    << unsigned(xdp) << " spin="
    << (*config.spin ? config.spin.val() : ZmSchedParams{}.spin())
    << " rx_spin=" << (xdp || config.backend == Backend::Afp ?
      int64_t(*config.rxSpin ? config.rxSpin.val() :
      (Zi_DefltSpin<<2)) : -1)
    << " tx_spin=" << (xdp ? int64_t(*config.txSpin ? config.txSpin.val() : Zi_DefltSpin) : -1)
    << " tx_batch=" << unsigned(config.txBatch)
    << " afp_qdisc_bypass_override=" << config.afpBypass
    << " spin_inherited=" << unsigned(!*config.spin) << " timeout_ms="
    << config.timeout << " quantum_ns=1000 ll=" << unsigned(config.ll)
    << " queue=65536 kernel_socket_bytes_requested="
    << (config.native() ? 0 : config.socketBytes) << " native_rx_frames_requested="
    << (config.native() ? config.reserve : 0) << " reserve_requested=" << config.reserve
    << " kernel_allocator_count_is_not_a_pool_limit=1\n";
  rusage before{}, after{};
  getrusage(RUSAGE_SELF, &before);
  bool ok;
  {
    App app{config, ZuMv(params)};
    ok = app.run();
    app.shutdown();
    getrusage(RUSAGE_SELF, &after);
    ok = app.report() && ok;
  }
  auto seconds = [](const timeval &v) { return double(v.tv_sec) + double(v.tv_usec)/1e6; };
  std::cout
    << "process_resources through_owner_stop=1 includes_setup_warmup=1 "
       "excludes_report_sort_csv=1 user_seconds="
    << ZuBoxed(seconds(after.ru_utime) - seconds(before.ru_utime)).fp<6>()
    << " system_seconds="
    << ZuBoxed(seconds(after.ru_stime) - seconds(before.ru_stime)).fp<6>() << " maxrss_kib="
    << after.ru_maxrss << " voluntary_switches=" << after.ru_nvcsw - before.ru_nvcsw
    << " involuntary_switches=" << after.ru_nivcsw - before.ru_nivcsw << "\n";
  ZiLog::stop();
  return ok ? 0 : 1;
}
