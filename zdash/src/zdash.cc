//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>
#include <stdlib.h>

#include <libintl.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuPolymorph.hh>
#include <zlib/ZuByteSwap.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRing.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZvRingParams.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZmRing.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcDB.hh>
#include <zlib/ZtcMsg.hh>
#include <zlib/Zws.hh>

#include <zlib/ZdfStore.hh>

#include <zlib/ZGtkApp.hh>
#include <zlib/ZGtkCallback.hh>
#include <zlib/ZGtkTreeModel.hh>
#include <zlib/ZGtkValue.hh>

#include "zdash_oauth.hh"
#include "zdash_protocol.hh"

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

void sigint();

namespace ZDash {

struct AppCf {
  ZvRingCf	telRing;
  ZtString<>	gtkGlade = "zdash.glade";
  ZtString<>	gtkStyle;
  unsigned	gtkRefresh = 1;
  unsigned	gtkThread = 5;
  unsigned	queueBytes = QueuedInputMax;
  unsigned	interval = 1000;
  unsigned	alertRows = 1000;
  ZtString<>	wssURL;
  ZtString<>	deviceID;
  ZtString<>	group = "App";
  ZuID		publisherID;
  ZtString<>	filter = "*";
};

ZfStruct(, (AppCf, Cf),
  (((telRing)),						(UDT)),
  (((gtkGlade)),					(String, "zdash.glade")),
  (((gtkStyle)),					(String)),
  (((gtkRefresh), ((Range<1U, 60000U>))),		(UInt32, 1)),
  (((gtkThread)),					(UInt32, 5)),
  (((queueBytes), ((Range<unsigned(FrameMax), 1U<<28>))),	(UInt32, QueuedInputMax)),
  (((interval), ((Range<1U, 3600000U>))),		(UInt32, 1000)),
  (((alertRows), ((Range<1U, 1000000U>))),		(UInt32, 1000)),
  (((wssURL)),					(String)),
  (((deviceID)),					(String)),
  (((group)),						(String, "App")),
  (((publisherID)),					(String)),
  (((filter)),						(String, "*")));

struct Options {
  ZtString<> config;
  ZtString<> wssURL;
  ZtString<> deviceID;
  ZtString<> caPath;
  bool noBrowser = false;
  bool help = false;
};
ZfStruct(, (Options, CLI),
  (((config), (CLI::Long<"config">)), (String)),
  (((wssURL), (CLI::Long<"wss">)), (String)),
  (((deviceID), (CLI::Long<"device-id">)), (String)),
  (((caPath), (CLI::Long<"ca">)), (String)),
  (((noBrowser), (CLI::Long<"no-browser">)), (Bool)),
  (((help), (CLI::Flag<'h'>, CLI::Long<"help">)), (Bool)));

namespace Telemetry {
  struct Watch {
    void	*ptr_ = nullptr;

    template <typename T> const T *ptr() const {
      return static_cast<const T *>(ptr_);
    }
    template <typename T> T *ptr() {
      return static_cast<T *>(ptr_);
    }
  };
  static auto Watch_Axor(const Watch &v) { return v.ptr_; }
  ZmListDeriveT((T), WatchList, T,
    (ZmListKey<Watch_Axor,
      ZmListNode<T,
	ZmListHeapID<"ZDash.Watch", ZmListLock<ZmNoLock>>>>));

  // display - contains pointer to tree array
  struct Display_ : public Watch {
    unsigned		row = 0;
  };
  using DispList = WatchList<Display_>;
  using Display = DispList::Node;

  // graph - contains pointer to graph - graph contains field selection
  struct Graph_ : public Watch { };
  using GraphList = WatchList<Graph_>;
  using Graph = GraphList::Node;

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
    using TelKey = ZuTuple<const ZtString<> &, const ZtString<> &, uint64_t>;
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
    ZtString<>	publisher_;
    ZtString<>	device_;
    uint64_t	generation_ = 0;
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
  template <typename Data_> class Item_ : public Item__<Data_> {
    using Base = Item__<Data_>;

  public:
    using Data = Data_;
    using DataFrame = Zdf::DataFrame<Data, true>;
    using Writer = typename DataFrame::Writer;
    using TelKey = typename Base::TelKey;

    Item_(void *link__) : link_{link__} { }
    template <typename FBType>
    Item_(void *link__, FBType *fbo) :
      link_{link__}, value{ZfbStruct::ctor<Data>(fbo)} { }

  private:
    Item_(const Item_ &) = delete;
    Item_ &operator =(const Item_ &) = delete;
    Item_(Item_ &&) = delete;
    Item_ &operator =(Item_ &&) = delete;
  public:
    ~Item_() {
      if (dataFrame) {
	ZmBlock<>{}([this](auto wake) {
	  dataFrame->run([this, wake = ZuMv(wake)]() mutable {
	    if (dfWriter) dfWriter->stop();
	    dfWriter = nullptr;
	    dataFrame->stopWriting(ZuMv(wake));
	  });
	});
      }
    }

    template <typename Link>
    Link *link() const { return static_cast<Link *>(link_); }

    template <typename T>
    T *gtkRow() const { return static_cast<T *>(gtkRow_); }
    template <typename T>
    void gtkRow(T *node) { gtkRow_ = node; }

    TelKey telKey() const { return Base::telKey(value); }
    int rag() const { return Base::rag(value); }

    bool record(ZuCSpan name, Zdf::Store *store, Zdf::Shard shard = 0) {
      dataFrame = ZmBlock<ZmRef<DataFrame>>{}([store, name, shard](auto wake) {
	store->template openDF<Data, true, true>(
	    shard, Zdf::IDString{name}, ZuMv(wake));
      });
      if (!dataFrame) return false;
      dfWriter = ZmBlock<ZmRef<Writer>>{}([this](auto wake) {
	dataFrame->run([this, wake = ZuMv(wake)]() mutable {
	  dataFrame->write(ZuMv(wake), []() { });
	});
      });
      return !!dfWriter;
    }

    void			*link_;
    Data			value;

    void			*gtkRow_ = nullptr;
    DispList			dispList;
    GraphList			graphList;

    ZmRef<DataFrame>		dataFrame;
    ZmRef<Writer>		dfWriter;
  };

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
    using Node = Item_<T>;
    ItemSingleton() = default;
    ~ItemSingleton() { if (m_node) delete m_node; }
    template <typename FBType>
    Node *lookup(const FBType *) const { return m_node; }
    void add(Node *node) { if (m_node) delete m_node; m_node = node; }
  private:
    Node	*m_node = nullptr;
  };
  class AlertArray {
    AlertArray(const AlertArray &) = delete;
    AlertArray &operator =(const AlertArray &) = delete;
    AlertArray(AlertArray &&) = delete;
    AlertArray &operator =(AlertArray &&) = delete;
  public:
    using T = Ztc::AlertTelemetry;
    using Node = T;
    AlertArray() = default;
    ~AlertArray() = default;
    ZtArray<T>	data;
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
}

namespace GtkTree {
  template <typename Impl, typename Item>
  class Row {
    auto impl() const { return static_cast<const Impl *>(this); }
    auto impl() { return static_cast<Impl *>(this); }

  public:
    Item	*item = nullptr;

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

  template <unsigned Depth, typename Item>
  struct Leaf :
      public Row<Leaf<Depth, Item>, Item>,
      public ZGtk::TreeHierarchy::Leaf<Leaf<Depth, Item>, Depth> {
    Leaf() = default;
    Leaf(Item *item) : Row<Leaf, Item>{item} { }
    using Row<Leaf, Item>::cmp;
  };

  template <unsigned Depth, typename Item, typename Child>
  struct Parent :
      public Row<Parent<Depth, Item, Child>, Item>,
      public ZGtk::TreeHierarchy::Parent<
	Parent<Depth, Item, Child>, Depth, Child> {
    Parent() = default;
    Parent(Item *item) : Row<Parent, Item>{item} { }
    using Row<Parent, Item>::cmp;
    using Base = ZGtk::TreeHierarchy::Parent<Parent, Depth, Child>;
    using Base::add;
    using Base::del;
  };

  template <unsigned Depth, typename Item, typename Tuple>
  struct Branch :
      public Row<Branch<Depth, Item, Tuple>, Item>,
      public ZGtk::TreeHierarchy::Branch<
	Branch<Depth, Item, Tuple>, Depth, Tuple> {
    Branch() = default; 
    Branch(Item *item) : Row<Branch, Item>{item} { }
    using Row<Branch, Item>::cmp;
    using Base = ZGtk::TreeHierarchy::Branch<Branch, Depth, Tuple>;
    using Base::add;
    using Base::del;
  };
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
  inline Heap *row(TelItem<Ztc::HeapTelemetry> *item) {
    return item->template gtkRow<Heap>();
  }
  inline HashTbl *row(TelItem<Ztc::HashTelemetry> *item) {
    return item->template gtkRow<HashTbl>();
  }
  inline Thread *row(TelItem<Ztc::ThreadTelemetry> *item) {
    return item->template gtkRow<Thread>();
  }
  inline Mx *row(TelItem<Ztc::MxTelemetry> *item) {
    return item->template gtkRow<Mx>();
  }
  inline Socket *row(TelItem<Ztc::CxnTelemetry> *item) {
    return item->template gtkRow<Socket>();
  }
  inline Queue *row(TelItem<Ztc::QueueTelemetry> *item) {
    return item->template gtkRow<Queue>();
  }
  inline Pool *row(TelItem<Ztc::PoolTelemetry> *item) {
    return item->template gtkRow<Pool>();
  }
  inline Engine *row(TelItem<Ztc::HubTelemetry> *item) {
    return item->template gtkRow<Engine>();
  }
  inline Link *row(TelItem<Ztc::LinkTelemetry> *item) {
    return item->template gtkRow<Link>();
  }
  inline DBTable *row(TelItem<Ztc::DBTableTelemetry> *item) {
    return item->template gtkRow<DBTable>();
  }
  inline DBHost *row(TelItem<Ztc::DBHostTelemetry> *item) {
    return item->template gtkRow<DBHost>();
  }
  inline DB *row(TelItem<Ztc::DBTelemetry> *item) {
    return item->template gtkRow<DB>();
  }
  inline App *row(TelItem<Ztc::AppTelemetry> *item) {
    return item->template gtkRow<App>();
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
  public:
    enum { RAGCol = 0, IDCol0, IDCol1, IDCol2, NCols };

    // root()
    Root *root() { return &m_root; }

    // parent() - child->parent type map
    template <typename T>
    static ZuSame<T, App, Root> *parent(void *ptr) {
      return static_cast<Root *>(ptr);
    }
    template <typename T>
    static ZuIfT<
	ZuIsSame<T, HeapParent>{} ||
	ZuIsSame<T, HashTblParent>{} ||
	ZuIsSame<T, ThreadParent>{} ||
	ZuIsSame<T, MxParent>{} ||
	ZuIsSame<T, QueueParent>{} ||
	ZuIsSame<T, PoolParent>{} ||
	ZuIsSame<T, EngineParent>{} ||
	ZuIsSame<T, DB>{}, App> *parent(void *ptr) {
      return static_cast<App *>(ptr);
    }
    template <typename T>
    static ZuSame<T, Heap, HeapParent> *parent(void *ptr) {
      return static_cast<HeapParent *>(ptr);
    }
    template <typename T>
    static ZuSame<T, HashTbl, HashTblParent> *parent(void *ptr) {
      return static_cast<HashTblParent *>(ptr);
    }
    template <typename T>
    static ZuSame<T, Thread, ThreadParent> *parent(void *ptr) {
      return static_cast<ThreadParent *>(ptr);
    }
    template <typename T>
    static ZuSame<T, Mx, MxParent> *parent(void *ptr) {
      return static_cast<MxParent *>(ptr);
    }
    template <typename T>
    static ZuSame<T, Queue, QueueParent> *parent(void *ptr) {
      return static_cast<QueueParent *>(ptr);
    }
    template <typename T>
    static ZuSame<T, Pool, PoolParent> *parent(void *ptr) {
      return static_cast<PoolParent *>(ptr);
    }
    template <typename T>
    static ZuSame<T, Engine, EngineParent> *parent(void *ptr) {
      return static_cast<EngineParent *>(ptr);
    }
    template <typename T>
    static ZuSame<T, Socket, Mx> *parent(void *ptr) {
      return static_cast<Mx *>(ptr);
    }
    template <typename T>
    static ZuSame<T, Link, Engine> *parent(void *ptr) {
      return static_cast<Engine *>(ptr);
    }
    template <typename T>
    static ZuIfT<
	ZuIsSame<T, DBHostParent>{} ||
	ZuIsSame<T, DBTableParent>{}, DB> *parent(void *ptr) {
      return static_cast<DB *>(ptr);
    }
    template <typename T>
    static ZuSame<T, DBHost, DBHostParent> *parent(void *ptr) {
      return static_cast<DBHostParent *>(ptr);
    }
    template <typename T>
    static ZuSame<T, DBTable, DBTableParent> *parent(void *ptr) {
      return static_cast<DBTableParent *>(ptr);
    }

    // key printing
    template <typename Impl, typename Key>
    struct KeyPrint_ {
      auto impl() const { return static_cast<const Impl *>(this); }
      auto impl() { return static_cast<Impl *>(this); }

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
    struct KeyPrint : public KeyPrint_<KeyPrint<Key>, Key> {
      using KeyPrint_<KeyPrint, Key>::KeyPrint_;
    };
    template <typename T, typename Key>
    static ZuIfT<
      !ZuIsSame<T, HashTbl>{} &&
      !ZuIsSame<T, Queue>{},
      KeyPrint<Key>> keyPrintType();
    // override addr for hash tables
    template <typename Key>
    struct HashTblKeyPrint : public KeyPrint_<HashTblKeyPrint<Key>, Key> {
      using KeyPrint_<HashTblKeyPrint<Key>, Key>::KeyPrint_;
      auto p1() { return ZuBoxed(this->key.template p<1>()).hex(); }
    };
    template <typename T, typename Key>
    static ZuIfT<
      ZuIsSame<T, HashTbl>{}, HashTblKeyPrint<Key>>
    keyPrintType();
    // override type for queues
    template <typename Key>
    struct QueueKeyPrint : public KeyPrint_<QueueKeyPrint<Key>, Key> {
      using KeyPrint_<QueueKeyPrint<Key>, Key>::KeyPrint_;
      auto p2() {
	return Ztc::QueueType::name(this->key.template p<2>());
      }
    };
    template <typename T, typename Key>
    static ZuIfT<
      ZuIsSame<T, Queue>{}, QueueKeyPrint<Key>>
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
      using Key = decltype(ptr->telKey());
      using KeyPrint = decltype(keyPrintType<T, Key>());
      KeyPrint print{ptr->telKey()};
      switch (i) {
	case RAGCol: 
	  v->init(G_TYPE_INT);
	  v->set_int(ptr->rag());
	  break;
	case IDCol0:
	  m_value.length(0);
	  v->init(G_TYPE_STRING);
	  m_value << print.p0();
	  v->set_static_string(m_value);
	  break;
	case IDCol1:
	  m_value.length(0);
	  v->init(G_TYPE_STRING);
	  m_value << print.p1();
	  v->set_static_string(m_value);
	  break;
	case IDCol2:
	  m_value.length(0);
	  v->init(G_TYPE_STRING);
	  m_value << print.p2();
	  v->set_static_string(m_value);
	  break;
	default:
	  v->init(G_TYPE_NONE);
	  break;
      }
    }

  private:
    Root	m_root;		// root of tree
    ZtString<>	m_value;	// re-used string buffer
  };

  class View {
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
      }, reinterpret_cast<gpointer>(this), nullptr);

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
      m_values[0].unset();
      gtk_tree_model_get_value(model, iter, TextCol, &m_values[0]);

      gint rag;
      {
	ZGtk::Value rag_;
	gtk_tree_model_get_value(model, iter, RagCol, &rag_);
	rag = rag_.get_int();
      }
      switch (rag) {
	case Ztc::RAG::Red:
	  m_values[1].set_static_boxed(&m_rag_red_bg);
	  m_values[2].set_static_boxed(&m_rag_red_fg);
	  g_object_setv(G_OBJECT(cell), 3, m_props, m_values);
	  break;
	case Ztc::RAG::Amber:
	  m_values[1].set_static_boxed(&m_rag_amber_bg);
	  m_values[2].set_static_boxed(&m_rag_amber_fg);
	  g_object_setv(G_OBJECT(cell), 3, m_props, m_values);
	  break;
	case Ztc::RAG::Green:
	  m_values[1].set_static_boxed(&m_rag_green_bg);
	  m_values[2].set_static_boxed(&m_rag_green_fg);
	  g_object_setv(G_OBJECT(cell), 3, m_props, m_values);
	  break;
	default:
	  m_values[1].set_static_boxed(&m_rag_off_bg);
	  m_values[2].set_static_boxed(&m_rag_off_fg);
	  g_object_setv(G_OBJECT(cell), 3, m_props, m_values);
	  break;
      }
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

      static const gchar *props[] = {
	"text", "background-rgba", "foreground-rgba"
      };

      m_props = props;
      m_values[1].init(GDK_TYPE_RGBA);
      m_values[2].init(GDK_TYPE_RGBA);

      {
	auto cell = gtk_cell_renderer_text_new();
	g_object_getv(G_OBJECT(cell), 2, &m_props[1], &m_values[1]);
	if (auto color = static_cast<const GdkRGBA *>(m_values[1].get_boxed()))
	  m_rag_off_bg = *color;
	if (auto color = static_cast<const GdkRGBA *>(m_values[2].get_boxed()))
	  m_rag_off_fg = *color;
	g_object_unref(G_OBJECT(cell));
      }

      g_signal_connect(
	  G_OBJECT(m_treeView), "destroy",
	  ZGtk::callback([](GObject *, gpointer this_) {
	    reinterpret_cast<View *>(this_)->destroyed();
	  }), reinterpret_cast<gpointer>(this));
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
    const gchar	**m_props;
    ZGtk::Value	m_values[3];
  };
}


struct Source {
  ZtString<> device;
  ZtString<> publisher;
  uint64_t generation;
  Telemetry::Containers telemetry;

  Source(ZuCSpan device_, ZuCSpan publisher_, uint64_t generation_) :
    device{device_}, publisher{publisher_}, generation{generation_} { }
  auto key() const { return ZuFwdTuple(device, publisher, generation); }
};
static auto sourceKey(const Source &source) { return source.key(); }
ZmRBTreeDerive(Sources, Source,
  ZmRBTreeNode<Source, ZmRBTreeKey<sourceKey, ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmNoLock, ZmRBTreeHeapID<"ZDash.Source">>>>>);

class App : public ZmPolymorph, public ZGtk::App {
public:
  using Client = Zws::Client<App, Zhttp::H1TLS>;
  using Link = Client::Link;
  using TelRing = ZmRing<ZmRingT<Frame>>;
  template <typename T> using TelItem = Telemetry::Item<T>;
  using AppItem = TelItem<Ztc::AppTelemetry>;
  using DBItem = TelItem<Ztc::DBTelemetry>;

  bool init(ZiMultiplex *mx, AppCf config, ZuCSpan caPath, ZuCSpan token) {
    Zws::URI uri;
    if (!Zws::URI::parse(uri, config.wssURL).ok() || !uri.secure() ||
	!uri.host || !uri.port || !uri.target || !token) return false;
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
    m_telRing.init(ZmRingParams{config.telRing.size}
	.spin(config.telRing.spin).timeout(0));
    if (m_telRing.open(TelRing::Read | TelRing::Write) != Zu::OK)
      return false;
    m_gladePath = ZuMv(config.gtkGlade);
    m_stylePath = ZuMv(config.gtkStyle);
    int64_t refreshRate = int64_t(config.gtkRefresh) * 1000000;
    m_refreshQuantum = ZuTime{ZuTime::Nano{refreshRate >> 1}};
    if (m_refreshQuantum < mx->params().quantum())
      m_refreshQuantum = mx->params().quantum();
    m_refreshRate = m_refreshQuantum + m_refreshQuantum;
    if (config.gtkThread <= 4 || config.gtkThread > mx->params().nThreads())
      return false;

    i18n("zdash", DATADIR);
    attach(mx, config.gtkThread);
    m_attached = true;
    mx->run([this]() {
      gtkInit();
      m_executed.post();
    }, config.gtkThread);
    m_executed.wait();
    if (!m_gtkReady) return false;

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
    ZtString<> authorization{"Bearer "};
    authorization << token;
    m_link = new Link{&m_client, uri, Ztc::Protocol, authorization};
    ZuClear(authorization.data(), authorization.length());
    m_link->connect();
    return true;
  }

  void final() {
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
      detach({[this]() {
	gtkFinal();
	m_executed.post();
      }});
      m_executed.wait();
      m_attached = false;
    }
    m_telRing.close();
  }

  void post() { m_done.post(); }
  void wait() { m_done.wait(); }
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
    auto msg = Ztc::msg(ZuBSpan{m_frame->data(), m_frame->length});
    if (!accepts(m_sub, msg))
      return reject(link, Zws::CloseCode::InvalidData);
    if (m_closing) return 1;
    switch (msg->body_type()) {
      case Ztc::fbs::Body::Ack:
	if (msg->body_as_Ack()->status() != Ztc::fbs::AckStatus::OK)
	  return reject(link, Zws::CloseCode::Policy);
	m_interval = msg->body_as_Ack()->interval();
	break;
      case Ztc::fbs::Body::Error:
	ZiLOG(Error, "zdash", ([msg](auto &s) {
	  s << "ztchub: " << Zfb::Load::str(msg->body_as_Error()->message());
	}));
	return reject(link, Zws::CloseCode::Policy);
      case Ztc::fbs::Body::EOS:
        // End of snapshot, not end of subscription or publisher lifetime.
        break;
      case Ztc::fbs::Body::Telemetry:
	if (!processTelemetry(ZuMv(m_frame)))
	  return reject(link, Zws::CloseCode::TooLarge);
	break;
      default:
	return reject(link, Zws::CloseCode::Protocol);
    }
    return 1;
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

  bool processTelemetry(Frame frame) {
    auto length = frame->length;
    if (length > m_queueBytes - m_telBytes.load_()) return false;
    auto slot = m_telRing.tryPush();
    if (!slot) return false;
    new (slot) Frame{ZuMv(frame)};
    m_telBytes += length;
    // Count before publication so a concurrent GTK drain cannot underflow.
    auto pending = m_telCount++;
    m_telRing.push2();
    if (!pending) armRefresh();
    return true;
  }

  template <typename ...Args>
  void gtkRun(Args &&...args) {
    ZGtk::App::run(ZuFwd<Args>(args)...);
  }
  template <typename ...Args>
  void gtkInvoke(Args &&...args) {
    ZGtk::App::invoke(ZuFwd<Args>(args)...);
  }
  void gtkInit() {
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
	  reinterpret_cast<App *>(this_)->gtkDestroyed();
	}), reinterpret_cast<gpointer>(this));

    gtk_widget_show_all(GTK_WIDGET(m_mainWindow));

    gtk_window_present(m_mainWindow);

    m_gtkReady = m_telRing.attach() == Zu::OK;
  }

  void gtkDestroyed() {
    m_mainWindow = nullptr;
    post();
  }

  void gtkFinal() {
    while (auto slot = m_telRing.tryShift()) {
      slot->~Frame();
      m_telRing.shift2();
    }
    m_telRing.detach();

    ZGtk::App::sched()->del(&m_refreshTimer);

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
	  return arm([this]() { gtkRefresh(); });
	}, ZGtk::App::sid());
  }

  void gtkRefresh() {
    auto deadline = Zm::now() + m_refreshQuantum;
    unsigned n = 0;
    while (auto slot = m_telRing.tryShift()) {
      Frame frame{ZuMv(*const_cast<Frame *>(slot))};
      slot->~Frame();
      m_telRing.shift2();
      m_telBytes -= frame->length;
      --m_telCount;
      processTel2(frame);
      if (!(++n & 0xf) && Zm::now() >= deadline) break;
    }
    if (m_telCount.load_()) armRefresh(ZmScheduler::Defer);
  }

  void removeSource(Sources::Node *source) {
    auto &container = source->telemetry.p<
      ZuTypeIndex<Ztc::AppTelemetry, Telemetry::TypeList>{}>();
    auto item = container.lookup(
	static_cast<const Ztc::fbs::AppTelemetry *>(nullptr));
    if (item) m_gtkModel->del(GtkTree::row(item));
    m_sources.del(source->key());
  }

  Source *source(ZuCSpan device, ZuCSpan publisher, uint64_t generation) {
    auto key = ZuFwdTuple(device, publisher, generation);
    if (auto source = m_sources.findPtr(key)) return source;
    // Discard the old epoch before creating rows for a restarted agent.
    auto i = m_sources.iter();
    while (auto old = i()) {
      if (old->device != device || old->publisher != publisher) continue;
      if (old->generation > generation) return nullptr;
      if (old->generation != generation) removeSource(old);
    }
    auto source = new Sources::Node{device, publisher, generation};
    m_sources.addNode(source);
    return source;
  }

  void processTel2(const Frame &frame) {
    auto msg = Ztc::fbs::GetMsg(frame->data()); // verified on Rx
    auto device = Zfb::Load::str(msg->deviceId());
    auto generation = msg->agentGen();
    if (msg->body_type() != Ztc::fbs::Body::Telemetry) return;
    auto tel = msg->body_as_Telemetry();
    auto publisher = Zfb::Load::str(tel->id());
    if (tel->value_type() == Ztc::fbs::TelemetryBody::Shutdown) {
      if (auto old = m_sources.findPtr(
	  ZuFwdTuple(device, publisher, generation))) removeSource(old);
      return;
    }
    auto src = source(device, publisher, generation);
    if (!src) return;
    switch (tel->value_type()) {
#define ZDashLoad(Name) \
      case Ztc::fbs::TelemetryBody::Name: \
	processTel3(src, tel->value_as_##Name()); break;
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

  template <typename FBType>
  ZuIsNot<FBType, Ztc::fbs::AlertTelemetry>
  processTel3(Source *src, const FBType *fbo) {
    ZuTypeIndex<FBType, Telemetry::FBTypeList> I;
    using T = ZuType<I, Telemetry::TypeList>;
    auto &container = src->telemetry.p<I>();
    using Item = TelItem<T>;
    if (auto item = container.lookup(fbo)) {
      ZfbStruct::update(item->value, fbo);
      m_gtkModel->updated(GtkTree::row(item));
    } else {
      item = new Item{src, fbo};
      container.add(item);
      addGtkRow(src, item);
    }
  }
  void addGtkRow(Source *src, AppItem *item) {
    item->initTelKey(src->publisher, src->device, src->generation);
    m_gtkModel->add(new GtkTree::App{item}, m_gtkModel->root());
  }
  AppItem *appItem(Source *src) {
    ZuTypeIndex<Ztc::AppTelemetry, Telemetry::TypeList> i;
    auto &container = src->telemetry.p<i>();
    auto item = container.lookup(
	static_cast<const Ztc::fbs::AppTelemetry *>(nullptr));
    if (!item) {
      item = new TelItem<Ztc::AppTelemetry>{src};
      container.add(item);
      addGtkRow(src, item);
    }
    return item;
  }
  DBItem *dbItem(Source *src) {
    ZuTypeIndex<Ztc::DBTelemetry, Telemetry::TypeList> i;
    auto &container = src->telemetry.p<i>();
    auto item = container.lookup(
	static_cast<const Ztc::fbs::DBTelemetry *>(nullptr));
    if (!item) {
      item = new TelItem<Ztc::DBTelemetry>{src};
      container.add(item);
      addGtkRow(src, item);
    }
    return item;
  }
  template <typename Item, typename ParentFn>
  void addGtkRow_(AppItem *appItem, Item *item, ParentFn parentFn) {
    auto appGtkRow = GtkTree::row(appItem);
    auto &parent = parentFn(appGtkRow);
    if (parent.row() < 0) m_gtkModel->add(&parent, appGtkRow);
    using GtkRow = ZuDecay<decltype(*GtkTree::row(item))>;
    m_gtkModel->add(new GtkRow{item}, &parent);
  }
  void addGtkRow(Source *src, TelItem<Ztc::HeapTelemetry> *item) {
    addGtkRow_(appItem(src), item,
	[](GtkTree::App *_) -> GtkTree::HeapParent & {
	  return _->heaps();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::HashTelemetry> *item) {
    addGtkRow_(appItem(src), item,
	[](GtkTree::App *_) -> GtkTree::HashTblParent & {
	  return _->hashTbls();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::ThreadTelemetry> *item) {
    addGtkRow_(appItem(src), item,
	[](GtkTree::App *_) -> GtkTree::ThreadParent & {
	  return _->threads();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::MxTelemetry> *item) {
    addGtkRow_(appItem(src), item,
	[](GtkTree::App *_) -> GtkTree::MxParent & { return _->mxs(); });
  }
  void addGtkRow(Source *src, TelItem<Ztc::CxnTelemetry> *item) {
    ZuTypeIndex<Ztc::MxTelemetry, Telemetry::TypeList> i;
    auto &mxContainer = src->telemetry.p<i>();
    auto mxItem = mxContainer.findPtr(ZuFwdTuple(item->value.mxID));
    if (!mxItem) {
      mxItem = new TelItem<Ztc::MxTelemetry>{src};
      mxItem->value.id = item->value.mxID;
      mxContainer.add(mxItem);
      addGtkRow(src, mxItem);
    }
    m_gtkModel->add(new GtkTree::Socket{item}, GtkTree::row(mxItem));
  }
  void addGtkRow(Source *src, TelItem<Ztc::QueueTelemetry> *item) {
    addGtkRow_(appItem(src), item,
	[](GtkTree::App *_) -> GtkTree::QueueParent & {
	  return _->queues();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::PoolTelemetry> *item) {
    addGtkRow_(appItem(src), item,
	[](GtkTree::App *app) -> GtkTree::PoolParent & {
	  return app->pools();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::HubTelemetry> *item) {
    addGtkRow_(appItem(src), item,
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
      engItem = new TelItem<Ztc::HubTelemetry>{src};
      engItem->value.linkType = item->value.type;
      engItem->value.id = item->value.hubID;
      engContainer.add(engItem);
      addGtkRow(src, engItem);
    }
    m_gtkModel->add(new GtkTree::Link{item}, GtkTree::row(engItem));
  }
  void addGtkRow(Source *src, DBItem *item) {
    auto appGtkRow = GtkTree::row(appItem(src));
    auto &db = appGtkRow->db();
    db.init(item);
    m_gtkModel->add(&db, appGtkRow);
  }
  template <typename Item, typename ParentFn>
  void addGtkRow_(DBItem *dbItem, Item *item, ParentFn parentFn) {
    auto dbGtkRow = GtkTree::row(dbItem);
    auto &parent = parentFn(dbGtkRow);
    if (parent.row() < 0) m_gtkModel->add(&parent, dbGtkRow);
    using GtkRow = ZuDecay<decltype(*GtkTree::row(item))>;
    m_gtkModel->add(new GtkRow{item}, &parent);
  }
  void addGtkRow(Source *src, TelItem<Ztc::DBHostTelemetry> *item) {
    addGtkRow_(dbItem(src), item,
	[](GtkTree::DB *_) -> GtkTree::DBHostParent & {
	  return _->hosts();
	});
  }
  void addGtkRow(Source *src, TelItem<Ztc::DBTableTelemetry> *item) {
    addGtkRow_(dbItem(src), item,
	[](GtkTree::DB *_) -> GtkTree::DBTableParent & {
	  return _->tables();
	});
  }
  template <typename FBType>
  ZuIs<FBType, Ztc::fbs::AlertTelemetry>
  processTel3(Source *src, const FBType *fbo) {
    ZuTypeIndex<FBType, Telemetry::FBTypeList> i;
    using T = ZuType<i, Telemetry::TypeList>;
    auto &container = src->telemetry.p<i>();
    if (container.data.length() >= m_alertRows) container.data.splice(0, 1);
    processAlert(new (container.data.push()) T{ZfbStruct::ctor<T>(fbo)});
  }

  void processAlert(const Ztc::AlertTelemetry *) {
    // FIXME - update alerts in UX
  }


private:
  ZmSemaphore		m_done;
  ZmSemaphore		m_executed;
  ZmSemaphore		m_down;
  Client		m_client{this};
  ZmRef<Link>		m_link;
  Subscription		m_sub;
  Frame			m_frame;
  bool			m_connected = false; // Rx
  bool			m_subscribed = false; // Rx
  bool			m_closing = false; // Rx
  unsigned		m_interval = 0; // Rx, negotiated by Ack
  ZmAtomic<unsigned>	m_failed = false;
  bool			m_clientInited = false;
  bool			m_clientStarted = false;
  bool			m_attached = false;
  bool			m_gtkReady = false;

  TelRing		m_telRing;
  unsigned		m_queueBytes = QueuedInputMax;
  unsigned		m_alertRows = 1000;
  ZmAtomic<unsigned>	m_telCount = 0;
  ZmAtomic<unsigned>	m_telBytes = 0;
  Sources		m_sources; // GTK-owned

  ZtString<>		m_gladePath;
  ZtString<>		m_stylePath;
  GtkStyleContext	*m_styleContext = nullptr;
  GtkWindow		*m_mainWindow = nullptr;
  gulong		m_mainDestroy = 0;

  ZuTime		m_refreshQuantum;
  ZuTime		m_refreshRate;
  ZmScheduler::Timer	m_refreshTimer;
  GtkTree::View		m_gtkView;
  GtkTree::Model	*m_gtkModel = nullptr;
};

} // namespace ZDash

static ZDash::App *signalApp = nullptr;
void sigint() { if (signalApp) signalApp->post(); }

static bool session(
    ZDash::AppCf config, ZuCSpan caPath, ZuCSpan token)
{
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.name("io-rx"); t.isolated(1); })
      .thread(2, [](auto &t) { t.name("io-tx"); t.isolated(1); })
      .thread(3, [](auto &t) { t.name("rx"); t.isolated(1); })
      .thread(4, [](auto &t) { t.name("tx"); t.isolated(1); })
      .thread(5, [](auto &t) { t.name("gtk"); t.isolated(1); });
  }).rxThread(1).txThread(2)};
  if (!mx.start()) return false;
  ZDash::App app;
  signalApp = &app;
  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();
  bool ok = app.init(&mx, ZuMv(config), caPath, token);
  if (ok) app.wait();
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
    if (argc != 1 || !options.config) usage();
    ZDashOAuth::Config oauth;
    if (!ZDashOAuth::loadConfig(options.config, oauth)) return 1;

    ZiFile file;
    if (file.open(Zi::Path{options.config}, ZiFile::ReadOnly |
	ZiFile::NoFollow | ZiFile::GC) != Zi::OK) return 1;
    auto size = file.size();
    if (size <= 0 || uint64_t(size) > ZDashOAuth::BodyMax) return 1;
    ZtString<> source;
    source.length(unsigned(size));
    if (file.read(source.data(), unsigned(size)) != int(size)) return 1;
    auto parsed = ZfCf::scan(source.span());
    if (parsed.p<0>() < 0 || !parsed.p<1>()) return 1;
    auto config = ZfCf::handler<ZDash::AppCf>(parsed.p<1>()).ctor();
    if (options.wssURL) config.wssURL = ZuMv(options.wssURL);
    if (options.deviceID) config.deviceID = ZuMv(options.deviceID);
    if (options.caPath) oauth.caPath = ZuMv(options.caPath);
    if (!config.wssURL) return 1;
    return ZDashOAuth::run(oauth, options.noBrowser,
      [&config, &oauth](ZiMultiplex &, ZDashOAuth::Clients &, ZuCSpan token) {
	return session(ZuMv(config), oauth.caPath, token);
      }) ? 0 : 1;
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    return 1;
  }
}
