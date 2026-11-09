//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuArray.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZhttpTransport.hh>

namespace Zhttp {

ZtEnumImplStruct(Transport);
ZtEnumImplStruct(Migration);
ZtEnumImplStruct(H2Policy);

Migration::T migrationMode(ZuCSpan s, Migration::T deflt)
{
  struct MigrationIDs {
    using Keys = ZuStringTL<"disabled", "disable", "passive", "active">;
    enum { MaxLen = 8 }; // "disabled"
  };
  if (s.length() > MigrationIDs::MaxLen) return deflt;
  ZuBArray<MigrationIDs::MaxLen> lower;
  for (unsigned i = 0, n = s.length(); i < n; ++i) {
    uint8_t c = s[i];
    lower[i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
  }
  static constexpr auto matcher = ZuMatcher<MigrationIDs>();
  switch (matcher.exact(ZuBSpan{lower.data(), s.length()})) {
    case 0:
    case 1: return Migration::Disabled;
    case 2: return Migration::Passive;
    case 3: return Migration::Active;
    default: return deflt;
  }
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
  start_0();
}

void Hubs::start_0()
{
  if (m_next >= m_entries.length()) {
    Engine::started(true);
    return;
  }
  unsigned i = m_next++;
  auto &entry = m_entries[i];
  entry.start(entry.ptr, DoneFn{this, [i](Hubs *hubs, bool ok) {
    hubs->start_1(i, ok);
  }});
}

void Hubs::start_1(unsigned i, bool ok)
{
  if (ok) {
    m_entries[i].started = true;
    start_0();
    return;
  }
  m_rollback = true;
  m_stopOK = true;
  m_next = i;
  stop_0();
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
  stop_0();
}

void Hubs::stop_0()
{
  while (m_next) {
    auto &entry = m_entries[--m_next];
    // Best-effort shutdown attempts every initialized hub, including when a
    // preceding hub reported failure. Startup rollback visits started hubs.
    if (m_rollback && !entry.started) continue;
    entry.stop(entry.ptr,
	DoneFn{this, [](Hubs *hubs, bool ok) {
	  hubs->stop_1(ok);
	}});
    return;
  }
  if (m_rollback) {
    m_rollback = false;
    Engine::started(false);
  } else
    Engine::stopped(m_stopOK);
}

void Hubs::stop_1(bool ok)
{
  if (!ok) m_stopOK = false;
  // A completed stop retires this activation even when cleanup reports failure.
  m_entries[m_next].started = false;
  stop_0();
}

void Hubs::final_()
{
  for (unsigned i = m_entries.length(); i; --i)
    m_entries[i - 1].final(m_entries[i - 1].ptr);
  m_entries.init();
}

} // namespace Zhttp
