#!/bin/sh
sed -i.bak -e s/\\r\\n/\\n/ configure.ac m4/*.m4
