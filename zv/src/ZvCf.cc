//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// application configuration

#include <zlib/ZmAssert.hh>

#include <zlib/ZtRegex.hh>

#include <zlib/ZePlatform.hh>
#include <zlib/ZeAssert.hh>

#include <zlib/ZvCf.hh>

namespace ZvCf_ {

template <bool Key = false, bool Raw = false>
ZuIfT<!Raw, ZuTuple<String, unsigned, bool>>
scanString(ZuCSpan in, unsigned off, Cf::Defines *defines = nullptr)
{
  unsigned n = in.length();

  if (off >= n) return {String{}, 0U};

  const auto &fileSpace = ZtREGEX("\G\s+");
  const auto &fileUnquoted = Key ?
    ZtREGEX("\G[^\\\"\$\s{}\[\]\.]+") :
    ZtREGEX("\G[^\\\"\$\s{}\[\],]+");
  const auto &fileQuoted = ZtREGEX("\G\\(.)");
  const auto &fileRefVar = ZtREGEX("\G\${(\w+)}");
  const auto &fileDblQuote = ZtREGEX("\G\"");
  const auto &fileDblUnquoted = ZtREGEX("\G[^\\\"]+");

  String value;
  ZtRegexCaptures(c, 1);
  unsigned off_ = off;
  bool failed = false;

  while (off < n) {
    if (fileUnquoted.m(in, c, off)) {
      off += c[1].length();
      value += c[1];
      continue;
    }
    if (fileQuoted.m(in, c, off)) {
      off += c[1].length();
      value += c[2];
      continue;
    }
    if constexpr (!Key)
      if (fileRefVar.m(in, c, off)) {
	off += c[1].length();
	ZuCSpan d;
	if (defines) d = defines->findVal(c[2]);
	if (!d) { String env{c[2]}; d = ::getenv(env); }
	if (d)
	  value += d;
	else
	  failed = true;
	continue;
      }
    if (fileDblQuote.m(in, c, off)) {
      off += c[1].length();
      while (off < n) {
	if (fileDblUnquoted.m(in, c, off)) {
	  off += c[1].length();
	  value += c[1];
	  continue;
	}
	if (fileQuoted.m(in, c, off)) {
	  off += c[1].length();
	  value += c[2];
	  continue;
	}
	++off; // elide fileDblQuote.m() of closing "
#if 0
	if (fileDblQuote.m(in, c, off)) {
	  off += c[1].length();
	  break;
	}
#endif
	break;
      }
      continue;
    }
    break;
  }
  if (off > off_ && off < n && fileSpace.m(in, c, off)) off += c[1].length();
  return {ZuMv(value), off - off_, failed};
}

template <bool Raw = false>
ZuIfT<!Raw, String>
quoteString(ZuCSpan in)
{
  unsigned n = in.length();

  if (!n) return "\"\"";

  const auto &strQuoted = ZtREGEX("\W");
  const auto &strUnquoted = ZtREGEX("\G[^\\\"]+");

  ZtRegexCaptures(c, 1);

  if (!strQuoted.m(in, c, 0)) return in;

  String out{n + (n>>3) + 2}; // 1+1/8 size estimation

  unsigned off = 0;

  out << '"';
  while (off < n) {
    if (strUnquoted.m(in, c, off)) {
      off += c[1].length();
      out << c[1];
      continue;
    }
    out << '\\' << in[off++];
  }
  out << '"';
  return out;
}

template <bool Key = false, bool Raw = false>
ZuIfT<Raw, ZuTuple<String, unsigned, bool>>
scanString(ZuCSpan in, unsigned off, Cf::Defines *defines = nullptr)
{
  unsigned n = in.length();

  if (off >= n) return {String{}, 0U};

  if constexpr (!Key) {
    in.offset(off);
    return {in, n, false};
  } else {
    const auto &argUnquoted = ZtREGEX("\G[^\.\[\]]+");
    const auto &argQuoted = ZtREGEX("\G\\(.)");

    String value;
    ZtRegexCaptures(c, 1);
    unsigned off_ = off;

    while (off < n) {
      if (argUnquoted.m(in, c, off)) {
	off += c[1].length();
	value += c[1];
	continue;
      }
      if (argQuoted.m(in, c, off)) {
	off += c[1].length();
	value += c[2];
	continue;
      }
      break;
    }
    return {ZuMv(value), off - off_, false};
  }
}

template <bool Raw = false>
ZuIfT<Raw, String>
quoteString(ZuCSpan in)
{
  return in;
}

// static const auto &indexMatch() { return ZtREGEX("\G\[(\d+)\]$"); }

template <bool Raw = false>
ZuTuple<String, int, unsigned>
scanKey(ZuCSpan in, unsigned off, int index, Cf::Defines *defines = nullptr)
{
  unsigned n = in.length();

  if (off >= n) {
null:
    return {String{}, 0, 0U};
  }

  unsigned off_ = off;

  auto [key, o, failed] = scanString<true, Raw>(in, off, defines);
  if (!o) goto null;
  off += o;

  return {ZuMv(key), index, off - off_};
}

static const auto &matchDot() { return ZtREGEX("\G\."); }

template <bool Raw>
ZuTuple<Cf *, String, int, unsigned>
Cf::getScope_(ZuCSpan in, Cf::Defines *defines) const
{
  unsigned n = in.length();

  ZtRegexCaptures(c, 1);
  unsigned off = 0;

  auto this_ = const_cast<Cf *>(this);
  String key;
  int index = -1;

  while (off < n) {
    auto [key_, index_, o] = scanKey<Raw>(in, off, -1, defines);
    off += o;
    key = ZuMv(key_);
    index = index_;
    if (!matchDot().m(in, c, off)) break;
    off += c[1].length();
    auto node = this_->m_tree.find(key);
    if (!node) goto null;
    if (index < 0) {
      if (!node->CfNode::data.is<ZmRef<Cf>>()) goto null;
      this_ = node->get_<ZmRef<Cf>>();
    } else {
      if (!node->CfNode::data.is<CfVec>()) goto null;
      this_ = node->get_<CfVec>().get(index);
    }
    if (!this_) goto null;
  }

  return {this_, ZuMv(key), index, off};

null:
  return {nullptr, String{}, 0, 0U};
}
  
template <bool Raw>
ZuTuple<Cf *, String, int, unsigned>
Cf::mkScope_(ZuCSpan in, Cf::Defines *defines)
{
  unsigned n = in.length();

  ZtRegexCaptures(c, 1);
  unsigned off = 0;

  Cf *this_ = this;
  String key;
  int index = -1;

  while (off < n) {
    auto [key_, index_, o] = scanKey<Raw>(in, off, -1, defines);
    off += o;
    key = ZuMv(key_);
    index = index_;
    if (!matchDot().m(in, c, off)) break;
    off += c[1].length();
    auto node = this_->m_tree.find(key);
    if (!node) this_->m_tree.addNode(node = new Cf::Node{this_, key});
    if (index < 0) {
      this_ = node->get_<ZmRef<Cf>>();
      if (!this_) node->set_<ZmRef<Cf>>(this_ = new Cf{node});
    } else {
      this_ = node->getElem<CfVec>(index);
      if (!this_) node->setElem<CfVec>(index, this_ = new Cf{node});
    }
  }

  return {this_, ZuMv(key), index, off};
}

template <bool Raw>
ZuTuple<Cf *, CfNode *, int, unsigned>
Cf::mkNode_(ZuCSpan in)
{
  auto [this_, key, index, o] = mkScope_<Raw>(in);
  auto node = this_->m_tree.find(key);
  if (!node) this_->m_tree.addNode(node = new Node{this_, key});
  return {this_, ZuMv(node), index, o};
}

void Cf::fromString(ZuCSpan in, ZuCSpan fileName, ZmRef<Defines> defines)
{
  unsigned n = in.length();

  if (!n) return;

  const auto &fileSpace = ZtREGEX("\G\s+");

  const auto &fileComment = ZtREGEX("\G#[^\n]*\n\s*");
  const auto &fileDirective = ZtREGEX("\G(%\w+)\s+");

  const auto &fileBeginScope = ZtREGEX("\G\{\s*");
  const auto &fileEndScope = ZtREGEX("\G\}\s*");

  const auto &fileBeginArray = ZtREGEX("\G\[\s*");
  const auto &fileEndArray = ZtREGEX("\G\]\s*");
  const auto &fileComma = ZtREGEX("\G,\s*");

  const auto &fileDefine = ZtREGEX("(\w+)\s+");

  const auto &fileLine = ZtREGEX("\G[^\n]*\n");

  enum {
    KVMask	= 0x0003,
    Key		= 0x0000,
    Value	= 0x0001,
    Next	= 0x0002,

    ArrayMask	= 0x000c,
    NoArray	= 0x0000,
    UnkArray	= 0x0004,
    StringVec_	= 0x0008,
    CfVec_	= 0x000c,
  };

  auto this_ = this;
  unsigned state = Key;
  int index = -1;
  using State = ZuTuple<unsigned, int>; // state, index
  ZtArray<State> stack;
  Node *node = nullptr;
  ZtRegexCaptures(c, 1);
  unsigned off = 0;
  
  if (fileSpace.m(in, c, off)) off += c[1].length();
  while (off < n) {
    // comments
    if (fileComment.m(in, c, off)) {
      off += c[1].length();
      continue;
    }
    if ((state & KVMask) == Key) {
      // directives
      if (fileDirective.m(in, c, off)) {
	off += c[1].length();
	if (c[2] == "%include") {
	  auto [file, o, failed] = scanString(in, off, defines);
	  if (!file) goto syntax;
	  off += o;
	  ZmRef<Cf> incCf = new Cf{};
	  incCf->fromFile(file, defines);
	  this_->merge(incCf);
	  continue;
	}
	if (c[2] == "%define") {
	  if (!fileDefine.m(in, c, off)) goto syntax;
	  off += c[1].length();
	  auto var = c[2];
	  auto [value, o, failed] = scanString(in, off, defines);
	  if (!o) goto syntax;
	  off += o;
	  defines->del(var);
	  defines->add(var, ZuMv(value));
	  continue;
	}
	goto syntax;
      }
      // end scope
      if (fileEndScope.m(in, c, off)) {
	if (!stack) goto syntax;
	off += c[1].length();
	this_ = this_->node()->owner;
	{
	  auto [ state_, index_ ] = stack.pop();
	  state = state_;
	  index = index_;
	}
	continue;
      }
      // key
      auto [key, index_, o] = scanKey(in, off, index, defines);
      if (!o) goto syntax;
      index = index_;
      off += o;
      node = this_->m_tree.find(key);
      if (!node) this_->m_tree.addNode(node = new Node{this_, key});
      state = (state & ~KVMask) | Value;
      continue;
    }
    if ((state & KVMask) == Value) {
      // begin array
      if (fileBeginArray.m(in, c, off)) {
	if ((state & ArrayMask) != NoArray) goto syntax;
	off += c[1].length();
	state = (state & ~ArrayMask) | UnkArray;
	index = 0;
	continue;
      }
      // begin scope
      if (fileBeginScope.m(in, c, off)) {
	switch (node->CfNode::data.type()) {
	  case Data::Index<void>{}:
	    break;
	  case Data::Index<String>{}:
	  case Data::Index<StringVec>{}:
	    goto syntax;
	  case Data::Index<ZmRef<Cf>>{}:
	    if (index >= 0) goto syntax;
	    break;
	  case Data::Index<CfVec>{}:
	    if (index < 0) goto syntax;
	    break;
	}
	if ((state & ArrayMask) == StringVec_) goto syntax;
	off += c[1].length();
	if (index < 0) {
	  this_ = node->get_<ZmRef<Cf>>();
	  if (!this_) node->set_<ZmRef<Cf>>(this_ = new Cf{node});
	} else {
	  this_ = node->getElem<CfVec>(index);
	  if (!this_) node->setElem<CfVec>(index, this_ = new Cf{node});
	}
	if ((state & ArrayMask) == NoArray) {
	  state = (state & ~KVMask) | Key;
	  node = nullptr;
	} else {
	  if ((state & ArrayMask) == UnkArray)
	    state = (state & ~ArrayMask) | CfVec_;
	  state = (state & ~KVMask) | Next;
	}
	stack.push(State{state, index});
	state = Key;
	index = -1;
	node = nullptr;
	continue;
      }
      // comma
      if (fileComma.m(in, c, off)) {
	if ((state & ArrayMask) == NoArray) goto syntax;
	off += c[1].length();
	++index;
	continue;
      }
      auto [value, o, failed] = scanString(in, off, defines);
      if (!o) goto syntax;
      switch (node->CfNode::data.type()) {
	case Data::Index<void>{}:
	  break;
	case Data::Index<String>{}:
	  if (index >= 0) goto syntax;
	  break;
	case Data::Index<StringVec>{}:
	  if (index < 0) goto syntax;
	  break;
	case Data::Index<ZmRef<Cf>>{}:
	case Data::Index<CfVec>{}:
	  goto syntax;
      }
      if ((state & ArrayMask) == CfVec_) goto syntax;
      off += o;
      if (index < 0) {
	if (failed)
	  this_->m_tree.delNode(static_cast<Tree::Node *>(node));
	else
	  node->set_<String>(ZuMv(value));
      } else
	node->setElem<StringVec>(index, failed ? String{} : ZuMv(value));
      if ((state & ArrayMask) == NoArray) {
	state = (state & ~KVMask) | Key;
	node = nullptr;
      } else {
	if ((state & ArrayMask) == UnkArray)
	  state = (state & ~ArrayMask) | StringVec_;
	state = (state & ~KVMask) | Next;
      }
      continue;
    }
    if ((state & KVMask) == Next) {
      // comma
      if (fileComma.m(in, c, off)) {
	off += c[1].length();
	state = (state & ~KVMask) | Value;
	++index;
	continue;
      }
      // end array
      if (fileEndArray.m(in, c, off)) {
	off += c[1].length();
	state = (state & ~(ArrayMask | KVMask)) | Key;
	index = -1;
	node = nullptr;
	continue;
      }
      goto syntax;
    }
  }
  return;

syntax:
  if (off < n - 1) {
    unsigned lpos = 0, line = 0;
    while (lpos < off && fileLine.m(in, c, lpos)) {
      lpos += c[1].length();
      line++;
    }
    if (!line) line = 1;
    throw badSyntax(line, in[off], fileName);
  }
}

void Cf::print(ZuVStream &s, String &indent) const
{
  auto i = m_tree.citer();
  while (auto node = i()) {
    s << indent << quoteString(node->CfNode::key) << ' ';
    switch (node->CfNode::data.type()) {
      case Data::Index<void>{}:
	break;
      case Data::Index<String>{}:
      case Data::Index<StringVec>{}: {
	if (node->CfNode::data.is<String>())
	  s << quoteString(node->get_<String>());
	else {
	  s << '[';
	  node->CfNode::data.p<StringVec>().all(
	      [&s, first = true](const String &value) mutable {
	    if (ZuUnlikely(first)) first = false; else s << ", ";
	    s << quoteString(value);
	  });
	  s << ']';
	}
	s << '\n';
      } break;
      case Data::Index<ZmRef<Cf>>{}:
      case Data::Index<CfVec>{}: {
	auto output = [&indent](Cf *cf, ZuVStream &s) mutable {
	  if (!cf || !cf->count()) { s << "{}"; return; }
	  s << "{\n";
	  indent.append("  ", 2);
	  cf->print(s, indent);
	  indent.length_(indent.length() - 2);
	  s << indent << '}';
	};
	if (node->CfNode::data.is<ZmRef<Cf>>())
	  output(node->get_<ZmRef<Cf>>(), s);
	else
	  node->CfNode::data.p<CfVec>().all(
	      [&s, output = ZuMv(output), first = true](Cf *cf) mutable {
	    if (ZuUnlikely(first)) first = false; else s << ",\n";
	    output(cf, s);
	  });
	s << '\n';
      } break;
    }
  }
}

ZuTuple<Cf *, String> Cf::getScope(ZuCSpan fullKey) const
{
  auto [this_, key, index, o] = getScope_<true>(fullKey);
  return {this_, key};
}

CfNode *Cf::mkNode(ZuCSpan fullKey)
{
  auto [this_, node, index, o] = mkNode_<true>(fullKey);
  return node;
}

void Cf::set(ZuCSpan key, String value)
{
  auto [this_, node, index, o] = mkNode_<true>(key);
  if (index < 0)
    node->set_<String>(ZuMv(value));
  else
    node->setElem<StringVec>(index, ZuMv(value));
}

void Cf::setStringVec(ZuCSpan key, StringVec value)
{
  auto [this_, node, index, o] = mkNode_<true>(key);
  node->set_<StringVec>(ZuMv(value));
}

ZmRef<Cf> Cf::mkCf(ZuCSpan key)
{
  auto [this_, node, index, o] = mkNode_<true>(key);
  ZmRef<Cf> cf = new Cf{node};
  if (index < 0)
    node->set_<ZmRef<Cf>>(cf);
  else
    node->setElem<CfVec>(index, cf);
  return cf;
}

void Cf::setCf(ZuCSpan key, ZmRef<Cf> cf)
{
  auto [this_, node, index, o] = mkNode_<true>(key);
  cf->m_node = node;
  if (index < 0)
    node->set_<ZmRef<Cf>>(ZuMv(cf));
  else
    node->setElem<CfVec>(index, ZuMv(cf));
}

void Cf::setCfVec(ZuCSpan key, CfVec value)
{
  auto [this_, node, index, o] = mkNode_<true>(key);
  node->set_<CfVec>(ZuMv(value));
}

void Cf::unset(ZuCSpan fullKey)
{
  auto [this_, key, index, o] = getScope_<true>(fullKey);
  if (this_) this_->m_tree.del(key);
}

void Cf::clean()
{
  m_tree.clean();
}

void Cf::merge(const Cf *cf)
{
  auto i = cf->m_tree.citer();
  while (auto srcNode = i()) {
    auto dstNode = m_tree.find(srcNode->CfNode::key);
    if (!dstNode)
      m_tree.addNode(dstNode = new Node{this, srcNode->CfNode::key});
    switch (srcNode->CfNode::data.type()) {
      case Data::Index<void>{}:
	break;
      case Data::Index<String>{}:
	dstNode->set_<String>(srcNode->get_<String>());
	break;
      case Data::Index<StringVec>{}:
	dstNode->set_<StringVec>(srcNode->get_<StringVec>());
	break;
      case Data::Index<ZmRef<Cf>>{}: {
	if (auto srcCf = srcNode->get_<ZmRef<Cf>>()) {
	  auto dstCf = dstNode->get_<ZmRef<Cf>>();
	  if (!dstCf) dstNode->set_<ZmRef<Cf>>(dstCf = new Cf{dstNode});
	  dstCf->merge(srcCf);
	}
      } break;
      case Data::Index<CfVec>{}: {
	for (unsigned i = 0, n = srcNode->get_<CfVec>().length(); i < n; i++)
	  if (auto srcCf = srcNode->getElem<CfVec>(i)) {
	    auto dstCf = dstNode->getElem<CfVec>(i);
	    if (!dstCf) dstNode->set_<CfVec>(dstCf = new Cf{dstNode});
	    dstCf->merge(srcCf);
	  }
      } break;
    }
  }
}

} // ZvCf_
