/* $Id: DevEFI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevEFI - EFI <-> VirtualBox Integration Framework.
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
#define LOG_GROUP LOG_GROUP_DEV_EFI

#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/mm.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/vmm/dbgf.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/mp.h>
#include <iprt/list.h>
#if defined(DEBUG) && defined(IN_RING3)
# include <iprt/stream.h>
# define DEVEFI_WITH_VBOXDBG_SCRIPT
#endif
#include <iprt/utf16.h>

#ifdef IN_RING3
# include <iprt/formats/efi-fv.h>
#endif

#include "DevEFI.h"
#include "FlashCore.h"
#include "VBoxDD.h"
#include "VBoxDD2.h"
#include "../PC/DevFwCommon.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The EFI device shared state structure.
 */
typedef struct DEVEFI
{
    /** The flash device containing the NVRAM. */
    FLASHCORE               Flash;
    /** The 8 I/O ports at 0xEF10 (EFI_PORT_BASE). */
    IOMIOPORTHANDLE         hIoPorts;
    /** The flash MMIO handle. */
    IOMMMIOHANDLE           hMmioFlash;
} DEVEFI;
/** Pointer to the shared EFI state. */
typedef DEVEFI *PDEVEFI;

/**
 * The EFI device state structure for ring-3.
 */
typedef struct DEVEFIR3
{
    /** Pointer back to the device instance. */
    PPDMDEVINS              pDevIns;

    /** EFI message buffer. */
    char                    szMsg[VBOX_EFI_DEBUG_BUFFER];
    /** EFI message buffer index. */
    uint32_t                iMsg;

    /** EFI panic message buffer. */
    char                    szPanicMsg[2048];
    /** EFI panic message buffer index. */
    uint32_t                iPanicMsg;

    struct
    {
        /** The current/last image event. */
        uint8_t             uEvt;
        /** Module path/name offset. */
        uint8_t             offName;
        /** The offset of the last component in the module path/name. */
        uint8_t             offNameLastComponent;
        /** Alignment padding. */
        uint8_t             abPadding[5];
        /** First address associated with the event (image address). */
        uint64_t            uAddr0;
        /** Second address associated with the event (old image address). */
        uint64_t            uAddr1;
        /** The size associated with the event (0 if none). */
        uint64_t            cb0;
        /** The module name. */
        char                szName[256];
    } ImageEvt;

    /** The system EFI ROM data. */
    uint8_t const          *pu8EfiRom;
    /** The system EFI ROM data pointer to be passed to RTFileReadAllFree. */
    uint8_t                *pu8EfiRomFree;
    /** The size of the system EFI ROM. */
    uint64_t                cbEfiRom;
    /** Offset into the actual ROM within EFI FW volume. */
    uint64_t                offEfiRom;
    /** The name of the EFI ROM file. */
    char                   *pszEfiRomFile;
    /** Thunk page pointer. */
    uint8_t                *pu8EfiThunk;
    /** First entry point of the EFI firmware. */
    RTGCPHYS                GCEntryPoint0;
    /** Second Entry Point (PeiCore)*/
    RTGCPHYS                GCEntryPoint1;
    /** EFI firmware physical load address. */
    RTGCPHYS                GCLoadAddress;
    /** Current info selector. */
    uint32_t                iInfoSelector;
    /** Current info position. */
    int32_t                 offInfo;

    /** Number of virtual CPUs. (Config) */
    uint32_t                cCpus;

    /** The size of the DMI tables. */
    uint16_t                cbDmiTables;
    /** Number of the DMI tables. */
    uint16_t                cNumDmiTables;
    /** The DMI tables. */
    uint8_t                 au8DMIPage[0x1000];

    /** I/O-APIC enabled? */
    uint8_t                 u8IOAPIC;

    /** APIC mode to be set up by firmware. */
    uint8_t                 u8APIC;

    /** Boot parameters passed to the firmware. */
    char                    szBootArgs[256];

    /** Host UUID (for DMI). */
    RTUUID                  aUuid;

    /** Device properties buffer. */
    R3PTRTYPE(uint8_t *)    pbDeviceProps;
    /** Device properties buffer size. */
    uint32_t                cbDeviceProps;

    /** Virtual machine front side bus frequency. */
    uint64_t                u64FsbFrequency;
    /** Virtual machine time stamp counter frequency. */
    uint64_t                u64TscFrequency;
    /** Virtual machine CPU frequency. */
    uint64_t                u64CpuFrequency;
    /** EFI Graphics mode (used as fallback if resolution is not known). */
    uint32_t                u32GraphicsMode;
    /** EFI Graphics (GOP or UGA) horizontal resolution. */
    uint32_t                u32HorizontalResolution;
    /** EFI Graphics (GOP or UGA) vertical resolution. */
    uint32_t                u32VerticalResolution;
    /** Physical address of PCI config space MMIO region */
    uint64_t                u64McfgBase;
    /** Length of PCI config space MMIO region */
    uint64_t                cbMcfgLength;
    /** Physical address of the TPM PPI area. */
    uint64_t                u64TpmPpiBase;
    /** Size of the configured NVRAM device. */
    uint32_t                cbNvram;
    /** Start address of the NVRAM flash. */
    RTGCPHYS                GCPhysNvram;

    /** Filename of the file containing the NVRAM store. */
    char                    *pszNvramFile;

    /**
     * NVRAM port - LUN\#0.
     */
    struct
    {
        /** The base interface we provide the NVRAM driver. */
        PDMIBASE            IBase;
        /** The NVRAM driver base interface. */
        PPDMIBASE           pDrvBase;
        /** The VFS interface of the driver below for NVRAM state loading and storing. */
        PPDMIVFSCONNECTOR   pDrvVfs;
    } Lun0;
} DEVEFIR3;
/** Pointer to the ring-3 EFI state. */
typedef DEVEFIR3 *PDEVEFIR3;


/**
 * The EFI device state structure for ring-0.
 */
typedef struct DEVEFIR0
{
    uint32_t uEmpty;
} DEVEFIR0;
/** Pointer to the ring-0 EFI state.  */
typedef DEVEFIR0 *PDEVEFIR0;


/**
 * The EFI device state structure for raw-mode.
 */
typedef struct DEVEFIRC
{
    uint32_t uEmpty;
} DEVEFIRC;
/** Pointer to the raw-mode EFI state.  */
typedef DEVEFIRC *PDEVEFIRC;


/** @typedef DEVEFICC
 * The instance data for the current context. */
/** @typedef PDEVEFICC
 * Pointer to the instance data for the current context. */
#ifdef IN_RING3
typedef  DEVEFIR3  DEVEFICC;
typedef PDEVEFIR3 PDEVEFICC;
#elif defined(IN_RING0)
typedef  DEVEFIR0  DEVEFICC;
typedef PDEVEFIR0 PDEVEFICC;
#elif defined(IN_RC)
typedef  DEVEFIRC  DEVEFICC;
typedef PDEVEFIRC PDEVEFICC;
#else
# error "Not IN_RING3, IN_RING0 or IN_RC"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The saved state version. */
#define EFI_SSM_VERSION                  3
/** The saved state version before working NVRAM support was implemented. */
#define EFI_SSM_VERSION_PRE_PROPER_NVRAM 2
/** The saved state version from VBox 4.2. */
#define EFI_SSM_VERSION_4_2              1

/** Non-volatile EFI variable. */
#define VBOX_EFI_VARIABLE_NON_VOLATILE  UINT32_C(0x00000001)
/** Non-volatile EFI variable. */
#define VBOX_EFI_VARIABLE_READ_ONLY     UINT32_C(0x00000008)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IN_RING3
/** The EfiSystemNvDataFv GUID for NVRAM storage. */
static const RTUUID g_UuidNvDataFv = { { 0x8d, 0x2b, 0xf1, 0xff, 0x96, 0x76, 0x8b, 0x4c, 0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50} };

# ifdef VBOX_WITH_EFI_IN_DD2
/** Special file name value for indicating the 32-bit built-in EFI firmware. */
static const char g_szEfiBuiltin32[] = "VBoxEFI32.fd";
/** Special file name value for indicating the 64-bit built-in EFI firmware. */
static const char g_szEfiBuiltin64[] = "VBoxEFI64.fd";
# endif
#endif /* IN_RING3 */


#ifdef IN_RING3

/**
 * Gets the info item size.
 *
 * @returns Size in bytes, UINT32_MAX on error.
 * @param   pThisCC             The EFI state for the current context.
 */
static uint32_t efiInfoSize(PDEVEFIR3 pThisCC)
{
    switch (pThisCC->iInfoSelector)
    {
        case EFI_INFO_INDEX_VOLUME_BASE:
        case EFI_INFO_INDEX_VOLUME_SIZE:
        case EFI_INFO_INDEX_TEMPMEM_BASE:
        case EFI_INFO_INDEX_TEMPMEM_SIZE:
        case EFI_INFO_INDEX_STACK_BASE:
        case EFI_INFO_INDEX_STACK_SIZE:
        case EFI_INFO_INDEX_GRAPHICS_MODE:
        case EFI_INFO_INDEX_VERTICAL_RESOLUTION:
        case EFI_INFO_INDEX_HORIZONTAL_RESOLUTION:
        case EFI_INFO_INDEX_CPU_COUNT_CURRENT:
        case EFI_INFO_INDEX_CPU_COUNT_MAX:
            return 4;
        case EFI_INFO_INDEX_BOOT_ARGS:
            return (uint32_t)RTStrNLen(pThisCC->szBootArgs, sizeof(pThisCC->szBootArgs)) + 1;
        case EFI_INFO_INDEX_DEVICE_PROPS:
            return pThisCC->cbDeviceProps;
        case EFI_INFO_INDEX_FSB_FREQUENCY:
        case EFI_INFO_INDEX_CPU_FREQUENCY:
        case EFI_INFO_INDEX_TSC_FREQUENCY:
        case EFI_INFO_INDEX_MCFG_BASE:
        case EFI_INFO_INDEX_MCFG_SIZE:
        case EFI_INFO_INDEX_TPM_PPI_BASE:
            return 8;
        case EFI_INFO_INDEX_APIC_MODE:
            return 1;
    }
    return UINT32_MAX;
}


/**
 * efiInfoNextByte for a uint8_t value.
 *
 * @returns Next (current) byte.
 * @param   pThisCC             The EFI state for the current context.
 * @param   u8                  The value.
 */
static uint8_t efiInfoNextByteU8(PDEVEFIR3 pThisCC, uint8_t u8)
{
    uint32_t off = pThisCC->offInfo;
    if (off >= 1)
        return 0;
    return (uint8_t)u8;
}


/**
 * efiInfoNextByte for a uint64_t value.
 *
 * @returns Next (current) byte.
 * @param   pThisCC             The EFI state for the current context.
 * @param   u64                 The value.
 */
static uint8_t efiInfoNextByteU64(PDEVEFIR3 pThisCC, uint64_t u64)
{
    uint64_t off = pThisCC->offInfo;
    if (off >= 8)
        return 0;
    return (uint8_t)(u64 >> (off * 8));
}

/**
 * efiInfoNextByte for a uint32_t value.
 *
 * @returns Next (current) byte.
 * @param   pThisCC             The EFI state for the current context.
 * @param   u32                 The value.
 */
static uint8_t efiInfoNextByteU32(PDEVEFIR3 pThisCC, uint32_t u32)
{
    uint32_t off = pThisCC->offInfo;
    if (off >= 4)
        return 0;
    return (uint8_t)(u32 >> (off * 8));
}

/**
 * efiInfoNextByte for a buffer.
 *
 * @returns Next (current) byte.
 * @param   pThisCC             The EFI state for the current context.
 * @param   pvBuf               The buffer.
 * @param   cbBuf               The buffer size.
 */
static uint8_t efiInfoNextByteBuf(PDEVEFIR3 pThisCC, void const *pvBuf, size_t cbBuf)
{
    uint32_t off = pThisCC->offInfo;
    if (off >= cbBuf)
        return 0;
    return ((uint8_t const *)pvBuf)[off];
}

/**
 * Gets the next info byte.
 *
 * @returns Next (current) byte.
 * @param   pThisCC             The EFI state for the current context.
 */
static uint8_t efiInfoNextByte(PDEVEFIR3 pThisCC)
{
    switch (pThisCC->iInfoSelector)
    {

        case EFI_INFO_INDEX_VOLUME_BASE:        return efiInfoNextByteU64(pThisCC, pThisCC->GCLoadAddress);
        case EFI_INFO_INDEX_VOLUME_SIZE:        return efiInfoNextByteU64(pThisCC, pThisCC->cbEfiRom);
        case EFI_INFO_INDEX_TEMPMEM_BASE:       return efiInfoNextByteU32(pThisCC, VBOX_EFI_TOP_OF_STACK); /* just after stack */
        case EFI_INFO_INDEX_TEMPMEM_SIZE:       return efiInfoNextByteU32(pThisCC, _512K);
        case EFI_INFO_INDEX_FSB_FREQUENCY:      return efiInfoNextByteU64(pThisCC, pThisCC->u64FsbFrequency);
        case EFI_INFO_INDEX_TSC_FREQUENCY:      return efiInfoNextByteU64(pThisCC, pThisCC->u64TscFrequency);
        case EFI_INFO_INDEX_CPU_FREQUENCY:      return efiInfoNextByteU64(pThisCC, pThisCC->u64CpuFrequency);
        case EFI_INFO_INDEX_BOOT_ARGS:          return efiInfoNextByteBuf(pThisCC, pThisCC->szBootArgs, sizeof(pThisCC->szBootArgs));
        case EFI_INFO_INDEX_DEVICE_PROPS:       return efiInfoNextByteBuf(pThisCC, pThisCC->pbDeviceProps, pThisCC->cbDeviceProps);
        case EFI_INFO_INDEX_GRAPHICS_MODE:      return efiInfoNextByteU32(pThisCC, pThisCC->u32GraphicsMode);
        case EFI_INFO_INDEX_HORIZONTAL_RESOLUTION:  return efiInfoNextByteU32(pThisCC, pThisCC->u32HorizontalResolution);
        case EFI_INFO_INDEX_VERTICAL_RESOLUTION:    return efiInfoNextByteU32(pThisCC, pThisCC->u32VerticalResolution);
        case EFI_INFO_INDEX_CPU_COUNT_CURRENT:      return efiInfoNextByteU32(pThisCC, pThisCC->cCpus); /** @todo CPU hotplugging. */
        case EFI_INFO_INDEX_CPU_COUNT_MAX:          return efiInfoNextByteU32(pThisCC, pThisCC->cCpus);

        /* Keep in sync with value in EfiThunk.asm */
        case EFI_INFO_INDEX_STACK_BASE:         return efiInfoNextByteU32(pThisCC,  VBOX_EFI_TOP_OF_STACK - _128K); /* 2M - 128 K */
        case EFI_INFO_INDEX_STACK_SIZE:         return efiInfoNextByteU32(pThisCC, _128K);
        case EFI_INFO_INDEX_MCFG_BASE:          return efiInfoNextByteU64(pThisCC, pThisCC->u64McfgBase);
        case EFI_INFO_INDEX_MCFG_SIZE:          return efiInfoNextByteU64(pThisCC, pThisCC->cbMcfgLength);
        case EFI_INFO_INDEX_APIC_MODE:          return efiInfoNextByteU8(pThisCC, pThisCC->u8APIC);
        case EFI_INFO_INDEX_TPM_PPI_BASE:       return efiInfoNextByteU64(pThisCC, pThisCC->u64TpmPpiBase);

        default:
            PDMDevHlpDBGFStop(pThisCC->pDevIns, RT_SRC_POS, "%#x", pThisCC->iInfoSelector);
            return 0;
    }
}


#ifdef IN_RING3
static void efiVBoxDbgScript(const char *pszFormat, ...)
{
# ifdef DEVEFI_WITH_VBOXDBG_SCRIPT
    PRTSTREAM pStrm;
    int rc2 = RTStrmOpen("./DevEFI.VBoxDbg", "a", &pStrm);
    if (RT_SUCCESS(rc2))
    {
        va_list va;
        va_start(va, pszFormat);
        RTStrmPrintfV(pStrm, pszFormat, va);
        va_end(va);
        RTStrmClose(pStrm);
    }
# else
    RT_NOREF(pszFormat);
# endif
}
#endif /* IN_RING3 */


/**
 * Handles writes to the event port.
 *
 * @returns VBox status suitable for I/O port write handler.
 *
 * @param   pThisCC             The EFI state for the current context.
 * @param   u32                 The value being written.
 * @param   cb                  The size of the value.
 */
static int efiR3PortEventWrite(PDEVEFIR3 pThisCC, uint32_t u32, unsigned cb)
{
    if (cb == sizeof(uint16_t))
    {
        switch (u32)
        {
            case EFI_EVENT_TYPE_BOOT_FAILED:
            {
                /* No additional data for this event. */
                LogRel(("EFI: Boot failure\n"));
                int rc = PDMDevHlpVMSetRuntimeError(pThisCC->pDevIns, 0 /*fFlags*/, "VMBootFail",
                                                    N_("The VM failed to boot. This is possibly caused by not having an operating system installed or a misconfigured boot order. Maybe picking a guest OS install DVD will resolve the situation"));
                AssertRC(rc);
                break;
            }
            default:
                Log(("EFI: Unknown event: %#x (cb=%d)\n", u32, cb));
                break;
        }
    }
    else
        Log(("EFI: Invalid write size for the event port cb=%u\n", cb));

    return VINF_SUCCESS;
}


/**
 * Handles writes to the image event port.
 *
 * @returns VBox status suitable for I/O port write handler.
 *
 * @param   pThisCC             The EFI state for the current context.
 * @param   u32                 The value being written.
 * @param   cb                  The size of the value.
 */
static int efiPortImageEventWrite(PDEVEFIR3 pThisCC, uint32_t u32, unsigned cb)
{
    RT_NOREF(cb);
    switch (u32 & EFI_IMAGE_EVT_CMD_MASK)
    {
        case EFI_IMAGE_EVT_CMD_START_LOAD32:
        case EFI_IMAGE_EVT_CMD_START_LOAD64:
        case EFI_IMAGE_EVT_CMD_START_UNLOAD32:
        case EFI_IMAGE_EVT_CMD_START_UNLOAD64:
        case EFI_IMAGE_EVT_CMD_START_RELOC32:
        case EFI_IMAGE_EVT_CMD_START_RELOC64:
            AssertBreak(EFI_IMAGE_EVT_GET_PAYLOAD(u32) == 0);

            /* Reset the state. */
            RT_ZERO(pThisCC->ImageEvt);
            pThisCC->ImageEvt.uEvt = (uint8_t)u32; Assert(pThisCC->ImageEvt.uEvt == u32);
            return VINF_SUCCESS;

        case EFI_IMAGE_EVT_CMD_COMPLETE:
        {
# ifdef IN_RING3
            AssertBreak(EFI_IMAGE_EVT_GET_PAYLOAD(u32) == 0);

            /* For now, just log it. */
            static uint64_t s_cImageEvtLogged = 0;
            if (s_cImageEvtLogged < 2048)
            {
                s_cImageEvtLogged++;
                switch (pThisCC->ImageEvt.uEvt)
                {
                    /* ASSUMES the name ends with .pdb and the image file ends with .efi! */
                    case EFI_IMAGE_EVT_CMD_START_LOAD32:
                        LogRel(("EFI: VBoxDbg> loadimage32 '%.*s.efi' %#llx LB %#llx\n",
                                pThisCC->ImageEvt.offName - 4 - pThisCC->ImageEvt.offNameLastComponent,
                                &pThisCC->ImageEvt.szName[pThisCC->ImageEvt.offNameLastComponent],
                                pThisCC->ImageEvt.uAddr0, pThisCC->ImageEvt.cb0));
                        if (pThisCC->ImageEvt.offName > 4)
                            efiVBoxDbgScript("loadimage32 '%.*s.efi' %#llx\n",
                                             pThisCC->ImageEvt.offName - 4 - pThisCC->ImageEvt.offNameLastComponent,
                                             &pThisCC->ImageEvt.szName[pThisCC->ImageEvt.offNameLastComponent],
                                             pThisCC->ImageEvt.uAddr0);
                        break;
                    case EFI_IMAGE_EVT_CMD_START_LOAD64:
                        LogRel(("EFI: VBoxDbg> loadimage64 '%.*s.efi' %#llx LB %#llx\n",
                                pThisCC->ImageEvt.offName - 4 - pThisCC->ImageEvt.offNameLastComponent,
                                &pThisCC->ImageEvt.szName[pThisCC->ImageEvt.offNameLastComponent],
                                pThisCC->ImageEvt.uAddr0, pThisCC->ImageEvt.cb0));
                        if (pThisCC->ImageEvt.offName > 4)
                            efiVBoxDbgScript("loadimage64 '%.*s.efi' %#llx\n",
                                             pThisCC->ImageEvt.offName - 4 - pThisCC->ImageEvt.offNameLastComponent,
                                             &pThisCC->ImageEvt.szName[pThisCC->ImageEvt.offNameLastComponent],
                                             pThisCC->ImageEvt.uAddr0);
                        break;
                    case EFI_IMAGE_EVT_CMD_START_UNLOAD32:
                    case EFI_IMAGE_EVT_CMD_START_UNLOAD64:
                    {
                        LogRel(("EFI: VBoxDbg> unload '%.*s.efi' # %#llx LB %#llx\n",
                                pThisCC->ImageEvt.offName - 4 - pThisCC->ImageEvt.offNameLastComponent,
                                &pThisCC->ImageEvt.szName[pThisCC->ImageEvt.offNameLastComponent],
                                pThisCC->ImageEvt.uAddr0, pThisCC->ImageEvt.cb0));
                        if (pThisCC->ImageEvt.offName > 4)
                            efiVBoxDbgScript("unload '%.*s.efi'\n",
                                             pThisCC->ImageEvt.offName - 4 - pThisCC->ImageEvt.offNameLastComponent,
                                             &pThisCC->ImageEvt.szName[pThisCC->ImageEvt.offNameLastComponent]);
                        break;
                    }
                case EFI_IMAGE_EVT_CMD_START_RELOC32:
                case EFI_IMAGE_EVT_CMD_START_RELOC64:
                {
                    LogRel(("EFI: relocate module to %#llx from %#llx\n",
                            pThisCC->ImageEvt.uAddr0, pThisCC->ImageEvt.uAddr1));
                    break;
                }
                }
            }
            return VINF_SUCCESS;
# else
            return VINF_IOM_R3_IOPORT_WRITE;
# endif
        }

        case EFI_IMAGE_EVT_CMD_ADDR0:
            AssertBreak(EFI_IMAGE_EVT_GET_PAYLOAD(u32) <= UINT16_MAX);
            pThisCC->ImageEvt.uAddr0 <<= 16;
            pThisCC->ImageEvt.uAddr0 |= EFI_IMAGE_EVT_GET_PAYLOAD_U16(u32);
            return VINF_SUCCESS;

        case EFI_IMAGE_EVT_CMD_ADDR1:
            AssertBreak(EFI_IMAGE_EVT_GET_PAYLOAD(u32) <= UINT16_MAX);
            pThisCC->ImageEvt.uAddr1 <<= 16;
            pThisCC->ImageEvt.uAddr1 |= EFI_IMAGE_EVT_GET_PAYLOAD_U16(u32);
            return VINF_SUCCESS;

        case EFI_IMAGE_EVT_CMD_SIZE0:
            AssertBreak(EFI_IMAGE_EVT_GET_PAYLOAD(u32) <= UINT16_MAX);
            pThisCC->ImageEvt.cb0 <<= 16;
            pThisCC->ImageEvt.cb0  |= EFI_IMAGE_EVT_GET_PAYLOAD_U16(u32);
            return VINF_SUCCESS;

        case EFI_IMAGE_EVT_CMD_NAME:
            AssertBreak(EFI_IMAGE_EVT_GET_PAYLOAD(u32) <= 0x7f);
            if (pThisCC->ImageEvt.offName < sizeof(pThisCC->ImageEvt.szName) - 1)
            {
                char ch = EFI_IMAGE_EVT_GET_PAYLOAD_U8(u32);
                if (ch == '\\')
                    ch = '/';
                pThisCC->ImageEvt.szName[pThisCC->ImageEvt.offName++] = ch;
                if (ch == '/' || ch == ':')
                    pThisCC->ImageEvt.offNameLastComponent = pThisCC->ImageEvt.offName;
            }
            else
                Log(("EFI: Image name overflow\n"));
            return VINF_SUCCESS;
    }

    Log(("EFI: Unknown image event: %#x (cb=%d)\n", u32, cb));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 *
 * @note The @a offPort parameter is absolute!
 */
static DECLCALLBACK(VBOXSTRICTRC) efiR3IoPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pvUser);
    PDEVEFIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVEFIR3);
    Log4(("EFI in: %x %x\n", offPort, cb));

    switch (offPort)
    {
        case EFI_INFO_PORT:
            if (pThisCC->offInfo == -1 && cb == 4)
            {
                pThisCC->offInfo = 0;
                uint32_t cbInfo = *pu32 = efiInfoSize(pThisCC);
                if (cbInfo == UINT32_MAX)
                    return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "iInfoSelector=%#x (%d)\n",
                                             pThisCC->iInfoSelector, pThisCC->iInfoSelector);
            }
            else
            {
                if (cb != 1)
                    return VERR_IOM_IOPORT_UNUSED;
                *pu32 = efiInfoNextByte(pThisCC);
                pThisCC->offInfo++;
            }
            return VINF_SUCCESS;

        case EFI_PANIC_PORT:
# ifdef IN_RING3
           LogRel(("EFI panic port read!\n"));
           /* Insert special code here on panic reads */
           return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "EFI Panic: panic port read!\n");
# else
           /* Reschedule to R3 */
           return VINF_IOM_R3_IOPORT_READ;
# endif

        case EFI_PORT_VARIABLE_OP: /* Obsolete */
        case EFI_PORT_VARIABLE_PARAM:
        case EFI_PORT_DEBUG_POINT:
        case EFI_PORT_IMAGE_EVENT:
            *pu32 = UINT32_MAX;
            return VINF_SUCCESS;
    }

    return VERR_IOM_IOPORT_UNUSED;
}


/**
 * Translates a debug point value into a string for logging.
 *
 * @returns read-only string
 * @param   enmDbgPoint         Valid debug point value.
 */
static const char *efiDbgPointName(EFIDBGPOINT enmDbgPoint)
{
    switch (enmDbgPoint)
    {
        case EFIDBGPOINT_SEC_PREMEM:    return "SEC_PREMEM";
        case EFIDBGPOINT_SEC_POSTMEM:   return "SEC_POSTMEM";
        case EFIDBGPOINT_DXE_CORE:      return "DXE_CORE";
        case EFIDBGPOINT_SMM:           return "SMM";
        case EFIDBGPOINT_SMI_ENTER:     return "SMI_ENTER";
        case EFIDBGPOINT_SMI_EXIT:      return "SMI_EXIT";
        case EFIDBGPOINT_GRAPHICS:      return "GRAPHICS";
        case EFIDBGPOINT_DXE_AP:        return "DXE_AP";
        default:
            AssertFailed();
            return "Unknown";
    }
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 *
 * @note The @a offPort parameter is absolute!
 */
static DECLCALLBACK(VBOXSTRICTRC) efiR3IoPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(pvUser);
    PDEVEFIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVEFIR3);
    VBOXSTRICTRC rc   = VINF_SUCCESS;
    Log4(("efi: out %x %x %d\n", offPort, u32, cb));

    switch (offPort)
    {
        case EFI_INFO_PORT:
            Log2(("EFI_INFO_PORT: iInfoSelector=%#x\n", u32));
            pThisCC->iInfoSelector = u32;
            pThisCC->offInfo       = -1;
            break;

        case EFI_DEBUG_PORT:
        {
            /* The raw version. */
            switch (u32)
            {
                case '\r': Log3(("efi: <return>\n")); break;
                case '\n': Log3(("efi: <newline>\n")); break;
                case '\t': Log3(("efi: <tab>\n")); break;
                default:   Log3(("efi: %c (%02x)\n", u32, u32)); break;
            }
            /* The readable, buffered version. */
            if (u32 == '\n' || u32 == '\r')
            {
                Assert(pThisCC->iMsg < sizeof(pThisCC->szMsg));
                pThisCC->szMsg[pThisCC->iMsg] = '\0';
                if (pThisCC->iMsg)
                    LogRel2(("efi: %s\n", pThisCC->szMsg));
                pThisCC->iMsg = 0;
            }
            else
            {
                if (pThisCC->iMsg >= sizeof(pThisCC->szMsg) - 1)
                {
                    pThisCC->szMsg[pThisCC->iMsg] = '\0';
                    LogRel2(("efi: %s\n", pThisCC->szMsg));
                    pThisCC->iMsg = 0;
                }
                pThisCC->szMsg[pThisCC->iMsg] = (char)u32;
                pThisCC->szMsg[++pThisCC->iMsg] = '\0';
            }
            break;
        }

        case EFI_PANIC_PORT:
        {
            switch (u32)
            {
                case EFI_PANIC_CMD_BAD_ORG: /* Legacy */
                case EFI_PANIC_CMD_THUNK_TRAP:
#ifdef IN_RING3
                    LogRel(("EFI: Panic! Unexpected trap!!\n"));
# ifdef VBOX_STRICT
                    return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "EFI Panic: Unexpected trap during early bootstrap!\n");
# else
                    AssertReleaseMsgFailed(("Unexpected trap during early EFI bootstrap!!\n"));
# endif
                    break;
#else
                    return VINF_IOM_R3_IOPORT_WRITE;
#endif

                case EFI_PANIC_CMD_START_MSG:
                    LogRel(("Receiving EFI panic...\n"));
                    pThisCC->iPanicMsg = 0;
                    pThisCC->szPanicMsg[0] = '\0';
                    break;

                case EFI_PANIC_CMD_END_MSG:
#ifdef IN_RING3
                    LogRel(("EFI: Panic! %s\n", pThisCC->szPanicMsg));
# ifdef VBOX_STRICT
                    return PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "EFI Panic: %s\n", pThisCC->szPanicMsg);
# else
                    return VERR_INTERNAL_ERROR;
# endif
#else
                    return VINF_IOM_R3_IOPORT_WRITE;
#endif


                default:
                    if (    u32 >= EFI_PANIC_CMD_MSG_FIRST
                        &&  u32 <= EFI_PANIC_CMD_MSG_LAST)
                    {
                        /* Add the message char to the buffer. */
                        uint32_t i = pThisCC->iPanicMsg;
                        if (i + 1 < sizeof(pThisCC->szPanicMsg))
                        {
                            char ch = EFI_PANIC_CMD_MSG_GET_CHAR(u32);
                            if (    ch == '\n'
                                &&  i > 0
                                &&  pThisCC->szPanicMsg[i - 1] == '\r')
                                i--;
                            pThisCC->szPanicMsg[i] = ch;
                            pThisCC->szPanicMsg[i + 1] = '\0';
                            pThisCC->iPanicMsg = i + 1;
                        }
                    }
                    else
                        Log(("EFI: Unknown panic command: %#x (cb=%d)\n", u32, cb));
                    break;
            }
            break;
        }

        case EFI_PORT_EVENT:
        {
            rc = efiR3PortEventWrite(pThisCC, u32, cb);
            break;
        }

        case EFI_PORT_VARIABLE_OP:
        case EFI_PORT_VARIABLE_PARAM:
        {
            /* Ignore access to the obsolete variable handling port. */
            Log(("EFI: Write to obsolete variable handling port %RTiop: %#x (cb=%d)\n", offPort, u32, cb));
            break;
        }

        case EFI_PORT_DEBUG_POINT:
# ifdef IN_RING3
            if (u32 > EFIDBGPOINT_INVALID && u32 < EFIDBGPOINT_END)
            {
                /* For now, just log it. */
                LogRelMax(1024, ("EFI: debug point %s\n", efiDbgPointName((EFIDBGPOINT)u32)));
                rc = VINF_SUCCESS;
            }
            else
                rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "Invalid debug point %#x\n", u32);
            break;
# else
            return VINF_IOM_R3_IOPORT_WRITE;
# endif

        case EFI_PORT_IMAGE_EVENT:
            rc = efiPortImageEventWrite(pThisCC, u32, cb);
            break;

        default:
            Log(("EFI: Write to reserved port %RTiop: %#x (cb=%d)\n", offPort, u32, cb));
            break;
    }
    return rc;
}

#endif /* IN_RING3 */

/**
 * @callback_method_impl{FNIOMMMIONEWWRITE, Flash memory write}
 */
static DECLCALLBACK(VBOXSTRICTRC) efiR3NvMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PDEVEFI pThis = PDMDEVINS_2_DATA(pDevIns, PDEVEFI);
    RT_NOREF(pvUser);

    return flashWrite(&pThis->Flash, off, pv, cb);
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD, Flash memory read}
 */
static DECLCALLBACK(VBOXSTRICTRC) efiR3NvMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PDEVEFI pThis = PDMDEVINS_2_DATA(pDevIns, PDEVEFI);
    RT_NOREF(pvUser);

    return flashRead(&pThis->Flash, off, pv, cb);
}

#ifdef IN_RING3

static DECLCALLBACK(int) efiSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVEFI pThis = PDMDEVINS_2_DATA(pDevIns, PDEVEFI);
    LogFlow(("efiSaveExec:\n"));

    return flashR3SaveExec(&pThis->Flash, pDevIns, pSSM);
}

static DECLCALLBACK(int) efiLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDEVEFI         pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVEFI);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    LogFlow(("efiLoadExec: uVersion=%d uPass=%d\n", uVersion, uPass));

    /*
     * Validate input.
     */
    if (uPass != SSM_PASS_FINAL)
        return VERR_SSM_UNEXPECTED_PASS;
    if (   uVersion != EFI_SSM_VERSION
        && uVersion != EFI_SSM_VERSION_PRE_PROPER_NVRAM
        && uVersion != EFI_SSM_VERSION_4_2
        )
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    int rc;
    if (uVersion > EFI_SSM_VERSION_PRE_PROPER_NVRAM)
        rc = flashR3LoadExec(&pThis->Flash, pDevIns, pSSM);
    else
    {
        /*
         * Ignore the old NVRAM state.
         */
        rc = pHlp->pfnSSMSkipToEndOfUnit(pSSM);
    }

    return rc;
}


/**
 * @copydoc(PDMIBASE::pfnQueryInterface)
 */
static DECLCALLBACK(void *) devEfiQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    LogFlowFunc(("ENTER: pIBase=%p pszIID=%p\n", pInterface, pszIID));
    PDEVEFIR3 pThisCC = RT_FROM_MEMBER(pInterface, DEVEFIR3, Lun0.IBase);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->Lun0.IBase);
    return NULL;
}


/**
 * Write to CMOS memory.
 * This is used by the init complete code.
 */
static void cmosWrite(PPDMDEVINS pDevIns, unsigned off, uint32_t u32Val)
{
    Assert(off < 128);
    Assert(u32Val < 256);

    int rc = PDMDevHlpCMOSWrite(pDevIns, off, u32Val);
    AssertRC(rc);
}

/**
 * Init complete notification.
 *
 * @returns VBOX status code.
 * @param   pDevIns     The device instance.
 */
static DECLCALLBACK(int) efiInitComplete(PPDMDEVINS pDevIns)
{
    PDEVEFIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVEFIR3);

    uint64_t const  cbRamSize  = PDMDevHlpMMPhysGetRamSize(pDevIns);
    uint32_t const  cbBelow4GB = PDMDevHlpMMPhysGetRamSizeBelow4GB(pDevIns);
    uint64_t const  cbAbove4GB = PDMDevHlpMMPhysGetRamSizeAbove4GB(pDevIns);
    NOREF(cbAbove4GB);

    /*
     * Memory sizes.
     */
    uint32_t u32Low = 0;
    uint32_t u32Chunks = 0;
    if (cbRamSize > 16 * _1M)
    {
        u32Low = RT_MIN(cbBelow4GB, UINT32_C(0xfe000000));
        u32Chunks = (u32Low - 16U * _1M) / _64K;
    }
    cmosWrite(pDevIns, 0x34, RT_BYTE1(u32Chunks));
    cmosWrite(pDevIns, 0x35, RT_BYTE2(u32Chunks));

    if (u32Low < cbRamSize)
    {
        uint64_t u64 = cbRamSize - u32Low;
        u32Chunks = (uint32_t)(u64 / _64K);
        cmosWrite(pDevIns, 0x5b, RT_BYTE1(u32Chunks));
        cmosWrite(pDevIns, 0x5c, RT_BYTE2(u32Chunks));
        cmosWrite(pDevIns, 0x5d, RT_BYTE3(u32Chunks));
        cmosWrite(pDevIns, 0x5e, RT_BYTE4(u32Chunks));
    }

    /*
     * Number of CPUs.
     */
    cmosWrite(pDevIns, 0x60, pThisCC->cCpus & 0xff);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnMemSetup}
 */
static DECLCALLBACK(void) efiMemSetup(PPDMDEVINS pDevIns, PDMDEVMEMSETUPCTX enmCtx)
{
    RT_NOREF(enmCtx);
    PDEVEFIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVEFIR3);

    /*
     * Re-shadow the Firmware Volume and make it RAM/RAM.
     */
    uint32_t    cPages = RT_ALIGN_64(pThisCC->cbEfiRom, GUEST_PAGE_SIZE) >> GUEST_PAGE_SHIFT;
    RTGCPHYS    GCPhys = pThisCC->GCLoadAddress;
    while (cPages > 0)
    {
        uint8_t abPage[GUEST_PAGE_SIZE];

        /* Read the (original) ROM page and write it back to the RAM page. */
        int rc = PDMDevHlpROMProtectShadow(pDevIns, GCPhys, GUEST_PAGE_SIZE, PGMROMPROT_READ_ROM_WRITE_RAM);
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
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) efiReset(PPDMDEVINS pDevIns)
{
    PDEVEFI   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVEFI);
    PDEVEFIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVEFIR3);
    LogFlow(("efiReset\n"));

    pThisCC->iInfoSelector = 0;
    pThisCC->offInfo       = -1;

    pThisCC->iMsg = 0;
    pThisCC->szMsg[0] = '\0';
    pThisCC->iPanicMsg = 0;
    pThisCC->szPanicMsg[0] = '\0';

    flashR3Reset(&pThis->Flash);

#ifdef DEVEFI_WITH_VBOXDBG_SCRIPT
    /*
     * Zap the debugger script
     */
    RTFileDelete("./DevEFI.VBoxDbg");
#endif
}


/**
 * @interface_method_impl{PDMDEVREG,pfnPowerOff}
 */
static DECLCALLBACK(void) efiPowerOff(PPDMDEVINS pDevIns)
{
    PDEVEFI   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVEFI);
    PDEVEFIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVEFIR3);

    if (pThisCC->Lun0.pDrvVfs)
    {
        int rc = flashR3SaveToVfs(&pThis->Flash, pDevIns, pThisCC->Lun0.pDrvVfs,
                                  pDevIns->pReg->szName, "nvram");
        if (RT_FAILURE(rc))
            LogRel(("EFI: Failed to save flash file to NVRAM store: %Rrc\n", rc));
    }
    else if (pThisCC->pszNvramFile)
    {
        int rc = flashR3SaveToFile(&pThis->Flash, pDevIns, pThisCC->pszNvramFile);
        if (RT_FAILURE(rc))
            LogRel(("EFI: Failed to save flash file to '%s': %Rrc\n", pThisCC->pszNvramFile, rc));
    }
}



/**
 * Destruct a device instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(int) efiDestruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PDEVEFI   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVEFI);
    PDEVEFIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVEFIR3);

    flashR3Destruct(&pThis->Flash, pDevIns);

    if (pThisCC->pszNvramFile)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisCC->pszNvramFile);
        pThisCC->pszNvramFile = NULL;
    }

    if (pThisCC->pu8EfiRomFree)
    {
        RTFileReadAllFree(pThisCC->pu8EfiRomFree, (size_t)pThisCC->cbEfiRom + pThisCC->offEfiRom);
        pThisCC->pu8EfiRomFree = NULL;
    }

    /*
     * Free MM heap pointers (waste of time, but whatever).
     */
    if (pThisCC->pszEfiRomFile)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisCC->pszEfiRomFile);
        pThisCC->pszEfiRomFile = NULL;
    }

    if (pThisCC->pu8EfiThunk)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisCC->pu8EfiThunk);
        pThisCC->pu8EfiThunk = NULL;
    }

    if (pThisCC->pbDeviceProps)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisCC->pbDeviceProps);
        pThisCC->pbDeviceProps = NULL;
        pThisCC->cbDeviceProps = 0;
    }

    return VINF_SUCCESS;
}


#if 0 /* unused */
/**
 * Helper that searches for a FFS file of a given type.
 *
 * @returns Pointer to the FFS file header if found, NULL if not.
 *
 * @param   pFfsFile    Pointer to the FFS file header to start searching at.
 * @param   pbEnd       The end of the firmware volume.
 * @param   FileType    The file type to look for.
 * @param   pcbFfsFile  Where to store the FFS file size (includes header).
 */
DECLINLINE(EFI_FFS_FILE_HEADER const *)
efiFwVolFindFileByType(EFI_FFS_FILE_HEADER const *pFfsFile, uint8_t const *pbEnd, EFI_FV_FILETYPE FileType, uint32_t *pcbFile)
{
# define FFS_SIZE(hdr)   RT_MAKE_U32_FROM_U8((hdr)->Size[0], (hdr)->Size[1], (hdr)->Size[2], 0)
    while ((uintptr_t)pFfsFile < (uintptr_t)pbEnd)
    {
        if (pFfsFile->Type == FileType)
        {
            *pcbFile = FFS_SIZE(pFfsFile);
            LogFunc(("Found %RTuuid of type:%d\n", &pFfsFile->Name, FileType));
            return pFfsFile;
        }
        pFfsFile = (EFI_FFS_FILE_HEADER *)((uintptr_t)pFfsFile + RT_ALIGN(FFS_SIZE(pFfsFile), 8));
    }
# undef FFS_SIZE
    return NULL;
}
#endif /* unused */


/**
 * Parse EFI ROM headers and find entry points.
 *
 * @returns VBox status code.
 * @param   pDevIns  The device instance.
 * @param   pThis    The shared device state.
 * @param   pThisCC  The device state for the current context.
 */
static int efiParseFirmware(PPDMDEVINS pDevIns, PDEVEFI pThis, PDEVEFIR3 pThisCC)
{
    EFI_FIRMWARE_VOLUME_HEADER const *pFwVolHdr = (EFI_FIRMWARE_VOLUME_HEADER const *)pThisCC->pu8EfiRom;

    /*
     * Validate firmware volume header.
     */
    AssertLogRelMsgReturn(pFwVolHdr->u32Signature == EFI_FIRMWARE_VOLUME_HEADER_SIGNATURE,
                          ("%#x, expected %#x\n", pFwVolHdr->u32Signature, EFI_FIRMWARE_VOLUME_HEADER_SIGNATURE),
                          VERR_INVALID_MAGIC);
    AssertLogRelMsgReturn(pFwVolHdr->bRevision == EFI_FIRMWARE_VOLUME_HEADER_REVISION,
                          ("%#x, expected %#x\n", pFwVolHdr->bRevision, EFI_FIRMWARE_VOLUME_HEADER_REVISION),
                          VERR_VERSION_MISMATCH);
    /** @todo check checksum, see PE spec vol. 3 */
    AssertLogRelMsgReturn(pFwVolHdr->cbFv <= pThisCC->cbEfiRom,
                          ("%#llx, expected %#llx\n", pFwVolHdr->cbFv, pThisCC->cbEfiRom),
                          VERR_INVALID_PARAMETER);
    PCEFI_FW_BLOCK_MAP pBlockMap = (PCEFI_FW_BLOCK_MAP)(pFwVolHdr + 1);
    AssertLogRelMsgReturn(      pBlockMap->cbBlock > 0
                          &&    pBlockMap->cBlocks > 0,
                          ("%#x, %x\n", pBlockMap->cbBlock, pBlockMap->cBlocks),
                          VERR_INVALID_PARAMETER);

    AssertLogRelMsgReturn(!(pThisCC->cbEfiRom & GUEST_PAGE_OFFSET_MASK), ("%RX64\n", pThisCC->cbEfiRom), VERR_INVALID_PARAMETER);

    LogRel(("Found EFI FW Volume, %u bytes (%u %u-byte blocks)\n", pFwVolHdr->cbFv, pBlockMap->cBlocks, pBlockMap->cbBlock));

    /** @todo Make this more dynamic, this assumes that the NV storage area comes first (always the case for our builds). */
    AssertLogRelMsgReturn(!memcmp(&pFwVolHdr->GuidFilesystem, &g_UuidNvDataFv, sizeof(g_UuidNvDataFv)),
                          ("Expected EFI_SYSTEM_NV_DATA_FV_GUID as an identifier"),
                          VERR_INVALID_MAGIC);

    /* Found NVRAM storage, configure flash device. */
    pThisCC->offEfiRom   = pFwVolHdr->cbFv;
    pThisCC->cbNvram     = pFwVolHdr->cbFv;
    pThisCC->GCPhysNvram = UINT32_C(0xfffff000) - pThisCC->cbEfiRom + GUEST_PAGE_SIZE;
    pThisCC->cbEfiRom   -= pThisCC->cbNvram;

    int rc = flashR3Init(&pThis->Flash, pThisCC->pDevIns, 0xA289 /*Intel*/, pThisCC->cbNvram, pBlockMap->cbBlock);
    if (RT_FAILURE(rc))
        return rc;

    if (pThisCC->Lun0.pDrvVfs)
    {
        rc = flashR3LoadFromVfs(&pThis->Flash, pDevIns, pThisCC->Lun0.pDrvVfs,
                                pDevIns->pReg->szName, "nvram");
        if (rc == VERR_NOT_FOUND)
        {
            /* Initialize the NVRAM content from the loaded ROM file as the NVRAM wasn't initialized yet. */
            rc = flashR3LoadFromBuf(&pThis->Flash, pThisCC->pu8EfiRom, pThisCC->cbNvram);
            if (RT_FAILURE(rc))
                return rc;
        }
        else if (RT_FAILURE(rc))
            return rc;
    }
    else
    {
        /* If the file does not exist we initialize the NVRAM from the loaded ROM file. */
        if (!pThisCC->pszNvramFile || !RTPathExists(pThisCC->pszNvramFile))
            rc = flashR3LoadFromBuf(&pThis->Flash, pThisCC->pu8EfiRom, pThisCC->cbNvram);
        else
            rc = flashR3LoadFromFile(&pThis->Flash, pDevIns, pThisCC->pszNvramFile);
        if (RT_FAILURE(rc))
            return rc;
    }

    pThisCC->GCLoadAddress = pThisCC->GCPhysNvram + pThisCC->cbNvram;

    return VINF_SUCCESS;
}

/**
 * Load EFI ROM file into the memory.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared Efi state.
 * @param   pThisCC     The device state for the current context.
 * @param   pCfg        Configuration node handle for the device.
 */
static int efiLoadRom(PPDMDEVINS pDevIns, PDEVEFI pThis, PDEVEFIR3 pThisCC, PCFGMNODE pCfg)
{
    RT_NOREF(pCfg);

    /*
     * Read the entire firmware volume into memory.
     */
    int     rc;
#ifdef VBOX_WITH_EFI_IN_DD2
    if (RTStrCmp(pThisCC->pszEfiRomFile, g_szEfiBuiltin32) == 0)
    {
        pThisCC->pu8EfiRomFree = NULL;
        pThisCC->pu8EfiRom = g_abEfiFirmware32;
        pThisCC->cbEfiRom  = g_cbEfiFirmware32;
    }
    else if (RTStrCmp(pThisCC->pszEfiRomFile, g_szEfiBuiltin64) == 0)
    {
        pThisCC->pu8EfiRomFree = NULL;
        pThisCC->pu8EfiRom = g_abEfiFirmware64;
        pThisCC->cbEfiRom  = g_cbEfiFirmware64;
    }
    else
#endif
    {
        void   *pvFile;
        size_t  cbFile;
        rc = RTFileReadAllEx(pThisCC->pszEfiRomFile,
                             0 /*off*/,
                             RTFOFF_MAX /*cbMax*/,
                             RTFILE_RDALL_O_DENY_WRITE,
                             &pvFile,
                             &cbFile);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Loading the EFI firmware volume '%s' failed with rc=%Rrc"),
                                       pThisCC->pszEfiRomFile, rc);
        pThisCC->pu8EfiRomFree = (uint8_t *)pvFile;
        pThisCC->pu8EfiRom = (uint8_t *)pvFile;
        pThisCC->cbEfiRom  = cbFile;
    }

    /*
     * Validate firmware volume and figure out the load address as well as the SEC entry point.
     */
    rc = efiParseFirmware(pDevIns, pThis, pThisCC);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Parsing the EFI firmware volume '%s' failed with rc=%Rrc"),
                                   pThisCC->pszEfiRomFile, rc);

    /*
     * Map the firmware volume into memory as shadowed ROM.
     *
     * This is a little complicated due to saved state legacy.  We used to have a
     * 2MB image w/o any flash portion, divided into four 512KB mappings.
     *
     * We've now increased the size of the firmware to 4MB, but for saved state
     * compatibility reasons need to use the same mappings and names (!!) for the
     * top 2MB.
     */
    /** @todo fix PGMR3PhysRomRegister so it doesn't mess up in SUPLib when mapping a big ROM image. */
#if 1
    static const char * const s_apszNames[16] =
    {
        "EFI Firmware Volume",           "EFI Firmware Volume (Part 2)",  "EFI Firmware Volume (Part 3)",  "EFI Firmware Volume (Part 4)",
        "EFI Firmware Volume (Part 5)",  "EFI Firmware Volume (Part 6)",  "EFI Firmware Volume (Part 7)",  "EFI Firmware Volume (Part 8)",
        "EFI Firmware Volume (Part 9)",  "EFI Firmware Volume (Part 10)", "EFI Firmware Volume (Part 11)", "EFI Firmware Volume (Part 12)",
        "EFI Firmware Volume (Part 13)", "EFI Firmware Volume (Part 14)", "EFI Firmware Volume (Part 15)", "EFI Firmware Volume (Part 16)",
    };
    AssertLogRelMsgReturn(pThisCC->cbEfiRom < RT_ELEMENTS(s_apszNames) * _512K,
                          ("EFI firmware image too big: %#RX64, max %#zx\n",
                           pThisCC->cbEfiRom, RT_ELEMENTS(s_apszNames) * _512K),
                          VERR_IMAGE_TOO_BIG);

    uint32_t const  cbChunk = pThisCC->cbNvram + pThisCC->cbEfiRom >= _2M ? _512K
                            : (uint32_t)RT_ALIGN_64((pThisCC->cbNvram + pThisCC->cbEfiRom) / 4, GUEST_PAGE_SIZE);
    uint32_t        cbLeft  = pThisCC->cbEfiRom;           /* ASSUMES NVRAM comes first! */
    uint32_t        off     = pThisCC->offEfiRom + cbLeft; /* ASSUMES NVRAM comes first! */
    RTGCPHYS64      GCPhys  = pThisCC->GCLoadAddress + cbLeft;
    AssertLogRelMsg(GCPhys == _4G, ("%RGp\n", GCPhys));

    /* Compatibility mappings at the top (note that this isn't entirely the same
       algorithm, but it will produce the same results for a power of two sized image): */
    unsigned i = 4;
    while (i-- > 0)
    {
        uint32_t const cb = RT_MIN(cbLeft, cbChunk);
        cbLeft -= cb;
        GCPhys -= cb;
        off    -= cb;
        rc = PDMDevHlpROMRegister(pDevIns, GCPhys, cb, pThisCC->pu8EfiRom + off, cb,
                                  PGMPHYS_ROM_FLAGS_SHADOWED | PGMPHYS_ROM_FLAGS_PERMANENT_BINARY, s_apszNames[i]);
        AssertRCReturn(rc, rc);
    }

    /* The rest (if any) is mapped in descending order of address and increasing name order: */
    if (cbLeft > 0)
    {
        Assert(cbChunk == _512K);
        for (i = 4; cbLeft > 0; i++)
        {
            uint32_t const cb = RT_MIN(cbLeft, cbChunk);
            cbLeft -= cb;
            GCPhys -= cb;
            off    -= cb;
            /** @todo Add flag to prevent saved state loading from bitching about these regions. */
            rc = PDMDevHlpROMRegister(pDevIns, GCPhys, cb, pThisCC->pu8EfiRom + off, cb,
                                      PGMPHYS_ROM_FLAGS_SHADOWED | PGMPHYS_ROM_FLAGS_PERMANENT_BINARY
                                      | PGMPHYS_ROM_FLAGS_MAYBE_MISSING_FROM_STATE, s_apszNames[i]);
            AssertRCReturn(rc, rc);
        }
        Assert(i <= RT_ELEMENTS(s_apszNames));
    }

    /* Not sure what the purpose of this one is... */
    rc = PDMDevHlpROMProtectShadow(pDevIns, pThisCC->GCLoadAddress, (uint32_t)cbChunk, PGMROMPROT_READ_RAM_WRITE_IGNORE);
    AssertRCReturn(rc, rc);

#else
    RTGCPHYS cbQuart = RT_ALIGN_64(pThisCC->cbEfiRom / 4, GUEST_PAGE_SIZE);
    rc = PDMDevHlpROMRegister(pDevIns,
                              pThisCC->GCLoadAddress,
                              cbQuart,
                              pThisCC->pu8EfiRom + pThisCC->uEfiRomOfs,
                              cbQuart,
                              PGMPHYS_ROM_FLAGS_SHADOWED | PGMPHYS_ROM_FLAGS_PERMANENT_BINARY,
                              "EFI Firmware Volume");
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpROMProtectShadow(pDevIns, pThisCC->GCLoadAddress, (uint32_t)cbQuart, PGMROMPROT_READ_RAM_WRITE_IGNORE);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpROMRegister(pDevIns,
                              pThisCC->GCLoadAddress + cbQuart,
                              cbQuart,
                              pThisCC->pu8EfiRom + pThisCC->uEfiRomOfs + cbQuart,
                              cbQuart,
                              PGMPHYS_ROM_FLAGS_SHADOWED | PGMPHYS_ROM_FLAGS_PERMANENT_BINARY,
                              "EFI Firmware Volume (Part 2)");
    if (RT_FAILURE(rc))
        return rc;
    rc = PDMDevHlpROMRegister(pDevIns,
                              pThisCC->GCLoadAddress + cbQuart * 2,
                              cbQuart,
                              pThisCC->pu8EfiRom + pThisCC->uEfiRomOfs + cbQuart * 2,
                              cbQuart,
                              PGMPHYS_ROM_FLAGS_SHADOWED | PGMPHYS_ROM_FLAGS_PERMANENT_BINARY,
                              "EFI Firmware Volume (Part 3)");
    if (RT_FAILURE(rc))
        return rc;
    rc = PDMDevHlpROMRegister(pDevIns,
                              pThisCC->GCLoadAddress + cbQuart * 3,
                              pThisCC->cbEfiRom - cbQuart * 3,
                              pThisCC->pu8EfiRom + pThisCC->uEfiRomOfs + cbQuart * 3,
                              pThisCC->cbEfiRom - cbQuart * 3,
                              PGMPHYS_ROM_FLAGS_SHADOWED | PGMPHYS_ROM_FLAGS_PERMANENT_BINARY,
                              "EFI Firmware Volume (Part 4)");
    if (RT_FAILURE(rc))
        return rc;
#endif

    /*
     * Register MMIO region for flash device.
     */
    rc = PDMDevHlpMmioCreateEx(pDevIns, pThisCC->cbNvram, IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU,
                               NULL /*pPciDev*/,  UINT32_MAX, efiR3NvMmioWrite, efiR3NvMmioRead, NULL, NULL /*pvUser*/,
                               "Flash Memory", &pThis->hMmioFlash);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpMmioMap(pDevIns, pThis->hMmioFlash, pThisCC->GCPhysNvram);
    AssertRCReturn(rc, rc);

    LogRel(("EFI: Registered %uKB flash at %RGp\n", pThisCC->cbNvram / _1K, pThisCC->GCPhysNvram));
    return VINF_SUCCESS;
}

static uint8_t efiGetHalfByte(char ch)
{
    uint8_t val;

    if (ch >= '0' && ch <= '9')
        val = ch - '0';
    else if (ch >= 'A' && ch <= 'F')
        val = ch - 'A' + 10;
    else if(ch >= 'a' && ch <= 'f')
        val = ch - 'a' + 10;
    else
        val = 0xff;

    return val;
}


/**
 * Converts a hex string into a binary data blob located at
 * pThisCC->pbDeviceProps, size returned as pThisCC->cbDeviceProps.
 *
 * @returns VERR_NO_MEMORY or VINF_SUCCESS.
 * @param   pThisCC         The device state for the current context.
 * @param   pszDeviceProps  The device property hex string to decode.
 */
static int efiParseDeviceString(PDEVEFIR3 pThisCC, const char *pszDeviceProps)
{
    uint32_t const cbOut = (uint32_t)RTStrNLen(pszDeviceProps, RTSTR_MAX) / 2 + 1;
    pThisCC->pbDeviceProps = (uint8_t *)PDMDevHlpMMHeapAlloc(pThisCC->pDevIns, cbOut);
    if (!pThisCC->pbDeviceProps)
        return VERR_NO_MEMORY;

    uint32_t    iHex    = 0;
    bool        fUpper  = true;
    uint8_t     u8Value = 0;                    /* (shut up gcc) */
    for (uint32_t iStr = 0; pszDeviceProps[iStr]; iStr++)
    {
        uint8_t u8Hb = efiGetHalfByte(pszDeviceProps[iStr]);
        if (u8Hb > 0xf)
            continue;

        if (fUpper)
            u8Value = u8Hb << 4;
        else
            pThisCC->pbDeviceProps[iHex++] = u8Hb | u8Value;

        Assert(iHex < cbOut);
        fUpper = !fUpper;
    }

    Assert(iHex == 0 || fUpper);
    pThisCC->cbDeviceProps = iHex;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int)  efiConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVEFI         pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVEFI);
    PDEVEFIR3       pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVEFIR3);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;

    RT_NOREF(iInstance);
    Assert(iInstance == 0);

    /*
     * Initalize the basic variables so that the destructor always works.
     */
    pThisCC->pDevIns = pDevIns;
    pThisCC->Lun0.IBase.pfnQueryInterface = devEfiQueryInterface;

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns,
                                  "EfiRom|"
                                  "NumCPUs|"
                                  "McfgBase|"
                                  "McfgLength|"
                                  "UUID|"
                                  "UuidLe|"
                                  "IOAPIC|"
                                  "APIC|"
                                  "DmiBIOSFirmwareMajor|"
                                  "DmiBIOSFirmwareMinor|"
                                  "DmiBIOSReleaseDate|"
                                  "DmiBIOSReleaseMajor|"
                                  "DmiBIOSReleaseMinor|"
                                  "DmiBIOSVendor|"
                                  "DmiBIOSVersion|"
                                  "DmiSystemFamily|"
                                  "DmiSystemProduct|"
                                  "DmiSystemSerial|"
                                  "DmiSystemSKU|"
                                  "DmiSystemUuid|"
                                  "DmiSystemVendor|"
                                  "DmiSystemVersion|"
                                  "DmiBoardAssetTag|"
                                  "DmiBoardBoardType|"
                                  "DmiBoardLocInChass|"
                                  "DmiBoardProduct|"
                                  "DmiBoardSerial|"
                                  "DmiBoardVendor|"
                                  "DmiBoardVersion|"
                                  "DmiChassisAssetTag|"
                                  "DmiChassisSerial|"
                                  "DmiChassisType|"
                                  "DmiChassisVendor|"
                                  "DmiChassisVersion|"
                                  "DmiProcManufacturer|"
                                  "DmiProcVersion|"
                                  "DmiOEMVBoxVer|"
                                  "DmiOEMVBoxRev|"
                                  "DmiUseHostInfo|"
                                  "DmiExposeMemoryTable|"
                                  "DmiExposeProcInf|"
                                  "64BitEntry|"
                                  "BootArgs|"
                                  "DeviceProps|"
                                  "GopMode|"                   // legacy
                                  "GraphicsMode|"
                                  "UgaHorizontalResolution|"   // legacy
                                  "UgaVerticalResolution|"     // legacy
                                  "GraphicsResolution|"
                                  "NvramFile|"
                                  "TpmPpiBase", "");

    /* CPU count (optional). */
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "NumCPUs", &pThisCC->cCpus, 1);
    AssertLogRelRCReturn(rc, rc);

    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "McfgBase", &pThisCC->u64McfgBase, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"\" as integer failed"));
    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "McfgLength", &pThisCC->cbMcfgLength, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"McfgLength\" as integer failed"));

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "IOAPIC", &pThisCC->u8IOAPIC, 1);
    if (RT_FAILURE (rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to read \"IOAPIC\""));

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "APIC", &pThisCC->u8APIC, 1);
    if (RT_FAILURE (rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to read \"APIC\""));

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
         * to carry this bug along... (see also DevPcBios.cpp when changing this)
         *
         * Convert the UUID to network byte order. Not entirely straightforward as
         * parts are MSB already...
         */
        uuid.Gen.u32TimeLow = RT_H2BE_U32(uuid.Gen.u32TimeLow);
        uuid.Gen.u16TimeMid = RT_H2BE_U16(uuid.Gen.u16TimeMid);
        uuid.Gen.u16TimeHiAndVersion = RT_H2BE_U16(uuid.Gen.u16TimeHiAndVersion);
    }
    memcpy(&pThisCC->aUuid, &uuid, sizeof pThisCC->aUuid);

    /*
     * Get the system EFI ROM file name.
     */
#ifdef VBOX_WITH_EFI_IN_DD2
    rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "EfiRom", &pThisCC->pszEfiRomFile, g_szEfiBuiltin32);
    if (RT_FAILURE(rc))
#else
    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "EfiRom", &pThisCC->pszEfiRomFile);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
    {
        pThisCC->pszEfiRomFile = (char *)PDMDevHlpMMHeapAlloc(pDevIns, RTPATH_MAX);
        AssertReturn(pThisCC->pszEfiRomFile, VERR_NO_MEMORY);
        rc = RTPathAppPrivateArchTop(pThisCC->pszEfiRomFile, RTPATH_MAX);
        AssertRCReturn(rc, rc);
        rc = RTPathAppend(pThisCC->pszEfiRomFile, RTPATH_MAX, "VBoxEFI32.fd");
        AssertRCReturn(rc, rc);
    }
    else if (RT_FAILURE(rc))
#endif
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"EfiRom\" as a string failed"));

    /*
     * Saved State handling.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, EFI_SSM_VERSION, sizeof(*pThisCC), efiSaveExec, efiLoadExec);
    AssertRCReturn(rc, rc);

    /*
     * NVRAM storage.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThisCC->Lun0.IBase, &pThisCC->Lun0.pDrvBase, "NvramStorage");
    if (RT_SUCCESS(rc))
    {
        pThisCC->Lun0.pDrvVfs = PDMIBASE_QUERY_INTERFACE(pThisCC->Lun0.pDrvBase, PDMIVFSCONNECTOR);
        if (!pThisCC->Lun0.pDrvVfs)
            return PDMDevHlpVMSetError(pDevIns, VERR_PDM_MISSING_INTERFACE_BELOW, RT_SRC_POS, N_("NVRAM storage driver is missing VFS interface below"));
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        rc = VINF_SUCCESS; /* Missing driver is no error condition. */
    else
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, N_("Can't attach Nvram Storage driver"));

    /*
     * Get boot args.
     */
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "BootArgs", pThisCC->szBootArgs, sizeof(pThisCC->szBootArgs), "");
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"BootArgs\" as a string failed"));

    //strcpy(pThisCC->szBootArgs, "-v keepsyms=1 io=0xf debug=0x2a");
    //strcpy(pThisCC->szBootArgs, "-v keepsyms=1 debug=0x2a");
    LogRel(("EFI: boot args = %s\n", pThisCC->szBootArgs));

    /*
     * Get device props.
     */
    char *pszDeviceProps;
    rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "DeviceProps", &pszDeviceProps, NULL);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"DeviceProps\" as a string failed"));
    if (pszDeviceProps)
    {
        LogRel(("EFI: device props = %s\n", pszDeviceProps));
        rc = efiParseDeviceString(pThisCC, pszDeviceProps);
        PDMDevHlpMMHeapFree(pDevIns, pszDeviceProps);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Configuration error: Cannot parse device properties"));
    }
    else
    {
        pThisCC->pbDeviceProps = NULL;
        pThisCC->cbDeviceProps = 0;
    }

    /*
     * CPU frequencies.
     */
    pThisCC->u64TscFrequency = PDMDevHlpTMCpuTicksPerSecond(pDevIns);
    pThisCC->u64CpuFrequency = pThisCC->u64TscFrequency;
    pThisCC->u64FsbFrequency = PDMDevHlpCpuGetGuestScalableBusFrequency(pDevIns);

    /*
     * EFI graphics mode (with new EFI VGA code used only as a fallback, for
     * old EFI VGA code the only way to select the GOP mode).
     */
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "GraphicsMode", &pThisCC->u32GraphicsMode, UINT32_MAX);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"GraphicsMode\" as a 32-bit int failed"));
    if (pThisCC->u32GraphicsMode == UINT32_MAX)
    {
        /* get the legacy value if nothing else was specified */
        rc = pHlp->pfnCFGMQueryU32Def(pCfg, "GopMode", &pThisCC->u32GraphicsMode, UINT32_MAX);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Configuration error: Querying \"GopMode\" as a 32-bit int failed"));
    }
    if (pThisCC->u32GraphicsMode == UINT32_MAX)
        pThisCC->u32GraphicsMode = 2; /* 1024x768, at least typically */

    /*
     * EFI graphics resolution, defaults to 1024x768 (used to be UGA only, now
     * is the main config setting as the mode number is so hard to predict).
     */
    char szResolution[16];
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "GraphicsResolution", szResolution, sizeof(szResolution), "");
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"GraphicsResolution\" as a string failed"));
    if (szResolution[0])
    {
        const char *pszX = RTStrStr(szResolution, "x");
        if (pszX)
        {
            pThisCC->u32HorizontalResolution = RTStrToUInt32(szResolution);
            pThisCC->u32VerticalResolution = RTStrToUInt32(pszX + 1);
        }
    }
    else
    {
        /* get the legacy values if nothing else was specified */
        rc = pHlp->pfnCFGMQueryU32Def(pCfg, "UgaHorizontalResolution", &pThisCC->u32HorizontalResolution, 0);
        AssertRCReturn(rc, rc);
        rc = pHlp->pfnCFGMQueryU32Def(pCfg, "UgaVerticalResolution", &pThisCC->u32VerticalResolution, 0);
        AssertRCReturn(rc, rc);
    }
    if (pThisCC->u32HorizontalResolution == 0 || pThisCC->u32VerticalResolution == 0)
    {
        pThisCC->u32HorizontalResolution = 1024;
        pThisCC->u32VerticalResolution = 768;
    }

    pThisCC->pszNvramFile = NULL;
    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "NvramFile", &pThisCC->pszNvramFile);
    if (RT_FAILURE(rc) && rc != VERR_CFGM_VALUE_NOT_FOUND)
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"NvramFile\" as a string failed"));

    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "TpmPpiBase", &pThisCC->u64TpmPpiBase, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"TpmPpiBase\" as integer failed"));

    /*
     * Load firmware volume and thunk ROM.
     */
    rc = efiLoadRom(pDevIns, pThis, pThisCC, pCfg);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register our I/O ports.
     */
    rc = PDMDevHlpIoPortCreateFlagsAndMap(pDevIns, EFI_PORT_BASE, EFI_PORT_COUNT, IOM_IOPORT_F_ABS,
                                          efiR3IoPortWrite, efiR3IoPortRead,
                                          "EFI communication ports", NULL /*paExtDescs*/, &pThis->hIoPorts);
    AssertRCReturn(rc, rc);

    /*
     * Plant DMI and MPS tables in the ROM region.
     */
    rc = FwCommonPlantDMITable(pDevIns, pThisCC->au8DMIPage, VBOX_DMI_TABLE_SIZE, &pThisCC->aUuid,
                               pDevIns->pCfg, pThisCC->cCpus, &pThisCC->cbDmiTables, &pThisCC->cNumDmiTables,
                               true /*fUefi*/);
    AssertRCReturn(rc, rc);

    /*
     * NB: VBox/Devices/EFI/Firmware/VBoxPkg/VBoxSysTables/VBoxSysTables.c scans memory for
     * the SMBIOS header. The header must be placed in a range that EFI will scan.
     */
    FwCommonPlantSmbiosAndDmiHdrs(pDevIns, pThisCC->au8DMIPage + VBOX_DMI_TABLE_SIZE,
                                  pThisCC->cbDmiTables, pThisCC->cNumDmiTables);

    if (pThisCC->u8IOAPIC)
    {
        FwCommonPlantMpsTable(pDevIns,
                              pThisCC->au8DMIPage /* aka VBOX_DMI_TABLE_BASE */ + VBOX_DMI_TABLE_SIZE + VBOX_DMI_HDR_SIZE,
                              _4K - VBOX_DMI_TABLE_SIZE - VBOX_DMI_HDR_SIZE, pThisCC->cCpus);
        FwCommonPlantMpsFloatPtr(pDevIns, VBOX_DMI_TABLE_BASE + VBOX_DMI_TABLE_SIZE + VBOX_DMI_HDR_SIZE);
    }

    rc = PDMDevHlpROMRegister(pDevIns, VBOX_DMI_TABLE_BASE, _4K, pThisCC->au8DMIPage, _4K,
                              PGMPHYS_ROM_FLAGS_PERMANENT_BINARY, "DMI tables");

    AssertRCReturn(rc, rc);

    /*
     * Call reset to set things up.
     */
    efiReset(pDevIns);

    return VINF_SUCCESS;
}

#else  /* IN_RING3 */


/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int)  efiRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVEFI pThis = PDMDEVINS_2_DATA(pDevIns, PDEVEFI);

# if 1
    int rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmioFlash, efiR3NvMmioWrite, efiR3NvMmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);
# else
    RT_NOREF(pDevIns, pThis); (void)&efiR3NvMmioRead; (void)&efiR3NvMmioWrite;
# endif

    return VINF_SUCCESS;
}


#endif /* IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceEFI =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "efi",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_ARCH_BIOS,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVEFI),
    /* .cbInstanceCC = */           sizeof(DEVEFICC),
    /* .cbInstanceRC = */           sizeof(DEVEFIRC),
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Extensible Firmware Interface Device.\n"
                                    "LUN#0 - NVRAM port",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           efiConstruct,
    /* .pfnDestruct = */            efiDestruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            efiMemSetup,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               efiReset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        efiInitComplete,
    /* .pfnPowerOff = */            efiPowerOff,
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
    /* .pfnConstruct = */           efiRZConstruct,
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
    /* .pfnConstruct = */           efiRZConstruct,
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

