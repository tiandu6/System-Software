# 实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab4 进程管理 |
| **小组成员** | 2511107-林永盛、2511592-陈逍宇、2513411-邱宇湛 |
| **完成日期** | 2026-11-19 |

### 小组分工

| 成员 | 负责的练习/模块 | 报告分工 |
|------|----------------|----------|
| 2511107-林永盛 | 功能模块三：进程调度 | 公共部分、模块三和 Challenge |
| 2511592-陈逍宇 | 功能模块一：虚拟内存页操作 | 模块一的设计、实现和验证 |
| 2513411-邱宇湛 | 功能模块二：进程创建及初始化 | 模块二的设计、实现和验证 |

---

## 一、实验目的

1. 掌握 Sv39 下页映射的建立、更新和撤销，理解页表项、物理页引用计数与 TLB 的关系。
2. 理解内核线程的创建过程，掌握 PCB、内核栈、`context` 和 `trapframe` 的作用。
3. 掌握可运行线程的选择和切换过程，理解内核如何从 `idleproc` 切换到 `initproc`。

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2511107-林永盛 | Codex（终端 Agent） | GPT-6.2 Sol | 高 |
| 2511592-陈逍宇 | Codex（VS Code 插件） | GPT-6.2 Sol | 中 |
| 2513411-邱宇湛 | Codex（终端 Agent） | GPT-6.1 Sol | 高 |

AI 编程工具记录交互方式，底层模型记录生成代码时使用的模型。

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

本实验从内核地址空间出发，先建立和维护虚拟页映射，再创建内核线程，最后调度线程运行。启动时的执行现场登记为 `idleproc`，新建的 `initproc` 在第一次调度时接管 CPU 并执行 `init_main()`。

### 3.2 功能的逐步实现

1. **维护页映射。** `page_insert()` 和 `page_remove()` 更新末级页表项、物理页引用计数和 TLB，为内核地址空间提供页级映射管理。
2. **创建内核线程。** `proc_init()` 让 `idleproc` 复用启动栈，再通过 `kernel_thread()` 和 `do_fork()` 为 `initproc` 准备 PCB、内核栈和首次运行现场，并把它设为可运行。
3. **调度并启动线程。** `cpu_idle()` 检查调度标记，`schedule()` 选出 `initproc`，`proc_run()` 更新当前进程和页表，`switch_to()` 切换寄存器与栈。随后从 `forkret` 进入 Trap 返回路径，经 `kernel_thread_entry` 调用 `init_main()`。

---

## 四、实验内容与实现

<!--
说明：本部分按照 exercises 文档中的练习顺序组织
每个功能模块或者练习中可能包含多项任务，请根据实际情况调整
-->

### 功能模块一：虚拟内存页操作功能

**负责人：2511592-陈逍宇**

#### 模块功能描述

**需要实现/修改的 C 语言接口：**

```c
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);(kern/mm/pmm.c)
void page_remove(pde_t *pgdir, uintptr_t la);(kern/mm/pmm.c)
```

**与之衔接的已有接口：**

```c
get_pte()、pte_create()、pte2page()、page_ref_inc()、page_ref_dec()、set_page_ref()、alloc_page()、free_page()、tlb_invalidate()、flush_tlb()
pde_t、pte_t、struct Page、pages、boot_pgdir_va
PTE_V、PTE_U、PTE_W、PTE_R
```

**功能说明：**

这个模块负责在现有多级页表框架上完成最基本的映射维护，也就是把物理页挂到指定线性地址上，以及把已有映射安全地撤销掉。

#### 关键状态和实现步骤
##### 关键状态

`page_insert()` 和 `page_remove()` 都会改变线性地址 `la` 的映射。实现时需要同时维护三项状态。

| 状态 | 在代码中的含义 | 更新要求 |
|---|---|---|
| 末级页表项 `*ptep` | `PTE_V` 表示该项有效，物理页号和权限位决定 `la` 指向哪一页、允许怎样访问 | 建立或替换映射时写入新页号和权限，删除时清零 |
| 物理页引用计数 `page_ref(page)` | 记录物理页持有的引用；每个有效映射贡献一个引用 | 新增映射加一，撤销映射减一。同一地址重新映射到同一页时计数不变 |
| TLB 中缓存的地址翻译 | CPU 可能仍缓存 `la` 对应的旧物理页或旧权限 | 先更新页表项，再使对应翻译失效，最后才允许回收旧页 |

##### `page_insert()` 的实现步骤

1. **取得末级页表项。** 先保存 `pgdir[PDX1(la)]` 的原值，再调用 `get_pte(pgdir, la, 1)`。参数 `1` 允许创建缺失的中间页表。若第一次分配成功、第二次失败，根表项已经发生变化，因此要恢复原值、刷新 TLB 并回收刚分配的页表页，然后返回 `-E_NO_MEM`。此时目标物理页的引用计数仍保持原值。
2. **判断原映射。** 若 `*ptep` 带有 `PTE_V`，用 `pte2page(*ptep)` 记下旧页 `old`；否则令 `old = NULL`。当 `old != page` 时，调用 `page_ref_inc(page)` 为新映射增加一个引用。若 `old == page`，说明此次操作只需更新权限。
3. **写入映射并刷新翻译。** 执行 `*ptep = pte_create(page2ppn(page), perm)`，用目标页号和本次传入的权限重新构造页表项。随后调用 `tlb_invalidate(pgdir, la)`。即使物理页相同，这一步仍然重要，例如撤销写权限后，CPU 需要丢弃缓存的旧权限。
4. **释放旧引用。** 若 `old` 存在且与 `page` 不同，调用 `page_ref_dec(old)`；计数降到零时才调用 `free_page(old)`。完成后返回 `0`。将回收放在最后，可以保证旧物理页重新进入分配器前，旧映射已经撤销。

##### `page_remove()` 的实现步骤

1. **只查询现有映射。** 调用 `get_page(pgdir, la, &ptep)`，同时取得物理页和末级页表项。其内部使用 `get_pte(pgdir, la, 0)`，查询过程不会分配页表。若中间页表缺失或末级页表项无效，直接返回。
2. **撤销映射。** 将 `*ptep` 清零，再调用 `tlb_invalidate(pgdir, la)`，使 CPU 丢弃该地址的旧翻译。
3. **减少物理页引用。** 调用 `page_ref_dec(page)`，仅在结果为零时释放物理页。因此，若两个地址都映射到 `p1`，删除第一个地址只会让计数从 `2` 降到 `1`，另一个地址仍能访问 `p1`；再次删除第一个地址时，查询已经找不到有效映射，计数仍为 `1`。这也就是删除操作的幂等性。
##### 最终提示词

以下是经过迭代优化后，最终成功实现该功能的提示词：

````markdown
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
````

#### 迭代过程

这两个函数的实现经历了三个阶段：先理清映射与引用计数的关系，再处理分配失败和重复删除等边界情况，最后从单独测试推进到完整内核验证。

1. **确定建立、替换和删除映射的规则。** `page_insert()` 先通过 `get_pte(pgdir, la, 1)` 定位末级页表项，再根据旧映射决定如何更新引用计数。原地址未映射时，目标页引用加一；原地址指向另一物理页时，新页引用加一，旧页引用减一；原地址已经指向目标页时，只更新权限，引用计数保持不变。`page_remove()` 则通过 `get_page()` 查询现有映射，只有找到有效映射才清除页表项并减少引用。

2. **补齐失败回滚，并确定旧页的回收时机。** 检查 `get_pte()` 的实现时发现，Sv39 页表遍历可能连续分配两张中间页表页。如果第一张分配成功、第二张失败，仅返回 `-E_NO_MEM` 会留下刚挂到根表上的空页表。因此 `page_insert()` 在调用前保存原根表项，必要时恢复它并回收新分配的页表页。对于成功的替换和删除，两个函数都先更新页表项，再使对应的 TLB 翻译失效，最后才释放引用计数归零的旧页。这样也保证了重复调用 `page_remove()` 时不会再次扣减引用。

3. **先单独验证，再验证完整启动路径。** 最初 Lab2、Lab3 的前置代码尚未补齐，因此先对 `pmm.c` 做 RISC-V 交叉编译，再用宿主模拟测试运行课程 PMM 断言，并额外验证两处分配失败时的回滚。补齐前置代码后，又在 QEMU 中运行完整内核，验证真实页表下的映射访问。`check_boot_pgdir()` 将同一物理页映射到两个虚拟地址，通过其中一个地址写入字符串，再从另一个地址读出相同内容。

#### 最终结果

`page_insert()` 能正确完成首次映射、同页权限更新和异页替换，成功返回 `0`，中间页表分配失败时回滚并返回 `-E_NO_MEM`。`page_remove()` 能安全撤销映射，保留仍被其他地址引用的物理页，对已删除或从未映射的地址可重复调用。

完整内核在 QEMU 中输出了 `check_pgdir() succeeded!` 和 `check_boot_pgdir() succeeded!`。课程评分中的 **PMM 项通过，获得 10/10**；包含进程创建和调度在内的 Lab4 总分为 **30/30**。

---

### 功能模块二：进程创建及初始化

**负责人：2513411-邱宇湛**

#### 模块功能描述

**需要实现/修改的 C 语言接口：**

```c
static struct proc_struct *alloc_proc(void);(kern/process/proc.c)
void proc_init(void);(kern/process/proc.c)
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);(kern/process/proc.c)
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);(kern/process/proc.c)
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);(kern/process/proc.c)
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);(kern/process/proc.c)
```

**与之衔接的已有接口与结构：**

```c
struct proc_struct、struct context、struct trapframe
proc_list、hash_list、idleproc、initproc、current、nr_process
kmalloc()、kfree()、memset()、setup_kstack()、put_kstack()、get_pid()、hash_proc()、find_proc()、set_proc_name()、wakeup_proc()
list_init()、list_add_before()、local_intr_save()、local_intr_restore()
boot_pgdir_pa、bootstack、read_csr(sstatus)、SSTATUS_SPP、SSTATUS_SPIE、SSTATUS_SIE
kernel_thread_entry、forkret、forkrets
```

**功能说明：**

该模块在 `vmm_init()` 之后由 `proc_init()` 入口接管执行，负责把当前单执行流的内核启动上下文登记为第 0 个内核线程 `idleproc`，并建立完整的线程创建通路（`kernel_thread()` -> `do_fork()` -> `alloc_proc()` / `setup_kstack()` / `copy_mm()` / `copy_thread()`），创建出可被调度器选中运行的第 1 个内核线程 `initproc`。

#### 关键状态和实现步骤

##### 关键状态

| 状态 | 在代码中的含义 | 维护要求 |
|---|---|---|
| 进程状态 `state` | 标记 PCB 当前处于 `PROC_UNINIT` 还是 `PROC_RUNNABLE` | `alloc_proc()` 置为 `PROC_UNINIT`，资源和上下文全部装配完成并入链后由 `wakeup_proc()` 转为 `PROC_RUNNABLE` |
| 内核栈与 `tf` | `kstack` 为 2 页（8192 字节）内核栈低地址，`tf` 位于栈顶 | 新线程必须使用独立的内核栈；`copy_thread()` 在 `kstack + KSTACKSIZE` 顶部保留一份 `trapframe`，并保证 `tf->gpr.sp` 有效 |
| 切换上下文 `context` | 保存进程切换所需的 `ra`、`sp` 与 `s0`~`s11` | 新线程的 `context.ra` 指向 `forkret`，`context.sp` 指向 `proc->tf`，用于首次调度后进入 Trap 返回流程 |
| 全局进程管理结构 | `proc_list`、`hash_list`、`nr_process`、`current` | 除 `idleproc` 外，新建线程在关中断保护下分配 PID 并同时挂入 `proc_list` 与 `hash_list` |

##### 核心函数的实现逻辑

1. `alloc_proc()`：通过 `kmalloc(sizeof(*proc))` 申请进程控制块。`kmalloc` 不会自动清零内存，因此申请成功后先调用 `memset(proc, 0, sizeof(*proc))` 将全部字段置零，再显式初始化非零初值：`state = PROC_UNINIT`、`pid = -1`、`pgdir = boot_pgdir_pa`，并调用 `list_init()` 初始化 `list_link` 与 `hash_link`。
2. `proc_init()`：先初始化 `proc_list` 和全部 `HASH_LIST_SIZE` 个哈希桶。由于开机后系统已经运行在 `bootstack` 上，第 0 个内核线程 `idleproc` 不单独申请内核栈，也不进入 `proc_list`，在 `alloc_proc()` 之后直接设置 `pid = 0`、`state = PROC_RUNNABLE`、`kstack = (uintptr_t)bootstack`、`need_resched = 1`、名称 `"idle"`，并将 `current` 指向 `idleproc`、`nr_process` 设为 `1`。接着调用 `kernel_thread(init_main, "Hello world!!", 0)` 创建 `initproc`，校验返回值 `pid > 0`，通过 `find_proc(pid)` 取回 PCB 并断言 `idleproc->pid == 0` 与 `initproc->pid == 1`，最后设置名称为 `"init"`。
3. `kernel_thread()`：普通进程创建通常复制父进程的中断帧，内核线程没有用户态中断现场，于是在栈上构造临时 `struct trapframe tf` 作为模板。清零后将 `fn` 和 `arg` 分别放入 `tf.gpr.s0` 和 `tf.gpr.s1`，将 `tf.epc` 设为 `kernel_thread_entry`；`tf.status` 读取当前 `sstatus` 后置位 `SSTATUS_SPP` 和 `SSTATUS_SPIE` 并清除 `SSTATUS_SIE`，让 `sret` 返回到 S 模式并开启中断，同时避免 `RESTORE_ALL` 恢复过程中提前触发中断。最后传入 `clone_flags | CLONE_VM` 和栈参数 `0` 调用 `do_fork()`。
4. `copy_thread()`：在子线程内核栈顶部划出 `sizeof(struct trapframe)` 空间给 `proc->tf` 并拷贝 `*tf`，按 `fork` 返回约定将 `proc->tf->gpr.a0` 清零。由于内核线程调用 `do_fork()` 时传入的 `esp` 为 `0`，这里令 `proc->tf->gpr.sp = (esp != 0) ? esp : (uintptr_t)proc->tf`，再令 `proc->context.ra = (uintptr_t)forkret`、`proc->context.sp = (uintptr_t)proc->tf`。首次切入该线程时，执行流按 `switch_to -> forkret -> forkrets(current->tf) -> __trapret(RESTORE_ALL + sret) -> kernel_thread_entry` 跳转，最终以 `s1` 为参数调用 `s0` 指向的 `init_main()`。
5. `copy_mm()`：本实验创建的都是共享内核页表的内核线程，断言 `current->mm == NULL` 后直接返回 `0`，保持与后续实验用户地址空间复制接口的一致性。
6. `do_fork()`：先检查 `nr_process >= MAX_PROCESS`（超限返回 `-E_NO_FREE_PROC`），随后按 `alloc_proc()`、设置 `proc->parent = current`、`setup_kstack(proc)`、`copy_mm()`、`copy_thread()` 的顺序装配资源。若 `setup_kstack()` 失败则释放 PCB 后返回错误码；若 `copy_mm()` 失败则按逆序先 `put_kstack(proc)` 再 `kfree(proc)`。装配完成后在 `local_intr_save()` 与 `local_intr_restore()` 保护下调用 `get_pid()` 分配 PID、挂入 `hash_list` 与 `proc_list` 尾部、递增 `nr_process` 并调用 `wakeup_proc(proc)` 置为就绪态，最后返回 `proc->pid`。

#### Spec 与 Prompt 的设计思路

模块二包含 6 个互相调用的函数，并直接依赖 `trapentry.S` 与 `kern/process/entry.S` 的寄存器约定。编写提示词时先核对 `proc.h`、`proc.c`、`trap.h` 与 `riscv.h` 的真实定义，在 `[RELY]` 中列出结构体字段、宏常量和 `set_proc_name()` 按固定长度 `PROC_NAME_LEN` 拷贝的行为边界；在 `[GUARANTEE]` 中固定 6 个函数的精确签名；在 `[SPECIFICATION]` 中按函数粒度明确前置条件、正常返回状态与失败清理条件，方便逐项对照验证。

| 函数 | 规格核心约束 |
|---|---|
| `alloc_proc` | 全字段清零后再设 `PROC_UNINIT`、`pid = -1`、`pgdir = boot_pgdir_pa` 与空链表头 |
| `copy_mm` | 断言 `current->mm == NULL` 后返回 `0`，不修改 `mm` 与 `pgdir` |
| `copy_thread` | 栈顶放置 `proc->tf`、`a0 = 0`、`esp == 0` 时修正 `tf->gpr.sp`、`context.ra = forkret` |
| `kernel_thread` | `s0/s1` 传入口和参数、`epc = kernel_thread_entry`、`status` 设 `SPP \| SPIE` 且清 `SIE` |
| `do_fork` | 关中断登记 PID 与链表并唤醒；`setup_kstack`/`copy_mm` 失败按逆序回收已申请资源 |
| `proc_init` | `idleproc`（PID 0）复用 `bootstack` 且不入 `proc_list`，再创建并检查 `initproc`（PID 1） |

#### 最终提示词

以下是经过迭代优化后，最终成功实现该功能的提示词（与 `report/prompt.md` 模块二一致）：

````markdown
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
````

#### 实现迭代过程

1. **第一次迭代：首次运行现场与栈指针核对**
   最初对照 `trapentry.S` 和 `kern/process/entry.S` 检查 `kernel_thread()` 与 `copy_thread()` 的衔接时，发现两个容易遗漏的细节。第一，内核线程调用 `do_fork()` 时传入的 `stack` 为 `0`，`copy_thread()` 必须把 `proc->tf->gpr.sp` 显式设为 `(uintptr_t)proc->tf`，否则 `__trapret` 中的 `RESTORE_ALL` 在最后恢复 `x2` 时会把 `sp` 清零，导致刚进入 `init_main()` 压栈就触发异常。第二，`kernel_thread()` 设置 `tf.status` 时，要在置位 `SSTATUS_SPP` 和 `SSTATUS_SPIE` 的同时清掉 `SSTATUS_SIE`，让中断在 `sret` 执行完毕后才打开。此外，`kernel_thread()` 原来传入 `do_fork(clone_flags, 0, &tf)`，修改为 `clone_flags | CLONE_VM` 以标明共享地址空间；`proc_init()` 中对 `kernel_thread()` 返回值的判断从 `pid < 0` 收紧为 `pid <= 0`（PID `0` 保留给 `idleproc`），并补上了 `idleproc->pid == 0` 与 `initproc->pid == 1` 的检查。
2. **第二次迭代：`set_proc_name()` 拷贝边界与 `do_fork()` 临界区核对**
   检查 `proc.c` 中已有的 `set_proc_name()` 时注意到，该函数直接使用 `memcpy(proc->name, name, PROC_NAME_LEN)` 固定拷贝 15 字节。如果直接传入 `"idle"` 或 `"init"` 这样的短字面量，`memcpy` 会向后多读十几个字节。因此在 `proc_init()` 中使用长度为 `PROC_NAME_LEN + 1` 的静态零填充字符数组来传递名称。同时在 `copy_mm()` 补上 `assert(current->mm == NULL)` 检查当前线程确实没有用户地址空间，并检查 `do_fork()` 中的 `get_pid()`、`hash_proc()`、链表插入、`nr_process++` 和 `wakeup_proc()` 均位于同一段关中断临界区内，确认失败跳转路径按先 `put_kstack()` 后 `kfree()` 的顺序清理。
3. **第三次迭代：全工程联调与本地验证环境兼容**
   在 WSL2 Ubuntu（`riscv64-unknown-elf-gcc 14.2.0`、`QEMU 4.1.1`）下与模块一、模块三代码一起做整机联调时遇到三处工具链和环境差异：一是 GCC 14.2.0 默认将 `-Wint-conversion` 和 `-Wincompatible-pointer-types` 作为报错处理，触发在框架原有的 `kern/mm/kmalloc.c:92` 与评测文件 `tools/test_lab4.c:90-91` 上；二是 Windows 检出使 `tools/grade.sh` 带有 CRLF 行尾；三是本机安装了 `gdb-multiarch` 但未安装 `riscv64-unknown-elf-gdb`。为了只保留对个人负责文件和报告的修改、不改动仓库 `Makefile` 或教师脚本，验证时仅在外部临时副本和环境变量中传入 `-Wno-error=int-conversion -Wno-error=incompatible-pointer-types`、转换临时 `grade.sh` 行尾并用 `gdb-multiarch bin/kernel` 包装脚本完成实测。

#### 最终结果

完成上述修改后，全工程编译通过且 `proc.c` 无警告。`make qemu` 启动后通过了 `check_alloc_page`、`check_pgdir`、`check_boot_pgdir`、`kmalloc_init` 和 `check_vmm`，`initproc` 成功打印 `this initproc, pid = 1, name = "init"`、`To U: "Hello world!!".` 和 `To U: "en.., Bye, Bye. :)"` 并经 `do_exit()` 退出。`make grade` 中 `check Process Create` 项通过（`10/10`），全部三项总分 `30/30`。当前模块覆盖内核线程的创建、登记和首次运行现场构造，用户态内存复制和进程回收将在后续实验中实现。

---
### 功能模块三：进程调度

**负责人：** 2511107-林永盛

#### 模块功能描述

- `cpu_idle()` 检查 `need_resched`，有调度请求时调用 `schedule()`。
- `schedule()` 在关中断期间清除请求，从当前进程之后轮转查找可运行进程。当前是 `idleproc` 时从表头开始，无可运行进程时回到 `idleproc`，并更新选中进程的 `runs`。
- `proc_run()` 切换到不同进程时更新 `current`，写入 Sv39 的 `satp` 并刷新 TLB，再调用 `switch_to()`。
- `switch_to()` 按 `struct context` 的布局保存和恢复 `ra`、`sp`、`s0–s11`。新线程沿 `forkret → forkrets → __trapret` 启动，已经运行过的线程从原切换点继续。

#### Spec 与 Prompt 的设计思路

以 PCB 和页表已经初始化为前置条件，分别约定调度选择、页表切换和寄存器恢复的结果。规格覆盖无可运行进程、切换到不同进程和恢复原中断状态。

#### 最终提示词

以下是经过迭代优化后，最终成功实现该功能的提示词。

````markdown
## [PROMPT]

修改真实文件，补全Lab4模块三。保留标记，YOUR CODE改为2511107。

## [RELY]

- `proc_list`、`current`、`idleproc`、`context`、`sync.h`。

## [GUARANTEE]

```c
/* proc.c */
void cpu_idle(void);
void proc_run(struct proc_struct *proc);

/* sched.c */
void schedule(void);

/* switch.S */
void switch_to(struct context *from, struct context *to);
```

## [SPECIFICATION]

- **PCB与页表就绪**：前置条件为 PCB 与页表初始化完成。
- **`cpu_idle`**：空闲循环见 `need_resched` 则调用 `schedule()` 调度。
- **`schedule`**：关中断清标记，轮转选就绪线程，无则回 `idle`，更新 `runs`。
- **`proc_run`**：异进程更新 `current`、写 Sv39 `satp` 并刷 TLB。
- **`switch_to`**：按 `context` 保存恢复 `ra`/`sp`/`s0–s11`，返回后恢复中断。
````

#### 实现迭代过程

本模块的实现和联调分为两次迭代。

**第一次迭代**实现四个接口，定向编译通过。整机编译时，旧 `sbi.c` 与头文件内联接口发生重定义，两个 `readline.c` 也造成链接冲突。

**第二次迭代**保留头文件的 SBI 接口和 `kern/libs/readline.c`，去掉旧的重复源文件。QEMU 启动后，`initproc` 输出预期内容。教师评分中调度项为 `10/10`，整份 Lab4 为 `30/30`。

---

### Challenge：中断开关与多级页表

#### 1. `local_intr_save()` 和 `local_intr_restore()` 如何开关中断

`local_intr_save(intr_flag)` 读取 `sstatus.SIE`，把原值存到 `intr_flag`。原来为 1 时，`intr_disable()` 清除 SIE，使当前 hart 在 S 模式下暂停响应 S 模式中断。原来为 0 时继续保持关闭。离开临界区时，`local_intr_restore(intr_flag)` 仅在原值为 1 时调用 `intr_enable()`，重新置位 SIE。

`schedule()` 调用 `proc_run()` 时会再次保存中断状态。外层已经关中断，内层保存的是 0，所以内层返回时中断仍关闭。外层最终根据自己的保存值恢复，避免内层提前打开中断。

#### 2. `get_pte()` 中两段代码为什么相似

| 分页模式 | 虚拟地址的索引与偏移 | 页表层数 | 每级页表项 |
|----------|--------------------|------------|------------|
| Sv32 | 10 + 10 + 12 位 | 2 | 1024 项，每项 4 字节 |
| Sv39 | 9 + 9 + 9 + 12 位 | 3 | 512 项，每项 8 字节 |
| Sv48 | 9 + 9 + 9 + 9 + 12 位 | 4 | 512 项，每项 8 字节 |

三种模式的基本页都是 4 KiB。查表时，每一级都用相应的虚拟页号字段选出一项，遇到非叶子项就沿着它指向的物理页进入下一张表，遇到叶子项则得到映射。分页模式的差异主要是重复查表的层数和每级使用的索引宽度。参考 [RISC-V 特权架构规范](https://docs.riscv.org/reference/isa/priv/supervisor.html)。

本实验的 `get_pte()` 定位 4 KiB 页的末级槽位。第一段用 `PDX1(la)` 查根表，第二段用 `PDX0(la)` 查中间表。每段都检查对应项是否有效，缺表时根据 `create` 决定是否分配、清零并挂接下一级表，最后用 `PTX(la)` 定位末级项。Sv32 走到 4 KiB 叶子需要下降一次，Sv39 下降两次，Sv48 下降三次，因此可以复用相同的遍历逻辑。

#### 3. 查找与分配是否需要拆开

本实验保留 `get_pte(pgdir, la, create)` 就够用。查询和建表共用同一条遍历路径，`create=0` 用于只查找，`create=1` 用于按需分配，调用简洁。缺点是同一接口的副作用取决于参数，且分配多级页表时可能中途失败。例如 Sv39 的第一张下级表已经挂接，第二张分配失败，就需要明确由哪一层回收刚分配的页。

代码规模扩大时，可以保留共用的内部遍历函数，在外层提供“只查找”和“按需建表”两个入口，并在建表路径集中处理失败回滚。这样能让调用者直接看出是否会分配内存，同时复用遍历代码。

---

## 五、测试与验证

<!--
提供 make qemu 的编译及运行截图，以及一张 make grade 的评分结果截图。
-->

**测试截图：**

![QEMU 启动截图](./images/lab4-qemu.png)

`make qemu` 输出了两项页表检查成功的信息，随后 `initproc` 打印 PID 1 和 `Hello world!!`。

![测试结果截图](./images/test_result.png)

`make grade` 的物理内存、进程创建和进程调度三项均通过，总分 `30/30`。

---

## 六、实验总结与收获

### 对操作系统的理解

| 知识点 | 原理与实验中的体现 |
|--------|--------------------|
| 多级页表与 TLB | 页表记录虚拟页到物理页的映射，TLB 缓存翻译。实验通过修改末级页表项和执行 `sfence.vma`，让处理器使用更新后的映射和权限。 |
| 物理页引用计数 | 一个物理页可以被多个虚拟地址引用。实验在创建和删除映射时维护引用计数，最后一个引用撤销后才回收物理页。 |
| PCB、内核栈与上下文 | PCB 保存线程的管理信息。内核线程共享内核页表，各自使用独立的内核栈。`context` 供线程切换使用，`trapframe` 保存 Trap 返回要恢复的现场，也可用于构造新线程的启动状态。 |
| 调度与中断保护 | 调度器选择下一个可运行线程，`switch_to()` 完成寄存器切换。本实验使用链表轮转选择，并用本地关中断保护当前 hart 上的切换过程。多核共享数据还需要跨核同步。 |

本实验未展开的 OS 知识包括用户程序加载与独立地址空间、进程退出及资源回收，以及时间片和优先级等调度策略。

### AI 协作开发的经验

提示词先写清修改位置和已有接口，再按正常、边界和失败情况明确结果。页映射要区分同页更新和异页替换，进程创建要写清失败清理，调度要覆盖无就绪线程及中断状态恢复。生成代码后先核对结构体和汇编的寄存器布局，再用 QEMU 和课程评分检查完整运行流程。
