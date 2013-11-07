/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "bitmap.h"

#include "sc.h"

void set_owner(struct scan_context * sc, u64 cluster, u32 inode, u64 cluster_pointing_here) {
	// size_t index=cluster%sc->comc.clusters_per_entry;
	// struct com_cache_entry * ccge=get_com_cache_entry(sc,cluster/sc->comc.clusters_per_entry);
	
	size_t index=cluster_to_cci(cluster);
	struct com_cache_entry * ccge=get_com_cache_entry(sc,cluster_to_ccg(cluster),cluster);
	
	ccge->entry[index]=inode;
	ccge->entry[index + sc->comc.clusters_per_entry] = cluster_pointing_here;
	ccge->dirty=true;
	release(sc,ccge);
}

void get_owner_both(struct scan_context * sc, u64 cluster, u32 * inode, u64 * cluster_pointing_here) {
	size_t index=cluster_to_cci(cluster);
	struct com_cache_entry * ccge=get_com_cache_entry(sc,cluster_to_ccg(cluster),cluster);
	
	*inode = ccge->entry[index];
	*cluster_pointing_here = ccge->entry[index + sc->comc.clusters_per_entry];
	release(sc,ccge);
}

bool set_owners(struct scan_context * sc, u64 * cp, size_t * num, u32 inode, u64 cluster_pointing_here) {
	size_t index=cluster_to_cci(*cp);
	struct com_cache_entry * ccge=get_com_cache_entry(sc,cluster_to_ccg(*cp),*cp);
	
	ccge->dirty=true;
	while (likely(index<sc->comc.clusters_per_entry)
	&&     likely(*num)) {
		if (ccge->entry[index]) {
			release(sc,ccge);
			return true;
		}
		ccge->entry[index]=inode;
		ccge->entry[index + sc->comc.clusters_per_entry] = cluster_pointing_here;
		++index;
		++*cp;
		--*num;
	}
	release(sc,ccge);
	
	return false;
}
