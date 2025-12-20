#include <zlib/ZmRing.hh>

#ifdef linux
#include <sys/syscall.h>
#include <sys/mman.h>
#include <linux/futex.h>
#endif

namespace ZmRing_ {

#ifdef linux

Blocker::Blocker() { }
Blocker::Blocker(const Blocker &) { }
Blocker::~Blocker() { }

bool Blocker::open(bool, const Params &) { return true; }
void Blocker::close() { }

#if 0
#pragma pack(push, 1)
struct Log {
  char op[8];
  ZuTime stamp;
  int errno_;
  void *blocker;
  uint32_t *addr;
  uint32_t val;
};
#pragma pack(pop)

static Log logBuf[1024] = { { { 0 } } };

static ZmAtomic<unsigned> logIndex;

void logDump(unsigned n) {
  unsigned index = logIndex + 1024;
  for (unsigned i = 0; i < n; i++) {
    const auto &log = logBuf[(index - n + i) & 1023];
    std::cout << log.stamp.tv_sec << '.' << log.stamp.tv_nsec << ' ' <<
      ZuBoxPtr(log.blocker).hex() << ' ' <<
      log.op << ' ' << ZuBoxPtr(log.addr).hex() << ' ' <<
      ZuBoxed(log.val).hex() << ' ' <<
      (!log.errno_ ? "" :
       log.errno_ == ETIMEDOUT ? "ETIMEDOUT" :
       log.errno_ == EAGAIN ? "EAGAIN" :
       log.errno_ == ENOSYS ? "ENOSYS" :
       log.errno_ == EINVAL ? "EINVAL" :
       "UNKNOWN") << ' ' << log.errno_ << '\n';
  }
}
#endif

int Blocker::wait(
    ZmAtomic<uint32_t> &addr, uint32_t val,
    const Params &params)
{
  /* new (&logBuf[(logIndex++) & 1023])
    Log{"wait", Zm::now(), 0, this,
      reinterpret_cast<uint32_t *>(&addr), val}; */
  if (ZuUnlikely(params.timeout)) {
    ZuTime out = Zm::now(params.timeout);
    unsigned i = 0;
    do {
      if (ZuUnlikely(i >= params.spin)) {
	if (syscall(SYS_futex, reinterpret_cast<volatile int *>(&addr),
	      FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
	      static_cast<int>(val), &out, 0, 0) < 0) {
	  auto errno_ = errno;
	  /* new (&logBuf[(logIndex++) & 1023])
	    Log{"woke", Zm::now(), errno_, this,
	      reinterpret_cast<uint32_t *>(&addr), addr}; */
	  if (errno_ == ETIMEDOUT) return Zu::NotReady;
	  if (errno_ == EAGAIN) return Zu::OK;
	}
	i = 0;
      } else
	++i;
    } while (addr == val);
  } else {
    unsigned i = 0;
    do {
      if (ZuUnlikely(i >= params.spin)) {
	syscall(SYS_futex, reinterpret_cast<volatile int *>(&addr),
	    FUTEX_WAIT | FUTEX_PRIVATE_FLAG, static_cast<int>(val), 0, 0, 0);
	/* new (&logBuf[(logIndex++) & 1023])
	  Log{"woke", Zm::now(), errno, this,
	    reinterpret_cast<uint32_t *>(&addr), addr}; */
	i = 0;
      } else
	++i;
    } while (addr == val);
  }
  return Zu::OK;
}

void Blocker::wake(ZmAtomic<uint32_t> &addr)
{
  /* new (&logBuf[(logIndex++) & 1023])
    Log{"wake", Zm::now(), 0, this,
      reinterpret_cast<uint32_t *>(&addr), addr.load_()}; */
  syscall(SYS_futex, reinterpret_cast<volatile int *>(&addr),
      FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT_MAX, 0, 0, 0);
}

#endif /* linux */

#ifdef _WIN32

Blocker::Blocker() { }

Blocker::Blocker(const Blocker &blocker) {
  DuplicateHandle(
    GetCurrentProcess(),
    blocker.m_sem,
    GetCurrentProcess(),
    &m_sem,
    0,
    FALSE,
    DUPLICATE_SAME_ACCESS);
}

Blocker::~Blocker() { close(); }

bool Blocker::open(bool, const Params &)
{
  if (m_sem) return true;
  m_sem = CreateSemaphore(0, 0, 0x7fffffff, 0);
  if (m_sem == INVALID_HANDLE_VALUE) m_sem = 0;
  return m_sem;
}

void Blocker::close()
{
  if (m_sem) { CloseHandle(m_sem); m_sem = 0; }
}

int Blocker::wait(
    ZmAtomic<uint32_t> &addr, uint32_t val,
    const Params &params)
{
  DWORD timeout = params.timeout ? params.timeout * 1000 : INFINITE;
  unsigned i = 0;
  while (i < params.spin) {
    if (addr != val) return Zu::OK;
    ++i;
  }
  while (addr == val) {
    if (ZuUnlikely(!m_sem)) return Zu::IOError;
    DWORD r = WaitForSingleObject(m_sem, timeout);
    switch (int(r)) {
      case WAIT_OBJECT_0:	return Zu::OK;
      case WAIT_TIMEOUT:	return Zu::NotReady;
    }
    return Zu::IOError;
  }
  return Zu::OK;
}

void Blocker::wake(ZmAtomic<uint32_t> &addr)
{
  if (ZuUnlikely(!m_sem)) return;
  LONG prev = 0;
  while (ReleaseSemaphore(m_sem, 1, &prev) && prev > 1);
}

#endif /* _WIN32 */

bool CtrlMem::open(unsigned size, const Params &params)
{
  if (m_addr) return true;
  if (!params.cpuset)
    m_addr = hwloc_alloc(ZmTopology::hwloc(), size);
  else
    m_addr = hwloc_alloc_membind(
	ZmTopology::hwloc(), size,
	params.cpuset, HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_MIGRATE);
  if (!m_addr) return false;
  memset(m_addr, 0, size);
  m_size = size;
  return true;
}

void CtrlMem::close()
{
  if (!m_addr) return;
  if (!m_shadow) hwloc_free(ZmTopology::hwloc(), m_addr, m_size);
  m_addr = nullptr;
  m_size = 0;
  m_shadow = false;
}

bool DataMem::open(unsigned size, const Params &params)
{
  if (m_addr) return true;
  if (!params.cpuset)
    m_addr = hwloc_alloc(ZmTopology::hwloc(), size);
  else
    m_addr = hwloc_alloc_membind(
	ZmTopology::hwloc(), size,
	params.cpuset, HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_MIGRATE);
  if (!m_addr) return false;
  *reinterpret_cast<uint64_t *>(m_addr) = 0;
  m_size = size;
  return true;
}

void DataMem::close()
{
  if (!m_addr) return;
  if (!m_shadow) hwloc_free(ZmTopology::hwloc(), m_addr, m_size);
  m_addr = nullptr;
  m_size = 0;
  m_shadow = false;
}

unsigned MirrorMem::alignSize(unsigned size)
{
#ifdef linux
  unsigned blkSize = ::sysconf(_SC_PAGESIZE);
#endif
#ifdef _WIN32
  unsigned blkSize = 64<<10; // Windows - 64k, not the system page size
#endif
  return ((size + blkSize - 1) / blkSize) * blkSize;
}

bool MirrorMem::open(unsigned size, const Params &params)
{
  if (m_addr) return true;

#ifdef linux
  m_handle = memfd_create("ZmRing", MFD_CLOEXEC);
  if (m_handle < 0) return false;
  if (ftruncate(m_handle, size) < 0) {
    ::close(m_handle);
    m_handle = nullHandle();
    return false;
  }
  m_addr = ::mmap(
      0, size<<1, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (!m_addr) {
    ::close(m_handle);
    m_handle = nullHandle();
    return false;
  }
  if (m_addr == MAP_FAILED) {
    ::close(m_handle);
    m_handle = nullHandle();
    m_addr = nullptr;
    return false;
  }
  void *addr = ::mmap(
      m_addr, size,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, m_handle, 0);
  if (addr != m_addr) {
    munmap(m_addr, size<<1);
    ::close(m_handle);
    m_handle = nullHandle();
    m_addr = nullptr;
    return false;
  }
  addr = ::mmap(
      static_cast<uint8_t *>(m_addr) + size, size,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, m_handle, 0);
  if (addr != static_cast<void *>(
	static_cast<uint8_t *>(m_addr) + size)) {
    munmap(m_addr, size<<1);
    ::close(m_handle);
    m_handle = nullHandle();
    m_addr = nullptr;
    return false;
  }
#endif /* linux */

#ifdef _WIN32
  m_handle = CreateFileMapping(
      INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, size, nullptr);
  if (nullHandle(m_handle)) {
    m_handle = nullHandle();
    return false;
  }
retry:
  m_addr = VirtualAlloc(
      0, static_cast<DWORD>(size<<1), MEM_RESERVE, PAGE_NOACCESS);
  if (!m_addr) {
    CloseHandle(m_handle);
    m_handle = nullHandle();
    return false;
  }
  if (!VirtualFree(m_addr, 0, MEM_RELEASE)) {
    CloseHandle(m_handle);
    m_handle = nullHandle();
    m_addr = nullptr;
    return false;
  }
  void *addr = MapViewOfFileEx(
      m_handle, FILE_MAP_WRITE, 0, 0,
      static_cast<DWORD>(size), m_addr);
  if (!addr) goto retry;
  if (addr != m_addr) { UnmapViewOfFile(addr); goto retry; }
  addr = MapViewOfFileEx(
      m_handle, FILE_MAP_WRITE, 0, 0,
      static_cast<DWORD>(size),
      static_cast<uint8_t *>(m_addr) + size);
  if (!addr) goto retry;
  if (addr != static_cast<void *>(
	static_cast<uint8_t *>(m_addr) + size)) {
    UnmapViewOfFile(m_addr);
    UnmapViewOfFile(addr);
    goto retry;
  }
#endif /* _WIN32 */

  if (!!params.cpuset) {
    hwloc_set_area_membind(
	ZmTopology::hwloc(), m_addr, size,
	params.cpuset, HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_MIGRATE);
    hwloc_set_area_membind(
	ZmTopology::hwloc(), static_cast<uint8_t *>(m_addr) + size, size,
	params.cpuset, HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_MIGRATE);
  }

  *reinterpret_cast<uint64_t *>(m_addr) = 0;
  m_size = size;
  return true;
}

void MirrorMem::close()
{
  if (!m_addr) return;
  if (m_handle != nullHandle()) {
#ifndef _WIN32
    munmap(m_addr, m_size);
    munmap(static_cast<uint8_t *>(m_addr) + m_size, m_size);
    ::close(m_handle);
#else
    UnmapViewOfFile(m_addr);
    UnmapViewOfFile(static_cast<uint8_t *>(m_addr) + m_size);
    CloseHandle(m_handle);
#endif
    m_handle = nullHandle();
  }
  m_addr = nullptr;
  m_size = 0;
}

} // ZmRing_
