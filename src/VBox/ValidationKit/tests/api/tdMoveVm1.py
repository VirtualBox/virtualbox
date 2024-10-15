#!/usr/bin/env python
# -*- coding: utf-8 -*-
# "$Id: tdMoveVm1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $"

"""
VirtualBox Validation Kit - VM Move Test #1
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
import shutil
from collections import defaultdict

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0]
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(g_ksValidationKitDir)

# Validation Kit imports.
from testdriver import base
from testdriver import reporter
from testdriver import vboxcon
from testdriver import vboxwrappers
from tdMoveMedium1 import SubTstDrvMoveMedium1; # pylint: disable=relative-import


# @todo r=aeichner: The whole path handling/checking needs fixing to also work on Windows
#                   The current quick workaround is to spill os.path.normcase() all over the place when
#                   constructing paths. I'll leave the real fix to the original author...
class SubTstDrvMoveVm1(base.SubTestDriverBase):
    """
    Sub-test driver for VM Move Test #1.
    """

    def __init__(self, oTstDrv):
        base.SubTestDriverBase.__init__(self, oTstDrv, 'move-vm', 'Move VM');

        # Note! Hardcoded indexing in test code further down.
        self.asRsrcs = [
            os.path.join('5.3','isos','tdMoveVM1.iso'),
            os.path.join('5.3','floppy','tdMoveVM1.img')
        ];

        self.asImagesNames = []
        self.dsKeys = {
            'StandardImage':    'SATA Controller',
            'ISOImage':         'IDE Controller',
            'FloppyImage':      'Floppy Controller',
            'SettingsFile':     'Settings File',
            'LogFile':          'Log File',
            'SavedStateFile':   'Saved State File',
            'SnapshotFile':     'Snapshot File'
        };

    def testIt(self):
        """
        Execute the sub-testcase.
        """
        reporter.testStart(self.sTestName);
        reporter.log('ValidationKit folder is "%s"' % (g_ksValidationKitDir,))
        fRc = self.testVMMove();
        return reporter.testDone(fRc is None)[1] == 0;

    #
    # Test execution helpers.
    #

    def createTestMachine(self):
        """
        Document me here, not with hashes above.
        """
        oVM = self.oTstDrv.createTestVM('test-vm-move', 1, None, 4)
        if oVM is None:
            return None

        # create hard disk images, one for each file-based backend, using the first applicable extension
        fRc = True
        oSession = self.oTstDrv.openSession(oVM)
        aoDskFmts = self.oTstDrv.oVBoxMgr.getArray(self.oTstDrv.oVBox.systemProperties, 'mediumFormats')

        for oDskFmt in aoDskFmts:
            aoDskFmtCaps = self.oTstDrv.oVBoxMgr.getArray(oDskFmt, 'capabilities')
            if vboxcon.MediumFormatCapabilities_File not in aoDskFmtCaps \
                or vboxcon.MediumFormatCapabilities_CreateDynamic not in aoDskFmtCaps:
                continue
            (asExts, aTypes) = oDskFmt.describeFileExtensions()
            for i in range(0, len(asExts)): # pylint: disable=consider-using-enumerate
                if aTypes[i] is vboxcon.DeviceType_HardDisk:
                    sExt = '.' + asExts[i]
                    break
            if sExt is None:
                fRc = False
                break
            sFile = 'test-vm-move' + str(len(self.asImagesNames)) + sExt
            sHddPath = os.path.join(self.oTstDrv.sScratchPath, sFile)
            oHd = oSession.createBaseHd(sHddPath, sFmt=oDskFmt.id, cb=1024*1024)
            if oHd is None:
                fRc = False
                break

            # attach HDD, IDE controller exists by default, but we use SATA just in case
            sController = self.dsKeys['StandardImage']
            fRc = fRc and oSession.attachHd(sHddPath, sController, iPort = len(self.asImagesNames),
                                            fImmutable=False, fForceResource=False)
            if fRc:
                self.asImagesNames.append(sFile)

        fRc = fRc and oSession.saveSettings()
        fRc = oSession.close() and fRc

        if fRc is False:
            oVM = None

        return oVM

    def moveVMToLocation(self, sLocation, oVM):
        """
        Document me here, not with hashes above.
        """
        fRc = True
        try:

            ## @todo r=bird: Too much unncessary crap inside try clause.  Only oVM.moveTo needs to be here.
            ##               Though, you could make an argument for oVM.name too, perhaps.

            # move machine
            reporter.log('Moving machine "%s" to the "%s"' % (oVM.name, sLocation))
            sType = 'basic'
            oProgress = vboxwrappers.ProgressWrapper(oVM.moveTo(sLocation, sType), self.oTstDrv.oVBoxMgr, self.oTstDrv,
                                                     'moving machine "%s"' % (oVM.name,))

        except:
            return reporter.errorXcpt('Machine::moveTo("%s") for machine "%s" failed' % (sLocation, oVM.name,))

        oProgress.wait()
        if oProgress.logResult() is False:
            fRc = False
            reporter.log('Progress object returned False')
        else:
            fRc = True

        return fRc

    def checkLocation(self, oMachine, dsReferenceFiles):
        """
        Document me.

        Prerequisites:
        1. All standard images are attached to SATA controller
        2. All ISO images are attached to IDE controller
        3. All floppy images are attached to Floppy controller
        4. The type defaultdict from collection is used here (some sort of multimap data structure)
        5. The dsReferenceFiles parameter here is the structure defaultdict(set):
           [
           ('StandardImage': ['somedisk.vdi', 'somedisk.vmdk',...]),
            ('ISOImage': ['somedisk_1.iso','somedisk_2.iso',...]),
            ('FloppyImage': ['somedisk_1.img','somedisk_2.img',...]),
            ('SnapshotFile': ['snapshot file 1','snapshot file 2', ...]),
            ('SettingsFile', ['setting file',...]),
            ('SavedStateFile': ['state file 1','state file 2',...]),
            ('LogFile': ['log file 1','log file 2',...]),
            ]
        """

        fRc = True

        for sKey, sValue in self.dsKeys.items():
            aActuals = set()
            aReferences = set()

            # Check standard images locations, ISO files locations, floppy images locations, snapshots files locations
            if sKey in ('StandardImage', 'ISOImage', 'FloppyImage',):
                aReferences = dsReferenceFiles[sKey]
                if aReferences:
                    aoMediumAttachments = oMachine.getMediumAttachmentsOfController(sValue) ##@todo r=bird: API call, try-except!
                    for oAttachment in aoMediumAttachments:
                        aActuals.add(os.path.normcase(oAttachment.medium.location))

            elif sKey == 'SnapshotFile':
                aReferences = dsReferenceFiles[sKey]
                if aReferences:
                    aActuals = self.__getSnapshotsFiles(oMachine)

            # Check setting file location
            elif sKey == 'SettingsFile':
                aReferences = dsReferenceFiles[sKey]
                if aReferences:
                    aActuals.add(os.path.normcase(oMachine.settingsFilePath))

            # Check saved state files location
            elif sKey == 'SavedStateFile':
                aReferences = dsReferenceFiles[sKey]
                if aReferences:
                    aActuals = self.__getStatesFiles(oMachine)

            # Check log files location
            elif sKey == 'LogFile':
                aReferences = dsReferenceFiles[sKey]
                if aReferences:
                    aActuals = self.__getLogFiles(oMachine)

            if aActuals:
                reporter.log('Check %s' % (sKey))
                intersection = aReferences.intersection(aActuals)
                for eachItem in intersection:
                    reporter.log('Item location "%s" is correct' % (eachItem))

                difference = aReferences.difference(aActuals)
                for eachItem in difference:
                    reporter.log('Item location "%s" isn\'t correct' % (eachItem))

                reporter.log('####### Reference locations: #######')
                for eachItem in aActuals:
                    reporter.log(' "%s"' % (eachItem))

                if len(intersection) != len(aActuals):
                    reporter.log('Not all items in the right location. Check it.')
                    fRc = False

        return fRc

    def checkAPIVersion(self):
        return self.oTstDrv.fpApiVer >= 5.3;

    @staticmethod
    def __safeListDir(sDir):
        """ Wrapper around os.listdir that returns empty array instead of exceptions. """
        try:
            return os.listdir(sDir);
        except:
            return [];

    def __getStatesFiles(self, oMachine, fPrint = False):
        asStateFilesList = set()
        sFolder = oMachine.snapshotFolder;
        for sFile in self.__safeListDir(sFolder):
            if sFile.endswith(".sav"):
                sFullPath = os.path.normcase(os.path.join(sFolder, sFile));
                asStateFilesList.add(sFullPath)
                if fPrint is True:
                    reporter.log("State file is %s" % (sFullPath))
        return asStateFilesList

    def __getSnapshotsFiles(self, oMachine, fPrint = False):
        asSnapshotsFilesList = set()
        sFolder = oMachine.snapshotFolder
        for sFile in self.__safeListDir(sFolder):
            if sFile.endswith(".sav") is False:
                sFullPath = os.path.normcase(os.path.join(sFolder, sFile));
                asSnapshotsFilesList.add(sFullPath)
                if fPrint is True:
                    reporter.log("Snapshot file is %s" % (sFullPath))
        return asSnapshotsFilesList

    def __getLogFiles(self, oMachine, fPrint = False):
        asLogFilesList = set()
        sFolder = oMachine.logFolder
        for sFile in self.__safeListDir(sFolder):
            if sFile.endswith(".log"):
                sFullPath = os.path.normcase(os.path.join(sFolder, sFile));
                asLogFilesList.add(sFullPath)
                if fPrint is True:
                    reporter.log("Log file is %s" % (sFullPath))
        return asLogFilesList


    def __testScenario_2(self, oSession, oMachine, sNewLoc, sOldLoc):
        """
        All disks attached to VM are located inside the VM's folder.
        There are no any snapshots and logs.
        """

        sController = self.dsKeys['StandardImage']
        aoMediumAttachments = oMachine.getMediumAttachmentsOfController(sController)
        oSubTstDrvMoveMedium1Instance = SubTstDrvMoveMedium1(self.oTstDrv)
        oSubTstDrvMoveMedium1Instance.moveTo(sOldLoc, aoMediumAttachments)

        del oSubTstDrvMoveMedium1Instance

        dsReferenceFiles = defaultdict(set)

        for s in self.asImagesNames:
            reporter.log('"%s"' % (s,))
            dsReferenceFiles['StandardImage'].add(os.path.normcase(sNewLoc + os.sep + oMachine.name + os.sep + s))

        sSettingFile = os.path.join(sNewLoc, os.path.join(oMachine.name, oMachine.name + '.vbox'))
        dsReferenceFiles['SettingsFile'].add(os.path.normcase(sSettingFile))

        fRc = self.moveVMToLocation(sNewLoc, oSession.o.machine)

        if fRc is True:
            fRc = self.checkLocation(oSession.o.machine, dsReferenceFiles)
            if fRc is False:
                reporter.testFailure('!!!!!!!!!!!!!!!!!! 2nd scenario: Check locations failed... !!!!!!!!!!!!!!!!!!')
        else:
            reporter.testFailure('!!!!!!!!!!!!!!!!!! 2nd scenario: Move VM failed... !!!!!!!!!!!!!!!!!!')

        fRes = oSession.saveSettings()
        if fRes is False:
            reporter.log('2nd scenario: Couldn\'t save machine settings')

        return fRc

    def __testScenario_3(self, oSession, oMachine, sNewLoc):
        """
        There are snapshots
        """

        # At moment, it's used only one snapshot due to the difficulty to get
        # all attachments of the machine (i.e. not only attached at moment)
        cSnap = 1

        for counter in range(1,cSnap+1):
            strSnapshot = 'Snapshot' + str(counter)
            fRc = oSession.takeSnapshot(strSnapshot)
            if fRc is False:
                reporter.testFailure('3rd scenario: Can\'t take snapshot "%s"' % (strSnapshot,))

        dsReferenceFiles = defaultdict(set)

        sController = self.dsKeys['StandardImage']
        aoMediumAttachments = oMachine.getMediumAttachmentsOfController(sController)
        if fRc is True:
            for oAttachment in aoMediumAttachments:
                sRes = oAttachment.medium.location.rpartition(os.sep)
                dsReferenceFiles['SnapshotFile'].add(os.path.normcase(sNewLoc + os.sep + oMachine.name + os.sep +
                                                     'Snapshots' + os.sep + sRes[2]))

            sSettingFile = os.path.join(sNewLoc, os.path.join(oMachine.name, oMachine.name + '.vbox'))
            dsReferenceFiles['SettingsFile'].add(os.path.normcase(sSettingFile))

            fRc = self.moveVMToLocation(sNewLoc, oSession.o.machine)

        if fRc is True:
            fRc = self.checkLocation(oSession.o.machine, dsReferenceFiles)
            if fRc is False:
                reporter.testFailure('!!!!!!!!!!!!!!!!!! 3rd scenario: Check locations failed... !!!!!!!!!!!!!!!!!!')
        else:
            reporter.testFailure('!!!!!!!!!!!!!!!!!! 3rd scenario: Move VM failed... !!!!!!!!!!!!!!!!!!')

        fRes = oSession.saveSettings()
        if fRes is False:
            reporter.log('3rd scenario: Couldn\'t save machine settings')

        return fRc

    def __testScenario_4(self, oMachine, sNewLoc):
        """
        There are one or more save state files in the snapshots folder
        and some files in the logs folder.
        Here we run VM, next stop it in the "save" state.
        And next move VM
        """

        # Run VM and get new Session object.
        oSession = self.oTstDrv.startVm(oMachine);
        if not oSession:
            return False;

        # Some time interval should be here for not closing VM just after start.
        self.oTstDrv.waitForTasks(1000);

        if oMachine.state != self.oTstDrv.oVBoxMgr.constants.MachineState_Running:
            reporter.log("Machine '%s' is not Running" % (oMachine.name))
            fRc = False

        # Call Session::saveState(), already closes session unless it failed.
        fRc = oSession.saveState()
        if fRc is True:
            reporter.log("Machine is in saved state")

        fRc = self.oTstDrv.terminateVmBySession(oSession)

        if fRc is True:
            # Create a new Session object for moving VM.
            oSession = self.oTstDrv.openSession(oMachine)

            # Always clear before each scenario.
            dsReferenceFiles = defaultdict(set)

            asLogs = self.__getLogFiles(oMachine)
            for sFile in asLogs:
                sRes = sFile.rpartition(os.sep)
                dsReferenceFiles['LogFile'].add(os.path.normcase(sNewLoc + os.sep + oMachine.name + os.sep +
                                                'Logs' + os.sep + sRes[2]))

            asStates = self.__getStatesFiles(oMachine)
            for sFile in asStates:
                sRes = sFile.rpartition(os.sep)
                dsReferenceFiles['SavedStateFile'].add(os.path.normcase(sNewLoc + os.sep + oMachine.name + os.sep +
                                                       'Snapshots' + os.sep + sRes[2]))

            fRc = self.moveVMToLocation(sNewLoc, oSession.o.machine)

            if fRc is True:
                fRc = self.checkLocation(oSession.o.machine, dsReferenceFiles)
                if fRc is False:
                    reporter.testFailure('!!!!!!!!!!!!!!!!!! 4th scenario: Check locations failed... !!!!!!!!!!!!!!!!!!')
            else:
                reporter.testFailure('!!!!!!!!!!!!!!!!!! 4th scenario: Move VM failed... !!!!!!!!!!!!!!!!!!')

            # cleaning up: get rid of saved state
            fRes = oSession.discardSavedState(True)
            if fRes is False:
                reporter.log('4th scenario: Failed to discard the saved state of machine')

            fRes = oSession.close()
            if fRes is False:
                reporter.log('4th scenario: Couldn\'t close machine session')
        else:
            reporter.testFailure('!!!!!!!!!!!!!!!!!! 4th scenario: Terminate machine by session failed... !!!!!!!!!!!!!!!!!!')

        return fRc

    def __testScenario_5(self, oMachine, sNewLoc, sOldLoc):
        """
        There is an ISO image (.iso) attached to the VM.
        Prerequisites - there is IDE Controller and there are no any images attached to it.
        """

        fRc = True
        sISOImageName = 'tdMoveVM1.iso'

        # Always clear before each scenario.
        dsReferenceFiles = defaultdict(set)

        # Create a new Session object.
        oSession = self.oTstDrv.openSession(oMachine)

        sISOLoc = self.asRsrcs[0] # '5.3/isos/tdMoveVM1.iso'
        reporter.log("sHost is '%s', sResourcePath is '%s'" % (self.oTstDrv.sHost, self.oTstDrv.sResourcePath))
        sISOLoc = self.oTstDrv.getFullResourceName(sISOLoc)
        reporter.log("sISOLoc is '%s'" % (sISOLoc,))

        if not os.path.exists(sISOLoc):
            reporter.log('ISO file does not exist at "%s"' % (sISOLoc,))
            fRc = False

        # Copy ISO image from the common resource folder into machine folder.
        shutil.copy(sISOLoc, sOldLoc)

        # Attach ISO image to the IDE controller.
        if fRc is True:
            # Set actual ISO location.
            sISOLoc = sOldLoc + os.sep + sISOImageName
            reporter.log("sISOLoc is '%s'" % (sISOLoc,))
            if not os.path.exists(sISOLoc):
                reporter.log('ISO file does not exist at "%s"' % (sISOLoc,))
                fRc = False

            sController=self.dsKeys['ISOImage']
            aoMediumAttachments = oMachine.getMediumAttachmentsOfController(sController)
            iPort = len(aoMediumAttachments)
            fRc = oSession.attachDvd(sISOLoc, sController, iPort, iDevice = 0)
            dsReferenceFiles['ISOImage'].add(os.path.normcase(os.path.join(os.path.join(sNewLoc, oMachine.name), sISOImageName)))

        if fRc is True:
            fRc = self.moveVMToLocation(sNewLoc, oSession.o.machine)
            if fRc is True:
                fRc = self.checkLocation(oSession.o.machine, dsReferenceFiles)
                if fRc is False:
                    reporter.testFailure('!!!!!!!!!!!!!!!!!! 5th scenario: Check locations failed... !!!!!!!!!!!!!!!!!!')
            else:
                reporter.testFailure('!!!!!!!!!!!!!!!!!! 5th scenario: Move VM failed... !!!!!!!!!!!!!!!!!!')
        else:
            reporter.testFailure('!!!!!!!!!!!!!!!!!! 5th scenario: Attach ISO image failed... !!!!!!!!!!!!!!!!!!')

        # Detach ISO image.
        fRes = oSession.detachHd(sController, iPort, 0)
        if fRes is False:
            reporter.log('5th scenario: Couldn\'t detach image from the controller %s '
                         'port %s device %s' % (sController, iPort, 0))

        fRes = oSession.saveSettings()
        if fRes is False:
            reporter.log('5th scenario: Couldn\'t save machine settings')

        fRes = oSession.close()
        if fRes is False:
            reporter.log('5th scenario: Couldn\'t close machine session')

        return fRc

    def __testScenario_6(self, oMachine, sNewLoc, sOldLoc):
        """
        There is a floppy image (.img) attached to the VM.
        Prerequisites - there is Floppy Controller and there are no any images attached to it.
        """

        fRc = True

        # Always clear before each scenario.
        dsReferenceFiles = defaultdict(set)

        # Create a new Session object.
        oSession = self.oTstDrv.openSession(oMachine)

        sFloppyLoc = self.asRsrcs[1] # '5.3/floppy/tdMoveVM1.img'
        sFloppyLoc = self.oTstDrv.getFullResourceName(sFloppyLoc)

        if not os.path.exists(sFloppyLoc):
            reporter.log('Floppy disk does not exist at "%s"' % (sFloppyLoc,))
            fRc = False

        # Copy floppy image from the common resource folder into machine folder.
        shutil.copy(sFloppyLoc, sOldLoc)

        # Attach floppy image.
        if fRc is True:
            # Set actual floppy location.
            sFloppyImageName = 'tdMoveVM1.img'
            sFloppyLoc = sOldLoc + os.sep + sFloppyImageName
            sController=self.dsKeys['FloppyImage']
            fRc = fRc and oSession.attachFloppy(sFloppyLoc, sController, 0, 0)
            dsReferenceFiles['FloppyImage'].add(os.path.normcase(os.path.join(os.path.join(sNewLoc, oMachine.name),
                                                                                           sFloppyImageName)))

        if fRc is True:
            fRc = self.moveVMToLocation(sNewLoc, oSession.o.machine)
            if fRc is True:
                fRc = self.checkLocation(oSession.o.machine, dsReferenceFiles)
                if fRc is False:
                    reporter.testFailure('!!!!!!!!!!!!!!!!!! 6th scenario: Check locations failed... !!!!!!!!!!!!!!!!!!')
            else:
                reporter.testFailure('!!!!!!!!!!!!!!!!!! 6th scenario: Move VM failed... !!!!!!!!!!!!!!!!!!')
        else:
            reporter.testFailure('!!!!!!!!!!!!!!!!!! 6th scenario: Attach floppy image failed... !!!!!!!!!!!!!!!!!!')

        # Detach floppy image.
        fRes = oSession.detachHd(sController, 0, 0)
        if fRes is False:
            reporter.log('6th scenario: Couldn\'t detach image from the controller %s port %s device %s' % (sController, 0, 0))

        fRes = oSession.saveSettings()
        if fRes is False:
            reporter.testFailure('6th scenario: Couldn\'t save machine settings')

        fRes = oSession.close()
        if fRes is False:
            reporter.log('6th scenario: Couldn\'t close machine session')
        return fRc


    def testVMMove(self):
        """
        Test machine moving.
        """
        if not self.oTstDrv.importVBoxApi():
            return False

        fSupported = self.checkAPIVersion()
        reporter.log('ValidationKit folder is "%s"' % (g_ksValidationKitDir,))

        if fSupported is False:
            reporter.log('API version %s is too old. Just skip this test.' % (self.oTstDrv.fpApiVer))
            return None;
        reporter.log('API version is "%s".' % (self.oTstDrv.fpApiVer))

        # Scenarios
        # 1. All disks attached to VM are located outside the VM's folder.
        #    There are no any snapshots and logs.
        #    In this case only VM setting file should be moved (.vbox file)
        #
        # 2. All disks attached to VM are located inside the VM's folder.
        #    There are no any snapshots and logs.
        #
        # 3. There are snapshots.
        #
        # 4. There are one or more save state files in the snapshots folder
        #    and some files in the logs folder.
        #
        # 5. There is an ISO image (.iso) attached to the VM.
        #
        # 6. There is a floppy image (.img) attached to the VM.
        #
        # 7. There are shareable disk and immutable disk attached to the VM.

        try: ## @todo r=bird: Would be nice to use sub-tests here for each scenario, however
             ##               this try/catch as well as lots of return points makes that very hard.
             ##               Big try/catch stuff like this should be avoided.
            # Create test machine.
            oMachine = self.createTestMachine()
            if oMachine is None:
                reporter.error('Failed to create test machine')

            # Create temporary subdirectory in the current working directory.
            sOrigLoc = self.oTstDrv.sScratchPath
            sBaseLoc = os.path.join(sOrigLoc, 'moveFolder')
            os.mkdir(sBaseLoc, 0o775)

            # lock machine
            # get session machine
            oSession = self.oTstDrv.openSession(oMachine)
            fRc = True

            sNewLoc = sBaseLoc + os.sep

            dsReferenceFiles = defaultdict(set)

            #
            # 1. case:
            #
            # All disks attached to VM are located outside the VM's folder.
            # There are no any snapshots and logs.
            # In this case only VM setting file should be moved (.vbox file)
            #
            reporter.log("Scenario #1:");
            for s in self.asImagesNames:
                reporter.log('"%s"' % (s,))
                dsReferenceFiles['StandardImage'].add(os.path.normcase(os.path.join(sOrigLoc, s)))

            sSettingFile = os.path.normcase(os.path.join(sNewLoc, os.path.join(oMachine.name, oMachine.name + '.vbox')))
            dsReferenceFiles['SettingsFile'].add(sSettingFile)

            fRc = self.moveVMToLocation(sNewLoc, oSession.o.machine)

            if fRc is True:
                fRc = self.checkLocation(oSession.o.machine, dsReferenceFiles)
                if fRc is False:
                    reporter.testFailure('!!!!!!!!!!!!!!!!!! 1st scenario: Check locations failed... !!!!!!!!!!!!!!!!!!')
                    return False;
            else:
                reporter.testFailure('!!!!!!!!!!!!!!!!!! 1st scenario: Move VM failed... !!!!!!!!!!!!!!!!!!')
                return False;

            fRc = oSession.saveSettings()
            if fRc is False:
                reporter.testFailure('1st scenario: Couldn\'t save machine settings')

            #
            # 2. case:
            #
            # All disks attached to VM are located inside the VM's folder.
            # There are no any snapshots and logs.
            #
            reporter.log("Scenario #2:");
            sOldLoc = sNewLoc + oMachine.name + os.sep
            sNewLoc = os.path.join(sOrigLoc, 'moveFolder_2nd_scenario')
            os.mkdir(sNewLoc, 0o775)

            fRc = self.__testScenario_2(oSession, oMachine, sNewLoc, sOldLoc)
            if fRc is False:
                return False;

            #
            # 3. case:
            #
            # There are snapshots.
            #
            reporter.log("Scenario #3:");
            sOldLoc = sNewLoc + oMachine.name + os.sep
            sNewLoc = os.path.join(sOrigLoc, 'moveFolder_3rd_scenario')
            os.mkdir(sNewLoc, 0o775)

            fRc = self.__testScenario_3(oSession, oMachine, sNewLoc)
            if fRc is False:
                return False;

            #
            # 4. case:
            #
            # There are one or more save state files in the snapshots folder
            # and some files in the logs folder.
            # Here we run VM, next stop it in the "save" state.
            # And next move VM
            #
            reporter.log("Scenario #4:");
            sOldLoc = sNewLoc + oMachine.name + os.sep
            sNewLoc = os.path.join(sOrigLoc, 'moveFolder_4th_scenario')
            os.mkdir(sNewLoc, 0o775)

            # Close Session object because after starting VM we get new instance of session
            fRc = oSession.close() and fRc
            if fRc is False:
                reporter.log('Couldn\'t close machine session')

            del oSession

            fRc = self.__testScenario_4(oMachine, sNewLoc)
            if fRc is False:
                return False;

            #
            # 5. case:
            #
            # There is an ISO image (.iso) attached to the VM.
            # Prerequisites - there is IDE Controller and there are no any images attached to it.
            #
            reporter.log("Scenario #5:");
            sOldLoc = sNewLoc + os.sep + oMachine.name
            sNewLoc = os.path.join(sOrigLoc, 'moveFolder_5th_scenario')
            os.mkdir(sNewLoc, 0o775)
            fRc = self.__testScenario_5(oMachine, sNewLoc, sOldLoc)
            if fRc is False:
                return False;

            #
            # 6. case:
            #
            # There is a floppy image (.img) attached to the VM.
            # Prerequisites - there is Floppy Controller and there are no any images attached to it.
            #
            reporter.log("Scenario #6:");
            sOldLoc = sNewLoc + os.sep + oMachine.name
            sNewLoc = os.path.join(sOrigLoc, 'moveFolder_6th_scenario')
            os.mkdir(sNewLoc, 0o775)
            fRc = self.__testScenario_6(oMachine, sNewLoc, sOldLoc)
            if fRc is False:
                return False;

#            #
#            # 7. case:
#            #
#            # There are shareable disk and immutable disk attached to the VM.
#            #
#            reporter.log("Scenario #7:");
#            fRc = fRc and oSession.saveSettings()
#            if fRc is False:
#                reporter.log('Couldn\'t save machine settings')
#

            assert fRc is True
        except:
            reporter.errorXcpt()

        return fRc;


if __name__ == '__main__':
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    from tdApi1 import tdApi1; # pylint: disable=relative-import
    sys.exit(tdApi1([SubTstDrvMoveVm1]).main(sys.argv))

