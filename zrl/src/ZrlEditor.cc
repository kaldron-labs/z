//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface - line editor

#include <signal.h>

#include <zlib/ZuSort.hh>

#include <zlib/ZmAlloc.hh>

#include <zlib/ZtRegex.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZrlEditor.hh>

namespace Zrl {

namespace Op {
ZrlExtern void print_(uint32_t op, ZuVStream &s)
{
  int op_ = op & Mask;
  switch (op_) {
    case Op::Nop:
    case Op::ArgDigit:
      op &= ~Op::KeepArg; // implied
      break;
  }
  switch (op_) {
    case Op::Nop:
    case Op::Mode:
    case Op::Push:
    case Op::Pop:
    case Op::Register:
      op &= ~Op::KeepReg; // implied
      break;
  }
  s << name(op_);
  if (op & ~Mask) {
    s << '[';
    bool pipe = false;
    auto sep = [&pipe, &s]() { if (!pipe) pipe = true; else s << '|'; };
    if (op & Mv) { sep(); s << "Mv"; }
    if (op & Del) { sep(); s << "Del"; }
    if (op & Copy) { sep(); s << "Copy"; }

    if (op & Draw) { sep(); s << "Draw"; }
    if (op & Vis) { sep(); s << "Vis"; }

    if (op & Unix) { sep(); s << "Unix"; }
    if (op & Past) { sep(); s << "Past"; }

    if (op & KeepArg) { sep(); s << "KeepArg"; }
    if (op & KeepReg) { sep(); s << "KeepReg"; }
    s << ']';
  }
}
} // Op

Editor::Editor()
{
  // manual initialization of opcode-indexed jump table
 
  m_cmdFn[Op::Nop] = &Editor::cmdNop;
  m_cmdFn[Op::Syn] = &Editor::cmdSyn;

  m_cmdFn[Op::Mode] = &Editor::cmdMode;
  m_cmdFn[Op::Push] = &Editor::cmdPush;
  m_cmdFn[Op::Pop] = &Editor::cmdPop;

  m_cmdFn[Op::Error] = &Editor::cmdError;
  m_cmdFn[Op::EndOfFile] = &Editor::cmdEndOfFile;

  m_cmdFn[Op::SigInt] = &Editor::cmdSigInt;
  m_cmdFn[Op::SigQuit] = &Editor::cmdSigQuit;
  m_cmdFn[Op::SigSusp] = &Editor::cmdSigSusp;

  m_cmdFn[Op::Enter] = &Editor::cmdEnter;

  m_cmdFn[Op::Up] = &Editor::cmdUp;
  m_cmdFn[Op::Down] = &Editor::cmdDown;
  m_cmdFn[Op::Left] = &Editor::cmdLeft;
  m_cmdFn[Op::Right] = &Editor::cmdRight;
  m_cmdFn[Op::Home] = &Editor::cmdHome;
  m_cmdFn[Op::End] = &Editor::cmdEnd;

  m_cmdFn[Op::FwdWord] = &Editor::cmdFwdWord;
  m_cmdFn[Op::RevWord] = &Editor::cmdRevWord;
  m_cmdFn[Op::FwdWordEnd] = &Editor::cmdFwdWordEnd;
  m_cmdFn[Op::RevWordEnd] = &Editor::cmdRevWordEnd;

  m_cmdFn[Op::MvMark] = &Editor::cmdMvMark;

  m_cmdFn[Op::ClrVis] = &Editor::cmdClrVis;

  m_cmdFn[Op::InsToggle] = &Editor::cmdInsToggle;
  m_cmdFn[Op::Insert] = &Editor::cmdInsert;
  m_cmdFn[Op::Over] = &Editor::cmdOver;

  m_cmdFn[Op::Clear] = &Editor::cmdClear;
  m_cmdFn[Op::Redraw] = &Editor::cmdRedraw;

  m_cmdFn[Op::Paste] = &Editor::cmdPaste;
  m_cmdFn[Op::Yank] = &Editor::cmdYank;
  m_cmdFn[Op::Rotate] = &Editor::cmdRotate;

  m_cmdFn[Op::Glyph] = &Editor::cmdGlyph;
  m_cmdFn[Op::InsGlyph] = &Editor::cmdInsGlyph;
  m_cmdFn[Op::OverGlyph] = &Editor::cmdOverGlyph;
  m_cmdFn[Op::BackSpace] = &Editor::cmdBackSpace;
  m_cmdFn[Op::Edit] = &Editor::cmdEdit;
  m_cmdFn[Op::EditRep] = &Editor::cmdEditRep;

  m_cmdFn[Op::TransGlyph] = &Editor::cmdTransGlyph;
  m_cmdFn[Op::TransWord] = &Editor::cmdTransWord;
  m_cmdFn[Op::TransUnixWord] = &Editor::cmdTransUnixWord;

  m_cmdFn[Op::CapGlyph] = &Editor::cmdCapGlyph;
  m_cmdFn[Op::LowerWord] = &Editor::cmdLowerWord;
  m_cmdFn[Op::UpperWord] = &Editor::cmdUpperWord;
  m_cmdFn[Op::CapWord] = &Editor::cmdCapWord;

  m_cmdFn[Op::LowerVis] = &Editor::cmdLowerVis;
  m_cmdFn[Op::UpperVis] = &Editor::cmdUpperVis;
  m_cmdFn[Op::CapVis] = &Editor::cmdCapVis;

  m_cmdFn[Op::XchMark] = &Editor::cmdXchMark;

  m_cmdFn[Op::ArgDigit] = &Editor::cmdArgDigit;

  m_cmdFn[Op::Register] = &Editor::cmdRegister;

  m_cmdFn[Op::Undo] = &Editor::cmdUndo;
  m_cmdFn[Op::Redo] = &Editor::cmdRedo;
  m_cmdFn[Op::EmacsUndo] = &Editor::cmdEmacsUndo;
  m_cmdFn[Op::EmacsAbort] = &Editor::cmdEmacsAbort;
  m_cmdFn[Op::Repeat] = &Editor::cmdRepeat;

  m_cmdFn[Op::FwdGlyphSrch] = &Editor::cmdFwdGlyphSrch;
  m_cmdFn[Op::RevGlyphSrch] = &Editor::cmdRevGlyphSrch;

  m_cmdFn[Op::Complete] = &Editor::cmdComplete;
  m_cmdFn[Op::RevComplete] = &Editor::cmdRevComplete;
  m_cmdFn[Op::ListComplete] = &Editor::cmdListComplete;

  m_cmdFn[Op::Next] = &Editor::cmdNext;
  m_cmdFn[Op::Prev] = &Editor::cmdPrev;

  m_cmdFn[Op::ClrIncSrch] = &Editor::cmdClrIncSrch;
  m_cmdFn[Op::FwdIncSrch] = &Editor::cmdFwdIncSrch;
  m_cmdFn[Op::RevIncSrch] = &Editor::cmdRevIncSrch;

  m_cmdFn[Op::PromptSrch] = &Editor::cmdPromptSrch;
  m_cmdFn[Op::EnterSrchFwd] = &Editor::cmdEnterSrchFwd;
  m_cmdFn[Op::EnterSrchRev] = &Editor::cmdEnterSrchRev;
  m_cmdFn[Op::AbortSrch] = &Editor::cmdAbortSrch;

  m_cmdFn[Op::FwdSearch] = &Editor::cmdFwdSearch;
  m_cmdFn[Op::RevSearch] = &Editor::cmdRevSearch;

  m_compSpliceFn = CompSpliceFn{this, ZmFnPtr<&Editor::spliceCompletion>{}};
}

void Editor::init(Config config, App app)
{
  m_config = config;
  m_app = app;

  // default key bindings

  m_defltMap = new Map{ "default" };

  m_defltMap->addMode(0, ModeType::Edit);	// normal
  m_defltMap->addMode(1, ModeType::Edit);	// literal next
  m_defltMap->addMode(2, ModeType::Edit);	// meta next
  m_defltMap->addMode(3, ModeType::Edit);	// highlit span
  m_defltMap->addMode(4, ModeType::Edit);	// highlit span - meta next

  // normal mode
  m_defltMap->bind(0, -VKey::Any, { { Op::Glyph } });

  m_defltMap->bind(0, -VKey::EndOfFile, { { Op::EndOfFile} });

  m_defltMap->bind(0, -VKey::SigInt, { { Op::SigInt} });
  m_defltMap->bind(0, -VKey::SigQuit, { { Op::SigQuit} });
  m_defltMap->bind(0, -VKey::SigSusp, { { Op::SigSusp} });

  m_defltMap->bind(0, -VKey::Enter, { { Op::Enter} });

  m_defltMap->bind(0, -VKey::Erase, { { Op::Left | Op::Del} });
  m_defltMap->bind(0, -VKey::WErase,
      { { Op::RevWord | Op::Del | Op::Unix } });
  m_defltMap->bind(0, -VKey::Kill, { { Op::Home | Op::Del } });

  m_defltMap->bind(0, -VKey::Up,
      { { Op::Up | Op::Mv }, { Op::End | Op::Mv } });
  m_defltMap->bind(0, -VKey::Down,
      { { Op::Down | Op::Mv }, { Op::End | Op::Mv } });
  m_defltMap->bind(0, -VKey::Left, { { Op::Left | Op::Mv } });
  m_defltMap->bind(0, -VKey::Right, { { Op::Right | Op::Mv } });

  m_defltMap->bind(0, -VKey::Home, { { Op::Home | Op::Mv } });
  m_defltMap->bind(0, -VKey::End, { { Op::End | Op::Mv } });

  m_defltMap->bind(0, -VKey::PgUp, { { Op::Prev } });
  m_defltMap->bind(0, -VKey::PgDn, { { Op::Next } });

  m_defltMap->bind(0, -VKey::Tab, { { Op::Complete } });
  m_defltMap->bind(0, -(VKey::Tab | VKey::Shift), { { Op::RevComplete } });
  m_defltMap->bind(0, '\x04', { { Op::ListComplete } });

  m_defltMap->bind(0, '\x0c', { { Op::Clear } });
  m_defltMap->bind(0, -VKey::Redraw, { { Op::Redraw } });

  m_defltMap->bind(0, -(VKey::Left | VKey::Ctrl), {
    { Op::RevWord | Op::Mv }
  });
  m_defltMap->bind(0, -(VKey::Right | VKey::Ctrl), {
    { Op::FwdWord | Op::Mv }
  });
  m_defltMap->bind(0, -(VKey::Left | VKey::Alt), {
    { Op::RevWord | Op::Unix | Op::Mv }
  });
  m_defltMap->bind(0, -(VKey::Right | VKey::Alt), {
    { Op::FwdWord | Op::Unix | Op::Mv }
  });

  m_defltMap->bind(0, -VKey::Insert, { { Op::InsToggle } });
  m_defltMap->bind(0, -VKey::Delete, { { Op::Right | Op::Del } });

  m_defltMap->bind(0, 'W' - '@', { { Op::Nop } });
  m_defltMap->bind(0, 'Y' - '@', { { Op::Yank } });

  m_defltMap->bind(0, '_' - '@', { { Op::EmacsUndo } });
  m_defltMap->bind(0, 'G' - '@', { { Op::EmacsAbort } });

  // initiate highlight "visual" mode
  m_defltMap->bind(0, -(VKey::Up | VKey::Shift), {
    { Op::Up | Op::Mv | Op::Vis }, { Op::Push, 3 }
  });
  m_defltMap->bind(0, -(VKey::Down | VKey::Shift), {
    { Op::Down | Op::Mv | Op::Vis }, { Op::Push, 3 }
  });
  m_defltMap->bind(0, -(VKey::Left | VKey::Shift), {
    { Op::Left | Op::Mv | Op::Vis }, { Op::Push, 3 }
  });
  m_defltMap->bind(0, -(VKey::Right | VKey::Shift), {
    { Op::Right | Op::Mv | Op::Vis }, { Op::Push, 3 }
  });
  m_defltMap->bind(0, -(VKey::Left | VKey::Shift | VKey::Ctrl), {
    { Op::RevWord | Op::Mv | Op::Vis }, { Op::Push, 3 }
  });
  m_defltMap->bind(0, -(VKey::Right | VKey::Shift | VKey::Ctrl), {
    { Op::FwdWordEnd | Op::Past | Op::Mv | Op::Vis }, { Op::Push, 3 }
  });
  m_defltMap->bind(0, -(VKey::Left | VKey::Shift | VKey::Alt), {
    { Op::RevWord | Op::Unix | Op::Mv | Op::Vis }, { Op::Push, 3 }
  });
  m_defltMap->bind(0, -(VKey::Right | VKey::Shift | VKey::Alt), {
    { Op::FwdWordEnd | Op::Unix | Op::Past | Op::Mv | Op::Vis },
    { Op::Push, 3 }
  });
  m_defltMap->bind(0, -(VKey::Home | VKey::Shift), {
    { Op::Home | Op::Mv | Op::Vis }, { Op::Push, 3 }
  });
  m_defltMap->bind(0, -(VKey::End | VKey::Shift), {
    { Op::End | Op::Mv | Op::Vis }, { Op::Push, 3 }
  });

  // literal mode
  m_defltMap->bind(0, -VKey::LNext, {
    { Op::Glyph, Cmd::nullArg(), '^' },
    { Op::Left | Op::Mv },
    { Op::Push, 1 }
  });
  m_defltMap->bind(1, -VKey::Any, { { Op::OverGlyph }, { Op::Pop } });
  m_defltMap->bind(1, -VKey::AnySys, { { Op::OverGlyph }, { Op::Pop } });
  m_defltMap->bind(1, -VKey::AnyFn, { { Op::Pop }, { Op::Syn } });

  // ESC-prefixed "meta" mode (handles non-Fn Alt-keystrokes)
  m_defltMap->bind(0, '\x1b', { { Op::Push, 2 } });
  m_defltMap->bind(2, -VKey::Any, { { Op::Pop } });
  m_defltMap->bind(2, -VKey::AnySys, { { Op::Pop }, { Op::Syn } });
  m_defltMap->bind(2, -VKey::AnyFn, { { Op::Pop }, { Op::Syn } });
  m_defltMap->bind(2, '\x1b', { { Op::Glyph }, { Op::Pop } });
  m_defltMap->bind(2, 'w', { { Op::Nop }, { Op::Pop } });

  // highlight "visual" mode
  m_defltMap->bind(3, -VKey::Any, {
    { Op::ClrVis | Op::Del | Op::Vis }, // Del|Vis implies interim splice
    { Op::Pop }, { Op::Syn }
  });
  m_defltMap->bind(3, -VKey::AnySys, {
    { Op::ClrVis },
    { Op::Pop }, { Op::Syn }
  });
  m_defltMap->bind(3, -VKey::AnyFn, {
    { Op::ClrVis },
    { Op::Pop }, { Op::Syn }
  });
  m_defltMap->bind(3, -VKey::Delete, {
    { Op::ClrVis | Op::Del },
    { Op::Pop }
  });
  m_defltMap->bind(3, -VKey::Erase, {
    { Op::Syn, { }, -VKey::Delete }
  });
  m_defltMap->bind(3, -(VKey::Up | VKey::Shift), {
    { Op::Up | Op::Mv | Op::Vis }
  });
  m_defltMap->bind(3, -(VKey::Down | VKey::Shift), {
    { Op::Down | Op::Mv | Op::Vis }
  });
  m_defltMap->bind(3, -(VKey::Left | VKey::Shift), {
    { Op::Left | Op::Mv | Op::Vis }
  });
  m_defltMap->bind(3, -(VKey::Right | VKey::Shift), {
    { Op::Right | Op::Mv | Op::Vis }
  });
  m_defltMap->bind(3, -(VKey::Left | VKey::Shift | VKey::Ctrl), {
    { Op::RevWord | Op::Mv | Op::Vis }
  });
  m_defltMap->bind(3, -(VKey::Right | VKey::Shift | VKey::Ctrl), {
    { Op::FwdWordEnd | Op::Past | Op::Mv | Op::Vis }
  });
  m_defltMap->bind(3, -(VKey::Left | VKey::Shift | VKey::Alt), {
    { Op::RevWord | Op::Unix | Op::Mv | Op::Vis }
  });
  m_defltMap->bind(3, -(VKey::Right | VKey::Shift | VKey::Alt), {
    { Op::FwdWordEnd | Op::Unix | Op::Past | Op::Mv | Op::Vis }
  });
  m_defltMap->bind(3, -(VKey::Home | VKey::Shift), {
    { Op::Home | Op::Mv | Op::Vis }
  });
  m_defltMap->bind(3, -(VKey::End | VKey::Shift), {
    { Op::End | Op::Mv | Op::Vis }
  });
  m_defltMap->bind(3, 'W' - '@', {
    { Op::ClrVis | Op::Copy | Op::Del },
    { Op::Pop }
  });

  // ESC-prefixed "meta" mode, from within highlight "visual" mode
  m_defltMap->bind(3, '\x1b', { { Op::Push, 4 } });
  m_defltMap->bind(4, -VKey::Any, { { Op::Pop } });
  m_defltMap->bind(4, -VKey::AnySys, { { Op::Pop }, { Op::Syn } });
  m_defltMap->bind(4, -VKey::AnyFn, { { Op::Pop }, { Op::Syn } });
  m_defltMap->bind(4, '\x1b', {
    { Op::ClrVis | Op::Del | Op::Vis },
    { Op::Glyph },
    { Op::Pop, 2 }
  });
  m_defltMap->bind(4, 'w', {
    { Op::ClrVis | Op::Copy },
    { Op::Pop, 2 }
  });

  m_map = m_defltMap;

  m_tty.init(m_config.vkeyInterval);
}
void Editor::final()
{
  m_tty.final();
  m_map = nullptr;
  m_defltMap = nullptr;
  m_app = {};
}

// all parse() functions return v such that
// v <= 0 - parse failure at -v
// v  > 0 - parse success ending at v
namespace {
int VKey_parse_char(uint8_t &byte, ZuCSpan s, int off)
{
  ZtRegexCaptures(c, 1);
  if (s[off] != '\\') {
    byte = s[off++];
  } else if (ZtREGEX("\G\\([^0123x])").m(s, c, off)) {
    off += c[1].length();
    switch (int(c[2][0])) {
      case 'a': byte = '\x07'; break; // BEL
      case 'b': byte = '\b';   break; // BS
      case 'd': byte = '\x7f'; break; // Del
      case 'e': byte = '\x1b'; break; // Esc
      case 'n': byte = '\n';   break; // LF
      case 'r': byte = '\r';   break; // CR
      case 't': byte = '\t';   break; // Tab
      case 'v': byte = '\x0b'; break; // VTab
      default: byte = c[2][0]; break;
    }
  } else if (ZtREGEX("\G\\x([0-9a-fA-F]{2})'").m(s, c, off)) { // hex
    off += c[1].length();
    byte = ZuBox<unsigned>{ZuFmt::Hex<>{}, c[2]};
  } else if (ZtREGEX("\G\\([0-3][0-7]{2})").m(s, c, off)) { // octal
    off += c[1].length();
    byte =
      (static_cast<unsigned>(c[2][0] - '0')<<6) |
      (static_cast<unsigned>(c[2][1] - '0')<<3) |
       static_cast<unsigned>(c[2][2] - '0');
  } else
    return -off;
  return off;
}

int VKey_parse(int32_t &vkey_, ZuCSpan s, int off)
{
  int32_t vkey;
  ZtRegexCaptures(c, 1);
  if (ZtREGEX("\G\s*'").m(s, c, off)) { // regular single character
    off += c[1].length();
    if (s[off] == '\'') return -off;
    uint8_t byte;
    off = VKey_parse_char(byte, s, off);
    if (off < 0) return off;
    if (off >= s.length() || s[off] != '\'') return -off;
    ++off;
    vkey = byte;
  } else if (ZtREGEX("\G\s*\"").m(s, c, off)) { // UTF8 multibyte character
    off += c[1].length();
    if (s[off] == '"') return -off;
    ZuArray<uint8_t, 4> utf;
    uint32_t u;
    do {
      uint8_t byte;
      off = VKey_parse_char(byte, s, off);
      if (off < 0) return off;
      utf << byte;
    } while (off < s.length() && s[off] != '"');
    if (off >= s.length()) return -off;
    if (!ZuUTF8::in(utf.data(), utf.length(), u)) return -off;
    ++off;
    vkey = u;
  } else if (ZtREGEX("\G\s*(?:\^|\\C-)([@A-Z\[\\\]\^_])").m(s, c, off)) {
    off += c[1].length();
    vkey = c[2][0] - '@';
  } else if (ZtREGEX("\G\s*(\w+)").m(s, c, off)) {
    vkey = VKey::lookup(c[2]);
    if (vkey < 0) return -off;
    off += c[1].length();
    if (ZtREGEX("\G\s*\[").m(s, c, off)) {
      off += c[1].length();
      while (ZtREGEX("\G\s*(\w+)").m(s, c, off)) {
	if (c[2] == "Shift") vkey |= VKey::Shift;
	else if (c[2] == "Ctrl") vkey |= VKey::Ctrl;
	else if (c[2] == "Alt") vkey |= VKey::Alt;
	else return -off;
	off += c[2].length();
	if (!ZtREGEX("\G\s*\|").m(s, c, off)) break;
	off += c[1].length();
      }
      if (!ZtREGEX("\G\s*\]").m(s, c, off)) return -off;
      off += c[1].length();
    }
    vkey = -vkey;
  } else
    return -off;
  vkey_ = vkey;
  return off;
}
} // anon namespace

void Cmd::print_(ZuVStream &s) const
{
  s << Op::print(op());
  auto arg = this->arg();
  if (!Cmd::nullArg(arg) || vkey() != -VKey::Null) {
    s << '(';
    if (!Cmd::nullArg(arg)) s << ZuBoxed(arg);
    {
      auto v = vkey();
      if (v != -VKey::Null)
	s << ", " << VKey::print(v);
    }
    s << ')';
  }
}

int Cmd::parse(ZuCSpan s, int off)
{
  ZtRegexCaptures(c, 1);
  if (!ZtREGEX("\G\s*(\w+)").m(s, c, off)) return -off;
  off += c[1].length();
  {
    auto op = Op::lookup(c[2]);
    if (op < 0) return -off;
    m_value = op;
    switch (int(op)) {
      case Op::Nop:
      case Op::ArgDigit:
	m_value |= Op::KeepArg; // implied
	break;
    }
    switch (int(op)) {
      case Op::Nop:
      case Op::Mode:
      case Op::Push:
      case Op::Pop:
      case Op::Register:
	m_value |= Op::KeepReg; // implied
	break;
    }
  }
  if (ZtREGEX("\G\s*\[").m(s, c, off)) {
    off += c[1].length();
    while (ZtREGEX("\G\s*(\w+)").m(s, c, off)) {
      if (c[1] == "Mv") m_value |= Op::Mv;
      else if (c[1] == "Del") m_value |= Op::Del;
      else if (c[1] == "Copy") m_value |= Op::Copy;
      else if (c[1] == "Draw") m_value |= Op::Draw;
      else if (c[1] == "Vis") m_value |= Op::Vis;
      else if (c[1] == "Unix") m_value |= Op::Unix;
      else if (c[1] == "Past") m_value |= Op::Past;
      else if (c[1] == "KeepArg") m_value |= Op::KeepArg;
      else if (c[1] == "KeepReg") m_value |= Op::KeepReg;
      else return -off;
      off += c[1].length();
      if (!ZtREGEX("\G\s*\|").m(s, c, off)) break;
      off += c[1].length();
    }
    if (!ZtREGEX("\G\s*\]").m(s, c, off)) return -off;
    off += c[1].length();
  }
  if (ZtREGEX("\G\s*\(").m(s, c, off)) {
    off += c[1].length();
    if (ZtREGEX("\G\s*(-?\d+)").m(s, c, off)) {
      off += c[1].length();
      uint16_t arg = ZuBox<int16_t>{c[2]};
      m_value |= (uint64_t(arg)<<16);
    } else
      m_value |= (uint64_t(uint16_t(Cmd::nullArg()))<<16);
    if (ZtREGEX("\G\s*,").m(s, c, off)) {
      off += c[1].length();
      int32_t vkey;
      off = VKey_parse(vkey, s, off);
      if (off <= 0) return off;
      m_value |= uint64_t(vkey)<<32;
    } else
      m_value |= uint64_t(-VKey::Null)<<32;
    if (!ZtREGEX("\G\s*\)").m(s, c, off)) return -off;
    off += c[1].length();
  } else
    m_value |=
      uint64_t(nullArg())<<16 |
      uint64_t(-VKey::Null)<<32;
  return off;
}

namespace {
const auto &comment() { return ZtREGEX("\G\s*#[^\n]*\n"); }

int CmdSeq_parse(CmdSeq &cmds, ZuCSpan s, int off)
{
  ZtRegexCaptures(c, 0);
  while (comment().m(s, c, off)) off += c[1].length();
  if (!ZtREGEX("\G\s*{").m(s, c, off)) return -off;
  off += c[1].length();
  Cmd cmd;
  while (comment().m(s, c, off)) off += c[1].length();
  while (!ZtREGEX("\G\s*}").m(s, c, off)) {
    off = cmd.parse(s, off);
    if (off <= 0) return off;
    if (!ZtREGEX("\G\s*;").m(s, c, off)) return -off;
    off += c[1].length();
    cmds.push(ZuMv(cmd));
    while (comment().m(s, c, off)) off += c[1].length();
  }
  off += c[1].length();
  return off;
}
} // anon namespace

int Map_::parseMode(ZuCSpan s, int off)
{
  ZtRegexCaptures(c, 1);
  while (comment().m(s, c, off)) off += c[1].length();
  if (!ZtREGEX("\G\s*mode\s+(\d+)").m(s, c, off)) return -off;
  off += c[1].length();
  unsigned mode = ZuBox<unsigned>{c[2]};
  int type = 0;
  if (ZtREGEX("\G\s+(\w+)").m(s, c, off)) {
    auto type_ = ModeType::lookup(c[2]);
    if (type_ < 0) return -off;
    type = type_;
    off += c[1].length();
  } else
    type = ModeType::Edit;
  ZtArray<unsigned> baseModes;
  if (ZtREGEX("\G\s+:\s+(\d+)").m(s, c, off)) {
    do {
      baseModes.push(ZuBox<unsigned>{c[2]}.val());
      off += c[1].length();
    } while (ZtREGEX("\G\s*,\s*(\d+)").m(s, c, off));
  }
  while (comment().m(s, c, off)) off += c[1].length();
  if (!ZtREGEX("\G\s*{").m(s, c, off)) return -off;
  off += c[1].length();
  addMode(mode, type);
  for (auto baseMode : baseModes)
    if (baseMode < modes.length()) {
      auto i = modes[baseMode].bindings->citer();
      while (auto binding = i.val())
	bind(mode, binding->vkey, binding->cmds);
    }
  int32_t vkey;
  CmdSeq cmds;
  while (comment().m(s, c, off)) off += c[1].length();
  while (!ZtREGEX("\G\s*}").m(s, c, off)) {
    off = VKey_parse(vkey, s, off);
    if (off <= 0) return off;
    off = CmdSeq_parse(cmds, s, off);
    if (off <= 0) return off;
    bind(mode, vkey, ZuMv(cmds));
    cmds.clear();
    while (comment().m(s, c, off)) off += c[1].length();
  }
  off += c[1].length();
  return off;
}

int Map_::parse(ZuCSpan s, int off)
{
  ZtRegexCaptures(c, 1);
  while (comment().m(s, c, off)) off += c[1].length();
  if (!ZtREGEX("\G\s*map\s+(\w+)").m(s, c, off)) return -off;
  off += c[1].length();
  id = c[2];
  while (comment().m(s, c, off)) off += c[1].length();
  if (!ZtREGEX("\G\s*{").m(s, c, off)) return -off;
  off += c[1].length();
  while (!ZtREGEX("\G\s*}").m(s, c, off)) {
    off = parseMode(s, off);
    if (off <= 0) return off;
    while (comment().m(s, c, off)) off += c[1].length();
  }
  off += c[1].length();
  return off;
}

void Map_printCmdSeq(const CmdSeq &cmds, ZuVStream &s)
{
  if (!cmds)
    s << "{}";
  else {
    s << "{ ";
    for (const auto &cmd : cmds) s << cmd << "; ";
    s << "}";
  }
}

void Binding::print_(ZuVStream &s) const
{
  s << VKey::print(vkey) << ' ';
  Map_printCmdSeq(cmds, s);
}

unsigned &Map_printIndentLevel() {
  return ZmTLS<unsigned, Map_printIndentLevel>();
}

void Map_printIndent(ZuVStream &s)
{
  unsigned level = Map_printIndentLevel();
  for (unsigned i = 0; i < level; i++) s << "  ";
}

ZuDerive(BindingPtrs,
  (ZtArray<const Binding *, ZtArrayHeapID<"Zrl.BindingPtr">>));

void Map_printMode(unsigned i, const Mode &mode, ZuVStream &s)
{
  auto &indentLevel = Map_printIndentLevel();
  Map_printIndent(s); s << "mode " << ZuBoxed(i);
  if (mode.type != ModeType::Edit) s << ' ' << ModeType::name(mode.type);
  s << " {\r\n";
  ++indentLevel;
  if (mode.bindings) {
    unsigned n = 0;
    for (auto i = mode.bindings->citer(); i.val(); ) ++n;
    auto bindings = ZtScratch(BindingPtrs, n);
    if (bindings) {
      for (auto i = mode.bindings->citer(); auto binding = i.val(); )
	bindings.push(binding);
      ZuSort(&bindings[0], bindings.length(),
	  [](const Binding *b1, const Binding *b2) -> int {
	    auto order = [](int32_t vkey) -> int32_t {
	      return vkey < 0 ? ((-vkey) - INT_MAX) : vkey;
	    };
	    return ZuCompare(order(b1->vkey), order(b2->vkey));
	  });
      bindings.all([&s](const Binding *binding) {
	Map_printIndent(s); s << *binding << "\r\n";
      });
    }
  }
  --indentLevel;
  Map_printIndent(s); s << "}\r\n";
}

void Map_::print_(ZuVStream &s) const
{
  auto &indentLevel = Map_printIndentLevel();
  Map_printIndent(s); s << "map " << id << " {\r\n";
  ++indentLevel;
  for (unsigned i = 0, n = modes.length(); i < n; i++)
    Map_printMode(i, modes[i], s);
  --indentLevel;
  Map_printIndent(s); s << "}\r\n";
}

void Editor::dumpMaps_(ZuVStream &s) const
{
  auto i = m_maps.citer();
  while (auto map = i()) s << *map;
  s << *m_defltMap;
}

void Map_::addMode(unsigned id, int type)
{
  modes.grow(id + 1);
  modes[id] = Mode{{}, type};
}
void Map_::bind(unsigned id, int vkey, CmdSeq cmds)
{
  modes.grow(id + 1);
  auto &mode = modes[id];
  if (!mode.bindings) mode.bindings = new Bindings{};
  mode.bindings->del(vkey);
  mode.bindings->add(new Binding{vkey, ZuMv(cmds)});
}
void Map_::reset()
{
  modes.clear();
}

void Editor::loadMap(ZuCSpan file, bool select)
{
  ZtArray<char, ZtArrayHeapID<"Zrl.Editor.Map", ZtArraySharded<true>>> s;
  {
    ZiFile f;
    int r;
    if ((r = f.open(file, ZiFile::ReadOnly | ZiFile::GC, 0666, 0)) != Zi::OK)
      throw ZeEXCEPT(Error, "ZrlEditor",
	([file = ZeString{file}, r, e = f.error()](auto &s) {
	  s << "open(\"" << file << "\"): " << Zi::ioResult(r);
	  if (r == Zi::IOError) s << " - " << e;
	}));
    unsigned len = f.size();
    if (len >= m_config.maxFileSize)
      throw ZeEXCEPT(Error, "ZrlEditor", ([file = ZeString{file}](auto &s) {
	s << "open(\"" << file << "\"): file too large";
      }));
    s.length(len);
    if ((r = f.read(s.data(), len)) < 0)
      throw ZeEXCEPT(Error, "ZrlEditor",
	([file = ZeString{file}, r, e = f.error()](auto &s) {
	  s << "read(\"" << file << "\"): " << Zi::ioResult(r);
	  if (r == Zi::IOError) s << " - " << e;
	}));
    if (r < len)
      throw ZeEXCEPT(Error, "ZrlEditor", ([file = ZeString{file}](auto &s) {
	s << "read(\"" << file << "\"): truncated read";
      }));
  }
  ZuPtr<Map> map = new Map{};
  int off = map->parse(s, 0);
  if (off <= 0) {
    off = -off;
    unsigned lpos = 0, line = 1;
    ZtRegexCaptures(c, 0);
    while (ZtREGEX("\G[^\n]*\n").m(s, c, lpos)) {
      unsigned npos = lpos + c[1].length();
      if (npos > off) break;
      lpos = npos; line++;
    }
    ZuCSpan region(s);
    region.offset(off - 8);
    region.trunc(16);
    throw ZeEXCEPT(Error, "ZrlEditor",
      ([file = ZeString{file}, line, region = ZeString{region}](auto &s) {
	s << '"' << file << "\":" << line
	  << ": parsing failed near \"" << region << '"';
      }));
  }
  if (!map->modes.length())
    throw ZeEXCEPT(Error, "ZrlEditor", ([file = ZeString{file}](auto &s) {
      s << '"' << file << "\": mode 0 not defined";
    }));
  if (select) m_map = map.ptr();
  m_maps.del(map->id);
  m_maps.addNode(ZuMv(map));
}

void Editor::open(ZmScheduler *sched, unsigned thread)
{
  m_tty.open(sched, thread,
      [this](bool ok) { m_app.open(ok); },
      [this](ZuCSpan s) { m_app.error(s); });
}
void Editor::close()
{
  m_tty.close([this]() { m_app.close(); });
}
bool Editor::isOpen() const
{
  return m_tty.isOpen();
}

// start/stop
void Editor::start(StartFn fn)
{
  m_tty.start(
    [this, fn = ZuMv(fn)]() mutable {
      m_context = { .histSaveOff = m_config.histOffset };
      m_tty.opost_on();
      fn(*this);
      m_tty.opost_off();
      prompt_();
    },
    Terminal::KeyFn{this, [](Editor *this_, int32_t vkey) {
      return this_->process(vkey);
    }});
}
void Editor::stop()
{
  m_tty.stop();
}
bool Editor::running() const
{
  return m_tty.running();
}

void Editor::prompt_()
{
  m_app.prompt(m_context.prompt);
  auto span = ZuUTF<uint32_t, uint8_t>::span(m_context.prompt);
  m_tty.splice(0, ZuUTFSpan(), m_context.prompt, span, true);
  m_tty.write();
  m_context.startPos = span.width();
}

bool Editor::map(ZuCSpan id)
{
  if (auto map = m_maps.find(id)) {
    cmdClrVis({ Op::ClrVis }, 0);
    m_context.mode = 0;
    m_context.stack.clear();
    m_map = map;
    return true;
  }
  return false;
}

bool Editor::process(int32_t vkey)
{
  if (!m_map) return false;
  if (vkey == -VKey::EndOfFile && m_tty.pos() > m_context.startPos)
    vkey = m_tty.literal(vkey);
  for (unsigned i = 0, n = m_config.maxSynVKey; i < n; i++) {
    m_context.synVKey = -VKey::Null;
    if (process_(vkey)) {
      m_context.synVKey = -VKey::Null;
      return true;
    }
    if ((vkey = m_context.synVKey) == -VKey::Null) return false;
  }
  m_context.synVKey = -VKey::Null;
  return false;
}

bool Editor::process_(int32_t vkey)
{
  const auto &mode = m_map->modes[m_context.mode];
  if (mode.bindings) {
    if (auto ptr = mode.bindings->findVal(vkey))
      return process__(ptr->cmds, vkey);
    {
      int32_t lkey = m_tty.literal(vkey);
      if (lkey != vkey) {
	if (auto ptr = mode.bindings->findVal(lkey))
	  return process__(ptr->cmds, lkey);
      }
    }
    if (auto ptr = mode.bindings->findVal(VKey::wildcard(vkey)))
      return process__(ptr->cmds, vkey);
  }
  return false;
}

bool Editor::process__(const CmdSeq &cmds, int32_t vkey)
{
  for (auto cmd : cmds) {
    int op = cmd.op() & Op::Mask;
    if (op != Op::ArgDigit) m_context.accumArg();
    switch (op) {
      case Op::Nop:
      case Op::InsToggle:
      case Op::Insert:
      case Op::Over:
      case Op::Glyph:
      case Op::InsGlyph:
      case Op::OverGlyph:
      case Op::BackSpace:
      case Op::EditRep:
	break;
      default:
	m_context.applyEdit();
	break;
    }
    int32_t vkey_ = cmd.vkey();
    if (vkey_ == -VKey::Null)
      vkey_ = vkey;
    else if (vkey_ == -VKey::Unset)
      vkey_ = -VKey::Null;
    // clear completion context
    switch (m_context.prevCmd.op()) {
      case Op::Complete:
      case Op::RevComplete:
      case Op::ListComplete:
	if (op != Op::Complete &&
	    op != Op::RevComplete &&
	    op != Op::ListComplete)
	  finalComplete();
	break;
    }
    bool stop = false;
    if (auto fn = m_cmdFn[op]) {
      stop = (this->*fn)(cmd, vkey_);
      m_context.prevCmd = cmd;
    }
    if (!(cmd.op() & Op::KeepReg)) m_context.register_ = -1;
    if (Cmd::nullArg(cmd.arg()) &&
	!(cmd.op() & Op::KeepArg)) m_context.clrArg();
    if (stop) {
      m_app.end();
      return true;
    }
  }
  if (m_tty.write() != Zi::OK) {
    m_app.end();
    return true;
  }
  return false;
}

bool Editor::cmdNop(Cmd, int32_t)
{
  return false;
}

bool Editor::cmdSyn(Cmd, int32_t vkey)
{
  m_context.synVKey = vkey;
  return false;
}

bool Editor::cmdMode(Cmd cmd, int32_t)
{
  int arg = cmd.arg();
  if (!Cmd::nullArg(arg) && arg >= 0) m_context.mode = arg;
  return false;
}
bool Editor::cmdPush(Cmd cmd, int32_t)
{
  int arg = cmd.arg();
  if (ZuLikely(m_context.stack.length() < m_config.maxStackDepth)) {
    m_context.stack.push(m_context.mode);
    if (!Cmd::nullArg(arg) && arg >= 0) m_context.mode = arg;
  }
  return false;
}
bool Editor::cmdPop(Cmd cmd, int32_t)
{
  int arg = cmd.arg();
  if (Cmd::nullArg(arg) || arg < 0) arg = 1;
  if (arg > m_context.stack.length()) arg = m_context.stack.length();
  if (arg > 0) {
    while (arg-- > 1) m_context.stack.pop();
    m_context.mode = m_context.stack.pop();
  }
  return false;
}

bool Editor::cmdError(Cmd, int32_t) {
  m_tty.opost_on();
  m_app.end();
  m_tty.opost_off();
  return true;
}
bool Editor::cmdEndOfFile(Cmd, int32_t) {
  m_tty.opost_on();
  m_app.end();
  m_tty.opost_off();
  return true;
}

bool Editor::cmdSigInt(Cmd, int32_t) {
  m_tty.opost_on();
  bool stop = m_app.sig(SIGINT);
  m_tty.opost_off();
  return stop;
}
bool Editor::cmdSigQuit(Cmd, int32_t) {
  m_tty.opost_on();
  bool stop = m_app.sig(SIGQUIT);
  m_tty.opost_off();
  return stop;
}
bool Editor::cmdSigSusp(Cmd, int32_t) {
  m_tty.opost_on();
  bool stop = m_app.sig(SIGTSTP);
  m_tty.opost_off();
  return stop;
}

bool Editor::cmdEnter(Cmd, int32_t)
{
  m_tty.mv(m_tty.line().width());
  m_tty.crnl_();
  m_tty.write();

  const auto &data = m_tty.line().data();
  ZuBSpan s(data.data(), data.length());
  s.offset(m_context.startPos);
  if (s) m_app.histSave(m_context.histSaveOff++, s);

  m_context.reset();

  m_tty.opost_on();
  bool stop = m_app.enter(s);
  m_tty.opost_off();
  m_tty.clear();

  if (stop) return true;
  prompt_();
  return false;
}

ZuBSpan Editor::substr(unsigned begin, unsigned end)
{
  const auto &data = m_tty.line().data();
  return {&data[begin], end - begin};
}

bool Editor::commandMode()
{
  return m_map->modes[m_context.mode].type == ModeType::Command;
}

// align cursor within line if not in an edit mode
unsigned Editor::align(unsigned pos)
{
  const auto &line = m_tty.line();
  if (ZuUnlikely(commandMode() &&
      pos > m_context.startPos &&
      pos >= line.width())) {
    return line.align(pos - 1);
  }
  return pos;
}

// splice data in line
void Editor::splice(
    unsigned off, ZuUTFSpan span,
    ZuBSpan replace, ZuUTFSpan rspan,
    bool append, bool last)
{
  m_context.appendEdit();
  m_context.undo.set(m_context.undoNext++,
      UndoOp{
	static_cast<int>(m_tty.pos()),
	static_cast<int>(off),
	substr(off, off + span.inLen()), replace, last});
  splice_(off, span, replace, rspan, append);
}
void Editor::splice_(
    unsigned off, ZuUTFSpan span,
    ZuBSpan replace, ZuUTFSpan rspan,
    bool append)
{
  // line is being modified
  m_context.horizPos = -1;
  m_context.histLoadOff = -1;

  const auto &line = m_tty.line();
  if (rspan.inLen() <= span.inLen() ||
      line.length() + (rspan.inLen() - span.inLen()) < m_config.maxLineLen)
    m_tty.splice(off, span, replace, rspan, append);
}

void Editor::motion(
    unsigned op, unsigned pos,
    unsigned begin, unsigned end,
    unsigned begPos, unsigned endPos)
{
  ZuBSpan s;
  if (op & (Op::Copy | Op::Del | Op::Draw | Op::Vis))
    s = substr(begin, end);
  if (op & Op::Copy) {
    int index = m_context.register_;
    if (index >= 0)
      m_context.registers.set(index) = s;
    else
      m_context.registers.vi_yank() = s;
  }
  if (op & Op::Del) {
    auto span = ZuUTF<uint32_t, uint8_t>::span(s);
    m_context.histLoadOff = -1;
    const auto &line = m_tty.line();
    // map pos to off, adjust off for the splice, then map off back to new pos
    unsigned off = line.position(pos).mapping();
    if (off > begin) {
      if (off > end)
	off -= (end - begin);
      else
	off = begin;
    }
    m_tty.mv(begPos);
    splice(begin, span,
	ZuBSpan(), ZuUTFSpan(),
	true, (op & (Op::Del | Op::Vis)) != (Op::Del | Op::Vis));
    if (m_context.highPos >= 0) {
      m_tty.cursor_on();
      m_context.markPos = m_context.highPos = -1;
    }
    pos = line.byte(off).mapping();
  } else if (op & (Op::Draw | Op::Vis)) {
    if (!(op & Op::Vis)) {
      m_tty.mv(begPos);
      m_tty.redraw(endPos, false);
      m_tty.cursor_on();
      m_context.markPos = m_context.highPos = -1;
    } else if (m_context.highPos < 0) {
      m_tty.mv(begPos);
      m_tty.redraw(endPos, true);
      m_tty.cursor_off();
      m_context.markPos = begPos;
      m_context.highPos = endPos;
    } else {
      if (pos == endPos && begPos == m_context.highPos) { // extend fwd
	m_tty.mv(begPos);
	m_tty.redraw(endPos, true);
	m_context.highPos = endPos;
      } else if (pos == begPos && endPos == m_context.markPos) { // extend rev
	m_tty.mv(begPos);
	m_tty.redraw(endPos, true);
	m_context.markPos = begPos;
      } else if (pos == begPos && endPos == m_context.highPos) { // contract fwd
	m_tty.mv(begPos);
	if (begPos == m_context.markPos) {
	  m_tty.redraw(m_context.highPos, false);
	  m_tty.cursor_on();
	  m_context.markPos = m_context.highPos = -1;
	} else if (begPos < m_context.markPos) {
	  m_tty.redraw(m_context.markPos, true);
	  m_tty.redraw(m_context.highPos, false);
	  m_context.highPos = m_context.markPos;
	  m_context.markPos = begPos;
	} else {
	  m_tty.redraw(m_context.highPos, false);
	  m_context.highPos = begPos;
	}
      } else if (pos == endPos && begPos == m_context.markPos) { // contract rev
	m_tty.mv(begPos);
	if (endPos == m_context.highPos) {
	  m_tty.redraw(endPos, false);
	  m_tty.cursor_on();
	  m_context.markPos = m_context.highPos = -1;
	} else if (endPos > m_context.highPos) {
	  m_tty.redraw(m_context.highPos, false);
	  m_tty.redraw(endPos, true);
	  m_context.markPos = m_context.highPos;
	  m_context.highPos = endPos;
	} else {
	  m_tty.redraw(endPos, false);
	  m_context.markPos = endPos;
	}
      } else {
	m_tty.mv(m_context.markPos);
	m_tty.redraw(m_context.highPos, false);
	m_tty.mv(begPos);
	m_tty.redraw(endPos, true);
	m_context.markPos = begPos;
	m_context.highPos = endPos;
      }
    }
  }
  m_tty.mv(align(pos));
}

// maintains consistent horizontal position during vertical movement
unsigned Editor::horizPos()
{
  unsigned prevOp = m_context.prevCmd.op();
  if (m_context.horizPos < 0 || (prevOp != Op::Up && prevOp != Op::Down))
    m_context.horizPos = m_tty.pos();
  return m_context.horizPos;
}

bool Editor::cmdUp(Cmd cmd, int32_t vkey)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) return cmdDown(cmd.negArg(), vkey);
  unsigned pos = horizPos();
  unsigned width = m_tty.width();
  if (arg <= 1 && (cmd.op() & (Op::Mv | Op::Del | Op::Copy)) == Op::Mv &&
      pos < m_context.startPos + width)
    return cmdPrev(cmd, vkey);
  if (arg < 1) arg = 1;
  const auto &line = m_tty.line();
  unsigned end = line.position(pos).mapping();
  unsigned endPos = pos;
  if (pos < m_context.startPos + width * arg) {
    pos = (m_context.startPos - (m_context.startPos % width)) + (pos % width);
    if (pos < m_context.startPos) pos += width;
  } else
    pos -= width * arg;
  m_context.horizPos = pos;
  pos = line.align(pos);
  unsigned begin = line.position(pos).mapping();
  if (begin < end) {
    unsigned begPos = pos;
    if (!(cmd.op() & Op::Mv)) pos = m_tty.pos();
    motion(cmd.op(), pos, begin, end, begPos, endPos);
  }
  return false;
}

bool Editor::cmdDown(Cmd cmd, int32_t vkey)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) return cmdUp(cmd.negArg(), vkey);
  unsigned pos = horizPos();
  unsigned width = m_tty.width();
  const auto &line = m_tty.line();
  unsigned finPos = line.width();
  if (arg <= 1 && (cmd.op() & (Op::Mv | Op::Del | Op::Copy)) == Op::Mv &&
      pos + width > finPos)
    return cmdNext(cmd, vkey);
  if (arg < 1) arg = 1;
  unsigned begin = line.position(pos).mapping();
  unsigned begPos = pos;
  if (pos + width * arg > finPos) {
    pos = (finPos - (finPos % width)) + (pos % width);
    if (pos > finPos) {
      if (pos > width)
	pos -= width;
      else
	pos = finPos;
    }
  } else
    pos += width * arg;
  m_context.horizPos = pos;
  pos = line.align(pos);
  unsigned end = line.position(pos).mapping();
  if (begin < end) {
    unsigned endPos = pos;
    if (!(cmd.op() & Op::Mv)) pos = m_tty.pos();
    motion(cmd.op(), pos, begin, end, begPos, endPos);
  }
  return false;
}

bool Editor::cmdLeft(Cmd cmd, int32_t vkey)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) return cmdRight(cmd.negArg(), vkey);
  unsigned pos = m_tty.pos();
  if (pos > m_context.startPos) {
    const auto &line = m_tty.line();
    unsigned end = line.position(pos).mapping(), begin = end;
    unsigned endPos = pos;
    unsigned start = line.position(m_context.startPos).mapping();
    do {
      begin = line.revGlyph(begin);
      if (begin < start) begin = start;
    } while (begin > start && --arg > 0);
    pos = line.byte(begin).mapping();
    if (begin < end) {
      unsigned begPos = pos;
      if (!(cmd.op() & Op::Mv)) pos = m_tty.pos();
      motion(cmd.op(), pos, begin, end, begPos, endPos);
    }
  }
  return false;
}

bool Editor::cmdRight(Cmd cmd, int32_t vkey)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) return cmdLeft(cmd.negArg(), vkey);
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  if (pos < line.width()) {
    unsigned begin = line.position(pos).mapping(), end = begin;
    unsigned begPos = pos;
    unsigned final_ = line.length();
    do {
      end = line.fwdGlyph(end);
      if (end > final_) end = final_;
    } while (end < final_ && --arg > 0);
    pos = line.byte(end).mapping();
    if (begin < end) {
      unsigned endPos = pos;
      if (!(cmd.op() & Op::Mv)) pos = m_tty.pos();
      motion(cmd.op(), pos, begin, end, begPos, endPos);
    }
  }
  return false;
}

bool Editor::cmdHome(Cmd cmd, int32_t vkey)
{
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  unsigned end = line.position(pos).mapping();
  unsigned endPos = pos;
  pos = m_context.startPos;
  unsigned begin = line.position(pos).mapping();
  if (begin < end) {
    unsigned begPos = pos;
    if (!(cmd.op() & Op::Mv)) pos = m_tty.pos();
    motion(cmd.op(), pos, begin, end, begPos, endPos);
  }
  return false;
}

bool Editor::cmdEnd(Cmd cmd, int32_t vkey)
{
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  unsigned begin = line.position(pos).mapping();
  unsigned begPos = pos;
  pos = line.width();
  unsigned end = line.length();
  if (begin < end) {
    unsigned endPos = pos;
    if (!(cmd.op() & Op::Mv)) pos = m_tty.pos();
    motion(cmd.op(), pos, begin, end, begPos, endPos);
  }
  return false;
}

bool Editor::cmdFwdWord(Cmd cmd, int32_t vkey)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) return cmdRevWord(cmd.negArg(), vkey);
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  if (pos < line.width()) {
    unsigned begin = line.position(pos).mapping(), end = begin;
    unsigned begPos = pos;
    unsigned final_ = line.length();
    do {
      if (cmd.op() & Op::Unix)
	end = line.fwdUnixWord(end);
      else
	end = line.fwdWord(end);
      if (end > final_) end = final_;
    } while (end < final_ && --arg > 0);
    if (begin < end) {
      unsigned endPos = line.byte(end).mapping();
      if (cmd.op() & Op::Mv) pos = endPos;
      motion(cmd.op(), pos, begin, end, begPos, endPos);
    }
  }
  return false;
}

bool Editor::cmdRevWord(Cmd cmd, int32_t vkey)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) return cmdFwdWord(cmd.negArg(), vkey);
  unsigned pos = m_tty.pos();
  if (pos > m_context.startPos) {
    const auto &line = m_tty.line();
    unsigned end = line.position(pos).mapping(), begin = end;
    unsigned endPos = pos;
    unsigned start = line.position(m_context.startPos).mapping();
    do {
      if (cmd.op() & Op::Unix)
	begin = line.revUnixWord(begin);
      else
	begin = line.revWord(begin);
      if (begin < start) begin = start;
    } while (begin > start && --arg > 0);
    if (begin < end) {
      unsigned begPos = line.byte(begin).mapping();
      if (cmd.op() & Op::Mv) pos = begPos;
      motion(cmd.op(), pos, begin, end, begPos, endPos);
    }
  }
  return false;
}

bool Editor::cmdFwdWordEnd(Cmd cmd, int32_t vkey)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) return cmdRevWordEnd(cmd.negArg(), vkey);
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  if (pos < line.width()) {
    unsigned begin = line.position(pos).mapping(), end = begin;
    unsigned begPos = pos;
    unsigned final_ = line.length();
    do {
      if (cmd.op() & Op::Unix)
	end = line.fwdUnixWordEnd(end, cmd.op() & Op::Past);
      else
	end = line.fwdWordEnd(end, cmd.op() & Op::Past);
      if (end > final_) end = final_;
    } while (end < final_ && --arg > 0);
    if (begin < end) {
      unsigned endPos = line.byte(end).mapping();
      if (cmd.op() & Op::Mv) pos = endPos;
      motion(cmd.op(), pos, begin, end, begPos, endPos);
    }
  }
  return false;
}

bool Editor::cmdRevWordEnd(Cmd cmd, int32_t vkey)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) return cmdFwdWordEnd(cmd.negArg(), vkey);
  unsigned pos = m_tty.pos();
  if (pos > m_context.startPos) {
    const auto &line = m_tty.line();
    unsigned end = line.position(pos).mapping(), begin = end;
    unsigned endPos = pos;
    unsigned start = line.position(m_context.startPos).mapping();
    do {
      if (cmd.op() & Op::Unix)
	begin = line.revUnixWordEnd(begin, cmd.op() & Op::Past);
      else
	begin = line.revWordEnd(begin, cmd.op() & Op::Past);
      if (begin < start) begin = start;
    } while (begin > start && --arg > 0);
    if (begin < end) {
      unsigned begPos = line.byte(begin).mapping();
      if (cmd.op() & Op::Mv) pos = begPos;
      motion(cmd.op(), pos, begin, end, begPos, endPos);
    }
  }
  return false;
}

bool Editor::cmdMvMark(Cmd cmd, int32_t vkey)
{
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  auto posIndex = line.position(pos);
  if (ZuLikely(pos != m_context.markPos)) {
    unsigned begin, end, begPos, endPos;
    if (pos > m_context.markPos) {
      end = posIndex.mapping();
      endPos = pos;
      pos = m_context.markPos;
      begPos = pos;
      posIndex = line.position(pos);
      begin = posIndex.mapping();
    } else {
      begin = posIndex.mapping();
      begPos = pos;
      pos = m_context.markPos;
      endPos = pos;
      posIndex = line.position(pos);
      pos -= posIndex.off();
      end = posIndex.mapping();
    }
    if (begin < end) {
      if (!(cmd.op() & Op::Mv)) pos = m_tty.pos();
      motion(cmd.op(), pos, begin, end, begPos, endPos);
    }
  }
  return false;
}

bool Editor::cmdInsToggle(Cmd, int32_t)
{
  m_context.overwrite = !m_context.overwrite;
  return false;
}
bool Editor::cmdInsert(Cmd, int32_t)
{
  m_context.overwrite = false;
  return false;
}
bool Editor::cmdOver(Cmd, int32_t)
{
  m_context.overwrite = true;
  return false;
}

bool Editor::cmdClrVis(Cmd cmd, int32_t)
{
  const auto &line = m_tty.line();
  if (m_context.highPos >= 0) {
    motion(cmd.op() | Op::Draw | Op::Mv,
	m_tty.pos(),
	line.position(m_context.markPos).mapping(),
	line.position(m_context.highPos).mapping(),
	m_context.markPos, m_context.highPos);
  }
  return false;
}

bool Editor::cmdClear(Cmd, int32_t)
{
  m_tty.cls();
  return false;
}
bool Editor::cmdRedraw(Cmd, int32_t)
{
  m_tty.redraw();
  return false;
}

bool Editor::cmdPaste(Cmd cmd, int32_t)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg <= 0) return false;
  int index = m_context.register_;
  if (index < 0) index = 0;
  if (const auto &data = m_context.registers.get(index)) {
    const auto &line = m_tty.line();
    unsigned off = line.position(m_tty.pos()).mapping();
    auto rspan = ZuUTF<uint32_t, uint8_t>::span(data);
    bool append = !commandMode();
    for (int i = 0; i < arg; i++) {
      bool last = i == arg - 1;
      if (ZuUnlikely(m_context.overwrite)) {
	ZuBSpan removed(line.data());
	removed.offset(off);
	auto span = ZuUTF<uint32_t, uint8_t>::nspan(removed, rspan.outLen());
	splice(off, span, data, rspan, append || !last, last);
      } else {
	splice(off, ZuUTFSpan(), data, rspan, append || !last, last);
      }
      off += rspan.inLen();
    }
  }
  return false;
}

bool Editor::cmdYank(Cmd cmd, int32_t)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg <= 0) return false;
  for (int i = 1; i < arg; i++) m_context.registers.emacs_rotateFwd();
  if (const auto &data = m_context.registers.emacs_yank()) {
    const auto &line = m_tty.line();
    unsigned off = line.position(m_tty.pos()).mapping();
    auto rspan = ZuUTF<uint32_t, uint8_t>::span(data);
    if (m_context.overwrite) {
      ZuBSpan removed(line.data());
      removed.offset(off);
      auto span = ZuUTF<uint32_t, uint8_t>::nspan(removed, rspan.outLen());
      splice(off, span, data, rspan, true);
    } else {
      splice(off, ZuUTFSpan(), data, rspan, true);
    }
  }
  return false;
}
bool Editor::cmdRotate(Cmd cmd, int32_t)
{
  if (m_context.prevCmd.op() != Op::Yank) return false;
  const auto &data = m_context.registers.emacs_yank();
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) {
    arg = -arg;
    for (int i = 0; i < arg; i++) m_context.registers.emacs_rotateRev();
  } else {
    for (int i = 0; i < arg; i++) m_context.registers.emacs_rotateFwd();
  }
  const auto &replace = m_context.registers.emacs_yank();
  if (&data == &replace) return false;
  auto span = ZuUTF<uint32_t, uint8_t>::span(data);
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  auto rspan = ZuUTF<uint32_t, uint8_t>::span(replace);
  if (rspan.inLen() <= span.inLen() ||
      line.length() + (rspan.inLen() - span.inLen()) < m_config.maxLineLen) {
    unsigned width = span.width();
    if (pos < m_context.startPos + width)
      pos = m_context.startPos;
    else
      pos -= width;
    m_tty.mv(pos);
    splice(line.position(pos).mapping(), span, replace, rspan, true);
  }
  return false;
}

void Editor::edit(
    ZuBSpan replace, ZuUTFSpan rspan, bool overwrite)
{
  const auto &line = m_tty.line();
  unsigned pos = m_tty.pos();
  unsigned off = line.position(pos).mapping();
  m_context.edit(pos, off);
  if (overwrite) {
    ZuBSpan removed(line.data());
    removed.offset(off);
    auto span = ZuUTF<uint32_t, uint8_t>::nspan(removed, rspan.outLen());
    removed.trunc(span.inLen());
    m_context.editOp.oldData << removed;
    m_context.editOp.newData << replace;
    splice_(off, span, replace, rspan, true);
  } else {
    m_context.editOp.newData << replace;
    splice_(off, ZuUTFSpan(), replace, rspan, true);
  }
}

bool Editor::glyph(Cmd cmd, int32_t vkey, bool overwrite)
{
  vkey = m_tty.literal(vkey);
  if (ZuUnlikely(vkey < 0)) return false;
  ZuArray<uint8_t, 4> replace;
  replace.length(ZuUTF8::out(replace.data(), 4, vkey));
  edit(replace, ZuUTF<uint32_t, uint8_t>::span(replace), overwrite);
  return false;
}
bool Editor::cmdGlyph(Cmd cmd, int32_t vkey)
{
  return glyph(cmd, vkey, m_context.overwrite);
}
bool Editor::cmdInsGlyph(Cmd cmd, int32_t vkey)
{
  return glyph(cmd, vkey, false);
}
bool Editor::cmdOverGlyph(Cmd cmd, int32_t vkey)
{
  return glyph(cmd, vkey, true);
}

bool Editor::cmdBackSpace(Cmd, int32_t vkey)
{
  unsigned pos = m_tty.pos();
  if (pos <= m_context.startPos) return false;
  if (m_context.editOp) {
    const auto &line = m_tty.line();
    unsigned end = line.position(pos).mapping(), begin = end;
    unsigned start = line.position(m_context.startPos).mapping();
    begin = line.revGlyph(begin);
    if (begin < start) begin = start;
    if (begin < end) {
      unsigned begPos = line.byte(begin).mapping();
      if (begPos >= m_context.editOp.oldPos) {
	auto &newData = m_context.editOp.newData;
	unsigned n = newData.length();
	if (n > end - begin) {
	  m_tty.mv(begPos);
	  unsigned newLength = n - (end - begin);
	  ZuBSpan removed(newData);
	  removed.offset(newLength);
	  auto span = ZuUTF<uint32_t, uint8_t>::span(removed);
	  newData.length(newLength);
	  if (auto &oldData = m_context.editOp.oldData) {
	    n = oldData.length();
	    unsigned off = n;
	    while (--off > 0 && !ZuUTF8::initial(oldData[off]));
	    ZuBSpan restore(oldData);
	    restore.offset(off);
	    splice_(begin, span,
		restore, ZuUTF<uint32_t, uint8_t>::span(restore), true);
	    oldData.splice(off, n - off);
	  } else {
	    splice_(begin, span, ZuBSpan(), ZuUTFSpan(), true);
	  }
	  return false;
	}
      }
    }
    m_context.clrEdit();
  }
  return cmdLeft(Cmd{Op::Left | Op::Del}, vkey);
}

bool Editor::cmdEdit(Cmd cmd, int32_t)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) arg = -arg;
  if (!arg) arg = 1;
  m_context.editArg = arg;
  return false;
}

bool Editor::cmdEditRep(Cmd, int32_t vkey)
{
  if (m_context.editOp && m_context.editArg > 1) {
    unsigned n = m_context.editArg - 1;
    ZuBSpan replace = m_context.editOp.newData;
    auto rspan = ZuUTF<uint32_t, uint8_t>::span(replace);
    bool overwrite = m_context.editOp.oldData;
    for (unsigned i = 0; i < n; i++) edit(replace, rspan, overwrite);
  }
  return false;
}

bool Editor::cmdArgDigit(Cmd cmd, int32_t vkey)
{
  auto arg = cmd.arg();
  if (!Cmd::nullArg(arg)) m_context.accumDigit(arg);
  return false;
}

bool Editor::cmdRegister(Cmd, int32_t vkey)
{
  if (vkey > 0 && vkey <= static_cast<int>(CHAR_MAX))
    m_context.register_ = m_context.registers.index(vkey);
  else
    m_context.register_ = -1;
  return false;
}

bool Editor::cmdUndo(Cmd, int32_t vkey)
{
  if (m_context.undoIndex < 0) {
    if (!m_context.undoNext) return false;
    m_context.undoIndex = m_context.undoNext;
    m_context.undoPos = m_tty.pos();
  } else {
    if (!m_context.undoIndex) return false;
  }
  int pos = -1;
  bool first = true;
  bool append = !commandMode();
  do {
    if (auto undoOp = m_context.undo.ptr(--m_context.undoIndex)) {
      if (!first && undoOp->last) {
	++m_context.undoIndex;
	break;
      }
      m_tty.mv(m_tty.line().byte(undoOp->spliceOff).mapping());
      splice_(undoOp->spliceOff,
	  ZuUTF<uint32_t, uint8_t>::span(undoOp->newData),
	  undoOp->oldData, ZuUTF<uint32_t, uint8_t>::span(undoOp->oldData),
	  append);
      pos = undoOp->oldPos;
    } else {
      ++m_context.undoIndex;
      break;
    }
    first = false;
  } while (m_context.undoIndex > 0);
  if (pos >= 0) m_tty.mv(align(pos));
  return false;
}

bool Editor::cmdRedo(Cmd cmd, int32_t vkey)
{
  if (m_context.undoIndex < 0) return cmdRedraw(cmd, vkey);
  bool append = !commandMode();
  do {
    if (auto undoOp = m_context.undo.ptr(m_context.undoIndex++)) {
      m_tty.mv(m_tty.line().byte(undoOp->spliceOff).mapping());
      splice_(undoOp->spliceOff,
	  ZuUTF<uint32_t, uint8_t>::span(undoOp->oldData),
	  undoOp->newData, ZuUTF<uint32_t, uint8_t>::span(undoOp->newData),
	  append);
      if (undoOp->last) break;
    } else
      break;
  } while (m_context.undoIndex < m_context.undoNext);
  if (m_context.undoIndex >= m_context.undoNext) {
    m_tty.mv(align(m_context.undoPos));
    m_context.undoIndex = -1;
    m_context.undoPos = -1;
  } else {
    unsigned pos;
    if (auto undoOp = m_context.undo.ptr(m_context.undoIndex))
      pos = undoOp->oldPos;
    else
      pos = m_tty.pos();
    m_tty.mv(align(pos));
  }
  return false;
}

bool Editor::cmdEmacsUndo(Cmd cmd, int32_t vkey)
{
  if (!m_context.emacsRedo) return cmdUndo(cmd, vkey);
  bool stop = cmdRedo(cmd, vkey);
  if (m_context.undoIndex < 0) m_context.emacsRedo = false;
  return stop;
}

bool Editor::cmdEmacsAbort(Cmd, int32_t vkey)
{
  if (m_context.undoIndex >= 0)
    m_context.emacsRedo = !m_context.emacsRedo;
  return false;
}

bool Editor::cmdRepeat(Cmd cmd, int32_t vkey)
{
  if (!m_context.undoNext) return false;
  unsigned i = m_context.undoNext;
  bool last = false;
  do {
    if (auto undoOp = m_context.undo.ptr(--i))
      last = undoOp->last;
    else
      last = true;
  } while (!last && i > 0);
  last = false;
  do {
    if (auto undoOp = m_context.undo.ptr(i)) {
      last = undoOp->last;
      splice(m_tty.line().position(m_tty.pos()).mapping(),
	  ZuUTF<uint32_t, uint8_t>::span(undoOp->oldData),
	  undoOp->newData, ZuUTF<uint32_t, uint8_t>::span(undoOp->newData),
	  true, last);
    } else
      last = true;
  } while (++i < m_context.undoNext && !last);
  return false;
}

bool Editor::cmdTransGlyph(Cmd, int32_t)
{
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  if (pos == line.width()) pos = line.align(pos - 1);
  if (pos <= m_context.startPos) return false;
  unsigned rbegin = line.position(pos).mapping();
  unsigned rend = rbegin + line.byte(rbegin).len();
  unsigned lbegin = line.revGlyph(rbegin);
  const auto &data = line.data();
  ZtArray<uint8_t> replace;
  replace.length(rend - lbegin);
  memcpy(&replace[0], &data[rbegin], rend - rbegin);
  memcpy(&replace[rend - rbegin], &data[lbegin], rbegin - lbegin);
  auto span = ZuUTF<uint32_t, uint8_t>::span(substr(lbegin, rend));
  splice(lbegin, span, replace, span, true);
  return false;
}
bool Editor::cmdTransWord(Cmd, int32_t)
{
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  if (pos == line.width()) pos = line.align(pos - 1);
  if (pos <= m_context.startPos) return false;
  unsigned start = line.position(m_context.startPos).mapping();
  unsigned rend = line.position(pos).mapping();
  rend = line.fwdWordEnd(rend, true);
  unsigned rbegin = line.revWord(rend);
  unsigned mbegin = rbegin;
  do {
    if (mbegin <= start) return false;
    mbegin = line.revWordEnd(mbegin, true);
  } while (!line.isword_(mbegin));
  unsigned lbegin = line.revWord(mbegin);
  if (lbegin <= start) return false;
  ZtArray<uint8_t> replace;
  replace.length(rend - lbegin);
  const auto &data = line.data();
  memcpy(&replace[0], &data[rbegin], rend - rbegin);
  memcpy(&replace[rend - rbegin], &data[mbegin], rbegin - mbegin);
  memcpy(&replace[rend - mbegin], &data[lbegin], mbegin - lbegin);
  auto span = ZuUTF<uint32_t, uint8_t>::span(substr(lbegin, rend));
  splice(lbegin, span, replace, span, true);
  return false;
}
bool Editor::cmdTransUnixWord(Cmd, int32_t)
{
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  if (pos == line.width()) pos = line.align(pos - 1);
  if (pos <= m_context.startPos) return false;
  unsigned start = line.position(m_context.startPos).mapping();
  unsigned rend = line.position(pos).mapping();
  rend = line.fwdUnixWordEnd(rend, true);
  unsigned rbegin = line.revUnixWord(rend);
  if (rbegin <= start) return false;
  unsigned mbegin = line.revUnixWordEnd(rbegin, true);
  if (mbegin <= start) return false;
  unsigned lbegin = line.revUnixWord(mbegin);
  if (lbegin <= start) return false;
  ZtArray<uint8_t> replace;
  replace.length(rend - lbegin);
  const auto &data = line.data();
  memcpy(&replace[0], &data[rbegin], rend - rbegin);
  memcpy(&replace[rend - rbegin], &data[mbegin], rbegin - mbegin);
  memcpy(&replace[rend - mbegin], &data[lbegin], mbegin - lbegin);
  auto span = ZuUTF<uint32_t, uint8_t>::span(substr(lbegin, rend));
  splice(lbegin, span, replace, span, true);
  return false;
}

namespace {
using TransformCharFn = Editor::TransformCharFn;
using TransformSpanFn = Editor::TransformSpanFn;

bool isupper__(char c) { return c >= 'A' && c <= 'Z'; }
char toupper__(char c)
{
  return c + (static_cast<int>('A') - static_cast<int>('a'));
}
bool islower__(char c) { return c >= 'a' && c <= 'z'; }
char tolower__(char c)
{
  return c + (static_cast<int>('a') - static_cast<int>('A'));
}
TransformCharFn lowerFn() {
  return [](uint8_t c, uint8_t &replace) {
    if (isupper__(c)) replace = tolower__(c);
  };
}
TransformCharFn upperFn() {
  return [](uint8_t c, uint8_t &replace) {
    if (islower__(c)) replace = toupper__(c);
  };
}
TransformCharFn toggleFn() {
  return [](uint8_t c, uint8_t &replace) {
    if (isupper__(c))
      replace = tolower__(c);
    else if (islower__(c))
      replace = toupper__(c);
  };
}
TransformSpanFn spanFn() {
  return [](TransformCharFn fn, ZuSpan<uint8_t> replace) {
    for (unsigned i = 0, n = replace.length(); i < n; ) {
      uint8_t c = replace[i];
      unsigned j = ZuUTF8::in(c);
      if (!j) return;
      fn(c, replace[i]);
      i += j;
    }
  };
}
TransformSpanFn capFn() {
  return [](TransformCharFn, ZuSpan<uint8_t> replace) {
    if (unsigned n = replace.length()) {
      unsigned i = 0;
      uint8_t c = replace[0];
      if (isupper__(c)) {
	if (n < 2 || isupper__(replace[1])) { // lc
	  replace[0] = tolower__(c);
	  ++i;
	  while (i < n) {
	    uint8_t c = replace[i];
	    unsigned j = ZuUTF8::in(c);
	    if (!j) return;
	    if (isupper__(c)) replace[i] = tolower__(c);
	    i += j;
	  }
	} else { // uc
	  ++i;
	  while (i < n) {
	    uint8_t c = replace[i];
	    unsigned j = ZuUTF8::in(c);
	    if (!j) return;
	    if (islower__(c)) replace[i] = toupper__(c);
	    i += j;
	  }
	}
      } else if (islower__(c)) { // ucfirst
	replace[0] = toupper__(c);
	++i;
	while (i < n) {
	  uint8_t c = replace[i];
	  unsigned j = ZuUTF8::in(c);
	  if (!j) return;
	  if (isupper__(c)) replace[i] = tolower__(c);
	  i += j;
	}
      }
    }
  };
}
} // anon namespace

void Editor::transformWord(TransformSpanFn fn, TransformCharFn charFn)
{
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  if (pos == line.width()) return;
  if (pos <= m_context.startPos) return;
  unsigned start = line.position(m_context.startPos).mapping();
  unsigned begin = line.position(pos).mapping();
  if (!line.isword_(begin)) return;
  unsigned end = line.fwdWordEnd(begin, true);
  if (begin <= start || begin >= end) return;
  ZtArray<uint8_t> replace;
  replace.length(end - begin);
  const auto &data = line.data();
  memcpy(&replace[0], &data[begin], end - begin);
  fn(charFn, replace);
  auto span = ZuUTF<uint32_t, uint8_t>::span(substr(begin, end));
  splice(begin, span, replace, span, true);
}

void Editor::transformVis(TransformSpanFn fn, TransformCharFn charFn)
{
  const auto &line = m_tty.line();
  if (m_context.markPos < 0 || m_context.highPos < 0) return;
  unsigned begin = line.position(m_context.markPos).mapping();
  unsigned end = line.position(m_context.highPos).mapping();
  if (begin >= end) return;
  ZtArray<uint8_t> replace;
  replace.length(end - begin);
  const auto &data = line.data();
  memcpy(&replace[0], &data[begin], end - begin);
  fn(charFn, replace);
  auto span = ZuUTF<uint32_t, uint8_t>::span(substr(begin, end));
  m_tty.mv(m_context.markPos);
  splice(begin, span, replace, span, true);
}

bool Editor::cmdCapGlyph(Cmd cmd, int32_t)
{
  auto charFn = toggleFn();
  auto fn = spanFn();
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  if (pos == line.width()) pos = line.align(pos - 1);
  if (pos <= m_context.startPos) return false;
  unsigned start = line.position(m_context.startPos).mapping();
  unsigned end = line.position(pos).mapping(), begin = end;
  int arg = m_context.evalArg(cmd.arg(), 1);
  for (int i = 0; i < arg; i++) end = line.fwdGlyph(end);
  if (begin <= start || begin >= end) return false; // redundant
  ZtArray<uint8_t> replace;
  replace.length(end - begin);
  const auto &data = line.data();
  memcpy(&replace[0], &data[begin], end - begin);
  fn(charFn, replace);
  auto span = ZuUTF<uint32_t, uint8_t>::span(substr(begin, end));
  splice(begin, span, replace, span, !commandMode());
  return false;
}
bool Editor::cmdLowerWord(Cmd, int32_t)
{
  transformWord(spanFn(), lowerFn());
  return false;
}
bool Editor::cmdUpperWord(Cmd, int32_t)
{
  transformWord(spanFn(), upperFn());
  return false;
}
bool Editor::cmdCapWord(Cmd, int32_t)
{
  transformWord(capFn(), nullptr);
  return false;
}

bool Editor::cmdLowerVis(Cmd, int32_t)
{
  transformVis(spanFn(), lowerFn());
  return false;
}
bool Editor::cmdUpperVis(Cmd, int32_t)
{
  transformVis(spanFn(), upperFn());
  return false;
}
bool Editor::cmdCapVis(Cmd, int32_t)
{
  transformVis(capFn(), nullptr);
  return false;
}

bool Editor::cmdXchMark(Cmd, int32_t)
{
  int pos = m_tty.pos() == m_context.markPos ?
    m_context.highPos : m_context.markPos;
  if (pos >= static_cast<int>(m_context.startPos)) {
    int finPos = m_tty.line().width();
    if (pos > finPos) pos = finPos;
    m_tty.mv(align(pos));
  }
  return false;
}

bool Editor::cmdFwdGlyphSrch(Cmd cmd, int32_t vkey)
{
  if (ZuUnlikely(vkey <= 0)) return false;
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) return cmdRevGlyphSrch(cmd.negArg(), vkey);
  unsigned pos = m_tty.pos();
  const auto &line = m_tty.line();
  if (pos < line.width()) {
    unsigned begin = line.position(pos).mapping(), end = begin;
    unsigned begPos = pos;
    unsigned final_ = line.length();
    do {
      end = line.fwdSearch(end, vkey);
      if (end > final_) end = final_;
    } while (end < final_ && --arg > 0);
    if (begin < end) {
      unsigned endPos = line.byte(end).mapping();
      if (cmd.op() & Op::Mv) pos = endPos;
      motion(cmd.op(), pos, begin, end, begPos, endPos);
    }
  }
  return false;
}
bool Editor::cmdRevGlyphSrch(Cmd cmd, int32_t vkey)
{
  if (ZuUnlikely(vkey <= 0)) return false;
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) return cmdFwdGlyphSrch(cmd.negArg(), vkey);
  unsigned pos = m_tty.pos();
  if (pos > m_context.startPos) {
    const auto &line = m_tty.line();
    unsigned end = line.position(pos).mapping(), begin = end;
    unsigned endPos = pos;
    unsigned start = line.position(m_context.startPos).mapping();
    do {
      begin = line.revSearch(begin, vkey);
      if (begin < start) begin = start;
    } while (begin > start && --arg > 0);
    if (begin < end) {
      unsigned begPos = line.byte(begin).mapping();
      if (cmd.op() & Op::Mv) pos = begPos;
      motion(cmd.op(), pos, begin, end, begPos, endPos);
    }
  }
  return false;
}

void Editor::initComplete()
{
  const auto &line = m_tty.line();
  unsigned start = line.position(m_context.startPos).mapping();
  unsigned cursor = line.position(m_tty.pos()).mapping();
  unsigned end = line.length();
  auto data = substr(start, end);
  m_app.compInit(data, cursor - start, m_compSpliceFn);
}
void Editor::finalComplete()
{
  m_app.compFinal();
}
void Editor::startComplete()
{
  m_app.compStart();
}
bool Editor::cmdComplete(Cmd, int32_t)
{
  complete(true);
  return false;
}
bool Editor::cmdRevComplete(Cmd, int32_t)
{
  complete(false);
  return false;
}
void Editor::complete(bool next)
{
  unsigned prevOp = m_context.prevCmd.op();
  if (prevOp != Op::Complete && prevOp != Op::RevComplete) {
    if (prevOp != Op::ListComplete) initComplete();
    startComplete();
  }
  if (!m_app.compSubst(m_compSpliceFn, next))
    m_tty.bell();
}
void Editor::spliceCompletion(
  unsigned off,
  ZuUTFSpan span,
  ZuBSpan replace,
  ZuUTFSpan rspan)
{
  const auto &line = m_tty.line();
  off += line.position(m_context.startPos).mapping();
  if (rspan.inLen() <= span.inLen() ||
      line.length() + (rspan.inLen() - span.inLen()) < m_config.maxLineLen) {
    m_tty.mv(line.byte(off).mapping());
    splice(off, span, replace, rspan, true);
  }
}
bool Editor::cmdListComplete(Cmd, int32_t)
{
  unsigned prevOp = m_context.prevCmd.op();
  if (prevOp != Op::ListComplete) initComplete();
  startComplete();
  unsigned ttyWidth = m_tty.width();
  unsigned maxHeight = m_config.maxCompPages * m_tty.height();
  unsigned colWidth = 0, colHeight, nCols;
  using Match = ZuTuple<ZtArray<uint8_t>, ZuUTFSpan>;
  ZtArray<Match> matches;
  CompIterFn fn{[ttyWidth, &colWidth, &colHeight, &nCols, &matches](
      ZuBSpan data, ZuUTFSpan span) {
    Match match{data, span};
    matches.push(match);
    auto width = span.width() + 1;
    if (width > colWidth) colWidth = width;
    // abort if more than maxCompPages of output
    nCols = colWidth > (ttyWidth>>1) ? 1 : ttyWidth / colWidth;
    colHeight = (matches.length() + nCols - 1) / nCols;
  }};
  while (m_app.compNext(fn) && colHeight <= maxHeight) { }
  if (!matches) return false;
  colWidth = ttyWidth / nCols;
  if (m_tty.write() != Zi::OK) return false;
  m_tty.crnl_();
  for (unsigned row = 0; row < colHeight; row++) {
    unsigned i = row;
    unsigned col;
    for (col = 0; col < nCols; col++) {
      const auto &[ data, span ] = matches[i];
      m_tty.out_(data);
      auto width = span.width();
      if (width < colWidth) m_tty.clrOver_(colWidth - width);
      if ((i += colHeight) >= matches.length()) break;
    }
    auto width = col * colWidth;
    if (width < ttyWidth)
      m_tty.clrBreak_(ttyWidth - width);
    if (m_tty.write() != Zi::OK) return false;
  }
  m_tty.redraw();
  return false;
}

void Editor::histLoad(int offset, ZuBSpan data, bool save)
{
  m_context.horizPos = -1;
  m_context.histLoadOff = offset;
  unsigned origPos = m_tty.pos();
  unsigned pos = m_context.startPos;
  m_tty.mv(pos);
  const auto &line = m_tty.line();
  unsigned begin = line.position(pos).mapping();
  unsigned end = line.length();
  auto orig = substr(begin, end);
  if (save && orig) m_app.histSave(m_context.histSaveOff, orig);
  m_context.clrUndo();
  m_tty.splice(
      begin, ZuUTF<uint32_t, uint8_t>::span(orig),
      data, ZuUTF<uint32_t, uint8_t>::span(data),
      !data || !commandMode());
  unsigned finPos = line.width();
  if (origPos >= finPos) origPos = finPos;
  m_tty.mv(origPos);
}
bool Editor::cmdNext(Cmd cmd, int32_t vkey)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) return cmdPrev(cmd.negArg(), vkey);
  if (!arg) return false;
  if (arg > m_context.histSaveOff + 1) arg = m_context.histSaveOff + 1;
  if (m_context.histLoadOff >= 0 &&
      m_context.histLoadOff <= m_context.histSaveOff - (arg - 1)) {
    int offset = m_context.histLoadOff + arg;
    if (!m_app.histLoad(offset, [this, offset](ZuBSpan data) {
      histLoad(offset, data, false);
    }))
      histLoad(m_context.histLoadOff, {}, false);
  }
  return false;
}
bool Editor::cmdPrev(Cmd cmd, int32_t vkey)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (arg < 0) return cmdNext(cmd.negArg(), vkey);
  if (!arg) return false;
  if (m_context.histLoadOff < 0)
    m_context.histLoadOff = m_context.histSaveOff;
  if (arg > m_context.histLoadOff) arg = m_context.histLoadOff;
  {
    bool save = m_context.histLoadOff == m_context.histSaveOff;
    int offset = m_context.histLoadOff - arg;
    m_app.histLoad(offset, [this, offset, save](ZuBSpan data) {
      histLoad(offset, data, save);
    });
  }
  return false;
}

bool Editor::cmdClrIncSrch(Cmd, int32_t)
{
  m_context.srchTerm = {};
  if (m_context.histLoadOff >= 0 &&
      m_context.histLoadOff < m_context.histSaveOff) {
    m_context.histLoadOff = -1;
    int offset = m_context.histSaveOff;
    if (!m_app.histLoad(offset, [this, offset](ZuBSpan data) {
      histLoad(offset, data, false);
    }))
      histLoad(-1, {}, false);
  }
  return false;
}
// adds a key to incremental search term
bool Editor::addIncSrch(int32_t vkey)
{
  if (vkey <= 0) return false;
  ZuArray<uint8_t, 4> utf;
  utf.length(ZuUTF8::out(utf.data(), 4, vkey));
  if (m_context.srchTerm.length() + utf.length() >= m_config.maxLineLen)
    return false;
  m_context.srchTerm << utf;
  return true;
}

// simple/fast raw substring matcher
bool Editor::match(ZuBSpan data)
{
  const auto &term = m_context.srchTerm;
  if (ZuUnlikely(data.length() < term.length())) return false;
  for (unsigned i = 0, n = data.length() - term.length(); i <= n; i++) {
    unsigned j = 0, m = term.length();
    for (; j < m; j++) if (data[i + j] != term[j]) goto cnt;
    return true;
    cnt:static_cast<void>(0); // bah
  }
  return false;
}
// searches forward skipping N-1 matches - returns true if found
bool Editor::searchFwd(int arg)
{
  if (ZuUnlikely(arg <= 0)) return false;
  int offset = m_context.histLoadOff;
  if (ZuUnlikely(offset < 0)) return false;
  HistFn fn{[this, offset, &arg](ZuBSpan data) {
    if (match(data) && !--arg) histLoad(offset, data, false);
  }};
  while (offset < m_context.histSaveOff) {
    ++offset;
    if (!m_app.histLoad(offset, fn)) break;
    if (m_context.histLoadOff == offset) return true;
  }
  return false;
}
// searches backward skipping N-1 matches - returns true if found
bool Editor::searchRev(int arg)
{
  if (ZuUnlikely(arg <= 0)) return false;
  int offset = m_context.histLoadOff;
  if (ZuUnlikely(offset < 0)) offset = m_context.histSaveOff;
  if (ZuUnlikely(!offset)) return false;
  bool save = offset == m_context.histSaveOff;
  HistFn fn{[this, offset, save, &arg](ZuBSpan data) {
    if (match(data) && !--arg) histLoad(offset, data, save);
  }};
  while (offset > 0) {
    --offset;
    if (!m_app.histLoad(offset, fn)) break;
    if (m_context.histLoadOff == offset) return true;
  }
  return false;
}

bool Editor::cmdFwdIncSrch(Cmd cmd, int32_t vkey)
{
  if (vkey == -VKey::Null || addIncSrch(vkey)) {
    int arg = m_context.evalArg(cmd.arg(), 1);
    if (arg < 0)
      searchRev(-arg);
    else
      searchFwd(arg);
  }
  return false;
}
bool Editor::cmdRevIncSrch(Cmd cmd, int32_t vkey)
{
  if (vkey == -VKey::Null || addIncSrch(vkey)) {
    int arg = m_context.evalArg(cmd.arg(), 1);
    if (arg < 0)
      searchFwd(-arg);
    else
      searchRev(arg);
  }
  return false;
}

bool Editor::cmdPromptSrch(Cmd cmd, int32_t vkey)
{
  if (ZuUnlikely(vkey <= 0)) return false;
  m_context.stack.push(m_context.mode);
  int arg = cmd.arg();
  if (ZuUnlikely(Cmd::nullArg(arg) || arg < 0)) return false;
  m_context.mode = arg;
  unsigned pos = m_context.startPos;
  const auto &line = m_tty.line();
  unsigned begin = line.position(pos).mapping();
  unsigned end = line.length();
  auto orig = substr(begin, end);
  if (m_context.histLoadOff < 0)
    m_context.histLoadOff = m_context.histSaveOff;
  if (m_context.histLoadOff == m_context.histSaveOff && orig)
    m_app.histSave(m_context.histSaveOff, orig);
  ZuArray<uint8_t, 4> prompt;
  prompt.length(ZuUTF8::out(prompt.data(), 4, vkey));
  m_context.srchPrmptSpan = ZuUTF<uint32_t, uint8_t>::span(prompt);
  m_context.startPos = m_context.srchPrmptSpan.width();
  m_tty.mv(0);
  m_tty.splice(
      0, ZuUTF<uint32_t, uint8_t>::span(substr(0, end)),
      prompt, m_context.srchPrmptSpan, true);
  return false;
}

// perform non-incremental search operation (abort, forward or reverse)
struct SearchOp { enum { Abort = 0, Fwd, Rev }; };
void Editor::srchEndPrompt(int op)
{
  unsigned pos = m_context.startPos;
  const auto &line = m_tty.line();
  unsigned begin = line.position(pos).mapping();
  unsigned end = line.length();
  if (op != SearchOp::Abort && end > begin)
    m_context.srchTerm = substr(begin, end);
  {
    auto prmptSpan = ZuUTF<uint32_t, uint8_t>::span(m_context.prompt);
    m_tty.mv(0);
    m_tty.splice(0,
	ZuUTF<uint32_t, uint8_t>::span(substr(0, end)),
	m_context.prompt,
	prmptSpan, true);
    m_context.startPos = prmptSpan.width();
  }
  pos = m_context.startPos;
  begin = line.position(pos).mapping();
  auto orig = substr(begin, end);
  bool found = false;
  if (op == SearchOp::Fwd)
    found = searchFwd(1);
  else if (op == SearchOp::Rev)
    found = searchRev(1);
  if (!found) {
    if (m_context.histLoadOff < 0)
      m_context.histLoadOff = m_context.histSaveOff;
    if (m_context.histLoadOff >= 0)
      m_app.histLoad(m_context.histLoadOff,
	  [this, begin, orig](ZuBSpan data) {
	m_tty.splice(
	    begin, ZuUTF<uint32_t, uint8_t>::span(orig),
	    data, ZuUTF<uint32_t, uint8_t>::span(data), true);
      });
  }
  m_context.mode = m_context.stack.pop();
  m_context.srchPrmptSpan = {};
}
bool Editor::cmdEnterSrchFwd(Cmd, int32_t)
{
  m_context.srchFwd = true;
  srchEndPrompt(SearchOp::Fwd);
  return false;
}
bool Editor::cmdEnterSrchRev(Cmd, int32_t)
{
  m_context.srchFwd = false;
  srchEndPrompt(SearchOp::Rev);
  return false;
}
bool Editor::cmdAbortSrch(Cmd, int32_t)
{
  m_context.srchFwd = true;
  srchEndPrompt(SearchOp::Abort);
  return false;
}

bool Editor::cmdFwdSearch(Cmd cmd, int32_t)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (!m_context.srchFwd) arg = -arg;
  if (arg < 0)
    searchRev(-arg);
  else
    searchFwd(arg);
  return false;
}
bool Editor::cmdRevSearch(Cmd cmd, int32_t)
{
  int arg = m_context.evalArg(cmd.arg(), 1);
  if (!m_context.srchFwd) arg = -arg;
  if (arg < 0)
    searchFwd(-arg);
  else
    searchRev(arg);
  return false;
}

} // namespace Zrl
