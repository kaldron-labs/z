//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Owner-shard posted-buffer admission

#ifndef ZfixIngress_HH
#define ZfixIngress_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZfixFrame.hh>

namespace Zfix {

struct PostedLimits {
  uint64_t	bytes = uint64_t(Fmt::Limits::FrameBytes)<<2;
};

struct PostedState {
  uint64_t	bytes = 0, count = 0, peak = 0, refused = 0;
};

// Each submission/administrative ledger belongs to application Rx. Initial
// limits precede admission; live changes cannot undercut outstanding reservations.
// Keep the domain alive until links drain.
class ZfixAPI PostedIngress {
public:
  explicit PostedIngress(PostedLimits limits = {}) : m_limit{limits.bytes} { }

  ~PostedIngress() { ZmAssert(!m_state.bytes && !m_state.count); }

  PostedIngress(const PostedIngress &) = delete;
  PostedIngress &operator =(const PostedIngress &) = delete;

  bool fits_(uint64_t bytes) const {
    return bytes && bytes <= m_limit && m_state.bytes <= m_limit - bytes;
  }

  PostedLimits limits_() const { return {m_limit}; }

  bool limits_(PostedLimits limits) {
    if (limits.bytes < m_state.bytes) return false;
    m_limit = limits.bytes; return true;
  }

  bool acquire_(uint64_t);
  void release_(uint64_t);

  void refuse_() { ++m_state.refused; }

  PostedState state_() const { return m_state; }

private:
  uint64_t	m_limit;
  PostedState	m_state;
};

template <typename App> class PostedHub {
public:
  explicit PostedHub(PostedLimits submit = {}, PostedLimits admin = {}) :
    m_submit{submit}, m_admin{admin} { }

  PostedIngress &fixSubmitRx_() {
    ZmAssert(static_cast<App *>(this)->rxInvoked()); return m_submit;
  }

  PostedIngress &fixAdminRx_() {
    ZmAssert(static_cast<App *>(this)->rxInvoked()); return m_admin;
  }

private:
  // One application Rx group; retain its boundary from preceding base state.
  alignas(Zm::CacheLineSize)
  PostedIngress	m_submit;
  PostedIngress	m_admin;
};

} // Zfix

#endif /* ZfixIngress_HH */
