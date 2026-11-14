//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details).

// An independent ordinary kernel TCP peer. Zi is used only for files and logging.
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <iostream>

#include <zlib/ZiFile.hh>
#include <zlib/ZuLib.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiLog.hh>

ZuDerive(PeerBytes, (ZtString<ZtStringHeapID<"Zutcp.KernelPeer.Bytes">>));
// Existing bounded utility request/prefix and ordinary kernel accept backlog.
enum { MaxRequest = 1<<20, PrefixBytes = 260, ReadSize = 4096, ListenBacklog = 16 };

static bool waitGate(const char *name)
{
  const char *path = getenv(name);
  if (!path) return true;
  ZiFile gate{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!gate) {
    ZiLOG(Error, "zutcpkernelpeer", ([error = gate.error()](auto &out) {
      out << "fixture gate: " << error;
    }));
    return false;
  }
  char value;
  return gate.read(&value, 1) == 1 && value == 'R';
}

class PeerSocket {
public:
  explicit PeerSocket(int fd) : m_fd{fd} { }
  PeerSocket(const PeerSocket &) = delete;
  PeerSocket &operator =(const PeerSocket &) = delete;
  ~PeerSocket() { if (m_fd >= 0) close(m_fd); }
  int fd() const { return m_fd; }
private:
  int		m_fd = -1;
};

static bool failed(const char *operation)
{
  ZiLOG(Error, "zutcpkernelpeer", ([error = ZeLastError, operation = ZeString{operation}]
    (auto &out) { out << operation << ": " << error; }));
  return false;
}

static bool readFile(const char *name, PeerBytes &bytes)
{
  ZiFile file{name, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) {
    ZiLOG(Error, "zutcpkernelpeer", ([error = file.error()](auto &out) {
      out << "request file: " << error;
    }));
    return false;
  }
  char buf[ReadSize]; // Cold bounded utility input in4K chunks.
  while (true) {
    int n = file.read(buf, sizeof(buf), false);
    if (n == Zi::EndOfFile) return true;
    if (n < 0 || bytes.length() + unsigned(n) > MaxRequest) {
      ZiLOG(Error, "zutcpkernelpeer", ([](auto &out) {
	out << "request exceeds bounded utility input or could not be read";
      }));
      return false;
    }
    bytes << ZuCSpan{buf, unsigned(n)};
  }
}

static bool sendAll(int fd, const char *data, unsigned bytes)
{
  while (bytes) {
    auto n = send(fd, data, bytes, MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return failed("send");
    data += n;
    bytes -= unsigned(n);
  }
  return true;
}

static bool requestEOF(int fd, PeerBytes &request, bool *reset = nullptr)
{
  char bytes[ReadSize];
  while (true) {
    auto n = recv(fd, bytes, sizeof(bytes), 0);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && errno == ECONNRESET && reset) { *reset = true; return true; }
    if (n < 0) return failed("recv request");
    if (!n) return true;
    if (request.length() + unsigned(n) > MaxRequest) {
      ZiLOG(Error, "zutcpkernelpeer", ([](auto &out) {
      out << "request exceeds bounded utility input";
    }));
      return false;
    }
    request << ZuCSpan{bytes, unsigned(n)};
  }
}

static bool responseEOF(int fd)
{
  char bytes[ReadSize];
  while (true) {
    auto n = recv(fd, bytes, sizeof(bytes), 0);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) return failed("recv response");
    if (!n) { std::cout << std::flush; return bool(std::cout); }
    std::cout << ZuCSpan{bytes, unsigned(n)};
    if (!std::cout) return failed("stdout");
  }
}

static void usage()
{
  std::cerr << "Usage: zutcpkernelpeer server ADDRESS PORT COUNT\n"
    "       zutcpkernelpeer client ADDRESS PORT REQUEST_FILE [COUNT]\n";
  exit(2);
}

int main(int argc, char **argv)
{
  if (argc != 5 && argc != 6) usage();
  bool server = !strcmp(argv[1], "server");
  if (!server && strcmp(argv[1], "client")) usage();
  if (server && argc != 5) usage();
  unsigned port = strtoul(argv[3], nullptr, 10);
  unsigned count = server ? strtoul(argv[4], nullptr, 10) :
    (argc == 6 ? strtoul(argv[5], nullptr, 10) : 1);
  if (!port || port > 65535 || !count) usage();
  sockaddr_storage address{};
  socklen_t length;
  int family;
  auto &v4 = reinterpret_cast<sockaddr_in &>(address);
  auto &v6 = reinterpret_cast<sockaddr_in6 &>(address);
  if (inet_pton(AF_INET, argv[2], &v4.sin_addr) == 1) {
    family = AF_INET;
    v4.sin_family = AF_INET;
    v4.sin_port = htons(uint16_t(port));
    length = sizeof(v4);
  } else if (inet_pton(AF_INET6, argv[2], &v6.sin6_addr) == 1) {
    family = AF_INET6;
    v6.sin6_family = AF_INET6;
    v6.sin6_port = htons(uint16_t(port));
    length = sizeof(v6);
  } else { usage(); return 2; }
  ZiLog::init("zutcpkernelpeer");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuGuard logStop{[] { ZiLog::stop(); }};
  signal(SIGPIPE, SIG_IGN);
  PeerBytes request;
  if (!server && !readFile(argv[4], request)) return 1;
  if (!server && (getenv("ZUTCP_KERNEL_TFO") || getenv("ZUTCP_KERNEL_RESET_GATE")) &&
      request.length() < PrefixBytes) {
    ZiLOG(Error, "zutcpkernelpeer", ([](auto &out) {
      out << "fixture request is shorter than its bounded prefix";
    }));
    return 1;
  }
  if (server) {
    PeerSocket listener{socket(family, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP)};
    if (listener.fd() < 0) { failed("socket"); return 1; }
    int enabled = 1;
    if (getenv("ZUTCP_KERNEL_TFO")) {
      int backlog = ListenBacklog;
      if (setsockopt(listener.fd(), IPPROTO_TCP, TCP_FASTOPEN, &backlog, sizeof(backlog))) {
	failed("kernel TCP_FASTOPEN listener unsupported"); return 77;
      }
      std::cerr << "tfo kernel_server supported=1\n";
    }
    if (getenv("ZUTCP_KERNEL_READ_GATE")) {
      int bytes = 4096;
      if (setsockopt(listener.fd(), SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes))) {
	failed("SO_RCVBUF"); return 1;
      }
      socklen_t size = sizeof(bytes);
      if (getsockopt(listener.fd(), SOL_SOCKET, SO_RCVBUF, &bytes, &size)) {
	failed("getsockopt SO_RCVBUF"); return 1;
      }
      std::cerr << "window kernel_rcvbuf=" << bytes << "\n";
    }
    if (setsockopt(listener.fd(), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) ||
	bind(listener.fd(), reinterpret_cast<sockaddr *>(&address), length) ||
	listen(listener.fd(), ListenBacklog)) { failed("listen"); return 1; }
    std::cerr << "backend=kernel-tcp role=server ready=1\n";
    std::cerr << std::flush;
    for (unsigned i = 0; i < count; ++i) {
      int accepted;
      do { accepted = accept4(listener.fd(), nullptr, nullptr, SOCK_CLOEXEC); }
      while (accepted < 0 && errno == EINTR);
      PeerSocket connection{accepted};
      if (accepted < 0) { failed("accept"); return 1; }
      if (setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)))
	{ failed("TCP_NODELAY"); return 1; }
      if (getenv("ZUTCP_KERNEL_READ_GATE")) {
	std::cerr << "window reader=held\n";
	std::cerr << std::flush;
	if (!waitGate("ZUTCP_KERNEL_READ_GATE")) return 1;
	std::cerr << "window reader=released\n";
	std::cerr << std::flush;
      }
      PeerBytes input;
      if (!i && getenv("ZUTCP_KERNEL_EXPECT_RESET")) {
	bool reset = false;
	if (!requestEOF(accepted, input, &reset) || !reset || input.length() != PrefixBytes) {
	  ZiLOG(Error, "zutcpkernelpeer", ([](auto &out) {
      out << "expected kernel reset after exact prefix";
    }));
	  return 1;
	}
	std::cerr << "reset kernel_server errno=" << ECONNRESET << " bytes="
	  << unsigned(input.length()) << "\n";
	continue;
      }
      if (!requestEOF(accepted, input) || !sendAll(accepted, "echo: ", 6) ||
	  !sendAll(accepted, input.data(), input.length()) || shutdown(accepted, SHUT_WR))
	return 1;
    }
  } else {
    for (unsigned i = 0; i < count; ++i) {
      PeerSocket connection{socket(family, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP)};
      if (connection.fd() < 0) { failed("socket"); return 1; }
      int enabled = 1;
      if (setsockopt(connection.fd(), IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)))
	{ failed("TCP_NODELAY"); return 1; }
      if (getenv("ZUTCP_KERNEL_TFO")) {
	ssize_t sent;
	do {
	  sent = sendto(connection.fd(), request.data(), PrefixBytes, MSG_FASTOPEN | MSG_NOSIGNAL,
	    reinterpret_cast<sockaddr *>(&address), length);
	} while (sent < 0 && errno == EINTR);
	if (sent < 0) { failed("kernel MSG_FASTOPEN unsupported/failed"); return 77; }
	if (sent > PrefixBytes) return 1;
	std::cerr << "backend=kernel-tcp role=client connected=1 tfo_sent="
	  << unsigned(sent) << "\n";
	if (!sendAll(connection.fd(), request.data() + sent,
	    request.length() - unsigned(sent)) || shutdown(connection.fd(), SHUT_WR) ||
	    !responseEOF(connection.fd())) return 1;
	continue;
      }
      if (connect(connection.fd(), reinterpret_cast<sockaddr *>(&address), length))
	{ failed("connect"); return 1; }
      std::cerr << "backend=kernel-tcp role=client connected=1\n";
      if (!i && getenv("ZUTCP_KERNEL_RESET_GATE")) {
	if (!sendAll(connection.fd(), request.data(), PrefixBytes) ||
	    !waitGate("ZUTCP_KERNEL_RESET_GATE")) return 1;
	linger abort{1, 0};
	if (setsockopt(connection.fd(), SOL_SOCKET, SO_LINGER, &abort, sizeof(abort))) {
	  failed("SO_LINGER reset"); return 1;
	}
	std::cerr << "reset kernel_client linger=0\n";
	continue;
      }
      if (!waitGate("ZUTCP_KERNEL_START_GATE") ||
	  !sendAll(connection.fd(), request.data(), request.length()) ||
	  shutdown(connection.fd(), SHUT_WR) || !responseEOF(connection.fd())) return 1;
    }
  }
  return 0;
}
