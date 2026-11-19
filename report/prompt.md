# Lab4 提示词汇总

## 功能模块一：

**负责人：2511592-陈逍宇**

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

## 功能模块二：中断与异常处理功能

**负责人：**

以下提示词根据实验网页要求、当前工程接口及本次真实验证整理。

[PROMPT]

**任务**：在当前 uCore Lab3 工程中，修改 `code/kern/trap/trap.c`，完成“功能模块二：中断与异常处理功能”，实现 `interrupt_handler(struct trapframe *tf)` 和 `exception_handler(struct trapframe *tf)`，使其与已有的 Trap 分发、上下文恢复和 `sret` 返回流程配合。

**操作要求**：必须修改项目中的实际文件。新增生产代码限定在 `lab3 function2 YOUR CODE` 标记区域，保留 `lab3 function2` 注释，并将 `YOUR CODE` 替换为 `2511592 2513411`。可以在该区域引入 `sbi.h`、定义本模块需要的宏。不得修改 `print_ticks()`、`trap_dispatch()`、`trap()`、`SAVE_ALL`、`RESTORE_ALL`、Trap frame 布局或教师测试；不得实现 Challenge，不提交或推送 Git。

**输出要求**：依据下面的真实依赖和行为规格实现，说明原因编号提取、下一次时钟事件设置、异常指令长度判断和返回地址更新的依据。实际执行构建、QEMU 启动及原课程评分，记录命令、结果和环境问题。补充验证应使用临时工程副本，不把测试入口写入正式启动流程；保留真实日志，截图应注明取材方式。

[RELY]

1. 先读取工程中的 `trap.c`、`trap.h`、`libs/riscv.h`、`libs/defs.h`、`kern/driver/clock.h`、`kern/driver/clock.c`、`libs/sbi.h`、`tools/grade.sh` 和 `tools/test_lab3.c`，以实际定义为准。

2. 当前目标为 RV64，`uintptr_t` 为 64 位。已有上下文结构如下：

```c
struct trapframe {
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};
```

`cause` 最高位表示中断，其余位为原因编号；同步异常的最高位为 0。已有 `trap_dispatch()` 通过 `(intptr_t)tf->cause < 0` 分发。两个处理函数只处理 C 层行为，由模块一从同一份帧恢复现场并执行 `sret`。

3. 已有接口和状态：

```c
void print_trapframe(struct trapframe *tf);
void clock_set_next_event(void);
extern volatile size_t ticks;
void sbi_shutdown(void);
#define TICK_NUM 100
static void print_ticks();
```

`clock_init()` 将 `ticks` 初始化为 0，并预约第一次时钟事件；`clock_set_next_event()` 通过 SBI 设置下一次事件。`print_ticks()` 已在同一文件实现，普通构建每次输出一行 `100 ticks`；保留其原有 `DEBUG_GRADE` 条件行为。

4. 工程提供 `IRQ_U/S/H/M_SOFT`、`IRQ_U/S/H/M_TIMER`、`IRQ_U/S/H/M_EXT`，以及 `IRQ_COP`、`IRQ_HOST` 常量。它们表示现有头文件列出的原因编号；按这些符号分类输出，不据此声称所有来源都能实际委托到当前 S 模式内核。

5. 异常原因使用 `CAUSE_ILLEGAL_INSTRUCTION` 和 `CAUSE_BREAKPOINT`。本实验要求恢复的指令限定为 16 位和 32 位；读取 `epc` 处的首个 16 位半字，低两位不等于 `11` 时长度为 2 字节，在本实验的指令集合中低两位等于 `11` 时长度为 4 字节。不扩展为对 48 位及更长指令的通用处理。

6. 当前框架没有定义 `SHUTDOWN_AFTER`。在本模块区域中用 `#ifndef` 提供默认值 10，允许通过 `-DSHUTDOWN_AFTER=1` 等编译参数覆盖；其含义是统计输出次数，要求为正整数。默认配置第 1000 次 S 时钟中断完成第 10 次输出后关机，课程持续时钟测试需要运行到 `ticks=200`。

[GUARANTEE]

必须实现以下两个公开接口，保留名称、参数和返回类型：

```c
void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);
```

- `interrupt_handler()`：根据原因分类处理中断，S 时钟中断完成重设事件、计数、周期输出和达到阈值后的关机。
- `exception_handler()`：非法指令和断点输出地址并正确推进 `epc`；其他异常输出完整帧。
- 除异常分支对 `tf->epc` 的规定更新外，不修改保存的通用寄存器、`status`、`badvaddr` 和 `cause`，也不改变现有分发与返回接口。

[SPECIFICATION]

## interrupt_handler

**Pre-Condition**：
- `tf` 指向有效、可读取的 Trap frame，执行环境允许使用控制台和 SBI 接口。
- 正常调用来自 `trap_dispatch()`；直接测试也可能传入不带中断最高位的原因编号。
- 正常时钟路径中，`clock_init()` 已执行，`ticks` 为自初始化后收到的 S 时钟中断次数，测试期间不发生计数溢出。
- `TICK_NUM=100`，`SHUTDOWN_AFTER` 为正整数。

**Post-Condition**：
- 使用去除最高位后的局部原因编号进行分类，保留 `tf->cause` 原值。
- 若返回，`tf` 的全部字段保持不变；不自行改变中断的返回地址。
- 只有 `IRQ_S_TIMER` 改变 `ticks`；其他原因不会触发时钟统计或关机逻辑。

**Case 1**：`IRQ_S_TIMER`。
- 每次调用 `clock_set_next_event()` 预约下一次事件，`ticks` 恰好增加 1。
- 增加后的 `ticks` 为 `TICK_NUM` 整数倍时，调用已有 `print_ticks()`。
- 累计输出次数达到 `SHUTDOWN_AFTER` 后，调用 `sbi_shutdown()`；默认在第 10 行 `100 ticks` 输出后关机。

**Case 2**：框架列出的其他软件、定时器、外部、协处理器和主机中断。
- 输出相应类别提示；S 软件中断必须输出 `Supervisor software interrupt`。
- 这些分支只做实验要求的类别识别和提示，不增加 `ticks`，不额外实现设备应答或中断控制器驱动。

**Case 3**：未知中断。
- 输出 `Unknown interrupt: cause N`，其中 `N` 是无符号原因编号，再调用 `print_trapframe(tf)`。
- 直接传入 `15` 或带最高位的 `15` 时，均应输出原因 `15`，不能把类型位混进编号。

**Requirements**：
- 每次 S 时钟中断都重新设置下一次事件，不能只在首次或输出统计时设置。
- 原因编号的掩码与 `uintptr_t` 的宽度一致，不使用会破坏无标志直接测试输入的异或去位方式。
- 统计输出使用已有函数，保留默认配置运行到 `ticks=200` 的能力。
- 不在本函数中调用 `sret`、手工恢复寄存器或重新打开全局中断。

## exception_handler

**Pre-Condition**：
- `tf` 指向有效 Trap frame，`tf->cause` 为同步异常原因。
- 对非法指令和断点，`tf->epc` 指向可读取、至少按 2 字节对齐的指令半字；本实验待跳过的指令长度为 2 或 4 字节。
- 对其他异常，不假定 `epc` 可解引用，也不要求实现具体恢复策略。

**Post-Condition**：
- 非法指令和断点在输出原始发生地址后，将 `epc` 推进至该指令之后。
- 其他异常调用 `print_trapframe(tf)`，帧的所有字段保持不变。
- 所有分支均不修改 `cause`、`status`、`badvaddr` 或保存的通用寄存器。

**Case 1**：`CAUSE_ILLEGAL_INSTRUCTION`。输出 `Illegal instruction at 0x...`，地址为更新前的 `epc`；16 位非法指令推进 2 字节，32 位非法指令推进 4 字节。

**Case 2**：`CAUSE_BREAKPOINT`。输出 `Breakpoint at 0x...`，地址为更新前的 `epc`；16 位 `c.ebreak` 推进 2 字节，32 位 `ebreak` 推进 4 字节，使返回后继续执行后续指令。

**Case 3**：其他异常。仅调用 `print_trapframe(tf)` 后返回；不读取指令、不统一跳过，也不把诊断输出解释为异常已被修复。

**Requirements**：
- 通过首个 16 位半字的编码判断本实验中的 2/4 字节长度，不能固定 `epc += 4`，也不能仅根据异常种类猜测长度。
- 使用 16 位读取，允许 32 位指令从仅按 2 字节对齐的地址开始；不要直接依赖 32 位对齐读取。
- 地址输出保留 RV64 地址宽度，使用工程支持的 `%p` 或等效格式。
- 不直接修改硬件 `sepc`，由统一恢复路径采用帧内更新后的 `epc`。

## 验证要求

- 先完成完整构建，再运行正常 `make qemu` 和原课程 `make grade`。默认应输出 10 行 `100 ticks` 后自动关机；课程三项得分应按实际结果记录。
- 当前 GCC 14.2 汇编器不接受框架旧 CSR 名 `sbadaddr` 时，使用编译参数 `DEFS=-Dsbadaddr=stval` 做名称映射，不改模块一的保存布局。对评分子构建，应通过环境传递该参数，保留各评分宏的追加。
- Windows 检出可能使 `tools/grade.sh` 使用 CRLF；遇到 `Illegal option -` 或回车相关错误时，只转换行尾为 LF，保留教师测试内容和断言。
- 增补真实 16/32 位断点、16/32 位非法指令的 Trap 返回，以及未知原因、现场保持、仅按半字对齐的指令读取和关机阈值覆盖测试。测试在临时工程副本执行，生产启动代码不插入测试调用。
- 明确区分实际硬件 Trap、构造帧直接调用和课程测试的覆盖范围，只有实际执行成功后才写入报告。
