/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "common.h"

#include <btfileio.h>
#include <ctype.h>
#include <mprintf.h>
#include <stdlib.h>
#include <string.h>

bool args_split(const char * args,int * _argc,char * * * _argv) {
	const char * p=args;
	int argc=0;
	char * * argv;
	while (*p && isspace(*p)) ++p;
	while (*p) {
		while (*p && !isspace(*p)) ++p;
		while (*p && isspace(*p)) ++p;
		++argc;
	}
	argv=malloc(sizeof(char *)*(argc+1));
	if (!argv) {
		return false;
	}
	p=args;
	argc=0;
	while (*p && isspace(*p)) ++p;
	while (*p) {
		const char * s=p;
		while (*p && !isspace(*p)) ++p;
		argv[argc]=malloc(p-s+1);
		if (!argv[argc]) {
			while (argc--) {
				free(argv[argc]);
			}
			free(argv);
			return false;
		}
		memcpy(argv[argc],s,p-s+1);
		while (*p &&  isspace(*p)) ++p;
		++argc;
	}
	*_argc=argc;
	*_argv=argv;
	return true;
}

char * mkhrtime(bttime_t t) {
	if (t>=86400) {
		return mprintf(
			"%llu Tage %02llu:%02llu:%02llu"
			,t/86400
			,(t/ 3600)%24
			,(t/   60)%60
			,(t      )%60
		);
	} else {
		return mprintf(
			"%02llu:%02llu:%02llu"
			,(t/ 3600)%24
			,(t/   60)%60
			,(t      )%60
		);
	}
}

struct progress_bar {
	bttime_t start_time;
	bttime_t prev_time;
	int lines;
	int format_size;
	unsigned long long max;
	char * space;
};

struct progress_bar * progress_bar_mk(int lines,unsigned long long max) {
	struct progress_bar * retval=malloc(sizeof(*retval));
	
	if (!retval) return NULL;
	
	retval->start_time=bttime();
	retval->prev_time=retval->start_time-1000000000LLU;
	retval->lines=lines;
	
	char * t=mprintf("%llu",retval->max=max);
	if (!t) {
		free(retval);
		return NULL;
	}
	retval->format_size=strlen(t);
	free(t);
	
	retval->space=malloc(retval->lines+2);
	if (!retval->space) {
		free(retval);
		return NULL;
	}
	memset(retval->space,'\n',retval->lines+1);
	retval->space[retval->lines+1]=0;
	
	return retval;
}

void print_progress_bar(struct progress_bar * progress_bar,unsigned long long num) {
	bttime_t curr_time=bttime();
	
	if (num==progress_bar->max || progress_bar->prev_time+333333333LLU<curr_time) {
		progress_bar->prev_time=curr_time;
		char * t1;
		char * t2;
		fdprintf(
			4,
			"\033[K\r%s%*llu/%*llu (%s/~%s)\033[K\r\033[%uA"
			,progress_bar->space
			,progress_bar->format_size
			,num
			,progress_bar->format_size
			,progress_bar->max
			,(t1=mkhrtime((curr_time-progress_bar->start_time)/1000000000))?t1:"ENOMEM"
			,(t2=mkhrtime((bttime_t)((
				(curr_time-progress_bar->start_time)/1000000000.0*progress_bar->max
			)/num)))?t2:"ENOMEM"
			,progress_bar->lines+1
		);
		free(t1);
		free(t2);
	}
}

void print_stalled_progress_bar(struct progress_bar * progress_bar,unsigned long long num) {
	bttime_t curr_time=bttime();
	
	if (num==progress_bar->max || progress_bar->prev_time+333333333LLU<curr_time) {
		progress_bar->prev_time=curr_time;
		
		char * t2;
		fdprintf(
			4,
			"\033[K\r%s%*llu/%*llu (stalled/~%s)\033[K\r\033[%uA"
			,progress_bar->space
			,progress_bar->format_size
			,num
			,progress_bar->format_size
			,progress_bar->max
			,(t2=mkhrtime((bttime_t)((
				(progress_bar->prev_time-progress_bar->start_time)/1000000000.0*progress_bar->max
			)/num)))?t2:"ENOMEM"
			,progress_bar->lines+1
		);
		free(t2);
	}
}

void progress_bar_free(struct progress_bar * pb) {
	free(pb->space);
	free(pb);
}
