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

bool SSFTransmitter::init(SSFTransmitterConfig config)
{
  if (m_started || !config.db || !config.context || !config.requests || !config.issuer ||
      !config.key.id || !config.key.issuer || !config.sign || !config.http ||
      !config.secret || !config.receiverMax || !config.retryBase ||
      !config.retryMax || config.receivers.length() > config.receiverMax)
    return false;
  for (const auto &receiver: config.receivers)
    if (!receiver.receiverID || !receiver.appID || !receiver.audience ||
        !receiver.deliveryURL || !ssfURL(receiver.deliveryURL) ||
        !receiver.secretRef || !receiver.revision)
      return false;
  m_config = ZuMv(config);
  return true;
}

void SSFTransmitter::start()
{
  if (m_started || !m_config.context) return;
  m_started = true;
  auto scheduler = m_config.requests->scheduler();
  scheduler->run([self = ZmRef<SSFTransmitter>{this}]() mutable {
    self->configure_(0);
  }, m_config.requests->sid());
}

void SSFTransmitter::configure_(unsigned index)
{
  auto table = m_config.context->ssfRx;
  if (!table || index >= m_config.receivers.length()) {
    auto scheduler = m_config.requests->scheduler();
    scheduler->run([self = ZmRef<SSFTransmitter>{this}]() mutable {
      if (self->m_started) self->retry_();
    }, m_config.requests->sid());
    return;
  }
  SSFRx receiver = ZuMv(m_config.receivers[index]);
  String receiverID = receiver.receiverID;
  table->run(0, [self = ZmRef<SSFTransmitter>{this}, table,
      receiver = ZuMv(receiver), receiverID = ZuMv(receiverID), index]() mutable {
    table->findUpd<0>(0, ZuFwdTuple(ZuMv(receiverID)),
          [self = ZuMv(self), table, receiver = ZuMv(receiver), index](
              ZdbRow<SSFRx> *row) mutable {
        if (row) {
          row->data() = ZuMv(receiver);
          if (!row->commit()) {
            self->configure_(index + 1);
            return;
          }
          self->configure_(index + 1);
          return;
        }
        ZdbRowRef<SSFRx> insert =
          new ZdbRow<SSFRx>{table, ZdbShard{0}};
        table->insert(ZuMv(insert),
          [self = ZuMv(self), receiver = ZuMv(receiver), index](
              ZdbRow<SSFRx> *row) mutable {
            if (!row) { self->configure_(index + 1); return; }
            new (row->ptr()) SSFRx{ZuMv(receiver)};
            if (!row->commit()) {
              self->configure_(index + 1);
              return;
            }
            self->configure_(index + 1);
          });
      });
  });
}

void SSFTransmitter::stop()
{
  if (!m_started) return;
  m_started = false;
  if (m_armed && m_config.requests) {
    m_config.requests->scheduler()->del(&m_timer);
    m_armed = false;
  }
}

void SSFTransmitter::revoke(AppID appID, RefreshID refreshID, int64_t expires)
{
  if (!m_started || !appID || !refreshID.issuer || !refreshID.familyID ||
      expires <= 0)
    return;
  auto scheduler = m_config.requests->scheduler();
  scheduler->run([self = ZmRef<SSFTransmitter>{this}, appID,
      refreshID = ZuMv(refreshID), expires]() mutable {
    self->revoke_(appID, ZuMv(refreshID), expires);
  }, m_config.requests->sid());
}

void SSFTransmitter::revoke_(AppID appID, RefreshID refreshID, int64_t expires)
{
  receivers_(appID, ZuMv(refreshID), expires);
}

void SSFTransmitter::receivers_(AppID appID, RefreshID refreshID, int64_t expires)
{
  // Configuration is already validated and owned by this transmitter.  Use
  // it for the live fan-out so a revoke arriving while SSFRx rows are being
  // restored cannot be lost; the rows remain the durable retry source.
  auto scheduler = m_config.requests->scheduler();
  auto sid = m_config.requests->sid();
  unsigned count = 0;
  for (const auto &configured: m_config.receivers) {
    if (configured.appID != appID) continue;
    if (count++ == m_config.receiverMax) break;
    SSFRx receiver = configured;
    RefreshID id{refreshID.issuer, refreshID.familyID};
    scheduler->run([self = ZmRef<SSFTransmitter>{this},
        receiver = ZuMv(receiver), refreshID = ZuMv(id), expires]() mutable {
      if (self->m_started)
        self->issue_(ZuMv(receiver), ZuMv(refreshID), expires,
            Zm::now().sec());
    }, sid);
  }
}

void SSFTransmitter::issue_(SSFRx receiver, RefreshID refreshID,
    int64_t expires, int64_t now)
{
  String authorization = m_config.secret(ZuMv(receiver.secretRef));
  if (!authorization) return;
  auto issue = [self = ZmRef<SSFTransmitter>{this}, receiver = ZuMv(receiver),
      refreshID = ZuMv(refreshID), expires, authorization = ZuMv(authorization),
      now](SignKey key) mutable {
    key.issuer = refreshID.issuer;
    makeSSF(key, receiver.audience, refreshID, expires, now, self->m_config.sign,
      [self = ZuMv(self), receiver = ZuMv(receiver), refreshID = ZuMv(refreshID),
        expires, authorization = ZuMv(authorization), now](String set) mutable {
        if (!set) return;
        String eventID{refreshID.issuer};
        eventID << ':' << refreshID.familyID;
        eventID << ':' << now;
        self->persist_(ZuMv(receiver), SSFDelivery{
          .eventID = ZuMv(eventID), .receiverID = receiver.receiverID,
          .familyIssuer = refreshID.issuer, .familyID = refreshID.familyID,
          .familyExpires = expires, .set = ZuMv(set), .nextDelivery = now},
          ZuMv(authorization));
      });
  };
  if (m_config.keyLoad)
    m_config.keyLoad(refreshID.issuer, ZuMv(issue));
  else {
    SignKey key = m_config.key;
    issue(ZuMv(key));
  }
}

void SSFTransmitter::persist_(SSFRx receiver, SSFDelivery delivery,
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

void SSFTransmitter::deliver_(SSFRx receiver, SSFDelivery delivery,
    String authorization)
{
  String eventID = delivery.eventID;
  String receiverID = delivery.receiverID;
  sendSSF(ZuMv(receiver), ZuMv(delivery), ZuMv(authorization), m_config.http,
    [self = ZmRef<SSFTransmitter>{this}, eventID = ZuMv(eventID),
        receiverID = ZuMv(receiverID)](int status) mutable {
      auto table = self->m_config.context->ssfDeliveries;
      if (!table) return;
      table->run(0, [self = ZuMv(self), table, eventID = ZuMv(eventID),
          receiverID = ZuMv(receiverID), status]() mutable {
        if (status == RevokeIssue::OK) {
          // A successful RFC 8935 response is the terminal outbox
          // transition.  The insert saga has already completed; never keep a
          // database saga open across this network operation.
          table->findDel<0>(0, ZuFwdTuple(ZuMv(eventID), ZuMv(receiverID)),
            [](ZdbRowRef<SSFDelivery>) { });
          return;
        }
        table->findUpd<0>(0, ZuFwdTuple(ZuMv(eventID), ZuMv(receiverID)),
          [self = ZuMv(self), table](ZdbRow<SSFDelivery> *row) mutable {
            if (!row) return;
            auto &item = row->data();
            int64_t now = Zm::now().sec();
            if (item.familyExpires <= now) {
              String expiredEvent = item.eventID;
              String expiredReceiver = item.receiverID;
              row->abort();
              table->findDel<0>(0, ZuFwdTuple(ZuMv(expiredEvent),
                  ZuMv(expiredReceiver)), [](ZdbRowRef<SSFDelivery>) { });
              return;
            }
            uint64_t delay = item.nextDelivery > now ?
              uint64_t(item.nextDelivery - now) * 2 : self->m_config.retryBase;
            if (delay > self->m_config.retryMax) delay = self->m_config.retryMax;
            item.nextDelivery = now + int64_t(delay);
            row->commit();
            self->arm_(item.nextDelivery);
          });
      });
    });
}

void SSFTransmitter::retry_()
{
  retryPage_({}, true);
}

void SSFTransmitter::retryPage_(SSFDeliveryTable::Key<0> key, bool first)
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
            [](ZdbRowRef<SSFDelivery>) { });
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
            if (!row) {
              self->arm_(Zm::now().sec() + self->m_config.retryBase);
              return;
            }
            SSFDelivery delivery{
              .eventID = ZuMv(tuple.template p<0>()),
              .receiverID = ZuMv(tuple.template p<1>()),
              .familyIssuer = ZuMv(tuple.template p<2>()),
              .familyID = ZuMv(tuple.template p<3>()),
              .familyExpires = tuple.template p<4>(),
              .set = ZuMv(tuple.template p<5>()),
              .nextDelivery = tuple.template p<6>()};
            SSFRx receiver = row->data();
            auto scheduler = self->m_config.requests->scheduler();
            auto sid = self->m_config.requests->sid();
            scheduler->run([self, receiver = ZuMv(receiver),
                delivery = ZuMv(delivery)]() mutable {
              if (!self->m_started) return;
              String authorization =
                self->m_config.secret(ZuMv(receiver.secretRef));
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

void SSFTransmitter::arm_(int64_t when)
{
  if (!m_started || !when) return;
  auto scheduler = m_config.requests->scheduler();
  scheduler->run([self = ZmRef<SSFTransmitter>{this}, when]() mutable {
    self->armOn_(when);
  }, m_config.requests->sid());
}

void SSFTransmitter::armOn_(int64_t when)
{
  if (!m_started || !when) return;
  auto scheduler = m_config.requests->scheduler();
  if (m_armed) scheduler->del(&m_timer);
  m_armed = true;
  int64_t now = Zm::now().sec();
    scheduler->add(&m_timer, Zm::now() + ZuTime{double(when > now ? when - now : 0)},
    ZmScheduler::Update, [this](auto &&arm) {
      return arm([this]() { m_armed = false; retry_(); });
    }, m_config.requests->sid());
}

void SSFTransmitter::clear_() { stop(); }

} // namespace Zum
