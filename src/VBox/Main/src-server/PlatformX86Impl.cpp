/* $Id: PlatformX86Impl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - x86 platform settings.
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

#define LOG_GROUP LOG_GROUP_MAIN_PLATFORMX86
#include "MachineImpl.h"
#include "PlatformX86Impl.h"
#include "PlatformImpl.h"
#include "LoggingNew.h"

#include "AutoStateDep.h"

#include <iprt/cpp/utils.h>

#include <VBox/settings.h>


/**
 * x86-specific platform data.
 *
 * This data is unique for a machine and for every machine snapshot.
 * Stored using the util::Backupable template in the |mPlatformX86Data| variable.
 *
 * SessionMachine instances can alter this data and discard changes.
 */
struct Data
{
    Data() { }

    ComObjPtr<PlatformX86> pPeer;

    // use the XML settings structure in the members for simplicity
    Backupable<settings::PlatformX86> bd;
};


/*
 * PlatformX86 implementation.
 */
PlatformX86::PlatformX86()
{
}

PlatformX86::~PlatformX86()
{
    uninit();
}

HRESULT PlatformX86::FinalConstruct()
{
    return BaseFinalConstruct();
}

void PlatformX86::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

HRESULT PlatformX86::init(Platform *aParent, Machine *aMachine)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* share the parent + machine weakly */
    unconst(mParent)  = aParent;
    unconst(mMachine) = aMachine;

    m = new Data;
    m->bd.allocate();

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Initializes the platform object given another platform object
 * (a kind of copy constructor). This object shares data with
 * the object passed as an argument.
 *
 * @note This object must be destroyed before the original object
 *       it shares data with is destroyed.
 */
HRESULT PlatformX86::init(Platform *aParent, Machine *aMachine, PlatformX86 *aThat)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    ComAssertRet(aParent && aParent, E_INVALIDARG);

    unconst(mParent)  = aParent;
    unconst(mMachine) = aMachine;

    m = new Data();
    m->pPeer = aThat;

    AutoWriteLock thatlock(aThat COMMA_LOCKVAL_SRC_POS);
    m->bd.share(aThat->m->bd);

    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Initializes the guest object given another guest object
 * (a kind of copy constructor). This object makes a private copy of data
 * of the original object passed as an argument.
 */
HRESULT PlatformX86::initCopy(Platform *aParent, Machine *aMachine, PlatformX86 *aThat)
{
    ComAssertRet(aParent && aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent)  = aParent;
    unconst(mMachine) = aMachine;

    m = new Data();
    // m->pPeer is left null

    AutoWriteLock thatlock(aThat COMMA_LOCKVAL_SRC_POS); /** @todo r=andy Shouldn't a read lock be sufficient here? */
    m->bd.attachCopy(aThat->m->bd);

    autoInitSpan.setSucceeded();

    return S_OK;
}

void PlatformX86::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(mMachine) = NULL;

    if (m)
    {
        m->bd.free();
        unconst(m->pPeer) = NULL;

        delete m;
        m = NULL;
    }
}

void PlatformX86::i_rollback()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (m)
        m->bd.rollback();
}

void PlatformX86::i_commit()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller peerCaller(m->pPeer);
    AssertComRCReturnVoid(peerCaller.hrc());

    /* lock both for writing since we modify both (mPeer is "master" so locked
     * first) */
    AutoMultiWriteLock2 alock(m->pPeer, this COMMA_LOCKVAL_SRC_POS);

    if (m->bd.isBackedUp())
    {
        m->bd.commit();
        if (m->pPeer)
        {
            /* attach new data to the peer and reshare it */
            AutoWriteLock peerlock(m->pPeer COMMA_LOCKVAL_SRC_POS);
            m->pPeer->m->bd.attach(m->bd);
        }
    }
}

void PlatformX86::i_copyFrom(PlatformX86 *aThat)
{
    AssertReturnVoid(aThat != NULL);

    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnVoid(thatCaller.hrc());

    /* peer is not modified, lock it for reading (aThat is "master" so locked
     * first) */
    AutoReadLock rl(aThat COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);

    /* this will back up current data */
    m->bd.assignCopy(aThat->m->bd);
}

HRESULT PlatformX86::getHPETEnabled(BOOL *aHPETEnabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aHPETEnabled = m->bd->fHPETEnabled;

    return S_OK;
}

HRESULT PlatformX86::setHPETEnabled(BOOL aHPETEnabled)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(mMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->fHPETEnabled = aHPETEnabled;

    alock.release();

    AutoWriteLock mlock(mMachine COMMA_LOCKVAL_SRC_POS);
    mMachine->i_setModified(Machine::IsModified_Platform);

    return S_OK;
}

HRESULT PlatformX86::getCPUProperty(CPUPropertyTypeX86_T aProperty, BOOL *aValue)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    switch (aProperty)
    {
        case CPUPropertyTypeX86_PAE:
            *aValue = m->bd->fPAE;
            break;

        case CPUPropertyTypeX86_LongMode:
            if (m->bd->enmLongMode == settings::PlatformX86::LongMode_Enabled)
                *aValue = TRUE;
            else if (m->bd->enmLongMode == settings::PlatformX86::LongMode_Disabled)
                *aValue = FALSE;
#if HC_ARCH_BITS == 64
            else
                *aValue = TRUE;
#else
            else
            {
                *aValue = FALSE;

                ComObjPtr<GuestOSType> pGuestOSType;
                HRESULT hrc2 = mParent->i_findGuestOSType(mUserData->s.strOsType,
                                                          pGuestOSType);
                if (SUCCEEDED(hrc2) && !pGuestOSType.isNull())
                {
                    if (pGuestOSType->i_is64Bit())
                    {
                        ComObjPtr<Host> pHost = mParent->i_host();
                        alock.release();

                        hrc2 = pHost->GetProcessorFeature(ProcessorFeature_LongMode, aValue); AssertComRC(hrc2);
                        if (FAILED(hrc2))
                            *aValue = FALSE;
                    }
                }
            }
#endif
            break;

        case CPUPropertyTypeX86_TripleFaultReset:
            *aValue = m->bd->fTripleFaultReset;
            break;

        case CPUPropertyTypeX86_APIC:
            *aValue = m->bd->fAPIC;
            break;

        case CPUPropertyTypeX86_X2APIC:
            *aValue = m->bd->fX2APIC;
            break;

        case CPUPropertyTypeX86_IBPBOnVMExit:
            *aValue = m->bd->fIBPBOnVMExit;
            break;

        case CPUPropertyTypeX86_IBPBOnVMEntry:
            *aValue = m->bd->fIBPBOnVMEntry;
            break;

        case CPUPropertyTypeX86_SpecCtrl:
            *aValue = m->bd->fSpecCtrl;
            break;

        case CPUPropertyTypeX86_SpecCtrlByHost:
            *aValue = m->bd->fSpecCtrlByHost;
            break;

        case CPUPropertyTypeX86_HWVirt:
            *aValue = m->bd->fNestedHWVirt;
            break;

        case CPUPropertyTypeX86_L1DFlushOnEMTScheduling:
            *aValue = m->bd->fL1DFlushOnSched;
            break;

        case CPUPropertyTypeX86_L1DFlushOnVMEntry:
            *aValue = m->bd->fL1DFlushOnVMEntry;
            break;

        case CPUPropertyTypeX86_MDSClearOnEMTScheduling:
            *aValue = m->bd->fMDSClearOnSched;
            break;

        case CPUPropertyTypeX86_MDSClearOnVMEntry:
            *aValue = m->bd->fMDSClearOnVMEntry;
            break;

        default:
            return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT PlatformX86::setCPUProperty(CPUPropertyTypeX86_T aProperty, BOOL aValue)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(mMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    switch (aProperty)
    {
        case CPUPropertyTypeX86_PAE:
        {
            m->bd.backup();
            m->bd->fPAE = !!aValue;
            break;
        }

        case CPUPropertyTypeX86_LongMode:
        {
            m->bd.backup();
            m->bd->enmLongMode = !aValue ? settings::PlatformX86::LongMode_Disabled : settings::PlatformX86::LongMode_Enabled;
            break;
        }

        case CPUPropertyTypeX86_TripleFaultReset:
        {
            m->bd.backup();
            m->bd->fTripleFaultReset = !!aValue;
            break;
        }

        case CPUPropertyTypeX86_APIC:
        {
            if (m->bd->fX2APIC)
                aValue = TRUE;
            m->bd.backup();
            m->bd->fAPIC = !!aValue;
            break;
        }

        case CPUPropertyTypeX86_X2APIC:
        {
            m->bd.backup();
            m->bd->fX2APIC = !!aValue;
            m->bd->fAPIC   = !!aValue;
            break;
        }

        case CPUPropertyTypeX86_IBPBOnVMExit:
        {
            m->bd.backup();
            m->bd->fIBPBOnVMExit = !!aValue;
            break;
        }

        case CPUPropertyTypeX86_IBPBOnVMEntry:
        {
            m->bd.backup();
            m->bd->fIBPBOnVMEntry = !!aValue;
            break;
        }

        case CPUPropertyTypeX86_SpecCtrl:
        {
            m->bd.backup();
            m->bd->fSpecCtrl = !!aValue;
            break;
        }

        case CPUPropertyTypeX86_SpecCtrlByHost:
        {
            m->bd.backup();
            m->bd->fSpecCtrlByHost = !!aValue;
            break;
        }

        case CPUPropertyTypeX86_HWVirt:
        {
            m->bd.backup();
            m->bd->fNestedHWVirt = !!aValue;
            break;
        }

        case CPUPropertyTypeX86_L1DFlushOnEMTScheduling:
        {
            m->bd.backup();
            m->bd->fL1DFlushOnSched = !!aValue;
            break;
        }

        case CPUPropertyTypeX86_L1DFlushOnVMEntry:
        {
            m->bd.backup();
            m->bd->fL1DFlushOnVMEntry = !!aValue;
            break;
        }

        case CPUPropertyTypeX86_MDSClearOnEMTScheduling:
        {
            m->bd.backup();
            m->bd->fMDSClearOnSched = !!aValue;
            break;
        }

        case CPUPropertyTypeX86_MDSClearOnVMEntry:
        {
            m->bd.backup();
            m->bd->fMDSClearOnVMEntry = !!aValue;
            break;
        }

        default:
            return E_INVALIDARG;
    }

    alock.release();

    AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);
    mMachine->i_setModified(Machine::IsModified_Platform);

    return S_OK;
}

HRESULT PlatformX86::getCPUIDLeafByOrdinal(ULONG aOrdinal, ULONG *aIdx, ULONG *aSubIdx, ULONG *aValEax, ULONG *aValEbx,
                                           ULONG *aValEcx, ULONG *aValEdx)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (aOrdinal < m->bd->llCpuIdLeafs.size())
    {
        for (settings::CpuIdLeafsX86List::const_iterator it = m->bd->llCpuIdLeafs.begin();
             it != m->bd->llCpuIdLeafs.end();
             ++it)
        {
            if (aOrdinal == 0)
            {
                const settings::CpuIdLeafX86 &rLeaf= *it;
                *aIdx    = rLeaf.idx;
                *aSubIdx = rLeaf.idxSub;
                *aValEax = rLeaf.uEax;
                *aValEbx = rLeaf.uEbx;
                *aValEcx = rLeaf.uEcx;
                *aValEdx = rLeaf.uEdx;
                return S_OK;
            }
            aOrdinal--;
        }
    }
    return E_INVALIDARG;
}

HRESULT PlatformX86::getCPUIDLeaf(ULONG aIdx, ULONG aSubIdx, ULONG *aValEax, ULONG *aValEbx, ULONG *aValEcx, ULONG *aValEdx)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Search the list.
     */
    for (settings::CpuIdLeafsX86List::const_iterator it = m->bd->llCpuIdLeafs.begin();
         it != m->bd->llCpuIdLeafs.end();
         ++it)
    {
        const settings::CpuIdLeafX86 &rLeaf= *it;
        if (   rLeaf.idx == aIdx
            && (   aSubIdx == UINT32_MAX
                || rLeaf.idxSub == aSubIdx) )
        {
            *aValEax = rLeaf.uEax;
            *aValEbx = rLeaf.uEbx;
            *aValEcx = rLeaf.uEcx;
            *aValEdx = rLeaf.uEdx;
            return S_OK;
        }
    }

    return E_INVALIDARG;
}

HRESULT PlatformX86::setCPUIDLeaf(ULONG aIdx, ULONG aSubIdx, ULONG aValEax, ULONG aValEbx, ULONG aValEcx, ULONG aValEdx)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /*
     * Validate input before taking locks and checking state.
     */
    if (aSubIdx != 0 && aSubIdx != UINT32_MAX)
        return setError(E_INVALIDARG, tr("Currently only aSubIdx values 0 and 0xffffffff are supported: %#x"), aSubIdx);
    if (   aIdx >= UINT32_C(0x20)
        && aIdx - UINT32_C(0x80000000) >= UINT32_C(0x20)
        && aIdx - UINT32_C(0xc0000000) >= UINT32_C(0x10) )
        return setError(E_INVALIDARG, tr("CpuId override leaf %#x is out of range"), aIdx);

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(mMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Impose a maximum number of leaves.
     */
    if (m->bd->llCpuIdLeafs.size() > 256)
        return setError(E_FAIL, tr("Max of 256 CPUID override leaves reached"));

    /*
     * Updating the list is a bit more complicated.  So, let's do a remove first followed by an insert.
     */
    m->bd.backup();

    for (settings::CpuIdLeafsX86List::iterator it = m->bd->llCpuIdLeafs.begin(); it != m->bd->llCpuIdLeafs.end(); )
    {
        settings::CpuIdLeafX86 &rLeaf= *it;
        if (   rLeaf.idx == aIdx
            && (   aSubIdx == UINT32_MAX
                || rLeaf.idxSub == aSubIdx) )
            it = m->bd->llCpuIdLeafs.erase(it);
        else
            ++it;
    }

    settings::CpuIdLeafX86 NewLeaf;
    NewLeaf.idx    = aIdx;
    NewLeaf.idxSub = aSubIdx == UINT32_MAX ? 0 : aSubIdx;
    NewLeaf.uEax   = aValEax;
    NewLeaf.uEbx   = aValEbx;
    NewLeaf.uEcx   = aValEcx;
    NewLeaf.uEdx   = aValEdx;
    m->bd->llCpuIdLeafs.push_back(NewLeaf);

    alock.release();

    AutoWriteLock mlock(mMachine COMMA_LOCKVAL_SRC_POS);
    mMachine->i_setModified(Machine::IsModified_Platform);

    return S_OK;
}

HRESULT PlatformX86::removeCPUIDLeaf(ULONG aIdx, ULONG aSubIdx)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    AutoWriteLock mlock(mMachine COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = mMachine->i_checkStateDependency(Machine::MutableStateDep);
    if (FAILED(hrc)) return hrc;

    /*
     * Do the removal.
     */
    bool fModified = m->bd.isBackedUp();
    for (settings::CpuIdLeafsX86List::iterator it = m->bd->llCpuIdLeafs.begin(); it != m->bd->llCpuIdLeafs.end(); )
    {
        settings::CpuIdLeafX86 &rLeaf= *it;
        if (   rLeaf.idx == aIdx
            && (   aSubIdx == UINT32_MAX
                || rLeaf.idxSub == aSubIdx) )
        {
            if (!fModified)
            {
                fModified = true;
                mMachine->i_setModified(Machine::IsModified_Platform);
                m->bd.backup();
                // Start from the beginning, since m->bd.backup() creates
                // a new list, causing iterator mixup. This makes sure that
                // the settings are not unnecessarily marked as modified,
                // at the price of extra list walking.
                it = m->bd->llCpuIdLeafs.begin();
            }
            else
                it = m->bd->llCpuIdLeafs.erase(it);
        }
        else
            ++it;
    }

    return S_OK;
}

HRESULT PlatformX86::removeAllCPUIDLeaves()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    AutoWriteLock mlock(mMachine COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = mMachine->i_checkStateDependency(Machine::MutableStateDep);
    if (FAILED(hrc)) return hrc;

    if (m->bd->llCpuIdLeafs.size() > 0)
    {
        mMachine->i_setModified(Machine::IsModified_Platform);
        m->bd.backup();

        m->bd->llCpuIdLeafs.clear();
    }

    return S_OK;
}

HRESULT PlatformX86::getHWVirtExProperty(HWVirtExPropertyType_T aProperty, BOOL *aValue)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    switch(aProperty)
    {
        case HWVirtExPropertyType_Enabled:
            *aValue = m->bd->fHWVirtEx;
            break;

        case HWVirtExPropertyType_VPID:
            *aValue = m->bd->fHWVirtExVPID;
            break;

        case HWVirtExPropertyType_NestedPaging:
            *aValue = m->bd->fHWVirtExNestedPaging;
            break;

        case HWVirtExPropertyType_UnrestrictedExecution:
            *aValue = m->bd->fHWVirtExUX;
            break;

        case HWVirtExPropertyType_LargePages:
            *aValue = m->bd->fHWVirtExLargePages;
            break;

        case HWVirtExPropertyType_Force:
            *aValue = m->bd->fHWVirtExForce;
            break;

        case HWVirtExPropertyType_UseNativeApi:
            *aValue = m->bd->fHWVirtExUseNativeApi;
            break;

        case HWVirtExPropertyType_VirtVmsaveVmload:
            *aValue = m->bd->fHWVirtExVirtVmsaveVmload;
            break;

        default:
            return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT PlatformX86::setHWVirtExProperty(HWVirtExPropertyType_T aProperty, BOOL aValue)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(mMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    switch (aProperty)
    {
        case HWVirtExPropertyType_Enabled:
        {
            m->bd.backup();
            m->bd->fHWVirtEx = !!aValue;
            break;
        }

        case HWVirtExPropertyType_VPID:
        {
            m->bd.backup();
            m->bd->fHWVirtExVPID = !!aValue;
            break;
        }

        case HWVirtExPropertyType_NestedPaging:
        {
            m->bd.backup();
            m->bd->fHWVirtExNestedPaging = !!aValue;
            break;
        }

        case HWVirtExPropertyType_UnrestrictedExecution:
        {
            m->bd.backup();
            m->bd->fHWVirtExUX = !!aValue;
            break;
        }

        case HWVirtExPropertyType_LargePages:
        {
            m->bd.backup();
            m->bd->fHWVirtExLargePages = !!aValue;
            break;
        }

        case HWVirtExPropertyType_Force:
        {
            m->bd.backup();
            m->bd->fHWVirtExForce = !!aValue;
            break;
        }

        case HWVirtExPropertyType_UseNativeApi:
        {
            m->bd.backup();
            m->bd->fHWVirtExUseNativeApi = !!aValue;
            break;
        }

        case HWVirtExPropertyType_VirtVmsaveVmload:
        {
            m->bd.backup();
            m->bd->fHWVirtExVirtVmsaveVmload = !!aValue;
            break;
        }

        default:
            return E_INVALIDARG;
    }

    alock.release();

    AutoWriteLock mlock(mParent COMMA_LOCKVAL_SRC_POS);
    mMachine->i_setModified(Machine::IsModified_Platform);

    return S_OK;
}

// public methods only for internal purposes
////////////////////////////////////////////////////////////////////////////////

#if 0
void PlatformX86::i_copyFrom(PlatformX86 *aThat)
{
    AssertReturnVoid(aThat != NULL);

    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnVoid(thatCaller.hrc());

    /* peer is not modified, lock it for reading (aThat is "master" so locked
     * first) */
    AutoReadLock rl(aThat COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);

    /* this will back up current data */
    m->bd.assignCopy(aThat->m->bd);
}
#endif

/**
 * Loads settings from the given platform x86 node.
 * May be called once right after this object creation.
 *
 * @returns HRESULT
 * @param   data                    Configuration settings.
 *
 * @note Locks this object for writing.
 */
HRESULT PlatformX86::i_loadSettings(const settings::PlatformX86 &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock mlock(mMachine COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    // cpuid leafs
    for (settings::CpuIdLeafsX86List::const_iterator
         it = data.llCpuIdLeafs.begin();
         it != data.llCpuIdLeafs.end();
         ++it)
    {
        const settings::CpuIdLeafX86 &rLeaf= *it;
        if (   rLeaf.idx < UINT32_C(0x20)
            || rLeaf.idx - UINT32_C(0x80000000) < UINT32_C(0x20)
            || rLeaf.idx - UINT32_C(0xc0000000) < UINT32_C(0x10) )
            m->bd->llCpuIdLeafs.push_back(rLeaf);
        /* else: just ignore */
    }

    // simply copy
    m->bd.assignCopy(&data);
    return S_OK;
}

/**
 * Saves settings to the given platform x86 node.
 *
 * @returns HRESULT
 * @param   data                    Configuration settings.
 *
 * @note Locks this object for reading.
 */
HRESULT PlatformX86::i_saveSettings(settings::PlatformX86 &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Standard and Extended CPUID leafs. */
    data.llCpuIdLeafs.clear();
    data.llCpuIdLeafs = m->bd->llCpuIdLeafs;

    data = *m->bd.data();

    return S_OK;
}

#if 0
void PlatformX86::i_rollback()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->bd.rollback();
}

void PlatformX86::i_commit()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller peerCaller(m->pPeer);
    AssertComRCReturnVoid(peerCaller.hrc());

    /* lock both for writing since we modify both (mPeer is "master" so locked
     * first) */
    AutoMultiWriteLock2 alock(m->pPeer, this COMMA_LOCKVAL_SRC_POS);

    if (m->bd.isBackedUp())
    {
        m->bd.commit();
        if (m->pPeer)
        {
            /* attach new data to the peer and reshare it */
            AutoWriteLock peerlock(m->pPeer COMMA_LOCKVAL_SRC_POS);
            m->pPeer->m->bd.attach(m->bd);
        }
    }
}
#endif

HRESULT PlatformX86::i_applyDefaults(GuestOSType *aOsType)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), autoCaller.hrc());

    HRESULT hrc = S_OK;

    BOOL                                fPAE;
    BOOL                                fAPIC = TRUE; /* Always was enabled. */
    BOOL                                fX2APIC;
    settings::PlatformX86::LongModeType enmLongMode;
    BOOL                                fHPET;
    BOOL                                fTripleFaultReset;

    if (aOsType)
    {
        hrc = aOsType->COMGETTER(RecommendedPAE)(&fPAE);
        AssertComRCReturn(hrc, hrc);

        hrc = aOsType->COMGETTER(RecommendedX2APIC)(&fX2APIC);
        AssertComRCReturn(hrc, hrc);

        /* Let the OS type select 64-bit ness. */
        enmLongMode = aOsType->i_is64Bit()
                    ? settings::PlatformX86::LongMode_Enabled : settings::PlatformX86::LongMode_Disabled;

        hrc = aOsType->COMGETTER(RecommendedHPET)(&fHPET);
        AssertComRCReturn(hrc, hrc);

        hrc = aOsType->COMGETTER(RecommendedTFReset)(&fTripleFaultReset);
        AssertComRCReturn(hrc, hrc);
    }
    else
    {
        /* No guest OS type object. Pick some plausible defaults which the
         * host can handle. There's no way to know or validate anything. */
        fX2APIC           = FALSE;
        enmLongMode       =   HC_ARCH_BITS == 64
                          ? settings::PlatformX86::LongMode_Enabled : settings::PlatformX86::LongMode_Disabled;

#if HC_ARCH_BITS == 64 || defined(RT_OS_WINDOWS) || defined(RT_OS_DARWIN)
        fPAE              = TRUE;
#else
        fPAE              = FALSE;
#endif
        fHPET             = FALSE;
        fTripleFaultReset = FALSE;
    }

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd->fPAE = fPAE;
    m->bd->fAPIC = fAPIC;
    m->bd->fX2APIC = fX2APIC;
    m->bd->enmLongMode = enmLongMode;
    m->bd->fHPETEnabled = fHPET;

    m->bd->fTripleFaultReset = RT_BOOL(fTripleFaultReset);
    m->bd->fIBPBOnVMExit = false;
    m->bd->fIBPBOnVMEntry = false;
    m->bd->fSpecCtrl = false;
    m->bd->fSpecCtrlByHost = false;
    m->bd->fL1DFlushOnSched = true;
    m->bd->fL1DFlushOnVMEntry = false;
    m->bd->fMDSClearOnSched = true;
    m->bd->fMDSClearOnVMEntry = false;

    /* Hardware virtualization must be ON by default. */
    m->bd->fHWVirtEx = true;

    return hrc;
}
