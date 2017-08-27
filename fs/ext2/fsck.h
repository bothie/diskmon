#ifndef FSCK_H
#define FSCK_H

#include "conf.h"

#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION

extern struct btlock_lock * ind_lock;

void try_clone();

#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION

#endif // #ifndef FSCK_H
