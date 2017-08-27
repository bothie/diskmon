/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

/*
 * This is proven to be correct for inode allocation map, 
 * but MAY be incorrect for block allocation map.
 */

#include <bttypes.h>

#include "bdev.h"

struct scan_context;

#define bit2byte(o) (o>>3)
#define bit2mask(o) (1<<(o&7))

static inline void set_bit(u8 * bitmap,u32 o) {
	bitmap[bit2byte(o)]|=bit2mask(o);
}

static inline bool get_bit(u8 * bitmap,u32 o) {
	return bitmap[bit2byte(o)]&bit2mask(o);
}

#define get_calculated_inode_allocation_map_bit(sc,inode_num) get_bit(sc->calculated_inode_allocation_map,inode_num-1)
#define set_calculated_inode_allocation_map_bit(sc,inode_num) set_bit(sc->calculated_inode_allocation_map,inode_num-1)

#define get_calculated_cluster_allocation_map_bit(sc,cluster_num) get_bit(sc->calculated_cluster_allocation_map,cluster_num-sc->cluster_offset)
#define set_calculated_cluster_allocation_map_bit(sc,cluster_num) set_bit(sc->calculated_cluster_allocation_map,cluster_num-sc->cluster_offset)

#define get_cluster_allocation_map_bit(sc,cluster_num) get_bit(sc->calculated_cluster_allocation_map,cluster_num-sc->cluster_offset)

#define cluster_to_cci(cluster) (((cluster)-sc->cluster_offset)%sc->comc.clusters_per_entry)
#define cluster_to_ccg(cluster) (((cluster)-sc->cluster_offset)/sc->comc.clusters_per_entry)

void set_owner(struct scan_context * sc, u64 cluster, u32 inode, u64 cluster_pointing_here);
bool set_owners(struct scan_context * sc, u64 * cp, size_t * num, u32 inode, u64 cluster_pointing_here);
void get_owner_both(struct scan_context * sc, u64 cluster, u32 * inode, u64 * cluster_pointing_here);

#define MARK_CLUSTER_IN_USE_BY(sc, _cluster, _inode, _new_cluster) do { \
	u64 __cluster=(_cluster); \
	u32 __inode=(_inode); \
	u64 __new_cluster = (_new_cluster); \
	u64 old_cluster; \
	u32 old_inode; \
	get_owner_both(sc, __cluster, &old_inode, &old_cluster); \
	set_owner(sc, __cluster, __inode, __new_cluster); \
	if (old_inode) { \
		/* TODO: get_problem_context(old_owner)-> */ \
		NOTIFYF( \
			"%s: Cluster %llu used multiple times, by inode %lu (cluster %llu points there) and inode %lu (cluster %llu points there)." \
			,sc->name \
			,(unsigned long long)__cluster \
			,(unsigned long)old_inode \
			,(unsigned long long)old_cluster \
			,(unsigned long)__inode \
			,(unsigned long long)__new_cluster \
		); \
	} \
} while (0)

#define MARK_CLUSTERS_IN_USE_BY(sc, _c, _num, _inode, _new_cluster) do { \
	u64 c ## __LINE__ = (_c); \
	size_t num ## __LINE__ = (_num); \
	u32 inode ## __LINE__ = (_inode); \
	u64 new_cluster ## __LINE__ = (_new_cluster); \
	while (num ## __LINE__) { \
		if (set_owners(sc, &c ## __LINE__, &num ## __LINE__, inode ## __LINE__, new_cluster ## __LINE__)) { \
			MARK_CLUSTER_IN_USE_BY(sc, c ## __LINE__, inode ## __LINE__, new_cluster ## __LINE__); \
			++c ## __LINE__; \
			--num ## __LINE__; \
		} \
	} \
} while (0)
