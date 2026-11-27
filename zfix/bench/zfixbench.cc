//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Plain, managed FIX over Zi kernel TCP or Zutcp/Zpkt.
#include "config.h"

#include <net/if.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include <fstream>
#include <iostream>

#include <zlib/ZuArray.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBitmap.hh>
#include <zlib/ZmHashMgr.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmTopology.hh>
#include <zlib/ZtcQueue.hh>
#include <zlib/ZtcHeap.hh>
#include <zlib/ZtcHash.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZfixClient.hh>
#include <zlib/ZfixServer.hh>
#include <zlib/Zpkt.hh>
#include <zlib/ZpktEnv.hh>
#include <zlib/Zutcp.hh>

#ifdef HAVE_DPDK
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <zlib/ZvCf.hh>
#include "ZpktDPDKCf.hh"
#include "zpktdkfixture.hh"
#endif

namespace FixBench {

using namespace Zfix;

enum { Port = 18187, MaxCxns = 128, MaxPipeline = 1024,
  MaxSamples = 2000000, MinReserve = 1024, MaxReserve = 65536,
  DefaultQueueSize = 1<<16 };

struct Business {
  uint64_t seqNo = 0;
  bool possDup = false;
};

struct Order : Business {
  ZuCSpan id, symbol = "TEST", transactTime = "20260101-00:00:00.000";
  unsigned side = 1, quantity = 100, ordType = 2;
  ZuCSpan price = "123.45";
};
ZfStruct(, (Order),
  (id, (FIX::ID<11>, Required), String),
  (symbol, (FIX::ID<55>, Required), String),
  (side, (FIX::ID<54>, Required), UInt32),
  (transactTime, (FIX::ID<60>, Required), String),
  (quantity, (FIX::ID<38>, Required), UInt32),
  (ordType, (FIX::ID<40>, Required), UInt32),
  (price, (FIX::ID<44>, Required), String));
using OrderFields = ZuTypeConcat<Zfix::SessionFields<Order>, ZfFields_Order>;
OrderFields ZuFields_(Order *, ZuFacet::FIX *);
ZfStructImpl(Order); ZfStructImpl(Order, FIX);

struct Execution : Business {
  ZuCSpan id, execID, orderID;
  unsigned execType = 0, ordStatus = 0;
  ZuCSpan symbol = "TEST";
  unsigned side = 1, quantity = 100;
  ZuCSpan price = "123.45";
  unsigned leavesQty = 100, cumQty = 0;
  ZuCSpan avgPx = "0";
};
ZfStruct(, (Execution),
  (id, (FIX::ID<11>, Required), String),
  (execID, (FIX::ID<17>, Required), String),
  (orderID, (FIX::ID<37>, Required), String),
  (execType, (FIX::ID<150>, Required), UInt32),
  (ordStatus, (FIX::ID<39>, Required), UInt32),
  (symbol, (FIX::ID<55>, Required), String),
  (side, (FIX::ID<54>, Required), UInt32),
  (quantity, (FIX::ID<38>, Required), UInt32),
  (price, (FIX::ID<44>, Required), String),
  (leavesQty, (FIX::ID<151>, Required), UInt32),
  (cumQty, (FIX::ID<14>, Required), UInt32),
  (avgPx, (FIX::ID<6>, Required), String));
using ExecutionFields = ZuTypeConcat<Zfix::SessionFields<Execution>, ZfFields_Execution>;
ExecutionFields ZuFields_(Execution *, ZuFacet::FIX *);
ZfStructImpl(Execution); ZfStructImpl(Execution, FIX);

struct EntryType { unsigned type = 0; };
ZfStruct(, (EntryType, FIX), (type, (FIX::ID<269>, Required), UInt32));
ZfStructImpl(EntryType); ZfStructImpl(EntryType, FIX);
struct EntryTypes : ZtArray<EntryType, ZtArrayHeapID<"Zfix.Bench.EntryTypes">> {
  ZuDerive_(EntryTypes, (ZtArray<EntryType, ZtArrayHeapID<"Zfix.Bench.EntryTypes">>));
friend Zfix::Fmt::AsArray<> Zfix_Fmt(EntryTypes *);
};

struct RelatedSymbol { ZuCSpan symbol = "TEST"; };
ZfStruct(, (RelatedSymbol, FIX), (symbol, (FIX::ID<55>, Required), String));
ZfStructImpl(RelatedSymbol); ZfStructImpl(RelatedSymbol, FIX);
struct RelatedSymbols : ZtArray<RelatedSymbol, ZtArrayHeapID<"Zfix.Bench.Symbols">> {
  ZuDerive_(RelatedSymbols, (ZtArray<RelatedSymbol, ZtArrayHeapID<"Zfix.Bench.Symbols">>));
friend Zfix::Fmt::AsArray<> Zfix_Fmt(RelatedSymbols *);
};

struct MarketRequest : Business {
  ZuCSpan mdReqID;
  unsigned subscription = 0, depth = 1;
  EntryTypes types;
  RelatedSymbols symbols;
};
ZfStruct(, (MarketRequest),
  (mdReqID, (FIX::ID<262>, Required), String),
  (subscription, (FIX::ID<263>, Required), UInt32),
  (depth, (FIX::ID<264>, Required), UInt32),
  (types, (FIX::ID<267>, Required), UDT),
  (symbols, (FIX::ID<146>, Required), UDT));
using MarketRequestFields = ZuTypeConcat<Zfix::SessionFields<MarketRequest>,
  ZfFields_MarketRequest>;
MarketRequestFields ZuFields_(MarketRequest *, ZuFacet::FIX *);
ZfStructImpl(MarketRequest); ZfStructImpl(MarketRequest, FIX);

struct SnapshotEntry {
  unsigned type = 0;
  ZuCSpan price = "123.45";
  unsigned size = 100;
};
ZfStruct(, (SnapshotEntry, FIX),
  (type, (FIX::ID<269>, Required), UInt32),
  (price, (FIX::ID<270>, Required), String),
  (size, (FIX::ID<271>, Required), UInt32));
ZfStructImpl(SnapshotEntry); ZfStructImpl(SnapshotEntry, FIX);
struct SnapshotEntries : ZtArray<SnapshotEntry, ZtArrayHeapID<"Zfix.Bench.Snapshot">> {
  ZuDerive_(SnapshotEntries, (ZtArray<SnapshotEntry, ZtArrayHeapID<"Zfix.Bench.Snapshot">>));
friend Zfix::Fmt::AsArray<> Zfix_Fmt(SnapshotEntries *);
};
struct Snapshot : Business {
  ZuCSpan mdReqID, symbol = "TEST";
  SnapshotEntries entries;
};
ZfStruct(, (Snapshot),
  (mdReqID, (FIX::ID<262>, Required), String),
  (symbol, (FIX::ID<55>, Required), String),
  (entries, (FIX::ID<268>, Required), UDT));
using SnapshotFields = ZuTypeConcat<Zfix::SessionFields<Snapshot>, ZfFields_Snapshot>;
SnapshotFields ZuFields_(Snapshot *, ZuFacet::FIX *);
ZfStructImpl(Snapshot); ZfStructImpl(Snapshot, FIX);

struct UpdateEntry {
  unsigned action = 0, type = 0;
  ZuCSpan id;
  ZuCSpan symbol = "TEST", price = "123.45";
  unsigned size = 100;
};
ZfStruct(, (UpdateEntry, FIX),
  (action, (FIX::ID<279>, Required), UInt32),
  (type, (FIX::ID<269>, Required), UInt32),
  (id, (FIX::ID<278>, Required), String),
  (symbol, (FIX::ID<55>, Required), String),
  (price, (FIX::ID<270>, Required), String),
  (size, (FIX::ID<271>, Required), UInt32));
ZfStructImpl(UpdateEntry); ZfStructImpl(UpdateEntry, FIX);
struct UpdateEntries : ZtArray<UpdateEntry, ZtArrayHeapID<"Zfix.Bench.Update">> {
  ZuDerive_(UpdateEntries, (ZtArray<UpdateEntry, ZtArrayHeapID<"Zfix.Bench.Update">>));
friend Zfix::Fmt::AsArray<> Zfix_Fmt(UpdateEntries *);
};
struct Update : Business {
  ZuCSpan mdReqID;
  UpdateEntries entries;
};
ZfStruct(, (Update),
  (mdReqID, (FIX::ID<262>, Required), String),
  (entries, (FIX::ID<268>, Required), UDT));
using UpdateFields = ZuTypeConcat<Zfix::SessionFields<Update>, ZfFields_Update>;
UpdateFields ZuFields_(Update *, ZuFacet::FIX *);
ZfStructImpl(Update); ZfStructImpl(Update, FIX);

using OrderReq = Zfix::MT<Order, "D">;
using OrderRes = Zfix::MT<Execution, "8">;
using MarketReq = Zfix::MT<MarketRequest, "V">;
using MarketRes = Zfix::MT<Snapshot, "W">;
using UpdateMsg = Zfix::MT<Update, "X">;
using Dictionary = Zfix::MTList<OrderReq, OrderRes, MarketReq, MarketRes,
  UpdateMsg>;

using Samples = ZtArray<uint64_t, ZtArrayHeapID<"Zfix.Bench.Samples">>;
using Times = ZtArray<ZuTime, ZtArrayHeapID<"Zfix.Bench.Times">>;
// All scheduled times use CLOCK_MONOTONIC. Scheduler deadlines use its clock.
static uint64_t monotonic() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return uint64_t(t.tv_sec) * 1000000000 + uint64_t(t.tv_nsec);
}

static ZuTime schedulerTime(uint64_t due) {
  auto now = monotonic();
  auto at = Zm::now();
  if (due > now) at += ZuTime{ZuTime::Nano{due - now}};
  return at;
}

struct OfferSchedule {
  unsigned rate = 0, count = 0, cap = 0;

  uint64_t offset(unsigned index) const { return uint64_t(index) * 1000000000/rate; }
  unsigned due(uint64_t elapsed) const {
    if (elapsed >= offset(count - 1)) return count;
    return unsigned(((elapsed + 1) * rate - 1)/1000000000) + 1;
  }
  unsigned alarm(unsigned admitted, unsigned received, unsigned pipeline) const {
    // With no slot, the first offer beyond the pending cap needs service.
    uint64_t index = admitted - received < pipeline ? admitted : uint64_t(admitted) + cap;
    return index < count ? unsigned(index) : count;
  }
};

struct OfferTime { uint64_t admitted = 0, completed = 0; };
using OfferTimes = ZtArray<OfferTime, ZtArrayHeapID<"Zfix.Bench.Offers">>;

using Queue = ZmList<Zfix::TxBody::Ref, ZmListHeapID<"Zfix.Bench.Queue">>;

struct Config {
  bool server = false, native = false, appColocate = true, ll = false,
    congestionControl = true, allocationTuning = false, txBatch = false;
  int xdpTxCPU = -1, afpBypass = -1;
  ZuBox<unsigned> spin, rxSpin, txSpin;
  unsigned messages = 0, warmup = 0, pipeline = 0,
    connections = 0, reserve = 0, rxCPU = 0, txCPU = 0,
    appRxCPU = 0, appTxCPU = 0,
    workload = 0, burstSize = 0, burstUS = 1000, ackHoldUS = 25000,
    queueSize = DefaultQueueSize, hashBits = 0;
  ZiIP address;
  const char *backend = "kernel", *interfaceName = nullptr, *addressText = nullptr;
  const char *routeInterface = nullptr, *dpdkConfig = nullptr;
  const char *xdpModule = ZFIX_XDP_MODULE;
  uint64_t tcpWindow = Zutcp::DefaultMaxData;
  const char *samples = nullptr;
  std::ostream *output = &std::cout;
  const char *telemetryDir = nullptr;
  const char *telemetryPeerDir = nullptr;
  OfferSchedule offers;

  bool afxdp() const { return !strcmp(backend, "native") || !strcmp(backend, "xdp"); }
  bool afp() const { return !strcmp(backend, "afp"); }
  bool dpdk() const { return !strcmp(backend, "dpdk"); }
};

static int xdpSenderCPU(const cpu_set_t &allowed, unsigned rx, unsigned tx) {
  auto topology = ZmTopology::hwloc();
  auto pu = hwloc_get_pu_obj_by_os_index(topology, rx);
  auto core = pu ? hwloc_get_ancestor_obj_by_type(topology, HWLOC_OBJ_CORE, pu) : nullptr;
  if (core)
    for (int cpu = hwloc_bitmap_first(core->cpuset);
	 cpu >= 0 && cpu < CPU_SETSIZE; cpu = hwloc_bitmap_next(core->cpuset, cpu))
      if (unsigned(cpu) != rx && unsigned(cpu) != tx && CPU_ISSET(cpu, &allowed)) return cpu;
  for (unsigned offset = 1; offset < CPU_SETSIZE; ++offset) {
    unsigned cpu = (rx + offset)%CPU_SETSIZE;
    if (cpu != tx && CPU_ISSET(cpu, &allowed)) return int(cpu);
  }
  return tx != rx && tx < CPU_SETSIZE && CPU_ISSET(tx, &allowed) ? int(tx) : -1;
}

// Cold traffic-only policy, identical for kernel and native endpoints. These
// counts cover observed object peaks; backing fallback remains enabled.
static void tuneAllocations(Config &c) {
  if (!c.allocationTuning) return;
  struct HeapPlan {
    const char *id;
    unsigned capacity, observedSizes;
  };
  static constexpr HeapPlan heaps[] = {
    {"ZiIOBuf", 256, 64 + 1152},
    {"Zutcp.Pkt.OutstandingRange", 512, 64},
    {"Zutcp.Pkt.TxSentNode", 2048, 704},
    {"Zutcp.Connection.TxRanges", 1024, 384},
    {"Zutcp.Connection.RxQueue", 64, 192},
    {"Zfix.Rx", 128, 512},
    {"Zfix.WireQueue", 128, 128},
    {"Zfix.Tx", 4096, 128},
    {"Zfix.TxBody", 8192, 1216},
    {"Zfix.TCP.Rx", 8192, 64 + 1152}
  };
  uint64_t estimate = 0;
  for (const auto &heap : heaps) {
    ZmHeapMgr::init(heap.id, 0, 0, ZmHeapConfig{.cacheSize = heap.capacity});
    estimate += uint64_t(heap.capacity) * heap.observedSizes;
    std::cout << "heap_plan id=" << heap.id << " capacity=" << heap.capacity
      << " partition=0 vshift=0\n";
  }
  auto params = ZmHashParams{c.connections};
  if (params.bits() < 5) params.bits(5);
  c.hashBits = params.bits();
  for (auto id : {"Zutcp.Endpoints", "Zutcp.Endpoint.TupleRouter", "Zutcp.ServerLinks"})
    ZmHashMgr::init(id, params);
  ZmHashMgr::init(ZiMxParams{}.requestHash(), params.cBits(4));
  // An estimate for the observed size classes, not a bound on future classes.
  // heap CSV size*cacheSize reports the actual instantiated cache storage.
  std::cout << "allocation_tuning partition=0 vshift=0 heap_ids=10"
    << " observed_bytes_estimate=" << estimate << " hash_bits=" << c.hashBits << '\n';
}

// Cold configuration shared by traffic and generator calibration. Restrict this
// diagnostic to the current benchmark size and the existing scheduler default.
static bool queueConfig(Config &c) {
  c.telemetryDir = getenv("BENCH_TELEMETRY_DIR");
  c.telemetryPeerDir = getenv("BENCH_TELEMETRY_PEER_DIR");
  if (bool(c.telemetryDir) != bool(c.telemetryPeerDir) ||
      (c.telemetryDir && (!*c.telemetryDir || !*c.telemetryPeerDir))) return false;
  if (c.telemetryDir) {
    if (!strcmp(c.telemetryDir, c.telemetryPeerDir)) return false;
    for (auto name : {"ready", "dumped"}) {
      Zi::Path path;
      path << c.telemetryDir << '/' << name;
      if (ZiStat{path}.exists()) return false;
    }
  }
  if (auto value = getenv("BENCH_QUEUE_SIZE")) {
    c.queueSize = unsigned(ZuBox<unsigned>{value});
    if (c.queueSize != DefaultQueueSize && c.queueSize != ZmSchedParams{}.queueSize())
      return false;
  }
  return true;
}

// Main only, after measured completion. The caller creates a separate existing
// directory for each endpoint. Keep live hashes before Hub::stop/final removes
// them; heap counters are also collected after the worker drain.
template <typename CSV>
static bool telemetryCSV(const Config &c, const char *name, CSV csv) {
  if (!c.telemetryDir) return true;
  Zi::Path path;
  path << c.telemetryDir << '/' << name;
  ZiFile file{path, ZiFile::Write | ZiFile::Truncate | ZiFile::GC};
  if (!file) {
    std::cerr << "telemetry_error file=" << path << " operation=open\n";
    return false;
  }
  ZtString<ZtStringHeapID<"Zfix.Bench.Telemetry">> text;
  text << csv;
  bool ok = file.write(text.data(), text.length()) == Zi::OK;
  if (file.close() != Zi::OK) ok = false;
  if (!ok)
    std::cerr << "telemetry_error file=" << path << " operation=write_close\n";
  return ok;
}

struct QueueCSV {
  const Ztc::QueueMgr &manager;

  template <typename S> void print(S &s) const {
    s << "ownerID,id,type,inBytes,outBytes,inCount,outCount,count,size,full\n";
    manager.allQueues([&s](Ztc::Queue *queue) {
      Ztc::QueueTelemetry data;
      queue->telemetry(data);
      auto id = [&s](const ZuID &value) {
	s << '"';
	for (char c : ZuCSpan{value}) {
	  if (c == '"') s << '"';
	  s << c;
	}
	s << '"';
      };
      id(data.ownerID); s << ','; id(data.id);
      s << ',' << int(data.type) << ',' << data.inBytes << ',' << data.outBytes
	<< ',' << data.inCount << ',' << data.outCount << ',' << data.count
	<< ',' << data.size << ',' << data.full << '\n';
    });
  }
  friend ZuPrintFn ZuPrintType(QueueCSV *);
};

static bool liveTelemetry(const Config &c, const ZiMultiplex &mx) {
  bool queues = telemetryCSV(c, "queue-live.csv", QueueCSV{mx});
  bool hashes = telemetryCSV(c, "hash-live.csv", Ztc::hashCSV());
  bool heaps = telemetryCSV(c, "heap-live.csv", Ztc::heapCSV());
  return queues && hashes && heaps;
}

static bool drainedTelemetry(const Config &c, const ZiMultiplex &mx) {
  bool queues = telemetryCSV(c, "queue-drained.csv", QueueCSV{mx});
  bool heaps = telemetryCSV(c, "heap-drained.csv", Ztc::heapCSV());
  return queues && heaps;
}

// Main only, after stop has joined all workers. These are existing whole-run
// ring counters; full is not a count of scheduler overflow-queue posts or a peak.
static void queueReport(const ZmScheduler &scheduler, std::ostream &out = std::cout) {
  scheduler.ZmScheduler::allQueues([&scheduler, &out, sid = 0U](Ztc::Queue *queue) mutable {
    Ztc::QueueTelemetry data;
    queue->telemetry(data);
    ++sid; // base allQueues visits scheduler slots in ascending order
    uint64_t overflowIn, overflowOut;
    scheduler.queue(sid).stats(overflowIn, overflowOut);
    out << "scheduler_queue sid=" << sid << " id=" << data.id
      << " in_count=" << data.inCount << " in_bytes=" << data.inBytes
      << " out_count=" << data.outCount << " out_bytes=" << data.outBytes
      << " count=" << data.count << " size=" << data.size
      << " capacity=" << scheduler.ring(sid).size() << " full=" << data.full
      << " overflow_in=" << overflowIn << " overflow_out=" << overflowOut
      << " overflow_count=" << scheduler.queue(sid).count_() << '\n';
  });
}

static const char *workloadName(unsigned kind) {
  switch (kind) {
    case 0: return "order";
    case 1: return "marketdata";
    default: return "updates";
  }
}

struct State {
  const Config &config;
  ZmSemaphore ready, done, drained, measured;
  ZmAtomic<unsigned> failed{0}, closed{0}, created{0};
  ZmAtomic<unsigned> measuredCount{0};
  Samples samples;
  OfferTimes offers;
  // Main-thread traffic envelope, excluding diagnostic output and teardown.
  alignas(Zm::CacheLineSize) rusage usageStart{}, usageEnd{};
  bool usageRecorded = false;
  // Rx-owned until all workers stop; failTime is set by the first failure owner.
  alignas(Zm::CacheLineSize) unsigned phaseReady = 0;
  uint64_t origin = 0, failTime = 0, stopTime = 0;
  int64_t clockOffset = 0;

  explicit State(const Config &c) : config{c} {
    if (!c.server) {
      auto n = uint64_t(c.messages) * c.connections;
      if (c.offers.rate) offers.length(n);
      else {
	samples.length(n);
	memset(samples.data(), 0, samples.length() * sizeof(uint64_t));
      }
    }
  }

  void fail(const char *reason, unsigned id = ~0U, unsigned serial = 0) {
    if (failed.xch(1)) return;
    if (config.offers.rate) failTime = monotonic();
    std::cerr << "first_failure reason=" << reason << " id=" << id
      << " sequence=" << serial << '\n';
    done.post();
    measured.post();
  }

  void measuredLink() {
    if (++measuredCount == config.connections) measured.post();
  }

  void trafficStart() {
    if (getrusage(RUSAGE_SELF, &usageStart)) fail("usage-start");
  }
  bool trafficEnd() {
    if (getrusage(RUSAGE_SELF, &usageEnd)) { fail("usage-end"); return false; }
    return usageRecorded = true;
  }
  void trafficReport() const {
    if (!usageRecorded) return;
    auto micros = [](const timeval &time) {
      return uint64_t(time.tv_sec) * 1000000 + uint64_t(time.tv_usec);
    };
    *config.output << "traffic_usage user_us="
      << micros(usageEnd.ru_utime) - micros(usageStart.ru_utime)
      << " system_us=" << micros(usageEnd.ru_stime) - micros(usageStart.ru_stime)
      << " voluntary=" << usageEnd.ru_nvcsw - usageStart.ru_nvcsw
      << " involuntary=" << usageEnd.ru_nivcsw - usageStart.ru_nivcsw << '\n';
  }

  void stopped() {
    if (config.telemetryDir && measuredCount.load_() < config.connections)
      fail("telemetry-early-stop");
    unsigned n = closed.xchAdd(1) + 1;
    drained.post();
    if (n == config.connections) done.post();
  }

  void drain(unsigned n) {
    for (unsigned i = 0; i < n; ++i) drained.wait();
    *config.output << "drain role=" << (config.server ? "server" : "client")
      << " expected=" << n << " stopped=" << closed.load_() << '\n';
  }
};

// Main-thread rendezvous after both endpoints finish measuring, while every
// connection still owns its queues. File I/O never runs on an I/O worker.
static bool telemetryBarrier(State &state, const ZiMultiplex &mx) {
  const auto &c = state.config;
  if (state.measured.timedwait(Zm::now(120)) || state.failed.load_()) {
    state.fail("telemetry-measured"); return false;
  }
  if (!state.trafficEnd()) return false;
  if (c.offers.rate) state.stopTime = monotonic();
  auto meet = [&state, &c](const char *name) {
    if (!telemetryCSV(c, name, "complete\n")) return false;
    Zi::Path peer;
    peer << c.telemetryPeerDir << '/' << name;
    auto deadline = Zm::now(30);
    while (!ZiStat{peer}.exists()) {
      if (state.failed.load_() || Zm::now() >= deadline) return false;
      Zm::sleep(ZuTime{0, 1000000});
    }
    return true;
  };
  if (!meet("ready") || !liveTelemetry(c, mx) || !meet("dumped")) {
    state.fail("telemetry-barrier"); return false;
  }
  return true;
}

static Zfix::SessionConfig sessionConfig(bool client) {
  Zfix::SessionConfig c;
  c.mode = Zfix::Transport::FIX4;
  c.initiator = client;
  c.heartBtInt = 30;
  c.identity.version = Zfix::Version::V4_4;
  c.identity.local = client ? "BENCH" : "PEER";
  c.identity.peer = client ? "PEER" : "BENCH";
  return c;
}

template <typename Impl> struct Peer {
  State *state;
  const unsigned id;
  const bool client;
  unsigned offered = 0, received = 0, limit = 0, peak = 0;
  unsigned burstRemaining = 0, bursts = 0, lateBursts = 0;
  uint64_t burstSerial = 0;
  bool burstArmed = false, stopping = false;
  ZuTime nextBurst;
  ZmScheduler::Timer burstTimer;
  unsigned rxCPU = ~0U, txCPU = ~0U;
  bool native = false;
  ZuTime begin, end;
  uint64_t rxBytes = 0;
  unsigned pendingPeak = 0;
  uint64_t offerAlarm = 0;
  Times times;
  alignas(Zm::CacheLineSize) unsigned completed = 0;
  Queue queue;

  Peer(State *s, unsigned id_, bool client_) : state{s}, id{id_}, client{client_} {
    if (client) {
      times.length(s->config.pipeline);
      limit = s->config.warmup ? s->config.warmup : s->config.messages;
    }
  }

  auto self() { return static_cast<Impl *>(this); }

  unsigned fixAssociateTx(Zfix::TxBody::Ref, Zfix::TxContext, uint64_t) {
    return Zfix::TxLoad::Accepted;
  }

  void fixSubmittedTx(Zfix::TxBody::Ref body, unsigned result) {
    if (result == Zfix::TxSelect::Busy || result == Zfix::TxSelect::Held) {
      queue.push(ZuMv(body)); self()->queued(); return;
    }
    if (result != Zfix::TxSelect::Accepted) state->fail("submit", id, offered);
  }

  Zfix::TxBody::Ref fixDequeueTx() {
    auto node = queue.shift(); return node ? ZuMv(node->data()) : Zfix::TxBody::Ref{};
  }

  void fixStopTx(uint64_t) { queue.clean(); }

  void fixStopRx(uint64_t, uint64_t) { stopping = true; cancelBurst(); }

  void fixCompleteTx(Zfix::TxBody::Ref body, Zfix::TxContext, bool ok) {
    if (txCPU == ~0U) txCPU = unsigned(sched_getcpu());
    if (!ok) { state->fail("completion", id, completed); return; }
    if (!body->administrative()) {
      ++completed;
      if (!client && completed == state->config.warmup + state->config.messages &&
	  state->config.telemetryDir) state->measuredLink();
    }
  }

  void fixFaultTx(Zfix::TxContext, Zfix::Fmt::Error) { state->fail("tx-fault", id); }

  static ZuCSpan serialText(unsigned serial, char (&buffer)[10]) {
    ZuStream out{buffer, sizeof(buffer)};
    out << ZuBoxed(serial).fmt<ZuFmt::Right<10>>();
    return {buffer, sizeof(buffer)};
  }

  bool sendMessage(unsigned serial, bool response) {
    auto &c = state->config;
    char buffer[10];
    auto number = serialText(serial, buffer);
    Zfix::Fmt::Error error;
    bool ok = false;
    if (!response) {
      switch (c.workload) {
        case 0: {
          Order m; m.id = number;
          ok = self()->template send<OrderReq>(m, serial, error);
        } break;
        case 1: {
          MarketRequest m; m.mdReqID = number;
          m.types.push(EntryType{}); m.symbols.push(RelatedSymbol{});
          ok = self()->template send<MarketReq>(m, serial, error);
        } break;
        default: {
          Update m; m.mdReqID = number;
          UpdateEntry entry; entry.id = number; m.entries.push(entry);
          ok = self()->template send<UpdateMsg>(m, serial, error);
        } break;
      }
    } else {
      switch (c.workload) {
        case 0: {
          Execution m; m.id = m.execID = m.orderID = number;
          ok = self()->template send<OrderRes>(m, serial, error);
        } break;
        case 1: {
          Snapshot m; m.mdReqID = number; m.entries.push(SnapshotEntry{});
          ok = self()->template send<MarketRes>(m, serial, error);
        } break;
        default: {
          Update m; m.mdReqID = number;
          UpdateEntry entry; entry.id = number; m.entries.push(entry);
          ok = self()->template send<UpdateMsg>(m, serial, error);
        } break;
      }
    }
    if (!ok || error) state->fail("send", id, serial);
    return ok && !error;
  }

  void cancelBurst() {
    ++burstSerial;
    self()->app()->mx()->del(&burstTimer);
    burstArmed = false;
    offerAlarm = 0;
  }

  void beginPhase() {
    cancelBurst();
    auto &c = state->config;
    burstRemaining = c.burstSize;
    if (c.burstSize) {
      nextBurst = Zm::now() + ZuTime{ZuTime::Nano{uint64_t(c.burstUS) * 1000}};
      ++bursts;
    }
  }

  void scheduleBurst() {
    auto &c = state->config;
    if (burstArmed || stopping || offered >= limit) return;
    auto now = Zm::now();
    if (nextBurst <= now) {
      ++lateBursts;
      nextBurst = now + ZuTime{ZuTime::Nano{uint64_t(c.burstUS) * 1000}};
    }
    burstArmed = true;
    self()->app()->mx()->add(&burstTimer, nextBurst, ZmScheduler::Update,
      [link = ZmRef<Impl>{self()}, serial = burstSerial](auto &&arm) mutable {
        return arm([link = ZuMv(link), serial] {
          if (serial != link->burstSerial) return;
          link->burstArmed = false;
          if (link->stopping) return;
          link->burstRemaining = link->state->config.burstSize;
          link->nextBurst = Zm::now() +
            ZuTime{ZuTime::Nano{uint64_t(link->state->config.burstUS) * 1000}};
          ++link->bursts;
          link->fill();
        });
      }, self()->app()->rxThread());
  }

  void readyPhase() {
    cancelBurst();
    if (stopping || state->failed.load_()) return;
    if (++state->phaseReady != state->config.connections) return;
    for (const auto &link : self()->app()->links)
      if (!link || link->stopping) { state->fail("phase-stop", id); return; }
    state->origin = monotonic();
    state->clockOffset = int64_t(Zm::now().nanosecs()) - int64_t(state->origin);
    for (const auto &link : self()->app()->links) {
      link->begin = ZuTime{ZuTime::Nano{state->origin}};
      link->limit = state->config.warmup + state->config.messages;
    }
    for (const auto &link : self()->app()->links) link->fill();
  }

  void armOffer(uint64_t due) {
    if (burstArmed && offerAlarm == due) return;
    cancelBurst();
    auto deadline = schedulerTime(due);
    burstArmed = true;
    offerAlarm = due;
    self()->app()->mx()->add(&burstTimer, deadline, ZmScheduler::Update,
      [link = self(), serial = burstSerial](auto &&arm) {
	return arm([link, serial] {
	  if (serial != link->burstSerial) return;
	  link->burstArmed = false;
	  link->offerAlarm = 0;
	  if (!link->stopping) link->fill();
	});
      }, self()->app()->rxThread());
  }

  void fillOffers() {
    const auto &c = state->config;
    unsigned admitted = offered - c.warmup, replies = received - c.warmup;
    while (!stopping && !state->failed.load_() && admitted < c.messages) {
      auto now = monotonic();
      unsigned due = now >= state->origin ? c.offers.due(now - state->origin) : 0;
      unsigned pending = due > admitted ? due - admitted : 0;
      if (pending > pendingPeak) pendingPeak = pending;
      if (pending > c.offers.cap) {
	state->fail("offer-overflow", id, offered + 1); return;
      }
      if (!pending || admitted - replies == c.pipeline) break;
      auto &sample = state->offers[uint64_t(id) * c.messages + admitted];
      // Admission takes the common bounded application slot, before formatting.
      sample.admitted = now;
      if (!sendMessage(offered + 1, false)) return;
      ++offered; ++admitted;
      if (offered - received > peak) peak = offered - received;
    }
    if (stopping || state->failed.load_()) { cancelBurst(); return; }
    unsigned alarm = c.offers.alarm(admitted, replies, c.pipeline);
    if (alarm == c.messages) cancelBurst();
    else armOffer(state->origin + c.offers.offset(alarm));
  }

  void fill() {
    auto &c = state->config;
    if (c.offers.rate && state->origin) { fillOffers(); return; }
    while (!stopping && !state->failed.load_() && offered < limit &&
      offered - received < c.pipeline && (!c.burstSize || burstRemaining)) {
      if (c.burstSize) --burstRemaining;
      unsigned serial = offered + 1;
      times[(serial - 1) % c.pipeline] = Zm::now();
      if (!c.warmup && !*begin) begin = times[0];
      if (!sendMessage(serial, false)) return;
      ++offered;
      if (offered - received > peak) peak = offered - received;
    }
    if (c.burstSize && !burstRemaining) scheduleBurst();
  }

  void fixEventRx(Zfix::SessionEvent e, ZmRef<Zfix::RxQueue::Node>) {
    if (e.actions & Zfix::SessionAction::Failure) state->fail("session", id);
    if (client && (e.actions & Zfix::SessionAction::Ready)) {
      if (state->config.offers.rate && !state->config.warmup) readyPhase();
      else { beginPhase(); fill(); }
    }
  }

  template <typename Entry>
  Zfix::Fmt::Error fixReceiveRx(Entry *, const typename Entry::Object &m,
      const Zfix::RequestContext &context) {
    if (rxCPU == ~0U) rxCPU = unsigned(sched_getcpu());
    auto &c = state->config;
    unsigned serial = received + 1;
    char buffer[10];
    auto number = serialText(serial, buffer);
    if (!context || m.possDup) {
      state->fail("receive", id, serial); return {};
    }
    bool valid = false;
    if constexpr (ZuIsSame<Entry, OrderReq>{}) {
      valid = !client && c.workload == 0 && m.id == number &&
        m.symbol == "TEST" && m.side == 1 &&
        m.transactTime == "20260101-00:00:00.000" &&
        m.quantity == 100 && m.ordType == 2 && m.price == "123.45";
    } else if constexpr (ZuIsSame<Entry, OrderRes>{}) {
      valid = client && c.workload == 0 && m.id == number &&
        m.execID == number && m.orderID == number && m.execType == 0 &&
        m.ordStatus == 0 && m.symbol == "TEST" && m.side == 1 &&
        m.quantity == 100 && m.price == "123.45" &&
        m.leavesQty == 100 && m.cumQty == 0 && m.avgPx == "0";
    } else if constexpr (ZuIsSame<Entry, MarketReq>{}) {
      valid = !client && c.workload == 1 && m.mdReqID == number &&
        m.subscription == 0 && m.depth == 1 && m.types.length() == 1 &&
        m.types[0].type == 0 && m.symbols.length() == 1 &&
        m.symbols[0].symbol == "TEST";
    } else if constexpr (ZuIsSame<Entry, MarketRes>{}) {
      valid = client && c.workload == 1 && m.mdReqID == number &&
        m.symbol == "TEST" && m.entries.length() == 1 &&
        m.entries[0].type == 0 && m.entries[0].price == "123.45" &&
        m.entries[0].size == 100;
    } else if constexpr (ZuIsSame<Entry, UpdateMsg>{}) {
      valid = c.workload == 2 && m.mdReqID == number &&
        m.entries.length() == 1 && m.entries[0].action == 0 &&
        m.entries[0].type == 0 && m.entries[0].id == number &&
        m.entries[0].symbol == "TEST" && m.entries[0].price == "123.45" &&
        m.entries[0].size == 100;
    }
    if (!valid) { state->fail("receive", id, serial); return {}; }
    ++received;
    if (serial > c.warmup) rxBytes += context.frame().wire.length();
    if (client) {
      auto now = c.offers.rate && serial > c.warmup ?
	ZuTime{ZuTime::Nano{monotonic()}} : Zm::now();
      if (serial > c.warmup) {
	uint64_t index = uint64_t(id) * c.messages + serial - c.warmup - 1;
	if (c.offers.rate) {
	  auto &sample = state->offers[index];
	  sample.completed = uint64_t(now.nanosecs());
	  if (!sample.admitted || sample.completed < sample.admitted)
	    state->fail("clock", id, serial);
	} else {
	  auto start = times[(serial - 1) % c.pipeline];
	  if (now <= start) state->fail("clock", id, serial);
	  else state->samples[index] = uint64_t((now - start).nanosecs());
	}
      }
      if (c.warmup && serial == c.warmup) {
	if (c.offers.rate) { readyPhase(); return {}; }
	begin = now; limit = c.warmup + c.messages;
	beginPhase();
      }
      if (serial == c.warmup + c.messages) {
        end = now; stopping = true; cancelBurst();
        if (c.telemetryDir) state->measuredLink();
        else self()->logout();
      } else fill();
    } else if (serial <= c.warmup + c.messages) sendMessage(serial, true);
    else state->fail("excess", id, serial);
    return {};
  }

  void fixStoppedRx(bool) {
    if (received != state->config.warmup + state->config.messages)
      state->fail("early-stop", id, received);
    state->stopped();
  }
};

struct ClientHub : Zfix::Client<ClientHub, Dictionary, Zfix::TCP> {
  using Base = Zfix::Client<ClientHub, Dictionary, Zfix::TCP>;
  struct Link;
  ZtArray<ZmRef<Link>, ZtArrayHeapID<"Zfix.Bench.Clients">> links;
  State *state;
  explicit ClientHub(State *s, Zfix::HubLimits limits) : Base{limits}, state{s} { }
  bool init(Zfix::TCP::ClientParams p);
};

struct ClientHub::Link : Zfix::CliLink<ClientHub, Link, Dictionary, Zfix::TCP>, Peer<Link> {
  using Base = Zfix::CliLink<ClientHub, Link, Dictionary, Zfix::TCP>;
  Link(ClientHub *hub, unsigned id) : Base{hub, ZuID{"fixBench"}, sessionConfig(true)},
    Peer<Link>{hub->state, id, true} { }
  void connected(Zfix::TCP::Connected event) {
    native = bool(this->cxn()->plugin());
    Base::connected(event);
  }
  using Peer<Link>::fixAssociateTx;
  using Peer<Link>::fixSubmittedTx;
  using Peer<Link>::fixDequeueTx;
  using Peer<Link>::fixCompleteTx;
  using Peer<Link>::fixFaultTx;
  using Peer<Link>::fixEventRx;
  using Peer<Link>::fixReceiveRx;
  using Peer<Link>::fixStoppedRx;
  using Peer<Link>::fixStopTx;
  using Peer<Link>::fixStopRx;
};

bool ClientHub::init(Zfix::TCP::ClientParams p) {
  if (p.multiplex()) {
    if (!p.rxAllocator()) p.rxAllocator(Link::rxAllocator(p.multiplex()));
    if (!p.txAllocator())
      p.txAllocator(p.multiplex()->allocator<Zi::IOBufType::TxTCP>(state->config.reserve));
  }
  return Base::init(ZuMv(p));
}

struct ServerHub : Zfix::Server<ServerHub, Dictionary, Zfix::TCP> {
  using Base = Zfix::Server<ServerHub, Dictionary, Zfix::TCP>;
  struct Link;
  State *state;
  ZtArray<ZmRef<Link>, ZtArrayHeapID<"Zfix.Bench.Servers">> links;
  explicit ServerHub(State *s, Zfix::HubLimits limits) : Base{limits}, state{s} {
    links.length(s->config.connections);
  }
  bool init(Zfix::TCP::ServerParams p);
  ZiIP localIP() const { return state->config.address; }
  unsigned localPort() const { return Port; }
  unsigned nAccepts() const { return state->config.connections; }
  void listening(const ZiListenInfo &) {
    *state->config.output << "ready=1\n" << std::flush; state->ready.post();
  }
  void listenFailed(bool) { state->fail("listen"); state->ready.post(); }
  ZiConnection *accepted(const ZiCxnInfo &info);
};

struct ServerHub::Link : Zfix::SrvLink<ServerHub, Link, Dictionary, Zfix::TCP>, Peer<Link> {
  using Base = Zfix::SrvLink<ServerHub, Link, Dictionary, Zfix::TCP>;
  Link(ServerHub *hub, unsigned id) : Base{hub, sessionConfig(false)},
    Peer<Link>{hub->state, id, false} { }
  void connected(Zfix::TCP::Connected event) {
    native = bool(this->cxn()->plugin());
    Base::connected(event);
  }
  using Peer<Link>::fixAssociateTx;
  using Peer<Link>::fixSubmittedTx;
  using Peer<Link>::fixDequeueTx;
  using Peer<Link>::fixCompleteTx;
  using Peer<Link>::fixFaultTx;
  using Peer<Link>::fixEventRx;
  using Peer<Link>::fixReceiveRx;
  using Peer<Link>::fixStoppedRx;
  using Peer<Link>::fixStopTx;
  using Peer<Link>::fixStopRx;
};

bool ServerHub::init(Zfix::TCP::ServerParams p) {
  if (p.multiplex()) {
    if (!p.rxAllocator()) p.rxAllocator(Link::rxAllocator(p.multiplex()));
    if (!p.txAllocator())
      p.txAllocator(p.multiplex()->allocator<Zi::IOBufType::TxTCP>(state->config.reserve));
  }
  return Base::init(ZuMv(p));
}

ZiConnection *ServerHub::accepted(const ZiCxnInfo &info) {
  unsigned id = state->created.load_();
  if (id >= links.length()) { state->fail("accept-count", id); return nullptr; }
  state->created.xchAdd(1);
  links[id] = new Link{this, id};
  return new Link::Cxn{links[id], info};
}

static int compare(const void *a, const void *b) {
  auto x = *static_cast<const uint64_t *>(a), y = *static_cast<const uint64_t *>(b);
  return (x > y) - (x < y);
}

static void quantiles(const char *name, Samples &values, std::ostream &out = std::cout) {
  qsort(values.data(), values.length(), sizeof(uint64_t), compare);
  uint64_t n = values.length(), over1 = 0, over10 = 0, over100 = 0;
  for (auto value : values) {
    over1 += value > 1000000; over10 += value > 10000000; over100 += value > 100000000;
  }
  out << "latency metric=" << name << " samples=" << n
    << " p50_ns=" << values[(n - 1)/2]
    << " p99_ns=" << values[((n - 1) * 99)/100]
    << " p999_ns=" << values[((n - 1) * 999)/1000]
    << " max_ns=" << values[n - 1] << " over_1ms=" << over1
    << " over_10ms=" << over10 << " over_100ms=" << over100 << '\n';
}

static bool reportOffers(const Config &c, State &state, auto &links, bool ok) {
  uint64_t observed = state.stopTime;
  if (state.failTime && (!observed || state.failTime < observed)) observed = state.failTime;
  if (!observed) observed = monotonic();
  int64_t offset = int64_t(Zm::now().nanosecs()) - int64_t(monotonic());
  (*c.output) << "offer_clock clock=monotonic origin_ns=" << state.origin
    << " observed_ns=" << observed << " realtime_offset_begin_ns=" << state.clockOffset
    << " realtime_offset_end_ns=" << offset << '\n';
  uint64_t finish = state.origin;
  for (unsigned i = 0; i < c.connections; ++i) {
    unsigned admitted = 0, completed = 0;
    for (unsigned j = 0; j < c.messages; ++j) {
      const auto &v = state.offers[uint64_t(i) * c.messages + j];
      admitted += bool(v.admitted); completed += bool(v.completed);
      if (v.completed > finish) finish = v.completed;
      if (v.completed && (!v.admitted || v.completed < v.admitted)) ok = false;
      if (v.admitted && v.admitted < state.origin + c.offers.offset(j)) ok = false;
    }
    unsigned due = state.origin && observed >= state.origin ?
      c.offers.due(observed - state.origin) : 0;
    (*c.output) << "offer_progress id=" << i << " planned=" << c.messages << " due=" << due
      << " admitted=" << admitted << " completed=" << completed
      << " unadmitted=" << c.messages - admitted << " uncompleted=" << c.messages - completed
      << " pending=" << (due > admitted ? due - admitted : 0)
      << " pending_peak=" << (links[i] ? links[i]->pendingPeak : 0) << '\n';
    if (admitted != c.messages || completed != c.messages) ok = false;
  }
  if (c.samples) {
    ZiFile file{c.samples, ZiFile::Write | ZiFile::GC};
    if (!file) return false;
    constexpr auto header =
      "connection,sequence,scheduled_ns,admitted_ns,completed_ns,status\n"_z;
    if (file.write(header.data(), header.length()) != Zi::OK) return false;
    for (unsigned i = 0; i < c.connections; ++i)
      for (unsigned j = 0; j < c.messages; ++j) {
	const auto &v = state.offers[uint64_t(i) * c.messages + j];
	ZuArray<char, 160> line;
	line << i << ',' << j + 1 << ',' << c.offers.offset(j) << ',';
	if (v.admitted) line << int64_t(v.admitted) - int64_t(state.origin);
	line << ',';
	if (v.completed) line << int64_t(v.completed) - int64_t(state.origin);
	line << ',' << (v.completed ? "completed" : v.admitted ? "admitted" : "unadmitted")
	  << '\n';
	if (file.write(line.data(), line.length()) != Zi::OK) return false;
      }
    if (file.close() != Zi::OK) return false;
  }
  if (!ok || !state.origin || finish <= state.origin) return false;
  Samples values;
  values.length(state.offers.length());
  for (unsigned metric = 0; metric < 3; ++metric) {
    for (unsigned i = 0; i < c.connections; ++i)
      for (unsigned j = 0; j < c.messages; ++j) {
	uint64_t k = uint64_t(i) * c.messages + j;
	const auto &v = state.offers[k];
	uint64_t due = state.origin + c.offers.offset(j);
	values[k] = !metric ? v.admitted - due :
	  metric == 1 ? v.completed - v.admitted : v.completed - due;
      }
    quantiles(!metric ? "admission" : metric == 1 ? "service" : "loaded", values, *c.output);
  }
  uint64_t n = values.length();
  double seconds = double(finish - state.origin)/1e9;
  (*c.output) << "measurement mode=scheduled clock=monotonic samples=" << n
    << " envelope_seconds=" << ZuBoxed(seconds).fp<9>() << " requests_per_second="
    << ZuBoxed(double(n)/seconds).fp<3>() << " p50_ns=" << values[(n - 1)/2]
    << " p99_ns=" << values[((n - 1) * 99)/100]
    << " p999_ns=" << values[((n - 1) * 999)/1000] << " max_ns=" << values[n - 1]
    << " schedule_span_ns=" << c.offers.offset(c.messages - 1)
    << " offered_requests_per_second=" << uint64_t(c.offers.rate) * c.connections << '\n';
  return true;
}

static bool report(const Config &c, State &state, auto &links) {
  bool ok = !state.failed.load_() && state.closed.load_() == c.connections;
  ZuTime begin, end;
  uint64_t count = 0, rxBytes = 0;
  for (unsigned i = 0; i < c.connections; ++i) {
    auto link = links[i];
    if (!link) { ok = false; continue; }
    ok &= link->received == c.warmup + c.messages &&
      link->completed == c.warmup + c.messages &&
      link->native == c.native &&
      link->rxCPU == c.appRxCPU && link->txCPU == c.appTxCPU;
    (*c.output) << "connection id=" << i << " plugin_actual=" << unsigned(link->native)
      << " rx_cpu_requested=" << c.rxCPU << " tx_cpu_requested=" << c.txCPU
      << " app_rx_cpu_actual=" << link->rxCPU
      << " app_tx_cpu_actual=" << link->txCPU
      << " pipeline_peak=" << link->peak << " application_bursts=" << link->bursts
      << " late_bursts=" << link->lateBursts << " received=" << link->received
      << " accepted=" << link->completed << " measured_rx_bytes=" << link->rxBytes << '\n';
    count += link->received; rxBytes += link->rxBytes;
    if (*link->begin && (!*begin || link->begin < begin)) begin = link->begin;
    if (*link->end && (!*end || link->end > end)) end = link->end;
  }
  (*c.output) << "result role=" << (c.server ? "server" : "client") << " backend="
    << c.backend << " exact=" << unsigned(ok)
    << " connections=" << c.connections << " pipeline=" << c.pipeline
    << " warmup=" << c.warmup << " measured_per_connection=" << c.messages
    << " workload=" << workloadName(c.workload) << " burst_size=" << c.burstSize
    << " burst_us=" << c.burstUS << " app_colocate=" << unsigned(c.appColocate)
    << " received_messages=" << count << " measured_rx_bytes=" << rxBytes
    << " offer_rate=" << c.offers.rate << " pending_cap=" << c.offers.cap << '\n';
  if (c.server) return ok;
  if (c.offers.rate) return reportOffers(c, state, links, ok);
  if (!ok) return false;
  if (c.samples) {
    ZiFile file{c.samples, ZiFile::Write | ZiFile::GC};
    if (!file) return false;
    constexpr auto header = "connection,sequence,rtt_ns\n"_z;
    if (file.write(header.data(), header.length()) != Zi::OK) return false;
    for (unsigned i = 0; i < c.connections; ++i)
      for (unsigned j = 0; j < c.messages; ++j) {
        ZuArray<char, 64> line;
        line << i << ',' << j + 1 << ',' << state.samples[uint64_t(i) * c.messages + j]
          << '\n';
        if (file.write(line.data(), line.length()) != Zi::OK) return false;
      }
    if (file.close() != Zi::OK) return false;
  }
  qsort(state.samples.data(), state.samples.length(), sizeof(uint64_t), compare);
  uint64_t n = state.samples.length();
  double seconds = double((end - begin).nanosecs())/1e9;
  if (!*begin || !*end || seconds <= 0 || !state.samples[0]) return false;
  (*c.output) << "measurement samples=" << n << " envelope_seconds="
    << ZuBoxed(seconds).fp<9>() << " requests_per_second="
    << ZuBoxed(double(n)/seconds).fp<3>() << " p50_ns="
    << state.samples[(n - 1)/2] << " p99_ns="
    << state.samples[((n - 1) * 99)/100] << " p999_ns="
    << state.samples[((n - 1) * 999)/1000] << " p9999_ns="
    << state.samples[((n - 1) * 9999)/10000] << " max_ns=" << state.samples[n - 1]
    << '\n';
  return true;
}

// The existing MaxCxns contract bounds nonmovable timer storage at startup.
struct CalStream {
  ZmScheduler::Timer timer;
  uint64_t serial = 0, alarm = 0, calls = 0, timers = 0, continuations = 0;
  unsigned observed = 0, peak = 0;
};

struct Calibration {
  const Config &config;
  ZmScheduler &scheduler;
  OfferTimes samples;
  CalStream streams[MaxCxns];
  ZmSemaphore ready, done, drained;
  unsigned cpus[3] = {~0U, ~0U, ~0U};
  unsigned finished = 0;
  uint64_t origin = 0, observed = 0;
  int64_t clockOffset = 0;
  bool stopping = false, failed = false;
  const char *failure = "none";

  Calibration(const Config &c, ZmScheduler &s) : config{c}, scheduler{s} {
    samples.length(uint64_t(c.connections) * c.messages);
  }

  void fail(const char *reason, uint64_t at) {
    if (failed) return;
    failed = stopping = true; failure = reason; observed = at;
    done.post();
  }

  void cancel(unsigned id) {
    auto &stream = streams[id];
    ++stream.serial;
    scheduler.del(&stream.timer);
    stream.alarm = 0;
  }

  void arm(unsigned id, uint64_t due) {
    auto &stream = streams[id];
    if (stream.alarm == due) return;
    cancel(id);
    stream.alarm = due;
    scheduler.add(&stream.timer, schedulerTime(due), ZmScheduler::Update,
      [this, id, serial = stream.serial](auto &&arm) {
	return arm([this, id, serial] {
	  auto &stream = streams[id];
	  if (serial != stream.serial) return;
	  stream.alarm = 0;
	  service(id, 1);
	});
      }, 1);
  }

  void service(unsigned id, unsigned source = 0) {
    const auto &c = config;
    auto &stream = streams[id];
    if (stopping || stream.observed == c.messages) return;
    ++stream.calls;
    if (source == 1) ++stream.timers;
    else if (source == 2) ++stream.continuations;
    for (unsigned n = 0; n < c.pipeline && stream.observed < c.messages; ++n) {
      auto now = monotonic();
      unsigned due = now >= origin ? c.offers.due(now - origin) : 0;
      unsigned pending = due > stream.observed ? due - stream.observed : 0;
      if (pending > stream.peak) stream.peak = pending;
      if (pending > c.offers.cap) { fail("offer-overflow", now); return; }
      if (!pending) break;
      auto &sample = samples[uint64_t(id) * c.messages + stream.observed];
      sample.admitted = now;
      // Match the bounded two-timestamp storage; this is not a reply completion.
      sample.completed = monotonic();
      ++stream.observed;
    }
    if (stream.observed == c.messages) {
      if (++finished == c.connections) {
	observed = samples[uint64_t(id + 1) * c.messages - 1].completed;
	done.post();
      }
      return;
    }
    auto due = origin + c.offers.offset(stream.observed);
    // Each stream has one chain: a future timer or one immediate continuation.
    if (due <= monotonic()) scheduler.run([this, id] { service(id, 2); }, 1);
    else arm(id, due);
  }

  void start() {
    origin = monotonic();
    clockOffset = int64_t(Zm::now().nanosecs()) - int64_t(origin);
    for (unsigned i = 0, n = config.connections; i < n; ++i) service(i);
  }

  void stop(bool timeout, uint64_t at) {
    if (timeout && !failed) fail("deadline", at);
    stopping = true;
    for (unsigned i = 0, n = config.connections; i < n; ++i) cancel(i);
    scheduler.run([this] { drained.post(); }, 1);
  }

  bool report() {
    const auto &c = config;
    uint64_t count = 0, calls = 0, timers = 0, continuations = 0;
    bool ok = !failed && finished == c.connections && origin && observed > origin;
    unsigned expected[] = {c.rxCPU, c.txCPU, unsigned(c.xdpTxCPU)};
    for (unsigned i = 0; i < 3; ++i) ok &= cpus[i] == expected[i];
    int64_t offset = int64_t(Zm::now().nanosecs()) - int64_t(monotonic());
    std::cout << "calibration_clock clock=monotonic origin_ns=" << origin
      << " observed_ns=" << observed << " realtime_offset_begin_ns=" << clockOffset
      << " realtime_offset_end_ns=" << offset << '\n';
    for (unsigned i = 0; i < c.connections; ++i) {
      const auto &stream = streams[i];
      for (unsigned j = 0; j < c.messages; ++j) {
	const auto &sample = samples[uint64_t(i) * c.messages + j];
	if (j < stream.observed) {
	  ok &= sample.admitted && sample.completed >= sample.admitted &&
	    sample.admitted >= origin + c.offers.offset(j);
	} else ok &= !sample.admitted && !sample.completed;
      }
      unsigned due = origin && observed >= origin ? c.offers.due(observed - origin) : 0;
      std::cout << "calibration_connection id=" << i << " planned=" << c.messages
	<< " due=" << due << " observed=" << stream.observed
	<< " missing=" << c.messages - stream.observed << " observed_pending_peak=" << stream.peak
	<< " calls=" << stream.calls << " timer_calls=" << stream.timers
	<< " continuation_calls=" << stream.continuations << '\n';
      count += stream.observed; calls += stream.calls;
      timers += stream.timers; continuations += stream.continuations;
    }
    if (c.samples) {
      ZiFile file{c.samples, ZiFile::Write | ZiFile::GC};
      if (!file) return false;
      constexpr auto header =
	"connection,sequence,scheduled_ns,observed_ns,recorded_ns,status\n"_z;
      if (file.write(header.data(), header.length()) != Zi::OK) return false;
      for (unsigned i = 0; i < c.connections; ++i)
	for (unsigned j = 0; j < c.messages; ++j) {
	  const auto &sample = samples[uint64_t(i) * c.messages + j];
	  ZuArray<char, 160> line;
	  line << i << ',' << j + 1 << ',' << c.offers.offset(j) << ',';
	  if (sample.admitted) line << sample.admitted - origin;
	  line << ',';
	  if (sample.completed) line << sample.completed - origin;
	  line << ',' << (sample.admitted ? "observed" : "pending") << '\n';
	  if (file.write(line.data(), line.length()) != Zi::OK) return false;
	}
      if (file.close() != Zi::OK) return false;
    }
    std::cout << "calibration exact=" << unsigned(ok) << " failure=" << failure
      << " connections=" << c.connections << " measured_per_connection=" << c.messages
      << " offer_rate=" << c.offers.rate << " pending_cap=" << c.offers.cap
      << " turn=" << c.pipeline << " observed=" << count
      << " planned=" << uint64_t(c.connections) * c.messages
      << " calls=" << calls << " timer_calls=" << timers
      << " continuation_calls=" << continuations
      << " rx_cpu_actual=" << cpus[0] << " tx_cpu_actual=" << cpus[1]
      << " sender_cpu_actual=" << cpus[2] << '\n';
    if (!ok) return false;
    Samples values;
    values.length(samples.length());
    for (unsigned i = 0; i < c.connections; ++i)
      for (unsigned j = 0; j < c.messages; ++j) {
	uint64_t k = uint64_t(i) * c.messages + j;
	const auto &sample = samples[k];
	values[k] = sample.admitted - origin - c.offers.offset(j);
      }
    quantiles("generator", values);
    for (uint64_t i = 0, n = samples.length(); i < n; ++i)
      values[i] = samples[i].completed - samples[i].admitted;
    quantiles("recording", values);
    double seconds = double(observed - origin)/1e9;
    std::cout << "calibration_rate envelope_seconds=" << ZuBoxed(seconds).fp<9>()
      << " observed_per_second=" << ZuBoxed(double(count)/seconds).fp<3>()
      << " target_per_second=" << uint64_t(c.offers.rate) * c.connections
      << " schedule_span_ns=" << c.offers.offset(c.messages - 1) << '\n';
    return true;
  }
};

static bool calibrate(const Config &c) {
  ZiMxParams params;
  params.rxBufSize(1<<20).txBufSize(1<<20).rxQuantum(8).rxThread(1).txThread(2)
    .scheduler([&c](auto &s) {
      s.nThreads(3).queueSize(c.queueSize).ll(c.ll).spin(c.spin.val())
	.quantum(ZuTime{ZuTime::Nano{1000}});
      unsigned cpus[] = {c.rxCPU, c.txCPU, unsigned(c.xdpTxCPU)};
      for (unsigned i = 0; i < 3; ++i) {
	ZmBitmap cpu;
	cpu.set(cpus[i]);
	s.thread(i + 1).isolated(true).cpuset(cpu);
      }
    });
  ZiMultiplex scheduler{ZuMv(params)};
  Calibration calibration{c, scheduler};
  std::cout << "calibration_config scheduler_threads=3 rx_sid=1 tx_sid=2 sender_sid=3"
    << " io_owner=ZiMultiplex rx_quantum=8 queue_size=" << scheduler.ring(1).size()
    << " scheduler_quantum_ns=1000 spin=" << c.spin.val()
    << " ll=" << unsigned(c.ll) << " turn=" << c.pipeline
    << " rx_cpu=" << c.rxCPU << " tx_cpu=" << c.txCPU << " sender_cpu=" << c.xdpTxCPU
    << " offer_rate=" << c.offers.rate << " pending_cap=" << c.offers.cap << '\n';
  if (!scheduler.start()) {
    calibration.failed = true; calibration.failure = "start";
    return calibration.report();
  }
  for (unsigned i = 0; i < 3; ++i)
    scheduler.run([&calibration, i] {
      calibration.cpus[i] = unsigned(sched_getcpu()); calibration.ready.post();
    }, i + 1);
  for (unsigned i = 0; i < 3; ++i) calibration.ready.wait();
  scheduler.run([&calibration] { calibration.start(); }, 1);
  bool timeout = calibration.done.timedwait(Zm::now(120));
  auto at = monotonic();
  scheduler.run([&calibration, timeout, at] { calibration.stop(timeout, at); }, 1);
  calibration.drained.wait();
  bool stopped = scheduler.stop();
  if (stopped) queueReport(scheduler);
  std::cout << "calibration_drain streams=" << c.connections
    << " scheduler_stopped=" << unsigned(stopped) << '\n';
  return calibration.report() && stopped;
}

static ZiMxParams endpointParams(const Config &c) {
  bool afxdp = c.afxdp();
  unsigned senderSid = 3;
  ZmSchedTParams sender;
  sender.isolated(true).name("zpktTx");
  if (afxdp) {
    ZmBitmap senderCPU;
    senderCPU.set(unsigned(c.xdpTxCPU));
    sender.cpuset(senderCPU);
  }
  ZiMxParams params;
  params.rxBufSize(c.native ? c.reserve : 1<<20).txBufSize(1<<20)
    .rxQuantum(8).rxThread(1).txThread(2).scheduler([&c, &sender, senderSid](auto &s) {
      ZmBitmap rx, tx;
      rx.set(c.rxCPU); tx.set(c.txCPU);
      if (*c.spin) s.spin(c.spin.val());
      s.nThreads(senderSid).queueSize(c.queueSize).ll(c.ll)
        .quantum(ZuTime{ZuTime::Nano{1000}})
        .thread(1, [&rx](auto &t) { t.isolated(true).cpuset(rx); })
        .thread(2, [&tx](auto &t) { t.isolated(true).cpuset(tx); });
      s.thread(senderSid) = sender;
    });
  return params;
}

struct PacketEndpoint {
  Zpkt::Nif *nif = nullptr; // retained by Zutcp after successful init
  Zpkt::CfString module;
  unsigned routeIfindex = 0;
#ifdef HAVE_DPDK
  uint16_t port = RTE_MAX_ETHPORTS;
#endif
};

using CfNode = ZfCf::AnyNode;

static CfNode *cfObject(CfNode *parent, ZuCSpan name) {
  auto child = ZfCf::newNode<CfNode::Object>(parent);
  auto ptr = child.ptr();
  parent->data<CfNode::Object>().push(CfNode::Field{name, ZuMv(child)});
  return ptr;
}

static void cfValue(CfNode *parent, ZuCSpan name, const auto &input) {
  CfNode::String text;
  text << input;
  parent->data<CfNode::Object>().push(CfNode::Field{
    name, ZfCf::newNode<CfNode::String>(parent, ZuMv(text))});
}

static const char *packetSelector(const Config &c, bool pair) {
  if (!pair) return c.interfaceName;
  return c.server ? "zfix_ring_peer" : "_loop";
}

#ifdef HAVE_DPDK
static const char *ringPortName(const Config &c) {
  return c.server ? "net_ring_zfix_server" : "net_ring_zfix_client";
}

static void initDpdkEnv(const Config &c, Zpkt::Env *env, PacketEndpoint &packet) {
  auto loaded = ZvCf::load(c.dpdkConfig);
  auto root = loaded.p<1>().ptr();
  if (!root || !root->has<CfNode::Object>())
    throw ZeEXCEPT(Error, "zfixbench", "BENCH_DPDK_CONFIG must contain an Env configuration");
  bool haveModule = false;
  for (const auto &field : root->data<CfNode::Object>())
    if (field.p<0>() == "module"_z) { haveModule = true; break; }
  // The loader allocates the underlying tree for this invocation.
  if (!haveModule) cfValue(const_cast<CfNode *>(root), "module", ZFIX_DPDK_MODULE);
  Zpkt::PluginLoadCf load;
  ZfCf::handler<Zpkt::PluginLoadCf>(root).update(load);
  Zpkt::Dpdk::PluginCf config;
  ZfCf::handler<Zpkt::Dpdk::PluginCf>(root).update(config);
  auto entry = config.interfaces->find(c.interfaceName);
  if (!entry)
    throw ZeEXCEPT(Error, "zfixbench", "BENCH_DPDK_CONFIG has no entry for the selected interface");
  packet.module = ZuMv(load.module);
  env->init(root);
  if (int rc = rte_eth_dev_get_port_by_name(entry->val().port, &packet.port))
    throw ZeEXCEPT(Error, "zfixbench", ([code = ZeError{-rc}](auto &out) {
      out << "resolve configured DPDK port: " << code;
    }));
}
#endif

static void initPacketEnv(const Config &c, Zpkt::Env *env, PacketEndpoint &packet) {
#ifdef HAVE_DPDK
  if (c.dpdk()) { initDpdkEnv(c, env, packet); return; }
#endif
  auto root = ZfCf::newNode<CfNode::Object>(nullptr);
  auto nif = cfObject(cfObject(root.ptr(), "interfaces"), c.interfaceName);
  packet.module = c.afxdp() ? c.xdpModule : ZFIX_AFP_MODULE;
  cfValue(root.ptr(), "module", packet.module);
  if (*c.rxSpin) cfValue(nif, "rxSpin", c.rxSpin.val());
  if (c.afxdp()) {
    cfValue(nif, "txSid", 3);
    if (*c.txSpin) cfValue(nif, "txSpin", c.txSpin.val());
  } else if (c.afpBypass >= 0)
    cfValue(nif, "qdiscBypass", c.afpBypass ? "true" : "false");
  env->init(root.ptr());
}

static bool initEndpoint(
  const Config &c, ZiMultiplex &mx, Zpkt::Env *env, PacketEndpoint &packet, bool sharedEnv = false)
{
  bool afxdp = c.afxdp();
  enum { SenderSid = 3 };
  ZiMxParams defaults;
  auto requestHash = defaults.requestHash();
  *c.output << "config backend=" << c.backend
    << " pmd=" << (sharedEnv ? "net_ring" : afxdp ? "af_xdp" :
      c.afp() ? "af_packet" : c.dpdk() ? "configured" : "none")
    << " rx_cpu=" << c.rxCPU << " tx_cpu=" << c.txCPU
    << " app_rx_cpu=" << c.appRxCPU << " app_tx_cpu=" << c.appTxCPU
    << " app_colocate=" << unsigned(c.appColocate)
    << " scheduler_threads=" << SenderSid << " xdp_tx_sid=" << SenderSid
    << " queue_size=" << mx.ring(1).size()
    << " native_xdp_sender_threads=" << unsigned(afxdp)
    << " xdp_tx_cpu=" << (afxdp ? c.xdpTxCPU : -1)
    << " rx_thread=1 tx_thread=2 app_rx_thread=" << (c.appColocate ? 1 : 3)
    << " app_tx_thread=" << (c.appColocate ? 2 : 4)
    << " spin=" << (*c.spin ? c.spin.val() : ZmSchedParams{}.spin())
    << " spin_inherited=" << unsigned(!*c.spin) << " ll=" << unsigned(c.ll)
    << " rx_spin=" << (afxdp || c.afp() ?
      int64_t(*c.rxSpin ? c.rxSpin.val() : (Zi_DefltSpin<<2)) : -1)
    << " tx_spin=" << (afxdp ? int64_t(*c.txSpin ? c.txSpin.val() : Zi_DefltSpin) : -1)
    << " allocation_tuning=" << unsigned(c.allocationTuning) << " hash_bits=" << c.hashBits
    << " request_hash=" << requestHash
    << " congestion_control=" << unsigned(!c.native || c.congestionControl)
    << " tx_batch=" << unsigned(c.native && c.txBatch)
    << " tcp_window_bytes=" << (c.native ? c.tcpWindow : 0)
    << " afp_qdisc_bypass_override=" << c.afpBypass
    << " offer_rate=" << c.offers.rate << " pending_cap=" << c.offers.cap << '\n';
  if (!c.native) return true;
  ZmRef<Zpkt::Nif> nif;
  try {
    if (!sharedEnv) initPacketEnv(c, env, packet);
#ifdef HAVE_DPDK
    else {
      packet.module = ZFIX_DPDK_MODULE;
      if (int rc = rte_eth_dev_get_port_by_name(ringPortName(c), &packet.port))
	throw ZeEXCEPT(Error, "zfixbench", ([code = ZeError{-rc}](auto &out) {
	  out << "resolve fixture DPDK port: " << code;
	}));
    }
#endif
    ZuCSpan selector = packetSelector(c, sharedEnv);
    if (selector == "_loop"_z && !sharedEnv && !env->loopback())
      throw ZeEXCEPT(Error, "zfixbench", "the loaded packet backend lacks loopback");
    auto route = sharedEnv ? c.interfaceName : c.routeInterface;
    if (!route && c.dpdk() && selector != "_loop"_z) route = c.interfaceName;
    if (route && !(packet.routeIfindex = if_nametoindex(route)))
      throw ZeEXCEPT(Error, "zfixbench", ([route = ZeString{route}](auto &out) {
	out << "failed to resolve routing interface " << route;
      }));
    auto opened = env->nif(&mx, selector);
    if (opened.is<ZeException>()) throw ZuMv(opened).p<ZeException>();
    nif = opened.p<Zpkt::Nif *>();
    if (!packet.routeIfindex) packet.routeIfindex = nif->ifindex();
    Zutcp::MxPlugin::Params settings{.packetCount = c.reserve, .routingIfindex = packet.routeIfindex,
      .maxData = c.tcpWindow,
      .ackHoldTimeout = ZuTime{0, int32_t(c.ackHoldUS * 1000)},
      .congestionControl = c.congestionControl, .txBatch = c.txBatch};
    auto ptr = nif.ptr();
    auto stack = static_cast<Zutcp::MxPlugin *>(mx.plugin());
    stack->init(&mx, env, ZuMv(nif), ZuMv(settings));
    packet.nif = ptr;
  } catch (const ZeException &e) {
    if (nif) {
      nif->final();
      if (auto error = nif->error()) ZiLogEvent(ZuMv(error));
    }
    ZiLogEvent(e);
    return false;
  }
  return true;
}

static void packetDrops(const Config &c, const PacketEndpoint &packet, const char *interval) {
  if (!packet.nif) return;
#ifdef HAVE_DPDK
  if (c.dpdk()) {
    rte_eth_stats stats{};
    if (!rte_eth_stats_get(packet.port, &stats))
      *c.output << "packet_drops source=rte_eth_stats interval=" << interval
	<< " cumulative_imissed=" << stats.imissed << " cumulative_ierrors=" << stats.ierrors
	<< " cumulative_oerrors=" << stats.oerrors << " cumulative_rx_nombuf=" << stats.rx_nombuf
	<< '\n';
    return;
  }
#endif
  uint64_t full = 0;
  unsigned rxQueues = 0;
  packet.nif->allQueues([&full, &rxQueues](Ztc::Queue *queue) {
    Ztc::QueueTelemetry data;
    queue->telemetry(data);
    if (data.type != Ztc::QueueType::Rx) return;
    full += data.full;
    ++rxQueues;
  });
  if (!rxQueues) return;
  *c.output << "packet_drops source="
    << (c.afp() ? "PACKET_STATISTICS.tp_drops" : "XDP_STATISTICS.rx_ring_full")
    << " interval=" << interval << " cumulative_rx_full=" << full << '\n';
  if (c.afp()) packet.nif->status(); // same per-run drop total, with software discards
}

#ifdef HAVE_DPDK
static void dpdkStatus(std::ostream &out, uint16_t port) {
  rte_eth_dev_info info{};
  if (rte_eth_dev_info_get(port, &info)) return;
  char name[RTE_ETH_NAME_MAX_LEN];
  out << "packet_dpdk pmd=" << (info.driver_name ? info.driver_name : "unknown")
    << " port_id=" << port
    << " port_name=" << (rte_eth_dev_get_name_by_port(port, name) ? "unknown" : name)
    << " rx_queues=" << info.nb_rx_queues << " tx_queues=" << info.nb_tx_queues;
  rte_eth_conf conf{};
  if (!rte_eth_dev_conf_get(port, &conf))
    out << " rx_offloads=0x" << ZuBoxed(conf.rxmode.offloads).hex()
      << " tx_offloads=0x" << ZuBoxed(conf.txmode.offloads).hex()
      << " rx_queue_interrupts=" << unsigned(conf.intr_conf.rxq);
  rte_eth_link link{};
  if (!rte_eth_link_get_nowait(port, &link))
    out << " link_up=" << unsigned(link.link_status) << " link_mbps=" << link.link_speed;
  out << '\n';
  for (uint16_t q = 0; q < info.nb_rx_queues; ++q) {
    rte_eth_rxq_info queue{};
    if (!rte_eth_rx_queue_info_get(port, q, &queue))
      out << "packet_dpdk_queue direction=rx queue=" << q << " descriptors=" << queue.nb_desc
	<< " offloads=0x" << ZuBoxed(queue.conf.offloads).hex() << '\n';
  }
  for (uint16_t q = 0; q < info.nb_tx_queues; ++q) {
    rte_eth_txq_info queue{};
    if (!rte_eth_tx_queue_info_get(port, q, &queue))
      out << "packet_dpdk_queue direction=tx queue=" << q << " descriptors=" << queue.nb_desc
	<< " offloads=0x" << ZuBoxed(queue.conf.offloads).hex() << '\n';
  }
  unsigned id;
  RTE_LCORE_FOREACH(id) {
    auto cpus = rte_lcore_cpuset(id);
    out << "packet_dpdk_lcore id=" << id << " main=" << unsigned(id == rte_get_main_lcore())
      << " cpus=";
    bool first = true;
    for (unsigned cpu = 0; cpu < CPU_SETSIZE; ++cpu) if (CPU_ISSET(cpu, &cpus)) {
      if (!first) out << ',';
      out << cpu;
      first = false;
    }
    out << '\n';
  }
}
#endif

static void packetStatus(const Config &c, const PacketEndpoint &packet) {
  auto nif = packet.nif;
  if (!nif) return;
  ZuCArray<18> mac;
  for (auto octet : nif->mac()) {
    if (mac.length()) mac << ':';
    mac << ZuBoxed(unsigned(octet)).hex<false, ZuFmt::Right<2>>();
  }
  *c.output << "packet_backend module=" << packet.module
    << " packet_selector=" << nif->name() << " interface=" << c.interfaceName
    << " packet_ifindex=" << nif->ifindex() << " routing_ifindex=" << packet.routeIfindex
    << " mac=" << mac << " mtu=" << nif->mtu() << " rx_queues=" << nif->rxQueues()
    << " zero_copy=" << unsigned(nif->zeroCopy())
    << " tcp_checksum_offload=" << unsigned(nif->tcpChksumOffload()) << '\n';
  if (!c.afp()) nif->status();
#ifdef HAVE_DPDK
  if (c.dpdk()) dpdkStatus(*c.output, packet.port);
#endif
  packetDrops(c, packet, "activation");
}

#ifdef HAVE_DPDK
struct RingRun {
  ZpktDpdkTest::RingPort ports[2];
  ZpktDpdkTest::Wire wire;
  ZmSemaphore ready, drained;
  ZmAtomic<unsigned> drainCount{0};
  bool finished[2]{}, listening = false;
  rte_mempool *serverPool = nullptr;
  ZmThreadParams wireThread;

  void retire(bool server) {
    if (finished[server]) return;
    finished[server] = true;
    if (++drainCount == 2) {
      wire.quiesce();
      drained.post();
    } else drained.wait();
  }
};
#else
struct RingRun;
#endif

static bool run(const Config &c, State &state, ZiMultiplex &mx, const PacketEndpoint &packet,
    RingRun *ring = nullptr, State *peer = nullptr) {
#ifdef HAVE_DPDK
  auto ringRx = ring ? mx.allocator<Zi::IOBufType::Rx>(c.reserve) : nullptr;
  if (ring && !ringRx) return false;
#else
  (void)ring;
#endif
  Zfix::HubLimits limits;
  uint64_t budget = uint64_t(c.pipeline) * 1024 + 65536;
  limits.submit.bytes = budget * c.connections;
  limits.link.postedSubmitBytes = budget;
  limits.link.tx.queuedOutputBytes = budget;
  limits.link.rx.codec.frameBytes = 2048;
  limits.link.tx.codec.frameBytes = 2048;
  bool ok = false, stopped = false;
  if (c.server) {
    ServerHub hub{&state, limits};
    if (!hub.init(Zfix::TCP::ServerParams{&mx, c.appColocate ? "1" : "3",
          c.appColocate ? "2" : "4"}) || !mx.start()) return false;
    if (!hub.start()) { mx.stop(); return false; }
    packetStatus(c, packet);
    if (c.telemetryDir) state.trafficStart();
    hub.listen();
    bool listening = !state.ready.timedwait(Zm::now(30)) && !state.failed.load_();
#ifdef HAVE_DPDK
    if (ring) {
      if (listening) {
        ring->serverPool = ZpktDpdkTest::native(ringRx->pool.ptr());
        ring->listening = bool(ring->serverPool);
      }
      ring->ready.post();
    }
#endif
    if (listening) {
      ok = !c.telemetryDir || telemetryBarrier(state, mx);
      if (ok) ok = !state.done.timedwait(Zm::now(120));
    }
    if (peer && (!ok || state.failed.load_())) peer->fail("ring-server");
    packetDrops(c, packet, "activation_through_fix_close");
    // The server stop barrier revokes listener admission and drains accepted
    // transports. FIX owner callbacks finish afterwards on the live scheduler.
    hub.stop();
    state.drain(state.created.load_());
    hub.final();
#ifdef HAVE_DPDK
    if (ring) ring->retire(true);
#endif
    stopped = mx.stop();
    if (stopped) ok = drainedTelemetry(c, mx) && ok;
    ok = report(c, state, hub.links) && ok;
  } else {
    ClientHub hub{&state, limits};
    auto &links = hub.links;
    links.length(c.connections);
    if (!hub.init(Zfix::TCP::ClientParams{&mx, c.appColocate ? "1" : "3",
          c.appColocate ? "2" : "4"}) || !mx.start()) return false;
    if (!hub.start()) { mx.stop(); return false; }
    packetStatus(c, packet);
#ifdef HAVE_DPDK
    if (ring) {
      auto wired = ring->wire.start({&ring->ports[0], ZpktDpdkTest::native(ringRx->pool.ptr())},
        {&ring->ports[1], ring->serverPool}, ring->wireThread);
      if (wired.is<ZeException>()) {
        ZiLogEvent(wired.p<ZeException>());
        hub.stop(); hub.final(); mx.stop(); return false;
      }
    }
#endif
    for (unsigned i = 0; i < c.connections; ++i)
      links[i] = new ClientHub::Link{&hub, i};
    if (c.telemetryDir) state.trafficStart();
    for (auto &link : links) link->connect(c.addressText, Port);
    ok = !c.telemetryDir || telemetryBarrier(state, mx);
    if (ok && c.telemetryDir)
      hub.rxRun([hub = &hub] {
	for (const auto &link : hub->links) if (link) link->logout();
      });
    if (ok) ok = !state.done.timedwait(Zm::now(120));
    if (peer && (!ok || state.failed.load_())) peer->fail("ring-client");
    if (c.offers.rate) {
      if (!state.stopTime) state.stopTime = monotonic();
      if (!ok) state.fail("deadline");
    }
    packetDrops(c, packet, "activation_through_fix_close");
    hub.rxRun([hub = &hub] {
      for (const auto &link : hub->links) if (link) {
	link->stopping = true; link->cancelBurst(); link->disconnect_();
      }
    });
    // A failed attempt also retires through fixStoppedRx. Consume every
    // initiated link's terminal post, including links already stopped.
    state.drain(c.connections);
    hub.stop(); hub.final();
#ifdef HAVE_DPDK
    if (ring) ring->retire(false);
#endif
    stopped = mx.stop();
    if (stopped) ok = drainedTelemetry(c, mx) && ok;
    ok = report(c, state, links) && ok;
  }
  if (stopped) queueReport(mx, *c.output);
  state.trafficReport();
  return ok && stopped;
}

#ifdef HAVE_DPDK
static bool enterNetwork(const char *path) {
  int fd = ::open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  int rc = setns(fd, CLONE_NEWNET);
  ::close(fd);
  return rc == 0;
}

static bool runRingPair(Config &client, Config &server, const char *clientNetns,
    const char *serverNetns, const char *clientOutput, const char *serverOutput) {
  std::ofstream clientFile{clientOutput}, serverFile{serverOutput};
  if (!clientFile || !serverFile) return false;
  client.output = &clientFile;
  server.output = &serverFile;
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  RingRun ring;
  int wireCPU = -1;
  cpu_set_t allowed;
  if (!sched_getaffinity(0, sizeof(allowed), &allowed)) {
    for (unsigned cpu = 0; cpu < CPU_SETSIZE; ++cpu)
      if (CPU_ISSET(cpu, &allowed) && cpu != client.rxCPU && cpu != client.txCPU &&
	  cpu != server.rxCPU && cpu != server.txCPU) {
	wireCPU = int(cpu);
	ZmBitmap cpuset;
	cpuset.set(cpu);
	ring.wireThread.cpuset(cpuset);
	break;
      }
  }
  std::cout << "pair_config pmd=net_ring wire_cpu=" << wireCPU << '\n';
  {
    auto root = ZfCf::newNode<CfNode::Object>(nullptr);
    cfValue(root.ptr(), "module", ZFIX_DPDK_MODULE);
    auto interfaces = cfObject(root.ptr(), "interfaces");
    for (const auto *c : {&client, &server}) {
      auto nif = cfObject(interfaces, packetSelector(*c, true));
      cfValue(nif, "port", ringPortName(*c));
      if (*c->spin) cfValue(nif, "spin", c->spin.val());
    }
    auto eal = cfObject(root.ptr(), "eal");
    for (auto flag : {"noPCI", "noHuge", "noShconf", "noTelemetry"})
      cfValue(eal, flag, "true");
    cfValue(eal, "iova", "va");
    try { env->init(root.ptr()); }
    catch (const ZeException &e) { ZiLogEvent(e); return false; }
  }
  if (!env->loopback()) {
    std::cerr << "the DPDK fixture requires backend loopback\n";
    return false;
  }
  constexpr unsigned RingSize = 4096;
  if (auto r = ring.ports[0].init("zfix_client", RingSize, {{2, 0, 0, 0, 0x87, 1}});
      r.is<ZeException>()) { ZiLogEvent(r.p<ZeException>()); return false; }
  if (auto r = ring.ports[1].init("zfix_server", RingSize, {{2, 0, 0, 0, 0x87, 2}});
      r.is<ZeException>()) { ZiLogEvent(r.p<ZeException>()); return false; }
  State clientState{client}, serverState{server};
  ZiMultiplex clientMx{endpointParams(client), ZmRef<Zutcp::MxPlugin>{new Zutcp::MxPlugin}};
  ZiMultiplex serverMx{endpointParams(server), ZmRef<Zutcp::MxPlugin>{new Zutcp::MxPlugin}};
  auto endpoint = [&env, &ring](
      Config &c, State &state, State &peer, ZiMultiplex &mx, const char *netns) {
    if (!enterNetwork(netns)) {
      state.fail("ring-control-interface");
      return false;
    }
    PacketEndpoint packet;
    if (!initEndpoint(c, mx, env.ptr(), packet, true)) return false;
    return run(c, state, mx, packet, &ring, &peer);
  };
  bool serverOK = false;
  ZmThread serverThread;
  if (serverThread.run([&endpoint, &ring, &clientState, &server, &serverState,
      &serverMx, &serverOK, serverNetns] {
    serverOK = endpoint(server, serverState, clientState, serverMx, serverNetns);
    if (!serverOK) clientState.fail("ring-server");
    ring.ready.post();
    ring.retire(true);
  })) return false;
  // The server has opened its Nif before the client accesses Env. Both
  // multiplexers remain owned here and finalize serially after the join.
  ring.ready.wait();
  bool clientOK = ring.listening &&
    endpoint(client, clientState, serverState, clientMx, clientNetns);
  if (!clientOK) serverState.fail("ring-client");
  ring.retire(false);
  bool joined = serverThread.join() == 0;
  ring.wire.stop();
  for (auto &port : ring.ports) port.drain();
  return clientOK && serverOK && joined && bool(clientFile) && bool(serverFile);
}
#endif

} // FixBench

static bool scheduleTest() {
  using namespace FixBench;
  OfferSchedule third{3, 8, 3};
  constexpr uint64_t offsets[] = {
    0, 333333333, 666666666, 1000000000, 1333333333, 1666666666, 2000000000, 2333333333};
  for (unsigned i = 0; i < 8; ++i) {
    if (third.offset(i) != offsets[i] || third.due(offsets[i]) != i + 1) return false;
    if (i && third.due(offsets[i] - 1) != i) return false;
  }
  if (third.due(9000000000) != 8) return false;
  // Two requests occupy both slots. The fourth pending offer exceeds cap three.
  unsigned alarm = third.alarm(2, 0, 2);
  if (alarm != 5 || third.due(third.offset(alarm) - 1) - 2 != 3 ||
      third.due(third.offset(alarm)) - 2 != 4) return false;
  // A delayed response frees one slot; the third offer keeps its old deadline.
  if (third.alarm(2, 1, 2) != 2 || third.offset(2) != 666666666) return false;
  uint64_t admitted = 1500000000, completed = 1600000000;
  uint64_t delay = admitted - third.offset(2), service = completed - admitted;
  if (delay != 833333334 || service != 100000000 ||
      delay + service != completed - third.offset(2)) return false;
  // Catch-up admission moves the full-pipeline overflow deadline; final count is finite.
  if (third.alarm(3, 1, 2) != 6 || third.alarm(6, 4, 2) != 8 ||
      third.alarm(8, 8, 2) != 8) return false;
  OfferSchedule dense{1000000000, 3, 1}, single{1, 1, 1};
  if (dense.offset(2) != 2 || dense.due(0) != 1 || dense.due(1) != 2 ||
      dense.due(2) != 3 || single.due(0) != 1 || single.alarm(1, 0, 1) != 1)
    return false;
  // Highest aggregate sample count at the calibration headroom rate.
  OfferSchedule full{240000, MaxSamples, 4096};
  if (full.offset(MaxSamples - 1) != 8333329166 ||
      full.due(8333329165) != MaxSamples - 1 || full.due(8333329166) != MaxSamples)
    return false;
  Config c;
  c.messages = 4; c.connections = 2; c.offers = {3, 4, 2};
  FixBench::State state{c};
  if (state.offers.length() != 8 || state.samples.length()) return false;
  for (const auto &sample : state.offers)
    if (sample.admitted || sample.completed) return false;
  return true;
}

static bool selftest() {
  using namespace FixBench;
  if (!scheduleTest()) return false;
  constexpr auto number = "0000000001";
  Zfix::Fmt::Error error;
  Order order; order.id = number;
  auto d = Zfix::TxBody::encode<OrderReq>(order, 1, error);
  if (!d || error || d->msgType() != "D") return false;
  Execution execution; execution.id = execution.execID = execution.orderID = number;
  auto e = Zfix::TxBody::encode<OrderRes>(execution, 1, error);
  if (!e || error || e->msgType() != "8") return false;
  MarketRequest request; request.mdReqID = number;
  request.types.push(EntryType{}); request.symbols.push(RelatedSymbol{});
  auto v = Zfix::TxBody::encode<MarketReq>(request, 1, error);
  if (!v || error || v->msgType() != "V") return false;
  Snapshot snapshot; snapshot.mdReqID = number; snapshot.entries.push(SnapshotEntry{});
  auto w = Zfix::TxBody::encode<MarketRes>(snapshot, 1, error);
  if (!w || error || w->msgType() != "W") return false;
  Update update; update.mdReqID = number;
  UpdateEntry entry; entry.id = number; update.entries.push(entry);
  auto x = Zfix::TxBody::encode<UpdateMsg>(update, 1, error);
  return bool(x) && !error && x->msgType() == "X";
}

static int calibrateMain(int argc, char **argv) {
  using namespace FixBench;
  if (argc != 11 && argc != 12) return 2;
  Config c;
  if (!queueConfig(c)) return 2;
  c.messages = unsigned(ZuBox<unsigned>{argv[2]});
  c.connections = unsigned(ZuBox<unsigned>{argv[3]});
  c.offers.rate = unsigned(ZuBox<unsigned>{argv[4]});
  c.offers.cap = unsigned(ZuBox<unsigned>{argv[5]});
  c.offers.count = c.messages;
  c.pipeline = unsigned(ZuBox<unsigned>{argv[6]});
  c.rxCPU = unsigned(ZuBox<unsigned>{argv[7]});
  c.txCPU = unsigned(ZuBox<unsigned>{argv[8]});
  c.xdpTxCPU = int(ZuBox<int>{argv[9]});
  c.spin = unsigned(ZuBox<unsigned>{argv[10]});
  if (argc == 12) c.samples = argv[11];
  if (auto value = getenv("BENCH_LL")) {
    if (strcmp(value, "0") && strcmp(value, "1")) return 2;
    c.ll = *value == '1';
  }
  if (!c.messages || !c.connections || c.connections > MaxCxns ||
      uint64_t(c.messages) * c.connections > MaxSamples ||
      !c.pipeline || c.pipeline > MaxPipeline || !c.offers.rate ||
      c.offers.rate > 1000000000 || !c.offers.cap || c.offers.cap > MaxSamples ||
      c.offers.offset(c.messages - 1) >= 110000000000) return 2;
  cpu_set_t allowed;
  if (sched_getaffinity(0, sizeof(allowed), &allowed)) return 2;
  unsigned cpus[] = {c.rxCPU, c.txCPU, unsigned(c.xdpTxCPU)};
  for (unsigned cpu : cpus)
    if (cpu >= CPU_SETSIZE || !CPU_ISSET(cpu, &allowed)) return 2;
  ZiLog::init("zfixbench"); ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2"))); ZiLog::start();
  bool ok = calibrate(c);
  ZiLog::stop();
  return ok ? 0 : 1;
}

int main(int argc, char **argv) {
  using namespace FixBench;
  if (argc == 2 && !strcmp(argv[1], "--selftest")) return selftest() ? 0 : 1;
  if (argc > 1 && !strcmp(argv[1], "--calibrate")) return calibrateMain(argc, argv);
  if (argc != 13 && argc != 14) return 2;
  Config c;
  if (!queueConfig(c)) return 2;
  c.server = !strcmp(argv[1], "server");
  bool pair = !strcmp(argv[1], "pair");
  if (!pair && !c.server && strcmp(argv[1], "client")) return 2;
  c.backend = argv[2];
  if (pair && !c.dpdk()) return 2;
  if (c.dpdk()) {
#ifdef HAVE_DPDK
    if (!pair) {
      c.dpdkConfig = getenv("BENCH_DPDK_CONFIG");
      if (!c.dpdkConfig || !*c.dpdkConfig) {
	std::cerr << "dpdk requires BENCH_DPDK_CONFIG\n";
	return 2;
      }
    }
#else
    std::cerr << "zfixbench was built without DPDK support\n";
    return 2;
#endif
  } else if (!c.afxdp() && !c.afp() && strcmp(c.backend, "kernel")) return 2;
  c.native = c.afxdp() || c.afp() || c.dpdk();
  c.interfaceName = argv[3];
  if (c.native && !*c.interfaceName) return 2;
  if (c.native && !pair) c.routeInterface = getenv("BENCH_ROUTE_INTERFACE");
  if (c.afxdp())
    if (auto path = getenv("BENCH_XDP_MODULE")) {
      if (!*path) return 2;
      c.xdpModule = path;
    }
  if (c.native)
    if (auto value = getenv("BENCH_TCP_WINDOW_BYTES")) {
      if (*value < '1' || *value > '9') return 2;
      char *end;
      auto bytes = strtoull(value, &end, 10);
      if (*end || bytes > Zutcp::DefaultMaxData) return 2;
      c.tcpWindow = bytes;
    }
  if (c.afp()) {
    if (auto value = getenv("BENCH_AFP_QDISC_BYPASS")) {
      if (strcmp(value, "0") && strcmp(value, "1")) return 2;
      c.afpBypass = *value == '1';
    }
  }
  c.address = ZiIP{argv[4]};
  c.addressText = argv[4];
  c.messages = unsigned(ZuBox<unsigned>{argv[5]});
  c.warmup = unsigned(ZuBox<unsigned>{argv[6]});
  c.pipeline = unsigned(ZuBox<unsigned>{argv[7]});
  c.connections = unsigned(ZuBox<unsigned>{argv[8]});
  c.reserve = unsigned(ZuBox<unsigned>{argv[9]});
  c.rxCPU = unsigned(ZuBox<unsigned>{argv[10]});
  c.txCPU = unsigned(ZuBox<unsigned>{argv[11]});
  c.appRxCPU = c.rxCPU; c.appTxCPU = c.txCPU;
  if (strcmp(argv[12], "default")) c.spin = unsigned(ZuBox<unsigned>{argv[12]});
  if (auto value = getenv("BENCH_RX_SPIN")) {
    c.rxSpin = ZuBox<unsigned>{value};
    if (!*c.rxSpin) return 2;
  }
  if (auto value = getenv("BENCH_TX_SPIN")) {
    c.txSpin = ZuBox<unsigned>{value};
    if (!*c.txSpin) return 2;
  }
  if (auto value = getenv("BENCH_LL")) {
    if (strcmp(value, "0") && strcmp(value, "1")) return 2;
    c.ll = *value == '1';
  }
  if (auto value = getenv(c.server ? "BENCH_SERVER_XDP_TX_CPU" : "BENCH_CLIENT_XDP_TX_CPU")) {
    c.xdpTxCPU = int(ZuBox<int>{value});
    cpu_set_t allowed;
    if (c.xdpTxCPU < 0 || c.xdpTxCPU >= CPU_SETSIZE ||
        sched_getaffinity(0, sizeof(allowed), &allowed) ||
        !CPU_ISSET(c.xdpTxCPU, &allowed)) return 2;
  } else if (c.afxdp()) {
    cpu_set_t allowed;
    if (c.rxCPU >= CPU_SETSIZE || sched_getaffinity(0, sizeof(allowed), &allowed) ||
	!CPU_ISSET(c.rxCPU, &allowed)) return 2;
    c.xdpTxCPU = xdpSenderCPU(allowed, c.rxCPU, c.txCPU);
    if (c.xdpTxCPU < 0) {
      std::cerr << "AF_XDP sender needs a second allowed CPU or an explicit CPU override\n";
      return 2;
    }
  }
  if (argc == 14) c.samples = argv[13];
  if (auto value = getenv("BENCH_WORKLOAD")) {
    if (!strcmp(value, "order")) c.workload = 0;
    else if (!strcmp(value, "marketdata")) c.workload = 1;
    else if (!strcmp(value, "updates")) c.workload = 2;
    else return 2;
  }
  if (auto value = getenv("BENCH_BURST_SIZE"))
    c.burstSize = unsigned(ZuBox<unsigned>{value});
  if (auto value = getenv("BENCH_BURST_US"))
    c.burstUS = unsigned(ZuBox<unsigned>{value});
  if (auto value = getenv("BENCH_OFFER_RATE"))
    c.offers.rate = unsigned(ZuBox<unsigned>{value});
  if (auto value = getenv("BENCH_PENDING_CAP"))
    c.offers.cap = unsigned(ZuBox<unsigned>{value});
  c.offers.count = c.messages;
  if (auto value = getenv("BENCH_ACK_HOLD_US"))
    c.ackHoldUS = unsigned(ZuBox<unsigned>{value});
  if (auto value = getenv("BENCH_ALLOC_TUNE")) {
    if (strcmp(value, "0") && strcmp(value, "1")) return 2;
    c.allocationTuning = *value == '1';
  }
  if (auto value = getenv("BENCH_CONGESTION_CONTROL")) {
    if (strcmp(value, "0") && strcmp(value, "1")) return 2;
    c.congestionControl = *value == '1';
  }
  if (auto value = getenv("BENCH_TX_BATCH")) {
    if (strcmp(value, "0") && strcmp(value, "1")) return 2;
    c.txBatch = *value == '1';
  }
  if (auto value = getenv("BENCH_APP_RX_CPU"))
    c.appRxCPU = unsigned(ZuBox<unsigned>{value});
  if (auto value = getenv("BENCH_APP_TX_CPU"))
    c.appTxCPU = unsigned(ZuBox<unsigned>{value});
  if (auto value = getenv("BENCH_APP_COLOCATE")) {
    if (strcmp(value, "0") && strcmp(value, "1")) return 2;
    if (*value != '1') return 2;
    c.appColocate = true;
  }
  if (c.appColocate) { c.appRxCPU = c.rxCPU; c.appTxCPU = c.txCPU; }
  if (!c.messages || !c.connections || c.connections > MaxCxns ||
      !c.pipeline || c.pipeline > MaxPipeline || c.reserve < MinReserve ||
      c.reserve > MaxReserve || uint64_t(c.messages) * c.connections > MaxSamples ||
      c.warmup > MaxSamples || c.burstSize > MaxPipeline || c.ackHoldUS >= 500000 ||
      (c.burstSize && (!c.burstUS || c.burstUS > 1000000))) return 2;
  if (c.offers.rate) {
    if (c.burstSize || c.offers.rate > 1000000000 || !c.offers.cap ||
	c.offers.cap > MaxSamples || c.offers.offset(c.messages - 1) >= 110000000000)
      return 2;
  } else if (c.offers.cap) return 2;
  tuneAllocations(c);
  ZiLog::init("zfixbench"); ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2"))); ZiLog::start();
  bool ok = false;
#ifdef HAVE_DPDK
  if (pair) {
    auto clientNetns = getenv("BENCH_RING_CLIENT_NETNS");
    auto serverNetns = getenv("BENCH_RING_SERVER_NETNS");
    auto clientOutput = getenv("BENCH_RING_CLIENT_OUTPUT");
    auto serverOutput = getenv("BENCH_RING_SERVER_OUTPUT");
    auto serverRx = getenv("BENCH_RING_SERVER_RX_CPU");
    auto serverTx = getenv("BENCH_RING_SERVER_TX_CPU");
    Config server = c;
    server.server = true;
    server.interfaceName = "zbenchs";
    server.samples = nullptr;
    if (c.telemetryDir) {
      std::cerr << "DPDK ring pairs record combined process CPU usage\n";
    } else if (clientNetns && serverNetns && clientOutput && serverOutput && serverRx && serverTx) {
      server.appRxCPU = server.rxCPU = unsigned(ZuBox<unsigned>{serverRx});
      server.appTxCPU = server.txCPU = unsigned(ZuBox<unsigned>{serverTx});
      ok = runRingPair(c, server, clientNetns, serverNetns, clientOutput, serverOutput);
    }
  } else
#endif
  {
    FixBench::State state{c};
    auto env = ZmRef{new Zpkt::Env};
    ZiMultiplex mx{endpointParams(c), c.native ?
      ZmRef<Zutcp::MxPlugin>{new Zutcp::MxPlugin} : ZmRef<Zutcp::MxPlugin>{}};
    PacketEndpoint packet;
    ok = initEndpoint(c, mx, env.ptr(), packet) && run(c, state, mx, packet);
  }
  ZiLog::stop();
  rusage usage{};
  if (!getrusage(RUSAGE_SELF, &usage)) {
    auto seconds = [](const timeval &t) { return double(t.tv_sec) + double(t.tv_usec)/1e6; };
    std::cout << "process_resources through_report=1 combined=" << unsigned(pair)
      << " user_seconds=" << ZuBoxed(seconds(usage.ru_utime)).fp<6>()
      << " system_seconds=" << ZuBoxed(seconds(usage.ru_stime)).fp<6>() << '\n';
  }
  return ok ? 0 : 1;
}
