#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdStorageSnapshotMerging1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Storage snapshotting and merging testcase.
"""

__copyright__ = \
"""
Copyright (C) 2013-2024 Oracle and/or its affiliates.

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
from common     import utils;
from testdriver import reporter;
from testdriver import base;
from testdriver import vbox;
from testdriver import vboxcon;
from testdriver import vboxwrappers;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name


class tdStorageSnapshot(vbox.TestDriver):                                      # pylint: disable=too-many-instance-attributes
    """
    Storage benchmark.
    """
    def __init__(self):
        vbox.TestDriver.__init__(self);
        self.asRsrcs           = None;
        self.oGuestToGuestVM   = None;
        self.oGuestToGuestSess = None;
        self.oGuestToGuestTxs  = None;
        self.asStorageCtrlsDef = ['AHCI'];
        self.asStorageCtrls    = self.asStorageCtrlsDef;
        #self.asDiskFormatsDef  = ['VDI', 'VMDK', 'VHD', 'QED', 'Parallels', 'QCOW', 'iSCSI'];
        self.asDiskFormatsDef  = ['VDI', 'VMDK', 'VHD'];
        self.asDiskFormats     = self.asDiskFormatsDef;
        self.sRndData          = os.urandom(100*1024*1024);

    #
    # Overridden methods.
    #
    def showUsage(self):
        rc = vbox.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('tdStorageSnapshot1 Options:');
        reporter.log('  --storage-ctrls <type1[:type2[:...]]>');
        reporter.log('      Default: %s' % (':'.join(self.asStorageCtrls)));
        reporter.log('  --disk-formats  <type1[:type2[:...]]>');
        reporter.log('      Default: %s' % (':'.join(self.asDiskFormats)));
        return rc;

    def parseOption(self, asArgs, iArg):                                        # pylint: disable=too-many-branches,too-many-statements
        if asArgs[iArg] == '--storage-ctrls':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--storage-ctrls" takes a colon separated list of Storage controller types');
            self.asStorageCtrls = asArgs[iArg].split(':');
        elif asArgs[iArg] == '--disk-formats':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--disk-formats" takes a colon separated list of disk formats');
            self.asDiskFormats = asArgs[iArg].split(':');
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def getResourceSet(self):
        # Construct the resource list the first time it's queried.
        if self.asRsrcs is None:
            self.asRsrcs = ['5.3/storage/mergeMedium/t-orig.vdi',
                            '5.3/storage/mergeMedium/t-fixed.vdi',
                            '5.3/storage/mergeMedium/t-resized.vdi'];
        return self.asRsrcs;

    def actionExecute(self):
        """
        Execute the testcase.
        """
        fRc = self.test1();
        return fRc;

    def resizeMedium(self, oMedium, cbNewSize):
        if oMedium.deviceType is not vboxcon.DeviceType_HardDisk:
            return False;

        if oMedium.type is not vboxcon.MediumType_Normal:
            return False;

        #currently only VDI can be resizable. Medium variant is not checked, because testcase creates disks itself
        oMediumFormat = oMedium.mediumFormat;
        if oMediumFormat.id != 'VDI':
            return False;

        cbCurrSize = oMedium.logicalSize;
        # currently reduce is not supported
        if cbNewSize < cbCurrSize:
            return False;

        try:
            oProgressCom = oMedium.resize(cbNewSize);
        except:
            reporter.logXcpt('IMedium::resize failed on %s' % (oMedium.name));
            return False;
        oProgress = vboxwrappers.ProgressWrapper(oProgressCom, self.oVBoxMgr, self.oVBox.oTstDrv,
                                                 'Resize medium %s' % (oMedium.name));
        oProgress.wait(cMsTimeout = 15*60*1000); # 15 min
        oProgress.logResult();
        return True;

    def getMedium(self, oVM, sController):
        oMediumAttachments = oVM.getMediumAttachmentsOfController(sController);

        for oAttachment in oMediumAttachments:
            oMedium = oAttachment.medium;
            if oMedium.deviceType is not vboxcon.DeviceType_HardDisk:
                continue;
            if oMedium.type is not vboxcon.MediumType_Normal:
                continue;
            return oMedium;

        return None;

    def getSnapshotMedium(self, oSnapshot, sController):
        oVM = oSnapshot.machine;
        oMedium = self.getMedium(oVM, sController);

        aoMediumChildren = self.oVBoxMgr.getArray(oMedium, 'children')
        if aoMediumChildren is None or not aoMediumChildren:
            return None;

        for oChildMedium in aoMediumChildren:
            for uSnapshotId in oChildMedium.getSnapshotIds(oVM.id):
                if uSnapshotId == oVM.id:
                    return oChildMedium;

        return None;

    def openMedium(self, sHd, fImmutable = False):
        """
        Opens medium in readonly mode.
        Returns Medium object on success and None on failure.  Error information is logged.
        """
        sFullName = self.oVBox.oTstDrv.getFullResourceName(sHd);
        try:
            oHd = self.oVBox.findHardDisk(sFullName);
        except:
            try:
                if self.fpApiVer >= 4.1:
                    oHd = self.oVBox.openMedium(sFullName, vboxcon.DeviceType_HardDisk, vboxcon.AccessMode_ReadOnly, False);
                elif self.fpApiVer >= 4.0:
                    oHd = self.oVBox.openMedium(sFullName, vboxcon.DeviceType_HardDisk, vboxcon.AccessMode_ReadOnly);
                else:
                    oHd = self.oVBox.openHardDisk(sFullName, vboxcon.AccessMode_ReadOnly, False, "", False, "");

            except:
                reporter.errorXcpt('failed to open hd "%s"' % (sFullName));
                return None;

        try:
            if fImmutable:
                oHd.type = vboxcon.MediumType_Immutable;
            else:
                oHd.type = vboxcon.MediumType_Normal;

        except:
            if fImmutable:
                reporter.errorXcpt('failed to set hd "%s" immutable' % (sHd));
            else:
                reporter.errorXcpt('failed to set hd "%s" normal' % (sHd));

            return None;

        return oHd;

    def cloneMedium(self, oSrcHd, oTgtHd):
        """
        Clones medium into target medium.
        """
        try:
            oProgressCom = oSrcHd.cloneTo(oTgtHd, (vboxcon.MediumVariant_Standard, ), None);
        except:
            reporter.errorXcpt('failed to clone medium %s to %s' % (oSrcHd.name, oTgtHd.name));
            return False;
        oProgress = vboxwrappers.ProgressWrapper(oProgressCom, self.oVBoxMgr, self.oVBox.oTstDrv,
                                                 'clone base disk %s to %s' % (oSrcHd.name, oTgtHd.name));
        oProgress.wait(cMsTimeout = 15*60*1000); # 15 min
        oProgress.logResult();
        return True;

    def deleteVM(self, oVM):
        try:
            oVM.unregister(vboxcon.CleanupMode_DetachAllReturnNone);
        except:
            reporter.logXcpt();

        if self.fpApiVer >= 4.0:
            try:
                if self.fpApiVer >= 4.3:
                    oProgressCom = oVM.deleteConfig([]);
                else:
                    oProgressCom = oVM.delete(None);
            except:
                reporter.logXcpt();
            else:
                oProgress = vboxwrappers.ProgressWrapper(oProgressCom, self.oVBoxMgr, self.oVBox.oTstDrv,
                                                 'Delete VM %s' % (oVM.name));
                oProgress.wait(cMsTimeout = 15*60*1000); # 15 min
                oProgress.logResult();
        else:
            try:    oVM.deleteSettings();
            except: reporter.logXcpt();

        return None;

    #
    # Test execution helpers.
    #

    def test1OneCfg(self, eStorageController, oDskFmt):
        """
        Runs the specified VM thru test #1.

        Returns a success indicator on the general test execution. This is not
        the actual test result.
        """

        (asExts, aTypes) = oDskFmt.describeFileExtensions()
        for i in range(0, len(asExts)): #pylint: disable=consider-using-enumerate
            if aTypes[i] is vboxcon.DeviceType_HardDisk:
                sExt = '.' + asExts[i]
                break

        if sExt is None:
            return False;

        oOrigBaseHd = self.openMedium('5.3/storage/mergeMedium/t-orig.vdi');
        if oOrigBaseHd is None:
            return False;

        #currently only VDI can be resizable. Medium variant is not checked, because testcase creates disks itself
        fFmtDynamic = oDskFmt.id == 'VDI';
        sOrigWithDiffHd = '5.3/storage/mergeMedium/t-fixed.vdi'
        uOrigCrc = long(0x7a417cbb);

        if fFmtDynamic:
            sOrigWithDiffHd = '5.3/storage/mergeMedium/t-resized.vdi';
            uOrigCrc = long(0xa8f5daa3);

        oOrigWithDiffHd = self.openMedium(sOrigWithDiffHd);
        if oOrigWithDiffHd is None:
            return False;

        oVM = self.createTestVM('testvm', 1, None);
        if oVM is None:
            return False;

        sController = self.controllerTypeToName(eStorageController);

        # Reconfigure the VM
        oSession = self.openSession(oVM);
        if oSession is None:
            return False;
        # Attach HD

        fRc = True;
        sFile = 't-base' + sExt;
        sHddPath = os.path.join(self.oVBox.oTstDrv.sScratchPath, sFile);
        oHd = oSession.createBaseHd(sHddPath, sFmt=oDskFmt.id, cb=oOrigBaseHd.logicalSize,
                                    cMsTimeout = 15 * 60 * 1000); # 15 min
        #if oSession.createBaseHd can't create disk because it exists, oHd will point to some stub object anyway
        fRc = fRc and oHd is not None and (oHd.logicalSize == oOrigBaseHd.logicalSize);
        fRc = fRc and self.cloneMedium(oOrigBaseHd, oHd);

        fRc = fRc and oSession.ensureControllerAttached(sController);
        fRc = fRc and oSession.setStorageControllerType(eStorageController, sController);
        fRc = fRc and oSession.saveSettings();
        fRc = fRc and oSession.attachHd(sHddPath, sController, iPort = 0, fImmutable=False, fForceResource=False)

        if fRc:
            oSession.takeSnapshot('Base snapshot');
            oSnapshot = oSession.findSnapshot('Base snapshot');

            if oSnapshot is not None:
                oSnapshotMedium = self.getSnapshotMedium(oSnapshot, sController);
                fRc = oSnapshotMedium is not None;

                if fFmtDynamic:
                    fRc = fRc and self.resizeMedium(oSnapshotMedium, oOrigWithDiffHd.logicalSize);
                fRc = fRc and self.cloneMedium(oOrigWithDiffHd, oSnapshotMedium);
                fRc = fRc and oSession.deleteSnapshot(oSnapshot.id, cMsTimeout = 120 * 1000);

                if fRc:
                    # disk for result test by checksum
                    sResFilePath = os.path.join(self.oVBox.oTstDrv.sScratchPath, 't_res.vmdk');
                    sResFilePathRaw = os.path.join(self.oVBox.oTstDrv.sScratchPath, 't_res-flat.vmdk');
                    oResHd = oSession.createBaseHd(sResFilePath, sFmt='VMDK', cb=oOrigWithDiffHd.logicalSize,
                                                   tMediumVariant = (vboxcon.MediumVariant_Fixed, ),
                                                   cMsTimeout = 15 * 60 * 1000); # 15 min
                    fRc = oResHd is not None;
                    fRc = fRc and self.cloneMedium(oHd, oResHd);

                    uResCrc32 = long(0);
                    if fRc:
                        uResCrc32 = long(utils.calcCrc32OfFile(sResFilePathRaw));
                        if uResCrc32 == uOrigCrc:
                            reporter.log('Snapshot merged successfully. Crc32 is correct');
                            fRc = True;
                        else:
                            reporter.error('Snapshot merging failed. Crc32 is invalid');
                            fRc = False;

                    self.oVBox.deleteHdByMedium(oResHd);

        if oSession is not None:
            if oHd is not None:
                oSession.detachHd(sController, iPort = 0, iDevice = 0);

            oSession.saveSettings(fClose = True);
            if oHd is not None:
                self.oVBox.deleteHdByMedium(oHd);

        self.deleteVM(oVM);
        return fRc;

    def test1(self):
        """
        Executes test #1 thru the various configurations.
        """
        if not self.importVBoxApi():
            return False;

        sVmName = 'testvm';
        reporter.testStart(sVmName);

        aoDskFmts = self.oVBoxMgr.getArray(self.oVBox.systemProperties, 'mediumFormats')
        if aoDskFmts is None or not aoDskFmts:
            return False;

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

            for oDskFmt in aoDskFmts:
                if oDskFmt.id in self.asDiskFormats:
                    reporter.testStart('%s' % (oDskFmt.id));
                    fRc = self.test1OneCfg(eStorageCtrl, oDskFmt);
                    reporter.testDone();
                    if not fRc:
                        break;

            reporter.testDone();
            if not fRc:
                break;

        reporter.testDone();
        return fRc;

if __name__ == '__main__':
    sys.exit(tdStorageSnapshot().main(sys.argv));
