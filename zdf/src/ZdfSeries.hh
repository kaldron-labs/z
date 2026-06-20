//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Data Series
// - chunked into blocks
// - compressed (see ZdfCompress)
// - fixed (ZuFixed) and floating point (64bit double)
// - indexable (monotonically increasing, e.g. time series)
// - support archiving of old data with purge()
// - in-memory or file-backed (see ZdfMem / ZdfFile)

#ifndef ZdfSeries_HH
#define ZdfSeries_HH

#ifndef ZdfLib_HH
#include <zlib/ZdfLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuFixed.hh>
#include <zlib/ZuSort.hh>
#include <zlib/ZuSearch.hh>

#include <zlib/ZmRef.hh>
#include <zlib/ZmPQueue.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtLocalArray.hh>

#include <zlib/ZdfTypes.hh>
#include <zlib/ZdfSchema.hh>
#include <zlib/ZdfBlk.hh>

namespace Zdf {

template <typename> class Series;

namespace RdrState {
  ZtEnum(RdrState, int8_t,
    Stopped,	// seek / find completed
    Loading,	// read called, loading block data
    Reading,	// reading historical data
    Live,	// reading, waiting for live data
    Stopping);	// stopping while reading

  constexpr bool stopped(T v) { return v == Stopped; }
  constexpr bool reading(T v) { return v >= Loading; }
  constexpr bool live(T v) { return v == Live; }
}

// from an application perspective a reader is either stopped() or reading()
// - internally there are additional transient states
//   (Loading, Stopping), and a further distinction is made between
//   readers of live and historical data (Live)
// - a reader can also be failed in any state due to error

// Reader internals - possible state paths:
//
// (*) - permitted while failed
//
// null > Stopped		ctor
// Stopped > null		dtor (*)
//
// Stopped > Stopped		seek, find (via init) - clears failed flag (*)
// Stopped > Stopped		stop - idempotent
// Stopped > Loading		read - block data not cached
// Stopped > Reading		read - block data remains cached
// Loading > Reading		loaded - block data loaded, now cached
// Reading > Reading		next - read callback returns true
// Reading > Stopping		stop - stop called during read callback
// Reading > Loading		nextBlk - read hits end of block (not EOS)
// Reading > Live		live - read hits end of stream (EOS)
// Live > Live			notify - writer appends new value
// Live > Live			notifyBlk - writer appends new block
// Live > Stopping		stop - stop called while live
// Stopping > Stopped		stopped (*)
//
// error paths (failed flag is set):
//
// Loading > Stopping		loadFail - load failed
// Reading > Stopping		readFail - fatal error while Reading
// Live > Stopping		liveFail - fatal error while Live
//
// blkData pin/unpin (block data is pinned in cache while being read/written):
//
// pin		loaded
// unpin	stop | nextBlk | readFail | liveFail 
//
// historical / live reader (de-)registration:
//
// addHistReader	seek, find, liveFail
// delHistReader	live, stop
// addLiveReader	live
// delLiveReader	stop, liveFail

using ErrorFn = ZmFn<void()>;
using StopFn = ZmFn<void()>;

// the decoder determines the value type (fixed or floating point)
template <typename Decoder_>
class Reader : public ZmObject {
public:
  using Decoder = Decoder_;
  using Series = Zdf::Series<Decoder>;
  using PValue = typename Decoder::Value; // primitive value
  enum { Fixed = ZuIsSame<PValue, int64_t>{} };
  using Value = ZuIf<Fixed, ZuFixed, double>;

private:
friend Series;

  ZuAssert((!ZuIsSame<Offset, Value>{})); // ensure distinct
  using Target = ZuUnion<void, Offset, Value>;

public:
  virtual ~Reader() { }

  Reader() = delete;
  Reader(const Reader &) = delete;
  Reader &operator =(const Reader &) = delete;
  Reader(Reader &&) = delete;
  Reader &operator =(Reader &&) = delete;

  const Series *series() const { return m_series; }
  bool paused() const { return m_paused; }
  bool stopped() const { return RdrState::stopped(m_state); }
  bool reading() const { return RdrState::reading(m_state); }
  bool live() const { return m_state == RdrState::Live; }
  bool failed() const { return m_failed; }
  NDP ndp() const { return m_blk ? m_blk->ndp() : NDP(0); }
  Offset offset() const;

  // the reader control interface is passed to the app's read callback
  class Ctrl;

friend Ctrl;
  class Ctrl {
  friend Reader;

    Ctrl(Reader &reader_) : reader{reader_} { }

  public:
    Reader &reader;

    // obtain reference to reader
    auto ref() const { return ZmRef<Reader>{&reader}; }

    // app's read callback
    using Fn = ZmFn<bool(Ctrl &, Value)>;

    // update callback
    void fn(Fn fn) { reader.fn(ZuMv(fn)); }
    void errorFn(ErrorFn fn) { reader.errorFn(ZuMv(fn)); }

    // seek forward to offset
    void seekFwd(Offset offset);
    // seek reverse to offset
    void seekRev(Offset offset);

    // series must monotonically increase to use find*() (e.g. time series)

    // seek forward to >= value
    void findFwd(Value value);
    // seek backwards to >= value
    void findRev(Value value);

    // pause reading
    void pause();

    // stop reading
    Offset stop(StopFn = {});

    // purge historical data up to current read position
    void purge();
  };

  using Fn = Ctrl::Fn;

  // resume reading (idempotent)
  void resume();

  // stop reading (called from outside callback, e.g. to stop a live reader)
  Offset stop(StopFn = {});

protected:
  Reader(
    const Series *series, BlkOffset blkOffset, const Blk *blk,
    Target target, Fn fn, ErrorFn errorFn, bool paused)
  :
    m_series{series}, m_blkOffset{blkOffset}, m_blk{blk},
    m_target{target}, m_fn{ZuMv(fn)}, m_errorFn{ZuMv(errorFn)},
    m_paused(paused)
  {
    ZiAssert(m_blk, "Zdf",
      (name = ZeString{m_series->name()}),
      name << " internal error - null blk", return);
  }

private:
  void seek(BlkOffset, const Blk *, Target);

  void fn(Fn fn) { m_fn = ZuMv(fn); }
  void errorFn(ErrorFn fn) { m_errorFn = ZuMv(fn); }

  void pause() { m_paused = true; }
  void goLive();
  void goHist();

  void loadBlk();
  void loaded(const Blk *blk);
  bool nextBlk();
  void nextValue();
  bool notifyValue(const uint8_t *end);

  void stopped();

  void fail();

  Offset offset_() const {
    return !m_decoder ? 0 : m_blk->offset() + m_decoder.offset();
  }

private:
  const Series		*m_series = nullptr;
  BlkOffset		m_blkOffset = 0;
  const Blk		*m_blk = nullptr;
  RdrState::T		m_state = RdrState::Stopped;
  Target		m_target;
  Decoder		m_decoder;
  Fn			m_fn;
  ErrorFn		m_errorFn;
  StopFn		m_stopFn;
  bool			m_paused = false;
  bool			m_failed = false;
};

template <typename Decoder>
ZuDerive(ReaderList,
  (ZmList<Reader<Decoder>,
    ZmListNode<Reader<Decoder>,
      ZmListHeapID<"Zdf.Reader">>>));

template <typename Decoder>
using RdrNode = typename ReaderList<Decoder>::Node;

// utility function to cast Reader to RdrNode
template <typename Decoder>
inline auto node(Reader<Decoder> *ptr) {
  return static_cast<RdrNode<Decoder> *>(ptr);
}

// Writer main class
template <typename Decoder, typename Heap, typename PValue_>
class Writer__ : public Heap, public ZmObject {
public:
  using Encoder = Zdf::Encoder<Decoder>;
  using Series = Zdf::Series<Decoder>;
  using PValue = PValue_;

private:
  friend Series;

protected:
  Writer__(Series *series, Offset offset, ErrorFn errorFn) :
    m_series{series}, m_offset{offset}, m_errorFn{ZuMv(errorFn)} { }
public:
  virtual ~Writer__() = default;

  Series *series() const { return m_series; }
  Offset offset() const { return m_offset; }
  const Encoder &encoder() const { return m_encoder; }
  bool failed() const { return m_failed; }

  // append value to series, notifying any live readers
  bool write(PValue);

  // stop writing (idempotent)
  void stop(StopFn fn = {});

protected:
  template <typename ...NDP>
  void finish(Blk *lastBlk, NDP... ndp) {
    m_encoder.finish();
    lastBlk->sync(m_encoder, m_encoder.last(), ndp...);
  }

private:
  bool savedBlk(); // returns true if stopped

  void fail() {
    m_failed = true;
    auto errorFn = ZuMv(m_errorFn);
    m_errorFn = ErrorFn{};
    errorFn();
    stop(); // do last
  }

  template <typename L> void encoder(L &&l) { m_encoder = ZuFwd<L>(l)(); }
  bool encode(PValue value) { return m_encoder.write(value); }

  Series	*m_series = nullptr;
  Offset	m_offset = 0;
  Encoder	m_encoder;
  ErrorFn	m_errorFn;
  StopFn	m_stopFn;
  bool		m_failed = false;
  bool		m_stopping = false;
};
template <typename Decoder, typename Heap, typename PValue = Decoder::Value>
class Writer_ : public Writer__<Decoder, Heap, PValue> {
  using Base = Writer__<Decoder, Heap, PValue>;

protected:
  using Base::Base;

public:
  virtual ~Writer_() = default;
};
template <typename Decoder, typename Heap>
class Writer_<Decoder, Heap, int64_t> :
  public Writer__<Decoder, Heap, int64_t>
{
  using Base = Writer__<Decoder, Heap, int64_t>;

public:
  using typename Base::Series;

private:
  friend Series;

protected:
  Writer_(Series *series, Offset offset, ErrorFn errorFn, NDP ndp) :
    Base{series, offset, ZuMv(errorFn)}, m_ndp{ndp} { }
public:
  virtual ~Writer_() = default;

  NDP ndp() const { return m_ndp; }

  void finish(Blk *lastBlk) {
    Base::finish(lastBlk, m_ndp);
  }

private:
  NDP		m_ndp;
};
template <typename Decoder>
using Writer_Heap = ZmHeap<"Zdf.Writer", Writer_<Decoder, ZuEmpty>>;
template <typename Decoder>
ZuDerive(Writer, (Writer_<Decoder, Writer_Heap<Decoder>>));

class Store;
// template <typename O, bool TimeIndex> class DataFrame;

// each IndexBlk contains Blk[512]
constexpr unsigned IndexBlkShift = 9;
constexpr unsigned IndexBlkSize = 1<<IndexBlkShift;
constexpr unsigned IndexBlkMask = ~((~0U)<<IndexBlkShift);

// the series index is a skiplist (ZmPQueue) of IndexBlks
struct IndexBlk_ : public ZuObject {
  Offset	offset;			// block offset
  Blk		blks[IndexBlkSize];

  IndexBlk_(Offset offset_) : offset{offset_} { }
};
struct IndexBlk_Fn {
  IndexBlk_	&indexBlk;

  using Key = Offset;
  static Key KeyAxor(const IndexBlk_ &indexBlk) { return indexBlk.offset; }

  ZuInline Offset key() const { return indexBlk.offset; }
  static constexpr uint64_t length() { return IndexBlkSize; }
};
ZuDerive(Index,
  (ZmPQueue<IndexBlk_,
    ZmPQueueFn<IndexBlk_Fn,
      ZmPQueueNode<IndexBlk_,
	ZmPQueueStats<false,
	  ZmPQueueOverlap<false,
	    ZmPQueueBits<3,
	      ZmPQueueLevels<3,
		ZmPQueueHeapID<"Zdf.IndexBlk">>>>>>>>));
using IndexBlk = Index::Node;

struct InternalError { };	// internal exception

struct AnySeries : public ZmObject {
  virtual void unloadBlkData(BlkData *blkData) const = 0;
};
template <typename Decoder_>
class Series : public AnySeries {
public:
  using Decoder = Decoder_;
  using Encoder = Zdf::Encoder<Decoder>;
  using Reader = Zdf::Reader<Decoder>;
  using ReadFn = typename Reader::Fn;
  using Writer = Zdf::Writer<Decoder>;
  using PValue = typename Decoder::Value;
  enum { Fixed = ZuIsSame<PValue, int64_t>{} };
  using Value = ZuIf<Fixed, ZuFixed, double>;

private:
  using ReaderList = Zdf::ReaderList<Decoder>;
  using RdrNode = Zdf::RdrNode<Decoder>;

protected:
  using DBSeries = ZuIf<Fixed, DB::SeriesFixed, DB::SeriesFloat>;
  using DBBlk = ZuIf<Fixed, DB::BlkFixed, DB::BlkFloat>;

private:
friend Store;
template <typename> friend class Zdf::Reader;
template <typename, typename, typename> friend class Writer__;

protected:
  Series(Store *store, ZdbObjRef<DBSeries> dbSeries) :
    m_store{store}, m_dbSeries{ZuMv(dbSeries)}
  {
    m_dbSeries->pin();
    // these are immutable and frequently accessed, so cache them
    m_shard = m_dbSeries->shard();
    m_id = m_dbSeries->data().id;
    m_name = &m_dbSeries->data().name;
  }

public:
  virtual ~Series() { m_dbSeries->unpin(); }

  Store *store() const { return m_store; }
  bool opened() const { return m_opened; }
  ZdbObject<DBSeries> *dbSeries() const { return m_dbSeries; }
  unsigned shard() const { return m_shard; }
  SeriesID id() const { return m_id; }
  const IDString &name() const { return *m_name; }

public:
  // run/invoke on shard
  template <typename ...Args> void run(Args &&...args) const;
  template <typename ...Args> void invoke(Args &&...args) const;
  bool invoked() const;

  // first blkOffset (will be non-zero following a purge())
  BlkOffset head() const {
    ZmAssert(invoked());

    return m_index.head();
  }

  // number of blocks in index
  unsigned blkCount() const {
    ZmAssert(invoked());

    return (m_lastBlkOffset + 1) - head();
  }

  // check if blk is last block in series
  bool lastBlk(const Blk *blk) const {
    ZmAssert(invoked());

    return blk == m_lastBlk;
  }

  // value count (length of series in #values)
  Offset count() const {
    ZmAssert(invoked());

    if (ZuUnlikely(!blkCount())) return 0;
    if (ZuUnlikely(!m_lastBlk)) return 0;
    return m_lastBlk->offset() + m_lastBlk->count();
  }

  // length in bytes (compressed)
  // - estimated value - intentionally prone to mild overestimation
  // - will not return a value less than the actual value
  uint64_t length() const {
    ZmAssert(invoked());

    unsigned n = blkCount();
    if (ZuUnlikely(!n)) return 0;
    auto blk = lastBlk();
    if (ZuUnlikely(!blk)) return 0;
    ZiAssert(blk->blkData, "Zdf",
      (name = ZeString{name()}),
      name << " internal error - null blkData", return n * BlkSize);
    return (n - 1) * BlkSize + blk->blkData->data().buf.length();
  }

  // Reader seek/find
  template <bool Paused = false>
  void seek(Offset offset, ReadFn readFn, ErrorFn errorFn = {}) const {
    ZmAssert(invoked());

    BlkOffset blkOffset;
    if (offset == MaxOffset)
      blkOffset = m_lastBlkOffset;
    else {
      uint64_t result;
      try {
	result = ZuInterSearch(blkCount(), seekFn(m_index.head(), offset));
      } catch (InternalError) {
	errorFn();
	return;
      }
      blkOffset = m_index.head() + ZuSearchPos(result);
      if (blkOffset > m_lastBlkOffset) blkOffset = m_lastBlkOffset;
    }
    ZmRef<Reader> reader = new RdrNode{
      this, blkOffset, getBlk(blkOffset), offset,
      ZuMv(readFn), ZuMv(errorFn), Paused};
    addHistReader(reader);
    reader->loadBlk();
  }

  template <bool Paused = false>
  void find(Value value, ReadFn readFn, ErrorFn errorFn = {}) const {
    ZmAssert(invoked());

    uint64_t result;
    try {
      result = ZuInterSearch(blkCount(), findFn(m_index.head(), value));
    } catch (InternalError) {
      errorFn();
      return;
    }
    BlkOffset blkOffset = m_index.head() + ZuSearchPos(result);
    if (blkOffset > m_lastBlkOffset) blkOffset = m_lastBlkOffset;
    ZmRef<Reader> reader = new RdrNode{
      this, blkOffset, getBlk(blkOffset), value,
      ZuMv(readFn), ZuMv(errorFn), Paused};
    addHistReader(reader);
    reader->loadBlk();
  }

  template <typename Vec, typename Fn, bool Live = false>
  auto readVec(Vec vec, unsigned n, Fn fn) {
    return [
      vec = ZuMv(vec), n, fn = ZuMv(fn), i = 0
    ](auto &rc, Value value) mutable -> bool {
      if (ZuUnlikely(ZuCmp<Value>::null(value))) {
	if constexpr (Live) return true;
      } else {
	vec[i++] = value;
	if (i < n) return true;
      }
      rc.stop();
      ZuMv(fn)(ZuMv(vec));
      return false;
    };
  }

private:
  void seekFwd(Reader *reader, BlkOffset blkOffset, Offset offset) const {
    uint64_t result;
    try {
      result = ZuInterSearch(
	(m_lastBlkOffset + 1) - blkOffset, seekFn(blkOffset, offset));
    } catch (InternalError) {
      reader->fail();
      return;
    }
    blkOffset += ZuSearchPos(result);
    if (blkOffset > m_lastBlkOffset) blkOffset = m_lastBlkOffset;
    reader->seek(blkOffset, getBlk(blkOffset), offset);
    reader->loadBlk();
  }
  void seekRev(Reader *reader, BlkOffset blkOffset, Offset offset) const {
    uint64_t result;
    try {
      result = ZuInterSearch(
	(blkOffset + 1) - m_index.head(), seekFn(m_index.head(), offset));
    } catch (InternalError) {
      reader->fail();
      return;
    }
    blkOffset = m_index.head() + ZuSearchPos(result);
    if (blkOffset > m_lastBlkOffset) blkOffset = m_lastBlkOffset;
    // if the reader is already in the last block and the offset is
    // actually past the end of the series despite the caller requesting
    // a reverse search, then the result can be past the end
    reader->seek(blkOffset, getBlk(blkOffset), offset);
    reader->loadBlk();
  }

  void findFwd(Reader *reader, BlkOffset blkOffset, Value value) const {
    uint64_t result;
    try {
      result = ZuInterSearch(
	(m_lastBlkOffset + 1) - blkOffset, findFn(blkOffset, value));
    } catch (InternalError) {
      reader->fail();
      return;
    }
    blkOffset += ZuSearchPos(result);
    if (blkOffset > m_lastBlkOffset) blkOffset = m_lastBlkOffset;
    reader->seek(blkOffset, getBlk(blkOffset), value);
    reader->loadBlk();
  }
  void findRev(Reader *reader, BlkOffset blkOffset, Value value) const {
    uint64_t result;
    try {
      result = ZuInterSearch(
	(blkOffset + 1) - m_index.head(), findFn(m_index.head(), value));
    } catch (InternalError) {
      reader->fail();
      return;
    }
    blkOffset = m_index.head() + ZuSearchPos(result);
    // if the reader is already in the last block and the value is
    // actually past the end of the series despite the caller requesting
    // a reverse search, then the result can be past the end
    if (blkOffset > m_lastBlkOffset) blkOffset = m_lastBlkOffset;
    reader->seek(blkOffset, getBlk(blkOffset), value);
    reader->loadBlk();
  }

  // open() - open series and query blk table to fill index
  void open(auto fn) {
    ZmAssert(invoked());

    m_index.head(m_dbSeries->data().blkOffset);
    const auto &data = m_dbSeries->data();
    blkTbl()->template nextRows<0>(
      ZuFwdTuple(data.id, data.blkOffset), true, IndexBlkSize,
      ZuLambda{[
	this_ = ZmMkRef(this), fn = ZuMv(fn), rowRcvd = false
      ](auto &&self, auto result, unsigned) mutable {
	if (this_->m_opened) return; // index already filled
	using Row = ZuStructTuple<DBBlk>;
	if (result.template is<Row>()) { // fill index
	  if (!this_->open_loadBlk(result.template p<Row>())) goto opened;
	  const_cast<bool &>(rowRcvd) = true; // clang needs const_cast
	} else { // complete
	  if (!rowRcvd) goto opened;
	  const_cast<bool &>(rowRcvd) = false; // clang needs const_cast
	  this_->blkTbl()->template nextRows<0>(
	    ZuFwdTuple(this_->m_dbSeries->data().id, this_->m_lastBlkOffset),
	    false, IndexBlkSize, ZuMv(self));
	}
	return;
      opened:
	this_->m_opened = true;
	// run open callback on the correct shard
	this_->run([this_ = ZuMv(this_), fn = ZuMv(fn)]() mutable {
	  fn(ZuMv(this_));
	});
      }});
  }

  // load an individual block header into the index during open
  template <typename Row>
  bool open_loadBlk(const Row &row) {
    BlkOffset blkOffset = row.template p<1>();
    auto blk = setBlk(blkOffset);
    if (!blk) return false;
    if constexpr (Fixed)
      blk->init(
	row.template p<2>(), row.template p<4>(),
	row.template p<5>(), row.template p<3>());
    else
      blk->init(
	row.template p<2>(), row.template p<4>(),
	row.template p<3>());
    m_lastBlk = blk;
    m_lastBlkOffset = blkOffset;
    return true;
  }

  ZdbTable<DB::BlkData> *blkDataTbl() const;
  ZuIf<Fixed, ZdbTable<DB::SeriesFixed> *, ZdbTable<DB::SeriesFloat> *>
  seriesTbl() const;
  ZuIf<Fixed, ZdbTable<DB::BlkFixed> *, ZdbTable<DB::BlkFloat> *>
  blkTbl() const;

  BlkData *newBlkData(BlkOffset blkOffset) const {
    return newBlkData(blkOffset, blkDataTbl());
  }
  BlkData *newBlkData(BlkOffset blkOffset, ZdbTable<DB::BlkData> *tbl) const {
    auto blkData = new BlkData(tbl, m_shard);
    new (blkData->ptr_()) DB::BlkData{
      .blkOffset = blkOffset,
      .seriesID = m_id,
      .series = static_cast<AnySeries *>(const_cast<Series *>(this)),
    };
    return blkData;
  }

public:
  template <typename ...NDP>
  ZuIfT<sizeof...(NDP) == Fixed, void>
  write(ZmFn<void(ZmRef<Writer>)> fn, ErrorFn errorFn, NDP... ndp) {
    ZmAssert(invoked());

    // if this is a fixed-point series, validate the ndp parameter
    if constexpr (Fixed) ZuAssert(ZuTraits<NDP...>::IsIntegral);

    if (m_writer) { errorFn(); return; }
    if (!m_lastBlk) { // new series - append first block
      m_writer = new Writer{this, 0, ZuMv(errorFn), ndp...};
      pushFirstBlk();
      write_loadedBlk(ZuMv(fn), ndp...);
    } else {
      auto lastBlkCount = m_lastBlk->count();
      m_writer = new Writer{
	this, m_lastBlk->offset() + lastBlkCount, ZuMv(errorFn), ndp...};
      if (lastBlkCount) { // last block is not empty - load the data
	if (m_lastBlk->blkData) {
	  write_loadedBlk(ZuMv(fn), ndp...);
	  return;
	}
	ZmRef<IndexBlk> indexBlk = m_index.find(m_lastBlkOffset);
	loadBlk(m_lastBlkOffset, [
	  this, fn = ZuMv(fn), ndp..., indexBlk = ZuMv(indexBlk)
	](ZmRef<BlkData> blkData) mutable {
	  if (ZuUnlikely(!blkData)) {
	    m_writer->fail();
	    return;
	  }
	  m_lastBlk->blkData = ZuMv(blkData);
	  write_loadedBlk(ZuMv(fn), ndp...);
	});
      } else { // last block is empty - allocate the data
	if (!m_lastBlk->blkData)
	  m_lastBlk->blkData = newBlkData(m_lastBlkOffset);
	write_loadedBlk(ZuMv(fn), ndp...);
      }
    }
  }

  // stop all readers
  void stopReading() {
    ZmAssert(invoked());

    unsigned n = m_histReaders.count_() + m_liveReaders.count_();
    if (!n) return;
    using RdrRef = ZmRef<RdrNode>;
    using RdrRefs = ZtArray<RdrRef, ZtArrayHeapID<"Zdf.RdrRefs">>;
    auto readers = ZtLocalArray(RdrRefs, n);
    {
      auto i = m_histReaders.citer();
      while (RdrNode *reader = i()) {
	if (ZuUnlikely(readers.length() >= n)) break;
	readers.push(reader);
      }
    }
    {
      auto i = m_liveReaders.citer();
      while (RdrNode *reader = i()) {
	if (ZuUnlikely(readers.length() >= n)) break;
	readers.push(reader);
      }
    }
    readers.all([](auto &&reader) { reader->stop(); });
  }

  // stop writer
  void stopWriting(StopFn fn = {}) {
    ZmAssert(invoked());

    if (m_writer) m_writer->stop(ZuMv(fn));
  }

private:
  template <typename ...NDP>
  ZuIfT<sizeof...(NDP) == Fixed, void>
  write_loadedBlk(ZmFn<void(ZmRef<Writer>)> fn, NDP... ndp) {
    m_lastBlk->blkData->pin();
    if (!m_lastBlk->count()) {
      write_newWriter(ZuMv(fn), ndp...);
      return;
    }
    bool newBlk = m_lastBlk->space() < 3;		// need >3 bytes' space
    if constexpr (Fixed)
      if (!newBlk && m_lastBlk->ndp() != (..., ndp))	// NDP must coincide
	newBlk = true;
    if (newBlk) {
      pushBlk();
      write_newWriter(ZuMv(fn), ndp...);
      return;
    }
    // continue writing to partially-full last block
    auto &buf = m_lastBlk->blkData->data().buf;
    Decoder decoder{buf.data(), buf.data() + buf.length()};
    while (decoder.skip()); // skip to end
    // construct encoder from decoder to continue writing block
    m_writer->encoder([&decoder, &buf]{
      return Encoder{decoder, buf.data() + BlkSize};
    });
    fn(ZmRef<Writer>{m_writer});
  }
  template <typename... NDP>
  ZuIfT<sizeof...(NDP) == Fixed, void>
  write_newWriter(ZmFn<void(ZmRef<Writer>)> fn, NDP... ndp) {
    if constexpr (Fixed) m_lastBlk->ndp(ndp...);
    m_writer->encoder([this]{ return m_lastBlk->encoder<Decoder>(this); });
    // call async to constrain stack depth
    run([this_ = ZmMkRef(this), fn = ZuMv(fn)]() mutable {
      ZuMv(fn)(ZmRef<Writer>{this_->m_writer});
    });
  }

  // add first block to series
  void pushFirstBlk() {
    m_lastBlkOffset = 0;
    IndexBlk *indexBlk = new IndexBlk{0};
    m_index.add(indexBlk);
    m_lastBlk = &indexBlk->blks[0];
    // m_lastBlk->offset(0); // redundant
    m_lastBlk->blkData = newBlkData(m_lastBlkOffset);
  }
  // add subsequent block to series
  void pushBlk() {
    auto offset = m_lastBlk->offset() + m_lastBlk->count();
    ++m_lastBlkOffset;
    IndexBlk *indexBlk = m_index.find(m_lastBlkOffset);
    if (!indexBlk)
      m_index.add(indexBlk = new IndexBlk{m_lastBlkOffset & ~IndexBlkMask});
    m_lastBlk = &indexBlk->blks[m_lastBlkOffset - indexBlk->offset];
    m_lastBlk->offset(offset);
    m_lastBlk->blkData = newBlkData(m_lastBlkOffset);
    m_lastBlk->blkData->pin();
  }
  // get Blk from index
  const Blk *getBlk(BlkOffset blkOffset) const {
    ZmRef<IndexBlk> indexBlk = m_index.find(blkOffset);
    if (!indexBlk) return nullptr;
    return &indexBlk->blks[blkOffset - indexBlk->offset];
  }
  // set Blk in index
  Blk *setBlk(BlkOffset blkOffset) {
    if (ZuUnlikely(blkOffset < m_index.head())) return nullptr;
    ZmRef<IndexBlk> indexBlk = m_index.find(blkOffset);
    if (!indexBlk)
      m_index.add(indexBlk = new IndexBlk{blkOffset & ~IndexBlkMask});
    return &indexBlk->blks[blkOffset - indexBlk->offset];
  }

  // store the first value in a new series (used for subsequent searching)
  void write_firstValue(Value value) {
    auto &data = m_dbSeries->data();
    if constexpr (Fixed) {
      data.first = value.mantissa;
      data.ndp = value.ndp;
    } else {
      data.first = value;
    }
    seriesTbl()->template update<>(m_dbSeries,
      [](ZdbObject<DBSeries> *dbSeries) {
	if (dbSeries) dbSeries->commit();
      });
  }
  // main write function
  bool write_(Writer *writer, PValue value) {
    auto offset = writer->encoder().offset();
    if (ZuUnlikely(!(writer->offset() + offset))) {
      if constexpr (Fixed) {
	auto ndp = writer->ndp();
	write_firstValue(ZuFixed{value, ndp});
	m_lastBlk->ndp(ndp);
      } else {
	write_firstValue(value);
      }
    }
    if (offset < MaxBlkCount && writer->encode(value)) return true;
    writer->finish(m_lastBlk);
    saveBlk();
    pushBlk();
    writer->encoder([this]() { return m_lastBlk->encoder<Decoder>(this); });
    if constexpr (Fixed) m_lastBlk->ndp(writer->ndp());
    return writer->encode(value);
  }
  // notify live readers
  void write_notify(const uint8_t *end) {
    auto i = m_liveReaders.iter();
    while (auto reader = i())
      if (!reader->notifyValue(end)) {
	m_histReaders.pushNode(reader);
	i.del();
      }
  }
  // called from Writer_::write
  bool write(Writer *writer, PValue value) {
    bool ok = write_(writer, value);
    if (ok) write_notify(writer->encoder().end());
    return ok;
  }

  // called from Writer::stop
  template <typename... NDP>
  ZuIfT<sizeof...(NDP) == Fixed, void>
  stop(Encoder &encoder, NDP... ndp) {
    encoder.finish();
    m_lastBlk->sync(encoder, encoder.last(), ndp...);
    saveBlk();
    encoder = {};
  }

  // save block to database (always m_lastBlk)
  void saveBlk() {
    ZiAssert(m_lastBlk, "Zdf",
      (name = ZeString{name()}),
      name << " internal error - null blk", return);
    ZiAssert(m_lastBlk->blkData, "Zdf",
      (name = ZeString{name()}),
      name << " internal error - null blkData", return);

    static auto lastFn = [](const Blk *blk) {
      if constexpr (Fixed)
	return blk->last.fixed;
      else
	return blk->last.float_;
    };

    if (m_lastBlk->blkData->state() == ZdbObjState::Undefined) {
      ZdbObjRef<DBBlk> dbBlk = new ZdbObject<DBBlk>{blkTbl(), m_shard};
      new (dbBlk->ptr_()) DBBlk{
	.blkOffset = m_lastBlkOffset,
	.offset = m_lastBlk->offset(),
	.last = lastFn(m_lastBlk),
	.seriesID = id(),
	.count = m_lastBlk->count(),
      };
      if constexpr (Fixed) dbBlk->ptr()->ndp = m_lastBlk->ndp();

      blkTbl()->insert(
	ZuMv(dbBlk), [this_ = ZmMkRef(this)](ZdbObject<DBBlk> *dbBlk) {
	  ZiAssert(dbBlk, "Zdf", (name = this_->name()),
	    name << "internal error - insert - null dbBlk", return);

	  dbBlk->commit();

	  auto ptr = this_.ptr();
	  ptr->blkDataTbl()->insert(
	    ptr->m_lastBlk->blkData,
	    [this_ = ZuMv(this_)](ZdbObject<DB::BlkData> *dbBlkData) mutable {
	      ZiAssert(dbBlkData, "Zdf", (name = this_->name()),
		name << "internal error - insert - null dbBlkData", return);

	      dbBlkData->commit();
	      dbBlkData->unpin();
	      this_->savedBlk();
	    });
	});
    } else {
      blkTbl()->template findUpd<0>(
	shard(), ZuFwdTuple(id(), m_lastBlkOffset),
	[this_ = ZmMkRef(this)](ZdbObject<DBBlk> *dbBlk) {
	  ZiAssert(dbBlk, "Zdf", (name = this_->name()),
	    name << "internal error - update - null dbBlk", return);
	  ZiAssert(this_->m_lastBlk, "Zdf", (name = this_->name()),
	    name << "internal error - update - null lastBlk", return);

	  auto &data = dbBlk->data();
	  data.offset = this_->m_lastBlk->offset();
	  data.last = lastFn(this_->m_lastBlk);
	  data.count = this_->m_lastBlk->count();
	  if constexpr (Fixed) data.ndp = this_->m_lastBlk->ndp();
	  dbBlk->commit();

	  auto ptr = this_.ptr();
	  ptr->blkDataTbl()->template update<>(
	    ptr->m_lastBlk->blkData,
	    [this_ = ZuMv(this_)](ZdbObject<DB::BlkData> *blkData) mutable {
	      ZiAssert(blkData, "Zdf", (name = this_->name()),
		name << "internal error - update - null blkData", return);

	      blkData->commit();
	      blkData->unpin();
	      this_->savedBlk();
	    });
	});
    }
  }
  void savedBlk() {
    if (ZuLikely(m_writer))
      if (m_writer->savedBlk())
	m_writer = nullptr;
  }

  // load block from data store
  template <typename L>
  void loadBlk(BlkOffset blkOffset, L l) const {
    blkDataTbl()->template find<0>(
      shard(), ZuFwdTuple(id(), blkOffset), [
	this, l = ZuMv(l)
      ](ZmRef<BlkData> blkData) mutable {
	if (ZuUnlikely(!blkData)) { l(nullptr); return; }
	blkData->data().series =
	  static_cast<AnySeries *>(const_cast<Series *>(this));
	l(ZuMv(blkData));
      });
  }

  // load block data from data store (idempotent)
  template <typename L>
  void loadBlkData(BlkOffset blkOffset, L l) const {
    ZmRef<IndexBlk> indexBlk = m_index.find(blkOffset);
    if (!indexBlk) { l(nullptr); return; }
    auto blk = &indexBlk->blks[blkOffset - indexBlk->offset];
    if (blk->blkData) { l(blk); return; }
    loadBlk(blkOffset, [
      indexBlk = ZuMv(indexBlk), blk, l = ZuMv(l) // keep indexBlk in scope
    ](ZmRef<BlkData> blkData) mutable {
      if (ZuUnlikely(!blkData)) { l(nullptr); return; }
      blk->blkData = ZuMv(blkData);
      l(blk);
    });
  }

  // called from BlkData_Evict::evict() during cache eviction
  void unloadBlkData(BlkData *blkData) const {
    BlkOffset blkOffset = blkData->data().blkOffset;
    ZmRef<IndexBlk> indexBlk = m_index.find(blkOffset);
    if (!indexBlk) return;
    auto blk = &indexBlk->blks[blkOffset - indexBlk->offset];
    blk->blkData = nullptr;
  }

  // Reader management
  void addHistReader(Reader *reader) const {
    m_histReaders.pushNode(node(reader));
  }
  void delHistReader(Reader *reader) const {
    m_histReaders.delNode(node(reader));
  }
  void addLiveReader(Reader *reader) const {
    m_liveReaders.pushNode(node(reader));
  }
  void delLiveReader(Reader *reader) const {
    m_liveReaders.delNode(node(reader));
  }

  // seek function used in interpolation search
  auto seekFn(BlkOffset blkOffset, Offset target) const {
    return [this, blkOffset, target](uint64_t i) -> double {
      i += blkOffset;
      auto blk = getBlk(i);
      ZiAssert(blk, "Zdf",
	(name = ZeString{this->name()}),
	name << " internal error - null blk", throw InternalError{});
      Offset offset = blk->offset();
      if (target < offset) return double(target) - double(offset);
      auto n = blk->count();
      ZiAssert(n, "Zdf",
	(name = ZeString{this->name()}),
	name << " internal error - empty blk", throw InternalError{});
      offset += (n - 1);
      if (target > offset) return double(target) - double(offset);
      return 0;
    };
  }
  // find function used in interpolation search
  auto findFn(uint64_t blkOffset, Value target) const {
    return [this, blkOffset, target](uint64_t i) -> double {
      // get last value from preceding blk
      Value value;
      i += blkOffset;
      if (i <= m_index.head()) {	// before first blk - use series first
	const auto &data = m_dbSeries->data();
	if constexpr (Fixed)
	  value = ZuFixed{data.first, data.ndp};
	else
	  value = data.first;
      } else {
	auto blk = getBlk(i - 1);
	ZiAssert(blk, "Zdf",
	  (name = ZeString{this->name()}),
	  name << " internal error - null blk", throw InternalError{});
	if constexpr (Fixed)
	  value = ZuFixed{blk->last.fixed, blk->ndp()};
	else
	  value = blk->last.float_;
      }
      if constexpr (Fixed) {
	double target_ = target.fp(), value_ = value.fp();
	if (target_ < value_) return target_ - value_;
      } else {
	if (target < value) return target - value;
      }
      // get last value from containing blk
      auto blk = getBlk(i);
      ZiAssert(blk, "Zdf",
	(name = ZeString{this->name()}),
	name << " internal error - null blk", throw InternalError{});
      if constexpr (Fixed) {
	value = ZuFixed{blk->last.fixed, blk->ndp()};
	double target_ = target.fp(), value_ = value.fp();
	if (target_ > value_) return target_ - value_;
      } else {
	value = blk->last.float_;
	if (target > value) return target - value;
      }
      return 0;
    };
  }

  // called from Reader::purge()
  // - purge index up to, but not including, blkOffset
  void purge(Reader *reader, BlkOffset blkOffset) {
    if (ZuUnlikely(!m_lastBlkOffset)) return;
    blkOffset &= ~IndexBlkMask;
    if (ZuUnlikely(blkOffset >= m_lastBlkOffset))
      blkOffset = m_lastBlkOffset - 1;
    if (!blkOffset) return;
    auto prevBlk = getBlk(blkOffset - 1);
    ZiAssert(prevBlk, "Zdf",
      (name = ZeString{name()}),
      name << " internal error - null prevBlk", return);
    if (blkOffset > m_index.head()) m_index.head(blkOffset);
    // write new starting blkOffset and first value to data store
    auto &data = m_dbSeries->data();
    if constexpr (Fixed) {
      data.first = prevBlk->last.fixed;
      data.ndp = prevBlk->ndp();
    } else {
      data.first = prevBlk->last.float_;
    }
    data.blkOffset = blkOffset;
    seriesTbl()->template update<>(m_dbSeries,
      [](ZdbObject<DBSeries> *dbSeries) {
	if (dbSeries) dbSeries->commit();
      });
  }

private:
  Store			*m_store = nullptr;
  ZdbObjRef<DBSeries>	m_dbSeries;
  Shard			m_shard;
  SeriesID		m_id;
  const IDString	*m_name = nullptr;
  mutable Index		m_index;
  Blk			*m_lastBlk = nullptr;
  BlkOffset		m_lastBlkOffset = 0;
  mutable ReaderList	m_liveReaders;
  mutable ReaderList	m_histReaders;
  ZmRef<Writer>		m_writer;
  bool			m_opened = false;
};

// Zdb evict hook
inline void DB::BlkData_Evict::evict(ZdbObject<BlkData> *blkData)
{
  static_cast<AnySeries *>(blkData->data().series)->unloadBlkData(blkData);
}

class TimeSeries : public Series<DeltaDecoder<>> {
  using Base = Series<DeltaDecoder<>>;

friend Store;

  TimeSeries(Store *store, ZdbObjRef<Base::DBSeries> dbSeries) :
    Base{store, ZuMv(dbSeries)}
  {
    m_epoch = this->dbSeries()->data().epoch.as_time();
  }

public:
  virtual ~TimeSeries() = default;

  ZuTime epoch() const { return m_epoch; }

  // time relative to epoch (creation time of series)
private:
  static constexpr int64_t pow10_9 = 1000000000L;
public:
  int64_t nsecs(ZuTime t) {
    t -= m_epoch;
    return int64_t(t.sec()) * pow10_9 + t.nsec();
  }
  ZuTime time(int64_t v) {
    return ZuTime{v / pow10_9, int32_t(v % pow10_9)} + m_epoch;
  }

private:
  ZuTime		m_epoch;
};

// Reader implementation

template <typename Decoder>
inline Offset Reader<Decoder>::offset() const
{
  using namespace RdrState;

  switch (m_state) {
    case Loading:
      if (m_target.template is<Offset>())
	return m_target.template p<Offset>();
      ZiAssert(m_blk, "Zdf",
	(name = ZeString{m_series->name()}),
	name << " internal error - null blk", return 0);
      return m_blk->offset();
    case Stopped:
    case Stopping:
    case Reading:
    case Live:
      return offset_();
    default:
      ZiAssert(false, "Zdf",
	(name = ZeString{m_series->name()}, state = int(m_state)),
	name << " internal error - invalid state=" << state, return 0);
      return 0;
  }
}

template <typename Decoder>
inline void Reader<Decoder>::Ctrl::seekFwd(Offset offset)
{
  reader.series()->seekFwd(&reader, reader.m_blkOffset, offset);
}

template <typename Decoder>
inline void Reader<Decoder>::Ctrl::seekRev(Offset offset)
{
  reader.series()->seekRev(&reader, reader.m_blkOffset, offset);
}

template <typename Decoder>
inline void Reader<Decoder>::Ctrl::findFwd(Value value)
{
  reader.series()->findFwd(&reader, reader.m_blkOffset, value);
}

template <typename Decoder>
inline void Reader<Decoder>::Ctrl::findRev(Value value)
{
  reader.series()->findRev(&reader, reader.m_blkOffset, value);
}

template <typename Decoder>
inline void Reader<Decoder>::seek(
  BlkOffset blkOffset, const Blk *blk, Target target)
{
  using namespace RdrState;

  m_decoder = {};
  m_blk->blkData->unpin();
  if (m_state == Live) goHist();
  m_blkOffset = blkOffset;
  m_blk = blk;
  m_target = target;
}

template <typename Decoder>
inline void Reader<Decoder>::Ctrl::pause()
{
  reader.pause();
}

template <typename Decoder>
inline Offset Reader<Decoder>::Ctrl::stop(StopFn fn)
{
  return reader.stop(ZuMv(fn));
}

template <typename Decoder>
inline void Reader<Decoder>::resume()
{
  ZmAssert(m_series->invoked());

  using namespace RdrState;

  m_paused = false;

  if (m_state == Reading)
    m_series->run([this_ = ZmMkRef(node(this))]() { this_->nextValue(); });
}

template <typename Decoder>
inline Offset Reader<Decoder>::stop(StopFn fn)
{
  ZmAssert(m_series->invoked());

  using namespace RdrState;

  Offset offset = 0;

  auto unpin = [this] {
    ZiAssert(m_blk, "Zdf",
      (name = ZeString{m_series->name()}),
      name << " internal error - null blk", break);
    ZiAssert(m_blk->blkData, "Zdf",
      (name = ZeString{m_series->name()}),
      name << " internal error - null blkData", break);
    ZiAssert(m_decoder, "Zdf",
      (name = ZeString{m_series->name()}),
      name << " internal error - null decoder", (void)0);
    m_decoder = {};
    m_blk->blkData->unpin();
  };
  switch (m_state) {
    case Stopped:
    case Stopping:
      offset = offset_();
      fn();
      return offset;
    case Loading:
      ZiAssert(m_blk, "Zdf",
	(name = ZeString{m_series->name()}),
	name << " internal error - null blk", break);
      offset = m_target.template is<Offset>() ?
	m_target.template p<Offset>() : m_blk->offset();
      break;
    case Live:
      offset = offset_();
      unpin();
      m_series->delLiveReader(this);
      break;
    case Reading:
      offset = offset_();
      unpin();
      m_series->delHistReader(this);
      break;
    default:
      ZiAssert(false, "Zdf",
	(name = ZeString{m_series->name()}, state = int(m_state)),
	name << " internal error - invalid state=" << state, return 0);
  }

  m_state = Stopping;
  m_stopFn = ZuMv(fn);
  m_fn = Fn{};
  m_errorFn = ErrorFn{};
  m_series->run([this_ = ZmMkRef(node(this))]() mutable {
    this_->stopped();
  });

  return offset;
}
template <typename Decoder>
inline void Reader<Decoder>::goLive()
{
  using namespace RdrState;

  if (m_state == Reading) m_series->delHistReader(this);
  m_state = Live;
  m_series->addLiveReader(this);
}

template <typename Decoder>
inline void Reader<Decoder>::goHist()
{
  using namespace RdrState;

  if (m_state == Live) m_series->delLiveReader(this);
  m_state = Reading;
  m_series->addHistReader(this);
}

template <typename Decoder>
inline void Reader<Decoder>::loadBlk()
{
  using namespace RdrState;

  if (ZuUnlikely(m_failed)) return;

  switch (m_state) {
    case Stopping:
      return;
    case Stopped:
    case Reading:
    case Live:
      if (m_paused) return;
      break;
    default:
      ZiAssert(false, "Zdf",
	(name = ZeString{m_series->name()}, state = int(m_state)),
	name << " internal error - invalid state=" << state, goto fail);
  }

  ZiAssert(m_blk, "Zdf",
    (name = ZeString{m_series->name()}),
    name << " internal error - null blk", goto fail);

  if (m_blk->blkData) {
    m_series->run([
      this_ = ZmMkRef(node(this))
    ]() {
      this_->loaded(this_->m_blk);
    });
    return;
  }

  m_state = Loading;
  m_series->loadBlkData(m_blkOffset, [
    this_ = ZmMkRef(node(this))
  ](Blk *blk) mutable {
    this_->loaded(blk);
  });

#ifndef ZDEBUG
  return;
fail:
  m_failed = true;
#endif
}

template <typename Decoder>
inline void Reader<Decoder>::loaded(const Blk *blk)
{
  using namespace RdrState;

  if (ZuUnlikely(m_failed)) return;

  switch (m_state) {
    case Stopping:
      return;
    case Stopped:
      if (m_target.template is<void>()) return;
    case Loading:
    case Reading:
    case Live:
      break;
    default:
      ZiAssert(false, "Zdf",
	(name = ZeString{m_series->name()}, state = int(m_state)),
	name << " internal error - invalid state=" << state, goto fail);
  }

  ZiAssert(blk, "Zdf",
    (name = ZeString{m_series->name()}),
    name << " internal error - null blk", goto fail);
  ZiAssert(blk == m_blk, "Zdf",
    (name = ZeString{m_series->name()}),
    name << " internal error - inconsistent blk", goto fail);

  m_state = m_state == Live ? Live : Reading;
  m_blk->blkData->pin();
  m_decoder = m_blk->decoder<Decoder>();

  if (ZuUnlikely(!m_target.template is<void>())) {
    if (m_target.template is<Offset>()) {
      auto offset = m_blk->offset();
      auto targetOffset = m_target.template p<Offset>();
      m_target = {};
      if (targetOffset > offset)
	m_decoder.seek(targetOffset - offset);
    } else {
      auto value = m_target.template p<Value>();
      m_target = {};
      PValue pvalue;
      auto ndp = m_blk->ndp();
      if constexpr (ZuIsSame<Value, ZuFixed>{})
	pvalue = value.adjust(ndp);
      else
	pvalue = value;
      m_decoder.search([
	pvalue
      ](PValue skip, unsigned rle) -> unsigned {
	if (skip < pvalue) return rle;
	return 0;
      });
    }
  }

  nextValue();

#ifndef ZDEBUG
  return;
fail:
  m_failed = true;
#endif
}

template <typename Decoder>
inline bool Reader<Decoder>::nextBlk()
{
  using namespace RdrState;

  m_decoder = {};
  m_blk->blkData->unpin();
  m_blk = m_series->getBlk(++m_blkOffset);
  if (ZuLikely(m_blk->blkData)) {
    m_blk->blkData->pin();
    m_decoder = m_blk->decoder<Decoder>();
    return true;
  }
  m_series->run([this_ = ZmMkRef(node(this))]() mutable {
    this_->loadBlk();
  });
  return false;
}

template <typename Decoder>
inline void Reader<Decoder>::nextValue()
{
  using namespace RdrState;

  if (ZuUnlikely(m_failed)) return;

  switch (m_state) {
    case Stopping: // handled below
      break;
    case Reading:
    case Live:
      ZiAssert(m_blk, "Zdf",
	(name = ZeString{m_series->name()}),
	name << " internal error - null blk", goto fail);
      ZiAssert(m_blk->blkData, "Zdf",
	(name = ZeString{m_series->name()}),
	name << " internal error - null blkData", goto fail);
      ZiAssert(m_decoder, "Zdf",
	(name = ZeString{m_series->name()}),
	name << " internal error - null decoder", goto fail);
      break;
    default:
      ZiAssert(false, "Zdf",
	(name = ZeString{m_series->name()}, state = int(m_state)),
	name << " internal error - invalid state=" << state, goto fail);
  }

  if (m_state != Reading && m_state != Live) return;

  if (m_paused) return;

  {
    Ctrl ctrl{*this};
    bool cont = true;

again:
    do {
      typename Decoder::Value value;
      if (ZuUnlikely(!m_decoder.read(value))) break;

      if constexpr (ZuIsSame<Value, ZuFixed>{})
	cont = m_fn(ctrl, ZuFixed{value, ndp()});
      else
	cont = m_fn(ctrl, value);
    } while (cont);

    if (m_state != Reading && m_state != Live) return;

    if (m_paused) {
      if (m_state == Live) goHist();
      return;
    }

    if (!cont) return;

    if (!m_series->lastBlk(m_blk)) {
      if (nextBlk()) goto again;
      return;
    }

    if (m_state == Live) return;

    // need to go live
    if constexpr (ZuIsSame<Value, ZuFixed>{})
      m_fn(ctrl, ZuFixed{});
    else
      m_fn(ctrl, ZuFP<double>::nan());

    // the callback may have changed the state
    if (m_state != Reading) return;
    goLive();
  }

#ifndef ZDEBUG
  return;
fail:
  m_failed = true;
#endif
}

template <typename Decoder>
inline bool Reader<Decoder>::notifyValue(const uint8_t *end)
{
  using namespace RdrState;

  ZiAssert(!m_failed, "Zdf",
    (name = ZeString{m_series->name()}),
    name << " internal error - failed", return false);
  ZiAssert(!m_paused, "Zdf",
    (name = ZeString{m_series->name()}),
    name << " internal error - paused", return false);
  ZiAssert(m_state == Live, "Zdf",
    (name = ZeString{m_series->name()}, state = int(m_state)),
    name << " internal error - invalid state=" << state, return false);

  m_decoder.extend(end);

  Ctrl ctrl{*this};

  typename Decoder::Value value;
  if (ZuLikely(m_decoder.read(value))) {
    if constexpr (ZuIsSame<Value, ZuFixed>{})
      m_fn(ctrl, ZuFixed{value, ndp()});
    else
      m_fn(ctrl, value);
  }

  if (m_state != Live) return false;

  if (m_paused) {
    m_state = Reading;
    return false;
  }

  return true;
}

template <typename Decoder>
inline void Reader<Decoder>::stopped()
{
  using namespace RdrState;

  ZiAssert(m_state == Stopping, "Zdf",
    (name = ZeString{m_series->name()}, state = int(m_state)),
    name << " internal error - invalid state=" << state, return);

  m_state = Stopped;
  m_paused = false;
  auto stopFn = ZuMv(m_stopFn);
  m_stopFn = StopFn{};
  stopFn();
}

template <typename Decoder>
inline void Reader<Decoder>::fail()
{
  using namespace RdrState;

  m_failed = true;
  m_paused = true;

  if (m_state == Live) goHist();

  if (m_blk && m_blk->blkData) m_blk->blkData->unpin();

  auto errorFn = ZuMv(m_errorFn);
  m_fn = Fn{};
  m_errorFn = ErrorFn{};
  errorFn();
}

template <typename Decoder>
inline void Reader<Decoder>::Ctrl::purge()
{
  const_cast<Series *>(reader.series())->purge(&reader, reader.m_blkOffset);
}

// Writer implementation

template <typename Decoder, typename Heap, typename Value>
inline bool Writer__<Decoder, Heap, Value>::write(Value value)
{
  if (ZuUnlikely(m_failed)) return false;

  ZiAssert(m_series, "Zdf",
    (), "internal error - attempt to use closed Writer", return false);

  return m_series->write(static_cast<Writer<Decoder> *>(this), value);
}

template <typename Decoder, typename Heap, typename Value>
void Writer__<Decoder, Heap, Value>::stop(StopFn fn)
{
  if (ZuUnlikely(m_stopping)) { fn(); return; }
  m_stopping = true;
  m_stopFn = ZuMv(fn);
  if constexpr (ZuIsSame<Value, int64_t>{}) {
    NDP ndp = static_cast<Writer<Decoder> *>(this)->ndp();
    m_series->stop(m_encoder, ndp);
  } else {
    m_series->stop(m_encoder);
  }
}

template <typename Decoder, typename Heap, typename Value>
bool Writer__<Decoder, Heap, Value>::savedBlk() {
  if (ZuLikely(!m_stopping)) return false;
  auto fn = ZuMv(m_stopFn);
  fn();
  m_series = nullptr;
  return true;
}

} // namespace Zdf

#endif /* ZdfSeries_HH */
