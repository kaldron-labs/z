//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <string.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/Ztls.hh>
#include <zlib/ZtlsPico.hh>

using namespace ZuTestUtil;

#define ZTLS_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

namespace {

constexpr unsigned BufSize = 128;
constexpr unsigned MaxSize = (1u << 20);
constexpr unsigned PayloadSize = 64u * 1024u;
constexpr unsigned TimeoutSeconds = 5;

const char *CertPem =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIDCTCCAfGgAwIBAgIUP94rLbvIoOd15zOK8+6MwicqslYwDQYJKoZIhvcNAQEL\n"
  "BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDEwMzAzMDEzOFoXDTI2MDEw\n"
  "NDAzMDEzOFowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF\n"
  "AAOCAQ8AMIIBCgKCAQEAyo5t3mg7qCrParHZZ6o/Ug+93wSNPrqcOufVsbUvmQk5\n"
  "fNZfoFwVttQ6w5gxBpEvV8oSG/ccTNpJFHsQsrhBgfv95vLnz4FPLIdCYAAsdrtf\n"
  "w7Hjb6rLw4G5Waaq9+KuFMcaZV3x+LUFH0eSxp384fNDtu8D856bCvbL1z+QeLmM\n"
  "8FmEjNQJjlNl4viiJ4Yt3LFfeuQ40iaFBohFNSaqmi6kiRFQ6+qN9YSjuw6Zv2CW\n"
  "aA+eDw2I3geRfuUM+s0W9K9/KmsI60Mr7qxzb7VqvZovSl3Me5M17LM8Sd4oQdz/\n"
  "4PtH6/tOe76MAr/Jl3HgLapBopTpWAHs/jyXy/4a1wIDAQABo1MwUTAdBgNVHQ4E\n"
  "FgQUa3b6ClM5SDah3D9tEfglN2mw3RswHwYDVR0jBBgwFoAUa3b6ClM5SDah3D9t\n"
  "EfglN2mw3RswDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAPjsR\n"
  "2nHx9fWR3Cgx1J9nOWiNIaFG6hPSh7ADMpy+miw9QMuYhtFC9dD6jN2TUZv5I/eG\n"
  "yzhods5U+b1A+Sx/fv3oaItm55iXBqHfEVxwbwJhQdDTLizcBtGhYEYgWs8JN2EV\n"
  "QBnqRt2PtXXK4qndmifC+2Xx6I8D0+qbX+QWm4UDrra6hHz2W+K4xnPGjj3ZnoT8\n"
  "D14Vt3usDSQ3SV2vEJ6a67l8vjMqvk6LjIvJjJL2CWugeEKcHy4ZHn3ubVry0skZ\n"
  "E4rP2iu3uj/z8PNX69h41Ykxc5spYWyCuJA2iJewFnrZ9sg/5lQZ8ldHRCN52U59\n"
  "9tDAxrbyv2bcqKkv3g==\n"
  "-----END CERTIFICATE-----\n";

const char *KeyPem =
  "-----BEGIN PRIVATE KEY-----\n"
  "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDKjm3eaDuoKs9q\n"
  "sdlnqj9SD73fBI0+upw659WxtS+ZCTl81l+gXBW21DrDmDEGkS9XyhIb9xxM2kkU\n"
  "exCyuEGB+/3m8ufPgU8sh0JgACx2u1/DseNvqsvDgblZpqr34q4UxxplXfH4tQUf\n"
  "R5LGnfzh80O27wPznpsK9svXP5B4uYzwWYSM1AmOU2Xi+KInhi3csV965DjSJoUG\n"
  "iEU1JqqaLqSJEVDr6o31hKO7Dpm/YJZoD54PDYjeB5F+5Qz6zRb0r38qawjrQyvu\n"
  "rHNvtWq9mi9KXcx7kzXsszxJ3ihB3P/g+0fr+057vowCv8mXceAtqkGilOlYAez+\n"
  "PJfL/hrXAgMBAAECggEAEeqlEdbZbaFGK7NW8U72fxOdlI3EBDTtOPg+h2BpH4Al\n"
  "e0/al7lQqGI/kmmyiHJGJEQSjWVMHDyNc0gEn/8hUdYqAFTv2tBrlU45H+G6c4Qh\n"
  "m987winy3RrrFXpJePF7IW/2rnNJW6aAMS/OoU5IyuzbSmHI6svwMRLHnZ7MCiMc\n"
  "zNmy8nexvJzBw+3N9whDz816XOtdjnYLTPsjs5J2B8o/+i9zBUfK3eaI7hvuWdJZ\n"
  "V2dnaouUxZG8mcj9J+ICVOLxg9PbNZg0y/HbsOpyUoiTQ5s79Jn7GAD9qA/mkDGN\n"
  "Wdp9sjsAYsGthXWJEpErLhsAYAsGpg6u2aJs4arxDQKBgQDpAtNOTydHrUEa5WFj\n"
  "HuLUdADiKCR80UeMLzYVN2BPWoqX6xYwHEVQi6FnCruoFc3uladyEcHbIODbmS6K\n"
  "TGsySFKB3YguSgS+6M8Vke8MD+xgejxHrOYiv3R+Dda6jtjfm2pe+Ic2rdT736qL\n"
  "XPpJzvpovOYJOlEwHt3HLD2gowKBgQDeimh8oskGYv2VCE5D4Gs6EaXBYXU54nqe\n"
  "/SpruCI9IxLWuakF1N6D7BfroNn8wOGfVy5VJDKYGzgllvbsNNeyq1yj+L8XI0N4\n"
  "MYrfD+D/X9J/U9V98Wu3TuR3JlWX4xXUFYHzRCQhBK7fkrvsy14+0z9nbTS6u+XO\n"
  "bi5XsB8cPQKBgF3dzoPwbRF54Q1VtGq6yYPui2CP7Ur+/8SgTDg1y62L+uMCSDjv\n"
  "Wpj89vNMppYq2n+vd/oC30ZIM20jg1UhPdnOurYoKTEEjm7d2HaHCHaif4XKGDiD\n"
  "lV4QJHyXVJZo70L9F9fUZJwJYRBqZQipVwaew8+nsT+sZ4JsHMmcr+LjAoGANB+K\n"
  "9ZZTK1HIPz3gxvkrZEB56F9hS5uGSPLXGr/YFSW/5dc6hYkkTRXhTGkyZYbv0Zhj\n"
  "28FMsF+/uN4xG4YM92Y3nphGea7iwKYp9rELbAUPko8aNBN1vUuXK2kpJxgjJrea\n"
  "5lWReMJWCzudFItVmbV05k6nyQz1eHJKHHO99akCgYEAhikGyynXIZUoyvEvUYk1\n"
  "4NI/PsnNyuRdaKQMxQhc9GxqnWCfrI4mp7srkVnk65M3hf7g+3deeSjSzFg0FLJ5\n"
  "6KqfK8bg/0FGkuM4STEeEaU58Zha8lGtDe7IhkljSFqpMmCmZ4lUKwS2lqf0tSVy\n"
  "pZnA7bHLDZG7Pzp9JcJXbaY=\n"
  "-----END PRIVATE KEY-----\n";

struct LogCapture {
  ZmAtomic<unsigned> copy_warns{0};
  ZmAtomic<unsigned> errors{0};
  ZmAtomic<unsigned> misalign_errors{0};

  void reset() {
    copy_warns.store_(0);
    errors.store_(0);
    misalign_errors.store_(0);
  }

  static bool contains(ZuCSpan haystack, ZuCSpan needle) {
    if (!needle.length()) return true;
    if (needle.length() > haystack.length()) return false;
    for (unsigned i = 0; i <= haystack.length() - needle.length(); ++i) {
      if (!memcmp(haystack.data() + i, needle.data(), needle.length()))
	return true;
    }
    return false;
  }

  void onLog(ZeLogBuf &buf, const ZeEventInfo &info) {
    ZuCSpan span{buf.data(), buf.length()};
    if (contains(span, "falling back to copy"))
      copy_warns.xchAdd(1);
    if (contains(span, "TLS TX buffer misaligned"))
      misalign_errors.xchAdd(1);
    if (info.severity >= Ze::Error)
      errors.xchAdd(1);
  }
};

struct TestState {
  static constexpr unsigned Target = 2;

  ZmSemaphore		done;
  ZmSemaphore		listening;
  ZmAtomic<unsigned>	done_count{0};
  ZmAtomic<unsigned>	rx_bytes{0};
  ZmAtomic<unsigned>	errors{0};
  const char		*error_msg = nullptr;
  bool			allow_fail = false;
  ZiIP			ip;
  unsigned		port = 0;
  ZtArray<uint8_t>	payload;

  void done_one() {
    done_count.xchAdd(1);
    done.post();
  }
  void fail(const char *msg) {
    if (allow_fail) {
      for (unsigned i = 0; i < Target; ++i) done.post();
      return;
    }
    if (!errors.xch(1)) error_msg = msg;
    for (unsigned i = 0; i < Target; ++i) done.post();
  }
};

template <typename State>
struct BaseClient : public Ztls::Client<BaseClient<State>> {
  using BufAlloc = Ztls::BufAlloc<BufSize, MaxSize>;
  using Base = Ztls::Client<BaseClient<State>>;

  struct Link : public Ztls::CliLink<BaseClient, Link, BufAlloc> {
    using BaseLink = Ztls::CliLink<BaseClient, Link, BufAlloc>;
    Link(BaseClient *app) : BaseLink{app} { }

    void connected(const char *, int) {
      auto &payload = this->app()->state.payload;
      if (payload.length()) {
	auto tx = this->txStream_();
	tx.append(payload.data(), unsigned(payload.length()));
	tx << Zi::flush();
      }
    }
    void disconnected() { this->app()->state.done_one(); }
    void connectFailed(bool) { this->app()->state.fail("connect failed"); }
    int process(Ztls::RxStream &rx) {
      while (!rx.empty()) {
	auto span = rx.span();
	rx.advance(span.length());
      }
      return 1;
    }
  };

  BaseClient(State &state_) : state(state_) { }

  void override_cipher_suites(ptls_cipher_suite_t **suites) {
    this->ctx()->cipher_suites = suites;
  }

  State	&state;
};

template <typename State>
struct BaseServer : public Ztls::Server<BaseServer<State>> {
  using BufAlloc = Ztls::BufAlloc<BufSize, MaxSize>;
  using Base = Ztls::Server<BaseServer<State>>;

  struct Link : public Ztls::SrvLink<BaseServer, Link, BufAlloc> {
    using BaseLink = Ztls::SrvLink<BaseServer, Link, BufAlloc>;
    Link(BaseServer *app) : BaseLink{app} { }

    void connected(const char *, int) { }
    void disconnected() { this->app()->state.done_one(); }
    int process(Ztls::RxStream &rx) {
      while (!rx.empty()) {
	auto span = rx.span();
	unsigned got = unsigned(span.length());
	unsigned total = this->app()->state.rx_bytes.xchAdd(got);
	total += got;
	rx.advance(span.length());
	if (this->app()->state.payload.length() &&
	    total >= unsigned(this->app()->state.payload.length())) {
	  auto tx = this->txStream_();
	  tx.append(this->app()->state.payload.data(),
	      unsigned(this->app()->state.payload.length()));
	  tx << Zi::flush();
	  return -1;
	}
      }
      return 1;
    }
  };

  using Cxn = typename Link::Cxn;
  Cxn *accepted(const ZiCxnInfo &ci) {
    return new Cxn(new Link(this), ci);
  }

  BaseServer(State &state_, ZiIP ip_) : state(state_), ip(ip_) { }

  ZiIP localIP() const { return ip; }
  unsigned localPort() const { return 0; }

  void listening(const ZiListenInfo &info) {
    state.port = info.port;
    state.listening.post();
  }
  void listenFailed(bool) { state.fail("listen failed"); }

  void override_cipher_suites(ptls_cipher_suite_t **suites) {
    this->ctx()->cipher_suites = suites;
  }

  State	&state;
  ZiIP	ip;
};

struct AlignOverride {
  struct st_ptls_aead_algorithm_t aead;
  struct st_ptls_cipher_suite_t cipher;
  ptls_cipher_suite_t *list[2]{};

  AlignOverride()
    : aead(*ptls_openssl_cipher_suites[0]->aead),
      cipher(*ptls_openssl_cipher_suites[0]) {
    cipher.aead = &aead;
    list[0] = &cipher;
    list[1] = nullptr;
  }

  void init(uint8_t align_bits) {
    aead.align_bits = align_bits;
  }
};

bool write_file(const char *path, const char *data)
{
  ZiFile file;
  if (file.open(path, ZiFile::Write) != Zi::OK) return false;
  return file.write(data, strlen(data)) == Zi::OK;
}

void fill_payload(ZtArray<uint8_t> &payload, unsigned len)
{
  payload.length(len);
  for (unsigned i = 0; i < len; ++i)
    payload[i] = uint8_t(i);
}

bool wait_for(ZmSemaphore &sem)
{
  return sem.timedwait(Zm::now(TimeoutSeconds)) == 0;
}

bool wait_done(TestState &state)
{
  for (unsigned i = 0; i < TestState::Target; ++i)
    if (!wait_for(state.done)) return false;
  return true;
}

void testDefaultBuffers(LogCapture &capture,
    const char *cert_path, const char *key_path)
{
  ZuTestScopeRT(testDefaultBuffers);

  Ztls::Pico::reset_stats();
  capture.reset();

  TestState state;
  state.ip = ZiIP("127.0.0.1");
  state.allow_fail = true;
  fill_payload(state.payload, PayloadSize);

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(4)
	  .thread(1, [](auto &t) { t.isolated(1); })
	  .thread(2, [](auto &t) { t.isolated(1); })
	  .thread(3, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  bool mxStarted = mx.start();
  ZTLS_CHECK_RT(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) return;

  BaseServer<TestState> server(state, state.ip);
  bool serverOK = server.init(
    Ztls::ServerParams(&mx, "3", {})
      .certPath(cert_path)
      .keyPath(key_path));
  ZTLS_CHECK_RT(serverOK, "TLS server init failed");
  if (!serverOK) { mx.stop(); return; }

  BaseClient<TestState> client(state);
  bool clientOK = client.init(
    Ztls::ClientParams(&mx, "3", {}).caPath(cert_path));
  ZTLS_CHECK_RT(clientOK, "TLS client init failed");
  if (!clientOK) { mx.stop(); return; }

  server.listen();
  bool listening = wait_for(state.listening);
  ZTLS_CHECK_RT(listening, "listen timed out");
  if (!listening) { mx.stop(); return; }

  ZmRef<BaseClient<TestState>::Link> link =
    new BaseClient<TestState>::Link(&client);
  link->connect(state.ip, state.port);

  bool done = wait_done(state);
  ZTLS_CHECK_RT(done, "TLS disconnect wait timed out");

  mx.stop();

  auto stats = Ztls::Pico::stats();
  (void)stats;
  ZTLS_CHECK_RT(!capture.copy_warns.load_(), "copy fallback warnings seen");
  ZTLS_CHECK_RT(!capture.errors.load_(), "unexpected error logs");
  ZTLS_CHECK_RT(!state.errors.load_(),
    state.error_msg ? state.error_msg : "state error");
}

void testAlignedBuffers(LogCapture &capture,
    const char *cert_path, const char *key_path)
{
  ZuTestScopeRT(testAlignedBuffers);

  Ztls::Pico::reset_stats();
  capture.reset();

  TestState state;
  state.ip = ZiIP("127.0.0.1");
  state.allow_fail = true;
  fill_payload(state.payload, 1);

  AlignOverride override_;
  override_.init(12);

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(4)
	  .thread(1, [](auto &t) { t.isolated(1); })
	  .thread(2, [](auto &t) { t.isolated(1); })
	  .thread(3, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  bool mxStarted = mx.start();
  ZTLS_CHECK_RT(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) return;

  BaseServer<TestState> server(state, state.ip);
  bool serverOK = server.init(
    Ztls::ServerParams(&mx, "3", {})
      .certPath(cert_path)
      .keyPath(key_path));
  ZTLS_CHECK_RT(serverOK, "TLS server init failed");
  if (!serverOK) { mx.stop(); return; }

  BaseClient<TestState> client(state);
  bool clientOK = client.init(
    Ztls::ClientParams(&mx, "3", {}).caPath(cert_path));
  ZTLS_CHECK_RT(clientOK, "TLS client init failed");
  if (!clientOK) { mx.stop(); return; }

  server.override_cipher_suites(override_.list);
  client.override_cipher_suites(override_.list);

  server.listen();
  bool listening = wait_for(state.listening);
  ZTLS_CHECK_RT(listening, "listen timed out");
  if (!listening) { mx.stop(); return; }

  ZmRef<BaseClient<TestState>::Link> link =
    new BaseClient<TestState>::Link(&client);
  link->connect(state.ip, state.port);

  bool done = wait_done(state);
  ZTLS_CHECK_RT(done, "TLS disconnect wait timed out");

  mx.stop();

  auto stats = Ztls::Pico::stats();
  (void)stats;
  ZTLS_CHECK_RT(!capture.copy_warns.load_(),
    "copy fallback warnings seen (align case)");
}

} // namespace

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);

  ZiLog::init("ZtlsBufHookTest");
  ZiLog::level(0);

  LogCapture capture;
  ZiLog::sink(ZiLog::lambdaSink([&capture](ZeLogBuf &buf, const ZeEventInfo &info) {
    capture.onLog(buf, info);
  }));
  ZiLog::start();

  const char *cert_path = "ZtlsBufHookTest-cert.pem";
  const char *key_path = "ZtlsBufHookTest-key.pem";

  ZuTestMain();
  bool wroteCerts =
    write_file(cert_path, CertPem) && write_file(key_path, KeyPem);
  ZuCHECK(wroteCerts, "failed to write cert/key");
  if (wroteCerts) {
    ZuTestCall(testDefaultBuffers, capture, cert_path, key_path);
    ZuTestCall(testAlignedBuffers, capture, cert_path, key_path);
  }

  ZiLog::stop();
}
