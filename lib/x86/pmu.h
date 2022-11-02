#ifndef _X86_PMU_H_
#define _X86_PMU_H_

#include "processor.h"
#include "libcflat.h"

#define FIXED_CNT_INDEX 32
#define MAX_NUM_LBR_ENTRY	  32

/* Performance Counter Vector for the LVT PC Register */
#define PMI_VECTOR	32

#define DEBUGCTLMSR_LBR	  (1UL <<  0)

#define PMU_CAP_LBR_FMT	  0x3f
#define PMU_CAP_FW_WRITES	(1ULL << 13)

#define EVNSEL_EVENT_SHIFT	0
#define EVNTSEL_UMASK_SHIFT	8
#define EVNTSEL_USR_SHIFT	16
#define EVNTSEL_OS_SHIFT	17
#define EVNTSEL_EDGE_SHIFT	18
#define EVNTSEL_PC_SHIFT	19
#define EVNTSEL_INT_SHIFT	20
#define EVNTSEL_EN_SHIF		22
#define EVNTSEL_INV_SHIF	23
#define EVNTSEL_CMASK_SHIFT	24

#define EVNTSEL_EN	(1 << EVNTSEL_EN_SHIF)
#define EVNTSEL_USR	(1 << EVNTSEL_USR_SHIFT)
#define EVNTSEL_OS	(1 << EVNTSEL_OS_SHIFT)
#define EVNTSEL_PC	(1 << EVNTSEL_PC_SHIFT)
#define EVNTSEL_INT	(1 << EVNTSEL_INT_SHIFT)
#define EVNTSEL_INV	(1 << EVNTSEL_INV_SHIF)

struct pmu_caps {
	u8 version;
	u8 nr_fixed_counters;
	u8 fixed_counter_width;
	u8 nr_gp_counters;
	u8 gp_counter_width;
	u8 gp_counter_mask_length;
	u32 gp_counter_available;
	u64 perf_cap;
};

extern struct pmu_caps pmu;

void pmu_init(void);

static inline bool this_cpu_has_pmu(void)
{
	return !!pmu.version;
}

static inline bool this_cpu_has_perf_global_ctrl(void)
{
	return pmu.version > 1;
}

static inline bool pmu_gp_counter_is_available(int i)
{
	return pmu.gp_counter_available & BIT(i);
}

static inline u64 pmu_lbr_version(void)
{
	return pmu.perf_cap & PMU_CAP_LBR_FMT;
}

static inline bool pmu_has_full_writes(void)
{
	return pmu.perf_cap & PMU_CAP_FW_WRITES;
}

#endif /* _X86_PMU_H_ */