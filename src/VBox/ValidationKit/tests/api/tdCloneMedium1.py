#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdCloneMedium1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Clone Medium Test #1
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
import platform

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0]
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(g_ksValidationKitDir)

# Validation Kit imports.
from common     import utils
from testdriver import base
from testdriver import reporter
from testdriver import vboxcon
from testdriver import vboxwrappers


class SubTstDrvCloneMedium1(base.SubTestDriverBase):
    """
    Sub-test driver for Clone Medium Test #1.
    """

    def __init__(self, oTstDrv):
        base.SubTestDriverBase.__init__(self, oTstDrv, 'clone-medium', 'Move Medium');
        # Solaris delivers a 32-bit version of Python 2.7 which can't handle signed values
        # above 0x7fffffff (MAXINT) such as 0xdeadbeef.  N.B. If this ever needs to be
        # extended to other OSes the Python docs:
        # https://docs.python.org/3/library/platform.html#platform.architecture
        # mention that the platform.architecture() call can be wrong on macOS.
        if utils.getHostOs() == 'solaris' and sys.version_info[0] < 3 and platform.architecture()[0] == '32bit':
            self.iDataToWrite = 0x0badcafe;
        else:
            self.iDataToWrite = 0xdeadbeef;

    def testIt(self):
        """
        Execute the sub-testcase.
        """

        return self.testAll()

    #
    # Test execution helpers.
    #

    def createTestMedium(self, oVM, sPathSuffix, sFmt = 'VDI', cbSize = 1024*1024, data = None):
        assert oVM is not None

        oSession = self.oTstDrv.openSession(oVM)

        if oSession is None:
            return False

        #
        # Create Medium Object
        #

        sBaseHdd1Path = os.path.join(self.oTstDrv.sScratchPath, sPathSuffix)
        sBaseHdd1Fmt = sFmt
        cbBaseHdd1Size = cbSize

        try:
            oBaseHdd1 = oSession.createBaseHd(sBaseHdd1Path, sBaseHdd1Fmt, cbBaseHdd1Size)
        except:
            return reporter.errorXcpt('createBaseHd failed')

        oMediumIOBaseHdd1 = oBaseHdd1.openForIO(True, "")

        if data:
            cbWritten = oMediumIOBaseHdd1.write(0, data)

            if cbWritten != 1:
                return reporter.error("Failed writing to test hdd.")

        oMediumIOBaseHdd1.close()

        return oBaseHdd1

    def cloneMedium(self, oSrcHd, oTgtHd):
        """
        Clones medium into target medium.
        """
        try:
            oProgressCom = oSrcHd.cloneTo(oTgtHd, (vboxcon.MediumVariant_Standard, ), None);
        except:
            reporter.errorXcpt('failed to clone medium %s to %s' % (oSrcHd.name, oTgtHd.name));
            return False;
        oProgress = vboxwrappers.ProgressWrapper(oProgressCom, self.oTstDrv.oVBoxMgr, self.oTstDrv,
                                                 'clone base disk %s to %s' % (oSrcHd.name, oTgtHd.name));
        oProgress.wait(cMsTimeout = 15*60*1000); # 15 min
        oProgress.logResult();
        return True;

    def resizeAndCloneMedium(self, oSrcHd, oTgtHd, cbTgtSize):
        """
        Clones medium into target medium.
        """

        try:
            oProgressCom = oSrcHd.resizeAndCloneTo(oTgtHd, cbTgtSize, (vboxcon.MediumVariant_Standard, ), None);
        except:
            reporter.errorXcpt('failed to resize and clone medium %s to %s and to size %d' \
                               % (oSrcHd.name, oTgtHd.name, cbTgtSize));
            return False;
        oProgress = vboxwrappers.ProgressWrapper(oProgressCom, self.oTstDrv.oVBoxMgr, self.oTstDrv,
                                                 'resize and clone base disk %s to %s and to size %d' \
                                                 % (oSrcHd.name, oTgtHd.name, cbTgtSize));
        oProgress.wait(cMsTimeout = 15*60*1000); # 15 min
        oProgress.logResult();
        return True;

    def deleteVM(self, oVM):
        try:
            oVM.unregister(vboxcon.CleanupMode_DetachAllReturnNone);
        except:
            reporter.logXcpt();

        try:
            oProgressCom = oVM.deleteConfig([]);
        except:
            reporter.logXcpt();
        else:
            oProgress = vboxwrappers.ProgressWrapper(oProgressCom, self.oTstDrv.oVBoxMgr, self.oTstDrv,
                                                'Delete VM %s' % (oVM.name));
            oProgress.wait(cMsTimeout = 15*60*1000); # 15 min
            oProgress.logResult();

        return None;

    #
    # Tests
    #

    def testCloneOnly(self):
        """
        Tests cloning mediums only. No resize.
        """

        reporter.testStart("testCloneOnly")

        oVM = self.oTstDrv.createTestVM('test-medium-clone-only', 1, None, 4)

        hd1 = self.createTestMedium(oVM, "hd1-cloneonly", data=[self.iDataToWrite])
        hd2 = self.createTestMedium(oVM, "hd2-cloneonly")

        if not self.cloneMedium(hd1, hd2):
            return False

        oMediumIOhd1 = hd1.openForIO(True, "")
        dataHd1 = oMediumIOhd1.read(0, 4)
        oMediumIOhd1.close()

        oMediumIOhd2 = hd2.openForIO(True, "")
        dataHd2 = oMediumIOhd2.read(0, 4)
        oMediumIOhd2.close()

        if dataHd1 != dataHd2:
            reporter.testFailure("Data read is unexpected.")

        self.deleteVM(oVM)

        reporter.testDone()
        return True

    def testResizeAndClone(self):
        """
        Tests resizing and cloning mediums only.
        """

        reporter.testStart("testResizeAndClone")

        oVM = self.oTstDrv.createTestVM('test-medium-resize-and-clone', 1, None, 4)

        hd1 = self.createTestMedium(oVM, "hd1-resizeandclone", data=[self.iDataToWrite])
        hd2 = self.createTestMedium(oVM, "hd2-resizeandclone")

        if not (hasattr(hd1, "resizeAndCloneTo") and callable(getattr(hd1, "resizeAndCloneTo"))):
            self.deleteVM(oVM)
            reporter.testDone()
            return True

        if not self.resizeAndCloneMedium(hd1, hd2, 1024*1024*2):
            return False

        oMediumIOhd1 = hd1.openForIO(True, "")
        dataHd1 = oMediumIOhd1.read(0, 4)
        oMediumIOhd1.close()

        oMediumIOhd2 = hd2.openForIO(True, "")
        dataHd2 = oMediumIOhd2.read(0, 4)
        oMediumIOhd2.close()

        if dataHd1 != dataHd2:
            reporter.testFailure("Data read is unexpected.")

        if hd2.logicalSize not in (hd1.logicalSize, 1024*1024*2):
            reporter.testFailure("Target medium did not resize.")

        self.deleteVM(oVM)

        reporter.testDone()
        return True

    def testCloneToBase(self):
        """
        Tests cloning diff to base
        """

        reporter.testStart("testCloneToBase")

        try:
            oVM = self.oTstDrv.createTestVM('test-medium-clone-base', 1, None, 4)
            assert oVM is not None

            fRc = True
            oSession = self.oTstDrv.openSession(oVM)
            cImages = 10
            reporter.log('Creating chain with %d disk images' % (cImages))
            sHddPath = os.path.join(self.oTstDrv.sScratchPath, 'CloneTest1.vdi')
            hd1 = oSession.createBaseHd(sHddPath, cb=1024*1024)
            if hd1 is None:
                fRc = False
            for i in range(2, cImages + 1):
                sHddPath = os.path.join(self.oTstDrv.sScratchPath, 'CloneTest' + str(i) + '.vdi')
                if i == 2:
                    oHd = oSession.createDiffHd(hd1, sHddPath)
                else:
                    oHd = oSession.createDiffHd(oHd, sHddPath)
                if oHd is None:
                    fRc = False
                    break


            # modify the VM config, attach HDD
            sController='SATA Controller'
            fRc = fRc and oSession.attachHd(sHddPath, sController, fImmutable=False, fForceResource=False)
            fRc = fRc and oSession.saveSettings()

            try:
                oProgressCom = oHd.cloneTo(hd1, (vboxcon.MediumVariant_Standard, ), None);
            except:
                reporter.errorXcpt('failed to clone medium %s to %s' % (oHd.name, hd1.name));
                return False;
            oProgress = vboxwrappers.ProgressWrapper(oProgressCom, self.oTstDrv.oVBoxMgr, self.oTstDrv,
                                                     'clone disk %s to base disk %s' % (oHd.name, hd1.name));
            oProgress.wait(cMsTimeout = 15*60*1000); # 15 min
            oProgress.logResult();

            fRc = oSession.close() and fRc
            self.deleteVM(oVM)

        except:
            reporter.errorXcpt()

        return reporter.testDone()[1] == 0

    def testAll(self):
        if self.oTstDrv.fpApiVer >= 7.1 and self.oTstDrv.uRevision > 157873:
            return self.testCloneOnly() & self.testResizeAndClone() & self.testCloneToBase()
        return self.testCloneOnly() & self.testResizeAndClone()

if __name__ == '__main__':
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    from tdApi1 import tdApi1; # pylint: disable=relative-import
    sys.exit(tdApi1([SubTstDrvCloneMedium1]).main(sys.argv))
