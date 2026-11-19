/* Standalone validation fixture, not a Lab3 module-two implementation. */
#include <defs.h>
#include <trap.h>
#include <riscv.h>

extern char test_stack_top[];
static uintptr_t current_case, expected_epc, resume_epc;
static uintptr_t trap_count, return_count;
#define SIE_BIT ((uintptr_t)1 << 1)
#define SPIE_BIT ((uintptr_t)1 << 5)
#define SPP_BIT ((uintptr_t)1 << 8)
#define MXR_BIT ((uintptr_t)1 << 19)

static void putch(char c) {
    volatile unsigned char *uart = (volatile unsigned char *)0x10000000;
    while ((uart[5] & 0x20) == 0) {}
    uart[0] = c;
}
static void puts_(const char *s) {
    while (*s) {
        if (*s == '\n') putch('\r');
        putch(*s++);
    }
}
static void hex_(uintptr_t n) {
    puts_("0x");
    for (int i = 60; i >= 0; i -= 4)
        putch("0123456789abcdef"[(n >> i) & 15]);
}
static void finish(void) __attribute__((noreturn));
static void finish(void) {
    /* Legacy SBI shutdown supplied by QEMU's OpenSBI. */
    register uintptr_t a7 asm("a7") = 8;
    register uintptr_t a0 asm("a0") = 0;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    for (;;) asm volatile("wfi");
}
static void fail(const char *what, uintptr_t index,
                 uintptr_t got, uintptr_t want) __attribute__((noreturn));
static void fail(const char *what, uintptr_t index,
                 uintptr_t got, uintptr_t want) {
    puts_("[MODULE1 FAIL] case="); hex_(current_case);
    puts_(" "); puts_(what); puts_(" index="); hex_(index);
    puts_(" got="); hex_(got); puts_(" want="); hex_(want);
    puts_("\n");
    finish();
}
static uintptr_t expected_reg(unsigned i) {
    if (i == 0) return 0;
    if (i == 2) return (uintptr_t)test_stack_top;
    return 0x100000000ULL + i;
}
void test_boot(void) {
    puts_("Module1 standalone QEMU harness: actual repository trapentry.S\n");
    puts_("RV64 / S mode / no paging / no module-two handlers\n");
}
void test_prepare(uintptr_t number, uintptr_t fault, uintptr_t resume) {
    current_case = number;
    expected_epc = fault;
    resume_epc = resume;
    if ((number == 1 ? 2 : 4) != resume - fault)
        fail("test instruction length", 0, resume - fault, number == 1 ? 2 : 4);
    clear_csr(sstatus, MXR_BIT | SIE_BIT);
    if (number == 2) set_csr(sstatus, SIE_BIT);
}
void test_trap(struct trapframe *tf) {
    uintptr_t expected_tf = (uintptr_t)test_stack_top - sizeof(*tf);
    if ((uintptr_t)tf != expected_tf)
        fail("frame argument/base", 0, (uintptr_t)tf, expected_tf);
    if (((uintptr_t)tf & 15) != 0)
        fail("frame alignment", 0, (uintptr_t)tf & 15, 0);
    uintptr_t *gpr = (uintptr_t *)&tf->gpr;
    for (unsigned i = 0; i < 32; ++i)
        if (gpr[i] != expected_reg(i))
            fail("saved register", i, gpr[i], expected_reg(i));
    if (tf->epc != expected_epc)
        fail("saved sepc", 0, tf->epc, expected_epc);
    uintptr_t cause = current_case == 3 ? 2 : 3;
    if (tf->cause != cause)
        fail("saved scause", 0, tf->cause, cause);
    uintptr_t wanted_status = SPP_BIT | (current_case == 2 ? SPIE_BIT : 0);
    if ((tf->status & (SPP_BIT | SPIE_BIT | SIE_BIT)) != wanted_status)
        fail("saved sstatus", 0,
             tf->status & (SPP_BIT | SPIE_BIT | SIE_BIT), wanted_status);
    if (tf->badvaddr != read_csr(sbadaddr))
        fail("saved stval", 0, tf->badvaddr, read_csr(sbadaddr));
    if (read_csr(sscratch) != 0)
        fail("sscratch cleared", 0, read_csr(sscratch), 0);
    ++trap_count;
    /* The handler updates the SAME frame; RESTORE_ALL must consume changes. */
    tf->epc = resume_epc;
    if (current_case != 2) tf->status |= MXR_BIT;
    else tf->status &= ~MXR_BIT;
    puts_("[MODULE1 PASS] trap frame layout, saved x0-x31, CSR snapshots, a0=tf; case ");
    putch('0' + current_case); puts_("\n");
}
void test_return(uintptr_t *gpr) {
    for (unsigned i = 0; i < 32; ++i)
        if (gpr[i] != expected_reg(i))
            fail("restored register", i, gpr[i], expected_reg(i));
    uintptr_t got_status = read_csr(sstatus);
    /* Check software-restored fields and the SPP transition.  Do not add an
       assertion about the implementation's post-sret SPIE value here. */
    uintptr_t wanted = current_case == 2 ? SIE_BIT : MXR_BIT;
    uintptr_t mask = SIE_BIT | SPP_BIT | MXR_BIT;
    if ((got_status & mask) != wanted)
        fail("restored sstatus / sret", 0, got_status & mask, wanted);
    if (read_csr(sepc) != resume_epc)
        fail("restored handler-updated sepc", 0, read_csr(sepc), resume_epc);
    ++return_count;
    puts_("[MODULE1 PASS] x1-x31 restored after deliberate clobber; original sp, edited EPC/status and sret; case ");
    putch('0' + current_case); puts_("\n");
}
void test_done(void) {
    if (trap_count != 3 || return_count != 3)
        fail("trap/return counts", trap_count, return_count, 3);
    puts_("[MODULE1 PASS] 16-bit breakpoint, 32-bit breakpoint, 32-bit illegal instruction returned\n");
    puts_("[MODULE1 PASS] 3/3 standalone round trips; ALL MODULE1 HARNESS TESTS PASSED\n");
    puts_("Scope: module one only; NOT full Lab3 grading or module-two validation.\n");
    finish();
}
