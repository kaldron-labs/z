//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Gtk tree model wrapper

#ifndef ZGtkTreeModel_HH
#define ZGtkTreeModel_HH

#ifndef ZGtkLib_HH
#include <zlib/ZGtkLib.hh>
#endif

#include <typeinfo>

#include <zlib/ZuSort.hh>
#include <zlib/ZuSearch.hh>
#include <zlib/ZuInvoke.hh>

#include <zlib/ZmPLock.hh>

#include <zlib/ZGtkValue.hh>

namespace ZGtk {

// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public TreeModel<Impl> {
  GtkTreeModelFlags get_flags();
  gint get_n_columns();
  GType get_column_type(gint i);
  gboolean get_iter(GtkTreeIter *iter, GtkTreePath *path);
  GtkTreePath *get_path(GtkTreeIter *iter);
  void get_value(GtkTreeIter *iter, gint i, Value *value);
  gboolean iter_next(GtkTreeIter *iter);
  gboolean iter_children(GtkTreeIter *iter, GtkTreeIter *parent);
  gboolean iter_has_child(GtkTreeIter *iter);
  gint iter_n_children(GtkTreeIter *iter);
  gboolean iter_nth_child(GtkTreeIter *iter, GtkTreeIter *parent, gint n);
  gboolean iter_parent(GtkTreeIter *iter, GtkTreeIter *child);

  gboolean get_sort_column_id(gint *column, GtkSortType *order);
  void set_sort_column_id(gint column, GtkSortType order);
};
#endif

struct TreeModelDragData {
  GList *events = nullptr;
  guint	handler = 0;		// button release handler
};

template <typename Impl>
class TreeModel : public GObject {
public:
  static const char *typeName() {
    static const char *name = nullptr;
    if (!name) name = typeid(Impl).name();
    return name;
  }
  static GdkAtom rowsAtom() {
    static GdkAtom atom = nullptr;
    if (!atom) atom = gdk_atom_intern_static_string(typeName());
    return atom;
  }
  static constexpr gint nRowsTargets() { return 1; }
  static const GtkTargetEntry *rowsTargets() {
    static GtkTargetEntry rowsTargets_[] = {
      { (gchar *)nullptr, GTK_TARGET_SAME_APP, 0 }
    };
    if (!rowsTargets_[0].target) rowsTargets_[0].target =
      const_cast<gchar *>(reinterpret_cast<const gchar *>(typeName()));
    return rowsTargets_;
  }

private:
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  template <typename Ptr>
  static auto impl(Ptr *ptr) {
    return static_cast<Impl *>(reinterpret_cast<TreeModel *>(ptr));
  }

  static GType gtype_init() {
    static const GTypeInfo gtype_info{
      sizeof(GObjectClass),
      nullptr,					// base_init
      nullptr,					// base_finalize
      [](void *c_, void *) {
	auto c = static_cast<GObjectClass *>(c_);
	c->finalize = [](GObject *m) {
	  impl(m)->~Impl();
	};
      },
      nullptr,					// class finalize
      nullptr,					// class_data
      sizeof(Impl),
      0,					// n_preallocs
      [](GTypeInstance *m, void *) {
	char object[sizeof(GObject)];
	memcpy(object, reinterpret_cast<const GObject *>(m), sizeof(GObject));
	new (m) Impl{};
	memcpy(reinterpret_cast<GObject *>(m), object, sizeof(GObject));
      }
    };

    static const GInterfaceInfo tree_model_info{
      [](void *i_, void *) {
	auto i = static_cast<GtkTreeModelIface *>(i_);
	i->get_flags = [](GtkTreeModel *m) -> GtkTreeModelFlags {
	  g_return_val_if_fail(
	      G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()),
	      (static_cast<GtkTreeModelFlags>(0)));
	  return impl(m)->get_flags();
	};
	i->get_n_columns = [](GtkTreeModel *m) -> gint {
	  g_return_val_if_fail(G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()), 0);
	  return impl(m)->get_n_columns();
	};
	i->get_column_type = [](GtkTreeModel *m, gint i) -> GType {
	  g_return_val_if_fail(
	      G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()), G_TYPE_INVALID);
	  return impl(m)->get_column_type(i);
	};
	i->get_iter = [](GtkTreeModel *m,
	    GtkTreeIter *iter, GtkTreePath *path) -> gboolean {
	  g_return_val_if_fail(
	      G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()), false);
	  g_return_val_if_fail(!!path, false);
	  return impl(m)->get_iter(iter, path);
	};
	i->get_path = [](
	    GtkTreeModel *m, GtkTreeIter *iter) -> GtkTreePath * {
	  g_return_val_if_fail(
	      G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()), nullptr);
	  g_return_val_if_fail(!!iter, nullptr);
	  return impl(m)->get_path(iter);
	};
	i->get_value = [](GtkTreeModel *m,
	    GtkTreeIter *iter, gint i, GValue *value) {
	  g_return_if_fail(G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()));
	  g_return_if_fail(!!iter);
	  impl(m)->get_value(iter, i, reinterpret_cast<Value *>(value));
	};
	i->iter_next = [](GtkTreeModel *m, GtkTreeIter *iter) -> gboolean {
	  g_return_val_if_fail(
	      G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()), false);
	  g_return_val_if_fail(!!iter, false);
	  return impl(m)->iter_next(iter);
	};
	i->iter_children = [](GtkTreeModel *m,
	    GtkTreeIter *iter, GtkTreeIter *parent) -> gboolean {
	  g_return_val_if_fail(
	      G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()), false);
	  return impl(m)->iter_children(iter, parent);
	};
	i->iter_has_child = [](
	    GtkTreeModel *m, GtkTreeIter *iter) -> gboolean {
	  g_return_val_if_fail(
	      G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()), false);
	  g_return_val_if_fail(!!iter, false);
	  return impl(m)->iter_has_child(iter);
	};
	i->iter_n_children = [](GtkTreeModel *m, GtkTreeIter *iter) -> gint {
	  g_return_val_if_fail(G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()), 0);
	  return impl(m)->iter_n_children(iter);
	};
	i->iter_nth_child = [](GtkTreeModel *m,
	    GtkTreeIter *iter, GtkTreeIter *parent, gint n) -> gboolean {
	  g_return_val_if_fail(G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()), false);
	  return impl(m)->iter_nth_child(iter, parent, n);
	};
	i->iter_parent = [](GtkTreeModel *m,
	    GtkTreeIter *iter, GtkTreeIter *child) -> gboolean {
	  g_return_val_if_fail(G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()), false);
	  g_return_val_if_fail(!!child, false);
	  return impl(m)->iter_parent(iter, child);
	};
	i->ref_node = [](GtkTreeModel *m, GtkTreeIter *iter) {
	  g_return_if_fail(G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()));
	  g_return_if_fail(!!iter);
	  return impl(m)->ref_node(iter);
	};
	i->unref_node = [](GtkTreeModel *m, GtkTreeIter *iter) {
	  g_return_if_fail(G_TYPE_CHECK_INSTANCE_TYPE((m), gtype()));
	  g_return_if_fail(!!iter);
	  return impl(m)->unref_node(iter);
	};
      },
      nullptr,
      nullptr
    };

    static const GInterfaceInfo tree_sortable_info{
      [](void *i_, void *) {
	auto i = static_cast<GtkTreeSortableIface *>(i_);
	i->get_sort_column_id = [](GtkTreeSortable *s,
	    gint *column, GtkSortType *order) -> gboolean {
	  g_return_val_if_fail(G_TYPE_CHECK_INSTANCE_TYPE((s), gtype()), false);
	  return impl(s)->get_sort_column_id(column, order);
	};
	i->set_sort_column_id = [](GtkTreeSortable *s,
	    gint column, GtkSortType order) {
	  g_return_if_fail(G_TYPE_CHECK_INSTANCE_TYPE((s), gtype()));
	  impl(s)->set_sort_column_id(column, order);
	};
	i->has_default_sort_func = [](GtkTreeSortable *s) -> gboolean {
	  return false;
	};
      },
      nullptr,
      nullptr
    }; 

    GType gtype_ = g_type_register_static(
	G_TYPE_OBJECT, "TreeModel", &gtype_info, (GTypeFlags)0);
    g_type_add_interface_static(gtype_,
	GTK_TYPE_TREE_MODEL, &tree_model_info);
    g_type_add_interface_static(gtype_,
	GTK_TYPE_TREE_SORTABLE, &tree_sortable_info);

    return gtype_;
  }

public:
  static GType gtype() {
    static GType gtype_ = 0;
    if (!gtype_) gtype_ = gtype_init();
    return gtype_;
  }

  static Impl *ctor() {
    return reinterpret_cast<Impl *>(g_object_new(gtype(), nullptr));
  }

  // click (popup menu, etc.)
  // Click(TreeModel *model, GtkWidget *widget, unsigned n) -> Fn
  // Fn(GtkTreeIter *iter)
  // - called once per row, n times
  template <
    int Type, unsigned Button, unsigned Mask, unsigned State, auto Click>
  void click(GtkTreeView *view) {
    g_signal_connect(G_OBJECT(view), "button-press-event",
	ZGtk::callback([](GtkWidget *widget,
	    GdkEventButton *event, gpointer) -> gboolean {
	  auto view = GTK_TREE_VIEW(widget);
	  g_return_val_if_fail(!!view, false);
	  if (event->type != Type ||
	      event->button != Button ||
	      (event->state & Mask) != State) return false;
	  GtkTreePath *path = nullptr;
	  GtkTreeViewColumn *column = nullptr;
	  gint cell_x, cell_y;
	  gtk_tree_view_get_path_at_pos(
	      view, event->x, event->y, &path, &column, &cell_x, &cell_y);
	  if (!path) return false;
	  auto selection = gtk_tree_view_get_selection(view);
	  if (!gtk_tree_selection_path_is_selected(selection, path)) {
	    gtk_tree_selection_unselect_all(selection);
	    gtk_tree_selection_select_path(selection, path);
	  }
	  gtk_tree_path_free(path);
	  GtkTreeModel *model = nullptr;
	  auto rows = gtk_tree_selection_get_selected_rows(selection, &model);
	  if (!rows) return false;
	  g_return_val_if_fail(!!model, false);
	  auto fn = ZuInvoke<Click>(impl(model), widget, g_list_length(rows));
	  for (GList *i = g_list_first(rows); i; i = g_list_next(i)) {
	    auto path = reinterpret_cast<GtkTreePath *>(i->data);
	    GtkTreeIter iter;
	    if (gtk_tree_model_get_iter(model, &iter, path)) fn(&iter);
	    gtk_tree_path_free(path);
	  }
	  g_list_free(rows);
	  return true;
	}), 0);
  }

  // multiple row drag-and-drop support
  void drag(GtkTreeView *view) {
    gtk_drag_source_set(GTK_WIDGET(view),
	GDK_BUTTON1_MASK,
	rowsTargets(), nRowsTargets(),
	GDK_ACTION_COPY);

    g_signal_connect(G_OBJECT(view), "drag-data-get",
	ZGtk::callback([](GObject *o, GdkDragContext *,
	    GtkSelectionData *data, guint, guint) -> gboolean {
	  auto view = GTK_TREE_VIEW(o);
	  g_return_val_if_fail(!!view, false);
	  auto selection = gtk_tree_view_get_selection(view);
	  if (!selection) return false;
	  GtkTreeModel *model = nullptr;
	  auto rows = gtk_tree_selection_get_selected_rows(selection, &model);
	  if (!rows) return false;
	  g_return_val_if_fail(!!model, false);
	  gtk_selection_data_set(data, rowsAtom(), sizeof(rows)<<3,
	      reinterpret_cast<const guchar *>(&rows), sizeof(rows));
	  return true;
	}), 0);

    g_signal_connect(G_OBJECT(view), "drag-end",
	ZGtk::callback([](GtkWidget *widget,
	    GdkEventButton *event, gpointer) {
	  TreeModelDragData *dragData =
	    reinterpret_cast<TreeModelDragData *>(
		g_object_get_data(G_OBJECT(widget), typeName()));
	  g_return_if_fail(!!dragData);
	  dragEnd(widget, dragData);
	}), 0);
 
    g_signal_connect(G_OBJECT(view), "button-press-event",
	ZGtk::callback([](GtkWidget *widget,
	    GdkEventButton *event, gpointer) -> gboolean {
	  auto view = GTK_TREE_VIEW(widget);
	  g_return_val_if_fail(!!view, false);
	  TreeModelDragData *dragData =
	    reinterpret_cast<TreeModelDragData *>(
		g_object_get_data(G_OBJECT(view), typeName()));
	  if (!dragData) {
	    auto &dragData_ = ZmTLS<TreeModelDragData, &TreeModel::drag>();
	    g_object_set_data(G_OBJECT(view), typeName(), &dragData_);
	    dragData = &dragData_;
	  }
	  if (g_list_find(dragData->events, event)) return false;
	  if (dragData->events) {
	    dragData->events = g_list_append(dragData->events,
		gdk_event_copy(reinterpret_cast<GdkEvent *>(event)));
	    return true;
	  }
	  if (event->type != GDK_BUTTON_PRESS) return false;
	  GtkTreePath *path = nullptr;
	  GtkTreeViewColumn *column = nullptr;
	  gint cell_x, cell_y;
	  gtk_tree_view_get_path_at_pos(
	      view, event->x, event->y, &path, &column, &cell_x, &cell_y);
	  if (!path) return false;
	  auto selection = gtk_tree_view_get_selection(view);
	  bool drag = gtk_tree_selection_path_is_selected(selection, path);
	  bool call_parent =
	    !drag ||
	    (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) ||
	    event->button != 1;
	  if (call_parent) {
	    (GTK_WIDGET_GET_CLASS(view))->button_press_event(widget, event);
	    drag = gtk_tree_selection_path_is_selected(selection, path);
	  }
	  gtk_tree_path_free(path);
	  if (!drag) return call_parent;
	  if (!call_parent)
	    dragData->events = g_list_append(dragData->events,
		gdk_event_copy((GdkEvent*)event));
	  dragData->handler =
	    g_signal_connect(G_OBJECT(view), "button-release-event",
		ZGtk::callback([](GtkWidget *widget,
		    GdkEventButton *event, gpointer) -> gboolean {
		  TreeModelDragData *dragData =
		    reinterpret_cast<TreeModelDragData *>(
			g_object_get_data(G_OBJECT(widget), typeName()));
		  g_return_val_if_fail(!!dragData, false);
		  for (GList *l = dragData->events; l; l = l->next)
		    gtk_propagate_event(widget,
			reinterpret_cast<GdkEvent *>(l->data));
		  dragEnd(widget, dragData);
		  return false;
		}), 0);
	  return true;
	}), 0);
  }
  // Drop(TreeModel *model, GtkWidget *widget, unsigned n) -> Fn
  // Fn(GtkTreeIter *iter)
  // - called once per row, n times
private:
  template <auto Drop>
  bool drop_(GtkWidget *widget, GtkSelectionData *data) {
    if (gtk_selection_data_get_data_type(data) != rowsAtom()) return false;
    gint length;
    auto ptr = gtk_selection_data_get_data_with_length(data, &length);
    if (length != sizeof(GList *)) return true;
    auto model = GTK_TREE_MODEL(this);
    auto rows = *reinterpret_cast<GList *const *>(ptr);
    auto fn = ZuInvoke<Drop>(impl(model), widget, g_list_length(rows));
    for (GList *i = g_list_first(rows); i; i = g_list_next(i)) {
      auto path = reinterpret_cast<GtkTreePath *>(i->data);
      GtkTreeIter iter;
      if (gtk_tree_model_get_iter(model, &iter, path)) fn(&iter);
      gtk_tree_path_free(path);
    }
    g_list_free(rows);
    return true;
  }
public:
  template <auto Drop>
  void drop(GtkWidget *dest) {
    gtk_drag_dest_set(dest,
	GTK_DEST_DEFAULT_ALL,
	TreeModel::rowsTargets(), TreeModel::nRowsTargets(),
	GDK_ACTION_COPY);

    g_signal_connect(G_OBJECT(dest), "drag-data-received",
	ZGtk::callback([](GtkWidget *widget, GdkDragContext *, int, int,
	    GtkSelectionData *data, guint, guint32, gpointer model) {
	  if (impl(model)->template drop_<Drop>(widget, data))
	    g_signal_stop_emission_by_name(widget, "drag-data-received");
	}), this);
  }

private:
  static void dragEnd(GtkWidget *widget, TreeModelDragData *dragData) {
    for (GList *l = dragData->events; l; l = l->next)
      gdk_event_free(reinterpret_cast<GdkEvent *>(l->data));
    if (dragData->events) {
      g_list_free(dragData->events);
      dragData->events = nullptr;
    }
    if (dragData->handler) {
      g_signal_handler_disconnect(widget, dragData->handler);
      dragData->handler = 0;
    }
  }

public:
  // defaults for unsorted model
  gboolean get_sort_column_id(gint *column, GtkSortType *order) {
    if (column) *column = GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID;
    if (order) *order = GTK_SORT_ASCENDING;
    return false;
  }
  void set_sort_column_id(gint column, GtkSortType order) {
    return;
  }
};

template <
  typename Impl,
  int DefaultCol = GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
  int DefaultOrder = GTK_SORT_ASCENDING>
class TreeSortable : public TreeModel<Impl> {
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

public:
  gboolean get_sort_column_id(gint *col, GtkSortType *order) {
    if (col) *col = m_col;
    if (order) *order = m_order;
    switch ((int)m_col) {
      case GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID:
      case GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID:
	return false;
      default:
	return true;
    }
  }
  void set_sort_column_id(gint col, GtkSortType order) {
    if (ZuUnlikely(m_col == col && m_order == order)) return;

    m_col = col;
    m_order = order;

    // emit #GtkTreeSortable::sort-column-changed
    gtk_tree_sortable_sort_column_changed(GTK_TREE_SORTABLE(this));

    impl()->sort(m_col, m_order);
  }

private:
  gint		m_col = DefaultCol;
  GtkSortType	m_order = static_cast<GtkSortType>(DefaultOrder);
};

// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public TreeArray<Impl, Iter, Data> {
  // call count(unsigned), then add(Iter) for each item
  template <typename Count, typename Add>
  void load(Count count, Add add);

  // get data given ptr
  Data *data(const Iter &iter);

  // get/set row# given iter
  void row(const Iter &iter, gint v);
  gint row(const Iter &iter);

  // get print format given column
  const ZtFieldVFmt &fmt(unsigned col);
};
#endif
// Note: following load() and association with a view, Impl must call
// add() and del() to inform Gtk about subsequent modifications to the model
template <typename Impl, typename Iter, typename Data>
class TreeArray : public TreeSortable<TreeArray<Impl, Iter, Data>> {
  ZuAssert(sizeof(Iter) <= sizeof(GtkTreeIter));
  ZuAssert(ZuTraits<Iter>::IsPOD);

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

public:
  void load(gint col, GtkSortType order) {
    this->set_sort_column_id(col, order);
    impl()->load(
	[this](unsigned count) { m_rows.size(count); },
	[this](Iter iter) {
	  impl()->row(iter, m_rows.length());
	  m_rows.push(ZuMv(iter));
	});
  }
  GtkTreeModelFlags get_flags() {
    return static_cast<GtkTreeModelFlags>(
	GTK_TREE_MODEL_LIST_ONLY | GTK_TREE_MODEL_ITERS_PERSIST);
  }
  gint get_n_columns() {
    auto fields = Data::fields();
    return fields.length();
  }
  GType get_column_type(gint i) {
    return G_TYPE_STRING;
  }
  gboolean get_iter(GtkTreeIter *iter, GtkTreePath *path) {
    gint depth = gtk_tree_path_get_depth(path);
    if (depth != 1) return false;
    gint *indices = gtk_tree_path_get_indices(path);
    auto row = indices[0];
    if (ZuUnlikely(row < 0 || row >= m_rows.length())) return false;
    new (iter) Iter{m_rows[row]};
  }
  GtkTreePath *get_path(GtkTreeIter *iter) {
    gint row = impl()->row(*reinterpret_cast<Iter *>(iter));
    return gtk_tree_path_new_from_indicesv(&row, 1);
  }
  void get_value(GtkTreeIter *iter, gint col, Value *value) {
    auto fields = Data::fields();
    // FIXME - use on-stack string buffer
    static ZtString<> s; // ok - this is single-threaded
    s.length(0);
    fields[col].print(
	s, impl()->data(*reinterpret_cast<Iter *>(iter)), impl()->fmt(col));
    value->init(G_TYPE_STRING);
    value->set_static_string(s);
  }
  gboolean iter_next(GtkTreeIter *iter) {
    auto row = impl()->row(*reinterpret_cast<Iter *>(iter));
    if (ZuUnlikely(++row >= m_rows.length())) return false;
    new (iter) Iter{m_rows[row]};
  }
  gboolean iter_children(GtkTreeIter *iter, GtkTreeIter *parent) {
    if (parent) return false;
    new (iter) Iter{m_rows[0]};
    return true;
  }
  gboolean iter_has_child(GtkTreeIter *parent) {
    return !parent;
  }
  gint iter_n_children(GtkTreeIter *parent) {
    if (parent) return 0;
    return m_rows.length();
  }
  gboolean iter_nth_child(GtkTreeIter *iter, GtkTreeIter *parent, gint row) {
    if (parent) return false;
    if (ZuUnlikely(row < 0 || row >= m_rows.length())) return false;
    new (iter) Iter{m_rows[row]};
    return true;
  }
  gboolean iter_parent(GtkTreeIter *iter, GtkTreeIter *child) {
    return false;
  }
  void ref_node(GtkTreeIter *) { }
  void unref_node(GtkTreeIter *) { }

private:
  auto cmp_(gint col, GtkSortType order) {
    return [impl = impl(), col, descending = order == GTK_SORT_DESCENDING](
	const Iter &i1, const Iter &i2) {
      int v = Data::fields()[col].cmp(impl->data(i1), impl->data(i2));
      if (descending) v = -v;
      return v;
    };
  }

public:
  void sort(gint col, GtkSortType order) {
    unsigned n = m_rows.length();
    ZuSort(&m_rows[0], n, cmp_(col, order));
    ZtArray<gint> new_order(n);
    for (unsigned i = 0; i < n; i++) {
      new_order.push(impl()->row(m_rows[i]));
      impl()->row(m_rows[i], i);
    }
    GtkTreePath *path = gtk_tree_path_new();
    // emit #GtkTreeModel::rows-reordered
    gtk_tree_model_rows_reordered(
	GTK_TREE_MODEL(this), path, nullptr, new_order.data());
    gtk_tree_path_free(path);
  }

  void add(Iter iter) {
    gint col;
    GtkSortType order;
    gint row;
    if (this->get_sort_column_id(&col, &order)) {
      row = ZuSearchPos(ZuSearch<false>(m_rows.length(),
	[this, &iter, fn = cmp_(col, order)](unsigned i) {
	  return fn(iter, m_rows[i]);
	}));
      impl()->row(iter, row);
      m_rows.splice(row, 0, ZuMv(iter));
      for (unsigned i = row + 1, n = m_rows.length(); i < n; i++)
	impl()->row(m_rows[i], i);
    } else {
      row = m_rows.length();
      impl()->row(iter, row);
      m_rows.push(ZuMv(iter));
    }
    GtkTreeIter iter_;
    new (&iter_) Iter{iter};
    auto path = gtk_tree_path_new_from_indicesv(&row, 1);
    // emit #GtkTreeModel::row-inserted
    gtk_tree_model_row_inserted(GTK_TREE_MODEL(this), path, &iter_);
    gtk_tree_path_free(path);
  }

  void updated(const Iter &iter) {
    gint row = impl()->row(iter);
    auto path = gtk_tree_path_new_from_indicesv(&row, 1);
    GtkTreeIter iter_;
    new (&iter_) Iter{iter};
    // emit #GtkTreeModel::row-changed
    gtk_tree_model_row_changed(GTK_TREE_MODEL(this), path, &iter_);
    gtk_tree_path_free(path);
  }

  void del(const Iter &iter) {
    gint row = impl()->row(iter);
    auto path = gtk_tree_path_new_from_indicesv(&row, 1);
    // emit #GtkTreeModel::row-deleted - invalidates iterators
    gtk_tree_model_row_deleted(GTK_TREE_MODEL(this), path);
    gtk_tree_path_free(path);
    m_rows.splice(row, 1);
    for (unsigned i = row, n = m_rows.length(); i < n; i++)
      impl()->row(m_rows[i], i);
  }

private:
  ZtArray<Iter>	m_rows;
};

namespace TreeHierarchy {
  class Row {
  public:
    gint row() const { return m_row; }
    void row(gint v) { m_row = v; }

  private:
    gint	m_row = -1;
  };

  template <typename Impl, unsigned Depth_> class Child : public Row {
    auto impl() const { return static_cast<const Impl *>(this); }
    auto impl() { return static_cast<Impl *>(this); }

  public:
    enum { Depth = Depth_ };

    void parent(void *p) { m_parent = p; }

    template <typename TreeImpl>
    auto parent() const { return TreeImpl::template parent<Impl>(m_parent); }

    template <typename TreeImpl, typename L>
    auto next(L &&l) const {
      return parent<TreeImpl>()->child(this->row() + 1, ZuFwd<L>(l));
    }

    template <typename TreeImpl>
    void ascend(gint *indices) {
      indices[Depth - 1] = this->row();
      parent<TreeImpl>()->template ascend<TreeImpl>(indices);
    }

  private:
    void	*m_parent = nullptr;
  };
  template <typename Impl> class Child<Impl, 1> : public Row {
    auto impl() const { return static_cast<const Impl *>(this); }
    auto impl() { return static_cast<Impl *>(this); }

  public:
    enum { Depth = 1 };

    void parent(void *p) { m_parent = p; }

    template <typename TreeImpl>
    auto parent() const { return static_cast<Impl *>(nullptr); }

    template <typename TreeImpl, typename L>
    auto next(L &&l) const {
      auto parent = TreeImpl::template parent<Impl>(m_parent);
      return parent->child(this->row() + 1, ZuFwd<L>(l));
    }

    template <typename>
    void ascend(gint *indices) { indices[0] = this->row(); }

  private:
    void	*m_parent = nullptr;
  };
  template <typename Impl> class Child<Impl, 0> {
  public:
    enum { Depth = 0 };

    template <typename> void ascend(gint *) { } // unused
  };

  // individual leaf (CRTP)
  template <typename Impl, unsigned Depth>
  class Leaf : public Child<Impl, Depth> {
    Leaf(const Leaf &) = delete;
    Leaf &operator =(const Leaf &) = delete;
    Leaf(Leaf &&) = delete;
    Leaf &operator =(Leaf &&) = delete;

    auto impl() const { return static_cast<const Impl *>(this); }
    auto impl() { return static_cast<Impl *>(this); }

  public:
    Leaf() = default;
    ~Leaf() = default;
    static constexpr bool hasChild() { return false; }
    static constexpr unsigned nChildren() { return 0; }
    template <typename L>
    bool child(gint, L &&l) const { return false; }
    template <typename L>
    bool descend(const gint *, unsigned, L &&l) const {
      l(impl());
      return true;
    }
  };

  // parent of array of homogenous Children (CRTP)
  template <typename Impl, unsigned Depth, typename Child_>
  class Parent : public Child<Impl, Depth> {
    Parent(const Parent &) = delete;
    Parent &operator =(const Parent &) = delete;
    Parent(Parent &&) = delete;
    Parent &operator =(Parent &&) = delete;

    auto impl() const { return static_cast<const Impl *>(this); }
    auto impl() { return static_cast<Impl *>(this); }

  public:
    Parent() = default;
    ~Parent() { for (auto &&child: m_rows) delete child; }

    static constexpr bool hasChild() { return true; }
    constexpr unsigned nChildren() const { return m_rows.length(); }
    template <typename L>
    bool child(gint i, L &&l) const {
      if (ZuUnlikely(i < 0 || i >= m_rows.length())) return false;
      l(m_rows[i]);
      return true;
    }
    template <typename L, unsigned Depth_ = Depth>
    ZuIfT<(Depth_ > 0), bool>
    descend(const gint *indices, unsigned n, L &&l) const {
      if (!n) { l(impl()); return true; }
      return descend_(indices, n, ZuFwd<L>(l));
    }
    template <typename L, unsigned Depth_ = Depth>
    ZuIfT<Depth_ == 0, bool>
    descend(const gint *indices, unsigned n, L &&l) const {
      return descend_(indices, n, ZuFwd<L>(l));
    }
    template <typename L>
    bool descend_(const gint *indices, unsigned n, L &&l) const {
      auto i = indices[0];
      if (i < 0 || i >= m_rows.length()) return false;
      ++indices, --n;
      return m_rows[i]->descend(indices, n, ZuFwd<L>(l));
    }

    void add(Child_ *child) {
      unsigned n = m_rows.length();
      unsigned i = ZuSearchPos(ZuInterSearch<false>(n,
	[this, c1 = const_cast<const Child_ *>(child)](unsigned i) {
	  auto c2 = const_cast<const Child_ *>(m_rows[i]);
	  return c1->cmp(*c2);
	}));
      child->row(i);
      m_rows.splice(i, 0, child);
      for (++i, ++n; i < n; i++) m_rows[i]->row(i);
    }
    void del(Child_ *child) {
      unsigned n = m_rows.length();
      unsigned i = child->row();
      m_rows.splice(i, 1);
      delete child;
      for (--n; i < n; i++) m_rows[i]->row(i);
    }

  private:
    ZtArray<Child_ *>		m_rows;
  };

  // parent of tuple of heterogeneous Children (CRTP)
  template <typename Impl, unsigned Depth, typename Tuple>
  class Branch : public Child<Impl, Depth>, public Tuple {
    Branch(const Branch &) = delete;
    Branch &operator =(const Branch &) = delete;
    Branch(Branch &&) = delete;
    Branch &operator =(Branch &&) = delete;

    auto impl() const { return static_cast<const Impl *>(this); }
    auto impl() { return static_cast<Impl *>(this); }

  public:
    Branch() = default;
    ~Branch() = default;
    static constexpr bool hasChild() { return true; }
    constexpr unsigned nChildren() const { return m_rows.length(); }
    template <typename L>
    bool child(gint i, L &&l) const {
      if (ZuUnlikely(i < 0 || i >= m_rows.length())) return false;
      Tuple::cdispatch(m_rows[i],
	[&l](auto, auto &child) mutable { l(&child); });
      return true;
    }
    template <typename L>
    bool descend(const gint *indices, unsigned n, L &&l) const {
      if (!n) { l(impl()); return true; }
      auto i = indices[0];
      if (i < 0 || i >= m_rows.length()) return false;
      ++indices, --n;
      return Tuple::cdispatch(m_rows[i],
	  [indices, n, &l](auto, auto &child) mutable {
	    return child.descend(indices, n, l);
	  });
    }
    template <typename Child_> void add(Child_ *child) {
      enum { I = typename Tuple::template Index<Child_>{} };
      unsigned n = m_rows.length();
      unsigned i;
      for (i = 0; i < n; i++) if (m_rows[i] > I) break;
      if (i < n) memmove(&m_rows[i + 1], &m_rows[i], (n - i) * sizeof(int));
      m_rows.length(++n);
      m_rows[i] = I;
      child->row(i);
      for (++i; i < n; i++)
	Tuple::dispatch(m_rows[i], [i](auto, auto &child) { child.row(i); });
    }
    template <typename Child_> void del(Child_ *child) {
      unsigned n = m_rows.length();
      unsigned i = child->row();
      if (i < --n) memmove(&m_rows[i], &m_rows[i + 1], (n - i) * sizeof(int));
      m_rows.length(n);
      child->row(-1);
      for (; i < n; i++)
	Tuple::dispatch(m_rows[i], [i](auto, auto &child) { child.row(i); });
    }

  private:
    ZuArray<int, Tuple::N>	m_rows;
  };

  // CRTP - implementation must conform to the following interface:
#if 0
  // Iter must be ZuUnion<T0 *, T1 *, ...>
  struct Impl : public TreeHierarchy<Impl, Iter, Depth> {
    auto root();	// must return a Parent|Branch pointer
    template <typename T> static auto parent(void *ptr); // ascend
    template <typename T> auto value(const T *ptr, gint i, ZGtk::Value *value);
  };
#endif
  template <typename Impl, typename Iter, unsigned Depth>
  class Model : public TreeModel<Impl> {
    ZuAssert(sizeof(Iter) <= sizeof(GtkTreeIter));
    ZuAssert(ZuTraits<Iter>::IsPOD);

    auto impl() const { return static_cast<const Impl *>(this); }
    auto impl() { return static_cast<Impl *>(this); }

  public:
    GtkTreeModelFlags get_flags() {
      return static_cast<GtkTreeModelFlags>(GTK_TREE_MODEL_ITERS_PERSIST);
    }
    gboolean get_iter(GtkTreeIter *iter_, GtkTreePath *path) {
      gint depth = gtk_tree_path_get_depth(path);
      if (!depth) return false;
      gint *indices = gtk_tree_path_get_indices(path);
      return impl()->root()->descend(indices, depth, [iter_](auto ptr) {
	using T = ZuDecay<decltype(*ptr)>;
	if (!ptr) return false;
	*Iter::template new_<T *>(iter_) = const_cast<T *>(ptr);
	return true;
      });
    }
    GtkTreePath *get_path(GtkTreeIter *iter_) {
      auto iter = reinterpret_cast<Iter *>(iter_);
      gint depth;
      gint indices[Depth] = { 0 };
      iter->cdispatch([&depth, indices](auto, auto ptr) mutable {
	using T = ZuDecay<decltype(*ptr)>;
	depth = T::Depth;
	if (ZuLikely(ptr)) ptr->template ascend<Impl>(indices);
      });
      return gtk_tree_path_new_from_indicesv(indices, depth + 1);
    }
    void get_value(GtkTreeIter *iter_, gint i, ZGtk::Value *value) {
      auto iter = reinterpret_cast<Iter *>(iter_);
      iter->cdispatch([this, i, value](auto, const auto ptr) {
	if (ZuLikely(ptr)) impl()->value(ptr, i, value);
      });
    }
    gboolean iter_next(GtkTreeIter *iter_) {
      auto iter = reinterpret_cast<Iter *>(iter_);
      return iter->cdispatch([iter](auto, auto ptr) {
	if (!ptr) return false;
	return ptr->template next<Impl>([iter](auto ptr) {
	  using T = ZuDecay<decltype(*ptr)>;
	  if (!ptr) return false;
	  *iter = const_cast<T *>(ptr);
	  return true;
	});
      });
    }
    gboolean iter_children(GtkTreeIter *iter_, GtkTreeIter *parent_) {
      if (!parent_)
	return impl()->root()->child(0, [iter_](auto ptr) {
	  using T = ZuDecay<decltype(*ptr)>;
	  if (!ptr) return false;
	  *Iter::template new_<T *>(iter_) = const_cast<T *>(ptr);
	  return true;
	});
      auto parent = reinterpret_cast<Iter *>(parent_);
      return parent->cdispatch([iter_](auto, auto ptr) {
	if (!ptr) return false;
	return ptr->child(0, [iter_](auto ptr) {
	  using T = ZuDecay<decltype(*ptr)>;
	  if (!ptr) return false;
	  *Iter::template new_<T *>(iter_) = const_cast<T *>(ptr);
	  return true;
	});
      });
    }
    gboolean iter_has_child(GtkTreeIter *iter_) {
      if (!iter_) return true;
      auto iter = reinterpret_cast<Iter *>(iter_);
      return iter->cdispatch([](auto, auto ptr) {
	if (!ptr) return false;
	return ptr->hasChild();
      });
    }
    gint iter_n_children(GtkTreeIter *iter_) {
      if (!iter_) return impl()->root()->nChildren();
      auto iter = reinterpret_cast<Iter *>(iter_);
      return iter->cdispatch([](auto, auto ptr) -> gint {
	if (!ptr) return 0;
	return ptr->nChildren();
      });
    }
    gboolean iter_nth_child(GtkTreeIter *iter_, GtkTreeIter *parent_, gint i) {
      if (!parent_)
	return impl()->root()->child(i, [iter_](auto ptr) {
	  using T = ZuDecay<decltype(*ptr)>;
	  if (!ptr) return false;
	  *Iter::template new_<T *>(iter_) = const_cast<T *>(ptr);
	  return true;
	});
      auto parent = reinterpret_cast<Iter *>(parent_);
      return parent->cdispatch([iter_](auto I, auto ptr) {
	if (!ptr) return false;
	return ptr->child(I, [iter_](auto ptr) {
	  using T = ZuDecay<decltype(*ptr)>;
	  if (!ptr) return false;
	  *Iter::template new_<T *>(iter_) = const_cast<T *>(ptr);
	  return true;
	});
      });
    }
    gboolean iter_parent(GtkTreeIter *iter_, GtkTreeIter *child_) {
      if (!child_) return false;
      auto child = reinterpret_cast<Iter *>(child_);
      return child->cdispatch([iter_](auto, auto ptr) {
	auto parent = ptr->template parent<Impl>();
	using T = ZuDecay<decltype(*parent)>;
	if (!parent) return false;
	*Iter::template new_<T *>(iter_) = const_cast<T *>(parent);
	return true;
      });
    }
    void ref_node(GtkTreeIter *) { }
    void unref_node(GtkTreeIter *) { }

    template <typename Ptr, typename Parent>
    void add(Ptr *ptr, Parent *parent) {
      gint indices[Depth];
      parent->template ascend<Impl>(indices);
      ptr->parent(parent);
      parent->add(ptr); // sets ptr->row()
      indices[Ptr::Depth - 1] = ptr->row();
      auto path = gtk_tree_path_new_from_indicesv(indices, Ptr::Depth);
      GtkTreeIter iter_;
      new (&iter_) Iter{ptr};
      // emit #GtkTreeModel::row-inserted
      gtk_tree_model_row_inserted(GTK_TREE_MODEL(this), path, &iter_);
      gtk_tree_path_free(path);
    }

    template <typename Ptr>
    void updated(Ptr *ptr) {
      gint indices[Depth];
      ptr->template ascend<Impl>(indices);
      auto path = gtk_tree_path_new_from_indicesv(indices, Ptr::Depth + 1);
      GtkTreeIter iter_;
      new (&iter_) Iter{ptr};
      // emit #GtkTreeModel::row-changed
      gtk_tree_model_row_changed(GTK_TREE_MODEL(this), path, &iter_);
      gtk_tree_path_free(path);
    }

    template <typename Ptr>
    void del(Ptr *ptr) {
      gint indices[Depth];
      ptr->template ascend<Impl>(indices);
      auto path = gtk_tree_path_new_from_indicesv(indices, Ptr::Depth + 1);
      // emit #GtkTreeModel::row-deleted - invalidates iterators
      gtk_tree_model_row_deleted(GTK_TREE_MODEL(this), path);
      gtk_tree_path_free(path);
      ptr->template parent<Impl>()->del(ptr);
    }
  };
} // TreeHierarchy

} // ZGtk

#endif /* ZGtkTreeModel_HH */
