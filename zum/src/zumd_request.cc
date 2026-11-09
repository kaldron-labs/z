//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <zlib/zumd_request.hh>

namespace Zum {

Requests::RequestData::RequestData(Requests *requests, Fn cancel) :
  m_requests{requests}, m_scheduler{requests->scheduler()},
  m_sid{requests->sid()}, m_cancel{ZuMv(cancel)} { }

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
  if (m_done.load_()) return;
  ZmRef<Node> self{static_cast<Node *>(this)};
  cancel_();
}

bool Requests::init(ZmScheduler *scheduler, unsigned sid, unsigned limit)
{
  if (Engine::state() != ZmEngineState::Stopped ||
      !scheduler || !sid || sid > scheduler->params().nThreads() || !limit)
    return false;
  m_scheduler = scheduler;
  m_sid = sid;
  m_limit = limit;
  return true;
}

void Requests::activate(CtrlFn complete)
{
  Engine::start(ZuMv(complete));
}

bool Requests::admit_(Node *request, ZuTime deadline)
{
  if (!active()) {
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
    case RequestListState::Active: {
      auto held = m_requests.delNode(node);
      m_retiring.pushNode(ZuMv(held));
      break;
    }
    case RequestListState::None:
      m_retiring.pushNode(node);
      break;
    default:
      ZmAssert_(false);
  }
  request->m_list = RequestListState::Retiring;
  retire_0();
}

void Requests::retire_0()
{
  if (m_retirePosted) return;
  m_retirePosted = true;
  // del() precedes this owner continuation. Any timeout that was already
  // enqueued can still inspect the retained node, whose done flag is set.
  m_scheduler->run([this]() { retire_1(); }, m_sid);
}

void Requests::retire_1()
{
  m_retirePosted = false;
  unsigned left = RequestWork::Batch;
  while (left--) {
    auto request = m_retiring.shift();
    if (!request) break;
    request->m_list = RequestListState::None;
    request = nullptr;
    --m_credit; // retirement storage is part of the admission bound
  }
  if (!m_retiring.empty_()) retire_0();
  else if (m_draining) deactivated_();
}

void Requests::start_()
{
  if (!m_scheduler) { Engine::started(false); return; }
  m_scheduler->run([this]() {
    ZmAssert_(m_credit.load_() == RequestCredit::Closed);
    ZmAssert_(m_requests.empty_() && m_retiring.empty_() && !m_retirePosted && !m_draining);
    m_credit = 0;
    Engine::started(true);
  }, m_sid);
}

void Requests::stop_()
{
  m_credit |= RequestCredit::Closed;
  m_scheduler->run([this]() { deactivate_(); }, m_sid);
}

void Requests::deactivate(CtrlFn complete)
{
  Engine::stop(ZuMv(complete));
}

void Requests::deactivate_()
{
  unsigned left = RequestWork::Batch;
  while (left--) {
    auto request = m_requests.headNode();
    if (!request) break;
    request->cancel_();
  }
  if (!m_requests.empty_()) {
    m_scheduler->run([this]() { deactivate_(); }, m_sid);
    return;
  }
  // One ordered barrier covers cancellation and already-enqueued timers.
  // Later reserved admissions notify through their actual retirement.
  m_scheduler->run([this]() {
    m_draining = true;
    deactivated_();
  }, m_sid);
}

void Requests::deactivated_()
{
  if ((m_credit.load_() & RequestCredit::Count) ||
      !m_retiring.empty_() || m_retirePosted) return;
  m_draining = false;
  Engine::stopped(true);
}

} // namespace Zum
