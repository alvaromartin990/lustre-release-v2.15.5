#ifndef _OSD_KTIME_H_
#define _OSD_KTIME_H_

#include <linux/ktime.h>
#include <linux/time64.h>

/* Replacement for ktime_get() */
static inline ktime_t osd_ktime_get(void)
{
    return ktime_get_real();
}

/* Replacement for ktime_get_with_offset() */
static inline ktime_t osd_ktime_get_with_offset(enum tk_offsets offs)
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

#endif /* _OSD_KTIME_H_ */