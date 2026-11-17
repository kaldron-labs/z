//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Persistent QUIC queue identity and owner-local container telemetry.

#ifndef ZquicQueue_HH
#define ZquicQueue_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZuArray.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZePlatform.hh>
#include <zlib/ZtcHub.hh>

namespace Zquic_ {

class QueueCatalog;
struct QueueEntry {
  QueueCatalog	*catalog = nullptr;
  Ztc::Queue	*queue = nullptr;
};
using QueueEntries = ZmList<QueueEntry,
  ZmListNode<QueueEntry, ZmListShadow<>>>;

// Protect only telemetry membership and borrowed object lifetime. Protocol
// containers remain owner-exclusive; counter reads deliberately stay unclean.
// Callbacks borrow the entry for the duration of the call and must not mutate
// membership. Registration/retirement occur only at queue lifecycle boundaries.
class alignas(Zm::CacheLineSize) QueueCatalog {
public:
  ~QueueCatalog() { ZmAssert(m_entries.empty_()); }
  void add(QueueEntries::Node *entry) {
    ZmGuard guard(m_lock);
    ZmAssert(!entry->catalog);
    entry->catalog = this;
    m_entries.pushNode(entry);
  }
  void del(QueueEntries::Node *entry) {
    ZmGuard guard(m_lock);
    m_entries.delNode(entry);
    entry->catalog = nullptr;
  }
  void clear() {
    ZmGuard guard(m_lock);
    while (auto entry = m_entries.shift()) entry->catalog = nullptr;
  }
  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const {
    ZmGuard guard(m_lock);
    unsigned n = 0;
    auto i = m_entries.citer();
    while (auto entry = i()) { ++n; fn(entry->queue); }
    return n;
  }

private:
  mutable ZmPLock m_lock;
  QueueEntries m_entries;
};

class alignas(Zm::CacheLineSize) QueueInfo : public Ztc::Queue {
public:
  QueueInfo() { m_entry.queue = this; }
  QueueInfo(const QueueInfo &) = delete;
  QueueInfo &operator =(const QueueInfo &) = delete;

  void joinQueue(QueueCatalog &queues) { queues.add(&m_entry); }
  void finalQueue() { if (m_entry.catalog) m_entry.catalog->del(&m_entry); }

  void initQueue(Ztc::Hub *hub, ZuCSpan prefix, ZuCSpan role,
      Ztc::QueueType::T type, unsigned size = 0) {
    ZtString<> id;
    id << prefix << ':' << role;
    if (id.length() > ZuIDSize)
      throw ZeEXCEPT(Error, "Zquic", "queue telemetry ID exceeds ZuID capacity");
    m_config = {hub ? ZuID{hub->telKey().p<1>()} : ZuID{}, ZuID{id}, size, type};
  }
  TelKey telKey() const override {
    return {m_config.ownerID, m_config.id, m_config.type};
  }
  void queueTelemetry(Ztc::QueueTelemetry &data) const {
    data = {};
    auto key = telKey();
    data.ownerID = key.p<0>();
    data.id = key.p<1>();
    data.type = key.p<2>();
    data.size = m_config.size;
  }

  // Leave room for a 64-bit stream ID and role. Full link identity remains in
  // LinkTelemetry; only long queue prefixes use the hub's cold lifetime ordinal.
  static ZuID prefix(ZuCSpan id, uint64_t ordinal) {
    if (id.length() <= 24) return ZuID{id};
    return ZuID{} << "quic:" << ZuBox<uint64_t>{ordinal}.hex();
  }

private:
  // Keep telemetry links private so Storage owns the queue's Node/data API.
  QueueEntries::Node m_entry;

  // Its complete extent prevents a following container from reusing the cold
  // identity cache lines, including base-class tail padding.
  struct alignas(Zm::CacheLineSize) Config {
    ZuID ownerID;
    ZuID id;
    unsigned size = 0;
    Ztc::QueueType::T type = -1;
  } m_config;
};

struct QueueCount {
  template <typename Storage>
  static void telemetry(const Storage &queue, Ztc::QueueTelemetry &data) {
    data.count = queue.count_();
  }
};

// The protocol owns admission and reclamation. Keep the actual slot storage
// with its publication cursors; telemetry never examines an in-use slot.
// Producer/consumer types have complete, separately aligned cache-line extents.
template <typename Slot, typename Producer, typename Consumer, unsigned N>
struct PublicationQueue final : public QueueInfo {
  Producer producer;
  Consumer consumer;
  ZuArray<Slot, N> slots = ZuArray<Slot, N>(N);

  void telemetry(Ztc::QueueTelemetry &data) const override {
    queueTelemetry(data);
    uint64_t published = producer.sequence;
    uint64_t retired = consumer.readSequence;
    // Exceptional handoffs also advance reclamation. Concurrent observation
    // can see a newer consumer, so never expose unsigned underflow as backlog.
    uint64_t count = published > retired ? published - retired : 0;
    data.count = count < N ? count : N;
    data.full = producer.full;
  }
};
struct ArrayCount {
  template <typename Storage>
  static void telemetry(const Storage &queue, Ztc::QueueTelemetry &data) {
    data.count = queue.length();
  }
};
struct PQueueCount {
  template <typename Storage>
  static void telemetry(const Storage &queue, Ztc::QueueTelemetry &data) {
    data.count = queue.count_();
    // Element units vary by queue role; do not mislabel packet numbers as bytes.
    uint64_t inElems, outElems;
    queue.stats(data.inCount, inElems, data.outCount, outElems);
  }
};

template <typename Storage, typename Counts = QueueCount>
class Queue : public QueueInfo, public Storage {
public:
  using Storage::Storage;
  Queue() = default;
  ~Queue() { finalQueue(); }
  Queue(const Queue &) = delete;
  Queue &operator =(const Queue &) = delete;
  // Container moves transfer work, never the owner's identity.
  Queue(Queue &&queue) : Storage{ZuMv(static_cast<Storage &>(queue))} { }
  Queue &operator =(Queue &&queue) {
    Storage::operator =(ZuMv(static_cast<Storage &>(queue)));
    return *this;
  }

  void queueFull() { ++m_full; } // owner only, on capacity refusal/eviction
  void telemetry(Ztc::QueueTelemetry &data) const override {
    queueTelemetry(data);
    Counts::telemetry(static_cast<const Storage &>(*this), data);
    data.full = m_full;
  }

private:
  uint32_t m_full = 0;
};

} // Zquic_

#endif /* ZquicQueue_HH */
