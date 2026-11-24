//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <string.h>

#include <rte_ethdev.h>

#include <zlib/ZmHeap.hh>
#include <zlib/ZpktPlugin.hh>

#include "ZpktDPDKCf.hh"
#include "ZpktDpdk.hh"
#include "ZpktDpdkNif.hh"

namespace Zpkt::Dpdk {

ZfStructImpl(InterfaceCf, Cf);
ZfStructImpl(PluginCf, Cf);

class Plugin final : public ZmNHeap<Plugin>, public Zpkt::Plugin {
friend inline ZuStringT<"Zpkt.Dpdk.Plugin"> ZmNHeapID(Plugin *);

public:
  using ZmNHeap<Plugin>::operator new;
  using ZmNHeap<Plugin>::operator delete;

  explicit Plugin(const ZfCf::AnyNode *cf) {
    ZfCf::handler<PluginCf>(cf).update(m_config);
    {
      auto interfaces = m_config.interfaces->citer();
      while (auto entry = interfaces()) {
	const auto &port = entry->val().port;
	if (!port || port.length() >= RTE_ETH_NAME_MAX_LEN ||
	    memchr(port.data(), 0, port.length()))
	  throw ZeEXCEPT(Error, "Zpkt.Dpdk", ([name = entry->key(), port](auto &s) {
	    s << "interface \"" << name << "\" has invalid configured DPDK port \""
	      << port << "\": " << ZeError{EINVAL};
	  }));
      }
    }
    EALParams params;
    if (cf && cf->has<ZfCf::AnyNode::Object>()) {
      for (const auto &field : cf->data<ZfCf::AnyNode::Object>()) {
	if (field.p<0>() == "eal") {
	  ZfCf::handler<EALParams, ZuFacet::CLI>(field.p<1>()).load(params);
	  break;
	}
      }
    }
    m_eal.init(params);
  }

  ZuUnion<Zpkt::Nif *, ZeException> nif(
    ZiMultiplex *mx, ZuCSpan name, const NifInfo &info) override
  {
    auto entry = m_config.interfaces->find(name);
    if (!entry)
      return ZeEXCEPT(Error, "Zpkt.Dpdk", ([name = CfString{name}](auto &s) {
	s << "unknown configured interface \"" << name << "\"";
      }));
    try {
      return static_cast<Zpkt::Nif *>(new Nif{m_eal, mx, entry->val(), info});
    } catch (ZeException error) {
      auto detail = error.fn();
      error.l = ZeMsgFn{[
	detail = ZuMv(detail), name = CfString{name}, port = entry->val().port
      ](auto &s, const ZeEventInfo &event) {
	s << "interface \"" << name << "\" on DPDK port \"" << port << "\": ";
	detail(s, event);
      }};
      return error;
    }
  }

  ZuUnion<void, ZeException> final() override { return {}; }

private:
  PluginCf	m_config;
  EAL		m_eal;
};

} // namespace Zpkt::Dpdk

extern "C" ZpktDPDKAPI Zpkt::Plugin *ZpktPlugin(const ZfCf::AnyNode *cf)
{
  return new Zpkt::Dpdk::Plugin{cf};
}
