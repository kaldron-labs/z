//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmVHeap.hh>
#include <zlib/ZtPlatform.hh>
#include <zlib/ZiCSV.hh>
#include <zlib/ZiFileTxStream.hh>
#include <zlib/ZiHeapTune.hh>

#include "zheaptune.hh"

using namespace ZuTestUtil;
using namespace HeapTest;

namespace HeapTest {

using Fixed = ZmHeapCacheT<ZuStringT<"HT.fixed">, 64, 8, false>;
using Small = ZmHeapCacheT<ZuStringT<"HT.multi">, 64, 8, false>;
using Large = ZmHeapCacheT<ZuStringT<"HT.multi">, 128, 64, true>;
using VHeap = ZmVHeap<"HT.vector", 64, 1024, 8>;

struct Options {
  const char *scenario;
  unsigned count;
  unsigned repeat;
  const char *report;
  const char *headroom;
  const char *late;
};

struct Op {
  void *(*alloc)(size_t);
  void (*free)(const void *);
  size_t size;
  unsigned count;
  ZtArray<void *, ZtArrayHeapID<"HeapTest.Ptrs">> ptrs;

  void allocate() {
    ptrs.length(count);
    for (unsigned i = 0; i < count; ++i) {
      ptrs[i] = alloc(size);
      memset(ptrs[i], (i % 251) + 1, size);
    }
  }
  bool release() {
    bool ok = true;
    for (unsigned i = 0; i < count; ++i) {
      auto p = static_cast<const unsigned char *>(ptrs[i]);
      for (size_t j = 0; j < size; ++j)
	ok &= p[j] == (i % 251) + 1;
      free(p);
    }
    return ok;
  }
};

using Ops = ZtArray<Op, ZtArrayHeapID<"HeapTest.Ops">>;

template <typename Cache>
void fixed(Ops &ops, unsigned n) {
  Cache::warmup();
  ops.push(Op{
    [](size_t) { return Cache::alloc(); },
    [](const void *p) { Cache::free(const_cast<void *>(p)); },
    Cache::Size, n, {}});
}

template <unsigned I>
void variable(Ops &ops, unsigned n) {
  VHeap::Cache<I>::warmup();
  ops.push(Op{&VHeap::valloc, &VHeap::vfree,
    VHeap::blockSize(I) - 8, n, {}});
}

template <typename Heap>
bool boundaries(unsigned minimum, unsigned classes) {
  using ID = typename Heap::template Cache<0>::ID;
  struct Totals {
    unsigned arenas = 0;
    uint64_t allocs = 0, frees = 0;
    uint64_t globalAllocs = 0, globalFrees = 0, peak = 0;
    bool operator ==(const Totals &) const = default;
  };
  auto totals = [] {
    Totals result;
    Ztc::HeapMgr::all([&result](Ztc::Heap *heap) {
      Ztc::HeapTelemetry data;
      heap->telemetry(data);
      if (data.id != ID{}()) return;
      ++result.arenas;
      result.allocs += data.cacheAllocs + data.heapAllocs;
      result.frees += data.cacheFrees + data.heapFrees;
      result.globalAllocs = data.globalHeapAllocs;
      result.globalFrees = data.globalHeapFrees;
      result.peak = data.globalHeapMax;
    });
    return result;
  };
  auto before = totals();
  bool ok = Heap::blockSize(0) == minimum && Heap::NCaches == classes;
  ok &= !Heap::valloc(0);
  Heap::vfree(nullptr);
  ok &= before == totals();
  for (unsigned i = 0; i < Heap::NCaches; ++i) {
    // First two classes and the final class cover the effective limits.
    if (i > 1 && i + 1 < Heap::NCaches) continue;
    size_t b = Heap::blockSize(i);
    for (int delta = -1; delta <= 1; ++delta) {
      size_t n = b - 1 + delta; // these boundary heaps have Align == 1
      if (!n) continue;
      unsigned expected = delta > 0 ? i + 1 : i;
      before = totals();
      auto p = static_cast<unsigned char *>(Heap::valloc(n));
      memset(p, 0xa5, n);
      for (size_t j = 0; j < n; ++j) ok &= p[j] == 0xa5;
      ok &= *(p - 1) == expected;
      if (expected < Heap::NCaches) {
	unsigned found = 0;
	Ztc::HeapMgr::all([&found, &ok, expected](Ztc::Heap *heap) {
	  Ztc::HeapTelemetry data;
	  heap->telemetry(data);
	  if (data.id != ID{}() || data.vshift != expected) return;
	  ++found;
	  ok &= data.size == Heap::blockSize(expected);
	});
	ok &= found == 1;
      }
      Heap::vfree(p);
      auto after = totals();
      if (expected >= Heap::NCaches)
	ok &= before == after;
      else
	ok &= after.allocs == before.allocs + 1 && after.frees == before.frees + 1;
    }
  }
  return ok;
}

struct Worker {
  ZmSemaphore go, done;
  int command = 0;
  bool ok = true;
  Options options;
  unsigned index;

  void run() {
    Ops ops;
    auto is = [this](const char *s) { return !strcmp(options.scenario, s); };
    if (is("multi") || is("unusedpair")) {
      fixed<Small>(ops, options.count);
      fixed<Large>(ops, is("unusedpair") ? 0 : options.count);
    } else if (is("variable") || is("keys") || is("late")) {
      variable<0>(ops, options.count);
      variable<1>(ops, options.count);
    } else if (is("shift1")) {
      variable<1>(ops, options.count);
    } else if (is("boundaries")) {
      // Boundary selection itself instantiates only the classes it uses.
    } else {
      fixed<Fixed>(ops, options.count * (index ? 3 : 1));
      if (is("unused")) {
	using Unused = ZmVHeap<"HT.unused", 64, 1024, 8>;
	Unused::Cache<1>::warmup();
      }
    }
    done.post();
    for (;;) {
      go.wait();
      switch (command) {
	case 1:
	  if (is("boundaries")) {
	    ok &= boundaries<ZmVHeap<"HT.default">>(1024, 10);
	    ok &= boundaries<ZmVHeap<"HT.small", 2, 256>>(8, 5);
	    ok &= boundaries<ZmVHeap<"HT.odd", 17, 256>>(32, 3);
	    ok &= boundaries<ZmVHeap<"HT.one", 17, 1>>(32, 1);
	    ok &= boundaries<ZmVHeap<"HT.cap", 8, (1U << 30)>>(8, 17);
	  } else {
	    for (auto &op : ops) op.allocate();
	  }
	  break;
	case 2:
	  for (auto &op : ops) ok &= op.release();
	  break;
	case 3: return;
      }
      done.post();
    }
  }
  void step(int value) {
    command = value;
    go.post();
    if (value != 3) done.wait();
  }
};

void capture(Reports &reports, uint8_t phase) {
  Ztc::HeapMgr::capture({}, [&reports, phase](auto span) {
    for (const auto &data : span) {
      if (!workload(data.id)) continue;
      Report r;
      static_cast<Ztc::HeapTelemetry &>(r) = data;
      r.phase = phase;
      reports.push(ZuMv(r));
    }
  });
}

bool execute(const Options &options) {
  ZuTestScopeRT(execute);
  log("scenario=", options.scenario, " count=", options.count,
    " repeat=", options.repeat, " headroom=",
    options.headroom ? options.headroom : "default");
  auto is = [&options](const char *s) { return !strcmp(options.scenario, s); };
  bool two = is("partitions") || is("sequential") || is("keys") ||
    is("late") || is("shared");
  Worker first{{}, {}, 0, true, options, 0};
  Worker second{{}, {}, 0, true, options, 1};
  ZmThread a{[&first] { first.run(); }, ZmThreadParams{}.partition(0)};
  ZmThread b;
  first.done.wait();
  if (two) {
    b = ZmThread{[&second] { second.run(); },
      ZmThreadParams{}.partition(is("shared") ? 0 : 1)};
    second.done.wait();
  }
  Reports reports;
  if (is("late")) {
    capture(reports, 2);
    ZiHeapTune::init(options.late);
  }
  capture(reports, 0);
  for (unsigned r = 0; r < options.repeat; ++r) {
    first.step(1);
    if (is("sequential")) first.step(2);
    if (two) second.step(1);
    if (!is("sequential")) first.step(2);
    if (two) second.step(2);
  }
  capture(reports, 1);
  // Join before checks: ZuTest is intentionally single-threaded.
  first.step(3);
  if (two) second.step(3);
  a.join();
  if (two) b.join();
  bool ok = first.ok && second.ok;
  ZuCheckRT(ok);
  auto out = ZiCSV::writeFile<Report>(Zi::Path{options.report}, ZiCSV::Replace);
  for (const auto &r : reports) ok &= out(r);
  ZuCheckRT(!out.error);
  Zi::Path tel;
  tel << options.report << ".heap.csv";
  ZiFile telemetry{tel, ZiFile::Write | ZiFile::GC};
  ZuCheckRT(bool(telemetry));
  if (!telemetry) return false;
  {
    ZiFileTxStream<> stream{telemetry};
    stream << Ztc::heapCSV();
    stream.flush();
    bool written = !telemetry.error();
    ZuCheckRT(written);
    ok &= written;
  }
  telemetry.close();
  if (options.headroom)
    ZiHeapTune::save(strtod(options.headroom, nullptr));
  else
    ZiHeapTune::save();
  return ok;
}

void testWork(const Options &options) {
  ZuTestScopeRT(testWork);
  try { ZuTestCallRT(execute, options); }
  catch (const ZeError &error) {
    log("file error: ", error);
    ZuCheckRT(false);
  }
  catch (...) { ZuCheckRT(false); }
}

} // HeapTest

int main(int argc, char **argv) {
  ZiHeapTune::load();
  ZuTestUtil::parse(1, argv);
  if (argc < 5 || argc > 7) {
    log("Usage: zheapwork scenario count repeat report [headroom|-] [late]");
    return 2;
  }
  Options options{argv[1], unsigned(strtoul(argv[2], nullptr, 10)),
    unsigned(strtoul(argv[3], nullptr, 10)), argv[4],
    argc > 5 && strcmp(argv[5], "-") ? argv[5] : nullptr,
    argc > 6 ? argv[6] : nullptr};
  ZuTestMain();
  ZuTestCall(testWork, options);
  return 0;
}
