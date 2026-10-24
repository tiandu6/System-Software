/* Independent host entry. Real writable backing, real Buddy and PMM dispatch.
 * pages[0] maps nbase; va_pa_offset maps those PAs into the mmap backing. */
#include "../kern/mm/slub.c"
#include <buddy_pmm.h>
extern int puts(const char *s);
extern int printf(const char *fmt, ...);
extern int vprintf(const char *fmt, va_list ap);
extern int fflush(void *stream);
extern void abort(void) __attribute__((noreturn));
extern void *mmap(void *address, size_t length, int prot, int flags, int fd, long offset);
#define HOST_PAGES 512
static struct Page descriptors[HOST_PAGES];
int cprintf(const char *fmt, ...) {
    va_list ap; va_start(ap,fmt); int result = vprintf(fmt,ap); va_end(ap); return result;
}
void __panic(const char *file, int line, const char *fmt, ...) {
    printf("PANIC at %s:%d: ",file,line);
    va_list ap; va_start(ap,fmt); vprintf(fmt,ap); va_end(ap);
    puts(""); fflush(NULL); abort();
}
static void setup(void) {
    void *memory = mmap(NULL,HOST_PAGES*PGSIZE,3,0x22,-1,0);
    assert(memory != (void *)-1 && (uintptr_t)memory % PGSIZE == 0);
    pages = descriptors; npage = nbase+HOST_PAGES;
    va_pa_offset = (uintptr_t)memory-(nbase << PGSHIFT);
    pmm_manager = &buddy_pmm_manager; pmm_manager->init();
    for (size_t i = 0; i < HOST_PAGES; i++) {
        descriptors[i] = (struct Page){0}; SetPageReserved(descriptors+i);
    }
    pmm_manager->init_memmap(descriptors,HOST_PAGES);
    struct Page *p = alloc_pages(3); assert(p && nr_free_pages() == HOST_PAGES-3);
    free_page(p+1); free_page(p); free_page(p+2);
    p = alloc_pages(5); assert(p && nr_free_pages() == HOST_PAGES-5); free_pages(p,5);
    assert(nr_free_pages() == HOST_PAGES);
}
static int negative(const char *name) {
    if (strcmp(name,"foreign_live") == 0) {
        assert(kmalloc(31) != NULL);
        slub_check(); return 3;
    }
    st_begin(); slub_test_init(); st.scenario = name;
    size_t id = st_alloc(16,0);
    struct slub_slab *s = st_slab(id);
    if (strcmp(name,"freelist_cycle") == 0) {
        assert(s->freelist); *(void **)s->freelist = s->freelist;
    } else if (strcmp(name,"freelist_live") == 0) {
        s->freelist = st.object[id].ptr;
    } else if (strcmp(name,"freelist_cross_slab") == 0) {
        size_t other = st_alloc(32,0); s->freelist = st_slab(other)->freelist;
    } else if (strcmp(name,"wrong_capacity") == 0) {
        s->capacity++;
    } else if (strcmp(name,"wrong_state") == 0) {
        s->state = SLUB_FULL;
    } else if (strcmp(name,"active_full") == 0) {
        size_t other = st_alloc(2048,0); struct slub_slab *full = st_slab(other);
        assert(full->inuse == full->capacity); full->cache->active = full;
    } else if (strcmp(name,"retired_reference") == 0) {
        struct slub_cache *cache = s->cache;
        st_free(id); st_reclaim(); cache->active = s;
    } else if (strcmp(name,"noop_state") == 0) {
        st_noop_snapshot();
        s->cache->active = s->cache->active == NULL ? s : NULL;
        st_noop_unchanged();
    } else if (strcmp(name,"backend_pair") == 0) {
        struct st_region *r = &st.region[st.object[id].region];
        slub_test_backend_release(r->base,r->pages+1);
    } else if (strcmp(name,"backend_duplicate") == 0) {
        struct st_region old = st.region[st.object[id].region];
        st_free(id); st_reclaim(); slub_test_backend_release(old.base,old.pages);
    } else return 2;
    slub_test_invariants(); return 3;
}
int main(int argc, char **argv) {
    setup();
    if (argc > 1) return negative(argv[1]);
    puts("PASS: actual Buddy exact 3/5-page prerequisite and coherent host mapping");
    /* Cold init occurs in the first required test; second call remains warm. */
    slub_check();
    assert(!st.enabled && nr_free_pages() == HOST_PAGES);
    slub_check();
    assert(!st.enabled && nr_free_pages() == HOST_PAGES && slub_reclaim() == 0);
    puts("All SLUB specification tests passed (two calls, four fully restored rounds).");
    return 0;
}
