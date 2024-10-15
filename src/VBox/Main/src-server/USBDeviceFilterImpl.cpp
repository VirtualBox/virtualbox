/* $Id: USBDeviceFilterImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Implementation of VirtualBox COM components: USBDeviceFilter and HostUSBDeviceFilter
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

#define LOG_GROUP LOG_GROUP_MAIN_USBDEVICEFILTER
#include "USBDeviceFilterImpl.h"
#include "USBDeviceFiltersImpl.h"
#include "MachineImpl.h"
#include "HostImpl.h"

#include <iprt/cpp/utils.h>
#include <VBox/settings.h>

#include "AutoStateDep.h"
#include "AutoCaller.h"
#include "LoggingNew.h"

////////////////////////////////////////////////////////////////////////////////
// Internal Helpers
////////////////////////////////////////////////////////////////////////////////

/**
 *  Converts a USBFilter field into a string.
 *
 *  (This function is also used by HostUSBDeviceFilter.)
 *
 *  @param  aFilter     The filter.
 *  @param  aIdx        The field index.
 *  @param  rstrOut     The output string.
 */
static void i_usbFilterFieldToString(PCUSBFILTER aFilter, USBFILTERIDX aIdx, Utf8Str &rstrOut)
{
    const USBFILTERMATCH matchingMethod = USBFilterGetMatchingMethod(aFilter, aIdx);
    Assert(matchingMethod != USBFILTERMATCH_INVALID);

    if (USBFilterIsMethodNumeric(matchingMethod))
    {
        int value = USBFilterGetNum(aFilter, aIdx);
        Assert(value >= 0 && value <= 0xffff);

        rstrOut.printf("%04RX16", (uint16_t)value);
    }
    else if (USBFilterIsMethodString(matchingMethod))
        rstrOut = USBFilterGetString(aFilter, aIdx);
    else
        rstrOut.setNull();
}

/*static*/
const char* USBDeviceFilter::i_describeUSBFilterIdx(USBFILTERIDX aIdx)
{
    switch (aIdx)
    {
        case USBFILTERIDX_VENDOR_ID:            return tr("Vendor ID");
        case USBFILTERIDX_PRODUCT_ID:           return tr("Product ID");
        case USBFILTERIDX_DEVICE:               return tr("Revision");
        case USBFILTERIDX_MANUFACTURER_STR:     return tr("Manufacturer");
        case USBFILTERIDX_PRODUCT_STR:          return tr("Product");
        case USBFILTERIDX_SERIAL_NUMBER_STR:    return tr("Serial number");
        case USBFILTERIDX_PORT:                 return tr("Port number");
        default:                                return "";
    }
    /* not reached. */
}

/**
 *  Interprets a string and assigns it to a USBFilter field.
 *
 *  (This function is also used by HostUSBDeviceFilter.)
 *
 *  @param  aFilter     The filter.
 *  @param  aIdx        The field index.
 *  @param  aValue      The input string.
 *  @param  aErrStr     Where to return the error string on failure.
 *
 *  @return COM status code.
 *  @remark The idea was to have this as a static function, but tr() doesn't wanna work without a class :-/
 */
/*static*/ HRESULT USBDeviceFilter::i_usbFilterFieldFromString(PUSBFILTER aFilter,
                                                               USBFILTERIDX aIdx,
                                                               const Utf8Str &aValue,
                                                               Utf8Str &aErrStr)
{
    int vrc;
    if (aValue.isEmpty())
        vrc = USBFilterSetIgnore(aFilter, aIdx);
    else
    {
        const char *pcszValue = aValue.c_str();
        if (USBFilterIsNumericField(aIdx))
        {
            /* Is it a lonely number? */
            char *pszNext;
            uint64_t u64;
            vrc = RTStrToUInt64Ex(pcszValue, &pszNext, 16, &u64);
            if (RT_SUCCESS(vrc))
                pszNext = RTStrStripL(pszNext);
            if (    vrc == VINF_SUCCESS
                &&  !*pszNext)
            {
                if (u64 > 0xffff)
                {
                    // there was a bug writing out "-1" values in earlier versions, which got
                    // written as "FFFFFFFF"; make sure we don't fail on those
                    if (u64 == 0xffffffff)
                        u64 = 0xffff;
                    else
                    {
                        aErrStr.printf(tr("The %s value '%s' is too big (max 0xFFFF)"), i_describeUSBFilterIdx(aIdx), pcszValue);
                        return E_INVALIDARG;
                    }
                }

                vrc = USBFilterSetNumExact(aFilter, aIdx, (uint16_t)u64, true /* fMustBePresent */);
            }
            else
                vrc = USBFilterSetNumExpression(aFilter, aIdx, pcszValue, true /* fMustBePresent */);
        }
        else
        {
            /* Any wildcard in the string? */
            Assert(USBFilterIsStringField(aIdx));
            if (   strchr(pcszValue, '*')
                || strchr(pcszValue, '?')
                /* || strchr (psz, '[') - later */
                )
                vrc = USBFilterSetStringPattern(aFilter, aIdx, pcszValue, true /*fMustBePresent*/);
            else
                vrc = USBFilterSetStringExact(aFilter, aIdx, pcszValue, true /*fMustBePresent*/, false /*fPurge*/);
        }
    }

    if (RT_FAILURE(vrc))
    {
        if (vrc == VERR_INVALID_PARAMETER)
        {
            aErrStr.printf(tr("The %s filter expression '%s' is not valid"), i_describeUSBFilterIdx(aIdx), aValue.c_str());
            return E_INVALIDARG;
        }
        if (vrc == VERR_BUFFER_OVERFLOW)
        {
            aErrStr.printf(tr("Insufficient expression space for the '%s' filter expression '%s'"),
                           i_describeUSBFilterIdx(aIdx), aValue.c_str());
            return E_FAIL;
        }
        AssertRC(vrc);
        aErrStr.printf(tr("Encountered unexpected status %Rrc when setting '%s' to '%s'"),
                       vrc, i_describeUSBFilterIdx(aIdx), aValue.c_str());
        return E_FAIL;
    }

    return S_OK;
}


////////////////////////////////////////////////////////////////////////////////
// USBDeviceFilter
////////////////////////////////////////////////////////////////////////////////

// constructor / destructor
////////////////////////////////////////////////////////////////////////////////

USBDeviceFilter::USBDeviceFilter()
    : mParent(NULL),
      mPeer(NULL)
{
}

USBDeviceFilter::~USBDeviceFilter()
{
}

HRESULT USBDeviceFilter::FinalConstruct()
{
    return BaseFinalConstruct();
}

void USBDeviceFilter::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
////////////////////////////////////////////////////////////////////////////////

/**
 *  Initializes the USB device filter object.
 *
 *  @param aParent  Handle of the parent object.
 *  @param data     Reference filter settings.
 */
HRESULT USBDeviceFilter::init(USBDeviceFilters *aParent,
                              const settings::USBDeviceFilter &data)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent && !data.strName.isEmpty(), E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;
    /* mPeer is left null */

    m_fModified = false;

    bd.allocate();
    bd->mData.strName = data.strName;
    bd->mData.fActive = data.fActive;
    bd->mData.ulMaskedInterfaces = 0;

    /* initialize all filters to any match using null string */
    USBFilterInit(&bd->mUSBFilter, USBFILTERTYPE_CAPTURE);
    bd->mRemote = NULL;

    mInList = false;

    /* use setters for the attributes below to reuse parsing errors
     * handling */

    HRESULT hrc = i_usbFilterFieldSetter(USBFILTERIDX_VENDOR_ID, data.strVendorId);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_PRODUCT_ID, data.strProductId);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_DEVICE, data.strRevision);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_MANUFACTURER_STR, data.strManufacturer);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_PRODUCT_STR, data.strProduct);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_SERIAL_NUMBER_STR, data.strSerialNumber);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_PORT, data.strPort);
    if (FAILED(hrc)) return hrc;

    hrc = COMSETTER(Remote)(Bstr(data.strRemote).raw());
    if (FAILED(hrc)) return hrc;

    hrc = COMSETTER(MaskedInterfaces)(data.ulMaskedInterfaces);
    if (FAILED(hrc)) return hrc;

    /* Confirm successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the USB device filter object (short version).
 *
 *  @param aParent  Handle of the parent object.
 *  @param aName    Name of the filter.
 */
HRESULT USBDeviceFilter::init(USBDeviceFilters *aParent, IN_BSTR aName)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent && aName && *aName, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;
    /* mPeer is left null */

    m_fModified = false;

    bd.allocate();

    bd->mData.strName = Utf8Str(aName);
    bd->mData.fActive = FALSE;
    bd->mData.ulMaskedInterfaces = 0;

    /* initialize all filters to any match using null string */
    USBFilterInit(&bd->mUSBFilter, USBFILTERTYPE_CAPTURE);
    bd->mRemote = NULL;

    mInList = false;

    /* Confirm successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the object given another object
 *  (a kind of copy constructor). This object shares data with
 *  the object passed as an argument.
 *
 *  @param  aParent  Handle of the parent object.
 *  @param  aThat
 *  @param  aReshare
 *      When false, the original object will remain a data owner.
 *      Otherwise, data ownership will be transferred from the original
 *      object to this one.
 *
 *  @note This object must be destroyed before the original object
 *  it shares data with is destroyed.
 *
 *  @note Locks @a aThat object for writing if @a aReshare is @c true, or for
 *  reading if @a aReshare is false.
 */
HRESULT USBDeviceFilter::init(USBDeviceFilters *aParent, USBDeviceFilter *aThat,
                              bool aReshare /* = false */)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p, aReshare=%RTbool\n",
                      aParent, aThat, aReshare));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    m_fModified = false;

    /* sanity */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    if (aReshare)
    {
        AutoWriteLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);

        unconst(aThat->mPeer) = this;
        bd.attach(aThat->bd);
    }
    else
    {
        unconst(mPeer) = aThat;

        AutoReadLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);
        bd.share(aThat->bd);
    }

    /* the arbitrary ID field is not reset because
     * the copy is a shadow of the original */

    mInList = aThat->mInList;

    /* Confirm successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the guest object given another guest object
 *  (a kind of copy constructor). This object makes a private copy of data
 *  of the original object passed as an argument.
 *
 *  @note Locks @a aThat object for reading.
 */
HRESULT USBDeviceFilter::initCopy(USBDeviceFilters *aParent, USBDeviceFilter *aThat)
{
    LogFlowThisFunc(("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet(aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;
    /* mPeer is left null */

    m_fModified = false;

    /* sanity */
    AutoCaller thatCaller(aThat);
    AssertComRCReturnRC(thatCaller.hrc());

    AutoReadLock thatLock(aThat COMMA_LOCKVAL_SRC_POS);
    bd.attachCopy(aThat->bd);

    /* reset the arbitrary ID field
     * (this field is something unique that two distinct objects, even if they
     * are deep copies of each other, should not share) */
    bd->mId = NULL;

    mInList = aThat->mInList;

    /* Confirm successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void USBDeviceFilter::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    mInList = false;

    bd.free();

    unconst(mPeer) = NULL;
    unconst(mParent) = NULL;
}


// IUSBDeviceFilter properties
////////////////////////////////////////////////////////////////////////////////

HRESULT USBDeviceFilter::getName(com::Utf8Str &aName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    return aName.assignEx(bd->mData.strName);
}

HRESULT USBDeviceFilter::setName(const com::Utf8Str &aName)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent->i_getMachine());
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (bd->mData.strName != aName)
    {
        m_fModified = true;
        ComObjPtr<Machine> pMachine = mParent->i_getMachine();

        bd.backup();
        bd->mData.strName = aName;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(pMachine COMMA_LOCKVAL_SRC_POS);
        pMachine->i_setModified(Machine::IsModified_USB);
        mlock.release();

        return mParent->i_onDeviceFilterChange(this);
    }

    return S_OK;
}

HRESULT USBDeviceFilter::getActive(BOOL *aActive)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aActive = bd->mData.fActive;

    return S_OK;
}

HRESULT USBDeviceFilter::setActive(const BOOL aActive)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent->i_getMachine());
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (bd->mData.fActive != RT_BOOL(aActive))
    {
        m_fModified = true;
        ComObjPtr<Machine> pMachine = mParent->i_getMachine();

        bd.backup();
        bd->mData.fActive = RT_BOOL(aActive);

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(pMachine COMMA_LOCKVAL_SRC_POS);
        pMachine->i_setModified(Machine::IsModified_USB);
        mlock.release();

        return mParent->i_onDeviceFilterChange(this, TRUE /* aActiveChanged */);
    }

    return S_OK;
}

HRESULT USBDeviceFilter::getVendorId(com::Utf8Str &aVendorId)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_VENDOR_ID, aVendorId);
}

HRESULT USBDeviceFilter::setVendorId(const com::Utf8Str &aVendorId)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_VENDOR_ID, aVendorId);
}

HRESULT USBDeviceFilter::getProductId(com::Utf8Str &aProductId)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_PRODUCT_ID, aProductId);
}

HRESULT USBDeviceFilter::setProductId(const com::Utf8Str &aProductId)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_PRODUCT_ID, aProductId);
}

HRESULT USBDeviceFilter::getRevision(com::Utf8Str &aRevision)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_DEVICE, aRevision);
}

HRESULT USBDeviceFilter::setRevision(const com::Utf8Str &aRevision)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_DEVICE, aRevision);
}

HRESULT USBDeviceFilter::getManufacturer(com::Utf8Str &aManufacturer)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_MANUFACTURER_STR, aManufacturer);
}

HRESULT USBDeviceFilter::setManufacturer(const com::Utf8Str &aManufacturer)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_MANUFACTURER_STR, aManufacturer);
}

HRESULT USBDeviceFilter::getProduct(com::Utf8Str &aProduct)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_PRODUCT_STR, aProduct);
}

HRESULT USBDeviceFilter::setProduct(const com::Utf8Str &aProduct)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_PRODUCT_STR, aProduct);
}

HRESULT USBDeviceFilter::getSerialNumber(com::Utf8Str &aSerialNumber)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_SERIAL_NUMBER_STR, aSerialNumber);
}

HRESULT USBDeviceFilter::setSerialNumber(const com::Utf8Str &aSerialNumber)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_SERIAL_NUMBER_STR, aSerialNumber);
}

HRESULT USBDeviceFilter::getPort(com::Utf8Str &aPort)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_PORT, aPort);
}

HRESULT USBDeviceFilter::setPort(const com::Utf8Str &aPort)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_PORT, aPort);
}


HRESULT USBDeviceFilter::getRemote(com::Utf8Str &aRemote)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aRemote = bd->mRemote.string();

    return S_OK;
}

HRESULT USBDeviceFilter::setRemote(const com::Utf8Str &aRemote)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent->i_getMachine());
    if (FAILED(adep.hrc())) return adep.hrc();
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Bstr bRemote = Bstr(aRemote).raw();

    if (bd->mRemote.string() != bRemote)
    {
        BackupableUSBDeviceFilterData::BOOLFilter flt = bRemote;
        ComAssertRet(!flt.isNull(), E_FAIL);
        if (!flt.isValid())
            return setError(E_INVALIDARG,
                            tr("Remote state filter string '%s' is not valid (error at position %d)"),
                            aRemote.c_str(), flt.errorPosition() + 1);

        m_fModified = true;
        ComObjPtr<Machine> pMachine = mParent->i_getMachine();

        bd.backup();
        bd->mRemote = flt;

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(pMachine COMMA_LOCKVAL_SRC_POS);
        pMachine->i_setModified(Machine::IsModified_USB);
        mlock.release();

        return mParent->i_onDeviceFilterChange(this);
    }
    return S_OK;
}


HRESULT USBDeviceFilter::getMaskedInterfaces(ULONG *aMaskedIfs)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aMaskedIfs = bd->mData.ulMaskedInterfaces;

    return S_OK;
}

HRESULT USBDeviceFilter::setMaskedInterfaces(ULONG aMaskedIfs)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent->i_getMachine());
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (bd->mData.ulMaskedInterfaces != aMaskedIfs)
    {
        m_fModified = true;
        ComObjPtr<Machine> pMachine = mParent->i_getMachine();

        bd.backup();
        bd->mData.ulMaskedInterfaces = aMaskedIfs;
        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(pMachine COMMA_LOCKVAL_SRC_POS);
        pMachine->i_setModified(Machine::IsModified_USB);
        mlock.release();

        return mParent->i_onDeviceFilterChange(this);
    }

    return S_OK;
}

// public methods only for internal purposes
////////////////////////////////////////////////////////////////////////////////

bool USBDeviceFilter::i_isModified()
{
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), false);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    return m_fModified;
}

/**
 *  @note Locks this object for writing.
 */
void USBDeviceFilter::i_rollback()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    bd.rollback();
}

/**
 *  @note Locks this object for writing, together with the peer object (also
 *  for writing) if there is one.
 */
void USBDeviceFilter::i_commit()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller peerCaller(mPeer);
    AssertComRCReturnVoid(peerCaller.hrc());

    /* lock both for writing since we modify both (mPeer is "master" so locked
     * first) */
    AutoMultiWriteLock2 alock(mPeer, this COMMA_LOCKVAL_SRC_POS);

    if (bd.isBackedUp())
    {
        bd.commit();
        if (mPeer)
        {
            /* attach new data to the peer and reshare it */
            mPeer->bd.attach(bd);
        }
    }
}

/**
 *  Cancels sharing (if any) by making an independent copy of data.
 *  This operation also resets this object's peer to NULL.
 *
 *  @note Locks this object for writing, together with the peer object
 *  represented by @a aThat (locked for reading).
 */
void USBDeviceFilter::unshare()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /* sanity too */
    AutoCaller peerCaller(mPeer);
    AssertComRCReturnVoid(peerCaller.hrc());

    /* peer is not modified, lock it for reading (mPeer is "master" so locked
     * first) */
    AutoReadLock rl(mPeer COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);

    if (bd.isShared())
    {
        if (!bd.isBackedUp())
            bd.backup();

        bd.commit();
    }

    unconst(mPeer) = NULL;
}

/**
 *  Generic USB filter field getter; converts the field value to UTF-16.
 *
 *  @param  aIdx    The field index.
 *  @param  aStr    Where to store the value.
 *
 *  @return COM status.
 */
HRESULT USBDeviceFilter::i_usbFilterFieldGetter(USBFILTERIDX aIdx, com::Utf8Str &aStr)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    i_usbFilterFieldToString(&bd->mUSBFilter, aIdx, aStr);
    return S_OK;
}

/**
 *  Generic USB filter field setter, expects UTF-8 input.
 *
 *  @param  aIdx    The field index.
 *  @param  strNew  The new value.
 *
 *  @return COM status.
 */
HRESULT USBDeviceFilter::i_usbFilterFieldSetter(USBFILTERIDX aIdx,
                                                const com::Utf8Str &strNew)
{
    /* the machine needs to be mutable */
    AutoMutableOrSavedOrRunningStateDependency adep(mParent->i_getMachine());
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);


    com::Utf8Str strOld;
    i_usbFilterFieldToString(&bd->mUSBFilter, aIdx, strOld);
    if (strOld != strNew)
    {
        m_fModified = true;
        ComObjPtr<Machine> pMachine = mParent->i_getMachine();

        bd.backup();

        com::Utf8Str errStr;
        HRESULT hrc = i_usbFilterFieldFromString(&bd->mUSBFilter, aIdx, strNew, errStr);
        if (FAILED(hrc))
        {
            bd.rollback();
            return setError(hrc, "%s", errStr.c_str());
        }

        // leave the lock before informing callbacks
        alock.release();

        AutoWriteLock mlock(pMachine COMMA_LOCKVAL_SRC_POS);
        pMachine->i_setModified(Machine::IsModified_USB);
        mlock.release();

        return mParent->i_onDeviceFilterChange(this);
    }

    return S_OK;
}


////////////////////////////////////////////////////////////////////////////////
// HostUSBDeviceFilter
////////////////////////////////////////////////////////////////////////////////

// constructor / destructor
////////////////////////////////////////////////////////////////////////////////

HostUSBDeviceFilter::HostUSBDeviceFilter()
    : mParent(NULL)
{
}

HostUSBDeviceFilter::~HostUSBDeviceFilter()
{
}


HRESULT HostUSBDeviceFilter::FinalConstruct()
{
    return S_OK;
}

void HostUSBDeviceFilter::FinalRelease()
{
    uninit();
}

// public initializer/uninitializer for internal purposes only
////////////////////////////////////////////////////////////////////////////////

/**
 *  Initializes the USB device filter object.
 *
 *  @param aParent  Handle of the parent object.
 *  @param data     Settings data.
 */
HRESULT HostUSBDeviceFilter::init(Host *aParent,
                                  const settings::USBDeviceFilter &data)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent && !data.strName.isEmpty(), E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    /* register with parent early, since uninit() will unconditionally
     * unregister on failure */
    mParent->i_addChild(this);

    bd.allocate();
    bd->mData.strName = data.strName;
    bd->mData.fActive = data.fActive;
    USBFilterInit (&bd->mUSBFilter, USBFILTERTYPE_IGNORE);
    bd->mRemote = NULL;
    bd->mData.ulMaskedInterfaces = 0;

    mInList = false;

    /* use setters for the attributes below to reuse parsing errors
     * handling */

    HRESULT hrc = setAction(data.action);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_VENDOR_ID, data.strVendorId);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_PRODUCT_ID, data.strProductId);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_DEVICE, data.strRevision);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_MANUFACTURER_STR, data.strManufacturer);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_PRODUCT_ID, data.strProduct);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_SERIAL_NUMBER_STR, data.strSerialNumber);
    if (FAILED(hrc)) return hrc;

    hrc = i_usbFilterFieldSetter(USBFILTERIDX_PORT, data.strPort);
    if (FAILED(hrc)) return hrc;

    /* Confirm successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the USB device filter object (short version).
 *
 *  @param aParent  Handle of the parent object.
 *  @param aName    Filter name.
 */
HRESULT HostUSBDeviceFilter::init(Host *aParent, IN_BSTR aName)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent && aName && *aName, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    /* register with parent early, since uninit() will unconditionally
     * unregister on failure */
    mParent->i_addChild(this);

    bd.allocate();

    bd->mData.strName = Utf8Str(aName);
    bd->mData.fActive = FALSE;
    mInList = false;
    USBFilterInit(&bd->mUSBFilter, USBFILTERTYPE_IGNORE);
    bd->mRemote = NULL;
    bd->mData.ulMaskedInterfaces = 0;

    /* Confirm successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void HostUSBDeviceFilter::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    mInList = false;

    bd.free();

    unconst(mParent) = NULL;
}

/**
 * Most of the USB bits are protect by one lock to simplify things.
 * This lock is currently the one of the Host object, which happens
 * to be our parent.
 */
RWLockHandle *HostUSBDeviceFilter::lockHandle() const
{
    return mParent->lockHandle();
}


// IUSBDeviceFilter properties
////////////////////////////////////////////////////////////////////////////////
HRESULT HostUSBDeviceFilter::getName(com::Utf8Str &aName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aName = bd->mData.strName;

    return S_OK;
}


HRESULT HostUSBDeviceFilter::setName(const com::Utf8Str &aName)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (bd->mData.strName != aName)
    {
        bd->mData.strName = aName;

        /* leave the lock before informing callbacks */
        alock.release();

        return mParent->i_onUSBDeviceFilterChange(this);
    }

    return S_OK;
}


HRESULT HostUSBDeviceFilter::getActive(BOOL *aActive)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aActive = bd->mData.fActive;

    return S_OK;
}


HRESULT HostUSBDeviceFilter::setActive(BOOL aActive)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (bd->mData.fActive != RT_BOOL(aActive))
    {
        bd->mData.fActive = RT_BOOL(aActive);

        /* leave the lock before informing callbacks */
        alock.release();

        return mParent->i_onUSBDeviceFilterChange(this, TRUE /* aActiveChanged  */);
    }

    return S_OK;
}

HRESULT HostUSBDeviceFilter::getVendorId(com::Utf8Str &aVendorId)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_VENDOR_ID, aVendorId);
}

HRESULT HostUSBDeviceFilter::setVendorId(const com::Utf8Str &aVendorId)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_VENDOR_ID, aVendorId);
}

HRESULT HostUSBDeviceFilter::getProductId(com::Utf8Str &aProductId)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_PRODUCT_ID, aProductId);
}

HRESULT HostUSBDeviceFilter::setProductId(const com::Utf8Str &aProductId)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_PRODUCT_ID, aProductId);
}

HRESULT HostUSBDeviceFilter::getRevision(com::Utf8Str &aRevision)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_DEVICE, aRevision);
}

HRESULT HostUSBDeviceFilter::setRevision(const com::Utf8Str &aRevision)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_DEVICE, aRevision);
}

HRESULT HostUSBDeviceFilter::getManufacturer(com::Utf8Str &aManufacturer)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_MANUFACTURER_STR, aManufacturer);
}

HRESULT HostUSBDeviceFilter::setManufacturer(const com::Utf8Str &aManufacturer)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_MANUFACTURER_STR, aManufacturer);
}

HRESULT HostUSBDeviceFilter::getProduct(com::Utf8Str &aProduct)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_PRODUCT_STR, aProduct);
}

HRESULT HostUSBDeviceFilter::setProduct(const com::Utf8Str &aProduct)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_PRODUCT_STR, aProduct);
}

HRESULT HostUSBDeviceFilter::getSerialNumber(com::Utf8Str &aSerialNumber)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_SERIAL_NUMBER_STR, aSerialNumber);
}

HRESULT HostUSBDeviceFilter::setSerialNumber(const com::Utf8Str &aSerialNumber)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_SERIAL_NUMBER_STR, aSerialNumber);
}

HRESULT HostUSBDeviceFilter::getPort(com::Utf8Str &aPort)
{
    return i_usbFilterFieldGetter(USBFILTERIDX_PORT, aPort);
}

HRESULT HostUSBDeviceFilter::setPort(const com::Utf8Str &aPort)
{
    return i_usbFilterFieldSetter(USBFILTERIDX_PORT, aPort);
}

HRESULT HostUSBDeviceFilter::getRemote(com::Utf8Str &aRemote)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aRemote = bd->mRemote.string();

    return S_OK;
}

HRESULT HostUSBDeviceFilter::setRemote(const com::Utf8Str & /* aRemote */)
{
    return setError(E_NOTIMPL,
                    tr("The remote state filter is not supported by IHostUSBDeviceFilter objects"));
}


HRESULT HostUSBDeviceFilter::getMaskedInterfaces(ULONG *aMaskedIfs)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aMaskedIfs = bd->mData.ulMaskedInterfaces;

    return S_OK;
}
HRESULT HostUSBDeviceFilter::setMaskedInterfaces(ULONG /* aMaskedIfs */)
{
    return setError(E_NOTIMPL,
                    tr("The masked interfaces property is not applicable to IHostUSBDeviceFilter objects"));
}

// wrapped IHostUSBDeviceFilter properties
////////////////////////////////////////////////////////////////////////////////
HRESULT HostUSBDeviceFilter::getAction(USBDeviceFilterAction_T *aAction)
{
    CheckComArgOutPointerValid(aAction);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    switch (USBFilterGetFilterType(&bd->mUSBFilter))
    {
        case USBFILTERTYPE_IGNORE:   *aAction = USBDeviceFilterAction_Ignore; break;
        case USBFILTERTYPE_CAPTURE:  *aAction = USBDeviceFilterAction_Hold; break;
        default:                     *aAction = USBDeviceFilterAction_Null; break;
    }

    return S_OK;
}


HRESULT HostUSBDeviceFilter::setAction(USBDeviceFilterAction_T aAction)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    USBFILTERTYPE filterType;
    switch (aAction)
    {
        case USBDeviceFilterAction_Ignore:   filterType = USBFILTERTYPE_IGNORE; break;
        case USBDeviceFilterAction_Hold:     filterType = USBFILTERTYPE_CAPTURE; break;
        case USBDeviceFilterAction_Null:
            return setError(E_INVALIDARG,
                            tr("Action value InvalidUSBDeviceFilterAction is not permitted"));
        default:
            return setError(E_INVALIDARG,
                            tr("Invalid action %d"),
                            aAction);
    }
    if (USBFilterGetFilterType(&bd->mUSBFilter) != filterType)
    {
        int vrc = USBFilterSetFilterType(&bd->mUSBFilter, filterType);
        if (RT_FAILURE(vrc))
            return setError(E_INVALIDARG,
                            tr("Unexpected error %Rrc"),
                            vrc);

        /* leave the lock before informing callbacks */
        alock.release();

        return mParent->i_onUSBDeviceFilterChange(this);
    }

    return S_OK;
}


// IHostUSBDeviceFilter properties
////////////////////////////////////////////////////////////////////////////////
/**
 *  Generic USB filter field getter.
 *
 *  @param  aIdx    The field index.
 *  @param  aStr    Where to store the value.
 *
 *  @return COM status.
 */
HRESULT HostUSBDeviceFilter::i_usbFilterFieldGetter(USBFILTERIDX aIdx, com::Utf8Str &aStr)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    i_usbFilterFieldToString(&bd->mUSBFilter, aIdx, aStr);
    return S_OK;
}

void HostUSBDeviceFilter::i_saveSettings(settings::USBDeviceFilter &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    data.strName = bd->mData.strName;
    data.fActive = bd->mData.fActive;
    i_usbFilterFieldToString(&bd->mUSBFilter, USBFILTERIDX_VENDOR_ID, data.strVendorId);
    i_usbFilterFieldToString(&bd->mUSBFilter, USBFILTERIDX_PRODUCT_ID, data.strProductId);
    i_usbFilterFieldToString(&bd->mUSBFilter, USBFILTERIDX_DEVICE, data.strRevision);
    i_usbFilterFieldToString(&bd->mUSBFilter, USBFILTERIDX_MANUFACTURER_STR, data.strManufacturer);
    i_usbFilterFieldToString(&bd->mUSBFilter, USBFILTERIDX_PRODUCT_STR, data.strProduct);
    i_usbFilterFieldToString(&bd->mUSBFilter, USBFILTERIDX_SERIAL_NUMBER_STR, data.strSerialNumber);
    i_usbFilterFieldToString(&bd->mUSBFilter, USBFILTERIDX_PORT, data.strPort);

    COMGETTER(Action)(&data.action);
}


/**
 *  Generic USB filter field setter.
 *
 *  @param  aIdx    The field index.
 *  @param  aStr    The new value.
 *
 *  @return COM status.
 */
HRESULT HostUSBDeviceFilter::i_usbFilterFieldSetter(USBFILTERIDX aIdx, const com::Utf8Str &aStr)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Utf8Str strOld;
    i_usbFilterFieldToString(&bd->mUSBFilter, aIdx, strOld);
    if (strOld != aStr)
    {
        //bd.backup();
        com::Utf8Str errStr;
        HRESULT hrc = USBDeviceFilter::i_usbFilterFieldFromString(&bd->mUSBFilter, aIdx, aStr, errStr);
        if (FAILED(hrc))
        {
            //bd.rollback();
            return setError(hrc, "%s", errStr.c_str());
        }

        /* leave the lock before informing callbacks */
        alock.release();

        return mParent->i_onUSBDeviceFilterChange(this);
    }

    return S_OK;
}
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
