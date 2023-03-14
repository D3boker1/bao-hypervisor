/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#ifndef __ARCH_PLATFORM_H__
#define __ARCH_PLATFORM_H__

#include <bao.h>

#define PLIC  (1)
#define APLIC (2)
#define AIA   (3)

struct arch_platform {
    paddr_t plic_base;
};

#endif /* __ARCH_PLATFORM_H__ */
