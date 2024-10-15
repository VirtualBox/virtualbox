#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdTreeDepth1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Medium and Snapshot Tree Depth Test #1
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
import random
import time
import gc

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0]
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(g_ksValidationKitDir)

# Validation Kit imports.
from testdriver import base
from testdriver import reporter
from testdriver import vboxcon


class SubTstDrvTreeDepth1(base.SubTestDriverBase):
    """
    Sub-test driver for Medium and Snapshot Tree Depth Test #1.
    """

    def __init__(self, oTstDrv):
        base.SubTestDriverBase.__init__(self, oTstDrv, 'tree-depth', 'Media and Snapshot tree depths');

    def testIt(self):
        """
        Execute the sub-testcase.
        """
        return  self.testMediumTreeDepth() \
            and self.testSnapshotTreeDepth();

    def getNumOfAttachedHDs(self, oVM, sController):
        """
        Helper routine for counting the hard disks, including differencing disks,
        attached to the specified countroller.
        """
        try:
            aoMediumAttachments = oVM.getMediumAttachmentsOfController(sController);
        except:
            reporter.errorXcpt('oVM.getMediumAttachmentsOfController("%s") failed' % (sController));
            return -1;

        cDisks = 0;
        for oAttachment in aoMediumAttachments:
            oMedium = oAttachment.medium;
            if oMedium.deviceType is vboxcon.DeviceType_HardDisk:
                cDisks = cDisks + 1;
                reporter.log2('base medium = %s cDisks = %d' % (oMedium.location, cDisks));
                oParentMedium = oMedium.parent;
                while oParentMedium is not None:
                    cDisks = cDisks + 1;
                    reporter.log2('parent medium = %s cDisks = %d' % (oParentMedium.location, cDisks));
                    oParentMedium = oParentMedium.parent;
        return cDisks;

    def getNumOfHDsFromHardDisksArray(self):
        """
        Helper routine for counting the hard disks that belong to this VM which are
        contained in the global IVirtualBox::hardDisks[] array (which contains base media
        only).
        """
        cDisks = 0;
        aoHDs = self.oTstDrv.oVBoxMgr.getArray(self.oTstDrv.oVBox, 'hardDisks');
        # Walk the IVirtualBox::hardDisks[] array for hard disks belonging to this VM.
        # The array may contain entries from other VMs created by previous API tessts
        # which haven't unregistered and deleted their VM's configuration.  The location
        # attribute of each disk looks similar to:
        # /var/tmp/VBoxTestTmp/VBoxUserHome/Machines/vm 2/tdAppliance1-t2-disk1.vmdk
        # So we compare the directory where the disk is located, i.e. the 'basefolder',
        # with our VM's basefolder which looks similar to:
        # /var/tmp/VBoxTestTmp/VBoxUserHome/Machines/test-medium
        # to determine if the disk belongs to us.
        for oHD in aoHDs:
            sHDBaseFolder = os.path.dirname(oHD.location);
            sVMBaseFolder = os.path.join(self.oTstDrv.oVBox.systemProperties.defaultMachineFolder, 'test-medium');
            reporter.log2('HDBaseFolder = %s VMBaseFolder = %s' % (sHDBaseFolder, sVMBaseFolder));
            if sHDBaseFolder== sVMBaseFolder:
                cDisks = cDisks + 1;
        return cDisks;

    def unregisterVM(self, oVM, enmCleanupMode):
        """
        Helper routine to unregister the VM using the CleanupMode specified.
        """
        reporter.log('unregistering VM using %s' % enmCleanupMode);
        if enmCleanupMode == 'DetachAllReturnHardDisksOnly':
            try:
                aoHDs = oVM.unregister(vboxcon.CleanupMode_DetachAllReturnHardDisksOnly);
            except:
                return reporter.logXcpt('unregister(CleanupMode_DetachAllReturnHardDisksOnly) failed');
            for oHD in aoHDs:
                reporter.log2('closing medium = %s' % (oHD.location));
                oHD.close();
            aoHDs = None;
        elif enmCleanupMode == 'UnregisterOnly':
            try:
                aoHDs = oVM.unregister(vboxcon.CleanupMode_UnregisterOnly);
            except:
                return reporter.logXcpt('unregister(CleanupMode_UnregisterOnly) failed');
            if aoHDs:
                return reporter.error('unregister(CleanupMode_UnregisterOnly) failed: returned %d disks' %
                                      len(aoHDs));
        else:
            return reporter.error('unregisterVM: unexpected CleanupMode "%s"' % enmCleanupMode);

        return True;

    def openAndRegisterMachine(self, sSettingsFile):
        """
        Helper routine which opens a VM and registers it.
        """
        reporter.log('opening VM using configuration file = %s, testing config reading' % (sSettingsFile));
        try:
            if self.oTstDrv.fpApiVer >= 7.0:
                # Needs a password parameter since 7.0.
                oVM = self.oTstDrv.oVBox.openMachine(sSettingsFile, "");
            else:
                oVM = self.oTstDrv.oVBox.openMachine(sSettingsFile);
        except:
            reporter.logXcpt('openMachine(%s) failed' % (sSettingsFile));
            return None;

        if not oVM:
            return None;

        try:
            self.oTstDrv.oVBox.registerMachine(oVM);
        except:
            reporter.logXcpt('registerMachine(%s) failed' % (sSettingsFile));
            return None;

        return oVM;

    #
    # Test execution helpers.
    #

    def testMediumTreeDepth(self):
        """
        Test medium tree depth.
        """
        reporter.testStart('mediumTreeDepth')

        oVM = self.oTstDrv.createTestVMOnly('test-medium', 'Other')
        if oVM is None:
            return False;

        # Save the path to the VM's settings file while oVM is valid (needed for
        # openMachine() later when oVM is gone).
        sSettingsFile = oVM.settingsFilePath

        oSession = self.oTstDrv.openSession(oVM)
        if oSession is None:
            return False;

        # create chain with up to 64 disk images (medium tree depth limit)
        cImages = random.randrange(1, 64);
        reporter.log('Creating chain with %d disk images' % (cImages))
        for i in range(1, cImages + 1):
            sHddPath = os.path.join(self.oTstDrv.sScratchPath, 'Test' + str(i) + '.vdi')
            if i == 1:
                oHd = oSession.createBaseHd(sHddPath, cb=1024*1024)
            else:
                oHd = oSession.createDiffHd(oHd, sHddPath)
            if oHd is None:
                return False;

        # modify the VM config, attach HDD
        sController='SATA Controller';
        fRc = oSession.attachHd(sHddPath, sController, fImmutable=False, fForceResource=False);
        if fRc:
            fRc = oSession.saveSettings(fClose=True);
        oSession = None;
        if not fRc:
            return False;

        # Verify that the number of hard disks attached to the VM's only storage
        # controller equals the number of disks created above.
        cDisks = self.getNumOfAttachedHDs(oVM, sController);
        reporter.log('Initial state: Number of hard disks attached = %d (should equal disks created = %d)' % (cDisks, cImages));
        if cImages != cDisks:
            reporter.error('Created %d disk images but found %d disks attached' % (cImages, cDisks));

        # unregister the VM using "DetachAllReturnHardDisksOnly" and confirm all
        # hard disks were returned and subsequently closed
        fRc = self.unregisterVM(oVM, 'DetachAllReturnHardDisksOnly');
        if not fRc:
            return False;
        oVM = None;

        # If there are no base media belonging to this VM in the global IVirtualBox::hardDisks[]
        # array (expected) then there are no leftover child images either.
        cNumDisks = self.getNumOfHDsFromHardDisksArray();
        reporter.log('After unregister(DetachAllReturnHardDisksOnly): API reports %d base images (should be zero)' % (cNumDisks));
        if cNumDisks != 0:
            reporter.error('Got %d initial base images, expected zero (0)' % (cNumDisks));

        # re-register to test loading of settings
        oVM = self.openAndRegisterMachine(sSettingsFile);
        if oVM is None:
            return False;

        # Verify that the number of hard disks attached to the VM's only storage
        # controller equals the number of disks created above.
        cDisks = self.getNumOfAttachedHDs(oVM, sController);
        reporter.log('After openMachine()+registerMachine(): Number of hard disks attached = %d '
                     '(should equal disks created = %d)' % (cDisks, cImages));
        if cImages != cDisks:
            reporter.error('Created %d disk images but after openMachine()+registerMachine() found %d disks attached' %
                           (cImages, cDisks));

        fRc = self.unregisterVM(oVM, 'UnregisterOnly');
        if not fRc:
            return False;
        oVM = None;

        # When unregistering a VM with CleanupMode_UnregisterOnly the associated medium's
        # objects will be closed when the corresponding Machine object ('oVM') is
        # uninitialized.  Assigning the 'None' object to 'oVM' will cause Python to delete
        # the object when the garbage collector runs however this can take several seconds
        # so we invoke the Python garbage collector manually here so we don't have to wait.
        try:
            gc.collect();
        except:
            reporter.logXcpt();

        reporter.log('Waiting three seconds for Machine::uninit() to be called to close the attached disks');
        # Fudge factor: Machine::uninit() will be invoked when the oVM object is processed
        # by the garbage collector above but it may take a few moments to close up to 64
        # disks.
        time.sleep(3);

        # If there are no base media belonging to this VM in the global IVirtualBox::hardDisks[]
        # array (expected) then there are no leftover child images either.
        cNumDisks = self.getNumOfHDsFromHardDisksArray();
        reporter.log('After unregister(UnregisterOnly): API reports %d base images (should be zero)' % (cNumDisks));
        if cNumDisks != 0:
            reporter.error('Got %d base images after unregistering, expected zero (0)' % (cNumDisks));

        return reporter.testDone()[1] == 0;

    def testSnapshotTreeDepth(self):
        """
        Test snapshot tree depth.
        """
        reporter.testStart('snapshotTreeDepth')

        oVM = self.oTstDrv.createTestVMOnly('test-snap', 'Other');
        if oVM is None:
            return False;

        # Save the path to the VM's settings file while oVM is valid (needed for
        # openMachine() later when oVM is gone).
        sSettingsFile = oVM.settingsFilePath

        # modify the VM config, create and attach empty HDD
        oSession = self.oTstDrv.openSession(oVM)
        if oSession is None:
            return False;
        sHddPath = os.path.join(self.oTstDrv.sScratchPath, 'TestSnapEmpty.vdi')
        sController='SATA Controller';
        fRc = oSession.createAndAttachHd(sHddPath, cb=1024*1024, sController=sController, fImmutable=False);
        fRc = fRc and oSession.saveSettings();
        if not fRc:
            return False;

        # take up to 200 snapshots (250 is the snapshot tree depth limit (settings.h:SETTINGS_SNAPSHOT_DEPTH_MAX))
        cSnapshots = random.randrange(1, 200);
        reporter.log('Taking %d snapshots' % (cSnapshots));
        for i in range(1, cSnapshots + 1):
            fRc = oSession.takeSnapshot('Snapshot ' + str(i));
            if not fRc:
                return False;
        fRc = oSession.close() and fRc and True;
        oSession = None;
        reporter.log('API reports %d snapshots (should equal snapshots created = %d)' % (oVM.snapshotCount, cSnapshots));
        if oVM.snapshotCount != cSnapshots:
            reporter.error('Got %d initial snapshots, expected %d' % (oVM.snapshotCount, cSnapshots));

        # unregister the VM using "DetachAllReturnHardDisksOnly" and confirm all
        # hard disks were returned and subsequently closed
        fRc = self.unregisterVM(oVM, 'DetachAllReturnHardDisksOnly');
        if not fRc:
            return False;
        oVM = None;

        # If there are no base media belonging to this VM in the global IVirtualBox::hardDisks[]
        # array (expected) then there are no leftover child images either.
        cNumDisks = self.getNumOfHDsFromHardDisksArray();
        reporter.log('After unregister(DetachAllReturnHardDisksOnly): API reports %d base images (should be zero)' % (cNumDisks));
        fRc = fRc and cNumDisks == 0
        if cNumDisks != 0:
            reporter.error('Got %d initial base images, expected zero (0)' % (cNumDisks));

        # re-register to test loading of settings
        oVM = self.openAndRegisterMachine(sSettingsFile);
        if oVM is None:
            return False;

        # Verify that the number of hard disks attached to the VM's only storage
        # controller equals the number of disks created above.
        reporter.log('After openMachine()+registerMachine(): Number of snapshots of VM = %d '
                     '(should equal snapshots created = %d)' % (oVM.snapshotCount, cSnapshots));
        if oVM.snapshotCount != cSnapshots:
            reporter.error('Created %d snapshots but after openMachine()+registerMachine() found %d snapshots' %
                           (cSnapshots, oVM.snapshotCount));

        fRc = self.unregisterVM(oVM, 'UnregisterOnly');
        if not fRc:
            return False;
        oVM = None;

        # When unregistering a VM with CleanupMode_UnregisterOnly the associated medium's
        # objects will be closed when the corresponding Machine object ('oVM') is
        # uninitialized.  Assigning the 'None' object to 'oVM' will cause Python to delete
        # the object when the garbage collector runs however this can take several seconds
        # so we invoke the Python garbage collector manually here so we don't have to wait.
        try:
            gc.collect();
        except:
            reporter.logXcpt();

        reporter.log('Waiting three seconds for Machine::uninit() to be called to close the attached disks');
        # Fudge factor: Machine::uninit() will be invoked when the oVM object is processed
        # by the garbage collector above but it may take a few moments to close up to 200
        # snapshots.
        time.sleep(3);

        # If there are no base media belonging to this VM in the global IVirtualBox::hardDisks[]
        # array (expected) then there are no leftover child images either.
        cNumDisks = self.getNumOfHDsFromHardDisksArray();
        reporter.log('After unregister(UnregisterOnly): API reports %d base images (should be zero)' % (cNumDisks));
        if cNumDisks != 0:
            reporter.error('Got %d base images after unregistering, expected zero (0)' % (cNumDisks));

        return reporter.testDone()[1] == 0


if __name__ == '__main__':
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    from tdApi1 import tdApi1;      # pylint: disable=relative-import
    sys.exit(tdApi1([SubTstDrvTreeDepth1]).main(sys.argv))
