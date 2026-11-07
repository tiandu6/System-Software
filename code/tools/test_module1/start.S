# Test-only RV64 S-mode harness; repository trapentry.S is linked unchanged.
#include <riscv.h>
    .section .text.entry
    .globl _start
    .option norelax
_start:
    csrw sie, zero
    csrci sstatus, 2
    csrw satp, zero
    sfence.vma
    la sp, test_stack_top
    la t0, __bss_start
    la t1, __bss_end
.Lclear:
    bgeu t0, t1, .Lcleared
    sd zero, 0(t0)
    addi t0, t0, 8
    j .Lclear
.Lcleared:
    csrw sscratch, zero
    la t0, __alltraps
    csrw stvec, t0
    call test_boot

    .macro LOAD_SENTINELS
    li x1, 0x100000001
    li x3, 0x100000003
    li x4, 0x100000004
    li x5, 0x100000005
    li x6, 0x100000006
    li x7, 0x100000007
    li x8, 0x100000008
    li x9, 0x100000009
    li x10, 0x10000000a
    li x11, 0x10000000b
    li x12, 0x10000000c
    li x13, 0x10000000d
    li x14, 0x10000000e
    li x15, 0x10000000f
    li x16, 0x100000010
    li x17, 0x100000011
    li x18, 0x100000012
    li x19, 0x100000013
    li x20, 0x100000014
    li x21, 0x100000015
    li x22, 0x100000016
    li x23, 0x100000017
    li x24, 0x100000018
    li x25, 0x100000019
    li x26, 0x10000001a
    li x27, 0x10000001b
    li x28, 0x10000001c
    li x29, 0x10000001d
    li x30, 0x10000001e
    li x31, 0x10000001f
    .endm

    .macro SNAPSHOT_AND_CHECK
    addi sp, sp, -32 * REGBYTES
    STORE x5, 5 * REGBYTES(sp)
    addi x5, sp, 32 * REGBYTES
    STORE x5, 2 * REGBYTES(sp)
    STORE x0, 0 * REGBYTES(sp)
    STORE x1, 1 * REGBYTES(sp)
    STORE x3, 3 * REGBYTES(sp)
    STORE x4, 4 * REGBYTES(sp)
    STORE x6, 6 * REGBYTES(sp)
    STORE x7, 7 * REGBYTES(sp)
    STORE x8, 8 * REGBYTES(sp)
    STORE x9, 9 * REGBYTES(sp)
    STORE x10, 10 * REGBYTES(sp)
    STORE x11, 11 * REGBYTES(sp)
    STORE x12, 12 * REGBYTES(sp)
    STORE x13, 13 * REGBYTES(sp)
    STORE x14, 14 * REGBYTES(sp)
    STORE x15, 15 * REGBYTES(sp)
    STORE x16, 16 * REGBYTES(sp)
    STORE x17, 17 * REGBYTES(sp)
    STORE x18, 18 * REGBYTES(sp)
    STORE x19, 19 * REGBYTES(sp)
    STORE x20, 20 * REGBYTES(sp)
    STORE x21, 21 * REGBYTES(sp)
    STORE x22, 22 * REGBYTES(sp)
    STORE x23, 23 * REGBYTES(sp)
    STORE x24, 24 * REGBYTES(sp)
    STORE x25, 25 * REGBYTES(sp)
    STORE x26, 26 * REGBYTES(sp)
    STORE x27, 27 * REGBYTES(sp)
    STORE x28, 28 * REGBYTES(sp)
    STORE x29, 29 * REGBYTES(sp)
    STORE x30, 30 * REGBYTES(sp)
    STORE x31, 31 * REGBYTES(sp)
    mv a0, sp
    call test_return
    addi sp, sp, 32 * REGBYTES
    .endm

    li a0, 1
    la a1, .Lfault16
    la a2, .Lresume16
    call test_prepare
    LOAD_SENTINELS
.Lfault16:
    .2byte 0x9002
.Lresume16:
    SNAPSHOT_AND_CHECK

    li a0, 2
    la a1, .Lfault32
    la a2, .Lresume32
    call test_prepare
    LOAD_SENTINELS
.Lfault32:
    .4byte 0x00100073
.Lresume32:
    SNAPSHOT_AND_CHECK

    li a0, 3
    la a1, .Lillegal32
    la a2, .Lresume_illegal32
    call test_prepare
    LOAD_SENTINELS
.Lillegal32:
    .4byte 0x0000007b
.Lresume_illegal32:
    SNAPSHOT_AND_CHECK
    call test_done
.Lhang:
    wfi
    j .Lhang

    # The production entry calls this test-only trap bridge.
    # After C inspection, deliberately corrupt every writable GPR except sp/ra,
    # so RESTORE_ALL must recover them instead of relying on the C ABI.
    .globl trap
    .align 2
trap:
    addi sp, sp, -16
    sd ra, 8(sp)
    call test_trap
    csrw sepc, zero
    li x3, 0x200000003
    li x4, 0x200000004
    li x5, 0x200000005
    li x6, 0x200000006
    li x7, 0x200000007
    li x8, 0x200000008
    li x9, 0x200000009
    li x10, 0x20000000a
    li x11, 0x20000000b
    li x12, 0x20000000c
    li x13, 0x20000000d
    li x14, 0x20000000e
    li x15, 0x20000000f
    li x16, 0x200000010
    li x17, 0x200000011
    li x18, 0x200000012
    li x19, 0x200000013
    li x20, 0x200000014
    li x21, 0x200000015
    li x22, 0x200000016
    li x23, 0x200000017
    li x24, 0x200000018
    li x25, 0x200000019
    li x26, 0x20000001a
    li x27, 0x20000001b
    li x28, 0x20000001c
    li x29, 0x20000001d
    li x30, 0x20000001e
    li x31, 0x20000001f
    ld ra, 8(sp)
    addi sp, sp, 16
    ret

    .section .bss
    .align 4
test_stack:
    .space 16384
    .globl test_stack_top
test_stack_top:
