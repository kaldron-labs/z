//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZutcpOptions.hh>
#include <zlib/ZutcpMx.hh>

using namespace ZuTestUtil;
using Zutcp::TCPOptions;
using Zutcp::TCPOptionState;

static bool sameOptions(const TCPOptions &a, const TCPOptions &b)
{
  if (a.flags != b.flags || a.mss != b.mss || a.scale != b.scale ||
      a.sackCount != b.sackCount || a.tsval != b.tsval || a.tsecr != b.tsecr ||
      a.tfoLength != b.tfoLength || a.tfoLength > TCPOptions::MaxTFOCookie ||
      memcmp(a.tfoCookie, b.tfoCookie, a.tfoLength)) return false;
  for (unsigned i = 0; i < TCPOptions::MaxSACKBlocks; ++i)
    if (a.sack[i].left != b.sack[i].left || a.sack[i].right != b.sack[i].right) return false;
  return true;
}

static bool filled(const uint8_t *bytes, unsigned length, uint8_t value)
{
  for (unsigned i = 0; i < length; ++i) if (bytes[i] != value) return false;
  return true;
}

static void testSYNVector()
{
  ZuTestScope(testSYNVector);
  const uint8_t expected[] = {
    2,4,5,0xb4, 3,3,7, 4,2, 8,10,1,2,3,4,0,0,0,0, 0
  };
  TCPOptions offers{.flags = TCPOptions::MSS | TCPOptions::WS |
    TCPOptions::SACKPermitted | TCPOptions::TS, .mss = 1460, .scale = 7, .tsval = 0x01020304};
  uint8_t wire[sizeof(expected) + 2];
  memset(wire, 0xa5, sizeof(wire));
  int n = offers.encode(wire + 1, sizeof(expected), true);
  TCPOptions decoded;
  ZuCHECK(n == 20 && offers.encodedLength(true) == 20 &&
      !memcmp(wire + 1, expected, sizeof(expected)) && wire[0] == 0xa5 && wire[21] == 0xa5 &&
      decoded.decode({wire + 1, unsigned(n)}, true) == n && sameOptions(offers, decoded),
    "independent standard SYN option bytes and unaligned receive must preserve each field");
  TCPOptions zero{.flags = offers.flags};
  n = zero.encode(wire, sizeof(wire), true);
  ZuCHECK(n == 20 && decoded.decode({wire, unsigned(n)}, true) == n &&
      sameOptions(zero, decoded) && decoded.flags == offers.flags,
    "advertised MSS, WS and timestamp zero must remain present rather than absent");
  TCPOptions empty;
  memset(wire, 0xa5, sizeof(wire));
  ZuCHECK(!empty.encodedLength(true) && !empty.encode(wire, sizeof(wire), true) &&
      filled(wire, sizeof(wire), 0xa5) && !decoded.decode({}, true) && !decoded.flags,
    "absent options need no fabricated MSS, scale, clock value or output padding");
}

static void testACKVector()
{
  ZuTestScope(testACKVector);
  const uint8_t expected[] = {
    8,10,1,2,3,4,0xa1,0xa2,0xa3,0xa4, 5,26,
    0,0,0,150,0,0,0,200, 0,0,0,100,0,0,1,44, 0,0,1,144,0,0,1,244
  };
  TCPOptions ack{.flags = TCPOptions::TS, .sackCount = 3,
    .tsval = 0x01020304, .tsecr = 0xa1a2a3a4, .sack = {{150,200},{100,300},{400,500}}};
  uint8_t wire[40];
  TCPOptions decoded;
  int n = ack.encode(wire, sizeof(wire), false);
  ZuCHECK(n == 36 && !memcmp(wire, expected, sizeof(expected)) &&
      decoded.decode({wire, unsigned(n)}, false) == n && sameOptions(ack, decoded) &&
      decoded.dsack(50),
    "TS plus three ordered SACK blocks must retain the first duplicate block on the wire");
  ack.sackCount = 4;
  ack.sack[3] = {600,700};
  memset(wire, 0xa5, sizeof(wire));
  ZuCHECK(ack.encodedLength(false) == -1 && ack.encode(wire, sizeof(wire), false) == -1 &&
      filled(wire, sizeof(wire), 0xa5),
    "four blocks plus TS must fail transactionally without truncating the first or last block");
  ack.flags = 0;
  ack.tsval = ack.tsecr = 0;
  n = ack.encode(wire, sizeof(wire), false);
  ZuCHECK(n == 36 && wire[0] == 5 && wire[1] == 34 && !wire[34] && !wire[35] &&
      decoded.decode({wire, unsigned(n)}, false) == n && sameOptions(ack, decoded),
    "four explicit SACK blocks fit alone, including EOL padding, without reordering");
}

static void testTransactionalBounds()
{
  ZuTestScope(testTransactionalBounds);
  const uint8_t timestamp[] = {8,10,1,2,3,4,5,6,7,8};
  TCPOptions original{.flags = TCPOptions::MSS, .mss = 1234,
    .scale = 9, .sackCount = 1, .tsval = 23, .tsecr = 24, .sack = {{30,40}}};
  bool unchanged = true;
  for (unsigned n = 1; n < sizeof(timestamp); ++n) {
    auto decoded = original;
    unchanged &= decoded.decode({timestamp, n}, false) == -1 && sameOptions(original, decoded);
  }
  const uint8_t invalid[][4] = {{30,5,1,2}, {3,2,0,0}, {2,3,0,0}, {4,3,0,0}, {5,2,0,0}};
  for (const auto &bytes : invalid) {
    auto decoded = original;
    unchanged &= decoded.decode({bytes, sizeof(bytes)}, true) == -1 &&
      sameOptions(original, decoded);
  }
  uint8_t tooLong[41] = {};
  auto decoded = original;
  ZuCHECK(unchanged && decoded.decode({tooLong, sizeof(tooLong)}, true) == -1 &&
      sameOptions(original, decoded),
    "truncation, malformed known or unknown lengths and overlong input publish no partial state");
  TCPOptions timestampOnly{.flags = TCPOptions::TS};
  uint8_t wire[40];
  memset(wire, 0xa5, sizeof(wire));
  bool refused = timestampOnly.encode(wire, 11, false) == -1;
  TCPOptions bad{.flags = TCPOptions::WS, .scale = 15};
  refused &= bad.encode(wire, sizeof(wire), true) == -1;
  bad = {.flags = TCPOptions::MSS, .mss = 1460};
  refused &= bad.encode(wire, sizeof(wire), false) == -1;
  bad = {.sackCount = 1, .sack = {{1,2}}};
  refused &= bad.encode(wire, sizeof(wire), true) == -1;
  bad = {.sackCount = 5};
  refused &= bad.encode(wire, sizeof(wire), false) == -1;
  bad = {.flags = 0x80};
  refused &= bad.encode(wire, sizeof(wire), true) == -1;
  ZuCHECK(refused && filled(wire, sizeof(wire), 0xa5),
    "capacity, scale, SYN context and unknown-flag refusal must write nothing");
}

static void testOptionFraming()
{
  ZuTestScope(testOptionFraming);
  const uint8_t bytes[] = {
    1, 30,4,0xaa,0xbb, 3,3,6, 2,4,0,0, 2,4,5,0xb4, 0,8,1
  };
  TCPOptions decoded;
  ZuCHECK(decoded.decode({bytes, sizeof(bytes)}, true) == int(sizeof(bytes)) &&
      decoded.flags == (TCPOptions::MSS | TCPOptions::WS) && !decoded.mss && decoded.scale == 6,
    "NOP and unknown options skip; first known offer wins; EOL hides subsequent malformed bytes");
  const uint8_t offers[] = {2,4,5,0xb4, 3,3,7, 4,2, 8,10,0,0,0,0,0,0,0,0};
  ZuCHECK(decoded.decode({offers, sizeof(offers)}, false) == int(sizeof(offers)) &&
      decoded.flags == TCPOptions::TS && !decoded.mss && !decoded.scale && !decoded.sackCount,
    "off-SYN offers do not negotiate MSS, scale or SACK permission while TS remains packet data");
  const uint8_t sack[] = {5,10,0,0,0,1,0,0,0,2};
  ZuCHECK(decoded.decode({sack, sizeof(sack)}, true) == int(sizeof(sack)) && !decoded.sackCount,
    "a SACK block in SYN is ignored without treating its raw range as established state");
}

static void testOptionNegotiation()
{
  ZuTestScope(testOptionNegotiation);
  TCPOptions local{.flags = TCPOptions::WS | TCPOptions::SACKPermitted | TCPOptions::TS,
    .scale = 3};
  TCPOptions peer{.flags = TCPOptions::MSS | TCPOptions::TS};
  auto state = local.negotiate(peer);
  ZuCHECK(state.flags == (TCPOptions::MSS | TCPOptions::TS | TCPOptionState::SACKReceive) &&
      !state.peerMSS && !state.localScale && !state.peerScale,
    "peer MSS zero stays present; one-sided WS is absent; local SACK permission enables receive");
  local = {};
  peer = {.flags = TCPOptions::WS | TCPOptions::SACKPermitted | TCPOptions::TS, .scale = 7};
  state = local.negotiate(peer);
  ZuCHECK(state.flags == TCPOptionState::SACKSend && !state.localScale && !state.peerScale,
    "peer SACK permission enables send independently; WS and TS each require both offers");
  const uint8_t scale[] = {3,3,255};
  bool parsed = peer.decode({scale, sizeof(scale)}, true) == int(sizeof(scale));
  local = {.flags = TCPOptions::WS};
  state = local.negotiate(peer);
  ZuCHECK(parsed && peer.scale == 14 && state.flags == TCPOptions::WS &&
      !state.localScale && state.peerScale == 14,
    "received scale clamps to14 while explicitly negotiated local scale zero remains present");
}

static void testRawSACK()
{
  ZuTestScope(testRawSACK);
  const uint8_t bytes[] = {5,10,0,0,0,20,0,0,0,10, 5,10,0,0,0,1,0,0,0,2};
  TCPOptions decoded;
  uint8_t wire[40];
  memset(wire, 0xa5, sizeof(wire));
  ZuCHECK(decoded.decode({bytes, sizeof(bytes)}, false) == int(sizeof(bytes)) &&
      decoded.sackCount == 1 && decoded.sack[0].left == 20 && decoded.sack[0].right == 10 &&
      !decoded.sack[0].valid() && !decoded.dsack(30) &&
      decoded.encode(wire, sizeof(wire), false) == -1 && filled(wire, sizeof(wire), 0xa5),
    "invalid first block survives parsing and later duplicates but cannot be emitted");
  Zutcp::SACKBlock zero{7,7}, half{0,0x80000000}, wrapped{0xfffffff0,0x10};
  ZuCHECK(!zero.valid() && !half.valid() && wrapped.valid(),
    "block validity rejects zero and half-space spans while retaining valid modulo wrap");
}

static void testDSACK()
{
  ZuTestScope(testDSACK);
  TCPOptions duplicate{.sackCount = 1, .sack = {{100,150}}};
  ZuCHECK(duplicate.dsack(150) && duplicate.dsack(200) && !duplicate.dsack(100) &&
      !duplicate.dsack(125) && !duplicate.dsack(50),
    "duplicate classification uses this packet ACK and requires the entire first block below it");
  duplicate = {.sackCount = 2, .sack = {{150,200},{100,300}}};
  bool contained = duplicate.dsack(50);
  duplicate.sack[1] = {175,300};
  bool crossing = duplicate.dsack(50);
  duplicate.sack[1] = {100,175};
  bool outside = duplicate.dsack(50);
  duplicate.sack[0] = {100,300};
  duplicate.sack[1] = {150,200};
  ZuCHECK(contained && !crossing && !outside && !duplicate.dsack(50),
    "above-ACK duplicate evidence must be first and contained in the second ordered block");
  duplicate = {.sackCount = 1, .sack = {{0xfffffff0,0x10}}};
  bool belowWrap = duplicate.dsack(0x10);
  duplicate = {.sackCount = 2, .sack = {{0x10,0x20},{0,0x30}}};
  ZuCHECK(belowWrap && duplicate.dsack(0xfffffff0),
    "both below-ACK and above-ACK containment retain their meaning across modulo wrap");
  duplicate = {.sackCount = 2, .sack = {{0x50000000,0x50000001},{0x40000000,0xa0000000}}};
  bool ambiguous = duplicate.dsack(0);
  duplicate.sack[1] = {0x40000000,0x80000000};
  bool exactHalf = duplicate.dsack(0);
  duplicate = {.sackCount = 2, .sack = {{0x7fffffff,0x80000000},{1,0x80000000}}};
  bool firstHalf = duplicate.dsack(0);
  duplicate = {.sackCount = 1, .sack = {{0x80000000,0x80000001}}};
  ZuCHECK(!ambiguous && !firstHalf && !exactHalf && !duplicate.dsack(0),
    "a first or enclosing edge at or beyond packet-ACK half-space is ambiguous, never DSACK");
}

static void testTCPClock()
{
  ZuTestScope(testTCPClock);
  Zutcp::TCPClock clock;
  uint32_t stamp = 99;
  bool zero = clock.sample(ZuTime{0}, stamp) && !stamp;
  bool first = clock.sample(ZuTime{0, 1000000}, stamp) && stamp == 1;
  bool subTick = clock.sample(ZuTime{0, 1500000}, stamp) && stamp == 1;
  bool rolled = clock.sample(ZuTime{0}, stamp) && stamp == 1;
  bool resumed = clock.sample(ZuTime{0, 1000000}, stamp) && stamp == 2;
  ZuCHECK(zero && first && subTick && rolled && resumed,
    "known zero and millisecond ticks survive rollback without freezing future logical progress");
  Zutcp::TCPClock wrapping;
  bool high = wrapping.sample(ZuTime{4294967, 295000000}, stamp) && stamp == UINT32_MAX;
  bool low = wrapping.sample(ZuTime{4294967, 296000000}, stamp) && !stamp;
  bool invalid = !wrapping.sample(ZuTime{}, stamp) && !stamp;
  ZuCHECK(high && low && invalid && !wrapping.sample(ZuTime{-1}, stamp) && !stamp,
    "wire timestamp wrap keeps zero valid while null or negative clocks publish no sample");
}

static void testTimestampReuse()
{
  ZuTestScope(testTimestampReuse);
  Zutcp::ClosedTuple old{.tsRecentTime = ZuTime{10}, .txNext = 1000, .rxNext = 2000,
    .tsRecent = 100, .timestamps = true, .fin = true};
  Zutcp::TCPHeader syn{.seq = 1999, .flags = 0x02,
    .options = {.flags = TCPOptions::TS, .tsval = 101}};
  bool newerTS = Zutcp::reusePeerTuple(old, syn, true, ZuTime{11});
  syn.options.tsval = 100;
  bool equal = Zutcp::reusePeerTuple(old, syn, true, ZuTime{11});
  syn.seq = 2000;
  bool equalNewSeq = Zutcp::reusePeerTuple(old, syn, true, ZuTime{11});
  syn.options.tsval = 99;
  ZuCHECK(newerTS && !equal && equalNewSeq &&
      !Zutcp::reusePeerTuple(old, syn, true, ZuTime{11}),
    "retained timestamp history admits newer TS or equal TS with newer sequence, not older TS");
  syn.options = {};
  bool absentNewSeq = Zutcp::reusePeerTuple(old, syn, true, ZuTime{11});
  syn.seq = 1999;
  bool absentEqual = Zutcp::reusePeerTuple(old, syn, true, ZuTime{11});
  old.timestamps = false;
  syn.options = {.flags = TCPOptions::TS};
  bool newlyEnabled = Zutcp::reusePeerTuple(old, syn, true, ZuTime{11});
  bool disabled = Zutcp::reusePeerTuple(old, syn, false, ZuTime{11});
  old.fin = false;
  ZuCHECK(absentNewSeq && !absentEqual && newlyEnabled && !disabled &&
      !Zutcp::reusePeerTuple(old, syn, true, ZuTime{11}),
    "absent TS uses sequence fallback; no TS bypass for incomplete abort history");
  old.fin = old.timestamps = true;
  old.tsRecent = UINT32_MAX - 15;
  syn.options.tsval = 16;
  bool wrapped = Zutcp::reusePeerTuple(old, syn, true, ZuTime{11});
  old.tsRecent = 0;
  syn.options.tsval = 0x80000000;
  bool ambiguous = Zutcp::reusePeerTuple(old, syn, true, ZuTime{11});
  syn.options.tsval = 1;
  bool expired = Zutcp::reusePeerTuple(old, syn, true, ZuTime{24*24*60*60 + 11});
  syn.seq = 2000;
  ZuCHECK(wrapped && !ambiguous && !expired &&
      Zutcp::reusePeerTuple(old, syn, true, ZuTime{24*24*60*60 + 11}) &&
      !Zutcp::reuseSequence(999, old.txNext - 1),
    "timestamp wrap is serial; ambiguous or aged evidence supplies no bypass of sequence proof");
}

static void testTFOValues()
{
  ZuTestScope(testTFOValues);
  TCPOptions request{.flags = TCPOptions::TFO};
  uint8_t wire[42];
  memset(wire, 0xa5, sizeof(wire));
  const uint8_t expected[] = {34,2,0,0};
  TCPOptions parsed;
  int n = request.encode(wire + 1, sizeof(wire) - 2, true);
  ZuCHECK(n == 4 && !memcmp(wire + 1, expected, sizeof(expected)) &&
      wire[0] == 0xa5 && wire[5] == 0xa5 &&
      parsed.decode({wire + 1, unsigned(n)}, true) == n &&
      (parsed.flags & TCPOptions::TFO) && !parsed.tfoLength,
    "TFO request uses kind34 length2 and remains distinct from absent option");
  bool cookies = true;
  for (unsigned length = 4; length <= TCPOptions::MaxTFOCookie; length += 2) {
    TCPOptions value{.flags = TCPOptions::TFO, .tfoLength = uint8_t(length)};
    for (unsigned i = 0; i < length; ++i) value.tfoCookie[i] = uint8_t(0xd0 + i);
    memset(wire, 0xa5, sizeof(wire));
    n = value.encode(wire + 1, sizeof(wire) - 2, true);
    cookies &= n == int((length + 5) & ~unsigned(3)) && wire[1] == 34 &&
      wire[2] == length + 2 && !memcmp(wire + 3, value.tfoCookie, length) &&
      wire[0] == 0xa5 && wire[unsigned(n) + 1] == 0xa5 &&
      parsed.decode({wire + 1, unsigned(n)}, true) == n && sameOptions(value, parsed);
  }
  TCPOptions absent;
  bool empty = parsed.decode({}, true) == 0;
  ZuCHECK(cookies && empty && !(parsed.flags & TCPOptions::TFO) &&
      !parsed.tfoLength && request.negotiate(request).flags == absent.flags,
    "every supported opaque even cookie length round trips without becoming a negotiated flag");
}

static void testTFOFallback()
{
  ZuTestScope(testTFOFallback);
  bool ignored = true;
  for (unsigned length = 2; length <= TCPOptions::MaxBytes; ++length) {
    if (length == 2 || (length >= 6 && length <= 18 && !(length & 1))) continue;
    uint8_t wire[40];
    memset(wire, 0xa5, sizeof(wire));
    wire[0] = 34;
    wire[1] = uint8_t(length);
    TCPOptions parsed;
    ignored &= parsed.decode({wire, length}, true) == int(length) &&
      !(parsed.flags & TCPOptions::TFO) && !parsed.tfoLength;
  }
  const uint8_t invalidThenMSS[] = {34,4,0xaa,0xbb, 2,4,2,0x18};
  TCPOptions parsed;
  ignored &= parsed.decode({invalidThenMSS, sizeof(invalidThenMSS)}, true) ==
    int(sizeof(invalidThenMSS)) && parsed.flags == TCPOptions::MSS && parsed.mss == 536;
  const uint8_t offSYN[] = {34,6,1,2,3,4, 8,10,0,0,0,7,0,0,0,9};
  ignored &= parsed.decode({offSYN, sizeof(offSYN)}, false) == int(sizeof(offSYN)) &&
    parsed.flags == TCPOptions::TS && parsed.tsval == 7 && parsed.tsecr == 9;
  ZuCHECK(ignored,
    "invalid TFO cookie sizes and off-SYN offers preserve ordinary option processing");
  const uint8_t invalidThenValid[] = {34,4,0,0, 34,6,1,2,3,4};
  bool selected = parsed.decode({invalidThenValid, sizeof(invalidThenValid)}, true) ==
    int(sizeof(invalidThenValid)) && parsed.flags == TCPOptions::TFO &&
    parsed.tfoLength == 4 && !memcmp(parsed.tfoCookie, invalidThenValid + 6, 4);
  const uint8_t requestFirst[] = {34,2, 34,6,1,2,3,4};
  selected &= parsed.decode({requestFirst, sizeof(requestFirst)}, true) ==
    int(sizeof(requestFirst)) && parsed.flags == TCPOptions::TFO && !parsed.tfoLength;
  ZuCHECK(selected,
    "invalid candidates do not mask a later valid TFO option; first valid presence wins");
  TCPOptions saved{.flags = TCPOptions::TFO, .tfoLength = 16,
    .tfoCookie = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}};
  uint8_t complete[18] = {34,18};
  bool truncated = true;
  for (unsigned length = 1; length < sizeof(complete); ++length) {
    auto value = saved;
    truncated &= value.decode({complete, length}, true) == -1 && sameOptions(value, saved);
  }
  const uint8_t malformed[][2] = {{34,0}, {34,1}};
  for (const auto &wire : malformed) {
    auto value = saved;
    truncated &= value.decode({wire, sizeof(wire)}, true) == -1 && sameOptions(value, saved);
  }
  ZuCHECK(truncated,
    "structurally truncated TFO still fails transactionally without publishing partial metadata");
}

static void testTFOBudget()
{
  ZuTestScope(testTFOBudget);
  const uint8_t expected[40] = {
    2,4,5,0xb4, 3,3,7, 4,2, 8,10,1,2,3,4,0xaa,0xbb,0xcc,0xdd,
    34,18,0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
    0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf, 0,0,0
  };
  TCPOptions full{.flags = TCPOptions::MSS | TCPOptions::WS | TCPOptions::SACKPermitted |
    TCPOptions::TS | TCPOptions::TFO, .mss = 1460, .scale = 7,
    .tsval = 0x01020304, .tsecr = 0xaabbccdd, .tfoLength = 16,
    .tfoCookie = {0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
      0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf}};
  uint8_t wire[42];
  memset(wire, 0xa5, sizeof(wire));
  TCPOptions parsed;
  int n = full.encode(wire + 1, 40, true);
  ZuCHECK(n == 40 && full.encodedLength(true) == 40 &&
      !memcmp(wire + 1, expected, sizeof(expected)) && wire[0] == 0xa5 && wire[41] == 0xa5 &&
      parsed.decode({wire + 1, 40}, true) == 40 && sameOptions(full, parsed),
    "MSS WS SACK-permitted TS and maximum TFO cookie occupy exactly the padded40-byte budget");
  memset(wire, 0xa5, sizeof(wire));
  bool refused = full.encode(wire, 39, true) == -1 &&
    full.encode(wire, 40, false) == -1 && filled(wire, sizeof(wire), 0xa5);
  for (unsigned length = 1; length <= 18; ++length) {
    if (length >= 4 && length <= 16 && !(length & 1)) continue;
    TCPOptions value{.flags = TCPOptions::TFO, .tfoLength = uint8_t(length)};
    refused &= value.encodedLength(true) == -1 && value.encode(wire, 40, true) == -1 &&
      filled(wire, sizeof(wire), 0xa5);
  }
  ZuCHECK(refused,
    "invalid local cookies, off-SYN output and insufficient option capacity write nothing");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testTCPClock);
  ZuTestCall(testTimestampReuse);
  ZuTestCall(testSYNVector);
  ZuTestCall(testACKVector);
  ZuTestCall(testTransactionalBounds);
  ZuTestCall(testOptionFraming);
  ZuTestCall(testOptionNegotiation);
  ZuTestCall(testRawSACK);
  ZuTestCall(testDSACK);
  ZuTestCall(testTFOValues);
  ZuTestCall(testTFOFallback);
  ZuTestCall(testTFOBudget);
}
