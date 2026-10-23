/*
 * Lab2 function2-2 2511107: isolated host-side allocator tests.
 *
 * This is a test driver, not a second allocator. It links the real
 * kern/mm/best_fit_pmm.c and uses the project's real types and list helpers.
 * It does not test RISC-V boot, page_init(), or the complete QEMU kernel.
 */
#include <pmm.h>
#include <best_fit_pmm.h>
#include <stdarg.h>

/* Avoid host headers: this project defines its own size_t and integer types. */
extern int printf(const char *format, ...);
extern int vprintf(const char *format, va_list args);
extern int puts(const char *text);
extern void exit(int status) __attribute__((noreturn));

#define TEST_PAGE_COUNT 128
#define RANDOM_ROUNDS 6000

static struct Page test_pages[TEST_PAGE_COUNT];
/* Oracle states: 0 = free, 1 = allocated, 2 = not registered/reserved. */
static unsigned char page_state[TEST_PAGE_COUNT];

struct Page *pages = test_pages;
const size_t nbase = DRAM_BASE / PGSIZE;
size_t npage = DRAM_BASE / PGSIZE + TEST_PAGE_COUNT;
uint64_t va_pa_offset;
const struct pmm_manager *pmm_manager = &best_fit_pmm_manager;

int
cprintf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    va_end(args);
    return result;
}

void
__panic(const char *file, int line, const char *format, ...) {
    printf("[HOST FAIL] %s:%d: ", file, line);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    puts("");
    exit(1);
}

struct Page *
alloc_pages(size_t count) {
    return pmm_manager->alloc_pages(count);
}

void
free_pages(struct Page *base, size_t count) {
    pmm_manager->free_pages(base, count);
}

size_t
nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

static void
verify_state(void) {
    list_entry_t *heads[TEST_PAGE_COUNT];
    unsigned int block_count = 0;
    size_t free_count = 0;

    for (unsigned int i = 0; i < TEST_PAGE_COUNT; i++) {
        assert(page_ref(&pages[i]) == 0);
        if (page_state[i] == 2) {
            assert(PageReserved(&pages[i]));
            assert(!PageProperty(&pages[i]));
        } else {
            assert(!PageReserved(&pages[i]));
        }

        if (page_state[i] == 0) {
            free_count++;
            if (i == 0 || page_state[i - 1] != 0) {
                unsigned int length = 0;
                while (i + length < TEST_PAGE_COUNT &&
                       page_state[i + length] == 0) {
                    length++;
                }
                assert(PageProperty(&pages[i]));
                assert(pages[i].property == length);
                heads[block_count++] = &pages[i].page_link;
            } else {
                assert(!PageProperty(&pages[i]));
            }
        } else {
            assert(!PageProperty(&pages[i]));
        }
    }
    assert(nr_free_pages() == free_count);

    /* Verify the real intrusive list through its public Page nodes. */
    if (block_count != 0) {
        list_entry_t *sentinel = list_prev(heads[0]);
        assert(sentinel != heads[0]);
        assert(list_next(sentinel) == heads[0]);
        assert(list_prev(sentinel) == heads[block_count - 1]);
        for (unsigned int i = 0; i < block_count; i++) {
            list_entry_t *previous = i == 0 ? sentinel : heads[i - 1];
            list_entry_t *next = i + 1 == block_count ? sentinel : heads[i + 1];
            assert(list_prev(heads[i]) == previous);
            assert(list_next(heads[i]) == next);
        }
    }
}

static void
reset_empty_pool(void) {
    pmm_manager->init();
    for (unsigned int i = 0; i < TEST_PAGE_COUNT; i++) {
        pages[i].flags = 0;
        SetPageReserved(&pages[i]);
        pages[i].property = 0;
        set_page_ref(&pages[i], 0);
        list_init(&pages[i].page_link);
        page_state[i] = 2;
    }
    verify_state();
}

static void
register_range(unsigned int start, unsigned int count) {
    assert(count > 0 && start + count <= TEST_PAGE_COUNT);
    for (unsigned int i = start; i < start + count; i++) {
        assert(page_state[i] == 2);
        assert(PageReserved(&pages[i]));
    }
    pmm_manager->init_memmap(&pages[start], count);
    for (unsigned int i = start; i < start + count; i++) {
        page_state[i] = 0;
    }
    verify_state();
}

static void
reset_full_pool(void) {
    reset_empty_pool();
    register_range(0, TEST_PAGE_COUNT);
}

/* Independent bitmap oracle; no free-list algorithm is copied here. */
static int
expected_allocation(unsigned int request) {
    int best_start = -1;
    unsigned int best_length = TEST_PAGE_COUNT + 1;
    unsigned int i = 0;
    while (i < TEST_PAGE_COUNT) {
        if (page_state[i] != 0) {
            i++;
            continue;
        }
        unsigned int start = i;
        while (i < TEST_PAGE_COUNT && page_state[i] == 0) {
            i++;
        }
        unsigned int length = i - start;
        if (length >= request && length < best_length) {
            best_start = (int)start;
            best_length = length;
        }
    }
    return best_start;
}

static struct Page *
allocate_checked(unsigned int count) {
    assert(count > 0);
    int expected = expected_allocation(count);
    struct Page *result = alloc_pages(count);
    if (expected < 0) {
        assert(result == NULL);
    } else {
        assert(result == &pages[expected]);
        for (unsigned int i = 0; i < count; i++) {
            assert(page_state[expected + i] == 0);
            page_state[expected + i] = 1;
        }
    }
    verify_state();
    return result;
}

static void
release_checked(unsigned int start, unsigned int count) {
    assert(count > 0 && start + count <= TEST_PAGE_COUNT);
    for (unsigned int i = start; i < start + count; i++) {
        assert(page_state[i] == 1);
    }
    free_pages(&pages[start], count);
    for (unsigned int i = start; i < start + count; i++) {
        page_state[i] = 0;
    }
    verify_state();
}

static void
occupy_entire_pool(void) {
    reset_full_pool();
    assert(allocate_checked(TEST_PAGE_COUNT) == pages);
}

static void
test_original_checks(void) {
    reset_full_pool();
    /* Runs the supplied basic_check()/best_fit_check() without modification. */
    pmm_manager->check();
    verify_state();
    puts("[HOST PASS] unchanged basic_check and best_fit_check");
}

static void
test_selection_and_split(void) {
    occupy_entire_pool();
    release_checked(0, 12);
    release_checked(20, 5);
    release_checked(40, 8);
    assert(allocate_checked(4) == &pages[20]);
    assert(pages[24].property == 1);
    assert(allocate_checked(1) == &pages[24]);
    assert(allocate_checked(8) == &pages[40]);
    assert(allocate_checked(7) == &pages[0]);
    assert(pages[7].property == 5);

    occupy_entire_pool();
    release_checked(60, 6);
    release_checked(10, 6);
    assert(allocate_checked(4) == &pages[10]);
    assert(allocate_checked(4) == &pages[60]);
    puts("[HOST PASS] best-fit choice, low-address tie, exact match and split");
}

static void
test_empty_and_fragmented(void) {
    occupy_entire_pool();
    assert(allocate_checked(1) == NULL);
    release_checked(0, 3);
    release_checked(10, 3);
    release_checked(20, 2);
    assert(nr_free_pages() == 8);

    struct Page snapshot[TEST_PAGE_COUNT];
    for (unsigned int i = 0; i < TEST_PAGE_COUNT; i++) {
        snapshot[i] = pages[i];
    }
    assert(allocate_checked(4) == NULL);
    for (unsigned int i = 0; i < TEST_PAGE_COUNT; i++) {
        assert(snapshot[i].ref == pages[i].ref);
        assert(snapshot[i].flags == pages[i].flags);
        assert(snapshot[i].property == pages[i].property);
        assert(snapshot[i].page_link.prev == pages[i].page_link.prev);
        assert(snapshot[i].page_link.next == pages[i].page_link.next);
    }
    assert(nr_free_pages() == 8);
    puts("[HOST PASS] empty pool and fragmented failure leave state unchanged");
}

static void
test_insert_and_merge(void) {
    occupy_entire_pool();
    release_checked(40, 4);
    release_checked(10, 3);
    release_checked(80, 2);

    occupy_entire_pool();
    release_checked(20, 3);
    release_checked(23, 2);
    assert(pages[20].property == 5);

    occupy_entire_pool();
    release_checked(23, 2);
    release_checked(20, 3);
    assert(pages[20].property == 5);

    occupy_entire_pool();
    release_checked(20, 3);
    release_checked(25, 3);
    release_checked(23, 2);
    assert(pages[20].property == 8);
    puts("[HOST PASS] free-list head/tail insertion and left/right/two-side merge");
}

static void
test_reverse_registration(void) {
    reset_empty_pool();
    register_range(64, 16);
    register_range(32, 16);
    register_range(48, 16);
    assert(pages[32].property == 48);
    register_range(16, 16);
    register_range(0, 16);
    register_range(80, 48);
    assert(nr_free_pages() == TEST_PAGE_COUNT);
    assert(pages[0].property == TEST_PAGE_COUNT);
    puts("[HOST PASS] reverse-order registration and adjacent-range coalescing");
}

static uint32_t random_state = 0x2511107U;

static uint32_t
next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void
test_randomized_operations(void) {
    reset_full_pool();
    for (unsigned int round = 0; round < RANDOM_ROUNDS; round++) {
        if (next_random() % 100 < 60) {
            allocate_checked(1 + next_random() % 17);
        } else {
            unsigned int start = next_random() % TEST_PAGE_COUNT;
            unsigned int scanned = 0;
            while (scanned < TEST_PAGE_COUNT && page_state[start] != 1) {
                start = (start + 1) % TEST_PAGE_COUNT;
                scanned++;
            }
            if (scanned == TEST_PAGE_COUNT) {
                allocate_checked(1 + next_random() % 17);
                continue;
            }
            unsigned int available = 0;
            while (start + available < TEST_PAGE_COUNT &&
                   page_state[start + available] == 1 && available < 17) {
                available++;
            }
            release_checked(start, 1 + next_random() % available);
        }
    }

    /* Return every remaining allocated range, including split allocations. */
    unsigned int i = 0;
    while (i < TEST_PAGE_COUNT) {
        if (page_state[i] != 1) {
            i++;
            continue;
        }
        unsigned int start = i;
        while (i < TEST_PAGE_COUNT && page_state[i] == 1) {
            i++;
        }
        release_checked(start, i - start);
    }
    assert(nr_free_pages() == TEST_PAGE_COUNT);
    assert(pages[0].property == TEST_PAGE_COUNT);
    printf("[HOST PASS] %u deterministic random operations against bitmap oracle\n",
           RANDOM_ROUNDS);
}

int
main(void) {
    puts("Best-Fit isolated host tests (not a QEMU/full-Lab2 test)");
    test_original_checks();
    test_selection_and_split();
    test_empty_and_fragmented();
    test_insert_and_merge();
    test_reverse_registration();
    test_randomized_operations();
    puts("[HOST PASS] ALL BEST-FIT TESTS PASSED");
    return 0;
}
