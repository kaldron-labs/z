//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Data Series Statistics
// - rolling mean, variance and standard deviation
// - with order statistics tree:
//   - rolling median, percentiles

#ifndef ZdfStats_HH
#define ZdfStats_HH

#ifndef ZdfLib_HH
#include <zlib/ZdfLib.hh>
#endif

#include <math.h>

#include <ZdfStatsTree.hh>

#include <zlib/ZuCmp.hh>
#include <zlib/ZuFP.hh>

#include <zlib/ZmAllocator.hh>

namespace Zdf {

namespace pbds = __gnu_pbds;

// rolling count, total, mean, variance, standard deviation
class Stats {
public:
  Stats() = default;
  Stats(const Stats &) = delete;
  Stats &operator =(const Stats &) = delete;
  Stats(Stats &&) = default;
  Stats &operator =(Stats &&) = default;
  ~Stats() = default;

  uint64_t count() const { return m_count; }
  double total() const { return m_total; }
  double mean() const {
    if (ZuUnlikely(!m_count)) return 0.0;
    return m_total / double(m_count);
  }
  double var() const {
    if (ZuUnlikely(!m_count)) return 0.0;
    return (m_var / double(m_count)) / m_varMul;
  }
  // Note: std() returns the population standard deviation, i.e.
  // the equivalent of Excel's STDEVP(); by contrast Excel's STDEV() uses
  // the n-1 formula intended for statistical sampling - this
  // implementation performs a running calculation on an entire window
  double std() const { return sqrt(var()); }

  void add(double v) {
    if (ZuUnlikely(!m_count)) {
      m_total = v;
    } else {
      double n = m_count;
      auto prev = double(m_total) / n;
      auto mean = double(m_total += v) / (n + 1);
      m_var += (v - prev) * (v - mean);
    }
    ++m_count;
  }

  void del(double v) {
    if (ZuUnlikely(!m_count)) return;
    if (m_count == 1) {
      m_total = 0;
    } else if (m_count == 2) {
      m_total -= v;
      m_var = 0;
    } else {
      double n = m_count;
      auto prev = double(m_total) / n;
      auto mean = double(m_total -= v) / (n - 1.0);
      m_var -= (v - prev) * (v - mean);
    }
    --m_count;
  }

  void clean() {
    m_count = 0;
    m_total = 0;
    m_var = 0;
  }

private:
  uint64_t	m_count = 0;
  double	m_total = 0;
  double	m_var = 0;	// accumulated variance
  double	m_varMul = 1;
};

// NTP defaults
struct StatsTree_Defaults {
  struct HeapID : public ZuStringT<"Zdf.StatsTree"> { };
};

// StatsTreeHeapID - the heap ID
template <typename HeapID_, class NTP = StatsTree_Defaults>
struct StatsTreeHeapID_ : public NTP {
  using HeapID = HeapID_;
};
template <ZuString HeapID, class NTP = StatsTree_Defaults>
using StatsTreeHeapID = StatsTreeHeapID_<ZuStringT<HeapID>, NTP>;

template <class NTP = StatsTree_Defaults>
class StatsTree : public Stats {
public:
  using HeapID = typename NTP::HeapID;
  using Alloc = ZmAllocator_<std::pair<double, unsigned>, HeapID>;

private:
  using Tree = pbds::stats_tree<double, unsigned, Alloc>;
public:
  using Iter = decltype(ZuDeclVal<Tree &>().begin());
  using CIter = decltype(ZuDeclVal<const Tree &>().begin());

public:
  StatsTree() = default;
  StatsTree(const StatsTree &) = delete;
  StatsTree &operator =(const StatsTree &) = delete;
  StatsTree(StatsTree &&) = default;
  StatsTree &operator =(StatsTree &&) = default;
  ~StatsTree() = default;

  void add(double v) {
    Stats::add(v);
    m_tree.insert(v);
  }
  void del(double v) {
    auto iter = m_tree.find(v);
    if (iter != end()) del_(iter);
  }
  template <typename T, typename = ZuIs<Iter, T>>
  void del(T iter) {
    if (iter != end()) del_(iter);
  }
private:
  void del_(Iter iter) {
    Stats::del(iter->first);
    m_tree.erase(iter);
  }

public:
  auto begin() const { return m_tree.begin(); }
  auto end() const { return m_tree.end(); }

  double fp(CIter iter) const {
    if (iter == end()) return ZuFP<double>::nan();
    return iter->first;
  }

  double minimum() const { return fp(begin()); }
  double maximum() const {
    if (!count()) return ZuFP<double>::nan();
    return fp(--end());
  }

  template <typename T>
  auto find(T &&v) const { return m_tree.find(ZuFwd<T>(v)); }

  auto order(unsigned n) const {
    return m_tree.find_by_order(n);
  }
  // 0 <= n < 1
  auto rankIter(double n) const {
    return m_tree.find_by_order(n *double(this->count()));
  }
  // 0 <= n < 1
  double rank(double n) const {
    auto iter = rankIter(n);
    if (iter == m_tree.end()) return ZuFP<double>::nan();
    return fp(iter);
  }
  auto medianIter() const {
    return m_tree.find_by_order(this->count()>>1);
  }
  double median() const {
    auto iter = medianIter();
    if (iter == m_tree.end()) return ZuFP<double>::nan();
    return fp(iter);
  }

  void clean() {
    Stats::clean();
    m_tree.clear();
  }

private:
  Tree		m_tree;
};

} // namespace Zdf

#endif /* ZdfStats_HH */
