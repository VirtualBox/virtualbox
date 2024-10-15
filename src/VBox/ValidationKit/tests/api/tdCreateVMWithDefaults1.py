#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdCreateVMWithDefaults1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Create VM with IMachine::applyDefaults() Test
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
from testdriver import base
from testdriver import reporter;
from testdriver import vboxcon;


class SubTstDrvCreateVMWithDefaults1(base.SubTestDriverBase):
    """
    Sub-test driver for VM Move Test #1.
    """

    def __init__(self, oTstDrv):
        base.SubTestDriverBase.__init__(self, oTstDrv, 'create-vm-with-defaults', 'Create VMs with defaults');

    def testIt(self):
        """
        Execute the sub-testcase.
        """
        reporter.log('ValidationKit folder is "%s"' % (g_ksValidationKitDir,))
        reporter.testStart(self.sTestName);
        fRc = self.testCreateVMWithDefaults();
        reporter.testDone();
        return fRc;


    def createVMWithDefaults(self, sGuestType):
        sName = 'testvm_%s' % (sGuestType)
        # create VM manually, because the self.createTestVM() makes registration inside
        # the method, but IMachine::applyDefault() must be called before machine is registered
        try:
            if self.oTstDrv.fpApiVer >= 4.2: # Introduces grouping (third parameter, empty for now).
                oVM = self.oTstDrv.oVBox.createMachine("", sName, [],
                                                       self.oTstDrv.tryFindGuestOsId(sGuestType),
                                                       "")
            elif self.oTstDrv.fpApiVer >= 4.0:
                oVM = self.oTstDrv.oVBox.createMachine("", sName,
                                                       self.oTstDrv.tryFindGuestOsId(sGuestType),
                                                       "", False)
            elif self.oTstDrv.fpApiVer >= 3.2:
                oVM = self.oTstDrv.oVBox.createMachine(sName,
                                                       self.oTstDrv.tryFindGuestOsId(sGuestType),
                                                       "", "", False)
            else:
                oVM = self.oTstDrv.oVBox.createMachine(sName,
                                                       self.oTstDrv.tryFindGuestOsId(sGuestType),
                                                       "", "")
            try:
                oVM.saveSettings()
            except:
                reporter.logXcpt()
                if self.oTstDrv.fpApiVer >= 4.0:
                    try:
                        if self.oTstDrv.fpApiVer >= 4.3:
                            oProgress = oVM.deleteConfig([])
                        else:
                            oProgress = oVM.delete(None);
                        self.oTstDrv.waitOnProgress(oProgress)
                    except:
                        reporter.logXcpt()
                else:
                    try:    oVM.deleteSettings()
                    except: reporter.logXcpt()
                raise
        except:
            reporter.errorXcpt('failed to create vm "%s"' % (sName))
            return None

        if oVM is None:
            return False

        # apply settings
        fRc = True
        try:
            if self.oTstDrv.fpApiVer >= 6.1:
                oVM.applyDefaults('')
                oVM.saveSettings();
            self.oTstDrv.oVBox.registerMachine(oVM)
        except:
            reporter.logXcpt()
            fRc = False

        # Some errors from applyDefaults can be observed only after further settings saving.
        # Change and save the size of the VM RAM as simple setting change.
        oSession = self.oTstDrv.openSession(oVM)
        if oSession is None:
            fRc = False

        if fRc:
            try:
                oSession.memorySize = 4096
                oSession.saveSettings(True)
            except:
                reporter.logXcpt()
                fRc = False

        # delete VM
        try:
            oVM.unregister(vboxcon.CleanupMode_DetachAllReturnNone)
        except:
            reporter.logXcpt()

        if self.oTstDrv.fpApiVer >= 4.0:
            try:
                if self.oTstDrv.fpApiVer >= 4.3:
                    oProgress = oVM.deleteConfig([])
                else:
                    oProgress = oVM.delete(None)
                self.oTstDrv.waitOnProgress(oProgress)

            except:
                reporter.logXcpt()

        else:
            try:    oVM.deleteSettings()
            except: reporter.logXcpt()

        return fRc

    def testCreateVMWithDefaults(self):
        """
        Test create VM with defaults.
        """
        if not self.oTstDrv.importVBoxApi():
            return reporter.error('importVBoxApi');

        # Get the guest OS types.
        try:
            aoGuestTypes = self.oTstDrv.oVBoxMgr.getArray(self.oTstDrv.oVBox, 'guestOSTypes')
            if aoGuestTypes is None or not aoGuestTypes:
                return reporter.error('No guest OS types');
        except:
            return reporter.errorXcpt();

        # Create VMs with defaults for each of the guest types.
        fRc = True
        for oGuestType in aoGuestTypes:
            try:
                sGuestType = oGuestType.id;
            except:
                fRc = reporter.errorXcpt();
            else:
                reporter.testStart(sGuestType);
                fRc = self.createVMWithDefaults(sGuestType) and fRc;
                reporter.testDone();
        return fRc

if __name__ == '__main__':
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    from tdApi1 import tdApi1; # pylint: disable=relative-import
    sys.exit(tdApi1([SubTstDrvCreateVMWithDefaults1]).main(sys.argv))

