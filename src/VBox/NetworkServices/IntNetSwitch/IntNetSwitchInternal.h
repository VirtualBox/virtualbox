/* $Id: IntNetSwitchInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox internal network switch process - Internal header.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_IntNetSwitch_IntNetSwitchInternal_h
#define VBOX_INCLUDED_SRC_IntNetSwitch_IntNetSwitchInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define IN_INTNET_TESTCASE
#define IN_INTNET_R3

#include <VBox/cdefs.h>
#include <VBox/types.h>

#undef INTNETR0DECL
#define INTNETR0DECL INTNETR3DECL
#undef DECLR0CALLBACKMEMBER
#define DECLR0CALLBACKMEMBER(type, name, args) DECLR3CALLBACKMEMBER(type, name, args)
typedef struct SUPDRVSESSION *MYPSUPDRVSESSION;
#define PSUPDRVSESSION  MYPSUPDRVSESSION

#include <VBox/intnet.h>
#include <VBox/sup.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Security objectype.
 */
typedef enum SUPDRVOBJTYPE
{
    /** The usual invalid object. */
    SUPDRVOBJTYPE_INVALID = 0,
    /** Internal network. */
    SUPDRVOBJTYPE_INTERNAL_NETWORK,
    /** Internal network interface. */
    SUPDRVOBJTYPE_INTERNAL_NETWORK_INTERFACE,
    /** The first invalid object type in this end. */
    SUPDRVOBJTYPE_END,
    /** The usual 32-bit type size hack. */
    SUPDRVOBJTYPE_32_BIT_HACK = 0x7ffffff
} SUPDRVOBJTYPE;

/**
 * Object destructor callback.
 * This is called for reference counted objectes when the count reaches 0.
 *
 * @param   pvObj       The object pointer.
 * @param   pvUser1     The first user argument.
 * @param   pvUser2     The second user argument.
 */
typedef DECLCALLBACKTYPE(void, FNSUPDRVDESTRUCTOR,(void *pvObj, void *pvUser1, void *pvUser2));
/** Pointer to a FNSUPDRVDESTRUCTOR(). */
typedef FNSUPDRVDESTRUCTOR *PFNSUPDRVDESTRUCTOR;


RT_C_DECLS_BEGIN

INTNETR3DECL(void *) SUPR0ObjRegister(PSUPDRVSESSION pSession, SUPDRVOBJTYPE enmType,
                                      PFNSUPDRVDESTRUCTOR pfnDestructor, void *pvUser1, void *pvUser2);
INTNETR3DECL(int) SUPR0ObjAddRefEx(void *pvObj, PSUPDRVSESSION pSession, bool fNoBlocking);
INTNETR3DECL(int) SUPR0ObjAddRef(void *pvObj, PSUPDRVSESSION pSession);
INTNETR3DECL(int) SUPR0ObjRelease(void *pvObj, PSUPDRVSESSION pSession);
INTNETR3DECL(int) SUPR0ObjVerifyAccess(void *pvObj, PSUPDRVSESSION pSession, const char *pszObjName);
INTNETR3DECL(int) SUPR0MemAlloc(PSUPDRVSESSION pSession, uint32_t cb, PRTR0PTR ppvR0, PRTR3PTR ppvR3);
INTNETR3DECL(int) SUPR0MemFree(PSUPDRVSESSION pSession, RTHCUINTPTR uPtr);


RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_IntNetSwitch_IntNetSwitchInternal_h */

