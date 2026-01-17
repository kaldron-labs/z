//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ring buffer inter-process utility functions
//
// (mainly a wrapper around Linux futexes and Win32 equivalent)

#ifndef ZiRingUtil_HH
#define ZiRingUtil_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmRingUtil.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiPlatform.hh>
#include <zlib/ZiFile.hh>

namespace ZiRingUtil_ {

template <typename> class Params;
class ParamData {
  template <typename> friend Params;
public:
  ParamData(ZuCSpan name) : m_name(name) { }

  ParamData() = default;
  ParamData(const ParamData &) = default; 
  ParamData &operator =(const ParamData &) = default;
  ParamData(ParamData &&) = default; 
  ParamData &operator =(ParamData &&) = default;

  const Zi::Name &name() const { return m_name; }

private:
  Zi::Name	m_name;
};
template <typename Derived> class Params :
    public ZmRingUtil_::Params<Derived>, public ParamData {
  ZuInline Derived &&derived() { return ZuMv(*static_cast<Derived *>(this)); }
public:
  // placeholder for optional named parameters
};

} // ZiRingUtil_

struct ZiRingUtilParams : public ZiRingUtil_::Params<ZiRingUtilParams> { };

class ZiAPI ZiRingUtil : public ZmRingUtil {
  using ParamData = ZiRingUtil_::ParamData;
  template <typename Derived> using Params = ZiRingUtil_::Params<Derived>;

public:
  ZiRingUtil() = default;
  template <typename Derived>
  ZiRingUtil(Params<Derived> params) :
      ZmRingUtil{ZuMv(params)},
      m_params{ZuMv(params)} { } // yes, moved twice

  const ParamData &params() const { return m_params; }

  int open(ZeError *e = nullptr);
  int close(ZeError *e = nullptr);

#ifdef linux
#define ZiRing_wait(index, addr, val) wait(addr, val)
#define ZiRing_wake(index, addr, n) wake(addr, n)
  // block until woken or timeout while addr == val
  int wait(ZmAtomic<uint32_t> &addr, uint32_t val);
  // wake up waiters on addr (up to n waiters are woken)
  int wake(ZmAtomic<uint32_t> &addr, unsigned n);
#endif

#ifdef _WIN32
#define ZiRing_wait(index, addr, val) wait(index, addr, val)
#define ZiRing_wake(index, addr, n) wake(index, addr, n)
  // block until woken or timeout while addr == val
  int wait(unsigned index, ZmAtomic<uint32_t> &addr, uint32_t val);
  // wake up waiters on addr (up to n waiters are woken)
  int wake(unsigned index, ZmAtomic<uint32_t> &addr, unsigned n);
#endif

  static void getpinfo(uint32_t &pid, ZuTime &start);
  static bool alive(uint32_t pid, ZuTime start);
  static bool kill(uint32_t pid, bool coredump);

protected:
  ParamData		m_params;
};

#endif /* ZiRingUtil_HH */
