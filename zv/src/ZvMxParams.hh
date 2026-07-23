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

struct ZvCxnCf {
  uint32_t	options = 0;
  ZtString<>	multicastInterface;
  unsigned	multicastTTL = 0;
  ZtString<>	familyName;
};

ZfStruct((ZvCxnCf, Cf),
  (((options),		(Flags<ZiCxnFlags::Map>)),	(UInt32)),
  (((multicastInterface)),			(String)),
  (((multicastTTL),	((Range<0U, unsigned(INT_MAX)>))), (UInt32)),
  (((familyName)),				(String)));

struct ZvMGroupCf {
  ZtString<> interface;
};

ZfStruct((ZvMGroupCf, Cf),
  (((interface), (Required)), (String)));

struct ZvMxThreadCf {
  bool		isolated = false;
  ZtString<>	name;
  unsigned	stackSize = 0;
  int		priority = ZmThreadPriority::Normal;
  unsigned	partition = 0;
  ZtString<>	cpuset;
  bool		detached = false;
};

ZfStruct((ZvMxThreadCf, Cf),
  (((isolated)),					(Bool)),
  (((name)),					(String)),
  (((stackSize),	((Range<0U, unsigned(INT_MAX)>))), (UInt32)),
  (((priority),		(Enum<ZmThreadPriority::Map>)),	(Int32,
      ZmThreadPriority::Normal)),
  (((partition),	((Range<0U, unsigned(INT_MAX)>))), (UInt32)),
  (((cpuset)),					(String)),
  (((detached)),				(Bool)));

struct ZvMxCf {
  unsigned	nThreads = 3;
  unsigned	stackSize = 0;
  int		priority = ZmThreadPriority::Normal;
  unsigned	partition = 0;
  double	quantum = 0;
  unsigned	queueSize = 0;
  bool		ll = false;
  unsigned	spin = 0;
  unsigned	timeout = 0;
  ZtString<>	rxThread;
  ZtString<>	txThread;
#ifdef ZiMultiplex_EPoll
  unsigned	epollMaxFDs = 0;
  unsigned	epollQuantum = 0;
#endif
  unsigned	rcvBufSize = 0;
  unsigned	sndBufSize = 0;
#ifdef ZiMultiplex_DEBUG
  bool		trace = false;
  bool		debug = false;
  bool		frag = false;
  bool		yield = false;
#endif
};

ZfStruct((ZvMxCf, Cf),
  (((nThreads),		((Range<1U, 1024U>))),		(UInt32, 3)),
  (((stackSize),	((Range<16384U, 2U<<20U>))),	(UInt32)),
  (((priority),		(Enum<ZmThreadPriority::Map>)),	(Int32,
      ZmThreadPriority::Normal)),
  (((partition)),				(UInt32)),
  (((quantum)),					(Float)),
  (((queueSize),	((Range<8192U, 1U<<30U>))),	(UInt32)),
  (((ll)),					(Bool)),
  (((spin),		((Range<0U, unsigned(INT_MAX)>))), (UInt32)),
  (((timeout),		((Range<0U, 3600U>))),		(UInt32)),
  (((rxThread)),				(String)),
  (((txThread)),				(String)),
#ifdef ZiMultiplex_EPoll
  (((epollMaxFDs),	((Range<1U, 100000U>))),	(UInt32)),
  (((epollQuantum),	((Range<1U, 1024U>))),		(UInt32)),
#endif
  (((rcvBufSize),	((Range<0U, unsigned(INT_MAX)>))), (UInt32)),
  (((sndBufSize),	((Range<0U, unsigned(INT_MAX)>))), (UInt32))
#ifdef ZiMultiplex_DEBUG
  ,
  (((trace)),					(Bool)),
  (((debug)),					(Bool)),
  (((frag)),					(Bool)),
  (((yield)),					(Bool))
#endif
);

struct ZvCxnOptions : public ZiCxnOptions {
  ZvCxnOptions() : ZiCxnOptions{} { }

  ZvCxnOptions(const ZiCxnOptions &p) : ZiCxnOptions{p} { }
  ZvCxnOptions &operator =(const ZiCxnOptions &p) {
    ZiCxnOptions::operator =(p);
    return *this;
  }

  ZvCxnOptions(const ZfCf::AnyNode *cf) : ZiCxnOptions{} { init(cf); }

  ZvCxnOptions(const ZfCf::AnyNode *cf, const ZiCxnOptions &deflt) :
      ZiCxnOptions{deflt} { init(cf); }

  void init(const ZfCf::AnyNode *cf) {
    if (!cf) return;
    auto patch = ZfCf::handler<ZvCxnCf>(cf).ctor();
    if (cf->resolve("options")) flags(patch.options);
    if (multicast()) {
      if (cf->resolve("multicastInterface")) mif(patch.multicastInterface);
      if (cf->resolve("multicastTTL")) ttl(patch.multicastTTL);
      if (auto groups = cf->resolve("multicastGroups")) {
	if (!groups->has<ZfCf::AnyNode::Object>())
	  throw ZfCf_EXCEPT(ZfCfError::badType(groups, "object"));
	for (auto &field: groups->data<ZfCf::AnyNode::Object>()) {
	  ZiIP addr{field.p<0>()};
	  auto group = ZfCf::handler<ZvMGroupCf>(field.p<1>()).ctor();
	  ZiIP interface{group.interface};
	  if (!addr || !addr.multicast())
	    throw ZeEXCEPT(Error, "ZvMxParams", ([
	      key = ZfCfError::fullKey(field.p<1>()), addr
	    ](auto &s) {
	      s << '"' << key << "\" invalid multicast IP " << addr;
	    }));
	  mreq(ZiMReq(addr, interface));
	}
      }
    }
    if (netlink()) {
      if (!cf->resolve("familyName"))
	throw ZfCf_EXCEPT(ZfCfError::required(cf, "familyName"));
      familyName(patch.familyName);
    }
  }

};

struct ZvMxParams : public ZiMxParams {
  ZvMxParams() = default;
  ZvMxParams(ZuCSpan id, const ZfCf::AnyNode *cf) { init(id, cf); }
  ZvMxParams(ZuCSpan id, const ZfCf::AnyNode *cf, ZiMxParams &&deflt) :
    ZiMxParams{ZuMv(deflt)} { init(id, cf); }

  void init(ZuCSpan id, const ZfCf::AnyNode *cf) {
    if (!cf) return;
    auto patch = ZfCf::handler<ZvMxCf>(cf).ctor();
    ZmSchedParams &sched = scheduler();
    static unsigned ncpu = Zm::getncpu();

    sched.id(id);
    if (cf->resolve("nThreads")) sched.nThreads(patch.nThreads);
    if (cf->resolve("stackSize")) sched.stackSize(patch.stackSize);
    if (cf->resolve("priority")) sched.priority(patch.priority);
    if (cf->resolve("partition")) {
      if (ZuUnlikely(patch.partition >= ncpu))
	throw ZeEXCEPT(Error, "ZvMxParams", ([
	  key = ZfCfError::fullKey(cf, "partition"), max = ncpu - 1
	](auto &s) {
	  s << '"' << key << "\": expected range [0, " << max << ']';
	}));
      sched.partition(patch.partition);
    }
    if (cf->resolve("quantum")) sched.quantum(patch.quantum);
    if (cf->resolve("queueSize")) sched.queueSize(patch.queueSize);
    if (cf->resolve("ll")) sched.ll(patch.ll);
    if (cf->resolve("spin")) sched.spin(patch.spin);
    if (cf->resolve("timeout")) sched.timeout(patch.timeout);

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
	auto threadCf = ZfCf::handler<ZvMxThreadCf>(field.p<1>()).ctor();
	auto &thread = sched.thread(sid);
	if (field.p<1>()->resolve("isolated")) thread.isolated(threadCf.isolated);
	if (field.p<1>()->resolve("name")) thread.name(threadCf.name);
	if (field.p<1>()->resolve("stackSize"))
	  thread.stackSize(threadCf.stackSize);
	if (field.p<1>()->resolve("priority"))
	  thread.priority(threadCf.priority);
	if (field.p<1>()->resolve("partition")) {
	  if (ZuUnlikely(threadCf.partition >= ncpu))
	    throw ZeEXCEPT(Error, "ZvMxParams", ([
	      key = ZfCfError::fullKey(field.p<1>(), "partition"),
	      max = ncpu - 1
	    ](auto &s) {
	      s << '"' << key << "\": expected range [0, " << max << ']';
	    }));
	  thread.partition(threadCf.partition);
	}
	if (field.p<1>()->resolve("cpuset")) thread.cpuset(threadCf.cpuset);
	if (field.p<1>()->resolve("detached")) thread.detached(threadCf.detached);
      }
    }
    auto sid = [&sched, cf](ZuCSpan key, ZuCSpan value) {
      unsigned sid = sched.sid(value);
      if (ZuLikely(sid && sid <= sched.nThreads())) return sid;
      throw ZeEXCEPT(Error, "ZvMxParams", ([
	key = ZfCfError::fullKey(cf, key), value = ZeString{value},
	max = sched.nThreads()
      ](auto &s) {
	s << '"' << key << "\": invalid scheduler SID \"" << value <<
	  "\"; expected [1, " << max << "] or a configured thread name";
      }));
    };
    if (cf->resolve("rxThread"))
      rxThread(sid("rxThread", patch.rxThread));
    if (cf->resolve("txThread"))
      txThread(sid("txThread", patch.txThread));
#ifdef ZiMultiplex_EPoll
    if (cf->resolve("epollMaxFDs")) epollMaxFDs(patch.epollMaxFDs);
    if (cf->resolve("epollQuantum")) epollQuantum(patch.epollQuantum);
#endif
    if (cf->resolve("rcvBufSize")) rxBufSize(patch.rcvBufSize);
    if (cf->resolve("sndBufSize")) txBufSize(patch.sndBufSize);
#ifdef ZiMultiplex_DEBUG
    if (cf->resolve("trace")) trace(patch.trace);
    if (cf->resolve("debug")) debug(patch.debug);
    if (cf->resolve("frag")) frag(patch.frag);
    if (cf->resolve("yield")) yield(patch.yield);
#endif
  }

};

#endif /* ZvMxParams_HH */
