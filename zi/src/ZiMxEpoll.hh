//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Linux epoll transport for ZiMultiplex

#ifndef ZiMxEpoll_HH
#define ZiMxEpoll_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPLock.hh>

#include <zlib/ZiMultiplex.hh>

class ZiMxEpoll final : public ZiMxPlugin, public ZmNHeap<ZiMxEpoll> {
friend inline ZuStringT<"ZiMxEpoll"> ZmNHeapID(ZiMxEpoll *);

  using Socket = Zi::Socket;

  class Listener_ : public ZuObject {
  friend ZiMxEpoll;

    using Socket = Zi::Socket;

  public:
    static Zi::Socket SocketAxor(const Listener_ &l) {
      return l.info().socket;
    }

  protected:
    template <typename ...Args>
    Listener_(ZiConnectFn acceptFn, Args &&...args) :
	m_acceptFn(ZuMv(acceptFn)),
	m_info{ZuFwd<Args>(args)...}, m_up(1) { }

  private:
    const ZiConnectFn &acceptFn() const { return m_acceptFn; }
    bool up() const { return m_up; }
    void down() { m_up = 0; }
    const ZiListenInfo &info() const { return m_info; }

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

  // heap-allocated non-blocking / asynchronous connect
  class Connect_ : public ZuObject
  {
  friend ZiMxEpoll;

    using Socket = Zi::Socket;

  public:
    static Zi::Socket SocketAxor(const Connect_ &c) {
      return c.info().socket;
    }

  protected:
    template <typename ...Args> Connect_(
	ZiConnectFn fn, ZiFailFn failFn, Args &&...args) :
      m_fn(fn), m_failFn(failFn), m_info{ZuFwd<Args>(args)...} {
    }

  private:
    void fail(bool transient) { m_failFn(transient); }

    ZiConnectFn takeFn() { return ZuMv(m_fn); }
    const ZiCxnInfo &info() const { return m_info; }
    ZiCxnInfo &info() { return m_info; }

    ZiConnectFn		m_fn;
    ZiFailFn		m_failFn;
    ZiCxnInfo		m_info;
  };
  ZmHashDerive(ConnectHash, Connect_,
    (ZmHashNode<Connect_,
      ZmHashKey<Connect_::SocketAxor,
	ZmHashHeapID<"ZiMultiplex.Connect">>>));
  using Connect = ConnectHash::Node;

  ZmHashDerive(CxnHash, ZmRef<ZiConnection>,
    (ZmHashKey<ZiConnection::SocketAxor,
	ZmHashHeapID<"ZiMultiplex.Connection">>));

public:
  explicit ZiMxEpoll(const ZiMxParams &params);

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
  bool cxnAdd(ZiConnection *, Socket);
  void listenerDel(Socket);
  void accept(Listener *);
  void stopAccepting();
  void retired(ZiConnection *, bool);

  void mxBound() override;
  bool listenerAdd(Listener *, Socket);
  void connect(Connect *);
  void completedConnect(Connect *);
  bool connectAdd(Connect *, Socket);
  void connectDel(Socket);
  bool epollRecv(ZiConnection *, int, uint32_t);
  bool pumpRx(ZiConnection *);
  void disconnectRx(ZiConnection *, bool);
  void closeRx(ZiConnection *, bool);
  void finishStop();
  bool readWake();
  ZmRef<ListenerHash>	m_listeners;
  ZmRef<CxnHash>	m_cxns;
  bool			m_stopping = false;
  const unsigned	m_epollMaxFDs;
  ZmRef<ConnectHash>	m_connects;
  int			m_epollFD = -1;
  ZmPLock		m_wakeLock;
  int			m_wakeFD = -1;
};

#endif
