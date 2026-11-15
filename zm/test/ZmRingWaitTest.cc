//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Deterministic LL0 spin, registration and expected-value wait handoffs.

#include "ZmRingWaitTest.hh"
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmThread.hh>

using namespace ZuTestUtil;

template <bool MW, bool MR>
static void readerWake(unsigned phase, bool eof)
{
  ZuTestScopeRT(readerWake);
  using Ring = WaitRing<MW, MR>;
  WaitCounts head, tail;
  Ring storage{ZmRingParams{}.size(256).spin(phase == WaitPhase::CAS ? 0 : 16)
    .timeout(50)};
  storage.headBlocker().counts = &head;
  storage.tailBlocker().counts = &tail;
  ZuCheck(storage.open(0) == Zu::OK);
  Ring writer{storage}, reader{storage};
  ZuCheck(writer.open(Ring::Write) == Zu::OK);
  ZuCheck(reader.open(Ring::Read) == Zu::OK && reader.attach() == Zu::OK);
  auto &gate = waitGate(reader, phase);
  gate.enable();
  bool received = false, ended = false;
  ZmThread thread{[&reader, &received, &ended] {
    auto p = reader.shift();
    if (p) { received = *p == 17; reader.shift2(); }
    else ended = reader.readStatus() == Zu::EndOfFile;
  }};
  bool reached = gate.wait();
  bool registered = readerWaiting(reader);
  bool sent = true;
  if (eof) writer.eof();
  else sent = pushValue(writer, 17);
  auto wakes = unsigned(head.wakes);
  gate.proceed();
  thread.join();
  if (phase == WaitPhase::Spin)
    log_("reader spin MW=", MW, " MR=", MR, " EOF=", eof,
      " registered=", registered, " wakes=", wakes, " blocker_entries=", unsigned(head.waits));
  ZuCHECK(reached && sent && (eof ? ended && !received : received && !ended),
    "publication/EOF at a controlled wait boundary completes without losing progress");
  ZuCHECK(registered == (phase == WaitPhase::Park) &&
      wakes == unsigned(phase == WaitPhase::Park) &&
      unsigned(head.waits) == unsigned(phase == WaitPhase::Park),
    "spin/pre-CAS publication avoids wake and blocker entry; registered publication wakes once");
  reader.detach();
}

template <bool MW, bool MR>
static void writerWake(unsigned phase)
{
  ZuTestScopeRT(writerWake);
  using Ring = WaitRing<MW, MR>;
  WaitCounts head, tail;
  Ring storage{ZmRingParams{}.size(256).spin(phase == WaitPhase::CAS ? 0 : 16)
    .timeout(50)};
  storage.headBlocker().counts = &head;
  storage.tailBlocker().counts = &tail;
  ZuCheck(storage.open(0) == Zu::OK);
  Ring writer{storage}, reader{storage};
  ZuCheck(writer.open(Ring::Write) == Zu::OK);
  ZuCheck(reader.open(Ring::Read) == Zu::OK && reader.attach() == Zu::OK);
  unsigned count = 0;
  while (pushValue(writer, 31)) ++count;
  ZuCheck(count > 1 && !unsigned(tail.waits));
  auto &gate = waitGate(writer, phase);
  gate.enable();
  bool sent = false;
  ZmThread thread{[&writer, &sent] { sent = pushValue(writer, 43, true); }};
  bool reached = gate.wait();
  bool freed = shiftValue(reader, 31);
  auto wakes = unsigned(tail.wakes);
  gate.proceed();
  thread.join();
  if (phase == WaitPhase::Spin)
    log_("writer spin MW=", MW, " MR=", MR,
      " wakes=", wakes, " blocker_entries=", unsigned(tail.waits));
  bool ordered = true;
  for (unsigned i = 1; i < count; ++i) ordered &= shiftValue(reader, 31);
  ordered &= shiftValue(reader, 43);
  ZuCHECK(reached && freed && sent && ordered && !reader.tryShift(),
    "full writer observes freed capacity and preserves every queued value");
  ZuCHECK(wakes == unsigned(phase == WaitPhase::Park) &&
      unsigned(tail.waits) == unsigned(phase == WaitPhase::Park),
    "full-writer spin/pre-CAS avoids wake and blocker entry; registered handoff remains live");
  reader.detach();
}

template <bool MW, bool MR>
static void waitTimeout()
{
  ZuTestScopeRT(waitTimeout);
  using Ring = WaitRing<MW, MR>;
  WaitCounts head, tail;
  Ring storage{ZmRingParams{}.size(256).spin(16).timeout(1)};
  storage.headBlocker().counts = &head;
  storage.tailBlocker().counts = &tail;
  ZuCheck(storage.open(0) == Zu::OK);
  Ring writer{storage}, reader{storage};
  ZuCheck(writer.open(Ring::Write) == Zu::OK);
  ZuCheck(reader.open(Ring::Read) == Zu::OK && reader.attach() == Zu::OK);
  ZuCheck(!reader.tryShift() && !unsigned(head.waits));
  ZuCheck(!reader.shift() && !reader.readStatus() && unsigned(head.waits) == 1);
  ZuCHECK(readerWaiting(reader) && pushValue(writer, 11) && shiftValue(reader, 11),
    "timeout keeps the conservative shared wait bit; next publication is still consumable");
  unsigned count = 0;
  while (pushValue(writer, 23)) ++count;
  ZuCheck(count && !unsigned(tail.waits));
  ZuCheck(!writer.push() && unsigned(tail.waits) == 1);
  bool ordered = true;
  while (count--) ordered &= shiftValue(reader, 23);
  ZuCHECK(ordered && pushValue(writer, 29) && shiftValue(reader, 29),
    "full-writer timeout neither consumes space nor loses a later returned slot");
  writer.eof();
  ZuCheck(!reader.shift() && reader.readStatus() == Zu::EndOfFile);
  reader.detach();
}

template <bool MW>
static void sharedReaders(bool timeout)
{
  ZuTestScopeRT(sharedReaders);
  using Ring = WaitRing<MW, true>;
  WaitCounts head, tail;
  Ring storage{ZmRingParams{}.size(256).spin(16).timeout(1)};
  storage.headBlocker().counts = &head;
  storage.tailBlocker().counts = &tail;
  ZuCheck(storage.open(0) == Zu::OK);
  Ring writer{storage}, first{storage}, second{storage};
  ZuCheck(writer.open(Ring::Write) == Zu::OK);
  ZuCheck(first.open(Ring::Read) == Zu::OK && first.attach() == Zu::OK);
  ZuCheck(second.open(Ring::Read) == Zu::OK && second.attach() == Zu::OK);
  first.bp_waitPark.enable();
  bool a = false, b = false;
  ZmThread readerA{[&first, &a] { a = shiftValue(first, 47, true); }};
  bool reachedA = first.bp_waitPark.wait();
  second.bp_waitSpin.enable();
  ZmThread readerB{[&second, &b] { b = shiftValue(second, 47, true); }};
  bool reachedB = second.bp_waitSpin.wait();
  bool registered = readerWaiting(second);
  if (timeout) {
    second.bp_waitSpin.proceed();
    readerB.join();
    registered &= readerWaiting(first) && !b;
  }
  bool sent = pushValue(writer, 47);
  auto wakes = unsigned(head.wakes);
  first.bp_waitPark.proceed();
  if (!timeout) second.bp_waitSpin.proceed();
  readerA.join();
  if (!timeout) readerB.join();
  else b = shiftValue(second, 47);
  ZuCHECK(reachedA && reachedB && registered && sent && wakes == 1 && a && b,
    "a second spinner or timed-out reader never clears another reader's registered wake");
  first.detach(); second.detach();
}

template <bool MR>
static void sharedWriters()
{
  ZuTestScopeRT(sharedWriters);
  using Ring = WaitRing<true, MR>;
  WaitCounts head, tail;
  Ring storage{ZmRingParams{}.size(256).spin(16).timeout(50)};
  storage.headBlocker().counts = &head;
  storage.tailBlocker().counts = &tail;
  ZuCheck(storage.open(0) == Zu::OK);
  Ring first{storage}, second{storage}, reader{storage};
  ZuCheck(first.open(Ring::Write) == Zu::OK && second.open(Ring::Write) == Zu::OK);
  ZuCheck(reader.open(Ring::Read) == Zu::OK && reader.attach() == Zu::OK);
  unsigned count = 0;
  while (pushValue(first, 53)) ++count;
  ZuCheck(count > 1);
  first.bp_waitPark.enable();
  bool a = false, b = false;
  ZmThread writerA{[&first, &a] { a = pushValue(first, 59, true); }};
  bool reachedA = first.bp_waitPark.wait();
  second.bp_waitSpin.enable();
  ZmThread writerB{[&second, &b] { b = pushValue(second, 61, true); }};
  bool reachedB = second.bp_waitSpin.wait();
  bool freed = shiftValue(reader, 53) && shiftValue(reader, 53);
  auto wakes = unsigned(tail.wakes);
  first.bp_waitPark.proceed(); second.bp_waitSpin.proceed();
  writerA.join(); writerB.join();
  bool ordered = true;
  for (unsigned i = 2; i < count; ++i) ordered &= shiftValue(reader, 53);
  unsigned found = 0;
  for (unsigned i = 0; i < 2; ++i) {
    if (auto p = reader.tryShift()) {
      found |= *p == 59 ? 1 : *p == 61 ? 2 : 0;
      reader.shift2();
    }
  }
  ZuCHECK(reachedA && reachedB && freed && wakes == 1 && a && b && ordered && found == 3,
    "multiple full writers retain the existing wake and each consume a distinct free slot");
  reader.detach();
}

template <bool MW, bool MR>
static void mode()
{
  ZuTestScopeRT(mode);
  for (unsigned phase = WaitPhase::Spin; phase <= WaitPhase::Park; ++phase) {
    ZuTestCall((readerWake<MW, MR>), phase, false);
    ZuTestCall((readerWake<MW, MR>), phase, true);
    ZuTestCall((writerWake<MW, MR>), phase);
  }
  ZuTestCall((waitTimeout<MW, MR>));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall((mode<false, false>));
  ZuTestCall((mode<false, true>));
  ZuTestCall((mode<true, false>));
  ZuTestCall((mode<true, true>));
  ZuTestCall(sharedReaders<false>, false);
  ZuTestCall(sharedReaders<false>, true);
  ZuTestCall(sharedReaders<true>, false);
  ZuTestCall(sharedReaders<true>, true);
  ZuTestCall(sharedWriters<false>);
  ZuTestCall(sharedWriters<true>);
}
