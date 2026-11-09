//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// bounded ownership and deadlines for public Zum requests

#ifndef zumd_request_HH
#define zumd_request_HH

#ifndef ZumLib_HH
#include <zlib/ZuDerive.hh>
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>

namespace Zum {

namespace RequestListState {
  enum { None, Active, Retiring };
}

// Closed bit and live admission credits share one publication word.
namespace RequestCredit { enum { Closed = 1LL<<62, Count = Closed - 1 }; }
// Limit cancellation and timer-retirement work independently of admission size.
namespace RequestWork { enum { Batch = 64 }; }

// An operation's caller can supply a graph credit protecting its borrowed
// resources. Helpers retain it through their entire lifetime, including DB
// continuations that survive application cancellation. As a base, this hold
// is released after all of the helper's own members have been destroyed.
// An explicit empty credit leaves lifetime control with the caller. The erased
// reference uses ZmPolymorph so the concrete credit's destructor always runs.
class RequestHold {
public:
  explicit RequestHold(ZmRef<ZmPolymorph> work) : m_work{ZuMv(work)} { }
  const ZmRef<ZmPolymorph> &work() const { return m_work; }

private:
  ZmRef<ZmPolymorph> m_work;
};

class Requests : public ZmNHeap<Requests>, public ZmObject,
    private ZmEngine<Requests> {
  using Engine = ZmEngine<Requests>;
  friend Engine;
friend inline ZuStringT<"Zum.Requests"> ZmNHeapID(Requests *);

public:
  Requests &operator =(const Requests &) = delete;

private:
  Requests(const Requests &) = delete;

public:
  class RequestData : public ZmObject {
    RequestData(const RequestData &) = delete;
    RequestData &operator =(const RequestData &) = delete;

  public:
    ZuDerive(Fn, (ZmFn<void(), ZmFnHeapID<"Zum.Request.Fn">>));

    template <typename L>
    void complete(L &&l) {
      if (m_done.load_()) return;
      auto self = ZmRef<Node>{static_cast<Node *>(this)};
      // A cancelled token can outlive its Requests owner. The route remains
      // caller-owned, and the owner shard rechecks done before its back-pointer.
      m_scheduler->invoke([self = ZuMv(self), l = ZuFwd<L>(l)]() mutable {
	if (!self->m_done.load_() && self->finish_()) {
	  if constexpr (ZuIs_<ZuDecay<L>, ZmAnyFn>{}) {
	    if (l) l();
	  } else {
	    l();
	  }
	}
      }, m_sid);
    }

  private:
  friend Requests;

    RequestData(Requests *, Fn);

    void start_(ZuTime);
    bool finish_();
    void cancel_();
    void timeout_();

    Requests		*m_requests = nullptr;
    ZmScheduler		*m_scheduler = nullptr;
    unsigned		m_sid = 0;
    ZmScheduler::Timer	m_timer;
    Fn			m_cancel;
    int8_t		m_list = RequestListState::None;
    ZmAtomic<uint32_t>	m_done = 0;
  };

  ZmListDerive(RequestList, RequestData,
    ZmListNode<RequestData, ZmListHeapID<"Zum.Request">>);
  using Node = RequestList::Node;

  using CtrlFn = ZmFn<void(bool), ZmFnHeapID<"ZmEngine.CtrlFn">>;
  using Engine::state;

  Requests() = default;
  ~Requests() {
    ZmAssert_(Engine::state() == ZmEngineState::Stopped);
    ZmAssert_(!(m_credit.load_() & RequestCredit::Count));
    ZmAssert_(m_requests.empty_() && m_retiring.empty_() && !m_retirePosted && !m_draining);
  }

  bool init(ZmScheduler *, unsigned sid, unsigned limit);
  // Activation completes on the admission owner; DB readiness is separate.
  void activate(CtrlFn = {});
  void deactivate(CtrlFn = {});

  template <typename L>
  bool run(ZuTime deadline, L &&l, RequestData::Fn cancel) {
    if (!m_scheduler || !deadline || !cancel || !active()) return false;
    if constexpr (ZuIs_<ZuDecay<L>, ZmAnyFn>{})
      if (!l) return false;
    for (;;) {
      uint64_t count = m_credit.load_();
      if ((count & RequestCredit::Closed) || count >= m_limit) return false;
      if (m_credit.cmpXch(count + 1, count) == count) break;
    }
    ZmRef<Node> request = new Node{this, ZuMv(cancel)};
    m_scheduler->run([
      this, request = ZuMv(request),
      deadline, l = ZuFwd<L>(l)
    ]() mutable {
      if (admit_(request.ptr(), deadline)) l(ZuMv(request));
    }, m_sid);
    return true;
  }

  bool active() const { return Engine::state() == ZmEngineState::Running; }
  // Owned slots include cancelled deadlines waiting for their owner barrier.
  unsigned count() const { return m_credit.load_() & RequestCredit::Count; }
  unsigned limit() const { return m_limit; }
  ZmScheduler *scheduler() const { return m_scheduler; }
  unsigned sid() const { return m_sid; }

private:
  void start_();
  void stop_();
  void retire_1();
  void retire_0();
  bool admit_(Node *, ZuTime);
  void start_(RequestData *, ZuTime);
  bool finish_(RequestData *);
  void arm_(RequestData *, ZmScheduler::Timer *, ZuTime);
  void del_(ZmScheduler::Timer *);
  void remove_(RequestData *);
  void deactivate_();
  void deactivated_();

  ZmScheduler		*m_scheduler = nullptr;
  unsigned		m_sid = 0;
  unsigned		m_limit = 0;
  alignas(Zm::CacheLineSize)
  ZmAtomic<uint64_t>	m_credit = RequestCredit::Closed;
  alignas(Zm::CacheLineSize)
  RequestList		m_requests;
  RequestList		m_retiring;
  bool			m_retirePosted = false;
  bool			m_draining = false;
};

using Request = Requests::Node;

} // namespace Zum

#endif /* zumd_request_HH */
