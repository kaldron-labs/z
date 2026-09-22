//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// multiplexer configuration

#ifndef ZvMxParams_HH
#define ZvMxParams_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZiMultiplex.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZvThreadParams.hh>

ZfStruct(ZvAPI, (ZiCxnOptions, Cf),
  (((options, AliasFn, flags, flags),
    (Mutable, (Flags<ZiCxnFlags::Map>))),		(UInt32)),
  (((ttl, Fn),	(Mutable, (Range<0U, unsigned(INT_MAX)>))), (UInt32)));

ZfStruct(ZvAPI, (ZmSchedTParams, Cf),
  (((isolated, Fn),	(Mutable)),				(Bool)),
  (((name, Fn),	(Mutable)),					(String)),
  (((stackSize, Fn),	(Mutable, (Range<0U, unsigned(INT_MAX)>))), (UInt32)),
  (((priority, Fn),	(Mutable, (Enum<ZmThreadPriority::Map>))),	(Int32,
      ZmThreadPriority::Normal)),
  (((partition, Fn),	(Mutable, (Range<0U, unsigned(INT_MAX)>))), (UInt32)),
  (((cpuset, Fn),	(Mutable)),					(String)),
  (((detached, Fn),	(Mutable)),				(Bool)));

ZfStruct(ZvAPI, (ZmSchedParams, Cf),
  (((nThreads, Fn),	(Mutable, (Range<1U, 1024U>))),		(UInt32, 3)),
  (((stackSize, Fn),	(Mutable, (Range<16384U, 2U<<20U>))),	(UInt32)),
  (((priority, Fn),	(Mutable, (Enum<ZmThreadPriority::Map>))),	(Int32, ZmThreadPriority::Normal)),
  (((partition, Fn),	(Mutable)),					(UInt32)),
  (((quantum, Fn),	(Mutable)),					(Time)),
  (((queueSize, Fn),	(Mutable, (Range<8192U, 1U<<30U>))),	(UInt32)),
  (((ll, Fn),		(Mutable)),					(Bool)),
  (((spin, Fn),		(Mutable, (Range<0U, unsigned(INT_MAX)>))), (UInt32)),
  (((timeout, Fn),	(Mutable, (Range<0U, 3600U>))),		(UInt32)),
);

ZfStruct(ZvAPI, (ZiMxParams, Cf),
  (((rxThread, AliasFn, rxThreadID, rxThread), (Mutable)),		(String)),
  (((txThread, AliasFn, txThreadID, txThread), (Mutable)),		(String)),
#ifdef ZiMultiplex_EPoll
  (((epollMaxFDs, Fn),	(Mutable, (Range<1U, 100000U>))),	(UInt32)),
  (((epollQuantum, Fn),	(Mutable, (Range<1U, 1024U>))),	(UInt32)),
#endif
  (((rxBufSize, Fn),	(Mutable, (Range<0U, unsigned(INT_MAX)>))), (UInt32)),
  (((txBufSize, Fn),	(Mutable, (Range<0U, unsigned(INT_MAX)>))), (UInt32))
#ifdef ZiMultiplex_DEBUG
  ,
  (((trace, Fn),	(Mutable)),					(Bool)),
  (((debug, Fn),	(Mutable)),					(Bool)),
  (((frag, Fn),	(Mutable)),					(Bool)),
  (((yield, Fn),	(Mutable)),					(Bool))
#endif
);

inline ZiIP zvCxnIP(const ZfCf::AnyNode *cf)
{
  if (!cf->has<ZfCf::AnyNode::String>())
    throw ZfCf_EXCEPT(ZfCfError::badType(cf, "string"));
  return ZiIP{cf->data<ZfCf::AnyNode::String>()};
}

inline ZiCxnOptions ZvCxnOptions(
    const ZfCf::AnyNode *cf, ZiCxnOptions options = {})
{
  if (!cf) return options;
  ZfCf::handler<ZiCxnOptions>(cf).update(options);
  if (options.multicast()) {
    if (auto mif = cf->resolve("mif")) options.mif(zvCxnIP(mif));
    if (auto mreqs = cf->resolve("mreqs")) {
	if (!mreqs->has<ZfCf::AnyNode::Object>())
	  throw ZfCf_EXCEPT(ZfCfError::badType(mreqs, "object"));
	for (auto &field: mreqs->data<ZfCf::AnyNode::Object>()) {
	  auto &mreq = field.p<1>();
	  if (!mreq->has<ZfCf::AnyNode::Object>())
	    throw ZfCf_EXCEPT(ZfCfError::badType(mreq, "object"));
	  auto interface = mreq->resolve("interface");
	  if (!interface)
	    throw ZfCf_EXCEPT(ZfCfError::required(mreq, "interface"));
	  ZiIP addr{field.p<0>()}, mif{zvCxnIP(interface)};
	  if (!addr || !addr.multicast())
	    throw ZeEXCEPT(Error, "ZvCxnOptions", ([
	      key = ZfCfError::fullKey(field.p<1>()), addr
	    ](auto &s) {
	      s << '"' << key << "\" invalid multicast IP " << addr;
	    }));
	  options.mreq(ZiMReq(addr, mif));
	}
      }
    }
  return options;
}

inline ZiMxParams ZvMxParams(
    ZuCSpan id, const ZfCf::AnyNode *cf, ZiMxParams params = {})
{
  ZmSchedParams &sched = params.scheduler();
  sched.id(id);
  if (!cf) return params;

  ZfCf::handler<ZmSchedParams>(cf).update(sched);
  if (auto threads = cf->resolve("threads")) {
      if (!threads->has<ZfCf::AnyNode::Object>())
	throw ZfCf_EXCEPT(ZfCfError::badType(threads, "object"));
      for (auto &field: threads->data<ZfCf::AnyNode::Object>()) {
	auto parsed = ZuBox<unsigned>::eov(field.p<0>());
	if (parsed.p<0>() != int(field.p<0>().length()) ||
	    !parsed.p<1>() || parsed.p<1>() > sched.nThreads())
	  throw ZeEXCEPT(Error, "ZvMxParams", ([
	    key = ZfCfError::fullKey(field.p<1>()),
	    value = ZeString{field.p<0>()}, max = sched.nThreads()
	  ](auto &s) {
	    s << '"' << key << "\": invalid scheduler SID \"" << value <<
	      "\"; expected [1, " << max << ']';
	  }));
	unsigned sid = parsed.p<1>();
	ZfCf::handler<ZmSchedTParams>(field.p<1>()).update(sched.thread(sid));
      }
    }
  ZfCf::handler<ZiMxParams>(cf).update(params);
  return params;
}

#endif /* ZvMxParams_HH */
