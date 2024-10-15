/* $Id: ExtPackManagerImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - interface for Extension Packs, VBoxSVC & VBoxC.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_ExtPackManagerImpl_h
#define MAIN_INCLUDED_ExtPackManagerImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VirtualBoxBase.h"
#include <VBox/ExtPack/ExtPack.h>
#include <VBox/version.h> /* VBOX_PUEL_PRODUCT */
#include "ExtPackWrap.h"
#include "ExtPackFileWrap.h"
#include "ExtPackManagerWrap.h"
#include <iprt/fs.h>


/** The name of the Oracle extension pack. */
#define ORACLE_PUEL_EXTPACK_NAME VBOX_PUEL_PRODUCT


#ifndef VBOX_COM_INPROC
/**
 * An extension pack file.
 */
class ATL_NO_VTABLE ExtPackFile :
    public ExtPackFileWrap
{
public:
    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    DECLARE_COMMON_CLASS_METHODS(ExtPackFile)

    HRESULT     FinalConstruct();
    void        FinalRelease();
    HRESULT     initWithFile(const char *a_pszFile, const char *a_pszDigest, class ExtPackManager *a_pExtPackMgr, VirtualBox *a_pVirtualBox);
    void        uninit();
    /** @}  */

private:
    /** @name Misc init helpers
     * @{ */
    HRESULT     initFailed(const char *a_pszWhyFmt, ...);
    /** @} */

private:

    // wrapped IExtPackFile properties
    HRESULT getName(com::Utf8Str &aName);
    HRESULT getDescription(com::Utf8Str &aDescription);
    HRESULT getVersion(com::Utf8Str &aVersion);
    HRESULT getRevision(ULONG *aRevision);
    HRESULT getEdition(com::Utf8Str &aEdition);
    HRESULT getVRDEModule(com::Utf8Str &aVRDEModule);
    HRESULT getCryptoModule(com::Utf8Str &aCryptoModule);
    HRESULT getPlugIns(std::vector<ComPtr<IExtPackPlugIn> > &aPlugIns);
    HRESULT getUsable(BOOL *aUsable);
    HRESULT getWhyUnusable(com::Utf8Str &aWhyUnusable);
    HRESULT getShowLicense(BOOL *aShowLicense);
    HRESULT getLicense(com::Utf8Str &aLicense);
    HRESULT getFilePath(com::Utf8Str &aFilePath);

    // wrapped IExtPackFile methods
    HRESULT queryLicense(const com::Utf8Str &aPreferredLocale,
                         const com::Utf8Str &aPreferredLanguage,
                         const com::Utf8Str &aFormat,
                         com::Utf8Str &aLicenseText);
    HRESULT install(BOOL aReplace,
                    const com::Utf8Str &aDisplayInfo,
                    ComPtr<IProgress> &aProgess);

    struct Data;
    /** Pointer to the private instance. */
    Data *m;

    friend class ExtPackManager;
    friend class ExtPackInstallTask;
};
#endif /* !VBOX_COM_INPROC */


/**
 * An installed extension pack.
 */
class ATL_NO_VTABLE ExtPack :
    public ExtPackWrap
{
public:
    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    DECLARE_COMMON_CLASS_METHODS(ExtPack)

    HRESULT     FinalConstruct();
    void        FinalRelease();
    HRESULT     initWithDir(VirtualBox *a_pVirtualBox, VBOXEXTPACKCTX a_enmContext, const char *a_pszName, const char *a_pszDir);
    void        uninit();
    RTMEMEF_NEW_AND_DELETE_OPERATORS();
    /** @}  */

    /** @name Internal interfaces used by ExtPackManager.
     * @{ */
#ifndef VBOX_COM_INPROC
    bool        i_callInstalledHook(IVirtualBox *a_pVirtualBox, AutoWriteLock *a_pLock, PRTERRINFO pErrInfo);
    HRESULT     i_callUninstallHookAndClose(IVirtualBox *a_pVirtualBox, bool a_fForcedRemoval);
    bool        i_callVirtualBoxReadyHook(IVirtualBox *a_pVirtualBox, AutoWriteLock *a_pLock);
#endif
#ifdef VBOX_COM_INPROC
    bool        i_callConsoleReadyHook(IConsole *a_pConsole, AutoWriteLock *a_pLock);
#endif
#ifndef VBOX_COM_INPROC
    bool        i_callVmCreatedHook(IVirtualBox *a_pVirtualBox, IMachine *a_pMachine, AutoWriteLock *a_pLock);
#endif
#ifdef VBOX_COM_INPROC
    bool        i_callVmConfigureVmmHook(IConsole *a_pConsole, PVM a_pVM, PCVMMR3VTABLE a_pVMM,
                                         AutoWriteLock *a_pLock, int *a_pvrc);
    bool        i_callVmPowerOnHook(IConsole *a_pConsole, PVM a_pVM, PCVMMR3VTABLE a_pVMM, AutoWriteLock *a_pLock, int *a_pvrc);
    bool        i_callVmPowerOffHook(IConsole *a_pConsole, PVM a_pVM, PCVMMR3VTABLE a_pVMM, AutoWriteLock *a_pLock);
#endif
    HRESULT     i_checkVrde(void);
    HRESULT     i_checkCrypto(void);
    HRESULT     i_getVrdpLibraryName(Utf8Str *a_pstrVrdeLibrary);
    HRESULT     i_getCryptoLibraryName(Utf8Str *a_pstrCryptoLibrary);
    HRESULT     i_getLibraryName(const char *a_pszModuleName, Utf8Str *a_pstrLibrary);
    bool        i_wantsToBeDefaultVrde(void) const;
    bool        i_wantsToBeDefaultCrypto(void) const;
    HRESULT     i_refresh(bool *pfCanDelete);
#ifndef VBOX_COM_INPROC
    bool        i_areThereCloudProviderUninstallVetos();
    void        i_notifyCloudProviderManager();
#endif
    /** @}  */

protected:
    /** @name Internal helper methods.
     * @{ */
    void        i_probeAndLoad(void);
    bool        i_findModule(const char *a_pszName, const char *a_pszExt, VBOXEXTPACKMODKIND a_enmKind,
                             Utf8Str *a_ppStrFound, bool *a_pfNative, PRTFSOBJINFO a_pObjInfo) const;
    static bool i_objinfoIsEqual(PCRTFSOBJINFO pObjInfo1, PCRTFSOBJINFO pObjInfo2);
    /** @}  */

    /** @name Extension Pack Helpers
     * @{ */
    static DECLCALLBACK(int)    i_hlpFindModule(PCVBOXEXTPACKHLP pHlp, const char *pszName, const char *pszExt,
                                                VBOXEXTPACKMODKIND enmKind, char *pszFound, size_t cbFound, bool *pfNative);
    static DECLCALLBACK(int)    i_hlpGetFilePath(PCVBOXEXTPACKHLP pHlp, const char *pszFilename, char *pszPath, size_t cbPath);
    static DECLCALLBACK(VBOXEXTPACKCTX) i_hlpGetContext(PCVBOXEXTPACKHLP pHlp);
    static DECLCALLBACK(int)    i_hlpLoadHGCMService(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IConsole) *pConsole, const char *pszServiceLibrary, const char *pszServiceName);
    static DECLCALLBACK(int)    i_hlpLoadVDPlugin(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IVirtualBox) *pVirtualBox, const char *pszPluginLibrary);
    static DECLCALLBACK(int)    i_hlpUnloadVDPlugin(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IVirtualBox) *pVirtualBox, const char *pszPluginLibrary);
    static DECLCALLBACK(uint32_t) i_hlpCreateProgress(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IUnknown) *pInitiator,
                                                      const char *pcszDescription, uint32_t cOperations,
                                                      uint32_t uTotalOperationsWeight, const char *pcszFirstOperationDescription,
                                                      uint32_t uFirstOperationWeight, VBOXEXTPACK_IF_CS(IProgress) **ppProgressOut);
    static DECLCALLBACK(uint32_t) i_hlpGetCanceledProgress(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IProgress) *pProgress,
                                                           bool *pfCanceled);
    static DECLCALLBACK(uint32_t) i_hlpUpdateProgress(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IProgress) *pProgress,
                                                      uint32_t uPercent);
    static DECLCALLBACK(uint32_t) i_hlpNextOperationProgress(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IProgress) *pProgress,
                                                             const char *pcszNextOperationDescription,
                                                             uint32_t uNextOperationWeight);
    static DECLCALLBACK(uint32_t) i_hlpWaitOtherProgress(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IProgress) *pProgress,
                                                         VBOXEXTPACK_IF_CS(IProgress) *pProgressOther,
                                                         uint32_t cTimeoutMS);
    static DECLCALLBACK(uint32_t) i_hlpCompleteProgress(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IProgress) *pProgress,
                                                        uint32_t uResultCode);
    static DECLCALLBACK(uint32_t) i_hlpCreateEvent(PCVBOXEXTPACKHLP pHlp,
                                                   VBOXEXTPACK_IF_CS(IEventSource) *aSource,
                                                   /* VBoxEventType_T */ uint32_t aType, bool aWaitable,
                                                   VBOXEXTPACK_IF_CS(IEvent) **ppEventOut);
    static DECLCALLBACK(uint32_t) i_hlpCreateVetoEvent(PCVBOXEXTPACKHLP pHlp,
                                                       VBOXEXTPACK_IF_CS(IEventSource) *aSource,
                                                       /* VBoxEventType_T */ uint32_t aType,
                                                       VBOXEXTPACK_IF_CS(IVetoEvent) **ppEventOut);
    static DECLCALLBACK(const char *) i_hlpTranslate(PCVBOXEXTPACKHLP pHlp,
                                                     const char      *pszComponent,
                                                     const char      *pszSourceText,
                                                     const char      *pszComment = NULL,
                                                     const size_t     uNum = ~(size_t)0);
    static DECLCALLBACK(int)      i_hlpReservedN(PCVBOXEXTPACKHLP pHlp);
    /** @}  */

private:

    // wrapped IExtPack properties
    HRESULT getName(com::Utf8Str &aName);
    HRESULT getDescription(com::Utf8Str &aDescription);
    HRESULT getVersion(com::Utf8Str &aVersion);
    HRESULT getRevision(ULONG *aRevision);
    HRESULT getEdition(com::Utf8Str &aEdition);
    HRESULT getVRDEModule(com::Utf8Str &aVRDEModule);
    HRESULT getCryptoModule(com::Utf8Str &aCryptoModule);
    HRESULT getPlugIns(std::vector<ComPtr<IExtPackPlugIn> > &aPlugIns);
    HRESULT getUsable(BOOL *aUsable);
    HRESULT getWhyUnusable(com::Utf8Str &aWhyUnusable);
    HRESULT getShowLicense(BOOL *aShowLicense);
    HRESULT getLicense(com::Utf8Str &aLicense);

    // wrapped IExtPack methods
    HRESULT queryLicense(const com::Utf8Str &aPreferredLocale,
                         const com::Utf8Str &aPreferredLanguage,
                         const com::Utf8Str &aFormat,
                         com::Utf8Str &aLicenseText);
    HRESULT queryObject(const com::Utf8Str &aObjUuid,
                        ComPtr<IUnknown> &aReturnInterface);


    struct Data;
    /** Pointer to the private instance. */
    Data *m;

    friend class ExtPackManager;
};


/**
 * Extension pack manager.
 */
class ATL_NO_VTABLE ExtPackManager :
    public ExtPackManagerWrap
{
public:
    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    DECLARE_COMMON_CLASS_METHODS(ExtPackManager)

    HRESULT     FinalConstruct();
    void        FinalRelease();
    HRESULT     initExtPackManager(VirtualBox *a_pVirtualBox, VBOXEXTPACKCTX a_enmContext);
    void        uninit();
    /** @}  */

    /** @name Internal interfaces used by other Main classes.
     * @{ */
#ifndef VBOX_COM_INPROC
    HRESULT     i_doInstall(ExtPackFile *a_pExtPackFile, bool a_fReplace, Utf8Str const *a_pstrDisplayInfo);
    HRESULT     i_doUninstall(const Utf8Str *a_pstrName, bool a_fForcedRemoval, const Utf8Str *a_pstrDisplayInfo);
    void        i_callAllVirtualBoxReadyHooks(void);
    HRESULT     i_queryObjects(const com::Utf8Str &aObjUuid, std::vector<ComPtr<IUnknown> > &aObjects, std::vector<com::Utf8Str> *a_pstrExtPackNames);
#endif
#ifdef VBOX_COM_INPROC
    void        i_callAllConsoleReadyHooks(IConsole *a_pConsole);
#endif
#ifndef VBOX_COM_INPROC
    void        i_callAllVmCreatedHooks(IMachine *a_pMachine);
#endif
#ifdef VBOX_COM_INPROC
    int         i_callAllVmConfigureVmmHooks(IConsole *a_pConsole, PVM a_pVM, PCVMMR3VTABLE a_pVMM);
    int         i_callAllVmPowerOnHooks(IConsole *a_pConsole, PVM a_pVM, PCVMMR3VTABLE a_pVMM);
    void        i_callAllVmPowerOffHooks(IConsole *a_pConsole, PVM a_pVM, PCVMMR3VTABLE a_pVMM);
#endif
    HRESULT     i_checkVrdeExtPack(Utf8Str const *a_pstrExtPack);
    int         i_getVrdeLibraryPathForExtPack(Utf8Str const *a_pstrExtPack, Utf8Str *a_pstrVrdeLibrary);
    HRESULT     i_checkCryptoExtPack(Utf8Str const *a_pstrExtPack);
    int         i_getCryptoLibraryPathForExtPack(Utf8Str const *a_pstrExtPack, Utf8Str *a_pstrVrdeLibrary);
    HRESULT     i_getLibraryPathForExtPack(const char *a_pszModuleName, const char *a_pszExtPack, Utf8Str *a_pstrLibrary);
    HRESULT     i_getDefaultVrdeExtPack(Utf8Str *a_pstrExtPack);
    HRESULT     i_getDefaultCryptoExtPack(Utf8Str *a_pstrExtPack);
    bool        i_isExtPackUsable(const char *a_pszExtPack);
    void        i_dumpAllToReleaseLog(void);
    uint64_t    i_getUpdateCounter(void);
    /** @}  */

private:
    // wrapped IExtPackManager properties
    HRESULT getInstalledExtPacks(std::vector<ComPtr<IExtPack> > &aInstalledExtPacks);

   // wrapped IExtPackManager methods
    HRESULT find(const com::Utf8Str &aName,
                 ComPtr<IExtPack> &aReturnData);
    HRESULT openExtPackFile(const com::Utf8Str &aPath,
                                  ComPtr<IExtPackFile> &aFile);
    HRESULT uninstall(const com::Utf8Str &aName,
                      BOOL aForcedRemoval,
                      const com::Utf8Str &aDisplayInfo,
                      ComPtr<IProgress> &aProgess);
    HRESULT cleanup();
    HRESULT queryAllPlugInsForFrontend(const com::Utf8Str &aFrontendName,
                                       std::vector<com::Utf8Str> &aPlugInModules);
    HRESULT isExtPackUsable(const com::Utf8Str &aName,
                            BOOL *aUsable);

    bool        i_areThereAnyRunningVMs(void) const;
    HRESULT     i_runSetUidToRootHelper(Utf8Str const *a_pstrDisplayInfo, const char *a_pszCommand, ...);
    ExtPack    *i_findExtPack(const char *a_pszName);
    void        i_removeExtPack(const char *a_pszName);
    HRESULT     i_refreshExtPack(const char *a_pszName, bool a_fUnsuableIsError, ExtPack **a_ppExtPack);

private:
    struct Data;
    /** Pointer to the private instance. */
    Data *m;

    friend class ExtPackUninstallTask;
};

#endif /* !MAIN_INCLUDED_ExtPackManagerImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
