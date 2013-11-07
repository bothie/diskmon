#include "isc.h"

#include <string.h>

struct inode_owning_map_entry * mk_iome_sl(unsigned long inode_num,const char * filename,size_t sl,bool virtual) {
	struct inode_owning_map_entry * retval=malloc(sizeof(*retval)+sl);
	assert(retval);
	retval->parent=inode_num;
	retval->virtual=virtual;
	memcpy(retval->name,filename,sl+1);
	return retval;
}

struct inode_owning_map_entry * mk_iome(unsigned long inode_num,const char * filename,bool virtual) {
	size_t sl=strlen(filename);
	return mk_iome_sl(inode_num,filename,sl,virtual);
}
