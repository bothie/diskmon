#include "pc.h"

struct problem_context * add_problem_context(struct scan_context * sc
	, unsigned long inode
	, unsigned long pm_bit
) {
	struct problem_context * pc = sc->inode_problem_context[inode - 1];
	if (!pc) {
		pc = sc->inode_problem_context[inode - 1] = malloc(sizeof(*sc->inode_problem_context[inode - 1]));
		if (!pc) {
			eprintf("new_problem_context: ENOMEN\n");
			exit(2);
		}
		VAINIT(pc->file_dir_eio_wdn);
		VAINIT(pc->ind_eio_wdn);
		VAINIT(pc->invalid_dot_entry_target);
		VAINIT(pc->bad_double_entry_dir);
		VAINIT(pc->missing_entry);
		pc->problem_mask = 0;
		pc->inode = NULL;
		VAINIT(pc->parents);
	}
	pc->problem_mask |= pm_bit;
	return pc;
}

void pc_free(struct problem_context * pc) {
	VAFREE(pc->file_dir_eio_wdn);
	VAFREE(pc->ind_eio_wdn);
	VAFREE(pc->invalid_dot_entry_target);
	VAFREE(pc->bad_double_entry_dir);
	VAFREE(pc->missing_entry);
	free(pc->inode);
	VAFREE(pc->parents); // The pointers pointed to by this are not owned by this struct
	free(pc);
}
