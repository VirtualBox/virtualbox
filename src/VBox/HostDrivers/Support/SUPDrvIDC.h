/* $Id: SUPDrvIDC.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Driver - Inter-Driver Communication (IDC) definitions.
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

#ifndef VBOX_INCLUDED_SRC_Support_SUPDrvIDC_h
#define VBOX_INCLUDED_SRC_Support_SUPDrvIDC_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>

/** @def SUP_IDC_CODE
 * Creates IDC function code.
 *
 * @param Function      The function number to encode, 1..255.
 *
 * @remarks We can take a slightly more relaxed attitude wrt to size encoding
 *          here since only windows will use standard I/O control function code.
 *
 * @{
 */

#ifdef RT_OS_WINDOWS
# define SUP_IDC_CODE(Function)                 CTL_CODE(FILE_DEVICE_UNKNOWN, (Function) + 2542, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#else
# define SUP_IDC_CODE(Function)                 ( UINT32_C(0xc0ffee00) | (uint32_t)(0x000000ff & (Function)) )
#endif


#ifdef RT_ARCH_AMD64
# pragma pack(8)                        /* paranoia. */
#else
# pragma pack(4)                        /* paranoia. */
#endif


/**
 * An IDC request packet header.
 *
 * The main purpose of this header is to pass the session handle
 * and status code in a generic manner in order to make things
 * easier on the receiving end.
 */
typedef struct SUPDRVIDCREQHDR
{
    /** IN: The size of the request. */
    uint32_t                cb;
    /** OUT: Status code of the request. */
    int32_t                 rc;
    /** IN: Pointer to the session handle. */
    PSUPDRVSESSION          pSession;
#if ARCH_BITS == 32
    /** Padding the structure to 16-bytes. */
    uint32_t                u32Padding;
#endif
} SUPDRVIDCREQHDR;
/** Pointer to an IDC request packet header. */
typedef SUPDRVIDCREQHDR *PSUPDRVIDCREQHDR;
/** Pointer to a const IDC request packet header. */
typedef SUPDRVIDCREQHDR const *PCSUPDRVIDCREQHDR;


/**
 * SUPDRV IDC: Connect request.
 * This request takes a SUPDRVIDCREQCONNECT packet.
 */
#define SUPDRV_IDC_REQ_CONNECT                          SUP_IDC_CODE(1)
/** A SUPDRV IDC connect request packet. */
typedef struct SUPDRVIDCREQCONNECT
{
    /** The request header. */
    SUPDRVIDCREQHDR         Hdr;
    /** The payload union. */
    union
    {
        /** The input. */
        struct SUPDRVIDCREQCONNECTIN
        {
            /** The magic cookie (SUPDRVIDCREQ_CONNECT_MAGIC_COOKIE). */
            uint32_t        u32MagicCookie;
            /** The desired version of the IDC interface. */
            uint32_t        uReqVersion;
            /** The minimum version of the IDC interface. */
            uint32_t        uMinVersion;
        } In;

        /** The output. */
        struct SUPDRVIDCREQCONNECTOUT
        {
            /** The support driver session. (An opaque.) */
            PSUPDRVSESSION  pSession;
            /** The version of the IDC interface for this session. */
            uint32_t        uSessionVersion;
            /** The version of the IDC interface . */
            uint32_t        uDriverVersion;
            /** The SVN revision of the driver.
             * This will be set to 0 if not compiled into the driver. */
            uint32_t        uDriverRevision;
        } Out;
    } u;
} SUPDRVIDCREQCONNECT;
/** Pointer to a SUPDRV IDC connect request. */
typedef SUPDRVIDCREQCONNECT *PSUPDRVIDCREQCONNECT;
/** Magic cookie value (SUPDRVIDCREQCONNECT::In.u32MagicCookie). ('tori') */
#define SUPDRVIDCREQ_CONNECT_MAGIC_COOKIE               UINT32_C(0x69726f74)


/**
 * SUPDRV IDC: Disconnect request.
 * This request only requires a SUPDRVIDCREQHDR.
 */
#define SUPDRV_IDC_REQ_DISCONNECT                       SUP_IDC_CODE(2)


/**
 * SUPDRV IDC: Query a symbol address.
 * This request takes a SUPDRVIDCREQGETSYM packet.
 */
#define SUPDRV_IDC_REQ_GET_SYMBOL                       SUP_IDC_CODE(3)
/** A SUPDRV IDC get symbol request packet. */
typedef struct SUPDRVIDCREQGETSYM
{
    /** The request header. */
    SUPDRVIDCREQHDR         Hdr;
    /** The payload union. */
    union
    {
        /** The input. */
        struct SUPDRVIDCREQGETSYMIN
        {
            /** The module name.
             * NULL is an alias for the support driver. */
            const char     *pszModule;
            /** The symbol name. */
            const char     *pszSymbol;
        } In;

        /** The output. */
        struct SUPDRVIDCREQGETSYMOUT
        {
            /** The symbol address. */
            PFNRT           pfnSymbol;
        } Out;
    } u;
} SUPDRVIDCREQGETSYM;
/** Pointer to a SUPDRV IDC get symbol request. */
typedef SUPDRVIDCREQGETSYM *PSUPDRVIDCREQGETSYM;


/**
 * SUPDRV IDC: Request the registration of a component factory.
 * This request takes a SUPDRVIDCREQCOMPREGFACTORY packet.
 */
#define SUPDRV_IDC_REQ_COMPONENT_REGISTER_FACTORY       SUP_IDC_CODE(10)
/** A SUPDRV IDC register component factory request packet. */
typedef struct SUPDRVIDCREQCOMPREGFACTORY
{
    /** The request header. */
    SUPDRVIDCREQHDR         Hdr;
    /** The payload union. */
    union
    {
        /** The input. */
        struct SUPDRVIDCREQCOMPREGFACTORYIN
        {
            /** Pointer to the factory. */
            PCSUPDRVFACTORY pFactory;
        } In;
    } u;
} SUPDRVIDCREQCOMPREGFACTORY;
/** Pointer to a SUPDRV IDC register component factory request. */
typedef SUPDRVIDCREQCOMPREGFACTORY *PSUPDRVIDCREQCOMPREGFACTORY;


/**
 * SUPDRV IDC: Deregister a component factory.
 * This request takes a SUPDRVIDCREQCOMPDEREGFACTORY packet.
 */
#define SUPDRV_IDC_REQ_COMPONENT_DEREGISTER_FACTORY     SUP_IDC_CODE(11)
/** A SUPDRV IDC deregister component factory request packet. */
typedef struct SUPDRVIDCREQCOMPDEREGFACTORY
{
    /** The request header. */
    SUPDRVIDCREQHDR         Hdr;
    /** The payload union. */
    union
    {
        /** The input. */
        struct SUPDRVIDCREQCOMPDEREGFACTORYIN
        {
            /** Pointer to the factory. */
            PCSUPDRVFACTORY pFactory;
        } In;
    } u;
} SUPDRVIDCREQCOMPDEREGFACTORY;
/** Pointer to a SUPDRV IDC deregister component factory request. */
typedef SUPDRVIDCREQCOMPDEREGFACTORY *PSUPDRVIDCREQCOMPDEREGFACTORY;


/*
 * The OS specific prototypes.
 * Most OSes uses
 */
RT_C_DECLS_BEGIN

#if defined(RT_OS_DARWIN)
# ifdef IN_SUP_R0
extern DECLEXPORT(int) VBOXCALL SUPDrvDarwinIDC(uint32_t iReq, PSUPDRVIDCREQHDR pReq);
# else
extern DECLIMPORT(int) VBOXCALL SUPDrvDarwinIDC(uint32_t iReq, PSUPDRVIDCREQHDR pReq);
# endif

#elif defined(RT_OS_FREEBSD)
extern int VBOXCALL SUPDrvFreeBSDIDC(uint32_t iReq, PSUPDRVIDCREQHDR pReq);

#elif defined(RT_OS_LINUX)
extern int VBOXCALL SUPDrvLinuxIDC(uint32_t iReq, PSUPDRVIDCREQHDR pReq);

#elif defined(RT_OS_OS2)
/** @todo Port to OS/2. */

#elif defined(RT_OS_SOLARIS)
extern int VBOXCALL SUPDrvSolarisIDC(uint32_t iReq, PSUPDRVIDCREQHDR pReq);

#elif defined(RT_OS_WINDOWS)
/* Nothing special for windows. */

#else
/* PORTME: OS specific IDC stuff goes here. */
#endif

RT_C_DECLS_END

/**
 * The SUPDRV IDC entry point.
 *
 * @returns VBox status code indicating the validity of the session, request and
 *          the return data packet. The status of the request it self is found
 *          in the packet (specific to each request).
 *
 * @param   pSession    The session. (This is NULL for SUPDRV_IDC_REQ_CONNECT.)
 * @param   uReq        The request number.
 * @param   pvReq       Pointer to the request packet. Optional for some requests.
 * @param   cbReq       The size of the request packet.
 */
/** @todo move this and change to function proto */
typedef DECLCALLBACKTYPE(int, FNSUPDRVIDCENTRY,(PSUPDRVSESSION pSession, uint32_t uReq, void *pvReq, uint32_t cbReq));

/** @} */


#pragma pack()                          /* paranoia */

#endif /* !VBOX_INCLUDED_SRC_Support_SUPDrvIDC_h */

