#!/bin/sh
libtool execute valgrind --db-attach=yes ./ZdbTest -f orders2 --hostID=2 --hosts:1:priority=75 --hosts:1:IP=127.0.0.1 --hosts:1:port=9944 --hosts:2:priority=50 --hosts:2:IP=127.0.0.1 --hosts:2:port=9945 --electionTimeout=5 $*
