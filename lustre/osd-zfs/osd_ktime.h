/* osd_ktime.h */
#ifndef __OSD_KTIME_H__
#define __OSD_KTIME_H__

#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/ts64.h> /* timespec64_to_ns */

static inline u64 ktime_real_ns_safe(void)
{
    struct timespec64 ts;
    ktime_get_real_ts64(&ts);  /* exported on your kernel */
    return timespec64_to_ns(&ts);
}

static inline u64 ktime_ns_safe(void)
{
    struct timespec64 ts;
    ktime_get_ts64(&ts);      /* exported on your kernel */
    return timespec64_to_ns(&ts);
}

/* If you used ktime_get_with_offset(), replace with this. The "with_offset"
 * semantics vary; many kernels export ktime_get_coarse_with_offset or
 * ktime_get_coarse_real_ts64. If you need "with_offset" to add/subtract an
 * offset in micro/nano seconds, compute it in C around the safe functions. */
static inline u64 ktime_with_offset_ns_safe(s64 offset_ns)
{
    /* return current real time in ns plus offset (offset in ns, can be negative) */
    return (s64)ktime_real_ns_safe() + offset_ns;
}

#endif /* __OSD_KTIME_H__ */
