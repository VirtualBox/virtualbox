/* $Id: DevSmc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevSmc - Apple System Management Controller.
 *
 * The SMC is controlling power, fans, take measurements (voltage, temperature,
 * fan speed, ++), and lock Mac OS X to Apple hardware.  For more details see:
 *      - http://en.wikipedia.org/wiki/System_Management_Controller
 *      - http://www.parhelia.ch/blog/statics/k3_keys.html
 *      - http://www.nosuchcon.org/talks/D1_02_Alex_Ninjas_and_Harry_Potter.pdf
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_SMC
#include <VBox/vmm/pdmdev.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#if defined(IN_RING0) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))
# include <iprt/asm-amd64-x86.h>
# include <iprt/once.h>
#endif
#if defined(RT_OS_DARWIN) && defined(IN_RING3) && !defined(VBOX_DEVICE_STRUCT_TESTCASE) /* drags in bad page size define */
# include "IOKit/IOKitLib.h"
#endif

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The current version of the saved state. */
#define SMC_SAVED_STATE_VERSION                 1 /** @todo later 2 */
/** Empty saved state version. */
#define SMC_SAVED_STATE_VERSION_BAKA            1

/** The ring-0 operation number that attempts to get OSK0 and OSK1 from the real
 *  SMC. */
#define SMC_CALLR0_READ_OSK                     1


/** @name Apple SMC port and register definitions.
 * @{ */

/** The first Apple SMC port. */
#define SMC_PORT_FIRST                          0x0300
/** The number of registers (also ports). */
#define SMC_REG_COUNT                           0x0020

/** The data register. */
#define SMC_REG_DATA                            0x00
#define SMC_PORT_DATA                           (SMC_PORT_FIRST + SMC_REG_DATA)

/** The command register. */
#define SMC_REG_CMD                             0x04
#define SMC_PORT_CMD                            (SMC_PORT_FIRST + SMC_REG_CMD)

/** Status code register. */
#define SMC_REG_STATUS_CODE                     0x1e
#define SMC_PORT_STATUS_CODE                    (SMC_PORT_FIRST + SMC_REG_STATUS_CODE)
/** @} */

/** @name Apple SMC Commands.
 *  @{ */
#define SMC_CMD_GET_KEY_VALUE                   0x10
#define SMC_CMD_PUT_KEY                         0x11
#define SMC_CMD_GET_KEY_BY_INDEX                0x12
#define SMC_CMD_GET_KEY_INFO                    0x13
/** @} */

/** @name Apple SMC Status Codes.
 * @{ */
#define SMC_STATUS_CD_SUCCESS                   UINT8_C(0x00)
#define SMC_STATUS_CD_COMM_COLLISION            UINT8_C(0x80)
#define SMC_STATUS_CD_SPURIOUS_DATA             UINT8_C(0x81)
#define SMC_STATUS_CD_BAD_COMMAND               UINT8_C(0x82)
#define SMC_STATUS_CD_BAD_PARAMETER             UINT8_C(0x83)
#define SMC_STATUS_CD_KEY_NOT_FOUND             UINT8_C(0x84)
#define SMC_STATUS_CD_KEY_NOT_READABLE          UINT8_C(0x85)
#define SMC_STATUS_CD_KEY_NOT_WRITABLE          UINT8_C(0x86)
#define SMC_STATUS_CD_KEY_SIZE_MISMATCH         UINT8_C(0x87)
#define SMC_STATUS_CD_FRAMING_ERROR             UINT8_C(0x88)
#define SMC_STATUS_CD_BAD_ARGUMENT_ERROR        UINT8_C(0x89)
#define SMC_STATUS_CD_TIMEOUT_ERROR             UINT8_C(0xb7)
#define SMC_STATUS_CD_KEY_INDEX_RANGE_ERROR     UINT8_C(0xb8)
#define SMC_STATUS_CD_BAD_FUNC_PARAMETER        UINT8_C(0xc0)
#define SMC_STATUS_CD_EVENT_BUFF_WRONG_ORDER    UINT8_C(0x??)
#define SMC_STATUS_CD_EVENT_BUFF_READ_ERROR     UINT8_C(0x??)
#define SMC_STATUS_CD_DEVICE_ACCESS_ERROR       UINT8_C(0xc7)
#define SMC_STATUS_CD_UNSUPPORTED_FEATURE       UINT8_C(0xcb)
#define SMC_STATUS_CD_SMB_ACCESS_ERROR          UINT8_C(0xcc)
/** @} */

/** @name Apple SMC Key Attributes.
 * @{ */
#define SMC_KEY_ATTR_PRIVATE                    UINT8_C(0x01)
#define SMC_KEY_ATTR_UKN_0x02                   UINT8_C(0x02)
#define SMC_KEY_ATTR_UKN_0x04                   UINT8_C(0x04)
#define SMC_KEY_ATTR_CONST                      UINT8_C(0x08)
#define SMC_KEY_ATTR_FUNCTION                   UINT8_C(0x10)
#define SMC_KEY_ATTR_UKN_0x20                   UINT8_C(0x20)
#define SMC_KEY_ATTR_WRITE                      UINT8_C(0x40)
#define SMC_KEY_ATTR_READ                       UINT8_C(0x80)
/** @} */


/** The index of the first enumerable key in g_aSmcKeys. */
#define SMC_KEYIDX_FIRST_ENUM                   2

/** Macro for emitting a static DEVSMC4CHID initializer. */
#define SMC4CH(ch1, ch2, ch3, ch4) { { ch1, ch2, ch3, ch4 } }

/**
 * Macro for comparing DEVSMC4CHID with a string value.
 * @returns true if equal, false if not.
 */
#define SMC4CH_EQ(a_pSmcKey, a_sz4) ( (a_pSmcKey)->u32 == RT_MAKE_U32_FROM_U8(a_sz4[0], a_sz4[1], a_sz4[2], a_sz4[3]) )

/** Indicates the we want a 2.x SMC. */
#define VBOX_WITH_SMC_2_x


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * 4 char identifier
 */
typedef union DEVSMC4CHID
{
    /** Byte view. */
    uint8_t         ab[4];
    /** 32-bit unsigned integer view. */
    uint32_t        u32;
} DEVSMC4CHID;


/**
 * Current key data area for communicating with the guest.
 */
typedef struct DEVSMCCURKEY
{
    /** The key. */
    DEVSMC4CHID         Key;
    /** The data type. */
    DEVSMC4CHID         Type;
    /** Key attributes. */
    uint8_t             fAttr;
    /** The value length. */
    uint8_t             cbValue;
    uint8_t             abAlignment[2];
    /**
     * The value union.  32 bytes is probably sufficient here, but we provide a
     * little more room since it doesn't cost us anything. */
    union
    {
        /** Byte view. */
        uint8_t         ab[128];
        /** 16-bit view. */
        uint16_t        u16;
        /** 32-bit view. */
        uint32_t        u32;
    } Value;
} DEVSMCCURKEY;
AssertCompileSize(DEVSMCCURKEY, 128+12);
/** Pointer to the current key buffer. */
typedef DEVSMCCURKEY *PDEVSMCCURKEY;
/** Const pointer to the current key buffer. */
typedef DEVSMCCURKEY const *PCDEVSMCCURKEY;


/**
 * The device
 */
typedef struct DEVSMC
{
    /** The current command (SMC_PORT_CMD write). */
    uint8_t             bCmd;
    /** Current key offset. */
    uint8_t             offKey;
    /** Current value offset. */
    uint8_t             offValue;
    /** Number of keys in the aKeys array. */
    uint8_t             cKeys;

    /** The current key data the user is accessing. */
    DEVSMCCURKEY        CurKey;

    /**
     * Generic read/write register values.
     *
     * The DATA register entry is not used at all.  The CMD register entry contains
     * the state value.
     */
    union
    {
        /** Index register view. */
        uint8_t             abRegsRW[SMC_REG_COUNT];
        /** Named register view. */
        struct
        {
            uint8_t         abUnknown0[0x04];
            /** The current state (SMC_PORT_CMD read). */
            uint8_t         bState;
            uint8_t         abUnknown1[0x1e - 0x05];
            /** The current status code (SMC_PORT_STATUS_CODE). */
            uint8_t         bStatusCode;
            uint8_t         abUnknown2[1];
        } s;
    } u;

    /** @name Key data.
     * @{ */
    /** OSK0 and OSK1. */
    char                    szOsk0And1[64+1];
    /** $Num - unknown function. */
    uint8_t                 bDollaryNumber;
    /** MSSD - shutdown reason. */
    uint8_t                 bShutdownReason;
    /** NATJ - Ninja action timer job. */
    uint8_t                 bNinjaActionTimerJob;
    /** @} */

    /** The I/O port registration handle. */
    IOMIOPORTHANDLE         hIoPorts;
} DEVSMC;
#ifndef _MSC_VER
AssertCompileMembersAtSameOffset(DEVSMC, u.abRegsRW[SMC_REG_CMD],         DEVSMC, u.s.bState);
AssertCompileMembersAtSameOffset(DEVSMC, u.abRegsRW[SMC_REG_STATUS_CODE], DEVSMC, u.s.bStatusCode);
#endif

/** Pointer to the SMC state. */
typedef DEVSMC *PDEVSMC;

#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/**
 * Method for retriving the key value and/or optionally also attributes.
 *
 * @returns Apple SMC Status Code.
 * @param   pThis           The SMC instance data.
 * @param   pCurKey         The current key structure (input / output).
 * @param   bCmd            The current command (mostly for getters that also
 *                          provides attributes or type info).
 * @param   pKeyDesc        Pointer to the key descriptor so that the getter can
 *                          service more than once key.
 */
typedef DECLCALLBACKTYPE(uint8_t, FNDEVSMCKEYGETTER,(PDEVSMC pThis, PDEVSMCCURKEY pCurKey, uint8_t bCmd,
                                                     struct DEVSMCKEYDESC const *pKeyDesc));

/**
 * Method for setting the key value.
 *
 * @returns Apple SMC Status Code.
 * @param   pThis           The SMC instance data.
 * @param   pCurKey         The current key structure (input / output).
 * @param   bCmd            The current command (currently not relevant).
 * @param   pKeyDesc        Pointer to the key descriptor so that the getter can
 *                          service more than once key.
 */
typedef DECLCALLBACKTYPE(uint8_t, FNDEVSMCKEYPUTTER,(PDEVSMC pThis, PCDEVSMCCURKEY pCurKey, uint8_t bCmd,
                                                     struct DEVSMCKEYDESC const *pKeyDesc));

/**
 * Key descriptor.
 */
typedef struct DEVSMCKEYDESC
{
    /** The key 4 character identifier. */
    DEVSMC4CHID         Key;
    /** Type 4 character identifier.  0 means the getter will set it dynamically. */
    DEVSMC4CHID         Type;
    /** Getter method, see FNDEVSMCKEYGETTER. */
    FNDEVSMCKEYGETTER  *pfnGet;
    /** Putter method, see FNDEVSMCKEYPUTTER. */
    FNDEVSMCKEYPUTTER  *pfnPut;
    /** The keyvalue size.  If 0 the pfnGet/pfnPut will define/check the size. */
    uint8_t             cbValue;
    /** Attributes.  0 means the getter will set it dynamically. */
    uint8_t             fAttr;
} DEVSMCKEYDESC;
/** Pointer to a constant SMC key descriptor. */
typedef DEVSMCKEYDESC const *PCDEVSMCKEYDESC;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static FNDEVSMCKEYGETTER scmKeyGetOSKs;
static FNDEVSMCKEYGETTER scmKeyGetKeyCount;
static FNDEVSMCKEYGETTER scmKeyGetRevision;
#ifdef VBOX_WITH_SMC_2_x
static FNDEVSMCKEYGETTER scmKeyGetDollarAddress;
static FNDEVSMCKEYGETTER scmKeyGetDollarNumber;
static FNDEVSMCKEYPUTTER scmKeyPutDollarNumber;
#endif
static FNDEVSMCKEYGETTER scmKeyGetShutdownReason;
static FNDEVSMCKEYPUTTER scmKeyPutShutdownReason;
static FNDEVSMCKEYGETTER scmKeyGetNinjaTimerAction;
static FNDEVSMCKEYPUTTER scmKeyPutNinjaTimerAction;
static FNDEVSMCKEYGETTER scmKeyGetOne;
static FNDEVSMCKEYGETTER scmKeyGetZero;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Apple SMC key descriptor table.
 */
static const DEVSMCKEYDESC g_aSmcKeys[] =
{
    /* Non-enum keys first. */
    { SMC4CH('O','S','K','0'), SMC4CH('c','h','8','*'), scmKeyGetOSKs, NULL, 32, SMC_KEY_ATTR_READ | SMC_KEY_ATTR_FUNCTION },
    { SMC4CH('O','S','K','1'), SMC4CH('c','h','8','*'), scmKeyGetOSKs, NULL, 32, SMC_KEY_ATTR_READ | SMC_KEY_ATTR_FUNCTION },

    /* The first enum key is the #KEY value. */
    { SMC4CH('#','K','E','Y'), SMC4CH('u','i','3','2'), scmKeyGetKeyCount,            NULL,                       4, SMC_KEY_ATTR_READ },
# ifdef VBOX_WITH_SMC_2_x
    { SMC4CH('$','A','d','r'), SMC4CH('u','i','3','2'), scmKeyGetDollarAddress,       NULL,                       4, SMC_KEY_ATTR_READ },
    { SMC4CH('$','N','u','m'), SMC4CH('u','i','8',' '), scmKeyGetDollarNumber,        scmKeyPutDollarNumber,      1, SMC_KEY_ATTR_READ | SMC_KEY_ATTR_WRITE | SMC_KEY_ATTR_PRIVATE },
    { SMC4CH('B','E','M','B'), SMC4CH('f','l','a','g'), scmKeyGetOne,                 NULL,                       1, SMC_KEY_ATTR_READ },
# else
    { SMC4CH('L','S','O','F'), SMC4CH('f','l','a','g'), scmKeyGetZero,                NULL,                       1, SMC_KEY_ATTR_READ },
# endif
    { SMC4CH('M','S','S','D'), SMC4CH('s','i','8',' '), scmKeyGetShutdownReason,      scmKeyPutShutdownReason,    1, SMC_KEY_ATTR_READ | SMC_KEY_ATTR_WRITE | SMC_KEY_ATTR_PRIVATE },
    /* MSDS is not present on MacPro3,1 nor MacBookPro10,1, so returning not found is fine. */
# ifdef VBOX_WITH_SMC_2_x
    { SMC4CH('M','S','T','f'), SMC4CH('u','i','8',' '), scmKeyGetZero,                NULL,                       1, SMC_KEY_ATTR_READ },
# endif
    { SMC4CH('N','A','T','J'), SMC4CH('u','i','8',' '), scmKeyGetNinjaTimerAction,    scmKeyPutNinjaTimerAction,  1, SMC_KEY_ATTR_READ | SMC_KEY_ATTR_WRITE | SMC_KEY_ATTR_PRIVATE },
    { SMC4CH('R','E','V',' '), SMC4CH('{','r','e','v'), scmKeyGetRevision,            NULL,                       6, SMC_KEY_ATTR_READ },
/** @todo MSSP, NTOK and more. */
};

#if defined(IN_RING0) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))

/** Do once for the SMC ring-0 static data (g_abOsk0And1, g_fHaveOsk). */
static RTONCE   g_SmcR0Once = RTONCE_INITIALIZER;
/** Indicates whether we've successfully queried the OSK* keys. */
static bool     g_fHaveOsk = false;
/** The OSK0 and OSK1 values. */
static uint8_t  g_abOsk0And1[32+32];


/**
 * Waits for the specified state on the host SMC.
 *
 * @returns success indicator.
 * @param   bState              The desired state.
 * @param   pszWhat             What we're currently doing. For the log.
 */
static bool devR0SmcWaitHostState(uint8_t bState, const char *pszWhat)
{
    uint8_t bCurState = 0; /* (MSC is potentially uninitialized) */
    for (uint32_t cMsSleep = 1; cMsSleep <= 64; cMsSleep <<= 1)
    {
        RTThreadSleep(cMsSleep);
        bCurState = ASMInU16(SMC_PORT_CMD);
        if ((bCurState & 0xf) == bState)
            return true;
    }

    LogRel(("devR0Smc: %s: bCurState=%#x, wanted %#x.\n", pszWhat, bCurState, bState));
#if 0
    uint8_t  bCurStatus2 = ASMInU8(SMC_PORT_STATUS_CODE);
    uint8_t  bCurStatus3 = ASMInU8(SMC_PORT_STATUS_CODE);
    uint16_t wCurStatus3 = ASMInU16(SMC_PORT_STATUS_CODE);
    uint32_t dwCurStatus3 = ASMInU32(SMC_PORT_STATUS_CODE);
    LogRel(("SMC: status2=%#x status3=%#x w=%#x dw=%#x\n", bCurStatus2, bCurStatus3, wCurStatus3, dwCurStatus3));
#endif
    return false;
}


/**
 * Reads a key by name from the host SMC.
 *
 * @returns success indicator.
 * @param   pszName             The key name, must be exactly 4 chars long.
 * @param   pbBuf               The output buffer.
 * @param   cbBuf               The buffer size. Max 32 bytes.
 */
static bool devR0SmcQueryHostKey(const char *pszName, uint8_t *pbBuf, size_t cbBuf)
{
    Assert(strlen(pszName) == 4);
    Assert(cbBuf <= 32);
    Assert(cbBuf > 0);

    /*
     * Issue the READ command.
     */
    uint32_t cMsSleep = 1;
    for (;;)
    {
        ASMOutU32(SMC_PORT_CMD, SMC_CMD_GET_KEY_VALUE);
        RTThreadSleep(cMsSleep);
        uint8_t bCurState = ASMInU8(SMC_PORT_CMD);
        if ((bCurState & 0xf) == 0xc)
            break;
        cMsSleep <<= 1;
        if (cMsSleep > 64)
        {
            LogRel(("devR0Smc: %s: bCurState=%#x, wanted %#x.\n", "cmd", bCurState, 0xc));
            return false;
        }
    }

    /*
     * Send it the key.
     */
    for (unsigned off = 0; off < 4; off++)
    {
        ASMOutU8(SMC_PORT_DATA, pszName[off]);
        if (!devR0SmcWaitHostState(4, "key"))
            return false;
    }

    /*
     * The desired amount of output.
     */
    ASMOutU8(SMC_PORT_DATA, (uint8_t)cbBuf);

    /*
     * Read the output.
     */
    for (size_t off = 0; off < cbBuf; off++)
    {
        if (!devR0SmcWaitHostState(5, off ? "data" : "len"))
            return false;
        pbBuf[off] = ASMInU8(SMC_PORT_DATA);
    }

    LogRel(("SMC: pbBuf=%.*s\n", cbBuf, pbBuf));
    return true;
}


/**
 * RTOnce callback that initializes g_fHaveOsk and g_abOsk0And1.
 *
 * @returns VINF_SUCCESS.
 * @param   pvUserIgnored     Ignored.
 */
static DECLCALLBACK(int) devR0SmcInitOnce(void *pvUserIgnored)
{
    g_fHaveOsk = devR0SmcQueryHostKey("OSK0", &g_abOsk0And1[0],  32)
              && devR0SmcQueryHostKey("OSK1", &g_abOsk0And1[32], 32);

#if 0
    /*
     * Dump the device registers.
     */
    for (uint16_t uPort = 0x300; uPort < 0x320; uPort ++)
        LogRel(("SMC: %#06x=%#010x w={%#06x, %#06x}, b={%#04x %#04x %#04x %#04x}\n", uPort,
                ASMInU32(uPort), ASMInU16(uPort), ASMInU16(uPort + 2),
                ASMInU8(uPort), ASMInU8(uPort + 1), ASMInU8(uPort +2), ASMInU8(uPort + 3) ));
#endif

    NOREF(pvUserIgnored);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREGR0,pfnRequest}
 */
static DECLCALLBACK(int) devR0SmcReqHandler(PPDMDEVINS pDevIns, uint32_t uReq, uint64_t uArg)
{
    PDEVSMC pThis = PDMDEVINS_2_DATA(pDevIns, PDEVSMC);
    int     rc    = VERR_INVALID_FUNCTION;
    RT_NOREF_PV(uArg);

    if (uReq == SMC_CALLR0_READ_OSK)
    {
        rc = RTOnce(&g_SmcR0Once, devR0SmcInitOnce, NULL);
        if (   RT_SUCCESS(rc)
            && g_fHaveOsk)
        {
            AssertCompile(sizeof(g_abOsk0And1) + 1 == sizeof(pThis->szOsk0And1));
            memcpy(pThis->szOsk0And1, g_abOsk0And1, sizeof(pThis->szOsk0And1) - 1);
            pThis->szOsk0And1[sizeof(pThis->szOsk0And1) - 1] = '\0';
        }
    }
    return rc;
}

#endif /* IN_RING0 && (AMD64 || X86) */

#if defined(IN_RING3) && defined(RT_OS_DARWIN)

/**
 * Preferred method to retrieve the SMC key.
 *
 * @param   pabKey  where to store the key.
 * @param   cbKey   size of the buffer.
 */
static int getSmcKeyOs(char *pabKey, uint32_t cbKey)
{
    /*
     * Method as described in Amit Singh's article:
     *   http://osxbook.com/book/bonus/chapter7/tpmdrmmyth/
     */
    typedef struct
    {
        uint32_t   key;
        uint8_t    pad0[22];
        uint32_t   datasize;
        uint8_t    pad1[10];
        uint8_t    cmd;
        uint32_t   pad2;
        uint8_t    data[32];
    } AppleSMCBuffer;

    AssertReturn(cbKey >= 65, VERR_INTERNAL_ERROR);

    RT_GCC_NO_WARN_DEPRECATED_BEGIN /* kIOMasterPortDefault: Deprecated since 12.0. */
    io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                       IOServiceMatching("AppleSMC"));
    RT_GCC_NO_WARN_DEPRECATED_END
    if (!service)
        return VERR_NOT_FOUND;

    io_connect_t    port = (io_connect_t)0;
    kern_return_t   kr   = IOServiceOpen(service, mach_task_self(), 0, &port);
    IOObjectRelease(service);

    if (kr != kIOReturnSuccess)
        return RTErrConvertFromDarwin(kr);

    AppleSMCBuffer  inputStruct    = { 0, {0}, 32, {0}, 5, };
    AppleSMCBuffer  outputStruct;
    size_t          cbOutputStruct = sizeof(outputStruct);

    for (int i = 0; i < 2; i++)
    {
        inputStruct.key = i == 0 ? RT_MAKE_U32_FROM_MSB_U8('O','S','K','0') : RT_MAKE_U32_FROM_MSB_U8('O','S','K','1');
        kr = IOConnectCallStructMethod((mach_port_t)port,
                                       (uint32_t)2,
                                       (const void *)&inputStruct,
                                       sizeof(inputStruct),
                                       (void *)&outputStruct,
                                       &cbOutputStruct);
        if (kr != kIOReturnSuccess)
        {
            IOServiceClose(port);
            return RTErrConvertFromDarwin(kr);
        }

        for (int j = 0; j < 32; j++)
            pabKey[j + i*32] = outputStruct.data[j];
    }

    IOServiceClose(port);

    pabKey[64] = 0;

    return VINF_SUCCESS;
}

#endif /* IN_RING3 && RT_OS_DARWIN */


/** @callback_method_impl{FNDEVSMCKEYGETTER, OSK0 and OSK1} */
static DECLCALLBACK(uint8_t) scmKeyGetOSKs(PDEVSMC pThis, PDEVSMCCURKEY pCurKey, uint8_t bCmd, PCDEVSMCKEYDESC pKeyDesc)
{
    RT_NOREF1(bCmd);
    Assert(SMC4CH_EQ(&pKeyDesc->Key, "OSK0") || SMC4CH_EQ(&pKeyDesc->Key, "OSK1"));
    const char *pszSrc = pThis->szOsk0And1;
    if (SMC4CH_EQ(&pKeyDesc->Key, "OSK1"))
        pszSrc += 32;
    memcpy(pCurKey->Value.ab, pszSrc, 32);
    return SMC_STATUS_CD_SUCCESS;
}


/** @callback_method_impl{FNDEVSMCKEYGETTER, \#KEY} */
static DECLCALLBACK(uint8_t) scmKeyGetKeyCount(PDEVSMC pThis, PDEVSMCCURKEY pCurKey, uint8_t bCmd, PCDEVSMCKEYDESC pKeyDesc)
{
    RT_NOREF3(pThis, bCmd, pKeyDesc);
    Assert(pKeyDesc == &g_aSmcKeys[SMC_KEYIDX_FIRST_ENUM]);
    uint32_t cKeys = RT_ELEMENTS(g_aSmcKeys) - SMC_KEYIDX_FIRST_ENUM;
    pCurKey->Value.u32 = RT_H2BE_U32(cKeys);
    return SMC_STATUS_CD_SUCCESS;
}


/** @callback_method_impl{FNDEVSMCKEYGETTER, REV - Source revision.} */
static DECLCALLBACK(uint8_t) scmKeyGetRevision(PDEVSMC pThis, PDEVSMCCURKEY pCurKey, uint8_t bCmd, PCDEVSMCKEYDESC pKeyDesc)
{
    RT_NOREF3(pThis, bCmd, pKeyDesc);
#ifdef VBOX_WITH_SMC_2_x
    pCurKey->Value.ab[0] = 0x02;
    pCurKey->Value.ab[1] = 0x03;
    pCurKey->Value.ab[2] = 0x0f;
    pCurKey->Value.ab[3] = 0x00;
    pCurKey->Value.ab[4] = 0x00;
    pCurKey->Value.ab[5] = 0x35;
#else
    pCurKey->Value.ab[0] = 0x01;
    pCurKey->Value.ab[1] = 0x25;
    pCurKey->Value.ab[2] = 0x0f;
    pCurKey->Value.ab[3] = 0x00;
    pCurKey->Value.ab[4] = 0x00;
    pCurKey->Value.ab[5] = 0x04;
#endif
    return SMC_STATUS_CD_SUCCESS;
}

#ifdef VBOX_WITH_SMC_2_x

/** @callback_method_impl{FNDEVSMCKEYGETTER, $Adr - SMC address.} */
static DECLCALLBACK(uint8_t) scmKeyGetDollarAddress(PDEVSMC pThis, PDEVSMCCURKEY pCurKey, uint8_t bCmd, PCDEVSMCKEYDESC pKeyDesc)
{
    RT_NOREF3(pThis, bCmd, pKeyDesc);
    pCurKey->Value.u32 = RT_H2BE_U32(SMC_PORT_FIRST);
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNDEVSMCKEYGETTER, $Num - Some kind of number.} */
static DECLCALLBACK(uint8_t) scmKeyGetDollarNumber(PDEVSMC pThis, PDEVSMCCURKEY pCurKey, uint8_t bCmd, PCDEVSMCKEYDESC pKeyDesc)
{
    RT_NOREF2(bCmd, pKeyDesc);
    pCurKey->Value.ab[0] = pThis->bDollaryNumber;
    return VINF_SUCCESS;
}

/** @callback_method_impl{FNDEVSMCKEYPUTTER, $Num - Some kind of number.} */
static DECLCALLBACK(uint8_t) scmKeyPutDollarNumber(PDEVSMC pThis, PCDEVSMCCURKEY pCurKey, uint8_t bCmd, PCDEVSMCKEYDESC pKeyDesc)
{
    RT_NOREF2(bCmd, pKeyDesc);
    Log(("scmKeyPutDollarNumber: %#x -> %#x\n", pThis->bDollaryNumber, pCurKey->Value.ab[0]));
    pThis->bDollaryNumber = pCurKey->Value.ab[0];
    return VINF_SUCCESS;
}

#endif /* VBOX_WITH_SMC_2_x */

/** @callback_method_impl{FNDEVSMCKEYGETTER, MSSD - Machine Shutdown reason.} */
static DECLCALLBACK(uint8_t) scmKeyGetShutdownReason(PDEVSMC pThis, PDEVSMCCURKEY pCurKey, uint8_t bCmd, PCDEVSMCKEYDESC pKeyDesc)
{
    RT_NOREF2(bCmd, pKeyDesc);
    pCurKey->Value.ab[0] = pThis->bShutdownReason;
    return SMC_STATUS_CD_SUCCESS;
}


/** @callback_method_impl{FNDEVSMCKEYPUTTER, MSSD - Machine Shutdown reason.} */
static DECLCALLBACK(uint8_t) scmKeyPutShutdownReason(PDEVSMC pThis, PCDEVSMCCURKEY pCurKey, uint8_t bCmd, PCDEVSMCKEYDESC pKeyDesc)
{
    RT_NOREF2(bCmd, pKeyDesc);
    Log(("scmKeyPutShutdownReason: %#x -> %#x\n", pThis->bShutdownReason, pCurKey->Value.ab[0]));
    pThis->bShutdownReason = pCurKey->Value.ab[0];
    return SMC_STATUS_CD_SUCCESS;
}


/** @callback_method_impl{FNDEVSMCKEYGETTER, MSSD - Ninja timer action job.} */
static DECLCALLBACK(uint8_t)
scmKeyGetNinjaTimerAction(PDEVSMC pThis, PDEVSMCCURKEY pCurKey, uint8_t bCmd, PCDEVSMCKEYDESC pKeyDesc)
{
    RT_NOREF2(bCmd, pKeyDesc);
    pCurKey->Value.ab[0] = pThis->bNinjaActionTimerJob;
    return SMC_STATUS_CD_SUCCESS;
}


/** @callback_method_impl{FNDEVSMCKEYPUTTER, NATJ - Ninja timer action job.} */
static DECLCALLBACK(uint8_t)
scmKeyPutNinjaTimerAction(PDEVSMC pThis, PCDEVSMCCURKEY pCurKey, uint8_t bCmd, PCDEVSMCKEYDESC pKeyDesc)
{
    RT_NOREF2(bCmd, pKeyDesc);
    Log(("scmKeyPutNinjaTimerAction: %#x -> %#x\n", pThis->bNinjaActionTimerJob, pCurKey->Value.ab[0]));
    pThis->bNinjaActionTimerJob = pCurKey->Value.ab[0];
    return SMC_STATUS_CD_SUCCESS;
}

#ifdef VBOX_WITH_SMC_2_x

/** @callback_method_impl{FNDEVSMCKEYGETTER, Generic one getter.} */
static DECLCALLBACK(uint8_t) scmKeyGetOne(PDEVSMC pThis, PDEVSMCCURKEY pCurKey, uint8_t bCmd, PCDEVSMCKEYDESC pKeyDesc)
{
    RT_NOREF2(pThis, bCmd);
    memset(&pCurKey->Value.ab[0], 0, pKeyDesc->cbValue);
    pCurKey->Value.ab[pKeyDesc->cbValue - 1] = 1;
    return SMC_STATUS_CD_SUCCESS;
}

#endif /* VBOX_WITH_SMC_2_x */

/** @callback_method_impl{FNDEVSMCKEYGETTER, Generic zero getter.} */
static DECLCALLBACK(uint8_t) scmKeyGetZero(PDEVSMC pThis, PDEVSMCCURKEY pCurKey, uint8_t bCmd, PCDEVSMCKEYDESC pKeyDesc)
{
    RT_NOREF2(pThis, bCmd);
    memset(&pCurKey->Value.ab[0], 0, pKeyDesc->cbValue);
    return SMC_STATUS_CD_SUCCESS;
}


/**
 * Looks up a key and copies its value and attributes into the CurKey.
 *
 * @returns Key index on success, UINT32_MAX on failure.
 * @param   uKeyValue   The key value (DEVSMC4CHID.u32).
 */
static uint32_t smcKeyLookup(uint32_t uKeyValue)
{
    uint32_t iKey = RT_ELEMENTS(g_aSmcKeys);
    while (iKey-- > 0)
        if (g_aSmcKeys[iKey].Key.u32 == uKeyValue)
            return iKey;
    return UINT32_MAX;
}


/**
 * Looks up a key and copies its value and attributes into the CurKey.
 *
 * @returns Apple SMC Status Code.
 * @param   pThis       The SMC instance data.
 */
static uint8_t smcKeyGetByName(PDEVSMC pThis)
{
    uint8_t  bRc;
#ifdef LOG_ENABLED
    uint32_t const uKeyValueLog = RT_H2LE_U32(pThis->CurKey.Key.u32);
#endif
    uint32_t iKey = smcKeyLookup(pThis->CurKey.Key.u32);
    if (iKey != UINT32_MAX)
    {
        if (   g_aSmcKeys[iKey].cbValue == pThis->CurKey.cbValue
            || !g_aSmcKeys[iKey].cbValue)
        {
            pThis->CurKey.Type  = g_aSmcKeys[iKey].Type;
            pThis->CurKey.fAttr = g_aSmcKeys[iKey].fAttr;
            RT_ZERO(pThis->CurKey.Value);
            if (g_aSmcKeys[iKey].pfnGet)
            {
                bRc = g_aSmcKeys[iKey].pfnGet(pThis, &pThis->CurKey, pThis->bCmd, &g_aSmcKeys[iKey]);
                if (bRc == SMC_STATUS_CD_SUCCESS)
                {
                    LogFlow(("smcKeyGetByName: key=%4.4s value=%.*Rhxs\n",
                             &uKeyValueLog, pThis->CurKey.cbValue, &pThis->CurKey.Value));
                    return SMC_STATUS_CD_SUCCESS;
                }

                Log(("smcKeyGetByName: key=%4.4s getter failed! bRc=%#x\n", &uKeyValueLog, bRc));
            }
            else
            {
                Log(("smcKeyGetByName: key=%4.4s is not readable!\n", &uKeyValueLog));
                bRc = SMC_STATUS_CD_KEY_NOT_READABLE;
            }
        }
        else
        {
            Log(("smcKeyGetByName: Wrong value size; user=%#x smc=%#x key=%4.4s !\n",
                 pThis->CurKey.cbValue, g_aSmcKeys[iKey].cbValue, &uKeyValueLog));
            bRc = SMC_STATUS_CD_KEY_SIZE_MISMATCH;
        }
    }
    else
    {
        Log(("smcKeyGetByName: Key not found! key=%4.4s size=%#x\n", &uKeyValueLog, pThis->CurKey.cbValue));
        bRc = SMC_STATUS_CD_KEY_NOT_FOUND;
    }

    RT_ZERO(pThis->CurKey);
    return bRc;
}


/**
 * Looks up a key by index and copies its name (and attributes) into the CurKey.
 *
 * @returns Apple SMC Status Code.
 * @param   pThis       The SMC instance data.
 */
static uint8_t smcKeyGetByIndex(PDEVSMC pThis)
{
    uint8_t  bRc;
    uint32_t iKey = RT_BE2H_U32(pThis->CurKey.Key.u32);
    if (iKey < RT_ELEMENTS(g_aSmcKeys) - SMC_KEYIDX_FIRST_ENUM)
    {
        pThis->CurKey.Key     = g_aSmcKeys[iKey].Key;
        pThis->CurKey.Type    = g_aSmcKeys[iKey].Type;
        pThis->CurKey.fAttr   = g_aSmcKeys[iKey].fAttr;
        pThis->CurKey.cbValue = g_aSmcKeys[iKey].cbValue;
        RT_ZERO(pThis->CurKey.Value);
        Log(("smcKeyGetByIndex: %#x -> %c%c%c%c\n", iKey,
             pThis->CurKey.Key.ab[3], pThis->CurKey.Key.ab[2], pThis->CurKey.Key.ab[1], pThis->CurKey.Key.ab[0]));
        bRc = SMC_STATUS_CD_SUCCESS;
    }
    else
    {
        Log(("smcKeyGetByIndex: Key out or range: %#x, max %#x\n", iKey, RT_ELEMENTS(g_aSmcKeys) - SMC_KEYIDX_FIRST_ENUM));
        bRc = SMC_STATUS_CD_KEY_NOT_FOUND;
    }
    return bRc;
}


/**
 * Looks up a key by index and copies its attributes into the CurKey.
 *
 * @returns Apple SMC Status Code.
 * @param   pThis       The SMC instance data.
 */
static uint8_t smcKeyGetAttrByName(PDEVSMC pThis)
{
    uint8_t  bRc;
#ifdef LOG_ENABLED
    uint32_t const uKeyValueLog = RT_H2LE_U32(pThis->CurKey.Key.u32);
#endif
    uint32_t iKey = smcKeyLookup(pThis->CurKey.Key.u32);
    if (iKey != UINT32_MAX)
    {
        pThis->CurKey.Type    = g_aSmcKeys[iKey].Type;
        pThis->CurKey.fAttr   = g_aSmcKeys[iKey].fAttr;
        pThis->CurKey.cbValue = g_aSmcKeys[iKey].cbValue;
        RT_ZERO(pThis->CurKey.Value);
        if (g_aSmcKeys[iKey].cbValue)
            bRc = SMC_STATUS_CD_SUCCESS;
        else
            bRc = g_aSmcKeys[iKey].pfnGet(pThis, &pThis->CurKey, pThis->bCmd, &g_aSmcKeys[iKey]);
        if (bRc == SMC_STATUS_CD_SUCCESS)
        {
            LogFlow(("smcKeyGetAttrByName: key=%4.4s value=%.*Rhxs\n",
                     &uKeyValueLog, pThis->CurKey.cbValue, &pThis->CurKey.Value));
            return SMC_STATUS_CD_SUCCESS;
        }

        Log(("smcKeyGetAttrByName: key=%4.4s getter failed! bRc=%#x\n", &uKeyValueLog, bRc));
    }
    else
    {
        Log(("smcKeyGetAttrByName: Key not found! key=%4.4s size=%#x\n", &uKeyValueLog, pThis->CurKey.cbValue));
        bRc = SMC_STATUS_CD_KEY_NOT_FOUND;
    }

    RT_ZERO(pThis->CurKey);
    return bRc;
}


static uint8_t smcKeyPutPrepare(PDEVSMC pThis)
{
    RT_NOREF1(pThis);
    return 0;
}

static uint8_t smcKeyPutValue(PDEVSMC pThis)
{
    RT_NOREF1(pThis);
    return 0;
}


/**
 * Data register read.
 *
 * @returns VINF_SUCCESS or VINF_IOM_R3_IOPORT_WRITE.
 * @param   uReg    The register number.
 * @param   pbValue Where to return the value.
 */
static VBOXSTRICTRC smcRegData_r(PDEVSMC pThis, uint8_t uReg, uint8_t *pbValue)
{
    RT_NOREF1(uReg);
    switch (pThis->bCmd)
    {
        case SMC_CMD_GET_KEY_VALUE:
            if (   pThis->u.s.bState == 0x05
                && pThis->offValue < pThis->CurKey.cbValue)
            {
                *pbValue = pThis->CurKey.Value.ab[pThis->offValue];
                if (++pThis->offValue >= pThis->CurKey.cbValue)
                    pThis->u.s.bState = 0x00;
                pThis->u.s.bStatusCode = SMC_STATUS_CD_SUCCESS;
            }
            else
            {
                Log(("smcRegData_r: Reading too much or at wrong time during SMC_CMD_GET_KEY_INFO!  bState=%#x offValue=%#x\n",
                     pThis->u.s.bState, pThis->offValue));
                pThis->u.s.bState = 0x00;
                pThis->u.s.bStatusCode = SMC_STATUS_CD_SPURIOUS_DATA; /** @todo check status code */
            }
            break;

        case SMC_CMD_GET_KEY_INFO:
            if (   pThis->u.s.bState == 0x05
                && pThis->offValue < 6)
            {
                if (pThis->offValue == 0)
                    *pbValue = pThis->CurKey.cbValue;
                else if (pThis->offValue < 1 + 4)
                    *pbValue = pThis->CurKey.Type.ab[pThis->offValue - 1];
                else
                    *pbValue = pThis->CurKey.fAttr;
                if (++pThis->offValue >= 6)
                    pThis->u.s.bState = 0x00;
                pThis->u.s.bStatusCode = SMC_STATUS_CD_SUCCESS;
            }
            else
            {
                Log(("smcRegData_r: Reading too much or at wrong time during SMC_CMD_GET_KEY_INFO!  bState=%#x offValue=%#x\n",
                     pThis->u.s.bState, pThis->offValue));
                pThis->u.s.bState = 0x00;
                pThis->u.s.bStatusCode = SMC_STATUS_CD_SPURIOUS_DATA; /** @todo check status code */
            }
            break;

        case SMC_CMD_GET_KEY_BY_INDEX:
            if (   pThis->u.s.bState == 0x05
                && pThis->offValue < sizeof(pThis->CurKey.Key))
            {
                *pbValue = pThis->CurKey.Key.ab[pThis->offValue];
                if (++pThis->offValue >= sizeof(pThis->CurKey.Key))
                    pThis->u.s.bState = 0x00;
                pThis->u.s.bStatusCode = SMC_STATUS_CD_SUCCESS;
            }
            else
            {
                Log(("smcRegData_r: Reading too much or at wrong time during GET_KEY_BY_INDEX!  bState=%#x offValue=%#x\n",
                     pThis->u.s.bState, pThis->offValue));
                pThis->u.s.bState = 0x00;
                pThis->u.s.bStatusCode = SMC_STATUS_CD_SPURIOUS_DATA; /** @todo check status code */
            }
            break;

        case SMC_CMD_PUT_KEY:
            Log(("smcRegData_r: Attempting to read data during PUT_KEY!\n"));
            *pbValue = 0xff;
            pThis->u.s.bState = 0;
            pThis->u.s.bStatusCode = SMC_STATUS_CD_SPURIOUS_DATA;
            break;

        default:
            Log(("smcRegData_r: Unknown command attempts reading data\n"));
            *pbValue = 0xff;
            pThis->u.s.bState = 0;
            pThis->u.s.bStatusCode = SMC_STATUS_CD_SPURIOUS_DATA;
            break;
    }

    return VINF_SUCCESS;
}


/**
 * Data register write.
 *
 * @returns VINF_SUCCESS or VINF_IOM_R3_IOPORT_WRITE.
 * @param   uReg    The register number.
 * @param   bValue  The value being written.
 */
static VBOXSTRICTRC smcRegData_w(PDEVSMC pThis, uint8_t uReg, uint8_t bValue)
{
    RT_NOREF1(uReg);
    switch (pThis->bCmd)
    {
        /*
         * Get or put key value.
         *
         * 5 bytes written, first 4 is the key the 5th is the value size.  In
         * the case of a put the value bytes are then written, while a get will
         * read the value bytes.
         */
        case SMC_CMD_GET_KEY_VALUE:
        case SMC_CMD_PUT_KEY:
            if (pThis->offKey < 4)
            {
                /* Key byte. */
                pThis->CurKey.Key.ab[pThis->offKey++] = bValue;
                pThis->u.s.bState = 0x04;
                pThis->u.s.bStatusCode = SMC_STATUS_CD_SUCCESS;
            }
            else if (pThis->offKey == 4)
            {
                /* Data length. */
                pThis->u.s.bState = 0;
                if (bValue <= sizeof(pThis->CurKey.Value))
                {
                    pThis->CurKey.cbValue = bValue;
                    pThis->offKey = 5;
                    Assert(pThis->offValue == 0);

                    if (pThis->bCmd == SMC_CMD_GET_KEY_VALUE)
                        pThis->u.s.bStatusCode = smcKeyGetByName(pThis);
                    else
                        pThis->u.s.bStatusCode = smcKeyPutPrepare(pThis);
                    if (pThis->u.s.bStatusCode == SMC_STATUS_CD_SUCCESS)
                        pThis->u.s.bState = 0x05;
                }
                else
                {
                    Log(("smcRegData_w: Guest attempts to get/put too many value bytes: %#x (max %#x)!\n",
                         bValue, sizeof(pThis->CurKey.Value)));
                    pThis->u.s.bStatusCode = SMC_STATUS_CD_KEY_SIZE_MISMATCH; /** @todo check this case! */
                }
            }
            else if (   pThis->bCmd == SMC_CMD_PUT_KEY
                     && pThis->offValue < pThis->CurKey.cbValue)
            {
                /* More value bytes for put key action. */
                pThis->CurKey.Value.ab[pThis->offValue++] = bValue;
                if (pThis->offValue != pThis->CurKey.cbValue)
                    pThis->u.s.bState = 0x05;
                else
                {
                    pThis->u.s.bState = 0x00;
                    pThis->u.s.bStatusCode = smcKeyPutValue(pThis);
                }
            }
            else
            {
                Log(("smcRegData_w: Writing too much data on %s command!\n", pThis->bCmd == SMC_CMD_PUT_KEY ? "put" : "get"));
                pThis->u.s.bState = 0x00;
                pThis->u.s.bStatusCode = SMC_STATUS_CD_SPURIOUS_DATA;
            }
            break;

        /*
         * Get key info and key by index seems to take action after the last
         * key char is written.  They then both go into a data reading phase.
         */
        case SMC_CMD_GET_KEY_INFO:
        case SMC_CMD_GET_KEY_BY_INDEX:
            if (pThis->offKey < 4)
            {
                pThis->CurKey.Key.ab[pThis->offKey] = bValue;
                if (++pThis->offKey == 4)
                {
                    if (pThis->bCmd == SMC_CMD_GET_KEY_BY_INDEX)
                        pThis->u.s.bStatusCode = smcKeyGetByIndex(pThis);
                    else
                        pThis->u.s.bStatusCode = smcKeyGetAttrByName(pThis);
                    pThis->u.s.bState = pThis->u.s.bStatusCode == SMC_STATUS_CD_SUCCESS ? 0x05 : 0x00;
                }
                else
                {
                    pThis->u.s.bState = 0x04;
                    pThis->u.s.bStatusCode = SMC_STATUS_CD_SUCCESS;
                }
            }
            else
            {
                Log(("smcRegData_w: Writing data beyond 5th byte on get %s command!\n",
                     pThis->bCmd == SMC_CMD_GET_KEY_INFO ? "info" : "by index"));
                pThis->u.s.bState = 0x00;
                pThis->u.s.bStatusCode = SMC_STATUS_CD_SPURIOUS_DATA;
            }
            break;

        default:
            Log(("smcRegData_w: Unknown command %#x!\n", bValue));
            pThis->u.s.bState = 0x00; /** @todo Check statuses with real HW. */
            pThis->u.s.bStatusCode = SMC_STATUS_CD_BAD_COMMAND;
            break;
    }
    return VINF_SUCCESS;
}


/**
 * Command register write.
 *
 * @returns VINF_SUCCESS or VINF_IOM_R3_IOPORT_WRITE.
 * @param   uReg    The register number.
 * @param   bValue  The value being written.
 */
static VBOXSTRICTRC smcRegCmd_w(PDEVSMC pThis, uint8_t uReg, uint8_t bValue)
{
    LogFlow(("smcRegCmd_w: New command: %#x (old=%#x)\n", bValue, pThis->bCmd)); NOREF(uReg);

    pThis->bCmd = bValue;

    /* Validate the command. */
    switch (bValue)
    {
        case SMC_CMD_GET_KEY_VALUE:
        case SMC_CMD_PUT_KEY:
        case SMC_CMD_GET_KEY_BY_INDEX:
        case SMC_CMD_GET_KEY_INFO:
            pThis->u.s.bState = 0x0c;
            pThis->u.s.bStatusCode = SMC_STATUS_CD_SUCCESS;
            break;

        default:
            Log(("SMC: Unknown command %#x!\n", bValue));
            pThis->u.s.bState = 0x00; /** @todo Check state with real HW. */
            pThis->u.s.bStatusCode = SMC_STATUS_CD_BAD_COMMAND;
            break;
    }

    /* Reset the value/key related state. */
    pThis->offKey   = 0;
    pThis->offValue = 0;
    pThis->CurKey.Key.u32 = 0;
    pThis->CurKey.cbValue = 0;

    return VINF_SUCCESS;
}


/**
 * Generic register write.
 *
 * @returns VINF_SUCCESS or VINF_IOM_R3_IOPORT_WRITE.
 * @param   uReg    The register number.
 * @param   bValue  The value being written.
 */
static VBOXSTRICTRC smcRegGen_w(PDEVSMC pThis, uint8_t uReg, uint8_t bValue)
{
    Log(("smcRegGen_w: %#04x: %#x -> %#x (write)\n", uReg, pThis->u.abRegsRW[uReg], bValue));
    pThis->u.abRegsRW[uReg] = bValue;
    return VINF_SUCCESS;
}


/**
 * Read from register that isn't writable and reads as 0xFF.
 *
 * @returns VINF_SUCCESS or VINF_IOM_R3_IOPORT_WRITE.
 * @param   uReg    The register number.
 * @param   pbValue Where to return the value.
 */
static VBOXSTRICTRC smcRegGen_r(PDEVSMC pThis, uint8_t uReg, uint8_t *pbValue)
{
    Log(("smcRegGen_r: %#04x: %#x (read)\n", uReg, pThis->u.abRegsRW[uReg]));
    *pbValue = pThis->u.abRegsRW[uReg];
    return VINF_SUCCESS;
}


/**
 * Write to register that isn't writable and reads as 0xFF.
 *
 * @returns VINF_SUCCESS or VINF_IOM_R3_IOPORT_WRITE.
 * @param   uReg    The register number.
 * @param   bValue  The value being written.
 */
static VBOXSTRICTRC smcRegFF_w(PDEVSMC pThis, uint8_t uReg, uint8_t bValue)
{
    RT_NOREF3(pThis, uReg, bValue);
    Log(("SMC: %#04x: Writing %#x to unknown register!\n", uReg, bValue));
    return VINF_SUCCESS;
}


/**
 * Read from register that isn't writable and reads as 0xFF.
 *
 * @returns VINF_SUCCESS or VINF_IOM_R3_IOPORT_WRITE.
 * @param   uReg    The register number.
 * @param   pbValue Where to return the value.
 */
static VBOXSTRICTRC smcRegFF_r(PDEVSMC pThis, uint8_t uReg, uint8_t *pbValue)
{
    RT_NOREF2(pThis, uReg);
    Log(("SMC: %#04x: Reading from unknown register!\n", uReg));
    *pbValue = 0xff;
    return VINF_SUCCESS;
}



/**
 * SMC register handlers (indexed by relative I/O port).
 *
 * The device seems to be all byte registers and will split wider
 * accesses between registers like if it was MMIO.  To better illustrate it
 * here is the output of the code in devR0SmcInitOnce on a MacPro3,1:
 * @verbatim
 * SMC: 0x0300=0xffffff63 w={0xff63, 0xffff}, b={0x63 0xff 0xff 0xff}
 * SMC: 0x0301=0x0cffffff w={0xffff, 0x0cff}, b={0xff 0xff 0xff 0x0c}
 * SMC: 0x0302=0xff0cffff w={0xffff, 0xff0c}, b={0xff 0xff 0x0c 0xff}
 * SMC: 0x0303=0xffff0cff w={0x0cff, 0xffff}, b={0xff 0x0c 0xff 0xff}
 * SMC: 0x0304=0xffffff0c w={0xff0c, 0xffff}, b={0x0c 0xff 0xff 0xff}
 * SMC: 0x0305=0xffffffff w={0xffff, 0xffff}, b={0xff 0xff 0xff 0xff}
 * SMC: 0x0306=0xffffffff w={0xffff, 0xffff}, b={0xff 0xff 0xff 0xff}
 * SMC: 0x0307=0xffffffff w={0xffff, 0xffff}, b={0xff 0xff 0xff 0xff}
 * SMC: 0x0308=0xffffffff w={0xffff, 0xffff}, b={0xff 0xff 0xff 0xff}
 * SMC: 0x0309=0xffffffff w={0xffff, 0xffff}, b={0xff 0xff 0xff 0xff}
 * SMC: 0x030a=0xffffffff w={0xffff, 0xffff}, b={0xff 0xff 0xff 0xff}
 * SMC: 0x030b=0xffffffff w={0xffff, 0xffff}, b={0xff 0xff 0xff 0xff}
 * SMC: 0x030c=0xffffffff w={0xffff, 0xffff}, b={0xff 0xff 0xff 0xff}
 * SMC: 0x030d=0x00ffffff w={0xffff, 0x00ff}, b={0xff 0xff 0xff 0x00}
 * SMC: 0x030e=0x0000ffff w={0xffff, 0x0000}, b={0xff 0xff 0x00 0x00}
 * SMC: 0x030f=0x000000ff w={0x00ff, 0x0000}, b={0xff 0x00 0x00 0x00}
 * SMC: 0x0310=0x00000000 w={0x0000, 0x0000}, b={0x00 0x00 0x00 0x00}
 * SMC: 0x0311=0x00000000 w={0x0000, 0x0000}, b={0x00 0x00 0x00 0x00}
 * SMC: 0x0312=0x00000000 w={0x0000, 0x0000}, b={0x00 0x00 0x00 0x00}
 * SMC: 0x0313=0x00000000 w={0x0000, 0x0000}, b={0x00 0x00 0x00 0x00}
 * SMC: 0x0314=0x00000000 w={0x0000, 0x0000}, b={0x00 0x00 0x00 0x00}
 * SMC: 0x0315=0x00000000 w={0x0000, 0x0000}, b={0x00 0x00 0x00 0x00}
 * SMC: 0x0316=0x00000000 w={0x0000, 0x0000}, b={0x00 0x00 0x00 0x00}
 * SMC: 0x0317=0x00000000 w={0x0000, 0x0000}, b={0x00 0x00 0x00 0x00}
 * SMC: 0x0318=0x00000000 w={0x0000, 0x0000}, b={0x00 0x00 0x00 0x00}
 * SMC: 0x0319=0xbe000000 w={0x0000, 0xbe00}, b={0x00 0x00 0x00 0xbe}
 * SMC: 0x031a=0xbabe0000 w={0x0000, 0xbabe}, b={0x00 0x00 0xbe 0xba}
 * SMC: 0x031b=0x00babe00 w={0xbe00, 0x00ba}, b={0x00 0xbe 0xba 0x00}
 * SMC: 0x031c=0xbe00babe w={0xbabe, 0xbe00}, b={0xbe 0xba 0x00 0xbe}
 * SMC: 0x031d=0xffbe00ba w={0x00ba, 0xffbe}, b={0xba 0x00 0xbe 0xff}
 * SMC: 0x031e=0xffffbe00 w={0xbe00, 0xffff}, b={0x00 0xbe 0xff 0xff}
 * SMC: 0x031f=0xffffffbe w={0xffbe, 0xffff}, b={0xbe 0xff 0xff 0xff}
 * @endverbatim
 *
 * The last dword is writable (0xbeXXbabe) where in the register at 0x1e is some
 * kind of status register for qualifying search failures and the like and will
 * be cleared under certain conditions.  The whole dword can be written and read
 * back unchanged, according to my experiments.  The 0x00 and 0x04 registers
 * does not read back what is written.
 *
 * My guess is that the 0xff values indicates ports that are not writable and
 * hardwired to 0xff, while the other values indicates ports that can be written
 * to and normally read back as written.  I'm not going to push my luck too far
 * wrt to exact behavior until I see the guest using the registers.
 */
static const struct
{
    VBOXSTRICTRC (*pfnWrite)(PDEVSMC pThis, uint8_t uReg, uint8_t bValue);
    VBOXSTRICTRC (*pfnRead)(PDEVSMC pThis, uint8_t uReg, uint8_t *pbValue);
} g_aSmcRegs[SMC_REG_COUNT] =
{
    /* [0x00] = */ { smcRegData_w,     smcRegData_r },
    /* [0x01] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x02] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x03] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x04] = */ { smcRegCmd_w,      smcRegGen_r },
    /* [0x05] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x06] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x07] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x08] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x09] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x0a] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x0b] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x0c] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x0d] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x0e] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x0f] = */ { smcRegFF_w,       smcRegFF_r },
    /* [0x10] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x11] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x12] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x13] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x14] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x15] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x16] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x17] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x18] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x19] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x1a] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x1b] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x1c] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x1d] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x1e] = */ { smcRegGen_w,      smcRegGen_r },
    /* [0x1f] = */ { smcRegGen_w,      smcRegGen_r },
};


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC) smcIoPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF1(pvUser);
#ifndef IN_RING3
    if (cb > 1)
    {
        Log3(("smcIoPortWrite: %#04x write access: %#x (LB %u) -> ring-3\n", offPort, u32, cb));
        return VINF_IOM_R3_IOPORT_WRITE;
    }
#endif
#ifdef LOG_ENABLED
    RTIOPORT const offPortLog = offPort;
    unsigned const cbLog      = cb;
#endif

    /*
     * The first register, usually only one is accessed.
     */
    PDEVSMC pThis = PDMDEVINS_2_DATA(pDevIns, PDEVSMC);
    AssertReturn(offPort < RT_ELEMENTS(g_aSmcRegs), VERR_INTERNAL_ERROR_3); /* impossible*/
    VBOXSTRICTRC rc = g_aSmcRegs[offPort].pfnWrite(pThis, offPort, u32);

    /*
     * On the off chance that multiple registers are being read.
     */
    if (cb > 1)
    {
        while (cb > 1 && offPort < SMC_REG_COUNT - 1)
        {
            cb--;
            offPort++;
            u32 >>= 8;
            VBOXSTRICTRC rc2 = g_aSmcRegs[offPort].pfnWrite(pThis, offPort, u32);
            if (rc2 != VINF_SUCCESS)
            {
                if (   rc == VINF_SUCCESS
                    || (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                    || (rc2 < rc && RT_SUCCESS(rc2) && RT_SUCCESS(rc)))
                    rc = rc2;
            }
        }
    }

    LogFlow(("smcIoPortWrite: %#04x write access: %#x (LB %u) rc=%Rrc\n", offPortLog, u32, cbLog, VBOXSTRICTRC_VAL(rc) ));
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC) smcIoPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF1(pvUser);
#ifndef IN_RING3
    if (cb > 1)
        return VINF_IOM_R3_IOPORT_READ;
#endif

    PDEVSMC        pThis = PDMDEVINS_2_DATA(pDevIns, PDEVSMC);
#ifdef LOG_ENABLED
    RTIOPORT const offPortLog = offPort;
    unsigned const cbLog      = cb;
#endif

    /*
     * The first register, usually only one is accessed.
     */
    AssertReturn(offPort < RT_ELEMENTS(g_aSmcRegs), VERR_INTERNAL_ERROR_3); /* impossible*/
    Log2(("smcIoPortRead: %#04x read access: LB %u\n", offPort, cb));
    uint8_t bValue = 0xff;
    VBOXSTRICTRC rc = g_aSmcRegs[offPort].pfnRead(pThis, offPort, &bValue);
    *pu32 = bValue;

    /*
     * On the off chance that multiple registers are being read.
     */
    if (cb > 1)
    {
        do
        {
            cb--;
            offPort++;
            bValue = 0xff;
            if (offPort < SMC_REG_COUNT)
            {
                VBOXSTRICTRC rc2 = g_aSmcRegs[offPort].pfnRead(pThis, offPort, &bValue);
                if (rc2 != VINF_SUCCESS)
                {
                    if (   rc == VINF_SUCCESS
                        || (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                        || (rc2 < rc && RT_SUCCESS(rc2) && RT_SUCCESS(rc)))
                        rc = rc2;
                }
            }
            *pu32 |= (uint32_t)bValue << ((4 - cb) * 8);
        } while (cb > 1);
    }
    LogFlow(("smcIoPortRead: %#04x read access: %#x (LB %u) rc=%Rrc\n", offPortLog, *pu32, cbLog, VBOXSTRICTRC_VAL(rc)));
    return rc;
}

#ifdef IN_RING3

/** @callback_method_impl{FNSSMDEVSAVEEXEC} */
static DECLCALLBACK(int) smcR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVSMC pThis = PDMDEVINS_2_DATA(pDevIns, PDEVSMC);
    RT_NOREF2(pSSM, pThis);

    /** @todo */

    return VINF_SUCCESS;
}


/** @callback_method_impl{FNSSMDEVLOADEXEC} */
static DECLCALLBACK(int) smcR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDEVSMC pThis = PDMDEVINS_2_DATA(pDevIns, PDEVSMC);
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);
    RT_NOREF2(pSSM, pThis);

    /* Fend off unsupported versions. */
    if (   uVersion != SMC_SAVED_STATE_VERSION
#if SMC_SAVED_STATE_VERSION != SMC_SAVED_STATE_VERSION_BAKA
        && uVersion != SMC_SAVED_STATE_VERSION_BAKA
#endif
        && uVersion != SMC_SAVED_STATE_VERSION_BAKA + 1)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /*
     * Do the actual restoring.
     */
    if (uVersion == SMC_SAVED_STATE_VERSION)
    {
        /** @todo */
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) smcR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVSMC         pThis = PDMDEVINS_2_DATA(pDevIns, PDEVSMC);
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;

    Assert(iInstance == 0); RT_NOREF1(iInstance);

    /*
     * Init the data.
     */
    pThis->bDollaryNumber  = 1;
    pThis->bShutdownReason = 3; /* STOP_CAUSE_POWERKEY_GOOD_CODE */

    /*
     * Validate configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "DeviceKey|GetKeyFromRealSMC", "");

    /*
     * Read configuration.
     */

    /* The DeviceKey sets OSK0 and OSK1. */
    int rc = pHlp->pfnCFGMQueryStringDef(pCfg, "DeviceKey", pThis->szOsk0And1, sizeof(pThis->szOsk0And1), "");
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"DeviceKey\" as a string failed"));

    /* Query the key from the OS / real hardware if asked to do so. */
    bool fGetKeyFromRealSMC;
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "GetKeyFromRealSMC", &fGetKeyFromRealSMC, false);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"GetKeyFromRealSMC\" as a boolean failed"));
    if (fGetKeyFromRealSMC)
    {
# ifdef RT_OS_DARWIN
        rc = getSmcKeyOs(pThis->szOsk0And1, sizeof(pThis->szOsk0And1));
        if (RT_FAILURE(rc))
        {
            LogRel(("SMC: Retrieving the SMC key from the OS failed (%Rrc), trying to read it from hardware\n", rc));
# endif
            rc = PDMDevHlpCallR0(pDevIns, SMC_CALLR0_READ_OSK, 0 /*uArg*/);
            if (RT_SUCCESS(rc))
                LogRel(("SMC: Successfully retrieved the SMC key from hardware\n"));
            else
                LogRel(("SMC: Retrieving the SMC key from hardware failed(%Rrc)\n", rc));
# ifdef RT_OS_DARWIN
        }
        else
            LogRel(("SMC: Successfully retrieved the SMC key from the OS\n"));
# endif
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Failed to query SMC value from the host"));
    }

    /*
     * Register I/O Ports
     */
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, SMC_PORT_FIRST, SMC_REG_COUNT, smcIoPortWrite, smcIoPortRead,
                                     "SMC data port", NULL, &pThis->hIoPorts);
    AssertRCReturn(rc, rc);

    /** @todo Newer versions (2.03) have an MMIO mapping as well (ACPI). */


    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, SMC_SAVED_STATE_VERSION, sizeof(*pThis), smcR3SaveExec, smcR3LoadExec);
    if (RT_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) smcRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVSMC pThis = PDMDEVINS_2_DATA(pDevIns, PDEVSMC);

    int rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPorts, smcIoPortWrite, smcIoPortRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);
    RT_NOREF(pThis);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */


/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceSmc =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "smc",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_ARCH,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVSMC),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Apple System Management Controller",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           smcR3Construct,
    /* .pfnDestruct = */            NULL,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               NULL,
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
    /* .pfnConstruct = */           smcRZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
# if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    /* .pfnRequest = */             devR0SmcReqHandler,
# else
    /* .pfnRequest = */             NULL,
# endif
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           smcRZConstruct,
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

#endif /* VBOX_DEVICE_STRUCT_TESTCASE */
