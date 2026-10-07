//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZdbusAdapter.hh>
#include <zlib/ZdbusConnection.hh>
#include <zlib/ZdbusMessage.hh>

using namespace ZuTestUtil;

ZuDerive(Text, (ZtString<ZtStringHeapID<"ZdbusMessageTest.Text">>));

struct Args { Text text; uint32_t count; };
struct Empty { };

ZfStruct(, Args,
  (text, (Mutable),		String),
  (count, (Mutable),		UInt32));
ZfStructRender(, Args, DBUS, text, count);
ZuTypeList<> ZuFields_(Empty *, ZuFacet::DBUS *);

static void call()
{
  ZuTestScope(call);
  Zdbus_::HeadSpec head;
  head.type = Zdbus_::MessageType::MethodCall;
  head.serial = 17;
  head.path = "/org/example/Test";
  head.interface = "org.example.Test";
  head.member = "Invoke";
  head.destination = "org.example.Service";
  Args args{{"test"}, 42};
  auto made = Zdbus_::message(head, args);
  ZuCHECK(bool(made), "message build failed");
  if (!made) return;
  auto parsed = Zdbus_::frame(made.buf->cspan());
  ZuCHECK(bool(parsed), "message parse failed");
  if (!parsed) return;
  ZuCheck(parsed.info.serial == 17);
  ZuCheck(parsed.info.headers.path == head.path);
  ZuCheck(parsed.info.headers.interface == head.interface);
  ZuCheck(parsed.info.headers.member == head.member);
  ZuCheck(parsed.info.headers.destination == head.destination);
  ZuCheck(parsed.info.headers.signature == "su");
  ZuCheck(parsed.info.total == made.buf->length);
  auto body = made.buf->cspan(parsed.info.bodyOffset);
  auto handler = ZfDBUS::handler<Args>({body, "su",
    parsed.info.bodyOffset, parsed.info.order});
  ZuCHECK(bool(handler), "body handler failed");
  if (!handler) return;
  auto decoded = handler.ctor();
  ZuCheck(decoded.text == args.text && decoded.count == args.count);
}

static void alternate()
{
  ZuTestScope(alternate);
  Zdbus_::HeadSpec head;
  head.type = Zdbus_::MessageType::Error;
  head.serial = 18;
  head.replySerial = 17;
  head.errorName = "org.example.Test.Error";
  head.member = "Call"; // valid on the wire, irrelevant to an error reply
  head.order = ZfDBUS::Order::Big;
  auto made = Zdbus_::message(head, Empty{});
  ZuCHECK(bool(made), "error build failed");
  if (!made) return;
  auto parsed = Zdbus_::frame(made.buf->cspan());
  ZuCHECK(bool(parsed), "error parse failed");
  if (!parsed) return;
  ZuCheck(parsed.info.type == Zdbus_::MessageType::Error);
  ZuCheck(parsed.info.serial == 18);
  ZuCheck(parsed.info.headers.replySerial == 17);
  ZuCheck(parsed.info.headers.errorName == head.errorName);
  ZuCheck(!parsed.info.headers.member.length());
  ZuCheck(!parsed.info.headers.signature.length());

  unsigned memberOffset = 0, typeOffset = 0;
  ZuCheck(Zdbus_::eachHeader(made.buf->cspan(), parsed.info,
    [&made, &memberOffset, &typeOffset](unsigned code,
        ZfDBUS::Any value) {
      if (code != Zdbus_::HeaderCode::Member) return;
      typeOffset = unsigned(ZuBSpan{value.signature}.begin() -
        made.buf->data());
      ZfDBUS::Reader reader{{value.bytes, value.signature,
        value.offset, value.order}};
      ZuBSpan text;
      if (ZfDBUS::text(reader, ZfDBUS::Type::String, text))
        memberOffset = unsigned(text.begin() - made.buf->data());
    }));
  ZuCHECK(memberOffset && typeOffset, "missing irrelevant member field");
  if (!memberOffset || !typeOffset) return;
  made.buf->data()[memberOffset] = '1';
  parsed = Zdbus_::frame(made.buf->cspan());
  ZuCheck(bool(parsed) && !parsed.info.headers.member.length());
  made.buf->data()[typeOffset] = 'u';
  ZuCheck(Zdbus_::frame(made.buf->cspan()).error ==
    Zdbus_::FrameError::Header);
}

static void emptyString()
{
  ZuTestScope(emptyString);
  Zdbus_::HeadSpec head;
  head.type = Zdbus_::MessageType::MethodCall;
  head.serial = 19;
  head.path = "/org/example/Test";
  head.member = "Empty";
  Args args{{}, 0};
  auto made = Zdbus_::message(head, args);
  ZuCHECK(bool(made), "empty-string message build failed");
  if (!made) return;
  auto parsed = Zdbus_::frame(made.buf->cspan());
  ZuCHECK(bool(parsed), "empty-string message parse failed");
  if (!parsed) return;
  auto handler = ZfDBUS::handler<Args>({
    made.buf->cspan(parsed.info.bodyOffset),
    parsed.info.headers.signature, parsed.info.bodyOffset,
    parsed.info.order});
  ZuCHECK(bool(handler), "empty-string body handler failed");
  if (!handler) return;
  auto decoded = handler.ctor();
  ZuCheck(!decoded.text.length() && !decoded.count);
}

static void invalid()
{
  ZuTestScope(invalid);
  Zdbus_::HeadSpec head;
  head.type = Zdbus_::MessageType::MethodCall;
  head.serial = 1;
  head.path = "not/a/path";
  head.member = "Call";
  ZuCheck(!Zdbus_::message(head, Empty{}));
  head.path = "/ok";
  ZuCheck(!Zdbus_::message(head, Empty{}, 16));
  head.member = "1Call";
  ZuCheck(!Zdbus_::message(head, Empty{}));
  head.member = "Call";
  head.interface = "org..example";
  ZuCheck(!Zdbus_::message(head, Empty{}));
  head.interface = "org.example.Test";
  head.destination = "1example.Service";
  ZuCheck(!Zdbus_::message(head, Empty{}));
  head.destination = ":1.42";
  ZuCheck(bool(Zdbus_::message(head, Empty{})));
  head.serial = 0;
  ZuCheck(!Zdbus_::message(head, Empty{}));
}

namespace Zdbus_ {

static void frameCopy(bool shortTail)
{
  ZuTestScope(frameCopy);
  HeadSpec head;
  head.type = MessageType::MethodCall;
  head.serial = 27;
  head.path = "/org/example/Test";
  head.interface = "org.example.Test";
  head.member = "Copy";
  head.destination = "org.example.Service";
  Args args{{"frame"}, 7};
  auto made = message(head, args);
  ZuCHECK(bool(made), "frame build failed");
  if (!made) return;

  unsigned total = made.buf->length;
  unsigned tail = shortTail ? 8 : total;
  ZmRef<ZiIOBuf> input = new ZiIOBufAlloc<ZiIOBuf_DefltSize,
    Wire::MaxMessageSize, "Zdbus.FrameTest">{};
  ZuCHECK(input->ensure(total + tail), "input allocation failed");
  if (input->size < total + tail) return;
  memcpy(input->data(), made.buf->data(), total);
  memcpy(input->data() + total, made.buf->data(), tail);
  input->length = total + tail;

  auto parsed = frame(input->cspan());
  ZuCHECK(bool(parsed), "frame parse failed");
  if (!parsed) return;
  auto info = parsed.info;
  auto observed = splitFrame_(input, info);
  ZuCHECK(bool(observed), "frame split failed");
  if (!observed) return;
  ZuCheck(observed->length == total);
  ZuCheck(input && input->length == tail);
  ZuCheck(!memcmp(input->data(), made.buf->data(), tail));
  ZuCheck(info.headers.path == head.path);
  ZuCheck(info.headers.interface == head.interface);
  ZuCheck(info.headers.member == head.member);
  ZuCheck(info.headers.destination == head.destination);
  ZuCheck(info.headers.signature == "su");
  auto begin = reinterpret_cast<uintptr_t>(observed->data());
  auto end = begin + observed->length;
  auto inside = [begin, end](ZuCSpan span) {
    auto address = reinterpret_cast<uintptr_t>(span.data());
    return address >= begin && address + span.length() <= end;
  };
  ZuCheck(inside(info.headers.path));
  ZuCheck(inside(info.headers.interface));
  ZuCheck(inside(info.headers.member));
  ZuCheck(inside(info.headers.destination));
  ZuCheck(inside(info.headers.signature));
}

} // Zdbus_

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(call);
  ZuTestCall(alternate);
  ZuTestCall(emptyString);
  ZuTestCall(invalid);
  ZuTestCall(Zdbus_::frameCopy, true);
  ZuTestCall(Zdbus_::frameCopy, false);
  return 0;
}
