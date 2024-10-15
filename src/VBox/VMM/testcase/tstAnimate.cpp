/* $Id: tstAnimate.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Animation Testcase / Tool.
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
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/err.h>
#include <VBox/vmm/pdmifs.h>
#include <VBox/param.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/initterm.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/file.h>
#include <iprt/thread.h>
#include <iprt/ctype.h>
#include <iprt/uuid.h>

#include <signal.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static volatile bool g_fSignaled = false;


static void SigInterrupt(int iSignal) RT_NOTHROW_DEF
{
    NOREF(iSignal);
    signal(SIGINT, SigInterrupt);
    g_fSignaled = true;
    RTPrintf("caught SIGINT\n");
}

typedef DECLCALLBACKTYPE(int, FNSETGUESTGPR,(PVM, uint32_t));
typedef FNSETGUESTGPR *PFNSETGUESTGPR;
static int scriptGPReg(PVM pVM, char *pszVar, char *pszValue, void *pvUser)
{
    NOREF(pszVar);
    uint32_t u32;
    int rc = RTStrToUInt32Ex(pszValue, NULL, 16, &u32);
    if (RT_FAILURE(rc))
        return rc;
    return ((PFNSETGUESTGPR)(uintptr_t)pvUser)(pVM, u32);
}

typedef DECLCALLBACKTYPE(int, FNSETGUESTSEL,(PVM, uint16_t));
typedef FNSETGUESTSEL *PFNSETGUESTSEL;
static int scriptSelReg(PVM pVM, char *pszVar, char *pszValue, void *pvUser)
{
    NOREF(pszVar);
    uint16_t u16;
    int rc = RTStrToUInt16Ex(pszValue, NULL, 16, &u16);
    if (RT_FAILURE(rc))
        return rc;
    return ((PFNSETGUESTSEL)(uintptr_t)pvUser)(pVM, u16);
}

typedef DECLCALLBACKTYPE(int, FNSETGUESTSYS,(PVM, uint32_t));
typedef FNSETGUESTSYS *PFNSETGUESTSYS;
static int scriptSysReg(PVM pVM, char *pszVar, char *pszValue, void *pvUser)
{
    NOREF(pszVar);
    uint32_t u32;
    int rc = RTStrToUInt32Ex(pszValue, NULL, 16, &u32);
    if (RT_FAILURE(rc))
        return rc;
    return ((PFNSETGUESTSYS)(uintptr_t)pvUser)(pVM, u32);
}


typedef DECLCALLBACKTYPE(int, FNSETGUESTDTR,(PVM, uint32_t, uint16_t));
typedef FNSETGUESTDTR *PFNSETGUESTDTR;
static int scriptDtrReg(PVM pVM, char *pszVar, char *pszValue, void *pvUser)
{
    NOREF(pszVar);
    char *pszPart2 = strchr(pszValue, ':');
    if (!pszPart2)
        return -1;
    *pszPart2++ = '\0';
    pszPart2 = RTStrStripL(pszPart2);
    pszValue = RTStrStripR(pszValue);

    uint32_t u32;
    int rc = RTStrToUInt32Ex(pszValue, NULL, 16, &u32);
    if (RT_FAILURE(rc))
        return rc;

    uint16_t u16;
    rc = RTStrToUInt16Ex(pszPart2, NULL, 16, &u16);
    if (RT_FAILURE(rc))
        return rc;

    return ((PFNSETGUESTDTR)(uintptr_t)pvUser)(pVM, u32, u16);
}




/* variables - putting in global scope to avoid MSC warning C4640.  */
static struct
{
    const char *pszVar;
    int (*pfnHandler)(PVM pVM, char *pszVar, char *pszValue, void *pvUser);
    PFNRT pvUser;
} g_aVars[] =
{
    { "eax", scriptGPReg,  (PFNRT)CPUMSetGuestEAX },
    { "ebx", scriptGPReg,  (PFNRT)CPUMSetGuestEBX },
    { "ecx", scriptGPReg,  (PFNRT)CPUMSetGuestECX },
    { "edx", scriptGPReg,  (PFNRT)CPUMSetGuestEDX },
    { "esp", scriptGPReg,  (PFNRT)CPUMSetGuestESP },
    { "ebp", scriptGPReg,  (PFNRT)CPUMSetGuestEBP },
    { "esi", scriptGPReg,  (PFNRT)CPUMSetGuestESI },
    { "edi", scriptGPReg,  (PFNRT)CPUMSetGuestEDI },
    { "efl", scriptGPReg,  (PFNRT)CPUMSetGuestEFlags },
    { "eip", scriptGPReg,  (PFNRT)CPUMSetGuestEIP },
    { "ss",  scriptSelReg, (PFNRT)CPUMSetGuestSS },
    { "cs",  scriptSelReg, (PFNRT)CPUMSetGuestCS },
    { "ds",  scriptSelReg, (PFNRT)CPUMSetGuestDS },
    { "es",  scriptSelReg, (PFNRT)CPUMSetGuestES },
    { "fs",  scriptSelReg, (PFNRT)CPUMSetGuestFS },
    { "gs",  scriptSelReg, (PFNRT)CPUMSetGuestGS },
    { "cr0", scriptSysReg, (PFNRT)CPUMSetGuestCR0 },
    { "cr2", scriptSysReg, (PFNRT)CPUMSetGuestCR2 },
    { "cr3", scriptSysReg, (PFNRT)CPUMSetGuestCR3 },
    { "cr4", scriptSysReg, (PFNRT)CPUMSetGuestCR4 },
    { "ldtr",scriptSelReg, (PFNRT)CPUMSetGuestLDTR },
    { "tr",  scriptSelReg, (PFNRT)CPUMSetGuestTR },
    { "idtr",scriptDtrReg, (PFNRT)CPUMSetGuestIDTR },
    { "gdtr",scriptDtrReg, (PFNRT)CPUMSetGuestGDTR }
};


static int scriptCommand(PVM pVM, const char *pszIn, size_t cch)
{
    NOREF(cch);
    int rc = VINF_SUCCESS;
    char *psz = RTStrDup(pszIn);
    char *pszEqual = strchr(psz, '=');
    if (pszEqual)
    {
        /*
         * var = value
         */
        *pszEqual = '\0';
        RTStrStripR(psz);
        char *pszValue = RTStrStrip(pszEqual + 1);

        rc = -1;
        for (unsigned i = 0; i < RT_ELEMENTS(g_aVars); i++)
        {
            if (!strcmp(psz, g_aVars[i].pszVar))
            {
                rc = g_aVars[i].pfnHandler(pVM, psz, pszValue, (void *)(uintptr_t)g_aVars[i].pvUser);
                break;
            }
        }
    }

    RTStrFree(psz);
    return rc;
}

static DECLCALLBACK(int) scriptRun(PVM pVM, RTFILE File)
{
    RTPrintf("info: running script...\n");
    uint64_t cb;
    int rc = RTFileQuerySize(File, &cb);
    if (RT_SUCCESS(rc))
    {
        if (cb == 0)
            return VINF_SUCCESS;
        if (cb < _1M)
        {
            char *pszBuf = (char *)RTMemAllocZ(cb + 1);
            if (pszBuf)
            {
                rc = RTFileRead(File, pszBuf, cb, NULL);
                if (RT_SUCCESS(rc))
                {
                    pszBuf[cb] = '\0';

                    /*
                     * Now process what's in the buffer.
                     */
                    char *psz = pszBuf;
                    while (psz && *psz)
                    {
                        /* skip blanks. */
                        while (RT_C_IS_SPACE(*psz))
                            psz++;
                        if (!*psz)
                            break;

                        /* end of line */
                        char *pszNext;
                        char *pszEnd = strchr(psz, '\n');
                        if (!pszEnd)
                            pszEnd = strchr(psz, '\r');
                        if (!pszEnd)
                            pszNext = pszEnd = strchr(psz, '\0');
                        else
                            pszNext = pszEnd + 1;

                        if (*psz != ';' && *psz != '#' && *psz != '/')
                        {
                            /* strip end */
                            *pszEnd = '\0';
                            while (pszEnd > psz && RT_C_IS_SPACE(pszEnd[-1]))
                                *--pszEnd = '\0';

                            /* process the line */
                            RTPrintf("debug: executing script line '%s'\n",  psz);
                            rc = scriptCommand(pVM, psz, pszEnd - psz);
                            if (RT_FAILURE(rc))
                            {
                                RTPrintf("error: '%s' failed: %Rrc\n", psz, rc);
                                break;
                            }
                        }
                        /* else comment line */

                        /* next */
                        psz = pszNext;
                    }

                }
                else
                    RTPrintf("error: failed to read script file: %Rrc\n", rc);
                RTMemFree(pszBuf);
            }
            else
            {
                RTPrintf("error: Out of memory. (%d bytes)\n", cb + 1);
                rc = VERR_NO_MEMORY;
            }
        }
        else
            RTPrintf("error: script file is too large (0x%llx bytes)\n", cb);
    }
    else
        RTPrintf("error: couldn't get size of script file: %Rrc\n", rc);

    return rc;
}


static DECLCALLBACK(int) loadMem(PVM pVM, RTFILE File, uint64_t *poff)
{
    uint64_t off = *poff;
    RTPrintf("info: loading memory...\n");

    int rc = RTFileSeek(File, off, RTFILE_SEEK_BEGIN, NULL);
    if (RT_SUCCESS(rc))
    {
        RTGCPHYS GCPhys = 0;
        for (;;)
        {
            if (!(GCPhys % (GUEST_PAGE_SIZE * 0x1000)))
                RTPrintf("info: %RGp...\n", GCPhys);

            /* read a page from the file */
            size_t cbRead = 0;
            uint8_t au8Page[GUEST_PAGE_SIZE * 16];
            rc = RTFileRead(File, &au8Page, sizeof(au8Page), &cbRead);
            if (RT_SUCCESS(rc) && !cbRead)
                rc = RTFileRead(File, &au8Page, sizeof(au8Page), &cbRead);
            if (RT_SUCCESS(rc) && !cbRead)
                rc = VERR_EOF;
            if (RT_FAILURE(rc) || rc == VINF_EOF)
            {
                if (rc == VERR_EOF)
                    rc = VINF_SUCCESS;
                else
                    RTPrintf("error: Read error %Rrc while reading the raw memory file.\n", rc);
                break;
            }

            /* Write that page to the guest - skip known rom areas for now. */
            if (GCPhys < 0xa0000 || GCPhys >= 0x100000) /* ASSUME size of a8Page is a power of 2. */
                PGMPhysWrite(pVM, GCPhys, &au8Page, cbRead, PGMACCESSORIGIN_DEBUGGER);
            GCPhys += cbRead;
        }
    }
    else
        RTPrintf("error: Failed to seek to 0x%llx in the raw memory file. rc=%Rrc\n", off, rc);

    return rc;
}


/**
 * @callback_method_impl{FNCFGMCONSTRUCTOR, Creates the default configuration.}
 *
 * This assumes an empty tree.
 */
static DECLCALLBACK(int) cfgmR3CreateDefault(PUVM pUVM, PVM pVM, PCVMMR3VTABLE pVMM, void *pvUser)
{
    RT_NOREF(pUVM, pVMM);
    uint64_t cbMem = *(uint64_t *)pvUser;
    int rc;
    int rcAll = VINF_SUCCESS;
    bool fIOAPIC = false;
#define UPDATERC() do { if (RT_FAILURE(rc) && RT_SUCCESS(rcAll)) rcAll = rc; } while (0)

    /*
     * Create VM default values.
     */
    PCFGMNODE pRoot = CFGMR3GetRoot(pVM);
    rc = CFGMR3InsertString(pRoot,  "Name",                 "Default VM");
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "RamSize",              cbMem);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "TimerMillies",         10);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "RawR3Enabled",         0);
    UPDATERC();
    /** @todo CFGM Defaults: RawR0, PATMEnabled and CASMEnabled needs attention later. */
    rc = CFGMR3InsertInteger(pRoot, "RawR0Enabled",         0);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "PATMEnabled",          0);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "CSAMEnabled",          0);
    UPDATERC();

    /*
     * PDM.
     */
    PCFGMNODE pPdm;
    rc = CFGMR3InsertNode(pRoot, "PDM", &pPdm);
    UPDATERC();
    PCFGMNODE pDevices = NULL;
    rc = CFGMR3InsertNode(pPdm, "Devices", &pDevices);
    UPDATERC();
    rc = CFGMR3InsertInteger(pDevices, "LoadBuiltin",       1); /* boolean */
    UPDATERC();
    PCFGMNODE pDrivers = NULL;
    rc = CFGMR3InsertNode(pPdm, "Drivers", &pDrivers);
    UPDATERC();
    rc = CFGMR3InsertInteger(pDrivers, "LoadBuiltin",       1); /* boolean */
    UPDATERC();


    /*
     * Devices
     */
    pDevices = NULL;
    rc = CFGMR3InsertNode(pRoot, "Devices", &pDevices);
    UPDATERC();
    /* device */
    PCFGMNODE pDev = NULL;
    PCFGMNODE pInst = NULL;
    PCFGMNODE pCfg = NULL;
#if 0
    PCFGMNODE pLunL0 = NULL;
    PCFGMNODE pLunL1 = NULL;
#endif

    /*
     * PC Arch.
     */
    rc = CFGMR3InsertNode(pDevices, "pcarch", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();

    /*
     * PC Bios.
     */
    rc = CFGMR3InsertNode(pDevices, "pcbios", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "BootDevice0",          "IDE");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "BootDevice1",          "NONE");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "BootDevice2",          "NONE");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "BootDevice3",          "NONE");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "HardDiskDevice",       "piix3ide");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "FloppyDevice",         "i82078");
    rc = CFGMR3InsertInteger(pCfg,  "IOAPIC", fIOAPIC);                         UPDATERC();
    RTUUID Uuid;
    RTUuidClear(&Uuid);
    rc = CFGMR3InsertBytes(pCfg,    "UUID", &Uuid, sizeof(Uuid));               UPDATERC();
    /* Bios logo. */
    rc = CFGMR3InsertInteger(pCfg,  "FadeIn",               0);
    UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "FadeOut",              0);
    UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "LogoTime",             0);
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "LogoFile",             "");
    UPDATERC();

    /*
     * ACPI
     */
    rc = CFGMR3InsertNode(pDevices, "acpi", &pDev);                             UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                               UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted", 1);              /* boolean */   UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                           UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "IOAPIC", fIOAPIC);                         UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          7);                 UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                 UPDATERC();

    /*
     * DMA
     */
    rc = CFGMR3InsertNode(pDevices, "8237A", &pDev);                            UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                               UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted", 1);             /* boolean */    UPDATERC();

    /*
     * PCI bus.
     */
    rc = CFGMR3InsertNode(pDevices, "pci", &pDev); /* piix3 */
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "IOAPIC", fIOAPIC);                         UPDATERC();

    /*
     * PS/2 keyboard & mouse
     */
    rc = CFGMR3InsertNode(pDevices, "pckbd", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted", 1); /* boolean */                UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();

    /*
     * Floppy
     */
    rc = CFGMR3InsertNode(pDevices, "i82078",    &pDev);                        UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0",         &pInst);                       UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",   1);                            UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config",    &pCfg);                        UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "IRQ",       6);                            UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "DMA",       2);                            UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "MemMapped", 0 );                           UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "IOBase",    0x3f0);                        UPDATERC();

    /*
     * i8254 Programmable Interval Timer And Dummy Speaker
     */
    rc = CFGMR3InsertNode(pDevices, "i8254", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();

    /*
     * i8259 Programmable Interrupt Controller.
     */
    rc = CFGMR3InsertNode(pDevices, "i8259", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();

    /*
     * APIC.
     */
    rc = CFGMR3InsertNode(pDevices, "apic", &pDev);                                 UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "IOAPIC", fIOAPIC);                             UPDATERC();

    if (fIOAPIC)
    {
        /*
         * I/O Advanced Programmable Interrupt Controller.
         */
        rc = CFGMR3InsertNode(pDevices, "ioapic", &pDev);                           UPDATERC();
        rc = CFGMR3InsertNode(pDev,     "0", &pInst);                               UPDATERC();
        rc = CFGMR3InsertInteger(pInst, "Trusted",          1);     /* boolean */   UPDATERC();
        rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                           UPDATERC();
    }


    /*
     * RTC MC146818.
     */
    rc = CFGMR3InsertNode(pDevices, "mc146818", &pDev);                             UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               UPDATERC();

    /*
     * VGA.
     */
    rc = CFGMR3InsertNode(pDevices, "vga", &pDev);                                  UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          2);                     UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                     UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "VRamSize",             8 * _1M);               UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "CustomVideoModes",     0);
    rc = CFGMR3InsertInteger(pCfg,  "HeightReduction",      0);                     UPDATERC();
    //rc = CFGMR3InsertInteger(pCfg,  "MonitorCount",         1);                     UPDATERC();

    /*
     * IDE controller.
     */
    rc = CFGMR3InsertNode(pDevices, "piix3ide", &pDev); /* piix3 */
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          1);                     UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        1);                     UPDATERC();

    /*
     * Network card.
     */
    rc = CFGMR3InsertNode(pDevices, "pcnet", &pDev);                                UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);      /* boolean */  UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          3);                     UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                     UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "Am79C973",             1);                     UPDATERC();
    RTMAC Mac;
    Mac.au16[0] = 0x0080;
    Mac.au16[2] = Mac.au16[1] = 0x8086;
    rc = CFGMR3InsertBytes(pCfg,    "MAC", &Mac, sizeof(Mac));                      UPDATERC();

    /*
     * VMM Device
     */
    rc = CFGMR3InsertNode(pDevices, "VMMDev", &pDev);                               UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          4);                     UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                     UPDATERC();

    /*
     * ...
     */

#undef UPDATERC
    return rcAll;
}

static void syntax(void)
{
    RTPrintf("Syntax: tstAnimate < -r <raw-mem-file> | -z <saved-state> > \n"
             "              [-o <rawmem offset>]\n"
             "              [-s <script file>]\n"
             "              [-m <memory size>]\n"
             "              [-w <warp drive percent>]\n"
             "              [-p]\n"
             "\n"
             "The script is on the form:\n"
             "<reg>=<value>\n");
}


/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    RT_NOREF1(envp);
    int rcRet = 1;
    int rc;
    RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_TRY_SUPLIB);

    /*
     * Parse input.
     */
    if (argc <= 1)
    {
        syntax();
        return 1;
    }

    bool        fPowerOn = false;
    uint32_t    u32WarpDrive = 100; /* % */
    uint64_t    cbMem = ~0ULL;
    const char *pszSavedState = NULL;
    const char *pszRawMem = NULL;
    uint64_t    offRawMem = 0;
    const char *pszScript = NULL;
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            /* check that it's on short form */
            if (argv[i][2])
            {
                if (    strcmp(argv[i], "--help")
                    &&  strcmp(argv[i], "-help"))
                    RTPrintf("tstAnimate: Syntax error: Unknown argument '%s'.\n", argv[i]);
                else
                    syntax();
                return 1;
            }

            /* check for 2nd argument */
            switch (argv[i][1])
            {
                case 'r':
                case 'o':
                case 'c':
                case 'm':
                case 'w':
                case 'z':
                    if (i + 1 < argc)
                        break;
                    RTPrintf("tstAnimate: Syntax error: '%s' takes a 2nd argument.\n", argv[i]);
                    return 1;
            }

            /* process argument */
            switch (argv[i][1])
            {
                case 'r':
                    pszRawMem = argv[++i];
                    break;

                case 'z':
                    pszSavedState = argv[++i];
                    break;

                case 'o':
                {
                    rc = RTStrToUInt64Ex(argv[++i], NULL, 0, &offRawMem);
                    if (RT_FAILURE(rc))
                    {
                        RTPrintf("tstAnimate: Syntax error: Invalid offset given to -o.\n");
                        return 1;
                    }
                    break;
                }

                case 'm':
                {
                    char *pszNext;
                    rc = RTStrToUInt64Ex(argv[++i], &pszNext, 0, &cbMem);
                    if (RT_FAILURE(rc))
                    {
                        RTPrintf("tstAnimate: Syntax error: Invalid memory size given to -m.\n");
                        return 1;
                    }
                    switch (*pszNext)
                    {
                        case 'G':   cbMem *= _1G; pszNext++; break;
                        case 'M':   cbMem *= _1M; pszNext++; break;
                        case 'K':   cbMem *= _1K; pszNext++; break;
                        case '\0':  break;
                        default:
                            RTPrintf("tstAnimate: Syntax error: Invalid memory size given to -m.\n");
                            return 1;
                    }
                    if (*pszNext)
                    {
                        RTPrintf("tstAnimate: Syntax error: Invalid memory size given to -m.\n");
                        return 1;
                    }
                    break;
                }

                case 's':
                    pszScript = argv[++i];
                    break;

                case 'p':
                    fPowerOn = true;
                    break;

                case 'w':
                {
                    rc = RTStrToUInt32Ex(argv[++i], NULL, 0, &u32WarpDrive);
                    if (RT_FAILURE(rc))
                    {
                        RTPrintf("tstAnimate: Syntax error: Invalid number given to -w.\n");
                        return 1;
                    }
                    break;
                }

                case 'h':
                case 'H':
                case '?':
                    syntax();
                    return 1;

                default:
                    RTPrintf("tstAnimate: Syntax error: Unknown argument '%s'.\n", argv[i]);
                    return 1;
            }
        }
        else
        {
            RTPrintf("tstAnimate: Syntax error at arg no. %d '%s'.\n", i, argv[i]);
            syntax();
            return 1;
        }
    }

    /*
     * Check that the basic requirements are met.
     */
    if (pszRawMem && pszSavedState)
    {
        RTPrintf("tstAnimate: Syntax error: Either -z or -r, not both.\n");
        return 1;
    }
    if (!pszRawMem && !pszSavedState)
    {
        RTPrintf("tstAnimate: Syntax error: The -r argument is compulsory.\n");
        return 1;
    }

    /*
     * Open the files.
     */
    RTFILE FileRawMem = NIL_RTFILE;
    if (pszRawMem)
    {
        rc = RTFileOpen(&FileRawMem, pszRawMem, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstAnimate: error: Failed to open '%s': %Rrc\n", pszRawMem, rc);
            return 1;
        }
    }
    RTFILE FileScript = NIL_RTFILE;
    if (pszScript)
    {
        rc = RTFileOpen(&FileScript, pszScript, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstAnimate: error: Failed to open '%s': %Rrc\n", pszScript, rc);
            return 1;
        }
    }

    /*
     * Figure the memsize if not specified.
     */
    if (cbMem == ~0ULL)
    {
        if (FileRawMem != NIL_RTFILE)
        {
            rc = RTFileQuerySize(FileRawMem, &cbMem);
            AssertReleaseRC(rc);
            cbMem -= offRawMem;
            cbMem &= ~(uint64_t)GUEST_PAGE_OFFSET_MASK;
        }
        else
        {
            RTPrintf("tstAnimate: error: too lazy to figure out the memsize in a saved state.\n");
            return 1;
        }
    }
    RTPrintf("tstAnimate: info: cbMem=0x%llx bytes\n", cbMem);

    /*
     * Open a release log.
     */
    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
    PRTLOGGER pRelLogger;
    rc = RTLogCreate(&pRelLogger, RTLOGFLAGS_PREFIX_TIME_PROG, "all", "VBOX_RELEASE_LOG",
                     RT_ELEMENTS(s_apszGroups), s_apszGroups, RTLOGDEST_FILE, "./tstAnimate.log");
    if (RT_SUCCESS(rc))
        RTLogRelSetDefaultInstance(pRelLogger);
    else
        RTPrintf("tstAnimate: rtLogCreateEx failed - %Rrc\n", rc);

    /*
     * Create empty VM.
     */
    PVM pVM;
    PUVM pUVM;
    rc = VMR3Create(1 /*cCpus*/, NULL, 0 /*fFlags*/, NULL, NULL, cfgmR3CreateDefault, &cbMem, &pVM, &pUVM);
    if (RT_SUCCESS(rc))
    {
        /*
         * Load memory.
         */
        if (FileRawMem != NIL_RTFILE)
            rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)loadMem, 3, pVM, FileRawMem, &offRawMem);
        else
            rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)SSMR3Load, 7,
                                  pVM, pszSavedState, (uintptr_t)NULL /*pStreamOps*/, (uintptr_t)NULL /*pvUser*/,
                                  SSMAFTER_DEBUG_IT, (uintptr_t)NULL /*pfnProgress*/, (uintptr_t)NULL /*pvProgressUser*/);
        if (RT_SUCCESS(rc))
        {
            /*
             * Load register script.
             */
            if (FileScript != NIL_RTFILE)
                rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)scriptRun, 2, pVM, FileScript);
            if (RT_SUCCESS(rc))
            {
                if (fPowerOn)
                {
                    /*
                     * Adjust warpspeed?
                     */
                    if (u32WarpDrive != 100)
                    {
                        rc = TMR3SetWarpDrive(pUVM, u32WarpDrive);
                        if (RT_FAILURE(rc))
                            RTPrintf("warning: TMVirtualSetWarpDrive(,%u) -> %Rrc\n", u32WarpDrive, rc);
                    }

                    /*
                     * Start the thing with single stepping and stuff enabled.
                     * (Try make sure we don't execute anything in raw mode.)
                     */
                    RTPrintf("info: powering on the VM...\n");
                    RTLogGroupSettings(NULL, "+REM_DISAS.e.l.f");
                    rc = VERR_NOT_IMPLEMENTED; /** @todo need some EM single-step indicator (was REMR3DisasEnableStepping) */
                    if (RT_SUCCESS(rc))
                    {
                        rc = EMR3SetExecutionPolicy(pUVM, EMEXECPOLICY_IEM_ALL, true); AssertReleaseRC(rc);
                        DBGFR3Info(pUVM, "cpumguest", "verbose", NULL);
                        if (fPowerOn)
                            rc = VMR3PowerOn(pUVM);
                        if (RT_SUCCESS(rc))
                        {
                            RTPrintf("info: VM is running\n");
                            signal(SIGINT, SigInterrupt);
                            while (!g_fSignaled)
                                RTThreadSleep(1000);
                        }
                        else
                            RTPrintf("error: Failed to power on the VM: %Rrc\n", rc);
                    }
                    else
                        RTPrintf("error: Failed to enabled singlestepping: %Rrc\n", rc);
                }
                else
                {
                    /*
                     * Don't start it, just enter the debugger.
                     */
                    RTPrintf("info: entering debugger...\n");
                    DBGFR3Info(pUVM, "cpumguest", "verbose", NULL);
                    signal(SIGINT, SigInterrupt);
                    while (!g_fSignaled)
                        RTThreadSleep(1000);
                }
                RTPrintf("info: shutting down the VM...\n");
            }
            /* execScript complains */
        }
        else if (FileRawMem == NIL_RTFILE) /* loadMem complains, SSMR3Load doesn't */
            RTPrintf("tstAnimate: error: SSMR3Load failed: rc=%Rrc\n", rc);
        rcRet = RT_SUCCESS(rc) ? 0 : 1;

        /*
         * Cleanup.
         */
        rc = VMR3Destroy(pUVM);
        if (!RT_SUCCESS(rc))
        {
            RTPrintf("tstAnimate: error: failed to destroy vm! rc=%Rrc\n", rc);
            rcRet++;
        }

        VMR3ReleaseUVM(pUVM);
    }
    else
    {
        RTPrintf("tstAnimate: fatal error: failed to create vm! rc=%Rrc\n", rc);
        rcRet++;
    }

    return rcRet;
}


#if !defined(VBOX_WITH_HARDENING) || !defined(RT_OS_WINDOWS)
/**
 * Main entry point.
 */
int main(int argc, char **argv, char **envp)
{
    return TrustedMain(argc, argv, envp);
}
#endif

