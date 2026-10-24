# 单核教学版 SLUB：设计与验证

实现目录：`lab2/lab2_new`。仅支持单 hart 串行调用，禁止在中断处理程序中调用。
不提供 SMP、无锁或中断重入保证；没有自定义缓存公开接口、构造函数或 NUMA 支持。

## 文件与启动接入

- `kern/mm/slub.h`：声明 `slub_init/kmalloc/kfree/slub_reclaim/slub_check`。
- `kern/mm/slub.c`：分配器、私有后端封装和初步启动自检。
- `kern/init/init.c`：在原 `pmm_init()` 后调用 `slub_init()`、`slub_check()`。
- `tools/test_slub.c`、`tools/test_slub.py`：实际 Buddy 后端的独立集成测试及运行入口。

Makefile 原有 `kern/mm/*.c` 自动收集规则直接包含 SLUB，没有修改构建规则。
Buddy、pmm_manager 接口、Page 布局、DTB、页描述符布局和原 Buddy 测试均保持原样。

启动顺序为：DTB → pmm_init（含原 Buddy 自检）→ slub_init → slub_check → 内核空转。
源码核对和实际集成测试均确认 Buddy 对外交付恰好 n 页，并支持原始 n 及合法子范围释放。

## 尺寸类与布局

缓存描述符是静态数组，`slub_init()` 只初始化这些描述符和链表，不申请后端页。
重复初始化立即返回，保留所有活动引用、链表和存活分配。

小对象每个 slab 使用一个物理页。RV64 下 `struct slub_slab` 大小为 64 字节，
对象区偏移也为 64 字节。头部包含 magic、缓存指针、后端 Page 指针、freelist、
唯一状态链表项、容量、已用数和状态。

| 请求范围（字节） | 槽位可用容量 | 每页对象数 |
|---|---:|---:|
| 1～16 | 16 | 252 |
| 17～32 | 32 | 126 |
| 33～64 | 64 | 63 |
| 65～128 | 128 | 31 |
| 129～256 | 256 | 15 |
| 257～512 | 512 | 7 |
| 513～1024 | 1024 | 3 |
| 1025～2048 | 2048 | 1 |

实现按实际 `sizeof(header)` 向上对齐到 16，再以
`(PGSIZE - offset) / class_size` 计算容量，没有忽略元数据。
页起点、对象区偏移和槽位步长均满足 16 字节对齐，全部槽位位于所属页内。

每个空闲对象的起始 `sizeof(void *)` 字节保存下一个空闲对象指针。
分配时将对象从 freelist 弹出；整个槽位立即归调用者使用，不再读取其中的旧链接。
不保证数据清零。只有再次 kfree 后，才将该槽位的开头用于链接。

## 活动引用与状态转换

每个尺寸类有 empty、partial、full 三条互斥链表；每个 slab 只有一个状态链表项。
active 只是额外引用，非空时必须属于该尺寸类并且仍有空闲对象。

分配顺序：active → partial → empty → 新建单页 slab。
取出槽位后增加 inuse，立即重新归类。变满时清理指向它的 active 引用。
释放对象后减少 inuse 并压入 freelist，立即从 full/partial 转入适当状态。

每类最多保留一个 empty slab。新出现的额外 empty slab 在归还之前先移除链表项、
清除 active 引用并保存后端 Page；归还后不再访问该页上的头部。
已有可用 slab 的小对象分配不申请后端页。

## 大对象及指针释放

大于 2048 字节走大对象路径。RV64 大对象头部为 48 字节，包含 magic、
后端首页、实际申请页数、原始请求大小和存活大对象链表项。
有效载荷固定从首个后端页的偏移 48 开始，满足 16 字节对齐。

先检查 `size + aligned_header` 加法，再用商和余数计算页数，避免向上取整加法溢出。
页数超过有效描述符数量或框架物理窗口能力时立即返回 NULL，其余交给 Buddy 判断
连续块可用性；后端失败也返回 NULL。

对于合法的原始指针，kfree 向下按页对齐即可找到首个页内的头部。
magic 区分小对象 slab 和大对象。小对象验证槽位偏移后归还 freelist；
大对象验证固定有效载荷偏移，按头部保存的 `(Page *, nr_pages)` 一次性释放。
不需要调用者提供 size，也不需要修改 Page 的 flags/property/page_link。
重复释放、内部指针及其他来源指针不属于合法输入。

## 地址转换、失败处理与回收

没有使用被注释的 KADDR。私有转换先验证描述符地址、对齐、数组长度
`npage - nbase`、物理页号和物理内存窗口，再计算 `page2pa(base) + va_pa_offset`。
同时检查地址加法、页范围和直接映射对齐；读取释放指针的页头前也先检查数值范围。

`slub_backend_alloc(n)` 和 `slub_backend_free(base,n)` 在正常构建中直接调用
现有 `alloc_pages/free_pages`。只有 `SLUB_TEST` 构建启用调用计数与 fail-next 注入。
新 slab/大对象在完整初始化后才发布到链表。后端失败不读取 NULL；
已取得页面但地址转换失败时，按相同首页和页数归还，缓存保持原状。

`slub_reclaim()` 只释放各类保留的 empty slab，返回实际归还的页数。
存活小对象所在 slab 和所有存活大对象不会被回收。再次回收没有新空 slab 时返回零。

启动 `slub_check()` 在客户端使用堆之前运行，检查零大小/NULL、小对象与非二次幂请求、
大对象、对齐、无重叠、读写、重复初始化、两页 slab 生命周期和最终页面恢复。
自检本身要求足够测试内存，所有断言及回收完成后才打印成功。

## 测试及实际结果

独立入口将主机 mmap 页面显式映射到模拟物理窗口，链接实际 Buddy 和实际 PMM 分发。
测试首先实际验证 3/5 页精确交付与释放，再验证 SLUB：

- 全部尺寸类和完整槽位容量、16 字节对齐、同页复用、freelist、三状态及额外空 slab 释放。
- 活动 slab、已有 partial、已有 empty 的选择优先级，不产生多余后端申请。
- 2049/4096/4097/9001 字节大对象、固定偏移归属恢复和完整请求区间读写。
- 0、SIZE_MAX、接近上限值、超过映射能力请求，以及真实后端耗尽。
- 创建新 slab/大对象前的注入失败、已有满 slab 时的失败、后端成功后转换失败回滚。
- 保持其他存活对象内容不变；1500 次固定种子混合申请/释放与中途 reclaim。
- 后端跟踪逐段验证申请/释放首页与页数一致、每段只释放一次，且 SLUB 未修改 Page 元数据。

实际执行命令（目录 `lab2/lab2_new`）：

```bash
python3 tools/test_buddy.py
python3 tools/test_slub.py
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tools/test_slub.py --sanitize
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tools/test_slub.py --sanitize --qemu
```

以上均退出 0。原 Buddy 规格测试和预期断言入口通过；SLUB 主机测试、ASan/UBSan、
保留原编译警告检查的完整交叉构建和 QEMU 实际启动均通过。

主机测试最新日志分别位于 `/tmp/ucore-slub-test-1m3uzfvy/host.log` 和
`/tmp/ucore-slub-test-ys2y6f6n/host.log`。完整构建及启动产物位于
`/tmp/ucore-slub-test-r4o71jd7/` 的 `build.log`、`qemu.log`。
最后新增的选择优先级测试仅改变主机测试文件，正常内核实现与已启动验证版本相同。

运行脚本在临时源码副本中实际执行：

```text
make -j2 GCCPREFIX=/mnt/d/flies/os/riscv/riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin/riscv64-unknown-elf-
qemu-system-riscv64 -machine virt -nographic -bios default -device loader,file=bin/ucore.img,addr=0x80200000
```

构建退出 0；QEMU 在检查完成后的内核空转循环中由脚本终止，进程退出 0。
关键实际日志：

```text
memory management: buddy_pmm_manager
buddy_check round=1 restored free=31575: PASS
buddy_check round=2 restored free=31575: PASS
buddy_check() succeeded!
check_alloc_page() succeeded!
slub_check() succeeded!
```

只读代码审查未发现需要修正的实现问题。当前没有未解决的构建或测试失败。
未运行：LeakSanitizer（当前 ptrace 环境不支持，显式关闭泄漏检测）、SMP/中断重入测试
（不属于支持范围）、原 Best-Fit 专用评分（与当前 Buddy 选择条件不同）。
