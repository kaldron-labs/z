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
#include <zlib/ZuPrint.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmRBTree.hh>

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

// display sequence:
//   id, addr, linear, bits, cBits, loadFactor, nodeSize,
//   count, effLoadFactor, resized
// derived display fields:
//   slots = 1<<bits
//   locks = 1<<cBits
struct ZmHashTelemetry {
  ZmIDString	id;		// primary key
  uintptr_t	addr = 9;	// primary key
  double	loadFactor = 0.0;// (double)N / 16.0
  double	effLoadFactor = 0.0;// graphable (*)
  uint64_t	count = 0;	// graphable (*)
  uint32_t	nodeSize = 0;
  uint32_t	resized = 0;	// dynamic
  uint8_t	bits = 0;
  uint8_t	cBits = 0;
  uint8_t	linear = 0;
  uint8_t	shadow = 0;
};

class ZmAPI ZmAnyHash_ : public ZmPolymorph {
public:
  virtual void telemetry(ZmHashTelemetry &) const { }
};
inline uintptr_t ZmAnyHash_PtrAxor(const ZmAnyHash_ &h) {
  return reinterpret_cast<uintptr_t>(&h);
}
ZuDerive(ZmHashMgr_Tables,
  (ZmRBTree<ZmAnyHash_,
    ZmRBTreeNode<ZmAnyHash_,
      ZmRBTreeKey<ZmAnyHash_PtrAxor,
	ZmRBTreeUnique<true,
	  ZmRBTreeHeapID<"">>>>>));
using ZmAnyHash = ZmHashMgr_Tables::Node;

template <typename, typename> class ZmHash; 
template <typename, typename, typename, unsigned> class ZmLHash_;

class ZmHashMgr_;
class ZmAPI ZmHashMgr {
friend ZmHashMgr_;
friend ZmHashParams;
template <typename, typename> friend class ZmHash; 
template <typename, typename, typename, unsigned> friend class ZmLHash_;

  template <class S> struct CSV_ {
    CSV_(S &stream) : m_stream(stream) {
      m_stream <<
	"id,addr,shadow,linear,bits,cBits,loadFactor,nodeSize,"
	"count,effLoadFactor,resized\n";
    }
    void print(ZmAnyHash *tbl) {
      ZmHashTelemetry data;
      tbl->telemetry(data);
      m_stream
	<< data.id << ','
	<< ZuBoxPtr(data.addr).hex() << ','
	<< unsigned(data.shadow) << ','
	<< unsigned(data.linear) << ','
	<< unsigned(data.bits) << ','
	<< unsigned(data.cBits) << ','
	<< ZuBoxed(data.loadFactor) << ','
	<< data.nodeSize << ','
	<< data.count << ','
	<< ZuBoxed(data.effLoadFactor) << ','
	<< data.resized << '\n';
    }
    S &stream() { return m_stream; }

  private:
    S	&m_stream;
  };

public:
  static void init(ZuCSpan id, const ZmHashParams &params);

  using AllFn = ZmFn<void(ZmAnyHash *), ZmFnHeapID<"ZmHashMgr.AllFn">>;
  static void all(AllFn fn);

  struct CSV;
friend CSV;
  struct CSV {
    template <typename S>
    void print(S &s) const {
      ZmHashMgr::CSV_<S> csv(s);
      ZmHashMgr::all({&csv, ZmFnPtr<&ZmHashMgr::CSV_<S>::print>{}});
    }
    friend ZuPrintFn ZuPrintType(CSV *);
  };
  static CSV csv() { return CSV(); }

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
