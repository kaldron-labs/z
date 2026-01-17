//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zcmd locally hosted commands

#ifndef ZcmdHost_HH
#define ZcmdHost_HH

#ifndef ZcmdLib_HH
#include <zlib/ZcmdLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmRBTree.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiFile.hh>

#include <zlib/ZrlTypes.hh>

#include <zlib/ZvCf.hh>

#include <zlib/Zcmd.hh>

namespace Ztls { class Random; }

namespace Zcmd {

class Dispatcher;

// command output sizes are highy unpredictable, some are a single short line
// while others can produce megabytes; this uses IO buffer default, i.e. the
// payload size of a single Ethernet packet (~1.5K)
using OutBufAlloc = ZiIOBufAlloc<>;

// can be thrown by command function
struct Usage { };

using Argv = ZuVArray<ZuSpan<char>>;		// argument array

template <template <typename> class Context_>
class Host {
public:
  ZuDerive(Context, Context_<Host>);

  // command handler (context)
  using Fn = ZmFn<void(Context *, ZiIOBuf *, const Argv &)>;

  struct CmdData {
    Fn		fn;
    ZuCSpan	brief;
    ZuCSpan	usage;
  };

  void init();
  void final();

  bool hasCmd(ZuCSpan name);

  void addCmd(ZuCSpan name, Fn fn, ZuCSpan brief, ZuCSpan usage);

  const CmdData *findCmd(ZuCSpan name) const {
    auto node = m_cmds.find(name);
    if (ZuUnlikely(!node)) return nullptr;
    return &node->val();
  }

  unsigned nCmds() const { return m_cmds.count_(); }

  template <typename L>
  void allCmds(L &&l) const {
    auto i = m_cmds.citer();
    while (auto node = i()) { l(node->key(), node->val()); }
  }

  void processCmd(ZmRef<Context>, const Argv &argv);

  void finalFn(ZmFn<>);

  virtual void executed(
    ZmRef<Context>, ZmRef<ZiIOBuf> buf, ZuBSpan out, int code) = 0;

  virtual Dispatcher *dispatcher() { return nullptr; }

  virtual void target(ZuCSpan) { }
  virtual Zrl::Passwd getpass(ZuCSpan prompt, unsigned passLen) { return {}; }

  virtual Ztls::Random *rng() { return nullptr; }

private:
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

  ZuDerive(Cmds,
    (ZmRBTreeKV<ZuCSpan, CmdData,
      ZmRBTreeUnique<true,
	ZmRBTreeLock<ZmNoLock>>>));

  Cmds		 	m_cmds;
  ZtArray<ZmFn<>>	m_finalFn;
};

template <typename ContextRef>
inline void executed(ContextRef ctx, ZmRef<ZiIOBuf> out, int code)
{
  auto host = ctx->host;
  auto span = out->cspan();
  host->executed(ZuMv(ctx), ZuMv(out), span, code);
}
template <typename ContextRef>
inline void executed(ContextRef ctx, ZmRef<ZiIOBuf> buf, ZuBSpan out, int code)
{
  auto host = ctx->host;
  host->executed(ZuMv(ctx), ZuMv(buf), out, code);
}

} // Zcmd

// FIXME
// - may want to get rid of server-side commands entirely
// - the Zcmd protocol is extensible via the ZuID type mechanism and Dispatcher

// loadable module must export void Zcmd_plugin(Zcmd::Host *)
extern "C" {
  typedef void (*ZcmdInitFn)(Zcmd::Host *host);
}

#endif /* ZcmdHost_HH */
