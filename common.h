#ifndef COMMON_H
#define COMMON_H

#include <bttime.h>
#include <mprintf.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define FMT(fmt, ...) do { \
	char * msg = mprintf(fmt, __VA_ARGS__); \
	if (!msg) { \
		eprintf(fmt, __VA_ARGS__); \
		eprintf("\n"); \
	} else { \
		eprintf("%s\n", msg); \
		free(msg); \
	} \
} while (0)

#define WARNING(x)        do { eprintf("%s\n",x); } while (0)
#define WARNINGF(fmt,...) FMT(fmt, __VA_ARGS__)
#define ERROR(x)          do { eprintf("%s\n",x); } while (0)
#define ERRORF(fmt,...)   FMT(fmt, __VA_ARGS__)
// do { eprintf(fmt,__VA_ARGS__); eprintf("\n"); } while (0)
#define NOTIFY(x)         do { eprintf("%s\n",x); } while (0)
#define NOTIFYF(fmt,...)  FMT(fmt, __VA_ARGS__)
#define DEBUG(x)          do { eprintf("%s\n",x); } while (0)
#define DEBUGF(fmt,...)   FMT(fmt, __VA_ARGS__)
#define INFO(x)           do { eprintf("%s\n",x); } while (0)
#define INFOF(fmt,...)    FMT(fmt, __VA_ARGS__)
#define FATAL(x)          do { eprintf("%s\n",x); abort(); } while (0)
#define FATALF(fmt,...)   FMT(fmt, __VA_ARGS__)

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
