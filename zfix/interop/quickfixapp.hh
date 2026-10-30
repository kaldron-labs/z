//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef quickfixapp_HH
#define quickfixapp_HH

#include <vector>
#include <quickfix/config.h>
#include <quickfix/MessageStore.h>
#include <quickfix/Mutex.h>
#include <zlib/ZmHeap.hh>
#include "quickfixwire.hh"
#include "../util/load_cache.hh"

namespace QfLoad {

// The SDK calls this foreign interface from its IO thread and send callers.
// Its mutex protects SDK-owned storage, not application Run/Broker state.
class Store : public ZmNHeap<Store>, public FIX::MessageStore {
friend inline ZuStringT<"Zfix.Qf.Store"> ZmNHeapID(Store *);
public:
  Store(const FIX::UtcTimeStamp &now, unsigned count, uint64_t bytes) :
    m_messages{count, bytes}, m_creation{now}, m_limit{count}, m_max{bytes} { }

  bool set(FIX::SEQNUM, const std::string &) override;
  void get(FIX::SEQNUM, FIX::SEQNUM, std::vector<std::string> &) const override;
  FIX::SEQNUM getNextSenderMsgSeqNum() const override;
  FIX::SEQNUM getNextTargetMsgSeqNum() const override;
  void setNextSenderMsgSeqNum(FIX::SEQNUM) override;
  void setNextTargetMsgSeqNum(FIX::SEQNUM) override;
  void incrNextSenderMsgSeqNum() override;
  void incrNextTargetMsgSeqNum() override;
  FIX::UtcTimeStamp getCreationTime() const override;
  void reset(const FIX::UtcTimeStamp &) override;

  void refresh() override { }

  uint64_t peak() const;
  uint64_t misses() const;
  unsigned count() const;
  // Cold main-only fixture checkpoint; failed loading discards this fresh store.
  bool load(const char *, const FIX::SessionID &, uint64_t completed, const char *dictionary);
  bool save(const char *, const FIX::SessionID &, uint64_t completed) const;
  void resetPolicy(bool armed);
private:
  mutable FIX::Mutex		m_lock;
  Cache<FIX::SEQNUM, Text>	m_messages;
  FIX::UtcTimeStamp		m_creation;
  FIX::SEQNUM			m_sender = 1, m_target = 1;
  mutable uint64_t		m_misses = 0;
  const unsigned		m_limit;
  const uint64_t		m_max;
  // unrestricted SDK contract, closed, or one agreed reset
  int8_t			m_reset = -1;
};

int run(bool server, int argc, char **argv);
} // QfLoad

#endif /* quickfixapp_HH */
