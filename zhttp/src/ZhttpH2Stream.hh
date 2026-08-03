//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 native streams and queue admission

#ifndef ZhttpH2Stream_HH
#define ZhttpH2Stream_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZiTxStream.hh>

#include <zlib/ZhttpH2.hh>

namespace Zhttp {

namespace H2 {

class QueueAdmission {
public:
  void init(uint32_t max) { m_max = max; m_count = 0; }
  bool push() {
    if (++m_count <= m_max) return true;
    --m_count;
    return false;
  }
  void pop(uint32_t n = 1) {
    ZmAssert(m_count >= n);
    m_count -= n;
  }
  uint32_t count() const { return m_count; }

private:
  ZmAtomic<uint32_t>	m_count = 0;
  uint32_t		m_max = 0;
};

} // namespace H2

namespace H2_ {

template <typename Logical>
struct Stream {
  Stream() = default;
  Stream(uint32_t id_, ZmRef<Logical> logical_, bool local_) :
    logical{ZuMv(logical_)}, id{id_}, local{local_} { }

  ZmRef<Logical> logical;
  ZiTxErrorFn	txErrorFn;
  uint64_t	deferred = 0;
  int64_t	rxWindow = H2::DefltWindow;
  int64_t	txWindowHint = H2::DefltWindow;
  uint32_t	id = 0;
  bool		begin = false;
  bool		finalHeaders = false;
  bool		localEnd = false;
  bool		localEndQueued = false;
  bool		remoteEnd = false;
  bool		notified = false;
  bool		closing = false;
  bool		flowError = false;
  bool		local = false;
};

template <typename Logical>
inline uint32_t Stream_IDAxor(const Stream<Logical> &stream)
{
  return stream.id;
}

template <typename Logical>
using StreamHash = ZmHash<Stream<Logical>,
  ZmHashNode<Stream<Logical>,
    ZmHashKey<Stream_IDAxor<Logical>,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zhttp.H2">>>>>;

} // namespace H2_

} // namespace Zhttp

#endif /* ZhttpH2Stream_HH */
