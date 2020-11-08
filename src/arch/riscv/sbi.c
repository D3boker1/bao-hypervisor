/**
 * Bao Hypervisor
 *
 * Copyright (c) Bao Project (www.bao-project.org), 2019-
 *
 * Authors:
 *      Jose Martins <jose.martins@bao-project.org>
 *
 * Bao is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License version 2 as published by the Free
 * Software Foundation, with a special exception exempting guest code from such
 * license. See the COPYING file in the top-level directory for details.
 *
 */

#include <arch/sbi.h>
#include <arch/csrs.h>
#include <cpu.h>
#include <vm.h>
#include <bitmap.h>
#include <fences.h>

#define SBI_LGCY_EXTID_SETTIMER (0x0)
#define SBI_LGCY_EXTID_PUTCHAR (0x1)
#define SBI_LGCY_EXTID_GETCHAR (0x2)
#define SBI_LGCY_EXTID_CLEARIPI (0x3)
#define SBI_LGCY_EXTID_SENDIPI (0x4)
#define SBI_LGCY_EXTID_REMFENCEI (0x5)
#define SBI_LGCY_EXTID_REMSFENCEVMA (0x6)
#define SBI_LGCY_EXTID_REMSFENCEASID (0x7)
#define SBI_LGCY_EXTID_SHUTDOWN (0x8)

#define SBI_EXTID_BASE (0x10)
#define SBI_GET_SBI_SPEC_VERSION_FID (0)
#define SBI_GET_SBI_IMPL_ID_FID (1)
#define SBI_GET_SBI_IMPL_VERSION_FID (2)
#define SBI_PROBE_EXTENSION_FID (3)
#define SBI_GET_MVENDORID_FID (4)
#define SBI_GET_MARCHID_FID (5)
#define SBI_GET_MIMPID_FID (6)

#define SBI_EXTID_TIME (0x54494D45)
#define SBI_SET_TIMER_FID (0x0)

#define SBI_EXTID_IPI (0x735049)
#define SBI_SEND_IPI_FID (0x0)

#define SBI_EXTID_HSM (0x48534D)
#define SBI_HART_START_FID  (0)
#define SBI_HART_STOP_FID   (1)
#define SBI_HART_STATUS_FID   (2)

#define SBI_EXTID_RFNC (0x52464E43)
#define SBI_REMOTE_FENCE_I_FID (0)
#define SBI_REMOTE_SFENCE_VMA_FID (1)
#define SBI_REMOTE_SFENCE_VMA_ASID_FID (2)
#define SBI_REMOTE_HFENCE_GVMA_FID (3)
#define SBI_REMOTE_HFENCE_GVMA_VMID_FID (4)
#define SBI_REMOTE_HFENCE_VVMA_FID (5)
#define SBI_REMOTE_HFENCE_VVMA_ASID_FID (6)

static inline struct sbiret sbi_ecall(long eid, long fid, long a0, long a1,
                                      long a2, long a3, long a4, long a5)
{
    long register _a0 asm("a0") = a0;
    long register _a1 asm("a1") = a1;
    long register _a2 asm("a2") = a2;
    long register _a3 asm("a3") = a3;
    long register _a4 asm("a4") = a4;
    long register _a5 asm("a5") = a5;
    long register _a6 asm("a6") = fid;
    long register _a7 asm("a7") = eid;

    asm volatile("ecall"
                 : "+r"(_a0), "+r"(_a1)
                 : "r"(_a2), "r"(_a3), "r"(_a4), "r"(_a5), "r"(_a6), "r"(_a7)
                 : "memory");

    struct sbiret ret = {.error = _a0, .value = _a1};

    return ret;
}

void sbi_console_putchar(int ch)
{
    (void)sbi_ecall(0x1, 0, ch, 0, 0, 0, 0, 0);
}

struct sbiret sbi_get_spec_version(void)
{
    return sbi_ecall(SBI_EXTID_BASE, SBI_GET_SBI_SPEC_VERSION_FID, 0, 0, 0, 0,
                     0, 0);
}
struct sbiret sbi_get_impl_id(void)
{
    return sbi_ecall(SBI_EXTID_BASE, SBI_GET_SBI_IMPL_ID_FID, 0, 0, 0, 0, 0, 0);
}
struct sbiret sbi_get_impl_version(void)
{
    return sbi_ecall(SBI_EXTID_BASE, SBI_GET_SBI_IMPL_VERSION_FID, 0, 0, 0, 0,
                     0, 0);
}
struct sbiret sbi_probe_extension(long extension_id)
{
    return sbi_ecall(SBI_EXTID_BASE, SBI_PROBE_EXTENSION_FID, extension_id, 0,
                     0, 0, 0, 0);
}
struct sbiret sbi_get_mvendorid(void)
{
    return sbi_ecall(SBI_EXTID_BASE, SBI_GET_MVENDORID_FID, 0, 0, 0, 0, 0, 0);
}
struct sbiret sbi_get_marchid(void)
{
    return sbi_ecall(SBI_EXTID_BASE, SBI_GET_MARCHID_FID, 0, 0, 0, 0, 0, 0);
}
struct sbiret sbi_get_mimpid(void)
{
    return sbi_ecall(SBI_EXTID_BASE, SBI_GET_MIMPID_FID, 0, 0, 0, 0, 0, 0);
}

struct sbiret sbi_send_ipi(const unsigned long hart_mask,
                           unsigned long hart_mask_base)
{
    return sbi_ecall(SBI_EXTID_IPI, SBI_SEND_IPI_FID, hart_mask, hart_mask_base,
                     0, 0, 0, 0);
}

struct sbiret sbi_set_timer(uint64_t stime_value)
{
    return sbi_ecall(SBI_EXTID_TIME, SBI_SET_TIMER_FID, stime_value, 0, 0, 0, 0,
                     0);
}

struct sbiret sbi_remote_fence_i(const unsigned long hart_mask,
                                 unsigned long hart_mask_base)
{
    return sbi_ecall(SBI_EXTID_RFNC, SBI_REMOTE_FENCE_I_FID, hart_mask,
                     hart_mask_base, 0, 0, 0, 0);
}

struct sbiret sbi_remote_sfence_vma(const unsigned long hart_mask,
                                    unsigned long hart_mask_base,
                                    unsigned long start_addr,
                                    unsigned long size)
{
    return sbi_ecall(SBI_EXTID_RFNC, SBI_REMOTE_SFENCE_VMA_FID, hart_mask,
                     hart_mask_base, start_addr, size, 0, 0);
}

struct sbiret sbi_remote_hfence_gvma(const unsigned long hart_mask,
                                     unsigned long hart_mask_base,
                                     unsigned long start_addr,
                                     unsigned long size)
{
    return sbi_ecall(SBI_EXTID_RFNC, SBI_REMOTE_HFENCE_GVMA_FID, hart_mask,
                     hart_mask_base, start_addr, size, 0, 0);
}

struct sbiret sbi_remote_hfence_gvma_vmid(const unsigned long hart_mask,
                                          unsigned long hart_mask_base,
                                          unsigned long start_addr,
                                          unsigned long size,
                                          unsigned long vmid)
{
    return sbi_ecall(SBI_EXTID_RFNC, SBI_REMOTE_HFENCE_GVMA_VMID_FID, hart_mask,
                     hart_mask_base, start_addr, size, vmid, 0);
}

struct sbiret sbi_remote_hfence_vvma_asid(const unsigned long hart_mask,
                                          unsigned long hart_mask_base,
                                          unsigned long start_addr,
                                          unsigned long size,
                                          unsigned long asid)
{
    return sbi_ecall(SBI_EXTID_RFNC, SBI_REMOTE_HFENCE_VVMA_ASID_FID, hart_mask,
                     hart_mask_base, start_addr, size, asid, 0);
}

struct sbiret sbi_remote_hfence_vvma(const unsigned long hart_mask,
                                     unsigned long hart_mask_base,
                                     unsigned long start_addr,
                                     unsigned long size)
{
    return sbi_ecall(SBI_EXTID_RFNC, SBI_REMOTE_HFENCE_VVMA_FID, hart_mask,
                     hart_mask_base, start_addr, size, 0, 0);
}

struct sbiret sbi_hart_start(unsigned long hartid, unsigned long start_addr,
                             unsigned long priv)
{
    return sbi_ecall(SBI_EXTID_HSM, SBI_HART_START_FID, hartid,
                     start_addr, priv, 0, 0, 0);    
}

struct sbiret sbi_hart_stop()
{
    return sbi_ecall(SBI_EXTID_HSM, SBI_HART_STOP_FID, 0,
                     0, 0, 0, 0, 0);   
}

struct sbiret sbi_hart_status(unsigned long hartid)
{
    return sbi_ecall(SBI_EXTID_HSM, SBI_HART_STATUS_FID, hartid,
                     0, 0, 0, 0, 0);   
}

static unsigned long ext_table[] = {SBI_LGCY_EXTID_SETTIMER,
                                    SBI_LGCY_EXTID_SENDIPI,
                                    SBI_LGCY_EXTID_REMFENCEI,
                                    SBI_LGCY_EXTID_REMSFENCEVMA,
                                    SBI_LGCY_EXTID_REMSFENCEASID,
                                    SBI_EXTID_BASE,
                                    SBI_EXTID_TIME,
                                    SBI_EXTID_IPI,
                                    SBI_EXTID_RFNC,
                                    SBI_EXTID_HSM};

static const size_t NUM_EXT = sizeof(ext_table) / sizeof(unsigned long);

enum SBI_MSG_EVENTS { SEND_IPI, HART_START };

void sbi_msg_handler(uint32_t event, uint64_t data);
CPU_MSG_HANDLER(sbi_msg_handler, SBI_MSG_ID);

void sbi_msg_handler(uint32_t event, uint64_t data)
{
    switch (event) {
        case SEND_IPI:
            CSRS(CSR_HVIP, HIP_VSSIP);
            break;
        case HART_START: {
            spin_lock(&cpu.vcpu->arch.sbi_ctx.lock);
            if(cpu.vcpu->arch.sbi_ctx.state == START_PENDING) {
                vcpu_arch_reset(cpu.vcpu, cpu.vcpu->arch.sbi_ctx.start_addr);
                vcpu_writereg(cpu.vcpu, REG_A1, cpu.vcpu->arch.sbi_ctx.priv); 
                cpu.vcpu->arch.sbi_ctx.state = STARTED;
            } 
            spin_unlock(&cpu.vcpu->arch.sbi_ctx.lock);
        } break;
        default:
            WARNING("unknown sbi msg");
            break;
    }
}

struct sbiret sbi_time_handler(unsigned long fid)
{
    if (fid != SBI_SET_TIMER_FID) return (struct sbiret){SBI_ERR_NOT_SUPPORTED};

    uint64_t stime_value = vcpu_readreg(cpu.vcpu, REG_A0);

    CSRC(CSR_HVIP, HIP_VSTIP);
    sbi_set_timer(stime_value);  // assumes always success
    CSRC(CSR_HVIP, HIP_VSTIP);
    CSRS(sie, SIE_STIE);

    return (struct sbiret){SBI_SUCCESS};
}

void sbi_timer_irq_handler()
{
    CSRS(CSR_HVIP, HIP_VSTIP);
    CSRC(sie, SIE_STIE);
}

struct sbiret sbi_ipi_handler(unsigned long fid)
{
    if (fid != SBI_SEND_IPI_FID) return (struct sbiret){SBI_ERR_NOT_SUPPORTED};

    unsigned long hart_mask = vcpu_readreg(cpu.vcpu, REG_A0);
    unsigned long hart_mask_base = vcpu_readreg(cpu.vcpu, REG_A1);

    cpu_msg_t msg = {
        .handler = SBI_MSG_ID,
        .event = SEND_IPI,
    };

    for (size_t i = 0; i < sizeof(hart_mask) * 8; i++) {
        if (bitmap_get((bitmap_t)&hart_mask, i)) {
            uint64_t vhart_id = hart_mask_base + i;
            int64_t phart_id = vm_translate_to_pcpuid(cpu.vcpu->vm, vhart_id); 
            if(phart_id >= 0) cpu_send_msg(phart_id, &msg);
        }
    }

    return (struct sbiret){SBI_SUCCESS};
}

struct sbiret sbi_base_handler(unsigned long fid)
{
    struct sbiret ret = {.error = SBI_SUCCESS};
    unsigned long extid = vcpu_readreg(cpu.vcpu, REG_A0);

    switch (fid) {
        case SBI_GET_SBI_SPEC_VERSION_FID:
            ret.value = 2;
            break;
        case SBI_PROBE_EXTENSION_FID:
            ret.value = 0;
            for (size_t i = 0; i < NUM_EXT; i++) {
                if (ext_table[i] == extid) {
                    ret.value = extid;
                }
            }
            break;
        default:
            break;
            /**
             * Implement sbi and hw implementation version and id.
             */
    }

    return ret;
}

struct sbiret sbi_rfence_handler(unsigned long fid)
{
    struct sbiret ret;

    unsigned long hart_mask = vcpu_readreg(cpu.vcpu, REG_A0);
    unsigned long hart_mask_base = vcpu_readreg(cpu.vcpu, REG_A1);
    unsigned long start_addr = vcpu_readreg(cpu.vcpu, REG_A2);
    unsigned long size = vcpu_readreg(cpu.vcpu, REG_A3);
    unsigned long asid = vcpu_readreg(cpu.vcpu, REG_A4);

    /**
     * For now we only support masks starting at 0. TODO: make this not true
     */
    if (hart_mask_base != 0) return (struct sbiret){SBI_ERR_INVALID_PARAM};

    unsigned long phart_mask = vm_translate_to_pcpu_mask(
        cpu.vcpu->vm, hart_mask, sizeof(hart_mask) * 8);

    switch (fid) {
        case SBI_REMOTE_FENCE_I_FID:
            ret = sbi_remote_fence_i(phart_mask, 0);
            break;
        case SBI_REMOTE_SFENCE_VMA_FID:
            ret = sbi_remote_hfence_vvma(phart_mask, 0, start_addr, size);
            break;
        case SBI_REMOTE_SFENCE_VMA_ASID_FID:
            ret = sbi_remote_hfence_vvma_asid(phart_mask, 0, start_addr, size, asid);
            break;
        default:
            ret.error = SBI_ERR_NOT_SUPPORTED;
    }

    return ret;
}

struct sbiret sbi_hsm_start_handler() {
    
    struct sbiret ret;
    uint64_t vhart_id = vcpu_readreg(cpu.vcpu, REG_A0);
    
    if(vhart_id == cpu.vcpu->id){
        ret.error = SBI_ERR_ALREADY_AVAILABLE;
    } else {
        vcpu_t *vcpu = vm_get_vcpu(cpu.vcpu->vm, vhart_id);
        if(vcpu == NULL) {
            ret.error = SBI_ERR_INVALID_PARAM;
        } else { 
            spin_lock(&vcpu->arch.sbi_ctx.lock);
            if (vcpu->arch.sbi_ctx.state == STARTED) {
                ret.error = SBI_ERR_ALREADY_AVAILABLE;
            } else if (vcpu->arch.sbi_ctx.state != STOPPED) {
                ret.error = SBI_ERR_FAILURE;
            } else {
                uint64_t start_addr = vcpu_readreg(cpu.vcpu, REG_A1);
                uint64_t priv = vcpu_readreg(cpu.vcpu, REG_A2);
                vcpu->arch.sbi_ctx.state = START_PENDING;
                vcpu->arch.sbi_ctx.start_addr = start_addr;
                vcpu->arch.sbi_ctx.priv = priv;

                fence_sync_write();

                cpu_msg_t msg = {
                    .handler = SBI_MSG_ID,
                    .event = HART_START,
                    .data = 0xdeadbeef
                };
                cpu_send_msg(vcpu->phys_id, &msg);
               
                ret.error = SBI_SUCCESS; 
            }
            spin_unlock(&vcpu->arch.sbi_ctx.lock);
       }
   }

    return ret;
}

struct sbiret sbi_hsm_status_handler() {

    struct sbiret ret;
    uint64_t vhart_id = vcpu_readreg(cpu.vcpu, REG_A0);
    vcpu_t *vhart = vm_get_vcpu(cpu.vcpu->vm, vhart_id);

    if(vhart != NULL) { 
        ret.error = SBI_SUCCESS;
        ret.value = vhart->arch.sbi_ctx.state;
    } else {
        ret.error = SBI_ERR_INVALID_PARAM;
    }

    return ret;
}

struct sbiret sbi_hsm_handler(unsigned long fid){

    struct sbiret ret;

    switch(fid) {
        case SBI_HART_START_FID:
            ret = sbi_hsm_start_handler();
        break;
        case SBI_HART_STATUS_FID:
            ret = sbi_hsm_status_handler(); 
        break;
        default:
            ret.error = SBI_ERR_NOT_SUPPORTED;
   }

   return ret; 
}

void sbi_lgcy_sendipi_handler()
{
    unsigned long *hart_mask = (unsigned long *)vcpu_readreg(cpu.vcpu, REG_A0);
    if (hart_mask == NULL) return;

    unsigned long phart_mask = 0;
    vm_readmem(cpu.vcpu->vm, &phart_mask, (uintptr_t)hart_mask,
               sizeof(unsigned long), false);
    phart_mask = vm_translate_to_pcpu_mask(cpu.vcpu->vm, phart_mask,
                                           sizeof(unsigned long));

    cpu_msg_t msg = {
        .handler = SBI_MSG_ID,
        .event = SEND_IPI,
    };

    for (int i = 0; i < sizeof(phart_mask) * 8; i++) {
        if (bitmap_get((bitmap_t)&phart_mask, i)) {
            cpu_send_msg(i, &msg);
        }
    }
}

void sbi_lgcy_rfence_handler(unsigned long extid)
{
    unsigned long *hart_mask = (unsigned long *)vcpu_readreg(cpu.vcpu, REG_A0);
    if (hart_mask == NULL) return;

    unsigned long phart_mask = 0;
    vm_readmem(cpu.vcpu->vm, &phart_mask, (uintptr_t)hart_mask,
               sizeof(unsigned long), false);
    phart_mask = vm_translate_to_pcpu_mask(cpu.vcpu->vm, (uint64_t)hart_mask,
                                           sizeof(unsigned long));

    unsigned long start_addr = vcpu_readreg(cpu.vcpu, REG_A2);
    unsigned long size = vcpu_readreg(cpu.vcpu, REG_A3);
    unsigned long asid = vcpu_readreg(cpu.vcpu, REG_A4);

    switch (extid) {
        case SBI_LGCY_EXTID_REMFENCEI:
            sbi_remote_fence_i(phart_mask, 0);
            break;
        case SBI_LGCY_EXTID_REMSFENCEVMA:
            sbi_remote_hfence_vvma(phart_mask, 0, start_addr, size);
            break;
        case SBI_LGCY_EXTID_REMSFENCEASID:
            sbi_remote_hfence_vvma_asid(phart_mask, 0, start_addr, size, asid);
            break;
    }
}

void sbi_lgcy_putchar_handler() 
{
    char c = (char)vcpu_readreg(cpu.vcpu, REG_A0);
    sbi_console_putchar(c);
}

void sbi_lgcy_handler(unsigned long extid)
{
    switch (extid) {
        case SBI_LGCY_EXTID_SETTIMER:
            (void)sbi_time_handler(SBI_SET_TIMER_FID);
            break;
        case SBI_LGCY_EXTID_SENDIPI:
            sbi_lgcy_sendipi_handler();
            break;
        case SBI_LGCY_EXTID_REMFENCEI:
        case SBI_LGCY_EXTID_REMSFENCEVMA:
        case SBI_LGCY_EXTID_REMSFENCEASID:
            sbi_lgcy_rfence_handler(extid);
            break;
        case SBI_LGCY_EXTID_PUTCHAR:
            sbi_lgcy_putchar_handler();
            break;
        default:
            WARNING("guest issued unsupported sbi legacy extension call (%d)",
                    extid);
    }
}

size_t sbi_vs_handler()
{
    unsigned long extid = vcpu_readreg(cpu.vcpu, REG_A7);

    if (extid < 16) {
        sbi_lgcy_handler(extid);
    } else {
        unsigned long fid = vcpu_readreg(cpu.vcpu, REG_A6);
        struct sbiret ret;

        switch (extid) {
            case SBI_EXTID_BASE:
                ret = sbi_base_handler(fid);
                break;
            case SBI_EXTID_TIME:
                ret = sbi_time_handler(fid);
                break;
            case SBI_EXTID_IPI:
                ret = sbi_ipi_handler(fid);
                break;
            case SBI_EXTID_RFNC:
                ret = sbi_rfence_handler(fid);
                break;
            case SBI_EXTID_HSM:
                ret = sbi_hsm_handler(fid);
                break;
            default:
                WARNING("guest issued unsupport sbi extension call (%d)",
                        extid);
                ret.value = SBI_ERR_NOT_SUPPORTED;
        }

        vcpu_writereg(cpu.vcpu, REG_A0, ret.error);
        vcpu_writereg(cpu.vcpu, REG_A1, ret.value);
    }

    return 4;
}

void sbi_init()
{
    struct sbiret ret;

    ret = sbi_get_spec_version();

    if (ret.error != SBI_SUCCESS || ret.value < 2) {
        ERROR("not supported SBI spec version: 0x%x", ret.value);
    }

    for (size_t i = 0; i < NUM_EXT; i++) {
        ret = sbi_probe_extension(ext_table[i]);
        if (ret.error != SBI_SUCCESS || ret.value == 0) {
            ERROR("sbi does not support ext 0x%x", ext_table[i]);
        }
    }

    interrupts_reserve(TIMR_INT_ID, sbi_timer_irq_handler);
}