#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdCpuIemInstr1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Test that runs various benchmarks.
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
from testdriver import vbox;
from testdriver import vboxtestvms;


class IemTestVm(vboxtestvms.BootSectorTestVm):
    """
    A Boot Sector Test VM which is configured to run in IEM mode only.
    """

    def __init__(self, oSet, oTestDriver, sVmName, asVirtModesSup = None, f64BitRequired = True):
        vboxtestvms.BootSectorTestVm.__init__(self,
                                              oSet,
                                              'tst-' + sVmName,
                                              os.path.join(oTestDriver.sVBoxBootSectors, sVmName + '.img'),
                                              asVirtModesSup,
                                              f64BitRequired);

    def _childVmReconfig(self, oTestDrv, oVM, oSession):
        _ = oTestDrv;
        _ = oVM;

        # Make sure the testcase runs in a sensible timeframe but we still excercise the recompiler.
        fRc =         oSession.setExtraData('VBoxInternal/Devices/VMMDev/0/Config/TestingThresholdNativeRecompiler', '2');
        fRc = fRc and oSession.setExtraData('VBoxInternal/IEM/NativeRecompileAtUsedCount', '1');

        return fRc;

class tdCpuIemInstr1(vbox.TestDriver):
    """
    CPU IEM instruction testcase #1.
    """

    def __init__(self):
        vbox.TestDriver.__init__(self);

        self.oTestVmSet.aoTestVms.extend([
            IemTestVm(self.oTestVmSet, self, 'bs3-cpu-basic-2'),
            IemTestVm(self.oTestVmSet, self, 'bs3-cpu-basic-3'),

            # @todo r=aeichner Fails currently in IEM
            #IemTestVm(self.oTestVmSet, self, 'bs3-cpu-decoding-1'),

            IemTestVm(self.oTestVmSet, self, 'bs3-cpu-generated-1'),
            IemTestVm(self.oTestVmSet, self, 'bs3-cpu-instr-2'),
            IemTestVm(self.oTestVmSet, self, 'bs3-cpu-instr-3'),
            IemTestVm(self.oTestVmSet, self, 'bs3-cpu-state64-1'),
            IemTestVm(self.oTestVmSet, self, 'bs3-cpu-weird-1'),
            IemTestVm(self.oTestVmSet, self, 'bs3-fpustate-1'),
        ]);


    #
    # Overridden methods.
    #


    def actionConfig(self):
        self._detectValidationKit();
        return self.oTestVmSet.actionConfig(self);

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

        fRc = False

        # Set up the result file
        sXmlFile = self.prepareResultFile();
        asEnv = [ 'IPRT_TEST_FILE=' + sXmlFile, ];

        # Do the test:
        self.logVmInfo(oVM);
        oSession = self.startVm(oVM, sName = oTestVm.sVmName, asEnv = asEnv);
        if oSession is not None:
            cMsTimeout = 30 * 60000;
            if not reporter.isLocal(): ## @todo need to figure a better way of handling timeouts on the testboxes ...
                cMsTimeout = self.adjustTimeoutMs(180 * 60000);

            oRc = self.waitForTasks(cMsTimeout);
            if oRc == oSession:
                fRc = oSession.assertPoweredOff();
            else:
                reporter.error('oRc=%s, expected %s' % (oRc, oSession));

            reporter.addSubXmlFile(sXmlFile);
            self.terminateVmBySession(oSession);

        return fRc;



if __name__ == '__main__':
    sys.exit(tdCpuIemInstr1().main(sys.argv));

