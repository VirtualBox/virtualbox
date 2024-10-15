/* $Id: VBoxGuestPropSvc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Property Service: Host service entry points.
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

/** @page pg_svc_guest_properties   Guest Property HGCM Service
 *
 * This HGCM service allows the guest to set and query values in a property
 * store on the host.  The service proxies the guest requests to the service
 * owner on the host using a request callback provided by the owner, and is
 * notified of changes to properties made by the host.  It forwards these
 * notifications to clients in the guest which have expressed interest and
 * are waiting for notification.
 *
 * The service currently consists of two threads.  One of these is the main
 * HGCM service thread which deals with requests from the guest and from the
 * host.  The second thread sends the host asynchronous notifications of
 * changes made by the guest and deals with notification timeouts.
 *
 * Guest requests to wait for notification are added to a list of open
 * notification requests and completed when a corresponding guest property
 * is changed or when the request times out.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_HGCM
#include <VBox/HostServices/GuestPropertySvc.h>

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/cpp/autores.h>
#include <iprt/cpp/utils.h>
#include <iprt/cpp/ministring.h>
#include <VBox/err.h>
#include <VBox/hgcmsvc.h>
#include <iprt/mem.h>
#include <iprt/req.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/version.h>
#include <VBox/AssertGuest.h>

#include <list>


namespace guestProp {

/**
 * Structure for holding a property
 */
struct Property
{
    /** The string space core record. */
    RTSTRSPACECORE mStrCore;
    /** The name of the property */
    RTCString mName;
    /** The property value */
    RTCString mValue;
    /** The timestamp of the property */
    uint64_t mTimestamp;
    /** The property flags */
    uint32_t mFlags;

    /** Default constructor */
    Property() : mTimestamp(0), mFlags(GUEST_PROP_F_NILFLAG)
    {
        RT_ZERO(mStrCore);
    }
    /** Constructor with const char * */
    Property(const char *pcszName, const char *pcszValue, uint64_t nsTimestamp, uint32_t u32Flags)
        : mName(pcszName)
        , mValue(pcszValue)
        , mTimestamp(nsTimestamp)
        , mFlags(u32Flags)
    {
        RT_ZERO(mStrCore);
        mStrCore.pszString = mName.c_str();
    }
    /** Constructor with std::string */
    Property(RTCString const &rName, RTCString const &rValue, uint64_t nsTimestamp, uint32_t fFlags)
        : mName(rName)
        , mValue(rValue)
        , mTimestamp(nsTimestamp)
        , mFlags(fFlags)
    {}

    /** Does the property name match one of a set of patterns? */
    bool Matches(const char *pszPatterns) const
    {
        return (   pszPatterns[0] == '\0'  /* match all */
                || RTStrSimplePatternMultiMatch(pszPatterns, RTSTR_MAX,
                                                mName.c_str(), RTSTR_MAX,
                                                NULL)
               );
    }

    /** Are two properties equal? */
    bool operator==(const Property &prop)
    {
        if (mTimestamp != prop.mTimestamp)
            return false;
        if (mFlags != prop.mFlags)
            return false;
        if (mName != prop.mName)
            return false;
        if (mValue != prop.mValue)
            return false;
        return true;
    }

    /* Is the property nil? */
    bool isNull()
    {
        return mName.isEmpty();
    }
};
/** The properties list type */
typedef std::list <Property> PropertyList;

/**
 * Structure for holding an uncompleted guest call
 */
struct GuestCall
{
    uint32_t u32ClientId;
    /** The call handle */
    VBOXHGCMCALLHANDLE mHandle;
    /** The function that was requested */
    uint32_t mFunction;
    /** Number of call parameters. */
    uint32_t mParmsCnt;
    /** The call parameters */
    VBOXHGCMSVCPARM *mParms;
    /** The default return value, used for passing warnings */
    int mRc;

    /** The standard constructor */
    GuestCall(void) : u32ClientId(0), mFunction(0), mParmsCnt(0) {}
    /** The normal constructor */
    GuestCall(uint32_t aClientId, VBOXHGCMCALLHANDLE aHandle, uint32_t aFunction,
              uint32_t aParmsCnt, VBOXHGCMSVCPARM aParms[], int aRc)
              : u32ClientId(aClientId), mHandle(aHandle), mFunction(aFunction),
                mParmsCnt(aParmsCnt), mParms(aParms), mRc(aRc) {}
};
/** The guest call list type */
typedef std::list <GuestCall> CallList;

/**
 * Class containing the shared information service functionality.
 */
class Service : public RTCNonCopyable
{
private:
    /** Type definition for use in callback functions */
    typedef Service SELF;
    /** HGCM helper functions. */
    PVBOXHGCMSVCHELPERS mpHelpers;
    /** Global flags for the service */
    uint32_t mfGlobalFlags;
    /** The property string space handle. */
    RTSTRSPACE mhProperties;
    /** The number of properties. */
    unsigned mcProperties;
    /** The list of property changes for guest notifications;
     *  only used for timestamp tracking in notifications at the moment */
    PropertyList mGuestNotifications;
    /** The list of outstanding guest notification calls */
    CallList mGuestWaiters;
    /** @todo we should have classes for thread and request handler thread */
    /** Callback function supplied by the host for notification of updates
     * to properties */
    PFNHGCMSVCEXT mpfnHostCallback;
    /** User data pointer to be supplied to the host callback function */
    void *mpvHostData;
    /** The previous timestamp.
     * This is used by getCurrentTimestamp() to decrease the chance of
     * generating duplicate timestamps.  */
    uint64_t mPrevTimestamp;
    /** The number of consecutive timestamp adjustments that we've made.
     * Together with mPrevTimestamp, this defines a set of obsolete timestamp
     * values: {(mPrevTimestamp - mcTimestampAdjustments), ..., mPrevTimestamp} */
    uint64_t mcTimestampAdjustments;
    /** For helping setting host version properties _after_ restoring VMs. */
    bool m_fSetHostVersionProps;

    /**
     * Get the next property change notification from the queue of saved
     * notification based on the timestamp of the last notification seen.
     * Notifications will only be reported if the property name matches the
     * pattern given.
     *
     * @returns iprt status value
     * @returns VWRN_NOT_FOUND if the last notification was not found in the queue
     * @param   pszPatterns   the patterns to match the property name against
     * @param   nsTimestamp   the timestamp of the last notification
     * @param   pProp         where to return the property found.  If none is
     *                        found this will be set to nil.
     * @throws  nothing
     * @thread  HGCM
     */
    int getOldNotification(const char *pszPatterns, uint64_t nsTimestamp, Property *pProp)
    {
        AssertPtrReturn(pszPatterns, VERR_INVALID_POINTER);
        /* Zero means wait for a new notification. */
        AssertReturn(nsTimestamp != 0, VERR_INVALID_PARAMETER);
        AssertPtrReturn(pProp, VERR_INVALID_POINTER);
        int rc = getOldNotificationInternal(pszPatterns, nsTimestamp, pProp);

#ifdef VBOX_STRICT
        /*
         * ENSURE that pProp is the first event in the notification queue that:
         *  - Appears later than nsTimestamp
         *  - Matches the pszPatterns
         */
        /** @todo r=bird: This incorrectly ASSUMES that mTimestamp is unique.
         *  The timestamp resolution can be very coarse on windows for instance. */
        PropertyList::const_iterator it = mGuestNotifications.begin();
        for (;    it != mGuestNotifications.end()
               && it->mTimestamp != nsTimestamp; ++it)
        { /*nothing*/ }
        if (it == mGuestNotifications.end())  /* Not found */
            it = mGuestNotifications.begin();
        else
            ++it;  /* Next event */
        for (;    it != mGuestNotifications.end()
               && it->mTimestamp != pProp->mTimestamp; ++it)
            Assert(!it->Matches(pszPatterns));
        if (pProp->mTimestamp != 0)
        {
            Assert(*pProp == *it);
            Assert(pProp->Matches(pszPatterns));
        }
#endif /* VBOX_STRICT */
        return rc;
    }

    /**
     * Check whether we have permission to change a property.
     *
     * @returns Strict VBox status code.
     * @retval  VINF_SUCCESS if we do.
     * @retval  VERR_PERMISSION_DENIED if the value is read-only for the requesting
     *          side.
     * @retval  VINF_PERMISSION_DENIED if the side is globally marked read-only.
     *
     * @param   fFlags   the flags on the property in question
     * @param   isGuest  is the guest or the host trying to make the change?
     */
    int checkPermission(uint32_t fFlags, bool isGuest)
    {
        if (fFlags & (isGuest ? GUEST_PROP_F_RDONLYGUEST : GUEST_PROP_F_RDONLYHOST))
            return VERR_PERMISSION_DENIED;
        if (isGuest && (mfGlobalFlags & GUEST_PROP_F_RDONLYGUEST))
            return VINF_PERMISSION_DENIED;
        return VINF_SUCCESS;
    }

    /**
     * Check whether the property name is reserved for host changes only.
     *
     * @returns Boolean true (host reserved) or false (available to guest).
     *
     * @param   pszName  The property name to check.
     */
    bool checkHostReserved(const char *pszName)
    {
        if (RTStrStartsWith(pszName, "/VirtualBox/GuestAdd/VBoxService/"))
            return true;
        if (RTStrStartsWith(pszName, "/VirtualBox/GuestAdd/PAM/"))
            return true;
        if (RTStrStartsWith(pszName, "/VirtualBox/GuestAdd/Greeter/"))
            return true;
        if (RTStrStartsWith(pszName, "/VirtualBox/GuestAdd/SharedFolders/"))
            return true;
        if (RTStrStartsWith(pszName, "/VirtualBox/HostInfo/"))
            return true;
        if (RTStrStartsWith(pszName, "/VirtualBox/VMInfo/"))
            return true;
        return false;
    }

    /**
     * Gets a property.
     *
     * @returns Pointer to the property if found, NULL if not.
     *
     * @param   pszName     The name of the property to get.
     */
    Property *getPropertyInternal(const char *pszName)
    {
        return (Property *)RTStrSpaceGet(&mhProperties, pszName);
    }

public:
    explicit Service(PVBOXHGCMSVCHELPERS pHelpers)
        : mpHelpers(pHelpers)
        , mfGlobalFlags(GUEST_PROP_F_NILFLAG)
        , mhProperties(NULL)
        , mcProperties(0)
        , mpfnHostCallback(NULL)
        , mpvHostData(NULL)
        , mPrevTimestamp(0)
        , mcTimestampAdjustments(0)
        , m_fSetHostVersionProps(false)
        , mhThreadNotifyHost(NIL_RTTHREAD)
        , mhReqQNotifyHost(NIL_RTREQQUEUE)
    { }

    /**
     * @interface_method_impl{VBOXHGCMSVCFNTABLE,pfnUnload}
     * Simply deletes the service object
     */
    static DECLCALLBACK(int) svcUnload(void *pvService)
    {
        AssertLogRelReturn(RT_VALID_PTR(pvService), VERR_INVALID_PARAMETER);
        SELF *pSelf = reinterpret_cast<SELF *>(pvService);
        int rc = pSelf->uninit();
        AssertRC(rc);
        if (RT_SUCCESS(rc))
            delete pSelf;
        return rc;
    }

    /**
     * @interface_method_impl{VBOXHGCMSVCFNTABLE,pfnConnect}
     * Stub implementation of pfnConnect.
     */
    static DECLCALLBACK(int) svcConnect(void * /* pvService */,
                                        uint32_t /* u32ClientID */,
                                        void * /* pvClient */,
                                        uint32_t /*fRequestor*/,
                                        bool /*fRestoring*/)
    {
        return VINF_SUCCESS;
    }

    static DECLCALLBACK(int) svcDisconnect(void *pvService, uint32_t idClient, void *pvClient);

    /**
     * @interface_method_impl{VBOXHGCMSVCFNTABLE,pfnCall}
     * Wraps to the call member function
     */
    static DECLCALLBACK(void) svcCall(void * pvService,
                                      VBOXHGCMCALLHANDLE callHandle,
                                      uint32_t u32ClientID,
                                      void *pvClient,
                                      uint32_t u32Function,
                                      uint32_t cParms,
                                      VBOXHGCMSVCPARM paParms[],
                                      uint64_t tsArrival)
    {
        AssertLogRelReturnVoid(RT_VALID_PTR(pvService));
        LogFlowFunc(("pvService=%p, callHandle=%p, u32ClientID=%u, pvClient=%p, u32Function=%u, cParms=%u, paParms=%p\n", pvService, callHandle, u32ClientID, pvClient, u32Function, cParms, paParms));
        SELF *pSelf = reinterpret_cast<SELF *>(pvService);
        pSelf->call(callHandle, u32ClientID, pvClient, u32Function, cParms, paParms);
        LogFlowFunc(("returning\n"));
        RT_NOREF_PV(tsArrival);
    }

    /**
     * @interface_method_impl{VBOXHGCMSVCFNTABLE,pfnHostCall}
     * Wraps to the hostCall member function
     */
    static DECLCALLBACK(int) svcHostCall(void *pvService,
                                         uint32_t u32Function,
                                         uint32_t cParms,
                                         VBOXHGCMSVCPARM paParms[])
    {
        AssertLogRelReturn(RT_VALID_PTR(pvService), VERR_INVALID_PARAMETER);
        LogFlowFunc(("pvService=%p, u32Function=%u, cParms=%u, paParms=%p\n", pvService, u32Function, cParms, paParms));
        SELF *pSelf = reinterpret_cast<SELF *>(pvService);
        int rc = pSelf->hostCall(u32Function, cParms, paParms);
        LogFlowFunc(("rc=%Rrc\n", rc));
        return rc;
    }

    /**
     * @interface_method_impl{VBOXHGCMSVCFNTABLE,pfnRegisterExtension}
     * Installs a host callback for notifications of property changes.
     */
    static DECLCALLBACK(int) svcRegisterExtension(void *pvService,
                                                  PFNHGCMSVCEXT pfnExtension,
                                                  void *pvExtension)
    {
        AssertLogRelReturn(RT_VALID_PTR(pvService), VERR_INVALID_PARAMETER);
        SELF *pSelf = reinterpret_cast<SELF *>(pvService);
        pSelf->mpfnHostCallback = pfnExtension;
        pSelf->mpvHostData = pvExtension;
        return VINF_SUCCESS;
    }

    int setHostVersionProps();
    void incrementCounterProp(const char *pszName);
    static DECLCALLBACK(void) svcNotify(void *pvService, HGCMNOTIFYEVENT enmEvent);

    int initialize();

private:
    static DECLCALLBACK(int) reqThreadFn(RTTHREAD ThreadSelf, void *pvUser);
    uint64_t getCurrentTimestamp(void);
    int setPropertyBlock(uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
    int getProperty(uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
    int setProperty(uint32_t cParms, VBOXHGCMSVCPARM paParms[], bool isGuest);
    int setPropertyInternal(const char *pcszName, const char *pcszValue, uint32_t fFlags, uint64_t nsTimestamp,
                            bool fIsGuest = false);
    int delProperty(uint32_t cParms, VBOXHGCMSVCPARM paParms[], bool isGuest);
    int enumProps(uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
    int getNotification(uint32_t u32ClientId, VBOXHGCMCALLHANDLE callHandle, uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
    int getOldNotificationInternal(const char *pszPattern, uint64_t nsTimestamp, Property *pProp);
    int getNotificationWriteOut(uint32_t cParms, VBOXHGCMSVCPARM paParms[], Property const &prop, bool fWasDeleted);
    int doNotifications(const char *pszProperty, uint64_t nsTimestamp);
    int notifyHost(const char *pszName, const char *pszValue, uint64_t nsTimestamp, const char *pszFlags);

    void call(VBOXHGCMCALLHANDLE callHandle, uint32_t u32ClientID,
              void *pvClient, uint32_t eFunction, uint32_t cParms,
              VBOXHGCMSVCPARM paParms[]);
    int hostCall(uint32_t eFunction, uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
    int uninit();
    static DECLCALLBACK(void) dbgInfo(void *pvUser, PCDBGFINFOHLP pHlp, const char *pszArgs);

    /* Thread for handling host notifications. */
    RTTHREAD mhThreadNotifyHost;
    /* Queue for handling requests for notifications. */
    RTREQQUEUE mhReqQNotifyHost;
    static DECLCALLBACK(int) threadNotifyHost(RTTHREAD self, void *pvUser);

    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(Service);
};


/**
 * Gets the current timestamp.
 *
 * Since the RTTimeNow resolution can be very coarse, this method takes some
 * simple steps to try avoid returning the same timestamp for two consecutive
 * calls.  Code like getOldNotification() more or less assumes unique
 * timestamps.
 *
 * @returns Nanosecond timestamp.
 */
uint64_t Service::getCurrentTimestamp(void)
{
    RTTIMESPEC time;
    uint64_t u64NanoTS = RTTimeSpecGetNano(RTTimeNow(&time));
    if (mPrevTimestamp - u64NanoTS > mcTimestampAdjustments)
        mcTimestampAdjustments = 0;
    else
    {
        mcTimestampAdjustments++;
        u64NanoTS = mPrevTimestamp + 1;
    }
    this->mPrevTimestamp = u64NanoTS;
    return u64NanoTS;
}

/**
 * Set a block of properties in the property registry, checking the validity
 * of the arguments passed.
 *
 * @returns iprt status value
 * @param   cParms  the number of HGCM parameters supplied
 * @param   paParms the array of HGCM parameters
 * @thread  HGCM
 */
int Service::setPropertyBlock(uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    const char **papszNames;
    const char **papszValues;
    const char **papszFlags;
    uint64_t    *paNsTimestamps;
    uint32_t     cbDummy;
    int          rc = VINF_SUCCESS;

    /*
     * Get and validate the parameters
     */
    if (   cParms != 4
        || RT_FAILURE(HGCMSvcGetPv(&paParms[0], (void **)&papszNames, &cbDummy))
        || RT_FAILURE(HGCMSvcGetPv(&paParms[1], (void **)&papszValues, &cbDummy))
        || RT_FAILURE(HGCMSvcGetPv(&paParms[2], (void **)&paNsTimestamps, &cbDummy))
        || RT_FAILURE(HGCMSvcGetPv(&paParms[3], (void **)&papszFlags, &cbDummy))
        )
        rc = VERR_INVALID_PARAMETER;
    /** @todo validate the array sizes... */
    else
    {
        for (unsigned i = 0; RT_SUCCESS(rc) && papszNames[i] != NULL; ++i)
        {
            if (   !RT_VALID_PTR(papszNames[i])
                || !RT_VALID_PTR(papszValues[i])
                || !RT_VALID_PTR(papszFlags[i])
                )
                rc = VERR_INVALID_POINTER;
            else
            {
                uint32_t fFlagsIgn;
                rc = GuestPropValidateFlags(papszFlags[i], &fFlagsIgn);
            }
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Add the properties.  No way to roll back here.
             */
            for (unsigned i = 0; papszNames[i] != NULL; ++i)
            {
                uint32_t fFlags;
                rc = GuestPropValidateFlags(papszFlags[i], &fFlags);
                AssertRCBreak(rc);
                /*
                 * Handle names which are read-only for the guest.
                 */
                if (checkHostReserved(papszNames[i]))
                    fFlags |= GUEST_PROP_F_RDONLYGUEST;

                Property *pProp = getPropertyInternal(papszNames[i]);
                if (pProp)
                {
                    /* Update existing property. */
                    rc = pProp->mValue.assignNoThrow(papszValues[i]);
                    AssertRCBreak(rc);
                    pProp->mTimestamp = paNsTimestamps[i];
                    pProp->mFlags     = fFlags;
                }
                else
                {
                    /* Create a new property */
                    try
                    {
                        pProp = new Property(papszNames[i], papszValues[i], paNsTimestamps[i], fFlags);
                    }
                    catch (std::bad_alloc &)
                    {
                        return VERR_NO_MEMORY;
                    }
                    if (RTStrSpaceInsert(&mhProperties, &pProp->mStrCore))
                        mcProperties++;
                    else
                    {
                        delete pProp;
                        rc = VERR_INTERNAL_ERROR_3;
                        AssertFailedBreak();
                    }
                }
            }
        }
    }

    return rc;
}

/**
 * Retrieve a value from the property registry by name, checking the validity
 * of the arguments passed.  If the guest has not allocated enough buffer
 * space for the value then we return VERR_OVERFLOW and set the size of the
 * buffer needed in the "size" HGCM parameter.  If the name was not found at
 * all, we return VERR_NOT_FOUND.
 *
 * @returns iprt status value
 * @param   cParms  the number of HGCM parameters supplied
 * @param   paParms the array of HGCM parameters
 * @thread  HGCM
 */
int Service::getProperty(uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    int         rc;
    const char *pcszName = NULL;        /* shut up gcc */
    char       *pchBuf = NULL;          /* shut up MSC */
    uint32_t    cbName;
    uint32_t    cbBuf = 0;              /* shut up MSC */

    /*
     * Get and validate the parameters
     */
    LogFlowThisFunc(("\n"));
    if (   cParms != 4  /* Hardcoded value as the next lines depend on it. */
        || RT_FAILURE(HGCMSvcGetCStr(&paParms[0], &pcszName, &cbName))  /* name */
        || RT_FAILURE(HGCMSvcGetBuf(&paParms[1], (void **)&pchBuf, &cbBuf))  /* buffer */
       )
        rc = VERR_INVALID_PARAMETER;
    else
        rc = GuestPropValidateName(pcszName, cbName);
    if (RT_FAILURE(rc))
    {
        LogFlowThisFunc(("rc = %Rrc\n", rc));
        return rc;
    }

    /*
     * Read and set the values we will return
     */

    /* Get the property. */
    Property *pProp = getPropertyInternal(pcszName);
    if (pProp)
    {
        char szFlags[GUEST_PROP_MAX_FLAGS_LEN];
        rc = GuestPropWriteFlags(pProp->mFlags, szFlags);
        if (RT_SUCCESS(rc))
        {
            /* Check that the buffer is big enough */
            size_t const cbFlags  = strlen(szFlags) + 1;
            size_t const cbValue  = pProp->mValue.length() + 1;
            size_t const cbNeeded = cbValue + cbFlags;
            HGCMSvcSetU32(&paParms[3], (uint32_t)cbNeeded);
            if (cbBuf >= cbNeeded)
            {
                /* Write the value, flags and timestamp */
                memcpy(pchBuf, pProp->mValue.c_str(), cbValue);
                memcpy(pchBuf + cbValue, szFlags, cbFlags);

                HGCMSvcSetU64(&paParms[2], pProp->mTimestamp);

                /*
                 * Done!  Do exit logging and return.
                 */
                Log2(("Queried string %s, value=%s, timestamp=%lld, flags=%s\n",
                      pcszName, pProp->mValue.c_str(), pProp->mTimestamp, szFlags));
            }
            else
                rc = VERR_BUFFER_OVERFLOW;
        }
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowThisFunc(("rc = %Rrc (%s)\n", rc, pcszName));
    return rc;
}

/**
 * Set a value in the property registry by name, checking the validity
 * of the arguments passed.
 *
 * @returns iprt status value
 * @param   cParms  the number of HGCM parameters supplied
 * @param   paParms the array of HGCM parameters
 * @param   isGuest is this call coming from the guest (or the host)?
 * @throws  std::bad_alloc  if an out of memory condition occurs
 * @thread  HGCM
 */
int Service::setProperty(uint32_t cParms, VBOXHGCMSVCPARM paParms[], bool isGuest)
{
    const char *pcszName = NULL;        /* shut up gcc */
    const char *pcszValue = NULL;       /* ditto */
    const char *pcszFlags = NULL;
    uint32_t cbName = 0;                /* ditto */
    uint32_t cbValue = 0;               /* ditto */
    uint32_t cbFlags = 0;
    uint32_t fFlags = GUEST_PROP_F_NILFLAG;
    uint64_t u64TimeNano = getCurrentTimestamp();

    LogFlowThisFunc(("\n"));

    /*
     * General parameter correctness checking.
     */
    int rc = VINF_SUCCESS;
    if (   cParms < 2  /* Hardcoded value as the next lines depend on it these range checks. */
        || cParms > 3
        || RT_FAILURE(HGCMSvcGetCStr(&paParms[0], &pcszName, &cbName))  /* name */
        || RT_FAILURE(HGCMSvcGetCStr(&paParms[1], &pcszValue, &cbValue))  /* value */
        || (   cParms == 3
            && RT_FAILURE(HGCMSvcGetCStr(&paParms[2], &pcszFlags, &cbFlags)) /* flags */
           )
       )
        rc = VERR_INVALID_PARAMETER;

    /*
     * Check the values passed in the parameters for correctness.
     */
    if (RT_SUCCESS(rc))
        rc = GuestPropValidateName(pcszName, cbName);
    if (RT_SUCCESS(rc))
        rc = GuestPropValidateValue(pcszValue, cbValue);
    if (cParms == 3 && RT_SUCCESS(rc))
        rc = GuestPropValidateFlags(pcszFlags, &fFlags);
    if (RT_FAILURE(rc))
    {
        LogFlowThisFunc(("rc = %Rrc\n", rc));
        return rc;
    }

    /*
     * Hand it over to the internal setter method.
     */
    rc = setPropertyInternal(pcszName, pcszValue, fFlags, u64TimeNano, isGuest);

    LogFlowThisFunc(("%s=%s, rc=%Rrc\n", pcszName, pcszValue, rc));
    return rc;
}

/**
 * Internal property setter.
 *
 * @returns VBox status code.
 * @param   pcszName            The property name.
 * @param   pcszValue           The new value.
 * @param   fFlags              The flags.
 * @param   nsTimestamp         The timestamp.
 * @param   fIsGuest            Is it the guest calling.
 * @throws  std::bad_alloc  if an out of memory condition occurs
 * @thread  HGCM
 */
int Service::setPropertyInternal(const char *pcszName, const char *pcszValue, uint32_t fFlags, uint64_t nsTimestamp,
                                 bool fIsGuest /*= false*/)
{
    /*
     * If the property already exists, check its flags to see if we are allowed
     * to change it.
     */
    Property *pProp = getPropertyInternal(pcszName);
    int rc = checkPermission(pProp ? pProp->mFlags : GUEST_PROP_F_NILFLAG, fIsGuest);
    /*
     * Handle names which are read-only for the guest.
     */
    if (rc == VINF_SUCCESS && checkHostReserved(pcszName))
    {
        if (fIsGuest)
            rc = VERR_PERMISSION_DENIED;
        else
            fFlags |= GUEST_PROP_F_RDONLYGUEST;
    }
    if (rc == VINF_SUCCESS)
    {
        /*
         * Set the actual value
         */
        if (pProp)
        {
            rc = pProp->mValue.assignNoThrow(pcszValue);
            if (RT_SUCCESS(rc))
            {
                pProp->mTimestamp = nsTimestamp;
                pProp->mFlags = fFlags;
            }
        }
        else if (mcProperties < GUEST_PROP_MAX_PROPS)
        {
            try
            {
                /* Create a new string space record. */
                pProp = new Property(pcszName, pcszValue, nsTimestamp, fFlags);
                AssertPtr(pProp);

                if (RTStrSpaceInsert(&mhProperties, &pProp->mStrCore))
                    mcProperties++;
                else
                {
                    AssertFailed();
                    delete pProp;

                    rc = VERR_ALREADY_EXISTS;
                }
            }
            catch (std::bad_alloc &)
            {
                rc = VERR_NO_MEMORY;
            }
        }
        else
            rc = VERR_TOO_MUCH_DATA;

        /*
         * Send a notification to the guest and host and return.
         */
        // if (fIsGuest) /* Notify the host even for properties that the host
        //                * changed.  Less efficient, but ensures consistency. */
        int rc2 = doNotifications(pcszName, nsTimestamp);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    LogFlowThisFunc(("%s=%s, rc=%Rrc\n", pcszName, pcszValue, rc));
    return rc;
}


/**
 * Remove a value in the property registry by name, checking the validity
 * of the arguments passed.
 *
 * @returns iprt status value
 * @param   cParms  the number of HGCM parameters supplied
 * @param   paParms the array of HGCM parameters
 * @param   isGuest is this call coming from the guest (or the host)?
 * @thread  HGCM
 */
int Service::delProperty(uint32_t cParms, VBOXHGCMSVCPARM paParms[], bool isGuest)
{
    int         rc;
    const char *pcszName = NULL;        /* shut up gcc */
    uint32_t    cbName;

    LogFlowThisFunc(("\n"));

    /*
     * Check the user-supplied parameters.
     */
    if (   (cParms == 1)  /* Hardcoded value as the next lines depend on it. */
        && RT_SUCCESS(HGCMSvcGetCStr(&paParms[0], &pcszName, &cbName))  /* name */
       )
        rc = GuestPropValidateName(pcszName, cbName);
    else
        rc = VERR_INVALID_PARAMETER;
    if (RT_FAILURE(rc))
    {
        LogFlowThisFunc(("rc=%Rrc\n", rc));
        return rc;
    }

    /*
     * If the property exists, check its flags to see if we are allowed
     * to change it.
     */
    Property *pProp = getPropertyInternal(pcszName);
    if (pProp)
        rc = checkPermission(pProp->mFlags, isGuest);

    /*
     * And delete the property if all is well.
     */
    if (rc == VINF_SUCCESS && pProp)
    {
        uint64_t nsTimestamp = getCurrentTimestamp();
        PRTSTRSPACECORE pStrCore = RTStrSpaceRemove(&mhProperties, pProp->mStrCore.pszString);
        AssertPtr(pStrCore); NOREF(pStrCore);
        mcProperties--;
        delete pProp;
        // if (isGuest)  /* Notify the host even for properties that the host
        //                * changed.  Less efficient, but ensures consistency. */
        int rc2 = doNotifications(pcszName, nsTimestamp);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    LogFlowThisFunc(("%s: rc=%Rrc\n", pcszName, rc));
    return rc;
}

/**
 * Enumeration data shared between enumPropsCallback and Service::enumProps.
 */
typedef struct ENUMDATA
{
    const char *pszPattern; /**< The pattern to match properties against. */
    char       *pchCur;     /**< The current buffer postion. */
    size_t      cbLeft;     /**< The amount of available buffer space. */
    size_t      cbNeeded;   /**< The amount of needed buffer space. */
} ENUMDATA;

/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK}
 */
static DECLCALLBACK(int) enumPropsCallback(PRTSTRSPACECORE pStr, void *pvUser)
{
    Property *pProp = (Property *)pStr;
    ENUMDATA *pEnum = (ENUMDATA *)pvUser;

    /* Included in the enumeration? */
    if (!pProp->Matches(pEnum->pszPattern))
        return 0;

    /* Convert the non-string members into strings. */
    char            szTimestamp[256];
    size_t const    cbTimestamp = RTStrFormatNumber(szTimestamp, pProp->mTimestamp, 10, 0, 0, 0) + 1;

    char            szFlags[GUEST_PROP_MAX_FLAGS_LEN];
    int rc = GuestPropWriteFlags(pProp->mFlags, szFlags);
    if (RT_FAILURE(rc))
        return rc;
    size_t const    cbFlags = strlen(szFlags) + 1;

    /* Calculate the buffer space requirements. */
    size_t const    cbName     = pProp->mName.length() + 1;
    size_t const    cbValue    = pProp->mValue.length() + 1;
    size_t const    cbRequired = cbName + cbValue + cbTimestamp + cbFlags;
    pEnum->cbNeeded += cbRequired;

    /* Sufficient buffer space? */
    if (cbRequired > pEnum->cbLeft)
    {
        pEnum->cbLeft = 0;
        return 0; /* don't quit */
    }
    pEnum->cbLeft -= cbRequired;

    /* Append the property to the buffer. */
    char *pchCur = pEnum->pchCur;
    pEnum->pchCur += cbRequired;

    memcpy(pchCur, pProp->mName.c_str(), cbName);
    pchCur += cbName;

    memcpy(pchCur, pProp->mValue.c_str(), cbValue);
    pchCur += cbValue;

    memcpy(pchCur, szTimestamp, cbTimestamp);
    pchCur += cbTimestamp;

    memcpy(pchCur, szFlags, cbFlags);
    pchCur += cbFlags;

    Assert(pchCur == pEnum->pchCur);
    return 0;
}

/**
 * Enumerate guest properties by mask, checking the validity
 * of the arguments passed.
 *
 * @returns iprt status value
 * @param   cParms  the number of HGCM parameters supplied
 * @param   paParms the array of HGCM parameters
 * @thread  HGCM
 */
int Service::enumProps(uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    int rc = VINF_SUCCESS;

    /*
     * Get the HGCM function arguments.
     */
    char const *pchPatterns = NULL;
    char *pchBuf = NULL;
    uint32_t cbPatterns = 0;
    uint32_t cbBuf = 0;
    LogFlowThisFunc(("\n"));
    if (   (cParms != 3)  /* Hardcoded value as the next lines depend on it. */
        || RT_FAILURE(HGCMSvcGetCStr(&paParms[0], &pchPatterns, &cbPatterns))  /* patterns */
        || RT_FAILURE(HGCMSvcGetBuf(&paParms[1], (void **)&pchBuf, &cbBuf))  /* return buffer */
       )
        rc = VERR_INVALID_PARAMETER;
    if (RT_SUCCESS(rc) && cbPatterns > GUEST_PROP_MAX_PATTERN_LEN)
        rc = VERR_TOO_MUCH_DATA;

    /*
     * First repack the patterns into the format expected by RTStrSimplePatternMatch()
     */
    char szPatterns[GUEST_PROP_MAX_PATTERN_LEN];
    if (RT_SUCCESS(rc))
    {
        for (unsigned i = 0; i < cbPatterns - 1; ++i)
        {
            char ch = pchPatterns[i];
            if (pchPatterns[i] != '\0')
            { /* likely*/ }
            else
            {
                /* Since the RTStrValidateEncodingEx call in HGCMSvcGetCStr stops at the
                   first terminator, we have to validate all subsequent pattern strings. */
                rc = RTStrValidateEncodingEx(&pchPatterns[i + 1], cbPatterns - i -1, RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
                ASSERT_GUEST_RC_BREAK(rc);
                ch = '|';
            }
            szPatterns[i] = ch;
        }
        szPatterns[cbPatterns - 1] = '\0';
    }

    /*
     * Next enumerate into the buffer.
     */
    if (RT_SUCCESS(rc))
    {
        ENUMDATA EnumData;
        EnumData.pszPattern = szPatterns;
        EnumData.pchCur     = pchBuf;
        EnumData.cbLeft     = cbBuf;
        EnumData.cbNeeded   = 0;
        rc = RTStrSpaceEnumerate(&mhProperties, enumPropsCallback, &EnumData);
        AssertRCSuccess(rc);
        if (RT_SUCCESS(rc))
        {
            HGCMSvcSetU32(&paParms[2], (uint32_t)(EnumData.cbNeeded + 4));
            if (EnumData.cbLeft >= 4)
            {
                /* The final terminators. */
                EnumData.pchCur[0] = '\0';
                EnumData.pchCur[1] = '\0';
                EnumData.pchCur[2] = '\0';
                EnumData.pchCur[3] = '\0';
            }
            else
                rc = VERR_BUFFER_OVERFLOW;
        }
    }

    return rc;
}


/** Helper query used by getOldNotification
 * @throws  nothing
 */
int Service::getOldNotificationInternal(const char *pszPatterns, uint64_t nsTimestamp, Property *pProp)
{
    /* We count backwards, as the guest should normally be querying the
     * most recent events. */
    int rc = VWRN_NOT_FOUND;
    PropertyList::reverse_iterator it = mGuestNotifications.rbegin();
    for (; it != mGuestNotifications.rend(); ++it)
        if (it->mTimestamp == nsTimestamp)
        {
            rc = VINF_SUCCESS;
            break;
        }

    /* Now look for an event matching the patterns supplied.  The base()
     * member conveniently points to the following element. */
    PropertyList::iterator base = it.base();
    for (; base != mGuestNotifications.end(); ++base)
        if (base->Matches(pszPatterns))
        {
            try
            {
                *pProp = *base;
            }
            catch (std::bad_alloc &)
            {
                rc = VERR_NO_MEMORY;
            }
            return rc;
        }
    *pProp = Property();
    return rc;
}


/** Helper query used by getNotification */
int Service::getNotificationWriteOut(uint32_t cParms, VBOXHGCMSVCPARM paParms[], Property const &rProp, bool fWasDeleted)
{
    AssertReturn(cParms == 4, VERR_INVALID_PARAMETER); /* Basic sanity checking. */

    /* Format the data to write to the buffer. */
    char    *pchBuf;
    uint32_t cbBuf;
    int rc = HGCMSvcGetBuf(&paParms[2], (void **)&pchBuf, &cbBuf);
    if (RT_SUCCESS(rc))
    {
        char szFlags[GUEST_PROP_MAX_FLAGS_LEN];
        rc = GuestPropWriteFlags(rProp.mFlags, szFlags);
        if (RT_SUCCESS(rc))
        {
            HGCMSvcSetU64(&paParms[1], rProp.mTimestamp);

            size_t const cbFlags      = strlen(szFlags) + 1;
            size_t const cbName       = rProp.mName.length() + 1;
            size_t const cbValue      = rProp.mValue.length() + 1;
            size_t const cbWasDeleted = 2;
            size_t const cbNeeded     = cbName + cbValue + cbFlags + cbWasDeleted;
            HGCMSvcSetU32(&paParms[3], (uint32_t)cbNeeded);
            if (cbNeeded <= cbBuf)
            {
                /* Buffer layout: Name\0Value\0Flags\0fWasDeleted\0. */
                memcpy(pchBuf, rProp.mName.c_str(), cbName);
                pchBuf += cbName;
                memcpy(pchBuf, rProp.mValue.c_str(), cbValue);
                pchBuf += cbValue;
                memcpy(pchBuf, szFlags, cbFlags);
                pchBuf += cbFlags;
                *pchBuf++ = fWasDeleted ? '1' : '0';
                *pchBuf++ = '\0';
            }
            else
                rc = VERR_BUFFER_OVERFLOW;
        }
    }
    return rc;
}


/**
 * Get the next guest notification.
 *
 * @returns iprt status value
 * @param   u32ClientId the client ID
 * @param   callHandle  handle
 * @param   cParms      the number of HGCM parameters supplied
 * @param   paParms     the array of HGCM parameters
 * @thread  HGCM
 * @throws  nothing
 */
int Service::getNotification(uint32_t u32ClientId, VBOXHGCMCALLHANDLE callHandle,
                             uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    int rc = VINF_SUCCESS;
    char *pszPatterns = NULL;           /* shut up gcc */
    char *pchBuf;
    uint32_t cchPatterns = 0;
    uint32_t cbBuf = 0;
    uint64_t nsTimestamp;

    /*
     * Get the HGCM function arguments and perform basic verification.
     */
    LogFlowThisFunc(("\n"));
    if (   cParms != 4  /* Hardcoded value as the next lines depend on it. */
        || RT_FAILURE(HGCMSvcGetStr(&paParms[0], &pszPatterns, &cchPatterns))  /* patterns */
        || RT_FAILURE(HGCMSvcGetU64(&paParms[1], &nsTimestamp))  /* timestamp */
        || RT_FAILURE(HGCMSvcGetBuf(&paParms[2], (void **)&pchBuf, &cbBuf))  /* return buffer */
       )
        rc = VERR_INVALID_PARAMETER;
    else
    {
        LogFlow(("pszPatterns=%s, nsTimestamp=%llu\n", pszPatterns, nsTimestamp));

        /*
         * If no timestamp was supplied or no notification was found in the queue
         * of old notifications, enqueue the request in the waiting queue.
         */
        Property prop;
        if (RT_SUCCESS(rc) && nsTimestamp != 0)
            rc = getOldNotification(pszPatterns, nsTimestamp, &prop);
        if (RT_SUCCESS(rc))
        {
            if (prop.isNull())
            {
                /*
                 * Check if the client already had the same request.
                 * Complete the old request with an error in this case.
                 * Protection against clients, which cancel and resubmits requests.
                 */
                uint32_t cPendingWaits = 0;
                CallList::iterator it = mGuestWaiters.begin();
                while (it != mGuestWaiters.end())
                {
                    if (u32ClientId == it->u32ClientId)
                    {
                        const char *pszPatternsExisting;
                        uint32_t    cchPatternsExisting;
                        int rc3 = HGCMSvcGetCStr(&it->mParms[0], &pszPatternsExisting, &cchPatternsExisting);
                        if (   RT_SUCCESS(rc3)
                            && RTStrCmp(pszPatterns, pszPatternsExisting) == 0)
                        {
                            /* Complete the old request. */
                            mpHelpers->pfnCallComplete(it->mHandle, VERR_INTERRUPTED);
                            it = mGuestWaiters.erase(it);
                        }
                        else if (mpHelpers->pfnIsCallCancelled(it->mHandle))
                        {
                            /* Cleanup cancelled request. */
                            mpHelpers->pfnCallComplete(it->mHandle, VERR_INTERRUPTED);
                            it = mGuestWaiters.erase(it);
                        }
                        else
                        {
                            /** @todo check if cancelled. */
                            cPendingWaits++;
                            ++it;
                        }
                    }
                    else
                        ++it;
                }

                if (cPendingWaits < GUEST_PROP_MAX_GUEST_CONCURRENT_WAITS)
                {
                    try
                    {
                        mGuestWaiters.push_back(GuestCall(u32ClientId, callHandle, GUEST_PROP_FN_GET_NOTIFICATION,
                                                          cParms, paParms, rc));
                        rc = VINF_HGCM_ASYNC_EXECUTE;
                    }
                    catch (std::bad_alloc &)
                    {
                        rc = VERR_NO_MEMORY;
                    }
                }
                else
                {
                    LogFunc(("Too many pending waits already!\n"));
                    rc = VERR_OUT_OF_RESOURCES;
                }
            }
            /*
             * Otherwise reply at once with the enqueued notification we found.
             */
            else
            {
                int rc2 = getNotificationWriteOut(cParms, paParms, prop, !getPropertyInternal(prop.mName.c_str()));
                if (RT_FAILURE(rc2))
                    rc = rc2;
            }
        }
    }

    LogFlowThisFunc(("returning rc=%Rrc\n", rc));
    return rc;
}


/**
 * Notify the service owner and the guest that a property has been
 * added/deleted/changed
 *
 * @param   pszProperty The name of the property which has changed.
 * @param   nsTimestamp The time at which the change took place.
 * @throws  nothing.
 * @thread  HGCM service
 */
int Service::doNotifications(const char *pszProperty, uint64_t nsTimestamp)
{
    AssertPtrReturn(pszProperty, VERR_INVALID_POINTER);
    LogFlowThisFunc(("pszProperty=%s, nsTimestamp=%llu\n", pszProperty, nsTimestamp));
    /* Ensure that our timestamp is different to the last one. */
    if (   !mGuestNotifications.empty()
        && nsTimestamp == mGuestNotifications.back().mTimestamp)
        ++nsTimestamp;

    /*
     * Don't keep too many changes around.
     */
    if (mGuestNotifications.size() >= GUEST_PROP_MAX_GUEST_NOTIFICATIONS)
        mGuestNotifications.pop_front();

    /*
     * Try to find the property.  Create a change event if we find it and a
     * delete event if we do not.
     */
    Property prop;
    int rc = prop.mName.assignNoThrow(pszProperty);
    AssertRCReturn(rc, rc);
    prop.mTimestamp = nsTimestamp;
    /* prop is currently a delete event for pszProperty */
    Property const * const pProp = getPropertyInternal(pszProperty);
    if (pProp)
    {
        /* Make prop into a change event. */
        rc = prop.mValue.assignNoThrow(pProp->mValue);
        AssertRCReturn(rc, rc);
        prop.mFlags = pProp->mFlags;
    }

    /* Release guest waiters if applicable and add the event
     * to the queue for guest notifications */
    CallList::iterator it = mGuestWaiters.begin();
    while (it != mGuestWaiters.end())
    {
        const char *pszPatterns = NULL;
        uint32_t    cchPatterns;
        int rc2;

        rc2 = HGCMSvcGetCStr(&it->mParms[0], &pszPatterns, &cchPatterns);
        if (RT_FAILURE(rc2))
        {
            LogRel(("doNotifications: failed to get match pattern for guest property notification request, rc=%Rrc\n", rc2));
            mpHelpers->pfnCallComplete(it->mHandle, VERR_INVALID_PARAMETER);
            it = mGuestWaiters.erase(it);
        }
        else if (prop.Matches(pszPatterns))
        {
            rc2 = getNotificationWriteOut(it->mParmsCnt, it->mParms, prop, !pProp);
            if (RT_SUCCESS(rc2))
                rc2 = it->mRc;
            mpHelpers->pfnCallComplete(it->mHandle, rc2);
            it = mGuestWaiters.erase(it);
        }
        else
            ++it;
    }

    try
    {
        mGuestNotifications.push_back(prop);
    }
    catch (std::bad_alloc &)
    {
        rc = VERR_NO_MEMORY;
    }

    if (   RT_SUCCESS(rc)
        && mpfnHostCallback)
    {
        /*
         * Host notifications - first case: if the property exists then send its
         * current value
         */
        if (pProp)
        {
            char szFlags[GUEST_PROP_MAX_FLAGS_LEN];
            /* Send out a host notification */
            const char *pszValue = prop.mValue.c_str();
            rc = GuestPropWriteFlags(prop.mFlags, szFlags);
            if (RT_SUCCESS(rc))
                rc = notifyHost(pszProperty, pszValue, nsTimestamp, szFlags);
        }
        /*
         * Host notifications - second case: if the property does not exist then
         * send the host an empty value
         */
        else
        {
            /* Send out a host notification */
            rc = notifyHost(pszProperty, NULL, nsTimestamp, "");
        }
    }

    LogFlowThisFunc(("returning rc=%Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(void)
notifyHostAsyncWorker(PFNHGCMSVCEXT pfnHostCallback, void *pvHostData, PGUESTPROPHOSTCALLBACKDATA pHostCallbackData)
{
    pfnHostCallback(pvHostData, 0 /*u32Function*/, (void *)pHostCallbackData, sizeof(GUESTPROPHOSTCALLBACKDATA));
    RTMemFree(pHostCallbackData);
}

/**
 * Notify the service owner that a property has been added/deleted/changed.
 * @returns  IPRT status value
 * @param    pszName       the property name
 * @param    pszValue      the new value, or NULL if the property was deleted
 * @param    nsTimestamp   the time of the change
 * @param    pszFlags      the new flags string
 */
int Service::notifyHost(const char *pszName, const char *pszValue, uint64_t nsTimestamp, const char *pszFlags)
{
    LogFlowFunc(("pszName=%s, pszValue=%s, nsTimestamp=%llu, pszFlags=%s\n", pszName, pszValue, nsTimestamp, pszFlags));
    int rc;

    /* Allocate buffer for the callback data and strings. */
    size_t cbName = pszName? strlen(pszName): 0;
    size_t cbValue = pszValue? strlen(pszValue): 0;
    size_t cbFlags = pszFlags? strlen(pszFlags): 0;
    size_t cbAlloc = sizeof(GUESTPROPHOSTCALLBACKDATA) + cbName + cbValue + cbFlags + 3;
    PGUESTPROPHOSTCALLBACKDATA pHostCallbackData = (PGUESTPROPHOSTCALLBACKDATA)RTMemAlloc(cbAlloc);
    if (pHostCallbackData)
    {
        uint8_t *pu8 = (uint8_t *)pHostCallbackData;
        pu8 += sizeof(GUESTPROPHOSTCALLBACKDATA);

        pHostCallbackData->u32Magic     = GUESTPROPHOSTCALLBACKDATA_MAGIC;

        pHostCallbackData->pcszName     = (const char *)pu8;
        memcpy(pu8, pszName, cbName);
        pu8 += cbName;
        *pu8++ = 0;

        /* NULL value means property was deleted. */
        pHostCallbackData->pcszValue    = pszValue ? (const char *)pu8 : NULL;
        memcpy(pu8, pszValue, cbValue);
        pu8 += cbValue;
        *pu8++ = 0;

        pHostCallbackData->u64Timestamp = nsTimestamp;

        pHostCallbackData->pcszFlags    = (const char *)pu8;
        memcpy(pu8, pszFlags, cbFlags);
        pu8 += cbFlags;
        *pu8++ = 0;

        rc = RTReqQueueCallEx(mhReqQNotifyHost, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              (PFNRT)notifyHostAsyncWorker, 3, mpfnHostCallback, mpvHostData, pHostCallbackData);
        if (RT_FAILURE(rc))
        {
            RTMemFree(pHostCallbackData);
        }
    }
    else
    {
        rc = VERR_NO_MEMORY;
    }
    LogFlowFunc(("returning rc=%Rrc\n", rc));
    return rc;
}


/**
 * Handle an HGCM service call.
 * @interface_method_impl{VBOXHGCMSVCFNTABLE,pfnCall}
 * @note    All functions which do not involve an unreasonable delay will be
 *          handled synchronously.  If needed, we will add a request handler
 *          thread in future for those which do.
 *
 * @thread  HGCM
 */
void Service::call (VBOXHGCMCALLHANDLE callHandle, uint32_t u32ClientID,
                    void * /* pvClient */, uint32_t eFunction, uint32_t cParms,
                    VBOXHGCMSVCPARM paParms[])
{
    int rc;
    LogFlowFunc(("u32ClientID = %d, fn = %d, cParms = %d, pparms = %p\n",
                 u32ClientID, eFunction, cParms, paParms));

    switch (eFunction)
    {
        /* The guest wishes to read a property */
        case GUEST_PROP_FN_GET_PROP:
            LogFlowFunc(("GET_PROP\n"));
            rc = getProperty(cParms, paParms);
            break;

        /* The guest wishes to set a property */
        case GUEST_PROP_FN_SET_PROP:
            LogFlowFunc(("SET_PROP\n"));
            rc = setProperty(cParms, paParms, true);
            break;

        /* The guest wishes to set a property value */
        case GUEST_PROP_FN_SET_PROP_VALUE:
            LogFlowFunc(("SET_PROP_VALUE\n"));
            rc = setProperty(cParms, paParms, true);
            break;

        /* The guest wishes to remove a configuration value */
        case GUEST_PROP_FN_DEL_PROP:
            LogFlowFunc(("DEL_PROP\n"));
            rc = delProperty(cParms, paParms, true);
            break;

        /* The guest wishes to enumerate all properties */
        case GUEST_PROP_FN_ENUM_PROPS:
            LogFlowFunc(("ENUM_PROPS\n"));
            rc = enumProps(cParms, paParms);
            break;

        /* The guest wishes to get the next property notification */
        case GUEST_PROP_FN_GET_NOTIFICATION:
            LogFlowFunc(("GET_NOTIFICATION\n"));
            rc = getNotification(u32ClientID, callHandle, cParms, paParms);
            break;

        default:
            rc = VERR_NOT_IMPLEMENTED;
    }
    LogFlowFunc(("rc = %Rrc\n", rc));
    if (rc != VINF_HGCM_ASYNC_EXECUTE)
        mpHelpers->pfnCallComplete(callHandle, rc);
}

/**
 * Enumeration data shared between dbgInfoCallback and Service::dbgInfoShow.
 */
typedef struct ENUMDBGINFO
{
    PCDBGFINFOHLP pHlp;
} ENUMDBGINFO;

static DECLCALLBACK(int) dbgInfoCallback(PRTSTRSPACECORE pStr, void *pvUser)
{
    Property     *pProp = (Property *)pStr;
    PCDBGFINFOHLP pHlp  = ((ENUMDBGINFO *)pvUser)->pHlp;

    char szFlags[GUEST_PROP_MAX_FLAGS_LEN];
    int rc = GuestPropWriteFlags(pProp->mFlags, szFlags);
    if (RT_FAILURE(rc))
        RTStrPrintf(szFlags, sizeof(szFlags), "???");

    pHlp->pfnPrintf(pHlp, "%s: '%s', %RU64", pProp->mName.c_str(), pProp->mValue.c_str(), pProp->mTimestamp);
    if (strlen(szFlags))
        pHlp->pfnPrintf(pHlp, " (%s)", szFlags);
    pHlp->pfnPrintf(pHlp, "\n");
    return 0;
}


/**
 * Handler for debug info.
 *
 * @param   pvUser      user pointer.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
DECLCALLBACK(void) Service::dbgInfo(void *pvUser, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF1(pszArgs);
    SELF *pSelf = reinterpret_cast<SELF *>(pvUser);

    ENUMDBGINFO EnumData = { pHlp };
    RTStrSpaceEnumerate(&pSelf->mhProperties, dbgInfoCallback, &EnumData);
}


/**
 * Service call handler for the host.
 * @interface_method_impl{VBOXHGCMSVCFNTABLE,pfnHostCall}
 * @thread  hgcm
 */
int Service::hostCall (uint32_t eFunction, uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    int rc;
    LogFlowFunc(("fn = %d, cParms = %d, pparms = %p\n", eFunction, cParms, paParms));

    switch (eFunction)
    {
        /* The host wishes to set a block of properties */
        case GUEST_PROP_FN_HOST_SET_PROPS:
            LogFlowFunc(("SET_PROPS_HOST\n"));
            rc = setPropertyBlock(cParms, paParms);
            break;

        /* The host wishes to read a configuration value */
        case GUEST_PROP_FN_HOST_GET_PROP:
            LogFlowFunc(("GET_PROP_HOST\n"));
            rc = getProperty(cParms, paParms);
            break;

        /* The host wishes to set a configuration value */
        case GUEST_PROP_FN_HOST_SET_PROP:
            LogFlowFunc(("SET_PROP_HOST\n"));
            rc = setProperty(cParms, paParms, false);
            break;

        /* The host wishes to set a configuration value */
        case GUEST_PROP_FN_HOST_SET_PROP_VALUE:
            LogFlowFunc(("SET_PROP_VALUE_HOST\n"));
            rc = setProperty(cParms, paParms, false);
            break;

        /* The host wishes to remove a configuration value */
        case GUEST_PROP_FN_HOST_DEL_PROP:
            LogFlowFunc(("DEL_PROP_HOST\n"));
            rc = delProperty(cParms, paParms, false);
            break;

        /* The host wishes to enumerate all properties */
        case GUEST_PROP_FN_HOST_ENUM_PROPS:
            LogFlowFunc(("ENUM_PROPS\n"));
            rc = enumProps(cParms, paParms);
            break;

        /* The host wishes to set global flags for the service */
        case GUEST_PROP_FN_HOST_SET_GLOBAL_FLAGS:
            LogFlowFunc(("SET_GLOBAL_FLAGS_HOST\n"));
            if (cParms == 1)
            {
                uint32_t fFlags;
                rc = HGCMSvcGetU32(&paParms[0], &fFlags);
                if (RT_SUCCESS(rc))
                    mfGlobalFlags = fFlags;
            }
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        default:
            rc = VERR_NOT_SUPPORTED;
            break;
    }

    LogFlowFunc(("rc = %Rrc\n", rc));
    return rc;
}

/**
 * @interface_method_impl{VBOXHGCMSVCFNTABLE,pfnDisconnect}
 */
/*static*/ DECLCALLBACK(int) Service::svcDisconnect(void *pvService, uint32_t idClient, void *pvClient)
{
    RT_NOREF(pvClient);
    LogFlowFunc(("idClient=%u\n", idClient));
    SELF *pThis = reinterpret_cast<SELF *>(pvService);
    AssertLogRelReturn(pThis, VERR_INVALID_POINTER);

    /*
     * Complete all pending requests for this client.
     */
    for (CallList::iterator It = pThis->mGuestWaiters.begin(); It != pThis->mGuestWaiters.end();)
    {
        GuestCall &rCurCall = *It;
        if (rCurCall.u32ClientId != idClient)
            ++It;
        else
        {
            LogFlowFunc(("Completing call %u (%p)...\n", rCurCall.mFunction, rCurCall.mHandle));
            pThis->mpHelpers->pfnCallComplete(rCurCall.mHandle, VERR_INTERRUPTED);
            It = pThis->mGuestWaiters.erase(It);
        }
    }

    return VINF_SUCCESS;
}

/**
 * Increments a counter property.
 *
 * It is assumed that this a transient property that is read-only to the guest.
 *
 * @param   pszName     The property name.
 * @throws  std::bad_alloc  if an out of memory condition occurs
 */
void Service::incrementCounterProp(const char *pszName)
{
    /* Format the incremented value. */
    char szValue[64];
    Property *pProp = getPropertyInternal(pszName);
    if (pProp)
    {
        uint64_t uValue = RTStrToUInt64(pProp->mValue.c_str());
        RTStrFormatU64(szValue, sizeof(szValue), uValue + 1, 10, 0, 0, 0);
    }
    else
    {
        szValue[0] = '1';
        szValue[1] = '\0';
    }

    /* Set it. */
    setPropertyInternal(pszName, szValue, GUEST_PROP_F_TRANSIENT | GUEST_PROP_F_RDONLYGUEST, getCurrentTimestamp());
}

/**
 * Sets the VBoxVer, VBoxVerExt and VBoxRev properties.
 */
int Service::setHostVersionProps()
{
    uint64_t nsTimestamp = getCurrentTimestamp();

    /* Set the raw VBox version string as a guest property. Used for host/guest
     * version comparison. */
    int rc = setPropertyInternal("/VirtualBox/HostInfo/VBoxVer", VBOX_VERSION_STRING_RAW,
                                 GUEST_PROP_F_TRANSIENT | GUEST_PROP_F_RDONLYGUEST, nsTimestamp);
    AssertRCReturn(rc, rc);

    /* Set the full VBox version string as a guest property. Can contain vendor-specific
     * information/branding and/or pre-release tags. */
    rc = setPropertyInternal("/VirtualBox/HostInfo/VBoxVerExt", VBOX_VERSION_STRING,
                             GUEST_PROP_F_TRANSIENT | GUEST_PROP_F_RDONLYGUEST, nsTimestamp + 1);
    AssertRCReturn(rc, rc);

    /* Set the VBox SVN revision as a guest property */
    rc = setPropertyInternal("/VirtualBox/HostInfo/VBoxRev", RTBldCfgRevisionStr(),
                             GUEST_PROP_F_TRANSIENT | GUEST_PROP_F_RDONLYGUEST, nsTimestamp + 2);
    AssertRCReturn(rc, rc);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{VBOXHGCMSVCFNTABLE,pfnNotify}
 */
/*static*/ DECLCALLBACK(void) Service::svcNotify(void *pvService, HGCMNOTIFYEVENT enmEvent)
{
    SELF *pThis = reinterpret_cast<SELF *>(pvService);
    AssertPtrReturnVoid(pThis);

    /* Make sure the host version properties have been touched and are
       up-to-date after a restore: */
    if (   !pThis->m_fSetHostVersionProps
        && (enmEvent == HGCMNOTIFYEVENT_RESUME || enmEvent == HGCMNOTIFYEVENT_POWER_ON))
    {
        pThis->setHostVersionProps();
        pThis->m_fSetHostVersionProps = true;
    }

    if (enmEvent == HGCMNOTIFYEVENT_RESUME)
        pThis->incrementCounterProp("/VirtualBox/VMInfo/ResumeCounter");

    if (enmEvent == HGCMNOTIFYEVENT_RESET)
        pThis->incrementCounterProp("/VirtualBox/VMInfo/ResetCounter");
}


/* static */
DECLCALLBACK(int) Service::threadNotifyHost(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF1(hThreadSelf);
    Service *pThis = (Service *)pvUser;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("ENTER: %p\n", pThis));

    for (;;)
    {
        rc = RTReqQueueProcess(pThis->mhReqQNotifyHost, RT_INDEFINITE_WAIT);

        AssertMsg(rc == VWRN_STATE_CHANGED,
                  ("Left RTReqProcess and error code is not VWRN_STATE_CHANGED rc=%Rrc\n",
                   rc));
        if (rc == VWRN_STATE_CHANGED)
        {
            break;
        }
    }

    LogFlowFunc(("LEAVE: %Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(int) wakeupNotifyHost(void)
{
    /* Returning a VWRN_* will cause RTReqQueueProcess return. */
    return VWRN_STATE_CHANGED;
}


int Service::initialize()
{
    /*
     * Insert standard host properties.
     */
    /* The host version will but updated again on power on or resume
       (after restore), however we need the properties now for restored
       guest notification/wait calls. */
    int rc = setHostVersionProps();
    AssertRCReturn(rc, rc);

    uint64_t nsNow = getCurrentTimestamp(); /* Must increment this for each property to avoid asserting in getOldNotification. */

    /* Resume and reset counters. */
    rc = setPropertyInternal("/VirtualBox/VMInfo/ResetCounter",  "0", GUEST_PROP_F_TRANSIENT | GUEST_PROP_F_RDONLYGUEST, nsNow);
    AssertRCReturn(rc, rc);
    rc = setPropertyInternal("/VirtualBox/VMInfo/ResumeCounter", "0", GUEST_PROP_F_TRANSIENT | GUEST_PROP_F_RDONLYGUEST, ++nsNow);
    AssertRCReturn(rc, rc);

    /* Sysprep execution by VBoxService (host is allowed to change these). */
    rc = setPropertyInternal("/VirtualBox/HostGuest/SysprepExec", "", GUEST_PROP_F_TRANSIENT | GUEST_PROP_F_RDONLYGUEST, ++nsNow);
    AssertRCReturn(rc, rc);
    rc = setPropertyInternal("/VirtualBox/HostGuest/SysprepArgs", "", GUEST_PROP_F_TRANSIENT | GUEST_PROP_F_RDONLYGUEST, ++nsNow);
    AssertRCReturn(rc, rc);


    /* The host notification thread and queue. */
    rc = RTReqQueueCreate(&mhReqQNotifyHost);
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadCreate(&mhThreadNotifyHost,
                            threadNotifyHost,
                            this,
                            0 /* default stack size */,
                            RTTHREADTYPE_DEFAULT,
                            RTTHREADFLAGS_WAITABLE,
                            "GstPropNtfy");
        if (RT_SUCCESS(rc))
        {
            /* Finally debug stuff (ignore failures): */
            HGCMSvcHlpInfoRegister(mpHelpers, "guestprops", "Display the guest properties", Service::dbgInfo, this);
            return rc;
        }

        RTReqQueueDestroy(mhReqQNotifyHost);
        mhReqQNotifyHost = NIL_RTREQQUEUE;
    }
    return rc;
}

/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Destroys Property.}
 */
static DECLCALLBACK(int) destroyProperty(PRTSTRSPACECORE pStr, void *pvUser)
{
    RT_NOREF(pvUser);
    Property *pProp = RT_FROM_CPP_MEMBER(pStr, struct Property, mStrCore); /* clang objects to offsetof on non-POD.*/
    delete pProp;
    return 0;
}


int Service::uninit()
{
    if (mpHelpers)
        HGCMSvcHlpInfoDeregister(mpHelpers, "guestprops");

    if (mhReqQNotifyHost != NIL_RTREQQUEUE)
    {
        /* Stop the thread */
        PRTREQ pReq;
        int rc = RTReqQueueCall(mhReqQNotifyHost, &pReq, 10000, (PFNRT)wakeupNotifyHost, 0);
        if (RT_SUCCESS(rc))
            RTReqRelease(pReq);
        rc = RTThreadWait(mhThreadNotifyHost, 10000, NULL);
        AssertRC(rc);
        rc = RTReqQueueDestroy(mhReqQNotifyHost);
        AssertRC(rc);
        mhReqQNotifyHost = NIL_RTREQQUEUE;
        mhThreadNotifyHost = NIL_RTTHREAD;
        RTStrSpaceDestroy(&mhProperties, destroyProperty, NULL);
        mhProperties = NULL;
    }
    return VINF_SUCCESS;
}

} /* namespace guestProp */

using guestProp::Service;

/**
 * @copydoc FNVBOXHGCMSVCLOAD
 */
extern "C" DECLCALLBACK(DECLEXPORT(int)) VBoxHGCMSvcLoad(VBOXHGCMSVCFNTABLE *ptable)
{
    int rc = VERR_IPE_UNINITIALIZED_STATUS;

    LogFlowFunc(("ptable = %p\n", ptable));

    if (!RT_VALID_PTR(ptable))
        rc = VERR_INVALID_PARAMETER;
    else
    {
        LogFlowFunc(("ptable->cbSize = %d, ptable->u32Version = 0x%08X\n", ptable->cbSize, ptable->u32Version));

        if (   ptable->cbSize != sizeof(VBOXHGCMSVCFNTABLE)
            || ptable->u32Version != VBOX_HGCM_SVC_VERSION)
            rc = VERR_VERSION_MISMATCH;
        else
        {
            Service *pService = NULL;
            /* No exceptions may propagate outside. */
            try
            {
                pService = new Service(ptable->pHelpers);
                rc = VINF_SUCCESS;
            }
            catch (int rcThrown)
            {
                rc = rcThrown;
            }
            catch (...)
            {
                rc = VERR_UNEXPECTED_EXCEPTION;
            }

            if (RT_SUCCESS(rc))
            {
                /* We do not maintain connections, so no client data is needed. */
                ptable->cbClient = 0;

                /* Legacy clients map to the kernel category. */
                ptable->idxLegacyClientCategory = HGCM_CLIENT_CATEGORY_KERNEL;

                /* Go with default client limits, but we won't ever need more than
                   16 pending calls per client I would think (1 should be enough). */
                for (uintptr_t i = 0; i < RT_ELEMENTS(ptable->acMaxClients); i++)
                    ptable->acMaxCallsPerClient[i] = 16;

                ptable->pfnUnload             = Service::svcUnload;
                ptable->pfnConnect            = Service::svcConnect;
                ptable->pfnDisconnect         = Service::svcDisconnect;
                ptable->pfnCall               = Service::svcCall;
                ptable->pfnHostCall           = Service::svcHostCall;
                ptable->pfnSaveState          = NULL;  /* The service is stateless, so the normal */
                ptable->pfnLoadState          = NULL;  /* construction done before restoring suffices */
                ptable->pfnRegisterExtension  = Service::svcRegisterExtension;
                ptable->pfnNotify             = Service::svcNotify;
                ptable->pvService             = pService;

                /* Service specific initialization. */
                rc = pService->initialize();
                if (RT_FAILURE(rc))
                {
                    delete pService;
                    pService = NULL;
                }
            }
            else
                Assert(!pService);
        }
    }

    LogFlowFunc(("returning %Rrc\n", rc));
    return rc;
}

