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
// Several GTK clock-check batches, small enough for the fixture's 16 KiB ring.
enum { Burst = 64 };

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

    ZtString<ZtStringSecret<true,
      ZtStringHeapID<"ZDash.TestToken">>> token{
      ::getenv("ZDASH_TEST_TOKEN")};
    session.token = token;
    session.hidden = true;
    session.offline = m_local;
    session.timeout = 15;
    if (auto value = ::getenv("ZDASH_TEST_TIMEOUT")) {
      auto n = ZuBox<unsigned>{value};
      if (!n || n > 3600) return 1;
      session.timeout = n;
    }
    if (!m_local && !token) return 1;
    session.ready = [this](const ZDash::ModuleHost &host) {
      m_host = host;
      if (m_local) m_host.rxRun([this]() { feed_(1); });
    };
    session.consumed = [this](bool wrapped) { m_wrapped |= wrapped; };
    session.drained = [this]() { wake_(); };
    session.closing = [this]() {
      m_closing = true;
      if (m_event) { g_source_remove(m_event); m_event = 0; }
    };
    session.closed = [this]() { m_closed = true; };
    bool ok = session.session(session);
    ok = ok && !m_failed && m_complete && m_closed;
    ZuCHECK(ok, "hidden GTK session, telemetry ownership and close event");
    return ok ? 0 : 1;
  }

private:
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

  bool checkKeys_() {
    // Literal fixture expectations cover all displayed identities and labels.
    // A GTK path followed by the five identity columns.
    static const char *rows[][6] = {
      {"0", "publisher", "test-a", "2", "", ""},
      {"0:0", "heaps", "partition", "size", "alignment", "sharded"},
      {"0:0:0", "heap", "7", "128", "64", "1"},
      {"0:1", "hashTbls", "addr", "", "", ""},
      {"0:1:0", "hash", "1234", "", "", ""},
      {"0:2", "threads", "", "", "", ""},
      {"0:2:0", "123", "", "", "", ""},
      {"0:3", "multiplexers", "", "", "", ""},
      {"0:3:0", "mx", "", "", "", ""},
      {"0:3:0:0", "mx", "192.0.2.1", "443", "192.0.2.2", "54321"},
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
    bool ok = gtk_tree_model_get_n_columns(m_host.model) == 6;
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
	  Ztc::RAG::Green : Ztc::RAG::Red);
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
    auto telemetry = [&deliver](ZuCSpan device, uint64_t generation,
	int rag, bool shutdown = false) {
      Zfb::IOBuilder builder{Frame{new FrameBuf}};
      auto dev = Zfb::Save::str(builder, device);
      auto id = Zfb::Save::str(builder, "publisher");
      // Shutdown is an empty protocol marker with no ZfbStruct payload type.
      auto value = shutdown ? Ztc::fbs::CreateShutdown(builder).Union() :
	ZfbStruct::save(builder, Ztc::AppTelemetry{
	  .version = "test", .rag = Ztc::RAG::T(rag)}).Union();
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
	auto dev = Zfb::Save::str(builder, "test-a");
	auto eos = ZfbStruct::save(builder, Ztc::EOS{ZuID{"publisher"}, 0});
	builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::EOS,
	  eos.Union(), 1, dev, 1));
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
      case 5: {
	// Exercise every concrete row allocator, placeholder parent and payload.
	auto send = [this]<typename T>(const T &data) {
	  Zfb::IOBuilder builder{Frame{new FrameBuf}};
	  auto device = Zfb::Save::str(builder, "test-a");
	  auto id = Zfb::Save::str(builder, "publisher");
	  auto value = ZfbStruct::save(builder, data);
	  auto tel = Ztc::saveTelemetry(builder, id, 0,
	    Ztc::fbs::TelemetryBodyTraits<ZfbType<T>>::enum_value,
	    value.Union());
	  builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::Telemetry,
	    tel.Union(), 1, device, 2));
	  auto frame = builder.buf();
	  // This phase covers GTK rendering beyond the App subscription.
	  if (!Ztc::msg(ZuBSpan{frame->data(), frame->length}) ||
	      !m_host.receive_(ZuBSpan{frame->data(), frame->length}, false))
	    m_failed = true;
	};
	ZuUnroll::all<Telemetry::TypeList>([&send]<typename T>() {
	  if constexpr (!ZuIsSame<T, Ztc::AppTelemetry>{}) send(sample<T>());
	});
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
      case 6:
	telemetry("test-a", 2, Ztc::RAG::Off, true);
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
      auto src = m_host.source_("test-a", "publisher");
      bool ok = phase == 6 ? !src.identity && !src.count :
	src.identity && src.generation == (phase >= 4 ? 2 : 1) &&
	src.count == (phase >= 4 ? 1 : 2);
      if (ok && phase != 6) {
	if (phase == 1) m_row = src.identity;
	else if (phase < 4) ok = src.identity == m_row;
	ok = ok && src.rag ==
	  (phase == 3 ? Ztc::RAG::Red : Ztc::RAG::Green);
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
	  gtk_tree_model_get_value(m_host.model, &iter,
	    m_host.deviceCol, &device);
	  ok = ok && ZuCSpan{publisher.get_string()} == "publisher" &&
	    ZuCSpan{device.get_string()} == "test-a";
	}
      }
      if (ok && phase == 6)
	ok = !gtk_tree_model_iter_n_children(m_host.model, nullptr);
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
      if (phase < 6) {
	m_host.rxRun([this, phase]() { feed_(phase + 1); });
	return;
      }
    } else if (!gtk_tree_model_iter_n_children(m_host.model, nullptr)) return;
    m_complete = true;
    // Exercise the real GTK close-event path while the window stays unmapped.
    gtk_window_close(m_host.window);
  }


private:
  // Rx failure is published before each GTK phase handoff.
  bool m_local = false;
  ZmAtomic<unsigned> m_failed = false;
  // GTK-owned until session() has drained both owners.
  ZDash::ModuleHost m_host;
  const void *m_row = nullptr;
  unsigned m_phase = 0;
  unsigned m_checked = 0;
  guint m_event = 0;
  bool m_complete = false;
  bool m_closed = false;
  bool m_wrapped = false;
  bool m_closing = false;
};

extern "C" ZuExport_API ZDash::Module *ZdashModule()
{
  return new Driver;
}
