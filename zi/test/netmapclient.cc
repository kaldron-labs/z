//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/poll.h>

#include "rump.hh"

void error(const char *op, int errNo)
{
  errno = rump_errno(errNo);
  perror(op);
  Zm::exit(1);
}

static lwp *lwp1 = 0, *lwp2 = 0;	// rump LWPs

void dosend(int fd)
{
  char buf[BUFSIZ];
  int n;

retry:
  rump_pub_lwproc_switch(lwp2);
  n = (int)rump_sys_sendto(fd, "Hello World", 11, 0, 0, 0);
  rump_pub_lwproc_switch(0);

  if (n < 0) {
    int errNo = errno;
    if (errNo == RUMP_EAGAIN || errNo == RUMP_EINTR) goto retry;
    error("send", errNo);
  }

retry2:
  rump_pub_lwproc_switch(lwp2);
  n = (int)rump_sys_recvfrom(fd, buf, BUFSIZ, 0, 0, 0);
  rump_pub_lwproc_switch(0);
  
  if (n < 0) {
    int errNo = errno;
    if (errNo == RUMP_EAGAIN || errNo == RUMP_EINTR) goto retry2;
    error("recv", errNo);
  }

  printf("dosend(%d): \"%.*s\"\n", fd, n, buf);
}

void doconnect()
{
  rump_pub_lwproc_switch(lwp2);

  int fd = rump_sys_socket(RUMP_AF_INET, RUMP_SOCK_STREAM, RUMP_IPPROTO_TCP);
  if (fd < 0) error("socket", errno);
  {
    int b = 1;
    if (rump_sys_setsockopt(
	  fd, RUMP_IPPROTO_TCP, RUMP_TCP_NODELAY,
	  (const char *)&b, sizeof(int)) < 0)
      error("setsockopt(TCP_NODELAY)", errno);
  }
  {
    sockaddr_in remote;
    remote.sin_family = RUMP_AF_INET;
    remote.sin_port = htons(8080);
    remote.sin_addr.s_addr = inet_addr("192.168.10.1");
    if (rump_sys_connect(fd, (sockaddr *)&remote, sizeof(sockaddr_in)) < 0)
      error("connect", errno);
  }
  rump_pub_lwproc_switch(0);

  printf("connect(): %d\n", fd);

  dosend(fd);

  rump_pub_lwproc_switch(lwp2);
  if (rump_sys_close(fd) < 0) error("close", errno);
  rump_pub_lwproc_switch(0);
}

int main(int argc, char **argv)
{
  // putenv((char *)"RUMP_NCPU=2");
  rump_init();

  {
    int errNo;
    if (errNo = rump_pub_netconfig_ifcreate("netmap0"))
      error("ifcreate", errNo);
    if (errNo = rump_pub_netconfig_ifsetlinkstr("netmap0", "vale0:1"))
      error("ifsetlinkstr", errNo);
    if (errNo = rump_pub_netconfig_ipv4_ifaddr(
	  "netmap0", "192.168.10.2", "255.255.255.0"))
      error("ipv4_ifaddr", errNo);
  }

  rump_pub_lwproc_rfork(RUMP_RFCFDG);

  lwp1 = rump_pub_lwproc_curlwp();
  rump_pub_lwproc_newlwp(rump_sys_getpid());
  lwp2 = rump_pub_lwproc_curlwp();
  rump_pub_lwproc_switch(0);

  doconnect();

  rump_pub_lwproc_switch(lwp2);
  rump_pub_lwproc_releaselwp();
  rump_pub_lwproc_switch(lwp1);
  rump_pub_lwproc_releaselwp();

  rump_sys_reboot(0, 0);

  return 0;
}
