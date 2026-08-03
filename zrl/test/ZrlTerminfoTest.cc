#include <zlib/ZuTestUtil.hh>

using namespace ZuTestUtil;

#ifdef _WIN32
static void terminfo() {
  ZuTestScope(terminfo);
  ZuCHECK(true, "terminfo is not used on Windows");
}
#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/unistd.h>

#include <zlib/ZuArray.hh>

#include <zlib/ZrlTerminfo.hh>

#define CHECK(x) ZuCHECK(x, #x)

namespace {
char *tigetstr_(const char *cap) {
  char *s = ::tigetstr(cap);
  if (s == reinterpret_cast<char *>(static_cast<uintptr_t>(-1)))
    s = nullptr;
  return s;
}
bool tigetflag_(const char *cap) {
  int i = ::tigetflag(cap);
  if (i < 0) i = 0;
  return i;
}
#if 0
unsigned tigetnum_(const char *cap) {
  int i = ::tigetnum(cap);
  if (i < 0) i = 0;
  return i;
}
#endif
template <typename ...Args>
char *tiparm_(const char *cap, Args &&...args) {
  return ::tiparm(cap, static_cast<int>(ZuFwd<Args>(args))...);
}
void putenv_(const char *s) {
  putenv(const_cast<char *>(s));
}
}

static void terminfo()
{
  ZuTestScope(terminfo);
  int fd = ::open("/dev/tty", O_RDWR, 0);
  if (fd < 0) {
    ZuCHECK(true, "/dev/tty unavailable; terminfo environment skipped");
    return;
  }

  {
    putenv_("TERM=hz1500");
    setupterm(nullptr, fd, nullptr);
    CHECK(tigetflag_("hz"));
    CHECK(tigetflag_("am"));
    CHECK(!tigetflag_("xenl"));
    del_curterm(cur_term);
  }
  {
    putenv_("TERM=vt100");
    setupterm(nullptr, fd, nullptr);
    CHECK(!tigetflag_("hz"));
    CHECK(tigetflag_("am"));
    CHECK(tigetflag_("xenl"));
    const char *cuu = tigetstr_("cuu");
    CHECK(cuu);
    if (cuu) {
      CHECK(!strcmp(tiparm_(cuu, 1), "\x1b[1A"));
    }
    del_curterm(cur_term);
  }
  ::close(fd);
}

#endif /* !_WIN32 */

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(terminfo);
  return 0;
}
