//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/Zfb.hh>

#include <zlib/ZtcDB.hh>

using namespace ZuTestUtil;

using Buffer = ZtArray<uint8_t, ZtArrayHeapID<"Zdb.Test.FB">>;

template <typename T>
Zfb::View<T> save(Buffer &bytes, const T &data)
{
  Zfb::save<ZuFacet::FB, ZfFieldFilter::All>(bytes, data);
  return Zfb::verify<T>(bytes) ? Zfb::view<T>(bytes) : Zfb::View<T>{};
}

void table()
{
  ZuTestScope(table);
  Ztc::DBTableTelemetry data;
  data.dbID = "database";
  data.id = "table_identifier_longer_than_twenty_eight_bytes_01";
  data.count = uint64_t(0xf123456789abcdef);
  data.cacheLoads = uint64_t(0xe123456789abcdef);
  data.cacheMisses = uint64_t(0xd123456789abcdef);
  data.cacheEvictions = uint64_t(0xc123456789abcdef);
  data.cacheSize = unsigned(0x71234567);
  data.cacheMode = Ztc::DBCacheMode::All;

  Buffer fbb;
  auto fbo = save(fbb, data);
  ZuCheck(fbo);
  ZuCheck(fbo.get<Ztc::ZfField(DBTableTelemetry, dbID)>() == data.dbID);
  ZuCheck(fbo.get<Ztc::ZfField(DBTableTelemetry, id)>() == data.id);
  ZuCheck(fbo.get<Ztc::ZfField(DBTableTelemetry, count)>() == data.count);
  ZuCheck(fbo.get<Ztc::ZfField(DBTableTelemetry, cacheLoads)>() == data.cacheLoads);
  ZuCheck(fbo.get<Ztc::ZfField(DBTableTelemetry, cacheMisses)>() == data.cacheMisses);
  ZuCheck(fbo.get<Ztc::ZfField(DBTableTelemetry, cacheEvictions)>() == data.cacheEvictions);
  ZuCheck(fbo.get<Ztc::ZfField(DBTableTelemetry, cacheSize)>() == data.cacheSize);
  ZuCheck(fbo.get<Ztc::ZfField(DBTableTelemetry, cacheMode)>() == Ztc::DBCacheMode::All);
  ZuCheck(fbo.get<Ztc::ZfField(DBTableTelemetry, rag)>() == data.rag());

  auto loaded = Zfb::handler<Ztc::DBTableTelemetry>(fbo).ctor();
  ZuCheck(loaded.dbID == data.dbID);
  ZuCheck(loaded.id == data.id);
  ZuCheck(loaded.count == data.count);
  ZuCheck(loaded.cacheLoads == data.cacheLoads);
  ZuCheck(loaded.cacheMisses == data.cacheMisses);
  ZuCheck(loaded.cacheEvictions == data.cacheEvictions);
  ZuCheck(loaded.cacheSize == data.cacheSize);
  ZuCheck(loaded.cacheMode == data.cacheMode);
}

void host()
{
  ZuTestScope(host);
  Ztc::DBHostTelemetry data;
  data.ip = ZiIP{"2001:db8::1234"};
  data.dbID = "database";
  data.id = "host";
  data.priority = unsigned(0x81234567);
  data.port = 43210;
  data.state = Ztc::DBHostState::Active;
  data.voted = 1;

  Buffer fbb;
  auto fbo = save(fbb, data);
  ZuCheck(fbo);
  ZuCheck(fbo.get<Ztc::ZfField(DBHostTelemetry, ip)>().type() == ZiIP::V6);
  ZuCheck(fbo.get<Ztc::ZfField(DBHostTelemetry, ip)>() == data.ip);
  ZuCheck(fbo.has<Ztc::ZfField(DBHostTelemetry, ip)>());
  ZuCheck(fbo.get<Ztc::ZfField(DBHostTelemetry, dbID)>() == data.dbID);
  ZuCheck(fbo.get<Ztc::ZfField(DBHostTelemetry, id)>() == data.id);
  ZuCheck(fbo.get<Ztc::ZfField(DBHostTelemetry, priority)>() == data.priority);
  ZuCheck(fbo.get<Ztc::ZfField(DBHostTelemetry, port)>() == data.port);
  ZuCheck(fbo.get<Ztc::ZfField(DBHostTelemetry, state)>() == Ztc::DBHostState::Active);
  ZuCheck(fbo.get<Ztc::ZfField(DBHostTelemetry, voted)>() == data.voted);
  ZuCheck(fbo.get<Ztc::ZfField(DBHostTelemetry, rag)>() == Ztc::RAG::Green);

  auto loaded = Zfb::handler<Ztc::DBHostTelemetry>(fbo).ctor();
  ZuCheck(loaded.ip == data.ip);
  ZuCheck(loaded.dbID == data.dbID);
  ZuCheck(loaded.id == data.id);
  ZuCheck(loaded.priority == data.priority);
  ZuCheck(loaded.port == data.port);
  ZuCheck(loaded.state == data.state);
  ZuCheck(loaded.voted == data.voted);
}

void db()
{
  ZuTestScope(db);
  Ztc::DBTelemetry data;
  data.thread = "db-thread";
  data.threads.push("thread-1");
  data.threads.push("thread-2");
  data.nShards = unsigned(0x81234567);
  data.self = "self";
  data.leader = "leader";
  data.prev = "prev";
  data.next = "next";
  data.nCxns = unsigned(0x81234567);
  data.heartbeatFreq = 101;
  data.heartbeatTimeout = 102;
  data.reconnectFreq = 103;
  data.electionTimeout = 104;
  data.nTables = 0x8123;
  data.nHosts = 201;
  data.nPeers = 202;
  data.state = Ztc::DBHostState::Inactive;
  data.active = 3;
  data.recovering = 4;
  data.replicating = 5;

  Buffer fbb;
  auto fbo = save(fbb, data);
  ZuCheck(fbo);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, thread)>() == data.thread);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, threads)>().length() == 2);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, threads)>()[0] == "thread-1");
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, threads)>()[1] == "thread-2");
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, nShards)>() == data.nShards);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, self)>() == data.self);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, leader)>() == data.leader);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, prev)>() == data.prev);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, next)>() == data.next);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, nCxns)>() == data.nCxns);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, heartbeatFreq)>() == data.heartbeatFreq);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, heartbeatTimeout)>() == data.heartbeatTimeout);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, reconnectFreq)>() == data.reconnectFreq);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, electionTimeout)>() == data.electionTimeout);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, nTables)>() == data.nTables);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, nHosts)>() == data.nHosts);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, nPeers)>() == data.nPeers);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, state)>() == Ztc::DBHostState::Inactive);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, active)>() == data.active);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, recovering)>() == data.recovering);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, replicating)>() == data.replicating);
  ZuCheck(fbo.get<Ztc::ZfField(DBTelemetry, rag)>() == Ztc::RAG::Amber);

  auto loaded = Zfb::handler<Ztc::DBTelemetry>(fbo).ctor();
  ZuCheck(loaded.thread == data.thread);
  ZuCheck(loaded.threads.length() == 2);
  ZuCheck(loaded.nShards == data.nShards);
  ZuCheck(loaded.self == data.self);
  ZuCheck(loaded.leader == data.leader);
  ZuCheck(loaded.prev == data.prev);
  ZuCheck(loaded.next == data.next);
  ZuCheck(loaded.nCxns == data.nCxns);
  ZuCheck(loaded.heartbeatFreq == data.heartbeatFreq);
  ZuCheck(loaded.heartbeatTimeout == data.heartbeatTimeout);
  ZuCheck(loaded.reconnectFreq == data.reconnectFreq);
  ZuCheck(loaded.electionTimeout == data.electionTimeout);
  ZuCheck(loaded.nTables == data.nTables);
  ZuCheck(loaded.nHosts == data.nHosts);
  ZuCheck(loaded.nPeers == data.nPeers);
  ZuCheck(loaded.state == data.state);
  ZuCheck(loaded.active == data.active);
  ZuCheck(loaded.recovering == data.recovering);
  ZuCheck(loaded.replicating == data.replicating);
}

void rag()
{
  ZuTestScope(rag);
  Ztc::DBTableTelemetry table;
  ZuCheck(table.rag() == Ztc::RAG::Off);
  table.cacheLoads = 1;
  table.cacheMisses = 9;
  ZuCheck(table.rag() == Ztc::RAG::Red);
  table.cacheLoads = 4;
  table.cacheMisses = 6;
  ZuCheck(table.rag() == Ztc::RAG::Amber);
  table.cacheLoads = 6;
  table.cacheMisses = 4;
  ZuCheck(table.rag() == Ztc::RAG::Green);

  Ztc::DBHostTelemetry host;
  host.state = Ztc::DBHostState::Instantiated;
  ZuCheck(host.rag() == Ztc::RAG::Off);
  host.state = Ztc::DBHostState::Initialized;
  ZuCheck(host.rag() == Ztc::RAG::Amber);
  host.state = Ztc::DBHostState::Electing;
  ZuCheck(host.rag() == Ztc::RAG::Amber);
  host.state = Ztc::DBHostState::Active;
  ZuCheck(host.rag() == Ztc::RAG::Green);
  host.state = Ztc::DBHostState::Inactive;
  ZuCheck(host.rag() == Ztc::RAG::Amber);
  host.state = Ztc::DBHostState::Stopping;
  ZuCheck(host.rag() == Ztc::RAG::Amber);
  host.state = -1;
  ZuCheck(host.rag() == Ztc::RAG::Off);
}

int main()
{
  ZuTestMain();
  ZuTestCall(table);
  ZuTestCall(host);
  ZuTestCall(db);
  ZuTestCall(rag);
}
