METASOURCES = AUTO

.PHONY: test
test:
	$(SHELL) $(srcdir)/zvtomlinteroptest.sh \
		$(top_builddir)/zv/test/ZvTOMLTest \
		$(srcdir)/zvtomlinterop.py

dist_noinst_SCRIPTS = zvtomlinteroptest.sh zvtomlinterop.py
