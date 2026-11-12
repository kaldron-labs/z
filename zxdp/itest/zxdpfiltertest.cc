//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <arpa/inet.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/Zxdp.hh>
#include <zlib/ZxdpBPF.hh>

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

static void testKernelMasks()
{
  ZuTestScopeRT(testKernelMasks);
  ZtString<> error;
  // Empty XSK map returns DROP for a selected packet and PASS for unmatched
  // traffic. Real socket redirection is exercised separately by packet tests.
  struct Case { const char *mask, *match, *other; unsigned family; } cases[] = {
    {"192.0.2.7/24", "192.0.2.99", "192.0.3.99", 4},
    {"2001:db8:1234::7/48", "2001:db8:1234::99", "2001:db8:1235::99", 6},
    {"128.0.0.1/1", "192.0.2.1", "127.0.0.1", 4}
  };
  for (auto test : cases) {
    Zxdp::Filter filter;
    Zxdp::BPFProgram program;
    bool loaded = Zxdp::parseFilter(test.mask, filter, error) && program.load(2, filter, error);
    ZuCheckRT(loaded, log_("kernel verifier accepts network mask ", test.mask, ": ", error));
    if (!loaded) continue;
    uint8_t bytes[96]{}; // Ethernet plus either IP header, including two VLAN tags
    for (unsigned tags = 0; tags <= 2; ++tags) {
      auto ip = 14 + 4 * tags;
      for (unsigned i = 0; i < tags; ++i) {
        bytes[12 + 4 * i] = 0x81;
        bytes[13 + 4 * i] = 0x00;
      }
      bytes[ip - 2] = test.family == 4 ? 0x08 : 0x86;
      bytes[ip - 1] = test.family == 4 ? 0x00 : 0xdd;
      bytes[ip] = test.family == 4 ? 0x45 : 0x60;
      auto dest = bytes + ip + (test.family == 4 ? 16 : 24);
      inet_pton(test.family == 4 ? AF_INET : AF_INET6, test.match, dest);
      bool matched = action(program.fd(), bytes, sizeof(bytes)) == XDP_DROP;
      inet_pton(test.family == 4 ? AF_INET : AF_INET6, test.other, dest);
      ZuCheckRT(matched && action(program.fd(), bytes, sizeof(bytes)) == XDP_PASS &&
          action(program.fd(), bytes, ip + (test.family == 4 ? 19 : 39)) == XDP_PASS,
        log_("kernel mask distinguishes destination and bounds with VLAN depth ", tags));
    }
  }
  Zxdp::BPFProgram program;
  ZuCheckRT(program.load(2, {}, error), log_("kernel accepts unmasked classifier: ", error));
  uint8_t frame[64]{};
  frame[12] = 0x88;
  frame[13] = 0xb5;
  ZuCheckRT(action(program.fd(), frame, sizeof(frame)) == XDP_DROP,
    log_("absent mask selects non-IP traffic as well"));
}

static void testForeign()
{
  ZuTestScopeRT(testForeign);
  auto ifindex = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10));
  Zxdp::BPFProgram foreign;
  Zxdp::BPFAttach attachment;
  ZtString<> error;
  bool attached = foreign.load(2, {}, error) && attachment.attach(ifindex, foreign, error);
  ZuCheckRT(attached, log_("fixture attaches an independent owner: ", error));
  if (!attached) return;
  Zxdp::BufPool rx{8, 1514};
  auto instance = Zxdp::Instance::create({.interface = ifindex, .rxPool = &rx,
    .rx = [](uint8_t *, unsigned) { }}, error);
  Zxdp::LinkState link;
  ZuCheckRT(!instance && !rx.ready() && Zxdp::queryLink(ifindex, link, error) &&
      (link.drvID == foreign.id() || link.skbID == foreign.id()),
    log_("creation refuses an occupied interface and preserves its program"));
  ZuCheckRT(attachment.detach(error), log_("foreign owner retires its own attachment: ", error));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZXDP_TEST_IFINDEX")) {
    std::cout << "1..0 # SKIP requires privileged fixture-owned interface\n";
    return 0;
  }
  ZuTestMain();
  ZuTestCall(testKernelMasks);
  ZuTestCall(testForeign);
}
