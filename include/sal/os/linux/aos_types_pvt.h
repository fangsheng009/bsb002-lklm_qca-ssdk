#ifndef _AOS_PVTTYPES_H
#define _AOS_PVTTYPES_H

#ifdef GCCV4
#ifdef KVER32
#include <generated/autoconf.h>
#else
#include <linux/autoconf.h>
#endif
#endif
#include <asm/types.h>
#include <linux/compiler.h>
/*
 * Private definitions of general data types
 */

/* generic data types */
typedef struct device *   __aos_device_t;
typedef int               __aos_size_t;

#ifdef KVER26
#ifdef LNX26_22
typedef __u8 * __aos_iomem_t;
#else
typedef u8 __iomem * __aos_iomem_t;
#endif
#else /*Linux Kernel 2.4 */
typedef   u8         * __aos_iomem_t;
#endif

#ifdef KVER32
typedef u8 __iomem * __aos_iomem_t;
#endif

#ifdef LNX26_22 /* > Linux 2.6.22 */
typedef   __u8              __a_uint8_t;
typedef   __s8              __a_int8_t;
typedef   __u16             __a_uint16_t;
typedef   __s16             __a_int16_t;
typedef   __u32             __a_uint32_t;
typedef   __s32             __a_int32_t;
typedef   __u64             __a_uint64_t;
typedef   __s64             __a_int64_t;
#else
typedef   u8              __a_uint8_t;
typedef   s8              __a_int8_t;
typedef   u16             __a_uint16_t;
typedef   s16             __a_int16_t;
typedef   u32             __a_uint32_t;
typedef   s32             __a_int32_t;
typedef   u64             __a_uint64_t;
typedef   s64             __a_int64_t;
#endif

#define aos_printk        printk

// #define AUTO_UPDATE_PPPOE_INFO 1
#undef AUTO_UPDATE_PPPOE_INFO

#ifdef AUTO_UPDATE_PPPOE_INFO
#include "lib/ppp_generic.h"
#endif

#endif
