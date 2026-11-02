//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Shared workload objects/accounting; no FIX parser or session dependency

#ifndef ZfixLoad_HH
#define ZfixLoad_HH

#include <zlib/ZuDateTime.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfJSON.hh>

namespace ZfixLoad {

namespace Mode {

  ZtEnum(, Mode, int8_t, FIX4, FIX44A, FIXT11);
  ZtEnumMap(, Mode, CLI, "4", "44A", "T11");
}

namespace Version {

  ZtEnum(, Version, int8_t, V4_0, V4_1, V4_2, V4_3, V4_4, VT1_1);
  ZtEnumMap(, Version, Wire, "FIX.4.0", "FIX.4.1", "FIX.4.2", "FIX.4.3", "FIX.4.4", "FIXT.1.1");
}

namespace Scenario {

  ZtEnum(, Scenario, int8_t, Orders, Groups, Cancel, Replace);
  ZtEnumMap(, Scenario, CLI, "orders", "groups", "cancel", "replace");
}

using Text = ZtString<ZtStringHeapID<"Zfix.Load.Text">>;
template <typename T> struct Rows : ZtArray<T, ZtArrayHeapID<"Zfix.Load.Rows">> {
  ZuDerive_(Rows, (ZtArray<T, ZtArrayHeapID<"Zfix.Load.Rows">>));
};

struct SubParty {
  ZuCSpan	id;
  unsigned	type = 0;
};

struct Party {
  ZuCSpan		id, source;
  unsigned		role = 0;
  Rows<SubParty>	subs;
};

struct Order {
  uint64_t	key = 0;
  ZuCSpan	handling{"1"}, symbol{"ZFIX"}, side{"1"}, type{"2"};
  ZuDateTime	time;
  unsigned	qty = 0;
  ZuDecimal	price;
  Rows<Party>	parties;
  ZuCSpan	text;
};

struct Cancel {
  uint64_t	original = 0, key = 0;
  ZuCSpan	symbol{"ZFIX"}, side{"1"};
  ZuDateTime	time;
  unsigned	qty = 0;
};

struct Replace {
  Order		order;
  uint64_t	original = 0;
};

struct Report {
  uint64_t	key = 0, order = 0, exec = 0, original = 0;
  ZuCSpan	kind, status, symbol{"ZFIX"}, side{"1"};
  unsigned	qty = 0, leaves = 0, cum = 0, lastQty = 0;
  ZuDecimal	price, avg, lastPx;
  ZuDateTime	time;
  ZuCSpan	text;
};

struct Shape {
  unsigned	parties = 0, subs = 0, text = 0;
};

// Build shape once per session. Returned spans remain valid until destruction;
// next() only changes fixed scalar values. Callers serialize before another next.
class Model {
public:
  explicit Model(Shape, uint64_t seed = 1);
  Order &next(uint64_t key, ZuDateTime);
  Order &next(uint64_t key, ZuDateTime, unsigned qty, ZuDecimal price);

  const Order &order() const { return m_order; }

  bool shape(ZuCSpan text) const {
    return text == m_order.text;
  }
private:
  uint64_t	m_seed;
  Order		m_order;
  Text		m_text;
};

Report report(const Order &, uint64_t exec, ZuCSpan kind,
  uint64_t order = 0, uint64_t original = 0);
bool valid(const Order &);
bool matches(const Report &, const Order &);

struct Quantiles {
  uint64_t	count = 0, p50 = 0, p95 = 0, p99 = 0, p999 = 0, max = 0;
};

ZfStruct(, (Quantiles, JSON), (count, , UInt64), (p50, , UInt64),
  (p95, , UInt64), (p99, , UInt64), (p999, , UInt64), (max, , UInt64));
// Exact samples, reserved before timing, bounded by the caller's measured count.
// Sort only after the owner stops recording. Values are nanoseconds.
class Samples {
public:
  explicit Samples(unsigned limit = 0) { m_values.length(limit, false); }

  Samples(const Samples &) = delete;
  Samples &operator =(const Samples &) = delete;

  Samples(Samples &&o) : m_values{ZuMv(o.m_values)}, m_count{o.m_count} { o.m_count = 0; }

  Samples &operator =(Samples &&o) {
    if (this != &o) { m_values = ZuMv(o.m_values); m_count = o.m_count; o.m_count = 0; }
    return *this;
  }

  bool add(uint64_t value) {
    if (m_count == m_values.length()) return false;
    m_values[m_count++] = value; return true;
  }

  Quantiles finish();

  unsigned count() const { return m_count; }
private:
  using Values = ZtArray<uint64_t, ZtArrayHeapID<"Zfix.Load.Samples">>;
  Values	m_values;
  unsigned	m_count = 0;
};

uint64_t monotonic();

struct Progress {
  uint64_t	offered = 0, admitted = 0, ack = 0, complete = 0;
  uint64_t	reports = 0, duplicate = 0, unknown = 0, invalid = 0, refused = 0;
  unsigned	inFlight = 0, peak = 0;
};

ZfStruct(, (Progress, JSON), (offered, , UInt64), (admitted, , UInt64),
  (ack, , UInt64), (complete, , UInt64), (reports, , UInt64),
  (duplicate, , UInt64), (unknown, , UInt64), (invalid, , UInt64),
  (refused, , UInt64), (inFlight, , UInt32), (peak, , UInt32));
namespace Workflow {

  enum { Orders, Cancel, Replace, Invalid };
}

unsigned workflow(Scenario::T);
namespace Outcome {

  enum { Invalid, Unknown, Duplicate, Ack, Changed, Terminal, Complete };
}

struct Call {
  uint64_t	order = 0, ack = 0, changed = 0, terminal = 0, sent = 0, intended = 0, follow = 0;
  unsigned	qty = 0, nextQty = 0;
  ZuDecimal	price, nextPrice;
  bool		warmup = false;
};

// One application owner. No callback, timer, automatic submission, or session
// policy lives here. Completed records are explicitly released by the caller.
class Calls {
public:
  explicit Calls(unsigned limit, unsigned flow = Workflow::Orders) :
    m_limit{limit}, m_flow{flow} { }

  bool add(const Order &, uint64_t sent, uint64_t intended, bool warmup = false);
  bool follow(uint64_t original, const Order &);
  bool refused(uint64_t key);
  unsigned receive(const Report &, uint64_t now, Samples *ack = nullptr,
    Samples *terminal = nullptr, Samples *intendedAck = nullptr, Samples *intendedTerm = nullptr);
  bool release(uint64_t key);
  void clear();

  unsigned count() const { return m_calls.count_(); }
  const Progress &progress() const { return m_progress; }
private:
  using Map = ZmHashKV<uint64_t, Call,
    ZmHashLock<ZmNoLock, ZmHashHeapID<"Zfix.Load.Calls">>>;
  const unsigned	m_limit;
  const unsigned	m_flow;
  Map			m_calls;
  Progress		m_progress;
};

} // ZfixLoad

#endif /* ZfixLoad_HH */
