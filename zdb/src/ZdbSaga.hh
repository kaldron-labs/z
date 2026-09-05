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
#include <zlib/ZuUnion.hh>

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
#define ZdbSaga_BuiltinSize 256
#endif

namespace Zdb_ {

class DB;
class Saga;
struct SagaStepComplete;
struct SagaRec;
struct SagaUNHash;
class AnyTable;
class Host;
class StoreTbl;
template <typename T> class Table;
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

ZfbStruct(SagaData,
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

ZfbStruct(SagaStep,
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

ZfbStruct(SagaTypeStep,
  (((type),	(Ctor<0>, Keys<0>)),	(String)),
  (((step),	(Ctor<2>, Keys<0>)),	(UInt32)),
  (((table),	(Ctor<1>)),		(String)),
  (((op),	(Ctor<3>, Enum<SagaOp::Map>)), (Int8)));

ZfbRoot(SagaTypeStep);

using SagaSubmitResult = ZuUnion<void, ZeException>;
using SagaSubmitFn = ZmFn<
  void(SagaSubmitResult), ZmFnHeapID<"Zdb.Saga.SubmitFn">>;

typedef void (*SagaDoneFn)(void *, ZuCSpan, SagaID);
typedef void (*SagaErrorFn)(void *, ZuCSpan, SagaID, ZeException);

struct SagaHandler {
  void		*context = nullptr;
  SagaDoneFn	doneFn = [](void *, ZuCSpan, SagaID) { };
  SagaErrorFn	errorFn = [](void *, ZuCSpan, SagaID, ZeException) { };
};

using SagaStartFn = ZmFn<void(), ZmFnHeapID<"Zdb.Saga.StartFn">>;
using SagaRunFn = ZmFn<void(ZmRef<Saga>), ZmFnHeapID<"Zdb.Saga.RunFn">>;

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
friend SagaStepComplete;
template <typename> friend struct SagaDB;

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
    m_epoch = epoch;
    m_shard = shard;
    m_step = 0;
    m_rec = nullptr;
    m_uns = nullptr;
  }

  DB		*m_db = nullptr;
  SagaKey	m_key;
  SagaLocs	m_locs;
  uint64_t	m_epoch = 0;
  uint32_t	m_step = 0;
  Shard		m_shard = 0;
  SagaRec	*m_rec = nullptr;	// staged recovery step
  SagaUNHash	*m_uns = nullptr;	// staged serial-replay reservations
};

template <typename Key, typename L>
struct SagaFind {
  Key key;
  L fn;

  template <typename O> void operator ()(O row) { fn(row); }
};

class Saga : public Saga_ {
friend DB;
template <typename> friend struct MSaga;

public:
  using Saga_::db;
  using Saga_::id;
  using Saga_::shard;
  using Saga_::step;
  using Saga_::type;

  void fail(ZeException);

  template <typename T, typename L>
  void insert(Table<T> *, ZmRef<Row<T>>, L &&);
  template <typename KeyIDs_ = ZuSeq<>, typename T, typename L>
  void update(Table<T> *, ZmRef<Row<T>>, L &&);
  template <
    unsigned KeyID, typename KeyIDs_ = ZuSeq<>, typename T, typename L>
  void findUpd(Table<T> *, Shard, typename Table<T>::template Key<KeyID>, L &&);
  template <typename T, typename L>
  void del(Table<T> *, ZmRef<Row<T>>, L &&);
  template <unsigned KeyID, typename T, typename L>
  void findDel(
    Table<T> *, Shard, const typename Table<T>::template Key<KeyID> &, L &&);

protected:
  Saga() = default;
  void done();

private:
  void complete_(Shard);
  void result_(OpResult::T, Shard);
  template <SagaOp::T Op, typename KeyIDs_, int Lookup = -1, typename T, typename L>
  void mutate(Table<T> *, Shard, ZmRef<Row<T>>, L &&);
  template <SagaOp::T Op, typename KeyIDs_, int Lookup, typename T, typename L>
  void mutate_(Table<T> *, Shard, ZmRef<Row<T>>, UN, bool, L &&);
  template <
    SagaOp::T Op, unsigned KeyID, typename KeyIDs_, typename T, typename L>
  void findMutate(
    Table<T> *, Shard, typename Table<T>::template Key<KeyID>, L &&);
  template <
    SagaOp::T Op, unsigned KeyID, typename KeyIDs_, typename T, typename L>
  void findMutate_(Table<T> *, Shard, L &&);
};

struct SagaStepComplete {
  ZmRef<Saga>	saga;
  uint64_t	epoch = 0;
  uint32_t	step = 0;

  void operator ()(bool);
};

struct SagaNode__ : public ZmPolymorph {
  ZmRef<Saga> saga;

  SagaNode__(ZmRef<Saga> saga_) : saga{ZuMv(saga_)} { }
};

ZuDerive(SagaList,
  (ZmList<SagaNode__,
    ZmListNode<SagaNode__,
      ZmListShadow<>>>));

inline const SagaKey &SagaKeyAxor(const SagaList::Node &node) {
  return static_cast<const SagaNode__ &>(node).saga->key();
}

ZuDerive(SagaHash,
  (ZmHash<SagaList::Node,
    ZmHashNode<SagaList::Node,
      ZmHashKey<SagaKeyAxor,
	ZmHashHeapID<"">>>>));

template <typename Heap>
struct SagaNode_ : public Heap, public SagaHash::Node {
  ZuDerive_(SagaNode_, SagaHash::Node)
};
using SagaNodeHeap = ZmHeap<"Zdb.Saga.Node", SagaNode_<ZuVoid>>;
ZuDerive(SagaNode, (SagaNode_<SagaNodeHeap>));

template <typename Sagas>
using SagaUnion =
  ZuTypeApply<ZuUnion, typename Sagas::template Unshift<void>>;

template <typename Sagas, typename Heap>
struct MSaga_ : public Heap, public Saga {
  using Union = SagaUnion<Sagas>;

  Union u;

  template <typename S>
  void init(S &&s) {
    using T = ZuDecay<S>;
    new (u.template new_<T>()) T(ZuFwd<S>(s));
  }
};

template <typename Sagas>
using MSagaHeap = ZmHeap<"Zdb.Saga", MSaga_<Sagas, ZuVoid>>;

template <typename Sagas> struct MSaga;

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

  template <typename Sagas> void load(SagaTypeStep);
  template <typename Sagas> void end();
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

ZuDerive(SagaUNHash,
  (ZmHash<SagaRec__,
    ZmHashNode<SagaRec__,
      ZmHashKey<SagaUNAxor,
	ZmHashShadow<ZmHashHeapID<"">>>>>));
ZuDerive(SagaStepHash,
  (ZmHash<SagaUNHash::Node,
    ZmHashNode<SagaUNHash::Node,
      ZmHashKey<SagaStepAxor, ZmHashHeapID<"">>>>));

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
    ZuDeclVal<U &>().template operator()<I{}>(
      nullptr, nullptr, [](bool) { }));

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

template <typename ...S>
struct MSaga<ZuTypeList<S...>> :
  public MSaga_<ZuTypeList<S...>, MSagaHeap<ZuTypeList<S...>>> {
  ZuDerive_(MSaga, (MSaga_<ZuTypeList<S...>, MSagaHeap<ZuTypeList<S...>>>))
  using Sagas = ZuTypeList<S...>;
  using Types = SagaTypes<Sagas>;
  using M = MSaga<Sagas>;
  using Saga::type;

  static_assert(sizeof...(S) > 0, "empty saga bundle");
  static_assert(ZuTypeUnique<Types>::N == sizeof...(S),
    "duplicate saga type");
  static_assert(SagaDefsValid_<Sagas>{}, "invalid saga definition");

  static int match(ZuCSpan type) {
    static constexpr auto matcher = ZuMatcher<Types>();
    return matcher.exact(type);
  }

  static bool catalog(unsigned type, unsigned step, SagaTypeStep &row) {
    if (type >= sizeof...(S)) return false;
    return ZuSwitch::dispatch<sizeof...(S)>(type, [step, &row](auto I) {
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
    bool loaded = ZuSwitch::dispatch<sizeof...(S)>(unsigned(i),
	[saga = saga.ptr(), data](auto I) {
	  using Def = ZuType<I, Sagas>;
	  auto fbo = ZfbStruct::verify<Def>(data);
	  if (ZuUnlikely(!fbo)) return false;
	  new (saga->u.template new_<Def>()) Def{ZfbStruct::ctor<Def>(fbo)};
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

  static void run(void *context, ZmRef<Saga> saga_) {
    auto saga = static_cast<M *>(saga_.ptr());
    saga->u.dispatch([context, saga = ZuMv(saga_)](
	auto, auto &def) mutable {
	using Def = ZuDecay<decltype(def)>;
	auto ptr = saga.ptr();
	auto step = ptr->step();
	if (ZuLikely(step < Def::NSteps)) {
	  SagaStepComplete complete{ZuMv(saga), ptr->epoch(), step};
	  ZuSwitch::dispatch<Def::NSteps>(step,
	      [context, ptr, &def, complete = ZuMv(complete)](auto I) mutable {
	    (void)def.template operator()<I>(
	      context, ptr, ZuMv(complete));
	  });
	  return;
	}
	if (ZuLikely(step == Def::NSteps)) {
	  ptr->done();
	  return;
	}
	ptr->fail(ZeEXCEPT(Fatal, "Zdb", "invalid saga step"));
    });
  }

  static ZuCSpan type(const M *saga) {
    return saga->u.cdispatch([](auto, const auto &def) {
      using Def = ZuDecay<decltype(def)>;
      return ZuCSpan{typename Def::Type{}()};
    });
  }

  static unsigned stepCount(ZuCSpan type) {
    int i = match(type);
    if (ZuUnlikely(i < 0)) return 0;
    return ZuSwitch::dispatch<sizeof...(S)>(unsigned(i), [](auto I) {
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
    return ZuSwitch::dispatch<sizeof...(S)>(unsigned(i),
	[step, &table, &op](auto I) {
	  using Def = ZuType<I, Sagas>;
	  return stepDef_<SagaSteps<Def>>(step, table, op);
	});
  }

};

template <typename Sagas>
inline void SagaCatalog__::end()
{
  if (error || typeIndex < 0) return;
  auto expected = MSaga<Sagas>::stepCount(type);
  if (step != expected)
    error = ZeEXCEPT(Fatal, "Zdb", ([
      type = ZeString{type}, step = step, expected
    ](auto &s) {
      s << "saga type \"" << type << "\" has " << step
	<< " stored steps, expected " << expected;
    }));
}

template <typename Sagas>
inline void SagaCatalog__::load(SagaTypeStep row)
{
  if (error) return;
  if (row.type != type) {
    end<Sagas>();
    if (error) return;
    type = ZuMv(row.type);
    step = 0;
    typeIndex = MSaga<Sagas>::match(type);
    if (typeIndex >= 0) seen[typeIndex] = 1;
  }
  if (typeIndex >= 0) {
    ZuCSpan table;
    SagaOp::T op = SagaOp::Invalid;
    if (row.step != step ||
	!MSaga<Sagas>::stepDef(type, row.step, table, op) ||
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
  template <unsigned Step, typename Complete> \
  ZuIfT<Step == step_, \
    ZdbSagaStep_<ZuStringT<ZuPP_Q(table)>, ZdbSagaOp::op>> \
  operator ()(void *context_, ZdbSaga *saga, Complete &&complete)
using ZdbSagaSubmitResult = Zdb_::SagaSubmitResult;
using ZdbSagaSubmitFn = Zdb_::SagaSubmitFn;
using ZdbSagaHandler = Zdb_::SagaHandler;
using ZdbSaga = Zdb_::Saga;
template <typename Sagas>
using ZdbMSaga = Zdb_::MSaga<Sagas>;

#endif /* ZdbSaga_HH */
