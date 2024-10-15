/* $Id: MediumFormatImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MediumFormat COM class implementation
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

#define LOG_GROUP LOG_GROUP_MAIN_MEDIUMFORMAT
#include "MediumFormatImpl.h"
#include "AutoCaller.h"
#include "LoggingNew.h"

#include <VBox/vd.h>

#include <iprt/cpp/utils.h>

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(MediumFormat)

HRESULT MediumFormat::FinalConstruct()
{
    return BaseFinalConstruct();
}

void MediumFormat::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the hard disk format object.
 *
 * @param aVDInfo  Pointer to a backend info object.
 */
HRESULT MediumFormat::init(const VDBACKENDINFO *aVDInfo)
{
    LogFlowThisFunc(("aVDInfo=%p\n", aVDInfo));

    ComAssertRet(aVDInfo, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* The ID of the backend */
    unconst(m.strId) = aVDInfo->pszBackend;
    /* The Name of the backend */
    /* Use id for now as long as VDBACKENDINFO hasn't any extra
     * name/description field. */
    unconst(m.strName) = aVDInfo->pszBackend;
    /* The capabilities of the backend. Assumes 1:1 mapping! */
    unconst(m.capabilities) = (MediumFormatCapabilities_T)aVDInfo->uBackendCaps;
    /* Save the supported file extensions in a list */
    if (aVDInfo->paFileExtensions)
    {
        PCVDFILEEXTENSION papExtension = aVDInfo->paFileExtensions;
        while (papExtension->pszExtension != NULL)
        {
            DeviceType_T devType;

            unconst(m.maFileExtensions).push_back(papExtension->pszExtension);

            switch(papExtension->enmType)
            {
                case VDTYPE_HDD:
                    devType = DeviceType_HardDisk;
                    break;
                case VDTYPE_OPTICAL_DISC:
                    devType = DeviceType_DVD;
                    break;
                case VDTYPE_FLOPPY:
                    devType = DeviceType_Floppy;
                    break;
                default:
                    AssertMsgFailed(("Invalid enm type %d!\n", papExtension->enmType));
                    return E_INVALIDARG;
            }

            unconst(m.maDeviceTypes).push_back(devType);
            ++papExtension;
        }
    }
    /* Save a list of configure properties */
    if (aVDInfo->paConfigInfo)
    {
        PCVDCONFIGINFO pa = aVDInfo->paConfigInfo;
        /* Walk through all available keys */
        while (pa->pszKey != NULL)
        {
            Utf8Str defaultValue("");
            DataType_T dt;
            ULONG flags = static_cast<ULONG>(pa->uKeyFlags);
            /* Check for the configure data type */
            switch (pa->enmValueType)
            {
                case VDCFGVALUETYPE_INTEGER:
                {
                    dt = DataType_Int32;
                    /* If there is a default value get them in the right format */
                    if (pa->pszDefaultValue)
                        defaultValue = pa->pszDefaultValue;
                    break;
                }
                case VDCFGVALUETYPE_BYTES:
                {
                    dt = DataType_Int8;
                    /* If there is a default value get them in the right format */
                    if (pa->pszDefaultValue)
                    {
                        /* Copy the bytes over - treated simply as a string */
                        defaultValue = pa->pszDefaultValue;
                        flags |= DataFlags_Array;
                    }
                    break;
                }
                case VDCFGVALUETYPE_STRING:
                {
                    dt = DataType_String;
                    /* If there is a default value get them in the right format */
                    if (pa->pszDefaultValue)
                        defaultValue = pa->pszDefaultValue;
                    break;
                }

                default:
                    AssertMsgFailed(("Invalid enm type %d!\n", pa->enmValueType));
                    return E_INVALIDARG;
            }

            /// @todo add extendedFlags to Property when we reach the 32 bit
            /// limit (or make the argument ULONG64 after checking that COM is
            /// capable of defining enums (used to represent bit flags) that
            /// contain 64-bit values)
            ComAssertRet(pa->uKeyFlags == ((ULONG)pa->uKeyFlags), E_FAIL);

            /* Create one property structure */
            const Property prop = { Utf8Str(pa->pszKey),
                                    Utf8Str(""),
                                    dt,
                                    flags,
                                    defaultValue };
            unconst(m.maProperties).push_back(prop);
            ++pa;
        }
    }

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Uninitializes the instance and sets the ready flag to FALSE.
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void MediumFormat::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(m.maProperties).clear();
    unconst(m.maFileExtensions).clear();
    unconst(m.maDeviceTypes).clear();
    unconst(m.capabilities) = (MediumFormatCapabilities_T)0;
    unconst(m.strName).setNull();
    unconst(m.strId).setNull();
}

// IMediumFormat properties
/////////////////////////////////////////////////////////////////////////////

HRESULT MediumFormat::getId(com::Utf8Str &aId)
{
    /* this is const, no need to lock */
    aId = m.strId;

    return S_OK;
}

HRESULT MediumFormat::getName(com::Utf8Str &aName)
{
    /* this is const, no need to lock */
    aName = m.strName;

    return S_OK;
}

HRESULT MediumFormat::getCapabilities(std::vector<MediumFormatCapabilities_T> &aCapabilities)
{
    /* m.capabilities is const, no need to lock */

    aCapabilities.resize(sizeof(MediumFormatCapabilities_T) * 8);
    size_t cCapabilities = 0;
    for (size_t i = 0; i < aCapabilities.size(); i++)
    {
        uint64_t tmp = m.capabilities;
        tmp &= 1ULL << i;
        if (tmp)
            aCapabilities[cCapabilities++] = (MediumFormatCapabilities_T)tmp;
    }
    aCapabilities.resize(RT_MAX(cCapabilities, 1));

    return S_OK;
}

// IMediumFormat methods
/////////////////////////////////////////////////////////////////////////////

HRESULT MediumFormat::describeFileExtensions(std::vector<com::Utf8Str> &aExtensions,
                                             std::vector<DeviceType_T> &aTypes)
{
    /* this is const, no need to lock */
    aExtensions = m.maFileExtensions;
    aTypes = m.maDeviceTypes;

    return S_OK;
}

HRESULT MediumFormat::describeProperties(std::vector<com::Utf8Str> &aNames,
                                         std::vector<com::Utf8Str> &aDescriptions,
                                         std::vector<DataType_T> &aTypes,
                                         std::vector<ULONG> &aFlags,
                                         std::vector<com::Utf8Str> &aDefaults)
{
    /* this is const, no need to lock */
    size_t c = m.maProperties.size();
    aNames.resize(c);
    aDescriptions.resize(c);
    aTypes.resize(c);
    aFlags.resize(c);
    aDefaults.resize(c);
    for (size_t i = 0; i < c; i++)
    {
        const Property &prop = m.maProperties[i];
        aNames[i] = prop.strName;
        aDescriptions[i] = prop.strDescription;
        aTypes[i] = prop.type;
        aFlags[i] = prop.flags;
        aDefaults[i] = prop.strDefaultValue;
    }

    return S_OK;
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
