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
#include <zlib/ZuVersion.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiModule.hh>
#include <zlib/ZiRing.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZvCSV.hh>
#include <zlib/ZvRingParams.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/ZvUserDB.hh>
#include <zlib/ZcmdClient.hh>
#include <zlib/ZcmdServer.hh>

#include <zlib/Zdf.hh>

#include <zlib/ZGtkApp.hh>
#include <zlib/ZGtkCallback.hh>
#include <zlib/ZGtkTreeModel.hh>
#include <zlib/ZGtkValue.hh>

#include "request_fbs.h"
#include "reqack_fbs.h"

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
    "Usage: zdash\n";
  std::cerr << usage << std::flush;
  ZiLog::stop();
  Zm::exit(1);
}

void sigint();

namespace ZDash {

struct AppCf {
  ZvRingCf	telRing;
  int		appRole = ZvTelemetry::AppRole::Dev;
  ZtString<>	gtkGlade;
  ZtString<>	gtkStyle;
  unsigned	gtkRefresh = 1;
  unsigned	thread = 0;
  unsigned	gtkThread = 0;
};

ZfStruct((AppCf, Cf),
  (((telRing)),						(UDT)),
  (((appRole), (Enum<ZvTelemetry::AppRole::Map>)),	(Int32,
      ZvTelemetry::AppRole::Dev)),
  (((gtkGlade), (Required)),				(String)),
  (((gtkStyle)),					(String)),
  (((gtkRefresh), ((Range<1U, 60000U>))),		(UInt32, 1)),
  (((thread), (Required)),				(UInt32)),
  (((gtkThread), (Required)),				(UInt32)));

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
  static constexpr const auto &Watch_HeapID() {
    return "zdash.Telemetry.Watch";
  }
  template <typename T>
  ZuDerive(WatchList,
    (ZmList<T,
      ZmListKey<Watch_Axor,
	ZmListNode<T,
	  ZmListHeapID<Watch_HeapID,
	    ZmListLock<ZmNoLock>>>>>));

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

  using TypeList = ZvTelemetry::TypeList;
  using FBTypeList = ZuTypeMap<ZfbType, TypeList>;

  template <typename Data> struct Item__ {
    static constexpr auto Axor = ZuFieldAxor<Data>();
    static decltype(auto) telKey(const Data &data) { return Axor(data); }
    using TelKey = ZuRDecay<decltype(telKey(ZuDeclVal<const Data &>()))>;
    static int rag(const Data &data) { return data.rag(); }
  };
  template <> struct Item__<ZvTelemetry::App> {
    using TelKey = ZuTuple<const ZtString<> &>;
    void initTelKey(const ZtString<> &server, uint16_t port) {
      telKey_ << server << ':' << port;
    }
    TelKey telKey(const ZvTelemetry::App &) const {
      return TelKey{telKey_};
    }
    static int rag(const ZvTelemetry::App &data) {
      return data.rag;
    }
    ZtString<>	telKey_;
  };
  template <> struct Item__<ZvTelemetry::DB> {
    using TelKey = ZuTuple<const char *>;
    static TelKey telKey(const ZvTelemetry::DB &) {
      return TelKey{"dbenv"};
    }
    static int rag(const ZvTelemetry::DB &) {
      return ZvTelemetry::RAG::Off;
    }
  };
  template <typename Data_> class Item_ : public Item__<Data_> {
    using Base = Item__<Data_>;

  public:
    using Data = Data_;
    using TelKey = typename Base::TelKey;

    Item_(void *link__) : link_{link__} { }
    template <typename FBType>
    Item_(void *link__, FBType *fbo) :
      link_{link__}, data{ZfbStruct::ctor<Data>(fbo)} { }

  private:
    Item_(const Item_ &) = delete;
    Item_ &operator =(const Item_ &) = delete;
    Item_(Item_ &&) = delete;
    Item_ &operator =(Item_ &&) = delete;
  public:
    ~Item_() {
      if (dataFrame) {
	dfWriter.final();
	ZmBlock<>{}([this](auto wake) {
	  dataFrame->close(
	      [wake = ZuMv(wake)](Zdf::CloseResult result) mutable {
	    if (result.is<Zdf::Event>())
	      ZiLogEvent(ZuMv(result).p<Zdf::Event>());
	    wake();
	  });
	});
	dataFrame = nullptr;
      }
    }

    template <typename Link>
    Link *link() const { return static_cast<Link *>(link_); }

    template <typename T>
    T *gtkRow() const { return static_cast<T *>(gtkRow_); }
    template <typename T>
    void gtkRow(T *node) { gtkRow_ = node; }

    TelKey telKey() const { return Base::telKey(data); }
    int rag() const { return Base::rag(data); }

    bool record(ZuCSpan name, Zdf::Store *store, ZeError *e = nullptr) {
      dataFrame = new Zdf::DataFrame{Data::fields(), name, true};
      dataFrame->init(store);
      if (!ZmBlock<bool>{}([this](auto wake) {
	dataFrame->open([wake = ZuMv(wake)](Zdf::OpenResult result) mutable {
	  if (result.is<Zdf::Event>()) {
	    ZiLogEvent(ZuMv(result).p<Zdf::Event>());
	    wake(false);
	  }
	  wake(true);
	});
      }));
      dfWriter = dataFrame->writer();
      return true;
    }

    void			*link_;
    Data			data;

    void			*gtkRow_ = nullptr;
    DispList			dispList;
    GraphList			graphList;

    ZuPtr<Zdf::DataFrame>	dataFrame;
    Zdf::DataFrame::Writer	dfWriter;
  };

  static constexpr const auto &ItemTree_HeapID() {
    return "zdash.Telemetry.Tree";
  }
  template <typename T>
  static auto KeyAxor(const T &v) { return v.telKey(); }
  template <typename T>
  ZuDerive(ItemTree_,
    (ZmRBTree<Item_<T>,
      ZmRBTreeNode<Item_<T>,
	ZmRBTreeKey<KeyAxor<Item_<T>>,
	  ZmRBTreeUnique<true,
	    ZmRBTreeLock<ZmNoLock,
	      ZmRBTreeHeapID<ItemTree_HeapID>>>>>>));
  template <typename T>
  class ItemTree : public ItemTree_<T> {
  public:
    using Node = Item_<T>;
    template <typename FBType>
    Node *lookup(const FBType *fbo) const {
      auto node_ = this->find(ZuStructKey(*fbo));
      if (!node_) return nullptr;
      return &node_->data();
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
    using T = ZvTelemetry::Alert;
    using Node = T;
    AlertArray() = default;
    ~AlertArray() = default;
    ZtArray<T>	data;
  };

  template <typename U> struct Container_ { // default
    using T = ItemTree<U>;
  };
  template <> struct Container_<ZvTelemetry::App> {
    using T = ItemSingleton<ZvTelemetry::App>;
  };
  template <> struct Container_<ZvTelemetry::DB> {
    using T = ItemSingleton<ZvTelemetry::DB>;
  };
  template <> struct Container_<ZvTelemetry::Alert> {
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
    static int rag() { return ZvTelemetry::RAG::Off; }
  };

  template <typename T> using TelItem = Telemetry::Item<T>;

  using Heap = Leaf<3, TelItem<ZvTelemetry::Heap>>;
  using HashTbl = Leaf<3, TelItem<ZvTelemetry::HashTbl>>;
  using Thread = Leaf<3, TelItem<ZvTelemetry::Thread>>;
  using Socket = Leaf<4, TelItem<ZvTelemetry::Socket>>;
  using Mx = Parent<3, TelItem<ZvTelemetry::Mx>, Socket>;
  using Queue = Leaf<3, TelItem<ZvTelemetry::Queue>>;
  using Link = Leaf<4, TelItem<ZvTelemetry::Link>>;
  using Engine = Parent<3, TelItem<ZvTelemetry::Engine>, Link>;
  using DBHost = Leaf<4, TelItem<ZvTelemetry::DBHost>>;
  using DBTable = Leaf<4, TelItem<ZvTelemetry::DBTable>>;

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
  // engines
  struct Engines : public BranchChild<ZuTuple<const char *>> {
    static auto telKey() { return TelKey{"engines"}; }
  };
  using EngineParent = Parent<2, Engines, Engine>;

  // db
  ZuDeclTuple(DBTuple,
      (DBHostParent, hosts),
      (DBTableParent, tables));
  using DB = Branch<2, TelItem<ZvTelemetry::DB>, DBTuple>;
  // applications
  ZuDeclTuple(AppTuple,
      (HeapParent, heaps),
      (HashTblParent, hashTbls),
      (ThreadParent, threads),
      (MxParent, mxs),
      (QueueParent, queues),
      (EngineParent, engines),
      (DB, db));
  using App = Branch<1, TelItem<ZvTelemetry::App>, AppTuple>;

  struct Root : public ZGtk::TreeHierarchy::Parent<Root, 0, App> { };

  // map telemetry items to corresponding tree nodes
  inline Heap *row(TelItem<ZvTelemetry::Heap> *item) {
    return item->template gtkRow<Heap>();
  }
  inline HashTbl *row(TelItem<ZvTelemetry::HashTbl> *item) {
    return item->template gtkRow<HashTbl>();
  }
  inline Thread *row(TelItem<ZvTelemetry::Thread> *item) {
    return item->template gtkRow<Thread>();
  }
  inline Mx *row(TelItem<ZvTelemetry::Mx> *item) {
    return item->template gtkRow<Mx>();
  }
  inline Socket *row(TelItem<ZvTelemetry::Socket> *item) {
    return item->template gtkRow<Socket>();
  }
  inline Queue *row(TelItem<ZvTelemetry::Queue> *item) {
    return item->template gtkRow<Queue>();
  }
  inline Engine *row(TelItem<ZvTelemetry::Engine> *item) {
    return item->template gtkRow<Engine>();
  }
  inline Link *row(TelItem<ZvTelemetry::Link> *item) {
    return item->template gtkRow<Link>();
  }
  inline DBTable *row(TelItem<ZvTelemetry::DBTable> *item) {
    return item->template gtkRow<DBTable>();
  }
  inline DBHost *row(TelItem<ZvTelemetry::DBHost> *item) {
    return item->template gtkRow<DBHost>();
  }
  inline DB *row(TelItem<ZvTelemetry::DB> *item) {
    return item->template gtkRow<DB>();
  }
  inline App *row(TelItem<ZvTelemetry::App> *item) {
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
    EngineParent *,	// app->engines (*)
    DB *,		// app->db (*)

    // app grandchildren
    Heap *,		// app->heaps->[heap]
    HashTbl *,		// app->hashTbls->[hashTbl]
    Thread *,		// app->threads->[thread]
    Mx *,		// app->mxs->[mx]
    Queue *,		// app->queues->[queue]
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
      auto p1() {
	return ZvTelemetry::QueueType::name(this->key.template p<1>());
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
	case ZvTelemetry::RAG::Red:
	  m_values[1].set_static_boxed(&m_rag_red_bg);
	  m_values[2].set_static_boxed(&m_rag_red_fg);
	  g_object_setv(G_OBJECT(cell), 3, m_props, m_values);
	  break;
	case ZvTelemetry::RAG::Amber:
	  m_values[1].set_static_boxed(&m_rag_amber_bg);
	  m_values[2].set_static_boxed(&m_rag_amber_fg);
	  g_object_setv(G_OBJECT(cell), 3, m_props, m_values);
	  break;
	case ZvTelemetry::RAG::Green:
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
	m_rag_off_bg =
	  *reinterpret_cast<const GdkRGBA *>(m_values[1].get_boxed());
	m_rag_off_fg =
	  *reinterpret_cast<const GdkRGBA *>(m_values[2].get_boxed());
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

    void final() {
      if (m_treeView) g_object_unref(G_OBJECT(m_treeView));
    }

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

class App_Cli;
class App_Srv;
class SrvLink;

class CliLink_ : public ZcmdCliLink<App_Cli, CliLink_> {
public:
  using Base = ZcmdCliLink<App_Cli, CliLink_>;
  using ID = unsigned;
  using Key = ID;
  Key key() const { return id; }

  template <typename Server>
  CliLink_(App_Cli *, ID, Server &&server, uint16_t port, SrvLink *);

  void loggedIn();
  void disconnected(bool);
  void connectFailed(bool transient);

  int processTelemetry(const uint8_t *data, unsigned len);
  int processDeflt(ZuCSpan id, const uint8_t *data, unsigned len);

  ID			id;
  ZvSeqNo		seqNo = 0;
  Telemetry::Containers	telemetry;
  SrvLink		*srvLink = nullptr;
  bool			connecting = false; // prevent overlapping connects
};
static CliLink_::Key CliLink_KeyAxor(const CliLink_ &link) {
  return link.key();
}
ZuDerive(CliLinks,
  (ZmRBTree<CliLink_,
    ZmRBTreeNode<CliLink_,
      ZmRBTreeKey<CliLink_KeyAxor,
	ZmRBTreeUnique<true,
	  ZmRBTreeLock<ZmPLock,
	    ZmRBTreeHeapID<"CliLink">>>>>>));
using CliLink = CliLinks::Node;

class SrvLink : public ZcmdSrvLink<App_Srv, SrvLink> {
public:
  using Base = ZcmdSrvLink<App_Srv, SrvLink>;
  SrvLink(App_Srv *app);

  int processCmd(const uint8_t *data, unsigned len);
  int processDeflt(ZuCSpan id, const uint8_t *data, unsigned len);

  CliLink		*cliLink = nullptr;
};

class App;

class App_Cli : public ZcmdClient<App_Cli, CliLink_> { };
class App_Srv : public ZcmdServer<App_Srv, SrvLink> {
public:
  void telemetry(ZvTelemetry::App &data);
};

class App :
    public ZmPolymorph,
    public App_Cli,
    public App_Srv,
    public ZGtk::App {
public:
  using Client = ZcmdClient<App_Cli, CliLink_>;
  using FBB = typename Client::FBB;
  using Server = ZcmdServer<App_Srv, SrvLink>;
  using User = Server::User;

#pragma pack(push, 1)
  struct Hdr {
    uintptr_t	cliLink;
    uint16_t	length;
  };
#pragma pack(pop)
  static unsigned SizeAxor(const void *ptr) {
    return reinterpret_cast<const Hdr *>(ptr)->length + sizeof(Hdr);
  }
  class TelRing : public ZiRing<ZmRingSizeAxor<SizeAxor>> {
    TelRing() = delete;
    TelRing(const TelRing &) = delete;
    TelRing &operator =(const TelRing &) = delete;
    TelRing(TelRing &&) = delete;
    TelRing &operator =(TelRing &&) = delete;
  public:
    using Base = ZiRing<ZmRingSizeAxor<SizeAxor>>;
    ~TelRing() = default;
    TelRing(ZiRingParams params) : Base{ZuMv(params)} { }
    bool push(CliLink_ *cliLink, ZuBSpan msg) {
      unsigned n = msg.length();
      if (void *ptr = Base::push(n + sizeof(Hdr))) {
	new (ptr) Hdr{
	  .cliLink = reinterpret_cast<uintptr_t>(cliLink),
	  .length = static_cast<uint16_t>(n)
	};
	memcpy(static_cast<uint8_t *>(ptr) + sizeof(Hdr), msg.data(), n);
	push2(n + sizeof(Hdr));
	return true;
      }
      auto i = writeStatus();
      if (i < 0)
	ZiLOG(Error, "zdash", ([i](auto &s) {
	  s << "ZiRing::push() failed - " << Zi::ioResult(i); }));
      else
	ZiLOG(Error, "zdash", ([i](auto &s) {
	  s << "ZiRing::push() failed - writeStatus=" << i; }));
      return false;
    }
    template <typename L>
    bool shift(L &&l) {
      if (const void *ptr = Base::shift()) {
	auto hdr = reinterpret_cast<const Hdr *>(ptr);
	CliLink_ *cliLink = reinterpret_cast<CliLink_ *>(hdr->cliLink);
	unsigned n = hdr->length;
	ZuFwd<L>(l)(
	  cliLink,
	  ZuSpan(static_cast<const uint8_t *>(ptr) + sizeof(Hdr), n));
	shift2(n + sizeof(Hdr));
	return true;
      }
      return false;
    }
  };

  template <typename T> using TelItem = Telemetry::Item<T>;

  using AppItem = TelItem<ZvTelemetry::App>;
  using DBItem = TelItem<ZvTelemetry::DB>;

  void init(ZiMultiplex *mx, const ZfCf::AnyNode *cf) {
    auto config = ZfCf::handler<AppCf>(cf).ctor();
    if (auto telRingCf = cf->resolve("telRing"))
      m_telRingParams.init(telRingCf);
    else
      m_telRingParams.name("zdash").size(131072);
    // 131072 is ~100mics at 1Gbit/s
    m_telRing = new TelRing{m_telRingParams};
    {
      if (m_telRing->open(TelRing::Read | TelRing::Write) != Zu::OK)
	throw ZeEXCEPT(Error, "zdash",
	    ([name = m_telRingParams.data().name](auto &s) {
	      s << name << ": open failed"; }));
      int r;
      if ((r = m_telRing->reset()) != Zu::OK)
	throw ZeEXCEPT(Error, "zdash",
	    ([name = m_telRingParams.data().name, r](auto &s) {
	      s << name << ": reset failed - " << Zu::ioResult(r); }));
    }

    m_role = config.appRole;

    m_gladePath = ZuMv(config.gtkGlade);
    m_stylePath = ZuMv(config.gtkStyle);

    {
      int64_t refreshRate = config.gtkRefresh * (int64_t)1000000;
      m_refreshQuantum = ZuTime{ZuTime::Nano{refreshRate>>1}};
      if (m_refreshQuantum < mx->params().quantum()) {
	m_refreshQuantum = mx->params().quantum();
	m_refreshRate = m_refreshQuantum + m_refreshQuantum;
      } else
	m_refreshRate = ZuTime{ZuTime::Nano{refreshRate}};
    }
    unsigned gtkTID;
    {
      unsigned nThreads = mx->params().nThreads();
      if (!config.thread || config.thread > nThreads ||
	  !config.gtkThread || config.gtkThread > nThreads)
	throw ZeEXCEPT(Error, "zdash", ([
	  thread = config.thread, gtkThread = config.gtkThread, nThreads
	](auto &s) {
	  s << "thread IDs " << thread << ", " << gtkThread <<
	    " outside [1, " << nThreads << ']';
	}));
      m_sid = config.thread;
      gtkTID = config.gtkThread;
    }

    // both server and client are initialized with the same mx, cf
    // so that all command and cxn processing on both sides is
    // handled by the same thread

    // FIXME - need to call Server::dbCf() then Zdb::init(), then Server::init()
    Server::init(mx, cf);
    static_cast<Server *>(this)->Dispatcher::map("zdash",
	[](void *link, const uint8_t *data, unsigned len) {
	  return static_cast<SrvLink *>(link)->processCmd(data, len);
	});
    static_cast<Server *>(this)->Dispatcher::deflt(
	[](void *link, ZuCSpan id, const uint8_t *data, unsigned len) {
	  return static_cast<SrvLink *>(link)->processDeflt(id, data, len);
	});

    for (unsigned i = 0; i < CmdPerm::N; i++)
      m_cmdPerms[i] = findPerm(
	  ZtString<>{} << "ZDash." <<
	  fbs::EnumNamesReqData()[i - CmdPerm::Offset]);

    Client::init(mx, cf);
    static_cast<Client *>(this)->Dispatcher::deflt(
	[](void *link, ZuCSpan id, const uint8_t *data, unsigned len) {
	  return static_cast<CliLink_ *>(link)->processDeflt(id, data, len);
	});

    ZmTrap::sigintFn(sigint);
    ZmTrap::trap();

    m_uptime = Zm::now();

    i18n(
	cf->get("i18n_domain", "zdash"),
	cf->get("dataDir", DATADIR));

    attach(mx, gtkTID);
    mx->run([this]() { gtkInit(); }, gtkTID);
  }

  void final() {
    detach(ZmFn<>{this, [](App *this_) {
      this_->gtkFinal();
      this_->m_executed.post();
    }});
    m_executed.wait();

    m_telRing->close();

    Client::final();
    Server::final();
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

    m_telRing->attach();
  }

  void gtkDestroyed() {
    m_mainWindow = nullptr;
    post();
  }

  void gtkFinal() {
    m_telRing->detach();

    ZGtk::App::sched()->del(&m_refreshTimer);

    if (m_mainWindow) {
      if (m_mainDestroy)
	g_signal_handler_disconnect(G_OBJECT(m_mainWindow), m_mainDestroy);
      gtk_window_close(m_mainWindow);
      gtk_widget_destroy(GTK_WIDGET(m_mainWindow));
      m_mainWindow = nullptr;
    }
    m_gtkView.final();
    if (m_gtkModel) g_object_unref(G_OBJECT(m_gtkModel));
    if (m_styleContext) g_object_unref(G_OBJECT(m_styleContext));
  }

  void post() { m_done.post(); }
  void wait() { m_done.wait(); }

  void telemetry(ZvTelemetry::App &data) {
    using namespace ZvTelemetry;
    data.id = "ZDash";
    data.version = ZuVerName(); // FIXME
    data.uptime = m_uptime;
    data.role = m_role;
    data.rag = RAG::Green;
  }

  template <typename ...Args>
  void gtkRun(Args &&...args) {
    ZGtk::App::run(ZuFwd<Args>(args)...);
  }
  template <typename ...Args>
  void gtkInvoke(Args &&...args) {
    ZGtk::App::invoke(ZuFwd<Args>(args)...);
  }

  void loggedIn(CliLink_ *cliLink) {
    cliLink->connecting = false;
  }

  void disconnected(CliLink_ *cliLink) {
    cliLink->connecting = false;
    if (auto srvLink = cliLink->srvLink) srvLink->cliLink = nullptr;
    cliLink->srvLink = nullptr;
    m_telRing->push(cliLink, ZuBSpan());
  }
  void disconnected2(CliLink_ *cliLink) {
    // FIXME - update App RAG to red (in caller)
  }

  void connectFailed(CliLink_ *cliLink, bool transient) {
    cliLink->connecting = false;
  }

  void disconnected(SrvLink *srvLink) {
    auto i = m_cliLinks.citer();
    while (auto cliLink = i())
      if (cliLink->srvLink == srvLink)
	cliLink->srvLink = nullptr;
    srvLink->cliLink = nullptr;
  }

  int processTelemetry(CliLink_ *cliLink, const uint8_t *data, unsigned len) {
    using namespace Zfb;
    {
      Verifier verifier(data, len);
      if (!ZvTelemetry::fbs::VerifyTelemetryBuffer(verifier)) return -1;
    }
    if (m_telRing->push(cliLink, {data, len}))
      if (!m_telCount++)
	ZGtk::App::sched()->add(&m_refreshTimer, Zm::now() + m_refreshRate,
	    ZmScheduler::Advance,
	    [this](auto &&arm) {
	      return arm(ZmFn<>{this, [](App *this_) {
		this_->gtkRefresh();
	      }});
	    }, ZGtk::App::sid());
    return len;
  }

  int rejectCmd(SrvLink *srvLink, unsigned len, uint64_t seqNo,
      unsigned code, ZtString<> text) {
    auto text_ = Zfb::Save::str(m_fbb, text);
    fbs::ReqAckBuilder fbb_(m_fbb);
    fbb_.add_seqNo(seqNo);
    fbb_.add_rejCode(code);
    fbb_.add_rejText(text_);
    m_fbb.Finish(fbb_.Finish());
    srvLink->send_(Zcmd::saveHdr(m_fbb, m_id));
    return len;
  }

  int processCmd(SrvLink *srvLink, const uint8_t *data, unsigned len) {
    using namespace Zfb;
    using namespace Load;

    {
      Verifier verifier(data, len);
      if (!fbs::VerifyRequestBuffer(verifier)) return -1;
    }

    auto request_ = fbs::GetRequest(data);
    uint64_t seqNo = request_->seqNo();
    auto reqType = request_->data_type();

    {
      auto perm = m_cmdPerms[CmdPerm::Offset + int(reqType)];
      if (ZuUnlikely(perm < 0)) {
	ZtString<> permName;
	permName << "ZDash." << fbs::EnumNamesReqData()[int(reqType)];
	perm = m_cmdPerms[CmdPerm::Offset + int(reqType)] = findPerm(permName);
	if (ZuUnlikely(perm < 0)) {
	  return rejectCmd(srvLink, len, seqNo, __LINE__, ZtString<>{} <<
	      "permission denied (\"" << permName << "\" missing)");
	}
      }
      if (ZuUnlikely(!ok(srvLink->user(), srvLink->interactive(), perm))) {
	ZtString<> text = "permission denied";
	if (srvLink->user()->flags & ZvUserDB::User::ChPass)
	  text << " (user must change password)";
	return rejectCmd(srvLink, len, seqNo, __LINE__, ZuMv(text));
      }
    }

    const void *reqData_ = request_->data();
    fbs::ReqAckData ackType = fbs::ReqAckData::NONE;
    Offset<void> ackData = 0;

    switch (reqType) {
      case fbs::ReqData::Version:
	ackType = fbs::ReqAckData::VersionAck;
	ackData = fbs::CreateVersion(m_fbb,
	    Save::str(m_fbb, ZuVerName())).Union(); // FIXME
	break;
      case fbs::ReqData::MkLink: {
	auto reqData = static_cast<const fbs::LinkData *>(reqData_);
	auto cliLink =
	  new CliLink{this, m_cliLinkID++,
	    str(reqData->server()), reqData->port(), srvLink};
	m_cliLinks.addNode(cliLink);
	cliLink->srvLink = srvLink;
	srvLink->cliLink = cliLink;
	ackType = fbs::ReqAckData::MkLinkAck;
	ackData = fbs::CreateLink(m_fbb, true, cliLink->id,
	    fbs::CreateLinkData(m_fbb,
	      Save::str(m_fbb, cliLink->server()), cliLink->port())).Union();
      } break;
      case fbs::ReqData::RmLink: {
	auto reqData = static_cast<const fbs::LinkID *>(reqData_);
	auto cliLink = m_cliLinks.del(reqData->id());
	if (!cliLink)
	  return rejectCmd(srvLink, len, seqNo, __LINE__, ZtString<>{} <<
	      "unknown link " << reqData->id());
	ackType = fbs::ReqAckData::RmLinkAck;
	ackData = fbs::CreateLink(m_fbb, false, cliLink->id,
	    fbs::CreateLinkData(m_fbb,
	      Save::str(m_fbb, cliLink->server()), cliLink->port())).Union();
      } break;
      case fbs::ReqData::Connect: {
	auto reqData = static_cast<const fbs::Connect *>(reqData_);
	auto cliLink = m_cliLinks.findPtr(reqData->link()->id());
	if (!cliLink)
	  return rejectCmd(srvLink, len, seqNo, __LINE__, ZtString<>{} <<
	      "unknown link " << reqData->link()->id());
	if (cliLink->connecting)
	  return rejectCmd(srvLink, len, seqNo, __LINE__, ZtString<>{} <<
	      "connect in progress " << reqData->link()->id());
	cliLink->connecting = true;
	cliLink->srvLink = srvLink;
	srvLink->cliLink = cliLink;
	auto loginReq = reqData->loginReq();
	switch (loginReq->data_type()) {
	  case ZvUserDB::fbs::LoginReqData::Login: {
	    auto login =
	      static_cast<const ZvUserDB::fbs::Login *>(loginReq->data());
	    cliLink->login(
		str(login->user()), str(login->passwd()), login->totp());
	  } break;
	  case ZvUserDB::fbs::LoginReqData::Access: {
	    auto access =
	      static_cast<const ZvUserDB::fbs::Access *>(loginReq->data());
	    cliLink->access_(
		str(access->keyID()),
		bytes(access->token()),
		access->stamp(),
		bytes(access->hmac()));
	  } break;
	  default:
	    return rejectCmd(srvLink, len, seqNo, __LINE__, ZtString<>{} <<
		"unknown credentials type " << int(loginReq->data_type()));
	}
	ackType = fbs::ReqAckData::ConnectAck;
	ackData = fbs::CreateLink(m_fbb,
	    cliLink->srvLink == srvLink, cliLink->id,
	    fbs::CreateLinkData(m_fbb,
	      Save::str(m_fbb, cliLink->server()), cliLink->port())).Union();
      } break;
      case fbs::ReqData::Disconnect: {
	auto reqData = static_cast<const fbs::LinkID *>(reqData_);
	auto cliLink = m_cliLinks.findPtr(reqData->id());
	if (!cliLink)
	  return rejectCmd(srvLink, len, seqNo, __LINE__, ZtString<>{} <<
	      "unknown link " << reqData->id());
	cliLink->disconnect();
	ackType = fbs::ReqAckData::ConnectAck;
	ackData = fbs::CreateLink(m_fbb,
	    cliLink->srvLink == srvLink, cliLink->id,
	    fbs::CreateLinkData(m_fbb,
	      Save::str(m_fbb, cliLink->server()), cliLink->port())).Union();
      } break;
      case fbs::ReqData::Links: {
	ZtArray<Zfb::Offset<fbs::Link>> v;
	auto i = m_cliLinks.citer();
	while (auto cliLink = i())
	  v.push(fbs::CreateLink(m_fbb,
		cliLink->srvLink == srvLink, cliLink->id,
		fbs::CreateLinkData(m_fbb,
		  Save::str(m_fbb, cliLink->server()), cliLink->port())));
	auto list_ = m_fbb.CreateVector(v.data(), v.length());
	ackType = fbs::ReqAckData::LinksAck;
	ackData = fbs::CreateLinkList(m_fbb, list_).Union();
      } break;
      case fbs::ReqData::Select: {
	auto reqData = static_cast<const fbs::LinkID *>(reqData_);
	auto cliLink = m_cliLinks.findPtr(reqData->id());
	if (!cliLink)
	  return rejectCmd(srvLink, len, seqNo, __LINE__, ZtString<>{} <<
	      "unknown link " << reqData->id());
	cliLink->srvLink = srvLink;
	srvLink->cliLink = cliLink;
	ackType = fbs::ReqAckData::SelectAck;
	ackData = fbs::CreateLink(m_fbb, true, cliLink->id,
	    fbs::CreateLinkData(m_fbb,
	      Save::str(m_fbb, cliLink->server()), cliLink->port())).Union();
      } break;
      default:
	break;
    }

    {
      fbs::ReqAckBuilder fbb_(m_fbb);
      fbb_.add_seqNo(seqNo);
      fbb_.add_data_type(ackType);
      fbb_.add_data(ackData);
      m_fbb.Finish(fbb_.Finish());
    }
    srvLink->send_(Zcmd::saveHdr(m_fbb, m_id));
    return len;
  }

  // fwd unknown app messages client <-> server using client-selected
  // server-side link (from zdash perspective, SrvLink selects CliLink)
  int processDeflt(
      CliLink_ *cliLink, ZuCSpan, const uint8_t *data, unsigned len) {
    if (auto srvLink = cliLink->srvLink)
      srvLink->send_(data - sizeof(Zcmd::Hdr), len + sizeof(Zcmd::Hdr));
    return len;
  }
  int processDeflt(
      SrvLink *srvLink, ZuCSpan id, const uint8_t *data, unsigned len) {
    if (auto cliLink = srvLink->cliLink)
      cliLink->send_(data - sizeof(Zcmd::Hdr), len + sizeof(Zcmd::Hdr));
    return len;
  }

private:
  void gtkRefresh() {
    // FIXME - freeze, save sort col, unset sort col

    ZuTime deadline = Zm::now() + m_refreshQuantum;
    unsigned i = 0, n;
    while (m_telRing->shift([](
	    CliLink_ *cliLink, const ZuBSpan &msg) {
      static_cast<App *>(cliLink->app())->processTel2(cliLink, msg);
    })) {
      do {
	if (!(n = m_telCount.load_())) break;
      } while (m_telCount.cmpXch(n - 1, n) != n);
      if (!(++i & 0xf) && Zm::now() >= deadline) break;
    }

    // FIXME - restore sort col, thaw

    if (n)
      ZGtk::App::sched()->add(&m_refreshTimer, Zm::now() + m_refreshRate,
	  ZmScheduler::Defer,
	  [this](auto &&arm) {
	    return arm(ZmFn<>{this, [](App *this_) { this_->gtkRefresh(); }});
	  }, ZGtk::App::sid());
  }
  void processTel2(CliLink_ *cliLink, const ZuBSpan &msg_) {
    if (ZuUnlikely(!msg_)) {
      disconnected2(cliLink);
      return;
    }
    using namespace ZvTelemetry;
    auto msg = ZvTelemetry::fbs::GetTelemetry(msg_);
    int i = int(msg->data_type());
    if (ZuUnlikely(i < int(TelData::First))) return;
    if (ZuUnlikely(i > int(TelData::MAX))) return;
    ZuSwitch::dispatch<TelData::N - TelData::First>(i - int(TelData::First),
	[this, cliLink, msg](auto i) {
      using FBType = TelData::Type<i + int(TelData::First)>;
      auto fbo = static_cast<const FBType *>(msg->data());
      processTel3(cliLink, fbo);
    });
  }
  template <typename FBType>
  ZuIsNot<FBType, ZvTelemetry::fbs::Alert>
  processTel3(CliLink_ *cliLink, const FBType *fbo) {
    ZuTypeIndex<FBType, Telemetry::FBTypeList> I;
    using T = ZuType<I, Telemetry::TypeList>;
    auto &container = cliLink->telemetry.p<I>();
    using Item = TelItem<T>;
    Item *item;
    if (item = container.lookup(fbo)) {
      ZfbStruct::update(item->data, fbo);
      m_gtkModel->updated(GtkTree::row(item));
    } else {
      item = new Item{cliLink, fbo};
      container.add(item);
      addGtkRow(cliLink, item);
    }
  }
  void addGtkRow(CliLink_ *cliLink, AppItem *item) {
    item->initTelKey(cliLink->server(), cliLink->port());
    m_gtkModel->add(new GtkTree::App{item}, m_gtkModel->root());
  }
  AppItem *appItem(CliLink_ *cliLink) {
    ZuTypeIndex<ZvTelemetry::App, ZvTelemetry::TypeList> i;
    auto &container = cliLink->telemetry.p<i>();
    auto item = container.lookup(
	static_cast<const ZvTelemetry::fbs::App *>(nullptr));
    if (!item) {
      item = new TelItem<ZvTelemetry::App>{cliLink};
      container.add(item);
      addGtkRow(cliLink, item);
    }
    return item;
  }
  DBItem *dbItem(CliLink_ *cliLink) {
    ZuTypeIndex<ZvTelemetry::DB, ZvTelemetry::TypeList> i;
    auto &container = cliLink->telemetry.p<i>();
    auto item = container.lookup(
	static_cast<const ZvTelemetry::fbs::DB *>(nullptr));
    if (!item) {
      item = new TelItem<ZvTelemetry::DB>{cliLink};
      container.add(item);
      addGtkRow(cliLink, item);
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
  void addGtkRow(CliLink_ *cliLink, TelItem<ZvTelemetry::Heap> *item) {
    addGtkRow_(appItem(cliLink), item,
	[](GtkTree::App *_) -> GtkTree::HeapParent & {
	  return _->heaps();
	});
  }
  void addGtkRow(CliLink_ *cliLink, TelItem<ZvTelemetry::HashTbl> *item) {
    addGtkRow_(appItem(cliLink), item,
	[](GtkTree::App *_) -> GtkTree::HashTblParent & {
	  return _->hashTbls();
	});
  }
  void addGtkRow(CliLink_ *cliLink, TelItem<ZvTelemetry::Thread> *item) {
    addGtkRow_(appItem(cliLink), item,
	[](GtkTree::App *_) -> GtkTree::ThreadParent & {
	  return _->threads();
	});
  }
  void addGtkRow(CliLink_ *cliLink, TelItem<ZvTelemetry::Mx> *item) {
    addGtkRow_(appItem(cliLink), item,
	[](GtkTree::App *_) -> GtkTree::MxParent & { return _->mxs(); });
  }
  void addGtkRow(CliLink_ *cliLink, TelItem<ZvTelemetry::Socket> *item) {
    ZuTypeIndex<ZvTelemetry::Mx, ZvTelemetry::TypeList> i;
    auto &mxContainer = cliLink->telemetry.p<i>();
    auto mxItem = mxContainer.find(ZuFwdTuple(item->data.mxID));
    if (!mxItem) {
      auto mxItem = new TelItem<ZvTelemetry::Mx>{cliLink};
      mxItem->data.id = item->data.mxID;
      mxContainer.add(mxItem);
      addGtkRow(cliLink, mxItem);
    }
    m_gtkModel->add(new GtkTree::Socket{item}, GtkTree::row(mxItem));
  }
  void addGtkRow(CliLink_ *cliLink, TelItem<ZvTelemetry::Queue> *item) {
    addGtkRow_(appItem(cliLink), item,
	[](GtkTree::App *_) -> GtkTree::QueueParent & {
	  return _->queues();
	});
  }
  void addGtkRow(CliLink_ *cliLink, TelItem<ZvTelemetry::Engine> *item) {
    addGtkRow_(appItem(cliLink), item,
	[](GtkTree::App *_) -> GtkTree::EngineParent & {
	  return _->engines();
	});
  }
  void addGtkRow(CliLink_ *cliLink, TelItem<ZvTelemetry::Link> *item) {
    ZuTypeIndex<ZvTelemetry::Engine, ZvTelemetry::TypeList> i;
    auto &engContainer = cliLink->telemetry.p<i>();
    auto engItem =
      engContainer.find(ZuFwdTuple(item->data.engineID));
    if (!engItem) {
      auto engItem = new TelItem<ZvTelemetry::Mx>{cliLink};
      engItem->data.id = item->data.engineID;
      engContainer.add(engItem);
      addGtkRow(cliLink, engItem);
    }
    m_gtkModel->add(new GtkTree::Link{item}, GtkTree::row(engItem));
  }
  void addGtkRow(CliLink_ *cliLink, DBItem *item) {
    auto appGtkRow = GtkTree::row(appItem(cliLink));
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
  void addGtkRow(CliLink_ *cliLink, TelItem<ZvTelemetry::DBHost> *item) {
    addGtkRow_(dbItem(cliLink), item,
	[](GtkTree::DB *_) -> GtkTree::DBHostParent & {
	  return _->hosts();
	});
  }
  void addGtkRow(CliLink_ *cliLink, TelItem<ZvTelemetry::DBTable> *item) {
    addGtkRow_(dbItem(cliLink), item,
	[](GtkTree::DB *_) -> GtkTree::DBTableParent & {
	  return _->tables();
	});
  }
  template <typename FBType>
  ZuIs<FBType, ZvTelemetry::fbs::Alert>
  processTel3(CliLink_ *cliLink, const FBType *fbo) {
    ZuTypeIndex<FBType, Telemetry::FBTypeList> i;
    using T = ZuType<i, Telemetry::TypeList>;
    auto &container = cliLink->telemetry.p<i>();
    processAlert(new (container.data.push()) T{ZfbStruct::ctor<T>(fbo)});
  }

  void processAlert(const ZvTelemetry::Alert *) {
    // FIXME - update alerts in UX
  }

private:
  ZmSemaphore		m_done;
  ZmSemaphore		m_executed;

  CliLink::ID		m_cliLinkID = 0;
  CliLinks		m_cliLinks;

  struct CmdPerm {
    enum {
      Offset = -(int(fbs::ReqData::NONE) + 1),
      N = int(fbs::ReqData::MAX) - int(fbs::ReqData::NONE)
    };
  };
  int			m_cmdPerms[CmdPerm::N];
  ZuID			m_id = "zdash";
  FBB			m_fbb;

  int			m_role;	// ZvTelemetry::AppRole
  ZuDateTime		m_uptime;
  unsigned		m_sid = 0;

  ZvRingParams		m_telRingParams;
  ZuPtr<TelRing>	m_telRing;
  ZmAtomic<unsigned>	m_telCount = 0;

  // FIXME - need Zdf in-memory manager (later, file manager)

  ZtString<>		m_gladePath;
  ZtString<>		m_stylePath;
  GtkStyleContext	*m_styleContext = nullptr;
  GtkWindow		*m_mainWindow = nullptr;
  gulong		m_mainDestroy = 0;

  ZuTime		m_refreshQuantum;
  ZuTime		m_refreshRate;
  ZmScheduler::Timer	m_refreshTimer;

  GtkTree::View		m_gtkView;
  GtkTree::Model	*m_gtkModel;
};

inline void App_Srv::telemetry(ZvTelemetry::App &data)
{
  static_cast<ZDash::App *>(this)->telemetry(data);
}

template <typename Server>
inline CliLink_::CliLink_(
    App_Cli *app, ID id_, Server &&server, uint16_t port, SrvLink *srvLink_) :
    Base{app, ZuFwd<Server>(server), port}, id{id_}, srvLink{srvLink_} { }

inline void CliLink_::loggedIn()
{
  static_cast<ZDash::App *>(this->app())->loggedIn(this);
}
inline void CliLink_::disconnected(bool peer)
{
  static_cast<ZDash::App *>(this->app())->disconnected(this);
  Base::disconnected(peer);
}
inline void CliLink_::connectFailed(bool transient)
{
  static_cast<ZDash::App *>(this->app())->connectFailed(this, transient);
}

inline int CliLink_::processTelemetry(const uint8_t *data, unsigned len)
{
  return static_cast<ZDash::App *>(
      this->app())->processTelemetry(this, data, len);
}
inline int CliLink_::processDeflt(
  ZuCSpan id, const uint8_t *data, unsigned len)
{
  return static_cast<ZDash::App *>(
      this->app())->processDeflt(this, id, data, len);
}

inline SrvLink::SrvLink(App_Srv *app) : Base{app} { }

inline int SrvLink::processCmd(const uint8_t *data, unsigned len)
{
  return static_cast<ZDash::App *>(
      this->app())->processCmd(this, data, len);
}
inline int SrvLink::processDeflt(
  ZuCSpan id, const uint8_t *data, unsigned len)
{
  return static_cast<ZDash::App *>(
      this->app())->processDeflt(this, id, data, len);
}

} // namespace ZDash

ZmRef<ZDash::App> app;

void sigint() { if (app) app->post(); }

int main(int argc, char **argv)
{
  if (argc != 1) usage();

  ZiLog::init("zcmd");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::lambdaSink([](ZeLogBuf &buf, const ZeEventInfo &) {
    buf << '\n';
    std::cerr << buf << std::flush;
  }));
  ZiLog::start();

  ZiMultiplex *mx = new ZiMultiplex(
      ZiMxParams{}
	.scheduler([](auto &s) {
	  s.nThreads(5)
	  .thread(1, [](auto &t) { t.isolated(1); })
	  .thread(2, [](auto &t) { t.isolated(1); })
	  .thread(3, [](auto &t) { t.isolated(1); })
	  .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  mx->start();

  app = new ZDash::App{};

  {
    ZmRef<ZfCf::Defines> defines = new ZfCf::Defines();
    auto caPath = ::getenv("ZCMD_CAPATH");
    defines->add(
      ZfCf::DefKey{"CAPATH"},
      ZfCf::DefVal{caPath ? caPath : "/etc/ssl/certs"});
    auto scan = ZfCf::scan(
      "timeout: 1,\n"
      "thread: 3,\n"
      "gtkThread: 4,\n"
      "gtkGlade: zdash.glade,\n"
      "caPath: ${CAPATH},\n"
      "rxThread: 1,\n"
      "txThread: 2\n",
      {}, ZuMv(defines));
    auto cf = ZuMv(scan.p<1>());
    try {
      app->init(mx, cf);
    } catch (const ZeException &e) {
      std::cerr << e << '\n' << std::flush;
      ::exit(1);
    } catch (...) {
      std::cerr << "unknown exception\n" << std::flush;
      ::exit(1);
    }
  }

  app->wait();

  app->final();

  mx->stop();

  ZiLog::stop();

  delete mx;

  ZmTrap::sigintFn(nullptr);
}
