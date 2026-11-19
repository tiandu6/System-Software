#ifndef __KERN_PROCESS_PROC_H__
#define __KERN_PROCESS_PROC_H__

#include <defs.h>
#include <list.h>
#include <trap.h>
#include <memlayout.h>

// process's state in his life cycle
enum proc_state
{
    PROC_UNINIT = 0, // uninitialized
    PROC_SLEEPING,   // sleeping
    PROC_RUNNABLE,   // runnable(maybe running)
    PROC_ZOMBIE,     // almost dead, and wait parent proc to reclaim his resource
};

struct context
{
    uintptr_t ra;
    uintptr_t sp;
    uintptr_t s0;
    uintptr_t s1;
    uintptr_t s2;
    uintptr_t s3;
    uintptr_t s4;
    uintptr_t s5;
    uintptr_t s6;
    uintptr_t s7;
    uintptr_t s8;
    uintptr_t s9;
    uintptr_t s10;
    uintptr_t s11;
};

#define PROC_NAME_LEN 15
#define MAX_PROCESS 4096
#define MAX_PID (MAX_PROCESS * 2)

extern list_entry_t proc_list;

struct proc_struct 
{
    enum proc_state state;       // 进程状态，例如可运行、睡眠或已退出待回收
    int pid;                     // 进程的唯一编号
    int runs;                    // 被调度器选中的次数

    uintptr_t kstack;            // 该进程内核栈的起始地址（低地址端）
    volatile bool need_resched;   // 为 true 时，表示当前进程需要重新调度
    struct proc_struct *parent;  // 指向父进程的 PCB

    struct mm_struct *mm;        // 指向虚拟地址空间的管理结构，记录 VMA 等信息
    struct context context;      // 进程切换时保存和恢复的寄存器，如 ra、sp、s0–s11
    struct trapframe *tf;        // 指向 Trap 现场，包含通用寄存器和 sepc 等
                                 // 新线程第一次运行时，也用它装入预先准备的初始现场
    uintptr_t pgdir;             // 根页表的物理基址，切换地址空间时用于设置 satp

    uint32_t flags;              // 进程的额外标志，用不同的位记录属性或状态
    char name[PROC_NAME_LEN + 1]; // 进程名，多出的一个字节存字符串结束符 '\0'

    list_entry_t list_link;      // 用这个节点把 PCB 挂入全局进程链表
    list_entry_t hash_link;      // 用这个节点把 PCB 挂入 PID 哈希表，便于按 PID 查找
};

#define le2proc(le, member) \
    to_struct((le), struct proc_struct, member)

extern struct proc_struct *idleproc, *initproc, *current;

void proc_init(void);
void proc_run(struct proc_struct *proc);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);

char *set_proc_name(struct proc_struct *proc, const char *name);
char *get_proc_name(struct proc_struct *proc);
void cpu_idle(void) __attribute__((noreturn));

struct proc_struct *find_proc(int pid);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int do_exit(int error_code);

#endif /* !__KERN_PROCESS_PROC_H__ */
