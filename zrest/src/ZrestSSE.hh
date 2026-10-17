//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Bounded REST server-sent event production

#ifndef ZrestSSE_HH
#define ZrestSSE_HH

#include <zlib/Zrest.hh>

#include <zlib/ZhttpSSE.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSpinLock.hh>

#include <zlib/ZiIOBuf.hh>

namespace Zrest {

struct SSEData : public ZiIOBuf {
  using ZiIOBuf::ZiIOBuf;
  bool terminal = false;
};
ZmListDerive(SSEQueue, SSEData,
  ZmListNode<SSEData, ZmListHeapID<"">>);
using SSEBuf = Zi::IOBufAlloc<SSEQueue::Node,
  ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize,
  ZuStringT<"Zrest.SSE.Buffer">>;

class SSEProducer;

template <typename Emit>
struct SSEDriver : public ZmNHeap<SSEDriver<Emit>>, public ZmPolymorph {
friend inline ZuStringT<"Zrest.SSE.Driver"> ZmNHeapID(SSEDriver *);

  SSEDriver(SSEProducer *producer_, Emit emit_) :
    producer{producer_}, emit{ZuMv(emit_)} { }

  bool resume();
  void invalidate() { producer = nullptr; }

  SSEProducer *producer;
  Emit emit;
};

class SSEProducer : public ZmNHeap<SSEProducer>, public ZmObject {
friend inline ZuStringT<"Zrest.SSE.Producer"> ZmNHeapID(SSEProducer *);

public:
  SSEProducer(ZmScheduler *scheduler, unsigned thread, Limits limits,
      ZmAtomic<unsigned> *posts = nullptr) :
    m_scheduler{scheduler}, m_thread{thread}, m_limits{limits},
    m_posts{posts} { }
  ~SSEProducer() { close_(); }

  bool emit(ZuCSpan body, ZuCSpan id = {}, int64_t retry = -1,
      bool terminal = false) {
    if (!m_open) return false;
    if (body.length() > m_limits.maxSSEEventBytes ||
	!lineLengths_(body, id, retry)) return fail_();
    uint64_t framed = frameBytes_(body, id, retry);
    if (framed > UINT_MAX) return fail_();
    if (++m_sendPosts > m_limits.maxQueue) {
      --m_sendPosts;
      return false;
    }
    {
      ZmGuard guard{m_admit};
      if (!m_open || m_terminal) { --m_sendPosts; return false; }
      if (m_queued >= m_limits.maxQueue ||
	  framed > m_limits.maxQueueBytes - m_bytes) {
	--m_sendPosts;
	failLocked_();
	return false;
      }
      ++m_queued;
      m_bytes += framed;
    }
    ZmRef<SSEQueue::Node> record = new SSEBuf{};
    if (!Zhttp::saveSSE(*record, id, retry, body) || record->failed() ||
	record->length != framed) {
      ZmGuard guard{m_admit};
      --m_queued;
      m_bytes -= framed;
      --m_sendPosts;
      failLocked_();
      return false;
    }
    record->data().terminal = terminal;
    {
      ZmGuard guard{m_admit};
      if (!m_open || m_terminal) {
	--m_queued;
	m_bytes -= framed;
	--m_sendPosts;
	return false;
      }
      if (terminal) m_terminal = true;
      schedule_([this, record = ZuMv(record)]() mutable {
	--m_sendPosts;
	enqueue_(ZuMv(record));
      });
    }
    return true;
  }

  template <typename Emit>
  void start(Emit &&emit) {
    if (!m_scheduler->invoked(m_thread)) return;
    using E = ZuDecay<Emit>;
    using Driver = SSEDriver<E>;
    ZmRef<Driver> driver = new Driver{this, ZuFwd<Emit>(emit)};
    m_driver = driver.ptr();
    m_invalidate = [](void *ptr) {
      static_cast<Driver *>(ptr)->invalidate();
    };
    m_resume = ResumeFn::fn(ZuMv(driver), [](Driver *driver_) {
      return driver_->resume();
    });
    drain_();
  }

  bool failed() const { return m_failed.load_(); }
  unsigned queued() const { return m_queued.load_(); }
  uint64_t queuedBytes() const { return m_bytes.load_(); }

  void close() {
    {
      ZmGuard guard{m_admit};
      m_open = 0;
    }
    if (m_scheduler && !m_scheduler->invoked(m_thread)) {
      if (m_closePosted.xch(1)) return;
      schedule_([this]() { close_(); });
    } else {
      m_closePosted = 1;
      close_();
    }
  }

  template <typename Out>
  Zhttp::WriteOutcome::T write_(Out &out) {
    if (!m_open || m_failed) return Zhttp::WriteOutcome::Abort;
    auto record = m_queue.shift();
    if (!record) return Zhttp::WriteOutcome::Stream;
    {
      ZmGuard guard{m_admit};
      --m_queued;
      m_bytes -= record->length;
    }
    out << ZuCSpan{record->cspan()};
    return record->data().terminal ?
      Zhttp::WriteOutcome::End : Zhttp::WriteOutcome::Stream;
  }

private:
  bool lineLengths_(ZuCSpan body, ZuCSpan id, int64_t retry) const {
    unsigned max = m_limits.maxSSELineBytes;
    if (max < 6 || id.length() > max - 4) return false;
    if (retry >= 0) {
      if (max < 8) return false;
      unsigned digits = 1;
      while (retry >= 10) { retry /= 10; ++digits; }
      if (digits > max - 7) return false;
    }
    unsigned begin = 0;
    for (unsigned i = 0; i < body.length(); ++i) {
      if (body[i] != '\n') continue;
      if (i - begin > max - 6) return false;
      begin = i + 1;
    }
    return body.length() - begin <= max - 6;
  }

  static uint64_t frameBytes_(ZuCSpan body, ZuCSpan id, int64_t retry) {
    uint64_t length = body.length() + 8;
    for (auto c : body) if (c == '\n') length += 6;
    if (id) length += id.length() + 5;
    if (retry >= 0) {
      length += 8;
      do { ++length; retry /= 10; } while (retry);
    }
    return length;
  }

  bool fail_() {
    ZmGuard guard{m_admit};
    failLocked_();
    return false;
  }
  void failLocked_() {
    if (!m_open) return;
    m_open = 0;
    m_failed = 1;
    schedule_([this]() {
      discard_();
      drain_();
    });
  }

  template <typename L>
  void schedule_(L &&l) {
    if (m_posts) ++*m_posts;
    ZmRef<SSEProducer> self{this};
    auto posts = m_posts;
    m_scheduler->run([self = ZuMv(self), l = ZuFwd<L>(l), posts]() mutable {
      try { l(); } catch (...) { }
      if (posts) --*posts;
    }, m_thread);
  }

  void enqueue_(ZmRef<SSEQueue::Node> record) {
    if (!m_open) {
      ZmGuard guard{m_admit};
      --m_queued;
      m_bytes -= record->length;
      return;
    }
    m_queue.pushNode(ZuMv(record));
    if (m_resume && !m_posted) post_();
  }

  void post_() {
    if (m_posted || !m_resume) return;
    m_posted = true;
    schedule_([this]() {
      m_posted = false;
      drain_();
    });
  }

  void drain_() {
    if (!m_resume) return;
    unsigned visited = 0;
    do {
      auto resume = m_resume;
      if (!resume || !resume()) {
	close_();
	return;
      }
      ++visited;
    } while (visited < m_limits.workBatch && m_queue.count_());
    if (m_queue.count_()) post_();
  }

  void discard_() {
    while (auto record = m_queue.shift()) {
      ZmGuard guard{m_admit};
      --m_queued;
      m_bytes -= record->length;
    }
  }
  void close_() {
    {
      ZmGuard guard{m_admit};
      m_open = 0;
    }
    discard_();
    if (m_driver) m_invalidate(m_driver);
    m_driver = nullptr;
    m_resume = {};
  }

  using ResumeFn = ZmFn<bool(), ZmFnHeapID<"Zrest.SSE.Resume">>;
  ZmScheduler *m_scheduler = nullptr;
  unsigned m_thread = 0;
  Limits m_limits;
  ZmAtomic<unsigned> *m_posts = nullptr;
  ZmSpinLock m_admit;
  // Tx queue/producer state; send admission is shared with posting threads.
  alignas(Zm::CacheLineSize)
  SSEQueue m_queue; // Tx owner
  ZmAtomic<unsigned> m_sendPosts = 0;
  ResumeFn m_resume; // Tx owner
  void *m_driver = nullptr;
  void (*m_invalidate)(void *) = nullptr;
  ZmAtomic<unsigned> m_open = 1;
  ZmAtomic<unsigned> m_failed = 0;
  bool m_terminal = false; // admission lock
  ZmAtomic<unsigned> m_queued = 0; // admission lock
  ZmAtomic<uint64_t> m_bytes = 0; // admission lock
  bool m_posted = false; // Tx owner
  ZmAtomic<unsigned> m_closePosted = 0;
};

template <typename Emit>
bool SSEDriver<Emit>::resume()
{
  auto producer_ = producer;
  if (!producer_) return false;
  auto write = [producer_](auto &out) {
    return producer_->write_(out);
  };
  if constexpr (ZuIsSame<decltype(emit(write)), void>{}) {
    emit(write);
    return true;
  } else return emit(write);
}

} // Zrest

#endif /* ZrestSSE_HH */
