//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>
#include <iostream>
#include <stdlib.h>
#include <quickfix/config.h>
#include <quickfix/Application.h>
#include <quickfix/DataDictionary.h>
#include <quickfix/Session.h>
#include <quickfix/SocketAcceptor.h>
#include <quickfix/SocketInitiator.h>
#include <quickfix/SSLSocketAcceptor.h>
#include <quickfix/SSLSocketInitiator.h>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include "quickfixapp.hh"
#include "../util/load_run.hh"

namespace QfLoad {

bool Store::set(FIX::SEQNUM seq, const std::string &bytes) {
  FIX::Locker guard{m_lock};
  if (bytes.length() > UINT_MAX || !m_messages.fits(bytes.length())) return false;
  Text copy{ZuCSpan{bytes.data(), unsigned(bytes.length())}};
  m_messages.erase(seq);
  return m_messages.put(seq, ZuMv(copy), bytes.length());
}
void Store::get(FIX::SEQNUM begin, FIX::SEQNUM end, std::vector<std::string> &out) const {
  FIX::Locker guard{m_lock};
  using Record = ZuTuple<FIX::SEQNUM, const Text *>;
  Rows<Record> found;
  m_messages.each([begin, end, &found](auto key, const Text &value) {
    if (key >= begin && key <= end) found.push(Record{key, &value});
  });
  qsort(found.data(), found.length(), sizeof(Record), [](const void *a, const void *b) {
    auto x = static_cast<const Record *>(a)->p<0>();
    auto y = static_cast<const Record *>(b)->p<0>();
    return (x > y) - (x < y);
  });
  if (begin <= end && uint64_t(found.length()) <= end - begin)
    m_misses += end - begin + 1 - found.length();
  for (const auto &record : found) {
    const auto &value = *record.p<1>(); out.emplace_back(value.data(), value.length());
  }
}
FIX::SEQNUM Store::getNextSenderMsgSeqNum() const { FIX::Locker g{m_lock}; return m_sender; }
FIX::SEQNUM Store::getNextTargetMsgSeqNum() const { FIX::Locker g{m_lock}; return m_target; }
void Store::setNextSenderMsgSeqNum(FIX::SEQNUM n) { FIX::Locker g{m_lock}; m_sender = n; }
void Store::setNextTargetMsgSeqNum(FIX::SEQNUM n) { FIX::Locker g{m_lock}; m_target = n; }
void Store::incrNextSenderMsgSeqNum() { FIX::Locker g{m_lock}; ++m_sender; }
void Store::incrNextTargetMsgSeqNum() { FIX::Locker g{m_lock}; ++m_target; }
FIX::UtcTimeStamp Store::getCreationTime() const { FIX::Locker g{m_lock}; return m_creation; }
void Store::reset(const FIX::UtcTimeStamp &now) {
  FIX::Locker g{m_lock};
  if (!m_reset && (m_sender != 1 || m_target != 1 || m_messages.count()))
    throw FIX::IOException("Unagreed fixture sequence reset");
  if (m_reset > 0) m_reset = 0;
  m_messages.clear(); m_creation = now; m_sender = m_target = 1;
}
void Store::resetPolicy(bool armed) { FIX::Locker g{m_lock}; m_reset = armed; }
uint64_t Store::peak() const { FIX::Locker g{m_lock}; return m_messages.peak(); }
uint64_t Store::misses() const { FIX::Locker g{m_lock}; return m_misses; }
unsigned Store::count() const { FIX::Locker g{m_lock}; return m_messages.count(); }

// Versioned same-host fixture records, not a framework persistence backend.
// Only main calls these after SDK join/owner drain or before the first start.
template <typename ...T> static bool write(ZiFile &file, const T &...value) {
  return ((file.write(&value, sizeof(value)) == Zi::OK) && ...);
}
template <typename ...T> static bool read(ZiFile &file, T &...value) {
  return ((file.read(&value, sizeof(value)) == sizeof(value)) && ...);
}
static bool writeText(ZiFile &file, ZuCSpan text) {
  uint32_t length = text.length();
  return write(file, length) && (!length || file.write(text.data(), length) == Zi::OK);
}
static bool readText(ZiFile &file, Text &text, uint64_t limit) {
  uint32_t length;
  if (!read(file, length) || length > limit) return false;
  text.length(length);
  return !length || file.read(text.data(), length) == length;
}
bool Store::save(const char *path, const FIX::SessionID &id, uint64_t completed) const {
  FIX::Locker guard{m_lock};
  ZiFile file{path, ZiFile::Write | ZiFile::NoFollow | ZiFile::GC, 0600};
  uint32_t magic = 0x51465331, version = 1, count = m_messages.count();
  const auto &name = id.toStringFrozen();
  auto created = FIX::UtcTimeStampConvertor::convert(m_creation, 9);
  if (!file || !write(file, magic, version, completed, m_sender, m_target, count) ||
      !writeText(file, {name.data(), unsigned(name.length())}) ||
      !writeText(file, {created.data(), unsigned(created.length())})) return false;
  bool ok = true;
  m_messages.each([&file, &ok](auto seq, const Text &wire) {
    ok = ok && write(file, seq) && writeText(file, wire);
  });
  return ok && file.sync() == Zi::OK;
}
bool Store::load(const char *path, const FIX::SessionID &id, uint64_t completed,
    const char *dictionary) {
  FIX::Locker guard{m_lock};
  if (m_messages.count() || m_sender != 1 || m_target != 1) return false;
  ZiFile file{path, ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC};
  uint32_t magic, version, count;
  uint64_t boundary, sender, target;
  Text name, created;
  const auto &identity = id.toStringFrozen();
  if (!file || !read(file, magic, version, boundary, sender, target, count) ||
      magic != 0x51465331 || version != 1 || boundary != completed ||
      !sender || !target || count > m_limit ||
      !readText(file, name, identity.length()) ||
      name != ZuCSpan{identity.data(), unsigned(identity.length())} ||
      !readText(file, created, FIX::UtcTimeStampConvertor::convert(m_creation, 9).length()))
    return false;
  try {
    auto creation = FIX::UtcTimeStampConvertor::convert(
      std::string{created.data(), created.length()});
    FIX::DataDictionary dict{dictionary};
    for (unsigned i = 0; i < count; ++i) {
      uint64_t seq; Text wire;
      if (!read(file, seq) || !seq || seq >= sender || m_messages.get(seq) ||
	  !readText(file, wire, m_max) || !wire.length() || !m_messages.fits(wire.length()))
	return false;
      FIX::Message message{std::string{wire.data(), wire.length()}, dict, true};
      dict.validate(message);
      const auto &header = message.getHeader();
      if (header.getField<FIX::MsgSeqNum>() != seq ||
	  header.getField(8) != id.getBeginString().getValue() ||
	  header.getField(49) != id.getSenderCompID().getValue() ||
	  header.getField(56) != id.getTargetCompID().getValue()) return false;
      auto bytes = wire.length();
      if (!m_messages.put(seq, ZuMv(wire), bytes) || m_messages.count() != i + 1) return false;
    }
    if (file.offset() != file.size()) return false;
    m_sender = sender; m_target = target; m_creation = creation;
    return true;
  } catch (const FIX::Exception &) { return false; }
}

struct NoRun { NoRun(const Options &, unsigned) { } };

struct NoBroker { NoBroker(const Options &, unsigned) { } };

template <bool Client> struct Slot : public ZmNHeap<Slot<Client>> {
friend inline ZuStringT<"Zfix.Qf.Slot"> ZmNHeapID(Slot *);
  FIX::SessionID			id;
  ZuIf<Client, Run, NoRun>		run;
  ZuIf<Client, NoBroker, Broker>	broker;
  Result				result;
  Measurements				measurement;
  ZmScheduler::Timer			pace;
  uint64_t				generation = 0;
  unsigned				connections = 0, closes = 0;
  bool					ready = false, closed = false;

  Slot(const Options &o, unsigned index, FIX::SessionID s) :
    id{ZuMv(s)}, run{o, index}, broker{o, index} { result.session = index + 1; }
};

using Index = ZmHashKV<ZuCSpan, unsigned, ZmHashLock<ZmNoLock,
  ZmHashHeapID<"Zfix.Qf.Index">>>;
template <bool Client> class App : public FIX::Application, public FIX::MessageStoreFactory,
    public FIX::LogFactory {
  using Item = Slot<Client>;
  class Log : public ZmNHeap<Log>, public FIX::Log {
  friend inline ZuStringT<"Zfix.Qf.Log"> ZmNHeapID(Log *);
  public:
    Log(App *a, unsigned i) : m_app{a}, m_index{i} { }

    void clear() override { }
    void backup() override { }

    void onIncoming(const std::string &s) override { m_app->metric(m_index, true, s.length()); }
    void onOutgoing(const std::string &s) override { m_app->metric(m_index, false, s.length()); }

    void onEvent(const std::string &) override { } // timing runs disable wire/event logging
  private:
    App			*m_app;
    const unsigned	m_index;
  };

public:
  App(const Options &o, const Ports &ports, ZmScheduler &scheduler) :
    m_options{o}, m_scheduler{scheduler}, m_budget{o.queueBytes * o.sessions} {
    m_slots.size(o.sessions);
    for (unsigned i = 0; i < o.sessions; ++i) {
      Text sender; sender << o.sender << '-' << i + 1;
      FIX::SessionID id{"FIX.4.4", Client ? str(sender) : str(o.target),
	Client ? str(o.target) : str(sender)};
      m_slots.push(new Item{o, i, id});
    }
    for (unsigned i = 0; i < o.sessions; ++i) {
      const auto &frozen = m_slots[i]->id.toStringFrozen();
      m_index.add(ZuCSpan{frozen.data(), unsigned(frozen.length())}, i);
    }
    FIX::Dictionary defaults;
    defaults.setString(FIX::CONNECTION_TYPE, Client ? "initiator" : "acceptor");
    defaults.setBool(FIX::NON_STOP_SESSION, true);
    defaults.setString(FIX::START_TIME, "00:00:00");
    defaults.setString(FIX::END_TIME, "00:00:00");
    defaults.setString(FIX::DATA_DICTIONARY, QUICKFIX_DICT_PATH);
    defaults.setBool(FIX::USE_DATA_DICTIONARY, true); defaults.setBool(FIX::SOCKET_NODELAY, true);
    defaults.setBool(FIX::RESET_ON_LOGON, false);
    defaults.setBool(FIX::RESET_ON_LOGOUT, false);
    defaults.setBool(FIX::RESET_ON_DISCONNECT, false);
    defaults.setBool(FIX::PERSIST_MESSAGES, true);
    defaults.setInt(FIX::HEARTBTINT, o.heartbeat);
    defaults.setInt(FIX::LOGON_TIMEOUT, o.deadline);
    defaults.setInt(FIX::LOGOUT_TIMEOUT, o.deadline); defaults.setInt(FIX::RECONNECT_INTERVAL, 1);
    defaults.setInt(FIX::TIMESTAMP_PRECISION, 9);
    if (!o.tcp) {
      if constexpr (Client) defaults.setString(FIX::CERTIFICATE_AUTHORITIES_FILE, str(o.ca));
      else {
	defaults.setString(FIX::SERVER_CERTIFICATE_FILE, str(o.cert));
	defaults.setString(FIX::SERVER_CERTIFICATE_KEY_FILE, str(o.key));
      }
    }
    m_settings.set(defaults);
    for (unsigned i = 0; i < o.sessions; ++i) {
      FIX::Dictionary settings;
      if constexpr (Client) {
	settings.setString(FIX::SOCKET_CONNECT_HOST, str(o.host));
	settings.setInt(FIX::SOCKET_CONNECT_PORT, ports[i]);
      } else settings.setInt(FIX::SOCKET_ACCEPT_PORT, ports[i]);
      m_settings.set(m_slots[i]->id, settings);
    }
  }

  const FIX::SessionSettings &settings() const { return m_settings; }
  const Rows<ZuPtr<Item>> &slots() const { return m_slots; }
  unsigned errors() const { return m_errors.load_(); }

  int waitReady(ZuTime at) { return m_readySem.timedwait(at); }
  int waitComplete(ZuTime at) { return m_completeSem.timedwait(at); }
  int waitClosed(ZuTime at) { return m_closedSem.timedwait(at); }

  void fail(const char *where = "callback") {
    if (!m_errors.xchAdd(1))
      ZiLOG(Error, "quickfix", ([where](auto &s) {
	s << "QuickFIX fixture failure: " << where;
      }));
    m_readySem.post(); m_completeSem.post(); m_closedSem.post();
  }

  void ports_(Ports ports) { m_ports = ZuMv(ports); }

  void quiesce() {
    m_live = false;
    for (auto &s : m_slots) { ++s->generation; m_scheduler.del(&s->pace); }
  }

  void finish(uint64_t start) {
    for (unsigned i = 0, n = m_slots.length(); i < n; ++i) {
      auto &s = *m_slots[i]; auto &r = s.result;
      r.engine = "quickfix"; r.role = Client ? "initiator" : "acceptor"; r.errors += errors();
      if constexpr (Client) s.run.finish(r, monotonic(), &s.measurement);
      else {
	r.progress = s.broker.progress; r.cached = s.broker.cached;
	r.replayRefused = s.broker.replayRefused; r.elapsedNs = monotonic() - start;
	r.warmup = m_options.warmup; r.targetRate = m_options.rate; r.seed = m_options.seed;
	r.cap = m_options.count; r.inFlightLimit = m_options.inFlight;
	if ((!m_options.duration &&
	    r.progress.admitted != uint64_t(m_options.warmup) + m_options.count) ||
	    r.progress.complete != r.progress.admitted) ++r.errors;
      }
      if (!s.ready || !s.closed) ++r.errors;
      r.connections = s.connections;
      if (i < m_stores.length()) {
	r.retainedPeak = m_stores[i]->peak(); r.historyMiss = m_stores[i]->misses();
	r.nextSender = m_stores[i]->getNextSenderMsgSeqNum();
	r.nextTarget = m_stores[i]->getNextTargetMsgSeqNum();
      }
    }
  }

  bool save() const {
    if constexpr (Client) {
      if (!m_options.stateOut.length() || errors()) return !errors();
      const auto &s = *m_slots[0];
      if (s.run.phase() != Phase::Complete) return false;
      return m_stores[0]->save(m_options.stateOut.data(), s.id,
	 m_options.keyOffset + m_options.count);
    }
    return true;
  }

  bool results() {
    bool failed = errors();
    for (auto &s : m_slots) {
      auto &r = s->result;
      if constexpr (Client) {
	s->measurement.finish(r);
	if (r.progress.inFlight || r.progress.invalid || r.progress.refused ||
	    (!m_options.duration && r.progress.complete != m_options.count) ||
	    r.ackNs.count != r.progress.complete || r.terminalNs.count != r.progress.complete)
	  ++r.errors;
      }
      failed |= bool(r.errors); output(r);
    }
    return !failed;
  }

  void logout() {
    for (const auto &s : m_slots) {
      auto session = FIX::Session::lookupSession(s->id);
      if (session) session->logout();
    }
  }

  void onCreate(const FIX::SessionID &) override { }

  void onLogon(const FIX::SessionID &id) override {
    auto i = index(id);
    post(sizeof(unsigned), [this, i] {
      auto &s = *m_slots[i];
      if (s.connections != s.closes || ++s.connections > m_options.connections) {
	fail("unexpected Logon"); return;
      }
      s.closed = false;
      if (s.ready) return;
      s.ready = true;
      if (++m_ready == m_slots.length()) {
	m_readySem.post();
	if constexpr (Client) for (unsigned j = 0, n = m_slots.length(); j < n; ++j) {
	  if (!m_slots[j]->run.start(monotonic())) fail("start"); else pump(j);
	}
      }
    });
  }

  void onLogout(const FIX::SessionID &id) override {
    auto i = index(id);
    post(sizeof(unsigned), [this, i] {
      auto &s = *m_slots[i]; if (s.closes == s.connections) return;
      if constexpr (Client)
	if (m_live && s.run.phase() != Phase::Complete) fail("early Logout");
      ++s.closes;
      if (s.closes == m_options.connections) {
	s.closed = true; if (++m_closed == m_slots.length()) m_closedSem.post();
      } else if constexpr (!Client) {
	if (s.closes == 1 && m_options.recoverRx) {
	  auto store = m_stores[i];
	  if (m_options.recoverRx >= store->getNextTargetMsgSeqNum()) {
	    fail("stale receive checkpoint boundary"); return;
	  }
	  store->setNextTargetMsgSeqNum(m_options.recoverRx);
	}
	if (m_options.resetLogon && s.closes == 1) m_stores[i]->resetPolicy(true);
	// SDK drop() has already closed the socket. Its single IO thread drains
	// dropped connections/unregisters before accepting any newly arriving
	// socket. This application turn drains our earlier callback work.
	post(sizeof(unsigned), [this, i] {
	  if (!m_live || errors()) return;
	  ReconnectReady event;
	  event.connection = m_slots[i]->connections + 1; event.ports = m_ports;
	  event.barrier = "sdk-drop-before-accept"; output(event);
	});
      }
    });
  }

  void toAdmin(FIX::Message &m, const FIX::SessionID &) override {
    if constexpr (Client)
      if (m_options.resetLogon && m.getHeader().getField(35) == "A")
	m.setField(FIX::ResetSeqNumFlag{true});
  }

  void toApp(FIX::Message &, const FIX::SessionID &) override { }

  void fromAdmin(const FIX::Message &m, const FIX::SessionID &) override {
    if (m.getHeader().getField(35) == "3") fail("session Reject");
  }

  void fromApp(const FIX::Message &message, const FIX::SessionID &id) override {
    auto i = index(id); const auto &type = message.getHeader().getField(35);
    bool replay = message.getHeader().isSetField(43) && message.getHeader().getField(43) == "Y";
    try {
      if constexpr (Client) {
	if (type != "8") { fail("unexpected report type"); throw FIX::UnsupportedMessageType(); }
	receive<Report>(message, i, replay);
      } else {
	switch (type.length() == 1 ? type[0] : '\0') {
	  case 'D': receive<Order>(message, i, replay); break;
	  case 'F': receive<Cancel>(message, i, replay); break;
	  case 'G': receive<Replace>(message, i, replay); break;
	  default: fail("unexpected order type"); throw FIX::UnsupportedMessageType();
	}
      }
    } catch (const FIX::Exception &) { fail("decode"); throw; }
  }

  FIX::MessageStore *create(const FIX::UtcTimeStamp &now, const FIX::SessionID &id) override {
    ZuPtr<Store> store{new Store{now, m_options.retain, m_options.cacheBytes}};
    if constexpr (Client)
      if (m_options.stateIn.length() && !store->load(m_options.stateIn.data(), id,
	  m_options.keyOffset, QUICKFIX_DICT_PATH))
	throw FIX::ConfigError("Invalid SDK fixture checkpoint");
    store->resetPolicy(false); // no implicit reset of restored/continuing progress
    unsigned i = index(id);
    if (m_stores.length() < m_slots.length()) m_stores.length(m_slots.length());
    m_stores[i] = store.ptr(); return ZuMv(store).release();
  }

  void destroy(FIX::MessageStore *store) override { delete store; }

  FIX::Log *create() override { return new Log{this, UINT_MAX}; }
  FIX::Log *create(const FIX::SessionID &id) override { return new Log{this, index(id)}; }

  void destroy(FIX::Log *log) override { delete log; }

private:
  static std::string str(ZuCSpan v) { return {v.data(), v.length()}; }

  unsigned index(const FIX::SessionID &id) const {
    const auto &value = id.toStringFrozen();
    auto n = m_index.findPtr(ZuCSpan{value.data(), unsigned(value.length())});
    ZmAssert_(n); return n->val();
  }

  template <typename Fn> bool post(uint64_t bytes, Fn fn) {
    auto used = m_posted.load_();
    for (;;) {
      if (bytes > m_budget || used > m_budget - bytes) { fail("callback budget"); return false; }
      auto previous = m_posted.cmpXch(used + bytes, used);
      if (previous == used) break;
      used = previous;
    }
    m_scheduler.run([this, bytes, fn = ZuMv(fn)]() mutable {
      { auto work = ZuMv(fn); work(); }
      m_posted.xchSub(bytes);
    }, 1);
    return true;
  }

  void metric(unsigned i, bool incoming, unsigned bytes) {
    if (i == UINT_MAX) return;
    post(sizeof(uint64_t), [this, i, incoming, bytes] {
      auto &r = m_slots[i]->result;
      if (incoming) { ++r.rxFrames; r.rxBytes += bytes; }
      else { ++r.txFrames; r.txBytes += bytes; }
    });
  }

  template <typename T> void receive(const FIX::Message &m, unsigned i, bool replay) {
    auto length = m.calculateLength();
    if (length <= 0 || uint64_t(length) > m_options.queueBytes) { fail("input size"); return; }
    ZmRef<Input<T>> input = new Input<T>{unsigned(length)};
    decode(m, *input);
    uint64_t bytes = ZmHeapAllocSize<sizeof(Input<T>)>{} + input->arena.bytes();
    if constexpr (ZuIsSame<T, Order>{}) {
      bytes += input->value.parties.size() * sizeof(Party);
      for (const auto &p : input->value.parties) bytes += p.subs.size() * sizeof(SubParty);
    } else if constexpr (ZuIsSame<T, Replace>{}) {
      bytes += input->value.order.parties.size() * sizeof(Party);
      for (const auto &p : input->value.order.parties) bytes += p.subs.size() * sizeof(SubParty);
    }
    post(bytes, [this, i, replay, input = ZuMv(input)] { received(i, input->value, replay); });
  }

  template <typename T> bool send(unsigned i, const T &value) {
    try {
      FIX::Message message; encode(message, value);
      return FIX::Session::sendToTarget(message, m_slots[i]->id);
    } catch (const FIX::Exception &error) {
      ZiLOG(Error, "quickfix", ([message = Text{error.what()}](auto &s) {
	s << "QuickFIX send: " << message;
      }));
      return false;
    }
  }

  template <typename T> void received(unsigned i, const T &value, bool replay) {
    if (!m_live || errors()) return;
    auto &s = *m_slots[i];
    if constexpr (Client) {
      if (!s.run.shape(value.text, value.encoded)) {
	fail("report shape"); return;
      }
      auto previous = s.run.phase();
      if (s.run.receive(value, monotonic(), [this, i](const auto &followup) {
	  return this->send(i, followup);
	}) == Outcome::Invalid) fail("report progression");
      if (s.run.phase() == Phase::Complete) {
	if (previous != Phase::Complete && ++m_complete == m_slots.length()) m_completeSem.post();
      } else pump(i);
    } else {
      if (!s.broker.receive(value, replay, [this, i](const Report &r) { return send(i, r); }))
	fail("broker");
    }
  }

  void pump(unsigned i) {
    if constexpr (Client) {
      if (!m_live || errors()) return;
      auto &s = *m_slots[i];
      if (s.run.phase() == Phase::Complete) return;
      ++s.generation; m_scheduler.del(&s.pace);
      s.run.pump(monotonic(), [this, i](const Order &o) { return send(i, o); });
      if (s.run.phase() == Phase::Failed) { fail("pump"); return; }
      if (s.run.phase() == Phase::Complete) {
	if (++m_complete == m_slots.length()) m_completeSem.post(); return;
      }
      auto now = monotonic(), at = s.run.nextDue(now);
      if (!at) return;
      auto serial = s.generation;
      m_scheduler.add(&s.pace, Zm::now() + ZuTime{ZuTime::Nano{at > now ? at - now : 0}},
	ZmScheduler::Update, [this, i, serial](auto &&arm) {
	  return arm([this, i, serial] {
	    if (m_live && serial == m_slots[i]->generation) pump(i);
	  });
	}, 1);
    }
  }

  const Options		&m_options;
  ZmScheduler		&m_scheduler;
  const uint64_t	m_budget;
  FIX::SessionSettings	m_settings;
  Ports			m_ports;
  Rows<ZuPtr<Item>>	m_slots;
  Index			m_index;
    // immutable after construction; SDK callbacks perform reads only
  Rows<Store *>		m_stores; // SDK creates before start; main reads after IO join
  // Exact multi-producer admission/refund is genuinely shared with the owner.
  // Keep its writes away from the immutable callback lookup table.
  alignas(Zm::CacheLineSize)
  ZmAtomic<uint64_t>	m_posted{0};
  ZmAtomic<unsigned>	m_errors{0};
  ZmSemaphore		m_readySem, m_completeSem, m_closedSem;
  unsigned		m_ready = 0, m_complete = 0, m_closed = 0;
  bool			m_live = true;
};

template <bool Client, typename Peer>
static bool execute(App<Client> &app, Peer &peer, ZmScheduler &scheduler,
    const Options &o, Ports &ports) {
  auto deadline = Zm::now(o.deadline); auto start = monotonic();
  bool started = false;
  try {
    peer.start(); started = true;
    if constexpr (!Client) {
      for (unsigned i = 0, n = app.slots().length(); i < n; ++i) {
	const auto &map = peer.sessionToPort(); auto p = map.find(app.slots()[i]->id);
	if (p == map.end()) { app.fail("listener port"); break; }
	ports[i] = p->second;
      }
      ZmSemaphore configured; auto actual = ports;
      scheduler.run([&app, &configured, actual = ZuMv(actual)]() mutable {
	app.ports_(ZuMv(actual)); configured.post();
      }, 1);
      configured.wait();
      Ready ready; ready.engine = "quickfix"; ready.role = "acceptor";
      ready.ports = ports; output(ready);
    }
    if (app.waitReady(deadline)) app.fail("Logon deadline");
    if constexpr (Client) {
      if (!app.errors()) {
	Ready ready; ready.engine = "quickfix"; ready.role = "initiator";
	ready.ports = ports; output(ready);
	if (app.waitComplete(deadline)) app.fail("workload deadline");
      }
      ZmSemaphore posted;
      scheduler.run([&app, &posted] { app.quiesce(); app.logout(); posted.post(); }, 1);
      posted.wait();
    }
    if (!app.errors() && app.waitClosed(deadline)) app.fail("Logout deadline");
  } catch (const std::exception &error) {
    ZiLOG(Error, "quickfix", ([message = Text{error.what()}](auto &s) {
      s << "QuickFIX startup: " << message;
    }));
    app.fail("startup");
  }
  // Only main blocks in the SDK stop/join. Keep app and timer owners alive until
  // all callbacks queued by that IO thread have crossed their application owner.
  ZmSemaphore held;
  scheduler.run([&app, &held] { app.quiesce(); held.post(); }, 1); held.wait();
  if (started) peer.stop(app.errors() != 0);
  ZmSemaphore callbacks;
  scheduler.run([&callbacks] { callbacks.post(); }, 1); callbacks.wait();
  if (!app.save()) app.fail("checkpoint write");
  ZmSemaphore drained;
  scheduler.run([&app, &drained, start] { app.finish(start); drained.post(); }, 1);
  drained.wait();
  heapEvent();
  return app.results();
}
int run(bool server, int argc, char **argv) {
  Options o; Ports ports;
  if (server) o.bind = "0.0.0.0";
  if (!options(o, server, argc, argv, ports)) { usage(server); return 2; }
  if (o.help) {
    usage(server);
    std::cerr << "  quickfix/quickfixd support --mode 4 --version FIX.4.4\n";
    if (server) std::cerr <<
      "  quickfixd listens on 0.0.0.0; --bind accepts only 0.0.0.0\n";
    return 0;
  }
  if (server && o.bind != "0.0.0.0") {
    std::cerr << "quickfixd: --bind must be 0.0.0.0 (the SDK listener address)\n";
    return 2;
  }
  if (o.mode != Mode::FIX4 || o.version != Version::V4_4) {
    std::cerr << "QuickFIX fixtures require canonical FIX.4.4 mode 4\n"; return 2;
  }
  ZiLog::init(server ? "quickfixd" : "quickfix");
  ZiLog::level(Ze::Error);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1).thread(1, [](auto &t) { t.isolated(1); })};
  if (!scheduler.start()) { ZiLog::stop(); return 1; }
  bool ok = false;
  try {
    if (server) {
      App<false> app{o, ports, scheduler};
      if (o.tcp) { FIX::SocketAcceptor peer{app, app, app.settings(), app};
	ok = execute(app, peer, scheduler, o, ports); }
      else { FIX::SSLSocketAcceptor peer{app, app, app.settings(), app};
	ok = execute(app, peer, scheduler, o, ports); }
    } else {
      App<true> app{o, ports, scheduler};
      if (o.tcp) { FIX::SocketInitiator peer{app, app, app.settings(), app};
	ok = execute(app, peer, scheduler, o, ports); }
      else { FIX::SSLSocketInitiator peer{app, app, app.settings(), app};
	ok = execute(app, peer, scheduler, o, ports); }
    }
  } catch (const std::exception &error) {
    ZiLOG(Error, "quickfix", ([message = Text{error.what()}](auto &s) {
      s << "QuickFIX fixture: " << message;
    }));
  }
  scheduler.stop(); ZiLog::stop(); return ok ? 0 : 1;
}
} // QfLoad
