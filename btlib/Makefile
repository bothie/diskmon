###############################################################################
#
# diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
#
###############################################################################

package        = diskmon
copyrightyears = 2007-2013
mainauthor     = Bodo Thiesen <bothie@gmx.de>

MAKEFILE = Makefile
TOPDIR   = ..
THISDIR  = btlib

include $(TOPDIR)/Makefile.conf

ifdef THREADS
ifdef BTCLONE

SRCS-THREADS = \
	btthread_btclone.c \
	btthread_btclone_stack_grows_down.c \

else # ifdef BTCLONE

SRCS-THREADS = \
	btthread_pthread.c \

SOFLAGS += \
	-lpthread \

endif # ifdef BTCLONE, else

else # ifdef THREADS

SRCS-THREADS = \

endif # ifdef THREADS, else

SRCS = \
	$(SRCS-THREADS) \
	avl.c \
	avl_do_find.c \
	avl_do_insert.c \
	avl_dump_tree.c \
	avl_find.c \
	avl_free.c \
	avl_insert.c \
	avl_insert_rc.c \
	avl_mk_tree.c \
	avl_mk_tree_rc.c \
	avl_node_find_authority.c \
	avl_node_free.c \
	avl_node_free_recursive.c \
	avl_rc_find.c \
	avl_remove.c \
	avl_remove_rc.c \
	avl_size.c \
	bterror.c \
	btfileio_fdprintf.c \
	btfileio_read_entire_file.c \
	btfileio_vfdprintf.c \
	btlock.c \
	btmacros_null_pointer.c \
	btmath.c \
	btsf.c \
	btstr_concat.c \
	btstr_mmemcpy.c \
	btstr_mstrcpy.c \
	btstr_strstartcmp.c \
	bttime_mstrftime.c \
	mprintf.c \
	object.c \
	object_avl_node_memory_management.c \
	parseutil_parse_c_string.c \
	parseutil_parse_skip.c \
	parseutil_parse_skip_string.c \
	parseutil_parse_string.c \
	parseutil_parse_unsigned_long.c \
	parseutil_parse_unsigned_long_long.c \
	parseutil_pwe_invalid_char.c \
	parseutil_pwerrno.c \
	parseutil_pwerror.c \
	refcounter.c \
	stddbg.c \
	vasiar.c \

LIBTARGET = bt

CPPFLAGS += \
	-I. \

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
