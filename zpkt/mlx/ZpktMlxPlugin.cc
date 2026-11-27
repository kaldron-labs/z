//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>

#include <zlib/ZpktPlugin.hh>

#include "ZpktMLXCf.hh"
#include "ZpktMlxEnv.hh"
#include "ZpktMlxNif.hh"

namespace Zpkt::Mlx {

ZfStructImpl(NifCf, Cf);
ZfStructImpl(PluginCf, Cf);

class Plugin final : public ZmNHeap<Plugin>, public Zpkt::Plugin {
friend inline ZuStringT<"Zpkt.Mlx.Plugin"> ZmNHeapID(Plugin *);

public:
  using ZmNHeap<Plugin>::operator new;
  using ZmNHeap<Plugin>::operator delete;

  explicit Plugin(const ZfCf::AnyNode *cf) {
    ZfCf::handler<PluginCf>(cf).update(m_config);
    auto interfaces = m_config.interfaces->citer();
    while (auto entry = interfaces()) {
      const auto &cf = entry->val();
      if (!cf.rxDepth || cf.rxDepth > INT_MAX || !cf.txDepth || cf.txDepth > INT_MAX)
	throw ZeEXCEPT(Error, "Zpkt.Mlx", ([name = entry->key()](auto &s) {
	  s << "invalid mlx queue depth for interface \"" << name << '"';
	}));
    }
  }

  bool loopback() const override { return bool(m_config.interfaces->find("_loop")); }

  ZuUnion<Zpkt::Nif *, ZeException> nif(
    ZiMultiplex *mx, ZuCSpan name, const NifInfo &info) override
  {
    auto entry = m_config.interfaces->find(name);
    if (!entry)
      return ZeEXCEPT(Error, "Zpkt.Mlx", ([name = CfString{name}](auto &s) {
	s << "unknown configured interface \"" << name << '"';
      }));
    try {
      return static_cast<Zpkt::Nif *>(new Nif{m_env, mx, entry->val(), info, name == "_loop"});
    } catch (ZeException error) {
      auto detail = error.fn();
      error.l = ZeMsgFn{[detail = ZuMv(detail), name = CfString{name}]
	(auto &s, const ZeEventInfo &event) {
	  s << "interface \"" << name << "\": ";
	  detail(s, event);
	}};
      return error;
    }
  }

  ZuUnion<void, ZeException> final() override { return m_env.final(); }

private:
  PluginCf	m_config;
  Env		m_env;
};

} // namespace Zpkt::Mlx

extern "C" ZuExport_API Zpkt::Plugin *ZpktPlugin(const ZfCf::AnyNode *cf)
{
  return new Zpkt::Mlx::Plugin{cf};
}
