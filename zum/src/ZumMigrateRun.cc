//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Bounded asynchronous copy from the pre-application schema.

#include <zlib/ZumMigrate.hh>
#include <zlib/ZumDB.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum::Migrate {

template <typename Table, typename Apply>
class Scan_ : public ZumObject {
  using Key = typename Table::template Key<0>;
public:
  Scan_(Table *table, Apply apply, PutFn complete) :
    m_table{table}, m_apply{ZuMv(apply)}, m_complete{ZuMv(complete)} { }

  void start(bool next = false)
  {
    m_table->run(0, [self = ZmRef<Scan_>{this}, next]() mutable {
      self->m_found = false;
      auto receive = [self](ZuUnion<void, Key> result, unsigned) mutable {
        if (result.template is<Key>()) {
          self->m_key = ZuMv(result).template p<Key>();
          self->m_found = true;
          return;
        }
        self->m_table->run(0, [self]() mutable {
          if (!self->m_found) { self->finish_(true); return; }
          self->m_table->template find<0>(0, self->m_key,
            [self](ZdbRowRef<typename Table::T> row) mutable {
              if (!row) { self->finish_(false); return; }
              self->m_apply(row->data(), [self](bool ok) mutable {
                if (!ok) self->finish_(false);
                else self->start(true);
              });
            });
        });
      };
      if (next)
        self->m_table->template nextKeys<0>(
          self->m_key, false, 1, ZuMv(receive));
      else
        self->m_table->template selectKeys<0>({}, 1, ZuMv(receive));
    });
  }

private:
  void finish_(bool ok)
  {
    auto complete = ZuMv(m_complete);
    if (complete) complete(ok);
  }

  Table		*m_table;
  Apply		m_apply;
  PutFn		m_complete;
  Key		m_key;
  bool		m_found = false;
};

ZmHashKVDerive(IDSet, uint64_t, bool,
  (ZmHashHeapID<"Zum.Migrate.IDSet">));
ZmHashKVDerive(StringSet, String, bool,
  (ZmHashHeapID<"Zum.Migrate.StringSet">));

class Run_ : public ZumObject {
public:
  Run_(Zdb *source, Legacy::DBContext *sourceContext,
      DB *target, DBContext *targetContext, Ztls::Random &rng,
      Config config, CompleteFn complete) :
    m_source{source}, m_sourceContext{sourceContext},
    m_target{target}, m_targetContext{targetContext}, m_rng{&rng},
    m_config{ZuMv(config)}, m_complete{ZuMv(complete)} { }

  ~Run_()
  {
    if (m_config.dbKey)
      ZuClear(m_config.dbKey.data(), m_config.dbKey.length());
  }

  void start()
  {
    if (!m_source || !m_sourceContext || !m_target || !m_targetContext ||
        m_config.dbKey.length() != 32 ||
        !m_mapper.init(m_config.plan, m_error)) {
      finish_(false);
      return;
    }
    internal_<Zdb_::SagaData, Zdb_::SagaTable>("saga", &Run_::steps_);
  }

private:
  using Next = void (Run_::*)();

  void finish_(bool ok)
  {
    if (m_done) return;
    m_done = true;
    if (!ok && !m_error) m_error = "migration failed";
    auto complete = ZuMv(m_complete);
    if (complete) complete(ok, ZuMv(m_error));
  }

  void failed_(ZuCSpan error)
  {
    m_error = error;
    finish_(false);
  }

  template <typename T, typename Table>
  void internal_(ZuCSpan id, Next next)
  {
    m_source->store()->open(true, id, ZfVFields<T>(), ZfVKeyFields<T>(),
      reflection::GetSchema(ZfbSchema<T>::data()), Table::allocBuf,
      [self = ZmRef<Run_>{this}, next](Zdb_::OpenResult result) mutable {
        if (!result.template is<Zdb_::OpenData>()) {
          self->failed_("cannot inspect legacy saga state");
          return;
        }
        auto data = ZuMv(result).template p<Zdb_::OpenData>();
        bool empty = !data.count;
        data.storeTbl->close([self = ZuMv(self), next, empty]() mutable {
          if (!empty) {
            self->failed_("legacy database contains unsettled sagas");
            return;
          }
          (self.ptr()->*next)();
        });
      });
  }

  void steps_()
  {
    internal_<Zdb_::SagaStep, Zdb_::SagaStepTable>(
      "saga_step", &Run_::issuers_);
  }

  template <typename Table, typename Apply>
  void scan_(Table *table, Apply apply, Next next)
  {
    ZmRef<Scan_<Table, Apply>> scan = new Scan_<Table, Apply>{
      table, ZuMv(apply),
      [self = ZmRef<Run_>{this}, next](bool ok) mutable {
        if (!ok) self->finish_(false);
        else (self.ptr()->*next)();
      }};
    scan->start();
  }

  void issuers_()
  {
    auto apply = [self = ZmRef<Run_>{this}](
        const Legacy::Issuer &row, PutFn done) mutable {
      ++self->m_issuerCount;
      if (self->m_issuerCount != 1 || row.id != self->m_config.plan.issuer) {
        self->m_error = "legacy issuer does not match migration mapping";
        done(false); return;
      }
      self->m_legacyActions = row.nextActionID;
      done(true);
    };
    scan_(m_sourceContext->issuers.ptr(), ZuMv(apply), &Run_::actions_);
  }

  void actions_()
  {
    auto apply = [self = ZmRef<Run_>{this}](
        const Legacy::Action &row, PutFn done) mutable {
      Action value;
      bool ok = row.id < self->m_legacyActions &&
        self->m_mapper.action(
          row, self->m_config.plan.time, value, self->m_error);
      if (ok) ++self->m_actionCount;
      done(ok);
    };
    scan_(m_sourceContext->actions.ptr(), ZuMv(apply), &Run_::roles_);
  }

  void roles_()
  {
    auto apply = [self = ZmRef<Run_>{this}](
        const Legacy::Role &row, PutFn done) mutable {
      Role value;
      bool ok = self->m_mapper.role(
        row, self->m_config.plan.time, value, self->m_error);
      if (ok) ++self->m_roleCount;
      done(ok);
    };
    scan_(m_sourceContext->roles.ptr(), ZuMv(apply), &Run_::scopes_);
  }

  void noteAudience_(ZuCSpan uri)
  {
    if (!m_audiences.findPtr(uri)) m_audiences.add(String{uri}, true);
  }

  void scopes_()
  {
    auto apply = [self = ZmRef<Run_>{this}](
        const Legacy::Scope &row, PutFn done) mutable {
      Scope value;
      bool ok = self->m_mapper.scope(
        row, self->m_config.plan.time, value, self->m_error);
      if (ok) {
        ++self->m_scopeCount;
        self->noteAudience_(row.audience);
      }
      done(ok);
    };
    scan_(m_sourceContext->scopes.ptr(), ZuMv(apply), &Run_::users_);
  }

  void users_()
  {
    auto apply = [self = ZmRef<Run_>{this}](
        const Legacy::User &row, PutFn done) mutable {
      User value;
      Memberships memberships;
      if (self->m_mapper.discardUser(row.id)) {
        if (!row.oidcSub) {
          self->m_error = "discarded external user is not external";
          done(false); return;
        }
        self->m_discardedUsers.add(row.id, true);
        ++self->m_discardedUserCount;
        done(true); return;
      }
      bool ok = row.id != self->m_config.plan.initialUserID &&
        self->m_mapper.user(row, value, memberships, self->m_error);
      if (!ok) {
        if (!self->m_error)
          self->m_error = "initial administrator id collides with a legacy user";
        done(false); return;
      }
      if (self->m_users.findPtr(row.id)) {
        self->m_error = "duplicate legacy user id";
        done(false); return;
      }
      self->m_users.add(row.id, true);
      done(true);
    };
    scan_(m_sourceContext->users.ptr(), ZuMv(apply), &Run_::creds_);
  }

  void creds_()
  {
    auto apply = [self = ZmRef<Run_>{this}](
        const Legacy::Cred &row, PutFn done) mutable {
      Cred value;
      if (self->m_discardedUsers.findPtr(row.userID)) {
        ++self->m_discardedCredCount;
        done(true); return;
      }
      bool ok = self->m_users.findPtr(row.userID) &&
        self->m_mapper.cred(row, value, self->m_error);
      if (ok) ++self->m_credCount;
      if (!ok && !self->m_error)
        self->m_error = "legacy credential references an unknown user";
      done(ok);
    };
    scan_(m_sourceContext->creds.ptr(), ZuMv(apply), &Run_::clients_);
  }

  void clients_()
  {
    auto apply = [self = ZmRef<Run_>{this}](
        const Legacy::Client &row, PutFn done) mutable {
      Client value;
      Accesses accesses;
      bool ok = self->m_mapper.client(row, value, accesses, self->m_error);
      if (ok) {
        ++self->m_clientCount;
        self->m_accessCount += accesses.length();
        for (const auto &uri: row.audiences) self->noteAudience_(uri);
      }
      done(ok);
    };
    scan_(m_sourceContext->clients.ptr(), ZuMv(apply), &Run_::grants_);
  }

  void grants_()
  {
    auto apply = [self = ZmRef<Run_>{this}](
        const Legacy::Grant &row, PutFn done) mutable {
      if (row.owner) {
        self->m_error = "legacy grant has an unsettled saga owner";
        done(false); return;
      }
      ++self->m_grantCount;
      done(true);
    };
    scan_(m_sourceContext->grants.ptr(), ZuMv(apply), &Run_::signKeys_);
  }

  void signKeys_()
  {
    auto apply = [self = ZmRef<Run_>{this}](
        const Legacy::SignKey &, PutFn done) mutable {
      ++self->m_signKeyCount;
      done(true);
    };
    scan_(m_sourceContext->signKeys.ptr(), ZuMv(apply), &Run_::audits_);
  }

  void audits_()
  {
    auto apply = [self = ZmRef<Run_>{this}](
        const Legacy::Audit &, PutFn done) mutable {
      ++self->m_auditCount;
      done(true);
    };
    scan_(m_sourceContext->audits.ptr(), ZuMv(apply), &Run_::preflight_);
  }

  void preflight_()
  {
    const auto &plan = m_config.plan;
    if (m_issuerCount != 1 || m_actionCount != plan.actions.length() ||
        m_roleCount != plan.roles.length() ||
        m_scopeCount != plan.scopes.length() ||
        m_clientCount != plan.clients.length() ||
        m_discardedUserCount != plan.discardExternalUsers.length() ||
        m_audiences.count_() != plan.audiences.length()) {
      failed_("migration mapping contains missing or unused records");
      return;
    }
    targetIssuer_();
  }

  void targetIssuer_()
  {
    auto table = m_targetContext->issuers;
    table->run(0, [self = ZmRef<Run_>{this}, table]() mutable {
      table->find<0>(0, ZuFwdTuple(self->m_config.plan.issuer),
        [self = ZuMv(self)](ZdbRowRef<Issuer> row) mutable {
          if (row) self->start_();
          else self->targetCounts_(false);
        });
    });
  }

  void start_()
  {
    Migrate::start(m_target, m_targetContext, *m_rng,
      m_config.plan, m_config.dbKey,
      [self = ZmRef<Run_>{this}](bool ok) mutable {
        if (!ok) self->finish_(false);
        else self->putApps_();
      });
  }

  template <typename Table>
  bool targetCount_(Table *table, uint64_t expected, bool final)
  {
    uint64_t actual = table->count();
    if (actual == expected) return true;
    failed_(String{} << (final ?
      "migration target contains unexpected records in " :
      "new migration target is not empty at ") << table->id() <<
      " (expected " << expected << ", found " << actual << ')');
    return false;
  }

  void targetCounts_(bool final)
  {
    const uint64_t zero = 0;
    if (!targetCount_(m_targetContext->issuers, final ? 1 : zero, final) ||
        !targetCount_(m_targetContext->apps,
          final ? m_config.plan.apps.length() : zero, final) ||
        !targetCount_(m_targetContext->users,
          final ? m_users.count_() : zero, final) ||
        !targetCount_(m_targetContext->creds,
          final ? m_credCount : zero, final) ||
        !targetCount_(m_targetContext->memberships,
          final ? m_membershipCount : zero, final) ||
        !targetCount_(m_targetContext->actions,
          final ? m_actionCount : zero, final) ||
        !targetCount_(m_targetContext->roles,
          final ? m_roleCount : zero, final) ||
        !targetCount_(m_targetContext->scopes,
          final ? m_scopeCount : zero, final) ||
        !targetCount_(m_targetContext->audiences,
          final ? m_audiences.count_() : zero, final) ||
        !targetCount_(m_targetContext->clients,
          final ? m_clientCount : zero, final) ||
        !targetCount_(m_targetContext->clientAccess,
          final ? m_accessCount : zero, final) ||
        !targetCount_(m_targetContext->adminAccess, zero, final) ||
        !targetCount_(m_targetContext->providers, zero, final) ||
        !targetCount_(m_targetContext->authPolicies, zero, final) ||
        !targetCount_(m_targetContext->extIdentities, zero, final) ||
        !targetCount_(m_targetContext->roleMaps, zero, final) ||
        !targetCount_(m_targetContext->evidence, zero, final) ||
        !targetCount_(m_targetContext->sessions, zero, final) ||
        !targetCount_(m_targetContext->consents, zero, final) ||
        !targetCount_(m_targetContext->grants, zero, final) ||
        !targetCount_(m_targetContext->signKeys, zero, final) ||
        !targetCount_(m_targetContext->requests, zero, final)) return;
    if (final) finishMigration_();
    else start_();
  }

  template <typename T>
  void put_(T value, PutFn done)
  {
    Migrate::put(m_target, m_targetContext, *m_rng,
      ZuMv(value), ZuMv(done));
  }

  void putApps_()
  {
    if (m_index >= m_config.plan.apps.length()) {
      m_index = 0;
      putAudiences_();
      return;
    }
    App value;
    if (!m_mapper.app(
        m_config.plan.apps[m_index], m_config.plan.time, value)) {
      finish_(false); return;
    }
    put_(ZuMv(value), [self = ZmRef<Run_>{this}](bool ok) mutable {
      if (!ok) self->finish_(false);
      else { ++self->m_index; self->putApps_(); }
    });
  }

  void putAudiences_()
  {
    if (m_index >= m_config.plan.audiences.length()) {
      m_index = 0;
      putActions_();
      return;
    }
    Audience value;
    if (!m_mapper.audience(
        m_config.plan.audiences[m_index], m_config.plan.time, value)) {
      finish_(false); return;
    }
    put_(ZuMv(value), [self = ZmRef<Run_>{this}](bool ok) mutable {
      if (!ok) self->finish_(false);
      else { ++self->m_index; self->putAudiences_(); }
    });
  }

  template <typename Table, typename Convert>
  void copy_(Table *table, Convert convert, Next next)
  {
    auto apply = [self = ZmRef<Run_>{this},
      convert = ZuMv(convert)](const typename Table::T &row, PutFn done) mutable {
      convert(row, ZuMv(done));
    };
    scan_(table, ZuMv(apply), next);
  }

  void putActions_()
  {
    copy_(m_sourceContext->actions.ptr(),
      [self = ZmRef<Run_>{this}](const Legacy::Action &row, PutFn done) mutable {
        Action value;
        if (!self->m_mapper.action(
            row, self->m_config.plan.time, value, self->m_error)) {
          done(false); return;
        }
        self->put_(ZuMv(value), ZuMv(done));
      }, &Run_::putRoles_);
  }

  void putRoles_()
  {
    copy_(m_sourceContext->roles.ptr(),
      [self = ZmRef<Run_>{this}](const Legacy::Role &row, PutFn done) mutable {
        Role value;
        if (!self->m_mapper.role(
            row, self->m_config.plan.time, value, self->m_error)) {
          done(false); return;
        }
        self->put_(ZuMv(value), ZuMv(done));
      }, &Run_::putScopes_);
  }

  void putScopes_()
  {
    copy_(m_sourceContext->scopes.ptr(),
      [self = ZmRef<Run_>{this}](const Legacy::Scope &row, PutFn done) mutable {
        Scope value;
        if (!self->m_mapper.scope(
            row, self->m_config.plan.time, value, self->m_error)) {
          done(false); return;
        }
        self->put_(ZuMv(value), ZuMv(done));
      }, &Run_::putUsers_);
  }

  template <typename Vec>
  void putVec_(Vec values, unsigned index, PutFn done)
  {
    if (index >= values.length()) { done(true); return; }
    auto value = ZuMv(values[index]);
    put_(ZuMv(value), [self = ZmRef<Run_>{this},
      values = ZuMv(values), index, done = ZuMv(done)](bool ok) mutable {
        if (!ok) done(false);
        else self->putVec_(ZuMv(values), index + 1, ZuMv(done));
      });
  }

  void putUsers_()
  {
    copy_(m_sourceContext->users.ptr(),
      [self = ZmRef<Run_>{this}](const Legacy::User &row, PutFn done) mutable {
      if (self->m_mapper.discardUser(row.id)) { done(true); return; }
        User value;
        Memberships memberships;
        if (!self->m_mapper.user(row, value, memberships, self->m_error)) {
          done(false); return;
        }
        self->m_membershipCount += memberships.length();
        self->put_(ZuMv(value), [self, memberships = ZuMv(memberships),
          done = ZuMv(done)](bool ok) mutable {
            if (!ok) done(false);
            else self->putVec_(ZuMv(memberships), 0, ZuMv(done));
          });
      }, &Run_::putCreds_);
  }

  void putCreds_()
  {
    copy_(m_sourceContext->creds.ptr(),
      [self = ZmRef<Run_>{this}](const Legacy::Cred &row, PutFn done) mutable {
        if (self->m_discardedUsers.findPtr(row.userID)) {
          done(true); return;
        }
        Cred value;
        if (!self->m_mapper.cred(row, value, self->m_error)) {
          done(false); return;
        }
        self->put_(ZuMv(value), ZuMv(done));
      }, &Run_::putClients_);
  }

  void putClients_()
  {
    copy_(m_sourceContext->clients.ptr(),
      [self = ZmRef<Run_>{this}](const Legacy::Client &row, PutFn done) mutable {
        Client value;
        Accesses accesses;
        if (!self->m_mapper.client(row, value, accesses, self->m_error)) {
          done(false); return;
        }
        self->put_(ZuMv(value), [self, accesses = ZuMv(accesses),
          done = ZuMv(done)](bool ok) mutable {
            if (!ok) done(false);
            else self->putVec_(ZuMv(accesses), 0, ZuMv(done));
          });
      }, &Run_::verify_);
  }

  void verify_()
  {
    targetCounts_(true);
  }

  void finishMigration_()
  {
    Migrate::finish(m_target, m_targetContext, *m_rng,
      m_config.plan.issuer, [self = ZmRef<Run_>{this}](bool ok) mutable {
        if (ok) ZiLOG(Info, "Zum", ([
          grants = self->m_grantCount, keys = self->m_signKeyCount,
          audits = self->m_auditCount,
          users = self->m_discardedUserCount,
          creds = self->m_discardedCredCount](auto &s) {
            s << "migration invalidated legacy grants=" << grants
              << " signing keys=" << keys << " audit rows=" << audits
              << " external users=" << users
              << " external credentials=" << creds;
          }));
        self->finish_(ok);
      });
  }

  Zdb			*m_source;
  Legacy::DBContext	*m_sourceContext;
  DB			*m_target;
  DBContext		*m_targetContext;
  Ztls::Random		*m_rng;
  Config		m_config;
  CompleteFn		m_complete;
  Mapper		m_mapper;
  String		m_error;
  IDSet			m_users;
  IDSet			m_discardedUsers;
  StringSet		m_audiences;
  unsigned		m_index = 0;
  unsigned		m_issuerCount = 0;
  unsigned		m_actionCount = 0;
  unsigned		m_roleCount = 0;
  unsigned		m_scopeCount = 0;
  unsigned		m_clientCount = 0;
  unsigned		m_credCount = 0;
  unsigned		m_membershipCount = 0;
  unsigned		m_accessCount = 0;
  unsigned		m_grantCount = 0;
  unsigned		m_signKeyCount = 0;
  unsigned		m_auditCount = 0;
  unsigned		m_discardedUserCount = 0;
  unsigned		m_discardedCredCount = 0;
  ActionID		m_legacyActions = 0;
  bool			m_done = false;
};

bool run(Zdb *source, Legacy::DBContext *sourceContext,
    DB *target, DBContext *targetContext, Ztls::Random &rng,
    Config config, CompleteFn complete)
{
  if (!source || !sourceContext || !target || !targetContext) {
    complete(false, "invalid migration database");
    return false;
  }
  ZmRef<Run_> run = new Run_{source, sourceContext, target, targetContext,
    rng, ZuMv(config), ZuMv(complete)};
  run->start();
  return true;
}

} // namespace Zum::Migrate
