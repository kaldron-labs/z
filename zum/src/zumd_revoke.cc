//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_revoke.hh>
#include <zlib/zumd_saga_ssf_delivery_add.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_db.hh>

#include <zlib/ZhttpURL.hh>

namespace Zum {

static bool ssfURL(ZuCSpan value)
{
  Zhttp::URL parsed{value};
  auto url = parsed.url();
  if (!parsed.ok() || !url.host || url.hasQuery || url.hasFragment ||
      (url.scheme != Zhttp::Scheme::https &&
       !(url.scheme == Zhttp::Scheme::http &&
         (url.host == "localhost" || url.host == "127.0.0.1" ||
          url.host == "::1"))))
    return false;
  return true;
}

static ZdbSagaID ssfSagaID(const SSFDelivery &delivery)
{
  Ztls::MD<> md;
  md.update(ZuBSpan{delivery.eventID});
  md.update(ZuBSpan{delivery.receiverID});
  ZuBArray<Ztls::MD<>::Size> digest(Ztls::MD<>::Size, false);
  md.finish(digest);
  ZdbSagaID id = 0;
  memcpy(&id, digest.data(), sizeof(id));
  return id ? id : ZdbSagaID{1};
}

// A concrete callback-chain credit. It retains no reference to its owner.
// The last dependent release posts retirement while the owner's credit is
// outstanding. One-shot callbacks consume their handle when invoked; selectors
// release their captured handle on their explicit terminal result.
struct SSFWork : public ZmNHeap<SSFWork>, public ZmObject {
friend inline ZuStringT<"Zum.SSF.Work"> ZmNHeapID(SSFWork *);

  SSFTransmitter *const owner;
  explicit SSFWork(SSFTransmitter *owner_) : owner{owner_} { }
  ~SSFWork() { owner->workDone_(); }
};

bool SSFTransmitter::init(SSFTransmitterConfig config)
{
  if (Engine::state() != ZmEngineState::Stopped || m_config.db ||
      !config.db || !config.requests || !config.issuer ||
      !config.secretIssuer || !config.dbKey || !config.rng ||
      !config.key.id || !config.key.issuer || !config.sign || !config.http ||
      !config.receiverMax || !config.leaseMax || !config.errorMax ||
      !config.retryBase || !config.retryMax)
    return false;
  m_config = ZuMv(config);
  return true;
}

bool SSFTransmitter::active_() const
{
  switch (Engine::state()) {
    case ZmEngineState::Starting:
    case ZmEngineState::StopPending:
    case ZmEngineState::Running: return true;
  }
  return false;
}

bool SSFTransmitter::admit_()
{
  if (Engine::state() != ZmEngineState::Running) return false;
  uint64_t count;
  do {
    count = m_credit.load_();
    if (count & SSFCredit::Closed) return false;
  } while (m_credit.cmpXch(count + 1, count) != count);
  return true;
}

ZmRef<SSFWork> SSFTransmitter::work_()
{
  // Startup owns one graph credit; public inputs reserve their graph through
  // admit_(). Internal stages transfer handles to that existing credit.
  ++m_credit;
  return new SSFWork{this};
}

void SSFTransmitter::workDone_()
{
  m_config.requests->scheduler()->run([this]() {
    --m_credit;
    postDrain_();
  }, m_config.requests->sid());
}

void SSFTransmitter::start_()
{
  if (!m_config.requests) { Engine::started(false); return; }
  m_config.requests->scheduler()->run([this]() {
    ZmAssert_(m_credit.load_() == SSFCredit::Closed && !m_drainPosted);
    m_credit = 0;
    m_draining = false;
    auto work = work_();
    gc_(work);
    retry_(work);
    Engine::started(true);
  }, m_config.requests->sid());
}

void SSFTransmitter::stop_()
{
  m_credit |= SSFCredit::Closed;
  m_config.requests->scheduler()->run([this]() {
    auto scheduler = m_config.requests->scheduler();
    scheduler->del(&m_gcTimer);
    scheduler->del(&m_timer);
    m_gcArmed = m_armed = false;
    m_nextDelivery = 0;
    m_draining = true;
    postDrain_();
  }, m_config.requests->sid());
}

void SSFTransmitter::postDrain_()
{
  if (!m_draining || m_drainPosted) return;
  m_drainPosted = true;
  m_config.requests->scheduler()->run([this]() {
    m_drainPosted = false;
    if (m_credit.load_() & SSFCredit::Count) return;
    m_draining = false;
    Engine::stopped(true);
  }, m_config.requests->sid());
}

void SSFTransmitter::final()
{
  ZmAssert_(Engine::state() == ZmEngineState::Stopped);
  ZmAssert_(!(m_credit.load_() & SSFCredit::Count) && !m_drainPosted && !m_draining);
  ZmAssert_(!m_armed && !m_gcArmed);
  if (m_config.dbKey.mutable_()) ZuClear(m_config.dbKey);
  m_config = {};
}

void SSFTransmitter::add(AppID appID, String clientID,
    SSFRegistration input, SSFRegisterFn complete)
{
  if (!appID || !clientID || !input.receiverID ||
      !ssfURL(input.deliveryURL) || !input.callbackAuth ||
      input.callbackAuth.find<"\r">() >= 0 ||
      input.callbackAuth.find<"\n">() >= 0 || input.expiresIn < 2) {
    complete(400, SSFLease{});
    return;
  }
  if (!admit_()) { complete(503, SSFLease{}); return; }
  ZmRef<SSFWork> work = new SSFWork{this};
  auto apps = m_config.db->apps;
  apps->run(0, [credit = work, apps, appID,
      clientID = ZuMv(clientID), input = ZuMv(input),
      complete = ZuMv(complete)]() mutable {
    auto work = ZuMv(credit);
    apps->find<0>(0, ZuFwdTuple(appID), [credit = work, appID,
        clientID = ZuMv(clientID), input = ZuMv(input),
        complete = ZuMv(complete)](ZdbRowRef<App> app) mutable {
      auto work = ZuMv(credit);
      auto self = work->owner;
      if (!app || app->data().owner || app->data().state != State::Active) {
        complete(403, SSFLease{});
        return;
      }
      auto ttl = input.expiresIn;
      if (ttl > self->m_config.leaseMax) ttl = self->m_config.leaseMax;
      int64_t now = Zm::now().sec();
      String id;
      id << appID << ':' << clientID.length() << ':' << clientID << input.receiverID;
      Bytes envelope;
      bool protected_ = serverSecretEncrypt(*self->m_config.rng,
        self->m_config.dbKey, self->m_config.secretIssuer, "zum.ssf_rx",
        id, "callbackAuth", ZuBSpan{input.callbackAuth}, envelope);
      if (input.callbackAuth.mutable_()) ZuClear(input.callbackAuth);
      uint64_t revision = 0;
      if (!protected_ || !self->m_config.rng->random({
          reinterpret_cast<uint8_t *>(&revision), sizeof(revision)}) || !revision) {
        complete(503, SSFLease{});
        return;
      }
      SSFRx receiver{
        .expires = now + ttl, .appID = appID, .receiverID = id,
        .audience = app->data().audience,
        .deliveryURL = ZuMv(input.deliveryURL),
        .callbackAuth = ZuMv(envelope), .revision = revision, .updated = now};
      auto table = self->m_config.db->ssfRx;
      table->run(0, [credit = work, table, id = ZuMv(id),
          receiver = ZuMv(receiver), ttl, complete = ZuMv(complete)]() mutable {
        auto work = ZuMv(credit);
        table->findUpd<0, ZuSeq<2>>(0, ZuFwdTuple(ZuMv(id)), [credit = work, table,
            receiver = ZuMv(receiver), ttl, complete = ZuMv(complete)](
              ZdbRow<SSFRx> *row) mutable {
          auto work = ZuMv(credit);
          auto self = work->owner;
          SSFLease lease{ttl, receiver.expires};
          if (row) {
            // A renewal of the same destination retains queued SETs. A
            // replacement destination invalidates in-flight completions.
            String before = self->authorization_(row->data());
            String after = self->authorization_(receiver);
            if (before && before == after &&
                receiver.deliveryURL == row->data().deliveryURL &&
                row->data().expires > Zm::now().sec())
              receiver.revision = row->data().revision;
            if (receiver.revision == row->data().revision)
              receiver.errors = row->data().errors;
            if (before.mutable_()) ZuClear(before);
            if (after.mutable_()) ZuClear(after);
            row->data() = ZuMv(receiver);
            bool ok = row->commit();
            if (ok) self->gc_(work);
            complete(ok ? 200 : 503, ok ? lease : SSFLease{});
            return;
          }
          if (table->count() >= self->m_config.receiverMax) {
            self->gc_(work);
            complete(429, SSFLease{});
            return;
          }
          ZdbRowRef<SSFRx> insert = new ZdbRow<SSFRx>{table, ZdbShard{0}};
          table->insert(ZuMv(insert),
            [credit = work, receiver = ZuMv(receiver), lease, complete = ZuMv(complete)](
                ZdbRow<SSFRx> *row) mutable {
            auto work = ZuMv(credit);
            auto self = work->owner;
            if (!row) { complete(503, SSFLease{}); return; }
            new (row->ptr()) SSFRx{ZuMv(receiver)};
            bool ok = row->commit();
            if (ok) self->gc_(work);
            complete(ok ? 200 : 503, ok ? lease : SSFLease{});
          });
        });
      });
    });
  });
}

// Read only the earliest lease. Wake at its deadline, rather than scanning
// receivers periodically; the persisted expiry index also restores GC.
void SSFTransmitter::gc_(const Work &work)
{
  if (!active_()) return;
  auto table = m_config.db->ssfRx;
  table->run(0, [credit = work, table]() mutable {
    auto work = ZuMv(credit);
    table->selectRows<2>({}, 1, [credit = work, table](
        ZuUnion<void, SSFRxTable::Tuple> result, unsigned count) mutable {
      auto work = credit;
      if (result.template is<void>()) credit = nullptr;
      auto self = work->owner;
      if (!result.template is<SSFRxTable::Tuple>()) {
        if (!count) self->gcArm_(work, 0);
        return;
      }
      auto tuple = ZuMv(result).template p<SSFRxTable::Tuple>();
      int64_t expires = tuple.template p<0>();
      if (expires > Zm::now().sec()) { self->gcArm_(work, expires); return; }
      String id = ZuMv(tuple.template p<2>());
      table->run(0, [credit = work, table, id = ZuMv(id)]() mutable {
        auto work = ZuMv(credit);
        table->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [credit = work, table](
            ZdbRow<SSFRx> *row) mutable {
          auto work = ZuMv(credit);
          auto self = work->owner;
          if (!row) { self->gc_(work); return; }
          if (row->data().expires > Zm::now().sec()) {
            row->abort();
            self->gc_(work);
            return;
          }
          ZdbRowRef<SSFRx> hold{row};
          row->abort();
          table->del(ZuMv(hold), [credit = work](ZdbRow<SSFRx> *row) mutable {
            auto work = ZuMv(credit);
            auto self = work->owner;
            if (row && row->commit()) { self->retry_(work); self->gc_(work); }
          });
        });
      });
    });
  });
}

void SSFTransmitter::gcArm_(const Work &work, int64_t expires)
{
  if (!active_()) return;
  auto scheduler = m_config.requests->scheduler();
  scheduler->run([credit = work, expires]() mutable {
    auto work = ZuMv(credit);
    auto self = work->owner;
    if (!self->active_()) return;
    auto scheduler = self->m_config.requests->scheduler();
    if (self->m_gcArmed) scheduler->del(&self->m_gcTimer);
    self->m_gcArmed = bool(expires);
    if (!expires) return;
    scheduler->add(&self->m_gcTimer, ZuTime{expires},
      ZmScheduler::Update, [credit = work](auto &&arm) mutable {
	auto work = ZuMv(credit);
	return arm([credit = work]() mutable {
	  auto work = ZuMv(credit);
	  auto self = work->owner;
	  if (!self->active_()) return;
	  self->m_gcArmed = false;
	  self->gc_(work);
	});
      }, self->m_config.requests->sid());
  }, m_config.requests->sid());
}

// Error accounting belongs to the receiver, across all its notifications.
void SSFTransmitter::result_(const Work &work, String id, uint64_t revision, int status)
{
  auto table = m_config.db->ssfRx;
  table->run(0, [credit = work, table,
      id = ZuMv(id), revision, status]() mutable {
    auto work = ZuMv(credit);
    table->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [credit = work, table, revision, status](
        ZdbRow<SSFRx> *row) mutable {
      auto work = ZuMv(credit);
      auto self = work->owner;
      if (!row) return;
      auto &receiver = row->data();
      if (receiver.revision != revision) { row->abort(); return; }
      if (status == RevokeIssue::OK) receiver.errors = 0;
      else ++receiver.errors;
      if (receiver.expires <= Zm::now().sec() ||
          receiver.errors >= self->m_config.errorMax) {
        ZdbRowRef<SSFRx> hold{row};
        row->abort();
        table->del(ZuMv(hold), [credit = work](ZdbRow<SSFRx> *row) mutable {
          auto work = ZuMv(credit);
          auto self = work->owner;
          if (row && row->commit()) { self->retry_(work); self->gc_(work); }
        });
        return;
      }
      row->commit();
    });
  });
}

void SSFTransmitter::revoke(AppID appID, RefreshID refreshID, int64_t expires)
{
  if (!active_() || !appID || !refreshID.issuerURL || !refreshID.familyID ||
      expires <= 0)
    return;
  if (!admit_()) return;
  ZmRef<SSFWork> work = new SSFWork{this};
  auto scheduler = m_config.requests->scheduler();
  scheduler->run([credit = work, appID,
      refreshID = ZuMv(refreshID), expires]() mutable {
    auto work = ZuMv(credit);
    auto self = work->owner;
    self->revoke_(work, appID, ZuMv(refreshID), expires);
  }, m_config.requests->sid());
}

void SSFTransmitter::revoke_(const Work &work, AppID appID, RefreshID refreshID, int64_t expires)
{
  if (!active_()) return;
  receivers_(work, appID, ZuMv(refreshID), expires);
}

void SSFTransmitter::receivers_(const Work &work,
    AppID appID, RefreshID refreshID, int64_t expires)
{
  auto table = m_config.db->ssfRx;
  table->run(0, [credit = work, table, appID,
      refreshID = ZuMv(refreshID), expires]() mutable {
    auto work = ZuMv(credit);
    auto self = work->owner;
    table->selectRows<1>(ZuFwdTuple(appID), self->m_config.receiverMax,
      [credit = work, refreshID = ZuMv(refreshID), expires](
          ZuUnion<void, SSFRxTable::Tuple> result, unsigned) mutable {
        auto work = credit;
        if (result.template is<void>()) credit = nullptr;
        if (!result.template is<SSFRxTable::Tuple>()) return;
        auto tuple = ZuMv(result).template p<SSFRxTable::Tuple>();
        ZuTupleCall(ZuMv(tuple), [&work, &refreshID, expires](auto &&...args) {
          auto self = work->owner;
          SSFRx receiver{ZuFwd<decltype(args)>(args)...};
          if (receiver.expires <= Zm::now().sec()) { self->gc_(work); return; }
          RefreshID id{refreshID.issuerURL, refreshID.familyID};
          auto scheduler = self->m_config.requests->scheduler();
          auto sid = self->m_config.requests->sid();
          scheduler->run([credit = work, receiver = ZuMv(receiver),
              refreshID = ZuMv(id), expires]() mutable {
            auto work = ZuMv(credit);
            auto self = work->owner;
            if (self->active_())
              self->issue_(work, ZuMv(receiver), ZuMv(refreshID), expires,
                Zm::now().sec());
          }, sid);
        });
      });
  });
}

void SSFTransmitter::issue_(const Work &work, SSFRx receiver, RefreshID refreshID,
    int64_t expires, int64_t now)
{
  String authorization = authorization_(receiver);
  if (!authorization) return;
  String issuer;
  if (m_config.keyLoad) issuer = refreshID.issuerURL;
  auto issue = [credit = work, receiver = ZuMv(receiver),
      refreshID = ZuMv(refreshID), expires, authorization = ZuMv(authorization),
      now](SignKey key) mutable {
    auto work = ZuMv(credit);
    auto self = work->owner;
    key.issuer = refreshID.issuerURL;
    // makeSSF borrows inputs while the completion takes ownership of delivery.
    String audience = receiver.audience;
    RefreshID source{refreshID.issuerURL, refreshID.familyID};
    makeSSF(key, audience, source, expires, now, self->m_config.sign,
      [credit = work, receiver = ZuMv(receiver), refreshID = ZuMv(refreshID),
        expires, authorization = ZuMv(authorization), now](String set) mutable {
        auto work = ZuMv(credit);
        auto self = work->owner;
        if (!set) return;
        String eventID{refreshID.issuerURL};
        eventID << ':' << refreshID.familyID;
        eventID << ':' << now;
        String receiverID = receiver.receiverID;
        uint64_t revision = receiver.revision;
        self->persist_(work, ZuMv(receiver), SSFDelivery{
          .eventID = ZuMv(eventID), .receiverID = ZuMv(receiverID),
          .familyIssuer = refreshID.issuerURL, .familyID = refreshID.familyID,
          .familyExpires = expires, .set = ZuMv(set), .nextDelivery = now,
          .receiverRevision = revision},
          ZuMv(authorization));
      });
  };
  if (m_config.keyLoad)
    m_config.keyLoad(ZuMv(issuer), ZuMv(issue));
  else {
    SignKey key = m_config.key;
    issue(ZuMv(key));
  }
}

void SSFTransmitter::persist_(const Work &work, SSFRx receiver, SSFDelivery delivery,
    String authorization)
{
  auto table = m_config.db->ssfDeliveries;
  if (!table || !m_config.db) return;
  ZdbSagaID sagaID = ssfSagaID(delivery);
  int64_t deadline = delivery.familyExpires;
  auto eventID = delivery.eventID;
  auto receiverID = delivery.receiverID;
  SagaRef saga = ssfDeliveryAdd(ZuMv(delivery));
  if (!sagaSubmit(m_config.db, sagaID, ZuMv(saga), SagaFn{},
      SagaFn{[credit = work, table,
          receiver = ZuMv(receiver), eventID = ZuMv(eventID),
          receiverID = ZuMv(receiverID),
          authorization = ZuMv(authorization)](bool ok) mutable {
        auto work = ZuMv(credit);
        if (!ok) return;
        table->run(0, [credit = work, table,
            receiver = ZuMv(receiver), eventID = ZuMv(eventID),
            receiverID = ZuMv(receiverID),
            authorization = ZuMv(authorization)]() mutable {
          auto work = ZuMv(credit);
          table->find<0>(0, ZuFwdTuple(ZuMv(eventID), ZuMv(receiverID)),
            [credit = work, receiver = ZuMv(receiver),
                authorization = ZuMv(authorization)](
                ZdbRowRef<SSFDelivery> row) mutable {
              auto work = ZuMv(credit);
              auto self = work->owner;
              if (!row) return;
              SSFDelivery delivery{row->data()};
              auto scheduler = self->m_config.requests->scheduler();
              auto sid = self->m_config.requests->sid();
              scheduler->run([credit = work, receiver = ZuMv(receiver),
                  delivery = ZuMv(delivery),
                  authorization = ZuMv(authorization)]() mutable {
                auto work = ZuMv(credit);
                auto self = work->owner;
                if (self->active_())
                  self->deliver_(work, ZuMv(receiver), ZuMv(delivery),
                      ZuMv(authorization));
              }, sid);
            });
        });
      }}, ZuTime{double(deadline)})) return;
}

String SSFTransmitter::authorization_(const SSFRx &receiver)
{
  Bytes plain;
  if (!serverSecretDecrypt(m_config.dbKey, m_config.secretIssuer,
      "zum.ssf_rx", receiver.receiverID, "callbackAuth", receiver.callbackAuth,
      plain)) return {};
  String authorization{ZuCSpan{plain}};
  ZuClear(plain);
  return authorization;
}

void SSFTransmitter::deliver_(const Work &work, SSFRx receiver, SSFDelivery delivery,
    String authorization)
{
  // Recheck the live registration after signing/persistence. Removed or
  // replaced receivers must not be revived by queued deliveries.
  auto table = m_config.db->ssfRx;
  table->run(0, [credit = work, table,
      id = receiver.receiverID, delivery = ZuMv(delivery),
      authorization = ZuMv(authorization)]() mutable {
    auto work = ZuMv(credit);
    table->find<0>(0, ZuFwdTuple(ZuMv(id)), [credit = work, delivery = ZuMv(delivery),
        authorization = ZuMv(authorization)](ZdbRowRef<SSFRx> row) mutable {
      auto work = ZuMv(credit);
      auto self = work->owner;
      if (!row || row->data().revision != delivery.receiverRevision ||
          row->data().expires <= Zm::now().sec()) {
        auto table = self->m_config.db->ssfDeliveries;
        table->run(0, [credit = work, table, delivery = ZuMv(delivery)]() mutable {
          auto work = ZuMv(credit);
          table->findDel<0>(0, ZuFwdTuple(ZuMv(delivery.eventID),
              ZuMv(delivery.receiverID)), [credit = work](ZdbRow<SSFDelivery> *row) mutable {
                auto work = ZuMv(credit);
                (void)work; // Retain the DB stage through its completion.
                if (row) row->commit();
              });
        });
        if (authorization.mutable_()) ZuClear(authorization);
        return;
      }
      SSFRx receiver = row->data();
      auto scheduler = self->m_config.requests->scheduler();
      auto sid = self->m_config.requests->sid();
      scheduler->run([credit = work, receiver = ZuMv(receiver), delivery = ZuMv(delivery),
          authorization = ZuMv(authorization)]() mutable {
        auto work = ZuMv(credit);
        auto self = work->owner;
        if (self->active_())
          self->send_(work, ZuMv(receiver), ZuMv(delivery), ZuMv(authorization));
      }, sid);
    });
  });
}

void SSFTransmitter::send_(const Work &work, SSFRx receiver, SSFDelivery delivery,
    String authorization)
{
  if (receiver.expires <= Zm::now().sec()) { gc_(work); retry_(work); return; }
  uint64_t revision = receiver.revision;
  unsigned errors = receiver.errors < 31 ? receiver.errors : 31;
  uint64_t delay = uint64_t(m_config.retryBase)<<errors;
  if (delay > m_config.retryMax) delay = m_config.retryMax;
  String eventID = delivery.eventID;
  String receiverID = delivery.receiverID;
  sendSSF(ZuMv(receiver), ZuMv(delivery), ZuMv(authorization), m_config.http,
    [credit = work, eventID = ZuMv(eventID),
        receiverID = ZuMv(receiverID), revision, delay](int status) mutable {
      auto work = ZuMv(credit);
      auto self = work->owner;
      self->result_(work, receiverID, revision, status);
      auto table = self->m_config.db->ssfDeliveries;
      if (!table) return;
      table->run(0, [credit = work, table, eventID = ZuMv(eventID),
          receiverID = ZuMv(receiverID), status, delay]() mutable {
        auto work = ZuMv(credit);
        if (status == RevokeIssue::OK) {
          // A successful RFC 8935 response is the terminal outbox
          // transition.  The insert saga has already completed; never keep a
          // database saga open across this network operation.
          table->findDel<0>(0, ZuFwdTuple(ZuMv(eventID), ZuMv(receiverID)),
            [credit = work](ZdbRow<SSFDelivery> *row) mutable {
              auto work = ZuMv(credit);
              (void)work; // Retain the DB stage through its completion.
              if (row) row->commit();
            });
          return;
        }
        table->findUpd<0>(0, ZuFwdTuple(ZuMv(eventID), ZuMv(receiverID)),
          [credit = work, table, delay](ZdbRow<SSFDelivery> *row) mutable {
            auto work = ZuMv(credit);
            auto self = work->owner;
            if (!row) return;
            auto &item = row->data();
            int64_t now = Zm::now().sec();
            if (item.familyExpires <= now) {
              String expiredEvent = item.eventID;
              String expiredReceiver = item.receiverID;
              row->abort();
              table->findDel<0>(0, ZuFwdTuple(ZuMv(expiredEvent),
                  ZuMv(expiredReceiver)), [credit = work](ZdbRow<SSFDelivery> *row) mutable {
                    auto work = ZuMv(credit);
                    (void)work; // Retain the DB stage through its completion.
                    if (row) row->commit();
                  });
              return;
            }
            item.nextDelivery = now + int64_t(delay);
            row->commit();
            self->arm_(work, item.nextDelivery);
          });
      });
    });
}

void SSFTransmitter::retry_(const Work &work)
{
  retryPage_(work, {}, true);
}

void SSFTransmitter::retryPage_(const Work &work, SSFDeliveryTable::Key<0> key, bool first)
{
  if (!active_() || !m_config.db->ssfDeliveries) return;
  auto table = m_config.db->ssfDeliveries;
  table->run(0, [credit = work, table,
      key = ZuMv(key), first]() mutable {
    auto work = ZuMv(credit);
    auto self = work->owner;
    int64_t now = Zm::now().sec();
    int64_t next = 0;
    auto receive = [credit = work, table, now, next, key = ZuMv(key)](
        ZuUnion<void, SSFDeliveryTable::Tuple> result, unsigned count) mutable {
      auto work = credit;
      if (result.template is<void>()) credit = nullptr;
      auto self = work->owner;
      if (!result.template is<SSFDeliveryTable::Tuple>()) {
        if (count == self->m_config.receiverMax) {
          auto scheduler = self->m_config.requests->scheduler();
          auto sid = self->m_config.requests->sid();
          scheduler->run([credit = work, key = ZuMv(key)]() mutable {
            auto work = ZuMv(credit);
            auto self = work->owner;
            if (self->active_()) self->retryPage_(work, ZuMv(key), false);
          }, sid);
        } else if (next) self->arm_(work, next);
        return;
      }
      auto tuple = ZuMv(result).template p<SSFDeliveryTable::Tuple>();
      key = ZuStructKey<0>(tuple);
      int64_t due = tuple.template p<6>();
      if (tuple.template p<5>().length() == 0 ||
          tuple.template p<4>() <= now) {
        String eventID = tuple.template p<0>();
        String receiverID = tuple.template p<1>();
        table->run(0, [credit = work, table, eventID = ZuMv(eventID),
            receiverID = ZuMv(receiverID)]() mutable {
          auto work = ZuMv(credit);
          table->findDel<0>(0, ZuFwdTuple(ZuMv(eventID), ZuMv(receiverID)),
            [credit = work](ZdbRow<SSFDelivery> *row) mutable {
              auto work = ZuMv(credit);
              (void)work; // Retain the DB stage through its completion.
              if (row) row->commit();
            });
        });
        return;
      }
      if (due > now) { if (!next || due < next) next = due; return; }
      String receiverID = tuple.template p<1>();
      auto rx = self->m_config.db->ssfRx;
      if (!rx) {
        self->arm_(work, Zm::now().sec() + self->m_config.retryBase);
        return;
      }
      rx->run(0, [credit = work, rx, receiverID = ZuMv(receiverID),
          tuple = ZuMv(tuple)]() mutable {
        auto work = ZuMv(credit);
        rx->find<0>(0, ZuFwdTuple(ZuMv(receiverID)),
          [credit = work, tuple = ZuMv(tuple)](ZdbRowRef<SSFRx> row) mutable {
            auto work = ZuMv(credit);
            auto self = work->owner;
            if (!row || row->data().expires <= Zm::now().sec()) {
              auto table = self->m_config.db->ssfDeliveries;
              table->run(0, [credit = work, table, tuple = ZuMv(tuple)]() mutable {
                auto work = ZuMv(credit);
                table->findDel<0>(0, ZuFwdTuple(ZuMv(tuple.template p<0>()),
                    ZuMv(tuple.template p<1>())),
                    [credit = work](ZdbRow<SSFDelivery> *row) mutable {
                      auto work = ZuMv(credit);
                      (void)work; // Retain the DB stage through its completion.
                      if (row) row->commit();
                    });
              });
              self->gc_(work);
              return;
            }
            SSFDelivery delivery{
              .eventID = ZuMv(tuple.template p<0>()),
              .receiverID = ZuMv(tuple.template p<1>()),
              .familyIssuer = ZuMv(tuple.template p<2>()),
              .familyID = ZuMv(tuple.template p<3>()),
              .familyExpires = tuple.template p<4>(),
              .set = ZuMv(tuple.template p<5>()),
              .nextDelivery = tuple.template p<6>(),
              .receiverRevision = tuple.template p<7>()};
            SSFRx receiver = row->data();
            auto scheduler = self->m_config.requests->scheduler();
            auto sid = self->m_config.requests->sid();
            scheduler->run([credit = work, receiver = ZuMv(receiver),
                delivery = ZuMv(delivery)]() mutable {
              auto work = ZuMv(credit);
              auto self = work->owner;
              if (!self->active_()) return;
              String authorization =
                self->authorization_(receiver);
              if (authorization)
                self->deliver_(work, ZuMv(receiver), ZuMv(delivery),
                    ZuMv(authorization));
              else
                self->arm_(work, Zm::now().sec() + self->m_config.retryBase);
            }, sid);
          });
      });
    };
    if (first)
      table->selectRows<0>({}, self->m_config.receiverMax, ZuMv(receive));
    else
      table->nextRows<0>(ZuMv(key), false, self->m_config.receiverMax,
          ZuMv(receive));
  });
}

void SSFTransmitter::arm_(const Work &work, int64_t when)
{
  if (!active_() || !when) return;
  auto scheduler = m_config.requests->scheduler();
  scheduler->run([credit = work, when]() mutable {
    auto work = ZuMv(credit);
    auto self = work->owner;
    self->armOn_(work, when);
  }, m_config.requests->sid());
}

void SSFTransmitter::armOn_(const Work &work, int64_t when)
{
  if (!active_() || !when) return;
  auto scheduler = m_config.requests->scheduler();
  if (m_armed && m_nextDelivery <= when) return;
  if (m_armed) scheduler->del(&m_timer);
  m_nextDelivery = when;
  m_armed = true;
  scheduler->add(&m_timer, ZuTime{when},
    ZmScheduler::Update, [credit = work](auto &&arm) mutable {
      auto work = ZuMv(credit);
      return arm([credit = work]() mutable {
	auto work = ZuMv(credit);
	auto self = work->owner;
	if (!self->active_()) return;
	self->m_armed = false;
	self->m_nextDelivery = 0;
	self->retry_(work);
      });
    }, m_config.requests->sid());
}


} // namespace Zum
