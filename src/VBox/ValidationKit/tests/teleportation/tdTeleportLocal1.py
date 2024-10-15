#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdTeleportLocal1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Local teleportation testdriver.
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

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from testdriver import reporter;
from testdriver import base;
from testdriver import vbox;


class tdTeleportLocal1(vbox.TestDriver):
    """
    Local Teleportation Test #1.
    """

    def __init__(self):
        vbox.TestDriver.__init__(self);
        self.asRsrcs            = None;

        self.asTestsDef         = ['test1', 'test2'];
        self.asTests            = ['test1', 'test2'];
        self.asSkipTests        = [];
        self.asTestVMsDef       = ['tst-rhel5', 'tst-win2k3ent', 'tst-sol10'];
        self.asTestVMs          = self.asTestVMsDef;
        self.asSkipVMs          = [];
        self.asVirtModesDef     = ['hwvirt', 'hwvirt-np', 'raw',]
        self.asVirtModes        = self.asVirtModesDef
        self.acCpusDef          = [1, 2,]
        self.acCpus             = self.acCpusDef;

    #
    # Overridden methods.
    #
    def showUsage(self):
        rc = vbox.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('tdTeleportLocal1 Options:');
        reporter.log('  --virt-modes   <m1[:m2[:]]');
        reporter.log('      Default: %s' % (':'.join(self.asVirtModesDef)));
        reporter.log('  --cpu-counts   <c1[:c2[:]]');
        reporter.log('      Default: %s' % (':'.join(str(c) for c in self.acCpusDef)));
        reporter.log('  --test-vms     <vm1[:vm2[:...]]>');
        reporter.log('      Test the specified VMs in the given order. Use this to change');
        reporter.log('      the execution order or limit the choice of VMs');
        reporter.log('      Default: %s  (all)' % (':'.join(self.asTestVMsDef)));
        reporter.log('  --skip-vms     <vm1[:vm2[:...]]>');
        reporter.log('      Skip the specified VMs when testing.');
        reporter.log('  --tests        <test1[:test2[:...]]>');
        reporter.log('      Run the specified tests.');
        reporter.log('      Default: %s  (all)' % (':'.join(self.asTestsDef)));
        reporter.log('  --skip-tests   <test1[:test2[:...]]>');
        reporter.log('      Skip the specified VMs when testing.');
        reporter.log('  --quick');
        reporter.log('      Shorthand for: --virt-modes hwvirt --cpu-counts 1');
        reporter.log('                     --test-vms tst-rhel5:tst-win2k3ent:tst-sol10');
        return rc;

    def parseOption(self, asArgs, iArg):
        if asArgs[iArg] == '--virt-modes':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--virt-modes" takes a colon separated list of modes');
            self.asVirtModes = asArgs[iArg].split(':');
            for s in self.asVirtModes:
                if s not in self.asVirtModesDef:
                    raise base.InvalidOption('The "--virt-modes" value "%s" is not valid; valid values are: %s' \
                        % (s, ' '.join(self.asVirtModesDef)));
        elif asArgs[iArg] == '--cpu-counts':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--cpu-counts" takes a colon separated list of cpu counts');
            self.acCpus = [];
            for s in asArgs[iArg].split(':'):
                try: c = int(s);
                except: raise base.InvalidOption('The "--cpu-counts" value "%s" is not an integer' % (s,));
                if c <= 0:  raise base.InvalidOption('The "--cpu-counts" value "%s" is zero or negative' % (s,));
                self.acCpus.append(c);
        elif asArgs[iArg] == '--test-vms':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--test-vms" takes colon separated list');
            if asArgs[iArg]:
                self.asTestVMs = asArgs[iArg].split(':');
                for s in self.asTestVMs:
                    if s not in self.asTestVMsDef:
                        raise base.InvalidOption('The "--test-vms" value "%s" is not valid; valid values are: %s' \
                            % (s, ' '.join(self.asTestVMsDef)));
            else:
                self.asTestVMs = [];
        elif asArgs[iArg] == '--skip-vms':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--skip-vms" takes colon separated list');
            self.asSkipVMs = asArgs[iArg].split(':');
            for s in self.asSkipVMs:
                if s not in self.asTestVMsDef:
                    reporter.log('warning: The "--skip-vms" value "%s" does not specify any of our test VMs.' % (s));
        elif asArgs[iArg] == '--tests':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--tests" takes colon separated list');
            self.asTests = asArgs[iArg].split(':');
            for s in self.asTests:
                if s not in self.asTestsDef:
                    reporter.log('warning: The "--tests" value "%s" does not specify any of our tests.' % (s));
        elif asArgs[iArg] == '--skip-tests':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--skip-tests" takes colon separated list');
            self.asSkipVMs = asArgs[iArg].split(':');
            for s in self.asSkipTests:
                if s not in self.asTestsDef:
                    reporter.log('warning: The "--skip-tests" value "%s" does not specify any of our tests.' % (s));
        elif asArgs[iArg] == '--quick':
            self.asVirtModes        = ['hwvirt',];
            self.acCpus             = [1,];
            #self.asTestVMs          = ['tst-rhel5', 'tst-win2k3ent', 'tst-sol10',];
            self.asTestVMs          = ['tst-rhel5', ];
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def completeOptions(self):
        # Remove skipped VMs from the test VM list.
        for sVM in self.asSkipVMs:
            try:    self.asTestVMs.remove(sVM);
            except: pass;

        # Remove skipped tests from the test list.
        for sTest in self.asSkipTests:
            try:    self.asTests.remove(sTest);
            except: pass;

        # If no test2, then no test VMs.
        if 'test2' not in self.asTests:
            self.asTestVMs = [];

        return vbox.TestDriver.completeOptions(self);

    def getResourceSet(self):
        # Construct the resource list the first time it's queried.
        if self.asRsrcs is None:
            self.asRsrcs = [];
            if 'tst-rhel5' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/rhel5.vdi');
            if 'tst-rhel5-64' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/rhel5-64.vdi');
            if 'tst-sles11' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/sles11.vdi');
            if 'tst-sles11-64' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/sles11-64.vdi');
            if 'tst-oel' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/oel.vdi');
            if 'tst-oel-64' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/oel-64.vdi');
            if 'tst-win2k3ent' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/win2k3ent-acpi.vdi');
            if 'tst-win2k3ent-64' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/win2k3ent-64.vdi');
            if 'tst-win2k8' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/win2k8.vdi');
            if 'tst-sol10' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/solaris10.vdi');
            if 'tst-sol11' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/solaris11.vdi');
        return self.asRsrcs;

    def actionConfig(self):
        ## @todo actionConfig() and getResourceSet() are working on the same
        # set of VMs as tdNetBenchmark1, creating common base class would be
        # a good idea.

        # Some stupid trickery to guess the location of the iso. ## fixme - testsuite unzip ++
        sVBoxValidationKit_iso = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../VBoxValidationKit.iso'));
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../VBoxTestSuite.iso'));
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = '/mnt/ramdisk/vbox/svn/trunk/validationkit/VBoxValidationKit.iso';
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = '/mnt/ramdisk/vbox/svn/trunk/testsuite/VBoxTestSuite.iso';
        if not os.path.isfile(sVBoxValidationKit_iso):
            sCur = os.getcwd();
            for i in range(0, 10):
                sVBoxValidationKit_iso = os.path.join(sCur, 'validationkit/VBoxValidationKit.iso');
                if os.path.isfile(sVBoxValidationKit_iso):
                    break;
                sVBoxValidationKit_iso = os.path.join(sCur, 'testsuite/VBoxTestSuite.iso');
                if os.path.isfile(sVBoxValidationKit_iso):
                    break;
                sCur = os.path.abspath(os.path.join(sCur, '..'));
                if i is not None: pass;     # shut up pychecker/pylint.
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = '/home/bird/validationkit/VBoxValidationKit.iso';
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = '/home/bird/testsuite/VBoxTestSuite.iso';

        # Make sure vboxapi has been imported so we can use the constants.
        if not self.importVBoxApi():
            return False;

        #
        # Configure the empty VMs we're going to use for the first tests.
        #

        if 'test1' in self.asTests:
            #oVM = self.createTestVMs('tst-empty-hwvirt', 0, sKind = 'Other', fVirtEx = True);
            #if oVM is None:
            #    return False;

            oVM = self.createTestVMs('tst-empty-raw', 2, sKind = 'Other', fVirtEx = False);
            if oVM is None:
                return False;

        #
        # Configure the VMs we're going to use for the last test.
        #

        # Linux VMs
        if 'tst-rhel5' in self.asTestVMs:
            oVM = self.createTestVMs('tst-rhel5', 1, '3.0/tcp/rhel5.vdi', sKind = 'RedHat', fIoApic = True, \
                                     sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-rhel5-64' in self.asTestVMs:
            oVM = self.createTestVMs('tst-rhel5-64', 1, '3.0/tcp/rhel5-64.vdi', sKind = 'RedHat_64', \
                                     sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-sles11' in self.asTestVMs:
            oVM = self.createTestVMs('tst-sles11', 1, '3.0/tcp/sles11.vdi', sKind = 'OpenSUSE', fIoApic = True, \
                                     sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-sles11-64' in self.asTestVMs:
            oVM = self.createTestVMs('tst-sles11-64', 1, '3.0/tcp/sles11-64.vdi', sKind = 'OpenSUSE_64', \
                                     sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-oel' in self.asTestVMs:
            oVM = self.createTestVMs('tst-oel', 1, '3.0/tcp/oel.vdi', sKind = 'Oracle', fIoApic = True, \
                                     sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-oel-64' in self.asTestVMs:
            oVM = self.createTestVMs('tst-oel-64', 1, '3.0/tcp/oel-64.vdi', sKind = 'Oracle_64', \
                                     sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        # Windows VMs
        if 'tst-win2k3ent' in self.asTestVMs:
            oVM = self.createTestVMs('tst-win2k3ent', 1, '3.0/tcp/win2k3ent-acpi.vdi', sKind = 'Windows2003', \
                                     sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-win2k3ent-64' in self.asTestVMs:
            oVM = self.createTestVMs('tst-win2k3ent-64', 1, '3.0/tcp/win2k3ent-64.vdi', sKind = 'Windows2003_64', \
                                     sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-win2k8' in self.asTestVMs:
            oVM = self.createTestVMs('tst-win2k8', 1, '3.0/tcp/win2k8.vdi', sKind = 'Windows2008_64', \
                                     sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        # Solaris VMs
        if 'tst-sol10' in self.asTestVMs:
            oVM = self.createTestVMs('tst-sol10', 1, '3.0/tcp/solaris10.vdi', sKind = 'Solaris_64', \
                                     sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-sol11' in self.asTestVMs:
            oVM = self.createTestVMs('tst-sol11', 1, '3.0/tcp/os2009-11.vdi', sKind = 'Solaris_64', \
                                     sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        return True;

    def actionExecute(self):
        """
        Execute the testcase.
        """
        fRc         = 'test1' not in self.asTests  or  self.test1();
        if fRc: fRc = 'test2' not in self.asTests  or  self.test2();
        return fRc;


    #
    # Test config helpers.
    #

    def createTestVMs(self, sName, iGroup, *tArgs, **dKeywordArgs):
        """
        Wrapper around vbox.createTestVM for creating two VMs, the source
        (sName-1) and target (sName-2).

        Returns the 2nd VM object on success, None on failure.
        """
        sName1 = sName + '-1';
        oVM = self.createTestVM(sName1, iGroup * 2, *tArgs, **dKeywordArgs);
        if oVM is not None:
            sName2 = sName + '-2';
            oVM = self.createTestVM(sName2, iGroup * 2 + 1, *tArgs, **dKeywordArgs);
        return oVM;


    #
    # Test execution helpers.
    #

    def test2Teleport(self, oVmSrc, oSessionSrc, oVmDst):
        """
        Attempts a teleportation.

        Returns the input parameters for the next test2Teleport call (source
        and destiation are switched around).  The input session is closed and
        removed from the task list, while the return session is in the list.
        """

        # Enable the teleporter of the VM.
        oSession = self.openSession(oVmDst);
        fRc = oSession is not None
        if fRc:
            fRc = oSession.enableTeleporter();
            fRc = fRc and oSession.saveSettings();
            fRc = oSession.close() and fRc and True; # pychecker hack.
        if fRc:
            # Start the destination VM.
            oSessionDst, oProgressDst = self.startVmEx(oVmDst, fWait = False);
            if oSessionDst is not None:
                if oProgressDst.waitForOperation(iOperation = -3) == 0:

                    # Do the teleportation.
                    try:
                        uDstPort = oVmDst.teleporterPort;
                    except:
                        reporter.logXcpt();
                        uDstPort = 6502;
                    oProgressSrc = oSessionSrc.teleport('localhost', uDstPort, 'password', 1024);
                    if oProgressSrc is not None:
                        oProgressSrc.wait();
                        if oProgressSrc.isSuccess():
                            oProgressDst.wait();
                        if oProgressSrc.isSuccess() and oProgressDst.isSuccess():

                            # Terminate the source VM.
                            self.terminateVmBySession(oSessionSrc, oProgressSrc);

                            # Return with the source and destination swapped.
                            return oVmDst, oSessionDst, oVmSrc;

                        # Failure / bail out.
                        oProgressSrc.logResult();
                oProgressDst.logResult();
                self.terminateVmBySession(oSessionDst, oProgressDst);
        return oVmSrc, oSessionSrc, oVmDst;

    def test2OneCfg(self, sVmBaseName, cCpus, fHwVirt, fNestedPaging):
        """
        Runs the specified VM thru test #1.
        """

        # Reconfigure the source VM.
        oVmSrc = self.getVmByName(sVmBaseName + '-1');
        fRc = True;
        oSession = self.openSession(oVmSrc);
        if oSession is not None:
            fRc = fRc and oSession.enableVirtExX86(fHwVirt);
            fRc = fRc and oSession.enableNestedPagingX86(fNestedPaging);
            fRc = fRc and oSession.setCpuCount(cCpus);
            fRc = fRc and oSession.setupTeleporter(False, uPort=6501, sPassword='password');
            fRc = fRc and oSession.saveSettings();
            fRc = oSession.close() and fRc and True; # pychecker hack.
            oSession = None;
        else:
            fRc = False;

        # Reconfigure the destination VM.
        oVmDst = self.getVmByName(sVmBaseName + '-2');
        oSession = self.openSession(oVmDst);
        if oSession is not None:
            fRc = fRc and oSession.enableVirtExX86(fHwVirt);
            fRc = fRc and oSession.enableNestedPagingX86(fNestedPaging);
            fRc = fRc and oSession.setCpuCount(cCpus);
            fRc = fRc and oSession.setupTeleporter(True, uPort=6502, sPassword='password');
            fRc = fRc and oSession.saveSettings();
            fRc = oSession.close() and fRc and True; # pychecker hack.
            oSession = None;
        else:
            fRc = False;

        # Simple test.
        if fRc is True:
            self.logVmInfo(oVmSrc);
            self.logVmInfo(oVmDst);

            # Start the source VM.
            oSessionSrc = self.startVm(oVmSrc);
            if oSessionSrc is not None:
                # Simple back and forth to test the ice...
                cTeleportations = 0;
                oVmSrc, oSessionSrc, oVmDst = self.test2Teleport(oVmSrc, oSessionSrc, oVmDst);
                if reporter.testErrorCount() == 0:
                    cTeleportations += 1;
                    oVmSrc, oSessionSrc, oVmDst = self.test2Teleport(oVmSrc, oSessionSrc, oVmDst);

                # Teleport back and forth for a while.
                msStart = base.timestampMilli();
                while reporter.testErrorCount() == 0:
                    cTeleportations += 1;
                    if oSessionSrc.txsTryConnectViaTcp(2500, 'localhost') is True:
                        break;
                    oVmSrc, oSessionSrc, oVmDst = self.test2Teleport(oVmSrc, oSessionSrc, oVmDst);
                    cMsElapsed = base.timestampMilli() - msStart;
                    if cMsElapsed > 5*60000:
                        reporter.testFailure('TXS did not show up after %u min of teleporting (%u)...' \
                                             % (cMsElapsed / 60000.0, cTeleportations));
                        break;

                # Clean up the source VM.
                self.terminateVmBySession(oSessionSrc)
        return None;

    def test2OneVM(self, sVmBaseName, asSupVirtModes = None, rSupCpus = range(1, 256)):
        """
        Runs one VM (a pair really) thru the various configurations.
        """
        if asSupVirtModes is None:
            asSupVirtModes = self.asVirtModes;

        reporter.testStart(sVmBaseName);
        for cCpus in self.acCpus:
            if cCpus == 1:  reporter.testStart('1 cpu');
            else:           reporter.testStart('%u cpus' % (cCpus));

            for sVirtMode in self.asVirtModes:
                if sVirtMode == 'raw' and cCpus > 1:
                    continue;
                if cCpus not in rSupCpus:
                    continue;
                if sVirtMode not in asSupVirtModes:
                    continue;
                hsVirtModeDesc = {};
                hsVirtModeDesc['raw']       = 'Raw-mode';
                hsVirtModeDesc['hwvirt']    = 'HwVirt';
                hsVirtModeDesc['hwvirt-np'] = 'NestedPaging';
                reporter.testStart(hsVirtModeDesc[sVirtMode]);

                fHwVirt       = sVirtMode != 'raw';
                fNestedPaging = sVirtMode == 'hwvirt-np';
                self.test2OneCfg(sVmBaseName, cCpus, fHwVirt, fNestedPaging);

                reporter.testDone();
            reporter.testDone();
        return reporter.testDone()[1] == 0;

    def test2(self):
        """
        Executes test #2.
        """

        # Loop thru the test VMs.
        fRc = True;
        for sVM in self.asTestVMs:
            # figure args.
            asSupVirtModes = None;
            if sVM in ('tst-sol11', 'tst-sol10'): # 64-bit only
                asSupVirtModes = ['hwvirt', 'hwvirt-np',];

            # run test on the VM.
            if not self.test2OneVM(sVM, asSupVirtModes):
                fRc = False;

        return fRc;
    #
    # Test #1
    #

    def test1ResetVmConfig(self, oVM, fTeleporterEnabled = False):
        """
        Resets the teleportation config for the specified VM.
        Returns True on success, False on failure.
        """
        oSession = self.openSession(oVM);
        if oSession is not None:
            fRc = oSession.setupTeleporter(fTeleporterEnabled, uPort=6502, sPassword='password');
            fRc = fRc and oSession.saveSettings();
            if not oSession.close(): fRc = False;
            oSession = None;
        else:
            fRc = False;
        return fRc;

    def test1Sub7(self, oVmSrc, oVmDst):
        """
        Test the password check.
        """
        reporter.testStart('Bad password');
        if    self.test1ResetVmConfig(oVmSrc, fTeleporterEnabled = False) \
          and self.test1ResetVmConfig(oVmDst, fTeleporterEnabled = True):
            # Start the target VM.
            oSessionDst, oProgressDst = self.startVmEx(oVmDst, fWait = False);
            if oSessionDst is not None:
                if oProgressDst.waitForOperation(iOperation = -3) == 0:
                    # Start the source VM.
                    oSessionSrc = self.startVm(oVmSrc);
                    if oSessionSrc is not None:
                        tsPasswords = ('password-bad', 'passwor', 'pass', 'p', '', 'Password', );
                        for sPassword in tsPasswords:
                            reporter.testStart(sPassword);
                            oProgressSrc = oSessionSrc.teleport('localhost', 6502, sPassword);
                            if oProgressSrc:
                                oProgressSrc.wait();
                                reporter.log('src: %s' % oProgressSrc.stringifyResult());
                                if oProgressSrc.isSuccess():
                                    reporter.testFailure('IConsole::teleport succeeded with bad password "%s"' % sPassword);
                                elif oProgressSrc.getErrInfoResultCode() != vbox.ComError.E_FAIL:
                                    reporter.testFailure('IConsole::teleport returns %s instead of E_FAIL' \
                                                         % (vbox.ComError.toString(oProgressSrc.getErrInfoResultCode()),));
                                elif oProgressSrc.getErrInfoText() != 'Invalid password':
                                    reporter.testFailure('IConsole::teleport returns "%s" instead of "Invalid password"' \
                                                         % (oProgressSrc.getErrInfoText(),));
                                elif oProgressDst.isCompleted():
                                    reporter.testFailure('Destination completed unexpectedly after bad password "%s"' \
                                                         % sPassword);
                            else:
                                reporter.testFailure('IConsole::teleport failed with password "%s"' % sPassword);
                            if reporter.testDone()[1] != 0:
                                break;
                        self.terminateVmBySession(oSessionSrc, oProgressSrc);
                self.terminateVmBySession(oSessionDst, oProgressDst);
        else:
            reporter.testFailure('reconfig failed');
        return reporter.testDone()[1] == 0;

    def test1Sub6(self, oVmSrc, oVmDst):
        """
        Misconfigure the target VM and check that teleportation fails with the
        same status and message on both ends.
        xTracker: #4813
        """
        reporter.testStart('Misconfiguration & error message');
        if    self.test1ResetVmConfig(oVmSrc, fTeleporterEnabled = False) \
          and self.test1ResetVmConfig(oVmDst, fTeleporterEnabled = True):
            # Give the source a bit more RAM.
            oSession = self.openSession(oVmSrc);
            if oSession is not None:
                try:    cbMB = oVmSrc.memorySize + 4;
                except: cbMB = 1; fRc = False;
                fRc = oSession.setRamSize(cbMB);
                if not oSession.saveSettings(): fRc = False;
                if not oSession.close():        fRc = False;
                oSession = None;
            else:
                fRc = False;
            if fRc:
                # Start the target VM.
                oSessionDst, oProgressDst = self.startVmEx(oVmDst, fWait = False);
                if oSessionDst is not None:
                    if oProgressDst.waitForOperation(iOperation = -3) == 0:
                        # Start the source VM.
                        oSessionSrc = self.startVm(oVmSrc);
                        if oSessionSrc is not None:
                            # Try teleport.
                            oProgressSrc = oSessionSrc.teleport('localhost', 6502, 'password');
                            if oProgressSrc:
                                oProgressSrc.wait();
                                oProgressDst.wait();

                                reporter.log('src: %s' % oProgressSrc.stringifyResult());
                                reporter.log('dst: %s' % oProgressDst.stringifyResult());

                                # Make sure it failed.
                                if oProgressSrc.isSuccess() and oProgressDst.isSuccess():
                                    reporter.testFailure('The teleporation did not fail as expected');

                                # Compare the results.
                                if oProgressSrc.getResult() != oProgressDst.getResult():
                                    reporter.testFailure('Result differs - src=%s dst=%s' \
                                                         % (vbox.ComError.toString(oProgressSrc.getResult()),\
                                                            vbox.ComError.toString(oProgressDst.getResult())));
                                elif oProgressSrc.getErrInfoResultCode() != oProgressDst.getErrInfoResultCode():
                                    reporter.testFailure('ErrorInfo::resultCode differs - src=%s dst=%s' \
                                                         % (vbox.ComError.toString(oProgressSrc.getErrInfoResultCode()),\
                                                            vbox.ComError.toString(oProgressDst.getErrInfoResultCode())));
                                elif oProgressSrc.getErrInfoText() != oProgressDst.getErrInfoText():
                                    reporter.testFailure('ErrorInfo::text differs - src="%s" dst="%s"' \
                                                         % (oProgressSrc.getErrInfoText(), oProgressDst.getErrInfoText()));

                            self.terminateVmBySession(oSessionSrc, oProgressSrc);
                    self.terminateVmBySession(oSessionDst, oProgressDst);
                    self.test1ResetVmConfig(oVmSrc, fTeleporterEnabled = False)
                    self.test1ResetVmConfig(oVmDst, fTeleporterEnabled = True);
            else:
                reporter.testFailure('reconfig #2 failed');
        else:
            reporter.testFailure('reconfig #1 failed');
        return reporter.testDone()[1] == 0;

    def test1Sub5(self, oVmSrc, oVmDst):
        """
        Test that basic teleporting works.
        xTracker: #4749
        """
        reporter.testStart('Simple teleportation');
        for cSecsX2 in range(0, 10):
            if    self.test1ResetVmConfig(oVmSrc, fTeleporterEnabled = False) \
              and self.test1ResetVmConfig(oVmDst, fTeleporterEnabled = True):
                # Start the target VM.
                oSessionDst, oProgressDst = self.startVmEx(oVmDst, fWait = False);
                if oSessionDst is not None:
                    if oProgressDst.waitForOperation(iOperation = -3) == 0:
                        # Start the source VM.
                        oSessionSrc = self.startVm(oVmSrc);
                        if oSessionSrc is not None:
                            self.sleep(cSecsX2 / 2);
                            # Try teleport.
                            oProgressSrc = oSessionSrc.teleport('localhost', 6502, 'password');
                            if oProgressSrc:
                                oProgressSrc.wait();
                                oProgressDst.wait();

                            self.terminateVmBySession(oSessionSrc, oProgressSrc);
                    self.terminateVmBySession(oSessionDst, oProgressDst);
            else:
                reporter.testFailure('reconfig failed');
        return reporter.testDone()[1] == 0;

    def test1Sub4(self, oVM):
        """
        Test that we can start and cancel a teleportation target.
        (No source VM trying to connect here.)
        xTracker: #4965
        """
        reporter.testStart('openRemoteSession cancel');
        for cSecsX2 in range(0, 10):
            if self.test1ResetVmConfig(oVM, fTeleporterEnabled = True):
                oSession, oProgress = self.startVmEx(oVM, fWait = False);
                if oSession is not None:
                    self.sleep(cSecsX2 / 2);
                    oProgress.cancel();
                    oProgress.wait();
                    self.terminateVmBySession(oSession, oProgress);
            else:
                reporter.testFailure('reconfig failed');
        return reporter.testDone()[1] == 0;

    def test1Sub3(self, oVM):
        """
        Test that starting a teleportation target VM will fail if we give it
        a bad address to bind to.
        """
        reporter.testStart('bad IMachine::teleporterAddress');

        # re-configure it with a bad bind-to address.
        fRc = False;
        oSession = self.openSession(oVM);
        if oSession is not None:
            fRc = oSession.setupTeleporter(True, uPort=6502, sAddress='no.such.hostname.should.ever.exist.duh');
            if not oSession.saveSettings(fClose=True): fRc = False;
            oSession = None;
        if fRc:
            # Try start it.
            oSession, oProgress = self.startVmEx(oVM, fWait = False);
            if oSession is not None:
                oProgress.wait();
                ## TODO: exact error code and look for the IPRT right string.
                if not oProgress.isCompleted() or oProgress.getResult() >= 0:
                    reporter.testFailure('%s' % (oProgress.stringifyResult(),));
                self.terminateVmBySession(oSession, oProgress);

            # put back the old teleporter setup.
            self.test1ResetVmConfig(oVM, fTeleporterEnabled = True);
        else:
            reporter.testFailure('reconfig #1 failed');
        return reporter.testDone()[1] == 0;

    # test1Sub2 - start

    def test1Sub2SetEnabled(self, oSession, fEnabled):
        """ This should never fail."""
        try:
            oSession.o.machine.teleporterEnabled = fEnabled;
        except:
            reporter.testFailureXcpt('machine.teleporterEnabled=%s' % (fEnabled,));
            return False;
        try:
            fNew = oSession.o.machine.teleporterEnabled;
        except:
            reporter.testFailureXcpt();
            return False;
        if fNew != fEnabled:
            reporter.testFailure('machine.teleporterEnabled=%s but afterwards it is actually %s' % (fEnabled, fNew));
            return False;
        return True;

    def test1Sub2SetPassword(self, oSession, sPassword):
        """ This should never fail."""
        try:
            oSession.o.machine.teleporterPassword = sPassword;
        except:
            reporter.testFailureXcpt('machine.teleporterPassword=%s' % (sPassword,));
            return False;
        try:
            sNew = oSession.o.machine.teleporterPassword;
        except:
            reporter.testFailureXcpt();
            return False;
        if sNew != sPassword:
            reporter.testFailure('machine.teleporterPassword="%s" but afterwards it is actually "%s"' % (sPassword, sNew));
            return False;
        return True;

    def test1Sub2SetPort(self, oSession, uPort, fInvalid = False):
        """ This can fail, thus fInvalid."""
        if not fInvalid:
            uOld = uPort;
        else:
            try:    uOld = oSession.o.machine.teleporterPort;
            except: return reporter.testFailureXcpt();

        try:
            oSession.o.machine.teleporterPort = uPort;
        except Exception as oXcpt:
            if not fInvalid or vbox.ComError.notEqual(oXcpt, vbox.ComError.E_INVALIDARG):
                return reporter.testFailureXcpt('machine.teleporterPort=%u' % (uPort,));
        else:
            if fInvalid:
                return reporter.testFailureXcpt('machine.teleporterPort=%u succeeded unexpectedly' % (uPort,));

        try:    uNew = oSession.o.machine.teleporterPort;
        except: return reporter.testFailureXcpt();
        if uNew != uOld:
            if not fInvalid:
                reporter.testFailure('machine.teleporterPort=%u but afterwards it is actually %u' % (uPort, uNew));
            else:
                reporter.testFailure('machine.teleporterPort is %u after failure, expected %u' % (uNew, uOld));
            return False;
        return True;

    def test1Sub2SetAddress(self, oSession, sAddress):
        """ This should never fail."""
        try:
            oSession.o.machine.teleporterAddress = sAddress;
        except:
            reporter.testFailureXcpt('machine.teleporterAddress=%s' % (sAddress,));
            return False;
        try:
            sNew = oSession.o.machine.teleporterAddress;
        except:
            reporter.testFailureXcpt();
            return False;
        if sNew != sAddress:
            reporter.testFailure('machine.teleporterAddress="%s" but afterwards it is actually "%s"' % (sAddress, sNew));
            return False;
        return True;

    def test1Sub2(self, oVM):
        """
        Test the attributes, making sure that we get exceptions on bad values.
        """
        reporter.testStart('IMachine::teleport*');

        # Save the original teleporter attributes for the discard test.
        try:
            sOrgAddress  = oVM.teleporterAddress;
            uOrgPort     = oVM.teleporterPort;
            sOrgPassword = oVM.teleporterPassword;
            fOrgEnabled  = oVM.teleporterEnabled;
        except:
            reporter.testFailureXcpt();
        else:
            # Open a session and start messing with the properties.
            oSession = self.openSession(oVM);
            if oSession is not None:
                # Anything goes for the address.
                reporter.testStart('teleporterAddress');
                self.test1Sub2SetAddress(oSession, '');
                self.test1Sub2SetAddress(oSession, '1');
                self.test1Sub2SetAddress(oSession, 'Anything goes! ^&$@!$%^');
                reporter.testDone();

                # The port is restricted to {0..65535}.
                reporter.testStart('teleporterPort');
                for uPort in range(0, 1000) + range(16000, 17000) + range(32000, 33000) + range(65000, 65536):
                    if not self.test1Sub2SetPort(oSession, uPort):
                        break;
                self.processPendingEvents();
                reporter.testDone();

                reporter.testStart('teleporterPort negative');
                self.test1Sub2SetPort(oSession,  65536, True);
                self.test1Sub2SetPort(oSession, 999999, True);
                reporter.testDone();

                # Anything goes for the password.
                reporter.testStart('teleporterPassword');
                self.test1Sub2SetPassword(oSession, 'password');
                self.test1Sub2SetPassword(oSession, '');
                self.test1Sub2SetPassword(oSession, '1');
                self.test1Sub2SetPassword(oSession, 'Anything goes! ^&$@!$%^');
                reporter.testDone();

                # Just test that it works.
                reporter.testStart('teleporterEnabled');
                self.test1Sub2SetEnabled(oSession, True);
                self.test1Sub2SetEnabled(oSession, True);
                self.test1Sub2SetEnabled(oSession, False);
                self.test1Sub2SetEnabled(oSession, False);
                reporter.testDone();

                # Finally, discard the changes, close the session and check
                # that we're back to the originals.
                if not oSession.discardSettings(True):
                    reporter.testFailure('Failed to discard settings & close the session')
            else:
                reporter.testFailure('Failed to open VM session')
            try:
                if oVM.teleporterAddress  != sOrgAddress:   reporter.testFailure('Rollback failed for teleporterAddress');
                if oVM.teleporterPort     != uOrgPort:      reporter.testFailure('Rollback failed for teleporterPort');
                if oVM.teleporterPassword != sOrgPassword:  reporter.testFailure('Rollback failed for teleporterPassword');
                if oVM.teleporterEnabled  != fOrgEnabled:   reporter.testFailure('Rollback failed for teleporterEnabled');
            except:
                reporter.testFailureXcpt();
        return reporter.testDone()[1] != 0;

    # test1Sub1 - start

    def test1Sub1DoTeleport(self, oSession, sHostname, uPort, sPassword, cMsMaxDowntime, hrcExpected, sTestName):
        """ Do a bad IConsole::teleport call and check the result."""
        reporter.testStart(sTestName);
        fRc = False;
        try:
            oProgress = oSession.o.console.teleport(sHostname, uPort, sPassword, cMsMaxDowntime);
        except vbox.ComException as oXcpt:
            if vbox.ComError.equal(oXcpt, hrcExpected):
                fRc = True;
            else:
                reporter.testFailure('hresult %s, expected %s' \
                                     % (vbox.ComError.toString(oXcpt.hresult),
                                        vbox.ComError.toString(hrcExpected)));
        except Exception as oXcpt:
            reporter.testFailure('Unexpected exception %s' % (oXcpt));
        else:
            reporter.testFailure('Unpexected success');
            oProgress.cancel();
            oProgress.wait();
        reporter.testDone();
        return fRc;

    def test1Sub1(self, oVM):
        """ Test simple IConsole::teleport() failure paths. """
        reporter.testStart('IConsole::teleport');
        oSession = self.startVm(oVM);
        if oSession:
            self.test1Sub1DoTeleport(oSession, 'localhost', 65536, 'password', 10000,
                                     vbox.ComError.E_INVALIDARG, 'Bad port value 65536');
            self.test1Sub1DoTeleport(oSession, 'localhost',     0, 'password', 10000,
                                     vbox.ComError.E_INVALIDARG, 'Bad port value 0');
            self.test1Sub1DoTeleport(oSession, 'localhost',  5000, 'password', 0,
                                     vbox.ComError.E_INVALIDARG, 'Bad max downtime');
            self.test1Sub1DoTeleport(oSession, '',           5000, 'password', 10000,
                                     vbox.ComError.E_INVALIDARG, 'No hostname');
            self.test1Sub1DoTeleport(oSession, 'no.such.hostname.should.ever.exist.duh', 5000, 'password', 0,
                                     vbox.ComError.E_INVALIDARG, 'Non-existing host');

            self.terminateVmBySession(oSession)
        else:
            reporter.testFailure('startVm');
        return reporter.testDone()[1] == 0;


    def test1(self):
        """
        Executes test #1 - Negative API testing.

        ASSUMES that the VMs are
        """
        reporter.testStart('Test 1');

        # Get the VMs.
        #oVmHwVirt1 = self.getVmByName('tst-empty-hwvirt-1');
        #oVmHwVirt2 = self.getVmByName('tst-empty-hwvirt-2');
        oVmRaw1    = self.getVmByName('tst-empty-raw-1');
        oVmRaw2    = self.getVmByName('tst-empty-raw-2');

        # Reset their teleportation related configuration.
        fRc = True;
        #for oVM in (oVmHwVirt1, oVmHwVirt2, oVmRaw1, oVmRaw2):
        for oVM in (oVmRaw1, oVmRaw2):
            if not self.test1ResetVmConfig(oVM): fRc = False;

        # Do the testing (don't both with fRc on the subtests).
        if fRc:
            self.test1Sub1(oVmRaw1);
            self.test1Sub2(oVmRaw2);
            self.test1Sub3(oVmRaw2);
            self.test1Sub4(oVmRaw2);
            self.processPendingEvents();
            self.test1Sub5(oVmRaw1, oVmRaw2);
            self.test1Sub6(oVmRaw1, oVmRaw2);
            self.test1Sub7(oVmRaw1, oVmRaw2);
        else:
            reporter.testFailure('Failed to reset the VM configs')
        return reporter.testDone()[1] == 0;



if __name__ == '__main__':
    sys.exit(tdTeleportLocal1().main(sys.argv));

