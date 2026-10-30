//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Linux/glibc diagnostic interposer. No allocation, framework objects or logging
// in hooks: these functions can run before C++/loader initialization completes.
// Observes dynamically dispatched libc calls, not inlined/custom allocator work.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#ifndef __GLIBC__
#error heaptrace requires Linux glibc allocator entry points
#endif

extern "C" {
void *__libc_malloc(size_t) noexcept;
void *__libc_calloc(size_t, size_t) noexcept;
void *__libc_realloc(void *, size_t) noexcept;
void *__libc_memalign(size_t, size_t) noexcept;
void __libc_free(void *) noexcept;
}
using CopyFn = void *(*)(void *, const void *, size_t);
static CopyFn copyFn, moveFn;
static uint64_t allocCalls, requestedBytes, freeCalls, copyCalls, copyBytes;
static bool active;
static __thread bool nested;

static void allocation(void *p, size_t n) {
  if (!p || !__atomic_load_n(&active, __ATOMIC_RELAXED) || nested) return;
  __atomic_fetch_add(&allocCalls, 1, __ATOMIC_RELAXED);
  __atomic_fetch_add(&requestedBytes, n, __ATOMIC_RELAXED);
}
extern "C" void *malloc(size_t n) noexcept {
  void *p = __libc_malloc(n); allocation(p, n); return p;
}
extern "C" void *calloc(size_t n, size_t size) noexcept {
  void *p = __libc_calloc(n, size);
  size_t total;
  if (!__builtin_mul_overflow(n, size, &total)) allocation(p, total);
  return p;
}
extern "C" void *realloc(void *old, size_t n) noexcept {
  void *p = __libc_realloc(old, n); allocation(p, n); return p;
}
extern "C" void free(void *p) noexcept {
  if (p && __atomic_load_n(&active, __ATOMIC_RELAXED) && !nested)
    __atomic_fetch_add(&freeCalls, 1, __ATOMIC_RELAXED);
  __libc_free(p);
}
extern "C" void *aligned_alloc(size_t alignment, size_t n) noexcept {
  if (!alignment || (alignment & (alignment - 1))) {
    errno = EINVAL; return nullptr;
  }
  void *p = __libc_memalign(alignment, n); allocation(p, n); return p;
}
extern "C" int posix_memalign(void **out, size_t alignment, size_t n) noexcept {
  if (!alignment || alignment % sizeof(void *) || (alignment & (alignment - 1))) return EINVAL;
  int saved = errno;
  void *p = __libc_memalign(alignment, n);
  errno = saved;
  if (!p) return ENOMEM;
  *out = p; allocation(p, n); return 0;
}
static void *copy(void *out, const void *in, size_t n, CopyFn fn) {
  if (!nested && __atomic_load_n(&active, __ATOMIC_RELAXED)) {
    __atomic_fetch_add(&copyCalls, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&copyBytes, n, __ATOMIC_RELAXED);
  }
  if (fn) return fn(out, in, n);
  // Loader bootstrap fallback also handles overlap. Compare integer addresses:
  // relational comparison of unrelated C++ object pointers is unspecified.
  auto d = static_cast<unsigned char *>(out);
  auto s = static_cast<const unsigned char *>(in);
  if (uintptr_t(d) > uintptr_t(s) && uintptr_t(d) - uintptr_t(s) < n) {
    for (size_t i = n; i; --i) d[i - 1] = s[i - 1];
  } else {
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
  }
  return out;
}
extern "C" void *memcpy(void *out, const void *in, size_t n) noexcept {
  return copy(out, in, n, copyFn);
}
extern "C" void *memmove(void *out, const void *in, size_t n) noexcept {
  return copy(out, in, n, moveFn);
}
__attribute__((constructor)) static void init() {
  nested = true;
  copyFn = reinterpret_cast<CopyFn>(dlsym(RTLD_NEXT, "memcpy"));
  moveFn = reinterpret_cast<CopyFn>(dlsym(RTLD_NEXT, "memmove"));
  nested = false; __atomic_store_n(&active, true, __ATOMIC_RELAXED);
}
__attribute__((destructor)) static void finish() {
  nested = true; __atomic_store_n(&active, false, __ATOMIC_RELAXED);
  const char *path = getenv("ZFIX_HEAPTRACE_FILE");
  if (!path || !*path) return;
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return;
  static constexpr const char format[] =
    "{\"scope\":\"dynamic glibc calls after observer init through exit hook; "
    "excludes inlined copies and custom allocator internals\","
    "\"allocCalls\":%llu,\"requestedBytes\":%llu,\"freeCalls\":%llu,"
    "\"copyCalls\":%llu,\"copyBytes\":%llu}\n";
  // At most20 decimal digits per uint64, plus fixed literals. Stack storage
  // avoids calling the observed allocator during destruction.
  char buffer[sizeof(format) + 5 * 20];
  int n = snprintf(buffer, sizeof(buffer), format,
    static_cast<unsigned long long>(__atomic_load_n(&allocCalls, __ATOMIC_RELAXED)),
    static_cast<unsigned long long>(__atomic_load_n(&requestedBytes, __ATOMIC_RELAXED)),
    static_cast<unsigned long long>(__atomic_load_n(&freeCalls, __ATOMIC_RELAXED)),
    static_cast<unsigned long long>(__atomic_load_n(&copyCalls, __ATOMIC_RELAXED)),
    static_cast<unsigned long long>(__atomic_load_n(&copyBytes, __ATOMIC_RELAXED)));
  if (n > 0 && size_t(n) < sizeof(buffer)) {
    size_t done = 0;
    while (done < size_t(n)) {
      auto wrote = write(fd, buffer + done, size_t(n) - done);
      if (wrote < 0) { if (errno == EINTR) continue; break; }
      done += size_t(wrote);
      if (!wrote) break;
    }
  }
  close(fd);
}
