/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#ifndef IMSIC_H
#define IMSIC_H

#include <bao.h>
#include <platform.h>

#define IMSIC_MAX_INTERRUPTS    (2047)
/** We only support 1 guest per hart at the moment */
#define IMSIC_NUM_VS_FILES      (1)  
#define IMSIC_NUM_FILES         (IMSIC_NUM_VS_FILES + 1)

#define STOPEI_EEID             (16)

#define IMSIC_EIDELIVERY		(0x70)
#define IMSIC_EITHRESHOLD		(0x72)
#define IMSIC_EIP		        (0x80)
#define IMSIC_EIE		        (0xC0)

struct imsic_intp_file_hw
{
    uint32_t seteipnum_le;
    uint32_t seteipnum_be;
}__attribute__((__packed__, aligned(0x1000ULL)));

/**
 *  NOTE: Should we be mapping the VS-files?
 *  We could inject any pending interrupt into the guest
 *  through the CSRs. We could create a function for that
 *  matter caller imsic_inject_virq(target_guest, intp_id)
 */
struct imsic_global_hw
{
    struct imsic_intp_file_hw file[IMSIC_NUM_FILES];
}__attribute__((__packed__, aligned(0x1000ULL)));

/**
 * @brief Initilaizes the IMSIC
 * 
 *        The function initializes the IMSIC by configuring its registers 
 *        and mapping the S-lvl interrupt file. It sets every intp as 
 *        triggerable, disables all interrupts, enables interrupt delivery, 
 *        and maps the S-lvl interrupt file in memory.
 * 
 */
void imsic_init(void);

/**
 * @brief Check if a given interrupt is pending 
 *        for the cpu that calls the function 
 * 
 * @param intp_id the inetrrupt to check
 * @return true the interrupt is pending
 * @return false the interrupt is not pending
 */
bool imsic_get_pend(irqid_t intp_id);

void imsic_clr_pend(irqid_t intp_id);

/**
 * @brief enables a given interrupt for the IMSIC 
 *        that executes the function
 * 
 * @param intp_id the interrupt to enable
 */
void imsic_set_enbl(irqid_t intp_id);

/**
 * @brief Sends an MSI to the specified CPU with the specified IPI ID.
 * 
 *        The function sends an MSI to the specified CPU by setting the 
 *        seteipnum_le register in the IMSIC. 
 *        The seteipnum_le register is used to specify the ID of the 
 *        interrupt being sent. Only little endian is supported.
 * 
 * @param target_cpu The ID of the target CPU
 * @param imsic_file the target interrupt file. 
 *                   0 is the hypervisor interrupt file;
 *                   N is for guest N;
 * @param ipi_id The ID of the IPI to send.
 */
void imsic_send_msi(cpuid_t target_cpu, size_t imsic_file, irqid_t ipi_id);

/**
 * @brief Handles interrupts in the IMSIC.
 * 
 *        The function handles interrupts in the IMSIC by looping through 
 *        all pending interrupts and calling the interrupts_handle() 
 *        function to handle each one. If an interrupt is handled by the 
 *        hypervisor, the function writes to the STOPEI CSR to clear the 
 *        interrupt. Otherwise, the Guest cleans it.
 * 
 */
void imsic_handle(void);

#endif //IMSIC_H