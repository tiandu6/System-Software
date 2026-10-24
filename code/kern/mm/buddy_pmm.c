#include <buddy_pmm.h>
#include <list.h>
#include <stdio.h>
#include <string.h>

/* MAX_ORDER is inclusive. 2^15 pages cover the framework's 128 MiB RAM
 * window, including its largest possible physically aligned block. */
#define MAX_ORDER 15
#define BUDDY_MANAGED (1UL << 2)
#define BUDDY_FREE (1UL << 3)
#define BUDDY_CHECKED (1UL << 4) /* temporary mark used only by validation */

struct buddy_state {
    list_entry_t free_list[MAX_ORDER + 1];
    size_t nr_free;
    struct Page *map;
    ppn_t base_ppn;
    size_t count;
};

static struct buddy_state system_state;
static struct buddy_state *buddy = &system_state;

static void buddy_check(void);

static void
buddy_state_init(struct buddy_state *state, struct Page *map,
                 ppn_t base_ppn, size_t count) {
    assert(count <= (size_t)-1 - base_ppn);
    for (unsigned int order = 0; order <= MAX_ORDER; order++) {
        list_init(&state->free_list[order]);
    }
    state->nr_free = 0;
    state->map = map;
    state->base_ppn = base_ppn;
    state->count = count;
}

static void
buddy_init(void) {
    static_assert(MAX_ORDER < sizeof(size_t) * 8);
    static_assert(MAX_ORDER < sizeof(((struct Page *)0)->property) * 8);
    static_assert((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE <=
                  ((size_t)1 << MAX_ORDER));
    /* page_init has not established pages/npage yet. Bind on registration. */
    buddy_state_init(&system_state, NULL, 0, 0);
    buddy = &system_state;
}

/* Validate descriptor addresses numerically before using pointer arithmetic. */
static size_t
buddy_index(struct Page *base, size_t n) {
    assert(n > 0 && buddy->map != NULL);
    uintptr_t address = (uintptr_t)base;
    uintptr_t start = (uintptr_t)buddy->map;
    assert(address >= start);
    uintptr_t offset = address - start;
    assert(offset % sizeof(struct Page) == 0);
    size_t index = offset / sizeof(struct Page);
    assert(index < buddy->count && n <= buddy->count - index);
    return index;
}

static ppn_t
buddy_ppn(struct Page *page) {
    return buddy->base_ppn + buddy_index(page, 1);
}

static bool
buddy_contains(ppn_t ppn, size_t n) {
    if (n == 0 || ppn < buddy->base_ppn) {
        return 0;
    }
    size_t index = ppn - buddy->base_ppn;
    return index < buddy->count && n <= buddy->count - index;
}

/* The caller supplies an aligned, free block not currently on a list.
 * Both participating blocks are wholly registered by construction; a free
 * head of the same size certifies that the partner has no holes inside it. */
static void
buddy_put_block(ppn_t ppn, unsigned int order) {
    size_t size = (size_t)1 << order;
    assert(buddy_contains(ppn, size) && (ppn & (size - 1)) == 0);
    while (order < MAX_ORDER) {
        ppn_t partner_ppn = ppn ^ size;
        /* Never form a descriptor pointer before the full numeric check. */
        if (!buddy_contains(partner_ppn, size)) {
            break;
        }
        struct Page *partner = buddy->map + (partner_ppn - buddy->base_ppn);
        if (PageReserved(partner) || !(partner->flags & BUDDY_MANAGED) ||
            !(partner->flags & BUDDY_FREE) || !PageProperty(partner) ||
            partner->property != size) {
            break;
        }
        list_del_init(&partner->page_link);
        ClearPageProperty(partner);
        partner->property = 0;
        if (partner_ppn < ppn) {
            ppn = partner_ppn;
        }
        order++;
        size <<= 1;
    }
    struct Page *head = buddy->map + (ppn - buddy->base_ppn);
    head->property = size;
    SetPageProperty(head);
    list_add(&buddy->free_list[order], &head->page_link);
}

/* Partition an arbitrary range using absolute PPN alignment. This helper
 * changes lists only; callers account for newly free pages exactly once. */
static void
buddy_return_range(ppn_t ppn, size_t n) {
    assert(buddy_contains(ppn, n));
    while (n != 0) {
        unsigned int order = 0;
        size_t size = 1;
        while (order < MAX_ORDER && size <= n / 2 &&
               (ppn & ((size << 1) - 1)) == 0) {
            size <<= 1;
            order++;
        }
        buddy_put_block(ppn, order);
        ppn += size;
        n -= size;
    }
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    if (buddy->map == NULL) {
        assert(pages != NULL && npage >= nbase);
        buddy->map = pages;
        buddy->base_ppn = nbase;
        buddy->count = npage - nbase;
    }
    size_t index = buddy_index(base, n);
    for (size_t i = 0; i < n; i++) {
        assert(PageReserved(base + i));
        assert(!(base[i].flags & BUDDY_MANAGED));
    }
    for (size_t i = 0; i < n; i++) {
        base[i].flags = BUDDY_MANAGED | BUDDY_FREE;
        base[i].property = 0;
        set_page_ref(base + i, 0);
        list_init(&base[i].page_link);
    }
    buddy->nr_free += n;
    buddy_return_range(buddy->base_ppn + index, n);
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > buddy->nr_free) {
        return NULL;
    }
    unsigned int order = 0;
    size_t size = 1;
    while (size < n) {
        if (order == MAX_ORDER) {
            return NULL;
        }
        size <<= 1;
        order++;
    }
    unsigned int found = order;
    while (found <= MAX_ORDER && list_empty(&buddy->free_list[found])) {
        found++;
    }
    if (found > MAX_ORDER) {
        return NULL;
    }

    struct Page *base = le2page(list_next(&buddy->free_list[found]), page_link);
    ppn_t ppn = buddy_ppn(base);
    list_del_init(&base->page_link);
    ClearPageProperty(base);
    base->property = 0;
    while (found > order) {
        found--;
        size_t half = (size_t)1 << found;
        struct Page *right = base + half;
        right->property = half;
        SetPageProperty(right);
        list_add(&buddy->free_list[found], &right->page_link);
    }
    for (size_t i = 0; i < n; i++) {
        assert((base[i].flags & (BUDDY_MANAGED | BUDDY_FREE)) ==
               (BUDDY_MANAGED | BUDDY_FREE));
        assert(!PageReserved(base + i) && !PageProperty(base + i));
        base[i].flags &= ~BUDDY_FREE;
        base[i].property = 0;
        set_page_ref(base + i, 0);
    }
    buddy->nr_free -= n;
    if (size > n) {
        /* Tail pages were never handed out; do not count them a second time. */
        buddy_return_range(ppn + n, size - n);
    }
    return base;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    size_t index = buddy_index(base, n);
    for (size_t i = 0; i < n; i++) {
        assert(base[i].flags & BUDDY_MANAGED);
        assert(!(base[i].flags & BUDDY_FREE));
        assert(!PageReserved(base + i) && !PageProperty(base + i));
    }
    for (size_t i = 0; i < n; i++) {
        base[i].flags |= BUDDY_FREE;
        base[i].property = 0;
        set_page_ref(base + i, 0);
    }
    buddy->nr_free += n;
    buddy_return_range(buddy->base_ppn + index, n);
}

static size_t
buddy_nr_free_pages(void) {
    return buddy->nr_free;
}

/* Full invariant check, using a temporary per-page bit to detect overlaps
 * and free pages missing from the lists. All marks are removed on success. */
static void
buddy_validate(void) {
    for (size_t i = 0; i < buddy->count; i++) {
        assert(!(buddy->map[i].flags & BUDDY_CHECKED));
    }
    size_t total = 0;
    for (unsigned int order = 0; order <= MAX_ORDER; order++) {
        list_entry_t *head = &buddy->free_list[order];
        assert(list_next(list_prev(head)) == head);
        assert(list_prev(list_next(head)) == head);
        size_t size = (size_t)1 << order;
        size_t visits = 0;
        for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
            assert(++visits <= buddy->count);
            assert(list_next(list_prev(le)) == le);
            assert(list_prev(list_next(le)) == le);
            struct Page *page = le2page(le, page_link);
            size_t index = buddy_index(page, size);
            ppn_t ppn = buddy->base_ppn + index;
            assert((ppn & (size - 1)) == 0);
            assert(PageProperty(page) && page->property == size);
            for (size_t i = 0; i < size; i++) {
                struct Page *p = page + i;
                assert((p->flags & (BUDDY_MANAGED | BUDDY_FREE)) ==
                       (BUDDY_MANAGED | BUDDY_FREE));
                assert(!PageReserved(p) && page_ref(p) == 0);
                assert(!(p->flags & BUDDY_CHECKED));
                if (i != 0) {
                    assert(!PageProperty(p) && p->property == 0);
                }
                p->flags |= BUDDY_CHECKED;
            }
            if (order < MAX_ORDER && buddy_contains(ppn ^ size, size)) {
                struct Page *other = buddy->map + ((ppn ^ size) - buddy->base_ppn);
                assert(!PageProperty(other) || other->property != size);
            }
            assert(size <= buddy->count - total);
            total += size;
        }
    }
    assert(total == buddy->nr_free);
    for (size_t i = 0; i < buddy->count; i++) {
        struct Page *page = buddy->map + i;
        if (page->flags & BUDDY_MANAGED) {
            assert(!PageReserved(page));
            assert(!!(page->flags & BUDDY_CHECKED) == !!(page->flags & BUDDY_FREE));
        } else {
            assert(!(page->flags & BUDDY_FREE) && !PageProperty(page));
        }
        if (PageProperty(page)) {
            assert((page->flags & (BUDDY_FREE | BUDDY_CHECKED)) ==
                   (BUDDY_FREE | BUDDY_CHECKED));
        } else {
            assert(page->property == 0);
        }
        page->flags &= ~BUDDY_CHECKED;
    }
}

/* Independent specification tests. Real pages are acquired before the private
 * state is switched. These tests never call buddy_validate/buddy_contains/
 * buddy_ppn or use their answers as an oracle. */
#define BT_CAP 64
#define BT_OWNED 128
#define BT_GLOBAL_CAP ((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE)
#define BT_RESERVED 0
#define BT_FREE 1
#define BT_LIVE 2

struct buddy_test_fixture {
    struct buddy_state state;
    struct Page *arena;
    struct Page *map;
    struct Page *owned;
    size_t count;
    unsigned char model[BT_CAP];
    size_t owner[BT_CAP];
    int ref[BT_CAP];
    size_t next_owner;
    struct Page initial[BT_CAP];
    struct Page guard[BT_OWNED];
};

static struct buddy_test_fixture bt_fixture;
static unsigned char bt_seen[BT_GLOBAL_CAP];
static unsigned char bt_order[BT_GLOBAL_CAP];
static unsigned char bt_real_free[BT_GLOBAL_CAP];
static struct Page bt_real_before[BT_GLOBAL_CAP];
static struct Page bt_owned_before[BT_OWNED];
static struct Page bt_failure_before[BT_CAP];
static unsigned char bt_failure_orders[BT_CAP];
static const char *bt_scenario = "startup";
static const char *bt_operation = "none";
static unsigned int bt_seed;
static size_t bt_step, bt_arg_base, bt_arg_n;
static bool bt_trace;

#define BT_ASSERT(condition) do {                                          \
    if (!(condition)) {                                                    \
        panic("Buddy spec test: scenario=%s seed=%08x step=%lu "            \
              "op=%s base=%lu n=%lu assertion=%s",                        \
              bt_scenario, bt_seed, (unsigned long)bt_step,                 \
              bt_operation, (unsigned long)bt_arg_base,                     \
              (unsigned long)bt_arg_n, #condition);                         \
    }                                                                     \
} while (0)

static bool
buddy_test_same_page(const struct Page *a, const struct Page *b, bool links) {
    return a->ref == b->ref && a->flags == b->flags &&
           a->property == b->property &&
           (!links || (a->page_link.next == b->page_link.next &&
                       a->page_link.prev == b->page_link.prev));
}

/* Validate a REAL descriptor numerically before invoking global conversions. */
static size_t
buddy_test_real_index(struct Page *p, size_t n) {
    BT_ASSERT(npage >= nbase && n > 0 && pages != NULL);
    uintptr_t address = (uintptr_t)p, start = (uintptr_t)pages;
    BT_ASSERT(address >= start);
    uintptr_t delta = address - start;
    BT_ASSERT(delta % sizeof(struct Page) == 0);
    size_t index = delta / sizeof(struct Page);
    size_t total = npage - nbase;
    BT_ASSERT(index < total && n <= total - index);
    BT_ASSERT(page2ppn(p) == nbase + index);
    BT_ASSERT(pa2page(page2pa(p)) == p);
    return index;
}

static struct Page *
buddy_test_node(list_entry_t *node) {
    uintptr_t address = (uintptr_t)node;
    BT_ASSERT(address >= offsetof(struct Page, page_link));
    struct Page *p = (struct Page *)(address - offsetof(struct Page, page_link));
    buddy_test_real_index(p, 1);
    uintptr_t start = (uintptr_t)buddy->map;
    BT_ASSERT((uintptr_t)p >= start);
    size_t index = ((uintptr_t)p - start) / sizeof(struct Page);
    BT_ASSERT(index < buddy->count);
    BT_ASSERT(&p->page_link == node);
    return p;
}

static void
buddy_test_link(list_entry_t *node, list_entry_t *head) {
    if (node != head) {
        buddy_test_node(node);
    }
}

static void
buddy_test_context(const char *operation, size_t base, size_t n) {
    bt_operation = operation;
    bt_arg_base = base;
    bt_arg_n = n;
    if (bt_trace) {
        cprintf("buddy-seq seed=%08x step=%lu op=%s base=%lu n=%lu\n",
                bt_seed, (unsigned long)bt_step, operation,
                (unsigned long)base, (unsigned long)n);
    }
}

/* NULL means the real pool: validate structural and per-page invariants.
 * A fixture additionally supplies an independent registered/owner/free model. */
static void
buddy_test_invariants(struct buddy_test_fixture *f) {
    BT_ASSERT(npage >= nbase && npage - nbase <= BT_GLOBAL_CAP);
    BT_ASSERT(buddy->count <= BT_GLOBAL_CAP);
    size_t count = buddy->count;
    if (count != 0) {
        buddy_test_real_index(buddy->map, count);
        BT_ASSERT(buddy->base_ppn == page2ppn(buddy->map));
    }
    if (f != NULL) {
        BT_ASSERT(buddy == &f->state && count == f->count && count <= BT_CAP);
        BT_ASSERT(buddy->map == f->map);
    }
    memset(bt_seen, 0, count);
    memset(bt_order, 0, count);
    size_t sum = 0;
    for (unsigned int k = 0; k <= MAX_ORDER; k++) {
        list_entry_t *head = &buddy->free_list[k];
        buddy_test_link(head->next, head);
        buddy_test_link(head->prev, head);
        BT_ASSERT(head->next->prev == head && head->prev->next == head);
        size_t visits = 0, size = (size_t)1 << k;
        for (list_entry_t *node = head->next; node != head; node = node->next) {
            BT_ASSERT(++visits <= count);
            struct Page *p = buddy_test_node(node);
            buddy_test_link(node->next, head);
            buddy_test_link(node->prev, head);
            BT_ASSERT(node->next->prev == node && node->prev->next == node);
            size_t index = p - buddy->map;
            BT_ASSERT(size <= count - index);
            ppn_t ppn = page2ppn(p);
            BT_ASSERT(ppn % size == 0);
            BT_ASSERT(PageProperty(p) && p->property == size);
            BT_ASSERT(bt_order[index] == 0);
            bt_order[index] = k + 1;
            for (size_t j = 0; j < size; j++) {
                size_t i = index + j;
                struct Page *q = p + j;
                BT_ASSERT(page2ppn(q) == ppn + j);
                BT_ASSERT(!bt_seen[i]);
                bt_seen[i] = 1;
                BT_ASSERT(!PageReserved(q) && page_ref(q) == 0);
                BT_ASSERT(q->flags & BUDDY_MANAGED);
                BT_ASSERT(q->flags & BUDDY_FREE);
                if (j != 0) {
                    BT_ASSERT(!PageProperty(q) && q->property == 0);
                }
                if (f != NULL) BT_ASSERT(f->model[i] == BT_FREE && f->owner[i] == 0);
            }
            BT_ASSERT(sum <= count && size <= count - sum);
            sum += size;
        }
    }
    /* Independent absolute-PPN partner check using the observed block map. */
    for (size_t i = 0; i < count; i++) {
        if (bt_order[i] != 0 && bt_order[i] - 1 < MAX_ORDER) {
            unsigned int k = bt_order[i] - 1;
            ppn_t other = page2ppn(buddy->map + i) ^ ((size_t)1 << k);
            ppn_t first = page2ppn(buddy->map);
            if (other >= first && other - first < count) {
                BT_ASSERT(bt_order[other - first] != k + 1);
            }
        }
    }
    size_t expected = 0;
    for (size_t i = 0; i < count; i++) {
        struct Page *p = buddy->map + i;
        BT_ASSERT(!(p->flags & BUDDY_CHECKED));
        BT_ASSERT(!!PageProperty(p) == (bt_order[i] != 0));
        if (!PageProperty(p)) BT_ASSERT(p->property == 0);
        BT_ASSERT(!!(p->flags & BUDDY_FREE) == !!bt_seen[i]);
        if (p->flags & BUDDY_MANAGED) BT_ASSERT(!PageReserved(p));
        if (f != NULL) {
            BT_ASSERT(!!bt_seen[i] == (f->model[i] == BT_FREE));
            if (f->model[i] == BT_RESERVED) {
                BT_ASSERT(f->owner[i] == 0);
                BT_ASSERT(buddy_test_same_page(p, &f->initial[i], 1));
            } else {
                BT_ASSERT(!PageReserved(p) && (p->flags & BUDDY_MANAGED));
                BT_ASSERT(page_ref(p) == f->ref[i]);
                BT_ASSERT((f->model[i] == BT_LIVE) == (f->owner[i] != 0));
                expected += f->model[i] == BT_FREE;
            }
        }
    }
    BT_ASSERT(sum == buddy->nr_free && sum == nr_free_pages());
    if (f != NULL) {
        BT_ASSERT(sum == expected);
        for (size_t i = 0; i < BT_OWNED; i++) {
            struct Page *p = f->owned + i;
            if (p < f->map || p >= f->map + f->count) {
                BT_ASSERT(buddy_test_same_page(p, &f->guard[i], 1));
            }
        }
    }
}

/* Reset only the owned arena. The real state's head objects remain alive;
 * they are neither copied nor overwritten while the fixture is active. */
static void
buddy_test_fixture_reset(struct buddy_test_fixture *f, size_t offset, size_t count) {
    BT_ASSERT(count > 0 && offset < BT_CAP && count <= BT_CAP - offset);
    for (size_t i = 0; i < f->count; i++) BT_ASSERT(f->model[i] != BT_LIVE);
    f->map = f->arena + offset;
    f->count = count;
    f->next_owner = 0;
    for (size_t i = 0; i < BT_CAP; i++) {
        struct Page *p = f->arena + i;
        p->flags = (1UL << PG_reserved);
        p->property = 0;
        p->ref = 23; /* registration must reset a nonzero reference count */
        list_init(&p->page_link);
    }
    for (unsigned int k = 0; k <= MAX_ORDER; k++) list_init(&f->state.free_list[k]);
    f->state.map = f->map;
    f->state.base_ppn = page2ppn(f->map);
    f->state.count = count;
    f->state.nr_free = 0;
    buddy = &f->state;
    for (size_t i = 0; i < count; i++) {
        f->model[i] = BT_RESERVED;
        f->owner[i] = 0;
        f->ref[i] = 23;
        f->initial[i] = f->map[i];
    }
    for (size_t i = 0; i < BT_OWNED; i++) f->guard[i] = f->owned[i];
    buddy_test_invariants(f);
}

static void
buddy_test_register(struct buddy_test_fixture *f, size_t index, size_t n) {
    buddy_test_context("register", index, n);
    BT_ASSERT(index < f->count && n > 0 && n <= f->count - index);
    for (size_t i = index; i < index + n; i++) BT_ASSERT(f->model[i] == BT_RESERVED);
    size_t before = nr_free_pages();
    buddy_init_memmap(f->map + index, n);
    for (size_t i = index; i < index + n; i++) {
        f->model[i] = BT_FREE;
        f->ref[i] = 0;
    }
    BT_ASSERT(nr_free_pages() == before + n);
    buddy_test_invariants(f);
}

/* Judge availability from actual pre-operation blocks, not from total pages
 * or another Buddy implementation. Returned addresses are never predicted. */
static struct Page *
buddy_test_take(struct buddy_test_fixture *f, size_t n) {
    buddy_test_context("alloc", 0, n);
    BT_ASSERT(n > 0);
    buddy_test_invariants(f);
    unsigned int source = MAX_ORDER + 1;
    for (unsigned int k = 0; k <= MAX_ORDER; k++) {
        if (((size_t)1 << k) >= n &&
            f->state.free_list[k].next != &f->state.free_list[k]) {
            source = k;
            break;
        }
    }
    size_t before = nr_free_pages();
    for (size_t i = 0; i < f->count; i++) {
        bt_failure_before[i] = f->map[i];
        bt_failure_orders[i] = bt_order[i];
    }
    struct Page *p = alloc_pages(n);
    BT_ASSERT((p != NULL) == (source <= MAX_ORDER));
    if (p != NULL) {
        buddy_test_real_index(p, n);
        BT_ASSERT(p >= f->map && p < f->map + f->count);
        size_t index = p - f->map;
        BT_ASSERT(n <= f->count - index && before >= n);
        bool in_minimum_order = 0;
        for (size_t i = 0; i < f->count; i++) {
            if (bt_failure_orders[i] == source + 1 && index >= i &&
                index - i <= ((size_t)1 << source) &&
                n <= ((size_t)1 << source) - (index - i)) {
                in_minimum_order = 1;
            }
        }
        BT_ASSERT(in_minimum_order);
        BT_ASSERT(f->next_owner < (size_t)-1);
        size_t owner = ++f->next_owner;
        for (size_t i = index; i < index + n; i++) {
            BT_ASSERT(f->model[i] == BT_FREE && f->owner[i] == 0);
            BT_ASSERT(page2ppn(f->map + i) == page2ppn(p) + (i - index));
            BT_ASSERT(!PageReserved(f->map + i) && !PageProperty(f->map + i));
            BT_ASSERT(page_ref(f->map + i) == 0);
            f->model[i] = BT_LIVE;
            f->owner[i] = owner;
            f->ref[i] = 0;
        }
        BT_ASSERT(nr_free_pages() == before - n);
    } else {
        BT_ASSERT(nr_free_pages() == before);
    }
    buddy_test_invariants(f);
    if (p == NULL) {
        for (size_t i = 0; i < f->count; i++) {
            /* List ordering is not part of the failure contract. */
            BT_ASSERT(buddy_test_same_page(f->map + i, &bt_failure_before[i], 0));
            BT_ASSERT(bt_order[i] == bt_failure_orders[i]);
        }
    }
    return p;
}

static void
buddy_test_give(struct buddy_test_fixture *f, struct Page *p, size_t n) {
    buddy_test_real_index(p, n);
    BT_ASSERT(p >= f->map && p < f->map + f->count);
    size_t index = p - f->map;
    buddy_test_context("free", index, n);
    BT_ASSERT(n > 0 && n <= f->count - index);
    for (size_t i = index; i < index + n; i++) {
        BT_ASSERT(f->model[i] == BT_LIVE && f->owner[i] != 0);
        set_page_ref(f->map + i, 7);
        f->ref[i] = 7;
    }
    size_t before = nr_free_pages();
    free_pages(p, n);
    for (size_t i = index; i < index + n; i++) {
        f->model[i] = BT_FREE;
        f->owner[i] = 0;
        f->ref[i] = 0;
    }
    BT_ASSERT(nr_free_pages() == before + n);
    buddy_test_invariants(f);
}

static void
buddy_test_release_all(struct buddy_test_fixture *f) {
    for (size_t i = 0; i < f->count;) {
        if (f->model[i] != BT_LIVE) { i++; continue; }
        size_t first = i;
        while (i < f->count && f->model[i] == BT_LIVE) i++;
        buddy_test_give(f, f->map + first, i - first);
    }
}

static void
buddy_test_init_memmap(struct buddy_test_fixture *f) {
    bt_scenario = "init_memmap";
    buddy_test_fixture_reset(f, 0, BT_CAP);
    BT_ASSERT(nr_free_pages() == 0);
    buddy_test_register(f, 19, 1);
    buddy_test_register(f, 3, 13);
    buddy_test_register(f, 40, 7);
    buddy_test_register(f, 17, 2);
    BT_ASSERT(nr_free_pages() == 23);
    BT_ASSERT(PageReserved(f->map + 16) && PageReserved(f->map + 39));
    struct Page *live = buddy_test_take(f, 1);
    BT_ASSERT(live != NULL);
    buddy_test_register(f, 24, 8); /* must preserve the live allocation */
    buddy_test_give(f, live, 1);
    buddy_test_fixture_reset(f, 1, BT_CAP - 1); /* odd absolute origin */
    buddy_test_register(f, 0, 13);
    BT_ASSERT(nr_free_pages() == 13);
    buddy_test_fixture_reset(f, 0, BT_CAP);
    buddy_test_register(f, 8, 8);
    buddy_test_register(f, 0, 8);
    BT_ASSERT(bt_order[0] == 5 && f->map[0].property == 16);
    buddy_test_invariants(f);
    cprintf("buddy_test_init_memmap: PASS\n");
}

static void
buddy_test_alloc(struct buddy_test_fixture *f) {
    bt_scenario = "alloc";
    buddy_test_fixture_reset(f, 0, BT_CAP);
    buddy_test_register(f, 0, BT_CAP);
    struct Page *p = buddy_test_take(f, 1);
    BT_ASSERT(p != NULL);
    ppn_t ppn = page2ppn(p), first = page2ppn(f->map);
    for (unsigned int k = 0; k < 6; k++) {
        size_t size = (size_t)1 << k;
        ppn_t sibling = (ppn - ppn % size) ^ size;
        BT_ASSERT(sibling >= first && sibling - first < f->count);
        BT_ASSERT(bt_order[sibling - first] == k + 1);
    }
    buddy_test_give(f, p, 1);
    for (size_t n = 2; n <= BT_CAP; n <<= 1) {
        p = buddy_test_take(f, n);
        BT_ASSERT(p != NULL && nr_free_pages() == BT_CAP - n);
        buddy_test_give(f, p, n);
        BT_ASSERT(nr_free_pages() == BT_CAP);
    }
    /* Only one M-page block: every leftover page must already be reusable. */
    const size_t requests[2] = {3, 5};
    for (size_t t = 0; t < 2; t++) {
        buddy_test_fixture_reset(f, 0, BT_CAP);
        size_t n = requests[t], m = t == 0 ? 4 : 8;
        buddy_test_register(f, 0, m);
        BT_ASSERT(buddy_test_take(f, n) != NULL);
        BT_ASSERT(nr_free_pages() == m - n);
        for (size_t i = 0; i < m - n; i++) BT_ASSERT(buddy_test_take(f, 1) != NULL);
        BT_ASSERT(nr_free_pages() == 0 && buddy_test_take(f, 1) == NULL);
        buddy_test_release_all(f);
    }
    buddy_test_fixture_reset(f, 0, BT_CAP);
    buddy_test_register(f, 0, 16);
    buddy_test_register(f, 32, 4);
    buddy_test_register(f, 50, 2);
    BT_ASSERT(buddy_test_take(f, 1) != NULL); /* smallest eligible order 1 */
    BT_ASSERT(buddy_test_take(f, 3) != NULL); /* then order 2 */
    BT_ASSERT(buddy_test_take(f, 8) != NULL); /* split order 4 */
    buddy_test_release_all(f);
    buddy_test_fixture_reset(f, 0, BT_CAP);
    buddy_test_register(f, 0, BT_CAP);
    for (size_t i = 0; i < BT_CAP; i++) BT_ASSERT(buddy_test_take(f, 1) != NULL);
    BT_ASSERT(nr_free_pages() == 0 && buddy_test_take(f, 1) == NULL);
    buddy_test_release_all(f);
    cprintf("buddy_test_alloc: PASS\n");
}

static void
buddy_test_free_merge(struct buddy_test_fixture *f) {
    bt_scenario = "free_merge";
    buddy_test_fixture_reset(f, 0, BT_CAP);
    buddy_test_register(f, 0, 8);
    struct Page *p = buddy_test_take(f, 5);
    BT_ASSERT(p != NULL);
    buddy_test_give(f, p + 1, 3);
    size_t index = p - f->map;
    BT_ASSERT(f->model[index] == BT_LIVE && f->model[index + 4] == BT_LIVE);
    buddy_test_give(f, p + 4, 1);
    buddy_test_give(f, p, 1);
    BT_ASSERT(nr_free_pages() == 8 && bt_order[0] == 4);
    /* Two allocations exhaust one 8-page block; their address order is free. */
    struct Page *a = buddy_test_take(f, 4), *b = buddy_test_take(f, 4);
    BT_ASSERT(a != NULL && b != NULL && a != b);
    struct Page *low = a < b ? a : b;
    struct Page *high = a < b ? b : a;
    BT_ASSERT(low + 4 == high);
    buddy_test_give(f, low + 2, 4);
    BT_ASSERT(f->model[low - f->map] == BT_LIVE);
    BT_ASSERT(f->model[high - f->map + 3] == BT_LIVE);
    buddy_test_give(f, low, 2);
    buddy_test_give(f, high + 2, 2);
    BT_ASSERT(bt_order[0] == 4);
    buddy_test_fixture_reset(f, 0, BT_CAP);
    buddy_test_register(f, 0, 4);
    BT_ASSERT(buddy_test_take(f, 4) != NULL);
    buddy_test_give(f, f->map + 1, 1);
    buddy_test_give(f, f->map + 2, 1);
    BT_ASSERT(bt_order[1] == 1 && bt_order[2] == 1);
    BT_ASSERT(buddy_test_take(f, 2) == NULL); /* adjacent nonpartners */
    buddy_test_give(f, f->map, 1); /* actual 0/1 partners */
    BT_ASSERT(bt_order[0] == 2);
    buddy_test_give(f, f->map + 3, 1);
    BT_ASSERT(bt_order[0] == 3);
    buddy_test_fixture_reset(f, 0, BT_CAP);
    buddy_test_register(f, 0, BT_CAP);
    for (size_t i = 0; i < BT_CAP; i++) BT_ASSERT(buddy_test_take(f, 1) != NULL);
    /* Reverse six bits: a permutation, independent of allocation order. */
    for (size_t i = 0; i < BT_CAP; i++) {
        size_t j = 0, value = i;
        for (unsigned int bit = 0; bit < 6; bit++) { j = (j << 1) | (value & 1); value >>= 1; }
        buddy_test_give(f, f->map + j, 1);
    }
    BT_ASSERT(bt_order[0] == 7 && nr_free_pages() == BT_CAP);
    BT_ASSERT(buddy_test_take(f, BT_CAP) != NULL);
    buddy_test_give(f, f->map + 3, 13);
    buddy_test_release_all(f);
    BT_ASSERT(bt_order[0] == 7);
    buddy_test_fixture_reset(f, 0, BT_CAP);
    buddy_test_register(f, 0, 7);
    buddy_test_register(f, 8, 8); /* page 7 is a permanent hole */
    struct Page *one = buddy_test_take(f, 1);
    BT_ASSERT(one != NULL);
    buddy_test_give(f, one, 1);
    BT_ASSERT(PageReserved(f->map + 7) && nr_free_pages() == 15);
    BT_ASSERT(buddy_test_take(f, 16) == NULL);
    buddy_test_invariants(f);
    cprintf("buddy_test_free_merge: PASS\n");
}

static void
buddy_test_failure(struct buddy_test_fixture *f) {
    bt_scenario = "failure";
    buddy_test_fixture_reset(f, 0, BT_CAP);
    buddy_test_register(f, 0, BT_CAP);
    BT_ASSERT(buddy_test_take(f, BT_CAP + 1) == NULL);
    BT_ASSERT(buddy_test_take(f, (size_t)-1) == NULL);
    BT_ASSERT(buddy_test_take(f, (size_t)-2) == NULL);
    BT_ASSERT(buddy_test_take(f, ((size_t)1 << MAX_ORDER) + 1) == NULL);
    for (size_t i = 0; i < BT_CAP; i++) BT_ASSERT(buddy_test_take(f, 1) != NULL);
    BT_ASSERT(buddy_test_take(f, 1) == NULL);
    for (size_t i = 0; i < BT_CAP; i += 2) buddy_test_give(f, f->map + i, 1);
    BT_ASSERT(nr_free_pages() == BT_CAP / 2);
    BT_ASSERT(buddy_test_take(f, 2) == NULL);
    buddy_test_release_all(f);
    cprintf("buddy_test_failure: PASS\n");
}

static unsigned int
buddy_test_random(unsigned int *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 17;
    *seed ^= *seed << 5;
    return *seed;
}

static void
buddy_test_sequences(struct buddy_test_fixture *f) {
    bt_scenario = "sequences";
    buddy_test_fixture_reset(f, 0, BT_CAP);
    buddy_test_register(f, 1, 27);
    buddy_test_register(f, 32, 29);
    unsigned int random = 0xc0ffee11U;
    bt_seed = random;
    bt_trace = 1;
    for (bt_step = 0; bt_step < 256; bt_step++) {
        size_t index = buddy_test_random(&random) % f->count;
        if (f->model[index] == BT_LIVE) {
            if (buddy_test_random(&random) & 1) {
                size_t first = index, last = index + 1, owner = f->owner[index];
                while (first > 0 && f->owner[first - 1] == owner) first--;
                while (last < f->count && f->owner[last] == owner) last++;
                buddy_test_give(f, f->map + first, last - first);
            } else {
                size_t run = 1;
                while (index + run < f->count && f->model[index + run] == BT_LIVE) run++;
                size_t n = 1 + buddy_test_random(&random) % run;
                buddy_test_give(f, f->map + index, n);
            }
        } else {
            buddy_test_take(f, 1 + buddy_test_random(&random) % 13);
        }
    }
    buddy_test_release_all(f);
    BT_ASSERT(nr_free_pages() == 56);
    for (size_t i = 0; i < f->count; i++) {
        BT_ASSERT(f->model[i] == ((i >= 1 && i < 28) || (i >= 32 && i < 61) ? BT_FREE : BT_RESERVED));
    }
    bt_trace = 0;
    cprintf("buddy_test_sequences: PASS seed=%08x steps=256\n", bt_seed);
    bt_seed = 0;
    bt_step = 0;
}

static void
buddy_test_public_api(struct buddy_test_fixture *f) {
    bt_scenario = "public_api";
    BT_ASSERT(pmm_manager == &buddy_pmm_manager);
    BT_ASSERT(pmm_manager->init == buddy_init);
    BT_ASSERT(pmm_manager->init_memmap == buddy_init_memmap);
    BT_ASSERT(pmm_manager->alloc_pages == buddy_alloc_pages);
    BT_ASSERT(pmm_manager->free_pages == buddy_free_pages);
    BT_ASSERT(pmm_manager->nr_free_pages == buddy_nr_free_pages);
    BT_ASSERT(pmm_manager->check == buddy_check);
    buddy_test_fixture_reset(f, 0, BT_CAP);
    buddy_test_register(f, 0, 16);
    struct Page *p = buddy_test_take(f, 3); /* uses actual public dispatch */
    BT_ASSERT(p != NULL && nr_free_pages() == 13);
    buddy_test_give(f, p, 3);
    BT_ASSERT(nr_free_pages() == 16);
    cprintf("buddy_test_public_api: PASS\n");
}

static void
buddy_check(void) {
    BT_ASSERT(pmm_manager == &buddy_pmm_manager);
    for (unsigned int round = 1; round <= 2; round++) {
        bt_scenario = "isolation";
        buddy_test_context("acquire_real_pages", 0, BT_OWNED);
        buddy_test_invariants(NULL);
        struct buddy_state *real = buddy;
        size_t real_count = npage - nbase;
        size_t before = nr_free_pages();
        for (size_t i = 0; i < real_count; i++) {
            bt_real_before[i] = pages[i];
            bt_real_free[i] = bt_seen[i];
        }
        struct Page *owned = alloc_pages(BT_OWNED);
        if (owned == NULL) {
            panic("Buddy tests cannot construct required real arena: need %u "
                  "continuous pages, free=%lu; no tests skipped as passing",
                  BT_OWNED, (unsigned long)before);
        }
        buddy_test_real_index(owned, BT_OWNED);
        BT_ASSERT(before >= BT_OWNED && nr_free_pages() == before - BT_OWNED);
        for (size_t i = 0; i < BT_OWNED; i++) {
            BT_ASSERT(!PageReserved(owned + i) && !PageProperty(owned + i));
            BT_ASSERT(page_ref(owned + i) == 0);
            bt_owned_before[i] = owned[i];
        }
        buddy_test_invariants(NULL);
        struct buddy_test_fixture *f = &bt_fixture;
        f->owned = owned;
        ppn_t first = page2ppn(owned);
        size_t offset = (BT_CAP - first % BT_CAP) % BT_CAP;
        BT_ASSERT(offset + BT_CAP <= BT_OWNED);
        f->arena = owned + offset;
        f->count = 0;
        cprintf("buddy_check round=%u begin ppn=0x%lx pages=%u\n",
                round, (unsigned long)page2ppn(f->arena), BT_CAP);
        buddy_test_init_memmap(f);
        buddy_test_alloc(f);
        buddy_test_free_merge(f);
        buddy_test_failure(f);
        buddy_test_sequences(f);
        buddy_test_public_api(f);
        buddy_test_release_all(f);
        for (unsigned int k = 0; k <= MAX_ORDER; k++) {
            list_init(&f->state.free_list[k]);
        }
        f->state.map = NULL;
        f->state.count = f->state.nr_free = 0;
        f->count = 0;
        f->map = f->arena = f->owned = NULL;
        /* Remove all temporary sentinel links before returning ownership. */
        for (size_t i = 0; i < BT_OWNED; i++) owned[i] = bt_owned_before[i];
        buddy = real;
        free_pages(owned, BT_OWNED);
        bt_scenario = "restoration";
        buddy_test_context("restore_real_pool", 0, BT_OWNED);
        buddy_test_invariants(NULL);
        BT_ASSERT(nr_free_pages() == before && buddy == real);
        for (size_t i = 0; i < real_count; i++) {
            BT_ASSERT(bt_seen[i] == bt_real_free[i]);
            /* Original live/reserved metadata, including links, is immutable.
             * Free-list ordering itself is not an API promise. */
            BT_ASSERT(buddy_test_same_page(pages + i, &bt_real_before[i], !bt_real_free[i]));
        }
        cprintf("buddy_check round=%u restored free=%lu: PASS\n",
                round, (unsigned long)before);
    }
    cprintf("buddy_check() succeeded!\n");
}

#undef BT_ASSERT

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
