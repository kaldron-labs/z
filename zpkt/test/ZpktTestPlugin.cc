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
ZfStructImpl(InterfaceCf, Cf);
ZfStructImpl(PluginCf, Cf);
}

namespace ZpktTest {

static Stats stats;

class Nif final : public ZmNHeap<Nif>, public Zpkt::Nif {
friend inline ZuStringT<"Zpkt.Test.Nif"> ZmNHeapID(Nif *);
public:
  using ZmNHeap<Nif>::operator new;
  using ZmNHeap<Nif>::operator delete;
  Nif(ZiMultiplex *mx, const Zpkt::NifInfo &info) : m_mx{mx}, m_info{info} { }
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
	m_error = ZeEXCEPT(Error, "ZpktTest", "injected child finalization failure");
	return;
      }
      ++stats.finals;
      stats.lastChildFinal = ++stats.events;
      retireNif();
    }
    m_error = {};
    m_mx = nullptr;
  }
  ZeException error() const override { return m_error; }
  ZiMultiplex *mx() const override { return m_mx; }
  unsigned ifindex() const override { return m_info.ifindex; }
  unsigned rxQueues() const override { return m_info.rxQueues; }
  bool allocated() const override { return false; }
  bool ready() const override { return running(); }
  unsigned mtu() const override { return m_info.mtu; }
  bool zeroCopy() const override { return false; }
  ZuBSpan mac() const override { return m_info.mac; }
  bool txReady(uint8_t *) const override { return false; }
  bool send(uint8_t *, unsigned) override { return false; }
  bool tcpChksumOffload() override { return false; }
  void rx() override { }
  void wake() override { if (Engine::stopping()) Engine::stopped(); }
  unsigned pollTx() override { return 0; }
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override { return 0; }
  void status() override { }
private:
  void start_() override { Engine::started(true); }
  void stop_() override { Engine::stopped(true); }
  ZiMultiplex *m_mx;
  Zpkt::NifInfo m_info;
  ZeException m_error;
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
    stats.spin = node->val().spin;
    stats.info = info;
    if (!stats.spin) return ZeEXCEPT(Error, "ZpktTest", "injected device construction failure");
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
