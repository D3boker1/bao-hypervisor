#include <imsic.h>
#include <cpu.h>
#include <interrupts.h>

#include <irqc.h>
#include <mem.h>
#include <platform.h>
#include <vm.h>
#include <arch/csrs.h>
#include <fences.h>

volatile struct imsic_global_hw* imsic[PLAT_CPU_NUM];

void imsic_init(void){
    /** Every intp is triggrable */
    CSRW(CSR_SISELECT, IMSIC_EITHRESHOLD);
    CSRW(CSR_SIREG, 0);

    /** Disable all interrupts */
    CSRW(CSR_SISELECT, IMSIC_EIE);
    CSRW(CSR_SIREG, 0x0);

    /** Enable interrupt delivery */
    CSRW(CSR_SISELECT, IMSIC_EIDELIVERY);
    CSRW(CSR_SIREG, 1);

    /** Maps the S-lvl interrupt file */
    imsic[cpu()->id] = (void*) mem_alloc_map_dev(&cpu()->as, SEC_HYP_GLOBAL, 
                    INVALID_VA, 
                    platform.arch.imsic_base+(cpu()->id*PAGE_SIZE*IMSIC_NUM_FILES), 
                    NUM_PAGES(sizeof(struct imsic_global_hw)));   
}

void imsic_set_enbl(irqid_t intp_id){
    CSRW(CSR_SISELECT, IMSIC_EIE+(intp_id/64));
    CSRS(CSR_SIREG, 1ULL << (intp_id%64));
}

bool imsic_get_pend(irqid_t intp_id){
    CSRW(CSR_SISELECT, IMSIC_EIP+(intp_id/64));
    return CSRR(CSR_SIREG) && (1ULL << (intp_id%64));
}

void imsic_send_msi(cpuid_t target_cpu, irqid_t ipi_id){
    imsic[target_cpu]->seteipnum_le = ipi_id;
}

void imsic_handle(void){
    uint32_t intp_identity;
    
    while ((intp_identity = (CSRR(CSR_STOPEI) >> STOPEI_EEID))){
        enum irq_res res = interrupts_handle(intp_identity);
        if (res == HANDLED_BY_HYP){
            /** Write to STOPEI to clear the interrupt */
            CSRW(CSR_STOPEI, 0);
        }
    };
}