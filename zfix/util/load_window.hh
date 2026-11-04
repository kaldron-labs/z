//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Opt-in process resource envelope for repository-owned Linux benchmark peers

#ifndef ZfixLoadWindow_HH
#define ZfixLoadWindow_HH
#ifndef _WIN32
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zlib/ZuAssert.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfStruct.hh>
#include "load.hh"
#include "load_resources.hh"

namespace ZfixLoad {
struct WindowSample {
  bool valid = false;
  uint64_t wallBeforeNs = 0, wallAfterNs = 0, cpuNs = 0, userNs = 0, systemNs = 0;
  uint64_t voluntary = 0, involuntary = 0, helperCpuNs = 0;
  static WindowSample capture(clockid_t helper) {
    WindowSample sample;
    auto process = ProcessPhaseResources::capture();
    timespec cpu{}, end{};
    if (!process.valid || clock_gettime(helper, &cpu) ||
        clock_gettime(CLOCK_MONOTONIC, &end)) return sample;
    sample.wallBeforeNs = process.wallBeforeNs;
    sample.wallAfterNs = ProcessPhaseResources::ns(end);
    sample.cpuNs = process.cpuNs; sample.userNs = process.userNs;
    sample.systemNs = process.systemNs; sample.voluntary = process.voluntary;
    sample.involuntary = process.involuntary;
    sample.helperCpuNs = ProcessPhaseResources::ns(cpu);
    sample.valid = true; return sample;
  }
};
ZfStruct(, (WindowSample, JSON), (valid, , Bool),
  (wallBeforeNs, , UInt64), (wallAfterNs, , UInt64), (cpuNs, , UInt64),
  (userNs, , UInt64), (systemNs, , UInt64), (voluntary, , UInt64),
  (involuntary, , UInt64), (helperCpuNs, , UInt64));

struct WindowWire {
  enum { Magic = 0x5a57494e, Version = 1, Start = 1, End = 2 };
  uint32_t magic = Magic, version = Version, command = 0, pid = 0, error = 0;
  uint64_t token = 0, sends = 0, receives = 0, polls = 0;
  WindowSample start, end;
  enum { Words = 27 };
  static void packSample(uint64_t *out, const WindowSample &sample) {
    const auto &p = sample;
    out[0] = sample.valid; out[1] = p.wallBeforeNs; out[2] = p.wallAfterNs;
    out[3] = p.cpuNs; out[4] = p.userNs; out[5] = p.systemNs;
    out[6] = p.voluntary; out[7] = p.involuntary; out[8] = sample.helperCpuNs;
  }
  static bool unpackSample(WindowSample &sample, const uint64_t *in) {
    if (in[0] > 1) return false;
    auto &p = sample;
    sample.valid = bool(in[0]); p.wallBeforeNs = in[1]; p.wallAfterNs = in[2];
    p.cpuNs = in[3]; p.userNs = in[4]; p.systemNs = in[5];
    p.voluntary = in[6]; p.involuntary = in[7]; sample.helperCpuNs = in[8];
    return true;
  }
  void pack(uint64_t *out) const {
    out[0] = magic; out[1] = version; out[2] = command; out[3] = pid; out[4] = error;
    out[5] = token; out[6] = sends; out[7] = receives; out[8] = polls;
    packSample(out + 9, start); packSample(out + 18, end);
  }
  bool unpack(const uint64_t *in) {
    if (in[0] != Magic || in[1] != Version || in[2] > UINT32_MAX ||
        in[3] > UINT32_MAX || in[4] > UINT32_MAX) return false;
    magic = in[0]; version = in[1]; command = in[2]; pid = in[3]; error = in[4];
    token = in[5]; sends = in[6]; receives = in[7]; polls = in[8];
    return unpackSample(start, in + 9) && unpackSample(end, in + 18);
  }
};
ZuAssert(WindowWire::Words * sizeof(uint64_t) < 4096);

// Only task-owned helper threads call this blocking transport. AppRx/Tx and
// SDK threads never wait on a control socket, semaphore or control response.
class WindowChannel {
public:
  explicit WindowChannel(unsigned seconds) : m_ms{int(seconds * 1000)} {
    if (auto text = getenv("ZFIX_WINDOW_FD")) {
      char *end = nullptr; long value = strtol(text, &end, 10);
      if (end && !*end && value >= 0 && value <= INT_MAX) m_fd = int(value);
    }
    if (m_fd >= 0) m_stop = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  }
  ~WindowChannel() { if (m_fd >= 0) close(m_fd); if (m_stop >= 0) close(m_stop); }
  bool enabled() const { return m_fd >= 0 && m_stop >= 0; }
  void stop() {
    if (m_stop < 0) return;
    uint64_t n = 1; ssize_t result;
    do { result = write(m_stop, &n, sizeof(n)); } while (result < 0 && errno == EINTR);
  }
  bool send(WindowWire &message) {
    ssize_t n;
    uint64_t words[WindowWire::Words]{};
    do {
      ++message.sends; message.pack(words);
      n = ::send(m_fd, words, sizeof(words), MSG_NOSIGNAL);
    } while (n < 0 && errno == EINTR);
    return n == sizeof(words);
  }
  bool receive(WindowWire &message) {
    pollfd fds[2]{{m_fd, POLLIN, 0}, {m_stop, POLLIN, 0}};
    int ready;
    do { ++m_polls; ready = poll(fds, 2, m_ms); } while (ready < 0 && errno == EINTR);
    if (ready <= 0 || fds[1].revents || !(fds[0].revents & POLLIN)) return false;
    ssize_t n;
    uint64_t words[WindowWire::Words]{};
    do { ++m_receives; n = recv(m_fd, words, sizeof(words), MSG_TRUNC); }
    while (n < 0 && errno == EINTR);
    return n == sizeof(words) && message.unpack(words);
  }
  uint64_t receives() const { return m_receives; }
  uint64_t polls() const { return m_polls; }
private:
  int m_fd = -1, m_stop = -1, m_ms;
  uint64_t m_receives = 0, m_polls = 0;
};

struct WindowRecord {
  ZuCSpan event{"process_window"}, engine, role;
  uint32_t pid = 0, error = 0, sessions = 0, overlappingWarmupSessions = 0;
  uint64_t token = 0, controlSends = 0, controlReceives = 0, controlPolls = 0;
  WindowSample start, end;
};
ZfStruct(, (WindowRecord, JSON), (event, , String), (engine, , String),
  (role, , String), (pid, , UInt32), (token, , UInt64), (error, , UInt32),
  (sessions, , UInt32), (overlappingWarmupSessions, , UInt32),
  (controlSends, , UInt64), (controlReceives, , UInt64), (controlPolls, , UInt64),
  (start, , UDT), (end, , UDT));
void outputWindow(ZuCSpan engine, ZuCSpan role, const WindowWire &record,
  unsigned sessions, unsigned overlappingWarmup);

class WindowServer : public ZmNHeap<WindowServer> {
friend inline ZuStringT<"Zfix.Load.Window.Server"> ZmNHeapID(WindowServer *);
public:
  explicit WindowServer(unsigned seconds) : m_channel{seconds} {
    if (m_channel.enabled()) m_worker = ZmThread{[this] { serve_(); }};
  }
  ~WindowServer() { m_channel.stop(); if (m_worker.tid() && !m_joined) m_worker.join(); }
  bool enabled() const { return m_channel.enabled(); }
  // Main only, after existing normal endpoint stop; no new application drain.
  bool finish(const char *engine, unsigned sessions) {
    if (!enabled()) return true;
    m_channel.stop();
    if (!m_worker.tid()) { m_record.error = 1; return false; }
    m_worker.join(); m_joined = true;
    m_record.receives = m_channel.receives(); m_record.polls = m_channel.polls();
    outputWindow(engine, "server", m_record, sessions, 0);
    return m_finished && !m_record.error && m_record.start.valid && m_record.end.valid;
  }
private:
  void serve_() {
    WindowWire request;
    if (!m_channel.receive(request) || request.command != WindowWire::Start) return;
    m_record.pid = getpid(); m_record.token = request.token;
    m_record.command = WindowWire::Start;
    m_record.start = WindowSample::capture(CLOCK_THREAD_CPUTIME_ID);
    if (!m_record.start.valid) m_record.error = 1;
    if (!m_channel.send(m_record)) return;
    if (!m_channel.receive(request) || request.command != WindowWire::End ||
	request.token != m_record.token) return;
    m_record.end = WindowSample::capture(CLOCK_THREAD_CPUTIME_ID);
    m_record.command = WindowWire::End;
    if (!m_record.end.valid) m_record.error = 1;
    if (!m_channel.send(m_record)) return;
    m_finished = true;
  }
  WindowChannel m_channel;
  ZmThread m_worker;
  WindowWire m_record;
  bool m_finished = false, m_joined = false;
};

// Actor-owned state is accessed only on native AppRx / QuickFIX scheduler1.
// The worker exchanges fixed metadata only; dispatcher posts callbacks back to
// that owner. The owner retains this object until worker join and posted drain.
class WindowClient : public ZmNHeap<WindowClient> {
friend inline ZuStringT<"Zfix.Load.Window.Client"> ZmNHeapID(WindowClient *);
public:
  using Fn = ZmFn<void()>;
  using Post = ZmFn<void(Fn)>;
  static WindowClient *active() { return m_active; }
  WindowClient(unsigned sessions, unsigned seconds) : m_channel{seconds}, m_sessions{sessions} {
    m_pending.length(sessions); m_warm.length(sessions);
    m_completed.length(sessions); m_registered.length(sessions);
    for (unsigned i = 0; i < sessions; ++i)
      m_warm[i] = m_completed[i] = m_registered[i] = false;
  }
  ~WindowClient() { shutdown(false); if (m_active == this) m_active = nullptr; }
  void shutdown(bool releaseOnOwner = true) {
    if (!m_worker.tid() || m_joined) return;
    m_job = Stop; m_request.post(); m_channel.stop(); m_worker.join(); m_joined = true;
    // This metadata release crosses the owner's already-required stop drain.
    // No callback can be posted by the helper after this join.
    if (releaseOnOwner && m_post) m_post(Fn{[this] { clearPending_(); }});
  }
  bool enabled() const { return m_channel.enabled(); }
  bool open() const { return m_open; }
  void configure(Post post, Fn fail) {
    if (!enabled()) return;
    m_post = ZuMv(post); m_fail = ZuMv(fail); m_active = this;
    m_worker = ZmThread{[this] { work_(); }};
    if (!m_worker.tid()) { m_error = true; return; }
    if (pthread_getcpuclockid(m_worker.context()->pthread(), &m_helperClock)) m_error = true;
  }
  void warmup(unsigned index, bool pending) { m_warm[index] = pending; }
  void begin(unsigned index, Fn resume) {
    if (m_error) { failed_(); return; }
    if (m_open) { resume(); return; }
    if (index >= m_sessions || m_registered[index]) { failed_(); return; }
    m_registered[index] = true; m_pending[index] = ZuMv(resume);
    if (m_started) return;
    m_started = true; m_job = Start; m_request.post();
  }
  void complete(unsigned index) {
    if (!enabled()) return;
    if (index >= m_sessions || m_completed[index] || !m_open) { failed_(); return; }
    m_completed[index] = true;
    if (++m_finished != m_sessions) return;
    m_record.end = WindowSample::capture(m_helperClock);
    if (!m_record.end.valid) m_error = true;
    m_job = End; m_request.post();
  }
  // Main only: call after existing waitComplete/done and BEFORE existing Logout.
  bool finish(const char *engine, ZuTime deadline) {
    if (!enabled()) return true;
    if (m_done.timedwait(deadline)) return false;
    auto record = m_record; // AppRx published this immutable completed record.
    record.sends = m_workerSends;
    record.receives = m_channel.receives(); record.polls = m_channel.polls();
    record.error = m_error ? 1 : 0;
    outputWindow(engine, "client", record, m_sessions, m_overlap);
    return !m_error && m_finished == m_sessions && m_record.start.valid && m_record.end.valid;
  }
private:
  enum { Start = 1, End = 2, Stop = 3 };
  void clearPending_() { for (auto &fn : m_pending) fn = {}; }
  void failed_() { m_error = true; clearPending_(); if (m_fail) m_fail(); }
  void opened_(uint64_t token, bool serverSampleValid) {
    if (m_error || !serverSampleValid) { failed_(); return; }
    m_record.pid = getpid(); m_record.token = token;
    m_record.start = WindowSample::capture(m_helperClock);
    if (!m_record.start.valid) { failed_(); return; }
    for (auto warm : m_warm) if (warm) ++m_overlap;
    m_open = true;
    for (auto &fn : m_pending) { auto next = ZuMv(fn); if (next) next(); }
  }
  void work_() {
    WindowWire request, remote; // worker-owned; only scalar reply control is posted.
    timespec now{}; clock_gettime(CLOCK_MONOTONIC, &now);
    request.token = (uint64_t(getpid())<<32) ^ ProcessPhaseResources::ns(now);
    while (true) {
      m_request.wait(); unsigned job = m_job.load_();
      if (job == Stop) return;
      request.command = job == Start ? WindowWire::Start : WindowWire::End;
      if (!m_channel.send(request) || !m_channel.receive(remote) ||
	  remote.command != request.command || remote.token != request.token ||
	  remote.error) {
	m_error = true;
	m_post(Fn{[this] { failed_(); }}); m_done.post(); return;
      }
      if (job == Start) {
        auto token = remote.token; bool valid = remote.start.valid;
        m_post(Fn{[this, token, valid] { opened_(token, valid); }});
      } else { m_workerSends = request.sends; m_done.post(); }
    }
  }
  inline static WindowClient *m_active = nullptr;
  WindowChannel m_channel;
  const unsigned m_sessions;
  Rows<Fn> m_pending;
  Rows<bool> m_warm, m_completed, m_registered;
  Post m_post;
  Fn m_fail;
  ZmThread m_worker;
  ZmSemaphore m_request, m_done;
  ZmAtomic<unsigned> m_job{0};
  ZmAtomic<unsigned> m_error{0};
  WindowWire m_record; // AppRx-owned until complete, then published to main.
  uint64_t m_workerSends = 0; // worker publishes through m_done before main reads.
  clockid_t m_helperClock = -1;
  unsigned m_finished = 0, m_overlap = 0;
  bool m_open = false, m_started = false, m_joined = false;
};
} // ZfixLoad
#endif // !_WIN32
#endif
