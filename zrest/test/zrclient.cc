//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmTrap.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfJSON.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiMultiplex.hh>

#include <zlib/Zrest.hh>

// FIXME - ZtString<>
struct Credentials {
  ZtString<>	username;
  ZtString<>	password;
};
ZfStruct(Credentials,
  (((username),	(Ctor<0>)), (String)),
  (((password),	(Ctor<1>)), (String)));

struct AuthResponse {
  ZtString<>	token;

  friend ZfStructPrint ZuPrintType(AuthResponse *);
};
ZfStruct(AuthResponse,
  (((token),	(Ctor<0>)), (String)));

struct ProtectedResponse {
  ZtString<>	message;
  ZtString<>	data;

  friend ZfStructPrint ZuPrintType(ProtectedResponse *);
};
ZfStruct(ProtectedResponse,
  (((message),	(Ctor<0>)), (String)),
  (((data),	(Ctor<1>)), (String)));

class Client;

/* using IOBufAlloc = Ztls::RxBufAlloc<Size, MaxSize, HeapID>; */

class Link : public Zrest::CliLink<Client, Link /* , IOBufAlloc */> {
public:
  using Base = Zrest::CliLink<Client, Link>;
  using Base::app;
  using Base::state;
  using Base::host;
  using Base::header;
  using Base::body;
  using Base::up;

  Link(Client *client, ZtString<> server, uint16_t port) :
    Base{client, ZuMv(server), port} { }

  void connected(Zi::Connected);
  void connectFailed(bool transient);
  void disconnected(bool);

  bool rcvd();

private:
  ZtString<>		m_token;
};

class Client : public ZmPolymorph, public Zrest::Client<Client, Link> {
public:
  using Base = Zrest::Client<Client, Link>;
  using Base::init;

  void final() {
    m_link = nullptr;
    Base::final();
  }

  void login(ZtString<> server, uint16_t port) {
    m_link = new Link{this, ZuMv(server), port};
    m_link->connect();
  }

  void disconnect() { if (m_link) m_link->disconnect(); }

  int state() const {
    if (!m_link) return ZvLinkState::Down;
    return m_link->state();
  }

  void wait() { m_done.wait(); }
  void done() { m_done.post(); }

  void sigint() { m_done.post(); }

private:
  ZmSemaphore		m_done;
  // ZmSemaphore	m_executed;

  ZmRef<Link>		m_link;
};

void Link::connected(Zi::Connected info)
{
  Base::connected(info);
  send_(request(Zhttp::Method::POST, "/api/auth", [](ZiIOBuf &buf) {
    ZfJSON::save(buf, Credentials{"test", "test123"});
  }));
}

void Link::connectFailed(bool transient)
{
  Base::connectFailed(transient);
  if (!transient || !app()->reconnFreq()) app()->done();
}

void Link::disconnected(bool peer)
{
  Base::disconnected(peer);
  app()->done();
}

bool Link::rcvd()
{
  auto scan = ZfJSON::scan(body().data);
  if (scan.p<0>() < 0) {
    ZiLOG(Error, "zrclient", "invalid response");
    return false;
  }
  if (state() == ZvLinkState::Connecting) {
    auto response = ZfJSON::handler<AuthResponse>(scan.p<1>()).ctor();
    std::cout << response << '\n';
    m_token = ZuMv(response.token);
    up();
    send(request_<"authorization">(
	Zhttp::Method::GET, "/api/protected", m_token));
  } else {
    std::cout << ZfJSON::handler<ProtectedResponse>(scan.p<1>()).ctor() << '\n';
    app()->done();
  }

  return true;
}

ZmRef<Client> client;

void sigint() { if (client) client->sigint(); }

static void usage()
{
  static const char *usage =
    "Usage: zrlclient\n";
  std::cerr << usage << std::flush;
  ZiLog::stop();
  Zm::exit(1);
}

int main(int argc, char **argv)
{
  if (argc != 1) usage();

  ZiLog::init("zrclient");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZuPtr<ZiMultiplex> mx = new ZiMultiplex(
    ZiMxParams()
      .scheduler([](auto &s) {
	s.nThreads(4)
	  .thread(1, [](auto &t) { t.isolated(1); })
	  .thread(2, [](auto &t) { t.isolated(1); })
	  .thread(3, [](auto &t) { t.isolated(1); })
	  .thread(4, [](auto &t) { t.isolated(1); }); })
      .rxThread(1).txThread(2));

  mx->start();

  client = new Client();

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  {
    ZmRef<ZvCf> cf = new ZvCf();
    cf->set("timeout", "1");
    cf->set("rxThread", "3");
    cf->set("txThread", "4");
    if (auto caPath = ::getenv("ZREST_CAPATH"))
      cf->set("caPath", caPath);
    else
      cf->set("caPath", "/etc/ssl/certs");
    try {
      client->init(mx, cf);
    } catch (const ZeException &e) {
      std::cerr << e << '\n' << std::flush;
      ::exit(1);
    } catch (const ZtString<> &e) {
      std::cerr << e << '\n' << std::flush;
      ::exit(1);
    } catch (...) {
      std::cerr << "unknown exception\n" << std::flush;
      ::exit(1);
    }
  }

  client->login("localhost", 8443);

  client->wait();

  if (client->state() == ZvLinkState::Up) {
    client->disconnect();
    client->wait();
  }

  mx->stop();

  ZiLog::stop();

  client->final();
  client = {};

  ZmTrap::sigintFn(nullptr);

  return 0;
}
