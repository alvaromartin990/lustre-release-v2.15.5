#ifndef _OSD_KTIME_H
#define _OSD_KTIME_H

#include <linux/ktime.h>
#include <linux/time64.h>

/*
 * Compatibility shims for kernels that don't export
 * ktime_get() or ktime_get_with_offset().
 */
#ifndef HAVE_KTIME_GET
static inline ktime_t ktime_get(void)
{
    return ktime_get_real();
}
#endif

#ifndef HAVE_KTIME_GET_WITH_OFFSET
static inline ktime_t ktime_get_with_offset(enum tk_offsets offs)
{
    switch (offs) {
    case TK_OFFS_REAL:
        return ktime_get_real();
    case TK_OFFS_BOOT:
        return ktime_get_boottime();
    default:
        return ktime_get_real();
    }
}
#endif

#endif /* _OSD_KTIME_H */
