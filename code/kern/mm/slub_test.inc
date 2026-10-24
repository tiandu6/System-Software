/* Private specification tests, included into slub.c. No public cache API.
 * The oracle uses recorded backend events, literal classes, independent
 * interval arithmetic and a requested-byte object model. */
#define ST_OBJECTS 1024
#define ST_REGIONS 256
#define ST_SLOTS 256
#define ST_PHYSICAL_PAGES ((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE)
#define ST_NONE ST_OBJECTS
#define ST_LARGE 8
static const size_t st_classes[8] = {16,32,64,128,256,512,1024,2048};
struct st_region {
    struct Page *base;
    size_t pages;
    uintptr_t address;
    size_t generation;
    bool live;
    unsigned int seen;
    unsigned int kind;
};
struct st_object {
    void *ptr;
    size_t size;
    size_t region;
    size_t generation;
    unsigned int klass;
    unsigned int serial;
    bool live;
};
static struct {
    bool enabled, trace;
    size_t baseline, calls, successes, frees, freed_pages, generation;
    size_t pressure_count, step, request, expected_pages;
    uint64_t mapping;
    unsigned int serial, seed;
    const char *scenario, *operation;
    void *pointer;
    struct st_object object[ST_OBJECTS];
    struct st_region region[ST_REGIONS];
    struct Page metadata[ST_PHYSICAL_PAGES];
    struct Page *pressure[ST_PHYSICAL_PAGES];
    unsigned char free_slots[ST_SLOTS], live_slots[ST_SLOTS];
    unsigned char objects_seen[ST_OBJECTS];
    unsigned char prior_region[ST_REGIONS];
    size_t vector[ST_OBJECTS];
    struct slub_cache noop_cache[8];
    struct slub_slab noop_small[ST_REGIONS];
    struct slub_large noop_large[ST_REGIONS];
    struct { void *slot, *next; } noop_links[ST_SLOTS];
    size_t noop_link_count, noop_calls, noop_frees, noop_free;
    bool noop_ready;
} st;

#define ST_ASSERT(c) do { if (!(c)) {                                      \
    panic("SLUB test scenario=%s seed=%08x step=%lu op=%s size=%lu "         \
          "ptr=%p assertion=%s", st.scenario, st.seed,                    \
          (unsigned long)st.step, st.operation,                           \
          (unsigned long)st.request, st.pointer, #c);                     \
} } while (0)

static void slub_test_invariants(void);
static void
st_context(const char *operation, size_t size, void *pointer) {
    st.operation = operation; st.request = size; st.pointer = pointer;
    if (st.trace)
        cprintf("slub-seq seed=%08x step=%lu op=%s size=%lu ptr=%p\n",
                st.seed, (unsigned long)st.step, operation,
                (unsigned long)size, pointer);
}
static size_t
st_small_offset(void) {
    /* Independent of slub_aligned_header(), offset fields and class table. */
    return (sizeof(struct slub_slab) / 16 + (sizeof(struct slub_slab) % 16 != 0)) * 16;
}
static size_t
st_large_offset(void) {
    return (sizeof(struct slub_large) / 16 + (sizeof(struct slub_large) % 16 != 0)) * 16;
}
static unsigned int
st_class(size_t size) {
    for (unsigned int k = 0; k < 8; k++) if (size <= st_classes[k]) return k;
    return ST_LARGE;
}
static size_t
st_page_index(struct Page *base, size_t n) {
    ST_ASSERT(n > 0 && pages != NULL && npage >= nbase);
    ST_ASSERT(npage - nbase <= ST_PHYSICAL_PAGES);
    uintptr_t address = (uintptr_t)base, first = (uintptr_t)pages;
    ST_ASSERT(address >= first && (address - first) % sizeof(struct Page) == 0);
    size_t i = (address - first) / sizeof(struct Page);
    ST_ASSERT(i < npage - nbase && n <= npage - nbase - i);
    ST_ASSERT(page2ppn(base) == nbase + i && pa2page(page2pa(base)) == base);
    return i;
}
static uintptr_t
st_page_address(struct Page *base, size_t n) {
    st_page_index(base,n);
    uintptr_t pa = page2pa(base);
    ST_ASSERT(pa <= (uintptr_t)-1 - st.mapping);
    uintptr_t address = pa + st.mapping;
    ST_ASSERT(address % PGSIZE == 0 && n <= ((uintptr_t)-1 - address) / PGSIZE);
    return address;
}
static size_t
st_header_region(uintptr_t address) {
    for (size_t i = 0; i < ST_REGIONS; i++)
        if (st.region[i].live && st.region[i].address == address) return i;
    ST_ASSERT(0); return 0;
}
static bool
st_contains(const struct st_region *r, uintptr_t address, size_t size) {
    return r->live && address >= r->address && address - r->address <= r->pages * PGSIZE &&
           size <= r->pages * PGSIZE - (address - r->address);
}
static size_t
st_payload_region(void *pointer, size_t size) {
    size_t found = ST_REGIONS;
    for (size_t i = 0; i < ST_REGIONS; i++) if (st_contains(&st.region[i],(uintptr_t)pointer,size)) {
        ST_ASSERT(found == ST_REGIONS); found = i;
    }
    ST_ASSERT(found < ST_REGIONS); return found;
}
static bool
st_same_page(const struct Page *a, const struct Page *b) {
    /* The original SLUB contract protects these Buddy fields; it does not
     * prescribe an additional SLUB policy for Page.ref. */
    return a->flags == b->flags && a->property == b->property &&
           a->page_link.next == b->page_link.next && a->page_link.prev == b->page_link.prev;
}
static uintptr_t
st_node_header(list_entry_t *node, size_t offset, size_t bytes) {
    ST_ASSERT((uintptr_t)node >= offset);
    uintptr_t address = (uintptr_t)node - offset;
    size_t r = st_header_region(address);
    ST_ASSERT(st_contains(&st.region[r],address,bytes));
    return address;
}
static void
st_link(list_entry_t *node, list_entry_t *head, size_t offset, size_t bytes) {
    if (node != head) st_node_header(node,offset,bytes);
}

/* Backends remain the real PMM calls; observers only run during these tests. */
static void
slub_test_backend_request(size_t n) {
    if (!st.enabled) return;
    ST_ASSERT(n > 0 && st.calls < (size_t)-1);
    if (st.expected_pages != 0) ST_ASSERT(n == st.expected_pages);
    st.calls++;
}
static void
slub_test_backend_result(struct Page *base, size_t n) {
    if (!st.enabled || base == NULL) return;
    size_t index = st_page_index(base,n), slot = ST_REGIONS;
    uintptr_t address = st_page_address(base,n);
    for (size_t i = 0; i < ST_REGIONS; i++) {
        if (!st.region[i].live && slot == ST_REGIONS) slot = i;
        if (st.region[i].live) {
            uintptr_t other = st.region[i].address;
            size_t bytes = st.region[i].pages * PGSIZE;
            ST_ASSERT(address + n * PGSIZE <= other || other + bytes <= address);
        }
    }
    ST_ASSERT(slot < ST_REGIONS && st.generation < (size_t)-1);
    st.region[slot] = (struct st_region){base,n,address,++st.generation,1,0,0};
    for (size_t i = 0; i < n; i++) st.metadata[index+i] = base[i];
    st.successes++;
}
static void
slub_test_backend_release(struct Page *base, size_t n) {
    if (!st.enabled) return;
    size_t found = ST_REGIONS, index = st_page_index(base,n);
    for (size_t i = 0; i < ST_REGIONS; i++) if (st.region[i].live && st.region[i].base == base) {
        ST_ASSERT(found == ST_REGIONS && st.region[i].pages == n); found = i;
    }
    ST_ASSERT(found < ST_REGIONS);
    struct st_region *r = &st.region[found];
    for (size_t i = 0; i < ST_OBJECTS; i++)
        ST_ASSERT(!st.object[i].live || st.object[i].region != found ||
                  st.object[i].generation != r->generation);
    /* Detach BEFORE free, not merely before the next successful allocation. */
    if (slub_ready) {
        for (size_t k = 0; k < 8; k++) {
            ST_ASSERT((uintptr_t)slub_caches[k].active != r->address);
            for (size_t state = 0; state < 3; state++) {
                list_entry_t *head = &slub_caches[k].slabs[state];
                size_t visits = 0;
                for (list_entry_t *le = head->next; le != head;) {
                    ST_ASSERT(++visits <= ST_REGIONS);
                    uintptr_t header = st_node_header(le,offsetof(struct slub_slab,link),sizeof(struct slub_slab));
                    ST_ASSERT(header != r->address); le = le->next;
                }
            }
        }
        list_entry_t *head = &slub_large_allocations;
        size_t visits = 0;
        for (list_entry_t *le = head->next; le != head;) {
            ST_ASSERT(++visits <= ST_REGIONS);
            uintptr_t header = st_node_header(le,offsetof(struct slub_large,link),sizeof(struct slub_large));
            ST_ASSERT(header != r->address); le = le->next;
        }
    }
    for (size_t i = 0; i < n; i++) ST_ASSERT(st_same_page(base+i,&st.metadata[index+i]));
    r->live = 0; st.frees++; st.freed_pages += n;
}

static void
slub_test_invariants(void) {
    ST_ASSERT(slub_ready && st.enabled);
    size_t backend_pages = 0;
    memset(st.objects_seen,0,sizeof(st.objects_seen));
    for (size_t i = 0; i < ST_REGIONS; i++) if (st.region[i].live) {
        struct st_region *r = &st.region[i];
        size_t index = st_page_index(r->base,r->pages);
        ST_ASSERT(r->address == st_page_address(r->base,r->pages));
        ST_ASSERT(backend_pages <= ST_PHYSICAL_PAGES && r->pages <= ST_PHYSICAL_PAGES - backend_pages);
        backend_pages += r->pages; r->seen = 0;
        for (size_t j = 0; j < r->pages; j++) ST_ASSERT(st_same_page(r->base+j,&st.metadata[index+j]));
        for (size_t j = 0; j < i; j++) if (st.region[j].live)
            ST_ASSERT(r->address + r->pages * PGSIZE <= st.region[j].address ||
                      st.region[j].address + st.region[j].pages * PGSIZE <= r->address);
    }
    for (unsigned int k = 0; k < 8; k++) {
        struct slub_cache *c = &slub_caches[k];
        size_t offset = st_small_offset(), capacity = (PGSIZE-offset)/st_classes[k];
        ST_ASSERT(c->size == st_classes[k] && c->offset == offset && c->capacity == capacity);
        ST_ASSERT(offset >= sizeof(struct slub_slab) && offset % 16 == 0 && capacity > 0 && capacity <= ST_SLOTS);
        ST_ASSERT(offset + capacity * st_classes[k] <= PGSIZE);
        ST_ASSERT(offset + (capacity+1) * st_classes[k] > PGSIZE);
        size_t empty = 0;
        for (unsigned int state = 0; state < 3; state++) {
            list_entry_t *head = &c->slabs[state];
            st_link(head->next,head,offsetof(struct slub_slab,link),sizeof(struct slub_slab));
            st_link(head->prev,head,offsetof(struct slub_slab,link),sizeof(struct slub_slab));
            ST_ASSERT(head->next->prev == head && head->prev->next == head);
            size_t visits = 0;
            for (list_entry_t *le = head->next; le != head; le = le->next) {
                ST_ASSERT(++visits <= ST_REGIONS);
                uintptr_t address = st_node_header(le,offsetof(struct slub_slab,link),sizeof(struct slub_slab));
                size_t ri = st_header_region(address); struct st_region *r = &st.region[ri];
                ST_ASSERT(++r->seen == 1 && r->pages == 1); r->kind = 1;
                st_link(le->next,head,offsetof(struct slub_slab,link),sizeof(struct slub_slab));
                st_link(le->prev,head,offsetof(struct slub_slab,link),sizeof(struct slub_slab));
                ST_ASSERT(le->next->prev == le && le->prev->next == le);
                struct slub_slab *s = (struct slub_slab *)address;
                ST_ASSERT(s->magic == SLUB_SLAB_MAGIC && s->cache == c && s->page == r->base);
                ST_ASSERT(s->state == state && s->capacity == capacity && s->inuse <= capacity);
                unsigned int expected = s->inuse == 0 ? SLUB_EMPTY :
                                        s->inuse == capacity ? SLUB_FULL : SLUB_PARTIAL;
                ST_ASSERT(state == expected); empty += state == SLUB_EMPTY;
                memset(st.free_slots,0,sizeof(st.free_slots)); memset(st.live_slots,0,sizeof(st.live_slots));
                size_t live = 0;
                for (size_t i = 0; i < ST_OBJECTS; i++) if (st.object[i].live && st.object[i].region == ri) {
                    struct st_object *o = &st.object[i];
                    ST_ASSERT(o->generation == r->generation && o->klass == k);
                    ST_ASSERT(st_contains(r,(uintptr_t)o->ptr,o->size) && o->size > 0 && o->size <= st_classes[k]);
                    uintptr_t first = address + offset;
                    ST_ASSERT((uintptr_t)o->ptr >= first && ((uintptr_t)o->ptr-first)%st_classes[k] == 0);
                    size_t slot = ((uintptr_t)o->ptr-first)/st_classes[k];
                    ST_ASSERT(slot < capacity && !st.live_slots[slot]); st.live_slots[slot] = 1;
                    ST_ASSERT(!st.objects_seen[i] && (uintptr_t)o->ptr % 16 == 0);
                    st.objects_seen[i] = 1; live++;
                }
                size_t free_count = 0;
                for (void *p = s->freelist; p != NULL;) {
                    ST_ASSERT(++free_count <= capacity);
                    uintptr_t first = address + offset;
                    ST_ASSERT((uintptr_t)p >= first && (uintptr_t)p % 16 == 0);
                    ST_ASSERT(((uintptr_t)p-first)%st_classes[k] == 0);
                    size_t slot = ((uintptr_t)p-first)/st_classes[k];
                    ST_ASSERT(slot < capacity && !st.free_slots[slot] && !st.live_slots[slot]);
                    ST_ASSERT(st_contains(r,(uintptr_t)p,st_classes[k])); st.free_slots[slot] = 1;
                    p = *(void **)p; /* only after proving this is a valid free slot */
                }
                ST_ASSERT(live == s->inuse && live + free_count == capacity);
                for (size_t i = 0; i < capacity; i++) ST_ASSERT(st.live_slots[i] + st.free_slots[i] == 1);
            }
        }
        ST_ASSERT(empty <= 1);
        if (c->active != NULL) {
            size_t r = st_header_region((uintptr_t)c->active);
            ST_ASSERT(st.region[r].seen == 1 && st.region[r].kind == 1);
            ST_ASSERT(c->active->cache == c && c->active->inuse < c->active->capacity);
        }
    }
    list_entry_t *head = &slub_large_allocations;
    st_link(head->next,head,offsetof(struct slub_large,link),sizeof(struct slub_large));
    st_link(head->prev,head,offsetof(struct slub_large,link),sizeof(struct slub_large));
    ST_ASSERT(head->next->prev == head && head->prev->next == head);
    size_t visits = 0;
    for (list_entry_t *le = head->next; le != head; le = le->next) {
        ST_ASSERT(++visits <= ST_REGIONS);
        uintptr_t address = st_node_header(le,offsetof(struct slub_large,link),sizeof(struct slub_large));
        size_t ri = st_header_region(address); struct st_region *r = &st.region[ri];
        ST_ASSERT(++r->seen == 1); r->kind = 2;
        st_link(le->next,head,offsetof(struct slub_large,link),sizeof(struct slub_large));
        st_link(le->prev,head,offsetof(struct slub_large,link),sizeof(struct slub_large));
        ST_ASSERT(le->next->prev == le && le->prev->next == le);
        struct slub_large *h = (struct slub_large *)address;
        size_t offset = st_large_offset();
        ST_ASSERT(h->magic == SLUB_LARGE_MAGIC && h->page == r->base);
        ST_ASSERT(h->requested > 2048 && h->requested <= (size_t)-1 - offset);
        size_t bytes = offset + h->requested;
        size_t expected_pages = bytes / PGSIZE + (bytes % PGSIZE != 0);
        ST_ASSERT(r->pages == expected_pages && h->nr_pages == expected_pages);
        size_t owners = 0;
        for (size_t i = 0; i < ST_OBJECTS; i++) if (st.object[i].live && st.object[i].region == ri) {
            struct st_object *o = &st.object[i];
            ST_ASSERT(o->generation == r->generation && o->klass == ST_LARGE && o->size == h->requested);
            ST_ASSERT((uintptr_t)o->ptr == address + offset && (uintptr_t)o->ptr % 16 == 0);
            ST_ASSERT(st_contains(r,(uintptr_t)o->ptr,o->size));
            ST_ASSERT(!st.objects_seen[i]); st.objects_seen[i] = 1; owners++;
        }
        ST_ASSERT(owners == 1);
    }
    for (size_t i = 0; i < ST_OBJECTS; i++) ST_ASSERT(!!st.objects_seen[i] == !!st.object[i].live);
    for (size_t i = 0; i < ST_REGIONS; i++) if (st.region[i].live) ST_ASSERT(st.region[i].seen == 1);
    ST_ASSERT(nr_free_pages() + backend_pages + st.pressure_count == st.baseline);
}

static unsigned char
st_pattern(unsigned int serial, size_t offset) {
    return (unsigned char)((serial * 37U) ^ ((unsigned int)offset * 13U) ^
                           ((unsigned int)(offset >> 8) * 17U));
}
static void
st_noop_snapshot(void) {
    if (slub_ready) slub_test_invariants();
    st.noop_calls = st.calls; st.noop_frees = st.frees;
    st.noop_free = nr_free_pages(); st.noop_ready = slub_ready;
    memcpy(st.noop_cache,slub_caches,sizeof(st.noop_cache));
    st.noop_link_count = 0;
    for (size_t i = 0; i < ST_REGIONS; i++) if (st.region[i].live) {
        if (st.region[i].kind == 1) {
            struct slub_slab *s = (struct slub_slab *)st.region[i].address;
            memcpy(&st.noop_small[i],s,sizeof(*s));
            for (void *p = s->freelist; p != NULL; p = *(void **)p) {
                ST_ASSERT(st.noop_link_count < ST_SLOTS);
                st.noop_links[st.noop_link_count].slot = p;
                st.noop_links[st.noop_link_count++].next = *(void **)p;
            }
        } else {
            memcpy(&st.noop_large[i],(void *)st.region[i].address,sizeof(struct slub_large));
        }
    }
}
static void
st_noop_unchanged(void) {
    ST_ASSERT(st.calls == st.noop_calls && st.frees == st.noop_frees &&
              nr_free_pages() == st.noop_free && slub_ready == st.noop_ready);
    ST_ASSERT(memcmp(st.noop_cache,slub_caches,sizeof(st.noop_cache)) == 0);
    for (size_t i = 0; i < ST_REGIONS; i++) if (st.region[i].live) {
        void *header = (void *)st.region[i].address;
        if (st.region[i].kind == 1) ST_ASSERT(memcmp(&st.noop_small[i],header,sizeof(struct slub_slab)) == 0);
        else ST_ASSERT(memcmp(&st.noop_large[i],header,sizeof(struct slub_large)) == 0);
    }
    for (size_t i = 0; i < st.noop_link_count; i++)
        ST_ASSERT(*(void **)st.noop_links[i].slot == st.noop_links[i].next);
    if (slub_ready) slub_test_invariants();
}
static void
st_verify_object(size_t id) {
    ST_ASSERT(id < ST_OBJECTS && st.object[id].live);
    struct st_object *o = &st.object[id];
    unsigned char *p = o->ptr;
    for (size_t j = 0; j < o->size; j++) if (p[j] != st_pattern(o->serial,j)) {
        st.pointer = p; st.request = o->size;
        cprintf("SLUB data mismatch: object=%lu byte=%lu expected=%02x actual=%02x\n",
                (unsigned long)id,(unsigned long)j,st_pattern(o->serial,j),p[j]);
        ST_ASSERT(0);
    }
}
static void
st_verify_data(void) {
    for (size_t i = 0; i < ST_OBJECTS; i++) if (st.object[i].live) st_verify_object(i);
}
static size_t
st_alloc(size_t size, bool nullable) {
    st_context("kmalloc",size,NULL);
    size_t id = ST_NONE;
    for (size_t i = 0; i < ST_OBJECTS; i++) if (!st.object[i].live) { id = i; break; }
    ST_ASSERT(id < ST_OBJECTS && size > 0);
    size_t candidates = 0, before_calls = st.calls, before_success = st.successes;
    memset(st.prior_region,0,sizeof(st.prior_region));
    unsigned int klass = st_class(size);
    if (klass < 8) {
        struct slub_cache *cache = &slub_caches[klass];
        if (cache->active != NULL) {
            size_t r = st_header_region((uintptr_t)cache->active);
            st.prior_region[r] = 1; candidates = 1;
        } else {
            unsigned int state = list_empty(&cache->slabs[SLUB_PARTIAL]) ? SLUB_EMPTY : SLUB_PARTIAL;
            for (size_t i = 0; i < ST_REGIONS; i++) if (st.region[i].live && st.region[i].kind == 1) {
                struct slub_slab *s = (struct slub_slab *)st.region[i].address;
                if (s->cache == cache && s->state == state) { st.prior_region[i] = 1; candidates++; }
            }
        }
    }
    void *p = kmalloc(size); st.pointer = p;
    if (st.trace) cprintf("slub-seq seed=%08x step=%lu op=kmalloc-result size=%lu ptr=%p\n",
                         st.seed,(unsigned long)st.step,(unsigned long)size,p);
    if (p == NULL) {
        ST_ASSERT(nullable && candidates == 0); slub_test_invariants(); st_verify_data(); return ST_NONE;
    }
    size_t r = st_payload_region(p,size);
    if (klass < 8) {
        if (candidates != 0) {
            ST_ASSERT(st.prior_region[r] && st.calls == before_calls && st.successes == before_success);
        } else ST_ASSERT(st.successes == before_success+1);
    }
    ST_ASSERT((uintptr_t)p % 16 == 0);
    for (size_t i = 0; i < ST_OBJECTS; i++) if (st.object[i].live) {
        uintptr_t other = (uintptr_t)st.object[i].ptr;
        ST_ASSERT((uintptr_t)p + size <= other || other + st.object[i].size <= (uintptr_t)p);
    }
    ST_ASSERT(st.serial < 0xfffffffeU);
    st.object[id] = (struct st_object){p,size,r,st.region[r].generation,st_class(size),++st.serial,1};
    slub_test_invariants();
    for (size_t j = 0; j < size; j++) ((unsigned char *)p)[j] = st_pattern(st.serial,j);
    st_verify_object(id); return id;
}
static void
st_free(size_t id) {
    ST_ASSERT(id < ST_OBJECTS && st.object[id].live);
    st_context("kfree",st.object[id].size,st.object[id].ptr);
    st_verify_object(id);
    void *p = st.object[id].ptr;
    st.object[id].live = 0; /* caller relinquishes ownership at this call */
    kfree(p); slub_test_invariants();
}
static size_t
st_region_owners(size_t r) {
    size_t count = 0;
    for (size_t i = 0; i < ST_OBJECTS; i++)
        count += st.object[i].live && st.object[i].region == r;
    return count;
}
static size_t
st_reclaim(void) {
    st_context("slub_reclaim",0,NULL);
    slub_test_invariants();
    size_t expected = 0, before = nr_free_pages(), freed = st.freed_pages;
    for (size_t i = 0; i < ST_REGIONS; i++)
        if (st.region[i].live && st.region[i].kind == 1 && st_region_owners(i) == 0) expected += st.region[i].pages;
    size_t result = slub_reclaim();
    ST_ASSERT(result == expected && nr_free_pages() == before + expected);
    ST_ASSERT(st.freed_pages == freed + expected);
    slub_test_invariants(); st_verify_data(); return result;
}
static void
st_cleanup(void) {
#ifdef SLUB_TEST
    slub_test_fail_next = 0;
#endif
    for (size_t i = 0; i < ST_OBJECTS; i++) if (st.object[i].live) st_free(i);
    st_reclaim(); ST_ASSERT(st_reclaim() == 0);
    ST_ASSERT(st.pressure_count == 0 && nr_free_pages() == st.baseline);
    ST_ASSERT(va_pa_offset == st.mapping);
    for (size_t i = 0; i < ST_REGIONS; i++) ST_ASSERT(!st.region[i].live);
    ST_ASSERT(st.successes == st.frees);
}
static struct slub_slab *
st_slab(size_t id) {
    ST_ASSERT(id < ST_OBJECTS && st.object[id].live && st.object[id].klass < 8);
    return (struct slub_slab *)st.region[st.object[id].region].address;
}
static void
st_null(size_t size, size_t calls) {
    st_context("expected_NULL",size,NULL);
    size_t before = nr_free_pages(), attempts = st.calls, successes = st.successes;
    st.expected_pages = size <= 2048 ? 1 : 0;
    if (size > 2048 && size <= (size_t)-1 - st_large_offset()) {
        size_t bytes = size + st_large_offset();
        st.expected_pages = bytes / PGSIZE + (bytes % PGSIZE != 0);
    }
    void *p = kmalloc(size); st.pointer = p;
    st.expected_pages = 0;
    ST_ASSERT(p == NULL && nr_free_pages() == before);
    if (calls != (size_t)-1) ST_ASSERT(st.calls == attempts + calls);
    ST_ASSERT(st.successes == successes);
    slub_test_invariants(); st_verify_data();
}

static void
slub_test_init(void) {
    st.scenario = "init"; st_context("slub_init",0,NULL);
    st_noop_snapshot(); ST_ASSERT(kmalloc(0) == NULL); kfree(NULL); st_noop_unchanged();
    size_t calls = st.calls, before = nr_free_pages();
    slub_init();
    ST_ASSERT(st.calls == calls && nr_free_pages() == before);
    for (unsigned int k = 0; k < 8; k++) {
        ST_ASSERT(slub_caches[k].active == NULL);
        for (size_t state = 0; state < 3; state++) ST_ASSERT(list_empty(&slub_caches[k].slabs[state]));
    }
    ST_ASSERT(list_empty(&slub_large_allocations));
    slub_test_invariants();
    st_noop_snapshot(); st_null(0,0); size_t frees = st.frees; kfree(NULL); st_noop_unchanged();
    ST_ASSERT(st.frees == frees && st.calls == calls && nr_free_pages() == before);
    size_t small = st_alloc(33,0), large = st_alloc(9001,0);
    size_t live_before = nr_free_pages(); calls = st.calls; frees = st.frees;
    void *saved_small = st.object[small].ptr, *saved_large = st.object[large].ptr;
    slub_init(); slub_init();
    ST_ASSERT(st.object[small].ptr == saved_small && st.object[large].ptr == saved_large);
    ST_ASSERT(st.calls == calls && st.frees == frees && nr_free_pages() == live_before);
    st_noop_snapshot(); st_null(0,0); kfree(NULL); st_noop_unchanged(); st_verify_data();
    st_cleanup(); cprintf("slub_test_init: PASS\n");
}
static void
slub_test_size_boundaries(void) {
    st.scenario = "size_boundaries";
    for (unsigned int k = 0; k < 8; k++) {
        size_t c = st_classes[k];
        st_alloc(c-1,0); st_alloc(c,0); st_alloc(c+1,0);
    }
    static const size_t extra[] = {1,3,5,17,33,100,1000};
    for (size_t i = 0; i < sizeof(extra)/sizeof(extra[0]); i++) st_alloc(extra[i],0);
    st_verify_data(); slub_test_invariants();
    st_cleanup(); cprintf("slub_test_size_boundaries: PASS\n");
}
static void
slub_test_lifecycle(void) {
    st.scenario = "lifecycle";
    for (unsigned int k = 0; k < 8; k++) {
        size_t capacity = (PGSIZE-st_small_offset())/st_classes[k];
        ST_ASSERT(3 * capacity <= ST_OBJECTS);
        size_t calls = st.calls;
        st.vector[0] = st_alloc(st_classes[k],0);
        struct slub_slab *a = st_slab(st.vector[0]);
        ST_ASSERT(st.calls == calls+1 && st.successes == st.frees+1);
        for (size_t i = 1; i < capacity; i++) {
            st.vector[i] = st_alloc(st_classes[k],0);
            ST_ASSERT(st_slab(st.vector[i]) == a);
        }
        ST_ASSERT(st.calls == calls+1 && a->state == SLUB_FULL && slub_caches[k].active != a);
        st.vector[capacity] = st_alloc(st_classes[k],0);
        ST_ASSERT(st_slab(st.vector[capacity]) != a && st.calls == calls+2);
        st_free(st.vector[0]);
        /* Capacity one has no reachable partial state: full goes to empty. */
        ST_ASSERT(a->state == (capacity == 1 ? SLUB_EMPTY : SLUB_PARTIAL));
        size_t reuse_calls = st.calls;
        size_t reused = st_alloc(st_classes[k],0);
        ST_ASSERT(st.calls == reuse_calls);
        st_free(reused); st_cleanup();

        /* Three slabs: active C, other partial A, empty B. No slot order oracle. */
        if (capacity > 1) {
            calls = st.calls;
            for (size_t i = 0; i < 2 * capacity + 1; i++) st.vector[i] = st_alloc(st_classes[k],0);
            a = st_slab(st.vector[0]);
            struct slub_slab *b = st_slab(st.vector[capacity]);
            struct slub_slab *c = st_slab(st.vector[2*capacity]);
            ST_ASSERT(a != b && a != c && b != c && st.calls == calls+3);
            st_free(st.vector[0]);
            for (size_t i = capacity; i < 2*capacity; i++) st_free(st.vector[i]);
            ST_ASSERT(a->state == SLUB_PARTIAL && b->state == SLUB_EMPTY && c->state == SLUB_PARTIAL);
            /* The spec does not fix retargeting on free. Install a valid
             * extra active reference solely to construct the priority case. */
            slub_caches[k].active = c;
            calls = st.calls;
            size_t id = st_alloc(st_classes[k],0); ST_ASSERT(st_slab(id) == c);
            size_t visits = 0;
            while (!list_empty(&slub_caches[k].slabs[SLUB_PARTIAL])) {
                ST_ASSERT(++visits <= 3 * capacity);
                slub_caches[k].active = NULL; /* legal absence of extra ref */
                id = st_alloc(st_classes[k],0); ST_ASSERT(st_slab(id) != b);
            }
            slub_caches[k].active = NULL;
            id = st_alloc(st_classes[k],0); ST_ASSERT(st_slab(id) == b);
            ST_ASSERT(st.calls == calls);
            st_verify_data(); st_cleanup();
        }
    }
    cprintf("slub_test_lifecycle: PASS (capacity-one full->empty included)\n");
}
static void
slub_test_data_integrity(void) {
    st.scenario = "data_integrity";
    size_t capacity = (PGSIZE-st_small_offset())/64, n = capacity+7;
    ST_ASSERT(n + 24 <= ST_OBJECTS);
    for (size_t i = 0; i < n; i++) st.vector[i] = st_alloc(63,0);
    static const size_t sizes[] = {1,17,100,257,1000,2048,2049,4097};
    for (size_t i = 0; i < 24; i++) st.vector[n+i] = st_alloc(sizes[i%8],0);
    st_verify_data();
    for (size_t i = 0; i < n+24; i += 3) { st_free(st.vector[i]); st_verify_data(); }
    for (size_t i = 0; i < 19; i++) st_alloc(sizes[(i*3)%8],0);
    st_reclaim(); st_verify_data();
    st_cleanup(); cprintf("slub_test_data_integrity: PASS\n");
}
static void
slub_test_large_alloc(void) {
    st.scenario = "large_alloc";
    size_t offset = st_large_offset();
    ST_ASSERT(offset < PGSIZE && PGSIZE-offset-1 > 2048);
    size_t sizes[] = {2049,PGSIZE-1,PGSIZE,PGSIZE+1,2*PGSIZE+17,
                     PGSIZE-offset-1,PGSIZE-offset,PGSIZE-offset+1,
                     2*PGSIZE-offset-1,2*PGSIZE-offset,2*PGSIZE-offset+1};
    size_t n = sizeof(sizes)/sizeof(sizes[0]); ST_ASSERT(n <= ST_OBJECTS);
    for (size_t i = 0; i < n; i++) {
        size_t before = nr_free_pages(), attempts = st.calls;
        st.vector[i] = st_alloc(sizes[i],0);
        size_t expected = (offset+sizes[i])/PGSIZE + ((offset+sizes[i])%PGSIZE != 0);
        ST_ASSERT(st.calls == attempts+1 && nr_free_pages() == before-expected);
        ST_ASSERT(st.region[st.object[st.vector[i]].region].pages == expected);
    }
    st_alloc(17,0); st_alloc(513,0); st_verify_data();
    for (size_t i = 0; i < n; i += 2) { size_t frees = st.frees; st_free(st.vector[i]); ST_ASSERT(st.frees == frees+1); st_verify_data(); }
    st_reclaim();
    for (size_t i = 1; i < n; i += 2) { st_free(st.vector[i]); st_verify_data(); }
    st_cleanup(); cprintf("slub_test_large_alloc: PASS\n");
}

static void
st_pressure_all(void) {
    ST_ASSERT(st.pressure_count == 0 && npage - nbase <= ST_PHYSICAL_PAGES);
    while (nr_free_pages() != 0) {
        ST_ASSERT(st.pressure_count < ST_PHYSICAL_PAGES);
        struct Page *page = alloc_page();
        ST_ASSERT(page != NULL); st_page_index(page,1);
        st.pressure[st.pressure_count++] = page;
    }
    ST_ASSERT(alloc_page() == NULL); slub_test_invariants();
}
static void
st_pressure_release(void) {
    while (st.pressure_count != 0) {
        struct Page *p = st.pressure[--st.pressure_count]; free_page(p);
    }
    slub_test_invariants();
}
static void
slub_test_failure(void) {
    st.scenario = "failure";
    size_t offset = st_large_offset();
    size_t bad[] = {(size_t)-1,(size_t)-2,(size_t)-16,(size_t)-32,
                   (size_t)-1-offset+1};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) st_null(bad[i],0);
    /* Arithmetic is representable here: the specification allows either an
     * early capacity rejection or delegation of the exact page count. */
    st_null((size_t)-1-offset,(size_t)-1);
    st_null((npage-nbase)*PGSIZE,(size_t)-1);
    size_t capacity = (PGSIZE-st_small_offset())/64;
    for (size_t i = 0; i < capacity; i++) st.vector[i] = st_alloc(64,0);
    st_alloc(31,0);
    st_context("exhaust_real_Buddy",0,NULL); st_pressure_all();
    st_null(16,1); st_null(64,1); st_null(9001,1);
    size_t calls = st.calls, id = st_alloc(31,0);
    ST_ASSERT(st.calls == calls && nr_free_pages() == 0); st_free(id);
    st_verify_data(); st_pressure_release();
    /* Same requests can succeed again after resource pressure is removed. */
    st_alloc(16,0); st_alloc(64,0); st_alloc(9001,0);
    st_cleanup();

    /* Existing fallible step AFTER backend success: direct-map validation.
     * Test observers use the saved actual mapping, never the perturbed value. */
    st_alloc(31,0);
    const size_t converted[] = {1024,9001};
    for (size_t i = 0; i < 2; i++) {
        st_context("conversion_failure",converted[i],NULL);
        uint64_t mapping = va_pa_offset;
        size_t before = nr_free_pages(), success = st.successes, frees = st.frees;
        ST_ASSERT(mapping < (uint64_t)-1);
        va_pa_offset = mapping+1;
        void *p = kmalloc(converted[i]);
        va_pa_offset = mapping; /* restore before any test assertion can fail */
        ST_ASSERT(p == NULL && nr_free_pages() == before);
        ST_ASSERT(st.successes == success+1 && st.frees == frees+1);
        slub_test_invariants(); st_verify_data();
        id = st_alloc(converted[i],0); st_free(id);
    }
    st_cleanup();
#ifdef SLUB_TEST
    slub_test_fail_next = 1; st_null(128,1);
    ST_ASSERT(!slub_test_fail_next); id = st_alloc(128,0); st_free(id); st_cleanup();
    st_alloc(2048,0); /* one slot, existing slab now full */
    slub_test_fail_next = 1; st_null(2048,1);
    id = st_alloc(2048,0); st_free(id); st_cleanup();
    st_alloc(31,0); calls = st.calls;
    slub_test_fail_next = 1; id = st_alloc(31,0);
    ST_ASSERT(st.calls == calls && slub_test_fail_next);
    st_null(9001,1); ST_ASSERT(!slub_test_fail_next);
    st_alloc(9001,0); st_cleanup();
    cprintf("slub_test_failure: PASS (real Buddy pressure + fail-next + rollback)\n");
#else
    cprintf("slub_test_failure: PASS (real Buddy pressure + rollback)\n");
#endif
}
static void
slub_test_reclaim(void) {
    st.scenario = "reclaim";
    for (unsigned int k = 0; k < 8; k++) st.vector[k] = st_alloc(st_classes[k],0);
    for (unsigned int k = 0; k < 8; k++) st_free(st.vector[k]);
    ST_ASSERT(st_reclaim() == 8 && st_reclaim() == 0);
    size_t small = st_alloc(100,0), large = st_alloc(9001,0);
    for (unsigned int k = 0; k < 8; k++) {
        size_t id = st_alloc(st_classes[k],0); st_free(id);
    }
    size_t live_small = st.object[small].region, live_large = st.object[large].region;
    st_reclaim();
    ST_ASSERT(st.region[live_small].live && st.region[live_large].live);
    st_verify_data(); ST_ASSERT(st_reclaim() == 0);
    st_cleanup(); cprintf("slub_test_reclaim: PASS\n");
}
static unsigned int
st_random(unsigned int *seed) {
    *seed ^= *seed << 13; *seed ^= *seed >> 17; *seed ^= *seed << 5; return *seed;
}
static void
slub_test_sequences(void) {
    st.scenario = "sequences"; st.seed = 0x51ab2026U;
    unsigned int random = st.seed;
    static const size_t sizes[] = {1,3,5,15,16,17,31,32,33,63,64,65,127,128,129,
        255,256,257,511,512,513,1000,1023,1024,1025,2047,2048,2049,4095,4096,4097,8209};
    st.trace = 1;
    for (st.step = 0; st.step < 256; st.step++) {
        size_t slot = st_random(&random) % 48;
        if (st.object[slot].live) st_free(slot);
        else {
            size_t size = sizes[st_random(&random) % (sizeof(sizes)/sizeof(sizes[0]))];
#ifdef SLUB_TEST
            if (st.step % 37 == 0) { size = 8209; slub_test_fail_next = 1; }
#endif
            st_alloc(size,1);
#ifdef SLUB_TEST
            slub_test_fail_next = 0;
#endif
        }
        if (st.step % 17 == 0) st_reclaim();
        slub_test_invariants(); st_verify_data();
    }
    st_cleanup(); st.trace = 0;
    cprintf("slub_test_sequences: PASS seed=%08x steps=256\n",st.seed);
    st.seed = 0; st.step = 0;
}

static void
st_begin(void) {
    /* Refuse to reset or test-destroy someone else's live allocations. */
    if (slub_ready) {
        for (size_t k = 0; k < 8; k++) {
            if (!list_empty(&slub_caches[k].slabs[SLUB_PARTIAL]) ||
                !list_empty(&slub_caches[k].slabs[SLUB_FULL]))
                panic("SLUB specification tests require startup/idle heap; foreign live small objects preserved");
        }
        if (!list_empty(&slub_large_allocations))
            panic("SLUB specification tests require startup/idle heap; foreign live large objects preserved");
    }
    st.enabled = 0;
    slub_reclaim(); /* permitted pre-test cleanup, before capturing baseline */
    memset(st.object,0,sizeof(st.object)); memset(st.region,0,sizeof(st.region));
    st.baseline = nr_free_pages(); st.mapping = va_pa_offset;
    st.calls = st.successes = st.frees = st.freed_pages = st.generation = 0;
    st.pressure_count = st.serial = st.seed = st.step = st.expected_pages = 0;
    st.trace = 0; st.scenario = "begin"; st.operation = "none"; st.pointer = NULL; st.request = 0;
#ifdef SLUB_TEST
    slub_test_fail_next = 0;
#endif
    st.enabled = 1;
}
void
slub_check(void) {
    for (unsigned int round = 1; round <= 2; round++) {
        st_begin();
        cprintf("slub_check round=%u begin free=%lu\n",round,(unsigned long)st.baseline);
        slub_test_init();
        slub_test_size_boundaries();
        slub_test_lifecycle();
        slub_test_data_integrity();
        slub_test_large_alloc();
        slub_test_failure();
        slub_test_reclaim();
        slub_test_sequences();
        st_cleanup();
        ST_ASSERT(nr_free_pages() == st.baseline);
        cprintf("slub_check round=%u restored free=%lu: PASS\n",round,(unsigned long)st.baseline);
        st.enabled = 0;
    }
    cprintf("slub_check() succeeded!\n");
}
