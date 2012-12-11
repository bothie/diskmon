###############################################################################
#
# The bothie-utils are Copyright (C) 2001-2007 by Bodo Thiesen <bothie@gmx.de>
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
###############################################################################

#package        = diskmon
#copyrightyears = ????-2007
#mainauthor     = Bodo Thiesen <bothie@gmx.de>

MAKEFILE = Makefile
TOPDIR   = ../..
THISDIR  = fs/ext2

SRCS = \
	ext2.c \

SOTARGET = ext2.so
# SOFLAGS += -L../../bdev -lbdev
# -L.. -lfs

CPPFLAGS-ext2.o += -I.. -I../.. -I../../bdev
CPPFLAGS-ext2.s += -I.. -I../.. -I../../bdev
CPPFLAGS-ext2.o += -DHAVE_LINUX_CLONE
CPPFLAGS-ext2.s += -DHAVE_LINUX_CLONE

# DONTOPTIMIZE=true

# CFLAGS += -fno-omit-framepointer
# CFLAGS += -fno-inline-functions

# PRINTFLAGS=true

CPPFLAGS += -I$(TOPDIR)/btlib
LDFLAGS += -L$(TOPDIR)/btlib -lbt -LBtLinuxLibrary -lBtLinuxLibrary

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
