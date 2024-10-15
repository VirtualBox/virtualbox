/* $Id: VBoxGuestR3LibGuestProp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, guest properties.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#if defined(VBOX_VBGLR3_XFREE86) || defined(VBOX_VBGLR3_XORG)
# define VBOX_VBGLR3_XSERVER
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/string.h>
#ifndef VBOX_VBGLR3_XSERVER
# include <iprt/mem.h>
#endif
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/stdarg.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/HostServices/GuestPropertySvc.h>

#include "VBoxGuestR3LibInternal.h"

#ifdef VBOX_VBGLR3_XFREE86
/* Rather than try to resolve all the header file conflicts, I will just
   prototype what we need here. */
extern "C" char* xf86strcpy(char*,const char*);
# undef strcpy
# define strcpy xf86strcpy
extern "C" void* xf86memchr(const void*,int,xf86size_t);
# undef memchr
# define memchr xf86memchr
extern "C" void* xf86memset(const void*,int,xf86size_t);
# undef memset
# define memset xf86memset

#endif /* VBOX_VBGLR3_XFREE86 */

#ifdef VBOX_VBGLR3_XSERVER

# undef RTStrEnd
# define RTStrEnd xf86RTStrEnd

DECLINLINE(char const *) RTStrEnd(char const *pszString, size_t cchMax)
{
    /* Avoid potential issues with memchr seen in glibc.
     * See sysdeps/x86_64/memchr.S in glibc versions older than 2.11 */
    while (cchMax > RTSTR_MEMCHR_MAX)
    {
        char const *pszRet = (char const *)memchr(pszString, '\0', RTSTR_MEMCHR_MAX);
        if (RT_LIKELY(pszRet))
            return pszRet;
        pszString += RTSTR_MEMCHR_MAX;
        cchMax    -= RTSTR_MEMCHR_MAX;
    }
    return (char const *)memchr(pszString, '\0', cchMax);
}

DECLINLINE(char *) RTStrEnd(char *pszString, size_t cchMax)
{
    /* Avoid potential issues with memchr seen in glibc.
     * See sysdeps/x86_64/memchr.S in glibc versions older than 2.11 */
    while (cchMax > RTSTR_MEMCHR_MAX)
    {
        char *pszRet = (char *)memchr(pszString, '\0', RTSTR_MEMCHR_MAX);
        if (RT_LIKELY(pszRet))
            return pszRet;
        pszString += RTSTR_MEMCHR_MAX;
        cchMax    -= RTSTR_MEMCHR_MAX;
    }
    return (char *)memchr(pszString, '\0', cchMax);
}

#endif /* VBOX_VBGLR3_XSERVER */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Structure containing information needed to enumerate through guest
 * properties.
 *
 * @remarks typedef in VBoxGuestLib.h.
 */
struct VBGLR3GUESTPROPENUM
{
    /** @todo add a magic and validate the handle. */
    /** The buffer containing the raw enumeration data */
    char *pchBuf;
    /** The end of the buffer */
    char *pchBufEnd;
    /** Pointer to the next entry to enumerate inside the buffer */
    char *pchNext;
};



/**
 * Connects to the guest property service.
 *
 * @returns VBox status code
 * @returns VERR_NOT_SUPPORTED if guest properties are not available on the host.
 * @param   pidClient       Where to put the client ID on success. The client ID
 *                          must be passed to all the other calls to the service.
 */
VBGLR3DECL(int) VbglR3GuestPropConnect(HGCMCLIENTID *pidClient)
{
    int rc = VbglR3HGCMConnect("VBoxGuestPropSvc", pidClient);
    if (rc == VERR_NOT_IMPLEMENTED || rc == VERR_HGCM_SERVICE_NOT_FOUND)
        rc = VERR_NOT_SUPPORTED;
    return rc;
}


/**
 * Disconnect from the guest property service.
 *
 * @returns VBox status code.
 * @param   idClient        The client id returned by VbglR3InfoSvcConnect().
 */
VBGLR3DECL(int) VbglR3GuestPropDisconnect(HGCMCLIENTID idClient)
{
    return VbglR3HGCMDisconnect(idClient);
}


/**
 * Checks if @a pszPropName exists.
 *
 * @returns \c true if the guest property exists, \c false if not.
 * @param   idClient            The HGCM client ID for the guest property session.
 * @param   pszPropName         The property name.
 */
VBGLR3DECL(bool) VbglR3GuestPropExist(uint32_t idClient, const char *pszPropName)
{
    return RT_SUCCESS(VbglR3GuestPropReadEx(idClient, pszPropName, NULL /*ppszValue*/, NULL /* ppszFlags */, NULL /* puTimestamp */));
}


/**
 * Write a property value.
 *
 * @returns VBox status code.
 * @param   idClient        The client id returned by VbglR3InvsSvcConnect().
 * @param   pszName         The property to save to.  Utf8
 * @param   pszValue        The value to store.  Utf8.  If this is NULL then
 *                          the property will be removed.
 * @param   pszFlags        The flags for the property
 */
VBGLR3DECL(int) VbglR3GuestPropWrite(HGCMCLIENTID idClient, const char *pszName, const char *pszValue, const char *pszFlags)
{
    int rc;

    if (pszValue != NULL)
    {
        GuestPropMsgSetProperty Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, GUEST_PROP_FN_SET_PROP_VALUE, 3);
        VbglHGCMParmPtrSetString(&Msg.name,  pszName);
        VbglHGCMParmPtrSetString(&Msg.value, pszValue);
        VbglHGCMParmPtrSetString(&Msg.flags, pszFlags);
        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    }
    else
    {
        GuestPropMsgDelProperty Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, GUEST_PROP_FN_DEL_PROP, 1);
        VbglHGCMParmPtrSetString(&Msg.name, pszName);
        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    }
    return rc;
}


/**
 * Write a property value.
 *
 * @returns VBox status code.
 *
 * @param   idClient        The client id returned by VbglR3InvsSvcConnect().
 * @param   pszName         The property to save to.  Must be valid UTF-8.
 * @param   pszValue        The value to store.  Must be valid UTF-8.
 *                          If this is NULL then the property will be removed.
 *
 * @note  if the property already exists and pszValue is not NULL then the
 *        property's flags field will be left unchanged
 */
VBGLR3DECL(int) VbglR3GuestPropWriteValue(HGCMCLIENTID idClient, const char *pszName, const char *pszValue)
{
    int rc;

    if (pszValue != NULL)
    {
        GuestPropMsgSetPropertyValue Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, GUEST_PROP_FN_SET_PROP_VALUE, 2);
        VbglHGCMParmPtrSetString(&Msg.name, pszName);
        VbglHGCMParmPtrSetString(&Msg.value, pszValue);
        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    }
    else
    {
        GuestPropMsgDelProperty Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, GUEST_PROP_FN_DEL_PROP, 1);
        VbglHGCMParmPtrSetString(&Msg.name, pszName);
        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    }
    return rc;
}

#ifndef VBOX_VBGLR3_XSERVER
/**
 * Write a property value where the value is formatted in RTStrPrintfV fashion.
 *
 * @returns The same as VbglR3GuestPropWriteValue with the addition of VERR_NO_STR_MEMORY.
 *
 * @param   idClient        The client ID returned by VbglR3InvsSvcConnect().
 * @param   pszName         The property to save to.  Must be valid UTF-8.
 * @param   pszValueFormat  The value format. This must be valid UTF-8 when fully formatted.
 * @param   va              The format arguments.
 */
VBGLR3DECL(int) VbglR3GuestPropWriteValueV(HGCMCLIENTID idClient, const char *pszName, const char *pszValueFormat, va_list va)
{
    /*
     * Format the value and pass it on to the setter.
     */
    int rc = VERR_NO_STR_MEMORY;
    char *pszValue;
    if (RTStrAPrintfV(&pszValue, pszValueFormat, va) >= 0)
    {
        rc = VbglR3GuestPropWriteValue(idClient, pszName, pszValue);
        RTStrFree(pszValue);
    }
    return rc;
}


/**
 * Write a property value where the value is formatted in RTStrPrintf fashion.
 *
 * @returns The same as VbglR3GuestPropWriteValue with the addition of VERR_NO_STR_MEMORY.
 *
 * @param   idClient        The client ID returned by VbglR3InvsSvcConnect().
 * @param   pszName         The property to save to.  Must be valid UTF-8.
 * @param   pszValueFormat  The value format. This must be valid UTF-8 when fully formatted.
 * @param   ...             The format arguments.
 */
VBGLR3DECL(int) VbglR3GuestPropWriteValueF(HGCMCLIENTID idClient, const char *pszName, const char *pszValueFormat, ...)
{
    va_list va;
    va_start(va, pszValueFormat);
    int rc = VbglR3GuestPropWriteValueV(idClient, pszName, pszValueFormat, va);
    va_end(va);
    return rc;
}
#endif /* VBOX_VBGLR3_XSERVER */

/**
 * Retrieve a property.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success, pszValue, pu64Timestamp and pszFlags
 *          containing valid data.
 * @retval  VERR_BUFFER_OVERFLOW if the scratch buffer @a pcBuf is not large
 *          enough.  In this case the size needed will be placed in
 *          @a pcbBufActual if it is not NULL.
 * @retval  VERR_NOT_FOUND if the key wasn't found.
 *
 * @param   idClient        The client id returned by VbglR3GuestPropConnect().
 * @param   pszName         The value to read.  Utf8
 * @param   pvBuf           A scratch buffer to store the data retrieved into.
 *                          The returned data is only valid for it's lifetime.
 *                          @a ppszValue will point to the start of this buffer.
 * @param   cbBuf           The size of @a pcBuf
 * @param   ppszValue       Where to store the pointer to the value retrieved.
 *                          Optional.
 * @param   pu64Timestamp   Where to store the timestamp.  Optional.
 * @param   ppszFlags       Where to store the pointer to the flags.  Optional.
 * @param   pcbBufActual    If @a pcBuf is not large enough, the size needed.
 *                          Optional.
 */
VBGLR3DECL(int) VbglR3GuestPropRead(HGCMCLIENTID idClient, const char *pszName,
                                    void *pvBuf, uint32_t cbBuf,
                                    char **ppszValue, uint64_t *pu64Timestamp,
                                    char **ppszFlags,
                                    uint32_t *pcbBufActual)
{
    /*
     * Create the GET_PROP message and call the host.
     */
    GuestPropMsgGetProperty Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, GUEST_PROP_FN_GET_PROP, 4);
    VbglHGCMParmPtrSetString(&Msg.name, pszName);
    VbglHGCMParmPtrSet(&Msg.buffer, pvBuf, cbBuf);
    VbglHGCMParmUInt64Set(&Msg.timestamp, 0);
    VbglHGCMParmUInt32Set(&Msg.size, 0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    /*
     * The cbBufActual parameter is also returned on overflow so the call can
     * adjust his/her buffer.
     */
    if (    rc == VERR_BUFFER_OVERFLOW
        ||  pcbBufActual != NULL)
    {
        int rc2 = VbglHGCMParmUInt32Get(&Msg.size, pcbBufActual);
        AssertRCReturn(rc2, RT_FAILURE(rc) ? rc : rc2);
    }
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Buffer layout: Value\0Flags\0.
     *
     * If the caller cares about any of these strings, make sure things are
     * properly terminated (paranoia).
     */
    if (    RT_SUCCESS(rc)
        &&  (ppszValue != NULL || ppszFlags != NULL))
    {
        /* Validate / skip 'Name'. */
        char *pszFlags = RTStrEnd((char *)pvBuf, cbBuf) + 1;
        AssertPtrReturn(pszFlags, VERR_TOO_MUCH_DATA);
        if (ppszValue)
            *ppszValue = (char *)pvBuf;

        if (ppszFlags)
        {
            /* Validate 'Flags'. */
            char *pszEos = RTStrEnd(pszFlags, cbBuf - (pszFlags - (char *)pvBuf));
            AssertPtrReturn(pszEos, VERR_TOO_MUCH_DATA);
            *ppszFlags = pszFlags;
        }
    }

    /* And the timestamp, if requested. */
    if (pu64Timestamp != NULL)
    {
        rc = VbglHGCMParmUInt64Get(&Msg.timestamp, pu64Timestamp);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}

/**
 * Reads a guest property by returning allocated values.
 *
 * @returns VBox status code, fully bitched.
 *
 * @param   idClient            The HGCM client ID for the guest property session.
 * @param   pszPropName         The property name.
 * @param   ppszValue           Where to return the value.  This is always set
 *                              to NULL.  Needs to be free'd using RTStrFree().  Optional.
 * @param   ppszFlags           Where to return the value flags.
 *                              Needs to be free'd using RTStrFree().  Optional.
 * @param   puTimestamp         Where to return the timestamp.  This is only set
 *                              on success.  Optional.
 */
VBGLR3DECL(int) VbglR3GuestPropReadEx(uint32_t idClient,
                                      const char *pszPropName, char **ppszValue, char **ppszFlags, uint64_t *puTimestamp)
{
    AssertPtrReturn(pszPropName, VERR_INVALID_POINTER);

    uint32_t    cbBuf = _1K;
    void       *pvBuf = NULL;
    int         rc    = VINF_SUCCESS;  /* MSC can't figure out the loop */

    if (ppszValue)
        *ppszValue = NULL;

    for (unsigned cTries = 0; cTries < 10; cTries++)
    {
        /*
         * (Re-)Allocate the buffer and try read the property.
         */
        RTMemFree(pvBuf);
        pvBuf = RTMemAlloc(cbBuf);
        if (!pvBuf)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        char    *pszValue;
        char    *pszFlags;
        uint64_t uTimestamp;
        rc = VbglR3GuestPropRead(idClient, pszPropName, pvBuf, cbBuf, &pszValue, &uTimestamp, &pszFlags, NULL);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_BUFFER_OVERFLOW)
            {
                /* try again with a bigger buffer. */
                cbBuf *= 2;
                continue;
            }
            break;
        }

        if (ppszValue)
        {
            *ppszValue = RTStrDup(pszValue);
            if (!*ppszValue)
            {
                rc = VERR_NO_MEMORY;
                break;
            }
        }

        if (puTimestamp)
            *puTimestamp = uTimestamp;
        if (ppszFlags)
            *ppszFlags = RTStrDup(pszFlags);
        break; /* done */
    }

    if (pvBuf)
        RTMemFree(pvBuf);
    return rc;
}

#ifndef VBOX_VBGLR3_XSERVER
/**
 * Retrieve a property value, allocating space for it.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success, *ppszValue containing valid data.
 * @retval  VERR_NOT_FOUND if the key wasn't found.
 * @retval  VERR_TOO_MUCH_DATA if we were unable to determine the right size
 *          to allocate for the buffer.  This can happen as the result of a
 *          race between our allocating space and the host changing the
 *          property value.
 *
 * @param   idClient        The client id returned by VbglR3GuestPropConnect().
 * @param   pszName         The value to read. Must be valid UTF-8.
 * @param   ppszValue       Where to store the pointer to the value returned.
 *                          This is always set to NULL or to the result, even
 *                          on failure.
 */
VBGLR3DECL(int) VbglR3GuestPropReadValueAlloc(HGCMCLIENTID idClient, const char *pszName, char **ppszValue)
{
    /*
     * Quick input validation.
     */
    AssertPtr(ppszValue);
    *ppszValue = NULL;
    AssertPtrReturn(pszName, VERR_INVALID_PARAMETER);

    /*
     * There is a race here between our reading the property size and the
     * host changing the value before we read it.  Try up to ten times and
     * report the problem if that fails.
     */
    char       *pszValue = NULL;
    void       *pvBuf    = NULL;
    uint32_t    cbBuf    = GUEST_PROP_MAX_VALUE_LEN;
    int         rc       = VERR_BUFFER_OVERFLOW;
    for (unsigned i = 0; i < 10 && rc == VERR_BUFFER_OVERFLOW; ++i)
    {
        /* We leave a bit of space here in case the maximum value is raised. */
        cbBuf += 1024;
        void *pvTmpBuf = RTMemRealloc(pvBuf, cbBuf);
        if (pvTmpBuf)
        {
            pvBuf = pvTmpBuf;
            rc = VbglR3GuestPropRead(idClient, pszName, pvBuf, cbBuf, &pszValue, NULL, NULL, &cbBuf);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    if (RT_SUCCESS(rc))
    {
        Assert(pszValue == (char *)pvBuf);
        *ppszValue = pszValue;
    }
    else
    {
        RTMemFree(pvBuf);
        if (rc == VERR_BUFFER_OVERFLOW)
            /* VERR_BUFFER_OVERFLOW has a different meaning here as a
             * return code, but we need to report the race. */
            rc = VERR_TOO_MUCH_DATA;
    }

    return rc;
}


/**
 * Free the memory used by VbglR3GuestPropReadValueAlloc for returning a
 * value.
 *
 * @param pszValue   the memory to be freed.  NULL pointers will be ignored.
 */
VBGLR3DECL(void) VbglR3GuestPropReadValueFree(char *pszValue)
{
    RTMemFree(pszValue);
}
#endif /* VBOX_VBGLR3_XSERVER */

/**
 * Retrieve a property value, using a user-provided buffer to store it.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success, pszValue containing valid data.
 * @retval  VERR_BUFFER_OVERFLOW and the size needed in pcchValueActual if the
 *          buffer provided was too small
 * @retval  VERR_NOT_FOUND if the key wasn't found.
 *
 * @note    There is a race here between obtaining the size of the buffer
 *          needed to hold the value and the value being updated.
 *
 * @param   idClient        The client id returned by VbglR3GuestPropConnect().
 * @param   pszName         The value to read.  Utf8
 * @param   pszValue        Where to store the value retrieved.
 * @param   cchValue        The size of the buffer pointed to by @a pszValue
 * @param   pcchValueActual Where to store the size of the buffer needed if
 *                          the buffer supplied is too small.  Optional.
 */
VBGLR3DECL(int) VbglR3GuestPropReadValue(HGCMCLIENTID idClient, const char *pszName,
                                         char *pszValue, uint32_t cchValue,
                                         uint32_t *pcchValueActual)
{
    void *pvBuf = pszValue;
    uint32_t cchValueActual;
    int rc = VbglR3GuestPropRead(idClient, pszName, pvBuf, cchValue, &pszValue, NULL, NULL, &cchValueActual);
    if (pcchValueActual != NULL)
        *pcchValueActual = cchValueActual;
    return rc;
}


#ifndef VBOX_VBGLR3_XSERVER
/**
 * Raw API for enumerating guest properties which match a given pattern.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success and pcBuf points to a packed array
 *          of the form \<name\>, \<value\>, \<timestamp string\>, \<flags\>,
 *          terminated by four empty strings.  pcbBufActual will contain the
 *          total size of the array.
 * @retval  VERR_BUFFER_OVERFLOW if the buffer provided was too small.  In
 *          this case pcbBufActual will contain the size of the buffer needed.
 * @returns IPRT error code in other cases, and pchBufActual is undefined.
 *
 * @param   idClient      The client ID returned by VbglR3GuestPropConnect
 * @param   pszzPatterns  A packed array of zero terminated strings, terminated
 *                        by an empty string.
 * @param   pcBuf         The buffer to store the results to.
 * @param   cbBuf         The size of the buffer
 * @param   pcbBufActual  Where to store the size of the returned data on
 *                        success or the buffer size needed if @a pcBuf is too
 *                        small.
 */
VBGLR3DECL(int) VbglR3GuestPropEnumRaw(HGCMCLIENTID idClient,
                                       const char *pszzPatterns,
                                       char *pcBuf,
                                       uint32_t cbBuf,
                                       uint32_t *pcbBufActual)
{
    GuestPropMsgEnumProperties Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, GUEST_PROP_FN_ENUM_PROPS, 3);

    /* Get the length of the patterns array... */
    size_t cchPatterns = 0;
    for (size_t cchCurrent = strlen(pszzPatterns); cchCurrent != 0;
         cchCurrent = strlen(pszzPatterns + cchPatterns))
        cchPatterns += cchCurrent + 1;
    /* ...including the terminator. */
    ++cchPatterns;
    VbglHGCMParmPtrSet(&Msg.patterns, (char *)pszzPatterns, (uint32_t)cchPatterns);
    VbglHGCMParmPtrSet(&Msg.strings, pcBuf, cbBuf);
    VbglHGCMParmUInt32Set(&Msg.size, 0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (   pcbBufActual
        && (    RT_SUCCESS(rc)
            ||  rc == VERR_BUFFER_OVERFLOW))
    {
        int rc2 = VbglHGCMParmUInt32Get(&Msg.size, pcbBufActual);
        if (RT_FAILURE(rc2))
            rc = rc2;
    }
    return rc;
}


/**
 * Start enumerating guest properties which match a given pattern.
 *
 * This function creates a handle which can be used to continue enumerating.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success, *ppHandle points to a handle for continuing
 *          the enumeration and *ppszName, *ppszValue, *pu64Timestamp and
 *          *ppszFlags are set.
 * @retval  VERR_TOO_MUCH_DATA if it was not possible to determine the amount
 *          of local space needed to store all the enumeration data.  This is
 *          due to a race between allocating space and the host adding new
 *          data, so retrying may help here.  Other parameters are left
 *          uninitialised
 *
 * @param   idClient        The client id returned by VbglR3InfoSvcConnect().
 * @param   papszPatterns   The patterns against which the properties are
 *                          matched.  Pass NULL if everything should be matched.
 * @param   cPatterns       The number of patterns in @a papszPatterns.  0 means
 *                          match everything.
 * @param   ppHandle        where the handle for continued enumeration is stored
 *                          on success.  This must be freed with
 *                          VbglR3GuestPropEnumFree when it is no longer needed.
 * @param   ppszName        Where to store the next property name.  This will be
 *                          set to NULL if there are no more properties to
 *                          enumerate.  This pointer should not be freed. Optional.
 * @param   ppszValue       Where to store the next property value.  This will be
 *                          set to NULL if there are no more properties to
 *                          enumerate.  This pointer should not be freed. Optional.
 * @param   pu64Timestamp   Where to store the next property timestamp.  This
 *                          will be set to zero if there are no more properties
 *                          to enumerate. Optional.
 * @param   ppszFlags       Where to store the next property flags.  This will be
 *                          set to NULL if there are no more properties to
 *                          enumerate.  This pointer should not be freed. Optional.
 *
 * @remarks While all output parameters are optional, you need at least one to
 *          figure out when to stop.
 */
VBGLR3DECL(int) VbglR3GuestPropEnum(HGCMCLIENTID idClient,
                                    char const * const *papszPatterns,
                                    uint32_t cPatterns,
                                    PVBGLR3GUESTPROPENUM *ppHandle,
                                    char const **ppszName,
                                    char const **ppszValue,
                                    uint64_t *pu64Timestamp,
                                    char const **ppszFlags)
{
    /* Create the handle. */
    PVBGLR3GUESTPROPENUM pHandle = (PVBGLR3GUESTPROPENUM)RTMemAllocZ(sizeof(VBGLR3GUESTPROPENUM));
    if (RT_LIKELY(pHandle))
    {/* likely */}
    else
        return VERR_NO_MEMORY;

    /* Get the length of the pattern string, including the final terminator. */
    size_t cbPatterns = 1;
    for (uint32_t i = 0; i < cPatterns; ++i)
        cbPatterns += strlen(papszPatterns[i]) + 1;

    /* Pack the pattern array. */
    char *pszzPatterns = (char *)RTMemAlloc(cbPatterns);
    size_t off = 0;
    for (uint32_t i = 0; i < cPatterns; ++i)
    {
        size_t cb = strlen(papszPatterns[i]) + 1;
        memcpy(&pszzPatterns[off], papszPatterns[i], cb);
        off += cb;
    }
    pszzPatterns[off] = '\0';

    /* In reading the guest property data we are racing against the host
     * adding more of it, so loop a few times and retry on overflow. */
    uint32_t cbBuf  = 4096; /* picked out of thin air */
    char    *pchBuf = NULL;
    int      rc     = VINF_SUCCESS;
    for (int i = 0; i < 10; ++i)
    {
        void *pvNew = RTMemRealloc(pchBuf, cbBuf);
        if (pvNew)
            pchBuf = (char *)pvNew;
        else
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        rc = VbglR3GuestPropEnumRaw(idClient, pszzPatterns, pchBuf, cbBuf, &cbBuf);
        if (rc != VERR_BUFFER_OVERFLOW)
            break;
        cbBuf += 4096;  /* Just to increase our chances */
    }
    RTMemFree(pszzPatterns);
    if (RT_SUCCESS(rc))
    {
        /*
         * Complete the handle and call VbglR3GuestPropEnumNext to retrieve the first entry.
         */
        pHandle->pchNext   = pchBuf;
        pHandle->pchBuf    = pchBuf;
        pHandle->pchBufEnd = pchBuf + cbBuf;

        const char *pszNameTmp;
        if (!ppszName)
            ppszName = &pszNameTmp;
        rc = VbglR3GuestPropEnumNext(pHandle, ppszName, ppszValue, pu64Timestamp, ppszFlags);
        if (RT_SUCCESS(rc))
        {
            *ppHandle = pHandle;
            return rc;
        }
    }
    else if (rc == VERR_BUFFER_OVERFLOW)
        rc = VERR_TOO_MUCH_DATA;
    RTMemFree(pchBuf);
    RTMemFree(pHandle);
    return rc;
}


/**
 * Get the next guest property.
 *
 * See @a VbglR3GuestPropEnum.
 *
 * @returns VBox status code.
 *
 * @param  pHandle       Handle obtained from @a VbglR3GuestPropEnum.
 * @param  ppszName      Where to store the next property name.  This will be
 *                       set to NULL if there are no more properties to
 *                       enumerate.  This pointer should not be freed. Optional.
 * @param  ppszValue     Where to store the next property value.  This will be
 *                       set to NULL if there are no more properties to
 *                       enumerate.  This pointer should not be freed. Optional.
 * @param  pu64Timestamp Where to store the next property timestamp.  This
 *                       will be set to zero if there are no more properties
 *                       to enumerate. Optional.
 * @param  ppszFlags     Where to store the next property flags.  This will be
 *                       set to NULL if there are no more properties to
 *                       enumerate.  This pointer should not be freed. Optional.
 *
 * @remarks While all output parameters are optional, you need at least one to
 *          figure out when to stop.
 */
VBGLR3DECL(int) VbglR3GuestPropEnumNext(PVBGLR3GUESTPROPENUM pHandle,
                                        char const **ppszName,
                                        char const **ppszValue,
                                        uint64_t *pu64Timestamp,
                                        char const **ppszFlags)
{
    /*
     * The VBGLR3GUESTPROPENUM structure contains a buffer containing the raw
     * properties data and a pointer into the buffer which tracks how far we
     * have parsed so far.  The buffer contains packed strings in groups of
     * four - name, value, timestamp (as a decimal string) and flags.  It is
     * terminated by four empty strings.  We can rely on this layout unless
     * the caller has been poking about in the structure internals, in which
     * case they must take responsibility for the results.
     *
     * Layout:
     *   Name\0Value\0Timestamp\0Flags\0
     */
    char *pchNext = pHandle->pchNext;       /* The cursor. */
    char *pchEnd  = pHandle->pchBufEnd;     /* End of buffer, for size calculations. */

    char *pszName      = pchNext;
    char *pszValue     = pchNext = RTStrEnd(pchNext, pchEnd - pchNext) + 1;
    AssertPtrReturn(pchNext, VERR_PARSE_ERROR);  /* 0x1 is also an invalid pointer :) */

    char *pszTimestamp = pchNext = RTStrEnd(pchNext, pchEnd - pchNext) + 1;
    AssertPtrReturn(pchNext, VERR_PARSE_ERROR);

    char *pszFlags     = pchNext = RTStrEnd(pchNext, pchEnd - pchNext) + 1;
    AssertPtrReturn(pchNext, VERR_PARSE_ERROR);

    /*
     * Don't move the index pointer if we found the terminating "\0\0\0\0" entry.
     * Don't try convert the timestamp either.
     */
    uint64_t u64Timestamp;
    if (*pszName != '\0')
    {
        pchNext = RTStrEnd(pchNext, pchEnd - pchNext) + 1;
        AssertPtrReturn(pchNext, VERR_PARSE_ERROR);

        /* Convert the timestamp string into a number. */
        int rc = RTStrToUInt64Full(pszTimestamp, 0, &u64Timestamp);
        AssertRCSuccessReturn(rc, VERR_PARSE_ERROR);

        pHandle->pchNext = pchNext;
        AssertPtr(pchNext);
    }
    else
    {
        u64Timestamp = 0;
        AssertMsgReturn(!*pszValue && !*pszTimestamp && !*pszFlags,
                        ("'%s' '%s' '%s'\n", pszValue, pszTimestamp, pszFlags),
                        VERR_PARSE_ERROR);
    }

    /*
     * Everything is fine, set the return values.
     */
    if (ppszName)
        *ppszName  = *pszName  != '\0' ? pszName  : NULL;
    if (ppszValue)
        *ppszValue = *pszValue != '\0' ? pszValue : NULL;
    if (pu64Timestamp)
        *pu64Timestamp = u64Timestamp;
    if (ppszFlags)
        *ppszFlags = *pszFlags != '\0' ? pszFlags : NULL;
    return VINF_SUCCESS;
}


/**
 * Free an enumeration handle returned by @a VbglR3GuestPropEnum.
 * @param pHandle the handle to free
 */
VBGLR3DECL(void) VbglR3GuestPropEnumFree(PVBGLR3GUESTPROPENUM pHandle)
{
    if (!pHandle)
        return;
    RTMemFree(pHandle->pchBuf);
    RTMemFree(pHandle);
}


/**
 * Deletes a guest property.
 *
 * @returns VBox status code.
 * @param   idClient        The client id returned by VbglR3InvsSvcConnect().
 * @param   pszName         The property to delete.  Utf8
 */
VBGLR3DECL(int) VbglR3GuestPropDelete(HGCMCLIENTID idClient, const char *pszName)
{
    AssertPtrReturn(pszName,  VERR_INVALID_POINTER);

    GuestPropMsgDelProperty Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, GUEST_PROP_FN_DEL_PROP, 1);
    VbglHGCMParmPtrSetString(&Msg.name, pszName);
    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}


/**
 * Deletes a set of keys.
 *
 * The set is specified in the same way as for VbglR3GuestPropEnum.
 *
 * @returns VBox status code. Stops on first failure.
 *          See also VbglR3GuestPropEnum.
 *
 * @param   idClient        The client id returned by VbglR3InfoSvcConnect().
 * @param   papszPatterns   The patterns against which the properties are
 *                          matched.  Pass NULL if everything should be matched.
 * @param   cPatterns       The number of patterns in @a papszPatterns.  0 means
 *                          match everything.
 */
VBGLR3DECL(int) VbglR3GuestPropDelSet(HGCMCLIENTID idClient,
                                      const char * const *papszPatterns,
                                      uint32_t cPatterns)
{
    PVBGLR3GUESTPROPENUM pHandle;
    char const *pszName, *pszValue, *pszFlags;
    uint64_t pu64Timestamp;
    int rc = VbglR3GuestPropEnum(idClient,
                                 (char **)papszPatterns, /** @todo fix this cast. */
                                 cPatterns,
                                 &pHandle,
                                 &pszName,
                                 &pszValue,
                                 &pu64Timestamp,
                                 &pszFlags);

    while (RT_SUCCESS(rc) && pszName)
    {
        rc = VbglR3GuestPropWriteValue(idClient, pszName, NULL);
        if (RT_FAILURE(rc))
            break;

        rc = VbglR3GuestPropEnumNext(pHandle,
                                     &pszName,
                                     &pszValue,
                                     &pu64Timestamp,
                                     &pszFlags);
    }

    VbglR3GuestPropEnumFree(pHandle);
    return rc;
}


/**
 * Wait for notification of changes to a guest property.  If this is called in
 * a loop, the timestamp of the last notification seen can be passed as a
 * parameter to be sure that no notifications are missed.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success, @a ppszName, @a ppszValue,
 *          @a pu64Timestamp and @a ppszFlags containing valid data.
 * @retval  VINF_NOT_FOUND if no previous notification could be found with the
 *          timestamp supplied.  This will normally mean that a large number
 *          of notifications occurred in between.
 * @retval  VERR_BUFFER_OVERFLOW if the scratch buffer @a pvBuf is not large
 *          enough.  In this case the size needed will be placed in
 *          @a pcbBufActual if it is not NULL.
 * @retval  VERR_TIMEOUT if a timeout occurred before a notification was seen.
 *
 * @param   idClient        The client id returned by VbglR3GuestPropConnect().
 * @param   pszPatterns     The patterns that the property names must matchfor
 *                          the change to be reported.
 * @param   pvBuf           A scratch buffer to store the data retrieved into.
 *                          The returned data is only valid for it's lifetime.
 *                          @a ppszValue will point to the start of this buffer.
 * @param   cbBuf           The size of @a pvBuf
 * @param   u64Timestamp    The timestamp of the last event seen.  Pass zero
 *                          to wait for the next event.
 * @param   cMillies        Timeout in milliseconds.  Use RT_INDEFINITE_WAIT
 *                          to wait indefinitely.
 * @param   ppszName        Where to store the pointer to the name retrieved.
 *                          Optional.
 * @param   ppszValue       Where to store the pointer to the value retrieved.
 *                          Optional.
 * @param   pu64Timestamp   Where to store the timestamp.  Optional.
 * @param   ppszFlags       Where to store the pointer to the flags.  Optional.
 * @param   pcbBufActual    If @a pcBuf is not large enough, the size needed.
 *                          Optional.
 * @param   pfWasDeleted    A flag which indicates that property was deleted.
 *                          Optional.
 */
VBGLR3DECL(int) VbglR3GuestPropWait(HGCMCLIENTID idClient,
                                    const char *pszPatterns,
                                    void *pvBuf, uint32_t cbBuf,
                                    uint64_t u64Timestamp, uint32_t cMillies,
                                    char ** ppszName, char **ppszValue,
                                    uint64_t *pu64Timestamp, char **ppszFlags,
                                    uint32_t *pcbBufActual, bool *pfWasDeleted)
{
    /*
     * Create the GET_NOTIFICATION message and call the host.
     */
    GuestPropMsgGetNotification Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT_TIMED(&Msg.hdr, idClient, GUEST_PROP_FN_GET_NOTIFICATION, 4, cMillies);

    VbglHGCMParmPtrSetString(&Msg.patterns, pszPatterns);
    RT_BZERO(pvBuf, cbBuf);
    VbglHGCMParmPtrSet(&Msg.buffer, pvBuf, cbBuf);
    VbglHGCMParmUInt64Set(&Msg.timestamp, u64Timestamp);
    VbglHGCMParmUInt32Set(&Msg.size, 0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    /*
     * The cbBufActual parameter is also returned on overflow so the caller can
     * adjust their buffer.
     */
    if (    rc == VERR_BUFFER_OVERFLOW
        &&  pcbBufActual != NULL)
    {
        int rc2 = Msg.size.GetUInt32(pcbBufActual);
        AssertRCReturn(rc2, RT_FAILURE(rc) ? rc : rc2);
    }
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Buffer layout: Name\0Value\0Flags\0fWasDeleted\0.
     *
     * If the caller cares about any of these strings, make sure things are
     * properly terminated (paranoia).
     */
    if (    RT_SUCCESS(rc)
        &&  (ppszName != NULL || ppszValue != NULL || ppszFlags != NULL || pfWasDeleted != NULL))
    {
        /* Validate / skip 'Name'. */
        char *pszValue = RTStrEnd((char *)pvBuf, cbBuf) + 1;
        AssertPtrReturn(pszValue, VERR_TOO_MUCH_DATA);
        if (ppszName)
            *ppszName = (char *)pvBuf;

        /* Validate / skip 'Value'. */
        char *pszFlags = RTStrEnd(pszValue, cbBuf - (pszValue - (char *)pvBuf)) + 1;
        AssertPtrReturn(pszFlags, VERR_TOO_MUCH_DATA);
        if (ppszValue)
            *ppszValue = pszValue;

        if (ppszFlags)
            *ppszFlags = pszFlags;

        /* Skip 'Flags' and deal with 'fWasDeleted' if it's present. */
        char *pszWasDeleted = RTStrEnd(pszFlags, cbBuf - (pszFlags - (char *)pvBuf)) + 1;
        AssertPtrReturn(pszWasDeleted, VERR_TOO_MUCH_DATA);
        char chWasDeleted = 0;
        if (   (size_t)pszWasDeleted - (size_t)pvBuf < cbBuf
            && (chWasDeleted = *pszWasDeleted) != '\0')
            AssertMsgReturn((chWasDeleted == '0' || chWasDeleted == '1') && pszWasDeleted[1] == '\0',
                            ("'%s'\n", pszWasDeleted), VERR_PARSE_ERROR);
        if (pfWasDeleted)
            *pfWasDeleted = chWasDeleted == '1';
    }

    /* And the timestamp, if requested. */
    if (pu64Timestamp != NULL)
    {
        rc = Msg.timestamp.GetUInt64(pu64Timestamp);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}
#endif /* VBOX_VBGLR3_XSERVER */
