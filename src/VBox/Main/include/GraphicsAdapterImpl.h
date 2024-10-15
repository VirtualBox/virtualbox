/* $Id: GraphicsAdapterImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Implementation of IGraphicsAdapter in VBoxSVC - Header.
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

#ifndef MAIN_INCLUDED_GraphicsAdapterImpl_h
#define MAIN_INCLUDED_GraphicsAdapterImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GraphicsAdapterWrap.h"


namespace settings
{
    struct GraphicsAdapter;
};


class ATL_NO_VTABLE GraphicsAdapter :
    public GraphicsAdapterWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(GraphicsAdapter)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aParent);
    HRESULT init(Machine *aParent, GraphicsAdapter *aThat);
    HRESULT initCopy(Machine *aParent, GraphicsAdapter *aThat);
    void uninit();


    // public methods only for internal purposes
    HRESULT i_loadSettings(const settings::GraphicsAdapter &data);
    HRESULT i_saveSettings(settings::GraphicsAdapter &data);

    void  i_rollback();
    void  i_commit();
    void  i_copyFrom(GraphicsAdapter *aThat);
    bool *i_getFeatureMemberBool(GraphicsFeature_T aFeature);
    void  i_updateFeatures();

private:

    // wrapped IGraphicsAdapter properties
    HRESULT getGraphicsControllerType(GraphicsControllerType_T *aGraphicsControllerType);
    HRESULT setGraphicsControllerType(GraphicsControllerType_T aGraphicsControllerType);
    HRESULT getVRAMSize(ULONG *aVRAMSize);
    HRESULT setVRAMSize(ULONG aVRAMSize);
    HRESULT setFeature(GraphicsFeature_T aFeature, BOOL aEnabled);
    HRESULT isFeatureEnabled(GraphicsFeature_T aFeature, BOOL *aEnabled);
    HRESULT getMonitorCount(ULONG *aMonitorCount);
    HRESULT setMonitorCount(ULONG aMonitorCount);

    Machine * const     mParent;
    const ComObjPtr<GraphicsAdapter> mPeer;
    Backupable<settings::GraphicsAdapter> mData;
};

#endif /* !MAIN_INCLUDED_GraphicsAdapterImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
