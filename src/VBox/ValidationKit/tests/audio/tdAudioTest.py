# -*- coding: utf-8 -*-
# $Id: tdAudioTest.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
AudioTest test driver which invokes the VKAT (Validation Kit Audio Test)
binary to perform the actual audio tests.

The generated test set archive on the guest will be downloaded by TXS
to the host for later audio comparison / verification.
"""

__copyright__ = \
"""
Copyright (C) 2021-2024 Oracle and/or its affiliates.

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
from datetime import datetime
import os
import sys
import subprocess
import time
import threading

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from testdriver import reporter
from testdriver import base
from testdriver import vbox
from testdriver import vboxcon;
from testdriver import vboxtestvms
from common     import utils;

# pylint: disable=unnecessary-semicolon

class tdDebugSettings(object):
    """
    Contains local test debug settings.
    """
    def __init__(self, sVkatExeHst = None):
        # Absolute path of VKAT on the host side which gets uploaded from the host.
        self.sVkatExeHst = sVkatExeHst;
        # Absolute path of VKAT on the guest side (which got uploaded from the host).
        self.sVkatExeGst = None;

class tdAudioTest(vbox.TestDriver):
    """
    Runs various audio tests.
    """
    def __init__(self):
        vbox.TestDriver.__init__(self);
        self.oTestVmSet       = self.oTestVmManager.getSmokeVmSet('nat');
        self.asGstVkatPaths   = [
            # Debugging stuff (SCP'd over to the guest).
            '/tmp/vkat',
            '/tmp/VBoxAudioTest',
            'C:\\Temp\\vkat',
            'C:\\Temp\\VBoxAudioTest',
            # Validation Kit .ISO.
            '${CDROM}/vboxvalidationkit/${OS/ARCH}/vkat${EXESUFF}',
            '${CDROM}/${OS/ARCH}/vkat${EXESUFF}',
            # Test VMs.
            '/opt/apps/vkat',
            '/opt/apps/VBoxAudioTest',
            '/apps/vkat',
            '/apps/VBoxAudioTest',
            'C:\\Apps\\vkat${EXESUFF}',
            'C:\\Apps\\VBoxAudioTest${EXESUFF}',
            ## @todo VBoxAudioTest on Guest Additions?
        ];
        self.asTestsDef       = [
            'guest_tone_playback', 'guest_tone_recording'
        ];
        self.asTests          = self.asTestsDef;
        self.oDebug           = tdDebugSettings();

        # Optional arguments passing to VKAT when doing the actual audio tests.
        self.asVkatTestArgs   = [];
        # Optional arguments passing to VKAT when verifying audio test sets.
        self.asVkatVerifyArgs = [];

        # Exit code of last host process execution, shared between exeuction thread and main thread.
        # This ASSUMES that we only have one thread running at a time. Rather hacky, but does the job for now.
        self.iThreadHstProcRc = 0;

        # Enable audio debug mode.
        #
        # This is needed in order to load and use the Validation Kit audio driver,
        # which in turn is being used in conjunction with the guest side to record
        # output (guest is playing back) and injecting input (guest is recording).
        self.asOptExtraData   = [
            'VBoxInternal2/Audio/Debug/Enabled:true',
        ];

        # Name of the running VM to use for running the test driver. Optional, and None if not being used.
        self.sRunningVmName   = None;

        # Audio controller type to use.
        # If set to None, the OS' recommended controller type will be used (defined by Main).
        self.sAudioControllerType = None;

    def showUsage(self):
        """
        Shows the audio test driver-specific command line options.
        """
        fRc = vbox.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('tdAudioTest Options:');
        reporter.log('  --runningvmname <vmname>');
        reporter.log('  --audio-tests   <s1[:s2[:]]>');
        reporter.log('      Default: %s  (all)' % (':'.join(self.asTestsDef)));
        reporter.log('  --audio-controller-type <HDA|AC97|SB16>');
        reporter.log('      Default: recommended controller');
        reporter.log('  --audio-debug-img <path/to/vkat>');
        reporter.log('  --audio-test-count <number>');
        reporter.log('      Default: 0 (means random)');
        reporter.log('  --audio-test-timeout <ms>');
        reporter.log('      Default: 5 minutes (300000)');
        reporter.log('  --audio-test-tone-duration <ms>');
        reporter.log('      Default: 0 (means random)');
        reporter.log('  --audio-verify-max-diff-count <number>');
        reporter.log('      Default: 0 (strict)');
        reporter.log('  --audio-verify-max-diff-percent <0-100>');
        reporter.log('      Default: 0 (strict)');
        reporter.log('  --audio-verify-max-size-percent <0-100>');
        reporter.log('      Default: 0 (strict)');
        return fRc;

    def parseOption(self, asArgs, iArg):
        """
        Parses the audio test driver-specific command line options.
        """
        if asArgs[iArg] == '--runningvmname':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--runningvmname" needs VM name');

            self.sRunningVmName = asArgs[iArg];
        elif asArgs[iArg] == '--audio-tests':
            iArg += 1;
            if asArgs[iArg] == 'all': # Nice for debugging scripts.
                self.asTests = self.asTestsDef;
            else:
                self.asTests = asArgs[iArg].split(':');
                for s in self.asTests:
                    if s not in self.asTestsDef:
                        raise base.InvalidOption('The "--audio-tests" value "%s" is not valid; valid values are: %s'
                                                    % (s, ' '.join(self.asTestsDef)));
        elif asArgs[iArg] == '--audio-controller-type':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('Option "%s" needs a value' % (asArgs[iArg - 1]));
            if    asArgs[iArg] == 'HDA' \
               or asArgs[iArg] == 'AC97' \
               or asArgs[iArg] == 'SB16':
                self.sAudioControllerType = asArgs[iArg];
            else:
                raise base.InvalidOption('The "--audio-controller-type" value "%s" is not valid' % (asArgs[iArg]));
        elif asArgs[iArg] == '--audio-debug-img':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('Option "%s" needs a value' % (asArgs[iArg - 1]));
            self.oDebug.sVkatExeHst = asArgs[iArg];
        elif    asArgs[iArg] == '--audio-test-count' \
             or asArgs[iArg] == '--audio-test-tone-duration' \
             or asArgs[iArg] == '--audio-test-timeout':
            fHandleOption = True;
            if  asArgs[iArg] == '--audio-test-timeout' \
            and (self.fpApiVer < 7.1 or self.uRevision < 161834):
                fHandleOption = False; # Older VKAT builds don't know about this option.
            # Strip the "--audio-test-" prefix and keep the options as defined in VKAT,
            # e.g. "--audio-test-count" -> "--count". That way we don't
            # need to do any special argument translation and whatnot.
            if fHandleOption:
                self.asVkatTestArgs.extend(['--' + asArgs[iArg][len('--audio-test-'):]]);
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('Option "%s" needs a value' % (asArgs[iArg - 1]));
            if fHandleOption:
                self.asVkatTestArgs.extend([asArgs[iArg]]);
        elif    asArgs[iArg] == '--audio-verify-max-diff-count' \
             or asArgs[iArg] == '--audio-verify-max-diff-percent' \
             or asArgs[iArg] == '--audio-verify-max-size-percent':
            # Strip the "--audio-verify-" prefix and keep the options as defined in VKAT,
            # e.g. "--audio-verify-max-diff-count" -> "--max-diff-count". That way we don't
            # need to do any special argument translation and whatnot.
            self.asVkatVerifyArgs.extend(['--' + asArgs[iArg][len('--audio-verify-'):]]);
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('Option "%s" needs a value' % (asArgs[iArg - 1]));
            self.asVkatVerifyArgs.extend([asArgs[iArg]]);
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def prepareGuestForDebugging(self, oSession, oTxsSession, oTestVm): # pylint: disable=unused-argument
        """
        Prepares a guest for (manual) debugging.

        This involves copying over and invoking a the locally built VKAT binary.
        """

        if self.oDebug.sVkatExeHst is None: # If no debugging enabled, bail out.
            reporter.log('Skipping debugging');
            return True;

        self.oDebug.sVkatExeGst = oTestVm.pathJoin(self.getGuestTempDir(oTestVm), 'vkat${EXESUFF}');

        reporter.log('Preparing for debugging ...');

        try:
            reporter.log('Uploading "%s" to "%s" ...' % (self.oDebug.sVkatExeHst, self.oDebug.sVkatExeGst));
            oTxsSession.syncUploadFile(self.oDebug.sVkatExeHst, self.oDebug.sVkatExeGst);

            if oTestVm.isLinux():
                oTxsSession.syncChMod(self.oDebug.sVkatExeGst, 0o755);
        except:
            return reporter.errorXcpt('Unable to prepare for debugging');

        return True;

    def actionVerify(self):
        """
        Verifies the test driver before running.
        """
        if self.sVBoxValidationKitIso is None or not os.path.isfile(self.sVBoxValidationKitIso):
            reporter.error('Cannot find the VBoxValidationKit.iso! (%s)'
                           'Please unzip a Validation Kit build in the current directory or in some parent one.'
                           % (self.sVBoxValidationKitIso,) );
            return False;
        return vbox.TestDriver.actionVerify(self);

    def actionConfig(self):
        """
        Configures the test driver before running.
        """
        if not self.importVBoxApi(): # So we can use the constant below.
            return False;

        # Make sure that the Validation Kit .ISO is mounted
        # to find the VKAT (Validation Kit Audio Test) binary on it.
        assert self.sVBoxValidationKitIso is not None;
        return self.oTestVmSet.actionConfig(self, sDvdImage = self.sVBoxValidationKitIso);

    def actionExecute(self):
        """
        Executes the test driver.
        """

        # Disable maximum logging line restrictions per group.
        # This comes in handy when running this test driver in a (very) verbose mode, e.g. for debugging.
        os.environ['VBOX_LOG_MAX_PER_GROUP'] = '0';
        os.environ['VBOX_RELEASE_LOG_MAX_PER_GROUP'] = '0';
        os.environ['VKAT_RELEASE_LOG_MAX_PER_GROUP'] = '0';

        if self.sRunningVmName is None:
            return self.oTestVmSet.actionExecute(self, self.testOneVmConfig);
        return self.actionExecuteOnRunnigVM();

    def actionExecuteOnRunnigVM(self):
        """
        Executes the tests in an already configured + running VM.
        """
        if not self.importVBoxApi():
            return False;

        fRc = True;

        oVM         = None;
        oVirtualBox = None;

        oVirtualBox = self.oVBoxMgr.getVirtualBox();
        try:
            oVM = oVirtualBox.findMachine(self.sRunningVmName);
            if oVM.state != self.oVBoxMgr.constants.MachineState_Running:
                reporter.error("Machine '%s' is not in Running state (state is %d)" % (self.sRunningVmName, oVM.state));
                fRc = False;
        except:
            reporter.errorXcpt("Machine '%s' not found" % (self.sRunningVmName));
            fRc = False;

        if fRc:
            oSession = self.openSession(oVM);
            if oSession:
                # Tweak this to your likings.
                oTestVm = vboxtestvms.TestVm('runningvm', sKind = 'WindowsXP'); #sKind = 'WindowsXP' # sKind = 'Ubuntu_64'
                (fRc, oTxsSession) = self.txsDoConnectViaTcp(oSession, 30 * 1000);
                if fRc:
                    self.doTest(oTestVm, oSession, oTxsSession);
            else:
                reporter.error("Unable to open session for machine '%s'" % (self.sRunningVmName));
                fRc = False;

        if oVM:
            del oVM;
        if oVirtualBox:
            del oVirtualBox;
        return fRc;

    def getGstVkatLogFilePath(self, oTestVm):
        """
        Returns the log file path of VKAT running on the guest (daemonized).
        """
        return oTestVm.pathJoin(self.getGuestTempDir(oTestVm), 'vkat-guest.log');

    def locateGstBinary(self, oSession, oTxsSession, asPaths):
        """
        Locates a guest binary on the guest by checking the paths in \a asPaths.
        """
        for sCurPath in asPaths:
            reporter.log2('Checking for \"%s\" ...' % (sCurPath));
            if self.txsIsFile(oSession, oTxsSession, sCurPath, fIgnoreErrors = True):
                return (True, sCurPath);
        reporter.error('Unable to find guest binary in any of these places:\n%s' % ('\n'.join(asPaths),));
        return (False, "");

    def executeHstLoop(self, sWhat, asArgs, asEnv = None, fAsAdmin = False):
        """
        Inner loop which handles the execution of a host binary.

        Might be called synchronously in main thread or via the thread exeuction helper (asynchronous).

        Returns (success status, exit code).
        """
        fRc = False;
        iRc = -42;

        asEnvTmp = os.environ.copy();
        if asEnv:
            for sEnv in asEnv:
                sKey, sValue = sEnv.split('=');
                reporter.log2('Setting env var \"%s\" -> \"%s\"' % (sKey, sValue));
                os.environ[sKey] = sValue; # Also apply it to the current environment.
                asEnvTmp[sKey]   = sValue;

        try:
            # Spawn process.
            if  fAsAdmin \
            and utils.getHostOs() != 'win':
                oProcess = utils.sudoProcessStart(asArgs, env = asEnvTmp, stdout=subprocess.PIPE, stderr=subprocess.STDOUT);
            else:
                oProcess = utils.processStart(asArgs, env = asEnvTmp, stdout=subprocess.PIPE, stderr=subprocess.STDOUT);

            if not oProcess:
                reporter.error('Starting process for "%s" failed!' % (sWhat));
                return fRc, iRc;

            iPid = oProcess.pid;
            self.pidFileAdd(iPid, sWhat);

            while True if sys.version_info[0] < 3 else oProcess.stdout.readable(): # pylint: disable=no-member
                try:
                    sStdOut = oProcess.stdout.readline();
                    if  sStdOut \
                    and isinstance(sStdOut, str):
                        sStdOut = sStdOut.strip();
                        reporter.log('%s [stdout]: %s' % (sWhat, sStdOut.rstrip('\n'),));
                except:
                    reporter.log('%s [stdout]: <Unable to read output>' % (sWhat,));
                self.processEvents(0);
                iRc = oProcess.poll();
                if iRc is not None:
                    break;

            if iRc == 0:
                reporter.log('*** %s: exit code %d' % (sWhat, iRc));
            else:
                reporter.log('!*! %s: exit code %d' % (sWhat, iRc));

            fRc = True;
            self.pidFileRemove(iPid);
        except:
            reporter.logXcpt('Executing "%s" failed!' % (sWhat));

        reporter.log('Executing \"%s\" on host %s' % (sWhat, 'done' if fRc else 'failed',));
        return (fRc, iRc);

    def executeHstThread(self, sWhat, asArgs, asEnv = None, fAsAdmin = False):
        """
        Thread execution helper to run a process on the host.
        """
        _, iRc = self.executeHstLoop(sWhat, asArgs, asEnv, fAsAdmin);

        # Save thread result code.
        self.iThreadHstProcRc = iRc;

    def executeHst(self, sWhat, asArgs, asEnv = None, fAsAdmin = False, fBlocking = True, iExpectedRc = None):
        """
        Runs a binary (image) with optional admin (root) rights on the host and
        waits until it terminates.

        Windows currently is not supported yet running stuff as Administrator.

        Returns (success status, exit code).
        """
        reporter.log('Executing \"%s\" on host (as admin = %s, blocking = %s, expected rc = %s)'
                     % (sWhat, fAsAdmin, fBlocking, str(iExpectedRc) if iExpectedRc is not None else '<None>'));
        reporter.log2('Arguments: %s' % (asArgs,));
        if asEnv:
            reporter.log2('Environment: %s' % (asEnv,));

        try:    sys.stdout.flush();
        except: pass;
        try:    sys.stderr.flush();
        except: pass;

        fRc = False;
        iRc = -42;

        if fBlocking: # Run in same thread (blocking).
            fRc, iRc = self.executeHstLoop(sWhat, asArgs, asEnv, fAsAdmin);
        else: # Run in separate thread (asynchronous).
            self.iThreadHstProcRc = -42; # Initialize thread rc.
            try:
                oThread = threading.Thread(target = self.executeHstThread, args = [ sWhat, asArgs, asEnv, fAsAdmin ]);
                oThread.start();
                while oThread.join(0.1):
                    if not oThread.is_alive():
                        break;
                    self.processEvents(0);
                reporter.log2('Thread returned exit code for "%s": %d' % (sWhat, self.iThreadHstProcRc));
                fRc = True;
                iRc = self.iThreadHstProcRc;
            except:
                reporter.logXcpt('Starting thread for "%s" failed' % (sWhat,));

        # Adjust fRc if caller expected a specific exit code.
        if  iExpectedRc is not None \
        and (iRc != iExpectedRc):
            reporter.error('Executing \"%s\" on host failed (got exit code %d, expected %d'
                           % (sWhat, iRc, iExpectedRc,));
            fRc = False;

        reporter.log2('Got fRc = %s + exit code %d' % (fRc, iRc,));
        return fRc, iRc;

    def getWinFirewallArgsDisable(self, sOsType):
        """
        Returns the command line arguments for Windows OSes
        to disable the built-in firewall (if any).

        If not supported, returns an empty array.
        """
        if   sOsType == 'vista': # pylint: disable=no-else-return
             # Vista and up.
            return (['netsh.exe', 'advfirewall', 'set', 'allprofiles', 'state', 'off']);
        elif sOsType == 'xp':   # Older stuff (XP / 2003).
            return(['netsh.exe', 'firewall', 'set', 'opmode', 'mode=DISABLE']);
        # Not supported / available.
        return [];

    def disableGstFirewall(self, oTestVm, oTxsSession):
        """
        Disables the firewall on a guest (if any).

        Needs elevated / admin / root privileges.

        Returns success status, not logged.
        """
        fRc = False;

        asArgs  = [];
        sOsType = '';
        if oTestVm.isWindows():
            if oTestVm.sKind in ['WindowsNT4', 'WindowsNT3x']:
                sOsType = 'nt3x'; # Not supported, but define it anyway.
            elif oTestVm.sKind in ('Windows2000', 'WindowsXP', 'Windows2003'):
                sOsType = 'xp';
            else:
                sOsType = 'vista';
            asArgs = self.getWinFirewallArgsDisable(sOsType);
        else:
            sOsType = 'unsupported';

        reporter.log('Disabling firewall on guest (type: %s) ...' % (sOsType,));

        if asArgs:
            fRc = self.txsRunTest(oTxsSession, 'Disabling guest firewall', 3 * 60 * 1000, \
                                  oTestVm.pathJoin(self.getGuestSystemDir(oTestVm), asArgs[0]), asArgs);
            if not fRc:
                reporter.error('Disabling firewall on guest returned exit code error %d' % (self.getLastRcFromTxs(oTxsSession)));
        else:
            reporter.log('Firewall not available on guest, skipping');
            fRc = True; # Not available, just skip.

        return fRc;

    def disableHstFirewall(self):
        """
        Disables the firewall on the host (if any).

        Needs elevated / admin / root privileges.

        Returns success status, not logged.
        """
        fRc = False;

        asArgs  = [];
        sOsType = sys.platform;

        if sOsType == 'win32':
            reporter.log('Disabling firewall on host (type: %s) ...' % (sOsType));

            ## @todo For now we ASSUME that we don't run (and don't support even) on old(er)
            #        Windows hosts than Vista.
            asArgs = self.getWinFirewallArgsDisable('vista');
            if asArgs:
                fRc, _ = self.executeHst('Disabling host firewall', asArgs, fAsAdmin = True);
        else:
            reporter.log('Firewall not available on host, skipping');
            fRc = True; # Not available, just skip.

        return fRc;

    def getLastRcFromTxs(self, oTxsSession):
        """
        Extracts the last exit code reported by TXS from a run before.
        Assumes that nothing else has been run on the same TXS session in the meantime.
        """
        iRc = 0;
        (_, sOpcode, abPayload) = oTxsSession.getLastReply();
        if sOpcode.startswith('PROC NOK '): # Extract process rc
            iRc = abPayload[0]; # ASSUMES 8-bit rc for now.
        return iRc;

    def startVkatOnGuest(self, oTestVm, oSession, oTxsSession, sTag):
        """
        Starts VKAT on the guest (running in background).
        """
        sPathTemp      = self.getGuestTempDir(oTestVm);
        sPathAudioOut  = oTestVm.pathJoin(sPathTemp, 'vkat-guest-out');
        sPathAudioTemp = oTestVm.pathJoin(sPathTemp, 'vkat-guest-temp');

        reporter.log('Guest audio test temp path is \"%s\"' % (sPathAudioOut));
        reporter.log('Guest audio test output path is \"%s\"' % (sPathAudioTemp));
        reporter.log('Guest audio test tag is \"%s\"' % (sTag));

        if self.oDebug.sVkatExeGst is None:
            fRc, sVkatExe = self.locateGstBinary(oSession, oTxsSession, self.asGstVkatPaths);
        else:
            sVkatExe = self.oDebug.sVkatExeGst;
            fRc      = True;
        if fRc:
            reporter.log('Using VKAT on guest at \"%s\"' % (sVkatExe));

            sCmd   = '';
            asArgs = [];

            asArgsVkat = [ sVkatExe, 'test', '--mode', 'guest', '--probe-backends', \
                           '--tempdir', sPathAudioTemp, '--outdir', sPathAudioOut, \
                           '--tag', sTag ];

            asArgs.extend(asArgsVkat);

            for _ in range(1, reporter.getVerbosity()): # Verbosity always is initialized at 1.
                asArgs.extend([ '-v' ]);

            # Needed for NATed VMs.
            asArgs.extend(['--tcp-connect-addr', '10.0.2.2' ]);

            if oTestVm.sKind in 'Oracle_64':
                #
                # Some Linux distros have a bug / are configured (?) so that processes started by init system
                # cannot access the PulseAudio server ("Connection refused"), for example OL 8.1.
                #
                # To work around this, we use the (hopefully) configured user "vbox" and run it under its behalf,
                # as the Test Execution Service (TxS) currently does not implement impersonation yet.
                #
                asSU         = [ '/bin/su',
                                 '/usr/bin/su',
                                 '/usr/local/bin/su' ];
                fRc, sCmd    = self.locateGstBinary(oSession, oTxsSession, asSU);
                if fRc:
                    sCmdArgs = '';
                    for sArg in asArgs:
                        sCmdArgs += sArg + " ";
                    asArgs   = [ sCmd, oTestVm.getTestUser(), '-c', sCmdArgs ];
                else:
                    reporter.log('Unable to find SU on guest, falling back to regular starting ...')

            if not sCmd: # Just start it with the same privileges as TxS.
                sCmd = sVkatExe;

            reporter.log2('startVkatOnGuest: sCmd=%s' % (sCmd,));
            reporter.log2('startVkatOnGuest: asArgs=%s' % (asArgs,));

            #
            # Add own environment stuff.
            #
            asEnv = [];

            # Write the log file to some deterministic place so TxS can retrieve it later.
            sVkatLogFile = 'VKAT_RELEASE_LOG_DEST=file=' + self.getGstVkatLogFilePath(oTestVm);
            asEnv.extend([ sVkatLogFile ]);

            #
            # Execute asynchronously on the guest.
            #
            fRc = oTxsSession.asyncExec(sCmd, asArgs, asEnv, cMsTimeout = 15 * 60 * 1000, sPrefix = '[VKAT Guest] ');
            if fRc:
                self.addTask(oTxsSession);

            if not fRc:
                reporter.error('VKAT on guest returned exit code error %d' % (self.getLastRcFromTxs(oTxsSession)));
        else:
            reporter.error('VKAT on guest not found');

        return fRc;

    def runTests(self, oTestVm, oSession, oTxsSession, sDesc, sTag, asTests):
        """
        Runs one or more tests using VKAT on the host, which in turn will
        communicate with VKAT running on the guest and the Validation Kit
        audio driver ATS (Audio Testing Service).
        """
        _              = oTestVm, oSession, oTxsSession;

        sPathTemp      = self.sScratchPath;
        sPathAudioOut  = os.path.join(sPathTemp, 'vkat-host-out-%s' % (sTag));
        sPathAudioTemp = os.path.join(sPathTemp, 'vkat-host-temp-%s' % (sTag));

        reporter.log('Host audio test temp path is \"%s\"' % (sPathAudioOut));
        reporter.log('Host audio test output path is \"%s\"' % (sPathAudioTemp));
        reporter.log('Host audio test tag is \"%s\"' % (sTag));

        sVkatExe = self.getBinTool('vkat');

        reporter.log('Using VKAT on host at: \"%s\"' % (sVkatExe));

        reporter.testStart(sDesc);

        # Build the base command line, exclude all tests by default.
        asArgs = [ sVkatExe, 'test', '--mode', 'host', '--probe-backends',
                             '--tempdir', sPathAudioTemp, '--outdir', sPathAudioOut, '-a',
                             '--tag', sTag,
                             '--no-audio-ok', # Enables running on hosts which do not have any audio hardware.
                             '--no-verify' ]; # We do the verification separately in the step below.

        for _ in range(1, reporter.getVerbosity()): # Verbosity always is initialized at 1.
            asArgs.extend([ '-v' ]);

        if self.asVkatTestArgs:
            asArgs.extend(self.asVkatTestArgs);

        # ... and extend it with wanted tests.
        asArgs.extend(asTests);

        #
        # Let VKAT on the host run synchronously.
        #
        fRc, _ = self.executeHst("VKAT Host", asArgs, iExpectedRc = 0);

        reporter.testDone();

        if fRc:
            #
            # When running the test(s) above were successful, do the verification step next.
            # This gives us a bit more fine-grained test results in the test manager.
            #
            reporter.testStart('Verifying audio data');

            sNameSetHst = '%s-host.tar.gz' % (sTag);
            sPathSetHst = os.path.join(sPathAudioOut, sNameSetHst);
            sNameSetGst = '%s-guest.tar.gz' % (sTag);
            sPathSetGst = os.path.join(sPathAudioOut, sNameSetGst);

            asArgs = [ sVkatExe, 'verify', sPathSetHst, sPathSetGst ];

            for _ in range(1, reporter.getVerbosity()): # Verbosity always is initialized at 1.
                asArgs.extend([ '-v' ]);

            if self.asVkatVerifyArgs:
                asArgs += self.asVkatVerifyArgs;

            fRc, _ = self.executeHst("VKAT Host Verify", asArgs, iExpectedRc = 0);
            if fRc:
                reporter.log("Verification audio data successful");
            else:
                #
                # Add the test sets to the test manager for later (manual) diagnosis.
                #
                reporter.addLogFile(sPathSetGst, 'misc/other', 'Guest audio test set');
                reporter.addLogFile(sPathSetHst, 'misc/other', 'Host audio test set');

                reporter.error("Verification of audio data failed");

            reporter.testDone();

        return fRc;

    def doTest(self, oTestVm, oSession, oTxsSession):
        """
        Executes the specified audio tests.
        """

        # Disable any OS-specific firewalls preventing VKAT / ATS to run.
        fRc = self.disableHstFirewall();
        fRc = self.disableGstFirewall(oTestVm, oTxsSession) and fRc;

        if not fRc:
            return False;

        reporter.log("Active tests: %s" % (self.asTests,));

        # Define a tag for the whole run.
        sTag = oTestVm.sVmName + "_" + datetime.now().strftime("%Y%m%d_%H%M%S");

        fRc = self.startVkatOnGuest(oTestVm, oSession, oTxsSession, sTag);
        if fRc:
            #
            # Execute the tests using VKAT on the guest side (in guest mode).
            #
            if "guest_tone_playback" in self.asTests:
                fRc = self.runTests(oTestVm, oSession, oTxsSession, \
                                    'Guest audio playback', sTag + "_test_playback", \
                                    asTests = [ '-i0' ]);
            if "guest_tone_recording" in self.asTests:
                fRc = fRc and self.runTests(oTestVm, oSession, oTxsSession, \
                                            'Guest audio recording', sTag + "_test_recording", \
                                            asTests = [ '-i1' ]);

            # Cancel guest VKAT execution task summoned by startVkatOnGuest().
            oTxsSession.cancelTask();

            #
            # Retrieve log files for diagnosis.
            #
            self.txsDownloadFiles(oSession, oTxsSession,
                                  [ ( self.getGstVkatLogFilePath(oTestVm),
                                      'vkat-guest-%s.log' % (oTestVm.sVmName,),),
                                  ],
                                  fIgnoreErrors = True);

        # A bit of diagnosis on error.
        ## @todo Remove this later when stuff runs stable.
        if not fRc:
            reporter.log('Kernel messages:');
            sCmdDmesg = oTestVm.pathJoin(self.getGuestSystemDir(oTestVm), 'dmesg');
            oTxsSession.syncExec(sCmdDmesg, (sCmdDmesg), fIgnoreErrors = True);
            reporter.log('Loaded kernel modules:');
            sCmdLsMod = oTestVm.pathJoin(self.getGuestSystemAdminDir(oTestVm), 'lsmod');
            oTxsSession.syncExec(sCmdLsMod, (sCmdLsMod), fIgnoreErrors = True);

        return fRc;

    def testOneVmConfig(self, oVM, oTestVm):
        """
        Runs tests using one specific VM config.
        """

        reporter.testStart("Audio Testing");

        fSkip = False;

        if  oTestVm.isWindows() \
        and oTestVm.sKind in ('WindowsNT4', 'Windows2000'): # Too old for DirectSound and WASAPI backends.
            reporter.log('Audio testing skipped, not implemented/available for that OS yet.');
            fSkip = True;

        if  not fSkip \
        and self.fpApiVer < 7.0:
            reporter.log('Audio testing not available for this branch, skipping.');
            fSkip = True;

        if fSkip:
            reporter.testDone(fSkipped = True);
            return True;

        reporter.log('Verbosity level is: %d' % (reporter.getVerbosity(),));

        sVkatExe = self.getBinTool('vkat');

        #
        # Probe the backends on the host.
        #
        reporter.testStart('VKAT Probing');
        asArgs   = [ sVkatExe, 'enum', '--probe-backends' ];
        for _ in range(1, reporter.getVerbosity()): # Verbosity always is initialized at 1.
            asArgs.extend([ '-v' ]);
        _, iRc = self.executeHst("VKAT Host Audio Probing", asArgs);
        if iRc != 0:
            # Not fatal, as VBox then should fall back to the NULL audio backend (also worth having as a test case).
            reporter.log('Warning: Backend probing on host failed, no audio available (pure server installation?)');
            # Mark the whole VM test as being skipped.
            fSkip = True;
        reporter.testDone();

        #
        # Reconfigure the VM.
        #
        fRc = True;
        oSession = self.openSession(oVM);
        if oSession is not None:

            cVerbosity = reporter.getVerbosity();
            if cVerbosity >= 2: # Explicitly set verbosity via extra-data when >= level 2.
                self.asOptExtraData.extend([ 'VBoxInternal2/Audio/Debug/Level:' + str(cVerbosity) ]);

            # Set extra data.
            for sExtraData in self.asOptExtraData:
                sKey, sValue = sExtraData.split(':');
                reporter.log('Set extradata: %s => %s' % (sKey, sValue));
                fRc = oSession.setExtraData(sKey, sValue) and fRc;
                if not fRc:
                    break;

            # Make sure that the VM's audio adapter is configured the way we need it to.
            if  fRc \
            and self.fpApiVer >= 4.0:
                enmAudioControllerType = None;
                reporter.log('Configuring audio controller type ...');
                if self.sAudioControllerType is None:
                    oOsType = oSession.getOsType();
                    enmAudioControllerType = oOsType.recommendedAudioController;
                else:
                    if self.sAudioControllerType == 'HDA':
                        enmAudioControllerType = vboxcon.AudioControllerType_HDA;
                    elif self.sAudioControllerType == 'AC97':
                        enmAudioControllerType = vboxcon.AudioControllerType_AC97;
                    elif self.sAudioControllerType == 'SB16':
                        enmAudioControllerType = vboxcon.AudioControllerType_SB16;
                    assert enmAudioControllerType is not None;

                # For now we're encforcing to test the HDA emulation only, regardless of
                # what the recommended audio controller type from above was.
                ## @todo Make other emulations work as well.
                fEncforceHDA = True;

                if fEncforceHDA:
                    enmAudioControllerType = vboxcon.AudioControllerType_HDA;
                    reporter.log('Enforcing audio controller type to HDA');

                fRc = fRc and oSession.setupAudio(enmAudioControllerType,
                                                  fEnable = True, fEnableIn = True, fEnableOut = True);

            # Save the settings.
            fRc = fRc and oSession.saveSettings();
            fRc = oSession.close() and fRc;
            oSession = None;

            self.logVmInfo(oVM);

        else:
            fRc = False;

        if fRc:
            reporter.testStart('Waiting for TXS');
            oSession, oTxsSession = self.startVmAndConnectToTxsViaTcp(oTestVm.sVmName,
                                                                      fCdWait = True,
                                                                      cMsTimeout = 3 * 60 * 1000,
                                                                      sFileCdWait = '${OS/ARCH}/vkat${EXESUFF}');
            reporter.testDone();

            if oSession is not None:

                self.prepareGuestForDebugging(oSession, oTxsSession, oTestVm);

                reporter.log('Waiting for any OS startup sounds getting played (to skip those) ...');
                time.sleep(5);

                self.addTask(oTxsSession);

                fRc = self.doTest(oTestVm, oSession, oTxsSession);

                # Cleanup.
                self.removeTask(oTxsSession);
                self.terminateVmBySession(oSession);

        # Report an error in case we forgot it.
        if not fRc:
            reporter.error('Audio testing failed!');

        reporter.testDone();
        return fRc;

    def onExit(self, iRc):
        """
        Exit handler for this test driver.
        """
        return vbox.TestDriver.onExit(self, iRc);

if __name__ == '__main__':
    sys.exit(tdAudioTest().main(sys.argv))
