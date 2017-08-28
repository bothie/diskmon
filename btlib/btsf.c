/*
 * btsf.c. Part of the bothie-utils.
 *
 * Copyright (C) 2010-2015 Bodo Thiesen <bothie@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "btsf.h"
#include "lstring.h"

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

struct string_factory * sf_new() {
	struct string_factory * retval=malloc(sizeof(*retval));
	if (retval) {
		retval->frst=retval->last=NULL;
		retval->size=0;
	}
	return retval;
}

void sf_add_node(struct string_factory * sf, struct string_node * sn) {
	sn->next = NULL;
	if (!sf->frst) {
		sf->frst = sn;
	} else {
		sf->last->next = sn;
	}
	sf->last = sn;
	sf->size += sn->size;
}

bool sf_add_char(struct string_factory * sf, char c) {
	struct string_node * sn = malloc(sizeof(*sn));
	if (unlikely(!sn)) {
		return false;
	}
	sn->ptr = malloc(1);
	if (!sn->ptr) {
		free(sn);
		return false;
	}
	sn->size = 1;
	*sn->ptr = c;
	
	sf_add_node(sf, sn);

	return true;
}

bool sf_add_buffer_reown1(struct string_factory * sf,char * b,size_t bs) {
	if (unlikely(!b)) return true;
	if (!bs) {
		free(b);
		return true;
	}
	struct string_node * sn=malloc(sizeof(*sn));
	if (unlikely(!sn)) {
		free(b);
		return false;
	}
	sn->ptr=b;
	sn->size=bs;
	sf_add_node(sf, sn);
	
	return true;
}

bool sf_add_lstring_reownX(struct string_factory * sf, struct lstring * lstring, bool reown) {
	bool retval = false;
	
	struct string_node * sn=malloc(sizeof(*sn));
	if (unlikely(!sn)) {
		goto out;
	}
	sn->ptr = malloc(sn->size = lstring->length);
	if (!sn->ptr) {
		free(sn);
		goto out;
	}
	memcpy(sn->ptr, lstring->data, sn->size);
	sf_add_node(sf, sn);
	
	retval = true;

out:
	if (reown) {
		free(lstring);
	}
	return retval;
}

void sf_free(struct string_factory * sf) {
	struct string_node * sn=sf->frst;
	while (sn) {
		struct string_node * nsn=sn->next;
		free(sn->ptr);
		free(sn);
		sn=nsn;
	}
	free(sf);
}

char * sf_c_str_reownX(struct string_factory * sf,bool reown) {
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

struct lstring * sf_lstr_reownX(struct string_factory * sf, bool reown) {
	struct lstring * retval = malloc(sizeof(*retval) + sf->size);
	
	if (!retval) {
		if (reown) sf_free(sf);
		return NULL;
	}
	
	retval->length = sf->size;
	
	struct string_node * sn = sf->frst;
	char * p = retval->data;
	while (sn) {
		memcpy(p, sn->ptr, sn->size);
		p += sn->size;
		struct string_node * nsn = sn->next;
		if (reown) {
			free(sn->ptr);
			free(sn);
		}
		sn = nsn;
	}
	
	assert(p == retval->data + retval->length);
	
	if (reown) {
		free(sf);
	}
	
	return retval;
}
