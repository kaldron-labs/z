//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Standalone raw Ethernet/IP/UDP packet endpoint; no Zutcp dependency.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <netinet/ether.h>
#include <iostream>
#include <limits.h>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zlib/ZuMatcher.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZxdpBPF.hh>
#include <zlib/Zxdp.hh>

struct Options {
  const char *interface = nullptr, *readyFile = nullptr;
  unsigned count = 32, payload = 128, frames = 2;
  unsigned localPort = 40000, peerPort = 40001;
  unsigned spin = 0, timeout = 1, deadline = 10;
  uint8_t local[4]{}, peer[4]{}, mac[6]{};
  bool server = false;
};

static bool number(const char *s, unsigned &out)
{
  char *end;
  errno = 0;
  unsigned long n = strtoul(s, &end, 10);
  if (errno || !*s || *end || *s == '-' || n > UINT_MAX) return false;
  out = n;
  return true;
}

struct Roles { using Keys = ZuStringTL<"client", "server">; };

static bool options(int argc, char **argv, Options &out)
{
  if (argc < 2) return false;
  auto role = ZuMatcher<Roles>().exact(argv[1]);
  if (role < 0) return false;
  out.server = role;
  static const option opts[] = {
    {"interface", required_argument, nullptr, 'i'},
    {"peer-mac", required_argument, nullptr, 'm'},
    {"local-ip", required_argument, nullptr, 'l'},
    {"peer-ip", required_argument, nullptr, 'r'},
    {"local-port", required_argument, nullptr, 'a'},
    {"peer-port", required_argument, nullptr, 'b'},
    {"count", required_argument, nullptr, 'n'},
    {"payload", required_argument, nullptr, 'p'},
    {"frames", required_argument, nullptr, 'f'},
    {"spin", required_argument, nullptr, 's'},
    {"timeout", required_argument, nullptr, 't'},
    {"deadline", required_argument, nullptr, 'd'},
    {"ready-file", required_argument, nullptr, 'R'},
    {nullptr, 0, nullptr, 0}
  };
  bool local = false, peer = false, mac = false;
  optind = 2;
  for (;;) {
    int opt = getopt_long(argc, argv, "", opts, nullptr);
    if (opt < 0) break;
    switch (opt) {
      case 'i': out.interface = optarg; break;
      case 'l': local = inet_pton(AF_INET, optarg, out.local) == 1; break;
      case 'r': peer = inet_pton(AF_INET, optarg, out.peer) == 1; break;
      case 'm': {
        ether_addr value;
        mac = ether_aton_r(optarg, &value);
        if (mac) memcpy(out.mac, value.ether_addr_octet, sizeof(out.mac));
      } break;
      case 'a': if (!number(optarg, out.localPort)) return false; break;
      case 'b': if (!number(optarg, out.peerPort)) return false; break;
      case 'n': if (!number(optarg, out.count)) return false; break;
      case 'p': if (!number(optarg, out.payload)) return false; break;
      case 'f': if (!number(optarg, out.frames)) return false; break;
      case 's': if (!number(optarg, out.spin)) return false; break;
      case 't': if (!number(optarg, out.timeout)) return false; break;
      case 'd': if (!number(optarg, out.deadline)) return false; break;
      case 'R': out.readyFile = optarg; break;
      default: return false;
    }
  }
  return optind == argc && out.interface && local && peer && mac && out.count &&
    out.frames && out.payload >= 4 && out.localPort && out.localPort <= 65535 &&
    out.peerPort && out.peerPort <= 65535 && out.deadline;
}

static unsigned get16(const uint8_t *p) { return unsigned(p[0])<<8 | p[1]; }
static void put16(uint8_t *p, unsigned n) { p[0] = n>>8; p[1] = n; }
static unsigned sum(const uint8_t *p, unsigned n, unsigned value = 0)
{
  while (n >= 2) { value += get16(p); p += 2; n -= 2; }
  if (n) value += unsigned(*p)<<8;
  return value;
}
static unsigned fold(unsigned n) { while (n>>16) n = (n & 65535) + (n>>16); return n; }

// The utility supplies a dedicated Rx owner and a separate Tx service owner.
// Protocol construction and verification are independent of the packet library.
struct Endpoint {
  Options config;
  Zxdp::BufPool rx, tx;
  ZmRef<Zxdp::Instance> instance;
  ZtString<> error;
  ZmAtomic<unsigned> received{0}, completed{0}, failed{0}, nextSeq{0};
  unsigned sent = 0, sequence = 0;
  uint8_t *pending = nullptr;

  explicit Endpoint(Options options) : config{options}, rx{options.frames, 0}, tx{2, 0} { }

  uint8_t pattern(unsigned seq, unsigned i, bool reply) const {
    auto value = i < 4 ? uint8_t(seq >> ((3 - i)<<3)) : uint8_t(seq + i * 13);
    return value ^ (reply ? 0x5a : 0);
  }

  void transmit(unsigned seq) {
    sequence = seq;
    if (!pending) {
      pending = tx.alloc();
      if (!pending) return;
      auto p = pending;
      memset(p, 0, 42);
      memcpy(p, config.mac, 6);
      memcpy(p + 6, instance->mac(), 6);
      put16(p + 12, 0x0800);
      p[14] = 0x45;
      put16(p + 16, config.payload + 28);
      p[22] = 64;
      p[23] = IPPROTO_UDP;
      memcpy(p + 26, config.local, 4);
      memcpy(p + 30, config.peer, 4);
      put16(p + 24, ~fold(sum(p + 14, 20)));
      put16(p + 34, config.localPort);
      put16(p + 36, config.peerPort);
      put16(p + 38, config.payload + 8);
      for (unsigned i = 0; i < config.payload; ++i)
        p[42 + i] = pattern(seq, i, config.server);
      auto check = ~fold(sum(p + 26, 8, IPPROTO_UDP + config.payload + 8) +
        sum(p + 34, config.payload + 8)) & 65535;
      put16(p + 40, check ? check : 65535);
    }
    if (instance->send(pending, config.payload + 42)) {
      pending = nullptr;
      sequence = 0;
      ++sent;
    }
  }

  void receive(uint8_t *p, unsigned length) {
    auto seq = unsigned(received) + 1;
    // Ignore unrelated Ethernet traffic. Verify every byte of the fixture's
    // selected IPv4/UDP stream before progressing its stop-and-wait exchange.
    if (length < 42 || get16(p + 12) != 0x0800 || p[23] != IPPROTO_UDP ||
        get16(p + 36) != config.localPort) { rx.free(p); return; }
    bool valid = length == config.payload + 42 && p[14] == 0x45 &&
      get16(p + 16) == config.payload + 28 && !memcmp(p + 26, config.peer, 4) &&
      !memcmp(p + 30, config.local, 4) && get16(p + 34) == config.peerPort &&
      get16(p + 38) == config.payload + 8 && fold(sum(p + 14, 20)) == 65535 &&
      get16(p + 40) && fold(sum(p + 26, 8, IPPROTO_UDP + config.payload + 8) +
        sum(p + 34, config.payload + 8)) == 65535;
    for (unsigned i = 0; i < config.payload && valid; ++i)
      valid = p[42 + i] == pattern(seq, i, !config.server);
    rx.free(p);
    if (!valid) {
      error = "packet integrity mismatch"; failed = 1; instance->wake(); return;
    }
    ++received;
    if (config.server) nextSeq = seq;
    else if (seq < config.count) nextSeq = seq + 1;
    if (received == config.count && completed == config.count) instance->wake();
  }

  bool run() {
    unsigned index = if_nametoindex(config.interface);
    Zxdp::LinkState link;
    if (!index || !Zxdp::queryLink(index, link, error) || link.mtu < 28 ||
        config.payload > link.mtu - 28 || config.frames < link.rxQueues) {
      std::cerr << "invalid interface/payload geometry: " << error << '\n';
      return false;
    }
    rx.size = tx.size = link.mtu + 14;
    instance = Zxdp::Instance::create({.interface = index, .txPools = &tx, .rxPool = &rx,
      .rx = [this](uint8_t *p, unsigned n) { receive(p, n); },
      .txCompleted = [this](uint8_t *p, unsigned n) {
        completed += n;
        tx.free(p);
        if (received == config.count && completed == config.count) instance->wake();
      }, .spin = config.spin}, error);
    if (!instance || !instance->ready()) failed = true;
    if (!failed && config.readyFile) {
      ZiFile file;
      if (file.open(config.readyFile, ZiFile::Write | ZiFile::Exclusive, 0600) != Zi::OK ||
          file.write("ready\n", 6) != Zi::OK) { error = "ready-file creation failed"; failed = true; }
    }
    if (!failed) {
      if (!config.server) nextSeq = 1;
      auto deadline = Zm::now() + ZuTime{config.deadline};
      ZmThread service{[this, deadline] {
        while (!failed && (received != config.count || completed != config.count)) {
          auto work = instance->pollTx();
          auto seq = nextSeq.xch(0);
          if (seq) transmit(seq);
          else if (sequence) transmit(sequence);
          if (!instance->ready() || Zm::now() >= deadline) {
            failed = 1;
            instance->wake();
            break;
          }
          // Application Tx completion/deadline service; Rx waiting is wholly
          // inside the library and its configured spin-before-epoll loop.
          if (!work) poll(nullptr, 0, config.timeout);
        }
      }};
      while (!failed && (received != config.count || completed != config.count))
        instance->rx();
      service.join();
      if (failed && !error) error = "packet endpoint deadline or device failure";
    }
    // Quiescent main-thread owner retries only deferred kernel retirement.
    if (instance) {
      bool closed = false;
      auto limit = Zm::now() + ZuTime{10};
      do {
        ZtString<> cleanup;
        if (instance->close(cleanup)) { closed = true; break; }
        if (!!cleanup) { error = ZuMv(cleanup); failed = true; }
        poll(nullptr, 0, 1);
      } while (Zm::now() < limit);
      ZmAssert_(closed); // unresolved kernel DMA never authorizes backing release
      instance = nullptr;
    }
    if (pending) { tx.free(pending); pending = nullptr; }
    auto ok = !failed && received == config.count && completed == config.count;
    std::cout << (ok ? "PASS" : "FAIL") << " role=" << (config.server ? "server" : "client")
      << " packets=" << unsigned(received) << " completions=" << unsigned(completed)
      << " payload=" << config.payload << " frames=" << config.frames << '\n';
    if (!!error) std::cerr << error << '\n';
    return ok;
  }
};

int main(int argc, char **argv)
{
  Options config;
  if (!options(argc, argv, config)) {
    std::cerr << "usage: zxdppacket client|server --interface NAME --peer-mac MAC\n"
      "  --local-ip IPV4 --peer-ip IPV4 [--local-port N --peer-port N]\n"
      "  [--count N --payload BYTES --frames N --spin N --timeout MS]\n"
      "  [--deadline SECONDS --ready-file PATH]\n";
    return 2;
  }
  Endpoint endpoint{config};
  return endpoint.run() ? 0 : 1;
}
