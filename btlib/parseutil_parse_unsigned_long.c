#include "parseutil.h"

#include "btmacros.h"

#include <assert.h>
#include <errno.h>

char parse_unsigned_long(const char * * p,unsigned long * target) {
	int base=0;
	const char * initial_p=*p;
	
	if (**p<'0' || **p>'9') {
		pwerrno=PW_INVAL_CHAR;
		return 0;
	}
	
	if (**p=='0') {
		if (*++(*p)=='x') {
			base=16;
			++(*p);
		} else {
			--(*p);
			base=8;
		}
	} else {
		base=10;
	}
	
	if (!is_digit(base,**p)) {
		*p=initial_p;
		pwerrno=PW_INVAL_CHAR;
		return 0;
	}
	
	*target=0;
	
	while (true) {
		if (!is_digit(base,**p)) {
			pwerrno=0;
			return **p;
		}
		unsigned long prevtarget=*target;
		if (**p>='0' && **p<='9') {
			*target=*target*base+*((*p)++)-'0';
		} else {
			switch (*((*p)++)) {
				case 'a': case 'A': *target=*target*base+10; break;
				case 'b': case 'B': *target=*target*base+11; break;
				case 'c': case 'C': *target=*target*base+12; break;
				case 'd': case 'D': *target=*target*base+13; break;
				case 'e': case 'E': *target=*target*base+14; break;
				case 'f': case 'F': *target=*target*base+15; break;
				default: assert(never_reached);
			}
		}
		
		if (prevtarget>*target) {
			*p=initial_p;
			pwerrno=PW_ERRNO;
			errno=ERANGE;
			return 0;
		}
	}
}
