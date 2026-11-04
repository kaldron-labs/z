//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZiPlatform.hh>

#include <zlib/ZmScratch.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZiIOBuf.hh>

#ifndef _WIN32

#include <pwd.h>
#include <sys/syscall.h>
#include <time.h>
#if Zi_URing
#include <linux/time_types.h>
#endif

Zi::Username Zi::username(ZeError *e)
{
  struct passwd pwd;
  struct passwd *result = 0;
  Zi::Username name;

  ssize_t size = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (size > UINT_MAX) return name;
  unsigned bufSize = size < 0 ? (1<<14) : unsigned(size);
  if (!bufSize) return name;
  auto pwdBuf = ZmScratch(char, bufSize);

  int s = getpwuid_r(geteuid(), &pwd, pwdBuf.data(), bufSize, &result);
  if (!result && s != 0) {
    if (e) *e = ZeError(s);
  } else if (result) {
    name = pwd.pw_name;
  }
  return name;
}

Zi::Hostname Zi::hostname(ZeError *e)
{
  Zi::Hostname name;
  name.size(HOST_NAME_MAX + 1);
  int s = gethostname(name.data(), HOST_NAME_MAX);
  if (s < 0) { name.null(); return name; }
  name.calcLength();
  name.truncate();
  return name;
}

#if Zi_URing
class ZiPlatform_URing {
public:
  ZiPlatform_URing() {
    Zi::IOBufRing ring;
    if (!ring.init(1)) return;
    // Probe required opcodes once; this is not a kernel-version guess.
    enum { N = IORING_OP_LAST };
    auto bytes = ZmScratch(uint8_t,
      sizeof(io_uring_probe) + N * sizeof(io_uring_probe_op), Zi::VHeap);
    memset(bytes.data(), 0, bytes.size());
    auto probe = reinterpret_cast<io_uring_probe *>(bytes.data());
    if (syscall(__NR_io_uring_register, ring.fd(), IORING_REGISTER_PROBE,
	probe, N) < 0) return;
    constexpr unsigned ops[] = {
      IORING_OP_NOP, IORING_OP_RECV, IORING_OP_RECVMSG,
      IORING_OP_SEND_ZC, IORING_OP_SENDMSG_ZC,
      IORING_OP_POLL_ADD, IORING_OP_READ, IORING_OP_ASYNC_CANCEL
    };
    for (auto op : ops)
      if (op > probe->last_op || op >= probe->ops_len ||
	  !(probe->ops[op].flags & IO_URING_OP_SUPPORTED)) return;

    // Complete a harmless operation to establish actual process usability.
    const auto &sq = ring.sq();
    sq.sqes[0] = {};
    sq.sqes[0].opcode = IORING_OP_NOP;
    sq.array[0] = 0;
    *sq.tail = 1U;
    timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += ProbeSecs;
    io_uring_cqe cqe;
    if (!complete_(ring, deadline, cqe) || cqe.res) return;
    m_enabled = probe_(ring, deadline);
  }

  bool enabled() const { return m_enabled; }

private:
  // Optional-backend startup has one bounded CQ wait budget, including EINTR.
  enum { ProbeSecs = 1 };

  static bool complete_(Zi::IOBufRing &ring, const timespec &deadline,
    io_uring_cqe &cqe)
  {
    const auto &sq = ring.sq();
    const auto &cq = ring.cq();
    for (;;) {
      timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      __kernel_timespec remaining{
	deadline.tv_sec - now.tv_sec, deadline.tv_nsec - now.tv_nsec};
      if (remaining.tv_nsec < 0) {
	--remaining.tv_sec;
	remaining.tv_nsec += 1000000000;
      }
      if (remaining.tv_sec < 0 || (!remaining.tv_sec && !remaining.tv_nsec))
	return false;
      io_uring_getevents_arg args{};
      args.ts = uintptr_t(&remaining);
      unsigned n = sq.tail->load_() - unsigned(*sq.head);
      int rc = syscall(__NR_io_uring_enter, ring.fd(), n, 1,
	IORING_ENTER_GETEVENTS | IORING_ENTER_EXT_ARG, &args, sizeof(args));
      unsigned head = cq.head->load_();
      if (head != unsigned(*cq.tail)) {
	cqe = cq.cqes[head & cq.mask];
	*cq.head = head + 1;
	return true;
      }
      if (rc < 0 && errno == EINTR) continue;
      return false;
    }
  }

  // Opcode probing alone does not establish support for these modifiers.
  // Run once on the availability probe's unregistered ring. Invalid
  // buffer references and NOWAIT guarantee that no application data is moved.
  static bool probe_(Zi::IOBufRing &ring, const timespec &deadline)
  {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    auto &sq = ring.sq();
    msghdr msg{};
    constexpr unsigned ops[] = {
      IORING_OP_SEND_ZC, IORING_OP_SENDMSG_ZC,
      IORING_OP_RECV, IORING_OP_RECVMSG
    };
    bool supported = true;
    for (auto opcode : ops) {
      bool tx = opcode == IORING_OP_SEND_ZC || opcode == IORING_OP_SENDMSG_ZC;
      bool message = opcode == IORING_OP_SENDMSG_ZC || opcode == IORING_OP_RECVMSG;
      unsigned tail = sq.tail->load_();
      unsigned i = tail & sq.mask;
      auto &sqe = sq.sqes[i];
      sqe = {};
      sqe.opcode = opcode;
      sqe.fd = fd;
      sqe.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
      if (tx) {
        sqe.ioprio = IORING_RECVSEND_FIXED_BUF;
        sqe.addr = 1;
        sqe.len = 1;
        msg.msg_iovlen = 1; // null iovec: recognized operation must fault
      } else {
        sqe.ioprio = IORING_RECV_MULTISHOT;
        sqe.flags = IOSQE_BUFFER_SELECT;
        msg.msg_iovlen = 0;
        msg.msg_namelen = sizeof(sockaddr_storage);
        msg.msg_controllen = CMSG_SPACE(sizeof(int));
      }
      if (message) { sqe.addr = uintptr_t(&msg); sqe.len = 1; }
      sq.array[i] = i;
      *sq.tail = tail + 1;
      io_uring_cqe cqe;
      if (!complete_(ring, deadline, cqe) ||
	  (tx ? cqe.res != -EFAULT :
	    (cqe.res != -EAGAIN && cqe.res != -ENOBUFS)) ||
	  (cqe.flags & IORING_CQE_F_NOTIF)) {
	supported = false;
	break;
      }
      if (cqe.flags & IORING_CQE_F_MORE) {
	// Failed sends may still owe a notification. Consume that actual CQE
	// before reusing the one-entry probe ring; an Rx probe must terminate.
	if (!tx || !complete_(ring, deadline, cqe) ||
	    !(cqe.flags & IORING_CQE_F_NOTIF) || (cqe.flags & IORING_CQE_F_MORE)) {
	  supported = false;
	  break;
	}
      }
    }
    ::close(fd);
    return supported;
  }

  bool m_enabled = false;
};

static const bool ZiPlatform_uring =
  ZmSingleton<ZiPlatform_URing>::instance()->enabled();

bool Zi::uring() { return ZiPlatform_uring; }
#else
bool Zi::uring() { return false; }
#endif

#else

Zi::Username Zi::username(ZeError *e)
{
  Zi::Username name;
  name.size(Zi::NameMax + 1);
  DWORD len = Zi::NameMax;
  if (!GetUserName(name.data(), &len))
    name.null();
  else {
    name.calcLength();
    name.truncate();
  }
  return name;
}

Zi::Hostname Zi::hostname(ZeError *e)
{
  Zi::Hostname name;
  ZuCArray<Zi::NameMax + 1> buf;
  name.size(Zi::NameMax + 1);
  if (gethostname(buf.data(), Zi::NameMax))
    buf.null();
  else {
    buf.calcLength();
  }
  name = buf;
  return name;
}

#endif /* _WIN32 */
