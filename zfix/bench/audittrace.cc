//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Linux x64/glibc LD_AUDIT observer, deliberately unsanitized in its separate
// loader namespace. Never replaces an allocator or changes a symbol address.
// Hooks only compare names/read registers/update atomics; no allocation/logging.
// ABI: <link.h>, <bits/link.h>; https://man7.org/linux/man-pages/man7/rtld-audit.7.html
// Counts selected main-namespace PLT entries from la_preinit through our exit
// hook. Direct/local, inlined and custom allocator-internal calls are invisible.
// Requests are attempts, not successful allocations or live bytes. Copy bytes
// sum selected entry arguments, not unique physical bytes (nested calls can
// count twice). asanCopyCalls/Bytes identify the sanitizer-entry subset.

#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#if !defined(__linux__) || !defined(__x86_64__) || !defined(__GLIBC__) || defined(__ILP32__)
#error audittrace requires Linux x64 glibc
#endif

namespace Kind {

  enum { Other, Malloc, Calloc, Realloc, Aligned, Posix, Free, Copy, AsanCopy };
}

static uint64_t allocAttempts, requestedBytes, requestOverflows, freeCalls;
static uint64_t copyCalls, copyBytes, asanCopyCalls, asanCopyBytes;
static bool active;

static bool equal(const char *a, const char *b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}
static unsigned kind(const char *name) {
  if (equal(name, "malloc")) return Kind::Malloc;
  if (equal(name, "calloc")) return Kind::Calloc;
  if (equal(name, "realloc")) return Kind::Realloc;
  if (equal(name, "aligned_alloc")) return Kind::Aligned;
  if (equal(name, "posix_memalign")) return Kind::Posix;
  if (equal(name, "free")) return Kind::Free;
  if (equal(name, "memcpy") || equal(name, "memmove")) return Kind::Copy;
  if (equal(name, "__asan_memcpy") || equal(name, "__asan_memmove")) return Kind::AsanCopy;
  return Kind::Other;
}
extern "C" unsigned la_version(unsigned version) {
  return version >= LAV_CURRENT ? LAV_CURRENT : 0;
}
extern "C" unsigned la_objopen(link_map *, Lmid_t ns, uintptr_t *) {
  return ns == LM_ID_BASE ? LA_FLG_BINDTO | LA_FLG_BINDFROM : 0;
}
extern "C" void la_preinit(uintptr_t *) {
  __atomic_store_n(&active, true, __ATOMIC_RELAXED);
}
extern "C" uintptr_t la_symbind64(Elf64_Sym *sym, unsigned, uintptr_t *, uintptr_t *,
    unsigned *flags, const char *name) {
  *flags |= LA_SYMB_NOPLTEXIT;
  if (!kind(name)) *flags |= LA_SYMB_NOPLTENTER;
  return sym->st_value;
}
extern "C" Elf64_Addr la_x86_64_gnu_pltenter(Elf64_Sym *sym, unsigned,
    uintptr_t *, uintptr_t *, La_x86_64_regs *regs, unsigned *, const char *name, long *) {
  if (!__atomic_load_n(&active, __ATOMIC_RELAXED)) return sym->st_value;
  uint64_t bytes;
  switch (kind(name)) {
    case Kind::Malloc: bytes = regs->lr_rdi; break;
    case Kind::Calloc:
      if (__builtin_mul_overflow(regs->lr_rdi, regs->lr_rsi, &bytes)) {
	__atomic_fetch_add(&allocAttempts, 1, __ATOMIC_RELAXED);
	__atomic_fetch_add(&requestOverflows, 1, __ATOMIC_RELAXED);
	return sym->st_value;
      }
      break;
    case Kind::Realloc:
    case Kind::Aligned: bytes = regs->lr_rsi; break;
    case Kind::Posix: bytes = regs->lr_rdx; break;
    case Kind::Free:
      __atomic_fetch_add(&freeCalls, 1, __ATOMIC_RELAXED);
      return sym->st_value;
    case Kind::AsanCopy:
      __atomic_fetch_add(&asanCopyCalls, 1, __ATOMIC_RELAXED);
      __atomic_fetch_add(&asanCopyBytes, regs->lr_rdx, __ATOMIC_RELAXED);
      [[fallthrough]];
    case Kind::Copy:
      __atomic_fetch_add(&copyCalls, 1, __ATOMIC_RELAXED);
      __atomic_fetch_add(&copyBytes, regs->lr_rdx, __ATOMIC_RELAXED);
      return sym->st_value;
    default: return sym->st_value;
  }
  __atomic_fetch_add(&allocAttempts, 1, __ATOMIC_RELAXED);
  __atomic_fetch_add(&requestedBytes, bytes, __ATOMIC_RELAXED);
  return sym->st_value;
}
__attribute__((destructor)) static void finish() {
  __atomic_store_n(&active, false, __ATOMIC_RELAXED);
  const char *path = getenv("ZFIX_AUDITTRACE_FILE");
  if (!path || !*path) return;
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return;
  static constexpr const char format[] =
    "{\"scope\":\"LD_AUDIT main-namespace PLT entries from preinit through exit hook; "
    "excludes direct/inlined/custom allocator internal calls; attempts not success; "
    "copy sums can include nested calls\","
    "\"allocAttempts\":%llu,\"requestedBytes\":%llu,\"requestOverflows\":%llu,"
    "\"freeCalls\":%llu,\"copyCalls\":%llu,\"copyBytes\":%llu,"
    "\"asanCopyCalls\":%llu,\"asanCopyBytes\":%llu}\n";
  // Eight uint64 values need at most20 digits each; fixed JSON plus this bound
  // fits stack storage without observing our own formatter's allocator work.
  char buffer[sizeof(format) + 8 * 20];
#define Value(v) static_cast<unsigned long long>(__atomic_load_n(&v, __ATOMIC_RELAXED))
  int n = snprintf(buffer, sizeof(buffer), format, Value(allocAttempts), Value(requestedBytes),
    Value(requestOverflows), Value(freeCalls), Value(copyCalls), Value(copyBytes),
    Value(asanCopyCalls), Value(asanCopyBytes));
#undef Value
  if (n > 0 && size_t(n) < sizeof(buffer)) {
    size_t done = 0;
    while (done < size_t(n)) {
      auto wrote = write(fd, buffer + done, size_t(n) - done);
      if (wrote < 0) { if (errno == EINTR) continue; break; }
      if (!wrote) break;
      done += size_t(wrote);
    }
  }
  close(fd);
}
