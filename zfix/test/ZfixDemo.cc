//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZfixDemo.hh"

namespace ZfixDemo {

ZfStructImpl(Party); ZfStructImpl(Party, FIX);
ZfStructImpl(Request); ZfStructImpl(Request, FIX);
ZfStructImpl(Report); ZfStructImpl(Report, FIX);
ZfStructImpl(Event); ZfStructImpl(Event, FIX);
ZfStructImpl(Login); ZfStructImpl(Login, FIX);
ZfStructImpl(Summary); ZfStructImpl(Summary, JSON);

bool Calls::add(uint64_t key)
{
  if (!key || m_calls.findPtr(key) || m_calls.count_() >= m_limit) return false;
  m_calls.add(key, Call{}); ++m_stats.registered; return true;
}
void Calls::completed(uint64_t key)
{
  ++m_stats.complete;
  if (!m_stats.firstComplete) m_stats.firstComplete = key;
}
bool Calls::report(const Report &report)
{
  auto node = m_calls.findPtr(report.key);
  if (!node) { ++m_stats.unknown; return false; }
  auto &call = node->val();
  if (!report.exec || call.rejected || (report.kind != "A" && report.kind != "F")) {
    ++m_stats.invalid; return false;
  }
  bool before = call.complete();
  auto &slot = report.kind == "A" ? call.ack : call.fill;
  auto other = report.kind == "A" ? call.fill : call.ack;
  if (slot) {
    if (slot == report.exec) ++m_stats.duplicate;
    else ++m_stats.invalid;
    return false;
  }
  if (other == report.exec) { ++m_stats.invalid; return false; }
  slot = report.exec;
  if (!before && call.complete()) { completed(report.key); return true; }
  return false;
}
bool Calls::reject(uint64_t key)
{
  auto node = m_calls.findPtr(key);
  if (!node) { ++m_stats.unknown; return false; }
  auto &call = node->val();
  if (call.rejected) { ++m_stats.duplicate; return false; }
  if (call.complete()) { ++m_stats.invalid; return false; }
  call.rejected = true; ++m_stats.rejected; completed(key); return true;
}
unsigned Results::admit(uint64_t key, unsigned qty, bool replay)
{
  if (auto node = m_results.findPtr(key)) {
    if (node->val().qty != qty) return Decision::Conflict;
    ++m_stats.cached; return Decision::Cached;
  }
  if (replay) { ++m_stats.unknownReplay; return Decision::UnknownReplay; }
  if (!key || !qty || m_results.count_() >= m_limit || m_nextExec > UINT64_MAX - 2)
    return Decision::Refused;
  Result result{qty, ++m_nextExec, ++m_nextExec};
  m_results.add(key, result); ++m_stats.fresh; return Decision::New;
}
bool Results::restore(uint64_t key, Result result)
{
  if (!key || !result.qty || !result.ack || !result.fill || result.ack == result.fill ||
      m_results.findPtr(key) || m_results.count_() >= m_limit) return false;
  m_results.add(key, result);
  if (result.ack > m_nextExec) m_nextExec = result.ack;
  if (result.fill > m_nextExec) m_nextExec = result.fill;
  ++m_stats.restored; return true;
}
const Result *Results::find(uint64_t key) const
{
  auto node = m_results.findPtr(key); return node ? &node->val() : nullptr;
}
bool Store::associate(Zfix::TxBody::Ref body, Zfix::TxContext context)
{
  if (!body || !context.epoch || !context.seqNo) return false;
  if (auto node = m_records.findPtr(Key{context.epoch, context.seqNo}))
    return node->val().body == body;
  uint64_t bytes = body->memory() + ZmHeapAllocSize<sizeof(Map::Node)>{};
  if (m_records.count_() >= m_count || bytes > m_limit
    || m_bytes > m_limit - bytes) return false;
  m_records.add(Key{context.epoch, context.seqNo}, Zfix::TxHistory{body, {}, context.epoch,
    context.seqNo});
  m_bytes += bytes; return true;
}
bool Store::published(Zfix::TxContext context, ZuDateTime time)
{
  auto node = m_records.findPtr(Key{context.epoch, context.seqNo});
  if (!node) return false;
  if (!context.replay) node->val().originalTime = time;
  return true;
}
bool Store::erase(uint64_t epoch, uint64_t seq)
{
  auto node = m_records.del(Key{epoch, seq});
  if (!node) return false;
  m_bytes -= node->val().body->memory() + ZmHeapAllocSize<sizeof(Map::Node)>{}; return true;
}
uint64_t Store::key(uint64_t epoch, uint64_t seq) const
{
  auto node = m_records.findPtr(Key{epoch, seq}); return node ? node->val().body->id() : 0;
}
Zfix::TxReload Store::fetch(uint64_t epoch, uint64_t seq) const
{
  auto node = m_records.findPtr(Key{epoch, seq});
    return node ? Zfix::TxReload{node->val()} : Zfix::TxReload{};
}
bool Store::restore(Zfix::TxHistory history)
{
  if (!*history.originalTime || !associate(history.body, {history.epoch, 0,
    history.seqNo})) return false;
  auto node = m_records.findPtr(Key{history.epoch, history.seqNo});
    node->val() = ZuMv(history); return true;
}
} // ZfixDemo
