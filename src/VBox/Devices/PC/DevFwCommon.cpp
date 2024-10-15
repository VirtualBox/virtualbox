/* $Id: DevFwCommon.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * FwCommon - Shared firmware code (used by DevPcBios & DevEFI).
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV
#include <VBox/vmm/pdmdev.h>

#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/system.h>
#include <iprt/cdefs.h>
#include <iprt/alloca.h>

#include "VBoxDD.h"
#include "VBoxDD2.h"
#include "DevFwCommon.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/*
 * Default DMI data (legacy).
 * Don't change this information otherwise Windows guests might demand re-activation!
 */

/* type 0 -- DMI BIOS information */
static const int32_t g_iDefDmiBIOSReleaseMajor  = 0;
static const int32_t g_iDefDmiBIOSReleaseMinor  = 0;
static const int32_t g_iDefDmiBIOSFirmwareMajor = 0;
static const int32_t g_iDefDmiBIOSFirmwareMinor = 0;
static const char   *g_pszDefDmiBIOSVendor      = "innotek GmbH";
static const char   *g_pszDefDmiBIOSVersion     = "VirtualBox";
static const char   *g_pszDefDmiBIOSReleaseDate = "12/01/2006";
/* type 1 -- DMI system information */
static const char   *g_pszDefDmiSystemVendor    = "innotek GmbH";
static const char   *g_pszDefDmiSystemProduct   = "VirtualBox";
static const char   *g_pszDefDmiSystemVersion   = "1.2";
static const char   *g_pszDefDmiSystemSerial    = "0";
static const char   *g_pszDefDmiSystemSKU       = "";
static const char   *g_pszDefDmiSystemFamily    = "Virtual Machine";
/* type 2 -- DMI board information */
static const char   *g_pszDefDmiBoardVendor     = "Oracle Corporation";
static const char   *g_pszDefDmiBoardProduct    = "VirtualBox";
static const char   *g_pszDefDmiBoardVersion    = "1.2";
static const char   *g_pszDefDmiBoardSerial     = "0";
static const char   *g_pszDefDmiBoardAssetTag   = "";
static const char   *g_pszDefDmiBoardLocInChass = "";
static const int32_t g_iDefDmiBoardBoardType    = 0x0A; /* Motherboard */
/* type 3 -- DMI chassis information */
static const char   *g_pszDefDmiChassisVendor   = "Oracle Corporation";
static const int32_t g_iDefDmiChassisType       = 0x01; /* ''other'', no chassis lock present */
static const char   *g_pszDefDmiChassisVersion  = "";
static const char   *g_pszDefDmiChassisSerial   = "";
static const char   *g_pszDefDmiChassisAssetTag = "";
/* type 4 -- DMI processor information */
static const char   *g_pszDefDmiProcManufacturer= "GenuineIntel";
static const char   *g_pszDefDmiProcVersion     = "Pentium(R) III";

/** The host DMI system product value, for DmiUseHostInfo=1. */
static       char    g_szHostDmiSystemProduct[64];
/** The host DMI system version value, for DmiUseHostInfo=1. */
static       char    g_szHostDmiSystemVersion[64];


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#pragma pack(1)

typedef struct SMBIOSHDR
{
    uint8_t         au8Signature[4];
    uint8_t         u8Checksum;
    uint8_t         u8Eps;
    uint8_t         u8VersionMajor;
    uint8_t         u8VersionMinor;
    uint16_t        u16MaxStructureSize;
    uint8_t         u8EntryPointRevision;
    uint8_t         u8Pad[5];
} *SMBIOSHDRPTR;
AssertCompileSize(SMBIOSHDR, 16);

typedef struct DMIMAINHDR
{
    uint8_t         au8Signature[5];
    uint8_t         u8Checksum;
    uint16_t        u16TablesLength;
    uint32_t        u32TableBase;
    uint16_t        u16TableEntries;
    uint8_t         u8TableVersion;
} *DMIMAINHDRPTR;
AssertCompileSize(DMIMAINHDR, 15);

AssertCompile(sizeof(SMBIOSHDR) + sizeof(DMIMAINHDR) <= VBOX_DMI_HDR_SIZE);

/** DMI header */
typedef struct DMIHDR
{
    uint8_t         u8Type;
    uint8_t         u8Length;
    uint16_t        u16Handle;
} *PDMIHDR;
AssertCompileSize(DMIHDR, 4);

/** DMI BIOS information (Type 0) */
typedef struct DMIBIOSINF
{
    DMIHDR          header;
    uint8_t         u8Vendor;
    uint8_t         u8Version;
    uint16_t        u16Start;
    uint8_t         u8Release;
    uint8_t         u8ROMSize;
    uint64_t        u64Characteristics;
    uint8_t         u8CharacteristicsByte1;
    uint8_t         u8CharacteristicsByte2;
    uint8_t         u8ReleaseMajor;
    uint8_t         u8ReleaseMinor;
    uint8_t         u8FirmwareMajor;
    uint8_t         u8FirmwareMinor;
} *PDMIBIOSINF;
AssertCompileSize(DMIBIOSINF, 0x18);

/** DMI system information (Type 1) */
typedef struct DMISYSTEMINF
{
    DMIHDR          header;
    uint8_t         u8Manufacturer;
    uint8_t         u8ProductName;
    uint8_t         u8Version;
    uint8_t         u8SerialNumber;
    uint8_t         au8Uuid[16];
    uint8_t         u8WakeupType;
    uint8_t         u8SKUNumber;
    uint8_t         u8Family;
} *PDMISYSTEMINF;
AssertCompileSize(DMISYSTEMINF, 0x1b);

/** DMI board (or module) information (Type 2) */
typedef struct DMIBOARDINF
{
    DMIHDR          header;
    uint8_t         u8Manufacturer;
    uint8_t         u8Product;
    uint8_t         u8Version;
    uint8_t         u8SerialNumber;
    uint8_t         u8AssetTag;
    uint8_t         u8FeatureFlags;
    uint8_t         u8LocationInChass;
    uint16_t        u16ChassisHandle;
    uint8_t         u8BoardType;
    uint8_t         u8cObjectHandles;
} *PDMIBOARDINF;
AssertCompileSize(DMIBOARDINF, 0x0f);

/** DMI system enclosure or chassis type (Type 3) */
typedef struct DMICHASSIS
{
    DMIHDR          header;
    uint8_t         u8Manufacturer;
    uint8_t         u8Type;
    uint8_t         u8Version;
    uint8_t         u8SerialNumber;
    uint8_t         u8AssetTag;
    uint8_t         u8BootupState;
    uint8_t         u8PowerSupplyState;
    uint8_t         u8ThermalState;
    uint8_t         u8SecurityStatus;
    /* v2.3+, currently not supported */
    uint32_t        u32OEMdefined;
    uint8_t         u8Height;
    uint8_t         u8NumPowerChords;
    uint8_t         u8ContElems;
    uint8_t         u8ContElemRecLen;
} *PDMICHASSIS;
AssertCompileSize(DMICHASSIS, 0x15);

/** DMI processor information (Type 4) */
typedef struct DMIPROCESSORINF
{
    DMIHDR          header;
    uint8_t         u8SocketDesignation;
    uint8_t         u8ProcessorType;
    uint8_t         u8ProcessorFamily;
    uint8_t         u8ProcessorManufacturer;
    uint64_t        u64ProcessorID;
    uint8_t         u8ProcessorVersion;
    uint8_t         u8Voltage;
    uint16_t        u16ExternalClock;
    uint16_t        u16MaxSpeed;
    uint16_t        u16CurrentSpeed;
    uint8_t         u8Status;
    uint8_t         u8ProcessorUpgrade;
    /* v2.1+ */
    uint16_t        u16L1CacheHandle;
    uint16_t        u16L2CacheHandle;
    uint16_t        u16L3CacheHandle;
    /* v2.3+ */
    uint8_t         u8SerialNumber;
    uint8_t         u8AssetTag;
    uint8_t         u8PartNumber;
    /* v2.5+ */
    uint8_t         u8CoreCount;
    uint8_t         u8CoreEnabled;
    uint8_t         u8ThreadCount;
    uint16_t        u16ProcessorCharacteristics;
    /* v2.6+ */
    uint16_t        u16ProcessorFamily2;
} *PDMIPROCESSORINF;
AssertCompileSize(DMIPROCESSORINF, 0x2a);

/** DMI OEM strings (Type 11) */
typedef struct DMIOEMSTRINGS
{
    DMIHDR          header;
    uint8_t         u8Count;
    uint8_t         u8VBoxVersion;
    uint8_t         u8VBoxRevision;
} *PDMIOEMSTRINGS;
AssertCompileSize(DMIOEMSTRINGS, 0x7);

/** DMI OEM-specific table (Type 128) */
typedef struct DMIOEMSPECIFIC
{
    DMIHDR          header;
    uint32_t        u32CpuFreqKHz;
} *PDMIOEMSPECIFIC;
AssertCompileSize(DMIOEMSPECIFIC, 0x8);

/** Physical memory array (Type 16) */
typedef struct DMIRAMARRAY
{
    DMIHDR          header;
    uint8_t         u8Location;
    uint8_t         u8Use;
    uint8_t         u8MemErrorCorrection;
    uint32_t        u32MaxCapacity;
    uint16_t        u16MemErrorHandle;
    uint16_t        u16NumberOfMemDevices;
} *PDMIRAMARRAY;
AssertCompileSize(DMIRAMARRAY, 15);

/** DMI Memory Device (Type 17) */
typedef struct DMIMEMORYDEV
{
    DMIHDR          header;
    uint16_t        u16PhysMemArrayHandle;
    uint16_t        u16MemErrHandle;
    uint16_t        u16TotalWidth;
    uint16_t        u16DataWidth;
    uint16_t        u16Size;
    uint8_t         u8FormFactor;
    uint8_t         u8DeviceSet;
    uint8_t         u8DeviceLocator;
    uint8_t         u8BankLocator;
    uint8_t         u8MemoryType;
    uint16_t        u16TypeDetail;
    uint16_t        u16Speed;
    uint8_t         u8Manufacturer;
    uint8_t         u8SerialNumber;
    uint8_t         u8AssetTag;
    uint8_t         u8PartNumber;
    /* v2.6+ */
    uint8_t         u8Attributes;
    /* v2.7+ */
    uint32_t        u32ExtendedSize;
    uint16_t        u16CfgSpeed;    /* Configured speed in MT/sec. */
} *PDMIMEMORYDEV;
AssertCompileSize(DMIMEMORYDEV, 34);

/** MPS floating pointer structure */
typedef struct MPSFLOATPTR
{
    uint8_t         au8Signature[4];
    uint32_t        u32MPSAddr;
    uint8_t         u8Length;
    uint8_t         u8SpecRev;
    uint8_t         u8Checksum;
    uint8_t         au8Feature[5];
} *PMPSFLOATPTR;
AssertCompileSize(MPSFLOATPTR, 16);

/** MPS config table header */
typedef struct MPSCFGTBLHEADER
{
    uint8_t         au8Signature[4];
    uint16_t        u16Length;
    uint8_t         u8SpecRev;
    uint8_t         u8Checksum;
    uint8_t         au8OemId[8];
    uint8_t         au8ProductId[12];
    uint32_t        u32OemTablePtr;
    uint16_t        u16OemTableSize;
    uint16_t        u16EntryCount;
    uint32_t        u32AddrLocalApic;
    uint16_t        u16ExtTableLength;
    uint8_t         u8ExtTableChecksum;
    uint8_t         u8Reserved;
} *PMPSCFGTBLHEADER;
AssertCompileSize(MPSCFGTBLHEADER, 0x2c);

/** MPS processor entry */
typedef struct MPSPROCENTRY
{
    uint8_t         u8EntryType;
    uint8_t         u8LocalApicId;
    uint8_t         u8LocalApicVersion;
    uint8_t         u8CPUFlags;
    uint32_t        u32CPUSignature;
    uint32_t        u32CPUFeatureFlags;
    uint32_t        u32Reserved[2];
} *PMPSPROCENTRY;
AssertCompileSize(MPSPROCENTRY, 20);

/** MPS bus entry */
typedef struct MPSBUSENTRY
{
    uint8_t         u8EntryType;
    uint8_t         u8BusId;
    uint8_t         au8BusTypeStr[6];
} *PMPSBUSENTRY;
AssertCompileSize(MPSBUSENTRY, 8);

/** MPS I/O-APIC entry */
typedef struct MPSIOAPICENTRY
{
    uint8_t         u8EntryType;
    uint8_t         u8Id;
    uint8_t         u8Version;
    uint8_t         u8Flags;
    uint32_t        u32Addr;
} *PMPSIOAPICENTRY;
AssertCompileSize(MPSIOAPICENTRY, 8);

/** MPS I/O-Interrupt entry */
typedef struct MPSIOINTERRUPTENTRY
{
    uint8_t         u8EntryType;
    uint8_t         u8Type;
    uint16_t        u16Flags;
    uint8_t         u8SrcBusId;
    uint8_t         u8SrcBusIrq;
    uint8_t         u8DstIOAPICId;
    uint8_t         u8DstIOAPICInt;
} *PMPSIOIRQENTRY;
AssertCompileSize(MPSIOINTERRUPTENTRY, 8);

#pragma pack()


/**
 * Calculate a simple checksum for the MPS table.
 *
 * @param   au8Data         data
 * @param   u32Length       size of data
 */
static uint8_t fwCommonChecksum(const uint8_t * const au8Data, uint32_t u32Length)
{
    uint8_t u8Sum = 0;
    for (size_t i = 0; i < u32Length; ++i)
        u8Sum += au8Data[i];
    return -u8Sum;
}

#if 0 /* unused */
static bool fwCommonChecksumOk(const uint8_t * const au8Data, uint32_t u32Length)
{
    uint8_t u8Sum = 0;
    for (size_t i = 0; i < u32Length; i++)
        u8Sum += au8Data[i];
    return (u8Sum == 0);
}
#endif

/**
 * Try fetch the DMI strings from the system.
 */
static void fwCommonUseHostDMIStrings(void)
{
    int rc;

    rc = RTSystemQueryDmiString(RTSYSDMISTR_PRODUCT_NAME,
                                g_szHostDmiSystemProduct, sizeof(g_szHostDmiSystemProduct));
    if (RT_SUCCESS(rc))
    {
        g_pszDefDmiSystemProduct = g_szHostDmiSystemProduct;
        LogRel(("DMI: Using DmiSystemProduct from host: %s\n", g_szHostDmiSystemProduct));
    }

    rc = RTSystemQueryDmiString(RTSYSDMISTR_PRODUCT_VERSION,
                                g_szHostDmiSystemVersion, sizeof(g_szHostDmiSystemVersion));
    if (RT_SUCCESS(rc))
    {
        g_pszDefDmiSystemVersion = g_szHostDmiSystemVersion;
        LogRel(("DMI: Using DmiSystemVersion from host: %s\n", g_szHostDmiSystemVersion));
    }
}

/**
 * Replace the DmiSystemUuid placeholder with the actual value.
 *
 * @param   pszBuf              Buffer
 * @param   cbBuf               Size of buffer
 * @param   pcszPlaceholder     Pointer to placeholder, must be in pszBuf
 * @param   cbPlaceholder       Length of placeholder
 * @param   pcszDmiSystemUuid   DmiSystemUuid value
 */
static void fwUseDmiSystemUuidInString(char *pszBuf, size_t cbBuf,
                                       const char *pcszPlaceholder, size_t cbPlaceholder,
                                       const char *pcszDmiSystemUuid)
{
    size_t const cbPrefix = pcszPlaceholder - pszBuf;
    size_t const cbUuid = strlen(pcszDmiSystemUuid);
    size_t const cbSuffix = strlen(pcszPlaceholder + cbPlaceholder);
    if (cbPrefix + cbUuid + cbSuffix < cbBuf)
    {
        /* Everything fits, no truncation. */
        memmove(pszBuf + cbPrefix + cbUuid, pcszPlaceholder + cbPlaceholder, cbSuffix + 1); \
        memcpy(pszBuf + cbPrefix, pcszDmiSystemUuid, cbUuid); \
    }
    else if (cbPrefix + cbUuid < cbBuf)
    {
        /* Prefix + DmiSystemUuid fits, truncate suffix. */
        memmove(pszBuf + cbPrefix + cbUuid, pcszPlaceholder + cbPlaceholder, cbBuf - cbPrefix - cbUuid - 1); \
        memcpy(pszBuf + cbPrefix, pcszDmiSystemUuid, cbUuid); \
        pszBuf[cbBuf] = '\0';
    }
    else
    {
        /* Prefix fits, truncate DmiSystemUuid. */
        memcpy(pszBuf + cbPrefix, pcszDmiSystemUuid, cbBuf - cbPrefix - 1); \
        pszBuf[cbBuf] = '\0';
    }
}

/**
 * Construct the DMI table.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pTable              Where to create the DMI table.
 * @param   cbMax               The maximum size of the DMI table.
 * @param   pUuid               Pointer to the UUID to use if the DmiUuid
 *                              configuration string isn't present.
 * @param   pCfg                The handle to our config node.
 * @param   cCpus               Number of VCPUs.
 * @param   pcbDmiTables        Size of DMI data in bytes.
 * @param   pcDmiTables         Number of DMI tables.
 * @param   fUefi               Flag whether the UEFI specification is supported.
 */
int FwCommonPlantDMITable(PPDMDEVINS pDevIns, uint8_t *pTable, unsigned cbMax, PCRTUUID pUuid, PCFGMNODE pCfg, uint16_t cCpus,
                          uint16_t *pcbDmiTables, uint16_t *pcDmiTables, bool fUefi)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    /*
     * CFGM Hint!
     *
     * The macros below makes it a bit hard to figure out the config options
     * available here.  To get a quick hint, take a look a the CFGM
     * validation in the calling code (DevEFI.cpp and DevPcBios.cpp).
     *
     * 32-bit signed integer CFGM options are read by DMI_READ_CFG_S32, the 2nd
     * parameter is the CFGM value name.
     *
     * Strings are read by DMI_READ_CFG_STR and DMI_READ_CFG_STR_DEF, the 2nd parameter is
     * the CFGM value name.
     */
#define DMI_CHECK_SIZE(cbWant) \
    { \
        size_t cbNeed = (size_t)(pszStr + cbWant - (char *)pTable) + 5; /* +1 for strtab terminator +4 for end-of-table entry */ \
        if (cbNeed > cbMax) \
        { \
            if (fHideErrors) \
            { \
                LogRel(("One of the DMI strings is too long -- using default DMI data!\n")); \
                continue; \
            } \
            return PDMDevHlpVMSetError(pDevIns, VERR_TOO_MUCH_DATA, RT_SRC_POS, \
                                       N_("One of the DMI strings is too long. Check all bios/Dmi* configuration entries. At least %zu bytes are needed but there is no space for more than %d bytes"), cbNeed, cbMax); \
        } \
    }

#define DMI_READ_CFG_STR_DEF(variable, name, default_value) \
    { \
        if (fForceDefault) \
            pszTmp = default_value; \
        else \
        { \
            rc = pHlp->pfnCFGMQueryStringDef(pCfg, name, szBuf, sizeof(szBuf), default_value); \
            if (RT_FAILURE(rc)) \
            { \
                if (fHideErrors) \
                { \
                    LogRel(("Configuration error: Querying \"" name "\" as a string failed -- using default DMI data!\n")); \
                    continue; \
                } \
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, \
                                           N_("Configuration error: Querying \"" name "\" as a string failed")); \
            } \
            if (!strcmp(szBuf, "<EMPTY>")) \
                pszTmp = ""; \
            else if ((pszTmp = RTStrStr(szBuf, "<DmiSystemUuid>"))) \
            { \
                char *pszUuid = pszDmiSystemUuid; \
                if (!pszUuid) \
                { \
                    pszUuid = (char *)alloca(RTUUID_STR_LENGTH); \
                    RTUuidToStr(pUuid, pszUuid, RTUUID_STR_LENGTH); \
                } \
                fwUseDmiSystemUuidInString(szBuf, sizeof(szBuf), pszTmp, 15, pszUuid); \
                pszTmp = szBuf; \
            } \
            else \
                pszTmp = szBuf; \
        } \
        if (!pszTmp[0]) \
            variable = 0; /* empty string */ \
        else \
        { \
            variable = iStrNr++; \
            size_t const cbStr = strlen(pszTmp) + 1; \
            DMI_CHECK_SIZE(cbStr); \
            pszStr = (char *)mempcpy(pszStr, pszTmp, cbStr); \
        } \
    }

#define DMI_READ_CFG_STR(variable, name) \
    DMI_READ_CFG_STR_DEF(variable, # name, g_pszDef ## name)

#define DMI_READ_CFG_S32(variable, name) \
    { \
        if (fForceDefault) \
            variable = g_iDef ## name; \
        else \
        { \
            rc = pHlp->pfnCFGMQueryS32Def(pCfg, # name, & variable, g_iDef ## name); \
            if (RT_FAILURE(rc)) \
            { \
                if (fHideErrors) \
                { \
                    LogRel(("Configuration error: Querying \"" # name "\" as an int failed -- using default DMI data!\n")); \
                    continue; \
                } \
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, \
                                           N_("Configuration error: Querying \"" # name "\" as an int failed")); \
            } \
        } \
    }

#define DMI_START_STRUCT(a_pTbl) do { \
        pszStr = (char *)((a_pTbl) + 1); \
        iStrNr = 1; \
    } while (0)

#if 0 /* GCC 11.2.1 barfs on this: error: writing 1 byte into a region of size 0 [-Werror=stringop-overflow=] */
# define DMI_TERM_STRUCT do { \
        *pszStr++                    = '\0'; /* terminate set of text strings */ \
        if (iStrNr == 1) \
            *pszStr++                = '\0'; /* terminate a structure without strings */ \
    } while (0)
#else
# define DMI_TERM_STRUCT do { \
        size_t const cbToZero = iStrNr == 1 ? 2 : 1; \
        pszStr = (char *)memset(pszStr, 0, cbToZero) + cbToZero; \
    } while (0)
#endif

    bool fForceDefault = false;
#ifdef VBOX_BIOS_DMI_FALLBACK
    /*
     * There will be two passes. If an error occurs during the first pass, a
     * message will be written to the release log and we fall back to default
     * DMI data and start a second pass.
     */
    bool fHideErrors = true;
#else
    /*
     * There will be one pass, every error is fatal and will prevent the VM
     * from starting.
     */
    bool fHideErrors = false;
#endif

    uint8_t fDmiUseHostInfo;
    int rc = pHlp->pfnCFGMQueryU8Def(pCfg, "DmiUseHostInfo", &fDmiUseHostInfo, 0);
    if (RT_FAILURE (rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"DmiUseHostInfo\""));

    /* Sync up with host default DMI values */
    if (fDmiUseHostInfo)
        fwCommonUseHostDMIStrings();

    uint8_t fDmiExposeMemoryTable;
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "DmiExposeMemoryTable", &fDmiExposeMemoryTable, 0);
    if (RT_FAILURE (rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"DmiExposeMemoryTable\""));
    uint8_t fDmiExposeProcessorInf;
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "DmiExposeProcInf", &fDmiExposeProcessorInf, 0);
    if (RT_FAILURE (rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to read \"DmiExposeProcInf\""));

    for  (;; fForceDefault = true, fHideErrors = false)
    {
        int  iStrNr;
        char szBuf[256];
        char *pszStr = (char *)pTable;
        char szDmiSystemUuid[64];
        char *pszDmiSystemUuid;
        const char *pszTmp;

        if (fForceDefault)
            pszDmiSystemUuid = NULL;
        else
        {
            rc = pHlp->pfnCFGMQueryString(pCfg, "DmiSystemUuid", szDmiSystemUuid, sizeof(szDmiSystemUuid));
            if (rc == VERR_CFGM_VALUE_NOT_FOUND)
                pszDmiSystemUuid = NULL;
            else if (RT_FAILURE(rc))
            {
                if (fHideErrors)
                {
                    LogRel(("Configuration error: Querying \"DmiSystemUuid\" as a string failed, using default DMI data\n"));
                    continue;
                }
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("Configuration error: Querying \"DmiSystemUuid\" as a string failed"));
            }
            else
                pszDmiSystemUuid = szDmiSystemUuid;
        }

        /*********************************
         * DMI BIOS information (Type 0) *
         *********************************/
        PDMIBIOSINF pBIOSInf         = (PDMIBIOSINF)pszStr;
        DMI_CHECK_SIZE(sizeof(*pBIOSInf));

        pszStr                       = (char *)&pBIOSInf->u8ReleaseMajor;
        pBIOSInf->header.u8Length    = RT_OFFSETOF(DMIBIOSINF, u8ReleaseMajor);

        /* don't set these fields by default for legacy compatibility */
        int iDmiBIOSReleaseMajor, iDmiBIOSReleaseMinor;
        DMI_READ_CFG_S32(iDmiBIOSReleaseMajor, DmiBIOSReleaseMajor);
        DMI_READ_CFG_S32(iDmiBIOSReleaseMinor, DmiBIOSReleaseMinor);
        if (iDmiBIOSReleaseMajor != 0 || iDmiBIOSReleaseMinor != 0)
        {
            pszStr = (char *)&pBIOSInf->u8FirmwareMajor;
            pBIOSInf->header.u8Length = RT_OFFSETOF(DMIBIOSINF, u8FirmwareMajor);
            pBIOSInf->u8ReleaseMajor  = iDmiBIOSReleaseMajor;
            pBIOSInf->u8ReleaseMinor  = iDmiBIOSReleaseMinor;

            int iDmiBIOSFirmwareMajor, iDmiBIOSFirmwareMinor;
            DMI_READ_CFG_S32(iDmiBIOSFirmwareMajor, DmiBIOSFirmwareMajor);
            DMI_READ_CFG_S32(iDmiBIOSFirmwareMinor, DmiBIOSFirmwareMinor);
            if (iDmiBIOSFirmwareMajor != 0 || iDmiBIOSFirmwareMinor != 0)
            {
                pszStr = (char *)(pBIOSInf + 1);
                pBIOSInf->header.u8Length = sizeof(DMIBIOSINF);
                pBIOSInf->u8FirmwareMajor = iDmiBIOSFirmwareMajor;
                pBIOSInf->u8FirmwareMinor = iDmiBIOSFirmwareMinor;
            }
        }

        iStrNr                       = 1;
        pBIOSInf->header.u8Type      = 0; /* BIOS Information */
        pBIOSInf->header.u16Handle   = 0x0000;
        DMI_READ_CFG_STR(pBIOSInf->u8Vendor,  DmiBIOSVendor);
        DMI_READ_CFG_STR(pBIOSInf->u8Version, DmiBIOSVersion);
        pBIOSInf->u16Start           = 0xE000;
        DMI_READ_CFG_STR(pBIOSInf->u8Release, DmiBIOSReleaseDate);
        pBIOSInf->u8ROMSize          = 1; /* 128K */
        pBIOSInf->u64Characteristics = RT_BIT(4)   /* ISA is supported */
                                     | RT_BIT(7)   /* PCI is supported */
                                     | RT_BIT(15)  /* Boot from CD is supported */
                                     | RT_BIT(16)  /* Selectable Boot is supported */
                                     | RT_BIT(27)  /* Int 9h, 8042 Keyboard services supported */
                                     | RT_BIT(30)  /* Int 10h, CGA/Mono Video Services supported */
                                     /* any more?? */
                                     ;
        pBIOSInf->u8CharacteristicsByte1 = RT_BIT(0)   /* ACPI is supported */
                                         /* any more?? */
                                         ;
        pBIOSInf->u8CharacteristicsByte2 = RT_BIT(2)   /* Enable targeted content distribution, needed by Windows to populate registry, see
                                                          https://download.microsoft.com/download/5/D/6/5D6EAF2B-7DDF-476B-93DC-7CF0072878E6/SMBIOS.doc */
                                         | (fUefi ? RT_BIT(3) : 0)
                                         /* any more?? */
                                         ;
        DMI_TERM_STRUCT;

        /***********************************
         * DMI system information (Type 1) *
         ***********************************/
        PDMISYSTEMINF pSystemInf     = (PDMISYSTEMINF)pszStr;
        DMI_CHECK_SIZE(sizeof(*pSystemInf));
        DMI_START_STRUCT(pSystemInf);
        pSystemInf->header.u8Type    = 1; /* System Information */
        pSystemInf->header.u8Length  = sizeof(*pSystemInf);
        pSystemInf->header.u16Handle = 0x0001;
        DMI_READ_CFG_STR(pSystemInf->u8Manufacturer, DmiSystemVendor);
        DMI_READ_CFG_STR(pSystemInf->u8ProductName,  DmiSystemProduct);
        DMI_READ_CFG_STR(pSystemInf->u8Version,      DmiSystemVersion);
        DMI_READ_CFG_STR(pSystemInf->u8SerialNumber, DmiSystemSerial);

        RTUUID uuid;
        if (pszDmiSystemUuid)
        {
            rc = RTUuidFromStr(&uuid, pszDmiSystemUuid);
            if (RT_FAILURE(rc))
            {
                if (fHideErrors)
                {
                    LogRel(("Configuration error: Invalid UUID for DMI tables specified, using default DMI data\n"));
                    continue;
                }
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("Configuration error: Invalid UUID for DMI tables specified"));
            }
            uuid.Gen.u32TimeLow = RT_H2BE_U32(uuid.Gen.u32TimeLow);
            uuid.Gen.u16TimeMid = RT_H2BE_U16(uuid.Gen.u16TimeMid);
            uuid.Gen.u16TimeHiAndVersion = RT_H2BE_U16(uuid.Gen.u16TimeHiAndVersion);
            pUuid = &uuid;
        }
        memcpy(pSystemInf->au8Uuid, pUuid, sizeof(RTUUID));

        pSystemInf->u8WakeupType     = 6; /* Power Switch */
        DMI_READ_CFG_STR(pSystemInf->u8SKUNumber, DmiSystemSKU);
        DMI_READ_CFG_STR(pSystemInf->u8Family, DmiSystemFamily);
        DMI_TERM_STRUCT;

        /**********************************
         * DMI board information (Type 2) *
         **********************************/
        PDMIBOARDINF pBoardInf       = (PDMIBOARDINF)pszStr;
        DMI_CHECK_SIZE(sizeof(*pBoardInf));
        DMI_START_STRUCT(pBoardInf);
        int iDmiBoardBoardType;
        pBoardInf->header.u8Type     = 2; /* Board Information */
        pBoardInf->header.u8Length   = sizeof(*pBoardInf);
        pBoardInf->header.u16Handle  = 0x0008;
        DMI_READ_CFG_STR(pBoardInf->u8Manufacturer, DmiBoardVendor);
        DMI_READ_CFG_STR(pBoardInf->u8Product,      DmiBoardProduct);
        DMI_READ_CFG_STR(pBoardInf->u8Version,      DmiBoardVersion);
        DMI_READ_CFG_STR(pBoardInf->u8SerialNumber, DmiBoardSerial);
        DMI_READ_CFG_STR(pBoardInf->u8AssetTag,     DmiBoardAssetTag);
        pBoardInf->u8FeatureFlags    = RT_BIT(0) /* hosting board, e.g. motherboard */
                                     ;
        DMI_READ_CFG_STR(pBoardInf->u8LocationInChass, DmiBoardLocInChass);
        pBoardInf->u16ChassisHandle  = 0x0003; /* see type 3 */
        DMI_READ_CFG_S32(iDmiBoardBoardType, DmiBoardBoardType);
        pBoardInf->u8BoardType = iDmiBoardBoardType;
        pBoardInf->u8cObjectHandles  = 0;

        DMI_TERM_STRUCT;

        /********************************************
         * DMI System Enclosure or Chassis (Type 3) *
         ********************************************/
        PDMICHASSIS pChassis         = (PDMICHASSIS)pszStr;
        DMI_CHECK_SIZE(sizeof(*pChassis));
        pszStr                       = (char *)&pChassis->u32OEMdefined;
        iStrNr                       = 1;
#ifdef VBOX_WITH_DMI_CHASSIS
        pChassis->header.u8Type      = 3; /* System Enclosure or Chassis */
#else
        pChassis->header.u8Type      = 0x7e; /* inactive */
#endif
        pChassis->header.u8Length    = RT_OFFSETOF(DMICHASSIS, u32OEMdefined);
        pChassis->header.u16Handle   = 0x0003;
        DMI_READ_CFG_STR(pChassis->u8Manufacturer, DmiChassisVendor);
        int iDmiChassisType;
        DMI_READ_CFG_S32(iDmiChassisType, DmiChassisType);
        pChassis->u8Type             = iDmiChassisType;
        DMI_READ_CFG_STR(pChassis->u8Version, DmiChassisVersion);
        DMI_READ_CFG_STR(pChassis->u8SerialNumber, DmiChassisSerial);
        DMI_READ_CFG_STR(pChassis->u8AssetTag, DmiChassisAssetTag);
        pChassis->u8BootupState      = 0x03; /* safe */
        pChassis->u8PowerSupplyState = 0x03; /* safe */
        pChassis->u8ThermalState     = 0x03; /* safe */
        pChassis->u8SecurityStatus   = 0x03; /* none XXX */
# if 0
        /* v2.3+, currently not supported */
        pChassis->u32OEMdefined      = 0;
        pChassis->u8Height           = 0; /* unspecified */
        pChassis->u8NumPowerChords   = 0; /* unspecified */
        pChassis->u8ContElems        = 0; /* no contained elements */
        pChassis->u8ContElemRecLen   = 0; /* no contained elements */
# endif
        DMI_TERM_STRUCT;

        /**************************************
         * DMI Processor Information (Type 4) *
         **************************************/

        /*
         * This is just a dummy processor. Should we expose the real guest CPU features
         * here? Accessing this information at this point is difficult.
         */
        char szSocket[32];
        PDMIPROCESSORINF pProcessorInf = (PDMIPROCESSORINF)pszStr;
        DMI_CHECK_SIZE(sizeof(*pProcessorInf));
        DMI_START_STRUCT(pProcessorInf);
        if (fDmiExposeProcessorInf)
            pProcessorInf->header.u8Type   = 4; /* Processor Information */
        else
            pProcessorInf->header.u8Type   = 126; /* inactive structure */
        pProcessorInf->header.u8Length     = sizeof(*pProcessorInf);
        pProcessorInf->header.u16Handle    = 0x0007;
        RTStrPrintf(szSocket, sizeof(szSocket), "Socket #%u", 0);
        pProcessorInf->u8SocketDesignation = iStrNr++;
        {
            size_t const cbStr = strlen(szSocket) + 1;
            DMI_CHECK_SIZE(cbStr);
            pszStr = (char *)mempcpy(pszStr, szSocket, cbStr);
        }
        pProcessorInf->u8ProcessorType     = 0x03; /* Central Processor */
        pProcessorInf->u8ProcessorFamily   = 0xB1; /* Pentium III with Intel SpeedStep(TM) */
        DMI_READ_CFG_STR(pProcessorInf->u8ProcessorManufacturer, DmiProcManufacturer);

        pProcessorInf->u64ProcessorID      = UINT64_C(0x0FEBFBFF00010676);
                                             /* Ext Family ID  = 0
                                              * Ext Model ID   = 2
                                              * Processor Type = 0
                                              * Family ID      = 6
                                              * Model          = 7
                                              * Stepping       = 6
                                              * Features: FPU, VME, DE, PSE, TSC, MSR, PAE, MCE, CX8,
                                              *           APIC, SEP, MTRR, PGE, MCA, CMOV, PAT, PSE-36,
                                              *           CFLSH, DS, ACPI, MMX, FXSR, SSE, SSE2, SS */
        DMI_READ_CFG_STR(pProcessorInf->u8ProcessorVersion, DmiProcVersion);
        pProcessorInf->u8Voltage           = 0x02;   /* 3.3V */
        pProcessorInf->u16ExternalClock    = 0x00;   /* unknown */
        pProcessorInf->u16MaxSpeed         = 3000;   /* 3GHz */
        pProcessorInf->u16CurrentSpeed     = 3000;   /* 3GHz */
        pProcessorInf->u8Status            = RT_BIT(6)  /* CPU socket populated */
                                           | RT_BIT(0)  /* CPU enabled */
                                           ;
        pProcessorInf->u8ProcessorUpgrade  = 0x04;   /* ZIF Socket */
        pProcessorInf->u16L1CacheHandle    = 0xFFFF; /* not specified */
        pProcessorInf->u16L2CacheHandle    = 0xFFFF; /* not specified */
        pProcessorInf->u16L3CacheHandle    = 0xFFFF; /* not specified */
        pProcessorInf->u8SerialNumber      = 0;      /* not specified */
        pProcessorInf->u8AssetTag          = 0;      /* not specified */
        pProcessorInf->u8PartNumber        = 0;      /* not specified */
        pProcessorInf->u8CoreCount         = cCpus;  /*  */
        pProcessorInf->u8CoreEnabled       = cCpus;
        pProcessorInf->u8ThreadCount       = 1;
        pProcessorInf->u16ProcessorCharacteristics
                                           = RT_BIT(2); /* 64-bit capable */
        pProcessorInf->u16ProcessorFamily2 = 0;
        DMI_TERM_STRUCT;

        /***************************************
         * DMI Physical Memory Array (Type 16) *
         ***************************************/
        uint64_t const  cbRamSize = PDMDevHlpMMPhysGetRamSize(pDevIns);

        PDMIRAMARRAY pMemArray = (PDMIRAMARRAY)pszStr;
        DMI_CHECK_SIZE(sizeof(*pMemArray));
        DMI_START_STRUCT(pMemArray);
        if (fDmiExposeMemoryTable)
            pMemArray->header.u8Type     = 16;     /* Physical Memory Array */
        else
            pMemArray->header.u8Type     = 126;    /* inactive structure */
        pMemArray->header.u8Length       = sizeof(*pMemArray);
        pMemArray->header.u16Handle      = 0x0005;
        pMemArray->u8Location            = 0x03;   /* Motherboard */
        pMemArray->u8Use                 = 0x03;   /* System memory */
        pMemArray->u8MemErrorCorrection  = 0x01;   /* Other */
        if (cbRamSize / _1K > INT32_MAX)
        {
            /** @todo 2TB-1K limit. In such cases we probably need to provide multiple type-16 descriptors.
             * Or use 0x8000'0000 = 'capacity unknown'? */
            AssertLogRelMsgFailed(("DMI: RAM size %#RX64 does not fit into type-16 descriptor, clipping to %#RX64\n",
                                   cbRamSize, (uint64_t)INT32_MAX * _1K));
            pMemArray->u32MaxCapacity    = INT32_MAX;
        }
        else
            pMemArray->u32MaxCapacity    = (int32_t)(cbRamSize / _1K); /* RAM size in K */
        pMemArray->u16MemErrorHandle     = 0xfffe; /* No error info structure */
        pMemArray->u16NumberOfMemDevices = 1;
        DMI_TERM_STRUCT;

        /***************************************
         * DMI Memory Device (Type 17)         *
         ***************************************/
        PDMIMEMORYDEV pMemDev = (PDMIMEMORYDEV)pszStr;
        DMI_CHECK_SIZE(sizeof(*pMemDev));
        DMI_START_STRUCT(pMemDev);
        if (fDmiExposeMemoryTable)
            pMemDev->header.u8Type       = 17;     /* Memory Device */
        else
            pMemDev->header.u8Type       = 126;    /* inactive structure */
        pMemDev->header.u8Length         = sizeof(*pMemDev);
        pMemDev->header.u16Handle        = 0x0006;
        pMemDev->u16PhysMemArrayHandle   = 0x0005; /* handle of array we belong to */
        pMemDev->u16MemErrHandle         = 0xfffe; /* system doesn't provide this information */
        pMemDev->u16TotalWidth           = 0xffff; /* Unknown */
        pMemDev->u16DataWidth            = 0xffff; /* Unknown */
        int16_t u16RamSizeM;
        int32_t u32ExtRamSizeM = 0;
        if (cbRamSize / _1M > INT16_MAX)
        {
            /* The highest bit of u16Size must be 0 to specify 'MB' units / 1 would be 'KB'.
             * SMBIOS 2.7 introduced a 32-bit extended size. If module size is 32GB or greater,
             * the old u16Size is set to 7FFFh; old parsers will see 32GB-1MB, new parsers will
             * look at new u32ExtendedSize which can represent at least 128TB. OS X 10.14+ looks
             * at the extended size.
             */
            LogRel(("DMI: RAM size %#RX64 too big for one type-17 descriptor, clipping to %#RX64\n",
                    cbRamSize, (uint64_t)INT16_MAX * _1M));
            u16RamSizeM = INT16_MAX;
            if (cbRamSize / _1M >= 0x8000000) {
                AssertLogRelMsgFailed(("DMI: RAM size %#RX64 too big for one type-17 descriptor, clipping to %#RX64\n",
                                       cbRamSize, (uint64_t)INT32_MAX * _1M));
                u32ExtRamSizeM = 0x8000000; /* 128TB */
            }
            else
                u32ExtRamSizeM = cbRamSize / _1M;
        }
        else
            u16RamSizeM = (uint16_t)(cbRamSize / _1M);
        if (u16RamSizeM == 0)
            u16RamSizeM = 0x400; /* 1G */
        pMemDev->u16Size                 = u16RamSizeM; /* RAM size */
        pMemDev->u32ExtendedSize         = u32ExtRamSizeM;
        pMemDev->u8FormFactor            = 0x09; /* DIMM */
        pMemDev->u8DeviceSet             = 0x00; /* Not part of a device set */
        DMI_READ_CFG_STR_DEF(pMemDev->u8DeviceLocator, " ", "DIMM 0");
        DMI_READ_CFG_STR_DEF(pMemDev->u8BankLocator, " ", "Bank 0");
        pMemDev->u8MemoryType            = 0x03; /* DRAM */
        pMemDev->u16TypeDetail           = 0;    /* Nothing special */
        pMemDev->u16Speed                = 1600; /* Unknown, shall be speed in MHz */
        DMI_READ_CFG_STR(pMemDev->u8Manufacturer, DmiSystemVendor);
        DMI_READ_CFG_STR_DEF(pMemDev->u8SerialNumber, " ", "00000000");
        DMI_READ_CFG_STR_DEF(pMemDev->u8AssetTag, " ", "00000000");
        DMI_READ_CFG_STR_DEF(pMemDev->u8PartNumber, " ", "00000000");
        pMemDev->u8Attributes            = 0; /* Unknown */
        DMI_TERM_STRUCT;

        /*****************************
         * DMI OEM strings (Type 11) *
         *****************************/
        PDMIOEMSTRINGS pOEMStrings    = (PDMIOEMSTRINGS)pszStr;
        DMI_CHECK_SIZE(sizeof(*pOEMStrings));
        DMI_START_STRUCT(pOEMStrings);
#ifdef VBOX_WITH_DMI_OEMSTRINGS
        pOEMStrings->header.u8Type    = 0xb; /* OEM Strings */
#else
        pOEMStrings->header.u8Type    = 126; /* inactive structure */
#endif
        pOEMStrings->header.u8Length  = sizeof(*pOEMStrings);
        pOEMStrings->header.u16Handle = 0x0002;
        pOEMStrings->u8Count          = 2;

        char szTmp[64];
        RTStrPrintf(szTmp, sizeof(szTmp), "vboxVer_%u.%u.%u",
                    RTBldCfgVersionMajor(), RTBldCfgVersionMinor(), RTBldCfgVersionBuild());
        DMI_READ_CFG_STR_DEF(pOEMStrings->u8VBoxVersion, "DmiOEMVBoxVer", szTmp);
        RTStrPrintf(szTmp, sizeof(szTmp), "vboxRev_%u", RTBldCfgRevision());
        DMI_READ_CFG_STR_DEF(pOEMStrings->u8VBoxRevision, "DmiOEMVBoxRev", szTmp);
        DMI_TERM_STRUCT;

        /*************************************
         * DMI OEM specific table (Type 128) *
         ************************************/
        PDMIOEMSPECIFIC pOEMSpecific = (PDMIOEMSPECIFIC)pszStr;
        DMI_CHECK_SIZE(sizeof(*pOEMSpecific));
        DMI_START_STRUCT(pOEMSpecific);
        pOEMSpecific->header.u8Type    = 0x80; /* OEM specific */
        pOEMSpecific->header.u8Length  = sizeof(*pOEMSpecific);
        pOEMSpecific->header.u16Handle = 0x0004;
        pOEMSpecific->u32CpuFreqKHz    = RT_H2LE_U32((uint32_t)((uint64_t)PDMDevHlpTMCpuTicksPerSecond(pDevIns) / 1000));
        DMI_TERM_STRUCT;

        /* End-of-table marker - includes padding to account for fixed table size. */
        PDMIHDR pEndOfTable          = (PDMIHDR)pszStr;
        pszStr                       = (char *)(pEndOfTable + 1);
        pEndOfTable->u8Type          = 0x7f;

        pEndOfTable->u8Length        = sizeof(*pEndOfTable);
        pEndOfTable->u16Handle       = 0xFEFF;
        *pcbDmiTables = ((uintptr_t)pszStr - (uintptr_t)pTable) + 2;

        /* We currently plant 10 DMI tables. Update this if tables number changed. */
        *pcDmiTables = 10;

        /* If more fields are added here, fix the size check in DMI_READ_CFG_STR */

        /* Success! */
        break;
    }

#undef DMI_READ_CFG_STR
#undef DMI_READ_CFG_S32
#undef DMI_CHECK_SIZE
    return VINF_SUCCESS;
}

/**
 * Construct the SMBIOS and DMI headers table pointer at VM construction and
 * reset.
 *
 * @param   pDevIns         The device instance data.
 * @param   pHdr            Pointer to the header destination.
 * @param   cbDmiTables     Size of all DMI tables planted in bytes.
 * @param   cNumDmiTables   Number of DMI tables planted.
 */
void FwCommonPlantSmbiosAndDmiHdrs(PPDMDEVINS pDevIns, uint8_t *pHdr, uint16_t cbDmiTables, uint16_t cNumDmiTables)
{
    RT_NOREF(pDevIns);

    struct
    {
        struct SMBIOSHDR     smbios;
        struct DMIMAINHDR    dmi;
    }
    aBiosHeaders =
    {
        // The SMBIOS header
        {
            { 0x5f, 0x53, 0x4d, 0x5f},         // "_SM_" signature
            0x00,                              // checksum
            0x1f,                              // EPS length, defined by standard
            VBOX_SMBIOS_MAJOR_VER,             // SMBIOS major version
            VBOX_SMBIOS_MINOR_VER,             // SMBIOS minor version
            VBOX_SMBIOS_MAXSS,                 // Maximum structure size
            0x00,                              // Entry point revision
            { 0x00, 0x00, 0x00, 0x00, 0x00 }   // padding
        },
        // The DMI header
        {
            { 0x5f, 0x44, 0x4d, 0x49, 0x5f },  // "_DMI_" signature
            0x00,                              // checksum
            0,                                 // DMI tables length
            VBOX_DMI_TABLE_BASE,               // DMI tables base
            0,                                 // DMI tables entries
            VBOX_DMI_TABLE_VER,                // DMI version
        }
    };

    aBiosHeaders.dmi.u16TablesLength = cbDmiTables;
    aBiosHeaders.dmi.u16TableEntries = cNumDmiTables;
    /* NB: The _SM_ table checksum technically covers both the _SM_ part (16 bytes) and the _DMI_ part
     * (further 15 bytes). However, because the _DMI_ checksum must be zero, the _SM_ checksum can
     * be calculated independently.
     */
    aBiosHeaders.smbios.u8Checksum   = fwCommonChecksum((uint8_t*)&aBiosHeaders.smbios, sizeof(aBiosHeaders.smbios));
    aBiosHeaders.dmi.u8Checksum      = fwCommonChecksum((uint8_t*)&aBiosHeaders.dmi,    sizeof(aBiosHeaders.dmi));

    memcpy(pHdr, &aBiosHeaders, sizeof(aBiosHeaders));
}

/**
 * Construct the MPS table for implanting as a ROM page.
 *
 * Only applicable if IOAPIC is active!
 *
 * See ``MultiProcessor Specification Version 1.4 (May 1997)'':
 *   ``1.3 Scope
 *     ...
 *     The hardware required to implement the MP specification is kept to a
 *     minimum, as follows:
 *     * One or more processors that are Intel architecture instruction set
 *       compatible, such as the CPUs in the Intel486 or Pentium processor
 *       family.
 *     * One or more APICs, such as the Intel 82489DX Advanced Programmable
 *       Interrupt Controller or the integrated APIC, such as that on the
 *       Intel Pentium 735\\90 and 815\\100 processors, together with a discrete
 *       I/O APIC unit.''
 * and later:
 *   ``4.3.3 I/O APIC Entries
 *     The configuration table contains one or more entries for I/O APICs.
 *     ...
 *     I/O APIC FLAGS: EN 3:0 1 If zero, this I/O APIC is unusable, and the
 *                              operating system should not attempt to access
 *                              this I/O APIC.
 *                              At least one I/O APIC must be enabled.''
 *
 * @param   pDevIns    The device instance data.
 * @param   pTable     Where to write the table.
 * @param   cbMax      The maximum size of the MPS table.
 * @param   cCpus      The number of guest CPUs.
 */
void FwCommonPlantMpsTable(PPDMDEVINS pDevIns, uint8_t *pTable, unsigned cbMax, uint16_t cCpus)
{
    RT_NOREF1(cbMax);

    /* configuration table */
    PMPSCFGTBLHEADER pCfgTab      = (MPSCFGTBLHEADER*)pTable;
    memcpy(pCfgTab->au8Signature, "PCMP", 4);
    pCfgTab->u8SpecRev             =  4;    /* 1.4 */
    memcpy(pCfgTab->au8OemId, "VBOXCPU ", 8);
    memcpy(pCfgTab->au8ProductId, "VirtualBox  ", 12);
    pCfgTab->u32OemTablePtr        =  0;
    pCfgTab->u16OemTableSize       =  0;
    pCfgTab->u16EntryCount         =  0;    /* Incremented as we go. */
    pCfgTab->u32AddrLocalApic      = 0xfee00000;
    pCfgTab->u16ExtTableLength     =  0;
    pCfgTab->u8ExtTableChecksum    =  0;
    pCfgTab->u8Reserved            =  0;

    uint32_t u32Eax, u32Ebx, u32Ecx, u32Edx;
    uint32_t u32CPUSignature = 0x0520; /* default: Pentium 100 */
    uint32_t u32FeatureFlags = 0x0001; /* default: FPU */
    PDMDevHlpGetCpuId(pDevIns, 0, &u32Eax, &u32Ebx, &u32Ecx, &u32Edx);
    if (u32Eax >= 1)
    {
        PDMDevHlpGetCpuId(pDevIns, 1, &u32Eax, &u32Ebx, &u32Ecx, &u32Edx);
        u32CPUSignature = u32Eax & 0xfff;
        /* Local APIC will be enabled later so override it here. Since we provide
         * an MP table we have an IOAPIC and therefore a Local APIC. */
        u32FeatureFlags = u32Edx | X86_CPUID_FEATURE_EDX_APIC;
    }
    /* Construct MPS table for each VCPU. */
    PMPSPROCENTRY pProcEntry = (PMPSPROCENTRY)(pCfgTab+1);
    for (int i = 0; i < cCpus; i++)
    {
        pProcEntry->u8EntryType        = 0; /* processor entry */
        pProcEntry->u8LocalApicId      = i;
        pProcEntry->u8LocalApicVersion = 0x14;
        pProcEntry->u8CPUFlags         = (i == 0 ? 2 /* bootstrap processor */ : 0 /* application processor */) | 1 /* enabled */;
        pProcEntry->u32CPUSignature    = u32CPUSignature;
        pProcEntry->u32CPUFeatureFlags = u32FeatureFlags;
        pProcEntry->u32Reserved[0]     =
        pProcEntry->u32Reserved[1]     = 0;
        pProcEntry++;
        pCfgTab->u16EntryCount++;
    }

    uint32_t iBusIdIsa  = 0;
    uint32_t iBusIdPci0 = 1;

    /* ISA bus */
    PMPSBUSENTRY pBusEntry         = (PMPSBUSENTRY)pProcEntry;
    pBusEntry->u8EntryType         = 1; /* bus entry */
    pBusEntry->u8BusId             = iBusIdIsa; /* this ID is referenced by the interrupt entries */
    memcpy(pBusEntry->au8BusTypeStr, "ISA   ", 6);
    pBusEntry++;
    pCfgTab->u16EntryCount++;

    /* PCI bus */
    pBusEntry->u8EntryType         = 1; /* bus entry */
    pBusEntry->u8BusId             = iBusIdPci0; /* this ID can be referenced by the interrupt entries */
    memcpy(pBusEntry->au8BusTypeStr, "PCI   ", 6);
    pBusEntry++;
    pCfgTab->u16EntryCount++;


    /* I/O-APIC.
     * MP spec: "The configuration table contains one or more entries for I/O APICs.
     *           ... At least one I/O APIC must be enabled." */
    PMPSIOAPICENTRY pIOAPICEntry   = (PMPSIOAPICENTRY)(pBusEntry);
    uint16_t iApicId = 0;
    pIOAPICEntry->u8EntryType      = 2; /* I/O-APIC entry */
    pIOAPICEntry->u8Id             = iApicId; /* this ID is referenced by the interrupt entries */
    pIOAPICEntry->u8Version        = 0x11;
    pIOAPICEntry->u8Flags          = 1 /* enable */;
    pIOAPICEntry->u32Addr          = 0xfec00000;
    pCfgTab->u16EntryCount++;

    /* Interrupt tables */
    /* Bus vectors */
    /* Note: The PIC is currently not routed to the I/O APIC. Therefore we skip
     * pin 0 on the I/O APIC.
     */
    PMPSIOIRQENTRY pIrqEntry       = (PMPSIOIRQENTRY)(pIOAPICEntry+1);
    for (int iPin = 1; iPin < 16; iPin++, pIrqEntry++)
    {
        pIrqEntry->u8EntryType     = 3; /* I/O interrupt entry */
        /*
         * 0 - INT, vectored interrupt,
         * 3 - ExtINT, vectored interrupt provided by PIC
         * As we emulate system with both APIC and PIC, it's needed for their coexistence.
         */
        pIrqEntry->u8Type          = (iPin == 0) ? 3 : 0;
        pIrqEntry->u16Flags        = 0;              /* polarity of APIC I/O input signal = conforms to bus,
                                                        trigger mode = conforms to bus */
        pIrqEntry->u8SrcBusId      = iBusIdIsa;      /* ISA bus */
        /* IRQ0 mapped to pin 2, other are identity mapped */
        /* If changing, also update PDMIsaSetIrq() and MADT */
        pIrqEntry->u8SrcBusIrq     = (iPin == 2) ? 0 : iPin; /* IRQ on the bus */
        pIrqEntry->u8DstIOAPICId   = iApicId;        /* destination IO-APIC */
        pIrqEntry->u8DstIOAPICInt  = iPin;           /* pin on destination IO-APIC */
        pCfgTab->u16EntryCount++;
    }
    /* Local delivery */
    pIrqEntry->u8EntryType     = 4; /* Local interrupt entry */
    pIrqEntry->u8Type          = 3; /* ExtINT */
    pIrqEntry->u16Flags        = (1 << 2) | 1; /* active-high, edge-triggered */
    pIrqEntry->u8SrcBusId      = iBusIdIsa;
    pIrqEntry->u8SrcBusIrq     = 0;
    pIrqEntry->u8DstIOAPICId   = 0xff;
    pIrqEntry->u8DstIOAPICInt  = 0;
    pIrqEntry++;
    pCfgTab->u16EntryCount++;
    pIrqEntry->u8EntryType     = 4; /* Local interrupt entry */
    pIrqEntry->u8Type          = 1; /* NMI */
    pIrqEntry->u16Flags        = (1 << 2) | 1; /* active-high, edge-triggered */
    pIrqEntry->u8SrcBusId      = iBusIdIsa;
    pIrqEntry->u8SrcBusIrq     = 0;
    pIrqEntry->u8DstIOAPICId   = 0xff;
    pIrqEntry->u8DstIOAPICInt  = 1;
    pIrqEntry++;
    pCfgTab->u16EntryCount++;

    pCfgTab->u16Length             = (uint8_t*)pIrqEntry - pTable;
    pCfgTab->u8Checksum            = fwCommonChecksum(pTable, pCfgTab->u16Length);

    AssertMsg(pCfgTab->u16Length < cbMax,
              ("VBOX_MPS_TABLE_SIZE=%d, maximum allowed size is %d",
              pCfgTab->u16Length, cbMax));
}

/**
 * Construct the MPS table pointer at VM construction and reset.
 *
 * Only applicable if IOAPIC is active!
 *
 * @param   pDevIns         The device instance data.
 * @param   u32MpTableAddr  The MP table physical address.
 */
void FwCommonPlantMpsFloatPtr(PPDMDEVINS pDevIns, uint32_t u32MpTableAddr)
{
    MPSFLOATPTR floatPtr;
    floatPtr.au8Signature[0]       = '_';
    floatPtr.au8Signature[1]       = 'M';
    floatPtr.au8Signature[2]       = 'P';
    floatPtr.au8Signature[3]       = '_';
    floatPtr.u32MPSAddr            = u32MpTableAddr;
    floatPtr.u8Length              = 1; /* structure size in paragraphs */
    floatPtr.u8SpecRev             = 4; /* MPS revision 1.4 */
    floatPtr.u8Checksum            = 0;
    floatPtr.au8Feature[0]         = 0;
    floatPtr.au8Feature[1]         = 0;
    floatPtr.au8Feature[2]         = 0;
    floatPtr.au8Feature[3]         = 0;
    floatPtr.au8Feature[4]         = 0;
    floatPtr.u8Checksum            = fwCommonChecksum((uint8_t*)&floatPtr, 16);
    PDMDevHlpPhysWrite(pDevIns, 0x9fff0, &floatPtr, 16);
}

