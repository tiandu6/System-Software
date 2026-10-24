#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>

/* Single hart, serial callers only. Never call from an interrupt handler. */
void slub_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
size_t slub_reclaim(void);
void slub_check(void);

#endif /* !__KERN_MM_SLUB_H__ */
