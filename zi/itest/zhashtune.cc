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
#include <time.h>
#include <unistd.h>
#endif

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZtPlatform.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZiCSV.hh>

#include "ZiTestResidue.hh"
#include "zhashtune.hh"

using namespace ZuTestUtil;
using namespace HashTest;

namespace HashTest {

struct Config {
  ZuID id;
  uint8_t bits = 8;
  double loadFactor = 1;
  uint8_t cBits = 3;
};
ZfStruct(, Config,
  (((id)),		String),
  (((bits)),		UInt8),
  (((loadFactor)),	Float),
  (((cBits)),		UInt8));
using Configs = ZtArray<Config, ZtArrayHeapID<"HashTest.Configs">>;

static bool write(const Zi::Path &path, ZuCSpan data) {
  ZiFile f{path, ZiFile::Write | ZiFile::GC};
  return f && f.write(data.data(), data.length()) == Zi::OK;
}

static bool read(const Zi::Path &path, ZtString<> &data) {
  ZiFile f{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!f) return false;
  auto n = f.size();
  if (n < 0 || uint64_t(n) > INT_MAX) return false;
  data.length(unsigned(n));
  return !n || f.read(data.data(), unsigned(n)) == n;
}

static bool check(bool ok, const ZtString<> &name) {
  return ZuTestMgr::check(nullptr, ok, name.data());
}

static Zi::Path executable(const char *argv0) {
  Zi::Path path{argv0};
  if (!ZiFile::absolute(path)) path = ZiFile::append(ZiFile::cwd(), path);
  path = ZiFile::dirname(path);
  if (ZiFile::leafname(path) == ".libs") path = ZiFile::dirname(path);
#ifdef _WIN32
  return ZiFile::append(path, "zhashwork.exe");
#else
  return ZiFile::append(path, "zhashwork");
#endif
}

struct Case {
  const char *name;
  const char *scenario = "chained";
  unsigned count = 100;
  unsigned repeat = 3;
  const char *headroom = nullptr;
  const char *seed = "";
  unsigned generations = 4;
  bool late = false;
};

static int run(const Zi::Path &exe, const Case &c, const Zi::Path &config,
    const Zi::Path &report, const Zi::Path &log, const Zi::Path &late) {
  ZiFile output{log, ZiFile::Write | ZiFile::GC};
  if (!output) return 255;
  ZtString<> count, repeat;
  count << ZuBoxed(c.count);
  repeat << ZuBoxed(c.repeat);
  unsigned timeout = 30; // bounds a child, not a workload performance target
  if (auto s = getenv("Z_HASHTUNE_TIMEOUT")) timeout = strtoul(s, nullptr, 10);
  if (!timeout || timeout > 3600) return 255;
#ifndef _WIN32
  pid_t pid = fork();
  if (pid < 0) return 255;
  if (!pid) {
    setpgid(0, 0);
    dup2(output.handle(), STDOUT_FILENO);
    dup2(output.handle(), STDERR_FILENO);
    setenv("Z_HASHTUNE", config.data(), 1);
    unsetenv("HARNESS_ACTIVE");
    const char *args[] = {exe.data(), c.scenario, count.data(), repeat.data(),
      report.data(), c.headroom ? c.headroom : "-", c.late ? late.data() : nullptr,
      nullptr};
    // A command prefix makes instrumentation explicit for the child too.
    if (auto prefix = getenv("Z_HASHTUNE_CHILD")) {
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
    timespec deadline;
    if (!clock_gettime(CLOCK_MONOTONIC, &deadline)) {
      deadline.tv_sec += timeout;
      for (;;) {
	timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now)) break;
	int64_t ns = int64_t(deadline.tv_sec - now.tv_sec) * 1000000000 +
	  deadline.tv_nsec - now.tv_nsec;
	if (ns <= 0) break;
	int r = poll(&event, 1, int((ns + 999999)/1000000));
	if (r < 0 && errno == EINTR) continue;
	finished = r > 0 && (event.revents & POLLIN);
	break;
      }
    }
    close(fd);
  }
  if (!finished) {
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
  }
  int status;
  pid_t waited;
  do waited = waitpid(pid, &status, 0); while (waited < 0 && errno == EINTR);
  if (!finished || waited != pid) return 254;
  return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
#else
  ZtString<> command;
  ZtString<> exeArg{exe}, reportArg{report}, lateArg{late};
  if (auto prefix = getenv("Z_HASHTUNE_CHILD")) command << prefix << ' ';
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
    if (!_wcsnicmp(p, L"Z_HASHTUNE=", 11) || !_wcsnicmp(p, L"HARNESS_ACTIVE=", 15))
      continue;
    environment << p << '\0';
  }
  FreeEnvironmentStringsW(env);
  environment << "Z_HASHTUNE=" << config << '\0' << '\0';
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

template <typename T, typename Rows>
static bool readRows(const Zi::Path &path, Rows &rows, bool tuning = false) {
  ZtString<> data;
  if (!read(path, data) || !data || data[data.length() - 1] != '\n') return false;
  if (tuning && data.cspan().prefix(header) != header.length()) return false;
  ZfCSV::Reader<T, ZuFacet::Core> reader;
  bool ok = true;
  auto emit = [&rows, &ok, tuning](const auto &scan) {
    if (tuning) {
      if (scan.row.length() != 4) { ok = false; return; }
      for (unsigned i : {1U, 3U}) {
	if (!scan.row[i].template is<ZuSpan<char>>()) { ok = false; continue; }
	auto span = scan.row[i].template p<ZuSpan<char>>();
	ZuBox<unsigned> value;
	if (!span || value.scan(span) != span.length() || value > UINT8_MAX) ok = false;
      }
      if (!scan.row[2].template is<ZuSpan<char>>()) ok = false;
      else {
	auto span = scan.row[2].template p<ZuSpan<char>>();
	ZuBox<double> value;
	if (!span || value.scan(span) != span.length() || !(value > 0)) ok = false;
      }
    }
    rows.push(scan.ctor());
  };
  unsigned consumed = reader.process(data.span(), emit);
  return ok && consumed == data.length();
}

static const Config *configFor(const Configs &configs, ZuID id) {
  for (const auto &config : configs) if (config.id == id) return &config;
  return nullptr;
}

static bool verify(const Case &c, unsigned gen, const Configs &input,
    const Configs &output, const Reports &reports, const Reports &telemetry,
    const Zi::Path &dir) {
  bool ok = true;
  auto test = [&c, gen, &dir, &ok](bool value, const auto &detail) {
    ZtString<> label;
    label << c.name << " generation " << gen << ' ' << detail;
    if (!check(value, label)) { log("artifacts: ", dir); ok = false; }
  };
  auto is = [&c](const char *name) { return !strcmp(c.scenario, name); };
  bool multi = is("multi") || is("overlap") || is("reverse");
  unsigned tables = 0, groups = 0;
  for (unsigned i = 0; i < output.length(); ++i) {
    if (i) test(ZuCmp<ZuID>::cmp(output[i - 1].id, output[i].id) < 0,
      "configuration IDs emitted in deterministic order");
    for (unsigned j = 0; j < i; ++j)
      test(output[i].id != output[j].id, "unique configuration ID");
    if (workload(output[i].id)) ++groups;
  }
  for (const auto &r : reports) {
    if (r.phase != 1) continue;
    ++tables;
    const Report *initial = nullptr, *csv = nullptr, *before = nullptr;
    for (const auto &s : reports) {
      if (sameTable(r, s) && s.phase == 0) initial = &s;
      if (sameTable(r, s) && s.phase == 2) before = &s;
    }
    for (const auto &s : telemetry) if (sameTable(r, s)) csv = &s;
    test(initial && !initial->count && !initial->maxCount && !initial->resized,
      "initial counters zero");
    if (!initial) continue;
    auto loaded = configFor(input, r.id);
    unsigned initBits = loaded ? loaded->bits : 8;
    double loadFactor = loaded ? loaded->loadFactor : 1;
    if (before) { initBits = before->bits; loadFactor = before->loadFactor; }
    if (r.linear) {
      if (loadFactor < 0.5) loadFactor = 0.5;
      if (loadFactor > 1) loadFactor = 1;
    } else if (loadFactor < 1) loadFactor = 1;
    loadFactor = unsigned(loadFactor * 16)/16.0;
    test(initial->bits == initBits && initial->loadFactor == loadFactor,
      "configuration applied before workload");
    test(r.resized ? r.bits > initial->bits : r.bits == initial->bits,
      "resize telemetry matches terminating bits");
    unsigned cBits = loaded ? loaded->cBits : 3;
    if (cBits > 12) cBits = 12;
    if (cBits > initBits) cBits = initBits;
    test(initial->cBits == (is("concurrent") || is("striped") ? cBits : 0),
      "configured lock policy applied");
    test(!r.count, "table drained");
    uint64_t peak = c.count;
    if (r.id == "HS.unused") peak = 0;
    if (is("concurrent")) {
      peak *= 2;
      test(r.maxCount == peak, "serialized peak after concurrent count checks");
    } else if (multi) {
      test(r.maxCount == c.count || r.maxCount == c.count * 3,
	"known independent same-ID peaks");
    } else {
      if (r.id == "HS.other") peak *= 3;
      test(r.maxCount == peak, "lifetime peak retained after drain");
    }
    test(csv && csv->maxCount == r.maxCount && csv->count == r.count &&
      csv->bits == r.bits && csv->resized == r.resized,
      "diagnostic CSV matches captured telemetry");
    if (gen && !c.late) test(!r.resized, "tuned replay avoids resizing");
    auto tuned = configFor(output, r.id);
    test(tuned, "configuration row emitted");
    if (!tuned) continue;
    test(tuned->loadFactor == (loaded ? loaded->loadFactor : 1) &&
      tuned->cBits == (loaded ? loaded->cBits : 3), "only bits tuned");
    unsigned expected = 2;
    for (const auto &s : reports) {
      if (s.phase != 1 || s.id != r.id) continue;
      unsigned candidate = s.bits;
      if (!s.resized) {
	// Independent threshold model: capacity is ceil(slots * LF).
	double h = c.headroom ? strtod(c.headroom, nullptr) : 0.05;
	double raw = double(s.maxCount) * (1 + h);
	uint64_t target = uint64_t(raw);
	if (double(target) < raw) ++target;
	candidate = 2;
	unsigned limit = s.linear ? sizeof(unsigned) * 8 - 1 : 28;
	for (; candidate < limit; ++candidate) {
	  double rawCapacity = double(uint64_t{1} << candidate) * s.loadFactor;
	  uint64_t capacity = uint64_t(rawCapacity);
	  if (double(capacity) < rawCapacity) ++capacity;
	  if (target <= capacity) break;
	}
      }
      if (candidate > expected) expected = candidate;
    }
    ZtString<> detail;
    detail << r.id << " tuned bits expected=" << expected << " actual=" << unsigned(tuned->bits);
    test(tuned->bits == expected, detail);
    if (gen >= 2 && !c.late)
      test(loaded && loaded->bits == tuned->bits, "stable repeated tuning");
    if (is("shadow")) test(r.shadow, "registered shadow participates");
    if (before) test(initial->bits == before->bits,
      "late init leaves existing table unchanged");
  }
  bool pair = multi || is("ids") || is("mixed") || is("late") || is("excluded");
  test(tables == (pair ? 2U : 1U), "exact workload table count");
  test(groups == (is("ids") || is("excluded") ? 2U : 1U), "exact configuration row count");
  test(telemetry.length() == tables, "diagnostic CSV includes all live workload tables");
  if (multi) {
    uint64_t sum = 0;
    for (const auto &r : reports) if (r.phase == 1) sum += r.maxCount;
    test(sum == c.count * 4, "both unequal peaks observed");
  }
  for (const auto &s : input) {
    if (!workload(s.id)) continue;
    bool live = false;
    for (const auto &r : reports) if (r.phase == 1 && r.id == s.id) live = true;
    test(bool(configFor(output, s.id)) == live, "uninstantiated IDs omitted");
  }
  return ok;
}

static void runCase(const Zi::Path &exe, const Case &c, const Zi::Path &root) {
  Zi::Path dir = ZiFile::append(root, c.name);
  if (!check(ZiFile::mkdir(dir) == Zi::OK, ZtString<>{"create case directory"})) return;
  Zi::Path config = ZiFile::append(dir, "tuning.csv");
  Zi::Path late = ZiFile::append(dir, "late.csv");
  if (!check(write(config, c.late ? "" : c.seed), ZtString<>{"seed configuration"})) return;
  if (c.late && !check(write(late, c.seed), ZtString<>{"late configuration"})) return;
  for (unsigned gen = 0; gen < c.generations; ++gen) {
    Zi::Name stem;
    stem << "generation-" << gen;
    Zi::Path base = ZiFile::append(dir, stem);
    Zi::Path before, after, report, childLog;
    before << base << ".input.csv";
    after << base << ".output.csv";
    report << base << ".report.csv";
    childLog << base << ".log";
    Configs input, output;
    Reports reports, telemetry, allTelemetry;
    ZtString<> seed;
    if (!check(read(c.late ? late : config, seed), ZtString<>{"read input"})) return;
    if (seed.length() && !check(readRows<Config>(c.late ? late : config, input, true),
	ZtString<>{"parse input configuration"})) return;
    if (!check(ZiFile::copy(config, before) == Zi::OK,
	ZtString<>{"save input generation"})) return;
    int status = run(exe, c, config, report, childLog, late);
    ZtString<> name;
    name << c.name << " generation " << gen << " child exit";
    if (!check(!status, name)) { log("status=", status, " log=", childLog); return; }
    if (!check(ZiFile::copy(config, after) == Zi::OK,
	ZtString<>{"save output generation"})) return;
    if (!strcmp(c.name, "replacement") && !gen) {
      ZtString<> replacement;
      check(read(config, replacement) && replacement.length() < seed.length(),
	ZtString<>{"longer seed replaced without stale bytes"});
    }
    if (!check(readRows<Config>(config, output, true),
	ZtString<>{"complete four-column tuning CSV"})) return;
    if (!check(readRows<Report>(report, reports), ZtString<>{"complete snapshot CSV"})) return;
    Zi::Path diagnostic;
    diagnostic << report << ".hash.csv";
    if (!check(readRows<Report>(diagnostic, allTelemetry),
	ZtString<>{"complete diagnostic CSV"})) return;
    for (auto &r : allTelemetry) if (workload(r.id)) telemetry.push(ZuMv(r));
    if (!verify(c, gen, input, output, reports, telemetry, dir)) return;
  }
}

static void cases(const Zi::Path &exe) {
  ZuTestScopeRT(cases);
  auto root = ZiTestResidue::dir("run");
  unsigned selected = 0;
  auto test = [&root, &exe, &selected](const Case &c) {
    if (auto filter = getenv("Z_HASHTUNE_CASE")) if (strcmp(filter, c.name)) return;
    ++selected;
    runCase(exe, c, root);
  };
  test({"cold"});
  test({"linear", "linear"});
  test({"header", "chained", 100, 3, nullptr, header.data()});
  test({"defaults", "chained", 100, 3, nullptr, "id,bits,loadFactor,cBits\nHS.table,8,1,3\n"});
  test({"quoted-id", "quoted"});
  test({"grown", "chained", 125, 3, nullptr, "id,bits,loadFactor,cBits\nHS.table,2,1,3\n"});
  test({"linear-grown", "linear", 125, 3, nullptr, "id,bits,loadFactor,cBits\nHS.table,2,1,3\n"});
  test({"oversized", "chained", 100, 3, nullptr, "id,bits,loadFactor,cBits\nHS.table,12,1,3\n"});
  test({"below-default", "chained", 3});
  test({"zero-headroom", "chained", 4, 3, "0", "id,bits,loadFactor,cBits\nHS.table,2,1,3\n"});
  test({"threshold-below", "chained", 3, 3, "0", "id,bits,loadFactor,cBits\nHS.table,2,1,3\n"});
  test({"threshold-above", "chained", 5, 3, "0", "id,bits,loadFactor,cBits\nHS.table,2,1,3\n"});
  test({"fractional-threshold", "chained", 6, 3, "0", "id,bits,loadFactor,cBits\nHS.table,2,1.3125,3\n"});
  test({"maximum-bits", "arithmetic", 1U << 28, 1, "0"});
  test({"linear-wide-bits", "arithmetic-linear", 1U << 28, 1, "0",
    "id,bits,loadFactor,cBits\nHS.table,8,0.5,3\n"});
  test({"headroom-boundary", "chained", 125});
  test({"quarter-headroom", "chained", 100, 3, "0.25"});
  test({"volume", "chained", 100, 7});
  test({"clean", "clean"});
  test({"multiple-ids", "ids"});
  test({"same-id", "multi"});
  test({"same-id-overlap", "overlap"});
  test({"same-id-reverse", "reverse"});
  test({"mixed", "mixed", 100, 3, nullptr, "id,bits,loadFactor,cBits\nHS.table,8,0.75,6\n"});
  test({"linear-load", "linear", 100, 3, nullptr, "id,bits,loadFactor,cBits\nHS.table,8,0.5,3\n"});
  test({"policy", "chained", 3, 3, nullptr, "id,bits,loadFactor,cBits\nHS.table,8,1.25,8\n"});
  test({"policy-precision", "chained", 3, 3, nullptr,
    "id,bits,loadFactor,cBits\nHS.table,8,1.2345678912345,8\n"});
  test({"lock-policy", "striped", 3, 3, nullptr, "id,bits,loadFactor,cBits\nHS.table,8,1.25,8\n"});
  test({"concurrent", "concurrent", 100, 1});
  test({"shadow", "shadow", 100, 1});
  test({"excluded", "excluded", 100, 1});
  test({"unused", "chained", 0});
  test({"late", "late", 100, 1, nullptr, "id,bits,loadFactor,cBits\nHS.table,10,1.25,4\n", 1, true});
  test({"utility", "utility"});
  ZtString<> replacement{header};
  for (unsigned i = 0; i < 100; ++i)
    replacement << "HS.old" << i << ",12,1,3\n";
  test({"replacement", "chained", 100, 3, nullptr, replacement.data()});
  ZuCheckRT(selected > 0);
}

} // HashTest

int main(int argc, char **argv) {
  ZuTestUtil::parse(argc, argv);
  ZiTestResidue::init("zhashtune");
  ZuTestMgr::finalFn(&ZiTestResidue::final);
  ZuTestMain();
  auto exe = executable(argv[0]);
  ZuTestCall(cases, exe);
  return 0;
}
