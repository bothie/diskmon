#ifndef FSCK_H
#define FSCK_H

#include "conf.h"

#ifdef THREADS

extern struct btlock_lock * ind_lock;

#endif // #ifdef THREADS

#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION

void try_clone();

#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION

#endif // #ifndef FSCK_H
