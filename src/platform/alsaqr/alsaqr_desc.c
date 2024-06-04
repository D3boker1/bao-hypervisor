/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#include <platform.h>
#include <interrupts.h>

struct platform platform = {

    .cpu_num = 2,

    .region_num = 1,
    .regions =  (struct mem_region[]) {
        {
            .base = 0x80200000,
            .size = 0x20000000 - 0x200000
        }
    },

    .console = {
        .base = 0x40000000,
    },

    .arch = {
        #if (IRQC == PLIC)
        .irqc.plic.base = 0xc000000,
        #elif (IRQC == APLIC)
        .irqc.aia.aplic.base = 0xd000000,
        #elif (IRQC == AIA)
        .irqc.aia.aplic.base = 0xd000000,
        .irqc.aia.imsic.base = 0x28000000,
        #else
        #error "unknown IRQC type " IRQC
        #endif
        .iommu = {
            .base = 0x50010000,
            .fq_irq_id = 152
        }
    }

};
