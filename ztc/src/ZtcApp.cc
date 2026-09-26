//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <string.h>
#include <stdlib.h>

#include <zlib/ZuDateTime.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtPlatform.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiDir.hh>
#include <zlib/ZiPIDFile.hh>

#include <zlib/ZtcApp.hh>
#include <zlib/ZtcAlert.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcFilter.hh>
#include <zlib/ZtcMsg.hh>
#include <zlib/ZtcRing.hh>

namespace Ztc {

namespace App_ {

enum { DefltFrameSize = 1024 }; // common telemetry/request frame
using ReqFrame =
  ZiIOBufAlloc<DefltFrameSize, AppCf::DefltMaxFrame, "Ztc.App.ReqFrame">;
using MsgFrame =
  ZiIOBufAlloc<DefltFrameSize, AppCf::DefltMaxFrame, "Ztc.App.MsgFrame">;
using AlertFrame =
  ZiIOBufAlloc<DefltFrameSize, AppCf::DefltMaxFrame, "Ztc.App.AlertFrame">;

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

using SubKey = ZuTuple<uint8_t, ZuCSpan>;
using DueKey = ZuTuple<ZuTime, uint64_t>;

struct PendingAlert {
  explicit operator bool() const { return bool(frame); }

  ZmRef<ZiIOBuf>	frame;
  uint64_t		seqNo = 0;
  uint32_t		date = 0;
};

using AlertFrames = ZmQueue<PendingAlert,
  ZmQueueHeapID<"Ztc.App.AlertPending">>;

class Subscription_ {
public:
  Pending_		*snapshot = nullptr;
  Filter_::Filter	filter;
  ZuTime		due;
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
  ZiFile		replayData;
  ZiFile		replayIndex;
  bool			running = false;
  bool			dirty = false;
  bool			replaying = false;
  bool			oneShot = false;
  bool			replayFailed = false;
};

class PendingData {
public:
  Subscription_	*sub = nullptr;
  uint64_t		seqNo = 0;
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

template <typename Heap = ZuVoid>
class Pending__ : public Heap, public PendingData { };
ZuDerive(PendingHeap, (ZmHeap<"Ztc.App.Pending", Pending__<>>));
ZuDerive(Pending_, (Pending__<PendingHeap>));

using PendingKey = uint64_t;
using PendingIdx = ZmRBTreeKV<PendingKey, Pending_ *,
  ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmNoLock,
      ZmRBTreeHeapID<"Ztc.App.PendingIdx">>>>;

static SubKey subKey(const Subscription_ &sub)
{
  return {uint8_t(sub.group), sub.filter.key()};
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
// Alert frames are pooled I/O buffers shared with the writer; they cannot own a queue node.
using AlertTail = ZmQueue<ZmRef<ZiIOBuf>,
  ZmQueueHeapID<"Ztc.App.AlertTail">>;

class AlertStore {
public:
  using LoadResult = Alert_::LoadResult::T;

  void init(
      Zi::Path prefix, ZuID id, uint32_t maxFrame, unsigned retention) {
    m_prefix = ZuMv(prefix);
    m_id = ZuMv(id);
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
    ZiLOG(Error, "Ztc.App", ([date, message = ZeString{message}](auto &s) {
      s << "alert partition " << date << ": " << message;
    }));
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
    if (!alert || !telemetry->id() ||
	ZuCSpan{telemetry->id()->c_str(), telemetry->id()->size()} != m_id ||
	telemetry->seqNo() || alert->date() != date ||
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
  ZuID		m_id;
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
    app->scheduler_()->run([app, event = ZuMv(event)]() mutable {
      app->alert_(ZuMv(event));
    }, app->config().workerThread);
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

template <typename Heap = ZuVoid>
class Ingress__ : public Heap, public IngressData {
public:
  using IngressData::IngressData;
};
ZuDerive(IngressHeap, (ZmHeap<"Ztc.App.Ingress", Ingress__<>>));
ZuDerive(Ingress, (Ingress__<IngressHeap>));

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
ZuDerive(AlertSinkHeap, (ZmHeap<"Ztc.App.AlertSink", AlertSink__<>>));
ZuDerive(AlertSink, (AlertSink__<AlertSinkHeap>));

struct StateData {
  StateData() {
    unsigned n = unsigned(fbs::Group::Alert);
    due.size(n);
    for (unsigned i = 0; i < n; ++i)
      new (due.push()) SubDueIdx;
  }

  Zi::Name		telName;
  Zi::Path		pidName;
  ZuUnion<void, ZiPIDFile> pidFile;
  ZmThread		reqThread;
  Ring			reqRing;
  ZmAtomic<unsigned>	reqStop = 0;
  Ring			telRing;
  bool			appPending = true;

  PendingIdx		pending;
  SubIdx		subs;
  SubDueSets		due;
  AlertTail		tail;
  AlertStore		store;
  ZmRef<Ingress>	ingress;
  ZmScheduler::Timer	timer;
  uint64_t		timerGeneration = 0;
  uint64_t		nextOrder = 0;
  uint64_t		minReqSeqNo = 0;
  uint64_t		alertSeqNo = 0;
  uint32_t		alertDate = 0;
  bool			degraded = false;
};

template <typename Heap = ZuVoid>
class State_ : public Heap, public StateData { };
ZuDerive(StateHeap, (ZmHeap<"Ztc.App.State", State_<>>));
ZuDerive(State, (State_<StateHeap>));

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

namespace App_ {

template <typename T>
ZmRef<ZiIOBuf> telemetryFrame(
  ZuCSpan id, uint64_t seqNo, fbs::TelemetryBody type, const T &data)
{
  Zfb::IOBuilder fbb{
    frameBuf(ZmRef<ZiIOBuf>{new MsgFrame})};
  auto id_ = fbb.CreateString(id.data(), id.length());
  auto value =
    ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data);
  auto telemetry =
    saveTelemetry(fbb, id_, seqNo, type, value.Union());
  fbb.Finish(
    saveMsg(fbb, fbs::Body::Telemetry, telemetry.Union()));
  return saveHdr(fbb);
}

ZmRef<ZiIOBuf> shutdownFrame(ZuCSpan id)
{
  Zfb::IOBuilder fbb{frameBuf(ZmRef<ZiIOBuf>{new MsgFrame})};
  auto id_ = fbb.CreateString(id.data(), id.length());
  auto value = fbs::CreateShutdown(fbb);
  auto telemetry = saveTelemetry(fbb, id_, 0,
    fbs::TelemetryBody::Shutdown, value.Union());
  fbb.Finish(saveMsg(fbb, fbs::Body::Telemetry, telemetry.Union()));
  return saveHdr(fbb);
}

ZmRef<ZiIOBuf> alertFrame(uint64_t seqNo, const ZiIOBuf *canonical)
{
  ZmRef<ZiIOBuf> frame = new MsgFrame;
  frame->append(canonical->data(), canonical->length);
  if (ZuUnlikely(frame->length != canonical->length)) return {};
  auto root = flatbuffers::GetMutableRoot<fbs::Msg>(
    frame->data() + sizeof(Hdr));
  auto telemetry = root->mutable_body_as_Telemetry();
  if (ZuUnlikely(!telemetry || !telemetry->mutate_seqNo(seqNo)))
    return {};
  return frame;
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
  if (m_initialized || !cf.id || cf.maxFrame < sizeof(Hdr) + 8 ||
      cf.maxFrame > AppCf::DefltMaxFrame ||
      !cf.reqTimeout || cf.reqTimeout > 3600 || cf.reqSize > (1U<<30) ||
      uint64_t(cf.reqSize) < uint64_t(cf.maxFrame) + Zm::CacheLineSize ||
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
      !cf.timerThread || !cf.workerThread ||
      !cf.scheduler.nThreads || cf.scheduler.nThreads > 1024 ||
      (cf.scheduler.stackSize &&
	(cf.scheduler.stackSize < 16384 || cf.scheduler.stackSize > (2U<<20))) ||
      (cf.scheduler.queueSize && cf.scheduler.queueSize < 8192) ||
      cf.scheduler.timeout > 3600 ||
      cf.timerThread > cf.scheduler.nThreads ||
      cf.workerThread > cf.scheduler.nThreads ||
      !cf.timerRole || !cf.workerRole ||
      cf.timerRole == cf.workerRole ||
      cf.timerThread == cf.workerThread)
    return false;
  if (!App_::claim(this)) return false;

  bool schedulerConstructed = false;
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
    m_state->store.init(
      cf.alertPrefix, cf.id, cf.maxFrame, cf.alertRetention);
    ZmSchedParams params;
    [
      id = cf.id, scheduler = cf.scheduler,
      timerRole = cf.timerRole,
      workerRole = cf.workerRole,
      timerThread = cf.timerThread,
      workerThread = cf.workerThread
    ](auto &sched) {
      sched.id(id).nThreads(scheduler.nThreads).priority(scheduler.priority)
	.partition(scheduler.partition).ll(scheduler.ll).spin(scheduler.spin)
	.timeout(scheduler.timeout)
	.thread(timerThread, [timerRole](auto &thread) {
	  thread.name(timerRole);
	  thread.isolated(true);
	})
	.thread(workerThread, [workerRole](auto &thread) {
	  thread.name(workerRole);
	  thread.isolated(true);
	});
      if (scheduler.stackSize) sched.stackSize(scheduler.stackSize);
      if (scheduler.quantum > 0) sched.quantum(scheduler.quantum);
      if (scheduler.queueSize) sched.queueSize(scheduler.queueSize);
    }(params);
    new (m_scheduler.new_<ZmScheduler>()) ZmScheduler{ZuMv(params)};
    schedulerConstructed = true;
    App_::warmSamples();
    warmIndices_();
    watch_();
    m_startTime = Zm::now().sec();
    m_initialized = true;
    return true;
  } catch (...) {
    unwatch_();
    clearIndices_();
    if (schedulerConstructed)
      m_scheduler = {};
    else
      m_scheduler.new_<void, true>();
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
    if (m_startPending) {
      guard.unlock();
      if (fn) fn(false);
      return;
    }
    m_startPending = true;
    m_startFn = ZuMv(fn);
  }

  ZmScheduler *scheduler = scheduler_();
  if (!scheduler || !scheduler->start()) {
    startDone_(false);
    return;
  }
  ZuTime now = Zm::now();
  if (!m_state->store.start(now)) m_state->degraded = true;
  m_state->alertDate = 0;
  m_state->alertSeqNo = 0;
  m_state->minReqSeqNo = 0;
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

  if (!m_state->pidName) {
    const char *telName = ::getenv("ZTC_RING");
    auto pidDir = Zt::getpath("ZTC_DIR");
    m_state->telName = telName ? telName : "ztc";
    if (pidDir)
      m_state->pidName << pidDir;
    else
      m_state->pidName << "ztc";
    m_state->pidName << '/' << m_cf.id << ".pid";
    m_state->telRing.init(ZiRingParams{m_state->telName, 0});
  }

  auto failed = [this, scheduler]() {
    m_running = false;
    if (m_state->pidFile.is<ZiPIDFile>())
      m_state->pidFile.new_<void>();
    if (!m_state->reqRing.closed()) {
      if ((m_state->reqRing.flags() & Ring::Read) &&
	  m_state->reqRing.rdrID() >= 0)
	m_state->reqRing.detach();
      m_state->reqRing.close();
    }
    m_state->store.close();
    scheduler->stop();
    startDone_(false);
  };

  m_state->reqRing.init(
    ZiRingParams{m_cf.id, m_cf.reqSize}.
      timeout(m_cf.reqTimeout).ll(m_cf.reqLL));
  if (m_state->reqRing.open(Ring::Write) != Zu::OK ||
      m_state->reqRing.reset() != Zu::OK) {
    failed();
    return;
  }
  m_state->reqRing.close();
  if (m_state->reqRing.open(Ring::Read) != Zu::OK ||
      m_state->reqRing.attach() != Zu::OK) {
    failed();
    return;
  }

  auto pidFile = new (m_state->pidFile.new_<ZiPIDFile>()) ZiPIDFile;
  if (pidFile->init(ZiFile::tmpDir(), m_state->pidName) != ZiPIDFile::OK) {
    failed();
    return;
  }

  m_state->reqStop = 0;
  m_running = true;
  if (m_state->reqThread.run(
      [this]() { reqRun_(); }, ZmThreadParams{}.name("ztcReq")) < 0) {
    failed();
    return;
  }
  m_state->ingress->open(this);
  scheduler->run([this]() {
    if (!m_running.load_()) return;
    m_state->appPending = true;
    if (publishApp_(0)) m_state->appPending = false;
  }, m_cf.workerThread);
  startDone_(true);
}

bool App::start()
{
  ZmScheduler *scheduler = scheduler_();
  if (scheduler && scheduler->running()) {
    auto tid = Zm::getTID();
    for (unsigned sid = 1; sid <= m_cf.scheduler.nThreads; ++sid)
      if (scheduler->invoked_(tid, sid)) return false;
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
  ZmScheduler *scheduler = scheduler_();
  bool schedulerRunning = scheduler && scheduler->running();
  if (schedulerRunning) {
    auto tid = Zm::getTID();
    for (unsigned sid = 1; sid <= m_cf.scheduler.nThreads; ++sid)
      if (scheduler->invoked_(tid, sid)) return false;
  }
  startDone_(false);
  if (m_running) publishRaw_(App_::shutdownFrame(m_cf.id),
    App_::Delivery::Telemetry);
  m_running = false;
  if (m_state->pidFile.is<ZiPIDFile>())
    m_state->pidFile.new_<void>();
  m_state->reqStop = 1;
  {
    Ring wake{ZiRingParams{m_cf.id, 0}};
    if (wake.open(Ring::Write) == Zu::OK) {
      if (void *ptr = wake.tryPush(sizeof(Hdr))) {
	static_cast<Hdr *>(ptr)->length = 0;
	wake.push2(ptr, sizeof(Hdr));
      }
    }
  }
  m_state->reqThread.join();
  if (!m_state->reqRing.closed()) {
    if (m_state->reqRing.rdrID() >= 0) m_state->reqRing.detach();
    m_state->reqRing.close();
  }
  m_state->ingress->close(this);
  if (schedulerRunning) {
    ZmBlock<>{}([this, scheduler](auto wake) mutable {
      scheduler->run([this, wake = ZuMv(wake)]() mutable {
	clearSubscriptions_();
	m_state->telRing.close();
	m_state->appPending = true;
	wake();
      }, m_cf.workerThread);
    });
    ZmBlock<>{}([this, scheduler](auto wake) mutable {
      scheduler->run([wake = ZuMv(wake)]() mutable { wake(); },
	m_cf.timerThread);
    });
    ZmBlock<>{}([this, scheduler](auto wake) mutable {
      scheduler->run([wake = ZuMv(wake)]() mutable { wake(); },
	m_cf.workerThread);
    });
    ZmAssert(m_state->ingress->drained());
  }
  if (schedulerRunning) scheduler->stop();
  m_state->store.close();
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
  m_scheduler = {};
  delete m_state;
  m_state = nullptr;
  m_initialized = false;
  App_::release(this);
}

void App::rag(RagFn fn) const
{
  if (!fn) return;
  if (!m_running.load_()) {
    fn(RAG::T(m_rag.load_()));
    return;
  }
  const App *app = this;
  scheduler_()->run([app, fn = ZuMv(fn)]() mutable {
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
  scheduler_()->run([app, rag]() { app->rag_(rag); }, m_cf.workerThread);
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
    auto id = fbb.CreateString(m_cf.id.data(), m_cf.id.length());
    auto message =
      fbb.CreateString(event->message().data(), event->message().length());
    Zfb::Time time{event->time.sec(), event->time.nsec()};
    auto value = fbs::CreateAlertTelemetry(
      fbb, message, &time, alertSeqNo, event->tid, date, event->severity);
    auto telemetry = saveTelemetry(
      fbb, id, 0, fbs::TelemetryBody::AlertTelemetry, value.Union());
    fbb.Finish(
      saveMsg(fbb, fbs::Body::Telemetry, telemetry.Union()));
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
      publish_(App_::alertFrame(sub->seqNo, canonical.ptr()));
    }
  } catch (...) {
    m_state->degraded = true;
  }
}

void App::warmIndices_()
{
  delete new App_::Pending_;
  App_::indexWarm(m_state->pending, App_::PendingKey{});
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
  MxMgr::all({[](Ztc::Mx *mx_) { mx_->unwatch(); }});
  MxMgr::unwatch();
  m_watching = false;
}

void App::mxAdded_(Ztc::Mx *mx_)
{
  App_::indexAdd(m_mxIdx, ZuID{mx_->telKey()}, mx_);
  mx_->watch(
    {[this](Ztc::Connection *cxn) { cxnAdded_(cxn); }},
    {[this](Ztc::Connection *cxn) { cxnDeleted_(cxn); }});
  mx_->allCxns(
    {[this](Ztc::Connection *cxn) { cxnAdded_(cxn); }});
  mx_->allQueues(
    {[this](Ztc::Queue *queue) { mxQueueAdded_(queue); }});
}

void App::mxDeleted_(Ztc::Mx *mx_)
{
  mx_->unwatch();
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

void App::reqRun_()
{
  try {
    while (!m_state->reqStop.load_()) {
      void *ptr = m_cf.reqLL ?
	m_state->reqRing.tryShift() : m_state->reqRing.shift();
      if (!ptr) {
	if (m_state->reqStop.load_()) return;
	int status = m_state->reqRing.readStatus();
	if (status >= 0 || status == Zu::NotReady) continue;
	ZiLOG(Fatal, "Ztc.App", ([status](auto &s) {
	  s << "request ring failed: " << status;
	}));
	Zm::exit(1);
      }

      unsigned size = ringSize(ptr);
      ZmRef<ZiIOBuf> buf = new App_::ReqFrame;
      if (size <= m_cf.maxFrame && buf->alloc(size)) {
	memcpy(buf->data(), ptr, size);
	buf->length = size;
      } else
	buf = {};
      m_state->reqRing.shift2(size);

      ZmScheduler *scheduler = scheduler_();
      if (!buf || !msg(buf->ptr<Hdr>())) {
	scheduler->run([this]() {
	  if (m_running.load_())
	    sendError_(ZuCmp<uint64_t>::null(), 1,
	      "invalid FlatBuffers message");
	}, m_cf.workerThread);
	continue;
      }
      scheduler->run([this, buf = ZuMv(buf)]() mutable {
	if (m_running.load_()) request_(ZuMv(buf));
      }, m_cf.workerThread);
    }
  } catch (...) {
    if (m_state->reqStop.load_()) return;
    ZiLOG(Fatal, "Ztc.App", "request thread terminated unexpectedly");
    Zm::exit(1);
  }
}

bool App::publishRaw_(ZmRef<ZiIOBuf> buf, App_::Delivery::T delivery)
{
  if (!buf) return false;
  if (m_state->telRing.closed() &&
      m_state->telRing.open(Ring::Write) != Zu::OK)
    return false;
  bool evicted = false;
  for (;;) {
    if (void *ptr = m_state->telRing.push(buf->length)) {
      memcpy(ptr, buf->data(), buf->length);
      m_state->telRing.push2(ptr, buf->length);
      return true;
    }
    int status = m_state->telRing.writeStatus();
    if (status >= 0) {
      if (delivery == App_::Delivery::Telemetry && evicted) return false;
      m_state->telRing.kill();
      evicted = true;
      if (!m_running.load_()) return false;
      continue;
    }
    switch (status) {
      case Zu::NotReady:
      case Zu::EndOfFile:
      case Zu::IOError:
      default:
	return false;
    }
  }
}

bool App::publish_(ZmRef<ZiIOBuf> buf)
{
  if (m_state->appPending && publishApp_(0))
    m_state->appPending = false;
  return publishRaw_(ZuMv(buf), App_::Delivery::Telemetry);
}

void App::appTelemetry_(AppTelemetry &data)
{
  data.version = m_cf.version;
  data.role = m_cf.role;
  data.startTime = m_startTime;
  data.ztcver = Z_VERSION;
  data.state = ZmEngineState::Running;
  data.degraded = m_state->degraded;
  data.rag = RAG::T(m_rag.load_());
}

bool App::publishApp_(uint64_t seqNo)
{
  AppTelemetry data;
  appTelemetry_(data);
  return publishRaw_(App_::telemetryFrame(
    m_cf.id, seqNo, fbs::TelemetryBody::AppTelemetry, data),
    App_::Delivery::Telemetry);
}

void App::request_(ZmRef<ZiIOBuf> buf)
{
  auto root = msg_(buf->ptr<Hdr>());
  if (ZuUnlikely(root->body_type() != fbs::Body::Request)) {
    sendError_(ZuCmp<uint64_t>::null(), 1, "expected Request message");
    return;
  }
  auto request = root->body_as_Request();
  if (ZuUnlikely(!request)) return;
  uint64_t seqNo = request->seqNo();
  ZuCSpan id = Zfb::Load::str(request->id());
  if (id && id != m_cf.id) return;
  if (seqNo == ZuCmp<uint64_t>::null()) {
    clearSubscriptions_();
    m_state->minReqSeqNo = 0;
    return;
  }
  if (request->subscribe()) {
    if (m_state->minReqSeqNo == ZuCmp<uint64_t>::null() ||
	seqNo < m_state->minReqSeqNo)
      return;
    m_state->minReqSeqNo = seqNo + 1;
  }
  ZuCSpan filter = Zfb::Load::str(request->filter());
  auto group = request->group();
  if (ZuUnlikely(
	int(group) < int(fbs::Group::Heap) ||
	int(group) > int(fbs::Group::Alert))) {
    sendAck_(seqNo, fbs::AckStatus::Invalid);
    return;
  }
  Filter_::Filter compiled;
  if (ZuUnlikely(!compiled.compile(group, filter, m_cf.maxFilter))) {
    sendAck_(seqNo, fbs::AckStatus::Invalid);
    return;
  }
  uint32_t alertDate = request->alertDate();
  uint64_t alertSeqNo = request->alertSeqNo();
  if (group != fbs::Group::Alert) {
    if (alertDate || alertSeqNo) {
      sendAck_(seqNo, fbs::AckStatus::Invalid);
      return;
    }
  } else if ((alertDate && !App_::AlertStore::validDate(alertDate)) ||
      (!alertDate && alertSeqNo)) {
    sendAck_(seqNo, fbs::AckStatus::Invalid);
    return;
  } else if (alertDate && m_state->alertDate &&
      (alertDate < m_state->store.earliest(m_state->alertDate) ||
       alertDate > m_state->alertDate ||
       (alertDate == m_state->alertDate && m_state->alertSeqNo &&
	alertSeqNo >= m_state->alertSeqNo))) {
    sendAck_(seqNo, fbs::AckStatus::Invalid);
    return;
  }
  if (!request->subscribe()) {
    unsubscribe_(seqNo, group, compiled);
    return;
  }
  uint32_t interval = request->interval();
  if (!interval) {
    if (group == fbs::Group::Alert) {
      subscribe_(seqNo, group, ZuMv(compiled), 0,
	alertDate, alertSeqNo);
	return;
      }
    if (!snapshot_(seqNo, group, compiled)) {
      sendAck_(seqNo, fbs::AckStatus::Failed);
      return;
    }
    sendAck_(seqNo, fbs::AckStatus::OK);
    return;
  }
  if (ZuUnlikely(interval > m_cf.maxInterval)) {
    sendAck_(seqNo, fbs::AckStatus::Invalid);
    return;
  }
  if (interval < m_cf.minInterval) interval = m_cf.minInterval;
  subscribe_(seqNo, group, ZuMv(compiled), interval,
    alertDate, alertSeqNo);
}

void App::subscribe_(
    uint64_t seqNo, fbs::Group group,
    Filter_::Filter filter, uint32_t interval,
    uint32_t alertDate, uint64_t alertSeqNo)
{
  App_::SubKey key{uint8_t(group), filter.key()};
  App_::Subscription *sub = m_state->subs.findPtr(key);
  App_::Pending_ *sameSeq = m_state->pending.findVal(seqNo);
  if (group != fbs::Group::Alert &&
      (((!sub || !sub->running) &&
	m_state->pending.count_() >= m_cf.maxPending) ||
       (sameSeq && (!sub || sameSeq != sub->snapshot)))) {
    sendAck_(seqNo, fbs::AckStatus::Failed);
    return;
  }
  if (sub) {
    if (*sub->due) {
      m_state->due[unsigned(sub->group)].delNode(sub);
      sub->due.null();
    }
    sub->seqNo = seqNo;
    sub->interval = interval;
    sub->dirty = true;
  } else {
    if (ZuUnlikely(m_state->subs.count_() >= m_cf.maxSubs)) {
      sendAck_(seqNo, fbs::AckStatus::Failed);
      return;
    }
    sub = new App_::Subscription;
    sub->filter = ZuMv(filter);
    sub->seqNo = seqNo;
    sub->order = ++m_state->nextOrder;
    sub->interval = interval;
    sub->group = group;
    if (group == fbs::Group::Alert)
      sub->pending.init(ZmQueueParams{}.initial(m_cf.alertReplay));
    m_state->subs.addNode(sub);
  }
  sendAck_(seqNo, fbs::AckStatus::OK, interval);
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
    uint64_t seqNo, fbs::Group group,
    const Filter_::Filter &filter)
{
  App_::SubKey key{uint8_t(group), filter.key()};
  if (App_::Subscription *sub = m_state->subs.findPtr(key)) {
    if (*sub->due) {
      m_state->due[unsigned(sub->group)].delNode(sub);
      sub->due.null();
    }
    if (auto pending = sub->snapshot) {
      App_::indexDel(m_state->pending,
	pending->seqNo, pending);
      pending->sub = nullptr;
      sub->snapshot = nullptr;
      sub->running = false;
      delete pending;
    }
    m_state->subs.delNode(sub);
    armTimer_();
  }
  sendAck_(seqNo, fbs::AckStatus::OK);
}

void App::runSubscription_(App_::Subscription_ *sub_)
{
  auto sub = static_cast<App_::Subscription *>(sub_);
  if (sub->running) {
    sub->dirty = true;
    return;
  }
  sub->dirty = false;
  if (!snapshot_(sub->seqNo, sub->group, sub->filter, sub)) {
    sub->dirty = true;
    sub->due = Zm::now() + App_::interval(sub->interval);
    m_state->due[unsigned(sub->group)].addNode(sub);
    return;
  }
  if (sub->interval) {
    sub->due = Zm::now() + App_::interval(sub->interval);
    m_state->due[unsigned(sub->group)].addNode(sub);
  }
}

void App::replay_(App_::Subscription_ *sub_)
{
  auto sub = static_cast<App_::Subscription *>(sub_);
  sub->pending.clean();
  sub->replayData.close();
  sub->replayIndex.close();
  sub->openDate = 0;
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
    failReplay_(sub, "alert replay handoff overflow");
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
      publish_(App_::alertFrame(sub->seqNo, frame.ptr()));
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

  auto scheduler = scheduler_();
  if (!scheduler || !scheduler->running()) return;
  scheduler->run([this, order]() { replayBatch_(order); }, m_cf.workerThread);
}

void App::finishReplay_(App_::Subscription_ *sub_)
{
  auto sub = static_cast<App_::Subscription *>(sub_);
  sub->replaying = false;
  sub->replayData.close();
  sub->replayIndex.close();
  sub->openDate = 0;
  sendEOS_(sub->seqNo);
  if (sub->oneShot) {
    m_state->subs.delNode(sub);
    return;
  }
  uint32_t lastDate = sub->highDate;
  uint64_t lastSeqNo = sub->highSeqNo;
  while (auto pending = sub->pending.shift()) {
    if (pending.date < lastDate ||
	(pending.date == lastDate && pending.seqNo <= lastSeqNo))
      continue;
    publish_(App_::alertFrame(sub->seqNo, pending.frame.ptr()));
    lastDate = pending.date;
    lastSeqNo = pending.seqNo;
  }
}

void App::failReplay_(
    App_::Subscription_ *sub_, ZuCSpan message)
{
  auto sub = static_cast<App_::Subscription *>(sub_);
  sub->replaying = false;
  sub->replayData.close();
  sub->replayIndex.close();
  sub->openDate = 0;
  sendError_(sub->seqNo, 2, message);
  m_state->subs.delNode(sub);
}

void App::armTimer_()
{
  ZmScheduler *scheduler = scheduler_();
  if (!scheduler || !scheduler->running()) return;
  uint64_t generation = ++m_state->timerGeneration;
  scheduler->del(&m_state->timer);
  App_::Subscription *sub = App_::earliest(m_state);
  if (!sub) return;
  ZuTime deadline = sub->due;
  scheduler->add(&m_state->timer, deadline, ZmScheduler::Update,
    [this, generation, deadline](auto &&arm) {
      return arm([this, generation, deadline]() {
	if (!m_running.load_()) return;
	auto scheduler = scheduler_();
	if (!scheduler) return;
	scheduler->run([this, generation, deadline]() {
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
    sub->due.null();
    if (sub->running)
      sub->dirty = true;
    else
      runSubscription_(sub);
    ++n;
  }
  armTimer_();
}

void App::clearSubscriptions_()
{
  ZmScheduler *scheduler = scheduler_();
  ++m_state->timerGeneration;
  if (scheduler) scheduler->del(&m_state->timer);
  while (auto node = m_state->pending.minimum()) {
    App_::Pending_ *pending = node->val();
    if (pending->sub) {
      pending->sub->snapshot = nullptr;
      pending->sub->running = false;
    }
    App_::indexDel(m_state->pending, node->key(), pending);
    delete pending;
  }
  auto i = m_state->subs.iter();
  while (App_::Subscription *sub = i()) {
    if (*sub->due) {
      m_state->due[unsigned(sub->group)].delNode(sub);
      sub->due.null();
    }
    i.del(sub);
  }
}

bool App::snapshot_(
    uint64_t seqNo, fbs::Group group,
    const Filter_::Filter &filter, App_::Subscription_ *sub_)
{
  App_::PendingKey key{seqNo};
  if (m_state->pending.findVal(key) ||
      m_state->pending.count_() >= m_cf.maxPending)
    return false;
  auto pending = new App_::Pending_;
  pending->sub = sub_;
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
      appTelemetry_(pending->app);
      pending->appCaptured = true;
      break;
    case fbs::Group::Alert:
    default:
      delete pending;
      return false;
  }

  App_::indexAdd(m_state->pending, ZuMv(key), pending);
  if (sub_) {
    sub_->snapshot = pending;
    sub_->running = true;
  }
  scheduler_()->run([this, seqNo]() {
    snapshotBatch_(seqNo);
  }, m_cf.workerThread);
  return true;
}

void App::snapshotBatch_(uint64_t seqNo)
{
  App_::Pending_ *pending = m_state->pending.findVal(seqNo);
  if (!pending) return;
  unsigned budget = m_cf.maxPending;
  bool done = false;
  auto sendBatch = [this, pending, seqNo, &budget](
      fbs::TelemetryBody type, const auto &samples) {
    unsigned n = samples.length();
    while (pending->offset < n && budget) {
      publish_(App_::telemetryFrame(
	m_cf.id, seqNo, type, samples[pending->offset++]));
      --budget;
    }
    return pending->offset == n;
  };

  switch (pending->group) {
    case fbs::Group::Heap:
      done = sendBatch(fbs::TelemetryBody::HeapTelemetry, pending->heaps);
      break;
    case fbs::Group::Hash:
      done = sendBatch(fbs::TelemetryBody::HashTelemetry, pending->hashes);
      break;
    case fbs::Group::Thread:
      done = sendBatch(fbs::TelemetryBody::ThreadTelemetry, pending->threads);
      break;
    case fbs::Group::Mx:
      while (budget && pending->stage < 2) {
	bool stageDone = pending->stage ?
	  sendBatch(fbs::TelemetryBody::CxnTelemetry, pending->cxns) :
	  sendBatch(fbs::TelemetryBody::MxTelemetry, pending->mxs);
	if (!stageDone) break;
	++pending->stage;
	pending->offset = 0;
      }
      done = pending->stage == 2;
      break;
    case fbs::Group::Queue:
      done = sendBatch(fbs::TelemetryBody::QueueTelemetry, pending->queues);
      break;
    case fbs::Group::Hub:
      while (budget && pending->stage < 3) {
	bool stageDone;
	switch (pending->stage) {
	  case 0:
	    stageDone = sendBatch(
	      fbs::TelemetryBody::HubTelemetry, pending->hubs);
	    break;
	  case 1:
	    stageDone = sendBatch(
	      fbs::TelemetryBody::LinkTelemetry, pending->links);
	    break;
	  default:
	    stageDone = sendBatch(
	      fbs::TelemetryBody::PoolTelemetry, pending->pools);
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
	    stageDone = sendBatch(
	      fbs::TelemetryBody::DBTelemetry, pending->dbs);
	    break;
	  case 1:
	    stageDone = sendBatch(
	      fbs::TelemetryBody::DBHostTelemetry, pending->hosts);
	    break;
	  default:
	    stageDone = sendBatch(
	      fbs::TelemetryBody::DBTableTelemetry, pending->tables);
	    break;
	}
	if (!stageDone) break;
	++pending->stage;
	pending->offset = 0;
      }
      done = pending->stage == 3;
      break;
    case fbs::Group::App:
      if (pending->appCaptured && !pending->offset) {
	publish_(App_::telemetryFrame(m_cf.id, seqNo,
	  fbs::TelemetryBody::AppTelemetry, pending->app));
	pending->offset = 1;
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
  scheduler_()->run([this, seqNo]() {
    snapshotBatch_(seqNo);
  }, m_cf.workerThread);
}

void App::snapshotDone_(App_::Pending_ *pending)
{
  auto sub = static_cast<App_::Subscription *>(pending->sub);
  App_::indexDel(m_state->pending, pending->seqNo, pending);
  if (sub) {
    sub->snapshot = nullptr;
    sub->running = false;
  }
  sendEOS_(pending->seqNo);
  delete pending;

  if (sub && sub->dirty) {
    sub->dirty = false;
    if (*sub->due) {
      m_state->due[unsigned(sub->group)].delNode(sub);
      sub->due.null();
    }
    runSubscription_(sub);
  }
  armTimer_();
}

void App::sendAck_(
    uint64_t seqNo, fbs::AckStatus status, uint32_t interval)
{
  Zfb::IOBuilder fbb{
    frameBuf(ZmRef<ZiIOBuf>{new App_::MsgFrame})};
  auto ack = ZfbStruct::save(fbb, Ack{m_cf.id, seqNo, interval, uint8_t(status)});
  fbb.Finish(saveMsg(fbb, fbs::Body::Ack, ack.Union()));
  publishRaw_(saveHdr(fbb), App_::Delivery::Control);
}

void App::sendError_(uint64_t seqNo, int32_t code, ZuCSpan message)
{
  Zfb::IOBuilder fbb{
    frameBuf(ZmRef<ZiIOBuf>{new App_::MsgFrame})};
  auto error = ZfbStruct::save(fbb, Error{ErrorMessage{message}, m_cf.id, seqNo, code});
  fbb.Finish(saveMsg(fbb, fbs::Body::Error, error.Union()));
  publishRaw_(saveHdr(fbb), App_::Delivery::Control);
}

void App::sendEOS_(uint64_t seqNo)
{
  Zfb::IOBuilder fbb{
    frameBuf(ZmRef<ZiIOBuf>{new App_::MsgFrame})};
  auto eos = ZfbStruct::save(fbb, EOS{m_cf.id, seqNo});
  fbb.Finish(saveMsg(fbb, fbs::Body::EOS, eos.Union()));
  publishRaw_(saveHdr(fbb), App_::Delivery::Control);
}

} // Ztc
