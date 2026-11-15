//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Deterministic LL0 wait-transition hooks; production builds erase these gates.
#ifndef ZmRingWaitTest_HH
#define ZmRingWaitTest_HH

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>

class ZmRing_Breakpoint {
public:
  void enable() { m_enabled = 1; }
  bool wait() { return !m_reached.timedwait(Zm::now() + ZuTime{2}); }
  void proceed() { m_proceed.post(); }
  void reached(const char *) {
    if (!m_enabled.xch(0)) return;
    m_reached.post();
    m_proceed.wait();
  }
private:
  ZmAtomic<unsigned> m_enabled{0};
  ZmSemaphore m_reached, m_proceed;
};

#define ZmRing_FUNCTEST
#include <zlib/ZmRing.hh>

struct WaitCounts {
  ZmAtomic<unsigned> waits{0}, wakes{0};
};

struct WaitBlocker : public ZmRing_::Blocker {
  WaitCounts *counts = nullptr;
  int wait(ZmAtomic<uint32_t> &addr, uint32_t value, const Params &params) {
    ++counts->waits;
    return ZmRing_::Blocker::wait(addr, value, params);
  }
  void wake(ZmAtomic<uint32_t> &addr) {
    ++counts->wakes;
    ZmRing_::Blocker::wake(addr);
  }
};

template <bool MW, bool MR>
using WaitRing = ZmRing_::Ring<ZmRingT<uint64_t, ZmRingMW<MW, ZmRingMR<MR>>>,
  ZmRing_::ParamData, WaitBlocker>;

struct WaitPhase { enum { Spin, CAS, Park }; };

template <typename Ring>
ZmRing_Breakpoint &waitGate(Ring &ring, unsigned phase) {
  if (phase == WaitPhase::Spin) return ring.bp_waitSpin;
  if (phase == WaitPhase::CAS) return ring.bp_waitCAS;
  return ring.bp_waitPark;
}

template <typename Ring>
bool pushValue(Ring &ring, uint64_t value, bool blocking = false) {
  auto p = static_cast<uint64_t *>(blocking ? ring.push() : ring.tryPush());
  if (!p) return false;
  *p = value;
  if constexpr (Ring::MW) ring.push2(p);
  else ring.push2();
  return true;
}

template <typename Ring>
bool shiftValue(Ring &ring, uint64_t value, bool blocking = false) {
  auto p = blocking ? ring.shift() : ring.tryShift();
  if (!p) return false;
  bool same = *p == value;
  ring.shift2();
  return same;
}

template <typename Ring>
bool readerWaiting(const Ring &ring) {
  if constexpr (!Ring::MW && !Ring::MR)
    return ring.head_() & ZmRing_::Waiting32();
  else {
    auto p = reinterpret_cast<const ZmAtomic<uint64_t> *>(
      &ring.data()[ring.tail_() & ~ZmRing_::Wrapped32()]);
    return uint64_t(*p) & ZmRing_::Waiting();
  }
}

#endif /* ZmRingWaitTest_HH */
