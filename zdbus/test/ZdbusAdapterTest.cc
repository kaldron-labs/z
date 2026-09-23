//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZdbusCatalog.hh>

using namespace ZuTestUtil;
using Text = ZtString<ZtStringHeapID<"ZdbusAdapterTest.Text">>;

struct Args { Text name; uint32_t count; };
ZfStruct(, Args,
  (((name), (Mutable)), (String)),
  (((count), (Mutable)), (UInt32)));
ZfStructRender(, Args, DBUS, name, count);

struct Call {
  Args args;
  Text path;
  Text member;
  Text destination;
};

using CallHeaders = ZuTypeList<
  Zdbus_::HeaderEntry<Zdbus_::Header::Path>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Interface,
    ZuStringT<"org.example.Test">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Member>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Destination>>;

struct CallBuilder : ZdbusReqBuilder<CallBuilder, Call, CallHeaders> {
  using Base = ZdbusReqBuilder<CallBuilder, Call, CallHeaders>;
  using Base::header;

  const Args &bodyObject(const Call *call) const { return call->args; }

  template <typename Key, typename Emit>
  void header(Emit &&emit) const {
    if constexpr (Key::Code == Zdbus_::HeaderCode::Path)
      emit(object->path);
    if constexpr (Key::Code == Zdbus_::HeaderCode::Member)
      emit(object->member);
    if constexpr (Key::Code == Zdbus_::HeaderCode::Destination)
      emit(object->destination);
  }
};

struct CallParser : ZdbusReqParser<CallParser, Call, CallHeaders> {
  using Base = ZdbusReqParser<CallParser, Call, CallHeaders>;
  using Base::header;
  Text seenPath;
  Text seenMember;

  Args &bodyObject(Call *call) { return call->args; }

  template <typename Key>
  void header(ZuCSpan value) {
    if constexpr (Key::Code == Zdbus_::HeaderCode::Path)
      seenPath = value;
    if constexpr (Key::Code == Zdbus_::HeaderCode::Member)
      seenMember = value;
  }
};

using RuntimeHeaders = ZuTypeList<
  Zdbus_::HeaderEntry<Zdbus_::Header::Interface,
    ZuStringT<"org.example.Test">>>;

struct RuntimeBuilder : ZdbusReqBuilder<RuntimeBuilder, Call, RuntimeHeaders> {
  using Base = ZdbusReqBuilder<RuntimeBuilder, Call, RuntimeHeaders>;
  using Base::header;

  const Args &bodyObject(const Call *call) const { return call->args; }

  template <typename Emit>
  void header(Emit &&emit) const {
    emit(Zdbus_::Header::Path{}, object->path);
    emit(Zdbus_::Header::Member{}, object->member);
    emit(Zdbus_::Header::Destination{}, object->destination);
  }
};

struct DuplicateBuilder : ZdbusReqBuilder<DuplicateBuilder, Call, CallHeaders> {
  using Base = ZdbusReqBuilder<DuplicateBuilder, Call, CallHeaders>;
  using Base::header;

  const Args &bodyObject(const Call *call) const { return call->args; }

  template <typename Key, typename Emit>
  void header(Emit &&emit) const {
    if constexpr (Key::Code == Zdbus_::HeaderCode::Path)
      emit(object->path);
    if constexpr (Key::Code == Zdbus_::HeaderCode::Member)
      emit(object->member);
    if constexpr (Key::Code == Zdbus_::HeaderCode::Destination)
      emit(object->destination);
  }

  template <typename Emit>
  void header(Emit &&emit) const {
    emit(Zdbus_::Header::Path{}, object->path);
  }
};

using ErrorHeaders = ZuTypeList<
  Zdbus_::HeaderEntry<Zdbus_::Header::ErrorName,
    ZuStringT<"org.example.Test.Failed">>>;
using SignalHeaders = ZuTypeList<
  Zdbus_::HeaderEntry<Zdbus_::Header::Path,
    ZuStringT<"/org/example/Test">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Interface,
    ZuStringT<"org.example.Test">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Member,
    ZuStringT<"Changed">>>;

struct ReplyBuilder : ZdbusResBuilder<ReplyBuilder, Args> { };
struct ReplyParser : ZdbusResParser<ReplyParser, Args> { };
struct ReplyParser2 : ZdbusResParser<ReplyParser2, Args> { };
struct ErrorBuilder : ZdbusErrBuilder<ErrorBuilder, Args, ErrorHeaders> { };
struct ErrorParser : ZdbusErrParser<ErrorParser, Args, ErrorHeaders> {
  using Base = ZdbusErrParser<ErrorParser, Args, ErrorHeaders>;
  using Base::header;
  Text errorName;

  template <typename Key>
  void header(ZuCSpan value) {
    if constexpr (Key::Code == Zdbus_::HeaderCode::ErrorName)
      errorName = value;
  }
};
struct SignalBuilder : ZdbusSigBuilder<SignalBuilder, Args,
  SignalHeaders> { };
struct SignalParser : ZdbusSigParser<SignalParser, Args,
  SignalHeaders> { };

using UnknownErrorHeaders = ZuTypeList<
  Zdbus_::HeaderEntry<Zdbus_::Header::ErrorName,
    ZuStringT<"org.example.Test.Other">>>;
struct UnknownErrorBuilder : ZdbusErrBuilder<UnknownErrorBuilder, Args,
  UnknownErrorHeaders> { };
struct CatalogReq : CallBuilder {
  using Responses = ZuTypeList<ReplyParser>;
  using Errors = ZuTypeList<ErrorParser>;
};
ZuAssert((ZuIsSame<Zdbus_::BodySignatureTag<ReplyParser>,
  Zdbus_::BodySignatureTag<ReplyParser2>>{}));

static Call sample()
{
  return {{{"alpha"}, 7}, "/org/example/Test", "Invoke",
    "org.example.Service"};
}

static void roundTrip()
{
  ZuTestScope(roundTrip);
  Call source = sample();
  CallBuilder builder;
  builder.init(&source);
  auto made = builder.build(31);
  ZuCHECK(bool(made), "fixed-key request build failed");
  if (!made) return;
  auto parsed = Zdbus_::frame(made.buf->cspan());
  ZuCHECK(bool(parsed), "fixed-key request parse failed");
  if (!parsed) return;
  ZuCheck(parsed.info.headers.signature == "su");
  ZuCheck(parsed.info.headers.interface == "org.example.Test");
  ZuCheck(parsed.info.headers.destination == source.destination);

  Call target;
  CallParser parser;
  parser.init(&target);
  auto loaded = parser.load(made.buf, parsed.info);
  ZuCHECK(bool(loaded), "typed request parse failed");
  ZuCheck(target.args.name == source.args.name);
  ZuCheck(target.args.count == source.args.count);
  ZuCheck(parser.seenPath == source.path);
  ZuCheck(parser.seenMember == source.member);
  ZuCheck(bool(parser.frame));
  parser.reset();
  ZuCheck(!parser.frame);
}

static void runtime()
{
  ZuTestScope(runtime);
  Call source = sample();
  RuntimeBuilder builder;
  builder.init(&source);
  auto made = builder.build(32);
  ZuCHECK(bool(made), "runtime-key request build failed");
  if (!made) return;
  auto parsed = Zdbus_::frame(made.buf->cspan());
  ZuCheck(bool(parsed));
  ZuCheck(parsed.info.headers.member == "Invoke");
}

static void duplicate()
{
  ZuTestScope(duplicate);
  Call source = sample();
  DuplicateBuilder builder;
  builder.init(&source);
  ZuCheck(!builder.build(33));
}

template <typename Builder, typename Parser>
static bool alternate(unsigned serial, unsigned replySerial,
  unsigned kind, ZuCSpan signature)
{
  Args source{{"changed"}, 19};
  Builder builder;
  builder.init(&source);
  auto made = builder.build(serial, replySerial);
  if (!made) return false;
  auto parsed = Zdbus_::frame(made.buf->cspan());
  if (!parsed || parsed.info.type != kind ||
      parsed.info.serial != serial ||
      parsed.info.headers.replySerial != replySerial ||
      parsed.info.headers.signature != signature) return false;
  if constexpr (ZuIsSame<Parser, ErrorParser>{})
    if (parsed.info.headers.errorName != "org.example.Test.Failed")
      return false;
  if constexpr (ZuIsSame<Parser, SignalParser>{}) {
    if (parsed.info.headers.path != "/org/example/Test" ||
        parsed.info.headers.interface != "org.example.Test" ||
        parsed.info.headers.member != "Changed") return false;
  }
  Args target;
  Parser parser;
  parser.init(&target);
  auto loaded = parser.load(made.buf, parsed.info);
  if (!loaded || target.name != source.name ||
      target.count != source.count) return false;
  if constexpr (ZuIsSame<Parser, ErrorParser>{})
    if (parser.errorName != "org.example.Test.Failed") return false;
  return true;
}

static void alternates()
{
  ZuTestScope(alternates);
  ZuCheck((alternate<ReplyBuilder, ReplyParser>(41, 31,
    Zdbus_::MessageType::MethodReturn, "su")));
  ZuCheck((alternate<ErrorBuilder, ErrorParser>(42, 31,
    Zdbus_::MessageType::Error, "su")));
  ZuCheck((alternate<SignalBuilder, SignalParser>(43, 0,
    Zdbus_::MessageType::Signal, "su")));
}

static void unknownHeader()
{
  ZuTestScope(unknownHeader);
  uint8_t bytes[] = {
    'l', 5, 0, 1, 0, 0, 0, 0, 3, 0, 0, 0, 8, 0, 0, 0,
    42, 1, 'u', 0, 7, 0, 0, 0
  };
  auto parsed = Zdbus_::frame(bytes);
  ZuCHECK(bool(parsed), "unknown-header frame parse failed");
  if (!parsed) return;
  unsigned calls = 0;
  auto ok = Zdbus_::eachHeader(bytes, parsed.info,
    [&calls](unsigned code, ZfDBUS::Any value) {
      ++calls;
      ZuCheck(code == 42);
      ZuCheck(value.signature == "u");
      ZuCheck(value.offset == 20);
      ZuCheck(value.bytes.length() == 4);
      ZuCheck(value.bytes[0] == 7);
    });
  ZuCheck(ok && calls == 1);
}

static void catalog()
{
  ZuTestScope(catalog);
  Args source{{"catalog"}, 73};
  ReplyBuilder reply;
  reply.init(&source);
  ErrorBuilder error;
  error.init(&source);
  UnknownErrorBuilder unknown;
  unknown.init(&source);

  int kind = 0;
  bool valueOK = false;
  auto visit = [&kind, &valueOK](auto value) {
    using Value = ZuDecay<decltype(value)>;
    if constexpr (ZuIsSame<Value, Zdbus_::Parsed<ReplyParser>>{}) {
      kind = 1;
      valueOK = value.object.name == "catalog" &&
        value.object.count == 73 && bool(value.frame);
    } else if constexpr (ZuIsSame<Value,
        Zdbus_::Parsed<ErrorParser>>{}) {
      kind = 2;
      valueOK = value.info.headers.errorName ==
        "org.example.Test.Failed" &&
        value.object.count == 73 && bool(value.frame);
    } else if constexpr (ZuIsSame<Value, Zdbus_::RemoteError>{}) {
      kind = 3;
      valueOK = value.name() == "org.example.Test.Other" &&
        value.signature() == "su" && bool(value.frame);
    }
  };

  auto made = reply.build(51, 31);
  ZuCHECK(bool(made), "catalog reply build failed");
  if (!made) return;
  auto parsed = Zdbus_::frame(made.buf->cspan());
  ZuCHECK(bool(parsed), "catalog reply parse failed");
  if (!parsed) return;
  Zdbus_::dispatchReply<CatalogReq>(made.buf, parsed.info, 0, visit);
  ZuCheck(kind == 1 && valueOK);

  kind = 0;
  made = error.build(52, 31);
  ZuCHECK(bool(made), "catalog error build failed");
  if (!made) return;
  parsed = Zdbus_::frame(made.buf->cspan());
  ZuCHECK(bool(parsed), "catalog error parse failed");
  if (!parsed) return;
  Zdbus_::dispatchReply<CatalogReq>(made.buf, parsed.info, 0, visit);
  ZuCheck(kind == 2 && valueOK);

  kind = 0;
  made = unknown.build(53, 31);
  ZuCHECK(bool(made), "unknown error build failed");
  if (!made) return;
  parsed = Zdbus_::frame(made.buf->cspan());
  ZuCHECK(bool(parsed), "unknown error parse failed");
  if (!parsed) return;
  Zdbus_::dispatchReply<CatalogReq>(made.buf, parsed.info, 0, visit);
  ZuCheck(kind == 3 && valueOK);

}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(roundTrip);
  ZuTestCall(runtime);
  ZuTestCall(duplicate);
  ZuTestCall(alternates);
  ZuTestCall(unknownHeader);
  ZuTestCall(catalog);
  return 0;
}
