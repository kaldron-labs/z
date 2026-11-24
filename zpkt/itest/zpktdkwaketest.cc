//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <dlfcn.h>
#include <limits.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <iostream>

#include <rte_mbuf.h>

#include <zlib/ZuTestUtil.hh>

#include "zpktdkfixture.hh"

using namespace ZuTestUtil;
using namespace ZpktDpdkTest;

// Observe and synchronously gate the actual wait syscall, without synthesizing
// readiness or depending on a wall clock's scheduler granularity.
struct WaitProbe {
  ZmAtomic<int>		fd{-1};
  ZmAtomic<unsigned>	calls{0}, writes{0}, minNS{UINT_MAX}, maxNS{0}, gateAt{0};
  ZmSemaphore		entered, release;
};

static ZmAtomic<WaitProbe *> waiting{nullptr};

extern "C" __attribute__((visibility("default")))
int eventfd(unsigned value, int flags) noexcept
{
  int fd = syscall(SYS_eventfd2, value, flags);
  if (auto probe = waiting.load_(); probe && fd >= 0) probe->fd.cmpXch(fd, -1);
  return fd;
}

extern "C" __attribute__((visibility("default")))
ssize_t write(int fd, const void *data, size_t size)
{
  if (auto probe = waiting.load_(); probe && fd == probe->fd) ++probe->writes;
  return syscall(SYS_write, fd, data, size);
}

extern "C" __attribute__((visibility("default")))
int ppoll(pollfd *fds, nfds_t count, const timespec *timeout, const sigset_t *mask)
{
  static const auto poll = reinterpret_cast<decltype(&ppoll)>(dlsym(RTLD_NEXT, "ppoll"));
  if (auto probe = waiting.load_(); probe && count && fds[0].fd == probe->fd) {
    if (timeout) {
      unsigned ns = unsigned(timeout->tv_sec * 1000000000L + timeout->tv_nsec);
      probe->minNS.minimum(ns);
      probe->maxNS.maximum(ns);
    }
    if (++probe->calls == probe->gateAt) {
      probe->entered.post();
      probe->release.wait();
    }
  }
  return poll(fds, count, timeout, mask);
}

struct ObserveWait {
  WaitProbe probe;
  ObserveWait() { waiting = &probe; }
  ~ObserveWait() { waiting = nullptr; }
};

struct Receive {
  Device		*endpoint;
  unsigned		count = 0, wakeAt = 0, gateAt = 0;
  uint8_t		*held[2]{};
  bool			retain = false;
  ZmSemaphore		entered, release, done;

  void receive(uint8_t *packet, unsigned) {
    ++count;
    if (retain && count <= 2)
      held[count - 1] = packet;
    else
      endpoint->ingress->free(packet);
    if (count == gateAt) {
      entered.post();
      release.wait();
    }
    if (count == wakeAt) endpoint->device->wake();
  }

  void run() {
    endpoint->mx.rxRun([this] {
      endpoint->device->rx();
      done.post();
    });
  }
};

static void testWait(const Runtime &runtime)
{
  ZuTestScopeRT(testWait);
  RingPort port;
  auto result = port.init("wait", 128, {{0x02, 0, 0, 0, 0, 1}});
  ZuCheckRT(result.is<void>(), log_(errorText(result.p<ZeException>())));
  if (!result.is<void>()) return;
  ObserveWait observer;
  auto &probe = observer.probe;
  Device endpoint;
  constexpr unsigned Interval = 37;
  result = endpoint.init(runtime, port.name(), 128);
  ZuCheckRT(result.is<void>() && probe.fd >= 0, log_("allocate device with a real wake eventfd"));
  if (!result.is<void>() || probe.fd < 0) return;
  Receive rx{.endpoint = &endpoint};
  result = endpoint.start([&rx](uint8_t *p, unsigned n) { rx.receive(p, n); });
  ZuCheckRT(result.is<void>(), log_(errorText(result.p<ZeException>())));
  if (!result.is<void>()) return;

  uint8_t frame[64]{};
  frame[12] = 0x88;
  frame[13] = 0xb5;
  auto ingress = native(endpoint.ingress);
  endpoint.device->wake();
  endpoint.device->wake();
  bool injected = port.inject(ingress, frame);
  rx.wakeAt = 1;
  rx.run();
  rx.done.wait();
  rx.run();
  rx.done.wait();
  ZuCheckRT(injected && !rx.count && !probe.calls,
    log_("two preposted pulses each return an Rx call before servicing queued traffic"));
  if (injected) {
    rx.run();
    rx.done.wait();
  }
  ZuCheckRT(rx.count == 1, log_("the packet remains queued after the preposted wakes"));

  auto before = probe.calls.load_();
  probe.gateAt = before + 8;
  rx.wakeAt = rx.count + 1;
  rx.run();
  probe.entered.wait();
  auto minNS = probe.minNS.load_(), maxNS = probe.maxNS.load_();
  injected = port.inject(ingress, frame);
  if (!injected) endpoint.device->wake();
  probe.release.post();
  rx.done.wait();
  ZuCheckRT(injected && minNS && minNS < maxNS && maxNS == Interval * 1000 &&
      rx.count == rx.wakeAt,
    log_("idle backs off to the configured microseconds and probes packets after timeout"));

  probe.gateAt = probe.calls.load_() + 1;
  rx.run();
  probe.entered.wait();
  endpoint.device->wake();
  endpoint.device->wake();
  probe.release.post();
  rx.done.wait();
  before = probe.calls.load_();
  rx.wakeAt = rx.count + 1;
  injected = port.inject(ingress, frame);
  rx.run();
  rx.done.wait();
  ZuCheckRT(injected && rx.count + 1 == rx.wakeAt && probe.calls == before,
    log_("a concurrent pulse at wait entry remains counted after the first return"));
  if (injected) {
    rx.run();
    rx.done.wait();
  }

  rx.count = 0;
  rx.retain = true;
  rx.wakeAt = 2;
  bool held = port.inject(ingress, frame) && port.inject(ingress, frame);
  if (held) {
    rx.run();
    rx.done.wait();
    probe.gateAt = probe.calls.load_() + 1;
    rx.run();
    probe.entered.wait();
    before = probe.writes.load_();
    endpoint.ingress->free(rx.held[0]);
    endpoint.ingress->free(rx.held[1]);
    ZuCheckRT(probe.writes == before + 1,
    log_("off-owner returns coalesce one internal eventfd wake during a park"));
    endpoint.device->wake();
    probe.release.post();
    rx.done.wait();
  }
  ZuCheckRT(held, log_("both retained ingress frames were produced"));
  rx.retain = false;

  constexpr unsigned Burst = 80;
  unsigned offered = 0;
  while (offered < Burst && port.inject(ingress, frame)) ++offered;
  ZuCheckRT(offered == Burst, log_("preload multiple complete Rx bursts"));
  if (offered) {
    before = probe.calls.load_();
    rx.count = 0;
    rx.gateAt = 1;
    rx.wakeAt = 0;
    rx.run();
    rx.entered.wait();
    endpoint.device->wake();
    rx.release.post();
    rx.done.wait();
    ZuCheckRT(rx.count && rx.count < offered && probe.calls == before,
    log_("a pulse during sustained ingress interrupts between bounded receive batches"));
    rx.gateAt = 0;
    rx.wakeAt = offered;
    if (rx.count < offered) {
      rx.run();
      rx.done.wait();
    }
    ZuCheckRT(rx.count == offered && probe.calls == before,
    log_("remaining sustained traffic resumes immediately without parking"));
  }
  ZuCheckRT(endpoint.stop(), log_("idle stop does not depend on arrival of another packet"));
}

static void testPoll(const Runtime &runtime)
{
  ZuTestScopeRT(testPoll);
  RingPort port;
  auto result = port.init("poll", 8, {{0x02, 0, 0, 0, 0, 2}});
  ZuCheckRT(result.is<void>(), log_(errorText(result.p<ZeException>())));
  if (!result.is<void>()) return;
  ObserveWait observer;
  Device endpoint;
  result = endpoint.init(runtime, port.name());
  ZuCheckRT(result.is<void>(), log_(errorText(result.p<ZeException>())));
  if (!result.is<void>()) return;
  Receive rx{.endpoint = &endpoint, .wakeAt = 1};
  result = endpoint.start([&rx](uint8_t *p, unsigned n) { rx.receive(p, n); });
  ZuCheckRT(result.is<void>(), log_(errorText(result.p<ZeException>())));
  if (!result.is<void>()) return;

  ZmSemaphore entered;
  endpoint.mx.rxRun([&endpoint, &entered, &rx] {
    entered.post();
    endpoint.device->rx();
    rx.done.post();
  });
  entered.wait();
  uint8_t frame[64]{};
  bool injected = port.inject(native(endpoint.ingress), frame);
  if (!injected) endpoint.device->wake();
  rx.done.wait();
  ZuCheckRT(injected && rx.count == 1 && observer.probe.fd >= 0 && !observer.probe.calls,
    log_("zero interval serves packets without calling the idle wait"));
  ZuCheckRT(endpoint.stop(), log_("stop polling device"));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid()) {
    std::cout << "1..0 # SKIP requires private dummy kernel Nif fixture privileges\n";
    return 0;
  }
  ZuTestMain();
  alarm(25);
  Runtime runtime;
  auto result = runtime.init();
  ZuCHECK(result.is<void>(), errorText(result.p<ZeException>()));
  if (!result.is<void>()) return 1;
  ZuTestCall(testWait, runtime);
  ZuTestCall(testPoll, runtime);
}
