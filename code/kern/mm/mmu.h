// 头文件保护：同一个源文件多次包含 mmu.h 时，只处理一次。
#ifndef __KERN_MM_MMU_H__
#define __KERN_MM_MMU_H__

// C 源文件需要 defs.h 中的类型定义，汇编预处理时跳过它。
#ifndef __ASSEMBLER__
#include <defs.h>
#endif /* !__ASSEMBLER__ */

// Lab4 使用 Sv39。虚拟地址 la 的低 39 位分成以下四段。
// 位号       [38:30]       [29:21]       [20:12]       [11:0]
// 用途       根页表索引    第二级索引    第三级索引    页内偏移
// 对应宏     PDX1(la)      PDX0(la)      PTX(la)       PGOFF(la)
// 对普通 4 KiB 页，先依次用三个索引查页表，最后用一次页内偏移。
// 例如 0x40403123 的三个索引依次是 1、2、3，页内偏移是 0x123。

// 先右移到最低位，再用 0x1FF 保留 9 位，得到 0 到 511 的索引。
#define PDX1(la) ((((uintptr_t)(la)) >> PDX1SHIFT) & 0x1FF)
#define PDX0(la) ((((uintptr_t)(la)) >> PDX0SHIFT) & 0x1FF)

// 取虚拟地址的第三级页表索引。
#define PTX(la) ((((uintptr_t)(la)) >> PTXSHIFT) & 0x1FF)

// 右移 12 位去掉页内偏移。传入物理地址时，得到物理页号。
#define PPN(la) (((uintptr_t)(la)) >> PTXSHIFT)

// 0xFFF 保留低 12 位，得到地址在 4 KiB 页内的字节偏移。
#define PGOFF(la) (((uintptr_t)(la)) & 0xFFF)

// 把三个索引移回各自的位置，再和页内偏移按位或，拼出虚拟地址的低 39 位。
#define PGADDR(d1, d0, t, o) ((uintptr_t)((d1) << PDX1SHIFT |(d0) << PDX0SHIFT | (t) << PTXSHIFT | (o)))

// Sv39 页表项中的物理页号位于 [53:10]，低 10 位存标志和软件保留位。
// 先清掉低 10 位，再左移 2 位，把物理页号放到物理地址的 [55:12]。
// 得到的页基址低 12 位全为零。对于 4 KiB 叶子项，再加上页内偏移即可。
#define PTE_ADDR(pte)   (((uintptr_t)(pte) & ~0x3FF) << (PTXSHIFT - PTE_PPN_SHIFT))
// 中间级页表项使用同样的格式，解码得到下一级页表的物理基址。
#define PDE_ADDR(pde)   PTE_ADDR(pde)

// 每项 8 字节，512 项刚好占用一个 4 KiB 物理页。
#define NPDEENTRY       512                    // 中间级页表的表项数
#define NPTEENTRY       512                    // 末级页表的表项数

#define PGSIZE          4096                   // 普通页大小，单位是字节
#define PGSHIFT         12                     // 2^12 = 4096
#define PTSIZE          (PGSIZE * NPTEENTRY)    // 一张末级页表覆盖 2 MiB 虚拟地址
#define PTSHIFT         21                     // 2^21 字节 = 2 MiB

#define PTXSHIFT        12                     // 第三级索引从虚拟地址的第 12 位开始
#define PDX0SHIFT       21                     // 第二级索引从虚拟地址的第 21 位开始
#define PDX1SHIFT		30                     // 根页表索引从虚拟地址的第 30 位开始
#define PTE_PPN_SHIFT   10                     // 物理页号从页表项的第 10 位开始

// 页表项的低 10 位。可以用 & 检查某一位，用 | 组合多个位。
#define PTE_V     0x001 // 页表项有效
#define PTE_R     0x002 // 可读
#define PTE_W     0x004 // 可写，须同时设置 R
#define PTE_X     0x008 // 可执行
#define PTE_U     0x010 // 允许用户态按 R/W/X 权限访问
#define PTE_G     0x020 // 全局映射，可跨地址空间复用该翻译
#define PTE_A     0x040 // 访问标志
#define PTE_D     0x080 // 写入标志
#define PTE_SOFT  0x300 // 供操作系统使用的第 8、9 位

// V 为 1 且 R/W/X 全为 0 时，这一项指向下一级页表。
#define PAGE_TABLE_DIR (PTE_V)

// 叶子页表项的常用权限组合，均包含有效位 V。
#define READ_ONLY (PTE_R | PTE_V)
#define READ_WRITE (PTE_R | PTE_W | PTE_V)
#define EXEC_ONLY (PTE_X | PTE_V)
#define READ_EXEC (PTE_R | PTE_X | PTE_V)
#define READ_WRITE_EXEC (PTE_R | PTE_W | PTE_X | PTE_V)

// 允许用户态读、写和执行的权限组合。
#define PTE_USER (PTE_R | PTE_W | PTE_X | PTE_U | PTE_V)

#endif /* !__KERN_MM_MMU_H__ */
