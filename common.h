#ifndef COMMON_H
#define COMMON_H

#include <bttime.h>
#include <mprintf.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define WARNING(x)        do { eprintf("%s\n",x); } while (0)
#define WARNINGF(fmt,...) do { eprintf(fmt,__VA_ARGS__); eprintf("\n"); } while (0)
#define ERROR(x)          do { eprintf("%s\n",x); } while (0)
#define ERRORF(fmt,...)   do { eprintf(fmt,__VA_ARGS__); eprintf("\n"); } while (0)
#define NOTIFY(x)         do { eprintf("%s\n",x); } while (0)
#define NOTIFYF(fmt,...)  do { eprintf(fmt,__VA_ARGS__); eprintf("\n"); } while (0)
#define DEBUG(x)          do { eprintf("%s\n",x); } while (0)
#define DEBUGF(fmt,...)   do { eprintf(fmt,__VA_ARGS__); eprintf("\n"); } while (0)
#define INFO(x)           do { eprintf("%s\n",x); } while (0)
#define INFOF(fmt,...)    do { eprintf(fmt,__VA_ARGS__); eprintf("\n"); } while (0)
#define FATAL(x)          do { eprintf("%s\n",x); abort(); } while (0)
#define FATALF(fmt,...)   do { eprintf(fmt,__VA_ARGS__); eprintf("\n"); abort(); } while (0)

bool args_split(const char * args,int * _argc,char * * * _argv);
char * mkhrtime(bttime_t t);

struct progress_bar;
struct progress_bar * progress_bar_mk(int lines,unsigned long long max);
void print_progress_bar(struct progress_bar * progress_bar,unsigned long long num);
void print_stalled_progress_bar(struct progress_bar * progress_bar,unsigned long long num);
void progress_bar_free(struct progress_bar * pb);

#define OPEN_PROGRESS_BAR_LOOP(_lines,_num,_max) \
{ \
	unsigned long long _progress_bar_max=(_max); \
	struct progress_bar * progress_bar=progress_bar_mk(_lines,_progress_bar_max); \
	assert(progress_bar); \
	\
	while ((unsigned long long)(_num)<_progress_bar_max) {

#define CLOSE_PROGRESS_BAR_LOOP() \
	} \
	\
	progress_bar_free(progress_bar); \
}

#define PRINT_PROGRESS_BAR(_num) print_progress_bar(progress_bar,(_num))

#define PROGRESS_STALLED(_num) print_stalled_progress_bar(progress_bar,(_num))

#endif // #ifndef COMMON_H
