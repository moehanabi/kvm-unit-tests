#include "pmu.h"

struct pmu_caps pmu;

void pmu_init(void)
{
	struct cpuid cpuid_10 = cpuid(10);

	pmu.version = cpuid_10.a & 0xff;

	if (pmu.version > 1) {
		pmu.nr_fixed_counters = cpuid_10.d & 0x1f;
		pmu.fixed_counter_width = (cpuid_10.d >> 5) & 0xff;
	}

	pmu.nr_gp_counters = (cpuid_10.a >> 8) & 0xff;
	pmu.gp_counter_width = (cpuid_10.a >> 16) & 0xff;
	pmu.gp_counter_mask_length = (cpuid_10.a >> 24) & 0xff;

	/* CPUID.0xA.EBX bit is '1' if a counter is NOT available. */
	pmu.gp_counter_available = ~cpuid_10.b;

	if (this_cpu_has(X86_FEATURE_PDCM))
		pmu.perf_cap = rdmsr(MSR_IA32_PERF_CAPABILITIES);
}