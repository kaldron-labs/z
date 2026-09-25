//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include <zlib/ZuHex.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuByteSwap.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtPlatform.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiModule.hh>
#include <zlib/ZiPIDFile.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsAge.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsVaultStore.hh>

#if defined(_WIN32) || defined(__linux__)
namespace Ztls_ { VaultStore *vaultKeyRing(); }
#endif

namespace Ztls {

ZtEnumImplNS(VaultStore);
ZtEnumImplNS(VaultVariant);

namespace Vault_ {

using Bytes = ZtBArray<ZtArrayHeapID<"Ztls.Vault.Value">>;

struct Entry_ {
  VaultString key;
  Bytes value;

  Entry_(ZuCSpan key_, ZuBSpan value_) : key{key_}, value{value_} { }
  ~Entry_() { ZuClear(value.data(), value.length()); }

  static ZuCSpan KeyAxor(const Entry_ &entry) { return entry.key; }
};

ZmHashDerive(Entries, Entry_,
  (ZmHashNode<Entry_,
    ZmHashKey<Entry_::KeyAxor,
      ZmHashHeapID<"Ztls.Vault.Entry">>>));

template <typename Heap = ZuVoid>
class Ephemeral_ : public Heap, public Ztls_::VaultStore {
public:
  VaultResult init(const VaultConfig &) override { return {}; }
  void final() override { m_entries.clean(); }

  VaultResult load(ZuCSpan key, VaultLoadFn fn) override {
    auto entry = m_entries.find(key);
    if (!entry)
      return ZeEXCEPT(Error, "ZtlsVault", "credential missing");
    fn(entry->value);
    return {};
  }

  VaultResult save(ZuCSpan key, ZuBSpan value) override {
    if (auto entry = m_entries.find(key)) {
      ZuClear(entry->value.data(), entry->value.length());
      entry->value = Bytes{value};
    } else
      m_entries.addNode(new Entries::Node{key, value});
    return {};
  }

private:
  Entries m_entries;
};

using EphemeralHeap = ZmHeap<"Ztls.Vault.Ephemeral", Ephemeral_<>>;
ZuDerive(Ephemeral, Ephemeral_<EphemeralHeap>);

template <typename Heap = ZuVoid>
class Values_ : public Heap,
    public ZmHashKV<ZuCSpan, ZuBSpan,
      ZmHashHeapID<"Ztls.Vault.FileEntry">> {
};
using ValuesHeap = ZmHeap<"Ztls.Vault.FileValues", Values_<>>;
ZuDerive(Values, Values_<ValuesHeap>);
inline ZfJSON::AsMap<ZfFieldTC::Bytes> ZfJSON_Fmt(Values *);

template <typename Heap = ZuVoid>
class Accounts_ : public Heap,
    public ZmHashKV<ZuCSpan, Values *,
      ZmHashHeapID<"Ztls.Vault.FileAccount">> {
public:
  ~Accounts_() {
    auto i = this->citer();
    while (auto node = i()) delete node->val();
  }
};
using AccountsHeap = ZmHeap<"Ztls.Vault.FileAccounts", Accounts_<>>;
ZuDerive(Accounts, Accounts_<AccountsHeap>);
inline ZfJSON::AsMap<ZfFieldTC::UDT> ZfJSON_Fmt(Accounts *);

struct FileData {
  uint32_t version = 0;
  Accounts *accounts = nullptr;

  FileData() = default;
  FileData(const FileData &) = delete;
  FileData &operator =(const FileData &) = delete;
  ~FileData() { delete accounts; }
};
ZfStruct(ZtlsAPI, (FileData, JSON),
  (((version), (Mutable)), (UInt32)),
  (((accounts), (Mutable)), (UDT)));

using FileText = ZtCArray<ZtArrayHeapID<"Ztls.Vault.FileText">>;

template <typename L>
VaultResult publish(const Zi::Path &dir, const Zi::Path &path,
  ZuCSpan prefix, L &&write)
{
  // The random suffix avoids conflicts with an interrupted older write.
  uint8_t id[16];
  Random rng;
  if (!rng.init() || !rng.random(id))
    return ZeEXCEPT(Error, "ZtlsVault", "temporary name failed");
  char hex[ZuHex::enclen(sizeof(id))];
  ZuHex::encode(hex, id);
  ZuClear(id, sizeof(id));
  Zi::Path leaf;
  leaf << prefix << ZuCSpan{hex, sizeof(hex)};
  Zi::Path temp = ZiFile::append(dir, leaf);
  ZiFile file;
  if (file.open(temp, ZiFile::WriteOnly | ZiFile::Create |
      ZiFile::Exclusive | ZiFile::NoFollow | ZiFile::GC, 0600) != Zi::OK)
    return ZeEXCEPT(Error, "ZtlsVault", "temporary open failed");
  bool published = false;
  ZuGuard cleanup{[&]() {
    file.close();
    if (!published) ZiFile::remove(temp);
  }};
  auto result = ZuFwd<L>(write)(file);
  if (result.template is<ZeException>()) return result;
  if (file.sync() != Zi::OK)
    return ZeEXCEPT(Error, "ZtlsVault", "aggregate sync failed");
  file.close();
  if (ZiFile::rename(temp, path) != Zi::OK)
    return ZeEXCEPT(Error, "ZtlsVault", "aggregate publish failed");
  published = true;
  return {};
}

template <typename Heap = ZuVoid>
class File_ : public Heap, public Ztls_::VaultStore {
public:
  File_(const Zi::Path &dir) : m_dir{dir} { }

  VaultResult init(const VaultConfig &cf) override {
    m_account = cf.account;
    m_path = ZiFile::append(m_dir, "secrets.json");
    return {};
  }
  void final() override { m_account.clear(); }

  VaultResult load(ZuCSpan key, VaultLoadFn fn) override {
    FileText text;
    ZuGuard clear{[&text]() { ZuClear(text.data(), text.length()); }};
    FileData data;
    bool exists = false;
    auto result = read_(data, text, exists);
    if (result.template is<ZeException>()) return result;
    if (!exists)
      return ZeEXCEPT(Error, "ZtlsVault", "credential missing");
    auto account = data.accounts->find(m_account);
    if (!account)
      return ZeEXCEPT(Error, "ZtlsVault", "credential missing");
    auto entry = account->val()->find(key);
    if (!entry)
      return ZeEXCEPT(Error, "ZtlsVault", "credential missing");
    fn(entry->val());
    return {};
  }

  VaultResult save(ZuCSpan key, ZuBSpan value) override {
    FileText text;
    ZuGuard clear{[&text]() { ZuClear(text.data(), text.length()); }};
    FileData data;
    bool exists = false;
    auto result = read_(data, text, exists);
    if (result.template is<ZeException>()) return result;
    if (!exists) {
      data.version = 1;
      data.accounts = new Accounts;
    }
    auto account = data.accounts->find(m_account);
    Values *values;
    if (account)
      values = account->val();
    else {
      values = new Values;
      data.accounts->add(m_account, values);
    }
    if (auto entry = values->find(key)) {
      entry->val() = value;
    } else
      values->add(key, value);
    return write_(data);
  }

private:
  VaultResult read_(FileData &data, FileText &text, bool &exists) const {
    ZiFile file;
    if (file.open(m_path, ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC)
	!= Zi::OK) {
      if (file.error().errNo() == ZiENOENT) return {};
#ifdef _WIN32
      if (file.error().errNo() == ERROR_PATH_NOT_FOUND) return {};
#endif
      return ZeEXCEPT(Error, "ZtlsVault", "aggregate open failed");
    }
    exists = true;
    auto size = file.size();
    if (size <= 0 || size > INT_MAX)
      return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
    text = FileText(size, size);
    if (file.read(text.data(), unsigned(size)) != size)
      return ZeEXCEPT(Error, "ZtlsVault", "aggregate read failed");
    auto parsed = ZfJSON::scan(text);
    if (parsed.p<0>() < 0 || !parsed.p<1>() ||
	parsed.p<1>()->data<ZfJSON::AnyNode::Array>().length() != 1)
      return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
    ZuCSpan remaining{text.data() + parsed.p<0>(),
	unsigned(size - parsed.p<0>())};
    if (remaining.find([](char c) {
	return c != ' ' && c != '\t' && c != '\n' && c != '\r';
      }) >= 0)
      return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
    auto &root = (*parsed.p<1>())[0];
    if (!root->has<ZfJSON::AnyNode::Object>())
      return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
    for (const auto &field : root->data<ZfJSON::AnyNode::Object>()) {
      if (field.template p<0>() != "accounts") continue;
      const auto &accounts = field.template p<1>();
      if (!accounts->has<ZfJSON::AnyNode::Object>())
	return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
      for (const auto &account : accounts->data<ZfJSON::AnyNode::Object>()) {
	const auto &values = account.template p<1>();
	if (!values->has<ZfJSON::AnyNode::Object>())
	  return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
	for (const auto &value : values->data<ZfJSON::AnyNode::Object>())
	  if (!value.template p<1>()->has<ZfJSON::AnyNode::String>())
	    return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
      }
    }
    auto handler = ZfJSON::handler<FileData>(root);
    if (!handler.valid)
      return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
    handler.load(data);
    if (data.version != 1 || !data.accounts)
      return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
    auto i = data.accounts->citer();
    while (auto node = i())
      if (!node->val())
	return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
    return {};
  }

  VaultResult write_(const FileData &data) const {
    FileText text;
    ZfJSON::save(text, data);
    ZuGuard clear{[&text]() { ZuClear(text.data(), text.length()); }};
    return publish(m_dir, m_path, "secrets.json.tmp.", [&](ZiFile &file) {
      if (file.write(text.data(), text.length()) != Zi::OK)
	return VaultResult{ZeEXCEPT(Error, "ZtlsVault", "aggregate write failed")};
      return VaultResult{};
    });
  }

  Zi::Path m_dir;
  Zi::Path m_path;
  VaultString m_account;
};
using FileHeap = ZmHeap<"Ztls.Vault.File", File_<>>;
ZuDerive(File, File_<FileHeap>);

VaultResult ensureDir(const Zi::Path &);

template <typename Heap = ZuVoid>
class Auto_ : public Heap, public Ztls_::VaultStore {
public:
  Auto_(const Zi::Path &home, ZiPIDFile *pid) :
    m_home{home}, m_dir{ZiFile::append(home, "vault")}, m_pid{pid} { }

  VaultResult init(const VaultConfig &cf) override {
    m_config = cf;
#ifdef _WIN32
    if (cf.variant != VaultVariant::Direct)
#endif
    {
      m_native = Ztls_::vaultKeyRing();
      auto result = m_native->init(cf);
      if (!result.template is<ZeException>()) return {};
      m_native->final();
      m_native = nullptr;
    }
    return file_();
  }

  void final() override {
    if (m_native) {
      m_native->final();
      m_native = nullptr;
    }
    if (m_file) {
      m_file->final();
      m_file = nullptr;
    }
  }

  VaultResult load(ZuCSpan key, VaultLoadFn fn) override {
    if (m_native) {
      auto result = m_native->load(key, fn);
      if (!result.template is<ZeException>()) return result;
    }
    auto result = file_();
    if (result.template is<ZeException>()) return result;
    return m_file->load(key, ZuMv(fn));
  }

  VaultResult save(ZuCSpan key, ZuBSpan value) override {
    if (m_native) {
      auto result = m_native->save(key, value);
      if (!result.template is<ZeException>()) return result;
    }
    auto result = file_();
    if (result.template is<ZeException>()) return result;
    return m_file->save(key, value);
  }

private:
  VaultResult file_() {
    if (m_file) return {};
    auto result = ensureDir(m_home);
    if (result.template is<ZeException>()) return result;
    result = ensureDir(m_dir);
    if (result.template is<ZeException>()) return result;
    if (m_config.variant == VaultVariant::Direct &&
	m_pid->init(m_dir, "vault.pid") != ZiPIDFile::OK)
      return ZeEXCEPT(Error, "ZtlsVault", "vault already open");
    m_file = new File{m_dir};
    result = m_file->init(m_config);
    if (result.template is<ZeException>()) {
      m_file->final();
      m_file = nullptr;
    }
    return result;
  }

  VaultConfig m_config;
  Zi::Path m_home;
  Zi::Path m_dir;
  ZiPIDFile *m_pid;
  ZmRef<Ztls_::VaultStore> m_native;
  ZmRef<Ztls_::VaultStore> m_file;
};
using AutoHeap = ZmHeap<"Ztls.Vault.Auto", Auto_<>>;
ZuDerive(Auto, Auto_<AutoHeap>);

// Versioned, length-delimited plaintext inside the authenticated age file.
constexpr uint8_t SecretsMagic[] = {'Z', 'V', 'L', 'T', 1};
constexpr auto PassphraseKey = "vault/passphrase"_Zu;
constexpr unsigned PassphraseSize = ZuBase64::enclen(32);

constexpr unsigned RecordSize = sizeof(uint32_t) + sizeof(uint64_t);

template <typename Heap = ZuVoid>
class Secrets_ : public Heap, public Ztls_::VaultStore {
public:
  Secrets_(ZmRef<Ztls_::VaultStore> store, const Zi::Path &dir) :
      m_store{ZuMv(store)}, m_dir{dir},
      m_path{ZiFile::append(dir, "secrets.age")} { }

  VaultResult init(const VaultConfig &cf) override {
    return m_store->init(cf);
  }
  void final() override { m_store->final(); }

  VaultResult load(ZuCSpan key, VaultLoadFn fn) override {
    ZiFile file;
    auto result = open_(file);
    if (result.template is<ZeException>()) return result;
    uint8_t passphrase[PassphraseSize];
    ZuGuard clearPassphrase{[&]() { ZuClear(passphrase, sizeof(passphrase)); }};
    result = passphrase_(passphrase, false);
    if (result.template is<ZeException>()) return result;
    Bytes plain;
    ZuGuard clearPlain{[&]() { ZuClear(plain.data(), plain.size()); }};
    result = decrypt_(file, passphrase, plain);
    if (result.template is<ZeException>()) return result;
    uint64_t offset = 0, length = 0;
    result = find_(plain, key, offset, length);
    if (result.template is<ZeException>()) return result;
    if (offset == UINT64_MAX)
      return ZeEXCEPT(Error, "ZtlsVault", "credential missing");
    fn(ZuBSpan{plain.data() + offset + RecordSize + key.length(), length});
    return {};
  }

  VaultResult save(ZuCSpan key, ZuBSpan value) override {
    if (key.length() > UINT32_MAX ||
	value.length() > SIZE_MAX - RecordSize - key.length())
      return ZeEXCEPT(Error, "ZtlsVault", "aggregate too large");
    uint64_t newSize = RecordSize + key.length() + value.length();
    ZiFile file;
    bool exists = false;
    if (file.open(m_path, ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC)
	== Zi::OK)
      exists = true;
    else if (file.error().errNo() != ZiENOENT
#ifdef _WIN32
	&& file.error().errNo() != ERROR_PATH_NOT_FOUND
#endif
      )
      return ZeEXCEPT(Error, "ZtlsVault", "aggregate open failed");

    uint8_t passphrase[PassphraseSize];
    ZuGuard clearPassphrase{[&]() { ZuClear(passphrase, sizeof(passphrase)); }};
    auto result = passphrase_(passphrase, !exists);
    if (result.template is<ZeException>()) return result;
    Bytes plain;
    ZuGuard clearPlain{[&]() { ZuClear(plain.data(), plain.size()); }};
    if (exists) {
      result = decrypt_(file, passphrase, plain, newSize);
      if (result.template is<ZeException>()) return result;
      file.close();
    } else {
      plain = Bytes{ZuBSpan{SecretsMagic}};
    }
    uint64_t offset = 0, oldLength = 0;
    result = find_(plain, key, offset, oldLength);
    if (result.template is<ZeException>()) return result;
    uint64_t oldSize = offset == UINT64_MAX ? 0 :
      RecordSize + key.length() + oldLength;
    uint64_t oldTotal = plain.length();
    if (newSize > SIZE_MAX - oldTotal + oldSize)
      return ZeEXCEPT(Error, "ZtlsVault", "aggregate too large");
    uint64_t newTotal = oldTotal - oldSize + newSize;
    if (offset == UINT64_MAX) offset = oldTotal;
    // Reserve before moving; replacement changes only the suffix in place.
    if (newTotal > plain.size()) plain.size(newTotal);
    uint64_t tail = oldTotal - offset - oldSize;
    if (tail)
      ::memmove(plain.data() + offset + newSize,
	plain.data() + offset + oldSize, tail);
    uint32_t keySize = ZuBE(uint32_t(key.length()));
    uint64_t valSize = ZuBE(uint64_t(value.length()));
    ::memcpy(plain.data() + offset, &keySize, sizeof(keySize));
    ::memcpy(plain.data() + offset + sizeof(keySize), &valSize, sizeof(valSize));
    ::memcpy(plain.data() + offset + RecordSize, key.data(), key.length());
    if (value)
      ::memcpy(plain.data() + offset + RecordSize + key.length(),
	value.data(), value.length());
    plain.length(newTotal);
    Random rng;
    if (!rng.init())
      return ZeEXCEPT(Error, "ZtlsVault", "encryption unavailable");
    ZtlsAge age;
    ZtlsAge::Recipient recipient{ZtlsAge::ScryptRecipient{passphrase}};
    return publish(m_dir, m_path, "secrets.age.tmp.", [&](ZiFile &output) {
      return age.encrypt(rng, {&recipient, 1}, plain, output);
    });
  }

private:
  VaultResult open_(ZiFile &file) const {
    if (file.open(m_path, ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC)
	== Zi::OK) return {};
    if (file.error().errNo() == ZiENOENT
#ifdef _WIN32
	|| file.error().errNo() == ERROR_PATH_NOT_FOUND
#endif
      )
      return ZeEXCEPT(Error, "ZtlsVault", "credential missing");
    return ZeEXCEPT(Error, "ZtlsVault", "aggregate open failed");
  }

  VaultResult passphrase_(ZuSpan<uint8_t> passphrase, bool create) {
    if (!create) {
      bool loaded = false;
      auto result = m_store->load(PassphraseKey, [&](ZuBSpan stored) {
	if (stored.length() == passphrase.length()) {
	  ::memcpy(passphrase.data(), stored.data(), stored.length());
	  loaded = true;
	}
      });
      if (result.template is<ZeException>()) return result;
      if (!loaded)
	return ZeEXCEPT(Error, "ZtlsVault", "invalid passphrase");
      return {};
    }
    uint8_t random[32];
    ZuGuard clearRandom{[&]() { ZuClear(random, sizeof(random)); }};
    Random rng;
    if (!rng.init() || !rng.random(random))
      return ZeEXCEPT(Error, "ZtlsVault", "passphrase generation failed");
    ZuBase64::encode(passphrase, random);
    return m_store->save(PassphraseKey, passphrase);
  }

  VaultResult decrypt_(ZiFile &file, ZuBSpan passphrase, Bytes &plain,
      uint64_t extra = 0) const {
    auto size = file.size();
    if (size <= 0 || uint64_t(size) > SIZE_MAX - extra)
      return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
    plain.size(size + extra);
    ZtlsAge age;
    ZtlsAge::Identity identity{ZtlsAge::ScryptIdentity{passphrase}};
    auto result = age.decrypt(file, {&identity, 1},
      ZuSpan<uint8_t>{plain.data(), plain.size()});
    if (result.template is<ZeException>())
      return ZuMv(result).template p<ZeException>();
    plain.length(result.template p<size_t>());
    return {};
  }

  VaultResult find_(ZuBSpan plain, ZuCSpan key,
      uint64_t &offset, uint64_t &valueLength) const {
    if (plain.length() < sizeof(SecretsMagic) ||
	::memcmp(plain.data(), SecretsMagic, sizeof(SecretsMagic)))
      return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
    offset = UINT64_MAX;
    for (uint64_t pos = sizeof(SecretsMagic), end = plain.length();
	pos < end;) {
      if (end - pos < RecordSize)
	return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
      uint32_t keySize;
      uint64_t valSize;
      ::memcpy(&keySize, plain.data() + pos, sizeof(keySize));
      ::memcpy(&valSize, plain.data() + pos + sizeof(keySize), sizeof(valSize));
      keySize = ZuBE(keySize);
      valSize = ZuBE(valSize);
      if (keySize > end - pos - RecordSize ||
	  valSize > end - pos - RecordSize - keySize)
	return ZeEXCEPT(Error, "ZtlsVault", "malformed aggregate");
      if (keySize == key.length() &&
	  !::memcmp(plain.data() + pos + RecordSize, key.data(), keySize)) {
	offset = pos;
	valueLength = valSize;
      }
      pos += RecordSize + keySize + valSize;
    }
    return {};
  }

  ZmRef<Ztls_::VaultStore> m_store;
  Zi::Path m_dir;
  Zi::Path m_path;
};
using SecretsHeap = ZmHeap<"Ztls.Vault.Secrets", Secrets_<>>;
ZuDerive(Secrets, Secrets_<SecretsHeap>);

template <typename L>
VaultResult withKey(Scope scope, ZuCSpan name, L &&l)
{
  if (!name || name.find([](char c) { return c == '/'; }) >= 0)
    return ZeEXCEPT(Error, "ZtlsVault", "invalid credential name");
  ZuCSpan env;
  if (scope.template is<Scopes::Environment>()) {
    env = scope.template p<Scopes::Environment>().name;
    if (!env || env.find([](char c) { return c == '/'; }) >= 0)
      return ZeEXCEPT(Error, "ZtlsVault", "invalid environment name");
  } else if (!scope.template is<Scopes::Global>())
    return ZeEXCEPT(Error, "ZtlsVault", "invalid scope");

  auto key = ZtScratch(VaultString,
    (env ? 5 + env.length() : 7) + name.length() + 1);
  if (!key.data())
    return ZeEXCEPT(Error, "ZtlsVault", "key allocation failed");
  if (env)
    key << "env/" << env << '/' << name;
  else
    key << "global/" << name;
  return ZuFwd<L>(l)(ZuCSpan{key});
}

void upper(VaultString &s)
{
  for (unsigned i = 0, n = s.length(); i < n; ++i)
    if (s[i] >= 'a' && s[i] <= 'z') s[i] -= 'a' - 'A';
}

VaultResult ensureDir(const Zi::Path &path)
{
  ZiFile::mkdir(path, 0700);
  ZiFile dir;
  if (dir.open(path, ZiFile::ReadOnly | ZiFile::Directory |
	ZiFile::NoFollow | ZiFile::GC) != Zi::OK)
    return ZeEXCEPT(Error, "ZtlsVault", "vault directory unavailable");
  return {};
}

template <typename Heap = ZuVoid>
struct State_ : public Heap {
  VaultConfig config;
  Zi::Path home;
  Zi::Path dir;
  Zi::Path modulePath;
  ZiPIDFile pid;
  ZmRef<Ztls_::VaultStore> store;
};

using StateHeap = ZmHeap<"Ztls.Vault.State", State_<>>;

} // Vault_

struct Vault::State : Vault_::State_<Vault_::StateHeap> { };

Vault::Vault() = default;
Vault::~Vault() { final(); }

VaultResult Vault::init(const VaultConfig &config)
{
  if (m_state)
    return ZeEXCEPT(Error, "ZtlsVault", "already initialized");
  if (config.store < VaultStore::Ephemeral || config.store >= VaultStore::N ||
      config.variant < VaultVariant::Default ||
      config.variant >= VaultVariant::N)
    return ZeEXCEPT(Error, "ZtlsVault", "invalid configuration");

  auto state = new State{};
  state->config = config;
  auto &cf = state->config;
  if (cf.variant == VaultVariant::Default) {
#ifdef _WIN32
    cf.variant = cf.store == VaultStore::KeyRing ||
      cf.store == VaultStore::Auto ?
      VaultVariant::Secrets : VaultVariant::Direct;
#else
    cf.variant = VaultVariant::Direct;
#endif
  }
#ifdef _WIN32
  if (cf.store == VaultStore::KeyRing && cf.variant == VaultVariant::Direct) {
    delete state;
    return ZeEXCEPT(Error, "ZtlsVault", "native direct mode unsupported");
  }
#endif
  if (!cf.service) cf.service = ZiLog::program();
  if (!cf.service) {
    delete state;
    return ZeEXCEPT(Error, "ZtlsVault", "service unavailable");
  }
  if (!cf.envPrefix) {
    cf.envPrefix = cf.service;
    Vault_::upper(cf.envPrefix);
  }

  VaultString envName = cf.envPrefix;
  envName << "_HOME";
  auto override = Zt::getpath(envName);
  if (override && *override)
    state->home = override;
  else if (auto base = Zt::getpath("HOME"))
    state->home = ZiFile::append(Zi::Path{base}, Zi::Path{} << '.' << cf.service);
  if (!state->home) {
    delete state;
    return ZeEXCEPT(Error, "ZtlsVault", "home unavailable");
  }
  if (!ZiFile::absolute(state->home))
    state->home = ZiFile::append(ZiFile::cwd(), state->home);
  if (cf.module) {
    state->modulePath = cf.module;
    if (!ZiFile::absolute(state->modulePath))
      state->modulePath = ZiFile::append(ZiFile::cwd(), state->modulePath);
  }

  if (!cf.account) {
    VaultString home{state->home};
    MD<SHA256> hash;
    hash.update(home);
    uint8_t digest[MD<SHA256>::Size];
    hash.finish(digest);
    char hex[16];
    ZuHex::encode(hex, ZuBSpan{digest, 8});
    ZuClear(digest, sizeof(digest));
    for (char &c : hex) if (c >= 'A' && c <= 'F') c += 'a' - 'A';
    cf.account << "vault|" << ZuCSpan{hex, sizeof(hex)};
  }
  m_state = state;
  return {};
}

VaultResult Vault::start()
{
  if (!m_state)
    return ZeEXCEPT(Error, "ZtlsVault", "not initialized");
  if (m_state->store)
    return ZeEXCEPT(Error, "ZtlsVault", "already started");

  auto &cf = m_state->config;
  bool secrets = cf.variant == VaultVariant::Secrets;
  if (cf.store == VaultStore::File || secrets) {
    auto result = Vault_::ensureDir(m_state->home);
    if (result.template is<ZeException>()) return result;
    m_state->dir = ZiFile::append(m_state->home, "vault");
    result = Vault_::ensureDir(m_state->dir);
    if (result.template is<ZeException>()) return result;
    if (m_state->pid.init(m_state->dir, "vault.pid") != ZiPIDFile::OK)
      return ZeEXCEPT(Error, "ZtlsVault", "vault already open");
  }
  ZmRef<Ztls_::VaultStore> selected;
  switch (cf.store) {
  case VaultStore::Ephemeral:
    selected = new Vault_::Ephemeral;
    break;
  case VaultStore::File:
    selected = new Vault_::File{m_state->dir};
    break;
  case VaultStore::KeyRing:
#if defined(_WIN32) || defined(__linux__)
    selected = Ztls_::vaultKeyRing();
    break;
#else
    m_state->pid.final();
    return ZeEXCEPT(Error, "ZtlsVault", "native store not yet implemented");
#endif
  case VaultStore::Module: {
    if (!cf.module) {
      m_state->pid.final();
      return ZeEXCEPT(Error, "ZtlsVault", "module path missing");
    }
    ZiModule module;
    if (module.load(m_state->modulePath, 0) != Zi::OK) {
      m_state->pid.final();
      return ZeEXCEPT(Error, "ZtlsVault", "module load failed");
    }
    auto factory = reinterpret_cast<Ztls_::VaultStoreFn>(
      module.resolve(ZtlsVaultStoreFnSym));
    if (!factory) {
      module.unload();
      m_state->pid.final();
      return ZeEXCEPT(Error, "ZtlsVault", "module factory missing");
    }
    selected = factory();
    if (!selected) {
      m_state->pid.final();
      return ZeEXCEPT(Error, "ZtlsVault", "module factory failed");
    }
  } break;
  case VaultStore::Auto:
    selected = new Vault_::Auto{m_state->home, &m_state->pid};
    break;
  default:
    m_state->pid.final();
    return ZeEXCEPT(Error, "ZtlsVault", "store not yet implemented");
  }
  m_state->store = secrets ?
    ZmRef<Ztls_::VaultStore>{new Vault_::Secrets{ZuMv(selected),
      m_state->dir}} :
    ZuMv(selected);
  auto result = m_state->store->init(cf);
  if (result.template is<ZeException>()) {
    m_state->store->final();
    m_state->store = nullptr;
    m_state->pid.final();
  }
  return result;
}

void Vault::stop()
{
  if (!m_state || !m_state->store) return;
  m_state->store->final();
  m_state->store = nullptr;
  m_state->pid.final();
}

void Vault::final()
{
  if (!m_state) return;
  ZmAssert(!m_state->store);
  delete m_state;
  m_state = nullptr;
}

VaultResult Vault::load(Scope scope, ZuCSpan name, VaultLoadFn fn)
{
  if (!m_state || !m_state->store)
    return ZeEXCEPT(Error, "ZtlsVault", "not started");
  return Vault_::withKey(ZuMv(scope), name,
    [this, &fn](ZuCSpan key) { return m_state->store->load(key, ZuMv(fn)); });
}

VaultResult Vault::save(Scope scope, ZuCSpan name, ZuBSpan value)
{
  if (!m_state || !m_state->store)
    return ZeEXCEPT(Error, "ZtlsVault", "not started");
  return Vault_::withKey(ZuMv(scope), name,
    [this, value](ZuCSpan key) { return m_state->store->save(key, value); });
}

} // Ztls
