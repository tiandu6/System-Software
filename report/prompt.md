# Lab3 提示词汇总

## 功能模块一：Trap 上下文恢复与 Trap 入口功能

**负责人：2511107-林永盛**

以下提示词根据本次模块一的真实接口和行为约束整理。Lab2 依赖迁移作为工程准备单独完成，不属于模块一的新增算法。

[PROMPT]

**任务**：在当前 uCore Lab3 工程中，修改 `code/kern/trap/trapentry.S`，完成“功能模块一：Trap 上下文恢复与 Trap 入口功能”。补全 `RESTORE_ALL` 汇编宏，以及 `__alltraps` 中从已有 `SAVE_ALL` 到 C 函数 `trap(struct trapframe *tf)` 的调用衔接，和已有 `__trapret`、`sret` 共同形成保存、处理、恢复、返回的闭环。

**操作要求**：必须修改项目中的实际文件，而不是仅展示代码。只在本模块两个 `lab3 function1 YOUR CODE` 标记位置补全，保留 `lab3 function1` 注释并将 `YOUR CODE` 替换为 `2511107`。不得修改 `SAVE_ALL`、`struct pushregs`、`struct trapframe`、`trap()` 的签名、现有 `__trapret` 返回路径和教师测试；不得实现队友负责的 `interrupt_handler()`、`exception_handler()` 或 Challenge，不提交或推送 Git。

**输出要求**：根据下面的 [RELY]、[GUARANTEE] 和 [SPECIFICATION] 完成文件操作，以当前工程的真实定义为准。说明修改位置、关键恢复顺序及实际验证结果。若完整构建受到模块二未完成的影响，应明确报告这一依赖；可以用独立测试入口验证本模块，但不得把测试入口写入正式模块二，也不得声称完整 Lab3 已通过。不要生成或伪造终端截图。

[RELY]

1. 首先读取实际文件：
   - `code/kern/trap/trapentry.S`：已有保存逻辑、待补全位置和返回路径；
   - `code/kern/trap/trap.h`：上下文结构和 C 层接口；
   - `code/libs/riscv.h`：寄存器宽度、内存访问宏和 CSR 定义；
   - `code/libs/defs.h`：`uintptr_t`；
   - `code/kern/trap/trap.c`：`trap()` 的调用约定和分发关系。

2. 当前目标为 RV64。相关宏的实际定义为：
```c
#define STORE sd
#define LOAD ld
#define LOG_REGBYTES 3
#define REGBYTES (1 << LOG_REGBYTES)
typedef uint64_t uintptr_t;
```
以上 STORE、LOAD、LOG_REGBYTES 对应 `__riscv_xlen == 64` 分支。

3. `struct pushregs` 按 x0 至 x31 的顺序包含 32 个 `uintptr_t` 字段：
```text
zero, ra, sp, gp, tp, t0, t1, t2, s0, s1,
a0, a1, a2, a3, a4, a5, a6, a7,
s2, s3, s4, s5, s6, s7, s8, s9, s10, s11,
t3, t4, t5, t6
```
`trap.h` 中的实际结构及接口为：
```c
struct trapframe {
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};
void trap(struct trapframe *tf);
```
因此，Trap frame 共占 36 * REGBYTES，即 RV64 下的 288 字节：
   - 槽位 0～31：对应 x0～x31；
   - 槽位 32：status；
   - 槽位 33：epc；
   - 槽位 34：badvaddr；
   - 槽位 35：cause。
槽位 i 相对帧基址的字节偏移为 i * REGBYTES。

4. 已有 `SAVE_ALL` 先将旧 sp 暂存在 `sscratch`，再在当前栈上分配 Trap frame。它将原 sp 写入槽位 2，并把 `sstatus/sepc/sbadaddr/scause` 的快照写入槽位 32～35；保存后当前 sp 指向帧基址，`sscratch` 为 0。不要把此时的 sp 当作被打断时的 sp。

5. 现有返回路径为：
```asm
__trapret:
    RESTORE_ALL
    sret
```
RISC-V 调用约定使用 a0 传递第一个参数，C 函数返回时应保持其调用前的 sp。本模块按已有框架提供可用、16 字节对齐的当前内核栈处理 Trap，不要求实现独立用户栈切换、进程调度或浮点上下文管理。

[GUARANTEE]

必须补全以下两个现有汇编接口，保留名称和布局：
```asm
.macro RESTORE_ALL
    # lab3 function1 2511107
.endm

.globl __alltraps
__alltraps:
    SAVE_ALL
    # lab3 function1 2511107
    # 调用现有 void trap(struct trapframe *tf)
```

- `RESTORE_ALL`：从当前 Trap frame 恢复通用寄存器和 Trap 返回所需的状态。
- `__alltraps`：在保存上下文后，将同一份 Trap frame 作为参数交给 `trap()`，返回后衔接已有恢复路径。
- 不增加或改变公开 C 接口；可在填空区域增加必要注释，不新增无关全局状态或硬编码返回地址。

[SPECIFICATION]

## RESTORE_ALL

**Pre-Condition**：
- 当前 sp 指向由已有 SAVE_ALL 形成的有效 Trap frame，所有槽位可读取。
- C 层处理已结束，返回用的 epc、status 以及通用寄存器槽位是有效状态；处理函数可能更新其中的内容。
- 当前处于 S 模式的 Trap 返回路径，尚未执行 sret。
- 帧内 status 的 SIE 位保持为 0，使恢复现场期间不响应 S 模式中断；返回后的中断开关状态通过 SPIE 和 sret 恢复。

**Post-Condition**：
- x1、x3～x31 从各自槽位恢复，x2 从槽位 2 恢复；x0 始终保持硬件规定的 0。
- sstatus、sepc 取自帧中当前的 status、epc，供紧随其后的 sret 使用。
- 栈指针不再指向本次 Trap frame，后续返回使用帧内记录的 sp。
- badvaddr 和 cause 作为诊断快照保留在帧中，不要求反写相应 CSR；不额外恢复 sscratch 中的旧 sp。

**Case 1**：C 处理函数未修改返回相关内容时，恢复保存的状态，使 sret 能返回被打断的执行流。

**Case 2**：C 处理函数修改了 epc 或其他允许修改的返回状态时，恢复修改后的值，而不是固定跳到原始异常位置，也不在恢复宏中自行计算异常指令长度。

**Requirements**：
- 在所有基于帧基址的内存读取完成之前，不能提前恢复 sp。
- 用作恢复 CSR 的临时通用寄存器，最终也必须恢复其帧内值，不能因临时使用而破坏被打断代码的寄存器。
- 不自行开启中断，不以普通 ret 替代 sret，不修改保存布局。
- RESTORE_ALL 负责准备返回状态；sret 对权限级和状态位的体系结构更新由现有返回指令完成，不等同于逐位保持 sstatus 不变。

## __alltraps

**Pre-Condition**：
- CPU 已进入 S 模式 Trap 入口，硬件已记录相关 Trap CSR。
- 当前栈有效、空间足够且对齐，已有 SAVE_ALL 可正常工作。
- C 层提供 void trap(struct trapframe *tf)，调用遵循 RISC-V ABI。

**Post-Condition**：
- 使用已有 SAVE_ALL 保存现场，只形成一份 Trap frame。
- trap() 的第一个参数准确指向这份帧，不传递帧副本或错误偏移。
- trap() 返回时，sp 仍指向相同帧基址，接着进入已有 __trapret，恢复后执行 sret。
- trap() 对帧内返回状态的合法修改不会被另一份旧副本覆盖。

**Requirements**：
- 进入 C 函数时保持 16 字节栈对齐；RV64 下帧大小为 288 字节，是 16 的整数倍。
- 不把 Trap 入口实现成不会返回的尾调用，不覆盖已有 C 分发逻辑，不增加另一份完整上下文。
- 保留原 SAVE_ALL 和 __trapret，不承诺当前框架尚未提供的 U 模式独立内核栈切换或中断嵌套支持。

## 验证要求

- 使用工程编译选项编译 trapentry.S，并检查目标代码中的参数传递、CSR 恢复及最后恢复 sp 的顺序。
- 验证进入 C 处理函数时的帧地址与布局、寄存器保存恢复、处理函数修改 epc 后的返回，以及多次 Trap 返回。
- 独立测试可以提供仅用于验证的 trap()；它验证模块一，不代表完成模块二。
- 完整工程 make、make qemu、make grade 需要两个模块及 Lab2 依赖都就绪；仅在实际成功后记录相应结论。

## 功能模块二：中断与异常处理功能

**负责人：2511592-陈逍宇、2513411-邱宇湛**

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
