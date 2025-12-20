//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ring buffer - intra-process mirrored memory region

#include <zlib/ZmRingMirror.hh>

bool ZmRingMirror::open(unsigned size)
{
  if (m_handle != nullHandle()) return false;

#ifdef linux
  m_handle = memfd_create("ZmRing", MFD_CLOEXEC);
  if (ftruncate(m_handle, size) < 0) { ::close(m_handle); return false; }
  m_addr = ::mmap(
      0, size<<1, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (m_addr == MAP_FAILED || !m_addr) return false;
  void *addr = ::mmap(
      m_addr, size,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, m_handle, 0);
  if (addr != m_addr) return false;
  addr = ::mmap(
      static_cast<uint8_t *>(m_addr) + size, size,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, m_handle, 0);
  if (addr != static_cast<void *>(
	static_cast<uint8_t *>(m_addr) + size)) return false;
#endif /* linux */

#ifdef _WIN32
  handle = 
  CreateFileMapping(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, size, nullptr);
    if (!handle || handle == INVALID_HANDLE_VALUE) return false;
retry:
  m_addr = VirtualAlloc(
      0, static_cast<DWORD>(size<<1), MEM_RESERVE, PAGE_NOACCESS);
  if (!m_addr) return false;
  if (!VirtualFree(m_addr, 0, MEM_RELEASE)) return false;
  void *addr = MapViewOfFileEx(
      handle, static_cast<DWORD>(accessFlags), 0, 0,
      static_cast<DWORD>(size), m_addr);
  if (!addr) goto retry;
  if (addr != m_addr) { UnmapViewOfFile(addr); goto retry; }
  addr = MapViewOfFileEx(
      handle, static_cast<DWORD>(accessFlags), 0, 0,
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

  m_size = size;
  return true;
}

void ZmRingMirror::close()
{
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
  m_addr = nullptr;
  m_size = 0;
}
