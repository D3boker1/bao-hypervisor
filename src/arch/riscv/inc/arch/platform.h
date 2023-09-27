/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#ifndef __ARCH_PLATFORM_H__
#define __ARCH_PLATFORM_H__

#include <bao.h>

#define PLIC  (1)
#define APLIC (2)

struct arch_platform {
    union irqc_dscrp {
        struct {
            paddr_t base;
        } plic;
        struct {
            struct {
                paddr_t base;
            } aplic;
        } aia;
    } irqc;
};

#endif /* __ARCH_PLATFORM_H__ */
