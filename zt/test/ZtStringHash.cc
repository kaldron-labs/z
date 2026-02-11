//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string_view>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmHash.hh>
#include <zlib/ZtString.hh>

using namespace ZuTestUtil;

namespace {

constexpr const char *Words[] = {
  "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta",
  "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi", "rho",
  "sigma", "tau", "upsilon", "phi", "chi", "psi", "omega"
};

void runCase(bool high)
{
  ZuTestScope(runCase);

  int count[1024] = { 0 };
  int count2[1024] = { 0 };
  unsigned total = 0;

  for (auto word: Words) {
    ZtString<> s = word;
    uint32_t hash = s.hash();
    hash = high ? ZmHashBits(hash, 10) : (hash & 1023);

    uint32_t hash2 = std::hash<std::basic_string_view<char>>{}(
      std::basic_string_view<char>(s.data(), s.length()));
    hash2 = high ? ZmHashBits(hash2, 10) : (hash2 & 1023);

    count[hash]++;
    count2[hash2]++;
    total++;
  }

  int sum1 = 0, sum2 = 0, used1 = 0, used2 = 0;
  for (unsigned i = 0; i < 1024; i++) {
    sum1 += count[i];
    sum2 += count2[i];
    if (count[i]) used1++;
    if (count2[i]) used2++;
  }

  ZuCheck(sum1 == int(total));
  ZuCheck(sum2 == int(total));
  ZuCheck(used1 > 1);
  ZuCheck(used2 > 1);
  log("hash mode=", high ? "high" : "low", " used=", used1, '/', used2);
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall_("high-bit distribution", runCase, true);
  ZuTestCall_("low-bit distribution", runCase, false);
  return 0;
}
