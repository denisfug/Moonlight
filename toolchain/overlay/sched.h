/* Moonlight stub: sched.h for libc++ build on Ubuntu with -nostdinc. */
#ifndef _SCHED_H
#define _SCHED_H

struct sched_param { int sched_priority; };

int sched_get_priority_min(int);
int sched_get_priority_max(int);
int sched_yield(void);

#endif /* _SCHED_H */
