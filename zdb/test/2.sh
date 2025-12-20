#!/bin/sh
./ZdbTest -f orders2 -h 2 --electionTimeout=5 $*
#libtool execute gdb --args ./ZdbTest -f orders2 -h 2 --electionTimeout=5 $*
#libtool execute valgrind --leak-check=full ./ZdbTest -f orders2 -h 2 --electionTimeout=5 $*
