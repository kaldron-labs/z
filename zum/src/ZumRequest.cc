//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumRequest.hh>

namespace Zum {

Request::Request(Requests *requests, Fn cancel) :
  m_requests{requests}, m_cancel{ZuMv(cancel)} { }

void Request::complete(Fn fn)
{
  m_requests->invoke_([self = ZmRef<Request>{this}, fn = ZuMv(fn)]() mutable {
    self->finish_(ZuMv(fn));
  });
}

void Request::start_(ZuTime deadline)
{
  auto node = m_requests->m_requests.push(ZmRef<Request>{this});
  m_node = node;
  m_requests->m_scheduler->add(&m_timer, deadline, ZmScheduler::Update,
    [this](auto &&arm) { return arm([this]() { timeout_(); }); },
    m_requests->m_sid);
}

void Request::finish_(Fn fn)
{
  if (m_done) return;
  m_done = true;
  m_requests->m_scheduler->del(&m_timer);
  m_requests->remove_(this);
  m_cancel = {};
  if (fn) fn();
}

void Request::cancel_()
{
  auto cancel = ZuMv(m_cancel);
  finish_(ZuMv(cancel));
}

void Request::timeout_()
{
  ZmRef<Request> self{this};
  cancel_();
}

bool Requests::init(ZmScheduler *scheduler, unsigned sid, unsigned limit)
{
  if (!scheduler || !sid || sid > scheduler->params().nThreads() || !limit)
    return false;
  m_scheduler = scheduler;
  m_sid = sid;
  m_limit = limit;
  return true;
}

void Requests::invoke_(Fn fn)
{
  m_scheduler->invoke([fn = ZuMv(fn)]() mutable { fn(); }, m_sid);
}

void Requests::activate()
{
  m_up = 1;
}

bool Requests::run(ZuTime deadline, StartFn start, Request::Fn cancel)
{
  if (!m_scheduler || !deadline || !start || !cancel || !m_up) return false;
  for (;;) {
    unsigned count = m_count;
    if (count >= m_limit || !m_up) return false;
    if (m_count.cmpXch(count + 1, count) == count) break;
  }
  ZmRef<Request> request = new Request{this, ZuMv(cancel)};
  m_scheduler->run([
    self = ZmRef<Requests>{this}, request = ZuMv(request),
    deadline, start = ZuMv(start)
  ]() mutable {
    self->admit_(ZuMv(request), deadline, ZuMv(start));
  }, m_sid);
  return true;
}

void Requests::admit_(ZmRef<Request> request, ZuTime deadline, StartFn start)
{
  if (!m_up || m_closing) {
    request->cancel_();
    return;
  }
  request->start_(deadline);
  start(ZuMv(request));
}

void Requests::remove_(Request *request)
{
  if (request->m_node) {
    m_requests.delNode(request->m_node);
    request->m_node = nullptr;
  }
  --m_count;
}

void Requests::deactivate(Fn complete)
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

void Requests::deactivate_()
{
  while (auto request = m_requests.shiftVal()) {
    request->m_node = nullptr;
    m_draining.push(request);
    request->cancel_();
  }
  m_scheduler->run([self = ZmRef<Requests>{this}]() {
    self->deactivated_();
  }, m_sid);
}

void Requests::deactivated_()
{
  m_draining.clean();
  m_closing = false;
  auto complete = ZuMv(m_deactivated);
  if (complete) complete();
}

} // namespace Zum
