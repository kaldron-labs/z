//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Rx-owned deferred application work admission and retirement

#ifndef ZfixWork_HH
#define ZfixWork_HH
#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZmAssert.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZfixFrame.hh>

namespace Zfix {

struct WorkLimits {
  uint64_t	bytes = uint64_t(Fmt::Limits::FrameBytes)<<2;
  // Initial small-order workload: at most 4096 roughly 1 KiB retained frames.
  unsigned	count = 4096;
};

struct WorkState {
  uint64_t	bytes = 0, count = 0, peakBytes = 0, peakCount = 0, refused = 0;
};

class ZfixAPI WorkBudget {
public:
  explicit WorkBudget(WorkLimits limits = {}) : m_limits{limits} { }

  ~WorkBudget() { ZmAssert(!m_state.count && !m_state.bytes); }

  WorkBudget(const WorkBudget &) = delete;
  WorkBudget &operator =(const WorkBudget &) = delete;

  bool fits_(uint64_t bytes) const {
    return bytes && m_state.count < m_limits.count && bytes <= m_limits.bytes &&
      m_state.bytes <= m_limits.bytes - bytes;
  }

  WorkLimits limits_() const { return m_limits; }

  bool limits_(WorkLimits);
  bool acquire_(uint64_t);
  void release_(uint64_t);

  void refuse_() { ++m_state.refused; }

  WorkState state_() const { return m_state; }

private:
  WorkLimits	m_limits;
  WorkState	m_state;
};

// Retain through this exact Ref type. Zero references schedule deletion on Rx;
// the enclosing input and pooled work object die before admission is returned.
// The owning link drains this credit before releasing its teardown reference.
template <typename Input, typename Owner>
class Work : public ZmNHeap<Work<Input, Owner>>, public ZmObject {
friend inline ZuStringT<"Zfix.Work"> ZmNHeapID(Work *);
public:
  using Ref = ZmRef<Work>;

  static Ref alloc_(Owner *owner, const Input &input, uint64_t bytes) {
    return new Work{owner, input, bytes};
  }

  const Input &input() const { return m_input; }
  uint64_t bytes() const { return m_bytes; }

#ifdef ZmObject_DEBUG
  bool deref(const void *referrer = nullptr) const {
    if (ZmObject::deref(referrer)) m_owner->workDone(this);
#else
  bool deref() const {
    if (ZmObject::deref()) m_owner->workDone(this);
#endif
    return false;
  }

private:
  Work(Owner *owner, const Input &input, uint64_t bytes) :
    m_owner{owner}, m_input{input}, m_bytes{bytes} { }

  // Immutable after publication. Only ZmObject's reference count is shared;
  // final release posts destruction and credit retirement to the Rx owner.
  Owner		*const m_owner;
  const Input	m_input;
  const uint64_t	m_bytes;
};

template <typename App> class WorkHub {
public:
  explicit WorkHub(WorkLimits limits = {}) : m_work{limits} { }

  WorkBudget &fixWorkRx_() {
    ZmAssert(static_cast<App *>(this)->rxInvoked()); return m_work;
  }

private:
  alignas(Zm::CacheLineSize)
  WorkBudget	m_work;
};

} // Zfix

#endif /* ZfixWork_HH */
