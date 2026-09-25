//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>


#ifndef _WIN32
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZuBase64.hh>

#include <zlib/ZtPlatform.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZtlsAge.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsVault.hh>

using namespace ZuTestUtil;

#ifndef _WIN32
static bool hasMode(const Zi::Path &path, mode_t mode)
{
  struct stat info;
  return !::stat(path, &info) && (info.st_mode & 0777) == mode;
}
#endif

static const ZfJSON::AnyNode *member(const ZfJSON::AnyNode *node,
    ZuCSpan key)
{
  if (!node || !node->has<ZfJSON::AnyNode::Object>()) return nullptr;
  for (const auto &field : node->data<ZfJSON::AnyNode::Object>())
    if (field.p<0>() == key) return field.p<1>();
  return nullptr;
}

static void ephemeral()
{
  ZuTestScope(ephemeral);
  Ztls::VaultConfig cf;
  cf.service = "ztls-vault-test";
  cf.store = Ztls::VaultStore::Ephemeral;
  Ztls::Vault vault;
  ZuCheck(!vault.init(cf).is<ZeException>());
  ZuCheck(!vault.start().is<ZeException>());
  ZuCheck(!vault.start().is<ZeException>());

  Ztls::Scope global{Ztls::Scopes::Global{}};
  Ztls::Scope env{Ztls::Scopes::Environment{"staging"}};
  uint8_t data[] = {0, 0xff, 1};
  ZuCheck(!vault.save(global, "token", data).is<ZeException>());
  ZuCheck(!vault.save(env, "token", {}).is<ZeException>());

  unsigned called = 0;
  auto got = [&called, &data](ZuBSpan value) {
    ++called;
    ZuCheck(value.length() == sizeof(data));
    if (value.length() == sizeof(data))
      ZuCheck(!memcmp(value.data(), data, sizeof(data)));
  };
  ZuCheck(!vault.load(global, "token", got).is<ZeException>());
  ZuCheck(called == 1);
  bool replaced = false;
  ZuCheck(!vault.load(global, "token",
    [&vault, &global, &replaced](ZuBSpan value) {
      replaced = !vault.save(global, "token",
        ZuBSpan{value.data() + 1, value.length() - 1}).is<ZeException>();
    }).is<ZeException>() && replaced);
  bool matched = false;
  ZuCheck(!vault.load(global, "token", [&matched](ZuBSpan value) {
    const uint8_t expected[] = {0xff, 1};
    matched = value == ZuBSpan{expected};
  }).is<ZeException>() && matched);
  auto emptyResult = vault.load(env, "token", [&called](ZuBSpan value) {
    ++called;
    ZuCheck(!value.length());
  });
  ZuCheck(!emptyResult.is<ZeException>());
  ZuCheck(called == 2);

  ZuCheck(vault.save(global, "bad/name", data).is<ZeException>());
  ZuCheck(vault.save(global, "", data).is<ZeException>());
  ZuCheck(vault.save(Ztls::Scopes::Environment{""}, "token", data)
    .is<ZeException>());
  ZuCheck(vault.save(Ztls::Scopes::Environment{"bad/name"}, "token", data)
    .is<ZeException>());
  ZuCheck(vault.load(global, "absent", [&called](ZuBSpan) { ++called; })
    .is<ZeException>());
  ZuCheck(called == 2);

  vault.stop();
  ZuCheck(vault.load(global, "token", got).is<ZeException>());
  ZuCheck(!vault.start().is<ZeException>());
  ZuCheck(vault.load(global, "token", got).is<ZeException>());
  vault.stop();
  vault.final();
}

static void relativeHome()
{
  ZuTestScopeRT(relativeHome);
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  uint8_t id[8];
  ZuCheckRT(rng.random(id));
  char hex[ZuHex::enclen(sizeof(id))];
  ZuHex::encode(hex, id);
  Zi::Path leaf;
  leaf << "ztls-vault-relative-" << ZuCSpan{hex, sizeof(hex)};
  Zi::Path home = ZiFile::append(ZiFile::cwd(), leaf);
  ZuGuard cleanup{[&home]() { ZiFile::removeTree(home); }};
  Zt::setenv("ZTLSVAULTRELATIVETEST_HOME", leaf);

  Ztls::VaultConfig cf;
  cf.service = "ztls-vault-relative-test";
  cf.envPrefix = "ZTLSVAULTRELATIVETEST";
  cf.store = Ztls::VaultStore::File;
  Ztls::Vault vault;
  ZuCheckRT(!vault.init(cf).is<ZeException>());
  ZuCheckRT(!vault.start().is<ZeException>());
  ZuGuard stop{[&vault]() { vault.stop(); }};
  ZuCheckRT(ZiStat{ZiFile::append(home, "vault")}.isdir());
  uint8_t value = 42;
  ZuBSpan one{&value, 1};
  ZuCheckRT(!vault.save(Ztls::Scopes::Global{}, "token", one)
    .is<ZeException>());
  ZuCheckRT(ZiStat{ZiFile::append(ZiFile::append(home, "vault"),
    "secrets.json")}.exists());
  vault.stop();
  vault.final();
}

static void file()
{
  ZuTestScopeRT(file);
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  uint8_t id[8];
  ZuCheckRT(rng.random(id));
  char hex[ZuHex::enclen(sizeof(id))];
  ZuHex::encode(hex, id);
  Zi::Path home = ZiFile::append(ZiFile::tmpDir(),
    Zi::Path{} << "ztls-vault-test-" << ZuCSpan{hex, sizeof(hex)});
  ZuGuard cleanup{[&home]() { ZiFile::removeTree(home); }};
  Zt::setenv("ZTLSVAULTTEST_HOME", home);

  Ztls::VaultConfig cf;
  cf.service = "ztls-vault-test";
  cf.envPrefix = "ZTLSVAULTTEST";
  cf.store = Ztls::VaultStore::File;
  Ztls::Scope global{Ztls::Scopes::Global{}};
  Ztls::Scope scoped{Ztls::Scopes::Environment{"staging"}};
  uint8_t binary[] = {0, 0xff, 1};

  ZuCheckRT(!ZiStat{home}.exists());
  Ztls::Vault first;
  ZuCheckRT(!first.init(cf).is<ZeException>());
  ZuCheckRT(!first.start().is<ZeException>());
  ZuGuard stopFirst{[&first]() { first.stop(); }};
#ifndef _WIN32
  pid_t childPID = ::fork();
  if (!childPID) {
    Ztls::Vault child;
    bool rejected = !child.init(cf).is<ZeException>() &&
      child.start().is<ZeException>();
    ::_exit(rejected ? 0 : 1);
  }
  ZuCheckRT(childPID >= 0);
  int contenderStatus = 0;
  ZuCheckRT(::waitpid(childPID, &contenderStatus, 0) == childPID);
  ZuCheckRT(WIFEXITED(contenderStatus) && !WEXITSTATUS(contenderStatus));
#endif
  Zi::Path dir = ZiFile::append(home, "vault");
  ZuCheckRT(ZiStat{dir}.isdir());
#ifndef _WIN32
  ZuCheckRT(hasMode(dir, 0700));
#endif
  ZuCheckRT(!first.save(global, "token", binary).is<ZeException>());
#ifndef _WIN32
  ZuCheckRT(hasMode(ZiFile::append(dir, "secrets.json"), 0600));
#endif
  ZuCheckRT(!first.save(scoped, "empty", {}).is<ZeException>());

  Ztls::Vault contender;
  ZuCheckRT(!contender.init(cf).is<ZeException>());
  ZuCheckRT(contender.start().is<ZeException>());
  contender.final();
  first.stop();
  first.final();

  Ztls::Vault second;
  ZuCheckRT(!second.init(cf).is<ZeException>());
  ZuCheckRT(!second.start().is<ZeException>());
  ZuGuard stopSecond{[&second]() { second.stop(); }};
  bool loaded = false;
  auto result = second.load(global, "token", [&loaded, &binary](ZuBSpan value) {
    loaded = true;
    ZuCheckRT(value.length() == sizeof(binary));
    if (value.length() == sizeof(binary))
      ZuCheckRT(!memcmp(value.data(), binary, sizeof(binary)));
  });
  ZuCheckRT(!result.is<ZeException>() && loaded);
  result = second.load(scoped, "empty", [](ZuBSpan value) {
    ZuCheckRT(!value.length());
  });
  ZuCheckRT(!result.is<ZeException>());
  uint8_t replacement[] = {9, 0, 8, 0xff};
  ZuCheckRT(!second.save(global, "token", replacement)
    .is<ZeException>());
  ZuCheckRT(!second.save(global, "caf\xc3\xa9%", binary)
    .is<ZeException>());
  ZuCheckRT(!second.save(Ztls::Scopes::Environment{"caf\xc3\xa9%"},
    "token", binary).is<ZeException>());
  ZuCSpan nulName{"nul\0name", 8};
  ZuCheckRT(!second.save(global, nulName, binary)
    .is<ZeException>());
  second.stop();
  second.final();

  Ztls::VaultConfig otherCf = cf;
  otherCf.account = "another account";
  Ztls::Vault other;
  ZuCheckRT(!other.init(otherCf).is<ZeException>());
  ZuCheckRT(!other.start().is<ZeException>());
  ZuGuard stopOther{[&other]() { other.stop(); }};
  ZuCheckRT(!other.save(global, "token", binary).is<ZeException>());
  other.stop();
  other.final();

  Ztls::Vault verify;
  ZuCheckRT(!verify.init(cf).is<ZeException>());
  ZuCheckRT(!verify.start().is<ZeException>());
  ZuGuard stopVerify{[&verify]() { verify.stop(); }};
  result = verify.load(global, "token", [&replacement](ZuBSpan value) {
    ZuCheckRT(value.length() == sizeof(replacement));
    if (value.length() == sizeof(replacement))
      ZuCheckRT(!memcmp(value.data(), replacement, sizeof(replacement)));
  });
  ZuCheckRT(!result.is<ZeException>());
  result = verify.load(global, nulName, [](ZuBSpan value) {
    ZuCheckRT(value.length() == sizeof(binary));
  });
  ZuCheckRT(!result.is<ZeException>());
  result = verify.load(global, "caf\xc3\xa9%", [](ZuBSpan value) {
    ZuCheckRT(value.length() == sizeof(binary));
  });
  ZuCheckRT(!result.is<ZeException>());
  Zi::Path aggregate = ZiFile::append(dir, "secrets.json");
  ZiFile keyFile{aggregate, ZiFile::ReadOnly | ZiFile::NoFollow |
    ZiFile::GC};
  ZuCheckRT(keyFile);
  auto keySize = keyFile.size();
  ZuCheckRT(keySize > 0);
  ZtCArray<ZtArrayHeapID<"Ztls.Vault.TestKeys">> keyText(keySize,
    keySize);
  ZuCheckRT(keyFile.read(keyText.data(), keySize) == keySize);
  auto keys = ZfJSON::scan(keyText);
  ZuCheckRT(keys.p<0>() >= 0 && keys.p<1>());
  auto accounts = member((*keys.p<1>())[0], "accounts");
  ZuCheckRT(accounts && accounts->has<ZfJSON::AnyNode::Object>());
  auto otherAccount = member(accounts, "another account");
  ZuCheckRT(otherAccount && member(otherAccount, "global/token"));
  const ZfJSON::AnyNode *defaultAccount = nullptr;
  ZuCSpan defaultName;
  for (const auto &account : accounts->data<ZfJSON::AnyNode::Object>()) {
    if (account.p<0>() == "another account") continue;
    ZuCSpan name = account.p<0>();
    defaultName = name;
    defaultAccount = account.p<1>();
  }
  ZuCheckRT(defaultAccount);
  Ztls::MD<Ztls::SHA256> hash;
  hash.update(home);
  uint8_t digest[Ztls::MD<Ztls::SHA256>::Size];
  hash.finish(digest);
  char expectedAccount[22];
  ::memcpy(expectedAccount, "vault|", 6);
  ZuHex::encode(ZuSpan<uint8_t>{expectedAccount + 6,
    sizeof(expectedAccount) - 6}, ZuBSpan{digest, 8});
  ZuClear(digest, sizeof(digest));
  for (unsigned i = 6; i < sizeof(expectedAccount); ++i)
    if (expectedAccount[i] >= 'A' && expectedAccount[i] <= 'F')
      expectedAccount[i] += 'a' - 'A';
  ZuCSpan expected{expectedAccount + 0, sizeof(expectedAccount)};
  ZuCheckRT(defaultName == expected);
  ZuCheckRT(member(defaultAccount, "global/token"));
  ZuCheckRT(member(defaultAccount, "global/caf\xc3\xa9%"));
  ZuCSpan nulKey{"global/nul\0name", 15};
  ZuCheckRT(member(defaultAccount, nulKey));
  ZuCheckRT(member(defaultAccount, "env/staging/empty"));
  ZuCheckRT(member(defaultAccount, "env/caf\xc3\xa9%/token"));
  Zi::Path orphan = ZiFile::append(dir, "secrets.json.tmp.interrupted");
  ZiFile abandoned;
  ZuCheckRT(abandoned.open(orphan, ZiFile::Write | ZiFile::Exclusive |
    ZiFile::NoFollow | ZiFile::GC, 0600) == Zi::OK);
  ZuCheckRT(abandoned.write("partial", 7) == Zi::OK);
  abandoned.close();
  ZuCheckRT(!verify.save(global, "after-orphan", binary)
    .is<ZeException>());
  ZuCheckRT(ZiStat{orphan}.exists());
  verify.stop();
  verify.final();

  ZiFile corrupt;
  ZuCheckRT(corrupt.open(aggregate, ZiFile::Write | ZiFile::NoFollow |
    ZiFile::GC, 0600) == Zi::OK);
  constexpr char malformed[] = "{not json";
  ZuCheckRT(corrupt.write(malformed, sizeof(malformed) - 1) == Zi::OK);
  corrupt.close();
  Ztls::Vault reject;
  ZuCheckRT(!reject.init(cf).is<ZeException>());
  ZuCheckRT(!reject.start().is<ZeException>());
  ZuGuard stopReject{[&reject]() { reject.stop(); }};
  ZuCheckRT(reject.save(global, "token", binary)
    .is<ZeException>());
  bool invoked = false;
  result = reject.load(global, "token", [&invoked](ZuBSpan) { invoked = true; });
  ZuCheckRT(result.is<ZeException>() && !invoked);
  reject.stop();
  reject.final();
  ZiFile unchanged;
  ZuCheckRT(unchanged.open(aggregate, ZiFile::ReadOnly | ZiFile::NoFollow |
    ZiFile::GC) == Zi::OK);
  char check[sizeof(malformed) - 1];
  ZuCheckRT(unchanged.read(check, sizeof(check)) == int(sizeof(check)));
  ZuCheckRT(!memcmp(check, malformed, sizeof(check)));
  unchanged.close();

  constexpr char wrongType[] =
    "{\"version\":1,\"accounts\":{\"a\":{\"k\":17}}}";
  ZiFile invalid;
  ZuCheckRT(invalid.open(aggregate, ZiFile::Write | ZiFile::NoFollow |
    ZiFile::GC, 0600) == Zi::OK);
  ZuCheckRT(invalid.write(wrongType, sizeof(wrongType) - 1) == Zi::OK);
  invalid.close();
  Ztls::Vault rejectType;
  ZuCheckRT(!rejectType.init(cf).is<ZeException>());
  ZuCheckRT(!rejectType.start().is<ZeException>());
  ZuGuard stopRejectType{[&rejectType]() { rejectType.stop(); }};
  ZuCheckRT(rejectType.save(global, "token", binary)
    .is<ZeException>());
  rejectType.stop();
  rejectType.final();
}

static void secrets()
{
  ZuTestScopeRT(secrets);
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  uint8_t id[8];
  ZuCheckRT(rng.random(id));
  char hex[ZuHex::enclen(sizeof(id))];
  ZuHex::encode(hex, id);
  Zi::Path home = ZiFile::append(ZiFile::tmpDir(),
    Zi::Path{} << "ztls-vault-secrets-" << ZuCSpan{hex, sizeof(hex)});
  ZuGuard cleanup{[&home]() { ZiFile::removeTree(home); }};
  Zt::setenv("ZTLSVAULTSECRETSTEST_HOME", home);

  Ztls::VaultConfig cf;
  cf.service = "ztls-vault-secrets-test";
  cf.envPrefix = "ZTLSVAULTSECRETSTEST";
  cf.store = Ztls::VaultStore::Ephemeral;
  cf.variant = Ztls::VaultVariant::Secrets;
  Ztls::Scope global{Ztls::Scopes::Global{}};
  uint8_t first[] = {0, 0xff, 42};
  uint8_t second[] = {9, 8};

  Ztls::Vault vault;
  ZuCheckRT(!vault.init(cf).is<ZeException>());
  ZuCheckRT(!vault.start().is<ZeException>());
  ZuGuard stop{[&vault]() { vault.stop(); }};
  bool called = false;
  ZuCheckRT(vault.load(global, "missing", [&called](ZuBSpan) { called = true; })
    .is<ZeException>() && !called);
  ZuCheckRT(!vault.save(global, "first", first).is<ZeException>());
  ZuCheckRT(!vault.save(global, "second", second).is<ZeException>());
  ZuCheckRT(!vault.save(global, "first", {}).is<ZeException>());
  auto result = vault.load(global, "first", [&called](ZuBSpan value) {
    called = true;
    ZuCheckRT(!value.length());
  });
  ZuCheckRT(!result.is<ZeException>() && called);
  result = vault.load(global, "second", [&second](ZuBSpan value) {
    ZuCheckRT(value.length() == sizeof(second));
    if (value.length() == sizeof(second))
      ZuCheckRT(!memcmp(value.data(), second, sizeof(second)));
  });
  ZuCheckRT(!result.is<ZeException>());
  Zi::Path age = ZiFile::append(ZiFile::append(home, "vault"), "secrets.age");
  ZuCheckRT(ZiStat{age}.exists());
  vault.stop();
  ZuCheckRT(!vault.start().is<ZeException>());
  called = false;
  ZuCheckRT(vault.load(global, "second", [&called](ZuBSpan) { called = true; })
    .is<ZeException>() && !called);
  vault.stop();
  vault.final();
}

static void secretsFile()
{
  ZuTestScopeRT(secretsFile);
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  uint8_t id[8];
  ZuCheckRT(rng.random(id));
  char hex[ZuHex::enclen(sizeof(id))];
  ZuHex::encode(hex, id);
  Zi::Path home = ZiFile::append(ZiFile::tmpDir(),
    Zi::Path{} << "ztls-vault-secrets-file-" << ZuCSpan{hex, sizeof(hex)});
  ZuGuard cleanup{[&home]() { ZiFile::removeTree(home); }};
  Zt::setenv("ZTLSVAULTSECRETSFILETEST_HOME", home);

  Ztls::VaultConfig cf;
  cf.service = "ztls-vault-secrets-file-test";
  cf.envPrefix = "ZTLSVAULTSECRETSFILETEST";
  cf.store = Ztls::VaultStore::File;
  cf.variant = Ztls::VaultVariant::Secrets;
  Ztls::Scope global{Ztls::Scopes::Global{}};
  uint8_t binary[] = {0, 0xff, 42};

  Ztls::Vault first;
  ZuCheckRT(!first.init(cf).is<ZeException>());
  ZuCheckRT(!first.start().is<ZeException>());
  ZuGuard stopFirst{[&first]() { first.stop(); }};
#ifndef _WIN32
  pid_t contender = ::fork();
  if (!contender) {
    Ztls::Vault child;
    bool denied = !child.init(cf).is<ZeException>() &&
      child.start().is<ZeException>();
    ::_exit(denied ? 0 : 1);
  }
  ZuCheckRT(contender >= 0);
  int contenderStatus = 0;
  ZuCheckRT(::waitpid(contender, &contenderStatus, 0) == contender);
  ZuCheckRT(WIFEXITED(contenderStatus) && !WEXITSTATUS(contenderStatus));
#endif
  Zi::Path dir = ZiFile::append(home, "vault");
  Zi::Path age = ZiFile::append(dir, "secrets.age");
  Zi::Path json = ZiFile::append(dir, "secrets.json");
  bool called = false;
  ZuCheckRT(first.load(global, "binary", [&called](ZuBSpan) { called = true; })
    .is<ZeException>() && !called);
  ZuCheckRT(!ZiStat{age}.exists() && !ZiStat{json}.exists());
  ZuCheckRT(!first.save(global, "binary", binary)
    .is<ZeException>());
#ifndef _WIN32
  ZuCheckRT(hasMode(dir, 0700));
  ZuCheckRT(hasMode(age, 0600));
  ZuCheckRT(hasMode(json, 0600));
#endif
  ZiFile initialJSON{json, ZiFile::ReadOnly | ZiFile::NoFollow |
    ZiFile::GC};
  ZuCheckRT(initialJSON);
  auto initialSize = initialJSON.size();
  ZuCheckRT(initialSize > 0);
  ZtCArray<ZtArrayHeapID<"Ztls.Vault.TestInitialJSON">> initialText(
    initialSize, initialSize);
  ZuGuard clearInitialText{[&initialText]() {
    ZuClear(initialText.data(), initialText.size());
  }};
  ZuCheckRT(initialJSON.read(initialText.data(), initialSize) == initialSize);
  initialJSON.close();
  ZuCheckRT(!first.save(global, "empty", {})
    .is<ZeException>());
  first.stop();
  first.final();

#ifndef _WIN32
  pid_t abandoned = ::fork();
  if (!abandoned) {
    Ztls::Vault child;
    bool started = !child.init(cf).is<ZeException>() &&
      !child.start().is<ZeException>();
    // Simulate process death without Vault::stop(), leaving a stale PID file.
    ::_exit(started ? 0 : 1);
  }
  ZuCheckRT(abandoned >= 0);
  int abandonedStatus = 0;
  ZuCheckRT(::waitpid(abandoned, &abandonedStatus, 0) == abandoned);
  ZuCheckRT(WIFEXITED(abandonedStatus) && !WEXITSTATUS(abandonedStatus));
#endif

  ZuCheckRT(ZiStat{age}.exists() && ZiStat{json}.exists());

  // Open the vault ciphertext independently with the stored passphrase.
  ZiFile jsonFile{json, ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC};
  ZuCheckRT(jsonFile);
  auto jsonSize = jsonFile.size();
  ZuCheckRT(jsonSize > 0);
  ZtCArray<ZtArrayHeapID<"Ztls.Vault.TestJSON">> text(jsonSize, jsonSize);
  ZuGuard clearText{[&text]() { ZuClear(text.data(), text.size()); }};
  ZuCheckRT(jsonFile.read(text.data(), jsonSize) == jsonSize);
  ZuCheckRT(jsonSize == initialSize &&
    !memcmp(text.data(), initialText.data(), initialSize));
  auto parsed = ZfJSON::scan(text);
  ZuCheckRT(parsed.p<0>() >= 0 && parsed.p<1>());
  auto &root = (*parsed.p<1>())[0];
  auto accounts = member(root, "accounts");
  ZuCheckRT(accounts && accounts->has<ZfJSON::AnyNode::Object>());
  ZuCheckRT(accounts->data<ZfJSON::AnyNode::Object>().length() == 1);
  auto &values = accounts->data<ZfJSON::AnyNode::Object>()[0].p<1>();
  auto encoded = member(values, "vault/passphrase");
  ZuCheckRT(encoded && encoded->has<ZfJSON::AnyNode::String>());
  uint8_t passphrase[ZuBase64::enclen(32)];
  ZuGuard clearPassphrase{[&passphrase]() {
    ZuClear(passphrase, sizeof(passphrase));
  }};
  ZuCheckRT(ZuBase64::decode(passphrase,
    encoded->data<ZfJSON::AnyNode::String>()) == sizeof(passphrase));
  ZuCheckRT(passphrase[sizeof(passphrase) - 1] == '=');
  ZiFile ageFile{age, ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC};
  ZuCheckRT(ageFile);
  auto ageSize = ageFile.size();
  ZuCheckRT(ageSize > 0);
  ZtBArray<ZtArrayHeapID<"Ztls.Vault.TestPlain">> plain(ageSize, ageSize);
  ZuGuard clearPlain{[&plain]() { ZuClear(plain.data(), plain.size()); }};
  ZtlsAge codec;
  ZtlsAge::Identity identity{ZtlsAge::ScryptIdentity{passphrase}};
  auto decrypted = codec.decrypt(ageFile, {&identity, 1}, plain);
  ZuCheckRT(!decrypted.is<ZeException>());
  ZuCheckRT(decrypted.p<size_t>() > 5);
  ZuCheckRT(!memcmp(plain.data(), "ZVLT\1", 5));
  ageFile.close();

  // Change the stored passphrase without touching the ciphertext. A failed
  // save must leave that ciphertext intact so the original can still open it.
  uint8_t wrong[sizeof(passphrase)];
  ::memcpy(wrong, passphrase, sizeof(wrong));
  wrong[0] = wrong[0] == 'A' ? 'B' : 'A';
  ZuGuard clearWrong{[&wrong]() { ZuClear(wrong, sizeof(wrong)); }};
  char originalText[ZuBase64::enclen(sizeof(passphrase))];
  char wrongText[sizeof(originalText)];
  ZuGuard clearEncoded{[&originalText, &wrongText]() {
    ZuClear(originalText, sizeof(originalText));
    ZuClear(wrongText, sizeof(wrongText));
  }};
  ZuBase64::encode(originalText, passphrase);
  ZuBase64::encode(wrongText, wrong);
  int64_t passphraseAt = ZuCSpan{initialText}.find(
    ZuCSpan{originalText + 0, sizeof(originalText)});
  ZuCheckRT(passphraseAt >= 0);
  ::memcpy(initialText.data() + passphraseAt, wrongText, sizeof(wrongText));
  ZiFile changed;
  ZuCheckRT(changed.open(json, ZiFile::Write | ZiFile::NoFollow |
    ZiFile::GC, 0600) == Zi::OK);
  ZuCheckRT(changed.write(initialText.data(), initialText.size()) == Zi::OK);
  changed.close();

  Ztls::Vault wrongKey;
  ZuCheckRT(!wrongKey.init(cf).is<ZeException>());
  ZuCheckRT(!wrongKey.start().is<ZeException>());
  called = false;
  ZuCheckRT(wrongKey.load(global, "binary", [&called](ZuBSpan) { called = true; })
    .is<ZeException>() && !called);
  ZuCheckRT(wrongKey.save(global, "binary", binary).is<ZeException>());
  wrongKey.stop();
  wrongKey.final();
  ZiFile unchanged{age, ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC};
  ZuCheckRT(unchanged && unchanged.size() == ageSize);
  unchanged.close();

  ::memcpy(initialText.data() + passphraseAt, originalText,
    sizeof(originalText));
  ZiFile restored;
  ZuCheckRT(restored.open(json, ZiFile::Write | ZiFile::NoFollow |
    ZiFile::GC, 0600) == Zi::OK);
  ZuCheckRT(restored.write(initialText.data(), initialText.size()) == Zi::OK);
  restored.close();

  Ztls::Vault second;
  ZuCheckRT(!second.init(cf).is<ZeException>());
  ZuCheckRT(!second.start().is<ZeException>());
  ZuGuard stopSecond{[&second]() { second.stop(); }};
  called = false;
  auto result = second.load(global, "binary", [&called, &binary](ZuBSpan value) {
    called = true;
    ZuCheckRT(value.length() == sizeof(binary));
    if (value.length() == sizeof(binary))
      ZuCheckRT(!memcmp(value.data(), binary, sizeof(binary)));
  });
  ZuCheckRT(!result.is<ZeException>() && called);
  called = false;
  result = second.load(global, "empty", [&called](ZuBSpan value) {
    called = true;
    ZuCheckRT(!value.length());
  });
  ZuCheckRT(!result.is<ZeException>() && called);
  Zi::Path orphan = ZiFile::append(dir, "secrets.age.tmp.interrupted");
  ZiFile orphanFile;
  ZuCheckRT(orphanFile.open(orphan, ZiFile::Write | ZiFile::Exclusive |
    ZiFile::NoFollow | ZiFile::GC, 0600) == Zi::OK);
  ZuCheckRT(orphanFile.write("partial", 7) == Zi::OK);
  orphanFile.close();
  ZuCheckRT(!second.save(global, "after-orphan", binary)
    .is<ZeException>());
  ZuCheckRT(ZiStat{orphan}.exists());
  called = false;
  ZuCheckRT(!second.load(global, "binary", [&called, &binary](ZuBSpan value) {
    called = value == ZuBSpan{binary};
  }).is<ZeException>() && called);

  // A valid age envelope with malformed authenticated plaintext must not be
  // mistaken for an empty aggregate or overwritten by a later save.
  ZiFile malformedAge;
  ZuCheckRT(malformedAge.open(age, ZiFile::Write | ZiFile::NoFollow |
    ZiFile::GC, 0600) == Zi::OK);
  uint8_t malformedPlain[] = {'Z', 'V', 'L', 'T', 1, 0};
  ZtlsAge::Recipient recipient{ZtlsAge::ScryptRecipient{passphrase}};
  auto encrypted = codec.encrypt(rng, {&recipient, 1}, malformedPlain,
    malformedAge);
  ZuCheckRT(!encrypted.is<ZeException>());
  malformedAge.close();
  called = false;
  ZuCheckRT(second.load(global, "binary", [&called](ZuBSpan) { called = true; })
    .is<ZeException>() && !called);
  ZuCheckRT(second.save(global, "binary", binary).is<ZeException>());
  ZiFile checkAge{age, ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC};
  ZuCheckRT(checkAge);
  auto checkSize = checkAge.size();
  ZuCheckRT(checkSize > 0);
  ZtBArray<ZtArrayHeapID<"Ztls.Vault.TestMalformed">> checkPlain(
    checkSize, checkSize);
  ZuGuard clearCheck{[&checkPlain]() {
    ZuClear(checkPlain.data(), checkPlain.size());
  }};
  auto malformedDecrypted = codec.decrypt(checkAge, {&identity, 1},
    checkPlain);
  ZuCheckRT(!malformedDecrypted.is<ZeException>() &&
    malformedDecrypted.p<size_t>() == sizeof(malformedPlain) &&
    !::memcmp(checkPlain.data(), malformedPlain, sizeof(malformedPlain)));
  checkAge.close();
  ZiFile corrupt;
  ZuCheckRT(corrupt.open(age, ZiFile::Write | ZiFile::NoFollow |
    ZiFile::GC, 0600) == Zi::OK);
  uint8_t bad = 0;
  ZuCheckRT(corrupt.write(&bad, 1) == Zi::OK);
  corrupt.close();
  called = false;
  ZuCheckRT(second.load(global, "binary", [&called](ZuBSpan) { called = true; })
    .is<ZeException>() && !called);
  ZuCheckRT(second.save(global, "binary", binary)
    .is<ZeException>());
  second.stop();
  second.final();
}

static void module()
{
  ZuTestScopeRT(module);
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  uint8_t id[8];
  ZuCheckRT(rng.random(id));
  char hex[ZuHex::enclen(sizeof(id))];
  ZuHex::encode(hex, id);
  Zi::Path home = ZiFile::append(ZiFile::tmpDir(),
    Zi::Path{} << "ztls-vault-module-" << ZuCSpan{hex, sizeof(hex)});
  ZuGuard cleanup{[&home]() { ZiFile::removeTree(home); }};
  Zt::setenv("ZTLSVAULTMODULETEST_HOME", home);

  Ztls::VaultConfig cf;
  cf.service = "ztls-vault-module-test";
  cf.envPrefix = "ZTLSVAULTMODULETEST";
  cf.store = Ztls::VaultStore::Module;
  cf.variant = Ztls::VaultVariant::Direct;
  Ztls::Scope global{Ztls::Scopes::Global{}};
  uint8_t value[] = {0, 42, 0xff};

  Ztls::Vault invalid;
  ZuCheckRT(!invalid.init(cf).is<ZeException>());
  ZuCheckRT(invalid.start().is<ZeException>());
  invalid.final();

  cf.module = ZTLS_VAULT_FIXTURE_PATH;
  Ztls::Vault direct;
  ZuCheckRT(!direct.init(cf).is<ZeException>());
  ZuCheckRT(!direct.start().is<ZeException>());
  ZuGuard stopDirect{[&direct]() { direct.stop(); }};
  ZuCheckRT(!direct.save(global, "item", value).is<ZeException>());
  bool called = false;
  auto result = direct.load(global, "item", [&called, &value](ZuBSpan loaded) {
    called = true;
    ZuCheckRT(loaded.length() == sizeof(value));
    if (loaded.length() == sizeof(value))
      ZuCheckRT(!memcmp(loaded.data(), value, sizeof(value)));
  });
  ZuCheckRT(!result.is<ZeException>() && called);
  direct.stop();
  direct.final();
  ZuCheckRT(!ZiStat{home}.exists());

  cf.variant = Ztls::VaultVariant::Secrets;
  Ztls::Vault secrets;
  ZuCheckRT(!secrets.init(cf).is<ZeException>());
  ZuCheckRT(!secrets.start().is<ZeException>());
  ZuGuard stopSecrets{[&secrets]() { secrets.stop(); }};
  ZuCheckRT(!secrets.save(global, "item", value)
    .is<ZeException>());
  called = false;
  result = secrets.load(global, "item", [&called](ZuBSpan loaded) {
    called = true;
    ZuCheckRT(loaded.length() == sizeof(value));
  });
  ZuCheckRT(!result.is<ZeException>() && called);
  Zi::Path age = ZiFile::append(ZiFile::append(home, "vault"), "secrets.age");
  ZuCheckRT(ZiStat{age}.exists());
  secrets.stop();
  secrets.final();
}

#ifdef __linux__
static void autoFallback()
{
  ZuTestScopeRT(autoFallback);
  const char *previous = ::getenv("DBUS_SESSION_BUS_ADDRESS");
  ZtString<ZtStringHeapID<"Ztls.Vault.TestBus">> saved{
    previous ? previous : ""};
  bool hadBus = previous != nullptr;
  Zt::setenv("DBUS_SESSION_BUS_ADDRESS", "unsupported:address");
  ZuGuard restoreBus{[&saved, hadBus]() {
    if (hadBus)
      Zt::setenv("DBUS_SESSION_BUS_ADDRESS", saved);
    else
      Zt::unsetenv("DBUS_SESSION_BUS_ADDRESS");
  }};

  Ztls::Random rng;
  ZuCheckRT(rng.init());
  uint8_t id[8];
  ZuCheckRT(rng.random(id));
  char hex[ZuHex::enclen(sizeof(id))];
  ZuHex::encode(hex, id);
  Zi::Path home = ZiFile::append(ZiFile::tmpDir(),
    Zi::Path{} << "ztls-vault-auto-" << ZuCSpan{hex, sizeof(hex)});
  ZuGuard cleanup{[&home]() { ZiFile::removeTree(home); }};
  Zt::setenv("ZTLSVAULTAUTOTEST_HOME", home);

  Ztls::VaultConfig cf;
  cf.service = "ztls-vault-auto-test";
  cf.envPrefix = "ZTLSVAULTAUTOTEST";
  cf.store = Ztls::VaultStore::Auto;
  cf.variant = Ztls::VaultVariant::Direct;
  Ztls::Scope scope{Ztls::Scopes::Global{}};
  uint8_t value[] = {0, 0xff, 42};
  Ztls::Vault direct;
  ZuCheckRT(!direct.init(cf).is<ZeException>());
  ZuCheckRT(!direct.start().is<ZeException>());
  ZuGuard stopDirect{[&direct]() { direct.stop(); }};
  ZuCheckRT(!direct.save(scope, "token", value)
    .is<ZeException>());
  bool loaded = false;
  ZuCheckRT(!direct.load(scope, "token", [&loaded, &value](ZuBSpan data) {
    loaded = data == ZuBSpan{value};
  }).is<ZeException>() && loaded);
  direct.stop();
  direct.final();

  cf.variant = Ztls::VaultVariant::Secrets;
  Ztls::Vault secrets;
  ZuCheckRT(!secrets.init(cf).is<ZeException>());
  ZuCheckRT(!secrets.start().is<ZeException>());
  ZuGuard stopSecrets{[&secrets]() { secrets.stop(); }};
  ZuCheckRT(!secrets.save(scope, "encrypted", value)
    .is<ZeException>());
  Zi::Path age = ZiFile::append(ZiFile::append(home, "vault"),
    "secrets.age");
  ZuCheckRT(ZiStat{age}.exists());
  secrets.stop();
  secrets.final();
}
#endif

int main()
{
  ZuTestMain();
  ZuTestCall_("ephemeral", ephemeral);
  ZuTestCall_("relativeHome", relativeHome);
  ZuTestCall_("file", file);
  ZuTestCall_("secrets", secrets);
  ZuTestCall_("secretsFile", secretsFile);
  ZuTestCall_("module", module);
#ifdef __linux__
  ZuTestCall_("autoFallback", autoFallback);
#endif
}
