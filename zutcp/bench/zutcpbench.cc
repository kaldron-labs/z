//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// The application, framing and dispatch are identical for both transport choices.
#include <limits.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sched.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include <iostream>

#include <zlib/ZiFile.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuArray.hh>
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
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRx.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZutcpMx.hh>

namespace FixBench {
// Existing fixture bounds: short FIX records and capped cold sample/config storage.
enum { MaxFrame = 256, MaxSamples = 2000000, Port = 18187,
  MaxCxns = 128, MaxPipeline = 1024, MaxBurstUS = 1000000,
  MinReserve = 1024, MaxReserve = 65536 };
using Samples = ZtArray<uint64_t, ZtArrayHeapID<"Zutcp.Bench.Samples">>;
using Times = ZtArray<ZuTime, ZtArrayHeapID<"Zutcp.Bench.Times">>;

namespace Workload { enum { Order, MarketData, Updates, N }; }
namespace Frame { enum { Order, Execution, Request, Snapshot, Update, Feedback, N }; }

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
  bool server = false, native = false;
  unsigned interface = 0, messages = 10000, warmup = 1000, pipeline = 1;
  unsigned connections = 1, reserve = 4096, spin = 1000, timeout = 1;
  unsigned workload = Workload::Order, burstSize = 0, burstUS = 1000;
  unsigned socketBytes = 1<<20, rxCPU = 0, txCPU = 0;
  ZiIP address;
  const char *samples = nullptr;
};

struct Result {
  ZuTime begin, end; // Rx, read by main after owner stop
  uint64_t bytes = 0;
  unsigned bursts = 0, lateBursts = 0, pipelinePeak = 0;
  unsigned received = 0, rxCPU = ~0U, rxSocketBytes = 0, txSocketBytes = 0;
  bool native = false;
  alignas(Zm::CacheLineSize) uint64_t accepted = 0; // Tx
  unsigned txCPU = ~0U; // Tx
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
  void sent(ZmRef<ZiTxBuf>, bool);
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
  alignas(Zm::CacheLineSize) Times m_times; // Rx: one timestamp per admitted application request
  unsigned	m_offered = 0, m_received = 0, m_limit = 0; // Rx
  unsigned	m_burstRemaining = 0; // Rx: application offer tokens only
  uint64_t	m_burstSerial = 0; // Rx: invalidates queued prior-phase callbacks
  bool		m_burstArmed = false, m_stopping = false; // Rx
  ZuTime		m_nextBurst;
  Timer		m_burstTimer; // existing scheduler, same Rx owner
  alignas(Zm::CacheLineSize) unsigned m_accepted = 0; // Tx
};

using Owners = ZtArray<ZmRef<Cxn>, ZtArrayHeapID<"Zutcp.Bench.Owners">>;

class App : public ZiMultiplex {
public:
  explicit App(const Config &config, ZiMxParams params) :
    ZiMultiplex{ZuMv(params)}, m_config{config} {
    if (config.native) {
      m_stack = new Zutcp::Stack{this,
	{.interface = config.interface, .spin = config.spin, .packetCount = config.reserve}};
      plugin(m_stack);
      Zutcp::StackConfig settings;
      ZtString<> error;
      if (!m_stack->configure(settings, error)) {
	ZiLOG(Error, "zutcpbench", ([error = ZeString{error}](auto &out) {
	  out << "native configure: " << error;
	}));
	m_failed = 1;
      }
      std::cout << "native_configuration control_count=" << settings.controlBufCount
	<< " control_credits=" << settings.txControlCredits << " rx_retain="
	<< settings.rxRetainFrames << " rx_per_cxn=" << settings.rxRetainFramesPerCxn
	<< " rx_reserve=" << settings.rxReserveFrames << " max_data="
	<< settings.maxData << "\n";
    }
    m_network = allocator<Zi::IOBufType::RxTCP, ZiRxBuf, 2048>(config.reserve);
    m_frames = allocator<Zi::IOBufType::App, ZiRxBuf, MaxFrame>(config.reserve);
    m_output = allocator<Zi::IOBufType::TxTCP, ZiTxBuf, 1024>(config.reserve);
    std::cout << "allocator_geometry network_size=" << m_network->size << " network_head="
      << m_network->headRoom << " network_tail=" << m_network->tailRoom << " tx_size="
      << m_output->size << " tx_head=" << m_output->headRoom << " tx_tail="
      << m_output->tailRoom << " app_capacity=" << m_frames->capacity() << "\n";
    m_results.length(config.connections);
    m_owners.length(config.connections);
    if (!config.server) {
      m_samples.length(uint64_t(config.messages) * config.connections);
      memset(m_samples.data(), 0, m_samples.length() * sizeof(uint64_t));
    }
  }
  ~App() { shutdown(); }
  const Config &config() const { return m_config; }
  Zi::IOBufAllocator *frames() const { return m_frames; }
  Zi::IOBufAllocator *output() const { return m_output; }
  Result &result(unsigned id) { return m_results[id]; }
  void sample(unsigned id, unsigned sequence, uint64_t nanos) {
    m_samples[uint64_t(id) * m_config.messages + sequence] = nanos;
  }
  void failure(const char *reason = "unspecified", unsigned id = ~0U,
      unsigned sequence = 0, unsigned detail = 0) {
    if (m_failed.xch(1)) return;
    ZiLOG(Error, "zutcpbench", ([reason = ZeString{reason}, id, sequence, detail](auto &out) {
      out << "first_failure reason=" << reason << " id=" << id
	<< " sequence=" << sequence << " detail=" << detail;
    }));
    if (m_stack) {
      auto provider = static_cast<Zutcp::Allocator *>(m_output->provider());
      ZiLOG(Error, "zutcpbench", ([used = provider->pool.used(), count = provider->pool.count,
	ready = unsigned(provider->pool.ready()), capacity = m_output->capacity()](auto &out) {
	out << "failure_pool used=" << used << " count=" << count
	  << " ready=" << ready << " capacity=" << capacity;
      }));
      txRun([this] {
	auto state = m_stack->txAdmission();
	ZiLOG(Error, "zutcpbench", ([state](auto &out) {
	  out << "failure_tx outstanding=" << state.outstanding << " capacity=" << state.capacity
	    << " control_free=" << state.controlFree;
	}));
	m_done.post();
      });
      return;
    }
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
    return !m_done.timedwait(Zm::now(120)) && !failed();
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
  using Alloc = Zi::IOBufAllocator;
  using Flag = ZmAtomic<unsigned>;
  Config		m_config;
  Zutcp::Stack	*m_stack = nullptr; // owned by the existing plugin binding
  Alloc		*m_network = nullptr, *m_frames = nullptr, *m_output = nullptr;
  Results		m_results;
  Owners		m_owners; // bounded App ownership through mx stop
  Samples		m_samples;
  ZmSemaphore	m_ready, m_done;
  Flag		m_failed = 0;
  unsigned	m_created = 0, m_closed = 0; // Rx
  bool		m_shutdown = false; // main: explicit shutdown and destructor share one stop
};

Cxn::Cxn(App *app, const ZiCxnInfo &info, unsigned id) :
  ZiConnection{app, info}, m_app{app}, m_id{id} {
  m_times.length(app->config().pipeline);
}

Zi::IOBufAllocator *Cxn::rxAllocator() const { return m_app->frames(); }
unsigned Cxn::maxQueueLen() const { return m_app->config().pipeline + 1; }

void Cxn::connected(ZiRxContext &io)
{
  auto &result = m_app->result(m_id);
  result.rxCPU = unsigned(sched_getcpu());
  result.native = bool(plugin());
  if (result.native != m_app->config().native ||
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
      m_app->result(m_id).begin = Zm::now();
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
  Tx::send(ZmRef<ZiTxBuf>::acquire(static_cast<ZiTxBuf *>(ZuMv(buf).release())));
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
  auto &config = m_app->config();
  m_burstRemaining = config.burstSize;
  if (config.burstSize) {
    m_nextBurst = Zm::now() + ZuTime{ZuTime::Nano{uint64_t(config.burstUS) * 1000}};
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
    m_nextBurst = now + ZuTime{ZuTime::Nano{uint64_t(m_app->config().burstUS) * 1000}};
  }
  m_burstArmed = true;
  mx()->add(&m_burstTimer, m_nextBurst, ZmScheduler::Update,
    [this, serial = m_burstSerial](auto &&arm) {
      return arm([this, serial] {
	if (m_burstSerial != serial) return;
	m_burstArmed = false;
	if (m_stopping || !up()) return;
	m_burstRemaining = m_app->config().burstSize;
	m_nextBurst = Zm::now() +
	  ZuTime{ZuTime::Nano{uint64_t(m_app->config().burstUS) * 1000}};
	++m_app->result(m_id).bursts;
	fill();
      });
    }, mx()->rxThread());
}

void Cxn::fill()
{
  auto &config = m_app->config();
  while (!m_stopping && m_offered < m_limit && m_offered - m_received < config.pipeline &&
      (!config.burstSize || m_burstRemaining)) {
    if (config.burstSize) --m_burstRemaining;
    m_times[m_offered % config.pipeline] = Zm::now();
    offer(++m_offered);
    auto &peak = m_app->result(m_id).pipelinePeak;
    if (m_offered - m_received > peak) peak = m_offered - m_received;
    if (m_app->failed()) return;
  }
  if (config.burstSize && !m_burstRemaining) scheduleBurst();
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
	now <= m_times[(sequence - 1) % config.pipeline]) {
      m_app->failure("rx-time", m_id, sequence); return -1;
    }
    if (sequence > config.warmup)
      m_app->sample(m_id, sequence - config.warmup - 1,
	uint64_t((now - m_times[(sequence - 1) % config.pipeline]).nanosecs()));
    if (sequence == config.warmup) {
      result.begin = now;
      m_limit = config.warmup + config.messages;
      beginPhase();
    }
    if (sequence == config.warmup + config.messages) {
      result.end = now;
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

void Cxn::sent(ZmRef<ZiTxBuf>, bool ok)
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
    ok &= r.native == m_config.native &&
      r.rxCPU == m_config.rxCPU && r.txCPU == m_config.txCPU;
    if (*r.begin && (!*begin || r.begin < begin)) begin = r.begin;
    if (*r.end && (!*end || r.end > end)) end = r.end;
  }
  std::cout << "result role=" << (m_config.server ? "server" : "client") << " backend="
    << (m_config.native ? "native" : "kernel") << " exact=" << unsigned(ok) << " connections="
    << m_config.connections << " pipeline=" << m_config.pipeline << " warmup="
    << m_config.warmup << " measured_per_connection=" << m_config.messages << " workload="
    << workloadName(m_config.workload) << " burst_size=" << m_config.burstSize
    << " burst_us=" << m_config.burstUS << " received_fix_bytes="
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
    std::cout << "application_bytes request_frame=" << request << " response_frame=" << response
      << " measured_request=" << (n * request) << " measured_response="
      << (n * response) << " request_bytes_per_second="
      << ZuBoxed(double(n * request)/seconds).fp<3>() << " response_bytes_per_second="
      << ZuBoxed(double(n * response)/seconds).fp<3>() << "\n";
  }
  if (m_stack) {
    auto c = m_stack->cxnAdmission();
    auto tx = m_stack->txAdmission();
    auto rx = m_stack->rxAdmission();
    std::cout << "native_stopped ready=" << unsigned(m_stack->ready()) << " cxns=" << c.total
      << " pending=" << c.pending << " tx_outstanding=" << tx.outstanding << " control_free="
      << tx.controlFree << " control_count=" << tx.controlCount << " data_stalls="
      << tx.dataStalls << " control_stalls="
      << tx.controlStalls << "\n";
    std::cout << "native_rx admitted=" << rx.admitted << " refused="
      << rx.refused << " retired=" << rx.retired
      << " retained=" << rx.retained << " capacity=" << rx.capacity << " limit=" << rx.limit
      << " configured=" << rx.configured << " per_cxn=" << rx.configuredPerCxn << " reserve="
      << rx.reserve << "\n";
    ok &= !rx.retained && !m_stack->ready() && !c.total && !c.pending && !tx.outstanding &&
      tx.controlFree == tx.controlCount;
  }
  return ok;
}
} // FixBench

static void usage()
{
  std::cerr << "zutcpbench server|client kernel|native INTERFACE ADDRESS MESSAGES WARMUP "
    "PIPELINE CONNECTIONS RESERVE RX_CPU TX_CPU SPIN [SAMPLES.csv]\n"
    "Environment: BENCH_WORKLOAD=order|marketdata|updates BENCH_BURST_SIZE=0 "
    "BENCH_BURST_US=1000\n"
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
  config.native = !strcmp(argv[2], "native");
  if (!config.native && strcmp(argv[2], "kernel")) usage();
  config.interface = if_nametoindex(argv[3]);
  config.address = ZiIP{argv[4]};
  config.messages = unsigned(ZuBox<unsigned>{argv[5]});
  config.warmup = unsigned(ZuBox<unsigned>{argv[6]});
  config.pipeline = unsigned(ZuBox<unsigned>{argv[7]});
  config.connections = unsigned(ZuBox<unsigned>{argv[8]});
  config.reserve = unsigned(ZuBox<unsigned>{argv[9]});
  config.rxCPU = unsigned(ZuBox<unsigned>{argv[10]});
  config.txCPU = unsigned(ZuBox<unsigned>{argv[11]});
  // Last positional argument is the explicit scheduler spin count for BOTH modes.
  config.spin = unsigned(ZuBox<unsigned>{argv[12]});
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
  if (!config.interface || !config.messages || !config.pipeline || !config.connections ||
      config.connections > MaxCxns || config.warmup > MaxSamples ||
      uint64_t(config.messages) * config.connections > MaxSamples ||
      config.pipeline > MaxPipeline || config.burstSize > MaxPipeline ||
      (config.burstSize && (!config.burstUS || config.burstUS > MaxBurstUS)) ||
      config.reserve < MinReserve || config.reserve > MaxReserve) usage();
  ZiLog::init("zutcpbench");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZiMxParams params;
  params.rxBufSize(config.native ? config.reserve : config.socketBytes)
    .txBufSize(config.socketBytes).rxQuantum(8)
    .rxThread(1).txThread(2).scheduler([&config](auto &s) {
      ZmBitmap rx, tx;
      rx.set(config.rxCPU); tx.set(config.txCPU);
      s.nThreads(3).queueSize(1<<16).ll(false).quantum(ZuTime{ZuTime::Nano{1000}})
	.spin(config.spin).timeout(config.timeout)
	.thread(1, [&rx](auto &t) { t.isolated(true).cpuset(rx); })
	.thread(2, [&tx](auto &t) { t.isolated(true).cpuset(tx); });
    });
  std::cout << "config backend=" << (config.native ? "native" : "kernel") << " rx_cpu="
    << config.rxCPU << " tx_cpu=" << config.txCPU
    << " threads=3 rx_thread=1 tx_thread=2 spin=" << config.spin << " timeout_ms="
    << config.timeout << " quantum_ns=1000 ll=0 queue=65536 kernel_socket_bytes_requested="
    << (config.native ? 0 : config.socketBytes) << " native_rx_frames_requested="
    << (config.native ? config.reserve : 0) << " reserve_requested=" << config.reserve
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
