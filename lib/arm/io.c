/*
 * Each architecture must implement puts() and exit() with the I/O
 * devices exposed from QEMU, e.g. pl011 and chr-testdev. That's
 * what's done here, along with initialization functions for those
 * devices.
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <libcflat.h>
#include <devicetree.h>
#include <chr-testdev.h>
#include <config.h>
#include <asm/psci.h>
#include <asm/spinlock.h>
#include <asm/io.h>

#include "io.h"

static struct spinlock uart_lock;
/*
 * Use this guess for the uart base in order to make an attempt at
 * having earlier printf support. We'll overwrite it with the real
 * base address that we read from the device tree later. This is
 * the address we expect the virtual machine manager to put in
 * its generated device tree.
 */
#define UART_EARLY_BASE (u8 *)(unsigned long)CONFIG_UART_EARLY_BASE
static volatile u8 *uart0_base = UART_EARLY_BASE;

static void uart0_init_fdt(void)
{
	/*
	 * kvm-unit-tests uses the uart only for output. Both uart models have
	 * the TX register at offset 0 from the base address, so there is no
	 * need to treat them separately.
	 */
	const char *compatible[] = {"arm,pl011", "ns16550a"};
	struct dt_pbus_reg base;
	int i, ret;

	ret = dt_get_default_console_node();
	assert(ret >= 0 || ret == -FDT_ERR_NOTFOUND);

	if (ret == -FDT_ERR_NOTFOUND) {

		for (i = 0; i < ARRAY_SIZE(compatible); i++) {
			ret = dt_pbus_get_base_compatible(compatible[i], &base);
			assert(ret == 0 || ret == -FDT_ERR_NOTFOUND);

			if (ret == 0)
				break;
		}

		if (ret) {
			printf("%s: Compatible uart not found in the device tree, "
				"aborting...\n", __func__);
			abort();
		}

	} else {
		ret = dt_pbus_translate_node(ret, 0, &base);
		assert(ret == 0);
	}

	uart0_base = ioremap(base.addr, base.size);
}

#ifdef CONFIG_EFI

#include <acpi.h>

static void uart0_init_acpi(void)
{
	struct spcr_descriptor *spcr = find_acpi_table_addr(SPCR_SIGNATURE);

	assert_msg(spcr, "Unable to find ACPI SPCR");
	uart0_base = ioremap(spcr->serial_port.address, spcr->serial_port.bit_width);
}
#else

static void uart0_init_acpi(void)
{
	assert_msg(false, "ACPI not available");
}

#endif

void io_init(void)
{
	if (dt_available())
		uart0_init_fdt();
	else
		uart0_init_acpi();

	if (uart0_base != UART_EARLY_BASE) {
		printf("WARNING: early print support may not work. "
		       "Found uart at %p, but early base is %p.\n",
			uart0_base, UART_EARLY_BASE);
	}

	chr_testdev_init();
}

void puts(const char *s)
{
	spin_lock(&uart_lock);
	while (*s)
		writeb(*s++, uart0_base);
	spin_unlock(&uart_lock);
}

static int do_getchar(void)
{
	int c;

	spin_lock(&uart_lock);
	c = readb(uart0_base);
	spin_unlock(&uart_lock);

	return c ?: -1;
}

/*
 * Minimalist implementation for migration completion detection.
 * Without FIFOs enabled on the QEMU UART device we just read
 * the data register: we cannot read more than 16 characters.
 */
int __getchar(void)
{
	int c = do_getchar();
	static int count;

	if (c != -1)
		++count;

	assert(count < 16);

	return c;
}

/*
 * Defining halt to take 'code' as an argument guarantees that it will
 * be in x0/r0 when we halt. That gives us a final chance to see the exit
 * status while inspecting the halted unit test state.
 */
extern void halt(int code);

void exit(int code)
{
	/*
	 * Print the test return code in the following format which is
	 * consistent with powerpc and s390x. The runner can pick it
	 * up when chr-testdev is not present.
	 */
	printf("\nEXIT: STATUS=%d\n", ((code) << 1) | 1);

	chr_testdev_exit(code);
	psci_system_off();
	halt(code);
	__builtin_unreachable();
}
