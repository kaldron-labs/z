//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// bounded ownership and deadlines for public Zum requests

#ifndef ZumRequest_HH
#define ZumRequest_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmScheduler.hh>

namespace Zum {

class Requests;

class ZumAPI Request : public ZumObject {
  Request(const Request &) = delete;
  Request &operator =(const Request &) = delete;

public:
  using Fn = ZmFn<void(), ZmFnHeapID<"Zum.Request.Fn">>;

  void complete(Fn);

private:
friend Requests;
  using List = ZmList<ZmRef<Request>, ZmListHeapID<"Zum.Requests">>;

  Request(Requests *, Fn);

  void start_(ZuTime);
  void finish_(Fn);
  void cancel_();
  void timeout_();

  Requests		*m_requests = nullptr;
  List::Node		*m_node = nullptr;
  ZmScheduler::Timer	m_timer;
  Fn			m_cancel;
  bool			m_done = false;
};

class ZumAPI Requests : public ZumObject {
  Requests(const Requests &) = delete;
  Requests &operator =(const Requests &) = delete;

public:
  using StartFn = ZmFn<void(ZmRef<Request>),
    ZmFnHeapID<"Zum.Requests.StartFn">>;
  using Fn = ZmFn<void(), ZmFnHeapID<"Zum.Requests.Fn">>;

  Requests() = default;

  bool init(ZmScheduler *, unsigned sid, unsigned limit);
  void activate();
  void deactivate(Fn = {});

  bool run(ZuTime deadline, StartFn, Request::Fn cancel);

  bool active() const { return m_up; }
  unsigned count() const { return m_count; }
  unsigned limit() const { return m_limit; }
  ZmScheduler *scheduler() const { return m_scheduler; }
  unsigned sid() const { return m_sid; }

private:
friend Request;
  using List = Request::List;

  void invoke_(Fn);
  void admit_(ZmRef<Request>, ZuTime, StartFn);
  void remove_(Request *);
  void deactivate_();
  void deactivated_();

  ZmScheduler		*m_scheduler = nullptr;
  unsigned		m_sid = 0;
  unsigned		m_limit = 0;
  ZmAtomic<unsigned>	m_count = 0;
  ZmAtomic<uint32_t>	m_up = 0;
  List			m_requests;
  List			m_draining;
  Fn			m_deactivated;
  bool			m_closing = false;
};

} // namespace Zum

#endif /* ZumRequest_HH */
