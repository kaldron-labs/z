//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Data Frame backing data store

#ifndef ZdfStore_HH
#define ZdfStore_HH

#ifndef ZdfLib_HH
#include <zlib/ZdfLib.hh>
#endif

#include <zlib/ZtString.hh>

#include <zlib/Zdb.hh>

#include <zlib/ZdfTypes.hh>
#include <zlib/ZdfSchema.hh>
#include <zlib/ZdfCompress.hh>
#include <zlib/ZdfSeries.hh>
#include <zlib/Zdf.hh>

namespace Zdf {

// data store state
ZtEnumNS(StoreState, int8_t,
  Uninitialized, Initialized, Opening, Opened, OpenFailed);

using OpenFn = ZmFn<void(bool), ZmFnHeapID<"Zdf.Store.OpenFn">>;

class ZdfAPI Store {
public:
  Store() { }

  static void dbCf(
    const ZfCf::AnyNode *, ZdbCf &dbCf);	// inject tables into dbCf
  void init(Zdb *);
  void final();

  // convert shard to thread slot ID
  auto sid(Shard shard) const {
    return m_sids[shard & (m_sids.length() - 1)];
  }

  // dataframe threads (may be shared by app workloads)
  template <typename ...Args>
  void run(Shard shard, Args &&...args) const {
    m_mx->run(ZuFwd<Args>(args)..., sid(shard));
  }
  template <typename ...Args>
  void invoke(Shard shard, Args &&...args) const {
    m_mx->invoke(ZuFwd<Args>(args)..., sid(shard));
  }
  bool invoked(Shard shard) const { return m_mx->invoked(sid(shard)); }

  void open(OpenFn);	// establishes nextSeriesID
  void close();

  template <typename O, bool TimeIndex>
  using OpenDFFn = ZmFn<void(ZmRef<DataFrame<O, TimeIndex>>),
    ZmFnHeapID<"Zdf.Store.OpenDFFn">>;
  template <typename Series_>
  using OpenSeriesFn = ZmFn<void(ZmRef<Series_>),
    ZmFnHeapID<"Zdf.Store.OpenSeriesFn">>;

  // open data frame
  template <typename O, bool TimeIndex, bool Create>
  void openDF(
    Shard shard, IDString name,
    OpenDFFn<O, TimeIndex> fn)
  {
    using DataFrame = Zdf::DataFrame<O, TimeIndex>;
    using DFRef = ZmRef<DataFrame>;
    using W = WrapType<O, TimeIndex>;
    using Fields = Zdf::Fields<W>;
    using SeriesRefs = Zdf::SeriesRefs<W>;

    ZuLambda{[
      this, shard, name = ZuMv(name), fn = ZuMv(fn),
      seriesRefs = SeriesRefs{}
    ](auto &&self, auto I, auto series) mutable {
      if constexpr (I >= 0) {
	if (ZuUnlikely(!series)) { fn(DFRef{}); return; }
	seriesRefs.template p<I>() = ZuMv(series);
      }
      enum { J = I + 1 };
      if constexpr (J >= SeriesRefs::N) {
	fn(DFRef{new DataFrame{this, shard, ZuMv(name), ZuMv(seriesRefs)}});
      } else {
	using Field = ZuType<J, Fields>;
	auto next = [self = ZuMv(self)](auto series) mutable {
	  ZuMv(self).operator()(ZuInt<J>{}, ZuMv(series));
	};
	ZuCSpan id(Field::id());
	if (name.length() + id.length() + 1 > IDSize_) {
	  ZiLOG(Fatal, "ZdfStore",
	    ([name = ZeString(name), id = ZeString(id)](auto &s) {
	      s << "Zdf series name \"" << name << '/' << id
		<< "\" too long (>" << IDSize_ << " bytes)";
	    }));
	  fn(nullptr);
	  return;
	}
	IDString seriesName;
	seriesName << name << '/' << id;
	if constexpr (Field::Type::Code == ZfFieldTC::Time) {
	  ZuTime epoch = Field::deflt();
	  if (!*epoch) epoch = DefltEpoch();
	  openTimeSeries<Create>(shard, ZuMv(seriesName), epoch, ZuMv(next));
	} else {
	  using Decoder = FieldDecoder<Field>;
	  openSeries<Decoder, Create>(shard, ZuMv(seriesName), ZuMv(next));
	}
      }
    }}(ZuInt<-1>{}, static_cast<void *>(nullptr));
  }

private:
  // open series
  template <typename Series_, bool Create>
  void openSeries_(
    Shard shard, IDString name,
    ZuTime epoch,
    OpenSeriesFn<Series_> fn)
  {
    using Series = Series_;
    using DBSeries = typename Series::DBSeries;
    enum { Fixed = Series::Fixed };

    static auto seriesTbl = [](const Store *this_) {
      if constexpr (Fixed)
	return this_->m_seriesFixedTbl;
      else
	return this_->m_seriesFloatTbl;
    };

    run(shard, [
      this, shard, name = ZuMv(name), epoch, fn = ZuMv(fn)
    ]() mutable {
      auto findFn = [
	this, shard, name, epoch, fn = ZuMv(fn)
      ](ZdbObjRef<DBSeries> dbSeries) mutable {
	if (dbSeries) {
	  ZmRef<Series> series = new Series{this, ZuMv(dbSeries)};
	  series->open(ZuMv(fn));
	  return;
	}
	if (!Create) { fn(nullptr); return; }
	dbSeries = new ZdbObject<DBSeries>{seriesTbl(this), shard};
	new (dbSeries->ptr_()) DBSeries{
	  .id = m_nextSeriesID++,
	  .name = ZuMv(name),
	  .epoch = epoch,
	  .blkOffset = 0
	};
	auto insertFn = [
	  this, fn = ZuMv(fn)
	](ZdbObjRef<DBSeries> dbSeries) mutable {
	  if (!dbSeries) { fn(nullptr); return; }
	  dbSeries->commit();
	  ZmRef<Series> series = new Series{this, ZuMv(dbSeries)};
	  series->open(ZuMv(fn));
	};
	seriesTbl(this)->insert(dbSeries, ZuMv(insertFn));
      };
      seriesTbl(this)->template find<1>(shard, ZuMvTuple(name), ZuMv(findFn));
    });
  }
public:
  template <typename Decoder, bool Create>
  void openSeries(
    Shard shard, IDString name,
    OpenSeriesFn<Series<Decoder>> fn)
  {
    openSeries_<Series<Decoder>, Create>(
      shard, ZuMv(name), ZuTime{0}, ZuMv(fn));
  }
  template <bool Create>
  void openTimeSeries(
    Shard shard, IDString name, ZuTime epoch,
    OpenSeriesFn<TimeSeries> fn)
  {
    openSeries_<TimeSeries, Create>(
      shard, ZuMv(name), epoch, ZuMv(fn));
  }

  ZdbTable<DB::SeriesFixed> *seriesFixedTbl() const { return m_seriesFixedTbl; }
  ZdbTable<DB::SeriesFloat> *seriesFloatTbl() const { return m_seriesFloatTbl; }
  ZdbTable<DB::BlkFixed> *blkFixedTbl() const { return m_blkFixedTbl; }
  ZdbTable<DB::BlkFloat> *blkFloatTbl() const { return m_blkFloatTbl; }
  ZdbTable<DB::BlkData> *blkDataTbl() const { return m_blkDataTbl; }

private:
  void open_recoverNextSeriesID_Fixed();
  void open_recoverNextSeriesID_Float();
  void opened(bool ok);

private:
  ZiMultiplex			*m_mx = nullptr;
  StoreState::T			m_state = StoreState::Uninitialized;
  ZdbTblRef<DB::SeriesFixed>	m_seriesFixedTbl;
  ZdbTblRef<DB::SeriesFloat>	m_seriesFloatTbl;
  ZdbTblRef<DB::BlkFixed>	m_blkFixedTbl;
  ZdbTblRef<DB::BlkFloat>	m_blkFloatTbl;
  ZdbTblRef<DB::BlkData>	m_blkDataTbl;
  ZdbTableCf::SIDArray		m_sids;
  ZmAtomic<uint32_t>		m_nextSeriesID = 1;
  OpenFn			m_openFn;
};

template <typename O, bool TimeIndex>
template <typename ...Args>
inline void DataFrame<O, TimeIndex>::run(Args &&...args) const
{
  m_store->run(m_shard, ZuFwd<Args>(args)...);
}
template <typename O, bool TimeIndex>
template <typename ...Args>
inline void DataFrame<O, TimeIndex>::invoke(Args &&...args) const
{
  m_store->invoke(m_shard, ZuFwd<Args>(args)...);
}
template <typename O, bool TimeIndex>
inline bool DataFrame<O, TimeIndex>::invoked() const
{
  return m_store->invoked(m_shard);
}

template <typename Decoder>
template <typename ...Args>
inline void Series<Decoder>::run(Args &&...args) const
{
  m_store->run(m_shard, ZuFwd<Args>(args)...);
}
template <typename Decoder>
template <typename ...Args>
inline void Series<Decoder>::invoke(Args &&...args) const
{
  m_store->invoke(m_shard, ZuFwd<Args>(args)...);
}
template <typename Decoder>
inline bool Series<Decoder>::invoked() const
{
  return m_store->invoked(m_shard);
}

template <typename Decoder>
ZdbTable<DB::BlkData> *Series<Decoder>::blkDataTbl() const
{
  return m_store->blkDataTbl();
}
template <typename Decoder>
auto Series<Decoder>::seriesTbl() const ->
ZuIf<Fixed, ZdbTable<DB::SeriesFixed> *, ZdbTable<DB::SeriesFloat> *>
{
  if constexpr (Fixed)
    return m_store->seriesFixedTbl();
  else
    return m_store->seriesFloatTbl();
}
template <typename Decoder>
auto Series<Decoder>::blkTbl() const ->
ZuIf<Fixed, ZdbTable<DB::BlkFixed> *, ZdbTable<DB::BlkFloat> *>
{
  if constexpr (Fixed)
    return m_store->blkFixedTbl();
  else
    return m_store->blkFloatTbl();
}

} // namespace Zdf

#endif /* ZdfStore_HH */
