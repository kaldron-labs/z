//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// hash table configuration management & telemetry

#ifndef ZmHashMgr_HH
#define ZmHashMgr_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmRBTree.hh>

#include <zlib/ZtcHash.hh>

class ZmHashParams {
public:
  ZmHashParams() { }
  ZmHashParams(ZuCSpan id) { init(id); }
  ZmHashParams(uint32_t size) :
    m_bits{size <= 8 ? 3 : (32 - ZuIntrin::clz(size - 1))} { }

  const ZmHashParams &init(ZuCSpan id);

  ZmHashParams &bits(unsigned v) { m_bits = v; return *this; }
  ZmHashParams &loadFactor(double v) { m_loadFactor = v; return *this; }
  ZmHashParams &cBits(unsigned v) { m_cBits = v; return *this; }

  unsigned bits() const { return m_bits; }
  double loadFactor() const { return m_loadFactor; }
  unsigned cBits() const { return m_cBits; }

private:
  unsigned	m_bits = 8;
  double	m_loadFactor = 1.0;
  unsigned	m_cBits = 3;
};

class ZmAPI ZmAnyHash_ : public ZmPolymorph, public Ztc::Hash { };
inline uintptr_t ZmAnyHash_PtrAxor(const ZmAnyHash_ &h) {
  return reinterpret_cast<uintptr_t>(&h);
}
ZuDerive(ZmHashMgr_Tables,
  (ZmRBTree<ZmAnyHash_,
    ZmRBTreeNode<ZmAnyHash_,
      ZmRBTreeKey<ZmAnyHash_PtrAxor,
	ZmRBTreeUnique<true,
	  ZmRBTreeShadow<
	    ZmRBTreeHeapID<"">>>>>>));
using ZmAnyHash = ZmHashMgr_Tables::Node;

template <typename, typename> class ZmHash; 
template <typename, typename, typename, unsigned> class ZmLHash_;

class ZmHashMgr_;
class ZmAPI ZmHashMgr {
friend ZmHashMgr_;
friend ZmHashParams;
template <typename, typename> friend class ZmHash; 
template <typename, typename, typename, unsigned> friend class ZmLHash_;

public:
  static void init(ZuCSpan id, const ZmHashParams &params);

private:
  static ZmHashParams &params(ZuCSpan id, ZmHashParams &in);

public:
  static void add(ZmAnyHash *);
  static void del(ZmAnyHash *);
};

inline const ZmHashParams &ZmHashParams::init(ZuCSpan id)
{
  return ZmHashMgr::params(id, *this);
}

#endif /* ZmHashMgr_HH */
