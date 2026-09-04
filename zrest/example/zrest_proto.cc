//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZiLog.hh>

#include "zrest_proto.hh"

void Client::idle()
{
  terminalReady_();
}

void Client::workload(const Options &options, ZiMultiplex *mx)
{
  txRun(0, [this, options = &options, mx]() {
    workload_(options, mx);
  });
}

void Client::quiesce()
{
  ZmBlock<>{}([this](auto wake) mutable {
    txRun(0, [this, wake = ZuMv(wake)]() mutable {
      m_stopping = true;
      cancelTimer_();
      quiesceClean_(ZuMv(wake));
    });
  });
}

void Client::pong(uint64_t logicalID, bool value)
{
  txRun(0, [this, logicalID, value]() {
    retire_();
    if (m_state == Failed || m_state == Complete) return;
    if (!value) { fail_(); return; }
    ++m_completed;
    if (m_options->verbose)
      ZiLOG(Info, "zrest", ([logicalID](auto &s) {
	s << "event=pong id=" << logicalID;
      }));
    drive_();
  });
}

void Client::requestFailed()
{
  txRun(0, [this]() {
    retire_();
    if (m_state == Failed || m_state == Complete) terminalReady_();
    else fail_();
  });
}

void Client::workload_(const Options *options, ZiMultiplex *mx)
{
  m_options = options;
  m_mx = mx;
  m_interval = options->intervalTime;
  m_txSID = ZmSelf()->sid();
  ZiAssert(ZmSelf()->sid() == m_txSID, "zrest", (),
      "workload outside Tx shard", return);
  authenticate_();
}

void Client::ping_(Pending pending, ZmRef<const TokenState> state)
{
  auto request = new PingReq{};
  request->client = this;
  request->state = ZuMv(state);
  request->logicalID = pending.logicalID;
  request->replayed = pending.replayed;
  request->ping = true;
  send_<PingBuilder>(request);
}

void Client::retire_()
{
  ZmAssert(m_active);
  --m_active;
}

void Client::drive_()
{
  ZiAssert(ZmSelf()->sid() == m_txSID, "zrest", (),
      "state transition outside Tx shard", return);
  if (m_state == Failed || m_state == Complete || m_state == Authenticating ||
      m_state == Refreshing || !m_options) return;
  if (m_waveRemaining) return;
  if (m_pending) {
    dispatchPending_();
    return;
  }
  if (m_completed == m_options->requests && !m_active) {
    m_state = Complete;
    cancelTimer_();
    if (m_options->verbose)
      ZiLOG(Info, "zrest", ([completed = m_completed](auto &s) {
	s << "event=summary completed=" << completed << " failed=0";
      }));
    seal(0);
    return;
  }
  if (m_generated >= m_options->requests) return;
  if (!m_interval) {
    unsigned n = m_options->concurrency - m_active;
    unsigned left = m_options->requests - m_generated;
    if (n > left) n = left;
    if (!n) return;
    auto now = Zm::now();
    if (m_tokens->deadline <= now.sec()) { refresh_(); return; }
    startWave_(n, false);
    return;
  }
  auto now = Zm::now();
  if (m_next > now) { armTimer_(); return; }
  unsigned wave = m_options->requests - m_generated;
  if (wave > m_options->concurrency) wave = m_options->concurrency;
  if (m_options->concurrency - m_active < wave) return;
  if (m_tokens->deadline <= now.sec()) { refresh_(); return; }
  startWave_(wave, true);
}

void Client::startWave_(unsigned count, bool paced)
{
  m_waveState = m_tokens;
  m_waveRemaining = count;
  m_waveCount = count;
  m_wavePaced = paced;
  issueWave_();
}

void Client::issueWave_()
{
  if (m_state != Ready || !m_waveRemaining) return;
  unsigned n = m_waveRemaining;
  if (n > WorkBatch) n = WorkBatch;
  m_waveRemaining -= n;
  while (n--)
    ping_(Pending{m_generated++, false}, m_waveState);
  if (m_waveRemaining) {
    txRun(0, [this]() { issueWave_(); });
    return;
  }
  unsigned count = m_waveCount;
  bool paced = m_wavePaced;
  m_waveState = nullptr;
  m_waveCount = 0;
  m_wavePaced = false;
  if (!paced) { drive_(); return; }
  auto now = Zm::now();
  m_next = now + m_interval;
  ++m_tick;
  if (m_options->verbose) {
    uint64_t ns = uint64_t(now.sec()) * 1000000000ULL + now.nsec();
    ZiLOG(Info, "zrest", ([tick = m_tick, count, ns](auto &s) {
      s << "event=ping-wave tick=" << tick << " count=" << count <<
	" time_ns=" << ns;
    }));
  }
  if (m_generated < m_options->requests) armTimer_();
}

void Client::dispatchPending_()
{
  if (!m_pending || m_active >= m_options->concurrency) return;
  auto now = Zm::now();
  if (m_tokens->deadline <= now.sec()) { refresh_(); return; }
  ZmRef<const TokenState> state = m_tokens;
  unsigned n = m_options->concurrency - m_active;
  if (n > WorkBatch) n = WorkBatch;
  while (n-- && m_pending) ping_(m_pending.shift(), state);
  if (m_pending && m_active < m_options->concurrency)
    txRun(0, [this]() { dispatchPending_(); });
}

void Client::armTimer_()
{
  if (m_timerArmed || m_stopping || !m_mx || !m_interval ||
      m_generated >= m_options->requests) return;
  m_timerArmed = true;
  m_mx->add(&m_timer, m_next, ZmScheduler::Update,
    [this](auto &&arm) {
      return arm([this]() {
	m_timerArmed = false;
	drive_();
      });
    }, m_txSID);
}

void Client::cancelTimer_()
{
  if (m_mx) m_mx->del(&m_timer);
  m_timerArmed = false;
}

void Client::fail_()
{
  if (m_state == Failed || m_state == Complete) return;
  m_state = Failed;
  m_logicalFailed = 1;
  cancelTimer_();
  m_waveState = nullptr;
  m_waveRemaining = 0;
  m_waveCount = 0;
  m_cleaning = true;
  seal(0);
  cleanPending_();
}

void Client::cleanPending_()
{
  unsigned n = WorkBatch;
  while (n-- && m_pending) (void)m_pending.shift();
  if (m_pending) {
    txRun(0, [this]() { cleanPending_(); });
    return;
  }
  m_cleaning = false;
  terminalReady_();
}

void Client::terminalReady_()
{
  if (!m_notified && !m_active && !m_cleaning &&
      (m_state == Complete || m_state == Failed)) {
    m_notified = true;
    done.post();
  }
}

template <typename Wake>
void Client::quiesceClean_(Wake wake)
{
  unsigned n = WorkBatch;
  while (n-- && m_pending) (void)m_pending.shift();
  if (m_pending) {
    txRun(0, [this, wake = ZuMv(wake)]() mutable {
      quiesceClean_(ZuMv(wake));
    });
    return;
  }
  m_waveState = nullptr;
  m_waveRemaining = 0;
  m_waveCount = 0;
  m_tokens = nullptr;
  m_mx = nullptr;
  txRun(0, [wake = ZuMv(wake)]() mutable { wake(); });
}
