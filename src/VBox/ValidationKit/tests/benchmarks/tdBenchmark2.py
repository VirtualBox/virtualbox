#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdBenchmark2.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

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
from testdriver import vboxcon;
from testdriver import vboxtestvms;


class tdBenchmark2(vbox.TestDriver):
    """
    Benchmark #2 - Memory.
    """

    def __init__(self):
        vbox.TestDriver.__init__(self);
        oTestVm = vboxtestvms.BootSectorTestVm(self.oTestVmSet, 'tst-bs-memalloc-1',
                                               os.path.join(self.sVBoxBootSectors, 'bs3-memalloc-1.img'));
        self.oTestVmSet.aoTestVms.append(oTestVm);


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
        fRc = False;

        #
        # Determin the RAM configurations we want to test.
        #
        cMbMaxGuestRam = self.oVBox.systemProperties.maxGuestRAM;
        cMbHostAvail   = self.oVBox.host.memoryAvailable;
        cMbHostTotal   = self.oVBox.host.memorySize;
        reporter.log('cMbMaxGuestRam=%s cMbHostAvail=%s cMbHostTotal=%s' % (cMbMaxGuestRam, cMbHostAvail, cMbHostTotal,));

        cMbHostAvail -= cMbHostAvail // 7; # Rough 14% safety/overhead margin.
        if cMbMaxGuestRam < cMbHostAvail:
            # Currently: 2048 GiB, 1536 GiB, 1024 GiB, 512 GiB, 256 GiB, 128 GiB, 64 GiB, 32 GiB
            acMbRam = [ cMbMaxGuestRam, cMbMaxGuestRam // 4 * 3, cMbMaxGuestRam // 2, cMbMaxGuestRam // 4,
                        cMbMaxGuestRam // 8, cMbMaxGuestRam // 16  ];
            if acMbRam[-1] > 64*1024:
                acMbRam.append(64*1024);
            if acMbRam[-1] > 32*1024:
                acMbRam.append(32*1024);
        elif cMbHostAvail > 8*1024:
            # First entry is available memory rounded down to the nearest 8 GiB
            cMbHostAvail = cMbHostAvail & ~(8 * 1024 - 1);
            acMbRam = [ cMbHostAvail, ];

            # The remaining entries are powers of two below that, up to 6 of these stopping at 16 GiB.
            cMb = 8*1024;
            while cMb < cMbHostAvail:
                cMb *= 2;
            while len(acMbRam) < 7 and cMb > 16 * 1024:
                cMb //= 2;
                acMbRam.append(cMb);
        elif cMbHostAvail >= 16000 and cMbHostAvail > 7168:
            # Desperate attempt at getting some darwin testruns too.  We've got two
            # with 16 GiB and they usually end up with just short of 8GiB of free RAM.
            acMbRam = [7168,];
        else:
            reporter.log("Less than 8GB of host RAM available for VMs, skipping test");
            return None;
        reporter.log("RAM configurations: %s" % (acMbRam));

        # Large pages only work with nested paging.
        afLargePages = [False, ];
        try:
            fNestedPaging = False;
            if self.fpApiVer >= 7.1:
                fNestedPaging = oVM.platform.x86.getHWVirtExProperty(vboxcon.HWVirtExPropertyType_NestedPaging);
            else:
                fNestedPaging = oVM.getHWVirtExProperty(vboxcon.HWVirtExPropertyType_NestedPaging);

            if fNestedPaging:
                afLargePages = [True, False];
        except:
            return reporter.errorXcpt("Failed to get HWVirtExPropertyType_NestedPaging");

        #
        # Test the RAM configurations.
        #
        for fLargePages in afLargePages:
            sLargePages = 'large pages' if fLargePages is True else 'no large pages';
            for cMbRam in acMbRam:
                reporter.testStart('%s MiB, %s' % (cMbRam, sLargePages));

                # Reconfigure the VM:
                fRc = False
                oSession = self.openSession(oVM);
                if oSession:
                    fRc = oSession.setRamSize(cMbRam);
                    fRc = oSession.setLargePagesX86(fLargePages) and fRc;
                    if fRc:
                        fRc = oSession.saveSettings();
                    if not fRc:
                        oSession.discardSettings(True);
                    oSession.close();
                if fRc:
                    # Set up the result file
                    sXmlFile = self.prepareResultFile();
                    asEnv = [ 'IPRT_TEST_FILE=' + sXmlFile, ];

                    # Do the test:
                    self.logVmInfo(oVM);
                    oSession = self.startVm(oVM, sName = oTestVm.sVmName, asEnv = asEnv);
                    if oSession is not None:
                        cMsTimeout = 15 * 60000 + cMbRam // 168;
                        if not reporter.isLocal(): ## @todo need to figure a better way of handling timeouts on the testboxes ...
                            cMsTimeout = self.adjustTimeoutMs(180 * 60000 + cMbRam // 168);

                        oRc = self.waitForTasks(cMsTimeout);
                        if oRc == oSession:
                            fRc = oSession.assertPoweredOff();
                        else:
                            reporter.error('oRc=%s, expected %s' % (oRc, oSession));

                        reporter.addSubXmlFile(sXmlFile);
                        self.terminateVmBySession(oSession);
                else:
                    reporter.errorXcpt("Failed to set memory size to %s MiB or setting largePages to %s" % (cMbRam, fLargePages));
                reporter.testDone();

        return fRc;



if __name__ == '__main__':
    sys.exit(tdBenchmark2().main(sys.argv));

