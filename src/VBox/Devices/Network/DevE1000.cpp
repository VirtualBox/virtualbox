/* $Id: DevE1000.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevE1000 - Intel 82540EM Ethernet Controller Emulation.
 *
 * Implemented in accordance with the specification:
 *
 *      PCI/PCI-X Family of Gigabit Ethernet Controllers Software Developer's Manual
 *      82540EP/EM, 82541xx, 82544GC/EI, 82545GM/EM, 82546GB/EB, and 82547xx
 *
 *      317453-002 Revision 3.5
 *
 * @todo IPv6 checksum offloading support
 * @todo Flexible Filter / Wakeup (optional?)
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_E1000
#include <iprt/crc.h>
#include <iprt/ctype.h>
#include <iprt/net.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/uuid.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/pdmnetinline.h>
#include <VBox/param.h>
#include "VBoxDD.h"

#include "DevEEPROM.h"
#include "DevE1000Phy.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name E1000 Build Options
 * @{ */
/** @def E1K_INIT_RA0
 * E1K_INIT_RA0 forces E1000 to set the first entry in Receive Address filter
 * table to MAC address obtained from CFGM. Most guests read MAC address from
 * EEPROM and write it to RA[0] explicitly, but Mac OS X seems to depend on it
 * being already set (see @bugref{4657}).
 */
#define E1K_INIT_RA0
/** @def E1K_LSC_ON_RESET
 * E1K_LSC_ON_RESET causes e1000 to generate Link Status Change
 * interrupt after hard reset. This makes the E1K_LSC_ON_SLU option unnecessary.
 * With unplugged cable, LSC is triggerred for 82543GC only.
 */
#define E1K_LSC_ON_RESET
/** @def E1K_LSC_ON_SLU
 * E1K_LSC_ON_SLU causes E1000 to generate Link Status Change interrupt when
 * the guest driver brings up the link via STATUS.LU bit. Again the only guest
 * that requires it is Mac OS X (see @bugref{4657}).
 */
//#define E1K_LSC_ON_SLU
/** @def E1K_INIT_LINKUP_DELAY
 * E1K_INIT_LINKUP_DELAY prevents the link going up while the driver is still
 * in init (see @bugref{8624}).
 */
#define E1K_INIT_LINKUP_DELAY_US (2000 * 1000)
/** @def E1K_IMS_INT_DELAY_NS
 * E1K_IMS_INT_DELAY_NS prevents interrupt storms in Windows guests on enabling
 * interrupts (see @bugref{8624}).
 */
#define E1K_IMS_INT_DELAY_NS 100
/** @def E1K_TX_DELAY
 * E1K_TX_DELAY aims to improve guest-host transfer rate for TCP streams by
 * preventing packets to be sent immediately. It allows to send several
 * packets in a batch reducing the number of acknowledgments. Note that it
 * effectively disables R0 TX path, forcing sending in R3.
 */
//#define E1K_TX_DELAY 150
/** @def E1K_USE_TX_TIMERS
 * E1K_USE_TX_TIMERS aims to reduce the number of generated TX interrupts if a
 * guest driver set the delays via the Transmit Interrupt Delay Value (TIDV)
 * register. Enabling it showed no positive effects on existing guests so it
 * stays disabled. See sections 3.2.7.1 and 3.4.3.1 in "8254x Family of Gigabit
 * Ethernet Controllers Software Developer’s Manual" for more detailed
 * explanation.
 */
//#define E1K_USE_TX_TIMERS
/** @def E1K_NO_TAD
 * E1K_NO_TAD disables one of two timers enabled by E1K_USE_TX_TIMERS, the
 * Transmit Absolute Delay time. This timer sets the maximum time interval
 * during which TX interrupts can be postponed (delayed). It has no effect
 * if E1K_USE_TX_TIMERS is not defined.
 */
//#define E1K_NO_TAD
/** @def E1K_REL_DEBUG
 * E1K_REL_DEBUG enables debug logging of l1, l2, l3 in release build.
 */
//#define E1K_REL_DEBUG
/** @def E1K_INT_STATS
 * E1K_INT_STATS enables collection of internal statistics used for
 * debugging of delayed interrupts, etc.
 */
#define E1K_INT_STATS
/** @def E1K_WITH_MSI
 * E1K_WITH_MSI enables rudimentary MSI support. Not implemented.
 */
//#define E1K_WITH_MSI
/** @def E1K_WITH_TX_CS
 * E1K_WITH_TX_CS protects e1kXmitPending with a critical section.
 */
#define E1K_WITH_TX_CS
/** @def E1K_WITH_TXD_CACHE
 * E1K_WITH_TXD_CACHE causes E1000 to fetch multiple TX descriptors in a
 * single physical memory read (or two if it wraps around the end of TX
 * descriptor ring). It is required for proper functioning of bandwidth
 * resource control as it allows to compute exact sizes of packets prior
 * to allocating their buffers (see @bugref{5582}).
 */
#define E1K_WITH_TXD_CACHE
/** @def E1K_WITH_RXD_CACHE
 * E1K_WITH_RXD_CACHE causes E1000 to fetch multiple RX descriptors in a
 * single physical memory read (or two if it wraps around the end of RX
 * descriptor ring). Intel's packet driver for DOS needs this option in
 * order to work properly (see @bugref{6217}).
 */
#define E1K_WITH_RXD_CACHE
/** @def E1K_WITH_PREREG_MMIO
 * E1K_WITH_PREREG_MMIO enables a new style MMIO registration and is
 * currently only done for testing the relateted PDM, IOM and PGM code. */
//#define E1K_WITH_PREREG_MMIO
/* @} */
/* End of Options ************************************************************/

#ifdef E1K_WITH_TXD_CACHE
/**
 * E1K_TXD_CACHE_SIZE specifies the maximum number of TX descriptors stored
 * in the state structure. It limits the amount of descriptors loaded in one
 * batch read. For example, Linux guest may use up to 20 descriptors per
 * TSE packet. The largest TSE packet seen (Windows guest) was 45 descriptors.
 */
# define E1K_TXD_CACHE_SIZE 64u
#endif /* E1K_WITH_TXD_CACHE */

#ifdef E1K_WITH_RXD_CACHE
/**
 * E1K_RXD_CACHE_SIZE specifies the maximum number of RX descriptors stored
 * in the state structure. It limits the amount of descriptors loaded in one
 * batch read. For example, XP guest adds 15 RX descriptors at a time.
 */
# define E1K_RXD_CACHE_SIZE 16u
#endif /* E1K_WITH_RXD_CACHE */


/* Little helpers ************************************************************/
#undef htons
#undef ntohs
#undef htonl
#undef ntohl
#define htons(x) ((((x) & 0xff00) >> 8) | (((x) & 0x00ff) << 8))
#define ntohs(x) htons(x)
#define htonl(x) ASMByteSwapU32(x)
#define ntohl(x) htonl(x)

#ifndef DEBUG
# ifdef E1K_REL_DEBUG
#  define DEBUG
#  define E1kLog(a)               LogRel(a)
#  define E1kLog2(a)              LogRel(a)
#  define E1kLog3(a)              LogRel(a)
#  define E1kLogX(x, a)           LogRel(a)
//#  define E1kLog3(a)              do {} while (0)
# else
#  define E1kLog(a)               do {} while (0)
#  define E1kLog2(a)              do {} while (0)
#  define E1kLog3(a)              do {} while (0)
#  define E1kLogX(x, a)           do {} while (0)
# endif
#else
#  define E1kLog(a)               Log(a)
#  define E1kLog2(a)              Log2(a)
#  define E1kLog3(a)              Log3(a)
#  define E1kLogX(x, a)           LogIt(x, LOG_GROUP, a)
//#  define E1kLog(a)               do {} while (0)
//#  define E1kLog2(a)              do {} while (0)
//#  define E1kLog3(a)              do {} while (0)
#endif

#if 0
# define LOG_ENABLED
# define E1kLogRel(a) LogRel(a)
# undef Log6
# define Log6(a) LogRel(a)
#else
# define E1kLogRel(a) do { } while (0)
#endif

//#undef DEBUG

#define E1K_RELOCATE(p, o) *(RTHCUINTPTR *)&p += o

#define E1K_INC_CNT32(cnt) \
do { \
    if (cnt < UINT32_MAX) \
        cnt++; \
} while (0)

#define E1K_ADD_CNT64(cntLo, cntHi, val) \
do { \
    uint64_t u64Cnt = RT_MAKE_U64(cntLo, cntHi); \
    uint64_t tmp  = u64Cnt; \
    u64Cnt += val; \
    if (tmp > u64Cnt ) \
        u64Cnt = UINT64_MAX; \
    cntLo = (uint32_t)u64Cnt; \
    cntHi = (uint32_t)(u64Cnt >> 32); \
} while (0)

#ifdef E1K_INT_STATS
# define E1K_INC_ISTAT_CNT(cnt) do { ++cnt; } while (0)
#else /* E1K_INT_STATS */
# define E1K_INC_ISTAT_CNT(cnt) do { } while (0)
#endif /* E1K_INT_STATS */


/*****************************************************************************/

typedef uint32_t E1KCHIP;
#define E1K_CHIP_82540EM 0
#define E1K_CHIP_82543GC 1
#define E1K_CHIP_82545EM 2

#ifdef IN_RING3
/** Different E1000 chips. */
static const struct E1kChips
{
    uint16_t uPCIVendorId;
    uint16_t uPCIDeviceId;
    uint16_t uPCISubsystemVendorId;
    uint16_t uPCISubsystemId;
    const char *pcszName;
} g_aChips[] =
{
    /* Vendor Device SSVendor SubSys  Name */
    { 0x8086,
      /* Temporary code, as MSI-aware driver dislike 0x100E. How to do that right? */
# ifdef E1K_WITH_MSI
      0x105E,
# else
      0x100E,
# endif
                      0x8086, 0x001E, "82540EM" }, /* Intel 82540EM-A in Intel PRO/1000 MT Desktop */
    { 0x8086, 0x1004, 0x8086, 0x1004, "82543GC" }, /* Intel 82543GC   in Intel PRO/1000 T  Server */
    { 0x8086, 0x100F, 0x15AD, 0x0750, "82545EM" }  /* Intel 82545EM-A in VMWare Network Adapter */
};
#endif /* IN_RING3 */


/* The size of register area mapped to I/O space */
#define E1K_IOPORT_SIZE                 0x8
/* The size of memory-mapped register area */
#define E1K_MM_SIZE                     0x20000

#define E1K_MAX_TX_PKT_SIZE    16288
#define E1K_MAX_RX_PKT_SIZE    16384

/*****************************************************************************/

#ifndef VBOX_DEVICE_STRUCT_TESTCASE
/** Gets the specfieid bits from the register. */
#define GET_BITS(reg, bits) ((reg & reg##_##bits##_MASK) >> reg##_##bits##_SHIFT)
#define GET_BITS_V(val, reg, bits) ((val & reg##_##bits##_MASK) >> reg##_##bits##_SHIFT)
#define BITS(reg, bits, bitval) (bitval << reg##_##bits##_SHIFT)
#define SET_BITS(reg, bits, bitval) do { reg = (reg & ~reg##_##bits##_MASK) | (bitval << reg##_##bits##_SHIFT); } while (0)
#define SET_BITS_V(val, reg, bits, bitval) do { val = (val & ~reg##_##bits##_MASK) | (bitval << reg##_##bits##_SHIFT); } while (0)

#define CTRL_SLU            UINT32_C(0x00000040)
#define CTRL_MDIO           UINT32_C(0x00100000)
#define CTRL_MDC            UINT32_C(0x00200000)
#define CTRL_MDIO_DIR       UINT32_C(0x01000000)
#define CTRL_MDC_DIR        UINT32_C(0x02000000)
#define CTRL_RESET          UINT32_C(0x04000000)
#define CTRL_VME            UINT32_C(0x40000000)

#define STATUS_LU           UINT32_C(0x00000002)
#define STATUS_TXOFF        UINT32_C(0x00000010)

#define EECD_EE_WIRES       UINT32_C(0x0F)
#define EECD_EE_REQ         UINT32_C(0x40)
#define EECD_EE_GNT         UINT32_C(0x80)

#define EERD_START          UINT32_C(0x00000001)
#define EERD_DONE           UINT32_C(0x00000010)
#define EERD_DATA_MASK      UINT32_C(0xFFFF0000)
#define EERD_DATA_SHIFT     16
#define EERD_ADDR_MASK      UINT32_C(0x0000FF00)
#define EERD_ADDR_SHIFT     8

#define MDIC_DATA_MASK      UINT32_C(0x0000FFFF)
#define MDIC_DATA_SHIFT     0
#define MDIC_REG_MASK       UINT32_C(0x001F0000)
#define MDIC_REG_SHIFT      16
#define MDIC_PHY_MASK       UINT32_C(0x03E00000)
#define MDIC_PHY_SHIFT      21
#define MDIC_OP_WRITE       UINT32_C(0x04000000)
#define MDIC_OP_READ        UINT32_C(0x08000000)
#define MDIC_READY          UINT32_C(0x10000000)
#define MDIC_INT_EN         UINT32_C(0x20000000)
#define MDIC_ERROR          UINT32_C(0x40000000)

#define TCTL_EN             UINT32_C(0x00000002)
#define TCTL_PSP            UINT32_C(0x00000008)

#define RCTL_EN             UINT32_C(0x00000002)
#define RCTL_UPE            UINT32_C(0x00000008)
#define RCTL_MPE            UINT32_C(0x00000010)
#define RCTL_LPE            UINT32_C(0x00000020)
#define RCTL_LBM_MASK       UINT32_C(0x000000C0)
#define RCTL_LBM_SHIFT      6
#define RCTL_RDMTS_MASK     UINT32_C(0x00000300)
#define RCTL_RDMTS_SHIFT    8
#define RCTL_LBM_TCVR       UINT32_C(3)                 /**< PHY or external SerDes loopback. */
#define RCTL_MO_MASK        UINT32_C(0x00003000)
#define RCTL_MO_SHIFT       12
#define RCTL_BAM            UINT32_C(0x00008000)
#define RCTL_BSIZE_MASK     UINT32_C(0x00030000)
#define RCTL_BSIZE_SHIFT    16
#define RCTL_VFE            UINT32_C(0x00040000)
#define RCTL_CFIEN          UINT32_C(0x00080000)
#define RCTL_CFI            UINT32_C(0x00100000)
#define RCTL_BSEX           UINT32_C(0x02000000)
#define RCTL_SECRC          UINT32_C(0x04000000)

#define ICR_TXDW            UINT32_C(0x00000001)
#define ICR_TXQE            UINT32_C(0x00000002)
#define ICR_LSC             UINT32_C(0x00000004)
#define ICR_RXDMT0          UINT32_C(0x00000010)
#define ICR_RXT0            UINT32_C(0x00000080)
#define ICR_TXD_LOW         UINT32_C(0x00008000)
#define RDTR_FPD            UINT32_C(0x80000000)

#define PBA_st  ((PBAST*)(pThis->auRegs + PBA_IDX))
typedef struct
{
    unsigned rxa   : 7;
    unsigned rxa_r : 9;
    unsigned txa   : 16;
} PBAST;
AssertCompileSize(PBAST, 4);

#define TXDCTL_WTHRESH_MASK   0x003F0000
#define TXDCTL_WTHRESH_SHIFT  16
#define TXDCTL_LWTHRESH_MASK  0xFE000000
#define TXDCTL_LWTHRESH_SHIFT 25

#define RXCSUM_PCSS_MASK    UINT32_C(0x000000FF)
#define RXCSUM_PCSS_SHIFT   0

/** @name Register access macros
 * @remarks These ASSUME alocal variable @a pThis of type PE1KSTATE.
 * @{ */
#define CTRL     pThis->auRegs[CTRL_IDX]
#define STATUS   pThis->auRegs[STATUS_IDX]
#define EECD     pThis->auRegs[EECD_IDX]
#define EERD     pThis->auRegs[EERD_IDX]
#define CTRL_EXT pThis->auRegs[CTRL_EXT_IDX]
#define FLA      pThis->auRegs[FLA_IDX]
#define MDIC     pThis->auRegs[MDIC_IDX]
#define FCAL     pThis->auRegs[FCAL_IDX]
#define FCAH     pThis->auRegs[FCAH_IDX]
#define FCT      pThis->auRegs[FCT_IDX]
#define VET      pThis->auRegs[VET_IDX]
#define ICR      pThis->auRegs[ICR_IDX]
#define ITR      pThis->auRegs[ITR_IDX]
#define ICS      pThis->auRegs[ICS_IDX]
#define IMS      pThis->auRegs[IMS_IDX]
#define IMC      pThis->auRegs[IMC_IDX]
#define RCTL     pThis->auRegs[RCTL_IDX]
#define FCTTV    pThis->auRegs[FCTTV_IDX]
#define TXCW     pThis->auRegs[TXCW_IDX]
#define RXCW     pThis->auRegs[RXCW_IDX]
#define TCTL     pThis->auRegs[TCTL_IDX]
#define TIPG     pThis->auRegs[TIPG_IDX]
#define AIFS     pThis->auRegs[AIFS_IDX]
#define LEDCTL   pThis->auRegs[LEDCTL_IDX]
#define PBA      pThis->auRegs[PBA_IDX]
#define FCRTL    pThis->auRegs[FCRTL_IDX]
#define FCRTH    pThis->auRegs[FCRTH_IDX]
#define RDFH     pThis->auRegs[RDFH_IDX]
#define RDFT     pThis->auRegs[RDFT_IDX]
#define RDFHS    pThis->auRegs[RDFHS_IDX]
#define RDFTS    pThis->auRegs[RDFTS_IDX]
#define RDFPC    pThis->auRegs[RDFPC_IDX]
#define RDBAL    pThis->auRegs[RDBAL_IDX]
#define RDBAH    pThis->auRegs[RDBAH_IDX]
#define RDLEN    pThis->auRegs[RDLEN_IDX]
#define RDH      pThis->auRegs[RDH_IDX]
#define RDT      pThis->auRegs[RDT_IDX]
#define RDTR     pThis->auRegs[RDTR_IDX]
#define RXDCTL   pThis->auRegs[RXDCTL_IDX]
#define RADV     pThis->auRegs[RADV_IDX]
#define RSRPD    pThis->auRegs[RSRPD_IDX]
#define TXDMAC   pThis->auRegs[TXDMAC_IDX]
#define TDFH     pThis->auRegs[TDFH_IDX]
#define TDFT     pThis->auRegs[TDFT_IDX]
#define TDFHS    pThis->auRegs[TDFHS_IDX]
#define TDFTS    pThis->auRegs[TDFTS_IDX]
#define TDFPC    pThis->auRegs[TDFPC_IDX]
#define TDBAL    pThis->auRegs[TDBAL_IDX]
#define TDBAH    pThis->auRegs[TDBAH_IDX]
#define TDLEN    pThis->auRegs[TDLEN_IDX]
#define TDH      pThis->auRegs[TDH_IDX]
#define TDT      pThis->auRegs[TDT_IDX]
#define TIDV     pThis->auRegs[TIDV_IDX]
#define TXDCTL   pThis->auRegs[TXDCTL_IDX]
#define TADV     pThis->auRegs[TADV_IDX]
#define TSPMT    pThis->auRegs[TSPMT_IDX]
#define CRCERRS  pThis->auRegs[CRCERRS_IDX]
#define ALGNERRC pThis->auRegs[ALGNERRC_IDX]
#define SYMERRS  pThis->auRegs[SYMERRS_IDX]
#define RXERRC   pThis->auRegs[RXERRC_IDX]
#define MPC      pThis->auRegs[MPC_IDX]
#define SCC      pThis->auRegs[SCC_IDX]
#define ECOL     pThis->auRegs[ECOL_IDX]
#define MCC      pThis->auRegs[MCC_IDX]
#define LATECOL  pThis->auRegs[LATECOL_IDX]
#define COLC     pThis->auRegs[COLC_IDX]
#define DC       pThis->auRegs[DC_IDX]
#define TNCRS    pThis->auRegs[TNCRS_IDX]
/* #define SEC      pThis->auRegs[SEC_IDX]       Conflict with sys/time.h */
#define CEXTERR  pThis->auRegs[CEXTERR_IDX]
#define RLEC     pThis->auRegs[RLEC_IDX]
#define XONRXC   pThis->auRegs[XONRXC_IDX]
#define XONTXC   pThis->auRegs[XONTXC_IDX]
#define XOFFRXC  pThis->auRegs[XOFFRXC_IDX]
#define XOFFTXC  pThis->auRegs[XOFFTXC_IDX]
#define FCRUC    pThis->auRegs[FCRUC_IDX]
#define PRC64    pThis->auRegs[PRC64_IDX]
#define PRC127   pThis->auRegs[PRC127_IDX]
#define PRC255   pThis->auRegs[PRC255_IDX]
#define PRC511   pThis->auRegs[PRC511_IDX]
#define PRC1023  pThis->auRegs[PRC1023_IDX]
#define PRC1522  pThis->auRegs[PRC1522_IDX]
#define GPRC     pThis->auRegs[GPRC_IDX]
#define BPRC     pThis->auRegs[BPRC_IDX]
#define MPRC     pThis->auRegs[MPRC_IDX]
#define GPTC     pThis->auRegs[GPTC_IDX]
#define GORCL    pThis->auRegs[GORCL_IDX]
#define GORCH    pThis->auRegs[GORCH_IDX]
#define GOTCL    pThis->auRegs[GOTCL_IDX]
#define GOTCH    pThis->auRegs[GOTCH_IDX]
#define RNBC     pThis->auRegs[RNBC_IDX]
#define RUC      pThis->auRegs[RUC_IDX]
#define RFC      pThis->auRegs[RFC_IDX]
#define ROC      pThis->auRegs[ROC_IDX]
#define RJC      pThis->auRegs[RJC_IDX]
#define MGTPRC   pThis->auRegs[MGTPRC_IDX]
#define MGTPDC   pThis->auRegs[MGTPDC_IDX]
#define MGTPTC   pThis->auRegs[MGTPTC_IDX]
#define TORL     pThis->auRegs[TORL_IDX]
#define TORH     pThis->auRegs[TORH_IDX]
#define TOTL     pThis->auRegs[TOTL_IDX]
#define TOTH     pThis->auRegs[TOTH_IDX]
#define TPR      pThis->auRegs[TPR_IDX]
#define TPT      pThis->auRegs[TPT_IDX]
#define PTC64    pThis->auRegs[PTC64_IDX]
#define PTC127   pThis->auRegs[PTC127_IDX]
#define PTC255   pThis->auRegs[PTC255_IDX]
#define PTC511   pThis->auRegs[PTC511_IDX]
#define PTC1023  pThis->auRegs[PTC1023_IDX]
#define PTC1522  pThis->auRegs[PTC1522_IDX]
#define MPTC     pThis->auRegs[MPTC_IDX]
#define BPTC     pThis->auRegs[BPTC_IDX]
#define TSCTC    pThis->auRegs[TSCTC_IDX]
#define TSCTFC   pThis->auRegs[TSCTFC_IDX]
#define RXCSUM   pThis->auRegs[RXCSUM_IDX]
#define WUC      pThis->auRegs[WUC_IDX]
#define WUFC     pThis->auRegs[WUFC_IDX]
#define WUS      pThis->auRegs[WUS_IDX]
#define MANC     pThis->auRegs[MANC_IDX]
#define IPAV     pThis->auRegs[IPAV_IDX]
#define WUPL     pThis->auRegs[WUPL_IDX]
/** @} */
#endif  /* VBOX_DEVICE_STRUCT_TESTCASE */

/**
 * Indices of memory-mapped registers in register table.
 */
typedef enum
{
    CTRL_IDX,
    STATUS_IDX,
    EECD_IDX,
    EERD_IDX,
    CTRL_EXT_IDX,
    FLA_IDX,
    MDIC_IDX,
    FCAL_IDX,
    FCAH_IDX,
    FCT_IDX,
    VET_IDX,
    ICR_IDX,
    ITR_IDX,
    ICS_IDX,
    IMS_IDX,
    IMC_IDX,
    RCTL_IDX,
    FCTTV_IDX,
    TXCW_IDX,
    RXCW_IDX,
    TCTL_IDX,
    TIPG_IDX,
    AIFS_IDX,
    LEDCTL_IDX,
    PBA_IDX,
    FCRTL_IDX,
    FCRTH_IDX,
    RDFH_IDX,
    RDFT_IDX,
    RDFHS_IDX,
    RDFTS_IDX,
    RDFPC_IDX,
    RDBAL_IDX,
    RDBAH_IDX,
    RDLEN_IDX,
    RDH_IDX,
    RDT_IDX,
    RDTR_IDX,
    RXDCTL_IDX,
    RADV_IDX,
    RSRPD_IDX,
    TXDMAC_IDX,
    TDFH_IDX,
    TDFT_IDX,
    TDFHS_IDX,
    TDFTS_IDX,
    TDFPC_IDX,
    TDBAL_IDX,
    TDBAH_IDX,
    TDLEN_IDX,
    TDH_IDX,
    TDT_IDX,
    TIDV_IDX,
    TXDCTL_IDX,
    TADV_IDX,
    TSPMT_IDX,
    CRCERRS_IDX,
    ALGNERRC_IDX,
    SYMERRS_IDX,
    RXERRC_IDX,
    MPC_IDX,
    SCC_IDX,
    ECOL_IDX,
    MCC_IDX,
    LATECOL_IDX,
    COLC_IDX,
    DC_IDX,
    TNCRS_IDX,
    SEC_IDX,
    CEXTERR_IDX,
    RLEC_IDX,
    XONRXC_IDX,
    XONTXC_IDX,
    XOFFRXC_IDX,
    XOFFTXC_IDX,
    FCRUC_IDX,
    PRC64_IDX,
    PRC127_IDX,
    PRC255_IDX,
    PRC511_IDX,
    PRC1023_IDX,
    PRC1522_IDX,
    GPRC_IDX,
    BPRC_IDX,
    MPRC_IDX,
    GPTC_IDX,
    GORCL_IDX,
    GORCH_IDX,
    GOTCL_IDX,
    GOTCH_IDX,
    RNBC_IDX,
    RUC_IDX,
    RFC_IDX,
    ROC_IDX,
    RJC_IDX,
    MGTPRC_IDX,
    MGTPDC_IDX,
    MGTPTC_IDX,
    TORL_IDX,
    TORH_IDX,
    TOTL_IDX,
    TOTH_IDX,
    TPR_IDX,
    TPT_IDX,
    PTC64_IDX,
    PTC127_IDX,
    PTC255_IDX,
    PTC511_IDX,
    PTC1023_IDX,
    PTC1522_IDX,
    MPTC_IDX,
    BPTC_IDX,
    TSCTC_IDX,
    TSCTFC_IDX,
    RXCSUM_IDX,
    WUC_IDX,
    WUFC_IDX,
    WUS_IDX,
    MANC_IDX,
    IPAV_IDX,
    WUPL_IDX,
    MTA_IDX,
    RA_IDX,
    VFTA_IDX,
    IP4AT_IDX,
    IP6AT_IDX,
    WUPM_IDX,
    FFLT_IDX,
    FFMT_IDX,
    FFVT_IDX,
    PBM_IDX,
    RA_82542_IDX,
    MTA_82542_IDX,
    VFTA_82542_IDX,
    E1K_NUM_OF_REGS
} E1kRegIndex;

#define E1K_NUM_OF_32BIT_REGS           MTA_IDX
/** The number of registers with strictly increasing offset. */
#define E1K_NUM_OF_BINARY_SEARCHABLE    (WUPL_IDX + 1)


/**
 * Define E1000-specific EEPROM layout.
 */
struct E1kEEPROM
{
    public:
        EEPROM93C46 eeprom;

#ifdef IN_RING3
        /**
         * Initialize EEPROM content.
         *
         * @param   macAddr     MAC address of E1000.
         */
        void init(RTMAC &macAddr)
        {
            eeprom.init();
            memcpy(eeprom.m_au16Data, macAddr.au16, sizeof(macAddr.au16));
            eeprom.m_au16Data[0x04] = 0xFFFF;
            /*
             * bit 3  - full support for power management
             * bit 10 - full duplex
             */
            eeprom.m_au16Data[0x0A] = 0x4408;
            eeprom.m_au16Data[0x0B] = 0x001E;
            eeprom.m_au16Data[0x0C] = 0x8086;
            eeprom.m_au16Data[0x0D] = 0x100E;
            eeprom.m_au16Data[0x0E] = 0x8086;
            eeprom.m_au16Data[0x0F] = 0x3040;
            eeprom.m_au16Data[0x21] = 0x7061;
            eeprom.m_au16Data[0x22] = 0x280C;
            eeprom.m_au16Data[0x23] = 0x00C8;
            eeprom.m_au16Data[0x24] = 0x00C8;
            eeprom.m_au16Data[0x2F] = 0x0602;
            updateChecksum();
        };

        /**
         * Compute the checksum as required by E1000 and store it
         * in the last word.
         */
        void updateChecksum()
        {
            uint16_t u16Checksum = 0;

            for (int i = 0; i < eeprom.SIZE-1; i++)
                u16Checksum += eeprom.m_au16Data[i];
            eeprom.m_au16Data[eeprom.SIZE-1] = 0xBABA - u16Checksum;
        };

        /**
         * First 6 bytes of EEPROM contain MAC address.
         *
         * @returns MAC address of E1000.
         */
        void getMac(PRTMAC pMac)
        {
            memcpy(pMac->au16, eeprom.m_au16Data, sizeof(pMac->au16));
        };

        uint32_t read()
        {
            return eeprom.read();
        }

        void write(uint32_t u32Wires)
        {
            eeprom.write(u32Wires);
        }

        bool readWord(uint32_t u32Addr, uint16_t *pu16Value)
        {
            return eeprom.readWord(u32Addr, pu16Value);
        }

        int load(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM)
        {
            return eeprom.load(pHlp, pSSM);
        }

        void save(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM)
        {
            eeprom.save(pHlp, pSSM);
        }
#endif /* IN_RING3 */
};


#define E1K_SPEC_VLAN(s)    (s      & 0xFFF)
#define E1K_SPEC_CFI(s) (!!((s>>12) & 0x1))
#define E1K_SPEC_PRI(s)    ((s>>13) & 0x7)

struct E1kRxDStatus
{
    /** @name Descriptor Status field (3.2.3.1)
     * @{ */
    unsigned fDD     : 1;                             /**< Descriptor Done. */
    unsigned fEOP    : 1;                               /**< End of packet. */
    unsigned fIXSM   : 1;                  /**< Ignore checksum indication. */
    unsigned fVP     : 1;                           /**< VLAN, matches VET. */
    unsigned         : 1;
    unsigned fTCPCS  : 1;       /**< RCP Checksum calculated on the packet. */
    unsigned fIPCS   : 1;        /**< IP Checksum calculated on the packet. */
    unsigned fPIF    : 1;                       /**< Passed in-exact filter */
    /** @} */
    /** @name Descriptor Errors field (3.2.3.2)
     * (Only valid when fEOP and fDD are set.)
     * @{ */
    unsigned fCE     : 1;                      /**< CRC or alignment error. */
    unsigned         : 4;    /**< Reserved, varies with different models... */
    unsigned fTCPE   : 1;                      /**< TCP/UDP checksum error. */
    unsigned fIPE    : 1;                           /**< IP Checksum error. */
    unsigned fRXE    : 1;                               /**< RX Data error. */
    /** @} */
    /** @name Descriptor Special field (3.2.3.3)
     * @{  */
    unsigned u16Special : 16;      /**< VLAN: Id, Canonical form, Priority. */
    /** @} */
};
typedef struct E1kRxDStatus E1KRXDST;

struct E1kRxDesc_st
{
    uint64_t u64BufAddr;                        /**< Address of data buffer */
    uint16_t u16Length;                       /**< Length of data in buffer */
    uint16_t u16Checksum;                              /**< Packet checksum */
    E1KRXDST status;
};
typedef struct E1kRxDesc_st E1KRXDESC;
AssertCompileSize(E1KRXDESC, 16);

#define E1K_DTYP_LEGACY -1
#define E1K_DTYP_CONTEXT 0
#define E1K_DTYP_DATA    1
#define E1K_DTYP_INVALID 2

struct E1kTDLegacy
{
    uint64_t u64BufAddr;                     /**< Address of data buffer */
    struct TDLCmd_st
    {
        unsigned u16Length : 16;
        unsigned u8CSO     : 8;
        /* CMD field       : 8 */
        unsigned fEOP      : 1;
        unsigned fIFCS     : 1;
        unsigned fIC       : 1;
        unsigned fRS       : 1;
        unsigned fRPS      : 1;
        unsigned fDEXT     : 1;
        unsigned fVLE      : 1;
        unsigned fIDE      : 1;
    } cmd;
    struct TDLDw3_st
    {
        /* STA field */
        unsigned fDD       : 1;
        unsigned fEC       : 1;
        unsigned fLC       : 1;
        unsigned fTURSV    : 1;
        /* RSV field */
        unsigned u4RSV     : 4;
        /* CSS field */
        unsigned u8CSS     : 8;
        /* Special field*/
        unsigned u16Special: 16;
    } dw3;
};

/**
 * TCP/IP Context Transmit Descriptor, section 3.3.6.
 */
struct E1kTDContext
{
    struct CheckSum_st
    {
        /** TSE: Header start. !TSE: Checksum start. */
        unsigned u8CSS     : 8;
        /** Checksum offset - where to store it. */
        unsigned u8CSO     : 8;
        /** Checksum ending (inclusive) offset, 0 = end of packet. */
        unsigned u16CSE    : 16;
    } ip;
    struct CheckSum_st tu;
    struct TDCDw2_st
    {
        /** TSE: The total number of payload bytes for this context. Sans header. */
        unsigned u20PAYLEN : 20;
        /** The descriptor type - E1K_DTYP_CONTEXT (0). */
        unsigned u4DTYP    : 4;
        /** TUCMD field, 8 bits
         * @{ */
        /** TSE: TCP (set) or UDP (clear). */
        unsigned fTCP      : 1;
        /** TSE: IPv4 (set) or IPv6 (clear) - for finding the payload length field in
         * the IP header.  Does not affect the checksumming.
         * @remarks 82544GC/EI interprets a cleared field differently.  */
        unsigned fIP       : 1;
        /** TSE: TCP segmentation enable.  When clear the context describes  */
        unsigned fTSE      : 1;
        /** Report status (only applies to dw3.fDD for here). */
        unsigned fRS       : 1;
        /** Reserved, MBZ. */
        unsigned fRSV1     : 1;
        /** Descriptor extension, must be set for this descriptor type. */
        unsigned fDEXT     : 1;
        /** Reserved, MBZ. */
        unsigned fRSV2     : 1;
        /** Interrupt delay enable. */
        unsigned fIDE      : 1;
        /** @} */
    } dw2;
    struct TDCDw3_st
    {
        /** Descriptor Done. */
        unsigned fDD       : 1;
        /** Reserved, MBZ. */
        unsigned u7RSV     : 7;
        /** TSO: The header (prototype) length (Ethernet[, VLAN tag], IP, TCP/UDP. */
        unsigned u8HDRLEN  : 8;
        /** TSO: Maximum segment size. */
        unsigned u16MSS    : 16;
    } dw3;
};
typedef struct E1kTDContext E1KTXCTX;

/**
 * TCP/IP Data Transmit Descriptor, section 3.3.7.
 */
struct E1kTDData
{
    uint64_t u64BufAddr;                        /**< Address of data buffer */
    struct TDDCmd_st
    {
        /** The total length of data pointed to by this descriptor. */
        unsigned u20DTALEN : 20;
        /** The descriptor type - E1K_DTYP_DATA (1). */
        unsigned u4DTYP    : 4;
        /** @name DCMD field, 8 bits (3.3.7.1).
         * @{ */
        /** End of packet.  Note TSCTFC update.  */
        unsigned fEOP      : 1;
        /** Insert Ethernet FCS/CRC (requires fEOP to be set). */
        unsigned fIFCS     : 1;
        /** Use the TSE context when set and the normal when clear. */
        unsigned fTSE      : 1;
        /** Report status (dw3.STA). */
        unsigned fRS       : 1;
        /** Reserved. 82544GC/EI defines this report packet set (RPS).  */
        unsigned fRPS      : 1;
        /** Descriptor extension, must be set for this descriptor type. */
        unsigned fDEXT     : 1;
        /** VLAN enable, requires CTRL.VME, auto enables FCS/CRC.
         *  Insert dw3.SPECIAL after ethernet header. */
        unsigned fVLE      : 1;
        /** Interrupt delay enable. */
        unsigned fIDE      : 1;
        /** @} */
    } cmd;
    struct TDDDw3_st
    {
        /** @name STA field (3.3.7.2)
         * @{  */
        unsigned fDD       : 1;                       /**< Descriptor done. */
        unsigned fEC       : 1;                      /**< Excess collision. */
        unsigned fLC       : 1;                        /**< Late collision. */
        /** Reserved, except for the usual oddball (82544GC/EI) where it's called TU. */
        unsigned fTURSV    : 1;
        /** @} */
        unsigned u4RSV     : 4;                   /**< Reserved field, MBZ. */
        /** @name POPTS (Packet Option) field (3.3.7.3)
         * @{  */
        unsigned fIXSM     : 1;                    /**< Insert IP checksum. */
        unsigned fTXSM     : 1;               /**< Insert TCP/UDP checksum. */
        unsigned u6RSV     : 6;                         /**< Reserved, MBZ. */
        /** @} */
        /** @name SPECIAL field - VLAN tag to be inserted after ethernet header.
         * Requires fEOP, fVLE and CTRL.VME to be set.
         * @{ */
        unsigned u16Special: 16;   /**< VLAN: Id, Canonical form, Priority. */
        /** @}  */
    } dw3;
};
typedef struct E1kTDData E1KTXDAT;

union E1kTxDesc
{
    struct E1kTDLegacy  legacy;
    struct E1kTDContext context;
    struct E1kTDData    data;
};
typedef union  E1kTxDesc E1KTXDESC;
AssertCompileSize(E1KTXDESC, 16);

#define RA_CTL_AS 0x0003
#define RA_CTL_AV 0x8000

union E1kRecAddr
{
    uint32_t au32[32];
    struct RAArray
    {
        uint8_t  addr[6];
        uint16_t ctl;
    } array[16];
};
typedef struct E1kRecAddr::RAArray E1KRAELEM;
typedef union E1kRecAddr E1KRA;
AssertCompileSize(E1KRA, 8*16);

#define E1K_IP_RF       UINT16_C(0x8000)   /**< reserved fragment flag */
#define E1K_IP_DF       UINT16_C(0x4000)   /**< dont fragment flag */
#define E1K_IP_MF       UINT16_C(0x2000)   /**< more fragments flag */
#define E1K_IP_OFFMASK  UINT16_C(0x1fff)   /**< mask for fragmenting bits */

/** @todo use+extend RTNETIPV4 */
struct E1kIpHeader
{
    /* type of service / version / header length */
    uint16_t tos_ver_hl;
    /* total length */
    uint16_t total_len;
    /* identification */
    uint16_t ident;
    /* fragment offset field */
    uint16_t offset;
    /* time to live / protocol*/
    uint16_t ttl_proto;
    /* checksum */
    uint16_t chksum;
    /* source IP address */
    uint32_t src;
    /* destination IP address */
    uint32_t dest;
};
AssertCompileSize(struct E1kIpHeader, 20);

#define E1K_TCP_FIN     UINT16_C(0x01)
#define E1K_TCP_SYN     UINT16_C(0x02)
#define E1K_TCP_RST     UINT16_C(0x04)
#define E1K_TCP_PSH     UINT16_C(0x08)
#define E1K_TCP_ACK     UINT16_C(0x10)
#define E1K_TCP_URG     UINT16_C(0x20)
#define E1K_TCP_ECE     UINT16_C(0x40)
#define E1K_TCP_CWR     UINT16_C(0x80)
#define E1K_TCP_FLAGS   UINT16_C(0x3f)

/** @todo use+extend RTNETTCP */
struct E1kTcpHeader
{
    uint16_t src;
    uint16_t dest;
    uint32_t seqno;
    uint32_t ackno;
    uint16_t hdrlen_flags;
    uint16_t wnd;
    uint16_t chksum;
    uint16_t urgp;
};
AssertCompileSize(struct E1kTcpHeader, 20);


#ifdef E1K_WITH_TXD_CACHE
/** The current Saved state version. */
# define E1K_SAVEDSTATE_VERSION         4
/** Saved state version for VirtualBox 4.2 with VLAN tag fields.  */
# define E1K_SAVEDSTATE_VERSION_VBOX_42_VTAG  3
#else /* !E1K_WITH_TXD_CACHE */
/** The current Saved state version. */
# define E1K_SAVEDSTATE_VERSION         3
#endif /* !E1K_WITH_TXD_CACHE */
/** Saved state version for VirtualBox 4.1 and earlier.
 * These did not include VLAN tag fields.  */
#define E1K_SAVEDSTATE_VERSION_VBOX_41  2
/** Saved state version for VirtualBox 3.0 and earlier.
 * This did not include the configuration part nor the E1kEEPROM.  */
#define E1K_SAVEDSTATE_VERSION_VBOX_30  1

/**
 * E1000 shared device state.
 *
 * This is shared between ring-0 and ring-3.
 */
typedef struct E1KSTATE
{
    char                    szPrf[8];                /**< Log prefix, e.g. E1000#1. */

    /** Handle to PCI region \#0, the MMIO region. */
    IOMIOPORTHANDLE         hMmioRegion;
    /** Handle to PCI region \#2, the I/O ports. */
    IOMIOPORTHANDLE         hIoPorts;

    /** Receive Interrupt Delay Timer. */
    TMTIMERHANDLE           hRIDTimer;
    /** Receive Absolute Delay Timer. */
    TMTIMERHANDLE           hRADTimer;
    /** Transmit Interrupt Delay Timer. */
    TMTIMERHANDLE           hTIDTimer;
    /** Transmit Absolute Delay Timer. */
    TMTIMERHANDLE           hTADTimer;
    /** Transmit Delay Timer. */
    TMTIMERHANDLE           hTXDTimer;
    /** Late Interrupt Timer. */
    TMTIMERHANDLE           hIntTimer;
    /** Link Up(/Restore) Timer. */
    TMTIMERHANDLE           hLUTimer;

    /** Transmit task. */
    PDMTASKHANDLE           hTxTask;

    /** Critical section - what is it protecting? */
    PDMCRITSECT             cs;
    /** RX Critical section. */
    PDMCRITSECT             csRx;
#ifdef E1K_WITH_TX_CS
    /** TX Critical section. */
    PDMCRITSECT             csTx;
#endif /* E1K_WITH_TX_CS */
    /** MAC address obtained from the configuration. */
    RTMAC       macConfigured;
    uint16_t    u16Padding0;
    /** EMT: Last time the interrupt was acknowledged.  */
    uint64_t    u64AckedAt;
    /** All: Used for eliminating spurious interrupts. */
    bool        fIntRaised;
    /** EMT: false if the cable is disconnected by the GUI. */
    bool        fCableConnected;
    /** true if the device is attached to a driver. */
    bool        fIsAttached;
    /** EMT: Compute Ethernet CRC for RX packets. */
    bool        fEthernetCRC;
    /** All: throttle interrupts. */
    bool        fItrEnabled;
    /** All: throttle RX interrupts. */
    bool        fItrRxEnabled;
    /** All: Delay TX interrupts using TIDV/TADV. */
    bool        fTidEnabled;
    bool        afPadding[2];
    /** Link up delay (in milliseconds). */
    uint32_t    cMsLinkUpDelay;

    /** All: Device register storage. */
    uint32_t    auRegs[E1K_NUM_OF_32BIT_REGS];
    /** TX/RX: Status LED. */
    PDMLED      led;
    /** TX/RX: Number of packet being sent/received to show in debug log. */
    uint32_t    u32PktNo;

    /** EMT: Offset of the register to be read via IO. */
    uint32_t    uSelectedReg;
    /** EMT: Multicast Table Array. */
    uint32_t    auMTA[128];
    /** EMT: Receive Address registers.  */
    E1KRA       aRecAddr;
    /** EMT: VLAN filter table array. */
    uint32_t    auVFTA[128];
    /** EMT: Receive buffer size. */
    uint16_t    u16RxBSize;
    /** EMT: Locked state -- no state alteration possible. */
    bool        fLocked;
    /** EMT: */
    bool        fDelayInts;
    /** All: */
    bool        fIntMaskUsed;

    /** N/A: */
    bool volatile fMaybeOutOfSpace;
    /** EMT: Gets signalled when more RX descriptors become available. */
    SUPSEMEVENT hEventMoreRxDescAvail;
#ifdef E1K_WITH_RXD_CACHE
    /** RX: Fetched RX descriptors. */
    E1KRXDESC   aRxDescriptors[E1K_RXD_CACHE_SIZE];
    //uint64_t    aRxDescAddr[E1K_RXD_CACHE_SIZE];
    /** RX: Actual number of fetched RX descriptors. */
    uint32_t    nRxDFetched;
    /** RX: Index in cache of RX descriptor being processed. */
    uint32_t    iRxDCurrent;
#endif /* E1K_WITH_RXD_CACHE */

    /** TX: Context used for TCP segmentation packets. */
    E1KTXCTX    contextTSE;
    /** TX: Context used for ordinary packets. */
    E1KTXCTX    contextNormal;
#ifdef E1K_WITH_TXD_CACHE
    /** TX: Fetched TX descriptors. */
    E1KTXDESC   aTxDescriptors[E1K_TXD_CACHE_SIZE];
    /** TX: Validity of TX descriptors. Set by e1kLocateTxPacket, used by e1kXmitPacket. */
    bool        afTxDValid[E1K_TXD_CACHE_SIZE];
    /** TX: Actual number of fetched TX descriptors. */
    uint8_t     nTxDFetched;
    /** TX: Index in cache of TX descriptor being processed. */
    uint8_t     iTxDCurrent;
    /** TX: Will this frame be sent as GSO. */
    bool        fGSO;
    /** Alignment padding. */
    bool        fReserved;
    /** TX: Number of bytes in next packet. */
    uint32_t    cbTxAlloc;

#endif /* E1K_WITH_TXD_CACHE */
    /** GSO context. u8Type is set to PDMNETWORKGSOTYPE_INVALID when not
     *  applicable to the current TSE mode. */
    PDMNETWORKGSO GsoCtx;
    /** Scratch space for holding the loopback / fallback scatter / gather
     *  descriptor. */
    union
    {
        PDMSCATTERGATHER    Sg;
        uint8_t             padding[8 * sizeof(RTUINTPTR)];
    }           uTxFallback;
    /** TX: Transmit packet buffer use for TSE fallback and loopback. */
    uint8_t     aTxPacketFallback[E1K_MAX_TX_PKT_SIZE];
    /** TX: Number of bytes assembled in TX packet buffer. */
    uint16_t    u16TxPktLen;
    /** TX: False will force segmentation in e1000 instead of sending frames as GSO. */
    bool        fGSOEnabled;
    /** TX: IP checksum has to be inserted if true. */
    bool        fIPcsum;
    /** TX: TCP/UDP checksum has to be inserted if true. */
    bool        fTCPcsum;
    /** TX: VLAN tag has to be inserted if true. */
    bool        fVTag;
    /** TX: TCI part of VLAN tag to be inserted. */
    uint16_t    u16VTagTCI;
    /** TX TSE fallback: Number of payload bytes remaining in TSE context. */
    uint32_t    u32PayRemain;
    /** TX TSE fallback: Number of header bytes remaining in TSE context. */
    uint16_t    u16HdrRemain;
    /** TX TSE fallback: Flags from template header. */
    uint16_t    u16SavedFlags;
    /** TX TSE fallback: Partial checksum from template header. */
    uint32_t    u32SavedCsum;
    /** ?: Emulated controller type. */
    E1KCHIP     eChip;

    /** EMT: Physical interface emulation. */
    PHY         phy;

#if 0
    /** Alignment padding. */
    uint8_t                             Alignment[HC_ARCH_BITS == 64 ? 8 : 4];
#endif

    STAMCOUNTER                         StatReceiveBytes;
    STAMCOUNTER                         StatTransmitBytes;
#if defined(VBOX_WITH_STATISTICS)
    STAMPROFILEADV                      StatMMIOReadRZ;
    STAMPROFILEADV                      StatMMIOReadR3;
    STAMPROFILEADV                      StatMMIOWriteRZ;
    STAMPROFILEADV                      StatMMIOWriteR3;
    STAMPROFILEADV                      StatEEPROMRead;
    STAMPROFILEADV                      StatEEPROMWrite;
    STAMPROFILEADV                      StatIOReadRZ;
    STAMPROFILEADV                      StatIOReadR3;
    STAMPROFILEADV                      StatIOWriteRZ;
    STAMPROFILEADV                      StatIOWriteR3;
    STAMPROFILEADV                      StatLateIntTimer;
    STAMCOUNTER                         StatLateInts;
    STAMCOUNTER                         StatIntsRaised;
    STAMCOUNTER                         StatIntsPrevented;
    STAMPROFILEADV                      StatReceive;
    STAMPROFILEADV                      StatReceiveCRC;
    STAMPROFILEADV                      StatReceiveFilter;
    STAMPROFILEADV                      StatReceiveStore;
    STAMPROFILEADV                      StatTransmitRZ;
    STAMPROFILEADV                      StatTransmitR3;
    STAMPROFILE                         StatTransmitSendRZ;
    STAMPROFILE                         StatTransmitSendR3;
    STAMPROFILE                         StatRxOverflow;
    STAMCOUNTER                         StatRxOverflowWakeupRZ;
    STAMCOUNTER                         StatRxOverflowWakeupR3;
    STAMCOUNTER                         StatTxDescCtxNormal;
    STAMCOUNTER                         StatTxDescCtxTSE;
    STAMCOUNTER                         StatTxDescLegacy;
    STAMCOUNTER                         StatTxDescData;
    STAMCOUNTER                         StatTxDescTSEData;
    STAMCOUNTER                         StatTxPathFallback;
    STAMCOUNTER                         StatTxPathGSO;
    STAMCOUNTER                         StatTxPathRegular;
    STAMCOUNTER                         StatPHYAccesses;
    STAMCOUNTER                         aStatRegWrites[E1K_NUM_OF_REGS];
    STAMCOUNTER                         aStatRegReads[E1K_NUM_OF_REGS];
#endif /* VBOX_WITH_STATISTICS */

#ifdef E1K_INT_STATS
    /* Internal stats */
    uint64_t    u64ArmedAt;
    uint64_t    uStatMaxTxDelay;
    uint32_t    uStatInt;
    uint32_t    uStatIntTry;
    uint32_t    uStatIntLower;
    uint32_t    uStatNoIntICR;
    int32_t     iStatIntLost;
    int32_t     iStatIntLostOne;
    uint32_t    uStatIntIMS;
    uint32_t    uStatIntSkip;
    uint32_t    uStatIntLate;
    uint32_t    uStatIntMasked;
    uint32_t    uStatIntEarly;
    uint32_t    uStatIntRx;
    uint32_t    uStatIntTx;
    uint32_t    uStatIntICS;
    uint32_t    uStatIntRDTR;
    uint32_t    uStatIntRXDMT0;
    uint32_t    uStatIntTXQE;
    uint32_t    uStatTxNoRS;
    uint32_t    uStatTxIDE;
    uint32_t    uStatTxDelayed;
    uint32_t    uStatTxDelayExp;
    uint32_t    uStatTAD;
    uint32_t    uStatTID;
    uint32_t    uStatRAD;
    uint32_t    uStatRID;
    uint32_t    uStatRxFrm;
    uint32_t    uStatTxFrm;
    uint32_t    uStatDescCtx;
    uint32_t    uStatDescDat;
    uint32_t    uStatDescLeg;
    uint32_t    uStatTx1514;
    uint32_t    uStatTx2962;
    uint32_t    uStatTx4410;
    uint32_t    uStatTx5858;
    uint32_t    uStatTx7306;
    uint32_t    uStatTx8754;
    uint32_t    uStatTx16384;
    uint32_t    uStatTx32768;
    uint32_t    uStatTxLarge;
    uint32_t    uStatAlign;
#endif /* E1K_INT_STATS */
} E1KSTATE;
/** Pointer to the E1000 device state. */
typedef E1KSTATE *PE1KSTATE;

/**
 * E1000 ring-3 device state
 *
 * @implements  PDMINETWORKDOWN
 * @implements  PDMINETWORKCONFIG
 * @implements  PDMILEDPORTS
 */
typedef struct E1KSTATER3
{
    PDMIBASE                IBase;
    PDMINETWORKDOWN         INetworkDown;
    PDMINETWORKCONFIG       INetworkConfig;
    /** LED interface */
    PDMILEDPORTS            ILeds;
    /** Attached network driver. */
    R3PTRTYPE(PPDMIBASE)    pDrvBase;
    R3PTRTYPE(PPDMILEDCONNECTORS) pLedsConnector;

    /** Pointer to the shared state. */
    R3PTRTYPE(PE1KSTATE)    pShared;

    /** Device instance. */
    PPDMDEVINSR3            pDevInsR3;
    /** Attached network driver. */
    PPDMINETWORKUPR3        pDrvR3;
    /** The scatter / gather buffer used for the current outgoing packet. */
    R3PTRTYPE(PPDMSCATTERGATHER) pTxSgR3;

    /** EMT: EEPROM emulation */
    E1kEEPROM               eeprom;
} E1KSTATER3;
/** Pointer to the E1000 ring-3 device state. */
typedef E1KSTATER3 *PE1KSTATER3;


/**
 * E1000 ring-0 device state
 */
typedef struct E1KSTATER0
{
    /** Device instance. */
    PPDMDEVINSR0            pDevInsR0;
    /** Attached network driver. */
    PPDMINETWORKUPR0        pDrvR0;
    /** The scatter / gather buffer used for the current outgoing packet - R0. */
    R0PTRTYPE(PPDMSCATTERGATHER) pTxSgR0;
} E1KSTATER0;
/** Pointer to the E1000 ring-0 device state. */
typedef E1KSTATER0 *PE1KSTATER0;


/**
 * E1000 raw-mode device state
 */
typedef struct E1KSTATERC
{
    /** Device instance. */
    PPDMDEVINSRC            pDevInsRC;
    /** Attached network driver. */
    PPDMINETWORKUPRC        pDrvRC;
    /** The scatter / gather buffer used for the current outgoing packet. */
    RCPTRTYPE(PPDMSCATTERGATHER) pTxSgRC;
} E1KSTATERC;
/** Pointer to the E1000 raw-mode device state. */
typedef E1KSTATERC *PE1KSTATERC;


/** @def PE1KSTATECC
 * Pointer to the instance data for the current context. */
#ifdef IN_RING3
typedef  E1KSTATER3  E1KSTATECC;
typedef PE1KSTATER3 PE1KSTATECC;
#elif defined(IN_RING0)
typedef  E1KSTATER0  E1KSTATECC;
typedef PE1KSTATER0 PE1KSTATECC;
#elif defined(IN_RC)
typedef  E1KSTATERC  E1KSTATECC;
typedef PE1KSTATERC PE1KSTATECC;
#else
# error "Not IN_RING3, IN_RING0 or IN_RC"
#endif


#ifndef VBOX_DEVICE_STRUCT_TESTCASE

/* Forward declarations ******************************************************/
static int e1kXmitPending(PPDMDEVINS pDevIns, PE1KSTATE pThis, bool fOnWorkerThread);

/**
 * E1000 register read handler.
 */
typedef int (FNE1KREGREAD)(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t *pu32Value);
/**
 * E1000 register write handler.
 */
typedef int (FNE1KREGWRITE)(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t u32Value);

static FNE1KREGREAD  e1kRegReadUnimplemented;
static FNE1KREGWRITE e1kRegWriteUnimplemented;
static FNE1KREGREAD  e1kRegReadAutoClear;
static FNE1KREGREAD  e1kRegReadDefault;
static FNE1KREGWRITE e1kRegWriteDefault;
#if 0 /* unused */
static FNE1KREGREAD  e1kRegReadCTRL;
#endif
static FNE1KREGWRITE e1kRegWriteCTRL;
static FNE1KREGREAD  e1kRegReadEECD;
static FNE1KREGWRITE e1kRegWriteEECD;
static FNE1KREGWRITE e1kRegWriteEERD;
static FNE1KREGWRITE e1kRegWriteMDIC;
static FNE1KREGREAD  e1kRegReadICR;
static FNE1KREGWRITE e1kRegWriteICR;
static FNE1KREGREAD  e1kRegReadICS;
static FNE1KREGWRITE e1kRegWriteICS;
static FNE1KREGWRITE e1kRegWriteIMS;
static FNE1KREGWRITE e1kRegWriteIMC;
static FNE1KREGWRITE e1kRegWriteRCTL;
static FNE1KREGWRITE e1kRegWritePBA;
static FNE1KREGWRITE e1kRegWriteRDT;
static FNE1KREGWRITE e1kRegWriteRDTR;
static FNE1KREGWRITE e1kRegWriteTDT;
static FNE1KREGREAD  e1kRegReadMTA;
static FNE1KREGWRITE e1kRegWriteMTA;
static FNE1KREGREAD  e1kRegReadRA;
static FNE1KREGWRITE e1kRegWriteRA;
static FNE1KREGREAD  e1kRegReadVFTA;
static FNE1KREGWRITE e1kRegWriteVFTA;

/**
 * Register map table.
 *
 * Override pfnRead and pfnWrite to get register-specific behavior.
 */
static const struct E1kRegMap_st
{
    /** Register offset in the register space. */
    uint32_t   offset;
    /** Size in bytes. Registers of size > 4 are in fact tables. */
    uint32_t   size;
    /** Readable bits. */
    uint32_t   readable;
    /** Writable bits. */
    uint32_t   writable;
    /** Read callback. */
    FNE1KREGREAD *pfnRead;
    /** Write callback. */
    FNE1KREGWRITE *pfnWrite;
    /** Abbreviated name. */
    const char *abbrev;
    /** Full name. */
    const char *name;
} g_aE1kRegMap[E1K_NUM_OF_REGS] =
{
    /* offset  size     read mask   write mask  read callback            write callback            abbrev      full name                     */
    /*-------  -------  ----------  ----------  -----------------------  ------------------------  ----------  ------------------------------*/
    { 0x00000, 0x00004, 0xDBF31BE9, 0xDBF31BE9, e1kRegReadDefault      , e1kRegWriteCTRL         , "CTRL"    , "Device Control" },
    { 0x00008, 0x00004, 0x0000FDFF, 0x00000000, e1kRegReadDefault      , e1kRegWriteUnimplemented, "STATUS"  , "Device Status" },
    { 0x00010, 0x00004, 0x000027F0, 0x00000070, e1kRegReadEECD         , e1kRegWriteEECD         , "EECD"    , "EEPROM/Flash Control/Data" },
    { 0x00014, 0x00004, 0xFFFFFF10, 0xFFFFFF00, e1kRegReadDefault      , e1kRegWriteEERD         , "EERD"    , "EEPROM Read" },
    { 0x00018, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "CTRL_EXT", "Extended Device Control" },
    { 0x0001c, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "FLA"     , "Flash Access (N/A)" },
    { 0x00020, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadDefault      , e1kRegWriteMDIC         , "MDIC"    , "MDI Control" },
    { 0x00028, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "FCAL"    , "Flow Control Address Low" },
    { 0x0002c, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "FCAH"    , "Flow Control Address High" },
    { 0x00030, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "FCT"     , "Flow Control Type" },
    { 0x00038, 0x00004, 0x0000FFFF, 0x0000FFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "VET"     , "VLAN EtherType" },
    { 0x000c0, 0x00004, 0x0001F6DF, 0x0001F6DF, e1kRegReadICR          , e1kRegWriteICR          , "ICR"     , "Interrupt Cause Read" },
    { 0x000c4, 0x00004, 0x0000FFFF, 0x0000FFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "ITR"     , "Interrupt Throttling" },
    { 0x000c8, 0x00004, 0x0001F6DF, 0xFFFFFFFF, e1kRegReadICS          , e1kRegWriteICS          , "ICS"     , "Interrupt Cause Set" },
    { 0x000d0, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadDefault      , e1kRegWriteIMS          , "IMS"     , "Interrupt Mask Set/Read" },
    { 0x000d8, 0x00004, 0x00000000, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteIMC          , "IMC"     , "Interrupt Mask Clear" },
    { 0x00100, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadDefault      , e1kRegWriteRCTL         , "RCTL"    , "Receive Control" },
    { 0x00170, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "FCTTV"   , "Flow Control Transmit Timer Value" },
    { 0x00178, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "TXCW"    , "Transmit Configuration Word (N/A)" },
    { 0x00180, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RXCW"    , "Receive Configuration Word (N/A)" },
    { 0x00400, 0x00004, 0x017FFFFA, 0x017FFFFA, e1kRegReadDefault      , e1kRegWriteDefault      , "TCTL"    , "Transmit Control" },
    { 0x00410, 0x00004, 0x3FFFFFFF, 0x3FFFFFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "TIPG"    , "Transmit IPG" },
    { 0x00458, 0x00004, 0x0000FFFF, 0x0000FFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "AIFS"    , "Adaptive IFS Throttle - AIT" },
    { 0x00e00, 0x00004, 0xCFCFCFCF, 0xCFCFCFCF, e1kRegReadDefault      , e1kRegWriteDefault      , "LEDCTL"  , "LED Control" },
    { 0x01000, 0x00004, 0xFFFF007F, 0x0000007F, e1kRegReadDefault      , e1kRegWritePBA          , "PBA"     , "Packet Buffer Allocation" },
    { 0x02160, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "FCRTL"   , "Flow Control Receive Threshold Low" },
    { 0x02168, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "FCRTH"   , "Flow Control Receive Threshold High" },
    { 0x02410, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RDFH"    , "Receive Data FIFO Head" },
    { 0x02418, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RDFT"    , "Receive Data FIFO Tail" },
    { 0x02420, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RDFHS"   , "Receive Data FIFO Head Saved Register" },
    { 0x02428, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RDFTS"   , "Receive Data FIFO Tail Saved Register" },
    { 0x02430, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RDFPC"   , "Receive Data FIFO Packet Count" },
    { 0x02800, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "RDBAL"   , "Receive Descriptor Base Low" },
    { 0x02804, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "RDBAH"   , "Receive Descriptor Base High" },
    { 0x02808, 0x00004, 0x000FFF80, 0x000FFF80, e1kRegReadDefault      , e1kRegWriteDefault      , "RDLEN"   , "Receive Descriptor Length" },
    { 0x02810, 0x00004, 0x0000FFFF, 0x0000FFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "RDH"     , "Receive Descriptor Head" },
    { 0x02818, 0x00004, 0x0000FFFF, 0x0000FFFF, e1kRegReadDefault      , e1kRegWriteRDT          , "RDT"     , "Receive Descriptor Tail" },
    { 0x02820, 0x00004, 0x0000FFFF, 0x0000FFFF, e1kRegReadDefault      , e1kRegWriteRDTR         , "RDTR"    , "Receive Delay Timer" },
    { 0x02828, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RXDCTL"  , "Receive Descriptor Control" },
    { 0x0282c, 0x00004, 0x0000FFFF, 0x0000FFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "RADV"    , "Receive Interrupt Absolute Delay Timer" },
    { 0x02c00, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RSRPD"   , "Receive Small Packet Detect Interrupt" },
    { 0x03000, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "TXDMAC"  , "TX DMA Control (N/A)" },
    { 0x03410, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "TDFH"    , "Transmit Data FIFO Head" },
    { 0x03418, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "TDFT"    , "Transmit Data FIFO Tail" },
    { 0x03420, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "TDFHS"   , "Transmit Data FIFO Head Saved Register" },
    { 0x03428, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "TDFTS"   , "Transmit Data FIFO Tail Saved Register" },
    { 0x03430, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "TDFPC"   , "Transmit Data FIFO Packet Count" },
    { 0x03800, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "TDBAL"   , "Transmit Descriptor Base Low" },
    { 0x03804, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "TDBAH"   , "Transmit Descriptor Base High" },
    { 0x03808, 0x00004, 0x000FFF80, 0x000FFF80, e1kRegReadDefault      , e1kRegWriteDefault      , "TDLEN"   , "Transmit Descriptor Length" },
    { 0x03810, 0x00004, 0x0000FFFF, 0x0000FFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "TDH"     , "Transmit Descriptor Head" },
    { 0x03818, 0x00004, 0x0000FFFF, 0x0000FFFF, e1kRegReadDefault      , e1kRegWriteTDT          , "TDT"     , "Transmit Descriptor Tail" },
    { 0x03820, 0x00004, 0x0000FFFF, 0x0000FFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "TIDV"    , "Transmit Interrupt Delay Value" },
    { 0x03828, 0x00004, 0xFF3F3F3F, 0xFF3F3F3F, e1kRegReadDefault      , e1kRegWriteDefault      , "TXDCTL"  , "Transmit Descriptor Control" },
    { 0x0382c, 0x00004, 0x0000FFFF, 0x0000FFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "TADV"    , "Transmit Absolute Interrupt Delay Timer" },
    { 0x03830, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "TSPMT"   , "TCP Segmentation Pad and Threshold" },
    { 0x04000, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "CRCERRS" , "CRC Error Count" },
    { 0x04004, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "ALGNERRC", "Alignment Error Count" },
    { 0x04008, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "SYMERRS" , "Symbol Error Count" },
    { 0x0400c, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RXERRC"  , "RX Error Count" },
    { 0x04010, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "MPC"     , "Missed Packets Count" },
    { 0x04014, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "SCC"     , "Single Collision Count" },
    { 0x04018, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "ECOL"    , "Excessive Collisions Count" },
    { 0x0401c, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "MCC"     , "Multiple Collision Count" },
    { 0x04020, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "LATECOL" , "Late Collisions Count" },
    { 0x04028, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "COLC"    , "Collision Count" },
    { 0x04030, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "DC"      , "Defer Count" },
    { 0x04034, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "TNCRS"   , "Transmit - No CRS" },
    { 0x04038, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "SEC"     , "Sequence Error Count" },
    { 0x0403c, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "CEXTERR" , "Carrier Extension Error Count" },
    { 0x04040, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RLEC"    , "Receive Length Error Count" },
    { 0x04048, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "XONRXC"  , "XON Received Count" },
    { 0x0404c, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "XONTXC"  , "XON Transmitted Count" },
    { 0x04050, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "XOFFRXC" , "XOFF Received Count" },
    { 0x04054, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "XOFFTXC" , "XOFF Transmitted Count" },
    { 0x04058, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "FCRUC"   , "FC Received Unsupported Count" },
    { 0x0405c, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "PRC64"   , "Packets Received (64 Bytes) Count" },
    { 0x04060, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "PRC127"  , "Packets Received (65-127 Bytes) Count" },
    { 0x04064, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "PRC255"  , "Packets Received (128-255 Bytes) Count" },
    { 0x04068, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "PRC511"  , "Packets Received (256-511 Bytes) Count" },
    { 0x0406c, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "PRC1023" , "Packets Received (512-1023 Bytes) Count" },
    { 0x04070, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "PRC1522" , "Packets Received (1024-Max Bytes)" },
    { 0x04074, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "GPRC"    , "Good Packets Received Count" },
    { 0x04078, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "BPRC"    , "Broadcast Packets Received Count" },
    { 0x0407c, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "MPRC"    , "Multicast Packets Received Count" },
    { 0x04080, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "GPTC"    , "Good Packets Transmitted Count" },
    { 0x04088, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "GORCL"   , "Good Octets Received Count (Low)" },
    { 0x0408c, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "GORCH"   , "Good Octets Received Count (Hi)" },
    { 0x04090, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "GOTCL"   , "Good Octets Transmitted Count (Low)" },
    { 0x04094, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "GOTCH"   , "Good Octets Transmitted Count (Hi)" },
    { 0x040a0, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RNBC"    , "Receive No Buffers Count" },
    { 0x040a4, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RUC"     , "Receive Undersize Count" },
    { 0x040a8, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RFC"     , "Receive Fragment Count" },
    { 0x040ac, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "ROC"     , "Receive Oversize Count" },
    { 0x040b0, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "RJC"     , "Receive Jabber Count" },
    { 0x040b4, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "MGTPRC"  , "Management Packets Received Count" },
    { 0x040b8, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "MGTPDC"  , "Management Packets Dropped Count" },
    { 0x040bc, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "MGTPTC"  , "Management Pkts Transmitted Count" },
    { 0x040c0, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "TORL"    , "Total Octets Received (Lo)" },
    { 0x040c4, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "TORH"    , "Total Octets Received (Hi)" },
    { 0x040c8, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "TOTL"    , "Total Octets Transmitted (Lo)" },
    { 0x040cc, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "TOTH"    , "Total Octets Transmitted (Hi)" },
    { 0x040d0, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "TPR"     , "Total Packets Received" },
    { 0x040d4, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "TPT"     , "Total Packets Transmitted" },
    { 0x040d8, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "PTC64"   , "Packets Transmitted (64 Bytes) Count" },
    { 0x040dc, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "PTC127"  , "Packets Transmitted (65-127 Bytes) Count" },
    { 0x040e0, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "PTC255"  , "Packets Transmitted (128-255 Bytes) Count" },
    { 0x040e4, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "PTC511"  , "Packets Transmitted (256-511 Bytes) Count" },
    { 0x040e8, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "PTC1023" , "Packets Transmitted (512-1023 Bytes) Count" },
    { 0x040ec, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "PTC1522" , "Packets Transmitted (1024 Bytes or Greater) Count" },
    { 0x040f0, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "MPTC"    , "Multicast Packets Transmitted Count" },
    { 0x040f4, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "BPTC"    , "Broadcast Packets Transmitted Count" },
    { 0x040f8, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "TSCTC"   , "TCP Segmentation Context Transmitted Count" },
    { 0x040fc, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadAutoClear    , e1kRegWriteUnimplemented, "TSCTFC"  , "TCP Segmentation Context Tx Fail Count" },
    { 0x05000, 0x00004, 0x000007FF, 0x000007FF, e1kRegReadDefault      , e1kRegWriteDefault      , "RXCSUM"  , "Receive Checksum Control" },
    { 0x05800, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "WUC"     , "Wakeup Control" },
    { 0x05808, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "WUFC"    , "Wakeup Filter Control" },
    { 0x05810, 0x00004, 0xFFFFFFFF, 0x00000000, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "WUS"     , "Wakeup Status" },
    { 0x05820, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadDefault      , e1kRegWriteDefault      , "MANC"    , "Management Control" },
    { 0x05838, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "IPAV"    , "IP Address Valid" },
    { 0x05900, 0x00004, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "WUPL"    , "Wakeup Packet Length" },
    { 0x05200, 0x00200, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadMTA          , e1kRegWriteMTA          , "MTA"     , "Multicast Table Array (n)" },
    { 0x05400, 0x00080, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadRA           , e1kRegWriteRA           , "RA"      , "Receive Address (64-bit) (n)" },
    { 0x05600, 0x00200, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadVFTA         , e1kRegWriteVFTA         , "VFTA"    , "VLAN Filter Table Array (n)" },
    { 0x05840, 0x0001c, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "IP4AT"   , "IPv4 Address Table" },
    { 0x05880, 0x00010, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "IP6AT"   , "IPv6 Address Table" },
    { 0x05a00, 0x00080, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "WUPM"    , "Wakeup Packet Memory" },
    { 0x05f00, 0x0001c, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "FFLT"    , "Flexible Filter Length Table" },
    { 0x09000, 0x003fc, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "FFMT"    , "Flexible Filter Mask Table" },
    { 0x09800, 0x003fc, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "FFVT"    , "Flexible Filter Value Table" },
    { 0x10000, 0x10000, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadUnimplemented, e1kRegWriteUnimplemented, "PBM"     , "Packet Buffer Memory (n)" },
    { 0x00040, 0x00080, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadRA           , e1kRegWriteRA           , "RA82542" , "Receive Address (64-bit) (n) (82542)" },
    { 0x00200, 0x00200, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadMTA          , e1kRegWriteMTA          , "MTA82542", "Multicast Table Array (n) (82542)" },
    { 0x00600, 0x00200, 0xFFFFFFFF, 0xFFFFFFFF, e1kRegReadVFTA         , e1kRegWriteVFTA         , "VFTA82542", "VLAN Filter Table Array (n) (82542)" }
};

#ifdef LOG_ENABLED

/**
 * Convert U32 value to hex string. Masked bytes are replaced with dots.
 *
 * @remarks The mask has half-byte byte (not bit) granularity (e.g. 0000000F).
 *
 * @returns The buffer.
 *
 * @param   u32         The word to convert into string.
 * @param   mask        Selects which bytes to convert.
 * @param   buf         Where to put the result.
 */
static char *e1kU32toHex(uint32_t u32, uint32_t mask, char *buf)
{
    for (char *ptr = buf + 7; ptr >= buf; --ptr, u32 >>=4, mask >>=4)
    {
        if (mask & 0xF)
            *ptr = (u32 & 0xF) + ((u32 & 0xF) > 9 ? '7' : '0');
        else
            *ptr = '.';
    }
    buf[8] = 0;
    return buf;
}

/**
 * Returns timer name for debug purposes.
 *
 * @returns The timer name.
 *
 * @param   pThis       The device state structure.
 * @param   hTimer      The timer to name.
 */
DECLINLINE(const char *) e1kGetTimerName(PE1KSTATE pThis, TMTIMERHANDLE hTimer)
{
    if (hTimer == pThis->hTIDTimer)
        return "TID";
    if (hTimer == pThis->hTADTimer)
        return "TAD";
    if (hTimer == pThis->hRIDTimer)
        return "RID";
    if (hTimer == pThis->hRADTimer)
        return "RAD";
    if (hTimer == pThis->hIntTimer)
        return "Int";
    if (hTimer == pThis->hTXDTimer)
        return "TXD";
    if (hTimer == pThis->hLUTimer)
        return "LinkUp";
    return "unknown";
}

#endif /* LOG_ENABLED */

/**
 * Arm a timer.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the device state structure.
 * @param   hTimer      The timer to arm.
 * @param   uExpireIn   Expiration interval in microseconds.
 */
DECLINLINE(void) e1kArmTimer(PPDMDEVINS pDevIns, PE1KSTATE pThis, TMTIMERHANDLE hTimer, uint32_t uExpireIn)
{
    if (pThis->fLocked)
        return;

    E1kLog2(("%s Arming %s timer to fire in %d usec...\n",
             pThis->szPrf, e1kGetTimerName(pThis, hTimer), uExpireIn));
    int rc = PDMDevHlpTimerSetMicro(pDevIns, hTimer, uExpireIn);
    AssertRC(rc);
}

#ifdef IN_RING3
/**
 * Cancel a timer.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the device state structure.
 * @param   pTimer      Pointer to the timer.
 */
DECLINLINE(void) e1kCancelTimer(PPDMDEVINS pDevIns, PE1KSTATE pThis, TMTIMERHANDLE hTimer)
{
    E1kLog2(("%s Stopping %s timer...\n",
            pThis->szPrf, e1kGetTimerName(pThis, hTimer)));
    int rc = PDMDevHlpTimerStop(pDevIns, hTimer);
    if (RT_FAILURE(rc))
        E1kLog2(("%s e1kCancelTimer: TMTimerStop(%s) failed with %Rrc\n",
                pThis->szPrf, e1kGetTimerName(pThis, hTimer), rc));
    RT_NOREF_PV(pThis);
}
#endif /* IN_RING3 */


#define e1kCsEnter(ps, rcBusy) PDMDevHlpCritSectEnter(pDevIns, &(ps)->cs, (rcBusy))
#define e1kCsEnterReturn(ps, rcBusy) do { \
        int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &(ps)->cs, (rcBusy)); \
        if (rcLock == VINF_SUCCESS) { /* likely */ } \
        else return rcLock; \
    } while (0)
#define e1kR3CsEnterAsserted(ps) do { \
        int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &(ps)->cs, VERR_SEM_BUSY); \
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &(ps)->cs, rcLock); \
    } while (0)
#define e1kCsLeave(ps) PDMDevHlpCritSectLeave(pDevIns, &(ps)->cs)


#define e1kCsRxEnter(ps, rcBusy) PDMDevHlpCritSectEnter(pDevIns, &(ps)->csRx, (rcBusy))
#define e1kCsRxEnterReturn(ps) do { \
        int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &(ps)->csRx, VERR_SEM_BUSY); \
        AssertRCReturn(rcLock, rcLock); \
    } while (0)
#define e1kR3CsRxEnterAsserted(ps) do { \
        int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &(ps)->csRx, VERR_SEM_BUSY); \
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &(ps)->csRx, rcLock); \
    } while (0)
#define e1kCsRxLeave(ps) PDMDevHlpCritSectLeave(pDevIns, &(ps)->csRx)
#define e1kCsRxIsOwner(ps) PDMDevHlpCritSectIsOwner(pDevIns, &(ps)->csRx)


#ifndef E1K_WITH_TX_CS
# define e1kCsTxEnter(ps, rcBusy) VINF_SUCCESS
# define e1kR3CsTxEnterAsserted(ps) do { } while (0)
# define e1kCsTxLeave(ps) do { } while (0)
#else /* E1K_WITH_TX_CS */
# define e1kCsTxEnter(ps, rcBusy) PDMDevHlpCritSectEnter(pDevIns, &(ps)->csTx, (rcBusy))
# define e1kR3CsTxEnterAsserted(ps) do { \
        int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &(ps)->csTx, VERR_SEM_BUSY); \
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &(ps)->csTx, rcLock); \
    } while (0)
# define e1kCsTxLeave(ps) PDMDevHlpCritSectLeave(pDevIns, &(ps)->csTx)
# define e1kCsTxIsOwner(ps) PDMDevHlpCritSectIsOwner(pDevIns, &(ps)->csTx)
#endif /* E1K_WITH_TX_CS */


#ifdef E1K_WITH_TXD_CACHE
/*
 * Transmit Descriptor Register Context
 */
struct E1kTxDContext
{
    uint32_t tdlen;
    uint32_t tdh;
    uint32_t tdt;
    uint8_t  nextPacket;
};
typedef struct E1kTxDContext E1KTXDC, *PE1KTXDC;

DECLINLINE(bool) e1kUpdateTxDContext(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KTXDC pContext)
{
    Assert(e1kCsTxIsOwner(pThis));
    if (!e1kCsTxIsOwner(pThis))
    {
        memset(pContext, 0, sizeof(E1KTXDC));
        return false;
    }
    pContext->tdlen = TDLEN;
    pContext->tdh   = TDH;
    pContext->tdt   = TDT;
    uint32_t cTxRingSize = pContext->tdlen / sizeof(E1KTXDESC);
#ifdef DEBUG
    if (pContext->tdh >= cTxRingSize)
    {
        Log(("%s e1kUpdateTxDContext: will return false because TDH too big (%u >= %u)\n",
             pThis->szPrf, pContext->tdh, cTxRingSize));
        return VINF_SUCCESS;
    }
    if (pContext->tdt >= cTxRingSize)
    {
        Log(("%s e1kUpdateTxDContext: will return false because TDT too big (%u >= %u)\n",
             pThis->szPrf, pContext->tdt, cTxRingSize));
        return VINF_SUCCESS;
    }
#endif /* DEBUG */
    return pContext->tdh < cTxRingSize && pContext->tdt < cTxRingSize;
}
#endif /* E1K_WITH_TXD_CACHE */
#ifdef E1K_WITH_RXD_CACHE
/*
 * Receive Descriptor Register Context
 */
struct E1kRxDContext
{
    uint32_t rdlen;
    uint32_t rdh;
    uint32_t rdt;
};
typedef struct E1kRxDContext E1KRXDC, *PE1KRXDC;

DECLINLINE(bool) e1kUpdateRxDContext(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KRXDC pContext, const char *pcszCallee)
{
    Assert(e1kCsRxIsOwner(pThis));
    if (!e1kCsRxIsOwner(pThis))
        return false;
    pContext->rdlen = RDLEN;
    pContext->rdh   = RDH;
    pContext->rdt   = RDT;
    uint32_t cRxRingSize = pContext->rdlen / sizeof(E1KRXDESC);
    /*
     * Note that the checks for RDT are a bit different. Some guests, OS/2 for
     * example, intend to use all descriptors in RX ring, so they point RDT
     * right beyond the last descriptor in the ring. While this is not
     * acceptable for other registers, it works out fine for RDT.
     */
#ifdef DEBUG
    if (pContext->rdh >= cRxRingSize)
    {
        Log(("%s e1kUpdateRxDContext: called from %s, will return false because RDH too big (%u >= %u)\n",
             pThis->szPrf, pcszCallee, pContext->rdh, cRxRingSize));
        return VINF_SUCCESS;
    }
    if (pContext->rdt > cRxRingSize)
    {
        Log(("%s e1kUpdateRxDContext: called from %s, will return false because RDT too big (%u > %u)\n",
             pThis->szPrf, pcszCallee, pContext->rdt, cRxRingSize));
        return VINF_SUCCESS;
    }
#else /* !DEBUG */
    RT_NOREF(pcszCallee);
#endif /* !DEBUG */
    return pContext->rdh < cRxRingSize && pContext->rdt <= cRxRingSize; // && (RCTL & RCTL_EN);
}
#endif /* E1K_WITH_RXD_CACHE */

/**
 * Wakeup the RX thread.
 */
static void e1kWakeupReceive(PPDMDEVINS pDevIns, PE1KSTATE pThis)
{
    if (   pThis->fMaybeOutOfSpace
        && pThis->hEventMoreRxDescAvail != NIL_SUPSEMEVENT)
    {
        STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatRxOverflowWakeup));
        E1kLog(("%s Waking up Out-of-RX-space semaphore\n",  pThis->szPrf));
        int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEventMoreRxDescAvail);
        AssertRC(rc);
    }
}

#ifdef IN_RING3

/**
 * Hardware reset. Revert all registers to initial values.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @param   pThisCC     The current context instance data.
 */
static void e1kR3HardReset(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KSTATECC pThisCC)
{
    E1kLog(("%s Hard reset triggered\n", pThis->szPrf));
    /* No interrupts should survive device reset, see @bugref(9556). */
    if (pThis->fIntRaised)
    {
        /* Lower(0) INTA(0) */
        PDMDevHlpPCISetIrq(pDevIns, 0, 0);
        pThis->fIntRaised = false;
        E1kLog(("%s e1kR3HardReset: Lowered IRQ: ICR=%08x\n", pThis->szPrf, ICR));
    }
    memset(pThis->auRegs,        0, sizeof(pThis->auRegs));
    memset(pThis->aRecAddr.au32, 0, sizeof(pThis->aRecAddr.au32));
# ifdef E1K_INIT_RA0
    memcpy(pThis->aRecAddr.au32, pThis->macConfigured.au8,
           sizeof(pThis->macConfigured.au8));
    pThis->aRecAddr.array[0].ctl |= RA_CTL_AV;
# endif /* E1K_INIT_RA0 */
    STATUS = 0x0081;    /* SPEED=10b (1000 Mb/s), FD=1b (Full Duplex) */
    EECD   = 0x0100;    /* EE_PRES=1b (EEPROM present) */
    CTRL   = 0x0a09;    /* FRCSPD=1b SPEED=10b LRST=1b FD=1b */
    TSPMT  = 0x01000400;/* TSMT=0400h TSPBP=0100h */
    Assert(GET_BITS(RCTL, BSIZE) == 0);
    pThis->u16RxBSize = 2048;

    uint16_t u16LedCtl = 0x0602; /* LED0/LINK_UP#, LED2/LINK100# */
    pThisCC->eeprom.readWord(0x2F, &u16LedCtl); /* Read LEDCTL defaults from EEPROM */
    LEDCTL = 0x07008300 | (((uint32_t)u16LedCtl & 0xCF00) << 8) | (u16LedCtl & 0xCF); /* Only LED0 and LED2 defaults come from EEPROM */

    /* Reset promiscuous mode */
    if (pThisCC->pDrvR3)
        pThisCC->pDrvR3->pfnSetPromiscuousMode(pThisCC->pDrvR3, false);

# ifdef E1K_WITH_TXD_CACHE
    e1kR3CsTxEnterAsserted(pThis);
    pThis->nTxDFetched  = 0;
    pThis->iTxDCurrent  = 0;
    pThis->fGSO         = false;
    pThis->cbTxAlloc    = 0;
    e1kCsTxLeave(pThis);
# endif /* E1K_WITH_TXD_CACHE */
# ifdef E1K_WITH_RXD_CACHE
    e1kR3CsRxEnterAsserted(pThis);
    pThis->iRxDCurrent = pThis->nRxDFetched = 0;
    e1kCsRxLeave(pThis);
# endif /* E1K_WITH_RXD_CACHE */
# ifdef E1K_LSC_ON_RESET
    E1kLog(("%s Will trigger LSC in %d seconds...\n",
            pThis->szPrf, pThis->cMsLinkUpDelay / 1000));
    e1kArmTimer(pDevIns, pThis, pThis->hLUTimer, pThis->cMsLinkUpDelay * 1000);
# endif /* E1K_LSC_ON_RESET */
}

#endif /* IN_RING3 */

/**
 * Compute Internet checksum.
 *
 * @remarks Refer to http://www.netfor2.com/checksum.html for short intro.
 *
 * @param   pThis       The device state structure.
 * @param   cpPacket    The packet.
 * @param   cb          The size of the packet.
 * @param   pszText     A string denoting direction of packet transfer.
 *
 * @return  The 1's complement of the 1's complement sum.
 *
 * @thread  E1000_TX
 */
static uint16_t e1kCSum16(const void *pvBuf, size_t cb)
{
    uint32_t  csum = 0;
    uint16_t *pu16 = (uint16_t *)pvBuf;

    while (cb > 1)
    {
        csum += *pu16++;
        cb -= 2;
    }
    if (cb)
        csum += *(uint8_t*)pu16;
    while (csum >> 16)
        csum = (csum >> 16) + (csum & 0xFFFF);
    Assert(csum < 65536);
    return (uint16_t)~csum;
}

/**
 * Dump a packet to debug log.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @param   cpPacket    The packet.
 * @param   cb          The size of the packet.
 * @param   pszText     A string denoting direction of packet transfer.
 * @thread  E1000_TX
 */
DECLINLINE(void) e1kPacketDump(PPDMDEVINS pDevIns, PE1KSTATE pThis, const uint8_t *cpPacket, size_t cb, const char *pszText)
{
#ifdef DEBUG
    if (RT_LIKELY(e1kCsEnter(pThis, VERR_SEM_BUSY) == VINF_SUCCESS))
    {
        Log4(("%s --- %s packet #%d: %RTmac => %RTmac (%d bytes) ---\n",
                pThis->szPrf, pszText, ++pThis->u32PktNo, cpPacket+6, cpPacket, cb));
        if (ntohs(*(uint16_t*)(cpPacket+12)) == 0x86DD)
        {
            Log4(("%s --- IPv6: %RTnaipv6 => %RTnaipv6\n",
                  pThis->szPrf, cpPacket+14+8, cpPacket+14+24));
            if (*(cpPacket+14+6) == 0x6)
                Log4(("%s --- TCP: seq=%x ack=%x\n", pThis->szPrf,
                      ntohl(*(uint32_t*)(cpPacket+14+40+4)), ntohl(*(uint32_t*)(cpPacket+14+40+8))));
        }
        else if (ntohs(*(uint16_t*)(cpPacket+12)) == 0x800)
        {
            Log4(("%s --- IPv4: %RTnaipv4 => %RTnaipv4\n",
                  pThis->szPrf, *(uint32_t*)(cpPacket+14+12), *(uint32_t*)(cpPacket+14+16)));
            if (*(cpPacket+14+6) == 0x6)
                Log4(("%s --- TCP: seq=%x ack=%x\n", pThis->szPrf,
                      ntohl(*(uint32_t*)(cpPacket+14+20+4)), ntohl(*(uint32_t*)(cpPacket+14+20+8))));
        }
        E1kLog3(("%.*Rhxd\n", cb, cpPacket));
        e1kCsLeave(pThis);
    }
#else
    if (RT_LIKELY(e1kCsEnter(pThis, VERR_SEM_BUSY) == VINF_SUCCESS))
    {
        if (ntohs(*(uint16_t*)(cpPacket+12)) == 0x86DD)
            E1kLogRel(("E1000: %s packet #%d, %RTmac => %RTmac, %RTnaipv6 => %RTnaipv6, seq=%x ack=%x\n",
                       pszText, ++pThis->u32PktNo, cpPacket+6, cpPacket, cpPacket+14+8, cpPacket+14+24,
                       ntohl(*(uint32_t*)(cpPacket+14+40+4)), ntohl(*(uint32_t*)(cpPacket+14+40+8))));
        else
            E1kLogRel(("E1000: %s packet #%d, %RTmac => %RTmac, %RTnaipv4 => %RTnaipv4, seq=%x ack=%x\n",
                       pszText, ++pThis->u32PktNo, cpPacket+6, cpPacket,
                       *(uint32_t*)(cpPacket+14+12), *(uint32_t*)(cpPacket+14+16),
                       ntohl(*(uint32_t*)(cpPacket+14+20+4)), ntohl(*(uint32_t*)(cpPacket+14+20+8))));
        e1kCsLeave(pThis);
    }
    RT_NOREF2(cb, pszText);
#endif
}

/**
 * Determine the type of transmit descriptor.
 *
 * @returns Descriptor type. See E1K_DTYP_XXX defines.
 *
 * @param   pDesc       Pointer to descriptor union.
 * @thread  E1000_TX
 */
DECLINLINE(int) e1kGetDescType(E1KTXDESC *pDesc)
{
    if (pDesc->legacy.cmd.fDEXT)
        return pDesc->context.dw2.u4DTYP;
    return E1K_DTYP_LEGACY;
}


#ifdef E1K_WITH_RXD_CACHE
/**
 * Return the number of RX descriptor that belong to the hardware.
 *
 * @returns the number of available descriptors in RX ring.
 * @param   pRxdc       The receive descriptor register context.
 * @thread  ???
 */
DECLINLINE(uint32_t) e1kGetRxLen(PE1KRXDC pRxdc)
{
    /**
     *  Make sure RDT won't change during computation. EMT may modify RDT at
     *  any moment.
     */
    uint32_t rdt = pRxdc->rdt;
    return (pRxdc->rdh > rdt ? pRxdc->rdlen/sizeof(E1KRXDESC) : 0) + rdt - pRxdc->rdh;
}

DECLINLINE(unsigned) e1kRxDInCache(PE1KSTATE pThis)
{
    return pThis->nRxDFetched > pThis->iRxDCurrent ?
        pThis->nRxDFetched - pThis->iRxDCurrent : 0;
}

DECLINLINE(unsigned) e1kRxDIsCacheEmpty(PE1KSTATE pThis)
{
    return pThis->iRxDCurrent >= pThis->nRxDFetched;
}

/**
 * Load receive descriptors from guest memory. The caller needs to be in Rx
 * critical section.
 *
 * We need two physical reads in case the tail wrapped around the end of RX
 * descriptor ring.
 *
 * @returns the actual number of descriptors fetched.
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @thread  EMT, RX
 */
DECLINLINE(unsigned) e1kRxDPrefetch(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KRXDC pRxdc)
{
    E1kLog3(("%s e1kRxDPrefetch: RDH=%x RDT=%x RDLEN=%x "
                 "iRxDCurrent=%x nRxDFetched=%x\n",
                 pThis->szPrf, pRxdc->rdh, pRxdc->rdt, pRxdc->rdlen, pThis->iRxDCurrent, pThis->nRxDFetched));
    /* We've already loaded pThis->nRxDFetched descriptors past RDH. */
    unsigned nDescsAvailable    = e1kGetRxLen(pRxdc) - e1kRxDInCache(pThis);
    unsigned nDescsToFetch      = RT_MIN(nDescsAvailable, E1K_RXD_CACHE_SIZE - pThis->nRxDFetched);
    unsigned nDescsTotal        = pRxdc->rdlen / sizeof(E1KRXDESC);
    Assert(nDescsTotal != 0);
    if (nDescsTotal == 0)
        return 0;
    unsigned nFirstNotLoaded    = (pRxdc->rdh + e1kRxDInCache(pThis)) % nDescsTotal;
    unsigned nDescsInSingleRead = RT_MIN(nDescsToFetch, nDescsTotal - nFirstNotLoaded);
    E1kLog3(("%s e1kRxDPrefetch: nDescsAvailable=%u nDescsToFetch=%u "
             "nDescsTotal=%u nFirstNotLoaded=0x%x nDescsInSingleRead=%u\n",
             pThis->szPrf, nDescsAvailable, nDescsToFetch, nDescsTotal,
             nFirstNotLoaded, nDescsInSingleRead));
    if (nDescsToFetch == 0)
        return 0;
    E1KRXDESC* pFirstEmptyDesc = &pThis->aRxDescriptors[pThis->nRxDFetched];
    PDMDevHlpPCIPhysRead(pDevIns,
                         ((uint64_t)RDBAH << 32) + RDBAL + nFirstNotLoaded * sizeof(E1KRXDESC),
                         pFirstEmptyDesc, nDescsInSingleRead * sizeof(E1KRXDESC));
    // uint64_t addrBase = ((uint64_t)RDBAH << 32) + RDBAL;
    // unsigned i, j;
    // for (i = pThis->nRxDFetched; i < pThis->nRxDFetched + nDescsInSingleRead; ++i)
    // {
    //     pThis->aRxDescAddr[i] = addrBase + (nFirstNotLoaded + i - pThis->nRxDFetched) * sizeof(E1KRXDESC);
    //     E1kLog3(("%s aRxDescAddr[%d] = %p\n", pThis->szPrf, i, pThis->aRxDescAddr[i]));
    // }
    E1kLog3(("%s Fetched %u RX descriptors at %08x%08x(0x%x), RDLEN=%08x, RDH=%08x, RDT=%08x\n",
             pThis->szPrf, nDescsInSingleRead,
             RDBAH, RDBAL + pRxdc->rdh * sizeof(E1KRXDESC),
             nFirstNotLoaded, pRxdc->rdlen, pRxdc->rdh, pRxdc->rdt));
    if (nDescsToFetch > nDescsInSingleRead)
    {
        PDMDevHlpPCIPhysRead(pDevIns,
                             ((uint64_t)RDBAH << 32) + RDBAL,
                             pFirstEmptyDesc + nDescsInSingleRead,
                             (nDescsToFetch - nDescsInSingleRead) * sizeof(E1KRXDESC));
        // Assert(i == pThis->nRxDFetched  + nDescsInSingleRead);
        // for (j = 0; i < pThis->nRxDFetched + nDescsToFetch; ++i, ++j)
        // {
        //     pThis->aRxDescAddr[i] = addrBase + j * sizeof(E1KRXDESC);
        //     E1kLog3(("%s aRxDescAddr[%d] = %p\n", pThis->szPrf, i, pThis->aRxDescAddr[i]));
        // }
        E1kLog3(("%s Fetched %u RX descriptors at %08x%08x\n",
                 pThis->szPrf, nDescsToFetch - nDescsInSingleRead,
                 RDBAH, RDBAL));
    }
    pThis->nRxDFetched += nDescsToFetch;
    return nDescsToFetch;
}

# ifdef IN_RING3 /* currently only used in ring-3 due to stack space requirements of the caller */
/**
 * Dump receive descriptor to debug log.
 *
 * @param   pThis       The device state structure.
 * @param   pDesc       Pointer to the descriptor.
 * @thread  E1000_RX
 */
static void e1kPrintRDesc(PE1KSTATE pThis, E1KRXDESC *pDesc)
{
    RT_NOREF2(pThis, pDesc);
    E1kLog2(("%s <-- Receive Descriptor (%d bytes):\n", pThis->szPrf, pDesc->u16Length));
    E1kLog2(("        Address=%16LX Length=%04X Csum=%04X\n",
             pDesc->u64BufAddr, pDesc->u16Length, pDesc->u16Checksum));
    E1kLog2(("        STA: %s %s %s %s %s %s %s ERR: %s %s %s %s SPECIAL: %s VLAN=%03x PRI=%x\n",
             pDesc->status.fPIF ? "PIF" : "pif",
             pDesc->status.fIPCS ? "IPCS" : "ipcs",
             pDesc->status.fTCPCS ? "TCPCS" : "tcpcs",
             pDesc->status.fVP ? "VP" : "vp",
             pDesc->status.fIXSM ? "IXSM" : "ixsm",
             pDesc->status.fEOP ? "EOP" : "eop",
             pDesc->status.fDD ? "DD" : "dd",
             pDesc->status.fRXE ? "RXE" : "rxe",
             pDesc->status.fIPE ? "IPE" : "ipe",
             pDesc->status.fTCPE ? "TCPE" : "tcpe",
             pDesc->status.fCE ? "CE" : "ce",
             E1K_SPEC_CFI(pDesc->status.u16Special) ? "CFI" :"cfi",
             E1K_SPEC_VLAN(pDesc->status.u16Special),
             E1K_SPEC_PRI(pDesc->status.u16Special)));
}
# endif /* IN_RING3 */
#endif /* E1K_WITH_RXD_CACHE */

/**
 * Dump transmit descriptor to debug log.
 *
 * @param   pThis       The device state structure.
 * @param   pDesc       Pointer to descriptor union.
 * @param   pszDir      A string denoting direction of descriptor transfer
 * @thread  E1000_TX
 */
static void e1kPrintTDesc(PE1KSTATE pThis, E1KTXDESC *pDesc, const char *pszDir,
                          unsigned uLevel = RTLOGGRPFLAGS_LEVEL_2)
{
    RT_NOREF4(pThis, pDesc, pszDir, uLevel);

    /*
     * Unfortunately we cannot use our format handler here, we want R0 logging
     * as well.
     */
    switch (e1kGetDescType(pDesc))
    {
        case E1K_DTYP_CONTEXT:
            E1kLogX(uLevel, ("%s %s Context Transmit Descriptor %s\n",
                    pThis->szPrf, pszDir, pszDir));
            E1kLogX(uLevel, ("        IPCSS=%02X IPCSO=%02X IPCSE=%04X TUCSS=%02X TUCSO=%02X TUCSE=%04X\n",
                    pDesc->context.ip.u8CSS, pDesc->context.ip.u8CSO, pDesc->context.ip.u16CSE,
                    pDesc->context.tu.u8CSS, pDesc->context.tu.u8CSO, pDesc->context.tu.u16CSE));
            E1kLogX(uLevel, ("        TUCMD:%s%s%s %s %s PAYLEN=%04x HDRLEN=%04x MSS=%04x STA: %s\n",
                    pDesc->context.dw2.fIDE ? " IDE":"",
                    pDesc->context.dw2.fRS  ? " RS" :"",
                    pDesc->context.dw2.fTSE ? " TSE":"",
                    pDesc->context.dw2.fIP  ? "IPv4":"IPv6",
                    pDesc->context.dw2.fTCP ?  "TCP":"UDP",
                    pDesc->context.dw2.u20PAYLEN,
                    pDesc->context.dw3.u8HDRLEN,
                    pDesc->context.dw3.u16MSS,
                    pDesc->context.dw3.fDD?"DD":""));
            break;
        case E1K_DTYP_DATA:
            E1kLogX(uLevel, ("%s %s Data Transmit Descriptor (%d bytes) %s\n",
                    pThis->szPrf, pszDir, pDesc->data.cmd.u20DTALEN, pszDir));
            E1kLogX(uLevel, ("        Address=%16LX DTALEN=%05X\n",
                    pDesc->data.u64BufAddr,
                    pDesc->data.cmd.u20DTALEN));
            E1kLogX(uLevel, ("        DCMD:%s%s%s%s%s%s%s STA:%s%s%s POPTS:%s%s SPECIAL:%s VLAN=%03x PRI=%x\n",
                    pDesc->data.cmd.fIDE ? " IDE" :"",
                    pDesc->data.cmd.fVLE ? " VLE" :"",
                    pDesc->data.cmd.fRPS ? " RPS" :"",
                    pDesc->data.cmd.fRS  ? " RS"  :"",
                    pDesc->data.cmd.fTSE ? " TSE" :"",
                    pDesc->data.cmd.fIFCS? " IFCS":"",
                    pDesc->data.cmd.fEOP ? " EOP" :"",
                    pDesc->data.dw3.fDD  ? " DD"  :"",
                    pDesc->data.dw3.fEC  ? " EC"  :"",
                    pDesc->data.dw3.fLC  ? " LC"  :"",
                    pDesc->data.dw3.fTXSM? " TXSM":"",
                    pDesc->data.dw3.fIXSM? " IXSM":"",
                    E1K_SPEC_CFI(pDesc->data.dw3.u16Special) ? "CFI" :"cfi",
                    E1K_SPEC_VLAN(pDesc->data.dw3.u16Special),
                    E1K_SPEC_PRI(pDesc->data.dw3.u16Special)));
            break;
        case E1K_DTYP_LEGACY:
            E1kLogX(uLevel, ("%s %s Legacy Transmit Descriptor (%d bytes) %s\n",
                    pThis->szPrf, pszDir, pDesc->legacy.cmd.u16Length, pszDir));
            E1kLogX(uLevel, ("        Address=%16LX DTALEN=%05X\n",
                    pDesc->data.u64BufAddr,
                    pDesc->legacy.cmd.u16Length));
            E1kLogX(uLevel, ("        CMD:%s%s%s%s%s%s%s STA:%s%s%s CSO=%02x CSS=%02x SPECIAL:%s VLAN=%03x PRI=%x\n",
                    pDesc->legacy.cmd.fIDE ? " IDE" :"",
                    pDesc->legacy.cmd.fVLE ? " VLE" :"",
                    pDesc->legacy.cmd.fRPS ? " RPS" :"",
                    pDesc->legacy.cmd.fRS  ? " RS"  :"",
                    pDesc->legacy.cmd.fIC  ? " IC"  :"",
                    pDesc->legacy.cmd.fIFCS? " IFCS":"",
                    pDesc->legacy.cmd.fEOP ? " EOP" :"",
                    pDesc->legacy.dw3.fDD  ? " DD"  :"",
                    pDesc->legacy.dw3.fEC  ? " EC"  :"",
                    pDesc->legacy.dw3.fLC  ? " LC"  :"",
                    pDesc->legacy.cmd.u8CSO,
                    pDesc->legacy.dw3.u8CSS,
                    E1K_SPEC_CFI(pDesc->legacy.dw3.u16Special) ? "CFI" :"cfi",
                    E1K_SPEC_VLAN(pDesc->legacy.dw3.u16Special),
                    E1K_SPEC_PRI(pDesc->legacy.dw3.u16Special)));
            break;
        default:
            E1kLog(("%s %s Invalid Transmit Descriptor %s\n",
                    pThis->szPrf, pszDir, pszDir));
            break;
    }
}

/**
 * Raise an interrupt later.
 *
 * @param   pThis       The device state structure.
 */
DECLINLINE(void) e1kPostponeInterrupt(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint64_t nsDeadline)
{
    if (!PDMDevHlpTimerIsActive(pDevIns, pThis->hIntTimer))
        PDMDevHlpTimerSetNano(pDevIns, pThis->hIntTimer, nsDeadline);
}

/**
 * Raise interrupt if not masked.
 *
 * @param   pThis       The device state structure.
 */
static int e1kRaiseInterrupt(PPDMDEVINS pDevIns, PE1KSTATE pThis, int rcBusy, uint32_t u32IntCause)
{
    /* Do NOT use e1kCsEnterReturn here as most callers doesn't check the
       status code.  They'll pass a negative rcBusy. */
    int rc = e1kCsEnter(pThis, rcBusy);
    if (RT_LIKELY(rc == VINF_SUCCESS))
    { /* likely */ }
    else
    {
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->cs, rc);
        return rc;
    }

    E1K_INC_ISTAT_CNT(pThis->uStatIntTry);
    ICR |= u32IntCause;
    if (ICR & IMS)
    {
        if (pThis->fIntRaised)
        {
            E1K_INC_ISTAT_CNT(pThis->uStatIntSkip);
            E1kLog2(("%s e1kRaiseInterrupt: Already raised, skipped. ICR&IMS=%08x\n",
                    pThis->szPrf, ICR & IMS));
        }
        else
        {
            uint64_t tsNow = PDMDevHlpTimerGet(pDevIns, pThis->hIntTimer);
            if (!!ITR && tsNow - pThis->u64AckedAt < ITR * 256
                && pThis->fItrEnabled && (pThis->fItrRxEnabled || !(ICR & ICR_RXT0)))
            {
                E1K_INC_ISTAT_CNT(pThis->uStatIntEarly);
                E1kLog2(("%s e1kRaiseInterrupt: Too early to raise again: %d ns < %d ns.\n",
                        pThis->szPrf, (uint32_t)(tsNow - pThis->u64AckedAt), ITR * 256));
                e1kPostponeInterrupt(pDevIns, pThis, ITR * 256);
            }
            else
            {

                /* Since we are delivering the interrupt now
                 * there is no need to do it later -- stop the timer.
                 */
                PDMDevHlpTimerStop(pDevIns, pThis->hIntTimer);
                E1K_INC_ISTAT_CNT(pThis->uStatInt);
                STAM_COUNTER_INC(&pThis->StatIntsRaised);
                /* Got at least one unmasked interrupt cause */
                pThis->fIntRaised = true;
                /* Raise(1) INTA(0) */
                E1kLogRel(("E1000: irq RAISED icr&mask=0x%x, icr=0x%x\n", ICR & IMS, ICR));
                PDMDevHlpPCISetIrq(pDevIns, 0, 1);
                E1kLog(("%s e1kRaiseInterrupt: Raised. ICR&IMS=%08x\n",
                        pThis->szPrf, ICR & IMS));
            }
        }
    }
    else
    {
        E1K_INC_ISTAT_CNT(pThis->uStatIntMasked);
        E1kLog2(("%s e1kRaiseInterrupt: Not raising, ICR=%08x, IMS=%08x\n",
                pThis->szPrf, ICR, IMS));
    }
    e1kCsLeave(pThis);
    return VINF_SUCCESS;
}

/**
 * Compute the physical address of the descriptor.
 *
 * @returns the physical address of the descriptor.
 *
 * @param   baseHigh        High-order 32 bits of descriptor table address.
 * @param   baseLow         Low-order 32 bits of descriptor table address.
 * @param   idxDesc         The descriptor index in the table.
 */
DECLINLINE(RTGCPHYS) e1kDescAddr(uint32_t baseHigh, uint32_t baseLow, uint32_t idxDesc)
{
    AssertCompile(sizeof(E1KRXDESC) == sizeof(E1KTXDESC));
    return ((uint64_t)baseHigh << 32) + baseLow + idxDesc * sizeof(E1KRXDESC);
}

#ifdef IN_RING3 /* currently only used in ring-3 due to stack space requirements of the caller */
/**
 * Advance the head pointer of the receive descriptor queue.
 *
 * @remarks RDH always points to the next available RX descriptor.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 */
DECLINLINE(void) e1kAdvanceRDH(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KRXDC pRxdc)
{
    Assert(e1kCsRxIsOwner(pThis));
    //e1kR3CsEnterAsserted(pThis);
    if (++pRxdc->rdh * sizeof(E1KRXDESC) >= pRxdc->rdlen)
        pRxdc->rdh = 0;
    RDH = pRxdc->rdh; /* Sync the actual register and RXDC */
#ifdef E1K_WITH_RXD_CACHE
    /*
     * We need to fetch descriptors now as the guest may advance RDT all the way
     * to RDH as soon as we generate RXDMT0 interrupt. This is mostly to provide
     * compatibility with Phar Lap ETS, see @bugref(7346). Note that we do not
     * check if the receiver is enabled. It must be, otherwise we won't get here
     * in the first place.
     *
     * Note that we should have moved both RDH and iRxDCurrent by now.
     */
    if (e1kRxDIsCacheEmpty(pThis))
    {
        /* Cache is empty, reset it and check if we can fetch more. */
        pThis->iRxDCurrent = pThis->nRxDFetched = 0;
        E1kLog3(("%s e1kAdvanceRDH: Rx cache is empty, RDH=%x RDT=%x "
                 "iRxDCurrent=%x nRxDFetched=%x\n",
                 pThis->szPrf, pRxdc->rdh, pRxdc->rdt, pThis->iRxDCurrent, pThis->nRxDFetched));
        e1kRxDPrefetch(pDevIns, pThis, pRxdc);
    }
#endif /* E1K_WITH_RXD_CACHE */
    /*
     * Compute current receive queue length and fire RXDMT0 interrupt
     * if we are low on receive buffers
     */
    uint32_t uRQueueLen = pRxdc->rdh>pRxdc->rdt ? pRxdc->rdlen/sizeof(E1KRXDESC)-pRxdc->rdh+pRxdc->rdt : pRxdc->rdt-pRxdc->rdh;
    /*
     * The minimum threshold is controlled by RDMTS bits of RCTL:
     * 00 = 1/2 of RDLEN
     * 01 = 1/4 of RDLEN
     * 10 = 1/8 of RDLEN
     * 11 = reserved
     */
    uint32_t uMinRQThreshold = pRxdc->rdlen / sizeof(E1KRXDESC) / (2 << GET_BITS(RCTL, RDMTS));
    if (uRQueueLen <= uMinRQThreshold)
    {
        E1kLogRel(("E1000: low on RX descriptors, RDH=%x RDT=%x len=%x threshold=%x\n", pRxdc->rdh, pRxdc->rdt, uRQueueLen, uMinRQThreshold));
        E1kLog2(("%s Low on RX descriptors, RDH=%x RDT=%x len=%x threshold=%x, raise an interrupt\n",
                 pThis->szPrf, pRxdc->rdh, pRxdc->rdt, uRQueueLen, uMinRQThreshold));
        E1K_INC_ISTAT_CNT(pThis->uStatIntRXDMT0);
        e1kRaiseInterrupt(pDevIns, pThis, VERR_SEM_BUSY, ICR_RXDMT0);
    }
    E1kLog2(("%s e1kAdvanceRDH: at exit RDH=%x RDT=%x len=%x\n",
             pThis->szPrf, pRxdc->rdh, pRxdc->rdt, uRQueueLen));
    //e1kCsLeave(pThis);
}
#endif /* IN_RING3 */

#ifdef E1K_WITH_RXD_CACHE

# ifdef IN_RING3 /* currently only used in ring-3 due to stack space requirements of the caller */

/**
 * Obtain the next RX descriptor from RXD cache, fetching descriptors from the
 * RX ring if the cache is empty.
 *
 * Note that we cannot advance the cache pointer (iRxDCurrent) yet as it will
 * go out of sync with RDH which will cause trouble when EMT checks if the
 * cache is empty to do pre-fetch @bugref(6217).
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @thread  RX
 */
DECLINLINE(E1KRXDESC *) e1kRxDGet(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KRXDC pRxdc)
{
    Assert(e1kCsRxIsOwner(pThis));
    /* Check the cache first. */
    if (pThis->iRxDCurrent < pThis->nRxDFetched)
        return &pThis->aRxDescriptors[pThis->iRxDCurrent];
    /* Cache is empty, reset it and check if we can fetch more. */
    pThis->iRxDCurrent = pThis->nRxDFetched = 0;
    if (e1kRxDPrefetch(pDevIns, pThis, pRxdc))
        return &pThis->aRxDescriptors[pThis->iRxDCurrent];
    /* Out of Rx descriptors. */
    return NULL;
}


/**
 * Return the RX descriptor obtained with e1kRxDGet() and advance the cache
 * pointer. The descriptor gets written back to the RXD ring.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @param   pDesc       The descriptor being "returned" to the RX ring.
 * @thread  RX
 */
DECLINLINE(void) e1kRxDPut(PPDMDEVINS pDevIns, PE1KSTATE pThis, E1KRXDESC* pDesc, PE1KRXDC pRxdc)
{
    Assert(e1kCsRxIsOwner(pThis));
    pThis->iRxDCurrent++;
    // Assert(pDesc >= pThis->aRxDescriptors);
    // Assert(pDesc < pThis->aRxDescriptors + E1K_RXD_CACHE_SIZE);
    // uint64_t addr = e1kDescAddr(RDBAH, RDBAL, RDH);
    // uint32_t rdh = RDH;
    // Assert(pThis->aRxDescAddr[pDesc - pThis->aRxDescriptors] == addr);
    PDMDevHlpPCIPhysWrite(pDevIns, e1kDescAddr(RDBAH, RDBAL, pRxdc->rdh), pDesc, sizeof(E1KRXDESC));
    /*
     * We need to print the descriptor before advancing RDH as it may fetch new
     * descriptors into the cache.
     */
    e1kPrintRDesc(pThis, pDesc);
    e1kAdvanceRDH(pDevIns, pThis, pRxdc);
}

/**
 * Store a fragment of received packet at the specifed address.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The device state structure.
 * @param   pDesc           The next available RX descriptor.
 * @param   pvBuf           The fragment.
 * @param   cb              The size of the fragment.
 */
static void e1kStoreRxFragment(PPDMDEVINS pDevIns, PE1KSTATE pThis, E1KRXDESC *pDesc, const void *pvBuf, size_t cb)
{
    STAM_PROFILE_ADV_START(&pThis->StatReceiveStore, a);
    E1kLog2(("%s e1kStoreRxFragment: store fragment of %04X at %016LX, EOP=%d\n",
             pThis->szPrf, cb, pDesc->u64BufAddr, pDesc->status.fEOP));
    PDMDevHlpPCIPhysWrite(pDevIns, pDesc->u64BufAddr, pvBuf, cb);
    pDesc->u16Length = (uint16_t)cb;
    Assert(pDesc->u16Length == cb);
    STAM_PROFILE_ADV_STOP(&pThis->StatReceiveStore, a);
    RT_NOREF(pThis);
}

# endif /* IN_RING3 */

#else /* !E1K_WITH_RXD_CACHE */

/**
 * Store a fragment of received packet that fits into the next available RX
 * buffer.
 *
 * @remarks Trigger the RXT0 interrupt if it is the last fragment of the packet.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The device state structure.
 * @param   pDesc           The next available RX descriptor.
 * @param   pvBuf           The fragment.
 * @param   cb              The size of the fragment.
 */
static void e1kStoreRxFragment(PPDMDEVINS pDevIns, PE1KSTATE pThis, E1KRXDESC *pDesc, const void *pvBuf, size_t cb)
{
    STAM_PROFILE_ADV_START(&pThis->StatReceiveStore, a);
    E1kLog2(("%s e1kStoreRxFragment: store fragment of %04X at %016LX, EOP=%d\n", pThis->szPrf, cb, pDesc->u64BufAddr, pDesc->status.fEOP));
    PDMDevHlpPCIPhysWrite(pDevIns, pDesc->u64BufAddr, pvBuf, cb);
    pDesc->u16Length = (uint16_t)cb;                        Assert(pDesc->u16Length == cb);
    /* Write back the descriptor */
    PDMDevHlpPCIPhysWrite(pDevIns, e1kDescAddr(RDBAH, RDBAL, RDH), pDesc, sizeof(E1KRXDESC));
    e1kPrintRDesc(pThis, pDesc);
    E1kLogRel(("E1000: Wrote back RX desc, RDH=%x\n", RDH));
    /* Advance head */
    e1kAdvanceRDH(pDevIns, pThis);
    //E1kLog2(("%s e1kStoreRxFragment: EOP=%d RDTR=%08X RADV=%08X\n", pThis->szPrf, pDesc->fEOP, RDTR, RADV));
    if (pDesc->status.fEOP)
    {
        /* Complete packet has been stored -- it is time to let the guest know. */
#ifdef E1K_USE_RX_TIMERS
        if (RDTR)
        {
            /* Arm the timer to fire in RDTR usec (discard .024) */
            e1kArmTimer(pDevIns, pThis, pThis->hRIDTimer, RDTR);
            /* If absolute timer delay is enabled and the timer is not running yet, arm it. */
            if (RADV != 0 && !PDMDevHlpTimerIsActive(pDevIns, pThis->CTX_SUFF(pRADTimer)))
                e1kArmTimer(pThis, pThis->hRADTimer, RADV);
        }
        else
        {
#endif
            /* 0 delay means immediate interrupt */
            E1K_INC_ISTAT_CNT(pThis->uStatIntRx);
            e1kRaiseInterrupt(pDevIns, pThis, VERR_SEM_BUSY, ICR_RXT0);
#ifdef E1K_USE_RX_TIMERS
        }
#endif
    }
    STAM_PROFILE_ADV_STOP(&pThis->StatReceiveStore, a);
}

#endif /* !E1K_WITH_RXD_CACHE */

/**
 * Returns true if it is a broadcast packet.
 *
 * @returns true if destination address indicates broadcast.
 * @param   pvBuf           The ethernet packet.
 */
DECLINLINE(bool) e1kIsBroadcast(const void *pvBuf)
{
    static const uint8_t s_abBcastAddr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    return memcmp(pvBuf, s_abBcastAddr, sizeof(s_abBcastAddr)) == 0;
}

/**
 * Returns true if it is a multicast packet.
 *
 * @remarks returns true for broadcast packets as well.
 * @returns true if destination address indicates multicast.
 * @param   pvBuf           The ethernet packet.
 */
DECLINLINE(bool) e1kIsMulticast(const void *pvBuf)
{
    return (*(char*)pvBuf) & 1;
}

#ifdef IN_RING3 /* currently only used in ring-3 due to stack space requirements of the caller */
/**
 * Set IXSM, IPCS and TCPCS flags according to the packet type.
 *
 * @remarks We emulate checksum offloading for major packets types only.
 *
 * @returns VBox status code.
 * @param   pThis          The device state structure.
 * @param   pFrame          The available data.
 * @param   cb              Number of bytes available in the buffer.
 * @param   status          Bit fields containing status info.
 */
static int e1kRxChecksumOffload(PE1KSTATE pThis, const uint8_t *pFrame, size_t cb, E1KRXDST *pStatus)
{
    /** @todo
     * It is not safe to bypass checksum verification for packets coming
     * from real wire. We currently unable to tell where packets are
     * coming from so we tell the driver to ignore our checksum flags
     * and do verification in software.
     */
# if 0
    uint16_t uEtherType = ntohs(*(uint16_t*)(pFrame + 12));

    E1kLog2(("%s e1kRxChecksumOffload: EtherType=%x\n", pThis->szPrf, uEtherType));

    switch (uEtherType)
    {
        case 0x800: /* IPv4 */
        {
            pStatus->fIXSM  = false;
            pStatus->fIPCS  = true;
            PRTNETIPV4 pIpHdr4 = (PRTNETIPV4)(pFrame + 14);
            /* TCP/UDP checksum offloading works with TCP and UDP only */
            pStatus->fTCPCS = pIpHdr4->ip_p == 6 || pIpHdr4->ip_p == 17;
            break;
        }
        case 0x86DD: /* IPv6 */
            pStatus->fIXSM = false;
            pStatus->fIPCS  = false;
            pStatus->fTCPCS = true;
            break;
        default: /* ARP, VLAN, etc. */
            pStatus->fIXSM = true;
            break;
    }
# else
    pStatus->fIXSM = true;
    RT_NOREF_PV(pThis); RT_NOREF_PV(pFrame); RT_NOREF_PV(cb);
# endif
    return VINF_SUCCESS;
}
#endif /* IN_RING3 */

/**
 * Pad and store received packet.
 *
 * @remarks Make sure that the packet appears to upper layer as one coming
 *          from real Ethernet: pad it and insert FCS.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pThis           The device state structure.
 * @param   pvBuf           The available data.
 * @param   cb              Number of bytes available in the buffer.
 * @param   status          Bit fields containing status info.
 */
static int e1kHandleRxPacket(PPDMDEVINS pDevIns, PE1KSTATE pThis, const void *pvBuf, size_t cb, E1KRXDST status)
{
#if defined(IN_RING3) /** @todo Remove this extra copying, it's gonna make us run out of kernel / hypervisor stack! */
    uint8_t   rxPacket[E1K_MAX_RX_PKT_SIZE];
    uint8_t  *ptr = rxPacket;
# ifdef E1K_WITH_RXD_CACHE
    E1KRXDC   rxdc;
# endif /* E1K_WITH_RXD_CACHE */

    e1kCsRxEnterReturn(pThis);
# ifdef E1K_WITH_RXD_CACHE
    if (RT_UNLIKELY(!e1kUpdateRxDContext(pDevIns, pThis, &rxdc, "e1kHandleRxPacket")))
    {
        e1kCsRxLeave(pThis);
        E1kLog(("%s e1kHandleRxPacket: failed to update Rx context, returning VINF_SUCCESS\n",  pThis->szPrf));
        return VINF_SUCCESS;
    }
# endif /* E1K_WITH_RXD_CACHE */

    if (cb > 70) /* unqualified guess */
        pThis->led.Asserted.s.fReading = pThis->led.Actual.s.fReading = 1;

    Assert(cb <= E1K_MAX_RX_PKT_SIZE);
    Assert(cb > 16);
    size_t cbMax = ((RCTL & RCTL_LPE) ? E1K_MAX_RX_PKT_SIZE - 4 : 1518) - (status.fVP ? 0 : 4);
    E1kLog3(("%s Max RX packet size is %u\n", pThis->szPrf, cbMax));
    if (status.fVP)
    {
        /* VLAN packet -- strip VLAN tag in VLAN mode */
        if ((CTRL & CTRL_VME) && cb > 16)
        {
            uint16_t *u16Ptr = (uint16_t*)pvBuf;
            memcpy(rxPacket, pvBuf, 12); /* Copy src and dst addresses */
            status.u16Special = RT_BE2H_U16(u16Ptr[7]); /* Extract VLAN tag */
            memcpy(rxPacket + 12, (uint8_t*)pvBuf + 16, cb - 16); /* Copy the rest of the packet */
            cb -= 4;
            E1kLog3(("%s Stripped tag for VLAN %u (cb=%u)\n",
                     pThis->szPrf, status.u16Special, cb));
        }
        else
        {
            status.fVP = false; /* Set VP only if we stripped the tag */
            memcpy(rxPacket, pvBuf, cb);
        }
    }
    else
        memcpy(rxPacket, pvBuf, cb);
    /* Pad short packets */
    if (cb < 60)
    {
        memset(rxPacket + cb, 0, 60 - cb);
        cb = 60;
    }
    if (!(RCTL & RCTL_SECRC) && cb <= cbMax)
    {
        STAM_PROFILE_ADV_START(&pThis->StatReceiveCRC, a);
        /*
         * Add FCS if CRC stripping is not enabled. Since the value of CRC
         * is ignored by most of drivers we may as well save us the trouble
         * of calculating it (see EthernetCRC CFGM parameter).
         */
        if (pThis->fEthernetCRC)
            *(uint32_t*)(rxPacket + cb) = RTCrc32(rxPacket, cb);
        cb += sizeof(uint32_t);
        STAM_PROFILE_ADV_STOP(&pThis->StatReceiveCRC, a);
        E1kLog3(("%s Added FCS (cb=%u)\n", pThis->szPrf, cb));
    }
    /* Compute checksum of complete packet */
    size_t cbCSumStart = RT_MIN(GET_BITS(RXCSUM, PCSS), cb);
    uint16_t checksum = e1kCSum16(rxPacket + cbCSumStart, cb - cbCSumStart);
    e1kRxChecksumOffload(pThis, rxPacket, cb, &status);

    /* Update stats */
    E1K_INC_CNT32(GPRC);
    if (e1kIsBroadcast(pvBuf))
        E1K_INC_CNT32(BPRC);
    else if (e1kIsMulticast(pvBuf))
        E1K_INC_CNT32(MPRC);
    /* Update octet receive counter */
    E1K_ADD_CNT64(GORCL, GORCH, cb);
    STAM_REL_COUNTER_ADD(&pThis->StatReceiveBytes, cb);
    if (cb == 64)
        E1K_INC_CNT32(PRC64);
    else if (cb < 128)
        E1K_INC_CNT32(PRC127);
    else if (cb < 256)
        E1K_INC_CNT32(PRC255);
    else if (cb < 512)
        E1K_INC_CNT32(PRC511);
    else if (cb < 1024)
        E1K_INC_CNT32(PRC1023);
    else
        E1K_INC_CNT32(PRC1522);

    E1K_INC_ISTAT_CNT(pThis->uStatRxFrm);

# ifdef E1K_WITH_RXD_CACHE
    while (cb > 0)
    {
        E1KRXDESC *pDesc = e1kRxDGet(pDevIns, pThis, &rxdc);

        if (pDesc == NULL)
        {
            E1kLog(("%s Out of receive buffers, dropping the packet "
                    "(cb=%u, in_cache=%u, RDH=%x RDT=%x)\n",
                    pThis->szPrf, cb, e1kRxDInCache(pThis), rxdc.rdh, rxdc.rdt));
            break;
        }
# else /* !E1K_WITH_RXD_CACHE */
    if (RDH == RDT)
    {
        E1kLog(("%s Out of receive buffers, dropping the packet\n",
                pThis->szPrf));
    }
    /* Store the packet to receive buffers */
    while (RDH != RDT)
    {
        /* Load the descriptor pointed by head */
        E1KRXDESC desc, *pDesc = &desc;
        PDMDevHlpPCIPhysRead(pDevIns, e1kDescAddr(RDBAH, RDBAL, RDH), &desc, sizeof(desc));
# endif /* !E1K_WITH_RXD_CACHE */
        if (pDesc->u64BufAddr)
        {
            uint16_t u16RxBufferSize = pThis->u16RxBSize; /* see @bugref{9427} */

            /* Update descriptor */
            pDesc->status        = status;
            pDesc->u16Checksum   = checksum;
            pDesc->status.fDD    = true;

            /*
             * We need to leave Rx critical section here or we risk deadlocking
             * with EMT in e1kRegWriteRDT when the write is to an unallocated
             * page or has an access handler associated with it.
             * Note that it is safe to leave the critical section here since
             * e1kRegWriteRDT() never modifies RDH. It never touches already
             * fetched RxD cache entries either.
             */
            if (cb > u16RxBufferSize)
            {
                pDesc->status.fEOP = false;
                e1kCsRxLeave(pThis);
                e1kStoreRxFragment(pDevIns, pThis, pDesc, ptr, u16RxBufferSize);
                e1kCsRxEnterReturn(pThis);
# ifdef E1K_WITH_RXD_CACHE
                if (RT_UNLIKELY(!e1kUpdateRxDContext(pDevIns, pThis, &rxdc, "e1kHandleRxPacket")))
                {
                    e1kCsRxLeave(pThis);
                    E1kLog(("%s e1kHandleRxPacket: failed to update Rx context, returning VINF_SUCCESS\n",  pThis->szPrf));
                    return VINF_SUCCESS;
                }
# endif /* E1K_WITH_RXD_CACHE */
                ptr += u16RxBufferSize;
                cb -= u16RxBufferSize;
            }
            else
            {
                pDesc->status.fEOP = true;
                e1kCsRxLeave(pThis);
                e1kStoreRxFragment(pDevIns, pThis, pDesc, ptr, cb);
# ifdef E1K_WITH_RXD_CACHE
                e1kCsRxEnterReturn(pThis);
                if (RT_UNLIKELY(!e1kUpdateRxDContext(pDevIns, pThis, &rxdc, "e1kHandleRxPacket")))
                {
                    e1kCsRxLeave(pThis);
                    E1kLog(("%s e1kHandleRxPacket: failed to update Rx context, returning VINF_SUCCESS\n",  pThis->szPrf));
                    return VINF_SUCCESS;
                }
                cb = 0;
# else /* !E1K_WITH_RXD_CACHE */
                pThis->led.Actual.s.fReading = 0;
                return VINF_SUCCESS;
# endif /* !E1K_WITH_RXD_CACHE */
            }
            /*
             * Note: RDH is advanced by e1kStoreRxFragment if E1K_WITH_RXD_CACHE
             * is not defined.
             */
        }
# ifdef E1K_WITH_RXD_CACHE
        /* Write back the descriptor. */
        pDesc->status.fDD = true;
        e1kRxDPut(pDevIns, pThis, pDesc, &rxdc);
# else /* !E1K_WITH_RXD_CACHE */
        else
        {
            /* Write back the descriptor. */
            pDesc->status.fDD = true;
            PDMDevHlpPCIPhysWrite(pDevIns, e1kDescAddr(RDBAH, RDBAL, RDH), pDesc, sizeof(E1KRXDESC));
            e1kAdvanceRDH(pDevIns, pThis);
        }
# endif /* !E1K_WITH_RXD_CACHE */
    }

    if (cb > 0)
        E1kLog(("%s Out of receive buffers, dropping %u bytes", pThis->szPrf, cb));

    pThis->led.Actual.s.fReading = 0;

    e1kCsRxLeave(pThis);
# ifdef E1K_WITH_RXD_CACHE
    /* Complete packet has been stored -- it is time to let the guest know. */
#  ifdef E1K_USE_RX_TIMERS
    if (RDTR)
    {
        /* Arm the timer to fire in RDTR usec (discard .024) */
        e1kArmTimer(pThis, pThis->hRIDTimer, RDTR);
        /* If absolute timer delay is enabled and the timer is not running yet, arm it. */
        if (RADV != 0 && !PDMDevHlpTimerIsActive(pDevIns, pThis->hRADTimer))
            e1kArmTimer(pThis, pThis->hRADTimer, RADV);
    }
    else
    {
#  endif /* E1K_USE_RX_TIMERS */
        /* 0 delay means immediate interrupt */
        E1K_INC_ISTAT_CNT(pThis->uStatIntRx);
        e1kRaiseInterrupt(pDevIns, pThis, VERR_SEM_BUSY, ICR_RXT0);
#  ifdef E1K_USE_RX_TIMERS
    }
#  endif /* E1K_USE_RX_TIMERS */
# endif /* E1K_WITH_RXD_CACHE */

    return VINF_SUCCESS;
#else  /* !IN_RING3 */
    RT_NOREF(pDevIns, pThis, pvBuf, cb, status);
    return VERR_INTERNAL_ERROR_2;
#endif /* !IN_RING3 */
}


#ifdef IN_RING3
/**
 * Bring the link up after the configured delay, 5 seconds by default.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @thread  any
 */
DECLINLINE(void) e1kBringLinkUpDelayed(PPDMDEVINS pDevIns, PE1KSTATE pThis)
{
    E1kLog(("%s Will bring up the link in %d seconds...\n",
            pThis->szPrf, pThis->cMsLinkUpDelay / 1000));
    e1kArmTimer(pDevIns, pThis, pThis->hLUTimer, pThis->cMsLinkUpDelay * 1000);
}

/**
 * Bring up the link immediately.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @param   pThisCC     The current context instance data.
 */
DECLINLINE(void) e1kR3LinkUp(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KSTATECC pThisCC)
{
    E1kLog(("%s Link is up\n", pThis->szPrf));
    STATUS |= STATUS_LU;
    Phy::setLinkStatus(&pThis->phy, true);
    e1kRaiseInterrupt(pDevIns, pThis, VERR_SEM_BUSY, ICR_LSC);
    if (pThisCC->pDrvR3)
        pThisCC->pDrvR3->pfnNotifyLinkChanged(pThisCC->pDrvR3, PDMNETWORKLINKSTATE_UP);
    /* Trigger processing of pending TX descriptors (see @bugref{8942}). */
    PDMDevHlpTaskTrigger(pDevIns, pThis->hTxTask);
}

/**
 * Bring down the link immediately.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @param   pThisCC     The current context instance data.
 */
DECLINLINE(void) e1kR3LinkDown(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KSTATECC pThisCC)
{
    E1kLog(("%s Link is down\n", pThis->szPrf));
    STATUS &= ~STATUS_LU;
#ifdef E1K_LSC_ON_RESET
    Phy::setLinkStatus(&pThis->phy, false);
#endif /* E1K_LSC_ON_RESET */
    e1kRaiseInterrupt(pDevIns, pThis, VERR_SEM_BUSY, ICR_LSC);
    if (pThisCC->pDrvR3)
        pThisCC->pDrvR3->pfnNotifyLinkChanged(pThisCC->pDrvR3, PDMNETWORKLINKSTATE_DOWN);
}

/**
 * Bring down the link temporarily.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @param   pThisCC     The current context instance data.
 */
DECLINLINE(void) e1kR3LinkDownTemp(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KSTATECC pThisCC)
{
    E1kLog(("%s Link is down temporarily\n", pThis->szPrf));
    STATUS &= ~STATUS_LU;
    Phy::setLinkStatus(&pThis->phy, false);
    e1kRaiseInterrupt(pDevIns, pThis, VERR_SEM_BUSY, ICR_LSC);
    /*
     * Notifying the associated driver that the link went down (even temporarily)
     * seems to be the right thing, but it was not done before. This may cause
     * a regression if the driver does not expect the link to go down as a result
     * of sending PDMNETWORKLINKSTATE_DOWN_RESUME to this device. Earlier versions
     * of code notified the driver that the link was up! See @bugref{7057}.
     */
    if (pThisCC->pDrvR3)
        pThisCC->pDrvR3->pfnNotifyLinkChanged(pThisCC->pDrvR3, PDMNETWORKLINKSTATE_DOWN);
    e1kBringLinkUpDelayed(pDevIns, pThis);
}
#endif /* IN_RING3 */

#if 0 /* unused */
/**
 * Read handler for Device Status register.
 *
 * Get the link status from PHY.
 *
 * @returns VBox status code.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   mask        Used to implement partial reads (8 and 16-bit).
 */
static int e1kRegReadCTRL(PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t *pu32Value)
{
    E1kLog(("%s e1kRegReadCTRL: mdio dir=%s mdc dir=%s mdc=%d\n",
            pThis->szPrf, (CTRL & CTRL_MDIO_DIR)?"OUT":"IN ",
            (CTRL & CTRL_MDC_DIR)?"OUT":"IN ", !!(CTRL & CTRL_MDC)));
    if ((CTRL & CTRL_MDIO_DIR) == 0 && (CTRL & CTRL_MDC))
    {
        /* MDC is high and MDIO pin is used for input, read MDIO pin from PHY */
        if (Phy::readMDIO(&pThis->phy))
            *pu32Value = CTRL | CTRL_MDIO;
        else
            *pu32Value = CTRL & ~CTRL_MDIO;
        E1kLog(("%s e1kRegReadCTRL: Phy::readMDIO(%d)\n",
                pThis->szPrf, !!(*pu32Value & CTRL_MDIO)));
    }
    else
    {
        /* MDIO pin is used for output, ignore it */
        *pu32Value = CTRL;
    }
    return VINF_SUCCESS;
}
#endif /* unused */

/**
 * A helper function to detect the link state to the other side of "the wire".
 *
 * When deciding to bring up the link we need to take into account both if the
 * cable is connected and if our device is actually connected to the outside
 * world. If no driver is attached we won't be able to allocate TX buffers,
 * which will prevent us from TX descriptor processing, which will result in
 * "TX unit hang" in the guest.
 *
 * @returns true if the device is connected to something.
 *
 * @param   pDevIns     The device instance.
 */
DECLINLINE(bool) e1kIsConnected(PPDMDEVINS pDevIns)
{
    PE1KSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    return pThis->fCableConnected && pThis->fIsAttached;
}

/**
 * A callback used by PHY to indicate that the link needs to be updated due to
 * reset of PHY.
 *
 * @param   pDevIns     The device instance.
 * @thread  any
 */
void e1kPhyLinkResetCallback(PPDMDEVINS pDevIns)
{
    PE1KSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);

    /* Make sure we have cable connected and MAC can talk to PHY */
    if (e1kIsConnected(pDevIns) && (CTRL & CTRL_SLU))
        e1kArmTimer(pDevIns, pThis, pThis->hLUTimer, E1K_INIT_LINKUP_DELAY_US);
    else
        Log(("%s PHY link reset callback ignored (cable %sconnected, driver %stached, CTRL_SLU=%u)\n", pThis->szPrf,
             pThis->fCableConnected ? "" : "dis", pThis->fIsAttached ? "at" : "de", CTRL & CTRL_SLU ? 1 : 0));
}

/**
 * Write handler for Device Control register.
 *
 * Handles reset.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWriteCTRL(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    int rc = VINF_SUCCESS;

    if (value & CTRL_RESET)
    { /* RST */
#ifndef IN_RING3
        return VINF_IOM_R3_MMIO_WRITE;
#else
        e1kR3HardReset(pDevIns, pThis, PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC));
#endif
    }
    else
    {
#ifdef E1K_LSC_ON_SLU
        /*
         * When the guest changes 'Set Link Up' bit from 0 to 1 we check if
         * the link is down and the cable is connected, and if they are we
         * bring the link up, see @bugref{8624}.
         */
        if (   (value & CTRL_SLU)
            && !(CTRL & CTRL_SLU)
            && pThis->fCableConnected
            && !(STATUS & STATUS_LU))
        {
            /* It should take about 2 seconds for the link to come up */
            e1kArmTimer(pDevIns, pThis, pThis->hLUTimer, E1K_INIT_LINKUP_DELAY_US);
        }
#else /* !E1K_LSC_ON_SLU */
        if (   (value & CTRL_SLU)
            && !(CTRL & CTRL_SLU)
            && e1kIsConnected(pDevIns)
            && !PDMDevHlpTimerIsActive(pDevIns, pThis->hLUTimer))
        {
            /* PXE does not use LSC interrupts, see @bugref{9113}. */
            STATUS |= STATUS_LU;
        }
#endif /* !E1K_LSC_ON_SLU */
        if ((value & CTRL_VME) != (CTRL & CTRL_VME))
        {
            E1kLog(("%s VLAN Mode %s\n", pThis->szPrf, (value & CTRL_VME) ? "Enabled" : "Disabled"));
        }
        Log7(("%s e1kRegWriteCTRL: mdio dir=%s mdc dir=%s mdc=%s mdio=%d\n",
              pThis->szPrf, (value & CTRL_MDIO_DIR)?"OUT":"IN ",
              (value & CTRL_MDC_DIR)?"OUT":"IN ", (value & CTRL_MDC)?"HIGH":"LOW ", !!(value & CTRL_MDIO)));
        if (value & CTRL_MDC)
        {
            if (value & CTRL_MDIO_DIR)
            {
                Log7(("%s e1kRegWriteCTRL: Phy::writeMDIO(%d)\n", pThis->szPrf, !!(value & CTRL_MDIO)));
                /* MDIO direction pin is set to output and MDC is high, write MDIO pin value to PHY */
                Phy::writeMDIO(&pThis->phy, !!(value & CTRL_MDIO), pDevIns);
            }
            else
            {
                if (Phy::readMDIO(&pThis->phy))
                    value |= CTRL_MDIO;
                else
                    value &= ~CTRL_MDIO;
                Log7(("%s e1kRegWriteCTRL: Phy::readMDIO(%d)\n", pThis->szPrf, !!(value & CTRL_MDIO)));
            }
        }
        rc = e1kRegWriteDefault(pDevIns, pThis, offset, index, value);
    }

    return rc;
}

/**
 * Write handler for EEPROM/Flash Control/Data register.
 *
 * Handles EEPROM access requests; forwards writes to EEPROM device if access has been granted.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWriteEECD(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    RT_NOREF(pDevIns, offset, index);
#ifdef IN_RING3
    /* So far we are concerned with lower byte only */
    if ((EECD & EECD_EE_GNT) || pThis->eChip == E1K_CHIP_82543GC)
    {
        /* Access to EEPROM granted -- forward 4-wire bits to EEPROM device */
        /* Note: 82543GC does not need to request EEPROM access */
        STAM_PROFILE_ADV_START(&pThis->StatEEPROMWrite, a);
        PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
        pThisCC->eeprom.write(value & EECD_EE_WIRES);
        STAM_PROFILE_ADV_STOP(&pThis->StatEEPROMWrite, a);
    }
    if (value & EECD_EE_REQ)
        EECD |= EECD_EE_REQ|EECD_EE_GNT;
    else
        EECD &= ~EECD_EE_GNT;
    //e1kRegWriteDefault(pThis, offset, index, value );

    return VINF_SUCCESS;
#else /* !IN_RING3 */
    RT_NOREF(pThis, value);
    return VINF_IOM_R3_MMIO_WRITE;
#endif /* !IN_RING3 */
}

/**
 * Read handler for EEPROM/Flash Control/Data register.
 *
 * Lower 4 bits come from EEPROM device if EEPROM access has been granted.
 *
 * @returns VBox status code.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   mask        Used to implement partial reads (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegReadEECD(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t *pu32Value)
{
#ifdef IN_RING3
    uint32_t value = 0; /* Get rid of false positive in parfait. */
    int      rc = e1kRegReadDefault(pDevIns, pThis, offset, index, &value);
    if (RT_SUCCESS(rc))
    {
        if ((value & EECD_EE_GNT) || pThis->eChip == E1K_CHIP_82543GC)
        {
            /* Note: 82543GC does not need to request EEPROM access */
            /* Access to EEPROM granted -- get 4-wire bits to EEPROM device */
            STAM_PROFILE_ADV_START(&pThis->StatEEPROMRead, a);
            PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
            value |= pThisCC->eeprom.read();
            STAM_PROFILE_ADV_STOP(&pThis->StatEEPROMRead, a);
        }
        *pu32Value = value;
    }

    return rc;
#else /* !IN_RING3 */
    RT_NOREF_PV(pDevIns); RT_NOREF_PV(pThis); RT_NOREF_PV(offset); RT_NOREF_PV(index); RT_NOREF_PV(pu32Value);
    return VINF_IOM_R3_MMIO_READ;
#endif /* !IN_RING3 */
}

/**
 * Write handler for EEPROM Read register.
 *
 * Handles EEPROM word access requests, reads EEPROM and stores the result
 * into DATA field.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWriteEERD(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
#ifdef IN_RING3
    /* Make use of 'writable' and 'readable' masks. */
    e1kRegWriteDefault(pDevIns, pThis, offset, index, value);
    /* DONE and DATA are set only if read was triggered by START. */
    if (value & EERD_START)
    {
        STAM_PROFILE_ADV_START(&pThis->StatEEPROMRead, a);
        uint16_t    tmp;
        PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
        if (pThisCC->eeprom.readWord(GET_BITS_V(value, EERD, ADDR), &tmp))
            SET_BITS(EERD, DATA, tmp);
        EERD |= EERD_DONE;
        STAM_PROFILE_ADV_STOP(&pThis->StatEEPROMRead, a);
    }

    return VINF_SUCCESS;
#else /* !IN_RING3 */
    RT_NOREF_PV(pDevIns); RT_NOREF_PV(pThis); RT_NOREF_PV(offset); RT_NOREF_PV(index); RT_NOREF_PV(value);
    return VINF_IOM_R3_MMIO_WRITE;
#endif /* !IN_RING3 */
}


/**
 * Write handler for MDI Control register.
 *
 * Handles PHY read/write requests; forwards requests to internal PHY device.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWriteMDIC(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    if (value & MDIC_INT_EN)
    {
        E1kLog(("%s ERROR! Interrupt at the end of an MDI cycle is not supported yet.\n",
                pThis->szPrf));
    }
    else if (value & MDIC_READY)
    {
        E1kLog(("%s ERROR! Ready bit is not reset by software during write operation.\n",
                pThis->szPrf));
    }
    else if (GET_BITS_V(value, MDIC, PHY) != 1)
    {
        E1kLog(("%s WARNING! Access to invalid PHY detected, phy=%d.\n",
                pThis->szPrf, GET_BITS_V(value, MDIC, PHY)));
        /*
         * Some drivers scan the MDIO bus for a PHY. We can work with these
         * drivers if we set MDIC_READY and MDIC_ERROR when there isn't a PHY
         * at the requested address, see @bugref{7346}.
         */
        MDIC = MDIC_READY | MDIC_ERROR;
    }
    else
    {
        /* Store the value */
        e1kRegWriteDefault(pDevIns, pThis, offset, index, value);
        STAM_COUNTER_INC(&pThis->StatPHYAccesses);
        /* Forward op to PHY */
        if (value & MDIC_OP_READ)
            SET_BITS(MDIC, DATA, Phy::readRegister(&pThis->phy, GET_BITS_V(value, MDIC, REG), pDevIns));
        else
            Phy::writeRegister(&pThis->phy, GET_BITS_V(value, MDIC, REG), value & MDIC_DATA_MASK, pDevIns);
        /* Let software know that we are done */
        MDIC |= MDIC_READY;
    }

    return VINF_SUCCESS;
}

/**
 * Write handler for Interrupt Cause Read register.
 *
 * Bits corresponding to 1s in 'value' will be cleared in ICR register.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWriteICR(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    ICR &= ~value;

    RT_NOREF_PV(pDevIns); RT_NOREF_PV(pThis); RT_NOREF_PV(offset); RT_NOREF_PV(index);
    return VINF_SUCCESS;
}

/**
 * Read handler for Interrupt Cause Read register.
 *
 * Reading this register acknowledges all interrupts.
 *
 * @returns VBox status code.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   mask        Not used.
 * @thread  EMT
 */
static int e1kRegReadICR(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t *pu32Value)
{
    e1kCsEnterReturn(pThis, VINF_IOM_R3_MMIO_READ);

    uint32_t value = 0;
    int rc = e1kRegReadDefault(pDevIns, pThis, offset, index, &value);
    if (RT_SUCCESS(rc))
    {
        if (value)
        {
            if (!pThis->fIntRaised)
                E1K_INC_ISTAT_CNT(pThis->uStatNoIntICR);
            /*
             * Not clearing ICR causes QNX to hang as it reads ICR in a loop
             * with disabled interrupts.
             */
            //if (IMS)
            if (1)
            {
                /*
                 * Interrupts were enabled -- we are supposedly at the very
                 * beginning of interrupt handler
                 */
                E1kLogRel(("E1000: irq lowered, icr=0x%x\n", ICR));
                E1kLog(("%s e1kRegReadICR: Lowered IRQ (%08x)\n", pThis->szPrf, ICR));
                /* Clear all pending interrupts */
                ICR = 0;
                pThis->fIntRaised = false;
                /* Lower(0) INTA(0) */
                PDMDevHlpPCISetIrq(pDevIns, 0, 0);

                pThis->u64AckedAt = PDMDevHlpTimerGet(pDevIns, pThis->hIntTimer);
                if (pThis->fIntMaskUsed)
                    pThis->fDelayInts = true;
            }
            else
            {
                /*
                 * Interrupts are disabled -- in windows guests ICR read is done
                 * just before re-enabling interrupts
                 */
                E1kLog(("%s e1kRegReadICR: Suppressing auto-clear due to disabled interrupts (%08x)\n", pThis->szPrf, ICR));
            }
        }
        *pu32Value = value;
    }
    e1kCsLeave(pThis);

    return rc;
}

/**
 * Read handler for Interrupt Cause Set register.
 *
 * VxWorks driver uses this undocumented feature of real H/W to read ICR without acknowledging interrupts.
 *
 * @returns VBox status code.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   pu32Value   Where to store the value of the register.
 * @thread  EMT
 */
static int e1kRegReadICS(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t *pu32Value)
{
    RT_NOREF_PV(index);
    return e1kRegReadDefault(pDevIns, pThis, offset, ICR_IDX, pu32Value);
}

/**
 * Write handler for Interrupt Cause Set register.
 *
 * Bits corresponding to 1s in 'value' will be set in ICR register.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWriteICS(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    RT_NOREF_PV(offset); RT_NOREF_PV(index);
    E1K_INC_ISTAT_CNT(pThis->uStatIntICS);
    return e1kRaiseInterrupt(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE, value & g_aE1kRegMap[ICS_IDX].writable);
}

/**
 * Write handler for Interrupt Mask Set register.
 *
 * Will trigger pending interrupts.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWriteIMS(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    RT_NOREF_PV(offset); RT_NOREF_PV(index);

    IMS |= value;
    E1kLogRel(("E1000: irq enabled, RDH=%x RDT=%x TDH=%x TDT=%x\n", RDH, RDT, TDH, TDT));
    E1kLog(("%s e1kRegWriteIMS: IRQ enabled\n", pThis->szPrf));
    /*
     * We cannot raise an interrupt here as it will occasionally cause an interrupt storm
     * in Windows guests (see @bugref{8624}, @bugref{5023}).
     */
    if ((ICR & IMS) && !pThis->fLocked)
    {
        E1K_INC_ISTAT_CNT(pThis->uStatIntIMS);
        e1kPostponeInterrupt(pDevIns, pThis, E1K_IMS_INT_DELAY_NS);
    }

    return VINF_SUCCESS;
}

/**
 * Write handler for Interrupt Mask Clear register.
 *
 * Bits corresponding to 1s in 'value' will be cleared in IMS register.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWriteIMC(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    RT_NOREF_PV(offset); RT_NOREF_PV(index);

    e1kCsEnterReturn(pThis, VINF_IOM_R3_MMIO_WRITE);
    if (pThis->fIntRaised)
    {
        /*
         * Technically we should reset fIntRaised in ICR read handler, but it will cause
         * Windows to freeze since it may receive an interrupt while still in the very beginning
         * of interrupt handler.
         */
        E1K_INC_ISTAT_CNT(pThis->uStatIntLower);
        STAM_COUNTER_INC(&pThis->StatIntsPrevented);
        E1kLogRel(("E1000: irq lowered (IMC), icr=0x%x\n", ICR));
        /* Lower(0) INTA(0) */
        PDMDevHlpPCISetIrq(pDevIns, 0, 0);
        pThis->fIntRaised = false;
        E1kLog(("%s e1kRegWriteIMC: Lowered IRQ: ICR=%08x\n", pThis->szPrf, ICR));
    }
    IMS &= ~value;
    E1kLog(("%s e1kRegWriteIMC: IRQ disabled\n", pThis->szPrf));
    e1kCsLeave(pThis);

    return VINF_SUCCESS;
}

/**
 * Write handler for Receive Control register.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWriteRCTL(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    /* Update promiscuous mode */
    bool fBecomePromiscous = !!(value & (RCTL_UPE | RCTL_MPE));
    if (fBecomePromiscous != !!( RCTL & (RCTL_UPE | RCTL_MPE)))
    {
        /* Promiscuity has changed, pass the knowledge on. */
#ifndef IN_RING3
        return VINF_IOM_R3_MMIO_WRITE;
#else
        PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
        if (pThisCC->pDrvR3)
            pThisCC->pDrvR3->pfnSetPromiscuousMode(pThisCC->pDrvR3, fBecomePromiscous);
#endif
    }

    /* Adjust receive buffer size */
    unsigned cbRxBuf = 2048 >> GET_BITS_V(value, RCTL, BSIZE);
    if (value & RCTL_BSEX)
        cbRxBuf *= 16;
    if (cbRxBuf > E1K_MAX_RX_PKT_SIZE)
        cbRxBuf = E1K_MAX_RX_PKT_SIZE;
    if (cbRxBuf != pThis->u16RxBSize)
        E1kLog2(("%s e1kRegWriteRCTL: Setting receive buffer size to %d (old %d)\n",
                 pThis->szPrf, cbRxBuf, pThis->u16RxBSize));
    Assert(cbRxBuf < 65536);
    pThis->u16RxBSize = (uint16_t)cbRxBuf;

    /* Update the register */
    return e1kRegWriteDefault(pDevIns, pThis, offset, index, value);
}

/**
 * Write handler for Packet Buffer Allocation register.
 *
 * TXA = 64 - RXA.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWritePBA(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    e1kRegWriteDefault(pDevIns, pThis, offset, index, value);
    PBA_st->txa = 64 - PBA_st->rxa;

    return VINF_SUCCESS;
}

/**
 * Write handler for Receive Descriptor Tail register.
 *
 * @remarks Write into RDT forces switch to HC and signal to
 *          e1kR3NetworkDown_WaitReceiveAvail().
 *
 * @returns VBox status code.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWriteRDT(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
#ifndef IN_RING3
    /* XXX */
//    return VINF_IOM_R3_MMIO_WRITE;
#endif
    int rc = e1kCsRxEnter(pThis, VINF_IOM_R3_MMIO_WRITE);
    if (RT_LIKELY(rc == VINF_SUCCESS))
    {
        E1kLog(("%s e1kRegWriteRDT\n",  pThis->szPrf));
#ifndef E1K_WITH_RXD_CACHE
        /*
         * Some drivers advance RDT too far, so that it equals RDH. This
         * somehow manages to work with real hardware but not with this
         * emulated device. We can work with these drivers if we just
         * write 1 less when we see a driver writing RDT equal to RDH,
         * see @bugref{7346}.
         */
        if (value == RDH)
        {
            if (RDH == 0)
                value = (RDLEN / sizeof(E1KRXDESC)) - 1;
            else
                value = RDH - 1;
        }
#endif /* !E1K_WITH_RXD_CACHE */
        rc = e1kRegWriteDefault(pDevIns, pThis, offset, index, value);
#ifdef E1K_WITH_RXD_CACHE
        E1KRXDC rxdc;
        if (RT_UNLIKELY(!e1kUpdateRxDContext(pDevIns, pThis, &rxdc, "e1kRegWriteRDT")))
        {
            e1kCsRxLeave(pThis);
            E1kLog(("%s e1kRegWriteRDT: failed to update Rx context, returning VINF_SUCCESS\n",  pThis->szPrf));
            return VINF_SUCCESS;
        }
        /*
         * We need to fetch descriptors now as RDT may go whole circle
         * before we attempt to store a received packet. For example,
         * Intel's DOS drivers use 2 (!) RX descriptors with the total ring
         * size being only 8 descriptors! Note that we fetch descriptors
         * only when the cache is empty to reduce the number of memory reads
         * in case of frequent RDT writes. Don't fetch anything when the
         * receiver is disabled either as RDH, RDT, RDLEN can be in some
         * messed up state.
         * Note that despite the cache may seem empty, meaning that there are
         * no more available descriptors in it, it may still be used by RX
         * thread which has not yet written the last descriptor back but has
         * temporarily released the RX lock in order to write the packet body
         * to descriptor's buffer. At this point we still going to do prefetch
         * but it won't actually fetch anything if there are no unused slots in
         * our "empty" cache (nRxDFetched==E1K_RXD_CACHE_SIZE). We must not
         * reset the cache here even if it appears empty. It will be reset at
         * a later point in e1kRxDGet().
         */
        if (e1kRxDIsCacheEmpty(pThis) && (RCTL & RCTL_EN))
            e1kRxDPrefetch(pDevIns, pThis, &rxdc);
#endif /* E1K_WITH_RXD_CACHE */
        e1kCsRxLeave(pThis);
        if (RT_SUCCESS(rc))
        {
            /* Signal that we have more receive descriptors available. */
            e1kWakeupReceive(pDevIns, pThis);
        }
    }
    return rc;
}

/**
 * Write handler for Receive Delay Timer register.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWriteRDTR(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    e1kRegWriteDefault(pDevIns, pThis, offset, index, value);
    if (value & RDTR_FPD)
    {
        /* Flush requested, cancel both timers and raise interrupt */
#ifdef E1K_USE_RX_TIMERS
        e1kCancelTimer(pDevIns, pThis, pThis->hRIDTimer);
        e1kCancelTimer(pDevIns, pThis, pThis->hRADTimer);
#endif
        E1K_INC_ISTAT_CNT(pThis->uStatIntRDTR);
        return e1kRaiseInterrupt(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE, ICR_RXT0);
    }

    return VINF_SUCCESS;
}

DECLINLINE(uint32_t) e1kGetTxLen(PE1KTXDC pTxdc)
{
    /**
     *  Make sure TDT won't change during computation. EMT may modify TDT at
     *  any moment.
     */
    uint32_t tdt = pTxdc->tdt;
    return (pTxdc->tdh > tdt ? pTxdc->tdlen/sizeof(E1KTXDESC) : 0) + tdt - pTxdc->tdh;
}

#ifdef IN_RING3

# ifdef E1K_TX_DELAY
/**
 * @callback_method_impl{FNTMTIMERDEV, Transmit Delay Timer handler.}
 */
static DECLCALLBACK(void) e1kR3TxDelayTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PE1KSTATE pThis = (PE1KSTATE)pvUser;
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->csTx));
    RT_NOREF(hTimer);

    E1K_INC_ISTAT_CNT(pThis->uStatTxDelayExp);
#  ifdef E1K_INT_STATS
    uint64_t u64Elapsed = RTTimeNanoTS() - pThis->u64ArmedAt;
    if (u64Elapsed > pThis->uStatMaxTxDelay)
        pThis->uStatMaxTxDelay = u64Elapsed;
#  endif
    int rc = e1kXmitPending(pDevIns, pThis, false /*fOnWorkerThread*/);
    AssertMsg(RT_SUCCESS(rc) || rc == VERR_TRY_AGAIN, ("%Rrc\n", rc));
}
# endif /* E1K_TX_DELAY */

//# ifdef E1K_USE_TX_TIMERS

/**
 * @callback_method_impl{FNTMTIMERDEV, Transmit Interrupt Delay Timer handler.}
 */
static DECLCALLBACK(void) e1kR3TxIntDelayTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PE1KSTATE pThis = (PE1KSTATE)pvUser;
    Assert(hTimer == pThis->hTIDTimer); RT_NOREF(hTimer);

    E1K_INC_ISTAT_CNT(pThis->uStatTID);
    /* Cancel absolute delay timer as we have already got attention */
#  ifndef E1K_NO_TAD
    e1kCancelTimer(pDevIns, pThis, pThis->hTADTimer);
#  endif
    e1kRaiseInterrupt(pDevIns, pThis, VERR_IGNORED, ICR_TXDW);
}

/**
 * @callback_method_impl{FNTMTIMERDEV, Transmit Absolute Delay Timer handler.}
 */
static DECLCALLBACK(void) e1kR3TxAbsDelayTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PE1KSTATE pThis = (PE1KSTATE)pvUser;
    Assert(hTimer == pThis->hTADTimer); RT_NOREF(hTimer);

    E1K_INC_ISTAT_CNT(pThis->uStatTAD);
    /* Cancel interrupt delay timer as we have already got attention */
    e1kCancelTimer(pDevIns, pThis, pThis->hTIDTimer);
    e1kRaiseInterrupt(pDevIns, pThis, VERR_IGNORED, ICR_TXDW);
}

//# endif /* E1K_USE_TX_TIMERS */
# ifdef E1K_USE_RX_TIMERS

/**
 * @callback_method_impl{FNTMTIMERDEV, Receive Interrupt Delay Timer handler.}
 */
static DECLCALLBACK(void) e1kR3RxIntDelayTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PE1KSTATE pThis = (PE1KSTATE)pvUser;
    Assert(hTimer == pThis->hRIDTimer); RT_NOREF(hTimer);

    E1K_INC_ISTAT_CNT(pThis->uStatRID);
    /* Cancel absolute delay timer as we have already got attention */
    e1kCancelTimer(pDevIns, pThis, pThis->hRADTimer);
    e1kRaiseInterrupt(pDevIns, pThis, VERR_IGNORED, ICR_RXT0);
}

/**
 * @callback_method_impl{FNTMTIMERDEV, Receive Absolute Delay Timer handler.}
 */
static DECLCALLBACK(void) e1kR3RxAbsDelayTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PE1KSTATE pThis = (PE1KSTATE)pvUser;
    Assert(hTimer == pThis->hRADTimer); RT_NOREF(hTimer);

    E1K_INC_ISTAT_CNT(pThis->uStatRAD);
    /* Cancel interrupt delay timer as we have already got attention */
    e1kCancelTimer(pDevIns, pThis, pThis->hRIDTimer);
    e1kRaiseInterrupt(pDevIns, pThis, VERR_IGNORED, ICR_RXT0);
}

# endif /* E1K_USE_RX_TIMERS */

/**
 * @callback_method_impl{FNTMTIMERDEV, Late Interrupt Timer handler.}
 */
static DECLCALLBACK(void) e1kR3LateIntTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PE1KSTATE pThis = (PE1KSTATE)pvUser;
    Assert(hTimer == pThis->hIntTimer); RT_NOREF(hTimer);
    RT_NOREF(hTimer);

    STAM_PROFILE_ADV_START(&pThis->StatLateIntTimer, a);
    STAM_COUNTER_INC(&pThis->StatLateInts);
    E1K_INC_ISTAT_CNT(pThis->uStatIntLate);
# if 0
    if (pThis->iStatIntLost > -100)
        pThis->iStatIntLost--;
# endif
    e1kRaiseInterrupt(pDevIns, pThis, VERR_SEM_BUSY, 0);
    STAM_PROFILE_ADV_STOP(&pThis->StatLateIntTimer, a);
}

/**
 * @callback_method_impl{FNTMTIMERDEV, Link Up Timer handler.}
 */
static DECLCALLBACK(void) e1kR3LinkUpTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PE1KSTATE   pThis   = (PE1KSTATE)pvUser;
    PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
    Assert(hTimer == pThis->hLUTimer); RT_NOREF(hTimer);

    /*
     * This can happen if we set the link status to down when the Link up timer was
     * already armed (shortly after e1kR3LoadDone() or when the cable was disconnected
     * and connect+disconnect the cable very quick. Moreover, 82543GC triggers LSC
     * on reset even if the cable is unplugged (see @bugref{8942}).
     */
    if (e1kIsConnected(pDevIns))
    {
        /* 82543GC does not have an internal PHY */
        if (pThis->eChip == E1K_CHIP_82543GC || (CTRL & CTRL_SLU))
            e1kR3LinkUp(pDevIns, pThis, pThisCC);
    }
# ifdef E1K_LSC_ON_RESET
    else if (pThis->eChip == E1K_CHIP_82543GC)
        e1kR3LinkDown(pDevIns, pThis, pThisCC);
# endif /* E1K_LSC_ON_RESET */
}

#endif /* IN_RING3 */

/**
 * Sets up the GSO context according to the TSE new context descriptor.
 *
 * @param   pGso                The GSO context to setup.
 * @param   pCtx                The context descriptor.
 */
DECLINLINE(bool) e1kSetupGsoCtx(PPDMNETWORKGSO pGso, E1KTXCTX const *pCtx)
{
    pGso->u8Type = PDMNETWORKGSOTYPE_INVALID;

    /*
     * See if the context descriptor describes something that could be TCP or
     * UDP over IPv[46].
     */
    /* Check the header ordering and spacing: 1. Ethernet, 2. IP, 3. TCP/UDP. */
    if (RT_UNLIKELY( pCtx->ip.u8CSS < sizeof(RTNETETHERHDR) ))
    {
        E1kLog(("e1kSetupGsoCtx: IPCSS=%#x\n", pCtx->ip.u8CSS));
        return false;
    }
    if (RT_UNLIKELY( pCtx->tu.u8CSS     < (size_t)pCtx->ip.u8CSS + (pCtx->dw2.fIP  ? RTNETIPV4_MIN_LEN : RTNETIPV6_MIN_LEN) ))
    {
        E1kLog(("e1kSetupGsoCtx: TUCSS=%#x\n", pCtx->tu.u8CSS));
        return false;
    }
    if (RT_UNLIKELY(   pCtx->dw2.fTCP
                     ? pCtx->dw3.u8HDRLEN <  (size_t)pCtx->tu.u8CSS + RTNETTCP_MIN_LEN
                     : pCtx->dw3.u8HDRLEN != (size_t)pCtx->tu.u8CSS + RTNETUDP_MIN_LEN ))
    {
        E1kLog(("e1kSetupGsoCtx: HDRLEN=%#x TCP=%d\n", pCtx->dw3.u8HDRLEN, pCtx->dw2.fTCP));
        return false;
    }

    /* The end of the TCP/UDP checksum should stop at the end of the packet or at least after the headers. */
    if (RT_UNLIKELY( pCtx->tu.u16CSE > 0 && pCtx->tu.u16CSE <= pCtx->dw3.u8HDRLEN ))
    {
        E1kLog(("e1kSetupGsoCtx: TUCSE=%#x HDRLEN=%#x\n", pCtx->tu.u16CSE, pCtx->dw3.u8HDRLEN));
        return false;
    }

    /* IPv4 checksum offset. */
    if (RT_UNLIKELY( pCtx->dw2.fIP && (size_t)pCtx->ip.u8CSO - pCtx->ip.u8CSS != RT_UOFFSETOF(RTNETIPV4, ip_sum) ))
    {
        E1kLog(("e1kSetupGsoCtx: IPCSO=%#x IPCSS=%#x\n", pCtx->ip.u8CSO, pCtx->ip.u8CSS));
        return false;
    }

    /* TCP/UDP checksum offsets. */
    if (RT_UNLIKELY(   (size_t)pCtx->tu.u8CSO - pCtx->tu.u8CSS
                    != ( pCtx->dw2.fTCP
                         ? RT_UOFFSETOF(RTNETTCP, th_sum)
                         : RT_UOFFSETOF(RTNETUDP, uh_sum) ) ))
    {
        E1kLog(("e1kSetupGsoCtx: TUCSO=%#x TUCSS=%#x TCP=%d\n", pCtx->ip.u8CSO, pCtx->ip.u8CSS, pCtx->dw2.fTCP));
        return false;
    }

    /*
     * Because of internal networking using a 16-bit size field for GSO context
     * plus frame, we have to make sure we don't exceed this.
     */
    if (RT_UNLIKELY( pCtx->dw3.u8HDRLEN + pCtx->dw2.u20PAYLEN > VBOX_MAX_GSO_SIZE ))
    {
        E1kLog(("e1kSetupGsoCtx: HDRLEN(=%#x) + PAYLEN(=%#x) = %#x, max is %#x\n",
                pCtx->dw3.u8HDRLEN, pCtx->dw2.u20PAYLEN, pCtx->dw3.u8HDRLEN + pCtx->dw2.u20PAYLEN, VBOX_MAX_GSO_SIZE));
        return false;
    }

    /*
     * We're good for now - we'll do more checks when seeing the data.
     * So, figure the type of offloading and setup the context.
     */
    if (pCtx->dw2.fIP)
    {
        if (pCtx->dw2.fTCP)
        {
            pGso->u8Type = PDMNETWORKGSOTYPE_IPV4_TCP;
            pGso->cbHdrsSeg = pCtx->dw3.u8HDRLEN;
        }
        else
        {
            pGso->u8Type = PDMNETWORKGSOTYPE_IPV4_UDP;
            pGso->cbHdrsSeg = pCtx->tu.u8CSS; /* IP header only */
        }
        /** @todo Detect IPv4-IPv6 tunneling (need test setup since linux doesn't do
         *        this yet it seems)... */
    }
    else
    {
        pGso->cbHdrsSeg = pCtx->dw3.u8HDRLEN; /** @todo IPv6 UFO */
        if (pCtx->dw2.fTCP)
            pGso->u8Type = PDMNETWORKGSOTYPE_IPV6_TCP;
        else
            pGso->u8Type = PDMNETWORKGSOTYPE_IPV6_UDP;
    }
    pGso->offHdr1  = pCtx->ip.u8CSS;
    pGso->offHdr2  = pCtx->tu.u8CSS;
    pGso->cbHdrsTotal = pCtx->dw3.u8HDRLEN;
    pGso->cbMaxSeg = pCtx->dw3.u16MSS + (pGso->u8Type == PDMNETWORKGSOTYPE_IPV4_UDP ? pGso->offHdr2 : 0);
    Assert(PDMNetGsoIsValid(pGso, sizeof(*pGso), pGso->cbMaxSeg * 5));
    E1kLog2(("e1kSetupGsoCtx: mss=%#x hdr=%#x hdrseg=%#x hdr1=%#x hdr2=%#x %s\n",
             pGso->cbMaxSeg, pGso->cbHdrsTotal, pGso->cbHdrsSeg, pGso->offHdr1, pGso->offHdr2, PDMNetGsoTypeName((PDMNETWORKGSOTYPE)pGso->u8Type) ));
    return PDMNetGsoIsValid(pGso, sizeof(*pGso), pGso->cbMaxSeg * 5);
}

/**
 * Checks if we can use GSO processing for the current TSE frame.
 *
 * @param   pThis              The device state structure.
 * @param   pGso                The GSO context.
 * @param   pData               The first data descriptor of the frame.
 * @param   pCtx                The TSO context descriptor.
 */
DECLINLINE(bool) e1kCanDoGso(PE1KSTATE pThis, PCPDMNETWORKGSO pGso, E1KTXDAT const *pData, E1KTXCTX const *pCtx)
{
    if (!pData->cmd.fTSE)
    {
        E1kLog2(("e1kCanDoGso: !TSE\n"));
        return false;
    }
    if (pData->cmd.fVLE) /** @todo VLAN tagging. */
    {
        E1kLog(("e1kCanDoGso: VLE\n"));
        return false;
    }
    if (RT_UNLIKELY(!pThis->fGSOEnabled))
    {
        E1kLog3(("e1kCanDoGso: GSO disabled via CFGM\n"));
        return false;
    }

    switch ((PDMNETWORKGSOTYPE)pGso->u8Type)
    {
        case PDMNETWORKGSOTYPE_IPV4_TCP:
        case PDMNETWORKGSOTYPE_IPV4_UDP:
            if (!pData->dw3.fIXSM)
            {
                E1kLog(("e1kCanDoGso: !IXSM (IPv4)\n"));
                return false;
            }
            if (!pData->dw3.fTXSM)
            {
                E1kLog(("e1kCanDoGso: !TXSM (IPv4)\n"));
                return false;
            }
            /** @todo what more check should we perform here? Ethernet frame type? */
            E1kLog2(("e1kCanDoGso: OK, IPv4\n"));
            return true;

        case PDMNETWORKGSOTYPE_IPV6_TCP:
        case PDMNETWORKGSOTYPE_IPV6_UDP:
            if (pData->dw3.fIXSM && pCtx->ip.u8CSO)
            {
                E1kLog(("e1kCanDoGso: IXSM (IPv6)\n"));
                return false;
            }
            if (!pData->dw3.fTXSM)
            {
                E1kLog(("e1kCanDoGso: TXSM (IPv6)\n"));
                return false;
            }
            /** @todo what more check should we perform here? Ethernet frame type? */
            E1kLog2(("e1kCanDoGso: OK, IPv4\n"));
            return true;

        default:
            Assert(pGso->u8Type == PDMNETWORKGSOTYPE_INVALID);
            E1kLog2(("e1kCanDoGso: e1kSetupGsoCtx failed\n"));
            return false;
    }
}

/**
 * Frees the current xmit buffer.
 *
 * @param   pThis              The device state structure.
 */
static void e1kXmitFreeBuf(PE1KSTATE pThis, PE1KSTATECC pThisCC)
{
    PPDMSCATTERGATHER pSg = pThisCC->CTX_SUFF(pTxSg);
    if (pSg)
    {
        pThisCC->CTX_SUFF(pTxSg) = NULL;

        if (pSg->pvAllocator != pThis)
        {
            PPDMINETWORKUP pDrv = pThisCC->CTX_SUFF(pDrv);
            if (pDrv)
                pDrv->pfnFreeBuf(pDrv, pSg);
        }
        else
        {
            /* loopback */
            AssertCompileMemberSize(E1KSTATE, uTxFallback.Sg, 8 * sizeof(size_t));
            Assert(pSg->fFlags == (PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_3));
            pSg->fFlags = 0;
            pSg->pvAllocator = NULL;
        }
    }
}

#ifndef E1K_WITH_TXD_CACHE
/**
 * Allocates an xmit buffer.
 *
 * @returns See PDMINETWORKUP::pfnAllocBuf.
 * @param   pThis              The device state structure.
 * @param   cbMin               The minimum frame size.
 * @param   fExactSize          Whether cbMin is exact or if we have to max it
 *                              out to the max MTU size.
 * @param   fGso                Whether this is a GSO frame or not.
 */
DECLINLINE(int) e1kXmitAllocBuf(PE1KSTATE pThis, PE1KSTATECC pThisCC, size_t cbMin, bool fExactSize, bool fGso)
{
    /* Adjust cbMin if necessary. */
    if (!fExactSize)
        cbMin = RT_MAX(cbMin, E1K_MAX_TX_PKT_SIZE);

    /* Deal with existing buffer (descriptor screw up, reset, etc). */
    if (RT_UNLIKELY(pThisCC->CTX_SUFF(pTxSg)))
        e1kXmitFreeBuf(pThis, pThisCC);
    Assert(pThisCC->CTX_SUFF(pTxSg) == NULL);

    /*
     * Allocate the buffer.
     */
    PPDMSCATTERGATHER pSg;
    if (RT_LIKELY(GET_BITS(RCTL, LBM) != RCTL_LBM_TCVR))
    {
        PPDMINETWORKUP pDrv = pThisCC->CTX_SUFF(pDrv);
        if (RT_UNLIKELY(!pDrv))
            return VERR_NET_DOWN;
        int rc = pDrv->pfnAllocBuf(pDrv, cbMin, fGso ? &pThis->GsoCtx : NULL, &pSg);
        if (RT_FAILURE(rc))
        {
            /* Suspend TX as we are out of buffers atm */
            STATUS |= STATUS_TXOFF;
            return rc;
        }
    }
    else
    {
        /* Create a loopback using the fallback buffer and preallocated SG. */
        AssertCompileMemberSize(E1KSTATE, uTxFallback.Sg, 8 * sizeof(size_t));
        pSg = &pThis->uTxFallback.Sg;
        pSg->fFlags      = PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_3;
        pSg->cbUsed      = 0;
        pSg->cbAvailable = 0;
        pSg->pvAllocator = pThis;
        pSg->pvUser      = NULL; /* No GSO here. */
        pSg->cSegs       = 1;
        pSg->aSegs[0].pvSeg = pThis->aTxPacketFallback;
        pSg->aSegs[0].cbSeg = sizeof(pThis->aTxPacketFallback);
    }

    pThisCC->CTX_SUFF(pTxSg) = pSg;
    return VINF_SUCCESS;
}
#else /* E1K_WITH_TXD_CACHE */
/**
 * Allocates an xmit buffer.
 *
 * @returns See PDMINETWORKUP::pfnAllocBuf.
 * @param   pThis              The device state structure.
 * @param   cbMin               The minimum frame size.
 * @param   fExactSize          Whether cbMin is exact or if we have to max it
 *                              out to the max MTU size.
 * @param   fGso                Whether this is a GSO frame or not.
 */
DECLINLINE(int) e1kXmitAllocBuf(PE1KSTATE pThis, PE1KSTATECC pThisCC, bool fGso)
{
    /* Deal with existing buffer (descriptor screw up, reset, etc). */
    if (RT_UNLIKELY(pThisCC->CTX_SUFF(pTxSg)))
        e1kXmitFreeBuf(pThis, pThisCC);
    Assert(pThisCC->CTX_SUFF(pTxSg) == NULL);

    /*
     * Allocate the buffer.
     */
    PPDMSCATTERGATHER pSg;
    if (RT_LIKELY(GET_BITS(RCTL, LBM) != RCTL_LBM_TCVR))
    {
        if (pThis->cbTxAlloc == 0)
        {
            /* Zero packet, no need for the buffer */
            return VINF_SUCCESS;
        }
        if (fGso && pThis->GsoCtx.u8Type == PDMNETWORKGSOTYPE_INVALID)
        {
            E1kLog3(("Invalid GSO context, won't allocate this packet, cb=%u %s%s\n",
                     pThis->cbTxAlloc, pThis->fVTag ? "VLAN " : "", pThis->fGSO ? "GSO " : ""));
            /* No valid GSO context is available, ignore this packet. */
            pThis->cbTxAlloc = 0;
            return VINF_SUCCESS;
        }

        PPDMINETWORKUP pDrv = pThisCC->CTX_SUFF(pDrv);
        if (RT_UNLIKELY(!pDrv))
            return VERR_NET_DOWN;
        int rc = pDrv->pfnAllocBuf(pDrv, pThis->cbTxAlloc, fGso ? &pThis->GsoCtx : NULL, &pSg);
        if (RT_FAILURE(rc))
        {
            /* Suspend TX as we are out of buffers atm */
            STATUS |= STATUS_TXOFF;
            return rc;
        }
        E1kLog3(("%s Allocated buffer for TX packet: cb=%u %s%s\n",
                 pThis->szPrf, pThis->cbTxAlloc,
                 pThis->fVTag ? "VLAN " : "",
                 pThis->fGSO ? "GSO " : ""));
    }
    else
    {
        /* Create a loopback using the fallback buffer and preallocated SG. */
        AssertCompileMemberSize(E1KSTATE, uTxFallback.Sg, 8 * sizeof(size_t));
        pSg = &pThis->uTxFallback.Sg;
        pSg->fFlags      = PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_3;
        pSg->cbUsed      = 0;
        pSg->cbAvailable = sizeof(pThis->aTxPacketFallback);
        pSg->pvAllocator = pThis;
        pSg->pvUser      = NULL; /* No GSO here. */
        pSg->cSegs       = 1;
        pSg->aSegs[0].pvSeg = pThis->aTxPacketFallback;
        pSg->aSegs[0].cbSeg = sizeof(pThis->aTxPacketFallback);
    }
    pThis->cbTxAlloc = 0;

    pThisCC->CTX_SUFF(pTxSg) = pSg;
    return VINF_SUCCESS;
}
#endif /* E1K_WITH_TXD_CACHE */

/**
 * Checks if it's a GSO buffer or not.
 *
 * @returns true / false.
 * @param   pTxSg               The scatter / gather buffer.
 */
DECLINLINE(bool) e1kXmitIsGsoBuf(PDMSCATTERGATHER const *pTxSg)
{
#if 0
    if (!pTxSg)
        E1kLog(("e1kXmitIsGsoBuf: pTxSG is NULL\n"));
    if (pTxSg && pTxSg->pvUser)
        E1kLog(("e1kXmitIsGsoBuf: pvUser is NULL\n"));
#endif
    return pTxSg && pTxSg->pvUser /* GSO indicator */;
}

#ifndef E1K_WITH_TXD_CACHE
/**
 * Load transmit descriptor from guest memory.
 *
 * @param   pDevIns     The device instance.
 * @param   pDesc       Pointer to descriptor union.
 * @param   addr        Physical address in guest context.
 * @thread  E1000_TX
 */
DECLINLINE(void) e1kLoadDesc(PPDMDEVINS pDevIns, E1KTXDESC *pDesc, RTGCPHYS addr)
{
    PDMDevHlpPCIPhysRead(pDevIns, addr, pDesc, sizeof(E1KTXDESC));
}
#else /* E1K_WITH_TXD_CACHE */
/**
 * Load transmit descriptors from guest memory.
 *
 * We need two physical reads in case the tail wrapped around the end of TX
 * descriptor ring.
 *
 * @returns the actual number of descriptors fetched.
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @thread  E1000_TX
 */
DECLINLINE(unsigned) e1kTxDLoadMore(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KTXDC pTxdc)
{
    Assert(pThis->iTxDCurrent == 0);
    /* We've already loaded pThis->nTxDFetched descriptors past TDH. */
    unsigned nDescsAvailable    = e1kGetTxLen(pTxdc) - pThis->nTxDFetched;
    /* The following two lines ensure that pThis->nTxDFetched never overflows. */
    AssertCompile(E1K_TXD_CACHE_SIZE < (256 * sizeof(pThis->nTxDFetched)));
    unsigned nDescsToFetch      = RT_MIN(nDescsAvailable, E1K_TXD_CACHE_SIZE - pThis->nTxDFetched);
    unsigned nDescsTotal        = pTxdc->tdlen / sizeof(E1KTXDESC);
    Assert(nDescsTotal != 0);
    if (nDescsTotal == 0)
        return 0;
    unsigned nFirstNotLoaded    = (pTxdc->tdh + pThis->nTxDFetched) % nDescsTotal;
    unsigned nDescsInSingleRead = RT_MIN(nDescsToFetch, nDescsTotal - nFirstNotLoaded);
    E1kLog3(("%s e1kTxDLoadMore: nDescsAvailable=%u nDescsToFetch=%u nDescsTotal=%u nFirstNotLoaded=0x%x nDescsInSingleRead=%u\n",
             pThis->szPrf, nDescsAvailable, nDescsToFetch, nDescsTotal,
             nFirstNotLoaded, nDescsInSingleRead));
    if (nDescsToFetch == 0)
        return 0;
    E1KTXDESC* pFirstEmptyDesc = &pThis->aTxDescriptors[pThis->nTxDFetched];
    PDMDevHlpPCIPhysRead(pDevIns,
                         ((uint64_t)TDBAH << 32) + TDBAL + nFirstNotLoaded * sizeof(E1KTXDESC),
                         pFirstEmptyDesc, nDescsInSingleRead * sizeof(E1KTXDESC));
    E1kLog3(("%s Fetched %u TX descriptors at %08x%08x(0x%x), TDLEN=%08x, TDH=%08x, TDT=%08x\n",
             pThis->szPrf, nDescsInSingleRead,
             TDBAH, TDBAL + pTxdc->tdh * sizeof(E1KTXDESC),
             nFirstNotLoaded, pTxdc->tdlen, pTxdc->tdh, pTxdc->tdt));
    if (nDescsToFetch > nDescsInSingleRead)
    {
        PDMDevHlpPCIPhysRead(pDevIns,
                             ((uint64_t)TDBAH << 32) + TDBAL,
                             pFirstEmptyDesc + nDescsInSingleRead,
                             (nDescsToFetch - nDescsInSingleRead) * sizeof(E1KTXDESC));
        E1kLog3(("%s Fetched %u TX descriptors at %08x%08x\n",
                 pThis->szPrf, nDescsToFetch - nDescsInSingleRead,
                 TDBAH, TDBAL));
    }
    pThis->nTxDFetched += (uint8_t)nDescsToFetch;
    return nDescsToFetch;
}

/**
 * Load transmit descriptors from guest memory only if there are no loaded
 * descriptors.
 *
 * @returns true if there are descriptors in cache.
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @thread  E1000_TX
 */
DECLINLINE(bool) e1kTxDLazyLoad(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KTXDC pTxdc)
{
    if (pThis->nTxDFetched == 0)
        return e1kTxDLoadMore(pDevIns, pThis, pTxdc) != 0;
    return true;
}
#endif /* E1K_WITH_TXD_CACHE */

/**
 * Write back transmit descriptor to guest memory.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @param   pDesc       Pointer to descriptor union.
 * @param   addr        Physical address in guest context.
 * @thread  E1000_TX
 */
DECLINLINE(void) e1kWriteBackDesc(PPDMDEVINS pDevIns, PE1KSTATE pThis, E1KTXDESC *pDesc, RTGCPHYS addr)
{
    /* Only the last half of the descriptor has to be written back. */
    e1kPrintTDesc(pThis, pDesc, "^^^");
    PDMDevHlpPCIPhysWrite(pDevIns, addr, pDesc, sizeof(E1KTXDESC));
}

/**
 * Transmit complete frame.
 *
 * @remarks We skip the FCS since we're not responsible for sending anything to
 *          a real ethernet wire.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The device state structure.
 * @param   pThisCC         The current context instance data.
 * @param   fOnWorkerThread Whether we're on a worker thread or an EMT.
 * @thread  E1000_TX
 */
static void e1kTransmitFrame(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KSTATECC pThisCC, bool fOnWorkerThread)
{
    PPDMSCATTERGATHER   pSg     = pThisCC->CTX_SUFF(pTxSg);
    uint32_t            cbFrame = pSg ? (uint32_t)pSg->cbUsed : 0;
    Assert(!pSg || pSg->cSegs == 1);

    if (cbFrame < 14)
    {
        Log(("%s Ignoring invalid frame (%u bytes)\n", pThis->szPrf, cbFrame));
        return;
    }
    if (cbFrame > 70) /* unqualified guess */
        pThis->led.Asserted.s.fWriting = pThis->led.Actual.s.fWriting = 1;

#ifdef E1K_INT_STATS
    if (cbFrame <= 1514)
        E1K_INC_ISTAT_CNT(pThis->uStatTx1514);
    else if (cbFrame <= 2962)
        E1K_INC_ISTAT_CNT(pThis->uStatTx2962);
    else if (cbFrame <= 4410)
        E1K_INC_ISTAT_CNT(pThis->uStatTx4410);
    else if (cbFrame <= 5858)
        E1K_INC_ISTAT_CNT(pThis->uStatTx5858);
    else if (cbFrame <= 7306)
        E1K_INC_ISTAT_CNT(pThis->uStatTx7306);
    else if (cbFrame <= 8754)
        E1K_INC_ISTAT_CNT(pThis->uStatTx8754);
    else if (cbFrame <= 16384)
        E1K_INC_ISTAT_CNT(pThis->uStatTx16384);
    else if (cbFrame <= 32768)
        E1K_INC_ISTAT_CNT(pThis->uStatTx32768);
    else
        E1K_INC_ISTAT_CNT(pThis->uStatTxLarge);
#endif /* E1K_INT_STATS */

    /* Add VLAN tag */
    if (cbFrame > 12 && pThis->fVTag && pSg->cbUsed + 4 <= pSg->cbAvailable)
    {
        E1kLog3(("%s Inserting VLAN tag %08x\n",
            pThis->szPrf, RT_BE2H_U16((uint16_t)VET) | (RT_BE2H_U16(pThis->u16VTagTCI) << 16)));
        memmove((uint8_t*)pSg->aSegs[0].pvSeg + 16, (uint8_t*)pSg->aSegs[0].pvSeg + 12, cbFrame - 12);
        *((uint32_t*)pSg->aSegs[0].pvSeg + 3) = RT_BE2H_U16((uint16_t)VET) | (RT_BE2H_U16(pThis->u16VTagTCI) << 16);
        pSg->cbUsed += 4;
        cbFrame     += 4;
        Assert(pSg->cbUsed == cbFrame);
        Assert(pSg->cbUsed <= pSg->cbAvailable);
    }
/*    E1kLog2(("%s < < < Outgoing packet. Dump follows: > > >\n"
            "%.*Rhxd\n"
            "%s < < < < < < < < < < < < <  End of dump > > > > > > > > > > > >\n",
            pThis->szPrf, cbFrame, pSg->aSegs[0].pvSeg, pThis->szPrf));*/

    /* Update the stats */
    E1K_INC_CNT32(TPT);
    E1K_ADD_CNT64(TOTL, TOTH, cbFrame);
    E1K_INC_CNT32(GPTC);
    if (pSg && e1kIsBroadcast(pSg->aSegs[0].pvSeg))
        E1K_INC_CNT32(BPTC);
    else if (pSg && e1kIsMulticast(pSg->aSegs[0].pvSeg))
        E1K_INC_CNT32(MPTC);
    /* Update octet transmit counter */
    E1K_ADD_CNT64(GOTCL, GOTCH, cbFrame);
    if (pThisCC->CTX_SUFF(pDrv))
        STAM_REL_COUNTER_ADD(&pThis->StatTransmitBytes, cbFrame);
    if (cbFrame == 64)
        E1K_INC_CNT32(PTC64);
    else if (cbFrame < 128)
        E1K_INC_CNT32(PTC127);
    else if (cbFrame < 256)
        E1K_INC_CNT32(PTC255);
    else if (cbFrame < 512)
        E1K_INC_CNT32(PTC511);
    else if (cbFrame < 1024)
        E1K_INC_CNT32(PTC1023);
    else
        E1K_INC_CNT32(PTC1522);

    E1K_INC_ISTAT_CNT(pThis->uStatTxFrm);

    /*
     * Dump and send the packet.
     */
    int rc = VERR_NET_DOWN;
    if (pSg && pSg->pvAllocator != pThis)
    {
        e1kPacketDump(pDevIns, pThis, (uint8_t const *)pSg->aSegs[0].pvSeg, cbFrame, "--> Outgoing");

        pThisCC->CTX_SUFF(pTxSg) = NULL;
        PPDMINETWORKUP pDrv = pThisCC->CTX_SUFF(pDrv);
        if (pDrv)
        {
            /* Release critical section to avoid deadlock in CanReceive */
            //e1kCsLeave(pThis);
            STAM_PROFILE_START(&pThis->CTX_SUFF_Z(StatTransmitSend), a);
            rc = pDrv->pfnSendBuf(pDrv, pSg, fOnWorkerThread);
            STAM_PROFILE_STOP(&pThis->CTX_SUFF_Z(StatTransmitSend), a);
            //e1kR3CsEnterAsserted(pThis);
        }
    }
    else if (pSg)
    {
        Assert(pSg->aSegs[0].pvSeg == pThis->aTxPacketFallback);
        e1kPacketDump(pDevIns, pThis, (uint8_t const *)pSg->aSegs[0].pvSeg, cbFrame, "--> Loopback");

        /** @todo do we actually need to check that we're in loopback mode here? */
        if (GET_BITS(RCTL, LBM) == RCTL_LBM_TCVR)
        {
            E1KRXDST status;
            RT_ZERO(status);
            status.fPIF = true;
            e1kHandleRxPacket(pDevIns, pThis, pSg->aSegs[0].pvSeg, cbFrame, status);
            rc = VINF_SUCCESS;
        }
        e1kXmitFreeBuf(pThis, pThisCC);
    }
    else
        rc = VERR_NET_DOWN;
    if (RT_FAILURE(rc))
    {
        E1kLogRel(("E1000: ERROR! pfnSend returned %Rrc\n", rc));
        /** @todo handle VERR_NET_DOWN and VERR_NET_NO_BUFFER_SPACE. Signal error ? */
    }

    pThis->led.Actual.s.fWriting = 0;
}

/**
 * Compute and write internet checksum (e1kCSum16) at the specified offset.
 *
 * @param   pThis       The device state structure.
 * @param   pPkt        Pointer to the packet.
 * @param   u16PktLen   Total length of the packet.
 * @param   cso         Offset in packet to write checksum at.
 * @param   css         Offset in packet to start computing
 *                      checksum from.
 * @param   cse         Offset in packet to stop computing
 *                      checksum at.
 * @param   fUdp        Replace 0 checksum with all 1s.
 * @thread  E1000_TX
 */
static void e1kInsertChecksum(PE1KSTATE pThis, uint8_t *pPkt, uint16_t u16PktLen, uint8_t cso, uint8_t css, uint16_t cse, bool fUdp = false)
{
    RT_NOREF1(pThis);

    if (css >= u16PktLen)
    {
        E1kLog2(("%s css(%X) is greater than packet length-1(%X), checksum is not inserted\n",
                 pThis->szPrf, cso, u16PktLen));
        return;
    }

    if (cso >= u16PktLen - 1)
    {
        E1kLog2(("%s cso(%X) is greater than packet length-2(%X), checksum is not inserted\n",
                 pThis->szPrf, cso, u16PktLen));
        return;
    }

    if (cse == 0 || cse >= u16PktLen)
        cse = u16PktLen - 1;
    else if (cse < css)
    {
        E1kLog2(("%s css(%X) is greater than cse(%X), checksum is not inserted\n",
                 pThis->szPrf, css, cse));
        return;
    }

    uint16_t u16ChkSum = e1kCSum16(pPkt + css, cse - css + 1);
    if (fUdp && u16ChkSum == 0)
        u16ChkSum = ~u16ChkSum;     /* 0 means no checksum computed in case of UDP (see @bugref{9883}) */
    E1kLog2(("%s Inserting csum: %04X at %02X, old value: %04X\n", pThis->szPrf,
             u16ChkSum, cso, *(uint16_t*)(pPkt + cso)));
    *(uint16_t*)(pPkt + cso) = u16ChkSum;
}

/**
 * Add a part of descriptor's buffer to transmit frame.
 *
 * @remarks data.u64BufAddr is used unconditionally for both data
 *          and legacy descriptors since it is identical to
 *          legacy.u64BufAddr.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The device state structure.
 * @param   pDesc           Pointer to the descriptor to transmit.
 * @param   u16Len          Length of buffer to the end of segment.
 * @param   fSend           Force packet sending.
 * @param   fOnWorkerThread Whether we're on a worker thread or an EMT.
 * @thread  E1000_TX
 */
#ifndef E1K_WITH_TXD_CACHE
static void e1kFallbackAddSegment(PPDMDEVINS pDevIns, PE1KSTATE pThis, RTGCPHYS PhysAddr, uint16_t u16Len, bool fSend, bool fOnWorkerThread)
{
    PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
    /* TCP header being transmitted */
    struct E1kTcpHeader *pTcpHdr = (struct E1kTcpHeader *)(pThis->aTxPacketFallback + pThis->contextTSE.tu.u8CSS);
    /* IP header being transmitted */
    struct E1kIpHeader *pIpHdr = (struct E1kIpHeader *)(pThis->aTxPacketFallback + pThis->contextTSE.ip.u8CSS);

    E1kLog3(("%s e1kFallbackAddSegment: Length=%x, remaining payload=%x, header=%x, send=%RTbool\n",
             pThis->szPrf, u16Len, pThis->u32PayRemain, pThis->u16HdrRemain, fSend));
    Assert(pThis->u32PayRemain + pThis->u16HdrRemain > 0);

    PDMDevHlpPCIPhysRead(pDevIns, PhysAddr, pThis->aTxPacketFallback + pThis->u16TxPktLen, u16Len);
    E1kLog3(("%s Dump of the segment:\n"
             "%.*Rhxd\n"
             "%s --- End of dump ---\n",
             pThis->szPrf, u16Len, pThis->aTxPacketFallback + pThis->u16TxPktLen, pThis->szPrf));
    pThis->u16TxPktLen += u16Len;
    E1kLog3(("%s e1kFallbackAddSegment: pThis->u16TxPktLen=%x\n",
             pThis->szPrf, pThis->u16TxPktLen));
    if (pThis->u16HdrRemain > 0)
    {
        /* The header was not complete, check if it is now */
        if (u16Len >= pThis->u16HdrRemain)
        {
            /* The rest is payload */
            u16Len -= pThis->u16HdrRemain;
            pThis->u16HdrRemain = 0;
            /* Save partial checksum and flags */
            pThis->u32SavedCsum = pTcpHdr->chksum;
            pThis->u16SavedFlags = pTcpHdr->hdrlen_flags;
            /* Clear FIN and PSH flags now and set them only in the last segment */
            pTcpHdr->hdrlen_flags &= ~htons(E1K_TCP_FIN | E1K_TCP_PSH);
        }
        else
        {
            /* Still not */
            pThis->u16HdrRemain -= u16Len;
            E1kLog3(("%s e1kFallbackAddSegment: Header is still incomplete, 0x%x bytes remain.\n",
                     pThis->szPrf, pThis->u16HdrRemain));
            return;
        }
    }

    pThis->u32PayRemain -= u16Len;

    if (fSend)
    {
        /* Leave ethernet header intact */
        /* IP Total Length = payload + headers - ethernet header */
        pIpHdr->total_len = htons(pThis->u16TxPktLen - pThis->contextTSE.ip.u8CSS);
        E1kLog3(("%s e1kFallbackAddSegment: End of packet, pIpHdr->total_len=%x\n",
                 pThis->szPrf, ntohs(pIpHdr->total_len)));
        /* Update IP Checksum */
        pIpHdr->chksum = 0;
        e1kInsertChecksum(pThis, pThis->aTxPacketFallback, pThis->u16TxPktLen,
                          pThis->contextTSE.ip.u8CSO,
                          pThis->contextTSE.ip.u8CSS,
                          pThis->contextTSE.ip.u16CSE);

        /* Update TCP flags */
        /* Restore original FIN and PSH flags for the last segment */
        if (pThis->u32PayRemain == 0)
        {
            pTcpHdr->hdrlen_flags = pThis->u16SavedFlags;
            E1K_INC_CNT32(TSCTC);
        }
        /* Add TCP length to partial pseudo header sum */
        uint32_t csum = pThis->u32SavedCsum
                      + htons(pThis->u16TxPktLen - pThis->contextTSE.tu.u8CSS);
        while (csum >> 16)
            csum = (csum >> 16) + (csum & 0xFFFF);
        pTcpHdr->chksum = csum;
        /* Compute final checksum */
        e1kInsertChecksum(pThis, pThis->aTxPacketFallback, pThis->u16TxPktLen,
                          pThis->contextTSE.tu.u8CSO,
                          pThis->contextTSE.tu.u8CSS,
                          pThis->contextTSE.tu.u16CSE);

        /*
         * Transmit it. If we've use the SG already, allocate a new one before
         * we copy of the data.
         */
        PPDMSCATTERGATHER pTxSg = pThisCC->CTX_SUFF(pTxSg);
        if (!pTxSg)
        {
            e1kXmitAllocBuf(pThis, pThisCC, pThis->u16TxPktLen + (pThis->fVTag ? 4 : 0), true /*fExactSize*/, false /*fGso*/);
            pTxSg = pThisCC->CTX_SUFF(pTxSg);
        }
        if (pTxSg)
        {
            Assert(pThis->u16TxPktLen <= pThisCC->CTX_SUFF(pTxSg)->cbAvailable);
            Assert(pTxSg->cSegs == 1);
            if (pThis->CCCTX_SUFF(pTxSg)->aSegs[0].pvSeg != pThis->aTxPacketFallback)
                memcpy(pTxSg->aSegs[0].pvSeg, pThis->aTxPacketFallback, pThis->u16TxPktLen);
            pTxSg->cbUsed         = pThis->u16TxPktLen;
            pTxSg->aSegs[0].cbSeg = pThis->u16TxPktLen;
        }
        e1kTransmitFrame(pDevIns, pThis, pThisCC, fOnWorkerThread);

        /* Update Sequence Number */
        pTcpHdr->seqno = htonl(ntohl(pTcpHdr->seqno) + pThis->u16TxPktLen
                               - pThis->contextTSE.dw3.u8HDRLEN);
        /* Increment IP identification */
        pIpHdr->ident = htons(ntohs(pIpHdr->ident) + 1);
    }
}
#else /* E1K_WITH_TXD_CACHE */
static int e1kFallbackAddSegment(PPDMDEVINS pDevIns, PE1KSTATE pThis, RTGCPHYS PhysAddr, uint16_t u16Len, bool fSend, bool fOnWorkerThread)
{
    int rc = VINF_SUCCESS;
    PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
    /* TCP header being transmitted */
    struct E1kTcpHeader *pTcpHdr = (struct E1kTcpHeader *)(pThis->aTxPacketFallback + pThis->contextTSE.tu.u8CSS);
    /* IP header being transmitted */
    struct E1kIpHeader *pIpHdr = (struct E1kIpHeader *)(pThis->aTxPacketFallback + pThis->contextTSE.ip.u8CSS);

    E1kLog3(("%s e1kFallbackAddSegment: Length=%x, remaining payload=%x, header=%x, send=%RTbool\n",
             pThis->szPrf, u16Len, pThis->u32PayRemain, pThis->u16HdrRemain, fSend));
    AssertReturn(pThis->u32PayRemain + pThis->u16HdrRemain > 0, VINF_SUCCESS);

    if (pThis->u16TxPktLen + u16Len <= sizeof(pThis->aTxPacketFallback))
        PDMDevHlpPCIPhysRead(pDevIns, PhysAddr, pThis->aTxPacketFallback + pThis->u16TxPktLen, u16Len);
    else
        E1kLog(("%s e1kFallbackAddSegment: writing beyond aTxPacketFallback, u16TxPktLen=%d(0x%x) + u16Len=%d(0x%x) > %d\n",
                pThis->szPrf, pThis->u16TxPktLen, pThis->u16TxPktLen, u16Len, u16Len, sizeof(pThis->aTxPacketFallback)));
    E1kLog3(("%s Dump of the segment:\n"
             "%.*Rhxd\n"
             "%s --- End of dump ---\n",
             pThis->szPrf, u16Len, pThis->aTxPacketFallback + pThis->u16TxPktLen, pThis->szPrf));
    pThis->u16TxPktLen += u16Len;
    E1kLog3(("%s e1kFallbackAddSegment: pThis->u16TxPktLen=%x\n",
             pThis->szPrf, pThis->u16TxPktLen));
    if (pThis->u16HdrRemain > 0)
    {
        /* The header was not complete, check if it is now */
        if (u16Len >= pThis->u16HdrRemain)
        {
            /* The rest is payload */
            u16Len -= pThis->u16HdrRemain;
            pThis->u16HdrRemain = 0;
            /* Save partial checksum and flags */
            pThis->u32SavedCsum = pTcpHdr->chksum;
            pThis->u16SavedFlags = pTcpHdr->hdrlen_flags;
            /* Clear FIN and PSH flags now and set them only in the last segment */
            pTcpHdr->hdrlen_flags &= ~htons(E1K_TCP_FIN | E1K_TCP_PSH);
        }
        else
        {
            /* Still not */
            pThis->u16HdrRemain -= u16Len;
            E1kLog3(("%s e1kFallbackAddSegment: Header is still incomplete, 0x%x bytes remain.\n",
                     pThis->szPrf, pThis->u16HdrRemain));
            return rc;
        }
    }

    if (u16Len > pThis->u32PayRemain)
        pThis->u32PayRemain = 0;
    else
        pThis->u32PayRemain -= u16Len;

    if (fSend)
    {
        /* Leave ethernet header intact */
        /* IP Total Length = payload + headers - ethernet header */
        pIpHdr->total_len = htons(pThis->u16TxPktLen - pThis->contextTSE.ip.u8CSS);
        E1kLog3(("%s e1kFallbackAddSegment: End of packet, pIpHdr->total_len=%x\n",
                 pThis->szPrf, ntohs(pIpHdr->total_len)));
        /* Update IP Checksum */
        pIpHdr->chksum = 0;
        e1kInsertChecksum(pThis, pThis->aTxPacketFallback, pThis->u16TxPktLen,
                          pThis->contextTSE.ip.u8CSO,
                          pThis->contextTSE.ip.u8CSS,
                          pThis->contextTSE.ip.u16CSE);

        /* Update TCP flags */
        /* Restore original FIN and PSH flags for the last segment */
        if (pThis->u32PayRemain == 0)
        {
            pTcpHdr->hdrlen_flags = pThis->u16SavedFlags;
            E1K_INC_CNT32(TSCTC);
        }
        /* Add TCP length to partial pseudo header sum */
        uint32_t csum = pThis->u32SavedCsum
                      + htons(pThis->u16TxPktLen - pThis->contextTSE.tu.u8CSS);
        while (csum >> 16)
            csum = (csum >> 16) + (csum & 0xFFFF);
        Assert(csum < 65536);
        pTcpHdr->chksum = (uint16_t)csum;
        /* Compute final checksum */
        e1kInsertChecksum(pThis, pThis->aTxPacketFallback, pThis->u16TxPktLen,
                          pThis->contextTSE.tu.u8CSO,
                          pThis->contextTSE.tu.u8CSS,
                          pThis->contextTSE.tu.u16CSE);

        /*
         * Transmit it.
         */
        PPDMSCATTERGATHER pTxSg = pThisCC->CTX_SUFF(pTxSg);
        if (pTxSg)
        {
            /* Make sure the packet fits into the allocated buffer */
            size_t cbCopy = RT_MIN(pThis->u16TxPktLen, pThisCC->CTX_SUFF(pTxSg)->cbAvailable);
#ifdef DEBUG
            if (pThis->u16TxPktLen > pTxSg->cbAvailable)
                E1kLog(("%s e1kFallbackAddSegment: truncating packet, u16TxPktLen=%d(0x%x) > cbAvailable=%d(0x%x)\n",
                        pThis->szPrf, pThis->u16TxPktLen, pThis->u16TxPktLen, pTxSg->cbAvailable, pTxSg->cbAvailable));
#endif /* DEBUG */
            Assert(pTxSg->cSegs == 1);
            if (pTxSg->aSegs[0].pvSeg != pThis->aTxPacketFallback)
                memcpy(pTxSg->aSegs[0].pvSeg, pThis->aTxPacketFallback, cbCopy);
            pTxSg->cbUsed         = cbCopy;
            pTxSg->aSegs[0].cbSeg = cbCopy;
        }
        e1kTransmitFrame(pDevIns, pThis, pThisCC, fOnWorkerThread);

        /* Update Sequence Number */
        pTcpHdr->seqno = htonl(ntohl(pTcpHdr->seqno) + pThis->u16TxPktLen
                               - pThis->contextTSE.dw3.u8HDRLEN);
        /* Increment IP identification */
        pIpHdr->ident = htons(ntohs(pIpHdr->ident) + 1);

        /* Allocate new buffer for the next segment. */
        if (pThis->u32PayRemain)
        {
            pThis->cbTxAlloc = RT_MIN(pThis->u32PayRemain,
                                       pThis->contextTSE.dw3.u16MSS)
                                + pThis->contextTSE.dw3.u8HDRLEN;
            /* Do not add VLAN tags to empty packets. */
            if (pThis->fVTag && pThis->cbTxAlloc > 0)
                pThis->cbTxAlloc += 4;
            rc = e1kXmitAllocBuf(pThis, pThisCC, false /* fGSO */);
        }
    }

    return rc;
}
#endif /* E1K_WITH_TXD_CACHE */

#ifndef E1K_WITH_TXD_CACHE
/**
 * TCP segmentation offloading fallback: Add descriptor's buffer to transmit
 * frame.
 *
 * We construct the frame in the fallback buffer first and the copy it to the SG
 * buffer before passing it down to the network driver code.
 *
 * @returns true if the frame should be transmitted, false if not.
 *
 * @param   pThis          The device state structure.
 * @param   pDesc           Pointer to the descriptor to transmit.
 * @param   cbFragment      Length of descriptor's buffer.
 * @param   fOnWorkerThread Whether we're on a worker thread or an EMT.
 * @thread  E1000_TX
 */
static bool e1kFallbackAddToFrame(PE1KSTATE pThis, E1KTXDESC *pDesc, uint32_t cbFragment, bool fOnWorkerThread)
{
    PPDMSCATTERGATHER pTxSg = pThisCC->CTX_SUFF(pTxSg);
    Assert(e1kGetDescType(pDesc) == E1K_DTYP_DATA);
    Assert(pDesc->data.cmd.fTSE);
    Assert(!e1kXmitIsGsoBuf(pTxSg));

    uint16_t u16MaxPktLen = pThis->contextTSE.dw3.u8HDRLEN + pThis->contextTSE.dw3.u16MSS;
    Assert(u16MaxPktLen != 0);
    Assert(u16MaxPktLen < E1K_MAX_TX_PKT_SIZE);

    /*
     * Carve out segments.
     */
    do
    {
        /* Calculate how many bytes we have left in this TCP segment */
        uint32_t cb = u16MaxPktLen - pThis->u16TxPktLen;
        if (cb > cbFragment)
        {
            /* This descriptor fits completely into current segment */
            cb = cbFragment;
            e1kFallbackAddSegment(pDevIns, pThis, pDesc->data.u64BufAddr, cb, pDesc->data.cmd.fEOP /*fSend*/, fOnWorkerThread);
        }
        else
        {
            e1kFallbackAddSegment(pDevIns, pThis, pDesc->data.u64BufAddr, cb, true /*fSend*/, fOnWorkerThread);
            /*
             * Rewind the packet tail pointer to the beginning of payload,
             * so we continue writing right beyond the header.
             */
            pThis->u16TxPktLen = pThis->contextTSE.dw3.u8HDRLEN;
        }

        pDesc->data.u64BufAddr += cb;
        cbFragment             -= cb;
    } while (cbFragment > 0);

    if (pDesc->data.cmd.fEOP)
    {
        /* End of packet, next segment will contain header. */
        if (pThis->u32PayRemain != 0)
            E1K_INC_CNT32(TSCTFC);
        pThis->u16TxPktLen = 0;
        e1kXmitFreeBuf(pThis, PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC));
    }

    return false;
}
#else /* E1K_WITH_TXD_CACHE */
/**
 * TCP segmentation offloading fallback: Add descriptor's buffer to transmit
 * frame.
 *
 * We construct the frame in the fallback buffer first and the copy it to the SG
 * buffer before passing it down to the network driver code.
 *
 * @returns error code
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The device state structure.
 * @param   pDesc           Pointer to the descriptor to transmit.
 * @param   cbFragment      Length of descriptor's buffer.
 * @param   fOnWorkerThread Whether we're on a worker thread or an EMT.
 * @thread  E1000_TX
 */
static int e1kFallbackAddToFrame(PPDMDEVINS pDevIns, PE1KSTATE pThis, E1KTXDESC *pDesc, bool fOnWorkerThread)
{
#ifdef VBOX_STRICT
    PPDMSCATTERGATHER pTxSg = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC)->CTX_SUFF(pTxSg);
    Assert(e1kGetDescType(pDesc) == E1K_DTYP_DATA);
    Assert(pDesc->data.cmd.fTSE);
    Assert(!e1kXmitIsGsoBuf(pTxSg));
#endif

    uint16_t u16MaxPktLen = pThis->contextTSE.dw3.u8HDRLEN + pThis->contextTSE.dw3.u16MSS;
    /* We cannot produce empty packets, ignore all TX descriptors (see @bugref{9571}) */
    if (u16MaxPktLen == 0)
        return VINF_SUCCESS;

    /*
     * Carve out segments.
     */
    int rc = VINF_SUCCESS;
    do
    {
        /* Calculate how many bytes we have left in this TCP segment */
        uint16_t cb = u16MaxPktLen - pThis->u16TxPktLen;
        if (cb > pDesc->data.cmd.u20DTALEN)
        {
            /* This descriptor fits completely into current segment */
            cb = (uint16_t)pDesc->data.cmd.u20DTALEN; /* u20DTALEN at this point is guarantied to fit into 16 bits. */
            rc = e1kFallbackAddSegment(pDevIns, pThis, pDesc->data.u64BufAddr, cb, pDesc->data.cmd.fEOP /*fSend*/, fOnWorkerThread);
        }
        else
        {
            rc = e1kFallbackAddSegment(pDevIns, pThis, pDesc->data.u64BufAddr, cb, true /*fSend*/, fOnWorkerThread);
            /*
             * Rewind the packet tail pointer to the beginning of payload,
             * so we continue writing right beyond the header.
             */
            pThis->u16TxPktLen = pThis->contextTSE.dw3.u8HDRLEN;
        }

        pDesc->data.u64BufAddr    += cb;
        pDesc->data.cmd.u20DTALEN -= cb;
    } while (pDesc->data.cmd.u20DTALEN > 0 && RT_SUCCESS(rc));

    if (pDesc->data.cmd.fEOP)
    {
        /* End of packet, next segment will contain header. */
        if (pThis->u32PayRemain != 0)
            E1K_INC_CNT32(TSCTFC);
        pThis->u16TxPktLen = 0;
        e1kXmitFreeBuf(pThis, PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC));
    }

    return VINF_SUCCESS; /// @todo consider rc;
}
#endif /* E1K_WITH_TXD_CACHE */


/**
 * Add descriptor's buffer to transmit frame.
 *
 * This deals with GSO and normal frames, e1kFallbackAddToFrame deals with the
 * TSE frames we cannot handle as GSO.
 *
 * @returns true on success, false on failure.
 *
 * @param   pDevIns     The device instance.
 * @param   pThisCC     The current context instance data.
 * @param   pThis       The device state structure.
 * @param   PhysAddr    The physical address of the descriptor buffer.
 * @param   cbFragment  Length of descriptor's buffer.
 * @thread  E1000_TX
 */
static bool e1kAddToFrame(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KSTATECC pThisCC, RTGCPHYS PhysAddr, uint32_t cbFragment)
{
    PPDMSCATTERGATHER   pTxSg    = pThisCC->CTX_SUFF(pTxSg);
    bool const          fGso     = e1kXmitIsGsoBuf(pTxSg);
    uint32_t const      cbNewPkt = cbFragment + pThis->u16TxPktLen;

    LogFlow(("%s e1kAddToFrame: ENTER cbFragment=%d u16TxPktLen=%d cbUsed=%d cbAvailable=%d fGSO=%s\n",
             pThis->szPrf, cbFragment, pThis->u16TxPktLen, pTxSg->cbUsed, pTxSg->cbAvailable,
             fGso ? "true" : "false"));
    PCPDMNETWORKGSO pGso = (PCPDMNETWORKGSO)pTxSg->pvUser;
    if (pGso)
    {
        if (RT_UNLIKELY(pGso->cbMaxSeg == 0))
        {
            E1kLog(("%s zero-sized fragments are not allowed\n", pThis->szPrf));
            return false;
        }
        if (RT_UNLIKELY(pGso->u8Type == PDMNETWORKGSOTYPE_IPV4_UDP))
        {
            E1kLog(("%s UDP fragmentation is no longer supported\n", pThis->szPrf));
            return false;
        }
    }
    if (RT_UNLIKELY( !fGso && cbNewPkt > E1K_MAX_TX_PKT_SIZE ))
    {
        E1kLog(("%s Transmit packet is too large: %u > %u(max)\n", pThis->szPrf, cbNewPkt, E1K_MAX_TX_PKT_SIZE));
        return false;
    }
    if (RT_UNLIKELY( cbNewPkt > pTxSg->cbAvailable ))
    {
        E1kLog(("%s Transmit packet is too large: %u > %u(max)\n", pThis->szPrf, cbNewPkt, pTxSg->cbAvailable));
        return false;
    }

    if (RT_LIKELY(pTxSg))
    {
        Assert(pTxSg->cSegs == 1);
        if (pTxSg->cbUsed != pThis->u16TxPktLen)
            E1kLog(("%s e1kAddToFrame:  pTxSg->cbUsed=%d(0x%x) != u16TxPktLen=%d(0x%x)\n",
                    pThis->szPrf, pTxSg->cbUsed, pTxSg->cbUsed, pThis->u16TxPktLen, pThis->u16TxPktLen));

        PDMDevHlpPCIPhysRead(pDevIns, PhysAddr, (uint8_t *)pTxSg->aSegs[0].pvSeg + pThis->u16TxPktLen, cbFragment);

        pTxSg->cbUsed = cbNewPkt;
    }
    pThis->u16TxPktLen = cbNewPkt;

    return true;
}


/**
 * Write the descriptor back to guest memory and notify the guest.
 *
 * @param   pThis       The device state structure.
 * @param   pDesc       Pointer to the descriptor have been transmitted.
 * @param   addr        Physical address of the descriptor in guest memory.
 * @thread  E1000_TX
 */
static void e1kDescReport(PPDMDEVINS pDevIns, PE1KSTATE pThis, E1KTXDESC *pDesc, RTGCPHYS addr)
{
    /*
     * We fake descriptor write-back bursting. Descriptors are written back as they are
     * processed.
     */
    /* Let's pretend we process descriptors. Write back with DD set. */
    /*
     * Prior to r71586 we tried to accomodate the case when write-back bursts
     * are enabled without actually implementing bursting by writing back all
     * descriptors, even the ones that do not have RS set. This caused kernel
     * panics with Linux SMP kernels, as the e1000 driver tried to free up skb
     * associated with written back descriptor if it happened to be a context
     * descriptor since context descriptors do not have skb associated to them.
     * Starting from r71586 we write back only the descriptors with RS set,
     * which is a little bit different from what the real hardware does in
     * case there is a chain of data descritors where some of them have RS set
     * and others do not. It is very uncommon scenario imho.
     * We need to check RPS as well since some legacy drivers use it instead of
     * RS even with newer cards.
     */
    if (pDesc->legacy.cmd.fRS || pDesc->legacy.cmd.fRPS)
    {
        pDesc->legacy.dw3.fDD = 1; /* Descriptor Done */
        e1kWriteBackDesc(pDevIns, pThis, pDesc, addr);
        if (pDesc->legacy.cmd.fEOP)
        {
//#ifdef E1K_USE_TX_TIMERS
            if (pThis->fTidEnabled && pDesc->legacy.cmd.fIDE)
            {
                E1K_INC_ISTAT_CNT(pThis->uStatTxIDE);
                //if (pThis->fIntRaised)
                //{
                //    /* Interrupt is already pending, no need for timers */
                //    ICR |= ICR_TXDW;
                //}
                //else {
                /* Arm the timer to fire in TIVD usec (discard .024) */
                e1kArmTimer(pDevIns, pThis, pThis->hTIDTimer, TIDV);
# ifndef E1K_NO_TAD
                /* If absolute timer delay is enabled and the timer is not running yet, arm it. */
                E1kLog2(("%s Checking if TAD timer is running\n",
                         pThis->szPrf));
                if (TADV != 0 && !PDMDevHlpTimerIsActive(pDevIns, pThis->hTADTimer))
                    e1kArmTimer(pDevIns, pThis, pThis->hTADTimer, TADV);
# endif /* E1K_NO_TAD */
            }
            else
            {
                if (pThis->fTidEnabled)
                {
                    E1kLog2(("%s No IDE set, cancel TAD timer and raise interrupt\n",
                             pThis->szPrf));
                    /* Cancel both timers if armed and fire immediately. */
# ifndef E1K_NO_TAD
                    PDMDevHlpTimerStop(pDevIns, pThis->hTADTimer);
# endif
                    PDMDevHlpTimerStop(pDevIns, pThis->hTIDTimer);
                }
//#endif /* E1K_USE_TX_TIMERS */
                E1K_INC_ISTAT_CNT(pThis->uStatIntTx);
                e1kRaiseInterrupt(pDevIns, pThis, VERR_SEM_BUSY, ICR_TXDW);
//#ifdef E1K_USE_TX_TIMERS
            }
//#endif /* E1K_USE_TX_TIMERS */
        }
    }
    else
    {
        E1K_INC_ISTAT_CNT(pThis->uStatTxNoRS);
    }
}

#ifndef E1K_WITH_TXD_CACHE

/**
 * Process Transmit Descriptor.
 *
 * E1000 supports three types of transmit descriptors:
 * - legacy   data descriptors of older format (context-less).
 * - data     the same as legacy but providing new offloading capabilities.
 * - context  sets up the context for following data descriptors.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The device state structure.
 * @param   pThisCC         The current context instance data.
 * @param   pDesc           Pointer to descriptor union.
 * @param   addr            Physical address of descriptor in guest memory.
 * @param   fOnWorkerThread Whether we're on a worker thread or an EMT.
 * @thread  E1000_TX
 */
static int e1kXmitDesc(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KSTATECC pThisCC, E1KTXDESC *pDesc,
                       RTGCPHYS addr, bool fOnWorkerThread)
{
    int rc = VINF_SUCCESS;
    uint32_t cbVTag = 0;

    e1kPrintTDesc(pThis, pDesc, "vvv");

//#ifdef E1K_USE_TX_TIMERS
    if (pThis->fTidEnabled)
        e1kCancelTimer(pDevIns, pThis, pThis->hTIDTimer);
//#endif /* E1K_USE_TX_TIMERS */

    switch (e1kGetDescType(pDesc))
    {
        case E1K_DTYP_CONTEXT:
            if (pDesc->context.dw2.fTSE)
            {
                pThis->contextTSE = pDesc->context;
                pThis->u32PayRemain = pDesc->context.dw2.u20PAYLEN;
                pThis->u16HdrRemain = pDesc->context.dw3.u8HDRLEN;
                e1kSetupGsoCtx(&pThis->GsoCtx, &pDesc->context);
                STAM_COUNTER_INC(&pThis->StatTxDescCtxTSE);
            }
            else
            {
                pThis->contextNormal = pDesc->context;
                STAM_COUNTER_INC(&pThis->StatTxDescCtxNormal);
            }
            E1kLog2(("%s %s context updated: IP CSS=%02X, IP CSO=%02X, IP CSE=%04X"
                    ", TU CSS=%02X, TU CSO=%02X, TU CSE=%04X\n", pThis->szPrf,
                     pDesc->context.dw2.fTSE ? "TSE" : "Normal",
                     pDesc->context.ip.u8CSS,
                     pDesc->context.ip.u8CSO,
                     pDesc->context.ip.u16CSE,
                     pDesc->context.tu.u8CSS,
                     pDesc->context.tu.u8CSO,
                     pDesc->context.tu.u16CSE));
            E1K_INC_ISTAT_CNT(pThis->uStatDescCtx);
            e1kDescReport(pThis, pDesc, addr);
            break;

        case E1K_DTYP_DATA:
        {
            if (pDesc->data.cmd.u20DTALEN == 0 || pDesc->data.u64BufAddr == 0)
            {
                E1kLog2(("% Empty data descriptor, skipped.\n", pThis->szPrf));
                /** @todo Same as legacy when !TSE. See below. */
                break;
            }
            STAM_COUNTER_INC(pDesc->data.cmd.fTSE?
                             &pThis->StatTxDescTSEData:
                             &pThis->StatTxDescData);
            STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatTransmit), a);
            E1K_INC_ISTAT_CNT(pThis->uStatDescDat);

            /*
             * The last descriptor of non-TSE packet must contain VLE flag.
             * TSE packets have VLE flag in the first descriptor. The later
             * case is taken care of a bit later when cbVTag gets assigned.
             *
             * 1) pDesc->data.cmd.fEOP && !pDesc->data.cmd.fTSE
             */
            if (pDesc->data.cmd.fEOP && !pDesc->data.cmd.fTSE)
            {
                pThis->fVTag      = pDesc->data.cmd.fVLE;
                pThis->u16VTagTCI = pDesc->data.dw3.u16Special;
            }
            /*
             * First fragment: Allocate new buffer and save the IXSM and TXSM
             * packet options as these are only valid in the first fragment.
             */
            if (pThis->u16TxPktLen == 0)
            {
                pThis->fIPcsum  = pDesc->data.dw3.fIXSM;
                pThis->fTCPcsum = pDesc->data.dw3.fTXSM;
                E1kLog2(("%s Saving checksum flags:%s%s; \n", pThis->szPrf,
                         pThis->fIPcsum ? " IP" : "",
                         pThis->fTCPcsum ? " TCP/UDP" : ""));
                if (pDesc->data.cmd.fTSE)
                {
                    /* 2) pDesc->data.cmd.fTSE && pThis->u16TxPktLen == 0 */
                    pThis->fVTag      = pDesc->data.cmd.fVLE;
                    pThis->u16VTagTCI = pDesc->data.dw3.u16Special;
                    cbVTag = pThis->fVTag ? 4 : 0;
                }
                else if (pDesc->data.cmd.fEOP)
                    cbVTag = pDesc->data.cmd.fVLE ? 4 : 0;
                else
                    cbVTag = 4;
                E1kLog3(("%s About to allocate TX buffer: cbVTag=%u\n", pThis->szPrf, cbVTag));
                if (e1kCanDoGso(pThis, &pThis->GsoCtx, &pDesc->data, &pThis->contextTSE))
                    rc = e1kXmitAllocBuf(pThis, pThisCC, pThis->contextTSE.dw2.u20PAYLEN + pThis->contextTSE.dw3.u8HDRLEN + cbVTag,
                                         true /*fExactSize*/, true /*fGso*/);
                else if (pDesc->data.cmd.fTSE)
                    rc = e1kXmitAllocBuf(pThis, pThisCC, , pThis->contextTSE.dw3.u16MSS + pThis->contextTSE.dw3.u8HDRLEN + cbVTag,
                                         pDesc->data.cmd.fTSE  /*fExactSize*/, false /*fGso*/);
                else
                    rc = e1kXmitAllocBuf(pThis, pThisCC, pDesc->data.cmd.u20DTALEN + cbVTag,
                                         pDesc->data.cmd.fEOP  /*fExactSize*/, false /*fGso*/);

                /**
                 * @todo: Perhaps it is not that simple for GSO packets! We may
                 * need to unwind some changes.
                 */
                if (RT_FAILURE(rc))
                {
                    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);
                    break;
                }
                /** @todo Is there any way to indicating errors other than collisions? Like
                 *        VERR_NET_DOWN. */
            }

            /*
             * Add the descriptor data to the frame.  If the frame is complete,
             * transmit it and reset the u16TxPktLen field.
             */
            if (e1kXmitIsGsoBuf(pThisCC->CTX_SUFF(pTxSg)))
            {
                STAM_COUNTER_INC(&pThis->StatTxPathGSO);
                bool fRc = e1kAddToFrame(pDevIns, pThis, pThisCC, pDesc->data.u64BufAddr, pDesc->data.cmd.u20DTALEN);
                if (pDesc->data.cmd.fEOP)
                {
                    if (   fRc
                        && pThisCC->CTX_SUFF(pTxSg)
                        && pThisCC->CTX_SUFF(pTxSg)->cbUsed == (size_t)pThis->contextTSE.dw3.u8HDRLEN + pThis->contextTSE.dw2.u20PAYLEN)
                    {
                        e1kTransmitFrame(pDevIns, pThis, pThisCC, fOnWorkerThread);
                        E1K_INC_CNT32(TSCTC);
                    }
                    else
                    {
                        if (fRc)
                           E1kLog(("%s bad GSO/TSE %p or %u < %u\n" , pThis->szPrf,
                                   pThisCC->CTX_SUFF(pTxSg), pThisCC->CTX_SUFF(pTxSg) ? pThisCC->CTX_SUFF(pTxSg)->cbUsed : 0,
                                   pThis->contextTSE.dw3.u8HDRLEN + pThis->contextTSE.dw2.u20PAYLEN));
                        e1kXmitFreeBuf(pThis);
                        E1K_INC_CNT32(TSCTFC);
                    }
                    pThis->u16TxPktLen = 0;
                }
            }
            else if (!pDesc->data.cmd.fTSE)
            {
                STAM_COUNTER_INC(&pThis->StatTxPathRegular);
                bool fRc = e1kAddToFrame(pDevIns, pThis, pThisCC, pDesc->data.u64BufAddr, pDesc->data.cmd.u20DTALEN);
                if (pDesc->data.cmd.fEOP)
                {
                    if (fRc && pThisCC->CTX_SUFF(pTxSg))
                    {
                        Assert(pThisCC->CTX_SUFF(pTxSg)->cSegs == 1);
                        if (pThis->fIPcsum)
                            e1kInsertChecksum(pThis, (uint8_t *)pThisCC->CTX_SUFF(pTxSg)->aSegs[0].pvSeg, pThis->u16TxPktLen,
                                              pThis->contextNormal.ip.u8CSO,
                                              pThis->contextNormal.ip.u8CSS,
                                              pThis->contextNormal.ip.u16CSE);
                        if (pThis->fTCPcsum)
                            e1kInsertChecksum(pThis, (uint8_t *)pThisCC->CTX_SUFF(pTxSg)->aSegs[0].pvSeg, pThis->u16TxPktLen,
                                              pThis->contextNormal.tu.u8CSO,
                                              pThis->contextNormal.tu.u8CSS,
                                              pThis->contextNormal.tu.u16CSE,
                                              !pThis->contextNormal.dw2.fTCP);
                        e1kTransmitFrame(pDevIns, pThis, pThisCC, fOnWorkerThread);
                    }
                    else
                        e1kXmitFreeBuf(pThis);
                    pThis->u16TxPktLen = 0;
                }
            }
            else
            {
                STAM_COUNTER_INC(&pThis->StatTxPathFallback);
                e1kFallbackAddToFrame(pDevIns, pThis, pDesc, pDesc->data.cmd.u20DTALEN, fOnWorkerThread);
            }

            e1kDescReport(pThis, pDesc, addr);
            STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);
            break;
        }

        case E1K_DTYP_LEGACY:
            if (pDesc->legacy.cmd.u16Length == 0 || pDesc->legacy.u64BufAddr == 0)
            {
                E1kLog(("%s Empty legacy descriptor, skipped.\n", pThis->szPrf));
                /** @todo 3.3.3, Length/Buffer Address: RS set -> write DD when processing. */
                break;
            }
            STAM_COUNTER_INC(&pThis->StatTxDescLegacy);
            STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatTransmit), a);

            /* First fragment: allocate new buffer. */
            if (pThis->u16TxPktLen == 0)
            {
                if (pDesc->legacy.cmd.fEOP)
                    cbVTag = pDesc->legacy.cmd.fVLE ? 4 : 0;
                else
                    cbVTag = 4;
                E1kLog3(("%s About to allocate TX buffer: cbVTag=%u\n", pThis->szPrf, cbVTag));
                /** @todo reset status bits? */
                rc = e1kXmitAllocBuf(pThis, pThisCC, pDesc->legacy.cmd.u16Length + cbVTag, pDesc->legacy.cmd.fEOP, false /*fGso*/);
                if (RT_FAILURE(rc))
                {
                    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);
                    break;
                }

                /** @todo Is there any way to indicating errors other than collisions? Like
                 *        VERR_NET_DOWN. */
            }

            /* Add fragment to frame. */
            if (e1kAddToFrame(pDevIns, pThis, pThisCC, pDesc->data.u64BufAddr, pDesc->legacy.cmd.u16Length))
            {
                E1K_INC_ISTAT_CNT(pThis->uStatDescLeg);

                /* Last fragment: Transmit and reset the packet storage counter.  */
                if (pDesc->legacy.cmd.fEOP)
                {
                    pThis->fVTag       = pDesc->legacy.cmd.fVLE;
                    pThis->u16VTagTCI  = pDesc->legacy.dw3.u16Special;
                    /** @todo Offload processing goes here. */
                    e1kTransmitFrame(pDevIns, pThis, pThisCC, fOnWorkerThread);
                    pThis->u16TxPktLen = 0;
                }
            }
            /* Last fragment + failure: free the buffer and reset the storage counter. */
            else if (pDesc->legacy.cmd.fEOP)
            {
                e1kXmitFreeBuf(pThis);
                pThis->u16TxPktLen = 0;
            }

            e1kDescReport(pThis, pDesc, addr);
            STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);
            break;

        default:
            E1kLog(("%s ERROR Unsupported transmit descriptor type: 0x%04x\n",
                    pThis->szPrf, e1kGetDescType(pDesc)));
            break;
    }

    return rc;
}

#else /* E1K_WITH_TXD_CACHE */

/**
 * Process Transmit Descriptor.
 *
 * E1000 supports three types of transmit descriptors:
 * - legacy   data descriptors of older format (context-less).
 * - data     the same as legacy but providing new offloading capabilities.
 * - context  sets up the context for following data descriptors.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The device state structure.
 * @param   pThisCC         The current context instance data.
 * @param   pDesc           Pointer to descriptor union.
 * @param   addr            Physical address of descriptor in guest memory.
 * @param   fOnWorkerThread Whether we're on a worker thread or an EMT.
 * @param   cbPacketSize    Size of the packet as previously computed.
 * @thread  E1000_TX
 */
static int e1kXmitDesc(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KSTATECC pThisCC, E1KTXDESC *pDesc,
                       RTGCPHYS addr, bool fOnWorkerThread)
{
    int rc = VINF_SUCCESS;

    e1kPrintTDesc(pThis, pDesc, "vvv");

//#ifdef E1K_USE_TX_TIMERS
    if (pThis->fTidEnabled)
        PDMDevHlpTimerStop(pDevIns, pThis->hTIDTimer);
//#endif /* E1K_USE_TX_TIMERS */

    switch (e1kGetDescType(pDesc))
    {
        case E1K_DTYP_CONTEXT:
            /* The caller have already updated the context */
            E1K_INC_ISTAT_CNT(pThis->uStatDescCtx);
            e1kDescReport(pDevIns, pThis, pDesc, addr);
            break;

        case E1K_DTYP_DATA:
        {
            STAM_COUNTER_INC(pDesc->data.cmd.fTSE?
                             &pThis->StatTxDescTSEData:
                             &pThis->StatTxDescData);
            E1K_INC_ISTAT_CNT(pThis->uStatDescDat);
            STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatTransmit), a);
            if (pDesc->data.cmd.u20DTALEN == 0 || pDesc->data.u64BufAddr == 0)
            {
                E1kLog2(("%s Empty data descriptor, skipped.\n", pThis->szPrf));
                if (pDesc->data.cmd.fEOP)
                {
                    e1kTransmitFrame(pDevIns, pThis, pThisCC, fOnWorkerThread);
                    pThis->u16TxPktLen = 0;
                }
            }
            else
            {
                /*
                 * Add the descriptor data to the frame.  If the frame is complete,
                 * transmit it and reset the u16TxPktLen field.
                 */
                if (e1kXmitIsGsoBuf(pThisCC->CTX_SUFF(pTxSg)))
                {
                    STAM_COUNTER_INC(&pThis->StatTxPathGSO);
                    bool fRc = e1kAddToFrame(pDevIns, pThis, pThisCC, pDesc->data.u64BufAddr, pDesc->data.cmd.u20DTALEN);
                    if (pDesc->data.cmd.fEOP)
                    {
                        if (   fRc
                            && pThisCC->CTX_SUFF(pTxSg)
                            && pThisCC->CTX_SUFF(pTxSg)->cbUsed == (size_t)pThis->contextTSE.dw3.u8HDRLEN + pThis->contextTSE.dw2.u20PAYLEN)
                        {
                            e1kTransmitFrame(pDevIns, pThis, pThisCC, fOnWorkerThread);
                            E1K_INC_CNT32(TSCTC);
                        }
                        else
                        {
                            if (fRc)
                                E1kLog(("%s bad GSO/TSE %p or %u < %u\n" , pThis->szPrf,
                                        pThisCC->CTX_SUFF(pTxSg), pThisCC->CTX_SUFF(pTxSg) ? pThisCC->CTX_SUFF(pTxSg)->cbUsed : 0,
                                        pThis->contextTSE.dw3.u8HDRLEN + pThis->contextTSE.dw2.u20PAYLEN));
                            e1kXmitFreeBuf(pThis, pThisCC);
                            E1K_INC_CNT32(TSCTFC);
                        }
                        pThis->u16TxPktLen = 0;
                    }
                }
                else if (!pDesc->data.cmd.fTSE)
                {
                    STAM_COUNTER_INC(&pThis->StatTxPathRegular);
                    bool fRc = e1kAddToFrame(pDevIns, pThis, pThisCC, pDesc->data.u64BufAddr, pDesc->data.cmd.u20DTALEN);
                    if (pDesc->data.cmd.fEOP)
                    {
                        if (fRc && pThisCC->CTX_SUFF(pTxSg))
                        {
                            Assert(pThisCC->CTX_SUFF(pTxSg)->cSegs == 1);
                            if (pThis->fIPcsum)
                                e1kInsertChecksum(pThis, (uint8_t *)pThisCC->CTX_SUFF(pTxSg)->aSegs[0].pvSeg, pThis->u16TxPktLen,
                                                  pThis->contextNormal.ip.u8CSO,
                                                  pThis->contextNormal.ip.u8CSS,
                                                  pThis->contextNormal.ip.u16CSE);
                            if (pThis->fTCPcsum)
                                e1kInsertChecksum(pThis, (uint8_t *)pThisCC->CTX_SUFF(pTxSg)->aSegs[0].pvSeg, pThis->u16TxPktLen,
                                                  pThis->contextNormal.tu.u8CSO,
                                                  pThis->contextNormal.tu.u8CSS,
                                                  pThis->contextNormal.tu.u16CSE,
                                                  !pThis->contextNormal.dw2.fTCP);
                            e1kTransmitFrame(pDevIns, pThis, pThisCC, fOnWorkerThread);
                        }
                        else
                            e1kXmitFreeBuf(pThis, pThisCC);
                        pThis->u16TxPktLen = 0;
                    }
                }
                else
                {
                    STAM_COUNTER_INC(&pThis->StatTxPathFallback);
                    rc = e1kFallbackAddToFrame(pDevIns, pThis, pDesc, fOnWorkerThread);
                }
            }
            e1kDescReport(pDevIns, pThis, pDesc, addr);
            STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);
            break;
        }

        case E1K_DTYP_LEGACY:
            STAM_COUNTER_INC(&pThis->StatTxDescLegacy);
            STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatTransmit), a);
            if (pDesc->legacy.cmd.u16Length == 0 || pDesc->legacy.u64BufAddr == 0)
            {
                E1kLog(("%s Empty legacy descriptor, skipped.\n", pThis->szPrf));
                if (pDesc->data.cmd.fEOP)
                {
                    e1kTransmitFrame(pDevIns, pThis, pThisCC, fOnWorkerThread);
                    pThis->u16TxPktLen = 0;
                }
            }
            else
            {
                /* Add fragment to frame. */
                if (e1kAddToFrame(pDevIns, pThis, pThisCC, pDesc->data.u64BufAddr, pDesc->legacy.cmd.u16Length))
                {
                    E1K_INC_ISTAT_CNT(pThis->uStatDescLeg);

                    /* Last fragment: Transmit and reset the packet storage counter.  */
                    if (pDesc->legacy.cmd.fEOP)
                    {
                        if (pDesc->legacy.cmd.fIC)
                        {
                            e1kInsertChecksum(pThis,
                                              (uint8_t *)pThisCC->CTX_SUFF(pTxSg)->aSegs[0].pvSeg,
                                              pThis->u16TxPktLen,
                                              pDesc->legacy.cmd.u8CSO,
                                              pDesc->legacy.dw3.u8CSS,
                                              0);
                        }
                        e1kTransmitFrame(pDevIns, pThis, pThisCC, fOnWorkerThread);
                        pThis->u16TxPktLen = 0;
                    }
                }
                /* Last fragment + failure: free the buffer and reset the storage counter. */
                else if (pDesc->legacy.cmd.fEOP)
                {
                    e1kXmitFreeBuf(pThis, pThisCC);
                    pThis->u16TxPktLen = 0;
                }
            }
            e1kDescReport(pDevIns, pThis, pDesc, addr);
            STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);
            break;

        default:
            E1kLog(("%s ERROR Unsupported transmit descriptor type: 0x%04x\n",
                    pThis->szPrf, e1kGetDescType(pDesc)));
            break;
    }

    return rc;
}

DECLINLINE(bool) e1kUpdateTxContext(PE1KSTATE pThis, E1KTXDESC *pDesc)
{
    if (pDesc->context.dw2.fTSE)
    {
        if (!e1kSetupGsoCtx(&pThis->GsoCtx, &pDesc->context))
        {
            pThis->contextTSE.dw2.u4DTYP = E1K_DTYP_INVALID;
            return false;
        }
        pThis->contextTSE = pDesc->context;
        uint32_t cbMaxSegmentSize = pThis->contextTSE.dw3.u16MSS + pThis->contextTSE.dw3.u8HDRLEN + 4; /*VTAG*/
        if (RT_UNLIKELY(cbMaxSegmentSize > E1K_MAX_TX_PKT_SIZE))
        {
            pThis->contextTSE.dw3.u16MSS = E1K_MAX_TX_PKT_SIZE - pThis->contextTSE.dw3.u8HDRLEN - 4; /*VTAG*/
            LogRelMax(10, ("%s: Transmit packet is too large: %u > %u(max). Adjusted MSS to %u.\n",
                           pThis->szPrf, cbMaxSegmentSize, E1K_MAX_TX_PKT_SIZE, pThis->contextTSE.dw3.u16MSS));
        }
        pThis->u32PayRemain = pThis->contextTSE.dw2.u20PAYLEN;
        pThis->u16HdrRemain = pThis->contextTSE.dw3.u8HDRLEN;
        e1kSetupGsoCtx(&pThis->GsoCtx, &pThis->contextTSE);
        STAM_COUNTER_INC(&pThis->StatTxDescCtxTSE);
    }
    else
    {
        pThis->contextNormal = pDesc->context;
        STAM_COUNTER_INC(&pThis->StatTxDescCtxNormal);
    }
    E1kLog2(("%s %s context updated: IP CSS=%02X, IP CSO=%02X, IP CSE=%04X"
             ", TU CSS=%02X, TU CSO=%02X, TU CSE=%04X\n", pThis->szPrf,
             pDesc->context.dw2.fTSE ? "TSE" : "Normal",
             pDesc->context.ip.u8CSS,
             pDesc->context.ip.u8CSO,
             pDesc->context.ip.u16CSE,
             pDesc->context.tu.u8CSS,
             pDesc->context.tu.u8CSO,
             pDesc->context.tu.u16CSE));
    return true; /* Consider returning false for invalid descriptors */
}

enum E1kPacketType
{
    E1K_PACKET_NONE = 0,
    E1K_PACKET_LEGACY,
    E1K_PACKET_NORMAL,
    E1K_PACKET_TSE
};

static int e1kLocateTxPacket(PE1KSTATE pThis, PE1KTXDC pTxdc)
{
    LogFlow(("%s e1kLocateTxPacket: ENTER cbTxAlloc=%d\n",
             pThis->szPrf, pThis->cbTxAlloc));
    /* Check if we have located the packet already. */
    if (pThis->cbTxAlloc)
    {
        LogFlow(("%s e1kLocateTxPacket: RET true cbTxAlloc=%d\n",
                 pThis->szPrf, pThis->cbTxAlloc));
        return true;
    }

    pThis->fGSO = false;
    pThis->fVTag = false;
    pThis->fIPcsum  = false;
    pThis->fTCPcsum = false;
    pThis->u16TxPktLen = 0;

    enum E1kPacketType packetType = E1K_PACKET_NONE;
    enum E1kPacketType expectedPacketType = E1K_PACKET_NONE;
    /*
     * Valid packets start with 1 or 0 context descriptors, followed by 1 or
     * more data descriptors of the same type: legacy, normal or TSE. Note
     * that legacy descriptors do not belong to neither normal nor segmentation
     * contexts rendering the sequence (context_descriptor, legacy_descriptor)
     * invalid, but the context descriptor will still be applied and the legacy
     * descriptor will be treated as the beginning of next packet.
     */
    bool fInvalidPacket = false;
    bool fTSE = false;
    uint32_t cbPacket = 0;

    /* Since we process one packet at a time we will only mark current packet's descriptors as valid */
    memset(pThis->afTxDValid, 0, sizeof(pThis->afTxDValid));
    for (int i = pThis->iTxDCurrent; i < pThis->nTxDFetched; ++i)
    {
        E1KTXDESC *pDesc = &pThis->aTxDescriptors[i];

        switch (e1kGetDescType(pDesc))
        {
            case E1K_DTYP_CONTEXT:
                /* There can be only one context per packet. Each context descriptor starts a new packet. */
                if (packetType != E1K_PACKET_NONE)
                {
                    fInvalidPacket = true;
                    break;
                }
                packetType = (pDesc->context.dw2.fTSE) ? E1K_PACKET_TSE : E1K_PACKET_NORMAL;
                if (cbPacket == 0)
                    pThis->afTxDValid[i] = e1kUpdateTxContext(pThis, pDesc);
                else
                    E1kLog(("%s e1kLocateTxPacket: ignoring a context descriptor in the middle of a packet, cbPacket=%d\n",
                            pThis->szPrf, cbPacket));
                continue;
            case E1K_DTYP_LEGACY:
                if (packetType != E1K_PACKET_NONE && packetType != E1K_PACKET_LEGACY)
                {
                    fInvalidPacket = true;
                    break;
                }
                packetType = E1K_PACKET_LEGACY;
                /* Skip invalid descriptors. */
                if (cbPacket > 0 && (pThis->fGSO || fTSE))
                {
                    E1kLog(("%s e1kLocateTxPacket: ignoring a legacy descriptor in the segmentation context, cbPacket=%d\n",
                            pThis->szPrf, cbPacket));
                    continue;
                }
                pThis->afTxDValid[i] = true; /* Passed all checks, process it */

                /* Skip empty descriptors. */
                if (!pDesc->legacy.u64BufAddr || !pDesc->legacy.cmd.u16Length)
                    break;
                cbPacket += pDesc->legacy.cmd.u16Length;
                pThis->fGSO = false;
                break;
            case E1K_DTYP_DATA:
                expectedPacketType = pDesc->data.cmd.fTSE ? E1K_PACKET_TSE : E1K_PACKET_NORMAL;
                if (packetType != E1K_PACKET_NONE && packetType != expectedPacketType)
                {
                    fInvalidPacket = true;
                    break;
                }
                /* Skip invalid descriptors. */
                if (pDesc->data.cmd.fTSE)
                {
                    if (pThis->contextTSE.dw2.u4DTYP == E1K_DTYP_INVALID)
                    {
                        E1kLog(("%s e1kLocateTxPacket: ignoring TSE descriptor in invalid segmentation context, cbPacket=%d\n",
                                pThis->szPrf, cbPacket));
                        continue;
                    }
                }
                else /* !TSE */
                {
                    if (pThis->contextNormal.dw2.u4DTYP == E1K_DTYP_INVALID)
                    {
                        E1kLog(("%s e1kLocateTxPacket: ignoring non-TSE descriptor in invalid normal context, cbPacket=%d\n",
                                pThis->szPrf, cbPacket));
                        continue;
                    }
                }
                if (cbPacket > 0 && (bool)pDesc->data.cmd.fTSE != fTSE)
                {
                    E1kLog(("%s e1kLocateTxPacket: ignoring %sTSE descriptor in the %ssegmentation context, cbPacket=%d\n",
                            pThis->szPrf, pDesc->data.cmd.fTSE ? "" : "non-", fTSE ? "" : "non-", cbPacket));
                    continue;
                }
                pThis->afTxDValid[i] = true; /* Passed all checks, process it */

                /* Skip empty descriptors. */
                if (!pDesc->data.u64BufAddr || !pDesc->data.cmd.u20DTALEN)
                    break;
                if (cbPacket == 0)
                {
                    /*
                     * The first fragment: save IXSM and TXSM options
                     * as these are only valid in the first fragment.
                     */
                    pThis->fIPcsum  = pDesc->data.dw3.fIXSM;
                    pThis->fTCPcsum = pDesc->data.dw3.fTXSM;
                            fTSE     = pDesc->data.cmd.fTSE;
                    /*
                     * TSE descriptors have VLE bit properly set in
                     * the first fragment.
                     */
                    if (fTSE)
                    {
                        pThis->fVTag = pDesc->data.cmd.fVLE;
                        pThis->u16VTagTCI = pDesc->data.dw3.u16Special;
                    }
                    pThis->fGSO = e1kCanDoGso(pThis, &pThis->GsoCtx, &pDesc->data, &pThis->contextTSE);
                }
                cbPacket += pDesc->data.cmd.u20DTALEN;
                break;
            default:
                AssertMsgFailed(("Impossible descriptor type!"));
                continue;
        }
        if (fInvalidPacket)
        {
            for (int index = pThis->iTxDCurrent; index < i; ++index)
                pThis->afTxDValid[index] = false; /* Make sure all descriptors for this packet are skipped by processing */
            LogFlow(("%s e1kLocateTxPacket: marked %d descriptors as invalid\n", pThis->szPrf, i - pThis->iTxDCurrent));
            LogFlow(("%s e1kLocateTxPacket: RET true cbTxAlloc=%d cbPacket=%d%s%s\n",
                     pThis->szPrf, pThis->cbTxAlloc, cbPacket,
                     pThis->fGSO ? " GSO" : "", fTSE ? " TSE" : ""));
            pTxdc->nextPacket = i;
            return true;
        }
        if (pDesc->legacy.cmd.fEOP)
        {
            /*
             * Non-TSE descriptors have VLE bit properly set in
             * the last fragment.
             */
            if (!fTSE)
            {
                pThis->fVTag = pDesc->data.cmd.fVLE;
                pThis->u16VTagTCI = pDesc->data.dw3.u16Special;
            }
            /*
             * Compute the required buffer size. If we cannot do GSO but still
             * have to do segmentation we allocate the first segment only.
             */
            pThis->cbTxAlloc = (!fTSE || pThis->fGSO) ?
                cbPacket :
                RT_MIN(cbPacket, pThis->contextTSE.dw3.u16MSS + pThis->contextTSE.dw3.u8HDRLEN);
            /* Do not add VLAN tags to empty packets. */
            if (pThis->fVTag && pThis->cbTxAlloc > 0)
                pThis->cbTxAlloc += 4;
            LogFlow(("%s e1kLocateTxPacket: RET true cbTxAlloc=%d cbPacket=%d%s%s\n",
                     pThis->szPrf, pThis->cbTxAlloc, cbPacket,
                     pThis->fGSO ? " GSO" : "", fTSE ? " TSE" : ""));
            pTxdc->nextPacket = i + 1;
            return true;
        }
    }

    if (cbPacket == 0 && pThis->nTxDFetched - pThis->iTxDCurrent > 0)
    {
        /* All descriptors were empty, we need to process them as a dummy packet */
        LogFlow(("%s e1kLocateTxPacket: RET true cbTxAlloc=%d, zero packet!\n",
                 pThis->szPrf, pThis->cbTxAlloc));
        pTxdc->nextPacket = pThis->nTxDFetched;
        return true;
    }
    LogFlow(("%s e1kLocateTxPacket: RET false cbTxAlloc=%d cbPacket=%d\n",
             pThis->szPrf, pThis->cbTxAlloc, cbPacket));
    return false;
}

static int e1kXmitPacket(PPDMDEVINS pDevIns, PE1KSTATE pThis, bool fOnWorkerThread, PE1KTXDC pTxdc)
{
    PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
    int rc = VINF_SUCCESS;

    LogFlow(("%s e1kXmitPacket: ENTER current=%d fetched=%d\n",
             pThis->szPrf, pThis->iTxDCurrent, pThis->nTxDFetched));

    while (pThis->iTxDCurrent < pTxdc->nextPacket && pThis->iTxDCurrent < pThis->nTxDFetched)
    {
        E1KTXDESC *pDesc = &pThis->aTxDescriptors[pThis->iTxDCurrent];
        E1kLog3(("%s About to process new TX descriptor at %08x%08x, TDLEN=%08x, TDH=%08x, TDT=%08x\n",
                 pThis->szPrf, TDBAH, TDBAL + pTxdc->tdh * sizeof(E1KTXDESC), pTxdc->tdlen, pTxdc->tdh, pTxdc->tdt));
        if (!pThis->afTxDValid[pThis->iTxDCurrent])
        {
            e1kPrintTDesc(pThis, pDesc, "vvv");
            E1kLog(("%s e1kXmitDesc: skipping bad descriptor ^^^\n", pThis->szPrf));
            e1kDescReport(pDevIns, pThis, pDesc, e1kDescAddr(TDBAH, TDBAL, pTxdc->tdh));
            rc = VINF_SUCCESS;
        }
        else
            rc = e1kXmitDesc(pDevIns, pThis, pThisCC, pDesc, e1kDescAddr(TDBAH, TDBAL, pTxdc->tdh), fOnWorkerThread);
        if (RT_FAILURE(rc))
            break;
        if (++pTxdc->tdh * sizeof(E1KTXDESC) >= pTxdc->tdlen)
            pTxdc->tdh = 0;
        TDH = pTxdc->tdh; /* Sync the actual register and TXDC */
        uint32_t uLowThreshold = GET_BITS(TXDCTL, LWTHRESH)*8;
        if (uLowThreshold != 0 && e1kGetTxLen(pTxdc) <= uLowThreshold)
        {
            E1kLog2(("%s Low on transmit descriptors, raise ICR.TXD_LOW, len=%x thresh=%x\n",
                     pThis->szPrf, e1kGetTxLen(pTxdc), GET_BITS(TXDCTL, LWTHRESH)*8));
            e1kRaiseInterrupt(pDevIns, pThis, VERR_SEM_BUSY, ICR_TXD_LOW);
        }
        ++pThis->iTxDCurrent;
        if (e1kGetDescType(pDesc) != E1K_DTYP_CONTEXT && pDesc->legacy.cmd.fEOP)
            break;
    }

    LogFlow(("%s e1kXmitPacket: RET %Rrc current=%d fetched=%d\n",
             pThis->szPrf, rc, pThis->iTxDCurrent, pThis->nTxDFetched));
    return rc;
}

#endif /* E1K_WITH_TXD_CACHE */
#ifndef E1K_WITH_TXD_CACHE

/**
 * Transmit pending descriptors.
 *
 * @returns VBox status code.  VERR_TRY_AGAIN is returned if we're busy.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The E1000 state.
 * @param   fOnWorkerThread     Whether we're on a worker thread or on an EMT.
 */
static int e1kXmitPending(PPDMDEVINS pDevIns, PE1KSTATE pThis, bool fOnWorkerThread)
{
    int rc = VINF_SUCCESS;
    PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);

    /* Check if transmitter is enabled. */
    if (!(TCTL & TCTL_EN))
        return VINF_SUCCESS;
    /*
     * Grab the xmit lock of the driver as well as the E1K device state.
     */
    rc = e1kCsTxEnter(pThis, VERR_SEM_BUSY);
    if (RT_LIKELY(rc == VINF_SUCCESS))
    {
        PPDMINETWORKUP pDrv = pThis->CTX_SUFF(pDrv);
        if (pDrv)
        {
            rc = pDrv->pfnBeginXmit(pDrv, fOnWorkerThread);
            if (RT_FAILURE(rc))
            {
                e1kCsTxLeave(pThis);
                return rc;
            }
        }
        /*
         * Process all pending descriptors.
         * Note! Do not process descriptors in locked state
         */
        while (TDH != TDT && !pThis->fLocked)
        {
            E1KTXDESC desc;
            E1kLog3(("%s About to process new TX descriptor at %08x%08x, TDLEN=%08x, TDH=%08x, TDT=%08x\n",
                     pThis->szPrf, TDBAH, TDBAL + TDH * sizeof(desc), TDLEN, TDH, TDT));

            e1kLoadDesc(pDevIns, &desc, ((uint64_t)TDBAH << 32) + TDBAL + TDH * sizeof(desc));
            rc = e1kXmitDesc(pDevIns, pThis, pThisCC, &desc, e1kDescAddr(TDBAH, TDBAL, TDH), fOnWorkerThread);
            /* If we failed to transmit descriptor we will try it again later */
            if (RT_FAILURE(rc))
                break;
            if (++TDH * sizeof(desc) >= TDLEN)
                TDH = 0;

            if (e1kGetTxLen(pThis) <= GET_BITS(TXDCTL, LWTHRESH)*8)
            {
                E1kLog2(("%s Low on transmit descriptors, raise ICR.TXD_LOW, len=%x thresh=%x\n",
                         pThis->szPrf, e1kGetTxLen(pThis), GET_BITS(TXDCTL, LWTHRESH)*8));
                e1kRaiseInterrupt(pDevIns, pThis, VERR_SEM_BUSY, ICR_TXD_LOW);
            }

            STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);
        }

        /// @todo uncomment: pThis->uStatIntTXQE++;
        /// @todo uncomment: e1kRaiseInterrupt(pDevIns, pThis, ICR_TXQE);
        /*
         * Release the lock.
         */
        if (pDrv)
            pDrv->pfnEndXmit(pDrv);
        e1kCsTxLeave(pThis);
    }

    return rc;
}

#else /* E1K_WITH_TXD_CACHE */

static void e1kDumpTxDCache(PPDMDEVINS pDevIns, PE1KSTATE pThis, PE1KTXDC pTxdc)
{
    unsigned i, cDescs = pTxdc->tdlen / sizeof(E1KTXDESC);
    uint32_t tdh = pTxdc->tdh;
    LogRel(("E1000: -- Transmit Descriptors (%d total) --\n", cDescs));
    for (i = 0; i < cDescs; ++i)
    {
        E1KTXDESC desc;
        PDMDevHlpPCIPhysRead(pDevIns , e1kDescAddr(TDBAH, TDBAL, i), &desc, sizeof(desc));
        if (i == tdh)
            LogRel(("E1000: >>> "));
        LogRel(("E1000: %RGp: %R[e1ktxd]\n", e1kDescAddr(TDBAH, TDBAL, i), &desc));
    }
    LogRel(("E1000: -- Transmit Descriptors in Cache (at %d (TDH %d)/ fetched %d / max %d) --\n",
            pThis->iTxDCurrent, pTxdc->tdh, pThis->nTxDFetched, E1K_TXD_CACHE_SIZE));
    if (tdh > pThis->iTxDCurrent)
        tdh -= pThis->iTxDCurrent;
    else
        tdh = cDescs + tdh - pThis->iTxDCurrent;
    for (i = 0; i < pThis->nTxDFetched; ++i)
    {
        if (i == pThis->iTxDCurrent)
            LogRel(("E1000: >>> "));
        if (cDescs)
            LogRel(("E1000: %RGp: %R[e1ktxd]\n", e1kDescAddr(TDBAH, TDBAL, tdh++ % cDescs), &pThis->aTxDescriptors[i]));
        else
            LogRel(("E1000: <lost>: %R[e1ktxd]\n", &pThis->aTxDescriptors[i]));
    }
}

/**
 * Transmit pending descriptors.
 *
 * @returns VBox status code.  VERR_TRY_AGAIN is returned if we're busy.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The E1000 state.
 * @param   fOnWorkerThread     Whether we're on a worker thread or on an EMT.
 */
static int e1kXmitPending(PPDMDEVINS pDevIns, PE1KSTATE pThis, bool fOnWorkerThread)
{
    PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
    int rc = VINF_SUCCESS;

    /* Check if transmitter is enabled. */
    if (!(TCTL & TCTL_EN))
        return VINF_SUCCESS;
    /*
     * Grab the xmit lock of the driver as well as the E1K device state.
     */
    PPDMINETWORKUP pDrv = pThisCC->CTX_SUFF(pDrv);
    if (pDrv)
    {
        rc = pDrv->pfnBeginXmit(pDrv, fOnWorkerThread);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Process all pending descriptors.
     * Note! Do not process descriptors in locked state
     */
    rc = e1kCsTxEnter(pThis, VERR_SEM_BUSY);
    if (RT_LIKELY(rc == VINF_SUCCESS && (TCTL & TCTL_EN)))
    {
        E1KTXDC txdc;
        bool fTxContextValid = e1kUpdateTxDContext(pDevIns, pThis, &txdc);
        STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatTransmit), a);
        /*
         * fIncomplete is set whenever we try to fetch additional descriptors
         * for an incomplete packet. If fail to locate a complete packet on
         * the next iteration we need to reset the cache or we risk to get
         * stuck in this loop forever.
         */
        bool fIncomplete = false;
        while (fTxContextValid && !pThis->fLocked && e1kTxDLazyLoad(pDevIns, pThis, &txdc))
        {
            while (e1kLocateTxPacket(pThis, &txdc))
            {
                Log4(("%s e1kXmitPending: Located packet at %d. Next packet at %d\n",
                      pThis->szPrf, pThis->iTxDCurrent, txdc.nextPacket));
                fIncomplete = false;
                /* Found a complete packet, allocate it. */
                rc = e1kXmitAllocBuf(pThis, pThisCC, pThis->fGSO);
                /* If we're out of bandwidth we'll come back later. */
                if (RT_FAILURE(rc))
                    goto out;
                /* Copy the packet to allocated buffer and send it. */
                rc = e1kXmitPacket(pDevIns, pThis, fOnWorkerThread, &txdc);
                /* If we're out of bandwidth we'll come back later. */
                if (RT_FAILURE(rc))
                    goto out;
            }
            uint8_t u8Remain = pThis->nTxDFetched - pThis->iTxDCurrent;
            if (RT_UNLIKELY(fIncomplete))
            {
                static bool fTxDCacheDumped = false;
                /*
                 * The descriptor cache is full, but we were unable to find
                 * a complete packet in it. Drop the cache and hope that
                 * the guest driver can recover from network card error.
                 */
                LogRel(("%s: No complete packets in%s TxD cache! "
                      "Fetched=%d, current=%d, TX len=%d.\n",
                      pThis->szPrf,
                      u8Remain == E1K_TXD_CACHE_SIZE ? " full" : "",
                      pThis->nTxDFetched, pThis->iTxDCurrent,
                      e1kGetTxLen(&txdc)));
                if (!fTxDCacheDumped)
                {
                    fTxDCacheDumped = true;
                    e1kDumpTxDCache(pDevIns, pThis, &txdc);
                }
                pThis->iTxDCurrent = pThis->nTxDFetched = 0;
                /*
                 * Returning an error at this point means Guru in R0
                 * (see @bugref{6428}).
                 */
# ifdef IN_RING3
                rc = VERR_NET_INCOMPLETE_TX_PACKET;
# else /* !IN_RING3 */
                rc = VINF_IOM_R3_MMIO_WRITE;
# endif /* !IN_RING3 */
                goto out;
            }
            if (u8Remain > 0)
            {
                Log4(("%s Incomplete packet at %d. Already fetched %d, "
                      "%d more are available\n",
                      pThis->szPrf, pThis->iTxDCurrent, u8Remain,
                      e1kGetTxLen(&txdc) - u8Remain));

                /*
                 * A packet was partially fetched. Move incomplete packet to
                 * the beginning of cache buffer, then load more descriptors.
                 */
                memmove(pThis->aTxDescriptors,
                        &pThis->aTxDescriptors[pThis->iTxDCurrent],
                        u8Remain * sizeof(E1KTXDESC));
                pThis->iTxDCurrent = 0;
                pThis->nTxDFetched = u8Remain;
                e1kTxDLoadMore(pDevIns, pThis, &txdc);
                fIncomplete = true;
            }
            else
                pThis->nTxDFetched = 0;
            pThis->iTxDCurrent = 0;
        }
        if (!pThis->fLocked && GET_BITS(TXDCTL, LWTHRESH) == 0)
        {
            E1kLog2(("%s Out of transmit descriptors, raise ICR.TXD_LOW\n",
                     pThis->szPrf));
            e1kRaiseInterrupt(pDevIns, pThis, VERR_SEM_BUSY, ICR_TXD_LOW);
        }
out:
        STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatTransmit), a);

        /// @todo uncomment: pThis->uStatIntTXQE++;
        /// @todo uncomment: e1kRaiseInterrupt(pDevIns, pThis, ICR_TXQE);

        e1kCsTxLeave(pThis);
    }


    /*
     * Release the lock.
     */
    if (pDrv)
        pDrv->pfnEndXmit(pDrv);
    return rc;
}

#endif /* E1K_WITH_TXD_CACHE */
#ifdef IN_RING3

/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnXmitPending}
 */
static DECLCALLBACK(void) e1kR3NetworkDown_XmitPending(PPDMINETWORKDOWN pInterface)
{
    PE1KSTATECC pThisCC = RT_FROM_MEMBER(pInterface, E1KSTATECC, INetworkDown);
    PE1KSTATE   pThis = pThisCC->pShared;
    /* Resume suspended transmission */
    STATUS &= ~STATUS_TXOFF;
    e1kXmitPending(pThisCC->pDevInsR3, pThis, true /*fOnWorkerThread*/);
}

/**
 * @callback_method_impl{FNPDMTASKDEV,
 *      Executes e1kXmitPending at the behest of ring-0/raw-mode.}
 * @note Not executed on EMT.
 */
static DECLCALLBACK(void) e1kR3TxTaskCallback(PPDMDEVINS pDevIns, void *pvUser)
{
    PE1KSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    E1kLog2(("%s e1kR3TxTaskCallback:\n", pThis->szPrf));

    int rc = e1kXmitPending(pDevIns, pThis, false /*fOnWorkerThread*/);
    AssertMsg(RT_SUCCESS(rc) || rc == VERR_TRY_AGAIN || rc == VERR_NET_DOWN, ("%Rrc\n", rc));

    RT_NOREF(rc, pvUser);
}

#endif /* IN_RING3 */

/**
 * Write handler for Transmit Descriptor Tail register.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */
static int e1kRegWriteTDT(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    int rc = e1kRegWriteDefault(pDevIns, pThis, offset, index, value);

    /* All descriptors starting with head and not including tail belong to us. */
    /* Process them. */
    E1kLog2(("%s e1kRegWriteTDT: TDBAL=%08x, TDBAH=%08x, TDLEN=%08x, TDH=%08x, TDT=%08x\n",
            pThis->szPrf, TDBAL, TDBAH, TDLEN, TDH, TDT));

    /* Compose a temporary TX context, breaking TX CS rule, for debugging purposes. */
    /* If we decide to transmit, the TX critical section will be entered later in e1kXmitPending(). */
    E1KTXDC txdc;
    txdc.tdlen = TDLEN;
    txdc.tdh   = TDH;
    txdc.tdt   = TDT;
    /* Ignore TDT writes when the link is down. */
    if (txdc.tdh != txdc.tdt && (STATUS & STATUS_LU))
    {
        Log5(("E1000: TDT write: TDH=%08x, TDT=%08x, %d descriptors to process\n", txdc.tdh, txdc.tdt, e1kGetTxLen(&txdc)));
        E1kLog(("%s e1kRegWriteTDT: %d descriptors to process\n",
                 pThis->szPrf, e1kGetTxLen(&txdc)));

        /* Transmit pending packets if possible, defer it if we cannot do it
           in the current context. */
#ifdef E1K_TX_DELAY
        rc = e1kCsTxEnter(pThis, VERR_SEM_BUSY);
        if (RT_LIKELY(rc == VINF_SUCCESS))
        {
            if (!PDMDevInsTimerIsActive(pDevIns, pThis->hTXDTimer))
            {
# ifdef E1K_INT_STATS
                pThis->u64ArmedAt = RTTimeNanoTS();
# endif
                e1kArmTimer(pDevIns, pThis, pThis->hTXDTimer, E1K_TX_DELAY);
            }
            E1K_INC_ISTAT_CNT(pThis->uStatTxDelayed);
            e1kCsTxLeave(pThis);
            return rc;
        }
        /* We failed to enter the TX critical section -- transmit as usual. */
#endif /* E1K_TX_DELAY */
#ifndef IN_RING3
        PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
        if (!pThisCC->CTX_SUFF(pDrv))
        {
            PDMDevHlpTaskTrigger(pDevIns, pThis->hTxTask);
            rc = VINF_SUCCESS;
        }
        else
#endif
        {
            rc = e1kXmitPending(pDevIns, pThis, false /*fOnWorkerThread*/);
            if (   rc == VERR_TRY_AGAIN
                || rc == VERR_NET_DOWN)
                rc = VINF_SUCCESS;
#ifndef IN_RING3
            else if (rc == VERR_SEM_BUSY)
                rc = VINF_IOM_R3_MMIO_WRITE;
#endif
            AssertRC(rc);
        }
    }

    return rc;
}

/**
 * Write handler for Multicast Table Array registers.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @thread  EMT
 */
static int e1kRegWriteMTA(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    RT_NOREF_PV(pDevIns);
    AssertReturn(offset - g_aE1kRegMap[index].offset < sizeof(pThis->auMTA), VERR_DEV_IO_ERROR);
    pThis->auMTA[(offset - g_aE1kRegMap[index].offset) / sizeof(pThis->auMTA[0])] = value;

    return VINF_SUCCESS;
}

/**
 * Read handler for Multicast Table Array registers.
 *
 * @returns VBox status code.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @thread  EMT
 */
static int e1kRegReadMTA(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t *pu32Value)
{
    RT_NOREF_PV(pDevIns);
    AssertReturn(offset - g_aE1kRegMap[index].offset < sizeof(pThis->auMTA), VERR_DEV_IO_ERROR);
    *pu32Value = pThis->auMTA[(offset - g_aE1kRegMap[index].offset)/sizeof(pThis->auMTA[0])];

    return VINF_SUCCESS;
}

/**
 * Write handler for Receive Address registers.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @thread  EMT
 */
static int e1kRegWriteRA(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    RT_NOREF_PV(pDevIns);
    AssertReturn(offset - g_aE1kRegMap[index].offset < sizeof(pThis->aRecAddr.au32), VERR_DEV_IO_ERROR);
    pThis->aRecAddr.au32[(offset - g_aE1kRegMap[index].offset)/sizeof(pThis->aRecAddr.au32[0])] = value;

    return VINF_SUCCESS;
}

/**
 * Read handler for Receive Address registers.
 *
 * @returns VBox status code.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @thread  EMT
 */
static int e1kRegReadRA(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t *pu32Value)
{
    RT_NOREF_PV(pDevIns);
    AssertReturn(offset - g_aE1kRegMap[index].offset< sizeof(pThis->aRecAddr.au32), VERR_DEV_IO_ERROR);
    *pu32Value = pThis->aRecAddr.au32[(offset - g_aE1kRegMap[index].offset)/sizeof(pThis->aRecAddr.au32[0])];

    return VINF_SUCCESS;
}

/**
 * Write handler for VLAN Filter Table Array registers.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @thread  EMT
 */
static int e1kRegWriteVFTA(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    RT_NOREF_PV(pDevIns);
    AssertReturn(offset - g_aE1kRegMap[index].offset < sizeof(pThis->auVFTA), VINF_SUCCESS);
    pThis->auVFTA[(offset - g_aE1kRegMap[index].offset)/sizeof(pThis->auVFTA[0])] = value;

    return VINF_SUCCESS;
}

/**
 * Read handler for VLAN Filter Table Array registers.
 *
 * @returns VBox status code.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @thread  EMT
 */
static int e1kRegReadVFTA(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t *pu32Value)
{
    RT_NOREF_PV(pDevIns);
    AssertReturn(offset - g_aE1kRegMap[index].offset< sizeof(pThis->auVFTA), VERR_DEV_IO_ERROR);
    *pu32Value = pThis->auVFTA[(offset - g_aE1kRegMap[index].offset)/sizeof(pThis->auVFTA[0])];

    return VINF_SUCCESS;
}

/**
 * Read handler for unimplemented registers.
 *
 * Merely reports reads from unimplemented registers.
 *
 * @returns VBox status code.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @thread  EMT
 */
static int e1kRegReadUnimplemented(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, offset, index);
    E1kLog(("%s At %08X read (00000000) attempt from unimplemented register %s (%s)\n",
            pThis->szPrf, offset, g_aE1kRegMap[index].abbrev, g_aE1kRegMap[index].name));
    *pu32Value = 0;

    return VINF_SUCCESS;
}

/**
 * Default register read handler with automatic clear operation.
 *
 * Retrieves the value of register from register array in device state structure.
 * Then resets all bits.
 *
 * @remarks The 'mask' parameter is simply ignored as masking and shifting is
 *          done in the caller.
 *
 * @returns VBox status code.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @thread  EMT
 */
static int e1kRegReadAutoClear(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t *pu32Value)
{
    AssertReturn(index < E1K_NUM_OF_32BIT_REGS, VERR_DEV_IO_ERROR);
    int rc = e1kRegReadDefault(pDevIns, pThis, offset, index, pu32Value);
    pThis->auRegs[index] = 0;

    return rc;
}

/**
 * Default register read handler.
 *
 * Retrieves the value of register from register array in device state structure.
 * Bits corresponding to 0s in 'readable' mask will always read as 0s.
 *
 * @remarks The 'mask' parameter is simply ignored as masking and shifting is
 *          done in the caller.
 *
 * @returns VBox status code.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @thread  EMT
 */
static int e1kRegReadDefault(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t *pu32Value)
{
    RT_NOREF_PV(pDevIns); RT_NOREF_PV(offset);

    AssertReturn(index < E1K_NUM_OF_32BIT_REGS, VERR_DEV_IO_ERROR);
    *pu32Value = pThis->auRegs[index] & g_aE1kRegMap[index].readable;

    return VINF_SUCCESS;
}

/**
 * Write handler for unimplemented registers.
 *
 * Merely reports writes to unimplemented registers.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @thread  EMT
 */

 static int e1kRegWriteUnimplemented(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    RT_NOREF_PV(pDevIns); RT_NOREF_PV(pThis); RT_NOREF_PV(offset); RT_NOREF_PV(index); RT_NOREF_PV(value);

    E1kLog(("%s At %08X write attempt (%08X) to  unimplemented register %s (%s)\n",
            pThis->szPrf, offset, value, g_aE1kRegMap[index].abbrev, g_aE1kRegMap[index].name));

    return VINF_SUCCESS;
}

/**
 * Default register write handler.
 *
 * Stores the value to the register array in device state structure. Only bits
 * corresponding to 1s both in 'writable' and 'mask' will be stored.
 *
 * @returns VBox status code.
 *
 * @param   pThis       The device state structure.
 * @param   offset      Register offset in memory-mapped frame.
 * @param   index       Register index in register array.
 * @param   value       The value to store.
 * @param   mask        Used to implement partial writes (8 and 16-bit).
 * @thread  EMT
 */

static int e1kRegWriteDefault(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offset, uint32_t index, uint32_t value)
{
    RT_NOREF(pDevIns, offset);

    AssertReturn(index < E1K_NUM_OF_32BIT_REGS, VERR_DEV_IO_ERROR);
    pThis->auRegs[index] = (value & g_aE1kRegMap[index].writable)
                         | (pThis->auRegs[index] & ~g_aE1kRegMap[index].writable);

    return VINF_SUCCESS;
}

/**
 * Search register table for matching register.
 *
 * @returns Index in the register table or -1 if not found.
 *
 * @param   offReg      Register offset in memory-mapped region.
 * @thread  EMT
 */
static int e1kRegLookup(uint32_t offReg)
{

#if 0
    int index;

    for (index = 0; index < E1K_NUM_OF_REGS; index++)
    {
        if (g_aE1kRegMap[index].offset <= offReg && offReg < g_aE1kRegMap[index].offset + g_aE1kRegMap[index].size)
        {
            return index;
        }
    }
#else
    int iStart = 0;
    int iEnd   = E1K_NUM_OF_BINARY_SEARCHABLE;
    for (;;)
    {
        int i = (iEnd - iStart) / 2 + iStart;
        uint32_t offCur = g_aE1kRegMap[i].offset;
        if (offReg < offCur)
        {
            if (i == iStart)
                break;
            iEnd = i;
        }
        else if (offReg >= offCur + g_aE1kRegMap[i].size)
        {
            i++;
            if (i == iEnd)
                break;
            iStart = i;
        }
        else
            return i;
        Assert(iEnd > iStart);
    }

    for (unsigned i = E1K_NUM_OF_BINARY_SEARCHABLE; i < RT_ELEMENTS(g_aE1kRegMap); i++)
        if (offReg - g_aE1kRegMap[i].offset < g_aE1kRegMap[i].size)
            return (int)i;

# ifdef VBOX_STRICT
    for (unsigned i = 0; i < RT_ELEMENTS(g_aE1kRegMap); i++)
        Assert(offReg - g_aE1kRegMap[i].offset >= g_aE1kRegMap[i].size);
# endif

#endif

    return -1;
}

/**
 * Handle unaligned register read operation.
 *
 * Looks up and calls appropriate handler.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @param   offReg      Register offset in memory-mapped frame.
 * @param   pv          Where to store the result.
 * @param   cb          Number of bytes to read.
 * @thread  EMT
 * @remarks IOM takes care of unaligned and small reads via MMIO.  For I/O port
 *          accesses we have to take care of that ourselves.
 */
static int e1kRegReadUnaligned(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offReg, void *pv, uint32_t cb)
{
    uint32_t    u32    = 0;
    uint32_t    shift;
    int         rc     = VINF_SUCCESS;
    int         index  = e1kRegLookup(offReg);
#ifdef LOG_ENABLED
    char        buf[9];
#endif

    /*
     * From the spec:
     * For registers that should be accessed as 32-bit double words, partial writes (less than a 32-bit
     * double word) is ignored. Partial reads return all 32 bits of data regardless of the byte enables.
     */

    /*
     * To be able to read bytes and short word we convert them to properly
     * shifted 32-bit words and masks.  The idea is to keep register-specific
     * handlers simple. Most accesses will be 32-bit anyway.
     */
    uint32_t mask;
    switch (cb)
    {
        case 4: mask = 0xFFFFFFFF; break;
        case 2: mask = 0x0000FFFF; break;
        case 1: mask = 0x000000FF; break;
        default:
            return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "unsupported op size: offset=%#10x cb=%#10x\n", offReg, cb);
    }
    if (index >= 0)
    {
        RT_UNTRUSTED_VALIDATED_FENCE(); /* paranoia because of port I/O. */
        if (g_aE1kRegMap[index].readable)
        {
            /* Make the mask correspond to the bits we are about to read. */
            shift = (offReg - g_aE1kRegMap[index].offset) % sizeof(uint32_t) * 8;
            mask <<= shift;
            if (!mask)
                return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "Zero mask: offset=%#10x cb=%#10x\n", offReg, cb);
            /*
             * Read it. Pass the mask so the handler knows what has to be read.
             * Mask out irrelevant bits.
             */
            //e1kCsEnterReturn(pThis, VERR_SEM_BUSY);
            //pThis->fDelayInts = false;
            //pThis->iStatIntLost += pThis->iStatIntLostOne;
            //pThis->iStatIntLostOne = 0;
            rc = g_aE1kRegMap[index].pfnRead(pDevIns, pThis, offReg & 0xFFFFFFFC, (uint32_t)index, &u32);
            u32 &= mask;
            //e1kCsLeave(pThis);
            E1kLog2(("%s At %08X read  %s          from %s (%s)\n",
                    pThis->szPrf, offReg, e1kU32toHex(u32, mask, buf), g_aE1kRegMap[index].abbrev, g_aE1kRegMap[index].name));
            Log6(("%s At %08X read  %s          from %s (%s) [UNALIGNED]\n",
                  pThis->szPrf, offReg, e1kU32toHex(u32, mask, buf), g_aE1kRegMap[index].abbrev, g_aE1kRegMap[index].name));
            /* Shift back the result. */
            u32 >>= shift;
        }
        else
            E1kLog(("%s At %08X read (%s) attempt from write-only register %s (%s)\n",
                    pThis->szPrf, offReg, e1kU32toHex(u32, mask, buf), g_aE1kRegMap[index].abbrev, g_aE1kRegMap[index].name));
        if (IOM_SUCCESS(rc))
            STAM_COUNTER_INC(&pThis->aStatRegReads[index]);
    }
    else
        E1kLog(("%s At %08X read (%s) attempt from non-existing register\n",
                pThis->szPrf, offReg, e1kU32toHex(u32, mask, buf)));

    memcpy(pv, &u32, cb);
    return rc;
}

/**
 * Handle 4 byte aligned and sized read operation.
 *
 * Looks up and calls appropriate handler.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @param   offReg      Register offset in memory-mapped frame.
 * @param   pu32        Where to store the result.
 * @thread  EMT
 */
static VBOXSTRICTRC e1kRegReadAlignedU32(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offReg, uint32_t *pu32)
{
    Assert(!(offReg & 3));

    /*
     * Lookup the register and check that it's readable.
     */
    VBOXSTRICTRC rc     = VINF_SUCCESS;
    int          idxReg = e1kRegLookup(offReg);
    if (RT_LIKELY(idxReg >= 0))
    {
        RT_UNTRUSTED_VALIDATED_FENCE(); /* paranoia because of port I/O. */
        if (RT_UNLIKELY(g_aE1kRegMap[idxReg].readable))
        {
            /*
             * Read it. Pass the mask so the handler knows what has to be read.
             * Mask out irrelevant bits.
             */
            //e1kCsEnterReturn(pThis, VERR_SEM_BUSY);
            //pThis->fDelayInts = false;
            //pThis->iStatIntLost += pThis->iStatIntLostOne;
            //pThis->iStatIntLostOne = 0;
            rc = g_aE1kRegMap[idxReg].pfnRead(pDevIns, pThis, offReg & 0xFFFFFFFC, (uint32_t)idxReg, pu32);
            //e1kCsLeave(pThis);
            Log6(("%s At %08X read  %08X          from %s (%s)\n",
                  pThis->szPrf, offReg, *pu32, g_aE1kRegMap[idxReg].abbrev, g_aE1kRegMap[idxReg].name));
            if (IOM_SUCCESS(rc))
                STAM_COUNTER_INC(&pThis->aStatRegReads[idxReg]);
        }
        else
            E1kLog(("%s At %08X read attempt from non-readable register %s (%s)\n",
                    pThis->szPrf, offReg, g_aE1kRegMap[idxReg].abbrev, g_aE1kRegMap[idxReg].name));
    }
    else
        E1kLog(("%s At %08X read attempt from non-existing register\n", pThis->szPrf, offReg));
    return rc;
}

/**
 * Handle 4 byte sized and aligned register write operation.
 *
 * Looks up and calls appropriate handler.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state structure.
 * @param   offReg      Register offset in memory-mapped frame.
 * @param   u32Value    The value to write.
 * @thread  EMT
 */
static VBOXSTRICTRC e1kRegWriteAlignedU32(PPDMDEVINS pDevIns, PE1KSTATE pThis, uint32_t offReg, uint32_t u32Value)
{
    VBOXSTRICTRC rc    = VINF_SUCCESS;
    int          index = e1kRegLookup(offReg);
    if (RT_LIKELY(index >= 0))
    {
        RT_UNTRUSTED_VALIDATED_FENCE(); /* paranoia because of port I/O. */
        if (RT_LIKELY(g_aE1kRegMap[index].writable))
        {
            /*
             * Write it. Pass the mask so the handler knows what has to be written.
             * Mask out irrelevant bits.
             */
            Log6(("%s At %08X write          %08X  to  %s (%s)\n",
                     pThis->szPrf, offReg, u32Value, g_aE1kRegMap[index].abbrev, g_aE1kRegMap[index].name));
            //e1kCsEnterReturn(pThis, VERR_SEM_BUSY);
            //pThis->fDelayInts = false;
            //pThis->iStatIntLost += pThis->iStatIntLostOne;
            //pThis->iStatIntLostOne = 0;
            rc = g_aE1kRegMap[index].pfnWrite(pDevIns, pThis, offReg, (uint32_t)index, u32Value);
            //e1kCsLeave(pThis);
        }
        else
            E1kLog(("%s At %08X write attempt (%08X) to  read-only register %s (%s)\n",
                    pThis->szPrf, offReg, u32Value, g_aE1kRegMap[index].abbrev, g_aE1kRegMap[index].name));
        if (IOM_SUCCESS(rc))
            STAM_COUNTER_INC(&pThis->aStatRegWrites[index]);
    }
    else
        E1kLog(("%s At %08X write attempt (%08X) to  non-existing register\n",
                pThis->szPrf, offReg, u32Value));
    return rc;
}


/* -=-=-=-=- MMIO and I/O Port Callbacks -=-=-=-=- */

/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) e1kMMIORead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, uint32_t cb)
{
    RT_NOREF2(pvUser, cb);
    PE1KSTATE pThis  = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatMMIORead), a);

    Assert(off < E1K_MM_SIZE);
    Assert(cb == 4);
    Assert(!(off & 3));

    VBOXSTRICTRC rcStrict = e1kRegReadAlignedU32(pDevIns, pThis, (uint32_t)off, (uint32_t *)pv);

    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatMMIORead), a);
    return rcStrict;
}

/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) e1kMMIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, uint32_t cb)
{
    RT_NOREF2(pvUser, cb);
    PE1KSTATE pThis  = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatMMIOWrite), a);

    Assert(off < E1K_MM_SIZE);
    Assert(cb == 4);
    Assert(!(off & 3));

    VBOXSTRICTRC rcStrict = e1kRegWriteAlignedU32(pDevIns, pThis, (uint32_t)off, *(uint32_t const *)pv);

    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatMMIOWrite), a);
    return rcStrict;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC) e1kIOPortIn(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PE1KSTATE    pThis = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    VBOXSTRICTRC rc;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatIORead), a);
    RT_NOREF_PV(pvUser);

    if (RT_LIKELY(cb == 4))
        switch (offPort)
        {
            case 0x00: /* IOADDR */
                *pu32 = pThis->uSelectedReg;
                Log9(("%s e1kIOPortIn: IOADDR(0), selecting register %#010x, val=%#010x\n", pThis->szPrf, pThis->uSelectedReg, *pu32));
                rc = VINF_SUCCESS;
                break;

            case 0x04: /* IODATA */
                if (!(pThis->uSelectedReg & 3))
                    rc = e1kRegReadAlignedU32(pDevIns, pThis, pThis->uSelectedReg, pu32);
                else /** @todo r=bird: I wouldn't be surprised if this unaligned branch wasn't necessary. */
                    rc = e1kRegReadUnaligned(pDevIns, pThis, pThis->uSelectedReg, pu32, cb);
                if (rc == VINF_IOM_R3_MMIO_READ)
                    rc = VINF_IOM_R3_IOPORT_READ;
                Log9(("%s e1kIOPortIn: IODATA(4), reading from selected register %#010x, val=%#010x\n", pThis->szPrf, pThis->uSelectedReg, *pu32));
                break;

            default:
                E1kLog(("%s e1kIOPortIn: invalid port %#010x\n", pThis->szPrf, offPort));
                /** @todo r=bird: Check what real hardware returns here. */
                //rc = VERR_IOM_IOPORT_UNUSED; /* Why not? */
                rc = VINF_IOM_MMIO_UNUSED_00;  /* used to return VINF_SUCCESS and not touch *pu32, which amounted to this. */
                break;
        }
    else
    {
        E1kLog(("%s e1kIOPortIn: invalid op size: offPort=%RTiop cb=%08x", pThis->szPrf, offPort, cb));
        rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "%s e1kIOPortIn: invalid op size: offPort=%RTiop cb=%08x\n", pThis->szPrf, offPort, cb);
        *pu32 = 0; /** @todo r=bird: Check what real hardware returns here.  (Didn't used to set a value here, picked zero as that's what we'd end up in most cases.) */
    }
    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatIORead), a);
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC) e1kIOPortOut(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PE1KSTATE    pThis = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    VBOXSTRICTRC rc;
    STAM_PROFILE_ADV_START(&pThis->CTX_SUFF_Z(StatIOWrite), a);
    RT_NOREF_PV(pvUser);

    Log9(("%s e1kIOPortOut: offPort=%RTiop value=%08x\n", pThis->szPrf, offPort, u32));
    if (RT_LIKELY(cb == 4))
    {
        switch (offPort)
        {
            case 0x00: /* IOADDR */
                pThis->uSelectedReg = u32;
                Log9(("%s e1kIOPortOut: IOADDR(0), selected register %08x\n", pThis->szPrf, pThis->uSelectedReg));
                rc = VINF_SUCCESS;
                break;

            case 0x04: /* IODATA */
                Log9(("%s e1kIOPortOut: IODATA(4), writing to selected register %#010x, value=%#010x\n", pThis->szPrf, pThis->uSelectedReg, u32));
                if (RT_LIKELY(!(pThis->uSelectedReg & 3)))
                {
                    rc = e1kRegWriteAlignedU32(pDevIns, pThis, pThis->uSelectedReg, u32);
                    if (rc == VINF_IOM_R3_MMIO_WRITE)
                        rc = VINF_IOM_R3_IOPORT_WRITE;
                }
                else
                    rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                                           "Spec violation: misaligned offset: %#10x, ignored.\n", pThis->uSelectedReg);
                break;

            default:
                E1kLog(("%s e1kIOPortOut: invalid port %#010x\n", pThis->szPrf, offPort));
                rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "invalid port %#010x\n", offPort);
        }
    }
    else
    {
        E1kLog(("%s e1kIOPortOut: invalid op size: offPort=%RTiop cb=%08x\n", pThis->szPrf, offPort, cb));
        rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "%s: invalid op size: offPort=%RTiop cb=%#x\n", pThis->szPrf, offPort, cb);
    }

    STAM_PROFILE_ADV_STOP(&pThis->CTX_SUFF_Z(StatIOWrite), a);
    return rc;
}

#ifdef IN_RING3

/**
 * Dump complete device state to log.
 *
 * @param   pThis          Pointer to device state.
 */
static void e1kDumpState(PE1KSTATE pThis)
{
    RT_NOREF(pThis);
    for (int i = 0; i < E1K_NUM_OF_32BIT_REGS; ++i)
        E1kLog2(("%s: %8.8s = %08x\n", pThis->szPrf, g_aE1kRegMap[i].abbrev, pThis->auRegs[i]));
# ifdef E1K_INT_STATS
    LogRel(("%s: Interrupt attempts: %d\n", pThis->szPrf, pThis->uStatIntTry));
    LogRel(("%s: Interrupts raised : %d\n", pThis->szPrf, pThis->uStatInt));
    LogRel(("%s: Interrupts lowered: %d\n", pThis->szPrf, pThis->uStatIntLower));
    LogRel(("%s: ICR outside ISR   : %d\n", pThis->szPrf, pThis->uStatNoIntICR));
    LogRel(("%s: IMS raised ints   : %d\n", pThis->szPrf, pThis->uStatIntIMS));
    LogRel(("%s: Interrupts skipped: %d\n", pThis->szPrf, pThis->uStatIntSkip));
    LogRel(("%s: Masked interrupts : %d\n", pThis->szPrf, pThis->uStatIntMasked));
    LogRel(("%s: Early interrupts  : %d\n", pThis->szPrf, pThis->uStatIntEarly));
    LogRel(("%s: Late interrupts   : %d\n", pThis->szPrf, pThis->uStatIntLate));
    LogRel(("%s: Lost interrupts   : %d\n", pThis->szPrf, pThis->iStatIntLost));
    LogRel(("%s: Interrupts by RX  : %d\n", pThis->szPrf, pThis->uStatIntRx));
    LogRel(("%s: Interrupts by TX  : %d\n", pThis->szPrf, pThis->uStatIntTx));
    LogRel(("%s: Interrupts by ICS : %d\n", pThis->szPrf, pThis->uStatIntICS));
    LogRel(("%s: Interrupts by RDTR: %d\n", pThis->szPrf, pThis->uStatIntRDTR));
    LogRel(("%s: Interrupts by RDMT: %d\n", pThis->szPrf, pThis->uStatIntRXDMT0));
    LogRel(("%s: Interrupts by TXQE: %d\n", pThis->szPrf, pThis->uStatIntTXQE));
    LogRel(("%s: TX int delay asked: %d\n", pThis->szPrf, pThis->uStatTxIDE));
    LogRel(("%s: TX delayed:         %d\n", pThis->szPrf, pThis->uStatTxDelayed));
    LogRel(("%s: TX delay expired:   %d\n", pThis->szPrf, pThis->uStatTxDelayExp));
    LogRel(("%s: TX no report asked: %d\n", pThis->szPrf, pThis->uStatTxNoRS));
    LogRel(("%s: TX abs timer expd : %d\n", pThis->szPrf, pThis->uStatTAD));
    LogRel(("%s: TX int timer expd : %d\n", pThis->szPrf, pThis->uStatTID));
    LogRel(("%s: RX abs timer expd : %d\n", pThis->szPrf, pThis->uStatRAD));
    LogRel(("%s: RX int timer expd : %d\n", pThis->szPrf, pThis->uStatRID));
    LogRel(("%s: TX CTX descriptors: %d\n", pThis->szPrf, pThis->uStatDescCtx));
    LogRel(("%s: TX DAT descriptors: %d\n", pThis->szPrf, pThis->uStatDescDat));
    LogRel(("%s: TX LEG descriptors: %d\n", pThis->szPrf, pThis->uStatDescLeg));
    LogRel(("%s: Received frames   : %d\n", pThis->szPrf, pThis->uStatRxFrm));
    LogRel(("%s: Transmitted frames: %d\n", pThis->szPrf, pThis->uStatTxFrm));
    LogRel(("%s: TX frames up to 1514: %d\n", pThis->szPrf, pThis->uStatTx1514));
    LogRel(("%s: TX frames up to 2962: %d\n", pThis->szPrf, pThis->uStatTx2962));
    LogRel(("%s: TX frames up to 4410: %d\n", pThis->szPrf, pThis->uStatTx4410));
    LogRel(("%s: TX frames up to 5858: %d\n", pThis->szPrf, pThis->uStatTx5858));
    LogRel(("%s: TX frames up to 7306: %d\n", pThis->szPrf, pThis->uStatTx7306));
    LogRel(("%s: TX frames up to 8754: %d\n", pThis->szPrf, pThis->uStatTx8754));
    LogRel(("%s: TX frames up to 16384: %d\n", pThis->szPrf, pThis->uStatTx16384));
    LogRel(("%s: TX frames up to 32768: %d\n", pThis->szPrf, pThis->uStatTx32768));
    LogRel(("%s: Larger TX frames    : %d\n", pThis->szPrf, pThis->uStatTxLarge));
    LogRel(("%s: Max TX Delay        : %lld\n", pThis->szPrf, pThis->uStatMaxTxDelay));
# endif /* E1K_INT_STATS */
}


/* -=-=-=-=- PDMINETWORKDOWN -=-=-=-=- */

/**
 * Check if the device can receive data now.
 * This must be called before the pfnRecieve() method is called.
 *
 * @returns VBox status code.
 * @retval  VERR_NET_NO_BUFFER_SPACE if we cannot receive.
 * @param   pDevIns     The device instance.
 * @param   pThis       The instance data.
 * @thread  EMT
 */
static int e1kR3CanReceive(PPDMDEVINS pDevIns, PE1KSTATE pThis)
{
# ifndef E1K_WITH_RXD_CACHE
    size_t cb;

    e1kCsRxEnterReturn(pThis);

    if (RT_UNLIKELY(RDLEN == sizeof(E1KRXDESC)))
    {
        E1KRXDESC desc;
        PDMDevHlpPCIPhysRead(pDevIns, e1kDescAddr(RDBAH, RDBAL, RDH), &desc, sizeof(desc));
        if (desc.status.fDD)
            cb = 0;
        else
            cb = pThis->u16RxBSize;
    }
    else if (RDH < RDT)
        cb = (RDT - RDH) * pThis->u16RxBSize;
    else if (RDH > RDT)
        cb = (RDLEN / sizeof(E1KRXDESC) - RDH + RDT) * pThis->u16RxBSize;
    else
    {
        cb = 0;
        E1kLogRel(("E1000: OUT of RX descriptors!\n"));
    }
    E1kLog2(("%s e1kR3CanReceive: at exit RDH=%d RDT=%d RDLEN=%d u16RxBSize=%d cb=%lu\n",
             pThis->szPrf, RDH, RDT, RDLEN, pThis->u16RxBSize, cb));

    e1kCsRxLeave(pThis);
    return cb > 0 ? VINF_SUCCESS : VERR_NET_NO_BUFFER_SPACE;
# else /* E1K_WITH_RXD_CACHE */

    e1kCsRxEnterReturn(pThis);

    E1KRXDC rxdc;
    if (RT_UNLIKELY(!e1kUpdateRxDContext(pDevIns, pThis, &rxdc, "e1kR3CanReceive")))
    {
        e1kCsRxLeave(pThis);
        E1kLog(("%s e1kR3CanReceive: failed to update Rx context, returning VERR_NET_NO_BUFFER_SPACE\n", pThis->szPrf));
        return VERR_NET_NO_BUFFER_SPACE;
    }

    int rc = VINF_SUCCESS;
    if (RT_UNLIKELY(rxdc.rdlen == sizeof(E1KRXDESC)))
    {
        E1KRXDESC desc;
        PDMDevHlpPCIPhysRead(pDevIns, e1kDescAddr(RDBAH, RDBAL, rxdc.rdh), &desc, sizeof(desc));
        if (desc.status.fDD)
            rc = VERR_NET_NO_BUFFER_SPACE;
    }
    else if (e1kRxDIsCacheEmpty(pThis) && rxdc.rdh == rxdc.rdt)
    {
        /* Cache is empty, so is the RX ring. */
        rc = VERR_NET_NO_BUFFER_SPACE;
    }
    E1kLog2(("%s e1kR3CanReceive: at exit in_cache=%d RDH=%d RDT=%d RDLEN=%d u16RxBSize=%d rc=%Rrc\n", pThis->szPrf,
             e1kRxDInCache(pThis), rxdc.rdh, rxdc.rdt, rxdc.rdlen, pThis->u16RxBSize, rc));

    e1kCsRxLeave(pThis);
    return rc;
# endif /* E1K_WITH_RXD_CACHE */
}

/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnWaitReceiveAvail}
 */
static DECLCALLBACK(int) e1kR3NetworkDown_WaitReceiveAvail(PPDMINETWORKDOWN pInterface, RTMSINTERVAL cMillies)
{
    PE1KSTATECC pThisCC = RT_FROM_MEMBER(pInterface, E1KSTATECC, INetworkDown);
    PE1KSTATE   pThis   = pThisCC->pShared;
    PPDMDEVINS  pDevIns = pThisCC->pDevInsR3;

    int rc = e1kR3CanReceive(pDevIns, pThis);
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;

    if (RT_UNLIKELY(cMillies == 0))
        return VERR_NET_NO_BUFFER_SPACE;

    rc = VERR_INTERRUPTED;
    ASMAtomicXchgBool(&pThis->fMaybeOutOfSpace, true);
    STAM_PROFILE_START(&pThis->StatRxOverflow, a);
    VMSTATE enmVMState;
    while (RT_LIKELY(   (enmVMState = PDMDevHlpVMState(pDevIns)) == VMSTATE_RUNNING
                     ||  enmVMState == VMSTATE_RUNNING_LS))
    {
        int rc2 = e1kR3CanReceive(pDevIns, pThis);
        if (RT_SUCCESS(rc2))
        {
            rc = VINF_SUCCESS;
            break;
        }
        E1kLogRel(("E1000: e1kR3NetworkDown_WaitReceiveAvail: waiting cMillies=%u...\n", cMillies));
        E1kLog(("%s: e1kR3NetworkDown_WaitReceiveAvail: waiting cMillies=%u...\n", pThis->szPrf, cMillies));
        PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->hEventMoreRxDescAvail, cMillies);
    }
    STAM_PROFILE_STOP(&pThis->StatRxOverflow, a);
    ASMAtomicXchgBool(&pThis->fMaybeOutOfSpace, false);

    return rc;
}


/**
 * Matches the packet addresses against Receive Address table. Looks for
 * exact matches only.
 *
 * @returns true if address matches.
 * @param   pThis          Pointer to the state structure.
 * @param   pvBuf           The ethernet packet.
 * @param   cb              Number of bytes available in the packet.
 * @thread  EMT
 */
static bool e1kPerfectMatch(PE1KSTATE pThis, const void *pvBuf)
{
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aRecAddr.array); i++)
    {
        E1KRAELEM* ra = pThis->aRecAddr.array + i;

        /* Valid address? */
        if (ra->ctl & RA_CTL_AV)
        {
            Assert((ra->ctl & RA_CTL_AS) < 2);
            //unsigned char *pAddr = (unsigned char*)pvBuf + sizeof(ra->addr)*(ra->ctl & RA_CTL_AS);
            //E1kLog3(("%s Matching %02x:%02x:%02x:%02x:%02x:%02x against %02x:%02x:%02x:%02x:%02x:%02x...\n",
            //         pThis->szPrf, pAddr[0], pAddr[1], pAddr[2], pAddr[3], pAddr[4], pAddr[5],
            //         ra->addr[0], ra->addr[1], ra->addr[2], ra->addr[3], ra->addr[4], ra->addr[5]));
            /*
             * Address Select:
             * 00b = Destination address
             * 01b = Source address
             * 10b = Reserved
             * 11b = Reserved
             * Since ethernet header is (DA, SA, len) we can use address
             * select as index.
             */
            if (memcmp((char*)pvBuf + sizeof(ra->addr)*(ra->ctl & RA_CTL_AS),
                ra->addr, sizeof(ra->addr)) == 0)
                return true;
        }
    }

    return false;
}

/**
 * Matches the packet addresses against Multicast Table Array.
 *
 * @remarks This is imperfect match since it matches not exact address but
 *          a subset of addresses.
 *
 * @returns true if address matches.
 * @param   pThis          Pointer to the state structure.
 * @param   pvBuf           The ethernet packet.
 * @param   cb              Number of bytes available in the packet.
 * @thread  EMT
 */
static bool e1kImperfectMatch(PE1KSTATE pThis, const void *pvBuf)
{
    /* Get bits 32..47 of destination address */
    uint16_t u16Bit = ((uint16_t*)pvBuf)[2];

    unsigned offset = GET_BITS(RCTL, MO);
    /*
     * offset means:
     * 00b = bits 36..47
     * 01b = bits 35..46
     * 10b = bits 34..45
     * 11b = bits 32..43
     */
    if (offset < 3)
        u16Bit = u16Bit >> (4 - offset);
    return ASMBitTest(pThis->auMTA, u16Bit & 0xFFF);
}

/**
 * Determines if the packet is to be delivered to upper layer.
 *
 * The following filters supported:
 * - Exact Unicast/Multicast
 * - Promiscuous Unicast/Multicast
 * - Multicast
 * - VLAN
 *
 * @returns true if packet is intended for this node.
 * @param   pThis          Pointer to the state structure.
 * @param   pvBuf           The ethernet packet.
 * @param   cb              Number of bytes available in the packet.
 * @param   pStatus         Bit field to store status bits.
 * @thread  EMT
 */
static bool e1kAddressFilter(PE1KSTATE pThis, const void *pvBuf, size_t cb, E1KRXDST *pStatus)
{
    Assert(cb > 14);
    /* Assume that we fail to pass exact filter. */
    pStatus->fPIF = false;
    pStatus->fVP  = false;
    /* Discard oversized packets */
    if (cb > E1K_MAX_RX_PKT_SIZE)
    {
        E1kLog(("%s ERROR: Incoming packet is too big, cb=%d > max=%d\n",
                pThis->szPrf, cb, E1K_MAX_RX_PKT_SIZE));
        E1K_INC_CNT32(ROC);
        return false;
    }
    else if (!(RCTL & RCTL_LPE) && cb > 1522)
    {
        /* When long packet reception is disabled packets over 1522 are discarded */
        E1kLog(("%s Discarding incoming packet (LPE=0), cb=%d\n",
                pThis->szPrf, cb));
        E1K_INC_CNT32(ROC);
        return false;
    }

    uint16_t *u16Ptr = (uint16_t*)pvBuf;
    /* Compare TPID with VLAN Ether Type */
    if (RT_BE2H_U16(u16Ptr[6]) == VET)
    {
        pStatus->fVP = true;
        /* Is VLAN filtering enabled? */
        if (RCTL & RCTL_VFE)
        {
            /* It is 802.1q packet indeed, let's filter by VID */
            if (RCTL & RCTL_CFIEN)
            {
                E1kLog3(("%s VLAN filter: VLAN=%d CFI=%d RCTL_CFI=%d\n", pThis->szPrf,
                         E1K_SPEC_VLAN(RT_BE2H_U16(u16Ptr[7])),
                         E1K_SPEC_CFI(RT_BE2H_U16(u16Ptr[7])),
                         !!(RCTL & RCTL_CFI)));
                if (E1K_SPEC_CFI(RT_BE2H_U16(u16Ptr[7])) != !!(RCTL & RCTL_CFI))
                {
                    E1kLog2(("%s Packet filter: CFIs do not match in packet and RCTL (%d!=%d)\n",
                             pThis->szPrf, E1K_SPEC_CFI(RT_BE2H_U16(u16Ptr[7])), !!(RCTL & RCTL_CFI)));
                    return false;
                }
            }
            else
                E1kLog3(("%s VLAN filter: VLAN=%d\n", pThis->szPrf,
                         E1K_SPEC_VLAN(RT_BE2H_U16(u16Ptr[7]))));
            if (!ASMBitTest(pThis->auVFTA, E1K_SPEC_VLAN(RT_BE2H_U16(u16Ptr[7]))))
            {
                E1kLog2(("%s Packet filter: no VLAN match (id=%d)\n",
                         pThis->szPrf, E1K_SPEC_VLAN(RT_BE2H_U16(u16Ptr[7]))));
                return false;
            }
        }
    }
    /* Broadcast filtering */
    if (e1kIsBroadcast(pvBuf) && (RCTL & RCTL_BAM))
        return true;
    E1kLog2(("%s Packet filter: not a broadcast\n", pThis->szPrf));
    if (e1kIsMulticast(pvBuf))
    {
        /* Is multicast promiscuous enabled? */
        if (RCTL & RCTL_MPE)
            return true;
        E1kLog2(("%s Packet filter: no promiscuous multicast\n", pThis->szPrf));
        /* Try perfect matches first */
        if (e1kPerfectMatch(pThis, pvBuf))
        {
            pStatus->fPIF = true;
            return true;
        }
        E1kLog2(("%s Packet filter: no perfect match\n", pThis->szPrf));
        if (e1kImperfectMatch(pThis, pvBuf))
            return true;
        E1kLog2(("%s Packet filter: no imperfect match\n", pThis->szPrf));
    }
    else {
        /* Is unicast promiscuous enabled? */
        if (RCTL & RCTL_UPE)
            return true;
        E1kLog2(("%s Packet filter: no promiscuous unicast\n", pThis->szPrf));
        if (e1kPerfectMatch(pThis, pvBuf))
        {
            pStatus->fPIF = true;
            return true;
        }
        E1kLog2(("%s Packet filter: no perfect match\n", pThis->szPrf));
    }
    E1kLog2(("%s Packet filter: packet discarded\n", pThis->szPrf));
    return false;
}

/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnReceive}
 */
static DECLCALLBACK(int) e1kR3NetworkDown_Receive(PPDMINETWORKDOWN pInterface, const void *pvBuf, size_t cb)
{
    PE1KSTATECC pThisCC = RT_FROM_MEMBER(pInterface, E1KSTATECC, INetworkDown);
    PE1KSTATE   pThis   = pThisCC->pShared;
    PPDMDEVINS  pDevIns = pThisCC->pDevInsR3;
    int         rc      = VINF_SUCCESS;

    /*
     * Drop packets if the VM is not running yet/anymore.
     */
    VMSTATE enmVMState = PDMDevHlpVMState(pDevIns);
    if (    enmVMState != VMSTATE_RUNNING
        &&  enmVMState != VMSTATE_RUNNING_LS)
    {
        E1kLog(("%s Dropping incoming packet as VM is not running.\n", pThis->szPrf));
        return VINF_SUCCESS;
    }

    /* Discard incoming packets in locked state */
    if (!(RCTL & RCTL_EN) || pThis->fLocked || !(STATUS & STATUS_LU))
    {
        E1kLog(("%s Dropping incoming packet as receive operation is disabled.\n", pThis->szPrf));
        return VINF_SUCCESS;
    }

    STAM_PROFILE_ADV_START(&pThis->StatReceive, a);

    //e1kR3CsEnterAsserted(pThis);

    e1kPacketDump(pDevIns, pThis, (const uint8_t*)pvBuf, cb, "<-- Incoming");

    /* Update stats */
    e1kR3CsEnterAsserted(pThis);
    E1K_INC_CNT32(TPR);
    E1K_ADD_CNT64(TORL, TORH, cb < 64? 64 : cb);
    e1kCsLeave(pThis);

    STAM_PROFILE_ADV_START(&pThis->StatReceiveFilter, a);
    E1KRXDST status;
    RT_ZERO(status);
    bool fPassed = e1kAddressFilter(pThis, pvBuf, cb, &status);
    STAM_PROFILE_ADV_STOP(&pThis->StatReceiveFilter, a);
    if (fPassed)
    {
        rc = e1kHandleRxPacket(pDevIns, pThis, pvBuf, cb, status);
    }
    //e1kCsLeave(pThis);
    STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);

    return rc;
}


/* -=-=-=-=- PDMILEDPORTS -=-=-=-=- */

/**
 * @interface_method_impl{PDMILEDPORTS,pfnQueryStatusLed}
 */
static DECLCALLBACK(int) e1kR3QueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    if (iLUN == 0)
    {
        PE1KSTATECC pThisCC = RT_FROM_MEMBER(pInterface, E1KSTATECC, ILeds);
        *ppLed = &pThisCC->pShared->led;
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}


/* -=-=-=-=- PDMINETWORKCONFIG -=-=-=-=- */

/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnGetMac}
 */
static DECLCALLBACK(int) e1kR3GetMac(PPDMINETWORKCONFIG pInterface, PRTMAC pMac)
{
    PE1KSTATECC pThisCC = RT_FROM_MEMBER(pInterface, E1KSTATECC, INetworkConfig);
    pThisCC->eeprom.getMac(pMac);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnGetLinkState}
 */
static DECLCALLBACK(PDMNETWORKLINKSTATE) e1kR3GetLinkState(PPDMINETWORKCONFIG pInterface)
{
    PE1KSTATECC pThisCC = RT_FROM_MEMBER(pInterface, E1KSTATECC, INetworkConfig);
    PE1KSTATE   pThis   = pThisCC->pShared;
    if (STATUS & STATUS_LU)
        return PDMNETWORKLINKSTATE_UP;
    return PDMNETWORKLINKSTATE_DOWN;
}

/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnSetLinkState}
 */
static DECLCALLBACK(int) e1kR3SetLinkState(PPDMINETWORKCONFIG pInterface, PDMNETWORKLINKSTATE enmState)
{
    PE1KSTATECC pThisCC = RT_FROM_MEMBER(pInterface, E1KSTATECC, INetworkConfig);
    PE1KSTATE   pThis   = pThisCC->pShared;
    PPDMDEVINS  pDevIns = pThisCC->pDevInsR3;

    E1kLog(("%s e1kR3SetLinkState: enmState=%d\n", pThis->szPrf, enmState));
    switch (enmState)
    {
        case PDMNETWORKLINKSTATE_UP:
            pThis->fCableConnected = true;
            /* If link was down, bring it up after a while. */
            if (!(STATUS & STATUS_LU))
                e1kBringLinkUpDelayed(pDevIns, pThis);
            break;
        case PDMNETWORKLINKSTATE_DOWN:
            pThis->fCableConnected = false;
            /* Always set the phy link state to down, regardless of the STATUS_LU bit.
             * We might have to set the link state before the driver initializes us. */
            Phy::setLinkStatus(&pThis->phy, false);
            /* If link was up, bring it down. */
            if (STATUS & STATUS_LU)
                e1kR3LinkDown(pDevIns, pThis, pThisCC);
            break;
        case PDMNETWORKLINKSTATE_DOWN_RESUME:
            /*
             * There is not much sense in bringing down the link if it has not come up yet.
             * If it is up though, we bring it down temporarely, then bring it up again.
             */
            if (STATUS & STATUS_LU)
                e1kR3LinkDownTemp(pDevIns, pThis, pThisCC);
            break;
        default:
            ;
    }
    return VINF_SUCCESS;
}


/* -=-=-=-=- PDMIBASE -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) e1kR3QueryInterface(struct PDMIBASE *pInterface, const char *pszIID)
{
    PE1KSTATECC pThisCC = RT_FROM_MEMBER(pInterface, E1KSTATECC, IBase);
    Assert(&pThisCC->IBase == pInterface);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE,          &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKDOWN,   &pThisCC->INetworkDown);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKCONFIG, &pThisCC->INetworkConfig);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS,      &pThisCC->ILeds);
    return NULL;
}


/* -=-=-=-=- Saved State -=-=-=-=- */

/**
 * Saves the configuration.
 *
 * @param   pThis      The E1K state.
 * @param   pSSM        The handle to the saved state.
 */
static void e1kR3SaveConfig(PCPDMDEVHLPR3 pHlp, PE1KSTATE pThis, PSSMHANDLE pSSM)
{
    pHlp->pfnSSMPutMem(pSSM, &pThis->macConfigured, sizeof(pThis->macConfigured));
    pHlp->pfnSSMPutU32(pSSM, pThis->eChip);
}

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC,Save basic configuration.}
 */
static DECLCALLBACK(int) e1kR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    RT_NOREF(uPass);
    e1kR3SaveConfig(pDevIns->pHlpR3, PDMDEVINS_2_DATA(pDevIns, PE1KSTATE), pSSM);
    return VINF_SSM_DONT_CALL_AGAIN;
}

/**
 * @callback_method_impl{FNSSMDEVSAVEPREP,Synchronize.}
 */
static DECLCALLBACK(int) e1kR3SavePrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    PE1KSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);

    e1kCsEnterReturn(pThis, VERR_SEM_BUSY);
    e1kCsLeave(pThis);
    return VINF_SUCCESS;
#if 0
    /* 1) Prevent all threads from modifying the state and memory */
    //pThis->fLocked = true;
    /* 2) Cancel all timers */
#ifdef E1K_TX_DELAY
    e1kCancelTimer(pThis, pThis->CTX_SUFF(pTXDTimer));
#endif /* E1K_TX_DELAY */
//#ifdef E1K_USE_TX_TIMERS
    if (pThis->fTidEnabled)
    {
        e1kCancelTimer(pThis, pThis->CTX_SUFF(pTIDTimer));
#ifndef E1K_NO_TAD
        e1kCancelTimer(pThis, pThis->CTX_SUFF(pTADTimer));
#endif /* E1K_NO_TAD */
    }
//#endif /* E1K_USE_TX_TIMERS */
#ifdef E1K_USE_RX_TIMERS
    e1kCancelTimer(pThis, pThis->CTX_SUFF(pRIDTimer));
    e1kCancelTimer(pThis, pThis->CTX_SUFF(pRADTimer));
#endif /* E1K_USE_RX_TIMERS */
    e1kCancelTimer(pThis, pThis->CTX_SUFF(pIntTimer));
    /* 3) Did I forget anything? */
    E1kLog(("%s Locked\n", pThis->szPrf));
    return VINF_SUCCESS;
#endif
}

/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) e1kR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PE1KSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    PE1KSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;

    e1kR3SaveConfig(pHlp, pThis, pSSM);
    pThisCC->eeprom.save(pHlp, pSSM);
    e1kDumpState(pThis);
    pHlp->pfnSSMPutMem(pSSM, pThis->auRegs, sizeof(pThis->auRegs));
    pHlp->pfnSSMPutBool(pSSM, pThis->fIntRaised);
    Phy::saveState(pHlp, pSSM, &pThis->phy);
    pHlp->pfnSSMPutU32(pSSM, pThis->uSelectedReg);
    pHlp->pfnSSMPutMem(pSSM, pThis->auMTA, sizeof(pThis->auMTA));
    pHlp->pfnSSMPutMem(pSSM, &pThis->aRecAddr, sizeof(pThis->aRecAddr));
    pHlp->pfnSSMPutMem(pSSM, pThis->auVFTA, sizeof(pThis->auVFTA));
    pHlp->pfnSSMPutU64(pSSM, pThis->u64AckedAt);
    pHlp->pfnSSMPutU16(pSSM, pThis->u16RxBSize);
    //pHlp->pfnSSMPutBool(pSSM, pThis->fDelayInts);
    //pHlp->pfnSSMPutBool(pSSM, pThis->fIntMaskUsed);
    pHlp->pfnSSMPutU16(pSSM, pThis->u16TxPktLen);
/** @todo State wrt to the TSE buffer is incomplete, so little point in
 *        saving this actually. */
    pHlp->pfnSSMPutMem(pSSM, pThis->aTxPacketFallback, pThis->u16TxPktLen);
    pHlp->pfnSSMPutBool(pSSM, pThis->fIPcsum);
    pHlp->pfnSSMPutBool(pSSM, pThis->fTCPcsum);
    pHlp->pfnSSMPutMem(pSSM, &pThis->contextTSE, sizeof(pThis->contextTSE));
    pHlp->pfnSSMPutMem(pSSM, &pThis->contextNormal, sizeof(pThis->contextNormal));
    pHlp->pfnSSMPutBool(pSSM, pThis->fVTag);
    pHlp->pfnSSMPutU16(pSSM, pThis->u16VTagTCI);
#ifdef E1K_WITH_TXD_CACHE
# if 0
    pHlp->pfnSSMPutU8(pSSM, pThis->nTxDFetched);
    pHlp->pfnSSMPutMem(pSSM, pThis->aTxDescriptors,
                         pThis->nTxDFetched * sizeof(pThis->aTxDescriptors[0]));
# else
    /*
     * There is no point in storing TX descriptor cache entries as we can simply
     * fetch them again. Moreover, normally the cache is always empty when we
     * save the state. Store zero entries for compatibility.
     */
    pHlp->pfnSSMPutU8(pSSM, 0);
# endif
#endif /* E1K_WITH_TXD_CACHE */
/** @todo GSO requires some more state here. */
    E1kLog(("%s State has been saved\n", pThis->szPrf));
    return VINF_SUCCESS;
}

#if 0
/**
 * @callback_method_impl{FNSSMDEVSAVEDONE}
 */
static DECLCALLBACK(int) e1kSaveDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PE1KSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);

    /* If VM is being powered off unlocking will result in assertions in PGM */
    if (PDMDevHlpGetVM(pDevIns)->enmVMState == VMSTATE_RUNNING)
        pThis->fLocked = false;
    else
        E1kLog(("%s VM is not running -- remain locked\n", pThis->szPrf));
    E1kLog(("%s Unlocked\n", pThis->szPrf));
    return VINF_SUCCESS;
}
#endif

/**
 * @callback_method_impl{FNSSMDEVLOADPREP,Synchronize.}
 */
static DECLCALLBACK(int) e1kR3LoadPrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    PE1KSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);

    e1kCsEnterReturn(pThis, VERR_SEM_BUSY);
    e1kCsLeave(pThis);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) e1kR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PE1KSTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    PE1KSTATECC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;

    if (    uVersion != E1K_SAVEDSTATE_VERSION
#ifdef E1K_WITH_TXD_CACHE
        &&  uVersion != E1K_SAVEDSTATE_VERSION_VBOX_42_VTAG
#endif /* E1K_WITH_TXD_CACHE */
        &&  uVersion != E1K_SAVEDSTATE_VERSION_VBOX_41
        &&  uVersion != E1K_SAVEDSTATE_VERSION_VBOX_30)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    if (   uVersion > E1K_SAVEDSTATE_VERSION_VBOX_30
        || uPass    != SSM_PASS_FINAL)
    {
        /* config checks */
        RTMAC macConfigured;
        rc = pHlp->pfnSSMGetMem(pSSM, &macConfigured, sizeof(macConfigured));
        AssertRCReturn(rc, rc);
        if (   memcmp(&macConfigured, &pThis->macConfigured, sizeof(macConfigured))
            && (uPass == 0 || !PDMDevHlpVMTeleportedAndNotFullyResumedYet(pDevIns)) )
            LogRel(("%s: The mac address differs: config=%RTmac saved=%RTmac\n", pThis->szPrf, &pThis->macConfigured, &macConfigured));

        E1KCHIP eChip;
        rc = pHlp->pfnSSMGetU32(pSSM, &eChip);
        AssertRCReturn(rc, rc);
        if (eChip != pThis->eChip)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("The chip type differs: config=%u saved=%u"), pThis->eChip, eChip);
    }

    if (uPass == SSM_PASS_FINAL)
    {
        if (uVersion > E1K_SAVEDSTATE_VERSION_VBOX_30)
        {
            rc = pThisCC->eeprom.load(pHlp, pSSM);
            AssertRCReturn(rc, rc);
        }
        /* the state */
        pHlp->pfnSSMGetMem(pSSM, &pThis->auRegs, sizeof(pThis->auRegs));
        pHlp->pfnSSMGetBool(pSSM, &pThis->fIntRaised);
        /** @todo PHY could be made a separate device with its own versioning */
        Phy::loadState(pHlp, pSSM, &pThis->phy);
        pHlp->pfnSSMGetU32(pSSM, &pThis->uSelectedReg);
        pHlp->pfnSSMGetMem(pSSM, &pThis->auMTA, sizeof(pThis->auMTA));
        pHlp->pfnSSMGetMem(pSSM, &pThis->aRecAddr, sizeof(pThis->aRecAddr));
        pHlp->pfnSSMGetMem(pSSM, &pThis->auVFTA, sizeof(pThis->auVFTA));
        pHlp->pfnSSMGetU64(pSSM, &pThis->u64AckedAt);
        pHlp->pfnSSMGetU16(pSSM, &pThis->u16RxBSize);
        //pHlp->pfnSSMGetBool(pSSM, pThis->fDelayInts);
        //pHlp->pfnSSMGetBool(pSSM, pThis->fIntMaskUsed);
        rc = pHlp->pfnSSMGetU16(pSSM, &pThis->u16TxPktLen);
        AssertRCReturn(rc, rc);
        if (pThis->u16TxPktLen > sizeof(pThis->aTxPacketFallback))
            pThis->u16TxPktLen = sizeof(pThis->aTxPacketFallback);
        pHlp->pfnSSMGetMem(pSSM, &pThis->aTxPacketFallback[0], pThis->u16TxPktLen);
        pHlp->pfnSSMGetBool(pSSM, &pThis->fIPcsum);
        pHlp->pfnSSMGetBool(pSSM, &pThis->fTCPcsum);
        pHlp->pfnSSMGetMem(pSSM, &pThis->contextTSE, sizeof(pThis->contextTSE));
        rc = pHlp->pfnSSMGetMem(pSSM, &pThis->contextNormal, sizeof(pThis->contextNormal));
        AssertRCReturn(rc, rc);
        if (uVersion > E1K_SAVEDSTATE_VERSION_VBOX_41)
        {
            pHlp->pfnSSMGetBool(pSSM, &pThis->fVTag);
            rc = pHlp->pfnSSMGetU16(pSSM, &pThis->u16VTagTCI);
            AssertRCReturn(rc, rc);
        }
        else
        {
            pThis->fVTag      = false;
            pThis->u16VTagTCI = 0;
        }
#ifdef E1K_WITH_TXD_CACHE
        if (uVersion > E1K_SAVEDSTATE_VERSION_VBOX_42_VTAG)
        {
            rc = pHlp->pfnSSMGetU8(pSSM, &pThis->nTxDFetched);
            AssertRCReturn(rc, rc);
            if (pThis->nTxDFetched)
                pHlp->pfnSSMGetMem(pSSM, pThis->aTxDescriptors,
                                     pThis->nTxDFetched * sizeof(pThis->aTxDescriptors[0]));
        }
        else
            pThis->nTxDFetched = 0;
        /**
         * @todo Perhaps we should not store TXD cache as the entries can be
         * simply fetched again from guest's memory. Or can't they?
         */
#endif /* E1K_WITH_TXD_CACHE */
#ifdef E1K_WITH_RXD_CACHE
        /*
         * There is no point in storing the RX descriptor cache in the saved
         * state, we just need to make sure it is empty.
         */
        pThis->iRxDCurrent = pThis->nRxDFetched = 0;
#endif /* E1K_WITH_RXD_CACHE */
        rc = pHlp->pfnSSMHandleGetStatus(pSSM);
        AssertRCReturn(rc, rc);

        /* derived state  */
        e1kSetupGsoCtx(&pThis->GsoCtx, &pThis->contextTSE);

        E1kLog(("%s State has been restored\n", pThis->szPrf));
        e1kDumpState(pThis);
    }
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMDEVLOADDONE, Link status adjustments after loading.}
 */
static DECLCALLBACK(int) e1kR3LoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PE1KSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
    RT_NOREF(pSSM);

    /* Update promiscuous mode */
    if (pThisCC->pDrvR3)
        pThisCC->pDrvR3->pfnSetPromiscuousMode(pThisCC->pDrvR3, !!(RCTL & (RCTL_UPE | RCTL_MPE)));

    /*
     * Force the link down here, since PDMNETWORKLINKSTATE_DOWN_RESUME is never
     * passed to us. We go through all this stuff if the link was up and we
     * wasn't teleported.
     */
    if (    (STATUS & STATUS_LU)
        && !PDMDevHlpVMTeleportedAndNotFullyResumedYet(pDevIns)
        && pThis->cMsLinkUpDelay)
    {
        e1kR3LinkDownTemp(pDevIns, pThis, pThisCC);
    }
    return VINF_SUCCESS;
}



/* -=-=-=-=- Debug Info + Log Types -=-=-=-=- */

/**
 * @callback_method_impl{FNRTSTRFORMATTYPE}
 */
static DECLCALLBACK(size_t) e1kR3FmtRxDesc(PFNRTSTROUTPUT pfnOutput,
                                           void *pvArgOutput,
                                           const char *pszType,
                                           void const *pvValue,
                                           int cchWidth,
                                           int cchPrecision,
                                           unsigned fFlags,
                                           void *pvUser)
{
    RT_NOREF(cchWidth,  cchPrecision,  fFlags, pvUser);
    AssertReturn(strcmp(pszType, "e1krxd") == 0, 0);
    E1KRXDESC* pDesc = (E1KRXDESC*)pvValue;
    if (!pDesc)
        return RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "NULL_RXD");

    size_t cbPrintf = 0;
    cbPrintf += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "Address=%16LX Length=%04X Csum=%04X\n",
                            pDesc->u64BufAddr, pDesc->u16Length, pDesc->u16Checksum);
    cbPrintf += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "        STA: %s %s %s %s %s %s %s ERR: %s %s %s %s SPECIAL: %s VLAN=%03x PRI=%x",
                            pDesc->status.fPIF ? "PIF" : "pif",
                            pDesc->status.fIPCS ? "IPCS" : "ipcs",
                            pDesc->status.fTCPCS ? "TCPCS" : "tcpcs",
                            pDesc->status.fVP ? "VP" : "vp",
                            pDesc->status.fIXSM ? "IXSM" : "ixsm",
                            pDesc->status.fEOP ? "EOP" : "eop",
                            pDesc->status.fDD ? "DD" : "dd",
                            pDesc->status.fRXE ? "RXE" : "rxe",
                            pDesc->status.fIPE ? "IPE" : "ipe",
                            pDesc->status.fTCPE ? "TCPE" : "tcpe",
                            pDesc->status.fCE ? "CE" : "ce",
                            E1K_SPEC_CFI(pDesc->status.u16Special) ? "CFI" :"cfi",
                            E1K_SPEC_VLAN(pDesc->status.u16Special),
                            E1K_SPEC_PRI(pDesc->status.u16Special));
    return cbPrintf;
}

/**
 * @callback_method_impl{FNRTSTRFORMATTYPE}
 */
static DECLCALLBACK(size_t) e1kR3FmtTxDesc(PFNRTSTROUTPUT pfnOutput,
                                           void *pvArgOutput,
                                           const char *pszType,
                                           void const *pvValue,
                                           int cchWidth,
                                           int cchPrecision,
                                           unsigned fFlags,
                                           void *pvUser)
{
    RT_NOREF(cchWidth, cchPrecision, fFlags, pvUser);
    AssertReturn(strcmp(pszType, "e1ktxd") == 0, 0);
    E1KTXDESC *pDesc = (E1KTXDESC*)pvValue;
    if (!pDesc)
        return RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "NULL_TXD");

    size_t cbPrintf = 0;
    switch (e1kGetDescType(pDesc))
    {
        case E1K_DTYP_CONTEXT:
            cbPrintf += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "Type=Context\n"
                                    "        IPCSS=%02X IPCSO=%02X IPCSE=%04X TUCSS=%02X TUCSO=%02X TUCSE=%04X\n"
                                    "        TUCMD:%s%s%s %s %s PAYLEN=%04x HDRLEN=%04x MSS=%04x STA: %s",
                                    pDesc->context.ip.u8CSS, pDesc->context.ip.u8CSO, pDesc->context.ip.u16CSE,
                                    pDesc->context.tu.u8CSS, pDesc->context.tu.u8CSO, pDesc->context.tu.u16CSE,
                                    pDesc->context.dw2.fIDE ? " IDE":"",
                                    pDesc->context.dw2.fRS  ? " RS" :"",
                                    pDesc->context.dw2.fTSE ? " TSE":"",
                                    pDesc->context.dw2.fIP  ? "IPv4":"IPv6",
                                    pDesc->context.dw2.fTCP ?  "TCP":"UDP",
                                    pDesc->context.dw2.u20PAYLEN,
                                    pDesc->context.dw3.u8HDRLEN,
                                    pDesc->context.dw3.u16MSS,
                                    pDesc->context.dw3.fDD?"DD":"");
            break;
        case E1K_DTYP_DATA:
            cbPrintf += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "Type=Data Address=%16LX DTALEN=%05X\n"
                                    "        DCMD:%s%s%s%s%s%s%s STA:%s%s%s POPTS:%s%s SPECIAL:%s VLAN=%03x PRI=%x",
                                    pDesc->data.u64BufAddr,
                                    pDesc->data.cmd.u20DTALEN,
                                    pDesc->data.cmd.fIDE ? " IDE" :"",
                                    pDesc->data.cmd.fVLE ? " VLE" :"",
                                    pDesc->data.cmd.fRPS ? " RPS" :"",
                                    pDesc->data.cmd.fRS  ? " RS"  :"",
                                    pDesc->data.cmd.fTSE ? " TSE" :"",
                                    pDesc->data.cmd.fIFCS? " IFCS":"",
                                    pDesc->data.cmd.fEOP ? " EOP" :"",
                                    pDesc->data.dw3.fDD  ? " DD"  :"",
                                    pDesc->data.dw3.fEC  ? " EC"  :"",
                                    pDesc->data.dw3.fLC  ? " LC"  :"",
                                    pDesc->data.dw3.fTXSM? " TXSM":"",
                                    pDesc->data.dw3.fIXSM? " IXSM":"",
                                    E1K_SPEC_CFI(pDesc->data.dw3.u16Special) ? "CFI" :"cfi",
                                    E1K_SPEC_VLAN(pDesc->data.dw3.u16Special),
                                    E1K_SPEC_PRI(pDesc->data.dw3.u16Special));
            break;
        case E1K_DTYP_LEGACY:
            cbPrintf += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "Type=Legacy Address=%16LX DTALEN=%05X\n"
                                    "        CMD:%s%s%s%s%s%s%s STA:%s%s%s CSO=%02x CSS=%02x SPECIAL:%s VLAN=%03x PRI=%x",
                                    pDesc->data.u64BufAddr,
                                    pDesc->legacy.cmd.u16Length,
                                    pDesc->legacy.cmd.fIDE ? " IDE" :"",
                                    pDesc->legacy.cmd.fVLE ? " VLE" :"",
                                    pDesc->legacy.cmd.fRPS ? " RPS" :"",
                                    pDesc->legacy.cmd.fRS  ? " RS"  :"",
                                    pDesc->legacy.cmd.fIC  ? " IC"  :"",
                                    pDesc->legacy.cmd.fIFCS? " IFCS":"",
                                    pDesc->legacy.cmd.fEOP ? " EOP" :"",
                                    pDesc->legacy.dw3.fDD  ? " DD"  :"",
                                    pDesc->legacy.dw3.fEC  ? " EC"  :"",
                                    pDesc->legacy.dw3.fLC  ? " LC"  :"",
                                    pDesc->legacy.cmd.u8CSO,
                                    pDesc->legacy.dw3.u8CSS,
                                    E1K_SPEC_CFI(pDesc->legacy.dw3.u16Special) ? "CFI" :"cfi",
                                    E1K_SPEC_VLAN(pDesc->legacy.dw3.u16Special),
                                    E1K_SPEC_PRI(pDesc->legacy.dw3.u16Special));
            break;
        default:
            cbPrintf += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "Invalid Transmit Descriptor");
            break;
    }

    return cbPrintf;
}

/** Initializes debug helpers (logging format types). */
static int e1kR3InitDebugHelpers(void)
{
    int         rc                   = VINF_SUCCESS;
    static bool s_fHelpersRegistered = false;
    if (!s_fHelpersRegistered)
    {
        s_fHelpersRegistered = true;
        rc = RTStrFormatTypeRegister("e1krxd", e1kR3FmtRxDesc, NULL);
        AssertRCReturn(rc, rc);
        rc = RTStrFormatTypeRegister("e1ktxd", e1kR3FmtTxDesc, NULL);
        AssertRCReturn(rc, rc);
    }
    return rc;
}

/**
 * Status info callback.
 *
 * @param   pDevIns     The device instance.
 * @param   pHlp        The output helpers.
 * @param   pszArgs     The arguments.
 */
static DECLCALLBACK(void) e1kR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PE1KSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    unsigned  i;
    // bool        fRcvRing = false;
    // bool        fXmtRing = false;

    /*
     * Parse args.
    if (pszArgs)
    {
        fRcvRing = strstr(pszArgs, "verbose") || strstr(pszArgs, "rcv");
        fXmtRing = strstr(pszArgs, "verbose") || strstr(pszArgs, "xmt");
    }
     */

    /*
     * Show info.
     */
    pHlp->pfnPrintf(pHlp, "E1000 #%d: port=%04x mmio=%RGp mac-cfg=%RTmac %s%s%s\n",
                    pDevIns->iInstance,
                    PDMDevHlpIoPortGetMappingAddress(pDevIns, pThis->hIoPorts),
                    PDMDevHlpMmioGetMappingAddress(pDevIns, pThis->hMmioRegion),
                    &pThis->macConfigured, g_aChips[pThis->eChip].pcszName,
                    pDevIns->fRCEnabled ? " RC" : "", pDevIns->fR0Enabled ? " R0" : "");

    e1kR3CsEnterAsserted(pThis); /* Not sure why but PCNet does it */

    for (i = 0; i < E1K_NUM_OF_32BIT_REGS; ++i)
        pHlp->pfnPrintf(pHlp, "%8.8s = %08x\n", g_aE1kRegMap[i].abbrev, pThis->auRegs[i]);

    for (i = 0; i < RT_ELEMENTS(pThis->aRecAddr.array); i++)
    {
        E1KRAELEM* ra = pThis->aRecAddr.array + i;
        if (ra->ctl & RA_CTL_AV)
        {
            const char *pcszTmp;
            switch (ra->ctl & RA_CTL_AS)
            {
                case 0:  pcszTmp = "DST"; break;
                case 1:  pcszTmp = "SRC"; break;
                default: pcszTmp = "reserved";
            }
            pHlp->pfnPrintf(pHlp, "RA%02d: %s %RTmac\n", i, pcszTmp, ra->addr);
        }
    }
    unsigned cDescs = RDLEN / sizeof(E1KRXDESC);
    uint32_t rdh = RDH;
    pHlp->pfnPrintf(pHlp, "\n-- Receive Descriptors (%d total) --\n", cDescs);
    for (i = 0; i < cDescs; ++i)
    {
        E1KRXDESC desc;
        PDMDevHlpPCIPhysRead(pDevIns, e1kDescAddr(RDBAH, RDBAL, i),
                             &desc, sizeof(desc));
        if (i == rdh)
            pHlp->pfnPrintf(pHlp, ">>> ");
        pHlp->pfnPrintf(pHlp, "%RGp: %R[e1krxd]\n", e1kDescAddr(RDBAH, RDBAL, i), &desc);
    }
#ifdef E1K_WITH_RXD_CACHE
    pHlp->pfnPrintf(pHlp, "\n-- Receive Descriptors in Cache (at %d (RDH %d)/ fetched %d / max %d) --\n",
                    pThis->iRxDCurrent, RDH, pThis->nRxDFetched, E1K_RXD_CACHE_SIZE);
    if (rdh > pThis->iRxDCurrent)
        rdh -= pThis->iRxDCurrent;
    else
        rdh = cDescs + rdh - pThis->iRxDCurrent;
    for (i = 0; i < pThis->nRxDFetched; ++i)
    {
        if (i == pThis->iRxDCurrent)
            pHlp->pfnPrintf(pHlp, ">>> ");
        if (cDescs)
            pHlp->pfnPrintf(pHlp, "%RGp: %R[e1krxd]\n",
                            e1kDescAddr(RDBAH, RDBAL, rdh++ % cDescs),
                            &pThis->aRxDescriptors[i]);
        else
            pHlp->pfnPrintf(pHlp, "<lost>: %R[e1krxd]\n",
                            &pThis->aRxDescriptors[i]);
    }
#endif /* E1K_WITH_RXD_CACHE */

    cDescs = TDLEN / sizeof(E1KTXDESC);
    uint32_t tdh = TDH;
    pHlp->pfnPrintf(pHlp, "\n-- Transmit Descriptors (%d total) --\n", cDescs);
    for (i = 0; i < cDescs; ++i)
    {
        E1KTXDESC desc;
        PDMDevHlpPCIPhysRead(pDevIns, e1kDescAddr(TDBAH, TDBAL, i),
                             &desc, sizeof(desc));
        if (i == tdh)
            pHlp->pfnPrintf(pHlp, ">>> ");
        pHlp->pfnPrintf(pHlp, "%RGp: %R[e1ktxd]\n", e1kDescAddr(TDBAH, TDBAL, i), &desc);
    }
#ifdef E1K_WITH_TXD_CACHE
    pHlp->pfnPrintf(pHlp, "\n-- Transmit Descriptors in Cache (at %d (TDH %d)/ fetched %d / max %d) --\n",
                    pThis->iTxDCurrent, TDH, pThis->nTxDFetched, E1K_TXD_CACHE_SIZE);
    if (tdh > pThis->iTxDCurrent)
        tdh -= pThis->iTxDCurrent;
    else
        tdh = cDescs + tdh - pThis->iTxDCurrent;
    for (i = 0; i < pThis->nTxDFetched; ++i)
    {
        if (i == pThis->iTxDCurrent)
            pHlp->pfnPrintf(pHlp, ">>> ");
        if (cDescs)
            pHlp->pfnPrintf(pHlp, "%RGp: %R[e1ktxd]\n",
                            e1kDescAddr(TDBAH, TDBAL, tdh++ % cDescs),
                            &pThis->aTxDescriptors[i]);
        else
            pHlp->pfnPrintf(pHlp, "<lost>: %R[e1ktxd]\n",
                            &pThis->aTxDescriptors[i]);
    }
#endif /* E1K_WITH_TXD_CACHE */


#ifdef E1K_INT_STATS
    pHlp->pfnPrintf(pHlp, "Interrupt attempts: %d\n", pThis->uStatIntTry);
    pHlp->pfnPrintf(pHlp, "Interrupts raised : %d\n", pThis->uStatInt);
    pHlp->pfnPrintf(pHlp, "Interrupts lowered: %d\n", pThis->uStatIntLower);
    pHlp->pfnPrintf(pHlp, "ICR outside ISR   : %d\n", pThis->uStatNoIntICR);
    pHlp->pfnPrintf(pHlp, "IMS raised ints   : %d\n", pThis->uStatIntIMS);
    pHlp->pfnPrintf(pHlp, "Interrupts skipped: %d\n", pThis->uStatIntSkip);
    pHlp->pfnPrintf(pHlp, "Masked interrupts : %d\n", pThis->uStatIntMasked);
    pHlp->pfnPrintf(pHlp, "Early interrupts  : %d\n", pThis->uStatIntEarly);
    pHlp->pfnPrintf(pHlp, "Late interrupts   : %d\n", pThis->uStatIntLate);
    pHlp->pfnPrintf(pHlp, "Lost interrupts   : %d\n", pThis->iStatIntLost);
    pHlp->pfnPrintf(pHlp, "Interrupts by RX  : %d\n", pThis->uStatIntRx);
    pHlp->pfnPrintf(pHlp, "Interrupts by TX  : %d\n", pThis->uStatIntTx);
    pHlp->pfnPrintf(pHlp, "Interrupts by ICS : %d\n", pThis->uStatIntICS);
    pHlp->pfnPrintf(pHlp, "Interrupts by RDTR: %d\n", pThis->uStatIntRDTR);
    pHlp->pfnPrintf(pHlp, "Interrupts by RDMT: %d\n", pThis->uStatIntRXDMT0);
    pHlp->pfnPrintf(pHlp, "Interrupts by TXQE: %d\n", pThis->uStatIntTXQE);
    pHlp->pfnPrintf(pHlp, "TX int delay asked: %d\n", pThis->uStatTxIDE);
    pHlp->pfnPrintf(pHlp, "TX delayed:         %d\n", pThis->uStatTxDelayed);
    pHlp->pfnPrintf(pHlp, "TX delayed expired: %d\n", pThis->uStatTxDelayExp);
    pHlp->pfnPrintf(pHlp, "TX no report asked: %d\n", pThis->uStatTxNoRS);
    pHlp->pfnPrintf(pHlp, "TX abs timer expd : %d\n", pThis->uStatTAD);
    pHlp->pfnPrintf(pHlp, "TX int timer expd : %d\n", pThis->uStatTID);
    pHlp->pfnPrintf(pHlp, "RX abs timer expd : %d\n", pThis->uStatRAD);
    pHlp->pfnPrintf(pHlp, "RX int timer expd : %d\n", pThis->uStatRID);
    pHlp->pfnPrintf(pHlp, "TX CTX descriptors: %d\n", pThis->uStatDescCtx);
    pHlp->pfnPrintf(pHlp, "TX DAT descriptors: %d\n", pThis->uStatDescDat);
    pHlp->pfnPrintf(pHlp, "TX LEG descriptors: %d\n", pThis->uStatDescLeg);
    pHlp->pfnPrintf(pHlp, "Received frames   : %d\n", pThis->uStatRxFrm);
    pHlp->pfnPrintf(pHlp, "Transmitted frames: %d\n", pThis->uStatTxFrm);
    pHlp->pfnPrintf(pHlp, "TX frames up to 1514: %d\n", pThis->uStatTx1514);
    pHlp->pfnPrintf(pHlp, "TX frames up to 2962: %d\n", pThis->uStatTx2962);
    pHlp->pfnPrintf(pHlp, "TX frames up to 4410: %d\n", pThis->uStatTx4410);
    pHlp->pfnPrintf(pHlp, "TX frames up to 5858: %d\n", pThis->uStatTx5858);
    pHlp->pfnPrintf(pHlp, "TX frames up to 7306: %d\n", pThis->uStatTx7306);
    pHlp->pfnPrintf(pHlp, "TX frames up to 8754: %d\n", pThis->uStatTx8754);
    pHlp->pfnPrintf(pHlp, "TX frames up to 16384: %d\n", pThis->uStatTx16384);
    pHlp->pfnPrintf(pHlp, "TX frames up to 32768: %d\n", pThis->uStatTx32768);
    pHlp->pfnPrintf(pHlp, "Larger TX frames    : %d\n", pThis->uStatTxLarge);
#endif /* E1K_INT_STATS */

    e1kCsLeave(pThis);
}



/* -=-=-=-=- PDMDEVREG -=-=-=-=- */

/**
 * Detach notification.
 *
 * One port on the network card has been disconnected from the network.
 *
 * @param   pDevIns     The device instance.
 * @param   iLUN        The logical unit which is being detached.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 */
static DECLCALLBACK(void) e1kR3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PE1KSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
    Log(("%s e1kR3Detach:\n", pThis->szPrf));
    RT_NOREF(fFlags);

    AssertLogRelReturnVoid(iLUN == 0);

    e1kR3CsEnterAsserted(pThis);

    /* Mark device as detached. */
    pThis->fIsAttached = false;
    /*
     * Zero some important members.
     */
    pThisCC->pDrvBase = NULL;
    pThisCC->pDrvR3   = NULL;
#if 0 /** @todo @bugref{9218} ring-0 driver stuff */
    pThisR0->pDrvR0   = NIL_RTR0PTR;
    pThisRC->pDrvRC   = NIL_RTRCPTR;
#endif

    PDMDevHlpCritSectLeave(pDevIns, &pThis->cs);
}

/**
 * Attach the Network attachment.
 *
 * One port on the network card has been connected to a network.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   iLUN        The logical unit which is being attached.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 *
 * @remarks This code path is not used during construction.
 */
static DECLCALLBACK(int) e1kR3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PE1KSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
    LogFlow(("%s e1kR3Attach:\n",  pThis->szPrf));
    RT_NOREF(fFlags);

    AssertLogRelReturn(iLUN == 0, VERR_PDM_NO_SUCH_LUN);

    e1kR3CsEnterAsserted(pThis);

    /*
     * Attach the driver.
     */
    int rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThisCC->IBase, &pThisCC->pDrvBase, "Network Port");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrvR3 = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMINETWORKUP);
        AssertMsgStmt(pThisCC->pDrvR3, ("Failed to obtain the PDMINETWORKUP interface!\n"),
                      rc = VERR_PDM_MISSING_INTERFACE_BELOW);
        if (RT_SUCCESS(rc))
        {
#if 0 /** @todo @bugref{9218} ring-0 driver stuff */
            pThisR0->pDrvR0 = PDMIBASER0_QUERY_INTERFACE(PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMIBASER0), PDMINETWORKUP);
            pThisRC->pDrvRC = PDMIBASERC_QUERY_INTERFACE(PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMIBASERC), PDMINETWORKUP);
#endif
            /* Mark device as attached. */
            pThis->fIsAttached = true;
        }
    }
    else if (   rc == VERR_PDM_NO_ATTACHED_DRIVER
             || rc == VERR_PDM_CFG_MISSING_DRIVER_NAME)
    {
        /* This should never happen because this function is not called
         * if there is no driver to attach! */
        Log(("%s No attached driver!\n", pThis->szPrf));
    }

    /*
     * Temporary set the link down if it was up so that the guest will know
     * that we have change the configuration of the network card
     */
    if ((STATUS & STATUS_LU) && RT_SUCCESS(rc))
        e1kR3LinkDownTemp(pDevIns, pThis, pThisCC);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->cs);
    return rc;
}

/**
 * @copydoc FNPDMDEVPOWEROFF
 */
static DECLCALLBACK(void) e1kR3PowerOff(PPDMDEVINS pDevIns)
{
    /* Poke thread waiting for buffer space. */
    e1kWakeupReceive(pDevIns, PDMDEVINS_2_DATA(pDevIns, PE1KSTATE));
}

/**
 * @copydoc FNPDMDEVRESET
 */
static DECLCALLBACK(void) e1kR3Reset(PPDMDEVINS pDevIns)
{
    PE1KSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
#ifdef E1K_TX_DELAY
    e1kCancelTimer(pDevIns, pThis, pThis->hTXDTimer);
#endif /* E1K_TX_DELAY */
    e1kCancelTimer(pDevIns, pThis, pThis->hIntTimer);
    e1kCancelTimer(pDevIns, pThis, pThis->hLUTimer);
    e1kXmitFreeBuf(pThis, pThisCC);
    pThis->u16TxPktLen  = 0;
    pThis->fIPcsum      = false;
    pThis->fTCPcsum     = false;
    pThis->fIntMaskUsed = false;
    pThis->fDelayInts   = false;
    pThis->fLocked      = false;
    pThis->u64AckedAt   = 0;
    e1kR3HardReset(pDevIns, pThis, pThisCC);
}

/**
 * @copydoc FNPDMDEVSUSPEND
 */
static DECLCALLBACK(void) e1kR3Suspend(PPDMDEVINS pDevIns)
{
    /* Poke thread waiting for buffer space. */
    e1kWakeupReceive(pDevIns, PDMDEVINS_2_DATA(pDevIns, PE1KSTATE));
}

/**
 * Device relocation callback.
 *
 * When this callback is called the device instance data, and if the
 * device have a GC component, is being relocated, or/and the selectors
 * have been changed. The device must use the chance to perform the
 * necessary pointer relocations and data updates.
 *
 * Before the GC code is executed the first time, this function will be
 * called with a 0 delta so GC pointer calculations can be one in one place.
 *
 * @param   pDevIns     Pointer to the device instance.
 * @param   offDelta    The relocation delta relative to the old location.
 *
 * @remark  A relocation CANNOT fail.
 */
static DECLCALLBACK(void) e1kR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    PE1KSTATERC pThisRC = PDMINS_2_DATA_RC(pDevIns, PE1KSTATERC);
    if (pThisRC)
        pThisRC->pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);
    RT_NOREF(offDelta);
}

/**
 * Destruct a device instance.
 *
 * We need to free non-VM resources only.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance data.
 * @thread  EMT
 */
static DECLCALLBACK(int) e1kR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PE1KSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);

    e1kDumpState(pThis);
    E1kLog(("%s Destroying instance\n", pThis->szPrf));
    if (PDMDevHlpCritSectIsInitialized(pDevIns, &pThis->cs))
    {
        if (pThis->hEventMoreRxDescAvail != NIL_SUPSEMEVENT)
        {
            PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEventMoreRxDescAvail);
            RTThreadYield();
            PDMDevHlpSUPSemEventClose(pDevIns, pThis->hEventMoreRxDescAvail);
            pThis->hEventMoreRxDescAvail = NIL_SUPSEMEVENT;
        }
#ifdef E1K_WITH_TX_CS
        PDMDevHlpCritSectDelete(pDevIns, &pThis->csTx);
#endif /* E1K_WITH_TX_CS */
        PDMDevHlpCritSectDelete(pDevIns, &pThis->csRx);
        PDMDevHlpCritSectDelete(pDevIns, &pThis->cs);
    }
    return VINF_SUCCESS;
}


/**
 * Set PCI configuration space registers.
 *
 * @param   pci         Reference to PCI device structure.
 * @thread  EMT
 */
static void e1kR3ConfigurePciDev(PPDMPCIDEV pPciDev, E1KCHIP eChip)
{
    Assert(eChip < RT_ELEMENTS(g_aChips));
    /* Configure PCI Device, assume 32-bit mode ******************************/
    PDMPciDevSetVendorId(pPciDev, g_aChips[eChip].uPCIVendorId);
    PDMPciDevSetDeviceId(pPciDev, g_aChips[eChip].uPCIDeviceId);
    PDMPciDevSetWord( pPciDev, VBOX_PCI_SUBSYSTEM_VENDOR_ID, g_aChips[eChip].uPCISubsystemVendorId);
    PDMPciDevSetWord( pPciDev, VBOX_PCI_SUBSYSTEM_ID, g_aChips[eChip].uPCISubsystemId);

    PDMPciDevSetWord( pPciDev, VBOX_PCI_COMMAND,            0x0000);
    /* DEVSEL Timing (medium device), 66 MHz Capable, New capabilities */
    PDMPciDevSetWord( pPciDev, VBOX_PCI_STATUS,
                      VBOX_PCI_STATUS_DEVSEL_MEDIUM | VBOX_PCI_STATUS_CAP_LIST |  VBOX_PCI_STATUS_66MHZ);
    /* Stepping A2 */
    PDMPciDevSetByte( pPciDev, VBOX_PCI_REVISION_ID,          0x02);
    /* Ethernet adapter */
    PDMPciDevSetByte( pPciDev, VBOX_PCI_CLASS_PROG,           0x00);
    PDMPciDevSetWord( pPciDev, VBOX_PCI_CLASS_DEVICE,       0x0200);
    /* normal single function Ethernet controller */
    PDMPciDevSetByte( pPciDev, VBOX_PCI_HEADER_TYPE,          0x00);
    /* Memory Register Base Address */
    PDMPciDevSetDWord(pPciDev, VBOX_PCI_BASE_ADDRESS_0, 0x00000000);
    /* Memory Flash Base Address */
    PDMPciDevSetDWord(pPciDev, VBOX_PCI_BASE_ADDRESS_1, 0x00000000);
    /* IO Register Base Address */
    PDMPciDevSetDWord(pPciDev, VBOX_PCI_BASE_ADDRESS_2, 0x00000001);
    /* Expansion ROM Base Address */
    PDMPciDevSetDWord(pPciDev, VBOX_PCI_ROM_ADDRESS,    0x00000000);
    /* Capabilities Pointer */
    PDMPciDevSetByte( pPciDev, VBOX_PCI_CAPABILITY_LIST,      0xDC);
    /* Interrupt Pin: INTA# */
    PDMPciDevSetByte( pPciDev, VBOX_PCI_INTERRUPT_PIN,        0x01);
    /* Max_Lat/Min_Gnt: very high priority and time slice */
    PDMPciDevSetByte( pPciDev, VBOX_PCI_MIN_GNT,              0xFF);
    PDMPciDevSetByte( pPciDev, VBOX_PCI_MAX_LAT,              0x00);

    /* PCI Power Management Registers ****************************************/
    /* Capability ID: PCI Power Management Registers */
    PDMPciDevSetByte( pPciDev, 0xDC,            VBOX_PCI_CAP_ID_PM);
    /* Next Item Pointer: PCI-X */
    PDMPciDevSetByte( pPciDev, 0xDC + 1,                      0xE4);
    /* Power Management Capabilities: PM disabled, DSI */
    PDMPciDevSetWord( pPciDev, 0xDC + 2,
                      0x0002 | VBOX_PCI_PM_CAP_DSI);
    /* Power Management Control / Status Register: PM disabled */
    PDMPciDevSetWord( pPciDev, 0xDC + 4,                    0x0000);
    /* PMCSR_BSE Bridge Support Extensions: Not supported */
    PDMPciDevSetByte( pPciDev, 0xDC + 6,                      0x00);
    /* Data Register: PM disabled, always 0 */
    PDMPciDevSetByte( pPciDev, 0xDC + 7,                      0x00);

    /* PCI-X Configuration Registers *****************************************/
    /* Capability ID: PCI-X Configuration Registers */
    PDMPciDevSetByte( pPciDev, 0xE4,          VBOX_PCI_CAP_ID_PCIX);
#ifdef E1K_WITH_MSI
    PDMPciDevSetByte( pPciDev, 0xE4 + 1,                      0x80);
#else
    /* Next Item Pointer: None (Message Signalled Interrupts are disabled) */
    PDMPciDevSetByte( pPciDev, 0xE4 + 1,                      0x00);
#endif
    /* PCI-X Command: Enable Relaxed Ordering */
    PDMPciDevSetWord( pPciDev, 0xE4 + 2,        VBOX_PCI_X_CMD_ERO);
    /* PCI-X Status: 32-bit, 66MHz*/
    /** @todo is this value really correct? fff8 doesn't look like actual PCI address */
    PDMPciDevSetDWord(pPciDev, 0xE4 + 4,                0x0040FFF8);
}

/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) e1kR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PE1KSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);
    int         rc;

    /*
     * Initialize the instance data (state).
     * Note! Caller has initialized it to ZERO already.
     */
    RTStrPrintf(pThis->szPrf, sizeof(pThis->szPrf), "E1000#%d", iInstance);
    E1kLog(("%s Constructing new instance sizeof(E1KRXDESC)=%d\n", pThis->szPrf, sizeof(E1KRXDESC)));
    pThis->hEventMoreRxDescAvail = NIL_SUPSEMEVENT;
    pThis->u16TxPktLen  = 0;
    pThis->fIPcsum      = false;
    pThis->fTCPcsum     = false;
    pThis->fIntMaskUsed = false;
    pThis->fDelayInts   = false;
    pThis->fLocked      = false;
    pThis->u64AckedAt   = 0;
    pThis->led.u32Magic = PDMLED_MAGIC;
    pThis->u32PktNo     = 1;
    pThis->fIsAttached  = false;

    pThisCC->pDevInsR3  = pDevIns;
    pThisCC->pShared    = pThis;

    /* Interfaces */
    pThisCC->IBase.pfnQueryInterface          = e1kR3QueryInterface;

    pThisCC->INetworkDown.pfnWaitReceiveAvail = e1kR3NetworkDown_WaitReceiveAvail;
    pThisCC->INetworkDown.pfnReceive          = e1kR3NetworkDown_Receive;
    pThisCC->INetworkDown.pfnXmitPending      = e1kR3NetworkDown_XmitPending;

    pThisCC->ILeds.pfnQueryStatusLed          = e1kR3QueryStatusLed;

    pThisCC->INetworkConfig.pfnGetMac         = e1kR3GetMac;
    pThisCC->INetworkConfig.pfnGetLinkState   = e1kR3GetLinkState;
    pThisCC->INetworkConfig.pfnSetLinkState   = e1kR3SetLinkState;

    /*
     * Internal validations.
     */
    for (uint32_t iReg = 1; iReg < E1K_NUM_OF_BINARY_SEARCHABLE; iReg++)
        AssertLogRelMsgReturn(   g_aE1kRegMap[iReg].offset > g_aE1kRegMap[iReg - 1].offset
                              &&    g_aE1kRegMap[iReg].offset + g_aE1kRegMap[iReg].size
                                 >= g_aE1kRegMap[iReg - 1].offset + g_aE1kRegMap[iReg - 1].size,
                              ("%s@%#xLB%#x vs %s@%#xLB%#x\n",
                               g_aE1kRegMap[iReg].abbrev,      g_aE1kRegMap[iReg].offset,     g_aE1kRegMap[iReg].size,
                               g_aE1kRegMap[iReg - 1].abbrev,  g_aE1kRegMap[iReg - 1].offset, g_aE1kRegMap[iReg - 1].size),
                               VERR_INTERNAL_ERROR_4);

    /*
     * Validate configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns,
                                  "MAC|"
                                  "CableConnected|"
                                  "AdapterType|"
                                  "LineSpeed|"
                                  "ItrEnabled|"
                                  "ItrRxEnabled|"
                                  "EthernetCRC|"
                                  "GSOEnabled|"
                                  "LinkUpDelay|"
                                  "StatNo",
                                  "");

    /** @todo LineSpeed unused! */

    /*
     * Get config params
     */
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;
    rc = pHlp->pfnCFGMQueryBytes(pCfg, "MAC", pThis->macConfigured.au8, sizeof(pThis->macConfigured.au8));
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get MAC address"));
    rc = pHlp->pfnCFGMQueryBool(pCfg, "CableConnected", &pThis->fCableConnected);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the value of 'CableConnected'"));
    rc = pHlp->pfnCFGMQueryU32(pCfg, "AdapterType", (uint32_t*)&pThis->eChip);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the value of 'AdapterType'"));
    Assert(pThis->eChip <= E1K_CHIP_82545EM);

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "EthernetCRC", &pThis->fEthernetCRC, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the value of 'EthernetCRC'"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "GSOEnabled", &pThis->fGSOEnabled, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the value of 'GSOEnabled'"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "ItrEnabled", &pThis->fItrEnabled, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the value of 'ItrEnabled'"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "ItrRxEnabled", &pThis->fItrRxEnabled, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the value of 'ItrRxEnabled'"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "TidEnabled", &pThis->fTidEnabled, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the value of 'TidEnabled'"));

    /*
     * Increased the link up delay from 3 to 5 seconds to make sure a guest notices the link loss
     * and updates its network configuration when the link is restored. See @bugref{10114}.
     */
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "LinkUpDelay", (uint32_t*)&pThis->cMsLinkUpDelay, 5000); /* ms */
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the value of 'LinkUpDelay'"));
    Assert(pThis->cMsLinkUpDelay <= 300000); /* less than 5 minutes */
    if (pThis->cMsLinkUpDelay > 5000)
        LogRel(("%s: WARNING! Link up delay is set to %u seconds!\n", pThis->szPrf, pThis->cMsLinkUpDelay / 1000));
    else if (pThis->cMsLinkUpDelay == 0)
        LogRel(("%s: WARNING! Link up delay is disabled!\n", pThis->szPrf));

    uint32_t uStatNo = (uint32_t)iInstance;
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "StatNo", &uStatNo, (uint32_t)iInstance);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"StatNo\" value"));

    LogRel(("%s: Chip=%s LinkUpDelay=%ums EthernetCRC=%s GSO=%s Itr=%s ItrRx=%s TID=%s R0=%s RC=%s\n", pThis->szPrf,
            g_aChips[pThis->eChip].pcszName, pThis->cMsLinkUpDelay,
            pThis->fEthernetCRC ? "on" : "off",
            pThis->fGSOEnabled ? "enabled" : "disabled",
            pThis->fItrEnabled ? "enabled" : "disabled",
            pThis->fItrRxEnabled ? "enabled" : "disabled",
            pThis->fTidEnabled ? "enabled" : "disabled",
            pDevIns->fR0Enabled ? "enabled" : "disabled",
            pDevIns->fRCEnabled ? "enabled" : "disabled"));

    /*
     * Initialize sub-components and register everything with the VMM.
     */

    /* Initialize the EEPROM. */
    pThisCC->eeprom.init(pThis->macConfigured);

    /* Initialize internal PHY. */
    Phy::init(&pThis->phy, iInstance, pThis->eChip == E1K_CHIP_82543GC ? PHY_EPID_M881000 : PHY_EPID_M881011);

    /* Initialize critical sections. We do our own locking. */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->cs, RT_SRC_POS, "E1000#%d", iInstance);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->csRx, RT_SRC_POS, "E1000#%dRX", iInstance);
    AssertRCReturn(rc, rc);
#ifdef E1K_WITH_TX_CS
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->csTx, RT_SRC_POS, "E1000#%dTX", iInstance);
    AssertRCReturn(rc, rc);
#endif

    /* Saved state registration. */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, E1K_SAVEDSTATE_VERSION, sizeof(E1KSTATE), NULL,
                                NULL,        e1kR3LiveExec, NULL,
                                e1kR3SavePrep, e1kR3SaveExec, NULL,
                                e1kR3LoadPrep, e1kR3LoadExec, e1kR3LoadDone);
    AssertRCReturn(rc, rc);

    /* Set PCI config registers and register ourselves with the PCI bus. */
    PDMPCIDEV_ASSERT_VALID(pDevIns, pDevIns->apPciDevs[0]);
    e1kR3ConfigurePciDev(pDevIns->apPciDevs[0], pThis->eChip);
    rc = PDMDevHlpPCIRegister(pDevIns, pDevIns->apPciDevs[0]);
    AssertRCReturn(rc, rc);

#ifdef E1K_WITH_MSI
    PDMMSIREG MsiReg;
    RT_ZERO(MsiReg);
    MsiReg.cMsiVectors    = 1;
    MsiReg.iMsiCapOffset  = 0x80;
    MsiReg.iMsiNextOffset = 0x0;
    MsiReg.fMsi64bit      = false;
    rc = PDMDevHlpPCIRegisterMsi(pDevIns, &MsiReg);
    AssertRCReturn(rc, rc);
#endif

    /*
     * Map our registers to memory space (region 0, see e1kR3ConfigurePciDev)
     * From the spec (regarding flags):
     *    For registers that should be accessed as 32-bit double words,
     *    partial writes (less than a 32-bit double word) is ignored.
     *    Partial reads return all 32 bits of data regardless of the
     *    byte enables.
     */
    rc = PDMDevHlpMmioCreateEx(pDevIns, E1K_MM_SIZE, IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_ONLY_DWORD,
                               pDevIns->apPciDevs[0], 0 /*iPciRegion*/,
                               e1kMMIOWrite, e1kMMIORead, NULL /*pfnFill*/, NULL /*pvUser*/, "E1000", &pThis->hMmioRegion);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpPCIIORegionRegisterMmio(pDevIns, 0, E1K_MM_SIZE, PCI_ADDRESS_SPACE_MEM, pThis->hMmioRegion, NULL);
    AssertRCReturn(rc, rc);

    /* Map our registers to IO space (region 2, see e1kR3ConfigurePciDev) */
    static IOMIOPORTDESC const s_aExtDescs[] =
    {
        { "IOADDR", "IOADDR", NULL, NULL }, { "unused", "unused", NULL, NULL }, { "unused", "unused", NULL, NULL }, { "unused", "unused", NULL, NULL },
        { "IODATA", "IODATA", NULL, NULL }, { "unused", "unused", NULL, NULL }, { "unused", "unused", NULL, NULL }, { "unused", "unused", NULL, NULL },
        { NULL, NULL, NULL, NULL }
    };
    rc = PDMDevHlpIoPortCreate(pDevIns, E1K_IOPORT_SIZE, pDevIns->apPciDevs[0], 2 /*iPciRegion*/,
                               e1kIOPortOut, e1kIOPortIn, NULL /*pvUser*/, "E1000", s_aExtDescs, &pThis->hIoPorts);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpPCIIORegionRegisterIo(pDevIns, 2, E1K_IOPORT_SIZE, pThis->hIoPorts);
    AssertRCReturn(rc, rc);

    /* Create transmit queue */
    rc = PDMDevHlpTaskCreate(pDevIns, PDMTASK_F_RZ, "E1000-Xmit", e1kR3TxTaskCallback, NULL, &pThis->hTxTask);
    AssertRCReturn(rc, rc);

#ifdef E1K_TX_DELAY
    /* Create Transmit Delay Timer */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, e1kR3TxDelayTimer, pThis,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, "E1000 Xmit Delay", &pThis->hTXDTimer);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpTimerSetCritSect(pDevIns, pThis->hTXDTimer, &pThis->csTx);
    AssertRCReturn(rc, rc);
#endif /* E1K_TX_DELAY */

//#ifdef E1K_USE_TX_TIMERS
    if (pThis->fTidEnabled)
    {
        /* Create Transmit Interrupt Delay Timer */
        rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, e1kR3TxIntDelayTimer, pThis,
                                  TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, "E1000 Xmit IRQ Delay", &pThis->hTIDTimer);
        AssertRCReturn(rc, rc);

# ifndef E1K_NO_TAD
        /* Create Transmit Absolute Delay Timer */
        rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, e1kR3TxAbsDelayTimer, pThis,
                                  TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, "E1000 Xmit Abs Delay", &pThis->hTADTimer);
        AssertRCReturn(rc, rc);
# endif /* E1K_NO_TAD */
    }
//#endif /* E1K_USE_TX_TIMERS */

#ifdef E1K_USE_RX_TIMERS
    /* Create Receive Interrupt Delay Timer */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, e1kR3RxIntDelayTimer, pThis,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0,  "E1000 Recv IRQ Delay", &pThis->hRIDTimer);
    AssertRCReturn(rc, rc);

    /* Create Receive Absolute Delay Timer */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, e1kR3RxAbsDelayTimer, pThis,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0,  "E1000 Recv Abs Delay", &pThis->hRADTimer);
    AssertRCReturn(rc, rc);
#endif /* E1K_USE_RX_TIMERS */

    /* Create Late Interrupt Timer */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, e1kR3LateIntTimer, pThis,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0,  "E1000 Late IRQ", &pThis->hIntTimer);
    AssertRCReturn(rc, rc);

    /* Create Link Up Timer */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, e1kR3LinkUpTimer, pThis,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0,  "E1000 Link Up", &pThis->hLUTimer);
    AssertRCReturn(rc, rc);

    /* Register the info item */
    char szTmp[20];
    RTStrPrintf(szTmp, sizeof(szTmp), "e1k%d", iInstance);
    PDMDevHlpDBGFInfoRegister(pDevIns, szTmp, "E1000 info.", e1kR3Info);

    /* Status driver */
    PPDMIBASE pBase;
    rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pThisCC->IBase, &pBase, "Status Port");
    if (RT_SUCCESS(rc))
        pThisCC->pLedsConnector = PDMIBASE_QUERY_INTERFACE(pBase, PDMILEDCONNECTORS);
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        rc = VINF_SUCCESS;
    else
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to attach the status LUN"));

    /* Network driver */
    rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThisCC->IBase, &pThisCC->pDrvBase, "Network Port");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrvR3 = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMINETWORKUP);
        AssertMsgReturn(pThisCC->pDrvR3, ("Failed to obtain the PDMINETWORKUP interface!\n"), VERR_PDM_MISSING_INTERFACE_BELOW);

#if 0 /** @todo @bugref{9218} ring-0 driver stuff */
        pThisR0->pDrvR0 = PDMIBASER0_QUERY_INTERFACE(PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMIBASER0), PDMINETWORKUP);
        pThisRC->pDrvRC = PDMIBASERC_QUERY_INTERFACE(PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMIBASERC), PDMINETWORKUP);
#endif
        /* Mark device as attached. */
        pThis->fIsAttached = true;
    }
    else if (   rc == VERR_PDM_NO_ATTACHED_DRIVER
             || rc == VERR_PDM_CFG_MISSING_DRIVER_NAME)
    {
        /* No error! */
        E1kLog(("%s This adapter is not attached to any network!\n", pThis->szPrf));
    }
    else
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to attach the network LUN"));

    rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pThis->hEventMoreRxDescAvail);
    AssertRCReturn(rc, rc);

    rc = e1kR3InitDebugHelpers();
    AssertRCReturn(rc, rc);

    e1kR3HardReset(pDevIns, pThis, pThisCC);

    /*
     * Register statistics.
     * The /Public/ bits are official and used by session info in the GUI.
     */
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatReceiveBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                           "Amount of data received",    "/Public/NetAdapter/%u/BytesReceived", uStatNo);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatTransmitBytes, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                           "Amount of data transmitted", "/Public/NetAdapter/%u/BytesTransmitted", uStatNo);
    PDMDevHlpSTAMRegisterF(pDevIns, &pDevIns->iInstance,       STAMTYPE_U32,     STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                           "Device instance number",     "/Public/NetAdapter/%u/%s", uStatNo, pDevIns->pReg->szName);

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatReceiveBytes,       STAMTYPE_COUNTER, "ReceiveBytes",         STAMUNIT_BYTES,          "Amount of data received");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitBytes,      STAMTYPE_COUNTER, "TransmitBytes",        STAMUNIT_BYTES,          "Amount of data transmitted");

#if defined(VBOX_WITH_STATISTICS)
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMMIOReadRZ,         STAMTYPE_PROFILE, "MMIO/ReadRZ",          STAMUNIT_TICKS_PER_CALL, "Profiling MMIO reads in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMMIOReadR3,         STAMTYPE_PROFILE, "MMIO/ReadR3",          STAMUNIT_TICKS_PER_CALL, "Profiling MMIO reads in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMMIOWriteRZ,        STAMTYPE_PROFILE, "MMIO/WriteRZ",         STAMUNIT_TICKS_PER_CALL, "Profiling MMIO writes in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMMIOWriteR3,        STAMTYPE_PROFILE, "MMIO/WriteR3",         STAMUNIT_TICKS_PER_CALL, "Profiling MMIO writes in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatEEPROMRead,         STAMTYPE_PROFILE, "EEPROM/Read",          STAMUNIT_TICKS_PER_CALL, "Profiling EEPROM reads");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatEEPROMWrite,        STAMTYPE_PROFILE, "EEPROM/Write",         STAMUNIT_TICKS_PER_CALL, "Profiling EEPROM writes");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIOReadRZ,           STAMTYPE_PROFILE, "IO/ReadRZ",            STAMUNIT_TICKS_PER_CALL, "Profiling IO reads in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIOReadR3,           STAMTYPE_PROFILE, "IO/ReadR3",            STAMUNIT_TICKS_PER_CALL, "Profiling IO reads in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIOWriteRZ,          STAMTYPE_PROFILE, "IO/WriteRZ",           STAMUNIT_TICKS_PER_CALL, "Profiling IO writes in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIOWriteR3,          STAMTYPE_PROFILE, "IO/WriteR3",           STAMUNIT_TICKS_PER_CALL, "Profiling IO writes in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatLateIntTimer,       STAMTYPE_PROFILE, "LateInt/Timer",        STAMUNIT_TICKS_PER_CALL, "Profiling late int timer");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatLateInts,           STAMTYPE_COUNTER, "LateInt/Occured",      STAMUNIT_OCCURENCES,     "Number of late interrupts");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIntsRaised,         STAMTYPE_COUNTER, "Interrupts/Raised",    STAMUNIT_OCCURENCES,     "Number of raised interrupts");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIntsPrevented,      STAMTYPE_COUNTER, "Interrupts/Prevented", STAMUNIT_OCCURENCES,     "Number of prevented interrupts");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatReceive,            STAMTYPE_PROFILE, "Receive/Total",        STAMUNIT_TICKS_PER_CALL, "Profiling receive");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatReceiveCRC,         STAMTYPE_PROFILE, "Receive/CRC",          STAMUNIT_TICKS_PER_CALL, "Profiling receive checksumming");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatReceiveFilter,      STAMTYPE_PROFILE, "Receive/Filter",       STAMUNIT_TICKS_PER_CALL, "Profiling receive filtering");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatReceiveStore,       STAMTYPE_PROFILE, "Receive/Store",        STAMUNIT_TICKS_PER_CALL, "Profiling receive storing");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRxOverflow,         STAMTYPE_PROFILE, "RxOverflow",           STAMUNIT_TICKS_PER_OCCURENCE, "Profiling RX overflows");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRxOverflowWakeupRZ, STAMTYPE_COUNTER, "RxOverflowWakeupRZ",   STAMUNIT_OCCURENCES,     "Nr of RX overflow wakeups in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRxOverflowWakeupR3, STAMTYPE_COUNTER, "RxOverflowWakeupR3",   STAMUNIT_OCCURENCES,     "Nr of RX overflow wakeups in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitRZ,         STAMTYPE_PROFILE, "Transmit/TotalRZ",     STAMUNIT_TICKS_PER_CALL, "Profiling transmits in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitR3,         STAMTYPE_PROFILE, "Transmit/TotalR3",     STAMUNIT_TICKS_PER_CALL, "Profiling transmits in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitSendRZ,     STAMTYPE_PROFILE, "Transmit/SendRZ",      STAMUNIT_TICKS_PER_CALL, "Profiling send transmit in RZ");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitSendR3,     STAMTYPE_PROFILE, "Transmit/SendR3",      STAMUNIT_TICKS_PER_CALL, "Profiling send transmit in R3");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTxDescCtxNormal,    STAMTYPE_COUNTER, "TxDesc/ContexNormal",  STAMUNIT_OCCURENCES,     "Number of normal context descriptors");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTxDescCtxTSE,       STAMTYPE_COUNTER, "TxDesc/ContextTSE",    STAMUNIT_OCCURENCES,     "Number of TSE context descriptors");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTxDescData,         STAMTYPE_COUNTER, "TxDesc/Data",          STAMUNIT_OCCURENCES,     "Number of TX data descriptors");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTxDescLegacy,       STAMTYPE_COUNTER, "TxDesc/Legacy",        STAMUNIT_OCCURENCES,     "Number of TX legacy descriptors");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTxDescTSEData,      STAMTYPE_COUNTER, "TxDesc/TSEData",       STAMUNIT_OCCURENCES,     "Number of TX TSE data descriptors");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTxPathFallback,     STAMTYPE_COUNTER, "TxPath/Fallback",      STAMUNIT_OCCURENCES,     "Fallback TSE descriptor path");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTxPathGSO,          STAMTYPE_COUNTER, "TxPath/GSO",           STAMUNIT_OCCURENCES,     "GSO TSE descriptor path");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTxPathRegular,      STAMTYPE_COUNTER, "TxPath/Normal",        STAMUNIT_OCCURENCES,     "Regular descriptor path");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatPHYAccesses,        STAMTYPE_COUNTER, "PHYAccesses",          STAMUNIT_OCCURENCES,     "Number of PHY accesses");
    for (unsigned iReg = 0; iReg < E1K_NUM_OF_REGS; iReg++)
    {
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStatRegReads[iReg],   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                               g_aE1kRegMap[iReg].name, "Regs/%s-Reads", g_aE1kRegMap[iReg].abbrev);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStatRegWrites[iReg],   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                               g_aE1kRegMap[iReg].name, "Regs/%s-Writes", g_aE1kRegMap[iReg].abbrev);
    }
#endif /* VBOX_WITH_STATISTICS */

#ifdef E1K_INT_STATS
    PDMDevHlpSTAMRegister(pDevIns, &pThis->u64ArmedAt,      STAMTYPE_U64, "u64ArmedAt",      STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatMaxTxDelay, STAMTYPE_U64, "uStatMaxTxDelay", STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatInt,        STAMTYPE_U32, "uStatInt",        STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntTry,     STAMTYPE_U32, "uStatIntTry",     STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntLower,   STAMTYPE_U32, "uStatIntLower",   STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatNoIntICR,   STAMTYPE_U32, "uStatNoIntICR",   STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->iStatIntLost,    STAMTYPE_U32, "iStatIntLost",    STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->iStatIntLostOne, STAMTYPE_U32, "iStatIntLostOne", STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntIMS,     STAMTYPE_U32, "uStatIntIMS",     STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntSkip,    STAMTYPE_U32, "uStatIntSkip",    STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntLate,    STAMTYPE_U32, "uStatIntLate",    STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntMasked,  STAMTYPE_U32, "uStatIntMasked",  STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntEarly,   STAMTYPE_U32, "uStatIntEarly",   STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntRx,      STAMTYPE_U32, "uStatIntRx",      STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntTx,      STAMTYPE_U32, "uStatIntTx",      STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntICS,     STAMTYPE_U32, "uStatIntICS",     STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntRDTR,    STAMTYPE_U32, "uStatIntRDTR",    STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntRXDMT0,  STAMTYPE_U32, "uStatIntRXDMT0",  STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatIntTXQE,    STAMTYPE_U32, "uStatIntTXQE",    STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTxNoRS,     STAMTYPE_U32, "uStatTxNoRS",     STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTxIDE,      STAMTYPE_U32, "uStatTxIDE",      STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTxDelayed,  STAMTYPE_U32, "uStatTxDelayed",  STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTxDelayExp, STAMTYPE_U32, "uStatTxDelayExp", STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTAD,        STAMTYPE_U32, "uStatTAD",        STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTID,        STAMTYPE_U32, "uStatTID",        STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatRAD,        STAMTYPE_U32, "uStatRAD",        STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatRID,        STAMTYPE_U32, "uStatRID",        STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatRxFrm,      STAMTYPE_U32, "uStatRxFrm",      STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTxFrm,      STAMTYPE_U32, "uStatTxFrm",      STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatDescCtx,    STAMTYPE_U32, "uStatDescCtx",    STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatDescDat,    STAMTYPE_U32, "uStatDescDat",    STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatDescLeg,    STAMTYPE_U32, "uStatDescLeg",    STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTx1514,     STAMTYPE_U32, "uStatTx1514",     STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTx2962,     STAMTYPE_U32, "uStatTx2962",     STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTx4410,     STAMTYPE_U32, "uStatTx4410",     STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTx5858,     STAMTYPE_U32, "uStatTx5858",     STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTx7306,     STAMTYPE_U32, "uStatTx7306",     STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTx8754,     STAMTYPE_U32, "uStatTx8754",     STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTx16384,    STAMTYPE_U32, "uStatTx16384",    STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTx32768,    STAMTYPE_U32, "uStatTx32768",    STAMUNIT_NS, NULL);
    PDMDevHlpSTAMRegister(pDevIns, &pThis->uStatTxLarge,    STAMTYPE_U32, "uStatTxLarge",    STAMUNIT_NS, NULL);
#endif /* E1K_INT_STATS */

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int)  e1kRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PE1KSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PE1KSTATE);
    PE1KSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PE1KSTATECC);

    /* Initialize context specific state data: */
    pThisCC->CTX_SUFF(pDevIns)      = pDevIns;
    /** @todo @bugref{9218} ring-0 driver stuff */
    pThisCC->CTX_SUFF(pDrv)         = NULL;
    pThisCC->CTX_SUFF(pTxSg)        = NULL;

    /* Configure critical sections the same way: */
    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /* Set up MMIO and I/O port callbacks for this context: */
    rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmioRegion, e1kMMIOWrite, e1kMMIORead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPorts, e1kIOPortOut, e1kIOPortIn, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceE1000 =
{
    /* .u32version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "e1000",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_NETWORK,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(E1KSTATE),
    /* .cbInstanceCC = */           sizeof(E1KSTATECC),
    /* .cbInstanceRC = */           sizeof(E1KSTATERC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Intel PRO/1000 MT Desktop Ethernet.",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           e1kR3Construct,
    /* .pfnDestruct = */            e1kR3Destruct,
    /* .pfnRelocate = */            e1kR3Relocate,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               e1kR3Reset,
    /* .pfnSuspend = */             e1kR3Suspend,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              e1kR3Attach,
    /* .pfnDeatch = */              e1kR3Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            e1kR3PowerOff,
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
    /* .pfnConstruct = */           e1kRZConstruct,
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
    /* .pfnConstruct = */           e1kRZConstruct,
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
