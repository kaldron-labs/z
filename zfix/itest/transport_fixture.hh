//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixTransportFixture_HH
#define ZfixTransportFixture_HH

#ifdef ZFIX_TEST_AFXDP
#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
#include <net/if.h>
#include <stdlib.h>
#include <unistd.h>
#endif

#include <zlib/ZiMultiplex.hh>

#ifdef ZFIX_TEST_AFXDP
#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>
#include <zlib/Zpkt.hh>
#include <zlib/ZpktEnv.hh>
#include "ZpktTestCf.hh"
#include "afxdp_wire.hh"
#endif

namespace ZfixTest {

#ifdef ZFIX_TEST_AFXDP
inline constexpr const char *ServerIP = "198.18.92.2";
inline constexpr const char *KernelPeerIP = "198.18.92.100";
enum { ServerPort = 19140, OtherPort = ServerPort + 1, SilentPort = ServerPort + 2 };
#else
inline constexpr const char *ServerIP = "127.0.0.1";
enum { ServerPort = 0, OtherPort = 0, SilentPort = 0 };
#endif

template <typename Link> void connect(Link *link, unsigned port)
{
#ifdef ZFIX_TEST_TLS
  link->connect("localhost", port, ZiIP{ServerIP});
#else
  link->connect(ServerIP, port);
#endif
}

class TransportFixture {
public:
  explicit TransportFixture(ZiMxParams params)
#ifdef ZFIX_TEST_AFXDP
  : m_mx{withSender(ZuMv(params))} { }
#else
  : m_mx{ZuMv(params)} { }
#endif

  ZiMultiplex &mx() { return m_mx; }

#ifdef ZFIX_TEST_AFXDP
  bool init(Zutcp::InstanceParams params = {}) {
    if (!resetDevice()) return false;
    unsigned index = if_nametoindex("zfixc");
    if (!index) {
      ZuTestUtil::log_("# missing AF_XDP fixture veth zfixc");
      return false;
    }
    auto cf = ZpktTest::config(ZFIX_XDP_MODULE, "zfixc", m_mx.params().nThreads());
    try { m_env->init(cf.ptr()); }
    catch (ZeException &e) { return failed(e); }
    auto opened = m_env->nif(&m_mx, "zfixc");
    if (opened.is<ZeException>()) return failed(opened.p<ZeException>());
    ZmRef<Zpkt::Nif> xdp = opened.p<Zpkt::Nif *>();
    ZmRef<Zutcp::Instance> tcp = new Zutcp::Instance;
    auto initialized = tcp->init(&m_mx, m_env, ZuMv(xdp), ZuMv(params));
    if (initialized.is<ZeException>()) {
      xdp->final();
      return failed(initialized.p<ZeException>());
    }
    m_mx.plugin(ZuMv(tcp));
    return true;
  }
#else
  bool init() { return true; }
#endif

private:
#ifdef ZFIX_TEST_AFXDP
  static bool resetDevice() {
    auto script = getenv("ZFIX_AFXDP_FIXTURE");
    if (!script || !*script) {
      ZuTestUtil::log_("# AF_XDP fixture requires transport-fixture.sh in a private network namespace");
      return false;
    }
    char shell[] = "bash", reset[] = "--reset";
    char *argv[] = {shell, script, reset, nullptr};
    pid_t child;
    if (int error = posix_spawnp(&child, shell, nullptr, nullptr, argv, environ)) {
      ZuTestUtil::log_("# AF_XDP fixture could not start setup, error ", error);
      return false;
    }
    int status;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited == child && WIFEXITED(status) && !WEXITSTATUS(status)) return true;
    ZuTestUtil::log_("# AF_XDP fixture device setup failed");
    return false;
  }

  static ZiMxParams withSender(ZiMxParams params) {
    auto &scheduler = params.scheduler();
    const auto original = scheduler;
    unsigned threads = original.nThreads();
    scheduler.nThreads(threads + 1);
    for (unsigned i = 0; i <= threads; ++i) scheduler.thread(i) = original.thread(i);
    scheduler.thread(threads + 1).isolated(true).name("zpktTx");
    return params;
  }

  static bool failed(const ZeException &error) {
    ZtString<> message;
    error.print(message);
    ZuTestUtil::log_("# AF_XDP fixture: ", message);
    return false;
  }
#endif

#ifdef ZFIX_TEST_AFXDP
  ZmRef<Zpkt::Env> m_env = new Zpkt::Env;
#endif
  ZiMultiplex m_mx;
};

} // namespace ZfixTest

#endif /* ZfixTransportFixture_HH */
