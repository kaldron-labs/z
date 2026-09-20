//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_session.hh>
#include <zlib/zumd_identity_db.hh>

#include <zlib/zumd_oauth.hh>

#include <zlib/ZuBase64URL.hh>

namespace Zum {

template <typename Heap = ZuVoid>
class SessionComplete__ : public Heap, public ZmObject  {
public:
  SessionComplete__(SessionFn complete) : m_complete{ZuMv(complete)} { }
  void request(ZmRef<Request> request) { m_request = ZuMv(request); }
  void complete(int error, Session session = {}, String token = {}) {
    m_request->complete([self = ZmRef<SessionComplete__>{this}, error,
	  session = ZuMv(session), token = ZuMv(token)]() mutable {
      auto complete = ZuMv(self->m_complete);
      complete(error, ZuMv(session), ZuMv(token));
    });
  }
  void cancel() {
    auto complete = ZuMv(m_complete);
    complete(SessionError::Storage, Session{}, String{});
  }
private:
  ZmRef<Request>	m_request;
  SessionFn	m_complete;
};
using SessionCompleteHeap =
  ZmHeap<"Zum.zumd.session.SessionComplete", SessionComplete__<>>;
ZuDerive(SessionComplete_, (SessionComplete__<SessionCompleteHeap>));

template <typename Heap = ZuVoid>
class SessionDoneComplete__ : public Heap, public ZmObject  {
public:
  SessionDoneComplete__(SessionDoneFn complete) : m_complete{ZuMv(complete)} { }
  void request(ZmRef<Request> request) { m_request = ZuMv(request); }
  void complete(int error) {
    m_request->complete([self = ZmRef<SessionDoneComplete__>{this}, error]() {
      auto complete = ZuMv(self->m_complete);
      complete(error);
    });
  }
  void cancel() {
    auto complete = ZuMv(m_complete);
    complete(SessionError::Storage);
  }
private:
  ZmRef<Request>	m_request;
  SessionDoneFn	m_complete;
};
using SessionDoneCompleteHeap =
  ZmHeap<"Zum.zumd.session.SessionDoneComplete", SessionDoneComplete__<>>;
ZuDerive(SessionDoneComplete_,
  (SessionDoneComplete__<SessionDoneCompleteHeap>));

static void issue_(DBContext *context, Ztls::Random &rng,
    SessionConfig config, SessionFn complete)
{
  if (!context || !config.issuer || !config.subject || !config.userID ||
      config.authTime <= 0 || config.authTime > config.now || config.now <= 0 ||
      config.idleLifetime <= 0 || config.absoluteLifetime <= 0 ||
      config.idleLifetime > config.absoluteLifetime || !config.authVersion ||
      config.idleLifetime > INT64_MAX - config.now ||
      config.absoluteLifetime > INT64_MAX - config.now) {
    complete(SessionError::Invalid, Session{}, String{});
    return;
  }
  int64_t absolute = config.now + config.absoluteLifetime;
  int64_t idle = config.now + config.idleLifetime;
  if (absolute <= config.now || idle <= config.now) {
    complete(SessionError::Invalid, Session{}, String{});
    return;
  }
  if (idle > absolute) idle = absolute;
  auto users = context->users;
  users->run(0, [context, users, rng = &rng, config = ZuMv(config),
      idle, absolute, complete = ZuMv(complete)]() mutable {
    users->find<0>(0, ZuFwdTuple(config.userID), [context, rng,
	  config = ZuMv(config), idle, absolute,
	  complete = ZuMv(complete)](ZdbRowRef<User> row) mutable {
      String subject;
      if (row && row->data().handle) {
	subject.length(ZuBase64URL::enclen(row->data().handle.length()));
	subject.length(ZuBase64URL::encode(
	  subject.span(), row->data().handle));
      }
      if (!row || row->data().state != State::Active || row->data().owner ||
	  row->data().authVersion != config.authVersion ||
	  subject != config.subject) {
	complete(SessionError::Invalid, Session{}, String{});
	return;
      }
      OpaqueToken opaque;
      if (!opaqueIssue(*rng, opaque)) {
	complete(SessionError::Storage, Session{}, String{});
	return;
      }
      Session session{
	.digest = ZuMv(opaque.digest), .userID = config.userID,
	.providerID = config.providerID, .issuer = ZuMv(config.issuer),
	.subject = ZuMv(config.subject), .authTime = config.authTime,
	.idleDeadline = idle, .absoluteDeadline = absolute,
	.state = State::Active, .authVersion = config.authVersion,
	.version = 1, .created = config.now, .updated = config.now};
      auto sessions = context->sessions;
      sessions->run(0, [sessions, session = ZuMv(session),
	  token = ZuMv(opaque.token), complete = ZuMv(complete)]() mutable {
	ZdbRowRef<Session> next =
	  new ZdbRow<Session>{sessions, ZdbShard{0}};
	sessions->insert(ZuMv(next), [session = ZuMv(session),
	    token = ZuMv(token), complete = ZuMv(complete)](
	      ZdbRow<Session> *row) mutable {
	  if (!row) {
	    complete(SessionError::Storage, Session{}, String{});
	    return;
	  }
	  new (row->ptr()) Session{ZuMv(session)};
	  if (!row->commit()) {
	    complete(SessionError::Storage, Session{}, String{});
	    return;
	  }
	  complete(SessionError::OK, Session{row->data()}, ZuMv(token));
	});
      });
    });
  });
}

static void use_(DBContext *context, String token, String issuer,
    int64_t now, int64_t idleLifetime, SessionFn complete)
{
  Bytes id, digest;
  if (!context || !token || !issuer || now <= 0 || idleLifetime <= 0 ||
      idleLifetime > INT64_MAX - now ||
      !opaqueParse(token, id, digest)) {
    complete(SessionError::Invalid, Session{}, String{});
    return;
  }
  if (token.mutable_()) ZuClear(token.data(), token.length());
  auto sessions = context->sessions;
  sessions->run(0, [sessions, digest = ZuMv(digest), issuer = ZuMv(issuer),
      now, idleLifetime, complete = ZuMv(complete)]() mutable {
    sessions->findUpd<0, ZuSeq<2>>(0, ZuFwdTuple(ZuMv(digest)), [issuer = ZuMv(issuer),
	  now, idleLifetime, complete = ZuMv(complete)](
	    ZdbRow<Session> *row) mutable {
      if (!row || row->data().issuer != issuer ||
	  row->data().state != State::Active || row->data().owner ||
	  row->data().idleDeadline <= now ||
	  row->data().absoluteDeadline <= now) {
	complete(SessionError::Expired, Session{}, String{});
	return;
      }
      int64_t idle = now + idleLifetime;
      if (idle <= now || idle > row->data().absoluteDeadline)
	idle = row->data().absoluteDeadline;
      row->data().idleDeadline = idle;
      row->data().updated = now;
      ++row->data().version;
      Session session{row->data()};
      if (!row->commit()) {
	complete(SessionError::Storage, Session{}, String{});
	return;
      }
      complete(SessionError::OK, ZuMv(session), String{});
    });
  });
}

static void revoke_(DBContext *context, String token, String issuer, int64_t now,
    SessionDoneFn complete)
{
  Bytes id, digest;
  if (!context || !token || !issuer || now <= 0 ||
      !opaqueParse(token, id, digest)) {
    complete(SessionError::Invalid);
    return;
  }
  if (token.mutable_()) ZuClear(token.data(), token.length());
  auto sessions = context->sessions;
  sessions->run(0, [sessions, digest = ZuMv(digest), issuer = ZuMv(issuer), now,
      complete = ZuMv(complete)]() mutable {
    sessions->findUpd<0>(0, ZuFwdTuple(ZuMv(digest)), [issuer = ZuMv(issuer), now,
	  complete = ZuMv(complete)](ZdbRow<Session> *row) mutable {
      if (!row || row->data().issuer != issuer) {
	complete(SessionError::OK);
	return;
      }
      if (row->data().owner) {
	complete(SessionError::Storage);
	return;
      }
      if (row->data().state == State::Revoked) {
	complete(SessionError::OK);
	return;
      }
      row->data().state = State::Revoked;
      row->data().idleDeadline = now;
      row->data().updated = now;
      ++row->data().version;
      complete(row->commit() ? SessionError::OK : SessionError::Storage);
    });
  });
}

bool sessionIssue(Requests *requests, ZuTime deadline, DBContext *context,
    Ztls::Random &rng, SessionConfig config, SessionFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<SessionComplete_> state = new SessionComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, context, &rng,
      config = ZuMv(config)](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    issue_(context, rng, ZuMv(config), [state](int error,
	Session session, String token) mutable {
      state->complete(error, ZuMv(session), ZuMv(token));
    });
  }, [state]() mutable { state->cancel(); });
}

bool sessionIssueGrant(Requests *requests, ZuTime deadline,
    DBContext *context, Ztls::Random &rng, Bytes grantID, String issuer,
    int64_t now, int64_t idleLifetime, int64_t absoluteLifetime,
    SessionFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<SessionComplete_> state = new SessionComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, context, &rng,
      grantID = ZuMv(grantID), issuer = ZuMv(issuer), now,
      idleLifetime, absoluteLifetime](
      ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    if (!context || !grantID || !issuer) {
      state->complete(SessionError::Invalid);
      return;
    }
    auto grants = context->grants;
    grants->run(0, [state, context, grants, &rng,
	grantID = ZuMv(grantID), issuer = ZuMv(issuer), now,
	idleLifetime, absoluteLifetime]() mutable {
      grants->find<0>(0, ZuFwdTuple(ZuMv(grantID)), [state, context, &rng,
	  issuer = ZuMv(issuer), now, idleLifetime, absoluteLifetime](
	    ZdbRowRef<Grant> row) mutable {
	if (!row || row->data().kind != GrantKind::Code ||
	    row->data().purpose != GrantPurpose::Authorization ||
	    row->data().state != State::Active || row->data().owner ||
	    !row->data().userID || !row->data().userVersion ||
	    row->data().authTime <= 0) {
	  state->complete(SessionError::Invalid);
	  return;
	}
	Grant grant{row->data()};
	auto users = context->users;
	users->run(0, [state, context, users, &rng, grant = ZuMv(grant),
	    issuer = ZuMv(issuer), now, idleLifetime, absoluteLifetime]() mutable {
	  users->find<0>(0, ZuFwdTuple(grant.userID), [state, context, &rng,
	    grant = ZuMv(grant), issuer = ZuMv(issuer), now,
	    idleLifetime, absoluteLifetime](
	      ZdbRowRef<User> row) mutable {
	    if (!row || !row->data().handle) {
	      state->complete(SessionError::Invalid);
	      return;
	    }
	    String subject;
	    subject.length(ZuBase64URL::enclen(row->data().handle.length()));
	    subject.length(ZuBase64URL::encode(
	      subject.span(), row->data().handle));
	    issue_(context, rng, SessionConfig{
	      .issuer = ZuMv(issuer), .subject = ZuMv(subject),
	      .userID = grant.userID,
	      .providerID = grant.authorityProviderID,
	      .authTime = grant.authTime, .now = now,
	      .idleLifetime = idleLifetime,
	      .absoluteLifetime = absoluteLifetime,
	      .authVersion = grant.userVersion},
	      [state](int error, Session session, String token) mutable {
		state->complete(error, ZuMv(session), ZuMv(token));
	      });
	  });
	});
      });
    });
  }, [state]() mutable { state->cancel(); });
}

bool sessionUse(Requests *requests, ZuTime deadline, DBContext *context,
    String token, String issuer, int64_t now, int64_t idleLifetime,
    SessionFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<SessionComplete_> state = new SessionComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, context, token = ZuMv(token),
      issuer = ZuMv(issuer), now, idleLifetime](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    use_(context, ZuMv(token), ZuMv(issuer), now, idleLifetime,
      [state](int error, Session session, String token) mutable {
        state->complete(error, ZuMv(session), ZuMv(token));
      });
  }, [state]() mutable { state->cancel(); });
}

bool sessionRevoke(Requests *requests, ZuTime deadline, DBContext *context,
    String token, String issuer, int64_t now, SessionDoneFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<SessionDoneComplete_> state =
    new SessionDoneComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, context, token = ZuMv(token),
      issuer = ZuMv(issuer), now](
      ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    revoke_(context, ZuMv(token), ZuMv(issuer), now,
      [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

} // namespace Zum
