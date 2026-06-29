//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// compile-time Aho-Corasick automaton FSM
// - high-speed fixed multi-string matching
// - simpler and faster than compile-time regex matching
// - typical use cases: parsing JSON fields, HTTP headers, ASN OIDs, etc.

// Historical note - Aho-Corasick is from 1975, yet remains state of the art
// - https://en.wikipedia.org/wiki/Aho%E2%80%93Corasick_algorithm
// - building the automaton at compile-time and evaluating the
//   match using constant-evaluation is only possible with modern C++23
// - Commentz-Walter is significantly more complex and actually slower
//   https://archive.org/details/Commentz-walterAnyBetterThanAho-corasickForPeptideIdentification

// Example usage:
//
// constexpr auto matcher = ZuMatcher<"foo", "foh", "bar", "baz">();
//
// matcher.match("fo") < 0
// matcher.match("foo") == 0
// matcher.match("foo!") == 0
// matcher.match("foh") == 1
// matcher.match("baz") == 3
// matcher.match("xbaz") == -1 

#ifndef ZuMatcher_HH
#define ZuMatcher_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuTL.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuSeq.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnroll.hh>

// gcc outperforms clang generally on this code (as of clang 18.1 and gcc 14.2)
// - for gcc, compile-time switching is 2x faster than in-memory LUT
// - for clang, compile-time switching is 10-15% faster
#define ZuMatcher_Switch 1

// if ZuMatcher_Switch is 0, by default with -g (which is -g2), the compiler
// will include debugging information for the built automaton even though
// this is not used at run-time; the debug information can be large and there
// is no simple way to prevent the compiler from generating it other
// than isolating the use of ZuMatcher to a single .cc source file and
// building that file with -g0 or -g1
// - this problem exists for all compile-time programming and is
//   significantly more challenging for complex libraries such as ctre

namespace Zu_::AhoCorasick {

// a node in the automaton (i.e. a state in the FSM)
// - each node contains a map from a matched character to the
//   corresponding next node in the automaton
template <unsigned Begin, unsigned End, unsigned NKeys>
struct Node {
// compile-time switching uses a simpler Node with a flat next_[] array
// that consumes more memory but only exists as a compile-time temporary;
// this version also builds the chars[] array that is used to construct the
// ZuSeq<> compile-time sequence of chars for switching
#if ZuMatcher_Switch
  // next index with 0 as null sentinel value
  struct Next { uint16_t v = 0; };

  static constexpr uint8_t Width = End - Begin;

  Next next_[Width];
  ZuArray<uint8_t, NKeys> chars;
  uint16_t fail = 0;	// index of node to fail back to - used by find()
  uint8_t output = 0;	// index of matched key + 1, 0 if no match

  constexpr Node() noexcept { }
  constexpr ~Node() noexcept { }

  // given a character c, look up the next node
  // - 0 is a sentinel value for null (no next node)
  constexpr uint16_t next(uint8_t c) const { return next_[c - Begin].v; }
  // set the next destination for a character c to the specified node j
  consteval void next(uint8_t c, uint16_t j) {
    if (!next_[c - Begin].v) {
      next_[c - Begin].v = j;
      chars.push(c);
    }
  }
#else
  // node index with 0 as null sentinel value
  struct KeyNext { uint16_t v = 0; };
  // key next index with 0 as null sentinel value
  struct Next { uint8_t v = 0; };

  static constexpr uint8_t Width = End - Begin;

  // if the number of keys is fewer than 14, nibbles can be used instead
  // of bytes for the next_[] array that is indexed by character value
  Next next_[NKeys > 14 ? Width : ((Width + 1)>>1)];
  KeyNext keyNext_[NKeys];
  uint16_t fail = 0;	// index of node to fail back to - used by find()
  uint8_t output = 0;	// index of matched key + 1, 0 if no match

  constexpr Node() noexcept { }
  constexpr ~Node() noexcept { }

  // given a character c, look up the next node
  // - 0 is a sentinel value for null (no next node)
  // - 1 is a sentinel value for the first node
  // - 2+ indirect via keyNext_[], which contains the actual next node
  // - this exploits the fact that there can only be as many next
  //   destinations in each node as there are keys in the matcher
  constexpr uint16_t next(uint8_t c) const {
    uint8_t k;
    if constexpr (NKeys > 14) {
      k = next_[c - Begin].v;
    } else {
      c -= Begin;
      k = next_[c>>1].v;
      k = (c & 1) ? (k>>4) : (k & 0x0f);
    }
    return k <= 1 ? k : keyNext_[k - 2].v;
  }
  // set the next destination for a character c to the first node
  consteval void next0(uint8_t c) {
    if constexpr (NKeys > 14) {
      next_[c - Begin].v = 1;
    } else {
      c -= Begin;
      auto v = next_[c>>1].v;
      next_[c>>1].v = (c & 1) ? (0x10 | (v & 0x0f)) : ((v & 0xf0) | 0x01);
    }
  }
  // set the next destination for a character c to the specified node j,
  // on behalf of the specified key index k
  // - if the destination was already set by another key, re-use that
  consteval void next(uint8_t c, int8_t k, uint16_t j) {
    uint8_t k_;
    if constexpr (NKeys > 14) {
      k_ = next_[c - Begin].v;
      if (k_ <= 1) next_[c - Begin].v = k_ = (k + 2);
    } else {
      c -= Begin;
      auto v = next_[c>>1].v;
      k_ = (c & 1) ? (v>>4) : (v & 0x0f);
      if (k_ <= 1) {
	k_ = k += 2;
	next_[c>>1].v =
	  (c & 1) ? ((k<<4) | (v & 0x0f)) : ((v & 0xf0) | (k & 0x0f));
      }
    }
    keyNext_[k_ - 2].v = j;
  }
#endif

  struct Traits : public ZuBaseTraits<Node> { enum { IsPOD = 1 }; };
  friend Traits ZuTraitsType(Node *);
};

// compile-time calculation of min/max characters present in an array of keys
template <typename Key>
constexpr uint8_t min_() {
  auto key = Key{}();
  unsigned n = key.length();
  uint8_t m = 0xff;
  for (unsigned i = 0; i < n; i++) {
    uint8_t m_ = key[i];
    if (m_ < m) m = m_;
  }
  return m;
}
template <typename Key>
constexpr uint8_t max_() {
  auto key = Key{}();
  unsigned n = key.length();
  uint8_t m = 0;
  for (unsigned i = 0; i < n; i++) {
    uint8_t m_ = key[i];
    if (m_ > m) m = m_;
  }
  return m;
}
// Min and Max overload operator & to use with a parameter pack fold expression
struct Min {
  unsigned i;
  constexpr Min(unsigned i_) noexcept : i(i_) { }
  constexpr Min operator &(Min r) const { return i < r.i ? *this : r; }
};
struct Max {
  unsigned i;
  constexpr Max(unsigned i_) noexcept : i(i_) { }
  constexpr Max operator &(Max r) const { return i > r.i ? *this : r; }
};

// compile-time calculation of total length of all keys
// - which is used to calculate an upper limit on the required #nodes
template <typename Keys> struct Total;
template <typename ...Key> struct Total<ZuTypeList<Key...>> {
  static constexpr unsigned length() { return (...+ Key{}().length()); }
};

// compile-time calculation of minimum character and maximum character
// - which is used to minimize range of the alphabet
template <typename Keys> struct MinMax;
template <typename ...Key> struct MinMax<ZuTypeList<Key...>> {
  static constexpr unsigned min() { return (...& Min(min_<Key>())).i; }
  static constexpr unsigned max() { return (...& Max(max_<Key>())).i; }
};

// compile-time automaton builder
template <typename, unsigned> struct Automaton;
template <
  typename Keys,
  unsigned N_ =
// gcc has trouble with compile-time evaluation of Automaton{}.nodes.length();
// if compile-time switching, there is no run-time benefit in reducing the
// size of the compile-time temporary
#if ZuMatcher_Switch || (defined(__GNUC__) && !defined(__llvm__))
    Total<Keys>::length() + 1
#else
// - each automaton is built twice to minimize memory, once as a compile-time
//   temporary using an upper bound on the number of nodes, then again with
//   the minimum number of nodes actually needed for the specific keys
    Automaton<Keys, Total<Keys>::length() + 1>{}.nodes.length()
#endif
  >
struct Automaton {
  using MinMax_ = MinMax<Keys>;
  static constexpr unsigned N = N_;
  static constexpr unsigned Begin = MinMax_::min();
  static constexpr unsigned End = MinMax_::max() + 1;
  static constexpr unsigned Width = End - Begin;

  using Node_ = Node<Begin, End, Keys::N>;

  ZuArray<Node_, N> nodes;

#ifdef __GNUC__
__attribute__((no_instrument_function))
#endif
  consteval Automaton() {
    nodes.push(Node_());
    ZuUnroll::all<Keys>([this]<typename Key>() {
      constexpr unsigned J = ZuTypeIndex<Key, Keys>{};
      unsigned current = 0;
      unsigned n = Key{}().length();
      for (unsigned i = 0; i < n; i++) {
	uint8_t c = Key{}()[i];
	if (ZuUnlikely(c < Begin || c >= End)) return;
	if (!nodes[current].next(c)) {
#if ZuMatcher_Switch
	  nodes[current].next(c, nodes.length() + 1);
#else
	  nodes[current].next(c, J, nodes.length() + 1);
#endif
	  nodes.push(Node_());
	}
	current = nodes[current].next(c) - 1;
      }
      nodes[current].output = J + 1;
    });

    ZuArray<unsigned, N> queue{};
    unsigned front = 0, back = 0;

    for (uint8_t i = Begin; i < End; ++i) {
      auto next = nodes[0].next(i);
      if (next) {
	nodes[next - 1].fail = 1;
	queue[back++] = next - 1;
      } else {
#if ZuMatcher_Switch
	nodes[0].next(i, 1);
#else
	nodes[0].next0(i);
#endif
      }
    }

    while (front < back) {
      unsigned u = queue[front++];
      for (uint8_t i = Begin; i < End; ++i) {
	auto v = nodes[u].next(i);
	if (!v--) continue;

	auto f = nodes[u].fail - 1;
	while (!nodes[f].next(i)) { f = nodes[f].fail - 1; }
	f = (nodes[v].fail = nodes[f].next(i)) - 1;

	auto output = nodes[f].output;
	if (output && !nodes[v].output) nodes[v].output = output;

	queue[back++] = v;
      }
    }
  }
  constexpr ~Automaton() { }
};

template <typename Keys>
struct Matcher {
  using Automaton_ = Automaton<Keys>;

  static constexpr Automaton_ automaton = Automaton_();

  static consteval Keys keys() { return Keys{}; };

  ZuInline static consteval auto size() { return automaton.nodes.size(); }
  ZuInline static consteval auto length() { return automaton.nodes.length(); }

  ZuInline static consteval unsigned begin() { return automaton.Begin; }
  ZuInline static consteval unsigned end() { return automaton.End; }
  ZuInline static consteval unsigned width() { return automaton.Width; }

  ZuInline static constexpr uint16_t next_(uint16_t index, uint8_t c) {
    if (ZuUnlikely(c < begin() || c >= end())) return !index;
#if ZuMatcher_Switch
    auto next = ZuSwitch::dispatch<length()>(index, [c](auto I) {
      constexpr uint16_t Index = I;
      using Chars = ZuStringSeq<automaton.nodes[Index].chars>;
      return ZuSwitch::dispatch<Chars>(c, [](auto Char) {
	constexpr uint16_t Next = automaton.nodes[Index].next(Char);
	return Next;
      }, uint16_t(!Index));
    });
#else
    auto next = automaton.nodes[index].next(c);
#endif
    return next;
  }
  ZuInline static constexpr uint8_t output_(uint16_t index) {
#if ZuMatcher_Switch
    return ZuSwitch::dispatch<length()>(index, [](auto I) {
      constexpr uint16_t Index = I;
      constexpr uint8_t Output = automaton.nodes[Index].output;
      return Output;
    });
#else
    return automaton.nodes[index].output;
#endif
  }
  ZuInline static constexpr uint16_t fail_(uint16_t index) {
#if ZuMatcher_Switch
    return ZuSwitch::dispatch<length()>(index, [](auto I) {
      constexpr uint16_t Index = I;
      constexpr uint16_t Fail = automaton.nodes[Index].fail;
      return Fail;
    });
#else
    return automaton.nodes[index].fail;
#endif
  }

  // prefix match of keys at the beginning of the passed string
  // - returns the index of the matched key or -1 if no match
  static constexpr int match(ZuCSpan s) {
    if constexpr (Keys::N == 1)
      return s.template match<ZuType<0, Keys>{}()>() ? 0 : -1;
    uint16_t current = 0;
    for (unsigned i = 0, n = s.length(); i < n; i++) {
      uint8_t c = s[i];
      auto next = next_(current, c);
      if (next <= 1) break;
      current = next - 1;
    }
    return int(output_(current)) - 1;
  }

  // substring match of keys anywhere in the passed string
  // - returns {offset, index} of the matched key or {-1, -1} if no match
  static constexpr ZuTuple<int, int> find(ZuCSpan s) {
    if constexpr (Keys::N == 1) {
      int offset = s.template find<ZuType<0, Keys>{}()>();
      return {offset, offset >= 0 ? 0 : -1};
    }
    uint16_t current = 0;
    for (unsigned i = 0, n = s.length(); i < n; i++) {
      uint8_t c = s[i];
      auto next = next_(current, c);
      while (!next) {
	current = fail_(current) - 1;
	next = next_(current, c);
      }
      current = next - 1;
      int output = int(output_(current)) - 1;
      if (output >= 0) {
	return ZuSwitch::dispatch<Keys::N>(output, [i](auto Output) {
	  return ZuTuple<int, int>{
	    i - (ZuType<Output, Keys>{}().length() - 1),
	    Output
	  };
	});
      }
    }
    return {-1, -1};
  }
};

} // Zu_::AhoCorasick

// ZuMatcher<"a", "b", ...>()
template <ZuString ...Keys>
constexpr auto ZuMatcher() {
  return Zu_::AhoCorasick::Matcher<ZuStringTL<Keys...>>{};
}
// ZuMatcher<ZuFieldProp::JSON::GetIDs<Order>>;
template <typename Keys>
constexpr auto ZuMatcher() {
  return Zu_::AhoCorasick::Matcher<Keys>{};
}

#endif /* ZuMatcher_HH */
