//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <time.h>
#include "load.hh"

namespace ZfixLoad {

ZtEnumImplNS(Mode);
ZtEnumImplNS(Mode, CLI);
ZtEnumImplNS(Version);
ZtEnumImplNS(Version, Wire);
ZtEnumImplNS(Scenario);
ZtEnumImplNS(Scenario, CLI);

ZfStructImpl(Quantiles); ZfStructImpl(Quantiles, JSON);
ZfStructImpl(Progress); ZfStructImpl(Progress, JSON);
Model::Model(Shape shape, uint64_t seed) : m_seed{seed} {
  m_text.length(shape.text);
  for (unsigned i = 0; i < shape.text; ++i) m_text[i] = 'a' + i % 26;
  m_order.text = m_text;
  m_order.parties.size(shape.parties);
  for (unsigned i = 0; i < shape.parties; ++i) {
    Party party{"PARTY", "D", 1 + i % 2, {}};
    party.subs.size(shape.subs);
    for (unsigned j = 0; j < shape.subs; ++j) party.subs.push(SubParty{"DESK", 1 + j % 2});
    m_order.parties.push(ZuMv(party));
  }
}
Order &Model::next(uint64_t key, ZuDateTime time) {
  // Fixed SplitMix64 permutation: identical inputs in every implementation.
  uint64_t v = key + m_seed + uint64_t(0x9e3779b97f4a7c15);
  v = (v ^ (v >> 30)) * uint64_t(0xbf58476d1ce4e5b9);
  v = (v ^ (v >> 27)) * uint64_t(0x94d049bb133111eb); v ^= v >> 31;
  return next(key, time, 1 + v % 999, ZuDecimal{int64_t(10000 + v % 100000), 2});
}
Order &Model::next(uint64_t key, ZuDateTime time, unsigned qty, ZuDecimal price) {
  m_order.key = key; m_order.time = time;
  m_order.qty = qty; m_order.price = price;
  return m_order;
}
Report report(const Order &o, uint64_t exec, ZuCSpan kind,
    uint64_t order, uint64_t original) {
  if (kind != "0" && kind != "F" && kind != "4" && kind != "5") return {};
  bool fill = kind == "F", cancel = kind == "4";
  auto status = fill ? ZuCSpan{"2"} : kind == "5" ? ZuCSpan{"0"} : kind;
  return {o.key, order ? order : o.key, exec, original, kind, status, o.symbol, o.side,
    o.qty, fill || cancel ? 0U : o.qty, fill ? o.qty : 0U, fill ? o.qty : 0U,
    o.price, fill ? o.price : ZuDecimal{0}, fill ? o.price : ZuDecimal{0}, o.time, o.text};
}
bool valid(const Order &o) {
  return o.key && o.qty && !ZuCmp<unsigned>::null(o.qty) &&
    o.price > ZuDecimal{0} && *o.time && o.symbol == "ZFIX" &&
    o.side == "1" && o.type == "2" && o.handling == "1";
}
static bool matches(const Report &r, uint64_t key, unsigned qty,
    ZuDecimal price, ZuCSpan symbol, ZuCSpan side) {
  bool fill = r.kind == "F", cancel = r.kind == "4";
  return r.key == key && r.order && r.exec && r.symbol == symbol && r.side == side &&
    r.qty == qty && r.price == price &&
    (r.kind == "0" || fill || cancel || r.kind == "5") &&
    r.status == (fill ? ZuCSpan{"2"} : r.kind == "5" ? ZuCSpan{"0"} : r.kind) &&
    r.leaves == (fill || cancel ? 0U : qty) && r.cum == (fill ? qty : 0U) &&
    r.lastQty == (fill ? qty : 0U) && r.avg == (fill ? price : ZuDecimal{0}) &&
    r.lastPx == (fill ? price : ZuDecimal{0});
}
bool matches(const Report &r, const Order &o) {
  return matches(r, o.key, o.qty, o.price, o.symbol, o.side);
}
unsigned workflow(Scenario::T scenario) {
  switch (scenario) {
    case Scenario::Orders:
    case Scenario::Groups: return Workflow::Orders;
    case Scenario::Cancel: return Workflow::Cancel;
    case Scenario::Replace: return Workflow::Replace;
    default: return Workflow::Invalid;
  }
}
static int sampleCmp(const void *a, const void *b) {
  auto x = *static_cast<const uint64_t *>(a), y = *static_cast<const uint64_t *>(b);
  return (x > y) - (x < y);
}
Quantiles Samples::finish() {
  if (!m_count) return {};
  // Sample values can be highly concentrated. Retain libc's sort rather than
  // a two-way partition that repeatedly revisits a mostly equal partition.
  qsort(m_values.data(), m_count, sizeof(uint64_t), sampleCmp);
  auto rank = [this](unsigned p) { return m_values[(uint64_t(m_count) * p + 999) / 1000 - 1]; };
  return {m_count, rank(500), rank(950), rank(990), rank(999), m_values[m_count - 1]};
}
uint64_t monotonic() {
#ifdef _WIN32
  static const int64_t frequency = [] { LARGE_INTEGER v; QueryPerformanceFrequency(&v);
    return v.QuadPart; }();
  LARGE_INTEGER v; QueryPerformanceCounter(&v);
  return uint64_t(uint128_t(v.QuadPart) * 1000000000 / frequency);
#else
  timespec value; clock_gettime(CLOCK_MONOTONIC, &value);
  return uint64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
#endif
}
bool Calls::add(const Order &o, uint64_t sent, uint64_t intended, bool warmup) {
  ++m_progress.offered;
  if (!valid(o) || !sent || !intended || intended > sent ||
    m_calls.findPtr(o.key) || m_calls.count_() >= m_limit) {
    ++m_progress.refused; return false;
  }
  m_calls.add(o.key, Call{.sent = sent, .intended = intended,
    .qty = o.qty, .price = o.price, .warmup = warmup});
  ++m_progress.admitted; ++m_progress.inFlight;
  if (m_progress.inFlight > m_progress.peak) m_progress.peak = m_progress.inFlight;
  return true;
}
bool Calls::follow(uint64_t original, const Order &o) {
  auto node = m_calls.findPtr(original);
  if (!node || !valid(o) || o.key == original || m_flow == Workflow::Orders) return false;
  auto &call = node->val();
  if (!call.ack || call.follow) return false;
  call.follow = o.key; call.nextQty = o.qty; call.nextPrice = o.price;
  return true;
}
bool Calls::refused(uint64_t key) {
  auto node = m_calls.findPtr(key);
  if (!node || node->val().ack || node->val().terminal) return false;
  m_calls.del(key);
  --m_progress.admitted; --m_progress.inFlight; ++m_progress.refused; return true;
}
unsigned Calls::receive(const Report &r, uint64_t now, Samples *acks, Samples *terminals,
    Samples *intendedAcks, Samples *intendedTerms) {
  ++m_progress.reports;
  auto root = r.original ? r.original : r.key;
  auto node = m_calls.findPtr(root);
  if (!node) { ++m_progress.unknown; return Outcome::Unknown; }
  auto &call = node->val();
  bool ack = r.kind == "0", changed = r.kind == "5";
  bool follow = m_flow != Workflow::Orders && !ack;
  auto expected = follow ? call.follow : root;
  auto qty = follow ? call.nextQty : call.qty;
  auto price = follow ? call.nextPrice : call.price;
  bool kind = ack || (m_flow == Workflow::Cancel ? r.kind == "4" :
    r.kind == "F" || (m_flow == Workflow::Replace && changed));
  if (!kind || r.original != (follow ? root : 0) || !matches(r, expected, qty, price, "ZFIX",
    "1") ||
      (m_flow == Workflow::Replace && r.kind == "F" && !call.changed) ||
      (call.order && call.order != r.order) || now < call.sent) {
    ++m_progress.invalid; return Outcome::Invalid;
  }
  auto &slot = ack ? call.ack : changed ? call.changed : call.terminal;
  if (slot) {
    if (slot == r.exec) { ++m_progress.duplicate; return Outcome::Duplicate; }
    ++m_progress.invalid; return Outcome::Invalid;
  }
  if (call.ack == r.exec || call.changed == r.exec || call.terminal == r.exec) {
    ++m_progress.invalid; return Outcome::Invalid;
  }
  call.order = r.order; slot = r.exec;
  if (ack) {
    ++m_progress.ack;
    if (!call.warmup && acks && !acks->add(now - call.sent)) ++m_progress.invalid;
    if (!call.warmup && intendedAcks && !intendedAcks->add(now - call.intended))
      ++m_progress.invalid;
  }
  if (!ack && !changed && !call.warmup && terminals && !terminals->add(now - call.sent))
    ++m_progress.invalid;
  if (!ack && !changed && !call.warmup && intendedTerms &&
    !intendedTerms->add(now - call.intended)) ++m_progress.invalid;
  if (call.ack && call.terminal && (m_flow != Workflow::Replace || call.changed)) {
    ++m_progress.complete; --m_progress.inFlight;
    return Outcome::Complete;
  }
  return ack ? Outcome::Ack : changed ? Outcome::Changed : Outcome::Terminal;
}
bool Calls::release(uint64_t key) {
  auto node = m_calls.findPtr(key);
  if (!node || !node->val().ack || !node->val().terminal ||
    (m_flow == Workflow::Replace && !node->val().changed)) return false;
  m_calls.del(key); return true;
}
void Calls::clear() { m_calls.clean(); }
} // ZfixLoad
