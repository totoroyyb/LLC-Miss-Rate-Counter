/*
 * sched.h - low-level scheduler routines (e.g. adding or preempting cores)
 */

#pragma once

#include <base/stddef.h>
#include <base/bitmap.h>
#include <base/limits.h>

#include "defs.h"

/*
 * Global variables
 */

DECLARE_BITMAP(sched_allowed_cores, NCPU);
extern unsigned int sched_siblings[NCPU];
extern unsigned int sched_dp_core;
extern unsigned int sched_ctrl_core;
extern unsigned int sched_linux_core;
/* per socket state */
struct socket {
	DEFINE_BITMAP(cores, NCPU);
};
extern struct socket socket_state[NNUMA];

/*
 * Core iterators
 */

extern unsigned int sched_cores_tbl[NCPU];
extern int sched_cores_nr;

#define sched_for_each_allowed_core(core, tmp)			\
	for ((core) = sched_cores_tbl[0], (tmp) = 0;		\
	     (tmp) < sched_cores_nr &&				\
	     ({(core) = sched_cores_tbl[(tmp)]; true;});	\
	     (tmp)++)


/*
 * API for the rest of the IOkernel
 */

extern void sched_poll(void);
// extern int sched_add_core(struct proc *p);
// extern int sched_attach_proc(struct proc *p);
// extern void sched_detach_proc(struct proc *p);


/*
 * Scheduler policies
 */

extern const struct sched_ops *sched_ops;

extern void ias_sched_poll(uint64_t);
