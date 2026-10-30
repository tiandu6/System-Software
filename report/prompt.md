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

## 功能模块二：中断与异常处理

以下提示词用于补齐本次集成依赖，原小组分工保持不变。

[PROMPT]

**任务**：修改当前 Lab3 工程的 `code/kern/trap/trap.c`，在 `lab3 function2 YOUR CODE` 处实现 `interrupt_handler()` 和 `exception_handler()`，补齐 Trap 入口调用的中断与异常处理依赖。

**操作要求**：必须修改真实文件，保留框架和教师测试。保留 `lab3 function2` 注释，将 `YOUR CODE` 替换为 `2511107`。新增内容限定在该标记区域；可在该区域引入 `sbi.h` 并定义需要的宏，不修改其他已有函数、SAVE_ALL、Trap frame 布局或模块一恢复代码，不实现 Challenge，不提交或推送 Git。

**输出要求**：以当前工程实际接口为准完成代码，解释时钟事件重设和异常 epc 更新的依据；运行完整构建、正常 QEMU 启动及原课程评分，按真实结果说明，不以独立测试替代集成评分。

[RELY]

- 读取 `trap.c`、`trap.h`、`libs/riscv.h`、`kern/driver/clock.h`、`libs/sbi.h`，确认实际常量和接口。
- `struct trapframe` 包含 `gpr`、`status`、`epc`、`badvaddr`、`cause`，各地址字段使用 `uintptr_t`；当前目标为 RV64。
- 已有接口与状态：
```c
void print_trapframe(struct trapframe *tf);
void clock_set_next_event(void);
extern volatile size_t ticks;
void sbi_shutdown(void);
#define TICK_NUM 100
static void print_ticks();
```
`print_ticks()` 已在同一文件中实现，每次输出一行 `100 ticks`，不得改动。
- 中断类型使用工程的 `IRQ_*` 常量；异常类型使用 `CAUSE_ILLEGAL_INSTRUCTION`、`CAUSE_BREAKPOINT`。
- 当前框架未定义关机输出次数；新增可被编译参数覆盖的 `SHUTDOWN_AFTER`，默认值为 10，即每 100 次输出一次，累计 10 次输出后关机。
- 已有 `trap_dispatch()` 根据 cause 最高位分发到两个处理函数。中断处理函数需要去掉该类型位，取得原因编号；保留已有分发函数。

[GUARANTEE]

必须实现：
```c
void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);
```

只允许新增模块所需宏或 static 辅助函数，不改变公开接口和已有测试。

[SPECIFICATION]

## interrupt_handler

**Pre-Condition**：
- tf 指向有效 Trap frame；时钟事件接口和计数器已由框架提供。
- 当前是内核处理路径，允许使用控制台和 SBI 接口。

**Post-Condition**：
- 按中断原因完成处理，不无故修改 epc 或通用寄存器现场。

**Case 1**：遇到 IRQ_S_TIMER，预约下一次事件，ticks 恰好增加 1；每达到 TICK_NUM 的整数倍调用 print_ticks()。累计输出达到 SHUTDOWN_AFTER 时调用 sbi_shutdown()。

**Case 2**：对框架列出的其他软件、定时器、外部等中断，输出对应类别提示，不误增加 S 模式时钟计数。其中 S 软件中断输出包含 `Supervisor software interrupt`。

**Case 3**：未知中断输出 `Unknown interrupt: cause N`，并调用 print_trapframe(tf)；即使传入的 cause 没有置最高位，未知编号也应正确显示。

**Requirements**：
- 每次 S 时钟中断都必须设置下一次事件，不能只累计计数。
- 默认关机阈值须允许课程持续时钟用例执行到 ticks=200。
- 不在处理函数中自行执行 sret，由统一返回路径完成恢复。

## exception_handler

**Pre-Condition**：
- tf 有效；在处理本实验非法指令和断点时，epc 所指向的指令可在当前内核映射中读取。
- 本实验需要跳过的指令为 16 位或 32 位，不要求实现更长指令的通用恢复。

**Post-Condition**：
- 按异常原因输出信息，完成实验要求的返回位置更新，或调用框架诊断输出。

**Case 1**：非法指令异常输出包含 `Illegal instruction at` 及发生地址，按实际 2/4 字节长度更新 epc。

**Case 2**：断点异常输出包含 `Breakpoint at` 及发生地址，按实际 2/4 字节长度更新 epc，使返回后继续执行后续指令。

**Case 3**：其他异常调用 print_trapframe(tf)，不擅自把未知异常也统一跳过。

**Requirements**：
- 根据指令编码判断本实验指令长度，不能将所有异常都固定推进 4 字节，也不能仅按异常编号猜测长度。
- 不修改 cause、其他保存寄存器或原有分发逻辑；不把兜底打印描述成通用异常恢复能力。
