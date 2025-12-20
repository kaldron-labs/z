#ifndef RUMP_HH
#define RUMP_HH

#include <stdint.h>
#include <errno.h>

#include <sys/types.h>

extern "C" {
#include <rump/rump.h>
#include <rump/rump_syscalls.h>
#include <rump/netconfig.h>
};

extern int rump_errno(int e);

#endif /* RUMP_HH */
