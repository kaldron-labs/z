//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Print the current session bus ID through a typed D-Bus method call.

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZdbusClient.hh>

namespace ZdbusGetId {

enum { TimeoutSeconds = 5 };

struct Empty { };
ZuTypeList<> ZuFields_(Empty *, ZuFacet::DBUS *);

struct IDReply {
  ZtString<ZtStringHeapID<"ZdbusGetId.Value">> value;
};
ZfStruct(, IDReply, (((value), (Mutable)), (String)));
ZfStructRender(, IDReply, DBUS, value);

using GetIdHeaders = ZuTypeList<
  ZdbusHeaderEntry<ZdbusHeaderPath,
    ZuStringT<"/org/freedesktop/DBus">>,
  ZdbusHeaderEntry<ZdbusHeaderInterface,
    ZuStringT<"org.freedesktop.DBus">>,
  ZdbusHeaderEntry<ZdbusHeaderMember,
    ZuStringT<"GetId">>,
  ZdbusHeaderEntry<ZdbusHeaderDestination,
    ZuStringT<"org.freedesktop.DBus">>>;

struct GetIdRes : ZdbusResParser<GetIdRes, IDReply> { };
struct GetIdReq : ZdbusReqBuilder<GetIdReq, Empty, GetIdHeaders> {
  using Responses = ZuTypeList<GetIdRes>;
};

bool waitFor(ZmSemaphore &sem)
{
  return sem.timedwait(Zm::now(TimeoutSeconds)) == 0;
}

} // ZdbusGetId

using namespace ZdbusGetId;

int main()
{
  ZdbusAddress address;
  if (!ZdbusAddress::session(address)) {
    auto err = ZiFile::stdErr();
    err << "DBUS_SESSION_BUS_ADDRESS is missing or unsupported\n";
    return 2;
  }

  ZmScheduler sched{ZmSchedParams().id("ZdbusGetId").nThreads(2)};
  ZdbusClient cli;
  ZmSemaphore ready, finished, stopped;
  ZmAtomic<unsigned> failed = 0;
  IDReply reply;
  bool returned = false;

  sched.start();
  cli.init(&sched, 1, 2, ZuMv(address), {},
    [&ready](ZuCSpan) { ready.post(); }, {}, {},
    [&failed, &ready](auto) {
      failed.store_(1);
      ready.post();
    });
  cli.start();
  bool connected = waitFor(ready) && !failed.load_();
  if (connected) {
    cli.call<GetIdReq>([](uint32_t serial) {
      Empty body;
      GetIdReq req;
      req.init(&body);
      return req.build(serial);
    }, [&reply, &returned, &finished](auto result) {
      using Value = ZuDecay<decltype(result)>;
      if constexpr (ZuIsSame<Value, ZdbusParsed<GetIdRes>>{}) {
        reply = ZuMv(result.object);
        returned = true;
      }
      finished.post();
    }, Zm::now(TimeoutSeconds));
  }
  bool completed = connected && waitFor(finished);
  cli.stop([&stopped] { stopped.post(); });
  stopped.wait();
  cli.final();
  sched.stop();

  if (!completed || !returned) {
    auto err = ZiFile::stdErr();
    err << "D-Bus GetId failed\n";
    return 1;
  }
  auto out = ZiFile::stdOut();
  if (out.write(reply.value.data(), reply.value.length()) != Zi::OK ||
      out.write("\n", 1) != Zi::OK) return 1;
  return 0;
}
