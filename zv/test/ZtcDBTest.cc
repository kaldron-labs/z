//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZfbStruct.hh>

#include <zlib/ZtcDB.hh>

using namespace ZuTestUtil;

template <typename T>
const ZfbType<T> *save(Zfb::Builder &fbb, const T &data)
{
  fbb.Finish(
    ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data));
  return ZfbStruct::verify<T>(
    {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
}

void table()
{
  ZuTestScope(table);
  Ztc::DBTableTelemetry data;
  data.dbID = "database";
  data.id = "table_identifier_longer_than_twenty_eight_bytes_01";
  data.threads.push("thread-1");
  data.threads.push("thread-2");
  data.count = UINT64_C(0xf123456789abcdef);
  data.cacheLoads = UINT64_C(0xe123456789abcdef);
  data.cacheMisses = UINT64_C(0xd123456789abcdef);
  data.cacheEvictions = UINT64_C(0xc123456789abcdef);
  data.nShards = UINT32_C(0x81234567);
  data.cacheSize = UINT32_C(0x71234567);
  data.cacheMode = Ztc::DBCacheMode::All;

  Zfb::Builder fbb;
  auto fbo = save(fbb, data);
  ZuCheck(fbo);
  ZuCheck(Zfb::Load::str(fbo->dbID()) == data.dbID);
  ZuCheck(Zfb::Load::str(fbo->id()) == data.id);
  ZuCheck(fbo->nShards() == data.nShards);
  ZuCheck(fbo->threads()->size() == 2);
  ZuCheck(Zfb::Load::str(fbo->threads()->Get(0)) == "thread-1");
  ZuCheck(Zfb::Load::str(fbo->threads()->Get(1)) == "thread-2");
  ZuCheck(fbo->count() == data.count);
  ZuCheck(fbo->cacheLoads() == data.cacheLoads);
  ZuCheck(fbo->cacheMisses() == data.cacheMisses);
  ZuCheck(fbo->cacheEvictions() == data.cacheEvictions);
  ZuCheck(fbo->cacheSize() == data.cacheSize);
  ZuCheck(fbo->cacheMode() == Ztc::fbs::DBCacheMode::All);
  ZuCheck(fbo->rag() == data.rag());

  auto loaded = ZfbStruct::ctor<Ztc::DBTableTelemetry>(fbo);
  ZuCheck(loaded.dbID == data.dbID);
  ZuCheck(loaded.id == data.id);
  ZuCheck(loaded.nShards == data.nShards);
  ZuCheck(loaded.threads.length() == 2);
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
  data.priority = UINT32_C(0x81234567);
  data.port = 43210;
  data.state = Ztc::DBHostState::Active;
  data.voted = 1;

  Zfb::Builder fbb;
  auto fbo = save(fbb, data);
  ZuCheck(fbo);
  ZuCheck(fbo->ip_type() == Zfb::IP::IPv6);
  ZuCheck(fbo->ip());
  ZuCheck(Zfb::Load::str(fbo->dbID()) == data.dbID);
  ZuCheck(Zfb::Load::str(fbo->id()) == data.id);
  ZuCheck(fbo->priority() == data.priority);
  ZuCheck(fbo->port() == data.port);
  ZuCheck(fbo->state() == Ztc::fbs::DBHostState::Active);
  ZuCheck(fbo->voted() == data.voted);
  ZuCheck(fbo->rag() == Ztc::RAG::Green);

  auto loaded = ZfbStruct::ctor<Ztc::DBHostTelemetry>(fbo);
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
  data.self = "self";
  data.leader = "leader";
  data.prev = "prev";
  data.next = "next";
  data.nCxns = UINT32_C(0x81234567);
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

  Zfb::Builder fbb;
  auto fbo = save(fbb, data);
  ZuCheck(fbo);
  ZuCheck(Zfb::Load::str(fbo->thread()) == data.thread);
  ZuCheck(Zfb::Load::str(fbo->self()) == data.self);
  ZuCheck(Zfb::Load::str(fbo->leader()) == data.leader);
  ZuCheck(Zfb::Load::str(fbo->prev()) == data.prev);
  ZuCheck(Zfb::Load::str(fbo->next()) == data.next);
  ZuCheck(fbo->nCxns() == data.nCxns);
  ZuCheck(fbo->heartbeatFreq() == data.heartbeatFreq);
  ZuCheck(fbo->heartbeatTimeout() == data.heartbeatTimeout);
  ZuCheck(fbo->reconnectFreq() == data.reconnectFreq);
  ZuCheck(fbo->electionTimeout() == data.electionTimeout);
  ZuCheck(fbo->nTables() == data.nTables);
  ZuCheck(fbo->nHosts() == data.nHosts);
  ZuCheck(fbo->nPeers() == data.nPeers);
  ZuCheck(fbo->state() == Ztc::fbs::DBHostState::Inactive);
  ZuCheck(fbo->active() == data.active);
  ZuCheck(fbo->recovering() == data.recovering);
  ZuCheck(fbo->replicating() == data.replicating);
  ZuCheck(fbo->rag() == Ztc::RAG::Amber);

  auto loaded = ZfbStruct::ctor<Ztc::DBTelemetry>(fbo);
  ZuCheck(loaded.thread == data.thread);
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
