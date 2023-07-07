
/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#ifndef _IRQC_H_
#define _IRQC_H_

#include <aplic.h>
#include <cpu.h>
#include <vaplic.h>

#define IRQC_MAX_INTERRUPTS (1024)

#define HART_REG_OFF APLIC_IDC_OFF
#define IRQC_HART_INST APLIC_DOMAIN_NUM_HARTS

static inline void irqc_init()
{
    aplic_init();
}

static inline void irqc_cpu_init()
{
    aplic_idc_init();
}

static inline void irqc_set_enbl(irqid_t int_id, bool en)
{
    aplic_set_enbl(int_id);
}

static inline void irqc_set_prio(irqid_t int_id)
{
    aplic_set_target(int_id, (cpu()->id << 18) | 0x01);
}

static inline void irqc_handle()
{
    aplic_handle();
}

static inline bool irqc_get_pend(irqid_t int_id)
{
    return aplic_get_pend(int_id);
}

static inline void irqc_clr_pend(irqid_t int_id)
{
    aplic_clr_pend(int_id);
}

static inline void virqc_set_hw(struct vm *vm, irqid_t id)
{
    vaplic_set_hw(vm, id);
}

#endif //_IRQC_H_