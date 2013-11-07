/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#ifndef LVM_CRC_H
#define LVM_CRC_H

#include <stdint.h>

uint32_t calc_crc_incremental(uint32_t initial, const void * buf, uint32_t size);
uint32_t calc_crc(const void * buf, uint32_t size);

#endif // #ifndef LVM_CRC_H
