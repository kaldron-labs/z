//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZtelClient_HH
#define ZtelClient_HH

#ifndef ZcmdLib_HH
#include <zlib/ZcmdLib.hh>
#endif

#include <zlib/ZuLambdaTraits.hh>

#include <zlib/ZmFn.hh>

#include <zlib/ZtRegex.hh>

#include <zlib/ZvEngine.hh>

#include <zlib/Ztls.hh>

#include <zlib/Zfb.hh>

#include <zlib/Zdb.hh>

// #include <zlib/Zcmd.hh>
#include <zlib/Ztel.hh>

namespace Ztel {

enum { ReqIOBufSize = 128 }; // built-in I/O buffer size

using ReqIOBufAlloc = ZiIOBufAlloc<ReqIOBufSize>;
//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZcmdClient_HH
#define ZcmdClient_HH

#ifndef ZcmdLib_HH
#include <zlib/ZcmdLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmPLock.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiRx.hh>

#include <zlib/Zfb.hh>
#include <zlib/Ztls.hh>

#include <zlib/ZvCf.hh>

#include <zlib/Zum.hh>

#include <zlib/ZtelClient.hh>

#include <zlib/Zcmd.hh>
#include <zlib/ZcmdDispatcher.hh>

namespace Ztel {

// FIXME

// telemetry response
using AckFn = ZmFn<void(const Ztel::fbs::ReqAck *),
  ZmFnHeapID<"Ztel.Client.AckFn">>;

using KeyData = Zum::KeyData;

struct Login {
  String	user;
  String	passwd;
  unsigned	totp = 0;
};
struct Access {
  String	keyID;
  KeyData	token;
  int64_t	stamp;
  KeyData	hmac;
};
using Credentials = ZuUnion<Login, Access>;

template <typename App, typename Link> class Client;

template <
  typename App_,
  typename Impl_,
  typename IOBufAlloc_ = Ztls::RxBufAlloc<>,
  typename TxBufAlloc_ = Ztls::TxBufAlloc<>>
class CliLink :
    public Ztls::CliLink<App_, Impl_, IOBufAlloc_, TxBufAlloc_>,
    public ZiRx<CliLink<App_, Impl_, IOBufAlloc_, TxBufAlloc_>, IOBufAlloc_> {
public:
  using App = App_;
  using Impl = Impl_;
  using Base = Ztls::CliLink<App, Impl, IOBufAlloc_, TxBufAlloc_>;
  using IOBufAlloc = IOBufAlloc_;
  using TxBufAlloc = TxBufAlloc_;
  using Rx = ZiRx<CliLink, IOBufAlloc>;

  using Base::impl;
  using Base::app;
  using Base::connect;
  using Base::disconnect;
  using Base::send;
  using Base::send_;

friend Base;

template <typename, typename> friend class Client;

private:
  // containers of pending requests
  ZuDerive(UserDBReqs,
    (ZmRBTreeKV<ZvSeqNo, ZumAckFn,
      ZmRBTreeUnique<true,
	ZmRBTreeLock<ZmPLock>>>));
  ZuDerive(CmdReqs,
    (ZmRBTreeKV<ZvSeqNo, ZcmdAckFn,
      ZmRBTreeUnique<true,
	ZmRBTreeLock<ZmPLock>>>));
  ZuDerive(TelReqs,
    (ZmRBTreeKV<ZvSeqNo, ZtelAckFn,
      ZmRBTreeUnique<true,
	ZmRBTreeLock<ZmPLock>>>));

public:
  struct State {
    enum {
      Down = 0,
      Login,
      Up
    };
  };

  CliLink(App *app, Ztls::Host server, uint16_t port) :
      Base{app, ZuMv(server), port} { }

  // Note: the caller must ensure that calls to login()/access()
  // are not overlapped - until loggedIn()/connectFailed()/disconnected()
  // no further calls must be made
  template <typename User, typename Passwd>
  void login(User &&user, Passwd &&passwd, unsigned totp) {
    new (m_credentials.new_<Login>())
      Login{ZuFwd<User>(user), ZuFwd<Passwd>(passwd), totp};
    connect();
  }
  template <typename KeyID>
  void access(KeyID &&keyID, ZuCSpan secret_) {
    ZtArray<uint8_t> secret;
    secret.length(ZuBase64::declen(secret_.length()));
    secret.length(ZuBase64::decode(secret, secret_));
    KeyData token, hmac;
    token.length(token.size());
    hmac.length(hmac.size());
    app()->random(token);
    int64_t stamp = Zm::now().sec();
    {
      Ztls::HMAC<Zum::KeyType> hmac_;
      hmac_.start(secret);
      hmac_.update(token);
      hmac_.update(
	  {reinterpret_cast<const uint8_t *>(&stamp), sizeof(uint64_t)});
      hmac_.finish(hmac.data());
    }
    new (m_credentials.new_<Access>())
      Access{ZuFwd<KeyID>(keyID), ZuMv(token), stamp, ZuMv(hmac)};
    connect();
  }
  template <typename KeyID>
  void access_(
      KeyID &&keyID, KeyData &&token,
      int64_t stamp, KeyData &&hmac) {
    new (m_credentials.new_<Access>())
      Access{ZuFwd<KeyID>(keyID), ZuMv(token), stamp, ZuMv(hmac)};
    connect();
  }

  int state() const { return m_state; }

  // available once logged in
  uint64_t userID() const { return m_userID; }
  const String &userName() const { return m_userName; }
  const ZtArray<String> &roles() const { return m_roles; }
  const ZtBitmap &perms() const { return m_perms; }
  uint8_t flags() const { return m_userFlags; }

public:
  // send userDB request
  void sendUserDB(ZmRef<ZiIOBuf> buf, ZvSeqNo seqNo, ZumAckFn fn) {
    using namespace Zcmd;
    m_userDBReqs.add(seqNo, ZuMv(fn));
    send(saveHdr(ZuMv(buf), Type::userDB()));
  }
  // send command
  void sendCmd(ZmRef<ZiIOBuf> buf, ZvSeqNo seqNo, ZcmdAckFn fn) {
    using namespace Zcmd;
    m_cmdReqs.add(seqNo, ZuMv(fn));
    send(saveHdr(ZuMv(buf), Type::cmd()));
  }
  // send telemetry request
  void sendTelReq(ZmRef<ZiIOBuf> buf, ZvSeqNo seqNo, ZtelAckFn fn) {
    using namespace Zcmd;
    m_telReqs.add(seqNo, ZuMv(fn));
    send(saveHdr(ZuMv(buf), Type::telReq()));
  }

  void loggedIn() { } // default

  void connected(Zi::Connected info) {
    if (info.alpn != "zcmd") {
      disconnect();
      return;
    }

    scheduleTimeout();
    m_state = State::Login;

    // send login
    Zfb::IOBuilder fbb(new ReqIOBufAlloc());
    if (m_credentials.type() == Credentials::Index<Login>{}) {
      using namespace Zum::fbs;
      const auto &data = m_credentials.p<Login>();
      fbb.Finish(CreateLoginReq(fbb,
	  LoginReqData::Login,
	  CreateLogin(fbb,
	    Zfb::Save::str(fbb, data.user),
	    Zfb::Save::str(fbb, data.passwd),
	    data.totp).Union()));
    } else {
      using namespace Zum::fbs;
      const auto &data = m_credentials.p<Access>();
      fbb.Finish(CreateLoginReq(fbb,
	    LoginReqData::Access,
	    CreateAccess(fbb,
	      Zfb::Save::str(fbb, data.keyID),
	      Zfb::Save::bytes(fbb, data.token),
	      data.stamp,
	      Zfb::Save::bytes(fbb, data.hmac)).Union()));
    }
    send(Zcmd::saveHdr(fbb.buf(), Zcmd::Type::login()));
  }

  void disconnected(bool) {
    m_userDBReqs.clean();
    m_cmdReqs.clean();
    m_telReqs.clean();

    m_state = State::Down;

    cancelTimeout();

    m_rxBuf = nullptr;
  }

private:
  int loadBody(ZmRef<ZiIOBuf> buf) {
    return Zcmd::verifyHdr(ZuMv(buf),
      [](const Zcmd::Hdr *hdr, ZmRef<ZiIOBuf> buf, ZuBSpan out) {
	auto this_ = static_cast<CliLink *>(buf->owner);
	auto type = hdr->type;
	if (ZuUnlikely(this_->m_state.load_() == State::Login)) {
	  this_->cancelTimeout();
	  if (type != Zcmd::Type::login()) return -1;
	  return this_->processLoginAck(ZuMv(buf));
	}
	return this_->app()->dispatch(type, this_->impl(), ZuMv(buf), out);
      });
  }

public:
  int process(Ztls::RxStream &rx) {
    while (!rx.empty()) {
      int n = 0;
      int64_t consumed = rx.consume(
	[](ZuBSpan span) -> int64_t { return span.length(); },
	[this, &n](ZuBSpan span) { n = process(span.data(), span.length()); });
      if (ZuUnlikely(n < 0)) return -1;
      if (ZuUnlikely(consumed < 0)) return -1;
      if (!consumed) return 0;
    }
    return 1;
  }

  int process(const uint8_t *data, unsigned length) {
    if (ZuUnlikely(m_state.load_() == State::Down))
      return -1; // disconnect

    int i = Rx::template recvMem<
      Zcmd::loadHdr, &CliLink::loadBody>(data, length, m_rxBuf);

    if (ZuUnlikely(i < 0)) m_state = State::Down;
    return i;
  }

private:
  int processLoginAck(ZmRef<ZiIOBuf> buf) {
    using namespace Zum::fbs;
    {
      Verifier verifier{buf->data(), buf->length};
      if (!VerifyLoginAckBuffer(verifier)) return -1;
    }
    auto loginAck = GetLoginAck(buf->data());
    if (!loginAck->ok()) return -1;
    m_userID = loginAck->id();
    m_userName = Zfb::Load::str(loginAck->name());
    all(loginAck->roles(), [this](unsigned i, auto role_) {
      m_roles.push(str(role_));
    });
    m_perms = ZfbTransform::Bitmap::load<ZtBitmap>(loginAck->perms());
    m_userFlags = loginAck->flags();
    m_state = State::Up;
    impl()->loggedIn();
    return buf->length;
  }
  int processUserDB(ZmRef<ZiIOBuf> buf) {
    using namespace Zum::fbs;
    {
      Verifier verifier{buf->data(), buf->length};
      if (!VerifyReqAckBuffer(verifier)) return -1;
    }
    auto reqAck = GetReqAck(buf->data());
    if (ZumAckFn fn = m_userDBReqs.delVal(reqAck->seqNo()))
      fn(reqAck);
    return buf->length;
  }
  int processCmd(ZmRef<ZiIOBuf> buf, ZuBSpan out) {
    using namespace Zcmd::fbs;
    {
      Verifier verifier{buf->data(), buf->length};
      if (!VerifyReqAckBuffer(verifier)) return -1;
    }
    auto reqAck = GetReqAck(buf->data());
    if (ZcmdAckFn fn = m_cmdReqs.delVal(reqAck->seqNo()))
      fn(reqAck, ZuMv(buf), out);
    return buf->length;
  }
  int processTelReq(ZmRef<ZiIOBuf> buf) {
    using namespace Ztel::fbs;
    {
      Verifier verifier{buf->data(), buf->length};
      if (!VerifyReqAckBuffer(verifier)) return -1;
    }
    auto reqAck = GetReqAck(buf->data());
    if (ZtelAckFn fn = m_telReqs.delVal(reqAck->seqNo()))
      fn(reqAck);
    return buf->length;
  }
  // default telemetry handler skips message, does nothing
  int processTelemetry(ZmRef<ZiIOBuf> buf) { return buf->length; }

  void scheduleTimeout() {
    if (app()->timeout())
      app()->mx()->add(&m_timer, Zm::now(app()->timeout()),
	  ZmScheduler::Update,
	  [this](auto &&arm) {
	    return arm([link = ZmMkRef(impl())]() {
	      link->disconnect();
	    });
	  });
  }
  void cancelTimeout() {
    app()->mx()->del(&m_timer);
  }

private:
  ZmScheduler::Timer	m_timer;
  ZmAtomic<int>		m_state = State::Down;
  ZmRef<ZiIOBuf>	m_rxBuf;
  Credentials		m_credentials;
  UserDBReqs		m_userDBReqs;
  CmdReqs		m_cmdReqs;
  TelReqs		m_telReqs;
  uint64_t		m_userID = 0;
  String		m_userName;
  ZtArray<String>	m_roles;
  ZtBitmap		m_perms;
  uint8_t		m_userFlags = 0;
};

template <typename App_, typename Link_>
class Client :
  public Dispatcher,
  public Ztls::Client<App_> {
public:
  using App = App_;
  using Link = Link_;
  using TLS = Ztls::Client<App>;

  using TLS::app;

friend TLS;

  void init(ZiMultiplex *mx, const ZvCf *cf) {
    ZuCSpan alpn[] = { "zcmd" };

    Dispatcher::init();

    Dispatcher::map(Zcmd::Type::userDB(),
	[](void *link, ZmRef<ZiIOBuf> buf, ZuBSpan) {
	  return static_cast<Link *>(link)->processUserDB(ZuMv(buf));
	});
    Dispatcher::map(Zcmd::Type::cmd(),
	[](void *link, ZmRef<ZiIOBuf> buf, ZuBSpan out) {
	  return static_cast<Link *>(link)->processCmd(ZuMv(buf), out);
	});
    Dispatcher::map(Zcmd::Type::telReq(),
	[](void *link, ZmRef<ZiIOBuf> buf, ZuBSpan) {
	  return static_cast<Link *>(link)->processTelReq(ZuMv(buf));
	});
    Dispatcher::map(Zcmd::Type::telemetry(),
	[](void *link, ZmRef<ZiIOBuf> buf, ZuBSpan) {
	  return static_cast<Link *>(link)->processTelemetry(ZuMv(buf));
	});

    if (!TLS::init(
	  Ztls::ClientParams(
	    mx, cf->get("rxThread", true), cf->get("txThread", true)).alpn(alpn)
	    .caPath(cf->get("caPath"))))
      ZiLOG(Error, "Ztel", "TLS client initialization failed");

    m_reconnFreq = cf->getInt("reconnFreq", 0, 3600, 0);
    m_timeout = cf->getInt("timeout", 0, 3600, 0);
  }

  void final() {
    TLS::final();

    Dispatcher::final();
  }

  unsigned reconnFreq() const { return m_reconnFreq; }
  unsigned timeout() const { return m_timeout; }

private:
  unsigned		m_reconnFreq = 0;
  unsigned		m_timeout = 0;
};

} // Ztel

#endif /* ZtelClient_HH */
