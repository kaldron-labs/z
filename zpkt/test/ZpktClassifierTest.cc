//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <linux/if_link.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZpktBPF.hh>

using namespace ZuTestUtil;

static void testEmission()
{
  ZuTestScope(testEmission);
  ZuCHECK(Zpkt::Xdp::redirectCode(-1).is<ZeException>(),
    "invalid map FD cannot publish partial instructions");
  auto emitted = Zpkt::Xdp::redirectCode(37);
  if (emitted.is<ZeException>()) {
    ZuCHECK(false, "valid redirect emits complete instructions");
    return;
  }
  const auto &code = emitted.p<Zpkt::Xdp::BPFCode>();
  bool valid = true;
  unsigned maps = 0, redirects = 0;
  auto n = code.length();
  for (unsigned i = 0; i < n; ++i) {
    auto insn = code[i];
    if (insn.code == (BPF_LD | BPF_DW | BPF_IMM)) {
      valid &= insn.src_reg == BPF_PSEUDO_MAP_FD && insn.imm == 37 &&
        i + 1 < n && !code[i + 1].code;
      ++maps;
      ++i;
    } else if (BPF_CLASS(insn.code) == BPF_JMP) {
      if (BPF_OP(insn.code) == BPF_CALL)
        redirects += insn.imm == BPF_FUNC_redirect_map;
      else valid &= BPF_OP(insn.code) == BPF_EXIT;
    }
  }
  ZuCHECK(valid && maps == 1 && redirects == 1,
    "one XSK map redirect with no conditional branches");
  ZuCHECK(Zpkt::Xdp::setXDP(1, 37, XDP_FLAGS_DRV_MODE, -1).is<ZeException>() &&
      Zpkt::Xdp::setXDP(1, -1, XDP_FLAGS_DRV_MODE | XDP_FLAGS_REPLACE, -1).is<ZeException>(),
    "unprotected attachment changes rejected before kernel mutation");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testEmission);
}
