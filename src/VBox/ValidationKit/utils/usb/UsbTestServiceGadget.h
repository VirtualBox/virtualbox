/* $Id: UsbTestServiceGadget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbTestServ - Remote USB test configuration and execution server, Gadget API.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_usb_UsbTestServiceGadget_h
#define VBOX_INCLUDED_SRC_usb_UsbTestServiceGadget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>

RT_C_DECLS_BEGIN

/** Opaque gadget host handle. */
typedef struct UTSGADGETHOSTINT *UTSGADGETHOST;
/** Pointer to a gadget host handle. */
typedef UTSGADGETHOST *PUTSGADGETHOST;

/** NIL gadget host handle. */
#define NIL_UTSGADGETHOST ((UTSGADGETHOST)0)

/** Opaque USB gadget handle. */
typedef struct UTSGADGETINT *UTSGADGET;
/** Pointer to a USB gadget handle. */
typedef UTSGADGET *PUTSGADET;

/** NIL gadget handle. */
#define NIL_UTSGADGET ((UTSGADGET)0)

/**
 * Gadget/Gadget host configuration item type.
 */
typedef enum UTSGADGETCFGTYPE
{
    /** Don't use! */
    UTSGADGETCFGTYPE_INVALID = 0,
    /** Boolean type. */
    UTSGADGETCFGTYPE_BOOLEAN,
    /** UTF-8 string. */
    UTSGADGETCFGTYPE_STRING,
    /** Unsigned 8bit integer. */
    UTSGADGETCFGTYPE_UINT8,
    /** Unsigned 16bit integer. */
    UTSGADGETCFGTYPE_UINT16,
    /** Unsigned 32bit integer. */
    UTSGADGETCFGTYPE_UINT32,
    /** Unsigned 64bit integer. */
    UTSGADGETCFGTYPE_UINT64,
    /** Signed 8bit integer. */
    UTSGADGETCFGTYPE_INT8,
    /** Signed 16bit integer. */
    UTSGADGETCFGTYPE_INT16,
    /** Signed 32bit integer. */
    UTSGADGETCFGTYPE_INT32,
    /** Signed 64bit integer. */
    UTSGADGETCFGTYPE_INT64,
    /** 32bit hack. */
    UTSGADGETCFGTYPE_32BIT_HACK = 0x7fffffff
} UTSGADGETCFGTYPE;

/**
 * Gadget configuration value.
 */
typedef struct UTSGADGETCFGVAL
{
    /** Value type */
    UTSGADGETCFGTYPE enmType;
    /** Value based on the type. */
    union
    {
        bool         f;
        const char  *psz;
        uint8_t      u8;
        uint16_t     u16;
        uint32_t     u32;
        uint64_t     u64;
        int8_t       i8;
        int16_t      i16;
        int32_t      i32;
        int64_t      i64;
    } u;
} UTSGADGETCFGVAL;
/** Pointer to a gadget configuration value. */
typedef UTSGADGETCFGVAL *PUTSGADGETCFGVAL;
/** Pointer to a const gadget configuration value. */
typedef const UTSGADGETCFGVAL *PCUTSGADGETCFGVAL;

/**
 * Gadget configuration item.
 */
typedef struct UTSGADGETCFGITEM
{
    /** Item key. */
    const char      *pszKey;
    /** Item value. */
    UTSGADGETCFGVAL  Val;
} UTSGADGETCFGITEM;
/** Pointer to a gadget configuration item. */
typedef UTSGADGETCFGITEM *PUTSGADGETCFGITEM;
/** Pointer to a const gadget configuration item. */
typedef const UTSGADGETCFGITEM *PCUTSGADGETCFGITEM;

/**
 * Type for the gadget host.
 */
typedef enum UTSGADGETHOSTTYPE
{
    /** Invalid type, don't use. */
    UTSGADGETHOSTTYPE_INVALID = 0,
    /** USB/IP host, gadgets are exported using a USB/IP server. */
    UTSGADGETHOSTTYPE_USBIP,
    /** Physical connection using a device or OTG port. */
    UTSGADGETHOSTTYPE_PHYSICAL,
    /** 32bit hack. */
    UTSGADGETHOSTTYPE_32BIT_HACK = 0x7fffffff
} UTSGADGETHOSTTYPE;

/**
 * USB gadget class.
 */
typedef enum UTSGADGETCLASS
{
    /** Invalid class, don't use. */
    UTSGADGETCLASS_INVALID = 0,
    /** Special test device class. */
    UTSGADGETCLASS_TEST,
    /** MSD device. */
    UTSGADGETCLASS_MSD,
    /** 32bit hack. */
    UTSGADGETCLASS_32BIT_HACK = 0x7fffffff
} UTSGADGETCLASS;

/**
 * Queries the value of a given boolean key from the given configuration array.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pf       Where to store the value on success.
 */
DECLHIDDEN(int) utsGadgetCfgQueryBool(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                      bool *pf);

/**
 * Queries the value of a given boolean key from the given configuration array,
 * setting a default if not found.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pf       Where to store the value on success.
 * @param   fDef     The default value to assign if the key is not found.
 */
DECLHIDDEN(int) utsGadgetCfgQueryBoolDef(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                         bool *pf, bool fDef);

/**
 * Queries the string value of a given key from the given configuration array.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   ppszVal  Where to store the pointer to the string on success,
 *                   must be freed with RTStrFree().
 */
DECLHIDDEN(int) utsGadgetCfgQueryString(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        char **ppszVal);

/**
 * Queries the string value of a given key from the given configuration array,
 * setting a default if not found.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   ppszVal  Where to store the pointer to the string on success,
 *                   must be freed with RTStrFree().
 * @param   pszDef   The default value to assign if the key is not found.
 */
DECLHIDDEN(int) utsGadgetCfgQueryStringDef(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                           char **ppszVal, const char *pszDef);

/**
 * Queries the value of a given uint8_t key from the given configuration array.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pu8      Where to store the value on success.
 */
DECLHIDDEN(int) utsGadgetCfgQueryU8(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                    uint8_t *pu8);

/**
 * Queries the value of a given uint8_t key from the given configuration array,
 * setting a default if not found.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pu8      Where to store the value on success.
 * @param   u8Def    The default value to assign if the key is not found.
 */
DECLHIDDEN(int) utsGadgetCfgQueryU8Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                       uint8_t *pu8, uint8_t u8Def);

/**
 * Queries the value of a given uint16_t key from the given configuration array.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pu16     Where to store the value on success.
 */
DECLHIDDEN(int) utsGadgetCfgQueryU16(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                     uint16_t *pu16);

/**
 * Queries the value of a given uint16_t key from the given configuration array,
 * setting a default if not found.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pu16     Where to store the value on success.
 * @param   u16Def   The default value to assign if the key is not found.
 */
DECLHIDDEN(int) utsGadgetCfgQueryU16Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        uint16_t *pu16, uint16_t u16Def);

/**
 * Queries the value of a given uint32_t key from the given configuration array.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pu32     Where to store the value on success.
 */
DECLHIDDEN(int) utsGadgetCfgQueryU32(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                     uint32_t *pu32);

/**
 * Queries the value of a given uint32_t key from the given configuration array,
 * setting a default if not found.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pu32     Where to store the value on success.
 * @param   u32Def   The default value to assign if the key is not found.
 */
DECLHIDDEN(int) utsGadgetCfgQueryU32Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        uint32_t *pu32, uint32_t u32Def);

/**
 * Queries the value of a given uint64_t key from the given configuration array.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pu64     Where to store the value on success.
 */
DECLHIDDEN(int) utsGadgetCfgQueryU64(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                     uint64_t *pu64);

/**
 * Queries the value of a given uint64_t key from the given configuration array,
 * setting a default if not found.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pu64     Where to store the value on success.
 * @param   u64Def   The default value to assign if the key is not found.
 */
DECLHIDDEN(int) utsGadgetCfgQueryU64Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        uint64_t *pu64, uint64_t u64Def);

/**
 * Queries the value of a given int8_t key from the given configuration array.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pi8      Where to store the value on success.
 */
DECLHIDDEN(int) utsGadgetCfgQueryS8(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                    int8_t *pi8);

/**
 * Queries the value of a given int8_t key from the given configuration array,
 * setting a default if not found.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pi8      Where to store the value on success.
 * @param   i8Def    The default value to assign if the key is not found.
 */
DECLHIDDEN(int) utsGadgetCfgQueryS8Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                       int8_t *pi8, uint8_t i8Def);

/**
 * Queries the value of a given int16_t key from the given configuration array.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pi16     Where to store the value on success.
 */
DECLHIDDEN(int) utsGadgetCfgQueryS16(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                     uint16_t *pi16);

/**
 * Queries the value of a given int16_t key from the given configuration array,
 * setting a default if not found.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pi16     Where to store the value on success.
 * @param   i16Def   The default value to assign if the key is not found.
 */
DECLHIDDEN(int) utsGadgetCfgQueryS16Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        uint16_t *pi16, uint16_t i16Def);

/**
 * Queries the value of a given int32_t key from the given configuration array.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pi32     Where to store the value on success.
 */
DECLHIDDEN(int) utsGadgetCfgQueryS32(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                     uint32_t *pi32);

/**
 * Queries the value of a given int32_t key from the given configuration array,
 * setting a default if not found.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pi32     Where to store the value on success.
 * @param   i32Def   The default value to assign if the key is not found.
 */
DECLHIDDEN(int) utsGadgetCfgQueryS32Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        uint32_t *pi32, uint32_t i32Def);

/**
 * Queries the value of a given int64_t key from the given configuration array.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pi64     Where to store the value on success.
 */
DECLHIDDEN(int) utsGadgetCfgQueryS64(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                     uint64_t *pi64);

/**
 * Queries the value of a given int64_t key from the given configuration array,
 * setting a default if not found.
 *
 * @returns IPRT status code.
 * @param   paCfg    The configuration items.
 * @param   pszKey   The key query the value for.
 * @param   pi64     Where to store the value on success.
 * @param   i64Def   The default value to assign if the key is not found.
 */
DECLHIDDEN(int) utsGadgetCfgQueryS64Def(PCUTSGADGETCFGITEM paCfg, const char *pszKey,
                                        uint64_t *pi64, uint64_t i64Def);

/**
 * Creates a new USB gadget host.
 *
 * @returns IPRT status code.
 * @param   enmType           The host type.
 * @param   paCfg             Additional configuration parameters - optional.
 *                            The array must be terminated with a NULL entry.
 * @param   phGadgetHost      Where to store the handle to the gadget host on success.
 */
DECLHIDDEN(int) utsGadgetHostCreate(UTSGADGETHOSTTYPE enmType, PCUTSGADGETCFGITEM paCfg,
                                    PUTSGADGETHOST phGadgetHost);

/**
 * Retains the given gadget host handle.
 *
 * @returns New reference count.
 * @param   hGadgetHost       The gadget host handle to retain.
 */
DECLHIDDEN(uint32_t) utsGadgetHostRetain(UTSGADGETHOST hGadgetHost);

/**
 * Releases the given gadget host handle, destroying it if the reference
 * count reaches 0.
 *
 * @returns New reference count.
 * @param   hGadgetHost       The gadget host handle to release.
 */
DECLHIDDEN(uint32_t) utsGadgetHostRelease(UTSGADGETHOST hGadgetHost);

/**
 * Returns the current config of the given gadget host.
 *
 * @returns Pointer to a constant array of configuration items for the given gadget host.
 * @param   hGadgetHost       The gadget host handle.
 */
DECLHIDDEN(PCUTSGADGETCFGITEM) utsGadgetHostGetCfg(UTSGADGETHOST hGadgetHost);

/**
 * Connects the given gadget to the host.
 *
 * @returns IPRT status code.
 * @param   hGadgetHost       The gadget host handle.
 * @param   hGadget           The gadget handle.
 */
DECLHIDDEN(int) utsGadgetHostGadgetConnect(UTSGADGETHOST hGadgetHost, UTSGADGET hGadget);

/**
 * Disconnects the given gadget from the host.
 *
 * @returns IPRT status code.
 * @param   hGadgetHost       The gadget host handle.
 * @param   hGadget           The gadget handle.
 */
DECLHIDDEN(int) utsGadgetHostGadgetDisconnect(UTSGADGETHOST hGadgetHost, UTSGADGET hGadget);

/**
 * Creates a new USB gadget based the class.
 *
 * @returns IPRT status code.
 * @param   hGadgetHost       The gadget host the gadget is part of.
 * @param   enmClass          The gadget class.
 * @param   paCfg             Array of optional configuration items for the gadget.
 * @param   phGadget          Where to store the gadget handle on success.
 */
DECLHIDDEN(int) utsGadgetCreate(UTSGADGETHOST hGadgetHost, UTSGADGETCLASS enmClass,
                                PCUTSGADGETCFGITEM paCfg, PUTSGADET phGadget);

/**
 * Retains the given gadget handle.
 *
 * @returns New reference count.
 * @param   hGadget       The gadget handle to retain.
 */
DECLHIDDEN(uint32_t) utsGadgetRetain(UTSGADGET hGadget);

/**
 * Releases the given gadget handle, destroying it if the reference
 * count reaches 0.
 *
 * @returns New reference count.
 * @param   hGadget           The gadget handle to destroy.
 */
DECLHIDDEN(uint32_t) utsGadgetRelease(UTSGADGET hGadget);

/**
 * Returns the current config of the given gadget.
 *
 * @returns Pointer to a constant array of configuration items for the given gadget.
 * @param   hGadget           The gadget handle.
 */
DECLHIDDEN(PCUTSGADGETCFGITEM) utsGadgetGetCfg(UTSGADGET hGadget);

/**
 * Returns the path of the given gadget from which it can be accessed.
 *
 * @returns Access path.
 * @param   hGadget           The gadget handle.
 */
DECLHIDDEN(const char *) utsGadgetGetAccessPath(UTSGADGET hGadget);

/**
 * Returns the bus ID the gadget is on.
 *
 * @returns Bus ID of the gadget.
 * @param   hGadget           The gadget handle.
 */
DECLHIDDEN(uint32_t) utsGadgetGetBusId(UTSGADGET hGadget);

/**
 * Returns the device ID of the gagdet.
 *
 * @returns Device ID of the gadget.
 * @param   hGadget           The gadget handle.
 */
DECLHIDDEN(uint32_t) utsGadgetGetDevId(UTSGADGET hGadget);

/**
 * Mark the gadget as connected to the host. Depending
 * on the host type it will be appear as physically attached
 * or will appear in the exported USB device list.
 *
 * @returns IPRT status code.
 * @param   hGadget           The gadget handle to connect.
 */
DECLHIDDEN(int) utsGadgetConnect(UTSGADGET hGadget);

/**
 * Mark the gadget as disconnected from the host.
 *
 * @returns IPRT status code.
 * @param   hGadget           The gadget handle to disconnect.
 */
DECLHIDDEN(int) utsGadgetDisconnect(UTSGADGET hGadget);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_usb_UsbTestServiceGadget_h */

