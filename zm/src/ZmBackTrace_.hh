//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// stack backtrace

#ifndef ZmBackTrace__HH
#define ZmBackTrace__HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#define ZmBackTrace_DEPTH 64
#define ZmBackTrace_BUFSIZ 32768	// 32k

#include <string.h>

struct ZmBackTrace_Print;

class ZmAPI ZmBackTrace {
public:
  ZmBackTrace() {
    memset(m_frames, 0, sizeof(void *) * ZmBackTrace_DEPTH);
  }
  ZmBackTrace(unsigned skip) {
    memset(m_frames, 0, sizeof(void *) * ZmBackTrace_DEPTH);
    capture(skip + 1);
  }

  ZmBackTrace(const ZmBackTrace &t) {
    memcpy(m_frames, t.m_frames, sizeof(void *) * ZmBackTrace_DEPTH);
  }
  ZmBackTrace &operator =(const ZmBackTrace &t) {
    if (this != &t)
      memcpy(m_frames, t.m_frames, sizeof(void *) * ZmBackTrace_DEPTH);
    return *this;
  }

  bool equals(const ZmBackTrace &t) const {
    return !memcmp(m_frames, t.m_frames, sizeof(void *) * ZmBackTrace_DEPTH);
  }

  bool operator !() const { return !m_frames[0]; }

  void capture() { capture(1); }
  void capture(unsigned skip);
#ifdef _WIN32
  void capture(EXCEPTION_POINTERS *exInfo, unsigned skip);
#endif

  void *const *frames() const { return m_frames; }

  friend ZmBackTrace_Print ZuPrintType(ZmBackTrace *);

private:
  void		*m_frames[ZmBackTrace_DEPTH];
};

inline bool operator ==(const ZmBackTrace &l, const ZmBackTrace &r) {
  return l.equals(r);
}

#endif /* ZmBackTrace__HH */
