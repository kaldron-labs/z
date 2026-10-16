//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z Database sagas

#ifndef ZdbSaga_HH
#define ZdbSaga_HH

#ifndef ZdbLib_HH
#include <zlib/ZdbLib.hh>
#endif

#include <string.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuInt.hh>
#include <zlib/ZuPP.hh>
#include <zlib/ZuSeq.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbReflect.hh>

#include <zlib/ZdbBuf.hh>
#include <zlib/ZdbTypes.hh>

#ifndef ZdbSaga_BuiltinSize
// Initial inline payload allocation, not a payload limit. 256 bytes covers
// ordinary saga state while ZtBuiltin retains its tagged heap fallback.
#define ZdbSaga_BuiltinSize 256
#endif

namespace Zdb_ {

class DB;
class AnySaga__;
struct SagaDriver;
class SagaDB;
struct SagaRec;
struct SagaUNHash;
class AnyTable;
class Host;
class StoreTbl;
template <typename Impl_> class Table_;
template <typename T_, typename Impl_ = void> class Table;
template <typename T> struct Row;

using SagaID = uint128_t;

ZuDerive(SagaType,
  (ZtString<ZtStringHeapID<"Zdb.Saga.Type">>));

ZuDerive(SagaPayload,
  (ZtBuiltin<
    ZtArray<uint8_t, ZtArrayHeapID<"Zdb.Saga.Data">>,
    ZdbSaga_BuiltinSize>));

ZuDerive(SagaKey, (ZuTuple<ZuCSpan, SagaID>));
ZuDerive(SagaCursor, (ZuTuple<SagaType, SagaID>));
ZuDerive(SagaShards,
  (ZtArray<Shard, ZtArrayHeapID<"Zdb.Saga.Shards">>));

struct SagaData {
  SagaType	type;
  SagaID	id;
  Shard		shard;
  SagaPayload	data;
  ZuTime	deadline;

  friend ZuStringT<"Zdb.Saga"> ZdbHeapID(SagaData *);
  friend ZuStringT<"Zdb.Saga.Buf"> ZdbBufHeapID(SagaData *);
};

ZfStruct(ZdbAPI, (SagaData, FB),
  (type,	(Ctor<0>, Keys<0>),	String),
  (id,		(Ctor<1>, Keys<0>),	UInt128),
  (shard,	(Ctor<2>),		UInt8),
  (data,	(Ctor<3>),		Bytes),
  (deadline,	(Ctor<4>),		Time));

Zfb::AsTable<"Zdb_.fbs.SagaData"> Zfb_Fmt(SagaData *);

struct SagaStep {
  SagaType	type;
  SagaID	id;
  uint32_t	step;
  Shard		shard;
  UN		un;

  friend ZuStringT<"Zdb.Saga.Step"> ZdbHeapID(SagaStep *);
  friend ZuStringT<"Zdb.Saga.Step.Buf"> ZdbBufHeapID(SagaStep *);
};

ZfStruct(ZdbAPI, (SagaStep, FB),
  (type,	(Ctor<0>, Keys<0>),	String),
  (id,		(Ctor<1>, Keys<0>),	UInt128),
  (step,	(Ctor<2>, Keys<0>),	UInt32),
  (shard,	(Ctor<3>),		UInt8),
  (un,		(Ctor<4>),		UInt64));

Zfb::AsTable<"Zdb_.fbs.SagaStep"> Zfb_Fmt(SagaStep *);

ZtEnumNS(ZdbAPI, SagaOp, int8_t,
  Invalid,
  Insert,
  Update,
  Delete);
namespace SagaOp { Zfb::AsEnum<"Zdb_.fbs.SagaOp"> Zfb_Fmt(Map *); }

struct SagaTypeStep {
  SagaType	type;
  IDString	table;
  uint32_t	step;
  SagaOp::T	op;
  bool		repeat = false;
};

ZfStruct(ZdbAPI, (SagaTypeStep, FB),
  (type,	(Ctor<0>, Keys<0>),		String),
  (step,	(Ctor<2>, Keys<0>),		UInt32),
  (table,	(Ctor<1>),			String),
  (op,		(Ctor<3>, Enum<SagaOp::Map>),	Int8),
  (repeat,	(Ctor<4>),			Bool));

Zfb::AsTable<"Zdb_.fbs.SagaTypeStep"> Zfb_Fmt(SagaTypeStep *);

ZuDerive(SagaRecoveryFn,
  (ZmFn<void(), ZmFnHeapID<"Zdb.Saga.RecoveryFn">>));
ZuDerive(SagaCompleteFn,
  (ZmFn<void(bool), ZmFnHeapID<"Zdb.Saga.CompleteFn">>));

struct SagaNoop {
  void operator ()(bool) { }
};

template <typename Fn, typename = void>
struct SagaCallbackValid : public ZuFalse { };
template <typename Fn>
struct SagaCallbackValid<Fn, decltype(
  ZuDeclVal<ZuDecay<Fn> &>()(bool{}), void())> :
  public ZuIsConstructible<Fn, ZuDecay<Fn>> { };

template <typename DB> struct AnySaga;
struct SagaDef;
template <typename Def> struct SagaLayout;
template <typename KeyIDs, typename Impl, typename Call>
struct SagaFindUpdate;
template <typename Impl, typename Call>
struct SagaFindDelete;

namespace SagaState {
  using T = int8_t;
  enum {
    Inactive = 0,
    Rebuilding,
    Active
  };
}

class AnySaga__ : public ZmPolymorph {
friend DB;
friend SagaDriver;
friend class SagaDB;
template <typename, typename, typename> friend struct SagaFindUpdate;
template <typename, typename> friend struct SagaFindDelete;

public:
  DB *db() const { return m_db; }
  virtual ZuCSpan type() const = 0;
  const SagaKey &key() const { return m_key; }
  SagaID id() const { return m_key.template p<1>(); }
  Shard shard() const { return m_shard; }
  uint32_t step() const { return m_step; }
  uint32_t iteration() const { return m_iteration; }
  uint64_t epoch() const { return m_epoch; }
  ZuTime deadline() const { return m_deadline; }
  const SagaShards &shards() const { return m_shards; }
  Shard stepShard(unsigned i) const { return m_shards[i]; }
  void stepShard(unsigned i, Shard v) { m_shards[i] = v; }

protected:
  AnySaga__() = default;
  void iteration(uint32_t v) { m_iteration = v; }

private:
  void init_(
      DB *db, SagaKey key, Shard shard, uint64_t epoch,
      unsigned stepCount, ZuTime deadline) {
    m_db = db;
    m_key = ZuMv(key);
    m_shards.length(stepCount, false);
    memset(m_shards.data(), 0xff, stepCount * sizeof(Shard));
    m_epoch = epoch;
    m_shard = shard;
    m_step = 0;
    m_iteration = 0;
    m_deadline = deadline;
    m_fwd = true;
    m_rec = nullptr;
  }

  void step(uint32_t v) { m_step = v; }
  void stepInc() { ++m_step; }
  void stepDec() { --m_step; }
  bool fwd() const { return m_fwd; }
  void fwd(bool v) { m_fwd = v; }
  SagaRec *rec() const { return m_rec; }
  void rec(SagaRec *v) { m_rec = v; }

public:
  virtual unsigned nPhases() const = 0;
  virtual bool phaseDef(unsigned, SagaTypeStep &) const = 0;
  virtual int save(SagaPayload &) const = 0;
  virtual unsigned nSteps() const = 0;
  virtual bool stepDef(unsigned, ZuCSpan &, SagaOp::T &) const = 0;
  virtual void run(SagaDriver) = 0;

  template <typename Complete>
  void skip(Complete &&);
  template <typename T, typename Impl, typename Complete, typename L>
  void insert(Impl *, ZmRef<Row<T>>, Complete &&, L &&);
  template <typename KeyIDs_ = ZuSeq<>, typename T, typename Impl,
    typename Complete, typename L>
  void update(Impl *, ZmRef<Row<T>>, Complete &&, L &&);
  template <unsigned KeyID, typename KeyIDs_ = ZuSeq<>, typename Impl,
    typename Complete, typename L>
  void findUpd(Impl *, Shard,
    Impl::template Key<KeyID>, Complete &&, L &&);
  template <typename T, typename Impl, typename Complete, typename L>
  void del(Impl *, ZmRef<Row<T>>, Complete &&, L &&);
  template <unsigned KeyID, typename Impl, typename Complete, typename L>
  void findDel(
    Impl *, Shard, Impl::template Key<KeyID>,
    Complete &&, L &&);

protected:
  ZdbAPI void result_(OpResult::T, Shard);

private:
  ZdbAPI void stepRecovered_(Shard);
  ZdbAPI bool prepare_(AnyTable *, Shard, SagaOp::T, bool, UN &, bool &);
  ZdbAPI void intent_(Shard, UN);
  ZdbAPI void replay_(OpResult::T, Shard);

  template <typename T, typename Impl, typename Call>
  void insert_(Impl *, Shard, ZmRef<Row<T>>, UN, bool, Call);
  template <typename KeyIDs_, typename T, typename Impl, typename Call>
  void update_(Impl *, Shard, ZmRef<Row<T>>, UN, bool, Call);
  template <unsigned KeyID, typename KeyIDs_, typename Impl, typename Call>
  void findUpd_(Impl *, Shard, Impl::template Key<KeyID>, Call);
  template <typename T, typename Impl, typename Call>
  void del_(Impl *, Shard, ZmRef<Row<T>>, UN, bool, Call);
  template <unsigned KeyID, typename Impl, typename Call>
  void findDel_(Impl *, Shard, Impl::template Key<KeyID>, Call);

  SagaShards	m_shards; // indexed by expanded step
  DB		*m_db = nullptr;
  SagaKey	m_key;
  uint64_t	m_epoch = 0;
  uint32_t	m_step = 0;
  uint32_t	m_iteration = 0;
  Shard		m_shard = 0;
  ZuTime	m_deadline;
  bool		m_fwd = true;
  SagaRec	*m_rec = nullptr;	// staged recovery step
};

template <typename Complete, typename L>
struct SagaInsert {
  Complete	complete;
  L		fn;

  void operator ()(decltype(nullptr)) { complete(false); }
  template <typename O> void operator ()(O row) {
    if (ZuUnlikely(!row)) { complete(false); return; }
    fn(row, ZuMv(complete));
  }
};

template <typename Complete, typename L>
struct SagaUpdate {
  Complete	complete;
  L		fn;

  void operator ()(decltype(nullptr)) { complete(false); }
  template <typename O> void operator ()(O row) {
    if (ZuUnlikely(!row)) { complete(false); return; }
    fn(row, ZuMv(complete));
  }
};

template <typename Complete, typename L>
struct SagaDelete {
  Complete	complete;
  L		fn;

  void operator ()(decltype(nullptr)) { complete(true); }
  template <typename O> void operator ()(O row) {
    if (ZuUnlikely(!row)) { complete(true); return; }
    fn(row, ZuMv(complete));
  }
};

ZmListDerive(SagaList, AnySaga__,
  ZmListNode<AnySaga__, ZmListShadow<>>);

inline const SagaKey &SagaKeyAxor(const AnySaga__ &saga) {
  return saga.key();
}

ZmHashDerive(SagaHash, SagaList::Node,
  (ZmHashNode<SagaList::Node,
    ZmHashKey<SagaKeyAxor, ZmHashHeapID<"">>>));

using AnySaga_ = SagaHash::Node;

ZuDerive(SagaRunFn,
  (ZmFn<void(ZmRef<AnySaga_>), ZmFnHeapID<"Zdb.Saga.RunFn">>));

template <typename DB_>
struct AnySaga : public AnySaga_ {
  using DB = DB_;

  auto db() const { return static_cast<DB *>(AnySaga_::db()); }
};

struct SagaDef {
  typedef bool (*PhaseDefFn)(unsigned, SagaTypeStep &);
  typedef ZmRef<AnySaga_> (*LoadFn)(ZuBSpan);

  ZuCSpan	type;
  unsigned	nPhases;
  PhaseDefFn	phaseDef;
  LoadFn	load;
};

ZuDerive(SagaDefs,
  (ZtArray<SagaDef, ZtArrayHeapID<"Zdb.SagaDefs">>));

ZmLHashKVDerive(SagaDefHash_, ZuCSpan, unsigned, ZmLHashLocal<>);
struct SagaDefHashT : public ZmNHeap<SagaDefHashT>, public ZmPolymorph,
    public SagaDefHash_ {
friend inline ZuStringT<"Zdb.Saga.Def.Hash"> ZmNHeapID(SagaDefHashT *);

  SagaDefHashT(unsigned n) : SagaDefHash_{ZmHashParams{n}} { }
};
using SagaDefHash = SagaDefHashT;

template <typename KeyIDs, typename Impl, typename Call>
struct SagaFindUpdate {
  AnySaga__	*saga;
  Impl		*table;
  Shard		shard;
  UN		un;
  bool		saved;
  Call		call;

  void operator ()(ZmRef<Row<typename Impl::T>>);
};

template <typename Impl, typename Call>
struct SagaFindDelete {
  AnySaga__	*saga;
  Impl		*table;
  Shard		shard;
  UN		un;
  bool		saved;
  Call		call;

  void operator ()(ZmRef<Row<typename Impl::T>>);
};

struct SagaDriver {
  ZmRef<AnySaga_> saga;
  uint64_t epoch = 0;
  uint32_t step = 0;
  bool fwd = true;
  SagaCompleteFn complete;

  ZdbAPI void operator ()(bool);
  ZdbAPI static void run_(DB *, ZmRef<AnySaga_>, SagaCompleteFn);

private:
  void finish(bool);
  static void del_(DB *, ZmRef<AnySaga_>, SagaCompleteFn, unsigned, bool);
  static void finish_(DB *, ZmRef<AnySaga_>, SagaCompleteFn, bool);
  static void cleanup_(DB *, ZmRef<AnySaga_>, SagaCompleteFn, unsigned);
  static void abandon_(DB *, ZmRef<AnySaga_>);
  static void end_(DB *, AnySaga_ *);
  static void terminal_(DB *, ZmRef<AnySaga_>, SagaCompleteFn, bool);
};

struct SagaHashObj : public ZmNHeap<SagaHashObj>, public SagaHash {
friend inline ZuStringT<"Zdb.Saga.Hash"> ZmNHeapID(SagaHashObj *);

  ZuDerive_(SagaHashObj, SagaHash)
};

using SagaTypeSeen = ZtArray<uint8_t, ZtArrayHeapID<"Zdb.Saga.Type.Seen">>;

struct SagaCatalog__ {
  StoreTbl	*table = nullptr;
  SagaTypeSeen	seen;
  SchemaBuf	schema = SchemaBuf(Zfb::SchemaOut::Schema<SagaTypeStep>::Size);
  SagaType	type;
  ZeException	error;
  UN		nextUN = 0;
  uint64_t	step = 0;
  int		typeIndex = -1;
  unsigned	pageCount = 0;
  unsigned	writeType = 0;
  unsigned	writeStep = 0;

  SagaCatalog__() { Zfb::saveSchema<SagaTypeStep>(schema); }

  ZdbAPI void load(const SagaDefs &, const SagaDefHash_ &, SagaTypeStep);
  ZdbAPI void end(unsigned);
};
struct SagaCatalog : public ZmNHeap<SagaCatalog>, public ZmPolymorph, public SagaCatalog__ {
friend inline ZuStringT<"Zdb.Saga.Type"> ZmNHeapID(SagaCatalog *);

  ZuDerive_(SagaCatalog, SagaCatalog__)
};

using SagaStepKey = ZuTuple<ZuCSpan, SagaID, uint32_t>;
using SagaUNKey = ZuTuple<AnyTable *, Shard, UN>;

struct SagaRec_ : public ZmPolymorph {
  AnySaga__	*saga;
  AnyTable	*table;
  UN		un;
  uint32_t	step;
  Shard		shard;
  SagaOp::T	op;

  SagaRec_(
      AnySaga_ *saga_, AnyTable *table_, UN un_, uint32_t phase_,
      Shard shard_, SagaOp::T op_) :
    saga{saga_}, table{table_}, un{un_}, step{phase_}, shard{shard_}, op{op_} { }
};

inline SagaUNKey SagaUNAxor(const SagaRec_ &rec) {
  return {rec.table, rec.shard, rec.un};
}
inline SagaStepKey SagaStepAxor(const SagaRec_ &rec) {
  return {rec.saga->type(), rec.saga->id(), rec.step};
}

ZmHashDerive(SagaUNHash, SagaRec_,
  (ZmHashNode<SagaRec_,
    ZmHashKey<SagaUNAxor,
	ZmHashShadow<ZmHashHeapID<"">>>>));
ZmHashDerive(SagaStepHash, SagaUNHash::Node,
  (ZmHashNode<SagaUNHash::Node,
    ZmHashKey<SagaStepAxor, ZmHashHeapID<"Zdb.Saga.Rec">>>));

ZuDerive(SagaRec, SagaStepHash::Node);

struct SagaStepHashObj : public ZmNHeap<SagaStepHashObj>, public SagaStepHash {
friend inline ZuStringT<"Zdb.Saga.StepHash"> ZmNHeapID(SagaStepHashObj *);

  ZuDerive_(SagaStepHashObj, SagaStepHash)
};

struct SagaUNHashObj : public ZmNHeap<SagaUNHashObj>, public SagaUNHash {
friend inline ZuStringT<"Zdb.Saga.UNHash"> ZmNHeapID(SagaUNHashObj *);

  ZuDerive_(SagaUNHashObj, SagaUNHash)
};

ZuDerive(SagaDataRows,
  (ZtArray<SagaData, ZtArrayHeapID<"Zdb.Saga.Scan.Data">>));
ZuDerive(SagaStepRows,
  (ZtArray<SagaStep, ZtArrayHeapID<"Zdb.Saga.Scan.Step">>));

struct SagaScan__ {
  SagaCursor	cursor;
  Host		*oldMaster = nullptr;
  SagaDataRows	dataRows;
  SagaStepRows	stepRows;
  uint64_t	epoch = 0;
  unsigned	pageCount = 0;
  unsigned	pending = 0;
  uint32_t	step = 0;
};
struct SagaScan : public ZmNHeap<SagaScan>, public ZmPolymorph, public SagaScan__ {
friend inline ZuStringT<"Zdb.Saga.Scan"> ZmNHeapID(SagaScan *);

  ZuDerive_(SagaScan, SagaScan__)
};

template <typename U>
struct SagaPhases_ {
  template <typename I>
  using Phase = decltype(
    ZuDeclVal<U &>().template operator()<I{}>(SagaNoop{}));

  using T = ZuTypeMap<Phase, ZuSeqTL<ZuMkSeq<U::NPhases>>>;
};

template <typename U>
using SagaPhases = SagaPhases_<U>::T;

template <typename Phase>
struct SagaPhaseValid : public ZuBool<
  Phase::Op >= SagaOp::Insert && Phase::Op <= SagaOp::Delete> { };

template <typename Phases> struct SagaPhasesValid_;
template <typename ...Phases>
struct SagaPhasesValid_<ZuTypeList<Phases...>> :
  public ZuBool<(SagaPhaseValid<Phases>{} && ...)> { };

template <typename S, bool = (S::NPhases > 0)>
struct SagaDefValid : public ZuFalse { };
template <typename S>
struct SagaDefValid<S, true> : public ZuBool<
  SagaPhasesValid_<SagaPhases<S>>{} &&
  !ZuType<S::NPhases - 1, SagaPhases<S>>::Repeat> { };

template <typename Phases> struct SagaRepeats;
template <typename ...Phases>
struct SagaRepeats<ZuTypeList<Phases...>> :
  public ZuBool<(Phases::Repeat || ...)> { };

// Repetition counts are immutable saved payload data, never live DB queries.
// The persisted catalog describes phases; journal step IDs describe individual
// effects. A terminal, non-repeated phase retains the existing commit boundary.
template <typename Def>
struct SagaLayout {
  ZuAssert((SagaDefValid<Def>{}), "invalid saga definition");
  using Phases = SagaPhases<Def>;

  template <unsigned I>
  static uint64_t count(const Def &def) {
    if constexpr (ZuType<I, Phases>::Repeat)
      return def.template repeat<I>();
    else
      return 1;
  }

  static unsigned size(const Def &def) {
    if constexpr (!SagaRepeats<Phases>{})
      return Def::NPhases;
    else {
      unsigned size = 0;
      bool valid = true;
      ZuUnroll::all<Def::NPhases>([&def, &size, &valid](auto I) {
	if (!valid) return;
	auto n = count<I>(def);
	if (n > UINT32_MAX - size) valid = false;
	else size += unsigned(n);
      });
      return valid ? size : 0;
    }
  }

  static unsigned phase(const Def &def, unsigned step, unsigned &iteration) {
    iteration = 0;
    if constexpr (!SagaRepeats<Phases>{})
      return step;
    else {
      unsigned phase = Def::NPhases;
      ZuUnroll::all<Def::NPhases>([&def, &step, &iteration, &phase](auto I) {
	if (phase != Def::NPhases) return;
	auto n = count<I>(def);
	if (step < n) {
	  phase = I;
	  iteration = step;
	} else
	  step -= unsigned(n);
      });
      return phase;
    }
  }

  static bool phaseDef(unsigned phase, SagaTypeStep &row) {
    if (phase >= Def::NPhases) return false;
    return ZuSwitch::dispatch<Def::NPhases>(phase, [&row](auto I) {
      using Phase = ZuType<I, Phases>;
      row = SagaTypeStep{
	.type = typename Def::Type{}(), .table = typename Phase::TableID{}(),
	.step = I, .op = Phase::Op, .repeat = Phase::Repeat
      };
      return true;
    });
  }

  static bool stepDef(
      const Def &def, unsigned step, ZuCSpan &table, SagaOp::T &op) {
    if (step >= size(def)) return false;
    unsigned iteration;
    auto phase_ = phase(def, step, iteration);
    if (phase_ >= Def::NPhases) return false;
    return ZuSwitch::dispatch<Def::NPhases>(phase_, [&table, &op](auto I) {
      using Phase = ZuType<I, Phases>;
      table = typename Phase::TableID{}();
      op = Phase::Op;
      return true;
    });
  }

  static void dispatch(Def &def, unsigned phase, SagaDriver driver) {
    if (driver.fwd) {
      ZuSwitch::dispatch<Def::NPhases>(phase,
	  [&def, driver = ZuMv(driver)](auto I) mutable {
	(void)def.template operator()<I, true, SagaCompleteFn>(ZuMv(driver));
      });
    } else if constexpr (Def::NPhases > 1) {
      ZuSwitch::dispatch<Def::NPhases - 1>(phase,
	  [&def, driver = ZuMv(driver)](auto I) mutable {
	(void)def.template operator()<I, false, SagaCompleteFn>(ZuMv(driver));
      });
    }
  }
};

template <typename Impl, typename DB>
class Saga : public AnySaga<DB> {
public:
  ZuCSpan type() const override { return typename Impl::Type{}(); }
  unsigned nPhases() const override { return Impl::NPhases; }
  bool phaseDef(unsigned phase, SagaTypeStep &row) const override {
    return SagaLayout<Impl>::phaseDef(phase, row);
  }
  int save(SagaPayload &data) const override {
    data.length(0);
    return Zfb::save(data, *static_cast<const Impl *>(this));
  }
  unsigned nSteps() const override {
    return SagaLayout<Impl>::size(*static_cast<const Impl *>(this));
  }
  bool stepDef(unsigned step, ZuCSpan &table,
      SagaOp::T &op) const override {
    return SagaLayout<Impl>::stepDef(
      *static_cast<const Impl *>(this), step, table, op);
  }
  void run(SagaDriver driver) override {
    auto impl = static_cast<Impl *>(this);
    unsigned iter;
    auto phase = SagaLayout<Impl>::phase(*impl, driver.step, iter);
    if (ZuUnlikely(phase >= Impl::NPhases ||
        (!driver.fwd && phase + 1 == Impl::NPhases))) {
      this->result_(OpResult::Invalid, this->shard());
      return;
    }
    this->iteration(iter);
    SagaLayout<Impl>::dispatch(*impl, phase, ZuMv(driver));
  }

  static ZmRef<AnySaga<DB>> load(ZuBSpan data) {
    if (ZuUnlikely(!Zfb::verify<Impl>(data)))
      throw ZeEXCEPT(Fatal, "Zdb", "invalid saved saga");
    return Zfb::handler<Impl>(data).alloc();
  }
  static SagaDef def() {
    ZuAssert((SagaDefValid<Impl>{}), "invalid saga definition");
    return {typename Impl::Type{}(), Impl::NPhases,
      [](unsigned phase, SagaTypeStep &row) {
        return SagaLayout<Impl>::phaseDef(phase, row);
      }, [](ZuBSpan data) -> ZmRef<AnySaga_> { return load(data); }};
  }

protected:
  Saga() = default;
};

} // Zdb_

using ZdbSagaID = Zdb_::SagaID;
using ZdbSagaType = Zdb_::SagaType;
namespace ZdbSagaOp = Zdb_::SagaOp;
template <typename TableID_, ZdbSagaOp::T Op_, bool Repeat_ = false>
struct ZdbSagaPhase_ {
  using TableID = TableID_;
  enum { Op = Op_, Repeat = Repeat_ };
};
#define ZdbSagaPhase(phase_, table, op) \
  template <unsigned Phase, bool Fwd = true, \
    typename Complete = Zdb_::SagaCompleteFn, \
    ZuUnsigned<phase_> * = nullptr, typename = ZuIfT<Phase == phase_>> \
  ZdbSagaPhase_<ZuStringT<ZuPP_Q(table)>, ZdbSagaOp::op> \
  operator ()(Complete complete)
#define ZdbSagaRepeatPhase(phase_, table, op, count_) \
  template <unsigned Phase, ZuUnsigned<phase_> * = nullptr, \
    typename = ZuIfT<Phase == phase_>> \
  uint64_t repeat() const { return (count_); } \
  template <unsigned Phase, bool Fwd = true, \
    typename Complete = Zdb_::SagaCompleteFn, \
    ZuUnsigned<phase_> * = nullptr, typename = ZuIfT<Phase == phase_>> \
  ZdbSagaPhase_<ZuStringT<ZuPP_Q(table)>, ZdbSagaOp::op, true> \
  operator ()(Complete complete)
template <typename Impl, typename DB>
using ZdbSaga = Zdb_::Saga<Impl, DB>;
template <typename DB>
using ZdbAnySaga = Zdb_::AnySaga<DB>;

#endif /* ZdbSaga_HH */
