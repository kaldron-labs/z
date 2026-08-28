//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <iostream>

#include <zlib/ZuDateTime.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiDir.hh>

#include <zlib/ZtcApp.hh>
#include <zlib/ZtcAlert.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcFilter.hh>
#include <zlib/ZtcMsg.hh>

namespace Ztc {
namespace App_ {

using RxFrame =
  Ztcp::RxBufAlloc<1024, AppCf::DefltMaxFrame, "Ztc.App.RxFrame">;
using CtrlFrame =
  Ztcp::TxBufAlloc<512, AppCf::DefltMaxFrame, "Ztc.App.CtrlFrame">;
using TelFrame =
  Ztcp::TxBufAlloc<1024, AppCf::DefltMaxFrame, "Ztc.App.TelFrame">;
using AlertFrame =
  ZiIOBufAlloc<1024, AppCf::DefltMaxFrame, "Ztc.App.AlertFrame">;

template <typename T, ZuString HeapID>
using Samples = ZtArray<T, ZtArrayHeapID<HeapID>>;

using HeapSamples = Samples<HeapTelemetry, "Ztc.App.HeapSamples">;
using HashSamples = Samples<HashTelemetry, "Ztc.App.HashSamples">;
using ThreadSamples = Samples<ThreadTelemetry, "Ztc.App.ThreadSamples">;
using MxSamples = Samples<MxTelemetry, "Ztc.App.MxSamples">;
using CxnSamples = Samples<CxnTelemetry, "Ztc.App.CxnSamples">;
using QueueSamples = Samples<QueueTelemetry, "Ztc.App.QueueSamples">;
using HubSamples = Samples<HubTelemetry, "Ztc.App.HubSamples">;
using LinkSamples = Samples<LinkTelemetry, "Ztc.App.LinkSamples">;
using PoolSamples = Samples<PoolTelemetry, "Ztc.App.PoolSamples">;
using DBSamples = Samples<DBTelemetry, "Ztc.App.DBSamples">;
using DBHostSamples = Samples<DBHostTelemetry, "Ztc.App.DBHostSamples">;
using DBTableSamples = Samples<DBTableTelemetry, "Ztc.App.DBTableSamples">;

using SubKey = ZuTuple<uint64_t, uint8_t, ZuCSpan>;
using DueKey = ZuTuple<ZuTime, uint64_t>;

struct PendingAlert {
  explicit operator bool() const { return bool(frame); }

  ZmRef<ZiIOBuf>	frame;
  uint64_t	seqNo = 0;
  uint32_t	date = 0;
};

using AlertFrames = ZmQueue<PendingAlert,
  ZmQueueHeapID<"Ztc.App.AlertPending">>;

class ClientData {
public:
  ZmRef<App::Link>	link;
  uint64_t		generation = 0;
  unsigned		pending = 0;
  unsigned		subs = 0;
};

template <typename Heap>
class Client__ : public Heap, public ClientData { };
using ClientHeap = ZmHeap<"Ztc.App.Client", Client__<ZuVoid>>;
class Client_ final : public Client__<ClientHeap> { };

class Subscription_ {
public:
  ZmRef<App::Link>	link;
  Client_		*client = nullptr;
  Pending_		*snapshot = nullptr;
  Filter_::Filter	filter;
  ZuTime		due;
  uint64_t		generation = 0;
  uint64_t		seqNo = 0;
  uint64_t		order = 0;
  uint32_t		interval = 0;
  fbs::Group		group = fbs::Group::Heap;
  AlertFrames		pending;
  uint64_t		highSeqNo = 0;
  uint64_t		replaySeqNo = 0;
  uint32_t		highDate = 0;
  uint32_t		replayDate = 0;
  uint32_t		openDate = 0;
  uint32_t		replayCount = 0;
  ZiFile		replayData;
  ZiFile		replayIndex;
  bool			running = false;
  bool			dirty = false;
  bool			inDue = false;
  bool			replaying = false;
  bool			oneShot = false;
  bool			replayFailed = false;
};

class PendingData {
public:
  Client_		*client = nullptr;
  Subscription_	*sub = nullptr;
  uint64_t		generation = 0;
  uint64_t		seqNo = 0;
  uint32_t		count = 0;
  uint32_t		offset = 0;
  uint8_t		stage = 0;
  fbs::Group		group = fbs::Group::Heap;
  HeapSamples		heaps;
  HashSamples		hashes;
  ThreadSamples		threads;
  MxSamples		mxs;
  CxnSamples		cxns;
  QueueSamples		queues;
  HubSamples		hubs;
  LinkSamples		links;
  PoolSamples		pools;
  DBSamples		dbs;
  DBHostSamples		hosts;
  DBTableSamples	tables;
  AppTelemetry		app;
  bool			appCaptured = false;
};

template <typename Heap>
class Pending__ : public Heap, public PendingData { };
using PendingHeap = ZmHeap<"Ztc.App.Pending", Pending__<ZuVoid>>;
class Pending_ final : public Pending__<PendingHeap> { };

using PendingKey = ZuTuple<uint64_t, uint64_t>;
using ClientIdx = ZmRBTreeKV<uint64_t, Client_ *,
  ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmNoLock,
      ZmRBTreeHeapID<"Ztc.App.ClientIdx">>>>;
using PendingIdx = ZmRBTreeKV<PendingKey, Pending_ *,
  ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmNoLock,
      ZmRBTreeHeapID<"Ztc.App.PendingIdx">>>>;

static SubKey subKey(const Subscription_ &sub)
{
  return {sub.generation, uint8_t(sub.group), sub.filter.key()};
}

static DueKey dueKey(const Subscription_ &sub)
{
  return {sub.due, sub.order};
}

using SubDueIdx = ZmRBTree<Subscription_,
  ZmRBTreeKey<dueKey,
    ZmRBTreeNode<Subscription_,
      ZmRBTreeUnique<true,
	ZmRBTreeShadow<>>>>>;

using SubIdx = ZmRBTree<SubDueIdx::Node,
  ZmRBTreeKey<subKey,
    ZmRBTreeNode<SubDueIdx::Node,
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"Ztc.App.SubIdx">>>>>;

using Subscription = SubIdx::Node;
using SubDueSets =
  ZtArray<SubDueIdx, ZtArrayHeapID<"Ztc.App.SubDueIdx">>;
using AlertTail = ZmQueue<ZmRef<ZiIOBuf>,
  ZmQueueHeapID<"Ztc.App.AlertTail">>;

class AlertStore {
public:
  using LoadResult = Alert_::LoadResult::T;

  void init(Zi::Path prefix, uint32_t maxFrame, unsigned retention) {
    m_prefix = ZuMv(prefix);
    m_maxFrame = maxFrame;
    m_retention = retention;
  }

  bool start(ZuTime now) {
    close();
    if (!m_prefix) return true;
    ZuDateTime date{now};
    bool ok = true;
    for (unsigned i = 0; i < m_retention; ++i) {
      unsigned yyyymmdd = unsigned(date.yyyymmdd());
      if (exists(yyyymmdd) && !check(yyyymmdd)) ok = false;
      --date.julian();
    }
    ++date.julian();
    if (!removeBefore(unsigned(date.yyyymmdd()))) ok = false;
    return ok;
  }

  void close() {
    m_data.close();
    m_index.close();
    m_date = 0;
    m_count = 0;
    m_offset = 0;
    m_usable = true;
  }

  static bool validDate(uint32_t date) {
    return Alert_::validDate(date);
  }

  static uint32_t addDays(uint32_t date, int days) {
    return Alert_::addDays(date, days);
  }

  uint32_t earliest(uint32_t date) const {
    return addDays(date, 1 - int(m_retention));
  }

  bool latest(ZuTime now, uint32_t &date, uint64_t &nextSeqNo) const {
    if (!m_prefix) return false;
    ZuDateTime candidate{now};
    for (unsigned i = 0; i < m_retention; ++i) {
      uint32_t date_ = uint32_t(candidate.yyyymmdd());
      ZiFile index;
      if (index.open(indexPath(date_), ZiFile::ReadOnly | ZiFile::GC) == Zi::OK) {
	Zi::Offset size = index.size();
	if (size >= Zi::Offset(sizeof(uint64_t))) {
	  date = date_;
	  nextSeqNo = uint64_t(size) / sizeof(uint64_t);
	  return true;
	}
      }
      --candidate.julian();
    }
    return false;
  }

  bool prepare(uint32_t date, uint64_t &seqNo) {
    if (date == m_date) {
      seqNo = m_count;
      return m_usable;
    }
    m_data.close();
    m_index.close();
    m_date = date;
    m_count = 0;
    m_offset = 0;
    m_usable = true;
    if (!m_prefix) {
      seqNo = 0;
      return true;
    }
    m_usable = open(date, true) && validate(m_data, m_index, date,
      m_count, m_offset, true);
    seqNo = m_count;
    if (!m_usable) diag(date, "partition is unusable");
    bool retentionOK = rotateRetention(date);
    return m_usable && retentionOK;
  }

  bool append(const ZiIOBuf *frame, uint64_t seqNo) {
    if (!m_prefix) return true;
    if (!m_usable || !m_data || !m_index || seqNo != m_count)
      return false;
    if (frame->length > m_maxFrame || m_offset < 0 ||
	uint64_t(m_offset) > UINT64_MAX - frame->length) {
      fail("invalid frame length or file offset");
      return false;
    }
    Zi::Offset offset = m_offset;
    if (m_data.pwrite(offset, frame->data(), frame->length) != Zi::OK) {
      fail("data write failed");
      return false;
    }
    if (seqNo > uint64_t(INT64_MAX) / sizeof(uint64_t)) {
      fail("index offset overflow");
      return false;
    }
    ZuLittleEndian<uint64_t> index{uint64_t(offset)};
    if (m_index.pwrite(
	  Zi::Offset(seqNo * sizeof(index)), &index, sizeof(index)) != Zi::OK) {
      fail("index write failed");
      return false;
    }
    m_offset += frame->length;
    ++m_count;
    return true;
  }

  LoadResult load(
      uint32_t date, uint64_t seqNo, ZmRef<ZiIOBuf> &frame,
      ZiFile &data, ZiFile &index, uint32_t &openDate) const {
    if (!m_prefix) return LoadResult::Missing;
    if (seqNo > uint64_t(INT64_MAX) / sizeof(uint64_t))
      return LoadResult::Missing;
    if (date != openDate) {
      data.close();
      index.close();
      openDate = 0;
      if (data.open(dataPath(date), ZiFile::ReadOnly | ZiFile::GC) != Zi::OK ||
	  index.open(indexPath(date), ZiFile::ReadOnly | ZiFile::GC) != Zi::OK) {
	data.close();
	index.close();
	return LoadResult::Missing;
      }
      openDate = date;
    }
    Zi::Offset indexSize = index.size();
    Zi::Offset dataSize = data.size();
    if (indexSize < 0 || dataSize < 0) return LoadResult::IOError;
    if (seqNo >= uint64_t(indexSize / sizeof(uint64_t)))
      return LoadResult::Missing;
    ZuLittleEndian<uint64_t> offset;
    if (index.pread(
	  Zi::Offset(seqNo * sizeof(offset)), &offset, sizeof(offset)) !=
	int(sizeof(offset)))
      return LoadResult::IOError;
    return loadFrame(data, dataSize, uint64_t(offset), date, seqNo, frame);
  }

private:
  static void diag(uint32_t date, ZuCSpan message) {
    std::cerr << "Ztc alert partition " << date << ": " << message << '\n';
  }

  void fail(ZuCSpan message) {
    m_usable = false;
    diag(m_date, message);
  }

  Zi::Path dataPath(uint32_t date) const {
    Zi::Path path{m_prefix};
    path << '.' << date << ".data";
    return path;
  }
  Zi::Path indexPath(uint32_t date) const {
    Zi::Path path{m_prefix};
    path << '.' << date << ".index";
    return path;
  }

  bool exists(uint32_t date) const {
    return ZiStat{dataPath(date)}.exists() || ZiStat{indexPath(date)}.exists();
  }

  bool remove(uint32_t date) const {
    ZeError e;
    auto remove_ = [&e](const Zi::Path &path) {
      if (!ZiStat{path}.exists()) return true;
      return ZiFile::remove(path, &e) == Zi::OK;
    };
    return remove_(dataPath(date)) && remove_(indexPath(date));
  }

  bool partitionDate(ZuCSpan name, uint32_t &date) const {
    Zi::Path leaf = ZiFile::leafname(m_prefix);
    unsigned prefix = leaf.length();
    unsigned length = name.length();
    if ((length != prefix + 14 && length != prefix + 15) ||
	!name.match(leaf) || name[prefix] != '.') return false;
    const char *ptr = name.data() + prefix + 1;
    uint32_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
      if (ptr[i] < '0' || ptr[i] > '9') return false;
      value = value * 10 + unsigned(ptr[i] - '0');
    }
    ZuCSpan suffix{ptr + 8, length - prefix - 9};
    if (suffix != ".data" && suffix != ".index") return false;
    if (!validDate(value)) return false;
    date = value;
    return true;
  }

  bool removeBefore(uint32_t first) const {
    ZiDir dir;
    if (dir.open(ZiFile::dirname(m_prefix)) != Zi::OK) return false;
    bool ok = true;
    Zi::Path name;
    for (;;) {
      int result = dir.read(name);
      if (result == Zi::EndOfFile) break;
      if (result != Zi::OK) return false;
      uint32_t date = 0;
      if (partitionDate(name, date) && date < first && !remove(date))
	ok = false;
    }
    return ok;
  }

  bool rotateRetention(uint32_t date) const {
    ZuDateTime expiry{
      ZuDateTime::YYYYMMDD{date}, ZuDateTime::HHMMSS{0}};
    expiry.julian() -= int(m_retention);
    return remove(unsigned(expiry.yyyymmdd()));
  }

  bool open(uint32_t date, bool create) {
    unsigned flags = ZiFile::GC | (create ? ZiFile::Create : 0);
    if (m_data.open(dataPath(date), flags) != Zi::OK) return false;
    if (m_index.open(indexPath(date), flags) != Zi::OK) {
      m_data.close();
      return false;
    }
    return true;
  }

  bool check(uint32_t date) {
    ZiFile data;
    ZiFile index;
    unsigned flags = ZiFile::GC | ZiFile::Create;
    if (data.open(dataPath(date), flags) != Zi::OK ||
	index.open(indexPath(date), flags) != Zi::OK) {
      diag(date, "open failed");
      return false;
    }
    uint64_t count = 0;
    Zi::Offset offset = 0;
    bool ok = validate(data, index, date, count, offset, true);
    if (!ok) diag(date, "validation failed");
    return ok;
  }

  LoadResult loadFrame(
      ZiFile &data, Zi::Offset dataSize, uint64_t offset,
      uint32_t date, uint64_t seqNo, ZmRef<ZiIOBuf> &frame) const {
    if (offset > uint64_t(dataSize) ||
	uint64_t(dataSize) - offset < sizeof(Hdr))
      return LoadResult::Incomplete;
    Hdr hdr;
    if (data.pread(Zi::Offset(offset), &hdr, sizeof(hdr)) != int(sizeof(hdr)))
      return LoadResult::IOError;
    uint64_t total = sizeof(Hdr) + uint32_t(hdr.length);
    if (total > m_maxFrame || total > unsigned(INT_MAX))
      return LoadResult::Corrupt;
    if (total > uint64_t(dataSize) - offset)
      return LoadResult::Incomplete;
    ZmRef<ZiIOBuf> loaded = new AlertFrame;
    if (!loaded->alloc(unsigned(total))) return LoadResult::IOError;
    loaded->length = unsigned(total);
    if (data.pread(
	  Zi::Offset(offset), loaded->data(), unsigned(total)) != int(total))
      return LoadResult::IOError;
    auto root = msg(loaded->ptr<Hdr>());
    auto telemetry = root && root->body_type() == fbs::Body::Telemetry ?
      root->body_as_Telemetry() : nullptr;
    auto alert = telemetry && telemetry->value_type() ==
	fbs::TelemetryBody::AlertTelemetry ?
      telemetry->value_as_AlertTelemetry() : nullptr;
    if (!alert || telemetry->seqNo() || alert->date() != date ||
	alert->seqNo() != seqNo) return LoadResult::Corrupt;
    frame = ZuMv(loaded);
    return LoadResult::OK;
  }

  bool validate(
      ZiFile &data, ZiFile &index, uint32_t date,
      uint64_t &count, Zi::Offset &dataEnd, bool recover) const {
    struct IO {
      const AlertStore	*store;
      ZiFile		*dataFile;
      ZiFile		*indexFile;
      uint32_t		date;
      bool		doRecover;
      Zi::Offset	dataSize_;
      Zi::Offset	indexSize_;

      Zi::Offset dataSize() const { return dataSize_; }
      Zi::Offset indexSize() const { return indexSize_; }
      bool index(uint64_t seqNo, uint64_t &value) {
	ZuLittleEndian<uint64_t> offset;
	if (indexFile->pread(Zi::Offset(seqNo * sizeof(offset)),
	      &offset, sizeof(offset)) != int(sizeof(offset))) return false;
	value = uint64_t(offset);
	return true;
      }
      LoadResult frame(uint64_t seqNo, uint64_t offset, unsigned &length) {
	ZmRef<ZiIOBuf> frame;
	auto result = store->loadFrame(
	  *dataFile, dataSize_, offset, date, seqNo, frame);
	if (result == LoadResult::OK) length = frame->length;
	return result;
      }
      bool truncateData(Zi::Offset offset) {
	return !doRecover || dataFile->truncate(offset) == Zi::OK;
      }
      bool truncateIndex(Zi::Offset offset) {
	return !doRecover || indexFile->truncate(offset) == Zi::OK;
      }
    } io{
      this, &data, &index, date, recover, data.size(), index.size()};
    return Alert_::recover(io, count, dataEnd);
  }

  Zi::Path	m_prefix;
  ZiFile	m_data;
  ZiFile	m_index;
  uint64_t	m_count = 0;
  Zi::Offset	m_offset = 0;
  uint32_t	m_date = 0;
  uint32_t	m_maxFrame = 0;
  unsigned	m_retention = 0;
  bool		m_usable = true;
};

class IngressData;

class AlertEvent final :
    private ZmVHeap<"Ztc.App.AlertEvent">, public ZmObject {
  using Heap = ZmVHeap<"Ztc.App.AlertEvent">;

public:
  static void *operator new(size_t size, unsigned length) {
    return Heap::valloc(size + length);
  }
  static void operator delete(void *ptr) { Heap::vfree(ptr); }
  static void operator delete(void *ptr, unsigned) { Heap::vfree(ptr); }

  AlertEvent(
      ZuCSpan message_, const ZeEventInfo &info, unsigned length_) :
      time{info.time}, tid{uint64_t(info.tid)}, length{length_},
      severity{info.severity} {
    if (length) memcpy(messageData(), message_.data(), length);
  }

  ZuCSpan message() const { return {messageData(), length}; }

  char *messageData() { return reinterpret_cast<char *>(this + 1); }
  const char *messageData() const {
    return reinterpret_cast<const char *>(this + 1);
  }

  IngressData	*ingress = nullptr;
  ZuTime	time;
  uint64_t	tid = 0;
  unsigned	length = 0;
  int8_t	severity = 0;
};

class IngressData : public ZmObject {
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

public:
  IngressData(unsigned maxMessage) : m_maxMessage{maxMessage} { }

  void open(App *app) {
    Guard guard(m_lock);
    m_target = app;
  }
  void close(App *app) {
    Guard guard(m_lock);
    if (m_target == app) m_target = nullptr;
  }
  void accept(ZeLogBuf &buf, const ZeEventInfo &info) {
    unsigned length = buf.length();
    if (length > m_maxMessage) length = m_maxMessage;
    ZmRef<AlertEvent> event =
      new (length) AlertEvent{buf, info, length};
    Guard guard(m_lock);
    App *app = m_target;
    if (!app) return;
    event->ingress = this;
    ++m_accepted;
    app->mx()->run([app, event = ZuMv(event)]() mutable {
      app->alert_(ZuMv(event));
    }, app->m_cf.workerThread);
  }
  void done() {
    Guard guard(m_lock);
    ZmAssert(m_accepted);
    --m_accepted;
  }
  bool drained() const {
    Guard guard(m_lock);
    return !m_accepted;
  }

private:
  mutable Lock	m_lock;
  App		*m_target = nullptr;
  const unsigned m_maxMessage;
  unsigned	m_accepted = 0;
};

template <typename Heap>
class Ingress__ : public Heap, public IngressData {
public:
  using IngressData::IngressData;
};
using IngressHeap = ZmHeap<"Ztc.App.Ingress", Ingress__<ZuVoid>>;
class Ingress final : public Ingress__<IngressHeap> {
public:
  using Ingress__<IngressHeap>::Ingress__;
};

template <typename Heap = ZuVoid>
class AlertSink__ : public Heap, public ZiSink {
public:
  AlertSink__(ZmRef<Ingress> ingress) :
    ZiSink{ZiSinkType::Lambda}, m_ingress{ZuMv(ingress)} { }

  void pre(ZeLogBuf &, const ZeEventInfo &) { }
  void post(ZeLogBuf &buf, const ZeEventInfo &info) {
    m_ingress->accept(buf, info);
  }
  void age() { }

private:
  ZmRef<Ingress>	m_ingress;
};
using AlertSinkHeap = ZmHeap<"Ztc.App.AlertSink", AlertSink__<>>;
class AlertSink final : public AlertSink__<AlertSinkHeap> {
public:
  using AlertSink__<AlertSinkHeap>::AlertSink__;
};

struct StateData {
  StateData() {
    unsigned n = unsigned(fbs::Group::Alert);
    due.size(n);
    for (unsigned i = 0; i < n; ++i)
      new (due.push()) SubDueIdx;
  }

  ClientIdx		clients;
  PendingIdx		pending;
  SubIdx		subs;
  SubDueSets		due;
  AlertTail		tail;
  AlertStore		store;
  ZmRef<Ingress>	ingress;
  ZmScheduler::Timer	timer;
  uint64_t		timerGeneration = 0;
  uint64_t		nextOrder = 0;
  uint64_t		alertSeqNo = 0;
  uint32_t		alertDate = 0;
  bool			degraded = false;
};

template <typename Heap>
class State_ : public Heap, public StateData { };
using StateHeap = ZmHeap<"Ztc.App.State", State_<ZuVoid>>;
class State final : public State_<StateHeap> { };

Subscription *earliest(State *state)
{
  Subscription *earliest_ = nullptr;
  unsigned n = state->due.length();
  for (unsigned i = 0; i < n; ++i) {
    auto candidate = static_cast<Subscription *>(
      state->due[i].minimumPtr());
    if (candidate && (!earliest_ || candidate->due < earliest_->due))
      earliest_ = candidate;
  }
  return earliest_;
}

ZmPLock appLock;
App *activeApp = nullptr;

bool claim(App *app)
{
  ZmGuard<ZmPLock> guard(appLock);
  if (activeApp) return false;
  activeApp = app;
  return true;
}

void release(App *app)
{
  ZmGuard<ZmPLock> guard(appLock);
  if (activeApp == app) activeApp = nullptr;
}

void warmSamples()
{
  HeapSamples{}.size(1);
  HashSamples{}.size(1);
  ThreadSamples{}.size(1);
  MxSamples{}.size(1);
  CxnSamples{}.size(1);
  QueueSamples{}.size(1);
  HubSamples{}.size(1);
  LinkSamples{}.size(1);
  PoolSamples{}.size(1);
  DBSamples{}.size(1);
  DBHostSamples{}.size(1);
  DBTableSamples{}.size(1);
}

ZuTime interval(uint32_t millisecs)
{
  return {
    int64_t(millisecs / 1000),
    int32_t(millisecs % 1000) * 1000000};
}

class FrameScan {
public:
  FrameScan(uint32_t maxFrame) : m_maxFrame{maxFrame} { }

  int64_t operator ()(ZuBSpan span) {
    uint64_t prior = m_seen;
    if (!m_total) {
      unsigned n = sizeof(Hdr) - m_hdrLength;
      if (n > span.length()) n = span.length();
      if (n) {
	memcpy(
	  reinterpret_cast<uint8_t *>(&m_hdr) + m_hdrLength,
	  span.data(), n);
	m_hdrLength += n;
      }
      if (m_hdrLength == sizeof(Hdr)) {
	uint64_t body = uint32_t(m_hdr.length);
	m_total = body + sizeof(Hdr);
	if (ZuUnlikely(
	      m_total > m_maxFrame || m_total > unsigned(INT_MAX) ||
	      m_maxFrame < sizeof(Hdr)))
	  return -1;
      }
    }
    if (m_total && prior + span.length() >= m_total)
      return int64_t(m_total - prior);
    m_seen += span.length();
    return 0;
  }

private:
  Hdr		m_hdr{};
  uint64_t	m_seen = 0;
  uint64_t	m_total = 0;
  uint32_t	m_maxFrame = 0;
  unsigned	m_hdrLength = 0;
};

template <typename Index, typename Key, typename Value>
void indexAdd(Index &index, Key &&key, Value *value)
{
  if (auto prior = index.findVal(key)) {
    ZmAssert(prior == value);
    return;
  }
  auto node = new typename Index::Node{
    ZuFwdTuple(ZuFwd<Key>(key), value)};
  index.addNode(node);
}

template <typename Index, typename Key, typename Value>
void indexDel(Index &index, const Key &key, Value *value)
{
  if (auto prior = index.delVal(key)) ZmAssert(prior == value);
}

template <typename Index, typename Key>
void indexWarm(Index &index, Key &&key)
{
  index.add(key, nullptr);
  index.del(key);
}

} // App_

class App::Link : public Ztcp::SrvLink<App, Link> {
  using Base = Ztcp::SrvLink<App, Link>;

friend App;

public:
  Link(App *app) :
    Base{app}, m_generation{++app->m_linkGeneration} { }

  void connected(Ztcp::Connected) { }
  void disconnected(bool) {
    auto app_ = app();
    uint64_t generation_ = m_generation;
    if (!app_->m_running.load_()) return;
    app_->mx()->run([app_, generation_]() {
      app_->disconnected_(generation_);
    }, app_->m_cf.workerThread);
  }

  uint64_t generation() const { return m_generation; }

  int process(Ztcp::RxStream &rx) {
    App_::FrameScan scan{app()->m_cf.maxFrame};
    ZmRef<ZiIOBuf> buf;
    int64_t n = rx.extract(
      [&scan](ZuBSpan span) { return scan(span); },
      [this]() -> ZmRef<Ztcp_::IOQueue::Node> {
	return new App_::RxFrame{this};
      },
      buf);
    if (n <= 0) return int(n);
    auto app_ = app();
    if (ZuUnlikely(!Ztc::msg(buf->ptr<Hdr>()))) {
      app_->mx()->run([
	app_, link = ZmRef(this)
      ]() mutable {
	app_->invalid_(ZuMv(link));
      }, app_->m_cf.workerThread);
      return int(n);
    }
    app_->mx()->run([
      app_, link = ZmRef(this), buf = ZuMv(buf)
    ]() mutable {
      app_->request_(ZuMv(link), ZuMv(buf));
    }, app_->m_cf.workerThread);
    return int(n);
  }

private:
  const uint64_t	m_generation;
};

namespace App_ {

template <typename T>
void sendTelemetry(
  App::Link *link, uint64_t seqNo, fbs::TelemetryBody type, const T &data)
{
  Zfb::IOBuilder fbb{
    frameBuf(ZmRef<ZiIOBuf>{new TelFrame{link}})};
  auto value =
    ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data);
  auto telemetry =
    fbs::CreateTelemetry(fbb, seqNo, type, value.Union());
  fbb.Finish(
    fbs::CreateMsg(fbb, fbs::Body::Telemetry, telemetry.Union()));
  if (auto buf = saveHdr(fbb, link)) link->send(ZuMv(buf));
}

bool sendAlertFrame(
    App::Link *link, uint64_t seqNo, const ZiIOBuf *canonical)
{
  ZmRef<ZiIOBuf> frame = new TelFrame{link};
  frame->append(canonical->data(), canonical->length);
  if (ZuUnlikely(frame->length != canonical->length)) return false;
  auto root = flatbuffers::GetMutableRoot<fbs::Msg>(
    frame->data() + sizeof(Hdr));
  auto telemetry = root->mutable_body_as_Telemetry();
  if (ZuUnlikely(!telemetry || !telemetry->mutate_seqNo(seqNo)))
    return false;
  link->send(ZuMv(frame));
  return true;
}

template <typename S, typename C>
void copySamples(S &samples, const C &captures)
{
  using T = ZuDecay<decltype(captures[0])>;
  unsigned n = captures.length();
  samples.size(n);
  for (unsigned i = 0; i < n; ++i)
    new (samples.push()) T{captures[i]};
}

template <typename S>
bool sendBatch(
    App::Link *link, uint64_t seqNo, fbs::TelemetryBody type,
    const S &samples, uint32_t &offset, uint32_t &count, unsigned &budget)
{
  unsigned n = samples.length();
  while (offset < n && budget) {
    sendTelemetry(link, seqNo, type, samples[offset++]);
    ++count;
    --budget;
  }
  return offset == n;
}

template <unsigned Mode_>
bool matchText(ZuCSpan pattern, ZuCSpan value)
{
  if constexpr (Mode_ == Filter_::Mode::All) return true;
  if constexpr (Mode_ == Filter_::Mode::Exact)
    return value.exact(pattern);
  return value.match(pattern);
}

template <unsigned TypeMode, unsigned OwnerMode, unsigned IDMode,
  typename Key>
bool queueMatch(const Filter_::Filter &filter, const Key &key)
{
  if constexpr (TypeMode == Filter_::Mode::Exact)
    if (key.template p<2>() != QueueType::T(filter.value0()))
      return false;
  return
    matchText<OwnerMode>(filter.text1(), key.template p<0>()) &&
    matchText<IDMode>(filter.text2(), key.template p<1>());
}

template <typename Samples_, typename Owner, typename Key>
void captureQueue(Samples_ &samples, Owner *owner, const Key &selected)
{
  owner->allQueues({[&samples, &selected](Ztc::Queue *queue) {
    auto key = queue->telKey();
    if (key.p<0>() != selected.template p<0>() ||
	key.p<1>() != selected.template p<1>() ||
	key.p<2>() != selected.template p<2>()) return;
    auto data = new (samples.push()) QueueTelemetry;
    queue->telemetry(*data);
  }});
}

template <unsigned TypeMode, unsigned OwnerMode, unsigned IDMode,
  typename Samples_, typename Index>
void captureQueues(
    Samples_ &samples, const Filter_::Filter &filter, const Index &index)
{
  ZuCSpan owner = filter.text1();
  ZuCSpan id = filter.text2();
  QueueType::T type = QueueType::T(filter.value0());
  if constexpr (
      TypeMode == Filter_::Mode::Exact &&
      OwnerMode == Filter_::Mode::Exact &&
      IDMode == Filter_::Mode::Exact) {
    auto key = ZuFwdTuple(owner, id, type);
    if (auto object = index.findVal(key))
      captureQueue(samples, object, key);
    return;
  }
  if constexpr (OwnerMode == Filter_::Mode::All) {
    auto i = index.citer();
    while (auto node = i())
      if (queueMatch<TypeMode, OwnerMode, IDMode>(filter, node->key()))
	captureQueue(samples, node->val(), node->key());
  } else {
    ZuCSpan firstID =
      OwnerMode == Filter_::Mode::Exact &&
      IDMode != Filter_::Mode::All ? id : ZuCSpan{};
    auto first = ZuFwdTuple(owner, firstID, QueueType::T{});
    auto i = index.template citer<ZmRBTreeGreaterEqual>(first);
    while (auto node = i()) {
      const auto &key = node->key();
      ZuCSpan keyOwner = key.template p<0>();
      if (!matchText<OwnerMode>(owner, keyOwner)) break;
      if constexpr (
	  OwnerMode == Filter_::Mode::Exact &&
	  IDMode != Filter_::Mode::All) {
	ZuCSpan keyID = key.template p<1>();
	if (!matchText<IDMode>(id, keyID)) break;
      }
      if (queueMatch<TypeMode, OwnerMode, IDMode>(filter, key))
	captureQueue(samples, node->val(), key);
    }
  }
}

} // App_

App::~App()
{
  final();
}

bool App::init(const AppCf &cf)
{
  if (m_initialized || !cf.ip || !cf.ip.loopback() || cf.ip.wildcard())
    return false;
  if (cf.maxFrame < sizeof(Hdr) + 8 ||
      cf.maxFrame > AppCf::DefltMaxFrame ||
      !cf.nAccepts || cf.nAccepts > 1024 ||
      !cf.maxFilter || cf.maxFilter > (1U<<20) ||
      uint64_t(cf.maxFilter) + sizeof(Hdr) + 128 > cf.maxFrame ||
      uint64_t(cf.maxAlertMsg) + sizeof(Hdr) + 128 > cf.maxFrame ||
      !cf.minInterval || cf.minInterval > cf.maxInterval ||
      cf.maxInterval > 3600000 ||
      !cf.maxPending || !cf.maxSubs || !cf.maxAlertMsg ||
      !cf.alertTail || !cf.alertReplay ||
      !cf.alertRetention ||
      cf.maxPending > 65536 || cf.maxSubs > 65536 ||
      cf.maxAlertMsg > (1U<<20) || cf.alertTail > (1U<<20) ||
      cf.alertReplay > (1U<<20) || cf.alertRetention > 3660 ||
      cf.rebindFreq > 3600 ||
      !cf.timerThread || !cf.rxThread || !cf.txThread || !cf.workerThread ||
      !cf.mx.nThreads || cf.mx.nThreads > 1024 ||
      (cf.mx.stackSize &&
	(cf.mx.stackSize < 16384 || cf.mx.stackSize > (2U<<20))) ||
      (cf.mx.queueSize && cf.mx.queueSize < 8192) ||
      cf.mx.timeout > 3600 ||
      cf.timerThread > cf.mx.nThreads ||
      cf.rxThread > cf.mx.nThreads ||
      cf.txThread > cf.mx.nThreads ||
      cf.workerThread > cf.mx.nThreads ||
      !cf.timerRole || !cf.mx.rxThread || !cf.mx.txThread || !cf.workerRole ||
      cf.timerRole == cf.mx.rxThread ||
      cf.timerRole == cf.mx.txThread ||
      cf.timerRole == cf.workerRole ||
      cf.mx.rxThread == cf.mx.txThread ||
      cf.mx.rxThread == cf.workerRole ||
      cf.mx.txThread == cf.workerRole ||
      cf.timerThread == cf.rxThread ||
      cf.timerThread == cf.txThread ||
      cf.timerThread == cf.workerThread ||
      cf.rxThread == cf.txThread ||
      cf.rxThread == cf.workerThread ||
      cf.txThread == cf.workerThread)
    return false;
  if (!App_::claim(this)) return false;

  bool mxConstructed = false;
  try {
    m_cf = cf;
    m_state = new App_::State;
    m_state->ingress = new App_::Ingress{cf.maxAlertMsg};
    {
      ZmRef<ZiSink> sink = new App_::AlertSink{m_state->ingress};
      ZmRef<App_::AlertEvent> event =
	new (0) App_::AlertEvent{{}, ZeEventInfo{}, 0};
    }
    m_state->tail.init(ZmQueueParams{}.initial(cf.alertTail));
    m_state->store.init(cf.alertPrefix, cf.maxFrame, cf.alertRetention);
    ZiMxParams params;
    params.scheduler([
      id = cf.id, mx = cf.mx,
      timerRole = cf.timerRole,
      workerRole = cf.workerRole,
      timerThread = cf.timerThread,
      rxThread = cf.rxThread,
      txThread = cf.txThread,
      workerThread = cf.workerThread
    ](auto &sched) {
      sched.id(id).nThreads(mx.nThreads).priority(mx.priority)
	.partition(mx.partition).ll(mx.ll).spin(mx.spin).timeout(mx.timeout)
	.thread(timerThread, [timerRole](auto &thread) {
	  thread.name(timerRole);
	  thread.isolated(true);
	})
	.thread(rxThread, [rxRole = mx.rxThread](auto &thread) {
	  thread.name(rxRole);
	  thread.isolated(true);
	})
	.thread(txThread, [txRole = mx.txThread](auto &thread) {
	  thread.name(txRole);
	  thread.isolated(true);
	})
	.thread(workerThread, [workerRole](auto &thread) {
	  thread.name(workerRole);
	  thread.isolated(true);
	});
      if (mx.stackSize) sched.stackSize(mx.stackSize);
      if (mx.quantum > 0) sched.quantum(mx.quantum);
      if (mx.queueSize) sched.queueSize(mx.queueSize);
    }).rxThread(cf.rxThread).txThread(cf.txThread)
      .rxBufSize(cf.mx.rcvBufSize).txBufSize(cf.mx.sndBufSize);
#ifdef ZiMultiplex_EPoll
    if (cf.mx.epollMaxFDs) params.epollMaxFDs(cf.mx.epollMaxFDs);
    if (cf.mx.epollQuantum) params.epollQuantum(cf.mx.epollQuantum);
#endif
#ifdef ZiMultiplex_DEBUG
    params.trace(cf.mx.trace).debug(cf.mx.debug).frag(cf.mx.frag)
      .yield(cf.mx.yield);
#endif
    new (m_mx.new_<ZiMultiplex>()) ZiMultiplex{ZuMv(params)};
    mxConstructed = true;
    App_::warmSamples();
    warmIndices_();
    watch_();
    m_startTime = Zm::now().sec();
    m_initialized = true;
    return true;
  } catch (...) {
    unwatch_();
    clearIndices_();
    if (mxConstructed)
      m_mx.new_<void>();
    else
      m_mx.new_<void, true>();
    delete m_state;
    m_state = nullptr;
    App_::release(this);
    return false;
  }
}

void App::start(CtrlFn fn)
{
  {
    CtrlGuard guard(m_ctrlLock);
    if (!m_initialized) {
      guard.unlock();
      if (fn) fn(false);
      return;
    }
    if (m_running.load_()) {
      guard.unlock();
      if (fn) fn(true);
      return;
    }
    if (m_startPending || m_serverInitialized) {
      guard.unlock();
      if (fn) fn(false);
      return;
    }
    m_startPending = true;
    m_startFn = ZuMv(fn);
  }

  ZiMultiplex *mx_ = serviceMx_();
  if (!mx_ || !mx_->start()) {
    startDone_(false);
    return;
  }
  ZuTime now = Zm::now();
  if (!m_state->store.start(now)) m_state->degraded = true;
  m_state->alertDate = 0;
  m_state->alertSeqNo = 0;
  uint32_t alertDate = 0;
  uint64_t alertSeqNo = 0;
  if (m_state->store.latest(now, alertDate, alertSeqNo)) {
    uint64_t preparedSeqNo = 0;
    if (!m_state->store.prepare(alertDate, preparedSeqNo)) {
      m_state->degraded = true;
      preparedSeqNo = alertSeqNo;
    }
    m_state->alertDate = alertDate;
    m_state->alertSeqNo = preparedSeqNo;
  }

  if (!Base::init(Ztcp::ServerParams{
      mx_, m_cf.mx.rxThread, m_cf.mx.txThread})) {
    m_state->store.close();
    mx_->stop();
    startDone_(false);
    return;
  }
  m_serverInitialized = true;
  Base::start([this](bool ok) {
    if (!ok) {
      startDone_(false);
      return;
    }
    Base::listen();
  });
}

bool App::start()
{
  ZiMultiplex *mx_ = serviceMx_();
  if (mx_ && mx_->running()) {
    auto tid = Zm::getTID();
    if (mx_->invoked_(tid, m_cf.timerThread) ||
	mx_->invoked_(tid, m_cf.rxThread) ||
	mx_->invoked_(tid, m_cf.txThread) ||
	mx_->invoked_(tid, m_cf.workerThread))
      return false;
  }
  bool ok = ZmBlock<bool>{}(
    [this](auto wake) { start(CtrlFn{ZuMv(wake)}); });
  if (!ok) stop();
  return ok;
}

void App::startDone_(bool ok)
{
  CtrlFn fn;
  {
    CtrlGuard guard(m_ctrlLock);
    if (!m_startPending) return;
    m_startPending = false;
    fn = ZuMv(m_startFn);
  }
  if (fn) fn(ok);
}

void App::stop(CtrlFn fn)
{
  bool ok = stop();
  if (fn) fn(ok);
}

bool App::stop()
{
  if (!m_initialized) return true;
  ZiMultiplex *mx_ = serviceMx_();
  bool mxRunning = mx_ && mx_->running();
  if (mxRunning) {
    auto tid = Zm::getTID();
    if (mx_->invoked_(tid, m_cf.timerThread) ||
	mx_->invoked_(tid, m_cf.rxThread) ||
	mx_->invoked_(tid, m_cf.txThread) ||
	mx_->invoked_(tid, m_cf.workerThread))
      return false;
  }
  startDone_(false);
  m_state->ingress->close(this);
  m_running = false;
  if (m_serverInitialized) {
    Base::stop();
    Base::final();
    m_serverInitialized = false;
  }
  m_state->ingress->close(this);
  m_running = false;
  if (mxRunning) {
    ZmBlock<>{}([this, mx_](auto wake) mutable {
      mx_->run([this, wake = ZuMv(wake)]() mutable {
	clearSubscriptions_();
	wake();
      }, m_cf.workerThread);
    });
    ZmBlock<>{}([this, mx_](auto wake) mutable {
      mx_->run([wake = ZuMv(wake)]() mutable { wake(); },
	m_cf.timerThread);
    });
    ZmBlock<>{}([this, mx_](auto wake) mutable {
      mx_->run([wake = ZuMv(wake)]() mutable { wake(); },
	m_cf.workerThread);
    });
    ZmAssert(m_state->ingress->drained());
  }
  if (mxRunning) mx_->stop();
  m_state->store.close();
  m_boundPort = 0;
  return true;
}

void App::final()
{
  if (!m_initialized) return;
  bool stopped = stop();
  ZiAssert(stopped, "Ztc.App", (),
    "final() called from an owned scheduler thread", return);
  unwatch_();
  clearIndices_();
  m_mx.new_<void>();
  delete m_state;
  m_state = nullptr;
  m_initialized = false;
  App_::release(this);
}

void App::listening(const ZiListenInfo &info)
{
  Base::listening(info);
  m_boundPort = info.port;
  m_running = true;
  m_state->ingress->open(this);
  startDone_(true);
}

void App::listenFailed(bool transient)
{
  Base::listenFailed(transient);
  if (transient && m_cf.rebindFreq) return;
  startDone_(false);
}

ZiConnection *App::accepted(const ZiCxnInfo &ci)
{
  return new Link::Cxn{new Link{this}, ci};
}

void App::rag(RagFn fn) const
{
  if (!fn) return;
  if (!m_running.load_()) {
    fn(RAG::T(m_rag.load_()));
    return;
  }
  const App *app = this;
  Base::mx()->run([app, fn = ZuMv(fn)]() mutable {
    fn(RAG::T(app->m_rag.load_()));
  }, m_cf.workerThread);
}

ZmRef<ZiSink> App::alertSink() const
{
  if (!m_state || !m_state->ingress) return {};
  return new App_::AlertSink{m_state->ingress};
}

void App::rag(RAG::T rag)
{
  if (!m_running.load_()) {
    m_rag = rag;
    return;
  }
  App *app = this;
  Base::mx()->run([app, rag]() { app->rag_(rag); }, m_cf.workerThread);
}

void App::rag_(RAG::T rag)
{
  m_rag = rag;
  auto i = m_state->subs.iter();
  while (auto sub = i())
    if (sub->group == fbs::Group::App) sub->dirty = true;
}

void App::alert_(ZmRef<App_::AlertEvent> event)
{
  ZuGuard done{[ingress = event->ingress]() { ingress->done(); }};

  try {
    uint32_t date = ZuDateTime{event->time}.yyyymmdd();
    if (date != m_state->alertDate) {
      m_state->alertDate = date;
      uint64_t seqNo = 0;
      if (!m_state->store.prepare(date, seqNo))
	m_state->degraded = true;
      m_state->alertSeqNo = seqNo;
    }
    uint64_t alertSeqNo = m_state->alertSeqNo++;

    Zfb::IOBuilder fbb{
      frameBuf(ZmRef<ZiIOBuf>{new App_::AlertFrame})};
    fbb.ForceDefaults(true);
    auto message =
      fbb.CreateString(event->message().data(), event->message().length());
    Zfb::Time time{event->time.sec(), event->time.nsec()};
    auto value = fbs::CreateAlertTelemetry(
      fbb, message, &time, alertSeqNo, event->tid, date, event->severity);
    auto telemetry = fbs::CreateTelemetry(
      fbb, 0, fbs::TelemetryBody::AlertTelemetry, value.Union());
    fbb.Finish(
      fbs::CreateMsg(fbb, fbs::Body::Telemetry, telemetry.Union()));
    ZmRef<ZiIOBuf> canonical = saveHdr(fbb);
    if (ZuUnlikely(!canonical || canonical->length > m_cf.maxFrame)) {
      m_state->degraded = true;
      return;
    }

    if (!m_state->store.append(canonical.ptr(), alertSeqNo))
      m_state->degraded = true;

    while (m_state->tail.count_() >= m_cf.alertTail)
      m_state->tail.shift();
    m_state->tail.push(canonical);

    auto i = m_state->subs.iter();
    while (App_::Subscription *sub = i()) {
      if (sub->group != fbs::Group::Alert) continue;
      if (sub->replaying) {
	if (sub->oneShot) continue;
	if (sub->pending.count_() >= m_cf.alertReplay) {
	  sub->replayFailed = true;
	  continue;
	}
	sub->pending.push(App_::PendingAlert{
	  .frame = canonical, .seqNo = alertSeqNo, .date = date});
	continue;
      }
      if (!App_::sendAlertFrame(
	    sub->link.ptr(), sub->seqNo, canonical.ptr())) {
	m_state->degraded = true;
	continue;
      }
    }
  } catch (...) {
    m_state->degraded = true;
  }
}

void App::warmIndices_()
{
  delete new App_::Client_;
  delete new App_::Pending_;
  App_::indexWarm(m_state->clients, uint64_t{});
  App_::indexWarm(
    m_state->pending, App_::PendingKey{uint64_t{}, uint64_t{}});
  App_::indexWarm(m_mxIdx, ZuID{});
  App_::indexWarm(m_cxnIdx,
    CxnKey{ZuID{}, ZiIP{}, uint16_t{}, ZiIP{}, uint16_t{}});
  App_::indexWarm(m_mxQueueIdx,
    QueueKey{ZuID{}, ZuID{}, QueueType::T{}});
  App_::indexWarm(m_hubIdx, HubKey{LinkType::T{}, ZuID{}});
  App_::indexWarm(m_linkIdx, ChildKey{ZuID{}, ZuID{}});
  App_::indexWarm(m_poolIdx, ChildKey{ZuID{}, ZuID{}});
  App_::indexWarm(m_linkQueueIdx,
    QueueKey{ZuID{}, ZuID{}, QueueType::T{}});
  App_::indexWarm(m_poolQueueIdx,
    QueueKey{ZuID{}, ZuID{}, QueueType::T{}});
  App_::indexWarm(m_dbIdx, ZuID{});
  App_::indexWarm(m_dbHostIdx, ChildKey{ZuID{}, ZuID{}});
  App_::indexWarm(m_dbTableIdx, DBTableKey{ZuID{}, Ztc::DBTableID{}});
}

void App::clearIndices_()
{
  m_dbTableIdx.clean();
  m_dbHostIdx.clean();
  m_dbIdx.clean();
  m_poolQueueIdx.clean();
  m_linkQueueIdx.clean();
  m_poolIdx.clean();
  m_linkIdx.clean();
  m_hubIdx.clean();
  m_mxQueueIdx.clean();
  m_cxnIdx.clean();
  m_mxIdx.clean();
}

App_::Client_ *App::client_(ZmRef<Link> link)
{
  uint64_t generation = link->generation();
  if (auto client = m_state->clients.findVal(generation)) return client;
  auto client = new App_::Client_;
  client->link = ZuMv(link);
  client->generation = generation;
  App_::indexAdd(m_state->clients, generation, client);
  return client;
}

void App::clientRelease_(App_::Client_ *client)
{
  if (!client || client->pending || client->subs) return;
  App_::indexDel(
    m_state->clients, client->generation, client);
  delete client;
}

void App::watch_()
{
  if (m_watching) return;
  m_watching = true;

  MxMgr::watch(
    {[this](Ztc::Mx *mx_) { mxAdded_(mx_); }},
    {[this](Ztc::Mx *mx_) { mxDeleted_(mx_); }});
  HubMgr::watch(
    {[this](Ztc::Hub *hub) { hubAdded_(hub); }},
    {[this](Ztc::Hub *hub) { hubDeleted_(hub); }},
    {[this](Ztc::Link *link) { idxLinkAdded_(link); }},
    {[this](Ztc::Link *link) { idxLinkDeleted_(link); }},
    {[this](Ztc::Pool *pool) { poolAdded_(pool); }},
    {[this](Ztc::Pool *pool) { poolDeleted_(pool); }},
    {[this](Ztc::Queue *queue) { hubQueueAdded_(queue); }},
    {[this](Ztc::Queue *queue) { hubQueueDeleted_(queue); }});
  DBMgr::watch(
    {[this](DB *db) { dbAdded_(db); }},
    {[this](DB *db) { dbDeleted_(db); }},
    {[this](DBHost *host) { dbHostAdded_(host); }},
    {[this](DBHost *host) { dbHostDeleted_(host); }},
    {[this](DBTable *table) { dbTableAdded_(table); }},
    {[this](DBTable *table) { dbTableDeleted_(table); }});

  MxMgr::all({[this](Ztc::Mx *mx_) { mxAdded_(mx_); }});
  HubMgr::all({[this](Ztc::Hub *hub) { hubAdded_(hub); }});
  DBMgr::all({[this](DB *db) { dbAdded_(db); }});
}

void App::unwatch_()
{
  if (!m_watching) return;
  DBMgr::unwatch();
  HubMgr::unwatch();
  MxMgr::all({[this](Ztc::Mx *mx_) {
    if (mx_ != serviceMx_()) mx_->unwatch();
  }});
  MxMgr::unwatch();
  m_watching = false;
}

void App::mxAdded_(Ztc::Mx *mx_)
{
  App_::indexAdd(m_mxIdx, ZuID{mx_->telKey()}, mx_);
  if (mx_ != serviceMx_()) {
    mx_->watch(
      {[this](Ztc::Connection *cxn) { cxnAdded_(cxn); }},
      {[this](Ztc::Connection *cxn) { cxnDeleted_(cxn); }});
    mx_->allCxns(
      {[this](Ztc::Connection *cxn) { cxnAdded_(cxn); }});
  }
  mx_->allQueues(
    {[this](Ztc::Queue *queue) { mxQueueAdded_(queue); }});
}

void App::mxDeleted_(Ztc::Mx *mx_)
{
  if (mx_ != serviceMx_()) mx_->unwatch();
  mx_->allQueues(
    {[this](Ztc::Queue *queue) { mxQueueDeleted_(queue); }});
  App_::indexDel(m_mxIdx, ZuID{mx_->telKey()}, mx_);
}

void App::cxnAdded_(Ztc::Connection *cxn)
{
  auto key = cxn->telKey();
  App_::indexAdd(m_cxnIdx,
    CxnKey{
      key.p<0>(), key.p<1>(), key.p<2>(), key.p<3>(), key.p<4>()},
    cxn);
}

void App::cxnDeleted_(Ztc::Connection *cxn)
{
  auto key = cxn->telKey();
  App_::indexDel(m_cxnIdx,
    CxnKey{
      key.p<0>(), key.p<1>(), key.p<2>(), key.p<3>(), key.p<4>()},
    cxn);
}

void App::mxQueueAdded_(Ztc::Queue *queue)
{
  auto key = queue->telKey();
  QueueKey owned{key.p<0>(), key.p<1>(), key.p<2>()};
  if (auto mx_ = m_mxIdx.findVal(key.p<0>()))
    App_::indexAdd(m_mxQueueIdx, ZuMv(owned), mx_);
}

void App::mxQueueDeleted_(Ztc::Queue *queue)
{
  auto key = queue->telKey();
  QueueKey owned{key.p<0>(), key.p<1>(), key.p<2>()};
  if (auto mx_ = m_mxQueueIdx.findVal(owned))
    App_::indexDel(m_mxQueueIdx, owned, mx_);
}

void App::hubAdded_(Ztc::Hub *hub)
{
  auto key = hub->telKey();
  App_::indexAdd(m_hubIdx, HubKey{key.p<0>(), key.p<1>()}, hub);
  hub->allLinks({[this](Ztc::Link *link) {
    idxLinkAdded_(link);
    link->allQueues(
      {[this](Ztc::Queue *queue) { hubQueueAdded_(queue); }});
  }});
  hub->allPools({[this](Ztc::Pool *pool) {
    poolAdded_(pool);
    pool->allQueues(
      {[this](Ztc::Queue *queue) { hubQueueAdded_(queue); }});
  }});
}

void App::hubDeleted_(Ztc::Hub *hub)
{
  auto key = hub->telKey();
  App_::indexDel(m_hubIdx, HubKey{key.p<0>(), key.p<1>()}, hub);
}

void App::idxLinkAdded_(Ztc::Link *link)
{
  auto key = link->telKey();
  App_::indexAdd(m_linkIdx, ChildKey{key.p<0>(), key.p<1>()}, link);
}

void App::idxLinkDeleted_(Ztc::Link *link)
{
  auto key = link->telKey();
  App_::indexDel(m_linkIdx, ChildKey{key.p<0>(), key.p<1>()}, link);
}

void App::poolAdded_(Ztc::Pool *pool)
{
  auto key = pool->telKey();
  App_::indexAdd(m_poolIdx, ChildKey{key.p<0>(), key.p<1>()}, pool);
}

void App::poolDeleted_(Ztc::Pool *pool)
{
  auto key = pool->telKey();
  App_::indexDel(m_poolIdx, ChildKey{key.p<0>(), key.p<1>()}, pool);
}

void App::hubQueueAdded_(Ztc::Queue *queue)
{
  auto key = queue->telKey();
  QueueKey owned{key.p<0>(), key.p<1>(), key.p<2>()};
  ChildKey child{key.p<0>(), key.p<1>()};
  if (auto link = m_linkIdx.findVal(child)) {
    App_::indexAdd(m_linkQueueIdx, ZuMv(owned), link);
    return;
  }
  if (auto pool = m_poolIdx.findVal(child))
    App_::indexAdd(m_poolQueueIdx, ZuMv(owned), pool);
}

void App::hubQueueDeleted_(Ztc::Queue *queue)
{
  auto key = queue->telKey();
  QueueKey owned{key.p<0>(), key.p<1>(), key.p<2>()};
  if (auto link = m_linkQueueIdx.findVal(owned)) {
    App_::indexDel(m_linkQueueIdx, owned, link);
    return;
  }
  if (auto pool = m_poolQueueIdx.findVal(owned))
    App_::indexDel(m_poolQueueIdx, owned, pool);
}

void App::dbAdded_(DB *db)
{
  App_::indexAdd(m_dbIdx, ZuID{db->telKey()}, db);
  db->allDBHosts(
    {[this](DBHost *host) { dbHostAdded_(host); }});
  db->allDBTables(
    {[this](DBTable *table) { dbTableAdded_(table); }});
}

void App::dbDeleted_(DB *db)
{
  App_::indexDel(m_dbIdx, ZuID{db->telKey()}, db);
}

void App::dbHostAdded_(DBHost *host)
{
  auto key = host->telKey();
  App_::indexAdd(
    m_dbHostIdx, ChildKey{key.p<0>(), key.p<1>()}, host);
}

void App::dbHostDeleted_(DBHost *host)
{
  auto key = host->telKey();
  App_::indexDel(
    m_dbHostIdx, ChildKey{key.p<0>(), key.p<1>()}, host);
}

void App::dbTableAdded_(DBTable *table)
{
  auto key = table->telKey();
  App_::indexAdd(
    m_dbTableIdx, DBTableKey{key.p<0>(), key.p<1>()}, table);
}

void App::dbTableDeleted_(DBTable *table)
{
  auto key = table->telKey();
  App_::indexDel(
    m_dbTableIdx, DBTableKey{key.p<0>(), key.p<1>()}, table);
}

void App::request_(ZmRef<Link> link, ZmRef<ZiIOBuf> buf)
{
  auto root = msg_(buf->ptr<Hdr>());
  if (ZuUnlikely(root->body_type() != fbs::Body::Request)) {
    sendError_(link.ptr(), 1, "expected Request message");
    return;
  }
  auto request = root->body_as_Request();
  if (ZuUnlikely(!request)) return;
  uint64_t seqNo = request->seqNo();
  ZuCSpan filter = Zfb::Load::str(request->filter());
  auto group = request->group();
  if (ZuUnlikely(
	int(group) < int(fbs::Group::Heap) ||
	int(group) > int(fbs::Group::Alert))) {
    sendAck_(link.ptr(), seqNo, fbs::AckStatus::Invalid);
    return;
  }
  Filter_::Filter compiled;
  if (ZuUnlikely(!compiled.compile(group, filter, m_cf.maxFilter))) {
    sendAck_(link.ptr(), seqNo, fbs::AckStatus::Invalid);
    return;
  }
  uint32_t alertDate = request->alertDate();
  uint64_t alertSeqNo = request->alertSeqNo();
  if (group != fbs::Group::Alert) {
    if (alertDate || alertSeqNo) {
      sendAck_(link.ptr(), seqNo, fbs::AckStatus::Invalid);
      return;
    }
  } else if ((alertDate && !App_::AlertStore::validDate(alertDate)) ||
      (!alertDate && alertSeqNo)) {
    sendAck_(link.ptr(), seqNo, fbs::AckStatus::Invalid);
    return;
  } else if (alertDate && m_state->alertDate &&
      (alertDate < m_state->store.earliest(m_state->alertDate) ||
       alertDate > m_state->alertDate ||
       (alertDate == m_state->alertDate && m_state->alertSeqNo &&
	alertSeqNo >= m_state->alertSeqNo))) {
    sendAck_(link.ptr(), seqNo, fbs::AckStatus::Invalid);
    return;
  }
  if (!request->subscribe()) {
    unsubscribe_(link.ptr(), seqNo, group, compiled);
    return;
  }
  uint32_t interval = request->interval();
  if (!interval) {
    if (group == fbs::Group::Alert) {
      subscribe_(ZuMv(link), seqNo, group, ZuMv(compiled), 0,
	alertDate, alertSeqNo);
	return;
      }
    if (!snapshot_(link, seqNo, group, compiled)) {
      sendAck_(link.ptr(), seqNo, fbs::AckStatus::Failed);
      return;
    }
    sendAck_(link.ptr(), seqNo, fbs::AckStatus::OK);
    return;
  }
  if (ZuUnlikely(interval > m_cf.maxInterval)) {
    sendAck_(link.ptr(), seqNo, fbs::AckStatus::Invalid);
    return;
  }
  if (interval < m_cf.minInterval) interval = m_cf.minInterval;
  subscribe_(ZuMv(link), seqNo, group, ZuMv(compiled), interval,
    alertDate, alertSeqNo);
}

void App::subscribe_(
    ZmRef<Link> link, uint64_t seqNo, fbs::Group group,
    Filter_::Filter filter, uint32_t interval,
    uint32_t alertDate, uint64_t alertSeqNo)
{
  App_::SubKey key{
    link->generation(), uint8_t(group), filter.key()};
  App_::Subscription *sub = m_state->subs.findPtr(key);
  App_::Pending_ *sameSeq = m_state->pending.findVal(
    App_::PendingKey{link->generation(), seqNo});
  if (group != fbs::Group::Alert &&
      (((!sub || !sub->running) &&
	m_state->pending.count_() >= m_cf.maxPending) ||
       (sameSeq && (!sub || sameSeq != sub->snapshot)))) {
    sendAck_(link.ptr(), seqNo, fbs::AckStatus::Failed);
    return;
  }
  if (sub) {
    if (sub->inDue) {
      m_state->due[unsigned(sub->group)].delNode(sub);
      sub->inDue = false;
    }
    sub->seqNo = seqNo;
    sub->interval = interval;
    sub->dirty = true;
  } else {
    App_::Client_ *client = client_(link);
    if (ZuUnlikely(client->subs >= m_cf.maxSubs)) {
      sendAck_(link.ptr(), seqNo, fbs::AckStatus::Failed);
      clientRelease_(client);
      return;
    }
    sub = new App_::Subscription;
    sub->link = link;
    sub->client = client;
    sub->filter = ZuMv(filter);
    sub->generation = link->generation();
    sub->seqNo = seqNo;
    sub->order = ++m_state->nextOrder;
    sub->interval = interval;
    sub->group = group;
    if (group == fbs::Group::Alert)
      sub->pending.init(ZmQueueParams{}.initial(m_cf.alertReplay));
    m_state->subs.addNode(sub);
    ++client->subs;
  }
  sendAck_(link.ptr(), seqNo, fbs::AckStatus::OK, interval);
  if (group == fbs::Group::Alert) {
    sub->oneShot = !interval;
    sub->replayDate = alertDate;
    sub->replaySeqNo = alertSeqNo;
    replay_(sub);
    return;
  }
  runSubscription_(sub);
  armTimer_();
}

void App::unsubscribe_(
    Link *link, uint64_t seqNo, fbs::Group group,
    const Filter_::Filter &filter)
{
  App_::SubKey key{
    link->generation(), uint8_t(group), filter.key()};
  if (App_::Subscription *sub = m_state->subs.findPtr(key)) {
    if (sub->inDue)
      m_state->due[unsigned(sub->group)].delNode(sub);
    sub->inDue = false;
    if (auto pending = sub->snapshot) {
      App_::indexDel(m_state->pending,
	App_::PendingKey{pending->generation, pending->seqNo}, pending);
      pending->sub = nullptr;
      sub->snapshot = nullptr;
      sub->running = false;
      --sub->client->pending;
      delete pending;
    }
    App_::Client_ *client = sub->client;
    --client->subs;
    m_state->subs.delNode(sub);
    clientRelease_(client);
    armTimer_();
  }
  sendAck_(link, seqNo, fbs::AckStatus::OK);
}

void App::runSubscription_(App_::Subscription_ *sub_)
{
  auto sub = static_cast<App_::Subscription *>(sub_);
  if (sub->running) {
    sub->dirty = true;
    return;
  }
  sub->dirty = false;
  if (!snapshot_(sub->link, sub->seqNo, sub->group, sub->filter, sub)) {
    sub->dirty = true;
    sub->due = Zm::now() + App_::interval(sub->interval);
    m_state->due[unsigned(sub->group)].addNode(sub);
    sub->inDue = true;
    return;
  }
  if (sub->interval) {
    sub->due = Zm::now() + App_::interval(sub->interval);
    m_state->due[unsigned(sub->group)].addNode(sub);
    sub->inDue = true;
  }
}

void App::replay_(App_::Subscription_ *sub_)
{
  auto sub = static_cast<App_::Subscription *>(sub_);
  sub->pending.clean();
  sub->replayData.close();
  sub->replayIndex.close();
  sub->openDate = 0;
  sub->replayCount = 0;
  sub->replayFailed = false;
  sub->highDate = m_state->alertDate;
  sub->highSeqNo = m_state->alertSeqNo ? m_state->alertSeqNo - 1 : 0;
  if (!sub->highDate || !m_state->alertSeqNo) {
    finishReplay_(sub);
    return;
  }

  uint32_t earliest = m_state->store.earliest(sub->highDate);
  if (!sub->replayDate) {
    sub->replayDate = earliest;
    sub->replaySeqNo = 0;
  } else {
    if (sub->replayDate < earliest || sub->replayDate > sub->highDate) {
      failReplay_(sub, "invalid alert replay lower bound");
      return;
    }
    if (sub->replaySeqNo == UINT64_MAX) {
      sub->replayDate = App_::AlertStore::addDays(sub->replayDate, 1);
      sub->replaySeqNo = 0;
    } else {
      ++sub->replaySeqNo;
    }
  }
  sub->replaying = true;
  replayBatch_(sub->order);
}

void App::replayBatch_(uint64_t order)
{
  App_::Subscription *sub = nullptr;
  auto i = m_state->subs.iter();
  while (auto candidate = i()) {
    if (candidate->order == order) {
      sub = candidate;
      break;
    }
  }
  if (!sub || !sub->replaying) return;
  if (sub->replayFailed) {
    failReplay_(sub, "alert replay handoff overflow", true);
    return;
  }

  unsigned n = 0;
  while (n < m_cf.maxPending) {
    if (sub->replayDate > sub->highDate ||
	(sub->replayDate == sub->highDate &&
	 sub->replaySeqNo > sub->highSeqNo)) {
      finishReplay_(sub);
      return;
    }
    ZmRef<ZiIOBuf> frame;
    auto result = m_state->store.load(
      sub->replayDate, sub->replaySeqNo, frame,
      sub->replayData, sub->replayIndex, sub->openDate);
    if (result == App_::AlertStore::LoadResult::OK) {
      if (!App_::sendAlertFrame(
	    sub->link.ptr(), sub->seqNo, frame.ptr())) {
	m_state->degraded = true;
	failReplay_(sub, "alert replay frame allocation failed");
	return;
      }
      ++sub->replayCount;
      ++sub->replaySeqNo;
      ++n;
      continue;
    }
    if (result != App_::AlertStore::LoadResult::Missing ||
	sub->replayDate == sub->highDate) {
      m_state->degraded = true;
      failReplay_(sub, "alert replay partition read failed");
      return;
    }
    sub->replayDate = App_::AlertStore::addDays(sub->replayDate, 1);
    sub->replaySeqNo = 0;
  }

  auto mx_ = serviceMx_();
  if (!mx_ || !mx_->running()) return;
  mx_->run([this, order]() { replayBatch_(order); }, m_cf.workerThread);
}

void App::finishReplay_(App_::Subscription_ *sub_)
{
  auto sub = static_cast<App_::Subscription *>(sub_);
  sub->replaying = false;
  sub->replayData.close();
  sub->replayIndex.close();
  sub->openDate = 0;
  sendComplete_(sub->link.ptr(), sub->seqNo, sub->replayCount);
  if (sub->oneShot) {
    App_::Client_ *client = sub->client;
    --client->subs;
    m_state->subs.delNode(sub);
    clientRelease_(client);
    return;
  }
  uint32_t lastDate = sub->highDate;
  uint64_t lastSeqNo = sub->highSeqNo;
  while (auto pending = sub->pending.shift()) {
    if (pending.date < lastDate ||
	(pending.date == lastDate && pending.seqNo <= lastSeqNo))
      continue;
    if (!App_::sendAlertFrame(
	  sub->link.ptr(), sub->seqNo, pending.frame.ptr())) {
      m_state->degraded = true;
      sub->link->disconnect();
      break;
    }
    lastDate = pending.date;
    lastSeqNo = pending.seqNo;
  }
}

void App::failReplay_(
    App_::Subscription_ *sub_, ZuCSpan message, bool disconnect)
{
  auto sub = static_cast<App_::Subscription *>(sub_);
  sub->replaying = false;
  sub->replayData.close();
  sub->replayIndex.close();
  sub->openDate = 0;
  sendError_(sub->link.ptr(), 2, message);
  if (disconnect) sub->link->disconnect();
  App_::Client_ *client = sub->client;
  --client->subs;
  m_state->subs.delNode(sub);
  clientRelease_(client);
}

void App::armTimer_()
{
  ZiMultiplex *mx_ = serviceMx_();
  if (!mx_ || !mx_->running()) return;
  uint64_t generation = ++m_state->timerGeneration;
  mx_->del(&m_state->timer);
  App_::Subscription *sub = App_::earliest(m_state);
  if (!sub) return;
  ZuTime deadline = sub->due;
  mx_->add(&m_state->timer, deadline, ZmScheduler::Update,
    [this, generation, deadline](auto &&arm) {
      return arm([this, generation, deadline]() {
	if (!m_running.load_()) return;
	auto mx__ = serviceMx_();
	if (!mx__) return;
	mx__->run([this, generation, deadline]() {
	  timerFired_(generation, deadline);
	}, m_cf.workerThread);
      });
    }, m_cf.timerThread);
}

void App::timerFired_(uint64_t generation, ZuTime)
{
  if (!m_running.load_() || generation != m_state->timerGeneration)
    return;
  ZuTime now = Zm::now();
  unsigned n = 0;
  while (n < m_cf.maxPending) {
    App_::Subscription *sub = App_::earliest(m_state);
    if (!sub || sub->due > now) break;
    m_state->due[unsigned(sub->group)].delNode(sub);
    sub->inDue = false;
    if (sub->running)
      sub->dirty = true;
    else
      runSubscription_(sub);
    ++n;
  }
  armTimer_();
}

void App::disconnected_(uint64_t generation)
{
  snapshotCancel_(generation);
  App_::Client_ *client = m_state->clients.findVal(generation);
  auto i = m_state->subs.iter();
  while (App_::Subscription *sub = i()) {
    if (sub->generation != generation) continue;
    if (sub->inDue)
      m_state->due[unsigned(sub->group)].delNode(sub);
    sub->inDue = false;
    --sub->client->subs;
    i.del(sub);
  }
  clientRelease_(client);
  armTimer_();
}

void App::clearSubscriptions_()
{
  ZiMultiplex *mx_ = serviceMx_();
  ++m_state->timerGeneration;
  if (mx_) mx_->del(&m_state->timer);
  while (m_state->pending.count_()) {
    auto node = m_state->pending.minimum();
    snapshotCancel_(node->key().p<0>());
  }
  auto i = m_state->subs.iter();
  while (App_::Subscription *sub = i()) {
    if (sub->inDue)
      m_state->due[unsigned(sub->group)].delNode(sub);
    sub->inDue = false;
    --sub->client->subs;
    i.del(sub);
  }
  while (auto node = m_state->clients.minimum()) {
    App_::Client_ *client = node->val();
    App_::indexDel(m_state->clients, node->key(), client);
    delete client;
  }
}

void App::invalid_(ZmRef<Link> link)
{
  sendError_(link.ptr(), 1, "invalid FlatBuffers message");
}

bool App::snapshot_(
    ZmRef<Link> link, uint64_t seqNo, fbs::Group group,
    const Filter_::Filter &filter, App_::Subscription_ *sub_)
{
  uint64_t generation = link->generation();
  App_::PendingKey key{generation, seqNo};
  if (m_state->pending.findVal(key) ||
      m_state->pending.count_() >= m_cf.maxPending)
    return false;
  App_::Client_ *client = sub_ ? sub_->client : client_(link);
  auto pending = new App_::Pending_;
  pending->client = client;
  pending->sub = sub_;
  pending->generation = generation;
  pending->seqNo = seqNo;
  pending->group = group;

  switch (group) {
    case fbs::Group::Heap: {
      auto save = [pending](const auto &captures) {
	App_::copySamples(pending->heaps, captures);
      };
      if (filter.all())
	HeapMgr::capture({}, save);
      else if (filter.mode0() == Filter_::Mode::Exact)
	HeapMgr::capture(
	  [&filter](Heap *heap) {
	    return filter.stringExact(heap->telKey().p<0>());
	  }, save);
      else
	HeapMgr::capture(
	  [&filter](Heap *heap) {
	    return filter.stringPrefix(heap->telKey().p<0>());
	  }, save);
    }
      break;
    case fbs::Group::Hash: {
      auto save = [pending](const auto &captures) {
	App_::copySamples(pending->hashes, captures);
      };
      if (filter.all())
	HashMgr::capture({}, save);
      else if (filter.mode0() == Filter_::Mode::Exact)
	HashMgr::capture(
	  [&filter](Hash *hash) {
	    return filter.stringExact(hash->telKey().p<0>());
	  }, save);
      else
	HashMgr::capture(
	  [&filter](Hash *hash) {
	    return filter.stringPrefix(hash->telKey().p<0>());
	  }, save);
    }
      break;
    case fbs::Group::Thread: {
      auto save = [pending](const auto &captures) {
	App_::copySamples(pending->threads, captures);
      };
      if (filter.all())
	ThreadMgr::capture({}, save);
      else
	ThreadMgr::capture(
	  [&filter](Thread *thread) {
	    return filter.value0() == thread->telKey();
	  }, save);
    }
      break;
    case fbs::Group::Mx: {
      MxMgr::guard([this, pending, &filter]() {
	pending->mxs.size(m_mxIdx.count_());
	pending->cxns.size(m_cxnIdx.count_());
	auto capture = [pending](Ztc::Mx *mx) {
	  auto data = new (pending->mxs.push()) MxTelemetry;
	  mx->telemetry(*data);
	  mx->allCxns({[pending](Ztc::Connection *cxn) {
	    auto data = new (pending->cxns.push()) CxnTelemetry;
	    cxn->telemetry(*data);
	  }});
	};
	if (filter.all()) {
	  auto i = m_mxIdx.citer();
	  while (auto node = i()) capture(node->val());
	} else if (filter.mode0() == Filter_::Mode::Exact) {
	  if (auto mx = m_mxIdx.findVal(filter.text0())) capture(mx);
	} else {
	  ZuCSpan prefix = filter.text0();
	  auto i = m_mxIdx.citer<ZmRBTreeGreaterEqual>(prefix);
	  while (auto node = i()) {
	    if (!ZuCSpan{node->key()}.match(prefix)) break;
	    capture(node->val());
	  }
	}
      });
    }
      break;
    case fbs::Group::Queue: {
      auto collect = [this, pending, &filter]
	  <unsigned TypeMode, unsigned OwnerMode, unsigned IDMode>() {
	MxMgr::guard([this, pending, &filter]() {
	  pending->queues.size(m_mxQueueIdx.count_());
	  App_::captureQueues<TypeMode, OwnerMode, IDMode>(
	    pending->queues, filter, m_mxQueueIdx);
	});
	HubMgr::guard([this, pending, &filter]() {
	  pending->queues.size(
	    pending->queues.length() +
	    m_linkQueueIdx.count_() + m_poolQueueIdx.count_());
	  App_::captureQueues<TypeMode, OwnerMode, IDMode>(
	    pending->queues, filter, m_linkQueueIdx);
	  App_::captureQueues<TypeMode, OwnerMode, IDMode>(
	    pending->queues, filter, m_poolQueueIdx);
	});
      };
      auto dispatchID = [&filter, &collect]
	  <unsigned TypeMode, unsigned OwnerMode>() {
	switch (filter.mode2()) {
	  case Filter_::Mode::All:
	    collect.template operator()<
	      TypeMode, OwnerMode, Filter_::Mode::All>();
	    break;
	  case Filter_::Mode::Exact:
	    collect.template operator()<
	      TypeMode, OwnerMode, Filter_::Mode::Exact>();
	    break;
	  default:
	    collect.template operator()<
	      TypeMode, OwnerMode, Filter_::Mode::Prefix>();
	    break;
	}
      };
      auto dispatchOwner = [&filter, &dispatchID]<unsigned TypeMode>() {
	switch (filter.mode1()) {
	  case Filter_::Mode::All:
	    dispatchID.template operator()<TypeMode, Filter_::Mode::All>();
	    break;
	  case Filter_::Mode::Exact:
	    dispatchID.template operator()<TypeMode, Filter_::Mode::Exact>();
	    break;
	  default:
	    dispatchID.template operator()<TypeMode, Filter_::Mode::Prefix>();
	    break;
	}
      };
      if (filter.mode0() == Filter_::Mode::All)
	dispatchOwner.template operator()<Filter_::Mode::All>();
      else
	dispatchOwner.template operator()<Filter_::Mode::Exact>();
    }
      break;
    case fbs::Group::Hub: {
      HubMgr::guard(
	  [this, pending, &filter]() {
	pending->hubs.size(m_hubIdx.count_());
	pending->links.size(m_linkIdx.count_());
	pending->pools.size(m_poolIdx.count_());
	auto capture = [pending](Ztc::Hub *hub) {
	  auto data = new (pending->hubs.push()) HubTelemetry;
	  hub->telemetry(*data);
	  hub->allLinks({[pending](Ztc::Link *link_) {
	    auto data = new (pending->links.push()) LinkTelemetry;
	    link_->telemetry(*data);
	  }});
	  hub->allPools({[pending](Ztc::Pool *pool) {
	    auto data = new (pending->pools.push()) PoolTelemetry;
	    pool->telemetry(*data);
	  }});
	};
	if (filter.all()) {
	  auto i = m_hubIdx.citer();
	  while (auto node = i()) capture(node->val());
	  return;
	}
	auto captureType = [this, &filter, &capture](LinkType::T type) {
	  switch (filter.mode1()) {
	    case Filter_::Mode::All: {
	      ZuCSpan id;
	      auto key = ZuFwdTuple(type, id);
	      auto i = m_hubIdx.citer<ZmRBTreeGreaterEqual>(key);
	      while (auto node = i()) {
		if (node->key().p<0>() != type) break;
		capture(node->val());
	      }
	    } break;
	    case Filter_::Mode::Exact: {
	      ZuCSpan id = filter.text1();
	      auto key = ZuFwdTuple(type, id);
	      if (auto hub = m_hubIdx.findVal(key)) capture(hub);
	    } break;
	    default: {
	      ZuCSpan prefix = filter.text1();
	      auto key = ZuFwdTuple(type, prefix);
	      auto i = m_hubIdx.citer<ZmRBTreeGreaterEqual>(key);
	      while (auto node = i()) {
		const auto &nodeKey = node->key();
		if (nodeKey.p<0>() != type ||
		    !ZuCSpan{nodeKey.p<1>()}.match(prefix)) break;
		capture(node->val());
	      }
	    } break;
	  }
	};
	if (filter.mode0() == Filter_::Mode::Exact) {
	  captureType(LinkType::T(filter.value0()));
	} else {
	  for (int type = 0; type < LinkType::N; ++type)
	    captureType(LinkType::T(type));
	}
      });
    }
      break;
    case fbs::Group::DB: {
      DBMgr::guard(
	  [this, pending, &filter]() {
	pending->dbs.size(m_dbIdx.count_());
	pending->hosts.size(m_dbHostIdx.count_());
	pending->tables.size(m_dbTableIdx.count_());
	auto capture = [pending](DB *db) {
	  auto data = new (pending->dbs.push()) DBTelemetry;
	  db->telemetry(*data);
	  db->allDBHosts({[pending](DBHost *host) {
	    auto data = new (pending->hosts.push()) DBHostTelemetry;
	    host->telemetry(*data);
	  }});
	  db->allDBTables({[pending](DBTable *table) {
	    auto data = new (pending->tables.push()) DBTableTelemetry;
	    table->telemetry(*data);
	  }});
	};
	if (filter.all()) {
	  auto i = m_dbIdx.citer();
	  while (auto node = i()) capture(node->val());
	} else if (filter.mode0() == Filter_::Mode::Exact) {
	  if (auto db = m_dbIdx.findVal(filter.text0())) capture(db);
	} else {
	  ZuCSpan prefix = filter.text0();
	  auto i = m_dbIdx.citer<ZmRBTreeGreaterEqual>(prefix);
	  while (auto node = i()) {
	    if (!ZuCSpan{node->key()}.match(prefix)) break;
	    capture(node->val());
	  }
	}
      });
    }
      break;
    case fbs::Group::App:
      if (!filter.all() &&
	  (filter.mode0() == Filter_::Mode::Exact ?
	    !filter.stringExact(m_cf.id) :
	    !filter.stringPrefix(m_cf.id))) break;
      pending->app.id = m_cf.id;
      pending->app.version = m_cf.version;
      pending->app.role = m_cf.role;
      pending->app.startTime = m_startTime;
      pending->app.state = ZmEngineState::Running;
      pending->app.degraded = m_state->degraded;
      pending->app.rag = RAG::T(m_rag.load_());
      pending->appCaptured = true;
      break;
    case fbs::Group::Alert:
    default:
      delete pending;
      clientRelease_(client);
      return false;
  }

  App_::indexAdd(m_state->pending, ZuMv(key), pending);
  ++client->pending;
  if (sub_) {
    sub_->snapshot = pending;
    sub_->running = true;
  }
  serviceMx_()->run([this, generation, seqNo]() {
    snapshotBatch_(generation, seqNo);
  }, m_cf.workerThread);
  return true;
}

void App::snapshotBatch_(uint64_t generation, uint64_t seqNo)
{
  App_::PendingKey key{generation, seqNo};
  App_::Pending_ *pending = m_state->pending.findVal(key);
  if (!pending) return;
  Link *link = pending->client->link.ptr();
  unsigned budget = m_cf.maxPending;
  bool done = false;

  switch (pending->group) {
    case fbs::Group::Heap:
      done = App_::sendBatch(link, seqNo,
	fbs::TelemetryBody::HeapTelemetry, pending->heaps,
	pending->offset, pending->count, budget);
      break;
    case fbs::Group::Hash:
      done = App_::sendBatch(link, seqNo,
	fbs::TelemetryBody::HashTelemetry, pending->hashes,
	pending->offset, pending->count, budget);
      break;
    case fbs::Group::Thread:
      done = App_::sendBatch(link, seqNo,
	fbs::TelemetryBody::ThreadTelemetry, pending->threads,
	pending->offset, pending->count, budget);
      break;
    case fbs::Group::Mx:
      while (budget && pending->stage < 2) {
	bool stageDone = pending->stage ?
	  App_::sendBatch(link, seqNo,
	    fbs::TelemetryBody::CxnTelemetry, pending->cxns,
	    pending->offset, pending->count, budget) :
	  App_::sendBatch(link, seqNo,
	    fbs::TelemetryBody::MxTelemetry, pending->mxs,
	    pending->offset, pending->count, budget);
	if (!stageDone) break;
	++pending->stage;
	pending->offset = 0;
      }
      done = pending->stage == 2;
      break;
    case fbs::Group::Queue:
      done = App_::sendBatch(link, seqNo,
	fbs::TelemetryBody::QueueTelemetry, pending->queues,
	pending->offset, pending->count, budget);
      break;
    case fbs::Group::Hub:
      while (budget && pending->stage < 3) {
	bool stageDone;
	switch (pending->stage) {
	  case 0:
	    stageDone = App_::sendBatch(link, seqNo,
	      fbs::TelemetryBody::HubTelemetry, pending->hubs,
	      pending->offset, pending->count, budget);
	    break;
	  case 1:
	    stageDone = App_::sendBatch(link, seqNo,
	      fbs::TelemetryBody::LinkTelemetry, pending->links,
	      pending->offset, pending->count, budget);
	    break;
	  default:
	    stageDone = App_::sendBatch(link, seqNo,
	      fbs::TelemetryBody::PoolTelemetry, pending->pools,
	      pending->offset, pending->count, budget);
	    break;
	}
	if (!stageDone) break;
	++pending->stage;
	pending->offset = 0;
      }
      done = pending->stage == 3;
      break;
    case fbs::Group::DB:
      while (budget && pending->stage < 3) {
	bool stageDone;
	switch (pending->stage) {
	  case 0:
	    stageDone = App_::sendBatch(link, seqNo,
	      fbs::TelemetryBody::DBTelemetry, pending->dbs,
	      pending->offset, pending->count, budget);
	    break;
	  case 1:
	    stageDone = App_::sendBatch(link, seqNo,
	      fbs::TelemetryBody::DBHostTelemetry, pending->hosts,
	      pending->offset, pending->count, budget);
	    break;
	  default:
	    stageDone = App_::sendBatch(link, seqNo,
	      fbs::TelemetryBody::DBTableTelemetry, pending->tables,
	      pending->offset, pending->count, budget);
	    break;
	}
	if (!stageDone) break;
	++pending->stage;
	pending->offset = 0;
      }
      done = pending->stage == 3;
      break;
    case fbs::Group::App:
      if (pending->appCaptured && !pending->offset && budget) {
	App_::sendTelemetry(link, seqNo,
	  fbs::TelemetryBody::AppTelemetry, pending->app);
	pending->offset = 1;
	++pending->count;
	--budget;
      }
      done = !pending->appCaptured || pending->offset;
      break;
    case fbs::Group::Alert:
    default:
      done = true;
      break;
  }

  if (done) {
    snapshotDone_(pending);
    return;
  }
  serviceMx_()->run([this, generation, seqNo]() {
    snapshotBatch_(generation, seqNo);
  }, m_cf.workerThread);
}

void App::snapshotDone_(App_::Pending_ *pending)
{
  App_::Client_ *client = pending->client;
  auto sub = static_cast<App_::Subscription *>(pending->sub);
  App_::indexDel(m_state->pending,
    App_::PendingKey{pending->generation, pending->seqNo}, pending);
  if (sub) {
    sub->snapshot = nullptr;
    sub->running = false;
  }
  sendComplete_(client->link.ptr(), pending->seqNo, pending->count);
  --client->pending;
  delete pending;

  if (sub && sub->dirty) {
    sub->dirty = false;
    if (sub->inDue) {
      m_state->due[unsigned(sub->group)].delNode(sub);
      sub->inDue = false;
    }
    runSubscription_(sub);
  }
  armTimer_();
  clientRelease_(client);
}

void App::snapshotCancel_(uint64_t generation)
{
  for (;;) {
    App_::Pending_ *pending = nullptr;
    {
      auto i = m_state->pending.citer();
      while (auto node = i()) {
	if (node->key().p<0>() != generation) continue;
	pending = node->val();
	break;
      }
    }
    if (!pending) return;
    App_::Client_ *client = pending->client;
    if (pending->sub) {
      pending->sub->snapshot = nullptr;
      pending->sub->running = false;
    }
    App_::indexDel(m_state->pending,
      App_::PendingKey{pending->generation, pending->seqNo}, pending);
    --client->pending;
    delete pending;
  }
}

void App::sendAck_(
    Link *link, uint64_t seqNo, fbs::AckStatus status, uint32_t interval)
{
  Zfb::IOBuilder fbb{
    frameBuf(ZmRef<ZiIOBuf>{new App_::CtrlFrame{link}})};
  auto ack = fbs::CreateAck(fbb, seqNo, status, interval);
  fbb.Finish(fbs::CreateMsg(fbb, fbs::Body::Ack, ack.Union()));
  if (auto buf = saveHdr(fbb, link)) link->send(ZuMv(buf));
}

void App::sendError_(Link *link, int32_t code, ZuCSpan message)
{
  Zfb::IOBuilder fbb{
    frameBuf(ZmRef<ZiIOBuf>{new App_::CtrlFrame{link}})};
  auto text = fbb.CreateString(message.data(), message.length());
  auto error = fbs::CreateError(fbb, 0, false, code, text);
  fbb.Finish(fbs::CreateMsg(fbb, fbs::Body::Error, error.Union()));
  if (auto buf = saveHdr(fbb, link)) link->send(ZuMv(buf));
}

void App::sendComplete_(Link *link, uint64_t seqNo, uint32_t count)
{
  Zfb::IOBuilder fbb{
    frameBuf(ZmRef<ZiIOBuf>{new App_::CtrlFrame{link}})};
  auto complete = fbs::CreateSnapshotComplete(fbb, seqNo, count);
  fbb.Finish(
    fbs::CreateMsg(fbb, fbs::Body::SnapshotComplete, complete.Union()));
  if (auto buf = saveHdr(fbb, link)) link->send(ZuMv(buf));
}

} // Ztc
