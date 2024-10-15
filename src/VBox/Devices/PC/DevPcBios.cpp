/* $Id: DevPcBios.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPcBios - PC BIOS Device.
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
#define LOG_GROUP LOG_GROUP_DEV_PC_BIOS
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmstorageifs.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/cpum.h>

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/cdefs.h>
#include <VBox/bios.h>
#include <VBox/err.h>
#include <VBox/param.h>

#include "VBoxDD.h"
#include "VBoxDD2.h"
#include "DevPcBios.h"
#include "DevFwCommon.h"

#define NET_BOOT_DEVS   4


/** @page pg_devbios_cmos_assign    CMOS Assignments (BIOS)
 *
 * The BIOS uses a CMOS to store configuration data.
 * It is currently used as follows:
 *
 * @verbatim
  First CMOS bank (offsets 0x00 to 0x7f):
    Floppy drive type:
         0x10
    Hard disk type (old):
         0x12
    Equipment byte:
         0x14
    Base memory:
         0x15
         0x16
    Extended memory:
         0x17
         0x18
         0x30
         0x31
    First IDE HDD:
         0x19
         0x1e - 0x25
    Second IDE HDD:
         0x1a
         0x26 - 0x2d
    Checksum of 0x10-0x2d:
         0x2e
         0x2f
    Amount of memory above 16M and below 4GB in 64KB units:
         0x34
         0x35
    Boot device (BOCHS BIOS specific):
         0x38
         0x3c
         0x3d
    PXE debug:
         0x3f
    First SATA HDD:
         0x40 - 0x47
    Second SATA HDD:
         0x48 - 0x4f
    Third SATA HDD:
         0x50 - 0x57
    Fourth SATA HDD:
         0x58 - 0x5f
    Number of CPUs:
         0x60
    RAM above 4G in 64KB units:
         0x61 - 0x65
    Third IDE HDD:
         0x67 - 0x6e
    Fourth IDE HDD:
         0x70 - 0x77
    APIC/x2APIC settings:
         0x78

  Second CMOS bank (offsets 0x80 to 0xff):
    Reserved for internal use by PXE ROM:
         0x80 - 0x81
    First net boot device PCI bus/dev/fn:
         0x82 - 0x83
    Second to third net boot devices:
         0x84 - 0x89
    First SCSI HDD:
         0x90 - 0x97
    Second SCSI HDD:
         0x98 - 0x9f
    Third SCSI HDD:
         0xa0 - 0xa7
    Fourth SCSI HDD:
         0xa8 - 0xaf

@endverbatim
 *
 * @todo Mark which bits are compatible with which BIOSes and
 *       which are our own definitions.
 */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * The boot device.
 */
typedef enum DEVPCBIOSBOOT
{
    DEVPCBIOSBOOT_NONE,
    DEVPCBIOSBOOT_FLOPPY,
    DEVPCBIOSBOOT_HD,
    DEVPCBIOSBOOT_DVD,
    DEVPCBIOSBOOT_LAN
} DEVPCBIOSBOOT;

/**
 * PC Bios instance data structure.
 */
typedef struct DEVPCBIOS
{
    /** Pointer back to the device instance. */
    PPDMDEVINS      pDevIns;

    /** Boot devices (ordered). */
    DEVPCBIOSBOOT   aenmBootDevice[4];
    /** Bochs control string index. */
    uint32_t        iControl;
    /** Floppy device. */
    char           *pszFDDevice;
    /** Harddisk device. */
    char           *pszHDDevice;
    /** Sata harddisk device. */
    char           *pszSataDevice;
    /** LUNs of the four BIOS-accessible SATA disks. */
    uint32_t        iSataHDLUN[4];
    /** SCSI harddisk device. */
    char           *pszScsiDevice;
    /** LUNs of the four BIOS-accessible SCSI disks. */
    uint32_t        iScsiHDLUN[4];
    /** Bios message buffer. */
    char            szMsg[256];
    /** Bios message buffer index. */
    uint32_t        iMsg;
    /** The system BIOS ROM data. */
    uint8_t        *pu8PcBios;
    /** The size of the system BIOS ROM. */
    uint32_t        cbPcBios;
    /** The name of the BIOS ROM file. */
    char           *pszPcBiosFile;
    /** The LAN boot ROM data. */
    uint8_t        *pu8LanBoot;
    /** The name of the LAN boot ROM file. */
    char           *pszLanBootFile;
    /** The size of the LAN boot ROM. */
    uint64_t        cbLanBoot;
    /** The DMI tables. */
    uint8_t         au8DMIPage[0x1000];
    /** The boot countdown (in seconds). */
    uint8_t         uBootDelay;
    /** I/O-APIC enabled? */
    uint8_t         u8IOAPIC;
    /** APIC mode to be set up by BIOS */
    uint8_t         u8APICMode;
    /** PXE debug logging enabled? */
    uint8_t         u8PXEDebug;
    /** Physical address of the MP table. */
    uint32_t        u32MPTableAddr;
    /** PXE boot PCI bus/dev/fn list. */
    uint16_t        au16NetBootDev[NET_BOOT_DEVS];
    /** Number of logical CPUs in guest */
    uint16_t        cCpus;
    /* Physical address of PCI config space MMIO region. Currently unused. */
    uint64_t        u64McfgBase;
    /* Length of PCI config space MMIO region. Currently unused. */
    uint64_t        cbMcfgLength;

    /** Firmware registration structure.   */
    PDMFWREG        FwReg;
    /** Dummy. */
    PCPDMFWHLPR3    pFwHlpR3;
    /** Number of soft resets we've logged. */
    uint32_t        cLoggedSoftResets;
    /** Whether to consult the shutdown status (CMOS[0xf]) for deciding upon soft
     * or hard reset. */
    bool            fCheckShutdownStatusForSoftReset;
    /** Whether to clear the shutdown status on hard reset. */
    bool            fClearShutdownStatusOnHardReset;
    /** Current port number for BIOS control (used by APM). */
    RTIOPORT        ControlPort;
    /** True=use new port number for BIOS control (used by APM). */
    bool            fNewControlPort;
    bool            afPadding[3+4];
    /** The BIOS I/O port, either at 0x040f or 0x8900
     *  (old saved state). */
    IOMMMIOHANDLE   hIoPortControl;
} DEVPCBIOS;
/** Pointer to the BIOS device state. */
typedef DEVPCBIOS *PDEVPCBIOS;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The saved state version. */
#define PCBIOS_SSM_VERSION 0


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Saved state DEVPCBIOS field descriptors. */
static SSMFIELD const g_aPcBiosFields[] =
{
    SSMFIELD_ENTRY(         DEVPCBIOS, fNewControlPort),
    SSMFIELD_ENTRY_TERM()
};


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, Bochs Debug.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
pcbiosIOPortDebugRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF5(pDevIns, pvUser, offPort, pu32, cb);
    return VERR_IOM_IOPORT_UNUSED;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, Bochs Debug.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
pcbiosIOPortDebugWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PDEVPCBIOS pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPCBIOS);
    RT_NOREF(pvUser);
    Assert(offPort < 4);

    /*
     * Bochs BIOS char printing.
     */
    if (    cb == 1
        &&  (   offPort == 2
             || offPort == 3))
    {
        /* The raw version. */
        switch (u32)
        {
            case '\r': Log2(("pcbios: <return>\n")); break;
            case '\n': Log2(("pcbios: <newline>\n")); break;
            case '\t': Log2(("pcbios: <tab>\n")); break;
            default:   Log2(("pcbios: %c (%02x)\n", u32, u32)); break;
        }

        /* The readable, buffered version. */
        uint32_t iMsg = pThis->iMsg;
        if (u32 == '\n' || u32 == '\r')
        {
            AssertStmt(iMsg < sizeof(pThis->szMsg), iMsg = sizeof(pThis->szMsg) - 1);
            pThis->szMsg[iMsg] = '\0';
            if (iMsg)
                Log(("pcbios: %s\n", pThis->szMsg));
            iMsg = 0;
        }
        else
        {
            if (iMsg >= sizeof(pThis->szMsg) - 1)
            {
                pThis->szMsg[iMsg] = '\0';
                Log(("pcbios: %s\n", pThis->szMsg));
                iMsg = 0;
            }
            pThis->szMsg[iMsg] = (char)u32;
            pThis->szMsg[++iMsg] = '\0';
        }
        pThis->iMsg = iMsg;
        return VINF_SUCCESS;
    }

    /* not in use. */
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, BIOS control port.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
pcbiosIOPortControlRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF5(pDevIns, pvUser, offPort, pu32, cb);
    return VERR_IOM_IOPORT_UNUSED;
}


static VBOXSTRICTRC pcbiosApmShutdown(PPDMDEVINS pDevIns)
{
    LogRel(("PcBios: APM shutdown request\n"));
    return PDMDevHlpVMPowerOff(pDevIns);
}


static VBOXSTRICTRC pcbiosApmIdle(PPDMDEVINS pDevIns)
{
    Log3(("PcBios: APM idle request\n"));

    /* This request is used to put the CPU into a halted state *without* using
     * a HLT instruction. This is especially useful for the APM BIOS
     * in situations where executing HLT causes problems. See BIOS APM source
     * code for comments.
     * NB: The CPU will be woken up by an interrupt regardless of the
     * state of rFLAGS.IF.
     */
    int rc = PDMDevHlpVMWaitForDeviceReady(pDevIns, PDMDevHlpGetCurrentCpuId(pDevIns));
    return rc;
}

static void pcbiosReportBootFail(PPDMDEVINS pDevIns)
{
    LogRel(("PcBios: Boot failure\n"));
    int rc = PDMDevHlpVMSetRuntimeError(pDevIns, 0 /*fFlags*/, "VMBootFail",
                                        N_("The VM failed to boot. This is possibly caused by not having an operating system installed or a misconfigured boot order. Maybe picking a guest OS install DVD will resolve the situation"));
    AssertRC(rc);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, BIOS control port.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
pcbiosIOPortControlWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PDEVPCBIOS pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPCBIOS);
    RT_NOREF(pvUser, offPort);
    Assert(offPort == 0);

    if (cb == 1)
    {
        static const unsigned char s_szShutdown[] = "Shutdown";
        static const unsigned char s_szBootfail[] = "Bootfail";
        static const unsigned char s_szProchalt[] = "Prochalt";
        AssertCompile(sizeof(s_szShutdown) == sizeof(s_szBootfail));
        AssertCompile(sizeof(s_szShutdown) == sizeof(s_szProchalt));

        if (pThis->iControl < sizeof(s_szShutdown)) /* paranoia */
        {
            if (u32 == s_szShutdown[pThis->iControl])
            {

                pThis->iControl++;
                if (pThis->iControl >= 8)
                {
                    pThis->iControl = 0;
                    return pcbiosApmShutdown(pDevIns);
                }
            }
            else if (u32 == s_szBootfail[pThis->iControl])
            {
                pThis->iControl++;
                if (pThis->iControl >= 8)
                {
                    pThis->iControl = 0;
                    pcbiosReportBootFail(pDevIns);
                }
            }
            else if (u32 == s_szProchalt[pThis->iControl])
            {

                pThis->iControl++;
                if (pThis->iControl >= 8)
                {
                    pThis->iControl = 0;
                    return pcbiosApmIdle(pDevIns);
                }
            }
            else
                pThis->iControl = 0;
        }
        else
            pThis->iControl = 0;
    }
    else if (cb == 2)
    {
        pThis->iControl = 0;
        /* Shortcuts for BIOS control, allowing guest to use one simple 16-bit I/O port write. */
        switch (u32)
        {
        case VBOX_BIOS_CTL_SHUTDOWN:
            return pcbiosApmShutdown(pDevIns);
        case VBOX_BIOS_CTL_BOOTFAIL:
            pcbiosReportBootFail(pDevIns);
            break;
        case VBOX_BIOS_CTL_PROCHALT:
            return pcbiosApmIdle(pDevIns);
        default:
            /* Ignore and do nothing. */
            LogFunc(("unrecognized control value (u32=%X)\n", u32));
        }
    }
    /* else: not in use. */

    return VINF_SUCCESS;
}


/**
 * Register the BIOS control port.
 * This is used by pcbiosConstruct, pcbiosReset and pcbiosLoadExec.
 */
static int pcbiosRegisterControl(PPDMDEVINS pDevIns, PDEVPCBIOS pThis, bool fNewControlPort)
{
    if (pThis->ControlPort != 0)
    {
        int rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortControl);
        AssertRC(rc);
    }

    pThis->fNewControlPort = fNewControlPort;
    if (fNewControlPort)
        pThis->ControlPort = VBOX_BIOS_SHUTDOWN_PORT;
    else
        pThis->ControlPort = VBOX_BIOS_OLD_SHUTDOWN_PORT;
    return PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortControl, pThis->ControlPort);
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) pcbiosSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVPCBIOS pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPCBIOS);
    return pDevIns->pHlpR3->pfnSSMPutStruct(pSSM, pThis, g_aPcBiosFields);
}


/**
 * @callback_method_impl{FNSSMDEVLOADPREP,
 *      Clears the fNewControlPort flag prior to loading the state so that old
 *      saved VM states keeps using the old port address (no pcbios state)}
 */
static DECLCALLBACK(int) pcbiosLoadPrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    PDEVPCBIOS pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPCBIOS);

    /* Since there are legacy saved state files without any SSM data for PCBIOS
     * this is the only way to handle them correctly. */
    pThis->fNewControlPort = false;

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) pcbiosLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDEVPCBIOS pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPCBIOS);

    if (uVersion > PCBIOS_SSM_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    return pDevIns->pHlpR3->pfnSSMGetStruct(pSSM, pThis, g_aPcBiosFields);
}


/**
 * @callback_method_impl{FNSSMDEVLOADDONE,
 *      Updates the shutdown port registration to match the flag loaded (or not).}
 */
static DECLCALLBACK(int) pcbiosLoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    PDEVPCBIOS pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPCBIOS);
    return pcbiosRegisterControl(pDevIns, pThis, pThis->fNewControlPort);
}


/**
 * Write to CMOS memory.
 * This is used by the init complete code.
 */
static void pcbiosCmosWrite(PPDMDEVINS pDevIns, int off, uint32_t u32Val)
{
    Assert(off < 256);
    Assert(u32Val < 256);

    int rc = PDMDevHlpCMOSWrite(pDevIns, off, u32Val);
    AssertRC(rc);
}


/**
 * Read from CMOS memory.
 * This is used by the init complete code.
 */
static uint8_t pcbiosCmosRead(PPDMDEVINS pDevIns, unsigned off)
{
    Assert(off < 256);

    uint8_t u8val;
    int rc = PDMDevHlpCMOSRead(pDevIns, off, &u8val);
    AssertRC(rc);

    return u8val;
}


/**
 * @interface_method_impl{PDMFWREG,pfnIsHardReset}
 */
static DECLCALLBACK(bool) pcbiosFw_IsHardReset(PPDMDEVINS pDevIns, uint32_t fFlags)
{
    RT_NOREF1(fFlags);
    PDEVPCBIOS pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPCBIOS);
    if (pThis->fCheckShutdownStatusForSoftReset)
    {
        uint8_t bShutdownStatus = pcbiosCmosRead(pDevIns, 0xf);
        if (   bShutdownStatus == 0x5
            || bShutdownStatus == 0x9
            || bShutdownStatus == 0xa)
        {
            const uint32_t cMaxLogged = 10;
            if (pThis->cLoggedSoftResets < cMaxLogged)
            {
                RTFAR16 Far16 = { 0xfeed, 0xface };
                PDMDevHlpPhysRead(pDevIns, 0x467, &Far16, sizeof(Far16));
                pThis->cLoggedSoftResets++;
                LogRel(("PcBios: Soft reset #%u - shutdown status %#x, warm reset vector (0040:0067) is %04x:%04x%s\n",
                        pThis->cLoggedSoftResets, bShutdownStatus, Far16.sel, Far16.off,
                        pThis->cLoggedSoftResets < cMaxLogged ? "." : " - won't log any more!"));
            }
            return false;
        }
    }
    return true;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) pcbiosReset(PPDMDEVINS pDevIns)
{
    PDEVPCBIOS pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPCBIOS);

    if (pThis->fClearShutdownStatusOnHardReset)
    {
        uint8_t bShutdownStatus = pcbiosCmosRead(pDevIns, 0xf);
        if (bShutdownStatus != 0)
        {
            LogRel(("PcBios: Clearing shutdown status code %02x.\n", bShutdownStatus));
            pcbiosCmosWrite(pDevIns, 0xf, 0);
        }
    }

    /* After reset the new BIOS code is active, use the new control port. */
    pcbiosRegisterControl(pDevIns, pThis, true /* fNewControlPort */);
}


/**
 * Attempt to guess the LCHS disk geometry from the MS-DOS master boot record
 * (partition table).
 *
 * @returns VBox status code.
 * @param   pMedia          The media device interface of the disk.
 * @param   pLCHSGeometry   Where to return the disk geometry on success
 */
static int biosGuessDiskLCHS(PPDMIMEDIA pMedia, PPDMMEDIAGEOMETRY pLCHSGeometry)
{
    uint8_t aMBR[512], *p;
    int rc;
    uint32_t iEndHead, iEndSector, cLCHSCylinders, cLCHSHeads, cLCHSSectors;

    if (!pMedia)
        return VERR_INVALID_PARAMETER;
    rc = pMedia->pfnReadPcBios(pMedia, 0, aMBR, sizeof(aMBR));
    if (RT_FAILURE(rc))
        return rc;
    /* Test MBR magic number. */
    if (aMBR[510] != 0x55 || aMBR[511] != 0xaa)
        return VERR_INVALID_PARAMETER;
    for (uint32_t i = 0; i < 4; i++)
    {
        /* Figure out the start of a partition table entry. */
        p = &aMBR[0x1be + i * 16];
        iEndHead = p[5];
        iEndSector = p[6] & 63;
        if ((p[12] | p[13] | p[14] | p[15]) && iEndSector && iEndHead)
        {
            /* Assumption: partition terminates on a cylinder boundary. */
            cLCHSHeads = iEndHead + 1;
            cLCHSSectors = iEndSector;
            cLCHSCylinders = RT_MIN(1024, pMedia->pfnGetSize(pMedia) / (512 * cLCHSHeads * cLCHSSectors));
            if (cLCHSCylinders >= 1)
            {
                pLCHSGeometry->cCylinders = cLCHSCylinders;
                pLCHSGeometry->cHeads = cLCHSHeads;
                pLCHSGeometry->cSectors = cLCHSSectors;
                Log(("%s: LCHS=%d %d %d\n", __FUNCTION__, cLCHSCylinders, cLCHSHeads, cLCHSSectors));
                return VINF_SUCCESS;
            }
        }
    }
    return VERR_INVALID_PARAMETER;
}


/* Several common PC/AT BIOS drive types. Corresponds to IBM BIOS drive tables. */
PDMMEDIAGEOMETRY aGeomPCAT[] = {
    /*  cyls    heads   sectors */
    {     0,     0,      0 },   /* Type  0 is not used */
    {   306,     4,     17 },   /* Type  1,  10MB */
    {   615,     4,     17 },   /* Type  2,  20MB */
    {   615,     6,     17 },   /* Type  3,  30MB */
    {   940,     8,     17 },   /* Type  4,  62MB */
    {   940,     6,     17 },   /* Type  5,  47MB */
    {   615,     4,     17 },   /* Type  6,  20MB (different WPCOMP from Type 2) */
    {   462,     8,     17 },   /* Type  7,  30MB */
    {   733,     5,     17 },   /* Type  8,  30MB */
    {   900,    15,     17 },   /* Type  9, 117MB */

    {   820,     3,     17 },   /* Type 10,  21MB */
    {   855,     5,     17 },   /* Type 11,  37MB */
    {   855,     7,     17 },   /* Type 12,  52MB */
    {   306,     8,     17 },   /* Type 13,  21MB */
    {   733,     7,     17 },   /* Type 14,  45MB */
    {     0,     0,      0 },   /* Type 15 is not used */
    {   612,     4,     17 },   /* Type 16,  21MB */
    {   977,     5,     17 },   /* Type 17,  43MB */
    {   977,     7,     17 },   /* Type 18,  60MB */
    {  1024,     7,     17 },   /* Type 19,  62MB */

    {   733,     5,     17 },   /* Type 20,  32MB */
    {   733,     7,     17 },   /* Type 21,  45MB */
    {   733,     5,     17 },   /* Type 22,  32MB */
    {   306,     4,     17 },   /* Type 23,  10MB */
};

/**
 * Attempts to initialize CMOS data for a hard disk matching one of
 * the PC/AT BIOS types. Only applicable to the first two drives.
 * Returns true if drive is one of the few recognized types.
 */
static bool pcbiosCmosTryPCATHardDisk(PPDMDEVINS pDevIns, int drive, PCPDMMEDIAGEOMETRY pLCHSGeometry)
{
    unsigned    type;
    unsigned    typeLow;
    bool        fCompatGeom = false;

    Assert((drive == 0) || (drive == 1));

    /* See if drive geometry is one of the ancient PC/AT BIOS types. */
    for (type = 0; type < RT_ELEMENTS(aGeomPCAT); ++type) {
        if ( (aGeomPCAT[type].cCylinders == pLCHSGeometry->cCylinders)
          && (aGeomPCAT[type].cHeads     == pLCHSGeometry->cHeads    )
          && (aGeomPCAT[type].cSectors   == pLCHSGeometry->cSectors  )) {
            LogRel(("PcBios: Recognized ATA hard disk %d as PC/AT BIOS type %d\n", drive, type));
            fCompatGeom = true;
            break;
        }
    }

    if (fCompatGeom) {
        uint32_t    u32;

        /* For types below 15, the type is in CMOS byte 0x12.
         * NB: The type for drive 0 is in the high nibble, drive 1
         * is in the low nibble.
         * For drive types above 15, CMOS byte 0x12 is set to 15
         * and actual drive type is in byte 0x19 (drive 0) or
         * byte 0x1a (drive 1).
         */
        typeLow = type < 15 ? type : 15;

        /* Always update CMOS byte 0x12. */
        u32 = pcbiosCmosRead(pDevIns, 0x12);
        u32 &= 0x0f << (4 * drive);
        u32 |= typeLow << (4 - 4 * drive);
        pcbiosCmosWrite(pDevIns, 0x12, u32);

        /* For higher drive types, also update CMOS byte 0x19/0x1a. */
        if (type > 15) {
            u32 = type;
            pcbiosCmosWrite(pDevIns, 0x19 + drive, u32);
        }
    }

    return fCompatGeom;
}


/**
 * Initializes the CMOS data for one harddisk.
 */
static void pcbiosCmosInitHardDisk(PPDMDEVINS pDevIns, int offType, int offInfo, PCPDMMEDIAGEOMETRY pLCHSGeometry)
{
    Log2(("%s: offInfo=%#x: LCHS=%d/%d/%d\n", __FUNCTION__, offInfo, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    if (offType) {
        uint32_t    u32;
        int         drive;

        Assert(offType == 0x1a || offType == 0x19);
        drive = offType == 0x19 ? 0 : 1;

        /* Update CMOS byte 12h. It will always be set to type 0fh for this disk. */
        u32 = pcbiosCmosRead(pDevIns, 0x12);
        u32 &= 0x0f << (4 * drive);
        u32 |= 0x0f << (4 - 4 * drive);
        pcbiosCmosWrite(pDevIns, 0x12, u32);

        /* Now write the extended drive type at offset 19h or 1Ah. */
        pcbiosCmosWrite(pDevIns, offType, 47);
    }
    /* Cylinders low */
    pcbiosCmosWrite(pDevIns, offInfo + 0, RT_MIN(pLCHSGeometry->cCylinders, 1024) & 0xff);
    /* Cylinders high */
    pcbiosCmosWrite(pDevIns, offInfo + 1, RT_MIN(pLCHSGeometry->cCylinders, 1024) >> 8);
    /* Heads */
    pcbiosCmosWrite(pDevIns, offInfo + 2, pLCHSGeometry->cHeads);
    /* Landing zone low */
    pcbiosCmosWrite(pDevIns, offInfo + 3, 0xff);
    /* Landing zone high */
    pcbiosCmosWrite(pDevIns, offInfo + 4, 0xff);
    /* Write precomp low */
    pcbiosCmosWrite(pDevIns, offInfo + 5, 0xff);
    /* Write precomp high */
    pcbiosCmosWrite(pDevIns, offInfo + 6, 0xff);
    /* Sectors */
    pcbiosCmosWrite(pDevIns, offInfo + 7, pLCHSGeometry->cSectors);
}


/**
 * Set logical CHS geometry for a hard disk
 *
 * @returns VBox status code.
 * @param   pBase         Base interface for the device.
 * @param   pHardDisk     The hard disk.
 * @param   pLCHSGeometry Where to store the geometry settings.
 */
static int setLogicalDiskGeometry(PPDMIBASE pBase, PPDMIMEDIA pHardDisk, PPDMMEDIAGEOMETRY pLCHSGeometry)
{
    RT_NOREF1(pBase);

    PDMMEDIAGEOMETRY LCHSGeometry;
    int rc = pHardDisk->pfnBiosGetLCHSGeometry(pHardDisk, &LCHSGeometry);
    if (   rc == VERR_PDM_GEOMETRY_NOT_SET
        || LCHSGeometry.cCylinders == 0
        || LCHSGeometry.cHeads == 0
        || LCHSGeometry.cHeads > 255
        || LCHSGeometry.cSectors == 0
        || LCHSGeometry.cSectors > 63)
    {
        /* No LCHS geometry, autodetect and set. */
        rc = biosGuessDiskLCHS(pHardDisk, &LCHSGeometry);
        if (RT_FAILURE(rc))
        {
            /* Try if PCHS geometry works, otherwise fall back. */
            rc = pHardDisk->pfnBiosGetPCHSGeometry(pHardDisk, &LCHSGeometry);
        }
        if (   RT_FAILURE(rc)
            || LCHSGeometry.cCylinders == 0
            || LCHSGeometry.cCylinders > 1024
            || LCHSGeometry.cHeads == 0
            || LCHSGeometry.cHeads > 255
            || LCHSGeometry.cSectors == 0
            || LCHSGeometry.cSectors > 63)
        {
            uint64_t cSectors = pHardDisk->pfnGetSize(pHardDisk) / 512;
            if (cSectors / 16 / 63 <= 1024)
            {
                LCHSGeometry.cCylinders = RT_MAX(cSectors / 16 / 63, 1);
                LCHSGeometry.cHeads = 16;
            }
            else if (cSectors / 32 / 63 <= 1024)
            {
                LCHSGeometry.cCylinders = RT_MAX(cSectors / 32 / 63, 1);
                LCHSGeometry.cHeads = 32;
            }
            else if (cSectors / 64 / 63 <= 1024)
            {
                LCHSGeometry.cCylinders = cSectors / 64 / 63;
                LCHSGeometry.cHeads = 64;
            }
            else if (cSectors / 128 / 63 <= 1024)
            {
                LCHSGeometry.cCylinders = cSectors / 128 / 63;
                LCHSGeometry.cHeads = 128;
            }
            else
            {
                LCHSGeometry.cCylinders = RT_MIN(cSectors / 255 / 63, 1024);
                LCHSGeometry.cHeads = 255;
            }
            LCHSGeometry.cSectors = 63;

        }
        rc = pHardDisk->pfnBiosSetLCHSGeometry(pHardDisk, &LCHSGeometry);
        if (rc == VERR_VD_IMAGE_READ_ONLY)
        {
            LogRel(("PcBios: ATA failed to update LCHS geometry, read only\n"));
            rc = VINF_SUCCESS;
        }
        else if (rc == VERR_PDM_GEOMETRY_NOT_SET)
        {
            LogRel(("PcBios: ATA failed to update LCHS geometry, backend refused\n"));
            rc = VINF_SUCCESS;
        }
    }

    *pLCHSGeometry = LCHSGeometry;

    return rc;
}


/**
 * Get logical CHS geometry for a hard disk, intended for SCSI/SAS drives
 * with no physical geometry.
 *
 * @returns VBox status code.
 * @param   pHardDisk     The hard disk.
 * @param   pLCHSGeometry Where to store the geometry settings.
 */
static int getLogicalDiskGeometry(PPDMIMEDIA pHardDisk, PPDMMEDIAGEOMETRY pLCHSGeometry)
{
    PDMMEDIAGEOMETRY LCHSGeometry;
    int rc = VINF_SUCCESS;

    rc = pHardDisk->pfnBiosGetLCHSGeometry(pHardDisk, &LCHSGeometry);
    if (   rc == VERR_PDM_GEOMETRY_NOT_SET
        || LCHSGeometry.cCylinders == 0
        || LCHSGeometry.cHeads == 0
        || LCHSGeometry.cHeads > 255
        || LCHSGeometry.cSectors == 0
        || LCHSGeometry.cSectors > 63)
    {
        /* Unlike the ATA case, if the image does not provide valid logical
         * geometry, we leave things alone and let the BIOS decide what the
         * logical geometry should be.
         */
        rc = VERR_PDM_GEOMETRY_NOT_SET;
    }
    else
        *pLCHSGeometry = LCHSGeometry;

    return rc;
}


/**
 * Get BIOS boot code from enmBootDevice in order
 *
 * @todo r=bird: This is a rather silly function since the conversion is 1:1.
 */
static uint8_t getBiosBootCode(PDEVPCBIOS pThis, unsigned iOrder)
{
    switch (pThis->aenmBootDevice[iOrder])
    {
        case DEVPCBIOSBOOT_NONE:
            return 0;
        case DEVPCBIOSBOOT_FLOPPY:
            return 1;
        case DEVPCBIOSBOOT_HD:
            return 2;
        case DEVPCBIOSBOOT_DVD:
            return 3;
        case DEVPCBIOSBOOT_LAN:
            return 4;
        default:
            AssertMsgFailed(("aenmBootDevice[%d]=%d\n", iOrder, pThis->aenmBootDevice[iOrder]));
            return 0;
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnInitComplete}
 *
 * This routine will write information needed by the bios to the CMOS.
 *
 * @see     http://www.brl.ntt.co.jp/people/takehiko/interrupt/CMOS.LST.txt for
 *          a description of standard and non-standard CMOS registers.
 */
static DECLCALLBACK(int) pcbiosInitComplete(PPDMDEVINS pDevIns)
{
    PDEVPCBIOS      pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPCBIOS);
    uint32_t        u32;
    unsigned        i;
    PPDMIMEDIA      apHDs[4] = {0};
    LogFlow(("pcbiosInitComplete:\n"));

    uint64_t const  cbRamSize  = PDMDevHlpMMPhysGetRamSize(pDevIns);
    uint32_t const  cbBelow4GB = PDMDevHlpMMPhysGetRamSizeBelow4GB(pDevIns);
    uint64_t const  cbAbove4GB = PDMDevHlpMMPhysGetRamSizeAbove4GB(pDevIns);

    /*
     * Memory sizes.
     */
    /* base memory. */
    u32 = cbRamSize > 640 ? 640 : (uint32_t)cbRamSize / _1K; /* <-- this test is wrong, but it doesn't matter since we never assign less than 1MB */
    pcbiosCmosWrite(pDevIns, 0x15, RT_BYTE1(u32));  /* 15h - Base Memory in K, Low Byte */
    pcbiosCmosWrite(pDevIns, 0x16, RT_BYTE2(u32));  /* 16h - Base Memory in K, High Byte */

    /* Extended memory, up to 65MB */
    u32 = cbRamSize >= 65 * _1M ? 0xffff : ((uint32_t)cbRamSize - _1M) / _1K;
    pcbiosCmosWrite(pDevIns, 0x17, RT_BYTE1(u32));  /* 17h - Extended Memory in K, Low Byte */
    pcbiosCmosWrite(pDevIns, 0x18, RT_BYTE2(u32));  /* 18h - Extended Memory in K, High Byte */
    pcbiosCmosWrite(pDevIns, 0x30, RT_BYTE1(u32));  /* 30h - Extended Memory in K, Low Byte */
    pcbiosCmosWrite(pDevIns, 0x31, RT_BYTE2(u32));  /* 31h - Extended Memory in K, High Byte */

    /* Bochs BIOS specific? Anyway, it's the amount of memory above 16MB
       and below 4GB (as it can only hold 4GB-16M). We have to chop off the
       top 32MB or it conflict with what the ACPI tables return. (Should these
       be adjusted, we still have to chop it at 0xfffc0000 or it'll conflict
       with the high BIOS mapping.) */
    if (cbRamSize > 16 * _1M)
        u32 = (RT_MIN(cbBelow4GB, UINT32_C(0xfe000000)) - 16U * _1M) / _64K;
    else
        u32 = 0;
    pcbiosCmosWrite(pDevIns, 0x34, RT_BYTE1(u32));
    pcbiosCmosWrite(pDevIns, 0x35, RT_BYTE2(u32));

    /* Bochs/VBox BIOS specific way of specifying memory above 4GB in 64KB units.
       Bochs got these in a different location which we've already used for SATA,
       it also lacks the last two. */
    uint64_t c64KBAbove4GB = cbAbove4GB / _64K;
    /* Make sure it doesn't hit the limits of the current BIOS code (RAM limit of ~255TB). */
    AssertLogRelMsgReturn((c64KBAbove4GB >> (3 * 8)) < 255, ("%#RX64\n", c64KBAbove4GB), VERR_OUT_OF_RANGE);
    pcbiosCmosWrite(pDevIns, 0x61, RT_BYTE1(c64KBAbove4GB));
    pcbiosCmosWrite(pDevIns, 0x62, RT_BYTE2(c64KBAbove4GB));
    pcbiosCmosWrite(pDevIns, 0x63, RT_BYTE3(c64KBAbove4GB));
    pcbiosCmosWrite(pDevIns, 0x64, RT_BYTE4(c64KBAbove4GB));
    pcbiosCmosWrite(pDevIns, 0x65, RT_BYTE5(c64KBAbove4GB));

    /*
     * Number of CPUs.
     */
    pcbiosCmosWrite(pDevIns, 0x60, pThis->cCpus & 0xff);

    /*
     * APIC mode.
     */
    pcbiosCmosWrite(pDevIns, 0x78, pThis->u8APICMode);

    /*
     * Bochs BIOS specifics - boot device.
     * We do both new and old (ami-style) settings.
     * See rombios.c line ~7215 (int19_function).
     */

    uint8_t reg3d = getBiosBootCode(pThis, 0) | (getBiosBootCode(pThis, 1) << 4);
    uint8_t reg38 = /* pcbiosCmosRead(pDevIns, 0x38) | */ getBiosBootCode(pThis, 2) << 4;
    /* This is an extension. Bochs BIOS normally supports only 3 boot devices. */
    uint8_t reg3c = getBiosBootCode(pThis, 3) | (pThis->uBootDelay << 4);
    pcbiosCmosWrite(pDevIns, 0x3d, reg3d);
    pcbiosCmosWrite(pDevIns, 0x38, reg38);
    pcbiosCmosWrite(pDevIns, 0x3c, reg3c);

    /*
     * PXE debug option.
     */
    pcbiosCmosWrite(pDevIns, 0x3f, pThis->u8PXEDebug);

    /*
     * Network boot device list.
     */
    for (i = 0; i < NET_BOOT_DEVS; ++i)
    {
        pcbiosCmosWrite(pDevIns, 0x82 + i * 2, RT_BYTE1(pThis->au16NetBootDev[i]));
        pcbiosCmosWrite(pDevIns, 0x83 + i * 2, RT_BYTE2(pThis->au16NetBootDev[i]));
    }

    /*
     * Floppy drive type.
     */
    uint32_t cFDs = 0;
    u32 = 0;
    for (i = 0; i < 2; i++)
    {
        PPDMIBASE pBase;
        int rc = PDMDevHlpQueryLun(pDevIns, pThis->pszFDDevice, 0, i, &pBase);
        if (RT_SUCCESS(rc))
        {
            PPDMIMEDIA pFD = PDMIBASE_QUERY_INTERFACE(pBase, PDMIMEDIA);
            if (pFD)
            {
                cFDs++;
                unsigned cShift = i == 0 ? 4 : 0;
                switch (pFD->pfnGetType(pFD))
                {
                    case PDMMEDIATYPE_FLOPPY_360:       u32 |= 1  << cShift; break;
                    case PDMMEDIATYPE_FLOPPY_1_20:      u32 |= 2  << cShift; break;
                    case PDMMEDIATYPE_FLOPPY_720:       u32 |= 3  << cShift; break;
                    case PDMMEDIATYPE_FLOPPY_1_44:      u32 |= 4  << cShift; break;
                    case PDMMEDIATYPE_FLOPPY_2_88:      u32 |= 5  << cShift; break;
                    case PDMMEDIATYPE_FLOPPY_FAKE_15_6: u32 |= 14 << cShift; break;
                    case PDMMEDIATYPE_FLOPPY_FAKE_63_5: u32 |= 15 << cShift; break;
                    default:                        AssertFailed(); break;
                }
            }
        }
    }
    pcbiosCmosWrite(pDevIns, 0x10, u32);                                        /* 10h - Floppy Drive Type */

    /*
     * Equipment byte.
     */
    if (cFDs > 0)
        u32 = ((cFDs - 1) << 6) | 0x01;    /* floppy installed, additional drives. */
    else
        u32 = 0x00;                        /* floppy not installed. */
    u32 |= RT_BIT(1);                      /* math coprocessor installed  */
    u32 |= RT_BIT(2);                      /* keyboard enabled (or mouse?) */
    u32 |= RT_BIT(3);                      /* display enabled (monitory type is 0, i.e. vga) */
    pcbiosCmosWrite(pDevIns, 0x14, u32);                                        /* 14h - Equipment Byte */

    /*
     * IDE harddisks.
     */
    for (i = 0; i < RT_ELEMENTS(apHDs); i++)
    {
        PPDMIBASE pBase;
        int rc = PDMDevHlpQueryLun(pDevIns, pThis->pszHDDevice, 0, i, &pBase);
        if (RT_SUCCESS(rc))
            apHDs[i] = PDMIBASE_QUERY_INTERFACE(pBase, PDMIMEDIA);
        if (   apHDs[i]
            && (   apHDs[i]->pfnGetType(apHDs[i]) != PDMMEDIATYPE_HARD_DISK
                || !apHDs[i]->pfnBiosIsVisible(apHDs[i])))
            apHDs[i] = NULL;
        if (apHDs[i])
        {
            PDMMEDIAGEOMETRY LCHSGeometry;
            int rc2 = setLogicalDiskGeometry(pBase, apHDs[i], &LCHSGeometry);
            AssertRC(rc2);

            if (i < 4)
            {
                /* Award BIOS extended drive types for first to fourth disk.
                 * Used by the BIOS for setting the logical geometry. */
                int offType, offInfo;
                switch (i)
                {
                    case 0:
                        offType = 0x19;
                        offInfo = 0x1e;
                        break;
                    case 1:
                        offType = 0x1a;
                        offInfo = 0x26;
                        break;
                    case 2:
                        offType = 0x00;
                        offInfo = 0x67;
                        break;
                    case 3:
                    default:
                        offType = 0x00;
                        offInfo = 0x70;
                        break;
                }
                pcbiosCmosInitHardDisk(pDevIns, offType, offInfo, &LCHSGeometry);
                if (i < 2)
                    pcbiosCmosTryPCATHardDisk(pDevIns, i, &LCHSGeometry);
            }
            LogRel(("PcBios: ATA LUN#%d LCHS=%u/%u/%u\n", i, LCHSGeometry.cCylinders, LCHSGeometry.cHeads, LCHSGeometry.cSectors));
        }
    }

    /*
     * SATA harddisks.
     */
    if (pThis->pszSataDevice)
    {
        /* Clear pointers to the block devices. */
        for (i = 0; i < RT_ELEMENTS(apHDs); i++)
            apHDs[i] = NULL;

        for (i = 0; i < RT_ELEMENTS(apHDs); i++)
        {
            PPDMIBASE pBase;
            int rc = PDMDevHlpQueryLun(pDevIns, pThis->pszSataDevice, 0, pThis->iSataHDLUN[i], &pBase);
            if (RT_SUCCESS(rc))
                apHDs[i] = PDMIBASE_QUERY_INTERFACE(pBase, PDMIMEDIA);
            if (   apHDs[i]
                && (   apHDs[i]->pfnGetType(apHDs[i]) != PDMMEDIATYPE_HARD_DISK
                    || !apHDs[i]->pfnBiosIsVisible(apHDs[i])))
                apHDs[i] = NULL;
            if (apHDs[i])
            {
                PDMMEDIAGEOMETRY LCHSGeometry;
                rc = setLogicalDiskGeometry(pBase, apHDs[i], &LCHSGeometry);
                AssertRC(rc);

                if (i < 4)
                {
                    /* Award BIOS extended drive types for first to fourth disk.
                     * Used by the BIOS for setting the logical geometry. */
                    int offInfo;
                    switch (i)
                    {
                        case 0:
                            offInfo = 0x40;
                            break;
                        case 1:
                            offInfo = 0x48;
                            break;
                        case 2:
                            offInfo = 0x50;
                            break;
                        case 3:
                        default:
                            offInfo = 0x58;
                            break;
                    }
                    pcbiosCmosInitHardDisk(pDevIns, 0x00, offInfo,
                                           &LCHSGeometry);
                }
                LogRel(("PcBios: SATA LUN#%d LCHS=%u/%u/%u\n", i, LCHSGeometry.cCylinders, LCHSGeometry.cHeads, LCHSGeometry.cSectors));
            }
        }
    }

    /*
     * SCSI harddisks. Not handled quite the same as SATA.
     */
    if (pThis->pszScsiDevice)
    {
        /* Clear pointers to the block devices. */
        for (i = 0; i < RT_ELEMENTS(apHDs); i++)
            apHDs[i] = NULL;

        for (i = 0; i < RT_ELEMENTS(apHDs); i++)
        {
            PPDMIBASE pBase;
            int rc = PDMDevHlpQueryLun(pDevIns, pThis->pszScsiDevice, 0, pThis->iScsiHDLUN[i], &pBase);
            if (RT_SUCCESS(rc))
                apHDs[i] = PDMIBASE_QUERY_INTERFACE(pBase, PDMIMEDIA);
            if (   apHDs[i]
                && (   apHDs[i]->pfnGetType(apHDs[i]) != PDMMEDIATYPE_HARD_DISK
                    || !apHDs[i]->pfnBiosIsVisible(apHDs[i])))
                apHDs[i] = NULL;
            if (apHDs[i])
            {
                PDMMEDIAGEOMETRY LCHSGeometry;
                rc = getLogicalDiskGeometry(apHDs[i], &LCHSGeometry);

                if (i < 4 && RT_SUCCESS(rc))
                {
                    /* Extended drive information (for SCSI disks).
                     * Used by the BIOS for setting the logical geometry, but
                     * only if the image provided valid data.
                     */
                    int offInfo;
                    switch (i)
                    {
                        case 0:
                            offInfo = 0x90;
                            break;
                        case 1:
                            offInfo = 0x98;
                            break;
                        case 2:
                            offInfo = 0xa0;
                            break;
                        case 3:
                        default:
                            offInfo = 0xa8;
                            break;
                    }
                    pcbiosCmosInitHardDisk(pDevIns, 0x00, offInfo, &LCHSGeometry);
                    LogRel(("PcBios: SCSI LUN#%d LCHS=%u/%u/%u\n", i, LCHSGeometry.cCylinders, LCHSGeometry.cHeads, LCHSGeometry.cSectors));
                }
                else
                    LogRel(("PcBios: SCSI LUN#%d LCHS not provided\n", i));
            }
        }
    }

    /* Calculate and store AT-style CMOS checksum. */
    uint16_t    cksum = 0;
    for (i = 0x10; i < 0x2e; ++i)
        cksum += pcbiosCmosRead(pDevIns, i);
    pcbiosCmosWrite(pDevIns, 0x2e, RT_BYTE1(cksum));
    pcbiosCmosWrite(pDevIns, 0x2f, RT_BYTE2(cksum));

    LogFlow(("%s: returns VINF_SUCCESS\n", __FUNCTION__));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnMemSetup}
 */
static DECLCALLBACK(void) pcbiosMemSetup(PPDMDEVINS pDevIns, PDMDEVMEMSETUPCTX enmCtx)
{
    RT_NOREF1(enmCtx);
    PDEVPCBIOS pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPCBIOS);
    LogFlow(("pcbiosMemSetup:\n"));

    if (pThis->u8IOAPIC)
        FwCommonPlantMpsFloatPtr(pDevIns, pThis->u32MPTableAddr);

    /*
     * Re-shadow the LAN ROM image and make it RAM/RAM.
     *
     * This is normally done by the BIOS code, but since we're currently lacking
     * the chipset support for this we do it here (and in the constructor).
     */
    uint32_t    cPages = RT_ALIGN_64(pThis->cbLanBoot, GUEST_PAGE_SIZE) >> GUEST_PAGE_SHIFT;
    RTGCPHYS    GCPhys = VBOX_LANBOOT_SEG << 4;
    while (cPages > 0)
    {
        uint8_t abPage[GUEST_PAGE_SIZE];
        int     rc;

        /* Read the (original) ROM page and write it back to the RAM page. */
        rc = PDMDevHlpROMProtectShadow(pDevIns, GCPhys, GUEST_PAGE_SIZE, PGMROMPROT_READ_ROM_WRITE_RAM);
        AssertLogRelRC(rc);

        rc = PDMDevHlpPhysRead(pDevIns, GCPhys, abPage, GUEST_PAGE_SIZE);
        AssertLogRelRC(rc);
        if (RT_FAILURE(rc))
            memset(abPage, 0xcc, sizeof(abPage));

        rc = PDMDevHlpPhysWrite(pDevIns, GCPhys, abPage, GUEST_PAGE_SIZE);
        AssertLogRelRC(rc);

        /* Switch to the RAM/RAM mode. */
        rc = PDMDevHlpROMProtectShadow(pDevIns, GCPhys, GUEST_PAGE_SIZE, PGMROMPROT_READ_RAM_WRITE_RAM);
        AssertLogRelRC(rc);

        /* Advance */
        GCPhys += GUEST_PAGE_SIZE;
        cPages--;
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) pcbiosDestruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PDEVPCBIOS  pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPCBIOS);
    LogFlow(("pcbiosDestruct:\n"));

    /*
     * Free MM heap pointers.
     */
    if (pThis->pu8PcBios)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThis->pu8PcBios);
        pThis->pu8PcBios = NULL;
    }

    if (pThis->pszPcBiosFile)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThis->pszPcBiosFile);
        pThis->pszPcBiosFile = NULL;
    }

    if (pThis->pu8LanBoot)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThis->pu8LanBoot);
        pThis->pu8LanBoot = NULL;
    }

    if (pThis->pszLanBootFile)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThis->pszLanBootFile);
        pThis->pszLanBootFile = NULL;
    }

    if (pThis->pszHDDevice)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThis->pszHDDevice);
        pThis->pszHDDevice = NULL;
    }

    if (pThis->pszFDDevice)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThis->pszFDDevice);
        pThis->pszFDDevice = NULL;
    }

    if (pThis->pszSataDevice)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThis->pszSataDevice);
        pThis->pszSataDevice = NULL;
    }

    if (pThis->pszScsiDevice)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThis->pszScsiDevice);
        pThis->pszScsiDevice = NULL;
    }

    return VINF_SUCCESS;
}


/**
 * Convert config value to DEVPCBIOSBOOT.
 *
 * @returns VBox status code.
 * @param   pDevIns         Device instance data.
 * @param   pCfg            Configuration handle.
 * @param   pszParam        The name of the value to read.
 * @param   penmBoot        Where to store the boot method.
 */
static int pcbiosBootFromCfg(PPDMDEVINS pDevIns, PCFGMNODE pCfg, const char *pszParam, DEVPCBIOSBOOT *penmBoot)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    char szBuf[64];
    int rc = pHlp->pfnCFGMQueryString(pCfg, pszParam, szBuf, sizeof(szBuf));
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"%s\" as a string failed"), pszParam);

    if (!strcmp(szBuf, "DVD") || !strcmp(szBuf, "CDROM"))
        *penmBoot = DEVPCBIOSBOOT_DVD;
    else if (!strcmp(szBuf, "IDE"))
        *penmBoot = DEVPCBIOSBOOT_HD;
    else if (!strcmp(szBuf, "FLOPPY"))
        *penmBoot = DEVPCBIOSBOOT_FLOPPY;
    else if (!strcmp(szBuf, "LAN"))
        *penmBoot = DEVPCBIOSBOOT_LAN;
    else if (!strcmp(szBuf, "NONE"))
        *penmBoot = DEVPCBIOSBOOT_NONE;
    else
        rc = PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                 N_("Configuration error: The \"%s\" value \"%s\" is unknown"),  pszParam, szBuf);
    return rc;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int)  pcbiosConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVPCBIOS      pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPCBIOS);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;
    int             cb;
    Assert(iInstance == 0); RT_NOREF(iInstance);

    /*
     * Validate configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns,
                                  "BootDevice0"
                                  "|BootDevice1"
                                  "|BootDevice2"
                                  "|BootDevice3"
                                  "|HardDiskDevice"
                                  "|SataHardDiskDevice"
                                  "|SataLUN1"
                                  "|SataLUN2"
                                  "|SataLUN3"
                                  "|SataLUN4"
                                  "|ScsiHardDiskDevice"
                                  "|ScsiLUN1"
                                  "|ScsiLUN2"
                                  "|ScsiLUN3"
                                  "|ScsiLUN4"
                                  "|FloppyDevice"
                                  "|DelayBoot"
                                  "|BiosRom"
                                  "|LanBootRom"
                                  "|PXEDebug"
                                  "|UUID"
                                  "|UuidLe"
                                  "|IOAPIC"
                                  "|APIC"
                                  "|NumCPUs"
                                  "|McfgBase"
                                  "|McfgLength"
                                  "|DmiBIOSFirmwareMajor"
                                  "|DmiBIOSFirmwareMinor"
                                  "|DmiBIOSReleaseDate"
                                  "|DmiBIOSReleaseMajor"
                                  "|DmiBIOSReleaseMinor"
                                  "|DmiBIOSVendor"
                                  "|DmiBIOSVersion"
                                  "|DmiSystemFamily"
                                  "|DmiSystemProduct"
                                  "|DmiSystemSerial"
                                  "|DmiSystemSKU"
                                  "|DmiSystemUuid"
                                  "|DmiSystemVendor"
                                  "|DmiSystemVersion"
                                  "|DmiBoardAssetTag"
                                  "|DmiBoardBoardType"
                                  "|DmiBoardLocInChass"
                                  "|DmiBoardProduct"
                                  "|DmiBoardSerial"
                                  "|DmiBoardVendor"
                                  "|DmiBoardVersion"
                                  "|DmiChassisAssetTag"
                                  "|DmiChassisSerial"
                                  "|DmiChassisType"
                                  "|DmiChassisVendor"
                                  "|DmiChassisVersion"
                                  "|DmiProcManufacturer"
                                  "|DmiProcVersion"
                                  "|DmiOEMVBoxVer"
                                  "|DmiOEMVBoxRev"
                                  "|DmiUseHostInfo"
                                  "|DmiExposeMemoryTable"
                                  "|DmiExposeProcInf"
                                  "|CheckShutdownStatusForSoftReset"
                                  "|ClearShutdownStatusOnHardReset"
                                  ,
                                  "NetBoot");
    /*
     * Init the data.
     */
    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "NumCPUs", &pThis->cCpus, 1);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"NumCPUs\" as integer failed"));

    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "McfgBase", &pThis->u64McfgBase, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"\" as integer failed"));
    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "McfgLength", &pThis->cbMcfgLength, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"McfgLength\" as integer failed"));


    LogRel(("PcBios: [SMP] BIOS with %u CPUs\n", pThis->cCpus));

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "IOAPIC", &pThis->u8IOAPIC, 1);
    if (RT_FAILURE (rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"IOAPIC\""));

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "APIC", &pThis->u8APICMode, 1);
    if (RT_FAILURE (rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"APIC\""));

    static const char * const s_apszBootDevices[] = { "BootDevice0", "BootDevice1", "BootDevice2", "BootDevice3" };
    Assert(RT_ELEMENTS(s_apszBootDevices) == RT_ELEMENTS(pThis->aenmBootDevice));
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aenmBootDevice); i++)
    {
        rc = pcbiosBootFromCfg(pDevIns, pCfg, s_apszBootDevices[i], &pThis->aenmBootDevice[i]);
        if (RT_FAILURE(rc))
            return rc;
    }

    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "HardDiskDevice", &pThis->pszHDDevice);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"HardDiskDevice\" as a string failed"));

    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "FloppyDevice", &pThis->pszFDDevice);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"FloppyDevice\" as a string failed"));

    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "SataHardDiskDevice", &pThis->pszSataDevice);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pThis->pszSataDevice = NULL;
    else if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"SataHardDiskDevice\" as a string failed"));

    if (pThis->pszSataDevice)
    {
        static const char * const s_apszSataDisks[] = { "SataLUN1", "SataLUN2", "SataLUN3", "SataLUN4" };
        Assert(RT_ELEMENTS(s_apszSataDisks) == RT_ELEMENTS(pThis->iSataHDLUN));
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->iSataHDLUN); i++)
        {
            rc = pHlp->pfnCFGMQueryU32(pCfg, s_apszSataDisks[i], &pThis->iSataHDLUN[i]);
            if (rc == VERR_CFGM_VALUE_NOT_FOUND)
                pThis->iSataHDLUN[i] = i;
            else if (RT_FAILURE(rc))
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("Configuration error: Querying \"%s\" as a string failed"), s_apszSataDisks);
        }
    }

    /* Repeat the exercise for SCSI drives. */
    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "ScsiHardDiskDevice", &pThis->pszScsiDevice);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pThis->pszScsiDevice = NULL;
    else if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"ScsiHardDiskDevice\" as a string failed"));

    if (pThis->pszScsiDevice)
    {
        static const char * const s_apszScsiDisks[] = { "ScsiLUN1", "ScsiLUN2", "ScsiLUN3", "ScsiLUN4" };
        Assert(RT_ELEMENTS(s_apszScsiDisks) == RT_ELEMENTS(pThis->iScsiHDLUN));
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->iScsiHDLUN); i++)
        {
            rc = pHlp->pfnCFGMQueryU32(pCfg, s_apszScsiDisks[i], &pThis->iScsiHDLUN[i]);
            if (rc == VERR_CFGM_VALUE_NOT_FOUND)
                pThis->iScsiHDLUN[i] = i;
            else if (RT_FAILURE(rc))
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("Configuration error: Querying \"%s\" as a string failed"), s_apszScsiDisks);
        }
    }

    /* PXE debug logging option. */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "PXEDebug", &pThis->u8PXEDebug, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"PXEDebug\" as integer failed"));


    /*
     * Register the I/O Ports.
     */
    IOMIOPORTHANDLE hIoPorts;
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, 0x400 /*uPort*/, 4 /*cPorts*/, pcbiosIOPortDebugWrite, pcbiosIOPortDebugRead,
                                     "Bochs PC BIOS - Panic & Debug", NULL, &hIoPorts);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpIoPortCreateIsa(pDevIns, 1 /*cPorts*/, pcbiosIOPortControlWrite, pcbiosIOPortControlRead, NULL /*pvUser*/,
                                  "PC BIOS - Control", NULL /*paExtDescs*/, &pThis->hIoPortControl);
    AssertRCReturn(rc, rc);
    rc = pcbiosRegisterControl(pDevIns, pThis, true /* fNewControlPort */);
    AssertRCReturn(rc, rc);

    /*
     * Register SSM handlers, for remembering which shutdown port to use.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, PCBIOS_SSM_VERSION, 1 /* cbGuess */, NULL,
                                NULL, NULL, NULL,
                                NULL, pcbiosSaveExec, NULL,
                                pcbiosLoadPrep, pcbiosLoadExec, pcbiosLoadDone);

    /* Clear the net boot device list. All bits set invokes old behavior,
     * as if no second CMOS bank was present.
     */
    memset(&pThis->au16NetBootDev, 0xff, sizeof(pThis->au16NetBootDev));

    /*
     * Determine the network boot order.
     */
    PCFGMNODE pCfgNetBoot = pHlp->pfnCFGMGetChild(pCfg, "NetBoot");
    if (pCfgNetBoot == NULL)
    {
        /* Do nothing. */
        rc = VINF_SUCCESS;
    }
    else
    {
        PCFGMNODE   pCfgNetBootDevice;
        uint8_t     u8PciBus;
        uint8_t     u8PciDev;
        uint8_t     u8PciFn;
        uint16_t    u16BusDevFn;
        char        szIndex[] = "?";

        Assert(pCfgNetBoot);
        for (unsigned i = 0; i < NET_BOOT_DEVS; ++i)
        {
            szIndex[0] = '0' + i;
            pCfgNetBootDevice = pHlp->pfnCFGMGetChild(pCfgNetBoot, szIndex);

            rc = pHlp->pfnCFGMQueryU8(pCfgNetBootDevice, "PCIBusNo", &u8PciBus);
            if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
            {
                /* Do nothing and stop iterating. */
                rc = VINF_SUCCESS;
                break;
            }
            else if (RT_FAILURE(rc))
                return PDMDEV_SET_ERROR(pDevIns, rc,
                                        N_("Configuration error: Querying \"Netboot/x/PCIBusNo\" as integer failed"));
            rc = pHlp->pfnCFGMQueryU8(pCfgNetBootDevice, "PCIDeviceNo", &u8PciDev);
            if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
            {
                /* Do nothing and stop iterating. */
                rc = VINF_SUCCESS;
                break;
            }
            else if (RT_FAILURE(rc))
                return PDMDEV_SET_ERROR(pDevIns, rc,
                                        N_("Configuration error: Querying \"Netboot/x/PCIDeviceNo\" as integer failed"));
            rc = pHlp->pfnCFGMQueryU8(pCfgNetBootDevice, "PCIFunctionNo", &u8PciFn);
            if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
            {
                /* Do nothing and stop iterating. */
                rc = VINF_SUCCESS;
                break;
            }
            else if (RT_FAILURE(rc))
                return PDMDEV_SET_ERROR(pDevIns, rc,
                                        N_("Configuration error: Querying \"Netboot/x/PCIFunctionNo\" as integer failed"));
            u16BusDevFn = (((uint16_t)u8PciBus) << 8) | ((u8PciDev & 0x1F) << 3) | (u8PciFn & 0x7);
            pThis->au16NetBootDev[i] = u16BusDevFn;
        }
    }

    /*
     * Get the system BIOS ROM file name.
     */
    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "BiosRom", &pThis->pszPcBiosFile);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
    {
        pThis->pszPcBiosFile = NULL;
        rc = VINF_SUCCESS;
    }
    else if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"BiosRom\" as a string failed"));
    else if (!*pThis->pszPcBiosFile)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThis->pszPcBiosFile);
        pThis->pszPcBiosFile = NULL;
    }

    /*
     * Get the CPU arch so we can load the appropriate ROMs.
     */
    CPUMMICROARCH const enmMicroarch = PDMDevHlpCpuGetGuestMicroarch(pDevIns);

    if (pThis->pszPcBiosFile)
    {
        /*
         * Load the BIOS ROM.
         */
        RTFILE hFilePcBios;
        rc = RTFileOpen(&hFilePcBios, pThis->pszPcBiosFile,
                        RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
        if (RT_SUCCESS(rc))
        {
            /* Figure the size and check restrictions. */
            uint64_t cbPcBios;
            rc = RTFileQuerySize(hFilePcBios, &cbPcBios);
            if (RT_SUCCESS(rc))
            {
                pThis->cbPcBios = (uint32_t)cbPcBios;
                if (    RT_ALIGN(pThis->cbPcBios, _64K) == pThis->cbPcBios
                    &&  pThis->cbPcBios == cbPcBios
                    &&  pThis->cbPcBios <= 32 * _64K
                    &&  pThis->cbPcBios >= _64K)
                {
                    pThis->pu8PcBios = (uint8_t *)PDMDevHlpMMHeapAlloc(pDevIns, pThis->cbPcBios);
                    if (pThis->pu8PcBios)
                    {
                        rc = RTFileRead(hFilePcBios, pThis->pu8PcBios, pThis->cbPcBios, NULL);
                        if (RT_FAILURE(rc))
                            rc = PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                                     N_("Error reading the BIOS image ('%s)"), pThis->pszPcBiosFile);
                    }
                    else
                        rc = PDMDevHlpVMSetError(pDevIns, VERR_NO_MEMORY, RT_SRC_POS,
                                                 N_("Failed to allocate %#x bytes for loading the BIOS image"),
                                                 pThis->cbPcBios);
                }
                else
                    rc = PDMDevHlpVMSetError(pDevIns, VERR_OUT_OF_RANGE, RT_SRC_POS,
                                             N_("Invalid system BIOS file size ('%s'): %#llx (%llu)"),
                                             pThis->pszPcBiosFile, cbPcBios, cbPcBios);
            }
            else
                rc = PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                         N_("Failed to query the system BIOS file size ('%s')"),
                                         pThis->pszPcBiosFile);
            RTFileClose(hFilePcBios);
        }
        else
            rc = PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                     N_("Failed to open system BIOS file '%s'"), pThis->pszPcBiosFile);
        if (RT_FAILURE(rc))
            return rc;

        LogRel(("PcBios: Using BIOS ROM '%s' with a size of %#x bytes\n", pThis->pszPcBiosFile, pThis->cbPcBios));
    }
    else
    {
        /*
         * Use one of the embedded BIOS ROM images.
         */
        uint8_t const *pbBios;
        uint32_t       cbBios;
        if (   enmMicroarch == kCpumMicroarch_Intel_8086
            || enmMicroarch == kCpumMicroarch_Intel_80186
            || enmMicroarch == kCpumMicroarch_NEC_V20
            || enmMicroarch == kCpumMicroarch_NEC_V30)
        {
            pbBios = g_abPcBiosBinary8086;
            cbBios = g_cbPcBiosBinary8086;
            LogRel(("PcBios: Using the 8086 BIOS image!\n"));
        }
        else if (enmMicroarch == kCpumMicroarch_Intel_80286)
        {
            pbBios = g_abPcBiosBinary286;
            cbBios = g_cbPcBiosBinary286;
            LogRel(("PcBios: Using the 286 BIOS image!\n"));
        }
        else
        {
            pbBios = g_abPcBiosBinary386;
            cbBios = g_cbPcBiosBinary386;
            LogRel(("PcBios: Using the 386+ BIOS image.\n"));
        }
        pThis->pu8PcBios = (uint8_t *)PDMDevHlpMMHeapAlloc(pDevIns, cbBios);
        if (pThis->pu8PcBios)
        {
            pThis->cbPcBios = cbBios;
            memcpy(pThis->pu8PcBios, pbBios, cbBios);
        }
        else
            return PDMDevHlpVMSetError(pDevIns, VERR_NO_MEMORY, RT_SRC_POS,
                                       N_("Failed to allocate %#x bytes for loading the embedded BIOS image"), cbBios);
    }
    const uint8_t *pu8PcBiosBinary = pThis->pu8PcBios;
    uint32_t       cbPcBiosBinary  = pThis->cbPcBios;

    /*
     * Query the machine's UUID for SMBIOS/DMI use.
     */
    RTUUID  uuid;
    rc = pHlp->pfnCFGMQueryBytes(pCfg, "UUID", &uuid, sizeof(uuid));
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"UUID\" failed"));

    bool fUuidLe;
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "UuidLe", &fUuidLe, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"UuidLe\" failed"));

    if (!fUuidLe)
    {
        /*
         * UUIDs are stored little endian actually (see chapter 7.2.1 System — UUID
         * of the DMI/SMBIOS spec) but to not force reactivation of existing guests we have
         * to carry this bug along... (see also DevEFI.cpp when changing this)
         *
         * Convert the UUID to network byte order. Not entirely straightforward as
         * parts are MSB already...
         */
        uuid.Gen.u32TimeLow = RT_H2BE_U32(uuid.Gen.u32TimeLow);
        uuid.Gen.u16TimeMid = RT_H2BE_U16(uuid.Gen.u16TimeMid);
        uuid.Gen.u16TimeHiAndVersion = RT_H2BE_U16(uuid.Gen.u16TimeHiAndVersion);
    }

    uint16_t cbDmiTables = 0;
    uint16_t cDmiTables = 0;
    rc = FwCommonPlantDMITable(pDevIns, pThis->au8DMIPage, VBOX_DMI_TABLE_SIZE,
                               &uuid, pCfg, pThis->cCpus, &cbDmiTables, &cDmiTables,
                               false /*fUefi*/);
    if (RT_FAILURE(rc))
        return rc;

    /* Look for _SM_/_DMI_ anchor strings within the BIOS and replace the table headers. */
    unsigned       offAnchor  = ~0U;
    unsigned const cbToSearch = pThis->cbPcBios - 32;
    for (unsigned off = 0; off <= cbToSearch; off += 16)
    {
        if (   pThis->pu8PcBios[off + 0x00] != '_'
            || pThis->pu8PcBios[off + 0x01] != 'S'
            || pThis->pu8PcBios[off + 0x02] != 'M'
            || pThis->pu8PcBios[off + 0x03] != '_'
            || pThis->pu8PcBios[off + 0x10] != '_'
            || pThis->pu8PcBios[off + 0x11] != 'D'
            || pThis->pu8PcBios[off + 0x12] != 'M'
            || pThis->pu8PcBios[off + 0x13] != 'I'
            || pThis->pu8PcBios[off + 0x14] != '_')
        { /* likely */ }
        else
        {
            offAnchor = off;
            FwCommonPlantSmbiosAndDmiHdrs(pDevIns, pThis->pu8PcBios + off, cbDmiTables, cDmiTables);
            break;
        }
    }
    AssertLogRel(offAnchor <= cbToSearch);

    if (pThis->u8IOAPIC)
    {
        pThis->u32MPTableAddr = VBOX_DMI_TABLE_BASE + VBOX_DMI_TABLE_SIZE;
        FwCommonPlantMpsTable(pDevIns, pThis->au8DMIPage /* aka VBOX_DMI_TABLE_BASE */ + VBOX_DMI_TABLE_SIZE,
                              _4K - VBOX_DMI_TABLE_SIZE, pThis->cCpus);
        LogRel(("PcBios: MPS table at %08x\n", pThis->u32MPTableAddr));
    }

    rc = PDMDevHlpROMRegister(pDevIns, VBOX_DMI_TABLE_BASE, _4K, pThis->au8DMIPage, _4K,
                              PGMPHYS_ROM_FLAGS_PERMANENT_BINARY, "DMI tables");
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Map the BIOS into memory.
     * There are two mappings:
     *      1. 0x000e0000 to 0x000fffff contains the last 128 kb of the bios.
     *         The bios code might be 64 kb in size, and will then start at 0xf0000.
     *      2. 0xfffxxxxx to 0xffffffff contains the entire bios.
     */
    AssertReleaseMsg(cbPcBiosBinary >= _64K, ("cbPcBiosBinary=%#x\n", cbPcBiosBinary));
    AssertReleaseMsg(RT_ALIGN_Z(cbPcBiosBinary, _64K) == cbPcBiosBinary,
                     ("cbPcBiosBinary=%#x\n", cbPcBiosBinary));
    cb = RT_MIN(cbPcBiosBinary, 128 * _1K); /* Effectively either 64 or 128K. */
    rc = PDMDevHlpROMRegister(pDevIns, 0x00100000 - cb, cb, &pu8PcBiosBinary[cbPcBiosBinary - cb], cb,
                              PGMPHYS_ROM_FLAGS_PERMANENT_BINARY, "PC BIOS - 0xfffff");
    if (RT_FAILURE(rc))
        return rc;
    rc = PDMDevHlpROMRegister(pDevIns, (uint32_t)-(int32_t)cbPcBiosBinary, cbPcBiosBinary, pu8PcBiosBinary, cbPcBiosBinary,
                              PGMPHYS_ROM_FLAGS_PERMANENT_BINARY, "PC BIOS - 0xffffffff");
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Get the LAN boot ROM file name.
     */
    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "LanBootRom", &pThis->pszLanBootFile);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
    {
        pThis->pszLanBootFile = NULL;
        rc = VINF_SUCCESS;
    }
    else if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"LanBootRom\" as a string failed"));
    else if (!*pThis->pszLanBootFile)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThis->pszLanBootFile);
        pThis->pszLanBootFile = NULL;
    }

    /*
     * Not loading LAN ROM for old CPUs.
     */
    if (   enmMicroarch != kCpumMicroarch_Intel_8086
        && enmMicroarch != kCpumMicroarch_Intel_80186
        && enmMicroarch != kCpumMicroarch_NEC_V20
        && enmMicroarch != kCpumMicroarch_NEC_V30
        && enmMicroarch != kCpumMicroarch_Intel_80286)
    {
        const uint8_t  *pu8LanBootBinary = NULL;
        uint64_t        cbLanBootBinary;
        uint64_t        cbFileLanBoot = 0;

        /*
         * Open the LAN boot ROM and figure it size.
         * Determine the LAN boot ROM size, open specified ROM file in the process.
         */
        if (pThis->pszLanBootFile)
        {
            RTFILE hFileLanBoot = NIL_RTFILE;
            rc = RTFileOpen(&hFileLanBoot, pThis->pszLanBootFile,
                            RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
            if (RT_SUCCESS(rc))
            {
                rc = RTFileQuerySize(hFileLanBoot, &cbFileLanBoot);
                if (RT_SUCCESS(rc))
                {
                    if (cbFileLanBoot <= _64K - (VBOX_LANBOOT_SEG << 4 & 0xffff))
                    {
                        LogRel(("PcBios: Using LAN ROM '%s' with a size of %#x bytes\n", pThis->pszLanBootFile, cbFileLanBoot));

                        /*
                         * Allocate buffer for the LAN boot ROM data and load it.
                         */
                        pThis->pu8LanBoot = (uint8_t *)PDMDevHlpMMHeapAllocZ(pDevIns, cbFileLanBoot);
                        if (pThis->pu8LanBoot)
                        {
                            rc = RTFileRead(hFileLanBoot, pThis->pu8LanBoot, cbFileLanBoot, NULL);
                            AssertLogRelRCReturnStmt(rc, RTFileClose(hFileLanBoot), rc);
                        }
                        else
                            rc = VERR_NO_MEMORY;
                    }
                    else
                        rc = VERR_TOO_MUCH_DATA;
                }
                RTFileClose(hFileLanBoot);
            }
            if (RT_FAILURE(rc))
            {
                /*
                 * Play stupid and ignore failures, falling back to the built-in LAN boot ROM.
                 */
                /** @todo r=bird: This should have some kind of rational. We don't usually
                 *        ignore the VM configuration.  */
                LogRel(("PcBios: Failed to open LAN boot ROM file '%s', rc=%Rrc!\n", pThis->pszLanBootFile, rc));
                PDMDevHlpMMHeapFree(pDevIns, pThis->pszLanBootFile);
                pThis->pszLanBootFile = NULL;
            }
        }

        /* If we were unable to get the data from file for whatever reason, fall
         * back to the built-in LAN boot ROM image.
         */
        if (pThis->pu8LanBoot == NULL)
        {
#ifdef VBOX_WITH_PXE_ROM
            pu8LanBootBinary = g_abNetBiosBinary;
            cbLanBootBinary  = g_cbNetBiosBinary;
#endif
        }
        else
        {
            pu8LanBootBinary = pThis->pu8LanBoot;
            cbLanBootBinary  = cbFileLanBoot;
        }

        /*
         * Map the Network Boot ROM into memory.
         *
         * Currently there is a fixed mapping: 0x000e2000 to 0x000effff contains
         * the (up to) 56 kb ROM image.  The mapping size is fixed to trouble with
         * the saved state (in PGM).
         */
        if (pu8LanBootBinary)
        {
            pThis->cbLanBoot = cbLanBootBinary;

            rc = PDMDevHlpROMRegister(pDevIns, VBOX_LANBOOT_SEG << 4,
                                      RT_MAX(cbLanBootBinary, _64K - (VBOX_LANBOOT_SEG << 4 & 0xffff)),
                                      pu8LanBootBinary, cbLanBootBinary,
                                      PGMPHYS_ROM_FLAGS_SHADOWED, "Net Boot ROM");
            AssertRCReturn(rc, rc);
        }
    }
    else if (pThis->pszLanBootFile)
        LogRel(("PcBios: Skipping LAN ROM '%s' due to ancient target CPU.\n", pThis->pszLanBootFile));
#ifdef VBOX_WITH_PXE_ROM
    else
        LogRel(("PcBios: Skipping built in ROM due to ancient target CPU.\n"));
#endif

    /*
     * Configure Boot delay.
     */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "DelayBoot", &pThis->uBootDelay, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                    N_("Configuration error: Querying \"DelayBoot\" as integer failed"));
    if (pThis->uBootDelay > 15)
        pThis->uBootDelay = 15;


    /*
     * Read shutdown status code config and register ourselves as the firmware device.
     */

    /** @cfgm{CheckShutdownStatusForSoftReset, boolean, true}
     * Whether to consult the shutdown status code (CMOS register 0Fh) to
     * determine whether the guest intended a soft or hard reset.  Currently only
     * shutdown status codes 05h, 09h and 0Ah are considered soft reset. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CheckShutdownStatusForSoftReset", &pThis->fCheckShutdownStatusForSoftReset, true);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{ClearShutdownStatusOnHardReset, boolean, true}
     * Whether to clear the shutdown status code (CMOS register 0Fh) on hard reset. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "ClearShutdownStatusOnHardReset", &pThis->fClearShutdownStatusOnHardReset, true);
    AssertLogRelRCReturn(rc, rc);

    LogRel(("PcBios: fCheckShutdownStatusForSoftReset=%RTbool  fClearShutdownStatusOnHardReset=%RTbool\n",
            pThis->fCheckShutdownStatusForSoftReset, pThis->fClearShutdownStatusOnHardReset));

    static PDMFWREG const s_FwReg = { PDM_FWREG_VERSION, pcbiosFw_IsHardReset, PDM_FWREG_VERSION };
    rc = PDMDevHlpFirmwareRegister(pDevIns, &s_FwReg, &pThis->pFwHlpR3);
    AssertLogRelRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/**
 * The device registration structure.
 */
const PDMDEVREG g_DevicePcBios =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "pcbios",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_ARCH_BIOS,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVPCBIOS),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "PC BIOS Device",
#if defined(IN_RING3)
    /* .pszRCMod = */               "",
    /* .pszR0Mod = */               "",
    /* .pfnConstruct = */           pcbiosConstruct,
    /* .pfnDestruct = */            pcbiosDestruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            pcbiosMemSetup,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               pcbiosReset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        pcbiosInitComplete,
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
    /* .pfnConstruct = */           NULL,
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
    /* .pfnConstruct = */           NULL,
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

