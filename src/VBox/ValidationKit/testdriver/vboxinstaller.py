#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
VirtualBox Installer Wrapper Driver.

This installs VirtualBox, starts a sub driver which does the real testing,
and then uninstall VirtualBox afterwards.  This reduces the complexity of the
other VBox test drivers.
"""

__copyright__ = \
"""
Copyright (C) 2010-2024 Oracle and/or its affiliates.

This file is part of VirtualBox base platform packages, as
available from https://www.virtualbox.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, in version 3 of the
License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <https://www.gnu.org/licenses>.

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL), a copy of it is provided in the "COPYING.CDDL" file included
in the VirtualBox distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.

SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
"""
__version__ = "$Revision: 106320 $"


# Standard Python imports.
import os
import sys
import re
import socket
import tempfile
import time

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from common             import utils, webutils;
from common.constants   import rtexitcode;
from testdriver         import reporter;
from testdriver.base    import TestDriverBase;



class VBoxInstallerTestDriver(TestDriverBase):
    """
    Implementation of a top level test driver.
    """


    ## State file indicating that we've skipped installation.
    ksVar_Skipped = 'vboxinstaller-skipped';


    def __init__(self):
        TestDriverBase.__init__(self);
        self._asSubDriver   = [];   # The sub driver and it's arguments.
        self._asBuildUrls   = [];   # The URLs passed us on the command line.
        self._asBuildFiles  = [];   # The downloaded file names.
        self._fUnpackedBuildFiles = False;
        self._fAutoInstallPuelExtPack = True;
        self._fKernelDrivers          = True;
        self._fWinForcedInstallTimestampCA = True;
        self._fInstallMsCrt = False; # By default we don't install the Microsoft CRT (only needed once).

    #
    # Base method we override
    #

    def showUsage(self):
        rc = TestDriverBase.showUsage(self);
        #             0         1         2         3         4         5         6         7         8
        #             012345678901234567890123456789012345678901234567890123456789012345678901234567890
        reporter.log('');
        reporter.log('vboxinstaller Options:');
        reporter.log('  --vbox-build    <url[,url2[,...]]>');
        reporter.log('      Comma separated list of URL to file to download and install or/and');
        reporter.log('      unpack.  URLs without a schema are assumed to be files on the');
        reporter.log('      build share and will be copied off it.');
        reporter.log('  --no-puel-extpack');
        reporter.log('      Indicates that the PUEL extension pack should not be installed if found.');
        reporter.log('      The default is to install it when found in the vbox-build.');
        reporter.log('  --no-kernel-drivers');
        reporter.log('      Indicates that the kernel drivers should not be installed on platforms');
        reporter.log('      where this is optional. The default is to install them.');
        reporter.log('  --forced-win-install-timestamp-ca, --no-forced-win-install-timestamp-ca');
        reporter.log('      Whether to force installation of the legacy Windows timestamp CA.');
        reporter.log('      If not forced, it will only installed on the hosts that needs it.');
        reporter.log('      Default: --no-forced-win-install-timestamp-ca');
        reporter.log('  --win-install-mscrt, --no-win-install-mscrt');
        reporter.log('      Whether to install the MS Visual Studio Redistributable.');
        reporter.log('      Default: --no-win-install-mscrt');
        reporter.log('  --');
        reporter.log('      Indicates the end of our parameters and the start of the sub');
        reporter.log('      testdriver and its arguments.');
        return rc;

    def parseOption(self, asArgs, iArg):
        """
        Parse our arguments.
        """
        if asArgs[iArg] == '--':
            # End of our parameters and start of the sub driver invocation.
            iArg = self.requireMoreArgs(1, asArgs, iArg);
            assert not self._asSubDriver;
            self._asSubDriver = asArgs[iArg:];
            self._asSubDriver[0] = self._asSubDriver[0].replace('/', os.path.sep);
            iArg = len(asArgs) - 1;
        elif asArgs[iArg] == '--vbox-build':
            # List of files to copy/download and install.
            iArg = self.requireMoreArgs(1, asArgs, iArg);
            self._asBuildUrls = asArgs[iArg].split(',');
        elif asArgs[iArg] == '--no-puel-extpack':
            self._fAutoInstallPuelExtPack = False;
        elif asArgs[iArg] == '--puel-extpack':
            self._fAutoInstallPuelExtPack = True;
        elif asArgs[iArg] == '--no-kernel-drivers':
            self._fKernelDrivers = False;
        elif asArgs[iArg] == '--kernel-drivers':
            self._fKernelDrivers = True;
        elif asArgs[iArg] == '--no-forced-win-install-timestamp-ca':
            self._fWinForcedInstallTimestampCA = False;
        elif asArgs[iArg] == '--forced-win-install-timestamp-ca':
            self._fWinForcedInstallTimestampCA = True;
        elif asArgs[iArg] == '--no-win-install-mscrt':
            self._fInstallMsCrt = False;
        elif asArgs[iArg] == '--win-install-mscrt':
            self._fInstallMsCrt = True;
        else:
            return TestDriverBase.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def completeOptions(self):
        #
        # Check that we've got what we need.
        #
        if not self._asBuildUrls:
            reporter.error('No build files specified ("--vbox-build file1[,file2[...]]")');
            return False;
        if not self._asSubDriver:
            reporter.error('No sub testdriver specified. (" -- test/stuff/tdStuff1.py args")');
            return False;

        #
        # Construct _asBuildFiles as an array parallel to _asBuildUrls.
        #
        for sUrl in self._asBuildUrls:
            sDstFile = os.path.join(self.sScratchPath, webutils.getFilename(sUrl));
            self._asBuildFiles.append(sDstFile);

        return TestDriverBase.completeOptions(self);

    def actionExtract(self):
        reporter.error('vboxinstall does not support extracting resources, you have to do that using the sub testdriver.');
        return False;

    def actionCleanupBefore(self):
        """
        Kills all VBox process we see.

        This is only supposed to execute on a testbox so we don't need to go
        all complicated wrt other users.
        """
        return self._killAllVBoxProcesses();

    def actionConfig(self):
        """
        Install VBox and pass on the configure request to the sub testdriver.
        """
        fRc = self._installVBox();
        if fRc is None:
            self._persistentVarSet(self.ksVar_Skipped, 'true');
            self.fBadTestbox = True;
        else:
            self._persistentVarUnset(self.ksVar_Skipped);

        ## @todo vbox.py still has bugs preventing us from invoking it seperately with each action.
        if fRc is True and 'execute' not in self.asActions and 'all' not in self.asActions:
            fRc = self._executeSubDriver([ 'verify', ]);
        if fRc is True and 'execute' not in self.asActions and 'all' not in self.asActions:
            fRc = self._executeSubDriver([ 'config', ], fPreloadASan = True);
        return fRc;

    def actionExecute(self):
        """
        Execute the sub testdriver.
        """
        return self._executeSubDriver(self.asActions, fPreloadASan = True);

    def actionCleanupAfter(self):
        """
        Forward this to the sub testdriver, then uninstall VBox.
        """
        fRc = True;
        if 'execute' not in self.asActions and 'all' not in self.asActions:
            fRc = self._executeSubDriver([ 'cleanup-after', ], fMaySkip = False);

        if not self._killAllVBoxProcesses():
            fRc = False;

        if not self._uninstallVBox(self._persistentVarExists(self.ksVar_Skipped)):
            fRc = False;

        if utils.getHostOs() == 'darwin':
            self._darwinUnmountDmg(fIgnoreError = True); # paranoia

        if not TestDriverBase.actionCleanupAfter(self):
            fRc = False;

        return fRc;


    def actionAbort(self):
        """
        Forward this to the sub testdriver first, then wipe all VBox like
        processes, and finally do the pid file processing (again).
        """
        fRc1 = self._executeSubDriver([ 'abort', ], fMaySkip = False, fPreloadASan = True);
        fRc2 = self._killAllVBoxProcesses();
        fRc3 = TestDriverBase.actionAbort(self);
        return fRc1 and fRc2 and fRc3;


    #
    # Persistent variables.
    #
    ## @todo integrate into the base driver. Persistent accross scratch wipes?

    def __persistentVarCalcName(self, sVar):
        """Returns the (full) filename for the given persistent variable."""
        assert re.match(r'^[a-zA-Z0-9_-]*$', sVar) is not None;
        return os.path.join(self.sScratchPath, 'persistent-%s.var' % (sVar,));

    def _persistentVarSet(self, sVar, sValue = ''):
        """
        Sets a persistent variable.

        Returns True on success, False + reporter.error on failure.

        May raise exception if the variable name is invalid or something
        unexpected happens.
        """
        sFull = self.__persistentVarCalcName(sVar);
        try:
            with open(sFull, 'w') as oFile: # pylint: disable=unspecified-encoding
                if sValue:
                    oFile.write(sValue.encode('utf-8'));
        except:
            reporter.errorXcpt('Error creating "%s"' % (sFull,));
            return False;
        return True;

    def _persistentVarUnset(self, sVar):
        """
        Unsets a persistent variable.

        Returns True on success, False + reporter.error on failure.

        May raise exception if the variable name is invalid or something
        unexpected happens.
        """
        sFull = self.__persistentVarCalcName(sVar);
        if os.path.exists(sFull):
            try:
                os.unlink(sFull);
            except:
                reporter.errorXcpt('Error unlinking "%s"' % (sFull,));
                return False;
        return True;

    def _persistentVarExists(self, sVar):
        """
        Checks if a persistent variable exists.

        Returns true/false.

        May raise exception if the variable name is invalid or something
        unexpected happens.
        """
        return os.path.exists(self.__persistentVarCalcName(sVar));

    def _persistentVarGet(self, sVar):
        """
        Gets the value of a persistent variable.

        Returns variable value on success.
        Returns None if the variable doesn't exist or if an
        error (reported) occured.

        May raise exception if the variable name is invalid or something
        unexpected happens.
        """
        sFull = self.__persistentVarCalcName(sVar);
        if not os.path.exists(sFull):
            return None;
        try:
            with open(sFull, 'r') as oFile: # pylint: disable=unspecified-encoding
                sValue = oFile.read().decode('utf-8');
        except:
            reporter.errorXcpt('Error creating "%s"' % (sFull,));
            return None;
        return sValue;


    #
    # Helpers.
    #

    def _killAllVBoxProcesses(self):
        """
        Kills all virtual box related processes we find in the system.
        """
        sHostOs = utils.getHostOs();
        asDebuggers = [ 'cdb', 'windbg', ] if sHostOs == 'windows' else [ 'gdb', 'gdb-i386-apple-darwin', 'lldb' ];

        for iIteration in range(22):
            # Gather processes to kill.
            aoTodo      = [];
            aoDebuggers = [];
            for oProcess in utils.processListAll():
                sBase = oProcess.getBaseImageNameNoExeSuff();
                if sBase is None:
                    continue;
                sBase = sBase.lower();
                if sBase in [ 'vboxsvc', 'vboxsds', 'virtualbox', 'virtualboxvm', 'vboxheadless', 'vboxmanage', 'vboxsdl',
                              'vboxwebsrv', 'vboxautostart', 'vboxballoonctrl', 'vboxbfe', 'vboxextpackhelperapp', 'vboxnetdhcp',
                              'vboxnetnat', 'vboxnetadpctl', 'vboxtestogl', 'vboxtunctl', 'vboxvmmpreload', 'vboxxpcomipcd', ]:
                    aoTodo.append(oProcess);
                if sBase.startswith('virtualbox-') and sBase.endswith('-multiarch.exe'):
                    aoTodo.append(oProcess);
                if sBase in asDebuggers:
                    aoDebuggers.append(oProcess);
                    if iIteration in [0, 21]:
                        reporter.log('Warning: debugger running: %s (%s %s)' % (oProcess.iPid, sBase, oProcess.asArgs));
            if not aoTodo:
                return True;

            # Are any of the debugger processes hooked up to a VBox process?
            if sHostOs == 'windows':
                def isDebuggerDebuggingVBox(oDebugger, aoVBoxProcesses):
                    for oProcess in aoVBoxProcesses:
                        # The whole command line is asArgs[0] here. Fix if that changes.
                        if oDebugger.asArgs and oDebugger.asArgs[0].find('-p %s ' % (oProcess.iPid,)) >= 0:
                            return True;
                    return False;
            else:
                def isDebuggerDebuggingVBox(oDebugger, aoVBoxProcesses):
                    for oProcess in aoVBoxProcesses:
                        # Simplistic approach: Just check for argument equaling our pid.
                        if oDebugger.asArgs and ('%s' % oProcess.iPid) in oDebugger.asArgs:
                            return True;
                    return False;
            for oDebugger in aoDebuggers:
                if isDebuggerDebuggingVBox(oDebugger, aoTodo):
                    aoTodo.append(oDebugger);

            # Kill.
            for oProcess in aoTodo:
                reporter.log('Loop #%d - Killing %s (%s, uid=%s)'
                             % ( iIteration, oProcess.iPid, oProcess.sImage if oProcess.sName is None else oProcess.sName,
                                 oProcess.iUid, ));
                if    not utils.processKill(oProcess.iPid) \
                  and sHostOs != 'windows' \
                  and utils.processExists(oProcess.iPid):
                    # Many of the vbox processes are initially set-uid-to-root and associated debuggers are running
                    # via sudo, so we might not be able to kill them unless we sudo and use /bin/kill.
                    try:    utils.sudoProcessCall(['/bin/kill', '-9', '%s' % (oProcess.iPid,)]);
                    except: reporter.logXcpt();

            # Check if they're all dead like they should be.
            time.sleep(0.1);
            for oProcess in aoTodo:
                if utils.processExists(oProcess.iPid):
                    time.sleep(2);
                    break;

        return False;

    def _executeSync(self, asArgs, fMaySkip = False):
        """
        Executes a child process synchronously.

        Returns True if the process executed successfully and returned 0.
        Returns None if fMaySkip is true and the child exits with RTEXITCODE_SKIPPED.
        Returns False for all other cases.
        """
        reporter.log('Executing: %s' % (asArgs, ));
        reporter.flushall();
        try:
            iRc = utils.processCall(asArgs, shell = False, close_fds = False);
        except:
            reporter.errorXcpt();
            return False;
        reporter.log('Exit code: %s (%s)' % (iRc, asArgs));
        if fMaySkip and iRc == rtexitcode.RTEXITCODE_SKIPPED:
            return None;
        return iRc == 0;

    def _sudoExecuteSync(self, asArgs):
        """
        Executes a sudo child process synchronously.
        Returns a tuple [True, 0] if the process executed successfully
        and returned 0, otherwise [False, rc] is returned.
        """
        reporter.log('Executing [sudo]: %s' % (asArgs, ));
        reporter.flushall();
        iRc = 0;
        try:
            iRc = utils.sudoProcessCall(asArgs, shell = False, close_fds = False);
        except:
            reporter.errorXcpt();
            return (False, 0);
        reporter.log('Exit code [sudo]: %s (%s)' % (iRc, asArgs));
        return (iRc == 0, iRc);

    def _findASanLibsForASanBuild(self):
        """
        Returns a list of (address) santizier related libraries to preload
        when launching the sub driver.
        Returns empty list for non-asan builds or on platforms where this isn't needed.
        """
        # Note! We include libasan.so.X in the VBoxAll tarball for asan builds, so we
        #       can use its presence both to detect an 'asan' build and to return it.
        #       Only the libasan.so.X library needs preloading at present.
        if self.sHost in ('linux',):
            sLibASan = self._findFile(r'libasan\.so\..*');
            if sLibASan:
                return [sLibASan,];
        return [];

    def _executeSubDriver(self, asActions, fMaySkip = True, fPreloadASan = True):
        """
        Execute the sub testdriver with the specified action.
        """
        asArgs = list(self._asSubDriver)
        asArgs.append('--no-wipe-clean');
        asArgs.extend(asActions);

        asASanLibs = [];
        if fPreloadASan:
            asASanLibs = self._findASanLibsForASanBuild();
        if asASanLibs:
            os.environ['LD_PRELOAD'] = ':'.join(asASanLibs);
            os.environ['LSAN_OPTIONS'] = 'detect_leaks=0'; # We don't want python leaks. vbox.py disables this.

            # Because of https://github.com/google/sanitizers/issues/856 we must try use setarch to disable
            # address space randomization.

            reporter.log('LD_PRELOAD...')
            if utils.getHostArch() == 'amd64':
                sSetArch = utils.whichProgram('setarch');
                reporter.log('sSetArch=%s' % (sSetArch,));
                if sSetArch:
                    asArgs = [ sSetArch, 'x86_64', '-R', sys.executable ] + asArgs;
                    reporter.log('asArgs=%s' % (asArgs,));

            rc = self._executeSync(asArgs, fMaySkip = fMaySkip);

            del os.environ['LSAN_OPTIONS'];
            del os.environ['LD_PRELOAD'];
            return rc;

        return self._executeSync(asArgs, fMaySkip = fMaySkip);

    def _maybeUnpackArchive(self, sMaybeArchive, fNonFatal = False):
        """
        Attempts to unpack the given build file.
        Updates _asBuildFiles.
        Returns True/False. No exceptions.
        """
        def unpackFilter(sMember):
            # type: (string) -> bool
            """ Skips debug info. """
            sLower = sMember.lower();
            if sLower.endswith('.pdb'):
                return False;
            return True;

        asMembers = utils.unpackFile(sMaybeArchive, self.sScratchPath, reporter.log,
                                     reporter.log if fNonFatal else reporter.error,
                                     fnFilter = unpackFilter);
        if asMembers is None:
            return False;
        self._asBuildFiles.extend(asMembers);
        return True;


    def _installVBox(self):
        """
        Download / copy the build files into the scratch area and install them.
        """
        reporter.testStart('Installing VirtualBox');
        reporter.log('CWD=%s' % (os.getcwd(),)); # curious

        #
        # Download the build files.
        #
        for i, sBuildUrl in enumerate(self._asBuildUrls):
            if webutils.downloadFile(sBuildUrl, self._asBuildFiles[i], self.sBuildPath, reporter.log, reporter.log) is not True:
                reporter.testDone(fSkipped = True);
                return None; # Failed to get binaries, probably deleted. Skip the test run.

        #
        # Unpack anything we know what is and append it to the build files
        # list.  This allows us to use VBoxAll*.tar.gz files.
        #
        for sFile in list(self._asBuildFiles): # Note! We copy the list as _maybeUnpackArchive updates it.
            if self._maybeUnpackArchive(sFile, fNonFatal = True) is not True:
                reporter.testDone(fSkipped = True);
                return None; # Failed to unpack. Probably local error, like busy
                             # DLLs on windows, no reason for failing the build.
        self._fUnpackedBuildFiles = True;

        #
        # Go to system specific installation code.
        #
        sHost = utils.getHostOs()
        if   sHost == 'darwin':     fRc = self._installVBoxOnDarwin();
        elif sHost == 'linux':      fRc = self._installVBoxOnLinux();
        elif sHost == 'solaris':    fRc = self._installVBoxOnSolaris();
        elif sHost == 'win':        fRc = self._installVBoxOnWindows();
        else:
            reporter.error('Unsupported host "%s".' % (sHost,));
        if fRc is False:
            reporter.testFailure('Installation error.');
        elif fRc is not True:
            reporter.log('Seems installation was skipped. Old version lurking behind? Not the fault of this build/test run!');

        #
        # Install the extension pack.
        #
        if fRc is True  and  self._fAutoInstallPuelExtPack:
            fRc = self._installExtPack();
            if fRc is False:
                reporter.testFailure('Extension pack installation error.');

        # Some debugging...
        try:
            cMbFreeSpace = utils.getDiskUsage(self.sScratchPath);
            reporter.log('Disk usage after VBox install: %d MB available at %s' % (cMbFreeSpace, self.sScratchPath,));
        except:
            reporter.logXcpt('Unable to get disk free space. Ignored. Continuing.');

        reporter.testDone(fRc is None);
        return fRc;

    def _uninstallVBox(self, fIgnoreError = False):
        """
        Uninstall VirtualBox.
        """
        reporter.testStart('Uninstalling VirtualBox');

        sHost = utils.getHostOs()
        if   sHost == 'darwin':     fRc = self._uninstallVBoxOnDarwin();
        elif sHost == 'linux':      fRc = self._uninstallVBoxOnLinux();
        elif sHost == 'solaris':    fRc = self._uninstallVBoxOnSolaris(True);
        elif sHost == 'win':        fRc = self._uninstallVBoxOnWindows('uninstall');
        else:
            reporter.error('Unsupported host "%s".' % (sHost,));
        if fRc is False and not fIgnoreError:
            reporter.testFailure('Uninstallation failed.');

        fRc2 = self._uninstallAllExtPacks();
        if not fRc2 and fRc:
            fRc = fRc2;

        reporter.testDone(fSkipped = fRc is None);
        return fRc;

    def _findFile(self, sRegExp, fMandatory = False):
        """
        Returns the first build file that matches the given regular expression
        (basename only).

        Returns None if no match was found, logging it as an error if
        fMandatory is set.
        """
        oRegExp = re.compile(sRegExp);

        reporter.log('_findFile: %s' % (sRegExp,));
        for sFile in self._asBuildFiles:
            if oRegExp.match(os.path.basename(sFile)) and os.path.exists(sFile):
                return sFile;

        # If we didn't unpack the build files, search all the files in the scratch area:
        if not self._fUnpackedBuildFiles:
            for sDir, _, asFiles in os.walk(self.sScratchPath):
                for sFile in asFiles:
                    #reporter.log('_findFile: considering %s' % (sFile,));
                    if oRegExp.match(sFile):
                        return os.path.join(sDir, sFile);

        if fMandatory:
            reporter.error('Failed to find a file matching "%s" in %s.' % (sRegExp, self._asBuildFiles,));
        return None;

    def _waitForTestManagerConnectivity(self, cSecTimeout):
        """
        Check and wait for network connectivity to the test manager.

        This is used with the windows installation and uninstallation since
        these usually disrupts network connectivity when installing the filter
        driver.  If we proceed to quickly, we might finish the test at a time
        when we cannot report to the test manager and thus end up with an
        abandonded test error.
        """
        cSecElapsed = 0;
        secStart    = utils.timestampSecond();
        while reporter.checkTestManagerConnection() is False:
            cSecElapsed = utils.timestampSecond() - secStart;
            if cSecElapsed >= cSecTimeout:
                reporter.log('_waitForTestManagerConnectivity: Giving up after %u secs.' % (cSecTimeout,));
                return False;
            time.sleep(2);

        if cSecElapsed > 0:
            reporter.log('_waitForTestManagerConnectivity: Waited %s secs.' % (cSecTimeout,));
        return True;


    #
    # Darwin (Mac OS X).
    #

    def _darwinDmgPath(self):
        """ Returns the path to the DMG mount."""
        return os.path.join(self.sScratchPath, 'DmgMountPoint');

    def _darwinUnmountDmg(self, fIgnoreError):
        """
        Umount any DMG on at the default mount point.
        """
        sMountPath = self._darwinDmgPath();
        if not os.path.exists(sMountPath):
            return True;

        # Unmount.
        fRc = self._executeSync(['hdiutil', 'detach', sMountPath ]);
        if not fRc and not fIgnoreError:
            # In case it's busy for some reason or another, just retry after a little delay.
            for iTry in range(6):
                time.sleep(5);
                reporter.log('Retry #%s unmount DMG at %s' % (iTry + 1, sMountPath,));
                fRc = self._executeSync(['hdiutil', 'detach', sMountPath ]);
                if fRc:
                    break;
            if not fRc:
                reporter.error('Failed to unmount DMG at %s' % (sMountPath,));

        # Remove dir.
        try:
            os.rmdir(sMountPath);
        except:
            if not fIgnoreError:
                reporter.errorXcpt('Failed to remove directory %s' % (sMountPath,));
        return fRc;

    def _darwinMountDmg(self, sDmg):
        """
        Mount the DMG at the default mount point.
        """
        self._darwinUnmountDmg(fIgnoreError = True)

        sMountPath = self._darwinDmgPath();
        if not os.path.exists(sMountPath):
            try:
                os.mkdir(sMountPath, 0o755);
            except:
                reporter.logXcpt();
                return False;

        return self._executeSync(['hdiutil', 'attach', '-readonly', '-mount', 'required', '-mountpoint', sMountPath, sDmg, ]);

    def _generateWithoutKextsChoicesXmlOnDarwin(self):
        """
        Generates the choices XML when kernel drivers are disabled.
        None is returned on failure.
        """
        sPath = os.path.join(self.sScratchPath, 'DarwinChoices.xml');
        oFile = utils.openNoInherit(sPath, 'wt');
        oFile.write('<?xml version="1.0" encoding="UTF-8"?>\n'
                    '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'
                    '<plist version="1.0">\n'
                    '<array>\n'
                    '    <dict>\n'
                    '        <key>attributeSetting</key>\n'
                    '        <integer>0</integer>\n'
                    '        <key>choiceAttribute</key>\n'
                    '        <string>selected</string>\n'
                    '        <key>choiceIdentifier</key>\n'
                    '        <string>choiceVBoxKEXTs</string>\n'
                    '    </dict>\n'
                    '</array>\n'
                    '</plist>\n');
        oFile.close();
        return sPath;

    def _installVBoxOnDarwin(self):
        """ Installs VBox on Mac OS X."""

        # TEMPORARY HACK - START
        # Don't install the kernel drivers on the testboxes with BigSur and later
        # Needs a more generic approach but that one needs more effort.
        sHostName = socket.getfqdn();
        if    sHostName.startswith('testboxmac10') \
           or sHostName.startswith('testboxmac11'):
            self._fKernelDrivers = False;
        # TEMPORARY HACK - END

        sDmg = self._findFile('^VirtualBox-.*\\.dmg$');
        if sDmg is None:
            return False;

        # Mount the DMG.
        fRc = self._darwinMountDmg(sDmg);
        if fRc is not True:
            return False;

        # Uninstall any previous vbox version first.
        sUninstaller = os.path.join(self._darwinDmgPath(), 'VirtualBox_Uninstall.tool');
        fRc, _ = self._sudoExecuteSync([sUninstaller, '--unattended',]);
        if fRc is True:

            # Install the package.
            sPkg = os.path.join(self._darwinDmgPath(), 'VirtualBox.pkg');
            if self._fKernelDrivers:
                fRc, _ = self._sudoExecuteSync(['installer', '-verbose', '-dumplog', '-pkg', sPkg, '-target', '/']);
            else:
                sChoicesXml = self._generateWithoutKextsChoicesXmlOnDarwin();
                if sChoicesXml is not None:
                    fRc, _ = self._sudoExecuteSync(['installer', '-verbose', '-dumplog', '-pkg', sPkg, \
                                                    '-applyChoiceChangesXML', sChoicesXml, '-target', '/']);
                else:
                    fRc = False;

        # Unmount the DMG and we're done.
        if not self._darwinUnmountDmg(fIgnoreError = False):
            fRc = False;
        return fRc;

    def _uninstallVBoxOnDarwin(self):
        """ Uninstalls VBox on Mac OS X."""

        # Is VirtualBox installed? If not, don't try uninstall it.
        sVBox = self._getVBoxInstallPath(fFailIfNotFound = False);
        if sVBox is None:
            return True;

        # Find the dmg.
        sDmg = self._findFile('^VirtualBox-.*\\.dmg$');
        if sDmg is None:
            return False;
        if not os.path.exists(sDmg):
            return True;

        # Mount the DMG.
        fRc = self._darwinMountDmg(sDmg);
        if fRc is not True:
            return False;

        # Execute the uninstaller.
        sUninstaller = os.path.join(self._darwinDmgPath(), 'VirtualBox_Uninstall.tool');
        fRc, _ = self._sudoExecuteSync([sUninstaller, '--unattended',]);

        # Unmount the DMG and we're done.
        if not self._darwinUnmountDmg(fIgnoreError = False):
            fRc = False;
        return fRc;

    #
    # GNU/Linux
    #

    def _installVBoxOnLinux(self):
        """ Installs VBox on Linux."""
        sRun = self._findFile('^VirtualBox-.*\\.run$');
        if sRun is None:
            return False;
        utils.chmodPlusX(sRun);

        # Install the new one.
        fRc, _ = self._sudoExecuteSync([sRun,]);
        return fRc;

    def _uninstallVBoxOnLinux(self):
        """ Uninstalls VBox on Linux."""

        # Is VirtualBox installed? If not, don't try uninstall it.
        sVBox = self._getVBoxInstallPath(fFailIfNotFound = False);
        if sVBox is None:
            return True;

        # Find the .run file and use it.
        sRun = self._findFile('^VirtualBox-.*\\.run$', fMandatory = False);
        if sRun is not None:
            utils.chmodPlusX(sRun);
            fRc, _ = self._sudoExecuteSync([sRun, 'uninstall']);
            return fRc;

        # Try the installed uninstaller.
        for sUninstaller in [os.path.join(sVBox, 'uninstall.sh'), '/opt/VirtualBox/uninstall.sh', ]:
            if os.path.isfile(sUninstaller):
                reporter.log('Invoking "%s"...' % (sUninstaller,));
                fRc, _ = self._sudoExecuteSync([sUninstaller, 'uninstall']);
                return fRc;

        reporter.log('Did not find any VirtualBox install to uninstall.');
        return True;


    #
    # Solaris
    #

    def _generateAutoResponseOnSolaris(self):
        """
        Generates an autoresponse file on solaris, returning the name.
        None is return on failure.
        """
        sPath = os.path.join(self.sScratchPath, 'SolarisAutoResponse');
        oFile = utils.openNoInherit(sPath, 'wt');
        oFile.write('basedir=default\n'
                    'runlevel=nocheck\n'
                    'conflict=quit\n'
                    'setuid=nocheck\n'
                    'action=nocheck\n'
                    'partial=quit\n'
                    'instance=unique\n'
                    'idepend=quit\n'
                    'rdepend=quit\n'
                    'space=quit\n'
                    'mail=\n');
        oFile.close();
        return sPath;

    def _installVBoxOnSolaris(self):
        """ Installs VBox on Solaris."""
        sPkg = self._findFile('^VirtualBox-.*\\.pkg$', fMandatory = False);
        if sPkg is None:
            sTar = self._findFile('^VirtualBox-.*-SunOS-.*\\.tar.gz$', fMandatory = False);
            if sTar is not None:
                if self._maybeUnpackArchive(sTar) is not True:
                    return False;
        sPkg = self._findFile('^VirtualBox-.*\\.pkg$', fMandatory = True);
        sRsp = self._findFile('^autoresponse$', fMandatory = True);
        if sPkg is None or sRsp is None:
            return False;

        # Uninstall first (ignore result).
        self._uninstallVBoxOnSolaris(False);

        # Install the new one.
        fRc, _ = self._sudoExecuteSync(['pkgadd', '-d', sPkg, '-n', '-a', sRsp, 'SUNWvbox']);
        return fRc;

    def _uninstallVBoxOnSolaris(self, fRestartSvcConfigD):
        """ Uninstalls VBox on Solaris."""
        reporter.flushall();
        if utils.processCall(['pkginfo', '-q', 'SUNWvbox']) != 0:
            return True;
        sRsp = self._generateAutoResponseOnSolaris();
        fRc, _ = self._sudoExecuteSync(['pkgrm', '-n', '-a', sRsp, 'SUNWvbox']);

        #
        # Restart the svc.configd as it has a tendency to clog up with time and
        # become  unresponsive.  It will handle SIGHUP by exiting the sigwait()
        # look in the main function and shut down the service nicely (backend_fini).
        # The restarter will then start a new instance of it.
        #
        if fRestartSvcConfigD:
            time.sleep(1); # Give it a chance to flush pkgrm stuff.
            self._sudoExecuteSync(['pkill', '-HUP', 'svc.configd']);
            time.sleep(5); # Spare a few cpu cycles it to shutdown and restart.

        return fRc;

    #
    # Windows
    #

    ## VBox windows services we can query the status of.
    kasWindowsServices = [ 'vboxsup', 'vboxusbmon', 'vboxnetadp', 'vboxnetflt', 'vboxnetlwf' ];

    def _installVBoxOnWindows(self):
        """ Installs VBox on Windows."""
        sExe = self._findFile('^VirtualBox-.*-(MultiArch|Win).exe$');
        if sExe is None:
            return False;

        # TEMPORARY HACK - START
        # It seems that running the NDIS cleanup script upon uninstallation is not
        # a good idea, so let's run it before installing VirtualBox.
        #sHostName = socket.getfqdn();
        #if    not sHostName.startswith('testboxwin3') \
        #  and not sHostName.startswith('testboxharp2') \
        #  and not sHostName.startswith('wei01-b6ka-3') \
        #  and utils.getHostOsVersion() in ['8', '8.1', '9', '2008Server', '2008ServerR2', '2012Server']:
        #    reporter.log('Peforming extra NDIS cleanup...');
        #    sMagicScript = os.path.abspath(os.path.join(g_ksValidationKitDir, 'testdriver', 'win-vbox-net-uninstall.ps1'));
        #    fRc2, _ = self._sudoExecuteSync(['powershell.exe', '-Command', 'set-executionpolicy unrestricted']);
        #    if not fRc2:
        #        reporter.log('set-executionpolicy failed.');
        #    self._sudoExecuteSync(['powershell.exe', '-Command', 'get-executionpolicy']);
        #    fRc2, _ = self._sudoExecuteSync(['powershell.exe', '-File', sMagicScript]);
        #    if not fRc2:
        #        reporter.log('NDIS cleanup failed.');
        # TEMPORARY HACK - END

        # Uninstall any previous vbox version first.
        fRc = self._uninstallVBoxOnWindows('install');
        if fRc is not True:
            return None; # There shouldn't be anything to uninstall, and if there is, it's not our fault.

        # Install the MS Visual Studio Redistributable, if requested. (VBox 7.0+ needs this installed once.)
        if self._fInstallMsCrt:
            reporter.log('Installing MS Visual Studio Redistributable (untested code)...');
            ## @todo Test this.
            ## @todo We could cache this on the testrsrc share.
            sName = "vc_redist.x64.exe"
            sUrl  = "https://aka.ms/vs/17/release/" + sName # Permalink, according to MS.
            sExe  = os.path.join(self.sBuildPath, sName);
            if webutils.downloadFile(sUrl, sExe, None, reporter.log, reporter.log):
                asArgs = [ sExe, '/Q' ];
                fRc2, iRc = self._sudoExecuteSync(asArgs);
                if fRc2 is False:
                    return reporter.error('Installing MS Visual Studio Redistributable failed, exit code: %s' % (iRc,));
                reporter.log('Installing MS Visual Studio Redistributable done');
            else:
                return False;

        # We need the help text to detect supported options below.
        reporter.log('Executing: %s' % ([sExe, '--silent', '--help'], ));
        reporter.flushall();
        (iExitCode, sHelp, _) = utils.processOutputUnchecked([sExe, '--silent', '--help'], fIgnoreEncoding = True);
        reporter.log('Exit code: %d, %u chars of help text' % (iExitCode, len(sHelp),));

        # Gather installer arguments.
        asArgs = [sExe, '-vvvv', '--silent', '--logging'];
        asArgs.extend(['--msiparams', 'REBOOT=ReallySuppress']);
        sVBoxInstallPath = os.environ.get('VBOX_INSTALL_PATH', None);
        if sVBoxInstallPath is not None:
            asArgs.extend(['INSTALLDIR="%s"' % (sVBoxInstallPath,)]);

        if sHelp.find("--msi-log-file") >= 0:
            sLogFile = os.path.join(self.sScratchPath, 'VBoxInstallLog.txt'); # Specify location to prevent a random one.
            asArgs.extend(['--msi-log-file', sLogFile]);
        else:
            sLogFile = os.path.join(tempfile.gettempdir(), 'VirtualBox', 'VBoxInstallLog.txt'); # Hardcoded TMP location.

        if self._fWinForcedInstallTimestampCA and sHelp.find("--force-install-timestamp-ca") >= 0:
            asArgs.extend(['--force-install-timestamp-ca']);

        # Install it.
        fRc2, iRc = self._sudoExecuteSync(asArgs);
        if fRc2 is False:
            if iRc == 3010: # ERROR_SUCCESS_REBOOT_REQUIRED
                reporter.error('Installer required a reboot to complete installation (ERROR_SUCCESS_REBOOT_REQUIRED)');
            else:
                reporter.error('Installer failed, exit code: %s' % (iRc,));
            fRc = False;

        # Add the installer log if present and wait for the network connection to be restore after the filter driver upset.
        if os.path.isfile(sLogFile):
            reporter.addLogFile(sLogFile, 'log/installer', "Verbose MSI installation log file");
        self._waitForTestManagerConnectivity(30);

        return fRc;

    def _isProcessPresent(self, sName):
        """ Checks whether the named process is present or not. """
        for oProcess in utils.processListAll():
            sBase = oProcess.getBaseImageNameNoExeSuff();
            if sBase is not None and sBase.lower() == sName:
                return True;
        return False;

    def _killProcessesByName(self, sName, sDesc, fChildren = False):
        """ Kills the named process, optionally including children. """
        cKilled = 0;
        aoProcesses = utils.processListAll();
        for oProcess in aoProcesses:
            sBase = oProcess.getBaseImageNameNoExeSuff();
            if sBase is not None and sBase.lower() == sName:
                reporter.log('Killing %s process: %s (%s)' % (sDesc, oProcess.iPid, sBase));
                utils.processKill(oProcess.iPid);
                cKilled += 1;

                if fChildren:
                    for oChild in aoProcesses:
                        if oChild.iParentPid == oProcess.iPid and oChild.iParentPid is not None:
                            reporter.log('Killing %s child process: %s (%s)' % (sDesc, oChild.iPid, sBase));
                            utils.processKill(oChild.iPid);
                            cKilled += 1;
        return cKilled;

    def _terminateProcessesByNameAndArgSubstr(self, sName, sArg, sDesc):
        """
        Terminates the named process using taskkill.exe, if any of its args
        contains the passed string.
        """
        cKilled = 0;
        aoProcesses = utils.processListAll();
        for oProcess in aoProcesses:
            sBase = oProcess.getBaseImageNameNoExeSuff();
            if sBase is not None and sBase.lower() == sName and any(sArg in s for s in oProcess.asArgs):

                reporter.log('Killing %s process: %s (%s)' % (sDesc, oProcess.iPid, sBase));
                self._executeSync(['taskkill.exe', '/pid', '%u' % (oProcess.iPid,)]);
                cKilled += 1;
        return cKilled;

    def _uninstallVBoxOnWindows(self, sMode):
        """
        Uninstalls VBox on Windows, all installations we find to be on the safe side...
        """
        assert sMode in ['install', 'uninstall',];

        import win32com.client; # pylint: disable=import-error
        win32com.client.gencache.EnsureModule('{000C1092-0000-0000-C000-000000000046}', 1033, 1, 0);
        oInstaller = win32com.client.Dispatch('WindowsInstaller.Installer',
                                              resultCLSID = '{000C1090-0000-0000-C000-000000000046}')

        # Search installed products for VirtualBox.
        asProdCodes = [];
        for sProdCode in oInstaller.Products:
            try:
                sProdName = oInstaller.ProductInfo(sProdCode, "ProductName");
            except:
                reporter.logXcpt();
                continue;
            #reporter.log('Info: %s=%s' % (sProdCode, sProdName));
            if sProdName.startswith('Oracle VirtualBox') \
            or sProdName.startswith('Oracle VM VirtualBox') \
            or sProdName.startswith('Sun VirtualBox'):
                asProdCodes.append([sProdCode, sProdName]);

        # Before we start uninstalling anything, just ruthlessly kill any cdb,
        # msiexec, drvinst and some rundll process we might find hanging around.
        if self._isProcessPresent('rundll32'):
            cTimes = 0;
            while cTimes < 3:
                cTimes += 1;
                cKilled = self._terminateProcessesByNameAndArgSubstr('rundll32', 'InstallSecurityPromptRunDllW',
                                                                     'MSI driver installation');
                if cKilled <= 0:
                    break;
                time.sleep(10); # Give related drvinst process a chance to clean up after we killed the verification dialog.

        if self._isProcessPresent('drvinst'):
            time.sleep(15);     # In the hope that it goes away.
            cTimes = 0;
            while cTimes < 4:
                cTimes += 1;
                cKilled = self._killProcessesByName('drvinst', 'MSI driver installation', True);
                if cKilled <= 0:
                    break;
                time.sleep(10); # Give related MSI process a chance to clean up after we killed the driver installer.

        if self._isProcessPresent('msiexec'):
            cTimes = 0;
            while cTimes < 3:
                reporter.log('found running msiexec process, waiting a bit...');
                time.sleep(20)  # In the hope that it goes away.
                if not self._isProcessPresent('msiexec'):
                    break;
                cTimes += 1;
            ## @todo this could also be the msiexec system service, try to detect this case!
            if cTimes >= 6:
                cKilled = self._killProcessesByName('msiexec', 'MSI driver installation');
                if cKilled > 0:
                    time.sleep(16); # fudge.

        # cdb.exe sometimes stays running (from utils.getProcessInfo), blocking
        # the scratch directory. No idea why.
        if self._isProcessPresent('cdb'):
            cTimes = 0;
            while cTimes < 3:
                cKilled = self._killProcessesByName('cdb', 'cdb.exe from getProcessInfo');
                if cKilled <= 0:
                    break;
                time.sleep(2); # fudge.

        # Do the uninstalling.
        fRc = True;
        sLogFile = os.path.join(self.sScratchPath, 'VBoxUninstallLog.txt');
        for sProdCode, sProdName in asProdCodes:
            reporter.log('Uninstalling %s (%s)...' % (sProdName, sProdCode));
            fRc2, iRc = self._sudoExecuteSync(['msiexec', '/uninstall', sProdCode, '/quiet', '/passive', '/norestart',
                                               '/L*v', '%s' % (sLogFile), ]);
            if fRc2 is False:
                if iRc == 3010: # ERROR_SUCCESS_REBOOT_REQUIRED
                    reporter.error('Uninstaller required a reboot to complete uninstallation');
                else:
                    reporter.error('Uninstaller failed, exit code: %s' % (iRc,));
                fRc = False;

        self._waitForTestManagerConnectivity(30);

        # Upload the log on failure.  Do it early if the extra cleanups below causes trouble.
        if fRc is False and os.path.isfile(sLogFile):
            reporter.addLogFile(sLogFile, 'log/uninstaller', "Verbose MSI uninstallation log file");
            sLogFile = None;

        # Log driver service states (should ls \Driver\VBox* and \Device\VBox*).
        fHadLeftovers = False;
        asLeftovers = [];
        for sService in reversed(self.kasWindowsServices):
            cTries = 0;
            while True:
                fRc2, _ = self._sudoExecuteSync(['sc.exe', 'query', sService]);
                if not fRc2:
                    break;
                fHadLeftovers = True;

                cTries += 1;
                if cTries > 3:
                    asLeftovers.append(sService,);
                    break;

                # Get the status output.
                try:
                    sOutput = utils.sudoProcessOutputChecked(['sc.exe', 'query', sService]);
                except:
                    reporter.logXcpt();
                else:
                    if re.search(r'STATE\s+:\s*1\s*STOPPED', sOutput) is None:
                        reporter.log('Trying to stop %s...' % (sService,));
                        fRc2, _ = self._sudoExecuteSync(['sc.exe', 'stop', sService]);
                        time.sleep(1); # fudge

                    reporter.log('Trying to delete %s...' % (sService,));
                    self._sudoExecuteSync(['sc.exe', 'delete', sService]);

                time.sleep(1); # fudge

        if asLeftovers:
            reporter.log('Warning! Leftover VBox drivers: %s' % (', '.join(asLeftovers),));
            fRc = False;

        if fHadLeftovers:
            self._waitForTestManagerConnectivity(30);

        # Upload the log if we have any leftovers and didn't upload it already.
        if sLogFile is not None and (fRc is False or fHadLeftovers) and os.path.isfile(sLogFile):
            reporter.addLogFile(sLogFile, 'log/uninstaller', "Verbose MSI uninstallation log file");

        return fRc;


    #
    # Extension pack.
    #

    def _getVBoxInstallPath(self, fFailIfNotFound):
        """ Returns the default VBox installation path. """
        sHost = utils.getHostOs();
        if sHost == 'win':
            sProgFiles = os.environ.get('ProgramFiles', 'C:\\Program Files');
            asLocs = [
                os.path.join(sProgFiles, 'Oracle', 'VirtualBox'),
                os.path.join(sProgFiles, 'OracleVM', 'VirtualBox'),
                os.path.join(sProgFiles, 'Sun', 'VirtualBox'),
            ];
        elif sHost in ('linux', 'solaris',):
            asLocs = [ '/opt/VirtualBox', '/opt/VirtualBox-3.2', '/opt/VirtualBox-3.1', '/opt/VirtualBox-3.0'];
        elif sHost == 'darwin':
            asLocs = [ '/Applications/VirtualBox.app/Contents/MacOS' ];
        else:
            asLocs = [ '/opt/VirtualBox' ];
        if 'VBOX_INSTALL_PATH' in os.environ:
            asLocs.insert(0, os.environ.get('VBOX_INSTALL_PATH', None));

        for sLoc in asLocs:
            if os.path.isdir(sLoc):
                return sLoc;
        if fFailIfNotFound:
            reporter.error('Failed to locate VirtualBox installation: %s' % (asLocs,));
        else:
            reporter.log2('Failed to locate VirtualBox installation: %s' % (asLocs,));
        return None;

    ksExtPackBasenames = [ 'Oracle_VirtualBox_Extension_Pack', 'Oracle_VM_VirtualBox_Extension_Pack', ];

    def _findExtPack(self):
        """ Locates the extension pack file. """
        for sExtPackBasename in self.ksExtPackBasenames:
            sExtPack = self._findFile('%s.vbox-extpack' % (sExtPackBasename,));
            if sExtPack is None:
                sExtPack = self._findFile('%s.*.vbox-extpack' % (sExtPackBasename,));
            if sExtPack is not None:
                return (sExtPack, sExtPackBasename);
        return (None, None);

    def _installExtPack(self):
        """ Installs the extension pack. """
        sVBox = self._getVBoxInstallPath(fFailIfNotFound = True);
        if sVBox is None:
            return False;
        sExtPackDir = os.path.join(sVBox, 'ExtensionPacks');

        if self._uninstallAllExtPacks() is not True:
            return False;

        (sExtPack, sExtPackBasename) = self._findExtPack();
        if sExtPack is None:
            return True;

        sDstDir = os.path.join(sExtPackDir, sExtPackBasename);
        reporter.log('Installing extension pack "%s" to "%s"...' % (sExtPack, sExtPackDir));
        fRc, _ = self._sudoExecuteSync([ self.getBinTool('vts_tar'),
                                         '--extract',
                                         '--verbose',
                                         '--gzip',
                                         '--file',                sExtPack,
                                         '--directory',           sDstDir,
                                         '--file-mode-and-mask',  '0644',
                                         '--file-mode-or-mask',   '0644',
                                         '--dir-mode-and-mask',   '0755',
                                         '--dir-mode-or-mask',    '0755',
                                         '--owner',               '0',
                                         '--group',               '0',
                                       ]);
        return fRc;

    def _uninstallAllExtPacks(self):
        """ Uninstalls all extension packs. """
        sVBox = self._getVBoxInstallPath(fFailIfNotFound = False);
        if sVBox is None:
            return True;

        sExtPackDir = os.path.join(sVBox, 'ExtensionPacks');
        if not os.path.exists(sExtPackDir):
            return True;

        fRc, _ = self._sudoExecuteSync([self.getBinTool('vts_rm'), '-Rfv', '--', sExtPackDir]);
        return fRc;



if __name__ == '__main__':
    sys.exit(VBoxInstallerTestDriver().main(sys.argv));
