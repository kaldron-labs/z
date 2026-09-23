//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZdbusConnection.hh>

#include "../util/ZdbusTestTool.hh"

using namespace ZuTestUtil;

enum { TimeoutSeconds = 5 };
// More than the longest decimal UID rendered as EXTERNAL auth hex text.
constexpr unsigned AuthLineCapacity = 2 * sizeof(unsigned) * 3 + 32;

static bool waitFor(ZmSemaphore &sem)
{
  return sem.timedwait(Zm::now(TimeoutSeconds)) == 0;
}

struct SocketReady {
  ZiEventLoop loop;
  ZmSemaphore started, stopped;
  ZmAtomic<int> state = 0;

  bool start(ZmScheduler *sched)
  {
    loop.init(sched, 3, [this](ZeException) {
      state.store_(-1);
      started.post();
    });
    loop.start([this](ZiEvent::StartResult result) {
      state.store_(result.is<ZiEvent::Exception>() ? -1 : 1);
      started.post();
    });
    if (waitFor(started) && state.load_() == 1) return true;
    loop.final();
    return false;
  }

  bool readable(int fd)
  {
    ZmSemaphore signalled;
    ZmAtomic<int> result = 0;
    loop.run([this, fd, &signalled, &result] {
      if (loop.addSocket(fd, {},
          [this, &signalled, &result](Zi::Socket socket) {
            result.store_(1);
            signalled.post();
            loop.delSocket(socket);
          }, false)) return;
      result.store_(-1);
      signalled.post();
    });
    bool done = waitFor(signalled);
    if (!done) {
      ZmSemaphore drained;
      loop.run([this, fd, &drained] {
        loop.delSocket(fd);
        drained.post();
      });
      ZmAssert_(waitFor(drained));
    }
    return done && result.load_() == 1;
  }

  void stop()
  {
    loop.stop([this](ZiEvent::StopResult) { stopped.post(); });
    ZmAssert_(waitFor(stopped));
    loop.final();
  }
};

static bool readLine(SocketReady &source, int fd, char *out,
  unsigned capacity)
{
  for (unsigned i = 0; i < capacity;) {
    int n = int(::recv(fd, out + i, 1, MSG_DONTWAIT));
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (!source.readable(fd)) return false;
      continue;
    }
    if (n != 1) return false;
    if (out[i] == '\n') {
      out[i + 1] = 0;
      return true;
    }
    ++i;
  }
  return false;
}

static bool readBytes(SocketReady &source, int fd, uint8_t *out,
  unsigned length)
{
  for (unsigned offset = 0; offset < length;) {
    int n = int(::recv(fd, out + offset, length - offset,
      MSG_DONTWAIT));
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (!source.readable(fd)) return false;
      continue;
    }
    if (n <= 0) return false;
    offset += unsigned(n);
  }
  return true;
}

static void transport()
{
  ZuTestScope(transport);

  ZdbusAddress::Text text;
  text << "unix:abstract=zdbus-cxn-test-" << unsigned(::getpid());
  ZdbusAddress address;
  ZuCHECK(ZdbusAddress::parse(address, text), "address parse failed");
  if (!address) return;

  sockaddr_un sa;
  socklen_t saLen;
  ZuCHECK(address.socketAddr(sa, saLen), "sockaddr conversion failed");
  int listener = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  ZuCHECK(listener >= 0, "socket failed");
  if (listener < 0) return;
  if (::bind(listener, reinterpret_cast<sockaddr *>(&sa), saLen) ||
      ::listen(listener, 1)) {
    ZuCHECK(false, "bind/listen failed");
    ::close(listener);
    return;
  }
  ZmScheduler sched{ZmSchedParams().id("ZdbusConnectionTest").nThreads(3)};
  sched.start();
  SocketReady source;
  ZuCHECK(source.start(&sched), "peer event loop did not start");
  if (source.state.load_() != 1) {
    sched.stop();
    ::close(listener);
    return;
  }

  ZdbusConnection cxn;
  ZmSemaphore ready;
  ZmSemaphore frame;
  ZmSemaphore sent;
  ZmSemaphore rejected;
  ZmSemaphore disconnected;
  ZmSemaphore stopped;
  ZmSemaphore stoppedAgain;
  ZmAtomic<unsigned> failures = 0;
  ZmAtomic<unsigned> frames = 0;
  ZmAtomic<unsigned> rejectedOK = 0;
  ZmAtomic<unsigned> stage = 0;
  ZmAtomic<int> failureCode = -1;
  ZmAtomic<int> failureSystem = -1;
  Zdbus_::CxnParams params;
  params.rxBytes = 3;
  params.txBytes = 3;
  params.msgs = 1;
  params.queueBytes = Zdbus_::Wire::MinHeaderSize;

  cxn.init(&sched, 1, 2, ZuMv(address), params,
    [&ready, &cxn] { cxn.ready(); ready.post(); },
    [&frames, &frame, &stage](ZmRef<ZiIOBuf>, Zdbus_::FrameInfo info) {
      if (info.serial == 7) ++frames;
      stage.store_(stage.load_() == 1 ? 2U : ~0U);
      frame.post();
    },
    [&failures, &failureCode, &failureSystem, &disconnected, &stage](
        Zdbus_::CxnFailure failure) {
      ++failures;
      failureCode.store_(failure.code);
      failureSystem.store_(failure.system);
      stage.store_(stage.load_() == 2 ? 3U : ~0U);
      disconnected.post();
    });
  cxn.start();
  cxn.start();

  int peer = -1;
  if (source.readable(listener)) peer = ::accept4(listener, nullptr, nullptr,
    SOCK_CLOEXEC);
  ZuCHECK(peer >= 0, "connection timed out");
  if (peer >= 0) {
    ZuArray<char, AuthLineCapacity> line;
    bool nul = source.readable(peer) &&
      ::recv(peer, line.data(), 1, MSG_DONTWAIT) == 1 && !line[0];
    ZuCHECK(nul, "missing initial NUL");
    bool auth = readLine(source, peer, line.data(), line.size() - 1);
    ZuCHECK(auth && !strncmp(line.data(), "AUTH EXTERNAL ", 14),
      "missing EXTERNAL auth");
    constexpr char ok[] = "OK 0123456789abcdef0123456789abcdef\r\n";
    ZuCHECK(::send(peer, ok, sizeof(ok) - 1, MSG_NOSIGNAL) ==
      int(sizeof(ok) - 1), "auth reply send failed");
    bool begin = readLine(source, peer, line.data(), line.size() - 1);
    ZuCHECK(begin && !strcmp(line.data(), "BEGIN\r\n"),
      "missing BEGIN");
    ZuCHECK(waitFor(ready), "ready callback timed out");

    const uint8_t msg[] = {
      'l', 5, 0, 1, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0
    };
    ZuAssert(sizeof(msg) == Zdbus_::Wire::MinHeaderSize);
    ZmRef<ZiIOBuf> oversized = new ZiIOBufAlloc<>{};
    oversized->append(msg);
    *oversized << char(0);
    cxn.send(ZuMv(oversized), [&rejected, &rejectedOK](bool ok) {
      rejectedOK.store_(!ok);
      rejected.post();
    });
    ZuCHECK(waitFor(rejected) && rejectedOK.load_(),
      "oversized queued frame was not rejected");
    ZmRef<ZiIOBuf> outbound = new ZiIOBufAlloc<>{};
    outbound->append(msg);
    cxn.send(ZuMv(outbound), [&sent, &failures, &stage](bool ok) {
      if (!ok) ++failures;
      stage.store_(ok && !stage.load_() ? 1U : ~0U);
      sent.post();
    });
    ZuArray<uint8_t, Zdbus_::Wire::MinHeaderSize> received;
    ZuCHECK(readBytes(source, peer, received.data(), received.size()),
      "outbound frame timed out");
    ZuCHECK(!memcmp(msg, received.data(), received.size()),
      "outbound frame mismatch");
    ZuCHECK(waitFor(sent), "send completion timed out");

    ZuCHECK(::send(peer, msg, sizeof(msg), MSG_NOSIGNAL) ==
      int(sizeof(msg)), "frame send failed");
    ZuCHECK(waitFor(frame), "frame callback timed out");
    ::close(peer);
    ZuCHECK(waitFor(disconnected), "peer EOF was not reported");
  }
  ::close(listener);
  source.stop();

  cxn.stop([&stopped, &stage] {
    stage.store_(stage.load_() == 3 ? 4U : ~0U);
    stopped.post();
  });
  cxn.stop([&stoppedAgain] { stoppedAgain.post(); });
  ZuCHECK(waitFor(stopped), "stop timed out");
  cxn.final();
  ZuCHECK(waitFor(stoppedAgain), "second stop timed out");
  sched.stop();
  ZuCHECK(frames.load_() == 1, "frame not received");
  ZuCHECK(stage.load_() == 4,
    "Tx send, Rx frame/failure, Tx stop ordering was violated");
  ZuCHECK(failures.load_() == 1 &&
    failureCode.load_() == Zdbus_::CxnError::System &&
    failureSystem.load_() == ECONNRESET,
    "peer EOF was not the single terminal failure");
}

static void stopBeforeStart()
{
  ZuTestScope(stopBeforeStart);
  ZdbusAddress address;
  ZuCHECK(ZdbusAddress::parse(address,
    "unix:abstract=zdbus-stop-before-start"), "address parse failed");
  if (!address) return;

  ZmScheduler sched{ZmSchedParams().id("ZdbusStopBeforeStart").nThreads(2)};
  ZdbusConnection cxn;
  ZmSemaphore stopped;
  ZmAtomic<unsigned> failures = 0;
  sched.start();
  cxn.init(&sched, 1, 2, ZuMv(address), {}, {}, {},
    [&failures](Zdbus_::CxnFailure) { ++failures; });
  cxn.stop([&stopped] { stopped.post(); });
  cxn.start();
  ZuCHECK(waitFor(stopped), "stop-before-start timed out");
  cxn.final();
  sched.stop();
  ZuCHECK(!failures.load_(), "stopped connection attempted to start");
}

static void childWait()
{
  ZuTestScope(childWait);
  // pipe2's two-int array is required by the OS ABI.
  int readyPipe[2];
  bool pipeOK = !::pipe2(readyPipe, O_CLOEXEC);
  ZuCHECK(pipeOK, "child ready pipe failed");
  if (!pipeOK) return;
  pid_t child = ::fork();
  if (!child) {
    ::close(readyPipe[0]);
    struct sigaction action{};
    action.sa_handler = SIG_IGN;
    if (::sigaction(SIGTERM, &action, nullptr)) _exit(1);
    constexpr char ready[] = "ready\n";
    if (::write(readyPipe[1], ready, sizeof(ready) - 1) !=
        int(sizeof(ready) - 1)) _exit(1);
    for (;;) ::pause(); // the parent must escalate from SIGTERM to SIGKILL
  }
  ::close(readyPipe[1]);
  ZuCHECK(child > 0, "child fork failed");
  if (child < 0) {
    ::close(readyPipe[0]);
    return;
  }
  ZdbusAddress::Text ready;
  bool started = ZdbusTestTool::line(readyPipe[0], ready) &&
    ready == "ready";
  ::close(readyPipe[0]);
  ZuCHECK(started, "child did not become ready");
  int status = 0;
  bool reaped = ZdbusTestTool::waitChild(child,
    started ? SIGTERM : SIGKILL, status, 1);
  ZuCHECK(started && reaped && WIFSIGNALED(status) &&
    WTERMSIG(status) == SIGKILL,
    "child wait did not escalate and reap");
  int remaining;
  ZuCHECK(::waitpid(child, &remaining, WNOHANG) < 0 && errno == ECHILD,
    "child remained waitable after reap");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(transport);
  ZuTestCall(stopBeforeStart);
  ZuTestCall(childWait);
  return 0;
}
