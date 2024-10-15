#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdSnapshots1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Nested Snapshot Restoration Test #1
"""

__copyright__ = \
"""
Copyright (C) 2023-2024 Oracle and/or its affiliates.

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
from testdriver import base
from testdriver import reporter
from testdriver import vboxcon


class SubTstDrvNestedSnapshots1(base.SubTestDriverBase):
    """
    Sub-test driver for nested snapshot testing.
    """
    def __init__(self, oTstDrv):
        base.SubTestDriverBase.__init__(self, oTstDrv, 'nested-snapshots', 'Nested Snapshot Testing');
        self.sVmName = 'tst-nested-snapshots';
        # Note that any VM can be used here as these tests simply involve taking
        # online snapshots and restoring them.  This OL6 image was chosen based purely
        # on its location being in the 7.1 directory.
        self.asRsrcs = ['7.1/ol-6u10-x86.vdi'];

    #
    # Overridden methods specified in the TestDriverBase class (testdriver/base.py).
    #

    # Handle the action to execute the test itself.
    def testIt(self):
        """
        Execute the sub-testcases.
        """
        return  self.testRestoreNestedSnapshot() \
            and self.testDeleteSnapshots();

    #
    # Test execution helpers.
    #
    def testRestoreNestedSnapshot(self):
        """
        The scenario being exercised here is referenced in xTracker 10252 Comment #9
        where the sequence of restoring a nested snapshot and then booting that restored
        VM would accidentally delete that snapshot's saved state file during boot.  So
        here we perform the following steps to exercise this functionality:
          + Take two online snapshots of the VM: 'alpha', and 'beta' (IMachine::takeSnapshot())
          + Restore snapshot 'beta' (IMachine::restoreSnapshot())
          + Boot and then poweroff the VM
          + Verify snapshot 'beta' still exists (IMachine::findSnapshot())
        """
        reporter.testStart('testRestoreNestedSnapshot');
        reporter.log('Verify saved state file exists after nested snapshot restore');

        # Restoring an online snapshot requires an updated TXS (r157880 or later) for the
        # TCP keep alive support added in r157875 thus it is essential that the
        # ValidationKit ISO be mounted in the VM so that TXS can auto-update if needed.
        reporter.log('Creating test VM: \'%s\'' % self.sVmName);
        oVM = self.oTstDrv.createTestVM(self.sVmName, 1, sHd = '7.1/ol-6u10-x86.vdi',
                                        sKind = 'Oracle', fIoApic = True,
                                        sDvdImage = self.oTstDrv.sVBoxValidationKitIso);
        if oVM is None:
            reporter.error('Error creating test VM: \'%s\'' % self.sVmName);

        reporter.log('Starting test VM \'%s\' for the first time' % self.sVmName);
        oSession, oTxsSession = self.oTstDrv.startVmAndConnectToTxsViaTcp(self.sVmName, fCdWait=True,
                                                                          fNatForwardingForTxs = False);
        if oSession is None or oTxsSession is None:
            return reporter.error('Failed to start test VM: \'%s\'' % self.sVmName);

        reporter.log('Guest VM \'%s\' successfully started. Connection to TXS service established.' % self.sVmName);
        self.oTstDrv.addTask(oTxsSession);

        # Take two online snapshots.
        reporter.log('Taking two online snapshots of test VM: \'%s\'' % self.sVmName);
        fRc = oSession.takeSnapshot('alpha');
        fRc = fRc and oSession.takeSnapshot('beta');

        # Shutdown the VM and cleanup.
        self.oTstDrv.txsDisconnect(oSession, oTxsSession)
        reporter.log('Shutting down test VM: \'%s\'' % self.sVmName);
        self.oTstDrv.removeTask(oTxsSession);
        self.oTstDrv.terminateVmBySession(oSession);
        fRc = oSession.close() and fRc and True; # pychecker hack.
        oSession = None;
        oTxsSession = None;
        if not fRc:
            return reporter.error('Failed to take snapshot of test VM: \'%s\'' % self.sVmName);

        oVM = self.oTstDrv.getVmByName(self.sVmName);
        oSession = self.oTstDrv.openSession(oVM);
        if oSession is None:
            return reporter.error('Failed to create session for test VM: \'%s\'' % self.sVmName);

        oSnapshot = oSession.findSnapshot('beta');
        if oSnapshot is None:
            return reporter.testFailure('Failed to find snapshot \'beta\' of test VM: \'%s\'' % self.sVmName);

        reporter.log('Restoring nested snapshot \'%s\' ({%s}) of test VM: \'%s\'' %
                     (oSnapshot.name, oSnapshot.id, self.sVmName));
        fRc = oSession.restoreSnapshot(oSnapshot);
        if not fRc:
            return reporter.error('Failed to restore snapshot \'%s\' of test VM: \'%s\'' % (oSnapshot.name, self.sVmName));

        fRc = oSession.close() and fRc and True; # pychecker hack.
        oSession = None;
        if not fRc:
            return reporter.error('Failed to close session of test VM: \'%s\'' % self.sVmName);

        reporter.log('Starting test VM after snapshot restore: \'%s\'' % self.sVmName);

        oSession, oTxsSession = self.oTstDrv.startVmAndConnectToTxsViaTcp(self.sVmName, fCdWait=True,
                                                                          fNatForwardingForTxs = False);
        if oSession is None or oTxsSession is None:
            return reporter.error('Failed to start test VM: \'%s\'' % self.sVmName);

        # Display the version of TXS running in the guest VM to confirm that it is r157880 or later.
        sTxsVer = self.oTstDrv.txsVer(oSession, oTxsSession, cMsTimeout=1000*30*60, fIgnoreErrors = True);
        if sTxsVer is not False:
            reporter.log('startVmAndConnectToTxsViaTcp: TestExecService version %s' % (sTxsVer));
        else:
            reporter.log('startVmAndConnectToTxsViaTcp: Unable to retrieve TestExecService version');

        reporter.log('Guest VM \'%s\' successfully started after restoring nested snapshot.' % self.sVmName);
        self.oTstDrv.addTask(oTxsSession);

        # Shutdown the VM and cleanup.
        reporter.log('Shutting down test VM: \'%s\'' % self.sVmName);
        self.oTstDrv.removeTask(oTxsSession);
        self.oTstDrv.terminateVmBySession(oSession);
        fRc = oSession.close() and fRc and True; # pychecker hack.
        oSession = None;
        oTxsSession = None;
        if not fRc:
            return reporter.testFailure('Failed to close session of test VM: \'%s\'' % self.sVmName);

        reporter.log('Verifying nested snapshot \'beta\' still exists.');
        oVM = self.oTstDrv.getVmByName(self.sVmName);
        oSession = self.oTstDrv.openSession(oVM);
        if oSession is None:
            return reporter.error('Failed to create session for test VM: \'%s\'' % self.sVmName);

        oSnapshot = oSession.findSnapshot('beta');
        if oSnapshot is None:
            return reporter.testFailure('Failed to find snapshot \'beta\' of test VM: \'%s\'' % self.sVmName);

        return reporter.testDone()[1] == 0;


    def testDeleteSnapshots(self):
        """
        The scenario being exercised here is also referenced in xTracker 10252 Comment #9
        where unregistering and deleting a VM which contained one or more snapshots would
        neglect to delete the snapshot(s).  So here we perform the following steps to
        exercise this functionality which conveniently also tidies up our test setup:
          + Unregister our test VM (IMachine::unregister())
          + Delete the VM and the attached media including snapshots (IMachine::deleteConfig())
          + Check that the snapshots are truly gone.
        """

        reporter.testStart('testDeleteSnapshots');
        reporter.log('Verify IMachine::unregister()+IMachine::deleteConfig() deletes snapshots');
        oVM = self.oTstDrv.getVmByName(self.sVmName);
        # IMachine::stateFilePath() isn't implemented in the testdriver so we manually
        # retrieve the paths to the snapshots.
        asStateFilesList = [];
        sSnapshotFolder = oVM.snapshotFolder;
        for sFile in os.listdir(sSnapshotFolder):
            if sFile.endswith(".sav"):
                sSnapshotFullPath = os.path.normcase(os.path.join(sSnapshotFolder, sFile));
                asStateFilesList.append(sSnapshotFullPath)
                reporter.log("Snapshot path = %s" % (sSnapshotFullPath))

        reporter.log('Calling IMachine::unregister() and IMachine::deleteConfig()');
        # Call IMachine::unregister() and IMachine::deleteConfig() (or their historic
        # equivalents) to remove all vestiges of the VM from the system.
        if self.oTstDrv.fpApiVer >= 4.0:
            try:
                oVM.unregister(vboxcon.CleanupMode_Full);
            except:
                return reporter.error('Failed to unregister VM \'%s\'' % self.sVmName);
            try:
                if self.oTstDrv.fpApiVer >= 4.3:
                    oProgress = oVM.deleteConfig([]);
                else:
                    oProgress = oVM.delete([]);
            except:
                return reporter.error('Failed to delete configuration of VM \'%s\'' % self.sVmName);
            self.oTstDrv.waitOnProgress(oProgress);
        else:
            try:
                self.oTstDrv.oVBox.unregisterMachine(oVM.id);
            except:
                return reporter.error('Failed to unregister VM \'%s\'' % self.sVmName);
            try:
                oVM.deleteSettings();
            except:
                return reporter.error('Failed to delete configuration of VM \'%s\'' % self.sVmName);

        # Verify that all of the snapshots were removed as part of the
        # IMachine::deleteConfig() call.
        reporter.log('Verifying no snapshots remain after IMachine::deleteConfig()');
        for sFile in os.listdir(sSnapshotFolder):
            if os.path.exists(sFile):
                return reporter.error('Snapshot \'%s\' was not deleted' % sFile);

        return reporter.testDone()[1] == 0;

if __name__ == '__main__':
    sys.path.append(os.path.dirname(os.path.abspath(__file__)));
    from tdApi1 import tdApi1;      # pylint: disable=relative-import
    sys.exit(tdApi1([SubTstDrvNestedSnapshots1]).main(sys.argv))
