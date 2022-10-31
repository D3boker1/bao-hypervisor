/**
 * @file vaplic.c
 * @author Jose Martins <jose.martins@bao-project.org>
 * @author Francisco Marques (fmarques_00@protonmail.com)
 * @brief This module gives a set of function to virtualize the RISC-V APLIC.
 * @version 0.1
 * @date 2022-10-24
 * 
 * @copyright Copyright (c) Bao Project (www.bao-project.org), 2019-
 * 
 * Bao is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License version 2 as published by the Free
 * Software Foundation, with a special exception exempting guest code from such
 * license. See the COPYING file in the top-level directory for details.
 * 
 * TODO:    criar um array de funções. Sendo que o index segue a ordem
 *          pela qual os elementos aparecem na struct. A esse index
 *          soma acc->write, para escolher se é uma escrita ou leitura.
 *          Desta forma, deixo de precisar das func. vaplic_emul_x_access.
 *          Como paramentro da função, vai o index do intp_id/reg/idc_id
 * 
 * TODO: Implementar as funções set, a aplic_emul, ipi.
 * TODO: Avaliar se posso tirar da struct os reg XXXnum
 */
#include <vaplic.h>
#include <vm.h>
#include <cpu.h>
#include <emul.h>
#include <mem.h>
#include <interrupts.h>
#include <arch/csrs.h>
#define APLIC_MAX_PRIO 6

static inline unsigned get_bit_from_reg(uint32_t reg, size_t bit){
    return (reg & (1 << bit)) ? 1U : 0U;
}

static void set_bit_from_reg(uint32_t* reg, size_t bit){
    *reg |=  (1 << bit);
}

static void clr_bit_from_reg(uint32_t* reg, size_t bit){
    *reg &=  ~(1 << bit);
}

/**
 * @brief Converts a virtual cpu id into the physical one
 * 
 * @param vcpu Virtual cpu to convert
 * @return int The physical cpu id; or INVALID_CPUID in case of error.
 */
static int vaplic_vcpuid_to_pcpuid(struct vcpu *vcpu){
    return vm_translate_to_pcpuid(vcpu->vm, vcpu->id);
}

static uint32_t vaplic_get_domaincfg(struct vcpu *vcpu);
static uint32_t vaplic_get_target(struct vcpu *vcpu, irqid_t intp_id); 
static uint32_t vaplic_get_idelivery(struct vcpu *vcpu, uint16_t idc_id);
static uint32_t vaplic_get_iforce(struct vcpu *vcpu, uint16_t idc_id);
static uint32_t vaplic_get_ithreshold(struct vcpu *vcpu, uint16_t idc_id);

void vaplic_set_hw(struct vm *vm, irqid_t intp_id)
{
    if (intp_id <= APLIC_MAX_INTERRUPTS) {
        bitmap_set(vm->arch.vxplic.hw,intp_id);
    }
}

/**
 * @brief Returns if a given interrupt is associated to the physical source
 * 
 * @param vcpu virtual cpu running
 * @param intp_id interrupt to evaluate
 * @return true if is a physical intp
 * @return false if is NOT a physical intp
 */
static bool vaplic_get_hw(struct vcpu* vcpu, irqid_t intp_id)
{
    bool ret = false;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (intp_id <= APLIC_MAX_INTERRUPTS) ret = bitmap_get(vxplic->hw, intp_id);
    return ret;
}

static bool vaplic_get_pend(struct vcpu *vcpu, irqid_t intp_id){
    uint32_t ret = 0;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (intp_id < APLIC_MAX_INTERRUPTS){
        ret = get_bit_from_reg(vxplic->setip[intp_id/32], intp_id);
    }
    return ret;
}

static bool vaplic_get_enbl(struct vcpu *vcpu, irqid_t intp_id){
    uint32_t ret = 0;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (intp_id < APLIC_MAX_INTERRUPTS){
        ret = get_bit_from_reg(vxplic->setie[intp_id/32], intp_id);
    }
    return ret;
}

// static uint32_t vaplic_emul_gateway(struct vcpu* vcpu, irqid_t intp_id){
//     // Só tem de verificar se a interrupção pode ou não ficar pend
//     // Level?
//     struct virqc * vxplic = &vcpu->vm->arch.vxplic;
//     if(vxplic->srccfg[intp_id] != APLIC_SOURCECFG_SM_INACTIVE &&
//        vxplic->srccfg[intp_id] != APLIC_SOURCECFG_SM_DETACH){
//         set_bit_from_reg(&vxplic->setip[intp_id/32], intp_id);
//     }
// }

static irqid_t vaplic_emul_notifier(struct vcpu* vcpu){
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    uint32_t max_prio = APLIC_MAX_PRIO;
    irqid_t int_id = 0;
    uint32_t hart_index = 0;
    uint32_t prio = 0;

    for (size_t i = 1; i < APLIC_MAX_INTERRUPTS; i++) {
        if (vaplic_get_pend(vcpu, i) && vaplic_get_enbl(vcpu, i)) {
            uint32_t target = vaplic_get_target(vcpu, i); 
            prio = target & 0xFF; 
            
            if (prio < max_prio) {
                max_prio = prio;
                int_id = i;
                hart_index = (target >> 18) & 0xFFFC;
            }
        }
    }

    uint32_t domaincgfIE = (vaplic_get_domaincfg(vcpu) >> 8) & 0x1;
    uint32_t threshold = vaplic_get_ithreshold(vcpu, hart_index);
    uint32_t delivery = vaplic_get_idelivery(vcpu, hart_index);
    uint32_t force =  vaplic_get_iforce(vcpu, hart_index);
    if ((max_prio < threshold || threshold == 0 || force == 1) && 
         delivery == 1 && domaincgfIE == 1){
        return int_id;
    }
    else{
        return 0;
    }
}

enum {UPDATE_HART_LINE};
static void vaplic_ipi_handler(uint32_t event, uint64_t data);
CPU_MSG_HANDLER(vaplic_ipi_handler, VPLIC_IPI_ID);

static void vaplic_update_hart_line(struct vcpu* vcpu) 
{
    int pcpu_id = vaplic_vcpuid_to_pcpuid(vcpu);
    /** If the current cpu is the targeting cpu, signal the intp to the hart*/
    /** Else, send a mensage to the targeting cpu */
    if(pcpu_id == cpu.id) {
        int id = vaplic_emul_notifier(vcpu);
        if(id != 0){
            CSRS(CSR_HVIP, HIP_VSEIP);
        } else  {
            CSRC(CSR_HVIP, HIP_VSEIP);
        }
    } else {
        struct cpu_msg msg = {VPLIC_IPI_ID, UPDATE_HART_LINE};
        cpu_send_msg(pcpu_id, &msg);       
    }
}

static void vaplic_ipi_handler(uint32_t event, uint64_t data) 
{
    switch(event) {
        case UPDATE_HART_LINE:
            vaplic_update_hart_line(cpu.vcpu);
            break;
    }
}


// ============================================================================
static void vaplic_set_domaincfg(struct vcpu *vcpu, uint32_t new_val){
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    /** Update only the virtual domaincfg */
    /** Only Interrupt Enable is configurable. */
    new_val = (new_val & (0x1 << 8));
    vxplic->domaincfg = new_val | (0x80 << 24);
    spin_unlock(&vxplic->lock);

    vaplic_update_hart_line(vcpu);
}

static uint32_t vaplic_get_domaincfg(struct vcpu *vcpu){
    uint32_t ret = 0;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    ret = vxplic->domaincfg;
    return ret;
}

static uint32_t vaplic_get_srccfg(struct vcpu *vcpu, irqid_t intp_id){
    uint32_t real_int_id = intp_id - 1;
    uint32_t ret = 0;

    if(intp_id == 0)
        return ret;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (real_int_id < APLIC_MAX_INTERRUPTS) ret = vxplic->srccfg[real_int_id];
    return ret;
}

static void vaplic_set_srccfg(struct vcpu *vcpu, irqid_t intp_id, uint32_t new_val){
    struct virqc *vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    /** If intp is valid and new source config is different from prev. one.*/
    if (intp_id > 0 && intp_id < APLIC_MAX_INTERRUPTS && 
        vaplic_get_srccfg(vcpu, intp_id) != new_val) {
        /** Update virt sourcecfg array */        
        new_val = (new_val & (0x1 << 10)) ? 0 : new_val & 0x7;
        if(new_val == 2 || new_val == 3)
            new_val = 0;
        /** Is this intp a phys. intp? */
        if(vaplic_get_hw(vcpu, intp_id)){
            /** Update in phys. aplic */
            aplic_set_sourcecfg(intp_id, new_val);
            if((impl_src[intp_id] == IMPLEMENTED) &&
                aplic_get_sourcecfg(intp_id) == new_val){
                printk("BAO: srccfg[%d]= %d\r\n", intp_id, new_val);
                vxplic->srccfg[intp_id-1] = new_val;
            }
        } else {
            /** If intp is not phys. emul aplic behaviour */
            vxplic->srccfg[intp_id-1] = new_val;
            vaplic_update_hart_line(vcpu);
        }
    }
    spin_unlock(&vxplic->lock);
}

static uint32_t vaplic_get_setip(struct vcpu *vcpu, uint8_t reg){
    uint32_t ret = 0;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (reg < (APLIC_MAX_INTERRUPTS/32)) ret = vxplic->setip[reg];
    return ret;
}

static void vaplic_set_setip(struct vcpu *vcpu, uint8_t reg, uint32_t new_val){
    struct virqc *vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    if (reg < APLIC_MAX_INTERRUPTS/32 && 
        vaplic_get_setip(vcpu, reg) != new_val) {
        /** Update virt setip array */
        if (reg == 0) new_val &= 0xFFFFFFFE;
        vxplic->setip[reg] = new_val;
        for(int i = 0; i < APLIC_MAX_INTERRUPTS/32; i++){
            /** Is this intp a phys. intp? */
            if(vaplic_get_hw(vcpu,i)){
                /** Update in phys. aplic */
                if(get_bit_from_reg(vxplic->setip[reg], i) && ((new_val >> i) & 1)){
                    aplic_set_pend_num(i);
                }
            } else {
                /** If intp is not phys. emul aplic behaviour */
                vaplic_update_hart_line(vcpu);
            }
        }
    }
    spin_unlock(&vxplic->lock);
}

static void vaplic_set_setipnum(struct vcpu *vcpu, uint32_t new_val){
    struct virqc *vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    if (new_val != 0 && new_val < APLIC_MAX_INTERRUPTS && 
        !get_bit_from_reg(vxplic->setip[new_val/32], new_val)) {
        //vxplic->setipnum = new_val;
        set_bit_from_reg(&vxplic->setip[new_val/32], new_val%32);
        if(vaplic_get_hw(vcpu,new_val)){
            aplic_set_pend_num(new_val);
        } else {
            vaplic_update_hart_line(vcpu);
        }
    }
    spin_unlock(&vxplic->lock);
}

static void vaplic_set_in_clrip(struct vcpu *vcpu, uint8_t reg, uint32_t new_val){
    struct virqc *vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    if (reg < APLIC_MAX_INTERRUPTS/32 && 
        vaplic_get_setip(vcpu, reg) != new_val) {
        if (reg == 0) new_val &= 0xFFFFFFFE;
        vxplic->setip[reg] &= ~new_val;
        for(int i = 0; i < APLIC_MAX_INTERRUPTS/32; i++){
            if(vaplic_get_hw(vcpu,i)){
                if(!get_bit_from_reg(vxplic->setip[reg], i) && ((new_val >> i) & 1)){
                    aplic_set_clripnum(i);
                }
            } else {
                vaplic_update_hart_line(vcpu);
            }
        }
    }
    spin_unlock(&vxplic->lock);
}

static uint32_t vaplic_get_in_clrip(struct vcpu *vcpu, uint8_t reg){
    uint32_t ret = 0;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (reg < (APLIC_MAX_INTERRUPTS/32)) ret = vxplic->in_clrip[reg];
    return ret;
}

static void vaplic_set_clripnum(struct vcpu *vcpu, uint32_t new_val){
    struct virqc *vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    if (new_val != 0 && new_val < APLIC_MAX_INTERRUPTS && 
        get_bit_from_reg(vxplic->setip[new_val/32], new_val)) {
        //vxplic->clripnum = new_val;
        clr_bit_from_reg(&vxplic->setip[new_val/32], new_val%32);
        if(vaplic_get_hw(vcpu,new_val)){
            aplic_set_clripnum(new_val);
        } else {
            vaplic_update_hart_line(vcpu);
        }
    }
    spin_unlock(&vxplic->lock);
}

static uint32_t vaplic_get_setie(struct vcpu *vcpu, uint32_t reg){
    uint32_t ret = 0;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (reg < (APLIC_MAX_INTERRUPTS/32)) ret = vxplic->setie[reg];
    return ret;
}

static void vaplic_set_setie(struct vcpu *vcpu, uint8_t reg, uint32_t new_val){
    struct virqc *vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    if (reg < APLIC_MAX_INTERRUPTS/32 && 
        vaplic_get_setie(vcpu, reg) != new_val) {
        /** Update virt setip array */
        if (reg == 0) new_val &= 0xFFFFFFFE;
        vxplic->setie[reg] = new_val;
        for(int i = 0; i < APLIC_MAX_INTERRUPTS/32; i++){
            /** Is this intp a phys. intp? */
            if(vaplic_get_hw(vcpu,i)){
                /** Update in phys. aplic */
                if(get_bit_from_reg(vxplic->setie[reg], i) && ((new_val >> i) & 1)){
                    aplic_set_ienum(i);
                }
            } else {
                /** If intp is not phys. emul aplic behaviour */
                vaplic_update_hart_line(vcpu);
            }
        }
    }
    spin_unlock(&vxplic->lock);
}

static void vaplic_set_setienum(struct vcpu *vcpu, uint32_t new_val){
    struct virqc *vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    if (new_val != 0 && new_val < APLIC_MAX_INTERRUPTS && 
        !get_bit_from_reg(vxplic->setie[new_val/32], new_val)) {
        //vxplic->setienum = new_val;
        set_bit_from_reg(&vxplic->setie[new_val/32], new_val%32);
        if(vaplic_get_hw(vcpu,new_val)){
            aplic_set_ienum(new_val);
        } else {
            vaplic_update_hart_line(vcpu);
        }
    }
    spin_unlock(&vxplic->lock);
}

static void vaplic_set_clrie(struct vcpu *vcpu, uint8_t reg, uint32_t new_val){
    struct virqc *vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    if (reg < APLIC_MAX_INTERRUPTS/32 && 
        vaplic_get_setie(vcpu, reg) != new_val) {
        if (reg == 0) new_val &= 0xFFFFFFFE;
        vxplic->setie[reg] &= ~new_val;
        for(int i = 0; i < APLIC_MAX_INTERRUPTS/32; i++){
            if(vaplic_get_hw(vcpu,i)){
                if(!get_bit_from_reg(vxplic->setie[reg], i) && ((new_val >> i) & 1)){
                    aplic_set_clrienum(i);
                }
            } else {
                vaplic_update_hart_line(vcpu);
            }
        }
    }
    spin_unlock(&vxplic->lock);
}

static void vaplic_set_clrienum(struct vcpu *vcpu, uint32_t new_val){
    struct virqc *vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    if (new_val != 0 && new_val < APLIC_MAX_INTERRUPTS && 
        get_bit_from_reg(vxplic->setie[new_val/32], new_val)) {
        //vxplic->clrienum = new_val;
        clr_bit_from_reg(&vxplic->setie[new_val/32], new_val%32);
        if(vaplic_get_hw(vcpu,new_val)){
            aplic_set_clrienum(new_val);
        } else {
            vaplic_update_hart_line(vcpu);
        }
    }
    spin_unlock(&vxplic->lock);
}

static void vaplic_set_target(struct vcpu *vcpu, irqid_t intp_id, uint32_t new_val){
    struct virqc *vxplic = &vcpu->vm->arch.vxplic;

    spin_lock(&vxplic->lock);
    if (intp_id > 0  && intp_id < APLIC_MAX_INTERRUPTS && 
        vaplic_get_target(vcpu, intp_id) != new_val) {
        if ((new_val & 0xFF) == 0){
            new_val &= ~0xFF;
            new_val |= 0x1;
        }
        new_val = (new_val & 0xFFFC00FF);
        if(vaplic_get_hw(vcpu,intp_id)){
            aplic_set_target(intp_id, new_val);
            if(impl_src[intp_id] == IMPLEMENTED)
                vxplic->target[intp_id-1] = new_val;
        } else {
            vaplic_update_hart_line(vcpu);
            vxplic->target[intp_id-1] = new_val;
        }
    }
    spin_unlock(&vxplic->lock);
}

static uint32_t vaplic_get_target(struct vcpu *vcpu, irqid_t intp_id){
    uint32_t ret = 0;

    if(intp_id == 0)
        return ret;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (intp_id < APLIC_MAX_INTERRUPTS) ret = vxplic->target[intp_id -1];
    return ret;
}

static void vaplic_set_idelivery(struct vcpu *vcpu, uint16_t idc_id, uint32_t new_val){
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    new_val = (new_val & 0x1);
    if (idc_id < vxplic->idc_num){
        if (new_val) 
            bitmap_set(vxplic->idelivery, idc_id);
        else
            bitmap_clear(vxplic->idelivery, idc_id);
    }
    spin_unlock(&vxplic->lock);

    vaplic_update_hart_line(vcpu);
}

static uint32_t vaplic_get_idelivery(struct vcpu *vcpu, uint16_t idc_id){
    uint32_t ret = 0;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (idc_id < vxplic->idc_num) ret = bitmap_get( vxplic->idelivery, idc_id);
    return ret;
}

static void vaplic_set_iforce(struct vcpu *vcpu, uint16_t idc_id, uint32_t new_val){
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    new_val = (new_val & 0x1);
    if (idc_id < vxplic->idc_num){
        if (new_val) 
            bitmap_set(vxplic->iforce, idc_id);
        else
            bitmap_clear(vxplic->iforce, idc_id);
    }
    spin_unlock(&vxplic->lock);

    vaplic_update_hart_line(vcpu);
}

static uint32_t vaplic_get_iforce(struct vcpu *vcpu, uint16_t idc_id){
    uint32_t ret = 0;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (idc_id < vxplic->idc_num) ret = bitmap_get(vxplic->iforce, idc_id);
    return ret;
}

static void vaplic_set_ithreshold(struct vcpu *vcpu, uint16_t idc_id, uint32_t new_val){
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    if (idc_id < vxplic->idc_num){
        vxplic->ithreshold[idc_id] = new_val;
    }
    spin_unlock(&vxplic->lock);

    vaplic_update_hart_line(vcpu);
}

static uint32_t vaplic_get_ithreshold(struct vcpu *vcpu, uint16_t idc_id){
    uint32_t ret = 0;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (idc_id < vxplic->idc_num) ret = vxplic->ithreshold[idc_id];
    return ret;
}

static uint32_t vaplic_get_topi(struct vcpu *vcpu, uint16_t idc_id){
    uint32_t ret = 0;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (idc_id < vxplic->idc_num) ret = vxplic->topi_claimi[idc_id];
    return ret;
}

static uint32_t vaplic_get_claimi(struct vcpu *vcpu, uint16_t idc_id){
    uint32_t ret = 0;
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    if (idc_id < vxplic->idc_num){
        ret = vxplic->topi_claimi[idc_id];
        if (ret == 0){
            // Clear the virt iforce bit
            vxplic->iforce[idc_id] = 0;
            if(vaplic_get_hw(vcpu,(ret >> 16))){
                // Clear the physical iforce bit
                aplic_idc_set_iforce(idc_id, 0);
            }
        }
        // Clear the virt pending bit for te read intp
        set_bit_from_reg(&vxplic->setip[(ret >> 16)/32], (ret >> 16)%32);
        if(vaplic_get_hw(vcpu,(ret >> 16))){
            // Clear the physical pending bit for te read intp
            aplic_set_pend_num((ret >> 16)%32);
        }
        vaplic_update_hart_line(vcpu);
    }
    return ret;
}
// ============================================================================


// ====================================================
/**
 * @brief register access emulation functions
 * 
 * @param acc access information
 * 
 * It determines whether it needs to call the write or read funcion
 * for the register.
 */

static void vaplic_emul_domaincfg_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_domaincfg(cpu.vcpu, vcpu_readreg(cpu.vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu.vcpu, acc->reg, vaplic_get_domaincfg(cpu.vcpu));
    }
}

static void vaplic_emul_srccfg_access(struct emul_access *acc){
    int intp = (acc->addr & 0xFFF)/4;
    if (acc->write) {
        vaplic_set_srccfg(cpu.vcpu, intp, vcpu_readreg(cpu.vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu.vcpu, acc->reg, vaplic_get_srccfg(cpu.vcpu, intp));
    }
}

static void vaplic_emul_setip_access(struct emul_access *acc){
    int reg = (acc->addr & 0xFF)/32;
    if (acc->write) {
        vaplic_set_setip(cpu.vcpu, reg, vcpu_readreg(cpu.vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu.vcpu, acc->reg, vaplic_get_setip(cpu.vcpu, reg));
    }
}

static void vaplic_emul_setipnum_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_setipnum(cpu.vcpu, vcpu_readreg(cpu.vcpu, acc->reg));
    }
}

static void vaplic_emul_in_clrip_access(struct emul_access *acc){
    int reg = (acc->addr & 0xFF)/32;
    if (acc->write) {
        vaplic_set_in_clrip(cpu.vcpu, reg, vcpu_readreg(cpu.vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu.vcpu, acc->reg, vaplic_get_in_clrip(cpu.vcpu, reg));
    }
}

static void vaplic_emul_clripnum_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_clripnum(cpu.vcpu, vcpu_readreg(cpu.vcpu, acc->reg));
    }
}

static void vaplic_emul_setie_access(struct emul_access *acc){
    int reg = (acc->addr & 0xFF)/32;
    if (acc->write) {
        vaplic_set_setie(cpu.vcpu, reg, vcpu_readreg(cpu.vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu.vcpu, acc->reg, vaplic_get_setie(cpu.vcpu, reg));
    }
}

static void vaplic_emul_setienum_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_setienum(cpu.vcpu, vcpu_readreg(cpu.vcpu, acc->reg));
    }
}

static void vaplic_emul_clrie_access(struct emul_access *acc){
    int reg = (acc->addr & 0xFF)/32;
    if (acc->write) {
        vaplic_set_clrie(cpu.vcpu, reg, vcpu_readreg(cpu.vcpu, acc->reg));
    }
}

static void vaplic_emul_clrienum_access(struct emul_access *acc){
    if (acc->write) {
        vaplic_set_clrienum(cpu.vcpu, vcpu_readreg(cpu.vcpu, acc->reg));
    }
}

static void vaplic_emul_target_access(struct emul_access *acc){
    int intp = (acc->addr & 0xFFF)/4;
    if (acc->write) {
        vaplic_set_target(cpu.vcpu, intp, vcpu_readreg(cpu.vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu.vcpu, acc->reg, vaplic_get_target(cpu.vcpu, intp));
    }
}

static void vaplic_emul_idelivery_access(struct emul_access *acc){
    int idc_id = ((acc->addr - APLIC_IDC_OFF) >> 5) & 0x3ff;
    if (acc->write) {
        vaplic_set_idelivery(cpu.vcpu, idc_id, vcpu_readreg(cpu.vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu.vcpu, acc->reg, vaplic_get_idelivery(cpu.vcpu, idc_id));
    }
}

static void vaplic_emul_iforce_access(struct emul_access *acc){
    int idc_id = ((acc->addr - APLIC_IDC_OFF) >> 5) & 0x3ff;
    if (acc->write) {
        vaplic_set_iforce(cpu.vcpu, idc_id, vcpu_readreg(cpu.vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu.vcpu, acc->reg, vaplic_get_iforce(cpu.vcpu, idc_id));
    }
}

static void vaplic_emul_ithreshold_access(struct emul_access *acc){
    int idc_id = ((acc->addr - APLIC_IDC_OFF) >> 5) & 0x3ff;
    if (acc->write) {
        vaplic_set_ithreshold(cpu.vcpu, idc_id, vcpu_readreg(cpu.vcpu, acc->reg));
    } else {
        vcpu_writereg(cpu.vcpu, acc->reg, vaplic_get_ithreshold(cpu.vcpu, idc_id));
    }
}

static void vaplic_emul_topi_access(struct emul_access *acc){
    int idc_id = ((acc->addr - APLIC_IDC_OFF) >> 5) & 0x3ff;

    if (acc->write) return;
    vcpu_writereg(cpu.vcpu, acc->reg, vaplic_get_topi(cpu.vcpu, idc_id));
}

static void vaplic_emul_claimi_access(struct emul_access *acc){
    int idc_id = ((acc->addr - APLIC_IDC_OFF) >> 5) & 0x3ff;

    if (acc->write) return;
    vcpu_writereg(cpu.vcpu, acc->reg, vaplic_get_claimi(cpu.vcpu, idc_id));
}

// ====================================================
void vaplic_inject(struct vcpu *vcpu, irqid_t intp_id)
{
    struct virqc * vxplic = &vcpu->vm->arch.vxplic;
    spin_lock(&vxplic->lock);
    
    /** Intp has a valid ID and the virtual interrupt is not pending*/
    if (intp_id > 0 && intp_id < APLIC_MAX_INTERRUPTS && !vaplic_get_pend(vcpu, intp_id)){
        //vaplic_emul_gateway(vcpu, intp_id);
        if(vxplic->srccfg[intp_id] != APLIC_SOURCECFG_SM_INACTIVE &&
           vxplic->srccfg[intp_id] != APLIC_SOURCECFG_SM_DETACH){
            set_bit_from_reg(&vxplic->setip[intp_id/32], intp_id);
        }
        vaplic_update_hart_line(vcpu);
    }
    spin_unlock(&vxplic->lock);
}

/**
 * @brief Function to handle writes (or reads) to (from) domain structure.
 * 
 * @param acc emulated access
 * @return true if conclude without errors.
 * @return false if the access is not aligned.
 */
static bool vaplic_domain_emul_handler(struct emul_access *acc)
{
    // only allow aligned word accesses
    if (acc->width != 4 || acc->addr & 0x3) return false;

    switch (acc->addr & 0xffff) {
        case APLIC_DOMAIN_OFF:
            vaplic_emul_domaincfg_access(acc);
            break;
        case APLIC_SOURCECFG_OFF ... (APLIC_SOURCECFG_OFF-0x4)+(1023*4):
            vaplic_emul_srccfg_access(acc);
            break;
        // case APLIC_MMSIADDRCFG_OFF:
        //     vaplic_emul_mmsiaddrcfg_access(acc);
        //     break;
        // case APLIC_MMSIADDRCFGH_OFF:
        //     vaplic_emul_mmsiaddrcfgh_access(acc);
        //     break;
        // case APLIC_SMSIADDRCFG_OFF:
        //     vaplic_emul_smsiaddrcfg_access(acc);
        //     break;
        // case APLIC_SMSIADDRCFGH_OFF:
        //     vaplic_emul_smsiaddrcfgh_access(acc);
        //     break;
        case APLIC_SETIP_OFF ... (APLIC_SETIP_OFF - 0x04)+(31*4):
            vaplic_emul_setip_access(acc);
            break;
        case APLIC_SETIPNUM_OFF:
            vaplic_emul_setipnum_access(acc);
            break;
        case APLIC_IN_CLRIP_OFF ... (APLIC_IN_CLRIP_OFF - 0x04)+(31*4):
            vaplic_emul_in_clrip_access(acc);
            break;
        case APLIC_CLRIPNUM_OFF:
            vaplic_emul_clripnum_access(acc);
            break;
        case APLIC_SETIE_OFF ... (APLIC_SETIE_OFF - 0x04)+(31*4):
            vaplic_emul_setie_access(acc);
            break;
        case APLIC_SETIENUM_OFF:
            vaplic_emul_setienum_access(acc);
            break;
        case APLIC_CLRIE_OFF ... (APLIC_CLRIE_OFF - 0x04)+(31*4):
            vaplic_emul_clrie_access(acc);
            break;
        case APLIC_CLRIENUM_OFF:
            vaplic_emul_clrienum_access(acc);
            break;
        // case APLIC_SETIPNUM_LE_OFF:
        //     vaplic_emul_setipnum_le_access(acc);
        //     break;
        // case APLIC_SETIPNUM_BE_OFF:
        //     vaplic_emul_setipnum_be_access(acc);
        //     break;
        // case APLIC_GENMSI_OFF:
        //     vaplic_emul_genmsi_access(acc);
        //     break;
        case APLIC_TARGET_OFF ... (APLIC_TARGET_OFF-0x4)+(1023*4):
            vaplic_emul_target_access(acc);
            break;
        default:
            if(!acc->write) {
                vcpu_writereg(cpu.vcpu, acc->reg, 0);
            }
            break;
    }

    return true;
}

/**
 * @brief Function to handle writes (or reads) to (from) IDC structure.
 * 
 * @param acc emulated access
 * @return true  if conclude without errors.
 * @return false if the access is not aligned.
 */
static bool vaplic_idc_emul_handler(struct emul_access *acc)
{
    // only allow aligned word accesses
    if (acc->width > 4 || acc->addr & 0x3) return false;

    int idc_id = ((acc->addr - APLIC_IDC_OFF) >> 5) & 0x3ff;
    if(!(idc_id < cpu.vcpu->vm->arch.vxplic.idc_num)){
        if(!acc->write) {
            vcpu_writereg(cpu.vcpu, acc->reg, 0);
        }
        return true;
    }
    uint32_t addr = acc->addr - APLIC_IDC_BASE;
    addr = addr - (0x20 * idc_id);
    switch (addr & 0x1F) {
        case APLIC_IDC_IDELIVERY_OFF:
            vaplic_emul_idelivery_access(acc);
            break;
        case APLIC_IDC_IFORCE_OFF:
            vaplic_emul_iforce_access(acc);
            break;
        case APLIC_IDC_ITHRESHOLD_OFF:
            vaplic_emul_ithreshold_access(acc);
            break;
        case APLIC_IDC_TOPI_OFF:
            vaplic_emul_topi_access(acc);
            break;
        case APLIC_IDC_CLAIMI_OFF:
            vaplic_emul_claimi_access(acc);
            break;
        default:
            if(!acc->write) {
                vcpu_writereg(cpu.vcpu, acc->reg, 0);
            }
            break;
    }

    return true;
}

void vaplic_init(struct vm *vm, vaddr_t vaplic_base)
{
    if (cpu.id == vm->master) {
        struct emul_mem aplic_domain_emu = {.va_base = vaplic_base,
                                         .size = sizeof(aplic_domain),
                                         .handler = vaplic_domain_emul_handler};
        vm_emul_add_mem(vm, &aplic_domain_emu);

        struct emul_mem aplic_idc_emu = {
            .va_base = vaplic_base + APLIC_IDC_OFF,
            .size = sizeof(idc),
            .handler = vaplic_idc_emul_handler};
        vm_emul_add_mem(vm, &aplic_idc_emu);

        /* 1 IDC per hart */
        vm->arch.vxplic.idc_num = vm->cpu_num;
    }
}