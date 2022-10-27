/**
 * @file aplic.c
 * @author Jose Martins <jose.martins@bao-project.org>
 * @author Francisco Marques (fmarques_00@protonmail.com)
 * @brief Implements the aplic domain and IDC's functions
 * @version 0.1
 * @date 2022-09-23
 * 
 * @copyright Copyright (c) Bao Project (www.bao-project.org), 2019-
 * 
 * Bao is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License version 2 as published by the Free
 * Software Foundation, with a special exception exempting guest code from such
 * license. See the COPYING file in the top-level directory for details.
 * 
 * TODO: Remove unecessary functions. Implement setip and setie functions
 */
#include <aplic.h>
#include <cpu.h>
#include <interrupts.h>

/**==== APLIC fields and masks defines ====*/
#define APLIC_DOMAINCFG_DM              (1U << 2)
#define APLIC_DOMAINCFG_IE              (1U << 8)
#define APLIC_DOMAINCFG_CTRL_MASK       (0x1FF)

#define SRCCFG_D                        (1U << 10)
#define SRCCFG_SM                       (1U << 0) | (1U << 1) | (1U << 2)
#define DOMAINCFG_DM                    (1U << 2)

#define INTP_IDENTITY                   (16)
#define INTP_IDENTITY_MASK              (0x3FF)

#define APLIC_TARGET_HART_IDX_SHIFT     (18)
#define APLIC_TARGET_IPRIO_MASK         (0xFF)
//#define APLIC_MAX_GEI                 (0)
#define APLIC_TARGET_PRIO_DEFAULT       (1)

#define APLIC_DISABLE_IDELIVERY	        (0)
#define APLIC_ENABLE_IDELIVERY	        (1)
#define APLIC_DISABLE_IFORCE	        (0)
#define APLIC_ENABLE_IFORCE	            (1)
#define APLIC_IDC_ITHRESHOLD_EN_ALL     (0)
#define APLIC_IDC_ITHRESHOLD_DISBL_ALL  (1)

#define APLIC_SRCCFG_DEFAULT            APLIC_SRCCFG_DETACH    

/**==== APLIC public data ====*/
volatile struct aplic_global aplic_domain
    __attribute__((section(".devices")));

volatile struct aplic_idc idc[APLIC_PLAT_IDC_NUM] //PLIC_PLAT_CNTXT_NUM
    __attribute__((section(".devices")));


/**==== APLIC private data ====*/
size_t APLIC_IMPL_INTERRUPTS;
size_t APLIC_IMPL_INTERRUPTS_REGS;
uint32_t impl_src[APLIC_MAX_INTERRUPTS];

/**==== APLIC private functions ====*/
/**
 * @brief Populate impl_src array with 1's if source i is an active
 * source in this domain
 * 
 * @return size_t total number of implemented interrupts
 */
static size_t aplic_scan_impl_int(void)
{
    size_t count = APLIC_MAX_INTERRUPTS-1;
    for (size_t i = 0; i < APLIC_MAX_INTERRUPTS-1; i++) {
        aplic_domain.sourcecfg[i] = APLIC_SOURCECFG_SM_DEFAULT;
        impl_src[i] = IMPLEMENTED;
        if (aplic_domain.sourcecfg[i] == APLIC_SOURCECFG_SM_INACTIVE) {
            impl_src[i] = NOT_IMPLEMENTED;
            count -= 1;           
        }
        aplic_domain.sourcecfg[i] = APLIC_SOURCECFG_SM_INACTIVE;
    }
    return count;
}

/**==== APLIC public functions ====*/
/**==== Domain functions ====*/
void aplic_init(void)
{
    aplic_domain.domaincfg = 0;

    /** Clear all pending and enabled bits*/
    for (size_t i = 0; i < APLIC_NUM_CLRIx_REGS; i++) {
        aplic_domain.in_clrip[i] = 0;
        aplic_domain.clrie[i] = 0;
    }

    /** Sets the defaults configurations to all interrupts*/
    /**
     * TODO: Isto pode sair, porque a func scan_impl_intp já faz isso
    */
    // for (size_t i = 0; i < APLIC_NUM_SRCCFG_REGS; i++) {
    //     aplic_domain.sourcecfg[i] = APLIC_SOURCECFG_SM_INACTIVE;
    // }

    APLIC_IMPL_INTERRUPTS = aplic_scan_impl_int();

    /** Sets the default value of hart index and prio for implemented sources*/
    for (size_t i = 0; i < APLIC_NUM_TARGET_REGS; i++){
        if(impl_src[i] == IMPLEMENTED){
            aplic_domain.target[i] = APLIC_TARGET_PRIO_DEFAULT;
        }
    }

    aplic_domain.domaincfg |= APLIC_DOMAINCFG_IE;
}

void aplic_idc_init(void){
    uint32_t idc_index = cpu.id;

    idc[idc_index].ithreshold = APLIC_IDC_ITHRESHOLD_EN_ALL;  
    idc[idc_index].idelivery = APLIC_ENABLE_IDELIVERY;
    idc[idc_index].iforce = APLIC_DISABLE_IFORCE;
}

inline void aplic_set_domaincfg(uint32_t val)
{
    aplic_domain.domaincfg = val;
}

inline uint32_t aplic_get_domaincfg(void)
{
 return aplic_domain.domaincfg;
}

void aplic_set_sourcecfg(irqid_t int_id, uint32_t val)
{
    uint32_t real_int_id = int_id - 1;
    if(impl_src[real_int_id] == IMPLEMENTED){
        if(!(val & SRCCFG_D) && 
            ((val & (SRCCFG_SM)) != 2) && ((val & (SRCCFG_SM)) != 3)){
                aplic_domain.sourcecfg[real_int_id] = val & APLIC_SOURCECFG_SM_MASK;
        }
    }
}

uint32_t aplic_get_sourcecfg(irqid_t int_id)
{
    uint32_t ret = 0;
    uint32_t real_int_id = int_id - 1;
    if(impl_src[real_int_id] == IMPLEMENTED){
        ret = aplic_domain.sourcecfg[real_int_id];
    }
    return ret;
}

void aplic_set_pend_num(irqid_t int_id)
{
    if (impl_src[int_id] == IMPLEMENTED)
    {
        aplic_domain.setipnum = int_id;
    }
}

bool aplic_get_pend(irqid_t int_id)
{
    uint32_t reg_indx = int_id / 32;
    uint32_t intp_to_pend_mask = (1U << (int_id % 32));

    if (impl_src[int_id] == IMPLEMENTED)
    {
        return aplic_domain.setip[reg_indx] & intp_to_pend_mask;
    } else {
        return false;
    }
}

void aplic_set_clripnum(irqid_t int_id)
{
    if (impl_src[int_id] == IMPLEMENTED)
    {
        aplic_domain.clripnum = int_id;
    }
}

bool aplic_get_inclrip(irqid_t int_id)
{
    uint32_t reg_indx = int_id / 32;
    uint32_t intp_to_pend_mask = (1U << (int_id % 32));

    if (impl_src[int_id] == IMPLEMENTED)
    {
        return aplic_domain.in_clrip[reg_indx] & intp_to_pend_mask;
    } else {
        return false;
    }
}

void aplic_set_ienum(irqid_t int_id)
{
    if (impl_src[int_id] == IMPLEMENTED)
    {
        aplic_domain.setienum = int_id;
    }
}

bool aplic_get_ie(irqid_t int_id)
{
    uint32_t reg_indx = int_id / 32;
    uint32_t intp_to_pend_mask = (1U << (int_id % 32));

    if (impl_src[int_id] == IMPLEMENTED)
    {
        return aplic_domain.setie[reg_indx] & intp_to_pend_mask;
    } else {
        return false;
    }
}

void aplic_set_clrienum(irqid_t int_id)
{
    if (impl_src[int_id] == IMPLEMENTED)
    {
        aplic_domain.clrienum = int_id;
    }
}

void aplic_set_target(irqid_t int_id, uint32_t val)
{
    uint32_t real_int_id = int_id - 1;
    uint8_t priority = val & APLIC_TARGET_IPRIO_MASK;
    uint32_t hart_index = (val >> APLIC_TARGET_HART_IDX_SHIFT);
    //uint32_t guest_index = (aplic_domain.target[real_int_id] >> 12) & 0x3F;

    if(impl_src[real_int_id] == IMPLEMENTED){
        /** Evaluate in what delivery mode the domain is configured*/
        /** Direct Mode*/
        if(((aplic_domain.domaincfg & APLIC_DOMAINCFG_CTRL_MASK) & DOMAINCFG_DM) == 0){
            /** Checks priority and hart index range */
            if((priority > 0) && (priority <= APLIC_TARGET_IPRIO_MASK) && 
               (hart_index < APLIC_PLAT_IDC_NUM)){
                aplic_domain.target[real_int_id] = val;
            }
        }
        /** MSI Mode*/
        else{ 
            // if(guest_index >= 0 && guest_index <= APLIC_MAX_GEI){
            //     aplic_domain.target[real_int_id] = val;
            // }
        }
    }
}

uint32_t aplic_get_target(irqid_t int_id)
{
    uint32_t real_int_id = int_id - 1;
    uint32_t ret = 0;
    if(impl_src[real_int_id] == IMPLEMENTED){
        ret = aplic_domain.target[real_int_id];
    }
    return ret;
}

/**==== IDC functions ====*/
void aplic_idc_set_idelivery(idcid_t idc_id, bool en)
{
    if(idc_id < APLIC_PLAT_IDC_NUM) {
        if (en){
            idc[idc_id].idelivery = APLIC_ENABLE_IDELIVERY;
        }else{
            idc[idc_id].idelivery = APLIC_DISABLE_IDELIVERY;
        }
    }
}

bool aplic_idc_get_idelivery(idcid_t idc_id)
{
    if(idc_id < APLIC_PLAT_IDC_NUM) {
        return idc[idc_id].idelivery && APLIC_ENABLE_IDELIVERY;
    } else{
        return false;
    }
}

void aplic_idc_set_iforce(idcid_t idc_id, bool en)
{
    if(idc_id < APLIC_PLAT_IDC_NUM) {
        if (en){
            idc[idc_id].iforce = APLIC_ENABLE_IFORCE;
        }else{
            idc[idc_id].iforce = APLIC_DISABLE_IFORCE;
        }
    }
}

bool aplic_idc_get_iforce(idcid_t idc_id)
{
    if(idc_id < APLIC_PLAT_IDC_NUM) {
        return idc[idc_id].iforce && APLIC_ENABLE_IFORCE;
    } else{
        return false;
    }   
}

void aplic_idc_set_ithreshold(idcid_t idc_id, uint32_t new_th)
{
    if(idc_id < APLIC_PLAT_IDC_NUM) {
        if(new_th <= APLIC_TARGET_IPRIO_MASK){
            idc[idc_id].ithreshold = new_th;
        }
    }
}

uint32_t aplic_idc_get_ithreshold(idcid_t idc_id)
{
    uint32_t ret = 0;
    if(idc_id < APLIC_PLAT_IDC_NUM) {
        ret = idc[idc_id].ithreshold;
    }
    return ret;
}

uint32_t aplic_idc_get_topi(idcid_t idc_id)
{
    uint32_t ret = 0;
    if(idc_id < APLIC_PLAT_IDC_NUM) {
        ret = idc[idc_id].topi;
    }
    return ret;
}

uint32_t aplic_idc_get_claimi(idcid_t idc_id)
{
    uint32_t ret = 0;
    if(idc_id < APLIC_PLAT_IDC_NUM) {
        ret = idc[idc_id].claimi;
    }
    return ret;
}

void aplic_handle(void){
    uint32_t intp_identity;
    idcid_t idc_id = cpu.id;

    intp_identity = (idc[idc_id].claimi >> INTP_IDENTITY) & INTP_IDENTITY_MASK;
    if(intp_identity > 0){
        interrupts_handle(intp_identity);
    }
}