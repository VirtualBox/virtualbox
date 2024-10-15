/* $Id: VBoxNetFltNobj.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetFltNobj.h - Notify Object for Bridged Networking Driver.
 * Used to filter Bridged Networking Driver bindings
 */
/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_VBoxNetFlt_win_nobj_VBoxNetFltNobj_h
#define VBOX_INCLUDED_SRC_VBoxNetFlt_win_nobj_VBoxNetFltNobj_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/win/windows.h>

#include "VBox/com/defs.h"
#include "VBoxNetFltNobjT.h"
#include "VBoxNetFltNobjRc.h"

#define VBOXNETFLTNOTIFY_ONFAIL_BINDDEFAULT false

/*
 * VirtualBox Bridging driver notify object.
 * Needed to make our driver bind to "real" host adapters only
 */
class ATL_NO_VTABLE VBoxNetFltNobj
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , public ATL::CComCoClass<VBoxNetFltNobj, &CLSID_VBoxNetFltNobj>
    , public INetCfgComponentControl
    , public INetCfgComponentNotifyBinding
{
public:
    VBoxNetFltNobj();
    virtual ~VBoxNetFltNobj();

    BEGIN_COM_MAP(VBoxNetFltNobj)
        COM_INTERFACE_ENTRY(INetCfgComponentControl)
        COM_INTERFACE_ENTRY(INetCfgComponentNotifyBinding)
    END_COM_MAP()

    // this is a "just in case" conditional, which is not defined
#ifdef VBOX_FORCE_REGISTER_SERVER
    DECLARE_REGISTRY_RESOURCEID(IDR_VBOXNETFLT_NOBJ)
#endif

    /* INetCfgComponentControl methods */
    STDMETHOD(Initialize)(IN INetCfgComponent *pNetCfgComponent, IN INetCfg *pNetCfg, IN BOOL bInstalling);
    STDMETHOD(ApplyRegistryChanges)();
    STDMETHOD(ApplyPnpChanges)(IN INetCfgPnpReconfigCallback *pCallback);
    STDMETHOD(CancelChanges)();

    /* INetCfgComponentNotifyBinding methods */
    STDMETHOD(NotifyBindingPath)(IN DWORD dwChangeFlag, IN INetCfgBindingPath *pNetCfgBP);
    STDMETHOD(QueryBindingPath)(IN DWORD dwChangeFlag, IN INetCfgBindingPath *pNetCfgBP);
private:

    void init(IN INetCfgComponent *pNetCfgComponent, IN INetCfg *pNetCfg, IN BOOL bInstalling);
    void cleanup();

    /* these two used to maintain the component info passed to
     * INetCfgComponentControl::Initialize */
    INetCfg *mpNetCfg;
    INetCfgComponent *mpNetCfgComponent;
    BOOL mbInstalling;
};

#endif /* !VBOX_INCLUDED_SRC_VBoxNetFlt_win_nobj_VBoxNetFltNobj_h */
