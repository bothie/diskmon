###############################################################################
#
# diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
#
###############################################################################

package        = diskmon
copyrightyears = 2007-2013
mainauthor     = Bodo Thiesen <bothie@gmx.de>

MAKEFILE = Makefile
TOPDIR   = ../../../..
THISDIR  = bdev/dmcrypt/iv/null

include $(TOPDIR)/Makefile.conf

SRCS = \
	null.c \

LIBTARGET = null
SOTARGET = null.so
SOFLAGS += -liv

CPPFLAGS += -I.. -I../../.. -I../../../.. -I../../../../crypt

LDFLAGS += -LBtLinuxLibrary -lBtLinuxLibrary -L..

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
