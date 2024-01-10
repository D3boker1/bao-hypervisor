
/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#ifndef IRQC_H
#define IRQC_H

#include <aplic.h>
#include <cpu.h>
#include <vaplic.h>
#if (IRQC == APLIC)
#include <arch/sbi.h>
#elif (IRQC == AIA)
#include <imsic.h>
#endif

#define IRQC_TIMR_INT_ID (APLIC_MAX_INTERRUPTS + 1)
#define IRQC_SOFT_INT_ID (APLIC_MAX_INTERRUPTS + 2)
#if (IRQC == APLIC)
#define IRQC_MAX_INTERRUPTS (IRQC_SOFT_INT_ID + 1)
#elif (IRQC == AIA)
/**
 * Why IRQC_TIMR_INT_ID and not IRQC_SOFT_INT_ID? With the AIA specification, the software interrupt
 * is now delivered through the IMSIC, which means that the target hart will see it as an external 
 * interrupt. Thus, the total number of interrupts is the maximum number of interrupts supported by 
 * aplic, the timer interrupt, the maximum number of interrupts supported by imsic, and one to 
 * support/keep "<" logic.
 */
#define IRQC_MAX_INTERRUPTS (IRQC_TIMR_INT_ID + IMSIC_MAX_INTERRUPTS + 1)
#define IRQC_MSI_INTERRUPTS_START_ID (IRQC_TIMR_INT_ID)
#else
#error "IRQC not defined"
#endif

#define HART_REG_OFF         APLIC_IDC_OFF
#define IRQC_HART_INST       APLIC_DOMAIN_NUM_HARTS
#define HYP_IRQ_SM_EDGE_RISE APLIC_SOURCECFG_SM_EDGE_RISE
#define HYP_IRQ_SM_INACTIVE  APLIC_SOURCECFG_SM_INACTIVE
#define HYP_IRQ_PRIO         APLIC_TARGET_MAX_PRIO

static inline void irqc_init()
{
    aplic_init();
}

static inline void irqc_cpu_init()
{
    aplic_idc_init();
}

static inline irqid_t irqc_reserve(irqid_t pintp_id)
{
    #if (IRQC == APLIC)
    return pintp_id;
    #else
    #error "IRQC not defined"
    #endif
}

static inline void irqc_send_ipi(cpuid_t target_cpu, irqid_t ipi_id)
{
    #if (IRQC == APLIC)
    sbi_send_ipi(1ULL << target_cpu, 0);
    #endif
}

static inline void irqc_config_irq(irqid_t int_id, bool en)
{
    if (en) {
        aplic_set_sourcecfg(int_id, HYP_IRQ_SM_EDGE_RISE);
        aplic_set_enbl(int_id);
        aplic_set_target_hart(int_id, cpu()->id);
        aplic_set_target_prio(int_id, HYP_IRQ_PRIO);
    } else {
        aplic_clr_enbl(int_id);
    }
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

static inline void virqc_set_hw(struct vm* vm, irqid_t id)
{
    vaplic_set_hw(vm, id);
}

#endif // IRQC_H
