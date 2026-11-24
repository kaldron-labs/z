//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuLib.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/Zpkt.hh>
#include <zlib/ZpktBPF.hh>

#include "zpktfixture.hh"

using namespace ZuTestUtil;

static unsigned action(int fd, const void *bytes, unsigned length)
{
  bpf_attr attr{};
  attr.test.prog_fd = fd;
  attr.test.data_in = uintptr_t(bytes);
  attr.test.data_size_in = length;
  attr.test.repeat = 1;
  return syscall(__NR_bpf, BPF_PROG_TEST_RUN, &attr, sizeof(attr)) < 0 ?
    unsigned(-1) : attr.test.retval;
}

static void testKernelRedirect()
{
  ZuTestScopeRT(testKernelRedirect);
  Zpkt::Xdp::BPFProgram program;
  auto loaded = program.load(2);
  ZeException error;
  if (loaded.is<ZeException>()) error = loaded.p<ZeException>();
  ZuCheckRT(loaded.is<void>(), log_("kernel verifier accepts redirect program: ", error));
  if (!loaded.is<void>()) return;
  // An empty XSK map drops every frame. Packet tests exercise live redirects.
  uint8_t frame[64]{};
  const unsigned types[] = {0x0800, 0x86dd, 0x0806, 0x88b5};
  for (auto type : types) {
    frame[12] = type>>8;
    frame[13] = type;
    ZuCheckRT(action(program.fd(), frame, sizeof(frame)) == XDP_DROP,
      log_("redirect selects EtherType ", type));
  }
}

static void testForeign()
{
  ZuTestScopeRT(testForeign);
  auto ifindex = unsigned(strtoul(getenv("ZPKT_TEST_IFINDEX"), nullptr, 10));
  Zpkt::Xdp::BPFProgram foreign;
  Zpkt::Xdp::BPFAttach attachment;
  ZeException error;
  auto attachedResult = foreign.load(2);
  if (attachedResult.is<void>()) attachedResult = attachment.attach(ifindex, foreign);
  bool attached = attachedResult.is<void>();
  if (!attached) error = attachedResult.p<ZeException>();
  ZuCheckRT(attached, log_("fixture attaches an independent owner: ", error));
  if (!attached) return;
  ZpktTest::Environment env;
  ZmRef<Zpkt::Nif> instance;
  auto rx = ZpktTest::pool(Zi::IOBufType::Rx, 8, 1514);
  auto tx = ZpktTest::pool(Zi::IOBufType::TxTCP, 1, 128);
  ZpktTest::Interface config;
  auto &params = config;
  params.ifindex = ifindex;
  params.txSid = 3;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.thread(3, [](auto &t) { t.isolated(true); });
  })};
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  auto initialized = env.open(instance, &mx, config);
  auto allocated = initialized.is<void>() ? instance->alloc(rx, pools) : initialized;
  bool started = allocated.is<void>() && mx.start();
  bool activated = started && instance->start([](uint8_t *, unsigned) { });
  error = allocated.is<ZeException>() ? allocated.p<ZeException>() : instance->error();
  auto link = Zpkt::Xdp::xdpQuery(ifindex);
  if (link.is<ZeException>()) error = link.p<ZeException>();
  ZuCheckRT(started && !activated && !instance->ready() && instance->allocated() &&
      link.is<Zpkt::Xdp::XDPInfo>() &&
      (link.p<Zpkt::Xdp::XDPInfo>().drvID == foreign.id() ||
	link.p<Zpkt::Xdp::XDPInfo>().skbID == foreign.id()),
    log_("creation refuses an occupied interface and preserves its program"));
  if (instance) {
    ZuCheckRT(instance->stop());
    instance->final();
    instance = nullptr;
  }
  if (started) ZuCheckRT(mx.stop());
  auto detached = attachment.detach();
  if (detached.is<ZeException>()) error = detached.p<ZeException>();
  ZuCheckRT(detached.is<void>(), log_("foreign owner retires its own attachment: ", error));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZPKT_TEST_IFINDEX")) {
    std::cout << "1..0 # SKIP requires privileged fixture-owned interface\n";
    return 0;
  }
  ZuTestMain();
  ZuTestCall(testKernelRedirect);
  ZuTestCall(testForeign);
}
