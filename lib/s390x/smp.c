/*
 * s390x smp
 * Based on Linux's arch/s390/kernel/smp.c and
 * arch/s390/include/asm/sigp.h
 *
 * Copyright (c) 2019 IBM Corp
 *
 * Authors:
 *  Janosch Frank <frankja@linux.ibm.com>
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2.
 */
#include <libcflat.h>
#include <asm/arch_def.h>
#include <asm/sigp.h>
#include <asm/page.h>
#include <asm/barrier.h>
#include <asm/spinlock.h>
#include <asm/asm-offsets.h>

#include <alloc.h>
#include <alloc_page.h>

#include "smp.h"
#include "sclp.h"

static char cpu_info_buffer[PAGE_SIZE] __attribute__((__aligned__(4096)));
static struct cpu *cpus;
static struct cpu *cpu0;
static struct spinlock lock;

extern void smp_cpu_setup_state(void);

int smp_query_num_cpus(void)
{
	struct ReadCpuInfo *info = (void *)cpu_info_buffer;
	return info->nr_configured;
}

struct cpu *smp_cpu_from_addr(uint16_t addr)
{
	int i, num = smp_query_num_cpus();

	for (i = 0; i < num; i++) {
		if (cpus[i].addr == addr)
			return &cpus[i];
	}
	return NULL;
}

bool smp_cpu_stopped(uint16_t addr)
{
	uint32_t status;

	if (sigp(addr, SIGP_SENSE, 0, &status) != SIGP_CC_STATUS_STORED)
		return false;
	return !!(status & (SIGP_STATUS_CHECK_STOP|SIGP_STATUS_STOPPED));
}

bool smp_cpu_running(uint16_t addr)
{
	if (sigp(addr, SIGP_SENSE_RUNNING, 0, NULL) != SIGP_CC_STATUS_STORED)
		return true;
	/* Status stored condition code is equivalent to cpu not running. */
	return false;
}

static int smp_cpu_stop_nolock(uint16_t addr, bool store)
{
	struct cpu *cpu;
	uint8_t order = store ? SIGP_STOP_AND_STORE_STATUS : SIGP_STOP;

	cpu = smp_cpu_from_addr(addr);
	if (!cpu || cpu == cpu0)
		return -1;

	if (sigp_retry(addr, order, 0, NULL))
		return -1;

	while (!smp_cpu_stopped(addr))
		mb();
	cpu->active = false;
	return 0;
}

int smp_cpu_stop(uint16_t addr)
{
	int rc;

	spin_lock(&lock);
	rc = smp_cpu_stop_nolock(addr, false);
	spin_unlock(&lock);
	return rc;
}

int smp_cpu_stop_store_status(uint16_t addr)
{
	int rc;

	spin_lock(&lock);
	rc = smp_cpu_stop_nolock(addr, true);
	spin_unlock(&lock);
	return rc;
}

int smp_cpu_restart(uint16_t addr)
{
	int rc = -1;
	struct cpu *cpu;

	spin_lock(&lock);
	cpu = smp_cpu_from_addr(addr);
	if (cpu) {
		rc = sigp(addr, SIGP_RESTART, 0, NULL);
		cpu->active = true;
	}
	spin_unlock(&lock);
	return rc;
}

int smp_cpu_start(uint16_t addr, struct psw psw)
{
	int rc = -1;
	struct cpu *cpu;
	struct lowcore *lc;

	spin_lock(&lock);
	cpu = smp_cpu_from_addr(addr);
	if (cpu) {
		lc = cpu->lowcore;
		lc->restart_new_psw.mask = psw.mask;
		lc->restart_new_psw.addr = psw.addr;
		rc = sigp(addr, SIGP_RESTART, 0, NULL);
	}
	spin_unlock(&lock);
	return rc;
}

int smp_cpu_destroy(uint16_t addr)
{
	struct cpu *cpu;
	int rc;

	spin_lock(&lock);
	rc = smp_cpu_stop_nolock(addr, false);
	if (!rc) {
		cpu = smp_cpu_from_addr(addr);
		free_pages(cpu->lowcore, 2 * PAGE_SIZE);
		free_pages(cpu->stack, 4 * PAGE_SIZE);
		cpu->lowcore = (void *)-1UL;
		cpu->stack = (void *)-1UL;
	}
	spin_unlock(&lock);
	return rc;
}

int smp_cpu_setup(uint16_t addr, struct psw psw)
{
	struct lowcore *lc;
	struct cpu *cpu;
	int rc = -1;

	spin_lock(&lock);

	if (!cpus)
		goto out;

	cpu = smp_cpu_from_addr(addr);

	if (!cpu || cpu->active)
		goto out;

	sigp_retry(cpu->addr, SIGP_INITIAL_CPU_RESET, 0, NULL);

	lc = alloc_pages(1);
	cpu->lowcore = lc;
	memset(lc, 0, PAGE_SIZE * 2);
	sigp_retry(cpu->addr, SIGP_SET_PREFIX, (unsigned long )lc, NULL);

	/* Copy all exception psws. */
	memcpy(lc, cpu0->lowcore, 512);

	/* Setup stack */
	cpu->stack = (uint64_t *)alloc_pages(2);

	/* Start without DAT and any other mask bits. */
	cpu->lowcore->sw_int_grs[14] = psw.addr;
	cpu->lowcore->sw_int_grs[15] = (uint64_t)cpu->stack + (PAGE_SIZE * 4);
	lc->restart_new_psw.mask = 0x0000000180000000UL;
	lc->restart_new_psw.addr = (uint64_t)smp_cpu_setup_state;
	lc->sw_int_crs[0] = 0x0000000000040000UL;

	/* Start processing */
	rc = sigp_retry(cpu->addr, SIGP_RESTART, 0, NULL);
	if (!rc)
		cpu->active = true;

out:
	spin_unlock(&lock);
	return rc;
}

/*
 * Disregarding state, stop all cpus that once were online except for
 * calling cpu.
 */
void smp_teardown(void)
{
	int i = 0;
	uint16_t this_cpu = stap();
	struct ReadCpuInfo *info = (void *)cpu_info_buffer;

	spin_lock(&lock);
	for (; i < info->nr_configured; i++) {
		if (cpus[i].active &&
		    cpus[i].addr != this_cpu) {
			sigp_retry(cpus[i].addr, SIGP_STOP, 0, NULL);
		}
	}
	spin_unlock(&lock);
}

/*Expected to be called from boot cpu */
extern uint64_t *stackptr;
void smp_setup(void)
{
	int i = 0;
	unsigned short cpu0_addr = stap();
	struct ReadCpuInfo *info = (void *)cpu_info_buffer;

	spin_lock(&lock);
	sclp_mark_busy();
	info->h.length = PAGE_SIZE;
	sclp_service_call(SCLP_READ_CPU_INFO, cpu_info_buffer);

	if (smp_query_num_cpus() > 1)
		printf("SMP: Initializing, found %d cpus\n", info->nr_configured);

	cpus = calloc(info->nr_configured, sizeof(cpus));
	for (i = 0; i < info->nr_configured; i++) {
		cpus[i].addr = info->entries[i].address;
		cpus[i].active = false;
		if (info->entries[i].address == cpu0_addr) {
			cpu0 = &cpus[i];
			cpu0->stack = stackptr;
			cpu0->lowcore = (void *)0;
			cpu0->active = true;
		}
	}
	spin_unlock(&lock);
}
