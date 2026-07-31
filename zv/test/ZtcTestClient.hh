//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// test-only telemetry public-wire client primitives

#ifndef ZtcTestClient_HH
#define ZtcTestClient_HH

#ifndef _WIN32

#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <zlib/Zfb.hh>

#include <zlib/ZtcApp.hh>
#include <zlib/ZtcMsg.hh>

namespace ZtcTestClient {

inline bool writeAll(int fd, ZuBSpan data)
{
  while (data.length()) {
    int n = ::send(fd, data.data(), data.length(), 0);
    if (n <= 0) return false;
    data.offset(unsigned(n));
  }
  return true;
}

inline bool readAll(int fd, uint8_t *data, unsigned length)
{
  while (length) {
    int n = ::recv(fd, data, length, 0);
    if (n <= 0) return false;
    data += n;
    length -= unsigned(n);
  }
  return true;
}

inline ZmRef<ZiIOBuf> readFrame(int fd)
{
  Ztc::Hdr hdr;
  if (!readAll(fd, reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr)))
    return nullptr;
  uint64_t total = sizeof(hdr) + uint32_t(hdr.length);
  if (total > Ztc::AppCf::DefltMaxFrame) return nullptr;
  ZmRef<ZiIOBuf> buf = new ZiIOBufAlloc<1024,
    Ztc::AppCf::DefltMaxFrame, "Ztc.TestClient.Rx">{};
  if (!buf->alloc(unsigned(total))) return nullptr;
  buf->append(reinterpret_cast<const uint8_t *>(&hdr), sizeof(hdr));
  unsigned body = uint32_t(hdr.length);
  if (body) {
    unsigned offset = buf->length;
    buf->length += body;
    if (!readAll(fd, buf->data() + offset, body)) return nullptr;
  }
  return buf;
}

inline int connect(const Ztc::App &app, bool timeout = false)
{
  int family = app.localIP().v6() ? AF_INET6 : AF_INET;
  int fd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) return fd;
  if (timeout) {
    timeval value{5, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value));
  }
  ZiSockAddr addr{app.localIP(), app.localPort()};
  if (::connect(fd, addr.sa(), addr.len())) {
    ::close(fd);
    return -1;
  }
  return fd;
}

inline ZmRef<ZiIOBuf> request(
    uint64_t seqNo, Ztc::fbs::Group group, ZuCSpan filter,
    uint32_t interval, bool subscribe,
    uint32_t alertDate = 0, uint64_t alertSeqNo = 0)
{
  Zfb::IOBuilder fbb{
    Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<1024,
      Ztc::AppCf::DefltMaxFrame, "Ztc.TestClient.Tx">{}})};
  auto filter_ = fbb.CreateString(filter.data(), filter.length());
  auto request_ = Ztc::fbs::CreateRequest(
    fbb, seqNo, group, filter_, interval, subscribe,
    alertDate, alertSeqNo);
  fbb.Finish(Ztc::fbs::CreateMsg(
    fbb, Ztc::fbs::Body::Request, request_.Union()));
  return Ztc::saveHdr(fbb);
}

} // ZtcTestClient

#endif /* !_WIN32 */

#endif /* ZtcTestClient_HH */
