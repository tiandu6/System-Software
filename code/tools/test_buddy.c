/* Host mapping: pages[0] -> nbase; npage = nbase + array length.
 * Public operations use the actual pmm.c dispatch functions. */
#include "../kern/mm/buddy_pmm.c"
#include <string.h>
extern int puts(const char *s);
extern int printf(const char *fmt, ...);
extern int vprintf(const char *fmt, va_list ap);
extern int fflush(void *stream);
extern void abort(void) __attribute__((noreturn));
#define HOST_CAP 32768
static struct Page storage[HOST_CAP];
static struct Page init_snapshot[HOST_CAP];
int cprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int result = vprintf(fmt, ap);
    va_end(ap);
    return result;
}
void __panic(const char *file, int line, const char *fmt, ...) {
    printf("PANIC at %s:%d: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    puts("");
    fflush(NULL);
    abort();
}
static void prepare(size_t count) {
    assert(count > 0 && count <= HOST_CAP);
    pages = storage;
    npage = nbase + count;
    pmm_manager = &buddy_pmm_manager;
    for (size_t i = 0; i < count; i++) {
        storage[i] = (struct Page){0};
        SetPageReserved(storage + i);
        storage[i].ref = 31;
        list_init(&storage[i].page_link);
        init_snapshot[i] = storage[i];
    }
    pmm_manager->init();
    for (size_t i = 0; i < count; i++)
        assert(buddy_test_same_page(storage + i, init_snapshot + i, 1));
    assert(nr_free_pages() == 0);
    buddy_test_invariants(NULL);
}
static void host_max_order(void) {
    bt_scenario = "host_MAX_ORDER";
    prepare(HOST_CAP);
    pmm_manager->init_memmap(storage, HOST_CAP);
    buddy_test_invariants(NULL);
    assert(nr_free_pages() == HOST_CAP && bt_order[0] == MAX_ORDER + 1);
    struct Page *all = alloc_pages(HOST_CAP);
    assert(all != NULL && nr_free_pages() == 0);
    buddy_test_invariants(NULL);
    assert(alloc_pages(1) == NULL);
    free_pages(all + 3, HOST_CAP - 3);
    buddy_test_invariants(NULL);
    assert(nr_free_pages() == HOST_CAP - 3);
    free_pages(all, 3);
    buddy_test_invariants(NULL);
    assert(bt_order[0] == MAX_ORDER + 1 && nr_free_pages() == HOST_CAP);
    puts("PASS: host inclusive MAX_ORDER and irregular partial free");
}
static void host_repeated_check(void) {
    bt_scenario = "host_repeated_check";
    prepare(512);
    pmm_manager->init_memmap(storage + 3, 253);
    pmm_manager->init_memmap(storage + 288, 224);
    struct Page *live = alloc_pages(3);
    assert(live != NULL);
    set_page_ref(live, 11);
    size_t before = nr_free_pages();
    struct buddy_state *original = buddy;
    pmm_manager->check();
    assert(buddy == original && nr_free_pages() == before && page_ref(live) == 11);
    assert(bt_fixture.state.map == NULL && bt_fixture.state.nr_free == 0);
    for (unsigned int k = 0; k <= MAX_ORDER; k++)
        assert(list_empty(&bt_fixture.state.free_list[k]));
    pmm_manager->check();
    assert(buddy == original && nr_free_pages() == before && page_ref(live) == 11);
    buddy_test_invariants(NULL);
    free_pages(live, 3);
    buddy_test_invariants(NULL);
    assert(nr_free_pages() == 477);
    puts("PASS: two check invocations preserve live allocation, reservations and real free sets");
}
static int negative(const char *name) {
    bt_scenario = name;
    prepare(16);
    pmm_manager->init_memmap(storage, 8);
    if (strcmp(name, "zero_alloc") == 0) alloc_pages(0);
    else if (strcmp(name, "zero_free") == 0) free_pages(storage, 0);
    else if (strcmp(name, "reserved_free") == 0) free_pages(storage + 12, 1);
    else if (strcmp(name, "double_free") == 0) {
        struct Page *p = alloc_pages(3);
        assert(p != NULL);
        free_pages(p, 3);
        free_pages(p, 3);
    } else if (strcmp(name, "duplicate_registration") == 0) {
        pmm_manager->init_memmap(storage, 8);
    } else if (strcmp(name, "out_of_bounds") == 0) {
        pmm_manager->init_memmap(storage + 15, 2);
    } else if (strcmp(name, "allocated_head_flag") == 0) {
        struct Page *p = alloc_pages(8);
        assert(p != NULL);
        SetPageProperty(p); p->property = 8;
        buddy_test_invariants(NULL);
    } else if (strcmp(name, "wrong_order_size") == 0) {
        storage[0].property = 4;
        buddy_test_invariants(NULL);
    } else if (strcmp(name, "free_nonzero_ref") == 0) {
        storage[1].ref = 9;
        buddy_test_invariants(NULL);
    } else if (strcmp(name, "stale_checked_bit") == 0) {
        storage[0].flags |= BUDDY_CHECKED;
        buddy_test_invariants(NULL);
    } else if (strcmp(name, "invalid_link") == 0) {
        buddy->free_list[3].next = (list_entry_t *)0x123;
        buddy_test_invariants(NULL);
    } else if (strcmp(name, "unmerged_buddies") == 0) {
        list_del_init(&storage[0].page_link);
        storage[0].property = 4;
        storage[4].property = 4;
        SetPageProperty(storage + 4);
        list_add(&buddy->free_list[2], &storage[0].page_link);
        list_add(&buddy->free_list[2], &storage[4].page_link);
        buddy_test_invariants(NULL);
    } else if (strcmp(name, "insufficient_test_arena") == 0) {
        pmm_manager->check();
    } else return 2;
    return 3;
}
int main(int argc, char **argv) {
    if (argc > 1) return negative(argv[1]);
    host_max_order();
    host_repeated_check();
    puts("All independent Buddy specification tests passed.");
    return 0;
}
