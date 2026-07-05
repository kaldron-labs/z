//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface

#include <stdlib.h>

#ifndef _WIN32
#include <unistd.h>

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <linux/unistd.h>
#endif

#include <zlib/ZiLib.hh>

#include <zlib/ZmAlloc.hh>

#include <zlib/ZtLocalArray.hh>

#include <zlib/ZrlTerminal.hh>

namespace Zrl {

using ErrorStr = ZuCArray<120>;

#define ZrlError(op, result, error) \
  Zrl::ErrorStr() << op << ' ' << Zi::ioResult(result) << ' ' << error

namespace VKey {
ZrlExtern void print_(int32_t vkey, ZuVStream &s)
{
  ZuCArray<60> out;
  if (vkey < 0) {
    vkey = -vkey;
    if (vkey == VKey::Null) return;
    out << name(vkey & Mask) << '[';
    bool pipe = false;
    auto sep = [&pipe, &out]() { if (!pipe) pipe = true; else out << '|'; };
    if (vkey & Shift) { sep(); out << "Shift"; }
    if (vkey & Ctrl) { sep(); out << "Ctrl"; }
    if (vkey & Alt) { sep(); out << "Alt"; }
    out << ']';
  } else {
    if (vkey < 0x20) {
      out << '^' << static_cast<char>('@' + vkey);
    } else if (vkey >= 0x7f && vkey < 0x100) {
      out << "'\\x" << ZuBoxed(vkey).hex<false, ZuFmt::Right<2, '0'>>() << '\'';
    } else {
      ZuArray<uint8_t, 4> utf;
      utf.length(ZuUTF8::out(utf.data(), 4, vkey));
      if (utf.length() == 1)
	out << '\'' << static_cast<char>(utf[0]) << '\'';
      else
	out << '"' << ZuCSpan(utf) << '"';
    }
  }
  s << out;
}
}

unsigned &VKeyMatch_printIndentLevel() {
  return ZmTLS<unsigned, VKeyMatch_printIndentLevel>();
}

void VKeyMatch::Action::print_(ZuVStream &s) const
{
  if (vkey != -VKey::Null) s << VKey::print(vkey);
  s << "\r\n";
  if (auto ptr = next.ptr<VKeyMatch>()) {
    auto &indentLevel = VKeyMatch_printIndentLevel();
    ++indentLevel;
    s << *ptr;
    --indentLevel;
  }
}

void VKeyMatch_print_byte(ZuVStream &s, uint8_t byte)
{
  if (byte < 0x20)
    s << '^' << static_cast<char>('@' + byte);
  else if (byte >= 0x7f)
    s << "\\x" << ZuBoxed(byte).hex<false, ZuFmt::Right<2, '0'>>();
  else
    s << static_cast<char>(byte);
}

void VKeyMatch::print_(ZuVStream &s) const
{
  unsigned level = VKeyMatch_printIndentLevel();
  for (unsigned i = 0, n = m_bytes.length(); i < n; i++) {
    for (unsigned j = 0; j < level; j++) s << ' ';
    VKeyMatch_print_byte(s, m_bytes[i]);
    if (m_actions[i].vkey != -VKey::Null) s << " -> ";
    s << m_actions[i];
  }
}

bool VKeyMatch::add_(VKeyMatch *this_, const uint8_t *s, int32_t vkey)
{
  uint8_t c = *s;
  if (!c) return false;
loop:
  unsigned i, n = this_->m_bytes.length();
  for (i = 0; i < n; i++) if (this_->m_bytes[i] == c) goto matched;
  this_->m_bytes.push(c);
  new (this_->m_actions.push()) Action{};
matched:
  if (!(c = *++s)) {
    if (this_->m_actions[i].vkey != -VKey::Null) return false;
    this_->m_actions[i].vkey = -vkey;
    return true;
  }
  VKeyMatch *ptr;
  if (!(ptr = this_->m_actions[i].next.ptr<VKeyMatch>()))
    this_->m_actions[i].next = ptr = new VKeyMatch{};
  this_ = ptr;
  goto loop;
}

bool VKeyMatch::add(uint8_t c, int32_t vkey)
{
  if (!c) return false;
  char s[2];
  s[0] = c;
  s[1] = 0;
  return add(s, vkey);
}

const VKeyMatch::Action *VKeyMatch::match(uint8_t c) const
{
  for (unsigned i = 0, n = m_bytes.length(); i < n; i++)
    if (m_bytes[i] == c) return &m_actions[i];
  return nullptr;
}

void Terminal::open(ZmScheduler *sched, unsigned thread,
    OpenFn openFn, ErrorFn errorFn) // async
{
  m_sched = sched;
  m_thread = thread;
  invoke([this, openFn = ZuMv(openFn), errorFn = ZuMv(errorFn)]() mutable {
    m_errorFn = ZuMv(errorFn);
    openFn(this->open_());
  });
}

using TerminalPtr = Terminal *;

bool Terminal::isOpen() const // synchronous
{
  return ZmBlock<bool>{}([this](auto wake) mutable {
    invoke([this, wake = ZuMv(wake)]() mutable {
      wake(isOpen_());
    });
  });
}

bool Terminal::isOpen_() const
{
#ifndef _WIN32
  return m_fd >= 0;
#else
  return m_conout != INVALID_HANDLE_VALUE;
#endif
}

void Terminal::close(CloseFn fn) // async
{
  invoke([this, fn = ZuMv(fn)]() { this->close_(); fn(); });
}

void Terminal::start(StartFn startFn, KeyFn keyFn) // async
{
  Guard guard(m_lock);
  m_sched->push([
    this, startFn = ZuMv(startFn), keyFn = ZuMv(keyFn)
  ]() mutable {
    if (!start_()) return;
    StartFn{ZuMv(startFn)}();
    m_keyFn = ZuMv(keyFn);
    m_sched->wakeFn(m_thread, ZmScheduler::WakeFn{this, [](Terminal *this_) {
      this_->wake();
    }});
    read();
  }, m_thread);
}

bool Terminal::running() const // synchronous
{
  return ZmBlock<bool>{}([this](auto wake) mutable {
    invoke([this, wake = ZuMv(wake)]() mutable {
      wake(m_running);
    });
  });
}

void Terminal::stop() // async
{
  if (!isOpen_()) return;
  Guard guard(m_lock);
  m_sched->wakeFn(m_thread, {});
  m_sched->push([this]() { stop_(); }, m_thread);
  wake_();
}

static TerminalPtr &localTerminal() {
  return ZmTLS<TerminalPtr, nullptr, ZmCleanup::Library>();
}

bool Terminal::open_()
{
#ifndef _WIN32

  // open tty device
  m_fd = ::open("/dev/tty", O_RDWR, 0);
  if (m_fd < 0) {
    ZeError e{errno};
    m_errorFn(ZrlError("open(\"/dev/tty\")", Zi::IOError, e));
    return false;
  }

  // save terminal control flags
  memset(&m_otermios, 0, sizeof(termios));
  tcgetattr(m_fd, &m_otermios);

  // set up I/O multiplexer (epoll)
  if ((m_epollFD = epoll_create(2)) < 0) {
    m_errorFn(ZrlError("epoll_create", Zi::IOError, ZeLastError));
    return false;
  }
  if (pipe(&m_wakeFD) < 0) {
    ZeError e{errno};
    close_fds();
    m_errorFn(ZrlError("pipe", Zi::IOError, e));
    return false;
  }
  if (fcntl(m_wakeFD, F_SETFL, O_NONBLOCK) < 0) {
    ZeError e{errno};
    close_fds();
    m_errorFn(ZrlError("fcntl(F_SETFL, O_NONBLOCK)", Zi::IOError, e));
    return false;
  }
  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN;
    ev.data.u64 = 3;
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, m_wakeFD, &ev) < 0) {
      ZeError e{errno};
      close_fds();
      m_errorFn(ZrlError("epoll_ctl(EPOLL_CTL_ADD)", Zi::IOError, e));
      return false;
    }
  }

  // initialize terminfo
  if (setupterm(nullptr, m_fd, nullptr) < 0) {
    m_errorFn("terminfo initialization failed");
    return false;
  }

  // obtain input capabilities
  m_smkx = tigetstr("smkx");
  m_rmkx = tigetstr("rmkx");
 
  // obtain output capabilities
  m_am = tigetflag("am");
  m_xenl = tigetflag("xenl");
  m_mir = tigetflag("mir");
  m_hz = tigetflag("hz");
  m_ul = tigetflag("ul");

  // xenl can manifest in two different ways. The vt100 way is that, when
  // you'd expect the cursor to wrap, it stays hung at the right margin
  // (on top of the character just emitted) and doesn't wrap until the
  // *next* glyph is emitted. The c100 way is to ignore LF received
  // just after an am wrap.

  // when scrolling, this is handled by emitting CR/LF after the char and
  // assuming the wrap is done, you're on the first position of the next
  // line, and the terminal is now out of its weird state.

  if (!(m_cr = tigetstr("cr"))) m_cr = "\r";
  if (!(m_ind = tigetstr("ind"))) m_ind = "\n";
  m_nel = tigetstr("nel");

  m_clear = tigetstr("clear");

  if (!tigetflag("xhpa")) m_hpa = tigetstr("hpa");

  m_cub = tigetstr("cub");
  if (!(m_cub1 = tigetstr("cub1"))) m_cub1 = "\b";
  m_cuf = tigetstr("cuf");
  m_cuf1 = tigetstr("cuf1");
  m_cuu = tigetstr("cuu");
  m_cuu1 = tigetstr("cuu1");
  m_cud = tigetstr("cud");
  m_cud1 = tigetstr("cud1");

  m_el = tigetstr("el");
  m_ech = tigetstr("ech");

  m_smir = tigetstr("smir");
  m_rmir = tigetstr("rmir");
  m_ich = tigetstr("ich");
  m_ich1 = tigetstr("ich1");

  m_smdc = tigetstr("smdc");
  m_rmdc = tigetstr("rmdc");
  m_dch = tigetstr("dch");
  m_dch1 = tigetstr("dch1");

  m_bold = tigetstr("bold");
  m_sgr = tigetstr("sgr");
  m_sgr0 = tigetstr("sgr0");
  m_smso = tigetstr("smso");
  m_rmso = tigetstr("rmso");
  m_civis = tigetstr("civis");
  m_cnorm = tigetstr("cnorm");

  m_bel = tigetstr("bel");

  if (m_ul) {
    m_underline << ' ';
    localTerminal() = this;
    ::tputs(m_cub1, 1, [](int c) -> int {
      localTerminal()->m_underline << static_cast<uint8_t>(c);
      return 0;
    });
    m_underline << '_';
  }

#else /* !_Win32 */

  m_wake = CreateEvent(nullptr, true, false, L"Local\\ZrlTerminal");
  if (m_wake == NULL || m_wake == INVALID_HANDLE_VALUE) {
    m_wake = INVALID_HANDLE_VALUE;
    m_errorFn(ZrlError("CreateEvent", Zi::IOError, ZeLastError));
    return false;
  }

  AllocConsole(); // idempotent - ignore errors

  m_conin = CreateFile(L"CONIN$",
      GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
      OPEN_EXISTING, 0, NULL);
  if (m_conin == INVALID_HANDLE_VALUE) {
    ZeError e{ZeLastError};
    close_fds();
    m_errorFn(ZrlError("CreateFile(\"CONIN$\")", Zi::IOError, e));
    return false;
  }
  {
    INPUT_RECORD rec;
    DWORD count;
    if (!PeekConsoleInput(m_conin, &rec, 1, &count)) {
      ZeError e{ZeLastError};
      close_fds();
      m_errorFn(ZrlError("PeekConsoleInput()", Zi::IOError, e));
      return false;
    }
  }

  m_conout = CreateFile(L"CONOUT$",
      GENERIC_WRITE | GENERIC_READ, FILE_SHARE_WRITE, nullptr,
      OPEN_EXISTING, 0, NULL);
  if (m_conout == INVALID_HANDLE_VALUE) {
    ZeError e{ZeLastError};
    close_fds();
    m_errorFn(ZrlError("CreateFile(\"CONOUT$\")", Zi::IOError, e));
    return false;
  }

  m_coninCP = GetConsoleCP();
  GetConsoleMode(m_conin, &m_coninMode);

  m_conoutCP = GetConsoleOutputCP();
  GetConsoleMode(m_conout, &m_conoutMode);

  SetConsoleMode(m_conin, m_coninMode &
      ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT));
  if (m_coninCP != 65001) SetConsoleCP(65001);

#endif /* !_WIN32 */

  // initialize keystroke matcher
  m_vkeyMatch = new VKeyMatch{};

#ifndef _WIN32

  //		| Normal | Shift | Ctrl/Alt/combinations (*)
  // -----------+--------+-------+--------------------------
  //  Up	| kcuu1  | kUP   | kUP[3-8]
  //  Down	| kcud1  | kDN   | kDN[3-8]
  //  Left	| kcub1  | kLFT  | kLFT[3-8]
  //  Right	| kcuf1  | kRIT  | kRIT[3-8]
  //  Home	| khome  | kHOM  | kHOM[3-8]
  //  End	| kend   | kEND  | kEND[3-8]
  //  Insert	| kich1  | kIC   | kIC[3-8]
  //  Delete	| kdch1  | KDC   | kDC[3-8]
  //
  //  (*) modifiers
  //  -------------
  //  3 - Alt
  //  4 - Shift + Alt
  //  5 - Ctrl
  //  6 - Shift + Ctrl
  //  7 - Ctrl + Alt
  //  8 - Shift + Ctrl + Alt

  // Enter
  addCtrlKey('\r', VKey::Enter);
  addCtrlKey(m_otermios.c_cc[VEOL], VKey::Enter);
  addCtrlKey(m_otermios.c_cc[VEOL2], VKey::Enter);
  addVKey("kent", nullptr, VKey::Enter);

  // Tab
  addCtrlKey('\t', VKey::Tab);
  addVKey("kcbt", nullptr, VKey::Tab | VKey::Shift);

  // EOF
  addCtrlKey(m_otermios.c_cc[VEOF], VKey::EndOfFile);

  // erase keys
  addCtrlKey(m_otermios.c_cc[VERASE], VKey::Erase);
  addCtrlKey(m_otermios.c_cc[VWERASE], VKey::WErase);
  addCtrlKey(m_otermios.c_cc[VKILL], VKey::Kill);

  // signals
  addCtrlKey(m_otermios.c_cc[VINTR], VKey::SigInt);
  addCtrlKey(m_otermios.c_cc[VQUIT], VKey::SigQuit);
  addCtrlKey(m_otermios.c_cc[VSUSP], VKey::SigSusp);

  // literal next
  addCtrlKey(m_otermios.c_cc[VLNEXT], VKey::LNext);

  // redraw/reprint
#ifdef VREPRINT
  addCtrlKey(m_otermios.c_cc[VREPRINT], VKey::Redraw);
#endif

  // motion keys
  addVKey("kcuu1", nullptr, VKey::Up);
  addVKey("kUP", nullptr, VKey::Up | VKey::Shift);
  addVKey("kUP3", nullptr, VKey::Up | VKey::Alt);
  addVKey("kUP4", nullptr, VKey::Up | VKey::Shift | VKey::Alt);
  addVKey("kUP5", nullptr, VKey::Up | VKey::Ctrl);
  addVKey("kUP6", nullptr, VKey::Up | VKey::Shift | VKey::Ctrl);
  addVKey("kUP7", nullptr, VKey::Up | VKey::Ctrl | VKey::Alt);
  addVKey("kUP8", nullptr, VKey::Up | VKey::Shift | VKey::Ctrl | VKey::Alt);

  addVKey("kcud1", nullptr, VKey::Down);
  addVKey("kDN", nullptr, VKey::Down | VKey::Shift);
  addVKey("kDN3", nullptr, VKey::Down | VKey::Alt);
  addVKey("kDN4", nullptr, VKey::Down | VKey::Shift | VKey::Alt);
  addVKey("kDN5", nullptr, VKey::Down | VKey::Ctrl);
  addVKey("kDN6", nullptr, VKey::Down | VKey::Shift | VKey::Ctrl);
  addVKey("kDN7", nullptr, VKey::Down | VKey::Ctrl | VKey::Alt);
  addVKey("kDN8", nullptr, VKey::Down | VKey::Shift | VKey::Ctrl | VKey::Alt);

  addVKey("kcub1", nullptr, VKey::Left);
  addVKey("kLFT", nullptr, VKey::Left | VKey::Shift);
  addVKey("kLFT3", nullptr, VKey::Left | VKey::Alt);
  addVKey("kLFT4", nullptr, VKey::Left | VKey::Shift | VKey::Alt);
  addVKey("kLFT5", nullptr, VKey::Left | VKey::Ctrl);
  addVKey("kLFT6", nullptr, VKey::Left | VKey::Shift | VKey::Ctrl);
  addVKey("kLFT7", nullptr, VKey::Left | VKey::Ctrl | VKey::Alt);
  addVKey("kLFT8", nullptr, VKey::Left | VKey::Shift | VKey::Ctrl | VKey::Alt);

  addVKey("kcuf1", nullptr, VKey::Right);
  addVKey("kRIT", nullptr, VKey::Right | VKey::Shift);
  addVKey("kRIT3", nullptr, VKey::Right | VKey::Alt);
  addVKey("kRIT4", nullptr, VKey::Right | VKey::Shift | VKey::Alt);
  addVKey("kRIT5", nullptr, VKey::Right | VKey::Ctrl);
  addVKey("kRIT6", nullptr, VKey::Right | VKey::Shift | VKey::Ctrl);
  addVKey("kRIT7", nullptr, VKey::Right | VKey::Ctrl | VKey::Alt);
  addVKey("kRIT8", nullptr, VKey::Right | VKey::Shift | VKey::Ctrl | VKey::Alt);

  addVKey("khome", nullptr, VKey::Home);
  addVKey("kHOM", nullptr, VKey::Home | VKey::Shift);
  addVKey("kHOM3", nullptr, VKey::Home | VKey::Alt);
  addVKey("kHOM4", nullptr, VKey::Home | VKey::Shift | VKey::Alt);
  addVKey("kHOM5", nullptr, VKey::Home | VKey::Ctrl);
  addVKey("kHOM6", nullptr, VKey::Home | VKey::Shift | VKey::Ctrl);
  addVKey("kHOM7", nullptr, VKey::Home | VKey::Ctrl | VKey::Alt);
  addVKey("kHOM8", nullptr, VKey::Home | VKey::Shift | VKey::Ctrl | VKey::Alt);

  addVKey("kend", nullptr, VKey::End);
  addVKey("kEND", nullptr, VKey::End | VKey::Shift);
  addVKey("kEND3", nullptr, VKey::End | VKey::Alt);
  addVKey("kEND4", nullptr, VKey::End | VKey::Shift | VKey::Alt);
  addVKey("kEND5", nullptr, VKey::End | VKey::Ctrl);
  addVKey("kEND6", nullptr, VKey::End | VKey::Shift | VKey::Ctrl);
  addVKey("kEND7", nullptr, VKey::End | VKey::Ctrl | VKey::Alt);
  addVKey("kEND8", nullptr, VKey::End | VKey::Shift | VKey::Ctrl | VKey::Alt);

  addVKey("kpp", nullptr, VKey::PgUp);
  addVKey("kPRV", nullptr, VKey::PgUp | VKey::Shift);
  addVKey("kPRV3", nullptr, VKey::PgUp | VKey::Alt);
  addVKey("kPRV4", nullptr, VKey::PgUp | VKey::Shift | VKey::Alt);
  addVKey("kPRV5", nullptr, VKey::PgUp | VKey::Ctrl);
  addVKey("kPRV6", nullptr, VKey::PgUp | VKey::Shift | VKey::Ctrl);
  addVKey("kPRV7", nullptr, VKey::PgUp | VKey::Ctrl | VKey::Alt);
  addVKey("kPRV8", nullptr, VKey::PgUp | VKey::Shift | VKey::Ctrl | VKey::Alt);

  addVKey("knp", nullptr, VKey::PgDn);
  addVKey("kNXT", nullptr, VKey::PgDn | VKey::Shift);
  addVKey("kNXT3", nullptr, VKey::PgDn | VKey::Alt);
  addVKey("kNXT4", nullptr, VKey::PgDn | VKey::Shift | VKey::Alt);
  addVKey("kNXT5", nullptr, VKey::PgDn | VKey::Ctrl);
  addVKey("kNXT6", nullptr, VKey::PgDn | VKey::Shift | VKey::Ctrl);
  addVKey("kNXT7", nullptr, VKey::PgDn | VKey::Ctrl | VKey::Alt);
  addVKey("kNXT8", nullptr, VKey::PgDn | VKey::Shift | VKey::Ctrl | VKey::Alt);

  addVKey("kich1", nullptr, VKey::Insert);
  addVKey("kIC", nullptr, VKey::Insert | VKey::Shift);
  addVKey("kIC3", nullptr, VKey::Insert | VKey::Alt);
  addVKey("kIC4", nullptr, VKey::Insert | VKey::Shift | VKey::Alt);
  addVKey("kIC5", nullptr, VKey::Insert | VKey::Ctrl);
  addVKey("kIC6", nullptr, VKey::Insert | VKey::Shift | VKey::Ctrl);
  addVKey("kIC7", nullptr, VKey::Insert | VKey::Ctrl | VKey::Alt);
  addVKey("kIC8", nullptr, VKey::Insert | VKey::Shift | VKey::Ctrl | VKey::Alt);

  addVKey("kdch1", nullptr, VKey::Delete);
  addVKey("kDC", nullptr, VKey::Delete | VKey::Shift);
  addVKey("kDC3", nullptr, VKey::Delete | VKey::Alt);
  addVKey("kDC4", nullptr, VKey::Delete | VKey::Shift | VKey::Alt);
  addVKey("kDC5", nullptr, VKey::Delete | VKey::Ctrl);
  addVKey("kDC6", nullptr, VKey::Delete | VKey::Shift | VKey::Ctrl);
  addVKey("kDC7", nullptr, VKey::Delete | VKey::Ctrl | VKey::Alt);
  addVKey("kDC8", nullptr, VKey::Delete | VKey::Shift | VKey::Ctrl | VKey::Alt);

  // handle SIGWINCH
  {
    struct sigaction nwinch;
    static Terminal *this_ = nullptr;

    this_ = this;
    memset(&nwinch, 0, sizeof(struct sigaction));
    nwinch.sa_handler = [](int) { this_->sigwinch(); };
    sigemptyset(&nwinch.sa_mask);
    sigaction(SIGWINCH, &nwinch, &m_winch);
  }

#else /* !_WIN32 */

  // Enter
  addCtrlKey('\r', VKey::Enter);	// ^M

  // Tab
  addCtrlKey('\t', VKey::Tab);		// ^I

  // EOF
  addCtrlKey('\x04', VKey::EndOfFile);	// ^D

  // erase keys
  addCtrlKey('\x08', VKey::Erase);	// ^H
  addCtrlKey('\x17', VKey::WErase);	// ^W
  addCtrlKey('\x15', VKey::Kill);	// ^U

  // signals
  addCtrlKey('\x03', VKey::SigInt);	// ^C
  addCtrlKey('\x1c', VKey::SigQuit);	// C-Backslash
  addCtrlKey('\x1a', VKey::SigSusp);	// ^Z

  // literal next
  addCtrlKey('\x16', VKey::LNext);	// ^V

  // redraw/reprint
  addCtrlKey('\x12', VKey::Redraw);	// ^R

#endif

  // get terminal width/height
  resized();

  return true;
}

void Terminal::close_()
{
  stop_(); // idempotent

#ifndef _WIN32

  // reset SIGWINCH handler
  sigaction(SIGWINCH, &m_winch, nullptr);

  // finalize terminfo
  m_smkx = nullptr;
  m_rmkx = nullptr;

  m_am = false;
  m_xenl = false;
  m_mir = false;
  m_hz = false;
  m_ul = false;

  m_cr = nullptr;
  m_ind = nullptr;
  m_nel = nullptr;

  m_clear = nullptr;

  m_hpa = nullptr;

  m_cub = nullptr;
  m_cub1 = nullptr;
  m_cuf = nullptr;
  m_cuf1 = nullptr;
  m_cuu = nullptr;
  m_cuu1 = nullptr;
  m_cud = nullptr;
  m_cud1 = nullptr;

  m_el = nullptr;
  m_ech = nullptr;

  m_smir = nullptr;
  m_rmir = nullptr;
  m_ich = nullptr;
  m_ich1 = nullptr;

  m_smdc = nullptr;
  m_rmdc = nullptr;
  m_dch = nullptr;
  m_dch1 = nullptr;

  m_bold = nullptr;
  m_sgr = nullptr;
  m_sgr0 = nullptr;
  m_smso = nullptr;
  m_rmso = nullptr;
  m_civis = nullptr;
  m_cnorm = nullptr;

  m_bel = nullptr;

  m_underline = {};

  del_curterm(cur_term);

#else /* !_WIN32 */

  // restore console
  SetConsoleMode(m_conin, m_coninMode);
  if (m_coninCP != 65001) SetConsoleCP(m_coninCP);

#endif /* !_WIN32 */

  // close file descriptors / handles
  close_fds();

  // finalize keystroke matcher
  m_vkeyMatch = nullptr;
}

void Terminal::close_fds()
{
#ifndef _WIN32

  // close I/O multiplexer
  if (m_epollFD >= 0) { ::close(m_epollFD); m_epollFD = -1; }
  if (m_wakeFD >= 0) { ::close(m_wakeFD); m_wakeFD = -1; }
  if (m_wakeFD2 >= 0) { ::close(m_wakeFD2); m_wakeFD2 = -1; }

  // close tty device
  if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }

#else /* !_WIN32 */

  // close wakeup event
  if (m_wake != INVALID_HANDLE_VALUE) {
    CloseHandle(m_wake);
    m_wake = INVALID_HANDLE_VALUE;
  }

  // close console
  if (m_conin != INVALID_HANDLE_VALUE) {
    CloseHandle(m_conin);
    m_conin = INVALID_HANDLE_VALUE;
  }
  if (m_conout != INVALID_HANDLE_VALUE) {
    CloseHandle(m_conout);
    m_conout = INVALID_HANDLE_VALUE;
  }

#endif /* !_WIN32 */
}

bool Terminal::start_()
{
  if (m_running) return true;

  if (!isOpen_()) {
    m_errorFn("Terminal::start_() terminal not successfully opened");
    return false;
  }

  m_running = true;

#ifndef _WIN32
  memcpy(&m_ntermios, &m_otermios, sizeof(termios));

  // Note: do not interfere with old dial-up modem settings here
  // ntermios.c_cflag &= ~CSIZE;
  // ntermios.c_cflag |= CS8;
  // ~(BRKINT | INPCK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
  m_ntermios.c_iflag &= ~(ISTRIP | INLCR | IGNCR | ICRNL | IXON);
  m_ntermios.c_lflag &= ~(ICANON | ECHO | IEXTEN | ISIG);
  m_ntermios.c_oflag &= ~(OPOST | ONLCR | OCRNL | ONOCR | ONLRET);
  m_ntermios.c_cc[VMIN] = 1;
  m_ntermios.c_cc[VTIME] = 0;

  tcsetattr(m_fd, TCSADRAIN, &m_ntermios);

  if (!start__()) {
    tcsetattr(m_fd, TCSANOW, &m_otermios);
    return false;
  }

  tputs(m_cr);
  if (m_smkx) tputs(m_smkx);

#else /* !_WIN32 */

  if (m_conoutCP != 65001) SetConsoleOutputCP(65001);
  SetConsoleMode(m_conout, m_conoutMode |
      ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
      ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);

  m_out << '\r';

#endif /* !_WIN32 */

  write();
  m_pos = 0;

  return true;
}

void Terminal::stop_()
{
  if (!m_running) return;
  m_running = false;

#ifndef _WIN32
  if (m_rmkx) tputs(m_rmkx);
#endif

  write();

  clear();

#ifndef _WIN32

  if (m_fd < 0) return;

  stop__();

  tcsetattr(m_fd, TCSADRAIN, &m_otermios);

#else /* !_WIN32 */

  SetConsoleMode(m_conout, m_conoutMode);
  if (m_conoutCP != 65001) SetConsoleOutputCP(m_conoutCP);

#endif /* !_WIN32 */
}

#ifndef _WIN32
bool Terminal::start__()
{
  if (fcntl(m_fd, F_SETFL, O_NONBLOCK) < 0) {
    ZeError e{errno};
    close_fds();
    m_errorFn(ZrlError("fcntl(F_SETFL, O_NONBLOCK)", Zi::IOError, e));
    return false;
  }
  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN;
    ev.data.u64 = 0;
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, m_fd, &ev) < 0) {
      ZeError e{errno};
      close_fds();
      m_errorFn(ZrlError("epoll_ctl(EPOLL_CTL_ADD)", Zi::IOError, e));
      return false;
    }
  }
  return true;
}

void Terminal::stop__()
{
  epoll_ctl(m_epollFD, EPOLL_CTL_DEL, m_fd, 0);
  fcntl(m_fd, F_SETFL, 0);
}
#endif /* !_WIN32 */

Passwd Terminal::getpass(ZuCSpan prompt, unsigned passLen)
{
  Passwd passwd;
  passwd.size(passLen + 4); // allow for \r\n and null terminator
#ifndef _WIN32
  struct termios ntermios;
  memcpy(&ntermios, &m_otermios, sizeof(termios));
  ntermios.c_lflag &= ~ECHO;
  if (tcsetattr(m_fd, TCSADRAIN, &ntermios)) return {};
  if (m_running) stop__();
  ::write(m_fd, prompt.data(), prompt.length());
  int n = ::read(m_fd, passwd.data(), passwd.size() - 1);
  if (n > 0 && n < passwd.size()) passwd.length(n);
  ::write(m_fd, "\r\n", 2);
  if (m_running) {
    if (start__()) tcsetattr(m_fd, TCSANOW, &m_ntermios);
  } else
    tcsetattr(m_fd, TCSANOW, &m_otermios);
#else
  SetConsoleMode(m_conin,
      (m_coninMode | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT) &
      ~ENABLE_ECHO_INPUT);
  DWORD n = 0;
  WriteConsoleA(m_conin, prompt.data(), prompt.length(), &n, nullptr);
  n = 0;
  ReadConsoleA(m_conin, passwd.data(), passwd.size() - 1, &n, nullptr);
  if (n > 0 && n < passwd.size()) passwd.length(n);
  WriteConsoleA(m_conin, "\r\n", 2, &n, nullptr);
  SetConsoleMode(m_conin, m_coninMode &
      ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT));
#endif
  passwd.chomp();
  return passwd;
}

void Terminal::opost_on()
{
#ifndef _WIN32
  m_ntermios.c_oflag = m_otermios.c_oflag;
  tcsetattr(m_fd, TCSADRAIN, &m_ntermios);
#endif
}

void Terminal::opost_off()
{
#ifndef _WIN32
  m_ntermios.c_oflag &= ~(OPOST | ONLCR | OCRNL | ONOCR | ONLRET);
  tcsetattr(m_fd, TCSADRAIN, &m_ntermios);
#endif
}

void Terminal::cursor_on()
{
#ifndef _WIN32
  if (m_cnorm) tputs(m_cnorm);
#else
  m_out << "\x1b[?25h";
#endif
}

void Terminal::cursor_off()
{
#ifndef _WIN32
  if (m_civis) tputs(m_civis);
#else
  m_out << "\x1b[?25l";
#endif
}

void Terminal::bell()
{
#ifndef _WIN32
  if (m_bel) tputs(m_bel);
#else
  m_out << "\x07";
#endif
}

// I/O multiplexing

void Terminal::wake()
{
  m_sched->push([this]() { read(); }, m_thread);
  wake_();
}

void Terminal::wake_()
{
#ifndef _WIN32
  char c = 0;
  while (::write(m_wakeFD2, &c, 1) < 0) {
    ZeError e{errno};
    if (e.errNo() != EINTR && e.errNo() != EAGAIN) {
      m_errorFn(ZrlError("write", Zi::IOError, e));
      break;
    }
  }
#else /* !_WIN32 */
  if (!SetEvent(m_wake))
    m_errorFn(ZrlError("SetEvent", Zi::IOError, ZeLastError));
#endif /* !_WIN32 */
}

// display resizing

#ifndef _WIN32
void Terminal::sigwinch()
{
  run([this]() { resized(); });
}
#endif

void Terminal::resized()
{
#ifndef _WIN32

  struct winsize ws;
  if (ioctl(m_fd, TIOCGWINSZ, &ws) < 0) {
    m_width = tigetnum("columns");
    m_height = tigetnum("lines");
  } else {
    m_width = ws.ws_col;
    m_height = ws.ws_row;
  }
  if (!m_width) m_width = 80;
  if (!m_height) m_height = 24;

#else /* _WIN32 */

  CONSOLE_SCREEN_BUFFER_INFO info;
  if (!GetConsoleScreenBufferInfo(m_conout, &info)) {
    m_errorFn(ZrlError("GetConsoleScreenBufferInfo", Zi::IOError, ZeLastError));
    m_width = 80;
    m_height = 24;
    return;
  }
  const auto &size = info.dwSize;
  m_width = size.X;
  m_height = size.Y;

#endif /* _WIN32 */
}

// low-level input

void Terminal::read()
{
  int timeout = -1;
#ifndef _WIN32
  struct epoll_event ev;
#else
  HANDLE handles[2] = { m_wake, m_conin };
#endif
  const VKeyMatch *nextVKMatch = m_vkeyMatch.ptr();
#ifndef _WIN32
  using UTF = ZuUTF8;
  ZuArray<uint8_t, 4> utf;
#else
  using UTF = ZuUTF16;
  ZuArray<uint16_t, 2> utf;
#endif
  unsigned utfn = 0;
  ZtArray<int32_t> pending;
  for (;;) {

    // wait
#ifndef _WIN32
    int r = epoll_wait(m_epollFD, &ev, 1, timeout);
    if (r < 0) {
      ZeError e{errno};
      if (e.errNo() == EINTR || e.errNo() == EAGAIN)
	continue;
      m_errorFn(ZrlError("epoll_wait", Zi::IOError, e));
      m_keyFn(VKey::EndOfFile);
      goto stop;
    }
#else
    DWORD event = WaitForMultipleObjects(2, handles, false, timeout);
    if (event == WAIT_FAILED) {
      m_errorFn(ZrlError("WaitForMultipleObjects", Zi::IOError, ZeLastError));
      m_keyFn(VKey::EndOfFile);
      goto stop;
    }
#endif

    // check timeout
#ifndef _WIN32
    if (!r)
#else
    if (event == WAIT_TIMEOUT)
#endif
    {
      timeout = -1;
      nextVKMatch = m_vkeyMatch.ptr();
      for (auto key : pending) if (m_keyFn(key)) goto stop;
      pending.clear();
      if (utfn && utf) {
	uint32_t u = 0;
	if (UTF::in(utf.data(), utf.length(), u))
	  if (m_keyFn(u)) goto stop;
	utf.clear();
	utfn = 0;
      }
      continue;
    }

    // check wake up
#ifndef _WIN32
    if (ZuLikely(ev.data.u64 == 3)) {
      char c;
      int r = ::read(m_wakeFD, &c, 1);
      if (r >= 1) return;
      if (r < 0) {
	ZeError e{errno};
	if (e.errNo() != EINTR && e.errNo() != EAGAIN) return;
      }
      continue;
    }
    if (!(ev.events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)))
      continue;
#else
    if (event == WAIT_OBJECT_0) {
      ResetEvent(m_wake);
      return;
    }
#endif

    // read input
#ifndef _WIN32
    uint8_t c;
    {
      int r = ::read(m_fd, &c, 1);
      if (r < 0) {
	ZeError e{errno};
	if (e.errNo() == EINTR || e.errNo() == EAGAIN) continue;
	m_keyFn(VKey::EndOfFile);
	goto stop;
      }
      if (!r) {
	m_keyFn(VKey::EndOfFile);
	goto stop;
      }
    }
#else
    uint16_t c;
    {
      INPUT_RECORD rec;
      DWORD count = 0;
      if (!ReadConsoleInput(m_conin, &rec, 1, &count)) {
	m_keyFn(VKey::EndOfFile);
	goto stop;
      }
      if (!count) continue;
      if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
	const auto &size = rec.Event.WindowBufferSizeEvent.dwSize;
	m_width = size.X;
	m_height = size.Y;
	continue;
      }
      if (rec.EventType != KEY_EVENT) continue;
      int code = rec.Event.KeyEvent.wVirtualKeyCode;
      auto state = rec.Event.KeyEvent.dwControlKeyState;
      int32_t vkey;
      if (code == VK_PACKET) {
	vkey = rec.Event.KeyEvent.wVirtualScanCode;
	goto process_vkey;
      }
      c = rec.Event.KeyEvent.uChar.UnicodeChar;
      if (code == VK_MENU) {
	if (c) goto process_char;
	continue;
      }
      if (!rec.Event.KeyEvent.bKeyDown) continue;
      vkey = VKey::Null;
      switch (code) {
	case VK_SPACE: c = ' '; break;
	case VK_TAB: vkey = VKey::Tab; break;
	case VK_ESCAPE: c = '\x1b'; break;
	case VK_BACK: vkey = VKey::Erase; break;
	case VK_RETURN: vkey = VKey::Enter; break;
	case VK_PRIOR: vkey = VKey::PgUp; break;
	case VK_NEXT: vkey = VKey::PgDn; break;
	case VK_END: vkey = VKey::End; break;
	case VK_HOME: vkey = VKey::Home; break;
	case VK_LEFT: vkey = VKey::Left; break;
	case VK_UP: vkey = VKey::Up; break;
	case VK_RIGHT: vkey = VKey::Right; break;
	case VK_DOWN: vkey = VKey::Down; break;
	case VK_INSERT: vkey = VKey::Insert; break;
	case VK_DELETE: vkey = VKey::Delete; break;
	case 0x32: // '@'
	  if (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
	    c = 0;
	  break;
	case 0xbf: // '/'
	case 0xbd: // '_'
	  if (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
	    c = '_' - '@';
	  break;
	default:
	  if (!c) continue;
	  break;
      }
      if (vkey != VKey::Null) {
	if (state & SHIFT_PRESSED)
	  vkey |= VKey::Shift;
	if (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
	  vkey |= VKey::Ctrl;
	if (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
	  vkey |= VKey::Alt;
	vkey = -vkey;
process_vkey:
	timeout = -1;
	nextVKMatch = m_vkeyMatch.ptr();
	for (auto key : pending) if (m_keyFn(key)) goto stop;
	pending.clear();
	if (utfn && utf) {
	  uint32_t u = 0;
	  if (UTF::in(utf.data(), utf.length(), u))
	    if (m_keyFn(u)) goto stop;
	  utf.clear();
	  utfn = 0;
	}
	if (m_keyFn(vkey)) goto stop;
	continue;
      }
process_char:
      if (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
	pending << '\x1b';
    }
#endif

    // vkey matching
    if (!utfn) {
#ifdef _WIN32
      if (c < 0x100)
#endif
	if (auto action = nextVKMatch->match(c)) {
	  if (action->next) {
	    timeout = m_vkeyInterval;
	    nextVKMatch = action->next.ptr<VKeyMatch>();
	    if (action->vkey != -VKey::Null)
	      pending = { action->vkey };
	    else
	      pending << c;
	    continue;
	  }
	  timeout = -1;
	  nextVKMatch = m_vkeyMatch.ptr();
	  pending.clear();
	  m_keyFn(action->vkey);
	  continue;
	}
      timeout = -1;
      nextVKMatch = m_vkeyMatch.ptr();
      for (auto vkey : pending) if (m_keyFn(vkey)) goto stop;
      pending.clear();
      if (!utf8_in() || !(utfn = UTF::in(c))) utfn = 1;
    }

    // UTF input
    utf << c;
    if (--utfn > 0) continue;
    uint32_t u = 0;
    if (UTF::in(utf.data(), utf.length(), u))
      if (m_keyFn(u)) goto stop;
    utf.clear();
  }

stop:
  stop_();
}

void Terminal::addCtrlKey(char c, int32_t vkey)
{
  if (c) m_vkeyMatch->add(c, vkey);
}

#ifndef _WIN32
void Terminal::addVKey(const char *cap, const char *deflt, int32_t vkey)
{
  const char *ent;
  if (!(ent = tigetstr(cap))) ent = deflt;
  if (ent) {
    m_vkeyMatch->add(ent, vkey);
    // std::cout << ZtHexDump(cap, ent, strlen(ent)) << '\n';
  }
}
#endif

// low-level output

int Terminal::write(ZeError *e_)
{
#if 0
  // debug log
  {
    static FILE *log = nullptr;
    if (!log) log = fopen("tty.log", "a");
    if (log) {
      static ZeString dump;
      static ZuDateTimeFmt::FIX<-9> fix;
      dump.clear();
      dump << ZuDateTime{Zm::now()}.fix(fix);
      dump << ZtHexDump("", m_out.data(), m_out.length());
      fwrite(dump.data(), 1, dump.length(), log);
      fflush(log);
    }
  }
#endif
#ifndef _WIN32
  unsigned n = m_out.length();
  while (::write(m_fd, m_out.data(), n) < n) {
    ZeError e{errno};
    if (e.errNo() != EINTR && e.errNo() != EAGAIN) {
      m_errorFn(ZrlError("write", Zi::IOError, e));
      if (e_) *e_ = e;
      return Zi::IOError;
    }
  }
#else
  DWORD n;
loop:
  if (!WriteConsoleA(m_conout, m_out.data(), m_out.length(), &n, NULL)) {
    ZeError e = ZeLastError;
    m_errorFn(ZrlError("WriteConsole", Zi::IOError, e));
    if (e_) *e_ = e;
    return Zi::IOError;
  }
  if (n < m_out.length()) {
    m_out.splice(0, n);
    goto loop;
  }
#endif
  m_out.clear();
  return Zi::OK;
}

#ifndef _WIN32
void Terminal::tputs(const char *cap)
{
  localTerminal() = this;
  ::tputs(cap, 1, [](int c) -> int {
    localTerminal()->m_out << static_cast<char>(c);
    return 0;
  });
}
#endif

// output routines

// all cursor motion is in screen position units, regardless of
// half/full-width characters drawn - e.g. to back up over a
// full-width character, use 2x cub1 or cub(2)

// Note: cub/cuf after right-most character is undefined - need to use hpa/cr

// carriage-return
void Terminal::cr()
{
  unsigned n = m_pos % m_width;
  if (n) {
#ifndef _WIN32
    tputs(m_cr);
#else
    m_out << '\r';
#endif
    m_pos -= n;
  }
}

// new-line / index
void Terminal::nl()
{
#ifndef _WIN32
  tputs(m_ind);
#else
  m_out << '\n';
#endif
  m_pos += m_width;
}

// CR + NL, without updating m_pos
void Terminal::crnl_()
{
#ifndef _WIN32
  if (ZuLikely(m_nel)) {
    tputs(m_nel);
  } else {
    tputs(m_cr);
    tputs(m_ind);
  }
#else
  m_out << "\r\n";
#endif
}

// CR + NL
void Terminal::crnl()
{
#ifndef _WIN32
  if (ZuUnlikely(!m_nel)) {
    cr();
    nl();
    return;
  }
  tputs(m_nel);
#else
  crnl_();
#endif
  m_pos = bol(m_pos) + m_width;
}

// out row from cursor, breaking cursor to start of next row
void Terminal::outBreak(unsigned endPos)
{
  if (m_pos < endPos) {
    outWrap(endPos);
#ifndef _WIN32
    if (!m_am || m_xenl)
#endif
      crnl_();
  }
}

// out row from cursor, wrapping cursor to start of next row
void Terminal::outWrap(unsigned endPos)
{
  ZmAssert(!(endPos % m_width));	// endPos is EOL
  ZmAssert(m_pos >= endPos - m_width);	// cursor is >=BOL
  ZmAssert(m_pos <= endPos);		// cursor is <EOL

  if (m_pos < endPos) {
    out(endPos);
    clrWrap(endPos);
  }
}

// clear row from cursor, breaking to start of next row
void Terminal::clrBreak_(unsigned n)
{
  if (!n) { crnl_(); return; }
#ifndef _WIN32
  if (ZuLikely(m_el)) {
    tputs(m_el);
    crnl_();
    return;
  }
  if (ZuLikely(m_ech)) {
    tputs(tiparm(m_ech, n));
    crnl_();
    return;
  }
  clrOver_(n);
  if (!m_am || m_xenl) crnl_();
#else
  m_out << "\x1b[K\r\n";
#endif
}

void Terminal::out_(ZuCSpan data)
{
  unsigned begin = m_out.length();
  m_out << ZuBSpan(data);
  unsigned end = m_out.length();
  for (unsigned off = begin; off < end; ) {
    unsigned n;
    uint32_t u = 0;
    if ((n = ZuUTF8::in(&m_out[off], end - off, u)) > 1) {
#ifndef _WIN32
      if (ZuUnlikely(!utf8_out())) {
	unsigned w = ZuUTF32::width(u);
	if (ZuUnlikely(m_ul)) {
	  m_out.splice(off, n, m_underline);
	  if (w == 1)
	    w = m_underline.length();
	  else {
	    m_out.splice(off, 0, m_underline);
	    w = m_underline.length();
	    w <<= 1;
	  }
	} else
	  m_out.splice(off, n, ZuBSpan("__", w));
	off += w; end += w; end -= n;
      } else
#endif
	off += n;
    } else {
      if (u < 0x20 || u == 0x7f) {
	ZuArray<char, 2> s;
	s << '^' << (u == 0x7f ? '?' : static_cast<char>('@' + u));
	m_out.splice(off, 1, s);
	off += 2; end += 1;
      }
#ifndef _WIN32
      else if ((m_hz && u == '~') || (m_ul && u == '_')) {
	m_out.splice(off, 1, m_underline);
	unsigned w = m_underline.length();
	off += w; end += w - 1;
      }
#endif
      else
	off += 1;
    }
  }
}

void Terminal::clrWrap_(unsigned n)
{
  clrOver_(n);
}

// clear remainder of row from cursor, leaving cursor at start of next row
void Terminal::clrWrap(unsigned endPos)
{
  if (m_pos < endPos) {
    unsigned n = endPos - m_pos;
    unsigned w = endPos < m_line.width() ? m_line.position(endPos).len() : 0;
    if (n >= w
#ifndef _WIN32
	|| !m_am
#endif
	) clrWrap_(n);
    m_pos = endPos;
  }
}

// clear remainder of row from cursor, leaving cursor at start of next row
void Terminal::clrBreak(unsigned endPos)
{
  clrBreak_(endPos - m_pos);
  m_pos = endPos;
}

// out row from cursor, leaving cursor on same row at pos
void Terminal::outNoWrap(unsigned endPos, unsigned pos)
{
  ZmAssert(!(endPos % m_width));	// endPos is EOL
  ZmAssert(m_pos >= endPos - m_width);	// cursor is >=BOL
  ZmAssert(m_pos <= endPos);		// cursor is <=EOL
  ZmAssert(pos >= endPos - m_width);	// pos is >=BOL
  ZmAssert(pos < endPos);		// pos is <EOL

#ifndef _WIN32
  if (!m_am || m_xenl)
#endif
  {
    // normal case - terminal is am + xenl, or no am, i.e. doesn't scroll
    // immediately following overwrite of right-most character, just output
    // entire row then move cursor within row as needed
    if (m_pos < endPos) {
      out(endPos);
      if (m_pos < endPos) {
#ifndef _WIN32
	if (ZuLikely(m_el)) {	// clear to EOL
	  tputs(m_el);
	  if (m_pos != pos) mvhoriz(pos);
	  return;
	}
	if (ZuLikely(m_ech)) {	// clear to EOL with ech
	  tputs(tiparm(m_ech, endPos - m_pos));
	  if (m_pos != pos) mvhoriz(pos);
	  return;
	}
	clrOver(endPos);	// clear to EOL with spaces
#else
	m_out << "\x1b[K";
	if (m_pos != pos) mvhoriz(pos);
	return;
#endif
      }
    }
    // right-most character on row was output, MUST move cursor, NO cub
#ifndef _WIN32
    if (m_hpa) {
      tputs(tiparm(m_hpa, pos % m_width));
      m_pos = pos;
      return;
    }
    tputs(m_cr);
    m_pos = endPos - m_width;
    if (pos > m_pos) mvright(pos);
    return;
#else
    m_out << "\x1b[" << ZuBoxed((pos % m_width) + 1) << 'G';
    m_pos = pos;
    return;
#endif
  }

#ifndef _WIN32
  if (m_ich || m_ich1 || (m_smir && m_rmir)) {
    // insert right-most character to leave cursor on same row
    outClr(endPos - 2);
    ++m_pos; // skip a position
    outClr(endPos);
    --m_pos;
    mvleft(endPos - 2);
    if (m_ich1) {
      tputs(m_ich1);
      outClr(endPos - 1);
    } else if (m_ich) {
      tputs(tiparm(m_ich, 1));
      outClr(endPos - 1);
    } else if (m_smir) {
      tputs(m_smir);
      outClr(endPos - 1);
      tputs(m_rmir);
    }
    if (pos < m_pos) mvleft(pos);
    return;
  }

  // dumb terminal, cannot output right-most character leaving cursor on row
  outClr(endPos - 1);
  if (pos < m_pos) mvleft(pos);
#endif
}

// output from m_pos to endPos within row, clearing any trailing space
void Terminal::outClr(unsigned endPos)
{
  if (m_pos < endPos) out(endPos);
  if (m_pos < endPos) clrOver(endPos);
}

#if defined(ZDEBUG) && !defined(Zrl_DEBUG)
#define Zrl_DEBUG
#endif

void Terminal::out(unsigned endPos)
{
  if (endPos > 0) endPos = m_line.align(endPos - 1);
  unsigned end = m_line.position(endPos).mapping();
  end += m_line.byte(end).len();
  endPos += m_line.position(endPos).len(); // may wrap to start of next row
  unsigned off = m_line.position(m_pos).mapping();
  if (end > off) out_(m_line.substr(off, end - off));
  m_pos = endPos;
}

// clear with ech
void Terminal::clrErase_(unsigned n)
{
#ifndef _WIN32
  tputs(tiparm(m_ech, n));
#else
  m_out << "\x1b[" << ZuBoxed(n) << 'X';
#endif
}

// clear with ech, falling back to spaces - endPos is not past end of row
void Terminal::clr(unsigned endPos)
{
#ifndef _WIN32
  if (ZuLikely(m_ech))
#endif
    clrErase_(endPos - m_pos);
#ifndef _WIN32
  else
    clrOver(endPos);
#endif
}

// clear by overwriting spaces
void Terminal::clrOver_(unsigned n)
{
  for (unsigned i = 0; i < n; i++) m_out << ' ';
}

// clear by overwriting spaces
void Terminal::clrOver(unsigned endPos)
{
  clrOver_(endPos - m_pos);
  m_pos = endPos;
}

// Note: This implementation does not support terminals lacking cuu/cuu1.
//
// There are only 34 out of 1550 non-hardcopy terminals in the
// terminfo database that lack the cuu/cuu1 capability.
//
// All such terminals are ancient, "dumb" or in "line mode".
//
// Prominent examples are Matrix Orbital LCDs, the
// Lear-Siegler ADM3, the Plan 9 terminal, obsolete DEC gt40/42,
// Hazeltine 1000/2000, ansi-mini (ansi-mr would be a good substitute),
// Tektronix 40xx and 41xx, GE Terminet, TI 7xx, Xerox 17xx, and
// IBM 3270s in line mode

// move cursor to position
void Terminal::mv(unsigned pos)
{
  if (pos < m_pos) {
    if (unsigned up = m_pos / m_width - pos / m_width) {
#ifndef _WIN32
      if (ZuLikely(m_cuu)) {
	m_pos -= up * m_width;
	tputs(tiparm(m_cuu, up));
      } else if (ZuLikely(m_cuu1)) {
	m_pos -= up * m_width;
	do { tputs(m_cuu1); } while (--up);
      } else { // no way to go up, just reprint destination row
	tputs(m_cr);
	m_pos = bol(pos);
	outNoWrap(m_pos + m_width, pos);
	return;
      }
#else
      m_pos -= up * m_width;
      m_out << "\x1b[" << ZuBoxed(up) << 'A';
#endif
      if (ZuUnlikely(m_pos == pos)) return;
      mvhoriz(pos);
    } else
      mvleft(pos);
  } else if (pos > m_pos) {
    if (unsigned down = pos / m_width - m_pos / m_width) {
      if (pos >= m_line.width()) {
	cr();
	nl();
	--down;
      }
      if (down) {
#ifndef _WIN32
	if (ZuLikely(m_cud)) {
	  m_pos += down * m_width;
	  tputs(tiparm(m_cud, down));
	} else if (ZuLikely(m_cud1)) {
	  m_pos += down * m_width;
	  do { tputs(m_cud1); } while (--down);
	} else {
	  cr();
	  do { nl(); } while (--down);
	}
#else
	m_pos += down * m_width;
	m_out << "\x1b[" << ZuBoxed(down) << 'B';
#endif
      }
      if (ZuUnlikely(m_pos == pos)) return;
      mvhoriz(pos);
    } else
      mvright(pos);
  }
}

// move cursor horizontally
void Terminal::mvhoriz(unsigned pos)
{
  if (pos < m_pos)
    mvleft(pos);
  else
    mvright(pos);
}

void Terminal::mvleft(unsigned pos)
{
  ZmAssert(m_pos >= bol(pos));			// cursor is >=BOL
  ZmAssert(m_pos < bol(pos) + m_width);		// cursor is <EOL
  ZmAssert(m_pos > pos);			// cursor is right of pos

#ifndef _WIN32
  if (ZuLikely(m_cub)) {
    tputs(tiparm(m_cub, m_pos - pos));
  } else if (ZuLikely(m_hpa)) {
    tputs(tiparm(m_hpa, pos % m_width));
  } else {
    for (unsigned i = 0, n = m_pos - pos; i < n; i++) tputs(m_cub1);
  }
#else
  m_out << "\x1b[" << ZuBoxed(m_pos - pos) << 'D';
#endif
  m_pos = pos;
}

void Terminal::mvright(unsigned pos)
{
#ifndef _WIN32
  if (ZuLikely(m_cuf)) {
    tputs(tiparm(m_cuf, pos - m_pos));
  } else if (ZuLikely(m_hpa)) {
    tputs(tiparm(m_hpa, pos % m_width));
#if 0
  } else if (m_cuf1) {
    for (unsigned i = 0, n = pos - m_pos; i < n; i++) tputs(m_cuf1);
#endif
  } else {
    outClr(pos);
    return;
  }
#else
  m_out << "\x1b[" << ZuBoxed(pos - m_pos) << 'C';
#endif
  m_pos = pos;
}

bool Terminal::ins_(unsigned n, bool mir)
{
  if (mir) return true;
#ifndef _WIN32
  if (m_ich) {
    tputs(tiparm(m_ich, n));
    return false;
  }
  if (m_smir) {
    tputs(m_smir);
    return true;
  }
  for (unsigned i = 0; i < n; i++) tputs(m_ich1);
#else
  m_out << "\x1b[" << ZuBoxed(n) << '@';
#endif
  return false;
}

void Terminal::del_(unsigned n)
{
#ifndef _WIN32
  if (m_smdc) tputs(m_smdc);
  if (m_dch)
    tputs(tiparm(m_dch, n));
  else
    for (unsigned i = 0; i < n; i++) tputs(m_dch1);
  if (m_rmdc) tputs(m_rmdc);
#else
  m_out << "\x1b[" << ZuBoxed(n) << 'P';
#endif
}

// encodes a glyph mark as a byte offset and display position into 64bits
class GlyphMark {
  static constexpr unsigned shift() { return 16; }
  static constexpr unsigned mask() { return ((1<<shift()) - 1); }

public:
  GlyphMark() = default;
  GlyphMark(const GlyphMark &) = default;
  GlyphMark &operator =(const GlyphMark &) = default;
  GlyphMark(GlyphMark &&) = default;
  GlyphMark &operator =(GlyphMark &&) = default;
  ~GlyphMark() = default;

  GlyphMark(uint32_t byte, uint32_t pos) : m_value{byte | (pos<<shift())} { }

  unsigned byte() const { return m_value & mask(); }
  unsigned pos() const { return m_value>>shift(); }

  bool operator !() const { return !m_value; }
  ZuOpBool

private:
  uint32_t	m_value = 0;
};

ZuDerive(GlyphMarks, (ZtArray<GlyphMark, ZtArrayHeapID<"Zrl.GlyphMark">>));

void Terminal::splice(
    unsigned off, ZuUTFSpan span,
    ZuBSpan replace, ZuUTFSpan rspan,
    bool append)
{
  ZmAssert(off == m_line.position(m_pos).mapping());

  bool shiftLeft = false, shiftRight = false;
  unsigned trailRows = 0;
  unsigned bolPos = bol(m_pos);
  unsigned oldWidth = m_line.width();

  // it's worth optimizing the common case where a long line of input
  // is being interactively edited at its beginning: When shifting the
  // trailing data, if the shift distance is less than half the width
  // of the display, and the overall width of the trailing data is greater
  // than the shift distance, it's worth leaving the old data in
  // place on the terminal and using insertions/deletions on each
  // trailing row rather than completely redrawing all the trailing rows
  if (off + span.inLen() < m_line.length()) {
    int trailWidth = oldWidth - m_pos; // width of trailing data

    if (trailWidth > 0) {
      if (rspan.width() < span.width()) {
	unsigned shiftWidth = span.width() - rspan.width();
	if (shiftWidth < (m_width>>1) && trailWidth > shiftWidth
#ifndef _WIN32
	    && (m_dch || m_dch1 || (m_smdc && m_rmdc))
#endif
	    )
	  shiftLeft = true; // shift left using deletions on each line
      } else if (rspan.width() > span.width()) {
	unsigned shiftWidth = rspan.width() - span.width();
	if (shiftWidth < (m_width>>1) && trailWidth > shiftWidth
#ifndef _WIN32
	    && (m_ich || m_ich1 || (m_smir && m_rmir))
#endif
	    )
	  shiftRight = true; // shift right using insertions on each line
      }
      if (shiftLeft || shiftRight)
	trailRows = (bol(oldWidth) - bolPos) / m_width + 1;
    }
  }
  auto glyphMarks = ZtLocalArray(GlyphMarks, trailRows);
  if (glyphMarks.size()) {
    unsigned endPos = m_pos + span.inLen();
    unsigned shiftOff;
    if (shiftLeft)
      shiftOff = span.inLen() - rspan.inLen();
    else
      shiftOff = rspan.inLen() - span.inLen();
    while (bolPos < oldWidth && glyphMarks.length() < trailRows) {
      if (shiftLeft) {
	// shift left - glyphMarks saves the old position of each trailing EOL
	unsigned pos = eol(bolPos);
	if (pos >= endPos)
	  glyphMarks.push(
	    GlyphMark{m_line.position(pos).mapping() - shiftOff, pos});
      } else {
	// shift right - glyphMarks saves the old position of each trailing BOL
	unsigned pos = bolPos < endPos ? endPos : bolPos;
	glyphMarks.push(
	  GlyphMark{m_line.position(pos).mapping() + shiftOff, pos});
      }
      bolPos += m_width;
    }
    trailRows = glyphMarks.length();
  } else {
    shiftLeft = shiftRight = false;
    trailRows = 0;
  }

  // splice in the new data
  m_line.data().splice(off, span.inLen(), replace);

  // reflow the line, from offset onwards
  m_line.reflow(off, m_width);
  mv(m_line.byte(off).mapping());

  // calculate reflowed endPos
  unsigned endPos = m_line.byte(off + rspan.inLen()).mapping();
  unsigned endBOLPos = bol(!endPos ? 0 : endPos - 1);

  // out/scroll all but last row of replacement data
  bolPos = bol(m_pos);
  while (bolPos < endBOLPos) outWrap(bolPos += m_width);

  // adjust endPos to final cursor position
  if (!append && endPos) endPos = m_line.align(endPos - 1);

  // out/scroll trailing data
  unsigned lineWidth = m_line.width();
  bolPos = endBOLPos;
  if (bolPos < lineWidth) {
    // this shift left/right code carefully evades a number of obscure pitfalls
    unsigned lastBOLPos = bol(lineWidth);
    unsigned row = 0;
    bool smir = false;
    for (;;) {
      if (row < trailRows) {
	// find the glyphMark in each row, and use it to shift the row
	if (shiftLeft) {
	  GlyphMark glyphMark;
	  unsigned rightPos;
	  do {
	    glyphMark = glyphMarks[row++];
	    rightPos = m_line.byte(glyphMark.byte()).mapping();
	  } while (row < trailRows && rightPos <= bolPos);
	  if (rightPos <= glyphMark.pos() &&
	      rightPos > bolPos && rightPos < bolPos + m_width) {
	    if (rightPos < glyphMark.pos()) del_(glyphMark.pos() - rightPos);
	    rightPos += m_line.position(rightPos).len();
	    mvright(rightPos);
	  } else if (rightPos > bolPos)
	    --row;
	} else if (shiftRight) {
	  GlyphMark glyphMark;
	  unsigned leftPos;
	  do {
	    glyphMark = glyphMarks[row++];
	    leftPos = m_line.byte(glyphMark.byte()).mapping();
	  } while (row < trailRows && leftPos <= bolPos);
	  if (leftPos >= glyphMark.pos() &&
	      leftPos > bolPos && leftPos < bolPos + m_width) {
	    if (leftPos > glyphMark.pos())
	      smir = ins_(leftPos - glyphMark.pos(), smir);
	    outClr(leftPos);
#ifndef _WIN32
	    if (smir && !m_mir) { tputs(m_rmir); smir = false; }
#endif
	    bolPos += m_width;
	    if (bolPos >= lastBOLPos) break;
	    crnl();
	    continue;
	  }
	  if (leftPos > bolPos) --row;
#ifndef _WIN32
	  if (smir && !m_mir) { tputs(m_rmir); smir = false; }
#endif
	}
      }
      bolPos += m_width;
      if (bolPos >= lastBOLPos) break;
      outWrap(bolPos);
    }
    // print the last row, taking great care not to scroll unnecessarily
    if (bolPos > lineWidth) bolPos -= m_width;
    if (m_pos < lineWidth) {
      if (bolPos < lineWidth)
	outClr(lineWidth);
      else if (endPos == lineWidth || lineWidth < oldWidth)
	outBreak(lineWidth);
      else
	// park cursor next to right-most
	outNoWrap(lineWidth, m_line.align(lineWidth - 2));
    }
#ifndef _WIN32
    if (smir) tputs(m_rmir);
#endif
  }

  // if the length of the line was reduced, clear to the end of the old line
  if (lineWidth < oldWidth) {
    unsigned clrPos = bol(oldWidth - 1);
    while (bolPos < clrPos) clrBreak(bolPos += m_width);
    if (bolPos < oldWidth) clr(oldWidth);
  }

  // move cursor to final position
  mv(endPos);
}

void Terminal::clear()
{
  m_line.clear();
  m_pos = 0;
}

void Terminal::cls()
{
#ifndef _WIN32
  tputs(m_clear);
#else
  m_out << "\x1b[H\x1b[J";
#endif
  redraw();
}

void Terminal::redraw()
{
  unsigned pos = m_pos;
  mv(0);
  redraw_(m_line.width());
  mv(pos);
}

void Terminal::redraw(unsigned endPos, bool high)
{
  if (ZuUnlikely(m_pos >= endPos)) return;
#ifndef _WIN32
  enum { None, Bold, Standout };
  int highType = None;
#endif
  if (high) {
#ifndef _WIN32
    if (m_smso && m_rmso) {
      highType = Standout;
      tputs(m_smso);
    } else if (m_bold && (m_sgr || m_sgr0)) {
      highType = Bold;
      tputs(m_bold);
    } else if (m_sgr) {
      highType = Bold;
      tputs(tiparm(m_sgr, 0, 0, 0, 0, 0, 1, 0, 0, 0));
    }
#else
    m_out << "\x1b[7m";
#endif
  }
  redraw_(endPos);
  if (high) {
#ifndef _WIN32
    switch (highType) {
      case Bold:
	if (m_sgr0)
	  tputs(m_sgr0);
	else
	  tputs(tiparm(m_sgr, 0, 0, 0, 0, 0, 0, 0, 0, 0));
	break;
      case Standout:
	tputs(m_rmso);
	break;
    }
#else
    m_out << "\x1b[m";
#endif
  }
}

void Terminal::redraw_(unsigned endPos)
{
  unsigned bolPos = bol(m_pos);
  unsigned endBOLPos = bol(!endPos ? 0 : endPos - 1);
  for (;;) {
    bolPos += m_width;
    if (bolPos >= endBOLPos) break;
    outWrap(bolPos);
  }
  if (bolPos > endPos) bolPos -= m_width;
  if (m_pos < endPos) {
    if (bolPos < endPos)
      outClr(endPos);
    else
      outNoWrap(endPos, m_line.align(endPos - 2));
  }
}

int32_t Terminal::literal(int32_t vkey) const
{
#ifndef _WIN32
  switch (int(-vkey)) {
    case VKey::EndOfFile:	return m_otermios.c_cc[VEOF];
    case VKey::SigInt:		return m_otermios.c_cc[VINTR];
    case VKey::SigQuit:		return m_otermios.c_cc[VQUIT];
    case VKey::SigSusp:		return m_otermios.c_cc[VSUSP];
    case VKey::Enter:		return '\r';
    case VKey::Tab:		return '\t';
    case VKey::Erase:		return m_otermios.c_cc[VERASE];
    case VKey::WErase:		return m_otermios.c_cc[VWERASE];
    case VKey::Kill:		return m_otermios.c_cc[VKILL];
    case VKey::LNext:		return m_otermios.c_cc[VLNEXT];
#ifdef VREPRINT
    case VKey::Redraw:		return m_otermios.c_cc[VREPRINT];
#endif
  }
#else /* !_WIN32 */
  switch (int(-vkey)) {
    case VKey::EndOfFile:	return '\x04';	// ^D
    case VKey::SigInt:		return '\x03';	// ^C
    case VKey::SigQuit:		return '\x1c';	// ^ backslash
    case VKey::SigSusp:		return '\x1a';	// ^Z
    case VKey::Enter:		return '\r';	// ^M
    case VKey::Tab:		return '\t';	// ^I
    case VKey::Erase:		return '\x08';	// ^H
    case VKey::WErase:		return '\x17';	// ^W
    case VKey::Kill:		return '\x15';	// ^U
    case VKey::LNext:		return '\x16';	// ^V
    case VKey::Redraw:		return '\x12';	// ^R
  }
#endif /* !_WIN32 */
  return vkey;
}

void Terminal::dumpVKeys_(ZuVStream &s) const
{
  if (m_vkeyMatch) s << *m_vkeyMatch;
}

} // namespace Zrl
