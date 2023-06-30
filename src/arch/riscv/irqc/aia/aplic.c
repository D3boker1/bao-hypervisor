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

#define APLIC_SRCCFG_DEFAULT            APLIC_SRCCFG_DETACH    

/** APLIC public data */
volatile struct aplic_global_hw *aplic_global;
volatile struct aplic_hart_hw *aplic_hart;

bool inline aplic_msi_mode(void)
{
    return (aplic_global->domaincfg & APLIC_DOMAINCFG_DM) && APLIC_DOMAINCFG_DM;
}

/** APLIC public functions */
/** Initialization Functions */
void aplic_init(void)
{
    /** Maps APLIC device */
    aplic_global = (void*) mem_alloc_map_dev(&cpu()->as, SEC_HYP_GLOBAL, INVALID_VA, 
            platform.arch.irqc.aia.aplic.base, NUM_PAGES(sizeof(struct aplic_global_hw)));
        
    aplic_hart = (void*) mem_alloc_map_dev(&cpu()->as, SEC_HYP_GLOBAL, INVALID_VA, 
        platform.arch.irqc.aia.aplic.base + HART_REG_OFF,
        NUM_PAGES(sizeof(struct aplic_hart_hw)*IRQC_HART_INST));
    
    /** Ensure that instructions after fence have the PLIC fully mapped */
    fence_sync();

    aplic_global->domaincfg = 0;
    #if (IRQC == AIA)
    aplic_global->domaincfg |= APLIC_DOMAINCFG_DM;
    #endif

    /** Clear all pending and enabled bits*/
    for (size_t i = 0; i < APLIC_NUM_CLRIx_REGS; i++) {
        aplic_global->setip[i] = 0;
        aplic_global->setie[i] = 0;
    }

    /** Sets the default value of hart index and prio for implemented sources*/
    for (size_t i = 0; i < APLIC_NUM_TARGET_REGS; i++){
        if (!aplic_msi_mode())
        {
            aplic_global->target[i] = APLIC_TARGET_PRIO_DEFAULT;
        } else {
            aplic_global->target[i] = i;
        }
    }
    aplic_global->domaincfg |= APLIC_DOMAINCFG_IE;
}

void aplic_idc_init(void){
    uint32_t idc_index = cpu()->id;
    aplic_hart[idc_index].ithreshold = APLIC_IDC_ITHRESHOLD_EN_ALL;  
    aplic_hart[idc_index].iforce = APLIC_DISABLE_IFORCE;
    aplic_hart[idc_index].idelivery = APLIC_ENABLE_IDELIVERY;
}

/** Domain functions */
void aplic_set_sourcecfg(irqid_t int_id, uint32_t val)
{
    uint32_t real_int_id = int_id - 1;
    aplic_global->sourcecfg[real_int_id] = val & APLIC_SOURCECFG_SM_MASK;
}

uint32_t aplic_get_sourcecfg(irqid_t int_id)
{
    uint32_t ret = 0;
    uint32_t real_int_id = int_id - 1;
    ret = aplic_global->sourcecfg[real_int_id];
    return ret;
}

void aplic_set_pend(irqid_t int_id)
{
    aplic_global->setipnum = int_id;
}

bool aplic_get_pend(irqid_t int_id)
{
    uint32_t reg_indx = int_id / 32;
    uint32_t intp_to_pend_mask = (1U << (int_id % 32));

    return (aplic_global->setip[reg_indx] & intp_to_pend_mask) != 0;
}

void aplic_clr_pend(irqid_t int_id)
{
    aplic_global->clripnum = int_id;
}

bool aplic_get_inclrip(irqid_t int_id)
{
    uint32_t reg_indx = int_id / 32;
    uint32_t intp_to_pend_mask = (1U << (int_id % 32));
    return (aplic_global->in_clrip[reg_indx] & intp_to_pend_mask) != 0;
}

void aplic_set_ienum(irqid_t int_id)
{
    aplic_global->setienum = int_id;
}

void aplic_set_clrienum(irqid_t int_id)
{
    aplic_global->clrienum = int_id;
}

void aplic_set_target(irqid_t int_id, uint32_t val)
{
    uint32_t real_int_id = int_id - 1;
    uint32_t eiid = val & APLIC_TARGET_EEID_MASK;
    uint32_t hart_index = (val >> APLIC_TARGET_HART_IDX_SHIFT);
    uint32_t guest_index = (val >> APLIC_TARGET_GUEST_IDX_SHIFT) 
                            & APLIC_TARGET_GUEST_INDEX_MASK;
    
    /** Evaluate in what delivery mode the domain is configured*/
    /** Direct Mode*/
    if(!aplic_msi_mode()){
        val &= APLIC_TARGET_DIRECT_MASK;
        aplic_global->target[real_int_id] = val;
    }
    /** MSI Mode*/
    else{ 
        val &= APLIC_TARGET_MSI_MASK;
        if((eiid > 0) && (hart_index < PLAT_CPU_NUM) && (guest_index <= 1)){
            aplic_global->target[real_int_id] = val;
        }
    }
}

uint32_t aplic_get_target(irqid_t int_id)
{
    uint32_t real_int_id = int_id - 1;
    uint32_t ret = 0;
    ret = aplic_global->target[real_int_id];
    return ret;
}

/** IDC functions */
void aplic_idc_set_iforce(idcid_t idc_id, bool en)
{
    if(idc_id < APLIC_DOMAIN_NUM_HARTS) {
        if (en){
            aplic_hart[idc_id].iforce = APLIC_ENABLE_IFORCE;
        }else{
            aplic_hart[idc_id].iforce = APLIC_DISABLE_IFORCE;
        }
    }
}

uint32_t aplic_idc_get_claimi(idcid_t idc_id)
{
    uint32_t ret = 0;
    if(idc_id < APLIC_DOMAIN_NUM_HARTS) {
        ret = aplic_hart[idc_id].claimi;
    }
    return ret;
}

/** APLIC Interrupt handler */
void aplic_handle(void){
    uint32_t intp_identity;
    idcid_t idc_id = cpu()->id;

    intp_identity = (aplic_hart[idc_id].claimi >> INTP_IDENTITY) & INTP_IDENTITY_MASK;
    if(intp_identity > 0){
        enum irq_res res = interrupts_handle(intp_identity);
        if (res == HANDLED_BY_HYP){
            /** Read the claimi to clear the interrupt */
            aplic_idc_get_claimi(idc_id);
        } 
    }

}