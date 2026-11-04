# Linux UAPI only; deployed processes still probe their own credentials/kernel.
AC_DEFUN([ZI_IO_URING], [
  AC_ARG_ENABLE([io-uring],
    [AS_HELP_STRING([--enable-io-uring=auto|yes|no], [Linux io_uring backend @<:@auto@:>@])],
    [z_uring=$enableval], [z_uring=auto])
  AS_CASE([$z_uring], [auto|yes|no], [],
    [AC_MSG_ERROR([invalid --enable-io-uring value: $z_uring])])
  z_have_uring=0
  z_uring_reason=disabled
  AS_IF([test "$z_uring" != no], [
    z_uring_reason="target is not Linux"
    AS_IF([test "$z_linux" = yes], [
      AC_MSG_CHECKING([for required io_uring Linux UAPI])
      AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <linux/io_uring.h>
#include <sys/syscall.h>
#include <unistd.h>
      ]], [[
        io_uring_params p = {};
        io_uring_sqe s = {};
        io_uring_cqe c = {};
        io_uring_buf_ring b = {};
        io_uring_buf_reg r = {};
        io_uring_recvmsg_out o = {};
        io_uring_getevents_arg a = {};
        unsigned flags = IORING_RECV_MULTISHOT | IORING_RECVSEND_FIXED_BUF |
          IORING_ASYNC_CANCEL_ALL | IORING_ASYNC_CANCEL_ANY |
          IORING_POLL_ADD_MULTI | IORING_ENTER_EXT_ARG | IORING_CQE_F_NOTIF |
          IORING_CQE_F_MORE | IORING_CQE_F_BUFFER | IORING_SETUP_CQSIZE |
          IORING_FEAT_NODROP | IORING_SQ_CQ_OVERFLOW;
        unsigned control = IOSQE_BUFFER_SELECT + IO_URING_OP_SUPPORTED +
          IORING_OFF_CQ_RING + IORING_CQE_BUFFER_SHIFT +
          IORING_ENTER_GETEVENTS +
          IORING_FEAT_SINGLE_MMAP +
          IORING_OFF_SQES +
          IORING_OFF_SQ_RING +
          IORING_OP_LAST +
          IORING_OP_NOP;
        unsigned syscalls = __NR_io_uring_setup + __NR_io_uring_enter;
        unsigned ops = IORING_OP_SEND_ZC + IORING_OP_SENDMSG_ZC +
          IORING_OP_RECV + IORING_OP_RECVMSG + IORING_OP_ASYNC_CANCEL +
          IORING_OP_POLL_ADD + IORING_OP_READ + IORING_OP_SEND + IORING_OP_SENDMSG;
        return syscall(__NR_io_uring_register, -1,
          IORING_REGISTER_PBUF_RING + IORING_REGISTER_BUFFERS + IORING_REGISTER_PROBE,
          &r, flags + ops + control + syscalls + sizeof(p) + sizeof(s) + sizeof(c) +
            sizeof(b) + sizeof(o) + sizeof(a));
      ]])], [z_uring_uapi=yes], [z_uring_uapi=no])
      AC_MSG_RESULT([$z_uring_uapi])
      z_uring_reason="required Linux UAPI unavailable"
      AS_IF([test "$z_uring_uapi" = yes], [
        AC_MSG_CHECKING([whether io_uring can complete a bounded NOP])
        AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <linux/io_uring.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
int main() {
  alarm(2);
  io_uring_params p = {};
  int fd = syscall(__NR_io_uring_setup, 2, &p);
  if (fd < 0) return 1;
  unsigned sqSize = p.sq_off.array + p.sq_entries * sizeof(unsigned);
  unsigned cqSize = p.cq_off.cqes + p.cq_entries * sizeof(io_uring_cqe);
  bool single = p.features & IORING_FEAT_SINGLE_MMAP;
  if (single && cqSize > sqSize) sqSize = cqSize;
  auto sq = static_cast<char *>(mmap(0, sqSize, PROT_READ | PROT_WRITE,
    MAP_SHARED, fd, IORING_OFF_SQ_RING));
  auto cq = single ? sq : static_cast<char *>(mmap(0, cqSize,
    PROT_READ | PROT_WRITE, MAP_SHARED, fd, IORING_OFF_CQ_RING));
  auto es = static_cast<io_uring_sqe *>(mmap(0, p.sq_entries * sizeof(io_uring_sqe),
    PROT_READ | PROT_WRITE, MAP_SHARED, fd, IORING_OFF_SQES));
  int result = 1;
  if (sq != MAP_FAILED && cq != MAP_FAILED && es != MAP_FAILED) {
    es[0] = {};
    es[0].opcode = IORING_OP_NOP;
    es[0].user_data = 1;
    *reinterpret_cast<unsigned *>(sq + p.sq_off.array) = 0;
    __atomic_store_n(reinterpret_cast<unsigned *>(sq + p.sq_off.tail), 1, __ATOMIC_RELEASE);
    if (syscall(__NR_io_uring_enter, fd, 1, 1, IORING_ENTER_GETEVENTS, 0, 0) >= 0) {
      unsigned tail = __atomic_load_n(reinterpret_cast<unsigned *>(cq + p.cq_off.tail), __ATOMIC_ACQUIRE);
      auto c = reinterpret_cast<io_uring_cqe *>(cq + p.cq_off.cqes);
      if (tail && c[0].user_data == 1 && c[0].res == 0) result = 0;
    }
  }
  if (es != MAP_FAILED) munmap(es, p.sq_entries * sizeof(io_uring_sqe));
  if (!single && cq != MAP_FAILED) munmap(cq, cqSize);
  if (sq != MAP_FAILED) munmap(sq, sqSize);
  close(fd);
  return result;
}
        ]])], [z_have_uring=1; z_uring_reason=usable],
          [z_uring_reason="ring creation or bounded NOP failed (kernel or process policy)"],
          [z_have_uring=1; z_uring_reason="runtime unknown (cross compiling; deferred to target)"])
        AC_MSG_RESULT([$z_uring_reason])
      ])
    ])
    AS_IF([test "$z_uring" = yes && test "$z_have_uring" = 0],
      [AC_MSG_ERROR([io_uring requested but $z_uring_reason])])
  ])
  AC_SUBST([z_have_uring])
  AC_CONFIG_FILES([zi/src/ZiConfig.hh])
])
