//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZtPlatform.hh>
#include <zlib/ZiCSV.hh>
#include <zlib/ZiFileTxStream.hh>
#include <zlib/ZiHashTune.hh>

#include "zhashtune.hh"

using namespace ZuTestUtil;
using namespace HashTest;

namespace HashTest {

using Chained = ZmHash<unsigned, ZmHashLock<ZmNoLock>>;
using Linear = ZmLHash<unsigned, ZmLHashLock<ZmNoLock>>;

struct Options {
  const char *scenario;
  unsigned count;
  unsigned repeat;
  const char *report;
  const char *headroom;
  const char *late;
};

static void capture(Reports &reports, unsigned phase) {
  Ztc::HashMgr::capture([](Ztc::Hash *hash) {
    return workload(hash->telKey().p<0>());
  }, [&reports, phase](ZuSpan<const Ztc::HashTelemetry> rows) {
    for (const auto &data : rows) {
      Report report;
      static_cast<Ztc::HashTelemetry &>(report) = data;
      report.phase = phase;
      reports.push(ZuMv(report));
    }
  });
}

template <typename H>
static void burst(H &hash, unsigned n, bool clean) {
  for (unsigned i = 0; i < n; ++i) {
    ZuCheckRT(bool(hash.add(i)));
    ZuCheckRT(bool(hash.find(i)));
  }
  for (unsigned i = 0; i < n; ++i) ZuCheckRT(bool(hash.find(i)));
  if (clean) hash.clean();
  else for (unsigned i = 0; i < n; ++i) hash.del(i);
  for (unsigned i = 0; i < n; ++i) ZuCheckRT(!hash.find(i));
  ZuCheckRT(!hash.count_());
}

static void save(const Options &options, const Reports &reports) {
  auto out = ZiCSV::writeFile<Report>(Zi::Path{options.report}, ZiCSV::Replace);
  for (const auto &r : reports) ZuCheckRT(out(r));
  ZuCheckRT(!out.error);
  Zi::Path path;
  path << options.report << ".hash.csv";
  ZiFile file{path, ZiFile::Write | ZiFile::GC};
  if (!file) throw file.error();
  {
    ZiFileTxStream<> stream{file};
    stream << Ztc::hashCSV();
    if (!stream.flush()) {
      if (file.error()) throw file.error();
      throw ZeEXCEPT(Error, "zhashwork", "telemetry output failed");
    }
  }
  if (options.headroom) ZiHashTune::save(strtod(options.headroom, nullptr));
  else ZiHashTune::save();
}

template <typename H>
static void single(const Options &options) {
  H hash{!strcmp(options.scenario, "quoted") ? "HS.\"table" : "HS.table"};
  Reports reports;
  capture(reports, 0);
  for (unsigned r = 0; r < options.repeat; ++r)
    burst(hash, options.count, !strcmp(options.scenario, "clean"));
  capture(reports, 1);
  save(options, reports);
}

static void multiple(const Options &options) {
  Chained first{"HS.table"};
  Chained second{!strcmp(options.scenario, "ids") ? "HS.other" : "HS.table"};
  Reports reports;
  capture(reports, 0);
  for (unsigned r = 0; r < options.repeat; ++r) {
    unsigned firstN = options.count, secondN = options.count * 3;
    if (!strcmp(options.scenario, "reverse")) {
      firstN *= 3;
      secondN = options.count;
    }
    if (!strcmp(options.scenario, "overlap")) {
      for (unsigned i = 0; i < firstN; ++i) ZuCheckRT(bool(first.add(i)));
      for (unsigned i = 0; i < secondN; ++i) ZuCheckRT(bool(second.add(i)));
      for (unsigned i = 0; i < firstN; ++i) {
	ZuCheckRT(bool(first.find(i)));
	first.del(i);
      }
      for (unsigned i = 0; i < secondN; ++i) {
	ZuCheckRT(bool(second.find(i)));
	second.del(i);
      }
    } else {
      burst(first, firstN, false);
      burst(second, secondN, false);
    }
  }
  capture(reports, 1);
  save(options, reports);
}

static void mixed(const Options &options) {
  Chained first{"HS.table"};
  Linear second{"HS.table"};
  Reports reports;
  capture(reports, 0);
  for (unsigned r = 0; r < options.repeat; ++r) {
    burst(first, options.count, false);
    burst(second, options.count, false);
  }
  capture(reports, 1);
  save(options, reports);
}

static void excluded(const Options &options) {
  Chained hash{"HS.table"}, unused{"HS.unused"};
  using Local = ZmLHash<unsigned, ZmLHashLocal<>>;
  using Static = ZmLHash<unsigned, ZmLHashStatic<4>>;
  Local local;
  Static fixed;
  burst(local, 3, false);
  burst(fixed, 3, false);
  { Chained gone{"HS.destroyed"}; burst(gone, 3, false); }
  Reports reports;
  capture(reports, 0);
  burst(hash, options.count, false);
  capture(reports, 1);
  save(options, reports);
}

static void late(const Options &options) {
  Chained first{"HS.table"};
  Reports reports;
  capture(reports, 2);
  ZiHashTune::init(options.late);
  Chained second{"HS.table"};
  capture(reports, 0);
  burst(first, options.count, false);
  burst(second, options.count, false);
  capture(reports, 1);
  save(options, reports);
}

static void concurrent(const Options &options) {
  ZmHash<unsigned, ZmHashLock<ZmPLock>> hash{"HS.table"};
  Reports reports;
  capture(reports, 0);
  ZmSemaphore ready, go;
  auto work = [&hash, &ready, &go, &options](unsigned base) {
    ready.post();
    go.wait();
    for (unsigned i = 0; i < options.count; ++i) hash.add(base + i);
  };
  ZmThread first{[&work] { work(0); }};
  ZmThread second{[&work, &options] { work(options.count); }};
  bool started = bool(first) && bool(second);
  if (first) ready.wait();
  if (second) ready.wait();
  go.post(); go.post();
  if (first) first.join();
  if (second) second.join();
  ZuCheckRT(started);
  ZuCheckRT(hash.count_() == options.count * 2);
  for (unsigned i = 0; i < options.count * 2; ++i)
    ZuCheckRT(bool(hash.find(i)));
  hash.clean();
  // Exercise parallel count updates above, then establish an exact peak
  // for replay sizing after both workers have joined.
  burst(hash, options.count * 2, true);
  capture(reports, 1);
  save(options, reports);
}

static void shadow(const Options &options) {
  ZmHash<unsigned, ZmHashShadow<ZmHashLock<ZmNoLock>>, Chained::Node>
    view{"HS.table"};
  ZtArray<Chained::Node *, ZtArrayHeapID<"HashTest.Nodes">> nodes;
  Reports reports;
  capture(reports, 0);
  for (unsigned i = 0; i < options.count; ++i) {
    auto node = new Chained::Node{i};
    nodes.push(node);
    view.addNode(node);
    ZuCheckRT(bool(view.find(i)));
  }
  view.clean();
  // The shadow must leave caller-owned nodes alive after cleaning.
  for (unsigned i = 0; i < nodes.length(); ++i) {
    ZuCheckRT(nodes[i]->key() == i);
    delete nodes[i];
  }
  capture(reports, 1);
  save(options, reports);
}

static void utility(const Options &options) {
  Zi::Path heapPath;
  heapPath << options.report << ".heap-tuning";
  Zi::Path originalHeap = Zt::getpath("Z_HEAPTUNE");
  Zt::setenv("Z_HEAPTUNE", heapPath.data());
  constexpr auto marker = "owned heap configuration sentinel\n"_z;
  {
    ZiFile heap{heapPath, ZiFile::Write | ZiFile::GC};
    ZuCheckRT(heap && heap.write(marker.data(), marker.length()) == Zi::OK);
  }
  Zi::Path original = Zt::getpath("Z_HASHTUNE");
  Zt::unsetenv("Z_HASHTUNE");
  ZiHashTune::load();
  Zt::setenv("Z_HASHTUNE", original.data());
  Zi::Path bad;
  bad << options.report << ".missing/tune.csv";
  Zt::setenv("Z_HASHTUNE", bad.data());
  bool failed = false;
  try { ZiHashTune::save(); }
  catch (const ZeError &) { failed = true; }
  ZuCheckRT(failed);
#ifndef _WIN32
  Zt::setenv("Z_HASHTUNE", "/dev/full");
  failed = false;
  try { ZiHashTune::save(); }
  catch (const ZeError &) { failed = true; }
  ZuCheckRT(failed);
#endif
  Zt::setenv("Z_HASHTUNE", original.data());
  single<Chained>(options);
  {
    ZiFile heap{heapPath, ZiFile::ReadOnly | ZiFile::GC};
    ZtString<> contents;
    contents.length(marker.length());
    ZuCheckRT(heap && heap.size() == marker.length() &&
      heap.read(contents.data(), contents.length()) == contents.length() &&
      contents == marker);
  }
  Zt::setenv("Z_HEAPTUNE", originalHeap.data());
}

// Synthetic telemetry exercises the supported limit without a huge table.
class Sample : public ZmAnyHash {
public:
  Sample(bool linear) {
    m_data.id = "HS.table";
    ZmHashParams params{m_data.id};
    m_data.bits = params.bits();
    m_data.loadFactor = params.loadFactor();
    m_data.linear = linear;
    ZmHashMgr::add(this);
  }
  ~Sample() { ZmHashMgr::del(this); }
  void peak(uint64_t count) { m_data.maxCount = count; }
  TelKey telKey() const override {
    return {m_data.id, reinterpret_cast<uintptr_t>(this)};
  }
  void telemetry(Ztc::HashTelemetry &data) const override {
    data = m_data;
    data.addr = reinterpret_cast<uintptr_t>(this);
  }
private:
  Ztc::HashTelemetry m_data;
};

static void arithmetic(const Options &options) {
  Sample sample{!strcmp(options.scenario, "arithmetic-linear")};
  Reports reports;
  capture(reports, 0);
  sample.peak(options.count);
  capture(reports, 1);
  save(options, reports);
}

static void execute(const Options &options) {
  ZuTestScopeRT(execute);
  try {
    if (!strcmp(options.scenario, "linear")) single<Linear>(options);
    else if (!strcmp(options.scenario, "striped"))
      single<ZmHash<unsigned, ZmHashLock<ZmPLock>>>(options);
    else if (!strcmp(options.scenario, "multi") || !strcmp(options.scenario, "ids") ||
	!strcmp(options.scenario, "overlap") || !strcmp(options.scenario, "reverse"))
      multiple(options);
    else if (!strcmp(options.scenario, "mixed")) mixed(options);
    else if (!strcmp(options.scenario, "excluded")) excluded(options);
    else if (!strcmp(options.scenario, "late")) late(options);
    else if (!strcmp(options.scenario, "concurrent")) concurrent(options);
    else if (!strcmp(options.scenario, "shadow")) shadow(options);
    else if (!strcmp(options.scenario, "utility")) utility(options);
    else if (!strcmp(options.scenario, "arithmetic") ||
	!strcmp(options.scenario, "arithmetic-linear")) arithmetic(options);
    else single<Chained>(options);
  } catch (const ZeError &error) {
    log("file error: ", error);
    ZuCheckRT(false);
  }
}

} // HashTest

int main(int argc, char **argv) {
  ZiHashTune::load();
  ZuTestUtil::parse(1, argv);
  if (argc < 5 || argc > 7) {
    log("Usage: zhashwork scenario count repeat report [headroom|-] [late-config]");
    return 2;
  }
  Options options{argv[1], unsigned(strtoul(argv[2], nullptr, 10)),
    unsigned(strtoul(argv[3], nullptr, 10)), argv[4],
    argc > 5 && strcmp(argv[5], "-") ? argv[5] : nullptr,
    argc > 6 ? argv[6] : nullptr};
  ZuTestMain();
  ZuTestCall(execute, options);
  return 0;
}
