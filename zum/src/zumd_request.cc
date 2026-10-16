//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <zlib/zumd_request.hh>

namespace Zum {

Requests::RequestData::RequestData(Requests *requests, Fn cancel) :
  m_requests{requests}, m_cancel{ZuMv(cancel)} { }

void Requests::RequestData::start_(ZuTime deadline)
{
  m_requests->start_(this, deadline);
}

bool Requests::RequestData::finish_()
{
  return m_requests->finish_(this);
}

void Requests::RequestData::cancel_()
{
  auto cancel = ZuMv(m_cancel);
  if (finish_() && cancel) cancel();
}

void Requests::RequestData::timeout_()
{
  ZmRef<Node> self{static_cast<Node *>(this)};
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

void Requests::activate()
{
  m_up = 1;
}

bool Requests::admit_(Node *request, ZuTime deadline)
{
  if (!m_up || m_closing) {
    request->cancel_();
    return false;
  }
  request->start_(deadline);
  return true;
}

void Requests::start_(RequestData *request, ZuTime deadline)
{
  Node *node = static_cast<Node *>(request);
  request->m_list = RequestListState::Active;
  m_requests.pushNode(node);
  arm_(request, &request->m_timer, deadline);
}

void Requests::arm_(
    RequestData *request, ZmScheduler::Timer *timer, ZuTime deadline)
{
  m_scheduler->add(timer, deadline, ZmScheduler::Update,
    [request](auto &&arm) { return arm([request]() { request->timeout_(); }); },
    m_sid);
}

void Requests::del_(ZmScheduler::Timer *timer)
{
  m_scheduler->del(timer);
}

bool Requests::finish_(RequestData *request)
{
  if (request->m_done) return false;
  request->m_done = true;
  del_(&request->m_timer);
  remove_(request);
  request->m_cancel = RequestData::Fn{};
  return true;
}

void Requests::remove_(RequestData *request)
{
  Node *node = static_cast<Node *>(request);
  switch (request->m_list) {
  case RequestListState::Active:
    m_requests.delNode(node);
    break;
  case RequestListState::Draining:
    m_draining.delNode(node);
    break;
  case RequestListState::None:
    break;
  }
  request->m_list = RequestListState::None;
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
  while (auto request = m_requests.shift()) {
    Node *node = request.ptr();
    node->m_list = RequestListState::Draining;
    m_draining.pushNode(ZuMv(request));
    node->cancel_();
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
