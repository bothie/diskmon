###############################################################################
#
# diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
#
###############################################################################

package        = diskmon
copyrightyears = 2007-2013
mainauthor     = Bodo Thiesen <bothie@gmx.de>

MAKEFILE = Makefile
TOPDIR   = ../..
THISDIR  = fs/ext2

include $(TOPDIR)/Makefile.conf

SRCS = \
	bitmap.c \
	com_cache.c \
	endian.c \
	ext2.c \
	fsck.c \
	isc.c \
	pc.c \
	threads.c \

SOTARGET = ext2.so
# SOFLAGS += -L../../bdev -lbdev
# -L.. -lfs

SOFLAGS += \
	-lz \

CPPFLAGS += -I.. -I../.. -I../../bdev
CPPFLAGS += -DHAVE_VALGRIND
CPPFLAGS += -D_GNU_SOURCE
# CPPFLAGS += -D_FORTIFY_SOURCE=0
CPPFLAGS += -DNEED_LONG_LONG
CPPFLAGS += -D__athlon__

CFLAGS += -fno-inline
CFLAGS += -fno-inline-small-functions
CFLAGS += -fno-inline-functions
CFLAGS += -fno-inline-functions-called-once
CFLAGS += -fno-early-inlining

# DONTOPTIMIZE=true

# CFLAGS += -fno-omit-framepointer
# CFLAGS += -fno-inline-functions

# PRINTFLAGS=true

CPPFLAGS += \
	-I$(TOPDIR)/btlib \
	-D__FUNCTION__=__func__ \

LDFLAGS += \
	-L$(TOPDIR)/btlib -lbt \
	-lrt \

include $(shell btmakefile --path)/Makefile.Include

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
