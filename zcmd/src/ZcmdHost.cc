//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Zcmd locally hosted commands

#include <zlib/ZtCLI.hh>

#include <zlib/ZcmdHost.hh>

#include <zlib/ZiModule.hh>

namespace Zcmd {

struct Help { ZuCSpan cmd; };
ZtStruct(Help, (((cmd), (CLI::Arg<1>)), (String)));
Fn helpCmd()
{
  return [](Context *ctx, ZiIOBuf *out, const Argv &argv) {
    Host *host = ctx->host;
    Help help;
    unsigned argc = ZtCLI::load(help, argv);
    if (argc > 2) throw Usage();
    if (ZuUnlikely(argc == 2)) {
      auto cmd = host->findCmd(help.cmd);
      if (!cmd) {
	*out << help.cmd << ": unknown command\n";
	executed(ctx, out, 1);
	return;
      }
      *out << cmd->usage << '\n';
      executed(ctx, out, 0);
      return;
    }
    out->ensure(host->nCmds() * 80 + 40);
    *out << "Commands:\n\n";
    host->allCmds([out](ZuCSpan name, const CmdData &data) {
      ZuCSpan tabs = "\t\t";
      if (name.length() >= 8) tabs.offset(1);
      *out << name << tabs << data.brief << '\n';
    });
    executed(ctx, out, 0);
  };
}

struct LoadMod { ZiModule::Path path; };
ZtStruct(LoadMod, (((path), (CLI::Arg<1>)), (String)));
Fn loadModCmd()
{
  return [](Context *ctx, ZiIOBuf *out, const Argv &argv) {
    LoadMod loadMod;
    unsigned argc = ZtCLI::load(loadMod, argv);
    if (argc != 2) throw Usage();
    ZiModule module;
    ZeString e;
    if (module.load(loadMod.path, false, &e) < 0) {
      *out << "failed to load \"" << loadMod.path << "\": " << ZuMv(e) << '\n';
      executed(ctx, out, 1);
      return;
    }
    ZcmdInitFn initFn = reinterpret_cast<ZcmdInitFn>(
      module.resolve("Zcmd_plugin", &e));
    if (!initFn) {
      module.unload();
      *out << "failed to resolve \"Zcmd_plugin\" in \""
	<< loadMod.path << "\": " << ZuMv(e) << '\n';
      executed(ctx, out, 1);
      return;
    }
    (*initFn)(ctx->host);
    *out << "module \"" << loadMod.path << "\" loaded\n";
    executed(ctx, out, 0);
  };
}

void Host::init()
{
  addCmd("help", helpCmd(),
      "list commands", "Usage: help [COMMAND]");
  addCmd("loadmod", loadModCmd(),
      "load application-specific module", "Usage: loadmod MODULE");
}

void Host::final()
{
  while (auto fn = m_finalFn.pop()) fn();
  m_cmds.clean();
}

void Host::addCmd(ZuCSpan name, Fn fn, ZuCSpan brief, ZuCSpan usage)
{
  if (auto cmd = m_cmds.find(name))
    cmd->val() = CmdData{ZuMv(fn), brief, usage};
  else
    m_cmds.add(name, CmdData{ZuMv(fn), brief, usage});
}

bool Host::hasCmd(ZuCSpan name) { return m_cmds.find(name); }

void Host::processCmd(ZmRef<Context> ctx, const Argv &argv)
{
  ZmRef<ZiIOBuf> out = new Zcmd::OutBufAlloc();
  Cmds::Node *cmd;
  try {
    if (cmd = m_cmds.find(argv[0])) {
      (cmd->val().fn)(ctx, out, argv);
      return;
    }
    *out << '"' << argv[0] << "\": unknown command\n";
  } catch (const Usage &) {
    *out << cmd->val().usage << '\n';
  } catch (const ZeException &e) {
    *out << '"' << argv[0] << "\": " << e << '\n';
  } catch (...) {
    *out << '"' << argv[0] << "\": unknown exception\n";
  }
  Zcmd::executed(ZuMv(ctx), ZuMv(out), 1);
}

void Host::finalFn(ZmFn<> fn)
{
  m_finalFn << ZuMv(fn);
}

} // Zcmd
