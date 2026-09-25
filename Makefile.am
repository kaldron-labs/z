ACLOCAL_AMFLAGS = -I m4
METASOURCES = AUTO
EXTRA_DIST = GUIDELINES.md naming.md z.config .lsan-suppressions \
	scripts/fbscodegen \
	scripts/qir-build-image scripts/qir-upload-image \
	scripts/qir-run-local scripts/qir-collect-results scripts/qir-report
SUBDIRS = zu zm zt ze zf zi zv zrl
if LINUX
SUBDIRS += zdbus
endif
SUBDIRS += ztcp ztls zquic zhttp zws
SUBDIRS += zrest
SUBDIRS += zmcp
SUBDIRS += zfb
SUBDIRS += zdb
if LIBPQ
SUBDIRS += zdb_pq
endif
if SQLITE
SUBDIRS += zdb_sqlite
endif
SUBDIRS += zum
SUBDIRS += ztc
SUBDIRS += zdf
if GTK
SUBDIRS += zgtk
endif
# SUBDIRS += zdash

DIST_SUBDIRS = zu zm zt ze zf zi zv zrl zdbus ztcp ztls zquic zhttp zws zrest zmcp zfb zdb ztc \
	zdb_pq zdb_sqlite zdf zum zgtk # zdash

.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: all
	@for subdir in $(SUBDIRS); do \
	  $(MAKE) $(AM_MAKEFLAGS) -C $$subdir test || exit $$?; \
	done

ZHTTP_QIR_INSTALL_DIRS = zu/src zm/src zt/src ze/src zf/src zi/src ztcp/src \
	ztls/src zquic/src zhttp/src

.PHONY: zhttp-qir-build
zhttp-qir-build:
	$(MAKE) $(AM_MAKEFLAGS) -C zu/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zm/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zt/src all
	$(MAKE) $(AM_MAKEFLAGS) -C ze/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zf/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zi/src all
	$(MAKE) $(AM_MAKEFLAGS) -C ztcp/src all
	$(MAKE) $(AM_MAKEFLAGS) -C ztls/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zquic/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zhttp/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zhttp/util all
	$(MAKE) $(AM_MAKEFLAGS) -C zhttp/interop zhttpqir

.PHONY: zhttp-qir-image
zhttp-qir-image: zhttp-qir-build
	test "$(prefix)" = "/usr/local"
	rm -rf "$(abs_builddir)/zhttp/interop/qir-root"
	@for subdir in $(ZHTTP_QIR_INSTALL_DIRS); do \
	  $(MAKE) $(AM_MAKEFLAGS) -C $$subdir install \
	    DESTDIR="$(abs_builddir)/zhttp/interop/qir-root" || exit $$?; \
	done
	$(MAKE) $(AM_MAKEFLAGS) -C zhttp/interop qir-stage \
	  ZHTTP_QIR_ROOT="$(abs_builddir)/zhttp/interop/qir-root"
	"$(abs_srcdir)/scripts/qir-build-image" \
	  --root "$(abs_builddir)/zhttp/interop/qir-root" \
	  --tag "$${ZHTTP_QIR_IMAGE:-zhttp-qir:local}" \
	  --no-configure --no-clean --allow-dirty
	rm -rf "$(abs_builddir)/zhttp/interop/qir-root"
