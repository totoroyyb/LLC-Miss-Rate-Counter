/*
 * sched.c - low-level scheduler routines (e.g. adding and preempting cores)
 */

#include <stdio.h>

#include <base/stddef.h>
#include <base/assert.h>
#include <base/time.h>
#include <base/bitmap.h>
#include <base/log.h>
#include <base/cpu.h>

#include "defs.h"
#include "sched.h"
#include "ksched.h"

#define PROC_TIMER_WHEEL_THRESH_US 100
#define NOHOT false

/* a bitmap of cores available to be allocated by the scheduler */
DEFINE_BITMAP(sched_allowed_cores, NCPU);

/* maps each cpu number to the cpu number of its hyperthread buddy */
unsigned int sched_siblings[NCPU];

/* core assignments */
unsigned int sched_dp_core;	/* used for the iokernel's dataplane */
unsigned int sched_ctrl_core;	/* used for the iokernel's controlplane */

/* keeps track of which cores are in each NUMA socket */
struct socket socket_state[NNUMA];
int managed_numa_node;

/* arrays of core numbers for fast polling */
unsigned int sched_cores_tbl[NCPU];
int sched_cores_nr;

static int nr_guaranteed;

LIST_HEAD(poll_list);

struct core_state {
	struct thread	*last_th;     /* recently run thread, waiting for preemption to complete */
	struct thread	*pending_th;  /* a thread waiting run */
	struct thread	*cur_th;      /* the currently running thread */
	unsigned int	idle:1;	      /* is the core idle? */
	unsigned int	pending:1;    /* the next run is waiting */
	unsigned int	wait:1;       /* waiting for run to finish */
};

/* a per-CPU state table to manage scheduling operations */
static struct core_state state[NCPU];
/* policy-specific operations (TODO: should be made configurable) */
const struct sched_ops *sched_ops;

/* current hardware timestamp */
static uint64_t cur_tsc;

/**
 * sched_poll - advance the scheduler during each poll loop iteration
 */
void sched_poll(void)
{
	static uint64_t last_time;
	DEFINE_BITMAP(idle, NCPU);
	struct core_state *s;
	uint64_t now;
	int i, core, idle_cnt = 0;
	struct proc *p, *p_next;

	/*
	 * slow pass --- runs every IOKERNEL_POLL_INTERVAL
	 */

	cur_tsc = rdtsc();
	now = (cur_tsc - start_tsc) / cycles_per_us;
	if (cur_tsc - last_time >= IOKERNEL_POLL_INTERVAL * cycles_per_us) {
		last_time = cur_tsc;
	}

	/*
	 * fast pass --- runs every poll loop
	 */

	ias_sched_poll(now);
	ksched_send_intrs();
}

static int sched_scan_node(int node)
{
	struct cpu_info *info;
	int i, sib, nr, ret = 0;

	for (i = 0; i < cpu_count; i++) {
		info = &cpu_info_tbl[i];
		if (info->package != node)
			continue;

		bitmap_set(socket_state[node].cores, i);

		/* TODO: can only support hyperthread pairs */
		// nr = bitmap_popcount(info->thread_siblings_mask, NCPU);
		// if (nr != 2) {
		// 	if (nr > 2)
		// 		ret = -EINVAL;
		// 	if (nr == 1 && !cfg.noht)  {
		// 		log_err("HT not detected. Please run again with noht option");
		// 		ret = -EINVAL;
		// 	}
		// }

		sib = bitmap_find_next_set(info->thread_siblings_mask,
					   NCPU, 0);
		if (sib == i)
			sib = bitmap_find_next_set(info->thread_siblings_mask,
						   NCPU, sib + 1);

		sched_siblings[i] = sib;
		if (i < sib) {
			if (sib != NCPU)
				printf("[%d,%d]", i, sib);
			else
				printf("[%d]", i);
		}
	}

	return ret;
}

/**
 * sched_init - the global initializer for the scheduler
 *
 * Returns 0 if successful, otherwise fail.
 */
int sched_init(void)
{
	int i;
	bool valid = true;

	bitmap_init(sched_allowed_cores, cpu_count, false);

	/*
	 * first pass: scan and log CPUs
	 */

	log_info("sched: CPU configuration...");
	for (i = 0; i < numa_count; i++) {
		printf("\tnode %d: ", i);
		if (sched_scan_node(i) != 0)
			valid = false;
		printf("\n");
		fflush(stdout);
	}
	if (!valid)
		return -EINVAL;

	/*
	 * second pass: determine available CPUs
	 */

	for (i = 0; i < cpu_count; i++) {
		// if (cpu_info_tbl[i].package != managed_numa_node && sched_ops != &numa_ops)
		// 	continue;

		// if (allowed_cores_supplied &&
		//     !bitmap_test(input_allowed_cores, i))
		// 	continue;

		bitmap_set(sched_allowed_cores, i);
	}
	/* check for minimum number of cores required */
	// i = bitmap_popcount(sched_allowed_cores, NCPU);
	// if (i < 3 + !cfg.noht) {
	// 	log_err("sched: %d is not enough cores\n", i);
	// 	return -EINVAL;
	// }

	/*
	 * third pass: reserve cores for iokernel and system
	 */

	sched_ctrl_core = bitmap_find_next_set(sched_allowed_cores, NCPU, 0);
	if (NOHOT)
		sched_dp_core = bitmap_find_next_set(sched_allowed_cores, NCPU, sched_ctrl_core + 1);
	else
		sched_dp_core = sched_siblings[sched_ctrl_core];
	bitmap_clear(sched_allowed_cores, sched_ctrl_core);
	bitmap_clear(sched_allowed_cores, sched_dp_core);
	log_info("sched: dataplane on %d, control on %d",
		 sched_dp_core, sched_ctrl_core);

	/* check if configuration disables hyperthreads */
	if (NOHOT) {
		for (i = 0; i < NCPU; i++) {
			if (!bitmap_test(sched_allowed_cores, i))
				continue;

			if (sched_siblings[i] == NCPU)
				continue;

			bitmap_clear(sched_allowed_cores, sched_siblings[i]);
		}
	}

	/* generate polling arrays */
	bitmap_for_each_set(sched_allowed_cores, NCPU, i)
		sched_cores_tbl[sched_cores_nr++] = i;

	return 0;
}
