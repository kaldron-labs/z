METASOURCES = AUTO

.PHONY: test
test:
	$(SHELL) $(srcdir)/zftomlinteroptest.sh \
		$(top_builddir)/zf/test/ZfTOMLTest \
		$(srcdir)/zftomlinterop.py

dist_noinst_SCRIPTS = zftomlinteroptest.sh zftomlinterop.py
