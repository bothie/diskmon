#ifndef BTENDIAN_H
#define BTENDIAN_H

#include <byteswap.h>
#include <endian.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define host2le32(x) (x)
#define le2host32(x) (x)
#define host2le16(x) (x)
#define le2host16(x) (x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define host2le32(x) bswap_32(x)
#define le2host32(x) bswap_32(x)
#define host2le16(x) bswap_16(x)
#define le2host16(x) bswap_16(x)
#else
#error PDP? You'r kidding, aren't you?
#endif

#endif // #ifndef BTENDIAN_H
