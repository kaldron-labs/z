//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Process fixture for the public Ztc publisher ring protocol.

#include <stdlib.h>
#include <string.h>

#include <iostream>

#include <zlib/ZuCmp.hh>
#include <zlib/ZuLib.hh>
#include <zlib/Zfb.hh>
#include <zlib/ZiRing.hh>
#include <zlib/ZtcApp.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcMsg.hh>
#include <zlib/ZtcRing.hh>

using Frame = ZiIOBufAlloc<1024, Ztc::AppCf::DefltMaxFrame,
  "Zum.ZtcProbe.Frame">;

static ZmRef<ZiIOBuf> request(ZuCSpan id, Ztc::fbs::Group group,
    uint64_t seqNo, uint32_t interval, bool subscribe)
{
  Zfb::IOBuilder builder{Ztc::frameBuf(ZmRef<ZiIOBuf>{new Frame})};
  Ztc::Request value;
  value.id = id;
  value.filter = "*";
  value.seqNo = seqNo;
  value.interval = interval;
  value.group = uint8_t(group);
  value.subscribe = subscribe;
  auto encoded = ZfbStruct::save(builder, value);
  builder.Finish(Ztc::saveMsg(
    builder, Ztc::fbs::Body::Request, encoded.Union()));
  return Ztc::saveHdr(builder);
}

static bool send(Ztc::Ring &ring, ZmRef<ZiIOBuf> frame)
{
  if (!frame) return false;
  void *ptr = ring.tryPush(frame->length);
  if (!ptr) return false;
  memcpy(ptr, frame->data(), frame->length);
  ring.push2(ptr, frame->length);
  return true;
}

static int probe(ZuCSpan id, Ztc::fbs::Group group, bool hold,
    int expectedDB = -1)
{
  const char *name = ::getenv("ZTC_RING");
  if (!name || !*name) { std::cerr << "missing ring\n"; return 1; }
  Ztc::Ring telemetry{ZiRingParams{name, 1U<<22}.timeout(1000)};
  if (telemetry.open(Ztc::Ring::Read) != Zu::OK) {
    std::cerr << "telemetry open failed\n";
    return 1;
  }
  if (telemetry.attach() != Zu::OK) {
    std::cerr << "telemetry attach failed\n";
    return 1;
  }
  ZuGuard detach{[&telemetry]() {
    telemetry.detach();
    telemetry.close();
  }};
  Ztc::Ring requests{ZiRingParams{id, 0}.timeout(1000)};
  if (requests.open(Ztc::Ring::Write) != Zu::OK) {
    std::cerr << "request ring open failed\n";
    return 1;
  }
  uint64_t seqNo = 1;
  if (!send(requests, request(id, Ztc::fbs::Group::App,
        ZuCmp<uint64_t>::null(), 0, false)) ||
      !send(requests, request(id, group, seqNo, hold ? 1000 : 0, true))) {
    std::cerr << "request send failed\n";
    return 1;
  }
  bool ack = false, data = false;
  for (unsigned attempt = 0; attempt < 20; ++attempt) {
    const void *ptr = telemetry.shift();
    if (!ptr) continue;
    auto msg = Ztc::msg(static_cast<const Ztc::Hdr *>(ptr));
    if (msg && msg->body_type() == Ztc::fbs::Body::Ack) {
      auto value = msg->body_as_Ack();
      if (value && value->seqNo() == seqNo &&
          value->status() == Ztc::fbs::AckStatus::OK)
        ack = true;
    } else if (msg && msg->body_type() == Ztc::fbs::Body::Telemetry) {
      auto value = msg->body_as_Telemetry();
      if (value && value->seqNo() == seqNo &&
	  value->value_type() != Ztc::fbs::TelemetryBody::NONE) {
	if (expectedDB < 0) data = true;
	else if (auto db = value->value_as_DBTelemetry())
	  data = db->active() == expectedDB;
      }
      if (value && value->value_type() ==
          Ztc::fbs::TelemetryBody::Shutdown) {
        telemetry.shift2(Ztc::ringSize(ptr));
        return hold && ack && data ? 0 : 1;
      }
    }
    telemetry.shift2(Ztc::ringSize(ptr));
    if (ack && data) {
      if (!hold) return 0;
      std::cout << "ready" << std::endl;
      break;
    }
  }
  if (!hold || !ack || !data) {
    std::cerr << "snapshot incomplete ack=" << ack << " data=" << data
      << '\n';
    return 1;
  }
  for (unsigned attempt = 0; attempt < 20; ++attempt) {
    const void *ptr = telemetry.shift();
    if (!ptr) continue;
    auto msg = Ztc::msg(static_cast<const Ztc::Hdr *>(ptr));
    bool shutdown = false;
    if (msg && msg->body_type() == Ztc::fbs::Body::Telemetry) {
      auto value = msg->body_as_Telemetry();
      shutdown = value && value->value_type() ==
        Ztc::fbs::TelemetryBody::Shutdown;
    }
    telemetry.shift2(Ztc::ringSize(ptr));
    if (shutdown) return 0;
  }
  return 1;
}

int main(int argc, char **argv)
{
  if (argc < 2 || argc > 4) return 1;
  ZuCSpan id{argv[1]};
  if (argc == 3 && ZuCSpan{argv[2]} == "stale-read") {
    const char *name = ::getenv("ZTC_RING");
    if (!name || !*name) return 1;
    Ztc::Ring telemetry{ZiRingParams{name, 1U<<22}.timeout(1000)};
    if (telemetry.open(Ztc::Ring::Read) != Zu::OK ||
	telemetry.attach() != Zu::OK) return 1;
    std::cout << "ready" << std::endl;
    char c;
    std::cin.get(c);
    return 0;
  }
  if (argc == 2) {
    const char *name = ::getenv("ZTC_RING");
    if (!name || !*name) return 1;
    Ztc::Ring telemetry{ZiRingParams{name, 1U<<22}.timeout(1000)};
    if (telemetry.open(Ztc::Ring::Write) != Zu::OK ||
        telemetry.reset() != Zu::OK) return 1;
    std::cout << "ready" << std::endl;
    char c;
    while (std::cin.get(c)) {
      if (c == 'p') {
	if (!send(telemetry, request(id, Ztc::fbs::Group::App,
	      1, 0, true))) return 1;
	std::cout << "pushed" << std::endl;
      } else if (c == 'g') {
	if (!telemetry.length()) return 1;
	telemetry.close();
	if (telemetry.open(Ztc::Ring::Write) != Zu::OK ||
	    telemetry.length() || telemetry.reset() != Zu::OK) {
	  std::cerr << "stale message was not reclaimed\n";
	  return 1;
	}
	std::cout << "reclaimed" << std::endl;
      } else if (c == 'q') break;
    }
    return 0;
  }
  ZuCSpan groupName{argv[2]};
  Ztc::fbs::Group group;
  if (groupName == "app") group = Ztc::fbs::Group::App;
  else if (groupName == "mx") group = Ztc::fbs::Group::Mx;
  else if (groupName == "db") group = Ztc::fbs::Group::DB;
  else if (groupName == "db-active" || groupName == "db-passive")
    group = Ztc::fbs::Group::DB;
  else return 1;
  int expectedDB = groupName == "db-active" ? 1 :
    groupName == "db-passive" ? 0 : -1;
  return probe(id, group, argc == 4 && ZuCSpan{argv[3]} == "hold",
    expectedDB);
}
