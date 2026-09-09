//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <zlib/ZuBox.hh>

#include <zlib/ZmBitmap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiFile.hh>

#include "ZhttpTestUtil.hh"
#include "ZrestITestPorts.hh"

enum { CaseTimeout = 15, ReadyTimeout = 5 };

static volatile sig_atomic_t interrupted;

static void interrupt(int sig) { interrupted = sig; }

ZuDerive(MatrixString, (ZtString<ZtStringHeapID<"zrest.Matrix.String">>));
ZuDerive(MatrixArgs, (ZtArray<MatrixString,
  ZtArrayHeapID<"zrest.Matrix.Args">>));
ZuDerive(MatrixArgv, (ZtArray<char *,
  ZtArrayHeapID<"zrest.Matrix.Argv">>));

struct Command {
  MatrixArgs args;

  Command &add(ZuCSpan value) { args.push(value); return *this; }
  Command &option(ZuCSpan name, ZuCSpan value) {
    MatrixString arg;
    arg << "--" << name << '=' << value;
    return add(arg);
  }
  Command &option(ZuCSpan name, uint64_t value) {
    MatrixString arg;
    arg << "--" << name << '=' << value;
    return add(arg);
  }
};

struct Child {
  MatrixString name;
  MatrixString output;
  pid_t pid = -1;
  int fd = -1;
  int status = -1;
};

static uint64_t monoMS()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return uint64_t(ts.tv_sec) * 1000 + uint64_t(ts.tv_nsec) / 1000000;
}

static bool exists(ZuCSpan path)
{
  MatrixString value{path};
  return !access(value.data(), X_OK);
}

static bool spawn(Child &child, ZuCSpan name, Command &command)
{
  int fds[2];
  if (pipe(fds)) return false;
  MatrixArgv argv;
  for (unsigned i = 0, n = command.args.length(); i < n; ++i)
    argv.push(command.args[i].data());
  argv.push(nullptr);
  pid_t pid = fork();
  if (!pid) {
    close(fds[0]);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    execv(argv[0], argv.data());
    _exit(127);
  }
  close(fds[1]);
  if (pid < 0) { close(fds[0]); return false; }
  int flags = fcntl(fds[0], F_GETFL, 0);
  if (flags >= 0) (void)fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
  child.name = name;
  child.pid = pid;
  child.fd = fds[0];
  return true;
}

static void drain(Child &child)
{
  char buf[4096];
  for (;;) {
    ssize_t n = read(child.fd, buf, sizeof(buf));
    if (n > 0) { child.output << ZuCSpan{buf, unsigned(n)}; continue; }
    if (n < 0 && errno == EINTR) continue;
    break;
  }
}

static bool pollChild(Child &child)
{
  if (child.pid < 0 || child.status >= 0) return true;
  drain(child);
  int status;
  pid_t pid = waitpid(child.pid, &status, WNOHANG);
  if (!pid) return false;
  if (pid < 0) status = 0xff00;
  child.status = status;
  drain(child);
  close(child.fd);
  child.fd = -1;
  return true;
}

static void terminate(Child &child)
{
  if (child.pid < 0 || child.status >= 0) return;
  kill(child.pid, SIGTERM);
  uint64_t deadline = monoMS() + 1000;
  while (monoMS() < deadline && !pollChild(child)) usleep(1000);
  if (!pollChild(child)) {
    kill(child.pid, SIGKILL);
    while (waitpid(child.pid, &child.status, 0) < 0 && errno == EINTR) { }
    drain(child);
    close(child.fd);
    child.fd = -1;
  }
}

static bool waitChild(Child &child, uint64_t deadline)
{
  while (!interrupted && monoMS() < deadline) {
    if (pollChild(child)) return WIFEXITED(child.status) && !WEXITSTATUS(child.status);
    usleep(1000);
  }
  terminate(child);
  return false;
}

static bool waitListening(Child &server, uint64_t deadline)
{
  while (!interrupted && monoMS() < deadline) {
    drain(server);
    if (server.output.find("event=listening") >= 0) return true;
    if (pollChild(server)) return false;
    usleep(1000);
  }
  return false;
}

static int findAfter(ZuCSpan text, ZuCSpan needle, unsigned offset)
{
  if (offset >= text.length()) return -1;
  int found = ZuCSpan{text.data() + offset, text.length() - offset}.find(needle);
  return found < 0 ? -1 : int(offset) + found;
}

static bool sequence(ZuCSpan log)
{
  int auth = log.find("event=auth");
  int pong1 = auth >= 0 ? findAfter(log, "event=pong", unsigned(auth + 1)) : -1;
  int refresh = pong1 >= 0 ? findAfter(log, "event=refresh", unsigned(pong1 + 1)) : -1;
  int pong2 = refresh >= 0 ? findAfter(log, "event=pong", unsigned(refresh + 1)) : -1;
  return auth >= 0 && pong1 > auth && refresh > pong1 && pong2 > refresh;
}

static bool pongIDs(ZuCSpan log, unsigned expected)
{
  static constexpr ZuCSpan prefix{"event=pong id="};
  ZmBitmap seen;
  unsigned offset = 0, count = 0, l = log.length();
  while (offset < l) {
    int found = findAfter(log, prefix, offset);
    if (found < 0) break;
    unsigned valueOffset = unsigned(found) + prefix.length();
    ZuBox<unsigned> value;
    int n = value.scan(log.data() + valueOffset,
      l - valueOffset);
    if (n <= 0 || value >= expected || seen.get(value)) return false;
    seen.set(value);
    ++count;
    offset = valueOffset + unsigned(n);
  }
  return count == expected;
}

static bool pacedWaves(
    ZuCSpan log, unsigned expected, unsigned count, uint64_t &lastNS)
{
  static constexpr ZuCSpan prefix{"event=ping-wave tick="};
  unsigned offset = 0;
  for (unsigned i = 1; i <= expected; ++i) {
    int found = findAfter(log, prefix, offset);
    if (found < 0) return false;
    unsigned valueOffset = unsigned(found) + prefix.length();
    ZuBox<unsigned> tick;
    int n = tick.scan(log.data() + valueOffset, log.length() - valueOffset);
    if (n <= 0 || tick != i) return false;
    int countAt = findAfter(log, " count=", valueOffset + unsigned(n));
    if (countAt < 0) return false;
    ZuBox<unsigned> waveCount;
    unsigned countOffset = unsigned(countAt) + strlen(" count=");
    n = waveCount.scan(log.data() + countOffset, log.length() - countOffset);
    if (n <= 0 || waveCount != count) return false;
    int timeAt = findAfter(log, " time_ns=", countOffset + unsigned(n));
    if (timeAt < 0) return false;
    unsigned timeOffset = unsigned(timeAt) + strlen(" time_ns=");
    ZuBox<uint64_t> time;
    n = time.scan(log.data() + timeOffset, log.length() - timeOffset);
    if (n <= 0) return false;
    lastNS = time;
    offset = timeOffset + unsigned(n);
  }
  return findAfter(log, prefix, offset) < 0;
}

static Command serverCommand(bool go, unsigned port,
    const Zhttp::Test::TempDir &temp, unsigned requests,
    ZuCSpan accessLifetime)
{
  Command command;
  if (go) {
    MatrixString addr;
    addr << "127.0.0.1:" << port;
    command.add("../interop/go/server").option("addr", addr)
      .option("cert", temp.certPath).option("key", temp.keyPath);
  } else {
    command.add("../example/zrestd").add("--https")
      .option("http2", "disable").option("addr", "127.0.0.1")
      .option("port", port).option("cert", temp.certPath)
      .option("key", temp.keyPath).option("log", "-");
  }
  command.option("access-token-lifetime", accessLifetime)
    .option("refresh-token-lifetime", "1h")
    .option("jwt-secret", "matrix-secret").option("requests", requests)
    .add("--verbose");
  return command;
}

static Command clientCommand(bool go, unsigned port,
    const Zhttp::Test::TempDir &temp, unsigned requests, unsigned jobs,
    ZuCSpan interval)
{
  MatrixString url;
  url << "https://localhost:" << port;
  Command command;
  if (go) {
    command.add("../interop/go/client").option("url", url)
      .option("cert", temp.certPath).option("requests", requests)
      .option("interval", interval);
  } else {
    command.add("../example/zrest").option("http3", "disable")
      .option("http2", "disable").option("ca", temp.certPath)
      .option("requests", requests).option("jobs", jobs)
      .option("links", jobs).option("link-max", 1)
      .option("interval", interval).add("--verbose").add(url);
  }
  return command;
}

static void failureOutput(const Child &server, const Child &client)
{
  auto out = ZiFile::stdOut();
  out << "# server (" << server.name << "):\n" << server.output <<
    "# client (" << client.name << "):\n" << client.output;
}

static bool runPair(bool goClient, bool goServer, unsigned requests,
    unsigned jobs, ZuCSpan interval, ZuCSpan accessLifetime,
    bool expectRefresh, bool fractional)
{
  Zhttp::Test::TempDir temp;
  if (!temp.init("zrestmatrix") || !Zhttp::Test::writeLocalhostCert(
	temp, temp.certPath, temp.keyPath)) return false;
  static unsigned nextPort = ZrestITestPort::Matrix;
  unsigned port = Zhttp::Test::loopbackPort(nextPort++);
  if (!port) return false;
  Command serverCmd = serverCommand(
    goServer, port, temp, requests, accessLifetime);
  Command clientCmd = clientCommand(
    goClient, port, temp, requests, jobs, interval);
  Child server, client;
  if (!spawn(server, goServer ? "go server" : "zrestd", serverCmd)) return false;
  bool ok = waitListening(server, monoMS() + ReadyTimeout * 1000);
  if (ok) ok = spawn(client, goClient ? "go client" : "zrest", clientCmd);
  uint64_t deadline = monoMS() + CaseTimeout * 1000;
  if (ok) ok = waitChild(client, deadline);
  if (ok) ok = waitChild(server, deadline);
  else terminate(server);
  if (ok) {
    MatrixString summary;
    summary << "summary auth=1 refresh=" << (expectRefresh ? 1 : 0) <<
      " pong=" << requests;
    ok = server.output.find(summary) >= 0 &&
      (!expectRefresh || sequence(server.output));
    if (!goClient) ok = ok && pongIDs(client.output, requests);
    if (fractional) {
      int auth = client.output.find("event=auth time_ns=");
      uint64_t tenthNS = 0;
      ok = ok && pacedWaves(client.output, 10, 2, tenthNS);
      if (auth < 0) ok = false;
      else {
	ZuBox<uint64_t> authNS;
	authNS.scan(client.output.data() + auth + strlen("event=auth time_ns="));
	ok = ok && tenthNS >= authNS + 90000000ULL;
      }
    }
  }
  terminate(server);
  terminate(client);
  if (!ok) failureOutput(server, client);
  return ok;
}

static bool runProbe(bool goServer)
{
  Zhttp::Test::TempDir temp;
  if (!temp.init("zrestprobe") || !Zhttp::Test::writeLocalhostCert(
	temp, temp.certPath, temp.keyPath)) return false;
  static unsigned nextPort = ZrestITestPort::MatrixEnd - 1;
  unsigned port = Zhttp::Test::loopbackPort(nextPort++);
  Command serverCmd = serverCommand(goServer, port, temp, 1000000, "1s");
  MatrixString url;
  url << "https://localhost:" << port;
  Command probeCmd;
  probeCmd.add("./zrestprobe").option("url", url)
    .option("cert", temp.certPath).option("jwt-secret", "matrix-secret");
  Child server, probe;
  if (!spawn(server, goServer ? "go server" : "zrestd", serverCmd)) return false;
  bool ok = waitListening(server, monoMS() + ReadyTimeout * 1000);
  if (ok) ok = spawn(probe, "zrestprobe", probeCmd);
  if (ok) ok = waitChild(probe, monoMS() + CaseTimeout * 1000);
  terminate(server);
  terminate(probe);
  if (!ok) failureOutput(server, probe);
  return ok;
}

int main()
{
  auto out = ZiFile::stdOut();
#ifdef _WIN32
  out << "1..0 # SKIP process fixture is not available on Windows\n";
  return 0;
#else
  struct sigaction action{};
  action.sa_handler = interrupt;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
  if (!exists("../interop/go/client") || !exists("../interop/go/server") ||
      !exists("./zrestprobe")) {
    out << "1..0 # SKIP Go interoperability fixtures unavailable\n";
    return 0;
  }
  ZiTestResidue::init("zrestmatrix");
  out << "1..9\n";
  unsigned test = 0, failed = 0;
#define RUN(name, expression) do { \
    if (interrupted) break; \
    bool ok = (expression); ++test; if (!ok) ++failed; \
    MatrixString result; \
    result << (ok ? "ok " : "not ok ") << test << " - " << name << '\n'; \
    out << result; \
  } while (0)
  RUN("zrest to Go server", runPair(false, true, 2, 1, "2", "3s", true, false));
  RUN("Go client to zrestd", runPair(true, false, 2, 1, "2s", "1s", true, false));
  RUN("zrest to zrestd", runPair(false, false, 2, 1, "2", "3s", true, false));
  RUN("Go client to Go server", runPair(true, true, 2, 1, "2s", "1s", true, false));
  RUN("concurrent zrest to zrestd", runPair(false, false, 8, 4, "2", "3s", true, false));
  RUN("concurrent zrest to Go", runPair(false, true, 8, 4, "2", "3s", true, false));
  RUN("fractional zrest pacing", runPair(false, false, 20, 2, "0.01", "5m", false, true));
  RUN("zrestd negative authentication", runProbe(false));
  RUN("Go negative authentication", runProbe(true));
#undef RUN
  bool passed = !interrupted && !failed;
  ZiTestResidue::final(passed);
  if (interrupted) return 128 + interrupted;
  return passed ? 0 : 1;
#endif
}
