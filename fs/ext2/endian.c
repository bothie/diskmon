/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "endian.h"

#include "ext2.h"

void endian_swap_sb(struct super_block * sb) {
	le32(sb->num_inodes);
	le32(sb->num_clusters);
	le32(sb->num_reserved_clusters);
	le32(sb->num_free_clusers);
	le32(sb->num_free_inodes);
	le32(sb->first_data_cluster);
	le32(sb->log_cluster_size);
	le32(sb->log_frag_size);
	le32(sb->clusters_per_group);
	le32(sb->frags_per_group);
	le32(sb->inodes_per_group);
	le32(sb->mtime);
	le32(sb->wtime);
	le16(sb->mount_count);
	le16(sb->max_mount_count);
	le16(sb->magic);
	le16(sb->state);
	le16(sb->errors);
	le16(sb->minor_rev_level);
	le32(sb->lastcheck_time);
	le32(sb->checkinterval);
	le32(sb->creator_os);
	le32(sb->rev_level);
	le16(sb->reserved_access_uid);
	le16(sb->reserved_access_gid);
	le32(sb->first_inode);
	le16(sb->inode_size);
	le16(sb->my_group);
	le32(sb->rw_compat);
	le32(sb->in_compat);
	le32(sb->ro_compat);
	le32(sb->e2compr_algorithm_usage_bitmap);
	le16(sb->onlineresize2fs_reserved_gdt_blocks);
	le32(sb->journal_inode);
	le32(sb->journal_dev);
	le32(sb->last_orphanded_inode);
	for (int i=0;i<4;++i) {
		le32(sb->hash_seed[i]);
	}
	le32(sb->default_mount_opts);
	le32(sb->first_meta_bg);
}

void gdt_disk2memory_v1(struct group_desciptor_in_memory *gdt, struct group_desciptor_v1 *gdtv1, unsigned num_groups) {
	for (unsigned group = 0; group < num_groups; ++group) {
		gdt[group].cluster_allocation_map = le2host32(gdtv1[group].cluster_allocation_map);
		gdt[group].inode_allocation_map = le2host32(gdtv1[group].inode_allocation_map);
		gdt[group].inode_table = le2host32(gdtv1[group].inode_table);
		gdt[group].num_free_clusters = le2host16(gdtv1[group].num_free_clusters);
		gdt[group].num_free_inodes = le2host16(gdtv1[group].num_free_inodes);
		gdt[group].num_directories = le2host16(gdtv1[group].num_directories);
		gdt[group].flags = le2host16(gdtv1[group].flags);
		gdt[group].snapshot_exclude_bitmap = le2host32(gdtv1[group].snapshot_exclude_bitmap);
		gdt[group].cluster_allocation_map_csum = le2host16(gdtv1[group].cluster_allocation_map_csum);
		gdt[group].inode_allocation_map_csum = le2host16(gdtv1[group].inode_allocation_map_csum);
		gdt[group].num_virgin_inodes = le2host16(gdtv1[group].num_virgin_inodes);
		gdt[group].csum = le2host16(gdtv1[group].csum);
		gdt[group].reserved = 0; // valgrind
	}
}

void gdt_disk2memory_v2(struct group_desciptor_in_memory *gdt, struct group_desciptor_v2 *gdtv2, unsigned num_groups) {
	for (unsigned group = 0; group < num_groups; ++group) {
		gdt[group].cluster_allocation_map = le2host32(gdtv2[group].cluster_allocation_map_lo) | ((u64)le2host32(gdtv2[group].cluster_allocation_map_hi) << 32);
		gdt[group].inode_allocation_map = le2host32(gdtv2[group].inode_allocation_map_lo) | ((u64)le2host32(gdtv2[group].inode_allocation_map_hi) << 32);
		gdt[group].inode_table = le2host32(gdtv2[group].inode_table_lo) | ((u64)le2host32(gdtv2[group].inode_table_hi) << 32);
		gdt[group].num_free_clusters = le2host16(gdtv2[group].num_free_clusters_lo) | ((u32)le2host16(gdtv2[group].num_free_clusters_hi) << 16);
		gdt[group].num_free_inodes = le2host16(gdtv2[group].num_free_inodes_lo) | ((u32)le2host16(gdtv2[group].num_free_inodes_hi) << 16);
		gdt[group].num_directories = le2host16(gdtv2[group].num_directories_lo) | ((u32)le2host16(gdtv2[group].num_directories_hi) << 16);
		gdt[group].snapshot_exclude_bitmap = le2host32(gdtv2[group].snapshot_exclude_bitmap_lo) | ((u64)le2host32(gdtv2[group].snapshot_exclude_bitmap_hi) << 32);
		gdt[group].cluster_allocation_map_csum = le2host16(gdtv2[group].cluster_allocation_map_csum_lo) | ((u32)le2host16(gdtv2[group].cluster_allocation_map_csum_hi) << 16);
		gdt[group].inode_allocation_map_csum = le2host16(gdtv2[group].inode_allocation_map_csum_lo) | ((u32)le2host16(gdtv2[group].inode_allocation_map_csum_hi) << 16);
		gdt[group].num_virgin_inodes = le2host16(gdtv2[group].num_virgin_inodes_lo) | ((u32)le2host16(gdtv2[group].num_virgin_inodes_hi) << 16);
		gdt[group].flags = le2host16(gdtv2[group].flags);
		gdt[group].reserved = le2host32(gdtv2[group].reserved);
		gdt[group].csum = le2host16(gdtv2[group].csum);
	}
}

void endian_swap_inode(struct inode * inode) {
	le16(inode->mode);
	le16(inode->uid);
	le32(inode->size);
	le32(inode->atime);
	le32(inode->ctime);
	le32(inode->mtime);
	le32(inode->dtime);
	le16(inode->gid);
	le16(inode->links_count);
	le32(inode->num_blocks);
	le32(inode->flags);
	le32(inode->translator);
	if (!(inode->flags & INOF_EXTENTS)) {
		for (int i=0;i<NUM_CLUSTER_POINTERS;++i) {
			le32(inode->cluster[i]);
		}
	} else {
		endian_swap_extent_block((char*)inode->cluster, sizeof(inode->cluster));
	}
	le32(inode->generation);
	le32(inode->file_acl);
	le32(inode->dir_acl);
	le32(inode->faddr);
	le16(inode->mode_high);
	le16(inode->uid_high);
	le16(inode->gid_high);
	le32(inode->author);
}

void endian_swap_extent_header(struct extent_header * eh) {
	le16(eh->magic);
	le16(eh->num_entries);
	le16(eh->max_entries);
	le16(eh->depth);
	le32(eh->generation);
}

void endian_swap_extent_descriptor(struct extent_descriptor * ed) {
	le32(ed->file_cluster);
	le16(ed->len);
	le16(ed->disk_cluster_hi);
	le32(ed->disk_cluster_lo);
}

void endian_swap_extent_index(struct extent_index * ei) {
	le32(ei->file_cluster);
	le32(ei->leaf_lo);
	le16(ei->leaf_hi);
	le16(ei->unused);
}

void endian_swap_extent_tail(struct extent_tail * et) {
	le32(et->checksum);
}

bool endian_swap_extent_block(char * eb, size_t size) {
	struct extent_header * eh = (struct extent_header *)eb;
	
	eb += 12;
	
	endian_swap_extent_header(eh);
	
	/*
	if (eh->magic != EXTENT_HEADER_MAGIC) {
		// endian_swap_extent_header(eh); // swap back
		return false;
	}
	*/
	
	u16 num = eh->num_entries;
	if (num > size / 12 - 1) {
		// endian_swap_extent_header(eh); // swap back
		/*
		return false;
		*/
		num = size / 12 - 1;
	}
	
	if (eh->depth) {
		while (num--) {
			struct extent_index * ei = (struct extent_index *)eb;
			eb += 12;
			endian_swap_extent_index(ei);
		}
	} else {
		while (num--) {
			struct extent_descriptor * ed = (struct extent_descriptor *)eb;
			eb += 12;
			endian_swap_extent_descriptor(ed);
		}
	}
	
	if (size != 60) {
		struct extent_tail * et = (struct extent_tail *)eb;
		endian_swap_extent_tail(et);
	}
	
	return true;
}
