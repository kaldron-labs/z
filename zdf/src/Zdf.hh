//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Data Frame

// Data d;
// ...
// DataFrame df{Data::fields(), "d"};
// ...
// auto w = df.writer();
// ZuTime time{ZuTime::Now};
// w.write(&d);
// ...
// AnyReader index, reader;
// ...
// df.find(index, 0, df.nsecs(time));	// index time to offset // FIXME
// df.seek(reader, N, index.offset());	// seek reader to offset
// ...
// ZuFixed nsecs, value;
// index.read(nsecs);
// ZuTime then = df.time(nsecs);
// reader.read(value);

#ifndef Zdf_HH
#define Zdf_HH

#ifndef ZdfLib_HH
#include <zlib/ZdfLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZdfTypes.hh>
#include <zlib/ZdfSchema.hh>
#include <zlib/ZdfCompress.hh>
#include <zlib/ZdfSeries.hh>

namespace Zdf {

// wrapper for type together with time index flag
template <typename O_, bool TimeIndex_>
struct WrapType {
  using O = O_;
  enum { TimeIndex = TimeIndex_ };
};

// synthetic field returning time now, used if TimeIndex is set
using TimeType = ZfFieldType_Time<ZuTime, ZuTypeList<ZuFieldProp::NDP<9>>>;
template <typename O_>
struct TimeField_ {
  using O = O_;
  using T = ZuTime;
  using Props = ZuTypeList<
    ZuFieldProp::NDP<9>,
    ZuFieldProp::Synthetic,
    ZuFieldProp::Series>;
  enum { ReadOnly = 1 };
  static constexpr const auto &id() { return "_time"; }
  ZuTime get(const O &) { return Zm::now(); }
  template <typename P> static void set(O &, P &&) { }
};
template <typename O>
using TimeField = ZfField_Time<TimeField_<O>>;

// Zdf data-frames are comprised of series fields
template <typename Field>
using FieldFilter = ZuTypeIn<ZuFieldProp::Series, typename Field::Props>;
template <typename O, bool TimeIndex>
struct Fields_ {
  using T = ZuTypeGrep<FieldFilter, ZuFields<O>>;
};
// if the data-frame is time-indexed, unshift a TimeField onto the field list
template <typename O>
struct Fields_<O, true> {
  using T = Fields_<O, false>::T::template Unshift<TimeField<O>>;
};
template <typename W>
using Fields = typename Fields_<typename W::O, W::TimeIndex>::T;

// map a field to its Decoder type
template <typename Field, typename Props = typename Field::Props>
using FieldDecoderFlags_ = ZuUnsigned<
  ((Field::Type::Code == ZfFieldTC::Float) ? 4 : 0) |
  (ZuTypeIn<ZuFieldProp::Delta2, Props>{} ? 2 : 0) |
  (ZuTypeIn<ZuFieldProp::Delta, Props>{} ? 1 : 0)>;
template <typename Field, unsigned = FieldDecoderFlags_<Field>{}>
struct FieldDecoder_;
template <typename Field>
struct FieldDecoder_<Field, 0U> { using T = Decoder; };
template <typename Field>
struct FieldDecoder_<Field, 1U> { using T = DeltaDecoder<>; };
template <typename Field>
struct FieldDecoder_<Field, 2U> { using T = DeltaDecoder<DeltaDecoder<>>; };
template <typename Field>
struct FieldDecoder_<Field, 4U> { using T = FloatDecoder; };
template <typename Field>
using FieldDecoder = typename FieldDecoder_<Field>::T;

// map a field to corresponding Series / Reader
template <typename Field, bool = Field::Type::Code == ZfFieldTC::Time>
struct FieldSeries_ { using T = Series<FieldDecoder<Field>>; };
template <typename Field>
struct FieldSeries_<Field, true> { using T = TimeSeries; };
template <typename Field> using FieldSeries = typename FieldSeries_<Field>::T;
template <typename Field> using FieldSeriesRef = ZmRef<FieldSeries<Field>>;
template <typename Field> using FieldReader = Reader<FieldDecoder<Field>>;
template <typename Field>
using FieldRdrCtrl = typename FieldReader<Field>::Ctrl;

// move-only ZmRef<Writer>-derived smart pointer,
// with a RAII dtor that calls writer->stop()
template <typename Decoder>
class WrRef : public ZmRef<Writer<Decoder>> {
  WrRef(const WrRef &) = delete;
  WrRef &operator =(const WrRef &) = delete;

public:
  using Writer = Zdf::Writer<Decoder>;
  using Ref = ZmRef<Writer>;

  using Ref::operator Writer *;
  using Ref::operator ->;

  WrRef() = default;
  WrRef(WrRef &&h) : Ref{ZuMv(h)} { }
  WrRef &operator =(WrRef &&h) {
    stop_();
    Ref::operator =(ZuMv(h));
    return *this;
  }
  template <typename Arg>
  WrRef(Arg &&arg) : Ref{ZuFwd<Arg>(arg)} { }
  template <typename Arg>
  WrRef &operator =(Arg &&arg) {
    stop_();
    Ref::operator =(ZuFwd<Arg>(arg));
    return *this;
  }
  ~WrRef() { stop_(); }

private:
  void stop_() {
    if (auto ptr = this->ptr_()) ptr->stop();
  }
};

// map a field to corresponding WrRef
template <typename Field> using FieldWrRef = WrRef<FieldDecoder<Field>>;

// tuples of series and writer references given object type wrapper
template <typename W>
using SeriesRefs = ZuTypeApply<ZuTuple, ZuTypeMap<FieldSeriesRef, Fields<W>>>;
template <typename W>
using WrRefTuple = ZuTypeApply<ZuTuple, ZuTypeMap<FieldWrRef, Fields<W>>>;

template <typename O, bool TimeIndex> class DataFrame;

template <
  typename Field,
  typename Props = typename Field::Props,
  bool HasNDP = ZuFieldProp::HasNDP<Props>{}>
struct GetNDP_ {
  using T = ZuFieldProp::GetNDP<Props>;
};
template <typename Field, typename Props>
struct GetNDP_<Field, Props, false> {
  using T = ZuUnsigned<0>;
};
template <typename Field>
using GetNDP = typename GetNDP_<Field>::T;

// data frame writer
template <typename W, typename Heap>
class DFWriter_ : public Heap, public ZmObject {
public:
  using O = typename W::O;
  enum { TimeIndex = W::TimeIndex };
  using DataFrame = Zdf::DataFrame<O, TimeIndex>;

protected:
friend DataFrame;

  using Fields = Zdf::Fields<W>;
  using WrRefs = WrRefTuple<W>;

  DFWriter_() = default;
  DFWriter_(DataFrame *df, ErrorFn errorFn) :
    m_df{df}, m_errorFn{ZuMv(errorFn)} { }

public:
  DFWriter_(DFWriter_ &&) = default;
  DFWriter_ &operator =(DFWriter_ &&) = default;
  ~DFWriter_() = default;

  DFWriter_(const DFWriter_ &) = delete;
  DFWriter_ &operator =(const DFWriter_ &) = delete;

  bool stopped() const { return m_stopped; }
  bool failed() const { return m_failed; }

private:
  template <unsigned I, typename WrRef>
  void writer(WrRef wrRef) {
    m_wrRefs.template p<I>(ZuMv(wrRef));
  }

public:
  void write(const O &o) {
    using namespace ZfFieldTC;

    if (m_stopped) return;

    bool ok = true;
    ZuUnroll::all<WrRefs::N>([this, &o, &ok](auto I) {
      if (!ok) return;
      using Field = ZuType<I, Fields>;
      if constexpr (Field::Code == Float)
	ok = ok && m_wrRefs.template p<I>()->write(Field::get(o));
      else if constexpr (Field::Code == Fixed)
	ok = ok && m_wrRefs.template p<I>()->write(
	  Field::get(o).adjust(GetNDP<Field>{}));
      else if constexpr (
	  Field::Code == Int8 ||
	  Field::Code == UInt8 ||
	  Field::Code == Int16 ||
	  Field::Code == UInt16 ||
	  Field::Code == Int32 ||
	  Field::Code == UInt32 ||
	  Field::Code == Int64 ||
	  Field::Code == UInt64)
	ok = ok && m_wrRefs.template p<I>()->write(Field::get(o));
      else if constexpr (Field::Code == Decimal)
	ok = ok && m_wrRefs.template p<I>()->write(
	  ZuFixed{Field::get(o), GetNDP<Field>{}});
      else if constexpr (Field::Code == Time)
	ok = ok && m_wrRefs.template p<I>()->write(
	  m_df->template series<Field>()->nsecs(Field::get(o)));
    });
    if (!ok) fail();
  }

public:
  void stop() {
    if (ZuUnlikely(m_stopped)) return;
    m_stopped = true;
    m_wrRefs = {}; // causes individual Writer::stop()
    m_errorFn = ErrorFn{};
  }

  void fail() {
    if (ZuUnlikely(m_failed)) return;
    m_failed = m_stopped = true;
    m_wrRefs = {};
    ErrorFn errorFn = ZuMv(m_errorFn);
    errorFn();
  }

private:
  DataFrame	*m_df = nullptr;
  ErrorFn	m_errorFn;
  WrRefs	m_wrRefs;
  bool		m_stopped = false;
  bool		m_failed = false;
};
template <typename W>
ZuDerive(DFWriter,
  (DFWriter_<W, ZmHeap<"Zdb.DFWriter", DFWriter_<W, ZuVoid>>>));

class Store;

template <typename O_, bool TimeIndex_ = false>
class DataFrame : public ZmObject {
public:
  using O = O_;
  enum { TimeIndex = TimeIndex_ };
private:
  using W = WrapType<O, TimeIndex>;
public:
  using Writer = DFWriter<W>;
  using WriteFn = ZmFn<void(ZmRef<Writer>),
    ZmFnHeapID<"Zdf.DataFrame.WriteFn">>;

private:
  friend Store;

  using Fields = Zdf::Fields<W>;
  using SeriesRefs = Zdf::SeriesRefs<W>;
  using WrRefs = WrRefTuple<W>;

  DataFrame(Store *store, Shard shard, IDString name, SeriesRefs seriesRefs) :
    m_store{store}, m_shard{shard}, m_name{ZuMv(name)},
    m_seriesRefs{ZuMv(seriesRefs)} { }

public:
  ~DataFrame() = default;

  Shard shard() const { return m_shard; }
  const IDString &name() const { return m_name; }

  // run/invoke on shard
  template <typename ...Args> void run(Args &&...args) const;
  template <typename ...Args> void invoke(Args &&...args) const;
  bool invoked() const;

  Offset count() const {
    return m_seriesRefs.template p<0>()->count();
  }

  template <typename Field>
  auto series() const {
    using I = ZuTypeIndex<Field, Fields>;
    return m_seriesRefs.template p<I{}>();
  }

  struct WriteContext {
    WrRefs	writers;
  };

  void write(WriteFn fn, ErrorFn errorFn) {
    ZmRef<Writer> dfw = new Writer{this, ZuMv(errorFn)};
    ZuLambda{[
      this, fn = ZuMv(fn), dfw = ZuMv(dfw)
    ](auto &&self, auto I, auto wrRef) mutable {
      if constexpr (I >= 0)
	dfw->template writer<I>(ZuMv(wrRef));
      enum { J = I + 1 };
      if constexpr (J >= SeriesRefs::N) {
	fn(ZuMv(dfw));
      } else {
	using Field = ZuType<J, Fields>;
	auto next = [self = ZuMv(self)](auto wrRef) mutable {
	  ZuMv(self).template operator()(ZuInt<J>{}, ZuMv(wrRef));
	};
	auto error = [dfw = dfw.ptr()]() { dfw->fail(); };
	if constexpr (Field::Code == ZfFieldTC::Float)
	  m_seriesRefs.template p<J>()->write(ZuMv(next), ZuMv(error));
	else
	  m_seriesRefs.template p<J>()->write(
	    ZuMv(next), ZuMv(error), GetNDP<Field>{});
      }
    }}(ZuInt<-1>{}, static_cast<void *>(nullptr));
  }

  template <typename Field>
  void seek(
    Offset offset,
    typename FieldReader<Field>::Fn readFn, ErrorFn errorFn = {}) const
  {
    using I = ZuTypeIndex<Field, Fields>;
    m_seriesRefs.template p<I{}>()->seek(offset, ZuMv(readFn), ZuMv(errorFn));
  }
  template <typename Field>
  void find(
    typename FieldSeries<Field>::Value value,
    typename FieldReader<Field>::Fn readFn, ErrorFn errorFn = {}) const
  {
    using I = ZuTypeIndex<Field, Fields>;
    m_seriesRefs.template p<I{}>()->find(value, ZuMv(readFn), ZuMv(errorFn));
  }

  void stopWriting(StopFn fn = {}) {
    ZuLambda{[this, fn = ZuMv(fn)](auto &&self, auto I) mutable {
      if constexpr (I >= SeriesRefs::N) {
	fn();
      } else {
	enum { J = I + 1 };
	auto next = [self = ZuMv(self)]() mutable {
	  ZuMv(self).template operator()(ZuUnsigned<J>{});
	};
	m_seriesRefs.template p<I>()->stopWriting(next);
      }
    }}(ZuUnsigned<0>{});
  }

  void stopReading() {
    ZuUnroll::all<SeriesRefs::N>([this](auto I) {
      m_seriesRefs.template p<I>()->stopReading();
    });
  }

private:
  Store		*m_store = nullptr;
  Shard		m_shard;
  IDString	m_name;
  SeriesRefs	m_seriesRefs;
};

} // namespace Zdf

#endif /* Zdf_HH */
