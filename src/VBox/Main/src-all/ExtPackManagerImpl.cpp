/* $Id: ExtPackManagerImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "ExtPackManagerImpl.h"
#include "CloudProviderManagerImpl.h"
#include "ExtPackUtil.h"
#include "ThreadTask.h"

#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/ldr.h>
#include <iprt/locale.h>
#include <iprt/manifest.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/process.h>
#include <iprt/string.h>

#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/sup.h>
#include <VBox/version.h>

#include <algorithm>

#include "AutoCaller.h"
#include "Global.h"
#include "ProgressImpl.h"
#ifdef VBOX_COM_INPROC
# include "ConsoleImpl.h"
#else
# include "VirtualBoxImpl.h"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def VBOX_EXTPACK_HELPER_NAME
 * The name of the utility application we employ to install and uninstall the
 * extension packs.  This is a set-uid-to-root binary on unixy platforms, which
 * is why it has to be a separate application.
 */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
# define VBOX_EXTPACK_HELPER_NAME       "VBoxExtPackHelperApp.exe"
#else
# define VBOX_EXTPACK_HELPER_NAME       "VBoxExtPackHelperApp"
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
struct ExtPackBaseData
{
public:
    /** The extension pack descriptor (loaded from the XML, mostly). */
    VBOXEXTPACKDESC     Desc;
    /** The file system object info of the XML file.
     * This is for detecting changes and save time in refresh().  */
    RTFSOBJINFO         ObjInfoDesc;
    /** Whether it's usable or not. */
    bool                fUsable;
    /** Why it is unusable. */
    Utf8Str             strWhyUnusable;
};

#ifndef VBOX_COM_INPROC
/**
 * Private extension pack data.
 */
struct ExtPackFile::Data : public ExtPackBaseData
{
public:
    /** The path to the tarball. */
    Utf8Str             strExtPackFile;
    /** The SHA-256 hash of the file (as string). */
    Utf8Str             strDigest;
    /** The file handle of the extension pack file. */
    RTFILE              hExtPackFile;
    /** Our manifest for the tarball. */
    RTMANIFEST          hOurManifest;
    /** Pointer to the extension pack manager. */
    ComObjPtr<ExtPackManager> ptrExtPackMgr;
    /** Pointer to the VirtualBox object so we can create a progress object. */
    VirtualBox         *pVirtualBox;

    RTMEMEF_NEW_AND_DELETE_OPERATORS();
};
#endif

/**
 * Private extension pack data.
 */
struct ExtPack::Data : public ExtPackBaseData
{
public:
    /** Where the extension pack is located. */
    Utf8Str             strExtPackPath;
    /** The file system object info of the extension pack directory.
     * This is for detecting changes and save time in refresh().  */
    RTFSOBJINFO         ObjInfoExtPack;
    /** The full path to the main module. */
    Utf8Str             strMainModPath;
    /** The file system object info of the main module.
     * This is used to determin whether to bother try reload it. */
    RTFSOBJINFO         ObjInfoMainMod;
    /** The module handle of the main extension pack module. */
    RTLDRMOD            hMainMod;

    /** The helper callbacks for the extension pack. */
    VBOXEXTPACKHLP      Hlp;
    /** Pointer back to the extension pack object (for Hlp methods). */
    ExtPack            *pThis;
#ifndef VBOX_COM_INPROC
    /** The extension pack main registration structure. */
    PCVBOXEXTPACKREG    pReg;
#else
    /** The extension pack main VM registration structure. */
    PCVBOXEXTPACKVMREG  pReg;
#endif
    /** The current context. */
    VBOXEXTPACKCTX      enmContext;
    /** Set if we've made the pfnVirtualBoxReady or pfnConsoleReady call. */
    bool                fMadeReadyCall;
#ifndef VBOX_COM_INPROC
    /** Pointer to the VirtualBox object so we can create a progress object. */
    VirtualBox         *pVirtualBox;
#endif
#ifdef VBOX_WITH_MAIN_NLS
    PTRCOMPONENT        pTrComponent;
#endif

    RTMEMEF_NEW_AND_DELETE_OPERATORS();
};

/** List of extension packs. */
typedef std::list< ComObjPtr<ExtPack> > ExtPackList;

/**
 * Private extension pack manager data.
 */
struct ExtPackManager::Data
{
    Data()
        : cUpdate(0)
    {}

    /** The directory where the extension packs are installed. */
    Utf8Str             strBaseDir;
    /** The directory where the certificates this installation recognizes are
     * stored. */
    Utf8Str             strCertificatDirPath;
    /** The list of installed extension packs. */
    ExtPackList         llInstalledExtPacks;
#ifndef VBOX_COM_INPROC
    /** Pointer to the VirtualBox object, our parent. */
    VirtualBox         *pVirtualBox;
#endif
    /** The current context. */
    VBOXEXTPACKCTX      enmContext;
    /** Update counter for the installed extension packs, increased in every list update. */
    uint64_t            cUpdate;

    RTMEMEF_NEW_AND_DELETE_OPERATORS();
};

#ifndef VBOX_COM_INPROC

/**
 * Extension pack installation job.
 */
class ExtPackInstallTask : public ThreadTask
{
public:
    explicit ExtPackInstallTask() : ThreadTask("ExtPackInst") { }
    ~ExtPackInstallTask() { }

    DECLARE_TRANSLATE_METHODS(ExtPackInstallTask)

    void handler()
    {
        HRESULT hrc = ptrExtPackMgr->i_doInstall(ptrExtPackFile, fReplace, &strDisplayInfo);
        ptrProgress->i_notifyComplete(hrc);
    }

    HRESULT Init(const ComPtr<ExtPackFile> &a_strExtPackFile, bool a_fReplace,
                 const Utf8Str &strDispInfo, const ComPtr<ExtPackManager> &a_ptrExtPackMgr)
    {
        ptrExtPackFile = a_strExtPackFile;
        fReplace       = a_fReplace;
        strDisplayInfo = strDispInfo;
        ptrExtPackMgr  = a_ptrExtPackMgr;

        HRESULT hrc = ptrProgress.createObject();
        if (SUCCEEDED(hrc))
        {
            Bstr bstrDescription(tr("Installing extension pack"));
            hrc = ptrProgress->init(ptrExtPackFile->m->pVirtualBox,
                                    static_cast<IExtPackFile *>(ptrExtPackFile),
                                    bstrDescription.raw(),
                                    FALSE /*aCancelable*/);
        }

        return hrc;
    }

    /** Smart pointer to the progress object for this job. */
    ComObjPtr<Progress>     ptrProgress;
private:
    /** Smart pointer to the extension pack file. */
    ComPtr<ExtPackFile>     ptrExtPackFile;
    /** The replace argument. */
    bool                    fReplace;
    /** The display info argument.  */
    Utf8Str                 strDisplayInfo;
    /** Smart pointer to the extension manager. */
    ComPtr<ExtPackManager>  ptrExtPackMgr;
};

/**
 * Extension pack uninstallation job.
 */
class ExtPackUninstallTask : public ThreadTask
{
public:
    explicit ExtPackUninstallTask() : ThreadTask("ExtPackUninst") { }
    ~ExtPackUninstallTask() { }
    DECLARE_TRANSLATE_METHODS(ExtPackUninstallTask)

    void handler()
    {
        HRESULT hrc = ptrExtPackMgr->i_doUninstall(&strName, fForcedRemoval, &strDisplayInfo);
        ptrProgress->i_notifyComplete(hrc);
    }

    HRESULT Init(const ComPtr<ExtPackManager> &a_ptrExtPackMgr, const Utf8Str &a_strName,
                 bool a_fForcedRemoval, const Utf8Str &a_strDisplayInfo)
    {
        ptrExtPackMgr  = a_ptrExtPackMgr;
        strName        = a_strName;
        fForcedRemoval = a_fForcedRemoval;
        strDisplayInfo = a_strDisplayInfo;

        HRESULT hrc = ptrProgress.createObject();
        if (SUCCEEDED(hrc))
        {
            Bstr bstrDescription(tr("Uninstalling extension pack"));
            hrc = ptrProgress->init(ptrExtPackMgr->m->pVirtualBox,
                                    static_cast<IExtPackManager *>(ptrExtPackMgr),
                                    bstrDescription.raw(),
                                    FALSE /*aCancelable*/);
        }

        return hrc;
    }

    /** Smart pointer to the progress object for this job. */
    ComObjPtr<Progress>     ptrProgress;
private:
    /** Smart pointer to the extension manager. */
    ComPtr<ExtPackManager>  ptrExtPackMgr;
    /** The name of the extension pack. */
    Utf8Str                 strName;
    /** The replace argument. */
    bool                    fForcedRemoval;
    /** The display info argument.  */
    Utf8Str                 strDisplayInfo;
};

DEFINE_EMPTY_CTOR_DTOR(ExtPackFile)

/**
 * Called by ComObjPtr::createObject when creating the object.
 *
 * Just initialize the basic object state, do the rest in initWithDir().
 *
 * @returns S_OK.
 */
HRESULT ExtPackFile::FinalConstruct()
{
    m = NULL;
    return BaseFinalConstruct();
}

/**
 * Initializes the extension pack by reading its file.
 *
 * @returns COM status code.
 * @param   a_pszFile       The path to the extension pack file.
 * @param   a_pszDigest     The SHA-256 digest of the file. Or an empty string.
 * @param   a_pExtPackMgr   Pointer to the extension pack manager.
 * @param   a_pVirtualBox   Pointer to the VirtualBox object.
 */
HRESULT ExtPackFile::initWithFile(const char *a_pszFile, const char *a_pszDigest, ExtPackManager *a_pExtPackMgr,
                                  VirtualBox *a_pVirtualBox)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /*
     * Allocate + initialize our private data.
     */
    m = new ExtPackFile::Data;
    VBoxExtPackInitDesc(&m->Desc);
    RT_ZERO(m->ObjInfoDesc);
    m->fUsable                      = false;
    m->strWhyUnusable               = tr("ExtPack::init failed");
    m->strExtPackFile               = a_pszFile;
    m->strDigest                    = a_pszDigest;
    m->hExtPackFile                 = NIL_RTFILE;
    m->hOurManifest                 = NIL_RTMANIFEST;
    m->ptrExtPackMgr                = a_pExtPackMgr;
    m->pVirtualBox                  = a_pVirtualBox;

    RTCString *pstrTarName = VBoxExtPackExtractNameFromTarballPath(a_pszFile);
    if (pstrTarName)
    {
        m->Desc.strName = *pstrTarName;
        delete pstrTarName;
        pstrTarName = NULL;
    }

    autoInitSpan.setSucceeded();

    /*
     * Try open the extension pack and check that it is a regular file.
     */
    int vrc = RTFileOpen(&m->hExtPackFile, a_pszFile,
                         RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN);
    if (RT_FAILURE(vrc))
    {
        if (vrc == VERR_FILE_NOT_FOUND || vrc == VERR_PATH_NOT_FOUND)
            return initFailed(tr("'%s' file not found"), a_pszFile);
        return initFailed(tr("RTFileOpen('%s',,) failed with %Rrc"), a_pszFile, vrc);
    }

    RTFSOBJINFO ObjInfo;
    vrc = RTFileQueryInfo(m->hExtPackFile, &ObjInfo, RTFSOBJATTRADD_UNIX);
    if (RT_FAILURE(vrc))
        return initFailed(tr("RTFileQueryInfo failed with %Rrc on '%s'"), vrc, a_pszFile);
    if (!RTFS_IS_FILE(ObjInfo.Attr.fMode))
        return initFailed(tr("Not a regular file: %s"), a_pszFile);

    /*
     * Validate the tarball and extract the XML file.
     */
    char        szError[8192];
    RTVFSFILE   hXmlFile;
    vrc = VBoxExtPackValidateTarball(m->hExtPackFile, NULL /*pszExtPackName*/, a_pszFile, a_pszDigest,
                                     szError, sizeof(szError), &m->hOurManifest, &hXmlFile, &m->strDigest);
    if (RT_FAILURE(vrc))
        return initFailed("%s", szError);

    /*
     * Parse the XML.
     */
    RTCString strSavedName(m->Desc.strName);
    RTCString *pStrLoadErr = VBoxExtPackLoadDescFromVfsFile(hXmlFile, &m->Desc, &m->ObjInfoDesc);
    RTVfsFileRelease(hXmlFile);
    if (pStrLoadErr != NULL)
    {
        m->strWhyUnusable.printf(tr("Failed to the xml file: %s"), pStrLoadErr->c_str());
        m->Desc.strName = strSavedName;
        delete pStrLoadErr;
        return S_OK;
    }

    /*
     * Match the tarball name with the name from the XML.
     */
    /** @todo drop this restriction after the old install interface is
     *        dropped. */
    if (!strSavedName.equalsIgnoreCase(m->Desc.strName))
        return initFailed(tr("Extension pack name mismatch between the downloaded file and the XML inside it (xml='%s' file='%s')"),
                          m->Desc.strName.c_str(), strSavedName.c_str());


    m->fUsable = true;
    m->strWhyUnusable.setNull();
    return S_OK;
}

/**
 * Protected helper that formats the strWhyUnusable value.
 *
 * @returns S_OK
 * @param   a_pszWhyFmt         Why it failed, format string.
 * @param   ...                 The format arguments.
 */
HRESULT ExtPackFile::initFailed(const char *a_pszWhyFmt, ...)
{
    va_list va;
    va_start(va, a_pszWhyFmt);
    m->strWhyUnusable.printfV(a_pszWhyFmt, va);
    va_end(va);
    return S_OK;
}

/**
 * COM cruft.
 */
void ExtPackFile::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

/**
 * Do the actual cleanup.
 */
void ExtPackFile::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (!autoUninitSpan.uninitDone() && m != NULL)
    {
        VBoxExtPackFreeDesc(&m->Desc);
        RTFileClose(m->hExtPackFile);
        m->hExtPackFile = NIL_RTFILE;
        RTManifestRelease(m->hOurManifest);
        m->hOurManifest = NIL_RTMANIFEST;

        delete m;
        m = NULL;
    }
}

HRESULT ExtPackFile::getName(com::Utf8Str &aName)
{
    aName = m->Desc.strName;
    return S_OK;
}

HRESULT ExtPackFile::getDescription(com::Utf8Str &aDescription)
{
    aDescription = m->Desc.strDescription;
    return S_OK;
}

HRESULT ExtPackFile::getVersion(com::Utf8Str &aVersion)
{
    aVersion = m->Desc.strVersion;
    return S_OK;
}

HRESULT ExtPackFile::getEdition(com::Utf8Str &aEdition)
{
    aEdition = m->Desc.strEdition;
    return S_OK;
}

HRESULT ExtPackFile::getRevision(ULONG *aRevision)
{
    *aRevision = m->Desc.uRevision;
    return S_OK;
}

HRESULT ExtPackFile::getVRDEModule(com::Utf8Str &aVRDEModule)
{
    aVRDEModule = m->Desc.strVrdeModule;
    return S_OK;
}

HRESULT ExtPackFile::getCryptoModule(com::Utf8Str &aCryptoModule)
{
    aCryptoModule = m->Desc.strCryptoModule;
    return S_OK;
}

HRESULT ExtPackFile::getPlugIns(std::vector<ComPtr<IExtPackPlugIn> > &aPlugIns)
{
    /** @todo implement plug-ins. */
#ifdef VBOX_WITH_XPCOM
    NOREF(aPlugIns);
#endif
    NOREF(aPlugIns);
    ReturnComNotImplemented();
}

HRESULT ExtPackFile::getUsable(BOOL *aUsable)
{
    *aUsable = m->fUsable;
    return S_OK;
}

HRESULT ExtPackFile::getWhyUnusable(com::Utf8Str &aWhyUnusable)
{
    aWhyUnusable = m->strWhyUnusable;
    return S_OK;
}

HRESULT  ExtPackFile::getShowLicense(BOOL *aShowLicense)
{
    *aShowLicense = m->Desc.fShowLicense;
    return S_OK;
}

HRESULT ExtPackFile::getLicense(com::Utf8Str &aLicense)
{
    Utf8Str strHtml("html");
    Utf8Str str("");
    return queryLicense(str, str, strHtml, aLicense);
}

/* Same as ExtPack::QueryLicense, should really explore the subject of base classes here... */
HRESULT ExtPackFile::queryLicense(const com::Utf8Str &aPreferredLocale, const com::Utf8Str &aPreferredLanguage,
                                  const com::Utf8Str &aFormat, com::Utf8Str &aLicenseText)
{
    HRESULT hrc = S_OK;

    /*
     * Validate input.
     */

    if (aPreferredLocale.length() != 2 && aPreferredLocale.length() != 0)
        return setError(E_FAIL, tr("The preferred locale is a two character string or empty."));

    if (aPreferredLanguage.length() != 2 && aPreferredLanguage.length() != 0)
        return setError(E_FAIL, tr("The preferred language is a two character string or empty."));

    if (   !aFormat.equals("html")
        && !aFormat.equals("rtf")
        && !aFormat.equals("txt"))
        return setError(E_FAIL, tr("The license format can only have the values 'html', 'rtf' and 'txt'."));

    /*
     * Combine the options to form a file name before locking down anything.
     */
    char szName[sizeof(VBOX_EXTPACK_LICENSE_NAME_PREFIX "-de_DE.html") + 2];
    if (aPreferredLocale.isNotEmpty() && aPreferredLanguage.isNotEmpty())
        RTStrPrintf(szName, sizeof(szName), VBOX_EXTPACK_LICENSE_NAME_PREFIX "-%s_%s.%s",
                    aPreferredLocale.c_str(), aPreferredLanguage.c_str(), aFormat.c_str());
    else if (aPreferredLocale.isNotEmpty())
        RTStrPrintf(szName, sizeof(szName), VBOX_EXTPACK_LICENSE_NAME_PREFIX "-%s.%s",
                    aPreferredLocale.c_str(), aFormat.c_str());
    else if (aPreferredLanguage.isNotEmpty())
        RTStrPrintf(szName, sizeof(szName), VBOX_EXTPACK_LICENSE_NAME_PREFIX "-_%s.%s",
                    aPreferredLocale.c_str(), aFormat.c_str());
    else
        RTStrPrintf(szName, sizeof(szName), VBOX_EXTPACK_LICENSE_NAME_PREFIX ".%s",
                    aFormat.c_str());
    /*
     * Lock the extension pack. We need a write lock here as there must not be
     * concurrent accesses to the tar file handle.
     */
    AutoWriteLock autoLock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Do not permit this query on a pack that isn't considered usable (could
     * be marked so because of bad license files).
     */
    if (!m->fUsable)
        hrc = setError(E_FAIL, "%s", m->strWhyUnusable.c_str());
    else
    {
        /*
         * Look it up in the manifest before scanning the tarball for it
         */
        if (RTManifestEntryExists(m->hOurManifest, szName))
        {
            RTVFSFSSTREAM   hTarFss;
            char            szError[8192];
            int vrc = VBoxExtPackOpenTarFss(m->hExtPackFile, szError, sizeof(szError), &hTarFss, NULL);
            if (RT_SUCCESS(vrc))
            {
                for (;;)
                {
                    /* Get the first/next. */
                    char           *pszName;
                    RTVFSOBJ        hVfsObj;
                    RTVFSOBJTYPE    enmType;
                    vrc = RTVfsFsStrmNext(hTarFss, &pszName, &enmType, &hVfsObj);
                    if (RT_FAILURE(vrc))
                    {
                        if (vrc != VERR_EOF)
                            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("RTVfsFsStrmNext failed: %Rrc"), vrc);
                        else
                            hrc = setErrorBoth(E_UNEXPECTED, vrc, tr("'%s' was found in the manifest but not in the tarball"), szName);
                        break;
                    }

                    /* Is this it? */
                    const char *pszAdjName = pszName[0] == '.' && pszName[1] == '/' ? &pszName[2] : pszName;
                    if (   !strcmp(pszAdjName, szName)
                        && (   enmType == RTVFSOBJTYPE_IO_STREAM
                            || enmType == RTVFSOBJTYPE_FILE))
                    {
                        RTVFSIOSTREAM hVfsIos = RTVfsObjToIoStream(hVfsObj);
                        RTVfsObjRelease(hVfsObj);
                        RTStrFree(pszName);

                        /* Load the file into memory. */
                        RTFSOBJINFO ObjInfo;
                        vrc = RTVfsIoStrmQueryInfo(hVfsIos, &ObjInfo, RTFSOBJATTRADD_NOTHING);
                        if (RT_SUCCESS(vrc))
                        {
                            size_t cbFile = (size_t)ObjInfo.cbObject;
                            void  *pvFile = RTMemAllocZ(cbFile + 1);
                            if (pvFile)
                            {
                                vrc = RTVfsIoStrmRead(hVfsIos, pvFile, cbFile, true /*fBlocking*/, NULL);
                                if (RT_SUCCESS(vrc))
                                {
                                    /* try translate it into a string we can return. */
                                    Bstr bstrLicense((const char *)pvFile, cbFile);
                                    if (bstrLicense.isNotEmpty())
                                    {
                                        aLicenseText = Utf8Str(bstrLicense);
                                        hrc = S_OK;
                                    }
                                    else
                                        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                                           tr("The license file '%s' is empty or contains invalid UTF-8 encoding"),
                                                           szName);
                                }
                                else
                                    hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Failed to read '%s': %Rrc"), szName, vrc);
                                RTMemFree(pvFile);
                            }
                            else
                                hrc = setError(E_OUTOFMEMORY, tr("Failed to allocate %zu bytes for '%s'", "", cbFile),
                                               cbFile, szName);
                        }
                        else
                            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("RTVfsIoStrmQueryInfo on '%s': %Rrc"), szName, vrc);
                        RTVfsIoStrmRelease(hVfsIos);
                        break;
                    }

                    /* Release current. */
                    RTVfsObjRelease(hVfsObj);
                    RTStrFree(pszName);
                }
                RTVfsFsStrmRelease(hTarFss);
            }
            else
                hrc = setError(VBOX_E_OBJECT_NOT_FOUND, "%s", szError);
        }
        else
            hrc = setError(VBOX_E_OBJECT_NOT_FOUND, tr("The license file '%s' was not found in '%s'"),
                           szName, m->strExtPackFile.c_str());
    }
    return hrc;
}

HRESULT ExtPackFile::getFilePath(com::Utf8Str &aFilePath)
{

    aFilePath = m->strExtPackFile;
    return S_OK;
}

HRESULT ExtPackFile::install(BOOL aReplace, const com::Utf8Str &aDisplayInfo, ComPtr<IProgress> &aProgress)
{
    HRESULT hrc;
    if (m->fUsable)
    {
        ExtPackInstallTask *pTask = NULL;
        try
        {
            pTask = new ExtPackInstallTask();
            hrc = pTask->Init(this, aReplace != FALSE, aDisplayInfo, m->ptrExtPackMgr);
            if (SUCCEEDED(hrc))
            {
                ComPtr<Progress> ptrProgress = pTask->ptrProgress;
                hrc = pTask->createThreadWithType(RTTHREADTYPE_DEFAULT);
                pTask = NULL; /* The _completely_ _undocumented_ createThread method always consumes pTask. */
                if (SUCCEEDED(hrc))
                    hrc = ptrProgress.queryInterfaceTo(aProgress.asOutParam());
                else
                    hrc = setError(VBOX_E_IPRT_ERROR,
                                  tr("Starting thread for an extension pack installation failed with %Rrc"), hrc);
            }
            else
                hrc = setError(VBOX_E_IPRT_ERROR,
                              tr("Looks like creating a progress object for ExtraPackInstallTask object failed"));
        }
        catch (std::bad_alloc &)
        {
            hrc = E_OUTOFMEMORY;
        }
        catch (HRESULT hrcXcpt)
        {
            LogFlowThisFunc(("Exception was caught in the function ExtPackFile::install() \n"));
            hrc = hrcXcpt;
        }
        if (pTask)
            delete pTask;
    }
    else
        hrc = setError(E_FAIL, "%s", m->strWhyUnusable.c_str());
    return hrc;
}

#endif /* !VBOX_COM_INPROC */




DEFINE_EMPTY_CTOR_DTOR(ExtPack)

/**
 * Called by ComObjPtr::createObject when creating the object.
 *
 * Just initialize the basic object state, do the rest in initWithDir().
 *
 * @returns S_OK.
 */
HRESULT ExtPack::FinalConstruct()
{
    m = NULL;
    return BaseFinalConstruct();
}

/**
 * Initializes the extension pack by reading its file.
 *
 * @returns COM status code.
 * @param   a_pVirtualBox   The VirtualBox object.
 * @param   a_enmContext    The context we're in.
 * @param   a_pszName       The name of the extension pack.  This is also the
 *                          name of the subdirector under @a a_pszParentDir
 *                          where the extension pack is installed.
 * @param   a_pszDir        The extension pack directory name.
 */
HRESULT ExtPack::initWithDir(VirtualBox *a_pVirtualBox, VBOXEXTPACKCTX a_enmContext, const char *a_pszName, const char *a_pszDir)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    static const VBOXEXTPACKHLP s_HlpTmpl =
    {
        /* u32Version           = */ VBOXEXTPACKHLP_VERSION,
        /* uVBoxFullVersion     = */ VBOX_FULL_VERSION,
        /* uVBoxVersionRevision = */ 0,
        /* u32Padding           = */ 0,
        /* pszVBoxVersion       = */ "",
        /* pfnFindModule        = */ ExtPack::i_hlpFindModule,
        /* pfnGetFilePath       = */ ExtPack::i_hlpGetFilePath,
        /* pfnGetContext        = */ ExtPack::i_hlpGetContext,
        /* pfnLoadHGCMService   = */ ExtPack::i_hlpLoadHGCMService,
        /* pfnLoadVDPlugin      = */ ExtPack::i_hlpLoadVDPlugin,
        /* pfnUnloadVDPlugin    = */ ExtPack::i_hlpUnloadVDPlugin,
        /* pfnCreateProgress    = */ ExtPack::i_hlpCreateProgress,
        /* pfnGetCanceledProgress = */ ExtPack::i_hlpGetCanceledProgress,
        /* pfnUpdateProgress    = */ ExtPack::i_hlpUpdateProgress,
        /* pfnNextOperationProgress = */ ExtPack::i_hlpNextOperationProgress,
        /* pfnWaitOtherProgress = */ ExtPack::i_hlpWaitOtherProgress,
        /* pfnCompleteProgress  = */ ExtPack::i_hlpCompleteProgress,
        /* pfnCreateEvent       = */ ExtPack::i_hlpCreateEvent,
        /* pfnCreateVetoEvent   = */ ExtPack::i_hlpCreateVetoEvent,
        /* pfnTranslate         = */ ExtPack::i_hlpTranslate,
        /* pfnReserved1         = */ ExtPack::i_hlpReservedN,
        /* pfnReserved2         = */ ExtPack::i_hlpReservedN,
        /* pfnReserved3         = */ ExtPack::i_hlpReservedN,
        /* pfnReserved4         = */ ExtPack::i_hlpReservedN,
        /* pfnReserved5         = */ ExtPack::i_hlpReservedN,
        /* pfnReserved6         = */ ExtPack::i_hlpReservedN,
        /* uReserved7           = */ 0,
        /* u32EndMarker         = */ VBOXEXTPACKHLP_VERSION
    };

    /*
     * Allocate + initialize our private data.
     */
    m = new Data;
    VBoxExtPackInitDesc(&m->Desc);
    m->Desc.strName                 = a_pszName;
    RT_ZERO(m->ObjInfoDesc);
    m->fUsable                      = false;
    m->strWhyUnusable               = tr("ExtPack::init failed");
    m->strExtPackPath               = a_pszDir;
    RT_ZERO(m->ObjInfoExtPack);
    m->strMainModPath.setNull();
    RT_ZERO(m->ObjInfoMainMod);
    m->hMainMod                     = NIL_RTLDRMOD;
    m->Hlp                          = s_HlpTmpl;
    m->Hlp.pszVBoxVersion           = RTBldCfgVersion();
    m->Hlp.uVBoxInternalRevision    = RTBldCfgRevision();
    m->pThis                        = this;
    m->pReg                         = NULL;
    m->enmContext                   = a_enmContext;
    m->fMadeReadyCall               = false;
#ifndef VBOX_COM_INPROC
    m->pVirtualBox                  = a_pVirtualBox;
#else
    RT_NOREF(a_pVirtualBox);
#endif
#ifdef VBOX_WITH_MAIN_NLS
    m->pTrComponent                 = NULL;
#endif
    /*
     * Make sure the SUPR3Hardened API works (ignoring errors for now).
     */
    int vrc = SUPR3HardenedVerifyInit();
    if (RT_FAILURE(vrc))
        LogRel(("SUPR3HardenedVerifyInit failed: %Rrc\n", vrc));

    /*
     * Probe the extension pack (this code is shared with refresh()).
     */
    i_probeAndLoad();

#ifdef VBOX_WITH_MAIN_NLS
    /* register language files if exist */
    if (m->pReg != NULL && m->pReg->pszNlsBaseName != NULL)
    {
        char szPath[RTPATH_MAX];
        vrc = RTPathJoin(szPath, sizeof(szPath), a_pszDir, "nls");
        if (RT_SUCCESS(vrc))
        {
            vrc = RTPathAppend(szPath, sizeof(szPath), m->pReg->pszNlsBaseName);
            if (RT_SUCCESS(vrc))
            {
                vrc = VirtualBoxTranslator::registerTranslation(szPath, false, &m->pTrComponent);
                if (RT_FAILURE(vrc))
                    m->pTrComponent = NULL;
            }
        }
    }
#endif

    autoInitSpan.setSucceeded();
    return S_OK;
}

/**
 * COM cruft.
 */
void ExtPack::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

/**
 * Do the actual cleanup.
 */
void ExtPack::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (!autoUninitSpan.uninitDone() && m != NULL)
    {
        if (m->hMainMod != NIL_RTLDRMOD)
        {
            AssertPtr(m->pReg);
            if (m->pReg->pfnUnload != NULL)
                m->pReg->pfnUnload(m->pReg);

            RTLdrClose(m->hMainMod);
            m->hMainMod = NIL_RTLDRMOD;
            m->pReg = NULL;
        }

        VBoxExtPackFreeDesc(&m->Desc);

#ifdef VBOX_WITH_MAIN_NLS
        if (m->pTrComponent != NULL)
            VirtualBoxTranslator::unregisterTranslation(m->pTrComponent);
#endif
        delete m;
        m = NULL;
    }
}


#ifndef VBOX_COM_INPROC
/**
 * Calls the installed hook.
 *
 * @returns true if we left the lock, false if we didn't.
 * @param   a_pVirtualBox       The VirtualBox interface.
 * @param   a_pLock             The write lock held by the caller.
 * @param   pErrInfo            Where to return error information.
 */
bool    ExtPack::i_callInstalledHook(IVirtualBox *a_pVirtualBox, AutoWriteLock *a_pLock, PRTERRINFO pErrInfo)
{
    if (   m != NULL
        && m->hMainMod != NIL_RTLDRMOD)
    {
        if (m->pReg->pfnInstalled)
        {
            ComPtr<ExtPack> ptrSelfRef = this;
            a_pLock->release();
            pErrInfo->rc = m->pReg->pfnInstalled(m->pReg, a_pVirtualBox, pErrInfo);
            a_pLock->acquire();
            return true;
        }
    }
    pErrInfo->rc = VINF_SUCCESS;
    return false;
}

/**
 * Calls the uninstall hook and closes the module.
 *
 * @returns S_OK or COM error status with error information.
 * @param   a_pVirtualBox       The VirtualBox interface.
 * @param   a_fForcedRemoval    When set, we'll ignore complaints from the
 *                              uninstall hook.
 * @remarks The caller holds the manager's write lock, not released.
 */
HRESULT ExtPack::i_callUninstallHookAndClose(IVirtualBox *a_pVirtualBox, bool a_fForcedRemoval)
{
    HRESULT hrc = S_OK;

    if (   m != NULL
        && m->hMainMod != NIL_RTLDRMOD)
    {
        if (m->pReg->pfnUninstall && !a_fForcedRemoval)
        {
            int vrc = m->pReg->pfnUninstall(m->pReg, a_pVirtualBox);
            if (RT_FAILURE(vrc))
            {
                LogRel(("ExtPack pfnUninstall returned %Rrc for %s\n", vrc, m->Desc.strName.c_str()));
                if (!a_fForcedRemoval)
                    hrc = setErrorBoth(E_FAIL, vrc, tr("pfnUninstall returned %Rrc"), vrc);
            }
        }
        if (SUCCEEDED(hrc))
        {
            RTLdrClose(m->hMainMod);
            m->hMainMod = NIL_RTLDRMOD;
            m->pReg = NULL;
        }
    }

    return hrc;
}

/**
 * Calls the pfnVirtualBoxReady hook.
 *
 * @returns true if we left the lock, false if we didn't.
 * @param   a_pVirtualBox       The VirtualBox interface.
 * @param   a_pLock             The write lock held by the caller.
 */
bool ExtPack::i_callVirtualBoxReadyHook(IVirtualBox *a_pVirtualBox, AutoWriteLock *a_pLock)
{
    if (    m != NULL
        &&  m->fUsable
        &&  m->hMainMod != NIL_RTLDRMOD
        && !m->fMadeReadyCall)
    {
        m->fMadeReadyCall = true;
        if (m->pReg->pfnVirtualBoxReady)
        {
            ComPtr<ExtPack> ptrSelfRef = this;
            a_pLock->release();
            m->pReg->pfnVirtualBoxReady(m->pReg, a_pVirtualBox);
            i_notifyCloudProviderManager();
            a_pLock->acquire();
            return true;
        }
    }
    return false;
}
#endif /* !VBOX_COM_INPROC */

#ifdef VBOX_COM_INPROC
/**
 * Calls the pfnConsoleReady hook.
 *
 * @returns true if we left the lock, false if we didn't.
 * @param   a_pConsole          The Console interface.
 * @param   a_pLock             The write lock held by the caller.
 */
bool ExtPack::i_callConsoleReadyHook(IConsole *a_pConsole, AutoWriteLock *a_pLock)
{
    if (    m != NULL
        &&  m->fUsable
        &&  m->hMainMod != NIL_RTLDRMOD
        && !m->fMadeReadyCall)
    {
        m->fMadeReadyCall = true;
        if (m->pReg->pfnConsoleReady)
        {
            ComPtr<ExtPack> ptrSelfRef = this;
            a_pLock->release();
            m->pReg->pfnConsoleReady(m->pReg, a_pConsole);
            a_pLock->acquire();
            return true;
        }
    }
    return false;
}
#endif /* VBOX_COM_INPROC */

#ifndef VBOX_COM_INPROC
/**
 * Calls the pfnVMCreate hook.
 *
 * @returns true if we left the lock, false if we didn't.
 * @param   a_pVirtualBox       The VirtualBox interface.
 * @param   a_pMachine          The machine interface of the new VM.
 * @param   a_pLock             The write lock held by the caller.
 */
bool ExtPack::i_callVmCreatedHook(IVirtualBox *a_pVirtualBox, IMachine *a_pMachine, AutoWriteLock *a_pLock)
{
    if (   m != NULL
        && m->hMainMod != NIL_RTLDRMOD
        && m->fUsable)
    {
        if (m->pReg->pfnVMCreated)
        {
            ComPtr<ExtPack> ptrSelfRef = this;
            a_pLock->release();
            m->pReg->pfnVMCreated(m->pReg, a_pVirtualBox, a_pMachine);
            a_pLock->acquire();
            return true;
        }
    }
    return false;
}
#endif /* !VBOX_COM_INPROC */

#ifdef VBOX_COM_INPROC

/**
 * Calls the pfnVMConfigureVMM hook.
 *
 * @returns true if we left the lock, false if we didn't.
 * @param   a_pConsole  The console interface.
 * @param   a_pVM       The VM handle.
 * @param   a_pVMM      The VMM function table.
 * @param   a_pLock     The write lock held by the caller.
 * @param   a_pvrc      Where to return the status code of the callback.  This
 *                      is always set.  LogRel is called on if a failure status
 *                      is returned.
 */
bool ExtPack::i_callVmConfigureVmmHook(IConsole *a_pConsole, PVM a_pVM, PCVMMR3VTABLE a_pVMM, AutoWriteLock *a_pLock, int *a_pvrc)
{
    *a_pvrc = VINF_SUCCESS;
    if (   m != NULL
        && m->hMainMod != NIL_RTLDRMOD
        && m->fUsable)
    {
        if (m->pReg->pfnVMConfigureVMM)
        {
            ComPtr<ExtPack> ptrSelfRef = this;
            a_pLock->release();
            int vrc = m->pReg->pfnVMConfigureVMM(m->pReg, a_pConsole, a_pVM, a_pVMM);
            *a_pvrc = vrc;
            a_pLock->acquire();
            if (RT_FAILURE(vrc))
                LogRel(("ExtPack pfnVMConfigureVMM returned %Rrc for %s\n", vrc, m->Desc.strName.c_str()));
            return true;
        }
    }
    return false;
}

/**
 * Calls the pfnVMPowerOn hook.
 *
 * @returns true if we left the lock, false if we didn't.
 * @param   a_pConsole  The console interface.
 * @param   a_pVM       The VM handle.
 * @param   a_pVMM      The VMM function table.
 * @param   a_pLock     The write lock held by the caller.
 * @param   a_pvrc      Where to return the status code of the callback.  This
 *                      is always set.  LogRel is called on if a failure status
 *                      is returned.
 */
bool ExtPack::i_callVmPowerOnHook(IConsole *a_pConsole, PVM a_pVM, PCVMMR3VTABLE a_pVMM, AutoWriteLock *a_pLock, int *a_pvrc)
{
    *a_pvrc = VINF_SUCCESS;
    if (   m != NULL
        && m->hMainMod != NIL_RTLDRMOD
        && m->fUsable)
    {
        if (m->pReg->pfnVMPowerOn)
        {
            ComPtr<ExtPack> ptrSelfRef = this;
            a_pLock->release();
            int vrc = m->pReg->pfnVMPowerOn(m->pReg, a_pConsole, a_pVM, a_pVMM);
            *a_pvrc = vrc;
            a_pLock->acquire();
            if (RT_FAILURE(vrc))
                LogRel(("ExtPack pfnVMPowerOn returned %Rrc for %s\n", vrc, m->Desc.strName.c_str()));
            return true;
        }
    }
    return false;
}

/**
 * Calls the pfnVMPowerOff hook.
 *
 * @returns true if we left the lock, false if we didn't.
 * @param   a_pConsole  The console interface.
 * @param   a_pVM       The VM handle.
 * @param   a_pVMM      The VMM function table.
 * @param   a_pLock     The write lock held by the caller.
 */
bool ExtPack::i_callVmPowerOffHook(IConsole *a_pConsole, PVM a_pVM, PCVMMR3VTABLE a_pVMM, AutoWriteLock *a_pLock)
{
    if (   m != NULL
        && m->hMainMod != NIL_RTLDRMOD
        && m->fUsable)
    {
        if (m->pReg->pfnVMPowerOff)
        {
            ComPtr<ExtPack> ptrSelfRef = this;
            a_pLock->release();
            m->pReg->pfnVMPowerOff(m->pReg, a_pConsole, a_pVM, a_pVMM);
            a_pLock->acquire();
            return true;
        }
    }
    return false;
}

#endif /* VBOX_COM_INPROC */

/**
 * Check if the extension pack is usable and has an VRDE module.
 *
 * @returns S_OK or COM error status with error information.
 *
 * @remarks Caller holds the extension manager lock for reading, no locking
 *          necessary.
 */
HRESULT ExtPack::i_checkVrde(void)
{
    HRESULT hrc;
    if (   m != NULL
        && m->fUsable)
    {
        if (m->Desc.strVrdeModule.isNotEmpty())
            hrc = S_OK;
        else
            hrc = setError(E_FAIL, tr("The extension pack '%s' does not include a VRDE module"), m->Desc.strName.c_str());
    }
    else
        hrc = setError(E_FAIL, "%s", m->strWhyUnusable.c_str());
    return hrc;
}

/**
 * Check if the extension pack is usable and has a cryptographic module.
 *
 * @returns S_OK or COM error status with error information.
 *
 * @remarks Caller holds the extension manager lock for reading, no locking
 *          necessary.
 */
HRESULT ExtPack::i_checkCrypto(void)
{
    HRESULT hrc;
    if (   m != NULL
        && m->fUsable)
    {
        if (m->Desc.strCryptoModule.isNotEmpty())
            hrc = S_OK;
        else
            hrc = setError(E_FAIL, tr("The extension pack '%s' does not include a cryptographic module"), m->Desc.strName.c_str());
    }
    else
        hrc = setError(E_FAIL, "%s", m->strWhyUnusable.c_str());
    return hrc;
}

/**
 * Same as checkVrde(), except that it also resolves the path to the module.
 *
 * @returns S_OK or COM error status with error information.
 * @param   a_pstrVrdeLibrary  Where to return the path on success.
 *
 * @remarks Caller holds the extension manager lock for reading, no locking
 *          necessary.
 */
HRESULT ExtPack::i_getVrdpLibraryName(Utf8Str *a_pstrVrdeLibrary)
{
    HRESULT hrc = i_checkVrde();
    if (SUCCEEDED(hrc))
    {
        if (i_findModule(m->Desc.strVrdeModule.c_str(), NULL, VBOXEXTPACKMODKIND_R3,
                         a_pstrVrdeLibrary, NULL /*a_pfNative*/, NULL /*a_pObjInfo*/))
            hrc = S_OK;
        else
            hrc = setError(E_FAIL, tr("Failed to locate the VRDE module '%s' in extension pack '%s'"),
                           m->Desc.strVrdeModule.c_str(), m->Desc.strName.c_str());
    }
    return hrc;
}

/**
 * Same as i_checkCrypto(), except that it also resolves the path to the module.
 *
 * @returns S_OK or COM error status with error information.
 * @param   a_pstrCryptoLibrary  Where to return the path on success.
 *
 * @remarks Caller holds the extension manager lock for reading, no locking
 *          necessary.
 */
HRESULT ExtPack::i_getCryptoLibraryName(Utf8Str *a_pstrCryptoLibrary)
{
    HRESULT hrc = i_checkCrypto();
    if (SUCCEEDED(hrc))
    {
        if (i_findModule(m->Desc.strCryptoModule.c_str(), NULL, VBOXEXTPACKMODKIND_R3,
                         a_pstrCryptoLibrary, NULL /*a_pfNative*/, NULL /*a_pObjInfo*/))
            hrc = S_OK;
        else
            hrc = setError(E_FAIL, tr("Failed to locate the cryptographic module '%s' in extension pack '%s'"),
                           m->Desc.strCryptoModule.c_str(), m->Desc.strName.c_str());
    }
    return hrc;
}

/**
 * Resolves the path to the module.
 *
 * @returns S_OK or COM error status with error information.
 * @param   a_pszModuleName  The library.
 * @param   a_pstrLibrary  Where to return the path on success.
 *
 * @remarks Caller holds the extension manager lock for reading, no locking
 *          necessary.
 */
HRESULT ExtPack::i_getLibraryName(const char *a_pszModuleName, Utf8Str *a_pstrLibrary)
{
    HRESULT hrc;
    if (i_findModule(a_pszModuleName, NULL, VBOXEXTPACKMODKIND_R3,
                     a_pstrLibrary, NULL /*a_pfNative*/, NULL /*a_pObjInfo*/))
        hrc = S_OK;
    else
        hrc = setError(E_FAIL, tr("Failed to locate the module '%s' in extension pack '%s'"),
                       a_pszModuleName, m->Desc.strName.c_str());
    return hrc;
}

/**
 * Check if this extension pack wishes to be the default VRDE provider.
 *
 * @returns @c true if it wants to and it is in a usable state, otherwise
 *          @c false.
 *
 * @remarks Caller holds the extension manager lock for reading, no locking
 *          necessary.
 */
bool ExtPack::i_wantsToBeDefaultVrde(void) const
{
    return m->fUsable
        && m->Desc.strVrdeModule.isNotEmpty();
}

/**
 * Check if this extension pack wishes to be the default cryptographic provider.
 *
 * @returns @c true if it wants to and it is in a usable state, otherwise
 *          @c false.
 *
 * @remarks Caller holds the extension manager lock for reading, no locking
 *          necessary.
 */
bool ExtPack::i_wantsToBeDefaultCrypto(void) const
{
    return m->fUsable
        && m->Desc.strCryptoModule.isNotEmpty();
}

/**
 * Refreshes the extension pack state.
 *
 * This is called by the manager so that the on disk changes are picked up.
 *
 * @returns S_OK or COM error status with error information.
 *
 * @param   a_pfCanDelete       Optional can-delete-this-object output indicator.
 *
 * @remarks Caller holds the extension manager lock for writing.
 * @remarks Only called in VBoxSVC.
 */
HRESULT ExtPack::i_refresh(bool *a_pfCanDelete)
{
    if (a_pfCanDelete)
        *a_pfCanDelete = false;

    AutoWriteLock autoLock(this COMMA_LOCKVAL_SRC_POS); /* for the COMGETTERs */

    /*
     * Has the module been deleted?
     */
    RTFSOBJINFO ObjInfoExtPack;
    int vrc = RTPathQueryInfoEx(m->strExtPackPath.c_str(), &ObjInfoExtPack, RTFSOBJATTRADD_UNIX, RTPATH_F_ON_LINK);
    if (   RT_FAILURE(vrc)
        || !RTFS_IS_DIRECTORY(ObjInfoExtPack.Attr.fMode))
    {
        if (a_pfCanDelete)
            *a_pfCanDelete = true;
        return S_OK;
    }

    /*
     * We've got a directory, so try query file system object info for the
     * files we are interested in as well.
     */
    RTFSOBJINFO ObjInfoDesc;
    char        szDescFilePath[RTPATH_MAX];
    vrc = RTPathJoin(szDescFilePath, sizeof(szDescFilePath), m->strExtPackPath.c_str(), VBOX_EXTPACK_DESCRIPTION_NAME);
    if (RT_SUCCESS(vrc))
        vrc = RTPathQueryInfoEx(szDescFilePath, &ObjInfoDesc, RTFSOBJATTRADD_UNIX, RTPATH_F_ON_LINK);
    if (RT_FAILURE(vrc))
        RT_ZERO(ObjInfoDesc);

    RTFSOBJINFO ObjInfoMainMod;
    if (m->strMainModPath.isNotEmpty())
        vrc = RTPathQueryInfoEx(m->strMainModPath.c_str(), &ObjInfoMainMod, RTFSOBJATTRADD_UNIX, RTPATH_F_ON_LINK);
    if (m->strMainModPath.isEmpty() || RT_FAILURE(vrc))
        RT_ZERO(ObjInfoMainMod);

    /*
     * If we have a usable module already, just verify that things haven't
     * changed since we loaded it.
     */
    if (m->fUsable)
    {
        if (m->hMainMod == NIL_RTLDRMOD)
            i_probeAndLoad();
        else if (   !i_objinfoIsEqual(&ObjInfoDesc,    &m->ObjInfoDesc)
                 || !i_objinfoIsEqual(&ObjInfoMainMod, &m->ObjInfoMainMod)
                 || !i_objinfoIsEqual(&ObjInfoExtPack, &m->ObjInfoExtPack) )
        {
            /** @todo not important, so it can wait. */
        }
    }
    /*
     * Ok, it is currently not usable.  If anything has changed since last time
     * reprobe the extension pack.
     */
    else if (   !i_objinfoIsEqual(&ObjInfoDesc,    &m->ObjInfoDesc)
             || !i_objinfoIsEqual(&ObjInfoMainMod, &m->ObjInfoMainMod)
             || !i_objinfoIsEqual(&ObjInfoExtPack, &m->ObjInfoExtPack) )
        i_probeAndLoad();

    return S_OK;
}

#ifndef VBOX_COM_INPROC
/**
 * Checks if there are cloud providers vetoing extension pack uninstall.
 *
 * This needs going through the cloud provider singleton in VirtualBox,
 * the job cannot be done purely by using the code in the extension pack).
 * It cannot be integrated into i_callUninstallHookAndClose, because it
 * can only do its job when the extpack lock is not held, whereas the
 * actual uninstall must be done with the lock held all the time for
 * consistency reasons.
 *
 * This is called when uninstalling or replacing an extension pack.
 *
 * @returns true / false
 */
bool ExtPack::i_areThereCloudProviderUninstallVetos()
{
    Assert(m->pVirtualBox != NULL); /* Only called from VBoxSVC. */

    ComObjPtr<CloudProviderManager> cpm(m->pVirtualBox->i_getCloudProviderManager());
    AssertReturn(!cpm.isNull(), false);

    return !cpm->i_canRemoveExtPack(static_cast<IExtPack *>(this));
}

/**
 * Notifies the Cloud Provider Manager that there is a new extension pack.
 *
 * This is called when installing an extension pack.
 */
void ExtPack::i_notifyCloudProviderManager()
{
    Assert(m->pVirtualBox != NULL); /* Only called from VBoxSVC. */

    ComObjPtr<CloudProviderManager> cpm(m->pVirtualBox->i_getCloudProviderManager());
    AssertReturnVoid(!cpm.isNull());

    cpm->i_addExtPack(static_cast<IExtPack *>(this));
}

#endif /* !VBOX_COM_INPROC */

/**
 * Probes the extension pack, loading the main dll and calling its registration
 * entry point.
 *
 * This updates the state accordingly, the strWhyUnusable and fUnusable members
 * being the most important ones.
 */
void ExtPack::i_probeAndLoad(void)
{
    m->fUsable = false;
    m->fMadeReadyCall = false;

    /*
     * Query the file system info for the extension pack directory.  This and
     * all other file system info we save is for the benefit of refresh().
     */
    int vrc = RTPathQueryInfoEx(m->strExtPackPath.c_str(), &m->ObjInfoExtPack, RTFSOBJATTRADD_UNIX, RTPATH_F_ON_LINK);
    if (RT_FAILURE(vrc))
    {
        m->strWhyUnusable.printf(tr("RTPathQueryInfoEx on '%s' failed: %Rrc"), m->strExtPackPath.c_str(), vrc);
        return;
    }
    if (!RTFS_IS_DIRECTORY(m->ObjInfoExtPack.Attr.fMode))
    {
        if (RTFS_IS_SYMLINK(m->ObjInfoExtPack.Attr.fMode))
            m->strWhyUnusable.printf(tr("'%s' is a symbolic link, this is not allowed"),
                                     m->strExtPackPath.c_str(), vrc);
        else if (RTFS_IS_FILE(m->ObjInfoExtPack.Attr.fMode))
            m->strWhyUnusable.printf(tr("'%s' is a symbolic file, not a directory"),
                                     m->strExtPackPath.c_str(), vrc);
        else
            m->strWhyUnusable.printf(tr("'%s' is not a directory (fMode=%#x)"),
                                     m->strExtPackPath.c_str(), m->ObjInfoExtPack.Attr.fMode);
        return;
    }

    RTERRINFOSTATIC ErrInfo;
    RTErrInfoInitStatic(&ErrInfo);
    vrc = SUPR3HardenedVerifyDir(m->strExtPackPath.c_str(), true /*fRecursive*/, true /*fCheckFiles*/, &ErrInfo.Core);
    if (RT_FAILURE(vrc))
    {
        m->strWhyUnusable.printf("%s (rc=%Rrc)", ErrInfo.Core.pszMsg, vrc);
        return;
    }

    /*
     * Read the description file.
     */
    RTCString strSavedName(m->Desc.strName);
    RTCString *pStrLoadErr = VBoxExtPackLoadDesc(m->strExtPackPath.c_str(), &m->Desc, &m->ObjInfoDesc);
    if (pStrLoadErr != NULL)
    {
        m->strWhyUnusable.printf(tr("Failed to load '%s/%s': %s"),
                                 m->strExtPackPath.c_str(), VBOX_EXTPACK_DESCRIPTION_NAME, pStrLoadErr->c_str());
        m->Desc.strName = strSavedName;
        delete pStrLoadErr;
        return;
    }

    /*
     * Make sure the XML name and directory matches.
     */
    if (!m->Desc.strName.equalsIgnoreCase(strSavedName))
    {
        m->strWhyUnusable.printf(tr("The description name ('%s') and directory name ('%s') does not match"),
                                 m->Desc.strName.c_str(), strSavedName.c_str());
        m->Desc.strName = strSavedName;
        return;
    }

    /*
     * Load the main DLL and call the predefined entry point.
     */
#ifndef VBOX_COM_INPROC
    const char *pszMainModule = m->Desc.strMainModule.c_str();
#else
    const char *pszMainModule = m->Desc.strMainVMModule.c_str();
    if (m->Desc.strMainVMModule.isEmpty())
    {
        /*
         * We're good! The main module for VM processes is optional.
         */
        m->fUsable = true;
        m->strWhyUnusable.setNull();
        return;
    }
#endif
    bool fIsNative;
    if (!i_findModule(pszMainModule, NULL /* default extension */, VBOXEXTPACKMODKIND_R3,
                      &m->strMainModPath, &fIsNative, &m->ObjInfoMainMod))
    {
        m->strWhyUnusable.printf(tr("Failed to locate the main module ('%s')"), pszMainModule);
        return;
    }

    vrc = SUPR3HardenedVerifyPlugIn(m->strMainModPath.c_str(), &ErrInfo.Core);
    if (RT_FAILURE(vrc))
    {
        m->strWhyUnusable.printf("%s", ErrInfo.Core.pszMsg);
        return;
    }

    if (fIsNative)
    {
        vrc = SUPR3HardenedLdrLoadPlugIn(m->strMainModPath.c_str(), &m->hMainMod, &ErrInfo.Core);
        if (RT_FAILURE(vrc))
        {
            m->hMainMod = NIL_RTLDRMOD;
            m->strWhyUnusable.printf(tr("Failed to load the main module ('%s'): %Rrc - %s"),
                                     m->strMainModPath.c_str(), vrc, ErrInfo.Core.pszMsg);
            return;
        }
    }
    else
    {
        m->strWhyUnusable.printf(tr("Only native main modules are currently supported"));
        return;
    }

    /*
     * Resolve the predefined entry point.
     */
#ifndef VBOX_COM_INPROC
    const char *pszMainEntryPoint = VBOX_EXTPACK_MAIN_MOD_ENTRY_POINT;
    PFNVBOXEXTPACKREGISTER pfnRegistration;
    uint32_t uVersion = VBOXEXTPACKREG_VERSION;
#else
    const char *pszMainEntryPoint = VBOX_EXTPACK_MAIN_VM_MOD_ENTRY_POINT;
    PFNVBOXEXTPACKVMREGISTER pfnRegistration;
    uint32_t uVersion = VBOXEXTPACKVMREG_VERSION;
#endif
    vrc = RTLdrGetSymbol(m->hMainMod, pszMainEntryPoint, (void **)&pfnRegistration);
    if (RT_SUCCESS(vrc))
    {
        RTErrInfoClear(&ErrInfo.Core);
        vrc = pfnRegistration(&m->Hlp, &m->pReg, &ErrInfo.Core);
        if (   RT_SUCCESS(vrc)
            && !RTErrInfoIsSet(&ErrInfo.Core)
            && RT_VALID_PTR(m->pReg))
        {
            if (   VBOXEXTPACK_IS_MAJOR_VER_EQUAL(m->pReg->u32Version, uVersion)
                && m->pReg->u32EndMarker == m->pReg->u32Version)
            {
#ifndef VBOX_COM_INPROC
                if (   (!m->pReg->pfnInstalled       || RT_VALID_PTR(m->pReg->pfnInstalled))
                    && (!m->pReg->pfnUninstall       || RT_VALID_PTR(m->pReg->pfnUninstall))
                    && (!m->pReg->pfnVirtualBoxReady || RT_VALID_PTR(m->pReg->pfnVirtualBoxReady))
                    && (!m->pReg->pfnUnload          || RT_VALID_PTR(m->pReg->pfnUnload))
                    && (!m->pReg->pfnVMCreated       || RT_VALID_PTR(m->pReg->pfnVMCreated))
                    && (!m->pReg->pfnQueryObject     || RT_VALID_PTR(m->pReg->pfnQueryObject))
                    )
                {
                    /*
                     * We're good!
                     */
                    m->fUsable = true;
                    m->strWhyUnusable.setNull();
                    return;
                }
#else
                if (   (!m->pReg->pfnConsoleReady    || RT_VALID_PTR(m->pReg->pfnConsoleReady))
                    && (!m->pReg->pfnUnload          || RT_VALID_PTR(m->pReg->pfnUnload))
                    && (!m->pReg->pfnVMConfigureVMM  || RT_VALID_PTR(m->pReg->pfnVMConfigureVMM))
                    && (!m->pReg->pfnVMPowerOn       || RT_VALID_PTR(m->pReg->pfnVMPowerOn))
                    && (!m->pReg->pfnVMPowerOff      || RT_VALID_PTR(m->pReg->pfnVMPowerOff))
                    && (!m->pReg->pfnQueryObject     || RT_VALID_PTR(m->pReg->pfnQueryObject))
                    )
                {
                    /*
                     * We're good!
                     */
                    m->fUsable = true;
                    m->strWhyUnusable.setNull();
                    return;
                }
#endif

                m->strWhyUnusable = tr("The registration structure contains one or more invalid function pointers");
            }
            else
                m->strWhyUnusable.printf(tr("Unsupported registration structure version %u.%u"),
                                         RT_HIWORD(m->pReg->u32Version), RT_LOWORD(m->pReg->u32Version));
        }
        else
            m->strWhyUnusable.printf(tr("%s returned %Rrc, pReg=%p ErrInfo='%s'"),
                                     pszMainEntryPoint, vrc, m->pReg, ErrInfo.Core.pszMsg);
        m->pReg = NULL;
    }
    else
        m->strWhyUnusable.printf(tr("Failed to resolve exported symbol '%s' in the main module: %Rrc"),
                                 pszMainEntryPoint, vrc);

    RTLdrClose(m->hMainMod);
    m->hMainMod = NIL_RTLDRMOD;
}

/**
 * Finds a module.
 *
 * @returns true if found, false if not.
 * @param   a_pszName           The module base name (no extension).
 * @param   a_pszExt            The extension. If NULL we use default
 *                              extensions.
 * @param   a_enmKind           The kind of module to locate.
 * @param   a_pStrFound         Where to return the path to the module we've
 *                              found.
 * @param   a_pfNative          Where to return whether this is a native module
 *                              or an agnostic one. Optional.
 * @param   a_pObjInfo          Where to return the file system object info for
 *                              the module. Optional.
 */
bool ExtPack::i_findModule(const char *a_pszName, const char *a_pszExt, VBOXEXTPACKMODKIND a_enmKind,
                           Utf8Str *a_pStrFound, bool *a_pfNative, PRTFSOBJINFO a_pObjInfo) const
{
    /*
     * Try the native path first.
     */
    char szPath[RTPATH_MAX];
    int vrc = RTPathJoin(szPath, sizeof(szPath), m->strExtPackPath.c_str(), RTBldCfgTargetDotArch());
    AssertLogRelRCReturn(vrc, false);
    vrc = RTPathAppend(szPath, sizeof(szPath), a_pszName);
    AssertLogRelRCReturn(vrc, false);
    if (!a_pszExt)
    {
        const char *pszDefExt;
        switch (a_enmKind)
        {
            case VBOXEXTPACKMODKIND_RC: pszDefExt = ".rc"; break;
            case VBOXEXTPACKMODKIND_R0: pszDefExt = ".r0"; break;
            case VBOXEXTPACKMODKIND_R3: pszDefExt = RTLdrGetSuff(); break;
            default:
                AssertFailedReturn(false);
        }
        vrc = RTStrCat(szPath, sizeof(szPath), pszDefExt);
        AssertLogRelRCReturn(vrc, false);
    }

    RTFSOBJINFO ObjInfo;
    if (!a_pObjInfo)
        a_pObjInfo = &ObjInfo;
    vrc = RTPathQueryInfo(szPath, a_pObjInfo, RTFSOBJATTRADD_UNIX);
    if (RT_SUCCESS(vrc) && RTFS_IS_FILE(a_pObjInfo->Attr.fMode))
    {
        if (a_pfNative)
            *a_pfNative = true;
        *a_pStrFound = szPath;
        return true;
    }

    /*
     * Try the platform agnostic modules.
     */
    /* gcc.x86/module.rel */
    char szSubDir[32];
    RTStrPrintf(szSubDir, sizeof(szSubDir), "%s.%s", RTBldCfgCompiler(), RTBldCfgTargetArch());
    vrc = RTPathJoin(szPath, sizeof(szPath), m->strExtPackPath.c_str(), szSubDir);
    AssertLogRelRCReturn(vrc, false);
    vrc = RTPathAppend(szPath, sizeof(szPath), a_pszName);
    AssertLogRelRCReturn(vrc, false);
    if (!a_pszExt)
    {
        vrc = RTStrCat(szPath, sizeof(szPath), ".rel");
        AssertLogRelRCReturn(vrc, false);
    }
    vrc = RTPathQueryInfo(szPath, a_pObjInfo, RTFSOBJATTRADD_UNIX);
    if (RT_SUCCESS(vrc) && RTFS_IS_FILE(a_pObjInfo->Attr.fMode))
    {
        if (a_pfNative)
            *a_pfNative = false;
        *a_pStrFound = szPath;
        return true;
    }

    /* x86/module.rel */
    vrc = RTPathJoin(szPath, sizeof(szPath), m->strExtPackPath.c_str(), RTBldCfgTargetArch());
    AssertLogRelRCReturn(vrc, false);
    vrc = RTPathAppend(szPath, sizeof(szPath), a_pszName);
    AssertLogRelRCReturn(vrc, false);
    if (!a_pszExt)
    {
        vrc = RTStrCat(szPath, sizeof(szPath), ".rel");
        AssertLogRelRCReturn(vrc, false);
    }
    vrc = RTPathQueryInfo(szPath, a_pObjInfo, RTFSOBJATTRADD_UNIX);
    if (RT_SUCCESS(vrc) && RTFS_IS_FILE(a_pObjInfo->Attr.fMode))
    {
        if (a_pfNative)
            *a_pfNative = false;
        *a_pStrFound = szPath;
        return true;
    }

    return false;
}

/**
 * Compares two file system object info structures.
 *
 * @returns true if equal, false if not.
 * @param   pObjInfo1           The first.
 * @param   pObjInfo2           The second.
 * @todo    IPRT should do this, really.
 */
/* static */ bool ExtPack::i_objinfoIsEqual(PCRTFSOBJINFO pObjInfo1, PCRTFSOBJINFO pObjInfo2)
{
    if (!RTTimeSpecIsEqual(&pObjInfo1->ModificationTime,   &pObjInfo2->ModificationTime))
        return false;
    if (!RTTimeSpecIsEqual(&pObjInfo1->ChangeTime,         &pObjInfo2->ChangeTime))
        return false;
    if (!RTTimeSpecIsEqual(&pObjInfo1->BirthTime,          &pObjInfo2->BirthTime))
        return false;
    if (pObjInfo1->cbObject                              != pObjInfo2->cbObject)
        return false;
    if (pObjInfo1->Attr.fMode                            != pObjInfo2->Attr.fMode)
        return false;
    if (pObjInfo1->Attr.enmAdditional                    == pObjInfo2->Attr.enmAdditional)
    {
        switch (pObjInfo1->Attr.enmAdditional)
        {
            case RTFSOBJATTRADD_UNIX:
                if (pObjInfo1->Attr.u.Unix.uid           != pObjInfo2->Attr.u.Unix.uid)
                    return false;
                if (pObjInfo1->Attr.u.Unix.gid           != pObjInfo2->Attr.u.Unix.gid)
                    return false;
                if (pObjInfo1->Attr.u.Unix.INodeIdDevice != pObjInfo2->Attr.u.Unix.INodeIdDevice)
                    return false;
                if (pObjInfo1->Attr.u.Unix.INodeId       != pObjInfo2->Attr.u.Unix.INodeId)
                    return false;
                if (pObjInfo1->Attr.u.Unix.GenerationId  != pObjInfo2->Attr.u.Unix.GenerationId)
                    return false;
                break;
            default:
                break;
        }
    }
    return true;
}


/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnFindModule}
 */
/*static*/ DECLCALLBACK(int)
ExtPack::i_hlpFindModule(PCVBOXEXTPACKHLP pHlp, const char *pszName, const char *pszExt, VBOXEXTPACKMODKIND enmKind,
                         char *pszFound, size_t cbFound, bool *pfNative)
{
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pszExt, VERR_INVALID_POINTER);
    AssertPtrReturn(pszFound, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfNative, VERR_INVALID_POINTER);
    AssertReturn(enmKind > VBOXEXTPACKMODKIND_INVALID && enmKind < VBOXEXTPACKMODKIND_END, VERR_INVALID_PARAMETER);

    AssertPtrReturn(pHlp, VERR_INVALID_POINTER);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, VERR_INVALID_POINTER);
    ExtPack::Data *pData = RT_FROM_CPP_MEMBER(pHlp, Data, Hlp);
    AssertPtrReturn(pData, VERR_INVALID_POINTER);
    ExtPack *pThis = pData->pThis;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    /*
     * This is just a wrapper around findModule.
     */
    Utf8Str strFound;
    if (pThis->i_findModule(pszName, pszExt, enmKind, &strFound, pfNative, NULL))
        return RTStrCopy(pszFound, cbFound, strFound.c_str());
    return VERR_FILE_NOT_FOUND;
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnGetFilePath}
 */
/*static*/ DECLCALLBACK(int)
ExtPack::i_hlpGetFilePath(PCVBOXEXTPACKHLP pHlp, const char *pszFilename, char *pszPath, size_t cbPath)
{
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cbPath > 0, VERR_BUFFER_OVERFLOW);

    AssertPtrReturn(pHlp, VERR_INVALID_POINTER);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, VERR_INVALID_POINTER);
    ExtPack::Data *pData = RT_FROM_CPP_MEMBER(pHlp, Data, Hlp);
    AssertPtrReturn(pData, VERR_INVALID_POINTER);

    /*
     * This is a simple RTPathJoin, no checking if things exists or anything.
     */
    int vrc = RTPathJoin(pszPath, cbPath, pData->strExtPackPath.c_str(), pszFilename);
    if (RT_FAILURE(vrc))
        RT_BZERO(pszPath, cbPath);
    return vrc;
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnGetContext}
 */
/*static*/ DECLCALLBACK(VBOXEXTPACKCTX)
ExtPack::i_hlpGetContext(PCVBOXEXTPACKHLP pHlp)
{
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pHlp, VBOXEXTPACKCTX_INVALID);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, VBOXEXTPACKCTX_INVALID);
    ExtPack::Data *pData = RT_FROM_CPP_MEMBER(pHlp, Data, Hlp);
    AssertPtrReturn(pData, VBOXEXTPACKCTX_INVALID);

    return pData->enmContext;
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnLoadHGCMService}
 */
/*static*/ DECLCALLBACK(int)
ExtPack::i_hlpLoadHGCMService(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IConsole) *pConsole,
                              const char *pszServiceLibrary, const char *pszServiceName)
{
#ifdef VBOX_COM_INPROC
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pszServiceLibrary, VERR_INVALID_POINTER);
    AssertPtrReturn(pszServiceName, VERR_INVALID_POINTER);

    AssertPtrReturn(pHlp, VERR_INVALID_POINTER);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, VERR_INVALID_POINTER);
    ExtPack::Data *pData = RT_FROM_CPP_MEMBER(pHlp, Data, Hlp);
    AssertPtrReturn(pData, VERR_INVALID_POINTER);
    ExtPack *pThis = pData->pThis;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pConsole, VERR_INVALID_POINTER);

    Console *pCon = (Console *)pConsole;
    return pCon->i_hgcmLoadService(pszServiceLibrary, pszServiceName);
#else
    RT_NOREF(pHlp, pConsole, pszServiceLibrary, pszServiceName);
    return VERR_INVALID_STATE;
#endif
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnLoadVDPlugin}
 */
/*static*/ DECLCALLBACK(int)
ExtPack::i_hlpLoadVDPlugin(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IVirtualBox) *pVirtualBox, const char *pszPluginLibrary)
{
#ifndef VBOX_COM_INPROC
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pszPluginLibrary, VERR_INVALID_POINTER);

    AssertPtrReturn(pHlp, VERR_INVALID_POINTER);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, VERR_INVALID_POINTER);
    ExtPack::Data *pData = RT_FROM_CPP_MEMBER(pHlp, Data, Hlp);
    AssertPtrReturn(pData, VERR_INVALID_POINTER);
    ExtPack *pThis = pData->pThis;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pVirtualBox, VERR_INVALID_POINTER);

    VirtualBox *pVBox = (VirtualBox *)pVirtualBox;
    return pVBox->i_loadVDPlugin(pszPluginLibrary);
#else
    RT_NOREF(pHlp, pVirtualBox, pszPluginLibrary);
    return VERR_INVALID_STATE;
#endif
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnUnloadVDPlugin}
 */
/*static*/ DECLCALLBACK(int)
ExtPack::i_hlpUnloadVDPlugin(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IVirtualBox) *pVirtualBox, const char *pszPluginLibrary)
{
#ifndef VBOX_COM_INPROC
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pszPluginLibrary, VERR_INVALID_POINTER);

    AssertPtrReturn(pHlp, VERR_INVALID_POINTER);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, VERR_INVALID_POINTER);
    ExtPack::Data *pData = RT_FROM_CPP_MEMBER(pHlp, Data, Hlp);
    AssertPtrReturn(pData, VERR_INVALID_POINTER);
    ExtPack *pThis = pData->pThis;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pVirtualBox, VERR_INVALID_POINTER);

    VirtualBox *pVBox = (VirtualBox *)pVirtualBox;
    return pVBox->i_unloadVDPlugin(pszPluginLibrary);
#else
    RT_NOREF(pHlp, pVirtualBox, pszPluginLibrary);
    return VERR_INVALID_STATE;
#endif
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnCreateProgress}
 */
/*static*/ DECLCALLBACK(uint32_t)
ExtPack::i_hlpCreateProgress(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IUnknown) *pInitiator,
                             const char *pcszDescription, uint32_t cOperations,
                             uint32_t uTotalOperationsWeight, const char *pcszFirstOperationDescription,
                             uint32_t uFirstOperationWeight, VBOXEXTPACK_IF_CS(IProgress) **ppProgressOut)
{
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pcszDescription, (uint32_t)E_INVALIDARG);
    AssertReturn(cOperations >= 1, (uint32_t)E_INVALIDARG);
    AssertReturn(uTotalOperationsWeight >= 1, (uint32_t)E_INVALIDARG);
    AssertPtrReturn(pcszFirstOperationDescription, (uint32_t)E_INVALIDARG);
    AssertReturn(uFirstOperationWeight >= 1, (uint32_t)E_INVALIDARG);
    AssertPtrReturn(ppProgressOut, (uint32_t)E_INVALIDARG);

    AssertPtrReturn(pHlp, (uint32_t)E_INVALIDARG);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, (uint32_t)E_INVALIDARG);
#ifndef VBOX_COM_INPROC
    ExtPack::Data *pData = RT_FROM_CPP_MEMBER(pHlp, Data, Hlp);
#endif

    ComObjPtr<Progress> pProgress;
    HRESULT hrc = pProgress.createObject();
    if (SUCCEEDED(hrc))
    {
        hrc = pProgress->init(
#ifndef VBOX_COM_INPROC
                              pData->pVirtualBox,
#endif
                              pInitiator, pcszDescription, TRUE /* aCancelable */,
                              cOperations, uTotalOperationsWeight,
                              pcszFirstOperationDescription, uFirstOperationWeight);
        if (SUCCEEDED(hrc))
            hrc = pProgress.queryInterfaceTo(ppProgressOut);
    }
    return hrc;
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnGetCanceledProgress}
 */
/*static*/ DECLCALLBACK(uint32_t)
ExtPack::i_hlpGetCanceledProgress(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IProgress) *pProgress,
                                  bool *pfCanceled)
{
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pProgress, (uint32_t)E_INVALIDARG);
    AssertPtrReturn(pfCanceled, (uint32_t)E_INVALIDARG);

    AssertPtrReturn(pHlp, (uint32_t)E_INVALIDARG);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, (uint32_t)E_INVALIDARG);

    BOOL fCanceled = FALSE;
    HRESULT hrc = pProgress->COMGETTER(Canceled)(&fCanceled);
    *pfCanceled  = !!fCanceled;
    return hrc;
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnUpdateProgress}
 */
/*static*/ DECLCALLBACK(uint32_t)
ExtPack::i_hlpUpdateProgress(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IProgress) *pProgress,
                             uint32_t uPercent)
{
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pProgress, (uint32_t)E_INVALIDARG);
    AssertReturn(uPercent <= 100, (uint32_t)E_INVALIDARG);

    AssertPtrReturn(pHlp, (uint32_t)E_INVALIDARG);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, (uint32_t)E_INVALIDARG);

    ComPtr<IInternalProgressControl> pProgressControl(pProgress);
    AssertReturn(!!pProgressControl, (uint32_t)E_INVALIDARG);
    return pProgressControl->SetCurrentOperationProgress(uPercent);
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnNextOperationProgress}
 */
/*static*/ DECLCALLBACK(uint32_t)
ExtPack::i_hlpNextOperationProgress(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IProgress) *pProgress,
                                    const char *pcszNextOperationDescription,
                                    uint32_t uNextOperationWeight)
{
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pProgress, (uint32_t)E_INVALIDARG);
    AssertPtrReturn(pcszNextOperationDescription, (uint32_t)E_INVALIDARG);
    AssertReturn(uNextOperationWeight >= 1, (uint32_t)E_INVALIDARG);

    AssertPtrReturn(pHlp, (uint32_t)E_INVALIDARG);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, (uint32_t)E_INVALIDARG);

    ComPtr<IInternalProgressControl> pProgressControl(pProgress);
    AssertReturn(!!pProgressControl, (uint32_t)E_INVALIDARG);
    return pProgressControl->SetNextOperation(Bstr(pcszNextOperationDescription).raw(), uNextOperationWeight);
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnWaitOtherProgress}
 */
/*static*/ DECLCALLBACK(uint32_t)
ExtPack::i_hlpWaitOtherProgress(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IProgress) *pProgress,
                                VBOXEXTPACK_IF_CS(IProgress) *pProgressOther, uint32_t cTimeoutMS)
{
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pProgress, (uint32_t)E_INVALIDARG);
    AssertPtrReturn(pProgressOther, (uint32_t)E_INVALIDARG);

    AssertPtrReturn(pHlp, (uint32_t)E_INVALIDARG);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, (uint32_t)E_INVALIDARG);

    ComPtr<IInternalProgressControl> pProgressControl(pProgress);
    AssertReturn(!!pProgressControl, (uint32_t)E_INVALIDARG);
    return pProgressControl->WaitForOtherProgressCompletion(pProgressOther, cTimeoutMS);
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnCompleteProgress}
 */
/*static*/ DECLCALLBACK(uint32_t)
ExtPack::i_hlpCompleteProgress(PCVBOXEXTPACKHLP pHlp, VBOXEXTPACK_IF_CS(IProgress) *pProgress,
                               uint32_t uResultCode)
{
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pHlp, (uint32_t)E_INVALIDARG);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, (uint32_t)E_INVALIDARG);
    AssertPtrReturn(pProgress, (uint32_t)E_INVALIDARG);

    ComPtr<IInternalProgressControl> pProgressControl(pProgress);
    AssertReturn(!!pProgressControl, (uint32_t)E_INVALIDARG);

    ComPtr<IVirtualBoxErrorInfo> errorInfo;
    if (FAILED((HRESULT)uResultCode))
    {
        ErrorInfoKeeper eik;
        eik.getVirtualBoxErrorInfo(errorInfo);
    }
    return pProgressControl->NotifyComplete((LONG)uResultCode, errorInfo);
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnCreateEvent}
 */
/*static*/ DECLCALLBACK(uint32_t)
ExtPack::i_hlpCreateEvent(PCVBOXEXTPACKHLP pHlp,
                          VBOXEXTPACK_IF_CS(IEventSource) *aSource,
                          /* VBoxEventType_T */ uint32_t aType, bool aWaitable,
                          VBOXEXTPACK_IF_CS(IEvent) **ppEventOut)
{
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pHlp, (uint32_t)E_INVALIDARG);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, (uint32_t)E_INVALIDARG);
    AssertPtrReturn(ppEventOut, (uint32_t)E_INVALIDARG);

    ComObjPtr<VBoxEvent> pEvent;
    HRESULT hrc = pEvent.createObject();
    if (SUCCEEDED(hrc))
    {
        /* default aSource to pVirtualBox? */
        hrc = pEvent->init(aSource, static_cast<VBoxEventType_T>(aType), aWaitable);
        if (SUCCEEDED(hrc))
            hrc = pEvent.queryInterfaceTo(ppEventOut);
    }
    return hrc;
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnCreateVetoEvent}
 */
/*static*/ DECLCALLBACK(uint32_t)
ExtPack::i_hlpCreateVetoEvent(PCVBOXEXTPACKHLP pHlp,
                              VBOXEXTPACK_IF_CS(IEventSource) *aSource,
                              /* VBoxEventType_T */ uint32_t aType,
                              VBOXEXTPACK_IF_CS(IVetoEvent) **ppEventOut)
{
    AssertPtrReturn(pHlp, (uint32_t)E_INVALIDARG);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, (uint32_t)E_INVALIDARG);
    AssertPtrReturn(ppEventOut, (uint32_t)E_INVALIDARG);

    ComObjPtr<VBoxVetoEvent> pEvent;

    HRESULT hrc = pEvent.createObject();
    if (SUCCEEDED(hrc))
    {
        /* default aSource to pVirtualBox? */
        hrc = pEvent->init(aSource, static_cast<VBoxEventType_T>(aType));
        if (SUCCEEDED(hrc))
            hrc = pEvent.queryInterfaceTo(ppEventOut);
    }
    return hrc;
}

/**
 * @interface_method_impl{VBOXEXTPACKHLP,pfnTranslate}
 */
/*static*/ DECLCALLBACK(const char *)
ExtPack::i_hlpTranslate(PCVBOXEXTPACKHLP pHlp,
                        const char  *pszComponent,
                        const char  *pszSourceText,
                        const char  *pszComment /*=NULL*/,
                        const size_t uNum /*= ~(size_t)0*/)
{
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pHlp, pszSourceText);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, pszSourceText);
    ExtPack::Data *pData = RT_FROM_CPP_MEMBER(pHlp, Data, Hlp);
    AssertPtrReturn(pData, pszSourceText);

#ifdef VBOX_WITH_MAIN_NLS
    return VirtualBoxTranslator::translate(pData->pTrComponent, pszComponent, pszSourceText, pszComment, uNum);
#else
    RT_NOREF(pszComponent, pszComment, uNum);
    return pszSourceText;
#endif
}


/*static*/ DECLCALLBACK(int)
ExtPack::i_hlpReservedN(PCVBOXEXTPACKHLP pHlp)
{
    /*
     * Validate the input and get our bearings.
     */
    AssertPtrReturn(pHlp, VERR_INVALID_POINTER);
    AssertReturn(pHlp->u32Version == VBOXEXTPACKHLP_VERSION, VERR_INVALID_POINTER);
    ExtPack::Data *pData = RT_FROM_CPP_MEMBER(pHlp, Data, Hlp);
    AssertPtrReturn(pData, VERR_INVALID_POINTER);
    ExtPack *pThis = pData->pThis;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    return VERR_NOT_IMPLEMENTED;
}




HRESULT ExtPack::getName(com::Utf8Str &aName)
{
    aName = m->Desc.strName;
    return S_OK;
}

HRESULT ExtPack::getDescription(com::Utf8Str &aDescription)
{
    aDescription = m->Desc.strDescription;
    return S_OK;
}

HRESULT ExtPack::getVersion(com::Utf8Str &aVersion)
{
    aVersion = m->Desc.strVersion;
    return S_OK;
}

HRESULT ExtPack::getRevision(ULONG *aRevision)
{
    *aRevision = m->Desc.uRevision;
    return S_OK;
}

HRESULT ExtPack::getEdition(com::Utf8Str &aEdition)
{
    aEdition = m->Desc.strEdition;
    return S_OK;
}

HRESULT ExtPack::getVRDEModule(com::Utf8Str &aVRDEModule)
{
    aVRDEModule = m->Desc.strVrdeModule;
    return S_OK;
}

HRESULT ExtPack::getCryptoModule(com::Utf8Str &aCryptoModule)
{
    aCryptoModule = m->Desc.strCryptoModule;
    return S_OK;
}

HRESULT ExtPack::getPlugIns(std::vector<ComPtr<IExtPackPlugIn> > &aPlugIns)
{
    /** @todo implement plug-ins. */
    NOREF(aPlugIns);
    ReturnComNotImplemented();
}

HRESULT ExtPack::getUsable(BOOL *aUsable)
{
    *aUsable = m->fUsable;
    return S_OK;
}

HRESULT ExtPack::getWhyUnusable(com::Utf8Str &aWhyUnusable)
{
    aWhyUnusable = m->strWhyUnusable;
    return S_OK;
}

HRESULT ExtPack::getShowLicense(BOOL *aShowLicense)
{
    *aShowLicense = m->Desc.fShowLicense;
    return S_OK;
}

HRESULT ExtPack::getLicense(com::Utf8Str &aLicense)
{
    Utf8Str strHtml("html");
    Utf8Str str("");
    return queryLicense(str, str, strHtml, aLicense);
}

HRESULT ExtPack::queryLicense(const com::Utf8Str &aPreferredLocale, const com::Utf8Str &aPreferredLanguage,
                              const com::Utf8Str &aFormat, com::Utf8Str &aLicenseText)
{
    HRESULT hrc = S_OK;

    /*
     * Validate input.
     */
    if (aPreferredLocale.length() != 2 && aPreferredLocale.length() != 0)
        return setError(E_FAIL, tr("The preferred locale is a two character string or empty."));

    if (aPreferredLanguage.length() != 2 && aPreferredLanguage.length() != 0)
        return setError(E_FAIL, tr("The preferred language is a two character string or empty."));

    if (   !aFormat.equals("html")
        && !aFormat.equals("rtf")
        && !aFormat.equals("txt"))
        return setError(E_FAIL, tr("The license format can only have the values 'html', 'rtf' and 'txt'."));

    /*
     * Combine the options to form a file name before locking down anything.
     */
    char szName[sizeof(VBOX_EXTPACK_LICENSE_NAME_PREFIX "-de_DE.html") + 2];
    if (aPreferredLocale.isNotEmpty() && aPreferredLanguage.isNotEmpty())
        RTStrPrintf(szName, sizeof(szName), VBOX_EXTPACK_LICENSE_NAME_PREFIX "-%s_%s.%s",
                    aPreferredLocale.c_str(), aPreferredLanguage.c_str(), aFormat.c_str());
    else if (aPreferredLocale.isNotEmpty())
        RTStrPrintf(szName, sizeof(szName), VBOX_EXTPACK_LICENSE_NAME_PREFIX "-%s.%s",
                    aPreferredLocale.c_str(), aFormat.c_str());
    else if (aPreferredLanguage.isNotEmpty())
        RTStrPrintf(szName, sizeof(szName), VBOX_EXTPACK_LICENSE_NAME_PREFIX "-_%s.%s",
                    aPreferredLocale.c_str(), aFormat.c_str());
    else
        RTStrPrintf(szName, sizeof(szName), VBOX_EXTPACK_LICENSE_NAME_PREFIX ".%s",
                    aFormat.c_str());

    /*
     * Effectuate the query.
     */
    AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS); /* paranoia */

    if (!m->fUsable)
        hrc = setError(E_FAIL, "%s", m->strWhyUnusable.c_str());
    else
    {
        char szPath[RTPATH_MAX];
        int vrc = RTPathJoin(szPath, sizeof(szPath), m->strExtPackPath.c_str(), szName);
        if (RT_SUCCESS(vrc))
        {
            void   *pvFile;
            size_t  cbFile;
            vrc = RTFileReadAllEx(szPath, 0, RTFOFF_MAX, RTFILE_RDALL_O_DENY_READ, &pvFile, &cbFile);
            if (RT_SUCCESS(vrc))
            {
                Bstr bstrLicense((const char *)pvFile, cbFile);
                if (bstrLicense.isNotEmpty())
                {
                    aLicenseText = Utf8Str(bstrLicense);
                    hrc = S_OK;
                }
                else
                    hrc = setError(VBOX_E_IPRT_ERROR, tr("The license file '%s' is empty or contains invalid UTF-8 encoding"),
                                   szPath);
                RTFileReadAllFree(pvFile, cbFile);
            }
            else if (vrc == VERR_FILE_NOT_FOUND || vrc == VERR_PATH_NOT_FOUND)
                hrc = setErrorBoth(VBOX_E_OBJECT_NOT_FOUND, vrc, tr("The license file '%s' was not found in extension pack '%s'"),
                                   szName, m->Desc.strName.c_str());
            else
                hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Failed to open the license file '%s': %Rrc"), szPath, vrc);
        }
        else
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("RTPathJoin failed: %Rrc"), vrc);
    }
    return hrc;
}

HRESULT ExtPack::queryObject(const com::Utf8Str &aObjUuid, ComPtr<IUnknown> &aReturnInterface)
{
    com::Guid ObjectId;
    CheckComArgGuid(aObjUuid, ObjectId);

    HRESULT hrc = S_OK;

    if (   m->pReg
        && m->pReg->pfnQueryObject)
    {
        void *pvUnknown = m->pReg->pfnQueryObject(m->pReg, ObjectId.raw());
        if (pvUnknown)
        {
            aReturnInterface = (IUnknown *)pvUnknown;
            /* The above assignment increased the refcount. Since pvUnknown
             * is a dumb pointer we have to do the release ourselves. */
            ((IUnknown *)pvUnknown)->Release();
        }
        else
            hrc = E_NOINTERFACE;
    }
    else
        hrc = E_NOINTERFACE;
    return hrc;
}

DEFINE_EMPTY_CTOR_DTOR(ExtPackManager)

/**
 * Called by ComObjPtr::createObject when creating the object.
 *
 * Just initialize the basic object state, do the rest in init().
 *
 * @returns S_OK.
 */
HRESULT ExtPackManager::FinalConstruct()
{
    m = NULL;
    return BaseFinalConstruct();
}

/**
 * Initializes the extension pack manager.
 *
 * @returns COM status code.
 * @param   a_pVirtualBox           Pointer to the VirtualBox object.
 * @param   a_enmContext            The context we're in.
 */
HRESULT ExtPackManager::initExtPackManager(VirtualBox *a_pVirtualBox, VBOXEXTPACKCTX a_enmContext)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /*
     * Figure some stuff out before creating the instance data.
     */
    char szBaseDir[RTPATH_MAX];
    int vrc = RTPathAppPrivateArchTop(szBaseDir, sizeof(szBaseDir));
    AssertLogRelRCReturn(vrc, E_FAIL);
    vrc = RTPathAppend(szBaseDir, sizeof(szBaseDir), VBOX_EXTPACK_INSTALL_DIR);
    AssertLogRelRCReturn(vrc, E_FAIL);

    char szCertificatDir[RTPATH_MAX];
    vrc = RTPathAppPrivateNoArch(szCertificatDir, sizeof(szCertificatDir));
    AssertLogRelRCReturn(vrc, E_FAIL);
    vrc = RTPathAppend(szCertificatDir, sizeof(szCertificatDir), VBOX_EXTPACK_CERT_DIR);
    AssertLogRelRCReturn(vrc, E_FAIL);

    /*
     * Allocate and initialize the instance data.
     */
    m = new Data;
    m->strBaseDir           = szBaseDir;
    m->strCertificatDirPath = szCertificatDir;
    m->enmContext           = a_enmContext;
#ifndef VBOX_COM_INPROC
    m->pVirtualBox          = a_pVirtualBox;
#else
    RT_NOREF_PV(a_pVirtualBox);
#endif

    /*
     * Go looking for extensions.  The RTDirOpen may fail if nothing has been
     * installed yet, or if root is paranoid and has revoked our access to them.
     *
     * We ASSUME that there are no files, directories or stuff in the directory
     * that exceed the max name length in RTDIRENTRYEX.
     */
    HRESULT hrc = S_OK;
    RTDIR   hDir;
    vrc = RTDirOpen(&hDir, szBaseDir);
    if (RT_SUCCESS(vrc))
    {
        for (;;)
        {
            RTDIRENTRYEX Entry;
            vrc = RTDirReadEx(hDir, &Entry, NULL /*pcbDirEntry*/, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
            if (RT_FAILURE(vrc))
            {
                AssertLogRelMsg(vrc == VERR_NO_MORE_FILES, ("%Rrc\n", vrc));
                break;
            }
            if (   RTFS_IS_DIRECTORY(Entry.Info.Attr.fMode)
                && strcmp(Entry.szName, ".")  != 0
                && strcmp(Entry.szName, "..") != 0
                && VBoxExtPackIsValidMangledName(Entry.szName) )
            {
                /*
                 * All directories are extensions, the shall be nothing but
                 * extensions in this subdirectory.
                 */
                char szExtPackDir[RTPATH_MAX];
                vrc = RTPathJoin(szExtPackDir, sizeof(szExtPackDir), m->strBaseDir.c_str(), Entry.szName);
                AssertLogRelRC(vrc);
                if (RT_SUCCESS(vrc))
                {
                    RTCString *pstrName = VBoxExtPackUnmangleName(Entry.szName, RTSTR_MAX);
                    AssertLogRel(pstrName);
                    if (pstrName)
                    {
                        ComObjPtr<ExtPack> NewExtPack;
                        HRESULT hrc2 = NewExtPack.createObject();
                        if (SUCCEEDED(hrc2))
                            hrc2 = NewExtPack->initWithDir(a_pVirtualBox, a_enmContext, pstrName->c_str(), szExtPackDir);
                        delete pstrName;
                        if (SUCCEEDED(hrc2))
                        {
                            m->llInstalledExtPacks.push_back(NewExtPack);
                            /* Paranoia, there should be no API clients before this method is finished. */

                            m->cUpdate++;
                        }
                        else if (SUCCEEDED(hrc))
                            hrc = hrc2;
                    }
                    else
                        hrc = E_UNEXPECTED;
                }
                else
                    hrc = E_UNEXPECTED;
            }
        }
        RTDirClose(hDir);
    }
    /* else: ignore, the directory probably does not exist or something. */

    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();
    return hrc;
}

/**
 * COM cruft.
 */
void ExtPackManager::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

/**
 * Do the actual cleanup.
 */
void ExtPackManager::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (!autoUninitSpan.uninitDone() && m != NULL)
    {
        delete m;
        m = NULL;
    }
}

HRESULT ExtPackManager::getInstalledExtPacks(std::vector<ComPtr<IExtPack> > &aInstalledExtPacks)
{
    Assert(m->enmContext == VBOXEXTPACKCTX_PER_USER_DAEMON);

    AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS);

    aInstalledExtPacks.resize(m->llInstalledExtPacks.size());
    std::copy(m->llInstalledExtPacks.begin(), m->llInstalledExtPacks.end(), aInstalledExtPacks.begin());

    return S_OK;
}

HRESULT ExtPackManager::find(const com::Utf8Str &aName, ComPtr<IExtPack> &aReturnData)
{
    HRESULT hrc = S_OK;

    Assert(m->enmContext == VBOXEXTPACKCTX_PER_USER_DAEMON);

    AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS);

    ComPtr<ExtPack> ptrExtPack = i_findExtPack(aName.c_str());
    if (!ptrExtPack.isNull())
        ptrExtPack.queryInterfaceTo(aReturnData.asOutParam());
    else
        hrc = VBOX_E_OBJECT_NOT_FOUND;

    return hrc;
}

HRESULT ExtPackManager::openExtPackFile(const com::Utf8Str &aPath, ComPtr<IExtPackFile> &aFile)
{
    AssertReturn(m->enmContext == VBOXEXTPACKCTX_PER_USER_DAEMON, E_UNEXPECTED);

#ifndef VBOX_COM_INPROC
    /* The API can optionally take a ::SHA-256=<hex-digest> attribute at the
       end of the file name.  This is just a temporary measure for
       backporting, in 4.2 we'll add another parameter to the method. */
    Utf8Str strTarball;
    Utf8Str strDigest;
    size_t offSha256 = aPath.find("::SHA-256=");
    if (offSha256 == Utf8Str::npos)
        strTarball = aPath;
    else
    {
        strTarball = aPath.substr(0, offSha256);
        strDigest  = aPath.substr(offSha256 + sizeof("::SHA-256=") - 1);
    }

    ComObjPtr<ExtPackFile> NewExtPackFile;
    HRESULT hrc = NewExtPackFile.createObject();
    if (SUCCEEDED(hrc))
        hrc = NewExtPackFile->initWithFile(strTarball.c_str(), strDigest.c_str(), this, m->pVirtualBox);
    if (SUCCEEDED(hrc))
        NewExtPackFile.queryInterfaceTo(aFile.asOutParam());

    return hrc;
#else
    RT_NOREF(aPath, aFile);
    return E_NOTIMPL;
#endif
}

HRESULT ExtPackManager::uninstall(const com::Utf8Str &aName, BOOL aForcedRemoval,
                                  const com::Utf8Str &aDisplayInfo, ComPtr<IProgress> &aProgress)
{
    Assert(m->enmContext == VBOXEXTPACKCTX_PER_USER_DAEMON);

#ifndef VBOX_COM_INPROC

    HRESULT hrc;
    ExtPackUninstallTask *pTask = NULL;
    try
    {
        pTask = new ExtPackUninstallTask();
        hrc = pTask->Init(this, aName, aForcedRemoval != FALSE, aDisplayInfo);
        if (SUCCEEDED(hrc))
        {
            ComPtr<Progress> ptrProgress = pTask->ptrProgress;
            hrc = pTask->createThreadWithType(RTTHREADTYPE_DEFAULT);
            pTask = NULL;               /* always consumed by createThread */
            if (SUCCEEDED(hrc))
                hrc = ptrProgress.queryInterfaceTo(aProgress.asOutParam());
            else
                hrc = setError(VBOX_E_IPRT_ERROR,
                               tr("Starting thread for an extension pack uninstallation failed with %Rrc"), hrc);
        }
        else
            hrc = setError(hrc, tr("Looks like creating a progress object for ExtraPackUninstallTask object failed"));
    }
    catch (std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }
    catch (HRESULT hrcXcpt)
    {
        LogFlowThisFunc(("Exception was caught in the function ExtPackManager::uninstall()\n"));
        hrc = hrcXcpt;
    }
    if (pTask)
        delete pTask;
    return hrc;
#else
    RT_NOREF(aName, aForcedRemoval, aDisplayInfo, aProgress);
    return E_NOTIMPL;
#endif
}

HRESULT ExtPackManager::cleanup(void)
{
    Assert(m->enmContext == VBOXEXTPACKCTX_PER_USER_DAEMON);

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        /*
         * Run the set-uid-to-root binary that performs the cleanup.
         *
         * Take the write lock to prevent conflicts with other calls to this
         * VBoxSVC instance.
         */
        AutoWriteLock autoLock(this COMMA_LOCKVAL_SRC_POS);
        hrc = i_runSetUidToRootHelper(NULL,
                                      "cleanup",
                                      "--base-dir", m->strBaseDir.c_str(),
                                      (const char *)NULL);
    }

    return hrc;
}

HRESULT ExtPackManager::queryAllPlugInsForFrontend(const com::Utf8Str &aFrontendName, std::vector<com::Utf8Str> &aPlugInModules)
{
    NOREF(aFrontendName);
    aPlugInModules.resize(0);
    return S_OK;
}

HRESULT ExtPackManager::isExtPackUsable(const com::Utf8Str &aName, BOOL *aUsable)
{
    *aUsable = i_isExtPackUsable(aName.c_str());
    return S_OK;
}

/**
 * Finds the success indicator string in the stderr output ofr hte helper app.
 *
 * @returns Pointer to the indicator.
 * @param   psz                 The stderr output string. Can be NULL.
 * @param   cch                 The size of the string.
 */
static char *findSuccessIndicator(char *psz, size_t cch)
{
    static const char s_szSuccessInd[] = "rcExit=RTEXITCODE_SUCCESS";
    Assert(!cch || strlen(psz) == cch);
    if (cch < sizeof(s_szSuccessInd) - 1)
        return NULL;
    char *pszInd = &psz[cch - sizeof(s_szSuccessInd) + 1];
    if (strcmp(s_szSuccessInd, pszInd))
        return NULL;
    return pszInd;
}

/**
 * Runs the helper application that does the privileged operations.
 *
 * @returns S_OK or a failure status with error information set.
 * @param   a_pstrDisplayInfo   Platform specific display info hacks.
 * @param   a_pszCommand        The command to execute.
 * @param   ...                 The argument strings that goes along with the
 *                              command. Maximum is about 16.  Terminated by a
 *                              NULL.
 */
HRESULT ExtPackManager::i_runSetUidToRootHelper(Utf8Str const *a_pstrDisplayInfo, const char *a_pszCommand, ...)
{
    /*
     * Calculate the path to the helper application.
     */
    char szExecName[RTPATH_MAX];
    int vrc = RTPathAppPrivateArch(szExecName, sizeof(szExecName));
    AssertLogRelRCReturn(vrc, E_UNEXPECTED);

    vrc = RTPathAppend(szExecName, sizeof(szExecName), VBOX_EXTPACK_HELPER_NAME);
    AssertLogRelRCReturn(vrc, E_UNEXPECTED);

    /*
     * Convert the variable argument list to a RTProcCreate argument vector.
     */
    const char *apszArgs[20];
    unsigned    cArgs = 0;

    LogRel(("ExtPack: Executing '%s'", szExecName));
    apszArgs[cArgs++] = &szExecName[0];

    if (   a_pstrDisplayInfo
        && a_pstrDisplayInfo->isNotEmpty())
    {
        LogRel((" '--display-info-hack' '%s'", a_pstrDisplayInfo->c_str()));
        apszArgs[cArgs++] = "--display-info-hack";
        apszArgs[cArgs++] = a_pstrDisplayInfo->c_str();
    }

    LogRel((" '%s'", a_pszCommand));
    apszArgs[cArgs++] = a_pszCommand;

    HRESULT hrc = S_OK;

    va_list va;
    va_start(va, a_pszCommand);
    const char *pszLastArg;
    for (;;)
    {
        AssertBreakStmt(cArgs < RT_ELEMENTS(apszArgs) - 1, hrc = E_UNEXPECTED);
        pszLastArg = va_arg(va, const char *);
        if (!pszLastArg)
            break;
        LogRel((" '%s'", pszLastArg));
        apszArgs[cArgs++] = pszLastArg;
    };
    va_end(va);
    ComAssertComRCRet(hrc, hrc);

    LogRel(("\n"));
    apszArgs[cArgs] = NULL;

    /*
     * Create a PIPE which we attach to stderr so that we can read the error
     * message on failure and report it back to the caller.
     */
    RTPIPE      hPipeR;
    RTHANDLE    hStdErrPipe;
    hStdErrPipe.enmType = RTHANDLETYPE_PIPE;
    vrc = RTPipeCreate(&hPipeR, &hStdErrPipe.u.hPipe, RTPIPE_C_INHERIT_WRITE);
    AssertLogRelRCReturn(vrc, E_UNEXPECTED);

    /*
     * Spawn the process.
     */
    RTPROCESS hProcess;
    vrc = RTProcCreateEx(szExecName,
                         apszArgs,
                         RTENV_DEFAULT,
                         0 /*fFlags*/,
                         NULL /*phStdIn*/,
                         NULL /*phStdOut*/,
                         &hStdErrPipe,
                         NULL /*pszAsUser*/,
                         NULL /*pszPassword*/,
                         NULL /*pvExtraData*/,
                         &hProcess);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTPipeClose(hStdErrPipe.u.hPipe);
        hStdErrPipe.u.hPipe = NIL_RTPIPE;

        /*
         * Read the pipe output until the process completes.
         */
        RTPROCSTATUS    ProcStatus   = { -42, RTPROCEXITREASON_ABEND };
        size_t          cbStdErrBuf  = 0;
        size_t          offStdErrBuf = 0;
        char           *pszStdErrBuf = NULL;
        do
        {
            /*
             * Service the pipe. Block waiting for output or the pipe breaking
             * when the process terminates.
             */
            if (hPipeR != NIL_RTPIPE)
            {
                char    achBuf[1024];
                size_t  cbRead;
                vrc = RTPipeReadBlocking(hPipeR, achBuf, sizeof(achBuf), &cbRead);
                if (RT_SUCCESS(vrc))
                {
                    /* grow the buffer? */
                    size_t cbBufReq = offStdErrBuf + cbRead + 1;
                    if (   cbBufReq > cbStdErrBuf
                        && cbBufReq < _256K)
                    {
                        size_t cbNew = RT_ALIGN_Z(cbBufReq, 16); // 1024
                        void  *pvNew = RTMemRealloc(pszStdErrBuf, cbNew);
                        if (pvNew)
                        {
                            pszStdErrBuf = (char *)pvNew;
                            cbStdErrBuf  = cbNew;
                        }
                    }

                    /* append if we've got room. */
                    if (cbBufReq <= cbStdErrBuf)
                    {
                        memcpy(&pszStdErrBuf[offStdErrBuf], achBuf, cbRead);
                        offStdErrBuf = offStdErrBuf + cbRead;
                        pszStdErrBuf[offStdErrBuf] = '\0';
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
            if (hProcess != NIL_RTPROCESS)
            {
                vrc = RTProcWait(hProcess,
                                 hPipeR == NIL_RTPIPE ? RTPROCWAIT_FLAGS_BLOCK : RTPROCWAIT_FLAGS_NOBLOCK,
                                 &ProcStatus);
                if (RT_SUCCESS(vrc))
                    hProcess = NIL_RTPROCESS;
                else
                    AssertLogRelMsgStmt(vrc == VERR_PROCESS_RUNNING, ("%Rrc\n", vrc), hProcess = NIL_RTPROCESS);
            }
        } while (   hPipeR != NIL_RTPIPE
                 || hProcess != NIL_RTPROCESS);

        LogRel(("ExtPack: enmReason=%d iStatus=%d stderr='%s'\n",
                ProcStatus.enmReason, ProcStatus.iStatus, offStdErrBuf ? pszStdErrBuf : ""));

        /*
         * Look for rcExit=RTEXITCODE_SUCCESS at the end of the error output,
         * cut it as it is only there to attest the success.
         */
        if (offStdErrBuf > 0)
        {
            RTStrStripR(pszStdErrBuf);
            offStdErrBuf = strlen(pszStdErrBuf);
        }

        char *pszSuccessInd = findSuccessIndicator(pszStdErrBuf, offStdErrBuf);
        if (pszSuccessInd)
        {
            *pszSuccessInd = '\0';
            offStdErrBuf  = (size_t)(pszSuccessInd - pszStdErrBuf);
        }
        else if (   ProcStatus.enmReason == RTPROCEXITREASON_NORMAL
                 && ProcStatus.iStatus   == 0)
            ProcStatus.iStatus = offStdErrBuf ? 667 : 666;

        /*
         * Compose the status code and, on failure, error message.
         */
        if (   ProcStatus.enmReason == RTPROCEXITREASON_NORMAL
            && ProcStatus.iStatus   == 0)
            hrc = S_OK;
        else if (ProcStatus.enmReason == RTPROCEXITREASON_NORMAL)
        {
            AssertMsg(ProcStatus.iStatus != 0, ("%s\n", pszStdErrBuf));
            hrc = setError(E_FAIL, tr("The installer failed with exit code %d: %s"),
                           ProcStatus.iStatus, offStdErrBuf ? pszStdErrBuf : "");
        }
        else if (ProcStatus.enmReason == RTPROCEXITREASON_SIGNAL)
            hrc = setError(E_UNEXPECTED, tr("The installer was killed by signal #d (stderr: %s)"),
                           ProcStatus.iStatus, offStdErrBuf ? pszStdErrBuf : "");
        else if (ProcStatus.enmReason == RTPROCEXITREASON_ABEND)
            hrc = setError(E_UNEXPECTED, tr("The installer aborted abnormally (stderr: %s)"),
                           offStdErrBuf ? pszStdErrBuf : "");
        else
            hrc = setError(E_UNEXPECTED, tr("internal error: enmReason=%d iStatus=%d stderr='%s'"),
                           ProcStatus.enmReason, ProcStatus.iStatus, offStdErrBuf ? pszStdErrBuf : "");

        RTMemFree(pszStdErrBuf);
    }
    else
        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Failed to launch the helper application '%s' (%Rrc)"), szExecName, vrc);

    RTPipeClose(hPipeR);
    RTPipeClose(hStdErrPipe.u.hPipe);

    return hrc;
}

/**
 * Finds an installed extension pack.
 *
 * @returns Pointer to the extension pack if found, NULL if not. (No reference
 *          counting problem here since the caller must be holding the lock.)
 * @param   a_pszName       The name of the extension pack.
 */
ExtPack *ExtPackManager::i_findExtPack(const char *a_pszName)
{
    size_t cchName = strlen(a_pszName);

    for (ExtPackList::iterator it = m->llInstalledExtPacks.begin();
         it != m->llInstalledExtPacks.end();
         ++it)
    {
        ExtPack::Data *pExtPackData = (*it)->m;
        if (   pExtPackData
            && pExtPackData->Desc.strName.length() == cchName
            && pExtPackData->Desc.strName.equalsIgnoreCase(a_pszName))
            return (*it);
    }
    return NULL;
}

/**
 * Removes an installed extension pack from the internal list.
 *
 * The package is expected to exist!
 *
 * @param   a_pszName       The name of the extension pack.
 */
void ExtPackManager::i_removeExtPack(const char *a_pszName)
{
    size_t cchName = strlen(a_pszName);

    for (ExtPackList::iterator it = m->llInstalledExtPacks.begin();
         it != m->llInstalledExtPacks.end();
         ++it)
    {
        ExtPack::Data *pExtPackData = (*it)->m;
        if (   pExtPackData
            && pExtPackData->Desc.strName.length() == cchName
            && pExtPackData->Desc.strName.equalsIgnoreCase(a_pszName))
        {
            m->llInstalledExtPacks.erase(it);
            m->cUpdate++;
            return;
        }
    }
    AssertMsgFailed(("%s\n", a_pszName));
}

#ifndef VBOX_COM_INPROC

/**
 * Refreshes the specified extension pack.
 *
 * This may remove the extension pack from the list, so any non-smart pointers
 * to the extension pack object may become invalid.
 *
 * @returns S_OK and *a_ppExtPack on success, COM status code and error
 *          message on failure.  Note that *a_ppExtPack can be NULL.
 *
 * @param   a_pszName           The extension to update..
 * @param   a_fUnusableIsError  If @c true, report an unusable extension pack
 *                              as an error.
 * @param   a_ppExtPack         Where to store the pointer to the extension
 *                              pack of it is still around after the refresh.
 *                              This is optional.
 *
 * @remarks Caller holds the extension manager lock.
 * @remarks Only called in VBoxSVC.
 */
HRESULT ExtPackManager::i_refreshExtPack(const char *a_pszName, bool a_fUnusableIsError, ExtPack **a_ppExtPack)
{
    Assert(m->pVirtualBox != NULL); /* Only called from VBoxSVC. */

    HRESULT hrc;
    ExtPack *pExtPack = i_findExtPack(a_pszName);
    if (pExtPack)
    {
        /*
         * Refresh existing object.
         */
        bool fCanDelete;
        hrc = pExtPack->i_refresh(&fCanDelete);
        if (SUCCEEDED(hrc))
        {
            if (fCanDelete)
            {
                i_removeExtPack(a_pszName);
                pExtPack = NULL;
            }
        }
    }
    else
    {
        /*
         * Do this check here, otherwise VBoxExtPackCalcDir() will fail with a strange
         * error.
         */
        bool fValid = VBoxExtPackIsValidName(a_pszName);
        if (!fValid)
            return setError(E_FAIL, "Invalid extension pack name specified");

        /*
         * Does the dir exist?  Make some special effort to deal with case
         * sensitivie file systems (a_pszName is case insensitive and mangled).
         */
        char szDir[RTPATH_MAX];
        int vrc = VBoxExtPackCalcDir(szDir, sizeof(szDir), m->strBaseDir.c_str(), a_pszName);
        AssertLogRelRCReturn(vrc, E_FAIL);

        RTDIRENTRYEX    Entry;
        RTFSOBJINFO     ObjInfo;
        vrc = RTPathQueryInfoEx(szDir, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
        bool fExists = RT_SUCCESS(vrc) && RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode);
        if (!fExists)
        {
            RTDIR hDir;
            vrc = RTDirOpen(&hDir, m->strBaseDir.c_str());
            if (RT_SUCCESS(vrc))
            {
                const char *pszMangledName = RTPathFilename(szDir);
                for (;;)
                {
                    vrc = RTDirReadEx(hDir, &Entry, NULL /*pcbDirEntry*/, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                    if (RT_FAILURE(vrc))
                    {
                        AssertLogRelMsg(vrc == VERR_NO_MORE_FILES, ("%Rrc\n", vrc));
                        break;
                    }
                    if (   RTFS_IS_DIRECTORY(Entry.Info.Attr.fMode)
                        && !RTStrICmp(Entry.szName, pszMangledName))
                    {
                        /*
                         * The installed extension pack has a uses different case.
                         * Update the name and directory variables.
                         */
                        vrc = RTPathJoin(szDir, sizeof(szDir), m->strBaseDir.c_str(), Entry.szName); /* not really necessary */
                        AssertLogRelRCReturnStmt(vrc, RTDirClose(hDir), E_UNEXPECTED);
                        a_pszName = Entry.szName;
                        fExists   = true;
                        break;
                    }
                }
                RTDirClose(hDir);
            }
        }
        if (fExists)
        {
            /*
             * We've got something, create a new extension pack object for it.
             */
            ComObjPtr<ExtPack> ptrNewExtPack;
            hrc = ptrNewExtPack.createObject();
            if (SUCCEEDED(hrc))
                hrc = ptrNewExtPack->initWithDir(m->pVirtualBox, m->enmContext, a_pszName, szDir);
            if (SUCCEEDED(hrc))
            {
                m->llInstalledExtPacks.push_back(ptrNewExtPack);
                m->cUpdate++;
                if (ptrNewExtPack->m->fUsable)
                    LogRel(("ExtPackManager: Found extension pack '%s'.\n", a_pszName));
                else
                    LogRel(("ExtPackManager: Found bad extension pack '%s': %s\n",
                            a_pszName, ptrNewExtPack->m->strWhyUnusable.c_str() ));
                pExtPack = ptrNewExtPack;
            }
        }
        else
            hrc = S_OK;
    }

    /*
     * Report error if not usable, if that is desired.
     */
    if (   SUCCEEDED(hrc)
        && pExtPack
        && a_fUnusableIsError
        && !pExtPack->m->fUsable)
        hrc = setError(E_FAIL, "%s", pExtPack->m->strWhyUnusable.c_str());

    if (a_ppExtPack)
        *a_ppExtPack = pExtPack;
    return hrc;
}

/**
 * Checks if there are any running VMs.
 *
 * This is called when uninstalling or replacing an extension pack.
 *
 * @returns true / false
 */
bool ExtPackManager::i_areThereAnyRunningVMs(void) const
{
    Assert(m->pVirtualBox != NULL); /* Only called from VBoxSVC. */

    /*
     * Get list of machines and their states.
     */
    com::SafeIfaceArray<IMachine> SaMachines;
    HRESULT hrc = m->pVirtualBox->COMGETTER(Machines)(ComSafeArrayAsOutParam(SaMachines));
    if (SUCCEEDED(hrc))
    {
        com::SafeArray<MachineState_T> SaStates;
        hrc = m->pVirtualBox->GetMachineStates(ComSafeArrayAsInParam(SaMachines), ComSafeArrayAsOutParam(SaStates));
        if (SUCCEEDED(hrc))
        {
            /*
             * Scan the two parallel arrays for machines in the running state.
             */
            Assert(SaStates.size() == SaMachines.size());
            for (size_t i = 0; i < SaMachines.size(); ++i)
                if (SaMachines[i] && Global::IsOnline(SaStates[i]))
                    return true;
        }
    }
    return false;
}

/**
 * Worker for IExtPackFile::Install.
 *
 * Called on a worker thread via doInstallThreadProc.
 *
 * @returns COM status code.
 * @param   a_pExtPackFile      The extension pack file, caller checks that
 *                              it's usable.
 * @param   a_fReplace          Whether to replace any existing extpack or just
 *                              fail.
 * @param   a_pstrDisplayInfo   Host specific display information hacks.
 *                              be NULL.
 */
HRESULT ExtPackManager::i_doInstall(ExtPackFile *a_pExtPackFile, bool a_fReplace, Utf8Str const *a_pstrDisplayInfo)
{
    AssertReturn(m->enmContext == VBOXEXTPACKCTX_PER_USER_DAEMON, E_UNEXPECTED);
    RTCString const * const pStrName          = &a_pExtPackFile->m->Desc.strName;
    RTCString const * const pStrTarball       = &a_pExtPackFile->m->strExtPackFile;
    RTCString const * const pStrTarballDigest = &a_pExtPackFile->m->strDigest;

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock autoLock(this COMMA_LOCKVAL_SRC_POS);

        /*
         * Refresh the data we have on the extension pack as it
         * may be made stale by direct meddling or some other user.
         */
        ExtPack *pExtPack;
        hrc = i_refreshExtPack(pStrName->c_str(), false /*a_fUnusableIsError*/, &pExtPack);
        if (SUCCEEDED(hrc))
        {
            if (pExtPack && a_fReplace)
            {
                /* We must leave the lock when calling i_areThereAnyRunningVMs,
                   which means we have to redo the refresh call afterwards. */
                autoLock.release();
                bool fRunningVMs = i_areThereAnyRunningVMs();
                bool fVetoingCP = pExtPack->i_areThereCloudProviderUninstallVetos();
                bool fUnloadedCryptoMod = m->pVirtualBox->i_unloadCryptoIfModule() == S_OK;
                autoLock.acquire();
                hrc = i_refreshExtPack(pStrName->c_str(), false /*a_fUnusableIsError*/, &pExtPack);
                if (fRunningVMs)
                {
                    LogRel(("Upgrading extension pack '%s' failed because at least one VM is still running.", pStrName->c_str()));
                    hrc = setError(E_FAIL, tr("Upgrading extension pack '%s' failed because at least one VM is still running"),
                                   pStrName->c_str());
                }
                else if (fVetoingCP)
                {
                    LogRel(("Upgrading extension pack '%s' failed because at least one Cloud Provider is still busy.", pStrName->c_str()));
                    hrc = setError(E_FAIL, tr("Upgrading extension pack '%s' failed because at least one Cloud Provider is still busy"),
                                   pStrName->c_str());
                }
                else if (!fUnloadedCryptoMod)
                {
                    LogRel(("Upgrading extension pack '%s' failed because the cryptographic support module is still in use.", pStrName->c_str()));
                    hrc = setError(E_FAIL, tr("Upgrading extension pack '%s' failed because the cryptographic support module is still in use"),
                                   pStrName->c_str());
                }
                else if (SUCCEEDED(hrc) && pExtPack)
                    hrc = pExtPack->i_callUninstallHookAndClose(m->pVirtualBox, false /*a_ForcedRemoval*/);
            }
            else if (pExtPack)
                hrc = setError(E_FAIL,
                               tr("Extension pack '%s' is already installed."
                                  " In case of a reinstallation, please uninstall it first"),
                               pStrName->c_str());
        }
        if (SUCCEEDED(hrc))
        {
            /*
             * Run the privileged helper binary that performs the actual
             * installation.  Then create an object for the packet (we do this
             * even on failure, to be on the safe side).
             */
            hrc = i_runSetUidToRootHelper(a_pstrDisplayInfo,
                                          "install",
                                          "--base-dir",   m->strBaseDir.c_str(),
                                          "--cert-dir",   m->strCertificatDirPath.c_str(),
                                          "--name",       pStrName->c_str(),
                                          "--tarball",    pStrTarball->c_str(),
                                          "--sha-256",    pStrTarballDigest->c_str(),
                                          pExtPack ? "--replace" : (const char *)NULL,
                                          (const char *)NULL);
            if (SUCCEEDED(hrc))
            {
                hrc = i_refreshExtPack(pStrName->c_str(), true /*a_fUnusableIsError*/, &pExtPack);
                if (SUCCEEDED(hrc) && pExtPack)
                {
                    RTERRINFOSTATIC ErrInfo;
                    RTErrInfoInitStatic(&ErrInfo);
                    pExtPack->i_callInstalledHook(m->pVirtualBox, &autoLock, &ErrInfo.Core);
                    if (RT_SUCCESS(ErrInfo.Core.rc))
                    {
                        LogRel(("ExtPackManager: Successfully installed extension pack '%s'.\n", pStrName->c_str()));
                        m->pVirtualBox->i_onExtPackInstalled(*pStrName);
                    }
                    else
                    {
                        LogRel(("ExtPackManager: Installed hook for '%s' failed: %Rrc - %s\n",
                                pStrName->c_str(), ErrInfo.Core.rc, ErrInfo.Core.pszMsg));

                        /*
                         * Uninstall the extpack if the error indicates that.
                         */
                        if (ErrInfo.Core.rc == VERR_EXTPACK_UNSUPPORTED_HOST_UNINSTALL)
                            i_runSetUidToRootHelper(a_pstrDisplayInfo,
                                                    "uninstall",
                                                    "--base-dir", m->strBaseDir.c_str(),
                                                    "--name",     pStrName->c_str(),
                                                    "--forced",
                                                    (const char *)NULL);
                        hrc = setErrorBoth(E_FAIL, ErrInfo.Core.rc, tr("The installation hook failed: %Rrc - %s"),
                                           ErrInfo.Core.rc, ErrInfo.Core.pszMsg);
                    }
                }
                else if (SUCCEEDED(hrc))
                    hrc = setError(E_FAIL, tr("Installing extension pack '%s' failed under mysterious circumstances"),
                                   pStrName->c_str());
            }
            else
            {
                ErrorInfoKeeper Eik;
                i_refreshExtPack(pStrName->c_str(), false /*a_fUnusableIsError*/, NULL);
            }
        }

        /*
         * Do VirtualBoxReady callbacks now for any freshly installed
         * extension pack (old ones will not be called).
         */
        if (m->enmContext == VBOXEXTPACKCTX_PER_USER_DAEMON)
        {
            autoLock.release();
            i_callAllVirtualBoxReadyHooks();
        }
    }

    return hrc;
}

/**
 * Worker for IExtPackManager::Uninstall.
 *
 * Called on a worker thread via doUninstallThreadProc.
 *
 * @returns COM status code.
 * @param   a_pstrName          The name of the extension pack to uninstall.
 * @param   a_fForcedRemoval    Whether to be skip and ignore certain bits of
 *                              the extpack feedback.  To deal with misbehaving
 *                              extension pack hooks.
 * @param   a_pstrDisplayInfo   Host specific display information hacks.
 */
HRESULT ExtPackManager::i_doUninstall(Utf8Str const *a_pstrName, bool a_fForcedRemoval, Utf8Str const *a_pstrDisplayInfo)
{
    Assert(m->enmContext == VBOXEXTPACKCTX_PER_USER_DAEMON);

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock autoLock(this COMMA_LOCKVAL_SRC_POS);

        /*
         * Refresh the data we have on the extension pack as it
         * may be made stale by direct meddling or some other user.
         */
        ExtPack *pExtPack;
        hrc = i_refreshExtPack(a_pstrName->c_str(), false /*a_fUnusableIsError*/, &pExtPack);
        if (SUCCEEDED(hrc) && pExtPack)
        {
            /* We must leave the lock when calling i_areThereAnyRunningVMs,
               which means we have to redo the refresh call afterwards. */
            autoLock.release();
            bool fRunningVMs = i_areThereAnyRunningVMs();
            bool fVetoingCP = pExtPack->i_areThereCloudProviderUninstallVetos();
            bool fUnloadedCryptoMod = m->pVirtualBox->i_unloadCryptoIfModule() == S_OK;
            autoLock.acquire();
            if (a_fForcedRemoval || (!fRunningVMs && !fVetoingCP && fUnloadedCryptoMod))
            {
                hrc = i_refreshExtPack(a_pstrName->c_str(), false /*a_fUnusableIsError*/, &pExtPack);
                if (SUCCEEDED(hrc))
                {
                    if (!pExtPack)
                    {
                        LogRel(("ExtPackManager: Extension pack '%s' is not installed, so nothing to uninstall.\n", a_pstrName->c_str()));
                        hrc = S_OK;             /* nothing to uninstall */
                    }
                    else
                    {
                        /*
                         * Call the uninstall hook and unload the main dll.
                         */
                        hrc = pExtPack->i_callUninstallHookAndClose(m->pVirtualBox, a_fForcedRemoval);
                        if (SUCCEEDED(hrc))
                        {
                            /*
                             * Run the set-uid-to-root binary that performs the
                             * uninstallation.  Then refresh the object.
                             *
                             * This refresh is theorically subject to races, but it's of
                             * the don't-do-that variety.
                             */
                            const char *pszForcedOpt = a_fForcedRemoval ? "--forced" : NULL;
                            hrc = i_runSetUidToRootHelper(a_pstrDisplayInfo,
                                                          "uninstall",
                                                          "--base-dir", m->strBaseDir.c_str(),
                                                          "--name",     a_pstrName->c_str(),
                                                          pszForcedOpt, /* Last as it may be NULL. */
                                                          (const char *)NULL);
                            if (SUCCEEDED(hrc))
                            {
                                hrc = i_refreshExtPack(a_pstrName->c_str(), false /*a_fUnusableIsError*/, &pExtPack);
                                if (SUCCEEDED(hrc))
                                {
                                    if (!pExtPack)
                                    {
                                        LogRel(("ExtPackManager: Successfully uninstalled extension pack '%s'.\n", a_pstrName->c_str()));
                                        m->pVirtualBox->i_onExtPackInstalled(*a_pstrName);
                                    }
                                    else
                                        hrc = setError(E_FAIL,
                                                       tr("Uninstall extension pack '%s' failed under mysterious circumstances"),
                                                       a_pstrName->c_str());
                                }
                            }
                            else
                            {
                                ErrorInfoKeeper Eik;
                                i_refreshExtPack(a_pstrName->c_str(), false /*a_fUnusableIsError*/, NULL);
                            }
                        }
                    }
                }
            }
            else
            {
                if (fRunningVMs)
                {
                    LogRel(("Uninstall extension pack '%s' failed because at least one VM is still running.", a_pstrName->c_str()));
                    hrc = setError(E_FAIL, tr("Uninstall extension pack '%s' failed because at least one VM is still running"),
                                   a_pstrName->c_str());
                }
                else if (fVetoingCP)
                {
                    LogRel(("Uninstall extension pack '%s' failed because at least one Cloud Provider is still busy.", a_pstrName->c_str()));
                    hrc = setError(E_FAIL, tr("Uninstall extension pack '%s' failed because at least one Cloud Provider is still busy"),
                                   a_pstrName->c_str());
                }
                else if (!fUnloadedCryptoMod)
                {
                    LogRel(("Uninstall extension pack '%s' failed because the cryptographic support module is still in use.", a_pstrName->c_str()));
                    hrc = setError(E_FAIL, tr("Uninstall extension pack '%s' failed because the cryptographic support module is still in use"),
                                   a_pstrName->c_str());
                }
                else
                {
                    LogRel(("Uninstall extension pack '%s' failed for an unknown reason.", a_pstrName->c_str()));
                    hrc = setError(E_FAIL, tr("Uninstall extension pack '%s' failed for an unknown reason"),
                                   a_pstrName->c_str());

                }
            }
        }
        else if (SUCCEEDED(hrc) && !pExtPack)
        {
            hrc = setError(E_FAIL, tr("Extension pack '%s' is not installed.\n"), a_pstrName->c_str());
        }

        /*
         * Do VirtualBoxReady callbacks now for any freshly installed
         * extension pack (old ones will not be called).
         */
        if (m->enmContext == VBOXEXTPACKCTX_PER_USER_DAEMON)
        {
            autoLock.release();
            i_callAllVirtualBoxReadyHooks();
        }
    }

    return hrc;
}


/**
 * Calls the pfnVirtualBoxReady hook for all working extension packs.
 *
 * @remarks The caller must not hold any locks.
 */
void ExtPackManager::i_callAllVirtualBoxReadyHooks(void)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return;
    AutoWriteLock autoLock(this COMMA_LOCKVAL_SRC_POS);
    ComPtr<ExtPackManager> ptrSelfRef = this;

    for (ExtPackList::iterator it = m->llInstalledExtPacks.begin();
         it != m->llInstalledExtPacks.end();
         /* advancing below */)
    {
        if ((*it)->i_callVirtualBoxReadyHook(m->pVirtualBox, &autoLock))
            it = m->llInstalledExtPacks.begin();
        else
            ++it;
    }
}


/**
 * Queries objects of type @a aObjUuid from all the extension packs.
 *
 * @returns COM status code.
 * @param   aObjUuid        The UUID of the kind of objects we're querying.
 * @param   aObjects        Where to return the objects.
 * @param   a_pstrExtPackNames Where to return the corresponding extpack names (may be NULL).
 *
 * @remarks The caller must not hold any locks.
 */
HRESULT ExtPackManager::i_queryObjects(const com::Utf8Str &aObjUuid, std::vector<ComPtr<IUnknown> > &aObjects, std::vector<com::Utf8Str> *a_pstrExtPackNames)
{
    aObjects.clear();
    if (a_pstrExtPackNames)
        a_pstrExtPackNames->clear();

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock autoLock(this COMMA_LOCKVAL_SRC_POS);
        ComPtr<ExtPackManager> ptrSelfRef = this;

        for (ExtPackList::iterator it = m->llInstalledExtPacks.begin();
             it != m->llInstalledExtPacks.end();
             ++it)
        {
            ComPtr<IUnknown> ptrIf;
            HRESULT hrc2 = (*it)->queryObject(aObjUuid, ptrIf);
            if (SUCCEEDED(hrc2))
            {
                aObjects.push_back(ptrIf);
                if (a_pstrExtPackNames)
                    a_pstrExtPackNames->push_back((*it)->m->Desc.strName);
            }
            else if (hrc2 != E_NOINTERFACE)
                hrc = hrc2;
        }

        if (aObjects.size() > 0)
            hrc = S_OK;
    }
    return hrc;
}

#endif /* !VBOX_COM_INPROC */

#ifdef VBOX_COM_INPROC
/**
 * Calls the pfnConsoleReady hook for all working extension packs.
 *
 * @param   a_pConsole          The console interface.
 * @remarks The caller must not hold any locks.
 */
void ExtPackManager::i_callAllConsoleReadyHooks(IConsole *a_pConsole)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return;
    AutoWriteLock autoLock(this COMMA_LOCKVAL_SRC_POS);
    ComPtr<ExtPackManager> ptrSelfRef = this;

    for (ExtPackList::iterator it = m->llInstalledExtPacks.begin();
         it != m->llInstalledExtPacks.end();
         /* advancing below */)
    {
        if ((*it)->i_callConsoleReadyHook(a_pConsole, &autoLock))
            it = m->llInstalledExtPacks.begin();
        else
            ++it;
    }
}
#endif

#ifndef VBOX_COM_INPROC
/**
 * Calls the pfnVMCreated hook for all working extension packs.
 *
 * @param   a_pMachine          The machine interface of the new VM.
 */
void ExtPackManager::i_callAllVmCreatedHooks(IMachine *a_pMachine)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return;
    AutoWriteLock           autoLock(this COMMA_LOCKVAL_SRC_POS);
    ComPtr<ExtPackManager>  ptrSelfRef = this; /* paranoia */
    ExtPackList             llExtPacks = m->llInstalledExtPacks;

    for (ExtPackList::iterator it = llExtPacks.begin(); it != llExtPacks.end(); ++it)
        (*it)->i_callVmCreatedHook(m->pVirtualBox, a_pMachine, &autoLock);
}
#endif

#ifdef VBOX_COM_INPROC

/**
 * Calls the pfnVMConfigureVMM hook for all working extension packs.
 *
 * @returns VBox status code.  Stops on the first failure, expecting the caller
 *          to signal this to the caller of the CFGM constructor.
 * @param   a_pConsole  The console interface for the VM.
 * @param   a_pVM       The VM handle.
 * @param   a_pVMM      The VMM function table.
 */
int ExtPackManager::i_callAllVmConfigureVmmHooks(IConsole *a_pConsole, PVM a_pVM, PCVMMR3VTABLE a_pVMM)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return Global::vboxStatusCodeFromCOM(hrc);
    AutoWriteLock           autoLock(this COMMA_LOCKVAL_SRC_POS);
    ComPtr<ExtPackManager>  ptrSelfRef = this; /* paranoia */
    ExtPackList             llExtPacks = m->llInstalledExtPacks;

    for (ExtPackList::iterator it = llExtPacks.begin(); it != llExtPacks.end(); ++it)
    {
        int vrc;
        (*it)->i_callVmConfigureVmmHook(a_pConsole, a_pVM, a_pVMM, &autoLock, &vrc);
        if (RT_FAILURE(vrc))
            return vrc;
    }

    return VINF_SUCCESS;
}

/**
 * Calls the pfnVMPowerOn hook for all working extension packs.
 *
 * @returns VBox status code.  Stops on the first failure, expecting the caller
 *          to not power on the VM.
 * @param   a_pConsole  The console interface for the VM.
 * @param   a_pVM       The VM handle.
 * @param   a_pVMM      The VMM function table.
 */
int ExtPackManager::i_callAllVmPowerOnHooks(IConsole *a_pConsole, PVM a_pVM, PCVMMR3VTABLE a_pVMM)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return Global::vboxStatusCodeFromCOM(hrc);
    AutoWriteLock           autoLock(this COMMA_LOCKVAL_SRC_POS);
    ComPtr<ExtPackManager>  ptrSelfRef = this; /* paranoia */
    ExtPackList             llExtPacks = m->llInstalledExtPacks;

    for (ExtPackList::iterator it = llExtPacks.begin(); it != llExtPacks.end(); ++it)
    {
        int vrc;
        (*it)->i_callVmPowerOnHook(a_pConsole, a_pVM, a_pVMM, &autoLock, &vrc);
        if (RT_FAILURE(vrc))
            return vrc;
    }

    return VINF_SUCCESS;
}

/**
 * Calls the pfnVMPowerOff hook for all working extension packs.
 *
 * @param   a_pConsole  The console interface for the VM.
 * @param   a_pVM       The VM handle. Can be NULL.
 * @param   a_pVMM      The VMM function table.
 */
void ExtPackManager::i_callAllVmPowerOffHooks(IConsole *a_pConsole, PVM a_pVM, PCVMMR3VTABLE a_pVMM)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return;
    AutoWriteLock           autoLock(this COMMA_LOCKVAL_SRC_POS);
    ComPtr<ExtPackManager>  ptrSelfRef = this; /* paranoia */
    ExtPackList             llExtPacks = m->llInstalledExtPacks;

    for (ExtPackList::iterator it = llExtPacks.begin(); it != llExtPacks.end(); ++it)
        (*it)->i_callVmPowerOffHook(a_pConsole, a_pVM, a_pVMM, &autoLock);
}

#endif /* VBOX_COM_INPROC */

/**
 * Checks that the specified extension pack contains a VRDE module and that it
 * is shipshape.
 *
 * @returns S_OK if ok, appropriate failure status code with details.
 * @param   a_pstrExtPack   The name of the extension pack.
 */
HRESULT ExtPackManager::i_checkVrdeExtPack(Utf8Str const *a_pstrExtPack)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS);

        ExtPack *pExtPack = i_findExtPack(a_pstrExtPack->c_str());
        if (pExtPack)
            hrc = pExtPack->i_checkVrde();
        else
            hrc = setError(VBOX_E_OBJECT_NOT_FOUND, tr("No extension pack by the name '%s' was found"), a_pstrExtPack->c_str());
    }

    return hrc;
}

/**
 * Gets the full path to the VRDE library of the specified extension pack.
 *
 * This will do extacly the same as checkVrdeExtPack and then resolve the
 * library path.
 *
 * @returns VINF_SUCCESS if a path is returned, VBox error status and message
 *          return if not.
 * @param   a_pstrExtPack       The extension pack.
 * @param   a_pstrVrdeLibrary   Where to return the path.
 */
int ExtPackManager::i_getVrdeLibraryPathForExtPack(Utf8Str const *a_pstrExtPack, Utf8Str *a_pstrVrdeLibrary)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS);

        ExtPack *pExtPack = i_findExtPack(a_pstrExtPack->c_str());
        if (pExtPack)
            hrc = pExtPack->i_getVrdpLibraryName(a_pstrVrdeLibrary);
        else
            hrc = setError(VBOX_E_OBJECT_NOT_FOUND, tr("No extension pack by the name '%s' was found"),
                           a_pstrExtPack->c_str());
    }

    return Global::vboxStatusCodeFromCOM(hrc);
}

/**
 * Checks that the specified extension pack contains a cryptographic module and that it
 * is shipshape.
 *
 * @returns S_OK if ok, appropriate failure status code with details.
 * @param   a_pstrExtPack   The name of the extension pack.
 */
HRESULT ExtPackManager::i_checkCryptoExtPack(Utf8Str const *a_pstrExtPack)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS);

        ExtPack *pExtPack = i_findExtPack(a_pstrExtPack->c_str());
        if (pExtPack)
            hrc = pExtPack->i_checkCrypto();
        else
            hrc = setError(VBOX_E_OBJECT_NOT_FOUND, tr("No extension pack by the name '%s' was found"), a_pstrExtPack->c_str());
    }

    return hrc;
}

/**
 * Gets the full path to the cryptographic library of the specified extension pack.
 *
 * This will do extacly the same as checkCryptoExtPack and then resolve the
 * library path.
 *
 * @returns VINF_SUCCESS if a path is returned, VBox error status and message
 *          return if not.
 * @param   a_pstrExtPack       The extension pack.
 * @param   a_pstrCryptoLibrary Where to return the path.
 */
int ExtPackManager::i_getCryptoLibraryPathForExtPack(Utf8Str const *a_pstrExtPack, Utf8Str *a_pstrCryptoLibrary)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS);

        ExtPack *pExtPack = i_findExtPack(a_pstrExtPack->c_str());
        if (pExtPack)
            hrc = pExtPack->i_getCryptoLibraryName(a_pstrCryptoLibrary);
        else
            hrc = setError(VBOX_E_OBJECT_NOT_FOUND, tr("No extension pack by the name '%s' was found"),
                           a_pstrExtPack->c_str());
    }

    return Global::vboxStatusCodeFromCOM(hrc);
}


/**
 * Gets the full path to the specified library of the specified extension pack.
 *
 * @returns S_OK if a path is returned, COM error status and message return if
 *          not.
 * @param   a_pszModuleName     The library.
 * @param   a_pszExtPack        The extension pack.
 * @param   a_pstrLibrary       Where to return the path.
 */
HRESULT ExtPackManager::i_getLibraryPathForExtPack(const char *a_pszModuleName, const char *a_pszExtPack, Utf8Str *a_pstrLibrary)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS);

        ExtPack *pExtPack = i_findExtPack(a_pszExtPack);
        if (pExtPack)
            hrc = pExtPack->i_getLibraryName(a_pszModuleName, a_pstrLibrary);
        else
            hrc = setError(VBOX_E_OBJECT_NOT_FOUND, tr("No extension pack by the name '%s' was found"), a_pszExtPack);
    }

    return hrc;
}

/**
 * Gets the name of the default VRDE extension pack.
 *
 * @returns S_OK or some COM error status on red tape failure.
 * @param   a_pstrExtPack   Where to return the extension pack name.  Returns
 *                          empty if no extension pack wishes to be the default
 *                          VRDP provider.
 */
HRESULT ExtPackManager::i_getDefaultVrdeExtPack(Utf8Str *a_pstrExtPack)
{
    a_pstrExtPack->setNull();

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS);

        for (ExtPackList::iterator it = m->llInstalledExtPacks.begin();
             it != m->llInstalledExtPacks.end();
             ++it)
        {
            if ((*it)->i_wantsToBeDefaultVrde())
            {
                *a_pstrExtPack = (*it)->m->Desc.strName;
                break;
            }
        }
    }
    return hrc;
}

/**
 * Gets the name of the default cryptographic extension pack.
 *
 * @returns S_OK or some COM error status on red tape failure.
 * @param   a_pstrExtPack   Where to return the extension pack name.  Returns
 *                          empty if no extension pack wishes to be the default
 *                          VRDP provider.
 */
HRESULT ExtPackManager::i_getDefaultCryptoExtPack(Utf8Str *a_pstrExtPack)
{
    a_pstrExtPack->setNull();

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS);

        for (ExtPackList::iterator it = m->llInstalledExtPacks.begin();
             it != m->llInstalledExtPacks.end();
             ++it)
        {
            if ((*it)->i_wantsToBeDefaultCrypto())
            {
                *a_pstrExtPack = (*it)->m->Desc.strName;
                break;
            }
        }
    }
    return hrc;
}

/**
 * Checks if an extension pack is (present and) usable.
 *
 * @returns @c true if it is, otherwise @c false.
 * @param   a_pszExtPack    The name of the extension pack.
 */
bool ExtPackManager::i_isExtPackUsable(const char *a_pszExtPack)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return false;
    AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS);

    ExtPack *pExtPack = i_findExtPack(a_pszExtPack);
    return pExtPack != NULL
        && pExtPack->m->fUsable;
}

/**
 * Dumps all extension packs to the release log.
 */
void ExtPackManager::i_dumpAllToReleaseLog(void)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return;
    AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS);

    LogRel(("Installed Extension Packs:\n"));
    for (ExtPackList::iterator it = m->llInstalledExtPacks.begin();
         it != m->llInstalledExtPacks.end();
         ++it)
    {
        ExtPack::Data *pExtPackData = (*it)->m;
        if (pExtPackData)
        {
            if (pExtPackData->fUsable)
                LogRel(("  %s (Version: %s r%u%s%s; VRDE Module: %s; Crypto Module: %s)\n",
                        pExtPackData->Desc.strName.c_str(),
                        pExtPackData->Desc.strVersion.c_str(),
                        pExtPackData->Desc.uRevision,
                        pExtPackData->Desc.strEdition.isEmpty() ? "" : " ",
                        pExtPackData->Desc.strEdition.c_str(),
                        pExtPackData->Desc.strVrdeModule.c_str(),
                        pExtPackData->Desc.strCryptoModule.c_str() ));
            else
                LogRel(("  %s (Version: %s r%u%s%s; VRDE Module: %s; Crypto Module: %s unusable because of '%s')\n",
                        pExtPackData->Desc.strName.c_str(),
                        pExtPackData->Desc.strVersion.c_str(),
                        pExtPackData->Desc.uRevision,
                        pExtPackData->Desc.strEdition.isEmpty() ? "" : " ",
                        pExtPackData->Desc.strEdition.c_str(),
                        pExtPackData->Desc.strVrdeModule.c_str(),
                        pExtPackData->Desc.strCryptoModule.c_str(),
                        pExtPackData->strWhyUnusable.c_str() ));
        }
        else
            LogRel(("  pExtPackData is NULL\n"));
    }

    if (!m->llInstalledExtPacks.size())
        LogRel(("  None installed!\n"));
}

/**
 * Gets the update counter (reflecting extpack list updates).
 */
uint64_t ExtPackManager::i_getUpdateCounter(void)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return 0;
    AutoReadLock autoLock(this COMMA_LOCKVAL_SRC_POS);
    return m->cUpdate;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
