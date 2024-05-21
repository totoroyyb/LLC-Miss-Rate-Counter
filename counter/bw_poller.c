#include <stdio.h>
#include <stdint.h>

#include <base/stddef.h>
#include <base/limits.h>
#include <base/log.h>

#include "defs.h"
#include "sched.h"
#include "ksched.h"
#include "pmc.h"

#define IAS_POLL_INTERVAL_US		100000

/* the current time in microseconds */
uint64_t now_us;
uint64_t ias_bw_sample_failures;
float	 ias_bw_estimate;
float	 ias_bw_estimate_multiplier;

/* bandwidth threshold in cache lines per cycle for a single channel */
// static float ias_bw_thresh;

struct pmc_sample {
	uint64_t gen;
	uint64_t val;
	uint64_t tsc;
};

enum {
	IAS_BW_STATE_RELAX = 0,
	IAS_BW_STATE_SAMPLE,
	IAS_BW_STATE_PUNISH,
};

static struct pmc_sample arr_1[NCPU], arr_2[NCPU];

static void ias_bw_request_pmc(uint64_t sel, struct pmc_sample *samples)
{
	struct ias_data *sd;
	int core, tmp;

	sched_for_each_allowed_core(core, tmp) {
		// sd = cores[core];
		// if (!sd) continue;
		// if (!sd || sd->is_lc ||
		//     bitmap_test(ias_ht_punished_cores, core)) {
		// 	samples[core].gen = ias_gen[core] - 1;
		// 	continue;
		// }

		// samples[core].gen = ias_gen[core];
		ksched_enqueue_pmc(core, sel);
	}
}

static void ias_bw_gather_pmc(struct pmc_sample *samples)
{
	int core, tmp;
	struct pmc_sample *s;

	sched_for_each_allowed_core(core, tmp) {
		s = &samples[core];
		// if (s->gen != ias_gen[core])
		// 	continue;
		if (!ksched_poll_pmc(core, &s->val, &s->tsc)) {
			// s->gen = ias_gen[core] - 1;
			ias_bw_sample_failures++;
			continue;
		}
	}
}

static float ias_measure_bw_mem_ctrl(void)
{
	static uint64_t last_tsc = 0;
	static uint32_t last_cas = 0;
	uint64_t tsc;
	uint32_t cur_cas;
	float bw_estimate;

	/* update the bandwidth estimate */
	barrier();
	tsc = rdtsc();
	barrier();
	cur_cas = pcm_caladan_get_cas_count(0);
	bw_estimate = (float)(cur_cas - last_cas) / (float)(tsc - last_tsc);
	last_cas = cur_cas;
	last_tsc = tsc;
	ias_bw_estimate = bw_estimate;
	return bw_estimate;
}

static float cores[NCPU];

void ias_estimate_bw(struct pmc_sample *start, struct pmc_sample *end) {
	float highest_l3miss_rate = 0.0, bw_estimate;
	int core, tmp;
	sched_for_each_allowed_core(core, tmp) {
		// if (cores[core] == NULL ||
		//     start[core].gen != end[core].gen ||
		//     start[core].gen != ias_gen[core]) {
		// 	continue;
		// }

		float miss_count = (float)(end[core].val - start[core].val);
		bw_estimate = miss_count / (float)(end[core].tsc - start[core].tsc);
		cores[core] = bw_estimate;
		// cores[core]->bw_llc_miss_rate += bw_estimate;
	}


	for (int i = 0; i < sched_cores_nr; i++) {
		core = sched_cores_tbl[i];
		log_info("NOW: %llu | Core #%d - miss rate = %.5f", now_us, core, cores[core]);
	}
}

/**
 * ias_bw_poll - runs the bandwidth controller
 */
void ias_bw_poll(void)
{
	static struct pmc_sample *start = arr_1, *end = arr_2;
	static int state;

	/* run the state machine */
	switch (state) {
	case IAS_BW_STATE_RELAX:
        ias_bw_request_pmc(PMC_LLC_MISSES, start);
        state = IAS_BW_STATE_SAMPLE;
		break;
	case IAS_BW_STATE_SAMPLE:
		state = IAS_BW_STATE_PUNISH;
		ias_bw_gather_pmc(start);
		ias_bw_request_pmc(PMC_LLC_MISSES, end);
		break;
	case IAS_BW_STATE_PUNISH:
		ias_bw_gather_pmc(end);
		// if (!throttle) {
			// ias_bw_sample_aborts++;
			// state = IAS_BW_STATE_RELAX;
			// break;
		// }
		// ias_bw_punish(start, end);
		ias_estimate_bw(start, end);
		swapvars(start, end);
		ias_bw_request_pmc(PMC_LLC_MISSES, end);
		break;

	default:
		panic("ias: invalid bw state");
	}
}

void ias_sched_poll(uint64_t now) {
	static uint64_t last_us;
	now_us = now;

	/* try to run the subcontroller polling stages */
	if (now - last_us >= IAS_POLL_INTERVAL_US) {
		log_info("start bw polling...");
		last_us = now;
		ias_bw_poll();
	}
}

void ias_bw_init(void) {
	int ret;
	unsigned int nr_channels;
	struct cpuid_info regs;
	const char *intel_cpu_str = "GenuineIntel";
	int namebytes[3];

	cpuid(0, 0, &regs);
	namebytes[0] = regs.ebx;
	namebytes[1] = regs.edx;
	namebytes[2] = regs.ecx;

	if (memcmp(namebytes, intel_cpu_str, strlen(intel_cpu_str))) {
		log_warn("Detected non-Intel CPU. Disabling memory bandwidth monitoring!");
		return 0;
	}

	cpuid(1, 0, &regs);
	if (regs.ecx & (1UL << 31UL)) {
		log_warn("Detected CPU virtualization. Disabling memory bandwidth monitoring!");
		return 0;
	}

	/* ensure threads created by pcm are pinned to control core */
	pin_thread(0, sched_ctrl_core);

	ret = pcm_caladan_init(0);
	if (ret)
		return ret;

	/* We monitor 1 channel, so multiply measurements by nr_channels to estimate real bw */
	nr_channels = pcm_caladan_get_active_channel_count();
	if (nr_channels == 0)
		return -EINVAL;

	log_info("Detected nr memory channels = %d", nr_channels);
	log_info("Detected cycles per us = %d", cycles_per_us);
	log_info("Detected cache line size = %d", CACHE_LINE_SIZE);

	// /* Use default limit if none supplied */
	// if (!cfg.ias_bw_limit)
	// 	cfg.ias_bw_limit = IAS_BW_LIMIT;

	/* Compute the multiplier to convert cache lines/cycle to bytes/us (= MB/s) */
	ias_bw_estimate_multiplier = cycles_per_us * nr_channels * CACHE_LINE_SIZE;

	log_info("bw estimate multiplier = %.2f", ias_bw_estimate_multiplier);

	/* convert from MB/s to per channel cache line/cycle */
	// ias_bw_thresh = cfg.ias_bw_limit / ias_bw_estimate_multiplier;

	return 0;
}
