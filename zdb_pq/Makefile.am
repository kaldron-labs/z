METASOURCES = AUTO
SUBDIRS = src test itest ext
DIST_SUBDIRS = src test itest

EXTRA_DIST = ext/Makefile ext/libz--0.sql ext/libz.control ext/libz.so \
	ext/magic.bc ext/magic.c ext/zbitmap.bc ext/zbitmap.c ext/zbitmap.sql \
	ext/zdecimal.bc ext/zdecimal.c ext/zdecimal.sql ext/ztime.bc ext/ztime.c \
	ext/ztime.sql ext/test/expected/drop.out ext/test/expected/init.out \
	ext/test/expected/zbitmap.out ext/test/expected/zdecimal.out \
	ext/test/expected/ztime.out ext/test/sql/drop.sql ext/test/sql/init.sql \
	ext/test/sql/zbitmap.sql ext/test/sql/zdecimal.sql ext/test/sql/ztime.sql

.PHONY: test
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C test test
	$(MAKE) $(AM_MAKEFLAGS) -C itest test
