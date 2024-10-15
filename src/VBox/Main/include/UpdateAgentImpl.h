/* $Id: UpdateAgentImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Update agent COM class implementation - Header
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

#ifndef MAIN_INCLUDED_UpdateAgentImpl_h
#define MAIN_INCLUDED_UpdateAgentImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/http.h>

#include <VBox/settings.h>

#include "EventImpl.h"
#include "UpdateAgentWrap.h"
#include "HostUpdateAgentWrap.h"

class  UpdateAgentTask;
struct UpdateAgentTaskParms;

struct UpdateAgentTaskResult
{
    Utf8Str          strVer;
    Utf8Str          strWebUrl;
    Utf8Str          strDownloadUrl;
    UpdateSeverity_T enmSeverity;
    Utf8Str          strReleaseNotes;
};

class UpdateAgentBase
{
protected: /* Not directly instancable. */

    UpdateAgentBase()
        : m_VirtualBox(NULL)
        , m(new settings::UpdateAgent) { }

    virtual ~UpdateAgentBase() { delete m; }

public:

    /** @name Pure virtual public methods for internal purposes only
     *        (ensure there is a caller and a read lock before calling them!)
     * @{ */
    virtual HRESULT i_loadSettings(const settings::UpdateAgent &data) = 0;
    virtual HRESULT i_saveSettings(settings::UpdateAgent &data) = 0;

    virtual HRESULT i_setCheckCount(ULONG aCount) = 0;
    virtual HRESULT i_setLastCheckDate(const com::Utf8Str &aDate) = 0;
    /** @} */

protected:

    /** @name Pure virtual internal task callbacks.
     * @{ */
    friend UpdateAgentTask;
    virtual DECLCALLBACK(HRESULT) i_checkForUpdateTask(UpdateAgentTask *pTask) = 0;
    /** @} */

    /** @name Static helper methods.
     * @{ */
    static Utf8Str i_getPlatformInfo(void);
    const char    *i_proxyModeToStr(ProxyMode_T enmMode);
    bool           i_urlSchemeIsSupported(const Utf8Str &strUrl) const;
    /** @} */

protected:
    /** The update agent's event source. */
    const ComObjPtr<EventSource> m_EventSource;
    VirtualBox * const           m_VirtualBox;

    /** @name Data members.
     * @{ */
    settings::UpdateAgent *m;

    struct Data
    {
        UpdateAgentTaskResult              m_lastResult;
        Utf8Str                            m_strName;
        /** Vector of update channels this agent supports. */
        const std::vector<UpdateChannel_T> m_enmChannels;
        bool                               m_fHidden;
        UpdateState_T                      m_enmState;
        uint32_t                           m_uOrder;

        Data(void)
        {
            m_fHidden  = true;
            m_enmState = UpdateState_Invalid;
            m_uOrder   = UINT32_MAX;
        }
    } mData;
    /** @} */
};

class ATL_NO_VTABLE UpdateAgent :
    public UpdateAgentWrap,
    public UpdateAgentBase
{
public:
    DECLARE_COMMON_CLASS_METHODS(UpdateAgent)

    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    HRESULT FinalConstruct();
    void FinalRelease();

    HRESULT init(VirtualBox *aVirtualBox);
    void uninit(void);
    /** @}  */

    /** @name Public methods for internal purposes only
     *        (ensure there is a caller and a read lock before calling them!)
     * @{ */
    HRESULT i_loadSettings(const settings::UpdateAgent &data);
    HRESULT i_saveSettings(settings::UpdateAgent &data);

    virtual HRESULT i_setCheckCount(ULONG aCount);
    virtual HRESULT i_setLastCheckDate(const com::Utf8Str &aDate);
    /** @} */

protected:

    /** @name Internal helper methods.
     * @{ */
    HRESULT i_getProxyMode(ProxyMode_T *aMode);
    HRESULT i_getProxyURL(com::Utf8Str &aAddress);
    HRESULT i_configureProxy(RTHTTP hHttp);
    HRESULT i_commitSettings(AutoWriteLock &aLock);
    HRESULT i_reportError(int vrc, const char *pcszMsgFmt, ...);
    /** @}  */

protected:
    /** @name Wrapped IUpdateAgent attributes and methods.
     * @{ */
    HRESULT checkFor(ComPtr<IProgress> &aProgress);
    HRESULT download(ComPtr<IProgress> &aProgress);
    HRESULT install(ComPtr<IProgress> &aProgress);
    HRESULT rollback(void);

    HRESULT getName(com::Utf8Str &aName);
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    HRESULT getOrder(ULONG *aOrder);
    HRESULT getDependsOn(std::vector<com::Utf8Str> &aDeps);
    HRESULT getVersion(com::Utf8Str &aVer);
    HRESULT getDownloadUrl(com::Utf8Str &aUrl);
    HRESULT getWebUrl(com::Utf8Str &aUrl);
    HRESULT getReleaseNotes(com::Utf8Str &aRelNotes);
    HRESULT getEnabled(BOOL *aEnabled);
    HRESULT setEnabled(BOOL aEnabled);
    HRESULT getHidden(BOOL *aHidden);
    HRESULT getState(UpdateState_T *aState);
    HRESULT getCheckCount(ULONG *aCount);
    HRESULT getCheckFrequency(ULONG  *aFreqSeconds);
    HRESULT setCheckFrequency(ULONG aFreqSeconds);
    HRESULT getChannel(UpdateChannel_T *aChannel);
    HRESULT setChannel(UpdateChannel_T aChannel);
    HRESULT getRepositoryURL(com::Utf8Str &aRepo);
    HRESULT setRepositoryURL(const com::Utf8Str &aRepo);
    HRESULT getLastCheckDate(com::Utf8Str &aData);
    HRESULT getIsCheckNeeded(BOOL *aCheckNeeded);
    HRESULT getSupportedChannels(std::vector<UpdateChannel_T> &aSupportedChannels);
    /** @} */
};

/** @todo Put this into an own module, e.g. HostUpdateAgentImpl.[cpp|h]. */

class ATL_NO_VTABLE HostUpdateAgent :
    public UpdateAgent
{
public:
    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    DECLARE_COMMON_CLASS_METHODS(HostUpdateAgent)

    HRESULT init(VirtualBox *aVirtualBox);
    void    uninit(void);

    HRESULT FinalConstruct(void);
    void    FinalRelease(void);
    /** @}  */

private:
    /** @name Implemented (pure) virtual methods from UpdateAgent.
     * @{ */
    HRESULT checkFor(ComPtr<IProgress> &aProgress);

    DECLCALLBACK(HRESULT) i_checkForUpdateTask(UpdateAgentTask *pTask);
    /** @}  */

    HRESULT i_checkForUpdate(void);
    HRESULT i_checkForUpdateInner(RTHTTP hHttp, com::Utf8Str const &strUrl, com::Utf8Str const &strUserAgent);
};

#endif /* !MAIN_INCLUDED_UpdateAgentImpl_h */

