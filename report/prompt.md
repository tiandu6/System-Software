# Lab2 提示词汇总

## 功能模块二任务二：Best-Fit 连续物理内存分配器

负责人：2511107-林永盛。


[PROMPT]

**任务**：在当前 uCore Lab2 工程中，参考 `code/kern/mm/default_pmm.c` 的数据组织方式，在 `code/kern/mm/best_fit_pmm.c` 的 `lab2 function2-2 YOUR CODE` 标记处，实现完整的 Best-Fit 连续物理页分配器，并在 `code/kern/mm/pmm.c` 的 `init_pmm_manager()` 中选择 `best_fit_pmm_manager`。

**操作要求**：必须修改项目中的实际文件，而不是只展示代码。在标记区域中保留 `lab2 function2-2`，将 `YOUR CODE` 替换为 `2511107`。可以新增模块私有状态、宏和 static 辅助函数，不改变已有公开接口。保留 `basic_check()` 和 `best_fit_check()` 的内容，不修改测试断言、评分逻辑或测试脚本。不修改 First-Fit、模块一的 `page_init()`、Buddy、SLUB 或 Challenge，不提交或推送 Git。不要照搬参考文件中尾部插入后继续遍历的控制流；确保每次插入只执行一次。

**输出要求**：以真实头文件、链表实现和现成测试为准，使用下面的上下文、接口清单和行为规格实现代码。说明修改文件、算法选择、验证命令和实际结果。编译或集成若受未完成的 `page_init()` 阻碍，应如实报告依赖，并在不篡改正式代码和既有测试的前提下独立验证分配器；不得填充假日志、声称未执行的测试通过。

[RELY]

**工程及参考文件**：

- `code/kern/mm/default_pmm.c`：First-Fit 参考实现。
- `code/kern/mm/pmm.h`：分配器接口、页面引用计数和地址转换。
- `code/kern/mm/memlayout.h`：页描述符、空闲区类型、页面标志。
- `code/libs/list.h`：循环双向链表。
- `code/kern/mm/best_fit_pmm.h`：已声明 `extern const struct pmm_manager best_fit_pmm_manager;`。

与本任务直接相关的实际类型如下：

```c
struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

typedef struct {
    list_entry_t free_list;
    unsigned int nr_free;
} free_area_t;

struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};
```

**页面及地址约定**：

- `PGSIZE = 4096`，`PGSHIFT = 12`。每个 `Page` 描述符对应一个物理页；同一 `pages[]` 数组中相邻描述符对应相邻物理页，`base + n` 是向后移动 n 个描述符，不是地址加 n 字节。
- 只有空闲块首页的 `property` 记录块的页数，只有空闲块首页设置 `PageProperty` 并挂入空闲链表；块内部其他页不单独入链。
- `nr_free` 表示总空闲页数，不是空闲块数。分配返回页描述符，不返回原始物理地址。
- `PageReserved(p)` 表示保留页；`PageProperty(p)` 表示空闲块首页。可用 `SetPageProperty`、`ClearPageProperty`、`set_page_ref`、`page_ref`、`assert`。
- 链表可用 `list_init`、`list_empty`、`list_next`、`list_prev`、`list_add_before`、`list_add`、`list_del` 和 `le2page(le, page_link)`。`list_add` 在指定节点之后插入；`list_add_before` 在之前插入。
- 原有测试直接访问当前文件中的 `free_list` 与 `nr_free`，并且会临时保存/替换这些状态；实现须提供这两个名称，状态不能另存为测试不可见的副本。可仿照参考文件使用一个 static `free_area_t` 配合宏。
- 原有测试可能以不同粒度申请和归还合法的已分配页面；不要要求只能按先前完整分配块的同一边界归还。测试恢复环境时会整体归还先前已分配的连续范围。

[GUARANTEE]

必须实现的接口：

```c
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);

const struct pmm_manager best_fit_pmm_manager;
```

必须修改的现有接口：

```c
static void init_pmm_manager(void);  /* code/kern/mm/pmm.c */
```

将管理器指针设置为 `&best_fit_pmm_manager`，保留现有名称输出和初始化调用。注册结构的各回调必须分别对应上面五个接口，`.check` 指向原有 `best_fit_check`，名称为 `"best_fit_pmm_manager"`。若注册结构位于测试定义之前，可添加不改变函数内容的前置声明。

允许新增 static 辅助函数和私有宏、变量；不得改变 `struct Page`、`struct pmm_manager` 或已有测试。

[SPECIFICATION]

## 全局状态约束

- 空闲链表按物理页地址递增，每个空闲块恰有一个首页节点；块之间不重叠。
- 合法登记、分配和释放后，`nr_free` 等于各空闲块长度之和；测试临时替换链表的中间步骤以其自身约定为准，不额外强制跨步骤一致性。
- 登记或释放时合并相邻空闲块，使连续空闲范围不被无必要地分成多个节点。
- 页面管理信息的初始化不等于清空物理页的数据内容；本任务不要求擦除页面数据。
- 现有单核实验环境由调用方保证操作串行；不引入新的锁、动态堆分配或并发机制。

## best_fit_init

**Pre-Condition**：
- 在向此分配器交付物理页之前调用。

**Post-Condition**：
- 空闲链表成为合法空循环双向链表，总空闲页数为 0；可以开始登记空闲范围。

## best_fit_init_memmap

**Pre-Condition**：
- 管理器已初始化；`n > 0`；`base` 指向同一有效描述符数组中连续 n 页的起点。
- 这些页仍处于保留状态，且没有被本分配器登记、分配或与已有空闲区重叠。

**Post-Condition**：
- 这 n 页成为可分配页面，引用计数清零，总空闲页数恰好增加 n。
- 空闲链表仍有序、无重复节点；本范围与相邻空闲范围在连续时合并。

**Case 1**：
- 原链表为空时，登记为唯一空闲块，首页记录正确长度。

**Case 2**：
- 已有空闲块时，将范围登记到正确位置；不连续则保持独立，向前、向后或两侧连续则合并为一个块。

**Requirements**：
- 保留页检查失败或 `n=0` 属于调用约定错误，使用框架断言报告，不静默把保留的系统区域变为可分配页。

## best_fit_alloc_pages

**Pre-Condition**：
- 管理器已初始化，`n > 0`。

**Post-Condition**：
- 成功时恰好分配连续 n 页，返回首页描述符，分配范围不再作为空闲块出现，总空闲页数减少 n。
- 在所有能容纳 n 页的空闲块中选择长度最小者；长度相同时选择地址较低者。
- 失败时返回 `NULL`，空闲链表、块长度、标志和总空闲页数不变。

**Case 1**：
- 空链表、总空闲页不足，或总数足够但没有足够大的单个连续块时，返回 `NULL`。

**Case 2**：
- 最优块恰好为 n 页时，整块分配并移除对应空闲节点。

**Case 3**：
- 最优块大于 n 页时，从其低地址端分配 n 页，剩余部分成为有正确首页、长度和链表位置的空闲块。

**Requirements**：
- 不能在遇到第一个足够大的块时无条件结束查找；但找到恰好匹配的块后可以结束，因为不存在更小的合法候选。
- `n=0` 用断言拒绝，不产生零长度分配。

## best_fit_free_pages

**Pre-Condition**：
- `n > 0`；`base` 到 `base+n` 表示合法、连续、已分配且非保留的 n 页，不与已有空闲块重叠。
- 调用方保证没有使用中的引用，不重复释放。

**Post-Condition**：
- 本范围重新可分配，引用计数清零，总空闲页数恰好增加 n。
- 相邻空闲块合并，链表有序；只保留最终合并块首页的空闲块标志和节点。
- 合并只改变块组织方式，不重复增加总空闲页数。

**Case 1**：
- 与左右空闲块均不相邻时，插入为独立空闲块。

**Case 2**：
- 只与左侧或右侧空闲块相邻时，合并对应一侧，正确更新首页和长度。

**Case 3**：
- 同时与两侧相邻时，三个块合并为一个块，最终首页位于最低地址，两个旧边界不再作为独立节点。

**Requirements**：
- 链表相邻不等于物理页连续，合并必须依据页面范围相接。
- 不修改已经分配出去的相邻页，也不触碰原有测试函数。

## best_fit_nr_free_pages

**Pre-Condition**：
- 管理器已初始化。

**Post-Condition**：
- 返回当前总空闲页数，不改变任何页面或链表状态。

## best_fit_pmm_manager 与 init_pmm_manager

**Pre-Condition**：
- Best-Fit 各接口和原有检查函数已声明，公共接口与头文件一致。

**Post-Condition**：
- 常量注册结构的函数指针完整且类型正确。
- 内核通过统一分配接口实际使用 Best-Fit，并执行其原有检查函数，而非仍使用 First-Fit。

**Requirements**：
- 先通过原有 `basic_check`、`best_fit_check`，再在依赖齐备的情况下运行 `make`、QEMU 与 `make grade`。
- 建议额外验证：最佳块不在链首、等长候选、拆分、空链表、总数足够但不连续、首尾插入及前/后/双向合并。
- 不改测试来迎合实现；报告明确区分独立测试、完整内核测试和课程评分，记录真实通过或受阻状态。
