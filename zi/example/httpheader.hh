//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Streaming delimiter state; every incoming byte is visited once.
bool httpHeaderEnd(const void *ptr, unsigned offset, unsigned length) {
  auto data = static_cast<const uint8_t *>(ptr);
  for (unsigned i = 0; i < length; ++i) {
    auto c = data[i];
    switch (m_headerState) {
      case 0: m_headerState = c == '\r' ? 1 : 0; break;
      case 1: m_headerState = c == '\n' ? 2 : c == '\r' ? 1 : 0; break;
      case 2: m_headerState = c == '\r' ? 3 : 0; break;
      case 3:
        if (c == '\n') { m_headerLen = offset + i + 1; return true; }
        m_headerState = c == '\r' ? 1 : 0;
        break;
    }
  }
  return false;
}
