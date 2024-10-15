/* $Id: UpdateAgentImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IUpdateAgent COM class implementations.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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


#define LOG_GROUP LOG_GROUP_MAIN_UPDATEAGENT

#include <iprt/cpp/utils.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/http.h>
#include <iprt/system.h>
#include <iprt/message.h>
#include <iprt/pipe.h>
#include <iprt/env.h>
#include <iprt/process.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/stream.h>
#include <iprt/time.h>
#include <VBox/com/defs.h>
#include <VBox/err.h>
#include <VBox/version.h>

#include "HostImpl.h"
#include "UpdateAgentImpl.h"
#include "ProgressImpl.h"
#include "AutoCaller.h"
#include "LoggingNew.h"
#include "VirtualBoxImpl.h"
#include "VBoxEvents.h"
#include "SystemPropertiesImpl.h"
#include "ThreadTask.h"
#include "VirtualBoxImpl.h"
#include "VirtualBoxBase.h"


/*********************************************************************************************************************************
*   Update agent task implementation                                                                                             *
*********************************************************************************************************************************/

/**
 * Base task class for asynchronous update agent tasks.
 */
class UpdateAgentTask : public ThreadTask
{
public:
    UpdateAgentTask(UpdateAgentBase *aThat, Progress *aProgress)
        : m_pParent(aThat)
        , m_pProgress(aProgress)
    {
        m_strTaskName = "UpdateAgentTask";
    }
    virtual ~UpdateAgentTask(void) { }

private:
    void handler(void);

    /** Weak pointer to parent (update agent). */
    UpdateAgentBase     *m_pParent;
    /** Smart pointer to the progress object for this job. */
    ComObjPtr<Progress>  m_pProgress;

    friend class UpdateAgent;  // allow member functions access to private data
};

void UpdateAgentTask::handler(void)
{
    UpdateAgentBase *pUpdateAgent = this->m_pParent;
    AssertPtr(pUpdateAgent);

    /** @todo Differentiate tasks once we have more stuff to do (downloading, installing, ++). */

    HRESULT hrc = pUpdateAgent->i_checkForUpdateTask(this);

    if (!m_pProgress.isNull())
        m_pProgress->i_notifyComplete(hrc);

    LogFlowFunc(("hrc=%Rhrc\n", hrc)); RT_NOREF(hrc);
}


/*********************************************************************************************************************************
*   Update agent base class implementation                                                                                       *
*********************************************************************************************************************************/

/**
 * Returns platform information as a string.
 *
 * @returns Platform information as string.
 */
/* static */
Utf8Str UpdateAgentBase::i_getPlatformInfo(void)
{
    /* Prepare platform report: */
    Utf8Str strPlatform;

# if defined (RT_OS_WINDOWS)
    strPlatform = "win";
# elif defined (RT_OS_LINUX)
    strPlatform = "linux";
# elif defined (RT_OS_DARWIN)
    strPlatform = "macosx";
# elif defined (RT_OS_OS2)
    strPlatform = "os2";
# elif defined (RT_OS_FREEBSD)
    strPlatform = "freebsd";
# elif defined (RT_OS_SOLARIS)
    strPlatform = "solaris";
# else
    strPlatform = "unknown";
# endif

    /* The format is <system>.<bitness>: */
    strPlatform.appendPrintf(".%lu", ARCH_BITS);

    /* Add more system information: */
    int vrc;
# ifdef RT_OS_LINUX
    // WORKAROUND:
    // On Linux we try to generate information using script first of all..

    /* Get script path: */
    char szAppPrivPath[RTPATH_MAX];
    vrc = RTPathAppPrivateNoArch(szAppPrivPath, sizeof(szAppPrivPath));
    AssertRC(vrc);
    if (RT_SUCCESS(vrc))
        vrc = RTPathAppend(szAppPrivPath, sizeof(szAppPrivPath), "/VBoxSysInfo.sh");
    AssertRC(vrc);
    if (RT_SUCCESS(vrc))
    {
        RTPIPE hPipeR;
        RTHANDLE hStdOutPipe;
        hStdOutPipe.enmType = RTHANDLETYPE_PIPE;
        vrc = RTPipeCreate(&hPipeR, &hStdOutPipe.u.hPipe, RTPIPE_C_INHERIT_WRITE);
        AssertLogRelRC(vrc);

        char const *szAppPrivArgs[2];
        szAppPrivArgs[0] = szAppPrivPath;
        szAppPrivArgs[1] = NULL;
        RTPROCESS hProc = NIL_RTPROCESS;

        /* Run script: */
        vrc = RTProcCreateEx(szAppPrivPath, szAppPrivArgs, RTENV_DEFAULT, 0 /*fFlags*/, NULL /*phStdin*/, &hStdOutPipe,
                             NULL /*phStderr*/, NULL /*pszAsUser*/, NULL /*pszPassword*/, NULL /*pvExtraData*/, &hProc);

        (void) RTPipeClose(hStdOutPipe.u.hPipe);
        hStdOutPipe.u.hPipe = NIL_RTPIPE;

        if (RT_SUCCESS(vrc))
        {
            RTPROCSTATUS  ProcStatus;
            size_t        cbStdOutBuf  = 0;
            size_t        offStdOutBuf = 0;
            char          *pszStdOutBuf = NULL;
            do
            {
                if (hPipeR != NIL_RTPIPE)
                {
                    char    achBuf[1024];
                    size_t  cbRead;
                    vrc = RTPipeReadBlocking(hPipeR, achBuf, sizeof(achBuf), &cbRead);
                    if (RT_SUCCESS(vrc))
                    {
                        /* grow the buffer? */
                        size_t cbBufReq = offStdOutBuf + cbRead + 1;
                        if (   cbBufReq > cbStdOutBuf
                            && cbBufReq < _256K)
                        {
                            size_t cbNew = RT_ALIGN_Z(cbBufReq, 16); // 1024
                            void  *pvNew = RTMemRealloc(pszStdOutBuf, cbNew);
                            if (pvNew)
                            {
                                pszStdOutBuf = (char *)pvNew;
                                cbStdOutBuf  = cbNew;
                            }
                        }

                        /* append if we've got room. */
                        if (cbBufReq <= cbStdOutBuf)
                        {
                            (void) memcpy(&pszStdOutBuf[offStdOutBuf], achBuf, cbRead);
                            offStdOutBuf = offStdOutBuf + cbRead;
                            pszStdOutBuf[offStdOutBuf] = '\0';
                        }
                    }
                    else
                    {
                        AssertLogRelMsg(vrc == VERR_BROKEN_PIPE, ("%Rrc\n", vrc));
                        RTPipeClose(hPipeR);
                        hPipeR = NIL_RTPIPE;
                    }
                }

                /*
                 * Service the process.  Block if we have no pipe.
                 */
                if (hProc != NIL_RTPROCESS)
                {
                    vrc = RTProcWait(hProc,
                                     hPipeR == NIL_RTPIPE ? RTPROCWAIT_FLAGS_BLOCK : RTPROCWAIT_FLAGS_NOBLOCK,
                                     &ProcStatus);
                    if (RT_SUCCESS(vrc))
                        hProc = NIL_RTPROCESS;
                    else
                        AssertLogRelMsgStmt(vrc == VERR_PROCESS_RUNNING, ("%Rrc\n", vrc), hProc = NIL_RTPROCESS);
                }
            } while (   hPipeR != NIL_RTPIPE
                     || hProc != NIL_RTPROCESS);

            if (   ProcStatus.enmReason == RTPROCEXITREASON_NORMAL
                && ProcStatus.iStatus == 0) {
                pszStdOutBuf[offStdOutBuf-1] = '\0';  // remove trailing newline
                Utf8Str pszStdOutBufUTF8(pszStdOutBuf);
                strPlatform.appendPrintf(" [%s]", pszStdOutBufUTF8.strip().c_str());
                // For testing, here is some sample output:
                //strPlatform.appendPrintf(" [Distribution: Redhat | Version: 7.6.1810 | Kernel: Linux version 3.10.0-952.27.2.el7.x86_64 (gcc version 4.8.5 20150623 (Red Hat 4.8.5-36) (GCC) ) #1 SMP Mon Jul 29 17:46:05 UTC 2019]");
            }
        }
        else
            vrc = VERR_TRY_AGAIN; /* (take the fallback path) */
    }

    if (RT_FAILURE(vrc))
# endif /* RT_OS_LINUX */
    {
        /* Use RTSystemQueryOSInfo: */
        char szTmp[256];

        vrc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szTmp, sizeof(szTmp));
        if ((RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW) && szTmp[0] != '\0')
            strPlatform.appendPrintf(" [Product: %s", szTmp);

        vrc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szTmp, sizeof(szTmp));
        if ((RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW) && szTmp[0] != '\0')
            strPlatform.appendPrintf(" %sRelease: %s", strlen(szTmp) == 0 ? "[" : "| ", szTmp);

        vrc = RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, szTmp, sizeof(szTmp));
        if ((RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW) && szTmp[0] != '\0')
            strPlatform.appendPrintf(" %sVersion: %s", strlen(szTmp) == 0 ? "[" : "| ", szTmp);

        vrc = RTSystemQueryOSInfo(RTSYSOSINFO_SERVICE_PACK, szTmp, sizeof(szTmp));
        if ((RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW) && szTmp[0] != '\0')
            strPlatform.appendPrintf(" %sSP: %s]", strlen(szTmp) == 0 ? "[" : "| ", szTmp);

        if (!strPlatform.endsWith("]"))
            strPlatform.append("]");
    }

    LogRel2(("UpdateAgent: Platform is '%s'\n", strPlatform.c_str()));

    return strPlatform;
}

/**
 * Returns the proxy mode as a string.
 *
 * @returns Proxy mode as string.
 * @param   enmMode             Proxy mode to return as string.
 */
/* static */
const char *UpdateAgentBase::i_proxyModeToStr(ProxyMode_T enmMode)
{
    switch (enmMode)
    {
        case ProxyMode_System:  return "System";
        case ProxyMode_Manual:  return "Manual";
        case ProxyMode_NoProxy: return "None";
        default:                break;
    }

    AssertFailed();
    return "<Invalid>";
}

/**
 * Returns whether a given URL's scheme is supported or not.
 *
 * @returns \c true if scheme is supported, or \c false if not.
 * @param   strUrl              URL to check scheme for.
 *
 * @note Empty URL are considered as being supported for convenience.
 */
bool UpdateAgentBase::i_urlSchemeIsSupported(const Utf8Str &strUrl) const
{
    if (strUrl.isEmpty())
        return true;
    return strUrl.startsWith("https://", com::Utf8Str::CaseInsensitive);
}


/*********************************************************************************************************************************
*   Update agent class implementation                                                                                            *
*********************************************************************************************************************************/
UpdateAgent::UpdateAgent()
{
}

UpdateAgent::~UpdateAgent()
{
}

HRESULT UpdateAgent::FinalConstruct(void)
{
    return BaseFinalConstruct();
}

void UpdateAgent::FinalRelease(void)
{
    uninit();

    BaseFinalRelease();
}

HRESULT UpdateAgent::init(VirtualBox *aVirtualBox)
{
    /* Weak reference to a VirtualBox object */
    unconst(m_VirtualBox) = aVirtualBox;

    HRESULT hrc = unconst(m_EventSource).createObject();
    if (SUCCEEDED(hrc))
        hrc = m_EventSource->init();

    return hrc;
}

void UpdateAgent::uninit(void)
{
    // Enclose the state transition Ready->InUninit->NotReady.
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(m_EventSource).setNull();
}

HRESULT UpdateAgent::checkFor(ComPtr<IProgress> &aProgress)
{
    RT_NOREF(aProgress);

    return VBOX_E_NOT_SUPPORTED;
}

HRESULT UpdateAgent::download(ComPtr<IProgress> &aProgress)
{
    RT_NOREF(aProgress);

    return VBOX_E_NOT_SUPPORTED;
}

HRESULT UpdateAgent::install(ComPtr<IProgress> &aProgress)
{
    RT_NOREF(aProgress);

    return VBOX_E_NOT_SUPPORTED;
}

HRESULT UpdateAgent::rollback(void)
{
    return VBOX_E_NOT_SUPPORTED;
}

HRESULT UpdateAgent::getName(com::Utf8Str &aName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aName = mData.m_strName;

    return S_OK;
}

HRESULT UpdateAgent::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    LogFlowThisFuncEnter();

    /* No need to lock - lifetime constant. */
    m_EventSource.queryInterfaceTo(aEventSource.asOutParam());

    LogFlowFuncLeaveRC(S_OK);
    return S_OK;
}

HRESULT UpdateAgent::getOrder(ULONG *aOrder)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aOrder = 0; /* 0 means no order / disabled. */

    return S_OK;
}

HRESULT UpdateAgent::getDependsOn(std::vector<com::Utf8Str> &aDeps)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aDeps.resize(0); /* No dependencies by default. */

    return S_OK;
}

HRESULT UpdateAgent::getVersion(com::Utf8Str &aVer)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aVer = mData.m_lastResult.strVer;

    return S_OK;
}

HRESULT UpdateAgent::getDownloadUrl(com::Utf8Str &aUrl)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aUrl = mData.m_lastResult.strDownloadUrl;

    return S_OK;
}


HRESULT UpdateAgent::getWebUrl(com::Utf8Str &aUrl)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aUrl = mData.m_lastResult.strWebUrl;

    return S_OK;
}

HRESULT UpdateAgent::getReleaseNotes(com::Utf8Str &aRelNotes)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aRelNotes = mData.m_lastResult.strReleaseNotes;

    return S_OK;
}

HRESULT UpdateAgent::getEnabled(BOOL *aEnabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aEnabled = m->fEnabled;

    return S_OK;
}

HRESULT UpdateAgent::setEnabled(const BOOL aEnabled)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->fEnabled = aEnabled;

    return i_commitSettings(alock);
}


HRESULT UpdateAgent::getHidden(BOOL *aHidden)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aHidden = mData.m_fHidden;

    return S_OK;
}

HRESULT UpdateAgent::getState(UpdateState_T *aState)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aState = mData.m_enmState;

    return S_OK;
}

HRESULT UpdateAgent::getCheckFrequency(ULONG *aFreqSeconds)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aFreqSeconds = m->uCheckFreqSeconds;

    return S_OK;
}

HRESULT UpdateAgent::setCheckFrequency(ULONG aFreqSeconds)
{
    if (aFreqSeconds < RT_SEC_1DAY) /* Don't allow more frequent checks for now. */
        return setError(E_INVALIDARG, tr("Frequency too small; one day is the minimum"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->uCheckFreqSeconds = aFreqSeconds;

    return i_commitSettings(alock);
}

HRESULT UpdateAgent::getChannel(UpdateChannel_T *aChannel)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aChannel = m->enmChannel;

    return S_OK;
}

HRESULT UpdateAgent::setChannel(UpdateChannel_T aChannel)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->enmChannel = aChannel;

    return i_commitSettings(alock);
}

HRESULT UpdateAgent::getCheckCount(ULONG *aCount)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aCount = m->uCheckCount;

    return S_OK;
}

HRESULT UpdateAgent::getRepositoryURL(com::Utf8Str &aRepo)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aRepo = m->strRepoUrl;

    return S_OK;
}

HRESULT UpdateAgent::setRepositoryURL(const com::Utf8Str &aRepo)
{
    if (!i_urlSchemeIsSupported(aRepo))
        return setError(E_INVALIDARG, tr("Invalid URL scheme specified!"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->strRepoUrl = aRepo;

    return i_commitSettings(alock);
}

HRESULT UpdateAgent::getLastCheckDate(com::Utf8Str &aDate)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aDate = m->strLastCheckDate;

    return S_OK;
}

HRESULT UpdateAgent::getIsCheckNeeded(BOOL *aCheckNeeded)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Is update checking enabled at all?
     */
    if (!m->fEnabled)
    {
        *aCheckNeeded = FALSE;
        return S_OK;
    }

    /*
     * When was the last update?
     */
    if (m->strLastCheckDate.isEmpty()) /* No prior update check performed -- do so now. */
    {
        *aCheckNeeded = TRUE;
        return S_OK;
    }

    RTTIMESPEC LastCheckTime;
    if (!RTTimeSpecFromString(&LastCheckTime, Utf8Str(m->strLastCheckDate).c_str()))
    {
        *aCheckNeeded = TRUE; /* Invalid date set or error? Perform check. */
        return S_OK;
    }

    /*
     * Compare last update with how often we are supposed to check for updates.
     */
    if (   !m->uCheckFreqSeconds                /* Paranoia */
        ||  m->uCheckFreqSeconds < RT_SEC_1DAY) /* This is the minimum we currently allow. */
    {
        /* Consider config (enable, 0 day interval) as checking once but never again.
           We've already check since we've got a date. */
        *aCheckNeeded = FALSE;
        return S_OK;
    }

    uint64_t const cCheckFreqDays = m->uCheckFreqSeconds / RT_SEC_1DAY_64;

    RTTIMESPEC TimeDiff;
    RTTimeSpecSub(RTTimeNow(&TimeDiff), &LastCheckTime);

    int64_t const diffLastCheckSecs = RTTimeSpecGetSeconds(&TimeDiff);
    int64_t const diffLastCheckDays = diffLastCheckSecs / (int64_t)RT_SEC_1DAY_64;

    /* Be as accurate as possible. */
    *aCheckNeeded = diffLastCheckSecs >= (int64_t)m->uCheckFreqSeconds ? TRUE : FALSE;

    LogRel2(("Update agent (%s): Last update %RI64 days (%RI64 seconds) ago, check frequency is every %RU64 days (%RU64 seconds) -> Check %s\n",
             mData.m_strName.c_str(), diffLastCheckDays, diffLastCheckSecs, cCheckFreqDays, m->uCheckFreqSeconds,
             *aCheckNeeded ? "needed" : "not needed"));

    return S_OK;
}

HRESULT UpdateAgent::getSupportedChannels(std::vector<UpdateChannel_T> &aSupportedChannels)
{
    /* No need to take the read lock, as m_enmChannels is const. */

    aSupportedChannels = mData.m_enmChannels;

    return S_OK;
}


/*********************************************************************************************************************************
*   Internal helper methods of update agent class                                                                                *
*********************************************************************************************************************************/

/**
 * Loads the settings of the update agent base class.
 *
 * @returns HRESULT
 * @retval  E_INVALIDARG if to-load settings are invalid / not supported.
 * @param   data                Where to load the settings from.
 */
HRESULT UpdateAgent::i_loadSettings(const settings::UpdateAgent &data)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->fEnabled          = data.fEnabled;
    m->enmChannel        = data.enmChannel;
    m->uCheckFreqSeconds = data.uCheckFreqSeconds;
    if (data.strRepoUrl.isNotEmpty()) /* Prevent overwriting the agent's default URL when XML settings are empty. */
        m->strRepoUrl    = data.strRepoUrl;
    m->strLastCheckDate  = data.strLastCheckDate;
    m->uCheckCount       = data.uCheckCount;

    /* Sanity checks. */
    if (!i_urlSchemeIsSupported(data.strRepoUrl))
        return setError(E_INVALIDARG, tr("Invalid URL scheme specified!"));

    return S_OK;
}

/**
 * Saves the settings of the update agent base class.
 *
 * @returns HRESULT
 * @param   data                Where to save the settings to.
 */
HRESULT UpdateAgent::i_saveSettings(settings::UpdateAgent &data)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    data = *m;

    return S_OK;
}

/**
 * Sets the update check count.
 *
 * @returns HRESULT
 * @param   aCount              Update check count to set.
 */
HRESULT UpdateAgent::i_setCheckCount(ULONG aCount)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->uCheckCount = aCount;

    return i_commitSettings(alock);
}

/**
 * Sets the last update check date.
 *
 * @returns HRESULT
 * @param   aDate               Last update check date to set.
 *                              Must be in ISO 8601 format (e.g. 2020-05-11T21:13:39.348416000Z).
 */
HRESULT UpdateAgent::i_setLastCheckDate(const com::Utf8Str &aDate)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->strLastCheckDate = aDate;

    return i_commitSettings(alock);
}

/**
 * Internal helper function to commit modified settings.
 *
 * @returns HRESULT
 * @param   aLock               Write lock to release before committing settings.
 */
HRESULT UpdateAgent::i_commitSettings(AutoWriteLock &aLock)
{
    aLock.release();

    m_VirtualBox->i_onUpdateAgentSettingsChanged(this, "" /** @todo Include attribute hints */);

    AutoWriteLock vboxLock(m_VirtualBox COMMA_LOCKVAL_SRC_POS);
    return m_VirtualBox->i_saveSettings();
}

/**
 * Returns the proxy mode to use.
 *
 * @returns HRESULT
 * @param   aMode               Where to return the proxy mode.
 */
HRESULT UpdateAgent::i_getProxyMode(ProxyMode_T *aMode)
{
    ComPtr<ISystemProperties> pSystemProperties;
    HRESULT hrc = m_VirtualBox->COMGETTER(SystemProperties)(pSystemProperties.asOutParam());
    if (SUCCEEDED(hrc))
        hrc = pSystemProperties->COMGETTER(ProxyMode)(aMode);

    return hrc;
}

/**
 * Returns the proxy URL to use.
 *
 * @returns HRESULT
 * @param   aUrl                Where to return the proxy URL to use.
 */
HRESULT UpdateAgent::i_getProxyURL(com::Utf8Str &aUrl)
{
    ComPtr<ISystemProperties> pSystemProperties;
    HRESULT hrc = m_VirtualBox->COMGETTER(SystemProperties)(pSystemProperties.asOutParam());
    if (SUCCEEDED(hrc))
    {
        com::Bstr bstrVal;
        hrc = pSystemProperties->COMGETTER(ProxyURL)(bstrVal.asOutParam());
        if (SUCCEEDED(hrc))
            aUrl = bstrVal;
    }

    return hrc;
}

/**
 * Configures a HTTP client's proxy.
 *
 * @returns HRESULT
 * @param   hHttp               HTTP client to configure proxy for.
 */
HRESULT UpdateAgent::i_configureProxy(RTHTTP hHttp)
{
    ProxyMode_T enmProxyMode;
    HRESULT hrc = i_getProxyMode(&enmProxyMode);
    ComAssertComRCRetRC(hrc);

    Utf8Str strProxyUrl;
    hrc = i_getProxyURL(strProxyUrl);
    ComAssertComRCRetRC(hrc);

    if (enmProxyMode == ProxyMode_Manual)
    {
        int vrc = RTHttpSetProxyByUrl(hHttp, strProxyUrl.c_str());
        if (RT_FAILURE(vrc))
            return i_reportError(vrc, tr("RTHttpSetProxyByUrl() failed: %Rrc"), vrc);
    }
    else if (enmProxyMode == ProxyMode_System)
    {
        int vrc = RTHttpUseSystemProxySettings(hHttp);
        if (RT_FAILURE(vrc))
            return i_reportError(vrc, tr("RTHttpUseSystemProxySettings() failed: %Rrc"), vrc);
    }
    else
        Assert(enmProxyMode == ProxyMode_NoProxy);

    LogRel2(("Update agent (%s): Using proxy mode = '%s', URL = '%s'\n",
             mData.m_strName.c_str(), UpdateAgentBase::i_proxyModeToStr(enmProxyMode), strProxyUrl.c_str()));

    return S_OK;
}

/**
 * Reports an error by setting the error info and also informs subscribed listeners.
 *
 * @returns HRESULT
 * @param   vrc                 Result code (IPRT-style) to report.
 * @param   pcszMsgFmt          Error message to report.
 * @param   ...                 Format string for \a pcszMsgFmt.
 */
HRESULT UpdateAgent::i_reportError(int vrc, const char *pcszMsgFmt, ...)
{
    AssertReturn(pcszMsgFmt && *pcszMsgFmt != '\0', E_INVALIDARG);

    va_list va;
    va_start(va, pcszMsgFmt);

    Utf8Str strMsg;
    int const vrc2 = strMsg.printfVNoThrow(pcszMsgFmt, va);
    if (RT_FAILURE(vrc2))
    {
        va_end(va);
        return setErrorBoth(VBOX_E_IPRT_ERROR, vrc2, tr("Failed to format update agent error string (%Rrc)"), vrc2);
    }

    va_end(va);

    LogRel(("Update agent (%s): %s\n", mData.m_strName.c_str(), strMsg.c_str()));

    m_VirtualBox->i_onUpdateAgentError(this, strMsg.c_str(), vrc);

    return setErrorBoth(VBOX_E_IPRT_ERROR, vrc, strMsg.c_str());
}


/*********************************************************************************************************************************
*   Host update implementation                                                                                                   *
*********************************************************************************************************************************/

HostUpdateAgent::HostUpdateAgent(void)
{
}

HostUpdateAgent::~HostUpdateAgent(void)
{
}


HRESULT HostUpdateAgent::FinalConstruct(void)
{
    return BaseFinalConstruct();
}

void HostUpdateAgent::FinalRelease(void)
{
    uninit();

    BaseFinalRelease();
}

HRESULT HostUpdateAgent::init(VirtualBox *aVirtualBox)
{
    // Enclose the state transition NotReady->InInit->Ready.
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Initialize the bare minimum to get things going.
     ** @todo Add more stuff later here. */
    mData.m_strName = "VirtualBox";
    mData.m_fHidden = false;

    const UpdateChannel_T aChannels[] =
    {
        UpdateChannel_Stable,
        UpdateChannel_All,
        UpdateChannel_WithBetas
        /** @todo Add UpdateChannel_WithTesting once it's implemented on the backend. */
    };
    unconst(mData.m_enmChannels).assign(aChannels, aChannels + RT_ELEMENTS(aChannels));

    /* Set default repository. */
    m->strRepoUrl = "https://update.virtualbox.org";

    HRESULT hrc = UpdateAgent::init(aVirtualBox);
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();

    return hrc;
}

void HostUpdateAgent::uninit(void)
{
    // Enclose the state transition Ready->InUninit->NotReady.
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;
}

HRESULT HostUpdateAgent::checkFor(ComPtr<IProgress> &aProgress)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    ComObjPtr<Progress> pProgress;
    HRESULT hrc = pProgress.createObject();
    if (FAILED(hrc))
        return hrc;

    hrc = pProgress->init(m_VirtualBox,
                          static_cast<IUpdateAgent*>(this),
                          tr("Checking for update for %s ...", this->mData.m_strName.c_str()),
                          TRUE /* aCancelable */);
    if (FAILED(hrc))
        return hrc;

    /* initialize the worker task */
    UpdateAgentTask *pTask = new UpdateAgentTask(this, pProgress);
    hrc = pTask->createThread();
    pTask = NULL;
    if (FAILED(hrc))
        return hrc;

    return pProgress.queryInterfaceTo(aProgress.asOutParam());
}


/*********************************************************************************************************************************
*   Host update internal functions                                                                                               *
*********************************************************************************************************************************/

/**
 * Task callback to perform an update check for the VirtualBox host (core).
 *
 * @returns HRESULT
 * @param   pTask               Associated update agent task to use.
 */
DECLCALLBACK(HRESULT) HostUpdateAgent::i_checkForUpdateTask(UpdateAgentTask *pTask)
{
    RT_NOREF(pTask);

    AssertReturn(m->strRepoUrl.isNotEmpty(), E_INVALIDARG);

    // Following the sequence of steps in UIUpdateStepVirtualBox::sltStartStep()
    // Build up our query URL starting with the configured repository.
    Utf8Str strUrl;
    strUrl.appendPrintf("%s/query.php/?", m->strRepoUrl.c_str());

    // Add platform ID.
    Bstr platform;
    HRESULT hrc = m_VirtualBox->COMGETTER(PackageType)(platform.asOutParam());
    AssertComRCReturn(hrc, hrc);
    strUrl.appendPrintf("platform=%ls", platform.raw()); // e.g. SOLARIS_64BITS_GENERIC

    // Get the complete current version string for the query URL
    Bstr versionNormalized;
    hrc = m_VirtualBox->COMGETTER(VersionNormalized)(versionNormalized.asOutParam());
    AssertComRCReturn(hrc, hrc);
    strUrl.appendPrintf("&version=%ls", versionNormalized.raw()); // e.g. 6.1.1
#ifdef DEBUG // Comment out previous line and uncomment this one for testing.
//  strUrl.appendPrintf("&version=6.0.12");
#endif

    ULONG revision = 0;
    hrc = m_VirtualBox->COMGETTER(Revision)(&revision);
    AssertComRCReturn(hrc, hrc);
    strUrl.appendPrintf("_%u", revision); // e.g. 135618

    // Update the last update check timestamp.
    RTTIME Time;
    RTTIMESPEC TimeNow;
    char szTimeStr[RTTIME_STR_LEN];
    RTTimeToString(RTTimeExplode(&Time, RTTimeNow(&TimeNow)), szTimeStr, sizeof(szTimeStr));
    LogRel2(("Update agent (%s): Setting last update check timestamp to '%s'\n", mData.m_strName.c_str(), szTimeStr));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->strLastCheckDate = szTimeStr;
    m->uCheckCount++;

    hrc = i_commitSettings(alock);
    AssertComRCReturn(hrc, hrc);

    strUrl.appendPrintf("&count=%RU32", m->uCheckCount);

    // Update the query URL (if necessary) with the 'channel' information.
    switch (m->enmChannel)
    {
        case UpdateChannel_All:
            strUrl.appendPrintf("&branch=allrelease"); // query.php expects 'allrelease' and not 'allreleases'
            break;
        case UpdateChannel_WithBetas:
            strUrl.appendPrintf("&branch=withbetas");
            break;
        /** @todo Handle UpdateChannel_WithTesting once implemented on the backend. */
        case UpdateChannel_Stable:
            RT_FALL_THROUGH();
        default:
            strUrl.appendPrintf("&branch=stable");
            break;
    }

    LogRel2(("Update agent (%s): Using URL '%s'\n", mData.m_strName.c_str(), strUrl.c_str()));

    /*
     * Compose the User-Agent header for the GET request.
     */
    Bstr version;
    hrc = m_VirtualBox->COMGETTER(Version)(version.asOutParam()); // e.g. 6.1.0_RC1
    AssertComRCReturn(hrc, hrc);

    Utf8StrFmt const strUserAgent("VirtualBox %ls <%s>", version.raw(), UpdateAgent::i_getPlatformInfo().c_str());
    LogRel2(("Update agent (%s): Using user agent '%s'\n",  mData.m_strName.c_str(), strUserAgent.c_str()));

    /*
     * Create the HTTP client instance and pass it to a inner worker method to
     * ensure proper cleanup.
     */
    RTHTTP hHttp = NIL_RTHTTP;
    int vrc = RTHttpCreate(&hHttp);
    if (RT_SUCCESS(vrc))
    {
        try
        {
            hrc = i_checkForUpdateInner(hHttp, strUrl, strUserAgent);
        }
        catch (...)
        {
            AssertFailed();
            hrc = E_UNEXPECTED;
        }
        RTHttpDestroy(hHttp);
    }
    else
        hrc = i_reportError(vrc, tr("RTHttpCreate() failed: %Rrc"), vrc);

    return hrc;
}

/**
 * Inner function of the actual update checking mechanism.
 *
 * @returns HRESULT
 * @param   hHttp               HTTP client instance to use for checking.
 * @param   strUrl              URL of repository to check.
 * @param   strUserAgent        HTTP user agent to use for checking.
 */
HRESULT HostUpdateAgent::i_checkForUpdateInner(RTHTTP hHttp, Utf8Str const &strUrl, Utf8Str const &strUserAgent)
{
    /*
     * Configure the proxy (if any).
     */
    HRESULT hrc = i_configureProxy(hHttp);
    if (FAILED(hrc))
        return hrc;

    /** @todo Are there any other headers needed to be added first via RTHttpSetHeaders()? */
    int vrc = RTHttpAddHeader(hHttp, "User-Agent", strUserAgent.c_str(), strUserAgent.length(), RTHTTPADDHDR_F_BACK);
    if (RT_FAILURE(vrc))
        return i_reportError(vrc, tr("RTHttpAddHeader() failed: %Rrc (user agent)"), vrc);

    /*
     * Perform the GET request, returning raw binary stuff.
     */
    void *pvResponse = NULL;
    size_t cbResponse = 0;
    vrc = RTHttpGetBinary(hHttp, strUrl.c_str(), &pvResponse, &cbResponse);
    if (RT_FAILURE(vrc))
        return i_reportError(vrc, tr("RTHttpGetBinary() failed: %Rrc"), vrc);

    /* Note! We can do nothing that might throw exceptions till we call RTHttpFreeResponse! */

    /*
     * If url is platform=DARWIN_64BITS_GENERIC&version=6.0.12&branch=stable for example, the reply is:
     *      6.0.14<SPACE>https://download.virtualbox.org/virtualbox/6.0.14/VirtualBox-6.0.14-133895-OSX.dmg
     * If no update required, 'UPTODATE' is returned.
     */
    /* Parse out the two first words of the response, ignoring whatever follows: */
    const char *pchResponse = (const char *)pvResponse;
    while (cbResponse > 0 && *pchResponse == ' ')
        cbResponse--, pchResponse++;

    char ch;
    const char *pchWord0 = pchResponse;
    while (cbResponse > 0 && (ch = *pchResponse) != ' ' && ch != '\0')
        cbResponse--, pchResponse++;
    size_t const cchWord0 = (size_t)(pchResponse - pchWord0);

    while (cbResponse > 0 && *pchResponse == ' ')
        cbResponse--, pchResponse++;
    const char *pchWord1 = pchResponse;
    while (cbResponse > 0 && (ch = *pchResponse) != ' ' && ch != '\0')
        cbResponse--, pchResponse++;
    size_t const cchWord1 = (size_t)(pchResponse - pchWord1);

    /* Decode the two word: */
    static char const s_szUpToDate[] = "UPTODATE";
    if (   cchWord0 == sizeof(s_szUpToDate) - 1
        && memcmp(pchWord0, s_szUpToDate, sizeof(s_szUpToDate) - 1) == 0)
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        mData.m_enmState = UpdateState_NotAvailable;
        hrc = S_OK;

        alock.release(); /* Release lock before firing off event. */

        m_VirtualBox->i_onUpdateAgentStateChanged(this, UpdateState_NotAvailable);
    }
    else
    {
        mData.m_enmState = UpdateState_Error; /* Play safe by default. */

        vrc = RTStrValidateEncodingEx(pchWord0, cchWord0, 0 /*fFlags*/);
        if (RT_SUCCESS(vrc))
            vrc = RTStrValidateEncodingEx(pchWord1, cchWord1, 0 /*fFlags*/);
        if (RT_SUCCESS(vrc))
        {
            /** @todo Any additional sanity checks we could perform here? */
            hrc = mData.m_lastResult.strVer.assignEx(pchWord0, cchWord0);
            if (SUCCEEDED(hrc))
                hrc = mData.m_lastResult.strDownloadUrl.assignEx(pchWord1, cchWord1);

            if (SUCCEEDED(hrc))
            {
                AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

                /** @todo Implement this on the backend first.
                 *        We also could do some guessing based on the installed version vs. reported update version? */
                mData.m_lastResult.enmSeverity = UpdateSeverity_Invalid;
                mData.m_enmState               = UpdateState_Available;

                alock.release(); /* Release lock before firing off events. */

                m_VirtualBox->i_onUpdateAgentStateChanged(this, UpdateState_Available);
                m_VirtualBox->i_onUpdateAgentAvailable(this, mData.m_lastResult.strVer, m->enmChannel,
                                                       mData.m_lastResult.enmSeverity, mData.m_lastResult.strDownloadUrl,
                                                       mData.m_lastResult.strWebUrl, mData.m_lastResult.strReleaseNotes);
            }
            else
                hrc = i_reportError(VERR_GENERAL_FAILURE /** @todo Use a better hrc */,
                                    tr("Invalid server response [1]: %Rhrc (%.*Rhxs -- %.*Rhxs)"),
                                    hrc, cchWord0, pchWord0, cchWord1, pchWord1);

            LogRel2(("Update agent (%s): HTTP server replied: %.*s %.*s\n",
                     mData.m_strName.c_str(), cchWord0, pchWord0, cchWord1, pchWord1));
        }
        else
            hrc = i_reportError(vrc, tr("Invalid server response [2]: %Rrc (%.*Rhxs -- %.*Rhxs)"),
                                vrc, cchWord0, pchWord0, cchWord1, pchWord1);
    }

    RTHttpFreeResponse(pvResponse);

    return hrc;
}

