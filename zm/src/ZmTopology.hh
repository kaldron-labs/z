//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// hwloc topology

#ifndef ZmTopology_HH
#define ZmTopology_HH

#ifdef _MSC_VER
#pragma once
#pragma warning(push)
#pragma warning(disable:4996)
#endif

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <hwloc.h>

#include <zlib/ZmPLock.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmCleanup.hh>

class ZmAPI ZmTopology {
  ZmTopology(const ZmTopology &) = delete;
  ZmTopology &operator =(const ZmTopology &) = delete;

  ZmTopology();

public:
  ~ZmTopology();

private:
  static ZmTopology *instance();

public:
  static hwloc_topology_t hwloc() { return instance()->m_hwloc; }

  typedef void (*ErrorFn)(int);
  static void errorFn(ErrorFn fn);
  static void error(int errNo);

private:
  ZmPLock_		m_lock;
  hwloc_topology_t	m_hwloc;
  ZmAtomic<ErrorFn>	m_errorFn;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* ZmTopology_HH */
