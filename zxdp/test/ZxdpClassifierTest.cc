//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <arpa/inet.h>
#include <linux/if_link.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZxdpBPF.hh>

using namespace ZuTestUtil;

static void testMasks()
{
  ZuTestScope(testMasks);
  Zxdp::Filter filter;
  ZtString<> error;
  ZuCHECK(Zxdp::parseFilter({}, filter, error) && !filter.family,
    "absent mask selects all interface traffic");
  ZuCHECK(Zxdp::parseFilter("192.0.2.7/24", filter, error) && filter.family == 4 &&
      filter.address[0] == htonl(0xc0000200) && filter.mask[0] == htonl(0xffffff00),
    "IPv4 address is normalized once to its network");
  ZuCHECK(Zxdp::parseFilter("2001:db8:1234::7/48", filter, error) && filter.family == 6 &&
      filter.address[0] == htonl(0x20010db8) && filter.address[1] == htonl(0x12340000) &&
      filter.mask[1] == htonl(0xffff0000) && !filter.mask[2], "IPv6 partial-word mask");
  ZuCHECK(!Zxdp::parseFilter("192.0.2.1/33", filter, error) &&
      !Zxdp::parseFilter("::1/129", filter, error) &&
      !Zxdp::parseFilter("192.0.2.1/", filter, error), "invalid mask fails during setup");
}

static void testEmission()
{
  ZuTestScope(testEmission);
  Zxdp::BPFCode code;
  Zxdp::Filter filter;
  ZtString<> error;
  ZuCHECK(!Zxdp::classifierCode(-1, filter, code, error) && !code.length(),
    "invalid map FD cannot publish partial instructions");
  const char *masks[] = {"", "192.0.2.7/24", "2001:db8::/32"};
  for (auto mask : masks) {
    bool valid = Zxdp::parseFilter(mask, filter, error) &&
      Zxdp::classifierCode(37, filter, code, error);
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
  ZuCHECK(!Zxdp::setXDP(1, 37, XDP_FLAGS_DRV_MODE, -1, error) &&
      !Zxdp::setXDP(1, -1, XDP_FLAGS_DRV_MODE | XDP_FLAGS_REPLACE, -1, error),
    "unprotected attachment changes rejected before kernel mutation");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testMasks);
  ZuTestCall(testEmission);
}
