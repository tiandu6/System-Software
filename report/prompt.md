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

## [PROMPT]

**任务**：在 `code/kern/process/proc.c` 的 `//LAB4 function2 YOUR CODE` 标记处实现 `alloc_proc`、`copy_mm`、`copy_thread`、`do_fork`、`kernel_thread` 和 `proc_init`，完成进程管理链表与哈希表初始化、第 0 个内核线程 `idleproc` 的封装，以及第 1 个可运行内核线程 `initproc` 的创建。

**操作要求**：直接修改项目中的实际文件。保留 `//LAB4 function2` 注释标记并将 `YOUR CODE` 替换为 `2513411`。函数签名须与 `code/kern/process/proc.h` 及静态辅助函数约定一致，不新增无关公开接口。不得改动 `init_main`、模块三区域、`Makefile` 或教师评测代码，不提交或推送 Git。

**输出要求**：依据以下上下文和行为规格完成代码编写，以工程中的真实定义为准。完成后简要说明字段初始化、`trapframe` 首次运行现场构造以及资源分配失败回滚的处理依据，并如实记录编译、`make qemu` 与 `make grade` 的验证结果。

## [RELY]

1. 先读取工程中的实际定义：
   - `code/kern/process/proc.h` 与 `code/kern/process/proc.c`：`struct proc_struct`、`struct context`、全局进程管理变量及已有辅助函数；
   - `code/kern/process/entry.S` 与 `code/kern/trap/trapentry.S`：`kernel_thread_entry`、`forkrets` 与 `__trapret`；
   - `code/kern/trap/trap.h`：`struct pushregs` 和 `struct trapframe`；
   - `code/kern/mm/pmm.h`、`code/kern/mm/memlayout.h`、`code/kern/mm/kmalloc.h`：`CLONE_VM`、`KSTACKSIZE`、`boot_pgdir_pa`、`bootstack`、`kmalloc`/`kfree`；
   - `code/kern/schedule/sched.h`、`code/kern/sync/sync.h`、`code/libs/riscv.h`、`code/libs/error.h`、`code/libs/list.h`。

2. 已有数据结构与全局状态（位于 `proc.h` 和 `proc.c`）：
   - `struct proc_struct` 包含字段：`state`、`pid`、`runs`、`kstack`、`need_resched`、`parent`、`mm`、`context`、`tf`、`pgdir`、`flags`、`name[PROC_NAME_LEN + 1]`、`list_link`、`hash_link`。
   - `enum proc_state`：`PROC_UNINIT`、`PROC_SLEEPING`、`PROC_RUNNABLE`、`PROC_ZOMBIE`。
   - `struct context` 包含 `ra`、`sp`、`s0`~`s11`。
   - 全局变量：`proc_list`、`hash_list[HASH_LIST_SIZE]`、`idleproc`、`initproc`、`current`、`nr_process`、`boot_pgdir_pa`、`bootstack`。
   - 常量：`PROC_NAME_LEN` 为 `15`，`MAX_PROCESS` 为 `4096`，`KSTACKSIZE` 为 `2 * PGSIZE`（8192 字节），`CLONE_VM` 为 `0x00000100`，错误码为 `-E_NO_MEM` 和 `-E_NO_FREE_PROC`。

3. 已有接口与汇编衔接约定：
   - `setup_kstack(proc)` 分配 2 页内核栈并写入 `proc->kstack`，失败返回 `-E_NO_MEM`；`put_kstack(proc)` 释放内核栈。
   - `get_pid()` 遍历 `proc_list` 分配大于 `0` 且唯一的 PID；`hash_proc(proc)` 将 `proc` 插入 `hash_list`；`find_proc(pid)` 按 PID 查找进程；`wakeup_proc(proc)` 将状态置为 `PROC_RUNNABLE`。
   - `set_proc_name(proc, name)` 内部按固定长度 `PROC_NAME_LEN` 调用 `memcpy(proc->name, name, PROC_NAME_LEN)`，调用方传入的字符串缓冲区长度应达到 `PROC_NAME_LEN` 字节以上，避免对短字符串字面量越界读取。
   - `forkret()` 调用 `forkrets(current->tf)`，进而执行 `__trapret`（`RESTORE_ALL` + `sret`）；`kernel_thread_entry` 从 `s1` 取出参数放入 `a0`，调用 `s0` 指向的函数，返回后调用 `do_exit`。
   - `local_intr_save(intr_flag)` 和 `local_intr_restore(intr_flag)` 用于保护临界区。

## [GUARANTEE]

交付以下六个接口，保留精确的函数名、参数和返回类型：

```c
static struct proc_struct *alloc_proc(void);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
void proc_init(void);
```

## [SPECIFICATION]

### `alloc_proc`

- **Pre-Condition**：`kmalloc` 已可用，系统已建立初始内核页表 `boot_pgdir_pa`。
- **Post-Condition**：若分配成功，返回完成初始化的 `struct proc_struct *`：`state` 为 `PROC_UNINIT`，`pid` 为 `-1`，`pgdir` 为 `boot_pgdir_pa`，`runs`、`kstack`、`need_resched`、`flags` 为 `0`，`parent`、`mm`、`tf` 为 `NULL`，`context` 和 `name` 清零，`list_link` 和 `hash_link` 完成链表节点初始化。内存不足时返回 `NULL`。
- **Requirements**：必须显式清零再设置非零初值，防止 `kmalloc` 返回未清零内存导致字段出现脏数据。

### `copy_mm`

- **Pre-Condition**：`proc` 为正在创建的子内核线程 PCB，当前进程为内核线程（`current->mm == NULL`）。
- **Post-Condition**：断言 `current->mm == NULL` 成立后返回 `0`，不分配新的用户态地址空间结构。
- **Requirements**：在当前实验阶段保留函数形态，不修改 `proc->mm` 与 `proc->pgdir`。

### `copy_thread`

- **Pre-Condition**：`proc->kstack` 已指向分配好的 `KSTACKSIZE` 字节内核栈，`tf` 指向有效的初始中断帧模板。
- **Post-Condition**：在内核栈顶留出 `sizeof(struct trapframe)` 空间保存 `proc->tf`，将 `*tf` 复制到 `proc->tf`，设置 `proc->tf->gpr.a0 = 0`；当 `esp == 0` 时将 `proc->tf->gpr.sp` 设为 `(uintptr_t)proc->tf`，否则设为 `esp`；将 `proc->context.ra` 设为 `(uintptr_t)forkret`，`proc->context.sp` 设为 `(uintptr_t)proc->tf`。
- **Requirements**：内核线程传入 `esp == 0` 时不得让 `tf->gpr.sp` 保持为 `0`，否则从 `__trapret` 恢复后将因栈指针无效触发访存异常。

### `kernel_thread`

- **Pre-Condition**：`fn` 为有效的内核线程入口函数指针，`arg` 为传递给 `fn` 的参数，`clone_flags` 为线程创建标志。
- **Post-Condition**：在栈上构造清零后的临时 `trapframe`，令 `tf.gpr.s0 = (uintptr_t)fn`、`tf.gpr.s1 = (uintptr_t)arg`、`tf.epc = (uintptr_t)kernel_thread_entry`，令 `tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE`，并调用 `do_fork(clone_flags | CLONE_VM, 0, &tf)` 返回结果。
- **Requirements**：`tf.status` 必须保证 `sret` 返回到 S 模式并在跳转时开启中断，同时在 `RESTORE_ALL` 恢复现场过程中保持 `SIE` 为 `0`。

### `do_fork`

- **Pre-Condition**：`current` 有效，`tf` 指向待复制的现场，内核栈与 PCB 分配器可用。
- **Post-Condition**：
  - **Case 1 · 正常路径**：成功分配 PCB 与内核栈、完成 `copy_mm` 与 `copy_thread` 后，在关中断临界区内分配 PID、将进程加入 `hash_list` 与 `proc_list`、递增 `nr_process` 并调用 `wakeup_proc(proc)` 置为 `PROC_RUNNABLE`，恢复中断后返回新进程 PID。
  - **Case 2 · 进程数达到上限**：`nr_process >= MAX_PROCESS` 时直接返回 `-E_NO_FREE_PROC`。
  - **Case 3 · 分配失败**：`alloc_proc()` 失败返回 `-E_NO_MEM`；`setup_kstack()` 失败释放已分配的 PCB 后返回错误码；`copy_mm()` 失败先调用 `put_kstack()` 释放内核栈，再调用 `kfree()` 释放 PCB 并返回错误码。
- **Requirements**：不得在失败路径遗漏已申请的资源；全局进程链表、哈希表和 `nr_process` 的更新必须在关中断保护下完成。

### `proc_init`

- **Pre-Condition**：物理内存、虚拟内存框架及 `bootstack`、`boot_pgdir_pa` 已就绪。
- **Post-Condition**：初始化 `proc_list` 和全部 `hash_list` 桶；分配并设置 `idleproc`（`pid = 0`、`state = PROC_RUNNABLE`、`kstack = (uintptr_t)bootstack`、`need_resched = 1`、`name = "idle"`），将 `current` 指向 `idleproc`、`nr_process` 设为 `1`；调用 `kernel_thread(init_main, "Hello world!!", 0)` 创建 `initproc`，检查返回的 `pid > 0` 并通过 `find_proc(pid)` 获取后断言 `idleproc->pid == 0` 且 `initproc->pid == 1`，设置其名称为 `"init"`。
- **Requirements**：若 `idleproc` 分配或 `initproc` 创建失败，必须调用 `panic()` 终止；`idleproc` 不加入 `proc_list`；函数尾部不得提前 `return`，以便评测注入代码接在函数末尾执行。

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
