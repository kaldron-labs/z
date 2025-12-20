//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface

#include <zlib/Zrl.hh>

#include <zlib/ZuDerive.hh>

#include <zlib/ZmSingleton.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmCondition.hh>

#include <zlib/ZrlCLI.hh>
#include <zlib/ZrlGlobber.hh>
#include <zlib/ZrlHistory.hh>

namespace {

class Context {
  using Lock = ZmLock;
  using Guard = ZmGuard<Lock>;
  ZuDerive(Cond, (ZmCondition<Lock>));

private:
  // Finite State Machine
  enum {
    Stopped,
    Editing,
    Processing
  };
  // 		 Stopped    Editing    Processing
  // 		 -------    -------    ----------
  // readline()  Editing               Editing
  // app.enter()            Processing
  // app.end()              Stopped
  // SIGINT                 Stopped
  // app.error()            Stopped
  // stop()                 Stopped    Stopped

  Lock			lock;
    Zrl::Globber	  globber;
    Zrl::History	  history{100};
    Zrl::CLI		  cli;
    Zrl::Prompt		  prompt;
    char		  *data = nullptr;
    Cond		  cond{lock};
    int			  state = Stopped;

public:
  Context() {
    cli.init(Zrl::App{
      .error = [this](ZuCSpan s) { std::cerr << s << '\n'; stop(); },
      .prompt = [this](Zrl::Prompt &s) {
	Guard guard(lock);
	if (prompt.owned()) s = ZuMv(prompt);
      },
      .enter = [this](ZuCSpan s) -> bool { return process(s); },
      .end = [this]() { stop(); },
      .sig = [this](int sig) -> bool {
	switch (sig) {
	  case SIGINT:
	    stop();
	    return true;
#ifdef _WIN32
	  case SIGQUIT:
	    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0);
	    return true;
#endif
	  case SIGTSTP:
	    raise(sig);
	    return false;
	  default:
	    return false;
	}
      },
      .compInit = globber.initFn(),
      .compFinal = globber.finalFn(),
      .compStart = globber.startFn(),
      .compSubst = globber.substFn(),
      .compNext = globber.nextFn(),
      .histSave = history.saveFn(),
      .histLoad = history.loadFn()
    });
  }
  ~Context() {
    cli.final();
  }

  void start(const char *prompt_) {
    Guard guard(lock);
    if (prompt_) prompt.copy(prompt_);
    if (state == Stopped) start_();
  }
private:
  void start_() {
    if (!cli.open()) {
      state = Stopped;
      cond.broadcast();
      return;
    }
    cli.start();
    state = Editing;
    cond.broadcast();
    while (state == Editing) cond.wait();
  }

  void stop_() {
    cli.stop();
    cli.close();
  }

public:
  void stop() {
    Guard guard(lock);
    state = Stopped;
    cond.broadcast();
  }

  bool running() {
    Guard guard(lock);
    return state != Stopped;
  }

private:
  void edit_() {
    state = Editing;
    cond.broadcast();
    while (state == Editing) cond.wait();
  }

  bool process(ZuCSpan s) {
    Guard guard(lock);
    {
      unsigned n = s.length();
      data = static_cast<char *>(::malloc(n + 1));
      memcpy(data, s.data(), n);
      data[n] = 0;
    }
    state = Processing;
    cond.broadcast();
    while (state == Processing) cond.wait();
    return state == Stopped;
  }

public:
  char *readline(const char *prompt_) {
    Guard guard(lock);
    if (prompt_) prompt.copy(prompt_);
    switch (state) {
      case Stopped:
	start_();
	if (state == Stopped) return nullptr;
	break;
      case Processing:
	edit_();
	break;
      default:
	return nullptr;	// multiple overlapping readline() calls
    }
    if (state == Stopped) {
      stop_();
      return nullptr;
    }
    return data; // caller frees
  }
};

inline Context *instance() {
  return ZmSingleton<Context>::instance();
}

}

ZrlExtern char *Zrl_readline(const char *prompt)
{
  return instance()->readline(prompt);
}

ZrlExtern void Zrl_start(const char *prompt)
{
  instance()->start(prompt);
}

ZrlExtern void Zrl_stop()
{
  instance()->stop();
}

ZrlExtern bool Zrl_running()
{
  return instance()->running();
}
