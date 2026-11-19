# Lab4 提示词汇总

## 功能模块一：虚拟内存页操作

**负责人：** 2511592-陈逍宇

[PROMPT]

修改实际文件 `code/kern/mm/pmm.c`，补全 `LAB4 function1` 标记下的页映射操作。保留标记，按负责人填写 `YOUR CODE`。完成后运行 PMM 评分项。

[RELY]

`get_pte(pgdir, la, create)`、`pte_create()`、`pte2page()`、`page2ppn()`、`page_ref_inc()`、`page_ref_dec()`、`free_page()`、`tlb_invalidate()` 和 `PTE_V/R/W/X`。

[GUARANTEE]

```c
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```

[SPECIFICATION]

- `page_insert` 的 Pre-Condition 为有效的页表根和已分配的目标页。Post-Condition 为末级项有效，指向目标页并具有合法的叶子权限。首次映射增加引用，同页映射只更新权限，异页替换撤销旧引用。分配失败返回 `-E_NO_MEM`，清理本次建立的部分页表结构。
- `page_remove` 的 Pre-Condition 为有效的页表根。Post-Condition 为撤销已有映射，旧页引用减一，归零时回收。缺少页表或有效映射时直接返回。
- Requirements：修改或清除页表项后使旧翻译失效，需要回收旧页时先完成失效。

---

## 功能模块二：进程创建及初始化

**负责人：** 2513411-邱宇湛

[PROMPT]

修改实际文件 `code/kern/process/proc.c`，补全 `LAB4 function2` 标记下的内核线程创建功能。保留标记，按负责人填写 `YOUR CODE`。完成后运行进程创建评分项。

[RELY]

`proc_struct`、`context`、`trapframe`、`boot_pgdir_pa`、`bootstack`、`setup_kstack()`、`put_kstack()`、`get_pid()`、`hash_proc()`、`wakeup_proc()`、`forkret`、`kernel_thread_entry`。

[GUARANTEE]

```c
static struct proc_struct *alloc_proc(void);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
void proc_init(void);
```

[SPECIFICATION]

- `alloc_proc` 在分配器可用时申请并初始化 PCB，设为 `PROC_UNINIT`，PID 为 `-1`，根页表为 `boot_pgdir_pa`，初始化两个链表节点。申请失败返回 `NULL`。本实验的 `copy_mm` 保持内核线程共享地址空间并返回成功。
- `kernel_thread` 以有效函数入口为前置条件，构造初始帧，把函数和参数写入 `s0/s1`，令 `epc` 指向 `kernel_thread_entry`，设置 S 模式返回状态后交给 `do_fork`。
- `copy_thread` 在新进程内核栈就绪后复制初始帧，令子线程的 `a0=0`，设置帧中的 `sp`，并用 `context.ra/sp` 指定 `forkret` 和帧地址。
- `do_fork` 成功时分配 PCB 和内核栈，设置父进程和执行现场。在本地关中断期间分配 PID、挂接链表和哈希表、更新计数并唤醒线程，最后恢复中断状态并返回 PID。失败时返回错误码并释放已申请的资源。
- `proc_init` 在启动栈和内核页表就绪后初始化管理结构，让 PID 0 的 `idleproc` 复用启动栈并标记需要调度，设置 `current` 和计数，再创建可运行的 `initproc`。

---

## 功能模块三：进程调度

**负责人：** 2511107-林永盛

[PROMPT]
修改真实文件，补全Lab4模块三。保留标记，YOUR CODE改为2511107。
[RELY]
proc_list、current、idleproc、context、sync.h。
[GUARANTEE]
proc.c
void cpu_idle(void),proc_run(struct proc_struct*);
sched.c
void schedule(void);
switch.S
void switch_to(struct context*,struct context*);
[SPECIFICATION]
PCB与页表就绪。空闲循环见need_resched则调度。关中断清标记，轮转选就绪线程，无则回idle，更新runs。异进程更新current、写Sv39 satp并刷TLB。按context保存恢复ra/sp/s0–s11，返回后恢复中断。
