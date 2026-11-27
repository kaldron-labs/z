METASOURCES = AUTO
SUBDIRS = src afp xdp
if DPDK
SUBDIRS += dpdk
endif
if MLX
SUBDIRS += mlx
endif
SUBDIRS += test util itest

.PHONY: test
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C test test
	$(MAKE) $(AM_MAKEFLAGS) -C itest test
