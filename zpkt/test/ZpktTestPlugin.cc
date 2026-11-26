//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Raw AF_XDP arena, pool and packet facilities.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <zlib/ZmHeap.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZpktEnv.hh>
#include <zlib/ZpktXDPCf.hh>
#include "ZpktTestPlugin.hh"

namespace Zpkt::Xdp {
ZfStructImpl(NifCf, Cf);
ZfStructImpl(PluginCf, Cf);
}

namespace ZpktTest {

static Stats stats;

class Nif final : public ZmNHeap<Nif>, public Zpkt::Nif {
friend inline ZuStringT<"Zpkt.Test.Nif"> ZmNHeapID(Nif *);
public:
  using ZmNHeap<Nif>::operator new;
  using ZmNHeap<Nif>::operator delete;
  Nif(ZiMultiplex *mx, const Zpkt::NifInfo &info) : Zpkt::Nif{mx, info} { }
  ~Nif() override { ++stats.destroys; }
  ZmRef<Zi::IOBufPool> bufPool(Zi::IOBufType::T, unsigned, unsigned,
    unsigned, unsigned, unsigned) override { return nullptr; }
  void reserveTxPools(ZuSpan<Zi::IOBufPool *const>) override { }
  ZuUnion<void, ZeException> alloc(Zi::IOBufPool *, const Zi::IOBufPoolList &) override {
    return {};
  }
  void final() override {
    if (env()) {
      ++stats.finalAttempts;
      if (stats.failFinals) {
	--stats.failFinals;
	error(ZeEXCEPT(Error, "ZpktTest", "injected child finalization failure"));
	return;
      }
      ++stats.finals;
      stats.lastChildFinal = ++stats.events;
      final_();
    }
    error({});
    resetInfo();
  }
  bool allocated() const override { return false; }
  bool ready() const override { return running(); }
  bool txReady(uint8_t *) const override { return false; }
  bool send(uint8_t *, unsigned) override { return false; }
  unsigned send(ZuSpan<const Zpkt::TxPacket>) override { return 0; }
  void rx() override { }
  unsigned pollTx() override { return 0; }
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override { return 0; }
  void status() override { }
private:
  void start_() override { Engine::started(true); }
  void stop_() override { clearCallbacks(); Engine::stopped(true); }
};

class Plugin final : public ZmNHeap<Plugin>, public Zpkt::Plugin {
friend inline ZuStringT<"Zpkt.Test.Plugin"> ZmNHeapID(Plugin *);
public:
  using ZmNHeap<Plugin>::operator new;
  using ZmNHeap<Plugin>::operator delete;
  Plugin(const ZfCf::AnyNode *cf) {
#ifdef ZPKT_TEST_INIT_FAIL
    throw ZeEXCEPT(Error, "ZpktTest", "injected plugin construction/EAL initialization failure");
#endif
    ZfCf::handler<Zpkt::Xdp::PluginCf>(cf).update(m_cf);
  }
  ~Plugin() override { ++stats.pluginDestroys; }
  ZuUnion<Zpkt::Nif *, ZeException> nif(
    ZiMultiplex *mx, ZuCSpan name, const Zpkt::NifInfo &info) override
  {
    ++stats.creates;
    auto node = m_cf.interfaces->find(name);
    if (!node) return ZeEXCEPT(Error, "ZpktTest", "unknown configured interface");
    stats.rxSpin = node->val().rxSpin;
    stats.txSpin = node->val().txSpin;
    stats.info = info;
    if (!stats.rxSpin) return ZeEXCEPT(Error, "ZpktTest", "injected device construction failure");
    return static_cast<Zpkt::Nif *>(new Nif{mx, info});
  }
  ZuUnion<void, ZeException> final() override {
    if (!m_final) {
      ++stats.pluginFinals;
      stats.pluginFinalOrder = ++stats.events;
      m_final = true;
    }
    return {};
  }
private:
  Zpkt::Xdp::PluginCf m_cf;
  bool m_final = false;
};

} // ZpktTest

extern "C" ZuExport_API ZpktTest::Stats *ZpktTestStats() { return &ZpktTest::stats; }
extern "C" ZuExport_API Zpkt::Plugin *ZpktPlugin(const ZfCf::AnyNode *cf)
{
#ifdef ZPKT_TEST_NULL
  return nullptr;
#elif defined(ZPKT_TEST_THROW)
  throw ZeEXCEPT(Error, "ZpktTest", "injected entry-point failure");
#else
  return new ZpktTest::Plugin{cf};
#endif
}
