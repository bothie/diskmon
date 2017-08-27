###############################################################################
#
# diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
#
###############################################################################

package        = diskmon
copyrightyears = 2007-2013
mainauthor     = Bodo Thiesen <bothie@gmx.de>

MAKEFILE = Makefile
TOPDIR   = .
THISDIR  = .

include $(TOPDIR)/Makefile.conf

BUILD_STATIC = false
# Static link command:
# g++ -static -pedantic -g -Wall -W -O3 -pg -pipe -L/usr/btmakefile/workarounds main.o -o checker -L btlib -L bdev -L bdev/raid6 -L bdev/lvm -L bdev/loop -L bdev/blindread -Wl,-whole-archive -l bdev -l raid6 -l lvm -l loop -l blindread -l bt -Wl,-no-whole-archive -lworkaround_big_files_bug -lBtLinuxLibrary -ldl

SRCS = \
	common.c \
	main.c \

SUBMAKES = \
	chkraid6 \

# bdev/dmcrypt needs crypt, so make crypt prior to bdev
DIRS = \
	btlib \
	crypt \
	bdev \
	lvm-conf-extractor \
	fs \

BINS = \
	checker \

include $(shell btmakefile --path)/Makefile.Include

.PHONY: so

so:
	@rm -fr so
	@mkdir so
	@cp -dp $$(find -name '*.so') so

btmake-all-post: so

checker: main.o

CPPFLAGS-main.o = -Ibdev

LDFLAGS-checker = -Lbdev -lbdev -ldl

LDFLAGS += -LBtLinuxLibrary -lBtLinuxLibrary -L$(TOPDIR)/btlib

/Makefile.Include:
	@echo "This program needs the project btmakefile installed on your"
	@echo "system. Please ask your system administrator to install it."
	@echo ""
	@echo "btmakefile can be obtained on"
	@echo ""
	@echo "    http://bothie.de.vu/downloads/index.en.html"
	@echo ""
	@echo "And via anoncvs:"
	@echo ""
	@echo "    :pserver:anonymous@bodo-thiesen-server.dyndns.org:/home/public/bodo/cvs"
	@echo "    checkout btmakefile"
	@echo ""
	@echo "Please don't do a 'checkout .' as there is together with many other things a "
	@echo "whole linux kernel - you don't want that - and I don't want it, too ;-)"
	
	false
