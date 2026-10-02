//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <zlib/ZdbusAdapter.hh>
#include <zlib/ZdbusClient.hh>
#include <zlib/ZdbusServer.hh>

#include "ZdbusName.hh"

namespace Zdbus_ {

template <typename Heap = ZuVoid>
struct ServerClient_ : Heap {
  Client cli;
};
ZuDerive(ServerClientHeap, (ZmHeap<"Zdbus.ServerClient", ServerClient_<>>));
ZuDerive(ServerClient, (ServerClient_<ServerClientHeap>));

struct NameArgs {
  ZuCSpan	name;
  uint32_t	flags;
};
struct NameReply { uint32_t code; };

namespace NameReplyCode {
  // org.freedesktop.DBus.RequestName method-return values.
  enum { PrimaryOwner = 1, AlreadyOwner = 4 };
}

ZfStruct(, NameArgs,
  (((name), (Mutable)),		String),
  (((flags), (Mutable)),	UInt32));
ZfStructRender(, NameArgs, DBUS, name, flags);
ZfStruct(, NameReply, (((code), (Mutable)), UInt32));
ZfStructRender(, NameReply, DBUS, code);

using NameHeaders = ZuTypeList<
  HeaderEntry<Header::Path, ZuStringT<"/org/freedesktop/DBus">>,
  HeaderEntry<Header::Interface, ZuStringT<"org.freedesktop.DBus">>,
  HeaderEntry<Header::Member, ZuStringT<"RequestName">>,
  HeaderEntry<Header::Destination, ZuStringT<"org.freedesktop.DBus">>>;
struct NameReq : ReqBuilder<NameReq, NameArgs, NameHeaders> { };
struct NameRes : ResParser<NameRes, NameReply> { };

Server::~Server()
{
  ZmAssert_(!m_name.length() && !m_routes.count_() && !m_cli);
}

void Server::init(ZmScheduler *sched, unsigned rxSid, unsigned txSid,
  Address addr, ZuCSpan name, ServerParams params, ReadyFn readyFn,
  MethodFn methodFn, SignalFn signalFn, CxnFailFn failFn)
{
  ZmAssert_(!m_name.length() && !m_cli && name.length());
  m_name << name;
  m_readyFn = ZuMv(readyFn);
  m_methodFn = ZuMv(methodFn);
  m_failFn = ZuMv(failFn);
  m_params = params;
  if (!m_params.routeLimit) m_params.routeLimit = 1;
  m_cli = new ServerClient{};
  m_cli->cli.init(sched, rxSid, txSid, ZuMv(addr), params.cli,
    [this](ZuCSpan uniqueName) { connected_(uniqueName); },
    ZuMv(signalFn),
    [this](ZmRef<ZiIOBuf> frame, FrameInfo info) {
      method_(ZuMv(frame), info);
    },
    [this](CxnFailure failure) {
      if (m_failFn) m_failFn(failure);
    });
}

void Server::start() { m_started = true; m_cli->cli.start(); }
void Server::stop(CxnStopFn fn) { m_cli->cli.stop(ZuMv(fn)); }

bool Server::route(ZuCSpan path, ZuCSpan interface, ZuCSpan member,
  MethodFn fn)
{
  if (!m_name.length() || m_started ||
      !RouteText::fits(path, interface, member) ||
      !ZfDBUS::objectPath(path) ||
      !Name::interface(interface) || !Name::member(member) || !fn ||
      m_routes.count_() >= m_params.routeLimit ||
      m_routes.find(RouteKey{path, interface, member})) return false;
  m_routes.addNode(new Route{path, interface, member, ZuMv(fn)});
  return true;
}

void Server::method_(ZmRef<ZiIOBuf> frame, FrameInfo info)
{
  auto route = m_routes.find(RouteKey{info.headers.path,
    info.headers.interface, info.headers.member});
  if (route) {
    route->fn(ZuMv(frame), info);
    return;
  }
  if (m_methodFn) m_methodFn(ZuMv(frame), info);
}

void Server::final()
{
  m_cli->cli.final();
  delete m_cli;
  m_cli = nullptr;
  m_routes.clean();
  m_name.length(0);
  m_readyFn = {};
  m_methodFn = {};
  m_failFn = {};
  m_started = false;
}

void Server::call(BuildFn build, CallFn fn, ZuTime timeout)
{
  m_cli->cli.call(ZuMv(build), ZuMv(fn), timeout);
}

void Server::cancel(uint32_t serial, CxnSendFn done)
{
  m_cli->cli.cancel(serial, ZuMv(done));
}

void Server::send(BuildFn build, CxnSendFn fn)
{
  m_cli->cli.send(ZuMv(build), ZuMv(fn));
}

void Server::addMatch(ZuCSpan rule, CallFn fn, ZuTime timeout)
{
  m_cli->cli.addMatch(rule, ZuMv(fn), timeout);
}

void Server::removeMatch(ZuCSpan rule, CallFn fn, ZuTime timeout)
{
  m_cli->cli.removeMatch(rule, ZuMv(fn), timeout);
}

void Server::subscribe(ZuCSpan path, ZuCSpan interface, ZuCSpan member,
  SignalFn fn, SubDoneFn done)
{
  m_cli->cli.subscribe(path, interface, member, ZuMv(fn), ZuMv(done));
}

void Server::unsubscribe(ZuCSpan path, ZuCSpan interface, ZuCSpan member,
  uint64_t id, CxnSendFn done)
{
  m_cli->cli.unsubscribe(path, interface, member, id, ZuMv(done));
}

void Server::connected_(ZuCSpan)
{
  m_cli->cli.call([this](uint32_t serial) {
    NameArgs args{m_name, 0};
    NameReq req;
    req.init(&args);
    return req.build(serial);
  }, [this](CallResult result) { nameReply_(ZuMv(result)); });
}

void Server::nameReply_(CallResult result)
{
  // Stop/disconnect cancels the in-flight RequestName; it is not a bad reply.
  if (result.error == ClientError::Stopped ||
      result.error == ClientError::Disconnect) return;
  if (result && result.frame &&
      result.info.type == MessageType::MethodReturn &&
      result.info.headers.signature == "u") {
    NameReply reply;
    NameRes parser;
    parser.init(&reply);
    if (parser.load(ZuMv(result.frame), result.info)) {
      unsigned code = reply.code;
      if (code == NameReplyCode::PrimaryOwner ||
          code == NameReplyCode::AlreadyOwner) {
        if (m_readyFn) m_readyFn(m_name);
        return;
      }
    }
  }
  if (m_failFn) m_failFn(CxnFailure{0, 0, CxnError::Frame});
  m_cli->cli.stop();
}

} // Zdbus_
