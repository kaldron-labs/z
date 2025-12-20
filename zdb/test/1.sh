#!/bin/sh
#libtool execute valgrind --vgdb=yes --vgdb-error=1 ./ZdbTest -f orders1 -h 1 --electionTimeout=5 $*
#libtool execute strace -tt -x -yy -e trace=network,desc,epoll_wait,epoll_ctl -f -o 1.strace ./ZdbTest -f orders1 -h 1 --electionTimeout=5 $*
#libtool execute gdb --args ./ZdbTest -f orders1 -h 1 --electionTimeout=5 $*
#libtool execute valgrind --leak-check=full ./ZdbTest -f orders1 -h 1 --electionTimeout=5 $*
./ZdbTest -f orders1 -h 1 --electionTimeout=5 $*
