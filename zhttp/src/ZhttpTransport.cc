//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuICmp.hh>

#include <zlib/ZhttpTransport.hh>

namespace Zhttp {

ZtEnumImplStruct(Transport);
ZtEnumImplStruct(Migration);
ZtEnumImplStruct(H2Policy);

Migration::T migrationMode(ZuCSpan s, Migration::T deflt)
{
  if (ZuICmp<ZuCSpan>::equals(s, "disabled") ||
      ZuICmp<ZuCSpan>::equals(s, "disable"))
    return Migration::Disabled;
  if (ZuICmp<ZuCSpan>::equals(s, "passive")) return Migration::Passive;
  if (ZuICmp<ZuCSpan>::equals(s, "active")) return Migration::Active;
  return deflt;
}

void ProfileCore::profiles(unsigned n)
{
  m_profiles.size(n);
}

void ProfileCore::add(const ProfileDesc &desc)
{
  for (auto &entry: m_profiles)
    if (entry.transport == desc.transport) {
      entry.versions |= desc.versions;
      return;
    }
  m_profiles.push(desc);
}

Config ProfileCore::selected(const Config &config) const
{
  bool tcp = false, h1 = false, h2 = false, h3 = false;
  for (const auto &entry: m_profiles) {
    tcp |= entry.transport == Transport::TCP;
    h1 |= entry.transport == Transport::TLS &&
      (entry.versions & (1U << Version::H1));
    h2 |= entry.transport == Transport::TLS &&
      (entry.versions & (1U << Version::H2));
    h3 |= entry.transport == Transport::QUIC;
  }
  return config.selected(tcp, h1, h2, h3);
}

void Hubs::final()
{
  // Finalize only after the stop continuation; never wait on an I/O shard.
  bool ok = Engine::lock(ZmEngineState::Stopped, [this]() {
    final_();
    return true;
  });
  ZmAssert(ok, (return));
}

void Hubs::start_()
{
  if (!m_entries) {
	Engine::started(false);
	return;
  }
  m_next = 0;
  startNext_();
}

void Hubs::startNext_()
{
  if (m_next >= m_entries.length()) {
    Engine::started(true);
    return;
  }
  unsigned i = m_next++;
  auto &entry = m_entries[i];
  entry.start(entry.ptr, DoneFn{this, [i](Hubs *hubs, bool ok) {
    hubs->started_(i, ok);
  }});
}

void Hubs::started_(unsigned i, bool ok)
{
  if (ok) {
    m_entries[i].started = true;
    startNext_();
    return;
  }
  m_rollback = true;
  m_stopOK = true;
  m_next = i;
  stopNext_();
}

void Hubs::stopAccepting()
{
  for (auto &entry: m_entries)
    if (entry.started) entry.stopAccepting(entry.ptr);
}

void Hubs::stop_()
{
  m_rollback = false;
  m_stopOK = true;
  m_next = m_entries.length();
  stopAccepting();
  stopNext_();
}

void Hubs::stopNext_()
{
  while (m_next) {
    auto &entry = m_entries[--m_next];
    if (!entry.started) continue;
    entry.stop(entry.ptr,
	DoneFn{this, [](Hubs *hubs, bool ok) {
	  hubs->stopped_(ok);
	}});
    return;
  }
  if (m_rollback) {
    m_rollback = false;
    Engine::started(false);
  } else
    Engine::stopped(m_stopOK);
}

void Hubs::stopped_(bool ok)
{
  if (!ok) m_stopOK = false;
  m_entries[m_next].started = false;
  stopNext_();
}

void Hubs::final_()
{
  for (unsigned i = m_entries.length(); i; --i)
    m_entries[i - 1].final(m_entries[i - 1].ptr);
  m_entries.init();
}

} // namespace Zhttp
