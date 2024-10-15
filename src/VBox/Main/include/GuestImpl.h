/* $Id: GuestImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
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

#ifndef MAIN_INCLUDED_GuestImpl_h
#define MAIN_INCLUDED_GuestImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GuestWrap.h"
#include "VirtualBoxBase.h"
#include <iprt/list.h>
#include <iprt/time.h>
#include <VBox/ostypes.h>
#include <VBox/param.h>
#include <VBox/vmm/stam.h>

#include "AdditionsFacilityImpl.h"
#ifdef VBOX_WITH_GUEST_CONTROL
# include "GuestCtrlImplPrivate.h"
# include "GuestSessionImpl.h"
#endif
#ifdef VBOX_WITH_DRAG_AND_DROP
# include "GuestDnDSourceImpl.h"
# include "GuestDnDTargetImpl.h"
#endif
#include "EventImpl.h"
#include "HGCM.h"

typedef enum
{
    GUESTSTATTYPE_CPUUSER     = 0,
    GUESTSTATTYPE_CPUKERNEL   = 1,
    GUESTSTATTYPE_CPUIDLE     = 2,
    GUESTSTATTYPE_MEMTOTAL    = 3,
    GUESTSTATTYPE_MEMFREE     = 4,
    GUESTSTATTYPE_MEMBALLOON  = 5,
    GUESTSTATTYPE_MEMCACHE    = 6,
    GUESTSTATTYPE_PAGETOTAL   = 7,
    GUESTSTATTYPE_PAGEFREE    = 8,
    GUESTSTATTYPE_MAX         = 9
} GUESTSTATTYPE;

class Console;

class ATL_NO_VTABLE Guest :
    public GuestWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS (Guest)

    HRESULT FinalConstruct();
    void FinalRelease();

    // Public initializer/uninitializer for internal purposes only.
    HRESULT init(Console *aParent);
    void uninit();


public:
    /** @name Static internal methods.
     * @{ */
#ifdef VBOX_WITH_GUEST_CONTROL
    /** Static callback for handling guest control notifications. */
    static DECLCALLBACK(int) i_notifyCtrlDispatcher(void *pvExtension, uint32_t u32Function, void *pvData, uint32_t cbData);
#endif
    static DECLCALLBACK(void) i_staticUpdateStats(RTTIMERLR hTimerLR, void *pvUser, uint64_t iTick);
    /** @}  */

public:
    /** @name Public internal methods.
     * @{ */
    void i_enableVMMStatistics(BOOL aEnable) { mCollectVMMStats = aEnable; };
    void i_setAdditionsInfo(const com::Utf8Str &aInterfaceVersion, VBOXOSTYPE aOsType);
    void i_setAdditionsInfo2(uint32_t a_uFullVersion, const char *a_pszName, uint32_t a_uRevision, uint32_t a_fFeatures);
    bool i_facilityIsActive(VBoxGuestFacilityType enmFacility);
    bool i_facilityUpdate(VBoxGuestFacilityType a_enmFacility, VBoxGuestFacilityStatus a_enmStatus,
                          uint32_t a_fFlags, PCRTTIMESPEC a_pTimeSpecTS);
    ComObjPtr<Console> i_getConsole(void) { return mParent; }
    void i_setAdditionsStatus(VBoxGuestFacilityType a_enmFacility, VBoxGuestFacilityStatus a_enmStatus,
                              uint32_t a_fFlags, PCRTTIMESPEC a_pTimeSpecTS);
    void i_onUserStateChanged(const Utf8Str &aUser, const Utf8Str &aDomain, VBoxGuestUserState enmState,
                              const uint8_t *puDetails, uint32_t cbDetails);
    void i_setSupportedFeatures(uint32_t aCaps);
    HRESULT i_setStatistic(ULONG aCpuId, GUESTSTATTYPE enmType, ULONG aVal);
    BOOL i_isPageFusionEnabled();
    void i_setCpuCount(uint32_t aCpus) { mCpus = aCpus; }
    static HRESULT i_setErrorStatic(HRESULT aResultCode, const char *aText, ...)
    {
        va_list va;
        va_start(va, aText);
        HRESULT hrc = setErrorInternalV(aResultCode, getStaticClassIID(), getStaticComponentName(), aText, va, false, true);
        va_end(va);
        return hrc;
    }
    uint32_t    i_getAdditionsRevision(void) { return mData.mAdditionsRevision; }
    uint32_t    i_getAdditionsVersion(void) { return mData.mAdditionsVersionFull; }
    VBOXOSTYPE  i_getGuestOSType(void) const { return mData.mOSType; }
    /** Checks if the guest OS type is part of the windows NT family.  */
    bool        i_isGuestInWindowsNtFamily(void) const
    {
        return mData.mOSType < VBOXOSTYPE_OS2 && mData.mOSType >= VBOXOSTYPE_WinNT;
    }
#ifdef VBOX_WITH_GUEST_CONTROL
    int         i_dispatchToSession(PVBOXGUESTCTRLHOSTCBCTX pCtxCb, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb);
    int         i_sessionCreate(const GuestSessionStartupInfo &ssInfo, const GuestCredentials &guestCreds,
                                ComObjPtr<GuestSession> &pGuestSession);
    int         i_sessionRemove(uint32_t uSessionID);
    inline bool i_sessionExists(uint32_t uSessionID);
    /** Returns the VBOX_GUESTCTRL_GF_0_XXX mask reported by the guest. */
    uint64_t    i_getGuestControlFeatures0() const { return mData.mfGuestFeatures0; }
    /** Returns the VBOX_GUESTCTRL_GF_1_XXX mask reported by the guest. */
    uint64_t    i_getGuestControlFeatures1() const { return mData.mfGuestFeatures1; }
#endif
    /** @}  */

private:

     // wrapped IGuest properties
     HRESULT getOSTypeId(com::Utf8Str &aOSTypeId);
     HRESULT getAdditionsRunLevel(AdditionsRunLevelType_T *aAdditionsRunLevel);
     HRESULT getAdditionsVersion(com::Utf8Str &aAdditionsVersion);
     HRESULT getAdditionsRevision(ULONG *aAdditionsRevision);
     HRESULT getDnDSource(ComPtr<IGuestDnDSource> &aDnDSource);
     HRESULT getDnDTarget(ComPtr<IGuestDnDTarget> &aDnDTarget);
     HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
     HRESULT getFacilities(std::vector<ComPtr<IAdditionsFacility> > &aFacilities);
     HRESULT getSessions(std::vector<ComPtr<IGuestSession> > &aSessions);
     HRESULT getMemoryBalloonSize(ULONG *aMemoryBalloonSize);
     HRESULT setMemoryBalloonSize(ULONG aMemoryBalloonSize);
     HRESULT getStatisticsUpdateInterval(ULONG *aStatisticsUpdateInterval);
     HRESULT setStatisticsUpdateInterval(ULONG aStatisticsUpdateInterval);
     HRESULT internalGetStatistics(ULONG *aCpuUser,
                                   ULONG *aCpuKernel,
                                   ULONG *aCpuIdle,
                                   ULONG *aMemTotal,
                                   ULONG *aMemFree,
                                   ULONG *aMemBalloon,
                                   ULONG *aMemShared,
                                   ULONG *aMemCache,
                                   ULONG *aPagedTotal,
                                   ULONG *aMemAllocTotal,
                                   ULONG *aMemFreeTotal,
                                   ULONG *aMemBalloonTotal,
                                   ULONG *aMemSharedTotal);
     HRESULT getFacilityStatus(AdditionsFacilityType_T aFacility,
                               LONG64 *aTimestamp,
                               AdditionsFacilityStatus_T *aStatus);
     HRESULT getAdditionsStatus(AdditionsRunLevelType_T aLevel,
                                BOOL *aActive);
     HRESULT setCredentials(const com::Utf8Str &aUserName,
                            const com::Utf8Str &aPassword,
                            const com::Utf8Str &aDomain,
                            BOOL aAllowInteractiveLogon);

     // wrapped IGuest methods
     HRESULT createSession(const com::Utf8Str &aUser,
                           const com::Utf8Str &aPassword,
                           const com::Utf8Str &aDomain,
                           const com::Utf8Str &aSessionName,
                           ComPtr<IGuestSession> &aGuestSession);

     HRESULT findSession(const com::Utf8Str &aSessionName,
                         std::vector<ComPtr<IGuestSession> > &aSessions);
     HRESULT shutdown(const std::vector<GuestShutdownFlag_T> &aFlags);
     HRESULT updateGuestAdditions(const com::Utf8Str &aSource,
                                  const std::vector<com::Utf8Str> &aArguments,
                                  const std::vector<AdditionsUpdateFlag_T> &aFlags,
                                  ComPtr<IProgress> &aProgress);


    /** @name Private internal methods.
     * @{ */
    void i_updateStats(uint64_t iTick);
    static DECLCALLBACK(int) i_staticEnumStatsCallback(const char *pszName, STAMTYPE enmType, void *pvSample,
                                                       STAMUNIT enmUnit, const char *pszUnit, STAMVISIBILITY enmVisiblity,
                                                       const char *pszDesc, void *pvUser);

    /** @}  */

    typedef std::map< AdditionsFacilityType_T, ComObjPtr<AdditionsFacility> > FacilityMap;
    typedef std::map< AdditionsFacilityType_T, ComObjPtr<AdditionsFacility> >::iterator FacilityMapIter;
    typedef std::map< AdditionsFacilityType_T, ComObjPtr<AdditionsFacility> >::const_iterator FacilityMapIterConst;

#ifdef VBOX_WITH_GUEST_CONTROL
    /** Map for keeping the guest sessions. The primary key marks the guest session ID. */
    typedef std::map <uint32_t, ComObjPtr<GuestSession> > GuestSessions;
#endif

    struct Data
    {
        Data() : mOSType(VBOXOSTYPE_Unknown),  mAdditionsRunLevel(AdditionsRunLevelType_None)
            , mAdditionsVersionFull(0), mAdditionsRevision(0), mAdditionsFeatures(0)
#ifdef VBOX_WITH_GUEST_CONTROL
            , mfGuestFeatures0(0), mfGuestFeatures1(0)
#endif
        { }

        VBOXOSTYPE                  mOSType;        /**@< For internal used. VBOXOSTYPE_Unknown if not reported. */
        Utf8Str                     mOSTypeId;
        FacilityMap                 mFacilityMap;
        AdditionsRunLevelType_T     mAdditionsRunLevel;
        uint32_t                    mAdditionsVersionFull;
        Utf8Str                     mAdditionsVersionNew;
        uint32_t                    mAdditionsRevision;
        uint32_t                    mAdditionsFeatures;
        Utf8Str                     mInterfaceVersion;
#ifdef VBOX_WITH_GUEST_CONTROL
        GuestSessions               mGuestSessions;
        /** Guest control features (reported by the guest), VBOX_GUESTCTRL_GF_0_XXX. */
        uint64_t                    mfGuestFeatures0;
        /** Guest control features (reported by the guest), VBOX_GUESTCTRL_GF_1_XXX. */
        uint64_t                    mfGuestFeatures1;
#endif
    } mData;

    ULONG                           mMemoryBalloonSize;
    ULONG                           mStatUpdateInterval; /**< In seconds. */
    uint64_t                        mNetStatRx;
    uint64_t                        mNetStatTx;
    uint64_t                        mNetStatLastTs;
    ULONG                           mCurrentGuestStat[GUESTSTATTYPE_MAX];
    ULONG                           mCurrentGuestCpuUserStat[VMM_MAX_CPU_COUNT];
    ULONG                           mCurrentGuestCpuKernelStat[VMM_MAX_CPU_COUNT];
    ULONG                           mCurrentGuestCpuIdleStat[VMM_MAX_CPU_COUNT];
    ULONG                           mVmValidStats;
    BOOL                            mCollectVMMStats;
    BOOL                            mfPageFusionEnabled;
    uint32_t                        mCpus;

    const ComObjPtr<Console>        mParent;

    /**
     * This can safely be used without holding any locks.
     * An AutoCaller suffices to prevent it being destroy while in use and
     * internally there is a lock providing the necessary serialization.
     */
    const ComObjPtr<EventSource>    mEventSource;
#ifdef VBOX_WITH_GUEST_CONTROL
    /** General extension callback for guest control. */
    HGCMSVCEXTHANDLE                mhExtCtrl;
#endif

#ifdef VBOX_WITH_DRAG_AND_DROP
    /** The guest's DnD source. */
    const ComObjPtr<GuestDnDSource> mDnDSource;
    /** The guest's DnD target. */
    const ComObjPtr<GuestDnDTarget> mDnDTarget;
#endif

    RTTIMERLR                       mStatTimer;
    uint32_t                        mMagic; /** @todo r=andy Rename this to something more meaningful. */
};
#define GUEST_MAGIC 0xCEED2006u /** @todo r=andy Not very well defined!? */

#endif /* !MAIN_INCLUDED_GuestImpl_h */

