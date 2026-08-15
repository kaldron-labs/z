//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZiResolver.hh>

using namespace ZuTestUtil;

namespace {

bool waitFor(ZmSemaphore &sem)
{
  return sem.timedwait(Zm::now(5)) == 0;
}

ZiIP ip4(uint32_t n)
{
  in_addr addr;
  addr.s_addr = htonl(n);
  return ZiIP{addr};
}

ZiIP ip6(const uint8_t (&bytes)[16])
{
  in6_addr addr;
  for (unsigned i = 0; i < sizeof(addr.s6_addr); i++)
    addr.s6_addr[i] = bytes[i];
  return ZiIP{addr};
}

class FakeDNS {
public:
  FakeDNS(bool dropTXT = false) : m_dropTXT{dropTXT}
  {
    m_socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (Zi::nullSocket(m_socket)) return;

    timeval tv{0, 100000};
    ::setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(
	m_socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
      return;

    socklen_t len = sizeof(addr);
    if (::getsockname(
	m_socket, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
      return;
    m_port = ntohs(addr.sin_port);

    m_thread = ZmThread{[this]() { run(); }};
    m_ok = true;
  }

  ~FakeDNS()
  {
    m_stopping.store_(1);
    if (!Zi::nullSocket(m_socket)) Zi::closeSocket(m_socket);
    m_thread.join();
  }

  Zi::Name servers() const {
    Zi::Name s;
    s << "127.0.0.1:" << ZuBoxed(m_port);
    return s;
  }

  bool ok() const { return m_ok; }
  unsigned a() const { return m_a.load_(); }
  unsigned aaaa() const { return m_aaaa.load_(); }
  bool waitTXT() { return m_txtSeen.timedwait(Zm::now(5)) == 0; }

private:
  static uint16_t u16(const uint8_t *ptr) {
    return (uint16_t(ptr[0]) << 8) | uint16_t(ptr[1]);
  }

  static void put16(uint8_t *&ptr, uint16_t v) {
    *ptr++ = uint8_t(v >> 8);
    *ptr++ = uint8_t(v);
  }

  static void put32(uint8_t *&ptr, uint32_t v) {
    *ptr++ = uint8_t(v >> 24);
    *ptr++ = uint8_t(v >> 16);
    *ptr++ = uint8_t(v >> 8);
    *ptr++ = uint8_t(v);
  }

  static void put(uint8_t *&ptr, ZuBSpan data) {
    for (unsigned i = 0, n = data.length(); i < n; i++) *ptr++ = data[i];
  }

  static void putName(uint8_t *&ptr, ZuCSpan name) {
    unsigned off = 0, n = name.length();
    while (off < n) {
      unsigned end = off;
      while (end < n && name[end] != '.') ++end;
      *ptr++ = uint8_t(end - off);
      for (unsigned i = off; i < end; i++) *ptr++ = name[i];
      off = end + 1;
    }
    *ptr++ = 0;
  }

  bool question(const uint8_t *buf, unsigned len, unsigned &end, uint16_t &type)
  {
    unsigned off = 12;
    while (off < len && buf[off]) off += unsigned(buf[off]) + 1;
    if (off >= len || off + 5 > len) return false;
    ++off;
    type = u16(buf + off);
    end = off + 4;
    return true;
  }

  void run()
  {
    static const uint8_t v6[] = {
      0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0x53
    };

    while (!m_stopping.load_()) {
      uint8_t buf[512];
      sockaddr_in from;
      socklen_t fromLen = sizeof(from);
      int n = ::recvfrom(
	m_socket, buf, sizeof(buf), 0,
	reinterpret_cast<sockaddr *>(&from), &fromLen);
      if (n <= 0) continue;

      unsigned qEnd = 0;
      uint16_t qType = 0;
      if (n < 12 || !question(buf, unsigned(n), qEnd, qType)) continue;
      if (qType == ZiDNSType::A) ++m_a;
      else if (qType == ZiDNSType::AAAA) ++m_aaaa;
      else if (qType == ZiDNSType::TXT) {
	m_txtSeen.post();
	if (m_dropTXT) continue;
      }

      uint8_t out[512];
      uint8_t *ptr = out;
      put(ptr, ZuBSpan{buf, 2});
      put16(ptr, 0x8180);
      put16(ptr, 1);
      put16(ptr,
	(qType == ZiDNSType::A || qType == ZiDNSType::AAAA ||
	 qType == ZiDNSType::PTR) ? 1 : 0);
      put16(ptr, 0);
      put16(ptr, 0);
      put(ptr, ZuBSpan{buf + 12, qEnd - 12});
      if (qType == ZiDNSType::A || qType == ZiDNSType::AAAA ||
	  qType == ZiDNSType::PTR) {
	put16(ptr, 0xc00c);
	put16(ptr, qType);
	put16(ptr, ZiDNSClass::IN);
	put32(ptr, 0);
	if (qType == ZiDNSType::A) {
	  put16(ptr, 4);
	  *ptr++ = 192; *ptr++ = 0; *ptr++ = 2; *ptr++ = 53;
	} else if (qType == ZiDNSType::AAAA) {
	  put16(ptr, 16);
	  put(ptr, ZuBSpan{v6, sizeof(v6)});
	} else {
	  uint8_t *len = ptr;
	  ptr += 2;
	  uint8_t *rdata = ptr;
	  putName(ptr, "resolver.test");
	  uint16_t n = ptr - rdata;
	  len[0] = uint8_t(n >> 8);
	  len[1] = uint8_t(n);
	}
      }
      ::sendto(
	m_socket, out, ptr - out, 0,
	reinterpret_cast<sockaddr *>(&from), fromLen);
    }
  }

private:
  Zi::Socket		m_socket = Zi::nullSocket();
  uint16_t		m_port = 0;
  ZmThread		m_thread;
  bool			m_ok = false;
  bool			m_dropTXT = false;
  ZmSemaphore		m_txtSeen;
  ZmAtomic<unsigned>	m_stopping = 0;
  ZmAtomic<unsigned>	m_a = 0;
  ZmAtomic<unsigned>	m_aaaa = 0;
};

void testLifecycle()
{
  ZuTestScope(testLifecycle);

  ZmSemaphore started;
  ZmSemaphore stopped;
  ZmAtomic<unsigned> failed = 0;

  ZiResolver::final();
  ZiResolver::init(
    ZiResolverParams{}.scheduler([](auto &s) { s.queueSize(4096); }),
    ZiEvent::FailFn{[&failed](ZeException) { failed.store_(1); }});
  ZuCheck(ZiResolver::instance()->initialized());

  ZiResolver::start([&failed, &started](ZiEvent::StartResult result) {
    if (result.is<ZiEvent::Exception>()) failed.store_(1);
    started.post();
  });
  ZuCheck(waitFor(started));
  ZuCheck(ZiResolver::instance()->running());
  ZuCheck(!failed.load_());

  ZiResolver::start([&failed, &started](ZiEvent::StartResult result) {
    if (result.is<ZiEvent::Exception>()) failed.store_(1);
    started.post();
  });
  ZuCheck(waitFor(started));
  ZuCheck(!failed.load_());

  ZiResolver::stop([&failed, &stopped](ZiEvent::StopResult result) {
    if (result.is<ZiEvent::Exception>()) failed.store_(1);
    stopped.post();
  });
  ZuCheck(waitFor(stopped));
  ZuCheck(!ZiResolver::instance()->running());
  ZuCheck(!failed.load_());

  ZiResolver::stop([&failed, &stopped](ZiEvent::StopResult result) {
    if (result.is<ZiEvent::Exception>()) failed.store_(1);
    stopped.post();
  });
  ZuCheck(waitFor(stopped));
  ZiResolver::final();
  ZuCheck(!ZiResolver::instance()->initialized());
}

void testLazyNumericResolve()
{
  ZuTestScope(testLazyNumericResolve);

  ZmSemaphore done;
  ZmAtomic<unsigned> failed = 0;
  ZiIP out;

  ZiResolver::final();
  ZiResolver::resolve("127.0.0.1",
    ZiResolver_::ResolveFn{[&failed, &out, &done](auto result) {
      if (result.template is<ZiResolver_::Event>()) failed.store_(1);
      else if (!result.template is<void>()) out = result.template p<ZiIP>();
      done.post();
      return false;
    }});

  ZuCheck(waitFor(done));
  ZuCheck(!failed.load_());
  ZuCheck(out == ip4(0x7f000001U));
  ZuCheck(ZiResolver::instance()->initialized());
  ZiResolver::stop();
  ZiResolver::final();
}

void testIPv6NumericResolve()
{
  ZuTestScope(testIPv6NumericResolve);

  static const uint8_t loopback[] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1
  };

  ZmSemaphore done;
  ZmAtomic<unsigned> failed = 0;
  ZiIP out;

  ZiResolver::final();
  ZiResolver::init(ZiResolverParams{}.ipv4(false).ipv6(true));
  ZiResolver::resolve("::1",
    ZiResolver_::ResolveFn{[&failed, &out, &done](auto result) {
      if (result.template is<ZiResolver_::Event>()) failed.store_(1);
      else if (!result.template is<void>()) out = result.template p<ZiIP>();
      done.post();
      return false;
    }});

  ZuCheck(waitFor(done));
  ZuCheck(!failed.load_());
  ZuCheck(out == ip6(loopback));
  ZiResolver::stop();
  ZiResolver::final();
}

void testIPv4OnlyResolve()
{
  ZuTestScope(testIPv4OnlyResolve);

  FakeDNS dns;
  ZuCheck(dns.ok());
  ZmSemaphore done;
  ZmAtomic<unsigned> failed = 0;
  ZiIP out;

  ZiResolver::final();
  ZiResolver::init(ZiResolverParams{}.ipv4(true).ipv6(false).
    timeoutMS(500).tries(1).servers(dns.servers()));
  ZiResolver::resolve("resolver.test",
    ZiResolver_::ResolveFn{[&failed, &out, &done](auto result) {
      if (result.template is<ZiResolver_::Event>()) failed.store_(1);
      else if (!result.template is<void>()) out = result.template p<ZiIP>();
      done.post();
      return false;
    }});

  ZuCheck(waitFor(done));
  ZuCheck(!failed.load_());
  ZuCheck(out.type() == ZiIPType::V4);
  ZuCheck(out == ip4(0xc0000235U));
  ZuCheck(dns.a() > 0);
  ZuCheck(dns.aaaa() == 0);
  ZiResolver::stop();
  ZiResolver::final();
}

void testIPv6EnabledResolve()
{
  ZuTestScope(testIPv6EnabledResolve);

  static const uint8_t v6[] = {
    0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0x53
  };

  FakeDNS dns;
  ZuCheck(dns.ok());
  ZmSemaphore done;
  ZmAtomic<unsigned> failed = 0;
  ZmAtomic<unsigned> count = 0;
  ZmAtomic<unsigned> have4 = 0;
  ZmAtomic<unsigned> have6 = 0;

  ZiResolver::final();
  ZiResolver::init(ZiResolverParams{}.ipv4(true).ipv6(true).
    timeoutMS(500).tries(1).servers(dns.servers()));
  ZiResolver::resolve("resolver.test",
    ZiResolver_::ResolveFn{[&failed, &count, &have4, &have6, &done](
	auto result) {
      if (result.template is<ZiResolver_::Event>()) {
	failed.store_(1);
	done.post();
	return false;
      }
      if (result.template is<void>()) return false;
      ZiIP ip = result.template p<ZiIP>();
      if (ip == ip4(0xc0000235U)) have4.store_(1);
      if (ip == ip6(v6)) have6.store_(1);
      if (++count >= 2) {
	done.post();
	return false;
      }
      return true;
    }});

  ZuCheck(waitFor(done));
  ZuCheck(!failed.load_());
  ZuCheck(have4.load_());
  ZuCheck(have6.load_());
  ZuCheck(dns.a() > 0);
  ZuCheck(dns.aaaa() > 0);
  ZiResolver::stop();
  ZiResolver::final();
}

void testZiIPBlockingNumeric()
{
  ZuTestScope(testZiIPBlockingNumeric);

  ZeError e;
  ZiIP ip;

  ZuCheck(ip.resolve("127.0.0.1", &e) == Zi::OK);
  ZuCheck(ip == ip4(0x7f000001U));

  ZiIP ip_;
  ZuCheck(ip_.resolve("::1", &e) == Zi::OK);
  ZuCheck(ip_.type() == ZiIPType::V6);
}

void testReverseLookup()
{
  ZuTestScope(testReverseLookup);

  static const uint8_t v6[] = {
    0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0x53
  };

  FakeDNS dns;
  ZuCheck(dns.ok());
  ZmSemaphore done;
  ZmAtomic<unsigned> failed = 0;
  ZmAtomic<unsigned> count = 0;

  ZiResolver::final();
  ZiResolver::init(ZiResolverParams{}.timeoutMS(500).tries(1).
    servers(dns.servers()));
  ZiResolver::name(ip4(0xc0000235U),
    ZiResolver_::NameFn{[&failed, &count, &done](auto result) {
      if (result.template is<ZiResolver_::Event>()) {
	failed.store_(1);
	done.post();
	return;
      }
      if (result.template p<ZiResolver_::Host>() != "resolver.test")
	failed.store_(1);
      if (++count == 2) done.post();
    }});
  ZiResolver::name(ip6(v6),
    ZiResolver_::NameFn{[&failed, &count, &done](auto result) {
      if (result.template is<ZiResolver_::Event>()) {
	failed.store_(1);
	done.post();
	return;
      }
      if (result.template p<ZiResolver_::Host>() != "resolver.test")
	failed.store_(1);
      if (++count == 2) done.post();
    }});

  ZuCheck(waitFor(done));
  ZuCheck(!failed.load_());
  ZuCheck(count.load_() == 2);
  ZiResolver::stop();
  ZiResolver::final();
}

void testNoCallbackAfterStop()
{
  ZuTestScope(testNoCallbackAfterStop);

  FakeDNS dns{true};
  ZuCheck(dns.ok());
  ZmSemaphore stopped;
  ZmSemaphore callback;
  ZmAtomic<unsigned> callbacks = 0;

  ZiResolver::final();
  ZiResolver::init(ZiResolverParams{}.timeoutMS(1000).tries(1).
    servers(dns.servers()));
  ZiResolver::query("resolver.test",
    ZiDNSType::TXT, ZiDNSClass::IN,
    ZiResolver_::QueryFn{[&callbacks, &callback](auto) {
      ++callbacks;
      callback.post();
    }});
  ZiResolver::stop([&stopped](ZiEvent::StopResult) {
    stopped.post();
  });
  ZuCheck(waitFor(stopped));
  while (callback.trywait() == 0);
  unsigned n = callbacks.load_();
  ZuCheck(callbacks.load_() == n);
  ZiResolver::final();
}

void testCancel()
{
  ZuTestScope(testCancel);

  FakeDNS dns{true};
  ZuCheck(dns.ok());
  ZmSemaphore callback;
  ZmAtomic<unsigned> callbacks = 0;

  ZiResolver::final();
  ZiResolver::init(ZiResolverParams{}.timeoutMS(1000).tries(1).
    servers(dns.servers()));
  auto handle = ZiResolver::query("resolver.test",
    ZiDNSType::TXT, ZiDNSClass::IN,
    ZiResolver_::QueryFn{[&callbacks, &callback](auto) {
      ++callbacks;
      callback.post();
    }});
  ZuCheck(dns.waitTXT());
  ZiResolver::cancel(ZuMv(handle));
  ZiResolver::stop();
  ZuCheck(callback.trywait() != 0);
  ZuCheck(!callbacks.load_());
  ZiResolver::final();
}

void testTXTParse()
{
  ZuTestScope(testTXTParse);

  static const uint8_t payload[] = {
    0x12, 0x34, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00,
    0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
    0x03, 'c', 'o', 'm', 0x00,
    0x00, 0x10, 0x00, 0x01,
    0xc0, 0x0c, 0x00, 0x10, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x0c,
    0x05, 'h', 'e', 'l', 'l', 'o',
    0x05, 'w', 'o', 'r', 'l', 'd'
  };

  ZiDNSMsg msg;
  msg.name = "example.com";
  msg.type = ZiDNSType::TXT;
  msg.class_ = ZiDNSClass::IN;
  msg.buf.length(sizeof(payload));
  for (unsigned i = 0; i < sizeof(payload); i++) msg.buf[i] = payload[i];

  ZmSemaphore done;
  ZmAtomic<unsigned> failed = 0;
  unsigned n = 0;
  ZiResolver::txt(msg, ZiResolver_::TxtFn{[&failed, &n, &done](
      auto result) {
    if (result.template is<ZiResolver_::Event>()) {
      failed.store_(1);
      done.post();
      return false;
    }
    if (result.template is<void>()) {
      done.post();
      return false;
    }
    ZuBSpan s = result.template p<ZuBSpan>();
    switch (n++) {
      case 0:
	return s == "hello";
      case 1:
	return s == "world";
      default:
	return false;
    }
  }});
  ZuCheck(waitFor(done));
  ZuCheck(!failed.load_());
  ZuCheck(n == 2);
  ZiResolver::stop();
  ZiResolver::final();
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testLifecycle);
  ZuTestCall(testLazyNumericResolve);
  ZuTestCall(testIPv6NumericResolve);
  ZuTestCall(testIPv4OnlyResolve);
  ZuTestCall(testIPv6EnabledResolve);
  ZuTestCall(testZiIPBlockingNumeric);
  ZuTestCall(testReverseLookup);
  ZuTestCall(testNoCallbackAfterStop);
  ZuTestCall(testCancel);
  ZuTestCall(testTXTParse);
  return 0;
}
