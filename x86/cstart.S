
#include "apic-defs.h"

.global online_cpus
.altmacro

ipi_vector = 0x20

max_cpus = MAX_TEST_CPUS

.bss

	. = . + 4096 * max_cpus
	.align 16
stacktop:

.data

.align 4096
pt:
i = 0
        .rept 1024
        .long 0x1e7 | (i << 22)
        i = i + 1
        .endr
#define SEG_NULLASM                                             \
        .word 0, 0;                                             \
        .byte 0, 0, 0, 0

// The 0xC0 means the limit is in 4096-byte units
// and (for executable segments) 32-bit mode.
#define SEG_ASM(type,base,lim)                                  \
        .word (((lim) >> 12) & 0xffff), ((base) & 0xffff);      \
        .byte (((base) >> 16) & 0xff), (0x90 | (type)),         \
                (0xC0 | (((lim) >> 28) & 0xf)), (((base) >> 24) & 0xff)

#define STA_X     0x8       // Executable segment
#define STA_E     0x4       // Expand down (non-executable segments)
#define STA_C     0x4       // Conforming code segment (executable only)
#define STA_W     0x2       // Writeable (non-executable segments)
#define STA_R     0x2       // Readable (executable segments)
#define STA_A     0x1       // Accessed

gdt32:
	SEG_NULLASM	# null seg
	SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)	# code seg
	SEG_ASM(STA_W, 0x0, 0xffffffff)	# data seg

gdtdesc32:
	.word	(gdtdesc32 - gdt32 - 1)
	.long	gdt32

.section .init

mb_magic = 0x1BADB002
mb_flags = 0x0

	# multiboot header
	# .long mb_magic, mb_flags, 0 - (mb_magic + mb_flags)
mb_cmdline = 16

.macro SETA20
LOCAL seta20.1, seta20.2
seta20.1:
	inb	$0x64,%al
	testb	$0x2,%al
	jnz	seta20.1 

	movb	$0xd1,%al
	outb	%al,$0x64

seta20.2:
	inb	$0x64,%al
	testb	$0x2,%al
	jnz	seta20.2

	movb	$0xdf,%al
	outb	%al,$0x60
.endm


.macro setup_tr_and_percpu
	call load_idt
	push %esp
	call setup_tss
	addl $4, %esp
	ltr %ax
	add $(max_cpus * 8), %ax
	mov %ax, %gs
.endm

.macro setup_segments
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss
.endm

.code16gcc
.globl start
start:
		SETA20
		lgdt	gdtdesc32
		movl	%cr0, %eax
		orl	$0x00000001, %eax
		movl	%eax, %cr0
		ljmp	$0x08, $start_32
.code32
start_32:
        setup_segments
        mov $stacktop, %esp

        push %ebx
        # call setup_multiboot
        addl $4, %esp
        call setup_libcflat
        mov mb_cmdline(%ebx), %eax
        mov %eax, __args
        call __setup_args
        call prepare_32
        jmpl $8, $start32

prepare_32:
	mov $(1 << 4), %eax // pse
	mov %eax, %cr4

	mov $pt, %eax
	mov %eax, %cr3

	mov %cr0, %eax
	bts $0, %eax
	bts $31, %eax
	mov %eax, %cr0
	ret

smp_stacktop:	.long stacktop - 4096

save_id:
	movl $(APIC_DEFAULT_PHYS_BASE + APIC_ID), %eax
	movl (%eax), %eax
	shrl $24, %eax
	lock btsl %eax, online_cpus
	retl

ap_start32:
	setup_segments
	mov $-4096, %esp
	lock xaddl %esp, smp_stacktop
	setup_tr_and_percpu
	call prepare_32
	call reset_apic
	call save_id
	call enable_apic
	call enable_x2apic
	call ap_online

	/* ap_online() should never return */
	ud2

start32:
	# setup_tr_and_percpu
	call reset_apic
	call save_id
	call mask_pic_interrupts
	call enable_apic
	# call bsp_rest_init
        push $__environ
        push $__argv
        push __argc
        call main
	push %eax
	call exit

#include "trampolines.S"
