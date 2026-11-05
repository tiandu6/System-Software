#include <intr.h>
#include <riscv.h>

/* intr_enable - enable irq interrupt */
void intr_enable(void) { set_csr(sstatus, SSTATUS_SIE); }
/* sststus寄存器是一个控制和状态寄存器，
SSTATUS_SIE是一个位掩码，用于启用超级模式下的中断。
通过设置该位，允许处理器在超级模式下响应中断请求。
*/
/* intr_disable - disable irq interrupt */
void intr_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }
