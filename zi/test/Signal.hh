//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <signal.h>
#include <stdlib.h>

extern "C" {
  extern void sigint(int);
#ifndef _WIN32
  extern void sigsegv(int, siginfo_t *, void *);
#endif
};

void sigint(int s)
{
  Global::post();
}

#ifndef _WIN32
void sigsegv(int s, siginfo_t *si, void *c)
{
  fprintf(stderr, "SIGSEGV @%p\n", si->si_addr); fflush(stderr);
  for (;;) sleep(1);
}
#endif

void trap()
{
#ifndef _WIN32
  {
    struct sigaction s;

    memset(&s, 0, sizeof(struct sigaction));
    s.sa_handler = sigint;
    sigemptyset(&s.sa_mask);
    sigaction(SIGINT, &s, 0);
    memset(&s, 0, sizeof(struct sigaction));
    s.sa_flags = SA_SIGINFO;
    s.sa_sigaction = sigsegv;
    sigemptyset(&s.sa_mask);
    sigaction(SIGSEGV, &s, 0);
  }
#else
  signal(SIGINT, sigint);
#endif
}
