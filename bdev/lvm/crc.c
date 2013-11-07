/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "crc.h"

#define INITIAL_CRC 0xf597a6cf

/* Calculate an endian-independent CRC of supplied buffer */
uint32_t calc_crc_incremental(uint32_t initial, const void * buf, uint32_t size) {
	static const uint32_t crctab[] = {
		0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
		0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
		0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
	};
	uint32_t crc=initial;
	const uint8_t * data=(const uint8_t *)buf;
	
	for (uint32_t i=0;i<size;i++) {
		crc^=*data++;
		crc =(crc>>4)^crctab[crc&0xf];
		crc =(crc>>4)^crctab[crc&0xf];
	}
	return crc;
}

uint32_t calc_crc(const void * buf, uint32_t size) {
	return calc_crc_incremental(INITIAL_CRC, buf, size);
}
