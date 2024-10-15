/* $Id: AdditionsFacilityImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Additions facility class.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_ADDITIONSFACILITY
#include "LoggingNew.h"

#include "AdditionsFacilityImpl.h"
#include "Global.h"

#include "AutoCaller.h"


/**
 * @note We ASSUME that unknown is the first entry!
 */
/* static */
const AdditionsFacility::FacilityInfo AdditionsFacility::s_aFacilityInfo[8] =
{
    { "Unknown",                        AdditionsFacilityType_None,             AdditionsFacilityClass_None },
    { "VirtualBox Base Driver",         AdditionsFacilityType_VBoxGuestDriver,  AdditionsFacilityClass_Driver },
    { "Auto Logon",                     AdditionsFacilityType_AutoLogon,        AdditionsFacilityClass_Feature },
    { "VirtualBox System Service",      AdditionsFacilityType_VBoxService,      AdditionsFacilityClass_Service },
    { "VirtualBox Desktop Integration", AdditionsFacilityType_VBoxTrayClient,   AdditionsFacilityClass_Program },
    { "Seamless Mode",                  AdditionsFacilityType_Seamless,         AdditionsFacilityClass_Feature },
    { "Graphics Mode",                  AdditionsFacilityType_Graphics,         AdditionsFacilityClass_Feature },
    { "Guest Monitor Attach",           AdditionsFacilityType_MonitorAttach,    AdditionsFacilityClass_Feature },
};

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(AdditionsFacility)

HRESULT AdditionsFacility::FinalConstruct()
{
    LogFlowThisFunc(("\n"));
    return BaseFinalConstruct();
}

void AdditionsFacility::FinalRelease()
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

HRESULT AdditionsFacility::init(Guest *a_pParent, AdditionsFacilityType_T a_enmFacility, AdditionsFacilityStatus_T a_enmStatus,
                                uint32_t a_fFlags, PCRTTIMESPEC a_pTimeSpecTS)
{
    RT_NOREF(a_pParent); /** @todo r=bird: For locking perhaps? */
    LogFlowThisFunc(("a_pParent=%p\n", a_pParent));

    /* Enclose the state transition NotReady->InInit->Ready. */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Initialize the data: */
    mData.mType         = a_enmFacility;
    mData.mStatus       = a_enmStatus;
    mData.mTimestamp    = *a_pTimeSpecTS;
    mData.mfFlags       = a_fFlags;
    mData.midxInfo      = 0;
    for (size_t i = 0; i < RT_ELEMENTS(s_aFacilityInfo); ++i)
        if (s_aFacilityInfo[i].mType == a_enmFacility)
        {
            mData.midxInfo = i;
            break;
        }

    /* Confirm a successful initialization when it's the case. */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Uninitializes the instance.
 *
 * Called from FinalRelease().
 */
void AdditionsFacility::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady. */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;
}

HRESULT AdditionsFacility::getClassType(AdditionsFacilityClass_T *aClassType)
{
    LogFlowThisFuncEnter();

    /* midxInfo is static, so no need to lock anything. */
    size_t idxInfo = mData.midxInfo;
    AssertStmt(idxInfo < RT_ELEMENTS(s_aFacilityInfo), idxInfo = 0);
    *aClassType = s_aFacilityInfo[idxInfo].mClass;
    return S_OK;
}

HRESULT AdditionsFacility::getName(com::Utf8Str &aName)
{
    LogFlowThisFuncEnter();

    /* midxInfo is static, so no need to lock anything. */
    size_t idxInfo = mData.midxInfo;
    AssertStmt(idxInfo < RT_ELEMENTS(s_aFacilityInfo), idxInfo = 0);
    int vrc = aName.assignNoThrow(s_aFacilityInfo[idxInfo].mName);
    return RT_SUCCESS(vrc) ? S_OK : E_OUTOFMEMORY;
}

HRESULT AdditionsFacility::getLastUpdated(LONG64 *aLastUpdated)
{
    LogFlowThisFuncEnter();

    /** @todo r=bird: Should take parent (Guest) lock here, see i_update(). */
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aLastUpdated = RTTimeSpecGetMilli(&mData.mTimestamp);
    return S_OK;
}

HRESULT AdditionsFacility::getStatus(AdditionsFacilityStatus_T *aStatus)
{
    LogFlowThisFuncEnter();

    /** @todo r=bird: Should take parent (Guest) lock here, see i_update(). */
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aStatus = mData.mStatus;
    return S_OK;
}

HRESULT AdditionsFacility::getType(AdditionsFacilityType_T *aType)
{
    LogFlowThisFuncEnter();

    /* mType is static, so no need to lock anything. */
    *aType = mData.mType;
    return S_OK;
}

#if 0 /* unused */

AdditionsFacilityType_T AdditionsFacility::i_getType() const
{
    return mData.mType;
}

AdditionsFacilityClass_T AdditionsFacility::i_getClass() const
{
    size_t idxInfo = mData.midxInfo;
    AssertStmt(idxInfo < RT_ELEMENTS(s_aFacilityInfo), idxInfo = 0);
    return s_aFacilityInfo[idxInfo].mClass;
}

const char *AdditionsFacility::i_getName() const
{
    size_t idxInfo = mData.midxInfo;
    AssertStmt(idxInfo < RT_ELEMENTS(s_aFacilityInfo), idxInfo = 0);
    return s_aFacilityInfo[idxInfo].mName;
}

#endif /* unused */

/**
 * @note Caller should read lock the Guest object.
 */
LONG64 AdditionsFacility::i_getLastUpdated() const
{
    return RTTimeSpecGetMilli(&mData.mTimestamp);
}

/**
 * @note Caller should read lock the Guest object.
 */
AdditionsFacilityStatus_T AdditionsFacility::i_getStatus() const
{
    return mData.mStatus;
}

/**
 * Method used by IGuest::facilityUpdate to make updates.
 *
 * @returns change indicator.
 *
 * @todo    r=bird: Locking here isn't quite sane.  While updating is serialized
 *          by the caller holding down the Guest object lock, this code doesn't
 *          serialize with this object.  So, the read locking done in the getter
 *          methods is utterly pointless.  OTOH, the getter methods only get
 *          single values, so there isn't really much to be worried about here,
 *          especially with 32-bit hosts no longer being supported.
 */
bool AdditionsFacility::i_update(AdditionsFacilityStatus_T a_enmStatus, uint32_t a_fFlags, PCRTTIMESPEC a_pTimeSpecTS)
{
    bool const fChanged = mData.mStatus != a_enmStatus;

    mData.mTimestamp = *a_pTimeSpecTS;
    mData.mStatus    = a_enmStatus;
    mData.mfFlags    = a_fFlags;

    return fChanged;
}

