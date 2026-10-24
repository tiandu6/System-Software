#include <slub.h>
#include <pmm.h>
#include <list.h>
#include <stdio.h>
#include <string.h>

#define SLUB_CLASSES 8
#define SLUB_ALIGN 16
#define SLUB_SLAB_MAGIC 0x534c5542534c4142UL
#define SLUB_LARGE_MAGIC 0x534c55424c415247UL

enum { SLUB_EMPTY, SLUB_PARTIAL, SLUB_FULL };
struct slub_cache;

/* 64 bytes on RV64; all following slots are aligned to 16 bytes. */
struct slub_slab {
    uint64_t magic;
    struct slub_cache *cache;
    struct Page *page;
    void *freelist;
    list_entry_t link;
    unsigned int capacity;
    unsigned int inuse;
    unsigned int state;
};

struct slub_cache {
    size_t size;
    size_t offset;
    unsigned int capacity;
    struct slub_slab *active;
    list_entry_t slabs[3];
};

/* Payload starts at a fixed aligned offset in the FIRST backend page. */
struct slub_large {
    uint64_t magic;
    struct Page *page;
    size_t nr_pages;
    size_t requested;
    list_entry_t link;
};

static struct slub_cache slub_caches[SLUB_CLASSES];
static const size_t slub_sizes[SLUB_CLASSES] = {16,32,64,128,256,512,1024,2048};
static list_entry_t slub_large_allocations;
static bool slub_ready;

/* Test observers are inert outside slub_check(); they never allocate memory. */
static void slub_test_backend_request(size_t n);
static void slub_test_backend_result(struct Page *base, size_t n);
static void slub_test_backend_release(struct Page *base, size_t n);

#ifdef SLUB_TEST
static bool slub_test_fail_next;
static size_t slub_test_alloc_calls;
static size_t slub_test_free_calls;
#endif

static struct Page *
slub_backend_alloc(size_t nr_pages) {
    slub_test_backend_request(nr_pages);
#ifdef SLUB_TEST
    slub_test_alloc_calls++;
    if (slub_test_fail_next) {
        slub_test_fail_next = 0;
        slub_test_backend_result(NULL, nr_pages);
        return NULL;
    }
#endif
    struct Page *base = alloc_pages(nr_pages);
    slub_test_backend_result(base, nr_pages);
    return base;
}

static void
slub_backend_free(struct Page *base, size_t nr_pages) {
    slub_test_backend_release(base, nr_pages);
#ifdef SLUB_TEST
    slub_test_free_calls++;
#endif
    free_pages(base, nr_pages);
}

static size_t
slub_aligned_header(size_t size) {
    /* Only compile-time, small header sizes are passed here. */
    assert(size <= (size_t)-1 - (SLUB_ALIGN - 1));
    return (size + SLUB_ALIGN - 1) & ~((size_t)SLUB_ALIGN - 1);
}

/* KADDR is unavailable. Validate descriptor and physical ranges first. */
static void *
slub_page_address(struct Page *base, size_t nr_pages) {
    if (base == NULL || pages == NULL || nr_pages == 0 || npage < nbase ||
        (uintptr_t)base < (uintptr_t)pages) {
        return NULL;
    }
    uintptr_t delta = (uintptr_t)base - (uintptr_t)pages;
    if (delta % sizeof(struct Page) != 0) return NULL;
    size_t index = delta / sizeof(struct Page), count = npage - nbase;
    if (index >= count || nr_pages > count - index ||
        nbase + index > ((uintptr_t)-1 >> PGSHIFT)) return NULL;
    uintptr_t pa = page2pa(base);
    if (pa < DRAM_BASE || pa >= PHYSICAL_MEMORY_END ||
        nr_pages > (PHYSICAL_MEMORY_END - pa) / PGSIZE ||
        pa > (uintptr_t)-1 - va_pa_offset) return NULL;
    uintptr_t address = pa + va_pa_offset;
    if (address % PGSIZE != 0 || nr_pages > ((uintptr_t)-1 - address) / PGSIZE)
        return NULL;
    return (void *)address;
}

/* This is also the numerical guard before reading a pointer's page header. */
static bool
slub_pointer_pa(void *ptr, uintptr_t *pa) {
    uintptr_t address = (uintptr_t)ptr;
    if (address < va_pa_offset || npage < nbase) return 0;
    uintptr_t physical = address - va_pa_offset;
    if (physical < DRAM_BASE || physical >= PHYSICAL_MEMORY_END ||
        PPN(physical) < nbase || PPN(physical) >= npage) return 0;
    *pa = physical;
    return 1;
}

void
slub_init(void) {
    if (slub_ready) return;
    static_assert(SLUB_ALIGN >= sizeof(void *));
    size_t offset = slub_aligned_header(sizeof(struct slub_slab));
    assert(offset < PGSIZE && slub_aligned_header(sizeof(struct slub_large)) < PGSIZE);
    for (size_t i = 0; i < SLUB_CLASSES; i++) {
        struct slub_cache *c = &slub_caches[i];
        c->size = slub_sizes[i];
        c->offset = offset;
        c->capacity = (PGSIZE - offset) / c->size;
        assert(c->capacity > 0 && c->size % SLUB_ALIGN == 0);
        c->active = NULL;
        for (size_t state = 0; state < 3; state++) list_init(&c->slabs[state]);
    }
    list_init(&slub_large_allocations);
    slub_ready = 1;
}

static struct slub_slab *
slub_create_slab(struct slub_cache *cache) {
    struct Page *page = slub_backend_alloc(1);
    if (page == NULL) return NULL;
    struct slub_slab *s = slub_page_address(page, 1);
    if (s == NULL) {
        slub_backend_free(page, 1);
        return NULL;
    }
    s->magic = SLUB_SLAB_MAGIC;
    s->cache = cache;
    s->page = page;
    s->capacity = cache->capacity;
    s->inuse = 0;
    s->state = SLUB_EMPTY;
    s->freelist = (char *)s + cache->offset;
    list_init(&s->link);
    for (size_t i = 0; i < s->capacity; i++) {
        void *slot = (char *)s + cache->offset + i * cache->size;
        *(void **)slot = i + 1 < s->capacity ? (char *)slot + cache->size : NULL;
    }
    /* Publish only after every header field and freelist link is valid. */
    list_add(&cache->slabs[SLUB_EMPTY], &s->link);
    return s;
}

static void
slub_release_slab(struct slub_slab *s) {
    struct slub_cache *cache = s->cache;
    struct Page *page = s->page;
    assert(s->inuse == 0);
    list_del_init(&s->link);
    if (cache->active == s) cache->active = NULL;
    s->magic = 0;
    slub_backend_free(page, 1);
    /* The page, including s, belongs to the backend now. */
}

static void
slub_update_state(struct slub_slab *s) {
    struct slub_cache *cache = s->cache;
    unsigned int next = s->inuse == 0 ? SLUB_EMPTY :
                        s->inuse == s->capacity ? SLUB_FULL : SLUB_PARTIAL;
    if (next == SLUB_EMPTY && s->state != SLUB_EMPTY &&
        !list_empty(&cache->slabs[SLUB_EMPTY])) {
        slub_release_slab(s);
        return;
    }
    list_del_init(&s->link);
    s->state = next;
    list_add(&cache->slabs[next], &s->link);
    if (next == SLUB_FULL) {
        if (cache->active == s) cache->active = NULL;
    } else if (cache->active == NULL) {
        cache->active = s;
    }
}

static void *
slub_alloc_small(struct slub_cache *cache) {
    struct slub_slab *s = cache->active;
    if (s == NULL && !list_empty(&cache->slabs[SLUB_PARTIAL]))
        s = to_struct(list_next(&cache->slabs[SLUB_PARTIAL]), struct slub_slab, link);
    if (s == NULL && !list_empty(&cache->slabs[SLUB_EMPTY]))
        s = to_struct(list_next(&cache->slabs[SLUB_EMPTY]), struct slub_slab, link);
    if (s == NULL) s = slub_create_slab(cache);
    if (s == NULL) return NULL;
    assert(s->freelist != NULL && s->inuse < s->capacity);
    void *object = s->freelist;
    s->freelist = *(void **)object;
    s->inuse++;
    slub_update_state(s);
    return object;
}

static void *
slub_alloc_large(size_t size) {
    size_t offset = slub_aligned_header(sizeof(struct slub_large));
    if (size > (size_t)-1 - offset) return NULL;
    size_t bytes = size + offset;
    size_t nr_pages = bytes / PGSIZE + (bytes % PGSIZE != 0);
    if (npage < nbase || nr_pages > npage - nbase ||
        nr_pages > (PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE) return NULL;
    struct Page *page = slub_backend_alloc(nr_pages);
    if (page == NULL) return NULL;
    struct slub_large *h = slub_page_address(page, nr_pages);
    if (h == NULL) {
        slub_backend_free(page, nr_pages);
        return NULL;
    }
    h->magic = SLUB_LARGE_MAGIC;
    h->page = page;
    h->nr_pages = nr_pages;
    h->requested = size;
    list_init(&h->link);
    list_add(&slub_large_allocations, &h->link);
    return (char *)h + offset;
}

void *
kmalloc(size_t size) {
    if (size == 0) return NULL;
    slub_init();
    for (size_t i = 0; i < SLUB_CLASSES; i++) {
        if (size <= slub_caches[i].size) return slub_alloc_small(&slub_caches[i]);
    }
    return slub_alloc_large(size);
}

void
kfree(void *ptr) {
    if (ptr == NULL) return;
    uintptr_t pa;
    assert(slub_ready && slub_pointer_pa(ptr, &pa));
    uintptr_t address = ROUNDDOWN((uintptr_t)ptr, PGSIZE);
    uint64_t magic = *(uint64_t *)address;
    if (magic == SLUB_LARGE_MAGIC) {
        struct slub_large *h = (struct slub_large *)address;
        assert(ptr == (char *)h + slub_aligned_header(sizeof(*h)));
        struct Page *page = h->page;
        size_t nr_pages = h->nr_pages;
        assert(slub_page_address(page, nr_pages) == h);
        list_del_init(&h->link);
        h->magic = 0;
        slub_backend_free(page, nr_pages);
        return;
    }
    assert(magic == SLUB_SLAB_MAGIC);
    struct slub_slab *s = (struct slub_slab *)address;
    struct slub_cache *cache = s->cache;
    assert(cache >= slub_caches && cache < slub_caches + SLUB_CLASSES);
    uintptr_t first = address + cache->offset;
    assert((uintptr_t)ptr >= first && ((uintptr_t)ptr - first) % cache->size == 0);
    assert(((uintptr_t)ptr - first) / cache->size < s->capacity && s->inuse > 0);
    assert(slub_page_address(s->page, 1) == s);
    size_t visits = 0;
    for (void *p = s->freelist; p != NULL; p = *(void **)p) {
        assert(++visits <= s->capacity && p != ptr);
    }
    *(void **)ptr = s->freelist;
    s->freelist = ptr;
    s->inuse--;
    slub_update_state(s);
}

size_t
slub_reclaim(void) {
    if (!slub_ready) return 0;
    size_t returned = 0;
    for (size_t i = 0; i < SLUB_CLASSES; i++) {
        list_entry_t *head = &slub_caches[i].slabs[SLUB_EMPTY];
        while (!list_empty(head)) {
            struct slub_slab *s = to_struct(list_next(head), struct slub_slab, link);
            slub_release_slab(s);
            returned++;
        }
    }
    return returned;
}

/* Module-private tests; production layouts and allocation algorithms above are unchanged. */
#include "slub_test.inc"
