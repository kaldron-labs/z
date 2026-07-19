//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include <zlib/ZuPolymorph.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtCLI.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiModule.hh>

#include <zlib/ZvCf.hh>
#include <zlib/ZvCSV.hh>

#include <zlib/Ztls.hh>
#include <zlib/ZtlsTOTP.hh>

#include <zlib/ZrlCLI.hh>
#include <zlib/ZrlGlobber.hh>
#include <zlib/ZrlHistory.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/ZcmdClient.hh>
#include <zlib/ZcmdHost.hh>

static void usage()
{
  static const char *usage =
    "Usage: zcmd [USER@][HOST:]PORT [CMD [ARGS]]\n"
    "  USER\t- user (not needed if API key used)\n"
    "  HOST\t- target host (default localhost)\n"
    "  PORT\t- target port\n"
    "  CMD\t- command to send to target\n"
    "  \t  (reads commands from standard input if none specified)\n"
    "  ARGS\t- command arguments\n\n"
    "Environment Variables:\n"
    "  ZCMD_PASSWD\t\tpassword\n"
    "  ZCMD_TOTP_SECRET\tTOTP secret\n"
    "  ZCMD_KEY_ID\t\tAPI key ID\n"
    "  ZCMD_KEY_SECRET\tAPI key secret\n"
    "  ZCMD_CAPATH\t\tCA for validating server TLS certificate\n"
    "  ZCMD_PLUGIN\t\tzcmd plugin module\n";
  std::cerr << usage << std::flush;
  ZiLog::stop();
  Zm::exit(1);
}

class Telcap {
public:
  using Fn = ZmFn<void(const void *), ZmFnHeapID<"zcmd.TelcapFn">>;

  Telcap() { }
  Telcap(Fn fn) : m_fn{ZuMv(fn)} { }
  Telcap(Telcap &&o) : m_fn{ZuMv(o.m_fn)} { }
  Telcap &operator =(Telcap &&o) {
    m_fn(nullptr);
    m_fn = ZuMv(o.m_fn);
    return *this;
  }
  ~Telcap() { m_fn(nullptr); }

  template <typename Data_>
  static Telcap keyedFn(Zi::Path path) {
    using Data = Data_;
    using FBType = ZfbType<Data>;
    using Tree_ =
      ZmRBTree<Data,
	ZmRBTreeKey<ZuFieldAxor<Data>(),
	  ZmRBTreeUnique<true>>>;
    struct Tree : public ZuObject, public Tree_ { };
    ZmRef<Tree> tree = new Tree{};
    return Telcap{[
	tree = ZuMv(tree),
	writer = ZvCSV::writeFile<Data>(path)](const void *fbo_) mutable {
      if (!fbo_) {
	writer.file.close();
	tree->clean();
	return;
      }
      auto fbo = static_cast<const FBType *>(fbo_);
      auto node = tree->find(ZuStructKey(*fbo));
      if (!node)
	tree->addNode(
	  node = new typename Tree::Node{ZfbStruct::ctor<Data>(fbo)});
      else
	ZfbStruct::update(node->data(), fbo);
      writer(node->data());
    }};
  }

  template <typename Data_>
  static Telcap singletonFn(Zi::Path path) {
    using Data = Data_;
    using FBType = ZfbType<Data>;
    return Telcap{[
	writer = ZvCSV::writeFile<Data>(path)](const void *fbo_) mutable {
      if (!fbo_) {
	writer.file.close();
	return;
      }
      auto fbo = static_cast<const FBType *>(fbo_);
      static Data *data = nullptr;
      if (!data)
	data = new Data{ZfbStruct::ctor<Data>(fbo)};
      else
	ZfbStruct::update(*data, fbo);
      writer(*data);
    }};
  }

  template <typename Data_>
  static Telcap alertFn(Zi::Path path) {
    using Data = Data_;
    using FBType = ZfbType<Data>;
    return Telcap{[
	writer = ZvCSV::writeFile<Data>(path)](const void *fbo_) mutable {
      if (!fbo_) {
	writer.file.close();
	return;
      }
      auto fbo = static_cast<const FBType *>(fbo_);
      Data data = ZfbStruct::ctor<Data>(fbo);
      writer(data);
    }};
  }

  void operator ()(const void *p) { m_fn(p); }

private:
  Fn		m_fn;
};

class ZCmd;

class Link : public Zcmd::CliLink<ZCmd, Link> {
public:
  using Base = Zcmd::CliLink<ZCmd, Link>;
  template <typename Server>
  Link(ZCmd *app, Server &&server, uint16_t port);

  void loggedIn();
  void disconnected(bool);
  void connectFailed(bool transient);

  int processTelemetry(ZmRef<ZiIOBuf>);
};

ZuDerive(InBuf, (ZiIOBufAlloc<4096, "Zcmd.InBuf">));

template <typename Host_>
struct CliContextData {
  using Host = Host_;

  Host		*host = nullptr;	// host
  ZiFile	dest;			// output destination
};
template <typename Host, typename Heap = ZuVoid>
struct CliContext_ : public Heap, public ZmObject, public CliContextData<Host> {
  ZuDerive_(CliContext_, CliContextData<Host>)
};
template <typename Host>
ZuDerive(CliContext,
  (CliContext_<Host, ZmHeap<"Zcmd.CliContext", CliContext_<Host>>>));

class ZCmd :
  public ZmPolymorph,
  public Zcmd::Client<ZCmd, Link>,
  public Zcmd::Host<CliContext> {
public:
  using Base = Zcmd::Client<ZCmd, Link>;
  using Host = Zcmd::Host<CliContext>;
  using Context = typename Host::Context;

  using Base::run;
  using Base::invoke;

  using PromptLock = ZmPLock;

friend Link;

  void init(ZiMultiplex *mx, const ZvCf *cf, bool interactive) {
    Base::init(mx, cf);
    m_interactive = interactive;
    Zcmd::Host::init();
    initCmds();
    if (m_interactive)
      m_cli.init(Zrl::App{
	.error = [this](ZuCSpan s) { std::cerr << s << '\n'; done(); },
	.prompt = [this](Zrl::Prompt &s) {
	  ZmGuard guard(m_promptLock);
	  if (m_prompt.owned()) s = ZuMv(m_prompt);
	},
	.enter = [this](ZuCSpan s) -> bool {
	  exec(ZuSpan<char>(const_cast<char *>(&s[0]), s.length()));
	  return false;
	},
	.end = [this]() { done(); },
	.sig = [](int sig) -> bool {
	  switch (sig) {
	    case SIGINT:
	      raise(sig);
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
	.compInit = m_globber.initFn(),
	.compFinal = m_globber.finalFn(),
	.compStart = m_globber.startFn(),
	.compSubst = m_globber.substFn(),
	.compNext = m_globber.nextFn(),
	.histSave = m_history.saveFn(),
	.histLoad = m_history.loadFn()
      });
  }
  void final() {
    m_cli.final();
    for (unsigned i = 0; i < TelDataN; i++) telcapClr(i);
    m_link = nullptr;
    Zcmd::Host::final();
    Base::final();
  }

  bool interactive() const { return m_interactive; }

  void solo(unsigned argc, const char *const *argv) {
    m_soloArgc = argc;
    m_soloArgv = argv;
  }

  template <typename Server, typename ...Args>
  void login(Server &&server, uint16_t port, Args &&...args) {
    m_cli.open(); // idempotent
    Zum::String passwd;
    if (auto passwd_ = ::getenv("ZCMD_PASSWD"))
      passwd = passwd_;
    else
      passwd = m_cli.getpass("password: ", 100);
    if (!passwd) return;
    ZuBox<unsigned> totp;
    if (auto secret_ = ::getenv("ZCMD_TOTP_SECRET")) {
      unsigned n = strlen(secret_);
      ZtArray<uint8_t> secret;
      secret.length(ZuBase32::declen(n));
      secret.length(ZuBase32::decode(secret, {secret_, n}));
      if (secret) totp = Ztls::TOTP::calc(secret);
    } else
      totp = m_cli.getpass("totp: ", 6);
    if (!*totp) return;
    m_link = new Link{this, ZuFwd<Server>(server), port};
    m_link->login(ZuFwd<Args>(args)..., ZuMv(passwd), totp);
  }
  template <typename Server, typename ...Args>
  void access(Server &&server, uint16_t port, Args &&...args) {
    m_link = new Link(this, ZuFwd<Server>(server), port);
    m_link->access(ZuFwd<Args>(args)...);
  }

  void disconnect() { if (m_link) m_link->disconnect(); }

  void wait() { m_done.wait(); }
  void done() { m_done.post(); }

  void sigint() { m_executed.post(); m_done.post(); }

  void exiting() { m_exiting = true; }

  // Zcmd::Host virtual functions
  Zcmd::Dispatcher *dispatcher() { return this; }
  void send(void *link, ZmRef<ZiIOBuf> buf) {
    return static_cast<Link *>(link)->send(ZuMv(buf));
  }
  void target(ZuCSpan s) {
    ZmGuard guard(m_promptLock);
    m_prompt = Zrl::Prompt() << s << "] ";
  }
  Zrl::Passwd getpass(ZuCSpan prompt, unsigned passLen) {
    return m_cli.getpass(prompt, passLen);
  }
  Ztls::Random *rng() { return this; }

  void exec(ZuSpan<char> s) {
    ZtCLI::InCLI in(s);
    exec_(in.argv, in.in, in.out, in.append);
  }

  void exec(unsigned argc, const char *const *argv) {
    ZtCLI::InArgv in(argc, argv);
    exec_(in.argv);
  }

  ZvSeqNo seqNoAlloc() { return m_seqNo++; }

  auto userID() { return m_link->userID(); }

  void sendUserDB(ZmRef<ZiIOBuf> buf, ZvSeqNo seqNo, ZumAckFn fn) {
    m_link->sendUserDB(ZuMv(buf), seqNo, ZuMv(fn));
  }
  void sendCmd(ZmRef<ZiIOBuf> buf, ZvSeqNo seqNo, ZcmdAckFn fn) {
    m_link->sendCmd(ZuMv(buf), seqNo, ZuMv(fn));
  }
  void sendTelReq(ZmRef<ZiIOBuf> buf, ZvSeqNo seqNo, ZtelAckFn fn) {
    m_link->sendTelReq(ZuMv(buf), seqNo, ZuMv(fn));
  }

  int filterAck(
      ZiIOBuf *out,
      const Zum::fbs::ReqAck *ack,
      int ackType1, int ackType2,
      const char *op) {
    using namespace Zum;
    if (ack->rejCode()) {
      *out << '[' << ZuBox<unsigned>(ack->rejCode()) << "] "
	<< Zfb::Load::str(ack->rejText()) << '\n';
      return 1;
    }
    auto ackType = ack->data_type();
    if (int(ackType) != ackType1 &&
	ackType2 >= int(fbs::ReqAckData::MIN) && int(ackType) != ackType2) {
      ZiLOG(Error, "zcmd", ([ackType](auto &s) {
	s << "mismatched ack from server: "
	  << fbs::EnumNameReqAckData(ackType);
      }));
      *out << op << " failed\n";
      return 1;
    }
    return 0;
  }

  void telcapSet(unsigned i, Telcap tc) {
    m_telcap[i] = ZuMv(tc);
  }
  void telcapClr(unsigned i) {
    m_telcap[i] = Telcap{};
  }
  void telcapPending(unsigned n) {
    m_telcapPending = n;
    m_telcapOK = n;
  }

  bool telcapAckd() { return !--m_telcapPending; }
  bool telcapOK() { return !--m_telcapOK; }

  int exitCode() const { return m_exitCode; }

private:
  void loggedIn() {
    if (auto plugin = ::getenv("ZCMD_PLUGIN")) {
      const char *const argv[] = { "loadmod", plugin };
      exec(2, argv);
      if (m_exitCode != 0) { 
	std::cerr << "loadmod \"" << plugin << "\" failed\n";
	done();
	return;
      }
    }
    start();
  }

  void start() {
    if (m_soloArgc) {
      exec(m_soloArgc, m_soloArgv);
      done();
    } else {
      if (m_interactive) {
	std::cout <<
	  "For a list of valid commands: help\n"
	  "For help on a particular command: COMMAND --help\n" << std::flush;
	m_cli.start();
      } else {
	ZiFile in = ZiFile::stdIn();
	ZmRef<ZiIOBuf> buf = new InBuf();
	for (;;) {
	  uint8_t *begin;
	  uint8_t *end;
	  for (;;) {
	    unsigned l = buf->length; // initially 0
	    begin = buf->ensure(l + InBuf::Size);
	    if (ZuUnlikely(!begin)) { done(); return; }
	    int r = in.read(begin + l, InBuf::Size);
	    if (ZuUnlikely(r <= 0)) { done(); return; }
	    end = static_cast<uint8_t *>(memchr(begin + l, '\n', r));
	    buf->length = (l += r);
	    if (end) break;
	  }
	  unsigned l = end - begin;
	  exec(ZuSpan<char>(begin, l));
	  memmove(begin, end, buf->length -= l);
	}
      }
    }
  }

  enum {
    ReqTypeN = Ztel::ReqType::N,
    TelDataN = Ztel::TelData::N
  };

  int processTelemetry(ZmRef<ZiIOBuf> buf) {
    using namespace Ztel;
    {
      Zfb::Verifier verifier{buf->data(), buf->length};
      if (!fbs::VerifyTelemetryBuffer(verifier)) return -1;
    }
    auto msg = fbs::GetTelemetry(buf->data());
    int i = int(msg->data_type());
    if (ZuUnlikely(i < TelData::MIN)) return 0;
    i -= TelData::MIN;
    if (ZuUnlikely(i >= TelDataN)) return 0;
    m_telcap[i](msg->data());
    return buf->length;
  }

  void disconnected(bool) {
    m_executed.post();
    if (m_interactive) {
      m_cli.stop();
      m_cli.close();
    }
    if (m_exiting) {
      done();
      return;
    }
    if (m_interactive) {
      m_cli.final();
      std::cerr << "server disconnected\n" << std::flush;
    }
    Zm::exit(1);
  }
  void connectFailed() {
    if (m_interactive) {
      m_cli.stop();
      m_cli.close();
      m_cli.final();
      std::cerr << "connect failed\n" << std::flush;
    }
    Zm::exit(1);
  }

  void exec_(
    const Zcmd::Argv &argv,
    ZuCSpan in = {},
    ZuCSpan out = {},
    bool append = false)
  {
    if (!argv) return;
    ZmRef<Context> ctx = new Context(this);
    if (out) {
      unsigned flags = ZiFile::Create | ZiFile::WriteOnly;
      if (append) flags |= ZiFile::Append;
      ZiFile outFile(out, flags);
      if (!outFile) {
	ZiLOG(Error, "zcmd",
	  ([file = ZeString{out}, e = outFile.error()](auto &s) {
	    s << "open(\"" << file << "\"): " << e;
	  }));
	return;
      } else
	ctx->dest = ZuMv(outFile);
    } else {
      ctx->dest = ZiFile::stdOut();
    }
    if (in) {
      ZiFile inFile(in, ZiFile::ReadOnly);
      if (!inFile) {
	ZiLOG(Error, "zcmd",
	  ([file = ZeString{in}, e = inFile.error()](auto &s) {
	    s << "open(\"" << file << "\"): " << e;
	  }));
	return;
      }
    }
    bool local, remote = false;
    if (ZuCSpan(argv[0]) == "remote") {
      remote = true;
      local = false;
    } else
      local = hasCmd(argv[0]);
    if (local)
      processCmd(ZuMv(ctx), argv);
    else
      send(ZuMv(ctx), argv, remote);
    m_executed.wait();
  }

  void send(ZmRef<Context> ctx, const Zcmd::Argv &argv, bool remote) {
    Zfb::IOBuilder fbb;
    auto seqNo = seqNoAlloc();
    fbb.Finish(Zcmd::fbs::CreateRequest(fbb, seqNo,
	Zfb::Save::strVecIter(fbb, argv.length() - remote,
	  [&argv, remote](unsigned i) { return argv[i + remote]; })));
    m_link->sendCmd(fbb.buf(), seqNo, [
      ctx = ZuMv(ctx)
    ](const Zcmd::fbs::ReqAck *ack, ZmRef<ZiIOBuf> buf, ZuBSpan out) mutable {
      Zcmd::executed(ZuMv(ctx), ZuMv(buf), ZuMv(out), ack->code());
    });
  }

  using Zcmd::Host::executed;
  void executed(
    ZmRef<Context> ctx, ZmRef<ZiIOBuf> buf, ZuBSpan out, int code)
  {
    invoke([ctx = ZuMv(ctx), buf = ZuMv(buf), out = ZuMv(out), code]() {
      auto zcmd = static_cast<ZCmd *>(ctx->host);
      auto &file = ctx->dest.p<ZiFile>();
      if (out) file.write(&out[0], out.length());
      if (!(file.flags() & ZiFile::StdOut)) {
	file.close();
	file.openStdOut();
      }
      zcmd->executed_(code);
    });
  }

  void initCmds();

  void executed_(int code) {
    m_exitCode = code;
    m_executed.post();
  }

  bool			m_interactive = true;
  unsigned		m_soloArgc = 0;
  const char *const	*m_soloArgv = nullptr;

  ZmSemaphore		m_done;
  ZmSemaphore		m_executed;

  Zrl::Globber		m_globber;
  Zrl::History		m_history{100};
  Zrl::CLI		m_cli;

  ZmRef<Link>		m_link;	
  ZvAtomicSeqNo		m_seqNo;
  int			m_exitCode = 0;

  PromptLock		m_promptLock;
    Zrl::Prompt		  m_prompt;

  bool			m_exiting = false;

  Telcap		m_telcap[TelDataN];
  ZmAtomic<unsigned>	m_telcapPending;
  ZmAtomic<unsigned>	m_telcapOK;
};

using Context = ZCmd::Context;

struct PasswdCmd { };
ZtStruct(PasswdCmd);
Zcmd::Fn passwdCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    PasswdCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 1) throw Zcmd::Usage();
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    auto oldpw = zcmd->getpass("Current password: ", 100);
    auto newpw = zcmd->getpass("New password: ", 100);
    auto checkpw = zcmd->getpass("Re-type new password: ", 100);
    if (checkpw != newpw) {
      *out << "passwords do not match\npassword unchanged!\n";
      Zcmd::executed(ZmMkRef(ctx), ZmMkRef(out), 1);
      return;
    }
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      Zfb::IOBuilder fbb;
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::ChPass,
	fbs::CreateUserChPass(fbb,
	  Zfb::Save::str(fbb, oldpw),
	  Zfb::Save::str(fbb, newpw)).Union()));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
	ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::ChPass), -1, "password change")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	*out << "password changed\n";
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}

struct UsersCmd {
  ZuAssert((ZuIsSame<Zum::UserID, uint64_t>{}));

  ZuBox<uint64_t>	id;
  ZuCSpan		name;
  bool			exclusive = false;
  int			limit = 10;
};
ZtStruct(UsersCmd,
  (((id),        (Ctor<0>, CLI::Opt<'i'>)),  (UInt64)),
  (((name),      (Ctor<1>, CLI::Opt<'n'>)),  (String)),
  (((exclusive), (Ctor<2>, CLI::Flag<'x'>)), (Bool)),
  (((limit),     (Ctor<3>, CLI::Opt<'l'>)),  (UInt16, 10,
						1, Zum::MaxQueryLimit)));
Zcmd::Fn usersCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    UsersCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 1) throw Zcmd::Usage();
    if (*options.id && options.name) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      Zfb::IOBuilder fbb;
      Zfb::Offset<void> fbKey;
      if (*options.id)
	fbKey = fbs::CreateUserID(fbb, options.id).Union();
      else if (options.name)
	fbKey = fbs::CreateUserName(
	    fbb, Zfb::Save::str(fbb, options.name)).Union();
      fbs::UserQueryBuilder fbb_(fbb);
      if (*options.id) {
	fbb_.add_userKey_type(fbs::UserKey::ID);
	fbb_.add_userKey(fbKey);
      } else if (options.name) {
	fbb_.add_userKey_type(fbs::UserKey::Name);
	fbb_.add_userKey(fbKey);
      }
      fbb_.add_inclusive(!options.exclusive);
      fbb_.add_limit(options.limit);
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::UserGet, fbb_.Finish().Union()));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::UserGet), -1, "user get")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto userList = static_cast<const fbs::UserList *>(ack->data());
	Zfb::Load::all(userList->list(), [&out](unsigned, auto user) {
	  *out << *user << '\n';
	});
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
	return;
      });
  };
}
struct UserAddCmd {
  ZuCSpan		name;
  ZtArray<ZuCSpan>	roles;
  bool			enabled = true;
  bool			immutable = false;
};
ZtStruct(UserAddCmd,
  (((name),      (Ctor<0>, CLI::Arg<1>)),    (String)),
  (((roles),     (Ctor<1>, CLI::Arg<2>)),    (StringVec)),
  (((enabled),   (Ctor<2>, CLI::Flag<'e'>)), (Bool)),
  (((immutable), (Ctor<3>, CLI::Flag<'i'>)), (Bool)));
Zcmd::Fn userAddCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    UserAddCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 3) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      auto name = Zfb::Save::str(fbb, options.name);
      auto roles = strVecIter(fbb, options.roles.length(),
	[&options](unsigned i) { return options.roles[i]; });
      uint8_t flags = 0;
      if (options.enabled) flags |= UserFlags::Enabled();
      if (options.immutable) flags |= UserFlags::Immutable();
      fbs::UserBuilder fbb_{fbb};
      fbb_.add_name(name);
      fbb_.add_roles(roles);
      fbb_.add_flags(flags);
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::UserAdd, fbb_.Finish().Union()));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::UserAdd), -1, "user add")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto userPass = static_cast<const fbs::UserPass *>(ack->data());
	*out << *(userPass->user()) << '\n';
	*out << "secret="
	    << ZtQuote::Base32{Zfb::Load::bytes(userPass->user()->secret())}
	    << '\n';
	*out << "passwd=" << Zfb::Load::str(userPass->passwd()) << '\n';
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct ResetPassCmd {
  Zum::UserID	userID;
};
ZtStruct(ResetPassCmd,
  (((userID), (Ctor<0>, CLI::Arg<1>)), (UInt64)));
Zcmd::Fn resetPassCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    ResetPassCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 2) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      Zfb::IOBuilder fbb;
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::ResetPass,
	fbs::CreateUserID(fbb, options.userID).Union()));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::ResetPass), -1, "reset password")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto userPass = static_cast<const fbs::UserPass *>(ack->data());
	*out << *(userPass->user()) << '\n';
	*out << "passwd=" << Zfb::Load::str(userPass->passwd()) << '\n';
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct UserModCmd {
  Zum::UserID		userID;
  ZuCSpan		name;
  ZtArray<ZuCSpan>	roles;
  ZuBox<int8_t>		enabled;
  ZuBox<int8_t>		immutable;
};
ZtStruct(UserModCmd,
  (((userID),    (Ctor<0>, CLI::Arg<1>)),    (UInt64)),
  (((name),      (Ctor<1>, CLI::Opt<'n'>)),  (String)),
  (((roles),     (Ctor<2>, CLI::Opt<'r'>)),  (StringVec)),
  (((enabled),   (Ctor<3>, CLI::Flag<'e'>)), (Bool, ZuCmp<int8_t>::null())),
  (((immutable), (Ctor<4>, CLI::Flag<'i'>)), (Bool, ZuCmp<int8_t>::null())));
Zcmd::Fn userModCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    UserModCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 2) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      Zfb::Offset<Zfb::String> name;
      Zfb::Offset<Zfb::Vector<Zfb::Offset<Zfb::String>>> roles;
      if (options.name) name = str(fbb, options.name);
      if (options.roles)
	roles = strVecIter(fbb, options.roles.length(),
	  [&options](unsigned i) { return options.roles[i]; });
      uint8_t flags = 0;
      bool modFlags = *options.enabled || *options.immutable;
      if (modFlags) {
	if (*options.enabled) flags |= UserFlags::Enabled();
	if (*options.immutable) flags |= UserFlags::Immutable();
      }
      fbs::UserBuilder fbb_{fbb};
      fbb_.add_id(options.userID);
      if (!name.IsNull()) fbb_.add_name(name);
      if (!roles.IsNull()) fbb_.add_roles(roles);
      if (modFlags) fbb_.add_flags(flags);
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::UserMod, fbb_.Finish().Union()));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::UserMod), -1, "user modify")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto user = static_cast<const fbs::User *>(ack->data());
	*out << *user << '\n';
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct UserDelCmd {
  Zum::UserID		userID;
};
ZtStruct(UserDelCmd,
  (((userID),    (Ctor<0>, CLI::Arg<1>)),    (UInt64)));
Zcmd::Fn userDelCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    UserDelCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 2) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      Zfb::IOBuilder fbb;
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::UserDel,
	fbs::CreateUserID(fbb, options.userID).Union()));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::UserDel), -1, "user delete")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto user = static_cast<const fbs::User *>(ack->data());
	*out << *user << '\n';
	*out << "user deleted\n";
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct RolesCmd {
  ZuCSpan	name;
  bool		exclusive = false;
  uint16_t	limit = 10;
};
ZtStruct(RolesCmd,
  (((name),      (Ctor<0>, CLI::Arg<1>)),    (String)),
  (((exclusive), (Ctor<1>, CLI::Flag<'x'>)), (Bool)),
  (((limit),     (Ctor<2>, CLI::Opt<'l'>)),  (UInt16, 10,
						1, Zum::MaxQueryLimit)));
Zcmd::Fn rolesCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    RolesCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc < 1 || argc > 2) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      Zfb::Offset<Zfb::String> name;
      if (options.name) name = str(fbb, options.name);
      fbs::RoleQueryBuilder fbb_(fbb);
      if (options.name) fbb_.add_roleKey(name);
      fbb_.add_inclusive(!options.exclusive);
      fbb_.add_limit(options.limit);
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::RoleGet, fbb_.Finish().Union()));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::RoleGet), -1, "role get")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto roleList = static_cast<const fbs::RoleList *>(ack->data());
	Zfb::Load::all(roleList->list(), [&out](unsigned, auto role) {
	  *out << *role << '\n';
	});
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct RoleAddCmd {
  ZuCSpan		name;
  ZtBitmap		perms;
  ZtBitmap		apiperms;
  bool			immutable = false;
};
ZtStruct(RoleAddCmd,
  (((name),      (Ctor<0>, CLI::Arg<1>)),    (String)),
  (((perms),     (Ctor<1>, CLI::Arg<2>)),    (UDT)),
  (((apiperms),  (Ctor<2>, CLI::Arg<3>)),    (UDT)),
  (((immutable), (Ctor<3>, CLI::Flag<'i'>)), (Bool)));
Zcmd::Fn roleAddCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    RoleAddCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 4) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      uint8_t flags = 0;
      if (options.immutable) flags |= RoleFlags::Immutable();
      Zfb::IOBuilder fbb;
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::RoleAdd,
	fbs::CreateRole(
	  fbb, str(fbb, options.name), bitmap(fbb, options.perms),
	  bitmap(fbb, options.apiperms), flags).Union()));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::RoleAdd), -1, "role add")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto role = static_cast<const fbs::Role *>(ack->data());
	*out << "added " << *role << '\n';
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct RoleModCmd {
  ZuCSpan		name;
  ZtBitmap		perms;
  ZtBitmap		apiperms;
  ZuBox<int8_t>		immutable;
};
ZtStruct(RoleModCmd,
  (((name),      (Ctor<0>, CLI::Arg<1>)),    (String)),
  (((perms),     (Ctor<1>, CLI::Opt<'p'>)),  (UDT)),
  (((apiperms),  (Ctor<2>, CLI::Opt<'a'>)),  (UDT)),
  (((immutable), (Ctor<4>, CLI::Flag<'i'>)), (Bool, ZuCmp<int8_t>::null())));
Zcmd::Fn roleModCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    // need the parser to check if --perms or --apiperms was used
    ZtCLI::Parser<RoleModCmd> parser;
    if (!parser.scanArgv(argv)) throw Zcmd::Usage();
    RoleModCmd options;
    ZtCLI::handler<RoleModCmd>(parser.root).load(options);
    if (parser.argc != 2) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      Zfb::Offset<Zfb::String> name;
      Zfb::Offset<Zfb::Bitmap> perms, apiperms;
      name = str(fbb, options.name);
      if (parser.hasKey("perms")) perms = bitmap(fbb, options.perms);
      if (parser.hasKey("apiperms")) apiperms = bitmap(fbb, options.apiperms);
      uint8_t flags = 0;
      bool modFlags = *options.immutable;
      if (modFlags) {
	if (options.immutable) flags |= RoleFlags::Immutable();
      }
      fbs::RoleBuilder fbb_{fbb};
      fbb_.add_name(name);
      if (!perms.IsNull()) fbb_.add_perms(perms);
      if (!apiperms.IsNull()) fbb_.add_apiperms(apiperms);
      if (modFlags) fbb_.add_flags(flags);
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::RoleMod, fbb_.Finish().Union()));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::RoleMod), -1, "role modify")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto role = static_cast<const fbs::Role *>(ack->data());
	*out << "modified " << *role << '\n';
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct RoleDelCmd {
  ZuCSpan		name;
};
ZtStruct(RoleDelCmd,
  (((name),      (Ctor<0>, CLI::Arg<1>)),    (String)));
Zcmd::Fn roleDelCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    RoleDelCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 2) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::RoleDel,
	fbs::CreateRoleID(fbb, str(fbb, options.name)).Union()));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::RoleDel), -1, "role delete")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto role = static_cast<const fbs::Role *>(ack->data());
	*out << "deleted " << *role << '\n';
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct PermsCmd {
  ZuBox<uint64_t>	id;
  ZuCSpan		name;
  bool			exclusive = false;
  uint16_t		limit = 10;
};
ZtStruct(PermsCmd,
  (((id),        (Ctor<0>, CLI::Opt<'i'>)),  (UInt64)),
  (((name),      (Ctor<1>, CLI::Opt<'n'>)),  (String)),
  (((exclusive), (Ctor<2>, CLI::Flag<'x'>)), (Bool)),
  (((limit),     (Ctor<3>, CLI::Opt<'l'>)),  (UInt16, 10,
						1, Zum::MaxQueryLimit)));
Zcmd::Fn permsCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    PermsCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 1) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      Zfb::Offset<void> fbKey;
      if (*options.id)
	fbKey = fbs::CreatePermID(fbb, options.id).Union();
      else if (options.name)
	fbKey = fbs::CreatePermName(fbb, str(fbb, options.name)).Union();
      fbs::PermQueryBuilder fbb_(fbb);
      if (*options.id) {
	fbb_.add_permKey_type(fbs::PermKey::ID);
	fbb_.add_permKey(fbKey);
      } else if (options.name) {
	fbb_.add_permKey_type(fbs::PermKey::Name);
	fbb_.add_permKey(fbKey);
      }
      fbb_.add_inclusive(!options.exclusive);
      fbb_.add_limit(options.limit);
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::PermGet, fbb_.Finish().Union()
      ));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::PermGet), -1, "perm get")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto permList = static_cast<const fbs::PermList *>(ack->data());
	Zfb::Load::all(permList->list(), [&out](unsigned, auto perm) {
	  *out << *perm << '\n';
	});
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct PermAddCmd {
  ZuCSpan		name;
};
ZtStruct(PermAddCmd,
  (((name),      (Ctor<0>, CLI::Arg<1>)),  (String)));
Zcmd::Fn permAddCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    PermAddCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 2) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      auto name = str(fbb, options.name);
      fbs::PermBuilder fbb_{fbb};
      fbb_.add_name(name);
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::PermAdd, fbb_.Finish().Union()
      ));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::PermAdd), -1, "permission add")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto perm = static_cast<const fbs::Perm *>(ack->data());
	*out << "added " << *perm << '\n';
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct PermModCmd {
  ZuBox<uint64_t>	id;
  ZuCSpan		name;
};
ZtStruct(PermModCmd,
  (((id),        (Ctor<0>, CLI::Arg<1>)),  (UInt64)),
  (((name),      (Ctor<1>, CLI::Arg<2>)),  (String)));
Zcmd::Fn permModCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    PermModCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 3) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::PermMod,
	fbs::CreatePerm(fbb, options.id, str(fbb, options.name)).Union()
      ));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::PermMod), -1, "permission modify")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto perm = static_cast<const fbs::Perm *>(ack->data());
	*out << "modified " << *perm << '\n';
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct PermDelCmd {
  ZuBox<uint64_t>	id;
};
ZtStruct(PermDelCmd,
  (((id),        (Ctor<0>, CLI::Arg<1>)),  (UInt64)));
Zcmd::Fn permDelCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    PermModCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 2) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::PermDel,
	fbs::CreatePermID(fbb, options.id).Union()
      ));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::PermDel), -1, "permission delete")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto perm = static_cast<const fbs::Perm *>(ack->data());
	*out << "deleted " << *perm << '\n';
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct KeysCmd {
  Zum::UserID		userID;
};
ZtStruct(KeysCmd,
  (((userID),    (Ctor<0>, CLI::Arg<1>)),    (UInt64)));
Zcmd::Fn keysCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    KeysCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc < 1 || argc > 2) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      if (argc == 1)
	fbb.Finish(fbs::CreateRequest(
	  fbb, seqNo, fbs::ReqData::OwnKeyGet,
	  fbs::CreateUserID(fbb, zcmd->userID()).Union()
	));
      else
	fbb.Finish(fbs::CreateRequest(
	  fbb, seqNo, fbs::ReqData::KeyGet,
	  fbs::CreateUserID(fbb, options.userID).Union()
	));
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::OwnKeyGet),
	    int(fbs::ReqAckData::KeyGet), "key get")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto keyIDList = static_cast<const fbs::KeyIDList *>(ack->data());
	Zfb::Load::all(keyIDList->list(), [&out](unsigned, auto keyID) {
	  *out << ZtQuote::Base64{Zfb::Load::bytes(keyID->data())} << '\n';
	});
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct KeyAddCmd {
  Zum::UserID		userID;
};
ZtStruct(KeyAddCmd,
  (((userID),    (Ctor<0>, CLI::Arg<1>)),    (UInt64)));
Zcmd::Fn keyAddCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    KeyAddCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc < 1 || argc > 2) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      if (argc == 1)
	fbb.Finish(fbs::CreateRequest(
	  fbb, seqNo, fbs::ReqData::OwnKeyAdd,
	  fbs::CreateUserID(fbb, zcmd->userID()).Union()
	));
      else
	fbb.Finish(fbs::CreateRequest(
	  fbb, seqNo, fbs::ReqData::KeyAdd,
	  fbs::CreateUserID(fbb, options.userID).Union()
	));
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::OwnKeyAdd),
	    int(fbs::ReqAckData::KeyAdd), "key add")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto key = static_cast<const fbs::Key *>(ack->data());
	*out << "added " << *key << '\n';
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct KeyClrCmd {
  Zum::UserID		userID;
};
ZtStruct(KeyClrCmd,
  (((userID),    (Ctor<0>, CLI::Arg<1>)),    (UInt64)));
Zcmd::Fn keyClrCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    KeyClrCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc < 1 || argc > 2) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      if (argc == 1)
	fbb.Finish(fbs::CreateRequest(
	  fbb, seqNo, fbs::ReqData::OwnKeyClr,
	  fbs::CreateUserID(fbb, zcmd->userID()).Union()
	));
      else
	fbb.Finish(fbs::CreateRequest(
	  fbb, seqNo, fbs::ReqData::KeyClr,
	  fbs::CreateUserID(fbb, options.userID).Union()
	));
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::OwnKeyClr),
	    int(fbs::ReqAckData::KeyClr), "key clear")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	*out << "keys cleared\n";
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}
struct KeyDelCmd {
  Zum::KeyIDData	id;
};
ZtStruct(KeyDelCmd,
  (((id),        (Ctor<0>, CLI::Arg<1>)),  (Bytes)));
Zcmd::Fn keyDelCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    KeyDelCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    if (argc != 2) throw Zcmd::Usage();
    using namespace Zum;
    ZmRef<ZiIOBuf> buf;
    auto seqNo = zcmd->seqNoAlloc();
    {
      using namespace Zfb::Save;
      Zfb::IOBuilder fbb;
      fbb.Finish(fbs::CreateRequest(
	fbb, seqNo, fbs::ReqData::KeyDel,
	fbs::CreateKeyID(fbb, bytes(fbb, options.id)).Union()
      ));
      buf = fbb.buf();
    }
    zcmd->sendUserDB(
      ZuMv(buf), seqNo, [
        ctx = ZmMkRef(ctx), out = ZmMkRef(out)
      ](const fbs::ReqAck *ack) mutable {
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	if (int code = zcmd->filterAck(
	    out, ack, int(fbs::ReqAckData::OwnKeyDel),
	    int(fbs::ReqAckData::KeyDel), "key delete")) {
	  Zcmd::executed(ZuMv(ctx), ZuMv(out), code);
	  return;
	}
	auto key = static_cast<const fbs::Key *>(ack->data());
	*out << "deleted " << *key << '\n';
	Zcmd::executed(ZuMv(ctx), ZuMv(out), 0);
      });
  };
}

struct TelcapHeapID : public ZuStringT<"Telcap"> { };
ZuDerive(TelcapSpec, (ZtArray<ZuCSpan, ZtArrayHeapID_<TelcapHeapID>>));
struct TelcapCmd {
  Zi::Path	path;
  TelcapSpec	captures;
  uint32_t	interval = 0;
  bool		unsubscribe = false;
};
ZtStruct(TelcapCmd,
  (((path),        (Ctor<0>, CLI::Arg<1>)),    (String)),
  (((captures),    (Ctor<1>, CLI::Args<2>)),   (StringVec)),
  (((interval),    (Ctor<2>, CLI::Opt<'i'>)),  (UInt32, 0, 100, 1000000)),
  (((unsubscribe), (Ctor<3>, CLI::Flag<'u'>)), (Bool)));
Zcmd::Fn telcapCmd() {
  return [](Context *ctx, ZiIOBuf *out, const Zcmd::Argv &argv) {
    auto zcmd = static_cast<ZCmd *>(ctx->host);
    TelcapCmd options;
    unsigned argc = ZtCLI::load(options, argv);
    using namespace Ztel;
    if (options.unsubscribe) {
      if (argc > 2) throw Zcmd::Usage();
    } else {
      if (argc < 3) throw Zcmd::Usage();
    }
    ZtArray<int, ZtArrayHeapID_<TelcapHeapID>> types;
    ZtArray<ZuCSpan, ZtArrayHeapID_<TelcapHeapID>> filters;
    auto n = options.captures.length();
    if (!n) {
      n = ReqType::N;
      types.length(n);
      filters.length(n);
      for (unsigned i = 0; i < n; i++) {
	types[i] = ReqType::MIN + i;
	filters[i] = "*";
      }
    } else {
      types.length(n);
      filters.length(n);
      for (unsigned i = 0; i < n; i++) {
	ZtRegexCaptures(c, 0);
	const auto &capture = options.captures[i];
	if (ZtREGEX(":").m(capture, c)) {
	  types[i] = Ztel::ReqType::lookup(c[0]);
	  filters[i] = c[2];
	} else {
	  types[i] = Ztel::ReqType::lookup(capture);
	  filters[i] = "*";
	}
	if (types[i] < 0) throw Zcmd::Usage();
      }
    }
    bool subscribe = !options.unsubscribe;
    Zi::Path dir;
    if (subscribe) {
      dir = ZuMv(options.path);
      ZiFile::age(dir, 10);
      ZeError e;
      if (ZiFile::mkdir(dir, &e) != Zi::OK) {
	*out << dir << ": " << e << '\n';
	Zcmd::executed(ctx, out, 1);
	return;
      }
    }
    zcmd->run([
      ctx = ZmMkRef(ctx), out = ZmMkRef(out),
      types = ZuMv(types), filters = ZuMv(filters),
      interval = options.interval, subscribe, dir = ZuMv(dir)
    ]() {
      // runs on TLS thread
      auto zcmd = static_cast<ZCmd *>(ctx->host);
      auto n = types.length();
      for (unsigned i = 0; i < n; i++) {
	switch (types[i]) {
	  case ReqType::Heap:
	    if (subscribe)
	      zcmd->telcapSet(TelData::Heap - TelData::MIN,
		Telcap::keyedFn<Heap>(ZiFile::append(dir, "heap.csv")));
	    else
	      zcmd->telcapClr(TelData::Heap - TelData::MIN);
	    break;
	  case ReqType::HashTbl:
	    if (subscribe)
	      zcmd->telcapSet(TelData::HashTbl - TelData::MIN,
		Telcap::keyedFn<HashTbl>(ZiFile::append(dir, "hash.csv")));
	    else
	      zcmd->telcapClr(TelData::HashTbl - TelData::MIN);
	    break;
	  case ReqType::Thread:
	    if (subscribe)
	      zcmd->telcapSet(TelData::Thread - TelData::MIN,
		Telcap::keyedFn<Thread>(ZiFile::append(dir, "thread.csv")));
	    else
	      zcmd->telcapClr(TelData::Thread - TelData::MIN);
	    break;
	  case ReqType::Mx:
	    if (subscribe) {
	      zcmd->telcapSet(TelData::Mx - TelData::MIN,
		Telcap::keyedFn<Mx>(ZiFile::append(dir, "mx.csv")));
	      zcmd->telcapSet(TelData::Socket - TelData::MIN,
		Telcap::keyedFn<Socket>(ZiFile::append(dir, "socket.csv")));
	    } else {
	      zcmd->telcapClr(TelData::Mx - TelData::MIN);
	      zcmd->telcapClr(TelData::Socket - TelData::MIN);
	    }
	    break;
	  case ReqType::Queue:
	    if (subscribe)
	      zcmd->telcapSet(TelData::Queue - TelData::MIN,
		Telcap::keyedFn<Queue>(ZiFile::append(dir, "queue.csv")));
	    else
	      zcmd->telcapClr(TelData::Queue - TelData::MIN);
	    break;
	  case ReqType::Engine:
	    if (subscribe) {
	      zcmd->telcapSet(TelData::Engine - TelData::MIN,
		Telcap::keyedFn<Ztel::Engine>(
		  ZiFile::append(dir, "engine.csv")));
	      zcmd->telcapSet(TelData::Link - TelData::MIN,
		Telcap::keyedFn<Ztel::Link>(
		  ZiFile::append(dir, "link.csv")));
	    } else {
	      zcmd->telcapClr(TelData::Engine - TelData::MIN);
	      zcmd->telcapClr(TelData::Link - TelData::MIN);
	    }
	    break;
	  case ReqType::DB:
	    if (subscribe) {
	      zcmd->telcapSet(TelData::DB - TelData::MIN,
		Telcap::singletonFn<DB>(ZiFile::append(dir, "dbenv.csv")));
	      zcmd->telcapSet(TelData::DBHost - TelData::MIN,
		Telcap::keyedFn<DBHost>(ZiFile::append(dir, "dbhost.csv")));
	      zcmd->telcapSet(TelData::DBTable - TelData::MIN,
		Telcap::keyedFn<DBTable>(ZiFile::append(dir, "db.csv")));
	    } else {
	      zcmd->telcapClr(TelData::DB - TelData::MIN);
	      zcmd->telcapClr(TelData::DBHost - TelData::MIN);
	      zcmd->telcapClr(TelData::DBTable - TelData::MIN);
	    }
	    break;
	  case ReqType::App:
	    if (subscribe)
	      zcmd->telcapSet(TelData::App - TelData::MIN,
		Telcap::singletonFn<Ztel::App>(
		  ZiFile::append(dir, "app.csv")));
	    else
	      zcmd->telcapClr(TelData::App - TelData::MIN);
	    break;
	  case ReqType::Alert:
	    if (subscribe)
	      zcmd->telcapSet(TelData::Alert - TelData::MIN,
		Telcap::alertFn<Alert>(ZiFile::append(dir, "alert.csv")));
	    else
	      zcmd->telcapClr(TelData::Alert - TelData::MIN);
	    break;
	}
      }
      zcmd->telcapPending(n);
      auto types_ = &types[0]; // survives move-capture of types
      using AckFn = ZmFn<void(const fbs::ReqAck *, unsigned),
	ZmFnHeapID<"zcmd.Telcap.AckFn">>;
      AckFn ackFn = [
	ctx = ZuMv(ctx), out = ZuMv(out),
	types = ZuMv(types), filters = ZuMv(filters)
      ](const fbs::ReqAck *ack, unsigned i) mutable {
	// runs on TLS thread
	auto zcmd = static_cast<ZCmd *>(ctx->host);
	bool allOK = false;
	auto reqNames = fbs::EnumNamesReqType();
	if (ack->ok())
	  allOK = zcmd->telcapOK();
	else
	  *out << "telemetry request " << reqNames[types[i]]
	    << ':' << filters[i] << " rejected\n";
	if (zcmd->telcapAckd()) Zcmd::executed(ZuMv(ctx), ZuMv(out), !allOK);
      };
      for (unsigned i = 0; i < n; i++) {
	using namespace Zfb::Save;
	auto seqNo = zcmd->seqNoAlloc();
	Zfb::IOBuilder fbb;
	fbb.Finish(fbs::CreateRequest(
	  fbb, seqNo, str(fbb, filters[i]), interval,
	  static_cast<fbs::ReqType>(types_[i]), subscribe));
	zcmd->sendTelReq(fbb.buf(), seqNo, [ackFn, i](const fbs::ReqAck *ack) {
	  ackFn(ack, i);
	});
      }
      if (subscribe) {
	if (!interval)
	  *out << "telemetry queried\n";
	else
	  *out << "telemetry subscribed\n";
      } else
	*out << "telemetry unsubscribed\n";
    });
  };
}

void ZCmd::initCmds()
{
  addCmd("passwd", passwdCmd(),
    "change passwd", "Usage: passwd");

  addCmd("users", usersCmd(),
    "list users", "Usage: users [OPTIONS...]\n\n"
    "  -i, --id=ID\t\tquery from user ID\n"
    "  -n, --name=NAME\tquery from user NAME\n"
    "  -x, --exclusive\texclude ID|NAME from results\n"
    "  -l, --limit=N\t\tlimit results to N (default: 10)");
  addCmd("useradd", userAddCmd(),
    "add user",
    "Usage: useradd NAME ROLE[,ROLE]... [OPTION]...\n\n"
    "Options:\n"
    "  -e, --enabled\t\tset Enabled flag\n"
    "  -i, --immutable\tset Immutable flag");
  addCmd("resetpass", resetPassCmd(),
    "reset password", "Usage: resetpass USERID");
  addCmd("usermod", userModCmd(),
    "modify user",
    "Usage: usermod ID [OPTION]...\n\n"
    "Options:\n"
    "  -n, --name=NAME\t\t\tset name\n"
    "  -r, --roles=ROLE[,ROLE]...\tset roles\n"
    "  -e, --enabled=[0|1]\t\tset/clear Enabled flag\n"
    "  -i, --immutable=[0|1]\t\tset/clear Immutable flag");
  addCmd("userdel", userDelCmd(),
    "delete user", "Usage: userdel ID");

  addCmd("roles", rolesCmd(),
    "list roles", "Usage: roles [NAME] [OPTIONS...]\n\n"
    "  -x, --exclusive\texclude NAME from results\n"
    "  -l, --limit=N\t\tlimit results to N (default: 10)");
  addCmd("roleadd", roleAddCmd(),
    "add role",
    "Usage: roleadd NAME PERMS APIPERMS [OPTIONS...]\n\n"
    "Options:\n"
    "  -i, --immutable\tset Immutable flag");
  addCmd("rolemod", roleModCmd(),
    "modify role",
    "Usage: rolemod NAME [OPTIONS...]\n\n"
    "Options:\n"
    "  -p, --perms=PERMS\tset permissions\n"
    "  -a, --apiperms=PERMS\tset API permissions\n"
    "  -i, --immutable=[0|1]\tset/clear Immutable flag");
  addCmd("roledel", roleDelCmd(),
    "delete role",
    "Usage: roledel NAME");

  addCmd("perms", permsCmd(),
    "list permissions", "Usage: perms [OPTIONS...]\n\n"
    "  -i, --id=ID\t\tquery from permission ID\n"
    "  -n, --name=NAME\tquery from permission NAME\n"
    "  -x, --exclusive\texclude ID|NAME from results\n"
    "  -l, --limit=N\t\tlimit results to N (default: 10)");
  addCmd("permadd", permAddCmd(),
    "add permission", "Usage: permadd NAME");
  addCmd("permmod", permModCmd(),
    "modify permission", "Usage: permmod ID NAME");
  addCmd("permdel", permDelCmd(),
    "delete permission", "Usage: permdel ID");

  addCmd("keys", keysCmd(),
    "list keys", "Usage: keys [USERID]");
  addCmd("keyadd", keyAddCmd(),
    "add key", "Usage: keyadd [USERID]");
  addCmd("keydel", keyDelCmd(),
    "delete key", "Usage: keydel ID");
  addCmd("keyclr", keyClrCmd(),
    "clear all keys", "Usage: keyclr [USERID]");

  addCmd("remote", {},
    "run command remotely", "Usage: remote COMMAND [OPTION]...");

  addCmd("telcap", telcapCmd(),
    "telemetry capture",
    "Usage: telcap [OPTION]... PATH [TYPE[:FILTER]]...\n\n"
    "  PATH\t\tdirectory for capture CSV files\n"
    "  TYPE\t\t[Heap|HashTbl|Thread|Mx|Queue|Engine|DB|App|Alert]\n"
    "  FILTER\tfilter specification in type-specific format\n\n"
    "Options:\n"
    "  -i, --interval=N\tset scan interval in milliseconds "
      "(100 <= N <= 1M)\n"
    "  -u, --unsubscribe\tunsubscribe (i.e. end capture)");
}

template <typename Server>
inline Link::Link(ZCmd *app, Server &&server, uint16_t port) :
    Base{app, ZuFwd<Server>(server), port} { }

inline void Link::loggedIn()
{
  this->app()->loggedIn();
}
inline void Link::disconnected(bool peer)
{
  this->app()->disconnected();
  Base::disconnected(peer);
}
inline void Link::connectFailed(bool transient)
{
  this->app()->connectFailed();
}

inline int Link::processTelemetry(ZmRef<ZiIOBuf> buf)
{
  return this->app()->processTelemetry(ZuMv(buf));
}

ZmRef<ZCmd> client;

void sigint() { if (client) client->sigint(); }

int main(int argc, char **argv)
{
  if (argc < 2) usage();

  ZiLog::init("zcmd");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::lambdaSink([](ZeLogBuf &buf, const ZeEventInfo &) {
    buf << '\n';
    std::cerr << buf << std::flush;
  }));
  ZiLog::start();

  bool interactive = Zrl::interactive();
  ZuCSpan keyID = ::getenv("ZCMD_KEY_ID");
  ZuCSpan secret = ::getenv("ZCMD_KEY_SECRET");
  ZtString<> user, server;
  ZuBox<unsigned> port;

  try {
    {
      ZtRegexCaptures(c, 3);
      if (ZtREGEX("^([^@]+)@([^:]+):(\d+)$").m(argv[1], c) == 4) {
	user = c[2];
	server = c[3];
	port = c[4];
      }
    }
    if (!user) {
      ZtRegexCaptures(c, 2);
      if (ZtREGEX("^([^@]+)@(\d+)$").m(argv[1], c) == 3) {
	user = c[2];
	server = "localhost";
	port = c[3];
      }
    }
    if (!user) {
      ZtRegexCaptures(c, 2);
      if (ZtREGEX("^([^:]+):(\d+)$").m(argv[1], c) == 3) {
	server = c[2];
	port = c[3];
      }
    }
    if (!server) {
      ZtRegexCaptures(c, 1);
      if (ZtREGEX("^(\d+)$").m(argv[1], c) == 2) {
	server = "localhost";
	port = c[2];
      }
    }
  } catch (const ZtRegexError &) {
    usage();
  }
  if (!server || !*port || !port) usage();
  if (user)
    keyID = secret = {};
  else if (!keyID) {
    std::cerr << "set ZCMD_KEY_ID and ZCMD_KEY_SECRET "
      "to use without username\n" << std::flush;
    ::exit(1);
  }
  if (keyID) {
    if (!secret) {
      std::cerr << "set ZCMD_KEY_SECRET "
	"to use with ZCMD_KEY_ID\n" << std::flush;
      ::exit(1);
    }
  } else {
    if (!interactive || argc > 2) {
      std::cerr << "set ZCMD_KEY_ID and ZCMD_KEY_SECRET "
	"to use non-interactively\n" << std::flush;
      ::exit(1);
    }
  }

  ZiMultiplex *mx = new ZiMultiplex(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(4)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  mx->start();

  client = new ZCmd();

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  {
    ZmRef<ZvCf> cf = new ZvCf();
    cf->set("timeout", "1");
    cf->set("rxThread", "3");
    cf->set("txThread", "4");
    if (auto caPath = ::getenv("ZCMD_CAPATH"))
      cf->set("caPath", caPath);
    else
      cf->set("caPath", "/etc/ssl/certs");
    try {
      client->init(mx, cf, interactive);
    } catch (const ZeException &e) {
      std::cerr << e << '\n' << std::flush;
      ::exit(1);
    } catch (...) {
      std::cerr << "unknown exception\n" << std::flush;
      ::exit(1);
    }
  }

  if (argc > 2)
    client->solo(argc - 2, static_cast<const char *const *>(&argv[2]));
  else
    client->target(argv[1]);

  if (keyID)
    client->access(ZuMv(server), port, ZuMv(keyID), ZuMv(secret));
  else
    client->login(ZuMv(server), port, ZuMv(user));

  client->wait();

  if (client->interactive()) {
    std::cerr << std::flush;
    std::cout << std::flush;
  }

  client->exiting();
  client->disconnect();
  client->wait();

  mx->stop();

  ZiLog::stop();

  client->final();

  delete mx;

  ZmTrap::sigintFn(nullptr);

  return client->exitCode();
}
