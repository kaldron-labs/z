//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_revoke.hh>
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

template <typename Heap>
bool SSFTransmitter_<Heap>::init(SSFTransmitterConfig config)
{
  if (m_started || !config.db || !config.context || !config.requests || !config.issuer ||
      !config.secretIssuer || !config.dbKey || !config.rng || !config.key.id || !config.key.issuer || !config.sign || !config.http ||
      !config.receiverMax || !config.leaseMax || !config.errorMax ||
      !config.retryBase || !config.retryMax)
    return false;
  m_config = ZuMv(config);
  return true;
}

template <typename Heap>
void SSFTransmitter_<Heap>::start()
{
  if (m_started || !m_config.context) return;
  m_started = true;
  auto scheduler = m_config.requests->scheduler();
  scheduler->run([self = ZmRef<SSFTransmitter>{this}]() mutable {
    self->gc_();
    self->retry_();
  }, m_config.requests->sid());
}

template <typename Heap>
void SSFTransmitter_<Heap>::add(AppID appID, String clientID,
    SSFRegistration input, SSFRegisterFn complete)
{
  if (!appID || !clientID || !input.receiverID ||
      !ssfURL(input.deliveryURL) || !input.callbackAuth ||
      input.callbackAuth.find<"\r">() >= 0 ||
      input.callbackAuth.find<"\n">() >= 0 || input.expiresIn < 2) {
    complete(400, SSFLease{});
    return;
  }
  auto apps = m_config.context->apps;
  apps->run(0, [self = ZmRef<SSFTransmitter>{this}, apps, appID,
      clientID = ZuMv(clientID), input = ZuMv(input),
      complete = ZuMv(complete)]() mutable {
    apps->find<0>(0, ZuFwdTuple(appID), [self = ZuMv(self), appID,
        clientID = ZuMv(clientID), input = ZuMv(input),
        complete = ZuMv(complete)](ZdbRowRef<App> app) mutable {
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
      SSFRx receiver{.expires = now + ttl, .appID = appID, .receiverID = id, .audience = app->data().audience,
        .deliveryURL = ZuMv(input.deliveryURL),
        .callbackAuth = ZuMv(envelope), .revision = revision, .updated = now};
      auto table = self->m_config.context->ssfRx;
      table->run(0, [self = ZuMv(self), table, id = ZuMv(id),
          receiver = ZuMv(receiver), ttl, complete = ZuMv(complete)]() mutable {
        table->findUpd<0, ZuSeq<2>>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self), table,
            receiver = ZuMv(receiver), ttl, complete = ZuMv(complete)](
              ZdbRow<SSFRx> *row) mutable {
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
            if (ok) self->gc_();
            complete(ok ? 200 : 503, ok ? lease : SSFLease{});
            return;
          }
          if (table->count() >= self->m_config.receiverMax) {
            self->gc_();
            complete(429, SSFLease{});
            return;
          }
          ZdbRowRef<SSFRx> insert = new ZdbRow<SSFRx>{table, ZdbShard{0}};
          table->insert(ZuMv(insert), [self = ZuMv(self),
              receiver = ZuMv(receiver), lease, complete = ZuMv(complete)](
                ZdbRow<SSFRx> *row) mutable {
            if (!row) { complete(503, SSFLease{}); return; }
            new (row->ptr()) SSFRx{ZuMv(receiver)};
            bool ok = row->commit();
            if (ok) self->gc_();
            complete(ok ? 200 : 503, ok ? lease : SSFLease{});
          });
        });
      });
    });
  });
}

// Read only the earliest lease. Wake at its deadline, rather than scanning
// receivers periodically; the persisted expiry index also restores GC.
template <typename Heap>
void SSFTransmitter_<Heap>::gc_()
{
  auto table = m_config.context->ssfRx;
  table->run(0, [self = ZmRef<SSFTransmitter>{this}, table]() mutable {
    table->selectRows<2>({}, 1, [self, table](
        ZuUnion<void, SSFRxTable::Tuple> result, unsigned count) mutable {
      if (!result.template is<SSFRxTable::Tuple>()) {
        if (!count) self->gcArm_(0);
        return;
      }
      auto tuple = ZuMv(result).template p<SSFRxTable::Tuple>();
      int64_t expires = tuple.template p<0>();
      if (expires > Zm::now().sec()) { self->gcArm_(expires); return; }
      String id = ZuMv(tuple.template p<2>());
      table->run(0, [self, table, id = ZuMv(id)]() mutable {
        table->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [self, table](
            ZdbRow<SSFRx> *row) mutable {
          if (!row) { self->gc_(); return; }
          if (row->data().expires > Zm::now().sec()) {
            row->abort();
            self->gc_();
            return;
          }
          ZdbRowRef<SSFRx> hold{row};
          row->abort();
          table->del(ZuMv(hold), [self](ZdbRow<SSFRx> *row) mutable {
            if (row && row->commit()) { self->retry_(); self->gc_(); }
          });
        });
      });
    });
  });
}

template <typename Heap>
void SSFTransmitter_<Heap>::gcArm_(int64_t expires)
{
  auto scheduler = m_config.requests->scheduler();
  scheduler->run([self = ZmRef<SSFTransmitter>{this}, expires]() mutable {
    if (!self->m_started) return;
    auto scheduler = self->m_config.requests->scheduler();
    if (self->m_gcArmed) scheduler->del(&self->m_gcTimer);
    self->m_gcArmed = bool(expires);
    if (!expires) return;
    scheduler->add(&self->m_gcTimer, ZuTime{double(expires)},
      ZmScheduler::Update, [self](auto &&arm) mutable {
        return arm([self]() mutable { self->m_gcArmed = false; self->gc_(); });
      }, self->m_config.requests->sid());
  }, m_config.requests->sid());
}

// Error accounting belongs to the receiver, across all its notifications.
template <typename Heap>
void SSFTransmitter_<Heap>::result_(String id, uint64_t revision, int status)
{
  auto table = m_config.context->ssfRx;
  table->run(0, [self = ZmRef<SSFTransmitter>{this}, table,
      id = ZuMv(id), revision, status]() mutable {
    table->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [self, table, revision, status](
        ZdbRow<SSFRx> *row) mutable {
      if (!row) return;
      auto &receiver = row->data();
      if (receiver.revision != revision) { row->abort(); return; }
      if (status == RevokeIssue::OK) receiver.errors = 0;
      else ++receiver.errors;
      if (receiver.expires <= Zm::now().sec() ||
          receiver.errors >= self->m_config.errorMax) {
        ZdbRowRef<SSFRx> hold{row};
        row->abort();
        table->del(ZuMv(hold), [self](ZdbRow<SSFRx> *row) mutable {
          if (row && row->commit()) { self->retry_(); self->gc_(); }
        });
        return;
      }
      row->commit();
    });
  });
}

template <typename Heap>
void SSFTransmitter_<Heap>::stop()
{
  if (!m_started) return;
  m_started = false;
  m_nextDelivery = 0;
  if (m_gcArmed && m_config.requests) {
    m_config.requests->scheduler()->del(&m_gcTimer);
    m_gcArmed = false;
  }
  if (m_armed && m_config.requests) {
    m_config.requests->scheduler()->del(&m_timer);
    m_armed = false;
  }
}

template <typename Heap>
void SSFTransmitter_<Heap>::revoke(AppID appID, RefreshID refreshID, int64_t expires)
{
  if (!m_started || !appID || !refreshID.issuerURL || !refreshID.familyID ||
      expires <= 0)
    return;
  auto scheduler = m_config.requests->scheduler();
  scheduler->run([self = ZmRef<SSFTransmitter>{this}, appID,
      refreshID = ZuMv(refreshID), expires]() mutable {
    self->revoke_(appID, ZuMv(refreshID), expires);
  }, m_config.requests->sid());
}

template <typename Heap>
void SSFTransmitter_<Heap>::revoke_(AppID appID, RefreshID refreshID, int64_t expires)
{
  receivers_(appID, ZuMv(refreshID), expires);
}

template <typename Heap>
void SSFTransmitter_<Heap>::receivers_(AppID appID, RefreshID refreshID, int64_t expires)
{
  auto table = m_config.context->ssfRx;
  table->run(0, [self = ZmRef<SSFTransmitter>{this}, table, appID,
      refreshID = ZuMv(refreshID), expires]() mutable {
    table->selectRows<1>(ZuFwdTuple(appID), self->m_config.receiverMax,
      [self, refreshID = ZuMv(refreshID), expires](
          ZuUnion<void, SSFRxTable::Tuple> result, unsigned) mutable {
        if (!result.template is<SSFRxTable::Tuple>()) return;
        auto tuple = ZuMv(result).template p<SSFRxTable::Tuple>();
        ZuTupleCall(ZuMv(tuple), [self, &refreshID, expires](auto &&...args) {
          SSFRx receiver{ZuFwd<decltype(args)>(args)...};
          if (receiver.expires <= Zm::now().sec()) { self->gc_(); return; }
          RefreshID id{refreshID.issuerURL, refreshID.familyID};
          auto scheduler = self->m_config.requests->scheduler();
          auto sid = self->m_config.requests->sid();
          scheduler->run([self, receiver = ZuMv(receiver),
              refreshID = ZuMv(id), expires]() mutable {
            if (self->m_started)
              self->issue_(ZuMv(receiver), ZuMv(refreshID), expires,
                Zm::now().sec());
          }, sid);
        });
      });
  });
}

template <typename Heap>
void SSFTransmitter_<Heap>::issue_(SSFRx receiver, RefreshID refreshID,
    int64_t expires, int64_t now)
{
  String authorization = authorization_(receiver);
  if (!authorization) return;
  auto issue = [self = ZmRef<SSFTransmitter>{this}, receiver = ZuMv(receiver),
      refreshID = ZuMv(refreshID), expires, authorization = ZuMv(authorization),
      now](SignKey key) mutable {
    key.issuer = refreshID.issuerURL;
    makeSSF(key, receiver.audience, refreshID, expires, now, self->m_config.sign,
      [self = ZuMv(self), receiver = ZuMv(receiver), refreshID = ZuMv(refreshID),
        expires, authorization = ZuMv(authorization), now](String set) mutable {
        if (!set) return;
        String eventID{refreshID.issuerURL};
        eventID << ':' << refreshID.familyID;
        eventID << ':' << now;
        String receiverID = receiver.receiverID;
        uint64_t revision = receiver.revision;
        self->persist_(ZuMv(receiver), SSFDelivery{
          .eventID = ZuMv(eventID), .receiverID = ZuMv(receiverID),
          .familyIssuer = refreshID.issuerURL, .familyID = refreshID.familyID,
          .familyExpires = expires, .set = ZuMv(set), .nextDelivery = now,
          .receiverRevision = revision},
          ZuMv(authorization));
      });
  };
  if (m_config.keyLoad)
    m_config.keyLoad(refreshID.issuerURL, ZuMv(issue));
  else {
    SignKey key = m_config.key;
    issue(ZuMv(key));
  }
}

template <typename Heap>
void SSFTransmitter_<Heap>::persist_(SSFRx receiver, SSFDelivery delivery,
    String authorization)
{
  auto table = m_config.context->ssfDeliveries;
  if (!table || !m_config.db) return;
  ZdbSagaID sagaID = ssfSagaID(delivery);
  int64_t deadline = delivery.familyExpires;
  auto eventID = delivery.eventID;
  auto receiverID = delivery.receiverID;
  ZmRef<MSaga> saga = new MSaga{};
  SSFDeliveryAdd add;
  add.delivery = ZuMv(delivery);
  saga->init(ZuMv(add));
  if (!sagaSubmit(m_config.db, sagaID, ZuMv(saga), SagaFn{},
      SagaFn{[self = ZmRef<SSFTransmitter>{this}, table,
          receiver = ZuMv(receiver), eventID = ZuMv(eventID),
          receiverID = ZuMv(receiverID),
          authorization = ZuMv(authorization)](bool ok) mutable {
        if (!ok) return;
        table->run(0, [self = ZuMv(self), table,
            receiver = ZuMv(receiver), eventID = ZuMv(eventID),
            receiverID = ZuMv(receiverID),
            authorization = ZuMv(authorization)]() mutable {
          table->find<0>(0, ZuFwdTuple(ZuMv(eventID), ZuMv(receiverID)),
            [self = ZuMv(self), receiver = ZuMv(receiver),
                authorization = ZuMv(authorization)](
                ZdbRowRef<SSFDelivery> row) mutable {
              if (!row) return;
              SSFDelivery delivery{row->data()};
              auto scheduler = self->m_config.requests->scheduler();
              auto sid = self->m_config.requests->sid();
              scheduler->run([self = ZuMv(self), receiver = ZuMv(receiver),
                  delivery = ZuMv(delivery),
                  authorization = ZuMv(authorization)]() mutable {
                if (self->m_started)
                  self->deliver_(ZuMv(receiver), ZuMv(delivery),
                      ZuMv(authorization));
              }, sid);
            });
        });
      }}, ZuTime{double(deadline)})) return;
}

template <typename Heap>
String SSFTransmitter_<Heap>::authorization_(const SSFRx &receiver)
{
  Bytes plain;
  if (!serverSecretDecrypt(m_config.dbKey, m_config.secretIssuer,
      "zum.ssf_rx", receiver.receiverID, "callbackAuth", receiver.callbackAuth,
      plain)) return {};
  String authorization{ZuCSpan{plain}};
  ZuClear(plain);
  return authorization;
}

template <typename Heap>
void SSFTransmitter_<Heap>::deliver_(SSFRx receiver, SSFDelivery delivery,
    String authorization)
{
  // Recheck the live registration after signing/persistence. Removed or
  // replaced receivers must not be revived by queued deliveries.
  auto table = m_config.context->ssfRx;
  table->run(0, [self = ZmRef<SSFTransmitter>{this}, table,
      id = receiver.receiverID, delivery = ZuMv(delivery),
      authorization = ZuMv(authorization)]() mutable {
    table->find<0>(0, ZuFwdTuple(ZuMv(id)), [self, delivery = ZuMv(delivery),
        authorization = ZuMv(authorization)](ZdbRowRef<SSFRx> row) mutable {
      if (!row || row->data().revision != delivery.receiverRevision ||
          row->data().expires <= Zm::now().sec()) {
        auto table = self->m_config.context->ssfDeliveries;
        table->run(0, [table, delivery = ZuMv(delivery)]() mutable {
          table->findDel<0>(0, ZuFwdTuple(ZuMv(delivery.eventID),
              ZuMv(delivery.receiverID)), [](ZdbRow<SSFDelivery> *row) { if (row) row->commit(); });
        });
        if (authorization.mutable_()) ZuClear(authorization);
        return;
      }
      SSFRx receiver = row->data();
      auto scheduler = self->m_config.requests->scheduler();
      auto sid = self->m_config.requests->sid();
      scheduler->run([self, receiver = ZuMv(receiver), delivery = ZuMv(delivery),
          authorization = ZuMv(authorization)]() mutable {
        if (self->m_started)
          self->send_(ZuMv(receiver), ZuMv(delivery), ZuMv(authorization));
      }, sid);
    });
  });
}

template <typename Heap>
void SSFTransmitter_<Heap>::send_(SSFRx receiver, SSFDelivery delivery,
    String authorization)
{
  if (receiver.expires <= Zm::now().sec()) { gc_(); retry_(); return; }
  uint64_t revision = receiver.revision;
  unsigned errors = receiver.errors < 31 ? receiver.errors : 31;
  uint64_t delay = uint64_t(m_config.retryBase)<<errors;
  if (delay > m_config.retryMax) delay = m_config.retryMax;
  String eventID = delivery.eventID;
  String receiverID = delivery.receiverID;
  sendSSF(ZuMv(receiver), ZuMv(delivery), ZuMv(authorization), m_config.http,
    [self = ZmRef<SSFTransmitter>{this}, eventID = ZuMv(eventID),
        receiverID = ZuMv(receiverID), revision, delay](int status) mutable {
      self->result_(receiverID, revision, status);
      auto table = self->m_config.context->ssfDeliveries;
      if (!table) return;
      table->run(0, [self = ZuMv(self), table, eventID = ZuMv(eventID),
          receiverID = ZuMv(receiverID), status, delay]() mutable {
        if (status == RevokeIssue::OK) {
          // A successful RFC 8935 response is the terminal outbox
          // transition.  The insert saga has already completed; never keep a
          // database saga open across this network operation.
          table->findDel<0>(0, ZuFwdTuple(ZuMv(eventID), ZuMv(receiverID)),
            [](ZdbRow<SSFDelivery> *row) { if (row) row->commit(); });
          return;
        }
        table->findUpd<0>(0, ZuFwdTuple(ZuMv(eventID), ZuMv(receiverID)),
          [self = ZuMv(self), table, delay](ZdbRow<SSFDelivery> *row) mutable {
            if (!row) return;
            auto &item = row->data();
            int64_t now = Zm::now().sec();
            if (item.familyExpires <= now) {
              String expiredEvent = item.eventID;
              String expiredReceiver = item.receiverID;
              row->abort();
              table->findDel<0>(0, ZuFwdTuple(ZuMv(expiredEvent),
                  ZuMv(expiredReceiver)), [](ZdbRow<SSFDelivery> *row) { if (row) row->commit(); });
              return;
            }
            item.nextDelivery = now + int64_t(delay);
            row->commit();
            self->arm_(item.nextDelivery);
          });
      });
    });
}

template <typename Heap>
void SSFTransmitter_<Heap>::retry_()
{
  retryPage_({}, true);
}

template <typename Heap>
void SSFTransmitter_<Heap>::retryPage_(SSFDeliveryTable::Key<0> key, bool first)
{
  if (!m_started || !m_config.context->ssfDeliveries) return;
  auto table = m_config.context->ssfDeliveries;
  table->run(0, [self = ZmRef<SSFTransmitter>{this}, table,
      key = ZuMv(key), first]() mutable {
    int64_t now = Zm::now().sec();
    int64_t next = 0;
    auto receive = [self, table, now, next, key = ZuMv(key)](
        ZuUnion<void, SSFDeliveryTable::Tuple> result, unsigned count) mutable {
      if (!result.template is<SSFDeliveryTable::Tuple>()) {
        if (count == self->m_config.receiverMax) {
          auto scheduler = self->m_config.requests->scheduler();
          auto sid = self->m_config.requests->sid();
          scheduler->run([self, key = ZuMv(key)]() mutable {
            if (self->m_started) self->retryPage_(ZuMv(key), false);
          }, sid);
        } else if (next) self->arm_(next);
        return;
      }
      auto tuple = ZuMv(result).template p<SSFDeliveryTable::Tuple>();
      key = ZuStructKey<0>(tuple);
      int64_t due = tuple.template p<6>();
      if (tuple.template p<5>().length() == 0 ||
          tuple.template p<4>() <= now) {
        String eventID = tuple.template p<0>();
        String receiverID = tuple.template p<1>();
        table->run(0, [table, eventID = ZuMv(eventID),
            receiverID = ZuMv(receiverID)]() mutable {
          table->findDel<0>(0, ZuFwdTuple(ZuMv(eventID), ZuMv(receiverID)),
            [](ZdbRow<SSFDelivery> *row) { if (row) row->commit(); });
        });
        return;
      }
      if (due > now) { if (!next || due < next) next = due; return; }
      String receiverID = tuple.template p<1>();
      auto rx = self->m_config.context->ssfRx;
      if (!rx) {
        self->arm_(Zm::now().sec() + self->m_config.retryBase);
        return;
      }
      rx->run(0, [self, rx, receiverID = ZuMv(receiverID),
          tuple = ZuMv(tuple)]() mutable {
        rx->find<0>(0, ZuFwdTuple(ZuMv(receiverID)),
          [self, tuple = ZuMv(tuple)](ZdbRowRef<SSFRx> row) mutable {
            if (!row || row->data().expires <= Zm::now().sec()) {
              auto table = self->m_config.context->ssfDeliveries;
              table->run(0, [table, tuple = ZuMv(tuple)]() mutable {
                table->findDel<0>(0, ZuFwdTuple(ZuMv(tuple.template p<0>()),
                    ZuMv(tuple.template p<1>())), [](ZdbRow<SSFDelivery> *row) { if (row) row->commit(); });
              });
              self->gc_();
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
            scheduler->run([self, receiver = ZuMv(receiver),
                delivery = ZuMv(delivery)]() mutable {
              if (!self->m_started) return;
              String authorization =
                self->authorization_(receiver);
              if (authorization)
                self->deliver_(ZuMv(receiver), ZuMv(delivery),
                    ZuMv(authorization));
              else
                self->arm_(Zm::now().sec() + self->m_config.retryBase);
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

template <typename Heap>
void SSFTransmitter_<Heap>::arm_(int64_t when)
{
  if (!m_started || !when) return;
  auto scheduler = m_config.requests->scheduler();
  scheduler->run([self = ZmRef<SSFTransmitter>{this}, when]() mutable {
    self->armOn_(when);
  }, m_config.requests->sid());
}

template <typename Heap>
void SSFTransmitter_<Heap>::armOn_(int64_t when)
{
  if (!m_started || !when) return;
  auto scheduler = m_config.requests->scheduler();
  if (m_armed && m_nextDelivery <= when) return;
  if (m_armed) scheduler->del(&m_timer);
  m_nextDelivery = when;
  m_armed = true;
  int64_t now = Zm::now().sec();
  scheduler->add(&m_timer, Zm::now() + ZuTime{double(when > now ? when - now : 0)},
    ZmScheduler::Update, [this](auto &&arm) {
      return arm([this]() { m_armed = false; m_nextDelivery = 0; retry_(); });
    }, m_config.requests->sid());
}

template <typename Heap>
void SSFTransmitter_<Heap>::clear_() { stop(); }

template class SSFTransmitter_<SSFTransmitterHeap>;

} // namespace Zum
