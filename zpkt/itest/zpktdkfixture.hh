//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private software PMD fixtures. One peer owns the other side of each ring.

#ifndef zpktdkfixture_HH
#define zpktdkfixture_HH

#include <rte_ether.h>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZiMultiplex.hh>

#include <zlib/ZpktEnv.hh>

struct rte_mbuf;
struct rte_mempool;
struct rte_ring;

namespace ZpktDpdkTest {

using Result = ZuUnion<void, ZeException>;

class Runtime {
public:
  Runtime() = default;
  ~Runtime();
  Runtime(const Runtime &) = delete;
  Runtime &operator =(const Runtime &) = delete;

  Result init(bool isolated = true);
  Zpkt::Env *env() const { return m_env; }
  const char *dir() const { return m_dir; }

private:
  ZmRef<Zpkt::Env> m_env = new Zpkt::Env;
  char m_dir[sizeof("/tmp/zpktdk.XXXXXX")] = "";
};

class RingPort {
public:
  RingPort() = default;
  ~RingPort();
  RingPort(const RingPort &) = delete;
  RingPort &operator =(const RingPort &) = delete;

  Result init(const char *id, unsigned ringSize, rte_ether_addr mac);
  const char *name() const { return m_name; }

  // Call from the fixture side. The Rx mbuf always comes from the receiver's pool.
  bool inject(rte_mempool *, ZuBSpan, uint8_t **packet = nullptr);
  rte_mbuf *take();

  // Once both queue owners have quiesced, return any residual ring entries.
  void drain();

private:
  ZtString<>	m_name;
  rte_ring	*m_rx = nullptr, *m_tx = nullptr;
};

class Wire {
public:
  struct End {
    RingPort	*port;
    rte_mempool	*ingress;
    ZmSemaphore	*retired = nullptr;
  };

  Wire() = default;
  ~Wire() { stop(); }
  Wire(const Wire &) = delete;
  Wire &operator =(const Wire &) = delete;

  Result start(End, End, ZmThreadParams = {});

  // Disable Rx production while continuing to retire all accepted Tx.
  void quiesce();
  void stop();

private:
  enum { Idle, Forward, Drop, Exit };

  ZmAtomic<unsigned>	m_mode{Idle};
  ZmSemaphore		m_quiesced;
  ZmThread		m_peer;
};

class PcapPort {
public:
  PcapPort() = default;
  ~PcapPort();
  PcapPort(const PcapPort &) = delete;
  PcapPort &operator =(const PcapPort &) = delete;

  Result init(const Runtime &, ZuBSpan input);
  const char *name() const { return "net_pcap_zpkt0"; }
  bool captured(ZuBSpan output) const;

private:
  ZtString<>	m_input, m_output;
  bool		m_attached = false;
};

struct Device {
  ZiMultiplex			mx;
  ZmRef<Zpkt::Nif>		device;
  ZmRef<Zi::IOBufPool>		ingress, tx;
  Zi::IOBufPoolList		pools;

  ~Device();
  Result init(const Runtime &, const char *, unsigned count = 32);
  Result start(Zpkt::RecvFn);
  bool stop();

  template <typename Fn> void onTx(Fn fn) {
    ZmSemaphore done;
    mx.txRun([fn = ZuMv(fn), &done]() mutable {
      fn();
      done.post();
    });
    done.wait();
  }
};

rte_mempool *native(Zi::IOBufPool *);
ZtString<> errorText(const ZeException &);

} // namespace ZpktDpdkTest
#endif /* zpktdkfixture_HH */
