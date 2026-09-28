//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#else
#include <poll.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmTopology.hh>
#include <zlib/ZtPlatform.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZiCSV.hh>

#include "ZiTestResidue.hh"
#include "zheaptune.hh"

using namespace ZuTestUtil;
using namespace HeapTest;

namespace HeapTest {

struct Config {
  ZuID id;
  uint16_t partition = 0;
  uint8_t vshift = 0;
  uint64_t cacheSize = 0;
  ZmBitmap cpuset;
};

ZfStruct(, Config,
  (((id)), (String)),
  (((partition)), (UInt16)),
  (((vshift)), (UInt8)),
  (((cacheSize)), (UInt64)),
  (((cpuset)), (UDT)));

using Configs = ZtArray<Config, ZtArrayHeapID<"HeapTest.Configs">>;

bool key(const Config &a, const Config &b) {
  return a.id == b.id && a.partition == b.partition && a.vshift == b.vshift;
}
bool key(const Config &a, const Report &b) {
  return a.id == b.id && a.partition == b.partition && a.vshift == b.vshift;
}

bool sameCpuset(const ZmBitmap &a, const ZmBitmap &b) {
  return (!a && !b) || a == b;
}

bool write(const Zi::Path &path, ZuCSpan data) {
  ZiFile f{path, ZiFile::Write | ZiFile::GC};
  return f && f.write(data.data(), data.length()) == Zi::OK;
}

bool read(const Zi::Path &path, ZtString<> &data) {
  ZiFile f{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!f) return false;
  auto n = f.size();
  if (n < 0 || uint64_t(n) > INT_MAX) return false;
  data.length(unsigned(n));
  return !n || f.read(data.data(), unsigned(n)) == n;
}

bool check(bool ok, const ZtString<> &name) {
  return ZuTestMgr::check(nullptr, ok, name.data());
}

template <typename T, typename Rows>
bool parse(const Zi::Path &path, Rows &rows, bool tuning = false) {
  ZtString<> data;
  if (!read(path, data) || !data || data[data.length() - 1] != '\n')
    return false;
  if (tuning && data.cspan().prefix(header) != header.length())
    return false;
  ZfCSV::Reader<T, ZuFacet::Core> reader;
  bool ok = true;
  auto emit = [&rows, &ok, tuning](const auto &scan) {
    if (tuning) {
      if (scan.row.length() != 5) { ok = false; return; }
      for (unsigned i = 1; i <= 3; ++i) {
	if (!scan.row[i].template is<ZuSpan<char>>()) { ok = false; continue; }
	auto s = scan.row[i].template p<ZuSpan<char>>();
	ZuBox<uint64_t> value;
	if (!s || value.scan(s) != s.length()) ok = false;
	if ((i == 1 && value > UINT16_MAX) || (i == 2 && value > UINT8_MAX))
	  ok = false;
      }
      if (!scan.row[4].template is<ZuSpan<char>>()) ok = false;
      else {
	auto s = scan.row[4].template p<ZuSpan<char>>();
	ZfCSV::unquote(s);
	ZmBitmap bitmap;
	if (bitmap.scan(s) != s.length()) ok = false;
      }
    }
    rows.push(scan.ctor());
  };
  unsigned consumed = reader.process(data.span(), emit);
  return ok && consumed == data.length();
}

Zi::Path executable(const char *argv0) {
  Zi::Path path{argv0};
  if (!ZiFile::absolute(path)) path = ZiFile::append(ZiFile::cwd(), path);
  path = ZiFile::dirname(path);
  if (ZiFile::leafname(path) == ".libs") path = ZiFile::dirname(path);
#ifdef _WIN32
  return ZiFile::append(path, "zheapwork.exe");
#else
  return ZiFile::append(path, "zheapwork");
#endif
}

struct Case {
  const char *name;
  const char *scenario = "fixed";
  unsigned count = 100;
  unsigned repeat = 3;
  const char *headroom = nullptr;
  const char *seed = "";
  unsigned generations = 3;
  bool late = false;
};

int run(const Zi::Path &exe, const Case &c, const Zi::Path &config,
    const Zi::Path &report, const Zi::Path &log, const Zi::Path &late) {
  ZiFile output{log, ZiFile::Write | ZiFile::GC};
  if (!output) return 255;
  ZtString<> count, repeat;
  count << ZuBoxed(c.count);
  repeat << ZuBoxed(c.repeat);
  unsigned timeout = 30; // bounds a child, not a workload performance target
  if (auto s = getenv("Z_HEAPTUNE_TIMEOUT")) timeout = strtoul(s, nullptr, 10);
  if (!timeout || timeout > 3600) return 255;
#ifndef _WIN32
  pid_t pid = fork();
  if (pid < 0) return 255;
  if (!pid) {
    setpgid(0, 0);
    dup2(output.handle(), STDOUT_FILENO);
    dup2(output.handle(), STDERR_FILENO);
    setenv("Z_HEAPTUNE", config.data(), 1);
    unsetenv("HARNESS_ACTIVE");
    const char *args[] = {exe.data(), c.scenario, count.data(), repeat.data(),
      report.data(), c.headroom ? c.headroom : "-", c.late ? late.data() : nullptr,
      nullptr};
    // A command prefix makes instrumentation explicit for the child too.
    if (auto prefix = getenv("Z_HEAPTUNE_CHILD")) {
      ZtString<> command{prefix};
      for (unsigned i = 0; args[i]; ++i) {
	command << ' ';
	command << '\'';
	for (const char *p = args[i]; *p; ++p) {
	  if (*p == '\'') command << "'\\''";
	  else command << *p;
	}
	command << '\'';
      }
      execl("/bin/sh", "sh", "-c", command.data(), nullptr);
    } else execv(exe.data(), const_cast<char *const *>(args));
    _exit(127);
  }
  setpgid(pid, pid);
  int fd = int(syscall(SYS_pidfd_open, pid, 0));
  bool finished = false;
  if (fd >= 0) {
    pollfd event{fd, POLLIN, 0};
    int r;
    do r = poll(&event, 1, timeout * 1000);
    while (r < 0 && errno == EINTR);
    finished = r > 0 && (event.revents & POLLIN);
    close(fd);
  }
  if (!finished) kill(-pid, SIGKILL);
  int status;
  pid_t waited;
  do waited = waitpid(pid, &status, 0); while (waited < 0 && errno == EINTR);
  if (!finished || waited != pid) return 254;
  return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
#else
  ZtString<> command;
  ZtString<> exeArg{exe}, reportArg{report}, lateArg{late};
  if (auto prefix = getenv("Z_HEAPTUNE_CHILD")) command << prefix << ' ';
  for (auto s : {exeArg.data(), c.scenario, count.data(), repeat.data(), reportArg.data(),
      c.headroom ? c.headroom : "-"}) {
    if (command) command << ' ';
    ZfCLI::CmdQuote::quote(command, s);
  }
  if (c.late) { command << ' '; ZfCLI::CmdQuote::quote(command, lateArg.data()); }
  // Construct a child-only environment, leaving the parent's tuning path alone.
  auto env = GetEnvironmentStringsW();
  ZtWString<> environment;
  for (auto p = env; p && *p; p += wcslen(p) + 1) {
    if (!_wcsnicmp(p, L"Z_HEAPTUNE=", 11) || !_wcsnicmp(p, L"HARNESS_ACTIVE=", 15))
      continue;
    environment << p << '\0';
  }
  FreeEnvironmentStringsW(env);
  environment << "Z_HEAPTUNE=" << config << '\0' << '\0';
  SetHandleInformation(output.handle(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  STARTUPINFOW start{};
  start.cb = sizeof(start);
  start.dwFlags = STARTF_USESTDHANDLES;
  start.hStdOutput = output.handle();
  start.hStdError = output.handle();
  start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION child{};
  ZtWString<> wideCommand{command};
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation,
      &limits, sizeof(limits))) {
    if (job) CloseHandle(job);
    return 255;
  }
  if (!CreateProcessW(nullptr, wideCommand.data(), nullptr, nullptr, TRUE,
      CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
      environment.data(), nullptr, &start, &child)) {
    CloseHandle(job);
    return 255;
  }
  if (!AssignProcessToJobObject(job, child.hProcess)) {
    TerminateProcess(child.hProcess, 255);
    WaitForSingleObject(child.hProcess, INFINITE);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    CloseHandle(job);
    return 255;
  }
  ResumeThread(child.hThread);
  auto waited = WaitForSingleObject(child.hProcess, timeout * 1000);
  DWORD status = 254;
  if (waited == WAIT_OBJECT_0) GetExitCodeProcess(child.hProcess, &status);
  else {
    TerminateJobObject(job, status);
    WaitForSingleObject(child.hProcess, INFINITE);
  }
  CloseHandle(child.hThread);
  CloseHandle(child.hProcess);
  CloseHandle(job);
  return status;
#endif
}

bool verify(const Case &c, unsigned gen, const Configs &input,
    const Configs &output, const Reports &reports, const Reports &telemetry,
    const Zi::Path &dir) {
  bool ok = true;
  auto test = [&c, gen, &dir, &ok](bool value, const auto &detail) {
    ZtString<> label;
    label << c.name << " generation " << ZuBoxed(gen) << ' ' << detail;
    if (!check(value, label)) {
      log("artifacts: ", dir);
      ok = false;
    }
  };
  auto is = [&c](const char *s) { return !strcmp(c.scenario, s); };
  unsigned finalCount = 0, configCount = 0, activeCount = 0;
  for (unsigned i = 0; i < output.length(); ++i) {
    if (workload(output[i].id)) ++configCount;
    for (unsigned j = 0; j < i; ++j)
      test(!key(output[i], output[j]), "unique configuration keys");
  }
  for (const auto &r : reports) {
    if (r.phase != 1) continue;
    ++finalCount;
    ZtString<> id;
    id << r.id << '/' << ZuBoxed(r.partition) << '/' << ZuBoxed(r.vshift)
      << '/' << ZuBoxed(r.size) << '/' << ZuBoxed(r.alignment)
      << '/' << ZuBoxed(r.sharded);
    auto assert_ = [&id, &test](bool value, const char *detail) {
      ZtString<> label;
      label << id << ' ' << detail;
      test(value, label);
    };
    const Report *initial = nullptr;
    for (const auto &s : reports)
      if (!s.phase && sameArena(r, s)) initial = &s;
    const Config *loaded = nullptr, *tuned = nullptr;
    for (const auto &s : input) if (key(s, r)) loaded = &s;
    for (const auto &s : output) if (key(s, r)) tuned = &s;
    if (!is("boundaries")) {
      assert_(initial && !initial->cacheAllocs && !initial->heapAllocs &&
	!initial->cacheFrees && !initial->heapFrees, "initial counters zero");
      assert_(initial && initial->cacheSize == (loaded ? loaded->cacheSize : 0),
	"configuration applied before allocation");
      assert_(initial && sameCpuset(initial->cpuset,
	loaded ? loaded->cpuset : ZmBitmap{}), "initial cpuset applied");
    }
    assert_(r.cacheSize == (loaded ? loaded->cacheSize : 0), "loaded capacity");
    assert_(sameCpuset(r.cpuset, loaded ? loaded->cpuset : ZmBitmap{}), "loaded cpuset");
    assert_(!r.allocated() && !r.crossFrees && r.cacheAllocs == r.cacheFrees &&
      r.heapAllocs == r.heapFrees && r.globalHeapAllocs == r.globalHeapFrees,
      "drained allocation balance");
    assert_(tuned, "configuration row emitted");
    if (!tuned) continue;
    assert_(sameCpuset(tuned->cpuset, r.cpuset), "cpuset exported");
    assert_(tuned->cacheSize >= r.cacheSize, "capacity never shrinks");
    unsigned arenas = 0;
    uint64_t allocs = 0;
    for (const auto &s : reports) {
      if (s.phase != 1 || s.id != r.id || s.partition != r.partition ||
	  s.vshift != r.vshift) continue;
      ++arenas;
      allocs += s.heapAllocs;
      assert_(s.cacheSize == r.cacheSize && sameCpuset(s.cpuset, r.cpuset),
	"consistent configuration per key");
    }
    double h = c.headroom ? strtod(c.headroom, nullptr) : 0.05;
    // Independent model: configured capacity plus this group's per-arena share.
    uint64_t expected = r.cacheSize;
    if (r.globalHeapAllocs)
      expected = uint64_t(r.cacheSize + r.globalHeapMax * (1 + h) *
	double(allocs) / (double(arenas) * r.globalHeapAllocs));
    ZtString<> arithmetic;
    arithmetic << id << " tuned expected=" << ZuBoxed(expected)
      << " actual=" << ZuBoxed(tuned->cacheSize);
    test(tuned->cacheSize == expected, arithmetic);
    if (!is("boundaries")) {
      unsigned n = c.count;
      if ((is("partitions") || is("sequential")) && r.partition) n *= 3;
      if (is("shared")) n *= 4;
      if ((is("unusedpair") && r.size == 128) || r.id == "HT.unused") n = 0;
      uint64_t cached = (r.cacheSize < n ? r.cacheSize : n) * c.repeat;
      assert_(r.cacheAllocs == cached &&
	r.heapAllocs == uint64_t(n) * c.repeat - cached, "exact allocation counts");
      uint64_t peak = 0, global = 0;
      for (const auto &s : reports) {
	if (s.phase != 1 || s.id != r.id) continue;
	unsigned demand = c.count;
	if ((is("partitions") || is("sequential")) && s.partition) demand *= 3;
	if (is("shared")) demand *= 4;
	if ((is("unusedpair") && s.size == 128) || s.id == "HT.unused") demand = 0;
	uint64_t fallback = demand > s.cacheSize ? demand - s.cacheSize : 0;
	global += fallback * c.repeat;
	if (is("sequential")) { if (fallback > peak) peak = fallback; }
	else peak += fallback;
      }
      assert_(r.globalHeapAllocs == global && r.globalHeapMax == peak,
	"exact per-ID fallback volume and peak");
      if (r.id == "HT.vector")
	assert_(r.size == (64U << r.vshift) && r.vshift < 2, "distinct vector classes");
    }
    if (r.cacheAllocs || r.heapAllocs) {
      ++activeCount;
      const Report *csv = nullptr;
      for (const auto &s : telemetry) if (sameArena(r, s)) csv = &s;
      assert_(csv && csv->vshift == r.vshift && csv->cacheSize == r.cacheSize &&
	csv->cacheAllocs == r.cacheAllocs && csv->heapAllocs == r.heapAllocs &&
	csv->globalHeapMax == r.globalHeapMax, "heapCSV matches capture");
    } else {
      bool absent = true;
      for (const auto &s : telemetry) if (sameArena(r, s)) absent = false;
      assert_(absent, "heapCSV omits unused arena");
    }
  }
  unsigned expectedRows = is("multi") || is("unusedpair") || is("shared") ? 1 :
    is("keys") || is("late") ? 4 :
    is("variable") || is("partitions") || is("sequential") || is("unused") ? 2 : 1;
  if (!is("boundaries")) {
    test(configCount == expectedRows, "exact workload configuration row count");
    test(finalCount == expectedRows + (is("multi") || is("unusedpair") ? 1 : 0),
      "exact workload arena count");
  } else {
    test(finalCount > 0 && configCount == finalCount, "boundary classes exported");
    for (const auto &r : reports) {
      if (r.phase != 1) continue;
      test(r.size >= sizeof(uintptr_t) && !(r.size & (r.size - 1)),
	"boundary class physical size");
      for (const auto &s : reports)
	if (s.phase == 1 && r.id == s.id && r.vshift != s.vshift)
	  test(r.size != s.size, "different shifts have different sizes");
    }
  }
  test(telemetry.length() == activeCount, "telemetry contains exactly active workload arenas");
  for (const auto &s : input) {
    if (!workload(s.id)) continue;
    bool instantiated = false, emitted = false;
    for (const auto &r : reports) if (r.phase == 1 && key(s, r)) instantiated = true;
    for (const auto &r : output) if (key(s, r)) emitted = true;
    test(instantiated == emitted, "only instantiated configuration keys exported");
  }
  if (c.late) {
    unsigned before = 0;
    for (const auto &r : reports) if (r.phase == 2) {
      ++before;
      test(!r.cacheSize && !r.heapAllocs && !r.cacheAllocs,
	"late configuration starts with zero arena");
    }
    test(before == 4, "late configuration covers both partitions and classes");
  }
  return ok;
}

void runCase_(const Zi::Path &exe, const Case &c, const Zi::Path &root) {
  Zi::Path dir = ZiFile::append(root, c.name);
  if (!check(ZiFile::mkdir(dir) == Zi::OK, ZtString<>{"create case directory"})) return;
  Zi::Path config = ZiFile::append(dir, "tuning.csv");
  Zi::Path late = ZiFile::append(dir, "late.csv");
  ZtString<> name;
  name << c.name << " seed configuration";
  if (!check(write(config, c.late ? "" : c.seed), name)) return;
  if (c.late && !check(write(late, c.seed), name)) return;
  for (unsigned g = 0; g < c.generations; ++g) {
    Zi::Name stem;
    stem << "generation-" << ZuBoxed(g);
    Zi::Path base = ZiFile::append(dir, stem);
    Zi::Path before, after, report, log;
    before << base << ".input.csv";
    after << base << ".output.csv";
    report << base << ".report.csv";
    log << base << ".log";
    Configs input, output;
    Reports reports, telemetry;
    ZtString<> seed;
    name.null(); name << c.name << " generation " << ZuBoxed(g) << " read input";
    if (!check(read(c.late ? late : config, seed), name)) return;
    if (seed.length() && !parse<Config>(c.late ? late : config, input, true)) {
      check(false, ZtString<>{"invalid input configuration"}); return;
    }
    if (ZiFile::copy(config, before) != Zi::OK) {
      check(false, ZtString<>{"save input generation"}); return;
    }
    name.null(); name << c.name << " generation " << ZuBoxed(g) << " child exit";
    int status = run(exe, c, config, report, log, late);
    if (!check(!status, name)) {
      ZuTestUtil::log("status=", status, " log=", log); return;
    }
    if (ZiFile::copy(config, after) != Zi::OK) {
      check(false, ZtString<>{"save output generation"}); return;
    }
    if (!strcmp(c.name, "replacement") && !g) {
      ZtString<> replacement;
      name.null(); name << c.name << " replaces longer input";
      check(read(config, replacement) && replacement.length() < seed.length(), name);
    }
    name.null(); name << c.name << " complete five-column tuning CSV";
    if (!check(parse<Config>(config, output, true), name)) return;
    name.null(); name << c.name << " complete snapshot CSV";
    if (!check(parse<Report>(report, reports), name)) return;
    Zi::Path tel;
    tel << report << ".heap.csv";
    Reports allTelemetry;
    name.null(); name << c.name << " complete heap telemetry CSV";
    if (!check(parse<Report>(tel, allTelemetry), name)) return;
    for (auto &r : allTelemetry) if (workload(r.id)) telemetry.push(ZuMv(r));
    if (!verify(c, g, input, output, reports, telemetry, dir)) return;
  }
}

void cases(const Zi::Path &exe) {
  ZuTestScopeRT(cases);
  auto root = ZiTestResidue::dir("run");
  unsigned selected = 0;
  auto runCase = [&root, &selected](const Zi::Path &exe, const Case &c) {
    if (auto filter = getenv("Z_HEAPTUNE_CASE"))
      if (strcmp(filter, c.name)) return;
    ++selected;
    runCase_(exe, c, root);
  };
  runCase(exe, {"cold"});
  runCase(exe, {"volume", "fixed", 100, 7});
  runCase(exe, {"header", "fixed", 100, 3, nullptr, header.data()});
  runCase(exe, {"zero", "fixed", 100, 3, nullptr,
    "id,partition,vshift,cacheSize,cpuset\n\"HT.fixed\",0,0,0,\n"});
  runCase(exe, {"headroom-zero", "fixed", 100, 3, "0"});
  runCase(exe, {"headroom-quarter", "fixed", 100, 3, "0.25"});
  runCase(exe, {"truncation", "fixed", 3, 3});
  runCase(exe, {"warm", "fixed", 100, 3, nullptr,
    "id,partition,vshift,cacheSize,cpuset\n\"HT.fixed\",0,0,40,\n"});
  runCase(exe, {"oversize", "fixed", 100, 3, nullptr,
    "id,partition,vshift,cacheSize,cpuset\n\"HT.fixed\",0,0,200,\n"});
  runCase(exe, {"partitions", "partitions"});
  runCase(exe, {"sequential", "sequential"});
  runCase(exe, {"multi", "multi"});
  runCase(exe, {"unusedpair", "unusedpair"});
  runCase(exe, {"shared", "shared"});
  runCase(exe, {"variable-cold", "variable"});
  runCase(exe, {"variable-warm", "variable", 50, 3, nullptr,
    "id,partition,vshift,cacheSize,cpuset\n\"HT.vector\",0,0,128,\n\"HT.vector\",0,1,64,\n"});
  runCase(exe, {"selective", "variable", 100, 3, nullptr,
    "id,partition,vshift,cacheSize,cpuset\n\"HT.vector\",0,0,128,\n\"HT.vector\",0,1,40,\n"});
  runCase(exe, {"shift1-cold", "shift1"});
  runCase(exe, {"shift1-warm", "shift1", 100, 3, nullptr,
    "id,partition,vshift,cacheSize,cpuset\n\"HT.vector\",0,1,128,\n"});
  const char *four = "id,partition,vshift,cacheSize,cpuset\n"
    "\"HT.vector\",0,0,128,\n\"HT.vector\",0,1,64,\n"
    "\"HT.vector\",1,0,80,\n\"HT.vector\",1,1,96,\n";
  runCase(exe, {"keys", "keys", 50, 3, nullptr, four});
  runCase(exe, {"permuted", "keys", 50, 3, nullptr,
    "id,partition,vshift,cacheSize,cpuset\n"
    "\"HT.vector\",1,1,96,\n\"HT.vector\",0,1,64,\n"
    "\"HT.vector\",1,0,80,\n\"HT.vector\",0,0,128,\n"});
  runCase(exe, {"missing", "keys", 50, 3, nullptr,
    "id,partition,vshift,cacheSize,cpuset\n"
    "\"HT.vector\",1,1,96,\n\"HT.vector\",1,0,80,\n\"HT.vector\",0,0,128,\n"});
  runCase(exe, {"late", "late", 50, 3, nullptr, four, 1, true});
  runCase(exe, {"boundaries", "boundaries", 1, 1, nullptr, "", 1});
  runCase(exe, {"unused", "unused"});
  runCase(exe, {"no-allocations", "fixed", 0, 3});
  ZmBitmap cpuset;
  auto available = hwloc_topology_get_allowed_cpuset(ZmTopology::hwloc());
  int bit = hwloc_bitmap_first(available);
  ZuCheckRT(bit >= 0);
  if (bit >= 0) {
    cpuset.set(bit);
    int second = hwloc_bitmap_next(available, bit);
    if (second >= 0) {
      int third = hwloc_bitmap_next(available, second);
      if (third >= 0) cpuset.set(third); // exercise a comma-separated mask
    }
    ZtString<> seed;
    seed << header << "\"HT.vector\",0,0,128,\"" << cpuset << "\"\n"
      << "\"HT.vector\",0,1,128,\"" << cpuset << "\"\n";
    runCase(exe, {"cpuset", "variable", 100, 3, nullptr, seed.data()});
  }
  ZtString<> replacement{header};
  for (unsigned i = 0; i < 100; ++i)
    replacement << "\"HT.old" << ZuBoxed(i) << "\",0,0,128,\n";
  runCase(exe, {"replacement", "fixed", 100, 3, nullptr, replacement.data()});
  ZuCheckRT(selected > 0);
}

} // HeapTest

int main(int argc, char **argv) {
  parse(argc, argv);
  ZiTestResidue::init("zheaptune");
  ZuTestMgr::finalFn(&ZiTestResidue::final);
  ZuTestMain();
  auto exe = executable(argv[0]);
  ZuTestCall(cases, exe);
  return 0;
}
