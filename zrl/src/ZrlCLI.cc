//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface

#include <zlib/ZrlCLI.hh>

#include <zlib/ZtRegex.hh>

using namespace Zrl;

namespace {
  static ZmSemaphore done;
  static bool openOK = false;
}

void CLI::init(App app)
{
  Guard guard(m_lock);
  switch (m_state) {
    case Running:
    case Opened:
    case Initialized:
      return;
    case Created:
      break;
  }
  m_state = Initialized;
  app.end = [end = ZuMv(app.end)]() { done.post(); end(); };
  app.sig = [sig = ZuMv(app.sig)](int i) {
    switch (i) {
      case SIGINT:
      case SIGQUIT:
	done.post();
	sig(i);
	return true;
      default:
	sig(i);
	return false;
    }
  };
  app.open = [open = ZuMv(app.open)](bool ok) {
    open(ok);
    openOK = ok;
    done.post();
  };
  Editor::init(ZuMv(app));
  m_sched = new ZmScheduler{ZmSchedParams{}.id("ZrlCLI").nThreads(1)};
  if (auto maps = ::getenv("ZRL_MAPS")) {
    ZtRegexCaptures(c, 2); // more than 2 will fall back to heap
    if (ZtREGEX(":").split(maps, c) > 0)
      for (const auto &map : c)
	try {
	  loadMap(map, false);
	} catch (const ZeException &e) {
	  std::cerr << e << '\n' << std::flush;
	}
  }
  if (auto map = ::getenv("ZRL_MAP"))
    try {
      loadMap(map, true);
    } catch (const ZeException &e) {
      std::cerr << e << '\n' << std::flush;
    }
  if (auto mapID = ::getenv("ZRL_MAPID"))
    map(mapID);
}

void CLI::final()
{
  Guard guard(m_lock);
  switch (m_state) {
    case Running:
      stop_();
    case Opened:
      close_();
    case Initialized:
      break;
    case Created:
      return;
  }
  final_();
  m_state = Created;
}

void CLI::final_()
{
  Editor::final();
  m_sched = nullptr;
}

bool CLI::open()
{
  Guard guard(m_lock);
  switch (m_state) {
    case Running:
    case Opened:
      return true;
    case Initialized:
      break;
    case Created:
      return false;
  }
  bool ok = open_();
  if (ok) m_state = Opened;
  return ok;
}

bool CLI::open_()
{
  m_sched->start();
  Editor::open(m_sched, 1);
  done.wait();
  return openOK;
}

void CLI::close()
{
  Guard guard(m_lock);
  switch (m_state) {
    case Running:
      stop_();
    case Opened:
      break;
    case Initialized:
    case Created:
      return;
  }
  close_();
  m_state = Initialized;
}

void CLI::close_()
{
  Editor::close();
  m_sched->stop();
}

bool CLI::start()
{
  Guard guard(m_lock);
  switch (m_state) {
    case Running:
      return true;
    case Opened:
      break;
    case Initialized:
    case Created:
      return false;
  }
  Editor::start();
  m_state = Running;
  return true;
}

void CLI::stop()
{
  Guard guard(m_lock);
  switch (m_state) {
    case Running:
      break;
    case Opened:
    case Initialized:
    case Created:
      return;
  }
  stop_();
  m_state = Opened;
}

void CLI::stop_()
{
  Editor::stop();
}

void CLI::join()
{
  done.wait();
}
