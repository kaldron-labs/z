//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Shared mlx5 contexts owned by one plugin instance.

#ifndef ZpktMlxEnv_HH
#define ZpktMlxEnv_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
#endif

#include <infiniband/verbs.h>

#include <zlib/ZuUnion.hh>
#include <zlib/ZePlatform.hh>
#include <zlib/ZtArray.hh>

namespace Zpkt::Mlx {

class Env {
public:
  Env() = default;
  Env(const Env &) = delete;
  Env &operator =(const Env &) = delete;
  ~Env();

  ZuUnion<ibv_context *, ZeException> context(unsigned deviceIndex);
  ZuUnion<void, ZeException> final();

private:
  struct Context {
    ibv_context	*verbs;
    int		index;
    uint64_t	guid;
  };
  ZtArray<Context, ZtArrayHeapID<"Zpkt.Mlx.Contexts">> m_contexts;
};

} // namespace Zpkt::Mlx
#endif /* ZpktMlxEnv_HH */
