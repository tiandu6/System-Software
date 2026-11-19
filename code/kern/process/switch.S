//LAB4 function3 2511107
#include <riscv.h>

    .text
    .globl switch_to
switch_to:
    # a0 指向旧进程的 context，a1 指向目标进程的 context。
    # 按 struct context 的字段顺序保存寄存器。RV64 每个字段占 8 字节。
    # ra 记住从哪条指令继续，sp 记住旧内核栈用到了哪里。
    STORE ra,  0*REGBYTES(a0)
    STORE sp,  1*REGBYTES(a0)
    STORE s0,  2*REGBYTES(a0)
    STORE s1,  3*REGBYTES(a0)
    STORE s2,  4*REGBYTES(a0)
    STORE s3,  5*REGBYTES(a0)
    STORE s4,  6*REGBYTES(a0)
    STORE s5,  7*REGBYTES(a0)
    STORE s6,  8*REGBYTES(a0)
    STORE s7,  9*REGBYTES(a0)
    STORE s8, 10*REGBYTES(a0)
    STORE s9, 11*REGBYTES(a0)
    STORE s10, 12*REGBYTES(a0)
    STORE s11, 13*REGBYTES(a0)

    # 从 a1 指向的 context 加载值。更新 sp 后，后续 LOAD 仍通过 a1 读 context。
    LOAD ra,  0*REGBYTES(a1)
    LOAD sp,  1*REGBYTES(a1)
    LOAD s0,  2*REGBYTES(a1)
    LOAD s1,  3*REGBYTES(a1)
    LOAD s2,  4*REGBYTES(a1)
    LOAD s3,  5*REGBYTES(a1)
    LOAD s4,  6*REGBYTES(a1)
    LOAD s5,  7*REGBYTES(a1)
    LOAD s6,  8*REGBYTES(a1)
    LOAD s7,  9*REGBYTES(a1)
    LOAD s8, 10*REGBYTES(a1)
    LOAD s9, 11*REGBYTES(a1)
    LOAD s10, 12*REGBYTES(a1)
    LOAD s11, 13*REGBYTES(a1)
    # 按目标进程的 ra 跳转。新线程进入 forkret，已运行过的线程接着执行。
    ret
