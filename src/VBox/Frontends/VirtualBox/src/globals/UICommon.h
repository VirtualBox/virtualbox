/* $Id: UICommon.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICommon class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UICommon_h
#define FEQT_INCLUDED_SRC_globals_UICommon_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QUuid>

/* GUI includes: */
#include "UIDefs.h"
#include "UILibraryDefs.h"
#ifdef VBOX_WS_NIX
# include "VBoxUtils-nix.h"
#endif

/* Other VBox includes: */
#ifdef VBOX_WITH_DEBUGGER_GUI
# include <iprt/types.h> /* for RTLDRMOD */
#endif

/* Forward declarations: */
class QSessionManager;
class QSpinBox;
class UIThreadPool;

/** QObject subclass containing common GUI functionality. */
class SHARED_LIBRARY_STUFF UICommon : public QObject
{
    Q_OBJECT;

signals:

    /** @name General stuff.
     * @{ */
        /** Asks #UIStarter listener to restart UI. */
        void sigAskToRestartUI();
        /** Asks #UIStarter listener to close UI. */
        void sigAskToCloseUI();

        /** Asks listeners to commit data. */
        void sigAskToCommitData();
        /** Asks listeners to detach COM. */
        void sigAskToDetachCOM();
    /** @} */

    /** @name Host OS stuff.
     * @{ */
        /** Notifies listeners about theme change. */
        void sigThemeChange();
    /** @} */

public:

    /** VM launch running options. */
    enum LaunchRunning
    {
        LaunchRunning_Default, /**< Default (depends on debug settings). */
        LaunchRunning_No,      /**< Start the VM paused. */
        LaunchRunning_Yes      /**< Start the VM running. */
    };

    /** Returns UICommon instance. */
    static UICommon *instance() { return s_pInstance; }
    /** Creates UICommon instance of passed @a enmType. */
    static void create(UIType enmType);
    /** Destroys UICommon instance. */
    static void destroy();

    /** @name General stuff.
     * @{ */
        /** Returns the UI type. */
        UIType uiType() const { return m_enmType; }

        /** Returns whether UICommon instance is properly initialized. */
        bool isValid() const { return m_fValid; }
        /** Returns whether UICommon instance cleanup is in progress. */
        bool isCleaningUp() const { return m_fCleaningUp; }
    /** @} */

    /** @name Host OS stuff.
     * @{ */
#ifdef VBOX_WS_MAC
        /** macOS: Returns #MacOSXRelease determined by <i>uname</i> call. */
        static MacOSXRelease determineOsRelease();
        /** macOS: Returns #MacOSXRelease determined during UICommon prepare routine. */
        MacOSXRelease osRelease() const { return m_enmMacOSVersion; }
#endif

#ifdef VBOX_WS_NIX
        /** X11: Returns the type of the Window Manager we are running under. */
        X11WMType typeOfWindowManager() const { return m_enmWindowManagerType; }
        /** X11: Returns whether the Window Manager we are running under is composition one. */
        bool isCompositingManagerRunning() const { return m_fCompositingManagerRunning; }
        /** Returns true if the detected display server type is either xorg or xwayland. */
        bool X11ServerAvailable() const;
        /** Returns display server type. */
        VBGHDISPLAYSERVERTYPE displayServerType() const;
#endif
        /** Returns the name of the host OS by using IHost::getOperatingSystem. */
        QString hostOperatingSystem() const;

#if defined(VBOX_WS_MAC)
        // Provided by UICocoaApplication ..
#elif defined(VBOX_WS_WIN)
        /** Returns whether Windows host is in Dark mode. */
        bool isWindowsInDarkMode() const;
#else /* Linux, BSD, Solaris */
        /** Returns whether palette is in Dark mode. */
        bool isPaletteInDarkMode() const;
#endif /* Linux, BSD, Solaris */

        /** Returns whether host OS is in Dark mode. */
        bool isInDarkMode() const { return m_fDarkMode; }

        /** Loads the color theme. */
        void loadColorTheme();
    /** @} */

    /** @name Process arguments stuff.
     * @{ */
        /** Process application args. */
        bool processArgs();

        /** Returns whether there are unhandled URL arguments present. */
        bool argumentUrlsPresent() const;
        /** Takes and returns the URL argument list while clearing the source. */
        QList<QUrl> takeArgumentUrls();

        /** Returns the --startvm option value (managed VM id). */
        QUuid managedVMUuid() const { return m_uManagedVMId; }
        /** Returns the --separate option value (whether GUI process is separate from VM process). */
        bool isSeparateProcess() const { return m_fSeparateProcess; }
        /** Returns the --no-startvm-errormsgbox option value (whether startup VM errors are disabled). */
        bool showStartVMErrors() const { return m_fShowStartVMErrors; }

        /** Returns the --aggressive-caching / --no-aggressive-caching option value (whether medium-enumeration is required). */
        bool agressiveCaching() const { return m_fAgressiveCaching; }

        /** Returns the --restore-current option value (whether we should restore current snapshot before VM started). */
        bool shouldRestoreCurrentSnapshot() const { return m_fRestoreCurrentSnapshot; }
        /** Returns the --restore-snapshot option value (whether to restore named snapshot before starting VM). */
        QString const &getSnapshotToRestore() const { return m_strSnapshotToRestore; }
        /** Clears the --restore-current and --restore-snapshot option values. */
        void clearSnapshotRestoreOptions() { m_fRestoreCurrentSnapshot = false; m_strSnapshotToRestore.clear(); }

        /** Returns the --no-keyboard-grabbing option value (whether we should restore
         *  grab the keyboard or not - for debugging). */
        bool shouldNotGrabKeyboard() const { return m_fNoKeyboardGrabbing; }

        /** Returns the --fda option value (whether we have floppy image). */
        bool hasFloppyImageToMount() const { return !m_strFloppyImage.isEmpty(); }
        /** Returns the --dvd | --cdrom option value (whether we have DVD image). */
        bool hasDvdImageToMount() const { return !m_strDvdImage.isEmpty(); }
        /** Returns floppy image name. */
        QString getFloppyImage() const { return m_strFloppyImage; }
        /** Returns DVD image name. */
        QString getDvdImage() const { return m_strDvdImage; }

        /** Returns the --execute-all-in-iem option value. */
        bool areWeToExecuteAllInIem() const { return m_fExecuteAllInIem; }
        /** Returns whether --warp-factor option value is equal to 100. */
        bool isDefaultWarpPct() const { return m_uWarpPct == 100; }
        /** Returns the --warp-factor option value. */
        uint32_t getWarpPct() const { return m_uWarpPct; }

#ifdef VBOX_WITH_DEBUGGER_GUI
        /** Holds whether the debugger should be accessible. */
        bool isDebuggerEnabled() const;
        /** Holds whether to show the debugger automatically with the console. */
        bool isDebuggerAutoShowEnabled() const;
        /** Holds whether to show the command line window when m_fDbgAutoShow is set. */
        bool isDebuggerAutoShowCommandLineEnabled() const;
        /** Holds whether to show the statistics window when m_fDbgAutoShow is set. */
        bool isDebuggerAutoShowStatisticsEnabled() const;
        /** Returns the combined --statistics-expand values. */
        QString const getDebuggerStatisticsExpand() const { return m_strDbgStatisticsExpand; }
        /** Returns the --statistics-filter value. */
        QString const getDebuggerStatisticsFilter() const { return m_strDbgStatisticsFilter; }
        /** Returns the --statistics-config value. */
        QString const getDebuggerStatisticsConfig() const { return m_strDbgStatisticsConfig; }

        /** VBoxDbg module handle. */
        RTLDRMOD getDebuggerModule() const { return m_hVBoxDbg; }
#endif

        /** Returns whether VM should start paused. */
        bool shouldStartPaused() const;
    /** @} */

    /** @name Application stuff.
     * @{ */
#ifdef VBOX_GUI_WITH_PIDFILE
        /** Creates PID file. */
        void createPidfile();
        /** Deletes PID file. */
        void deletePidfile();
#endif
    /** @} */

    /** @name File-system stuff.
     * @{ */
        /** Returns full help file name. */
        static QString helpFile();

        /** Returns documents path. */
        static QString documentsPath();

        /** Returns whether passed @a strFileName ends with one of allowed extension in the @a extensions list. */
        static bool hasAllowedExtension(const QString &strFileName, const QStringList &extensions);

        /** Returns a file name (unique up to extension) wrt. @a strFullFolderPath folder content. Starts
          * searching strBaseFileName and adds suffixes until a unique file name is found. */
        static QString findUniqueFileName(const QString &strFullFolderPath, const QString &strBaseFileName);
    /** @} */

    /** @name Widget stuff.
     * @{ */
        /** Assigns minimum @a pSpinBox to correspond to @a cCount digits. */
        static void setMinimumWidthAccordingSymbolCount(QSpinBox *pSpinBox, int cCount);
    /** @} */

    /** @name Thread stuff.
     * @{ */
        /** Returns the thread-pool instance. */
        UIThreadPool *threadPool() const { return m_pThreadPool; }
        /** Returns the thread-pool instance for cloud needs. */
        UIThreadPool *threadPoolCloud() const { return m_pThreadPoolCloud; }
    /** @} */

    /** @name Context sensitive help related functionality
     * @{ */
        /** Sets the property for help keyword on a QObject
          * @param  pObject      The object to set the help keyword property on
          * @param  strKeyword   The values of the key word property. */
        static void setHelpKeyword(QObject *pObject, const QString &strHelpKeyword);
        /** Returns the property for help keyword of a QObject. If no such property exists returns an empty QString.
          * @param  pWidget      The object to get the help keyword property from. */
        static QString helpKeyword(const QObject *pWidget);
    /** @} */

public slots:

    /** @name Process arguments stuff.
     * @{ */
        /** Opens the specified URL using OS/Desktop capabilities. */
        bool openURL(const QString &strURL) const;
    /** @} */

protected:

    /** Preprocesses any Qt @a pEvent for passed @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;

    /** Handles translation event. */
    virtual void retranslateUi();

protected slots:

    /** Calls for cleanup() functionality. */
    void sltCleanup() { cleanup(); }

#ifndef VBOX_GUI_WITH_CUSTOMIZATIONS1
    /** @name Common stuff.
     * @{ */
        /** Handles @a manager request for emergency session shutdown. */
        void sltHandleCommitDataRequest(QSessionManager &manager);
    /** @} */
#endif /* VBOX_GUI_WITH_CUSTOMIZATIONS1 */

    /** @name COM stuff.
     * @{ */
        /** Handles the VBoxSVC availability change. */
        void sltHandleVBoxSVCAvailabilityChange(bool fAvailable);
    /** @} */

    /** @name Localization stuff.
     * @{ */
        /** Handles language change to new @a strLanguage. */
        void sltGUILanguageChange(QString strLanguage);

        /** Handles font @a iFontScaleFactor change. */
        void sltHandleFontScaleFactorChanged(int iFontScaleFactor);
    /** @} */

private:

    /** Construcs global VirtualBox object of passed @a enmType. */
    UICommon(UIType enmType);
    /** Destrucs global VirtualBox object. */
    virtual ~UICommon() RT_OVERRIDE RT_FINAL;

    /** Prepares all. */
    void prepare();
    /** Cleanups all. */
    void cleanup();

    /** @name Process arguments stuff.
     * @{ */
#ifdef VBOX_WITH_DEBUGGER_GUI
        /** Initializes a debugger config variable.
          * @param  piDbgCfgVar       Brings the debugger config variable to init.
          * @param  pszEnvVar         Brings the environment variable name relating to this variable.
          * @param  pszExtraDataName  Brings the extra data name relating to this variable.
          * @param  fDefault          Brings the default value. */
        void initDebuggerVar(int *piDbgCfgVar, const char *pszEnvVar, const char *pszExtraDataName, bool fDefault = false);
        /** Set a debugger config variable according according to start up argument.
          * @param  piDbgCfgVar  Brings the debugger config variable to set.
          * @param  fState       Brings the value from the command line. */
        void setDebuggerVar(int *piDbgCfgVar, bool fState);
        /** Checks the state of a debugger config variable, updating it with the machine settings on the first invocation.
          * @param  piDbgCfgVar       Brings the debugger config variable to consult.
          * @param  pszExtraDataName  Brings the extra data name relating to this variable. */
        bool isDebuggerWorker(int *piDbgCfgVar, const char *pszExtraDataName) const;
#endif
    /** @} */

    /** @name USB stuff.
     * @{ */
#ifdef RT_OS_LINUX
        /** Verifies that USB drivers are properly configured on Linux. */
        static void checkForWrongUSBMounted();
#endif
    /** @} */

    /** Holds the singleton UICommon instance. */
    static UICommon *s_pInstance;

    /** @name General stuff.
     * @{ */
        /** Holds the UI type. */
        UIType  m_enmType;

        /** Holds whether UICommon instance is properly initialized. */
        bool  m_fValid;
        /** Holds whether UICommon instance cleanup is in progress. */
        bool  m_fCleaningUp;
#ifdef VBOX_WS_WIN
        /** Holds whether overall GUI data is committed. */
        bool  m_fDataCommitted;
#endif
    /** @} */

    /** @name Host OS stuff.
     * @{ */
#ifdef VBOX_WS_MAC
        /** macOS: Holds the #MacOSXRelease determined using <i>uname</i> call. */
        MacOSXRelease  m_enmMacOSVersion;
#endif

#ifdef VBOX_WS_NIX
        /** X11: Holds the #X11WMType of the Window Manager we are running under. */
        X11WMType             m_enmWindowManagerType;
        /** X11: Holds whether the Window Manager we are running at is composition one. */
        bool                  m_fCompositingManagerRunning;
        /** Unixes: Holds the display server type. */
        VBGHDISPLAYSERVERTYPE m_enmDisplayServerType;
#endif

        /** Holds whether host OS is in Dark mode. */
        bool  m_fDarkMode;
    /** @} */

    /** @name Process arguments stuff.
     * @{ */
        /** Holds the URL arguments list. */
        QList<QUrl>  m_listArgUrls;

        /** Holds the --startvm option value (managed VM id). */
        QUuid  m_uManagedVMId;
        /** Holds the --separate option value (whether GUI process is separate from VM process). */
        bool   m_fSeparateProcess;
        /** Holds the --no-startvm-errormsgbox option value (whether startup VM errors are disabled). */
        bool   m_fShowStartVMErrors;

        /** Holds the --aggressive-caching / --no-aggressive-caching option value (whether medium-enumeration is required). */
        bool  m_fAgressiveCaching;

        /** Holds the --no-keyboard-grabbing option value. */
        bool  m_fNoKeyboardGrabbing;

        /** Holds the --restore-current option value. */
        bool    m_fRestoreCurrentSnapshot;
        /** Holds the --restore-snapshot option value. */
        QString m_strSnapshotToRestore;

        /** Holds the --fda option value (floppy image). */
        QString  m_strFloppyImage;
        /** Holds the --dvd | --cdrom option value (DVD image). */
        QString  m_strDvdImage;

        /** Holds the --execute-all-in-iem option value. */
        bool      m_fExecuteAllInIem;
        /** Holds the --warp-factor option value. */
        uint32_t  m_uWarpPct;

#ifdef VBOX_WITH_DEBUGGER_GUI
        /** Holds whether the debugger should be accessible. */
        mutable int  m_fDbgEnabled;
        /** Holds whether to show the debugger automatically with the console. */
        mutable int  m_fDbgAutoShow;
        /** Holds whether to show the command line window when m_fDbgAutoShow is set. */
        mutable int  m_fDbgAutoShowCommandLine;
        /** Holds whether to show the statistics window when m_fDbgAutoShow is set. */
        mutable int  m_fDbgAutoShowStatistics;
        /** Pattern of statistics to expand when opening the viewer. */
        QString      m_strDbgStatisticsExpand;
        /** The statistics viewer main filter pattern. */
        QString      m_strDbgStatisticsFilter;
        /** The statistics viewer advanced filter configuration and possibly more. */
        QString      m_strDbgStatisticsConfig;

        /** VBoxDbg module handle. */
        RTLDRMOD  m_hVBoxDbg;

        /** Holds whether --start-running, --start-paused or nothing was given. */
        LaunchRunning  m_enmLaunchRunning;
#endif

        /** Holds the --settingspw option value or the content of --settingspwfile. */
        char  m_astrSettingsPw[256];
        /** Holds the --settingspwfile option value. */
        bool  m_fSettingsPwSet;
    /** @} */

    /** @name Application stuff.
     * @{ */
#ifdef VBOX_GUI_WITH_PIDFILE
        /** Holds the --pidfile option value (application PID file path). */
        QString  m_strPidFile;
#endif
    /** @} */

    /** @name Thread stuff.
     * @{ */
        /** Holds the thread-pool instance. */
        UIThreadPool *m_pThreadPool;
        /** Holds the thread-pool instance for cloud needs. */
        UIThreadPool *m_pThreadPoolCloud;
    /** @} */

    /** @name Font scaling related variables.
     * @{ */
       int iOriginalFontPixelSize;
       int iOriginalFontPointSize;
    /** @} */

    /** Allows for shortcut access. */
    friend UICommon &uiCommon();
};

/** Singleton UICommon 'official' name. */
inline UICommon &uiCommon() { return *UICommon::instance(); }

#endif /* !FEQT_INCLUDED_SRC_globals_UICommon_h */
