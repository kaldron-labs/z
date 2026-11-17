//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <arpa/inet.h>
#include <linux/if_link.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZxdpBPF.hh>

using namespace ZuTestUtil;

static void testMasks()
{
  ZuTestScope(testMasks);
  auto absent = Zxdp::Xdp::parseFilter({});
  ZuCHECK(absent.is<Zxdp::Xdp::Filter>() && !absent.p<Zxdp::Xdp::Filter>().family,
    "absent mask selects all interface traffic");
  auto v4 = Zxdp::Xdp::parseFilter("192.0.2.7/24");
  ZuCHECK(v4.is<Zxdp::Xdp::Filter>() && v4.p<Zxdp::Xdp::Filter>().family == 4 &&
      v4.p<Zxdp::Xdp::Filter>().address[0] == htonl(0xc0000200) &&
      v4.p<Zxdp::Xdp::Filter>().mask[0] == htonl(0xffffff00),
    "IPv4 address is normalized once to its network");
  auto v6 = Zxdp::Xdp::parseFilter("2001:db8:1234::7/48");
  ZuCHECK(v6.is<Zxdp::Xdp::Filter>() && v6.p<Zxdp::Xdp::Filter>().family == 6 &&
      v6.p<Zxdp::Xdp::Filter>().address[0] == htonl(0x20010db8) &&
      v6.p<Zxdp::Xdp::Filter>().address[1] == htonl(0x12340000) &&
      v6.p<Zxdp::Xdp::Filter>().mask[1] == htonl(0xffff0000) &&
      !v6.p<Zxdp::Xdp::Filter>().mask[2], "IPv6 partial-word mask");
  ZuCHECK(Zxdp::Xdp::parseFilter("192.0.2.1/33").is<ZeException>() &&
      Zxdp::Xdp::parseFilter("::1/129").is<ZeException>() &&
      Zxdp::Xdp::parseFilter("192.0.2.1/").is<ZeException>(),
    "invalid mask fails during setup");
}

static void testEmission()
{
  ZuTestScope(testEmission);
  ZuCHECK(Zxdp::Xdp::classifierCode(-1, {}).is<ZeException>(),
    "invalid map FD cannot publish partial instructions");
  const char *masks[] = {"", "192.0.2.7/24", "2001:db8::/32"};
  for (auto mask : masks) {
    auto parsed = Zxdp::Xdp::parseFilter(mask);
    if (parsed.is<ZeException>()) {
      ZuCHECK(false, "valid mask parses before instruction emission");
      continue;
    }
    auto emitted = Zxdp::Xdp::classifierCode(37, parsed.p<Zxdp::Xdp::Filter>());
    if (emitted.is<ZeException>()) {
      ZuCHECK(false, "valid classifier emits complete instructions");
      continue;
    }
    const auto &code = emitted.p<Zxdp::Xdp::BPFCode>();
    bool valid = true;
    unsigned maps = 0;
    auto n = code.length();
    for (unsigned i = 0; i < n; ++i) {
      auto insn = code[i];
      if (insn.code == (BPF_LD | BPF_DW | BPF_IMM)) {
        valid &= insn.src_reg == BPF_PSEUDO_MAP_FD && insn.imm == 37 &&
          i + 1 < n && !code[i + 1].code;
        ++maps;
        ++i;
      } else if (BPF_CLASS(insn.code) == BPF_JMP &&
          BPF_OP(insn.code) != BPF_CALL && BPF_OP(insn.code) != BPF_EXIT)
        valid &= insn.off >= 0 && i + 1 + insn.off < n;
    }
    ZuCHECK(valid && maps == 1, "one XSK map and bounded forward branches");
  }
  ZuCHECK(Zxdp::Xdp::setXDP(1, 37, XDP_FLAGS_DRV_MODE, -1).is<ZeException>() &&
      Zxdp::Xdp::setXDP(1, -1, XDP_FLAGS_DRV_MODE | XDP_FLAGS_REPLACE, -1).is<ZeException>(),
    "unprotected attachment changes rejected before kernel mutation");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testMasks);
  ZuTestCall(testEmission);
}
