/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#ifndef APLIC_H
#define APLIC_H

#include <bao.h>
#include <platform.h>

#define APLIC_DOMAIN_NUM_HARTS          (PLAT_CPU_NUM)
#define APLIC_MAX_NUM_HARTS_MAKS        (0x3FFF)
/** APLIC Specific types */
typedef cpuid_t idcid_t;

/** APLIC Addresses defines */
#define APLIC_IDC_OFF                   (0x4000)
#define APLIC_IDC_SIZE                  (32)

#define APLIC_MAX_INTERRUPTS            (1024)
#define APLIC_NUM_SRCCFG_REGS           (APLIC_MAX_INTERRUPTS - 1)
#define APLIC_NUM_TARGET_REGS           (APLIC_MAX_INTERRUPTS - 1)
/** where x = E or P*/
#define APLIC_NUM_CLRIx_REGS            (APLIC_MAX_INTERRUPTS / 32)
#define APLIC_NUM_SETIx_REGS            (APLIC_MAX_INTERRUPTS / 32)
#define APLIC_NUM_INTP_PER_REG          (APLIC_MAX_INTERRUPTS / APLIC_NUM_SETIx_REGS)

/** Source Mode defines */
#define APLIC_SOURCECFG_SM_MASK         (0x00000007)
#define APLIC_SOURCECFG_SM_INACTIVE     (0x0)
#define APLIC_SOURCECFG_SM_DETACH       (0x1)
#define APLIC_SOURCECFG_SM_EDGE_RISE    (0x4)
#define APLIC_SOURCECFG_SM_EDGE_FALL    (0x5)
#define APLIC_SOURCECFG_SM_LEVEL_HIGH   (0x6)
#define APLIC_SOURCECFG_SM_LEVEL_LOW    (0x7)
#define APLIC_SOURCECFG_SM_DEFAULT      APLIC_SOURCECFG_SM_INACTIVE

/** APLIC fields and masks defines */
#define APLIC_DOMAINCFG_DM              (1U << 2)
#define APLIC_DOMAINCFG_IE              (1U << 8)
#define APLIC_DOMAINCFG_RO80            (0x80 << 24)

#define APLIC_SRCCFG_D                  (1U << 10)
#define APLIC_SRCCFG_SM                 ((1U << 0) | (1U << 1) | (1U << 2))

#define APLIC_TARGET_HART_IDX_SHIFT     (18)
#define APLIC_TARGET_GUEST_IDX_SHIFT    (12)
#define APLIC_TARGET_HART_IDX_MASK      (APLIC_MAX_NUM_HARTS_MAKS)
#define APLIC_TARGET_IPRIO_MASK         (0xFF)
#define APLIC_TARGET_EEID_MASK          (0x7FF)
#define APLIC_TARGET_GUEST_INDEX_MASK   (0x3F)
#define APLIC_TARGET_PRIO_DEFAULT       (1)
#define APLIC_TARGET_DIRECT_MASK        (0xFFFC00FF)
#define APLIC_TARGET_MSI_MASK           (0xFFFFF7FF)
/** Data structures for APLIC devices */
struct aplic_global_hw {
    uint32_t domaincfg;
    uint32_t sourcecfg[APLIC_NUM_SRCCFG_REGS];
    uint8_t  reserved1[0x1C00 - 0x1000];
    uint32_t setip[APLIC_NUM_SETIx_REGS];
    uint8_t  reserved2[0x1CDC - 0x1C80];
    uint32_t setipnum;
    uint8_t  reserved3[0x1D00 - 0x1CE0];
    uint32_t in_clrip[APLIC_NUM_CLRIx_REGS];
    uint8_t  reserved4[0x1DDC - 0x1D80];
    uint32_t clripnum;
    uint8_t  reserved5[0x1E00 - 0x1DE0];
    uint32_t setie[APLIC_NUM_SETIx_REGS];
    uint8_t  reserved6[0x1EDC - 0x1E80];
    uint32_t setienum;
    uint8_t  reserved7[0x1F00 - 0x1EE0];
    uint32_t clrie[APLIC_NUM_CLRIx_REGS];
    uint8_t  reserved8[0x1FDC - 0x1F80];
    uint32_t clrienum;
    uint8_t  reserved9[0x2000 - 0x1FE0];
    uint32_t setipnum_le;
    uint32_t setipnum_be;
    uint8_t reserved10[0x3000 - 0x2008];
    uint32_t genmsi;
    uint32_t target[APLIC_NUM_TARGET_REGS];
} __attribute__((__packed__, aligned(PAGE_SIZE)));

struct aplic_hart_hw {
    uint32_t idelivery;
    uint32_t iforce;
    uint32_t ithreshold;
    uint8_t  reserved[0x18-0x0C];
    uint32_t topi;
    uint32_t claimi;
}__attribute__((__packed__, aligned(APLIC_IDC_SIZE))); // IDC structure CANNOT be page aligned.

extern volatile struct aplic_hart_hw *aplic_hart;

/**
 * @brief Check if the phys APLIC is in MSI mode
 * 
 * @return true : it is in MSI mode
 * @return false : it is in direct mode
 */
bool aplic_msi_mode(void);

/**
 * @brief Initialize the APLIC domain.
 * 
 */
void aplic_init(void);

/**
 * @brief Initialize the APLIC IDCs. 
 * 
 */
void aplic_idc_init(void);

/**
 * @brief Write to APLIC's sourcecfg register
 * 
 * @param intp_id interruption ID identifies the interrupt to be configured.
 * @param val Value to be written into sourcecfg
 */
void aplic_set_sourcecfg(irqid_t intp_id, uint32_t val);

/**
 * @brief Read from APLIC's sourcecfg register
 * 
 * @param intp_id interruption ID identifies the interrupt to be read.
 * @return a 32 bit value containing interrupt sourcecfg configuration.
 */
uint32_t aplic_get_sourcecfg(irqid_t intp_id);

/**
 * @brief Set a given interrupt as pending. 
 * 
 * @param intp_id Interrupt to be set as pending
 */
void aplic_set_pend(irqid_t intp_id);

/**
 * @brief Potentially modifies the pending bits for interrupt
 *        sources reg_indx × 32 through reg_indx × 32 + 31.
 * 
 * @param reg_indx register index
 * @param reg_val register value to be written.
 */
void aplic_set32_pend(uint8_t reg_indx, uint32_t reg_val);

/**
 * @brief Read the pending value of a given interrut
 * 
 * @param intp_id interrupt to read from
 * @return true if interrupt is pending
 * @return false if interrupt is NOT pending
 */
bool aplic_get_pend(irqid_t intp_id);

/**
 * @brief Reads the pending bits for interrupt sources 
 *        reg_indx × 32 through reg_indx × 32 + 31. 
 * 
 * @param reg_indx register index
 * @return a 32 bit value containing interrupts pending state for reg_indx.
 */
uint32_t aplic_get32_pend(uint8_t reg_indx);

/**
 * @brief Clear a pending bit from a inetrrupt writting to in_clripnum.
 *  
 * @param intp_id interrupt to clear the pending bit from
 */
void aplic_clr_pend(irqid_t intp_id);

/**
 * @brief Read the current rectified value for interrupt sources 
 *        reg_indx × 32 through reg_indx × 32 + 31. 
 * 
 * @param reg_indx register index
 * @return a 32 bit value containing interrupts rectified state for reg_indx.
 */
uint32_t aplic_get_inclrip(uint8_t reg_indx);

/**
 * @brief Enable a given interrupt
 * 
 * @param intp_id interrupt to be enabled
 */
void aplic_set_enbl(irqid_t intp_id);

/**
 * @brief Modifies the enable bits for interrupt
 *        sources reg_indx × 32 through reg_indx × 32 + 31.
 * 
 * @param reg_indx register index
 * @param reg_val register value to be written.
 */
void aplic_set32_enbl(uint8_t reg_indx, uint32_t reg_val);

/**
 * @brief Read the enable value of a given interrut
 * 
 * @param intp_id interrupt to read from
 * @return true if interrupt is enabled
 * @return false if interrupt is NOT enbaled
 */
bool aplic_get_enbl(irqid_t intp_id);

/**
 * @brief Disable a given interrupt 
 * 
 * @param intp_id Interrupt to disable
 */
void aplic_clr_enbl(irqid_t intp_id);

/**
 * @brief Modifies the enable bits for interrupt
 *        sources reg_indx × 32 through reg_indx × 32 + 31.
 * 
 * @param reg_indx register index
 * @param reg_val register value to be written.
 */
void aplic_clr32_enbl(uint8_t reg_indx, uint32_t reg_val);

/**
 * @brief Write to register target (see AIA spec 0.3.2 section 4.5.16)
 * 
 * @param intp_id Interrupt to configure the target options
 * @param val Value to be written in target register
 * 
 * If domaincfg.DM = 0, target have the format:
 * 
 * +-----------+------------+----------------------------------------+
 * | Bit-Field |    Name    |              Description               |
 * +-----------+------------+----------------------------------------+
 * | 31:28     | Hart Index | Hart to which interrupts will delivery |
 * | 7:0       | IPRIO      | Interrupt priority.                    |
 * +-----------+------------+----------------------------------------+
 * 
 * 
 * If domaincfg.DM = 1, target have the format:
 * 
 * +-----------+-------------+------------------------------------------------+
 * | Bit-Field |    Name     |                  Description                   |
 * +-----------+-------------+------------------------------------------------+
 * | 31:28     | Hart Index  | Hart to which interrupts will delivery         |
 * | 17:12     | Guest Index | Only if hypervisor extension were implemented. |
 * | 10:0      | EIID        | External Interrupt Identity. Specifies the data|
 * |           |             | value for MSIs                                 |
 * +-----------+-------------+------------------------------------------------+
 */
void aplic_set_target(irqid_t intp_id, uint32_t val);

/**
 * @brief Read the target configurations for a given interrupt
 * 
 * @param intp_id Interrupt to read from
 * @return a 32 bit value with the target data
 */
uint32_t aplic_get_target(irqid_t intp_id);

/**
 * @brief Useful for testing. Seting this register forces an interrupt to
 *        be asserted to the corresponding hart
 * 
 * @param idc_id IDC to force an interruption
 * @param en value to be written
 */
void aplic_idc_set_iforce(idcid_t idc_id, bool en);

/**
 * @brief Returns the highest pending and enabled interrupt.
 * 
 * Claimi has the same value as topi. However, reading claimi has the side 
 * effect of clearing the pending bit for the reported interrupt identity.
 * 
 * @param idc_id IDC to read and clear the pending-bit the highest-priority
 * @return uint32_t returns the interrupt identity and interrupt priority.
 */
uint32_t aplic_idc_get_claimi(idcid_t idc_id);

/**
 * @brief Handles an incomming interrupt in irq controller.
 * 
 */
void aplic_handle(void);

#endif //_APLIC_H_