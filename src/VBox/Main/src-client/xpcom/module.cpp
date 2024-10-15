/* $Id: module.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * XPCOM module implementation functions
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

#define LOG_GROUP LOG_GROUP_MAIN

/* Make sure all the stdint.h macros are included - must come first! */
#ifndef __STDC_LIMIT_MACROS
# define __STDC_LIMIT_MACROS
#endif
#ifndef __STDC_CONSTANT_MACROS
# define __STDC_CONSTANT_MACROS
#endif

#include <nsIGenericFactory.h>

// generated file
#include <VBox/com/VirtualBox.h>

#include "SessionImpl.h"
#include "VirtualBoxClientImpl.h"
#include "RemoteUSBDeviceImpl.h"
#include "USBDeviceImpl.h"

// XPCOM glue code unfolding

/*
 * Declare extern variables here to tell the compiler that
 * NS_DECL_CLASSINFO(SessionWrap)
 * already exists in the VBoxAPIWrap library.
 */
NS_DECL_CI_INTERFACE_GETTER(SessionWrap)
extern nsIClassInfo *NS_CLASSINFO_NAME(SessionWrap);

/*
 * Declare extern variables here to tell the compiler that
 * NS_DECL_CLASSINFO(VirtualBoxClientWrap)
 * already exists in the VBoxAPIWrap library.
 */
NS_DECL_CI_INTERFACE_GETTER(VirtualBoxClientWrap)
extern nsIClassInfo *NS_CLASSINFO_NAME(VirtualBoxClientWrap);

/**
 *  Singleton class factory that holds a reference to the created instance
 *  (preventing it from being destroyed) until the module is explicitly
 *  unloaded by the XPCOM shutdown code.
 *
 *  Suitable for IN-PROC components.
 */
class VirtualBoxClientClassFactory : public VirtualBoxClient
{
public:
    virtual ~VirtualBoxClientClassFactory()
    {
        FinalRelease();
        instance = 0;
    }

    static nsresult GetInstance(VirtualBoxClient **inst)
    {
        int rv = NS_OK;
        if (instance == 0)
        {
            instance = new VirtualBoxClientClassFactory();
            if (instance)
            {
                instance->AddRef(); // protect FinalConstruct()
                rv = instance->FinalConstruct();
                if (NS_FAILED(rv))
                    instance->Release();
                else
                    instance->AddRef(); // self-reference
            }
            else
            {
                rv = NS_ERROR_OUT_OF_MEMORY;
            }
        }
        else
        {
            instance->AddRef();
        }
        *inst = instance;
        return rv;
    }

    static nsresult FactoryDestructor()
    {
        if (instance)
            instance->Release();
        return NS_OK;
    }

private:
    static VirtualBoxClient *instance;
};

VirtualBoxClient *VirtualBoxClientClassFactory::instance = nsnull;


NS_GENERIC_FACTORY_CONSTRUCTOR_WITH_RC(Session)

NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR_WITH_RC(VirtualBoxClient, VirtualBoxClientClassFactory::GetInstance)

/**
 *  Component definition table.
 *  Lists all components defined in this module.
 */
static const nsModuleComponentInfo components[] =
{
    {
        "Session component", // description
        NS_SESSION_CID, NS_SESSION_CONTRACTID, // CID/ContractID
        SessionConstructor, // constructor function
        NULL, // registration function
        NULL, // deregistration function
        NULL, // destructor function
        NS_CI_INTERFACE_GETTER_NAME(SessionWrap), // interfaces function
        NULL, // language helper
        &NS_CLASSINFO_NAME(SessionWrap) // global class info & flags
    },
    {
        "VirtualBoxClient component", // description
        NS_VIRTUALBOXCLIENT_CID, NS_VIRTUALBOXCLIENT_CONTRACTID, // CID/ContractID
        VirtualBoxClientConstructor, // constructor function
        NULL, // registration function
        NULL, // deregistration function
        VirtualBoxClientClassFactory::FactoryDestructor, // destructor function
        NS_CI_INTERFACE_GETTER_NAME(VirtualBoxClientWrap), // interfaces function
        NULL, // language helper
        &NS_CLASSINFO_NAME(VirtualBoxClientWrap) // global class info & flags
    },
};

NS_IMPL_NSGETMODULE (VirtualBox_Client_Module, components)
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
