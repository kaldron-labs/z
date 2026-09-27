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
#include <zlib/ZuMatcher.hh>
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
#include <zlib/ZmList.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/ZdbBuf.hh>
#include <zlib/ZdbTypes.hh>

#include <zlib/zdb_saga_fbs.h>
#include <zlib/zdb_saga_step_fbs.h>
#include <zlib/zdb_saga_type_fbs.h>

#ifndef ZdbSaga_BuiltinSize
// Initial inline payload allocation, not a payload limit. 256 bytes covers
// ordinary saga state while ZtBuiltin retains its tagged heap fallback.
#define ZdbSaga_BuiltinSize 256
#endif

namespace Zdb_ {

class DB;
class Saga;
template <typename, typename> struct SagaStepDriver;
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

ZfbStruct(ZdbAPI, SagaData,
  (((type),	(Ctor<0>, Keys<0>)),	(String)),
  (((id),	(Ctor<1>, Keys<0>)),	(UInt128)),
  (((shard),	(Ctor<2>)),		(UInt8)),
  (((data),	(Ctor<3>)),		(Bytes)),
  (((deadline),	(Ctor<4>)),		(Time)));

ZfbRoot(SagaData);

struct SagaStep {
  SagaType	type;
  SagaID	id;
  uint32_t	step;
  Shard		shard;
  UN		un;

  friend ZuStringT<"Zdb.Saga.Step"> ZdbHeapID(SagaStep *);
  friend ZuStringT<"Zdb.Saga.Step.Buf"> ZdbBufHeapID(SagaStep *);
};

ZfbStruct(ZdbAPI, SagaStep,
  (((type),	(Ctor<0>, Keys<0>)),	(String)),
  (((id),	(Ctor<1>, Keys<0>)),	(UInt128)),
  (((step),	(Ctor<2>, Keys<0>)),	(UInt32)),
  (((shard),	(Ctor<3>)),		(UInt8)),
  (((un),	(Ctor<4>)),		(UInt64)));

ZfbRoot(SagaStep);

ZtEnumNS(ZdbAPI, SagaOp, int8_t,
  Invalid,
  Insert,
  Update,
  Delete);

struct SagaTypeStep {
  SagaType	type;
  IDString	table;
  uint32_t	step;
  SagaOp::T	op;
  bool		repeat = false;
};

ZfbStruct(ZdbAPI, SagaTypeStep,
  (((type),	(Ctor<0>, Keys<0>)),		(String)),
  (((step),	(Ctor<2>, Keys<0>)),		(UInt32)),
  (((table),	(Ctor<1>)),			(String)),
  (((op),	(Ctor<3>, Enum<SagaOp::Map>)),	(Int8)),
  (((repeat),	(Ctor<4>)),			(Bool)));

ZfbRoot(SagaTypeStep);

ZuDerive(SagaRecoveryFn,
  (ZmFn<void(), ZmFnHeapID<"Zdb.Saga.RecoveryFn">>));
ZuDerive(SagaRunFn,
  (ZmFn<void(ZmRef<Saga>), ZmFnHeapID<"Zdb.Saga.RunFn">>));
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

template <typename Context_>
struct SagaBase {
  Context_	*context = nullptr;
  Saga		*saga = nullptr;
};

namespace SagaState {
  using T = int8_t;
  enum {
    Inactive = 0,
    Rebuilding,
    Active
  };
}

struct SagaRunData {
  SagaShards	shards;
};

struct Saga_ : public ZmPolymorph, public SagaRunData {
friend DB;
friend Saga;
template <typename, typename> friend struct SagaStepDriver;
template <typename, typename, typename, typename> friend struct SagaDB;
template <typename, typename> friend struct MSaga;

public:
  DB *db() const { return m_db; }
  ZuCSpan type() const { return m_key.template p<0>(); }
  const SagaKey &key() const { return m_key; }
  SagaID id() const { return m_key.template p<1>(); }
  Shard shard() const { return m_shard; }
  uint32_t step() const { return m_step; }
  uint32_t &iteration() { return m_iteration; }
  uint32_t iteration() const { return m_iteration; }
  uint64_t epoch() const { return m_epoch; }
  ZuTime deadline() const { return m_deadline; }

protected:
  Saga_() = default;

private:
  void init_(
      DB *db, SagaKey key, Shard shard, uint64_t epoch,
      unsigned stepCount, ZuTime deadline) {
    m_db = db;
    m_key = ZuMv(key);
    shards.length(stepCount, false);
    memset(shards.data(), 0xff, stepCount * sizeof(Shard));
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

template <typename T, typename Complete>
using SagaFn = ZmFn<void(Row<T> *, Complete), ZmFnHeapID<"Zdb.Saga.Fn">>;

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

template <typename KeyIDs, typename Impl, typename Call>
struct SagaFindUpdate;
template <typename Impl, typename Call>
struct SagaFindDelete;

class Saga : public Saga_ {
friend DB;
template <typename, typename> friend struct MSaga;
template <typename, typename, typename> friend struct SagaFindUpdate;
template <typename, typename> friend struct SagaFindDelete;

public:
  using Saga_::db;
  using Saga_::id;
  using Saga_::shard;
  using Saga_::step;
  using Saga_::type;

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
    typename Impl::template Key<KeyID>, Complete &&, L &&);
  template <typename T, typename Impl, typename Complete, typename L>
  void del(Impl *, ZmRef<Row<T>>, Complete &&, L &&);
  template <unsigned KeyID, typename Impl, typename Complete, typename L>
  void findDel(
    Impl *, Shard, const typename Impl::template Key<KeyID> &,
    Complete &&, L &&);

protected:
  Saga() = default;

private:
  ZdbAPI void stepRecovered_(Shard);
  ZdbAPI void result_(OpResult::T, Shard);
  ZdbAPI bool prepare_(AnyTable *, Shard, SagaOp::T, bool, UN &, bool &);
  ZdbAPI void intent_(Shard, UN);
  ZdbAPI void replay_(OpResult::T, Shard);

  template <typename T, typename Impl, typename Call>
  void insert_(Impl *, Shard, ZmRef<Row<T>>, UN, bool, Call);
  template <typename KeyIDs_, typename T, typename Impl, typename Call>
  void update_(Impl *, Shard, ZmRef<Row<T>>, UN, bool, Call);
  template <unsigned KeyID, typename KeyIDs_, typename Impl, typename Call>
  void findUpd_(Impl *, Shard, typename Impl::template Key<KeyID>, Call);
  template <typename T, typename Impl, typename Call>
  void del_(Impl *, Shard, ZmRef<Row<T>>, UN, bool, Call);
  template <unsigned KeyID, typename Impl, typename Call>
  void findDel_(Impl *, Shard, typename Impl::template Key<KeyID>, Call);
};

template <typename KeyIDs, typename Impl, typename Call>
struct SagaFindUpdate {
  Saga		*saga;
  Impl		*table;
  Shard		shard;
  UN		un;
  bool		saved;
  Call		call;

  void operator ()(ZmRef<Row<typename Impl::T>>);
};

template <typename Impl, typename Call>
struct SagaFindDelete {
  Saga		*saga;
  Impl		*table;
  Shard		shard;
  UN		un;
  bool		saved;
  Call		call;

  void operator ()(ZmRef<Row<typename Impl::T>>);
};

template <typename M, typename Complete>
struct SagaStepDriver {
  ZmRef<M>	saga;
  uint64_t	epoch = 0;
  uint32_t	step = 0;
  bool		fwd = true;
  Complete	complete;

  void operator ()(bool);

private:
  template <typename, typename> friend struct MSaga;

  void finish(bool);
  static void run_(DB *, ZmRef<M>, Complete);
  static void del_(DB *, ZmRef<M>, Complete, unsigned, bool);
  static void finish_(DB *, ZmRef<M>, Complete, bool);
  static void cleanup_(DB *, ZmRef<M>, Complete, unsigned);
  static void abandon_(DB *, ZmRef<M>);
  static void end_(DB *, Saga *);
  static void terminal_(DB *, ZmRef<M>, Complete, bool);
};

struct SagaNode__ : public ZmPolymorph {
  ZmRef<Saga> saga;

  SagaNode__(ZmRef<Saga> saga_) : saga{ZuMv(saga_)} { }
};

ZmListDerive(SagaList, SagaNode__,
  ZmListNode<SagaNode__,
    ZmListShadow<>>);

inline const SagaKey &SagaKeyAxor(const SagaList::Node &node) {
  return static_cast<const SagaNode__ &>(node).saga->key();
}

ZmHashDerive(SagaHash, SagaList::Node,
  (ZmHashNode<SagaList::Node,
    ZmHashKey<SagaKeyAxor,
      ZmHashHeapID<"Zdb.Saga.Node">>>));

ZuDerive(SagaNode, SagaHash::Node);

template <typename Sagas>
using SagaUnion =
  ZuTypeApply<ZuUnion, typename Sagas::template Unshift<void>>;

template <typename Catalog, typename Heap = ZuVoid>
struct MSaga_ : public Heap, public Saga {
  using Union = SagaUnion<typename Catalog::List>;

  Union u;

  template <typename S>
  void init(S &&s) {
    using T = ZuDecay<S>;
    new (u.template new_<T>()) T(ZuFwd<S>(s));
  }
};

template <typename Catalog>
ZuDerive(MSagaHeap, (ZmHeap<"Zdb.Saga", MSaga_<Catalog>>));

template <typename Catalog, typename Impl_ = void> struct MSaga;

template <typename Heap = ZuVoid>
struct SagaHash_ : public Heap, public SagaHash {
  ZuDerive_(SagaHash_, SagaHash)
};
ZuDerive(SagaHashHeap, (ZmHeap<"Zdb.Saga.Hash", SagaHash_<>>));
ZuDerive(SagaHashObj, (SagaHash_<SagaHashHeap>));

using SagaTypeSeen = ZtArray<uint8_t, ZtArrayHeapID<"Zdb.Saga.Type.Seen">>;

struct SagaCatalog__ {
  StoreTbl	*table = nullptr;
  SagaTypeSeen	seen;
  SagaType	type;
  ZeException	error;
  UN		nextUN = 0;
  uint64_t	step = 0;
  int		typeIndex = -1;
  unsigned	pageCount = 0;
  unsigned	writeType = 0;
  unsigned	writeStep = 0;

  template <typename Catalog> void load(SagaTypeStep);
  template <typename Catalog> void end();
};
template <typename Heap = ZuVoid>
struct SagaCatalog_ : public Heap, public ZmPolymorph, public SagaCatalog__ {
  ZuDerive_(SagaCatalog_, SagaCatalog__)
};
ZuDerive(SagaCatalogHeap, (ZmHeap<"Zdb.Saga.Type", SagaCatalog_<>>));
ZuDerive(SagaCatalog, (SagaCatalog_<SagaCatalogHeap>));

using SagaStepKey = ZuTuple<ZuCSpan, SagaID, uint32_t>;
using SagaUNKey = ZuTuple<AnyTable *, Shard, UN>;

struct SagaRec_ : public ZmPolymorph {
  Saga		*saga;
  AnyTable	*table;
  UN		un;
  uint32_t	step;
  Shard		shard;
  SagaOp::T	op;

  SagaRec_(
      Saga *saga_, AnyTable *table_, UN un_, uint32_t step_,
      Shard shard_, SagaOp::T op_) :
    saga{saga_}, table{table_}, un{un_}, step{step_}, shard{shard_}, op{op_} { }
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

template <typename Heap = ZuVoid>
struct SagaStepHash_ : public Heap, public SagaStepHash {
  ZuDerive_(SagaStepHash_, SagaStepHash)
};
ZuDerive(SagaStepHashHeap, (ZmHeap<"Zdb.Saga.StepHash", SagaStepHash_<>>));
ZuDerive(SagaStepHashObj, (SagaStepHash_<SagaStepHashHeap>));

template <typename Heap = ZuVoid>
struct SagaUNHash_ : public Heap, public SagaUNHash {
  ZuDerive_(SagaUNHash_, SagaUNHash)
};
ZuDerive(SagaUNHashHeap, (ZmHeap<"Zdb.Saga.UNHash", SagaUNHash_<>>));
ZuDerive(SagaUNHashObj, (SagaUNHash_<SagaUNHashHeap>));

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
template <typename Heap = ZuVoid>
struct SagaScan_ : public Heap, public ZmPolymorph, public SagaScan__ {
  ZuDerive_(SagaScan_, SagaScan__)
};
ZuDerive(SagaScanHeap, (ZmHeap<"Zdb.Saga.Scan", SagaScan_<>>));
ZuDerive(SagaScan, (SagaScan_<SagaScanHeap>));

template <typename S>
using SagaDefType = typename S::Type;

template <typename Sagas>
using SagaTypes = ZuTypeMap<SagaDefType, Sagas>;

template <typename U>
struct SagaSteps_ {
  template <typename I>
  using Step = decltype(
    ZuDeclVal<U &>().template operator()<I{}>(SagaNoop{}));

  using T = ZuTypeMap<Step, ZuSeqTL<ZuMkSeq<U::NSteps>>>;
};

template <typename U>
using SagaSteps = typename SagaSteps_<U>::T;

template <typename Step>
struct SagaStepValid : public ZuBool<
  Step::Op >= SagaOp::Insert && Step::Op <= SagaOp::Delete> { };

template <typename Steps> struct SagaStepsValid_;
template <typename ...Steps>
struct SagaStepsValid_<ZuTypeList<Steps...>> :
  public ZuBool<(SagaStepValid<Steps>{} && ...)> { };

template <typename S>
struct SagaDefValid : public ZuBool<
  (S::NSteps > 0) && SagaStepsValid_<SagaSteps<S>>{} &&
  !ZuType<S::NSteps - 1, SagaSteps<S>>::Repeat> { };

template <typename Steps> struct SagaRepeats;
template <typename ...Steps>
struct SagaRepeats<ZuTypeList<Steps...>> :
  public ZuBool<(Steps::Repeat || ...)> { };

// Repetition counts are immutable saved payload data, never live DB queries.
// The persisted catalog describes phases; journal step IDs describe individual
// effects. A terminal, non-repeated phase retains the existing commit boundary.
template <typename Def>
struct SagaLayout {
  using Steps = SagaSteps<Def>;

  template <unsigned I>
  static uint64_t count(const Def &def) {
    if constexpr (ZuType<I, Steps>::Repeat)
      return def.template repeat<I>();
    else
      return 1;
  }

  static unsigned size(const Def &def) {
    if constexpr (!SagaRepeats<Steps>{})
      return Def::NSteps;
    else {
      unsigned size = 0;
      bool valid = true;
      ZuUnroll::all<Def::NSteps>([&def, &size, &valid](auto I) {
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
    if constexpr (!SagaRepeats<Steps>{})
      return step;
    else {
      unsigned phase = Def::NSteps;
      ZuUnroll::all<Def::NSteps>([&def, &step, &iteration, &phase](auto I) {
	if (phase != Def::NSteps) return;
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
};

template <typename Sagas> struct SagaDefsValid_;
template <typename ...S>
struct SagaDefsValid_<ZuTypeList<S...>> :
  public ZuBool<(SagaDefValid<S>{} && ...)> { };

template <typename Context, typename Sagas> struct SagaBasesValid_;
template <typename Context, typename ...S>
struct SagaBasesValid_<Context, ZuTypeList<S...>> :
  public ZuBool<(ZuIsBase<S, SagaBase<Context>>{} && ...)> { };

template <typename Catalog, typename Impl_>
struct MSaga : public MSaga_<Catalog, MSagaHeap<Catalog>> {
  ZuDerive_(MSaga, (MSaga_<Catalog, MSagaHeap<Catalog>>))
  using Sagas = typename Catalog::List;
  using Types = SagaTypes<Sagas>;
  using Impl = ZuIf<ZuIsSame<Impl_, void>{}, MSaga<Catalog>, Impl_>;
  using M = Impl;
  using Saga::type;

  ZuAssert((Sagas::N > 0), "empty saga bundle");
  ZuAssert((ZuTypeUnique<Types>::N == Sagas::N),
    "duplicate saga type");
  ZuAssert((SagaDefsValid_<Sagas>{}), "invalid saga definition");

  static int match(ZuCSpan type) { return Catalog::match(type); }

  static bool catalog(unsigned type, unsigned step, SagaTypeStep &row) {
    if (type >= Sagas::N) return false;
    return ZuSwitch::dispatch<Sagas::N>(type, [step, &row](auto I) {
      using Def = ZuType<I, Sagas>;
      using Steps = SagaSteps<Def>;
      if (step >= Steps::N) return false;
      return ZuSwitch::dispatch<Steps::N>(step, [&row](auto J) {
	using Step = ZuType<J, Steps>;
	row = SagaTypeStep{
	  .type = typename Def::Type{}(),
	  .table = typename Step::TableID{}(),
	  .step = J,
	  .op = Step::Op,
	  .repeat = Step::Repeat
	};
	return true;
      });
    });
  }

  static ZmRef<M> load(ZuCSpan type, ZuBSpan data) {
    int i = match(type);
    if (ZuUnlikely(i < 0))
      throw ZeEXCEPT(Fatal, "Zdb", ([type = ZeString{type}](auto &s) {
	s << "unknown saga type \"" << type << '"';
      }));
    ZmRef<M> saga = new M{};
    bool loaded = ZuSwitch::dispatch<Sagas::N>(unsigned(i),
	[saga = saga.ptr(), data](auto I) {
	  using Def = ZuType<I, Sagas>;
	  auto fbo = ZfbStruct::verify<Def>(data);
	  if (ZuUnlikely(!fbo)) return false;
	  ZfbStruct::new_<Def>(saga->u.template new_<Def>(), fbo,
	    typename Def::Base{});
	  return true;
	});
    if (ZuUnlikely(!loaded))
      throw ZeEXCEPT(Fatal, "Zdb", ([type = ZeString{type}](auto &s) {
	s << "invalid saved saga \"" << type << '"';
      }));
    return saga;
  }

  static void save(const M *saga, SagaPayload &data) {
    saga->u.cdispatch([&data](auto, const auto &def) {
      Zfb::IOBuilder fbb{new IOBufAlloc<SagaData>{}};
      fbb.Finish(ZfbStruct::save(fbb, def));
      unsigned n = fbb.GetSize();
      data.length(n, false);
      memcpy(data.data(), fbb.GetBufferPointer(), n);
    });
  }

  template <typename Context, typename Complete>
  static void run(Context *context, ZmRef<M> saga, Complete &&complete) {
    runImpl(context, ZuMv(saga),
      SagaCompleteFn{[complete = ZuFwd<Complete>(complete)](
	bool ok) mutable { complete(ok); }});
  }

  template <typename Complete>
  static void run(ZmRef<M> saga, Complete &&complete) {
    runImpl(ZuMv(saga),
      SagaCompleteFn{[complete = ZuFwd<Complete>(complete)](
	bool ok) mutable { complete(ok); }});
  }

private:
  template <typename Context>
  static void runImpl(
      Context *context, ZmRef<M> saga, SagaCompleteFn complete) {
    saga->u.dispatch([
      context, saga = ZuMv(saga), complete = ZuMv(complete)
    ](auto, auto &def) mutable {
      auto ptr = saga.ptr();
      if (!def.saga) {
	def.context = context;
	def.saga = ptr;
      } else {
	ZmAssert(def.context == context && def.saga == ptr);
      }
      run_(def, ZuMv(saga), ZuMv(complete));
    });
  }

  static void runImpl(ZmRef<M> saga, SagaCompleteFn complete) {
    saga->u.dispatch([
      saga = ZuMv(saga), complete = ZuMv(complete)
    ](auto, auto &def) mutable {
      ZmAssert(def.saga == saga.ptr());
      run_(def, ZuMv(saga), ZuMv(complete));
    });
  }

  template <typename Def>
  static void run_(Def &def, ZmRef<M> saga, SagaCompleteFn complete) {
    auto ptr = saga.ptr();
    auto step = ptr->step();
    if (ZuUnlikely(!ptr->fwd() && ptr->rec() &&
	(ptr->rec()->un == nullUN() ||
	 ZuCmp<UN>::cmp(ptr->rec()->table->nextUN(ptr->rec()->shard),
	   ptr->rec()->un) <= 0))) {
      SagaStepDriver<M, SagaCompleteFn>{
	ZuMv(saga), ptr->epoch(), step, false, ZuMv(complete)}(true);
      return;
    }
    if (ZuLikely(step < ptr->shards.length())) {
      auto phase = SagaLayout<Def>::phase(def, step, ptr->iteration());
      if (ZuUnlikely(phase >= Def::NSteps)) {
	ptr->result_(OpResult::Invalid, ptr->shard());
	return;
      }
      bool fwd = ptr->fwd();
      SagaStepDriver<M, SagaCompleteFn> stepComplete{
	ZuMv(saga), ptr->epoch(), step, fwd, ZuMv(complete)};
      if (fwd) {
	ZuSwitch::dispatch<Def::NSteps>(phase,
	    [&def, complete = ZuMv(stepComplete)](auto I) mutable {
	  (void)def.template operator()<I, true, SagaCompleteFn>(
	    ZuMv(complete));
	});
      } else if constexpr (Def::NSteps > 1) {
	if (ptr->shards[step] == Shard(-1)) {
	  stepComplete(true);
	  return;
	}
	ZuSwitch::dispatch<Def::NSteps - 1>(phase,
	    [&def, complete = ZuMv(stepComplete)](auto I) mutable {
	  (void)def.template operator()<I, false, SagaCompleteFn>(
	    ZuMv(complete));
	});
      } else
	stepComplete(false);
      return;
    }
    if (ZuLikely(step == ptr->shards.length())) {
      SagaStepDriver<M, SagaCompleteFn>{
	ZuMv(saga), ptr->epoch(), step, ptr->fwd(), ZuMv(complete)
	}.finish(ptr->fwd());
      return;
    }
    ptr->result_(OpResult::Invalid, ptr->shard());
  }

public:
  static ZuCSpan type(const M *saga) {
    return saga->u.cdispatch([](auto, const auto &def) {
      using Def = ZuDecay<decltype(def)>;
      return ZuCSpan{typename Def::Type{}()};
    });
  }

  static unsigned stepCount(ZuCSpan type) {
    int i = match(type);
    if (ZuUnlikely(i < 0)) return 0;
    return ZuSwitch::dispatch<Sagas::N>(unsigned(i), [](auto I) {
      using Def = ZuType<I, Sagas>;
      return unsigned(Def::NSteps);
    });
  }

  static unsigned stepCount(const M *saga) {
    return saga->u.cdispatch([](auto, const auto &def) {
      return SagaLayout<ZuDecay<decltype(def)>>::size(def);
    });
  }

  template <typename Steps>
  static bool stepDef_(unsigned step, ZuCSpan &table, SagaOp::T &op,
      bool *repeat = nullptr) {
    if (step >= Steps::N) return false;
    return ZuSwitch::dispatch<Steps::N>(step, [&table, &op, repeat](auto I) {
      using Step = ZuType<I, Steps>;
      table = typename Step::TableID{}();
      op = Step::Op;
      if (repeat) *repeat = Step::Repeat;
      return true;
    });
  }

  static bool stepDef(
      ZuCSpan type, unsigned step, ZuCSpan &table, SagaOp::T &op,
      bool *repeat = nullptr) {
    int i = match(type);
    if (ZuUnlikely(i < 0)) return false;
    return ZuSwitch::dispatch<Sagas::N>(unsigned(i),
	[step, &table, &op, repeat](auto I) {
	  using Def = ZuType<I, Sagas>;
	  return stepDef_<SagaSteps<Def>>(step, table, op, repeat);
	});
  }

  static bool stepDef(
      const M *saga, unsigned step, ZuCSpan &table, SagaOp::T &op) {
    return saga->u.cdispatch([step, &table, &op](auto, const auto &def) {
      using Def = ZuDecay<decltype(def)>;
      unsigned iteration;
      auto phase = SagaLayout<Def>::phase(def, step, iteration);
      return stepDef_<SagaSteps<Def>>(phase, table, op);
    });
  }
};

template <typename Catalog>
inline void SagaCatalog__::end()
{
  if (error || typeIndex < 0) return;
  auto expected = MSaga<Catalog>::stepCount(type);
  if (step != expected)
    error = ZeEXCEPT(Fatal, "Zdb", ([
      type = ZeString{type}, step = step, expected
    ](auto &s) {
      s << "saga type \"" << type << "\" has " << step
	<< " stored steps, expected " << expected;
    }));
}

template <typename Catalog>
inline void SagaCatalog__::load(SagaTypeStep row)
{
  if (error) return;
  if (row.type != type) {
    end<Catalog>();
    if (error) return;
    type = ZuMv(row.type);
    step = 0;
    typeIndex = MSaga<Catalog>::match(type);
    if (typeIndex >= 0) seen[typeIndex] = 1;
  }
  if (typeIndex >= 0) {
    ZuCSpan table;
    SagaOp::T op = SagaOp::Invalid;
    bool repeat = false;
    if (row.step != step ||
	!MSaga<Catalog>::stepDef(type, row.step, table, op, &repeat) ||
	row.table != table || row.op != op || row.repeat != repeat) {
      error = ZeEXCEPT(Fatal, "Zdb", ([
	type = ZeString{type}, step = row.step
      ](auto &s) {
	s << "saga type \"" << type << "\" step " << step
	  << " conflicts with the stored definition";
      }));
      return;
    }
  }
  step = uint64_t(row.step) + 1;
}

} // Zdb_

using ZdbSagaID = Zdb_::SagaID;
using ZdbSagaType = Zdb_::SagaType;
namespace ZdbSagaOp = Zdb_::SagaOp;
template <typename TableID_, ZdbSagaOp::T Op_, bool Repeat_ = false>
struct ZdbSagaStep_ {
  using TableID = TableID_;
  enum { Op = Op_, Repeat = Repeat_ };
};
#define ZdbSagaStep(step_, table, op) \
  template <unsigned Step, bool Fwd = true, \
    typename Complete = Zdb_::SagaCompleteFn, \
    ZuUnsigned<step_> * = nullptr, typename = ZuIfT<Step == step_>> \
  ZdbSagaStep_<ZuStringT<ZuPP_Q(table)>, ZdbSagaOp::op> \
  operator ()(Complete complete)
#define ZdbSagaRepeatStep(step_, table, op, count_) \
  template <unsigned Step, ZuUnsigned<step_> * = nullptr, \
    typename = ZuIfT<Step == step_>> \
  uint64_t repeat() const { return (count_); } \
  template <unsigned Step, bool Fwd = true, \
    typename Complete = Zdb_::SagaCompleteFn, \
    ZuUnsigned<step_> * = nullptr, typename = ZuIfT<Step == step_>> \
  ZdbSagaStep_<ZuStringT<ZuPP_Q(table)>, ZdbSagaOp::op, true> \
  operator ()(Complete complete)
using ZdbSaga = Zdb_::Saga;
template <typename Context>
using ZdbSagaBase = Zdb_::SagaBase<Context>;
template <typename Catalog, typename Impl_ = void>
using ZdbMSaga = Zdb_::MSaga<Catalog, Impl_>;

#define ZdbSagaDerive(Name, ...) \
  struct Name { \
    using List = ZuTypeList<__VA_ARGS__>; \
    static int match(ZuCSpan); \
  }
#define ZdbSagaImpl(Name, ...) \
  int Name::match(ZuCSpan type) { \
    struct IDs { using Keys = Zdb_::SagaTypes<List>; }; \
    static constexpr auto matcher = ZuMatcher<IDs, false>(); \
    return matcher.exact(type); \
  }

#endif /* ZdbSaga_HH */
