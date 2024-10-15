/* $Id: USBProxyBackend.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB Proxy Service (base) class.
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

#define LOG_GROUP LOG_GROUP_MAIN_USBPROXYBACKEND
#include "USBProxyBackend.h"
#include "USBProxyService.h"
#include "HostUSBDeviceImpl.h"
#include "HostImpl.h"
#include "MachineImpl.h"
#include "VirtualBoxImpl.h"

#include "AutoCaller.h"
#include "LoggingNew.h"

#include <VBox/com/array.h>
#include <iprt/errcore.h>
#include <iprt/asm.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/mem.h>
#include <iprt/string.h>


/**
 * Empty constructor.
 */
USBProxyBackend::USBProxyBackend()
{
    LogFlowThisFunc(("\n"));
}


/**
 * Empty destructor.
 */
USBProxyBackend::~USBProxyBackend()
{
}


HRESULT USBProxyBackend::FinalConstruct()
{
    return BaseFinalConstruct();
}

void USBProxyBackend::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

/**
 * Stub needed as long as the class isn't virtual
 */
int USBProxyBackend::init(USBProxyService *pUsbProxyService, const com::Utf8Str &strId,
                          const com::Utf8Str &strAddress, bool fLoadingSettings)
{
    RT_NOREF1(fLoadingSettings);

    m_pUsbProxyService    = pUsbProxyService;
    mThread               = NIL_RTTHREAD;
    mTerminate            = false;
    unconst(m_strId)      = strId;
    m_cRefs               = 0;
    unconst(m_strAddress) = strAddress;

    unconst(m_strBackend) = Utf8Str::Empty;

    return VINF_SUCCESS;
}


void USBProxyBackend::uninit()
{
    LogFlowThisFunc(("\n"));
    Assert(mThread == NIL_RTTHREAD);
    mTerminate = true;
    m_pUsbProxyService = NULL;
    m_llDevices.clear();
}

/**
 * Query if the service is active and working.
 *
 * @returns true if the service is up running.
 * @returns false if the service isn't running.
 */
bool USBProxyBackend::isActive(void)
{
    return mThread != NIL_RTTHREAD;
}


/**
 * Returns the ID of the instance.
 *
 * @returns ID string for the instance.
 */
const com::Utf8Str &USBProxyBackend::i_getId()
{
    return m_strId;
}


/**
 * Returns the address of the instance.
 *
 * @returns ID string for the instance.
 */
const com::Utf8Str &USBProxyBackend::i_getAddress()
{
    return m_strAddress;
}


/**
 * Returns the backend of the instance.
 *
 * @returns ID string for the instance.
 */
const com::Utf8Str &USBProxyBackend::i_getBackend()
{
    return m_strBackend;
}

/**
 * Returns the current reference counter for the backend.
 */
uint32_t USBProxyBackend::i_getRefCount()
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    return m_cRefs;
}


/**
 * A filter was inserted / loaded.
 *
 * @param   aFilter         Pointer to the inserted filter.
 * @return  ID of the inserted filter
 */
void *USBProxyBackend::insertFilter(PCUSBFILTER aFilter)
{
    // return non-NULL to fake success.
    NOREF(aFilter);
    return (void *)1;
}


/**
 * A filter was removed.
 *
 * @param   aId             ID of the filter to remove
 */
void USBProxyBackend::removeFilter(void *aId)
{
    NOREF(aId);
}


/**
 * A VM is trying to capture a device, do necessary preparations.
 *
 * @returns VBox status code.
 * @param   aDevice     The device in question.
 */
int USBProxyBackend::captureDevice(HostUSBDevice *aDevice)
{
    NOREF(aDevice);
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Notification that an async captureDevice() operation completed.
 *
 * This is used by the proxy to release temporary filters.
 *
 * @param   aDevice     The device in question.
 * @param   aSuccess    Whether it succeeded or failed.
 */
void USBProxyBackend::captureDeviceCompleted(HostUSBDevice *aDevice, bool aSuccess)
{
    NOREF(aDevice);
    NOREF(aSuccess);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    incRef();
}


/**
 * A VM is releasing a device back to the host.
 *
 * @returns VBox status code.
 * @param   aDevice     The device in question.
 */
int USBProxyBackend::releaseDevice(HostUSBDevice *aDevice)
{
    NOREF(aDevice);
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Notification that an async releaseDevice() operation completed.
 *
 * This is used by the proxy to release temporary filters.
 *
 * @param   aDevice     The device in question.
 * @param   aSuccess    Whether it succeeded or failed.
 */
void USBProxyBackend::releaseDeviceCompleted(HostUSBDevice *aDevice, bool aSuccess)
{
    NOREF(aDevice);
    NOREF(aSuccess);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    decRef();
}


bool USBProxyBackend::isFakeUpdateRequired()
{
    return false;
}

/**
 * Returns whether devices reported by this backend go through a de/re-attach
 * and device re-enumeration cycle when they are captured or released.
 */
bool USBProxyBackend::i_isDevReEnumerationRequired()
{
    return false;
}

// Internals
/////////////////////////////////////////////////////////////////////////////


/**
 * Starts the service.
 *
 * @returns VBox status code.
 */
int USBProxyBackend::start(void)
{
    int vrc = VINF_SUCCESS;
    if (mThread == NIL_RTTHREAD)
    {
        /*
         * Force update before starting the poller thread.
         */
        vrc = wait(0);
        if (vrc == VERR_TIMEOUT || vrc == VERR_INTERRUPTED || RT_SUCCESS(vrc))
        {
            PUSBDEVICE pDevices = getDevices();
            updateDeviceList(pDevices);

            /*
             * Create the poller thread which will look for changes.
             */
            mTerminate = false;
            vrc = RTThreadCreate(&mThread, USBProxyBackend::serviceThread, this,
                                 0, RTTHREADTYPE_INFREQUENT_POLLER, RTTHREADFLAGS_WAITABLE, "USBPROXY");
            AssertRC(vrc);
            if (RT_SUCCESS(vrc))
                LogFlowThisFunc(("started mThread=%RTthrd\n", mThread));
            else
                mThread = NIL_RTTHREAD;
        }
    }
    else
        LogFlowThisFunc(("already running, mThread=%RTthrd\n", mThread));
    return vrc;
}


/**
 * Stops the service.
 *
 * @returns VBox status code.
 */
int USBProxyBackend::stop(void)
{
    int vrc = VINF_SUCCESS;
    if (mThread != NIL_RTTHREAD)
    {
        /*
         * Mark the thread for termination and kick it.
         */
        ASMAtomicXchgSize(&mTerminate, true);
        vrc = interruptWait();
        AssertRC(vrc);

        /*
         * Wait for the thread to finish and then update the state.
         */
        vrc = RTThreadWait(mThread, 60000, NULL);
        if (vrc == VERR_INVALID_HANDLE)
            vrc = VINF_SUCCESS;
        if (RT_SUCCESS(vrc))
        {
            LogFlowThisFunc(("stopped mThread=%RTthrd\n", mThread));
            mThread = NIL_RTTHREAD;
            mTerminate = false;
        }
        else
            AssertRC(vrc);
    }
    else
        LogFlowThisFunc(("not active\n"));

    /* Make sure there is no device from us in the list anymore. */
    updateDeviceList(NULL);

    return vrc;
}


/**
 * The service thread created by start().
 *
 * @param   Thread      The thread handle.
 * @param   pvUser      Pointer to the USBProxyBackend instance.
 */
/*static*/ DECLCALLBACK(int) USBProxyBackend::serviceThread(RTTHREAD /* Thread */, void *pvUser)
{
    USBProxyBackend *pThis = (USBProxyBackend *)pvUser;
    LogFlowFunc(("pThis=%p\n", pThis));
    pThis->serviceThreadInit();
    int vrc = VINF_SUCCESS;

    /*
     * Processing loop.
     */
    for (;;)
    {
        vrc = pThis->wait(RT_INDEFINITE_WAIT);
        if (RT_FAILURE(vrc) && vrc != VERR_INTERRUPTED && vrc != VERR_TIMEOUT)
            break;
        if (pThis->mTerminate)
            break;

        PUSBDEVICE pDevices = pThis->getDevices();
        pThis->updateDeviceList(pDevices);
    }

    pThis->serviceThreadTerm();
    LogFlowFunc(("returns %Rrc\n", vrc));
    return vrc;
}


/**
 * First call made on the service thread, use it to do
 * thread initialization.
 *
 * The default implementation in USBProxyBackend just a dummy stub.
 */
void USBProxyBackend::serviceThreadInit(void)
{
}


/**
 * Last call made on the service thread, use it to do
 * thread termination.
 */
void USBProxyBackend::serviceThreadTerm(void)
{
}


/**
 * Wait for a change in the USB devices attached to the host.
 *
 * The default implementation in USBProxyBackend just a dummy stub.
 *
 * @returns VBox status code.  VERR_INTERRUPTED and VERR_TIMEOUT are considered
 *          harmless, while all other error status are fatal.
 * @param   aMillies    Number of milliseconds to wait.
 */
int USBProxyBackend::wait(RTMSINTERVAL aMillies)
{
    return RTThreadSleep(RT_MIN(aMillies, 250));
}


/**
 * Interrupt any wait() call in progress.
 *
 * The default implementation in USBProxyBackend just a dummy stub.
 *
 * @returns VBox status code.
 */
int USBProxyBackend::interruptWait(void)
{
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Get a list of USB device currently attached to the host.
 *
 * The default implementation in USBProxyBackend just a dummy stub.
 *
 * @returns Pointer to a list of USB devices.
 *          The list nodes are freed individually by calling freeDevice().
 */
PUSBDEVICE USBProxyBackend::getDevices(void)
{
    return NULL;
}


/**
 * Increments the reference counter.
 *
 * @returns New reference count value.
 */
uint32_t USBProxyBackend::incRef()
{
    Assert(isWriteLockOnCurrentThread());

    return ++m_cRefs;
}

/**
 * Decrements the reference counter.
 *
 * @returns New reference count value.
 */
uint32_t USBProxyBackend::decRef()
{
    Assert(isWriteLockOnCurrentThread());

    return --m_cRefs;
}


/**
 * Free all the members of a USB device returned by getDevice().
 *
 * @param   pDevice     Pointer to the device.
 */
/*static*/ void
USBProxyBackend::freeDeviceMembers(PUSBDEVICE pDevice)
{
    RTStrFree((char *)pDevice->pszManufacturer);
    pDevice->pszManufacturer = NULL;
    RTStrFree((char *)pDevice->pszProduct);
    pDevice->pszProduct = NULL;
    RTStrFree((char *)pDevice->pszSerialNumber);
    pDevice->pszSerialNumber = NULL;

    RTStrFree((char *)pDevice->pszAddress);
    pDevice->pszAddress = NULL;
    RTStrFree((char *)pDevice->pszBackend);
    pDevice->pszBackend = NULL;
#ifdef RT_OS_WINDOWS
    RTStrFree(pDevice->pszAltAddress);
    pDevice->pszAltAddress = NULL;
    RTStrFree(pDevice->pszHubName);
    pDevice->pszHubName = NULL;
#elif defined(RT_OS_SOLARIS)
    RTStrFree(pDevice->pszDevicePath);
    pDevice->pszDevicePath = NULL;
#endif
}


/**
 * Free one USB device returned by getDevice().
 *
 * @param   pDevice     Pointer to the device.
 */
/*static*/ void
USBProxyBackend::freeDevice(PUSBDEVICE pDevice)
{
    freeDeviceMembers(pDevice);
    RTMemFree(pDevice);
}

void USBProxyBackend::deviceAdded(ComObjPtr<HostUSBDevice> &aDevice, PUSBDEVICE pDev)
{
    /* Nothing to do. */
    NOREF(aDevice);
    NOREF(pDev);
}

/**
 * Initializes a filter with the data from the specified device.
 *
 * @param   aFilter     The filter to fill.
 * @param   aDevice     The device to fill it with.
 */
/*static*/ void
USBProxyBackend::initFilterFromDevice(PUSBFILTER aFilter, HostUSBDevice *aDevice)
{
    PCUSBDEVICE pDev = aDevice->i_getUsbData();
    int vrc;

    vrc = USBFilterSetNumExact(aFilter, USBFILTERIDX_VENDOR_ID,         pDev->idVendor,         true); AssertRC(vrc);
    vrc = USBFilterSetNumExact(aFilter, USBFILTERIDX_PRODUCT_ID,        pDev->idProduct,        true); AssertRC(vrc);
    vrc = USBFilterSetNumExact(aFilter, USBFILTERIDX_DEVICE_REV,        pDev->bcdDevice,        true); AssertRC(vrc);
    vrc = USBFilterSetNumExact(aFilter, USBFILTERIDX_DEVICE_CLASS,      pDev->bDeviceClass,     true); AssertRC(vrc);
    vrc = USBFilterSetNumExact(aFilter, USBFILTERIDX_DEVICE_SUB_CLASS,  pDev->bDeviceSubClass,  true); AssertRC(vrc);
    vrc = USBFilterSetNumExact(aFilter, USBFILTERIDX_DEVICE_PROTOCOL,   pDev->bDeviceProtocol,  true); AssertRC(vrc);
    vrc = USBFilterSetNumExact(aFilter, USBFILTERIDX_PORT,              pDev->bPort,            false); AssertRC(vrc);
    vrc = USBFilterSetNumExact(aFilter, USBFILTERIDX_BUS,               pDev->bBus,             false); AssertRC(vrc);
    if (pDev->pszSerialNumber)
    {
        vrc = USBFilterSetStringExact(aFilter, USBFILTERIDX_SERIAL_NUMBER_STR, pDev->pszSerialNumber,
                                      true /*fMustBePresent*/, true /*fPurge*/);
        AssertRC(vrc);
    }
    if (pDev->pszProduct)
    {
        vrc = USBFilterSetStringExact(aFilter, USBFILTERIDX_PRODUCT_STR, pDev->pszProduct,
                                      true /*fMustBePresent*/, true /*fPurge*/);
        AssertRC(vrc);
    }
    if (pDev->pszManufacturer)
    {
        vrc = USBFilterSetStringExact(aFilter, USBFILTERIDX_MANUFACTURER_STR, pDev->pszManufacturer,
                                      true /*fMustBePresent*/, true /*fPurge*/);
        AssertRC(vrc);
    }
}

HRESULT USBProxyBackend::getName(com::Utf8Str &aName)
{
    /* strId is constant during life time, no need to lock */
    aName = m_strId;
    return S_OK;
}

HRESULT USBProxyBackend::getType(com::Utf8Str &aType)
{
    aType = Utf8Str::Empty;
    return S_OK;
}

/**
 * Sort a list of USB devices.
 *
 * @returns Pointer to the head of the sorted doubly linked list.
 * @param   pDevices        Head pointer (can be both singly and doubly linked list).
 */
static PUSBDEVICE sortDevices(PUSBDEVICE pDevices)
{
    PUSBDEVICE pHead = NULL;
    PUSBDEVICE pTail = NULL;
    while (pDevices)
    {
        /* unlink head */
        PUSBDEVICE pDev = pDevices;
        pDevices = pDev->pNext;
        if (pDevices)
            pDevices->pPrev = NULL;

        /* find location. */
        PUSBDEVICE pCur = pTail;
        while (     pCur
               &&   HostUSBDevice::i_compare(pCur, pDev) > 0)
            pCur = pCur->pPrev;

        /* insert (after pCur) */
        pDev->pPrev = pCur;
        if (pCur)
        {
            pDev->pNext = pCur->pNext;
            pCur->pNext = pDev;
            if (pDev->pNext)
                pDev->pNext->pPrev = pDev;
            else
                pTail = pDev;
        }
        else
        {
            pDev->pNext = pHead;
            if (pHead)
                pHead->pPrev = pDev;
            else
                pTail = pDev;
            pHead = pDev;
        }
    }

    LogFlowFuncLeave();
    return pHead;
}


/**
 * Process any relevant changes in the attached USB devices.
 *
 * This is called from any available USB proxy backends service thread when they discover
 * a change.
 */
void USBProxyBackend::updateDeviceList(PUSBDEVICE pDevices)
{
    LogFlowThisFunc(("\n"));

    pDevices = sortDevices(pDevices);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Compare previous list with the new list of devices
     * and merge in any changes while notifying Host.
     */
    HostUSBDeviceList::iterator it = this->m_llDevices.begin();
    while (    it != m_llDevices.end()
           || pDevices)
    {
        ComObjPtr<HostUSBDevice> pHostDevice;

        if (it != m_llDevices.end())
            pHostDevice = *it;

        /*
         * Assert that the object is still alive (we still reference it in
         * the collection and we're the only one who calls uninit() on it.
         */
        AutoCaller devCaller(pHostDevice.isNull() ? NULL : pHostDevice);
        AssertComRC(devCaller.hrc());

        /*
         * Lock the device object since we will read/write its
         * properties. All Host callbacks also imply the object is locked.
         */
        AutoWriteLock devLock(pHostDevice.isNull() ? NULL : pHostDevice
                              COMMA_LOCKVAL_SRC_POS);

        /* We should never get devices from other backends here. */
        Assert(pHostDevice.isNull() || pHostDevice->i_getUsbProxyBackend() == this);

        /*
         * Compare.
         */
        int iDiff;
        if (pHostDevice.isNull())
            iDiff = 1;
        else
        {
            if (!pDevices)
                iDiff = -1;
            else
                iDiff = pHostDevice->i_compare(pDevices);
        }
        if (!iDiff)
        {
            /*
             * The device still there, update the state and move on. The PUSBDEVICE
             * structure is eaten by updateDeviceState / HostUSBDevice::updateState().
             */
            PUSBDEVICE pCur = pDevices;
            pDevices = pDevices->pNext;
            pCur->pPrev = pCur->pNext = NULL;

            devLock.release();
            alock.release();
            m_pUsbProxyService->i_updateDeviceState(pHostDevice, pCur, isFakeUpdateRequired());
            alock.acquire();
            ++it;
        }
        else
        {
            if (iDiff > 0)
            {
                /*
                 * Head of pDevices was attached.
                 */
                PUSBDEVICE pNew = pDevices;
                pDevices = pDevices->pNext;
                pNew->pPrev = pNew->pNext = NULL;

                ComObjPtr<HostUSBDevice> NewObj;
                NewObj.createObject();
                NewObj->init(pNew, this);
                LogFlowThisFunc(("attached %p {%s} %s / %p:{.idVendor=%#06x, .idProduct=%#06x, .pszProduct=\"%s\", .pszManufacturer=\"%s\"}\n",
                     (HostUSBDevice *)NewObj,
                     NewObj->i_getName().c_str(),
                     NewObj->i_getStateName(),
                     pNew,
                     pNew->idVendor,
                     pNew->idProduct,
                     pNew->pszProduct,
                     pNew->pszManufacturer));

                m_llDevices.insert(it, NewObj);

                devLock.release();
                alock.release();
                /* Do any backend specific work. */
                deviceAdded(NewObj, pNew);
                m_pUsbProxyService->i_deviceAdded(NewObj, pNew);
                alock.acquire();
            }
            else
            {
                /*
                 * Check if the device was actually detached or logically detached
                 * as the result of a re-enumeration.
                 */
                if (!pHostDevice->i_wasActuallyDetached())
                    ++it;
                else
                {
                    it = m_llDevices.erase(it);
                    devLock.release();
                    alock.release();
                    m_pUsbProxyService->i_deviceRemoved(pHostDevice);
                    LogFlowThisFunc(("detached %p {%s}\n",
                         (HostUSBDevice *)pHostDevice,
                         pHostDevice->i_getName().c_str()));

                    /* from now on, the object is no more valid,
                     * uninitialize to avoid abuse */
                    devCaller.release();
                    pHostDevice->uninit();
                    alock.acquire();
                }
            }
        }
    } /* while */

    LogFlowThisFunc(("returns void\n"));
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
