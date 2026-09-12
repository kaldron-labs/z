//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Native saga for one typed row produced by offline migration.

#ifndef ZumMigratePut_HH
#define ZumMigratePut_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumIdentityDB.hh>
#include <zlib/ZumAppDB.hh>
#include <zlib/ZumSagaImage.hh>
#include <zlib/zum_saga_fbs.h>

namespace Zum {

struct MigrationStart : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"migrationStart.v1">;
  enum { NSteps = 1 };

  Issuer issuer;

  ZdbSagaStep(0, zum.issuer, Insert) {
    auto table = context->issuers;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
        table->find<0>(0, ZuFwdTuple(issuer.id),
          [this, table, complete = ZuMv(complete)](
              ZdbRowRef<Issuer> existing) mutable {
            ZdbRowRef<Issuer> row =
              new ZdbRow<Issuer>{table, ZdbShard{0}};
            saga->insert(table, ZuMv(row), ZuMv(complete),
              [this, duplicate = bool(existing)](
                  ZdbRow<Issuer> *row, auto &&complete) mutable {
                new (row->ptr()) Issuer{issuer};
                if (duplicate) { complete(false); return; }
                complete(row->commit());
              });
          });
      } else {
        saga->findDel<0>(table, 0, ZuFwdTuple(issuer.id), ZuMv(complete),
          [](ZdbRow<Issuer> *row, auto &&complete) mutable {
            complete(row && row->commit());
          });
      }
    });
    return {};
  }
};
ZfbStruct(ZumAPI, MigrationStart,
  (((issuer), (Ctor<0>)), (UDT)));

struct MigrationPut : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"migrationPut.v1">;

  struct Kind {
    enum { Invalid, App, User, Cred, Membership, Action, Role, Scope,
      Audience, Client, ClientAccess };
  };

  enum { NSteps = 20 };

  uint32_t kind = Kind::Invalid;
  Bytes image;

  template <bool Fwd, typename Table>
  void insert_(Table *table, Zdb_::SagaCompleteFn complete)
  {
    using T = typename Table::T;
    T value;
    if (!SagaImage::load(image, value)) { complete(false); return; }
    typename Table::template Key<0> key{ZuStructKey<0>(value)};
    table->run(0, [this, table, key = ZuMv(key), value = ZuMv(value),
      complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
        table->template find<0>(0, key, [this, table, value = ZuMv(value),
          complete = ZuMv(complete)](ZdbRowRef<T> existing) mutable {
          ZdbRowRef<T> row = new ZdbRow<T>{table, ZdbShard{0}};
          saga->insert(table, ZuMv(row), ZuMv(complete),
            [this, duplicate = bool(existing), value = ZuMv(value)](
                ZdbRow<T> *row, auto &&complete) mutable {
              new (row->ptr()) T{ZuMv(value)};
              row->data().owner = saga->id();
              if (duplicate) { complete(false); return; }
              complete(row->commit());
            });
        });
      } else {
        saga->template findDel<0>(table, 0, ZuMv(key), ZuMv(complete),
          [this](ZdbRow<T> *row, auto &&complete) mutable {
            complete(row && row->data().owner == saga->id() && row->commit());
          });
      }
    });
  }

  template <bool Fwd, typename Table>
  void finish_(Table *table, Zdb_::SagaCompleteFn complete)
  {
    using T = typename Table::T;
    T value;
    if (!SagaImage::load(image, value)) { complete(false); return; }
    typename Table::template Key<0> key{ZuStructKey<0>(value)};
    table->run(0, [this, table, key = ZuMv(key),
      complete = ZuMv(complete)]() mutable {
      saga->template findUpd<0>(table, 0, ZuMv(key), ZuMv(complete),
        [this](ZdbRow<T> *row, auto &&complete) mutable {
          if (!row) { complete(false); return; }
          auto expected = Fwd ? saga->id() : uint128_t{};
          if (row->data().owner != expected) { complete(false); return; }
          row->data().owner = Fwd ? uint128_t{} : saga->id();
          complete(row->commit());
        });
    });
  }

#define ZUM_MIGRATE_PUT_STEP(N, K, TABLE, MEMBER) \
  ZdbSagaStep(N, TABLE, Insert) { \
    if (kind != Kind::K) { saga->skip(ZuMv(complete)); return {}; } \
    insert_<Fwd>(context->MEMBER, ZuMv(complete)); \
    return {}; \
  } \
  ZdbSagaStep(N + 10, TABLE, Update) { \
    if (kind != Kind::K) { saga->skip(ZuMv(complete)); return {}; } \
    finish_<Fwd>(context->MEMBER, ZuMv(complete)); \
    return {}; \
  }

  ZUM_MIGRATE_PUT_STEP(0, App, zum.app, apps)
  ZUM_MIGRATE_PUT_STEP(1, User, zum.user, users)
  ZUM_MIGRATE_PUT_STEP(2, Cred, zum.cred, creds)
  ZUM_MIGRATE_PUT_STEP(3, Membership, zum.membership, memberships)
  ZUM_MIGRATE_PUT_STEP(4, Action, zum.action, actions)
  ZUM_MIGRATE_PUT_STEP(5, Role, zum.role, roles)
  ZUM_MIGRATE_PUT_STEP(6, Scope, zum.scope, scopes)
  ZUM_MIGRATE_PUT_STEP(7, Audience, zum.audience, audiences)
  ZUM_MIGRATE_PUT_STEP(8, Client, zum.client, clients)
  ZUM_MIGRATE_PUT_STEP(9, ClientAccess, zum.client_access, clientAccess)

#undef ZUM_MIGRATE_PUT_STEP
};
ZfbStruct(ZumAPI, MigrationPut,
  (((kind), (Ctor<0>)), (UInt32)),
  (((image), (Ctor<1>)), (Bytes)));

struct MigrationFinish : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"migrationFinish.v1">;
  enum { NSteps = 1 };

  String issuer;

  ZdbSagaStep(0, zum.issuer, Update) {
    auto table = context->issuers;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(table, 0, ZuFwdTuple(issuer), ZuMv(complete),
        [](ZdbRow<Issuer> *row, auto &&complete) mutable {
          if (!row) { complete(false); return; }
          auto expected = Fwd ? BootstrapPhase::Migrating : BootstrapPhase::Empty;
          if (row->data().bootstrapPhase != expected) {
            complete(false); return;
          }
          row->data().bootstrapPhase = BootstrapPhase::T(
            Fwd ? BootstrapPhase::Empty : BootstrapPhase::Migrating);
          complete(row->commit());
        });
    });
    return {};
  }
};
ZfbStruct(ZumAPI, MigrationFinish,
  (((issuer), (Ctor<0>)), (String)));

} // namespace Zum

#endif /* ZumMigratePut_HH */
