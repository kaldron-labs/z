//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// compiled telKey component filters

#ifndef ZtcFilter_HH
#define ZtcFilter_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZuPercent.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/Zu_aton.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZtcLink.hh>
#include <zlib/ZtcQueue.hh>

#include <zlib/ZtcMsg_fbs.h>

namespace Ztc {
namespace Filter_ {

struct KeyPolicy : public ZuPercent::NoPlus, public ZuPercent::NoTerm {
  static constexpr bool esc(uint8_t c) {
    return c <= 0x20 || c >= 0x7f ||
      c == '%' || c == ':' || c == '*';
  }
};

using Codec = ZuPercent::Codec<KeyPolicy>;
using Text = ZtString<ZtStringHeapID<"Ztc.App.Filter">>;

namespace Mode {
  enum { All, Exact, Prefix };
}

struct Component {
  unsigned	offset = 0;
  unsigned	length = 0;
  uint64_t	value = 0;
  uint8_t	mode = Mode::All;

  ZuCSpan text(const Text &text_) const {
    return {text_.data() + offset, length};
  }
};

class Filter {
public:
  bool compile(fbs::Group group, ZuCSpan input, unsigned maxLength) {
    if (input.length() > maxLength) return false;
    m_text.length(0);
    m_keyLength = 0;
    m_c0 = {};
    m_c1 = {};
    m_c2 = {};

    ZuCSpan raw{input};
    int64_t first = raw.find(
      [](char c) { return c == ':'; });
    int64_t second = -1;
    if (first >= 0) {
      ZuCSpan tail = raw;
      tail.offset(unsigned(first + 1));
      second = tail.find(
	[](char c) { return c == ':'; });
      if (second >= 0) second += first + 1;
    }
    if (second >= 0) {
      ZuCSpan tail = raw;
      tail.offset(unsigned(second + 1));
      if (tail.find([](char c) { return c == ':'; }) >= 0)
	return false;
    }

    switch (group) {
      case fbs::Group::Heap:
      case fbs::Group::Hash:
      case fbs::Group::Mx:
      case fbs::Group::DB:
      case fbs::Group::App:
	if (first >= 0) return false;
	if (!component(m_c0, raw, 0, raw.length(), true)) return false;
	break;
      case fbs::Group::Alert:
	if (first >= 0) return false;
	if (!component(m_c0, raw, 0, raw.length(), false) ||
	    m_c0.mode != Mode::All) return false;
	break;
      case fbs::Group::Thread:
	if (first >= 0) return false;
	if (!component(m_c0, raw, 0, raw.length(), false)) return false;
	if (!number(m_c0, raw)) return false;
	break;
      case fbs::Group::Hub:
	if (first < 0 || second >= 0) return false;
	if (!component(m_c0, raw, 0, unsigned(first), false) ||
	    !component(
	      m_c1, raw, unsigned(first + 1),
	      raw.length() - unsigned(first + 1), true))
	  return false;
	if (!linkType(m_c0, raw)) return false;
	break;
      case fbs::Group::Queue:
	if (first < 0) return false;
	if (!component(m_c0, raw, 0, unsigned(first), false))
	  return false;
	if (!queueType(m_c0, raw)) return false;
	if (second < 0) {
	  if (!component(
		m_c2, raw, unsigned(first + 1),
		raw.length() - unsigned(first + 1), true))
	    return false;
	} else {
	  if (!component(
		m_c1, raw, unsigned(first + 1),
		unsigned(second - first - 1), true) ||
	      !component(
		m_c2, raw, unsigned(second + 1),
		raw.length() - unsigned(second + 1), true))
	    return false;
	}
	break;
      default:
	return false;
    }
    return canonical(group, raw, maxLength);
  }

  ZuCSpan key() const { return {m_text.data(), m_keyLength}; }
  bool all() const { return m_all; }

  uint8_t mode0() const { return m_c0.mode; }
  uint8_t mode1() const { return m_c1.mode; }
  uint8_t mode2() const { return m_c2.mode; }
  ZuCSpan text0() const { return m_c0.text(m_text); }
  ZuCSpan text1() const { return m_c1.text(m_text); }
  ZuCSpan text2() const { return m_c2.text(m_text); }
  uint64_t value0() const { return m_c0.value; }

  bool stringExact(ZuCSpan value) const {
    return value.exact(m_c0.text(m_text));
  }
  bool stringPrefix(ZuCSpan value) const {
    return value.match(m_c0.text(m_text));
  }

  bool string(ZuCSpan value) const { return match(m_c0, value); }
  bool thread(uint64_t value) const {
    return m_c0.mode == Mode::All || m_c0.value == value;
  }
  bool hub(LinkType::T type, ZuCSpan id) const {
    return enumeration(m_c0, type) && match(m_c1, id);
  }
  bool queue(QueueType::T type, ZuCSpan owner, ZuCSpan id) const {
    return enumeration(m_c0, type) &&
      match(m_c1, owner) && match(m_c2, id);
  }

private:
  bool component(
      Component &out, ZuCSpan input,
      unsigned offset, unsigned length, bool prefix) {
    out.offset = offset;
    if (!length) return true;

    ZuCSpan raw{input.data() + offset, length};
    if (length == 1 && raw[0] == '*') return true;

    int64_t star = raw.find(
      [](char c) { return c == '*'; });
    if (star >= 0) {
      if (!prefix || unsigned(star) != length - 1) return false;
      out.mode = Mode::Prefix;
      --length;
      raw = {raw.data(), length};
    } else {
      out.mode = Mode::Exact;
    }

    for (unsigned i = 0; i < length; ++i) {
      uint8_t c = uint8_t(raw[i]);
      if (c != '%') {
	if (KeyPolicy::esc(c)) return false;
	continue;
      }
      if (i + 2 >= length) return false;
      char h = raw[i + 1], l = raw[i + 2];
      int hi = ZuPercent::hex(h), lo = ZuPercent::hex(l);
      if (hi < 0 || lo < 0 ||
	  (h >= 'a' && h <= 'f') || (l >= 'a' && l <= 'f'))
	return false;
      if (!KeyPolicy::esc(uint8_t((hi<<4) | lo))) return false;
      i += 2;
    }

    out.length = length;
    return true;
  }

  bool number(Component &component_, ZuCSpan input) {
    if (component_.mode == Mode::All) return true;
    if (component_.mode != Mode::Exact || !component_.length) return false;
    ZuCSpan text_{input.data() + component_.offset, component_.length};
    if (text_.length() > 1 && text_[0] == '0') return false;
    uint64_t value = 0;
    unsigned n = Zu_atou(value, text_.data(), text_.length());
    if (n != text_.length()) return false;
    component_.value = value;
    return true;
  }

  bool linkType(Component &component_, ZuCSpan input) {
    if (component_.mode == Mode::All) return true;
    if (component_.mode != Mode::Exact) return false;
    ZuCSpan text{input.data() + component_.offset, component_.length};
    auto value = LinkType::lookup(text);
    if (value < 0 || value >= LinkType::N) return false;
    component_.value = uint64_t(value);
    return true;
  }

  bool queueType(Component &component_, ZuCSpan input) {
    if (component_.mode == Mode::All) return true;
    if (component_.mode != Mode::Exact) return false;
    ZuCSpan text{input.data() + component_.offset, component_.length};
    auto value = QueueType::lookup(text);
    if (value < 0 || value >= QueueType::N) return false;
    component_.value = uint64_t(value);
    return true;
  }

  static bool enumeration(const Component &component_, int value) {
    return component_.mode == Mode::All ||
      int(component_.value) == value;
  }

  bool match(const Component &component_, ZuCSpan value) const {
    switch (component_.mode) {
      case Mode::All:
	return true;
      case Mode::Exact:
	return value.exact(component_.text(m_text));
      case Mode::Prefix:
	return value.match(component_.text(m_text));
      default:
	return false;
    }
  }

  static ZuCSpan raw(const Component &component_, ZuCSpan input) {
    return {input.data() + component_.offset, component_.length};
  }

  void append(const Component &component_, ZuCSpan input) {
    if (component_.mode == Mode::All) {
      m_text << '*';
      return;
    }
    m_text << raw(component_, input);
    if (component_.mode == Mode::Prefix) m_text << '*';
  }

  bool decode(Component &component_, ZuCSpan input) {
    if (component_.mode == Mode::All) {
      component_.offset = m_text.length();
      component_.length = 0;
      return true;
    }
    ZuCSpan encoded = raw(component_, input);
    unsigned offset = m_text.length();
    m_text << encoded;
    ZuSpan<char> span{m_text.data() + offset, encoded.length()};
    auto decoded = Codec::decode(span);
    if (!decoded || unsigned(decoded.in) != encoded.length()) return false;
    component_.offset = offset;
    component_.length = unsigned(decoded.out);
    m_text.length(offset + component_.length);
    return true;
  }

  bool canonical(fbs::Group group, ZuCSpan input, unsigned maxLength) {
    m_text.length(0);
    m_all = false;
    switch (group) {
      case fbs::Group::Heap:
      case fbs::Group::Hash:
      case fbs::Group::Mx:
      case fbs::Group::DB:
      case fbs::Group::App:
	append(m_c0, input);
	break;
      case fbs::Group::Alert:
	append(m_c0, input);
	break;
      case fbs::Group::Thread:
	if (m_c0.mode == Mode::All)
	  m_text << '*';
	else
	  m_text << m_c0.value;
	break;
      case fbs::Group::Hub:
	if (m_c0.mode == Mode::All)
	  m_text << '*';
	else
	  m_text << LinkType::name(int(m_c0.value));
	m_text << ':';
	append(m_c1, input);
	break;
      case fbs::Group::Queue:
	if (m_c0.mode == Mode::All)
	  m_text << '*';
	else
	  m_text << QueueType::name(int(m_c0.value));
	m_text << ':';
	append(m_c1, input);
	m_text << ':';
	append(m_c2, input);
	break;
      default:
	return false;
    }
    if (m_text.length() > maxLength) return false;
    m_keyLength = m_text.length();

    switch (group) {
      case fbs::Group::Heap:
      case fbs::Group::Hash:
      case fbs::Group::Mx:
      case fbs::Group::DB:
      case fbs::Group::App:
	if (!decode(m_c0, input)) return false;
	break;
      case fbs::Group::Hub:
	if (!decode(m_c1, input)) return false;
	break;
      case fbs::Group::Queue:
	if (!decode(m_c1, input) || !decode(m_c2, input)) return false;
	break;
      case fbs::Group::Alert:
      case fbs::Group::Thread:
	break;
      default:
	return false;
    }

    switch (group) {
      case fbs::Group::Heap:
      case fbs::Group::Hash:
      case fbs::Group::Thread:
      case fbs::Group::Mx:
      case fbs::Group::DB:
      case fbs::Group::App:
	m_all = m_c0.mode == Mode::All;
	break;
      case fbs::Group::Alert:
	m_all = m_c0.mode == Mode::All;
	break;
      case fbs::Group::Hub:
	m_all = m_c0.mode == Mode::All && m_c1.mode == Mode::All;
	break;
      case fbs::Group::Queue:
	m_all = m_c0.mode == Mode::All &&
	  m_c1.mode == Mode::All && m_c2.mode == Mode::All;
	break;
      default:
	return false;
    }
    return true;
  }

private:
  Text		m_text;
  Component	m_c0;
  Component	m_c1;
  Component	m_c2;
  unsigned	m_keyLength = 0;
  bool		m_all = false;
};

} // Filter_
} // Ztc

#endif /* ZtcFilter_HH */
