//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiModule.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZpktEnv.hh>
#include "ZpktTestCf.hh"
#include "ZpktTestPlugin.hh"

using namespace ZuTestUtil;

static unsigned queries = 0;
static Zpkt::NifInfo metadata{731, 9000, 7, {2, 3, 4, 5, 6, 7}};
static ZuUnion<Zpkt::NifInfo, ZeException> query(ZuCSpan name)
{
  ++queries;
  if (name == "query-error"_z) return ZeEXCEPT(Error, "ZpktTest", "injected kernel query error");
  return metadata;
}

static ZtString<> path(ZuCSpan file)
{
  return ZtString<>{} << ZPKT_TEST_MODULE_DIR << '/' << file;
}

static ZpktTest::Stats *pluginStats()
{
  ZiModule module;
  ZeString error;
  if (module.load(path("libZpktTestPlugin.so"), 0, &error) < 0) return nullptr;
  auto fn = reinterpret_cast<ZpktTest::StatsFn>(module.resolve("ZpktTestStats", &error));
  return fn ? (*fn)() : nullptr;
}

static ZtString<> errorText(ZeException error)
{
  ZeLogBuf message;
  if (error) error.fn()(message, error);
  return ZtString<>{message.data(), message.length()};
}

static void testLoader()
{
  ZuTestScopeRT(testLoader);
  constexpr const char *failures[] = {
    "missing.so", "libZpktNoFactory.so", "libZpktNullPlugin.so",
    "libZpktThrowPlugin.so", "libZpktInitFailPlugin.so"
  };
  constexpr const char *messages[] = {
    "failed to load", "failed to resolve", "null ZpktPlugin result",
    "injected entry-point failure", "injected plugin construction/EAL initialization failure"
  };
  unsigned scenario = 0;
  for (auto file : failures) {
    ZmRef<Zpkt::Env> env = new Zpkt::Env;
    auto cf = ZpktTest::config(path(file));
    ZeException failure;
    try { env->init(cf); }
    catch (const ZeException &error) { failure = error; }
    auto text = errorText(failure);
    ZuCheckRT(failure && failure.severity == Ze::Error && failure.line > 0 &&
	failure.file && failure.function && strstr(text.ndata(), messages[scenario]), log_(
      "loader failure preserves original structured event and diagnostic: ", text));
    if (scenario < 3) {
      ZuCheckRT(failure.component == "Zpkt"_z && strstr(text.ndata(), file), log_(
	"loader errors retain module path and loader context"));
      if (scenario == 1) ZuCheckRT(strstr(text.ndata(), ZpktPluginFnSym), log_(
	"missing entry point retains symbol context"));
    } else {
      ZuCheckRT(failure.component == "ZpktTest"_z &&
	strstr(ZtString<>{failure.file}.ndata(), "ZpktTestPlugin.cc"), log_(
	"factory exceptions preserve originating plugin context"));
    }
    ++scenario;
    ZuCheckRT(env->final().is<void>(), log_("failed initialization publishes no plugin"));
    cf = ZpktTest::config(path("libZpktTestPlugin.so"));
    env->init(cf);
    ZuCheckRT(env->final().is<void>() && env->final().is<void>(), log_(
      "failed initialization permits a successful retry and idempotent finalization"));
  }
  auto stats = pluginStats();
  ZuCheckRT(stats, log_("success module remains callable after local ZiModule destruction"));
}

static void testNamedFactory()
{
  ZuTestScope(testNamedFactory);
  auto stats = pluginStats();
  ZuCHECK(stats);
  if (!stats) return;
  *stats = {};
  queries = 0;
  ZiMultiplex mx, other;
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  env->nifQueryFn(query);
  auto cf = ZpktTest::config(path("libZpktTestPlugin.so"), "synthetic", 3, 4321, 0);
  env->init(cf);
  cf = nullptr;
  auto opened = env->nif(&mx, "synthetic");
  ZuCHECK(opened.is<Zpkt::Nif *>(), "mock query survives init and no kernel interface is needed");
  if (opened.is<ZeException>()) return;
  ZmRef<Zpkt::Nif> nif = opened.p<Zpkt::Nif *>();
  ZuCHECK(nif->env() == env.ptr() && nif->name() == "synthetic"_z &&
      nif->mx() == &mx && nif->state() == ZmEngineState::Stopped && !nif->allocated(),
    "Env owns a fully initialized stopped child with a raw parent pointer");
  ZuCHECK(queries == 1 && stats->creates == 1 && stats->rxSpin == 4321 && !stats->txSpin &&
      stats->info.ifindex == metadata.ifindex && stats->info.mtu == metadata.mtu &&
      stats->info.rxQueues == metadata.rxQueues && !memcmp(stats->info.mac, metadata.mac, 6),
    "owned config and unmodified synthetic kernel metadata reach the module");
  auto again = env->nif(&mx, "synthetic");
  ZuCHECK(again.is<Zpkt::Nif *>() && again.p<Zpkt::Nif *>() == nif.ptr() &&
      queries == 1 && stats->creates == 1, "cache hit makes no query or plugin call");
  ZuCHECK(nif->start([](uint8_t *, unsigned, ZiRxBurst *) { }), "test device starts");
  again = env->nif(&mx, "synthetic");
  ZuCHECK(again.is<Zpkt::Nif *>() && again.p<Zpkt::Nif *>() == nif.ptr() && queries == 1,
    "lookup while running returns the same device");
  ZuCHECK(env->nif(&other, "synthetic").is<ZeException>() && queries == 1,
    "conflicting multiplexer leaves the existing child unchanged");
  ZuCHECK(nif->stop());
  auto calls = stats->creates;
  ZuCHECK(env->nif(&mx, "query-error").is<ZeException>() && stats->creates == calls,
    "query error does not invoke plugin");
  ZuCHECK(env->nif(&mx, "unknown").is<ZeException>(), "unknown configuration key is an error");
  nif->final();
  ZuCHECK(!nif->env() && stats->finals == 1, "successful final retires cache membership");
  opened = env->nif(&mx, "synthetic");
  ZmRef<Zpkt::Nif> replacement = opened.p<Zpkt::Nif *>();
  nif->final();
  again = env->nif(&mx, "synthetic");
  ZuCHECK(replacement != nif && again.p<Zpkt::Nif *>() == replacement.ptr(),
    "old finalization cannot remove a replacement child");
  replacement = nullptr;
  ZuCHECK(stats->destroys == 0, "tree retains its own child reference");
  ZuCHECK(env->final().is<void>() && stats->finals == 2 && stats->destroys == 1 &&
      stats->pluginFinals == 1 && stats->pluginDestroys == 0,
    "Env finalizes children before releasing Plugin");
  nif = nullptr;
  ZuCHECK(stats->destroys == 2 && env->final().is<void>(), "external children retire before Env release");
  env = nullptr;
  ZuCHECK(stats->pluginDestroys == 1, "last Env reference destroys its Plugin");
}

static void testCreationFailure()
{
  ZuTestScope(testCreationFailure);
  auto stats = pluginStats();
  *stats = {};
  ZiMultiplex mx;
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  env->nifQueryFn(query);
  auto cf = ZpktTest::config(path("libZpktTestPlugin.so"), "bad", 3, 0);
  env->init(cf);
  ZuCHECK(env->nif(&mx, "bad").is<ZeException>() && env->nif(&mx, "bad").is<ZeException>() &&
      stats->creates == 2 && stats->finals == 0,
    "failed construction is never cached");
  ZuCHECK(env->final().is<void>());
  env = nullptr;
  auto before = stats->pluginDestroys;
  env = new Zpkt::Env;
  env->init(cf);
  env = nullptr;
  ZuCHECK(stats->pluginDestroys == before + 1, "last Env reference releases its Plugin");
}

static void testFinalFailure()
{
  ZuTestScope(testFinalFailure);
  auto stats = pluginStats();
  ZuCHECK(stats);
  if (!stats) return;
  *stats = {};
  queries = 0;
  ZiMultiplex mx;
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  env->nifQueryFn(query);
  auto cf = ZpktTest::config(path("libZpktTestPlugin.so"));
  ZpktTest::Node *interfaces = nullptr;
  for (auto &field : static_cast<ZpktTest::Node *>(cf.ptr())->data<ZpktTest::Node::Object>())
    if (field.p<0>() == "interfaces") interfaces = field.p<1>().ptr();
  ZuCHECK(interfaces);
  if (!interfaces) return;
  for (auto name : {"first", "second"}) {
    auto child = ZpktTest::object(interfaces, name);
    ZpktTest::value(child, "txSid", 3);
    ZpktTest::value(child, "rxSpin", 1000);
  }
  env->init(cf);
  cf = nullptr;
  auto opened = env->nif(&mx, "first");
  auto other = env->nif(&mx, "second");
  ZuCHECK(opened.is<Zpkt::Nif *>() && other.is<Zpkt::Nif *>() &&
      opened.p<Zpkt::Nif *>() != other.p<Zpkt::Nif *>(),
    "tree owns distinct fully initialized children");
  if (opened.is<ZeException>() || other.is<ZeException>()) return;
  ZmRef<Zpkt::Nif> first = opened.p<Zpkt::Nif *>();
  ZmRef<Zpkt::Nif> second = other.p<Zpkt::Nif *>();
  second = nullptr;
  stats->failFinals = 1;
  auto failed = env->final();
  ZuCHECK(failed.is<ZeException>() && failed.p<ZeException>().component == "ZpktTest"_z &&
      strstr(errorText(failed.p<ZeException>()).ndata(), "injected child finalization failure") &&
      first->env() == env.ptr() && first->mx() == &mx &&
      stats->finalAttempts == 1 && !stats->finals && !stats->destroys && !stats->pluginFinals,
    "failed child finalization retains children and original error without finalizing Plugin");
  auto retained = env->nif(&mx, "first");
  ZuCHECK(retained.is<Zpkt::Nif *>() && retained.p<Zpkt::Nif *>() == first.ptr() &&
      queries == 2 && stats->creates == 2,
    "failed finalization preserves cache membership and lookup identity");
  ZuCHECK(env->final().is<void>() && stats->finalAttempts == 3 && stats->finals == 2 &&
      stats->destroys == 1 && stats->pluginFinals == 1 &&
      stats->lastChildFinal == 2 && stats->pluginFinalOrder == 3 && !first->env(),
    "retry finalizes every retained child before Plugin and releases tree references");
  first = nullptr;
  ZuCHECK(stats->destroys == 2 && !stats->pluginDestroys,
    "external finalized child retires before Env destruction");
  ZuCHECK(env->final().is<void>() && stats->pluginFinals == 1 && stats->finalAttempts == 3,
    "repeated finalization performs no duplicate child retirement");
  env = nullptr;
  ZuCHECK(stats->pluginDestroys == 1, "last Env release destroys finalized Plugin");
}

static void testMalformedConfig()
{
  ZuTestScopeRT(testMalformedConfig);
  auto stats = pluginStats();
  ZuCheckRT(stats);
  if (!stats) return;
  ZiMultiplex mx;
  for (unsigned scenario = 0; scenario < 3; ++scenario) {
    auto cf = ZpktTest::object();
    ZpktTest::value(cf.ptr(), "module", path("libZpktTestPlugin.so"));
    auto interfaces = ZpktTest::object(cf.ptr(), "interfaces");
    if (scenario == 2) {
      auto prior = ZpktTest::object(interfaces, "prior");
      ZpktTest::value(prior, "txSid", 3);
      ZpktTest::value(prior, "rxSpin", 1000);
    }
    auto invalid = ZpktTest::object(interfaces, "invalid");
    auto field = scenario == 2 ? "txSpin" : scenario ? "rxSpin" : "txSid";
    if (scenario) {
      ZpktTest::value(invalid, "txSid", 3);
      ZpktTest::value(invalid, field, "not-an-integer");
    } else {
      ZpktTest::value(invalid, "rxSpin", 1000);
    }
    ZmRef<Zpkt::Env> env = new Zpkt::Env;
    env->nifQueryFn(query);
    ZeException failure;
    auto destroyed = stats->pluginDestroys;
    try { env->init(cf); }
    catch (const ZeException &error) { failure = error; }
    auto message = errorText(failure);
    ZuCheckRT(failure && strstr(message.ndata(), field) &&
	stats->pluginDestroys == destroyed && env->final().is<void>(),
      log_("nested typed configuration failure preserves its field diagnostic: ", message));
    cf = ZpktTest::config(path("libZpktTestPlugin.so"), "recovered", 3);
    env->init(cf);
    cf = nullptr;
    auto opened = env->nif(&mx, "recovered");
    ZuCheckRT(opened.is<Zpkt::Nif *>() &&
	stats->rxSpin == (Zi_DefltSpin<<2) && stats->txSpin == Zi_DefltSpin,
      log_("valid retry with omitted polling budgets inherits the AF_XDP defaults"));
    if (opened.is<Zpkt::Nif *>()) {
      ZmRef<Zpkt::Nif> nif = opened.p<Zpkt::Nif *>();
      nif->final();
    }
    ZuCheckRT(env->final().is<void>());
    env = nullptr;
    ZuCheckRT(stats->pluginDestroys == destroyed + 1,
      log_("only the successfully initialized Plugin reaches destruction"));
  }
}

static void testNativeModule(ZuCSpan module, bool dpdk)
{
  ZuTestScopeRT(testNativeModule);
  auto cf = ZpktTest::config(module);
  if (dpdk) {
    auto eal = ZpktTest::object(cf.ptr(), "eal");
    for (auto flag : {"noPCI", "noHuge", "noShconf", "noTelemetry"})
      ZpktTest::value(eal, flag, "true");
    ZpktTest::value(eal, "iova", "va");
  }
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  try {
    env->init(cf);
  } catch (const ZeException &error) {
    ZuCheckRT(false, log_("native module startup failed: ", errorText(error)));
    return;
  }
  cf = nullptr;
  auto finalized = env->final();
  ZuCheckRT(finalized.is<void>(), log_("native module initializes and finalizes through common Env",
    finalized.is<ZeException>() ? errorText(finalized.p<ZeException>()) : ZtString<>{}));
  env = nullptr;
  ZuCheckRT(true, log_("last Env reference releases the native Plugin"));
}

static void runTests()
{
  ZuTestScopeRT(runTests);
  if (auto module = getenv("ZPKT_NATIVE_MODULE")) {
    auto dpdk = getenv("ZPKT_NATIVE_DPDK");
    ZuTestCallRT(testNativeModule, ZuCSpan{module}, dpdk && ZuCSpan{dpdk} == "1"_z);
    return;
  }
  ZuTestCallRT(testLoader);
  ZuTestCallRT(testNamedFactory);
  ZuTestCallRT(testCreationFailure);
  ZuTestCallRT(testFinalFailure);
  ZuTestCallRT(testMalformedConfig);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(runTests);
}
