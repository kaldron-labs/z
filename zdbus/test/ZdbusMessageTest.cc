//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSingleton.hh>
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
  auto body = parsed.info.body();
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
  ZuCheck(Zdbus_::eachHeader(parsed.info.span(), parsed.info,
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
    parsed.info.body(),
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

static void frameRanges(bool shortTail)
{
  ZuTestScopeRT(frameRanges);
  HeadSpec head;
  head.type = MessageType::MethodCall;
  head.serial = 27;
  head.path = "/org/example/Test";
  head.interface = "org.example.Test";
  head.member = "Copy";
  head.destination = "org.example.Service";
  Args args{{"frame"}, 7};
  auto made = message(head, args);
  ZuCheckRT(bool(made), log_("frame build failed"));
  if (!made) return;

  unsigned total = made.buf->length;
  unsigned tail = shortTail ? 8 : total;
  // The parser's input need not start at its backing's original address.
  unsigned start = sizeof(uint32_t) + 1;
  ZmRef<ZiIOBuf> input = new Zi::IOBufBuiltin<ZiIOBuf, ZiIOBuf_DefltSize, ZiIOBuf_Align, ZuStringT<"Zdbus.FrameTest">,
      Wire::MaxMessageSize>{
      ZmSingleton<Zi::IOBufBuiltinAllocator<Zi::IOBufType::App, ZiIOBuf,
        ZiIOBuf_DefltSize, Wire::MaxMessageSize, ZiIOBuf_Align,
        ZuStringT<"Zdbus.FrameTest">>>::instance()};
  ZuCheckRT(input->ensure(start + total + tail), log_("input allocation failed"));
  if (input->size < start + total + tail) return;
  input->skip = start;
  memcpy(input->data(), made.buf->data(), total);
  memcpy(input->data() + total, made.buf->data(), tail);
  input->length = total + tail;

  auto parsed = frame(input->cspan());
  ZuCheckRT(bool(parsed), log_("frame parse failed"));
  if (!parsed) return;
  auto info = parsed.info;
  auto backing = input;
  auto original = input->data();
  unsigned capacity = input->size;
  auto observed = splitFrame(input, info);
  ZuCheckRT(bool(observed), log_("frame split failed"));
  if (!observed) return;
  ZuCheckRT(observed.ptr() == backing.ptr() && input.ptr() == backing.ptr());
  ZuCheckRT(info.span().length() == total);
  ZuCheckRT(info.span().data() == original);
  ZuCheckRT(input->data() == original + total);
  ZuCheckRT(backing->data_() + start == original && backing->size == capacity);
  ZuCheckRT(input && input->length == tail);
  ZuCheckRT(!memcmp(input->data(), made.buf->data(), tail));
  ZuCheckRT(info.headers.path == head.path);
  ZuCheckRT(info.headers.interface == head.interface);
  ZuCheckRT(info.headers.member == head.member);
  ZuCheckRT(info.headers.destination == head.destination);
  ZuCheckRT(info.headers.signature == "su");
  auto begin = reinterpret_cast<uintptr_t>(info.data);
  auto end = begin + info.total;
  auto inside = [begin, end](auto span) {
    auto address = reinterpret_cast<uintptr_t>(span.data());
    return address >= begin && address + span.length() <= end;
  };
  ZuCheckRT(inside(info.headers.path));
  ZuCheckRT(inside(info.headers.interface));
  ZuCheckRT(inside(info.headers.member));
  ZuCheckRT(inside(info.headers.destination));
  ZuCheckRT(inside(info.headers.signature));
  ZuCheckRT(info.body().data() == info.data + info.bodyOffset &&
    info.body().length() == info.bodyLength && inside(info.body()));

  if (shortTail) {
    // The unfinished tail constructs directly in its final ordinary owner.
    // The delivered frame and its borrowed views keep the original backing.
    ZuCheckRT(detachInput(input, total));
    ZuCheckRT(input.ptr() != backing.ptr());
    ZuCheckRT(backing->refCount() == 2);
    ZuCheckRT(input->append(made.buf->cspan(tail)));
    auto next = frame(input->cspan());
    ZuCheckRT(bool(next) && next.info.span().data() == input->data());
  } else {
    // A complete coalesced frame uses the exact same original ZiIOBuf.
    auto next = frame(input->cspan());
    ZuCheckRT(bool(next));
    ZuCheckRT(next.info.data == original + total && next.info.total == total);
    auto second = input;
    ZuCheckRT(second.ptr() == observed.ptr());
    input->advance(total);
    ZuCheckRT(!input->length);
    ZuCheckRT(!memcmp(next.info.span().data(), made.buf->data(), total));
    input = ZuMv(second);
  }
  ZuCheckRT(info.span().data() == original);
  ZuCheckRT(!memcmp(info.span().data(), made.buf->data(), total));
  ZuCheckRT(info.headers.member == head.member);
  // Connection/input release must not invalidate an already delivered frame.
  input = nullptr;
  backing = nullptr;
  ZuCheckRT(info.span().data() == original);
  auto handler = ZfDBUS::handler<Args>({info.body(), info.headers.signature,
    info.bodyOffset, info.order});
  ZuCheckRT(bool(handler));
  if (handler) {
    auto value = handler.ctor();
    ZuCheckRT(value.text == args.text && value.count == args.count);
  }
}

static void frameAssembly()
{
  ZuTestScopeRT(frameAssembly);
  HeadSpec head;
  head.type = MessageType::MethodCall;
  head.serial = 28;
  head.path = "/org/example/Test";
  head.interface = "org.example.Test";
  head.member = "Copy";
  Args args{{"first"}, 7};
  auto first = message(head, args);
  Args large;
  large.count = 9;
  large.text.length(CxnDefault::RxBytes + Wire::MinHeaderSize);
  memset(large.text.data(), 'x', large.text.length());
  auto second = message(head, large);
  ZuCheckRT(bool(first) && bool(second));
  if (!first || !second) return;

  ZmRef<ZiIOBuf> input = new Zi::IOBufBuiltin<ZiIOBuf, ZiIOBuf_DefltSize, ZiIOBuf_Align, ZuStringT<"Zdbus.FrameTest">,
      Wire::MaxMessageSize>{
      ZmSingleton<Zi::IOBufBuiltinAllocator<Zi::IOBufType::App, ZiIOBuf,
        ZiIOBuf_DefltSize, Wire::MaxMessageSize, ZiIOBuf_Align,
        ZuStringT<"Zdbus.FrameTest">>>::instance()};
  ZuCheckRT(input->append(first.buf->cspan()));
  ZuCheckRT(input->append(second.buf->data(), Wire::MinHeaderSize));
  auto parsed = frame(input->cspan());
  ZuCheckRT(bool(parsed));
  if (!parsed) return;
  auto info = parsed.info;
  auto observed = splitFrame(input, info);
  ZuCheckRT(observed.ptr() == input.ptr());
  auto pending = frame(input->cspan());
  ZuCheckRT(pending.error == FrameError::NeedMore &&
    pending.info.total == second.buf->length &&
    pending.info.total > CxnDefault::RxBytes);
  ZuCheckRT(detachInput(input, pending.info.total));
  auto storage = input->data();
  unsigned capacity = input->size;
  ZuCheckRT(capacity >= pending.info.total && input.ptr() != observed.ptr());
  unsigned offset = Wire::MinHeaderSize;
  while (offset < second.buf->length) {
    unsigned n = second.buf->length - offset;
    if (n > CxnDefault::RxBytes) n = CxnDefault::RxBytes;
    ZuCheckRT(input->append(second.buf->data() + offset, n));
    ZuCheckRT(input->data() == storage && input->size == capacity);
    offset += n;
  }
  auto complete = frame(input->cspan());
  ZuCheckRT(bool(complete));
  if (complete) {
    auto handler = ZfDBUS::handler<Args>({complete.info.body(),
      complete.info.headers.signature, complete.info.bodyOffset,
      complete.info.order});
    ZuCheckRT(bool(handler));
    if (handler) {
      auto value = handler.ctor();
      ZuCheckRT(value.text == large.text && value.count == large.count);
    }
  }
  input = nullptr;
  ZuCheckRT(info.headers.member == head.member &&
    !memcmp(info.span().data(), first.buf->data(), first.buf->length));
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
  ZuTestCall(Zdbus_::frameRanges, true);
  ZuTestCall(Zdbus_::frameRanges, false);
  ZuTestCall(Zdbus_::frameAssembly);
  return 0;
}
