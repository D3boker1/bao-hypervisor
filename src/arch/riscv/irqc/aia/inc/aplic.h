/**
 * @file aplic.h
 * @author Jose Martins <jose.martins@bao-project.org>
 * @author Francisco Marques (fmarques_00@protonmail.com)
 * @brief This module gives a set of function to controls the RISC-V APLIC.
 * @version 0.1
 * @date 2022-09-23
 * 
 * @copyright Copyright (c) Bao Project (www.bao-project.org), 2019-
 * 
 * Bao is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License version 2 as published by the Free
 * Software Foundation, with a special exception exempting guest code from such
 * license. See the COPYING file in the top-level directory for details.
 */

#ifndef _APLIC_H_
#define _APLIC_H_

#include <bao.h>
//#include <csrs.h>
#include <plat/aplic.h>
//#include <stdio.h>

//#define APLIC_PLAT_IDC_NUM (4)

/**==== APLIC Specific types ====*/
typedef unsigned idcid_t;
typedef unsigned irqid_t;

/**==== APLIC Addresses defines ====*/
// TODO: addr is given by configuration file
#define APLIC_BASE                      (0xd000000)
#define APLIC_IDC_OFF                   (0x4000)
#define APLIC_IDC_BASE                  (APLIC_BASE + APLIC_IDC_OFF)

#define APLIC_MAX_INTERRUPTS            (1024)
#define APLIC_NUM_SRCCFG_REGS           (APLIC_MAX_INTERRUPTS - 1)
#define APLIC_NUM_TARGET_REGS           (APLIC_MAX_INTERRUPTS - 1)
/** where x = E or P*/
#define APLIC_NUM_CLRIx_REGS            (APLIC_MAX_INTERRUPTS / 32)
#define APLIC_NUM_SETIx_REGS            (APLIC_MAX_INTERRUPTS / 32)

/**==== Source Mode defines ====*/
#define APLIC_SOURCECFG_SM_MASK         0x00000007
#define APLIC_SOURCECFG_SM_INACTIVE     0x0
#define APLIC_SOURCECFG_SM_DETACH       0x1
#define APLIC_SOURCECFG_SM_EDGE_RISE    0x4
#define APLIC_SOURCECFG_SM_EDGE_FALL    0x5
#define APLIC_SOURCECFG_SM_LEVEL_HIGH   0x6
#define APLIC_SOURCECFG_SM_LEVEL_LOW    0x7
#define APLIC_SOURCECFG_SM_DEFAULT      APLIC_SOURCECFG_SM_DETACH

/** Sources State*/
#define IMPLEMENTED                     (1)
#define NOT_IMPLEMENTED                 (0)

/**==== APLIC Offsets ====*/
#define APLIC_DOMAIN_OFF                (0x0000)
#define APLIC_SOURCECFG_OFF             (0x0004)
#define APLIC_MMSIADDRCFG_OFF           (0x1BC0)
#define APLIC_MMSIADDRCFGH_OFF          (0x1BC4)
#define APLIC_SMSIADDRCFG_OFF           (0x1BC8)
#define APLIC_SMSIADDRCFGH_OFF          (0x1BCC)
#define APLIC_SETIP_OFF                 (0x1C00)
#define APLIC_SETIPNUM_OFF              (0x1CDC)
#define APLIC_IN_CLRIP_OFF              (0x1D00)
#define APLIC_CLRIPNUM_OFF              (0x1DDC)
#define APLIC_SETIE_OFF                 (0x1E00)
#define APLIC_SETIENUM_OFF              (0x1EDC)
#define APLIC_CLRIE_OFF                 (0x1F00)
#define APLIC_CLRIENUM_OFF              (0x1FDC)
#define APLIC_SETIPNUM_LE_OFF           (0x2000)
#define APLIC_SETIPNUM_BE_OFF           (0x2004)
#define APLIC_GENMSI_OFF                (0x3000)
#define APLIC_TARGET_OFF                (0x3004)

/**==== IDC Offsets ====*/
#define APLIC_IDC_IDELIVERY_OFF         (0x00)
#define APLIC_IDC_IFORCE_OFF            (0x04)
#define APLIC_IDC_ITHRESHOLD_OFF        (0x08)
#define APLIC_IDC_TOPI_OFF              (0x18)
#define APLIC_IDC_CLAIMI_OFF            (0x1C)

/**==== Data structures for APLIC devices ====*/
struct aplic_global {
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

struct aplic_idc {
    uint32_t idelivery;
    uint32_t iforce;
    uint32_t ithreshold;
    uint8_t  reserved[0x18-0x0C];
    uint32_t topi;
    uint32_t claimi;
}; // IDC structure CANNOT be page aligned.

extern uint32_t impl_src[APLIC_MAX_INTERRUPTS];

extern volatile struct aplic_global aplic_domain;
extern volatile struct aplic_idc idc[APLIC_PLAT_IDC_NUM];

/**==== Initialization Functions ====*/

/**
 * @brief Initialize the aplic domain.
 * 
 */
void aplic_init(void);

/**
 * @brief Initialize the aplic IDCs. 
 * The IDC component is the closest to the cpu.
 * 
 */
void aplic_idc_init(void);

/**==== APLIC Domain registers manipulation functions ====*/

/**
 * @brief Write to aplic domaincfg register.
 * AIA Spec. 0.3.2 section 4.5.1
 * 
 * @param val Value to be written into domaincfg
 */
void aplic_set_domaincfg(uint32_t val);

/**
 * @brief Read from aplic domaincfg register.
 * AIA Spec. 0.3.2 section 4.5.1
 * 
 * @return uint32_t 32 bit value containing domaincfg info.
 */
uint32_t aplic_get_domaincfg(void);

/**
 * @brief Write to aplic's sourcecfg register
 * 
 * @param int_id interruption ID identifies the interrupt to be configured/read.
 * @param val Value to be written into sourcecfg
 */
void aplic_set_sourcecfg(irqid_t int_id, uint32_t val);

/**
 * @brief Read from aplic's sourcecfg register
 * 
 * @param int_id interruption ID identifies the interrupt to be configured/read.
 * @return uint32_t 32 bit value containing interrupt int_id's configuration.
 */
uint32_t aplic_get_sourcecfg(irqid_t int_id);

/**
 * @brief Set a given interrupt as pending, using setipnum register.
 * This should be faster than aplic_set_pend.
 * 
 * @param int_id Interrupt to be set as pending
 */
void aplic_set_pend_num(irqid_t int_id);

/**
 * @brief Read the pending value of a given interrut
 * 
 * @param int_id interrupt to read from
 * @return true if interrupt is pended
 * @return false if interrupt is NOT pended
 */
bool aplic_get_pend(irqid_t int_id);

/**
 * @brief Clear a pending bit from a inetrrupt writting to in_clripnum.
 * Should be faster than aplic_set_inclrip.
 *  
 * @param int_id interrupt to clear the pending bit from
 */
void aplic_set_clripnum(irqid_t intp_id);

/**
 * @brief Read the current rectified value for a given interrupt
 * 
 * @param int_id interrupt to read from
 * @return true 
 * @return false 
 */
bool aplic_get_inclrip(irqid_t int_id);

/**
 * @brief Enable a given interrupt writting to setienum register
 * Should be faster than aplic_set_ie 
 * 
 * @param int_id Interrupt to be enabled
 */
void aplic_set_ienum(irqid_t int_id);

/**
 * @brief Read if a given interrupt is enabled
 * 
 * @param intp_id interrupt to evaluate if it is enabled
 * @return uint32_t 
 */
bool aplic_get_ie(irqid_t intp_id);

/**
 * @brief Clear enable bit be writting to clrie register of a given interrupt. 
 * It should be faster than aplic_set_clrie 
 * 
 * @param int_id Interrupt to clear the enable bit
 */
void aplic_set_clrienum(irqid_t int_id);

/**
 * @brief Write to register target (see AIA spec 0.3.2 section 4.5.16)
 * 
 * @param int_id Interrupt to configure the target options
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
 * +-----------+-------------+----------------------------------------------------------------+
 * | Bit-Field |    Name     |                          Description                           |
 * +-----------+-------------+----------------------------------------------------------------+
 * | 31:28     | Hart Index  | Hart to which interrupts will delivery                         |
 * | 17:12     | Guest Index | Only if hypervisor extension were implemented.                 |
 * | 10:0      | EIID        | External Interrupt Identity. Specifies the data value for MSIs |
 * +-----------+-------------+----------------------------------------------------------------+
 */
void aplic_set_target(irqid_t int_id, uint32_t val);

/**
 * @brief Read the target configurations for a given interrupt
 * 
 * @param int_id Interrupt to read from
 * @return uint32_t value with the requested data
 */
uint32_t aplic_get_target(irqid_t int_id);

/**==== APLIC IDC's registers manipulation functions ====*/

/**
 * @brief Enable/Disable the delivering for a given idc
 * 
 * @param idc_id IDC to enbale/disable the delivering
 * @param en if 0: interrupts delivery disable; if 1: interrupts delivery enable
 */
void aplic_idc_set_idelivery(idcid_t idc_id, bool en);

/**
 * @brief Read if for a given idc the interrupts are being delivered.
 * 
 * @param idc_id IDC to read.
 * @return true Interrupt delivery is enabled
 * @return false Interrupt delivery is disable
 */
bool aplic_idc_get_idelivery(idcid_t idc_id);

/**
 * @brief Useful for testing. Seting this register forces an interrupt to
 * be asserted to the corresponding hart
 * 
 * @param idc_id IDC to force an interruption
 * @param en value to be written
 */
void aplic_idc_set_iforce(idcid_t idc_id, bool en);

/**
 * @brief Read if for a given IDC was forced an interrupt
 * 
 * @param idc_id IDC to test
 * @return true if an interrupt were forced
 * @return false if an interrupt were NOT forced
 */
bool aplic_idc_get_iforce(idcid_t idc_id);

/**
 * @brief Write a new value of threshold for a given IDC 
 * 
 * @param idc_id IDC to set a new value for threshold
 * @param new_th the new value of threshold. It is a value with IPRIOLEN. 
 * 
 * IPRIOLEN is in range 1 to 8 and is an implementation specific.
 * setting threshold for a nonzero value P, inetrrupts with priority of P of higher
 * DO NOT contribute to signaling interrupts to the hart.
 * If 0, all enabled interrupts can contibute to signaling inetrrupts to the hart.
 * Writting a value higher than APLIC minimum priority (maximum number)
 * takes no effect.
 */
void aplic_idc_set_ithreshold(idcid_t idc_id, uint32_t new_th);

/**
 * @brief Read the current value of threshold for a given IDC.
 * 
 * @param idc_id IDC to read the threshold value
 * @return uint32_t value with the threshold
 */
uint32_t aplic_idc_get_ithreshold(idcid_t idc_id);

/**
 * @brief Indicates the current highest-priority pending-and-enabled interrupt
 * targeted to this this hart.
 * 
 * @param idc_id IDC to read the highest-priority
 * @return uint32_t returns the interrupt identity and interrupt priority.
 * 
 * Formart:
 * 
 * +-----------+--------------------+------------------------+
 * | Bit-Field |        Name        |      Description       |
 * +-----------+--------------------+------------------------+
 * | 25:16     | Interrupt Identity | Equal to source number |
 * | 7:0       | Interrupt priority | Interrupt priority     |
 * +-----------+--------------------+------------------------+
 * 
 */
uint32_t aplic_idc_get_topi(idcid_t idc_id);

/**
 * @brief As the same value as topi. However, reading claimi has the side effect
 * of clearing the pending bit for the reported inetrrupt identity.
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