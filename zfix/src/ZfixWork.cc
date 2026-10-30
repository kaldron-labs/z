//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixWork.hh>

namespace Zfix {

bool WorkBudget::limits_(WorkLimits limits)
{
  if (limits.bytes < m_state.bytes || limits.count < m_state.count) return false;
  m_limits = limits; return true;
}
bool WorkBudget::acquire_(uint64_t bytes)
{
  if (!fits_(bytes)) { refuse_(); return false; }
  m_state.bytes += bytes; ++m_state.count;
  if (m_state.bytes > m_state.peakBytes) m_state.peakBytes = m_state.bytes;
  if (m_state.count > m_state.peakCount) m_state.peakCount = m_state.count;
  return true;
}
void WorkBudget::release_(uint64_t bytes)
{
  ZmAssert(bytes && m_state.count && bytes <= m_state.bytes);
  m_state.bytes -= bytes; --m_state.count;
}
} // Zfix

