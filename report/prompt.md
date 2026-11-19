# Lab4 提示词汇总

## 功能模块一：虚拟内存页操作

**负责人：** 2511592-陈逍宇

## [PROMPT]

**任务**：在 `code/kern/mm/pmm.c` 的 `//LAB4 function1 YOUR CODE` 标记处，补全 `page_insert` 和 `page_remove`。两个函数共同维护 `la` 所在虚拟页的映射，使叶子页表项、物理页引用计数和 TLB 状态在操作完成后保持一致。

**操作要求**：直接修改项目中的实际文件。保留标记和既有逻辑，仅在标记对应的两个函数内进行必要修改。函数签名须与 `code/kern/mm/pmm.h` 中的声明一致。

**输出要求**：依据以下上下文和行为规格完成修改，以工程中的真实定义为准。完成后简要说明验证结果，区分已运行的测试和仅通过代码检查的情形。

## [RELY]

- `get_pte(pgdir, la, create)` 定位 `la` 对应的叶子页表项。`create` 为 `0` 时仅查找，为 `1` 时允许分配缺少的中间级页表页。返回非空指针表示叶子槽位存在，槽位内容含 `PTE_V` 才表示已有有效映射。
- 当前工程使用三级页表。`get_pte(pgdir, la, 1)` 可能在第一个中间级页表页已经分配并挂接后，因第二次分配失败而返回 `NULL`。`PDX1(la)` 可定位根表项，`pde2page()` 可取得有效中间级表项指向的页表页。
- `page2ppn(page)` 返回物理页号。`pte_create(ppn, type)` 使用物理页号和标志构造页表项，并自动置 `PTE_V`。`pte2page()` 可从有效叶子项取得 `struct Page *`。`PTE_U`、`PTE_R`、`PTE_W` 等权限位沿用工程定义。
- `get_page(pgdir, la, &ptep)` 仅查找已有映射。它返回映射的物理页，同时通过 `ptep` 提供叶子槽位指针。缺少中间级页表或叶子项无效时，返回 `NULL`。
- `page_ref_inc()` 和 `page_ref_dec()` 均返回更新后的引用计数。`free_page()` 回收一个物理页。`tlb_invalidate(pgdir, la)` 使指定地址的翻译失效，`flush_tlb()` 提供整体失效操作。

## [GUARANTEE]

交付以下两个现有接口，无需新增辅助函数或公开接口。

```c
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```

## [SPECIFICATION]

### `page_insert`

**Pre-Condition**：`pgdir` 指向有效页表根，`page` 指向已分配、可建立映射的受管物理页，`perm` 为调用者要求的页表标志。`la` 可以带有页内偏移，操作对象是其所在的虚拟页。

**Post-Condition**：成功返回 `0`。目标叶子项有效，指向 `page`，权限按本次 `perm` 设置；相关物理页的引用计数反映映射的实际增减。内存不足时返回 `-E_NO_MEM`，原有映射、引用计数和空闲页数量保持调用前的状态。

- **Case 1 · 原地址未映射**：建立映射，目标物理页的引用计数增加一次。若同一物理页已映射到其他地址，那些映射继续有效。
- **Case 2 · 原地址已映射到 `page`**：更新权限，清除旧映射中本次未请求的权限位。该物理页的引用计数保持不变，包括它仅有这一个引用的情形。
- **Case 3 · 原地址已映射到另一物理页**：将映射替换为目标页及本次权限。目标页增加一个引用，旧页减少一个引用。旧页引用归零时回收，仍有引用时保留。
- **Case 4 · 中间级页表分配失败**：撤销本次调用已经挂接的临时页表，归还对应页表页，保留调用前已存在的页表结构。目标地址不留下半完成映射，目标物理页不增加引用。

**Requirements**：新叶子项写入后，使 `la` 对应的 TLB 条目失效，包括仅更新同页权限的情形。若旧物理页需要回收，应先使旧翻译失效，再让物理页进入空闲集合。失败回滚涉及已挂接的中间级页表时，同样应在回收该页表页前完成必要的失效处理。

### `page_remove`

**Pre-Condition**：`pgdir` 指向有效页表根。`la` 可以带有页内偏移，其所在虚拟页可以已映射，也可以未映射。

**Post-Condition**：返回后，`la` 所在虚拟页无有效映射。其他虚拟页的映射保持原状。对同一地址重复调用不会重复减少引用或回收页面。

- **Case 1 · 存在有效映射**：清空目标叶子项，使该地址的 TLB 条目失效，并将原物理页的引用计数减少一次。计数归零时回收物理页，仍有引用时保留。已有中间级页表继续保留。
- **Case 2 · 不存在有效映射**：无论缺少中间级页表，还是已有叶子槽位但其有效位未置位，均直接返回。查找过程不分配页表页，不改变任何引用计数，也不回收页面。

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
