/* $Id: DevACPI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevACPI - Advanced Configuration and Power Interface (ACPI) Device.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_ACPI
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/dbgftrace.h>
#include <VBox/vmm/vmcpuset.h>
#include <VBox/AssertGuest.h>
#include <VBox/log.h>
#include <VBox/param.h>
#include <VBox/pci.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/asm-math.h>
#include <iprt/file.h>
#ifdef IN_RING3
# include <iprt/alloc.h>
# include <iprt/string.h>
# include <iprt/uuid.h>
#endif /* IN_RING3 */
#ifdef VBOX_WITH_IOMMU_AMD
# include <VBox/iommu-amd.h>
#endif
#ifdef VBOX_WITH_IOMMU_INTEL
# include <VBox/iommu-intel.h>
#endif

#include "VBoxDD.h"
#ifdef VBOX_WITH_IOMMU_AMD
# include "../Bus/DevIommuAmd.h"
#endif
#ifdef VBOX_WITH_IOMMU_INTEL
# include "../Bus/DevIommuIntel.h"
#endif

#ifdef LOG_ENABLED
# define DEBUG_ACPI
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef IN_RING3
/** Locks the device state, ring-3 only.  */
# define DEVACPI_LOCK_R3(a_pDevIns, a_pThis) \
    do { \
        int rcLock = PDMDevHlpCritSectEnter((a_pDevIns), &(a_pThis)->CritSect, VERR_IGNORED); \
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV((a_pDevIns), &(a_pThis)->CritSect, rcLock); \
    } while (0)
#endif
/** Unlocks the device state (all contexts). */
#define DEVACPI_UNLOCK(a_pDevIns, a_pThis) \
    do { PDMDevHlpCritSectLeave((a_pDevIns), &(a_pThis)->CritSect); } while (0)


#define DEBUG_HEX       0x3000
#define DEBUG_CHR       0x3001

/** PM Base Address PCI config space offset */
#define PMBA            0x40
/** PM Miscellaneous Power Management PCI config space offset */
#define PMREGMISC       0x80

#define PM_TMR_FREQ     3579545
/** Default base for PM PIIX4 device */
#define PM_PORT_BASE    0x4000
/* Port offsets in PM device */
enum
{
    PM1a_EVT_OFFSET                     = 0x00,
    PM1b_EVT_OFFSET                     =   -1,   /**<  not supported  */
    PM1a_CTL_OFFSET                     = 0x04,
    PM1b_CTL_OFFSET                     =   -1,   /**<  not supported  */
    PM2_CTL_OFFSET                      =   -1,   /**<  not supported  */
    PM_TMR_OFFSET                       = 0x08,
    GPE0_OFFSET                         = 0x20,
    GPE1_OFFSET                         =   -1    /**<  not supported  */
};

/* Maximum supported number of custom ACPI tables */
#define MAX_CUST_TABLES 4

/* Undef this to enable 24 bit PM timer (mostly for debugging purposes) */
#define PM_TMR_32BIT

#define BAT_INDEX       0x00004040
#define BAT_DATA        0x00004044
#define SYSI_INDEX      0x00004048
#define SYSI_DATA       0x0000404c
#define ACPI_RESET_BLK  0x00004050

/* PM1x status register bits */
#define TMR_STS         RT_BIT(0)
#define RSR1_STS        (RT_BIT(1) | RT_BIT(2) | RT_BIT(3))
#define BM_STS          RT_BIT(4)
#define GBL_STS         RT_BIT(5)
#define RSR2_STS        (RT_BIT(6) | RT_BIT(7))
#define PWRBTN_STS      RT_BIT(8)
#define SLPBTN_STS      RT_BIT(9)
#define RTC_STS         RT_BIT(10)
#define IGN_STS         RT_BIT(11)
#define RSR3_STS        (RT_BIT(12) | RT_BIT(13) | RT_BIT(14))
#define WAK_STS         RT_BIT(15)
#define RSR_STS         (RSR1_STS | RSR2_STS | RSR3_STS)

/* PM1x enable register bits */
#define TMR_EN          RT_BIT(0)
#define RSR1_EN         (RT_BIT(1) | RT_BIT(2) | RT_BIT(3) | RT_BIT(4))
#define GBL_EN          RT_BIT(5)
#define RSR2_EN         (RT_BIT(6) | RT_BIT(7))
#define PWRBTN_EN       RT_BIT(8)
#define SLPBTN_EN       RT_BIT(9)
#define RTC_EN          RT_BIT(10)
#define RSR3_EN         (RT_BIT(11) | RT_BIT(12) | RT_BIT(13) | RT_BIT(14) | RT_BIT(15))
#define RSR_EN          (RSR1_EN | RSR2_EN | RSR3_EN)
#define IGN_EN          0

/* PM1x control register bits */
#define SCI_EN          RT_BIT(0)
#define BM_RLD          RT_BIT(1)
#define GBL_RLS         RT_BIT(2)
#define RSR1_CNT        (RT_BIT(3) | RT_BIT(4) | RT_BIT(5) | RT_BIT(6) | RT_BIT(7) | RT_BIT(8))
#define IGN_CNT         RT_BIT(9)
#define SLP_TYPx_SHIFT  10
#define SLP_TYPx_MASK    7
#define SLP_EN          RT_BIT(13)
#define RSR2_CNT        (RT_BIT(14) | RT_BIT(15))
#define RSR_CNT         (RSR1_CNT | RSR2_CNT)

#define GPE0_BATTERY_INFO_CHANGED RT_BIT(0)

enum
{
    BAT_STATUS_STATE                    = 0x00, /**< BST battery state */
    BAT_STATUS_PRESENT_RATE             = 0x01, /**< BST battery present rate */
    BAT_STATUS_REMAINING_CAPACITY       = 0x02, /**< BST battery remaining capacity */
    BAT_STATUS_PRESENT_VOLTAGE          = 0x03, /**< BST battery present voltage */
    BAT_INFO_UNITS                      = 0x04, /**< BIF power unit */
    BAT_INFO_DESIGN_CAPACITY            = 0x05, /**< BIF design capacity */
    BAT_INFO_LAST_FULL_CHARGE_CAPACITY  = 0x06, /**< BIF last full charge capacity */
    BAT_INFO_TECHNOLOGY                 = 0x07, /**< BIF battery technology */
    BAT_INFO_DESIGN_VOLTAGE             = 0x08, /**< BIF design voltage */
    BAT_INFO_DESIGN_CAPACITY_OF_WARNING = 0x09, /**< BIF design capacity of warning */
    BAT_INFO_DESIGN_CAPACITY_OF_LOW     = 0x0A, /**< BIF design capacity of low */
    BAT_INFO_CAPACITY_GRANULARITY_1     = 0x0B, /**< BIF battery capacity granularity 1 */
    BAT_INFO_CAPACITY_GRANULARITY_2     = 0x0C, /**< BIF battery capacity granularity 2 */
    BAT_DEVICE_STATUS                   = 0x0D, /**< STA device status */
    BAT_POWER_SOURCE                    = 0x0E, /**< PSR power source */
    BAT_INDEX_LAST
};

enum
{
    CPU_EVENT_TYPE_ADD                  = 0x01, /**< Event type add */
    CPU_EVENT_TYPE_REMOVE               = 0x03  /**< Event type remove */
};

enum
{
    SYSTEM_INFO_INDEX_LOW_MEMORY_LENGTH = 0,
    SYSTEM_INFO_INDEX_USE_IOAPIC        = 1,
    SYSTEM_INFO_INDEX_HPET_STATUS       = 2,
    SYSTEM_INFO_INDEX_SMC_STATUS        = 3,
    SYSTEM_INFO_INDEX_FDC_STATUS        = 4,
    SYSTEM_INFO_INDEX_SERIAL2_IOBASE    = 5,
    SYSTEM_INFO_INDEX_SERIAL2_IRQ       = 6,
    SYSTEM_INFO_INDEX_SERIAL3_IOBASE    = 7,
    SYSTEM_INFO_INDEX_SERIAL3_IRQ       = 8,
    SYSTEM_INFO_INDEX_PREF64_MEMORY_MIN = 9,
    SYSTEM_INFO_INDEX_RTC_STATUS        = 10,
    SYSTEM_INFO_INDEX_CPU_LOCKED        = 11, /**< Contains a flag indicating whether the CPU is locked or not */
    SYSTEM_INFO_INDEX_CPU_LOCK_CHECK    = 12, /**< For which CPU the lock status should be checked */
    SYSTEM_INFO_INDEX_CPU_EVENT_TYPE    = 13, /**< Type of the CPU hot-plug event */
    SYSTEM_INFO_INDEX_CPU_EVENT         = 14, /**< The CPU id the event is for */
    SYSTEM_INFO_INDEX_NIC_ADDRESS       = 15, /**< NIC PCI address, or 0 */
    SYSTEM_INFO_INDEX_AUDIO_ADDRESS     = 16, /**< Audio card PCI address, or 0 */
    SYSTEM_INFO_INDEX_POWER_STATES      = 17,
    SYSTEM_INFO_INDEX_IOC_ADDRESS       = 18, /**< IO controller PCI address */
    SYSTEM_INFO_INDEX_HBC_ADDRESS       = 19, /**< host bus controller PCI address */
    SYSTEM_INFO_INDEX_PCI_BASE          = 20, /**< PCI bus MCFG MMIO range base */
    SYSTEM_INFO_INDEX_PCI_LENGTH        = 21, /**< PCI bus MCFG MMIO range length */
    SYSTEM_INFO_INDEX_SERIAL0_IOBASE    = 22,
    SYSTEM_INFO_INDEX_SERIAL0_IRQ       = 23,
    SYSTEM_INFO_INDEX_SERIAL1_IOBASE    = 24,
    SYSTEM_INFO_INDEX_SERIAL1_IRQ       = 25,
    SYSTEM_INFO_INDEX_PARALLEL0_IOBASE  = 26,
    SYSTEM_INFO_INDEX_PARALLEL0_IRQ     = 27,
    SYSTEM_INFO_INDEX_PARALLEL1_IOBASE  = 28,
    SYSTEM_INFO_INDEX_PARALLEL1_IRQ     = 29,
    SYSTEM_INFO_INDEX_PREF64_MEMORY_MAX = 30,
    SYSTEM_INFO_INDEX_NVME_ADDRESS      = 31, /**< First NVMe controller PCI address, or 0 */
    SYSTEM_INFO_INDEX_IOMMU_ADDRESS     = 32, /**< IOMMU PCI address, or 0 */
    SYSTEM_INFO_INDEX_SB_IOAPIC_ADDRESS = 33, /**< Southbridge I/O APIC (needed by AMD IOMMU) PCI address, or 0 */
    SYSTEM_INFO_INDEX_END               = 34,
    SYSTEM_INFO_INDEX_INVALID           = 0x80,
    SYSTEM_INFO_INDEX_VALID             = 0x200
};

#define AC_OFFLINE                              0
#define AC_ONLINE                               1

#define BAT_TECH_PRIMARY                        1
#define BAT_TECH_SECONDARY                      2

#define STA_DEVICE_PRESENT_MASK                 RT_BIT(0) /**< present */
#define STA_DEVICE_ENABLED_MASK                 RT_BIT(1) /**< enabled and decodes its resources */
#define STA_DEVICE_SHOW_IN_UI_MASK              RT_BIT(2) /**< should be shown in UI */
#define STA_DEVICE_FUNCTIONING_PROPERLY_MASK    RT_BIT(3) /**< functioning properly */
#define STA_BATTERY_PRESENT_MASK                RT_BIT(4) /**< the battery is present */

/** SMBus Base Address PCI config space offset */
#define SMBBA           0x90
/** SMBus Host Configuration PCI config space offset */
#define SMBHSTCFG       0xd2
/** SMBus Slave Command PCI config space offset */
#define SMBSLVC         0xd3
/** SMBus Slave Shadow Port 1 PCI config space offset */
#define SMBSHDW1        0xd4
/** SMBus Slave Shadow Port 2 PCI config space offset */
#define SMBSHDW2        0xd5
/** SMBus Revision Identification PCI config space offset */
#define SMBREV          0xd6

#define SMBHSTCFG_SMB_HST_EN    RT_BIT(0)
#define SMBHSTCFG_INTRSEL       (RT_BIT(1) | RT_BIT(2) | RT_BIT(3))
#define SMBHSTCFG_INTRSEL_SMI   0
#define SMBHSTCFG_INTRSEL_IRQ9  4
#define SMBHSTCFG_INTRSEL_SHIFT 1

/** Default base for SMBus PIIX4 device */
#define SMB_PORT_BASE   0x4100

/** SMBus Host Status Register I/O offset */
#define SMBHSTSTS_OFF   0x0000
/** SMBus Slave Status Register I/O offset */
#define SMBSLVSTS_OFF   0x0001
/** SMBus Host Count Register I/O offset */
#define SMBHSTCNT_OFF   0x0002
/** SMBus Host Command Register I/O offset */
#define SMBHSTCMD_OFF   0x0003
/** SMBus Host Address Register I/O offset */
#define SMBHSTADD_OFF   0x0004
/** SMBus Host Data 0 Register I/O offset */
#define SMBHSTDAT0_OFF  0x0005
/** SMBus Host Data 1 Register I/O offset */
#define SMBHSTDAT1_OFF  0x0006
/** SMBus Block Data Register I/O offset */
#define SMBBLKDAT_OFF   0x0007
/** SMBus Slave Control Register I/O offset */
#define SMBSLVCNT_OFF   0x0008
/** SMBus Shadow Command Register I/O offset */
#define SMBSHDWCMD_OFF  0x0009
/** SMBus Slave Event Register I/O offset */
#define SMBSLVEVT_OFF   0x000a
/** SMBus Slave Data Register I/O offset */
#define SMBSLVDAT_OFF   0x000c

#define SMBHSTSTS_HOST_BUSY RT_BIT(0)
#define SMBHSTSTS_INTER     RT_BIT(1)
#define SMBHSTSTS_DEV_ERR   RT_BIT(2)
#define SMBHSTSTS_BUS_ERR   RT_BIT(3)
#define SMBHSTSTS_FAILED    RT_BIT(4)
#define SMBHSTSTS_INT_MASK  (SMBHSTSTS_INTER | SMBHSTSTS_DEV_ERR | SMBHSTSTS_BUS_ERR | SMBHSTSTS_FAILED)

#define SMBSLVSTS_WRITE_MASK 0x3c

#define SMBHSTCNT_INTEREN   RT_BIT(0)
#define SMBHSTCNT_KILL      RT_BIT(1)
#define SMBHSTCNT_CMD_PROT  (RT_BIT(2) | RT_BIT(3) | RT_BIT(4))
#define SMBHSTCNT_START     RT_BIT(6)
#define SMBHSTCNT_WRITE_MASK (SMBHSTCNT_INTEREN | SMBHSTCNT_KILL | SMBHSTCNT_CMD_PROT)

#define SMBSLVCNT_WRITE_MASK (RT_BIT(0) | RT_BIT(1) | RT_BIT(2) | RT_BIT(3))


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The TPM mode configured.
 */
typedef enum ACPITPMMODE
{
    ACPITPMMODE_INVALID = 0,
    ACPITPMMODE_DISABLED,
    ACPITPMMODE_TIS_1_2,
    ACPITPMMODE_CRB_2_0,
    ACPITPMMODE_FIFO_2_0,
    ACPITPMMODE_32BIT_HACK = 0x7fffffff
} ACPITPMMODE;


/**
 * The shared ACPI device state.
 */
typedef struct ACPISTATE
{
    /** Critical section protecting the ACPI state. */
    PDMCRITSECT         CritSect;

    uint16_t            pm1a_en;
    uint16_t            pm1a_sts;
    uint16_t            pm1a_ctl;
    /** Number of logical CPUs in guest */
    uint16_t            cCpus;

    uint64_t            u64PmTimerInitial;
    /** The PM timer. */
    TMTIMERHANDLE       hPmTimer;
    /* PM Timer last calculated value */
    uint32_t            uPmTimerVal;
    uint32_t            Alignment0;

    uint32_t            gpe0_en;
    uint32_t            gpe0_sts;

    uint32_t            uBatteryIndex;
    uint32_t            au8BatteryInfo[13];

    uint32_t            uSystemInfoIndex;
    uint32_t            u32Alignment0;
    uint64_t            u64RamSize;
    /** Offset of the 64-bit prefetchable memory window. */
    uint64_t            u64PciPref64Min;
    /** Limit of the 64-bit prefetchable memory window. */
    uint64_t            u64PciPref64Max;
    /** The number of bytes below 4GB. */
    uint32_t            cbRamLow;

    /** Current ACPI S* state. We support S0 and S5. */
    uint32_t            uSleepState;
    uint8_t             au8RSDPPage[0x1000];
    /** This is a workaround for incorrect index field handling by Intels ACPICA.
     *  The system info _INI method writes to offset 0x200. We either observe a
     *  write request to index 0x80 (in that case we don't change the index) or a
     *  write request to offset 0x200 (in that case we divide the index value by
     *  4. Note that the _STA method is sometimes called prior to the _INI method
     *  (ACPI spec 6.3.7, _STA). See the special case for BAT_DEVICE_STATUS in
     *  acpiR3BatIndexWrite() for handling this. */
    uint8_t             u8IndexShift;
    /** provide an I/O-APIC */
    uint8_t             u8UseIOApic;
    /** provide a floppy controller */
    bool                fUseFdc;
    /** If High Precision Event Timer device should be supported */
    bool                fUseHpet;
    /** If System Management Controller device should be supported */
    bool                fUseSmc;
    /** the guest handled the last power button event */
    bool                fPowerButtonHandled;
    /** If ACPI CPU device should be shown */
    bool                fShowCpu;
    /** If Real Time Clock ACPI object to be shown */
    bool                fShowRtc;
    /** I/O port address of PM device. */
    RTIOPORT            uPmIoPortBase;
    /** I/O port address of SMBus device. */
    RTIOPORT            uSMBusIoPortBase;
    /** Which CPU to check for the locked status. */
    uint32_t            idCpuLockCheck;
    /** Array of flags of attached CPUs */
    VMCPUSET            CpuSetAttached;
    /** Mask of locked CPUs (used by the guest). */
    VMCPUSET            CpuSetLocked;
    /** The CPU event type. */
    uint32_t            u32CpuEventType;
    /** The CPU id affected. */
    uint32_t            u32CpuEvent;
    /** Flag whether CPU hot plugging is enabled. */
    bool                fCpuHotPlug;
    /** If MCFG ACPI table shown to the guest */
    bool                fUseMcfg;
    /** if the 64-bit prefetchable memory window is shown to the guest */
    bool                fPciPref64Enabled;
    /** If the IOMMU (AMD) device should be enabled */
    bool                fUseIommuAmd;
    /** If the IOMMU (Intel) device should be enabled */
    bool                fUseIommuIntel;
    /** Padding. */
    bool                afPadding0[3];
    /** Primary NIC PCI address. */
    uint32_t            u32NicPciAddress;
    /** HD Audio PCI address. */
    uint32_t            u32AudioPciAddress;
    /** Primary NVMe controller PCI address. */
    uint32_t            u32NvmePciAddress;
    /** Flag whether S1 power state is enabled. */
    bool                fS1Enabled;
    /** Flag whether S4 power state is enabled. */
    bool                fS4Enabled;
    /** Flag whether S1 triggers a state save. */
    bool                fSuspendToSavedState;
    /** Flag whether to set WAK_STS on resume (restore included). */
    bool                fSetWakeupOnResume;
    /** PCI address of the IO controller device. */
    uint32_t            u32IocPciAddress;
    /** PCI address of the host bus controller device. */
    uint32_t            u32HbcPciAddress;
    /** PCI address of the IOMMU device. */
    uint32_t            u32IommuPciAddress;
    /** PCI address of the southbridge I/O APIC device. */
    uint32_t            u32SbIoApicPciAddress;

    /** Physical address of PCI config space MMIO region */
    uint64_t            u64PciConfigMMioAddress;
    /** Length of PCI config space MMIO region */
    uint64_t            u64PciConfigMMioLength;
    /** Serial 0 IRQ number */
    uint8_t             uSerial0Irq;
    /** Serial 1 IRQ number */
    uint8_t             uSerial1Irq;
    /** Serial 2 IRQ number */
    uint8_t             uSerial2Irq;
    /** Serial 3 IRQ number */
    uint8_t             uSerial3Irq;
    /** Serial 0 IO port base */
    RTIOPORT            uSerial0IoPortBase;
    /** Serial 1 IO port base */
    RTIOPORT            uSerial1IoPortBase;
    /** Serial 2 IO port base */
    RTIOPORT            uSerial2IoPortBase;
    /** Serial 3 IO port base */
    RTIOPORT            uSerial3IoPortBase;

    /** @name Parallel port config bits
     * @{ */
    /** Parallel 0 IO port base */
    RTIOPORT            uParallel0IoPortBase;
    /** Parallel 1 IO port base */
    RTIOPORT            uParallel1IoPortBase;
    /** Parallel 0 IRQ number */
    uint8_t             uParallel0Irq;
    /** Parallel 1 IRQ number */
    uint8_t             uParallel1Irq;
    /** @} */

#ifdef VBOX_WITH_TPM
    /** @name TPM config bits
     * @{ */
    /** The ACPI TPM mode configured. */
    ACPITPMMODE         enmTpmMode;
    /** The MMIO register area base address. */
    RTGCPHYS            GCPhysTpmMmio;
    /** @} */
#endif

    /** Number of custom ACPI tables */
    uint8_t             cCustTbls;
    /** ACPI OEM ID */
    uint8_t             au8OemId[6];
    /** ACPI Crator ID */
    uint8_t             au8CreatorId[4];
    uint8_t             abAlignment2[3];
    /** ACPI Crator Rev */
    uint32_t            u32CreatorRev;
    /** ACPI custom OEM Tab ID */
    uint8_t             au8OemTabId[8];
    /** ACPI custom OEM Rev */
    uint32_t            u32OemRevision;

    /** SMBus Host Status Register */
    uint8_t             u8SMBusHstSts;
    /** SMBus Slave Status Register */
    uint8_t             u8SMBusSlvSts;
    /** SMBus Host Control Register */
    uint8_t             u8SMBusHstCnt;
    /** SMBus Host Command Register */
    uint8_t             u8SMBusHstCmd;
    /** SMBus Host Address Register */
    uint8_t             u8SMBusHstAdd;
    /** SMBus Host Data 0 Register */
    uint8_t             u8SMBusHstDat0;
    /** SMBus Host Data 1 Register */
    uint8_t             u8SMBusHstDat1;
    /** SMBus Slave Control Register */
    uint8_t             u8SMBusSlvCnt;
    /** SMBus Slave Event Register */
    uint16_t            u16SMBusSlvEvt;
    /** SMBus Slave Data Register */
    uint16_t            u16SMBusSlvDat;
    /** SMBus Shadow Command Register */
    uint8_t             u8SMBusShdwCmd;
    /** SMBus Host Block Index */
    uint8_t             u8SMBusBlkIdx;
    uint8_t             abAlignment3[2];
    /** SMBus Host Block Data Buffer */
    uint8_t             au8SMBusBlkDat[32];

    /** @todo DEBUGGING */
    uint32_t            uPmTimeOld;
    uint32_t            uPmTimeA;
    uint32_t            uPmTimeB;
    uint32_t            Alignment5;

    /** @name PM1a, PM timer and GPE0 I/O ports - mapped/unmapped as a group.
     *  @{ */
    IOMIOPORTHANDLE     hIoPortPm1aEn;
    IOMIOPORTHANDLE     hIoPortPm1aSts;
    IOMIOPORTHANDLE     hIoPortPm1aCtl;
    IOMIOPORTHANDLE     hIoPortPmTimer;
    IOMIOPORTHANDLE     hIoPortGpe0En;
    IOMIOPORTHANDLE     hIoPortGpe0Sts;
    /** @} */

    /** SMBus I/O ports (mapped/unmapped). */
    IOMIOPORTHANDLE     hIoPortSMBus;

    /** @name Fixed I/O ports
     * @{ */
    /** ACPI SMI I/O port. */
    IOMIOPORTHANDLE     hIoPortSmi;
    /** ACPI Debug hex I/O port. */
    IOMIOPORTHANDLE     hIoPortDebugHex;
    /** ACPI Debug char I/O port. */
    IOMIOPORTHANDLE     hIoPortDebugChar;
    /** ACPI Battery status index I/O port. */
    IOMIOPORTHANDLE     hIoPortBatteryIndex;
    /** ACPI Battery status data I/O port. */
    IOMIOPORTHANDLE     hIoPortBatteryData;
    /** ACPI system info index I/O port. */
    IOMIOPORTHANDLE     hIoPortSysInfoIndex;
    /** ACPI system info data I/O port. */
    IOMIOPORTHANDLE     hIoPortSysInfoData;
    /** ACPI Reset I/O port. */
    IOMIOPORTHANDLE     hIoPortReset;
    /** @} */

} ACPISTATE;
/** Pointer to the shared ACPI device state. */
typedef ACPISTATE *PACPISTATE;



/**
 * The ring-3 ACPI device state.
 */
typedef struct ACPISTATER3
{
    /** ACPI port base interface. */
    PDMIBASE            IBase;
    /** ACPI port interface. */
    PDMIACPIPORT        IACPIPort;
    /** The button event interface for power button events. */
    PDMIEVENTBUTTONPORT IButtonEventPort;
    /** Pointer to the device instance so we can get our bearings from
     *  interface functions. */
    PPDMDEVINSR3        pDevIns;

    /** Pointer to the driver base interface. */
    R3PTRTYPE(PPDMIBASE) pDrvBase;
    /** Pointer to the driver connector interface. */
    R3PTRTYPE(PPDMIACPICONNECTOR) pDrv;

    /** Custom ACPI tables binary data. */
    R3PTRTYPE(uint8_t *) apu8CustBin[MAX_CUST_TABLES];
    /** The size of the custom table binary. */
    uint64_t            acbCustBin[MAX_CUST_TABLES];
} ACPISTATER3;
/** Pointer to the ring-3 ACPI device state. */
typedef ACPISTATER3 *PACPISTATER3;


#pragma pack(1)

/** Generic Address Structure (see ACPIspec 3.0, 5.2.3.1) */
struct ACPIGENADDR
{
    uint8_t             u8AddressSpaceId;       /**< 0=sys, 1=IO, 2=PCICfg, 3=emb, 4=SMBus */
    uint8_t             u8RegisterBitWidth;     /**< size in bits of the given register */
    uint8_t             u8RegisterBitOffset;    /**< bit offset of register */
    uint8_t             u8AccessSize;           /**< 1=byte, 2=word, 3=dword, 4=qword */
    uint64_t            u64Address;             /**< 64-bit address of register */
};
AssertCompileSize(ACPIGENADDR, 12);

/** Root System Description Pointer */
struct ACPITBLRSDP
{
    uint8_t             au8Signature[8];        /**< 'RSD PTR ' */
    uint8_t             u8Checksum;             /**< checksum for the first 20 bytes */
    uint8_t             au8OemId[6];            /**< OEM-supplied identifier */
    uint8_t             u8Revision;             /**< revision number, currently 2 */
#define ACPI_REVISION   2                       /**< ACPI 3.0 */
    uint32_t            u32RSDT;                /**< phys addr of RSDT */
    uint32_t            u32Length;              /**< bytes of this table */
    uint64_t            u64XSDT;                /**< 64-bit phys addr of XSDT */
    uint8_t             u8ExtChecksum;          /**< checksum of entire table */
    uint8_t             u8Reserved[3];          /**< reserved */
};
AssertCompileSize(ACPITBLRSDP, 36);

/** System Description Table Header */
struct ACPITBLHEADER
{
    uint8_t             au8Signature[4];        /**< table identifier */
    uint32_t            u32Length;              /**< length of the table including header */
    uint8_t             u8Revision;             /**< revision number */
    uint8_t             u8Checksum;             /**< all fields inclusive this add to zero */
    uint8_t             au8OemId[6];            /**< OEM-supplied string */
    uint8_t             au8OemTabId[8];         /**< to identify the particular data table */
    uint32_t            u32OemRevision;         /**< OEM-supplied revision number */
    uint8_t             au8CreatorId[4];        /**< ID for the ASL compiler */
    uint32_t            u32CreatorRev;          /**< revision for the ASL compiler */
};
AssertCompileSize(ACPITBLHEADER, 36);

/** Root System Description Table */
struct ACPITBLRSDT
{
    ACPITBLHEADER       header;
    uint32_t            u32Entry[1];            /**< array of phys. addresses to other tables */
};
AssertCompileSize(ACPITBLRSDT, 40);

/** Extended System Description Table */
struct ACPITBLXSDT
{
    ACPITBLHEADER       header;
    uint64_t            u64Entry[1];            /**< array of phys. addresses to other tables */
};
AssertCompileSize(ACPITBLXSDT, 44);

/** Fixed ACPI Description Table */
struct ACPITBLFADT
{
    ACPITBLHEADER       header;
    uint32_t            u32FACS;                /**< phys. address of FACS */
    uint32_t            u32DSDT;                /**< phys. address of DSDT */
    uint8_t             u8IntModel;             /**< was eleminated in ACPI 2.0 */
#define INT_MODEL_DUAL_PIC        1             /**< for ACPI 2+ */
#define INT_MODEL_MULTIPLE_APIC   2
    uint8_t             u8PreferredPMProfile;   /**< preferred power management profile */
    uint16_t            u16SCIInt;              /**< system vector the SCI is wired in 8259 mode */
#define SCI_INT         9
    uint32_t            u32SMICmd;              /**< system port address of SMI command port */
#define SMI_CMD         0x0000442e
    uint8_t             u8AcpiEnable;           /**< SMICmd val to disable ownership of ACPIregs */
#define ACPI_ENABLE     0xa1
    uint8_t             u8AcpiDisable;          /**< SMICmd val to re-enable ownership of ACPIregs */
#define ACPI_DISABLE    0xa0
    uint8_t             u8S4BIOSReq;            /**< SMICmd val to enter S4BIOS state */
    uint8_t             u8PStateCnt;            /**< SMICmd val to assume processor performance
                                                     state control responsibility */
    uint32_t            u32PM1aEVTBLK;          /**< port addr of PM1a event regs block */
    uint32_t            u32PM1bEVTBLK;          /**< port addr of PM1b event regs block */
    uint32_t            u32PM1aCTLBLK;          /**< port addr of PM1a control regs block */
    uint32_t            u32PM1bCTLBLK;          /**< port addr of PM1b control regs block */
    uint32_t            u32PM2CTLBLK;           /**< port addr of PM2 control regs block */
    uint32_t            u32PMTMRBLK;            /**< port addr of PMTMR regs block */
    uint32_t            u32GPE0BLK;             /**< port addr of gen-purp event 0 regs block */
    uint32_t            u32GPE1BLK;             /**< port addr of gen-purp event 1 regs block */
    uint8_t             u8PM1EVTLEN;            /**< bytes decoded by PM1a_EVT_BLK. >= 4 */
    uint8_t             u8PM1CTLLEN;            /**< bytes decoded by PM1b_CNT_BLK. >= 2 */
    uint8_t             u8PM2CTLLEN;            /**< bytes decoded by PM2_CNT_BLK. >= 1 or 0 */
    uint8_t             u8PMTMLEN;              /**< bytes decoded by PM_TMR_BLK. ==4 */
    uint8_t             u8GPE0BLKLEN;           /**< bytes decoded by GPE0_BLK. %2==0 */
#define GPE0_BLK_LEN    2
    uint8_t             u8GPE1BLKLEN;           /**< bytes decoded by GPE1_BLK. %2==0 */
#define GPE1_BLK_LEN    0
    uint8_t             u8GPE1BASE;             /**< offset of GPE1 based events */
#define GPE1_BASE       0
    uint8_t             u8CSTCNT;               /**< SMICmd val to indicate OS supp for C states */
    uint16_t            u16PLVL2LAT;            /**< us to enter/exit C2. >100 => unsupported */
#define P_LVL2_LAT      101                     /**< C2 state not supported */
    uint16_t            u16PLVL3LAT;            /**< us to enter/exit C3. >1000 => unsupported */
#define P_LVL3_LAT      1001                    /**< C3 state not supported */
    uint16_t            u16FlushSize;           /**< # of flush strides to read to flush dirty
                                                     lines from any processors memory caches */
#define FLUSH_SIZE      0                       /**< Ignored if WBVIND set in FADT_FLAGS */
    uint16_t            u16FlushStride;         /**< cache line width */
#define FLUSH_STRIDE    0                       /**< Ignored if WBVIND set in FADT_FLAGS */
    uint8_t             u8DutyOffset;
    uint8_t             u8DutyWidth;
    uint8_t             u8DayAlarm;             /**< RTC CMOS RAM index of day-of-month alarm */
    uint8_t             u8MonAlarm;             /**< RTC CMOS RAM index of month-of-year alarm */
    uint8_t             u8Century;              /**< RTC CMOS RAM index of century */
    uint16_t            u16IAPCBOOTARCH;        /**< IA-PC boot architecture flags */
#define IAPC_BOOT_ARCH_LEGACY_DEV       RT_BIT(0)  /**< legacy devices present such as LPT
                                                     (COM too?) */
#define IAPC_BOOT_ARCH_8042             RT_BIT(1)  /**< legacy keyboard device present */
#define IAPC_BOOT_ARCH_NO_VGA           RT_BIT(2)  /**< VGA not present */
#define IAPC_BOOT_ARCH_NO_MSI           RT_BIT(3)  /**< OSPM must not enable MSIs on this platform */
#define IAPC_BOOT_ARCH_NO_ASPM          RT_BIT(4)  /**< OSPM must not enable ASPM on this platform */
    uint8_t             u8Must0_0;                 /**< must be 0 */
    uint32_t            u32Flags;                  /**< fixed feature flags */
#define FADT_FL_WBINVD                  RT_BIT(0)  /**< emulation of WBINVD available */
#define FADT_FL_WBINVD_FLUSH            RT_BIT(1)
#define FADT_FL_PROC_C1                 RT_BIT(2)  /**< 1=C1 supported on all processors */
#define FADT_FL_P_LVL2_UP               RT_BIT(3)  /**< 1=C2 works on SMP and UNI systems */
#define FADT_FL_PWR_BUTTON              RT_BIT(4)  /**< 1=power button handled as ctrl method dev */
#define FADT_FL_SLP_BUTTON              RT_BIT(5)  /**< 1=sleep button handled as ctrl method dev */
#define FADT_FL_FIX_RTC                 RT_BIT(6)  /**< 0=RTC wake status in fixed register */
#define FADT_FL_RTC_S4                  RT_BIT(7)  /**< 1=RTC can wake system from S4 */
#define FADT_FL_TMR_VAL_EXT             RT_BIT(8)  /**< 1=TMR_VAL implemented as 32 bit */
#define FADT_FL_DCK_CAP                 RT_BIT(9)  /**< 0=system cannot support docking */
#define FADT_FL_RESET_REG_SUP           RT_BIT(10) /**< 1=system supports system resets */
#define FADT_FL_SEALED_CASE             RT_BIT(11) /**< 1=case is sealed */
#define FADT_FL_HEADLESS                RT_BIT(12) /**< 1=system cannot detect moni/keyb/mouse */
#define FADT_FL_CPU_SW_SLP              RT_BIT(13)
#define FADT_FL_PCI_EXT_WAK             RT_BIT(14) /**< 1=system supports PCIEXP_WAKE_STS */
#define FADT_FL_USE_PLATFORM_CLOCK      RT_BIT(15) /**< 1=system has ACPI PM timer */
#define FADT_FL_S4_RTC_STS_VALID        RT_BIT(16) /**< 1=RTC_STS flag is valid when waking from S4 */
#define FADT_FL_REMOVE_POWER_ON_CAPABLE RT_BIT(17) /**< 1=platform can remote power on */
#define FADT_FL_FORCE_APIC_CLUSTER_MODEL  RT_BIT(18)
#define FADT_FL_FORCE_APIC_PHYS_DEST_MODE RT_BIT(19)

/* PM Timer mask and msb */
#ifndef PM_TMR_32BIT
#define TMR_VAL_MSB     0x800000
#define TMR_VAL_MASK    0xffffff
#undef  FADT_FL_TMR_VAL_EXT
#define FADT_FL_TMR_VAL_EXT     0
#else
#define TMR_VAL_MSB     0x80000000
#define TMR_VAL_MASK    0xffffffff
#endif

    /** Start of the ACPI 2.0 extension. */
    ACPIGENADDR         ResetReg;               /**< ext addr of reset register */
    uint8_t             u8ResetVal;             /**< ResetReg value to reset the system */
#define ACPI_RESET_REG_VAL  0x10
    uint8_t             au8Must0_1[3];          /**< must be 0 */
    uint64_t            u64XFACS;               /**< 64-bit phys address of FACS */
    uint64_t            u64XDSDT;               /**< 64-bit phys address of DSDT */
    ACPIGENADDR         X_PM1aEVTBLK;           /**< ext addr of PM1a event regs block */
    ACPIGENADDR         X_PM1bEVTBLK;           /**< ext addr of PM1b event regs block */
    ACPIGENADDR         X_PM1aCTLBLK;           /**< ext addr of PM1a control regs block */
    ACPIGENADDR         X_PM1bCTLBLK;           /**< ext addr of PM1b control regs block */
    ACPIGENADDR         X_PM2CTLBLK;            /**< ext addr of PM2 control regs block */
    ACPIGENADDR         X_PMTMRBLK;             /**< ext addr of PMTMR control regs block */
    ACPIGENADDR         X_GPE0BLK;              /**< ext addr of GPE1 regs block */
    ACPIGENADDR         X_GPE1BLK;              /**< ext addr of GPE1 regs block */
};
AssertCompileSize(ACPITBLFADT, 244);
#define ACPITBLFADT_VERSION1_SIZE               RT_OFFSETOF(ACPITBLFADT, ResetReg)

/** Firmware ACPI Control Structure */
struct ACPITBLFACS
{
    uint8_t             au8Signature[4];        /**< 'FACS' */
    uint32_t            u32Length;              /**< bytes of entire FACS structure >= 64 */
    uint32_t            u32HWSignature;         /**< systems HW signature at last boot */
    uint32_t            u32FWVector;            /**< address of waking vector */
    uint32_t            u32GlobalLock;          /**< global lock to sync HW/SW */
    uint32_t            u32Flags;               /**< FACS flags */
    uint64_t            u64X_FWVector;          /**< 64-bit waking vector */
    uint8_t             u8Version;              /**< version of this table */
    uint8_t             au8Reserved[31];        /**< zero */
};
AssertCompileSize(ACPITBLFACS, 64);

/** Processor Local APIC Structure */
struct ACPITBLLAPIC
{
    uint8_t             u8Type;                 /**< 0 = LAPIC */
    uint8_t             u8Length;               /**< 8 */
    uint8_t             u8ProcId;               /**< processor ID */
    uint8_t             u8ApicId;               /**< local APIC ID */
    uint32_t            u32Flags;               /**< Flags */
#define LAPIC_ENABLED   0x1
};
AssertCompileSize(ACPITBLLAPIC, 8);

/** I/O APIC Structure */
struct ACPITBLIOAPIC
{
    uint8_t             u8Type;                 /**< 1 == I/O APIC */
    uint8_t             u8Length;               /**< 12 */
    uint8_t             u8IOApicId;             /**< I/O APIC ID */
    uint8_t             u8Reserved;             /**< 0 */
    uint32_t            u32Address;             /**< phys address to access I/O APIC */
    uint32_t            u32GSIB;                /**< global system interrupt number to start */
};
AssertCompileSize(ACPITBLIOAPIC, 12);

/** Interrupt Source Override Structure */
struct ACPITBLISO
{
    uint8_t             u8Type;                 /**< 2 ==  Interrupt Source Override*/
    uint8_t             u8Length;               /**< 10 */
    uint8_t             u8Bus;                  /**< Bus */
    uint8_t             u8Source;               /**< Bus-relative interrupt source (IRQ) */
    uint32_t            u32GSI;                 /**< Global System Interrupt */
    uint16_t            u16Flags;               /**< MPS INTI flags Global */
};
AssertCompileSize(ACPITBLISO, 10);
#define NUMBER_OF_IRQ_SOURCE_OVERRIDES 2

/** HPET Descriptor Structure */
struct ACPITBLHPET
{
    ACPITBLHEADER aHeader;
    uint32_t      u32Id;                        /**< hardware ID of event timer block
                                                     [31:16] PCI vendor ID of first timer block
                                                     [15]    legacy replacement IRQ routing capable
                                                     [14]    reserved
                                                     [13]    COUNT_SIZE_CAP counter size
                                                     [12:8]  number of comparators in first timer block
                                                     [7:0]   hardware rev ID */
    ACPIGENADDR   HpetAddr;                     /**< lower 32-bit base address */
    uint8_t       u32Number;                    /**< sequence number starting at 0 */
    uint16_t      u32MinTick;                   /**< minimum clock ticks which can be set without
                                                     lost interrupts while the counter is programmed
                                                     to operate in periodic mode. Unit: clock tick. */
    uint8_t       u8Attributes;                 /**< page protection and OEM attribute. */
};
AssertCompileSize(ACPITBLHPET, 56);

#ifdef VBOX_WITH_IOMMU_AMD
/** AMD IOMMU: IVRS (I/O Virtualization Reporting Structure).
 *  In accordance with the AMD spec. */
typedef struct ACPIIVRS
{
    ACPITBLHEADER       header;
    uint32_t            u32IvInfo;  /**< IVInfo: I/O virtualization info. common to all IOMMUs in the system. */
    uint64_t            u64Rsvd;    /**< Reserved (MBZ). */
    /* IVHD type block follows. */
} ACPIIVRS;
AssertCompileSize(ACPIIVRS, 48);
AssertCompileMemberOffset(ACPIIVRS, u32IvInfo, 36);

/**
 * AMD IOMMU: The ACPI table.
 */
typedef struct ACPITBLIOMMU
{
    ACPIIVRS            Hdr;
    ACPIIVHDTYPE10      IvhdType10;
    ACPIIVHDDEVENTRY4   IvhdType10Start;
    ACPIIVHDDEVENTRY4   IvhdType10End;
    ACPIIVHDDEVENTRY4   IvhdType10Rsvd0;
    ACPIIVHDDEVENTRY4   IvhdType10Rsvd1;
    ACPIIVHDDEVENTRY8   IvhdType10IoApic;
    ACPIIVHDDEVENTRY8   IvhdType10Hpet;

    ACPIIVHDTYPE11      IvhdType11;
    ACPIIVHDDEVENTRY4   IvhdType11Start;
    ACPIIVHDDEVENTRY4   IvhdType11End;
    ACPIIVHDDEVENTRY4   IvhdType11Rsvd0;
    ACPIIVHDDEVENTRY4   IvhdType11Rsvd1;
    ACPIIVHDDEVENTRY8   IvhdType11IoApic;
    ACPIIVHDDEVENTRY8   IvhdType11Hpet;
} ACPITBLIOMMU;
AssertCompileMemberAlignment(ACPITBLIOMMU, IvhdType10Start, 4);
AssertCompileMemberAlignment(ACPITBLIOMMU, IvhdType10End, 4);
AssertCompileMemberAlignment(ACPITBLIOMMU, IvhdType11Start, 4);
AssertCompileMemberAlignment(ACPITBLIOMMU, IvhdType11End, 4);
#endif  /* VBOX_WITH_IOMMU_AMD */

#ifdef VBOX_WITH_IOMMU_INTEL
/** Intel IOMMU: DMAR (DMA Remapping) Reporting Structure.
 *  In accordance with the AMD spec. */
typedef struct ACPIDMAR
{
    ACPITBLHEADER       Hdr;
    /** Host-address Width (N+1 physical bits addressable). */
    uint8_t             uHostAddrWidth;
    /** Flags, see ACPI_DMAR_F_XXX. */
    uint8_t             fFlags;
    /** Reserved. */
    uint8_t             abRsvd[10];
    /* Remapping Structures[] follows. */
} ACPIDMAR;
AssertCompileSize(ACPIDMAR, 48);
AssertCompileMemberOffset(ACPIDMAR, uHostAddrWidth, 36);
AssertCompileMemberOffset(ACPIDMAR, fFlags, 37);

/**
 * Intel VT-d: The ACPI table.
 */
typedef struct ACPITBLVTD
{
    ACPIDMAR            Dmar;
    ACPIDRHD            Drhd;
    ACPIDMARDEVSCOPE    DevScopeIoApic;
} ACPITBLVTD;
#endif  /* VBOX_WITH_IOMMU_INTEL */

/** MCFG Descriptor Structure */
typedef struct ACPITBLMCFG
{
    ACPITBLHEADER aHeader;
    uint64_t      u64Reserved;
} ACPITBLMCFG;
AssertCompileSize(ACPITBLMCFG, 44);

/** Number of such entries can be computed from the whole table length in header */
typedef struct ACPITBLMCFGENTRY
{
    uint64_t      u64BaseAddress;
    uint16_t      u16PciSegmentGroup;
    uint8_t       u8StartBus;
    uint8_t       u8EndBus;
    uint32_t      u32Reserved;
} ACPITBLMCFGENTRY;
AssertCompileSize(ACPITBLMCFGENTRY, 16);

#define PCAT_COMPAT   0x1                       /**< system has also a dual-8259 setup */

/** Custom Description Table */
struct ACPITBLCUST
{
    ACPITBLHEADER       header;
    uint8_t             au8Data[476];
};
AssertCompileSize(ACPITBLCUST, 512);


#ifdef VBOX_WITH_TPM
/**
 * TPM: The ACPI table for a TPM 2.0 device
  * (from: https://trustedcomputinggroup.org/wp-content/uploads/TCG_ACPIGeneralSpec_v1p3_r8_pub.pdf).
 */
typedef struct ACPITBLTPM20
{
    /** The common ACPI table header. */
    ACPITBLHEADER       Hdr;
    /** The platform class. */
    uint16_t            u16PlatCls;
    /** Reserved. */
    uint16_t            u16Rsvd0;
    /** Address of the CRB control area or FIFO base address. */
    uint64_t            u64BaseAddrCrbOrFifo;
    /** The start method selector. */
    uint32_t            u32StartMethod;
    /** Following are start method specific parameters and optional LAML and LASA fields we don't implement right now. */
    /** @todo */
} ACPITBLTPM20;
AssertCompileSize(ACPITBLTPM20, 52);

/** Revision of the TPM2.0 ACPI table. */
#define ACPI_TPM20_REVISION                 4
/** The default MMIO base address of the TPM. */
#define ACPI_TPM_MMIO_BASE_DEFAULT          0xfed40000


/** @name Possible values for the ACPITBLTPM20::u16PlatCls member.
 * @{ */
/** Client platform. */
#define ACPITBL_TPM20_PLAT_CLS_CLIENT       UINT16_C(0)
/** Server platform. */
#define ACPITBL_TPM20_PLAT_CLS_SERVER       UINT16_C(1)
/** @} */


/** @name Possible values for the ACPITBLTPM20::u32StartMethod member.
 * @{ */
/** MMIO interface (TIS1.2+Cancel). */
#define ACPITBL_TPM20_START_METHOD_TIS12    UINT16_C(6)
/** CRB interface. */
#define ACPITBL_TPM20_START_METHOD_CRB      UINT16_C(7)
/** @} */


/**
 * TPM: The ACPI table for a TPM 1.2 device
  * (from: https://trustedcomputinggroup.org/wp-content/uploads/TCG_ACPIGeneralSpecification_v1.20_r8.pdf).
 */
typedef struct ACPITBLTCPA
{
    /** The common ACPI table header. */
    ACPITBLHEADER       Hdr;
    /** The platform class. */
    uint16_t            u16PlatCls;
    /** Log Area Minimum Length. */
    uint32_t            u32Laml;
    /** Log Area Start Address. */
    uint64_t            u64Lasa;
} ACPITBLTCPA;
AssertCompileSize(ACPITBLTCPA, 50);

/** Revision of the TPM1.2 ACPI table. */
#define ACPI_TCPA_REVISION                  2
/** LAML region size. */
#define ACPI_TCPA_LAML_SZ                   _16K


/** @name Possible values for the ACPITBLTCPA::u16PlatCls member.
 * @{ */
/** Client platform. */
#define ACPI_TCPA_PLAT_CLS_CLIENT           UINT16_C(0)
/** @} */
#endif


#pragma pack()


#ifndef VBOX_DEVICE_STRUCT_TESTCASE /* exclude the rest of the file */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef IN_RING3
static int acpiR3PlantTables(PPDMDEVINS pDevIns, PACPISTATE pThis, PACPISTATER3 pThisCC);
#endif

/* SCI, usually IRQ9 */
DECLINLINE(void) acpiSetIrq(PPDMDEVINS pDevIns, int level)
{
    PDMDevHlpPCISetIrq(pDevIns, 0, level);
}

DECLINLINE(bool) pm1a_level(PACPISTATE pThis)
{
    return    (pThis->pm1a_ctl & SCI_EN)
           && (pThis->pm1a_en & pThis->pm1a_sts & ~(RSR_EN | IGN_EN));
}

DECLINLINE(bool) gpe0_level(PACPISTATE pThis)
{
    return !!(pThis->gpe0_en & pThis->gpe0_sts);
}

DECLINLINE(bool) smbus_level(PPDMDEVINS pDevIns, PACPISTATE pThis)
{
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    return    (pThis->u8SMBusHstCnt & SMBHSTCNT_INTEREN)
           && (pPciDev->abConfig[SMBHSTCFG] & SMBHSTCFG_SMB_HST_EN)
           && (pPciDev->abConfig[SMBHSTCFG] & SMBHSTCFG_INTRSEL) == SMBHSTCFG_INTRSEL_IRQ9 << SMBHSTCFG_INTRSEL_SHIFT
           && (pThis->u8SMBusHstSts & SMBHSTSTS_INT_MASK);
}

DECLINLINE(bool) acpiSCILevel(PPDMDEVINS pDevIns, PACPISTATE pThis)
{
    return pm1a_level(pThis) || gpe0_level(pThis) || smbus_level(pDevIns, pThis);
}

/**
 * Used by acpiR3PM1aStsWrite, acpiR3PM1aEnWrite, acpiR3PmTimer,
 * acpiR3Port_PowerBuffonPress, acpiR3Port_SleepButtonPress
 * and acpiPmTmrRead to update the PM1a.STS and PM1a.EN
 * registers and trigger IRQs.
 *
 * Caller must hold the state lock.
 *
 * @param   pDevIns     The PDM device instance.
 * @param   pThis       The ACPI shared instance data.
 * @param   sts         The new PM1a.STS value.
 * @param   en          The new PM1a.EN value.
 */
static void acpiUpdatePm1a(PPDMDEVINS pDevIns, PACPISTATE pThis, uint32_t sts, uint32_t en)
{
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));

    const bool old_level = acpiSCILevel(pDevIns, pThis);
    pThis->pm1a_en = en;
    pThis->pm1a_sts = sts;
    const bool new_level = acpiSCILevel(pDevIns, pThis);

    LogFunc(("old=%x new=%x\n", old_level, new_level));

    if (new_level != old_level)
        acpiSetIrq(pDevIns, new_level);
}

#ifdef IN_RING3

/**
 * Used by acpiR3Gpe0StsWrite, acpiR3Gpe0EnWrite, acpiAttach and acpiDetach to
 * update the GPE0.STS and GPE0.EN registers and trigger IRQs.
 *
 * Caller must hold the state lock.
 *
 * @param   pDevIns     The PDM device instance.
 * @param   pThis       The ACPI shared instance data.
 * @param   sts         The new GPE0.STS value.
 * @param   en          The new GPE0.EN value.
 */
static void apicR3UpdateGpe0(PPDMDEVINS pDevIns, PACPISTATE pThis, uint32_t sts, uint32_t en)
{
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));

    const bool old_level = acpiSCILevel(pDevIns, pThis);
    pThis->gpe0_en  = en;
    pThis->gpe0_sts = sts;
    const bool new_level = acpiSCILevel(pDevIns, pThis);

    LogFunc(("old=%x new=%x\n", old_level, new_level));

    if (new_level != old_level)
        acpiSetIrq(pDevIns, new_level);
}

/**
 * Used by acpiR3PM1aCtlWrite to power off the VM.
 *
 * @param   pDevIns     The device instance.
 * @returns Strict VBox status code.
 */
static VBOXSTRICTRC acpiR3DoPowerOff(PPDMDEVINS pDevIns)
{
    VBOXSTRICTRC rc = PDMDevHlpVMPowerOff(pDevIns);
    AssertRC(VBOXSTRICTRC_VAL(rc));
    return rc;
}

/**
 * Used by acpiR3PM1aCtlWrite to put the VM to sleep.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis   The ACPI shared instance data.
 * @returns Strict VBox status code.
 */
static VBOXSTRICTRC acpiR3DoSleep(PPDMDEVINS pDevIns, PACPISTATE pThis)
{
    /* We must set WAK_STS on resume (includes restore) so the guest knows that
       we've woken up and can continue executing code.  The guest is probably
       reading the PMSTS register in a loop to check this. */
    VBOXSTRICTRC rc;
    pThis->fSetWakeupOnResume = true;
    if (pThis->fSuspendToSavedState)
    {
        rc = PDMDevHlpVMSuspendSaveAndPowerOff(pDevIns);
        if (rc != VERR_NOT_SUPPORTED)
            AssertRC(VBOXSTRICTRC_VAL(rc));
        else
        {
            LogRel(("ACPI: PDMDevHlpVMSuspendSaveAndPowerOff is not supported, falling back to suspend-only\n"));
            rc = PDMDevHlpVMSuspend(pDevIns);
            AssertRC(VBOXSTRICTRC_VAL(rc));
        }
    }
    else
    {
        rc = PDMDevHlpVMSuspend(pDevIns);
        AssertRC(VBOXSTRICTRC_VAL(rc));
    }
    return rc;
}


/**
 * @interface_method_impl{PDMIACPIPORT,pfnGetGuestEnteredACPIMode, Check if the
 *                       Guest entered into G0 (working) or G1 (sleeping)}
 */
static DECLCALLBACK(int) acpiR3Port_GetGuestEnteredACPIMode(PPDMIACPIPORT pInterface, bool *pfEntered)
{
    PACPISTATER3 pThisCC = RT_FROM_MEMBER(pInterface, ACPISTATER3, IACPIPort);
    PPDMDEVINS   pDevIns = pThisCC->pDevIns;
    PACPISTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    *pfEntered = (pThis->pm1a_ctl & SCI_EN) != 0;

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIACPIPORT,pfnGetCpuStatus}
 */
static DECLCALLBACK(int) acpiR3Port_GetCpuStatus(PPDMIACPIPORT pInterface, unsigned uCpu, bool *pfLocked)
{
    PACPISTATER3 pThisCC = RT_FROM_MEMBER(pInterface, ACPISTATER3, IACPIPort);
    PPDMDEVINS   pDevIns = pThisCC->pDevIns;
    PACPISTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    *pfLocked = VMCPUSET_IS_PRESENT(&pThis->CpuSetLocked, uCpu);

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}


/**
 * Send an ACPI monitor hot-plug event.
 *
 * @returns VBox status code
 * @param   pInterface      Pointer to the interface structure containing the
 *                          called function pointer.
 */
static DECLCALLBACK(int) acpiR3Port_MonitorHotPlugEvent(PPDMIACPIPORT pInterface)
{
    PACPISTATER3 pThisCC = RT_FROM_MEMBER(pInterface, ACPISTATER3, IACPIPort);
    PPDMDEVINS   pDevIns = pThisCC->pDevIns;
    PACPISTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    apicR3UpdateGpe0(pDevIns, pThis, pThis->gpe0_sts | 0x4, pThis->gpe0_en);

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}

/**
 * Send an ACPI battery status change event.
 *
 * @returns VBox status code
 * @param   pInterface      Pointer to the interface structure containing the
 *                          called function pointer.
 */
static DECLCALLBACK(int) acpiR3Port_BatteryStatusChangeEvent(PPDMIACPIPORT pInterface)
{
    PACPISTATER3 pThisCC = RT_FROM_MEMBER(pInterface, ACPISTATER3, IACPIPort);
    PPDMDEVINS   pDevIns = pThisCC->pDevIns;
    PACPISTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    apicR3UpdateGpe0(pDevIns, pThis, pThis->gpe0_sts | 0x1, pThis->gpe0_en);

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIEVENTBUTTONPORT,pfnQueryGuestCanHandleButtonEvents}
 */
static DECLCALLBACK(int) acpiR3Port_QueryGuestCanHandleButtonEvents(PPDMIEVENTBUTTONPORT pInterface, bool *pfCanHandleButtonEvents)
{
    PACPISTATER3 pThisCC = RT_FROM_MEMBER(pInterface, ACPISTATER3, IButtonEventPort);
    PPDMDEVINS   pDevIns = pThisCC->pDevIns;
    PACPISTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    /* Just checks whether the guest has entered ACPI mode. */
    *pfCanHandleButtonEvents = (pThis->pm1a_ctl & SCI_EN) != 0;

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIEVENTBUTTONPORT,pfnPowerButtonPress}
 */
static DECLCALLBACK(int) acpiR3Port_PowerButtonPress(PPDMIEVENTBUTTONPORT pInterface)
{
    PACPISTATER3 pThisCC = RT_FROM_MEMBER(pInterface, ACPISTATER3, IButtonEventPort);
    PPDMDEVINS   pDevIns = pThisCC->pDevIns;
    PACPISTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    Log(("acpiR3Port_PowerButtonPress: handled=%d status=%x\n", pThis->fPowerButtonHandled, pThis->pm1a_sts));
    pThis->fPowerButtonHandled = false;
    acpiUpdatePm1a(pDevIns, pThis, pThis->pm1a_sts | PWRBTN_STS, pThis->pm1a_en);

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIEVENTBUTTONPORT,pfnSleepButtonPress}
 */
static DECLCALLBACK(int) acpiR3Port_SleepButtonPress(PPDMIEVENTBUTTONPORT pInterface)
{
    PACPISTATER3 pThisCC = RT_FROM_MEMBER(pInterface, ACPISTATER3, IButtonEventPort);
    PPDMDEVINS   pDevIns = pThisCC->pDevIns;
    PACPISTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    acpiUpdatePm1a(pDevIns, pThis, pThis->pm1a_sts | SLPBTN_STS, pThis->pm1a_en);

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIEVENTBUTTONPORT,pfnQueryPowerButtonHandled}
 */
static DECLCALLBACK(int) acpiR3Port_QueryPowerButtonHandled(PPDMIEVENTBUTTONPORT pInterface, bool *pfHandled)
{
    PACPISTATER3 pThisCC = RT_FROM_MEMBER(pInterface, ACPISTATER3, IButtonEventPort);
    PPDMDEVINS   pDevIns = pThisCC->pDevIns;
    PACPISTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    *pfHandled = pThis->fPowerButtonHandled;

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}


/**
 * Used by acpiR3PmTimer to re-arm the PM timer.
 *
 * The caller is expected to either hold the clock lock or to have made sure
 * the VM is resetting or loading state.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The ACPI shared instance data.
 * @param   uNow        The current time.
 */
static void acpiR3PmTimerReset(PPDMDEVINS pDevIns, PACPISTATE pThis, uint64_t uNow)
{
    uint64_t uTimerFreq = PDMDevHlpTimerGetFreq(pDevIns, pThis->hPmTimer);
    uint32_t uPmTmrCyclesToRollover = TMR_VAL_MSB - (pThis->uPmTimerVal & (TMR_VAL_MSB - 1));
    uint64_t uInterval  = ASMMultU64ByU32DivByU32(uPmTmrCyclesToRollover, uTimerFreq, PM_TMR_FREQ);
    PDMDevHlpTimerSet(pDevIns, pThis->hPmTimer, uNow + uInterval + 1);
    Log(("acpi: uInterval = %RU64\n", uInterval));
}

#endif /* IN_RING3 */

/**
 * Used by acpiR3PMTimer & acpiPmTmrRead to update TMR_VAL and update TMR_STS
 *
 * The caller is expected to either hold the clock lock or to have made sure
 * the VM is resetting or loading state.
 *
 * @param   pDevIns     The PDM device instance.
 * @param   pThis       The ACPI instance
 * @param   u64Now      The current time
 */
static void acpiPmTimerUpdate(PPDMDEVINS pDevIns, PACPISTATE pThis, uint64_t u64Now)
{
    uint32_t msb = pThis->uPmTimerVal & TMR_VAL_MSB;
    uint64_t u64Elapsed = u64Now - pThis->u64PmTimerInitial;
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, pThis->hPmTimer));

    pThis->uPmTimerVal = ASMMultU64ByU32DivByU32(u64Elapsed, PM_TMR_FREQ, PDMDevHlpTimerGetFreq(pDevIns, pThis->hPmTimer))
                       & TMR_VAL_MASK;

    if ((pThis->uPmTimerVal & TMR_VAL_MSB) != msb)
        acpiUpdatePm1a(pDevIns, pThis, pThis->pm1a_sts | TMR_STS, pThis->pm1a_en);
}

#ifdef IN_RING3

/**
 * @callback_method_impl{FNTMTIMERDEV, PM Timer callback}
 */
static DECLCALLBACK(void) acpiR3PmTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    Assert(pThis->hPmTimer == hTimer);
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, hTimer));
    RT_NOREF(pvUser);

    DEVACPI_LOCK_R3(pDevIns, pThis);
    Log(("acpi: pm timer sts %#x (%d), en %#x (%d)\n",
         pThis->pm1a_sts, (pThis->pm1a_sts & TMR_STS) != 0,
         pThis->pm1a_en, (pThis->pm1a_en & TMR_EN) != 0));
    uint64_t tsNow = PDMDevHlpTimerGet(pDevIns, hTimer);
    acpiPmTimerUpdate(pDevIns, pThis, tsNow);
    DEVACPI_UNLOCK(pDevIns, pThis);

    acpiR3PmTimerReset(pDevIns, pThis, tsNow);
}

/**
 * _BST method - used by acpiR3BatDataRead to implement BAT_STATUS_STATE and
 * acpiR3LoadState.
 *
 * @returns VINF_SUCCESS.
 * @param   pThis           The ACPI shared instance data.
 * @param   pThisCC         The ACPI instance data for ring-3.
 */
static int acpiR3FetchBatteryStatus(PACPISTATE pThis, PACPISTATER3 pThisCC)
{
    uint32_t           *p = pThis->au8BatteryInfo;
    bool               fPresent;              /* battery present? */
    PDMACPIBATCAPACITY hostRemainingCapacity; /* 0..100 */
    PDMACPIBATSTATE    hostBatteryState;      /* bitfield */
    uint32_t           hostPresentRate;       /* 0..1000 */
    int                rc;

    if (!pThisCC->pDrv)
        return VINF_SUCCESS;
    rc = pThisCC->pDrv->pfnQueryBatteryStatus(pThisCC->pDrv, &fPresent, &hostRemainingCapacity,
                                              &hostBatteryState, &hostPresentRate);
    AssertRC(rc);

    /* default values */
    p[BAT_STATUS_STATE]              = hostBatteryState;
    p[BAT_STATUS_PRESENT_RATE]       = hostPresentRate == ~0U ? 0xFFFFFFFF
                                                              : hostPresentRate * 50;  /* mW */
    p[BAT_STATUS_REMAINING_CAPACITY] = 50000; /* mWh */
    p[BAT_STATUS_PRESENT_VOLTAGE]    = 10000; /* mV */

    /* did we get a valid battery state? */
    if (hostRemainingCapacity != PDM_ACPI_BAT_CAPACITY_UNKNOWN)
        p[BAT_STATUS_REMAINING_CAPACITY] = hostRemainingCapacity * 500; /* mWh */
    if (hostBatteryState == PDM_ACPI_BAT_STATE_CHARGED)
        p[BAT_STATUS_PRESENT_RATE] = 0; /* mV */

    return VINF_SUCCESS;
}

/**
 * _BIF method - used by acpiR3BatDataRead to implement BAT_INFO_UNITS and
 * acpiR3LoadState.
 *
 * @returns VINF_SUCCESS.
 * @param   pThis           The ACPI shared instance data.
 */
static int acpiR3FetchBatteryInfo(PACPISTATE pThis)
{
    uint32_t *p = pThis->au8BatteryInfo;

    p[BAT_INFO_UNITS]                      = 0;     /* mWh */
    p[BAT_INFO_DESIGN_CAPACITY]            = 50000; /* mWh */
    p[BAT_INFO_LAST_FULL_CHARGE_CAPACITY]  = 50000; /* mWh */
    p[BAT_INFO_TECHNOLOGY]                 = BAT_TECH_PRIMARY;
    p[BAT_INFO_DESIGN_VOLTAGE]             = 10000; /* mV */
    p[BAT_INFO_DESIGN_CAPACITY_OF_WARNING] = 100;   /* mWh */
    p[BAT_INFO_DESIGN_CAPACITY_OF_LOW]     = 50;    /* mWh */
    p[BAT_INFO_CAPACITY_GRANULARITY_1]     = 1;     /* mWh */
    p[BAT_INFO_CAPACITY_GRANULARITY_2]     = 1;     /* mWh */

    return VINF_SUCCESS;
}

/**
 * The _STA method - used by acpiR3BatDataRead to implement BAT_DEVICE_STATUS.
 *
 * @returns status mask or 0.
 * @param   pThisCC         The ACPI instance data for ring-3.
 */
static uint32_t acpiR3GetBatteryDeviceStatus(PACPISTATER3 pThisCC)
{
    bool               fPresent;              /* battery present? */
    PDMACPIBATCAPACITY hostRemainingCapacity; /* 0..100 */
    PDMACPIBATSTATE    hostBatteryState;      /* bitfield */
    uint32_t           hostPresentRate;       /* 0..1000 */
    int                rc;

    if (!pThisCC->pDrv)
        return 0;
    rc = pThisCC->pDrv->pfnQueryBatteryStatus(pThisCC->pDrv, &fPresent, &hostRemainingCapacity,
                                              &hostBatteryState, &hostPresentRate);
    AssertRC(rc);

    return fPresent
         ?   STA_DEVICE_PRESENT_MASK                     /* present */
           | STA_DEVICE_ENABLED_MASK                     /* enabled and decodes its resources */
           | STA_DEVICE_SHOW_IN_UI_MASK                  /* should be shown in UI */
           | STA_DEVICE_FUNCTIONING_PROPERLY_MASK        /* functioning properly */
           | STA_BATTERY_PRESENT_MASK                    /* battery is present */
         : 0;                                            /* device not present */
}

/**
 * Used by acpiR3BatDataRead to implement BAT_POWER_SOURCE.
 *
 * @returns status.
 * @param   pThisCC         The ACPI instance data for ring-3.
 */
static uint32_t acpiR3GetPowerSource(PACPISTATER3 pThisCC)
{
    /* query the current power source from the host driver */
    if (!pThisCC->pDrv)
        return AC_ONLINE;

    PDMACPIPOWERSOURCE ps;
    int rc = pThisCC->pDrv->pfnQueryPowerSource(pThisCC->pDrv, &ps);
    AssertRC(rc);
    return ps == PDM_ACPI_POWER_SOURCE_BATTERY ? AC_OFFLINE : AC_ONLINE;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, Battery status index}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3BatIndexWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(pvUser, offPort);
    Log(("acpiR3BatIndexWrite: %#x (%#x)\n", u32, u32 >> 2));
    if (cb != 4)
        return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x\n", cb, offPort, u32);

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    u32 >>= pThis->u8IndexShift;
    /* see comment at the declaration of u8IndexShift */
    if (pThis->u8IndexShift == 0 && u32 == (BAT_DEVICE_STATUS << 2))
    {
        pThis->u8IndexShift = 2;
        u32 >>= 2;
    }
    ASSERT_GUEST_MSG(u32 < BAT_INDEX_LAST, ("%#x\n", u32));
    pThis->uBatteryIndex = u32;

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, Battery status data}
 */
static DECLCALLBACK(VBOXSTRICTRC)  acpiR3BatDataRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pvUser, offPort);
    if (cb != 4)
        return VERR_IOM_IOPORT_UNUSED;

    PACPISTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    PACPISTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PACPISTATER3);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    VBOXSTRICTRC rc = VINF_SUCCESS;
    switch (pThis->uBatteryIndex)
    {
        case BAT_STATUS_STATE:
            acpiR3FetchBatteryStatus(pThis, pThisCC);
            RT_FALL_THRU();
        case BAT_STATUS_PRESENT_RATE:
        case BAT_STATUS_REMAINING_CAPACITY:
        case BAT_STATUS_PRESENT_VOLTAGE:
            *pu32 = pThis->au8BatteryInfo[pThis->uBatteryIndex];
            break;

        case BAT_INFO_UNITS:
            acpiR3FetchBatteryInfo(pThis);
            RT_FALL_THRU();
        case BAT_INFO_DESIGN_CAPACITY:
        case BAT_INFO_LAST_FULL_CHARGE_CAPACITY:
        case BAT_INFO_TECHNOLOGY:
        case BAT_INFO_DESIGN_VOLTAGE:
        case BAT_INFO_DESIGN_CAPACITY_OF_WARNING:
        case BAT_INFO_DESIGN_CAPACITY_OF_LOW:
        case BAT_INFO_CAPACITY_GRANULARITY_1:
        case BAT_INFO_CAPACITY_GRANULARITY_2:
            *pu32 = pThis->au8BatteryInfo[pThis->uBatteryIndex];
            break;

        case BAT_DEVICE_STATUS:
            *pu32 = acpiR3GetBatteryDeviceStatus(pThisCC);
            break;

        case BAT_POWER_SOURCE:
            *pu32 = acpiR3GetPowerSource(pThisCC);
            break;

        default:
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u idx=%u\n", cb, offPort, pThis->uBatteryIndex);
            *pu32 = UINT32_MAX;
            break;
    }

    DEVACPI_UNLOCK(pDevIns, pThis);
    return rc;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, System info index}
 */
static DECLCALLBACK(VBOXSTRICTRC)  acpiR3SysInfoIndexWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(pvUser, offPort);
    Log(("acpiR3SysInfoIndexWrite: %#x (%#x)\n", u32, u32 >> 2));
    if (cb != 4)
        return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x\n", cb, offPort, u32);

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    if (u32 == SYSTEM_INFO_INDEX_VALID || u32 == SYSTEM_INFO_INDEX_INVALID)
        pThis->uSystemInfoIndex = u32;
    else
    {
        /* see comment at the declaration of u8IndexShift */
        if (u32 > SYSTEM_INFO_INDEX_END && pThis->u8IndexShift == 0)
        {
            if ((u32 >> 2) < SYSTEM_INFO_INDEX_END && (u32 & 0x3) == 0)
                pThis->u8IndexShift = 2;
        }

        u32 >>= pThis->u8IndexShift;

        /* If the index exceeds 31 (which is all we can fit within offset 0x80), we need to divide the index again
           for indices > 31 and < SYSTEM_INFO_INDEX_END. */
        if (u32 > SYSTEM_INFO_INDEX_END && pThis->u8IndexShift == 2 && (u32 >> 2) < SYSTEM_INFO_INDEX_END)
            u32 >>= 2;

        ASSERT_GUEST_MSG(u32 < SYSTEM_INFO_INDEX_END, ("%u - Max=%u. IndexShift=%u\n", u32, SYSTEM_INFO_INDEX_END, pThis->u8IndexShift));
        pThis->uSystemInfoIndex = u32;
    }

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, System info data}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3SysInfoDataRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pvUser, offPort);
    if (cb != 4)
        return VERR_IOM_IOPORT_UNUSED;

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    VBOXSTRICTRC rc = VINF_SUCCESS;
    uint32_t const uSystemInfoIndex = pThis->uSystemInfoIndex;
    switch (uSystemInfoIndex)
    {
        case SYSTEM_INFO_INDEX_LOW_MEMORY_LENGTH:
            *pu32 = pThis->cbRamLow;
            break;

        case SYSTEM_INFO_INDEX_PREF64_MEMORY_MIN:
            *pu32 = pThis->u64PciPref64Min >> 16; /* 64KB units */
            Assert(((uint64_t)*pu32 << 16) == pThis->u64PciPref64Min);
            break;

        case SYSTEM_INFO_INDEX_PREF64_MEMORY_MAX:
            *pu32 = pThis->u64PciPref64Max >> 16; /* 64KB units */
            Assert(((uint64_t)*pu32 << 16) == pThis->u64PciPref64Max);
            break;

        case SYSTEM_INFO_INDEX_USE_IOAPIC:
            *pu32 = pThis->u8UseIOApic;
            break;

        case SYSTEM_INFO_INDEX_HPET_STATUS:
            *pu32 = pThis->fUseHpet
                  ? (  STA_DEVICE_PRESENT_MASK
                     | STA_DEVICE_ENABLED_MASK
                     | STA_DEVICE_SHOW_IN_UI_MASK
                     | STA_DEVICE_FUNCTIONING_PROPERLY_MASK)
                  : 0;
            break;

        case SYSTEM_INFO_INDEX_SMC_STATUS:
            *pu32 = pThis->fUseSmc
                  ? (  STA_DEVICE_PRESENT_MASK
                     | STA_DEVICE_ENABLED_MASK
                     /* no need to show this device in the UI */
                     | STA_DEVICE_FUNCTIONING_PROPERLY_MASK)
                  : 0;
            break;

        case SYSTEM_INFO_INDEX_FDC_STATUS:
            *pu32 = pThis->fUseFdc
                  ? (  STA_DEVICE_PRESENT_MASK
                     | STA_DEVICE_ENABLED_MASK
                     | STA_DEVICE_SHOW_IN_UI_MASK
                     | STA_DEVICE_FUNCTIONING_PROPERLY_MASK)
                  : 0;
            break;

        case SYSTEM_INFO_INDEX_NIC_ADDRESS:
            *pu32 = pThis->u32NicPciAddress;
            break;

        case SYSTEM_INFO_INDEX_AUDIO_ADDRESS:
            *pu32 = pThis->u32AudioPciAddress;
            break;

        case SYSTEM_INFO_INDEX_NVME_ADDRESS:
            *pu32 = pThis->u32NvmePciAddress;
            break;

        case SYSTEM_INFO_INDEX_POWER_STATES:
            *pu32 = RT_BIT(0) | RT_BIT(5);  /* S1 and S5 always exposed */
            if (pThis->fS1Enabled)          /* Optionally expose S1 and S4 */
                *pu32 |= RT_BIT(1);
            if (pThis->fS4Enabled)
                *pu32 |= RT_BIT(4);
            break;

        case SYSTEM_INFO_INDEX_IOC_ADDRESS:
            *pu32 = pThis->u32IocPciAddress;
            break;

        case SYSTEM_INFO_INDEX_HBC_ADDRESS:
            *pu32 = pThis->u32HbcPciAddress;
            break;

        case SYSTEM_INFO_INDEX_PCI_BASE:
            /** @todo couldn't MCFG be in 64-bit range? */
            Assert(pThis->u64PciConfigMMioAddress < 0xffffffff);
            *pu32 = (uint32_t)pThis->u64PciConfigMMioAddress;
            break;

        case SYSTEM_INFO_INDEX_PCI_LENGTH:
            /** @todo couldn't MCFG be in 64-bit range? */
            Assert(pThis->u64PciConfigMMioLength < 0xffffffff);
            *pu32 = (uint32_t)pThis->u64PciConfigMMioLength;
            break;

        case SYSTEM_INFO_INDEX_RTC_STATUS:
            *pu32 = pThis->fShowRtc
                  ? (  STA_DEVICE_PRESENT_MASK
                     | STA_DEVICE_ENABLED_MASK
                     | STA_DEVICE_SHOW_IN_UI_MASK
                     | STA_DEVICE_FUNCTIONING_PROPERLY_MASK)
                  : 0;
            break;

        case SYSTEM_INFO_INDEX_CPU_LOCKED:
            if (pThis->idCpuLockCheck < VMM_MAX_CPU_COUNT)
            {
                *pu32 = VMCPUSET_IS_PRESENT(&pThis->CpuSetLocked, pThis->idCpuLockCheck);
                pThis->idCpuLockCheck = UINT32_C(0xffffffff); /* Make the entry invalid */
            }
            else
            {
                rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "CPU lock check protocol violation (idCpuLockCheck=%#x)\n",
                                       pThis->idCpuLockCheck);
                /* Always return locked status just to be safe */
                *pu32 = 1;
            }
            break;

        case SYSTEM_INFO_INDEX_CPU_EVENT_TYPE:
            *pu32 = pThis->u32CpuEventType;
            break;

        case SYSTEM_INFO_INDEX_CPU_EVENT:
            *pu32 = pThis->u32CpuEvent;
            break;

        case SYSTEM_INFO_INDEX_SERIAL0_IOBASE:
            *pu32 = pThis->uSerial0IoPortBase;
            break;

        case SYSTEM_INFO_INDEX_SERIAL0_IRQ:
            *pu32 = pThis->uSerial0Irq;
            break;

        case SYSTEM_INFO_INDEX_SERIAL1_IOBASE:
            *pu32 = pThis->uSerial1IoPortBase;
            break;

        case SYSTEM_INFO_INDEX_SERIAL1_IRQ:
            *pu32 = pThis->uSerial1Irq;
            break;

        case SYSTEM_INFO_INDEX_SERIAL2_IOBASE:
            *pu32 = pThis->uSerial2IoPortBase;
            break;

        case SYSTEM_INFO_INDEX_SERIAL2_IRQ:
            *pu32 = pThis->uSerial2Irq;
            break;

        case SYSTEM_INFO_INDEX_SERIAL3_IOBASE:
            *pu32 = pThis->uSerial3IoPortBase;
            break;

        case SYSTEM_INFO_INDEX_SERIAL3_IRQ:
            *pu32 = pThis->uSerial3Irq;
            break;

        case SYSTEM_INFO_INDEX_PARALLEL0_IOBASE:
            *pu32 = pThis->uParallel0IoPortBase;
            break;

        case SYSTEM_INFO_INDEX_PARALLEL0_IRQ:
            *pu32 = pThis->uParallel0Irq;
            break;

        case SYSTEM_INFO_INDEX_PARALLEL1_IOBASE:
            *pu32 = pThis->uParallel1IoPortBase;
            break;

        case SYSTEM_INFO_INDEX_PARALLEL1_IRQ:
            *pu32 = pThis->uParallel1Irq;
            break;

        case SYSTEM_INFO_INDEX_IOMMU_ADDRESS:
            *pu32 = pThis->u32IommuPciAddress;
            break;

        case SYSTEM_INFO_INDEX_SB_IOAPIC_ADDRESS:
            *pu32 = pThis->u32SbIoApicPciAddress;
            break;

        case SYSTEM_INFO_INDEX_END:
            /** @todo why isn't this setting any output value?  */
            break;

        /* Solaris 9 tries to read from this index */
        case SYSTEM_INFO_INDEX_INVALID:
            *pu32 = 0;
            break;

        default:
            *pu32 = UINT32_MAX;
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u idx=%u\n", cb, offPort, uSystemInfoIndex);
            break;
    }

    DEVACPI_UNLOCK(pDevIns, pThis);
    Log(("acpiR3SysInfoDataRead: idx=%d val=%#x (%u) rc=%Rrc\n", uSystemInfoIndex, *pu32, *pu32, VBOXSTRICTRC_VAL(rc)));
    return rc;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, System info data}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3SysInfoDataWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(pvUser, offPort);
    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    if (cb != 4)
        return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x idx=%u\n", cb, offPort, u32, pThis->uSystemInfoIndex);

    DEVACPI_LOCK_R3(pDevIns, pThis);
    Log(("addr=%#x cb=%d u32=%#x si=%#x\n", offPort, cb, u32, pThis->uSystemInfoIndex));

    VBOXSTRICTRC rc = VINF_SUCCESS;
    switch (pThis->uSystemInfoIndex)
    {
        case SYSTEM_INFO_INDEX_INVALID:
            AssertMsg(u32 == 0xbadc0de, ("u32=%u\n", u32));
            pThis->u8IndexShift = 0;
            break;

        case SYSTEM_INFO_INDEX_VALID:
            AssertMsg(u32 == 0xbadc0de, ("u32=%u\n", u32));
            pThis->u8IndexShift = 2;
            break;

        case SYSTEM_INFO_INDEX_CPU_LOCK_CHECK:
            pThis->idCpuLockCheck = u32;
            break;

        case SYSTEM_INFO_INDEX_CPU_LOCKED:
            if (u32 < pThis->cCpus)
                VMCPUSET_DEL(&pThis->CpuSetLocked, u32); /* Unlock the CPU */
            else
                LogRel(("ACPI: CPU %u does not exist\n", u32));
            break;

        default:
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x idx=%u\n", cb, offPort, u32, pThis->uSystemInfoIndex);
            break;
    }

    DEVACPI_UNLOCK(pDevIns, pThis);
    return rc;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, PM1a Enable}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3Pm1aEnRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    if (cb != 2)
        return VERR_IOM_IOPORT_UNUSED;

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    *pu32 = pThis->pm1a_en;

    DEVACPI_UNLOCK(pDevIns, pThis);
    Log(("acpiR3Pm1aEnRead -> %#x\n", *pu32));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, PM1a Enable}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3PM1aEnWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    if (cb != 2 && cb != 4)
        return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x\n", cb, offPort, u32);

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    Log(("acpiR3PM1aEnWrite: %#x (%#x)\n", u32, u32 & ~(RSR_EN | IGN_EN) & 0xffff));
    u32 &= ~(RSR_EN | IGN_EN);
    u32 &= 0xffff;
    acpiUpdatePm1a(pDevIns, pThis, pThis->pm1a_sts, u32);

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, PM1a Status}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3Pm1aStsRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    if (cb != 2)
    {
        int rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u\n", cb, offPort);
        return rc == VINF_SUCCESS ? VERR_IOM_IOPORT_UNUSED : rc;
    }

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    *pu32 = pThis->pm1a_sts;

    DEVACPI_UNLOCK(pDevIns, pThis);
    Log(("acpiR3Pm1aStsRead: %#x\n", *pu32));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, PM1a Status}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3PM1aStsWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    if (cb != 2 && cb != 4)
        return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x\n", cb, offPort, u32);

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    Log(("acpiR3PM1aStsWrite: %#x (%#x)\n", u32, u32 & ~(RSR_STS | IGN_STS) & 0xffff));
    u32 &= 0xffff;
    if (u32 & PWRBTN_STS)
        pThis->fPowerButtonHandled = true; /* Remember that the guest handled the last power button event */
    u32 = pThis->pm1a_sts & ~(u32 & ~(RSR_STS | IGN_STS));
    acpiUpdatePm1a(pDevIns, pThis, u32, pThis->pm1a_en);

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, PM1a Control}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3Pm1aCtlRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    if (cb != 2)
    {
        int rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u\n", cb, offPort);
        return rc == VINF_SUCCESS ? VERR_IOM_IOPORT_UNUSED : rc;
    }

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    *pu32 = pThis->pm1a_ctl;

    DEVACPI_UNLOCK(pDevIns, pThis);
    Log(("acpiR3Pm1aCtlRead: %#x\n", *pu32));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, PM1a Control}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3PM1aCtlWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    if (cb != 2 && cb != 4)
        return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x\n", cb, offPort, u32);

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    Log(("acpiR3PM1aCtlWrite: %#x (%#x)\n", u32, u32 & ~(RSR_CNT | IGN_CNT) & 0xffff));
    u32 &= 0xffff;
    pThis->pm1a_ctl = u32 & ~(RSR_CNT | IGN_CNT);

    VBOXSTRICTRC rc = VINF_SUCCESS;
    uint32_t const uSleepState = (pThis->pm1a_ctl >> SLP_TYPx_SHIFT) & SLP_TYPx_MASK;
    if (uSleepState != pThis->uSleepState)
    {
        pThis->uSleepState = uSleepState;
        switch (uSleepState)
        {
            case 0x00:                  /* S0 */
                break;

            case 0x01:                  /* S1 */
                if (pThis->fS1Enabled)
                {
                    LogRel(("ACPI: Entering S1 power state (powered-on suspend)\n"));
                    rc = acpiR3DoSleep(pDevIns, pThis);
                    break;
                }
                LogRel(("ACPI: Ignoring guest attempt to enter S1 power state (powered-on suspend)!\n"));
                RT_FALL_THRU();

            case 0x04:                  /* S4 */
                if (pThis->fS4Enabled)
                {
                    LogRel(("ACPI: Entering S4 power state (suspend to disk)\n"));
                    rc = acpiR3DoPowerOff(pDevIns);/* Same behavior as S5 */
                    break;
                }
                LogRel(("ACPI: Ignoring guest attempt to enter S4 power state (suspend to disk)!\n"));
                RT_FALL_THRU();

            case 0x05:                  /* S5 */
                LogRel(("ACPI: Entering S5 power state (power down)\n"));
                rc = acpiR3DoPowerOff(pDevIns);
                break;

            default:
                rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "Unknown sleep state %#x (u32=%#x)\n", uSleepState, u32);
                break;
        }
    }

    DEVACPI_UNLOCK(pDevIns, pThis);
    Log(("acpiR3PM1aCtlWrite: rc=%Rrc\n", VBOXSTRICTRC_VAL(rc)));
    return rc;
}

#endif /* IN_RING3 */

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, PMTMR}
 *
 * @remarks The only I/O port currently implemented in all contexts.
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiPMTmrRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    if (cb != 4)
        return VERR_IOM_IOPORT_UNUSED;

    /*
     * We use the clock lock to serialize access to u64PmTimerInitial and to
     * make sure we get a reliable time from the clock
     * as well as and to prevent uPmTimerVal from being updated during read.
     */
    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    VBOXSTRICTRC rc = PDMDevHlpTimerLockClock2(pDevIns, pThis->hPmTimer, &pThis->CritSect, VINF_IOM_R3_IOPORT_READ);
    if (rc == VINF_SUCCESS)
    {
        uint64_t u64Now = PDMDevHlpTimerGet(pDevIns, pThis->hPmTimer);
        acpiPmTimerUpdate(pDevIns, pThis, u64Now);
        *pu32 = pThis->uPmTimerVal;

        PDMDevHlpTimerUnlockClock2(pDevIns, pThis->hPmTimer, &pThis->CritSect);

        DBGFTRACE_PDM_U64_TAG(pDevIns, u64Now, "acpi");
        Log(("acpi: acpiPMTmrRead -> %#x\n", *pu32));

#if 0
        /** @todo temporary: sanity check against running backwards */
        uint32_t uOld = ASMAtomicXchgU32(&pThis->uPmTimeOld, *pu32);
        if (*pu32 - uOld >= 0x10000000)
        {
# if defined(IN_RING0)
            pThis->uPmTimeA = uOld;
            pThis->uPmTimeB = *pu32;
            return VERR_TM_TIMER_BAD_CLOCK;
# elif defined(IN_RING3)
            AssertReleaseMsgFailed(("acpiPMTmrRead: old=%08RX32, current=%08RX32\n", uOld, *pu32));
# endif
        }
#endif
    }
    return rc;
}

#ifdef IN_RING3

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, GPE0 Status}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3Gpe0StsRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    if (cb != 1)
    {
        int rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u\n", cb, offPort);
        return rc == VINF_SUCCESS ? VERR_IOM_IOPORT_UNUSED : rc;
    }

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    *pu32 = pThis->gpe0_sts & 0xff;

    DEVACPI_UNLOCK(pDevIns, pThis);
    Log(("acpiR3Gpe0StsRead: %#x\n", *pu32));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, GPE0 Status}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3Gpe0StsWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    if (cb != 1)
        return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x\n", cb, offPort, u32);

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    Log(("acpiR3Gpe0StsWrite: %#x (%#x)\n", u32, pThis->gpe0_sts & ~u32));
    u32 = pThis->gpe0_sts & ~u32;
    apicR3UpdateGpe0(pDevIns, pThis, u32, pThis->gpe0_en);

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, GPE0 Enable}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3Gpe0EnRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    if (cb != 1)
    {
        int rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u\n", cb, offPort);
        return rc == VINF_SUCCESS ? VERR_IOM_IOPORT_UNUSED : rc;
    }

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    *pu32 = pThis->gpe0_en & 0xff;

    DEVACPI_UNLOCK(pDevIns, pThis);
    Log(("acpiR3Gpe0EnRead: %#x\n", *pu32));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, GPE0 Enable}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3Gpe0EnWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    if (cb != 1)
        return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x\n", cb, offPort, u32);

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    Log(("acpiR3Gpe0EnWrite: %#x\n", u32));
    apicR3UpdateGpe0(pDevIns, pThis, pThis->gpe0_sts, u32);

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, SMI_CMD}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3SmiWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    Log(("acpiR3SmiWrite %#x\n", u32));
    if (cb != 1)
        return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x\n", cb, offPort, u32);

    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    DEVACPI_LOCK_R3(pDevIns, pThis);

    if (u32 == ACPI_ENABLE)
        pThis->pm1a_ctl |= SCI_EN;
    else if (u32 == ACPI_DISABLE)
        pThis->pm1a_ctl &= ~SCI_EN;
    else
        Log(("acpiR3SmiWrite: %#x <- unknown value\n", u32));

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, ACPI_RESET_BLK}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3ResetWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    Log(("acpiR3ResetWrite: %#x\n", u32));
    NOREF(pvUser);
    if (cb != 1)
        return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x\n", cb, offPort, u32);

    /* No state locking required. */
    VBOXSTRICTRC rc;
    if (u32 == ACPI_RESET_REG_VAL)
    {
        LogRel(("ACPI: Reset initiated by ACPI\n"));
        rc = PDMDevHlpVMReset(pDevIns, PDMVMRESET_F_ACPI);
    }
    else
    {
        Log(("acpiR3ResetWrite: %#x <- unknown value\n", u32));
        rc = VINF_SUCCESS;
    }

    return rc;
}

# ifdef DEBUG_ACPI

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, Debug hex value logger}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3DebugHexWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    NOREF(pvUser);
    switch (cb)
    {
        case 1:
            Log(("%#x\n", u32 & 0xff));
            break;
        case 2:
            Log(("%#6x\n", u32 & 0xffff));
            break;
        case 4:
            Log(("%#10x\n", u32));
            break;
        default:
            return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x\n", cb, offPort, u32);
    }
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, Debug char logger}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3DebugCharWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    NOREF(pvUser);
    switch (cb)
    {
        case 1:
            Log(("%c", u32 & 0xff));
            break;
        default:
            return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x\n", cb, offPort, u32);
    }
    return VINF_SUCCESS;
}

# endif /* DEBUG_ACPI */

/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) acpiR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    pHlp->pfnPrintf(pHlp,
                    "timer: old=%08RX32, current=%08RX32\n", pThis->uPmTimeA, pThis->uPmTimeB);
}

/**
 * Called by acpiR3Reset and acpiR3Construct to set up the PM PCI config space.
 *
 * @param   pDevIns     The PDM device instance.
 * @param   pThis       The ACPI shared instance data.
 */
static void acpiR3PmPCIBIOSFake(PPDMDEVINS pDevIns, PACPISTATE pThis)
{
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    pPciDev->abConfig[PMBA    ] = pThis->uPmIoPortBase | 1; /* PMBA, PM base address, bit 0 marks it as IO range */
    pPciDev->abConfig[PMBA + 1] = pThis->uPmIoPortBase >> 8;
    pPciDev->abConfig[PMBA + 2] = 0x00;
    pPciDev->abConfig[PMBA + 3] = 0x00;
}

/**
 * Used to calculate the value of a PM I/O port.
 *
 * @returns The actual I/O port value.
 * @param   pThis               The ACPI shared instance data.
 * @param   offset              The offset into the I/O space, or -1 if invalid.
 */
static RTIOPORT acpiR3CalcPmPort(PACPISTATE pThis, int32_t offset)
{
    Assert(pThis->uPmIoPortBase != 0);

    if (offset == -1)
        return 0;

    return (RTIOPORT)(pThis->uPmIoPortBase + offset);
}

/**
 * Called by acpiR3LoadState and acpiR3UpdatePmHandlers to map the PM1a, PM
 * timer and GPE0 I/O ports.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pThis           The ACPI shared instance data.
 */
static int acpiR3MapPmIoPorts(PPDMDEVINS pDevIns, PACPISTATE pThis)
{
    if (pThis->uPmIoPortBase == 0)
        return VINF_SUCCESS;

    int rc;
    rc = PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortPm1aSts, acpiR3CalcPmPort(pThis, PM1a_EVT_OFFSET));
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortPm1aEn,  acpiR3CalcPmPort(pThis, PM1a_EVT_OFFSET + 2));
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortPm1aCtl, acpiR3CalcPmPort(pThis, PM1a_CTL_OFFSET));
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortPmTimer, acpiR3CalcPmPort(pThis, PM_TMR_OFFSET));
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortGpe0Sts, acpiR3CalcPmPort(pThis, GPE0_OFFSET));
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortGpe0En,  acpiR3CalcPmPort(pThis, GPE0_OFFSET + GPE0_BLK_LEN / 2));

    return VINF_SUCCESS;
}

/**
 * Called by acpiR3LoadState and acpiR3UpdatePmHandlers to unmap the PM1a, PM
 * timer and GPE0 I/O ports.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The ACPI shared instance data.
 */
static int acpiR3UnmapPmIoPorts(PPDMDEVINS pDevIns, PACPISTATE pThis)
{
    if (pThis->uPmIoPortBase != 0)
    {
        int rc;
        rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortPm1aSts);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortPm1aEn);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortPm1aCtl);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortPmTimer);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortGpe0Sts);
        AssertRCReturn(rc, rc);
        rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortGpe0En);
        AssertRCReturn(rc, rc);
    }
    return VINF_SUCCESS;
}

/**
 * Called by acpiR3PciConfigWrite and acpiReset to change the location of the
 * PM1a, PM timer and GPE0 ports.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The ACPI shared instance data.
 * @param   pThisCC         The ACPI instance data for ring-3.
 * @param   NewIoPortBase   The new base address of the I/O ports.
 */
static int acpiR3UpdatePmHandlers(PPDMDEVINS pDevIns, PACPISTATE pThis, PACPISTATER3 pThisCC, RTIOPORT NewIoPortBase)
{
    Log(("acpi: rebasing PM 0x%x -> 0x%x\n", pThis->uPmIoPortBase, NewIoPortBase));
    if (NewIoPortBase != pThis->uPmIoPortBase)
    {
        int rc = acpiR3UnmapPmIoPorts(pDevIns, pThis);
        if (RT_FAILURE(rc))
            return rc;

        pThis->uPmIoPortBase = NewIoPortBase;

        rc = acpiR3MapPmIoPorts(pDevIns, pThis);
        if (RT_FAILURE(rc))
            return rc;

        /* We have to update FADT table acccording to the new base */
        rc = acpiR3PlantTables(pDevIns, pThis, pThisCC);
        AssertRC(rc);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, SMBus}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3SMBusWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(pvUser);
    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);

    LogFunc(("offPort=%#x u32=%#x cb=%u\n", offPort, u32, cb));
    uint8_t off = offPort & 0x000f;
    if (   (cb != 1 && off <= SMBSHDWCMD_OFF)
        || (cb != 2 && (off == SMBSLVEVT_OFF || off == SMBSLVDAT_OFF)))
        return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "cb=%d offPort=%u u32=%#x\n", cb, offPort, u32);

    DEVACPI_LOCK_R3(pDevIns, pThis);
    switch (off)
    {
        case SMBHSTSTS_OFF:
            /* Bit 0 is readonly, bits 1..4 are write clear, bits 5..7 are reserved */
            pThis->u8SMBusHstSts &= ~(u32 & SMBHSTSTS_INT_MASK);
            break;
        case SMBSLVSTS_OFF:
            /* Bit 0 is readonly, bit 1 is reserved, bits 2..5 are write clear, bits 6..7 are reserved */
            pThis->u8SMBusSlvSts &= ~(u32 & SMBSLVSTS_WRITE_MASK);
            break;
        case SMBHSTCNT_OFF:
        {
            Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));

            const bool old_level = acpiSCILevel(pDevIns, pThis);
            pThis->u8SMBusHstCnt = u32 & SMBHSTCNT_WRITE_MASK;
            if (u32 & SMBHSTCNT_START)
            {
                /* Start, trigger error as this is a dummy implementation */
                pThis->u8SMBusHstSts |= SMBHSTSTS_DEV_ERR | SMBHSTSTS_INTER;
            }
            if (u32 & SMBHSTCNT_KILL)
            {
                /* Kill */
                pThis->u8SMBusHstSts |= SMBHSTSTS_FAILED | SMBHSTSTS_INTER;
            }
            const bool new_level = acpiSCILevel(pDevIns, pThis);

            LogFunc(("old=%x new=%x\n", old_level, new_level));

            /* This handles only SCI/IRQ9. SMI# makes not much sense today and
             * needs to be implemented later if it ever becomes relevant. */
            if (new_level != old_level)
                acpiSetIrq(pDevIns, new_level);
            break;
        }
        case SMBHSTCMD_OFF:
            pThis->u8SMBusHstCmd = u32;
            break;
        case SMBHSTADD_OFF:
            pThis->u8SMBusHstAdd = u32;
            break;
        case SMBHSTDAT0_OFF:
            pThis->u8SMBusHstDat0 = u32;
            break;
        case SMBHSTDAT1_OFF:
            pThis->u8SMBusHstDat1 = u32;
            break;
        case SMBBLKDAT_OFF:
            pThis->au8SMBusBlkDat[pThis->u8SMBusBlkIdx] = u32;
            pThis->u8SMBusBlkIdx++;
            pThis->u8SMBusBlkIdx &= sizeof(pThis->au8SMBusBlkDat) - 1;
            break;
        case SMBSLVCNT_OFF:
            pThis->u8SMBusSlvCnt = u32 & SMBSLVCNT_WRITE_MASK;
            break;
        case SMBSHDWCMD_OFF:
            /* readonly register */
            break;
        case SMBSLVEVT_OFF:
            pThis->u16SMBusSlvEvt = u32;
            break;
        case SMBSLVDAT_OFF:
            /* readonly register */
            break;
        default:
            /* caught by the sanity check above */
            ;
    }

    DEVACPI_UNLOCK(pDevIns, pThis);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, SMBus}
 */
static DECLCALLBACK(VBOXSTRICTRC)  acpiR3SMBusRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pvUser);
    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);

    VBOXSTRICTRC rc = VINF_SUCCESS;
    LogFunc(("offPort=%#x cb=%u\n", offPort, cb));
    uint8_t const off = offPort & 0x000f;
    if (   (cb != 1 && off <= SMBSHDWCMD_OFF)
        || (cb != 2 && (off == SMBSLVEVT_OFF || off == SMBSLVDAT_OFF)))
        return VERR_IOM_IOPORT_UNUSED;

    DEVACPI_LOCK_R3(pDevIns, pThis);
    switch (off)
    {
        case SMBHSTSTS_OFF:
            *pu32 = pThis->u8SMBusHstSts;
            break;
        case SMBSLVSTS_OFF:
            *pu32 = pThis->u8SMBusSlvSts;
            break;
        case SMBHSTCNT_OFF:
            pThis->u8SMBusBlkIdx = 0;
            *pu32 = pThis->u8SMBusHstCnt;
            break;
        case SMBHSTCMD_OFF:
            *pu32 = pThis->u8SMBusHstCmd;
            break;
        case SMBHSTADD_OFF:
            *pu32 = pThis->u8SMBusHstAdd;
            break;
        case SMBHSTDAT0_OFF:
            *pu32 = pThis->u8SMBusHstDat0;
            break;
        case SMBHSTDAT1_OFF:
            *pu32 = pThis->u8SMBusHstDat1;
            break;
        case SMBBLKDAT_OFF:
            *pu32 = pThis->au8SMBusBlkDat[pThis->u8SMBusBlkIdx];
            pThis->u8SMBusBlkIdx++;
            pThis->u8SMBusBlkIdx &= sizeof(pThis->au8SMBusBlkDat) - 1;
            break;
        case SMBSLVCNT_OFF:
            *pu32 = pThis->u8SMBusSlvCnt;
            break;
        case SMBSHDWCMD_OFF:
            *pu32 = pThis->u8SMBusShdwCmd;
            break;
        case SMBSLVEVT_OFF:
            *pu32 = pThis->u16SMBusSlvEvt;
            break;
        case SMBSLVDAT_OFF:
            *pu32 = pThis->u16SMBusSlvDat;
            break;
        default:
            /* caught by the sanity check above */
            rc = VERR_IOM_IOPORT_UNUSED;
    }
    DEVACPI_UNLOCK(pDevIns, pThis);

    LogFunc(("offPort=%#x u32=%#x cb=%u rc=%Rrc\n", offPort, *pu32, cb, VBOXSTRICTRC_VAL(rc)));
    return rc;
}

/**
 * Called by acpiR3Reset and acpiR3Construct to set up the SMBus PCI config space.
 *
 * @param   pDevIns     The PDM device instance.
 * @param   pThis       The ACPI shared instance data.
 */
static void acpiR3SMBusPCIBIOSFake(PPDMDEVINS pDevIns, PACPISTATE pThis)
{
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    pPciDev->abConfig[SMBBA  ] = pThis->uSMBusIoPortBase | 1; /* SMBBA, SMBus base address, bit 0 marks it as IO range */
    pPciDev->abConfig[SMBBA+1] = pThis->uSMBusIoPortBase >> 8;
    pPciDev->abConfig[SMBBA+2] = 0x00;
    pPciDev->abConfig[SMBBA+3] = 0x00;
    pPciDev->abConfig[SMBHSTCFG] = SMBHSTCFG_INTRSEL_IRQ9 << SMBHSTCFG_INTRSEL_SHIFT | SMBHSTCFG_SMB_HST_EN; /* SMBHSTCFG */
    pPciDev->abConfig[SMBSLVC] = 0x00; /* SMBSLVC */
    pPciDev->abConfig[SMBSHDW1] = 0x00; /* SMBSHDW1 */
    pPciDev->abConfig[SMBSHDW2] = 0x00; /* SMBSHDW2 */
    pPciDev->abConfig[SMBREV] = 0x00; /* SMBREV */
}

/**
 * Called by acpiR3LoadState, acpiR3Reset and acpiR3Construct to reset the SMBus device register state.
 *
 * @param   pThis           The ACPI shared instance data.
 */
static void acpiR3SMBusResetDevice(PACPISTATE pThis)
{
    pThis->u8SMBusHstSts = 0x00;
    pThis->u8SMBusSlvSts = 0x00;
    pThis->u8SMBusHstCnt = 0x00;
    pThis->u8SMBusHstCmd = 0x00;
    pThis->u8SMBusHstAdd = 0x00;
    pThis->u8SMBusHstDat0 = 0x00;
    pThis->u8SMBusHstDat1 = 0x00;
    pThis->u8SMBusSlvCnt = 0x00;
    pThis->u8SMBusShdwCmd = 0x00;
    pThis->u16SMBusSlvEvt = 0x0000;
    pThis->u16SMBusSlvDat = 0x0000;
    memset(pThis->au8SMBusBlkDat, 0x00, sizeof(pThis->au8SMBusBlkDat));
    pThis->u8SMBusBlkIdx = 0;
}

/**
 * Called by acpiR3LoadState and acpiR3UpdateSMBusHandlers to map the SMBus ports.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The ACPI shared instance data.
 */
static int acpiR3MapSMBusIoPorts(PPDMDEVINS pDevIns, PACPISTATE pThis)
{
    if (pThis->uSMBusIoPortBase != 0)
    {
        int rc = PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortSMBus, pThis->uSMBusIoPortBase);
        AssertRCReturn(rc, rc);
    }
    return VINF_SUCCESS;
}

/**
 * Called by acpiR3LoadState and acpiR3UpdateSMBusHandlers to unmap the SMBus ports.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The ACPI shared instance data.
 */
static int acpiR3UnmapSMBusPorts(PPDMDEVINS pDevIns, PACPISTATE pThis)
{
    if (pThis->uSMBusIoPortBase != 0)
    {
        int rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortSMBus);
        AssertRCReturn(rc, rc);
    }
    return VINF_SUCCESS;
}

/**
 * Called by acpiR3PciConfigWrite and acpiReset to change the location of the
 * SMBus ports.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The ACPI shared instance data.
 * @param   NewIoPortBase   The new base address of the I/O ports.
 */
static int acpiR3UpdateSMBusHandlers(PPDMDEVINS pDevIns, PACPISTATE pThis, RTIOPORT NewIoPortBase)
{
    Log(("acpi: rebasing SMBus 0x%x -> 0x%x\n", pThis->uSMBusIoPortBase, NewIoPortBase));
    if (NewIoPortBase != pThis->uSMBusIoPortBase)
    {
        int rc = acpiR3UnmapSMBusPorts(pDevIns, pThis);
        AssertRCReturn(rc, rc);

        pThis->uSMBusIoPortBase = NewIoPortBase;

        rc = acpiR3MapSMBusIoPorts(pDevIns, pThis);
        AssertRCReturn(rc, rc);

#if 0 /* is there an FADT table entry for the SMBus base? */
        /* We have to update FADT table acccording to the new base */
        rc = acpiR3PlantTables(pThis);
        AssertRC(rc);
        if (RT_FAILURE(rc))
            return rc;
#endif
    }

    return VINF_SUCCESS;
}


/**
 * Saved state structure description, version 4.
 */
static const SSMFIELD g_AcpiSavedStateFields4[] =
{
    SSMFIELD_ENTRY(ACPISTATE, pm1a_en),
    SSMFIELD_ENTRY(ACPISTATE, pm1a_sts),
    SSMFIELD_ENTRY(ACPISTATE, pm1a_ctl),
    SSMFIELD_ENTRY(ACPISTATE, u64PmTimerInitial),
    SSMFIELD_ENTRY(ACPISTATE, gpe0_en),
    SSMFIELD_ENTRY(ACPISTATE, gpe0_sts),
    SSMFIELD_ENTRY(ACPISTATE, uBatteryIndex),
    SSMFIELD_ENTRY(ACPISTATE, uSystemInfoIndex),
    SSMFIELD_ENTRY(ACPISTATE, u64RamSize),
    SSMFIELD_ENTRY(ACPISTATE, u8IndexShift),
    SSMFIELD_ENTRY(ACPISTATE, u8UseIOApic),
    SSMFIELD_ENTRY(ACPISTATE, uSleepState),
    SSMFIELD_ENTRY_TERM()
};

/**
 * Saved state structure description, version 5.
 */
static const SSMFIELD g_AcpiSavedStateFields5[] =
{
    SSMFIELD_ENTRY(ACPISTATE, pm1a_en),
    SSMFIELD_ENTRY(ACPISTATE, pm1a_sts),
    SSMFIELD_ENTRY(ACPISTATE, pm1a_ctl),
    SSMFIELD_ENTRY(ACPISTATE, u64PmTimerInitial),
    SSMFIELD_ENTRY(ACPISTATE, gpe0_en),
    SSMFIELD_ENTRY(ACPISTATE, gpe0_sts),
    SSMFIELD_ENTRY(ACPISTATE, uBatteryIndex),
    SSMFIELD_ENTRY(ACPISTATE, uSystemInfoIndex),
    SSMFIELD_ENTRY(ACPISTATE, uSleepState),
    SSMFIELD_ENTRY(ACPISTATE, u8IndexShift),
    SSMFIELD_ENTRY(ACPISTATE, uPmIoPortBase),
    SSMFIELD_ENTRY_TERM()
};

/**
 * Saved state structure description, version 6.
 */
static const SSMFIELD g_AcpiSavedStateFields6[] =
{
    SSMFIELD_ENTRY(ACPISTATE, pm1a_en),
    SSMFIELD_ENTRY(ACPISTATE, pm1a_sts),
    SSMFIELD_ENTRY(ACPISTATE, pm1a_ctl),
    SSMFIELD_ENTRY(ACPISTATE, u64PmTimerInitial),
    SSMFIELD_ENTRY(ACPISTATE, gpe0_en),
    SSMFIELD_ENTRY(ACPISTATE, gpe0_sts),
    SSMFIELD_ENTRY(ACPISTATE, uBatteryIndex),
    SSMFIELD_ENTRY(ACPISTATE, uSystemInfoIndex),
    SSMFIELD_ENTRY(ACPISTATE, uSleepState),
    SSMFIELD_ENTRY(ACPISTATE, u8IndexShift),
    SSMFIELD_ENTRY(ACPISTATE, uPmIoPortBase),
    SSMFIELD_ENTRY(ACPISTATE, fSuspendToSavedState),
    SSMFIELD_ENTRY_TERM()
};

/**
 * Saved state structure description, version 7.
 */
static const SSMFIELD g_AcpiSavedStateFields7[] =
{
    SSMFIELD_ENTRY(ACPISTATE, pm1a_en),
    SSMFIELD_ENTRY(ACPISTATE, pm1a_sts),
    SSMFIELD_ENTRY(ACPISTATE, pm1a_ctl),
    SSMFIELD_ENTRY(ACPISTATE, u64PmTimerInitial),
    SSMFIELD_ENTRY(ACPISTATE, uPmTimerVal),
    SSMFIELD_ENTRY(ACPISTATE, gpe0_en),
    SSMFIELD_ENTRY(ACPISTATE, gpe0_sts),
    SSMFIELD_ENTRY(ACPISTATE, uBatteryIndex),
    SSMFIELD_ENTRY(ACPISTATE, uSystemInfoIndex),
    SSMFIELD_ENTRY(ACPISTATE, uSleepState),
    SSMFIELD_ENTRY(ACPISTATE, u8IndexShift),
    SSMFIELD_ENTRY(ACPISTATE, uPmIoPortBase),
    SSMFIELD_ENTRY(ACPISTATE, fSuspendToSavedState),
    SSMFIELD_ENTRY_TERM()
};

/**
 * Saved state structure description, version 8.
 */
static const SSMFIELD g_AcpiSavedStateFields8[] =
{
    SSMFIELD_ENTRY(ACPISTATE, pm1a_en),
    SSMFIELD_ENTRY(ACPISTATE, pm1a_sts),
    SSMFIELD_ENTRY(ACPISTATE, pm1a_ctl),
    SSMFIELD_ENTRY(ACPISTATE, u64PmTimerInitial),
    SSMFIELD_ENTRY(ACPISTATE, uPmTimerVal),
    SSMFIELD_ENTRY(ACPISTATE, gpe0_en),
    SSMFIELD_ENTRY(ACPISTATE, gpe0_sts),
    SSMFIELD_ENTRY(ACPISTATE, uBatteryIndex),
    SSMFIELD_ENTRY(ACPISTATE, uSystemInfoIndex),
    SSMFIELD_ENTRY(ACPISTATE, uSleepState),
    SSMFIELD_ENTRY(ACPISTATE, u8IndexShift),
    SSMFIELD_ENTRY(ACPISTATE, uPmIoPortBase),
    SSMFIELD_ENTRY(ACPISTATE, fSuspendToSavedState),
    SSMFIELD_ENTRY(ACPISTATE, uSMBusIoPortBase),
    SSMFIELD_ENTRY(ACPISTATE, u8SMBusHstSts),
    SSMFIELD_ENTRY(ACPISTATE, u8SMBusSlvSts),
    SSMFIELD_ENTRY(ACPISTATE, u8SMBusHstCnt),
    SSMFIELD_ENTRY(ACPISTATE, u8SMBusHstCmd),
    SSMFIELD_ENTRY(ACPISTATE, u8SMBusHstAdd),
    SSMFIELD_ENTRY(ACPISTATE, u8SMBusHstDat0),
    SSMFIELD_ENTRY(ACPISTATE, u8SMBusHstDat1),
    SSMFIELD_ENTRY(ACPISTATE, u8SMBusSlvCnt),
    SSMFIELD_ENTRY(ACPISTATE, u8SMBusShdwCmd),
    SSMFIELD_ENTRY(ACPISTATE, u16SMBusSlvEvt),
    SSMFIELD_ENTRY(ACPISTATE, u16SMBusSlvDat),
    SSMFIELD_ENTRY(ACPISTATE, au8SMBusBlkDat),
    SSMFIELD_ENTRY(ACPISTATE, u8SMBusBlkIdx),
    SSMFIELD_ENTRY_TERM()
};

/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) acpiR3SaveState(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PACPISTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    return pHlp->pfnSSMPutStruct(pSSM, pThis, &g_AcpiSavedStateFields8[0]);
}

/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) acpiR3LoadState(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PACPISTATE      pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    PACPISTATER3    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PACPISTATER3);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    /*
     * Unmap PM I/O ports, will remap it with the actual base after state
     * successfully loaded.
     */
    int rc = acpiR3UnmapPmIoPorts(pDevIns, pThis);
    AssertRCReturn(rc, rc);

    /*
     * Unregister SMBus handlers, will register with actual base after state
     * successfully loaded.
     */
    rc = acpiR3UnmapSMBusPorts(pDevIns, pThis);
    AssertRCReturn(rc, rc);
    acpiR3SMBusResetDevice(pThis);

    switch (uVersion)
    {
        case 4:
            rc = pHlp->pfnSSMGetStruct(pSSM, pThis, &g_AcpiSavedStateFields4[0]);
            break;
        case 5:
            rc = pHlp->pfnSSMGetStruct(pSSM, pThis, &g_AcpiSavedStateFields5[0]);
            break;
        case 6:
            rc = pHlp->pfnSSMGetStruct(pSSM, pThis, &g_AcpiSavedStateFields6[0]);
            break;
        case 7:
            rc = pHlp->pfnSSMGetStruct(pSSM, pThis, &g_AcpiSavedStateFields7[0]);
            break;
        case 8:
            rc = pHlp->pfnSSMGetStruct(pSSM, pThis, &g_AcpiSavedStateFields8[0]);
            break;
        default:
            rc = VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
            break;
    }
    if (RT_SUCCESS(rc))
    {
        AssertLogRelMsgReturn(pThis->u8SMBusBlkIdx < RT_ELEMENTS(pThis->au8SMBusBlkDat),
                              ("%#x\n", pThis->u8SMBusBlkIdx), VERR_SSM_LOAD_CONFIG_MISMATCH);
        rc = acpiR3MapPmIoPorts(pDevIns, pThis);
        AssertRCReturn(rc, rc);
        rc = acpiR3MapSMBusIoPorts(pDevIns, pThis);
        AssertRCReturn(rc, rc);
        rc = acpiR3FetchBatteryStatus(pThis, pThisCC);
        AssertRCReturn(rc, rc);
        rc = acpiR3FetchBatteryInfo(pThis);
        AssertRCReturn(rc, rc);

        PDMDevHlpTimerLockClock(pDevIns, pThis->hPmTimer, VERR_IGNORED);
        DEVACPI_LOCK_R3(pDevIns, pThis);
        uint64_t u64Now = PDMDevHlpTimerGet(pDevIns, pThis->hPmTimer);
        /* The interrupt may be incorrectly re-generated if the state is restored from versions < 7. */
        acpiPmTimerUpdate(pDevIns, pThis, u64Now);
        acpiR3PmTimerReset(pDevIns, pThis, u64Now);
        DEVACPI_UNLOCK(pDevIns, pThis);
        PDMDevHlpTimerUnlockClock(pDevIns, pThis->hPmTimer);
    }
    return rc;
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) acpiR3QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PACPISTATER3 pThisCC = RT_FROM_MEMBER(pInterface, ACPISTATER3, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIACPIPORT, &pThisCC->IACPIPort);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIEVENTBUTTONPORT, &pThisCC->IButtonEventPort);
    return NULL;
}

/**
 * Calculate the check sum for some ACPI data before planting it.
 *
 * All the bytes must add up to 0.
 *
 * @returns check sum.
 * @param   pvSrc       What to check sum.
 * @param   cbData      The amount of data to checksum.
 */
static uint8_t acpiR3Checksum(const void * const pvSrc, size_t cbData)
{
    uint8_t const *pbSrc = (uint8_t const *)pvSrc;
    uint8_t uSum = 0;
    for (size_t i = 0; i < cbData; ++i)
        uSum += pbSrc[i];
    return -uSum;
}

/**
 * Prepare a ACPI table header.
 */
static void acpiR3PrepareHeader(PACPISTATE pThis, ACPITBLHEADER *header,
                                const char au8Signature[4],
                                uint32_t u32Length, uint8_t u8Revision)
{
    memcpy(header->au8Signature, au8Signature, 4);
    header->u32Length             = RT_H2LE_U32(u32Length);
    header->u8Revision            = u8Revision;
    memcpy(header->au8OemId, pThis->au8OemId, 6);
    memcpy(header->au8OemTabId, "VBOX", 4);
    memcpy(header->au8OemTabId+4, au8Signature, 4);
    header->u32OemRevision        = RT_H2LE_U32(1);
    memcpy(header->au8CreatorId, pThis->au8CreatorId, 4);
    header->u32CreatorRev         = pThis->u32CreatorRev;
}

/**
 * Initialize a generic address structure (ACPIGENADDR).
 */
static void acpiR3WriteGenericAddr(ACPIGENADDR *g, uint8_t u8AddressSpaceId,
                                   uint8_t u8RegisterBitWidth, uint8_t u8RegisterBitOffset,
                                   uint8_t u8AccessSize, uint64_t u64Address)
{
    g->u8AddressSpaceId    = u8AddressSpaceId;
    g->u8RegisterBitWidth  = u8RegisterBitWidth;
    g->u8RegisterBitOffset = u8RegisterBitOffset;
    g->u8AccessSize        = u8AccessSize;
    g->u64Address          = RT_H2LE_U64(u64Address);
}

/**
 * Wrapper around PDMDevHlpPhysWrite used when planting ACPI tables.
 */
DECLINLINE(void) acpiR3PhysCopy(PPDMDEVINS pDevIns, RTGCPHYS32 GCPhys32Dst, const void *pvSrc, size_t cbToCopy)
{
    PDMDevHlpPhysWrite(pDevIns, GCPhys32Dst, pvSrc, cbToCopy);
}

/**
 * Plant the Differentiated System Description Table (DSDT).
 */
static void acpiR3SetupDsdt(PPDMDEVINS pDevIns, RTGCPHYS32 GCPhys32, void const *pvSrc, size_t cbDsdt)
{
    acpiR3PhysCopy(pDevIns, GCPhys32, pvSrc, cbDsdt);
}

/**
 * Plant the Secondary System Description Table (SSDT).
 */
static void acpiR3SetupSsdt(PPDMDEVINS pDevIns, RTGCPHYS32 addr, void const *pvSrc, size_t uSsdtLen)
{
    acpiR3PhysCopy(pDevIns, addr, pvSrc, uSsdtLen);
}

#ifdef VBOX_WITH_TPM
/**
 * Plant the Secondary System Description Table (SSDT).
 */
static void acpiR3SetupTpmSsdt(PPDMDEVINS pDevIns, RTGCPHYS32 addr, void const *pvSrc, size_t uSsdtLen)
{
    acpiR3PhysCopy(pDevIns, addr, pvSrc, uSsdtLen);
}
#endif

/**
 * Plant the Firmware ACPI Control Structure (FACS).
 */
static void acpiR3SetupFacs(PPDMDEVINS pDevIns, RTGCPHYS32 addr)
{
    ACPITBLFACS facs;

    memset(&facs, 0, sizeof(facs));
    memcpy(facs.au8Signature, "FACS", 4);
    facs.u32Length            = RT_H2LE_U32(sizeof(ACPITBLFACS));
    facs.u32HWSignature       = RT_H2LE_U32(0);
    facs.u32FWVector          = RT_H2LE_U32(0);
    facs.u32GlobalLock        = RT_H2LE_U32(0);
    facs.u32Flags             = RT_H2LE_U32(0);
    facs.u64X_FWVector        = RT_H2LE_U64(0);
    facs.u8Version            = 1;

    acpiR3PhysCopy(pDevIns, addr, (const uint8_t *)&facs, sizeof(facs));
}

/**
 * Plant the Fixed ACPI Description Table (FADT aka FACP).
 */
static void acpiR3SetupFadt(PPDMDEVINS pDevIns, PACPISTATE pThis, RTGCPHYS32 GCPhysAcpi1, RTGCPHYS32 GCPhysAcpi2,
                            RTGCPHYS32 GCPhysFacs, RTGCPHYS GCPhysDsdt)
{
    ACPITBLFADT fadt;

    /* First the ACPI version 2+ version of the structure. */
    memset(&fadt, 0, sizeof(fadt));
    acpiR3PrepareHeader(pThis, &fadt.header, "FACP", sizeof(fadt), 4);
    fadt.u32FACS              = RT_H2LE_U32(GCPhysFacs);
    fadt.u32DSDT              = RT_H2LE_U32(GCPhysDsdt);
    fadt.u8IntModel           = 0;  /* dropped from the ACPI 2.0 spec. */
    fadt.u8PreferredPMProfile = 0;  /* unspecified */
    fadt.u16SCIInt            = RT_H2LE_U16(SCI_INT);
    fadt.u32SMICmd            = RT_H2LE_U32(SMI_CMD);
    fadt.u8AcpiEnable         = ACPI_ENABLE;
    fadt.u8AcpiDisable        = ACPI_DISABLE;
    fadt.u8S4BIOSReq          = 0;
    fadt.u8PStateCnt          = 0;
    fadt.u32PM1aEVTBLK        = RT_H2LE_U32(acpiR3CalcPmPort(pThis, PM1a_EVT_OFFSET));
    fadt.u32PM1bEVTBLK        = RT_H2LE_U32(acpiR3CalcPmPort(pThis, PM1b_EVT_OFFSET));
    fadt.u32PM1aCTLBLK        = RT_H2LE_U32(acpiR3CalcPmPort(pThis, PM1a_CTL_OFFSET));
    fadt.u32PM1bCTLBLK        = RT_H2LE_U32(acpiR3CalcPmPort(pThis, PM1b_CTL_OFFSET));
    fadt.u32PM2CTLBLK         = RT_H2LE_U32(acpiR3CalcPmPort(pThis, PM2_CTL_OFFSET));
    fadt.u32PMTMRBLK          = RT_H2LE_U32(acpiR3CalcPmPort(pThis, PM_TMR_OFFSET));
    fadt.u32GPE0BLK           = RT_H2LE_U32(acpiR3CalcPmPort(pThis, GPE0_OFFSET));
    fadt.u32GPE1BLK           = RT_H2LE_U32(acpiR3CalcPmPort(pThis, GPE1_OFFSET));
    fadt.u8PM1EVTLEN          = 4;
    fadt.u8PM1CTLLEN          = 2;
    fadt.u8PM2CTLLEN          = 0;
    fadt.u8PMTMLEN            = 4;
    fadt.u8GPE0BLKLEN         = GPE0_BLK_LEN;
    fadt.u8GPE1BLKLEN         = GPE1_BLK_LEN;
    fadt.u8GPE1BASE           = GPE1_BASE;
    fadt.u8CSTCNT             = 0;
    fadt.u16PLVL2LAT          = RT_H2LE_U16(P_LVL2_LAT);
    fadt.u16PLVL3LAT          = RT_H2LE_U16(P_LVL3_LAT);
    fadt.u16FlushSize         = RT_H2LE_U16(FLUSH_SIZE);
    fadt.u16FlushStride       = RT_H2LE_U16(FLUSH_STRIDE);
    fadt.u8DutyOffset         = 0;
    fadt.u8DutyWidth          = 0;
    fadt.u8DayAlarm           = 0;
    fadt.u8MonAlarm           = 0;
    fadt.u8Century            = 0;
    fadt.u16IAPCBOOTARCH      = RT_H2LE_U16(IAPC_BOOT_ARCH_LEGACY_DEV | IAPC_BOOT_ARCH_8042);
    /** @note WBINVD is required for ACPI versions newer than 1.0 */
    fadt.u32Flags             = RT_H2LE_U32(  FADT_FL_WBINVD
                                            | FADT_FL_FIX_RTC
                                            | FADT_FL_TMR_VAL_EXT
                                            | FADT_FL_RESET_REG_SUP);

    /* We have to force physical APIC mode or Linux can't use more than 8 CPUs */
    if (pThis->fCpuHotPlug)
        fadt.u32Flags |= RT_H2LE_U32(FADT_FL_FORCE_APIC_PHYS_DEST_MODE);

    acpiR3WriteGenericAddr(&fadt.ResetReg,     1,  8, 0, 1, ACPI_RESET_BLK);
    fadt.u8ResetVal           = ACPI_RESET_REG_VAL;
    fadt.u64XFACS             = RT_H2LE_U64((uint64_t)GCPhysFacs);
    fadt.u64XDSDT             = RT_H2LE_U64((uint64_t)GCPhysDsdt);
    acpiR3WriteGenericAddr(&fadt.X_PM1aEVTBLK, 1, 32, 0, 2, acpiR3CalcPmPort(pThis, PM1a_EVT_OFFSET));
    acpiR3WriteGenericAddr(&fadt.X_PM1bEVTBLK, 0,  0, 0, 0, acpiR3CalcPmPort(pThis, PM1b_EVT_OFFSET));
    acpiR3WriteGenericAddr(&fadt.X_PM1aCTLBLK, 1, 16, 0, 2, acpiR3CalcPmPort(pThis, PM1a_CTL_OFFSET));
    acpiR3WriteGenericAddr(&fadt.X_PM1bCTLBLK, 0,  0, 0, 0, acpiR3CalcPmPort(pThis, PM1b_CTL_OFFSET));
    acpiR3WriteGenericAddr(&fadt.X_PM2CTLBLK,  0,  0, 0, 0, acpiR3CalcPmPort(pThis, PM2_CTL_OFFSET));
    acpiR3WriteGenericAddr(&fadt.X_PMTMRBLK,   1, 32, 0, 3, acpiR3CalcPmPort(pThis, PM_TMR_OFFSET));
    acpiR3WriteGenericAddr(&fadt.X_GPE0BLK,    1, 16, 0, 1, acpiR3CalcPmPort(pThis, GPE0_OFFSET));
    acpiR3WriteGenericAddr(&fadt.X_GPE1BLK,    0,  0, 0, 0, acpiR3CalcPmPort(pThis, GPE1_OFFSET));
    fadt.header.u8Checksum    = acpiR3Checksum(&fadt, sizeof(fadt));
    acpiR3PhysCopy(pDevIns, GCPhysAcpi2, &fadt, sizeof(fadt));

    /* Now the ACPI 1.0 version. */
    fadt.header.u32Length     = ACPITBLFADT_VERSION1_SIZE;
    fadt.u8IntModel           = INT_MODEL_DUAL_PIC;
    fadt.header.u8Checksum    = 0;  /* Must be zeroed before recalculating checksum! */
    fadt.header.u8Checksum    = acpiR3Checksum(&fadt, ACPITBLFADT_VERSION1_SIZE);
    acpiR3PhysCopy(pDevIns, GCPhysAcpi1, &fadt, ACPITBLFADT_VERSION1_SIZE);
}

/**
 * Plant the root System Description Table.
 *
 * The RSDT and XSDT tables are basically identical. The only difference is 32
 * vs 64 bits addresses for description headers. RSDT is for ACPI 1.0. XSDT for
 * ACPI 2.0 and up.
 */
static int acpiR3SetupRsdt(PPDMDEVINS pDevIns, PACPISTATE pThis, RTGCPHYS32 addr, unsigned int nb_entries, uint32_t *addrs)
{
    ACPITBLRSDT *rsdt;
    const size_t size = sizeof(ACPITBLHEADER) + nb_entries * sizeof(rsdt->u32Entry[0]);

    rsdt = (ACPITBLRSDT*)RTMemAllocZ(size);
    if (!rsdt)
        return PDMDEV_SET_ERROR(pDevIns, VERR_NO_TMP_MEMORY, N_("Cannot allocate RSDT"));

    acpiR3PrepareHeader(pThis, &rsdt->header, "RSDT", (uint32_t)size, 1);
    for (unsigned int i = 0; i < nb_entries; ++i)
    {
        rsdt->u32Entry[i] = RT_H2LE_U32(addrs[i]);
        Log(("Setup RSDT: [%d] = %x\n", i, rsdt->u32Entry[i]));
    }
    rsdt->header.u8Checksum = acpiR3Checksum(rsdt, size);
    acpiR3PhysCopy(pDevIns, addr, rsdt, size);
    RTMemFree(rsdt);
    return VINF_SUCCESS;
}

/**
 * Plant the Extended System Description Table.
 */
static int acpiR3SetupXsdt(PPDMDEVINS pDevIns, PACPISTATE pThis, RTGCPHYS32 addr, unsigned int nb_entries, uint32_t *addrs)
{
    ACPITBLXSDT *xsdt;
    const size_t cbXsdt = sizeof(ACPITBLHEADER) + nb_entries * sizeof(xsdt->u64Entry[0]);
    xsdt = (ACPITBLXSDT *)RTMemAllocZ(cbXsdt);
    if (!xsdt)
        return VERR_NO_TMP_MEMORY;

    acpiR3PrepareHeader(pThis, &xsdt->header, "XSDT", (uint32_t)cbXsdt, 1 /* according to ACPI 3.0 specs */);

    if (pThis->cCustTbls > 0)
        memcpy(xsdt->header.au8OemTabId, pThis->au8OemTabId, 8);

    for (unsigned int i = 0; i < nb_entries; ++i)
    {
        xsdt->u64Entry[i] = RT_H2LE_U64((uint64_t)addrs[i]);
        Log(("Setup XSDT: [%d] = %RX64\n", i, xsdt->u64Entry[i]));
    }
    xsdt->header.u8Checksum = acpiR3Checksum(xsdt, cbXsdt);
    acpiR3PhysCopy(pDevIns, addr, xsdt, cbXsdt);

    RTMemFree(xsdt);
    return VINF_SUCCESS;
}

/**
 * Plant the Root System Description Pointer (RSDP).
 */
static void acpiR3SetupRsdp(PACPISTATE pThis, ACPITBLRSDP *rsdp, RTGCPHYS32 GCPhysRsdt, RTGCPHYS GCPhysXsdt)
{
    memset(rsdp, 0, sizeof(*rsdp));

    /* ACPI 1.0 part (RSDT) */
    memcpy(rsdp->au8Signature, "RSD PTR ", 8);
    memcpy(rsdp->au8OemId, pThis->au8OemId, 6);
    rsdp->u8Revision    = ACPI_REVISION;
    rsdp->u32RSDT       = RT_H2LE_U32(GCPhysRsdt);
    rsdp->u8Checksum    = acpiR3Checksum(rsdp, RT_OFFSETOF(ACPITBLRSDP, u32Length));

    /* ACPI 2.0 part (XSDT) */
    rsdp->u32Length     = RT_H2LE_U32(sizeof(ACPITBLRSDP));
    rsdp->u64XSDT       = RT_H2LE_U64(GCPhysXsdt);
    rsdp->u8ExtChecksum = acpiR3Checksum(rsdp, sizeof(ACPITBLRSDP));
}

/**
 * Multiple APIC Description Table.
 *
 * This structure looks somewhat convoluted due layout of MADT table in MP case.
 * There extpected to be multiple LAPIC records for each CPU, thus we cannot
 * use regular C structure and proxy to raw memory instead.
 */
class AcpiTableMadt
{
    /**
     * All actual data stored in dynamically allocated memory pointed by this field.
     */
    uint8_t            *m_pbData;
    /**
     * Number of CPU entries in this MADT.
     */
    uint32_t            m_cCpus;

    /**
     * Number of interrupt overrides.
     */
     uint32_t            m_cIsos;

public:
    /**
     * Address of ACPI header
     */
    inline ACPITBLHEADER *header_addr(void) const
    {
        return (ACPITBLHEADER *)m_pbData;
    }

    /**
     * Address of local APIC for each CPU. Note that different CPUs address different LAPICs,
     * although address is the same for all of them.
     */
    inline uint32_t *u32LAPIC_addr(void) const
    {
        return (uint32_t *)(header_addr() + 1);
    }

    /**
     * Address of APIC flags
     */
    inline uint32_t *u32Flags_addr(void) const
    {
        return (uint32_t *)(u32LAPIC_addr() + 1);
    }

    /**
     * Address of ISO description
     */
    inline ACPITBLISO *ISO_addr(void) const
    {
        return (ACPITBLISO *)(u32Flags_addr() + 1);
    }

    /**
     * Address of per-CPU LAPIC descriptions
     */
    inline ACPITBLLAPIC *LApics_addr(void) const
    {
        return (ACPITBLLAPIC *)(ISO_addr() + m_cIsos);
    }

    /**
     * Address of IO APIC description
     */
    inline ACPITBLIOAPIC *IOApic_addr(void) const
    {
        return (ACPITBLIOAPIC *)(LApics_addr() + m_cCpus);
    }

    /**
     * Size of MADT.
     * Note that this function assumes IOApic to be the last field in structure.
     */
    inline uint32_t size(void) const
    {
        return (uint8_t *)(IOApic_addr() + 1) - (uint8_t *)header_addr();
    }

    /**
     * Raw data of MADT.
     */
    inline const uint8_t *data(void) const
    {
        return m_pbData;
    }

    /**
     * Size of MADT for given ACPI config, useful to compute layout.
     */
    static uint32_t sizeFor(PACPISTATE pThis, uint32_t cIsos)
    {
        return AcpiTableMadt(pThis->cCpus, cIsos).size();
    }

    /*
     * Constructor, only works in Ring 3, doesn't look like a big deal.
     */
    AcpiTableMadt(uint32_t cCpus, uint32_t cIsos)
    {
        m_cCpus  = cCpus;
        m_cIsos  = cIsos;
        m_pbData = NULL;                /* size() uses this and gcc will complain if not initialized. */
        uint32_t cb = size();
        m_pbData = (uint8_t *)RTMemAllocZ(cb);
    }

    ~AcpiTableMadt()
    {
        RTMemFree(m_pbData);
    }
};


/**
 * Plant the Multiple APIC Description Table (MADT).
 *
 * @note    APIC without IO-APIC hangs Windows Vista therefore we setup both.
 *
 * @todo    All hardcoded, should set this up based on the actual VM config!!!!!
 */
static void acpiR3SetupMadt(PPDMDEVINS pDevIns, PACPISTATE pThis, RTGCPHYS32 addr)
{
    uint16_t cpus = pThis->cCpus;
    AcpiTableMadt madt(cpus, NUMBER_OF_IRQ_SOURCE_OVERRIDES);

    acpiR3PrepareHeader(pThis, madt.header_addr(), "APIC", madt.size(), 2);

    *madt.u32LAPIC_addr()          = RT_H2LE_U32(0xfee00000);
    *madt.u32Flags_addr()          = RT_H2LE_U32(PCAT_COMPAT);

    /* LAPICs records */
    ACPITBLLAPIC* lapic = madt.LApics_addr();
    for (uint16_t i = 0; i < cpus; i++)
    {
        lapic->u8Type      = 0;
        lapic->u8Length    = sizeof(ACPITBLLAPIC);
        lapic->u8ProcId    = i;
        /** Must match numbering convention in MPTABLES */
        lapic->u8ApicId    = i;
        lapic->u32Flags    = VMCPUSET_IS_PRESENT(&pThis->CpuSetAttached, i) ? RT_H2LE_U32(LAPIC_ENABLED) : 0;
        lapic++;
    }

    /* IO-APIC record */
    ACPITBLIOAPIC* ioapic = madt.IOApic_addr();
    ioapic->u8Type     = 1;
    ioapic->u8Length   = sizeof(ACPITBLIOAPIC);
    /** Must match MP tables ID */
    ioapic->u8IOApicId = cpus;
    ioapic->u8Reserved = 0;
    ioapic->u32Address = RT_H2LE_U32(0xfec00000);
    ioapic->u32GSIB    = RT_H2LE_U32(0);

    /* Interrupt Source Overrides */
    /* Flags:
        bits[3:2]:
          00 conforms to the bus
          01 edge-triggered
          10 reserved
          11 level-triggered
        bits[1:0]
          00 conforms to the bus
          01 active-high
          10 reserved
          11 active-low */
    /* If changing, also update PDMIsaSetIrq() and MPS */
    ACPITBLISO* isos = madt.ISO_addr();
    /* Timer interrupt rule IRQ0 to GSI2 */
    isos[0].u8Type     = 2;
    isos[0].u8Length   = sizeof(ACPITBLISO);
    isos[0].u8Bus      = 0; /* Must be 0 */
    isos[0].u8Source   = 0; /* IRQ0 */
    isos[0].u32GSI     = 2; /* connected to pin 2 */
    isos[0].u16Flags   = 0; /* conform to the bus */

    /* ACPI interrupt rule - IRQ9 to GSI9 */
    isos[1].u8Type     = 2;
    isos[1].u8Length   = sizeof(ACPITBLISO);
    isos[1].u8Bus      = 0; /* Must be 0 */
    isos[1].u8Source   = 9; /* IRQ9 */
    isos[1].u32GSI     = 9; /* connected to pin 9 */
    isos[1].u16Flags   = 0xf; /* active low, level triggered */
    Assert(NUMBER_OF_IRQ_SOURCE_OVERRIDES == 2);

    madt.header_addr()->u8Checksum = acpiR3Checksum(madt.data(), madt.size());
    acpiR3PhysCopy(pDevIns, addr, madt.data(), madt.size());
}

/**
 * Plant the High Performance Event Timer (HPET) descriptor.
 */
static void acpiR3SetupHpet(PPDMDEVINS pDevIns, PACPISTATE pThis, RTGCPHYS32 addr)
{
    ACPITBLHPET hpet;

    memset(&hpet, 0, sizeof(hpet));

    acpiR3PrepareHeader(pThis, &hpet.aHeader, "HPET", sizeof(hpet), 1);
    /* Keep base address consistent with appropriate DSDT entry  (vbox.dsl) */
    acpiR3WriteGenericAddr(&hpet.HpetAddr,
                         0  /* Memory address space */,
                         64 /* Register bit width */,
                         0  /* Bit offset */,
                         0, /* Register access size, is it correct? */
                         0xfed00000 /* Address */);

    hpet.u32Id        = 0x8086a201; /* must match what HPET ID returns, is it correct ? */
    hpet.u32Number    = 0;
    hpet.u32MinTick   = 4096;
    hpet.u8Attributes = 0;

    hpet.aHeader.u8Checksum = acpiR3Checksum(&hpet, sizeof(hpet));

    acpiR3PhysCopy(pDevIns, addr, (const uint8_t *)&hpet, sizeof(hpet));
}


#ifdef VBOX_WITH_IOMMU_AMD
/**
 * Plant the AMD IOMMU descriptor.
 */
static void acpiR3SetupIommuAmd(PPDMDEVINS pDevIns, PACPISTATE pThis, RTGCPHYS32 addr)
{
    ACPITBLIOMMU Ivrs;
    RT_ZERO(Ivrs);

    uint16_t const uIommuBus = 0;
    uint16_t const uIommuDev = RT_HI_U16(pThis->u32IommuPciAddress);
    uint16_t const uIommuFn  = RT_LO_U16(pThis->u32IommuPciAddress);

    /* IVRS header. */
    acpiR3PrepareHeader(pThis, &Ivrs.Hdr.header, "IVRS", sizeof(Ivrs), ACPI_IVRS_FMT_REV_FIXED);
    /* NOTE! The values here must match what we expose via MMIO/PCI config. space in the IOMMU device code. */
    Ivrs.Hdr.u32IvInfo = RT_BF_MAKE(ACPI_IVINFO_BF_EFR_SUP,       1)
                       | RT_BF_MAKE(ACPI_IVINFO_BF_DMA_REMAP_SUP, 0)   /* Pre-boot DMA remap support not supported. */
                       | RT_BF_MAKE(ACPI_IVINFO_BF_GVA_SIZE,      2)   /* Guest Virt. Addr size (2=48 bits) */
                       | RT_BF_MAKE(ACPI_IVINFO_BF_PA_SIZE,      48)   /* Physical Addr size (48 bits) */
                       | RT_BF_MAKE(ACPI_IVINFO_BF_VA_SIZE,      64)   /* Virt. Addr size (64 bits) */
                       | RT_BF_MAKE(ACPI_IVINFO_BF_HT_ATS_RESV,   0);  /* ATS response range reserved (only applicable for HT) */

    /* IVHD type 10 definition block. */
    Ivrs.IvhdType10.u8Type             = 0x10;
    Ivrs.IvhdType10.u16Length          = sizeof(Ivrs.IvhdType10)
                                       + sizeof(Ivrs.IvhdType10Start)
                                       + sizeof(Ivrs.IvhdType10End)
                                       + sizeof(Ivrs.IvhdType10Rsvd0)
                                       + sizeof(Ivrs.IvhdType10Rsvd1)
                                       + sizeof(Ivrs.IvhdType10IoApic)
                                       + sizeof(Ivrs.IvhdType10Hpet);
    Ivrs.IvhdType10.u16DeviceId        = PCIBDF_MAKE(uIommuBus, VBOX_PCI_DEVFN_MAKE(uIommuDev, uIommuFn));
    Ivrs.IvhdType10.u16CapOffset       = IOMMU_PCI_OFF_CAP_HDR;
    Ivrs.IvhdType10.u64BaseAddress     = IOMMU_MMIO_BASE_ADDR;
    Ivrs.IvhdType10.u16PciSegmentGroup = 0;
    /* NOTE! Subfields in the following fields must match any corresponding field in PCI/MMIO registers of the IOMMU device. */
    Ivrs.IvhdType10.u8Flags            = ACPI_IVHD_10H_F_COHERENT; /* Remote IOTLB etc. not supported. */
    Ivrs.IvhdType10.u16IommuInfo       = RT_BF_MAKE(ACPI_IOMMU_INFO_BF_MSI_NUM, 0)
                                       | RT_BF_MAKE(ACPI_IOMMU_INFO_BF_UNIT_ID, 0);
    Ivrs.IvhdType10.u32Features        = RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_XT_SUP,      0)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_NX_SUP,      0)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_GT_SUP,      0)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_GLX_SUP,     0)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_IA_SUP,      1)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_GA_SUP,      0)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_HE_SUP,      1)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_PAS_MAX,     0)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_PN_COUNTERS, 0)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_PN_BANKS,    0)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_PN_COUNTERS, 0)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_MSI_NUM_PPR, 0)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_GATS,        0)
                                       | RT_BF_MAKE(ACPI_IOMMU_FEAT_BF_HATS,        IOMMU_MAX_HOST_PT_LEVEL & 3);
    /* Start range from BDF (00:01:00). */
    Ivrs.IvhdType10Start.u8DevEntryType = ACPI_IVHD_DEVENTRY_TYPE_START_RANGE;
    Ivrs.IvhdType10Start.u16DevId       = PCIBDF_MAKE(0, VBOX_PCI_DEVFN_MAKE(1, 0));
    Ivrs.IvhdType10Start.u8DteSetting   = 0;
    /* End range at BDF (ff:1f:7). */
    Ivrs.IvhdType10End.u8DevEntryType   = ACPI_IVHD_DEVENTRY_TYPE_END_RANGE;
    Ivrs.IvhdType10End.u16DevId         = PCIBDF_MAKE(0xff, VBOX_PCI_DEVFN_MAKE(0x1f, 7U));
    Ivrs.IvhdType10End.u8DteSetting     = 0;

    /* Southbridge I/O APIC special device entry. */
    Ivrs.IvhdType10IoApic.u8DevEntryType          = 0x48;
    Ivrs.IvhdType10IoApic.u.special.u16Rsvd0      = 0;
    Ivrs.IvhdType10IoApic.u.special.u8DteSetting  = RT_BF_MAKE(ACPI_IVHD_DTE_INIT_PASS,   1)
                                                  | RT_BF_MAKE(ACPI_IVHD_DTE_EXTINT_PASS, 1)
                                                  | RT_BF_MAKE(ACPI_IVHD_DTE_NMI_PASS,    1)
                                                  | RT_BF_MAKE(ACPI_IVHD_DTE_LINT0_PASS,  1)
                                                  | RT_BF_MAKE(ACPI_IVHD_DTE_LINT1_PASS,  1);
    Ivrs.IvhdType10IoApic.u.special.u8Handle      = pThis->cCpus;   /* The I/O APIC ID, see u8IOApicId in acpiR3SetupMadt(). */
    Ivrs.IvhdType10IoApic.u.special.u16DevIdB     = VBOX_PCI_BDF_SB_IOAPIC;
    Ivrs.IvhdType10IoApic.u.special.u8Variety     = ACPI_IVHD_VARIETY_IOAPIC;

    /* HPET special device entry. */
    Ivrs.IvhdType10Hpet.u8DevEntryType          = 0x48;
    Ivrs.IvhdType10Hpet.u.special.u16Rsvd0      = 0;
    Ivrs.IvhdType10Hpet.u.special.u8DteSetting  = 0;
    Ivrs.IvhdType10Hpet.u.special.u8Handle      = 0; /* HPET number. ASSUMING it's identical to u32Number in acpiR3SetupHpet(). */
    Ivrs.IvhdType10Hpet.u.special.u16DevIdB     = VBOX_PCI_BDF_SB_IOAPIC;   /* HPET goes through the I/O APIC. */
    Ivrs.IvhdType10Hpet.u.special.u8Variety     = ACPI_IVHD_VARIETY_HPET;

    /* IVHD type 11 definition block. */
    Ivrs.IvhdType11.u8Type             = 0x11;
    Ivrs.IvhdType11.u16Length          = sizeof(Ivrs.IvhdType11)
                                       + sizeof(Ivrs.IvhdType11Start)
                                       + sizeof(Ivrs.IvhdType11End)
                                       + sizeof(Ivrs.IvhdType11Rsvd0)
                                       + sizeof(Ivrs.IvhdType11Rsvd1)
                                       + sizeof(Ivrs.IvhdType11IoApic)
                                       + sizeof(Ivrs.IvhdType11Hpet);
    Ivrs.IvhdType11.u16DeviceId        = Ivrs.IvhdType10.u16DeviceId;
    Ivrs.IvhdType11.u16CapOffset       = Ivrs.IvhdType10.u16CapOffset;
    Ivrs.IvhdType11.u64BaseAddress     = Ivrs.IvhdType10.u64BaseAddress;
    Ivrs.IvhdType11.u16PciSegmentGroup = Ivrs.IvhdType10.u16PciSegmentGroup;
    Ivrs.IvhdType11.u8Flags            = ACPI_IVHD_11H_F_COHERENT;
    Ivrs.IvhdType11.u16IommuInfo       = Ivrs.IvhdType10.u16IommuInfo;
    Ivrs.IvhdType11.u32IommuAttr       = RT_BF_MAKE(ACPI_IOMMU_ATTR_BF_PN_COUNTERS, 0)
                                       | RT_BF_MAKE(ACPI_IOMMU_ATTR_BF_PN_BANKS,    0)
                                       | RT_BF_MAKE(ACPI_IOMMU_ATTR_BF_MSI_NUM_PPR, 0);
    /* NOTE! The feature bits below must match the IOMMU device code (MMIO/PCI access of the EFR register). */
    Ivrs.IvhdType11.u64EfrRegister     = RT_BF_MAKE(IOMMU_EXT_FEAT_BF_PREF_SUP,           0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_PPR_SUP,            0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_X2APIC_SUP,         0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_NO_EXEC_SUP,        0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_GT_SUP,             0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_IA_SUP,             1)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_GA_SUP,             0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_HE_SUP,             1)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_PC_SUP,             0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_HATS,               IOMMU_MAX_HOST_PT_LEVEL & 3)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_GATS,               0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_GLX_SUP,            0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_SMI_FLT_SUP,        0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_SMI_FLT_REG_CNT,    0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_GAM_SUP,            0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_DUAL_PPR_LOG_SUP,   0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_DUAL_EVT_LOG_SUP,   0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_PASID_MAX,          0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_US_SUP,             0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_DEV_TBL_SEG_SUP,    IOMMU_MAX_DEV_TAB_SEGMENTS)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_PPR_OVERFLOW_EARLY, 0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_PPR_AUTO_RES_SUP,   0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_MARC_SUP,           0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_BLKSTOP_MARK_SUP,   0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_PERF_OPT_SUP,       0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_MSI_CAP_MMIO_SUP,   1)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_GST_IO_PROT_SUP,    0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_HST_ACCESS_SUP,     0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_ENHANCED_PPR_SUP,   0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_ATTR_FW_SUP,        0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_HST_DIRTY_SUP,      0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_INV_IOTLB_TYPE_SUP, 0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_GA_UPDATE_DIS_SUP,  0)
                                       | RT_BF_MAKE(IOMMU_EXT_FEAT_BF_FORCE_PHYS_DST_SUP, 0);

    /* The IVHD type 11 entries can be copied from their type 10 counterparts. */
    Ivrs.IvhdType11Start  = Ivrs.IvhdType10Start;
    Ivrs.IvhdType11End    = Ivrs.IvhdType10End;
    Ivrs.IvhdType11Rsvd0  = Ivrs.IvhdType10Rsvd0;
    Ivrs.IvhdType11Rsvd1  = Ivrs.IvhdType10Rsvd1;
    Ivrs.IvhdType11IoApic = Ivrs.IvhdType10IoApic;
    Ivrs.IvhdType11Hpet   = Ivrs.IvhdType10Hpet;

    /* Finally, compute checksum. */
    Ivrs.Hdr.header.u8Checksum = acpiR3Checksum(&Ivrs, sizeof(Ivrs));

    /* Plant the ACPI table. */
    acpiR3PhysCopy(pDevIns, addr, (const uint8_t *)&Ivrs, sizeof(Ivrs));
}
#endif  /* VBOX_WITH_IOMMU_AMD */


#ifdef VBOX_WITH_IOMMU_INTEL
/**
 * Plant the Intel IOMMU (VT-d) descriptor.
 */
static void acpiR3SetupIommuIntel(PPDMDEVINS pDevIns, PACPISTATE pThis, RTGCPHYS32 addr)
{
    ACPITBLVTD VtdTable;
    RT_ZERO(VtdTable);

    /* VT-d Table. */
    acpiR3PrepareHeader(pThis, &VtdTable.Dmar.Hdr, "DMAR", sizeof(ACPITBLVTD), ACPI_DMAR_REVISION);

    /* DMAR. */
    uint8_t cPhysAddrBits;
    uint8_t cLinearAddrBits;
    PDMDevHlpCpuGetGuestAddrWidths(pDevIns, &cPhysAddrBits, &cLinearAddrBits);
    Assert(cPhysAddrBits > 0); NOREF(cLinearAddrBits);
    VtdTable.Dmar.uHostAddrWidth = cPhysAddrBits - 1;
    VtdTable.Dmar.fFlags         = DMAR_ACPI_DMAR_FLAGS;

    /* DRHD. */
    VtdTable.Drhd.cbLength     = sizeof(ACPIDRHD);
    VtdTable.Drhd.fFlags       = ACPI_DRHD_F_INCLUDE_PCI_ALL;
    VtdTable.Drhd.uRegBaseAddr = DMAR_MMIO_BASE_PHYSADDR;

    /* Device Scopes: I/O APIC. */
    if (pThis->u8UseIOApic)
    {
        uint8_t const uIoApicBus = 0;
        uint8_t const uIoApicDev = RT_HI_U16(pThis->u32SbIoApicPciAddress);
        uint8_t const uIoApicFn  = RT_LO_U16(pThis->u32SbIoApicPciAddress);

        VtdTable.DevScopeIoApic.uType          = ACPIDMARDEVSCOPE_TYPE_IOAPIC;
        VtdTable.DevScopeIoApic.cbLength       = sizeof(ACPIDMARDEVSCOPE);
        VtdTable.DevScopeIoApic.idEnum         = pThis->cCpus;   /* The I/O APIC ID, see u8IOApicId in acpiR3SetupMadt(). */
        VtdTable.DevScopeIoApic.uStartBusNum   = uIoApicBus;
        VtdTable.DevScopeIoApic.Path.uDevice   = uIoApicDev;
        VtdTable.DevScopeIoApic.Path.uFunction = uIoApicFn;

        VtdTable.Drhd.cbLength += sizeof(VtdTable.DevScopeIoApic);
    }

    /* Finally, compute checksum. */
    VtdTable.Dmar.Hdr.u8Checksum = acpiR3Checksum(&VtdTable, sizeof(VtdTable));

    /* Plant the ACPI table. */
    acpiR3PhysCopy(pDevIns, addr, (const uint8_t *)&VtdTable, sizeof(VtdTable));
}
#endif  /* VBOX_WITH_IOMMU_INTEL */


#ifdef VBOX_WITH_TPM
/**
 * Plant the TPM 2.0 ACPI descriptor.
 */
static void acpiR3SetupTpm(PPDMDEVINS pDevIns, PACPISTATE pThis, RTGCPHYS32 addr)
{
    if (pThis->enmTpmMode == ACPITPMMODE_TIS_1_2)
    {
        ACPITBLTCPA TcpaTbl;
        RT_ZERO(TcpaTbl);

        acpiR3PrepareHeader(pThis, &TcpaTbl.Hdr, "TCPA", sizeof(TcpaTbl), ACPI_TCPA_REVISION);

        TcpaTbl.u16PlatCls = ACPI_TCPA_PLAT_CLS_CLIENT;
        TcpaTbl.u32Laml    = ACPI_TCPA_LAML_SZ;
        TcpaTbl.u64Lasa    = addr + sizeof(TcpaTbl);

        /* Finally, compute checksum. */
        TcpaTbl.Hdr.u8Checksum = acpiR3Checksum(&TcpaTbl, sizeof(TcpaTbl));

        /* Plant the ACPI table. */
        acpiR3PhysCopy(pDevIns, addr, (const uint8_t *)&TcpaTbl, sizeof(TcpaTbl));
    }
    else
    {
        ACPITBLTPM20 Tpm2Tbl;
        RT_ZERO(Tpm2Tbl);

        acpiR3PrepareHeader(pThis, &Tpm2Tbl.Hdr, "TPM2", sizeof(ACPITBLTPM20), ACPI_TPM20_REVISION);

        switch (pThis->enmTpmMode)
        {
            case ACPITPMMODE_CRB_2_0:
                Tpm2Tbl.u32StartMethod       = ACPITBL_TPM20_START_METHOD_CRB;
                Tpm2Tbl.u64BaseAddrCrbOrFifo = pThis->GCPhysTpmMmio;
                break;
            case ACPITPMMODE_FIFO_2_0:
                Tpm2Tbl.u32StartMethod = ACPITBL_TPM20_START_METHOD_TIS12;
                break;
            case ACPITPMMODE_TIS_1_2: /* Handled above. */
            case ACPITPMMODE_DISABLED: /* Should never be called with the TPM disabled. */
            default:
                AssertFailed();
        }

        Tpm2Tbl.u16PlatCls = ACPITBL_TPM20_PLAT_CLS_CLIENT;

        /* Finally, compute checksum. */
        Tpm2Tbl.Hdr.u8Checksum = acpiR3Checksum(&Tpm2Tbl, sizeof(Tpm2Tbl));

        /* Plant the ACPI table. */
        acpiR3PhysCopy(pDevIns, addr, (const uint8_t *)&Tpm2Tbl, sizeof(Tpm2Tbl));
    }
}
#endif


/**
 * Used by acpiR3PlantTables to plant a MMCONFIG PCI config space access (MCFG)
 * descriptor.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The ACPI shared instance data.
 * @param   GCPhysDst   Where to plant it.
 */
static void acpiR3SetupMcfg(PPDMDEVINS pDevIns, PACPISTATE pThis, RTGCPHYS32 GCPhysDst)
{
    struct
    {
        ACPITBLMCFG       hdr;
        ACPITBLMCFGENTRY  entry;
    }       tbl;
    uint8_t u8StartBus = 0;
    uint8_t u8EndBus   = (pThis->u64PciConfigMMioLength >> 20) - 1;

    RT_ZERO(tbl);

    acpiR3PrepareHeader(pThis, &tbl.hdr.aHeader, "MCFG", sizeof(tbl), 1);
    tbl.entry.u64BaseAddress = pThis->u64PciConfigMMioAddress;
    tbl.entry.u8StartBus     = u8StartBus;
    tbl.entry.u8EndBus       = u8EndBus;
    // u16PciSegmentGroup must match _SEG in ACPI table

    tbl.hdr.aHeader.u8Checksum = acpiR3Checksum(&tbl, sizeof(tbl));

    acpiR3PhysCopy(pDevIns, GCPhysDst, (const uint8_t *)&tbl, sizeof(tbl));
}

/**
 * Used by acpiR3PlantTables and acpiConstruct.
 *
 * @returns Guest memory address.
 */
static uint32_t apicR3FindRsdpSpace(void)
{
    return 0xe0000;
}

/**
 * Called by acpiR3Construct to read and allocate a custom ACPI table
 *
 * @param   pDevIns         The device instance.
 * @param   ppu8CustBin     Address to receive the address of the table
 * @param   pcbCustBin      Address to receive the size of the the table.
 * @param   pszCustBinFile
 * @param   cbBufAvail      Maximum space in bytes available for the custom
 *                          table (including header).
 */
static int acpiR3ReadCustomTable(PPDMDEVINS pDevIns, uint8_t **ppu8CustBin, uint64_t *pcbCustBin,
                                 char *pszCustBinFile, uint32_t cbBufAvail)
{
    RTFILE FileCUSTBin;
    int rc = RTFileOpen(&FileCUSTBin, pszCustBinFile,
                        RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        rc = RTFileQuerySize(FileCUSTBin, pcbCustBin);
        if (RT_SUCCESS(rc))
        {
            /* The following checks should be in sync the AssertReleaseMsg's below. */
            if (    *pcbCustBin > cbBufAvail
                ||  *pcbCustBin < sizeof(ACPITBLHEADER))
                rc = VERR_TOO_MUCH_DATA;

            /*
             * Allocate buffer for the custom table binary data.
             */
            *ppu8CustBin = (uint8_t *)PDMDevHlpMMHeapAlloc(pDevIns, *pcbCustBin);
            if (*ppu8CustBin)
            {
                rc = RTFileRead(FileCUSTBin, *ppu8CustBin, *pcbCustBin, NULL);
                if (RT_FAILURE(rc))
                {
                    AssertMsgFailed(("RTFileRead(,,%d,NULL) -> %Rrc\n", *pcbCustBin, rc));
                    PDMDevHlpMMHeapFree(pDevIns, *ppu8CustBin);
                    *ppu8CustBin = NULL;
                }
            }
            else
            {
                rc = VERR_NO_MEMORY;
            }
            RTFileClose(FileCUSTBin);
        }
    }
    return rc;
}

/**
 * Create the ACPI tables in guest memory.
 */
static int acpiR3PlantTables(PPDMDEVINS pDevIns, PACPISTATE pThis, PACPISTATER3 pThisCC)
{
    int        rc;
    RTGCPHYS32 GCPhysCur, GCPhysRsdt, GCPhysXsdt, GCPhysFadtAcpi1, GCPhysFadtAcpi2, GCPhysFacs, GCPhysDsdt;
    RTGCPHYS32 GCPhysHpet = 0;
#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    RTGCPHYS32 GCPhysIommu = 0;
#endif
#ifdef VBOX_WITH_TPM
    RTGCPHYS32 GCPhysTpm  = 0;
    RTGCPHYS32 GCPhysSsdtTpm  = 0;
#endif
    RTGCPHYS32 GCPhysApic = 0;
    RTGCPHYS32 GCPhysSsdt = 0;
    RTGCPHYS32 GCPhysMcfg = 0;
    RTGCPHYS32 aGCPhysCust[MAX_CUST_TABLES] = {0};
    uint32_t   addend = 0;
#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
# ifdef VBOX_WITH_TPM
    RTGCPHYS32 aGCPhysRsdt[10 + MAX_CUST_TABLES];
    RTGCPHYS32 aGCPhysXsdt[10 + MAX_CUST_TABLES];
# else
    RTGCPHYS32 aGCPhysRsdt[8 + MAX_CUST_TABLES];
    RTGCPHYS32 aGCPhysXsdt[8 + MAX_CUST_TABLES];
# endif
#else
# ifdef VBOX_WITH_TPM
    RTGCPHYS32 aGCPhysRsdt[9 + MAX_CUST_TABLES];
    RTGCPHYS32 aGCPhysXsdt[9 + MAX_CUST_TABLES];
# else
    RTGCPHYS32 aGCPhysRsdt[7 + MAX_CUST_TABLES];
    RTGCPHYS32 aGCPhysXsdt[7 + MAX_CUST_TABLES];
# endif
#endif
    uint32_t   cAddr;
    uint32_t   iMadt  = 0;
    uint32_t   iHpet  = 0;
#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    uint32_t   iIommu = 0;
#endif
#ifdef VBOX_WITH_TPM
    uint32_t   iTpm   = 0;
    uint32_t   iSsdtTpm   = 0;
#endif
    uint32_t   iSsdt  = 0;
    uint32_t   iMcfg  = 0;
    uint32_t   iCust  = 0;
    size_t     cbRsdt = sizeof(ACPITBLHEADER);
    size_t     cbXsdt = sizeof(ACPITBLHEADER);

    cAddr = 1;                  /* FADT */
    if (pThis->u8UseIOApic)
        iMadt = cAddr++;        /* MADT */

    if (pThis->fUseHpet)
        iHpet = cAddr++;        /* HPET */

#ifdef VBOX_WITH_IOMMU_AMD
    if (pThis->fUseIommuAmd)
        iIommu = cAddr++;      /* IOMMU (AMD) */
#endif

#ifdef VBOX_WITH_IOMMU_INTEL
    if (pThis->fUseIommuIntel)
        iIommu = cAddr++;      /* IOMMU (Intel) */
#endif

#ifdef VBOX_WITH_TPM
    if (pThis->enmTpmMode != ACPITPMMODE_DISABLED)
    {
        iTpm     = cAddr++;   /* TPM device */
        iSsdtTpm = cAddr++;
    }
#endif

    if (pThis->fUseMcfg)
        iMcfg = cAddr++;        /* MCFG */

    if (pThis->cCustTbls > 0)
    {
        iCust = cAddr;          /* CUST */
        cAddr += pThis->cCustTbls;
    }

    iSsdt = cAddr++;            /* SSDT */

    Assert(cAddr < RT_ELEMENTS(aGCPhysRsdt));
    Assert(cAddr < RT_ELEMENTS(aGCPhysXsdt));

    cbRsdt += cAddr * sizeof(uint32_t);  /* each entry: 32 bits phys. address. */
    cbXsdt += cAddr * sizeof(uint64_t);  /* each entry: 64 bits phys. address. */

    /*
     * Calculate the sizes for the low region and for the 64-bit prefetchable memory.
     * The latter starts never below 4G.
     */
    uint32_t        cbBelow4GB = PDMDevHlpMMPhysGetRamSizeBelow4GB(pDevIns);
    uint64_t const  cbAbove4GB = PDMDevHlpMMPhysGetRamSizeAbove4GB(pDevIns);

    pThis->u64RamSize = PDMDevHlpMMPhysGetRamSize(pDevIns);
    if (pThis->fPciPref64Enabled)
    {
        uint64_t const u64PciPref64Min = _4G + cbAbove4GB;
        if (pThis->u64PciPref64Max > u64PciPref64Min)
        {
            /* Activate MEM4. See also DevPciIch9.cpp / ich9pciFakePCIBIOS() / uPciBiosMmio64 */
            pThis->u64PciPref64Min = u64PciPref64Min;
            LogRel(("ACPI: Enabling 64-bit prefetch root bus resource %#018RX64..%#018RX64\n",
                   u64PciPref64Min, pThis->u64PciPref64Max-1));
        }
        else
            LogRel(("ACPI: NOT enabling 64-bit prefetch root bus resource (min/%#018RX64 >= max/%#018RX64)\n",
                   u64PciPref64Min, pThis->u64PciPref64Max-1));
    }
    if (cbBelow4GB > UINT32_C(0xfe000000)) /* See MEM3. */
    {
        /* Note: This is also enforced by DevPcBios.cpp. */
        LogRel(("ACPI: Clipping cbRamLow=%#RX64 down to 0xfe000000.\n", cbBelow4GB));
        cbBelow4GB = UINT32_C(0xfe000000);
    }
    pThis->cbRamLow = cbBelow4GB;

    GCPhysCur = 0;
    GCPhysRsdt = GCPhysCur;

    GCPhysCur = RT_ALIGN_32(GCPhysCur + cbRsdt, 16);
    GCPhysXsdt = GCPhysCur;

    GCPhysCur = RT_ALIGN_32(GCPhysCur + cbXsdt, 16);
    GCPhysFadtAcpi1 = GCPhysCur;

    GCPhysCur = RT_ALIGN_32(GCPhysCur + ACPITBLFADT_VERSION1_SIZE, 16);
    GCPhysFadtAcpi2 = GCPhysCur;

    GCPhysCur = RT_ALIGN_32(GCPhysCur + sizeof(ACPITBLFADT), 64);
    GCPhysFacs = GCPhysCur;

    GCPhysCur = RT_ALIGN_32(GCPhysCur + sizeof(ACPITBLFACS), 16);
    if (pThis->u8UseIOApic)
    {
        GCPhysApic = GCPhysCur;
        GCPhysCur = RT_ALIGN_32(GCPhysCur + AcpiTableMadt::sizeFor(pThis, NUMBER_OF_IRQ_SOURCE_OVERRIDES), 16);
    }
    if (pThis->fUseHpet)
    {
        GCPhysHpet = GCPhysCur;
        GCPhysCur = RT_ALIGN_32(GCPhysCur + sizeof(ACPITBLHPET), 16);
    }
#ifdef VBOX_WITH_IOMMU_AMD
    if (pThis->fUseIommuAmd)
    {
        GCPhysIommu = GCPhysCur;
        GCPhysCur = RT_ALIGN_32(GCPhysCur + sizeof(ACPITBLIOMMU), 16);
    }
#endif
#ifdef VBOX_WITH_IOMMU_INTEL
    if (pThis->fUseIommuIntel)
    {
        GCPhysIommu = GCPhysCur;
        GCPhysCur = RT_ALIGN_32(GCPhysCur + sizeof(ACPITBLVTD), 16);
    }
#endif
#ifdef VBOX_WITH_TPM
    void  *pvSsdtTpmCode = NULL;
    size_t cbSsdtTpm = 0;

    if (pThis->enmTpmMode != ACPITPMMODE_DISABLED)
    {
        GCPhysTpm = GCPhysCur;

        if (pThis->enmTpmMode == ACPITPMMODE_TIS_1_2)
            GCPhysCur = RT_ALIGN_32(GCPhysCur + sizeof(ACPITBLTCPA) + ACPI_TCPA_LAML_SZ, 16);
        else
            GCPhysCur = RT_ALIGN_32(GCPhysCur + sizeof(ACPITBLTPM20), 16);

        rc = acpiPrepareTpmSsdt(pDevIns, &pvSsdtTpmCode, &cbSsdtTpm);
        if (RT_FAILURE(rc))
            return rc;

        GCPhysSsdtTpm = GCPhysCur;
        GCPhysCur = RT_ALIGN_32(GCPhysCur + cbSsdtTpm, 16);
    }
#endif

    if (pThis->fUseMcfg)
    {
        GCPhysMcfg = GCPhysCur;
        /* Assume one entry */
        GCPhysCur = RT_ALIGN_32(GCPhysCur + sizeof(ACPITBLMCFG) + sizeof(ACPITBLMCFGENTRY), 16);
    }

    for (uint8_t i = 0; i < pThis->cCustTbls; i++)
    {
        aGCPhysCust[i] = GCPhysCur;
        GCPhysCur = RT_ALIGN_32(GCPhysCur + pThisCC->acbCustBin[i], 16);
    }

    void  *pvSsdtCode = NULL;
    size_t cbSsdt = 0;
    rc = acpiPrepareSsdt(pDevIns, &pvSsdtCode, &cbSsdt);
    if (RT_FAILURE(rc))
        return rc;

    GCPhysSsdt = GCPhysCur;
    GCPhysCur = RT_ALIGN_32(GCPhysCur + cbSsdt, 16);

    GCPhysDsdt = GCPhysCur;

    void  *pvDsdtCode = NULL;
    size_t cbDsdt = 0;
    rc = acpiPrepareDsdt(pDevIns, &pvDsdtCode, &cbDsdt);
    if (RT_FAILURE(rc))
        return rc;

    GCPhysCur = RT_ALIGN_32(GCPhysCur + cbDsdt, 16);

    if (GCPhysCur > 0x10000)
        return PDMDEV_SET_ERROR(pDevIns, VERR_TOO_MUCH_DATA,
                                N_("Error: ACPI tables bigger than 64KB"));

    Log(("RSDP 0x%08X\n", apicR3FindRsdpSpace()));
    addend = pThis->cbRamLow - 0x10000;
    Log(("RSDT 0x%08X XSDT 0x%08X\n", GCPhysRsdt + addend, GCPhysXsdt + addend));
    Log(("FACS 0x%08X FADT (1.0) 0x%08X, FADT (2+) 0x%08X\n", GCPhysFacs + addend, GCPhysFadtAcpi1 + addend, GCPhysFadtAcpi2 + addend));
    Log(("DSDT 0x%08X", GCPhysDsdt + addend));
    if (pThis->u8UseIOApic)
        Log((" MADT 0x%08X", GCPhysApic + addend));
    if (pThis->fUseHpet)
        Log((" HPET 0x%08X", GCPhysHpet + addend));
    if (pThis->fUseMcfg)
        Log((" MCFG 0x%08X", GCPhysMcfg + addend));
    for (uint8_t i = 0; i < pThis->cCustTbls; i++)
        Log((" CUST(%d) 0x%08X", i, aGCPhysCust[i] + addend));
    Log((" SSDT 0x%08X", GCPhysSsdt + addend));
    Log(("\n"));

    acpiR3SetupRsdp(pThis, (ACPITBLRSDP *)pThis->au8RSDPPage, GCPhysRsdt + addend, GCPhysXsdt + addend);
    acpiR3SetupDsdt(pDevIns, GCPhysDsdt + addend, pvDsdtCode, cbDsdt);
    acpiCleanupDsdt(pDevIns, pvDsdtCode);
    acpiR3SetupFacs(pDevIns, GCPhysFacs + addend);
    acpiR3SetupFadt(pDevIns, pThis, GCPhysFadtAcpi1 + addend, GCPhysFadtAcpi2 + addend, GCPhysFacs + addend, GCPhysDsdt + addend);

    aGCPhysRsdt[0] = GCPhysFadtAcpi1 + addend;
    aGCPhysXsdt[0] = GCPhysFadtAcpi2 + addend;
    if (pThis->u8UseIOApic)
    {
        acpiR3SetupMadt(pDevIns, pThis, GCPhysApic + addend);
        aGCPhysRsdt[iMadt] = GCPhysApic + addend;
        aGCPhysXsdt[iMadt] = GCPhysApic + addend;
    }
    if (pThis->fUseHpet)
    {
        acpiR3SetupHpet(pDevIns, pThis, GCPhysHpet + addend);
        aGCPhysRsdt[iHpet] = GCPhysHpet + addend;
        aGCPhysXsdt[iHpet] = GCPhysHpet + addend;
    }
#ifdef VBOX_WITH_IOMMU_AMD
    if (pThis->fUseIommuAmd)
    {
        acpiR3SetupIommuAmd(pDevIns, pThis, GCPhysIommu + addend);
        aGCPhysRsdt[iIommu] = GCPhysIommu + addend;
        aGCPhysXsdt[iIommu] = GCPhysIommu + addend;
    }
#endif
#ifdef VBOX_WITH_IOMMU_INTEL
    if (pThis->fUseIommuIntel)
    {
        acpiR3SetupIommuIntel(pDevIns, pThis, GCPhysIommu + addend);
        aGCPhysRsdt[iIommu] = GCPhysIommu + addend;
        aGCPhysXsdt[iIommu] = GCPhysIommu + addend;
    }
#endif
#ifdef VBOX_WITH_TPM
    if (pThis->enmTpmMode != ACPITPMMODE_DISABLED)
    {
        acpiR3SetupTpm(pDevIns, pThis, GCPhysTpm + addend);
        aGCPhysRsdt[iTpm] = GCPhysTpm + addend;
        aGCPhysXsdt[iTpm] = GCPhysTpm + addend;

        acpiR3SetupTpmSsdt(pDevIns, GCPhysSsdtTpm + addend, pvSsdtTpmCode, cbSsdtTpm);
        acpiCleanupTpmSsdt(pDevIns, pvSsdtTpmCode);
        aGCPhysRsdt[iSsdtTpm] = GCPhysSsdtTpm + addend;
        aGCPhysXsdt[iSsdtTpm] = GCPhysSsdtTpm + addend;
    }
#endif

    if (pThis->fUseMcfg)
    {
        acpiR3SetupMcfg(pDevIns, pThis, GCPhysMcfg + addend);
        aGCPhysRsdt[iMcfg] = GCPhysMcfg + addend;
        aGCPhysXsdt[iMcfg] = GCPhysMcfg + addend;
    }
    for (uint8_t i = 0; i < pThis->cCustTbls; i++)
    {
        AssertBreak(i < MAX_CUST_TABLES);
        acpiR3PhysCopy(pDevIns, aGCPhysCust[i] + addend, pThisCC->apu8CustBin[i], pThisCC->acbCustBin[i]);
        aGCPhysRsdt[iCust + i] = aGCPhysCust[i] + addend;
        aGCPhysXsdt[iCust + i] = aGCPhysCust[i] + addend;
        uint8_t* pSig = pThisCC->apu8CustBin[i];
        LogRel(("ACPI: Planted custom table '%c%c%c%c' at 0x%08X\n",
               pSig[0], pSig[1], pSig[2], pSig[3], aGCPhysCust[i] + addend));
    }

    acpiR3SetupSsdt(pDevIns, GCPhysSsdt + addend, pvSsdtCode, cbSsdt);
    acpiCleanupSsdt(pDevIns, pvSsdtCode);
    aGCPhysRsdt[iSsdt] = GCPhysSsdt + addend;
    aGCPhysXsdt[iSsdt] = GCPhysSsdt + addend;

    rc = acpiR3SetupRsdt(pDevIns, pThis, GCPhysRsdt + addend, cAddr, aGCPhysRsdt);
    if (RT_FAILURE(rc))
        return rc;
    return acpiR3SetupXsdt(pDevIns, pThis, GCPhysXsdt + addend, cAddr, aGCPhysXsdt);
}

/**
 * @callback_method_impl{FNPCICONFIGREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3PciConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                      uint32_t uAddress, unsigned cb, uint32_t *pu32Value)
{
    VBOXSTRICTRC rcStrict = PDMDevHlpPCIConfigRead(pDevIns, pPciDev, uAddress, cb, pu32Value);
    Log2(("acpi: PCI config read: %#x (%d) -> %#x %Rrc\n", uAddress, cb, *pu32Value, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}

/**
 * @callback_method_impl{FNPCICONFIGWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) acpiR3PciConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                       uint32_t uAddress, unsigned cb, uint32_t u32Value)
{
    PACPISTATE      pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    PACPISTATER3    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PACPISTATER3);

    Log2(("acpi: PCI config write: 0x%x -> 0x%x (%d)\n", u32Value, uAddress, cb));
    DEVACPI_LOCK_R3(pDevIns, pThis);

    if (uAddress == VBOX_PCI_INTERRUPT_LINE)
    {
        Log(("acpi: ignore interrupt line settings: %d, we'll use hardcoded value %d\n", u32Value, SCI_INT));
        u32Value = SCI_INT;
    }

    VBOXSTRICTRC rcStrict = PDMDevHlpPCIConfigWrite(pDevIns, pPciDev, uAddress, cb, u32Value);

    /* Assume that the base address is only changed when the corresponding
     * hardware functionality is disabled. The IO region is mapped when the
     * functionality is enabled by the guest. */

    if (uAddress == PMREGMISC)
    {
        RTIOPORT NewIoPortBase = 0;
        /* Check Power Management IO Space Enable (PMIOSE) bit */
        if (pPciDev->abConfig[PMREGMISC] & 0x01)
        {
            NewIoPortBase = (RTIOPORT)PDMPciDevGetDWord(pPciDev, PMBA);
            NewIoPortBase &= 0xffc0;
        }

        int rc = acpiR3UpdatePmHandlers(pDevIns, pThis, pThisCC, NewIoPortBase);
        AssertRC(rc);
    }

    if (uAddress == SMBHSTCFG)
    {
        RTIOPORT NewIoPortBase = 0;
        /* Check SMBus Controller Host Interface Enable (SMB_HST_EN) bit */
        if (pPciDev->abConfig[SMBHSTCFG] & SMBHSTCFG_SMB_HST_EN)
        {
            NewIoPortBase = (RTIOPORT)PDMPciDevGetDWord(pPciDev, SMBBA);
            NewIoPortBase &= 0xfff0;
        }

        int rc = acpiR3UpdateSMBusHandlers(pDevIns, pThis, NewIoPortBase);
        AssertRC(rc);
    }

    DEVACPI_UNLOCK(pDevIns, pThis);
    return rcStrict;
}

/**
 * Attach a new CPU.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   iLUN        The logical unit which is being attached.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 *
 * @remarks This code path is not used during construction.
 */
static DECLCALLBACK(int) acpiR3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PACPISTATE      pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    PACPISTATER3    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PACPISTATER3);
    LogFlow(("acpiAttach: pDevIns=%p iLUN=%u fFlags=%#x\n", pDevIns, iLUN, fFlags));

    AssertMsgReturn(!(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG),
                    ("Hot-plug flag is not set\n"),
                    VERR_NOT_SUPPORTED);
    AssertReturn(iLUN < VMM_MAX_CPU_COUNT, VERR_PDM_NO_SUCH_LUN);

    /* Check if it was already attached */
    int rc = VINF_SUCCESS;
    DEVACPI_LOCK_R3(pDevIns, pThis);
    if (!VMCPUSET_IS_PRESENT(&pThis->CpuSetAttached, iLUN))
    {
        PPDMIBASE IBaseTmp;
        rc = PDMDevHlpDriverAttach(pDevIns, iLUN, &pThisCC->IBase, &IBaseTmp, "ACPI CPU");
        if (RT_SUCCESS(rc))
        {
            /* Enable the CPU */
            VMCPUSET_ADD(&pThis->CpuSetAttached, iLUN);

            /*
             * Lock the CPU because we don't know if the guest will use it or not.
             * Prevents ejection while the CPU is still used
             */
            VMCPUSET_ADD(&pThis->CpuSetLocked, iLUN);
            pThis->u32CpuEventType = CPU_EVENT_TYPE_ADD;
            pThis->u32CpuEvent     = iLUN;

            /* Notify the guest */
            apicR3UpdateGpe0(pDevIns, pThis, pThis->gpe0_sts | 0x2, pThis->gpe0_en);
        }
    }
    DEVACPI_UNLOCK(pDevIns, pThis);
    return rc;
}

/**
 * Detach notification.
 *
 * @param   pDevIns     The device instance.
 * @param   iLUN        The logical unit which is being detached.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 */
static DECLCALLBACK(void) acpiR3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);

    LogFlow(("acpiDetach: pDevIns=%p iLUN=%u fFlags=%#x\n", pDevIns, iLUN, fFlags));

    AssertMsgReturnVoid(!(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG),
                        ("Hot-plug flag is not set\n"));

    /* Check if it was already detached */
    DEVACPI_LOCK_R3(pDevIns, pThis);
    if (VMCPUSET_IS_PRESENT(&pThis->CpuSetAttached, iLUN))
    {
        if (!VMCPUSET_IS_PRESENT(&pThis->CpuSetLocked, iLUN))
        {
            /* Disable the CPU */
            VMCPUSET_DEL(&pThis->CpuSetAttached, iLUN);
            pThis->u32CpuEventType = CPU_EVENT_TYPE_REMOVE;
            pThis->u32CpuEvent     = iLUN;

            /* Notify the guest */
            apicR3UpdateGpe0(pDevIns, pThis, pThis->gpe0_sts | 0x2, pThis->gpe0_en);
        }
        else
            AssertMsgFailed(("CPU is still locked by the guest\n"));
    }
    DEVACPI_UNLOCK(pDevIns, pThis);
}

/**
 * @interface_method_impl{PDMDEVREG,pfnResume}
 */
static DECLCALLBACK(void) acpiR3Resume(PPDMDEVINS pDevIns)
{
    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    if (pThis->fSetWakeupOnResume)
    {
        Log(("acpiResume: setting WAK_STS\n"));
        pThis->fSetWakeupOnResume = false;
        pThis->pm1a_sts |= WAK_STS;
    }
}

/**
 * @interface_method_impl{PDMDEVREG,pfnMemSetup}
 */
static DECLCALLBACK(void) acpiR3MemSetup(PPDMDEVINS pDevIns, PDMDEVMEMSETUPCTX enmCtx)
{
    RT_NOREF(enmCtx);
    PACPISTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    PACPISTATER3    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PACPISTATER3);
    acpiR3PlantTables(pDevIns, pThis, pThisCC);
}

/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) acpiR3Reset(PPDMDEVINS pDevIns)
{
    PACPISTATE      pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    PACPISTATER3    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PACPISTATER3);

    /* Play safe: make sure that the IRQ isn't stuck after a reset. */
    acpiSetIrq(pDevIns, 0);

    PDMDevHlpTimerLockClock(pDevIns, pThis->hPmTimer, VERR_IGNORED);
    pThis->pm1a_en           = 0;
    pThis->pm1a_sts          = 0;
    pThis->pm1a_ctl          = 0;
    pThis->u64PmTimerInitial = PDMDevHlpTimerGet(pDevIns, pThis->hPmTimer);
    pThis->uPmTimerVal       = 0;
    acpiR3PmTimerReset(pDevIns, pThis, pThis->u64PmTimerInitial);
    pThis->uPmTimeOld        = pThis->uPmTimerVal;
    pThis->uBatteryIndex     = 0;
    pThis->uSystemInfoIndex  = 0;
    pThis->gpe0_en           = 0;
    pThis->gpe0_sts          = 0;
    pThis->uSleepState       = 0;
    PDMDevHlpTimerUnlockClock(pDevIns, pThis->hPmTimer);

    /* Real device behavior is resetting only the PM controller state,
     * but we're additionally doing the job of the BIOS. */
    acpiR3UpdatePmHandlers(pDevIns, pThis, pThisCC, PM_PORT_BASE);
    acpiR3PmPCIBIOSFake(pDevIns, pThis);

    /* Reset SMBus base and PCI config space in addition to the SMBus controller
     * state. Real device behavior is only the SMBus controller state reset,
     * but we're additionally doing the job of the BIOS. */
    acpiR3UpdateSMBusHandlers(pDevIns, pThis, SMB_PORT_BASE);
    acpiR3SMBusPCIBIOSFake(pDevIns, pThis);
    acpiR3SMBusResetDevice(pThis);
}

/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) acpiR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PACPISTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    PACPISTATER3    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PACPISTATER3);

    for (uint8_t i = 0; i < pThis->cCustTbls; i++)
    {
        if (pThisCC->apu8CustBin[i])
        {
            PDMDevHlpMMHeapFree(pDevIns, pThisCC->apu8CustBin[i]);
            pThisCC->apu8CustBin[i] = NULL;
        }
    }
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) acpiR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PACPISTATE      pThis   = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);
    PACPISTATER3    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PACPISTATER3);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;

    /*
     * Init data and set defaults.
     */
    /** @todo move more of the code up! */

    pThisCC->pDevIns       = pDevIns;
    VMCPUSET_EMPTY(&pThis->CpuSetAttached);
    VMCPUSET_EMPTY(&pThis->CpuSetLocked);
    pThis->idCpuLockCheck  = UINT32_C(0xffffffff);
    pThis->u32CpuEventType = 0;
    pThis->u32CpuEvent     = UINT32_C(0xffffffff);

    /* The first CPU can't be attached/detached */
    VMCPUSET_ADD(&pThis->CpuSetAttached, 0);
    VMCPUSET_ADD(&pThis->CpuSetLocked, 0);

    /* IBase */
    pThisCC->IBase.pfnQueryInterface                             = acpiR3QueryInterface;
    /* IACPIPort */
    pThisCC->IACPIPort.pfnGetGuestEnteredACPIMode                = acpiR3Port_GetGuestEnteredACPIMode;
    pThisCC->IACPIPort.pfnGetCpuStatus                           = acpiR3Port_GetCpuStatus;
    pThisCC->IACPIPort.pfnMonitorHotPlugEvent                    = acpiR3Port_MonitorHotPlugEvent;
    pThisCC->IACPIPort.pfnBatteryStatusChangeEvent               = acpiR3Port_BatteryStatusChangeEvent;
    /* IButtonEventPort */
    pThisCC->IButtonEventPort.pfnSleepButtonPress                = acpiR3Port_SleepButtonPress;
    pThisCC->IButtonEventPort.pfnPowerButtonPress                = acpiR3Port_PowerButtonPress;
    pThisCC->IButtonEventPort.pfnQueryPowerButtonHandled         = acpiR3Port_QueryPowerButtonHandled;
    pThisCC->IButtonEventPort.pfnQueryGuestCanHandleButtonEvents = acpiR3Port_QueryGuestCanHandleButtonEvents;

    /*
     * Set the default critical section to NOP (related to the PM timer).
     */
    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSect, RT_SRC_POS, "acpi#%u", iInstance);
    AssertRCReturn(rc, rc);

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns,
                                  "IOAPIC"
                                  "|NumCPUs"
                                  "|HpetEnabled"
                                  "|McfgEnabled"
                                  "|McfgBase"
                                  "|McfgLength"
                                  "|PciPref64Enabled"
                                  "|PciPref64LimitGB"
                                  "|SmcEnabled"
                                  "|FdcEnabled"
                                  "|ShowRtc"
                                  "|ShowCpu"
                                  "|NicPciAddress"
                                  "|AudioPciAddress"
                                  "|NvmePciAddress"
                                  "|IocPciAddress"
                                  "|HostBusPciAddress"
                                  "|EnableSuspendToDisk"
                                  "|PowerS1Enabled"
                                  "|PowerS4Enabled"
                                  "|CpuHotPlug"
                                  "|AmlFilePath"
                                  "|Serial0IoPortBase"
                                  "|Serial1IoPortBase"
                                  "|Serial2IoPortBase"
                                  "|Serial3IoPortBase"
                                  "|Serial0Irq"
                                  "|Serial1Irq"
                                  "|Serial2Irq"
                                  "|Serial3Irq"
                                  "|AcpiOemId"
                                  "|AcpiCreatorId"
                                  "|AcpiCreatorRev"
                                  "|CustomTable"
                                  "|CustomTable0"
                                  "|CustomTable1"
                                  "|CustomTable2"
                                  "|CustomTable3"
                                  "|Parallel0IoPortBase"
                                  "|Parallel1IoPortBase"
                                  "|Parallel0Irq"
                                  "|Parallel1Irq"
                                  "|IommuIntelEnabled"
                                  "|IommuAmdEnabled"
                                  "|IommuPciAddress"
                                  "|SbIoApicPciAddress"
                                  "|TpmMode"
                                  "|TpmMmioAddress"
                                  "|SsdtTpmFilePath"
                                  , "");

    /* query whether we are supposed to present an IOAPIC */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "IOAPIC", &pThis->u8UseIOApic, 1);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"IOAPIC\""));

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "NumCPUs", &pThis->cCpus, 1);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"NumCPUs\" as integer failed"));

    /* query whether we are supposed to present an FDC controller */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "FdcEnabled", &pThis->fUseFdc, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"FdcEnabled\""));

    /* query whether we are supposed to present HPET */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "HpetEnabled", &pThis->fUseHpet, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"HpetEnabled\""));
    /* query MCFG configuration */
    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "McfgBase", &pThis->u64PciConfigMMioAddress, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"McfgBase\""));
    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "McfgLength", &pThis->u64PciConfigMMioLength, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"McfgLength\""));
    pThis->fUseMcfg = (pThis->u64PciConfigMMioAddress != 0) && (pThis->u64PciConfigMMioLength != 0);

    /* query whether we are supposed to set up the 64-bit prefetchable memory window */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "PciPref64Enabled", &pThis->fPciPref64Enabled, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"PciPref64Enabled\""));

    /* query the limit of the the 64-bit prefetchable memory window */
    uint64_t u64PciPref64MaxGB;
    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "PciPref64LimitGB", &u64PciPref64MaxGB, 64);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"PciPref64LimitGB\""));
    pThis->u64PciPref64Max = _1G64 * u64PciPref64MaxGB;

    /* query whether we are supposed to present SMC */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "SmcEnabled", &pThis->fUseSmc, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"SmcEnabled\""));

    /* query whether we are supposed to present RTC object */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "ShowRtc", &pThis->fShowRtc, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"ShowRtc\""));

    /* query whether we are supposed to present CPU objects */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "ShowCpu", &pThis->fShowCpu, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"ShowCpu\""));

    /* query primary NIC PCI address (GIGE) */
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "NicPciAddress", &pThis->u32NicPciAddress, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"NicPciAddress\""));

    /* query HD Audio PCI address (HDAA) */
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "AudioPciAddress", &pThis->u32AudioPciAddress, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"AudioPciAddress\""));

    /* query NVMe PCI address (NVMA) */
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "NvmePciAddress", &pThis->u32NvmePciAddress, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"NvmePciAddress\""));

    /* query IO controller (southbridge) PCI address */
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "IocPciAddress", &pThis->u32IocPciAddress, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"IocPciAddress\""));

    /* query host bus controller PCI address */
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "HostBusPciAddress", &pThis->u32HbcPciAddress, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"HostBusPciAddress\""));

    /* query whether S1 power state should be exposed */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "PowerS1Enabled", &pThis->fS1Enabled, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"PowerS1Enabled\""));

    /* query whether S4 power state should be exposed */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "PowerS4Enabled", &pThis->fS4Enabled, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"PowerS4Enabled\""));

    /* query whether S1 power state should save the VM state */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "EnableSuspendToDisk", &pThis->fSuspendToSavedState, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"EnableSuspendToDisk\""));

    /* query whether we are allow CPU hot plugging */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CpuHotPlug", &pThis->fCpuHotPlug, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"CpuHotPlug\""));

    /* query serial info */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "Serial0Irq", &pThis->uSerial0Irq, 4);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"Serial0Irq\""));

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "Serial0IoPortBase", &pThis->uSerial0IoPortBase, 0x3f8);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"Serial0IoPortBase\""));

    /* Serial 1 is enabled, get config data */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "Serial1Irq", &pThis->uSerial1Irq, 3);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"Serial1Irq\""));

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "Serial1IoPortBase", &pThis->uSerial1IoPortBase, 0x2f8);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"Serial1IoPortBase\""));

    /* Read serial port 2 settings; disabled if CFGM keys do not exist. */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "Serial2Irq", &pThis->uSerial2Irq, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"Serial2Irq\""));

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "Serial2IoPortBase", &pThis->uSerial2IoPortBase, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"Serial2IoPortBase\""));

    /* Read serial port 3 settings; disabled if CFGM keys do not exist. */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "Serial3Irq", &pThis->uSerial3Irq, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"Serial3Irq\""));

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "Serial3IoPortBase", &pThis->uSerial3IoPortBase, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"Serial3IoPortBase\""));
    /*
     * Query settings for both parallel ports, if the CFGM keys don't exist pretend that
     * the corresponding parallel port is not enabled.
     */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "Parallel0Irq", &pThis->uParallel0Irq, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"Parallel0Irq\""));

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "Parallel0IoPortBase", &pThis->uParallel0IoPortBase, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"Parallel0IoPortBase\""));

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "Parallel1Irq", &pThis->uParallel1Irq, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"Parallel1Irq\""));

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "Parallel1IoPortBase", &pThis->uParallel1IoPortBase, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"Parallel1IoPortBase\""));

#ifdef VBOX_WITH_IOMMU_AMD
    /* Query whether an IOMMU (AMD) is enabled. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "IommuAmdEnabled", &pThis->fUseIommuAmd, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"IommuAmdEnabled\""));

    if (pThis->fUseIommuAmd)
    {
        /* Query IOMMU AMD address (IOMA). */
        rc = pHlp->pfnCFGMQueryU32(pCfg, "IommuPciAddress", &pThis->u32IommuPciAddress);
        if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"IommuPciAddress\""));

        /* Query southbridge I/O APIC address (required when an AMD IOMMU is configured). */
        rc = pHlp->pfnCFGMQueryU32(pCfg, "SbIoApicPciAddress", &pThis->u32SbIoApicPciAddress);
        if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"SbIoApicAddress\""));

        /* Warn if the IOMMU Address is at the PCI host-bridge address. */
        /** @todo We should eventually not assign the IOMMU at this address, see
         *        @bugref{9654#c53}. */
        if (!pThis->u32IommuPciAddress)
            LogRel(("ACPI: Warning! AMD IOMMU assigned the PCI host bridge address.\n"));

        /* Warn if the IOAPIC is not at the expected address. */
        if (pThis->u32SbIoApicPciAddress != RT_MAKE_U32(VBOX_PCI_FN_SB_IOAPIC, VBOX_PCI_DEV_SB_IOAPIC))
        {
            LogRel(("ACPI: Southbridge I/O APIC not at %#x:%#x:%#x when an AMD IOMMU is present.\n",
                    VBOX_PCI_BUS_SB_IOAPIC, VBOX_PCI_DEV_SB_IOAPIC, VBOX_PCI_FN_SB_IOAPIC));
            return PDMDEV_SET_ERROR(pDevIns, VERR_MISMATCH, N_("Configuration error: \"SbIoApicAddress\" mismatch"));
        }
    }
#endif

#ifdef VBOX_WITH_IOMMU_INTEL
    /* Query whether an IOMMU (Intel) is enabled. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "IommuIntelEnabled", &pThis->fUseIommuIntel, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"IommuIntelEnabled\""));

    if (pThis->fUseIommuIntel)
    {
        /* Query IOMMU Intel address. */
        rc = pHlp->pfnCFGMQueryU32(pCfg, "IommuPciAddress", &pThis->u32IommuPciAddress);
        if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"IommuPciAddress\""));

        /* Get the reserved I/O APIC PCI address (required when an Intel IOMMU is configured). */
        rc = pHlp->pfnCFGMQueryU32(pCfg, "SbIoApicPciAddress", &pThis->u32SbIoApicPciAddress);
        if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"SbIoApicAddress\""));

        /* Warn if the IOAPIC is not at the expected address. */
        if (pThis->u32SbIoApicPciAddress != RT_MAKE_U32(VBOX_PCI_FN_SB_IOAPIC, VBOX_PCI_DEV_SB_IOAPIC))
        {
            LogRel(("ACPI: Southbridge I/O APIC not at %#x:%#x:%#x when an Intel IOMMU is present.\n",
                    VBOX_PCI_BUS_SB_IOAPIC, VBOX_PCI_DEV_SB_IOAPIC, VBOX_PCI_FN_SB_IOAPIC));
            return PDMDEV_SET_ERROR(pDevIns, VERR_MISMATCH, N_("Configuration error: \"SbIoApicAddress\" mismatch"));
        }
    }
#endif

    /* Don't even think about enabling an Intel and an AMD IOMMU at the same time! */
    if (   pThis->fUseIommuAmd
        && pThis->fUseIommuIntel)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Cannot enable Intel and AMD IOMMU simultaneously!"));

#ifdef VBOX_WITH_TPM
    char szTpmMode[64]; RT_ZERO(szTpmMode);

    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "TpmMode", &szTpmMode[0], RT_ELEMENTS(szTpmMode) - 1, "disabled");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"TpmMode\""));

    if (!RTStrICmp(szTpmMode, "disabled"))
        pThis->enmTpmMode = ACPITPMMODE_DISABLED;
    else if (!RTStrICmp(szTpmMode, "tis1.2"))
        pThis->enmTpmMode = ACPITPMMODE_TIS_1_2;
    else if (!RTStrICmp(szTpmMode, "crb2.0"))
        pThis->enmTpmMode = ACPITPMMODE_CRB_2_0;
    else if (!RTStrICmp(szTpmMode, "fifo2.0"))
        pThis->enmTpmMode = ACPITPMMODE_FIFO_2_0;
    else
        return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER, N_("Configuration error: Value of \"TpmMode\" is not known"));

    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "TpmMmioAddress", (uint64_t *)&pThis->GCPhysTpmMmio, ACPI_TPM_MMIO_BASE_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"TpmMmioAddress\""));
#endif

    /* Try to attach the other CPUs */
    for (unsigned i = 1; i < pThis->cCpus; i++)
    {
        if (pThis->fCpuHotPlug)
        {
            PPDMIBASE IBaseTmp;
            rc = PDMDevHlpDriverAttach(pDevIns, i, &pThisCC->IBase, &IBaseTmp, "ACPI CPU");

            if (RT_SUCCESS(rc))
            {
                VMCPUSET_ADD(&pThis->CpuSetAttached, i);
                VMCPUSET_ADD(&pThis->CpuSetLocked, i);
                Log(("acpi: Attached CPU %u\n", i));
            }
            else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
                Log(("acpi: CPU %u not attached yet\n", i));
            else
                return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to attach CPU object\n"));
        }
        else
        {
            /* CPU is always attached if hot-plug is not enabled. */
            VMCPUSET_ADD(&pThis->CpuSetAttached, i);
            VMCPUSET_ADD(&pThis->CpuSetLocked, i);
        }
    }

    char szOemId[16];
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "AcpiOemId", szOemId, sizeof(szOemId), "VBOX  ");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"AcpiOemId\" as string failed"));
    size_t cchOemId = strlen(szOemId);
    if (cchOemId > 6)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: \"AcpiOemId\" must contain not more than 6 characters"));
    memset(pThis->au8OemId, ' ', sizeof(pThis->au8OemId));
    memcpy(pThis->au8OemId, szOemId, cchOemId);

    char szCreatorId[16];
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "AcpiCreatorId", szCreatorId, sizeof(szCreatorId), "ASL ");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"AcpiCreatorId\" as string failed"));
    size_t cchCreatorId = strlen(szCreatorId);
    if (cchCreatorId > 4)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: \"AcpiCreatorId\" must contain not more than 4 characters"));
    memset(pThis->au8CreatorId, ' ', sizeof(pThis->au8CreatorId));
    memcpy(pThis->au8CreatorId, szCreatorId, cchCreatorId);

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "AcpiCreatorRev", &pThis->u32CreatorRev, RT_H2LE_U32(0x61));
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"AcpiCreatorRev\" as integer failed"));

    pThis->u32OemRevision = RT_H2LE_U32(0x1);

    /*
     * Load custom ACPI tables.
     */
    /* Total space available for custom ACPI tables */
    /** @todo define as appropriate, remove as a magic number, and document
     *        limitation in product manual */
    uint32_t cbBufAvail = 3072;
    pThis->cCustTbls = 0;

    static const char *s_apszCustTblConfigKeys[] = {"CustomTable0", "CustomTable1", "CustomTable2", "CustomTable3"};
    AssertCompile(RT_ELEMENTS(s_apszCustTblConfigKeys) <= RT_ELEMENTS(pThisCC->apu8CustBin));
    for (unsigned i = 0; i < RT_ELEMENTS(s_apszCustTblConfigKeys); ++i)
    {
        const char *pszConfigKey = s_apszCustTblConfigKeys[i];

        /*
         * Get the custom table binary file name.
         */
        char *pszCustBinFile = NULL;
        rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, pszConfigKey, &pszCustBinFile);
        if (rc == VERR_CFGM_VALUE_NOT_FOUND && i == 0)
            rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "CustomTable", &pszCustBinFile); /* legacy */
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        {
            rc = VINF_SUCCESS;
            pszCustBinFile = NULL;
        }
        else if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc,
                                    N_("Configuration error: Querying \"CustomTableN\" as a string failed"));
        else if (!*pszCustBinFile)
        {
            PDMDevHlpMMHeapFree(pDevIns, pszCustBinFile);
            pszCustBinFile = NULL;
        }

        /*
         * Determine the custom table binary size, open specified file in the process.
         */
        if (pszCustBinFile)
        {
            uint32_t idxCust = pThis->cCustTbls;
            rc = acpiR3ReadCustomTable(pDevIns, &pThisCC->apu8CustBin[idxCust],
                                       &pThisCC->acbCustBin[idxCust], pszCustBinFile, cbBufAvail);
            LogRel(("ACPI: Reading custom ACPI table(%u) from file '%s' (%d bytes)\n",
                    idxCust, pszCustBinFile, pThisCC->acbCustBin[idxCust]));
            PDMDevHlpMMHeapFree(pDevIns, pszCustBinFile);
            if (RT_FAILURE(rc))
                return PDMDEV_SET_ERROR(pDevIns, rc, N_("Error reading custom ACPI table."));
            cbBufAvail -= pThisCC->acbCustBin[idxCust];

            /* Update custom OEM attributes based on custom table */
            /** @todo is it intended for custom tables to overwrite user provided values above? */
            ACPITBLHEADER *pTblHdr = (ACPITBLHEADER*)pThisCC->apu8CustBin[idxCust];
            memcpy(&pThis->au8OemId[0], &pTblHdr->au8OemId[0], 6);
            memcpy(&pThis->au8OemTabId[0], &pTblHdr->au8OemTabId[0], 8);
            pThis->u32OemRevision = pTblHdr->u32OemRevision;
            memcpy(&pThis->au8CreatorId[0], &pTblHdr->au8CreatorId[0], 4);
            pThis->u32CreatorRev = pTblHdr->u32CreatorRev;

            pThis->cCustTbls++;
            AssertBreak(pThis->cCustTbls <= MAX_CUST_TABLES);
        }
    }

    /* Set default PM port base */
    pThis->uPmIoPortBase = PM_PORT_BASE;

    /* Set default SMBus port base */
    pThis->uSMBusIoPortBase = SMB_PORT_BASE;

    /*
     * FDC and SMC try to use the same non-shareable interrupt (6),
     * enable only one device.
     */
    if (pThis->fUseSmc)
        pThis->fUseFdc = false;

    /*
     * Plant ACPI tables.
     */
    /** @todo Part of this is redone by acpiR3MemSetup, we only need to init the
     *        au8RSDPPage here. However, there should be no harm in doing it
     *        twice, so the lazy bird is taking the quick way out for now. */
    RTGCPHYS32 GCPhysRsdp = apicR3FindRsdpSpace();
    if (!GCPhysRsdp)
        return PDMDEV_SET_ERROR(pDevIns, VERR_NO_MEMORY, N_("Can not find space for RSDP. ACPI is disabled"));

    rc = acpiR3PlantTables(pDevIns, pThis, pThisCC);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpROMRegister(pDevIns, GCPhysRsdp, 0x1000, pThis->au8RSDPPage, 0x1000,
                              PGMPHYS_ROM_FLAGS_PERMANENT_BINARY, "ACPI RSDP");
    AssertRCReturn(rc, rc);

    /*
     * Create the PM I/O ports.  These can be unmapped and remapped.
     */
    rc = PDMDevHlpIoPortCreateIsa(pDevIns,               1 /*cPorts*/,  acpiR3PM1aStsWrite, acpiR3Pm1aStsRead,  NULL /*pvUser*/,
                                  "ACPI PM1a Status",  NULL /*paExtDesc*/, &pThis->hIoPortPm1aSts);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateIsa(pDevIns,               1 /*cPorts*/,  acpiR3PM1aEnWrite,  acpiR3Pm1aEnRead,   NULL /*pvUser*/,
                                  "ACPI PM1a Enable",  NULL /*paExtDesc*/, &pThis->hIoPortPm1aEn);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateIsa(pDevIns,               1 /*cPorts*/,  acpiR3PM1aCtlWrite, acpiR3Pm1aCtlRead,  NULL /*pvUser*/,
                                  "ACPI PM1a Control", NULL /*paExtDesc*/, &pThis->hIoPortPm1aCtl);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateIsa(pDevIns,               1 /*cPorts*/,  NULL,               acpiPMTmrRead,      NULL /*pvUser*/,
                                  "ACPI PM Timer",     NULL /*paExtDesc*/, &pThis->hIoPortPmTimer);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateIsa(pDevIns, GPE0_BLK_LEN / 2 /*cPorts*/, acpiR3Gpe0StsWrite, acpiR3Gpe0StsRead,  NULL /*pvUser*/,
                                  "ACPI GPE0 Status",  NULL /*paExtDesc*/, &pThis->hIoPortGpe0Sts);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateIsa(pDevIns, GPE0_BLK_LEN / 2 /*cPorts*/, acpiR3Gpe0EnWrite,  acpiR3Gpe0EnRead,   NULL /*pvUser*/,
                                  "ACPI GPE0 Enable",  NULL /*paExtDesc*/, &pThis->hIoPortGpe0En);
    AssertRCReturn(rc, rc);
    rc = acpiR3MapPmIoPorts(pDevIns, pThis);
    AssertRCReturn(rc, rc);

    /*
     * Create the System Management Bus I/O ports.  These can be unmapped and remapped.
     */
    rc = PDMDevHlpIoPortCreateIsa(pDevIns, 16, acpiR3SMBusWrite, acpiR3SMBusRead, NULL /*pvUser*/,
                                  "SMBus", NULL /*paExtDesc*/, &pThis->hIoPortSMBus);
    AssertRCReturn(rc, rc);
    rc = acpiR3MapSMBusIoPorts(pDevIns, pThis);
    AssertRCReturn(rc, rc);

    /*
     * Create and map the fixed I/O ports.
     */
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, SMI_CMD,          1, acpiR3SmiWrite, NULL,
                                     "ACPI SMI",                    NULL /*paExtDesc*/, &pThis->hIoPortSmi);
    AssertRCReturn(rc, rc);
#ifdef DEBUG_ACPI
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, DEBUG_HEX,        1, acpiR3DebugHexWrite, NULL,
                                     "ACPI Debug hex",              NULL /*paExtDesc*/, &pThis->hIoPortDebugHex);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, DEBUG_CHR,        1, acpiR3DebugCharWrite, NULL,
                                     "ACPI Debug char",             NULL /*paExtDesc*/, &pThis->hIoPortDebugChar);
    AssertRCReturn(rc, rc);
#endif
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, BAT_INDEX,        1, acpiR3BatIndexWrite, NULL,
                                     "ACPI Battery status index",   NULL /*paExtDesc*/, &pThis->hIoPortBatteryIndex);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, BAT_DATA,         1, NULL, acpiR3BatDataRead,
                                     "ACPI Battery status data",    NULL /*paExtDesc*/, &pThis->hIoPortBatteryData);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, SYSI_INDEX,       1, acpiR3SysInfoIndexWrite, NULL,
                                     "ACPI system info index",      NULL /*paExtDesc*/, &pThis->hIoPortSysInfoIndex);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, SYSI_DATA,        1, acpiR3SysInfoDataWrite, acpiR3SysInfoDataRead,
                                     "ACPI system info data",       NULL /*paExtDesc*/, &pThis->hIoPortSysInfoData);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, ACPI_RESET_BLK,   1, acpiR3ResetWrite, NULL,
                                     "ACPI Reset",                  NULL /*paExtDesc*/, &pThis->hIoPortReset);
    AssertRCReturn(rc, rc);

    /*
     * Create the PM timer.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, acpiR3PmTimer, NULL /*pvUser*/,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, "ACPI PM", &pThis->hPmTimer);
    AssertRCReturn(rc, rc);

    PDMDevHlpTimerLockClock(pDevIns, pThis->hPmTimer, VERR_IGNORED);
    pThis->u64PmTimerInitial = PDMDevHlpTimerGet(pDevIns, pThis->hPmTimer);
    acpiR3PmTimerReset(pDevIns, pThis, pThis->u64PmTimerInitial);
    PDMDevHlpTimerUnlockClock(pDevIns, pThis->hPmTimer);

    /*
     * Set up the PCI device.
     */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetVendorId(pPciDev,      0x8086); /* Intel */
    PDMPciDevSetDeviceId(pPciDev,      0x7113); /* 82371AB */

    /* See p. 50 of PIIX4 manual */
    PDMPciDevSetCommand(pPciDev,       PCI_COMMAND_IOACCESS);
    PDMPciDevSetStatus(pPciDev,        0x0280);

    PDMPciDevSetRevisionId(pPciDev,    0x08);

    PDMPciDevSetClassProg(pPciDev,     0x00);
    PDMPciDevSetClassSub(pPciDev,      0x80);
    PDMPciDevSetClassBase(pPciDev,     0x06);

    PDMPciDevSetHeaderType(pPciDev,    0x80);

    PDMPciDevSetBIST(pPciDev,          0x00);

    PDMPciDevSetInterruptLine(pPciDev, SCI_INT);
    PDMPciDevSetInterruptPin(pPciDev,  0x01);

    Assert((pThis->uPmIoPortBase & 0x003f) == 0);
    acpiR3PmPCIBIOSFake(pDevIns, pThis);

    Assert((pThis->uSMBusIoPortBase & 0x000f) == 0);
    acpiR3SMBusPCIBIOSFake(pDevIns, pThis);
    acpiR3SMBusResetDevice(pThis);

    rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpPCIInterceptConfigAccesses(pDevIns, pPciDev, acpiR3PciConfigRead, acpiR3PciConfigWrite);
    AssertRCReturn(rc, rc);

    /*
     * Register the saved state.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, 8, sizeof(*pThis), acpiR3SaveState, acpiR3LoadState);
    AssertRCReturn(rc, rc);

   /*
    * Get the corresponding connector interface
    */
   rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThisCC->IBase, &pThisCC->pDrvBase, "ACPI Driver Port");
   if (RT_SUCCESS(rc))
   {
       pThisCC->pDrv = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMIACPICONNECTOR);
       if (!pThisCC->pDrv)
           return PDMDEV_SET_ERROR(pDevIns, VERR_PDM_MISSING_INTERFACE, N_("LUN #0 doesn't have an ACPI connector interface"));
   }
   else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
   {
       Log(("acpi: %s/%d: warning: no driver attached to LUN #0!\n", pDevIns->pReg->szName, pDevIns->iInstance));
       rc = VINF_SUCCESS;
   }
   else
       return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to attach LUN #0"));

    PDMDevHlpDBGFInfoRegister(pDevIns, "acpi", "ACPI info", acpiR3Info);

    return rc;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) acpiRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PACPISTATE pThis = PDMDEVINS_2_DATA(pDevIns, PACPISTATE);

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /* Only the PM timer read port is handled directly in ring-0/raw-mode. */
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortPmTimer, NULL, acpiPMTmrRead, NULL);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceACPI =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "acpi",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_ACPI,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(ACPISTATE),
    /* .cbInstanceCC = */           CTX_EXPR(sizeof(ACPISTATER3), 0, 0),
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Advanced Configuration and Power Interface",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           acpiR3Construct,
    /* .pfnDestruct = */            acpiR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            acpiR3MemSetup,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               acpiR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              acpiR3Resume,
    /* .pfnAttach = */              acpiR3Attach,
    /* .pfnDetach = */              acpiR3Detach,
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
    /* .pfnConstruct = */           acpiRZConstruct,
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
    /* .pfnConstruct = */           acpiRZConstruct,
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
