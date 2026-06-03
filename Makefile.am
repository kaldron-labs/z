ACLOCAL_AMFLAGS = -I m4
METASOURCES = AUTO
SUBDIRS = zu zm zt ze zi zrl
SUBDIRS += ztls zquic zhttp
SUBDIRS += zfb
SUBDIRS += zv
SUBDIRS += zdb
if LIBPQ
SUBDIRS += zdb_pq
endif
#SUBDIRS += zum zcmd zproxy
SUBDIRS += zdf
if GTK
#SUBDIRS += zgtk
endif
#SUBDIRS += zrest

.PHONY: test
test:
	@for subdir in $(SUBDIRS); do \
	  $(MAKE) $(AM_MAKEFLAGS) -C $$subdir test || exit $$?; \
	done
