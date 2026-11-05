#include <assert.h>
#include <clock.h>
#include <defs.h>
#include <riscv.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>

extern void interrupt_handler(struct trapframe *tf);
extern void exception_handler(struct trapframe *tf);

void test_module2(void) {
    /* Real exceptions must return through the production assembly entry. */
    asm volatile(".balign 4\n.half 0x9002");
    cprintf("[M2] 16-bit c.ebreak returned: PASS\n");
    asm volatile(".balign 4\n.word 0x00100073");
    cprintf("[M2] 32-bit ebreak returned: PASS\n");
    asm volatile(".balign 4\n.half 0x0000");
    cprintf("[M2] 16-bit illegal instruction returned: PASS\n");
    asm volatile(".balign 4\n.word 0x0000007b");
    cprintf("[M2] 32-bit illegal instruction returned: PASS\n");

    struct trapframe tf, before;
    memset(&tf, 0, sizeof(tf));
    tf.epc = (uintptr_t)&test_module2;
    uintptr_t interrupt_bit = (uintptr_t)1 << 63;
    size_t ticks_before = ticks;
    const uintptr_t causes[] = {IRQ_S_SOFT | interrupt_bit, 15, 15 | interrupt_bit};
    for (size_t i = 0; i < sizeof(causes) / sizeof(causes[0]); i++) {
        tf.cause = causes[i];
        before = tf;
        interrupt_handler(&tf);
        assert(memcmp(&tf, &before, sizeof(tf)) == 0);
        assert(ticks == ticks_before);
    }
    cprintf("[M2] software/unknown IRQs preserve frame and ticks: PASS\n");

    tf.cause = 20;
    before = tf;
    exception_handler(&tf);
    assert(memcmp(&tf, &before, sizeof(tf)) == 0);
    cprintf("[M2] unknown exception preserves EPC and frame: PASS\n");

    /* A 32-bit instruction may begin at an address only aligned to 2 bytes. */
    const uint16_t instructions[] = {0x0000, 0x007b, 0x0000};
    tf.cause = CAUSE_ILLEGAL_INSTRUCTION;
    tf.epc = (uintptr_t)&instructions[1];
    before = tf;
    exception_handler(&tf);
    before.epc += 4;
    assert(memcmp(&tf, &before, sizeof(tf)) == 0);
    cprintf("[M2] halfword-aligned 32-bit EPC advance: PASS\n");
    cprintf("ALL MODULE2 SUPPLEMENTAL TESTS PASSED\n");
    sbi_shutdown();
    while (1) {}
}
