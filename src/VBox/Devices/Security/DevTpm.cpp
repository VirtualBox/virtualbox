/* $Id: DevTpm.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevTpm - Trusted Platform Module emulation.
 *
 * This emulation is based on the spec available under (as of 2021-08-02):
 *     https://trustedcomputinggroup.org/wp-content/uploads/PC-Client-Specific-Platform-TPM-Profile-for-TPM-2p0-v1p05p_r14_pub.pdf
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_TPM
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmtpmifs.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/uuid.h>

#include <iprt/formats/tpm.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The TPM saved state version. */
#define TPM_SAVED_STATE_VERSION                         1

/** Default vendor ID. */
#define TPM_VID_DEFAULT                                 0x1014
/** Default device ID. */
#define TPM_DID_DEFAULT                                 0x0001
/** Default revision ID. */
#define TPM_RID_DEFAULT                                 0x01
/** Maximum size of the data buffer in bytes. */
#define TPM_DATA_BUFFER_SIZE_MAX                        3968

/** The TPM MMIO base default as defined in chapter 5.2. */
#define TPM_MMIO_BASE_DEFAULT                           0xfed40000
/** The size of the TPM MMIO area. */
#define TPM_MMIO_SIZE                                   0x5000

/** Number of localities as mandated by the TPM spec. */
#define TPM_LOCALITY_COUNT                              5
/** Size of each locality in the TPM MMIO area (chapter 6.5.2).*/
#define TPM_LOCALITY_MMIO_SIZE                          0x1000

/** @name TPM locality register related defines for the FIFO interface.
 * @{ */
/** Ownership management for a particular locality. */
#define TPM_FIFO_LOCALITY_REG_ACCESS                         0x00
/** Indicates whether a dynamic OS has been established on this platform before.. */
# define TPM_FIFO_LOCALITY_REG_ACCESS_ESTABLISHMENT          RT_BIT(0)
/** On reads indicates whether the locality requests use of the TPM (1) or not or is already active locality (0),
 * writing a 1 requests the locality to be granted getting the active locality.. */
# define TPM_FIFO_LOCALITY_REG_ACCESS_REQUEST_USE            RT_BIT(1)
/** Indicates whether another locality is requesting usage of the TPM. */
# define TPM_FIFO_LOCALITY_REG_ACCESS_PENDING_REQUEST        RT_BIT(2)
/** Writing a 1 forces the TPM to give control to the locality if it has a higher priority. */
# define TPM_FIFO_LOCALITY_REG_ACCESS_SEIZE                  RT_BIT(3)
/** On reads indicates whether this locality has been seized by a higher locality (1) or not (0), writing a 1 clears this bit. */
# define TPM_FIFO_LOCALITY_REG_ACCESS_BEEN_SEIZED            RT_BIT(4)
/** On reads indicates whether this locality is active (1) or not (0), writing a 1 relinquishes control for this locality. */
# define TPM_FIFO_LOCALITY_REG_ACCESS_ACTIVE                 RT_BIT(5)
/** Set bit indicates whether all other bits in this register have valid data. */
# define TPM_FIFO_LOCALITY_REG_ACCESS_VALID                  RT_BIT(7)
/** Writable mask. */
# define TPM_FIFO_LOCALITY_REG_ACCESS_WR_MASK                0x3a

/** Interrupt enable register. */
#define TPM_FIFO_LOCALITY_REG_INT_ENABLE                     0x08
/** Data available interrupt enable bit. */
# define TPM_FIFO_LOCALITY_REG_INT_ENABLE_DATA_AVAIL         RT_BIT_32(0)
/** Status valid interrupt enable bit. */
# define TPM_FIFO_LOCALITY_REG_INT_ENABLE_STS_VALID          RT_BIT_32(1)
/** Locality change interrupt enable bit. */
# define TPM_FIFO_LOCALITY_REG_INT_ENABLE_LOCALITY_CHANGE    RT_BIT_32(2)
/** Interrupt polarity configuration. */
# define TPM_FIFO_LOCALITY_REG_INT_ENABLE_POLARITY_MASK      0x18
# define TPM_FIFO_LOCALITY_REG_INT_ENABLE_POLARITY_SHIFT     3
# define TPM_FIFO_LOCALITY_REG_INT_ENABLE_POLARITY_SET(a)    ((a) << TPM_FIFO_LOCALITY_REG_INT_POLARITY_SHIFT)
# define TPM_FIFO_LOCALITY_REG_INT_ENABLE_POLARITY_GET(a)    (((a) & TPM_FIFO_LOCALITY_REG_INT_POLARITY_MASK) >> TPM_FIFO_LOCALITY_REG_INT_POLARITY_SHIFT)
/** High level interrupt trigger. */
#  define TPM_FIFO_LOCALITY_REG_INT_ENABLE_POLARITY_HIGH     0
/** Low level interrupt trigger. */
#  define TPM_FIFO_LOCALITY_REG_INT_ENABLE_POLARITY_LOW      1
/** Rising edge interrupt trigger. */
#  define TPM_FIFO_LOCALITY_REG_INT_ENABLE_POLARITY_RISING   2
/** Falling edge interrupt trigger. */
#  define TPM_FIFO_LOCALITY_REG_INT_ENABLE_POLARITY_FALLING  3
/** Command ready enable bit. */
# define TPM_FIFO_LOCALITY_REG_INT_ENABLE_CMD_RDY            RT_BIT_32(7)
/** Global interrupt enable/disable bit. */
# define TPM_FIFO_LOCALITY_REG_INT_ENABLE_GLOBAL             RT_BIT_32(31)

/** Configured interrupt vector register. */
#define TPM_FIFO_LOCALITY_REG_INT_VEC                        0x0c

/** Interrupt status register. */
#define TPM_FIFO_LOCALITY_REG_INT_STS                        0x10
/** Data available interrupt occured bit, writing a 1 clears the bit. */
# define TPM_FIFO_LOCALITY_REG_INT_STS_DATA_AVAIL            RT_BIT_32(0)
/** Status valid interrupt occured bit, writing a 1 clears the bit. */
# define TPM_FIFO_LOCALITY_REG_INT_STS_STS_VALID             RT_BIT_32(1)
/** Locality change interrupt occured bit, writing a 1 clears the bit. */
# define TPM_FIFO_LOCALITY_REG_INT_STS_LOCALITY_CHANGE       RT_BIT_32(2)
/** Command ready occured bit, writing a 1 clears the bit. */
# define TPM_FIFO_LOCALITY_REG_INT_STS_CMD_RDY               RT_BIT_32(7)
/** Writable mask. */
# define TPM_FIFO_LOCALITY_REG_INT_STS_WR_MASK               UINT32_C(0x87)

/** Interfacce capabilities register. */
#define TPM_FIFO_LOCALITY_REG_IF_CAP                         0x14
/** Flag whether the TPM supports the data avilable interrupt. */
# define TPM_FIFO_LOCALITY_REG_IF_CAP_INT_DATA_AVAIL         RT_BIT(0)
/** Flag whether the TPM supports the status valid interrupt. */
# define TPM_FIFO_LOCALITY_REG_IF_CAP_INT_STS_VALID          RT_BIT(1)
/** Flag whether the TPM supports the data avilable interrupt. */
# define TPM_FIFO_LOCALITY_REG_IF_CAP_INT_LOCALITY_CHANGE    RT_BIT(2)
/** Flag whether the TPM supports high level interrupts. */
# define TPM_FIFO_LOCALITY_REG_IF_CAP_INT_LVL_HIGH           RT_BIT(3)
/** Flag whether the TPM supports low level interrupts. */
# define TPM_FIFO_LOCALITY_REG_IF_CAP_INT_LVL_LOW            RT_BIT(4)
/** Flag whether the TPM supports rising edge interrupts. */
# define TPM_FIFO_LOCALITY_REG_IF_CAP_INT_RISING_EDGE        RT_BIT(5)
/** Flag whether the TPM supports falling edge interrupts. */
# define TPM_FIFO_LOCALITY_REG_IF_CAP_INT_FALLING_EDGE       RT_BIT(6)
/** Flag whether the TPM supports the command ready interrupt. */
# define TPM_FIFO_LOCALITY_REG_IF_CAP_INT_CMD_RDY            RT_BIT(7)
/** Flag whether the busrt count field is static or dynamic. */
# define TPM_FIFO_LOCALITY_REG_IF_CAP_BURST_CNT_STATIC       RT_BIT(8)
/** Maximum transfer size support. */
# define TPM_FIFO_LOCALITY_REG_IF_CAP_DATA_XFER_SZ_MASK      0x600
# define TPM_FIFO_LOCALITY_REG_IF_CAP_DATA_XFER_SZ_SHIFT     9
# define TPM_FIFO_LOCALITY_REG_IF_CAP_DATA_XFER_SZ_SET(a)    ((a) << TPM_FIFO_LOCALITY_REG_IF_CAP_DATA_XFER_SZ_SHIFT)
/** Only legacy transfers supported. */
#  define TPM_FIFO_LOCALITY_REG_IF_CAP_DATA_XFER_SZ_LEGACY   0x0
/** 8B maximum transfer size. */
#  define TPM_FIFO_LOCALITY_REG_IF_CAP_DATA_XFER_SZ_8B       0x1
/** 32B maximum transfer size. */
#  define TPM_FIFO_LOCALITY_REG_IF_CAP_DATA_XFER_SZ_32B      0x2
/** 64B maximum transfer size. */
#  define TPM_FIFO_LOCALITY_REG_IF_CAP_DATA_XFER_SZ_64B      0x3
/** Interface version. */
# define TPM_FIFO_LOCALITY_REG_IF_CAP_IF_VERSION_MASK        UINT32_C(0x70000000)
# define TPM_FIFO_LOCALITY_REG_IF_CAP_IF_VERSION_SHIFT       28
# define TPM_FIFO_LOCALITY_REG_IF_CAP_IF_VERSION_SET(a)      ((a) << TPM_FIFO_LOCALITY_REG_IF_CAP_IF_VERSION_SHIFT)
/** Interface 1.21 or ealier. */
#  define TPM_FIFO_LOCALITY_REG_IF_CAP_IF_VERSION_IF_1_21    0
/** Interface 1.3. */
#  define TPM_FIFO_LOCALITY_REG_IF_CAP_IF_VERSION_IF_1_3     2
/** Interface 1.3 for TPM 2.0. */
#  define TPM_FIFO_LOCALITY_REG_IF_CAP_IF_VERSION_IF_1_3_TPM2    3

/** TPM status register. */
#define TPM_FIFO_LOCALITY_REG_STS                            0x18
/** Writing a 1 forces the TPM to re-send the response. */
# define TPM_FIFO_LOCALITY_REG_STS_RESPONSE_RETRY            RT_BIT_32(1)
/** Indicating whether the TPM has finished a self test. */
# define TPM_FIFO_LOCALITY_REG_STS_SELF_TEST_DONE            RT_BIT_32(2)
/** Flag indicating whether the TPM expects more data for the command. */
# define TPM_FIFO_LOCALITY_REG_STS_EXPECT                    RT_BIT_32(3)
/** Flag indicating whether the TPM has more response data available. */
# define TPM_FIFO_LOCALITY_REG_STS_DATA_AVAIL                RT_BIT_32(4)
/** Written by software to cause the TPM to execute a previously transfered command. */
# define TPM_FIFO_LOCALITY_REG_STS_TPM_GO                    RT_BIT_32(5)
/** On reads indicates whether the TPM is ready to receive a new command (1) or not (0),
 * a write of 1 causes the TPM to transition to this state. */
# define TPM_FIFO_LOCALITY_REG_STS_CMD_RDY                   RT_BIT_32(6)
/** Indicates whether the Expect and data available bits are valid. */
# define TPM_FIFO_LOCALITY_REG_STS_VALID                     RT_BIT_32(7)
/** Sets the burst count. */
# define TPM_FIFO_LOCALITY_REG_STS_BURST_CNT_MASK            UINT32_C(0xffff00)
# define TPM_FIFO_LOCALITY_REG_STS_BURST_CNT_SHIFT           UINT32_C(8)
# define TPM_FIFO_LOCALITY_REG_STS_BURST_CNT_SET(a)          ((a) << TPM_FIFO_LOCALITY_REG_STS_BURST_CNT_SHIFT)
/** Cancels the active command. */
# define TPM_FIFO_LOCALITY_REG_STS_CMD_CANCEL                RT_BIT_32(24)
/** Reset establishment bit. */
# define TPM_FIFO_LOCALITY_REG_STS_RST_ESTABLISHMENT         RT_BIT_32(25)
/** Sets the TPM family. */
# define TPM_FIFO_LOCALITY_REG_STS_TPM_FAMILY_MASK           UINT32_C(0x0c000000)
# define TPM_FIFO_LOCALITY_REG_STS_TPM_FAMILY_SHIFT          UINT32_C(26)
# define TPM_FIFO_LOCALITY_REG_STS_TPM_FAMILY_SET(a)         ((a) << TPM_FIFO_LOCALITY_REG_STS_TPM_FAMILY_SHIFT)
#  define TPM_FIFO_LOCALITY_REG_STS_TPM_FAMILY_1_2           UINT32_C(0)
#  define TPM_FIFO_LOCALITY_REG_STS_TPM_FAMILY_2_0           UINT32_C(1)


/** TPM end of HASH operation signal register for locality 4. */
#define TPM_FIFO_LOCALITY_REG_HASH_END                       0x20
/** Data FIFO read/write register. */
#define TPM_FIFO_LOCALITY_REG_DATA_FIFO                      0x24
/** TPM start of HASH operation signal register for locality 4. */
#define TPM_FIFO_LOCALITY_REG_HASH_START                     0x28

/** Locality interface ID register. */
#define TPM_FIFO_LOCALITY_REG_INTF_ID                         0x30
/** Interface type field. */
# define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_TYPE_MASK           UINT32_C(0xf)
# define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_TYPE_SHIFT          0
# define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_TYPE_SET(a)         ((a) << TPM_FIFO_LOCALITY_REG_INTF_ID_IF_TYPE_SHIFT)
/** FIFO interface as defined in PTP for TPM 2.0 is active. */
#  define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_TYPE_FIFO_TPM20    0x0
/** CRB interface is active. */
#  define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_TYPE_CRB           0x1
/** FIFO interface as defined in TIS 1.3 is active. */
#  define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_TYPE_TIS1_3        0xf
/** Interface type field. */
# define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_VERS_MASK           UINT32_C(0xf)
# define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_VERS_SHIFT          4
# define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_VERS_SET(a)         ((a) << TPM_FIFO_LOCALITY_REG_INTF_ID_IF_VERS_SHIFT)
/** FIFO interface for TPM 2.0 */
#  define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_VERS_FIFO          0
/** CRB interface version 0. */
#  define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_VERS_CRB           1
/** Only locality 0 is supported when clear, set if 5 localities are supported. */
# define TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_LOCALITY           RT_BIT(8)
/** Maximum transfer size support. */
# define TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_MASK   0x1800
# define TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_SHIFT  11
# define TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_SET(a) ((a) << TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_SHIFT)
/** Only legacy transfers supported. */
#  define TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_LEGACY 0x0
/** 8B maximum transfer size. */
#  define TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_8B   0x1
/** 32B maximum transfer size. */
#  define TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_32B  0x2
/** 64B maximum transfer size. */
#  define TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_64B  0x3
/** FIFO interface is supported and may be selected. */
# define TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_FIFO               RT_BIT(13)
/** CRB interface is supported and may be selected. */
# define TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_CRB                RT_BIT(14)
/** Interrupt polarity configuration. */
# define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_MASK            0x60000
# define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_SHIFT           17
# define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_SET(a)          ((a) << TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_SHIFT)
# define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_GET(a)          (((a) & TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_MASK) >> TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_SHIFT)
/** Selects the FIFO interface, takes effect on next _TPM_INIT. */
#  define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_FIFO           0
/** Selects the CRB interface, takes effect on next _TPM_INIT. */
#  define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_CRB            1
/** Locks the interface selector field and prevents further changes. */
# define TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_LOCK            RT_BIT(19)


/** Extended data FIFO read/write register. */
#define TPM_FIFO_LOCALITY_REG_XDATA_FIFO                     0x80
/** TPM device and vendor ID. */
#define TPM_FIFO_LOCALITY_REG_DID_VID                        0xf00
/** TPM revision ID. */
#define TPM_FIFO_LOCALITY_REG_RID                            0xf04
/** @} */


/** @name TPM locality register related defines for the CRB interface.
 * @{ */
/** Locality state register. */
#define TPM_CRB_LOCALITY_REG_STATE                           0x00
/** Indicates whether a dynamic OS has been established on this platform before.. */
# define TPM_CRB_LOCALITY_REG_ESTABLISHMENT                  RT_BIT(0)
/** Flag whether the host has a locality assigned (1) or not (0). */
# define TPM_CRB_LOCALITY_REG_STATE_LOC_ASSIGNED             RT_BIT(1)
/** Indicates the currently active locality. */
# define TPM_CRB_LOCALITY_REG_STATE_ACTIVE_LOC_MASK          UINT32_C(0x1c)
# define TPM_CRB_LOCALITY_REG_STATE_ACTIVE_LOC_SHIFT         2
# define TPM_CRB_LOCALITY_REG_STATE_ACTIVE_LOC_SET(a)        ((a) << TPM_CRB_LOCALITY_REG_STATE_ACTIVE_LOC_SHIFT)
/** Flag whether the register contains valid values. */
# define TPM_CRB_LOCALITY_REG_STATE_VALID                    RT_BIT(7)

/** Locality control register. */
#define TPM_CRB_LOCALITY_REG_CTRL                            0x08
/** Request TPM access from this locality. */
# define TPM_CRB_LOCALITY_REG_CTRL_REQ_ACCESS                RT_BIT(0)
/** Release TPM access from this locality. */
# define TPM_CRB_LOCALITY_REG_CTRL_RELINQUISH                RT_BIT(1)
/** Seize TPM access in favor of this locality if it has a higher priority. */
# define TPM_CRB_LOCALITY_REG_CTRL_SEIZE                     RT_BIT(2)
/** Resets the established bit if written from locality 3 or 4. */
# define TPM_CRB_LOCALITY_REG_CTRL_RST_ESTABLISHMENT         RT_BIT(3)

/** Locality status register. */
#define TPM_CRB_LOCALITY_REG_STS                             0x0c
/** Locality has been granted access to the TPM. */
# define TPM_CRB_LOCALITY_REG_STS_GRANTED                    RT_BIT(0)
/** A higher locality has seized the TPM from this locality. */
# define TPM_CRB_LOCALITY_REG_STS_SEIZED                     RT_BIT(1)

/** Locality interface ID register. */
#define TPM_CRB_LOCALITY_REG_INTF_ID                         0x30
/** Interface type field. */
# define TPM_CRB_LOCALITY_REG_INTF_ID_IF_TYPE_MASK           UINT32_C(0xf)
# define TPM_CRB_LOCALITY_REG_INTF_ID_IF_TYPE_SHIFT          0
# define TPM_CRB_LOCALITY_REG_INTF_ID_IF_TYPE_SET(a)         ((a) << TPM_CRB_LOCALITY_REG_INTF_ID_IF_TYPE_SHIFT)
/** FIFO interface as defined in PTP for TPM 2.0 is active. */
#  define TPM_CRB_LOCALITY_REG_INTF_ID_IF_TYPE_FIFO_TPM20    0x0
/** CRB interface is active. */
#  define TPM_CRB_LOCALITY_REG_INTF_ID_IF_TYPE_CRB           0x1
/** FIFO interface as defined in TIS 1.3 is active. */
#  define TPM_CRB_LOCALITY_REG_INTF_ID_IF_TYPE_TIS1_3        0xf
/** Interface type field. */
# define TPM_CRB_LOCALITY_REG_INTF_ID_IF_VERS_MASK           UINT32_C(0xf)
# define TPM_CRB_LOCALITY_REG_INTF_ID_IF_VERS_SHIFT          4
# define TPM_CRB_LOCALITY_REG_INTF_ID_IF_VERS_SET(a)         ((a) << TPM_CRB_LOCALITY_REG_INTF_ID_IF_VERS_SHIFT)
/** FIFO interface for TPM 2.0 */
#  define TPM_CRB_LOCALITY_REG_INTF_ID_IF_VERS_FIFO          0
/** CRB interface version 0. */
#  define TPM_CRB_LOCALITY_REG_INTF_ID_IF_VERS_CRB           1
/** Only locality 0 is supported when clear, set if 5 localities are supported. */
# define TPM_CRB_LOCALITY_REG_INTF_ID_CAP_LOCALITY           RT_BIT(8)
/** @todo TPM supports ... */
# define TPM_CRB_LOCALITY_REG_INTF_ID_CAP_CRB_IDLE_BYPASS    RT_BIT(9)
/** Maximum transfer size support. */
# define TPM_CRB_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_MASK   0x1800
# define TPM_CRB_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_SHIFT  11
# define TPM_CRB_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_SET(a) ((a) << TPM_CRB_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_SHIFT)
/** Only legacy transfers supported. */
#  define TPM_CRB_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_LEGACY 0x0
/** 8B maximum transfer size. */
#  define TPM_CRB_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_8B   0x1
/** 32B maximum transfer size. */
#  define TPM_CRB_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_32B  0x2
/** 64B maximum transfer size. */
#  define TPM_CRB_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_64B  0x3
/** FIFO interface is supported and may be selected. */
# define TPM_CRB_LOCALITY_REG_INTF_ID_CAP_FIFO               RT_BIT(13)
/** CRB interface is supported and may be selected. */
# define TPM_CRB_LOCALITY_REG_INTF_ID_CAP_CRB                RT_BIT(14)
/** Interrupt polarity configuration. */
# define TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_MASK            0x60000
# define TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_SHIFT           17
# define TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_SET(a)          ((a) << TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_SHIFT)
# define TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_GET(a)          (((a) & TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_MASK) >> TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_SHIFT)
/** Selects the FIFO interface, takes effect on next _TPM_INIT. */
#  define TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_FIFO           0
/** Selects the CRB interface, takes effect on next _TPM_INIT. */
#  define TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_CRB            1
/** Locks the interface selector field and prevents further changes. */
# define TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_LOCK            RT_BIT(19)
/** Revision ID field. */
# define TPM_CRB_LOCALITY_REG_INTF_ID_RID_SHIFT              17
# define TPM_CRB_LOCALITY_REG_INTF_ID_RID_SET(a)             ((uint64_t)(a) << TPM_CRB_LOCALITY_REG_INTF_ID_RID_SHIFT)
/** Vendor ID field. */
# define TPM_CRB_LOCALITY_REG_INTF_ID_VID_SHIFT              32
# define TPM_CRB_LOCALITY_REG_INTF_ID_VID_SET(a)             ((uint64_t)(a) << TPM_CRB_LOCALITY_REG_INTF_ID_VID_SHIFT)
/** Device ID field. */
# define TPM_CRB_LOCALITY_REG_INTF_ID_DID_SHIFT              48
# define TPM_CRB_LOCALITY_REG_INTF_ID_DID_SET(a)             ((uint64_t)(a) << TPM_CRB_LOCALITY_REG_INTF_ID_DID_SHIFT)

/** Locality CRB extension register (optional and locality 0 only). */
#define TPM_CRB_LOCALITY_REG_CTRL_EXT                        0x38

/** Locality CRB request register. */
#define TPM_CRB_LOCALITY_REG_CTRL_REQ                        0x40
/** The TPM should transition to the ready state to receive a new command. */
# define TPM_CRB_LOCALITY_REG_CTRL_REQ_CMD_RDY               RT_BIT(0)
/** The TPM should transition to the idle state. */
# define TPM_CRB_LOCALITY_REG_CTRL_REQ_IDLE                  RT_BIT(1)

/** Locality CRB status register. */
#define TPM_CRB_LOCALITY_REG_CTRL_STS                        0x44
/** This bit indicates that the TPM ran into a fatal error if set. */
# define TPM_CRB_LOCALITY_REG_CTRL_STS_TPM_FATAL_ERR         RT_BIT(0)
/** This bit indicates that the TPM is in the idle state. */
# define TPM_CRB_LOCALITY_REG_CTRL_STS_TPM_IDLE              RT_BIT(1)

/** Locality CRB cancel register. */
#define TPM_CRB_LOCALITY_REG_CTRL_CANCEL                     0x48
/** Locality CRB start register. */
#define TPM_CRB_LOCALITY_REG_CTRL_START                      0x4c

/** Locality interrupt enable register. */
#define TPM_CRB_LOCALITY_REG_INT_ENABLE                      0x50
/** Enable the "TPM has executed a reqeust and response is available" interrupt. */
# define TPM_CRB_LOCALITY_REG_INT_ENABLE_START               RT_BIT(0)
/** Enable the "TPM has transitioned to the command ready state" interrupt. */
# define TPM_CRB_LOCALITY_REG_INT_CMD_RDY                    RT_BIT(1)
/** Enable the "TPM has cleared the establishment flag" interrupt. */
# define TPM_CRB_LOCALITY_REG_INT_ESTABLISHMENT_CLR          RT_BIT(2)
/** Enable the "active locality has changed" interrupt. */
# define TPM_CRB_LOCALITY_REG_INT_LOC_CHANGED                RT_BIT(3)
/** Enables interrupts globally as defined by the individual bits in this register. */
# define TPM_CRB_LOCALITY_REG_INT_GLOBAL_ENABLE              RT_BIT(31)

/** Locality interrupt status register. */
#define TPM_CRB_LOCALITY_REG_INT_STS                         0x54
/** Indicates that the TPM as executed a command and the response is available for reading, writing a 1 clears the bit. */
# define TPM_CRB_LOCALITY_REG_INT_STS_START                  RT_BIT(0)
/** Indicates that the TPM has finished the transition to the ready state, writing a 1 clears this bit. */
# define TPM_CRB_LOCALITY_REG_INT_STS_CMD_RDY                RT_BIT(1)
/** Indicates that the TPM has cleared the establishment flag, writing a 1 clears this bit. */
# define TPM_CRB_LOCALITY_REG_INT_STS_ESTABLISHMENT_CLR      RT_BIT(2)
/** Indicates that a locality change has occurrec, writing a 1 clears this bit. */
# define TPM_CRB_LOCALITY_REG_INT_STS_LOC_CHANGED            RT_BIT(3)

/** Locality command buffer size register. */
#define TPM_CRB_LOCALITY_REG_CTRL_CMD_SZ                     0x58
/** Locality command buffer low address register. */
#define TPM_CRB_LOCALITY_REG_CTRL_CMD_LADDR                  0x5c
/** Locality command buffer low address register. */
#define TPM_CRB_LOCALITY_REG_CTRL_CMD_HADDR                  0x60
/** Locality response buffer size register. */
#define TPM_CRB_LOCALITY_REG_CTRL_RSP_SZ                     0x64
/** Locality response buffer address register. */
#define TPM_CRB_LOCALITY_REG_CTRL_RSP_ADDR                   0x68
/** Locality data buffer. */
#define TPM_CRB_LOCALITY_REG_DATA_BUFFER                     0x80
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Possible TPM states
 * (see chapter 5.6.12.1 Figure 3 State Transition Diagram).
 */
typedef enum DEVTPMSTATE
{
    /** Invalid state, do not use. */
    DEVTPMSTATE_INVALID = 0,
    /** Idle state. */
    DEVTPMSTATE_IDLE,
    /** Ready to accept command data. */
    DEVTPMSTATE_READY,
    /** Command data being transfered. */
    DEVTPMSTATE_CMD_RECEPTION,
    /** Command is being executed by the TPM. */
    DEVTPMSTATE_CMD_EXEC,
    /** Command has completed and data can be read. */
    DEVTPMSTATE_CMD_COMPLETION,
    /** Command is being canceled. */
    DEVTPMSTATE_CMD_CANCEL,
    /** TPM ran into a fatal error and is not operational. */
    DEVTPMSTATE_FATAL_ERROR,
    /** Last valid state (used for saved state sanity check). */
    DEVTPMSTATE_LAST_VALID = DEVTPMSTATE_FATAL_ERROR,
    /** 32bit hack. */
    DEVTPMSTATE_32BIT_HACK = 0x7fffffff
} DEVTPMSTATE;


/**
 * Locality state.
 */
typedef struct DEVTPMLOCALITY
{
    /** The interrupt enable register. */
    uint32_t                        uRegIntEn;
    /** The interrupt status register. */
    uint32_t                        uRegIntSts;
} DEVTPMLOCALITY;
/** Pointer to a locality state. */
typedef DEVTPMLOCALITY *PDEVTPMLOCALITY;
/** Pointer to a const locality state. */
typedef const DEVTPMLOCALITY *PCDEVTPMLOCALITY;


/**
 * Shared TPM device state.
 */
typedef struct DEVTPM
{
    /** Base MMIO address of the TPM device. */
    RTGCPHYS                        GCPhysMmio;
    /** The handle of the MMIO region. */
    IOMMMIOHANDLE                   hMmio;
    /** The handle for the ring-3 task. */
    PDMTASKHANDLE                   hTpmCmdTask;
    /** The vendor ID configured. */
    uint16_t                        uVenId;
    /** The device ID configured. */
    uint16_t                        uDevId;
    /** The revision ID configured. */
    uint8_t                         bRevId;
    /** The IRQ value. */
    uint8_t                         uIrq;
    /** Flag whether CRB access mode is used. */
    bool                            fCrb;
    /** Flag whether the TPM driver below supportes other localities than 0. */
    bool                            fLocChangeSup;
    /** Flag whether the establishment bit is set. */
    bool                            fEstablishmentSet;

    /** Currently selected locality. */
    uint8_t                         bLoc;
    /** States of the implemented localities. */
    DEVTPMLOCALITY                  aLoc[TPM_LOCALITY_COUNT];
    /** Bitmask of localities having requested access to the TPM. */
    uint32_t                        bmLocReqAcc;
    /** Bitmask of localities having been seized access from the TPM. */
    uint32_t                        bmLocSeizedAcc;
    /** The current state of the TPM. */
    DEVTPMSTATE                     enmState;
    /** The TPM version being emulated. */
    TPMVERSION                      enmTpmVers;

    /** Size of the command/response buffer. */
    uint32_t                        cbCmdResp;
    /** Offset into the Command/Response buffer. */
    uint32_t                        offCmdResp;
    /** Command/Response buffer. */
    uint8_t                         abCmdResp[TPM_DATA_BUFFER_SIZE_MAX];
} DEVTPM;
/** Pointer to the shared TPM device state. */
typedef DEVTPM *PDEVTPM;

/** The special no current locality selected value. */
#define TPM_NO_LOCALITY_SELECTED    0xff


/**
 * TPM device state for ring-3.
 */
typedef struct DEVTPMR3
{
    /** Pointer to the device instance. */
    PPDMDEVINS                      pDevIns;
    /** The base interface for LUN\#0. */
    PDMIBASE                        IBase;
    /** The base interface for LUN\#0. */
    PDMITPMPORT                     ITpmPort;
    /** The base interface below. */
    R3PTRTYPE(PPDMIBASE)            pDrvBase;
    /** The TPM connector interface below. */
    R3PTRTYPE(PPDMITPMCONNECTOR)    pDrvTpm;
} DEVTPMR3;
/** Pointer to the TPM device state for ring-3. */
typedef DEVTPMR3 *PDEVTPMR3;


/**
 * TPM device state for ring-0.
 */
typedef struct DEVTPMR0
{
    uint32_t                        u32Dummy;
} DEVTPMR0;
/** Pointer to the TPM device state for ring-0. */
typedef DEVTPMR0 *PDEVTPMR0;


/**
 * TPM device state for raw-mode.
 */
typedef struct DEVTPMRC
{
    uint32_t                        u32Dummy;
} DEVTPMRC;
/** Pointer to the TPM device state for raw-mode. */
typedef DEVTPMRC *PDEVTPMRC;

/** The TPM device state for the current context. */
typedef CTX_SUFF(DEVTPM) DEVTPMCC;
/** Pointer to the TPM device state for the current context. */
typedef CTX_SUFF(PDEVTPM) PDEVTPMCC;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IN_RING3
/**
 * SSM descriptor table for the TPM structure.
 */
static SSMFIELD const g_aTpmFields[] =
{
    SSMFIELD_ENTRY(DEVTPM, fEstablishmentSet),
    SSMFIELD_ENTRY(DEVTPM, bLoc),
    SSMFIELD_ENTRY(DEVTPM, aLoc[0].uRegIntEn),
    SSMFIELD_ENTRY(DEVTPM, aLoc[0].uRegIntSts),
    SSMFIELD_ENTRY(DEVTPM, aLoc[1].uRegIntEn),
    SSMFIELD_ENTRY(DEVTPM, aLoc[1].uRegIntSts),
    SSMFIELD_ENTRY(DEVTPM, aLoc[2].uRegIntEn),
    SSMFIELD_ENTRY(DEVTPM, aLoc[2].uRegIntSts),
    SSMFIELD_ENTRY(DEVTPM, aLoc[3].uRegIntEn),
    SSMFIELD_ENTRY(DEVTPM, aLoc[3].uRegIntSts),
    SSMFIELD_ENTRY(DEVTPM, aLoc[4].uRegIntEn),
    SSMFIELD_ENTRY(DEVTPM, aLoc[4].uRegIntSts),
    SSMFIELD_ENTRY(DEVTPM, bmLocReqAcc),
    SSMFIELD_ENTRY(DEVTPM, bmLocSeizedAcc),
    SSMFIELD_ENTRY(DEVTPM, enmState),
    SSMFIELD_ENTRY(DEVTPM, offCmdResp),
    SSMFIELD_ENTRY(DEVTPM, abCmdResp),
    SSMFIELD_ENTRY_TERM()
};
#endif


/**
 * Sets the IRQ line of the given device to the given state.
 *
 * @param   pDevIns             Pointer to the PDM device instance data.
 * @param   pThis               Pointer to the shared TPM device.
 * @param   iLvl                The interrupt level to set.
 */
DECLINLINE(void) tpmIrqReq(PPDMDEVINS pDevIns, PDEVTPM pThis, int iLvl)
{
    PDMDevHlpISASetIrqNoWait(pDevIns, pThis->uIrq, iLvl);
}


/**
 * Updates the IRQ status of the given locality.
 *
 * @param   pDevIns             Pointer to the PDM device instance data.
 * @param   pThis               Pointer to the shared TPM device.
 * @param   pLoc                The locality state.
 */
static void tpmLocIrqUpdate(PPDMDEVINS pDevIns, PDEVTPM pThis, PDEVTPMLOCALITY pLoc)
{
    if (   (pLoc->uRegIntEn & TPM_CRB_LOCALITY_REG_INT_GLOBAL_ENABLE) /* Aliases with TPM_FIFO_LOCALITY_REG_INT_ENABLE_GLOBAL */
        && (pLoc->uRegIntEn & pLoc->uRegIntSts))
        tpmIrqReq(pDevIns, pThis, 1);
    else
        tpmIrqReq(pDevIns, pThis, 0);
}


/**
 * Sets the interrupt status for the given locality, firing an interrupt if necessary.
 *
 * @param   pDevIns             Pointer to the PDM device instance data.
 * @param   pThis               Pointer to the shared TPM device.
 * @param   pLoc                The locality state.
 * @param   uSts                The interrupt status bit to set.
 */
static void tpmLocSetIntSts(PPDMDEVINS pDevIns, PDEVTPM pThis, PDEVTPMLOCALITY pLoc, uint32_t uSts)
{
    pLoc->uRegIntSts |= uSts;
    tpmLocIrqUpdate(pDevIns, pThis, pLoc);
}


/**
 * Selects the next locality which has requested access.
 *
 * @param   pDevIns             Pointer to the PDM device instance data.
 * @param   pThis               Pointer to the shared TPM device.
 */
static void tpmLocSelectNext(PPDMDEVINS pDevIns, PDEVTPM pThis)
{
    Assert(pThis->bmLocReqAcc);
    Assert(pThis->bLoc == TPM_NO_LOCALITY_SELECTED);
    pThis->bLoc = (uint8_t)ASMBitLastSetU32(pThis->bmLocReqAcc) - 1; /* Select one with highest priority. */

    tpmLocSetIntSts(pDevIns, pThis, &pThis->aLoc[pThis->bLoc], TPM_CRB_LOCALITY_REG_INT_STS_LOC_CHANGED);
}


/**
 * Returns the given locality being accessed from the given TPM MMIO offset.
 *
 * @returns Locality number.
 * @param   off                 The offset into the TPM MMIO region.
 */
DECLINLINE(uint8_t) tpmGetLocalityFromOffset(RTGCPHYS off)
{
    return off / TPM_LOCALITY_MMIO_SIZE;
}


/**
 * Returns the given register of a particular locality being accessed from the given TPM MMIO offset.
 *
 * @returns Register index being accessed.
 * @param   off                 The offset into the TPM MMIO region.
 */
DECLINLINE(uint32_t) tpmGetRegisterFromOffset(RTGCPHYS off)
{
    return off % TPM_LOCALITY_MMIO_SIZE;
}


/**
 * Read from a FIFO interface register.
 *
 * @returns VBox strict status code.
 * @param   pDevIns             Pointer to the PDM device instance data.
 * @param   pThis               Pointer to the shared TPM device.
 * @param   pLoc                The locality state being read from.
 * @param   bLoc                The locality index.
 * @param   uReg                The register offset being accessed.
 * @param   pu64                Where to store the read data.
 * @param   cb                  Number of bytes to read.
 */
static VBOXSTRICTRC tpmMmioFifoRead(PPDMDEVINS pDevIns, PDEVTPM pThis, PDEVTPMLOCALITY pLoc,
                                    uint8_t bLoc, uint32_t uReg, uint64_t *pu64, size_t cb)
{
    RT_NOREF(pDevIns);
    VBOXSTRICTRC rc = VINF_SUCCESS;

    /* Special path for the data buffer. */
    if (   (   (   uReg >= TPM_FIFO_LOCALITY_REG_DATA_FIFO
               && uReg < TPM_FIFO_LOCALITY_REG_DATA_FIFO + sizeof(uint32_t))
            || (   uReg >= TPM_FIFO_LOCALITY_REG_XDATA_FIFO
                && uReg < TPM_FIFO_LOCALITY_REG_XDATA_FIFO + sizeof(uint32_t)))
        && bLoc == pThis->bLoc
        && pThis->enmState == DEVTPMSTATE_CMD_COMPLETION)
    {
        if (pThis->offCmdResp <= pThis->cbCmdResp - cb)
        {
            memcpy(pu64, &pThis->abCmdResp[pThis->offCmdResp], cb);
            pThis->offCmdResp += (uint32_t)cb;
        }
        else
            memset(pu64, 0xff, cb);
        return VINF_SUCCESS;
    }

    uint64_t u64;
    switch (uReg)
    {
        case TPM_FIFO_LOCALITY_REG_ACCESS:
            u64 = TPM_FIFO_LOCALITY_REG_ACCESS_VALID;
            if (pThis->bLoc == bLoc)
                u64 |= TPM_FIFO_LOCALITY_REG_ACCESS_ACTIVE;
            if (pThis->bmLocSeizedAcc & RT_BIT_32(bLoc))
                u64 |= TPM_FIFO_LOCALITY_REG_ACCESS_BEEN_SEIZED;
            if (pThis->bmLocReqAcc & ~RT_BIT_32(bLoc))
                u64 |= TPM_FIFO_LOCALITY_REG_ACCESS_PENDING_REQUEST;
            if (   pThis->bLoc != bLoc
                && pThis->bmLocReqAcc & RT_BIT_32(bLoc))
                u64 |= TPM_FIFO_LOCALITY_REG_ACCESS_REQUEST_USE;
            if (pThis->fEstablishmentSet)
                u64 |= TPM_FIFO_LOCALITY_REG_ACCESS_ESTABLISHMENT;
            break;
        case TPM_FIFO_LOCALITY_REG_INT_ENABLE:
            u64 = pLoc->uRegIntEn;
            break;
        case TPM_FIFO_LOCALITY_REG_INT_VEC:
            u64 = pThis->uIrq;
            break;
        case TPM_FIFO_LOCALITY_REG_INT_STS:
            u64 = pLoc->uRegIntSts;
            break;
        case TPM_FIFO_LOCALITY_REG_IF_CAP:
            u64 =   TPM_FIFO_LOCALITY_REG_IF_CAP_INT_DATA_AVAIL
                  | TPM_FIFO_LOCALITY_REG_IF_CAP_INT_STS_VALID
                  | TPM_FIFO_LOCALITY_REG_IF_CAP_INT_LOCALITY_CHANGE
                  | TPM_FIFO_LOCALITY_REG_IF_CAP_INT_LVL_LOW
                  | TPM_FIFO_LOCALITY_REG_IF_CAP_INT_CMD_RDY
                  | TPM_FIFO_LOCALITY_REG_IF_CAP_DATA_XFER_SZ_SET(TPM_FIFO_LOCALITY_REG_IF_CAP_DATA_XFER_SZ_64B); /** @todo Make some of them configurable? */
            if (pThis->enmTpmVers == TPMVERSION_1_2)
                u64 |= TPM_FIFO_LOCALITY_REG_IF_CAP_IF_VERSION_SET(TPM_FIFO_LOCALITY_REG_IF_CAP_IF_VERSION_IF_1_3);
            else
                u64 |= TPM_FIFO_LOCALITY_REG_IF_CAP_IF_VERSION_SET(TPM_FIFO_LOCALITY_REG_IF_CAP_IF_VERSION_IF_1_3_TPM2);
            break;
        case TPM_FIFO_LOCALITY_REG_STS:
            if (bLoc != pThis->bLoc)
            {
                u64 = UINT64_MAX;
                break;
            }

            u64 =   TPM_FIFO_LOCALITY_REG_STS_TPM_FAMILY_SET(  pThis->enmTpmVers == TPMVERSION_1_2
                                                             ? TPM_FIFO_LOCALITY_REG_STS_TPM_FAMILY_1_2
                                                             : TPM_FIFO_LOCALITY_REG_STS_TPM_FAMILY_2_0)
                  | TPM_FIFO_LOCALITY_REG_STS_BURST_CNT_SET(_1K)
                  | TPM_FIFO_LOCALITY_REG_STS_VALID;
            if (pThis->enmState == DEVTPMSTATE_READY)
                u64 |= TPM_FIFO_LOCALITY_REG_STS_CMD_RDY;
            else if (pThis->enmState == DEVTPMSTATE_CMD_RECEPTION) /* When in the command reception state check whether all of the command data has been received. */
            {
                if (   pThis->offCmdResp < sizeof(TPMREQHDR)
                    || pThis->offCmdResp < RTTpmReqGetSz((PCTPMREQHDR)&pThis->abCmdResp[0]))
                    u64 |= TPM_FIFO_LOCALITY_REG_STS_EXPECT;
            }
            else if (pThis->enmState == DEVTPMSTATE_CMD_COMPLETION) /* Check whether there is more response data available. */
            {
                if (pThis->offCmdResp < RTTpmRespGetSz((PCTPMRESPHDR)&pThis->abCmdResp[0]))
                    u64 |= TPM_FIFO_LOCALITY_REG_STS_DATA_AVAIL;
            }
            break;
        case TPM_FIFO_LOCALITY_REG_INTF_ID:
            u64 =   TPM_FIFO_LOCALITY_REG_INTF_ID_IF_VERS_SET(TPM_FIFO_LOCALITY_REG_INTF_ID_IF_VERS_FIFO)
                  | TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_SET(TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_64B)
                  | TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_GET(TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_FIFO)
                  | TPM_FIFO_LOCALITY_REG_INTF_ID_IF_SEL_LOCK
                  | TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_FIFO;
            if (pThis->enmTpmVers == TPMVERSION_1_2)
                u64 |= TPM_FIFO_LOCALITY_REG_INTF_ID_IF_TYPE_SET(TPM_FIFO_LOCALITY_REG_INTF_ID_IF_TYPE_TIS1_3);
            else
                u64 |= TPM_FIFO_LOCALITY_REG_INTF_ID_IF_TYPE_SET(TPM_FIFO_LOCALITY_REG_INTF_ID_IF_TYPE_FIFO_TPM20);

            if (pThis->fLocChangeSup) /* Only advertise the locality capability if the driver below supports it. */
                u64 |= TPM_FIFO_LOCALITY_REG_INTF_ID_CAP_LOCALITY;
            break;
        case TPM_FIFO_LOCALITY_REG_DID_VID:
            u64 = RT_H2BE_U32(RT_MAKE_U32(pThis->uVenId, pThis->uDevId));
            break;
        case TPM_FIFO_LOCALITY_REG_RID:
            u64 = pThis->bRevId;
            break;
        default: /* Return ~0. */
            u64 = UINT64_MAX;
            break;
    }

    *pu64 = u64;

    return rc;
}


/**
 * Read to a FIFO interface register.
 *
 * @returns VBox strict status code.
 * @param   pDevIns             Pointer to the PDM device instance data.
 * @param   pThis               Pointer to the shared TPM device.
 * @param   pLoc                The locality state being written to.
 * @param   bLoc                The locality index.
 * @param   uReg                The register offset being accessed.
 * @param   u64                 The value to write.
 * @param   cb                  Number of bytes to write.
 */
static VBOXSTRICTRC tpmMmioFifoWrite(PPDMDEVINS pDevIns, PDEVTPM pThis, PDEVTPMLOCALITY pLoc,
                                     uint8_t bLoc, uint32_t uReg, uint64_t u64, size_t cb)
{
#ifdef IN_RING3
    PDEVTPMR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVTPMR3);
#endif

    /* Special path for the data buffer. */
    if (   (   (   uReg >= TPM_FIFO_LOCALITY_REG_DATA_FIFO
               && uReg < TPM_FIFO_LOCALITY_REG_DATA_FIFO + sizeof(uint32_t))
            || (   uReg >= TPM_FIFO_LOCALITY_REG_XDATA_FIFO
                && uReg < TPM_FIFO_LOCALITY_REG_XDATA_FIFO + sizeof(uint32_t)))
        && bLoc == pThis->bLoc
        && (   pThis->enmState == DEVTPMSTATE_READY
            || pThis->enmState == DEVTPMSTATE_CMD_RECEPTION))
    {
        pThis->enmState = DEVTPMSTATE_CMD_RECEPTION;
        if (pThis->offCmdResp <=  pThis->cbCmdResp - cb)
        {
            memcpy(&pThis->abCmdResp[pThis->offCmdResp], &u64, cb);
            pThis->offCmdResp += (uint32_t)cb;
        }
        return VINF_SUCCESS;
    }

    VBOXSTRICTRC rc = VINF_SUCCESS;
    uint32_t u32 = (uint32_t)u64;

    switch (uReg)
    {
        case TPM_FIFO_LOCALITY_REG_ACCESS:
            u32 &= TPM_FIFO_LOCALITY_REG_ACCESS_WR_MASK;
             /*
              * Chapter 5.6.11, 2 states that writing to this register with more than one
              * bit set to '1' is vendor specific, we decide to ignore such writes to make the logic
              * below simpler.
              */
            if (!RT_IS_POWER_OF_TWO(u32))
                break;

            /* Seize access only if this locality has a higher priority than the currently selected one. */
            if (   (u32 & TPM_FIFO_LOCALITY_REG_ACCESS_SEIZE)
                && pThis->bLoc != TPM_NO_LOCALITY_SELECTED
                && bLoc > pThis->bLoc)
            {
                pThis->bmLocSeizedAcc |= RT_BIT_32(pThis->bLoc);
                /** @todo Abort command. */
                pThis->bLoc = bLoc;
            }

            if (   (u64 & TPM_FIFO_LOCALITY_REG_ACCESS_REQUEST_USE)
                && !(pThis->bmLocReqAcc & RT_BIT_32(bLoc)))
            {
                pThis->bmLocReqAcc |= RT_BIT_32(bLoc);
                if (pThis->bLoc == TPM_NO_LOCALITY_SELECTED)
                {
                    pThis->bLoc = bLoc; /* Doesn't fire an interrupt. */
                    pThis->bmLocSeizedAcc &= ~RT_BIT_32(bLoc);
                }
            }

            if (   (u64 & TPM_FIFO_LOCALITY_REG_ACCESS_ACTIVE)
                && (pThis->bmLocReqAcc & RT_BIT_32(bLoc)))
            {
                pThis->bmLocReqAcc &= ~RT_BIT_32(bLoc);
                if (pThis->bLoc == bLoc)
                {
                    pThis->bLoc = TPM_NO_LOCALITY_SELECTED;
                    if (pThis->bmLocReqAcc)
                        tpmLocSelectNext(pDevIns, pThis); /* Select the next locality. */
                }
            }
            break;
        case TPM_FIFO_LOCALITY_REG_INT_ENABLE:
            if (bLoc != pThis->bLoc)
                break;
            pLoc->uRegIntEn = u32;
            tpmLocIrqUpdate(pDevIns, pThis, pLoc);
            break;
        case TPM_FIFO_LOCALITY_REG_INT_STS:
            if (bLoc != pThis->bLoc)
                break;
            pLoc->uRegIntSts &= ~(u32 & TPM_FIFO_LOCALITY_REG_INT_STS_WR_MASK);
            tpmLocIrqUpdate(pDevIns, pThis, pLoc);
            break;
        case TPM_FIFO_LOCALITY_REG_STS:
            /*
             * Writes are ignored completely if the locality being accessed is not the
             * current active one or if the value has multiple bits set (not a power of two),
             * see chapter 5.6.12.1.
             */
            if (   bLoc != pThis->bLoc
                || !RT_IS_POWER_OF_TWO(u64))
                break;

            if (   (u64 & TPM_FIFO_LOCALITY_REG_STS_CMD_RDY)
                && (   pThis->enmState == DEVTPMSTATE_IDLE
                    || pThis->enmState == DEVTPMSTATE_CMD_COMPLETION))
            {
                pThis->enmState   = DEVTPMSTATE_READY;
                pThis->offCmdResp = 0;
                tpmLocSetIntSts(pDevIns, pThis, pLoc, TPM_FIFO_LOCALITY_REG_INT_STS_CMD_RDY);
            }

            if (   (u64 & TPM_FIFO_LOCALITY_REG_STS_TPM_GO)
                && pThis->enmState == DEVTPMSTATE_CMD_RECEPTION)
            {
                pThis->enmState = DEVTPMSTATE_CMD_EXEC;
                rc = PDMDevHlpTaskTrigger(pDevIns, pThis->hTpmCmdTask);
            }

            if (   (u64 & TPM_FIFO_LOCALITY_REG_STS_RST_ESTABLISHMENT)
                && pThis->bLoc >= 3
                && (   pThis->enmState == DEVTPMSTATE_IDLE
                    || pThis->enmState == DEVTPMSTATE_CMD_COMPLETION))
            {
#ifndef IN_RING3
                rc = VINF_IOM_R3_MMIO_WRITE;
                break;
#else
                if (pThisCC->pDrvTpm)
                {
                    int rc2 = pThisCC->pDrvTpm->pfnResetEstablishedFlag(pThisCC->pDrvTpm, pThis->bLoc);
                    if (RT_SUCCESS(rc2))
                        pThis->fEstablishmentSet = false;
                    else
                        pThis->enmState = DEVTPMSTATE_FATAL_ERROR;
                }
                else
                    pThis->fEstablishmentSet = false;
#endif
            }

            if (   (u64 & TPM_FIFO_LOCALITY_REG_STS_CMD_CANCEL)
                && pThis->enmState == DEVTPMSTATE_CMD_EXEC)
            {
#ifndef IN_RING3
                rc = VINF_IOM_R3_MMIO_WRITE;
                break;
#else
                if (pThisCC->pDrvTpm)
                {
                    pThis->enmState = DEVTPMSTATE_CMD_CANCEL;
                    int rc2 = pThisCC->pDrvTpm->pfnCmdCancel(pThisCC->pDrvTpm);
                    if (RT_FAILURE(rc2))
                        pThis->enmState = DEVTPMSTATE_FATAL_ERROR;
                }
#endif
            }

            break;
        case TPM_FIFO_LOCALITY_REG_INT_VEC:
        case TPM_FIFO_LOCALITY_REG_IF_CAP:
        case TPM_FIFO_LOCALITY_REG_DID_VID:
        case TPM_FIFO_LOCALITY_REG_RID:
        default: /* Ignore. */
            break;
    }

    return rc;
}


/**
 * Read from a CRB interface register.
 *
 * @returns VBox strict status code.
 * @param   pDevIns             Pointer to the PDM device instance data.
 * @param   pThis               Pointer to the shared TPM device.
 * @param   pLoc                The locality state being read from.
 * @param   bLoc                The locality index.
 * @param   uReg                The register offset being accessed.
 * @param   pu64                Where to store the read data.
 * @param   cb                  Size of the read in bytes.
 */
static VBOXSTRICTRC tpmMmioCrbRead(PPDMDEVINS pDevIns, PDEVTPM pThis, PDEVTPMLOCALITY pLoc,
                                   uint8_t bLoc, uint32_t uReg, uint64_t *pu64, size_t cb)
{
    RT_NOREF(pDevIns);

    /* Special path for the data buffer. */
    if (   uReg >= TPM_CRB_LOCALITY_REG_DATA_BUFFER
        && uReg < TPM_CRB_LOCALITY_REG_DATA_BUFFER + pThis->cbCmdResp
        && bLoc == pThis->bLoc
        && pThis->enmState == DEVTPMSTATE_CMD_COMPLETION)
    {
        memcpy(pu64, &pThis->abCmdResp[uReg - TPM_CRB_LOCALITY_REG_DATA_BUFFER], cb);
        return VINF_SUCCESS;
    }

    VBOXSTRICTRC rc = VINF_SUCCESS;
    uint64_t u64 = UINT64_MAX;
    switch (uReg)
    {
        case TPM_CRB_LOCALITY_REG_STATE:
            u64 =   TPM_CRB_LOCALITY_REG_STATE_VALID
                  | (  pThis->bLoc != TPM_NO_LOCALITY_SELECTED
                     ? TPM_CRB_LOCALITY_REG_STATE_ACTIVE_LOC_SET(pThis->bLoc) | TPM_CRB_LOCALITY_REG_STATE_LOC_ASSIGNED
                     : TPM_CRB_LOCALITY_REG_STATE_ACTIVE_LOC_SET(0));
            if (pThis->fEstablishmentSet)
                u64 |= TPM_CRB_LOCALITY_REG_ESTABLISHMENT;
            break;
        case TPM_CRB_LOCALITY_REG_STS:
            u64 =   pThis->bLoc == bLoc
                  ? TPM_CRB_LOCALITY_REG_STS_GRANTED
                  : 0;
            u64 |=   pThis->bmLocSeizedAcc & RT_BIT_32(bLoc)
                   ? TPM_CRB_LOCALITY_REG_STS_SEIZED
                   : 0;
            break;
        case TPM_CRB_LOCALITY_REG_INTF_ID:
            u64 =   TPM_CRB_LOCALITY_REG_INTF_ID_IF_TYPE_SET(TPM_CRB_LOCALITY_REG_INTF_ID_IF_TYPE_CRB)
                  | TPM_CRB_LOCALITY_REG_INTF_ID_IF_VERS_SET(TPM_CRB_LOCALITY_REG_INTF_ID_IF_VERS_CRB)
                  | TPM_CRB_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_SET(TPM_CRB_LOCALITY_REG_INTF_ID_CAP_DATA_XFER_SZ_64B)
                  | TPM_CRB_LOCALITY_REG_INTF_ID_CAP_CRB
                  | TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_GET(TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_CRB)
                  | TPM_CRB_LOCALITY_REG_INTF_ID_IF_SEL_LOCK
                  | TPM_CRB_LOCALITY_REG_INTF_ID_RID_SET(pThis->bRevId)
                  | TPM_CRB_LOCALITY_REG_INTF_ID_VID_SET(pThis->uVenId)
                  | TPM_CRB_LOCALITY_REG_INTF_ID_DID_SET(pThis->uDevId);

            if (pThis->fLocChangeSup) /* Only advertise the locality capability if the driver below supports it. */
                u64 |= TPM_CRB_LOCALITY_REG_INTF_ID_CAP_LOCALITY;

            break;
        case TPM_CRB_LOCALITY_REG_CTRL_REQ:
            if (bLoc != pThis->bLoc)
                break;
            /*
             * Command ready and go idle are always 0 upon read
             * as we don't need time to transition to this state
             * when written by the guest.
             */
            u64 = 0;
            break;
        case TPM_CRB_LOCALITY_REG_CTRL_STS:
            if (bLoc != pThis->bLoc)
                break;
            if (pThis->enmState == DEVTPMSTATE_FATAL_ERROR)
                u64 = TPM_CRB_LOCALITY_REG_CTRL_STS_TPM_FATAL_ERR;
            else if (pThis->enmState == DEVTPMSTATE_IDLE)
                u64 = TPM_CRB_LOCALITY_REG_CTRL_STS_TPM_IDLE;
            else
                u64 = 0;
            break;
        case TPM_CRB_LOCALITY_REG_CTRL_CANCEL:
            if (bLoc != pThis->bLoc)
                break;
            if (pThis->enmState == DEVTPMSTATE_CMD_CANCEL)
                u64 = 0x1;
            else
                u64 = 0;
            break;
        case TPM_CRB_LOCALITY_REG_CTRL_START:
            if (bLoc != pThis->bLoc)
                break;
            if (pThis->enmState == DEVTPMSTATE_CMD_EXEC)
                u64 = 0x1;
            else
                u64 = 0;
            break;
        case TPM_CRB_LOCALITY_REG_INT_ENABLE:
            u64 = pLoc->uRegIntEn;
            break;
        case TPM_CRB_LOCALITY_REG_INT_STS:
            u64 = pLoc->uRegIntSts;
            break;
        case TPM_CRB_LOCALITY_REG_CTRL_CMD_LADDR:
            u64 = pThis->GCPhysMmio + (bLoc * TPM_LOCALITY_MMIO_SIZE) + TPM_CRB_LOCALITY_REG_DATA_BUFFER;
            break;
        case TPM_CRB_LOCALITY_REG_CTRL_CMD_HADDR:
            u64 = (pThis->GCPhysMmio + (bLoc * TPM_LOCALITY_MMIO_SIZE) + TPM_CRB_LOCALITY_REG_DATA_BUFFER) >> 32;
            break;
        case TPM_CRB_LOCALITY_REG_CTRL_CMD_SZ:
        case TPM_CRB_LOCALITY_REG_CTRL_RSP_SZ:
            u64 = pThis->cbCmdResp;
            break;
        case TPM_CRB_LOCALITY_REG_CTRL_RSP_ADDR:
            u64 = pThis->GCPhysMmio + (bLoc * TPM_LOCALITY_MMIO_SIZE) + TPM_CRB_LOCALITY_REG_DATA_BUFFER;
            break;
        case TPM_CRB_LOCALITY_REG_CTRL: /* Writeonly */
            u64 = 0;
            break;
        case TPM_CRB_LOCALITY_REG_CTRL_EXT:
        default:
            break; /* Return ~0 */
    }

    *pu64 = u64;
    return rc;
}


/**
 * Read to a CRB interface register.
 *
 * @returns VBox strict status code.
 * @param   pDevIns             Pointer to the PDM device instance data.
 * @param   pThis               Pointer to the shared TPM device.
 * @param   pLoc                The locality state being written to.
 * @param   bLoc                The locality index.
 * @param   uReg                The register offset being accessed.
 * @param   u64                 The value to write.
 * @param   cb                  Size of the write in bytes.
 */
static VBOXSTRICTRC tpmMmioCrbWrite(PPDMDEVINS pDevIns, PDEVTPM pThis, PDEVTPMLOCALITY pLoc,
                                    uint8_t bLoc, uint32_t uReg, uint64_t u64, size_t cb)
{
#ifdef IN_RING3
    PDEVTPMR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVTPMR3);
#endif

    VBOXSTRICTRC rc = VINF_SUCCESS;
    uint32_t u32 = (uint32_t)u64;

    /* Special path for the data buffer. */
    if (   uReg >= TPM_CRB_LOCALITY_REG_DATA_BUFFER
        && uReg < TPM_CRB_LOCALITY_REG_DATA_BUFFER + pThis->cbCmdResp
        && bLoc == pThis->bLoc
        && (   pThis->enmState == DEVTPMSTATE_READY
            || pThis->enmState == DEVTPMSTATE_CMD_RECEPTION))
    {
        pThis->enmState = DEVTPMSTATE_CMD_RECEPTION;
        memcpy(&pThis->abCmdResp[uReg - TPM_CRB_LOCALITY_REG_DATA_BUFFER], &u64, cb);
        return VINF_SUCCESS;
    }

    switch (uReg)
    {
        case TPM_CRB_LOCALITY_REG_CTRL:
        {
            /* See chapter 6.5.3.2.2.1. */
            if (   (u64 & TPM_CRB_LOCALITY_REG_CTRL_RST_ESTABLISHMENT)
                && pThis->bLoc >= 3
                && (   pThis->enmState == DEVTPMSTATE_IDLE
                    || pThis->enmState == DEVTPMSTATE_CMD_COMPLETION))
            {
#ifndef IN_RING3
                rc = VINF_IOM_R3_MMIO_WRITE;
                break;
#else
                if (pThisCC->pDrvTpm)
                {
                    int rc2 = pThisCC->pDrvTpm->pfnResetEstablishedFlag(pThisCC->pDrvTpm, pThis->bLoc);
                    if (RT_SUCCESS(rc2))
                        pThis->fEstablishmentSet = false;
                    else
                        pThis->enmState = DEVTPMSTATE_FATAL_ERROR;
                }
                else
                    pThis->fEstablishmentSet = false;
#endif
            }

            /*
             * The following three checks should be mutually exclusive as the writer shouldn't
             * request, relinquish and seize access in the same write.
             */
            /* Seize access only if this locality has a higher priority than the currently selected one. */
            if (   (u64 & TPM_CRB_LOCALITY_REG_CTRL_SEIZE)
                && pThis->bLoc != TPM_NO_LOCALITY_SELECTED
                && bLoc > pThis->bLoc)
            {
                if (pThis->enmState == DEVTPMSTATE_CMD_EXEC)
                {
#ifndef IN_RING3
                    rc = VINF_IOM_R3_MMIO_WRITE;
                    break;
#else
                    pThis->enmState = DEVTPMSTATE_CMD_CANCEL;
                    if (pThisCC->pDrvTpm)
                    {
                        int rc2 = pThisCC->pDrvTpm->pfnCmdCancel(pThisCC->pDrvTpm);
                        if (RT_FAILURE(rc2))
                            pThis->enmState = DEVTPMSTATE_FATAL_ERROR;
                        else
                        {
                            pThis->enmState = DEVTPMSTATE_CMD_COMPLETION;
                            tpmLocSetIntSts(pDevIns, pThis, pLoc, TPM_CRB_LOCALITY_REG_INT_STS_START);
                        }
                    }
#endif
                }

                pThis->bmLocSeizedAcc |= RT_BIT_32(pThis->bLoc);
                pThis->bLoc = bLoc;
            }

            if (   (u64 & TPM_CRB_LOCALITY_REG_CTRL_REQ_ACCESS)
                && !(pThis->bmLocReqAcc & RT_BIT_32(bLoc)))
            {
                pThis->bmLocReqAcc |= RT_BIT_32(bLoc);
                if (pThis->bLoc == TPM_NO_LOCALITY_SELECTED)
                {
                    pThis->bLoc = bLoc; /* Doesn't fire an interrupt. */
                    pThis->bmLocSeizedAcc &= ~RT_BIT_32(bLoc);
                }
            }

            if (   (u64 & TPM_CRB_LOCALITY_REG_CTRL_RELINQUISH)
                && (pThis->bmLocReqAcc & RT_BIT_32(bLoc)))
            {
                pThis->bmLocReqAcc &= ~RT_BIT_32(bLoc);
                if (pThis->bLoc == bLoc)
                {
                    pThis->bLoc = TPM_NO_LOCALITY_SELECTED;
                    if (pThis->bmLocReqAcc)
                        tpmLocSelectNext(pDevIns, pThis); /* Select the next locality. */
                }
            }
            break;
        }
        case TPM_CRB_LOCALITY_REG_CTRL_REQ:
            if (   bLoc != pThis->bLoc
                || !RT_IS_POWER_OF_TWO(u32)) /* Ignore if multiple bits are set. */
                break;
            if (   (u32 & TPM_CRB_LOCALITY_REG_CTRL_REQ_CMD_RDY)
                && (   pThis->enmState == DEVTPMSTATE_IDLE
                    || pThis->enmState == DEVTPMSTATE_CMD_COMPLETION))
            {
                pThis->enmState = DEVTPMSTATE_READY;
                tpmLocSetIntSts(pDevIns, pThis, pLoc, TPM_CRB_LOCALITY_REG_INT_STS_CMD_RDY);
            }
            else if (   (u32 & TPM_CRB_LOCALITY_REG_CTRL_REQ_IDLE)
                     && pThis->enmState != DEVTPMSTATE_CMD_EXEC)
            {
                /* Invalidate the command/response buffer. */
                RT_ZERO(pThis->abCmdResp);
                pThis->offCmdResp = 0;
                pThis->enmState   = DEVTPMSTATE_IDLE;
            }
            break;
        case TPM_CRB_LOCALITY_REG_CTRL_CANCEL:
            if (bLoc != pThis->bLoc)
                break;
            if (   pThis->enmState == DEVTPMSTATE_CMD_EXEC
                && u32 == 0x1)
            {
#ifndef IN_RING3
                rc = VINF_IOM_R3_MMIO_WRITE;
                break;
#else
                pThis->enmState = DEVTPMSTATE_CMD_CANCEL;
                if (pThisCC->pDrvTpm)
                {
                    int rc2 = pThisCC->pDrvTpm->pfnCmdCancel(pThisCC->pDrvTpm);
                    if (RT_FAILURE(rc2))
                        pThis->enmState = DEVTPMSTATE_FATAL_ERROR;
                    else
                    {
                        pThis->enmState = DEVTPMSTATE_CMD_COMPLETION;
                        tpmLocSetIntSts(pDevIns, pThis, pLoc, TPM_CRB_LOCALITY_REG_INT_STS_START);
                    }
                }
#endif
            }
            break;
        case TPM_CRB_LOCALITY_REG_CTRL_START:
            if (bLoc != pThis->bLoc)
                break;
            if (   pThis->enmState == DEVTPMSTATE_CMD_RECEPTION
                && u32 == 0x1)
            {
                pThis->enmState = DEVTPMSTATE_CMD_EXEC;
                rc = PDMDevHlpTaskTrigger(pDevIns, pThis->hTpmCmdTask);
            }
            break;
        case TPM_CRB_LOCALITY_REG_INT_ENABLE:
            pLoc->uRegIntEn = u32;
            tpmLocIrqUpdate(pDevIns, pThis, pLoc);
            break;
        case TPM_CRB_LOCALITY_REG_INT_STS:
            pLoc->uRegIntSts &= ~u32;
            tpmLocIrqUpdate(pDevIns, pThis, pLoc);
            break;
        case TPM_CRB_LOCALITY_REG_CTRL_EXT: /* Not implemented. */
        case TPM_CRB_LOCALITY_REG_STATE: /* Readonly */
        case TPM_CRB_LOCALITY_REG_INTF_ID:
        case TPM_CRB_LOCALITY_REG_CTRL_STS:
        case TPM_CRB_LOCALITY_REG_CTRL_CMD_LADDR:
        case TPM_CRB_LOCALITY_REG_CTRL_CMD_HADDR:
        case TPM_CRB_LOCALITY_REG_CTRL_CMD_SZ:
        case TPM_CRB_LOCALITY_REG_CTRL_RSP_SZ:
        case TPM_CRB_LOCALITY_REG_CTRL_RSP_ADDR:
        default: /* Ignore. */
            break;
    }

    return rc;
}


/* -=-=-=-=-=- MMIO callbacks -=-=-=-=-=- */

/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) tpmMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PDEVTPM pThis  = PDMDEVINS_2_DATA(pDevIns, PDEVTPM);
    RT_NOREF(pvUser);

    AssertReturn(cb <= sizeof(uint64_t), VERR_INTERNAL_ERROR);

    RTGCPHYS offAligned = off & ~UINT64_C(0x3);
    uint8_t cBitsShift  = (off & 0x3) * 8;

    VBOXSTRICTRC rc = VINF_SUCCESS;
    uint32_t uReg = tpmGetRegisterFromOffset(offAligned);
    uint8_t bLoc = tpmGetLocalityFromOffset(offAligned);
    PDEVTPMLOCALITY pLoc = &pThis->aLoc[bLoc];

    uint64_t u64;
    if (pThis->fCrb)
        rc = tpmMmioCrbRead(pDevIns, pThis, pLoc, bLoc, uReg, &u64, cb);
    else
        rc = tpmMmioFifoRead(pDevIns, pThis, pLoc, bLoc, uReg, &u64, cb);

    LogFlowFunc((": %RGp %#x %#llx\n", off, cb, u64));

    if (rc == VINF_SUCCESS)
    {
        switch (cb)
        {
            case 1: *(uint8_t *)pv = (uint8_t)(u64 >> cBitsShift); break;
            case 2: *(uint16_t *)pv = (uint16_t)(u64 >> cBitsShift); break;
            case 4: *(uint32_t *)pv = (uint32_t)(u64 >> cBitsShift); break;
            case 8: *(uint64_t *)pv = u64; break;
            default: AssertFailedBreakStmt(rc = VERR_INTERNAL_ERROR);
        }
    }

    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) tpmMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PDEVTPM pThis  = PDMDEVINS_2_DATA(pDevIns, PDEVTPM);
    RT_NOREF(pvUser);

    Assert(!(off & (cb - 1)));

    uint64_t u64;
    switch (cb)
    {
        case 1: u64 = *(const uint8_t *)pv; break;
        case 2: u64 = *(const uint16_t *)pv; break;
        case 4: u64 = *(const uint32_t *)pv; break;
        case 8: u64 = *(const uint64_t *)pv; break;
        default: AssertFailedReturn(VERR_INTERNAL_ERROR);
    }

    LogFlowFunc((": %RGp %#llx\n", off, u64));

    VBOXSTRICTRC rc = VINF_SUCCESS;
    uint32_t uReg = tpmGetRegisterFromOffset(off);
    uint8_t bLoc = tpmGetLocalityFromOffset(off);
    PDEVTPMLOCALITY pLoc = &pThis->aLoc[bLoc];

    if (pThis->fCrb)
        rc = tpmMmioCrbWrite(pDevIns, pThis, pLoc, bLoc, uReg, u64, cb);
    else
        rc = tpmMmioFifoWrite(pDevIns, pThis, pLoc, bLoc, uReg, u64, cb);

    return rc;
}


#ifdef IN_RING3

/**
 * @callback_method_impl{FNPDMTASKDEV, Execute a command in ring-3}
 */
static DECLCALLBACK(void) tpmR3CmdExecWorker(PPDMDEVINS pDevIns, void *pvUser)
{
    PDEVTPM     pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVTPM);
    PDEVTPMR3   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVTPMR3);
    RT_NOREF(pvUser);
    LogFlowFunc(("\n"));

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    if (pThisCC->pDrvTpm)
    {
        size_t cbCmd = RTTpmReqGetSz((PCTPMREQHDR)&pThis->abCmdResp[0]);
        int rc = pThisCC->pDrvTpm->pfnCmdExec(pThisCC->pDrvTpm, pThis->bLoc, &pThis->abCmdResp[0], cbCmd,
                                              &pThis->abCmdResp[0], sizeof(pThis->abCmdResp));
        if (RT_SUCCESS(rc))
        {
            pThis->enmState   = DEVTPMSTATE_CMD_COMPLETION;
            pThis->offCmdResp = 0;
            if (pThis->fCrb)
                tpmLocSetIntSts(pThisCC->pDevIns, pThis, &pThis->aLoc[pThis->bLoc], TPM_CRB_LOCALITY_REG_INT_STS_START);
            else
                tpmLocSetIntSts(pThisCC->pDevIns, pThis, &pThis->aLoc[pThis->bLoc], TPM_FIFO_LOCALITY_REG_INT_STS_DATA_AVAIL | TPM_FIFO_LOCALITY_REG_INT_STS_STS_VALID);
        }
        else
        {
            /* Set fatal error. */
            pThis->enmState = DEVTPMSTATE_FATAL_ERROR;
        }
    }

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
}


/**
 * Resets the shared hardware TPM state.
 *
 * @param   pThis               Pointer to the shared TPM device.
 */
static void tpmR3HwReset(PDEVTPM pThis)
{
    pThis->enmState       = DEVTPMSTATE_IDLE;
    pThis->bLoc           = TPM_NO_LOCALITY_SELECTED;
    pThis->bmLocReqAcc    = 0;
    pThis->bmLocSeizedAcc = 0;
    pThis->offCmdResp     = 0;
    RT_ZERO(pThis->abCmdResp);

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aLoc); i++)
    {
        PDEVTPMLOCALITY pLoc = &pThis->aLoc[i];
        pLoc->uRegIntEn  = 0;
        pLoc->uRegIntSts = 0;
    }
}


/* -=-=-=-=-=-=-=-=- Saved State -=-=-=-=-=-=-=-=- */

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) tpmR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PDEVTPM         pThis = PDMDEVINS_2_DATA(pDevIns, PDEVTPM);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    RT_NOREF(uPass);

    /* Save the part of the config used for verification purposes when restoring. */
    pHlp->pfnSSMPutGCPhys(pSSM, pThis->GCPhysMmio);
    pHlp->pfnSSMPutU16(   pSSM, pThis->uVenId);
    pHlp->pfnSSMPutU16(   pSSM, pThis->uDevId);
    pHlp->pfnSSMPutU8(    pSSM, pThis->bRevId);
    pHlp->pfnSSMPutU8(    pSSM, pThis->uIrq);
    pHlp->pfnSSMPutBool(  pSSM, pThis->fLocChangeSup);
    pHlp->pfnSSMPutU32(   pSSM, (uint32_t)pThis->enmTpmVers);
    pHlp->pfnSSMPutU32(   pSSM, pThis->cbCmdResp);

    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) tpmR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVTPM         pThis = PDMDEVINS_2_DATA(pDevIns, PDEVTPM);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    tpmR3LiveExec(pDevIns, pSSM, SSM_PASS_FINAL);

    int rc = pHlp->pfnSSMPutStructEx(pSSM, pThis, sizeof(*pThis), 0 /*fFlags*/, &g_aTpmFields[0], NULL);
    AssertRCReturn(rc, rc);

    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* sanity/terminator */
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) tpmR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDEVTPM         pThis = PDMDEVINS_2_DATA(pDevIns, PDEVTPM);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    uint8_t         u8;
    uint16_t        u16;
    uint32_t        u32;
    bool            f;
    RTGCPHYS        GCPhysMmio;
    TPMVERSION      enmTpmVers;

    Assert(uPass == SSM_PASS_FINAL); RT_NOREF(uPass);
    AssertMsgReturn(uVersion == TPM_SAVED_STATE_VERSION, ("%d\n", uVersion), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);

    /* Verify the config first. */
    int rc = pHlp->pfnSSMGetGCPhys(pSSM, &GCPhysMmio);
    AssertRCReturn(rc, rc);
    if (GCPhysMmio != pThis->GCPhysMmio)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                       N_("Config mismatch - saved GCPhysMmio=%#RGp; configured GCPhysMmio=%#RGp"),
                                       GCPhysMmio, pThis->GCPhysMmio);

    rc = pHlp->pfnSSMGetU16(pSSM, &u16);
    AssertRCReturn(rc, rc);
    if (u16 != pThis->uVenId)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                       N_("Config mismatch - saved uVenId=%#RX16; configured uVenId=%#RX16"),
                                       u16, pThis->uVenId);

    rc = pHlp->pfnSSMGetU16(pSSM,  &u16);
    AssertRCReturn(rc, rc);
    if (u16 != pThis->uDevId)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                       N_("Config mismatch - saved uDevId=%#RX16; configured uDevId=%#RX16"),
                                       u16, pThis->uDevId);

    rc = pHlp->pfnSSMGetU8(pSSM, &u8);
    AssertRCReturn(rc, rc);
    if (u8 != pThis->bRevId)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                       N_("Config mismatch - saved bRevId=%#RX8; configured bDevId=%#RX8"),
                                       u8, pThis->bRevId);

    rc = pHlp->pfnSSMGetU8(pSSM, &u8);
    AssertRCReturn(rc, rc);
    if (u8 != pThis->uIrq)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                       N_("Config mismatch - saved uIrq=%#RX8; configured uIrq=%#RX8"),
                                       u8, pThis->uIrq);

    rc = pHlp->pfnSSMGetBool(pSSM, &f);
    AssertRCReturn(rc, rc);
    if (f != pThis->fLocChangeSup)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                       N_("Config mismatch - saved fLocChangeSup=%RTbool; configured fLocChangeSup=%RTbool"),
                                       f, pThis->fLocChangeSup);

    rc = pHlp->pfnSSMGetU32(pSSM,  (uint32_t *)&enmTpmVers);
    AssertRCReturn(rc, rc);
    if (enmTpmVers != pThis->enmTpmVers)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                       N_("Config mismatch - saved enmTpmVers=%RU32; configured enmTpmVers=%RU32"),
                                       enmTpmVers, pThis->enmTpmVers);

    rc = pHlp->pfnSSMGetU32(pSSM, &u32);
    AssertRCReturn(rc, rc);
    if (u32 != pThis->cbCmdResp)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                       N_("Config mismatch - saved cbCmdResp=%RU32; configured cbCmdResp=%RU32"),
                                       u32, pThis->cbCmdResp);

    if (uPass == SSM_PASS_FINAL)
    {
        rc = pHlp->pfnSSMGetStructEx(pSSM, pThis, sizeof(*pThis), 0 /*fFlags*/, &g_aTpmFields[0], NULL);
        AssertRCReturn(rc, rc);

        /* The marker. */
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertRCReturn(rc, rc);
        AssertMsgReturn(u32 == UINT32_MAX, ("%#x\n", u32), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

        /* Verify device state sanity. */
        AssertLogRelMsgReturn(   pThis->enmState > DEVTPMSTATE_INVALID
                              && pThis->enmState <= DEVTPMSTATE_LAST_VALID,
                              ("Invalid TPM state loaded from saved state: %#x\n", pThis->enmState),
                              VERR_SSM_UNEXPECTED_DATA);

        AssertLogRelMsgReturn(pThis->offCmdResp <= pThis->cbCmdResp,
                              ("Invalid TPM command/response buffer offset loaded from saved state: %#x\n", pThis->offCmdResp),
                              VERR_SSM_UNEXPECTED_DATA);
    }

    return VINF_SUCCESS;
}


/* -=-=-=-=-=-=-=-=- PDMIBASE -=-=-=-=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) tpmR3QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PDEVTPMCC pThisCC = RT_FROM_MEMBER(pInterface, DEVTPMCC, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMITPMPORT, &pThisCC->ITpmPort);
    return NULL;
}


/* -=-=-=-=-=-=-=-=- PDMITPMPORT -=-=-=-=-=-=-=-=- */

/**
 * @interface_method_impl{PDMITPMPORT,pfnGetMaxBufferSize}
 */
static DECLCALLBACK(uint32_t) tpmR3TpmPortGetMaxBufferSize(PPDMITPMPORT pInterface)
{
    RT_NOREF(pInterface);
    return TPM_DATA_BUFFER_SIZE_MAX;
}


/* -=-=-=-=-=-=-=-=- PDMDEVREG -=-=-=-=-=-=-=-=- */

/**
 * @interface_method_impl{PDMDEVREG,pfnPowerOn}
 */
static DECLCALLBACK(void) tpmR3PowerOn(PPDMDEVINS pDevIns)
{
    PDEVTPM   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVTPM);
    PDEVTPMCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVTPMCC);

    if (pThisCC->pDrvTpm)
        pThis->fEstablishmentSet = pThisCC->pDrvTpm->pfnGetEstablishedFlag(pThisCC->pDrvTpm);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) tpmR3Reset(PPDMDEVINS pDevIns)
{
    PDEVTPM   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVTPM);
    PDEVTPMCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVTPMCC);

    tpmR3HwReset(pThis);
    if (pThisCC->pDrvTpm)
        pThis->fEstablishmentSet = pThisCC->pDrvTpm->pfnGetEstablishedFlag(pThisCC->pDrvTpm);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) tpmR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PDEVTPM pThis = PDMDEVINS_2_DATA(pDevIns, PDEVTPM);

    /** @todo */
    RT_NOREF(pThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) tpmR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVTPM         pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVTPM);
    PDEVTPMCC       pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVTPMCC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;

    RT_NOREF(iInstance);

    pThis->hTpmCmdTask = NIL_PDMTASKHANDLE;

    pThisCC->pDevIns   = pDevIns;

    /* IBase */
    pThisCC->IBase.pfnQueryInterface = tpmR3QueryInterface;

    /* ITpmPort */
    pThisCC->ITpmPort.pfnGetMaxBufferSize = tpmR3TpmPortGetMaxBufferSize;


    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Irq"
                                           "|MmioBase"
                                           "|VendorId"
                                           "|DeviceId"
                                           "|RevisionId"
                                           "|Crb",
                                           "");

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "Irq", &pThis->uIrq, 10);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"Irq\" value"));

    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "MmioBase", &pThis->GCPhysMmio, TPM_MMIO_BASE_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"MmioBase\" value"));

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "VendorId", &pThis->uDevId, TPM_VID_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"VendorId\" value"));

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "DeviceId", &pThis->uDevId, TPM_DID_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"DeviceId\" value"));

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "RevisionId", &pThis->bRevId, TPM_RID_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"RevisionId\" value"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "Crb", &pThis->fCrb, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"Crb\" value"));

    /*
     * Register the MMIO range, PDM API requests page aligned
     * addresses and sizes.
     */
    rc = PDMDevHlpMmioCreateAndMap(pDevIns, pThis->GCPhysMmio, TPM_MMIO_SIZE, tpmMmioWrite, tpmMmioRead,
                                   IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU,
                                   "TPM MMIO", &pThis->hMmio);
    AssertRCReturn(rc, rc);

    /*
     * Attach any TPM driver below.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, 0 /*iLUN*/, &pThisCC->IBase, &pThisCC->pDrvBase, "TPM");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrvTpm = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMITPMCONNECTOR);
        AssertLogRelMsgReturn(pThisCC->pDrvTpm, ("TPM#%d: Driver is missing the TPM interface.\n", iInstance), VERR_PDM_MISSING_INTERFACE);

        uint32_t cbBufDrv = pThisCC->pDrvTpm->pfnGetBufferSize(pThisCC->pDrvTpm);
        pThis->cbCmdResp     = RT_MIN(cbBufDrv, TPM_DATA_BUFFER_SIZE_MAX);
        pThis->fLocChangeSup = pThisCC->pDrvTpm->pfnGetLocalityMax(pThisCC->pDrvTpm) > 0;

        pThis->enmTpmVers = pThisCC->pDrvTpm->pfnGetVersion(pThisCC->pDrvTpm);
        if (pThis->enmTpmVers == TPMVERSION_UNKNOWN)
            return PDMDEV_SET_ERROR(pDevIns, VERR_NOT_SUPPORTED, N_("The emulated TPM version is not supported"));
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
    {
        pThis->fLocChangeSup     = false;
        pThis->fEstablishmentSet = false;
        pThis->cbCmdResp         = TPM_DATA_BUFFER_SIZE_MAX;

        pThisCC->pDrvBase = NULL;
        pThisCC->pDrvTpm  = NULL;
        LogRel(("TPM#%d: no unit\n", iInstance));
    }
    else
        AssertLogRelMsgRCReturn(rc, ("TPM#%d: Failed to attach to TPM driver. rc=%Rrc\n", iInstance, rc), rc);

    /* Create task for executing requests in ring-3. */
    rc = PDMDevHlpTaskCreate(pDevIns, PDMTASK_F_RZ, "TPMCmdWrk",
                             tpmR3CmdExecWorker, NULL /*pvUser*/, &pThis->hTpmCmdTask);
    AssertRCReturn(rc,rc);

    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegister3(pDevIns, TPM_SAVED_STATE_VERSION, sizeof(*pThis),
                               tpmR3LiveExec, tpmR3SaveExec, tpmR3LoadExec);
    AssertRCReturn(rc, rc);

    tpmR3HwReset(pThis);
    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) tpmRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVTPM   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVTPM);

    int rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, tpmMmioWrite, tpmMmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceTpm =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "tpm",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_SERIAL,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVTPM),
    /* .cbInstanceCC = */           sizeof(DEVTPMCC),
    /* .cbInstanceRC = */           sizeof(DEVTPMRC),
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Trusted Platform Module",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           tpmR3Construct,
    /* .pfnDestruct = */            tpmR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             tpmR3PowerOn,
    /* .pfnReset = */               tpmR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           tpmRZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           tpmRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

