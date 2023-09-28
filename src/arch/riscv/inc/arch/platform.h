/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#ifndef ARCH_PLATFORM_H
#define ARCH_PLATFORM_H

#include <bao.h>

// Arch-specific platform data
struct arch_platform {
    union irqc_dscrp {
        struct {
            paddr_t base;
        } plic;
    } irqc;

    struct {
        paddr_t base;       // Base address of the IOMMU mmapped IF
        unsigned mode;      // Overall IOMMU mode (Off, Bypass, DDT-lvl)
        irqid_t fq_irq_id;  // Fault Queue IRQ ID (wired)
    } iommu;
};

#endif /* ARCH_PLATFORM_H */
