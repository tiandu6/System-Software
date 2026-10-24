# SLUB 原始规格测试：覆盖关系与实际结果

## 修改范围与入口

本任务只扩展测试，没有修复或改写生产算法：

- `kern/mm/slub.c`：后端观察器接入，以及包含私有测试文件替代旧 `slub_check()`。
- `kern/mm/slub_test.inc`：全部模块内测试与记录表，在同一翻译单元访问私有状态。
- `tools/test_slub.c`：独立映射环境、真实 Buddy 后端、负向入口。
- `tools/test_slub.py`：普通/注入构建、日志保存、实际内核构建及运行。
- 本覆盖报告。

生产尺寸类、元数据结构和布局计算、初始化、kmalloc/kfree/reclaim 的算法正文保持逐字一致。
Buddy、PMM、Page、DTB、启动调用位置、头文件接口和 Makefile 均未修改。

后端包装器仍按原参数调用 `alloc_pages/free_pages`；观察器在 `slub_check()` 外立即返回，
不申请元数据、不注入失败、不改变返回值。原有 `SLUB_TEST` 宏仅在测试构建启用 fail-next。
观察器在实际申请成功后记录首页/页数，在实际归还前核对配对、活对象和引用解除。

要求的函数全部保留原名称，完整签名为：

```c
void slub_check(void);
static void slub_test_init(void);
static void slub_test_size_boundaries(void);
static void slub_test_lifecycle(void);
static void slub_test_data_integrity(void);
static void slub_test_large_alloc(void);
static void slub_test_failure(void);
static void slub_test_reclaim(void);
static void slub_test_sequences(void);
static void slub_test_invariants(void);
```

它们定义在 `slub_test.inc`，通过 `slub.c` 的私有 include 编译。
原启动流程仍实际调用公开 `slub_check()`，没有替换为仅打印成功的入口。

## 独立判据及夹具

1. 尺寸类预期使用测试自身的字面表：16/32/64/128/256/512/1024/2048。
   头部对齐与页数预期用实际结构的 `sizeof` 和独立除法/余数计算，不调用生产布局函数。
2. 对象表记录指针、请求长度、预期尺寸类、分配代号、后端代号及存活状态。
   只在请求长度内按“分配代号 + 字节偏移”写入和检查模式，不读取已释放对象。
3. 后端表记录每次成功申请的真实 Page 首页、页数、映射地址、代号、存活状态。
   同时记录包装器请求次数、成功段数、归还段数和实际归还页数。
4. 所有后端范围先做数值检查：描述符数量是 `npage-nbase`，映射以 `pages[0] -> nbase`
   和保存的真实 `va_pa_offset` 建立。没有把伪造描述符交给内核地址转换。
5. 单个后端范围的所有权、对象区间、freelist 槽位和状态链表相互交叉验证；
   不另写一套 SLUB，不预测 freelist 槽位返回次序。
6. 活动引用可合法为空，也可在可分配 slab 间重定向。一般分配检查使用调用前实际观察到的
   active/partial/empty 候选集合；专门的优先级夹具只设置/清除合法的额外 active 引用，
   不修改任何 slab 归属、已用数或 freelist，不固定生产代码的重定向策略。

每轮开始先拒绝存在外来存活对象的环境，然后在观察器未启用时回收已有空 slab，再记录
真实页数基线。不会重置存活 SLUB 或释放非测试对象。该启动/空闲堆前置条件不满足时明确
报告错误，不能绕过核心场景并宣称通过；独立 `foreign_live` 入口专门验证这一点。

所有辅助存储是静态、有界的：1024 个对象记录、256 个后端记录、256 个槽位标记，
以及最多 32768 页的受保护 Page 字段快照和压力页指针表。写入前检查容量，约使用
1.6 MiB 测试 BSS，没有大规模内核栈数组，也不通过被测 kmalloc 申请记录表。
首次冷初始化在独立入口覆盖；内核正常启动中 SLUB 已先初始化，检查的是幂等调用。

## 需求—场景—操作—断言

| 需求 / 测试职责 | 场景与操作 | 独立预期断言 |
|---|---|---|
| `init`：首次初始化 | 冷进程中首次调用；记录调用次数、页数及缓存 | 初始化不申请后端页；所有缓存为空，布局合法 |
| `init`：幂等 | 同时保留 33、9001 字节对象，再初始化两次 | 指针、数据、计数、归属保持有效；没有后端申请/归还 |
| 零大小与 NULL | 冷/热状态、存在小/大对象时调用 0 和 NULL | 不申请/归还；缓存、头部、freelist 链接和准备状态快照完全不变 |
| `size_boundaries` | 每个 C 的 C-1/C/C+1，加 1/3/5/17/33/100/1000 | 最小足够尺寸类；2048 小对象、2049 大对象；16 对齐；全请求可读写且互不重叠 |
| `lifecycle`：单页复用 | 每类首对象后填满同一 slab，再多申请一个 | 首对象只取一页；填满期间不再调用后端；随后新增一页；对象数明显超过页数 |
| 状态转换 | 从 full 释放一个，再继续分配/释放 | inuse、freelist 与 empty/partial/full 一致；full 不能被 active 引用 |
| 容量为 1 的边界 | RV64 的 2048 类每页只容纳一个槽位 | 原始状态定义要求 full→empty；partial 在该类不可达，不错误强求 partial |
| 分配优先级 | 三 slab 中构造 active C、其他 partial A、empty B；随后构造 active=NULL | 当前 active 优先；无 active 时选 partial，再选 empty；不调用新后端，不要求具体槽位次序 |
| 空 slab 上限 | 释放多 slab 的所有对象 | 每类至多一个 empty；额外空 slab 已实际归还，且先移除所有引用 |
| `data_integrity` | 多个尺寸类、超过一页的 63 字节对象，同时保留大对象 | 按编号与偏移检查完整请求区域，含首尾字节；释放部分、其他分配/回收均不破坏存活数据 |
| `large_alloc`：典型大小 | 2049、PGSIZE-1、PGSIZE、PGSIZE+1、2*PGSIZE+17 | 头部和填充计入页数；真实后端记录与头部 Page/n 一致；请求区间不越界 |
| 大对象页边界 | 对齐头部偏移 H，测试 PGSIZE-H 与 2*PGSIZE-H 的前一字节、恰好、后一字节 | 独立计算 ceil((H+size)/PGSIZE)，验证跨页和不跨页边界 |
| 大对象指针释放 | 同时分配多个大对象，交错释放，中间插入其他类分配与 reclaim | 只传指针即释放完整真实后端段，恰好一次；其他对象数据不变 |
| `failure`：真实后端不足 | 小/大对象存活时用真实 Buddy 单页申请耗尽剩余页 | 新类、新满 slab 扩容、大对象均返回 NULL，无空壳/虚假计数/泄漏；存活数据不变 |
| 现成槽位不依赖后端 | 真实后端已耗尽，已有 partial slab 仍有槽位 | 申请成功且后端请求次数不变；回收压力页后相同扩容请求可成功 |
| 确定性注入 | `SLUB_TEST` 中分别设置 fail-next，新类、满 slab、large | 返回 NULL 且无成功段；关闭注入后重试成功；已有槽位不消耗 fail-next |
| 非法算术 | SIZE_MAX、其附近确实使 size+H 溢出的值 | 后端调用前 NULL；不回绕成小分配，不改变存活对象 |
| 可表示但超过后端 | SIZE_MAX-H、有效总页数*PGSIZE | NULL；允许预先拒绝或将独立计算的正确页数交给后端，不强制未承诺的早拒绝策略 |
| 取得页面后的失败 | 现有转换校验可失败：临时将 va_pa_offset 错开一字节 | small/large 各实际取得一次资源、归还一次；立即恢复映射后比较计数/归属/数据，再重试成功 |
| `reclaim`：全部空 slab | 各类保留一个空 slab | 返回 8，后端实际归还页数和 nr_free_pages 增量均为 8，相关活动引用清除 |
| `reclaim`：保留活对象 | 保留 100 和 9001 字节对象，同时形成其他 empty slab | 仅回收模型 owner 数为零的小对象段；活小对象/大对象数据保持；再次回收为零 |
| `sequences` | 固定种子 0x51ab2026，256 步，边界/奇数/跨页请求、随机合法释放、间歇回收 | 每步核对实际返回区间、所有活对象数据、不变量及页面守恒；NULL 不进入对象表 |
| 序列失败可复现 | 记录请求前和返回后，清理也记录操作 | 日志含种子、步号、操作、长度、相关指针；断言报告具体表达式 |
| `invariants`：状态与唯一归属 | 逐类、逐状态遍历 | 双向链接互逆、有上限；每个活后端头部恰好出现一次；状态与 inuse/容量一致 |
| `invariants`：freelist | 独立检查槽位区间、步长和逐槽位标记 | 16 对齐；不在元数据、不跨 slab、不指向活对象，无重复/环；free+live=容量 |
| `invariants`：后端与对象 | 后端事件代号与对象代号交叉核对 | 活后端不重叠；每个活对象恰好一个归属；任何已退休段不得被引用 |
| 全程页面守恒 | 每次公开操作及每轮完整清理 | nr_free_pages + 活后端页数 + 原始压力页数 = 基线；全部清理后无活记录，成功段数=归还段数 |
| 后端释放时机 | free 回调在调用真实 PMM free 之前扫描引用 | 对应 slab/list/active 已移除，段内没有任何存活对象；Page 首页/n 匹配，不能二次释放 |
| Page 受保护字段 | 后端取得后快照，归还前及不变量中核对 | flags/property/page_link 不被 SLUB 改写；原规格未规定额外 Page.ref 策略，未添加此约束 |

不变量判据不调用生产布局函数；预期回收集合来自对象表的 owner 数，不直接相信 empty 标志。
所有链表/freelist 读取先检查活后端范围，遍历受记录容量或槽位容量约束，不能因环无限遍历。

## 真实内核、受控测试与负向测试

### 真实 Buddy 内核

普通构建不定义 `SLUB_TEST`。完整自检仍包括所有职责函数。
`slub_test_failure` 的确定性失败来自真实内存压力：将剩余 Buddy 页面合法取出并保存在
有界静态指针表，验证后端为空，再测试新建/扩容/large 失败及已有槽位复用，最后逐页归还。
不重置 Buddy，不改算法或存活分配；压力页不会作为 SLUB 后端段误记。

每次 `slub_check()` 运行两轮，所有必需断言、清理和基线恢复之后才打印成功。
回收前移除引用在后端真实释放前观测，不靠再次分配覆盖旧内容来判断。

### 受控构建

独立入口用真实可读写、页对齐 mmap 内存，明确建立 `pages[0] -> nbase` 与直接映射偏移；
仍链接实际 Buddy 和 PMM 分发。首次冷初始化和实际 3/5 页契约均在该环境验证。
`SLUB_TEST` 增加 fail-next，普通内核没有注入该分支。

转换失败测试不增加新的生产失败步骤：已有 `slub_page_address` 数值/对齐检查是可失败步骤。
测试临时改变映射变量，观察器始终按保存的真实映射记账；恢复变量后才检查结果。
没有通过这种方式改变真实页表、使用假描述符或访问错位地址。

### 11 个独立负向入口

每个在独立子进程运行，明确期望 SIGABRT(-6) 与对应测试断言：

- `freelist_cycle`、`freelist_live`、`freelist_cross_slab`；
- `wrong_capacity`、`wrong_state`、`active_full`；
- `retired_reference`、`backend_pair`、`backend_duplicate`；
- `noop_state`：有效 active 引用改变可被“不改变状态”快照发现；
- `foreign_live`：有外来活对象时拒绝启动测试，不能重置/释放它们或宣称通过。

这些是故意破坏夹具或检查测试前置条件的入口，不是生产算法缺陷。
没有把重复/内部指针 kfree 混入正常测试，也没有要求非法输入返回错误码。

## 清理与限制

- 对象记录在 kfree 调用前交出所有权；不再读取其数据，但仍检查真实释放不能牵连其他活对象。
- 每阶段关闭 fail-next，释放全部本阶段测试对象，回收所有 empty slab，并再回收一次验证零。
- 压力页全部归还，映射变量恢复，后端活记录清空，页数恢复基线，再关闭观察器。
- 使用单 hart 串行场景，无并发分配、中断处理或其他客户端操作。
- 测试入口要求启动/空闲堆；外来存活分配不会被隔离重置，会明确拒绝测试。
- 辅助容量或核心分配不足会断言失败，不跳过场景后输出全部通过。

## 实际运行结果

工作目录 `lab2/lab2_new`，实际执行：

```bash
python3 tools/test_slub.py --normal --qemu
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
python3 tools/test_slub.py --sanitize --qemu --instrumented-kernel
python3 tools/test_buddy.py
```

以上最终均退出 0。主机每次调用两次 `slub_check()`，共四轮完整清理；两种真实 QEMU
内核各完成两轮自检。所有普通检查、受控失败检查及 11 个预期负向入口通过。
原 Buddy 规格测试和原内核 Buddy 自检均通过，ASan/UBSan 通过。

普通最终产物：`/tmp/ucore-slub-test-r51lhvvj/`；
注入版最终产物：`/tmp/ucore-slub-test-99qupuos/`。
分别包含 `host.log`、`build.log`、`qemu.log` 和各负向入口日志。

脚本在未修改的临时源码副本中实际执行：

```text
make -j2 GCCPREFIX=/mnt/d/flies/os/riscv/riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin/riscv64-unknown-elf-
make -j2 GCCPREFIX=/mnt/d/flies/os/riscv/riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin/riscv64-unknown-elf- DEFS=-DSLUB_TEST
qemu-system-riscv64 -machine virt -nographic -bios default -device loader,file=bin/ucore.img,addr=0x80200000
```

两种构建均保持原有警告检查并退出 0。QEMU 在完成检查、进入内核空转循环后由脚本终止，
均退出 0；没有把编译通过当作启动通过。两种实际日志都包含：

```text
memory management: buddy_pmm_manager
buddy_check() succeeded!
check_alloc_page() succeeded!
slub_check round=1 restored free=31161: PASS
slub_check round=2 restored free=31161: PASS
slub_check() succeeded!
```

普通版日志还包含 `slub_test_failure: PASS (real Buddy pressure + rollback)`；
注入版增加 `fail-next`。序列完整日志保存请求与返回地址，终端摘要只过滤逐步 trace 行。

已通过：上述规格场景、重复自检、正常/受控后端、负向检查器验证、ASan/UBSan、构建和启动。
未解决失败：无。当前测试范围内没有发现生产 SLUB 违反原始规格的最小复现场景，
因此没有修改任何生产算法来使测试通过。

未运行：LeakSanitizer（当前 ptrace 环境限制，显式关闭泄漏检测）、SMP/中断重入测试
（原规格排除）、原 Best-Fit 专用评分（要求选择 Best Fit，与实际 Buddy 选择不同）。
