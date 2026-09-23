//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZdbusAuth.hh>

namespace Zdbus_ {

static bool hex(uint8_t c)
{
  return (c >= '0' && c <= '9') ||
    (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

unsigned Auth::feed(ZuBSpan bytes)
{
  if (m_state != AuthState::Waiting) return 0;
  unsigned n = bytes.length();
  for (unsigned i = 0; i < n; ++i) {
    uint8_t c = bytes[i];
    if (!c || c > 127 || m_line.length() == MaxLine) {
      m_state = AuthState::Failed;
      return i + 1;
    }
    if (c == '\n') {
      if (!reply_()) m_state = AuthState::Failed;
      else m_state = AuthState::Ready;
      return i + 1;
    }
    if (m_line.length() && m_line[m_line.length() - 1] == '\r') {
      m_state = AuthState::Failed;
      return i + 1;
    }
    if (c < ' ' && c != '\r') {
      m_state = AuthState::Failed;
      return i + 1;
    }
    m_line << char(c);
  }
  return n;
}

bool Auth::reply_()
{
  unsigned n = m_line.length();
  if (n < 5 || m_line[n - 1] != '\r') return false;
  ZuCSpan line{m_line.data(), n - 1};
  if (ZuCSpan{line.begin(), 3} != "OK ") return false;
  ZuCSpan guid{line.begin() + 3, line.length() - 3};
  if (guid.length() != GuidChars) return false;
  for (unsigned i = 0, length = guid.length(); i < length; ++i)
    if (!hex(guid[i])) return false;
  m_line.length(0);
  return true;
}

} // Zdbus_
