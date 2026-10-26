#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <buddy_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    // lab2 function2-2 2511107
    pmm_manager = &buddy_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 2513411
static void page_init(void) {
    extern char end[];

    uint64_t memory_base = get_memory_base();
    uint64_t memory_size = get_memory_size();
    uint64_t memory_end;
    uintptr_t managed_end;
    uintptr_t kernel_end_pa;
    uintptr_t pages_pa;
    uintptr_t pages_end_pa;
    uintptr_t free_begin;
    size_t descriptor_count;
    size_t free_count;

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    if (memory_base == 0 || memory_size == 0 ||
        memory_base > (uint64_t)-1 - memory_size) {
        panic("invalid physical memory range in device tree");
    }
    memory_end = memory_base + memory_size;

    /* npage describes the range reported by the firmware, in page numbers. */
    npage = PPN(memory_end);
    if (npage <= nbase || memory_end <= DRAM_BASE ||
        memory_base > KERNEL_BEGIN_PADDR) {
        panic("physical memory does not contain the kernel");
    }

    /* This lab only maps the DRAM interval bounded by PHYSICAL_MEMORY_END. */
    managed_end = (memory_end < PHYSICAL_MEMORY_END) ? memory_end
                                                       : PHYSICAL_MEMORY_END;
    managed_end = ROUNDDOWN(managed_end, PGSIZE);
    kernel_end_pa = PADDR((uintptr_t)end);
    if (managed_end <= DRAM_BASE || kernel_end_pa >= managed_end) {
        panic("physical memory does not contain the kernel");
    }

    /* The descriptor array starts after the kernel and is page aligned. */
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
    pages_pa = PADDR((uintptr_t)pages);
    descriptor_count = npage - nbase;
    if (pages_pa < memory_base || pages_pa >= managed_end ||
        descriptor_count > ((uintptr_t)-1 - pages_pa) / sizeof(struct Page)) {
        panic("not enough physical memory for Page descriptors");
    }
    pages_end_pa = pages_pa + descriptor_count * sizeof(struct Page);
    if (pages_end_pa > managed_end) {
        panic("not enough physical memory for Page descriptors");
    }

    /* Start conservatively: firmware, the kernel, and descriptors are reserved. */
    for (size_t i = 0; i < descriptor_count; i++) {
        struct Page *page = &pages[i];
        page->flags = 0;
        page->property = 0;
        set_page_ref(page, 0);
        SetPageReserved(page);
        list_init(&page->page_link);
    }

    /* Only complete pages after the descriptor array enter the allocator. */
    free_begin = ROUNDUP(pages_end_pa, PGSIZE);
    if (free_begin > managed_end) {
        panic("no complete physical page remains after reservations");
    }
    free_count = (managed_end - free_begin) / PGSIZE;
    if (free_count == 0) {
        panic("no complete physical page remains after reservations");
    }

    cprintf("physical memory: [0x%016lx, 0x%016lx)\n",
            memory_base, memory_end);
    cprintf("managed memory ends at: 0x%016lx\n", managed_end);
    cprintf("Page descriptors: [0x%016lx, 0x%016lx)\n",
            pages_pa, pages_end_pa);
    init_memmap(pa2page(free_begin), free_count);
    cprintf("free physical pages: %lu\n", nr_free_pages());
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
