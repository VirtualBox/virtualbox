/* $Id: VBoxManage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - VirtualBox's command-line interface.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/NativeEventQueue.h>

#include <VBox/com/VirtualBox.h>

#ifdef VBOX_WITH_VBOXMANAGE_NLS
# include <VBox/com/AutoLock.h>
# include <VBox/com/listeners.h>
#endif

#include <VBox/version.h>

#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/log.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include <signal.h>

#include "VBoxManage.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The command doesn't need the COM stuff. */
#define VBMG_CMD_F_NO_COM       RT_BIT_32(0)

#define VBMG_CMD_INTERNAL       HELP_CMD_VBOXMANAGE_INVALID


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * VBoxManage command descriptor.
 */
typedef struct VBMGCMD
{
    /** The command.   */
    const char                 *pszCommand;
    /** The new help command. */
    enum HELP_CMD_VBOXMANAGE    enmCmdHelp;
    /** The handler. */
    RTEXITCODE                (*pfnHandler)(HandlerArg *pArg);
    /** VBMG_CMD_F_XXX,    */
    uint32_t                    fFlags;
} VBMGCMD;
/** Pointer to a const VBoxManage command descriptor. */
typedef VBMGCMD const *PCVBMGCMD;


DECLARE_TRANSLATION_CONTEXT(VBoxManage);

void setBuiltInHelpLanguage(const char *pszLang);

#ifdef VBOX_WITH_VBOXMANAGE_NLS
/* listener class for language updates */
class VBoxEventListener
{
public:
    VBoxEventListener()
    {}


    HRESULT init(void *)
    {
        return S_OK;
    }

    HRESULT init()
    {
        return S_OK;
    }

    void uninit()
    {
    }

    virtual ~VBoxEventListener()
    {
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        switch(aType)
        {
            case VBoxEventType_OnLanguageChanged:
            {
                /*
                 * Proceed with uttmost care as we might be racing com::Shutdown()
                 * and have the ground open up beneath us.
                 */
                LogFunc(("VBoxEventType_OnLanguageChanged\n"));
                VirtualBoxTranslator *pTranslator = VirtualBoxTranslator::tryInstance();
                if (pTranslator)
                {
                    ComPtr<ILanguageChangedEvent> pEvent = aEvent;
                    Assert(pEvent);

                    /* This call may fail if we're racing COM shutdown. */
                    com::Bstr bstrLanguageId;
                    HRESULT hrc = pEvent->COMGETTER(LanguageId)(bstrLanguageId.asOutParam());
                    if (SUCCEEDED(hrc))
                    {
                        try
                        {
                            com::Utf8Str strLanguageId(bstrLanguageId);
                            LogFunc(("New language ID: %s\n", strLanguageId.c_str()));
                            pTranslator->i_loadLanguage(strLanguageId.c_str());
                            setBuiltInHelpLanguage(strLanguageId.c_str());
                        }
                        catch (std::bad_alloc &)
                        {
                            LogFunc(("Caught bad_alloc"));
                        }
                    }
                    else
                        LogFunc(("Failed to get new language ID: %Rhrc\n", hrc));

                    pTranslator->release();
                }
                break;
            }

            default:
              AssertFailed();
        }

        return S_OK;
    }
};

typedef ListenerImpl<VBoxEventListener> VBoxEventListenerImpl;

VBOX_LISTENER_DECLARE(VBoxEventListenerImpl)
#endif /* !VBOX_WITH_VBOXMANAGE_NLS */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/*extern*/ bool         g_fDetailedProgress = false;
/** Set by the signal handler. */
static volatile bool    g_fCanceled = false;


/**
 * All registered command handlers
 */
static const VBMGCMD g_aCommands[] =
{
    { "internalcommands",   VBMG_CMD_INTERNAL,          handleInternalCommands,     0 },
    { "list",               HELP_CMD_LIST,              handleList,                 0 },
    { "showvminfo",         HELP_CMD_SHOWVMINFO,        handleShowVMInfo,           0 },
    { "registervm",         HELP_CMD_REGISTERVM,        handleRegisterVM,           0 },
    { "unregistervm",       HELP_CMD_UNREGISTERVM,      handleUnregisterVM,         0 },
    { "clonevm",            HELP_CMD_CLONEVM,           handleCloneVM,              0 },
    { "movevm",             HELP_CMD_MOVEVM,            handleMoveVM,               0 },
#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    { "encryptvm",          HELP_CMD_ENCRYPTVM,         handleEncryptVM,            0 },
#endif
    { "mediumproperty",     HELP_CMD_MEDIUMPROPERTY,    handleMediumProperty,       0 },
    { "hdproperty",         HELP_CMD_MEDIUMPROPERTY,    handleMediumProperty,       0 }, /* backward compatibility */
    { "createmedium",       HELP_CMD_CREATEMEDIUM,      handleCreateMedium,         0 },
    { "createhd",           HELP_CMD_CREATEMEDIUM,      handleCreateMedium,         0 }, /* backward compatibility */
    { "createvdi",          HELP_CMD_CREATEMEDIUM,      handleCreateMedium,         0 }, /* backward compatibility */
    { "modifymedium",       HELP_CMD_MODIFYMEDIUM,      handleModifyMedium,         0 },
    { "modifyhd",           HELP_CMD_MODIFYMEDIUM,      handleModifyMedium,         0 }, /* backward compatibility */
    { "modifyvdi",          HELP_CMD_MODIFYMEDIUM,      handleModifyMedium,         0 }, /* backward compatibility */
    { "clonemedium",        HELP_CMD_CLONEMEDIUM,       handleCloneMedium,          0 },
    { "clonehd",            HELP_CMD_CLONEMEDIUM,       handleCloneMedium,          0 }, /* backward compatibility */
    { "clonevdi",           HELP_CMD_CLONEMEDIUM,       handleCloneMedium,          0 }, /* backward compatibility */
    { "encryptmedium",      HELP_CMD_ENCRYPTMEDIUM,     handleEncryptMedium,        0 },
    { "checkmediumpwd",     HELP_CMD_CHECKMEDIUMPWD,    handleCheckMediumPassword,  0 },
    { "createvm",           HELP_CMD_CREATEVM,          handleCreateVM,             0 },
    { "modifyvm",           HELP_CMD_MODIFYVM,          handleModifyVM,             0 },
    { "startvm",            HELP_CMD_STARTVM,           handleStartVM,              0 },
    { "controlvm",          HELP_CMD_CONTROLVM,         handleControlVM,            0 },
    { "unattended",         HELP_CMD_UNATTENDED,        handleUnattended,           0 },
    { "discardstate",       HELP_CMD_DISCARDSTATE,      handleDiscardState,         0 },
    { "adoptstate",         HELP_CMD_ADOPTSTATE,        handleAdoptState,           0 },
    { "snapshot",           HELP_CMD_SNAPSHOT,          handleSnapshot,             0 },
    { "closemedium",        HELP_CMD_CLOSEMEDIUM,       handleCloseMedium,          0 },
    { "storageattach",      HELP_CMD_STORAGEATTACH,     handleStorageAttach,        0 },
    { "storagectl",         HELP_CMD_STORAGECTL,        handleStorageController,    0 },
    { "showmediuminfo",     HELP_CMD_SHOWMEDIUMINFO,    handleShowMediumInfo,       0 },
    { "showhdinfo",         HELP_CMD_SHOWMEDIUMINFO,    handleShowMediumInfo,       0 }, /* backward compatibility */
    { "showvdiinfo",        HELP_CMD_SHOWMEDIUMINFO,    handleShowMediumInfo,       0 }, /* backward compatibility */
    { "mediumio",           HELP_CMD_MEDIUMIO,          handleMediumIO,             0 },
    { "getextradata",       HELP_CMD_GETEXTRADATA,      handleGetExtraData,         0 },
    { "setextradata",       HELP_CMD_SETEXTRADATA,      handleSetExtraData,         0 },
    { "setproperty",        HELP_CMD_SETPROPERTY,       handleSetProperty,          0 },
    { "usbfilter",          HELP_CMD_USBFILTER,         handleUSBFilter,            0 },
    { "sharedfolder",       HELP_CMD_SHAREDFOLDER,      handleSharedFolder,         0 },
#ifdef VBOX_WITH_GUEST_PROPS
    { "guestproperty",      HELP_CMD_GUESTPROPERTY,     handleGuestProperty,        0 },
#endif
#ifdef VBOX_WITH_GUEST_CONTROL
    { "guestcontrol",       HELP_CMD_GUESTCONTROL,      handleGuestControl,         0 },
#endif
    { "metrics",            HELP_CMD_METRICS,           handleMetrics,              0 },
    { "import",             HELP_CMD_IMPORT,            handleImportAppliance,      0 },
    { "export",             HELP_CMD_EXPORT,            handleExportAppliance,      0 },
    { "signova",            HELP_CMD_SIGNOVA,           handleSignAppliance,        VBMG_CMD_F_NO_COM },
#ifdef VBOX_WITH_NETFLT
    { "hostonlyif",         HELP_CMD_HOSTONLYIF,        handleHostonlyIf,           0 },
#endif
#ifdef VBOX_WITH_VMNET
    { "hostonlynet",        HELP_CMD_HOSTONLYNET,       handleHostonlyNet,          0 },
#endif
    { "dhcpserver",         HELP_CMD_DHCPSERVER,        handleDHCPServer,           0 },
#ifdef VBOX_WITH_NAT_SERVICE
    { "natnetwork",         HELP_CMD_NATNETWORK,        handleNATNetwork,           0 },
#endif
    { "extpack",            HELP_CMD_EXTPACK,           handleExtPack,              0 },
    { "bandwidthctl",       HELP_CMD_BANDWIDTHCTL,      handleBandwidthControl,     0 },
    { "debugvm",            HELP_CMD_DEBUGVM,           handleDebugVM,              0 },
    { "convertfromraw",     HELP_CMD_CONVERTFROMRAW,    handleConvertFromRaw,       VBMG_CMD_F_NO_COM },
    { "convertdd",          HELP_CMD_CONVERTFROMRAW,    handleConvertFromRaw,       VBMG_CMD_F_NO_COM },
    { "usbdevsource",       HELP_CMD_USBDEVSOURCE,      handleUSBDevSource,         0 },
    { "cloudprofile",       HELP_CMD_CLOUDPROFILE,      handleCloudProfile,         0 },
    { "cloud",              HELP_CMD_CLOUD,             handleCloud,                0 },
#ifdef VBOX_WITH_UPDATE_AGENT
    { "updatecheck",        HELP_CMD_UPDATECHECK,       handleUpdateCheck,          0 },
#endif
    { "modifynvram",        HELP_CMD_MODIFYNVRAM,       handleModifyNvram,          0 },
};

/**
 * Looks up a command by name.
 *
 * @returns Pointer to the command structure.
 * @param   pszCommand          Name of the command.
 */
static PCVBMGCMD lookupCommand(const char *pszCommand)
{
    if (pszCommand)
        for (uint32_t i = 0; i < RT_ELEMENTS(g_aCommands); i++)
            if (!strcmp(g_aCommands[i].pszCommand, pszCommand))
                return &g_aCommands[i];
    return NULL;
}


/**
 * Signal handler that sets g_fCanceled.
 *
 * This can be executed on any thread in the process, on Windows it may even be
 * a thread dedicated to delivering this signal.  Do not doing anything
 * unnecessary here.
 */
static void showProgressSignalHandler(int iSignal) RT_NOTHROW_DEF
{
    NOREF(iSignal);
    ASMAtomicWriteBool(&g_fCanceled, true);
}

/**
 * Print out progress on the console.
 *
 * This runs the main event queue every now and then to prevent piling up
 * unhandled things (which doesn't cause real problems, just makes things
 * react a little slower than in the ideal case).
 */
HRESULT showProgress(ComPtr<IProgress> progress, uint32_t fFlags)
{
    using namespace com;
    HRESULT hrc;

    AssertReturn(progress.isNotNull(), E_FAIL);

    /* grandfather the old callers */
    if (g_fDetailedProgress)
        fFlags = SHOW_PROGRESS_DETAILS;

    const bool fDetailed =  RT_BOOL(fFlags & SHOW_PROGRESS_DETAILS);
    const bool fOps      =  RT_BOOL(fFlags & SHOW_PROGRESS_OPS);
    const bool fQuiet    = !RT_BOOL(fFlags & (SHOW_PROGRESS | SHOW_PROGRESS_DETAILS | SHOW_PROGRESS_OPS));

    AssertReturn((!fDetailed && !fOps) || (fDetailed != fOps), E_INVALIDARG); /* Mutually exclusive. */

    BOOL fCompleted = FALSE;
    ULONG ulCurrentPercent = 0;
    ULONG ulLastPercent = 0;

    ULONG ulLastOperationPercent = (ULONG)-1;

    ULONG ulLastOperation = (ULONG)-1;
    Bstr bstrOperationDescription;

    NativeEventQueue::getMainEventQueue()->processEventQueue(0);

    ULONG cOperations = 1;
    hrc = progress->COMGETTER(OperationCount)(&cOperations);
    if (FAILED(hrc))
    {
        RTStrmPrintf(g_pStdErr, VBoxManage::tr("Progress object failure: %Rhrc\n"), hrc);
        RTStrmFlush(g_pStdErr);
        return hrc;
    }

    /*
     * Note: Outputting the progress info to stderr (g_pStdErr) is intentional
     *       to not get intermixed with other (raw) stdout data which might get
     *       written in the meanwhile.
     */

    if (fFlags & SHOW_PROGRESS_DESC)
    {
        com::Bstr bstrDescription;
        hrc = progress->COMGETTER(Description(bstrDescription.asOutParam()));
        if (FAILED(hrc))
        {
            RTStrmPrintf(g_pStdErr, VBoxManage::tr("Failed to get progress description: %Rhrc\n"), hrc);
            return hrc;
        }

        const char *pcszDescSep;
        if (fDetailed)          /* multiline output */
            pcszDescSep = "\n";
        else                    /* continues on the same line */
            pcszDescSep = ": ";

        RTStrmPrintf(g_pStdErr, "%ls%s", bstrDescription.raw(), pcszDescSep);
        RTStrmFlush(g_pStdErr);
    }

    if (!fQuiet && !fDetailed && !fOps)
    {
        RTStrmPrintf(g_pStdErr, "0%%...");
        RTStrmFlush(g_pStdErr);
    }

    /* setup signal handling if cancelable */
    bool fCanceledAlready = false;
    BOOL fCancelable;
    hrc = progress->COMGETTER(Cancelable)(&fCancelable);
    if (FAILED(hrc))
        fCancelable = FALSE;
    if (fCancelable)
    {
        signal(SIGINT,   showProgressSignalHandler);
        signal(SIGTERM,  showProgressSignalHandler);
#ifdef SIGBREAK
        signal(SIGBREAK, showProgressSignalHandler);
#endif
    }

    hrc = progress->COMGETTER(Completed(&fCompleted));
    while (SUCCEEDED(hrc))
    {
        progress->COMGETTER(Percent(&ulCurrentPercent));

        if (   fDetailed
            || fOps)
        {
            ULONG ulOperation = 1;
            hrc = progress->COMGETTER(Operation)(&ulOperation);
            if (FAILED(hrc))
                break;
            ULONG ulCurrentOperationPercent = 0;
            hrc = progress->COMGETTER(OperationPercent(&ulCurrentOperationPercent));
            if (FAILED(hrc))
                break;

            if (ulLastOperation != ulOperation)
            {
                hrc = progress->COMGETTER(OperationDescription(bstrOperationDescription.asOutParam()));
                if (FAILED(hrc))
                    break;
                ulLastPercent = (ULONG)-1;        // force print
                ulLastOperation = ulOperation;
            }

            if (    ulCurrentPercent != ulLastPercent
                 || ulCurrentOperationPercent != ulLastOperationPercent
               )
            {
                LONG lSecsRem = 0;
                progress->COMGETTER(TimeRemaining)(&lSecsRem);

                if (fDetailed)
                    RTStrmPrintf(g_pStdErr, VBoxManage::tr("(%u/%u) %ls %02u%% => %02u%% (%d s remaining)\n"), ulOperation + 1, cOperations,
                                 bstrOperationDescription.raw(), ulCurrentOperationPercent, ulCurrentPercent, lSecsRem);
                else
                    RTStrmPrintf(g_pStdErr, VBoxManage::tr("%02u%%: %ls\n"), ulCurrentPercent, bstrOperationDescription.raw());

                ulLastPercent = ulCurrentPercent;
                ulLastOperationPercent = ulCurrentOperationPercent;
            }
        }
        else if (!fQuiet)
        {
            /* did we cross a 10% mark? */
            if (ulCurrentPercent / 10  >  ulLastPercent / 10)
            {
                /* make sure to also print out missed steps */
                for (ULONG curVal = (ulLastPercent / 10) * 10 + 10; curVal <= (ulCurrentPercent / 10) * 10; curVal += 10)
                {
                    if (curVal < 100)
                    {
                        RTStrmPrintf(g_pStdErr, "%u%%...", curVal);
                        RTStrmFlush(g_pStdErr);
                    }
                }
                ulLastPercent = (ulCurrentPercent / 10) * 10;
            }
        }
        if (fCompleted)
            break;

        /* process async cancelation */
        if (g_fCanceled && !fCanceledAlready)
        {
            hrc = progress->Cancel();
            if (SUCCEEDED(hrc))
                fCanceledAlready = true;
            else
                g_fCanceled = false;
        }

        /* make sure the loop is not too tight */
        progress->WaitForCompletion(100);

        NativeEventQueue::getMainEventQueue()->processEventQueue(0);
        hrc = progress->COMGETTER(Completed(&fCompleted));
    }

    /* undo signal handling */
    if (fCancelable)
    {
        signal(SIGINT,   SIG_DFL);
        signal(SIGTERM,  SIG_DFL);
# ifdef SIGBREAK
        signal(SIGBREAK, SIG_DFL);
# endif
    }

    /* complete the line. */
    LONG iRc = E_FAIL;
    hrc = progress->COMGETTER(ResultCode)(&iRc);
    if (SUCCEEDED(hrc))
    {
        /* async operation completed successfully */
        if (SUCCEEDED(iRc))
        {
            if (!fDetailed)
            {
                if (fFlags == SHOW_PROGRESS_DESC)
                    RTStrmPrintf(g_pStdErr, "ok\n");
                else if (!fQuiet)
                    RTStrmPrintf(g_pStdErr, "100%%\n");
            }
        }
        else if (g_fCanceled)
            RTStrmPrintf(g_pStdErr, VBoxManage::tr("CANCELED\n"));
        else
        {
            if (fDetailed)
                RTStrmPrintf(g_pStdErr, VBoxManage::tr("Progress state: %Rhrc\n"), iRc);
            else if (fFlags != SHOW_PROGRESS_NONE)
                RTStrmPrintf(g_pStdErr, "%Rhrc\n", iRc);
        }
        hrc = iRc;
    }
    else
    {
        if (!fDetailed)
            RTStrmPrintf(g_pStdErr, "\n");
        RTStrmPrintf(g_pStdErr, VBoxManage::tr("Progress object failure: %Rhrc\n"), hrc);
    }
    RTStrmFlush(g_pStdErr);
    return hrc;
}


void setBuiltInHelpLanguage(const char *pszLang)
{
#ifdef VBOX_WITH_VBOXMANAGE_NLS
    if (pszLang == NULL || pszLang[0] == '\0' || (pszLang[0] == 'C' && pszLang[1] == '\0'))
        pszLang = "en_US";

    /* find language entry matching exactly pszLang */
    PCHELP_LANG_ENTRY_T pHelpLangEntry = NULL;
    for (uint32_t i = 0; i < g_cHelpLangEntries; i++)
    {
        if (strcmp(g_aHelpLangEntries[i].pszLang, pszLang) == 0)
        {
            pHelpLangEntry = &g_aHelpLangEntries[i];
            break;
        }
    }

    /* find first entry containing language specified if pszLang contains only language */
    if (pHelpLangEntry == NULL)
    {
        size_t const cchLang = strlen(pszLang);
        for (uint32_t i = 0; i < g_cHelpLangEntries; i++)
        {
            if (   cchLang < g_aHelpLangEntries[i].cchLang
                && memcmp(g_aHelpLangEntries[i].pszLang, pszLang, cchLang) == 0)
            {
                pHelpLangEntry = &g_aHelpLangEntries[i];
                break;
            }
        }
    }

    /* set to en_US (i.e. untranslated) if not found */
    if (pHelpLangEntry == NULL)
        pHelpLangEntry = &g_aHelpLangEntries[0];

    ASMAtomicWritePtr(&g_pHelpLangEntry, pHelpLangEntry);
#else
    NOREF(pszLang);
#endif
}


int main(int argc, char *argv[])
{
    /*
     * Before we do anything, init the runtime without loading
     * the support driver.
     */
    int vrc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(vrc))
        return RTMsgInitFailure(vrc);
#if defined(RT_OS_WINDOWS)
    ATL::CComModule _Module; /* Required internally by ATL (constructor records instance in global variable). */
#endif

#ifdef VBOX_WITH_VBOXMANAGE_NLS
    /*
     * Initialize the translator and associated fun.
     */
    util::InitAutoLockSystem();
    ComObjPtr<VBoxEventListenerImpl> ptrEventListner;
    PTRCOMPONENT          pTrComponent = NULL;
    VirtualBoxTranslator *pTranslator  = VirtualBoxTranslator::instance();
    if (pTranslator != NULL)
    {
        char szNlsPath[RTPATH_MAX];
        vrc = RTPathAppPrivateNoArch(szNlsPath, sizeof(szNlsPath));
        if (RT_SUCCESS(vrc))
            vrc = RTPathAppend(szNlsPath, sizeof(szNlsPath), "nls" RTPATH_SLASH_STR "VBoxManageNls");
        if (RT_SUCCESS(vrc))
        {
            vrc = pTranslator->registerTranslation(szNlsPath, true, &pTrComponent);
            if (RT_SUCCESS(vrc))
            {
                vrc = pTranslator->i_loadLanguage(NULL);
                if (RT_SUCCESS(vrc))
                {
                    com::Utf8Str strLang = pTranslator->language();
                    setBuiltInHelpLanguage(strLang.c_str());
                }
                else
                    RTMsgWarning("Load language failed: %Rrc\n", vrc);
            }
            else
                RTMsgWarning("Register translation failed: %Rrc\n", vrc);
        }
        else
            RTMsgWarning("Path constructing failed: %Rrc\n", vrc);
    }
#endif

    /*
     * Parse the global options
     */
    bool fShowLogo = false;
    bool fShowHelp = false;
    int  iCmd      = 1;
    int  iCmdArg;
    const char *pszSettingsPw = NULL;
    const char *pszSettingsPwFile = NULL;
    int         cResponseFileArgs     = 0;
    char      **papszResponseFileArgs = NULL;
    char      **papszNewArgv          = NULL;

    for (int i = 1; i < argc || argc <= iCmd; i++)
    {
        if (    argc <= iCmd
            ||  !strcmp(argv[i], "help")
            ||  !strcmp(argv[i], "--help")
            ||  !strcmp(argv[i], "-?")
            ||  !strcmp(argv[i], "-h")
            ||  !strcmp(argv[i], "-help"))
        {
            if (i >= argc - 1)
            {
                showLogo(g_pStdOut);
                printUsage(g_pStdOut);
                return 0;
            }
            fShowLogo = true;
            fShowHelp = true;
            iCmd++;
            continue;
        }

        if (   !strcmp(argv[i], "-V")
            || !strcmp(argv[i], "--version")
            || !strcmp(argv[i], "-v")       /* deprecated */
            || !strcmp(argv[i], "-version") /* deprecated */
            || !strcmp(argv[i], "-Version") /* deprecated */)
        {
            /* Print version number, and do nothing else. */
            RTPrintf("%sr%u\n", VBOX_VERSION_STRING, RTBldCfgRevision());
            return 0;
        }
        if (!strcmp(argv[i], "--dump-build-type"))
        {
            /* Print the build type, and do nothing else. (Used by ValKit to detect build type.) */
            RTPrintf("%s\n", RTBldCfgType());
            return 0;
        }

        if (   !strcmp(argv[i], "--dumpopts")
            || !strcmp(argv[i], "-dumpopts") /* deprecated */)
        {
            /* Special option to dump really all commands,
             * even the ones not understood on this platform. */
            printUsage(g_pStdOut);
            return 0;
        }

        if (   !strcmp(argv[i], "--nologo")
            || !strcmp(argv[i], "-q")
            || !strcmp(argv[i], "-nologo") /* deprecated */)
        {
            /* suppress the logo */
            fShowLogo = false;
            iCmd++;
        }
        else if (   !strcmp(argv[i], "--detailed-progress")
                 || !strcmp(argv[i], "-d"))
        {
            /* detailed progress report */
            g_fDetailedProgress = true;
            iCmd++;
        }
        else if (!strcmp(argv[i], "--settingspw"))
        {
            if (i >= argc - 1)
                return RTMsgErrorExit(RTEXITCODE_FAILURE, VBoxManage::tr("Password expected"));
            /* password for certain settings */
            pszSettingsPw = argv[i + 1];
            iCmd += 2;
        }
        else if (!strcmp(argv[i], "--settingspwfile"))
        {
            if (i >= argc-1)
                return RTMsgErrorExit(RTEXITCODE_FAILURE, VBoxManage::tr("No password file specified"));
            pszSettingsPwFile = argv[i+1];
            iCmd += 2;
        }
        else if (argv[i][0] == '@')
        {
            if (papszResponseFileArgs)
                return RTMsgErrorExitFailure(VBoxManage::tr("Only one response file allowed"));

            /* Load response file, making sure it's valid UTF-8. */
            char  *pszResponseFile;
            size_t cbResponseFile;
            vrc = RTFileReadAllEx(&argv[i][1], 0, RTFOFF_MAX, RTFILE_RDALL_O_DENY_NONE | RTFILE_RDALL_F_TRAILING_ZERO_BYTE,
                                  (void **)&pszResponseFile, &cbResponseFile);
            if (RT_FAILURE(vrc))
                return RTMsgErrorExitFailure(VBoxManage::tr("Error reading response file '%s': %Rrc"), &argv[i][1], vrc);
            vrc = RTStrValidateEncoding(pszResponseFile);
            if (RT_FAILURE(vrc))
            {
                RTFileReadAllFree(pszResponseFile, cbResponseFile);
                return RTMsgErrorExitFailure(VBoxManage::tr("Invalid response file ('%s') encoding: %Rrc"), &argv[i][1], vrc);
            }

            /* Parse it. */
            vrc = RTGetOptArgvFromString(&papszResponseFileArgs, &cResponseFileArgs, pszResponseFile,
                                         RTGETOPTARGV_CNV_QUOTE_BOURNE_SH, NULL);
            RTFileReadAllFree(pszResponseFile, cbResponseFile);
            if (RT_FAILURE(vrc))
                return RTMsgErrorExitFailure(VBoxManage::tr("Failed to parse response file '%s' (bourne shell style): %Rrc"), &argv[i][1], vrc);

            /* Construct new argv+argc with the response file arguments inserted. */
            int cNewArgs = argc + cResponseFileArgs;
            papszNewArgv = (char **)RTMemAllocZ((cNewArgs + 2) * sizeof(papszNewArgv[0]));
            if (!papszNewArgv)
                return RTMsgErrorExitFailure(VBoxManage::tr("out of memory"));
            memcpy(&papszNewArgv[0], &argv[0], sizeof(argv[0]) * (i + 1));
            memcpy(&papszNewArgv[i + 1], papszResponseFileArgs, sizeof(argv[0]) * cResponseFileArgs);
            memcpy(&papszNewArgv[i + 1 + cResponseFileArgs], &argv[i + 1], sizeof(argv[0]) * (argc - i - 1 + 1));
            argv = papszNewArgv;
            argc = argc + cResponseFileArgs;

            iCmd++;
        }
        else
            break;
    }

    iCmdArg = iCmd + 1;

    /*
     * Show the logo and lookup the command and deal with fShowHelp = true.
     */
    if (fShowLogo)
        showLogo(g_pStdOut);

    PCVBMGCMD pCmd = lookupCommand(argv[iCmd]);
    if (pCmd && pCmd->enmCmdHelp != VBMG_CMD_INTERNAL)
        setCurrentCommand(pCmd->enmCmdHelp);

    if (   pCmd
        && (   fShowHelp
            || argc - iCmdArg == 0))
    {
        if (pCmd->enmCmdHelp == VBMG_CMD_INTERNAL)
            printUsageInternalCmds(g_pStdOut);
        else if (fShowHelp)
            printHelp(g_pStdOut);
        else
            printUsage(g_pStdOut);
        return RTEXITCODE_FAILURE; /* error */
    }
    if (!pCmd)
    {
        if (!strcmp(argv[iCmd], "commands"))
        {
            RTPrintf(VBoxManage::tr("commands:\n"));
            for (unsigned i = 0; i < RT_ELEMENTS(g_aCommands); i++)
                if (   i == 0  /* skip backwards compatibility entries */
                    || (g_aCommands[i].enmCmdHelp != g_aCommands[i - 1].enmCmdHelp))
                    RTPrintf("    %s\n", g_aCommands[i].pszCommand);
            return RTEXITCODE_SUCCESS;
        }
        return errorSyntax(VBoxManage::tr("Invalid command '%s'"), argv[iCmd]);
    }

    RTEXITCODE rcExit;
    if (!(pCmd->fFlags & VBMG_CMD_F_NO_COM))
    {
        /*
         * Initialize COM.
         */
        using namespace com;
        HRESULT hrc = com::Initialize();
        if (FAILED(hrc))
        {
# ifdef VBOX_WITH_XPCOM
            if (hrc == NS_ERROR_FILE_ACCESS_DENIED)
            {
                char szHome[RTPATH_MAX] = "";
                com::GetVBoxUserHomeDirectory(szHome, sizeof(szHome));
                return RTMsgErrorExit(RTEXITCODE_FAILURE,
                       VBoxManage::tr("Failed to initialize COM because the global settings directory '%s' is not accessible!"), szHome);
            }
# endif
            return RTMsgErrorExit(RTEXITCODE_FAILURE, VBoxManage::tr("Failed to initialize COM! (hrc=%Rhrc)"), hrc);
        }


        /*
         * Get the remote VirtualBox object and create a local session object.
         */
        rcExit = RTEXITCODE_FAILURE;
        ComPtr<IVirtualBoxClient> virtualBoxClient;
        ComPtr<IVirtualBox> virtualBox;
        hrc = virtualBoxClient.createInprocObject(CLSID_VirtualBoxClient);
        if (SUCCEEDED(hrc))
            hrc = virtualBoxClient->COMGETTER(VirtualBox)(virtualBox.asOutParam());
        if (SUCCEEDED(hrc))
        {
#ifdef VBOX_WITH_VBOXMANAGE_NLS
            /* Load language settings from IVirtualBox. */
            if (pTranslator != NULL)
            {
                HRESULT hrc1 = pTranslator->loadLanguage(virtualBox);
                if (SUCCEEDED(hrc1))
                {
                    com::Utf8Str strLang = pTranslator->language();
                    setBuiltInHelpLanguage(strLang.c_str());
                }
                else
                    RTMsgWarning("Failed to load API language: %Rhrc", hrc1);

                /* VirtualBox language events registration. */
                ComPtr<IEventSource> pES;
                hrc1 = virtualBox->COMGETTER(EventSource)(pES.asOutParam());
                if (SUCCEEDED(hrc1))
                {
                    hrc1 = ptrEventListner.createObject();
                    if (SUCCEEDED(hrc1))
                        hrc1 = ptrEventListner->init(new VBoxEventListener());
                    if (SUCCEEDED(hrc1))
                    {
                        com::SafeArray<VBoxEventType_T> eventTypes;
                        eventTypes.push_back(VBoxEventType_OnLanguageChanged);
                        hrc1 = pES->RegisterListener(ptrEventListner, ComSafeArrayAsInParam(eventTypes), true);
                    }
                    if (FAILED(hrc1))
                    {
                        ptrEventListner.setNull();
                        RTMsgWarning("Failed to register event listener: %Rhrc", hrc1);
                    }
                }
            }
#endif

            ComPtr<ISession> session;
            hrc = session.createInprocObject(CLSID_Session);
            if (SUCCEEDED(hrc))
            {
                /* Session secret. */
                if (pszSettingsPw)
                    CHECK_ERROR2I_STMT(virtualBox, SetSettingsSecret(Bstr(pszSettingsPw).raw()), rcExit = RTEXITCODE_FAILURE);
                else if (pszSettingsPwFile)
                    rcExit = settingsPasswordFile(virtualBox, pszSettingsPwFile);
                else
                    rcExit = RTEXITCODE_SUCCESS;
                if (rcExit == RTEXITCODE_SUCCESS)
                {
                    /*
                     * Call the handler.
                     */
                    HandlerArg handlerArg = { argc - iCmdArg, &argv[iCmdArg], virtualBox, session };
                    rcExit = pCmd->pfnHandler(&handlerArg);

                    /* Although all handlers should always close the session if they open it,
                     * we do it here just in case if some of the handlers contains a bug --
                     * leaving the direct session not closed will turn the machine state to
                     * Aborted which may have unwanted side effects like killing the saved
                     * state file (if the machine was in the Saved state before). */
                    session->UnlockMachine();
                }

                NativeEventQueue::getMainEventQueue()->processEventQueue(0);
            }
            else
            {
                com::ErrorInfo info;
                RTMsgError(VBoxManage::tr("Failed to create a session object!"));
                if (!info.isFullAvailable() && !info.isBasicAvailable())
                    com::GluePrintRCMessage(hrc);
                else
                    com::GluePrintErrorInfo(info);
            }
        }
        else
        {
            com::ErrorInfo info;
            RTMsgError(VBoxManage::tr("Failed to create the VirtualBox object!"));
            if (!info.isFullAvailable() && !info.isBasicAvailable())
            {
                com::GluePrintRCMessage(hrc);
                RTMsgError(VBoxManage::tr("Most likely, the VirtualBox COM server is not running or failed to start."));
            }
            else
                com::GluePrintErrorInfo(info);
        }

#ifdef VBOX_WITH_VBOXMANAGE_NLS
        /* VirtualBox event callback unregistration. */
        if (ptrEventListner.isNotNull())
        {
            ComPtr<IEventSource> pES;
            HRESULT hrc1 = virtualBox->COMGETTER(EventSource)(pES.asOutParam());
            if (pES.isNotNull())
            {
                hrc1 = pES->UnregisterListener(ptrEventListner);
                if (FAILED(hrc1))
                    LogRel(("Failed to unregister listener, %Rhrc", hrc1));
            }
            ptrEventListner.setNull();
        }
#endif
        /*
         * Terminate COM, make sure the virtualBox object has been released.
         */
        virtualBox.setNull();
        virtualBoxClient.setNull();
        NativeEventQueue::getMainEventQueue()->processEventQueue(0);
        com::Shutdown();
    }
    else
    {
        /*
         * The command needs no COM.
         */
        HandlerArg  handlerArg;
        handlerArg.argc = argc - iCmdArg;
        handlerArg.argv = &argv[iCmdArg];
        rcExit = pCmd->pfnHandler(&handlerArg);
    }

#ifdef VBOX_WITH_VBOXMANAGE_NLS
    if (pTranslator != NULL)
    {
        pTranslator->release();
        pTranslator = NULL;
        pTrComponent = NULL;
    }
#endif

    if (papszResponseFileArgs)
    {
        RTGetOptArgvFree(papszResponseFileArgs);
        RTMemFree(papszNewArgv);
    }

    return rcExit;
}
