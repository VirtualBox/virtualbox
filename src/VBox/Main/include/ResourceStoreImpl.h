/* $Id: ResourceStoreImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM resource store class implementation
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_ResourceStoreImpl_h
#define MAIN_INCLUDED_ResourceStoreImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "ResourceStoreWrap.h"
#include <VBox/vmm/pdmdrv.h>

#include <iprt/vfs.h>


class Console;
struct DRVMAINRESOURCESTORE;

class ATL_NO_VTABLE ResourceStore :
    public ResourceStoreWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(ResourceStore)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Console *aParent);
    void uninit();

    int i_addItem(const char *pszNamespace, const char *pszPath, RTVFSFILE hVfsFile);

    // public methods for internal purposes only
    static const PDMDRVREG  DrvReg;

private:

    static DECLCALLBACK(int)    i_resourceStoreQuerySize(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                                         uint64_t *pcb);
    static DECLCALLBACK(int)    i_resourceStoreReadAll(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                                       void *pvBuf, size_t cbRead);
    static DECLCALLBACK(int)    i_resourceStoreWriteAll(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                                        const void *pvBuf, size_t cbWrite);
    static DECLCALLBACK(int)    i_resourceStoreDelete(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath);
    static DECLCALLBACK(void *) i_drvQueryInterface(PPDMIBASE pInterface, const char *pszIID);
    static DECLCALLBACK(int)    i_drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);
    static DECLCALLBACK(void)   i_drvDestruct(PPDMDRVINS pDrvIns);

    struct Data;            // opaque data struct, defined in ResourceStoreImpl.cpp
    Data *m;
};

#endif /* !MAIN_INCLUDED_ResourceStoreImpl_h */
