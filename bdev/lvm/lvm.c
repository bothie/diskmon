/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "common.h"
#include "crc.h"
#include "bdev.h"

#include <assert.h>
#include <btmacros.h>
#include <btstr.h>
#include <bttime.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mprintf.h>
#include <parseutil.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vasiar.h>

#include "lvm_private.h"

#define DRIVER_NAME "lvm"

/*
 * Public interface
 */

static bool pvmove(
	char * vg,
	char * lv,
	block_t * src_logical_offset,
	block_t * num_extents,
	char * dst_pv,
	char * dst_physical_offset
) {
	ignore(vg);
	ignore(lv);
	ignore(src_logical_offset);
	ignore(num_extents);
	ignore(dst_pv);
	ignore(dst_physical_offset);
	
	errno=ENOSYS;
	
	return false;
}


/*
 * Indirect public interface (via struct bdev)
 */

struct lvm_bdev;

static bool lvm_destroy(struct lvm_bdev * bdev);

static block_t lv_read(struct bdev * bdev, block_t first, block_t num, unsigned char * data, const char * reason);
static block_t lv_write(struct bdev * bdev, block_t first, block_t num, const unsigned char * data);
static bool lv_destroy(struct bdev * bdev);
static block_t lv_short_read(struct bdev * bdev, block_t first, block_t num, u8 * data, u8 * error_map, const char * reason);
static block_t lv_disaster_read(struct bdev * bdev, block_t first, block_t num, u8 * data, u8 * error_map, const u8 * ignore_map, const char * reason);

static struct bdev * lvm_init(struct bdev_driver * bdev_driver,char * name,const char * args);

static bool initialized;

BDEV_INIT {
	if (!bdev_register_driver(DRIVER_NAME,&lvm_init)) {
		eprintf("Couldn't register driver %s",DRIVER_NAME);
	} else {
		initialized=true;
	}
}

BDEV_FINI {
	if (initialized) {
		bdev_deregister_driver(DRIVER_NAME);
	}
}

/*
 * Implementation - everything non-public (except for init of course).
 */

struct range {
	block_t offset;
	block_t size;
};
typedef VASIAR(struct range) range_vasiar_t;

struct pv_in_core {
	uuid uuid;
	char * device;
	// unsigned status_flags // VG_FLAGS_ALLOCATABLE
	block_t first_pe_start_block;
	block_t pe_count;
	range_vasiar_t free_ranges;
	block_t dev_size;
};

struct lv_segments {
	block_t logical_offset;
	block_t num_extents;
	// type // LVS_TYPE_STRIPED
	// stripe_count = 1
//	struct segment {
	unsigned long pv_num;
	block_t physical_offset;
//	}
};

#define LVF_READ 1
#define LVF_WRITE 2
#define LVF_VISIBLE 4
#define LVF_PVMOVE 8
#define LVF_LOCKED 16

struct lvf {
	char * s;
	unsigned f;
} lvf[] = {
	{ "READ", LVF_READ },
	{ "WRITE", LVF_WRITE },
	{ "VISIBLE", LVF_VISIBLE },
	{ "PVMOVE", LVF_PVMOVE },
	{ "LOCKED", LVF_LOCKED },
	{ NULL, 0 }
};

struct lv_in_core {
	char * name;
	uuid uuid;
	// Status for normal LVs: READ, WRITE, VISIBLE
	// Status for pvmove LVs: READ, WRITE, PVMOVE, LOCKED
	unsigned status_flags;
	unsigned long num_segments;
	struct lv_segments * segment;
};

struct conf {
	char * vg_name;
	uuid vg_uuid;
	unsigned long seqno;
	unsigned long pe_size;
	VASIAR(struct pv_in_core) pv;
	VASIAR(struct lv_in_core) lv;
	char * gen_prog;
	char * gen_hrtime;
	char * creat_host;
	char * creat_osline;
	unsigned long creat_time;
	char * creat_hrtime;
};

static void free_config(struct conf * c) {
	int j;
	free(c->vg_name);
	for (j=VASIZE(c->pv);j--;) {
		free(VAACCESS(c->pv,j).device);
	}
	VAFREE(c->pv);
	for (j=VASIZE(c->lv);j--;) {
		struct lv_in_core * lv=&VAACCESS(c->lv,j);
		free(lv->name);
		free(lv->segment);
	}
	VAFREE(c->lv);
	free(c);
}

struct string_node {
	char * ptr;
	size_t size;
	struct string_node * next;
};

struct string_factory {
	struct string_node * frst;
	struct string_node * last;
	size_t size;
};

static struct string_factory * sf_new() {
	struct string_factory * retval=malloc(sizeof(*retval));
	if (retval) {
		retval->frst=retval->last=NULL;
		retval->size=0;
	}
	return retval;
}

#define sf_add sf_add_reown1
static bool sf_add_reown1(struct string_factory * sf,struct string_node * sn) {
	if (!sn) return false;
	sf->size+=sn->size;
	if (!sf->frst) {
		sf->frst=sn;
	} else {
		sf->last->next=sn;
	}
	sf->last=sn;
	return true;
}

static void sf_free(struct string_factory * sf) {
	struct string_node * sn=sf->frst;
	while (sn) {
		struct string_node * nsn=sn->next;
		free(sn->ptr);
		free(sn);
		sn=nsn;
	}
	free(sf);
}

static char * sf_do_mkcstr(struct string_factory * sf,bool reown) {
	char * retval=malloc(sf->size+1);
	if (!retval) {
		if (reown) sf_free(sf);
		return NULL;
	}
	struct string_node * sn=sf->frst;
	char * p=retval;
	while (sn) {
		memcpy(p,sn->ptr,sn->size);
		p+=sn->size;
		struct string_node * nsn=sn->next;
		if (reown) {
			free(sn->ptr);
			free(sn);
		}
		sn=nsn;
	}
	if (reown) {
		free(sf);
	}
	*p=0;
	return retval;
}

#define sf_mkcstr sf_mkcstr_reown0
#define sf_mkcstr_reown0(sf) sf_do_mkcstr(sf,false)
#define sf_mkcstr_reown1(sf) sf_do_mkcstr(sf,true)

static inline struct string_node * sf_mknode_buffer_reown1(char * b,size_t bs) {
	if (unlikely(!b)) return NULL;
	struct string_node * retval=malloc(sizeof(*retval));
	if (unlikely(!retval)) {
		free(b);
	} else {
		retval->ptr=b;
		retval->size=bs;
		retval->next=NULL;
	}
	return retval;
}

static inline struct string_node * sf_mknode_string_reown1(char * s) {
	if (unlikely(!s)) return NULL;
	struct string_node * retval=sf_mknode_buffer_reown1(s,strlen(s));
	if (unlikely(!retval)) {
		free(s);
	}
	return retval;
}

static inline struct string_node * sf_mknode_buffer_reown0(const char * b,size_t bs) {
	if (unlikely(!b)) return NULL;
	return sf_mknode_buffer_reown1(mmemcpy(b,bs),bs);
}

static inline struct string_node * sf_mknode_string_reown0(const char * s) {
	if (unlikely(!s)) return NULL;
	return sf_mknode_buffer_reown0(s,strlen(s));
}

static inline char * unparse_uuid(uuid my_uuid) {
	return mprintf(
		"%.6s-%.4s-%.4s-%.4s-%.4s-%.4s-%.6s"
		, my_uuid +  0
		, my_uuid +  6
		, my_uuid + 10
		, my_uuid + 14
		, my_uuid + 18
		, my_uuid + 22
		, my_uuid + 26
	);
}

static char * unparse_config(struct conf * c) {
	struct string_factory * sf=sf_new();
	if (!sf) {
		goto err;
	}
	if (!sf_add(sf,sf_mknode_string_reown1(mprintf("%s {\nid = \"",c->vg_name)))) {
		goto err;
	}
	if (!sf_add(sf,sf_mknode_string_reown1(unparse_uuid(c->vg_uuid)))) {
		goto err;
	}
	if (!sf_add(sf,sf_mknode_string_reown1(mprintf(
		"\"\n"
		"seqno = %lu\n"
		"status = [\"RESIZEABLE\", \"READ\", \"WRITE\"]\n"
		"extent_size = %lu\n"
		"max_lv = 0\n"
		"max_pv = 0\n"
		"\n"
		"physical_volumes {\n"
		,c->seqno
		,c->pe_size
	)))) {
		goto err;
	}
	for (size_t i=0;i<VASIZE(c->pv);++i) {
		struct pv_in_core * pv=&VAACCESS(c->pv,i);
		if (!sf_add(sf,sf_mknode_string_reown1(mprintf(
			"\n"
			"pv%u {\n"
			"id = \""
			,(unsigned)i
		)))) {
			goto err;
		}
		if (!sf_add(sf,sf_mknode_string_reown1(unparse_uuid(pv->uuid)))) {
			goto err;
		}
		if (!sf_add(sf,sf_mknode_string_reown1(mprintf(
			"\"\ndevice = \"%s\"\n"
			"\n"
			"status = [\"ALLOCATABLE\"]\n"
//			"dev_size = %llu\n" // FIXME: Should be configurable
			"pe_start = %llu\n"
			"pe_count = %llu\n"
			"}\n"
			,pv->device
			,(unsigned long long)pv->first_pe_start_block
			,(unsigned long long)pv->pe_count
		)))) {
			goto err;
		}
	}
	if (!sf_add(sf,sf_mknode_string_reown1(mprintf(
		"}\n"
		"\n"
		"logical_volumes {\n"
	)))) {
		goto err;
	}
	for (size_t i=0;i<VASIZE(c->lv);++i) {
		struct lv_in_core * lv=&VAACCESS(c->lv,i);
		if (!sf_add(sf,sf_mknode_string_reown1(mprintf(
			"\n"
			"%s {\n"
			"id = \""
			,lv->name
		)))) {
			goto err;
		}
		if (!sf_add(sf,sf_mknode_string_reown1(unparse_uuid(lv->uuid)))) {
			goto err;
		}
		if (!sf_add(sf,sf_mknode_string_reown0(
			"\"\n"
			"status = ["
		))) {
			goto err;
		}
		bool first=true;
		unsigned f=0;
		bool is_pvmove=lv->status_flags&LVF_PVMOVE;
		while (lv->status_flags) {
			if (first) {
				first=false;
			} else {
				if (!sf_add(sf,sf_mknode_string_reown0(", "))) {
					goto err;
				}
			}
			for (;lvf[f].s;++f) {
				if (lv->status_flags&lvf[f].f) {
					lv->status_flags&=~lvf[f].f;
					if (!sf_add(sf,sf_mknode_string_reown1(mprintf(
						"\"%s\""
						,lvf[f].s
					)))) {
						goto err;
					}
					break;
				}
			}
		}
		if (!sf_add(sf,sf_mknode_string_reown0(
			"]"
		))) {
			goto err;
		}
		if (is_pvmove) {
			if (!sf_add(sf,sf_mknode_string_reown0(
				"\n"
				"allocation_policy = \"contiguous\""
			))) {
				goto err;
			}
		}
		if (!sf_add(sf,sf_mknode_string_reown1(mprintf(
			"\n"
			"segment_count = %lu\n"
			"\n"
			,lv->num_segments
		)))) {
			goto err;
		}
		for (unsigned s=0;s<lv->num_segments;++s) {
			if (!sf_add(sf,sf_mknode_string_reown1(mprintf(
				"segment%u {\n"
				"start_extent = %llu\n"
				"extent_count = %llu\n"
				"\n"
				"type = \"striped\"\n"
				"stripe_count = 1\t# linear\n"
				"\n"
				"stripes = [\n"
				"\"pv%lu\", %llu\n"
				"]\n"
				"}\n"
				,s+1
				,(unsigned long long)lv->segment[s].logical_offset
				,(unsigned long long)lv->segment[s].num_extents
				,lv->segment[s].pv_num
				,(unsigned long long)lv->segment[s].physical_offset
			)))) {
				goto err;
			}
		}
		if (!sf_add(sf,sf_mknode_string_reown0(
			"}\n"
		))) {
			goto err;
		}
	}
	unsigned long t=time(NULL);
	char * hrt=mstrftime_t("%a %b %d %H:%M:%S %Y",t);
	if (!sf_add(sf,sf_mknode_string_reown1(mprintf(
		"}\n"
		"}\n"
		"# Generated by %s: %s\n"
		"\n"
		"contents = \"Text Format Volume Group\"\n"
		"version = 1\n"
		"\n"
		"description = \"\"\n"
		"\n"
		"creation_host = \"%s\"\t# %s\n"
		"creation_time = %lu\t# %s\n"
		"\n"
		,"uldaf" // c->gen_prog
		,hrt // c->gen_hrtime
		,c->creat_host
		,c->creat_osline
		,t // c->creat_time
		,hrt // c->creat_hrtime
	)))) {
		goto err;
	}
	free(hrt);
	
	return sf_mkcstr_reown1(sf);

err:
	if (sf) sf_free(sf);
	return NULL;
}

static bool remove_from_free_ranges(range_vasiar_t * free_ranges,block_t offset,block_t size) {
	for (size_t i=0;i<VASIZE(*free_ranges);++i) {
		struct range * r=&VAACCESS(*free_ranges,i);
		if (r->offset<=offset
		&&  r->offset+r->size>=offset+size) {
			block_t fo=r->offset;
			block_t fs=r->size;
			if (fo<offset) {
				r->size=offset-fo;
				fo+=r->size;
				fs-=r->size;
				i=VASIZE(*free_ranges);
				r=&VAACCESS(*free_ranges,i);
			}
			fs-=size;
			if (fs) {
				fo+=size;
				r->offset=fo;
				r->size=fs;
			} else {
				struct range * lr=&VAACCESS(*free_ranges,VASIZE(*free_ranges)-1);
				r->offset=lr->offset;
				r->size=lr->size;
				VATRUNCATE(*free_ranges,VASIZE(*free_ranges)-1);
			}
			break;
		}
	}
	return true;
}

static bool parse_fancy_uuid(const char * * p, uuid my_uuid) {
	if ((*p)[ 6]!='-'
	||  (*p)[11]!='-'
	||  (*p)[16]!='-'
	||  (*p)[21]!='-'
	||  (*p)[26]!='-'
	||  (*p)[31]!='-') {
		return false;
	}
	const char * s=*p;
	for (int i=0;i<32;) {
		if (isalnum(**p)) {
			my_uuid[i++] = **p;
		} else if (**p!='-') {
			return false;
		}
		++*p;
	}
	if (*p-s!=38) {
		return false;
	}
	my_uuid[32] = 0;
	return true;
}

#define STRTOL(var,msg) do { \
	char * tmp; \
	var=strtol(p,&tmp,0); \
	if (var>=LONG_MAX) { \
		ERROR(msg); \
		eprintf("[%.20s]\n",p); \
		goto err; \
	} \
	p=tmp; \
} while (0)

#define STRTOLL(var,msg) do { \
	char * tmp; \
	var=strtoll(p,&tmp,0); \
	if (var>=LLONG_MAX) { \
		ERROR(msg); \
		eprintf("[%.20s]\n",p); \
		goto err; \
	} \
	p=tmp; \
} while (0)

#define SKIP_STRING(str,msg) do { \
	if (!parse_skip_string(&p,str)) { \
		ERROR(msg); \
		eprintf("p=[%.*s]\n",(int)(strlen(str)+10),p); \
		eprintf("c=[%s]\n",str); \
		goto err; \
	} \
} while (0)

static struct conf * parse_config(const char * data) {
	struct conf * c=malloc(sizeof(*c));
	if (!c) {
		ERROR("Couldn't allocate memory for VG meta data.");
		return NULL;
	}
	VAINIT(c->pv);
	VAINIT(c->lv);
	const char * p=data;
	if (!parse_string(&p," ",NULL,NULL,&c->vg_name)) {
		ERROR("Couldn't parse VG name.");
		goto err;
	}
	SKIP_STRING("{\nid = \"","[1] Couldn't parse VG config.");
	if (!parse_fancy_uuid(&p,c->vg_uuid)) {
		ERROR("Couldn't parse VG's uuid.");
		goto err;
	}
	SKIP_STRING("\"\nseqno = ","[2] Couldn't parse VG config.");
	STRTOL(c->seqno,"Couldn't parse VG's seqno.");
	SKIP_STRING("\nstatus = [\"RESIZEABLE\", \"READ\", \"WRITE\"]\nextent_size = ","[3] Couldn't parse VG config (status value).");
	STRTOL(c->pe_size,"Couldn't parse VG's PE size.");
	SKIP_STRING("\nmax_lv = 0\nmax_pv = 0\n\nphysical_volumes {\n","[4] Couldn't parse VG config.");
	while (*p=='\n') {
		SKIP_STRING("\npv","Couldn't parse PV's config.");
		unsigned long pvnum;
		STRTOL(pvnum,"Couldn't parse PV num\n");
		if (pvnum!=(unsigned long)VASIZE(c->pv)) {
			ERROR("Read unexpected PV num\n");
			goto err;
		}
		struct pv_in_core * pv=&VANEW(c->pv);
		VAINIT(pv->free_ranges);
		SKIP_STRING(" {\nid = \"","Couldn't parse PV's config.");
		if (!parse_fancy_uuid(&p,pv->uuid)) {
			ERROR("Couldn't parse PV's uuid.");
			goto err;
		}
		SKIP_STRING("\"\ndevice = \"","Couldn't parse PV's config.");
		if (!parse_string(&p,"\"",NULL,NULL,&pv->device)) {
			ERROR("Couldn't parse PV's device name.");
			goto err;
		}
		SKIP_STRING("\n\nstatus = [\"ALLOCATABLE\"]\n","Couldn't parse PV's config (status value).");
		if (*p=='d') {
			SKIP_STRING("dev_size = ","Couldn't parse PV's config (dev_size).");
			STRTOLL(pv->dev_size,"Couldn't parse PV's dev_size.");
			SKIP_STRING("\n","Couldn't parse PV's config (dev_size).");
			if (!pv->dev_size) {
				ERROR("Explicitely given dev_size MUST NOT be zero (but is).");
				goto err;
			}
		} else {
			pv->dev_size=0;
		}
		SKIP_STRING("pe_start = ","Couldn't parse PV's config (pe_start).");
		STRTOLL(pv->first_pe_start_block,"Couldn't parse PV's first PE start block.");
		SKIP_STRING("\npe_count = ","Couldn't parse PV's config.");
		STRTOLL(pv->pe_count,"Couldn't parse PV's PE count.");
		SKIP_STRING("\n}\n","Couldn't parse PV's config.");
		struct range * r=&VAACCESS(pv->free_ranges,0);
		r->offset=0;
		r->size=pv->pe_count;
	}
	SKIP_STRING("}\n\nlogical_volumes {\n","Couldn't parse VG's pv list.");
	do {
		if (!parse_skip(&p,'\n')) {
			ERROR("[4] Couldn't parse LV's config.");
			goto err;
		}
		struct lv_in_core * lv=&VANEW(c->lv);
		if (!parse_string(&p," ",NULL,NULL,&lv->name)) {
			ERROR("Couldn't parse LV name.");
			goto err;
		}
		SKIP_STRING("{\nid = \"","[1] Couldn't parse LV's config.");
		if (!parse_fancy_uuid(&p,lv->uuid)) {
			ERROR("Couldn't parse LV's uuid.");
			goto err;
		}
		SKIP_STRING("\"\nstatus = [","[2] Couldn't parse LV's config.");
		lv->status_flags=0;
		while (*p=='"') {
			++p;
			for (unsigned i=0;lvf[i].s;++i) {
				if (parse_skip_string(&p,lvf[i].s)) {
					if (parse_skip(&p,'"')) {
						if (lv->status_flags&lvf[i].f) {
							ERRORF("LV-Flag %s given twice.",lvf[i].s);
							goto err;
						}
						lv->status_flags|=lvf[i].f;
						break;
					} else {
						p-=strlen(lvf[i].s);
					}
				}
			}
			if (parse_skip(&p,']')) {
				break;
			}
			SKIP_STRING(", ","[6] Couldn't parse LV's config.");
		}
		if (lv->status_flags!=(LVF_READ|LVF_WRITE|LVF_VISIBLE)
		&&  (lv->status_flags!=(LVF_READ|LVF_WRITE|LVF_PVMOVE|LVF_LOCKED)
		||   strstartcmp(lv->name,"pvmove"))) {
			ERRORF(
				"Illegal LV-Flag combination while reading LV %s's config."
				,lv->name
			);
			
			goto err;
		}
		if (lv->status_flags&LVF_PVMOVE) {
			SKIP_STRING("\nallocation_policy = \"contiguous\"","Couldn't parse pvmove's LV config.");
		}
		SKIP_STRING("\nsegment_count = ","[5] Couldn't parse LV's config.");
		STRTOL(lv->num_segments,"Couldn't parse LV's segment count.");
		SKIP_STRING("\n\n","[3] Couldn't parse LV's config.");
		lv->segment=malloc(sizeof(*lv->segment)*lv->num_segments);
		for (unsigned s=0;s<lv->num_segments;++s) {
			SKIP_STRING("segment","Couldn't parse LV's segment's config.");
			unsigned long n;
			STRTOL(n,"Couldn't parse LV's segment's num.");
			if (n!=s+1) {
				ERROR("Unexpected LV's segment's num.");
				goto err;
			}
			SKIP_STRING(" {\nstart_extent = ","Couldn't parse LV's segment's config.");
			STRTOLL(lv->segment[s].logical_offset,"Couldn't parse LV's segment count.");
			SKIP_STRING("\nextent_count = ","Couldn't parse LV's segment's config.");
			STRTOLL(lv->segment[s].num_extents,"Couldn't parse LV's segment size.");
			SKIP_STRING("\n\ntype = \"striped\"\nstripe_count = 1\t# linear\n\nstripes = [\n\"pv","Couldn't parse LV's segment's config.");
			STRTOL(lv->segment[s].pv_num,"Couldn't parse LV's segment's PV num.");
			SKIP_STRING("\", ","Couldn't parse LV's segment's config.");
			STRTOLL(lv->segment[s].physical_offset,"Couldn't parse LV's segment's physical offset.");
			SKIP_STRING("\n]\n}\n","Couldn't parse LV's segment's config.");
			
			struct pv_in_core * pv=&VAACCESS(c->pv,lv->segment[s].pv_num);
			if (!remove_from_free_ranges(
				&pv->free_ranges,
				lv->segment[s].physical_offset,
				lv->segment[s].num_extents
			)) {
				ERRORF(
					"Segment %i of LV %s overlaps with a different segement in VG %s."
					,s
					,lv->name
					,c->vg_name
				);
				goto err;
			}
		} // while (s++<lv->num_segments)
		SKIP_STRING("}\n","[1] Couldn't parse VG's config.");
	} while (*p=='\n');
	SKIP_STRING("}\n}\n# Generated by ","[2] Couldn't parse VG's config.");
	if (!parse_string(&p,":",NULL,NULL,&c->gen_prog)) {
		ERROR("[8] Couldn't parse VG's config.");
		goto err;
	}
	if (!parse_skip(&p,' ')) {
		ERROR("[9] Couldn't parse VG's config.");
		goto err;
	}
	if (!parse_string(&p,"\n",NULL,NULL,&c->gen_hrtime)) {
		ERROR("[10] Couldn't parse VG's config.");
		goto err;
	}
	SKIP_STRING("\ncontents = \"Text Format Volume Group\"\nversion = 1\n\ndescription = \"\"\n\ncreation_host = \"","[3] Couldn't parse VG's config.");
	if (!parse_string(&p,"\"",NULL,NULL,&c->creat_host)) {
		ERROR("[11] Couldn't parse VG's config.");
		goto err;
	}
	SKIP_STRING("\t# ","[4] Couldn't parse VG's config.");
	if (!parse_string(&p,"\n",NULL,NULL,&c->creat_osline)) {
		ERROR("[12] Couldn't parse VG's config.");
		goto err;
	}
	SKIP_STRING("creation_time = ","[5] Couldn't parse VG's config.");
	STRTOL(c->creat_time,"[13] Couldn't parse VG's config.");
	SKIP_STRING("\t# ","[6] Couldn't parse VG's config.");
	if (!parse_string(&p,"\n",NULL,NULL,&c->creat_hrtime)) {
		ERROR("[14] Couldn't parse VG's config.");
		goto err;
	}
	SKIP_STRING("\n","[7] Couldn't parse VG's config.");
	return c;

err:
	free_config(c);
	return NULL;
}

struct pv {
	uuid uuid;
	int num;
	unsigned long long data_start;
	unsigned long long meta_start;
	unsigned long long meta_size;
	unsigned long long conf_start;
	unsigned long long conf_size;
	unsigned char * conf;
};

struct lvm_bdev {
	unsigned ndisks;
	struct bdev * * bdev;
	struct pv * pv;
	struct conf * conf;
	unsigned refcount;
};

struct bdev_private {
	struct lvm_bdev * vg;
	int lvnum;
};

// <dev1> [<dev2> [...]]
//
// name will be reowned by init, args will be freed by the caller.
static struct bdev * lvm_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	int num_disks;
	const char * arg_p;
	struct bdev_private * * lvs=NULL;
	
	free(name); // Not needed here (at least not until now)
	
	arg_p=args;
	num_disks=0;
	while (*arg_p) {
		while (*arg_p && *arg_p!=' ') ++arg_p;
		++num_disks;
		while (*arg_p && *arg_p==' ') ++arg_p;
	}
	
	struct lvm_bdev * lvm=malloc(sizeof(*lvm));
	if (!lvm) {
		
		ERROR("Couldn't allocate lvm device desctiptor.");
		goto err;
	}
	lvm->ndisks=num_disks;
	lvm->pv=NULL;
	lvm->refcount=0;
	
	lvm->bdev=malloc(sizeof(*lvm->bdev)*num_disks);
	if (!lvm->bdev) {
		ERROR("Couldn't allocate device list array.");
		goto err;
	}
	lvm->pv=malloc(sizeof(*lvm->pv)*num_disks);
	if (!lvm->pv) {
		ERROR("Couldn't allocate pv list array.");
		goto err;
	}
	lvm->conf=NULL;
	arg_p=args;
	do {
		--num_disks;
		lvm->pv[num_disks].conf=NULL;
		lvm->pv[num_disks].num=-1;
	} while (num_disks);
	unsigned char buffer[512];
	struct pv_superblock * pv=(struct pv_superblock *)buffer;
	struct vg_superblock * vg=(struct vg_superblock *)buffer;
	while (*arg_p) {
		char * disk_device_name;
		{
			const char * s = arg_p;
			while (*arg_p && *arg_p != ' ') ++arg_p;
			disk_device_name = malloc(arg_p - s + 1);
			memcpy(disk_device_name, s, arg_p - s);
			disk_device_name[arg_p - s] = 0;
		}
		lvm->bdev[num_disks]=bdev_lookup_bdev(disk_device_name);
		if (!lvm->bdev[num_disks]) {
			ERROR("Couldn't locate all devices.");
			goto err;
		}
		if (!num_disks) {
			if (bdev_get_block_size(lvm->bdev[0])!=512) {
				ERROR("Can't handle block_sizes != 512 right now");
				goto err;
			}
		} else {
			if (bdev_get_block_size(lvm->bdev[0]) != bdev_get_block_size(lvm->bdev[num_disks])) {
				ERROR("Block sizes differ between component devices");
				goto err;
			}
		}
		if (1 != bdev_read(lvm->bdev[num_disks], 1, 1, buffer, "lvm-pv-sb")) {
			ERROR("Couldn't read PV superblock");
			goto err;
		}
		if (memcmp(pv->magic,PV_MAGIC,PV_MAGIC_LEN)) {
			ERRORF("This (%s) is not a PV.",disk_device_name);
			goto err;
		}
		if (pv->crc!=calc_crc(
			&pv->offset,
			512-((unsigned char *)&pv->offset-(unsigned char *)pv)
		)) {
			ERROR("Incorrect checksum in PV superblock");
			goto err;
		}
		if (pv->this_block_number!=1
		||  pv->offset!=0x20
		||  memcmp(pv->type,PV_TYPE,PV_TYPE_LEN)) {
			ERROR("Unexpected or inconsistent data in PV superblock");
			goto err;
		}
		memcpy(lvm->pv[num_disks].uuid,pv->pv_uuid,UUID_LEN);
		if ((off_t)pv->device_size!=(off_t)(bdev_get_block_size(lvm->bdev[num_disks])*bdev_get_size(lvm->bdev[num_disks]))) {
			WARNINGF(
				"Superblock of PV %s contains an inconsistent view about "
				"the size of this device - ignored (LVM tools ignore this too)"
				,disk_device_name
			);
		}
		lvm->pv[num_disks].data_start=pv->pv_area.offset;
		lvm->pv[num_disks].meta_start=pv->vg_metadata_area.offset;
		lvm->pv[num_disks].meta_size=pv->vg_metadata_area.size;
		if (pv->null1.offset
		||  pv->null1.size
		||  pv->null2.offset
		||  pv->null2.size) {
			ERRORF(
				"This PV has more than one metadata area or more than one data area (%llx,%llx,%llx,%llx)"
				,(unsigned long long)pv[num_disks].null1.offset
				,(unsigned long long)pv[num_disks].null1.size
				,(unsigned long long)pv[num_disks].null2.offset
				,(unsigned long long)pv[num_disks].null2.size
			);
			goto err;
		}
		if (lvm->pv[num_disks].meta_start%bdev_get_block_size(lvm->bdev[0])) {
			ERROR("VG superblock doesn't start at a block boundary");
			goto err;
		}
		if (1!=bdev_read(
			lvm->bdev[num_disks],
			lvm->pv[num_disks].meta_start/bdev_get_block_size(lvm->bdev[0]),
			1,buffer
			, "lvm-vg-sb"
		)) {
			ERROR("Couldn't read VG superblock");
			goto err;
		}
		if (vg->checksum!=calc_crc(
			&vg->magic,
			512-(((unsigned char *)&(vg->magic))-(unsigned char *)vg)
		)) {
			ERROR("Incorrect checksum in VG superblock");
			goto err;
		}
		if (memcmp(vg->magic,VG_MAGIC,VG_MAGIC_LEN)
		||  vg->version!=VG_VERSION
		||  vg->start!=lvm->pv[num_disks].meta_start
		||  vg->size!=lvm->pv[num_disks].meta_size) {
			ERROR("Unexpected or inconsistent data in VG superblock");
			goto err;
		}
		lvm->pv[num_disks].conf_start=vg->vg_metadata_area.offset;
		lvm->pv[num_disks].conf_size=vg->vg_metadata_area.size;
		if ((lvm->pv[num_disks].conf_start+lvm->pv[num_disks].meta_start)%bdev_get_block_size(lvm->bdev[0])) {
			ERROR("LVM metadata doesn't start at block boundary.");
			goto err;
		}
		block_t num_blocks=(lvm->pv[num_disks].conf_size+bdev_get_block_size(lvm->bdev[0])-1)
			/bdev_get_block_size(lvm->bdev[0]);
		lvm->pv[num_disks].conf=malloc(num_blocks*bdev_get_block_size(lvm->bdev[0]));
		if (!lvm->pv[num_disks].conf) {
			ERROR("Couldn't allocate memory for VG meta data.");
			goto err;
		}
		if (num_blocks!=bdev_read(
			lvm->bdev[num_disks],
			(lvm->pv[num_disks].conf_start+lvm->pv[num_disks].meta_start)/bdev_get_block_size(lvm->bdev[0]),
			num_blocks,lvm->pv[num_disks].conf
			, "lvm-lv-conf"
		)) {
			ERROR("Couldn't read LVM metadata.");
			goto err;
		}
		uint32_t crc;
		if (vg->vg_metadata_area.checksum!=(crc=calc_crc(
			lvm->pv[num_disks].conf,
			lvm->pv[num_disks].conf_size
		))) {
			ERRORF("Incorrect checksum in LVM metadata. Expected 0x%x, got 0x%x",vg->vg_metadata_area.checksum,crc);
//			goto err;
		}
		if (num_disks) {
			if (lvm->pv[num_disks].conf_size!=lvm->pv[0].conf_size
			||  memcmp(lvm->pv[num_disks].conf,lvm->pv[0].conf,lvm->pv[0].conf_size)) {
				eprintf("LVM metadata differ between component PVs\n");
				goto err;
			}
			free(lvm->pv[num_disks].conf);
			lvm->pv[num_disks].conf=NULL;
		} else {
			size_t s=lvm->pv[0].conf_size-1;
			for (size_t i=0;i<s;++i) {
				if (unlikely(!lvm->pv[0].conf[i])) {
					ERROR("Unexpected NULL character in LV config space.");
					goto err; // Warum war der auskommentiert?
				}
			}
			if (lvm->pv[0].conf[s]) {
				ERROR("No terminating NULL character in LV config space.");
				goto err; // Warum war der auskommentiert?
			}
			lvm->conf=parse_config((char *)lvm->pv[0].conf);
			if (!lvm->conf) goto err;
			if (VAACCESS(lvm->conf->pv,0).dev_size
			&&  VAACCESS(lvm->conf->pv,0).dev_size != bdev_get_size(lvm->bdev[0])) {
				ERROR("Explicitely given dev_size doesn't match actual device size.");
				goto err;
			}
			char * cmp=unparse_config(lvm->conf);
			if (!cmp) {
				ERROR("unparse_config failed.");
				goto err;
			}
/*
			// Die Daten MÜSSEN sich unterscheiden, da die 
			// Erstellungszeit und Kommentare beim Schreiben neu 
			// generiert werden.
			if (memcmp(lvm->pv[0].conf,cmp,lvm->pv[0].conf_size)) {
				ERROR("unparse_config returned different data as expected.");
				int fd;
				fd=open("lvm-metadata.org",O_WRONLY|O_TRUNC|O_CREAT,0666);
				write(fd,lvm->pv[0].conf,lvm->pv[0].conf_size);
				close(fd);
				fd=open("lvm-metadata.cmp",O_WRONLY|O_TRUNC|O_CREAT,0666);
				write(fd,cmp,lvm->pv[0].conf_size);
				close(fd);
				goto err;
			}
*/
			free(cmp);
		}
		++num_disks;
		while (*arg_p && *arg_p==' ') ++arg_p;
		free(disk_device_name);
	}
	
	for (int i=0;i<num_disks;++i) {
		for (int j=0;j<num_disks;++j) {
			if (!memcmp(
				lvm->pv[i].uuid,
				VAACCESS(lvm->conf->pv,j).uuid,
				UUID_LEN
			)) {
				if (lvm->pv[j].num!=-1) {
					eprintf(
						"UUID %.32s for a PV is registered twice in LVM metadata config."
						,lvm->pv[j].uuid
					);
					goto err;
				}
				lvm->pv[j].num=i;
			}
		}
	}
	for (int i=0;i<num_disks;++i) {
		if (lvm->pv[i].num==-1) {
			eprintf("UUID %.32s for this PV is not registered in LVM metadata config.",lvm->pv[i].uuid);
			goto err;
		}
	}
	
	struct conf * c=lvm->conf;
	unsigned npvs=VASIZE(c->pv);
	printf(
		"Configuration of VG %s:\n"
		"\tNum physical volumes: %u\n"
		"\tUUID: %.32s\n"
		"\tSequence number: %lu\n"
		"\tSize of physical extents (in blocks): %lu\n"
		,c->vg_name
		,npvs
		,c->vg_uuid
		,c->seqno
		,c->pe_size
	);
	for (unsigned i=0;i<npvs;++i) {
		struct pv_in_core * pvic = &VAACCESS(c->pv, i);
		printf(
			"\tPhysical volume %i:\n"
			"\t\tGiven in Argument %i\n"
			"\t\tBacking device (this field is informal only): %s\n"
			"\t\tUUID: %.32s\n"
			"\t\tFirst block of PV data heap (in blocks): %llu\n"
			"\t\tNum physical extents in dh (in PEs): %llu\n"
			, i
			, lvm->pv[i].num
			, pvic->device
			, pvic->uuid
			, (unsigned long long)pvic->first_pe_start_block
			, (unsigned long long)pvic->pe_count
		);
		if (VASIZE(pvic->free_ranges)) {
			printf("\t\t%u Free Ranges:\n", (unsigned)VASIZE(pvic->free_ranges));
			for (size_t rnum = 0; rnum < VASIZE(pvic->free_ranges); ++rnum) {
				struct range * r = &VAACCESS(pvic->free_ranges, rnum);
				eprintf("\t\t\t%10llu..%10llu[%9llu]\n",(unsigned long long)r->offset,(unsigned long long)(r->offset+r->size-1),(unsigned long long)r->size);
			}
		}
	}
	unsigned nlvs=VASIZE(c->lv);
	printf( 
		"\tNum logical volumes: %u\n"
		,nlvs
	);
	for (unsigned i=0;i<nlvs;++i) {
		struct lv_in_core * lv=&VAACCESS(c->lv,i);
		block_t size=0;
		printf( 
			"\tLogical volume %i:\n"
			"\t\tName: %s\n"
			"\t\tUUID: %.32s\n"
			"\t\tNum segments: %lu\n"
			,i
			,lv->name
			,lv->uuid
			,lv->num_segments
		);
		for (unsigned s=0;s<lv->num_segments;++s) {
			printf(
				"\t\tSegment %i: LV(%s/%s:%10llu-%10llu[%9llu]): PE(%s:%9llu-%9llu)\n"
				,s
				,c->vg_name,lv->name
				,(unsigned long long)lv->segment[s].logical_offset
				,(unsigned long long)(lv->segment[s].logical_offset+lv->segment[s].num_extents-1)
				,(unsigned long long)lv->segment[s].num_extents
				,bdev_get_name(lvm->bdev[lvm->pv[lv->segment[s].pv_num].num])
				,(unsigned long long)lv->segment[s].physical_offset
				,(unsigned long long)(lv->segment[s].physical_offset+lv->segment[s].num_extents-1)
			);
			if (size!=lv->segment[s].logical_offset) {
				ERRORF(
					"Expected logical offset %llu in segment %i but got %llu."
					,(unsigned long long)size
					,s
					,(unsigned long long)lv->segment[s].logical_offset
				);
				goto err;
			}
			size+=lv->segment[s].num_extents;
		}
		size*=c->pe_size;
	}
	lvs=malloc(sizeof(*lvs)*nlvs);
	if (!lvs) {
		ERRORF("malloc error while trying to register LVs of VG %s: %s.",c->vg_name,strerror(errno));
		goto err;
	}
	for (unsigned i=0;i<nlvs;++i) {
		lvs[i]=NULL;
	}
	for (unsigned i=0;i<nlvs;++i) {
		lvs[i]=malloc(sizeof(**lvs));
		if (!lvs[i]) {
			ERRORF("malloc error while trying to register LVs of VG %s: %s.",c->vg_name,strerror(errno));
			goto err;
		}
		/*
		 * backlink pointer to lvm structure will be misused as 
		 * temporal storage for pointer to the lv name.
		 */
		lvs[i]->vg=(struct lvm_bdev * )mprintf("%s/%s",c->vg_name,VAACCESS(c->lv,i).name);
	}
	for (unsigned i=0;i<nlvs;++i) {
		struct lv_in_core * lv=&VAACCESS(c->lv,i);
		block_t size=0;
		for (unsigned s=0;s<lv->num_segments;++s) {
			size+=lv->segment[s].num_extents;
		}
		size*=c->pe_size;
		
		/*
		 * This is where the lv name will be read from the backlink 
		 * pointer.
		 */
		char * lv_name = (char *)lvs[i]->vg;
		lvs[i]->vg=lvm;
		lvs[i]->lvnum=i;
		++lvm->refcount;
		struct bdev * dev;
		if (!(dev=bdev_register_bdev(
			bdev_driver,
			lv_name,
			size,
			bdev_get_block_size(lvm->bdev[0]),
			lv_destroy,
			lvs[i]
		))) {
			ERRORF("Couldn't register %s.", lv_name);
			free(lv_name);
			--lvm->refcount;
			free(lvs[i]);
			for (++i;i<nlvs;++i) {
				free(lvs[i]->vg);
				free(lvs[i]);
			}
			return (struct bdev *)0xdeadbeef;
		}
		bdev_set_read_function(dev, lv_read);
		bdev_set_write_function(dev, lv_write);
		bdev_set_short_read_function(dev, lv_short_read);
		bdev_set_disaster_read_function(dev, lv_disaster_read);
	}
	free(lvs);
	/*
	 * 1. Sicherstellen, daß alle Devices zur selben volume group gehören
	 * 2. Prüfen, ob alle pvs anwesend sind -> sonst Warnung aber nicht 
	 *    Abbruch.
	 * 3. Alle lvs finden (auch die, von denen nicht alle pvs anwesend 
	 *    sind) und separat registrieren.
	 *
	 * -> Die VG an sich wird NICHT registriert. Wenn es keien LVs gibt, 
	 *    wird diese Funktion fehlschlagen.
	 */
	
	// Return a non-NULL pointer to make a check like "if (!init(...)) 
	// exit(2);" fail, but don't give 'em a valid pointer.
	
	return (struct bdev *)0xdeadbeef;

err:
	if (lvs) {
		for (unsigned i=0;i<nlvs;++i) {
			if (lvs[i]) {
				free(lvs[i]->vg);
				free(lvs[i]);
			}
		}
		free(lvs);
	}
	if (lvm) {
		lvm_destroy(lvm);
	}
	return NULL;
}

bool lvm_destroy(struct lvm_bdev * lvm) {
	if (lvm->pv) {
		if (lvm->conf) {
			free_config(lvm->conf);
		}
		for (unsigned i=0;i<lvm->ndisks;++i) {
			free(lvm->pv[i].conf);
		}
		free(lvm->pv);
	}
	free(lvm->bdev);
	free(lvm);
	return true;
}

bool lv_destroy(struct bdev * bdev) {
	struct bdev_private * private = bdev_get_private(bdev);
	
	if (!--private->vg->refcount) {
		lvm_destroy(private->vg);
	}
	free(private);
	return true;
}

block_t lv_read(struct bdev * bdev, block_t first, block_t num, u8 * data, const char * reason) {
	return lv_disaster_read(bdev, first, num, data, NULL, NULL, reason);
}

block_t lv_short_read(struct bdev * bdev, block_t first, block_t num, u8 * data, u8 * error_map, const char * reason) {
	return lv_disaster_read(bdev, first, num, data, error_map, NULL, reason);
}

block_t lv_disaster_read(struct bdev * bdev, block_t first, block_t num, u8 * data, u8 * error_map, const u8 * ignore_map, const char * reason) {
	struct bdev_private * lv_bdev = bdev_get_private(bdev);
	
//	struct bdev * * vg_bdev=lv_bdev->vg->bdev;
	struct conf * conf=lv_bdev->vg->conf;
	struct lv_in_core * lv=&VAACCESS(conf->lv,lv_bdev->lvnum);
	block_t lso,pso,ssi;
	block_t retval=0;
	for (unsigned long s=0;s<lv->num_segments;++s) {
		lso=conf->pe_size*lv->segment[s].logical_offset;
		pso=conf->pe_size*lv->segment[s].physical_offset;
		ssi=conf->pe_size*lv->segment[s].num_extents;
		if (lso+ssi>first) {
			block_t sn=lso+ssi-first;
			block_t ro=first-lso;
			if (sn>num) {
				sn=num;
			}
			struct pv * pv=lv_bdev->vg->pv+(lv->segment[s].pv_num);
			struct pv_in_core * cpv=&VAACCESS(conf->pv,lv->segment[s].pv_num);
			block_t r;
			if (error_map && ignore_map) {
				r=bdev_disaster_read(
					lv_bdev->vg->bdev[pv->num],
					cpv->first_pe_start_block+pso+ro,
					sn,
					data,
					error_map,
					ignore_map
					, reason
				);
			} else if (error_map) {
				r=bdev_short_read(
					lv_bdev->vg->bdev[pv->num],
					cpv->first_pe_start_block+pso+ro,
					sn,
					data,
					error_map
					, reason
				);
			} else {
				r=bdev_read(
					lv_bdev->vg->bdev[pv->num],
					cpv->first_pe_start_block+pso+ro,
					sn,
					data
					, reason
				);
			}
			if (!r) {
				return retval;
			}
			retval+=r;
			num-=sn;
			if (!num) {
				return retval;
			}
			data+=sn*bdev_get_block_size(*lv_bdev->vg->bdev);
			if (error_map) {
				error_map+=sn*bdev_get_block_size(*lv_bdev->vg->bdev);
			}
			first+=sn;
		}
	}
	if (num) {
		WARNINGF(
			"Attempt to access %s/%s beyond end of device."
//			,bdev_get_name(*lv_bdev->bdev)
			,lv_bdev->vg->conf->vg_name
			,VAACCESS(lv_bdev->vg->conf->lv,lv_bdev->lvnum).name
		);
	}
	return retval;
}

block_t lv_write(struct bdev * bdev, block_t first, block_t num, const unsigned char * data) {
	struct bdev_private * lv_bdev = bdev_get_private(bdev);
	
//	struct bdev * * vg_bdev=lv_bdev->vg->bdev;
	struct conf * conf=lv_bdev->vg->conf;
	struct lv_in_core * lv=&VAACCESS(conf->lv,lv_bdev->lvnum);
	block_t lso,pso,ssi;
	block_t retval=0;
	for (unsigned long s=0;s<lv->num_segments;++s) {
		lso=conf->pe_size*lv->segment[s].logical_offset;
		pso=conf->pe_size*lv->segment[s].physical_offset;
		ssi=conf->pe_size*lv->segment[s].num_extents;
		if (lso+ssi>first) {
			block_t sn=lso+ssi-first;
			block_t ro=first-lso;
			if (sn>num) {
				sn=num;
			}
			struct pv * pv=lv_bdev->vg->pv+(lv->segment[s].pv_num);
			struct pv_in_core * cpv=&VAACCESS(conf->pv,lv->segment[s].pv_num);
			block_t w=bdev_write(
				lv_bdev->vg->bdev[pv->num],
				cpv->first_pe_start_block+pso+ro,
				sn,
				data
			);
			if (!w) {
				return retval;
			}
			retval+=w;
			num-=sn;
			if (!num) {
				return retval;
			}
			data+=sn*bdev_get_block_size(*lv_bdev->vg->bdev);
			first+=sn;
		}
	}
	if (num) {
		WARNINGF(
			"Attempt to access %s/%s beyond end of device."
//			,bdev_get_name(*lv_bdev->bdev)
			,lv_bdev->vg->conf->vg_name
			,VAACCESS(lv_bdev->vg->conf->lv,lv_bdev->lvnum).name
		);
	}
	return retval;
}
