//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <libintl.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmRing.hh>
#include <zlib/ZmHeap.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtPlatform.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZfCLI.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZvRingParams.hh>

#include <zlib/Zws.hh>

#include <zlib/ZtcFB.hh>
#include <zlib/ZtcDB.hh>
#include <zlib/ZtcMsg.hh>

#include <zlib/ZGtkApp.hh>
#include <zlib/ZGtkCallback.hh>
#include <zlib/ZGtkTreeModel.hh>
#include <zlib/ZGtkValue.hh>

#include "zdash_oauth.hh"
#include "zdash_module.hh"
#include <zlib/ZiModule.hh>

// FIXME - css
//
// @define-color rag_red_bg #ff1515;

// FIXME
// - right-click row selection in tree -> watch
// - drag/drop rowset in watchlist -> graph
// - field select in graph
//   - defaults to no field selected
//   - until field selected, no trace

static void usage()
{
  static const char *usage =
    "Usage: zdash --config=CONFIG [--wss=URL] [--no-browser]\n";
  std::cerr << usage << std::flush;
  ZiLog::stop();
  Zm::exit(1);
}

static void sigint();

namespace ZDash {

ZuDerive(String, (ZtString<ZtStringHeapID<"ZDash.String">>));
ZuDerive(Secret, (ZtString<ZtStringSecret<true,
  ZtStringHeapID<"ZDash.String">>>));

// Bound network reassembly and queued telemetry independently of ring size.
enum { FrameMax = 1U << 20, QueuedInputMax = 1U << 22 };
using Frame = ZmRef<ZiIOBuf>;
using FrameBuf = ZiIOBufAlloc<1024, FrameMax, "ZDash.Frame">;

struct Subscription {
  using Filter = Ztc::RequestFilter;
  using Group = Ztc::fbs::Group;
  String	deviceID;
  ZuID		publisherID;
  Filter	filter{"*"};
  uint64_t	id = 1;
  unsigned	interval = 1000;
  Group		group = Group::App;
};

static Frame requestFrame(const Subscription &sub, bool subscribe)
{
  Zfb::IOBuilder builder{Frame{new FrameBuf}};
  auto device = sub.deviceID ? Zfb::Save::str(builder, sub.deviceID) :
    Zfb::Offset<flatbuffers::String>{};
  auto filter = Zfb::Save::str(builder, sub.filter);
  auto publisher = sub.publisherID ? Zfb::Save::str(builder, sub.publisherID) :
    Zfb::Offset<flatbuffers::String>{};
  // An inventory request must omit id, not encode an empty string. The
  // generated builder preserves that distinction at this protocol boundary.
  Ztc::fbs::RequestBuilder requestBuilder{builder};
  requestBuilder.add_seqNo(0);
  requestBuilder.add_group(sub.group);
  requestBuilder.add_filter(filter);
  requestBuilder.add_id(publisher);
  requestBuilder.add_interval(subscribe ? sub.interval : 0);
  requestBuilder.add_subscribe(subscribe);
  auto request = requestBuilder.Finish();
  builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::Request,
    request.Union(), sub.id, device));
  return builder.buf();
}

static bool accepts(const Subscription &sub, const Ztc::fbs::Msg *msg)
{
  if (!Ztc::validMsg(msg) || msg->subId() != sub.id) return false;
  auto device = Zfb::Load::str(msg->deviceId());
  if (sub.deviceID && (device != sub.deviceID || !msg->agentGen()))
    return false;
  switch (msg->body_type()) {
    case Ztc::fbs::Body::Ack:
    case Ztc::fbs::Body::Error:
      return true;
    case Ztc::fbs::Body::EOS:
      // Snapshot completion may be per source or for the entire inventory.
      return (device && msg->agentGen()) ||
	(!sub.deviceID && !device && !msg->agentGen() &&
	  Zfb::Load::str(msg->body_as_EOS()->id()) == "inventory");
    case Ztc::fbs::Body::Telemetry: {
      if (!device || !msg->agentGen()) return false;
      auto tel = msg->body_as_Telemetry();
      if (sub.publisherID && Zfb::Load::str(tel->id()) != sub.publisherID)
	return false;
      using namespace Ztc::fbs;
      switch (tel->value_type()) {
	case TelemetryBody::AppTelemetry: return sub.group == Group::App;
	case TelemetryBody::HeapTelemetry: return sub.group == Group::Heap;
	case TelemetryBody::HashTelemetry: return sub.group == Group::Hash;
	case TelemetryBody::ThreadTelemetry: return sub.group == Group::Thread;
	case TelemetryBody::MxTelemetry:
	case TelemetryBody::CxnTelemetry: return sub.group == Group::Mx;
	case TelemetryBody::QueueTelemetry: return sub.group == Group::Queue;
	case TelemetryBody::HubTelemetry:
	case TelemetryBody::LinkTelemetry:
	case TelemetryBody::PoolTelemetry: return sub.group == Group::Hub;
	case TelemetryBody::DBTelemetry:
	case TelemetryBody::DBHostTelemetry:
	case TelemetryBody::DBTableTelemetry: return sub.group == Group::DB;
	case TelemetryBody::AlertTelemetry: return sub.group == Group::Alert;
	case TelemetryBody::Shutdown: return true;
	default: return false;
      }
    }
    default: return false;
  }
}

// Dedicated application shards; the transport uses scheduler threads 1/2.
enum { RxSID = 3, TxSID = 4, GtkSID = 5 };
// Amortize clock reads while bounding GTK work.
enum { RefreshBatch = 16 };

struct AppCf {
  ZiRingParams	telRing;
  String	gtkGlade = "zdash.glade";
  String	gtkStyle;
  String	wssURL;
  String	caPath;
  String	deviceID;
  String	group = "App";
  ZuID		publisherID;
  String	filter = "*";
  unsigned	gtkRefresh = 1;
  unsigned	gtkThread = 5;
  unsigned	queueBytes = QueuedInputMax;
  unsigned	interval = 1000;
  unsigned	alertRows = 1000;
};

ZfStruct(, (AppCf, Cf),
  (((telRing)),					(UDT)),
  (((gtkGlade)),				(String, "zdash.glade")),
  (((gtkStyle)),				(String)),
  (((gtkRefresh), ((Range<1U, 60000U>))),	(UInt32, 1)),
  (((gtkThread)),				(UInt32, 5)),
  (((queueBytes), ((Range<unsigned(FrameMax), 1U<<28>))),	(UInt32, QueuedInputMax)),
  (((interval), ((Range<1U, 3600000U>))),	(UInt32, 1000)),
  (((alertRows), ((Range<1U, 1000000U>))),	(UInt32, 1000)),
  (((wssURL)),					(String)),
  (((caPath)),					(String)),
  (((deviceID)),				(String)),
  (((group)),					(String, "App")),
  (((publisherID)),				(String)),
  (((filter)),					(String, "*")));

struct Options {
  String	config;
  String	wssURL;
  String	deviceID;
  String	caPath;
  bool		noBrowser = false;
  bool		help = false;
};
ZfStruct(, (Options, CLI),
  (((config), (CLI::Long<"config">)),			(String)),
  (((wssURL), (CLI::Long<"wss">)),			(String)),
  (((deviceID), (CLI::Long<"device-id">)),		(String)),
  (((caPath), (CLI::Long<"ca">)),			(String)),
  (((noBrowser), (CLI::Long<"no-browser">)),		(Bool)),
  (((help), (CLI::Flag<'h'>, CLI::Long<"help">)),	(Bool)));

namespace Telemetry {
using TypeList = ZuTypeList<
  Ztc::HeapTelemetry, Ztc::HashTelemetry, Ztc::ThreadTelemetry,
  Ztc::MxTelemetry, Ztc::CxnTelemetry, Ztc::QueueTelemetry,
  Ztc::HubTelemetry, Ztc::LinkTelemetry, Ztc::DBTableTelemetry,
  Ztc::DBHostTelemetry, Ztc::DBTelemetry, Ztc::AppTelemetry,
  Ztc::AlertTelemetry, Ztc::PoolTelemetry>;
using FBTypeList = ZuTypeMap<ZfbType, TypeList>;

template <typename Data> struct Item__ {
  static constexpr auto Axor = ZuFieldAxor<Data>();
  static decltype(auto) telKey(const Data &data) { return Axor(data); }
  using TelKey = ZuRDecay<decltype(telKey(ZuDeclVal<const Data &>()))>;
  static int rag(const Data &data) { return data.rag(); }
};
template <> struct Item__<Ztc::AppTelemetry> {
  using TelKey = ZuTuple<ZuCSpan, ZuCSpan, uint64_t>;
  // Source owns these immutable strings and outlives the item and GTK row.
  ZuCSpan	publisher_;
  ZuCSpan	device_;
  uint64_t	generation_ = 0;

  void initTelKey(ZuCSpan publisher, ZuCSpan device, uint64_t generation) {
    publisher_ = publisher;
    device_ = device;
    generation_ = generation;
  }
  TelKey telKey(const Ztc::AppTelemetry &) const {
    return {publisher_, device_, generation_};
  }
  static int rag(const Ztc::AppTelemetry &data) {
    return data.rag;
  }
};
template <> struct Item__<Ztc::DBTelemetry> {
  using TelKey = ZuTuple<const char *>;
  static TelKey telKey(const Ztc::DBTelemetry &) {
    return TelKey{"dbenv"};
  }
  static int rag(const Ztc::DBTelemetry &) {
    return Ztc::RAG::Off;
  }
};
template <typename Data_> struct Item_ : public Item__<Data_> {
  using Base = Item__<Data_>;
  using Data = Data_;
  using TelKey = typename Base::TelKey;

  Data			value;
  void			*gtkRow_ = nullptr;

  Item_() = default;
  template <typename FBType>
  Item_(FBType *fbo) : value{ZfbStruct::ctor<Data>(fbo)} { }

  Item_(const Item_ &) = delete;
  Item_ &operator =(const Item_ &) = delete;
  Item_(Item_ &&) = delete;
  Item_ &operator =(Item_ &&) = delete;

  template <typename T>
  T *gtkRow() const { return static_cast<T *>(gtkRow_); }
  template <typename T>
  void gtkRow(T *node) { gtkRow_ = node; }

  TelKey telKey() const { return Base::telKey(value); }
  int rag() const { return Base::rag(value); }

};

template <typename T, typename Heap = ZuVoid>
struct SingletonItem_ : public Heap, public Item_<T> {
  using Item_<T>::Item_;
};
template <typename T>
ZuDerive(SingletonItem_Heap, (ZmHeap<"ZDash.Singleton", SingletonItem_<T>>));
template <typename T>
ZuDerive(SingletonItem, (SingletonItem_<T, SingletonItem_Heap<T>>));

template <typename T>
static typename T::TelKey itemKey(const T &v) { return v.telKey(); }
ZmRBTreeDeriveT((T), ItemTree_, Item_<T>,
  (ZmRBTreeNode<Item_<T>,
    ZmRBTreeKey<ZDash::Telemetry::itemKey<Item_<T>>,
      ZmRBTreeUnique<true,
	ZmRBTreeLock<ZmNoLock,
	  ZmRBTreeHeapID<"ZDash.ItemTree">>>>>));
template <typename T>
class ItemTree : public ItemTree_<T> {
public:
  using Node = typename ItemTree_<T>::Node;
  void add(Node *node) { this->addNode(node); }
  template <typename FBType>
  Node *lookup(const FBType *fbo) const {
    return this->findPtr(ZuStructKey(*fbo));
  }
};
template <typename T> class ItemSingleton {
  ItemSingleton(const ItemSingleton &) = delete;
  ItemSingleton &operator =(const ItemSingleton &) = delete;
  ItemSingleton(ItemSingleton &&) = delete;
  ItemSingleton &operator =(ItemSingleton &&) = delete;
public:
  using Node = SingletonItem<T>;
  ItemSingleton() = default;
  ~ItemSingleton() { if (m_node) delete m_node; }
  template <typename FBType>
  Node *lookup(const FBType *) const { return m_node; }
  Node *get() const { return m_node; }
  void add(Node *node) { if (m_node) delete m_node; m_node = node; }
private:
  Node	*m_node = nullptr;
};
struct AlertArray {
  using T = Ztc::AlertTelemetry;
  using Node = T;
  using Data = ZtArray<T, ZtArrayHeapID<"ZDash.Alert">>;
  Data	data;

  AlertArray(const AlertArray &) = delete;
  AlertArray &operator =(const AlertArray &) = delete;
  AlertArray(AlertArray &&) = delete;
  AlertArray &operator =(AlertArray &&) = delete;
  AlertArray() = default;
  ~AlertArray() = default;
};

template <typename U> struct Container_ { // default
  using T = ItemTree<U>;
};
template <> struct Container_<Ztc::AppTelemetry> {
  using T = ItemSingleton<Ztc::AppTelemetry>;
};
template <> struct Container_<Ztc::DBTelemetry> {
  using T = ItemSingleton<Ztc::DBTelemetry>;
};
template <> struct Container_<Ztc::AlertTelemetry> {
  using T = AlertArray;
};
template <typename U>
using Container = typename Container_<U>::T;

using ContainerTL = ZuTypeMap<Container, TypeList>;
using Containers = ZuTypeApply<ZuTuple, ContainerTL>;

template <typename T> using Item = typename Container<T>::Node;
} // Telemetry

namespace GtkTree {
template <typename Impl, typename Item>
struct Row {
  Item	*item = nullptr;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Row() = default;
  Row(Item *item_) : item{item_} { init_(); }
  void init(Item *item_) { item = item_; init_(); }
  void init_() { item->gtkRow(impl()); }

  using TelKey = typename Item::TelKey;
  TelKey telKey() const { return item->telKey(); }
  int rag() const { return item->rag(); }
  int cmp(const Impl &v) const {
    return item->telKey().cmp(v.telKey());
  }
};

template <unsigned Depth, typename Item> struct Leaf;
template <unsigned Depth, typename Item, typename Heap = ZuVoid>
struct Leaf_ : public Heap,
    public Row<Leaf<Depth, Item>, Item>,
    public ZGtk::TreeHierarchy::Leaf<Leaf<Depth, Item>, Depth> {
  Leaf_() = default;
  Leaf_(Item *item) : Row<Leaf<Depth, Item>, Item>{item} { }
  using Row<Leaf<Depth, Item>, Item>::cmp;
};
template <unsigned Depth, typename Item>
ZuDerive(Leaf_Heap, (ZmHeap<"ZDash.Leaf", Leaf_<Depth, Item>>));
template <unsigned Depth, typename Item>
ZuDerive(Leaf, (Leaf_<Depth, Item, Leaf_Heap<Depth, Item>>));

template <unsigned Depth, typename Item, typename Child> struct Parent;
template <unsigned Depth, typename Item, typename Child,
  typename Heap = ZuVoid>
struct Parent_ : public Heap,
    public Row<Parent<Depth, Item, Child>, Item>,
    public ZGtk::TreeHierarchy::Parent<
      Parent<Depth, Item, Child>, Depth, Child> {
  Parent_() = default;
  Parent_(Item *item) : Row<Parent<Depth, Item, Child>, Item>{item} { }
  using Row<Parent<Depth, Item, Child>, Item>::cmp;
  using Base = ZGtk::TreeHierarchy::Parent<Parent<Depth, Item, Child>, Depth, Child>;
  using Base::add;
  using Base::del;
};
template <unsigned Depth, typename Item, typename Child>
ZuDerive(Parent_Heap, (ZmHeap<"ZDash.Parent", Parent_<Depth, Item, Child>>));
template <unsigned Depth, typename Item, typename Child>
ZuDerive(Parent, (Parent_<Depth, Item, Child, Parent_Heap<Depth, Item, Child>>));

template <unsigned Depth, typename Item, typename Tuple> struct Branch;
template <unsigned Depth, typename Item, typename Tuple,
  typename Heap = ZuVoid>
struct Branch_ : public Heap,
    public Row<Branch<Depth, Item, Tuple>, Item>,
    public ZGtk::TreeHierarchy::Branch<
      Branch<Depth, Item, Tuple>, Depth, Tuple> {
  Branch_() = default;
  Branch_(Item *item) : Row<Branch<Depth, Item, Tuple>, Item>{item} { }
  using Row<Branch<Depth, Item, Tuple>, Item>::cmp;
  using Base = ZGtk::TreeHierarchy::Branch<Branch<Depth, Item, Tuple>, Depth, Tuple>;
  using Base::add;
  using Base::del;
};
template <unsigned Depth, typename Item, typename Tuple>
ZuDerive(Branch_Heap, (ZmHeap<"ZDash.Branch", Branch_<Depth, Item, Tuple>>));
template <unsigned Depth, typename Item, typename Tuple>
ZuDerive(Branch, (Branch_<Depth, Item, Tuple, Branch_Heap<Depth, Item, Tuple>>));
template <typename TelKey_>
struct BranchChild {
  using TelKey = TelKey_;
  static int rag() { return Ztc::RAG::Off; }
};

template <typename T> using TelItem = Telemetry::Item<T>;

using Heap = Leaf<3, TelItem<Ztc::HeapTelemetry>>;
using HashTbl = Leaf<3, TelItem<Ztc::HashTelemetry>>;
using Thread = Leaf<3, TelItem<Ztc::ThreadTelemetry>>;
using Socket = Leaf<4, TelItem<Ztc::CxnTelemetry>>;
using Mx = Parent<3, TelItem<Ztc::MxTelemetry>, Socket>;
using Queue = Leaf<3, TelItem<Ztc::QueueTelemetry>>;
using Pool = Leaf<3, TelItem<Ztc::PoolTelemetry>>;
using Link = Leaf<4, TelItem<Ztc::LinkTelemetry>>;
using Engine = Parent<3, TelItem<Ztc::HubTelemetry>, Link>;
using DBHost = Leaf<4, TelItem<Ztc::DBHostTelemetry>>;
using DBTable = Leaf<4, TelItem<Ztc::DBTableTelemetry>>;

// DBTable hosts
struct DBHosts : public BranchChild<ZuTuple<const char *>> {
  static auto telKey() { return TelKey{"hosts"}; }
};
using DBHostParent = Parent<3, DBHosts, DBHost>;
// DBTables
struct DBTables : public BranchChild<ZuTuple<const char *>> {
  static auto telKey() { return TelKey{"tables"}; }
};
using DBTableParent = Parent<3, DBTables, DBTable>;

// heaps
struct Heaps :
    public BranchChild<ZuTuple<const char *, const char *, const char *>> {
  static auto telKey() { return TelKey{"heaps", "partition", "size"}; }
};
using HeapParent = Parent<2, Heaps, Heap>;
// hashTbls
struct HashTbls : public BranchChild<ZuTuple<const char *, const char *>> {
  static auto telKey() { return TelKey{"hashTbls", "addr"}; }
};
using HashTblParent = Parent<2, HashTbls, HashTbl>;
// threads
struct Threads : public BranchChild<ZuTuple<const char *>> {
  static auto telKey() { return TelKey{"threads"}; }
};
using ThreadParent = Parent<2, Threads, Thread>;
// multiplexers
struct Mxs : public BranchChild<ZuTuple<const char *>> {
  static auto telKey() { return TelKey{"multiplexers"}; }
};
using MxParent = Parent<2, Mxs, Mx>;
// queues
struct Queues : public BranchChild<ZuTuple<const char *, const char *>> {
  static auto telKey() { return TelKey{"queues", "type"}; }
};
using QueueParent = Parent<2, Queues, Queue>;
struct Pools : public BranchChild<ZuTuple<const char *>> {
  static auto telKey() { return TelKey{"pools"}; }
};
using PoolParent = Parent<2, Pools, Pool>;
// engines
struct Engines : public BranchChild<ZuTuple<const char *>> {
  static auto telKey() { return TelKey{"engines"}; }
};
using EngineParent = Parent<2, Engines, Engine>;

// db
ZuDeclTuple(DBTuple,
    (DBHostParent, hosts),
    (DBTableParent, tables));
using DB = Branch<2, TelItem<Ztc::DBTelemetry>, DBTuple>;
// applications
ZuDeclTuple(AppTuple,
    (HeapParent, heaps),
    (HashTblParent, hashTbls),
    (ThreadParent, threads),
    (MxParent, mxs),
    (QueueParent, queues),
    (PoolParent, pools),
    (EngineParent, engines),
    (DB, db));
using App = Branch<1, TelItem<Ztc::AppTelemetry>, AppTuple>;

struct Root : public ZGtk::TreeHierarchy::Parent<Root, 0, App> { };

// map telemetry items to corresponding tree nodes
using RowTypes = ZuTypeList<
  Heap, HashTbl, Thread, Mx, Socket, Queue, Engine, Link,
  DBTable, DBHost, DB, App, void, Pool>;
ZuAssert(RowTypes::N == Telemetry::TypeList::N);
template <typename Item>
auto row(Item *item) {
  using T = ZuType<
    ZuTypeIndex<typename Item::Data, Telemetry::TypeList>{}, RowTypes>;
  return item->template gtkRow<T>();
}

enum { Depth = 5 };

// (*) - branch
using Iter = ZuUnion<
  // Root *,
  App *,		// [app]

  // app children
  HeapParent *,	// app->heaps (*)
  HashTblParent *,	// app->hashTbls (*)
  ThreadParent *,	// app->threads (*)
  MxParent *,		// app->mxs (*)
  QueueParent *,	// app->queues (*)
  PoolParent *,	// app->pools (*)
  EngineParent *,	// app->engines (*)
  DB *,		// app->db (*)

  // app grandchildren
  Heap *,		// app->heaps->[heap]
  HashTbl *,		// app->hashTbls->[hashTbl]
  Thread *,		// app->threads->[thread]
  Mx *,		// app->mxs->[mx]
  Queue *,		// app->queues->[queue]
  Pool *,		// app->pools->[pool]
  Engine *,		// app->engines->[engine]

  // app great-grandchildren
  Socket *,		// app->mxs->mx->[socket]
  Link *,		// app->engines->engine->[link]

  // DB children
  DBHostParent *,	// app->db->hosts (*)
  DBTableParent *,	// app->db->tables (*)

  // DB grandchildren
  DBHost *,		// app->db->hosts->[host]
  DBTable *>;		// app->db->tables->[table]

class Model : public ZGtk::TreeHierarchy::Model<Model, Iter, Depth> {
  using Base = ZGtk::TreeHierarchy::Model<Model, Iter, Depth>;
public:
  // parent() - child->parent type map

  template <typename T,
    typename = ZuIfT<
      (ZuIsSame<T, App>{}) ||
      (ZuIsSame<T, HeapParent>{} || ZuIsSame<T, HashTblParent>{} || ZuIsSame<T,
	ThreadParent>{} || ZuIsSame<T, MxParent>{} || ZuIsSame<T, QueueParent>{} ||
	ZuIsSame<T, PoolParent>{} || ZuIsSame<T, EngineParent>{} || ZuIsSame<T, DB>{}) ||
      (ZuIsSame<T, Heap>{}) ||
      (ZuIsSame<T, HashTbl>{}) ||
      (ZuIsSame<T, Thread>{}) ||
      (ZuIsSame<T, Mx>{}) ||
      (ZuIsSame<T, Queue>{}) ||
      (ZuIsSame<T, Pool>{}) ||
      (ZuIsSame<T, Engine>{}) ||
      (ZuIsSame<T, Socket>{}) ||
      (ZuIsSame<T, Link>{}) ||
      (ZuIsSame<T, DBHostParent>{} || ZuIsSame<T, DBTableParent>{}) ||
      (ZuIsSame<T, DBHost>{}) ||
      (ZuIsSame<T, DBTable>{})>>
  static auto *parent(void *ptr) {
    if constexpr (ZuIsSame<T, App>{}) {
      return static_cast<Root *>(ptr);
    } else if constexpr (ZuIsSame<T, HeapParent>{} || ZuIsSame<T, HashTblParent>{} ||
      ZuIsSame<T, ThreadParent>{} || ZuIsSame<T, MxParent>{} || ZuIsSame<T,
      QueueParent>{} || ZuIsSame<T, PoolParent>{} || ZuIsSame<T, EngineParent>{} ||
      ZuIsSame<T, DB>{}) {
      return static_cast<App *>(ptr);
    } else if constexpr (ZuIsSame<T, Heap>{}) {
      return static_cast<HeapParent *>(ptr);
    } else if constexpr (ZuIsSame<T, HashTbl>{}) {
      return static_cast<HashTblParent *>(ptr);
    } else if constexpr (ZuIsSame<T, Thread>{}) {
      return static_cast<ThreadParent *>(ptr);
    } else if constexpr (ZuIsSame<T, Mx>{}) {
      return static_cast<MxParent *>(ptr);
    } else if constexpr (ZuIsSame<T, Queue>{}) {
      return static_cast<QueueParent *>(ptr);
    } else if constexpr (ZuIsSame<T, Pool>{}) {
      return static_cast<PoolParent *>(ptr);
    } else if constexpr (ZuIsSame<T, Engine>{}) {
      return static_cast<EngineParent *>(ptr);
    } else if constexpr (ZuIsSame<T, Socket>{}) {
      return static_cast<Mx *>(ptr);
    } else if constexpr (ZuIsSame<T, Link>{}) {
      return static_cast<Engine *>(ptr);
    } else if constexpr (ZuIsSame<T, DBHostParent>{} || ZuIsSame<T, DBTableParent>{}) {
      return static_cast<DB *>(ptr);
    } else if constexpr (ZuIsSame<T, DBHost>{}) {
      return static_cast<DBHostParent *>(ptr);
    } else {
      return static_cast<DBTableParent *>(ptr);
    }
  }


  enum { RAGCol = 0, IDCol0, IDCol1, IDCol2, NCols };

  static Model *ctor() {
    auto model = Base::ctor();
    // Release GObject's notification data as well as the C++ tree members.
    G_OBJECT_GET_CLASS(model)->finalize = [](GObject *object) {
      auto parent = G_OBJECT_CLASS(
	g_type_class_peek_parent(G_OBJECT_GET_CLASS(object)));
      reinterpret_cast<Model *>(object)->~Model();
      parent->finalize(object);
    };
    return model;
  }

  // root()
  Root *root() { return &m_root; }

  // Construct current ZuUnion iterators locally; retain ZGtk's tree storage.
  gboolean get_iter(GtkTreeIter *iter, GtkTreePath *path) {
    auto depth = gtk_tree_path_get_depth(path);
    if (depth <= 0 || depth > Depth) return false;
    return m_root.descend(gtk_tree_path_get_indices(path), depth,
      [iter](auto ptr) {
	using T = ZuDecay<decltype(*ptr)>;
	new (iter) Iter{const_cast<T *>(ptr)};
      });
  }
  GtkTreePath *get_path(GtkTreeIter *iter) {
    gint indices[Depth]; // bounded by the compile-time GTK hierarchy
    unsigned depth = 0;
    reinterpret_cast<Iter *>(iter)->cdispatch(
      [&indices, &depth](auto, auto ptr) {
	depth = ZuDecay<decltype(*ptr)>::Depth;
	ptr->template ascend<Model>(indices);
      });
    return gtk_tree_path_new_from_indicesv(indices, depth);
  }
  gboolean iter_nth_child(
      GtkTreeIter *iter, GtkTreeIter *parent, gint i) {
    auto child = [iter](auto ptr) {
      using T = ZuDecay<decltype(*ptr)>;
      new (iter) Iter{const_cast<T *>(ptr)};
    };
    if (!parent) return m_root.child(i, child);
    return reinterpret_cast<Iter *>(parent)->cdispatch(
      [i, &child](auto, auto ptr) { return ptr->child(i, child); });
  }
  gboolean iter_children(GtkTreeIter *iter, GtkTreeIter *parent) {
    return iter_nth_child(iter, parent, 0);
  }
  gboolean iter_parent(GtkTreeIter *iter, GtkTreeIter *child) {
    return reinterpret_cast<Iter *>(child)->cdispatch(
      [iter](auto, auto ptr) {
	auto parent = ptr->template parent<Model>();
	if (!parent) return false;
	new (iter) Iter{parent};
	return true;
      });
  }
  template <typename Row>
  void updated(Row *row) {
    GtkTreeIter iter;
    new (&iter) Iter{row};
    auto path = get_path(&iter);
    gtk_tree_model_row_changed(GTK_TREE_MODEL(this), path, &iter);
    gtk_tree_path_free(path);
  }
  template <typename Row>
  void del(Row *row) {
    GtkTreeIter iter;
    new (&iter) Iter{row};
    auto path = get_path(&iter);
    if constexpr (Row::Depth == 1) m_root.del(row);
    else row->template parent<Model>()->del(row);
    gtk_tree_model_row_deleted(GTK_TREE_MODEL(this), path);
    gtk_tree_path_free(path);
  }

  // key printing
  template <typename Key>
  struct KeyPrint_ {
    Key key;

    template <typename Key_>
    KeyPrint_(Key_ &&key_) : key{ZuFwd<Key_>(key_)} { }
    auto p0() const { return key.template p<0>(); }
    template <unsigned N = Key::N, ZuIfT<(N <= 1), int> = 0>
    auto p1() const { return ""; }
    template <unsigned N = Key::N, ZuIfT<(N > 1), int> = 0>
    auto p1() const { return key.template p<1>(); }
    template <unsigned N = Key::N, ZuIfT<(N <= 2), int> = 0>
    auto p2() const { return ""; }
    template <unsigned N = Key::N, ZuIfT<(N > 2), int> = 0>
    auto p2() const { return key.template p<2>(); }
  };
  // generic key printing
  template <typename Key>
  struct KeyPrint : public KeyPrint_<Key> {
    using KeyPrint_<Key>::KeyPrint_;
  };

  // override addr for hash tables
  template <typename Key>
  struct HashTblKeyPrint : public KeyPrint_<Key> {
    using KeyPrint_<Key>::KeyPrint_;
    auto p1() { return ZuBoxed(this->key.template p<1>()).hex(); }
  };

  // override type for queues
  template <typename Key>
  struct QueueKeyPrint : public KeyPrint_<Key> {
    using KeyPrint_<Key>::KeyPrint_;
    auto p2() {
      return Ztc::QueueType::name(this->key.template p<2>());
    }
  };
  template <typename T, typename Key>
  static ZuIf<ZuIsSame<T, HashTbl>{}, HashTblKeyPrint<Key>,
    ZuIf<ZuIsSame<T, Queue>{}, QueueKeyPrint<Key>, KeyPrint<Key>>>
  keyPrintType();

  gint get_n_columns() { return NCols; }
  GType get_column_type(gint i) {
    switch (i) {
      case RAGCol: return G_TYPE_INT;
      case IDCol0: return G_TYPE_STRING;
      case IDCol1: return G_TYPE_STRING;
      case IDCol2: return G_TYPE_STRING;
      default: return G_TYPE_NONE;
    }
  }
  template <typename T>
  void value(const T *ptr, gint i, ZGtk::Value *v) {
    switch (i) {
      case RAGCol:
	v->init(G_TYPE_INT);
	v->set_int(ptr->rag());
	return;
      case IDCol0: case IDCol1: case IDCol2: break;
      default: return;
    }
    using Key = decltype(ptr->telKey());
    using KeyPrint = decltype(keyPrintType<T, Key>());
    KeyPrint print{ptr->telKey()};
    m_value.length(0);
    switch (i) {
      case IDCol0: m_value << print.p0(); break;
      case IDCol1: m_value << print.p1(); break;
      case IDCol2: m_value << print.p2(); break;
    }
    v->init(G_TYPE_STRING);
    // GTK owns this result; subsequent column reads reuse m_value.
    v->set_string(m_value);
  }

private:
  Root	m_root;		// root of tree
  String	m_value;	// re-used string buffer
};

class View {
  // GtkCellRenderer properties form a fixed ABI-shaped tuple, not a queue.
  enum { TextProp, BgProp, FgProp, NProps };
  View(const View &) = delete;
  View &operator =(const View &) = delete;
  View(View &&) = delete;
  View &operator =(View &&) = delete;
public:
  View() = default;
  ~View() = default;

private:
  template <unsigned RagCol, unsigned TextCol>
  void addCol(const char *id) {
    auto col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, gettext(id));

    auto cell = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, cell, true);

    gtk_tree_view_column_set_cell_data_func(col, cell, [](
	  GtkTreeViewColumn *col, GtkCellRenderer *cell,
	  GtkTreeModel *model, GtkTreeIter *iter, gpointer this_) {
      reinterpret_cast<View *>(this_)->render<RagCol, TextCol>(
	  col, cell, model, iter);
    }, this, nullptr);

    gtk_tree_view_append_column(m_treeView, col);

    // normally would add columns in order of saved column order;
    // and not add columns unselected by user
    //
    // need
    // - array of available columns (including ID)
    // - array of selected columns in display order
  }

  template <unsigned RagCol, unsigned TextCol>
  void render(
      GtkTreeViewColumn *col, GtkCellRenderer *cell,
      GtkTreeModel *model, GtkTreeIter *iter) {
    m_values[TextProp].unset();
    gtk_tree_model_get_value(model, iter, TextCol, &m_values[TextProp]);

    gint rag;
    {
      ZGtk::Value rag_;
      gtk_tree_model_get_value(model, iter, RagCol, &rag_);
      rag = rag_.get_int();
    }
    switch (rag) {
      case Ztc::RAG::Red:
	m_values[BgProp].set_static_boxed(&m_rag_red_bg);
	m_values[FgProp].set_static_boxed(&m_rag_red_fg);
	break;
      case Ztc::RAG::Amber:
	m_values[BgProp].set_static_boxed(&m_rag_amber_bg);
	m_values[FgProp].set_static_boxed(&m_rag_amber_fg);
	break;
      case Ztc::RAG::Green:
	m_values[BgProp].set_static_boxed(&m_rag_green_bg);
	m_values[FgProp].set_static_boxed(&m_rag_green_fg);
	break;
      default:
	m_values[BgProp].set_static_boxed(&m_rag_off_bg);
	m_values[FgProp].set_static_boxed(&m_rag_off_fg);
	break;
    }
    g_object_setv(G_OBJECT(cell), NProps, m_props, m_values);
  }

public:
  void init(GtkTreeView *view, GtkStyleContext *context) {
    m_treeView = view;

    if (!context || !gtk_style_context_lookup_color(
	  context, "rag_red_fg", &m_rag_red_fg))
      m_rag_red_fg = { 0.0, 0.0, 0.0, 1.0 }; // #000000
    if (!context || !gtk_style_context_lookup_color(
	  context, "rag_red_bg", &m_rag_red_bg))
      m_rag_red_bg = { 1.0, 0.0820, 0.0820, 1.0 }; // #ff1515

    if (!context || !gtk_style_context_lookup_color(
	  context, "rag_amber_fg", &m_rag_amber_fg))
      m_rag_amber_fg = { 0.0, 0.0, 0.0, 1.0 }; // #000000
    if (!context || !gtk_style_context_lookup_color(
	  context, "rag_amber_bg", &m_rag_amber_bg))
      m_rag_amber_bg = { 1.0, 0.5976, 0.0, 1.0 }; // #ff9900

    if (!context || !gtk_style_context_lookup_color(
	  context, "rag_green_fg", &m_rag_green_fg))
      m_rag_green_fg = { 0.0, 0.0, 0.0, 1.0 }; // #000000
    if (!context || !gtk_style_context_lookup_color(
	  context, "rag_green_bg", &m_rag_green_bg))
      m_rag_green_bg = { 0.1835, 0.8789, 0.2304, 1.0 }; // #2fe13b

    addCol<Model::RAGCol, Model::IDCol0>("ID");
    addCol<Model::RAGCol, Model::IDCol1>("");
    addCol<Model::RAGCol, Model::IDCol2>("");

    // GLib takes a mutable array of pointers to immutable property names.
    static const gchar *props[] = {
      "text", "background-rgba", "foreground-rgba"
    };
    ZuAssert(sizeof(props) / sizeof(*props) == NProps);

    m_props = props;
    m_values[BgProp].init(GDK_TYPE_RGBA);
    m_values[FgProp].init(GDK_TYPE_RGBA);

    {
      auto cell = gtk_cell_renderer_text_new();
      g_object_getv(G_OBJECT(cell), 2, &m_props[BgProp], &m_values[BgProp]);
      if (auto color = static_cast<const GdkRGBA *>(m_values[BgProp].get_boxed()))
	m_rag_off_bg = *color;
      if (auto color = static_cast<const GdkRGBA *>(m_values[FgProp].get_boxed()))
	m_rag_off_fg = *color;
      g_object_unref(G_OBJECT(cell));
    }

    g_signal_connect(
	G_OBJECT(m_treeView), "destroy",
	ZGtk::callback([](GObject *, gpointer this_) {
	  reinterpret_cast<View *>(this_)->destroyed();
	}), this);
  }

  void destroyed() {
    m_treeView = nullptr;
  }

  void final() { m_treeView = nullptr; }

  void bind(GtkTreeModel *model) {
    gtk_tree_view_set_model(m_treeView, model);
  }

private:
  GtkTreeView	*m_treeView = nullptr;
  GdkRGBA	m_rag_red_fg = { 0.0, 0.0, 0.0, 0.0 };
  GdkRGBA	m_rag_red_bg = { 0.0, 0.0, 0.0, 0.0 };
  GdkRGBA	m_rag_amber_fg = { 0.0, 0.0, 0.0, 0.0 };
  GdkRGBA	m_rag_amber_bg = { 0.0, 0.0, 0.0, 0.0 };
  GdkRGBA	m_rag_green_fg = { 0.0, 0.0, 0.0, 0.0 };
  GdkRGBA	m_rag_green_bg = { 0.0, 0.0, 0.0, 0.0 };
  GdkRGBA	m_rag_off_fg = { 0.0, 0.0, 0.0, 0.0 };
  GdkRGBA	m_rag_off_bg = { 0.0, 0.0, 0.0, 0.0 };
  const gchar	**m_props = nullptr;
  ZGtk::Value	m_values[NProps];
};
} // GtkTree

struct Source {
  using Containers = Telemetry::Containers;
  String	device;
  String	publisher;
  uint64_t	generation;
  Containers	telemetry;

  Source(ZuCSpan device_, ZuCSpan publisher_, uint64_t generation_) :
    device{device_}, publisher{publisher_}, generation{generation_} { }
  auto key() const { return ZuFwdTuple(device, publisher); }
};
static auto sourceKey(const Source &source) { return source.key(); }
ZmRBTreeDerive(Sources, Source,
  ZmRBTreeNode<Source, ZmRBTreeKey<sourceKey, ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmNoLock, ZmRBTreeHeapID<"ZDash.Source">>>>>);

// Keep the FlatBuffers payload aligned for its widest scalar. The Ztc
// envelope carries source identity, so no connection pointer crosses threads.
struct alignas(uint64_t) TelHdr {
  unsigned	length;

  ZuBSpan data() const {
    return {reinterpret_cast<const uint8_t *>(this + 1), length};
  }
};
static unsigned telSize(const void *ptr) {
  return sizeof(TelHdr) + static_cast<const TelHdr *>(ptr)->length;
}
ZuDerive(TelRing, (ZmRing<ZmRingSizeAxor<telSize>>));

class App : public ZGtk::App {
public:
  using Client = Zws::Client<App, Zhttp::H1TLS>;
  using Link = Client::Link;
  template <typename T> using TelItem = Telemetry::Item<T>;
  using AppItem = TelItem<Ztc::AppTelemetry>;
  using DBItem = TelItem<Ztc::DBTelemetry>;

  bool init(ZiMultiplex *mx, AppCf config, ZuCSpan caPath, ZuCSpan token,
      ModuleSession *module) {
    m_module = module;
    m_offline = module && module->offline;
    Zws::URI uri;
    if (!m_offline && (!Zws::URI::parse(uri, config.wssURL).ok() ||
	!uri.secure() || !uri.host || !uri.port || !uri.target || !token))
      return false;
    m_sub.deviceID = ZuMv(config.deviceID);
    m_sub.publisherID = config.publisherID;
    m_sub.filter = config.filter;
    m_sub.interval = config.interval;
    bool groupOK = false;
    for (auto group: Ztc::fbs::EnumValuesGroup()) {
      if (config.group != Ztc::fbs::EnumNameGroup(group)) continue;
      m_sub.group = group;
      groupOK = true;
      break;
    }
    if (!groupOK || (!m_sub.deviceID &&
	(m_sub.group != Ztc::fbs::Group::App || m_sub.publisherID ||
	  m_sub.filter != "*"))) return false;

    m_queueBytes = config.queueBytes;
    m_alertRows = config.alertRows;
    auto &ring = config.telRing;
    m_telRing.init(ZmRingParams{ring.size()}
	.ll(ring.ll()).spin(ring.spin()).timeout(ring.timeout()));
    if (m_telRing.open(TelRing::Read | TelRing::Write) != Zu::OK)
      return false;
    m_gladePath = ZuMv(config.gtkGlade);
    m_stylePath = ZuMv(config.gtkStyle);
    int64_t refreshRate = int64_t(config.gtkRefresh) * 1000000;
    m_refreshQuantum = ZuTime{ZuTime::Nano{refreshRate >> 1}};
    if (m_refreshQuantum < mx->params().quantum()) {
      m_refreshQuantum = mx->params().quantum();
      m_refreshRate = m_refreshQuantum + m_refreshQuantum;
    } else
      m_refreshRate = ZuTime{ZuTime::Nano{refreshRate}};
    if (config.gtkThread <= TxSID ||
	config.gtkThread > mx->params().nThreads())
      return false;

    i18n("zdash", DATADIR);
    attach(mx, config.gtkThread);
    m_attached = true;
    mx->run([this]() {
      gtkInit_();
      m_executed.post();
    }, config.gtkThread);
    m_executed.wait();
    if (!m_gtkReady) return false;
    if (m_offline) return true;

    Zws::Config ws;
    ws.maxMessage = FrameMax;
    ws.maxQueuedInput = QueuedInputMax;
    ws.handshakeTimeout = 10;
    ws.closeTimeout = 5;
    ws.pingInterval = 30;
    ws.pongTimeout = 60;
    Zhttp::H2Config tls;
    if (caPath) tls.caPath(caPath);
    if (!m_client.init(Zhttp::HubConfig{mx, "rx", "tx"}, tls, ws))
      return false;
    m_clientInited = true;
    if (!m_client.start()) return false;
    m_clientStarted = true;
    Secret authorization{"Bearer "};
    authorization << token;
    m_link = new Link{&m_client, uri, Ztc::Protocol, authorization};
    authorization.null();
    m_link->connect();
    return true;
  }

  void final() {
    if (m_offline && m_attached) {
      ZGtk::App::sched()->run([this]() {
	m_closing = true;
	m_executed.post();
      }, RxSID);
      m_executed.wait();
    }
    if (m_clientStarted) {
      m_client.rxRun([this]() {
	m_closing = true;
	if (m_connected) {
	  if (m_subscribed) send(*m_link, requestFrame(m_sub, false));
	  m_link->close();
	} else
	  m_down.post();
      });
      (void)m_down.timedwait(Zm::now(6));
      ZmSemaphore stopped;
      m_client.stop([&stopped](bool) { stopped.post(); });
      stopped.wait();
      m_clientStarted = false;
    }
    m_link = nullptr;
    if (m_clientInited) {
      m_client.final();
      m_clientInited = false;
    }
    if (m_attached) {
      // Rx is drained. Cancel the GTK timer, then drain queued callbacks
      // before detaching the GLib integration and releasing model state.
      gtkRun([this]() {
	m_gtkClosing = true;
	ZGtk::App::sched()->del(&m_refreshTimer);
	gtkRun([this]() {
	  detach({[this]() {
	    gtkFinal_();
	    m_executed.post();
	  }});
	});
      });
      m_executed.wait();
      m_attached = false;
    }
    m_telRing.close();
  }

  void post() { m_done.post(); }
  bool wait(unsigned timeout = 0) {
    if (!timeout) { m_done.wait(); return true; }
    if (!m_done.timedwait(Zm::now(timeout))) return true;
    m_failed = true;
    ZiLOG(Error, "zdash", "session timed out");
    return false;
  }
  bool failed() const { return m_failed; }

  template <typename L>
  static void send(L &link, Frame frame) {
    link.txStream([frame = ZuMv(frame)](auto &tx) {
      tx << ZuBSpan{frame->data(), frame->length};
      tx.flush();
    }, Zws::Opcode::Binary);
  }

  template <typename L>
  void connected(L &link, const Zhttp::ConnectedInfo &) {
    if (m_closing) { link.close(); return; }
    m_connected = true;
    send(link, requestFrame(m_sub, true));
    m_subscribed = true;
  }

  template <typename L>
  int reject(L &link, Zws::CloseCode::T code) {
    m_failed = true;
    link.close(code);
    post();
    return -1;
  }

  template <typename L>
  int messageStart(L &link, Zws::Opcode::T opcode) {
    if (opcode != Zws::Opcode::Binary)
      return reject(link, Zws::CloseCode::Unsupported);
    if (!m_frame) m_frame = new FrameBuf;
    m_frame->length = 0;
    return 1;
  }

  template <typename L, typename Rx>
  int process(L &link, Rx &rx) {
    return Zhttp::bodyEach(rx, [this, &link](ZuSpan<uint8_t> span) {
      if (span.length() > FrameMax - m_frame->length) {
	reject(link, Zws::CloseCode::TooLarge);
	return false;
      }
      auto length = m_frame->length + span.length();
      m_frame->append(span);
      if (m_frame->length == length) return true;
      reject(link, Zws::CloseCode::TooLarge);
      return false;
    }) ? 1 : -1;
  }

  template <typename L>
  int messageEnd(L &link) {
    auto code = receive_({m_frame->data(), m_frame->length}, true);
    return code == Zws::CloseCode::Normal ? 1 : reject(link, code);
  }

  Zws::CloseCode::T receive_(ZuBSpan data, bool filter) {
    auto msg = Ztc::msg(data);
    if (!msg || (filter && !accepts(m_sub, msg)))
      return Zws::CloseCode::InvalidData;
    if (m_closing) return Zws::CloseCode::Normal;
    switch (msg->body_type()) {
      case Ztc::fbs::Body::Ack:
	if (msg->body_as_Ack()->status() != Ztc::fbs::AckStatus::OK)
	  return Zws::CloseCode::Policy;
	break;
      case Ztc::fbs::Body::Error:
	ZiLOG(Error, "zdash", ([message = ZeString{
	    Zfb::Load::str(msg->body_as_Error()->message())}](auto &s) {
	  s << "ztchub: " << message;
	}));
	return Zws::CloseCode::Policy;
      case Ztc::fbs::Body::EOS:
        // End of snapshot, not end of subscription or publisher lifetime.
        break;
      case Ztc::fbs::Body::Telemetry:
	if (!processTelemetry_(data))
	  return Zws::CloseCode::TooLarge;
	break;
      default:
	return Zws::CloseCode::Protocol;
    }
    return Zws::CloseCode::Normal;
  }

  template <typename L>
  void connectFailed(L &, bool) {
    m_failed = true;
    m_down.post();
    post();
  }

  template <typename L>
  void disconnected(L &, bool) {
    m_connected = false;
    m_subscribed = false;
    if (!m_closing) m_failed = true;
    m_down.post();
    post();
  }

  bool processTelemetry_(ZuBSpan msg) {
    if (msg.length() > FrameMax) return false;
    unsigned length = msg.length();
    unsigned size = sizeof(TelHdr) + length;
    if (size > m_telRing.size()) return false;
    if (length + m_telBytes.load_() > m_queueBytes) return false;
    auto slot = m_telRing.tryPush(size);
    if (!slot) return false;
    auto hdr = new (slot) TelHdr{length};
    memcpy(hdr + 1, msg.data(), length);
    m_telBytes += length;
    // Count before publication so a concurrent GTK drain cannot underflow.
    auto pending = m_telCount++;
    m_telRing.push2(size);
    if (!pending) armRefresh();
    return true;
  }

  template <typename ...Args>
  void gtkRun(Args &&...args) {
    ZGtk::App::run(ZuFwd<Args>(args)...);
  }
  void gtkInit_() {
    gtk_init(nullptr, nullptr);

    auto builder = gtk_builder_new();
    GError *e = nullptr;

    if (!gtk_builder_add_from_file(builder, m_gladePath, &e)) {
      if (e) {
	ZiLOG(Error, "zdash", e->message);
	g_error_free(e);
      }
      g_object_unref(G_OBJECT(builder));
      m_failed = true;
      post();
      return;
    }

    m_mainWindow = GTK_WINDOW(gtk_builder_get_object(builder, "window"));
    auto view_ = GTK_TREE_VIEW(gtk_builder_get_object(builder, "treeview"));
    // m_watchlist = GTK_TREE_VIEW(gtk_builder_get_object(builder, "watchlist"));
    g_object_unref(G_OBJECT(builder));

    if (m_stylePath) {
      auto file = g_file_new_for_path(m_stylePath);
      auto provider = gtk_css_provider_new();
      g_signal_connect(G_OBJECT(provider), "parsing-error",
	  ZGtk::callback([](
	      GtkCssProvider *, GtkCssSection *,
	      GError *e, gpointer) { ZiLOG(Error, "zdash", e->message); }), 0);
      gtk_css_provider_load_from_file(provider, file, nullptr);
      g_object_unref(G_OBJECT(file));
      m_styleContext = gtk_style_context_new();
      gtk_style_context_add_provider(
	  m_styleContext, GTK_STYLE_PROVIDER(provider), G_MAXUINT);
      g_object_unref(G_OBJECT(provider));
    }

    m_gtkModel = GtkTree::Model::ctor();
    m_gtkView.init(view_, m_styleContext);
    m_gtkView.bind(GTK_TREE_MODEL(m_gtkModel));

    m_mainDestroy = g_signal_connect(
	G_OBJECT(m_mainWindow), "destroy",
	ZGtk::callback([](GObject *, gpointer this_) {
	  reinterpret_cast<App *>(this_)->gtkDestroyed_();
	}), this);

    if (m_module && m_module->hidden) {
      // Realize without mapping; the module controls presentation.
      gtk_window_set_accept_focus(m_mainWindow, false);
      gtk_window_set_focus_on_map(m_mainWindow, false);
      gtk_widget_set_no_show_all(GTK_WIDGET(m_mainWindow), true);
      gtk_widget_realize(GTK_WIDGET(m_mainWindow));
    } else {
      gtk_widget_show_all(GTK_WIDGET(m_mainWindow));
      gtk_window_present(m_mainWindow);
    }

    m_gtkReady = m_telRing.attach() == Zu::OK;
    if (m_gtkReady && m_module && m_module->ready) {
      ModuleHost host;
      host.rxRun = [this](ZmFn<void()> fn) {
	ZGtk::App::sched()->run([this, fn = ZuMv(fn)]() mutable {
	  if (!m_closing) fn();
	}, RxSID);
      };
      host.gtkRun = [this](ZmFn<void()> fn) {
	gtkRun([this, fn = ZuMv(fn)]() mutable {
	  if (!m_gtkClosing) fn();
	});
      };
      host.request_ = [this]() { return requestFrame(m_sub, true); };
      host.receive_ = [this](ZuBSpan data, bool filter) {
	return receive_(data, filter) == Zws::CloseCode::Normal;
      };
      host.source_ = [this](ZuCSpan device, ZuCSpan publisher) {
	SourceView view;
	view.count = m_sources.count_();
	if (auto src = m_sources.findPtr(ZuTuple{device, publisher})) {
	  view.generation = src->generation;
	  auto &items = src->telemetry.p<
	    ZuTypeIndex<Ztc::AppTelemetry, Telemetry::TypeList>{}>();
	  if (auto item = items.get()) {
	    auto row = GtkTree::row(item);
	    view.identity = row;
	    view.row = row->row();
	    view.rag = item->value.rag;
	  }
	}
	return view;
      };
      host.pending_ = [this]() { return m_telCount.load_(); };
      host.stop = [this]() { post(); };
      host.model = GTK_TREE_MODEL(m_gtkModel);
      host.window = m_mainWindow;
      host.publisherCol = GtkTree::Model::IDCol0;
      host.deviceCol = GtkTree::Model::IDCol1;
      m_module->ready(host);
    }
  }

  void gtkDestroyed_() {
    m_mainWindow = nullptr;
    if (m_module && m_module->closed) m_module->closed();
    post();
  }

  void gtkFinal_() {
    if (m_module && m_module->closing) m_module->closing();
    while (auto slot = m_telRing.tryShift()) {
      m_telRing.shift2(telSize(slot));
    }
    m_telRing.detach();

    if (m_mainWindow) {
      if (m_mainDestroy)
	g_signal_handler_disconnect(G_OBJECT(m_mainWindow), m_mainDestroy);
      gtk_widget_destroy(GTK_WIDGET(m_mainWindow));
      m_mainWindow = nullptr;
    }
    m_gtkView.final();
    if (m_gtkModel) g_object_unref(G_OBJECT(m_gtkModel));
    m_gtkModel = nullptr;
    m_sources.clean();
    if (m_styleContext) g_object_unref(G_OBJECT(m_styleContext));
  }

private:
  void armRefresh(int mode = ZmScheduler::Advance) {
    ZGtk::App::sched()->add(&m_refreshTimer, Zm::now() + m_refreshRate,
	mode,
	[this](auto &&arm) {
	  return arm([this]() { gtkRefresh_(); });
	}, ZGtk::App::sid());
  }

  void gtkRefresh_() {
    if (m_gtkClosing) return;
    auto deadline = Zm::now() + m_refreshQuantum;
    unsigned n = 0;
    while (auto slot = m_telRing.tryShift()) {
      auto hdr = static_cast<const TelHdr *>(slot);
      auto length = hdr->length;
      // Decode directly from ring storage before allowing Rx to reuse it.
      processTel2_(hdr->data());
      if (m_module && m_module->consumed) {
	auto offset = static_cast<const uint8_t *>(slot) -
	  static_cast<const uint8_t *>(m_telRing.data());
	m_module->consumed(offset + sizeof(TelHdr) + length > m_telRing.size());
      }
      m_telRing.shift2(sizeof(TelHdr) + length);
      m_telBytes -= length;
      --m_telCount;
      if (!(++n % RefreshBatch) && Zm::now() >= deadline) break;
    }
    if (m_telCount.load_()) armRefresh(ZmScheduler::Defer);
    else if (m_module && m_module->drained) m_module->drained();
  }

  void removeSource_(Sources::Node *source) {
    auto &container = source->telemetry.p<
      ZuTypeIndex<Ztc::AppTelemetry, Telemetry::TypeList>{}>();
    auto item = container.get();
    if (item) m_gtkModel->del(GtkTree::row(item));
    m_sources.del(source->key());
  }

  Source *source_(ZuCSpan device, ZuCSpan publisher, uint64_t generation) {
    auto key = ZuFwdTuple(device, publisher);
    if (auto old = m_sources.findPtr(key)) {
      if (old->generation == generation) return old;
      if (old->generation > generation) return nullptr;
      // One epoch per publisher: retire it directly, without a tree scan.
      removeSource_(old);
    }
    auto source = new Sources::Node{device, publisher, generation};
    m_sources.addNode(source);
    return source;
  }

  void processTel2_(ZuBSpan data) {
    auto msg = Ztc::fbs::GetMsg(data.data()); // verified on Rx
    auto device = Zfb::Load::str(msg->deviceId());
    auto generation = msg->agentGen();
    if (msg->body_type() != Ztc::fbs::Body::Telemetry) return;
    auto tel = msg->body_as_Telemetry();
    auto publisher = Zfb::Load::str(tel->id());
    if (tel->value_type() == Ztc::fbs::TelemetryBody::Shutdown) {
      if (auto old = m_sources.findPtr(
	  ZuFwdTuple(device, publisher)); old && old->generation == generation)
	removeSource_(old);
      return;
    }
    auto src = source_(device, publisher, generation);
    if (!src) return;
    switch (tel->value_type()) {
#define ZDashLoad(Name) \
      case Ztc::fbs::TelemetryBody::Name: \
	processTel3_(src, tel->value_as_##Name()); break;
      ZDashLoad(HeapTelemetry)
      ZDashLoad(HashTelemetry)
      ZDashLoad(ThreadTelemetry)
      ZDashLoad(MxTelemetry)
      ZDashLoad(CxnTelemetry)
      ZDashLoad(QueueTelemetry)
      ZDashLoad(HubTelemetry)
      ZDashLoad(LinkTelemetry)
      ZDashLoad(PoolTelemetry)
      ZDashLoad(DBTelemetry)
      ZDashLoad(DBHostTelemetry)
      ZDashLoad(DBTableTelemetry)
      ZDashLoad(AppTelemetry)
      ZDashLoad(AlertTelemetry)
#undef ZDashLoad
      default: break;
    }
  }

  void addGtkRow(Source *src, AppItem *item) {
    item->initTelKey(src->publisher, src->device, src->generation);
    m_gtkModel->add(new GtkTree::App{item}, m_gtkModel->root());
  }
  AppItem *appItem_(Source *src) {
    ZuTypeIndex<Ztc::AppTelemetry, Telemetry::TypeList> i;
    auto &container = src->telemetry.p<i>();
    auto item = container.get();
    if (!item) {
      item = new TelItem<Ztc::AppTelemetry>;
      container.add(item);
      addGtkRow(src, item);
    }
    return item;
  }
  DBItem *dbItem_(Source *src) {
    ZuTypeIndex<Ztc::DBTelemetry, Telemetry::TypeList> i;
    auto &container = src->telemetry.p<i>();
    auto item = container.get();
    if (!item) {
      item = new TelItem<Ztc::DBTelemetry>;
      container.add(item);
      addGtkRow(src, item);
    }
    return item;
  }
  template <typename ParentItem, typename Item, typename ParentFn>
  void addGtkRow_(ParentItem *parentItem, Item *item, ParentFn parentFn) {
    auto parentRow = GtkTree::row(parentItem);
    auto &parent = parentFn(parentRow);
    if (parent.row() < 0) m_gtkModel->add(&parent, parentRow);
    using GtkRow = ZuDecay<decltype(*GtkTree::row(item))>;
    m_gtkModel->add(new GtkRow{item}, &parent);
  }
  void addGtkRow(Source *src, TelItem<Ztc::HeapTelemetry> *item) {
    addGtkRow_(appItem_(src), item,
	[](GtkTree::App *_) -> GtkTree::HeapParent & {
	  return _->heaps();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::HashTelemetry> *item) {
    addGtkRow_(appItem_(src), item,
	[](GtkTree::App *_) -> GtkTree::HashTblParent & {
	  return _->hashTbls();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::ThreadTelemetry> *item) {
    addGtkRow_(appItem_(src), item,
	[](GtkTree::App *_) -> GtkTree::ThreadParent & {
	  return _->threads();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::MxTelemetry> *item) {
    addGtkRow_(appItem_(src), item,
	[](GtkTree::App *_) -> GtkTree::MxParent & { return _->mxs(); });
  }
  void addGtkRow(Source *src, TelItem<Ztc::CxnTelemetry> *item) {
    ZuTypeIndex<Ztc::MxTelemetry, Telemetry::TypeList> i;
    auto &mxContainer = src->telemetry.p<i>();
    auto mxItem = mxContainer.findPtr(ZuFwdTuple(item->value.mxID));
    if (!mxItem) {
      mxItem = new TelItem<Ztc::MxTelemetry>;
      mxItem->value.id = item->value.mxID;
      mxContainer.add(mxItem);
      addGtkRow(src, mxItem);
    }
    m_gtkModel->add(new GtkTree::Socket{item}, GtkTree::row(mxItem));
  }
  void addGtkRow(Source *src, TelItem<Ztc::QueueTelemetry> *item) {
    addGtkRow_(appItem_(src), item,
	[](GtkTree::App *_) -> GtkTree::QueueParent & {
	  return _->queues();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::PoolTelemetry> *item) {
    addGtkRow_(appItem_(src), item,
	[](GtkTree::App *app) -> GtkTree::PoolParent & {
	  return app->pools();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::HubTelemetry> *item) {
    addGtkRow_(appItem_(src), item,
	[](GtkTree::App *_) -> GtkTree::EngineParent & {
	  return _->engines();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::LinkTelemetry> *item) {
    ZuTypeIndex<Ztc::HubTelemetry, Telemetry::TypeList> i;
    auto &engContainer = src->telemetry.p<i>();
    auto engItem =
      engContainer.findPtr(ZuFwdTuple(item->value.hubID, item->value.type));
    if (!engItem) {
      engItem = new TelItem<Ztc::HubTelemetry>;
      engItem->value.linkType = item->value.type;
      engItem->value.id = item->value.hubID;
      engContainer.add(engItem);
      addGtkRow(src, engItem);
    }
    m_gtkModel->add(new GtkTree::Link{item}, GtkTree::row(engItem));
  }
  void addGtkRow(Source *src, DBItem *item) {
    auto appGtkRow = GtkTree::row(appItem_(src));
    auto &db = appGtkRow->db();
    db.init(item);
    m_gtkModel->add(&db, appGtkRow);
  }
  void addGtkRow(Source *src, TelItem<Ztc::DBHostTelemetry> *item) {
    addGtkRow_(dbItem_(src), item,
	[](GtkTree::DB *_) -> GtkTree::DBHostParent & {
	  return _->hosts();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::DBTableTelemetry> *item) {
    addGtkRow_(dbItem_(src), item,
	[](GtkTree::DB *_) -> GtkTree::DBTableParent & {
	  return _->tables();
	});
  }
  template <typename FBType,
    typename = decltype(void(bool(ZuIs_<FBType, Ztc::fbs::AlertTelemetry>{})))>
  void
  processTel3_(Source *src, const FBType *fbo) {
    if constexpr (!ZuIs_<FBType, Ztc::fbs::AlertTelemetry>{}) {
      ZuTypeIndex<FBType, Telemetry::FBTypeList> I;
      using T = ZuType<I, Telemetry::TypeList>;
      auto &container = src->telemetry.p<I>();
      using Item = TelItem<T>;
      if (auto item = container.lookup(fbo)) {
	ZfbStruct::update(item->value, fbo);
	m_gtkModel->updated(GtkTree::row(item));
      } else {
	item = new Item{fbo};
	container.add(item);
	addGtkRow(src, item);
      }
    } else {
      ZuTypeIndex<FBType, Telemetry::FBTypeList> i;
      using T = ZuType<i, Telemetry::TypeList>;
      auto &container = src->telemetry.p<i>();
      if (container.data.length() >= m_alertRows) container.data.splice(0, 1);
      processAlert(new (container.data.push()) T{ZfbStruct::ctor<T>(fbo)});
    }
  }

  void processAlert(const Ztc::AlertTelemetry *) {
    // FIXME - update alerts in UX
  }

private:
  // Immutable after init; shared read-only by Rx and GTK.
  Subscription		m_sub;
  String		m_gladePath;
  String		m_stylePath;
  ZuTime		m_refreshQuantum;
  ZuTime		m_refreshRate;
  unsigned		m_queueBytes = QueuedInputMax;
  unsigned		m_alertRows = 1000;

  ModuleSession		*m_module = nullptr;
  bool			m_offline = false;

  // Main-thread lifecycle; callbacks are drained before final releases them.
  Client		m_client{this};
  ZmRef<Link>		m_link;
  bool			m_clientInited = false;
  bool			m_clientStarted = false;
  bool			m_attached = false;

  // Control and ring accounting require exact cross-thread publication.
  alignas(Zm::CacheLineSize)
  ZmSemaphore		m_done;
  ZmSemaphore		m_executed;
  ZmSemaphore		m_down;
  TelRing		m_telRing;
  ZmScheduler::Timer	m_refreshTimer;
  ZmAtomic<unsigned>	m_failed = false;
  ZmAtomic<unsigned>	m_telCount = 0;
  ZmAtomic<unsigned>	m_telBytes = 0;

  alignas(Zm::CacheLineSize) // Rx-owned
  Frame			m_frame;
  bool			m_connected = false;
  bool			m_subscribed = false;
  bool			m_closing = false;

  alignas(Zm::CacheLineSize) // GTK-owned; main reads after handoff/drain
  Sources		m_sources;
  GtkTree::View		m_gtkView;
  GtkTree::Model	*m_gtkModel = nullptr;
  GtkStyleContext	*m_styleContext = nullptr;
  GtkWindow		*m_mainWindow = nullptr;
  gulong		m_mainDestroy = 0;
  bool			m_gtkReady = false;
  bool			m_gtkClosing = false;
};

} // namespace ZDash

static ZDash::App *signalApp = nullptr;
static void sigint() { if (signalApp) signalApp->post(); }

static bool session(
    ZDash::AppCf config, ZuCSpan caPath, ZuCSpan token,
    ZDash::ModuleSession *module = nullptr)
{
  unsigned timeout = module ? module->timeout : 0;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(ZDash::GtkSID)
      .thread(1, [](auto &t) { t.name("io-rx"); t.isolated(1); })
      .thread(2, [](auto &t) { t.name("io-tx"); t.isolated(1); })
      .thread(ZDash::RxSID, [](auto &t) { t.name("rx"); t.isolated(1); })
      .thread(ZDash::TxSID, [](auto &t) { t.name("tx"); t.isolated(1); })
      .thread(ZDash::GtkSID, [](auto &t) { t.name("gtk"); t.isolated(1); });
  }).rxThread(1).txThread(2)};
  if (!mx.start()) return false;
  ZDash::App app;
  signalApp = &app;
  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();
  bool ok = app.init(&mx, ZuMv(config), caPath, token, module);
  if (ok) ok = app.wait(timeout);
  app.final();
  ok = ok && !app.failed();
  signalApp = nullptr;
  ZmTrap::sigintFn(nullptr);
  mx.stop();
  return ok;
}

int main(int argc, char **argv)
{
  try {
    ZDash::Options options;
    argc = ZfCLI::load(options, argc, argv);
    if (options.help) {
      std::cout << "Usage: zdash --config=CONFIG [--wss=URL] "
	"[--device-id=ID] [--ca=PATH] [--no-browser]\n";
      return 0;
    }
    auto modulePath = Zt::getpath("ZDASH_TEST");
    if (argc != 1 || (!options.config && !modulePath)) usage();
    ZDash::AppCf config;
    if (options.config) {
      ZiFile file;
      if (file.open(Zi::Path{options.config}, ZiFile::ReadOnly |
	  ZiFile::NoFollow | ZiFile::GC) != Zi::OK) return 1;
      auto size = file.size();
      if (size <= 0 || uint64_t(size) > ZDashOAuth::BodyMax) return 1;
      ZDash::String source;
      source.length(unsigned(size));
      if (file.read(source.data(), unsigned(size)) != int(size)) return 1;
      auto parsed = ZfCf::scan(source.span());
      if (parsed.p<0>() < 0 || !parsed.p<1>()) return 1;
      config = ZfCf::handler<ZDash::AppCf>(parsed.p<1>()).ctor();
    }
    if (options.wssURL) config.wssURL = ZuMv(options.wssURL);
    if (options.deviceID) config.deviceID = ZuMv(options.deviceID);
    if (options.caPath) config.caPath = ZuMv(options.caPath);
    if (modulePath) {
      ZiModule library;
      ZeString error;
      if (library.load(Zi::Path{modulePath}, 0, &error) < 0) {
	std::cerr << error << '\n';
	return 1;
      }
      // Keep the module resident after its factory has returned an object.
      // Its vtable and framework callbacks may remain reachable at teardown.
      auto factory = reinterpret_cast<ZDash::FactoryFn>(
	library.resolve(ZdashModuleFnSym, &error));
      if (!factory) {
	library.unload();
	std::cerr << error << '\n';
	return 1;
      }
      ZmRef<ZDash::Module> module{(*factory)()};
      if (!module) {
	std::cerr << "null dashboard module\n";
	return 1;
      }
      ZDash::ModuleSession run;
      run.online = !!config.wssURL;
      run.session = {&config,
	[](ZDash::AppCf *cf, ZDash::ModuleSession &run) {
	  auto caPath = ZuMv(cf->caPath);
	  return session(ZuMv(*cf), caPath, run.token, &run);
	}};
      return module->run(run);
    }
    if (!config.wssURL) return 1;
    ZDashOAuth::Config oauth;
    if (!ZDashOAuth::loadConfig(options.config, oauth)) return 1;
    oauth.caPath = ZuMv(config.caPath);
    return ZDashOAuth::run(oauth, options.noBrowser,
      [&config, &oauth](ZiMultiplex &, ZDashOAuth::Clients &, ZuCSpan token) {
	return session(ZuMv(config), oauth.caPath, token);
      }) ? 0 : 1;
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    return 1;
  }
}
