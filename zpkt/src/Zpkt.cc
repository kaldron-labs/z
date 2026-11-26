//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmBlock.hh>

#include <zlib/Zpkt.hh>

namespace Zpkt {

ZtEnumImplNS(NifFlags);

Nif::Nif(ZiMultiplex *mx, const NifInfo &info) :
  m_mx{mx}, m_ifindex{info.ifindex}, m_mtu{info.mtu}, m_rxQueues{info.rxQueues}
{
  memcpy(m_mac, info.mac, sizeof(m_mac));
}

void Nif::resetInfo()
{
  m_mx = nullptr;
  m_flags = 0;
  m_ifindex = m_mtu = m_rxQueues = 0;
  memset(m_mac, 0, sizeof(m_mac));
  m_tcpChksumOffload = false;
}

void Nif::startRun()
{
  m_receive = ZuMv(m_next.receive);
  m_txStopped = ZuMv(m_next.txStopped);
  start_();
}

void DeviceEngine::start_() { static_cast<Nif *>(this)->startRun(); }

void DeviceEngine::stop_() { static_cast<Nif *>(this)->stop_(); }

bool Nif::stop()
{
  return ZmBlock<bool>{}([this](auto done) { stop(ZuMv(done)); });
}

void Nif::stop(StartFn done)
{
  Callbacks canceled;
  Engine::stop(ZuMv(done), [this, &canceled] { canceled = ZuMv(m_next); });
}

bool Nif::start(RecvFn receive, StopFn txStopped)
{
  return ZmBlock<bool>{}([this, &receive, &txStopped](auto wake) {
    start(ZuMv(receive), ZuMv(txStopped), ZuMv(wake));
  });
}

void Nif::start(RecvFn receive, StopFn txStopped, StartFn completion)
{
  Engine::start(ZuMv(completion), [&] {
    m_next = {ZuMv(receive), ZuMv(txStopped)};
  });
}

ZuTime Nif::monotonicNow()
{
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return ZuTime{now};
}

void Nif::wake()
{
  ++m_wakes;
  wake_();
}

void Nif::closeWake()
{
  if (m_wakeFD >= 0) ::close(m_wakeFD);
  m_wakeFD = -1;
}

void Nif::wake_()
{
  if (!m_blocking) return;
  uint64_t one = 1;
  while (::write(m_wakeFD, &one, sizeof(one)) < 0) {
    if (errno == EINTR) continue;
    ZmAssert_(errno == EAGAIN);
    break;
  }
}

void Nif::ackWake()
{
  uint64_t value;
  while (::read(m_wakeFD, &value, sizeof(value)) < 0) {
    if (errno == EINTR) continue;
    ZmAssert_(errno == EAGAIN);
    break;
  }
}

} // namespace Zpkt
