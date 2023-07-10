/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#include <aplic.h>
#include <cpu.h>
#include <interrupts.h>
#include <fences.h>

/** APLIC fields and masks defines */
#define APLIC_DOMAINCFG_CTRL_MASK       (0x1FF)

#define DOMAINCFG_DM                    (1U << 2)

#define INTP_IDENTITY                   (16)
#define INTP_IDENTITY_MASK              (0x3FF)

#define APLIC_DISABLE_IDELIVERY	        (0)
#define APLIC_ENABLE_IDELIVERY	        (1)
#define APLIC_DISABLE_IFORCE	        (0)
#define APLIC_ENABLE_IFORCE	            (1)
#define APLIC_IDC_ITHRESHOLD_EN_ALL     (0)
#define APLIC_IDC_ITHRESHOLD_DISBL_ALL  (1)

/** APLIC public data */
volatile struct aplic_global_hw *aplic_global;
volatile struct aplic_hart_hw *aplic_hart;
uint8_t APLIC_IPRIOLEN = 0;

void aplic_init(void)
{
    /** Maps APLIC device */
    aplic_global = (void*) mem_alloc_map_dev(&cpu()->as, SEC_HYP_GLOBAL, INVALID_VA, 
            platform.arch.irqc.aia.aplic.base, NUM_PAGES(sizeof(struct aplic_global_hw)));
        
    aplic_hart = (void*) mem_alloc_map_dev(&cpu()->as, SEC_HYP_GLOBAL, INVALID_VA, 
        platform.arch.irqc.aia.aplic.base + HART_REG_OFF,
        NUM_PAGES(sizeof(struct aplic_hart_hw)*IRQC_HART_INST));
    
    /** Ensure that instructions after fence have the APLIC fully mapped */
    fence_sync();

    aplic_global->domaincfg = 0;

    /** Clear all pending and enabled bits*/
    for (size_t i = 0; i < APLIC_NUM_CLRIx_REGS; i++) {
        aplic_global->setip[i] = 0;
        aplic_global->setie[i] = 0;
    }

    /** Sets the default value of target and sourcecfg */
    for (size_t i = 0; i < APLIC_NUM_TARGET_REGS; i++){
        aplic_global->sourcecfg[i] = APLIC_SOURCECFG_SM_INACTIVE;
        aplic_global->target[i] = APLIC_TARGET_MIN_PRIO;
    }
    APLIC_IPRIOLEN = aplic_global->target[0] & APLIC_TARGET_IPRIO_MASK; 
    aplic_global->domaincfg |= APLIC_DOMAINCFG_IE;
}

void aplic_idc_init(void){
    uint32_t idc_index = cpu()->id;
    aplic_hart[idc_index].ithreshold = APLIC_IDC_ITHRESHOLD_EN_ALL;  
    aplic_hart[idc_index].iforce = APLIC_DISABLE_IFORCE;
    aplic_hart[idc_index].idelivery = APLIC_ENABLE_IDELIVERY;
}

void aplic_set_sourcecfg(irqid_t intp_id, uint32_t val)
{
    aplic_global->sourcecfg[intp_id - 1] = val & APLIC_SOURCECFG_SM_MASK;
}

uint32_t aplic_get_sourcecfg(irqid_t intp_id)
{
    return aplic_global->sourcecfg[intp_id - 1];
}

void aplic_set_pend(irqid_t intp_id)
{
    aplic_global->setipnum = intp_id;
}

void aplic_set_pend_reg(size_t reg_indx, uint32_t reg_val)
{
    aplic_global->setip[reg_indx] = reg_val;
}

bool aplic_get_pend(irqid_t intp_id)
{
    uint32_t reg_indx = intp_id / 32;
    uint32_t intp_to_pend_mask = (1U << (intp_id % 32));

    return (aplic_global->setip[reg_indx] & intp_to_pend_mask) != 0;
}

uint32_t aplic_get_pend_reg(size_t reg_indx)
{
    return aplic_global->setip[reg_indx];
}

void aplic_clr_pend(irqid_t intp_id)
{
    aplic_global->clripnum = intp_id;
}

void aplic_clr_pend_reg(size_t reg_indx, uint32_t reg_val)
{
    aplic_global->in_clrip[reg_indx] = reg_val;
}

uint32_t aplic_get_inclrip_reg(size_t reg_indx)
{
    return aplic_global->in_clrip[reg_indx];
}

void aplic_set_enbl(irqid_t intp_id)
{
    aplic_global->setienum = intp_id;
}

void aplic_set_enbl_reg(size_t reg_indx, uint32_t reg_val)
{
    aplic_global->setie[reg_indx] = reg_val;
}

bool aplic_get_enbl(irqid_t intp_id){
    uint32_t reg_indx = intp_id / 32;
    uint32_t intp_to_pend_mask = (1U << (intp_id % 32));

    return (aplic_global->setie[reg_indx] & intp_to_pend_mask) != 0;
}

void aplic_clr_enbl(irqid_t intp_id)
{
    aplic_global->clrienum = intp_id;
}

void aplic_clr_enbl_reg(size_t reg_indx, uint32_t reg_val)
{
    aplic_global->clrie[reg_indx] = reg_val;
}

void aplic_set_target_prio(irqid_t intp_id, uint8_t prio)
{
    aplic_global->target[intp_id - 1] &= ~(APLIC_TARGET_IPRIO_MASK);
    aplic_global->target[intp_id - 1] |= (prio & APLIC_TARGET_IPRIO_MASK);
}

void aplic_set_target_hart(irqid_t intp_id, cpuid_t hart)
{
    aplic_global->target[intp_id - 1] &= ~(APLIC_TARGET_HART_IDX_MASK << 
                                           APLIC_TARGET_HART_IDX_SHIFT);
    aplic_global->target[intp_id - 1] |= hart << APLIC_TARGET_HART_IDX_SHIFT;
}

uint8_t aplic_get_target_prio(irqid_t intp_id)
{
    return aplic_global->target[intp_id - 1] & APLIC_TARGET_IPRIO_MASK;
}

cpuid_t aplic_get_target_hart(irqid_t intp_id)
{
    return (aplic_global->target[intp_id - 1] >> APLIC_TARGET_HART_IDX_SHIFT) &
                                                 APLIC_TARGET_HART_IDX_MASK;
}

irqid_t aplic_idc_get_claimi_intpid(idcid_t idc_id)
{
    return (aplic_hart[idc_id].claimi >> IDC_CLAIMI_INTP_ID_SHIFT) 
                                                    & IDC_CLAIMI_INTP_ID_MASK;
}

void aplic_handle(void){
    irqid_t intp_identity = 0;
    idcid_t idc_id = cpu()->id;

    while((intp_identity = aplic_idc_get_claimi_intpid(idc_id)) > 0){
        interrupts_handle(intp_identity);
    }
}