#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdGuestOsInstTest1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Guest OS installation tests.
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


# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0]
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(g_ksValidationKitDir)

# Validation Kit imports.
from testdriver import vbox;
from testdriver import base;
from testdriver import reporter;
from testdriver import vboxcon;
from testdriver import vboxtestvms;


class InstallTestVm(vboxtestvms.TestVm):
    """ Installation test VM. """

    ## @name The primary controller, to which the disk will be attached.
    ## @{
    ksScsiController = 'SCSI Controller'
    ksSataController = 'SATA Controller'
    ksIdeController  = 'IDE Controller'
    ## @}

    ## @name VM option flags (OR together).
    ## @{
    kf32Bit                 = 0x01;
    kf64Bit                 = 0x02;
    # most likely for ancient Linux kernels assuming that AMD processors have always an I/O-APIC
    kfReqIoApic             = 0x10;
    kfReqIoApicSmp          = 0x20;
    kfReqPae                = 0x40;
    kfIdeIrqDelay           = 0x80;
    kfUbuntuNewAmdBug       = 0x100;
    kfNoWin81Paravirt       = 0x200;
    ## @}

    ## IRQ delay extra data config for win2k VMs.
    kasIdeIrqDelay   = [ 'VBoxInternal/Devices/piix3ide/0/Config/IRQDelay:1', ];

    ## Install ISO path relative to the testrsrc root.
    ksIsoPathBase    = os.path.join('4.2', 'isos');


    def __init__(self, oSet, sVmName, sKind, sInstallIso, sHdCtrlNm, cGbHdd, fFlags):
        vboxtestvms.TestVm.__init__(self, sVmName, oSet = oSet, sKind = sKind, sHddControllerType = sHdCtrlNm,
                                    fRandomPvPMode = (fFlags & self.kfNoWin81Paravirt) == 0);
        self.sDvdImage    = os.path.join(self.ksIsoPathBase, sInstallIso);
        self.cGbHdd       = cGbHdd;
        self.fInstVmFlags = fFlags;
        if fFlags & self.kfReqPae:
            self.fPae     = True;
        if fFlags & (self.kfReqIoApic | self.kfReqIoApicSmp):
            self.fIoApic  = True;

        # Tweaks
        self.iOptRamAdjust  = 0;
        self.asExtraData    = [];
        if fFlags & self.kfIdeIrqDelay:
            self.asExtraData = self.kasIdeIrqDelay;

    def detatchAndDeleteHd(self, oTestDrv):
        """
        Detaches and deletes the HD.
        Returns success indicator, error info logged.
        """
        fRc = False;
        oVM = oTestDrv.getVmByName(self.sVmName);
        if oVM is not None:
            oSession = oTestDrv.openSession(oVM);
            if oSession is not None:
                (fRc, oHd) = oSession.detachHd(self.sHddControllerType, iPort = 0, iDevice = 0);
                if fRc is True and oHd is not None:
                    fRc = oSession.saveSettings();
                    fRc = fRc and oTestDrv.oVBox.deleteHdByMedium(oHd);
                    fRc = fRc and oSession.saveSettings(); # Necessary for media reg?
                fRc = oSession.close() and fRc;
        return fRc;

    def getReconfiguredVm(self, oTestDrv, cCpus, sVirtMode, sParavirtMode = None):
        #
        # Do the standard reconfig in the base class first, it'll figure out
        # if we can run the VM as requested.
        #
        (fRc, oVM) = vboxtestvms.TestVm.getReconfiguredVm(self, oTestDrv, cCpus, sVirtMode, sParavirtMode);

        #
        # Make sure there is no HD from the previous run attached nor taking
        # up storage on the host.
        #
        if fRc is True:
            fRc = self.detatchAndDeleteHd(oTestDrv);

        #
        # Check for ubuntu installer vs. AMD host CPU.
        #
        if fRc is True and (self.fInstVmFlags & self.kfUbuntuNewAmdBug):
            if self.isHostCpuAffectedByUbuntuNewAmdBug(oTestDrv):
                return (None, None); # (skip)

        #
        # Make adjustments to the default config, and adding a fresh HD.
        #
        if fRc is True:
            oSession = oTestDrv.openSession(oVM);
            if oSession is not None:
                if self.sHddControllerType == self.ksSataController:
                    fRc = fRc and oSession.setStorageControllerType(vboxcon.StorageControllerType_IntelAhci,
                                                                    self.sHddControllerType);
                    fRc = fRc and oSession.setStorageControllerPortCount(self.sHddControllerType, 1);
                elif self.sHddControllerType == self.ksScsiController:
                    fRc = fRc and oSession.setStorageControllerType(vboxcon.StorageControllerType_LsiLogic,
                                                                    self.sHddControllerType);
                try:
                    sHddPath = os.path.join(os.path.dirname(oVM.settingsFilePath),
                                            '%s-%s-%s.vdi' % (self.sVmName, sVirtMode, cCpus,));
                except:
                    reporter.errorXcpt();
                    sHddPath = None;
                    fRc = False;

                fRc = fRc and oSession.createAndAttachHd(sHddPath,
                                                         cb = self.cGbHdd * 1024*1024*1024,
                                                         sController = self.sHddControllerType,
                                                         iPort = 0,
                                                         fImmutable = False);

                # Set proper boot order
                fRc = fRc and oSession.setBootOrder(1, vboxcon.DeviceType_HardDisk)
                fRc = fRc and oSession.setBootOrder(2, vboxcon.DeviceType_DVD)

                # Adjust memory if requested.
                if self.iOptRamAdjust != 0:
                    fRc = fRc and oSession.setRamSize(oSession.o.machine.memorySize + self.iOptRamAdjust);

                # Set extra data
                for sExtraData in self.asExtraData:
                    try:
                        sKey, sValue = sExtraData.split(':')
                    except ValueError:
                        raise base.InvalidOption('Invalid extradata specified: %s' % sExtraData)
                    reporter.log('Set extradata: %s => %s' % (sKey, sValue))
                    fRc = fRc and oSession.setExtraData(sKey, sValue)

                # Other variations?

                # Save the settings.
                fRc = fRc and oSession.saveSettings()
                fRc = oSession.close() and fRc;
            else:
                fRc = False;
            if fRc is not True:
                oVM = None;

        # Done.
        return (fRc, oVM)





class tdGuestOsInstTest1(vbox.TestDriver):
    """
    Guest OS installation tests.

    Scenario:
        - Create new VM that corresponds specified installation ISO image.
        - Create HDD that corresponds to OS type that will be installed.
        - Boot VM from ISO image (i.e. install guest OS).
        - Wait for incomming TCP connection (guest should initiate such a
          connection in case installation has been completed successfully).
    """


    def __init__(self):
        """
        Reinitialize child class instance.
        """
        vbox.TestDriver.__init__(self)
        self.fLegacyOptions = False;
        assert self.fEnableVrdp; # in parent driver.

        #
        # Our install test VM set.
        #
        oSet = vboxtestvms.TestVmSet(self.oTestVmManager, fIgnoreSkippedVm = True);
        oSet.aoTestVms.extend([
            # pylint: disable=line-too-long
            InstallTestVm(oSet, 'tst-fedora4',      'Fedora',           'fedora4-txs.iso',          InstallTestVm.ksIdeController,   8, InstallTestVm.kf32Bit),
            InstallTestVm(oSet, 'tst-fedora5',      'Fedora',           'fedora5-txs.iso',          InstallTestVm.ksSataController,  8, InstallTestVm.kf32Bit | InstallTestVm.kfReqPae | InstallTestVm.kfReqIoApicSmp),
            InstallTestVm(oSet, 'tst-fedora6',      'Fedora',           'fedora6-txs.iso',          InstallTestVm.ksSataController,  8, InstallTestVm.kf32Bit | InstallTestVm.kfReqIoApic),
            InstallTestVm(oSet, 'tst-fedora7',      'Fedora',           'fedora7-txs.iso',          InstallTestVm.ksSataController,  8, InstallTestVm.kf32Bit | InstallTestVm.kfUbuntuNewAmdBug | InstallTestVm.kfReqIoApic),
            InstallTestVm(oSet, 'tst-fedora9',      'Fedora',           'fedora9-txs.iso',          InstallTestVm.ksSataController,  8, InstallTestVm.kf32Bit),
            InstallTestVm(oSet, 'tst-fedora18-64',  'Fedora_64',        'fedora18-x64-txs.iso',     InstallTestVm.ksSataController,  8, InstallTestVm.kf64Bit),
            InstallTestVm(oSet, 'tst-fedora18',     'Fedora',           'fedora18-txs.iso',         InstallTestVm.ksScsiController,  8, InstallTestVm.kf32Bit),
            InstallTestVm(oSet, 'tst-ols6',         'Oracle',           'ols6-i386-txs.iso',        InstallTestVm.ksSataController, 12, InstallTestVm.kf32Bit | InstallTestVm.kfReqPae),
            InstallTestVm(oSet, 'tst-ols6-64',      'Oracle_64',        'ols6-x86_64-txs.iso',      InstallTestVm.ksSataController, 12, InstallTestVm.kf64Bit),
            InstallTestVm(oSet, 'tst-rhel5',        'RedHat',           'rhel5-txs.iso',            InstallTestVm.ksSataController,  8, InstallTestVm.kf32Bit | InstallTestVm.kfReqPae | InstallTestVm.kfReqIoApic),
            InstallTestVm(oSet, 'tst-suse102',      'OpenSUSE',         'opensuse102-txs.iso',      InstallTestVm.ksIdeController,   8, InstallTestVm.kf32Bit | InstallTestVm.kfReqIoApic),
            ## @todo InstallTestVm(oSet, 'tst-ubuntu606',    'Ubuntu',           'ubuntu606-txs.iso',        InstallTestVm.ksSataController,  8, InstallTestVm.kf32Bit),
            ## @todo InstallTestVm(oSet, 'tst-ubuntu710',    'Ubuntu',           'ubuntu710-txs.iso',        InstallTestVm.ksSataController,  8, InstallTestVm.kf32Bit),
            InstallTestVm(oSet, 'tst-ubuntu804',    'Ubuntu',           'ubuntu804-txs.iso',        InstallTestVm.ksSataController,  8, InstallTestVm.kf32Bit | InstallTestVm.kfUbuntuNewAmdBug | InstallTestVm.kfReqPae | InstallTestVm.kfReqIoApic),
            InstallTestVm(oSet, 'tst-ubuntu804-64', 'Ubuntu_64',        'ubuntu804-amd64-txs.iso',  InstallTestVm.ksSataController,  8, InstallTestVm.kf64Bit),
            InstallTestVm(oSet, 'tst-ubuntu904',    'Ubuntu',           'ubuntu904-txs.iso',        InstallTestVm.ksSataController,  8, InstallTestVm.kf32Bit | InstallTestVm.kfUbuntuNewAmdBug | InstallTestVm.kfReqPae),
            InstallTestVm(oSet, 'tst-ubuntu904-64', 'Ubuntu_64',        'ubuntu904-amd64-txs.iso',  InstallTestVm.ksSataController,  8, InstallTestVm.kf64Bit),
            #InstallTestVm(oSet, 'tst-ubuntu1404',   'Ubuntu',           'ubuntu1404-txs.iso',       InstallTestVm.ksSataController,  8, InstallTestVm.kf32Bit | InstallTestVm.kfUbuntuNewAmdBug | InstallTestVm.kfReqPae), bird: Is 14.04 one of the 'older ones'?
            InstallTestVm(oSet, 'tst-ubuntu1404',   'Ubuntu',           'ubuntu1404-txs.iso',       InstallTestVm.ksSataController,  8, InstallTestVm.kf32Bit | InstallTestVm.kfReqPae),
            InstallTestVm(oSet, 'tst-ubuntu1404-64','Ubuntu_64',        'ubuntu1404-amd64-txs.iso', InstallTestVm.ksSataController,  8, InstallTestVm.kf64Bit),
            InstallTestVm(oSet, 'tst-debian7',      'Debian',           'debian-7.0.0-txs.iso',     InstallTestVm.ksSataController,  8, InstallTestVm.kf32Bit),
            InstallTestVm(oSet, 'tst-debian7-64',   'Debian_64',        'debian-7.0.0-x64-txs.iso', InstallTestVm.ksScsiController,  8, InstallTestVm.kf64Bit),
            InstallTestVm(oSet, 'tst-vista-64',     'WindowsVista_64',  'vista-x64-txs.iso',        InstallTestVm.ksSataController, 25, InstallTestVm.kf64Bit),
            InstallTestVm(oSet, 'tst-vista-32',     'WindowsVista',     'vista-x86-txs.iso',        InstallTestVm.ksSataController, 25, InstallTestVm.kf32Bit),
            InstallTestVm(oSet, 'tst-w7-64',        'Windows7_64',      'win7-x64-txs.iso',         InstallTestVm.ksSataController, 25, InstallTestVm.kf64Bit),
            InstallTestVm(oSet, 'tst-w7-32',        'Windows7',         'win7-x86-txs.iso',         InstallTestVm.ksSataController, 25, InstallTestVm.kf32Bit),
            InstallTestVm(oSet, 'tst-w2k3',         'Windows2003',      'win2k3ent-txs.iso',        InstallTestVm.ksIdeController,  25, InstallTestVm.kf32Bit),
            InstallTestVm(oSet, 'tst-w2k',          'Windows2000',      'win2ksp0-txs.iso',         InstallTestVm.ksIdeController,  25, InstallTestVm.kf32Bit | InstallTestVm.kfIdeIrqDelay),
            InstallTestVm(oSet, 'tst-w2ksp4',       'Windows2000',      'win2ksp4-txs.iso',         InstallTestVm.ksIdeController,  25, InstallTestVm.kf32Bit | InstallTestVm.kfIdeIrqDelay),
            InstallTestVm(oSet, 'tst-wxp',          'WindowsXP',        'winxppro-txs.iso',         InstallTestVm.ksIdeController,  25, InstallTestVm.kf32Bit),
            InstallTestVm(oSet, 'tst-wxpsp2',       'WindowsXP',        'winxpsp2-txs.iso',         InstallTestVm.ksIdeController,  25, InstallTestVm.kf32Bit),
            InstallTestVm(oSet, 'tst-wxp64',        'WindowsXP_64',     'winxp64-txs.iso',          InstallTestVm.ksIdeController,  25, InstallTestVm.kf64Bit),
            ## @todo disable paravirt for Windows 8.1 guests as long as it's not fixed in the code
            InstallTestVm(oSet, 'tst-w81-32',       'Windows81',        'win81-x86-txs.iso',        InstallTestVm.ksSataController, 25, InstallTestVm.kf32Bit),
            InstallTestVm(oSet, 'tst-w81-64',       'Windows81_64',     'win81-x64-txs.iso',        InstallTestVm.ksSataController, 25, InstallTestVm.kf64Bit),
            InstallTestVm(oSet, 'tst-w10-32',       'Windows10',        'win10-x86-txs.iso',        InstallTestVm.ksSataController, 25, InstallTestVm.kf32Bit | InstallTestVm.kfReqPae),
            InstallTestVm(oSet, 'tst-w10-64',       'Windows10_64',     'win10-x64-txs.iso',        InstallTestVm.ksSataController, 25, InstallTestVm.kf64Bit),
            # pylint: enable=line-too-long
        ]);
        self.oTestVmSet = oSet;



    #
    # Overridden methods.
    #

    def showUsage(self):
        """
        Extend usage info
        """
        rc = vbox.TestDriver.showUsage(self)
        reporter.log('');
        reporter.log('tdGuestOsInstTest1 options:');
        reporter.log('  --ioapic, --no-ioapic');
        reporter.log('      Enable or disable the I/O apic.');
        reporter.log('      Default: --ioapic');
        reporter.log('  --pae, --no-pae');
        reporter.log('      Enable or disable PAE support for 32-bit guests.');
        reporter.log('      Default: Guest dependent.');
        reporter.log('  --ram-adjust <MBs>')
        reporter.log('      Adjust the VM ram size by the given delta.  Both negative and positive');
        reporter.log('      values are accepted.');
        reporter.log('  --set-extradata <key>:value')
        reporter.log('      Set VM extra data. This command line option might be used multiple times.')
        reporter.log('obsolete:');
        reporter.log('  --nested-paging, --no-nested-paging');
        reporter.log('  --raw-mode');
        reporter.log('  --cpus <# CPUs>');
        reporter.log('  --install-iso <ISO file name>');

        return rc

    def parseOption(self, asArgs, iArg):
        """
        Extend standard options set
        """

        if False is True: # placeholder.    pylint: disable=comparison-of-constants
            pass;
        elif asArgs[iArg] == '--ioapic':
            for oTestVm in self.oTestVmSet.aoTestVms:
                oTestVm.fIoApic = True;
        elif asArgs[iArg] == '--no-ioapic':
            for oTestVm in self.oTestVmSet.aoTestVms:
                oTestVm.fIoApic = False;
        elif asArgs[iArg] == '--pae':
            for oTestVm in self.oTestVmSet.aoTestVms:
                oTestVm.fPae = True;
        elif asArgs[iArg] == '--no-pae':
            for oTestVm in self.oTestVmSet.aoTestVms:
                oTestVm.fPae = False;
        elif asArgs[iArg] == '--ram-adjust':
            iArg = self.requireMoreArgs(1, asArgs, iArg);
            for oTestVm in self.oTestVmSet.aoTestVms:
                oTestVm.iOptRamAdjust = int(asArgs[iArg]);
        elif asArgs[iArg] == '--set-extradata':
            iArg = self.requireMoreArgs(1, asArgs, iArg)
            for oTestVm in self.oTestVmSet.aoTestVms:
                oTestVm.asExtraData.append(asArgs[iArg]);

        # legacy, to be removed once TM is reconfigured.
        elif asArgs[iArg] == '--install-iso':
            self.legacyOptions();
            iArg = self.requireMoreArgs(1, asArgs, iArg);
            for oTestVm in self.oTestVmSet.aoTestVms:
                oTestVm.fSkip = os.path.basename(oTestVm.sDvdImage) != asArgs[iArg];
        elif asArgs[iArg] == '--cpus':
            self.legacyOptions();
            iArg = self.requireMoreArgs(1, asArgs, iArg);
            self.oTestVmSet.acCpus = [ int(asArgs[iArg]), ];
        elif asArgs[iArg] == '--raw-mode':
            self.legacyOptions();
            self.oTestVmSet.asVirtModes = [ 'raw', ];
        elif asArgs[iArg] == '--nested-paging':
            self.legacyOptions();
            self.oTestVmSet.asVirtModes = [ 'hwvirt-np', ];
        elif asArgs[iArg] == '--no-nested-paging':
            self.legacyOptions();
            self.oTestVmSet.asVirtModes = [ 'hwvirt', ];
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg)

        return iArg + 1

    def legacyOptions(self):
        """ Enables legacy option mode. """
        if not self.fLegacyOptions:
            self.fLegacyOptions = True;
            self.oTestVmSet.asVirtModes = [ 'hwvirt', ];
            self.oTestVmSet.acCpus      = [ 1, ];
        return True;

    def actionConfig(self):
        if not self.importVBoxApi(): # So we can use the constant below.
            return False;
        return self.oTestVmSet.actionConfig(self, eNic0AttachType = vboxcon.NetworkAttachmentType_NAT);

    def actionExecute(self):
        """
        Execute the testcase.
        """
        return self.oTestVmSet.actionExecute(self, self.testOneVmConfig)

    def testOneVmConfig(self, oVM, oTestVm):
        """
        Install guest OS and wait for result
        """

        self.logVmInfo(oVM)
        reporter.testStart('Installing %s' % (oTestVm.sVmName,))

        cMsTimeout = 40*60000;
        if not reporter.isLocal(): ## @todo need to figure a better way of handling timeouts on the testboxes ...
            cMsTimeout = 180 * 60000; # will be adjusted down.

        oSession, _ = self.startVmAndConnectToTxsViaTcp(oTestVm.sVmName, fCdWait = False, cMsTimeout = cMsTimeout);
        if oSession is not None:
            # The guest has connected to TXS, so we're done (for now anyways).
            reporter.log('Guest reported success')
            ## @todo Do save + restore.

            reporter.testDone()
            fRc = self.terminateVmBySession(oSession)
            return fRc is True

        reporter.error('Installation of %s has failed' % (oTestVm.sVmName,))
        oTestVm.detatchAndDeleteHd(self); # Save space.
        reporter.testDone()
        return False

if __name__ == '__main__':
    sys.exit(tdGuestOsInstTest1().main(sys.argv))

