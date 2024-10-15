#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdAddBasic1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Additions Basics #1.
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
import os;
import sys;
import uuid;

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from testdriver import reporter;
from testdriver import base;
from testdriver import vbox;
from testdriver import vboxcon;

# Sub-test driver imports.
sys.path.append(os.path.dirname(os.path.abspath(__file__))); # For sub-test drivers.
from tdAddGuestCtrl import SubTstDrvAddGuestCtrl;
from tdAddSharedFolders1 import SubTstDrvAddSharedFolders1;


class tdAddBasic1(vbox.TestDriver):                                         # pylint: disable=too-many-instance-attributes
    """
    Additions Basics #1.
    """
    ## @todo
    # - More of the settings stuff can be and need to be generalized!
    #

    def __init__(self):
        vbox.TestDriver.__init__(self);
        self.oTestVmSet  = self.oTestVmManager.getSmokeVmSet('nat');
        self.asTestsDef  = ['install', 'guestprops', 'stdguestprops', 'guestcontrol', 'sharedfolders'];
        self.asTests     = self.asTestsDef;
        self.asRsrcs     = None
        # The file we're going to use as a beacon to wait if the Guest Additions CD-ROM is ready.
        self.sFileCdWait = '';
        # Path pointing to the Guest Additions on the (V)ISO file.
        self.sGstPathGaPrefix = '';

        # Wether to reboot guest after Guest Additions installation.
        self.fRebootAfterInstall = True;

        self.addSubTestDriver(SubTstDrvAddGuestCtrl(self));
        self.addSubTestDriver(SubTstDrvAddSharedFolders1(self));

    #
    # Overridden methods.
    #
    def showUsage(self):
        """ Shows this driver's command line options. """
        rc = vbox.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('tdAddBasic1 Options:');
        reporter.log('  --tests        <s1[:s2[:]]>');
        reporter.log('      Default: %s  (all)' % (':'.join(self.asTestsDef)));
        reporter.log('  --quick');
        reporter.log('      Same as --virt-modes hwvirt --cpu-counts 1.');
        reporter.log('  --no-reboot-after-install');
        reporter.log('      Do not reboot guest after Guest Additions installation.');
        return rc;

    def parseOption(self, asArgs, iArg):                                  # pylint: disable=too-many-branches,too-many-statements
        if asArgs[iArg] == '--tests':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--tests" takes a colon separated list of tests');
            self.asTests = asArgs[iArg].split(':');
            for s in self.asTests:
                if s not in self.asTestsDef:
                    raise base.InvalidOption('The "--tests" value "%s" is not valid; valid values are: %s'
                                             % (s, ' '.join(self.asTestsDef),));

        elif asArgs[iArg] == '--quick':
            self.parseOption(['--virt-modes', 'hwvirt'], 0);
            self.parseOption(['--cpu-counts', '1'], 0);

        elif asArgs[iArg] == '--no-reboot-after-install':
            self.fRebootAfterInstall = False;
            reporter.log('Guest will not be rebooted after Guest Additions installation, ' +
                         'kernel modules and user services should be reloaded automatically without reboot');

        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def getResourceSet(self):
        if self.asRsrcs is None:
            self.asRsrcs = []
            for oSubTstDrv in self.aoSubTstDrvs:
                self.asRsrcs.extend(oSubTstDrv.asRsrcs)
            self.asRsrcs.extend(self.oTestVmSet.getResourceSet())
        return self.asRsrcs

    def actionConfig(self):
        if not self.importVBoxApi(): # So we can use the constant below.
            return False;

        eNic0AttachType = vboxcon.NetworkAttachmentType_NAT;
        sGaIso = self.getGuestAdditionsIso();

        # On 6.0 we merge the GAs with the ValidationKit so we can get at FsPerf.
        #
        # Note1: Not possible to do a double import as both images an '/OS2' dir.
        #        So, using same dir as with unattended VISOs for the valkit.
        #
        # Note2: We need to make sure that we don't change the location of the
        #        ValidationKit bits of the combined VISO, as this will break TXS' (TestExecService)
        #        automatic updating mechanism (uses hardcoded paths, e.g. "{CDROM}/linux/amd64/TestExecService").
        #
        ## @todo Find a solution for testing the automatic Guest Additions updates, which also looks at {CDROM}s root.
        if self.fpApiVer >= 6.0:
            sGaViso = os.path.join(self.sScratchPath, 'AdditionsAndValKit.viso');
            ## @todo encode as bash cmd line:
            sVisoContent = '--iprt-iso-maker-file-marker-bourne-sh %s ' \
                           '--import-iso \'%s\' ' \
                           '--push-iso \'%s\' ' \
                           '/vboxadditions=/ ' \
                           '--pop ' \
                          % (uuid.uuid4(), self.sVBoxValidationKitIso, sGaIso);
            reporter.log2('Using VISO combining ValKit and GAs "%s": %s' % (sVisoContent, sGaViso));
            with open(sGaViso, 'w') as oGaViso: # pylint: disable=unspecified-encoding
                oGaViso.write(sVisoContent);
            sGaIso = sGaViso;

            self.sGstPathGaPrefix = 'vboxadditions';
        else:
            self.sGstPathGaPrefix = '';


        reporter.log2('Path to Guest Additions on ISO is "%s"' % self.sGstPathGaPrefix);

        return self.oTestVmSet.actionConfig(self, eNic0AttachType = eNic0AttachType, sDvdImage = sGaIso);

    def actionExecute(self):
        return self.oTestVmSet.actionExecute(self, self.testOneCfg);


    #
    # Test execution helpers.
    #

    def testOneCfg(self, oVM, oTestVm):
        """
        Runs the specified VM thru the tests.

        Returns a success indicator on the general test execution. This is not
        the actual test result.
        """
        fRc = False;

        self.logVmInfo(oVM);

        # We skip Linux Guest Additions testing for VBox < 6.1 for now.
        fVersionIgnored = oTestVm.isLinux() and self.fpApiVer < 6.1;

        if fVersionIgnored:
            reporter.log('Skipping testing for "%s" because VBox version %s is ignored' % (oTestVm.sKind, self.fpApiVer,));
            fRc = True;
        else:
            reporter.testStart('Waiting for TXS');
            if oTestVm.isWindows():
                self.sFileCdWait = ('%s/VBoxWindowsAdditions.exe' % (self.sGstPathGaPrefix,));
            elif oTestVm.isLinux():
                self.sFileCdWait = ('%s/VBoxLinuxAdditions.run' % (self.sGstPathGaPrefix,));

            oSession, oTxsSession = self.startVmAndConnectToTxsViaTcp(oTestVm.sVmName, fCdWait = True,
                                                                      cMsCdWait = 5 * 60 * 1000,
                                                                      sFileCdWait = self.sFileCdWait);
            reporter.testDone();

            if  oSession    is not None \
            and oTxsSession is not None:
                self.addTask(oTxsSession);
                # Do the testing.
                fSkip = 'install' not in self.asTests;
                reporter.testStart('Install');
                if not fSkip:
                    fRc, oTxsSession = self.testInstallAdditions(oSession, oTxsSession, oTestVm);
                reporter.testDone(fSkip);

                if  not fSkip \
                and not fRc:
                    reporter.log('Skipping following tests as Guest Additions were not installed successfully');
                else:
                    fSkip = 'guestprops' not in self.asTests;
                    reporter.testStart('Guest Properties');
                    if not fSkip:
                        fRc = self.testGuestProperties(oSession, oTxsSession, oTestVm) and fRc;
                    reporter.testDone(fSkip);

                    fSkip = 'guestcontrol' not in self.asTests;
                    reporter.testStart('Guest Control');
                    if not fSkip:
                        fRc, oTxsSession = self.aoSubTstDrvs[0].testIt(oTestVm, oSession, oTxsSession);
                    reporter.testDone(fSkip);

                    fSkip = 'sharedfolders' not in self.asTests;
                    reporter.testStart('Shared Folders');
                    if not fSkip:
                        fRc, oTxsSession = self.aoSubTstDrvs[1].testIt(oTestVm, oSession, oTxsSession);
                    reporter.testDone(fSkip or fRc is None);

                ## @todo Save and restore test.

                ## @todo Reset tests.

                ## @todo Final test: Uninstallation.

                # Download the TxS (Test Execution Service) log. This is not fatal when not being present.
                if not fRc:
                    self.txsDownloadFiles(oSession, oTxsSession,
                                          [ (oTestVm.pathJoin(self.getGuestTempDir(oTestVm), 'vbox-txs-release.log'),
                                                              'vbox-txs-%s.log' % oTestVm.sVmName) ],
                                          fIgnoreErrors = True);

                # Cleanup.
                self.removeTask(oTxsSession);
                self.terminateVmBySession(oSession);

        return fRc;

    def testInstallAdditions(self, oSession, oTxsSession, oTestVm):
        """
        Tests installing the guest additions
        """
        if oTestVm.isWindows():
            (fRc, oTxsSession) = self.testWindowsInstallAdditions(oSession, oTxsSession, oTestVm);
        elif oTestVm.isLinux():
            (fRc, oTxsSession) = self.testLinuxInstallAdditions(oSession, oTxsSession, oTestVm);
        else:
            reporter.error('Guest Additions installation not implemented for %s yet! (%s)' %
                           (oTestVm.sKind, oTestVm.sVmName,));
            fRc = False;

        #
        # Verify installation of Guest Additions using commmon bits.
        #
        if fRc:
            #
            # Check if the additions are operational.
            #
            try:    oGuest = oSession.o.console.guest;
            except:
                reporter.errorXcpt('Getting IGuest failed.');
                return (False, oTxsSession);

            # Wait for the GAs to come up.
            reporter.testStart('IGuest::additionsRunLevel');
            fRc = self.testIGuest_additionsRunLevel(oSession, oTxsSession, oTestVm, oGuest);
            reporter.testDone();

            # Check the additionsVersion attribute. It must not be empty.
            reporter.testStart('IGuest::additionsVersion');
            fRc = self.testIGuest_additionsVersion(oGuest) and fRc;
            reporter.testDone();

            # Check Guest Additions facilities
            reporter.testStart('IGuest::getFacilityStatus');
            fRc = self.testIGuest_getFacilityStatus(oTestVm, oGuest) and fRc;
            reporter.testDone();

            # Do a bit of diagnosis on error.
            if not fRc:
                if oTestVm.isLinux():
                    reporter.log('Boot log:');
                    sCmdJournalCtl = oTestVm.pathJoin(self.getGuestSystemDir(oTestVm), 'journalctl');
                    oTxsSession.syncExec(sCmdJournalCtl, (sCmdJournalCtl, '-b'), fIgnoreErrors = True);
                    reporter.log('Loaded processes:');
                    sCmdPs = oTestVm.pathJoin(self.getGuestSystemDir(oTestVm), 'ps');
                    oTxsSession.syncExec(sCmdPs, (sCmdPs, '-a', '-u', '-x'), fIgnoreErrors = True);
                    reporter.log('Kernel messages:');
                    sCmdDmesg = oTestVm.pathJoin(self.getGuestSystemDir(oTestVm), 'dmesg');
                    oTxsSession.syncExec(sCmdDmesg, (sCmdDmesg), fIgnoreErrors = True);
                    reporter.log('Loaded modules:');
                    sCmdLsMod = oTestVm.pathJoin(self.getGuestSystemAdminDir(oTestVm), 'lsmod');
                    oTxsSession.syncExec(sCmdLsMod, (sCmdLsMod), fIgnoreErrors = True);
                elif oTestVm.isWindows() or oTestVm.isOS2():
                    sShell    = self.getGuestSystemShell(oTestVm);
                    sShellOpt = '/C' if oTestVm.isWindows() or oTestVm.isOS2() else '-c';
                    reporter.log('Loaded processes:');
                    oTxsSession.syncExec(sShell, (sShell, sShellOpt, "tasklist.exe", "/FO", "CSV"), fIgnoreErrors = True);
                    reporter.log('Listing autostart entries:');
                    oTxsSession.syncExec(sShell, (sShell, sShellOpt, "wmic.exe", "startup", "get"), fIgnoreErrors = True);
                    reporter.log('Listing autostart entries:');
                    oTxsSession.syncExec(sShell, (sShell, sShellOpt, "dir",
                                                  oTestVm.pathJoin(self.getGuestSystemDir(oTestVm), 'VBox*')),
                                         fIgnoreErrors = True);
                    reporter.log('Downloading logs ...');
                    self.txsDownloadFiles(oSession, oTxsSession,
                              [ ( self.getGuestVBoxTrayClientLogFile(oTestVm),
                                  'ga-vboxtrayclient-%s.log' % (oTestVm.sVmName,),),
                                ( "C:\\Documents and Settings\\All Users\\Application Data\\Microsoft\\Dr Watson\\drwtsn32.log",
                                  'ga-drwatson-%s.log' % (oTestVm.sVmName,), ),
                              ],
                                fIgnoreErrors = True);

        return (fRc, oTxsSession);

    def getGuestVBoxTrayClientLogFile(self, oTestVm):
        """ Gets the path on the guest for the (release) log file of VBoxTray / VBoxClient. """
        if oTestVm.isWindows():
            return oTestVm.pathJoin(self.getGuestTempDir(oTestVm), 'VBoxTray.log');

        return oTestVm.pathJoin(self.getGuestTempDir(oTestVm), 'VBoxClient.log');

    def setGuestEnvVar(self, oSession, oTxsSession, oTestVm, sName, sValue):
        """ Sets a system-wide environment variable on the guest. Only supports Windows guests so far. """
        _ = oSession;
        if oTestVm.sKind not in ('WindowsNT4', 'Windows2000',):
            sPathRegExe   = oTestVm.pathJoin(self.getGuestSystemDir(oTestVm), 'reg.exe');
            self.txsRunTest(oTxsSession, ('Set env var \"%s\"' % (sName,)),
                            30 * 1000, sPathRegExe,
                            (sPathRegExe, 'add',
                             '"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"', '/v',
                             sName, '/t', 'REG_EXPAND_SZ', '/d', sValue, '/f'));

    def testWindowsInstallAdditions(self, oSession, oTxsSession, oTestVm):
        """
        Installs the Windows guest additions using the test execution service.
        Since this involves rebooting the guest, we will have to create a new TXS session.
        """
        if oTestVm.sKind not in ('WindowsNT4', 'Windows2000',):
            fGuestRequiresReboot = False;
            sRegExe = oTestVm.pathJoin(self.getGuestSystemDir(oTestVm), 'reg.exe');
            fGuestRequiresReboot = self.txsRunTest(oTxsSession, 'Check if reboot is required', 30 * 1000,
                                                    sRegExe,
                                                    (sRegExe, 'query',
                                                    '"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired"'));   # pylint: disable=line-too-long
            reporter.log('Status of RebootRequired query is %s' % fGuestRequiresReboot);

        # Set system-wide env vars to enable release logging on some applications.
        self.setGuestEnvVar(oSession, oTxsSession, oTestVm, 'VBOXTRAY_RELEASE_LOG', 'all.e.l.l2.l3.f');
        self.setGuestEnvVar(oSession, oTxsSession, oTestVm, 'VBOXTRAY_RELEASE_LOG_FLAGS', 'time thread group append');
        self.setGuestEnvVar(oSession, oTxsSession, oTestVm, 'VBOXTRAY_RELEASE_LOG_DEST',
                            ('file=%s' % (self.getGuestVBoxTrayClientLogFile(oTestVm),)));

        #
        # Install the public signing key.
        #
        if oTestVm.sKind not in ('WindowsNT4', 'Windows2000', 'WindowsXP', 'Windows2003'):
            fRc = self.txsRunTest(oTxsSession, 'VBoxCertUtil.exe', 5 * 60 * 1000,
                                  '${CDROM}/%s/cert/VBoxCertUtil.exe' % (self.sGstPathGaPrefix,),
                                  ('${CDROM}/%s/cert/VBoxCertUtil.exe' % (self.sGstPathGaPrefix,),
                                   'add-trusted-publisher',
                                   '${CDROM}/%s/cert/vbox-sha1.cer'    % (self.sGstPathGaPrefix,)),
                                  fCheckSessionStatus = True);
            if not fRc:
                reporter.error('Error installing SHA1 certificate');
            else:
                fRc = self.txsRunTest(oTxsSession, 'VBoxCertUtil.exe', 5 * 60 * 1000,
                                      '${CDROM}/%s/cert/VBoxCertUtil.exe' % (self.sGstPathGaPrefix,),
                                      ('${CDROM}/%s/cert/VBoxCertUtil.exe' % (self.sGstPathGaPrefix,),
                                       'add-trusted-publisher',
                                       '${CDROM}/%s/cert/vbox-sha256.cer'  % (self.sGstPathGaPrefix,)),
                                      fCheckSessionStatus = True);
                if not fRc:
                    reporter.error('Error installing SHA256 certificate');

        #
        # Delete relevant log files.
        #
        # Note! On some guests the files in question still can be locked by the OS, so ignore
        #       deletion errors from the guest side (e.g. sharing violations) and just continue.
        #
        sWinDir = self.getGuestWinDir(oTestVm);
        aasLogFiles = [
            ( oTestVm.pathJoin(sWinDir, 'setupapi.log'), 'ga-setupapi-%s.log' % (oTestVm.sVmName,), ),
            ( oTestVm.pathJoin(sWinDir, 'setupact.log'), 'ga-setupact-%s.log' % (oTestVm.sVmName,), ),
            ( oTestVm.pathJoin(sWinDir, 'setuperr.log'), 'ga-setuperr-%s.log' % (oTestVm.sVmName,), ),
        ];

        # Apply The SetupAPI logging level so that we also get the (most verbose) setupapi.dev.log file.
        ## @todo !!! HACK ALERT !!! Add the value directly into the testing source image. Later.
        fHaveSetupApiDevLog = False;
        if oTestVm.sKind not in ('WindowsNT4', 'Windows2000',):
            sRegExe = oTestVm.pathJoin(self.getGuestSystemDir(oTestVm), 'reg.exe');
            fHaveSetupApiDevLog = self.txsRunTest(oTxsSession, 'Enabling setupapi.dev.log', 30 * 1000,
                                                  sRegExe,
                                                  (sRegExe, 'add',
                                                   '"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Setup"',
                                                   '/v', 'LogLevel', '/t', 'REG_DWORD', '/d', '0xFF'),
                                                   fCheckSessionStatus = True);

        for sGstFile, _ in aasLogFiles:
            self.txsRmFile(oSession, oTxsSession, sGstFile, 10 * 1000, fIgnoreErrors = True);

        # Enable installing the optional auto-logon modules (VBoxGINA/VBoxCredProv).
        # Also tell the installer to produce the appropriate log files.
        sExe   = '${CDROM}/%s/VBoxWindowsAdditions.exe' % (self.sGstPathGaPrefix,);
        asArgs = [ sExe, '/S', '/l', '/with_autologon' ];

        # Determine if we need to force installing the legacy timestamp CA to make testing succeed.
        # Note: Don't force installing when the Guest Additions installer should do this automatically,
        #       i.e, only force it for Windows Server 2016 and up.
        # Note! This may mess up testing if GA ISO is from before r152467 when the option was added.
        #       Just add a VBox revision check here if we're suddenly keen on testing old stuff.
        if (    self.fpApiVer >= 6.1
            and oTestVm.getNonCanonicalGuestOsType() in [ 'Windows2016', 'Windows2019', 'Windows2022', 'Windows11' ]):
            asArgs.append('/install_timestamp_ca');

        #
        # Do the actual install.
        #
        fRc = self.txsRunTest(oTxsSession, 'VBoxWindowsAdditions.exe', 5 * 60 * 1000, sExe, asArgs, fCheckSessionStatus = True);

        # Add the Windows Guest Additions installer files to the files we want to download
        # from the guest. Note: There won't be a install_ui.log because of the silent installation.
        sGuestAddsDir = 'C:\\Program Files\\Oracle\\VirtualBox Guest Additions\\';
        aasLogFiles.append((sGuestAddsDir + 'install.log',           'ga-install-%s.log' % (oTestVm.sVmName,),));
        aasLogFiles.append((sGuestAddsDir + 'install_drivers.log',   'ga-install_drivers-%s.log' % (oTestVm.sVmName,),));
        aasLogFiles.append((oTestVm.pathJoin(self.getGuestWinDir(oTestVm), 'setupapi.log'),
                            'ga-setupapi-%s.log' % (oTestVm.sVmName,),));

        # Note: setupapi.dev.log only is available since Windows 2000.
        if fHaveSetupApiDevLog:
            aasLogFiles.append((oTestVm.pathJoin(self.getGuestWinDir(oTestVm), 'setupapi.dev.log'),
                                'ga-setupapi.dev-%s.log' % (oTestVm.sVmName,),));

        #
        # Download log files.
        # Ignore errors as all files above might not be present (or in different locations)
        # on different Windows guests.
        #
        self.txsDownloadFiles(oSession, oTxsSession, aasLogFiles, fIgnoreErrors = True);

        #
        # Reboot the VM and reconnect the TXS session.
        #
        if fRc:
            reporter.testStart('Rebooting guest after Guest Additions installation');
            (fRc, oTxsSession) = self.txsRebootAndReconnectViaTcp(oSession, oTxsSession, cMsTimeout = 15 * 60 * 1000);
            if fRc:
                pass;
            else:
                reporter.testFailure('Rebooting and reconnecting to TXS service failed');
            reporter.testDone();
        else:
            reporter.error('Error installing Windows Guest Additions (installer returned with exit code <> 0)')

        return (fRc, oTxsSession);

    def getAdditionsInstallerResult(self, oTxsSession):
        """
        Extracts the Guest Additions installer exit code from a run before.
        Assumes that nothing else has been run on the same TXS session in the meantime.
        """
        iRc = 0;
        (_, sOpcode, abPayload) = oTxsSession.getLastReply();
        if sOpcode.startswith('PROC NOK '): # Extract process rc
            iRc = abPayload[0]; # ASSUMES 8-bit rc for now.
        ## @todo Parse more statuses here.
        return iRc;

    def testLinuxInstallAdditions(self, oSession, oTxsSession, oTestVm):
        #
        # The actual install.
        # Also tell the installer to produce the appropriate log files.

        # Deploy signing keys into guest if VM has Secure Boot enabled.
        if oTestVm.fSecureBoot:
            reporter.log('Deploying Secure Boot signing keys to the guest');
            fRc = self.txsMkDirPath(oSession, oTxsSession, '/var/lib/shim-signed/mok');
            if fRc:
                fRc = self.txsUploadFile(oSession, oTxsSession,
                                         self.getFullResourceName(oTestVm.sUefiMokPathPrefix) + '.der',
                                         '/var/lib/shim-signed/mok/MOK.der')
            if fRc:
                fRc = self.txsUploadFile(oSession, oTxsSession,
                                         self.getFullResourceName(oTestVm.sUefiMokPathPrefix) + '.priv',
                                         '/var/lib/shim-signed/mok/MOK.priv')
            if fRc and oTxsSession.isSuccess():
                pass
            else:
                reporter.testFailure('Unable to deploy Secure Boot signing keys to the guest');

        # Construct arguments for installer.
        asArgs = [ self.getGuestSystemShell(oTestVm), '${CDROM}/%s/VBoxLinuxAdditions.run' % self.sGstPathGaPrefix ];

        # Make sure to add "--nox11" to the makeself wrapper in order to not getting any blocking xterm window spawned.
        asArgs.extend( [ '--nox11' ] );

        # Ugly kludge to make tst-rhel5 (or any other borked test VM) work which (accidentally?) have old(er) Guest Additions
        # version pre-installed.
        #
        # This forces our installer (inside the makeself wrapper, hence the "--") to install, regardless of whether there already
        # are any (older) Guest Additions installed already.
        fForceInstallation = False;
        if oTestVm.sVmName == "tst-rhel5":
            fForceInstallation = True;

        if fForceInstallation:
            asArgs.extend( [ "--", "--force" ] );

        fRc = self.txsRunTest(oTxsSession, 'VBoxLinuxAdditions.run', 30 * 60 * 1000,
                              self.getGuestSystemShell(oTestVm), asArgs);
        if fRc and oTxsSession.isSuccess():
            reporter.log('Installation completed');
        else:
            # Guest Additions installer which requires guest reboot after installation might return
            # special exit code which can indicate that all was installed, but kernel modules were
            # not rebooted. Handle this case here.
            if self.fRebootAfterInstall:
                iRc = self.getAdditionsInstallerResult(oTxsSession);
                # Check for rc == 0 just for completeness.
                if iRc in (0, 2): # Can happen if the GA installer has detected older VBox kernel modules running
                                  # and needs a reboot.
                    reporter.log('Guest has old(er) VBox kernel modules still running; requires a reboot');
                    fRc = True;

            if not fRc:
                reporter.error('Installing Linux Additions failed (isSuccess=%s, lastReply=%s, see log file for details)'
                               % (oTxsSession.isSuccess(), oTxsSession.getLastReply()));

        #
        # Download log files.
        # Ignore errors as all files above might not be present for whatever reason.
        #
        self.txsDownloadFiles(oSession, oTxsSession,
                              [ ('/var/log/vboxadd-install.log', 'vboxadd-install-%s.log' % oTestVm.sVmName),
                                ('/var/log/vboxadd-setup.log', 'vboxadd-setup-%s.log' % oTestVm.sVmName),
                                ('/var/log/vboxadd-uninstall.log', 'vboxadd-uninstall-%s.log' % oTestVm.sVmName), ],
                              fIgnoreErrors = True);

        # Do the final reboot to get the just installed Guest Additions up and running.
        if fRc:
            if self.fRebootAfterInstall:
                reporter.testStart('Rebooting guest after Guest Additions installation');
                (fRc, oTxsSession) = self.txsRebootAndReconnectViaTcp(oSession, oTxsSession, cMsTimeout = 15 * 60 * 1000);
                if not fRc:
                    reporter.testFailure('Rebooting and reconnecting to TXS service failed');
            else:
                reporter.log('Skipping guest reboot after Guest Additions installation as requested');
                fRc = self.txsRunTest(oTxsSession, 'Check Guest Additions kernel modules status', 5 * 60 * 1000,
                                      self.getGuestSystemShell(oTestVm),
                                      (self.getGuestSystemShell(oTestVm),
                                      '/sbin/rcvboxadd', 'status-kernel'));
                if fRc and oTxsSession.isSuccess():
                    fRc = self.txsRunTest(oTxsSession, 'Check Guest Additions user services status', 5 * 60 * 1000,
                                          self.getGuestSystemShell(oTestVm),
                                          (self.getGuestSystemShell(oTestVm),
                                          '/sbin/rcvboxadd', 'status-user'));
                    if fRc and oTxsSession.isSuccess():
                        pass;
                    else:
                        fRc = False;
                        reporter.testFailure('User services were not reloaded');
                else:
                    fRc = False;
                    reporter.testFailure('Kernel modules were not reloaded');
            if fRc:
                reporter.testDone();

        return (fRc, oTxsSession);

    def testIGuest_additionsRunLevel(self, oSession, oTxsSession, oTestVm, oGuest):
        """
        Do run level tests.

        Returns success status.
        """

        _ = oGuest;

        if oTestVm.isWindows():
            if oTestVm.isLoggedOntoDesktop():
                eExpectedRunLevel = vboxcon.AdditionsRunLevelType_Desktop;
            else:
                eExpectedRunLevel = vboxcon.AdditionsRunLevelType_Userland;
        else:
            ## @todo VBoxClient does not have facility statuses implemented yet.
            eExpectedRunLevel = vboxcon.AdditionsRunLevelType_Userland;

        # Give the guest some time to build Guest Additions on system boot if needed.
        fRc = self.waitForGAs(oSession, cMsTimeout = 15 * 60 * 1000, aenmWaitForRunLevels = [ eExpectedRunLevel ]);

        # Try to detect the display server running on the guest OS.
        # This might fail on pure server guest OSes (no X, no Wayland).
        if  fRc \
        and oTestVm.isLinux():
            ## @todo Fudge factor -- Wait for the desktop env to come up.
            #        Remove once facility statuses are implemented within VBoxClient.
            reporter.log('Waiting 30s for the desktop environment to come up before checking for the display server ...');
            self.sleep(30);
            if self.fpApiVer >= 7.1 and self.uRevision >= 157189:
                sVBoxClient = oTestVm.pathJoin(self.getGuestSystemDir(oTestVm, '/usr'), 'VBoxClient');
                fRc = fRc and self.txsRunTest(oTxsSession, 'Check display server detection', 5 * 60 * 1000,
                                              sVBoxClient, (sVBoxClient, '-v', '-v', '--session-detect'));
        return fRc;

    def testIGuest_additionsVersion(self, oGuest):
        """
        Returns False if no version string could be obtained, otherwise True
        even though errors are logged.
        """
        try:
            sVer = oGuest.additionsVersion;
        except:
            reporter.errorXcpt('Getting the additions version failed.');
            return False;
        reporter.log('IGuest::additionsVersion="%s"' % (sVer,));

        if sVer.strip() == '':
            reporter.error('IGuest::additionsVersion is empty.');
            return False;

        if sVer != sVer.strip():
            reporter.error('IGuest::additionsVersion is contains spaces: "%s".' % (sVer,));

        asBits = sVer.split('.');
        if len(asBits) < 3:
            reporter.error('IGuest::additionsVersion does not contain at least tree dot separated fields: "%s" (%d).'
                           % (sVer, len(asBits)));

        ## @todo verify the format.
        return True;

    def checkFacilityStatus(self, oGuest, eFacilityType, sDesc, fMustSucceed = True):
        """
        Prints the current status of a Guest Additions facility.

        Return success status.
        """

        fRc = True;

        try:
            eStatus, tsLastUpdatedMs = oGuest.getFacilityStatus(eFacilityType);
        except:
            if fMustSucceed:
                reporter.errorXcpt('Getting facility status for "%s" failed' % (sDesc,));
                fRc = False;
        else:
            if eStatus == vboxcon.AdditionsFacilityStatus_Inactive:
                sStatus = "INACTIVE";
            elif eStatus == vboxcon.AdditionsFacilityStatus_Paused:
                sStatus = "PAUSED";
            elif eStatus == vboxcon.AdditionsFacilityStatus_PreInit:
                sStatus = "PREINIT";
            elif eStatus == vboxcon.AdditionsFacilityStatus_Init:
                sStatus = "INIT";
            elif eStatus == vboxcon.AdditionsFacilityStatus_Active:
                sStatus = "ACTIVE";
            elif eStatus == vboxcon.AdditionsFacilityStatus_Terminating:
                sStatus = "TERMINATING";
                fRc = not fMustSucceed;
            elif eStatus == vboxcon.AdditionsFacilityStatus_Terminated:
                sStatus = "TERMINATED";
                fRc = not fMustSucceed;
            elif eStatus == vboxcon.AdditionsFacilityStatus_Failed:
                sStatus = "FAILED";
                fRc = not fMustSucceed;
            elif eStatus == vboxcon.AdditionsFacilityStatus_Unknown:
                sStatus = "UNKNOWN";
                fRc = not fMustSucceed;
            else:
                sStatus = "???";
                fRc = not fMustSucceed;

        reporter.log('Guest Additions facility "%s": %s (last updated: %sms)' % (sDesc, sStatus, str(tsLastUpdatedMs)));
        if      fMustSucceed \
        and not fRc:
            reporter.error('Guest Additions facility "%s" did not report expected status (is "%s")' % (sDesc, sStatus));

        return fRc;

    def testIGuest_getFacilityStatus(self, oTestVm, oGuest):
        """
        Checks Guest Additions facilities for their status.

        Returns success status.
        """

        reporter.testStart('Status VBoxGuest Driver');
        fRc = self.checkFacilityStatus(oGuest, vboxcon.AdditionsFacilityType_VBoxGuestDriver, "VBoxGuest Driver");
        reporter.testDone();

        reporter.testStart('Status VBoxService');
        fRc = self.checkFacilityStatus(oGuest, vboxcon.AdditionsFacilityType_VBoxService,     "VBoxService") and fRc;
        reporter.testDone();

        if oTestVm.isWindows():
            if oTestVm.isLoggedOntoDesktop():
                ## @todo VBoxClient does not have facility statuses implemented yet.
                reporter.testStart('Status VBoxTray / VBoxClient');
                fRc = self.checkFacilityStatus(oGuest, vboxcon.AdditionsFacilityType_VBoxTrayClient,
                                               "VBoxTray / VBoxClient") and fRc;
                reporter.testDone();
        ## @todo Add more.

        return fRc;

    def testGuestProperties(self, oSession, oTxsSession, oTestVm):
        """
        Test guest properties.
        """
        _ = oSession; _ = oTxsSession; _ = oTestVm;
        return True;

if __name__ == '__main__':
    sys.exit(tdAddBasic1().main(sys.argv));
