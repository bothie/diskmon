#include "threads.h"

#ifdef THREADS

#include "common.h"
#include "ext2.h"
#include "sc.h"

#include <btmacros.h>

volatile unsigned live_child = 0;

unsigned hard_child_limit = INIT_HARD_CHILD_LIMIT;

THREAD_RETURN_TYPE ext2_read_tables(THREAD_ARGUMENT_TYPE arg) {
	struct scan_context * sc=(struct scan_context *)arg;
	
	eprintf("Executing read_tables via thread %i\n",gettid());
	
	OPEN_PROGRESS_BAR_LOOP(0,sc->table_reader_group,sc->num_groups)
	
		read_table_for_one_group(sc, false);
		
		TRE_WAKE();
		
		PRINT_PROGRESS_BAR(sc->table_reader_group);
	CLOSE_PROGRESS_BAR_LOOP()
	
	if (sc->background) {
		NOTIFYF("%s: Background reader is successfully quitting",sc->name);
		--live_child;
	}
	
	eprintf("Returning from ext2_read_tables thread (exiting)\n");
	
	THREAD_RETURN();
}

struct thread * thread_head=NULL;
struct thread * thread_tail=NULL;

#endif // #ifdef THREADS
