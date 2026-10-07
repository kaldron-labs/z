//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Peer-owned live requests and bounded recent ID history

#ifndef ZjrpcInbound_HH
#define ZjrpcInbound_HH

#ifndef ZjrpcLib_HH
#include <zlib/ZjrpcLib.hh>
#endif

#include <zlib/ZmCache.hh>
#include <zlib/ZmNoLock.hh>

#include <zlib/Zjrpc.hh>

namespace Zjrpc {

struct InboundEntry {
  using CancelFn = bool (*)(void *, const ID &, ZuCSpan);
  using CloseFn = void (*)(void *);

  ID		id;
  void		*object = nullptr;
  CancelFn	cancelFn = nullptr;
  CloseFn	closeFn = nullptr;

  template <typename Work>
  void bind(ID id_, Work *work) {
    id = ZuMv(id_);
    object = work;
    cancelFn = [](void *object_, const ID &id_, ZuCSpan reason) {
      return static_cast<Work *>(object_)->cancel_(id_, reason);
    };
    closeFn = [](void *object_) {
      static_cast<Work *>(object_)->close_();
    };
  }

  bool cancel(ZuCSpan reason) {
    return object && cancelFn(object, id, reason);
  }
  void close() { if (object) closeFn(object); }
};

inline const ID &InboundEntry_KeyAxor(const InboundEntry &entry) {
  return entry.id;
}

ZmHashDerive(InboundHash, InboundEntry,
  (ZmHashNode<InboundEntry,
    ZmHashKey<InboundEntry_KeyAxor,
      ZmHashShadow<ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zjrpc.Inbound">>>>>));

template <typename Heap = ZuVoid>
struct InboundTable_ : public Heap, public InboundHash {
  ZuDerive_(InboundTable_, InboundHash)
};
ZuDerive(InboundTableHeap, (ZmHeap<"Zjrpc.Inbound.Table", InboundTable_<>>));
ZuDerive(InboundTable, (InboundTable_<InboundTableHeap>));

// ZmCache shares nodes with its LRU and hash; intrusive ownership keeps an
// evicted node alive until both memberships have released it.
struct HistoricID : public ZmObject {
  ID id;
  HistoricID(ID id_) : id{ZuMv(id_)} { }
};
inline const ID &HistoricID_KeyAxor(const HistoricID &entry) { return entry.id; }
using IDHistory = ZmCache<HistoricID,
  ZmCacheKey<HistoricID_KeyAxor,
    ZmCacheLock<ZmNoLock, ZmCacheHeapID<"Zjrpc.History">>>>;

class InboundCalls {
public:
  // ZmHashParams rounds capacity up to a power of two, minimum eight.
  InboundCalls(unsigned size = Default::HistSize) :
    m_history{ZmHashParams{size}} { }

  unsigned histSize() const { return m_history.size(); }
  unsigned count() const { return m_live->count_(); }

  void init(unsigned size) {
    m_history.~IDHistory();
    new (&m_history) IDHistory{ZmHashParams{size}};
  }

  template <typename Work>
  bool begin(const ID &id, Work *work) {
    if (id.is<void>()) return true;
    if (m_live->findPtr(id) || m_history.find(id)) return false;
    auto entry = static_cast<InboundHash::Node *>(work);
    entry->bind(id, work);
    m_live->addNode(entry);
    return true;
  }

  void finish(const ID &id, const void *work) {
    auto entry = m_live->findPtr(id);
    if (!entry || entry->object != work) return;
    auto removed = m_live->delNode(entry);
    m_history.add(new IDHistory::Node{ZuMv(removed->id)});
  }

  bool cancel(const ID &id, ZuCSpan reason) {
    auto entry = m_live->findPtr(id);
    return entry && entry->cancel(reason);
  }

  bool close(unsigned limit) {
    for (unsigned n = 0; n < limit; ++n) {
      InboundHash::NodeMvRef entry = nullptr;
      {
	auto i = m_live->iter();
	if (!i()) break;
	entry = i.del();
      }
      entry->close();
    }
    if (m_live->count_()) return false;
    init(histSize());
    return true;
  }

private:
  ZmRef<InboundTable> m_live = new InboundTable{};
  IDHistory m_history;
};

} // Zjrpc

#endif /* ZjrpcInbound_HH */
