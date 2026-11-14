//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmAssert.hh>
#include <zlib/ZutcpAlloc.hh>

namespace Zutcp {

RxAccount::~RxAccount() { ZmAssert_(!frames); }

RxFrames::RxFrames(unsigned count, unsigned limit) : m_limit{limit}
{
  ZmAssert_(count && limit && limit <= count);
  m_slots.length(count);
}

RxFrames::~RxFrames()
{
  ZmAssert_(!m_retained && unsigned(m_returns) == NoSlot);
}

bool RxFrames::retain(unsigned id, RxAccount *account)
{
  reconcile();
  ZmAssert_(id < m_slots.length() && account);
  auto &slot = m_slots[id];
  if (slot.account) {
    if (slot.account == account) return true; // aliases charge once
    ++m_refused;
    return false;
  }
  if (m_retained >= m_limit || account->frames >= account->limit) {
    ++m_refused;
    return false;
  }
  slot.account = account;
  ++account->frames;
  ++m_retained;
  ++m_admitted;
  return true;
}

void RxFrames::returned(unsigned id)
{
  ZmAssert_(id < m_slots.length());
  auto &slot = m_slots[id];
  unsigned head;
  do {
    head = m_returns;
    slot.next = head;
  } while (m_returns.cmpXch(id, head) != head);
}

unsigned RxFrames::reconcile()
{
  unsigned retired = 0;
  auto id = m_returns.xch(NoSlot);
  while (id != NoSlot) {
    auto &slot = m_slots[id];
    auto next = slot.next;
    if (slot.account) {
      ZmAssert_(slot.account->frames && m_retained);
      --slot.account->frames;
      --m_retained;
      slot.account = nullptr;
      ++retired;
    }
    id = next;
  }
  m_retired += retired;
  return retired;
}

RxFrameStats RxFrames::stats() const
{
  return {m_admitted, m_refused, m_retired, unsigned(m_slots.length()),
    m_limit, m_retained};
}

void RxFrames::limit(unsigned value)
{
  ZmAssert_(value && value <= m_slots.length());
  reconcile();
  m_limit = value;
}

Allocator::Allocator(unsigned count, unsigned bytes, unsigned head, unsigned tail)
: pool{count, bytes}
{
  ZmAssert_(head <= bytes && tail <= bytes - head);
  size = bytes;
  headRoom = head;
  tailRoom = tail;
}

uint8_t *Allocator::alloc() { return pool.alloc(); }
void Allocator::free(uint8_t *packet)
{
  // Publish before raw free makes this slot eligible for FILL again. Rx must
  // reconcile before dispatching its next incarnation. No account is touched
  // by an off-owner producer and no access to packet bytes follows raw free.
  if (rxFrames) rxFrames->returned(pool.slot(packet));
  pool.free(packet);
  if (returned) returned();
}

PacketLease::~PacketLease() { reset(); }

void PacketLease::adopt(Zi::IOBufAllocator *allocator, uint8_t *data, unsigned size)
{
  ZmAssert_(allocator && data);
  if (m_data) {
    ZmAssert_(m_allocator == allocator);
    return;
  }
  m_allocator = allocator;
  m_data = data;
  m_size = size;
}

void PacketLease::reset()
{
  auto data = m_data;
  m_data = nullptr;
  if (data) m_allocator->free(data, m_size, true);
  m_allocator = nullptr;
  m_size = 0;
}

RxAllocator::RxAllocator(Allocator *source, unsigned head, unsigned n, unsigned alignment)
: ingress{source}, count{n}, align{alignment}
{
  ZmAssert_(head <= source->size);
  size = source->size;
  headRoom = head;
  tailRoom = 0;
}

void RxAllocator::free(uint8_t *packet) { ingress->free(packet); }

} // namespace Zutcp
