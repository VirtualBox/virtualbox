/* $Id: HostUSBDeviceImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox IHostUSBDevice COM interface implementation.
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

#ifndef MAIN_INCLUDED_HostUSBDeviceImpl_h
#define MAIN_INCLUDED_HostUSBDeviceImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VirtualBoxBase.h"
#include "USBDeviceFilterImpl.h"
#include <VBox/usb.h>
#include "HostUSBDeviceWrap.h"

class SessionMachine;
class USBProxyBackend;

/**
 * The unified state machine of HostUSBDevice.
 *
 * This is a super set of USBDEVICESTATE / USBDeviceState_T that
 * includes additional states for tracking state transitions.
 *
 * @remarks
 *  The CapturingForVM and CapturingForProxy states have been merged
 *  into Capturing with a destination state (AttachingToVM or HeldByProxy).
 *
 *  The DetachingFromVM state is a merge of DetachingFromVMToProxy and
 *  DetachingFromVMToHost and uses the destination state (HeldByProxy
 *  or ReleasingToHost) like Capturing.
 *
 *  The *AwaitingDetach and *AwaitingReattach substates (optionally used
 *  in Capturing, AttachingToVM, DetachingFromVM and ReleasingToHost) are
 *  implemented via a substate kHostUSBDeviceSubState.
 */
typedef enum
{
    /** The device is unsupported (HUB).
     * Next Host: PhysDetached.
     * Next VBox: No change permitted.
     */
    kHostUSBDeviceState_Unsupported = USBDEVICESTATE_UNSUPPORTED,
    /** The device is used exclusivly by the host or is inaccessible for some other reason.
     * Next Host: Capturable, Unused, PhysDetached.
     *            Run filters.
     * Next VBox: No change permitted.
     */
    kHostUSBDeviceState_UsedByHost = USBDEVICESTATE_USED_BY_HOST,
    /** The device is used by the host but can be captured.
     * Next Host: Unsupported, UsedByHost, Unused, PhysDetached.
     *            Run filters if Unused (for wildcard filters).
     * Next VBox: CapturingForVM, CapturingForProxy.
     */
    kHostUSBDeviceState_Capturable = USBDEVICESTATE_USED_BY_HOST_CAPTURABLE,
    /** The device is not used by the host and can be captured.
     * Next Host: UsedByHost, Capturable, PhysDetached
     *            Don't run any filters (done on state entry).
     * Next VBox: CapturingForVM, CapturingForProxy.
     */
    kHostUSBDeviceState_Unused = USBDEVICESTATE_UNUSED,
    /** The device is held captive by the proxy.
     * Next Host: PhysDetached
     * Next VBox: ReleasingHeld, AttachingToVM
     */
    kHostUSBDeviceState_HeldByProxy = USBDEVICESTATE_HELD_BY_PROXY,
    /** The device is in use by a VM.
     * Next Host: PhysDetachingFromVM
     * Next VBox: DetachingFromVM
     */
    kHostUSBDeviceState_UsedByVM = USBDEVICESTATE_USED_BY_GUEST,
    /** The device has been detach from both the host and VMs.
     * This is the final state. */
    kHostUSBDeviceState_PhysDetached = 9,


    /** The start of the transitional states. */
    kHostUSBDeviceState_FirstTransitional,

    /** The device is being seized from the host, either for HeldByProxy or for AttachToVM.
     *
     * On some hosts we will need to re-enumerate the in which case the sub-state
     * is employed to track this progress. On others, this is synchronous or faked, and
     * will will then leave the device in this state and poke the service thread to do
     * the completion state change.
     *
     * Next Host: PhysDetached.
     * Next VBox: HeldByProxy or AttachingToVM on success,
     *            previous state (Unused or Capturable) or UsedByHost on failure.
     */
    kHostUSBDeviceState_Capturing = kHostUSBDeviceState_FirstTransitional,

    /** The device is being released back to the host, following VM or Proxy usage.
     * Most hosts needs to re-enumerate the device and will therefore employ the
     * sub-state as during capturing. On the others we'll just leave it to the usb
     * service thread to advance the device state.
     *
     * Next Host: Unused, UsedByHost, Capturable.
     *            No filters.
     * Next VBox: PhysDetached (timeout), HeldByProxy (failure).
     */
    kHostUSBDeviceState_ReleasingToHost,

    /** The device is being attached to a VM.
     *
     * This requires IPC to the VM and we will not advance the state until
     * that completes.
     *
     * Next Host: PhysDetachingFromVM.
     * Next VBox: UsedByGuest, HeldByProxy (failure).
     */
    kHostUSBDeviceState_AttachingToVM,

    /** The device is being detached from a VM and will be returned to the proxy or host.
     *
     * This involves IPC and may or may not also require re-enumeration of the
     * device. Which means that it might transition directly into the ReleasingToHost state
     * because the client (VM) will do the actual re-enumeration.
     *
     * Next Host: PhysDetachingFromVM (?) or just PhysDetached.
     * Next VBox: ReleasingToHost, HeldByProxy.
     */
    kHostUSBDeviceState_DetachingFromVM,

    /** The device has been physically removed while a VM used it.
     *
     * This is the device state while VBoxSVC is doing IPC to the client (VM) telling it
     * to detach it.
     *
     * Next Host: None.
     * Next VBox: PhysDetached
     */
    kHostUSBDeviceState_PhysDetachingFromVM,

    /** Just an invalid state value for use as default for some methods. */
    kHostUSBDeviceState_Invalid = 0x7fff
} HostUSBDeviceState;


/**
 * Sub-state for dealing with device re-enumeration.
 */
typedef enum
{
    /** Not in any sub-state. */
    kHostUSBDeviceSubState_Default = 0,
    /** Awaiting a logical device detach following a device re-enumeration. */
    kHostUSBDeviceSubState_AwaitingDetach,
    /** Awaiting a logical device re-attach following a device re-enumeration. */
    kHostUSBDeviceSubState_AwaitingReAttach
} HostUSBDeviceSubState;


/**
 * Object class used to hold Host USB Device properties.
 */
class ATL_NO_VTABLE HostUSBDevice :
    public HostUSBDeviceWrap
{
public:
    DECLARE_COMMON_CLASS_METHODS(HostUSBDevice)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(PUSBDEVICE aUsb, USBProxyBackend *aUSBProxyBackend);
    void uninit();

    // public methods only for internal purposes

    /** @note Must be called from under the object read lock. */
    const Guid& i_getId() const { return mId; }

    /** @note Must be called from under the object read lock. */
    HostUSBDeviceState i_getUnistate() const { return mUniState; }

    /** @note Must be called from under the object read lock. */
    const char *i_getStateName() { return i_stateName (mUniState, mPendingUniState, mUniSubState); }

    /** @note Must be called from under the object read lock. */
    bool i_isCapturableOrHeld()
    {
        return mUniState == kHostUSBDeviceState_Unused
            || mUniState == kHostUSBDeviceState_Capturable
            || mUniState == kHostUSBDeviceState_HeldByProxy;
    }

    /** @note Must be called from under the object read lock. */
    ComObjPtr<SessionMachine> &i_getMachine() { return mMachine; }

    /** @note Must be called from under the object read lock. */
    PCUSBDEVICE i_getUsbData() const { return mUsb; }

    USBProxyBackend *i_getUsbProxyBackend() const { return mUSBProxyBackend; }

    com::Utf8Str i_getName();

    HRESULT i_requestCaptureForVM(SessionMachine *aMachine, bool aSetError,
                                  const com::Utf8Str &aCaptureFilename, ULONG aMaskedIfs = 0);
    HRESULT i_onDetachFromVM(SessionMachine *aMachine, bool aDone, bool *aRunFilters, bool aAbnormal = false);
    HRESULT i_requestReleaseToHost();
    HRESULT i_requestHold();
    bool i_wasActuallyDetached();
    void i_onPhysicalDetached();

    bool i_isMatch(const USBDeviceFilter::BackupableUSBDeviceFilterData &aData);
    int i_compare(PCUSBDEVICE aDev2);
    static int i_compare(PCUSBDEVICE aDev1, PCUSBDEVICE aDev2, bool aIsAwaitingReAttach = false);

    bool i_updateState(PCUSBDEVICE aDev, bool *aRunFilters, SessionMachine **aIgnoreMachine);
    bool i_updateStateFake(PCUSBDEVICE aDev, bool *aRunFilters, SessionMachine **aIgnoreMachine);

    static const char *i_stateName(HostUSBDeviceState aState,
                                   HostUSBDeviceState aPendingState = kHostUSBDeviceState_Invalid,
                                   HostUSBDeviceSubState aSubState = kHostUSBDeviceSubState_Default);

    void *i_getBackendUserData() { return m_pvBackendUser; }
    void i_setBackendUserData(void *pvBackendUser) { m_pvBackendUser = pvBackendUser; }

protected:

    HRESULT i_attachToVM(SessionMachine *aMachine, const com::Utf8Str &aCaptureFilename, ULONG aMaskedIfs = 0);
    void i_detachFromVM(HostUSBDeviceState aFinalState);
    void i_onPhysicalDetachedInternal();
    bool i_hasAsyncOperationTimedOut() const;

    bool i_setState (HostUSBDeviceState aNewState, HostUSBDeviceState aNewPendingState = kHostUSBDeviceState_Invalid,
                     HostUSBDeviceSubState aNewSubState = kHostUSBDeviceSubState_Default);
    bool i_startTransition (HostUSBDeviceState aNewState, HostUSBDeviceState aFinalState,
                            HostUSBDeviceSubState aNewSubState = kHostUSBDeviceSubState_Default);
    bool i_advanceTransition(bool aSkipReAttach = false);
    bool i_failTransition(HostUSBDeviceState a_enmStateHint);
    USBDeviceState_T i_canonicalState() const;

private:

    // wrapped IUSBDevice properties
    HRESULT getId(com::Guid &aId);
    HRESULT getVendorId(USHORT *aVendorId);
    HRESULT getProductId(USHORT *aProductId);
    HRESULT getRevision(USHORT *aRevision);
    HRESULT getManufacturer(com::Utf8Str &aManufacturer);
    HRESULT getProduct(com::Utf8Str &aProduct);
    HRESULT getSerialNumber(com::Utf8Str &aSerialNumber);
    HRESULT getAddress(com::Utf8Str &aAddress);
    HRESULT getPort(USHORT *aPort);
    HRESULT getPortPath(com::Utf8Str &aPortPath);
    HRESULT getVersion(USHORT *aVersion);
    HRESULT getPortVersion(USHORT *aPortVersion);
    HRESULT getSpeed(USBConnectionSpeed_T *aSpeed);
    HRESULT getRemote(BOOL *aRemote);
    HRESULT getState(USBDeviceState_T *aState);
    HRESULT getBackend(com::Utf8Str &aBackend);
    HRESULT getDeviceInfo(std::vector<com::Utf8Str> &aInfo);


    const Guid mId;

    /** @name The state machine variables
     * Only setState(), init() and uninit() will modify these members!
     * @{ */
    /** The RTTimeNanoTS() corresponding to the last state change.
     *
     * Old state machine: RTTimeNanoTS() of when mIsStatePending was set or mDetaching changed
     * from kNotDetaching. For operations that cannot be canceled it's 0. */
    uint64_t mLastStateChangeTS;
    /** Current state. */
    HostUSBDeviceState mUniState;
    /** Sub-state for tracking re-enumeration. */
    HostUSBDeviceSubState mUniSubState;
    /** The final state of an pending transition.
     * This is mainly a measure to reduce the number of HostUSBDeviceState values. */
    HostUSBDeviceState mPendingUniState;
    /** Previous state.
     * This is used for bailing out when a transition like capture fails. */
    HostUSBDeviceState mPrevUniState;
    /** Indicator set by onDetachedPhys and check when advancing a transitional state. */
    bool mIsPhysicallyDetached;
    /** @} */

    /** The machine the usb device is (being) attached to. */
    ComObjPtr<SessionMachine> mMachine;
    /** Pointer to the USB Proxy Backend instance. */
    USBProxyBackend *mUSBProxyBackend;
    /** Pointer to the USB Device structure owned by this device.
     * Only used for host devices. */
    PUSBDEVICE mUsb;
    /** The interface mask to be used in the pending capture.
     * This is a filter property. */
    ULONG mMaskedIfs;
    /** The name of this device. */
    Utf8Str mNameObj;
    /** The name of this device (for logging purposes).
     * This points to the string in mNameObj. */
    const char *mName;
    /** The filename to capture the USB traffic to. */
    com::Utf8Str mCaptureFilename;
    /** Optional opaque user data assigned by the USB proxy backend owning the device. */
    void        *m_pvBackendUser;
};

#endif /* !MAIN_INCLUDED_HostUSBDeviceImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
