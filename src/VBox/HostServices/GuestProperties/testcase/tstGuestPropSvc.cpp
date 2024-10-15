/* $Id: tstGuestPropSvc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * Testcase for the guest property service.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <VBox/HostServices/GuestPropertySvc.h>
#include <VBox/err.h>
#include <VBox/hgcmsvc.h>
#include <iprt/test.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest = NIL_RTTEST;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
extern "C" DECLCALLBACK(DECLEXPORT(int)) VBoxHGCMSvcLoad (VBOXHGCMSVCFNTABLE *ptable);


/** Simple call handle structure for the guest call completion callback */
struct VBOXHGCMCALLHANDLE_TYPEDEF
{
    /** Where to store the result code */
    int32_t rc;
};

/** Dummy helper callback. */
static DECLCALLBACK(int) tstHlpInfoDeregister(void *pvInstance, const char *pszName)
{
    RT_NOREF(pvInstance, pszName);
    return VINF_SUCCESS;
}

/** Dummy helper callback. */
static DECLCALLBACK(int) tstHlpInfoRegister(void *pvInstance, const char *pszName, const char *pszDesc,
                                            PFNDBGFHANDLEREXT pfnHandler, void *pvUser)
{
    RT_NOREF(pvInstance, pszName, pszDesc, pfnHandler, pvUser);
    return VINF_SUCCESS;
}

/** Call completion callback for guest calls. */
static DECLCALLBACK(int) callComplete(VBOXHGCMCALLHANDLE callHandle, int32_t rc)
{
    callHandle->rc = rc;
    return VINF_SUCCESS;
}

/**
 * Initialise the HGCM service table as much as we need to start the
 * service
 * @param  pTable the table to initialise
 */
static void initTable(VBOXHGCMSVCFNTABLE *pTable, VBOXHGCMSVCHELPERS *pHelpers)
{
    RT_ZERO(*pHelpers);
    pHelpers->pfnCallComplete   = callComplete;
    pHelpers->pfnInfoRegister   = tstHlpInfoRegister;
    pHelpers->pfnInfoDeregister = tstHlpInfoDeregister;

    RT_ZERO(*pTable);
    pTable->cbSize              = sizeof(VBOXHGCMSVCFNTABLE);
    pTable->u32Version          = VBOX_HGCM_SVC_VERSION;
    pTable->pHelpers            = pHelpers;
}

/**
 * A list of valid flag strings for testConvertFlags.  The flag conversion
 * functions should accept these and convert them from string to a flag type
 * and back without errors.
 */
struct flagStrings
{
    /** Flag string in a format the functions should recognise */
    const char *pcszIn;
    /** How the functions should output the string again */
    const char *pcszOut;
}
g_aValidFlagStrings[] =
{
    /* pcszIn,                                          pcszOut */
    { "  ",                                             "" },
    { "transient, ",                                    "TRANSIENT" },
    { "  rdOnLyHOST, transIENT  ,     READONLY    ",    "TRANSIENT, READONLY" },
    { " rdonlyguest",                                   "RDONLYGUEST" },
    { "rdonlyhost     ",                                "RDONLYHOST" },
    { "transient, transreset, rdonlyhost",              "TRANSIENT, RDONLYHOST, TRANSRESET" },
    { "transient, transreset, rdonlyguest",             "TRANSIENT, RDONLYGUEST, TRANSRESET" },     /* max length */
    { "rdonlyguest, rdonlyhost",                        "READONLY" },
    { "transient,   transreset, ",                      "TRANSIENT, TRANSRESET" }, /* Don't combine them ... */
    { "transreset, ",                                   "TRANSIENT, TRANSRESET" }, /* ... instead expand transreset for old adds. */
};

/**
 * A list of invalid flag strings for testConvertFlags.  The flag conversion
 * functions should reject these.
 */
const char *g_apszInvalidFlagStrings[] =
{
    "RDONLYHOST,,",
    "  TRANSIENT READONLY"
};

/**
 * Test the flag conversion functions.
 * @returns iprt status value to indicate whether the test went as expected.
 * @note    prints its own diagnostic information to stdout.
 */
static void testConvertFlags(void)
{
    int rc = VINF_SUCCESS;
    char *pszFlagBuffer = (char *)RTTestGuardedAllocTail(g_hTest, GUEST_PROP_MAX_FLAGS_LEN);

    RTTestISub("Conversion of valid flags strings");
    for (unsigned i = 0; i < RT_ELEMENTS(g_aValidFlagStrings) && RT_SUCCESS(rc); ++i)
    {
        uint32_t fFlags;
        rc = GuestPropValidateFlags(g_aValidFlagStrings[i].pcszIn, &fFlags);
        if (RT_FAILURE(rc))
            RTTestIFailed("Failed to validate flag string '%s'", g_aValidFlagStrings[i].pcszIn);
        if (RT_SUCCESS(rc))
        {
            rc = GuestPropWriteFlags(fFlags, pszFlagBuffer);
            if (RT_FAILURE(rc))
                RTTestIFailed("Failed to convert flag string '%s' back to a string.",
                              g_aValidFlagStrings[i].pcszIn);
        }
        if (RT_SUCCESS(rc) && (strlen(pszFlagBuffer) > GUEST_PROP_MAX_FLAGS_LEN - 1))
        {
            RTTestIFailed("String '%s' converts back to a flag string which is too long.\n",
                          g_aValidFlagStrings[i].pcszIn);
            rc = VERR_TOO_MUCH_DATA;
        }
        if (RT_SUCCESS(rc) && (strcmp(pszFlagBuffer, g_aValidFlagStrings[i].pcszOut) != 0))
        {
            RTTestIFailed("String '%s' converts back to '%s' instead of to '%s'\n",
                          g_aValidFlagStrings[i].pcszIn, pszFlagBuffer,
                          g_aValidFlagStrings[i].pcszOut);
            rc = VERR_PARSE_ERROR;
        }
    }
    if (RT_SUCCESS(rc))
    {
        RTTestISub("Rejection of invalid flags strings");
        for (unsigned i = 0; i < RT_ELEMENTS(g_apszInvalidFlagStrings) && RT_SUCCESS(rc); ++i)
        {
            uint32_t fFlags;
            /* This is required to fail. */
            if (RT_SUCCESS(GuestPropValidateFlags(g_apszInvalidFlagStrings[i], &fFlags)))
            {
                RTTestIFailed("String '%s' was incorrectly accepted as a valid flag string.\n",
                              g_apszInvalidFlagStrings[i]);
                rc = VERR_PARSE_ERROR;
            }
        }
    }
    if (RT_SUCCESS(rc))
    {
        uint32_t u32BadFlags = GUEST_PROP_F_ALLFLAGS << 1;
        RTTestISub("Rejection of an invalid flags field");
        /* This is required to fail. */
        if (RT_SUCCESS(GuestPropWriteFlags(u32BadFlags, pszFlagBuffer)))
        {
            RTTestIFailed("Flags 0x%x were incorrectly written out as '%.*s'\n",
                          u32BadFlags, GUEST_PROP_MAX_FLAGS_LEN, pszFlagBuffer);
            rc = VERR_PARSE_ERROR;
        }
    }

    RTTestGuardedFree(g_hTest, pszFlagBuffer);
}

/**
 * List of property names for testSetPropsHost.
 */
const char *g_apcszNameBlock[] =
{
    "test/name/",
    "test name",
    "TEST NAME",
    "/test/name",
    NULL
};

/**
 * List of property values for testSetPropsHost.
 */
const char *g_apcszValueBlock[] =
{
    "test/value/",
    "test value",
    "TEST VALUE",
    "/test/value",
    NULL
};

/**
 * List of property timestamps for testSetPropsHost.
 */
uint64_t g_au64TimestampBlock[] =
{
    0, 999, 999999, UINT64_C(999999999999), 0
};

/**
 * List of property flags for testSetPropsHost.
 */
const char *g_apcszFlagsBlock[] =
{
    "",
    "readonly, transient",
    "RDONLYHOST",
    "RdOnlyGuest",
    NULL
};

/**
 * Test the SET_PROPS_HOST function.
 * @returns iprt status value to indicate whether the test went as expected.
 * @note    prints its own diagnostic information to stdout.
 */
static void testSetPropsHost(VBOXHGCMSVCFNTABLE *ptable)
{
    RTTestISub("SET_PROPS_HOST");
    RTTESTI_CHECK_RETV(RT_VALID_PTR(ptable->pfnHostCall));

    VBOXHGCMSVCPARM aParms[4];
    HGCMSvcSetPv(&aParms[0], (void *)g_apcszNameBlock, 0);
    HGCMSvcSetPv(&aParms[1], (void *)g_apcszValueBlock, 0);
    HGCMSvcSetPv(&aParms[2], (void *)g_au64TimestampBlock, 0);
    HGCMSvcSetPv(&aParms[3], (void *)g_apcszFlagsBlock, 0);
    RTTESTI_CHECK_RC(ptable->pfnHostCall(ptable->pvService, GUEST_PROP_FN_HOST_SET_PROPS, 4, &aParms[0]), VINF_SUCCESS);
}

#if 0
/** Result strings for zeroth enumeration test */
static const char *g_apchEnumResult0[] =
{
    "test/name/\0test/value/\0""0\0",
    "test name\0test value\0""999\0TRANSIENT, READONLY",
    "TEST NAME\0TEST VALUE\0""999999\0RDONLYHOST",
    "/test/name\0/test/value\0""999999999999\0RDONLYGUEST",
    NULL
};

/** Result string sizes for zeroth enumeration test */
static const uint32_t g_acbEnumResult0[] =
{
    sizeof("test/name/\0test/value/\0""0\0"),
    sizeof("test name\0test value\0""999\0TRANSIENT, READONLY"),
    sizeof("TEST NAME\0TEST VALUE\0""999999\0RDONLYHOST"),
    sizeof("/test/name\0/test/value\0""999999999999\0RDONLYGUEST"),
    0
};

/**
 * The size of the buffer returned by the zeroth enumeration test -
 * the - 1 at the end is because of the hidden zero terminator
 */
static const uint32_t g_cbEnumBuffer0 =
    sizeof("test/name/\0test/value/\0""0\0\0"
           "test name\0test value\0""999\0TRANSIENT, READONLY\0"
           "TEST NAME\0TEST VALUE\0""999999\0RDONLYHOST\0"
           "/test/name\0/test/value\0""999999999999\0RDONLYGUEST\0\0\0\0\0") - 1;
#endif

/** Result strings for first and second enumeration test */
static const char *g_apchEnumResult1[] =
{
    "TEST NAME\0TEST VALUE\0""999999\0RDONLYHOST",
    "/test/name\0/test/value\0""999999999999\0RDONLYGUEST",
    NULL
};

/** Result string sizes for first and second enumeration test */
static const uint32_t g_acbEnumResult1[] =
{
    sizeof("TEST NAME\0TEST VALUE\0""999999\0RDONLYHOST"),
    sizeof("/test/name\0/test/value\0""999999999999\0RDONLYGUEST"),
    0
};

/**
 * The size of the buffer returned by the first enumeration test -
 * the - 1 at the end is because of the hidden zero terminator
 */
static const uint32_t g_cbEnumBuffer1 =
    sizeof("TEST NAME\0TEST VALUE\0""999999\0RDONLYHOST\0"
           "/test/name\0/test/value\0""999999999999\0RDONLYGUEST\0\0\0\0\0") - 1;

static const struct enumStringStruct
{
    /** The enumeration pattern to test */
    const char     *pszPatterns;
    /** The size of the pattern string (including terminator) */
    const uint32_t  cbPatterns;
    /** The expected enumeration output strings */
    const char    **papchResult;
    /** The size of the output strings */
    const uint32_t *pacchResult;
    /** The size of the buffer needed for the enumeration */
    const uint32_t  cbBuffer;
}   g_aEnumStrings[] =
{
#if 0 /* unpredictable automatic variables set by the service now */
    {
        "", sizeof(""),
        g_apchEnumResult0,
        g_acbEnumResult0,
        g_cbEnumBuffer0
    },
#endif
    {
        "/t*\0?E*", sizeof("/t*\0?E*"),
        g_apchEnumResult1,
        g_acbEnumResult1,
        g_cbEnumBuffer1
    },
    {
        "/t*|?E*", sizeof("/t*|?E*"),
        g_apchEnumResult1,
        g_acbEnumResult1,
        g_cbEnumBuffer1
    }
};

/**
 * Test the ENUM_PROPS_HOST function.
 * @returns iprt status value to indicate whether the test went as expected.
 * @note    prints its own diagnostic information to stdout.
 */
static void testEnumPropsHost(VBOXHGCMSVCFNTABLE *ptable)
{
    RTTestISub("ENUM_PROPS_HOST");
    RTTESTI_CHECK_RETV(RT_VALID_PTR(ptable->pfnHostCall));

    for (unsigned i = 0; i < RT_ELEMENTS(g_aEnumStrings); ++i)
    {
        VBOXHGCMSVCPARM aParms[3];
        char            abBuffer[2048];
        RTTESTI_CHECK_RETV(g_aEnumStrings[i].cbBuffer < sizeof(abBuffer));

        /* Check that we get buffer overflow with a too small buffer. */
        HGCMSvcSetPv(&aParms[0], (void *)g_aEnumStrings[i].pszPatterns, g_aEnumStrings[i].cbPatterns);
        HGCMSvcSetPv(&aParms[1], (void *)abBuffer, g_aEnumStrings[i].cbBuffer - 1);
        memset(abBuffer, 0x55, sizeof(abBuffer));
        int rc2 = ptable->pfnHostCall(ptable->pvService, GUEST_PROP_FN_HOST_ENUM_PROPS, 3, aParms);
        if (rc2 == VERR_BUFFER_OVERFLOW)
        {
            uint32_t cbNeeded;
            RTTESTI_CHECK_RC(rc2 = HGCMSvcGetU32(&aParms[2], &cbNeeded), VINF_SUCCESS);
            if (RT_SUCCESS(rc2))
                RTTESTI_CHECK_MSG(cbNeeded == g_aEnumStrings[i].cbBuffer,
                                  ("expected %#x, got %#x, pattern %d\n", g_aEnumStrings[i].cbBuffer, cbNeeded, i));
        }
        else
            RTTestIFailed("ENUM_PROPS_HOST returned %Rrc instead of VERR_BUFFER_OVERFLOW on too small buffer, pattern number %d.", rc2, i);

        /* Make a successfull call. */
        HGCMSvcSetPv(&aParms[0], (void *)g_aEnumStrings[i].pszPatterns, g_aEnumStrings[i].cbPatterns);
        HGCMSvcSetPv(&aParms[1], (void *)abBuffer, g_aEnumStrings[i].cbBuffer);
        memset(abBuffer, 0x55, sizeof(abBuffer));
        rc2 = ptable->pfnHostCall(ptable->pvService, GUEST_PROP_FN_HOST_ENUM_PROPS, 3, aParms);
        if (rc2 == VINF_SUCCESS)
        {
            /* Look for each of the result strings in the buffer which was returned */
            for (unsigned j = 0; g_aEnumStrings[i].papchResult[j] != NULL; ++j)
            {
                bool found = false;
                for (unsigned k = 0; !found && k < g_aEnumStrings[i].cbBuffer - g_aEnumStrings[i].pacchResult[j]; ++k)
                    if (memcmp(abBuffer + k, g_aEnumStrings[i].papchResult[j], g_aEnumStrings[i].pacchResult[j]) == 0)
                        found = true;
                if (!found)
                    RTTestIFailed("ENUM_PROPS_HOST did not produce the expected output for pattern %d.", i);
            }
        }
        else
            RTTestIFailed("ENUM_PROPS_HOST returned %Rrc instead of VINF_SUCCESS, pattern number %d.", rc2, i);
    }
}

/**
 * Set a property by calling the service
 * @returns the status returned by the call to the service
 *
 * @param   pTable      the service instance handle
 * @param   pcszName    the name of the property to set
 * @param   pcszValue   the value to set the property to
 * @param   pcszFlags   the flag string to set if one of the SET_PROP[_HOST]
 *                      commands is used
 * @param   isHost      whether the SET_PROP[_VALUE]_HOST commands should be
 *                      used, rather than the guest ones
 * @param   useSetProp  whether SET_PROP[_HOST] should be used rather than
 *                      SET_PROP_VALUE[_HOST]
 */
static int doSetProperty(VBOXHGCMSVCFNTABLE *pTable, const char *pcszName,
                         const char *pcszValue, const char *pcszFlags, bool isHost,
                         bool useSetProp)
{
    RTThreadSleep(1); /* stupid, stupid timestamp fudge to avoid asserting in getOldNotification() */

    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };
    int command = GUEST_PROP_FN_SET_PROP_VALUE;
    if (isHost)
    {
        if (useSetProp)
            command = GUEST_PROP_FN_HOST_SET_PROP;
        else
            command = GUEST_PROP_FN_HOST_SET_PROP_VALUE;
    }
    else if (useSetProp)
        command = GUEST_PROP_FN_SET_PROP;
    VBOXHGCMSVCPARM aParms[3];
    /* Work around silly constant issues - we ought to allow passing
     * constant strings in the hgcm parameters. */
    char szName[GUEST_PROP_MAX_NAME_LEN];
    char szValue[GUEST_PROP_MAX_VALUE_LEN];
    char szFlags[GUEST_PROP_MAX_FLAGS_LEN];
    RTStrPrintf(szName, sizeof(szName), "%s", pcszName);
    RTStrPrintf(szValue, sizeof(szValue), "%s", pcszValue);
    RTStrPrintf(szFlags, sizeof(szFlags), "%s", pcszFlags);
    HGCMSvcSetStr(&aParms[0], szName);
    HGCMSvcSetStr(&aParms[1], szValue);
    HGCMSvcSetStr(&aParms[2], szFlags);
    if (isHost)
        callHandle.rc = pTable->pfnHostCall(pTable->pvService, command,
                                            useSetProp ? 3 : 2, aParms);
    else
        pTable->pfnCall(pTable->pvService, &callHandle, 0, NULL, command,
                        useSetProp ? 3 : 2, aParms, 0);
    return callHandle.rc;
}

/**
 * Test the SET_PROP, SET_PROP_VALUE, SET_PROP_HOST and SET_PROP_VALUE_HOST
 * functions.
 * @returns iprt status value to indicate whether the test went as expected.
 * @note    prints its own diagnostic information to stdout.
 */
static void testSetProp(VBOXHGCMSVCFNTABLE *pTable)
{
    RTTestISub("SET_PROP, _VALUE, _HOST, _VALUE_HOST");

    /** Array of properties for testing SET_PROP_HOST and _GUEST. */
    static const struct
    {
        /** Property name */
        const char *pcszName;
        /** Property value */
        const char *pcszValue;
        /** Property flags */
        const char *pcszFlags;
        /** Should this be set as the host or the guest? */
        bool isHost;
        /** Should we use SET_PROP or SET_PROP_VALUE? */
        bool useSetProp;
        /** Should this succeed or be rejected with VERR_PERMISSION_DENIED? */
        bool isAllowed;
    }
    s_aSetProperties[] =
    {
        { "Red", "Stop!", "transient", false, true, true },
        { "Amber", "Caution!", "", false, false, true },
        { "Green", "Go!", "readonly", true, true, true },
        { "Blue", "What on earth...?", "", true, false, true },
        { "/test/name", "test", "", false, true, false },
        { "TEST NAME", "test", "", true, true, false },
        { "Green", "gone out...", "", false, false, false },
        { "Green", "gone out...", "", true, false, false },
        { "/VirtualBox/GuestAdd/SharedFolders/MountDir", "test", "", false, true, false },
        { "/VirtualBox/GuestAdd/SomethingElse", "test", "", false, true, true },
        { "/VirtualBox/HostInfo/VRDP/Client/1/Name", "test", "", false, false, false },
        { "/VirtualBox/GuestAdd/SharedFolders/MountDir", "test", "", true, true, true },
        { "/VirtualBox/HostInfo/VRDP/Client/1/Name", "test", "TRANSRESET", true, true, true },
    };

    for (unsigned i = 0; i < RT_ELEMENTS(s_aSetProperties); ++i)
    {
        int rc = doSetProperty(pTable,
                               s_aSetProperties[i].pcszName,
                               s_aSetProperties[i].pcszValue,
                               s_aSetProperties[i].pcszFlags,
                               s_aSetProperties[i].isHost,
                               s_aSetProperties[i].useSetProp);
        if (s_aSetProperties[i].isAllowed && RT_FAILURE(rc))
            RTTestIFailed("Setting property '%s' failed with rc=%Rrc.",
                          s_aSetProperties[i].pcszName, rc);
        else if (   !s_aSetProperties[i].isAllowed
                 && rc != VERR_PERMISSION_DENIED)
            RTTestIFailed("Setting property '%s' returned %Rrc instead of VERR_PERMISSION_DENIED.",
                          s_aSetProperties[i].pcszName, rc);
    }
}

/**
 * Delete a property by calling the service
 * @returns the status returned by the call to the service
 *
 * @param   pTable    the service instance handle
 * @param   pcszName  the name of the property to delete
 * @param   isHost    whether the DEL_PROP_HOST command should be used, rather
 *                    than the guest one
 */
static int doDelProp(VBOXHGCMSVCFNTABLE *pTable, const char *pcszName, bool isHost)
{
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };
    int command = GUEST_PROP_FN_DEL_PROP;
    if (isHost)
        command = GUEST_PROP_FN_HOST_DEL_PROP;
    VBOXHGCMSVCPARM aParms[1];
    HGCMSvcSetStr(&aParms[0], pcszName);
    if (isHost)
        callHandle.rc = pTable->pfnHostCall(pTable->pvService, command, 1, aParms);
    else
        pTable->pfnCall(pTable->pvService, &callHandle, 0, NULL, command, 1, aParms, 0);
    return callHandle.rc;
}

/**
 * Test the DEL_PROP, and DEL_PROP_HOST functions.
 * @returns iprt status value to indicate whether the test went as expected.
 * @note    prints its own diagnostic information to stdout.
 */
static void testDelProp(VBOXHGCMSVCFNTABLE *pTable)
{
    RTTestISub("DEL_PROP, DEL_PROP_HOST");

    /** Array of properties for testing DEL_PROP_HOST and _GUEST. */
    static const struct
    {
        /** Property name */
        const char *pcszName;
        /** Should this be set as the host or the guest? */
        bool isHost;
        /** Should this succeed or be rejected with VERR_PERMISSION_DENIED? */
        bool isAllowed;
    } s_aDelProperties[] =
    {
        { "Red", false, true },
        { "Amber", true, true },
        { "Red2", false, true },
        { "Amber2", true, true },
        { "Green", false, false },
        { "Green", true, false },
        { "/test/name", false, false },
        { "TEST NAME", true, false },
    };

    for (unsigned i = 0; i < RT_ELEMENTS(s_aDelProperties); ++i)
    {
        int rc = doDelProp(pTable, s_aDelProperties[i].pcszName, s_aDelProperties[i].isHost);
        if (s_aDelProperties[i].isAllowed && RT_FAILURE(rc))
            RTTestIFailed("Deleting property '%s' failed with rc=%Rrc.",
                          s_aDelProperties[i].pcszName, rc);
        else if (   !s_aDelProperties[i].isAllowed
                 && rc != VERR_PERMISSION_DENIED )
            RTTestIFailed("Deleting property '%s' returned %Rrc instead of VERR_PERMISSION_DENIED.",
                          s_aDelProperties[i].pcszName, rc);
    }
}

/**
 * Test the GET_PROP_HOST function.
 * @returns iprt status value to indicate whether the test went as expected.
 * @note    prints its own diagnostic information to stdout.
 */
static void testGetProp(VBOXHGCMSVCFNTABLE *pTable)
{
    RTTestISub("GET_PROP_HOST");

    /** Array of properties for testing GET_PROP_HOST. */
    static const struct
    {
        /** Property name */
        const char *pcszName;
        /** What value/flags pattern do we expect back? */
        const char *pchValue;
        /** What size should the value/flags array be? */
        uint32_t cchValue;
        /** Should this property exist? */
        bool exists;
        /** Do we expect a particular timestamp? */
        bool hasTimestamp;
        /** What timestamp if any do ex expect? */
        uint64_t u64Timestamp;
    }
    s_aGetProperties[] =
    {
        { "test/name/", "test/value/\0", sizeof("test/value/\0"), true, true, 0 },
        { "test name", "test value\0TRANSIENT, READONLY",
          sizeof("test value\0TRANSIENT, READONLY"), true, true, 999 },
        { "TEST NAME", "TEST VALUE\0RDONLYHOST", sizeof("TEST VALUE\0RDONLYHOST"),
          true, true, 999999 },
        { "/test/name", "/test/value\0RDONLYGUEST",
          sizeof("/test/value\0RDONLYGUEST"), true, true, UINT64_C(999999999999) },
        { "Green", "Go!\0READONLY", sizeof("Go!\0READONLY"), true, false, 0 },
        { "Blue", "What on earth...?\0", sizeof("What on earth...?\0"), true,
          false, 0 },
        { "Red", "", 0, false, false, 0 },
    };

    for (unsigned i = 0; i < RT_ELEMENTS(s_aGetProperties); ++i)
    {
        VBOXHGCMSVCPARM aParms[4];
        /* Work around silly constant issues - we ought to allow passing
         * constant strings in the hgcm parameters. */
        char szBuffer[GUEST_PROP_MAX_VALUE_LEN + GUEST_PROP_MAX_FLAGS_LEN];
        RTTESTI_CHECK_RETV(s_aGetProperties[i].cchValue < sizeof(szBuffer));

        HGCMSvcSetStr(&aParms[0], s_aGetProperties[i].pcszName);
        memset(szBuffer, 0x55, sizeof(szBuffer));
        HGCMSvcSetPv(&aParms[1], szBuffer, sizeof(szBuffer));
        int rc2 = pTable->pfnHostCall(pTable->pvService, GUEST_PROP_FN_HOST_GET_PROP, 4, aParms);

        if (s_aGetProperties[i].exists && RT_FAILURE(rc2))
        {
            RTTestIFailed("Getting property '%s' failed with rc=%Rrc.",
                          s_aGetProperties[i].pcszName, rc2);
            continue;
        }

        if (!s_aGetProperties[i].exists && rc2 != VERR_NOT_FOUND)
        {
            RTTestIFailed("Getting property '%s' returned %Rrc instead of VERR_NOT_FOUND.",
                          s_aGetProperties[i].pcszName, rc2);
            continue;
        }

        if (s_aGetProperties[i].exists)
        {
            AssertRC(rc2);

            uint32_t u32ValueLen = UINT32_MAX;
            RTTESTI_CHECK_RC(rc2 = HGCMSvcGetU32(&aParms[3], &u32ValueLen), VINF_SUCCESS);
            if (RT_SUCCESS(rc2))
            {
                RTTESTI_CHECK_MSG(u32ValueLen <= sizeof(szBuffer), ("u32ValueLen=%d", u32ValueLen));
                if (memcmp(szBuffer, s_aGetProperties[i].pchValue, s_aGetProperties[i].cchValue) != 0)
                    RTTestIFailed("Unexpected result '%.*s' for property '%s', expected '%.*s'.",
                                  u32ValueLen, szBuffer, s_aGetProperties[i].pcszName,
                                  s_aGetProperties[i].cchValue, s_aGetProperties[i].pchValue);
            }

            if (s_aGetProperties[i].hasTimestamp)
            {
                uint64_t u64Timestamp = UINT64_MAX;
                RTTESTI_CHECK_RC(rc2 = HGCMSvcGetU64(&aParms[2], &u64Timestamp), VINF_SUCCESS);
                if (u64Timestamp != s_aGetProperties[i].u64Timestamp)
                    RTTestIFailed("Bad timestamp %llu for property '%s', expected %llu.",
                                  u64Timestamp, s_aGetProperties[i].pcszName,
                                  s_aGetProperties[i].u64Timestamp);
            }
        }
    }
}

/** Array of properties for testing GET_PROP_HOST. */
static const struct
{
    /** Buffer returned */
    const char *pchBuffer;
    /** What size should the buffer be? */
    uint32_t cbBuffer;
}
g_aGetNotifications[] =
{
    // Name\0Value\0Flags\0fWasDeleted\0
#define STR_AND_SIZE(a_sz) { a_sz, sizeof(a_sz) }
    STR_AND_SIZE("Red\0Stop!\0TRANSIENT\0" "0"), /* first test is used by testAsyncNotification, - testGetNotification skips it. (mess) */
    STR_AND_SIZE("Red\0Stop!\0TRANSIENT\0" "1"),
    STR_AND_SIZE("Amber\0Caution!\0\0" "1"),
    STR_AND_SIZE("Green\0Go!\0READONLY\0" "0"),
    STR_AND_SIZE("Blue\0What on earth...?\0\0" "0"),
    STR_AND_SIZE("/VirtualBox/GuestAdd/SomethingElse\0test\0\0" "0"),
    STR_AND_SIZE("/VirtualBox/GuestAdd/SharedFolders/MountDir\0test\0RDONLYGUEST\0" "0"),
    STR_AND_SIZE("/VirtualBox/HostInfo/VRDP/Client/1/Name\0test\0TRANSIENT, RDONLYGUEST, TRANSRESET\0" "0"),
    STR_AND_SIZE("Red\0\0\0" "1"),
    STR_AND_SIZE("Amber\0\0\0" "1"),
#undef STR_AND_SIZE
};

/**
 * Test the GET_NOTIFICATION function.
 * @returns iprt status value to indicate whether the test went as expected.
 * @note    prints its own diagnostic information to stdout.
 */
static void testGetNotification(VBOXHGCMSVCFNTABLE *pTable)
{
    RTTestISub("GET_NOTIFICATION");

    /* Test "buffer too small" */
    static char                 s_szPattern[] = "/VirtualBox/GuestAdd/*|/VirtualBox/HostInfo/VRDP/Client*|Red*|Amber*|Green*|Blue*";
    VBOXHGCMCALLHANDLE_TYPEDEF  callHandle = { VINF_SUCCESS };
    VBOXHGCMSVCPARM             aParms[4];
    uint32_t                    cbRetNeeded = 0;

    for (uint32_t cbBuf = 1;
         cbBuf < g_aGetNotifications[1].cbBuffer - 1;
         cbBuf++)
    {
        void *pvBuf = RTTestGuardedAllocTail(g_hTest, cbBuf);
        RTTESTI_CHECK_BREAK(pvBuf);
        memset(pvBuf, 0x55, cbBuf);

        HGCMSvcSetStr(&aParms[0], s_szPattern);
        HGCMSvcSetU64(&aParms[1], 1);
        HGCMSvcSetPv(&aParms[2], pvBuf, cbBuf);
        pTable->pfnCall(pTable->pvService, &callHandle, 0, NULL, GUEST_PROP_FN_GET_NOTIFICATION, 4, aParms, 0);

        if (   callHandle.rc != VERR_BUFFER_OVERFLOW
            || RT_FAILURE(HGCMSvcGetU32(&aParms[3], &cbRetNeeded))
            || cbRetNeeded != g_aGetNotifications[1].cbBuffer
           )
            RTTestIFailed("Getting notification for property '%s' with a too small buffer did not fail correctly: rc=%Rrc, cbRetNeeded=%#x (expected %#x)",
                          g_aGetNotifications[1].pchBuffer, callHandle.rc, cbRetNeeded, g_aGetNotifications[1].cbBuffer);
        RTTestGuardedFree(g_hTest, pvBuf);
    }

    /* Test successful notification queries.  Start with an unknown timestamp
     * to get the oldest available notification. */
    uint64_t u64Timestamp = 1;
    for (unsigned i = 1; i < RT_ELEMENTS(g_aGetNotifications); ++i)
    {
        uint32_t cbBuf = g_aGetNotifications[i].cbBuffer + _1K;
        void *pvBuf = RTTestGuardedAllocTail(g_hTest, cbBuf);
        RTTESTI_CHECK_BREAK(pvBuf);
        memset(pvBuf, 0x55, cbBuf);

        HGCMSvcSetStr(&aParms[0], s_szPattern);
        HGCMSvcSetU64(&aParms[1], u64Timestamp);
        HGCMSvcSetPv(&aParms[2], pvBuf, cbBuf);
        pTable->pfnCall(pTable->pvService, &callHandle, 0, NULL, GUEST_PROP_FN_GET_NOTIFICATION, 4, aParms, 0);
        if (   RT_FAILURE(callHandle.rc)
            || (i == 0 && callHandle.rc != VWRN_NOT_FOUND)
            || RT_FAILURE(HGCMSvcGetU64(&aParms[1], &u64Timestamp))
            || RT_FAILURE(HGCMSvcGetU32(&aParms[3], &cbRetNeeded))
            || cbRetNeeded != g_aGetNotifications[i].cbBuffer
            || memcmp(pvBuf, g_aGetNotifications[i].pchBuffer, cbRetNeeded) != 0
           )
        {
            RTTestIFailed("Failed to get notification for property '%s' (#%u): rc=%Rrc (expected %Rrc), cbRetNeeded=%#x (expected %#x)\n"
                          "%.*Rhxd\n---expected:---\n%.*Rhxd",
                          g_aGetNotifications[i].pchBuffer, i, callHandle.rc, i == 0 ? VWRN_NOT_FOUND : VINF_SUCCESS,
                          cbRetNeeded, g_aGetNotifications[i].cbBuffer, RT_MIN(cbRetNeeded, cbBuf), pvBuf,
                          g_aGetNotifications[i].cbBuffer, g_aGetNotifications[i].pchBuffer);
        }
        RTTestGuardedFree(g_hTest, pvBuf);
    }
}

/** Parameters for the asynchronous guest notification call */
struct asyncNotification_
{
    /** Call parameters */
    VBOXHGCMSVCPARM aParms[4];
    /** Result buffer */
    char abBuffer[GUEST_PROP_MAX_NAME_LEN + GUEST_PROP_MAX_VALUE_LEN + GUEST_PROP_MAX_FLAGS_LEN];
    /** Return value */
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle;
} g_AsyncNotification;

/**
 * Set up the test for the asynchronous GET_NOTIFICATION function.
 */
static void setupAsyncNotification(VBOXHGCMSVCFNTABLE *pTable)
{
    RTTestISub("Async GET_NOTIFICATION without notifications");
    static char s_szPattern[] = "";

    HGCMSvcSetStr(&g_AsyncNotification.aParms[0], s_szPattern);
    HGCMSvcSetU64(&g_AsyncNotification.aParms[1], 0);
    HGCMSvcSetPv(&g_AsyncNotification.aParms[2], g_AsyncNotification.abBuffer, sizeof(g_AsyncNotification.abBuffer));
    g_AsyncNotification.callHandle.rc = VINF_HGCM_ASYNC_EXECUTE;
    pTable->pfnCall(pTable->pvService, &g_AsyncNotification.callHandle, 0, NULL,
                    GUEST_PROP_FN_GET_NOTIFICATION, 4, g_AsyncNotification.aParms, 0);
    if (RT_FAILURE(g_AsyncNotification.callHandle.rc))
        RTTestIFailed("GET_NOTIFICATION call failed, rc=%Rrc.", g_AsyncNotification.callHandle.rc);
    else if (g_AsyncNotification.callHandle.rc != VINF_HGCM_ASYNC_EXECUTE)
        RTTestIFailed("GET_NOTIFICATION call completed when no new notifications should be available.");
}

/**
 * Test the asynchronous GET_NOTIFICATION function.
 */
static void testAsyncNotification(VBOXHGCMSVCFNTABLE *pTable)
{
    RT_NOREF1(pTable);
    uint64_t u64Timestamp;
    uint32_t cb = 0;
    if (   g_AsyncNotification.callHandle.rc != VINF_SUCCESS
        || RT_FAILURE(HGCMSvcGetU64(&g_AsyncNotification.aParms[1], &u64Timestamp))
        || RT_FAILURE(HGCMSvcGetU32(&g_AsyncNotification.aParms[3], &cb))
        || cb != g_aGetNotifications[0].cbBuffer
        || memcmp(g_AsyncNotification.abBuffer, g_aGetNotifications[0].pchBuffer, cb) != 0
       )
    {
        RTTestIFailed("Asynchronous GET_NOTIFICATION call did not complete as expected: rc=%Rrc, cb=%#x (expected %#x)\n"
                      "abBuffer=%.*Rhxs\n"
                      "expected=%.*Rhxs",
                      g_AsyncNotification.callHandle.rc, cb, g_aGetNotifications[0].cbBuffer,
                      cb, g_AsyncNotification.abBuffer, g_aGetNotifications[0].cbBuffer, g_aGetNotifications[0].pchBuffer);
    }
}


static void test2(void)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    initTable(&svcTable, &svcHelpers);

    /* The function is inside the service, not HGCM. */
    RTTESTI_CHECK_RC_OK_RETV(VBoxHGCMSvcLoad(&svcTable));

    testSetPropsHost(&svcTable);
    testEnumPropsHost(&svcTable);

    /* Set up the asynchronous notification test */
    setupAsyncNotification(&svcTable);
    testSetProp(&svcTable);
    RTTestISub("Async notification call data");
    testAsyncNotification(&svcTable); /* Our previous notification call should have completed by now. */

    testDelProp(&svcTable);
    testGetProp(&svcTable);
    testGetNotification(&svcTable);

    /* Cleanup */
    RTTESTI_CHECK_RC_OK(svcTable.pfnUnload(svcTable.pvService));
}

/**
 * Set the global flags value by calling the service
 * @returns the status returned by the call to the service
 *
 * @param   pTable  the service instance handle
 * @param   fFlags  the flags to set
 */
static int doSetGlobalFlags(VBOXHGCMSVCFNTABLE *pTable, uint32_t fFlags)
{
    VBOXHGCMSVCPARM paParm;
    HGCMSvcSetU32(&paParm, fFlags);
    int rc = pTable->pfnHostCall(pTable->pvService, GUEST_PROP_FN_HOST_SET_GLOBAL_FLAGS, 1, &paParm);
    if (RT_FAILURE(rc))
    {
        char szFlags[GUEST_PROP_MAX_FLAGS_LEN];
        if (RT_FAILURE(GuestPropWriteFlags(fFlags, szFlags)))
            RTTestIFailed("Failed to set the global flags.");
        else
            RTTestIFailed("Failed to set the global flags \"%s\".", szFlags);
    }
    return rc;
}

/**
 * Test the SET_PROP, SET_PROP_VALUE, SET_PROP_HOST and SET_PROP_VALUE_HOST
 * functions.
 * @returns iprt status value to indicate whether the test went as expected.
 * @note    prints its own diagnostic information to stdout.
 */
static void testSetPropROGuest(VBOXHGCMSVCFNTABLE *pTable)
{
    RTTestISub("global READONLYGUEST and SET_PROP*");

    /** Array of properties for testing SET_PROP_HOST and _GUEST with the
     * READONLYGUEST global flag set. */
    static const struct
    {
        /** Property name */
        const char *pcszName;
        /** Property value */
        const char *pcszValue;
        /** Property flags */
        const char *pcszFlags;
        /** Should this be set as the host or the guest? */
        bool isHost;
        /** Should we use SET_PROP or SET_PROP_VALUE? */
        bool useSetProp;
        /** Should this succeed or be rejected with VERR_ (NOT VINF_!)
         * PERMISSION_DENIED?  The global check is done after the property one. */
        bool isAllowed;
    }
    s_aSetPropertiesROGuest[] =
    {
        { "Red", "Stop!", "transient", false, true, true },
        { "Amber", "Caution!", "", false, false, true },
        { "Green", "Go!", "readonly", true, true, true },
        { "Blue", "What on earth...?", "", true, false, true },
        { "/test/name", "test", "", false, true, true },
        { "TEST NAME", "test", "", true, true, true },
        { "Green", "gone out...", "", false, false, false },
        { "Green", "gone out....", "", true, false, false },
    };

    RTTESTI_CHECK_RC_OK_RETV(VBoxHGCMSvcLoad(pTable));
    int rc = doSetGlobalFlags(pTable, GUEST_PROP_F_RDONLYGUEST);
    if (RT_SUCCESS(rc))
    {
        for (unsigned i = 0; i < RT_ELEMENTS(s_aSetPropertiesROGuest); ++i)
        {
            rc = doSetProperty(pTable, s_aSetPropertiesROGuest[i].pcszName,
                               s_aSetPropertiesROGuest[i].pcszValue,
                               s_aSetPropertiesROGuest[i].pcszFlags,
                               s_aSetPropertiesROGuest[i].isHost,
                               s_aSetPropertiesROGuest[i].useSetProp);
            if (s_aSetPropertiesROGuest[i].isAllowed && RT_FAILURE(rc))
                RTTestIFailed("Setting property '%s' to '%s' failed with rc=%Rrc.",
                              s_aSetPropertiesROGuest[i].pcszName,
                              s_aSetPropertiesROGuest[i].pcszValue, rc);
            else if (   !s_aSetPropertiesROGuest[i].isAllowed
                     && rc != VERR_PERMISSION_DENIED)
                RTTestIFailed("Setting property '%s' to '%s' returned %Rrc instead of VERR_PERMISSION_DENIED.\n",
                              s_aSetPropertiesROGuest[i].pcszName,
                              s_aSetPropertiesROGuest[i].pcszValue, rc);
            else if (   !s_aSetPropertiesROGuest[i].isHost
                     && s_aSetPropertiesROGuest[i].isAllowed
                     && rc != VINF_PERMISSION_DENIED)
                RTTestIFailed("Setting property '%s' to '%s' returned %Rrc instead of VINF_PERMISSION_DENIED.\n",
                              s_aSetPropertiesROGuest[i].pcszName,
                              s_aSetPropertiesROGuest[i].pcszValue, rc);
        }
    }
    RTTESTI_CHECK_RC_OK(pTable->pfnUnload(pTable->pvService));
}

/**
 * Test the DEL_PROP, and DEL_PROP_HOST functions.
 * @returns iprt status value to indicate whether the test went as expected.
 * @note    prints its own diagnostic information to stdout.
 */
static void testDelPropROGuest(VBOXHGCMSVCFNTABLE *pTable)
{
    RTTestISub("global READONLYGUEST and DEL_PROP*");

    /** Array of properties for testing DEL_PROP_HOST and _GUEST with
     * READONLYGUEST set globally. */
    static const struct
    {
        /** Property name */
        const char *pcszName;
        /** Should this be deleted as the host (or the guest)? */
        bool isHost;
        /** Should this property be created first?  (As host, obviously) */
        bool shouldCreate;
        /** And with what flags? */
        const char *pcszFlags;
        /** Should this succeed or be rejected with VERR_ (NOT VINF_!)
         * PERMISSION_DENIED?  The global check is done after the property one. */
        bool isAllowed;
    }
    s_aDelPropertiesROGuest[] =
    {
        { "Red", true, true, "", true },
        { "Amber", false, true, "", true },
        { "Red2", true, false, "", true },
        { "Amber2", false, false, "", true },
        { "Red3", true, true, "READONLY", false },
        { "Amber3", false, true, "READONLY", false },
        { "Red4", true, true, "RDONLYHOST", false },
        { "Amber4", false, true, "RDONLYHOST", true },
    };

    RTTESTI_CHECK_RC_OK_RETV(VBoxHGCMSvcLoad(pTable));
    int rc = doSetGlobalFlags(pTable, GUEST_PROP_F_RDONLYGUEST);
    if (RT_SUCCESS(rc))
    {
        for (unsigned i = 0; i < RT_ELEMENTS(s_aDelPropertiesROGuest); ++i)
        {
            if (s_aDelPropertiesROGuest[i].shouldCreate)
                rc = doSetProperty(pTable, s_aDelPropertiesROGuest[i].pcszName,
                                   "none", s_aDelPropertiesROGuest[i].pcszFlags,
                                   true, true);
            rc = doDelProp(pTable, s_aDelPropertiesROGuest[i].pcszName,
                           s_aDelPropertiesROGuest[i].isHost);
            if (s_aDelPropertiesROGuest[i].isAllowed && RT_FAILURE(rc))
                RTTestIFailed("Deleting property '%s' failed with rc=%Rrc.",
                              s_aDelPropertiesROGuest[i].pcszName, rc);
            else if (   !s_aDelPropertiesROGuest[i].isAllowed
                     && rc != VERR_PERMISSION_DENIED)
                RTTestIFailed("Deleting property '%s' returned %Rrc instead of VERR_PERMISSION_DENIED.",
                              s_aDelPropertiesROGuest[i].pcszName, rc);
            else if (   !s_aDelPropertiesROGuest[i].isHost
                     && s_aDelPropertiesROGuest[i].shouldCreate
                     && s_aDelPropertiesROGuest[i].isAllowed
                     && rc != VINF_PERMISSION_DENIED)
                RTTestIFailed("Deleting property '%s' as guest returned %Rrc instead of VINF_PERMISSION_DENIED.",
                              s_aDelPropertiesROGuest[i].pcszName, rc);
        }
    }
    RTTESTI_CHECK_RC_OK(pTable->pfnUnload(pTable->pvService));
}

static void test3(void)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    initTable(&svcTable, &svcHelpers);
    testSetPropROGuest(&svcTable);
    testDelPropROGuest(&svcTable);
}

static void test4(void)
{
    RTTestISub("GET_PROP_HOST buffer handling");

    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    initTable(&svcTable, &svcHelpers);
    RTTESTI_CHECK_RC_OK_RETV(VBoxHGCMSvcLoad(&svcTable));

    /* Insert a property that we can mess around with. */
    static char const s_szProp[]  = "/MyProperties/Sub/Sub/Sub/Sub/Sub/Sub/Sub/Property";
    static char const s_szValue[] = "Property Value";
    RTTESTI_CHECK_RC_OK(doSetProperty(&svcTable, s_szProp, s_szValue, "", true, true));


    /* Get the value with buffer sizes up to 1K.  */
    for (unsigned iVariation = 0; iVariation < 2; iVariation++)
    {
        for (uint32_t cbBuf = 0; cbBuf < _1K; cbBuf++)
        {
            void *pvBuf;
            RTTESTI_CHECK_RC_BREAK(RTTestGuardedAlloc(g_hTest, cbBuf, 1, iVariation == 0, &pvBuf), VINF_SUCCESS);

            VBOXHGCMSVCPARM aParms[4];
            HGCMSvcSetStr(&aParms[0], s_szProp);
            HGCMSvcSetPv(&aParms[1], pvBuf, cbBuf);
            svcTable.pfnHostCall(svcTable.pvService, GUEST_PROP_FN_HOST_GET_PROP, RT_ELEMENTS(aParms), aParms);

            RTTestGuardedFree(g_hTest, pvBuf);
        }
    }

    /* Done. */
    RTTESTI_CHECK_RC_OK(svcTable.pfnUnload(svcTable.pvService));
}

static void test5(void)
{
    RTTestISub("ENUM_PROPS_HOST buffer handling");

    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    initTable(&svcTable, &svcHelpers);
    RTTESTI_CHECK_RC_OK_RETV(VBoxHGCMSvcLoad(&svcTable));

    /* Insert a few property that we can mess around with. */
    RTTESTI_CHECK_RC_OK(doSetProperty(&svcTable, "/MyProperties/Sub/Sub/Sub/Sub/Sub/Sub/Sub/Property", "Property Value", "", true, true));
    RTTESTI_CHECK_RC_OK(doSetProperty(&svcTable, "/MyProperties/12357",  "83848569", "", true, true));
    RTTESTI_CHECK_RC_OK(doSetProperty(&svcTable, "/MyProperties/56678",  "abcdefghijklm", "", true, true));
    RTTESTI_CHECK_RC_OK(doSetProperty(&svcTable, "/MyProperties/932769", "n", "", true, true));

    /* Get the value with buffer sizes up to 1K.  */
    for (unsigned iVariation = 0; iVariation < 2; iVariation++)
    {
        for (uint32_t cbBuf = 0; cbBuf < _1K; cbBuf++)
        {
            void *pvBuf;
            RTTESTI_CHECK_RC_BREAK(RTTestGuardedAlloc(g_hTest, cbBuf, 1, iVariation == 0, &pvBuf), VINF_SUCCESS);

            VBOXHGCMSVCPARM aParms[3];
            HGCMSvcSetStr(&aParms[0], "*");
            HGCMSvcSetPv(&aParms[1], pvBuf, cbBuf);
            svcTable.pfnHostCall(svcTable.pvService, GUEST_PROP_FN_HOST_ENUM_PROPS, RT_ELEMENTS(aParms), aParms);

            RTTestGuardedFree(g_hTest, pvBuf);
        }
    }

    /* Done. */
    RTTESTI_CHECK_RC_OK(svcTable.pfnUnload(svcTable.pvService));
}

static void test6(void)
{
    RTTestISub("Max properties");

    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    initTable(&svcTable, &svcHelpers);
    RTTESTI_CHECK_RC_OK_RETV(VBoxHGCMSvcLoad(&svcTable));

    /* Insert the max number of properties. */
    static char const   s_szPropFmt[] = "/MyProperties/Sub/Sub/Sub/Sub/Sub/Sub/Sub/PropertyNo#%u";
    char                szProp[80];
    unsigned            cProps = 0;
    for (;;)
    {
        RTStrPrintf(szProp, sizeof(szProp), s_szPropFmt, cProps);
        int rc = doSetProperty(&svcTable, szProp, "myvalue", "", true, true);
        if (rc == VERR_TOO_MUCH_DATA)
            break;
        if (RT_FAILURE(rc))
        {
            RTTestIFailed("Unexpected error %Rrc setting property number %u", rc, cProps);
            break;
        }
        cProps++;
    }
    RTTestIValue("Max Properties", cProps, RTTESTUNIT_OCCURRENCES);

    /* Touch them all again. */
    for (unsigned iProp = 0; iProp < cProps; iProp++)
    {
        RTStrPrintf(szProp, sizeof(szProp), s_szPropFmt, iProp);
        int rc;
        RTTESTI_CHECK_MSG((rc = doSetProperty(&svcTable, szProp, "myvalue", "", true, true)) == VINF_SUCCESS,
                          ("%Rrc - #%u\n", rc, iProp));
        RTTESTI_CHECK_MSG((rc = doSetProperty(&svcTable, szProp, "myvalue", "", true, false)) == VINF_SUCCESS,
                          ("%Rrc - #%u\n", rc, iProp));
        RTTESTI_CHECK_MSG((rc = doSetProperty(&svcTable, szProp, "myvalue", "", false, true)) == VINF_SUCCESS,
                          ("%Rrc - #%u\n", rc, iProp));
        RTTESTI_CHECK_MSG((rc = doSetProperty(&svcTable, szProp, "myvalue", "", false, false)) == VINF_SUCCESS,
                          ("%Rrc - #%u\n", rc, iProp));
    }

    /* Benchmark. */
    uint64_t cNsMax = 0;
    uint64_t cNsMin = UINT64_MAX;
    uint64_t cNsAvg = 0;
    for (unsigned iProp = 0; iProp < cProps; iProp++)
    {
        size_t cchProp = RTStrPrintf(szProp, sizeof(szProp), s_szPropFmt, iProp);

        uint64_t cNsElapsed = RTTimeNanoTS();
        unsigned iCall;
        for (iCall = 0; iCall < 1000; iCall++)
        {
            VBOXHGCMSVCPARM aParms[4];
            char            szBuffer[256];
            HGCMSvcSetPv(&aParms[0], szProp, (uint32_t)cchProp + 1);
            HGCMSvcSetPv(&aParms[1], szBuffer, sizeof(szBuffer));
            RTTESTI_CHECK_RC_BREAK(svcTable.pfnHostCall(svcTable.pvService, GUEST_PROP_FN_HOST_GET_PROP, 4, aParms), VINF_SUCCESS);
        }
        cNsElapsed = RTTimeNanoTS() - cNsElapsed;
        if (iCall)
        {
            uint64_t cNsPerCall = cNsElapsed / iCall;
            cNsAvg += cNsPerCall;
            if (cNsPerCall < cNsMin)
                cNsMin = cNsPerCall;
            if (cNsPerCall > cNsMax)
                cNsMax = cNsPerCall;
        }
    }
    if (cProps)
        cNsAvg /= cProps;
    RTTestIValue("GET_PROP_HOST Min", cNsMin, RTTESTUNIT_NS_PER_CALL);
    RTTestIValue("GET_PROP_HOST Avg", cNsAvg, RTTESTUNIT_NS_PER_CALL);
    RTTestIValue("GET_PROP_HOST Max", cNsMax, RTTESTUNIT_NS_PER_CALL);

    /* Done. */
    RTTESTI_CHECK_RC_OK(svcTable.pfnUnload(svcTable.pvService));
}




int main()
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstGuestPropSvc", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    testConvertFlags();
    test2();
    test3();
    test4();
    test5();
    test6();

    return RTTestSummaryAndDestroy(g_hTest);
}
