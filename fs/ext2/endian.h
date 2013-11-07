/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#ifndef ENDIAN_H
#define ENDIAN_H

#include <btendian.h>
#include <stdbool.h>
#include <stdlib.h>

struct super_block;
struct group_desciptor_in_memory;
struct group_desciptor_v1;
struct group_desciptor_v2;
struct inode;

#define le32(x) x=le2host32(x)
#define le16(x) x=le2host16(x)

void endian_swap_sb(struct super_block * sb);
void endian_swap_inode(struct inode * inode);
bool endian_swap_extent_block(char * eb, size_t size);

void gdt_disk2memory_v1(struct group_desciptor_in_memory *gdt, struct group_desciptor_v1 *gdtv1, unsigned num_groups);
void gdt_disk2memory_v2(struct group_desciptor_in_memory *gdt, struct group_desciptor_v2 *gdtv2, unsigned num_groups);

#endif // #ifndef ENDIAN_H
