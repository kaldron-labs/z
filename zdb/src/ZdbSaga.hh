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
#include <zlib/ZuTuple.hh>
#include <zlib/ZuTL.hh>

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
// Initial inline payload allocation, not a payload limit.  256 bytes covers
// ordinary saga state while ZtBuiltin retains its tagged heap fallback.
#define ZdbSaga_BuiltinSize 256
#endif

namespace Zdb_ {

class DB;
class Saga;
template <typename, typename> struct SagaStepComplete;
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
ZuDerive(SagaLocs,
  (ZtArray<Shard, ZtArrayHeapID<"Zdb.Saga.Locs">>));

struct SagaData {
  SagaType	type;
  SagaID	id;
  Shard		shard;
  SagaPayload	data;

  friend ZuStringT<"Zdb.Saga"> ZdbHeapID(SagaData *);
  friend ZuStringT<"Zdb.Saga.Buf"> ZdbBufHeapID(SagaData *);
};

ZfbStruct(ZdbAPI, SagaData,
  (((type),	(Ctor<0>, Keys<0>)),	(String)),
  (((id),	(Ctor<1>, Keys<0>)),	(UInt128)),
  (((shard),	(Ctor<2>)),		(UInt8)),
  (((data),	(Ctor<3>)),		(Bytes)));

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
};

ZfbStruct(ZdbAPI, SagaTypeStep,
  (((type),	(Ctor<0>, Keys<0>)),	(String)),
  (((step),	(Ctor<2>, Keys<0>)),	(UInt32)),
  (((table),	(Ctor<1>)),		(String)),
  (((op),	(Ctor<3>, Enum<SagaOp::Map>)), (Int8)));

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

class Saga_ : public ZmPolymorph {
friend DB;
friend Saga;
template <typename, typename> friend struct SagaStepComplete;
template <typename, typename, typename, typename> friend struct SagaDB;
template <typename, typename> friend struct MSaga;

public:
  DB *db() const { return m_db; }
  ZuCSpan type() const { return m_key.template p<0>(); }
  const SagaKey &key() const { return m_key; }
  SagaID id() const { return m_key.template p<1>(); }
  Shard shard() const { return m_shard; }
  uint32_t step() const { return m_step; }
  uint64_t epoch() const { return m_epoch; }

protected:
  Saga_() = default;

private:
  void init_(
      DB *db, SagaKey key, Shard shard, uint64_t epoch,
      unsigned stepCount) {
    m_db = db;
    m_key = ZuMv(key);
    m_locs.length(stepCount, false);
    memset(m_locs.data(), 0xff, stepCount * sizeof(Shard));
    m_epoch = epoch;
    m_shard = shard;
    m_step = 0;
    m_fwd = true;
    m_rec = nullptr;
    m_uns = nullptr;
  }

  DB		*m_db = nullptr;
  SagaKey	m_key;
  SagaLocs	m_locs;
  uint64_t	m_epoch = 0;
  uint32_t	m_step = 0;
  Shard		m_shard = 0;
  bool		m_fwd = true;
  SagaRec	*m_rec = nullptr;	// staged recovery step
  SagaUNHash	*m_uns = nullptr;	// staged serial-replay reservations
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
struct SagaStepComplete {
  ZmRef<M>	saga;
  uint64_t	epoch = 0;
  uint32_t	step = 0;
  bool		fwd = true;
  Complete	complete;

  void operator ()(bool);

private:
  template <typename, typename> friend struct MSaga;

  void finish();
  static void run_(DB *, ZmRef<M>, Complete);
  static void delStep_(DB *, ZmRef<M>, Complete, unsigned, bool);
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
	ZmHashHeapID<"">>>));

template <typename Heap>
struct SagaNode_ : public Heap, public SagaHash::Node {
  ZuDerive_(SagaNode_, SagaHash::Node)
};
using SagaNodeHeap = ZmHeap<"Zdb.Saga.Node", SagaNode_<ZuVoid>>;
ZuDerive(SagaNode, (SagaNode_<SagaNodeHeap>));

template <typename Sagas>
using SagaUnion =
  ZuTypeApply<ZuUnion, typename Sagas::template Unshift<void>>;

template <typename Catalog, typename Heap>
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
using MSagaHeap = ZmHeap<"Zdb.Saga", MSaga_<Catalog, ZuVoid>>;

template <typename Catalog, typename Impl_ = void> struct MSaga;

template <typename Heap>
struct SagaHash_ : public Heap, public SagaHash {
  ZuDerive_(SagaHash_, SagaHash)
};
using SagaHashHeap = ZmHeap<"Zdb.Saga.Hash", SagaHash_<ZuVoid>>;
ZuDerive(SagaHashObj, (SagaHash_<SagaHashHeap>));

struct SagaCatalog__ {
  StoreTbl	*table = nullptr;
  ZtArray<uint8_t, ZtArrayHeapID<"Zdb.Saga.Type.Seen">> seen;
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
template <typename Heap>
struct SagaCatalog_ : public Heap, public ZmPolymorph, public SagaCatalog__ {
  ZuDerive_(SagaCatalog_, SagaCatalog__)
};
using SagaCatalogHeap =
  ZmHeap<"Zdb.Saga.Type", SagaCatalog_<ZuVoid>>;
ZuDerive(SagaCatalog, (SagaCatalog_<SagaCatalogHeap>));

using SagaStepKey = ZuTuple<ZuCSpan, SagaID, uint32_t>;
using SagaUNKey = ZuTuple<AnyTable *, Shard, UN>;

struct SagaRec__ : public ZmPolymorph {
  Saga		*saga;
  AnyTable	*table;
  UN		un;
  uint32_t	step;
  Shard		shard;
  SagaOp::T	op;

  SagaRec__(
      Saga *saga_, AnyTable *table_, UN un_, uint32_t step_,
      Shard shard_, SagaOp::T op_) :
    saga{saga_}, table{table_}, un{un_}, step{step_}, shard{shard_}, op{op_} { }
};

inline SagaUNKey SagaUNAxor(const SagaRec__ &rec) {
  return {rec.table, rec.shard, rec.un};
}
inline SagaStepKey SagaStepAxor(const SagaRec__ &rec) {
  return {rec.saga->type(), rec.saga->id(), rec.step};
}

ZmHashDerive(SagaUNHash, SagaRec__,
  (ZmHashNode<SagaRec__,
    ZmHashKey<SagaUNAxor,
	ZmHashShadow<ZmHashHeapID<"">>>>));
ZmHashDerive(SagaStepHash, SagaUNHash::Node,
  (ZmHashNode<SagaUNHash::Node,
    ZmHashKey<SagaStepAxor, ZmHashHeapID<"">>>));

template <typename Heap>
struct SagaRec_ : public Heap, public SagaStepHash::Node {
  ZuDerive_(SagaRec_, SagaStepHash::Node)
};
using SagaRecHeap = ZmHeap<"Zdb.Saga.Rec", SagaRec_<ZuVoid>>;
ZuDerive(SagaRec, (SagaRec_<SagaRecHeap>));

template <typename Heap>
struct SagaStepHash_ : public Heap, public SagaStepHash {
  ZuDerive_(SagaStepHash_, SagaStepHash)
};
using SagaStepHashHeap = ZmHeap<"Zdb.Saga.StepHash", SagaStepHash_<ZuVoid>>;
ZuDerive(SagaStepHashObj, (SagaStepHash_<SagaStepHashHeap>));

template <typename Heap>
struct SagaUNHash_ : public Heap, public SagaUNHash {
  ZuDerive_(SagaUNHash_, SagaUNHash)
};
using SagaUNHashHeap = ZmHeap<"Zdb.Saga.UNHash", SagaUNHash_<ZuVoid>>;
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
template <typename Heap>
struct SagaScan_ : public Heap, public ZmPolymorph, public SagaScan__ {
  ZuDerive_(SagaScan_, SagaScan__)
};
using SagaScanHeap =
  ZmHeap<"Zdb.Saga.Scan", SagaScan_<ZuVoid>>;
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
  (S::NSteps > 0) && SagaStepsValid_<SagaSteps<S>>{}> { };

template <typename Sagas> struct SagaDefsValid_;
template <typename ...S>
struct SagaDefsValid_<ZuTypeList<S...>> :
  public ZuBool<(SagaDefValid<S>{} && ...)> { };

template <typename Context, typename Sagas> struct SagaBasesValid_;
template <typename Context, typename ...S>
struct SagaBasesValid_<Context, ZuTypeList<S...>> :
  public ZuBool<(ZuIsBase<S, SagaBase<Context>>{} && ...)> { };

template <typename Catalog>
struct SagaIDs { using Keys = SagaTypes<typename Catalog::List>; };

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

  static int match(ZuCSpan type) {
    static constexpr auto matcher = ZuMatcher<SagaIDs<Catalog>>();
    return matcher.exact(type);
  }

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
	  .op = Step::Op
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
    saga->u.dispatch([context, saga = ZuMv(saga),
	complete = ZuMv(complete)](
	auto, auto &def) mutable {
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
    saga->u.dispatch([saga = ZuMv(saga), complete = ZuMv(complete)](
	auto, auto &def) mutable {
	ZmAssert(def.saga == saga.ptr());
	run_(def, ZuMv(saga), ZuMv(complete));
    });
  }

  template <typename Def>
  static void run_(Def &def, ZmRef<M> saga, SagaCompleteFn complete) {
	auto ptr = saga.ptr();
	auto step = ptr->step();
	if (ZuLikely(step < Def::NSteps)) {
	  bool fwd = ptr->m_fwd;
	  SagaStepComplete<M, SagaCompleteFn> stepComplete{
	    ZuMv(saga), ptr->epoch(), step, fwd, ZuMv(complete)};
	  if (fwd) {
	    ZuSwitch::dispatch<Def::NSteps>(step,
		[&def, complete = ZuMv(stepComplete)](auto I) mutable {
	      (void)def.template operator()<I, true, SagaCompleteFn>(
		ZuMv(complete));
	    });
	  } else if constexpr (Def::NSteps > 1) {
	    if (ptr->m_locs[step] == Shard(-1)) {
	      stepComplete(true);
	      return;
	    }
	    ZuSwitch::dispatch<Def::NSteps - 1>(step,
		[&def, complete = ZuMv(stepComplete)](auto I) mutable {
	      (void)def.template operator()<I, false, SagaCompleteFn>(
		ZuMv(complete));
	    });
	  } else
	    stepComplete(false);
	  return;
	}
	if (ZuLikely(step == Def::NSteps)) {
	  SagaStepComplete<M, SagaCompleteFn>{
	    ZuMv(saga), ptr->epoch(), step, true, ZuMv(complete)
	  }.finish();
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

  template <typename Steps>
  static bool stepDef_(unsigned step, ZuCSpan &table, SagaOp::T &op) {
    if (step >= Steps::N) return false;
    return ZuSwitch::dispatch<Steps::N>(step, [&table, &op](auto I) {
      using Step = ZuType<I, Steps>;
      table = typename Step::TableID{}();
      op = Step::Op;
      return true;
    });
  }

  static bool stepDef(
      ZuCSpan type, unsigned step, ZuCSpan &table, SagaOp::T &op) {
    int i = match(type);
    if (ZuUnlikely(i < 0)) return false;
    return ZuSwitch::dispatch<Sagas::N>(unsigned(i),
	[step, &table, &op](auto I) {
	  using Def = ZuType<I, Sagas>;
	  return stepDef_<SagaSteps<Def>>(step, table, op);
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
    if (row.step != step ||
	!MSaga<Catalog>::stepDef(type, row.step, table, op) ||
	row.table != table || row.op != op) {
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
template <typename TableID_, ZdbSagaOp::T Op_>
struct ZdbSagaStep_ {
  using TableID = TableID_;
  enum { Op = Op_ };
};
#define ZdbSagaStep(step_, table, op) \
  template <unsigned Step, bool Fwd = true, \
    typename Complete = Zdb_::SagaCompleteFn> \
  ZuIfT<Step == step_, \
    ZdbSagaStep_<ZuStringT<ZuPP_Q(table)>, ZdbSagaOp::op>> \
  operator ()(Complete complete)
using ZdbSaga = Zdb_::Saga;
template <typename Context>
using ZdbSagaBase = Zdb_::SagaBase<Context>;
template <typename Catalog, typename Impl_ = void>
using ZdbMSaga = Zdb_::MSaga<Catalog, Impl_>;

#endif /* ZdbSaga_HH */
