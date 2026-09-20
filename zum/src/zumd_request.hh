//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// bounded ownership and deadlines for public Zum requests

#ifndef zumd_request_HH
#define zumd_request_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmScheduler.hh>

namespace Zum {

template <typename Heap = ZuVoid>
class Requests_ : public Heap, public ZmObject {

public:
  Requests_ &operator =(const Requests_ &) = delete;

private:
  Requests_(const Requests_ &) = delete;

public:
  class RequestData : public ZmObject {
    RequestData(const RequestData &) = delete;
    RequestData &operator =(const RequestData &) = delete;

  public:
    ZuDerive(Fn, (ZmFn<void(), ZmFnHeapID<"Zum.Request.Fn">>));

    void complete(Fn);

  private:
  friend Requests_;

    enum class List : uint8_t { None, Active, Draining };

    RequestData(Requests_ *, Fn);

    void start_(ZuTime);
    void finish_(Fn);
    void cancel_();
    void timeout_();

    Requests_		*m_requests = nullptr;
    ZmScheduler::Timer	m_timer;
    Fn			m_cancel;
    List			m_list = List::None;
    bool			m_done = false;
  };

  ZmListDerive(RequestList, RequestData,
    ZmListNode<RequestData, ZmListHeapID<"Zum.Request">>);
  using Node = RequestList::Node;

  ZuDerive(StartFn, (ZmFn<void(ZmRef<Node>),
    ZmFnHeapID<"Zum.Requests.StartFn">>));
  ZuDerive(Fn, (ZmFn<void(), ZmFnHeapID<"Zum.Requests.Fn">>));

  Requests_() = default;

  bool init(ZmScheduler *, unsigned sid, unsigned limit);
  void activate();
  void deactivate(Fn = {});

  bool run(ZuTime deadline, StartFn, RequestData::Fn cancel);

  bool active() const { return m_up; }
  unsigned count() const { return m_count; }
  unsigned limit() const { return m_limit; }
  ZmScheduler *scheduler() const { return m_scheduler; }
  unsigned sid() const { return m_sid; }

private:
  void invoke_(Fn);
  void admit_(ZmRef<Node>, ZuTime, StartFn);
  void start_(RequestData *, ZuTime);
  void finish_(RequestData *, RequestData::Fn);
  void arm_(RequestData *, ZmScheduler::Timer *, ZuTime);
  void del_(ZmScheduler::Timer *);
  void remove_(RequestData *);
  void deactivate_();
  void deactivated_();

  ZmScheduler		*m_scheduler = nullptr;
  unsigned		m_sid = 0;
  unsigned		m_limit = 0;
  ZmAtomic<unsigned>	m_count = 0;
  ZmAtomic<uint32_t>	m_up = 0;
  RequestList		m_requests;
  RequestList		m_draining;
  Fn			m_deactivated;
  bool			m_closing = false;
};

using RequestsHeap = ZmHeap<"Zum.Requests", Requests_<>>;
ZuDerive(Requests, (Requests_<RequestsHeap>));
using Request = Requests::Node;

} // namespace Zum

#endif /* zumd_request_HH */
