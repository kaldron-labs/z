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

static int lfd = 0;

void dolisten()
{
  rump_pub_lwproc_switch(lwp1);

  lfd = rump_sys_socket(RUMP_AF_INET, RUMP_SOCK_STREAM, RUMP_IPPROTO_TCP);
  if (lfd < 0) { error("socket", errno); }

  {
    int b = 1;
    if (rump_sys_setsockopt(
	  lfd, RUMP_IPPROTO_TCP, RUMP_TCP_NODELAY,
	  (const char *)&b, sizeof(int)) < 0)
      error("setsockopt(TCP_NODELAY)", errno);
  }

  {
    sockaddr_in local;
    local.sin_family = RUMP_AF_INET;
    local.sin_port = htons(8080);
    local.sin_addr.s_addr = inet_addr("192.168.10.1");
    if (rump_sys_bind(lfd, (const sockaddr *)&local, sizeof(sockaddr_in)) < 0) 
      error("bind", errno);
  }

  if (rump_sys_listen(lfd, 1) < 0) error("listen", errno);

  rump_pub_lwproc_switch(0);
}

void doecho(int fd)
{
  char buf[BUFSIZ];
  int n;

retry:
  rump_pub_lwproc_switch(lwp2);
  n = (int)rump_sys_recvfrom(fd, buf, BUFSIZ, 0, 0, 0);
  rump_pub_lwproc_switch(0);
  
  if (n < 0) {
    int errNo = errno;
    if (errNo == RUMP_EAGAIN || errNo == RUMP_EINTR) goto retry;
    error("recv", errNo);
  }

  printf("doecho(%d): \"%.*s\"\n", fd, n, buf);

  if (!n) {
    rump_pub_lwproc_switch(lwp2);
    if (rump_sys_close(fd) < 0) error("close", errno);
    rump_pub_lwproc_switch(0);
    return;
  }

retry2:
  rump_pub_lwproc_switch(lwp2);
  n = (int)rump_sys_sendto(fd, buf, n, 0, 0, 0);
  rump_pub_lwproc_switch(0);

  if (n < 0) {
    int errNo = errno;
    if (errNo == RUMP_EAGAIN || errNo == RUMP_EINTR) goto retry2;
    error("send", errNo);
  }
}

void doaccept()
{
  int fd;
  rump_pub_lwproc_switch(lwp1);
  {
    sockaddr_in remote;
    socklen_t len = sizeof(sockaddr_in);
    fd = rump_sys_accept(lfd, (sockaddr *)&remote, &len);
    if (fd < 0) error("accept", errno);
  }
  {
    int b = 1;
    if (rump_sys_setsockopt(
	  fd, RUMP_IPPROTO_TCP, RUMP_TCP_NODELAY,
	  (const char *)&b, sizeof(int)) < 0)
      error("setsockopt(TCP_NODELAY)", errno);
  }
  rump_pub_lwproc_switch(0);

  printf("accept(%d): %d\n", lfd, fd);

  doecho(fd);

  rump_pub_lwproc_switch(lwp2);
  if (rump_sys_close(fd) < 0) error("close", errno);
  rump_pub_lwproc_switch(0);
}

void dopoll()
{
  pollfd fds[1];
  fds[0].fd = lfd;
  fds[0].events = POLLIN;
  fds[0].revents = 0;

retry:
  rump_pub_lwproc_switch(lwp2);
  int r = rump_sys_poll(fds, 1, -1);
  rump_pub_lwproc_switch(0);

  if (r < 0) {
    int errNo = errno;
    if (errNo == RUMP_EAGAIN || errNo == RUMP_EINTR) goto retry;
    error("poll", errNo);
  }

  if (r == 0) goto retry;

  printf("poll(%d): %c%c%c%c%c\n", lfd,
	    (char)((fds[0].revents & POLLIN) ? 'I' : '-'),
	    (char)((fds[0].revents & POLLOUT) ? 'O' : '-'),
	    (char)((fds[0].revents & POLLHUP) ? 'H' : '-'),
	    (char)((fds[0].revents & POLLERR) ? 'E' : '-'),
	    (char)((fds[0].revents & POLLNVAL) ? 'N' : '-'));

  if (!(fds[0].revents & POLLIN)) goto retry;
  
  doaccept();
}

int main(int argc, char **argv)
{
  // putenv((char *)"RUMP_NCPU=2");
  rump_init();

  {
    int errNo;
    if (errNo = rump_pub_netconfig_ifcreate("netmap0"))
      error("ifcreate", errNo);
    if (errNo = rump_pub_netconfig_ifsetlinkstr("netmap0", "vale0:0"))
      error("ifsetlinkstr", errNo);
    if (errNo = rump_pub_netconfig_ipv4_ifaddr(
	  "netmap0", "192.168.10.1", "255.255.255.0"))
      error("ipv4_ifaddr", errNo);
  }

  rump_pub_lwproc_rfork(RUMP_RFCFDG);

  lwp1 = rump_pub_lwproc_curlwp();
  rump_pub_lwproc_newlwp(rump_sys_getpid());
  lwp2 = rump_pub_lwproc_curlwp();
  rump_pub_lwproc_switch(0);

  dolisten();

  dopoll();

  rump_pub_lwproc_switch(lwp2);
  rump_pub_lwproc_releaselwp();
  rump_pub_lwproc_switch(lwp1);
  rump_pub_lwproc_releaselwp();

  rump_sys_reboot(0, 0);

  return 0;
}
