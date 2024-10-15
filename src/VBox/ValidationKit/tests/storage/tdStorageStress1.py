#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Storage testcase using xfstests.
"""

__copyright__ = \
"""
Copyright (C) 2012-2024 Oracle and/or its affiliates.

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
__version__ = "$Id: tdStorageStress1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $"


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
from testdriver import vboxcon;


class tdStorageStress(vbox.TestDriver):                                      # pylint: disable=too-many-instance-attributes
    """
    Storage testcase.
    """

    def __init__(self):
        vbox.TestDriver.__init__(self);
        self.asRsrcs           = None;
        self.oGuestToGuestVM   = None;
        self.oGuestToGuestSess = None;
        self.oGuestToGuestTxs  = None;
        self.asTestVMsDef      = ['tst-debian'];
        self.asTestVMs         = self.asTestVMsDef;
        self.asSkipVMs         = [];
        self.asVirtModesDef    = ['hwvirt', 'hwvirt-np', 'raw',]
        self.asVirtModes       = self.asVirtModesDef
        self.acCpusDef         = [1, 2,]
        self.acCpus            = self.acCpusDef;
        self.asStorageCtrlsDef = ['AHCI', 'IDE', 'LsiLogicSAS', 'LsiLogic', 'BusLogic'];
        self.asStorageCtrls    = self.asStorageCtrlsDef;
        self.asDiskFormatsDef  = ['VDI', 'VMDK', 'VHD', 'QED', 'Parallels', 'QCOW'];
        self.asDiskFormats     = self.asDiskFormatsDef;
        self.asTestsDef        = ['xfstests'];
        self.asTests           = self.asTestsDef;
        self.asGuestFs         = ['xfs', 'ext4', 'btrfs'];
        self.asGuestFsDef      = self.asGuestFs;
        self.asIscsiTargetsDef = ['aurora|iqn.2011-03.home.aurora:aurora.storagebench|1'];
        self.asIscsiTargets    = self.asIscsiTargetsDef;
        self.asDirsDef         = ['/run/media/alexander/OWCSSD/alexander', \
                                  '/run/media/alexander/CrucialSSD/alexander', \
                                  '/run/media/alexander/HardDisk/alexander', \
                                  '/home/alexander'];
        self.asDirs            = self.asDirsDef;

    #
    # Overridden methods.
    #
    def showUsage(self):
        rc = vbox.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('tdStorageBenchmark1 Options:');
        reporter.log('  --virt-modes    <m1[:m2[:]]');
        reporter.log('      Default: %s' % (':'.join(self.asVirtModesDef)));
        reporter.log('  --cpu-counts    <c1[:c2[:]]');
        reporter.log('      Default: %s' % (':'.join(str(c) for c in self.acCpusDef)));
        reporter.log('  --storage-ctrls <type1[:type2[:...]]>');
        reporter.log('      Default: %s' % (':'.join(self.asStorageCtrls)));
        reporter.log('  --disk-formats  <type1[:type2[:...]]>');
        reporter.log('      Default: %s' % (':'.join(self.asDiskFormats)));
        reporter.log('  --disk-dirs     <path1[:path2[:...]]>');
        reporter.log('      Default: %s' % (':'.join(self.asDirs)));
        reporter.log('  --iscsi-targets     <target1[:target2[:...]]>');
        reporter.log('      Default: %s' % (':'.join(self.asIscsiTargets)));
        reporter.log('  --tests         <test1[:test2[:...]]>');
        reporter.log('      Default: %s' % (':'.join(self.asTests)));
        reporter.log('  --guest-fs      <fs1[:fs2[:...]]>');
        reporter.log('      Default: %s' % (':'.join(self.asGuestFs)));
        reporter.log('  --test-vms      <vm1[:vm2[:...]]>');
        reporter.log('      Test the specified VMs in the given order. Use this to change');
        reporter.log('      the execution order or limit the choice of VMs');
        reporter.log('      Default: %s  (all)' % (':'.join(self.asTestVMsDef)));
        reporter.log('  --skip-vms      <vm1[:vm2[:...]]>');
        reporter.log('      Skip the specified VMs when testing.');
        return rc;

    def parseOption(self, asArgs, iArg):                                        # pylint: disable=too-many-branches,too-many-statements
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
        elif asArgs[iArg] == '--storage-ctrls':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--storage-ctrls" takes a colon separated list of Storage controller types');
            self.asStorageCtrls = asArgs[iArg].split(':');
        elif asArgs[iArg] == '--disk-formats':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--disk-formats" takes a colon separated list of disk formats');
            self.asDiskFormats = asArgs[iArg].split(':');
        elif asArgs[iArg] == '--disk-dirs':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--disk-dirs" takes a colon separated list of directories');
            self.asDirs = asArgs[iArg].split(':');
        elif asArgs[iArg] == '--iscsi-targets':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--iscsi-targets" takes a colon separated list of iscsi targets');
            self.asIscsiTargets = asArgs[iArg].split(':');
        elif asArgs[iArg] == '--tests':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--tests" takes a colon separated list of disk formats');
            self.asTests = asArgs[iArg].split(':');
        elif asArgs[iArg] == '--guest-fs':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--guest-fs" takes a colon separated list of filesystem identifiers');
            self.asGuestFs = asArgs[iArg].split(':');
        elif asArgs[iArg] == '--test-vms':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--test-vms" takes colon separated list');
            self.asTestVMs = asArgs[iArg].split(':');
            for s in self.asTestVMs:
                if s not in self.asTestVMsDef:
                    raise base.InvalidOption('The "--test-vms" value "%s" is not valid; valid values are: %s' \
                        % (s, ' '.join(self.asTestVMsDef)));
        elif asArgs[iArg] == '--skip-vms':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--skip-vms" takes colon separated list');
            self.asSkipVMs = asArgs[iArg].split(':');
            for s in self.asSkipVMs:
                if s not in self.asTestVMsDef:
                    reporter.log('warning: The "--test-vms" value "%s" does not specify any of our test VMs.' % (s));
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def completeOptions(self):
        # Remove skipped VMs from the test list.
        for sVM in self.asSkipVMs:
            try:    self.asTestVMs.remove(sVM);
            except: pass;

        return vbox.TestDriver.completeOptions(self);

    def getResourceSet(self):
        # Construct the resource list the first time it's queried.
        if self.asRsrcs is None:
            self.asRsrcs = [];
            if 'tst-debian' in self.asTestVMs:
                self.asRsrcs.append('4.2/storage/debian.vdi');

        return self.asRsrcs;

    def actionConfig(self):
        # Some stupid trickery to guess the location of the iso. ## fixme - testsuite unzip ++
        sVBoxValidationKit_iso = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                                              '../../VBoxValidationKitStorIo.iso'));
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                                                  '../../VBoxValidationKitStorIo.iso'));
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = '/mnt/ramdisk/vbox/svn/trunk/validationkit/VBoxValidationKitStorIo.iso';
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = '/mnt/ramdisk/vbox/svn/trunk/testsuite/VBoxTestSuiteStorIo.iso';
        if not os.path.isfile(sVBoxValidationKit_iso):
            sCur = os.getcwd();
            for i in range(0, 10):
                sVBoxValidationKit_iso = os.path.join(sCur, 'validationkit/VBoxValidationKitStorIo.iso');
                if os.path.isfile(sVBoxValidationKit_iso):
                    break;
                sVBoxValidationKit_iso = os.path.join(sCur, 'testsuite/VBoxTestSuiteStorIo.iso');
                if os.path.isfile(sVBoxValidationKit_iso):
                    break;
                sCur = os.path.abspath(os.path.join(sCur, '..'));
                if i is None: pass; # shut up pychecker/pylint.
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = '/mnt/VirtualBox/VBoxValidationKitStorIo.iso';
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = '/mnt/VirtualBox/VBoxTestSuiteStorIo.iso';



        # Make sure vboxapi has been imported so we can use the constants.
        if not self.importVBoxApi():
            return False;

        #
        # Configure the VMs we're going to use.
        #

        # Linux VMs
        if 'tst-debian' in self.asTestVMs:
            oVM = self.createTestVM('tst-debian', 1, '4.2/storage/debian.vdi', sKind = 'Debian_64', fIoApic = True, \
                                    eNic0AttachType = vboxcon.NetworkAttachmentType_NAT, \
                                    eNic0Type = vboxcon.NetworkAdapterType_Am79C973, \
                                    sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        return True;

    def actionExecute(self):
        """
        Execute the testcase.
        """
        fRc = self.test1();
        return fRc;


    #
    # Test execution helpers.
    #

    def test1RunTestProgs(self, oSession, oTxsSession, fRc, sTestName, sGuestFs):
        """
        Runs all the test programs on the test machine.
        """
        _ = oSession;

        reporter.testStart(sTestName);

        sMkfsCmd = 'mkfs.' + sGuestFs;

        # Prepare test disks, just create filesystem without partition
        reporter.testStart('Preparation');
        fRc = fRc and self.txsRunTest(oTxsSession, 'Create FS 1', 60000, \
                                      '/sbin/' + sMkfsCmd,
                                      (sMkfsCmd, '/dev/sdb'));

        fRc = fRc and self.txsRunTest(oTxsSession, 'Create FS 2', 60000, \
                                      '/sbin/' + sMkfsCmd,
                                      (sMkfsCmd, '/dev/sdc'));

        # Create test and scratch directory
        fRc = fRc and self.txsRunTest(oTxsSession, 'Create /mnt/test', 10000, \
                                      '/bin/mkdir',
                                      ('mkdir', '/mnt/test'));

        fRc = fRc and self.txsRunTest(oTxsSession, 'Create /mnt/scratch', 10000, \
                                      '/bin/mkdir',
                                      ('mkdir', '/mnt/scratch'));

        # Mount test and scratch directory.
        fRc = fRc and self.txsRunTest(oTxsSession, 'Mount /mnt/test', 10000, \
                                      '/bin/mount',
                                      ('mount', '/dev/sdb','/mnt/test'));

        fRc = fRc and self.txsRunTest(oTxsSession, 'Mount /mnt/scratch', 10000, \
                                      '/bin/mount',
                                      ('mount', '/dev/sdc','/mnt/scratch'));

        fRc = fRc and self.txsRunTest(oTxsSession, 'Copying xfstests', 10000, \
                                      '/bin/cp',
                                      ('cp', '-r','${CDROM}/${OS.ARCH}/xfstests', '/tmp'));

        reporter.testDone();

        # Run xfstests (this sh + cd crap is required because the cwd for the script must be in the root
        # of the xfstests directory...)
        reporter.testStart('xfstests');
        if fRc and 'xfstests' in self.asTests:
            fRc = self.txsRunTest(oTxsSession, 'xfstests', 3600000,
                                  '/bin/sh',
                                  ('sh', '-c', '(cd /tmp/xfstests && ./check -g auto)'),
                                  ('TEST_DIR=/mnt/test', 'TEST_DEV=/dev/sdb', 'SCRATCH_MNT=/mnt/scratch', 'SCRATCH_DEV=/dev/sdc',
                                   'FSTYP=' + sGuestFs));
            reporter.testDone();
        else:
            reporter.testDone(fSkipped = True);

        reporter.testDone(not fRc);
        return fRc;

    # pylint: disable=too-many-arguments

    def test1OneCfg(self, sVmName, eStorageController, sDiskFormat, sDiskPath1, sDiskPath2, \
                    sGuestFs, cCpus, fHwVirt, fNestedPaging):
        """
        Runs the specified VM thru test #1.

        Returns a success indicator on the general test execution. This is not
        the actual test result.
        """
        oVM = self.getVmByName(sVmName);

        # Reconfigure the VM
        fRc = True;
        oSession = self.openSession(oVM);
        if oSession is not None:
            # Attach HD
            fRc = oSession.ensureControllerAttached(self.controllerTypeToName(eStorageController));
            fRc = fRc and oSession.setStorageControllerType(eStorageController, self.controllerTypeToName(eStorageController));

            if sDiskFormat == "iSCSI":
                listNames = [];
                listValues = [];
                listValues = sDiskPath1.split('|');
                listNames.append('TargetAddress');
                listNames.append('TargetName');
                listNames.append('LUN');

                if self.fpApiVer >= 5.0:
                    oHd = oSession.oVBox.createMedium(sDiskFormat, sDiskPath1, vboxcon.AccessMode_ReadWrite, \
                                                      vboxcon.DeviceType_HardDisk);
                else:
                    oHd = oSession.oVBox.createHardDisk(sDiskFormat, sDiskPath1);
                oHd.type = vboxcon.MediumType_Normal;
                oHd.setProperties(listNames, listValues);

                # Attach it.
                if fRc is True:
                    try:
                        if oSession.fpApiVer >= 4.0:
                            oSession.o.machine.attachDevice(self.controllerTypeToName(eStorageController),
                                                            1, 0, vboxcon.DeviceType_HardDisk, oHd);
                        else:
                            oSession.o.machine.attachDevice(self.controllerTypeToName(eStorageController),
                                                            1, 0, vboxcon.DeviceType_HardDisk, oHd.id);
                    except:
                        reporter.errorXcpt('attachDevice("%s",%s,%s,HardDisk,"%s") failed on "%s"' \
                                           % (self.controllerTypeToName(eStorageController), 1, 0, oHd.id, oSession.sName) );
                        fRc = False;
                    else:
                        reporter.log('attached "%s" to %s' % (sDiskPath1, oSession.sName));
            else:
                fRc = fRc and oSession.createAndAttachHd(sDiskPath1, sDiskFormat, self.controllerTypeToName(eStorageController),
                                                         cb = 10*1024*1024*1024, iPort = 1, fImmutable = False);
                fRc = fRc and oSession.createAndAttachHd(sDiskPath2, sDiskFormat, self.controllerTypeToName(eStorageController),
                                                         cb = 10*1024*1024*1024, iPort = 2, fImmutable = False);
            fRc = fRc and oSession.enableVirtExX86(fHwVirt);
            fRc = fRc and oSession.enableNestedPagingX86(fNestedPaging);
            fRc = fRc and oSession.setCpuCount(cCpus);
            fRc = fRc and oSession.saveSettings();
            fRc = oSession.close() and fRc and True; # pychecker hack.
            oSession = None;
        else:
            fRc = False;

        # Start up.
        if fRc is True:
            self.logVmInfo(oVM);
            oSession, oTxsSession = self.startVmAndConnectToTxsViaTcp(sVmName, fCdWait = False, fNatForwardingForTxs = True);
            if oSession is not None:
                self.addTask(oTxsSession);

                # Fudge factor - Allow the guest to finish starting up.
                self.sleep(5);

                fRc = self.test1RunTestProgs(oSession, oTxsSession, fRc, 'stress testing', sGuestFs);

                # cleanup.
                self.removeTask(oTxsSession);
                self.terminateVmBySession(oSession)

                # Remove disk
                oSession = self.openSession(oVM);
                if oSession is not None:
                    try:
                        oSession.o.machine.detachDevice(self.controllerTypeToName(eStorageController), 1, 0);
                        oSession.o.machine.detachDevice(self.controllerTypeToName(eStorageController), 2, 0);

                        # Remove storage controller if it is not an IDE controller.
                        if eStorageController not in (vboxcon.StorageControllerType_PIIX3, vboxcon.StorageControllerType_PIIX4,):
                            oSession.o.machine.removeStorageController(self.controllerTypeToName(eStorageController));

                        oSession.saveSettings();
                        oSession.oVBox.deleteHdByLocation(sDiskPath1);
                        oSession.oVBox.deleteHdByLocation(sDiskPath2);
                        oSession.saveSettings();
                        oSession.close();
                        oSession = None;
                    except:
                        reporter.errorXcpt('failed to detach/delete disks %s and %s from storage controller' % \
                                           (sDiskPath1, sDiskPath2));
                else:
                    fRc = False;
            else:
                fRc = False;
        return fRc;

    def test1OneVM(self, sVmName):
        """
        Runs one VM thru the various configurations.
        """
        reporter.testStart(sVmName);
        fRc = True;
        for sStorageCtrl in self.asStorageCtrls:
            reporter.testStart(sStorageCtrl);

            if sStorageCtrl == 'AHCI':
                eStorageCtrl = vboxcon.StorageControllerType_IntelAhci;
            elif sStorageCtrl == 'IDE':
                eStorageCtrl = vboxcon.StorageControllerType_PIIX4;
            elif sStorageCtrl == 'LsiLogicSAS':
                eStorageCtrl = vboxcon.StorageControllerType_LsiLogicSas;
            elif sStorageCtrl == 'LsiLogic':
                eStorageCtrl = vboxcon.StorageControllerType_LsiLogic;
            elif sStorageCtrl == 'BusLogic':
                eStorageCtrl = vboxcon.StorageControllerType_BusLogic;
            else:
                eStorageCtrl = None;

            for sDiskFormat in self.asDiskFormats:
                reporter.testStart('%s' % (sDiskFormat,));

                asPaths = self.asDirs;

                for sDir in asPaths:
                    reporter.testStart('%s' % (sDir,));

                    sPathDisk1 = sDir + "/disk1.disk";
                    sPathDisk2 = sDir + "/disk2.disk";

                    for sGuestFs in self.asGuestFs:
                        reporter.testStart('%s' % (sGuestFs,));

                        for cCpus in self.acCpus:
                            if cCpus == 1:  reporter.testStart('1 cpu');
                            else:           reporter.testStart('%u cpus' % (cCpus,));

                            for sVirtMode in self.asVirtModes:
                                if sVirtMode == 'raw' and cCpus > 1:
                                    continue;
                                hsVirtModeDesc = {};
                                hsVirtModeDesc['raw']       = 'Raw-mode';
                                hsVirtModeDesc['hwvirt']    = 'HwVirt';
                                hsVirtModeDesc['hwvirt-np'] = 'NestedPaging';
                                reporter.testStart(hsVirtModeDesc[sVirtMode]);

                                fHwVirt       = sVirtMode != 'raw';
                                fNestedPaging = sVirtMode == 'hwvirt-np';
                                fRc = self.test1OneCfg(sVmName, eStorageCtrl, sDiskFormat, sPathDisk1, sPathDisk2, \
                                                       sGuestFs, cCpus, fHwVirt, fNestedPaging) and  fRc and True;
                                reporter.testDone();
                            reporter.testDone();
                        reporter.testDone();
                    reporter.testDone();
                reporter.testDone();
            reporter.testDone();
        reporter.testDone();
        return fRc;

    def test1(self):
        """
        Executes test #1.
        """

        # Loop thru the test VMs.
        for sVM in self.asTestVMs:
            # run test on the VM.
            if not self.test1OneVM(sVM):
                fRc = False;
            else:
                fRc = True;

        return fRc;



if __name__ == '__main__':
    sys.exit(tdStorageStress().main(sys.argv));

