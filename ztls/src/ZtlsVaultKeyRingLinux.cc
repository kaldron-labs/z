//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Linux Secret Service store, transported by the in-tree D-Bus client.

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZdbusClient.hh>

#include <zlib/ZtlsVaultStore.hh>

#include "ZtlsVaultSS.hh"

namespace Ztls_ {

namespace VaultKeyRingLinux_ {

constexpr auto Destination = "org.freedesktop.secrets"_Zu;
constexpr auto ServicePath = "/org/freedesktop/secrets"_Zu;
constexpr auto ServiceIF = "org.freedesktop.Secret.Service"_Zu;
constexpr auto CollectionIF = "org.freedesktop.Secret.Collection"_Zu;
constexpr auto ItemIF = "org.freedesktop.Secret.Item"_Zu;
constexpr auto SessionIF = "org.freedesktop.Secret.Session"_Zu;
constexpr auto PromptIF = "org.freedesktop.Secret.Prompt"_Zu;
constexpr unsigned TimeoutSeconds = 5;

template <typename Heap = ZuVoid>
class KeyRing_ : public Heap, public VaultStore {
public:
  Ztls::VaultResult init(const Ztls::VaultConfig &cf) override {
    ZdbusAddress address;
    if (!ZdbusAddress::session(address))
      return ZeEXCEPT(Error, "ZtlsVault", "session bus unavailable");
    m_service = cf.service;
    m_account = cf.account;
    m_sched.start();
    m_client.init(&m_sched, 1, 2, ZuMv(address), {},
      [this](ZuCSpan) {
        if (!m_ready.cmpXch(1, 0)) m_readySem.post();
      }, {}, {}, [this](auto) {
        if (!m_ready.xch(2)) m_readySem.post();
      });
    m_live = true;
    m_client.start();
    if (m_readySem.timedwait(Zm::now(TimeoutSeconds)) ||
        m_ready.load_() != 1)
      return ZeEXCEPT(Error, "ZtlsVault", "session bus connection failed");

    SS::OpenSessionArg arg{"plain", SS::Variant{SS::Text{""}}};
    return call_<SS::OpenSessionArg, SS::OpenSessionRes>(
      ServicePath, ServiceIF, "OpenSession", arg,
      [this](SS::OpenSessionRes &reply) -> Ztls::VaultResult {
        if (!reply.output.template is<SS::Text>())
          return ZeEXCEPT(Error, "ZtlsVault", "unsupported session response");
        m_session = ZuMv(reply.session);
        return {};
      });
  }

  void final() override {
    if (!m_live) return;
    if (m_session && m_ready.load_() == 1) {
      SS::Empty empty;
      call_<SS::Empty, SS::Empty>(m_session, SessionIF, "Close", empty,
        [](SS::Empty &) -> Ztls::VaultResult { return {}; });
    }
    m_session.clear();
    ZmBlock<>{}([this](auto done) {
      m_client.stop(ZuMv(done));
    });
    m_client.final();
    m_sched.stop();
    m_live = false;
    m_ready.store_(0);
    m_readySem.reset();
    m_service.clear();
    m_account.clear();
  }

  Ztls::VaultResult load(ZuCSpan key, Ztls::VaultLoadFn fn) override {
    SS::Attrs attrs;
    attributes_(attrs, key);
    SS::SearchArg arg{SS::AttrsView{&attrs}};
    SS::Paths unlocked, locked;
    auto result = call_<SS::SearchArg, SS::SearchRes>(
      ServicePath, ServiceIF, "SearchItems", arg,
      [&unlocked, &locked](SS::SearchRes &reply) -> Ztls::VaultResult {
        unlocked = ZuMv(reply.unlocked);
        locked = ZuMv(reply.locked);
        return {};
      });
    if (result.template is<ZeException>()) return result;
    if (!unlocked.length() && locked.length()) {
      result = unlock_(locked, unlocked);
      if (result.template is<ZeException>()) return result;
    }
    if (!unlocked.length() && locked.length())
      return ZeEXCEPT(Error, "ZtlsVault", "credential remains locked");
    if (!unlocked.length())
      return ZeEXCEPT(Error, "ZtlsVault", "credential missing");
    SS::GetSecretArg secretArg{m_session};
    return call_<SS::GetSecretArg, SS::GetSecretRes>(
      unlocked[0], ItemIF, "GetSecret", secretArg,
      [&fn](SS::GetSecretRes &reply) -> Ztls::VaultResult {
        fn(reply.secret.value);
        return {};
      });
  }

  Ztls::VaultResult save(ZuCSpan key, ZuBSpan value) override {
    SS::ReadAliasArg alias{"default"};
    SS::Text collection;
    auto result = call_<SS::ReadAliasArg, SS::ReadAliasRes>(
      ServicePath, ServiceIF, "ReadAlias", alias,
      [&collection](SS::ReadAliasRes &reply) -> Ztls::VaultResult {
        collection = ZuMv(reply.collection);
        return {};
      });
    if (result.template is<ZeException>()) return result;
    if (collection == "/") {
      SS::CreateCollectionArg arg;
      arg.properties.add("org.freedesktop.Secret.Collection.Label",
        SS::Variant{SS::Text{m_service}});
      arg.alias = "default";
      result = call_<SS::CreateCollectionArg, SS::CreateCollectionRes>(
        ServicePath, ServiceIF, "CreateCollection", arg,
        [this, &collection](SS::CreateCollectionRes &reply) -> Ztls::VaultResult {
          if (reply.prompt != "/") {
            SS::PromptPath resolved;
            auto prompted = prompt_(reply.prompt, resolved);
            if (prompted.template is<ZeException>()) return prompted;
            collection = ZuMv(resolved.path);
            return {};
          }
          collection = ZuMv(reply.collection);
          return {};
        });
      if (result.template is<ZeException>()) return result;
    }
    if (collection == "/")
      return ZeEXCEPT(Error, "ZtlsVault", "collection unavailable");
    SS::Paths objects;
    objects.push(collection);
    SS::Paths unlocked;
    result = unlock_(objects, unlocked);
    if (result.template is<ZeException>()) return result;
    if (!unlocked.length())
      return ZeEXCEPT(Error, "ZtlsVault", "collection remains locked");

    SS::Attrs attrs;
    attributes_(attrs, key);
    SS::CreateItemArg item;
    item.properties.add("org.freedesktop.Secret.Item.Label",
      SS::Variant{SS::Text{key}});
    item.properties.add("org.freedesktop.Secret.Item.Attributes",
      SS::Variant{SS::AttrsView{&attrs}});
    item.secret = SS::Secret{m_session, {}, value,
      SS::Text{"application/octet-stream"}};
    item.replace = true;
    return call_<SS::CreateItemArg, SS::CreateItemRes>(
      collection, CollectionIF, "CreateItem", item,
      [this](SS::CreateItemRes &reply) -> Ztls::VaultResult {
        if (reply.prompt != "/") {
          SS::PromptPath resolved;
          return prompt_(reply.prompt, resolved);
        }
        return {};
      });
  }

private:
  void attributes_(SS::Attrs &attrs, ZuCSpan key) const {
    attrs.add("service", m_service);
    attrs.add("account", m_account);
    attrs.add("key", key);
  }

  Ztls::VaultResult unlock_(const SS::Paths &objects,
      SS::Paths &unlocked) {
    SS::UnlockArg arg{objects};
    return call_<SS::UnlockArg, SS::UnlockRes>(
      ServicePath, ServiceIF, "Unlock", arg,
      [this, &unlocked](SS::UnlockRes &reply) -> Ztls::VaultResult {
        unlocked = ZuMv(reply.unlocked);
        if (reply.prompt != "/") {
          SS::PromptPaths resolved;
          auto prompted = prompt_(reply.prompt, resolved);
          if (prompted.template is<ZeException>()) return prompted;
          for (auto &path : resolved.paths)
            unlocked.push(ZuMv(path));
          return {};
        }
        return {};
      });
  }

  template <typename Out>
  Ztls::VaultResult prompt_(ZuCSpan path, Out &output) {
    ZmSemaphore signalSem;
    ZmRef<ZiIOBuf> signalFrame;
    Zdbus_::FrameInfo signalInfo;
    auto id = ZmBlock<uint64_t>{}([this, path, &signalSem,
        &signalFrame, &signalInfo](auto done) {
      m_client.subscribe(path, PromptIF, "Completed",
        [&signalSem, &signalFrame, &signalInfo](ZmRef<ZiIOBuf> frame,
            Zdbus_::FrameInfo info) {
          if (signalFrame) return;
          signalInfo = info;
          signalFrame = ZuMv(frame);
          signalSem.post();
        }, ZuMv(done));
    });
    if (!id)
      return ZeEXCEPT(Error, "ZtlsVault", "prompt subscription failed");
    SS::Text rule;
    rule << "type='signal',interface='" << PromptIF <<
      "',member='Completed',path='" << path << '\'';
    bool matched = false;
    ZuGuard cleanup{[this, path, id, &rule, &matched]() {
      ZmBlock<bool>{}([this, path, id](auto done) {
        m_client.unsubscribe(path, PromptIF, "Completed", id, ZuMv(done));
      });
      if (matched)
        ZmBlock<Zdbus_::CallResult>{}([this, &rule](auto done) {
          m_client.removeMatch(rule, ZuMv(done), Zm::now(TimeoutSeconds));
        });
    }};
    auto match = ZmBlock<Zdbus_::CallResult>{}([this, &rule](auto done) {
      m_client.addMatch(rule, ZuMv(done), Zm::now(TimeoutSeconds));
    });
    if (!match || match.info.type != Zdbus_::MessageType::MethodReturn)
      return ZeEXCEPT(Error, "ZtlsVault", "prompt match failed");
    matched = true;
    SS::PromptArg arg{""};
    auto result = call_<SS::PromptArg, SS::Empty>(
      path, PromptIF, "Prompt", arg,
      [](SS::Empty &) -> Ztls::VaultResult { return {}; });
    if (result.template is<ZeException>()) return result;
    if (signalSem.timedwait(Zm::now(TimeoutSeconds)))
      return ZeEXCEPT(Error, "ZtlsVault", "prompt timed out");
    auto handler = ZfDBUS::handler<SS::PromptCompleted>({
      signalFrame->cspan(signalInfo.bodyOffset),
      signalInfo.headers.signature, signalInfo.bodyOffset,
      signalInfo.order});
    if (!handler)
      return ZeEXCEPT(Error, "ZtlsVault", "malformed prompt completion");
    auto completed = handler.ctor();
    if (completed.dismissed)
      return ZeEXCEPT(Error, "ZtlsVault", "prompt dismissed");
    if (!completed.result.template is<ZfDBUS::Any>())
      return ZeEXCEPT(Error, "ZtlsVault", "unexpected prompt result");
    auto any = completed.result.template p<ZfDBUS::Any>();
    auto value = ZfDBUS::handler<Out>({any.bytes, any.signature,
      any.offset, any.order});
    if (!value)
      return ZeEXCEPT(Error, "ZtlsVault", "malformed prompt result");
    output = value.ctor();
    return {};
  }

  template <typename Arg, typename Res, typename Fn>
  Ztls::VaultResult call_(ZuCSpan path, ZuCSpan interface,
      ZuCSpan member, const Arg &arg, Fn &&fn) {
    auto result = ZmBlock<Zdbus_::CallResult>{}([&](auto done) {
      m_client.call([&](uint32_t serial) {
        Zdbus_::HeadSpec head;
        head.type = Zdbus_::MessageType::MethodCall;
        head.serial = serial;
        head.path = path;
        head.interface = interface;
        head.member = member;
        head.destination = Destination;
        return Zdbus_::message(head, arg);
      }, ZuMv(done), Zm::now(TimeoutSeconds));
    });
    if (!result)
      return ZeEXCEPT(Error, "ZtlsVault", "Secret Service call failed");
    if (result.info.type == Zdbus_::MessageType::Error)
      return ZeEXCEPT(Error, "ZtlsVault", result.info.headers.errorName);
    if (result.info.type != Zdbus_::MessageType::MethodReturn)
      return ZeEXCEPT(Error, "ZtlsVault", "unexpected Secret Service reply");
    auto handler = ZfDBUS::handler<Res>({
      result.frame->cspan(result.info.bodyOffset),
      result.info.headers.signature, result.info.bodyOffset,
      result.info.order});
    if (!handler)
      return ZeEXCEPT(Error, "ZtlsVault", "malformed Secret Service reply");
    auto reply = handler.ctor();
    return ZuFwd<Fn>(fn)(reply);
  }

  ZmScheduler m_sched{ZmSchedParams().id("ZtlsVaultKeyRing").nThreads(2)};
  ZdbusClient m_client;
  ZmSemaphore m_readySem;
  ZmAtomic<unsigned> m_ready = 0;
  bool m_live = false;
  SS::Text m_service;
  SS::Text m_account;
  SS::Text m_session;
};

using KeyRingHeap = ZmHeap<"Ztls.Vault.KeyRing.Linux", KeyRing_<>>;
ZuDerive(KeyRing, KeyRing_<KeyRingHeap>);

} // namespace VaultKeyRingLinux_

VaultStore *vaultKeyRing() { return new VaultKeyRingLinux_::KeyRing; }

} // namespace Ztls_
