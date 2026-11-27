//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Windows completion-port transport for ZiMultiplex

#ifndef ZiMxIOCP_HH
#define ZiMxIOCP_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <winsock2.h>
#include <mswsock.h>

#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPLock.hh>

#include <zlib/ZiMultiplex.hh>

class ZiMxIOCP final : public ZiMxPlugin, public ZmNHeap<ZiMxIOCP> {
friend inline ZuStringT<"ZiMxIOCP"> ZmNHeapID(ZiMxIOCP *);

  using Socket = Zi::Socket;

  class Overlapped : public WSAOVERLAPPED {
  public:
    using Executed = ZmFn<void(int, unsigned, ZeError),
      ZmFnHeapID<"ZiMxIOCP.Overlapped">>;

    void init(Executed executed) {
      *static_cast<WSAOVERLAPPED *>(this) = {};
      m_executed = ZuMv(executed);
    }

    void complete(int status, unsigned len, ZeError e) {
      auto executed = ZuMv(m_executed);
      executed(status, len, e); // May destroy this request.
    }

    bool pending() const { return !!m_executed; }
    void clear() { m_executed = {}; }

  private:
    Executed m_executed;
  };

  class Listener_;
  class Accept;

  class Listener_ : public ZuObject {
  friend ZiMxIOCP;
  friend Accept;

    using Socket = Zi::Socket;

  public:
    static Zi::Socket SocketAxor(const Listener_ &l) {
      return l.info().socket;
    }

  protected:
    template <typename ...Args>
    Listener_(ZiMxIOCP *backend, ZiConnectFn acceptFn, Args &&...args) :
	m_backend(backend), m_acceptFn(ZuMv(acceptFn)),
	m_info{ZuFwd<Args>(args)...}, m_up(1) { }

  private:
    ZiMxIOCP *backend() const { return m_backend; }
    const ZiConnectFn &acceptFn() const { return m_acceptFn; }
    bool up() const { return m_up; }
    void down() { m_up = 0; }
    const ZiListenInfo &info() const { return m_info; }

    ZiMxIOCP		*m_backend;
    ZiConnectFn		m_acceptFn;
    ZiListenInfo	m_info;
    bool		m_up;
  };
  ZmHashDerive(ListenerHash, Listener_,
    (ZmHashNode<Listener_,
      ZmHashKey<Listener_::SocketAxor,
	ZmHashHeapID<"ZiMultiplex.Listener",
	  ZmHashSharded<true>>>>));
  using Listener = ListenerHash::Node;

  // heap-allocated asynchronous accept, exclusively used by IOCP
  class Accept : public ZmNHeap<Accept> {
  friend inline ZuStringT<"ZiMultiplex.Accept"> ZmNHeapID(Accept *);

  friend ZiMxIOCP;

    Accept(Listener *listener) :
      m_listener(listener), m_info{
	ZiCxnType::TCPIn, Zi::nullSocket(), listener->info().options}
    {
      m_overlapped.init(
	Overlapped::Executed{this, ZmFnPtr<&Accept::executed>{}});
    }

    ZmRef<Listener> listener() const { return m_listener; }
    ZiCxnInfo &info() { return m_info; }
    Overlapped &overlapped() { return m_overlapped; }
    void *buf() { return (void *)&m_buf[0]; }

    void executed(int status, unsigned n, ZeError e) {
      m_listener->backend()->overlappedAccept(this, status, n, e);
      delete this;
    }

    ZmRef<Listener>	m_listener;
    ZiCxnInfo		m_info;
    Overlapped	m_overlapped;

    alignas(16)
    char		m_buf[(ZiSockAddr::MaxLen + 16) * 2];
  };

  // heap-allocated non-blocking / asynchronous connect
  class Connect_ : public ZuObject
  {
  friend ZiMxIOCP;

  public:
    static Socket SocketAxor(const Connect_ &request) { return request.info().socket; }

  protected:
    template <typename ...Args> Connect_(
	ZiMxIOCP *backend, ZiConnectFn fn, ZiFailFn failFn, Args &&...args) :
      m_backend(backend), m_fn(fn), m_failFn(failFn), m_info{ZuFwd<Args>(args)...} {
      m_overlapped.init(
	Overlapped::Executed{this, ZmFnPtr<&Connect_::executed>{}});
    }

  private:
    void fail(bool transient) { m_failFn(transient); }

    const ZiConnectFn &fn() const { return m_fn; }
    const ZiCxnInfo &info() const { return m_info; }
    ZiCxnInfo &info() { return m_info; }

    Overlapped &overlapped() { return m_overlapped; }

    void executed(int status, unsigned n, ZeError e) {
      m_backend->overlappedConnect(this, status, n, e);
    }

    ZiMxIOCP		*m_backend;
    ZiConnectFn		m_fn;
    ZiFailFn		m_failFn;
    ZiCxnInfo		m_info;
    Overlapped	m_overlapped;
  };

  ZmHashDerive(ConnectHash, Connect_,
    (ZmHashNode<Connect_, ZmHashKey<Connect_::SocketAxor,
      ZmHashHeapID<"ZiMxIOCP.Connect">>>));
  using Connect = ConnectHash::Node;

  ZmHashDerive(CxnHash, ZmRef<ZiConnection>,
    (ZmHashKey<ZiConnection::SocketAxor,
	ZmHashHeapID<"ZiMultiplex.Connection">>));

public:
  explicit ZiMxIOCP(const ZiMxParams &params);

private:
  bool kernel() const override { return true; }
  Zi::IOBufPool *allocator(Zi::IOBufType::T, unsigned, unsigned, unsigned) override {
    return nullptr;
  }
  void start() override;
  void stop() override;
  void stopped() override;
  void rx() override;
  void wake() override;

  void udp(ZiConnectFn, ZiFailFn, ZiIPType::T,
    ZiIP, uint16_t, ZiIP, uint16_t, Zi::IOBufAllocator *, ZiCxnOptions) override;
  void connect(ZiConnectFn, ZiFailFn, ZiIPType::T,
    ZiIP, uint16_t, ZiIP, uint16_t, Zi::IOBufAllocator *, ZiCxnOptions) override;
  void listen(ZiListenFn, ZiFailFn, ZiConnectFn,
    ZiIP, uint16_t, unsigned, Zi::IOBufAllocator *, ZiCxnOptions) override;
  void stopListening(ZiIP, uint16_t) override;
  void recv(ZiConnection *) override;
  void send(ZiConnection *) override;
  void disconnect(ZiConnection *, bool) override;
  void close(ZiConnection *, bool) override;

  void connected(ZiConnection *);
  void executedConnect(ZiConnectFn, ZiCxnInfo &);
  bool initSocket(Socket, const ZiCxnOptions &);
  void cxnAdd(ZiConnection *);
  void listenerDel(Socket);
  void accept(Listener *);
  void stopListeners();
  void retired(ZiConnection *, bool);

  class Cxn;
  void finishStop();
  void cancelConnects();
  void overlappedConnect(Connect_ *, int, unsigned, ZeError);
  void overlappedAccept(Accept *, int, unsigned, ZeError);
  ZmRef<ListenerHash>	m_listeners;
  ZmRef<CxnHash>	m_cxns;
  ZmRef<ConnectHash>	m_connects;
  unsigned		m_pendingAccepts = 0;
  bool			m_stopping = false;
  ZmPLock		m_wakeLock;
  HANDLE		m_completionPort = nullptr;
};

#endif
