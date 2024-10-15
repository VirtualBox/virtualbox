/* $Id: main.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - The main() function.
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

/* Qt includes: */
#include <QApplication>
#include <QMessageBox>
#ifdef VBOX_WS_NIX
# ifndef Q_OS_SOLARIS
#  include <QFontDatabase>
# endif
#endif

/* GUI includes: */
#include "UICommon.h"
#include "UILoggingDefs.h"
#include "UIModalWindowManager.h"
#include "UIStarter.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils.h"
# include "UICocoaApplication.h"
#endif /* VBOX_WS_MAC */
#ifdef VBOX_WS_NIX
# include "UIVersion.h"
#endif

/* Other VBox includes: */
#include <iprt/buildconfig.h>
#include <iprt/stream.h>
#include <VBox/err.h>
#include <VBox/version.h>
#include <VBox/sup.h>
#if !defined(VBOX_WITH_HARDENING) || !defined(VBOX_RUNTIME_UI)
# include <iprt/initterm.h>
# ifdef VBOX_WS_MAC
#  include <iprt/asm.h>
# endif
#endif
#ifdef VBOX_WS_NIX
# include <iprt/env.h>
#endif
#ifdef VBOX_WITH_HARDENING
# include <iprt/ctype.h>
#endif
#if defined(VBOX_RUNTIME_UI) && defined(VBOX_WS_MAC)
# include <iprt/path.h>
#endif
#ifdef VBOX_WS_WIN
# include <VBox/com/microatl.h>
#endif
#if defined(VBOX_WITH_HARDENING) || defined(VBOX_WS_WIN)
# include <iprt/string.h>
#endif

/* Other includes: */
#ifdef VBOX_WS_MAC
# include <dlfcn.h>
# include <sys/mman.h>
#endif /* VBOX_WS_MAC */
#ifdef VBOX_WS_NIX
# include <dlfcn.h>
# include <unistd.h>
# include <X11/Xlib.h>
# include "VBoxUtils-nix.h"
# if defined(RT_OS_LINUX) && defined(DEBUG)
#  include <signal.h>
#  include <execinfo.h>
#  ifndef __USE_GNU
#   define __USE_GNU
#  endif /* !__USE_GNU */
#  include <ucontext.h>
#  ifdef RT_ARCH_AMD64
#   define REG_PC REG_RIP
#  else /* !RT_ARCH_AMD64 */
#   define REG_PC REG_EIP
#  endif /* !RT_ARCH_AMD64 */
# endif /* RT_OS_LINUX && DEBUG */
#endif /* VBOX_WS_NIX */


/* XXX Temporarily. Don't rely on the user to hack the Makefile himself! */
QString g_QStrHintLinuxNoMemory = QApplication::tr(
    "This error means that the kernel driver was either not able to "
    "allocate enough memory or that some mapping operation failed."
    );

QString g_QStrHintLinuxNoDriver = QApplication::tr(
    "The VirtualBox Linux kernel driver is either not loaded or not set "
    "up correctly. Please try setting it up again by executing<br/><br/>"
    "  <font color=blue>'/sbin/vboxconfig'</font><br/><br/>"
    "as root.<br/><br/>"
    "If your system has EFI Secure Boot enabled you may also need to sign "
    "the kernel modules (vboxdrv, vboxnetflt, vboxnetadp, vboxpci) before "
    "you can load them. Please see your Linux system's documentation for "
    "more information."
    );

QString g_QStrHintOtherWrongDriverVersion = QApplication::tr(
    "The VirtualBox kernel modules do not match this version of "
    "VirtualBox. The installation of VirtualBox was apparently not "
    "successful. Please try completely uninstalling and reinstalling "
    "VirtualBox."
    );

QString g_QStrHintLinuxWrongDriverVersion = QApplication::tr(
    "The VirtualBox kernel modules do not match this version of "
    "VirtualBox. The installation of VirtualBox was apparently not "
    "successful. Executing<br/><br/>"
    "  <font color=blue>'/sbin/vboxconfig'</font><br/><br/>"
    "may correct this. Make sure that you are not mixing builds "
    "of VirtualBox from different sources."
    );

QString g_QStrHintOtherNoDriver = QApplication::tr(
    "Make sure the kernel module has been loaded successfully."
    );

/* I hope this isn't (C), (TM) or (R) Microsoft support ;-) */
QString g_QStrHintReinstall = QApplication::tr(
    "Please try reinstalling VirtualBox."
    );


#ifdef VBOX_WS_NIX
/** X11: For versions of Xlib which are aware of multi-threaded environments this function
  *      calls for XInitThreads() which initializes Xlib support for concurrent threads.
  * @returns @c non-zero unless it is unsafe to make multi-threaded calls to Xlib.
  * @remarks This is a workaround for a bug on old Xlib versions, fixed in commit
  *          941f02e and released in Xlib version 1.1. We check for the symbol
  *          "xcb_connect" which was introduced in that version. */
static Status MakeSureMultiThreadingIsSafe()
{
    /* Success by default: */
    Status rc = 1;
    /* Get a global handle to process symbols: */
    void *pvProcess = dlopen(0, RTLD_GLOBAL | RTLD_LAZY);
    /* Initialize multi-thread environment only if we can obtain
     * an address of xcb_connect symbol in this process: */
    if (pvProcess && dlsym(pvProcess, "xcb_connect"))
        rc = XInitThreads();
    /* Close the handle: */
    if (pvProcess)
        dlclose(pvProcess);
    /* Return result: */
    return rc;
}

# if (defined(RT_OS_LINUX) && !defined(RT_ARCH_ARM64)) && defined(DEBUG)
/** X11, Linux, Debug: The signal handler that prints out a backtrace of the call stack.
  * @remarks The code is taken from http://www.linuxjournal.com/article/6391. */
static void BackTraceSignalHandler(int sig, siginfo_t *pInfo, void *pSecret)
{
    void *trace[16];
    char **messages = (char **)0;
    int i, iTraceSize = 0;
    ucontext_t *uc = (ucontext_t *)pSecret;

    /* Do something useful with siginfo_t: */
    if (sig == SIGSEGV)
        Log(("GUI: Got signal %d, faulty address is %p, from %p\n",
             sig, pInfo->si_addr, uc->uc_mcontext.gregs[REG_PC]));
    /* Or do nothing by default: */
    else
        Log(("GUI: Got signal %d\n", sig));

    /* Acquire backtrace of 16 lvls depth: */
    iTraceSize = backtrace(trace, 16);

    /* Overwrite sigaction with caller's address: */
    trace[1] = (void *)uc->uc_mcontext.gregs[REG_PC];

    /* Translate the addresses into an array of messages: */
    messages = backtrace_symbols(trace, iTraceSize);

    /* Skip first stack frame (points here): */
    Log(("GUI: [bt] Execution path:\n"));
    for (i = 1; i < iTraceSize; ++i)
        Log(("GUI: [bt] %s\n", messages[i]));

    exit(0);
}

/** X11, Linux, Debug: Installs signal handler printing backtrace of the call stack. */
static void InstallSignalHandler()
{
    struct sigaction sa;
    sa.sa_sigaction = BackTraceSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGBUS,  &sa, 0);
    sigaction(SIGUSR1, &sa, 0);
}
# endif /* RT_OS_LINUX && DEBUG */
#endif /* VBOX_WS_NIX */

/** Qt5 message handler, function that prints out
  * debug, warning, critical, fatal and system error messages.
  * @param  enmType     Holds the type of the message.
  * @param  context     Holds the message context.
  * @param  strMessage  Holds the message body. */
static void QtMessageOutput(QtMsgType enmType, const QMessageLogContext &context, const QString &strMessage)
{
    NOREF(context);
# ifndef VBOX_WS_NIX
    NOREF(strMessage);
# endif
    switch (enmType)
    {
        case QtDebugMsg:
            Log(("Qt DEBUG: %s\n", strMessage.toUtf8().constData()));
            break;
        case QtWarningMsg:
            Log(("Qt WARNING: %s\n", strMessage.toUtf8().constData()));
# ifdef VBOX_WS_NIX
            /* Needed for instance for the message ``cannot connect to X server'': */
            RTStrmPrintf(g_pStdErr, "Qt WARNING: %s\n", strMessage.toUtf8().constData());
# endif
            break;
        case QtCriticalMsg:
            Log(("Qt CRITICAL: %s\n", strMessage.toUtf8().constData()));
# ifdef VBOX_WS_NIX
            /* Needed for instance for the message ``cannot connect to X server'': */
            RTStrmPrintf(g_pStdErr, "Qt CRITICAL: %s\n", strMessage.toUtf8().constData());
# endif
            break;
        case QtFatalMsg:
            Log(("Qt FATAL: %s\n", strMessage.toUtf8().constData()));
# ifdef VBOX_WS_NIX
            /* Needed for instance for the message ``cannot connect to X server'': */
            RTStrmPrintf(g_pStdErr, "Qt FATAL: %s\n", strMessage.toUtf8().constData());
# endif
            break;
        case QtInfoMsg:
            /** @todo ignore? */
            break;
    }
}

/** Shows all available command line parameters. */
static void ShowHelp()
{
#ifndef VBOX_RUNTIME_UI
    static const char s_szTitle[] = VBOX_PRODUCT " Manager";
#else
    static const char s_szTitle[] = VBOX_PRODUCT " Runner";
#endif

    static const char s_szUsage[] =
#ifdef VBOX_RUNTIME_UI
        "Options:\n"
        "  --startvm=<vmname|UUID>    start a VM by specifying its UUID or name\n"
        "  --separate                 start a separate VM process\n"
        "  --normal                   keep normal (windowed) mode during startup\n"
        "  --fullscreen               switch to fullscreen mode during startup\n"
        "  --seamless                 switch to seamless mode during startup\n"
        "  --scale                    switch to scale mode during startup\n"
        "  --no-startvm-errormsgbox   do not show a message box for VM start errors\n"
        "  --restore-current          restore the current snapshot before starting\n"
        "  --restore-snapshot=<name>  restore the named snapshot before starting\n"
        "  --no-aggressive-caching    delays caching media info in VM processes\n"
        "  --fda=<image|none>         Mount the specified floppy image\n"
        "  --dvd=<image|none>         Mount the specified DVD image\n"
# ifdef VBOX_GUI_WITH_PIDFILE
        "  --pidfile=<file>           create a pidfile file when a VM is up and running\n"
# endif /* VBOX_GUI_WITH_PIDFILE */
# ifdef VBOX_WITH_DEBUGGER_GUI
        "  --dbg                      enable the GUI debug menu\n"
        "  --debug                    like --dbg and show debug windows at VM startup\n"
        "  --debug-command-line       like --dbg and show command line window at VM startup\n"
        "  --debug-statistics         like --dbg and show statistics window at VM startup\n"
        "  --statistics-expand=<pat>  expand the matching statistics (can be repeated)\n"
        "  --statistics-filter=<pat>  statistics filter\n"
        "  --statistics-config=<str>  statistics configuration\n"
        "  --no-debug                 disable the GUI debug menu and debug windows\n"
        "  --start-paused             start the VM in the paused state\n"
        "  --start-running            start the VM running (for overriding --debug*)\n"
# endif /* VBOX_WITH_DEBUGGER_GUI */
        "\n"
        "Expert options:\n"
        "  --execute-all-in-iem       For debugging the interpreted execution mode.\n"
        "  --driverless               Do not open the support driver (NEM or IEM mode).\n"
        "  --warp-pct=<pct>           time warp factor, 100%% (= 1.0) = normal speed\n"
        "  --no-keyboard-grabbing     Don't grab the keyboard on windows (for debugging).\n"
        "\n"
# ifdef VBOX_WITH_DEBUGGER_GUI
        "The following environment (and extra data) variables are evaluated:\n"
        "  VBOX_GUI_DBG_ENABLED (GUI/Dbg/Enabled)\n"
        "                             enable the GUI debug menu if set\n"
        "  VBOX_GUI_DBG_AUTO_SHOW (GUI/Dbg/AutoShow)\n"
        "                             show debug windows at VM startup\n"
        "  VBOX_GUI_NO_DEBUGGER\n"
        "                             disable the GUI debug menu and debug windows\n"
# endif /* VBOX_WITH_DEBUGGER_GUI */
#else
        "No special options.\n"
        "\n"
        "If you are looking for --startvm and related options, you need to use VirtualBoxVM.\n"
#endif
        ;

    RTPrintf("%s v%s\n"
             "Copyright (C) 2005-" VBOX_C_YEAR " " VBOX_VENDOR "\n"
             "\n"
             "%s",
             s_szTitle, RTBldCfgVersion(), s_szUsage);

#ifdef VBOX_WS_WIN
    /* Show message box. Modify the option list a little
     * to better make it fit in the upcoming dialog. */
    char szTitleWithVersion[sizeof(s_szTitle) + 128];
    char szMsg[sizeof(s_szUsage) + 128];
    char *pszDst = szMsg;
    size_t offSrc = 0;
    while (offSrc < sizeof(s_szUsage) - 1U)
    {
        char ch;
        if (   s_szUsage[offSrc]     == ' '
            && s_szUsage[offSrc + 1] == ' '
            && (   (ch = s_szUsage[offSrc + 2]) == '-' /* option line */
                || ch == 'V' /* env.var. line */))
        {
            /* Split option lines: */
            if (ch == '-')
            {
                offSrc += 2;
                size_t cchOption = 0;
                while (   s_szUsage[offSrc + cchOption] != ' '
                       || s_szUsage[offSrc + cchOption + 1] != ' ')
                    ++cchOption;

                memcpy(pszDst, &s_szUsage[offSrc], cchOption);
                offSrc += cchOption + 2;
                pszDst += cchOption;
            }
            /* Change environment variable indentation: */
            else
            {
                offSrc += 2;
                size_t cchLine = 0;
                while ((ch = s_szUsage[offSrc + cchLine]) != '\n' && ch != '\0')
                    ++cchLine;

                memcpy(pszDst, &s_szUsage[offSrc], cchLine);
                offSrc += cchLine + 1;
                pszDst += cchLine;
            }
            *pszDst++ = '\n';
            *pszDst++ = '\t';

            while (s_szUsage[offSrc] == ' ')
                ++offSrc;
        }

        /* Copy up to and including end of line: */
        while ((ch = s_szUsage[offSrc++]) != '\n' && ch != '\0')
            *pszDst++ = ch;
        *pszDst++ = ch;
    }
    *pszDst = '\0';

    RTStrPrintf(szTitleWithVersion, sizeof(szTitleWithVersion), "%s v%s - Command Line Options", s_szTitle, RTBldCfgVersion());
    MessageBoxExA(NULL /*hwndOwner*/, szMsg, szTitleWithVersion, MB_OK | MB_ICONINFORMATION,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
#endif /* VBOX_WS_WIN */
}

extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char ** /*envp*/)
{
#ifdef VBOX_WS_WIN
    ATL::CComModule _Module; /* Required internally by ATL (constructor records instance in global variable). */
#endif

    /* Failed result initially: */
    int iResultCode = 1;

    /* Start logging: */
    LogFlowFuncEnter();

    /* Simulate try-catch block: */
    do
    {
#ifdef VBOX_WS_NIX
        /* Make sure multi-threaded environment is safe: */
        if (!MakeSureMultiThreadingIsSafe())
            break;
        VBGHDISPLAYSERVERTYPE enmDisplayServerType = VBGHDisplayServerTypeDetect();
        /* Abort before instantiating QApplication in case no active display server can be found to
         * prevent QApplication from aborting:  */
        if (enmDisplayServerType == VBGHDISPLAYSERVERTYPE_NONE)
        {
            iResultCode = 1;
            Log(("We could not detect an active display server. Exiting."));
            RTStrmPrintf(g_pStdErr, "No active display server, X11 or Wayland, detected. Exiting.\n");
            break;
        }
        if (VBGHDisplayServerTypeIsXAvailable(enmDisplayServerType))
            /* Force using Qt platform plugin 'xcb', we have X11 specific code: */
            RTEnvSet("QT_QPA_PLATFORM", "xcb");
        else
            /* Assume pure Wayland (without a X server):*/
            RTEnvSet("QT_QPA_PLATFORM", "wayland");
#endif /* VBOX_WS_NIX */

        /* Console help preprocessing: */
        bool fHelpShown = false;
        for (int i = 0; i < argc; ++i)
        {
            if (   !strcmp(argv[i], "-h")
                || !strcmp(argv[i], "-?")
                || !strcmp(argv[i], "-help")
                || !strcmp(argv[i], "--help"))
            {
                fHelpShown = true;
                ShowHelp();
                break;
            }
        }
        if (fHelpShown)
        {
            iResultCode = 0;
            break;
        }

#ifdef VBOX_WITH_HARDENING
        /* Make sure the image verification code works: */
        SUPR3HardenedVerifyInit();
#endif /* VBOX_WITH_HARDENING */

#ifdef VBOX_WS_MAC
        /* Prevent AppNap for Runtime UI only: */
        bool fPreventAppNap = false;
# ifdef VBOX_RUNTIME_UI
        fPreventAppNap = true;
# endif
        /* Instantiate own NSApplication before QApplication do it for us: */
        UICocoaApplication::create(fPreventAppNap);

# ifdef VBOX_RUNTIME_UI
        /* If we're a helper app inside Resources in the main application bundle,
         * we need to amend the library path so the platform plugin can be found.
         * Note! This builds on the initIprtForDarwinHelperApp() hack. */
        {
            char szExecDir[RTPATH_MAX];
            int vrc = RTPathExecDir(szExecDir, sizeof(szExecDir));
            AssertRC(vrc);
            RTPathStripTrailingSlash(szExecDir); /* .../Contents/MacOS */
            RTPathStripFilename(szExecDir);      /* .../Contents */
            RTPathAppend(szExecDir, sizeof(szExecDir), "plugins");      /* .../Contents/plugins */
            QCoreApplication::addLibraryPath(QString::fromUtf8(szExecDir));
        }
# endif /* VBOX_RUNTIME_UI */
#endif /* VBOX_WS_MAC */

#ifdef VBOX_WS_NIX
# if (defined(RT_OS_LINUX) && !defined(RT_ARCH_ARM64)) && defined(DEBUG)
        /* Install signal handler to backtrace the call stack: */
        InstallSignalHandler();
# endif /* RT_OS_LINUX && DEBUG */
#endif /* VBOX_WS_NIX */

        /* Install Qt console message handler: */
        qInstallMessageHandler(QtMessageOutput);

#ifdef VBOX_WS_NIX
        // WORKAROUND:
        // Some Desktop Environments forcing this variable which was useful in Qt5 case.
        // But in Qt6 case this variable is additional multiplier to scale-factor
        // calculated automatic way, which means for x2 the effective factor will be 2x2=4.
        // No idea how to proceed, probably we will ignore this variable for now, by
        // setting it to be always equal to 1.
        //qputenv("QT_SCALE_FACTOR", "1");
#endif /* VBOX_WS_NIX */

        /* Create application: */
        QApplication a(argc, argv);

#ifdef VBOX_WS_MAC
        /* Disable menu icons on MacOS X host: */
        ::darwinDisableIconsInMenus();
#endif /* VBOX_WS_MAC */

#ifdef VBOX_WS_WIN
        /* Drag in the sound drivers and DLLs early to get rid of the delay taking
         * place when the main menu bar (or any action from that menu bar) is
         * activated for the first time. This delay is especially annoying if it
         * happens when the VM is executing in real mode (which gives 100% CPU
         * load and slows down the load process that happens on the main GUI
         * thread to several seconds). */
        PlaySound(NULL, NULL, 0);
#endif /* VBOX_WS_WIN */

#ifdef VBOX_WS_NIX
        /* Make all widget native.
         * We did it to avoid various Qt crashes while testing widget attributes or acquiring winIds.
         * Yes, we aware of note that alien widgets faster to draw but the only widget we need to be fast
         * is viewport of VM which was always native since we are using his id for 3D service needs. */
        a.setAttribute(Qt::AA_NativeWindows);

# ifdef Q_OS_SOLARIS
        a.setStyle("fusion");
# endif /* Q_OS_SOLARIS */

# ifndef Q_OS_SOLARIS
        /* Apply font fixes (after QApplication get created and instantiated font-family): */
        QFontDatabase fontDataBase;
        QString currentFamily(QApplication::font().family());
        bool isCurrentScaleable = fontDataBase.isScalable(currentFamily);
        QString subFamily(QFont::substitute(currentFamily));
        bool isSubScaleable = fontDataBase.isScalable(subFamily);
        if (isCurrentScaleable && !isSubScaleable)
            QFont::removeSubstitutions(currentFamily);
# endif /* !Q_OS_SOLARIS */

        /* Qt version check (major.minor are sensitive, fix number is ignored): */
        if (UIVersionInfo::qtRTVersion() < (UIVersionInfo::qtCTVersion() & 0xFFFF00))
        {
            QString strMsg = QApplication::tr("Executable <b>%1</b> requires Qt %2.x, found Qt %3.")
                                              .arg(qAppName())
                                              .arg(UIVersionInfo::qtCTVersionString().section('.', 0, 1))
                                              .arg(UIVersionInfo::qtRTVersionString());
            QMessageBox::critical(0, QApplication::tr("Incompatible Qt Library Error"),
                                  strMsg, QMessageBox::Abort, QMessageBox::NoButton);
            qFatal("%s", strMsg.toUtf8().constData());
            break;
        }
#endif /* VBOX_WS_NIX */

        /* Create modal-window manager: */
        UIModalWindowManager::create();

#ifndef VBOX_RUNTIME_UI
        /* Create global app instance for Selector UI: */
        UICommon::create(UIType_ManagerUI);
#else
        /* Create global app instance for Runtime UI: */
        UICommon::create(UIType_RuntimeUI);
#endif

        /* Simulate try-catch block: */
        do
        {
            /* Exit if UICommon is not valid: */
            if (!uiCommon().isValid())
                break;
            /* Exit if UICommon pre-processed arguments: */
            if (uiCommon().processArgs())
                break;

            // WORKAROUND:
            // Initially we wanted to make that workaround for Runtime UI only,
            // because only there we had a strict handling for proper application quit
            // procedure.  But it appeared on X11 (as usually due to an async nature) there
            // can happen situations that Qt application is checking whether at least one
            // window is already shown and if not - exits prematurely _before_ it is actually
            // shown.  That can happen for example if window is not yet shown because blocked
            // by startup error message-box which is not treated as real window by some
            // reason.  So we are making application exit manual everywhere.
            a.setQuitOnLastWindowClosed(false);

            /* Create UI starter: */
            UIStarter uiStarter;
            /* Request to Start UI _after_ QApplication executed: */
            QMetaObject::invokeMethod(&uiStarter, "sltStartUI", Qt::QueuedConnection);

            /* Start application: */
            iResultCode = a.exec();
        }
        while (0);

        /* Destroy global app instance: */
        UICommon::destroy();

        /* Destroy modal-window manager: */
        UIModalWindowManager::destroy();
    }
    while (0);

    /* Finish logging: */
    LogFlowFunc(("rc=%d\n", iResultCode));
    LogFlowFuncLeave();

    /* Return result: */
    return iResultCode;
}

#if !defined(VBOX_WITH_HARDENING) || !defined(VBOX_RUNTIME_UI)

# if defined(RT_OS_DARWIN) && defined(VBOX_RUNTIME_UI)

extern "C" const char *_dyld_get_image_name(uint32_t);

/** Init runtime with the executable path pointing into the
 * VirtualBox.app/Contents/MacOS/ rather than
 * VirtualBox.app/Contents/Resource/VirtualBoxVM.app/Contents/MacOS/.
 *
 * This is a HACK to make codesign and friends happy on OS X.   The idea is to
 * improve and eliminate this over time.
 */
DECL_NO_INLINE(static, int) initIprtForDarwinHelperApp(int cArgs, char ***ppapszArgs, uint32_t fFlags)
{
    const char *pszImageName = _dyld_get_image_name(0);
    AssertReturn(pszImageName, VERR_INTERNAL_ERROR);

    char szTmpPath[PATH_MAX + 1];
    const char *psz = realpath(pszImageName, szTmpPath);
    int rc;
    if (psz)
    {
        char *pszFilename = RTPathFilename(szTmpPath);
        if (pszFilename)
        {
            char const chSavedFilename0 = *pszFilename;
            *pszFilename = '\0';
            RTPathStripTrailingSlash(szTmpPath); /* VirtualBox.app/Contents/Resources/VirtualBoxVM.app/Contents/MacOS */
            RTPathStripFilename(szTmpPath);      /* VirtualBox.app/Contents/Resources/VirtualBoxVM.app/Contents/ */
            RTPathStripFilename(szTmpPath);      /* VirtualBox.app/Contents/Resources/VirtualBoxVM.app */
            RTPathStripFilename(szTmpPath);      /* VirtualBox.app/Contents/Resources */
            RTPathStripFilename(szTmpPath);      /* VirtualBox.app/Contents */
            char *pszDst = strchr(szTmpPath, '\0');
            pszDst = (char *)memcpy(pszDst, RT_STR_TUPLE("/MacOS/")) + sizeof("/MacOS/") - 1; /** @todo where is mempcpy? */
            *pszFilename = chSavedFilename0;
            memmove(pszDst, pszFilename, strlen(pszFilename) + 1);

            return RTR3InitEx(RTR3INIT_VER_CUR, fFlags, cArgs, ppapszArgs, szTmpPath);
        }
        rc = VERR_INVALID_NAME;
    }
    else
        rc = RTErrConvertFromErrno(errno);
    AssertMsgRCReturn(rc, ("rc=%Rrc pszLink=\"%s\"\nhex: %.*Rhxs\n", rc, pszImageName, strlen(pszImageName), pszImageName), rc);
    return rc;
}
# endif


int main(int argc, char **argv, char **envp)
{
# ifdef VBOX_WS_NIX
    /* Make sure multi-threaded environment is safe: */
    if (!MakeSureMultiThreadingIsSafe())
        return 1;
# endif /* VBOX_WS_NIX */

    /*
     * Determine the IPRT/SUPLib initialization flags if runtime UI process.
     * Only initialize SUPLib if about to start a VM in this process.
     *
     * Note! This must must match the corresponding parsing in hardenedmain.cpp
     *       and UICommon.cpp exactly, otherwise there will be weird error messages.
     */
    /** @todo r=bird: We should consider just postponing this stuff till VM
     *        creation, it shouldn't make too much of a difference GIP-wise. */
    uint32_t fFlags = 0;
# ifdef VBOX_RUNTIME_UI
    unsigned cOptionsLeft     = 4;
    bool     fStartVM         = false;
    bool     fSeparateProcess = false;
    bool     fExecuteAllInIem = false;
    bool     fDriverless      = false;
    for (int i = 1; i < argc && cOptionsLeft > 0; ++i)
    {
        if (   !strcmp(argv[i], "--startvm")
            || !strcmp(argv[i], "-startvm"))
        {
            cOptionsLeft -= fStartVM == false;
            fStartVM = true;
            i++;
        }
        else if (   !strcmp(argv[i], "--separate")
                 || !strcmp(argv[i], "-separate"))
        {
            cOptionsLeft -= fSeparateProcess == false;
            fSeparateProcess = true;
        }
        else if (!strcmp(argv[i], "--execute-all-in-iem"))
        {
            cOptionsLeft -= fExecuteAllInIem == false;
            fExecuteAllInIem = true;
        }
        else if (!strcmp(argv[i], "--driverless"))
        {
            cOptionsLeft -= fDriverless == false;
            fDriverless = true;
        }
    }
    if (fStartVM && !fSeparateProcess)
    {
        fFlags |= RTR3INIT_FLAGS_TRY_SUPLIB;
        if (fExecuteAllInIem)
            fFlags |= SUPR3INIT_F_DRIVERLESS_IEM_ALLOWED << RTR3INIT_FLAGS_SUPLIB_SHIFT;
        if (fDriverless)
            fFlags |= SUPR3INIT_F_DRIVERLESS << RTR3INIT_FLAGS_SUPLIB_SHIFT;
    }
# endif /* VBOX_RUNTIME_UI */

    /* Initialize VBox Runtime: */
# if defined(RT_OS_DARWIN) && defined(VBOX_RUNTIME_UI)
    int rc = initIprtForDarwinHelperApp(argc, &argv, fFlags);
# else
    int rc = RTR3InitExe(argc, &argv, fFlags);
# endif
    /* Initialization failed: */
    if (RT_FAILURE(rc))
    {
        /* We have to create QApplication anyway
         * just to show the only one error-message: */
        QApplication a(argc, &argv[0]);
        Q_UNUSED(a);

# ifdef Q_OS_SOLARIS
        a.setStyle("fusion");
# endif

        /* Prepare the error-message: */
        QString strTitle = QApplication::tr("VirtualBox - Runtime Error");
        QString strText = "<html>";
        switch (rc)
        {
            case VERR_VM_DRIVER_NOT_INSTALLED:
            case VERR_VM_DRIVER_LOAD_ERROR:
                strText += QApplication::tr("<b>Cannot access the kernel driver!</b><br/><br/>");
# ifdef RT_OS_LINUX
                strText += g_QStrHintLinuxNoDriver;
# else
                strText += g_QStrHintOtherNoDriver;
# endif
                break;
# ifdef RT_OS_LINUX
            case VERR_NO_MEMORY:
                strText += g_QStrHintLinuxNoMemory;
                break;
# endif
            case VERR_VM_DRIVER_NOT_ACCESSIBLE:
                strText += QApplication::tr("Kernel driver not accessible");
                break;
            case VERR_VM_DRIVER_VERSION_MISMATCH:
# ifdef RT_OS_LINUX
                strText += g_QStrHintLinuxWrongDriverVersion;
# else
                strText += g_QStrHintOtherWrongDriverVersion;
# endif
                break;
            default:
                strText += QApplication::tr("Unknown error %2 during initialization of the Runtime").arg(rc);
                break;
        }
        strText += "</html>";

        /* Show the error-message: */
        QMessageBox::critical(0 /* parent */, strTitle, strText,
                              QMessageBox::Abort /* 1st button */, QMessageBox::NoButton /* 2nd button */);

        /* Default error-result: */
        return 1;
    }

    /* Call to actual main function: */
    return TrustedMain(argc, argv, envp);
}

#endif /* !VBOX_WITH_HARDENING || !VBOX_RUNTIME_UI */

#ifdef VBOX_WITH_HARDENING

/**
 * Special entrypoint used by the hardening code when something goes south.
 *
 * Display an error dialog to the user.
 *
 * @param   pszWhere    Indicates where the error occured.
 * @param   enmWhat     Indicates what init operation was going on at the time.
 * @param   rc          The VBox status code corresponding to the error.
 * @param   pszMsgFmt   The message format string.
 * @param   va          Format arguments.
 */
extern "C" DECLEXPORT(void) TrustedError(const char *pszWhere, SUPINITOP enmWhat, int rc, const char *pszMsgFmt, va_list va)
{
    char szMsgBuf[_16K];

    /*
     * We have to create QApplication anyway just to show the only one error-message.
     * This is a bit hackish as we don't have the argument vector handy.
     */
    int argc = 0;
    char *argv[2] = { NULL, NULL };
    QApplication a(argc, &argv[0]);

    /*
     * The details starts off a properly formatted rc and where/what, we use
     * the szMsgBuf for this, thus this have to come before the actual message
     * formatting.
     */
    RTStrPrintf(szMsgBuf, sizeof(szMsgBuf),
                "<!--EOM-->"
                "where: %s\n"
                "what:  %d\n"
                "%Rra\n",
                pszWhere, enmWhat, rc);
    QString strDetails = szMsgBuf;

    /*
     * Format the error message. Take whatever comes after a double new line as
     * something better off in the details section.
     */
    RTStrPrintfV(szMsgBuf, sizeof(szMsgBuf), pszMsgFmt, va);

    char *pszDetails = strstr(szMsgBuf, "\n\n");
    if (pszDetails)
    {
        while (RT_C_IS_SPACE(*pszDetails))
            *pszDetails++ = '\0';
        if (*pszDetails)
        {
            strDetails += "\n";
            strDetails += pszDetails;
        }
        RTStrStripR(szMsgBuf);
    }

    QString strText = QApplication::tr("<html><b>%1 (rc=%2)</b><br/><br/>").arg(szMsgBuf).arg(rc);
    strText.replace(QString("\n"), QString("<br>"));

    /*
     * Append possibly helpful hints to the error message.
     */
    switch (enmWhat)
    {
        case kSupInitOp_Driver:
# ifdef RT_OS_LINUX
            strText += g_QStrHintLinuxNoDriver;
# else /* RT_OS_LINUX */
            strText += g_QStrHintOtherNoDriver;
# endif /* !RT_OS_LINUX */
            break;
        case kSupInitOp_IPRT:
        case kSupInitOp_Misc:
            if (rc == VERR_VM_DRIVER_VERSION_MISMATCH)
# ifndef RT_OS_LINUX
                strText += g_QStrHintOtherWrongDriverVersion;
# else
                strText += g_QStrHintLinuxWrongDriverVersion;
            else if (rc == VERR_NO_MEMORY)
                strText += g_QStrHintLinuxNoMemory;
# endif
            else
                strText += g_QStrHintReinstall;
            break;
        case kSupInitOp_Integrity:
        case kSupInitOp_RootCheck:
            strText += g_QStrHintReinstall;
            break;
        default:
            /* no hints here */
            break;
    }

# ifdef VBOX_WS_NIX
    /* We have to to make sure that we display the error-message
     * after the parent displayed its own message. */
    sleep(2);
# endif /* VBOX_WS_NIX */

    /* Update strText with strDetails: */
    if (!strDetails.isEmpty())
        strText += QString("<br><br>%1").arg(strDetails);

    /* Close the <html> scope: */
    strText += "</html>";

    /* Create and show the error message-box: */
    QMessageBox::critical(0, QApplication::tr("VirtualBox - Error In %1").arg(pszWhere), strText);

    qFatal("%s", strText.toUtf8().constData());
}

#endif /* VBOX_WITH_HARDENING */
