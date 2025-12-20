# https://www.postgresql.org/docs/current/extend-pgxs.html

PG_CONFIG = pg_config

extension_version = 0

EXTENSION = libz
MODULE_big = libz
OBJS = zdecimal.o ztime.o zbitmap.o magic.o
PG_CPPFLAGS = -I../../zu/src
SHLIB_LINK = -L../../zu/src/.libs -lZu
DATA_built = libz--$(extension_version).sql

REGRESS = init zdecimal ztime zbitmap drop
REGRESS_OPTS = --inputdir=test

#EXTRA_CLEAN += 

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

libz--$(extension_version).sql: zdecimal.sql ztime.sql zbitmap.sql
	cat $^ >$@
