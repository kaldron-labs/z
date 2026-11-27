//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Raw AF_XDP arena, pool and packet facilities.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <zlib/ZiLog.hh>
#include <zlib/ZiModule.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZpktEnv.hh>
#include <zlib/ZpktNif.hh>

namespace Zpkt {

ZfStructImpl(PluginLoadCf, Cf);

Env::Env() : m_nifQueryFn{Zpkt::nifQuery} { }

Env::~Env()
{
  auto result = final();
  if (result.is<ZeException>()) ZiLogEvent(ZuMv(result).p<ZeException>());
}

void Env::init(const ZfCf::AnyNode *cf)
{
  ZmAssert_(!m_plugin && !m_nifs.count_());
  PluginLoadCf config;
  ZfCf::handler<PluginLoadCf>(cf).update(config);
  ZiModule module;
  auto &path = config.module;
  ZeString error;
  if (module.load(path, config.preload ? ZiModule::Pre : 0, &error) < 0)
    throw ZeEXCEPT(Error, "Zpkt", ([path = ZeString{path}, error](auto &s) {
      s << "failed to load \"" << path << "\": " << error;
    }));
  auto fn = reinterpret_cast<PluginFn>(module.resolve(ZpktPluginFnSym, &error));
  if (!fn) {
    module.unload();
    throw ZeEXCEPT(Error, "Zpkt", ([path = ZeString{path}, error](auto &s) {
      s << "failed to resolve \"" ZpktPluginFnSym "\" in \"" << path << "\": " << error;
    }));
  }
  ZmRef<Plugin> plugin = (*fn)(cf);
  if (!plugin)
    throw ZeEXCEPT(Error, "Zpkt", ([path = ZeString{path}](auto &s) {
      s << "null " ZpktPluginFnSym " result from \"" << path << '"';
    }));
  m_plugin = ZuMv(plugin);
}

bool Env::loopback() const
{
  ZmAssert_(m_plugin);
  return m_plugin->loopback();
}

ZuUnion<Nif *, ZeException> Env::nif(ZiMultiplex *mx, ZuCSpan name)
{
  ZmAssert_(m_plugin);
  if (auto node = m_nifs.findPtr(name)) {
    auto nif = node->val().ptr();
    if (nif->mx() == mx) return nif;
    return ZeEXCEPT(Error, "Zpkt", ([name = ZeString{name}](auto &s) {
      s << "conflicting multiplexer for interface \"" << name << '"';
    }));
  }
  ZmAssert_(mx && mx->state() == ZmEngineState::Stopped);
  NifInfo info;
  if (name == "_loop") {
    if (!loopback())
      return ZeEXCEPT(Error, "Zpkt", "backend loopback (_loop) is unsupported");
  } else if (m_plugin->hostNifInfo()) {
    auto queried = m_nifQueryFn(name);
    if (queried.is<ZeException>()) return ZuMv(queried).p<ZeException>();
    info = queried.p<NifInfo>();
  }
  auto result = m_plugin->nif(mx, name, info);
  if (result.is<ZeException>()) return ZuMv(result).p<ZeException>();
  ZmRef<Nif> nif = result.p<Nif *>();
  if (!nif)
    return ZeEXCEPT(Error, "Zpkt", ([name = ZeString{name}](auto &s) {
      s << "null plugin Nif for interface \"" << name << '"';
    }));
  nif->env(this, name);
  m_nifs.add(CfString{name}, nif);
  return nif.ptr();
}

void Env::retire(Nif *nif)
{
  if (auto node = m_nifs.findPtr(nif->name()))
    if (node->val().ptr() == nif) m_nifs.delNode(node);
  nif->env(nullptr);
}

void Nif::final_()
{
  if (m_env) m_env->retire(this);
}

ZuUnion<void, ZeException> Env::final()
{
  {
    auto it = m_nifs.citer();
    while (auto node = it())
      ZmAssert_(node->val()->state() == ZmEngineState::Stopped);
  }
  while (auto node = m_nifs.minimumPtr()) {
    ZmRef<Nif> nif = node->val();
    nif->final();
    if (auto error = nif->error()) return error;
    // Successful backends retire themselves; test doubles follow the same rule.
    ZmAssert_(!nif->env());
  }
  if (m_plugin) {
    auto result = m_plugin->final();
    if (result.is<ZeException>()) return ZuMv(result).p<ZeException>();
  }
  return {};
}

} // Zpkt
