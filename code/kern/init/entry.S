#include <mmu.h>
#include <memlayout.h>

    .section .text,"ax",%progbits
    .globl kern_entry
kern_entry:
    la sp, bootstacktop 
    #load address sp(栈指针寄存器)=bootstacktop的地址

    tail kern_init 
    #把执行控制权转交给 kern_init，即进入init.c中的C函数。
    #tail尾调用伪指令，因为kern_init不会返回

.section .data
    # .align 2^12
    .align PGSHIFT
    .global bootstack #低地址边界
bootstack:
    .space KSTACKSIZE #两个边界之间预留这么多字节
    .global bootstacktop #高地址边界
bootstacktop: