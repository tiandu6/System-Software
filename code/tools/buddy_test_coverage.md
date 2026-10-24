# Buddy System 规格测试与验证记录

判定依据是任务给出的原始行为规格；测试不把现有实现的输出定义为预期。
本次只修改 `buddy_pmm.c` 的测试部分和测试用的 `string.h` 引入，替换独立入口
`test_buddy.c`，扩展 `test_buddy.py`。分配、释放、登记、查询、初始化及其生产辅助函数，
原有 `buddy_validate()`、管理器对象、公开接口、Page 布局、DTB 和其他分配器均未修改。
Makefile 已自动收集模块，不需要新构建规则。

## 实际职责与完整签名

以下函数均为 `buddy_pmm.c` 中的模块私有函数；没有公开私有状态：

```c
static void buddy_check(void);
static void buddy_test_invariants(struct buddy_test_fixture *f);
static void buddy_test_init_memmap(struct buddy_test_fixture *f);
static void buddy_test_alloc(struct buddy_test_fixture *f);
static void buddy_test_free_merge(struct buddy_test_fixture *f);
static void buddy_test_failure(struct buddy_test_fixture *f);
static void buddy_test_sequences(struct buddy_test_fixture *f);
static void buddy_test_public_api(struct buddy_test_fixture *f);
```

`buddy_test_invariants(NULL)` 检查真实管理器的结构和逐页状态；非空参数还检查
夹具独立维护的登记、空闲、存活 owner、引用计数模型。它不调用生产代码的
`buddy_validate()`、`buddy_contains()` 或 `buddy_ppn()`。

## 需求—夹具—操作—预期断言

表中区间 `[a,b)` 是 64 页物理对齐测试区内的描述符区间；所有物理对齐与伙伴
关系均经真实 `page2ppn()` 计算。不会将区间下标当作绝对物理页号。

| 需求 | 夹具 / 操作 | 独立预期断言 | 运行位置 |
|---|---|---|---|
| 空初始化，不改描述符 | 独立入口先设置 Reserved、ref=31 和链接快照，再调用真实 init 回调 | 空闲数为零、全部阶为空、每个描述符与快照完全相同 | 独立入口 |
| 空登记状态 | 隔离状态下尚未登记任何页 | 全部阶为空，空闲数为零，保留初值不变 | 内核两轮 |
| 单页、非二次幂、乱序登记 | 登记 `[19,20)`、`[3,16)`、`[40,47)`、`[17,19)` | 每次恰好增加 n；总数 23；所有页进入模型、ref 归零；孔洞页不变 | 内核两轮 |
| 后续登记不破坏分配 | 上述范围分配 1 页，再登记 `[24,32)` | 旧 owner 及所有未释放页仍有效 | 内核两轮 |
| 非对齐起点和绝对坐标 | 将私有描述符视图起点设为真实 arena+1，登记其前 13 页 | 依真实 PPN 检查每一阶；拒绝用视图下标对齐/XOR 代替物理坐标 | 内核两轮 |
| 跨登记调用合并 | 先登记 `[8,16)`，再登记 `[0,8)` | 合成唯一 16 页块；只有首项 Property=1、property=16 | 内核两轮 |
| 拆块 | 单个对齐 64 页块申请 1 页 | 根据实际返回 PPN，逐阶计算未选中伙伴区间；0..5 阶各有对应剩余块 | 内核两轮 |
| 二次幂成功请求 | 完整 64 页块中逐个申请并释放 2/4/8/16/32/64 页 | 成功、连续、非重叠、引用/标志正确，计数恰好减/增 n | 内核两轮 |
| 精确 n、即时归还尾部 | 唯一 4 页块申请 3 页；唯一 8 页块申请 5 页 | 分别只扣 3/5 页；余下 1/3 页立即可逐个分配，最终恰好耗尽 | 内核两轮 |
| 最小可用阶优先 | 同时存在 2/4/16 页块，请求 1/3/8 页 | 成功范围必须落在操作前最小可满足非空阶的某个块中，不预测同阶节点顺序 | 内核两轮 |
| 耗尽及有效范围不重叠 | 对齐 64 页区逐页申请 | 64 次成功均更新实际返回区间的 owner；随后单页失败 | 内核两轮 |
| 中间释放 | 申请 5 页，释放中间 3 页，再乱序释放两端 | 每次计数恰好变化；两端 owner 保留；ref=7 的释放页归零 | 内核两轮 |
| 跨相邻分配释放 | 唯一 8 页块分成两次 4 页分配，按实际地址排序后释放跨界 4 页 | 不假定先申请者在低地址；跨界范围归还，两侧各 2 页仍存活 | 内核两轮 |
| 相邻非伙伴与真伙伴 | 4 对齐区全部分配，只释放偏移 1、2，再释放 0 和 3 | 1/2 不能形成二页块；0/1 合并，再最终合为四页块 | 内核两轮 |
| 多级合并 | 64 页逐页分配，按六位位反转排列释放 | 每步检查模型和最大化合并；最后恢复 64 页块 | 内核两轮 |
| 未对齐、非二次幂释放 | 整个 64 页分配中先释放 `[3,16)` | 精确归还 13 页，无重叠/遗漏，其他页仍有 owner；清理后恢复整块 | 内核两轮 |
| 空洞边界 | 登记 `[0,7)` 与 `[8,16)`，保持第 7 页未登记 | 第 7 页包括 ref、flags、property、链接的初值不变，不能组成 16 页块 | 内核两轮 |
| 过大请求 | n=65、SIZE_MAX、SIZE_MAX-1、2^MAX_ORDER+1 | NULL；完整空闲集合、块首/阶及元数据、存活 owner 不变 | 内核两轮 |
| 真正碎片失败 | 64 页全部逐页分配，仅释放偶数偏移页 | 总空闲 32 页，但 2 页请求必须失败；状态不变 | 内核两轮 |
| 序列一致性 | 固定种子 0xc0ffee11，256 步，整段/部分释放及分配 | 按实际返回结果更新 owner；每步检查全部不变量；清理后恢复原 56 页登记集合 | 内核两轮 |
| 公开分发和接入 | 检查六个回调及 pmm_manager；公开接口申请/释放 3 页 | 指向 Buddy；计数 16→13→16；启动正常调用全部测试 | 内核两轮 |
| 最大有效阶 | 独立映射 32768 个描述符，登记/申请 32768 页，分成 32765+3 页释放 | 阶 15 确实参与；最大块恢复，不丢页或越界 | 独立入口 |
| 连续重复与存活分配保护 | 独立入口预留孔洞、保持一个 ref=11 的三页分配，连续调用 check 两次 | 共四轮恢复；原存活页 ref=11、保留页、空闲集合与计数不变；测试链表已清空 | 独立入口 |

所有关键操作后，独立不变量检查同时验证：

- 各阶块大小、正确阶号、绝对 PPN 对齐，双向链接互逆；遍历最多访问视图页数个节点。
- 访问链接描述符前做真实数组数值范围检查，真实数组长度始终为 `npage - nbase`。
- 块首标志与页数、内部页 property=0、空闲 ref=0、无 Reserved、无临时 Checked 残留。
- 页级覆盖不重叠、不重复、不遗漏；与已登记/存活 owner/未登记模型一致。
- 按块页数加权之和等于公开查询值与模型页数。
- 从观察到的块首图，独立用 `page2ppn(head) XOR (1 << order)` 拒绝仍可合并的同阶伙伴。

失败状态快照比较每页 flags/ref/property、空闲块首/阶和模型，不要求链表遍历顺序不变。
可满足性来自操作前实际存在的可用阶块，不由“总空闲页数足够”推断，也没有另写一套 Buddy。

## 隔离、容量与恢复

每轮先经真实 `alloc_pages(128)` 取得连续页面所有权，并保存该次分配后的全部 128 个
描述符元数据。在其中按实际 PPN 主动选取一个 64 对齐、64 页的子区间。
私有状态只切换到这个已拥有范围的视图，`pages/npage/nbase` 全程不变，因此所有真实
地址转换都使用有效描述符；不会把静态模拟指针交给内核真实转换函数。

原管理器的链表头始终保留在原对象中，不浅拷贝或移动它们。每项测试结束先释放模型中的
所有存活页；每轮结束清空临时链表头，恢复所有 128 个已拥有描述符，切回原管理器，
再用公开 `free_pages(owned,128)` 归还所有权。随后独立重新遍历，逐页核对原空闲集合、
计数、块首元数据，以及原存活/保留页的完整元数据。只有自由链表的节点次序不作承诺。
未留下指向临时测试状态的活动链表；独立入口也专门断言这一点。

`buddy_check()` 连续执行两轮完整流程，每轮都先取所有权、后恢复并比较。
必需的 128 页连续测试资源不足时明确 panic，不跳过核心测试后宣称通过。

工作区是有上限的静态数组：场景最多 64 页、所有权快照 128 页、全局快照/覆盖图最多
32768 页（当前 128 MiB 上界）。访问前检查容量；全局快照约 1.3 MiB，增加的是测试 BSS，
没有大规模栈数组、malloc 或页分配器元数据递归分配。

序列的完整日志包含种子、步骤、操作、偏移和 n。测试断言失败还输出场景与断言文本。
运行脚本只过滤终端中的逐步日志，完整内容保留在 `host.log` / `qemu.log`。

## 独立负向入口

脚本分别启动新进程，期望 SIGABRT（-6）：

- 前置条件/非法输入：`zero_alloc`、`zero_free`、`reserved_free`、`double_free`、
  `duplicate_registration`、`out_of_bounds`。它们不是正常功能承诺，不要求返回错误码。
- 故意破坏元数据，验证检查器能发现错误：`allocated_head_flag`、`wrong_order_size`、
  `free_nonzero_ref`、`stale_checked_bit`、`invalid_link`、`unmerged_buddies`。
  损坏链接先被数值范围检查拒绝，不向真实地址转换函数传入伪造指针。
- `insufficient_test_arena`：只有 8 页空闲时调用 check，必须明确报告构造资源不足。

正常启动不会触发这些负向场景；不会用未定义输入的结果评价正常分配功能。

## 实际验证记录

运行目录 `lab2/lab2_new`：

```bash
python3 tools/test_buddy.py
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
python3 tools/test_buddy.py --sanitize --qemu
```

两类命令均已运行并退出 0。最新完整 ASan/UBSan、交叉构建与启动产物在
`/tmp/ucore-buddy-test-5iuguynp/`，包含 `host.log`、`build.log`、`qemu.log` 和各负向入口日志。
脚本在未修改源码的临时副本中实际执行：

```text
make -j2 GCCPREFIX=/mnt/d/flies/os/riscv/riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin/riscv64-unknown-elf-
qemu-system-riscv64 -machine virt -nographic -bios default -device loader,file=bin/ucore.img,addr=0x80200000
```

构建退出 0；QEMU 完成两轮检查、进入内核空转循环后由脚本发送终止信号，进程退出 0。
关键实际日志：

```text
memory management: buddy_pmm_manager
buddy_check round=1 restored free=31576: PASS
buddy_check round=2 restored free=31576: PASS
buddy_check() succeeded!
check_alloc_page() succeeded!
```

已通过：正常规格测试、独立重复检查、13 个预期断言入口、ASan/UBSan、真实交叉构建与 QEMU 启动。
未运行：LeakSanitizer（当前 ptrace 环境不支持，显式关闭泄漏检测）；原 Best-Fit 专用 `make grade`
（要求选择 Best Fit，与本任务的 Buddy 接入不相容）。没有把编译成功当作启动通过。

当前测试范围内未发现生产分配器违反原始规格的场景，因此没有修改生产算法。
测试开发期间保留了两类回归：临时测试头未清理，以及检查器漏检持久 Checked 位；
二者先在独立入口失败，再仅修正测试代码，现已通过。它们不是生产分配算法的反例。
