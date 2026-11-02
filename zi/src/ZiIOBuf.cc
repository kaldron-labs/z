//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// I/O buffers

#include <zlib/ZiIOBuf.hh>

namespace Zi {
  ZtEnumImplNS(IOBufType);
}

#ifndef _WIN32
#include <sys/syscall.h>
#include <sys/eventfd.h>

Zi::IOBufRing::~IOBufRing() { final(); }

ZmRef<Zi::IOBuf> Zi::IOBufRing::alloc(void *, uint32_t) { return {}; }

bool Zi::IOBufRing::init(unsigned ringSize)
{
  io_uring_params params{};
  size_t sqSize, cqSize;
  uint8_t *map;
  params.flags = IORING_SETUP_CQSIZE;
  params.cq_entries = ringSize<<1;
  m_fd = syscall(__NR_io_uring_setup, ringSize, &params);
  if (m_fd < 0) return false;
  // The required network operations postdate SINGLE_MMAP support.
  if ((params.features & (IORING_FEAT_SINGLE_MMAP | IORING_FEAT_NODROP)) !=
      (IORING_FEAT_SINGLE_MMAP | IORING_FEAT_NODROP)) goto fail;
  sqSize = params.sq_off.array + params.sq_entries * sizeof(unsigned);
  cqSize = params.cq_off.cqes + params.cq_entries * sizeof(io_uring_cqe);
  m_map = mmap(nullptr, sqSize > cqSize ? sqSize : cqSize,
    PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, IORING_OFF_SQ_RING);
  if (m_map == MAP_FAILED) { m_map = nullptr; goto fail; }
  map = static_cast<uint8_t *>(m_map);
  m_sq = {
    reinterpret_cast<ZmAtomic<unsigned> *>(map + params.sq_off.head),
    reinterpret_cast<ZmAtomic<unsigned> *>(map + params.sq_off.tail),
    reinterpret_cast<unsigned *>(map + params.sq_off.array), nullptr,
    params.sq_entries - 1,
    reinterpret_cast<ZmAtomic<unsigned> *>(map + params.sq_off.flags)
  };
  m_cq = {
    reinterpret_cast<ZmAtomic<unsigned> *>(map + params.cq_off.head),
    reinterpret_cast<ZmAtomic<unsigned> *>(map + params.cq_off.tail),
    reinterpret_cast<io_uring_cqe *>(map + params.cq_off.cqes),
    params.cq_entries - 1
  };
  m_sq.sqes = static_cast<io_uring_sqe *>(mmap(nullptr,
    size_t(params.sq_entries) * sizeof(io_uring_sqe),
    PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, IORING_OFF_SQES));
  if (m_sq.sqes == MAP_FAILED) { m_sq.sqes = nullptr; goto fail; }
  return true;
fail:
  final();
  return false;
}

bool Zi::IOBufRing::init_(unsigned ringSize, unsigned bufSize)
{
  if (!init(ringSize)) return false;
  m_bufSize = bufSize;
  m_bufCount = ringSize;
  // Populate writable pages during initialization, before the first receive.
  m_bufs = static_cast<uint8_t *>(mmap(nullptr,
    size_t(bufSize) * ringSize, PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0));
  if (m_bufs == MAP_FAILED) { m_bufs = nullptr; final(); return false; }
  m_free.init(ZmRingParams{}.size((ringSize + 1) * Free::MsgSize)
    .spin(0).timeout(0));
  if (m_free.open(Free::Read | Free::Write) != Zu::OK) {
    final();
    return false;
  }
  for (unsigned i = 0; i < ringSize; ++i) {
    auto slot = static_cast<uint8_t **>(m_free.push());
    *slot = m_bufs + size_t(i) * bufSize;
    m_free.push2(slot);
  }
  return true;
}

bool Zi::IOBufRing::init(unsigned ringSize, unsigned bufSize)
{
  if (!init_(ringSize, bufSize)) return false;
  // The ring's payload mapping is one fixed-buffer registration, at index zero.
  iovec region{m_bufs, size_t(bufSize) * ringSize};
  if (syscall(__NR_io_uring_register, m_fd, IORING_REGISTER_BUFFERS,
      &region, 1) >= 0) return true;
  final();
  return false;
}

uint8_t *Zi::IOBufRing::allocSlot()
{
  if (m_fd < 0) return nullptr;
  auto slot = m_free.tryShift();
  if (!slot) return nullptr;
  auto data = *slot;
  m_free.shift2();
  return data;
}

void Zi::IOBufRing::free(uint8_t *data)
{
  auto slot = static_cast<uint8_t **>(m_free.tryPush());
  ZmAssert(slot);
  *slot = data;
  m_free.push2(slot);
  if (m_waiting.load_() && m_waiting.xch(0)) {
    uint64_t n = 1;
    while (write(m_returnFD, &n, sizeof(n)) < 0 && errno == EINTR);
  }
}

bool Zi::IOBufRing::initReturn()
{
  m_returnFD = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  return m_returnFD >= 0;
}

void Zi::IOBufRing::wait()
{
  m_waiting = 1;
  // Close the publication/arming race without a wake for every buffer return.
  if (available() && m_waiting.xch(0)) {
    uint64_t n = 1;
    while (write(m_returnFD, &n, sizeof(n)) < 0 && errno == EINTR);
  }
}

void Zi::IOBufRing::final()
{
  // The allocator owner releases all buffers and I/O before destroying its ring.
  if (m_io) {
    auto io = m_io;
    m_io = nullptr;
    m_destroyIO(io);
  }
  m_waiting = 0;
  if (m_returnFD >= 0) { close(m_returnFD); m_returnFD = -1; }
  if (m_fd >= 0) { close(m_fd); m_fd = -1; }
  if (m_sq.sqes) munmap(m_sq.sqes, size_t(m_sq.mask + 1) * sizeof(io_uring_sqe));
  if (m_map) {
    auto sqEnd = uintptr_t(m_sq.array + m_sq.mask + 1);
    auto cqEnd = uintptr_t(m_cq.cqes + m_cq.mask + 1);
    munmap(m_map, (sqEnd > cqEnd ? sqEnd : cqEnd) - uintptr_t(m_map));
    m_map = nullptr;
  }
  m_sq = {};
  m_cq = {};
  m_free.close();
  if (m_bufs) {
    munmap(m_bufs, size_t(m_bufSize) * m_bufCount);
    m_bufs = nullptr;
  }
  m_bufSize = m_bufCount = 0;
}

Zi::IOBufRxRing::~IOBufRxRing() { final(); }

bool Zi::IOBufRxRing::init(unsigned ringSize, unsigned bufSize)
{
  if (!init_(ringSize, bufSize)) return false;
  if (!initReturn()) { final(); return false; }
  // Provided-ring size is power-of-two, at most 32768 (Linux UAPI).
  m_ringSize = 1;
  while (m_ringSize < ringSize && m_ringSize < 32768) m_ringSize <<= 1;
  if (ringSize > m_ringSize) goto fail;
  m_bufRing = static_cast<io_uring_buf_ring *>(mmap(nullptr,
    size_t(m_ringSize) * sizeof(io_uring_buf),
    PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (m_bufRing == MAP_FAILED) { m_bufRing = nullptr; goto fail; }
  {
    io_uring_buf_reg reg{};
    reg.ring_addr = uintptr_t(m_bufRing);
    reg.ring_entries = m_ringSize;
    // Group zero belongs to this ring. Publishing slots is an I/O-owner task.
    if (syscall(__NR_io_uring_register, fd(), IORING_REGISTER_PBUF_RING,
	&reg, 1) >= 0) return true;
  }
fail:
  final();
  return false;
}

void Zi::IOBufRxRing::final()
{
  IOBufRing::final();
  if (m_bufRing) {
    munmap(m_bufRing, size_t(m_ringSize) * sizeof(io_uring_buf));
    m_bufRing = nullptr;
  }
  m_ringSize = 0;
}
#endif
