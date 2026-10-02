//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_rekey.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum {

// One row and one saga in flight: offline traversal has bounded memory and
// returns to the table scheduler between records. Never retain a scan iterator.
template <typename Table, typename Apply, typename Heap = ZuVoid>
class RekeyScan__ : public Heap, public ZmObject  {
  using Key = typename Table::template Key<0>;
public:
  RekeyScan__(Table *table, Apply apply, RekeyFn complete) :
    m_table{table}, m_apply{ZuMv(apply)}, m_complete{ZuMv(complete)} { }

  void start(bool next = false)
  {
    m_table->run(0, [self = ZmRef<RekeyScan__>{this}, next]() mutable {
      self->m_found = false;
      auto receive = [self](ZuUnion<void, Key> result, unsigned) mutable {
	if (result.template is<Key>()) {
	  self->m_key = ZuMv(result).template p<Key>();
	  self->m_found = true;
	  return;
	}
	// Store callbacks transfer the completed cursor to the table owner.
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
	self->m_table->template nextKeys<0>(self->m_key, false, 1, ZuMv(receive));
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
  Table *m_table;
  Apply m_apply;
  RekeyFn m_complete;
  Key m_key;
  bool m_found = false;
};
template <typename Table, typename Apply>
ZuDerive(RekeyScanHeap,
  (ZmHeap<"Zum.zumd.rekey.RekeyScan", RekeyScan__<Table, Apply>>));
template <typename Table, typename Apply>
ZuDerive(RekeyScan_, (RekeyScan__<Table, Apply,
  RekeyScanHeap<Table, Apply>>));

template <typename Heap = ZuVoid>
class Rekey__ : public Heap, public ZmObject  {
public:
  Rekey__(DB *db, DBContext *context, Ztls::Random &rng, String issuer,
      Bytes oldKey, Bytes newKey, RekeyFn complete) :
    m_db{db}, m_context{context}, m_rng{&rng}, m_issuer{ZuMv(issuer)},
    m_oldKey{ZuMv(oldKey)}, m_newKey{ZuMv(newKey)},
    m_complete{ZuMv(complete)} { }

  ~Rekey__()
  {
    if (m_oldKey) ZuClear(m_oldKey);
    if (m_newKey) ZuClear(m_newKey);
  }

  void start()
  {
    m_oldCheck = serverKeyCheck(m_oldKey);
    m_newCheck = serverKeyCheck(m_newKey);
    if (!m_oldCheck || !m_newCheck || m_oldCheck == m_newCheck) {
      finish_(false); return;
    }
    auto table = m_context->issuers;
    table->run(0, [self = ZmRef<Rekey__>{this}, table]() mutable {
      table->find<0>(0, ZuFwdTuple(self->m_issuer),
	[self](ZdbRowRef<Issuer> row) mutable {
	  if (!row || row->data().schemaVersion != SchemaVersion) {
	    self->finish_(false); return;
	  }
	  const auto &value = row->data();
	  if (Ztls::ctEqual(value.keyCheck, self->m_newCheck) &&
	      !value.pendingKeyCheck) {
	    self->m_verify = self->m_committed = true;
	    self->providers_();
	    return;
	  }
	  if (!Ztls::ctEqual(value.keyCheck, self->m_oldCheck) ||
	      (value.pendingKeyCheck &&
	       !Ztls::ctEqual(value.pendingKeyCheck, self->m_newCheck))) {
	    self->finish_(false); return;
	  }
	  if (value.pendingKeyCheck) { self->providers_(); return; }
	  self->submit_(KeyBinding{
	    .issuer = self->m_issuer, .beforeCheck = self->m_oldCheck,
	    .afterCheck = self->m_oldCheck, .afterPending = self->m_newCheck},
	    [self](bool ok) mutable {
	      if (!ok) self->finish_(false);
	      else self->providers_();
	    });
	});
    });
  }

private:
  template <typename Def>
  void submit_(Def def, RekeyFn complete)
  {
    ZdbSagaID id;
    if (!m_rng->random({reinterpret_cast<uint8_t *>(&id), sizeof(id)}) || !id) {
      complete(false); return;
    }
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(def));
    m_next = ZuMv(complete);
    auto done = [self = ZmRef<Rekey__>{this}](bool ok) mutable {
      auto next = ZuMv(self->m_next);
      if (next) next(ok);
    };
    if (!sagaSubmit(m_db, id, ZuMv(saga),
	[done](bool ok) mutable { if (!ok) done(false); }, done)) done(false);
  }

  template <typename Table, typename Next>
  void scan_(Table *table, Next next)
  {
    auto apply = [self = ZmRef<Rekey__>{this}](const auto &row, RekeyFn done) {
      self->record_(row, ZuMv(done));
    };
    ZmRef<RekeyScan_<Table, decltype(apply)>> scan =
      new RekeyScan_<Table, decltype(apply)>{table, ZuMv(apply),
	[self = ZmRef<Rekey__>{this}, next](bool ok) mutable {
	  if (!ok) self->finish_(false);
	  else (self.ptr()->*next)();
	}};
    scan->start();
  }

  void providers_() { scan_(m_context->providers, &Rekey__::evidence_); }
  void evidence_() { scan_(m_context->evidence, &Rekey__::signers_); }
  void signers_() { scan_(m_context->signKeys, &Rekey__::receivers_); }

  void receivers_() { scan_(m_context->ssfRx, &Rekey__::scanned_); }

  void record_(const SSFRx &value, RekeyFn done)
  {
    if (value.owner) { done(false); return; }
    change_(SecretRekey{.field = SecretRekey::SSFField,
      .keyID = value.receiverID, .before = value.callbackAuth},
      m_issuer, "zum.ssf_rx", value.receiverID, "callbackAuth", ZuMv(done));
  }
  void record_(const Provider &value, RekeyFn done)
  {
    if (value.owner) { done(false); return; }
    String id;
    id << value.id;
    change_(SecretRekey{.field = SecretRekey::ProviderField,
      .providerID = value.id, .before = value.clientSecret},
      m_issuer, "provider", id, "clientSecret", ZuMv(done));
  }
  void record_(const Evidence &value, RekeyFn done)
  {
    if (value.owner) { done(false); return; }
    String id;
    id << value.appID << ':' << value.userID << ':' << value.providerID;
    change_(SecretRekey{.field = SecretRekey::EvidenceField,
      .providerID = value.providerID, .appID = value.appID,
      .userID = value.userID, .before = value.protectedRefreshToken},
      m_issuer, "evidence", id, "protectedRefreshToken", ZuMv(done));
  }
  void record_(const SignKey &value, RekeyFn done)
  {
    change_(SecretRekey{.field = SecretRekey::SignKeyField,
      .keyID = value.id, .before = value.privateMaterial},
      value.issuer, "zum.sign_key", value.id, "privateMaterial", ZuMv(done));
  }

  void change_(SecretRekey change, ZuCSpan issuer, ZuCSpan table,
      ZuCSpan id, ZuCSpan field, RekeyFn done)
  {
    if (!change.before) { done(true); return; }
    if (m_verify) {
      Bytes plain;
      bool ok = serverSecretDecrypt(m_newKey, issuer, table, id, field,
	change.before, plain);
      if (plain) ZuClear(plain);
      done(ok);
      return;
    }
    change.after = change.before;
    if (!serverSecretRekey(*m_rng, m_oldKey, m_newKey, issuer,
	table, id, field, change.after)) { done(false); return; }
    if (change.after == change.before) { done(true); return; }
    submit_(ZuMv(change), ZuMv(done));
  }

  void scanned_()
  {
    if (!m_verify) { m_verify = true; providers_(); return; }
    if (m_committed) { finish_(true); return; }
    submit_(KeyBinding{
      .issuer = m_issuer, .beforeCheck = m_oldCheck, .afterCheck = m_newCheck,
      .beforePending = m_newCheck},
      [self = ZmRef<Rekey__>{this}](bool ok) mutable { self->finish_(ok); });
  }
  void finish_(bool ok)
  {
    auto complete = ZuMv(m_complete);
    if (complete) complete(ok);
  }

  DB *m_db;
  DBContext *m_context;
  Ztls::Random *m_rng;
  String m_issuer;
  Bytes m_oldKey;
  Bytes m_newKey;
  Bytes m_oldCheck;
  Bytes m_newCheck;
  RekeyFn m_complete;
  RekeyFn m_next;
  bool m_verify = false;
  bool m_committed = false;
};
ZuDerive(RekeyHeap, (ZmHeap<"Zum.zumd.rekey.Rekey", Rekey__<>>));
ZuDerive(Rekey_, (Rekey__<RekeyHeap>));

void serverRekey(DB *db, DBContext *context, Ztls::Random &rng, String issuer,
    Bytes oldKey, Bytes newKey, RekeyFn complete)
{
  ZmRef<Rekey_> rekey = new Rekey_{db, context, rng, ZuMv(issuer),
    ZuMv(oldKey), ZuMv(newKey), ZuMv(complete)};
  rekey->start();
}

} // namespace Zum
