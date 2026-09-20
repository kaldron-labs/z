//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_request.hh>

namespace Zum {

template <typename Heap>
Requests_<Heap>::RequestData::RequestData(Requests_ *requests, Fn cancel) :
  m_requests{requests}, m_cancel{ZuMv(cancel)} { }

template <typename Heap>
void Requests_<Heap>::RequestData::complete(Fn fn)
{
  auto self = ZmRef<Node>{static_cast<Node *>(this)};
  m_requests->invoke_([self = ZuMv(self), fn = ZuMv(fn)]() mutable {
    self->finish_(ZuMv(fn));
  });
}

template <typename Heap>
void Requests_<Heap>::RequestData::start_(ZuTime deadline)
{
  m_requests->start_(this, deadline);
}

template <typename Heap>
void Requests_<Heap>::RequestData::finish_(Fn fn)
{
  m_requests->finish_(this, ZuMv(fn));
}

template <typename Heap>
void Requests_<Heap>::RequestData::cancel_()
{
  auto cancel = ZuMv(m_cancel);
  finish_(ZuMv(cancel));
}

template <typename Heap>
void Requests_<Heap>::RequestData::timeout_()
{
  ZmRef<Node> self{static_cast<Node *>(this)};
  cancel_();
}

template <typename Heap>
bool Requests_<Heap>::init(ZmScheduler *scheduler, unsigned sid, unsigned limit)
{
  if (!scheduler || !sid || sid > scheduler->params().nThreads() || !limit)
    return false;
  m_scheduler = scheduler;
  m_sid = sid;
  m_limit = limit;
  return true;
}

template <typename Heap>
void Requests_<Heap>::invoke_(Fn fn)
{
  m_scheduler->invoke([fn = ZuMv(fn)]() mutable { fn(); }, m_sid);
}

template <typename Heap>
void Requests_<Heap>::activate()
{
  m_up = 1;
}

template <typename Heap>
bool Requests_<Heap>::run(
    ZuTime deadline, StartFn start, RequestData::Fn cancel)
{
  if (!m_scheduler || !deadline || !start || !cancel || !m_up) return false;
  for (;;) {
    unsigned count = m_count;
    if (count >= m_limit || !m_up) return false;
    if (m_count.cmpXch(count + 1, count) == count) break;
  }
  ZmRef<Node> request = new Node{this, ZuMv(cancel)};
  m_scheduler->run([
    self = ZmRef<Requests>{this}, request = ZuMv(request),
    deadline, start = ZuMv(start)
  ]() mutable {
    self->admit_(ZuMv(request), deadline, ZuMv(start));
  }, m_sid);
  return true;
}

template <typename Heap>
void Requests_<Heap>::admit_(ZmRef<Node> request, ZuTime deadline, StartFn start)
{
  if (!m_up || m_closing) {
    request->cancel_();
    return;
  }
  request->start_(deadline);
  start(ZuMv(request));
}

template <typename Heap>
void Requests_<Heap>::start_(RequestData *request, ZuTime deadline)
{
  Node *node = static_cast<Node *>(request);
  request->m_list = RequestData::List::Active;
  m_requests.pushNode(node);
  arm_(request, &request->m_timer, deadline);
}

template <typename Heap>
void Requests_<Heap>::arm_(
    RequestData *request, ZmScheduler::Timer *timer, ZuTime deadline)
{
  m_scheduler->add(timer, deadline, ZmScheduler::Update,
    [request](auto &&arm) { return arm([request]() { request->timeout_(); }); },
    m_sid);
}

template <typename Heap>
void Requests_<Heap>::del_(ZmScheduler::Timer *timer)
{
  m_scheduler->del(timer);
}

template <typename Heap>
void Requests_<Heap>::finish_(RequestData *request, RequestData::Fn fn)
{
  if (request->m_done) return;
  request->m_done = true;
  del_(&request->m_timer);
  remove_(request);
  request->m_cancel = typename RequestData::Fn{};
  if (fn) fn();
}

template <typename Heap>
void Requests_<Heap>::remove_(RequestData *request)
{
  Node *node = static_cast<Node *>(request);
  switch (request->m_list) {
  case RequestData::List::Active:
    m_requests.delNode(node);
    break;
  case RequestData::List::Draining:
    m_draining.delNode(node);
    break;
  case RequestData::List::None:
    break;
  }
  request->m_list = RequestData::List::None;
  --m_count;
}

template <typename Heap>
void Requests_<Heap>::deactivate(Fn complete)
{
  m_up = 0;
  invoke_([self = ZmRef<Requests>{this}, complete = ZuMv(complete)]() mutable {
    if (self->m_closing) {
      if (complete) complete();
      return;
    }
    self->m_closing = true;
    self->m_deactivated = ZuMv(complete);
    self->deactivate_();
  });
}

template <typename Heap>
void Requests_<Heap>::deactivate_()
{
  while (auto request = m_requests.shift()) {
    Node *node = request.ptr();
    node->m_list = RequestData::List::Draining;
    m_draining.pushNode(ZuMv(request));
    node->cancel_();
  }
  m_scheduler->run([self = ZmRef<Requests>{this}]() {
    self->deactivated_();
  }, m_sid);
}

template <typename Heap>
void Requests_<Heap>::deactivated_()
{
  m_draining.clean();
  m_closing = false;
  auto complete = ZuMv(m_deactivated);
  if (complete) complete();
}

template class Requests_<ZmHeap<"Zum.Requests", Requests_<>>>;

} // namespace Zum
