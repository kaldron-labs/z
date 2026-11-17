//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// I/O buffers

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>

namespace Zi {

ZtEnumImplNS(IOBufType);
ZtEnumImplNS(IOBufWaitResult);

IOBufWait::~IOBufWait() { ZmAssert_(!busy() && !queued); }

void IOBufWait::start(
  IOBufAllocator *allocator, unsigned sid, void *owner,
  uint32_t length, uint32_t mps, AllocFn fn)
{
  ZmAssert_(!busy() && allocator->mx && allocator->mx->invoked(sid));
  m_allocator = allocator;
  m_owner = owner;
  m_sid = sid;
  m_length = length;
  m_mps = mps;
  m_fn = ZuMv(fn);
  m_active = true;
  allocator->mx->txRun([this] { start_(); });
}

void IOBufWait::start_()
{
  ZmAssert_(m_allocator->mx->invoked(m_allocator->mx->txThread()));
  m_provider = nullptr;
  m_grant = nullptr;
  m_pending = true;
  m_allocator->waitBuf_(*this);
}

void IOBufWait::wait(IOBufPool *provider, GrantFn grant)
{
  m_provider = provider;
  m_grant = grant;
  provider->wait(*this);
}

void IOBufWait::grant(uint8_t *data)
{
  ZmAssert_(data && m_grant);
  m_raw = data;
  complete(IOBufWaitResult::Ready);
}

void IOBufWait::ready(ZmRef<IOBuf> buf)
{
  ZmAssert_(buf);
  m_buf = ZuMv(buf);
  complete(IOBufWaitResult::Ready);
}

void IOBufWait::fail() { complete(IOBufWaitResult::Failed); }

void IOBufWait::complete(IOBufWaitResult::T result)
{
  ZmAssert_(m_pending && !queued);
  m_pending = false;
  m_result = result;
  m_allocator->mx->run([this] { completed(); }, m_sid);
}

void IOBufWait::completed()
{
  auto buf = ZuMv(m_buf);
  if (auto raw = m_raw) {
    m_raw = nullptr;
    buf = m_grant(m_allocator, raw, m_owner, m_length, m_mps);
  }
  auto result = m_result;
  auto fn = ZuMv(m_fn);
  m_active = false;
  if (fn) fn(result, ZuMv(buf));
}

void IOBufWait::cancel(DoneFn fn)
{
  auto mx = m_allocator->mx;
  ZmAssert_(mx->invoked(m_sid) && m_active && !m_cancelling);
  m_cancelling = true;
  mx->txRun([this, fn = ZuMv(fn)]() mutable {
    if (m_pending) {
      if (m_provider) m_provider->cancel(*this);
      complete(IOBufWaitResult::Cancelled);
    }
    m_allocator->mx->run([this, fn = ZuMv(fn)]() mutable {
      m_cancelling = false;
      if (fn) fn();
    }, m_sid);
  });
}

void IOBufAllocator::waitBuf(IOBufWait &request, unsigned sid, void *owner,
  uint32_t length, uint32_t mps, IOBufWait::AllocFn fn)
{
  request.start(this, sid, owner, length, mps, ZuMv(fn));
}

void IOBufAllocator::cancelBuf(IOBufWait &request, IOBufWait::DoneFn fn)
{
  request.cancel(ZuMv(fn));
}

void IOBufAllocator::waitBuf_(IOBufWait &request)
{
  auto buf = allocBuf(request.owner(), request.length(), request.mps());
  if (buf) request.ready(ZuMv(buf));
  else request.fail();
}

void IOBufPool_::wait(IOBufWait &request)
{
  auto data = alloc();
  if (data) request.grant(data);
  else request.fail();
}

} // Zi
