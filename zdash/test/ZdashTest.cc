//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <zlib/ZuBox.hh>
#include <zlib/ZuLib.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcDB.hh>
#include <zlib/ZtcMsg.hh>
#include <zlib/ZGtkValue.hh>
#include "../src/zdash_module.hh"

using namespace ZuTestUtil;

namespace Telemetry {
using TypeList = ZuTypeList<
  Ztc::HeapTelemetry, Ztc::HashTelemetry, Ztc::ThreadTelemetry,
  Ztc::MxTelemetry, Ztc::CxnTelemetry, Ztc::QueueTelemetry,
  Ztc::HubTelemetry, Ztc::LinkTelemetry, Ztc::DBTableTelemetry,
  Ztc::DBHostTelemetry, Ztc::DBTelemetry, Ztc::AppTelemetry,
  Ztc::AlertTelemetry, Ztc::PoolTelemetry>;

}

using Frame = ZmRef<ZiIOBuf>;
using FrameBuf = ZiIOBufAlloc<1024, 1U << 20, "ZDash.TestFrame">;
// Several GTK clock-check batches fit in the fixture's 64 KiB ring.
enum { Burst = 256 };

class Driver : public ZDash::Module {
public:
  int run(ZDash::ModuleSession &session) override {
    ZuTestMain();
    ZiLog::init("zdash-test");
    ZiLog::level(Ze::Warning);
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
    ZiLog::start();
    ZuGuard stop{[]() { ZiLog::stop(); }};
    m_local = !session.online;
    m_mapped = ::getenv("ZDASH_TEST_MAPPED");
    m_live = ::getenv("ZDASH_TEST_LIVE");
    m_liveDevice = ::getenv("ZDASH_TEST_DEVICE");
    m_livePublisher = ::getenv("ZDASH_TEST_PUBLISHER");

    ZtString<ZtStringSecret<true,
      ZtStringHeapID<"ZDash.TestToken">>> token{
      ::getenv("ZDASH_TEST_TOKEN")};
    session.token = token;
    session.hidden = !m_mapped;
    session.offline = m_local;
    session.timeout = 15;
    if (auto value = ::getenv("ZDASH_TEST_TIMEOUT")) {
      auto n = ZuBox<unsigned>{value};
      if (!n || n > 3600) return 1;
      session.timeout = n;
    }
    if (!m_local && !token) return 1;
    session.requested = [this](ZuBSpan frame) {
      auto msg = Ztc::msg(frame);
      auto request = msg ? msg->body_as_Request() : nullptr;
      if (!request) { m_failed = true; return; }
      if (!request->subscribe()) ++m_cancels;
      else if (request->group() == Ztc::fbs::Group::App) ++m_inventoryReqs;
      else ++m_detailReqs;
    };
    session.ready = [this](const ZDash::ModuleHost &host) {
      m_host = host;
      m_gtkThread = g_thread_self();
      g_signal_connect(host.treeView, "draw",
	G_CALLBACK(+[](GtkWidget *, cairo_t *, gpointer p) -> gboolean {
	  auto driver = static_cast<Driver *>(p);
	  if (g_thread_self() != driver->m_gtkThread) driver->m_failed = true;
	  if (driver->m_host.pending_()) ++driver->m_pendingEvents;
	  if (driver->m_checked >= 2 && !driver->m_painted) {
	    driver->m_painted = true;
	    driver->wake_();
	  }
	  return false;
	}), this);
      for (auto name: {"row-inserted", "row-changed", "row-has-child-toggled"})
	g_signal_connect(host.model, name,
	  G_CALLBACK(+[](GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter,
	      gpointer p) {
	    auto driver = static_cast<Driver *>(p);
	    if (g_thread_self() != driver->m_gtkThread) driver->m_failed = true;
	    ++driver->m_notifications;
	    if (driver->m_local && !driver->m_closed) driver->render_(model, iter);
	  }), this);
      g_signal_connect(host.model, "row-deleted",
	G_CALLBACK(+[](GtkTreeModel *, GtkTreePath *, gpointer p) {
	  auto driver = static_cast<Driver *>(p);
	  if (g_thread_self() != driver->m_gtkThread) driver->m_failed = true;
	  ++driver->m_notifications;
	}), this);
      if (m_local) m_host.rxRun([this]() { feed_(1); });
    };
    session.consumed = [this](bool wrapped) {
      m_wrapped |= wrapped;
      if (m_shutdownPending && !m_complete && m_host.pending_() > 1) {
	m_complete = true;
	gtk_window_close(m_host.window);
      }
    };
    session.drained = [this]() { wake_(); };
    session.refreshed = [this](unsigned pending) {
      if (pending) { ++m_partialDrains; wake_(); }
    };
    session.closing = [this]() {
      m_closing = true;
      if (m_savedHeap) {
        gtk_tree_row_reference_free(m_savedHeap);
        m_savedHeap = nullptr;
      }
      if (m_event) { g_source_remove(m_event); m_event = 0; }
    };
    session.closed = [this]() { m_closed = true; };
    bool ok = session.session(session);
    ok = ok && !m_failed && m_complete && m_closed &&
      (!m_local || (m_partialDrains && m_pendingEvents &&
	(!m_mapped || m_painted)));
    if (m_local)
      log("partial GTK drains=", m_partialDrains,
	" GTK events with queued telemetry=", m_pendingEvents,
	" populated GTK draw=", m_painted);
    ZuCHECK(ok, "hidden GTK session, telemetry ownership and close event");
    return ok ? 0 : 1;
  }

private:
  bool checkLive_() {
    auto src = m_host.source_(m_liveDevice, m_livePublisher);
    if (m_livePhase == 4) {
      if (src.identity || src.count ||
          gtk_tree_model_iter_n_children(
            gtk_tree_view_get_model(m_host.details), nullptr)) return false;
      std::cout << "# dashboard publisher shutdown\n" << std::flush;
      return true;
    }
    if (!src.identity) return false;
    struct Rows {
      int root;
      GtkTreeIter base;
      bool baseFound = false;
      bool lateFound = false;
      int rag = Ztc::RAG::Off;
      unsigned groups = 0;
      unsigned otherGroups = 0;
      bool cxn = false;
      bool link = false;
      unsigned dbLeaves = 0;
    } rows{src.row};
    gtk_tree_model_foreach(m_host.model, [](
	GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
	gpointer p) -> gboolean {
      auto rows = static_cast<Rows *>(p);
      auto depth = gtk_tree_path_get_depth(path);
      auto indices = gtk_tree_path_get_indices(path);
      ZGtk::Value text;
      gtk_tree_model_get_value(model, iter, 1, &text);
      ZuCSpan key{text.get_string()};
      if (depth == 2) {
	static const char *names[] = {"heaps", "hashTbls", "threads",
	  "multiplexers", "queues", "pools", "engines", "databases"};
	auto &groups = indices[0] == rows->root ? rows->groups : rows->otherGroups;
	for (unsigned i = 0; i < 8; ++i)
	  if (key == names[i]) groups |= 1U << i;
	return false;
      }
      if (indices[0] != rows->root) return false;
      if (depth == 4) {
	if (indices[1] == 0) {
	  GtkTreeIter parent;
	  gtk_tree_model_iter_parent(model, &parent, iter);
	  ZGtk::Value name;
	  gtk_tree_model_get_value(model, &parent, 1, &name);
	  ZuCSpan id{name.get_string()};
	  if (id == "ZDash.LiveHeap") rows->lateFound = true;
	  else if (id == "ZDash.BaseHeap") {
	    rows->baseFound = true;
	    rows->base = *iter;
	    ZGtk::Value rag;
	    gtk_tree_model_get_value(model, iter, 0, &rag);
	    rows->rag = rag.get_int();
	  }
	  return false;
	}
	if (indices[1] == 3 && key == "fixture-mx") rows->cxn = true;
	if (indices[1] == 6 && key == "fixture-hub") rows->link = true;
	return false;
      }
      if (depth == 5 && indices[1] == 7 &&
	  (key == "fixture-db" || key == "fixture-db2")) ++rows->dbLeaves;
      return false;
    }, &rows);
    if (!rows.baseFound || rows.groups != 255 || !rows.cxn || !rows.link ||
	rows.dbLeaves != 4) return false;
    switch (m_livePhase) {
      case 0: {
	if (rows.rag != Ztc::RAG::Green || rows.lateFound) return false;
	if (!checkRequests_(7, 0)) return false;
	m_liveGeneration = src.generation;
	m_row = src.identity;
	m_heap = rows.base;
	auto path = gtk_tree_model_get_path(m_host.model, &m_heap);
	gtk_tree_view_expand_to_path(m_host.treeView, path);
	gtk_tree_view_set_cursor(m_host.treeView, path, nullptr, false);
	gtk_tree_path_free(path);
	std::cout << "# dashboard initial\n" << std::flush;
      } break;
      case 1: {
	if (!rows.lateFound || rows.rag != Ztc::RAG::Red) return false;
	if (!checkRequests_(7, 0)) return false;
	auto oldPath = gtk_tree_model_get_path(m_host.model, &m_heap);
	auto path = gtk_tree_model_get_path(m_host.model, &rows.base);
	bool same = !gtk_tree_path_compare(oldPath, path);
	gtk_tree_path_free(oldPath);
	gtk_tree_path_free(path);
	if (src.generation != m_liveGeneration || !same) {
	  m_failed = true;
	  m_host.stop();
	  return false;
	}
	std::cout << "# dashboard live update\n" << std::flush;
      } break;
      case 2:
	if (src.count < 2 || rows.otherGroups != 255) return false;
	if (m_detailReqs < 14) return false;
	if (!checkRequests_(14, 0)) return false;
	gtk_tree_view_expand_all(m_host.treeView);
	std::cout << "# dashboard late publisher\n" << std::flush;
	break;
      case 3:
	if (src.generation <= m_liveGeneration || !rows.lateFound ||
	    rows.rag != Ztc::RAG::Red) return false;
	if (src.count < 2 || rows.otherGroups != 255 || m_detailReqs < 28) return false;
	if (!checkRequests_(28, 0)) return false;
	auto path = gtk_tree_model_get_path(m_host.model, &m_heap);
	auto parent = gtk_tree_path_copy(path);
	gtk_tree_path_up(parent);
	bool stable = src.identity == m_row &&
	  gtk_tree_selection_path_is_selected(
	    gtk_tree_view_get_selection(m_host.treeView), path) &&
	  gtk_tree_view_row_expanded(m_host.treeView, parent) &&
	  checkDetail_("id", "ZDash.BaseHeap", false) &&
	  checkDetail_("rag", "Red", false);
	gtk_tree_path_free(parent);
	gtk_tree_path_free(path);
	if (!stable) { m_failed = true; m_host.stop(); return false; }
	std::cout << "# dashboard transport reconnection\n" << std::flush;
	break;
    }
    ++m_livePhase;
    return false;
  }

  bool checkRequests_(unsigned details, unsigned cancels) {
    if (m_inventoryReqs == 1 && m_detailReqs == details &&
	m_cancels == cancels) return true;
    log("unexpected dashboard requests: inventory=", m_inventoryReqs.load_(),
	" detail=", m_detailReqs.load_(), " cancel=", m_cancels.load_());
    m_failed = true;
    m_host.stop();
    return false;
  }

  static uint64_t formatAllocs_() {
    uint64_t n = 0;
    Ztc::HeapMgr::all([&n](Ztc::Heap *heap) {
      if (heap->telKey().p<0>() != "ZDash.Value") return;
      Ztc::HeapTelemetry data;
      heap->telemetry(data);
      n += data.cacheAllocs + data.heapAllocs;
    });
    return n;
  }

  void render_(GtkTreeModel *model, GtkTreeIter *iter) {
    for (int i = 0; i < gtk_tree_model_get_n_columns(model) - 1; ++i) {
      auto col = gtk_tree_view_get_column(m_host.treeView, i);
      gtk_tree_view_column_cell_set_cell_data(col, model, iter,
	  gtk_tree_model_iter_has_child(model, iter), false);
    }
  }

  bool checkFormatting_() {
    auto read = [this]() {
      gtk_tree_model_foreach(m_host.model, [](
	  GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter,
	  gpointer p) -> gboolean {
	auto driver = static_cast<Driver *>(p);
	driver->render_(model, iter);
	return false;
      }, this);
    };
    read(); // Warm up the largest key and its named formatting arena.
    auto before = formatAllocs_();
    for (unsigned i = 0; i < 4; ++i) read();
    bool ok = before && formatAllocs_() == before;
    if (!ok) log("model formatting did not reuse its named heap buffer");
    return ok;
  }

  template <typename T>
  static T sample() {
    T data;
    if constexpr (ZuIsSame<T, Ztc::HeapTelemetry>{}) {
      data.id = "heap";
      data.partition = 7;
      data.vshift = 4;
      data.size = 128;
      data.alignment = 64;
      data.sharded = true;
    } else if constexpr (ZuIsSame<T, Ztc::HashTelemetry>{}) {
      data.id = "hash";
      data.addr = 0x1234;
    } else if constexpr (ZuIsSame<T, Ztc::ThreadTelemetry>{}) {
      data.tid = 123;
    } else if constexpr (ZuIsSame<T, Ztc::MxTelemetry>{}) {
      data.id = "mx";
    } else if constexpr (ZuIsSame<T, Ztc::CxnTelemetry>{}) {
      data.mxID = "mx";
      data.remoteIP = "192.0.2.1";
      data.remotePort = 443;
      data.localIP = "192.0.2.2";
      data.localPort = 54321;
    } else if constexpr (ZuIsSame<T, Ztc::QueueTelemetry>{}) {
      data.ownerID = "owner";
      data.id = "queue";
      data.type = Ztc::QueueType::Tx;
    } else if constexpr (ZuIsSame<T, Ztc::HubTelemetry>{}) {
      data.id = "hub";
      data.linkType = Ztc::LinkType::QUIC;
    } else if constexpr (ZuIsSame<T, Ztc::LinkTelemetry>{} ||
	ZuIsSame<T, Ztc::PoolTelemetry>{}) {
      data.hubID = "hub";
      data.id = "child";
      data.type = Ztc::LinkType::QUIC;
    } else if constexpr (ZuIsSame<T, Ztc::DBTableTelemetry>{} ||
	ZuIsSame<T, Ztc::DBHostTelemetry>{}) {
      data.dbID = "db";
      data.id = "child";
    } else if constexpr (ZuIsSame<T, Ztc::DBTelemetry>{}) {
      data.self = "db";
      data.state = Ztc::DBHostState::Active;
    }
    return data;
  }

  bool checkDetail_(ZuCSpan field, ZuCSpan expected, bool numeric) {
    auto model = gtk_tree_view_get_model(m_host.details);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter_first(model, &iter)) return false;
    do {
      ZGtk::Value key;
      gtk_tree_model_get_value(model, &iter, 0, &key);
      if (ZuCSpan{key.get_string()} != field) continue;
      ZGtk::Value value, align;
      gtk_tree_model_get_value(model, &iter, 1, &value);
      bool ok = ZuCSpan{value.get_string()} == expected;
      if (!ok) log("detail ", field, " expected ", expected, " got ", value.get_string());
      gtk_tree_model_get_value(model, &iter, 2, &align);
      ok &= align.get_float() == (numeric ? 1 : 0);
      auto col = gtk_tree_view_get_column(m_host.details, 1);
      gtk_tree_view_column_cell_set_cell_data(col, model, &iter, false, false);
      auto cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(col));
      gboolean bg = false;
      gfloat xalign = -1;
      gchar *text = nullptr;
      g_object_get(cells->data, "background-set", &bg, "xalign", &xalign,
	"text", &text, nullptr);
      ok &= bool(bg) == !(iter.stamp & 1) && xalign == (numeric ? 1 : 0) &&
	ZuCSpan{text} == expected;
      if (!ok) log("detail ", field, " renderer text=", text, " align=", xalign,
	" stripe=", bg, " row=", iter.stamp - 1);
      g_free(text);
      g_list_free(cells);
      if (!ok) log("detail field ", field, " value, alignment or stripe differs");
      return ok;
    } while (gtk_tree_model_iter_next(model, &iter));
    log("detail field ", field, " not found");
    return false;
  }

  bool checkRAG_(GtkTreeIter *iter, bool colored) {
    auto col = gtk_tree_view_get_column(m_host.treeView, 0);
    gtk_tree_view_column_cell_set_cell_data(col, m_host.model, iter, true, false);
    auto cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(col));
    bool ok = g_list_length(cells) == 2;
    GdkPixbuf *icon = nullptr;
    if (cells) {
      g_object_get(cells->data, "pixbuf", &icon, nullptr);
      ok &= bool(icon) == colored;
      if (icon) g_object_unref(icon);
    }
    if (cells && cells->next) {
      gboolean fg = true, bg = true;
      g_object_get(cells->next->data, "foreground-set", &fg, "background-set", &bg, nullptr);
      ok &= !bg && !fg;
    }
    g_list_free(cells);
    if (!ok) log("RAG indicator or theme colors differ");
    return ok;
  }

  bool checkKeys_() {
    auto tree = m_host.treeView;
    auto path = gtk_tree_path_new_from_string("0:0:0:0");
    gtk_tree_view_expand_to_path(tree, path);
    auto heapPath = gtk_tree_path_new_from_string("0:0");
    bool expanded = gtk_tree_view_row_expanded(tree, heapPath);
    gtk_tree_path_free(heapPath);
    gtk_tree_view_set_cursor(tree, path, nullptr, false);
    gtk_tree_path_free(path);
    bool headers = expanded;
    for (unsigned i = 0; i < 5; ++i) {
      auto col = gtk_tree_view_get_column(tree, i);
      headers &= gtk_tree_view_column_get_visible(col) &&
	ZuCSpan{gtk_tree_view_column_get_title(col)} == (i ? "" : "ID");
    }
    if (!headers) log("heap column headers do not match field metadata");
    GtkTreeIter group;
    if (gtk_tree_model_get_iter_from_string(m_host.model, &group, "0:0")) {
      headers &= checkRAG_(&group, false);
    } else headers = false;
    // Literal fixture expectations cover all displayed identities and labels.
    // A GTK path followed by the five identity columns.
    static const char *rows[][6] = {
      {"0", "publisher", "test-a", "2", "", ""},
      {"0:0", "heaps", "size", "alignment", "sharded", ""},
      {"0:0:0", "heap", "", "", "", ""},
      {"0:0:0:0", "7", "128", "64", "1", ""},
      {"0:0:0:1", "8", "128", "64", "1", ""},
      {"0:1", "hashTbls", "addr", "", "", ""},
      {"0:1:0", "hash", "1234", "", "", ""},
      {"0:2", "threads", "", "", "", ""},
      {"0:2:0", "123", "", "", "", ""},
      {"0:3", "multiplexers", "", "", "", ""},
      {"0:3:0", "mx", "", "", "", ""},
      {"0:3:0:0", "mx", "192.0.2.1", "443", "192.0.2.2", "54,321"},
      {"0:4", "queues", "id", "type", "", ""},
      {"0:4:0", "owner", "queue", "Tx", "", ""},
      {"0:5", "pools", "id", "", "", ""},
      {"0:5:0", "hub", "child", "", "", ""},
      {"0:6", "engines", "id", "", "", ""},
      {"0:6:0", "QUIC", "hub", "", "", ""},
      {"0:6:0:0", "hub", "child", "", "", ""},
      {"0:7", "databases", "", "", "", ""},
      {"0:7:0", "db", "", "", "", ""},
      {"0:7:0:0", "hosts", "id", "", "", ""},
      {"0:7:0:0:0", "db", "child", "", "", ""},
      {"0:7:0:1", "tables", "id", "", "", ""},
      {"0:7:0:1:0", "db", "child", "", "", ""},
      {"0:7:1", "db2", "", "", "", ""},
      {"0:7:1:0", "hosts", "id", "", "", ""},
      {"0:7:1:0:0", "db2", "child", "", "", ""},
      {"0:7:1:1", "tables", "id", "", "", ""},
      {"0:7:1:1:0", "db2", "child", "", "", ""}
    };
    bool ok = headers && gtk_tree_model_get_n_columns(m_host.model) == 6 &&
      gtk_tree_view_get_n_columns(m_host.details) == 2 &&
      checkDetail_("id", "heap", false) &&
      checkDetail_("size", "128", true) &&
      checkDetail_("partition", "7", true) &&
      checkDetail_("allocated", "0", true) &&
      checkDetail_("rag", "Off", false);
    for (const auto &row: rows) {
      GtkTreeIter iter;
      bool found = gtk_tree_model_get_iter_from_string(
	m_host.model, &iter, row[0]);
      if (!found) log(row[0], " not found");
      ok &= found;
      if (!found) continue;
      if (ZuCSpan{row[0]} == "0:7:0" ||
	  ZuCSpan{row[0]} == "0:7:1") {
	ZGtk::Value rag;
	gtk_tree_model_get_value(m_host.model, &iter, 0, &rag);
	bool matches = rag.get_int() == (ZuCSpan{row[0]} == "0:7:0" ?
	  Ztc::RAG::Green : Ztc::RAG::Amber);
	if (!matches) log("database RAG differs from telemetry state");
	ok &= matches;
      }
      auto path = gtk_tree_model_get_path(m_host.model, &iter);
      auto text = gtk_tree_path_to_string(path);
      ok &= ZuCSpan{text} == row[0];
      g_free(text);
      gtk_tree_path_free(path);
      for (unsigned col = 1; col < 6; ++col) {
	ZGtk::Value value;
	gtk_tree_model_get_value(m_host.model, &iter, col, &value);
	bool matches = ZuCSpan{value.get_string()} == row[col];
	if (!matches) log(row[0], " column ", col, " expected ",
	  row[col], " got ", value.get_string());
	ok &= matches;
      }
    }
    if (m_mapped) {
      // Exercise GTK's real key bindings across leaves and every parent depth.
      gtk_widget_grab_focus(GTK_WIDGET(tree));
      ok &= gtk_widget_has_focus(GTK_WIDGET(tree));
      auto press = [tree](guint key) {
	GdkEventKey event{};
	event.type = GDK_KEY_PRESS;
	event.keyval = key;
	gboolean handled = false;
	g_signal_emit_by_name(tree, "key-press-event", &event, &handled);
	return handled;
      };
      for (const auto &row: rows) {
	auto path = gtk_tree_path_new_from_string(row[0]);
	GtkTreeIter iter;
	bool parent = gtk_tree_model_get_iter(m_host.model, &iter, path) &&
	  gtk_tree_model_iter_has_child(m_host.model, &iter);
	if (parent) {
	  gtk_tree_view_expand_to_path(tree, path);
	  gtk_tree_view_set_cursor(tree, path,
	    gtk_tree_view_get_column(tree, 2), false);
	  for (auto key: {GDK_KEY_Left, GDK_KEY_Right}) {
	    auto handled = press(key);
	    GtkTreePath *cursor = nullptr;
	    gtk_tree_view_get_cursor(tree, &cursor, nullptr);
	    ok &= handled && gtk_tree_view_row_expanded(tree, path) ==
	      (key == GDK_KEY_Right) && cursor &&
	      !gtk_tree_path_compare(path, cursor);
	    if (cursor) gtk_tree_path_free(cursor);
	  }
	} else {
	  auto parentPath = gtk_tree_path_copy(path);
	  if (gtk_tree_path_up(parentPath)) {
	    gtk_tree_view_expand_to_path(tree, path);
	    gtk_tree_view_set_cursor(tree, path,
	      gtk_tree_view_get_column(tree, 2), false);
	    ok &= press(GDK_KEY_Left);
	    GtkTreePath *cursor = nullptr;
	    gtk_tree_view_get_cursor(tree, &cursor, nullptr);
	    ok &= gtk_tree_view_row_expanded(tree, parentPath) && cursor &&
	      !gtk_tree_path_compare(parentPath, cursor);
	    if (cursor) gtk_tree_path_free(cursor);
	    ok &= press(GDK_KEY_Left) &&
	      !gtk_tree_view_row_expanded(tree, parentPath);
	  }
	  gtk_tree_path_free(parentPath);
	}
	gtk_tree_path_free(path);
      }
      for (const auto &row: rows) {
	auto path = gtk_tree_path_new_from_string(row[0]);
	gtk_tree_view_expand_to_path(tree, path);
	gtk_tree_view_set_cursor(tree, path, nullptr, false);
	gtk_tree_path_free(path);
	for (auto key: {GDK_KEY_Right, GDK_KEY_Down, GDK_KEY_Left,
	    GDK_KEY_Up, GDK_KEY_End, GDK_KEY_Home, GDK_KEY_Page_Down,
	    GDK_KEY_Page_Up, GDK_KEY_space}) {
	  ok &= gtk_bindings_activate(G_OBJECT(tree), key, GdkModifierType(0));
	  GtkTreePath *cursor = nullptr;
	  gtk_tree_view_get_cursor(tree, &cursor, nullptr);
	  GtkTreeIter iter;
	  ok &= cursor && gtk_tree_model_get_iter(m_host.model, &iter, cursor);
	  if (cursor && key == GDK_KEY_Home)
	    ok &= gtk_tree_path_get_depth(cursor) == 1 &&
	      gtk_tree_path_get_indices(cursor)[0] == 0;
	  if (cursor) gtk_tree_path_free(cursor);
	}
      }
      auto path = gtk_tree_path_new_from_string("0:0:0:0");
      gtk_tree_view_expand_to_path(tree, path);
      gtk_tree_view_set_cursor(tree, path, nullptr, false);
      gtk_tree_path_free(path);
      ok &= checkDetail_("id", "heap", false);
    }
    GtkTreeIter dbs;
    if (gtk_tree_model_get_iter_from_string(m_host.model, &dbs, "0:7")) {
      ok &= gtk_tree_model_iter_n_children(m_host.model, &dbs) == 2;
      for (unsigned i = 0; i < 2; ++i) {
	GtkTreeIter db;
	if (!gtk_tree_model_iter_nth_child(m_host.model, &db, &dbs, i)) {
	  ok = false;
	  continue;
	}
	ok &= gtk_tree_model_iter_n_children(m_host.model, &db) == 2;
	for (unsigned j = 0; j < 2; ++j) {
	  GtkTreeIter group;
	  bool found = gtk_tree_model_iter_nth_child(
	    m_host.model, &group, &db, j);
	  ok &= found;
	  if (found)
	    ok &= gtk_tree_model_iter_n_children(m_host.model, &group) == 1;
	}
      }
    } else ok = false;
    return ok;
  }

  void feed_(unsigned phase) {
    auto deliver = [this](Frame frame) {
      if (!m_host.receive_(ZuBSpan{frame->data(), frame->length}, true))
	m_failed = true;
      // The ring owns bytes, not the input buffer or its reference.
      ZuClear(frame->data(), frame->length);
    };
    auto telemetry = [&deliver, phase](ZuCSpan device, uint64_t generation,
	int rag, bool shutdown = false) {
      Zfb::IOBuilder builder{Frame{new FrameBuf}};
      auto dev = Zfb::Save::str(builder, device);
      auto id = Zfb::Save::str(builder, "publisher");
      // Shutdown is an empty protocol marker with no ZfbStruct payload type.
      auto value = shutdown ? Ztc::fbs::CreateShutdown(builder).Union() :
	ZfbStruct::save(builder, Ztc::AppTelemetry{
	  .version = "test", .startTime = ZuDateTime{2026, 10, 1, 12, 34,
            phase == 12 ? 57 : 56, 123456789},
	  .ztcver = ZuSemVer{1234567},
	  .rag = Ztc::RAG::T(rag)}).Union();
      auto tel = Ztc::saveTelemetry(builder, id, 0,
	shutdown ? Ztc::fbs::TelemetryBody::Shutdown :
	  Ztc::fbs::TelemetryBody::AppTelemetry, value);
      builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::Telemetry,
	tel.Union(), 1, dev, generation));
      deliver(builder.buf());
    };
    switch (phase) {
      case 1:
	{
	  auto frame = m_host.request_();
	  auto msg = Ztc::msg(ZuBSpan{frame->data(), frame->length});
	  auto req = msg ? msg->body_as_Request() : nullptr;
	  if (!req || msg->deviceId() || req->id() ||
	      req->group() != Ztc::fbs::Group::App || !req->subscribe() ||
	      !req->interval()) {
	    m_failed = true;
	    m_host.stop();
	    return;
	  }
	}
	telemetry("test-a", 1, Ztc::RAG::Green);
	telemetry("test-b", 1, Ztc::RAG::Green);
	break;
      case 2: {
	Zfb::IOBuilder builder{Frame{new FrameBuf}};
	auto eos = ZfbStruct::save(builder, Ztc::EOS{ZuID{"inventory"}, 0});
	builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::EOS,
	  eos.Union(), 1));
	deliver(builder.buf());
      } break;
      case 3:
	for (unsigned i = 0; i < Burst; ++i)
	  telemetry("test-a", 1, Ztc::RAG::Red);
	break;
      case 4:
	telemetry("test-b", 1, Ztc::RAG::Off, true);
	telemetry("test-a", 2, Ztc::RAG::Green);
	telemetry("test-a", 1, Ztc::RAG::Red); // obsolete generation
	break;

      case 5: case 6: case 7: {
	// Exercise every concrete row allocator, placeholder parent and payload.
	auto send = [this]<typename T>(const T &data) {
	  constexpr auto group = []() {
	    using namespace Ztc::fbs;
	    if constexpr (ZuIsSame<T, Ztc::HeapTelemetry>{}) return Group::Heap;
	    else if constexpr (ZuIsSame<T, Ztc::HashTelemetry>{}) return Group::Hash;
	    else if constexpr (ZuIsSame<T, Ztc::ThreadTelemetry>{}) return Group::Thread;
	    else if constexpr (ZuIsSame<T, Ztc::MxTelemetry>{} ||
	      ZuIsSame<T, Ztc::CxnTelemetry>{}) return Group::Mx;
	    else if constexpr (ZuIsSame<T, Ztc::QueueTelemetry>{}) return Group::Queue;
	    else if constexpr (ZuIsSame<T, Ztc::DBTelemetry>{} ||
	      ZuIsSame<T, Ztc::DBHostTelemetry>{} ||
	      ZuIsSame<T, Ztc::DBTableTelemetry>{}) return Group::DB;
	    else return Group::Hub;
	  }();
	  auto subID = m_host.subscription_("test-a", "publisher", unsigned(group));
	  if (!subID) { m_failed = true; return; }
	  Zfb::IOBuilder builder{Frame{new FrameBuf}};
	  auto device = Zfb::Save::str(builder, "test-a");
	  auto id = Zfb::Save::str(builder, "publisher");
	  auto value = ZfbStruct::save(builder, data);
	  auto tel = Ztc::saveTelemetry(builder, id, 0,
	    Ztc::fbs::TelemetryBodyTraits<ZfbType<T>>::enum_value,
	    value.Union());
	  builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::Telemetry,
	    tel.Union(), subID, device, 2));
	  auto frame = builder.buf();
	  // Exercise production filtering through independently admitted groups.
	  if (!Ztc::msg(ZuBSpan{frame->data(), frame->length}) ||
	      !m_host.receive_(ZuBSpan{frame->data(), frame->length}, true))
	    m_failed = true;
	  ZuClear(frame->data(), frame->length);
	};
	if (phase > 5) {
	  auto heap = sample<Ztc::HeapTelemetry>();
	  heap.id = phase == 6 ? "aaa" :
	    "zzzz-long-heap-key-to-exercise-formatting-buffer-growth";
	  send(heap);
	  send(sample<Ztc::HeapTelemetry>());
	  auto db = sample<Ztc::DBTelemetry>();
	  db.state = phase == 6 ? Ztc::DBHostState::Stopping :
	    Ztc::DBHostState::Active;
	  send(db);
	  if (phase == 7) {
	    auto sub = m_host.subscription_("test-a", "publisher",
		unsigned(Ztc::fbs::Group::Hash));
	    Zfb::IOBuilder ack{Frame{new FrameBuf}};
	    auto device = Zfb::Save::str(ack, "test-a");
	    auto body = ZfbStruct::save(ack, Ztc::Ack{
		ZuID{"publisher"}, 0, 1000, uint8_t(Ztc::fbs::AckStatus::Failed)});
	    ack.Finish(Ztc::saveMsg(ack, Ztc::fbs::Body::Ack,
		body.Union(), sub, device, 2));
	    deliver(ack.buf());
	    if (!sub || m_host.subscription_("test-a", "publisher",
		unsigned(Ztc::fbs::Group::Hash))) m_failed = true;
	    // A late cancelled reply cannot add a row or resurrect its route.
	    Zfb::IOBuilder late{Frame{new FrameBuf}};
	    device = Zfb::Save::str(late, "test-a");
	    auto id = Zfb::Save::str(late, "publisher");
	    auto hash = sample<Ztc::HashTelemetry>();
	    hash.addr = 0x5678;
	    auto value = ZfbStruct::save(late, hash);
	    auto tel = Ztc::saveTelemetry(late, id, 0,
		Ztc::fbs::TelemetryBody::HashTelemetry, value.Union());
	    late.Finish(Ztc::saveMsg(late, Ztc::fbs::Body::Telemetry,
		tel.Union(), sub, device, 2));
	    deliver(late.buf());
	    // Unrelated detail delivery and inventory survive the rejection.
	    send(sample<Ztc::HeapTelemetry>());
	  }
	  break;
	}
	// Children create placeholder parents before the real parent records.
	send(sample<Ztc::CxnTelemetry>());
	send(sample<Ztc::LinkTelemetry>());
	ZuUnroll::all<Telemetry::TypeList>([&send]<typename T>() {
	  if constexpr (!ZuIsSame<T, Ztc::AppTelemetry>{} &&
	    !ZuIsSame<T, Ztc::AlertTelemetry>{}) send(sample<T>());
	});
	auto arena = sample<Ztc::HeapTelemetry>();
	arena.partition = 8;
	send(arena);
	// The first DB was created by its children; the second arrives first.
	// Matching child IDs must remain scoped to their own DB.
	auto db = sample<Ztc::DBTelemetry>();
	db.self = "db2";
	send(db);
	auto host = sample<Ztc::DBHostTelemetry>();
	host.dbID = "db2";
	send(host);
	auto table = sample<Ztc::DBTableTelemetry>();
	table.dbID = "db2";
	send(table);
	// Update one DB, then the other: each retains its row and children.
	db.state = Ztc::DBHostState::Stopping;
	send(db);
	send(sample<Ztc::DBTelemetry>());
	// Repeated child updates must also find the original rows.
	send(host);
	send(table);
      } break;
      case 10: { // Source-attributed EOS is still only a snapshot boundary.
	Zfb::IOBuilder builder{Frame{new FrameBuf}};
	auto device = Zfb::Save::str(builder, "test-a");
	auto eos = ZfbStruct::save(builder, Ztc::EOS{ZuID{"publisher"}, 123});
	builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::EOS,
	  eos.Union(), 1, device, 2));
	deliver(builder.buf());
      } break;
      case 13: { // Explicit transport loss preserves objects and selection.
	Zfb::IOBuilder builder{Frame{new FrameBuf}};
	auto device = Zfb::Save::str(builder, "test-a");
	auto error = ZfbStruct::save(builder, Ztc::Error{
	  Ztc::ErrorMessage{"agent disconnected"}, ZuID{"publisher"},
	  0, int(Ztc::HubError::AgentGone)});
	builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::Error,
	  error.Union(), 1, device, 2));
	deliver(builder.buf());
      } break;
      case 11: {
	telemetry("test-a", 3, Ztc::RAG::Green);
	telemetry("test-a", 2, Ztc::RAG::Red); // obsolete transport generation
	Zfb::IOBuilder builder{Frame{new FrameBuf}};
	auto device = Zfb::Save::str(builder, "test-a");
	auto id = Zfb::Save::str(builder, "publisher");
	auto heap = sample<Ztc::HeapTelemetry>();
	heap.heapAllocs = 1379;
	auto value = ZfbStruct::save(builder, heap);
	auto tel = Ztc::saveTelemetry(builder, id, 0,
	  Ztc::fbs::TelemetryBody::HeapTelemetry, value.Union());
	auto sub = m_host.subscription_("test-a", "publisher",
	  unsigned(Ztc::fbs::Group::Heap));
	builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::Telemetry,
	  tel.Union(), sub, device, 3));
	deliver(builder.buf());
      } break;
      case 12:
	telemetry("test-a", 3, Ztc::RAG::Green); // actual process replacement
	break;
      case 8:
	telemetry("test-a", 3, Ztc::RAG::Off, true);
	break;
      case 9:
	for (unsigned i = 0; i < Burst; ++i)
	  telemetry("test-a", 3, Ztc::RAG::Green);
	break;
    }
    // Publish completion on GTK after all frames in this phase are queued.
    m_host.gtkRun([this, phase]() {
      if (m_closing) return;
      m_phase = phase;
      wake_();
    });
  }

  void wake_() {
    if (m_closing || m_complete || m_event) return;
    m_event = g_idle_add([](gpointer p) -> gboolean {
      auto app = static_cast<Driver *>(p);
      app->m_event = 0;
      if (app->m_host.pending_()) ++app->m_pendingEvents;
      app->check_();
      return G_SOURCE_REMOVE;
    }, this);
  }

  // A GLib event (not a scheduler job) proves the front end keeps dispatching
  // after each ring drain, including an empty drain and snapshot completion.
  void check_() {
    if (m_closing) return;
    if (m_failed) { m_host.stop(); return; }
    if (m_host.pending_()) return;
    if (m_local) {
      unsigned phase = m_phase;
      if (!phase || phase == m_checked) return;
      if (m_mapped && phase == 7 && !m_painted) return;
      auto src = m_host.source_("test-a", "publisher");
      bool ok = phase == 8 ? !src.identity && !src.count :
	src.identity && src.generation ==
          (phase == 11 || phase == 12 ? 3 : phase >= 4 ? 2 : 1) &&
	src.count == (phase >= 4 ? 1 : 2);
      if (ok && phase != 8) {
	if (phase == 1) {
	  gint width, height;
	  gtk_window_get_default_size(m_host.window, &width, &height);
	  ok &= width >= 1600 && height >= 900 &&
	    gtk_paned_get_position(GTK_PANED(gtk_bin_get_child(GTK_BIN(m_host.window)))) >= 900;
	}
	if (phase == 1) {
	  GtkTreeIter root;
	  ok &= gtk_tree_model_get_iter_from_string(m_host.model, &root, "0");
	  ok &= gtk_tree_model_iter_n_children(m_host.model, &root) == 0;
	  ok &= !gtk_tree_model_iter_has_child(m_host.model, &root);
	  for (unsigned i = 0; i < 5; ++i) {
	    auto col = gtk_tree_view_get_column(m_host.treeView, i);
	    ok &= gtk_tree_view_column_get_visible(col) &&
	      ZuCSpan{gtk_tree_view_column_get_title(col)} == (i ? "" : "ID");
	  }
	}
	if (phase == 1) {
	  auto path = gtk_tree_path_new_from_indices(src.row, -1);
	  gtk_tree_view_set_cursor(m_host.treeView, path, nullptr, false);
	  gtk_tree_path_free(path);
	  ok &= checkDetail_("startTime", "2026/10/01 12:34:56.123456789", false) &&
	    checkDetail_("ztcver", "12.34.567", false);
	}
	if (phase == 1) m_row = src.identity;
	else if (phase != 12) ok &= src.identity == m_row;
	ok = ok && src.rag ==
	  (phase == 3 ? Ztc::RAG::Red :
            phase == 13 ? Ztc::RAG::Off : Ztc::RAG::Green);
	GtkTreeIter iter;
	ok = ok && gtk_tree_model_iter_nth_child(
	  m_host.model, &iter, nullptr, src.row);
	if (ok) {
	  auto path = gtk_tree_model_get_path(m_host.model, &iter);
	  ok = gtk_tree_path_get_depth(path) == 1 &&
	    gtk_tree_path_get_indices(path)[0] == src.row;
	  gtk_tree_path_free(path);
	  ZGtk::Value publisher, device;
	  gtk_tree_model_get_value(m_host.model, &iter,
	    m_host.publisherCol, &publisher);
	  // Consume borrowed model text before another text read reuses its buffer.
	  ok = ok && ZuCSpan{publisher.get_string()} == "publisher";
	  gtk_tree_model_get_value(m_host.model, &iter,
	    m_host.deviceCol, &device);
	  ok = ok && ZuCSpan{device.get_string()} == "test-a";
	}
      }
      if (ok && phase == 8)
	ok = !gtk_tree_model_iter_n_children(m_host.model, nullptr) &&
	  !gtk_tree_model_iter_n_children(gtk_tree_view_get_model(m_host.details), nullptr);
      if (ok && phase == 2)
	// Retirement must release expanded descendants while siblings remain.
	gtk_tree_view_expand_all(m_host.treeView);
      if (ok && phase == 5) {
	unsigned rows = 0;
	gtk_tree_model_foreach(m_host.model, [](
	    GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter,
	    gpointer data) -> gboolean {
	  ++*static_cast<unsigned *>(data);
	  int cols = gtk_tree_model_get_n_columns(model);
	  for (int col = 0; col < cols; ++col) {
	    ZGtk::Value value;
	    gtk_tree_model_get_value(model, iter, col, &value);
	  }
	  return false;
	}, &rows);
	// Every non-alert telemetry kind must have a visible row.
	ok = rows >= Telemetry::TypeList::N - 1 && checkKeys_();
	GtkTreeIter heap;
	ok &= gtk_tree_model_get_iter_from_string(m_host.model, &heap, "0:0:0:0");
	m_heap = heap;
	m_notificationsBefore = m_notifications;
      }
      if (ok && phase == 6) ok &= checkDetail_("id", "heap", false);
      if (ok && phase == 7) ok &= checkDetail_("state", "Active", false) &&
	checkDetail_("rag", "Green", false);
      if (ok && (phase == 6 || phase == 7)) {
	GtkTreeIter heaps, db, hashes;
	ok &= gtk_tree_model_get_iter_from_string(m_host.model, &heaps, "0:0");
	ok &= gtk_tree_model_iter_n_children(m_host.model, &heaps) == phase - 4;
	GtkTreeIter heapID;
	ok &= gtk_tree_model_iter_parent(m_host.model, &heapID, &m_heap) &&
	  gtk_tree_model_iter_n_children(m_host.model, &heapID) == 2;
	ok &= gtk_tree_model_get_iter_from_string(m_host.model, &hashes, "0:1");
	ok &= gtk_tree_model_iter_n_children(m_host.model, &hashes) == 1;
	ok &= gtk_tree_model_get_iter_from_string(m_host.model, &db, "0:7:0");
	ZGtk::Value rag;
	gtk_tree_model_get_value(m_host.model, &db, 0, &rag);
	ok &= rag.get_int() == (phase == 6 ? Ztc::RAG::Amber : Ztc::RAG::Green);
	ok &= m_notifications > m_notificationsBefore;
	// The persistent iterator and selection follow the original object after
	// a new sorted sibling changes its path; updates do not replace that row.
	auto path = gtk_tree_model_get_path(m_host.model, &m_heap);
	auto text = gtk_tree_path_to_string(path);
	ok &= ZuCSpan{text} == "0:0:1:0";
	g_free(text);
	auto selection = gtk_tree_view_get_selection(m_host.treeView);
	if (phase == 6)
	  ok &= gtk_tree_selection_path_is_selected(selection, path);
	gtk_tree_path_free(path);
	ZGtk::Value value;
	gtk_tree_model_get_value(m_host.model, &m_heap, 1, &value);
	ok &= ZuCSpan{value.get_string()} == "7";
	auto groupPath = gtk_tree_path_new_from_string("0:0");
	if (phase == 6) {
	  ok &= gtk_tree_view_row_expanded(m_host.treeView, groupPath);
	  gtk_tree_view_collapse_row(m_host.treeView, groupPath);
	} else {
	  ok &= !gtk_tree_view_row_expanded(m_host.treeView, groupPath);
	  gtk_tree_view_expand_row(m_host.treeView, groupPath, false);
	  ok &= gtk_tree_view_row_expanded(m_host.treeView, groupPath);
	  ok &= checkFormatting_();
	}
	gtk_tree_path_free(groupPath);
	ok &= checkRAG_(&db, true);
	if (phase == 6) {
	  auto path = gtk_tree_model_get_path(m_host.model, &db);
	  gtk_tree_view_expand_to_path(m_host.treeView, path);
	  gtk_tree_view_set_cursor(m_host.treeView, path, nullptr, false);
	  gtk_tree_path_free(path);
	  ok &= checkDetail_("state", "Stopping", false) &&
	    checkDetail_("rag", "Amber", false);
	}
      }
      if (ok && phase == 7) {
	auto path = gtk_tree_model_get_path(m_host.model, &m_heap);
	gtk_tree_view_expand_to_path(m_host.treeView, path);
	gtk_tree_view_set_cursor(m_host.treeView, path, nullptr, false);
	m_savedHeap = gtk_tree_row_reference_new(m_host.model, path);
	gtk_tree_path_free(path);
      }
      if (ok && (phase == 10 || phase == 13 || phase == 11)) {
	auto path = gtk_tree_row_reference_get_path(m_savedHeap);
	auto parent = gtk_tree_path_copy(path);
	gtk_tree_path_up(parent);
	ok &= gtk_tree_row_reference_valid(m_savedHeap) &&
	  gtk_tree_selection_path_is_selected(
	    gtk_tree_view_get_selection(m_host.treeView), path) &&
	  gtk_tree_view_row_expanded(m_host.treeView, parent) &&
	  checkDetail_("id", "heap", false) &&
	  checkDetail_("allocated", phase == 11 ? "1,379" : "0", true);
	gtk_tree_path_free(parent);
	gtk_tree_path_free(path);
      }
      if (ok && phase == 12) {
	GtkTreeIter root;
	ok &= !gtk_tree_row_reference_valid(m_savedHeap) &&
	  gtk_tree_model_get_iter_from_string(m_host.model, &root, "0") &&
	  !gtk_tree_model_iter_n_children(m_host.model, &root) &&
	  !gtk_tree_model_iter_n_children(
	    gtk_tree_view_get_model(m_host.details), nullptr);
	gtk_tree_row_reference_free(m_savedHeap);
	m_savedHeap = nullptr;
      }
      if (!ok) {
	m_failed = true;
	ZiLOG(Error, "zdash", ([phase](auto &s) {
	  s << "hidden test phase " << phase << " failed";
	}));
	m_host.stop();
	return;
      }
      m_checked = phase;
      if (phase == 3 && !m_wrapped) {
	// Keep bounded, acknowledged bursts until GTK has decoded a record
	// spanning the mirrored mapping boundary, not just crossed the head.
	m_phase = 0; // wait for Rx to publish the next complete burst
	m_checked = 0;
	m_host.rxRun([this]() { feed_(3); });
	return;
      }
      if (phase != 8) {
	unsigned next = phase == 7 ? 10 : phase == 10 ? 13 :
	  phase == 13 ? 11 : phase == 11 ? 12 : phase == 12 ? 8 : phase + 1;
	m_host.rxRun([this, next]() { feed_(next); });
	return;
      }
      m_shutdownPending = true;
      m_host.rxRun([this]() { feed_(9); });
      return;
    } else if (m_live ? !checkLive_() :
	!gtk_tree_model_iter_n_children(m_host.model, nullptr)) return;
    m_complete = true;
    // Exercise the real GTK close-event path on the isolated display.
    gtk_window_close(m_host.window);
  }


private:
  // Rx failure is published before each GTK phase handoff.
  bool m_local = false;
  bool m_mapped = false;
  bool m_live = false;
  ZtString<ZtStringHeapID<"ZDash.TestSource">> m_liveDevice;
  ZtString<ZtStringHeapID<"ZDash.TestSource">> m_livePublisher;
  unsigned m_livePhase = 0;
  uint64_t m_liveGeneration = 0;
  ZmAtomic<unsigned> m_failed = false;
  ZmAtomic<unsigned> m_inventoryReqs = 0;
  ZmAtomic<unsigned> m_detailReqs = 0;
  ZmAtomic<unsigned> m_cancels = 0;
  // GTK-owned until session() has drained both owners.
  ZDash::ModuleHost m_host;
  const void *m_row = nullptr;
  GThread *m_gtkThread = nullptr;
  GtkTreeIter m_heap;
  GtkTreeRowReference *m_savedHeap = nullptr;
  unsigned m_notifications = 0;
  unsigned m_notificationsBefore = 0;
  unsigned m_partialDrains = 0;
  unsigned m_pendingEvents = 0;
  unsigned m_phase = 0;
  unsigned m_checked = 0;
  guint m_event = 0;
  bool m_complete = false;
  bool m_closed = false;
  bool m_painted = false;
  bool m_wrapped = false;
  bool m_closing = false;
  bool m_shutdownPending = false;
};

extern "C" ZuExport_API ZDash::Module *ZdashModule()
{
  return new Driver;
}
