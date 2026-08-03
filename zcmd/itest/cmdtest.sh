#!/bin/sh
#./cmdtest "$@" cmdtest.crt cmdtest.key 127.0.0.1 9000
libtool exec gdb --args ./cmdtest "$@" cmdtest.crt cmdtest.key 127.0.0.1 9000
